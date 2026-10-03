# DH2Work r1 — Fold7 crash diagnosis (2026-10-03)

Physical device: Galaxy Z Fold7 **SM-F966B**, Android 16, `arm64-v8a`, 4096-byte pages.
Build under test: `local.dh2.fold7` versionCode **15**, versionName
`1.0-work1-standalone`, APK SHA-256
`dfc6d3cd…` (13.7 MB wrapper) / `…-standalone` 446,954,145 B with the owner cache bundled.

Guest libraries inside the shipped APK (read back out of the built package):

| Entry | SHA-256 | Matches |
| --- | --- | --- |
| `lib/armeabi-v7a/libDungeonHunter2.so` | `45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4` | pinned engine fix |
| `lib/armeabi-v7a/libStormGLOFT.so` | `2489c037d75cd2a3c994b7bc349aac767c34f96acf25a79188a72c77dc7502de` | current Storm revision |

## 1. Result: the app runs the game on real hardware

On the **inner display** the build reaches gameplay and renders continuously. From the
app's own event trace (`dh2-events.txt`):

```text
surface created #1 EGL=… GL=OpenGL ES 3.2 renderer=Adreno (TM) 830
render returned frames=1    elapsedMs=482
surface created #2 …           (context recreated after the intro video)
render returned frames=240  elapsedMs=37145
render returned frames=1200 elapsedMs=63371
```

8,640 frames were not reached here, but the measured window
(240 → 1200 frames over 37.145 s → 63.371 s) is **960 frames / 26.226 s ≈ 36.6 FPS**.
The guest process stayed alive at ~517 MB RSS. `DH2FileGuard` recovered repeated cache
roots 178 times with no open failures, and `prince_modular.bdae` opened correctly
(3,207,072 bytes, first 8 bytes `42524553 feff0000` = `BRES`), so the Test 5 absolute-path
fix and the Test 6 duplicated-root collapse both work on device.

## 2. The crash: startup, not the documented 5-minute map-load fault

Two consecutive launches produced an identical fault:

```text
16:30:45.835  guest SIGSEGV: read of 0x0000004c, pc 0xfccf74d8
16:30:47.502  guest SIGSEGV: read of 0x0000004c, pc 0xfcbef4d8
  r0 00000000  r1 00000015  r2 000032f0  r3 00000000
  r14 fccb2a90  r15 fcbef4d8   cpsr a0000010 (ARM)   crash-precise: no
  pc in libDungeonHunter2.so offset 0x46d4d8
  lr in libDungeonHunter2.so offset 0x530a90
```

Symbolised against the pristine engine (`_ZNK15SavegameManager20getAutoReorientationEv`
at `0x46d4d8`, `SetFinalOrientation` at `0x530a6c`, `nativeSetOrientation` at `0x533500`):

* Fault is a **read of address `0x4c`** — a null base plus a `0x4c` field offset.
* `crash-registers` `r0 = 0`, so the object pointer is null.
* Call chain: Java `nativeSetOrientation` → `SetFinalOrientation` → `SavegameManager::getAutoReorientation`.
* `crash-recent-jni-calls` shows a burst of `GetMethodID` / `GetStringUTFRegion` /
  `NewGlobalRef` immediately before the fault.
* `gl-calls: 0`, `egl-swaps: 0` — **the guest had made no GL call yet**, and the guest's
  own event trace has no `surface initialization returned` entry, so death occurred
  during EGL surface initialisation.

**This is a different fault from the one the project documents.** The recorded Test 6
blocker is a null `FILE*` into the `vfwprintf` wrapper (`libc.so +0x675a0`, `lr +0x64c21`)
after roughly five minutes of play while a new map loads. Neither address appears here,
and this fault happens within ~2 s of game start. The 5-minute map-load crash remains
unreproduced and unfixed.

## 3. Leading cause: the display the game starts on

Correlating every `mDisplayFrame=Rect(...)` for `Dh2Activity` in the host capture:

| Time | Activity display frame | Outcome |
| --- | --- | --- |
| 16:30:42 – 16:30:48 | `0,0 - 2520,1080` (cover display, landscape) | **both launches SIGSEGV** |
| 16:32:26 – 16:32:54 | `0,0 - 1080,2520` (cover display, portrait) | launcher only |
| 16:34 onward | `0,0 - 2184,1968` (inner display) | **runs, ~36 FPS, no crash** |

`dumpsys display` at the time of writing confirms the geometry: display 0 (inner) is
`2184x1968` and ON; the cover panel is `1080x2520` and OFF. The app's own log for the
working run contains **14 `mDisplayFrame` samples, all `2184x1968`, and zero SIGSEGV**.

So: **folded (cover display) → crash at `nativeSetOrientation`; unfolded (inner display)
→ runs.** This is the project's open "fold lifecycle" item showing up as a hard crash.

This is a single-axis correlation across two crashes and one success on one device. It is
consistent, not yet proven, and section 5 gives the test that would prove or refute it.

## 4. Honest configuration caveat

This build's guest APK was **rebuilt from the owner's original APK** with
`patch_game.py` + apktool 2.12.1 (guest SHA-256 `5f5db323…`). The verified Test 11
configuration instead reused the **pinned Test 10 guest** (`57cefd15…`), which carries two
deltas this rebuild does not:

* `repack_test9_guest.py` — Storm revision (`2489c037…`, which this rebuild *does* match).
* `repack_test10_guest.py` — an `onCreate` smali patch calling
  `GameTrace.initialPhoneWidth/Height` before `nativeSetPhone(II)V`, plus a Test 10
  `GameTrace.java`, so the DEX is rebuilt.

The missing Test 10 `initialPhoneWidth/initialPhoneHeight` correction applies exactly to the
**initial phone size**, which is adjacent to — and a plausible contributor to — the
`nativeSetOrientation` fault. Reproducing the pinned Test 10 guest (the pinned chain can be
restarted from the surviving Test 5 unsigned guests `6f1c00a2…` / `071f3bd9…`) is the
cleanest way to separate "my rebuild regressed" from "this is a cover-display bug".

## 5. How to confirm

1. Launch folded, on the cover display → expect the `read of 0x0000004c` / `0x46d4d8` fault.
2. Launch unfolded, on the inner display → expect gameplay.

If both hold, the fix belongs in the app layer (do not start the guest until the activity
is on a stable inner display, and re-run the orientation setup on unfold), not in the engine.

## 6. Artifacts in this directory

| File | Contents |
| --- | --- |
| `DH2-diagnostics.zip` | the app's own crash export, pulled from `/sdcard/Dh2_crashes/` |
| `diag/zb-runtime-report.txt` | guest-exit line, registers, stack candidates, native/GL call counts |
| `diag/dh2-events.txt` | app milestone trace up to the crash |
| `diag/dh2-logcat.txt` | guest log tail, ending at the SIGSEGV dump |
| `live-dh2-events.txt` | the successful inner-display run, 1 → 1200 frames |
| `app-live-logcat.txt` | 356 KB of the working run; inner display only, no SIGSEGV |
| `run-logcat.txt` | host `adb logcat` capture spanning both crashes |
| `symbolicate.py` | symbolises a guest PC/LR against the pristine engine |

## 7. Still unverified

* Physical-device gameplay beyond ~64 s and ~1200 frames on this revision.
* The 5-minute map-load `vfwprintf` crash (not reached).
* Audio, saves/reload, quests, all levels, 16 KB pages, ZettaBridge performance.
