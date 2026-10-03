# DH2 frame limiter: verdict and exact site

Scope of this note: Dungeon Hunter 2 HD v1.0.2, `libDungeonHunter2.so`
(ELF32/ARM, not stripped, SHA-256
`36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`), running on
an arm64-only Galaxy Z Fold7 (SM-F966B) through ZettaBridge on stock Dynarmic.
Everything below was read from the pristine engine, the decompiled game Java, the
ZettaBridge checkout, and read-only `adb` queries against the connected device.

---

## 1. Verdict

**The cap is the presentation clock (60 Hz display vsync on a 120 Hz panel). It is not an
engine-internal limiter, and there is no engine instruction to patch.**

There is no frame limiter in `libDungeonHunter2.so` at all: no target-frame-duration
constant, no timing accumulator gate, no sleep on the frame path, no EGL call. The guest
emits exactly one frame per `GLSurfaceView.onDrawFrame` callback and returns; the
presentation swap is performed by the Java `GLSurfaceView`, and therefore the frame
cadence is whatever the display/Choreographer cadence is.

**Consequence for the owner's goal:** removing the limiter does **not** require an engine
patch, a ZettaBridge patch, or a Dynarmic patch. It requires the display to run at a
refresh rate above 60 Hz. The panel supports 120 Hz at the same resolution; the device is
currently in a 60 Hz mode and the app never asks for more.

Marked clearly: the *absence* of an engine limiter is evidential (exhaustive
enumeration, §3). The *identity* of the pacer as Android's 60 Hz vsync is a strong
inference from that absence plus device state plus the shape of the FPS distribution
(§2, §6); it has not been proven by a live A/B run, because that needs a device session.

---

## 2. The evidence: 60 Hz display, not an engine constant

Read-only `adb shell dumpsys display` on the connected device, inner panel
(`uniqueId="local:4630946449689556883"`, 1968 x 2184):

```
modeId 3, renderFrameRate 60.000004
supportedRefreshRates [120.00001, 80.0, 60.000004, 48.000004, 30.000002, 24.000002, 10.0]
mActiveModeId=3            (60.000004 Hz)
mActiveSfDisplayMode=DisplayMode{id=2, ..., vsyncRate=60.000004, appVsyncOffsetNanos=16666666}
mActiveRenderFrameRate=60.000004
mDisplayModeSpecs={baseModeId=3 ... primary=physical: (10.0 60.0) render: (0.0 60.0) appRequest=physical: (10.0 60.0) ...}
frameRateOverride            <- EMPTY on the inner display
```

* The panel's 120 Hz mode exists **at the same 1968 x 2184 resolution** (`supportedModes` id 1,
  `fps=120.00001`), so a refresh-rate request costs no resolution.
* Every advertised rate is a divisor of 120 (120 / 80 / 60 / 48 / 30 / 24 / 10) — the panel
  has no "odd" rates, which matters when reading the FPS histogram below.
* The single `frameRateOverride {uid=10150 frameRateHz=60.000004}` in the dump belongs to the
  **outer** (cover) display and to `com.samsung.android.wallpaper.live`, not to
  `local.dh2.fold7` (`uid 10623`). No per-app refresh cap is being applied to the game.
* `appVsyncOffsetNanos=16666666` — the app-facing vsync period is exactly 16.666666 ms.

The measured plateau follows that number and nothing else. Re-derived from
`DH2Work\evidence\dh2work-r1\longrun\dh2-events.txt` (`render returned frames=N elapsedMs=M`),
per 120-frame difference over the 388 s run (87 batches):

```
max   dt=2038 ms -> 58.88 FPS     (the cap; nothing faster is ever observed)
mode  dt=2040-2043 ms -> 58.7-58.8 FPS
median 26.77 FPS, min 8.67 FPS
```

