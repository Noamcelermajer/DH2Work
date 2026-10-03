# Physical-device validation — Galaxy Z Fold7 (SM-F966B)

All results below are from the connected device, adb-driven, on real hardware — not an
emulator. Device: **SM-F966B**, Android 16, `arm64-v8a`, **4096-byte pages**.

## Build progression

| Build | versionCode / name | Engine | APK SHA-256 | Result on device |
| --- | --- | --- | --- | --- |
| r1 | 15 / `1.0-work1` | `45891aad…` | `dfc6d3cd…` | installs, launches, cache imports |
| r1-standalone | 15 / `1.0-work1-standalone` | `45891aad…` | — | bundled cache self-imports, 635 MB |
| r2-orguard | 16 / `1.0-work2-orguard` | `45891aad…` | `8e8901ac…` | **startup crash fixed** |
| r3-profile | 17 / `1.0-work3-profile` | `45891aad…` | — | profileable build; long-run crash found |
| **r4-sync** | **18 / `1.0-work4-sync`** | **`ad33304f…`** | `f03fd8ec…` | **long-run crash fixed** |

## What is verified

**Startup reliability.** Eight consecutive automated launches (force-stop → launch → tap
LAUNCH GAME → check at 28 s): **8 survived, 0 died, 0 SIGSEGV**. Before the fix the same
fault killed four of five launches. It was the accelerometer listener calling
`nativeSetOrientation` from `onSensorChanged` before the engine's globals existed, faulting
on an unguarded null global at engine `0x530a88`.

**Long-session stability.** r4-sync reached **21,720 frames / 834 s (13.9 minutes)** with the
guest alive at ~684 MB RSS and **zero SIGSEGV**. The three runs before it died at **388 s,
428 s and ~440 s** (~6.5–7.1 min). Same automated procedure throughout, so the comparison is
like-for-like. The fix is one ARM instruction at RVA `0x32c534` retargeting a per-frame
`BNE` from a thread-spawning path to the existing synchronous `Savegame::UpdateJobs()` path
at `0x32cc34`.

**Gameplay actually runs.** `prince_modular.bdae` opens as a valid BRES (3,207,072 B,
`42524553 feff0000`), `DH2FileGuard` recovered repeated cache roots 178+ times with no open
failures, the intro cinematic plays, the GL context is created (Adreno 830, GLES 3.2) and is
recreated after the video, and the render loop sustains **~22–30 FPS** (2.17 M pixels).

**16:9 aspect.** With the option enabled on the unfolded inner panel (2184×1968, an aspect of
1.11:1 where distortion would be severe) the surface becomes **2184×1228 — exactly 16:9**,
letterboxed and undistorted, confirmed by screenshot. This closes an item the project had
recorded twice as "not confirmed on a screen".

**Performance characterisation.** ~25.7 ms of single-threaded CPU per frame; one `GLThread`
at ~77% of one core; the 8-core device ~85% idle. The GPU is **not** the bottleneck — forcing
`wm size 1092x984` (4× fewer pixels) changed FPS from 25.15 to 24.48. Profile: **82.7%** of
CPU in JIT'd guest code, **15.1%** in the translator, **0.16%** in the GL driver.

## What is NOT verified

* **Saves, reload, quests, inventory, combat depth, audio, all levels.** Only the opening
  sequence and a sustained render loop were exercised.
* **A human-driven session across several map loads.** The soak is one 14-minute automated
  run; three earlier failures were all in map loading, so this is the interaction most worth
  repeating by hand.
* **Touch alignment under the 16:9 letterbox** — a screenshot cannot show whether input maps
  into the 2184×1228 view or still spans the panel.
* **Cover-display behaviour** with 16:9 enabled, and behaviour across fold/unfold mid-session.
* **16 KB page devices** — this device is 4096-byte pages. The project has separate 16 KiB
  work that is not part of this build.
* **The rebuilt native runtime has never been *run*.** Its `libzbridge.so` is byte-identical
  to the shipped one (`25e8da7e…`), which proves the recipe but says nothing about *edited*
  ZettaBridge code.

## Reproduce

```sh
adb devices -l                                   # expect SM_F966B
adb install -r Dungeon-Hunter-2-DH2Work-r4-sync.apk
adb shell am start -n local.dh2.fold7/com.zettabridge.launcher.Dh2Activity
# tap LAUNCH GAME, or: adb shell input tap 1092 807   (unfolded 2184x1968)
adb shell sh /data/local/tmp/threadcpu.sh 20      # per-thread CPU, root-free
adb shell "grep -E 'VmSize|VmRSS' /proc/$(adb shell pidof local.dh2.fold7:guest)/status"
```

After a crash, reopen the launcher and use **EXPORT DIAGNOSTIC ZIP**; on this device it
lands in `/sdcard/Dh2_crashes/`.
