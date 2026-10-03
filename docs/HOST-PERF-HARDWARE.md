# HOST-PERF-HARDWARE.md — using more of the Fold7's silicon for the one thread that matters

Scope: Dungeon Hunter 2 HD v1.0.2 (armeabi-v7a) on the Galaxy Z Fold7 (**SM-F966B**, Snapdragon 8
Elite, Android 16) through this fork's Dynarmic AArch32→AArch64 host. Two changes were attempted:
**(1)** CPU affinity + priority for the guest's GL/render thread and **(2)** a programmatic request
for the panel's 120 Hz mode. **Change 1 is implemented, applied on device and measured. Change 2 is
implemented, issued, and refused by the platform — measured, with the reason.** Every number below
was read off the device during this work; nothing here is a projection.

Fork under test: `…\DH2Work-toolchain\host\shrink\ZettaBridge` (the shrunk, tailored host), built to
`libzbridge.so` 3 015 240 B / `b6bfe0e4a429665c252adffc096a32a4c71e3cd73431d0fa248c8f9603c2510d`
and packed by `fold7-build\build_apk.py` as `1.0-perf-hw1001` (v1001).

---

## 0. Result

| change | state | evidence |
| --- | --- | --- |
| **1. Render-thread affinity + priority** | **implemented and applied** | topology read from the device; thread pinned to **cpu6** (cap 1024, 4 473 600 kHz); `renice -8` accepted; verified from outside the app with `/proc/<tid>/status` → `Cpus_allowed_list: 6` (vs `0-7` unpinned) and `lastcpu=6` |
| **1. its in-game effect** | **+1.0 FPS (+1.8 %) in the only phase below the vsync ceiling; 0 in the ceiling phase** | interleaved reversal A/B, 8 blocks, one session: pin=0 56.72 mean vs pin=1 57.67 mean on the matched adjacent blocks; 60.02 vs 60.01 where the engine is already at the 60 Hz ceiling |
| **2. 120 Hz request** | **implemented, issued, refused by the platform** | `preferredDisplayModeId`/`preferredRefreshRate` on the guest window **and** `Surface.setFrameRate(120, FIXED_SOURCE)` on every `SurfaceView`; `mActiveModeId` stayed 3 (60.000004 Hz); the display's own mode spec caps apps at 60 (`primary=physical: (10.0 60.0)`), and even a shell-level `cmd display set-user-preferred-display-mode 1968 2184 120.00001 0` persisted the preference without moving the active mode. The launcher Java for this is **removed again** (see §4). |

---

## 1. The topology, measured on the device (not assumed)

```
/sys/devices/system/cpu/cpu0..5/cpufreq/cpuinfo_max_freq = 3532800   cpu_capacity = 765
/sys/devices/system/cpu/cpu6..7/cpufreq/cpuinfo_max_freq = 4473600   cpu_capacity = 1024
```

Read out of the app itself at run time (logcat, tag `zbridge`):

```
perf: render thread tid=5813 allowed=8 cpus 0-7 nice=0
perf: topology cpu0-5 cap765 3532800kHz | cpu6-7 cap1024 4473600kHz -> fastest cpu6 (cap1024 4473600kHz)
```

`allowed=8 cpus 0-7` is what `sched_getaffinity` reports for the app; the candidate set is
intersected with it, so the choice is always a CPU this process may actually use.

## 2. Change 1 — how the thread is identified and moved

**Identification, not a guess.** The engine presents through Java's `GLSurfaceView` and reaches GL
only through the host's svc-trapped host calls, so **the thread that executes `gl*` is the render
thread**. `HostGl::handle_host_call` is that observation point, and it is the same thread the
engine's own trace prints (`render returned … tid=`). The first thread to reach it is claimed for
the process; the claim and its tid are logged for the device check.

* `core/include/zb/thread_perf.h`, `core/src/thread_perf.cpp` (new).
* `core/src/gl/host_gl.cpp` — one `note_gl_host_call()` after the `is_gl_host_call` test.
* `core/CMakeLists.txt` — `src/thread_perf.cpp`.