`2038 ms / 120 frames = 16.98 ms/frame` against a 16.667 ms vsync period. The distribution
is not a smooth CPU-load distribution: it has a hard ceiling at ~16.98 ms and then a gap to
the next occupied level, exactly the signature of waiting on a fixed presentation clock and
occasionally missing it. It is also inconsistent with an engine constant, because a constant
would sit *on* a round number (16666/16667 us, 60.0) and would not move with device state.

---

## 3. Why there is no engine limiter (exhaustive, not sampled)

### 3.1 The guest never touches EGL

`libDungeonHunter2.so` has 351 `.rel.plt` imports. 91 of them are GL (`glDrawArrays`,
`glClear`, ...). **Zero are EGL** — no `eglSwapBuffers`, no `eglSwapInterval`, no
`eglMakeCurrent`, no `eglGetDisplay`, no `glFinish`. Enumerated directly from
`.rel.plt` / `.dynsym`. The engine therefore *cannot* be calling `eglSwapInterval`, and it
is not doing the swap.

This is what the runtime report's two lines mean together:

* `DH2Work\evidence\dh2work-r1\longrun\zb-runtime-report.txt:121` `egl-swaps: 0`
* `...:122` `egl-observation: current context created outside guest EGL bridge; Java swaps are not counted`
* `...:114` `gl-egl-context-current: yes`, `...:112` `gl-calls: 6571031`, `...:113`
  `gl-first-call: glGetIntegerv tid=1641`, `...:79` `GameRenderer.nativeRender=10603`

`egl_swap_total_` is incremented only inside
`core\src\gl\egl_manual.cpp:442` (`runtime_report().note_egl_swap()`), i.e. when a **guest**
`eglSwapBuffers` is marshalled by the bridge. Zero means the guest performed zero EGL
swaps. The observation line is emitted at `core\src\runtime_report.cpp:588-589` precisely
when `egl_current_tids_` is empty while a GL context is current — i.e. the current context
was made current by the Java `GLSurfaceView` (which for this app runs its renderer on
`tid=1641`, the same thread that issues all 6.57 M GL calls). The guest renders into the
context Java established; **Java presents**.

### 3.2 Nothing on the frame path sleeps or yields

Every direct call from the engine into a libc pacing/blocking import was enumerated by
decoding all `BL` instructions in `.text` against the resolved PLT veneer map:

| import | call sites | any inside the frame chain `0x52f000`-`0x535000`? |
|---|---|---|
| `usleep` | 5 (`0x840704 0x88f2d4 0x88f4ac 0x8934e4 0x893588`) | none |
| `nanosleep` | 6 (`0x533c14 0x533c3c 0x56ee74 0x6a03ec 0x6a03fc 0x6a0424`) | only inside `__sleep` itself |
| `select` | 7 (`0x827ca0 0x827d64 0x8402ec 0x841288 0x8412ac 0x89efd8 0x89effc`) | none |
| `sched_yield` | 1 (`0x56ee54`) | none |
| `clock` | 9 | none |
| `time` | 11 | none |
| `gettimeofday` | 20 | `0x533478` only (see below) |

The two in-range `nanosleep` sites are the body of the engine's own `__sleep`:

```
; _ZN...  0x533bec  __sleep(unsigned int ms)      <- loops on nanosleep until it completes
  00533c08  str  r2, [sp, #8]        ; tv_nsec
  00533c0c  str  r3, [sp, #0xc]
  00533c10  stm  sp, {r2, r3}
  00533c14  bl   #0x30e748           ; -> IMPORT:nanosleep
  00533c18  cmp  r0, #0
  00533c1c  mov  r5, sp
  00533c20  beq  #0x533c40
  00533c24  ldr  r3, [sp]
  ...
  00533c3c  bl   #0x30e748           ; -> IMPORT:nanosleep  (EINTR retry)
```

and its wrapper `NVThreadSleep` at `0x533c60` (converts ms ->
`sec = ms/1000`, `nsec = (ms%1000)*1000000`, calls `__sleep`). This is dead pacing code as far
as the game loop is concerned:

