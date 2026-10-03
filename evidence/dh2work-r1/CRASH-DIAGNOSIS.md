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
render returned frames=7200 elapsedMs=256328   ← last sample while still alive
```

The run passed **7200 frames / 256.3 s ≈ 4.3 minutes** with the guest alive at ~711 MB RSS
and no crash — i.e. it went past the point where the four earlier launches died, and past
the interval in which the documented Test 6 fault would be expected if this were that bug.
Overall average 7200 / 256.3 ≈ **28.1 FPS**; the most recent 240 frames took 9.78 s, i.e.
**≈ 24.5 FPS**. `DH2FileGuard` recovered repeated cache roots 178 times with no open
failures, and `prince_modular.bdae` opened correctly (3,207,072 bytes, first 8 bytes
`42524553 feff0000` = `BRES`), so the Test 5 absolute-path fix and the Test 6
duplicated-root collapse both work on device.

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

## 3. Root cause: an unguarded null global in `SetFinalOrientation`

A first pass suggested the crash was tied to the cover display. **That hypothesis was
refuted** by the later `activity exit-info` history: there were four crashes, not two, and
the two at 16:34:06 / 16:34:09 happened on the **inner** display (`2184x1968`), the same
display the successful run used.

| # | Time | Display | Outcome |
| --- | --- | --- | --- |
| 1 | 16:30:45.835 | cover `2520x1080` | SIGSEGV |
| 2 | 16:30:47.502 | cover `2520x1080` | SIGSEGV |
| 3 | 16:34:06.820 | inner `2184x1968` | SIGSEGV |
| 4 | 16:34:09.183 | inner `2184x1968` | SIGSEGV |
| 5 | 16:34:19 → | inner `2184x1968` | **ran, 7200 frames / 256 s, no crash** |

Display is therefore **not** the trigger. The crash is **intermittent**: four consecutive
failures followed by a success with no configuration change.

Disassembling the pristine engine pins the faulting instruction exactly. In
`SetFinalOrientation` (`0x530a6c`):

```text
0x530a70  ldr  r4, [pc, #0xd0]   ; 0x00464014 -> global base
0x530a7c  add  r4, pc, r4        ; r4 = 0x996a90
0x530a80  ldr  r6, [r4, r3]      ; r6 = *(base + 0x46a4)     <- pointer slot, non-null
0x530a84  ldr  r3, [r6]          ; r3 = *r6                  <- TARGET IS NULL
0x530a88  ldr  r0, [r3, #0x4c]   ; *** FAULT: read of 0x0000004c ***
0x530a8c  bl   #0x46d4d8         ; SavegameManager::getAutoReorientation(r0)
```

`ldr r0, [r3, #0x4c]` with `r3 = 0` produces exactly the reported
`read of 0x0000004c`. The slot itself (`r6`) is valid; the **object it points to is null**.

The caller `nativeSetOrientation` (`0x533500`) *does* carry a null guard — but on a
**different** global (`base + 0x4128`):

```text
0x533514  ldr  r2, [r3, r2]      ; base + 0x4128
0x533518  ldr  r2, [r2]
0x53351c  cmp  r2, #0
0x533520  beq  #0x533544         ; null -> return, no call
...
0x53353c  bl   #0x530a6c         ; SetFinalOrientation  <- which needs base + 0x46a4
```

So `nativeSetOrientation` guards one global and then calls code that dereferences a
different one. That is a genuine latent null dereference in the original engine, normally
masked because both globals are populated before any orientation request. Inside the
wrapper, `nativeSetOrientation` can be driven during early EGL surface setup, when only the
guarded global is set — which is why the guest dies with `gl-calls: 0` and no
`surface initialization returned`.

### 3b. Why it is intermittent: the accelerometer listener

`nativeSetOrientation` is called from the guest's **sensor listener**, not from an
orientation-change callback. In `DungeonHunter2.smali`:

```text
L2108  invoke-static {v0}, L…/DungeonHunter2;->nativeSetOrientation(I)V
L2135  invoke-static {v0}, L…/DungeonHunter2;->nativeSetOrientation(I)V
       both inside: .method public onSensorChanged(Landroid/hardware/SensorEvent;)V
```

That closes the loop on everything observed:

* The trigger is a **hardware sensor callback**, so it races the engine's own
  initialisation instead of following a fixed UI sequence — hence four failures and then a
  success with nothing changed.
* It fires regardless of which panel the activity is on, which is why the display
  hypothesis in the first draft of this document was wrong.
* The runtime report already showed `nativeAccelerometer=1`, i.e. the listener was live and
  had been delivered exactly one event — enough to reach the unguarded path.
* It is consistent with `gl-calls: 0`: an accelerometer event can arrive before the first
  frame is drawn.

The natural fix is therefore in the app/guest layer: **gate the sensor-driven
`nativeSetOrientation` call until the engine is known to be initialised** (for example, a
`GameTrace`-style readiness flag set once the renderer reports its first frame, checked
before the `invoke-static` at L2108/L2135). That needs no engine bytes and leaves the
pinned engine hash `45891aad…` untouched.

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

## 5. Candidate fixes and how to confirm

The fault is a **null read in engine globals**, reached because `nativeSetOrientation` is
called before the object at `base + 0x46a4` exists. Three fix sites, cheapest first:

1. **App/guest layer — defer the early call.** `nativeSetOrientation` must not run until the
   engine has finished initialising. This is the only site DH2Work already owns, needs no
   engine bytes to change, and does not invalidate the pinned engine hash `45891aad…`.
   The Test 10 delta (`GameTrace.initialPhoneWidth/Height` before `nativeSetPhone(II)V`)
   already perturbs exactly this startup ordering, which is why reproducing the pinned
   Test 10 guest is the cleanest next experiment.
2. **Engine guard, local.** At `0x530a84`, bail out when `r3 == 0` before the `+0x4c` load.
   Behaviourally safe — `nativeSetOrientation` still stores the requested orientation
   afterwards and a later call finalises it — but it changes the engine hash and needs an
   8-byte redirection, like the existing Test 5 `engine_path_fix.S` did.
3. **Engine guard, at the caller.** Extend `nativeSetOrientation`'s existing null check to
   cover `base + 0x46a4` as well as `base + 0x4128`. Conceptually the correct fix (the guard
   that is there is simply checking the wrong global), but again needs engine bytes.

Confirm the race before fixing: launch the game repeatedly, unchanged, and expect a mix of
the `0x530a88` fault and successful runs. If every launch fails, this is deterministic and
the init-order reading is wrong.

## 5b. Threat to validity

Steps 1–4 of the guest chain in DH2Work's `build/` (`package_emulator_guest.py` etc.) pin
**intermediate** Storm revisions — `Test 7`/`Test 8` expect Storm `334c23b8…`, which the
current `storm_import_fix.c` no longer produces (it yields `2489c037…`). Reproducing the
pinned Test 10 guest therefore needs the Test 5-era Storm sources from
`compatibility-work-test5.zip`, not just the current tree.

## 6. Artifacts in this directory

| File | Contents |
| --- | --- |
| `DH2-diagnostics.zip` | the app's own crash export, pulled from `/sdcard/Dh2_crashes/` |
| `diag/zb-runtime-report.txt` | guest-exit line, registers, stack candidates, native/GL call counts |
| `diag/dh2-events.txt` | app milestone trace up to the crash |
| `diag/dh2-logcat.txt` | guest log tail, ending at the SIGSEGV dump |
| `live-dh2-events.txt` | the successful inner-display run, 1 → 1200 frames |
| `live4-events.txt` | the same run later, up to 7200 frames / 256 s |
| `app-live-logcat.txt` | 356 KB of the working run; inner display only, no SIGSEGV |
| `crash-window.txt` | compacted host `adb logcat` window covering both 16:30 crashes |
| `symbolicate.py` | symbolises a guest PC/LR against the pristine engine |
| `disas_chain.py` | disassembles `nativeSetOrientation` → `SetFinalOrientation` and resolves PC-relative literals |
| `symbol_at.py` | symbolises any (library, offset) pair, honouring `$a`/`$t` mapping symbols |
| `resolve_veneer.py` | resolves an engine PLT veneer to its imported symbol through `.rel.plt` |
| `longrun/` | runtime report, event and exit traces for the 388 s run and the `IBuffer::copy` crash |

## 7. The second crash: the long run, and it looks like the documented one

The successful launch was left running. It died at **16:40:53** after **388 s (~6.5 min)**
and **10,560 frames**, with a completely different fault:

```text
guest-exit: guest SIGSEGV: read of 0x0001c9c8, pc <libc> +0x67304
crash-lr : libDungeonHunter2.so offset 0x5a1c6c
crash-registers: r0=59e01af8 r1=0001c9c8 r2=00000150 r3=0000003c
                 r14=fcc24c6c r15=fdad3304   crash-precise: no
```

`gl-calls: 6571031`, `nativeAccelerometer=18230`, `nativeRender=10603`,
`nativeOnTouch=9379`, `native-calls total=38252`.

Symbolising the two addresses:

* **LR `+0x5a1c6c`** is inside `glitch::video::IBuffer::copy()` (`0x5a1c1c`), on the
  instruction immediately after `bl 0x30e868`.
* **`0x30e868`** is a **PLT veneer** (`add ip, pc, #…; ldr pc, [ip, #…]`) that resolves —
  via the engine's `.rel.plt` (GOT slot `0x994e44`) — to the imported symbol **`memcpy`**.
  That is why the return address is in the engine while the fault sits in guest `libc`.
* The instruction stream is self-consistent with `IBuffer::copy()` doing
  `memcpy(dst, src, size)`: it checks `[this+0xc]` (size), saves `[this+8]` (old data),
  calls `operator new[]` (`0x5341a8`) with that size, stores the new pointer back to
  `[this+8]`, reloads `[this+0xc]` into `r2`, then calls the veneer. The registers are
  `dst=0x59e01af8`, `src=0x0001c9c8`, `n=0x150` (336) — consistent with a 336-byte copy
  from an invalid source pointer.
* **`crash-stack-candidate-3: +0x59b424`** lands in
  `glitch::scene::createMeshCopy(…)` (`0x59b2a4`) at the instruction right after its
  `bl 0x5a1c1c` — so the call chain is
  `createMeshCopy()` → `IBuffer::copy()` → the `memcpy` import.

### 7b. Correction: the faulting libc frame is NOT identified

An earlier revision of this document stated that the fault was *inside* `memcpy`. **That is
wrong and is retracted.** `memcpy` and `memmove` are both exported at guest
`libc.so +0x68280` (size `0x28c`), and the reported PC was `+0x67304` — outside them. No
exported symbol brackets `0x67304` either, so it names an internal function at best.

The wrapper's own library attribution is also **internally inconsistent for this crash**:

| Derived from | Address | Reported | Implied libc base |
| --- | --- | --- | --- |
| `crash-pc` | `0xfdad3304` | libc `+0x67304` | `0xfda6c000` |
| stack candidate | `0xfdb07f93` (Thumb, bit 0 set) | libc `+0x56f92` | `0xfdab1000` |

Those bases differ by `0x45000`. `/proc/<pid>/maps` is denied to `adb shell`, so it cannot be
resolved from the host. `crash-precise: no` means `crash-pc` is a JIT block start anyway.

**What survives this correction**, because it is verified independently of the libc
attribution:

1. The veneer at `0x30e868` is `memcpy` — established from the engine's own `.rel.plt`
   relocation, not from the guest crash report.
2. LR `+0x5a1c6c` is exactly the instruction after that `bl`, and the stack candidate
   `+0x59b424` is exactly the instruction after `createMeshCopy`'s `bl` into
   `IBuffer::copy()`. Landing on post-call instructions in both frames is a strong
   signature of a genuine call chain.
3. The fault address `0x0001c9c8` is precise (it is `si_addr`), and 336 is a plausible
   length for a mesh sub-buffer.

So the *chain* and the *bad pointer* are established; the identity of the libc frame on top
is not.

`createMeshCopy` is a **mesh-copy during scene setup**, and the documented blocker is
*"a crash after roughly five minutes of play while a new map loads … during script or map
initialisation"*. The placement, the timing and the mesh-copy frame all agree, and the libc
offset (`0x67304`) is close to the documented one (`0x675a0`) — plausibly the same region of
a different guest libc build. The event trace does not itself record map loads (it only
logs surface lifecycle and render milestones), so *"this is the documented map-load crash"*
is a well-supported inference, not a proven identity.

Either way this is now a much tighter target than "a wide stdio print with a null `FILE*`":
a **336-byte copy from source `0x0001c9c8` inside `createMeshCopy`**.

## 8. Still unverified

* Whether the section 3b readiness guard actually removes the startup crash — r2 is built
  and verified but not yet exercised on the device.
* Whether orientation/tilt still works *after* the first frame, i.e. that the guard defers
  the call rather than dropping it.
* The second crash is still unreproduced after the fix and unfixed.
* Audio, saves/reload, quests, all levels, 16 KB pages, ZettaBridge performance.