What it does on the claiming thread, once: read the CPU topology from sysfs, pick the highest
`(capacity, max_freq)` CPU, `sched_setaffinity` to that single CPU, then
`setpriority(PRIO_PROCESS, 0, nice)`.

**Configurable and reversible at run time**, via system properties read on the render thread (one
`__system_property_get` per 4096 GL calls ≈ every 6–7 frames):

| property | default | effect |
| --- | --- | --- |
| `debug.zb.perf.pin` | `1` | `0` disables the policy and **reverts** the thread to its original mask and nice |
| `debug.zb.perf.core` | `-1` | explicit CPU, `-1` = auto (fastest allowed) |
| `debug.zb.perf.nice` | `-8` | nice value (`THREAD_PRIORITY_URGENT_DISPLAY`) |
| `debug.zb.perf.rt` | `0` | `1` also attempts `SCHED_FIFO` priority 1 |

Revert is a real revert: the original `sched_getaffinity` mask and the original `getpriority` value
are captured at claim time and restored. Measured in the A/B session:

```
perf: pin core 6 (cap1024 4473600kHz)
perf: renice -8 (was 0)
perf: revert (debug.zb.perf.pin=0) affinity->8 cpus 0-7 nice->0
perf: pin core 6 (cap1024 4473600kHz)          <- live re-apply, same session, no restart
```

**A failure here cannot break startup.** Every kernel call's return value and `errno` are recorded
and reported; a denial is a logged null result, never an abort. (In this run nothing was denied:
the app is permitted to lower its own nice to −8 — `RLIMIT_NICE` allows it.)

## 3. Change 1 — the measurement

Method: **one app session**, the two arms alternating in short blocks, so content drift and any
co-resident agent's reinstall cannot silently land in one arm only. Reversal design
`0 1 1 0 0 1 1 0`: a linear drift in the content (opening cutscene → level → heavier area) cancels,
because the pin=0 blocks {1,4,5,8} and the pin=1 blocks {2,3,6,7} are symmetric about the middle.
FPS of a batch = `Δframes / (ΔelapsedMs / 1000)` over consecutive `render returned frames=N
elapsedMs=M` lines from the engine's own trace
(`/sdcard/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/dh2-events.txt`).
The intro video is not a sample: while `MyVideoView` is up the engine is paused and writes no
render lines at all.

Every block, with the kernel's own view of the thread (read from a shell, outside the app):

```
block 1 pin=0 tid=5813 [Cpus_allowed_list: 0-7 lastcpu=4] fps=55.53,55.4,57.25,56.68,57.94,57.31,57.01,56.6
block 2 pin=1 tid=5813 [Cpus_allowed_list: 6   lastcpu=6] fps=57.97,58.11,57.31,57.69,57.75,57.69,57.86,56.98
block 3 pin=1 tid=5813 [Cpus_allowed_list: 6   lastcpu=6] fps=57.33,57.66,57.01,59.26,12.12,23.4,60.06,60
block 4 pin=0 tid=5813 [Cpus_allowed_list: 0-7 lastcpu=1] fps=60.03,60.06,60.06,59.91,60.12,60.06,59.97,60.09
block 5 pin=0 tid=5813 [Cpus_allowed_list: 0-7 lastcpu=4] fps=60,60.03,60.06,60,60.03,60.03,60.03,60
block 6 pin=1 tid=5813 [Cpus_allowed_list: 6   lastcpu=6] fps=60.03,60.03,60.03,60.06,59.97,60.03,60.03,60.03
block 7 pin=1 tid=5813 [Cpus_allowed_list: 6   lastcpu=6] fps=60.03,60.03,60.03,60.03,60,60.06,60,54.89
block 8 pin=0 [process already replaced by another agent's install; not a sample]
```

| subset | n | mean | median | min | max |
| --- | --- | --- | --- | --- | --- |
| pin=0, blocks 1+4+5 | 24 | 58.92 | 60.00 | 55.40 | 60.12 |
| pin=1, blocks 2+3+6+7 | 32 | 56.36 | 59.98 | 12.12 | 60.06 |
| **matched adjacent pair 1 vs 2** (only phase below the ceiling) | 8 / 8 | **56.72 / 57.67** | 56.84 / 57.72 | 55.40 / 56.98 | 57.94 / 58.11 |
| ceiling phase, blocks 4+5 vs 6+7 | 16 / 16 | **60.02 / 60.01** | 60.03 / 60.03 | 59.91 / 54.89 | 60.12 / 60.06 |