* `NVThreadSleep` (`0x533c60`) has **zero callers** in the whole engine.
* `__sleep` (`0x533bec`) has exactly **one** caller, `0x533cac`, which is inside
  `NVThreadSleep`.

It is a libc-shim/utility pair (`NVThread*`, matching the engine's `glitch::os` layer),
not the frame pacer.

The `gettimeofday` at `0x533478` is not pacing either — it is the touch-event timestamp in
`Java_com_gameloft_android_GAND_GloftD2SS_GameGLSurfaceView_nativeOnTouch` (`0x533440`):
it builds a `timeval`, divides `tv_usec` by 1000 with the magic constant `0x10624DD3`
(`movw r3,#0x4dd3` / `movt r3,#0x1062` / `smull` / `asr #6`), forms
`sec*1000 + usec/1000`, and passes that as the 4th argument to `appOnTouch` (`0x52f11c`).

### 3.3 The frame path itself, with symbols

The prior call-graph note in the task is partly wrong and needs correcting:

| address | actual symbol (from `.symtab`) |
|---|---|
| `0x5311a0` | `Java_com_gameloft_android_GAND_GloftD2SS_GameRenderer_nativeRender` (40 bytes) |
| `0x530fc8` | `appUpdate` (392 bytes) |
| `0x533500` | `Java_..._DungeonHunter2_nativeSetOrientation` — **not** a game renderer |
| `0x32ccc4` | `_ZN11Application6UpdateEv` |

`nativeRender` is a four-instruction pause gate plus a tail call:

```
005311a0  Java_..._GameRenderer_nativeRender:
005311a0  ldr  r3, [pc, #0x18]
005311a4  ldr  r2, [pc, #0x18]
005311a8  add  r3, pc, r3
005311ac  ldr  r2, [r3, r2]
005311b0  ldr  r3, [r2]
005311b4  cmp  r3, #0
005311b8  bxne lr            ; paused -> return immediately, draw nothing
005311bc  b    #0x530fc8     ; else tail-call appUpdate
```

`appUpdate` (`0x530fc8`) then runs, per frame: `time()` (`bl 0x30e580`) for a wall-clock
stamp, an `Application::Update` virtual call on `[r4+0x10]` (`bl 0x32ccc4`), then
`snprintf` (`bl 0x30e868`) into a 16-byte stack buffer and
`__aeabi_ui2f`/`__aeabi_fdiv` for a counter. No wait, no yield, no frame-budget compare.

`Application::Update` (`0x32ccc4`) is where a limiter would live, and it does not have one:

```
0032cd30  ldr  r3, [r4, #0x10]
0032cd3c  ldr  r3, [r3]
0032cd40  mov  lr, pc
0032cd44  ldr  pc, [r3, #0xc]      ; virtual: now_ms = timer->getTime()
0032cd48  ldr  r3, [r4, #0x70]     ; previous frame stamp
0032cd50  rsb  r3, r3, r0          ; delta = now - previous
0032cd54  cmp  r3, #0x7d0          ; 2000 ms
0032cd58  bhi  #0x32cf00           ; stale/restart path: just re-stamp, return
...
0032cd94  ldr  r0, [r4, #0x20]
0032cd98  bl   #0x33c568           ; TouchScreenBase::ProcessEvents()
0032cd9c  mov  r0, r4
0032cda0  bl   #0x320da4           ; Application::ComputeDt()
0032cddc  ldr  r1, [r4, #0x8c]
0032cde0  mov  r0, r4
0032cde4  bl   #0x32c438           ; Application::_Update(int dt)
0032cde8  mov  r0, r4
0032cdec  bl   #0x32ade8           ; Application::_Draw()
```

The only comparisons are **robustness** guards (a 2000 ms "clock jumped / was suspended" guard,
and a 4000 ms `DataReloaderManager::checkFiles()` period), never a "have I run too soon"
gate. Everything else in the function is the FPS/perf-counter reporting block — the engine's
own `avgFrameRate` static, which the symbol table names explicitly:

* symbols.csv: `_ZZN11Application6UpdateEvE12avgFrameRate` = `Application::Update()::avgFrameRate`, `.bss` @ `0x99fa08`
* symbols.csv: `lastFPS` (`STT_OBJECT`, `.bss` @ `0x9f63f4`), `glitch::video::CFPSCounter::registerFrame`, `getFPS`
* symbols.csv: `_ZN6glitch5video12IVideoDriver6getFPSEv`

i.e. the engine *measures* frame rate and never *clamps* it.

Finally, the delta-time source itself:

```
0060b0cc  glitch::os::Timer::getRealTime:
0060b0cc  str  lr, [sp, #-4]!
0060b0d0  sub  sp, sp, #0xc
0060b0d4  mov  r1, #0
0060b0d8  mov  r0, sp
0060b0dc  bl   #0x30e724        ; -> IMPORT:gettimeofday
0060b0e0  ldr  r2, [sp, #4]     ; tv_usec
0060b0e4  movw r3, #0x4dd3
0060b0e8  movt r3, #0x1062      ; /1000 magic
0060b0ec  smull r1, r3, r3, r2
0060b0f4  rsb  r3, r2, r3, asr #6
0060b0f8  ldr  r2, [sp]         ; tv_sec
0060b0fc  mov  r0, #0x3e8
0060b100  mla  r0, r0, r2, r3   ; r0 = sec*1000 + usec/1000
```

`Application::ComputeDt` (`0x320da4`) calls that, and stores the result in an integer:
`rsb r0, r3, r0` (delta) then `__aeabi_fmul` by a scale and `__aeabi_f2iz`. So the engine's
notion of a frame time is a **whole number of milliseconds**, which is why the observed
frame times quantise onto 16/17 ms and never land mid-way. This is the engine's *input*
granularity, not a cap.

### 3.4 A note on the constants that look suspicious

Searching the whole image for `16666`, `16667`, `33333`, `33334`, `0x10624DD3`,
`1/60.0f`, `1/30.0f`, `16384`, and `movw #16666`/`#33333` yields **nothing loadable from
`.text`** at all:

* `0x86f4` (16666) and `0x2868c` (16667) are inside the `.hash` section, not code or data.
* The `0x9ed6xx` / `0xbf92xx` / `0xbfd2xx` / `0xc7cfxx` hits are all **past the end of every
  allocated section** — they are in the DWARF debug blob (`0x9a23f0`-`0xd16b94`), so they are
  debug-info byte noise, not program constants.
* No ARM `movw` anywhere in `.text` encodes 16666, 16667 or 33333.

So there is no target-frame-duration constant anywhere in the engine's code.

---

## 4. Is it 60, or 30, and what switches between them?

There are **not** two regimes with different caps. There is one clock at ~59 FPS, and a
content/CPU-limited continuum below it. Per-batch FPS over the run is a smooth descent
(58.9 -> 50 -> 39 -> 30 -> 26 -> 20 -> 8.7) with single-batch spikes back up to 58.8; the
earlier "48-59 FPS in light scenes" and "26.77 median" are the two ends of that same
distribution, not two limits.

The apparent "~26.8 median" is the *load* on a heavy scene, not a 30 FPS cap. Two
independent checks:

* the maximum is never 30 — it is 58.88, and the top of the histogram is a tight cluster at
  2038-2048 ms (16.98-17.07 ms/frame);
* nothing ever blocks a frame to 33.3 ms. If an engine gate were rounding frames up to 33 ms
  there would be a spike at exactly dt≈4000 ms per 120 frames (30.0 FPS) with a hard edge;
  the observed data has single counts spread smoothly across 4019-6012 ms instead.

What *does* switch between regimes is whether the renderer finishes inside one vsync:
one vsync (16.7 ms, ~60 FPS) vs two (33.3 ms, ~30 FPS) vs three (50 ms, ~24 FPS), then
unbounded when simulation work dominates. `dt=3075 ms` (39.0 FPS) and `dt=3581 ms`
(33.5 FPS) are the missed-vsync transition, and 4019-4043 ms (29.7-29.9 FPS) is the first
full two-vsync shelf. This is textbook vsync quantisation.

---

## 5. Blast radius of removing the cap

**Good news:** the cap is not a *pace* control, because the engine does not implement one.
The engine already handles variable frame time by design:

* `Application::ComputeDt` (`0x320da4`) computes a per-frame delta and keeps a **fractional
  remainder** in a separate field (`0x94`), i.e. a fixed-point accumulator: it converts the
  integer millisecond delta to float, multiplies by a scale (`+0x9c`), truncates to int,
  adds a carried fraction and stores the new remainder. This is an explicit
  frame-rate-independence mechanism, not a fixed-step assumption.
* `glitch::os::Timer` exposes `setSpeed`, `setTime`, `startTimer`/`stopTimer`, `tick`, and a
  virtual timer — again a time-based, not frame-count-based, model.
* The guest's simulation is fed `dt` in milliseconds (`_Update(int dt)` at `0x32c438`), so
  raising the frame rate raises update frequency rather than changing per-frame step size.

**Evidenced risk (low):**

* **CPU/JIT bound, not limiter bound.** `GLThread` already uses ~73-77% of one core at ~27 FPS.
  Raising the ceiling to 120 Hz converts the 60 FPS ceiling into "as fast as the JIT can go",
  which for a light scene is the ~37% headroom the task already measured — i.e. ~37 FPS in the
  same scene, not 120. The 4/8-core idle figures say nothing about single-thread JIT throughput.
* **Input timestamp resolution.** `nativeOnTouch` sends milliseconds
  (`0x533440`, `gettimeofday` -> `sec*1000 + usec/1000`); `DungeonHunter2.nativeAccelerometer`
  is called 18230 times in the run. Fine-grained touch/accelerometer pacing that assumed
  ~16 ms sampling should still be correct, but is untested above 60 Hz.

**Inference (not evidenced), listed honestly:**

* A 2011 Gameloft action engine may clamp `dt` internally somewhere in `_Update`; no clamp was
  found in `Application::Update`/`ComputeDt`, but `_Update` (`0x32c438`, `Application::_Update`)
  was not audited instruction-by-instruction. If a clamp exists there, frame rate will rise
  while simulation speed stays put — visible as smoothness improvement without faster movement.
  That is the safest possible failure mode.
* Anything frame-count-driven (an animation counter, a flash-blend counter, a fixed number of
  update ticks per scripted event) would run faster. No such counter was identified in the
  frame path.
* Audio sync: `GLMediaPlayer`/vox run on their own threads and their sleep helpers
  (`vox::VoxThread::Sleep` `0x893604`, `CAndroidOSManager`-adjacent `usleep` sites) are
  independent of the render cadence, so audio was never paced by the render loop. Inferred.
* Because the engine derives animation from wall-clock `dt` rather than frame counts, the
  expected visible outcome of a higher refresh rate is *smoother* motion at the same speed.

**Bottom line:** this looks like a *presentation* limit. The safest engineering posture is to
treat the engine as frame-rate independent (it is built that way) while accepting that
"engine runs faster" is the intended outcome of the owner's port.

---

## 6. How to remove it — no engine patch, no ZettaBridge/Dynarmic patch

### 6.1 The mechanism

Java's `GLSurfaceView` drives `onDrawFrame` from a `Choreographer` callback, which fires on
display vsync. The device's inner panel is in its 60 Hz mode and **nothing in our app asks for
a higher mode**. Requesting 120 Hz is therefore the entire fix. Since our launcher is
project-controlled, this is allowed and available.

Relevant, project-controlled surfaces:

* `research\ZettaBridge\android\launcher\app\src\main\AndroidManifest.xml:92-100` — the
  manifest stub `Stubs$SingleInstance` (the actual runtime class, per the device:
  `SurfaceView[local.dh2.fold7/com.zettabridge.launcher.Stubs$SingleInstance]@0`). This is
  where an `android:preferredDisplayModeId` can be declared, on the stub, without touching the
  guest APK.
* `research\ZettaBridge\android\launcher\app\src\main\java\com\zettabridge\launcher\GuestWindowStyle.java:42-53`
  — `afterCreate(Activity)` runs after the guest's own `onCreate`, i.e. **after** the guest has
  called `setContentView` and its `GLSurfaceView` exists. This is the natural place for a
  `Surface.setFrameRate(...)` / `Window.setFrameRate(...)` call, or for walking the view
  hierarchy for the `SurfaceView` and setting `holder.setFixedSize`-independent frame rate.
* `research\ZettaBridge\android\launcher\app\build.gradle.kts:23,28` — `compileSdk = 35`,
  `targetSdk = 35`, so `Surface.setFrameRate` (API 30) and
  `WindowManager.LayoutParams.preferredDisplayModeId` (API 23) are both available without
  compat shims.
* `GuestInstrumentation.java` already intercepts `callActivityOnCreate`, so any launcher-side
  window/surface work can be hooked deterministically.

Concretely, the minimum change is one of:

1. `android:preferredDisplayModeId="1"` on the `Stubs$SingleInstance` (and `Stubs$Landscape`)
   `<activity>` entries in our launcher manifest — mode id 1 is the 1968x2184@120.00001 mode,
   same resolution as the 60 Hz mode 3, so no scaling. This asks the system to switch the panel
   to 120 Hz for the window's lifetime.
2. In `GuestWindowStyle.afterCreate`, request `120f` on the guest activity's window/surface.

Either way the guest engine, ZettaBridge and Dynarmic are untouched. This is the "no engine
patch" route, and it is the correct route here.

### 6.2 Why the guest-byte-patch idea does not apply

Changing the guest's `eglSwapInterval` argument is not available as a fix: **the guest never
calls `eglSwapInterval`** (zero EGL imports, §3.1), so there is no argument to change. Likewise
`Surface.setFrameRate` is never called by the guest Java — the decompiled
`GameGLSurfaceView.java:21-29` sets only `setEGLContextFactory`, `setEGLConfigChooser`,
`setRenderer`; there is no `setRenderMode`/`setFrameRate` anywhere in
`work\decompiled-java` (verified by grep). `GLSurfaceView`'s default
`RENDERMODE_CONTINUOUSLY` puts the cadence entirely in the framework's hands.

### 6.3 What an engine patch would look like if there were a limiter

For completeness, the project's established method does apply cleanly to this binary if a
limiter is ever found: `DH2Work\patches\engine\patch_engine.py` pins the input SHA-256
(`:18`), asserts the exact original bytes at the site (`:21-22`), assembles
`engine_path_fix.S` with the NDK clang, links it at an absolute VA with
`engine_path_fix.ld` (`:4-6`: `SECTIONS { . = 0x56dd9c; ... }`, plus
`/DISCARD/ : { *(.ARM.exidx*) ... }`), `llvm-objcopy -O binary` to raw bytes, asserts the
length matches, splices, and writes a JSON report. It needs no relocation beyond
`ld`-defined absolute symbols (`engine_path_fix.ld:2-3`), so a single-instruction
patch would be trivially expressible. **No such site exists in this binary**, so no patch
was authored — inventing one would be the wrong answer.

Related project patches for the same style of absolute-VA fix:
`DH2Work\patches\storm\patch_storm.py`, `DH2Work\patches\storm\storm_bias_fix.ld`,
`DH2Work\patches\game\patch_game.py`.

---

## 7. Residual uncertainty and the one experiment worth running

* The vsync attribution is inferred from (a) the proven absence of any engine limiter,
  (b) `mActiveModeId=3` = 60.000004 Hz with an app vsync period of 16.666666 ms,
  (c) the histogram's hard edge at 16.98 ms, and (d) the fact that the plateau (~58.8 FPS) sits
  1.9% below nominal 60 Hz in every batch. Point (d) is not fully explained: 120 frames at a
  perfect 60 Hz would be 2000 ms, and the best batches are 2038-2048 ms. Sub-2%
  measurement overhead in the wrapper's 120-frame probe, or a slightly loose vsync period, is
  the likely cause; a live A/B would settle it.
* Decisive experiment (needs the device and the Lead's go-ahead, since a manual tester may be
  using it): launch once with the panel forced to 120 Hz (Developer options / Samsung refresh
  settings, or `Stubs$SingleInstance` + `preferredDisplayModeId=1`) and re-run the same
  120-frame batch measurement. If the plateau moves from ~58.8 to a higher quantum
  (~100-113 FPS on this JIT, or 120 in an empty scene), the vsync attribution is proven and the
  fix is validated. If the plateau stays at ~59, the cap is elsewhere and the host-side swap
  path should be re-examined.
* `Application::_Update` (`0x32c438`) was not audited instruction-by-instruction; a `dt` clamp
  there would not change the frame-rate conclusion, only the simulation-speed expectation.
* The engine's GL calls are 6.57 M in 388 s (~620/frame) and the renderer already rejects 86
  `glDrawElements` calls ("client vertex attribute range is unreadable"); neither is a limiter,
  but both are independent things worth watching when the ceiling is raised.

---

## Appendix: addresses cited

| address | symbol / role |
|---|---|
| `0x5311a0` | `Java_..._GameRenderer_nativeRender`; pause gate + tail call to `appUpdate` |
| `0x530fc8` | `appUpdate`; per-frame JNI body; `time()`, `Application::Update`, `snprintf` |
| `0x32ccc4` | `_ZN11Application6UpdateEv`; timestamp guard + `ComputeDt` + `_Update` + `_Draw` |
| `0x320da4` | `_ZN11Application9ComputeDtEv`; `getRealTime` delta, ms-quantised |
| `0x60b0cc` | `_ZN6glitch2os5Timer11getRealTimeEv`; `gettimeofday` -> `sec*1000 + usec/1000` |
| `0x32c438` | `Application::_Update(int dt)` (not audited in full) |
| `0x32ade8` | `Application::_Draw()` |
| `0x533440` | `Java_..._GameGLSurfaceView_nativeOnTouch`; `gettimeofday` -> ms timestamp |
| `0x52f11c` | `appOnTouch` |
| `0x533bec` | `__sleep`; nanosleep loop; 1 caller |
| `0x533c60` | `NVThreadSleep`; ms -> timespec; **0 callers** (dead) |
| `0x30e724` | PLT veneer -> `gettimeofday` (GOT `0x994dd8`) |
| `0x30e748` | PLT veneer -> `nanosleep` (GOT `0x994de4`) |
| `0x30e580` | PLT veneer -> `time` (GOT `0x994d4c`) |
| `0x30e880` | PLT veneer -> `usleep` (GOT `0x994e4c`) |
| `0x99fa08` | `Application::Update()::avgFrameRate` (`.bss`) |
| `0x9f63f4` | `lastFPS` (`.bss`) |

PLT decoding note for future work: the veneers are 12 bytes each starting at
`0x30dd88` (`add ip, pc, #0x600000` / `add ip, ip, #0x86000` /
`ldr pc, [ip, #imm]!`), and the displacement is a plain **byte** offset (no scaling), with
`base = veneer_va + 8 + 0x686000`. The first stub at `0x30dd84` is the resolver header.