**Reading.** The affinity and the priority are applied, live-reconfigurable and verifiably in force
(`Cpus_allowed_list: 6`, `lastcpu=6`, nice 0 → −8 → 0). On the content this save reaches, the
engine spends most of the session pinned against the 60 Hz vsync ceiling — 33 of the 56 valid
batches sit at 59.9–60.1 FPS — and there the pin changes **nothing** (60.02 vs 60.01). In the one
phase that is below the ceiling the pinned arm is **+1.0 FPS / +1.8 %** and better at both ends of
its range, which is inside the run-to-run spread of a single 8-batch block (this save's content
moves between blocks: the pinned arm's own block means run 57.7 / 48.4 / 60.0 / 59.4 as the scene
changes). **Verdict: no measurable harm, a small improvement in the right direction, not
distinguishable from noise at this sample size.** The one large excursion in block 3 (12.12 and
23.4 FPS) is a ~10-second stall inside a pinned block and is not attributable to the pin: it spans
two consecutive batches, and the same content was not sampled unpinned.

The project's earlier in-game baseline (28.9 average / 36.6 median / 9.0–54.4 range, measured on
the shipped shrunk build) is **not comparable** to these numbers: it was taken in a heavy gameplay
area, whereas the save reachable now opens on the intro cutscene, whose frame cost is at the vsync
ceiling. Same method, different content.

## 4. Change 2 — the 120 Hz request, and why it cannot be granted here

The request was implemented in `GuestWindowStyle.afterCreate` (which this fork runs *after* the
guest's `onCreate`, on the guest's own `Stubs$SingleInstance` window) by two independent routes,
because which one an OEM build honours is not knowable from the docs:

1. `WindowManager.LayoutParams.preferredDisplayModeId` / `preferredRefreshRate`, selecting from
   `Display.getSupportedModes()` the highest rate ≥120 at the *same* physical size, so no rescaling;
2. `Surface.setFrameRate(120f, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)` on every `SurfaceView` of
   the decor view, re-applied from `SurfaceHolder.Callback` when the surface is created or resized.

Both ran (logcat, tag `zb-launcher`):

```
refresh: requested 120.0Hz; current mode 3 @60.000004Hz 1968x2184; modes=5; asked for mode -1
refresh: Surface.setFrameRate(120.0) for mode -1
```

`asked for mode -1` is the platform's answer in the first route: no 120 Hz mode is *offered to this
app* at its current size. The `Surface` route reported success and changed nothing. The display
stayed at `mActiveModeId=3`, `mActiveRenderFrameRate=60.000004`.

The reason, measured:

* the panel's mode spec has a **60 Hz ceiling for apps**:
  `baseModeId=3 allowGroupSwitching=false primary=physical: (10.0 60.0) render: (0.0 60.0) appRequest=physical: (10.0 60.0)`;
* it is not an app-permission or app-API limit: as `shell`,
  `cmd display set-user-preferred-display-mode 1968 2184 120.00001 0` **persisted** the preference
  (`User preferred display mode: 1968 2184 120.00001`) and the active mode still read
  `Mode ID: 3 … 60.00 Hz`; a screen off/on cycle did not change it;
* the vendor display layer only ever programs 24 Hz and 60 Hz —
  `HWDeviceDRM::SetupAtomic: update ActiveConfig. current_vrefresh: 24` (during the intro video) and
  `… current_vrefresh: 60`;
* the Samsung settings that could move it are `secure refresh_rate_mode=0` (Motion smoothness
  *Standard*) and `secure pms_override_refresh_rate_mode=1`; writing them (0/1/2/3) and the
  system-namespace duplicates did not change the mode spec, and **all values were restored**
  afterwards (`refresh_rate_mode=0`, `pms_override_refresh_rate_mode=1`, system-namespace keys
  deleted, `get-user-preferred-display-mode` → `null`).

**Both routes are therefore removed from the launcher again** — a dead code path that also caused
window letterboxing (black bars) on `1.0-perf-hw1000`, reported by the coordinator. The launcher's
`GuestWindowStyle.java` is byte-identical to the pristine file
(`707d872afd8a13c9c8a66c8de6bef4b72e6fc3c8e6002d135b95b680bd54aad6`). The 60 FPS ceiling stands.

## 5. One more thing the host could do, found while measuring

The guest's own thread layer asks the kernel for scheduler priority and the host refuses **silently**
(logcat, both pre-existing):

```
unimplemented syscall sched_setscheduler (156)
unimplemented syscall sched_getparam (155)
unimplemented syscall sched_get_priority_min (160)
unimplemented syscall sched_get_priority_max (159)
```

So the engine *already tries* to raise its render thread's priority and gets nothing. Change 1 does
it from the host side instead. Implementing `NR_sched_setscheduler` in `syscalls.cpp` (host
`sched_setscheduler` on the calling thread when the guest asks, denied → `EPERM` as the kernel
would) would satisfy the guest's own request without the host guessing which thread it means — and
`debug.zb.perf.rt=1` already attempts `SCHED_FIFO` priority 1 on the identified render thread, which
was **not** measured in this run.

## 6. What did not work, and what is not claimed

* **120 Hz — dead on this device.** Refused by the platform on every route tried, including a
  privileged one. Nothing app-side can lift it; the fork's `preferredDisplayModeId` /
  `Surface.setFrameRate` code is deleted.
* **A frame-rate gain from pinning was not demonstrated.** In the ceiling phase it is exactly zero;
  below the ceiling it is +1.8 %, inside the noise of this sample size.
* The **black screen** observed by the coordinator (game audio responds, screenshot all black) was
  present during the A/B session on `hw1001`, i.e. **with the display-mode code already removed**:
  `screen-block1.png` is uniformly black while the engine's trace advanced at 55–60 FPS, and the
  panel itself was healthy (a home-screen screencap in the same state is 3.87 MB of normal UI, and
  the launcher screen captured minutes later shows no letterboxing). Attribution therefore points
  away from the display-mode request; it is recorded here as an observation, not a conclusion.
* **`SCHED_FIFO` was not attempted** (`debug.zb.perf.rt` left at 0), and no `nice` value other than
  −8 was tried, nor a pin to cpu7 instead of cpu6.
* **Version codes.** This work had to install above a co-resident build that overwrote it twice
  (`1.0-nativehook300`); the artifact on the device is `1.0-perf-hw1001`. The package versions 28/29
  cannot be installed until this is removed, because Android refuses version-code downgrades even
  with `-d` (and `adb uninstall -k` is refused on this build, so a plain uninstall would delete the
  imported 637 MB cache).

## 7. Files changed in the fork

| file | change |
| --- | --- |
| `core/include/zb/thread_perf.h` | new — the policy's interface and the record of what the kernel said |
| `core/src/thread_perf.cpp` | new — topology read, CPU choice, `sched_setaffinity`, `setpriority`, property-gated apply/revert |
| `core/src/gl/host_gl.cpp` | one `note_gl_host_call()` in `handle_host_call` (the render-thread observation point) |
| `core/CMakeLists.txt` | `src/thread_perf.cpp` added to `zbcore` |
| `android/…/launcher/GuestWindowStyle.java` | 120 Hz attempt added, then **reverted** (file is pristine) |

Supporting scripts and raw evidence (this fork's own directories):
`host\perf\scripts\{build-perf.sh,build-apk.ps1,arm.ps1,ab-pin.ps1,measure-fps.ps1}`,
`host\perf\evidence\run-abpin-hw1001\` (per-block traces, screenshots, `measure.txt`),
`host\perf\apk\dh2-perf-hw1001.apk` (the measured artifact),
`host\perf\apk\dh2-perf-hw1102.apk` (same source, pre-built for the next install slot),
`host\perf\lib\libzbridge-perf.so` (`b6bfe0e4…` / 3 015 240 B).
The build's stage slot (`…\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so`)
was restored to the shipped `25e8da7e…682d24` / 3 306 992 B, and the stage launcher Java to
`707d872a…4aad6`.
