# Performance baseline and what a game-specific solution would buy

Device: Galaxy Z Fold7 **SM-F966B** (Snapdragon 8 Elite), Android 16, arm64-v8a,
4096-byte pages. Build: `local.dh2.fold7` versionCode 16, `1.0-work2-orguard`, nested engine
`45891aad…`, Storm `2489c037…`, signed with the project development key. Guest: the owner's
cache, 635 MB, already imported.

## 1. Measured frame cost

Sampled per-thread CPU from `/proc/<pid>/task/*/stat` (root-free) while the game rendered,
and read frame counters from the guest's own event trace across the same window.

| Window | Frames | Wall | FPS | Hot thread | Guest total |
| --- | ---: | ---: | ---: | ---: | ---: |
| 19.2 s | 480 | 19,163 ms | 25.05 | `GLThread` 72.8% of one core | ~77% |
| 12.0 s | 360 | 14,312 ms | 25.15 | `GLThread` 77.1% | ~81% |

**≈ 25.7–30.3 ms of single-threaded CPU per frame.** The frame budget is 16.7 ms at 60 FPS
and 40 ms at 25 FPS, so the hot thread is ~77% busy — the remaining ~23% is stall (GL
driver entry, vsync, blocking).

The process has 37 threads, but only one matters: every other thread is under 3%.

## 2. The GPU is not the bottleneck

`adb shell wm size 1092x984` reduces the render target from 2184x1968 to 1092x984 — **a 4x
reduction in pixels**. Re-measured across the same method:

| Configuration | Pixels | FPS | `GLThread` CPU |
| --- | ---: | ---: | ---: |
| Native 2184x1968 | 4.30 M | **25.15** | 77.1% |
| Quarter 1092x984 | 1.07 M | **24.48** | 76.8% |

Four times fewer pixels changed the frame rate by **nothing** (inside noise) and left the
CPU load unchanged. The cost is therefore **not** GPU fill, shading, bandwidth or driver
overhead. It is the single-threaded execution of the game's translated ARM32 code.

This is the measurement that answers "should we build something game-specific?" — the
target is unambiguous, and it is the CPU translation, not the renderer.

## 3. The engine itself caps at 60 FPS, not thousands

Per-120-frame batch rates over the 388 s run: **min 4.05, median 26.77, max 58.88 FPS.**

Light scenes already reach ~59 FPS, which is a 60 Hz vsync/frame-limiter signature. So:

* The engine will not exceed ~60 FPS without defeating its own limiter, which is an engine
  change, not a translator change.
* At 2184x1968 with ~680 GL calls per frame, three-digit FPS is not physically available
  regardless of CPU speed.
* **The realistic target is a held 60 FPS**, replacing the current 25–27 FPS in heavy
  scenes. From 25.7 ms/frame that needs roughly a **1.6x** cut in translated CPU per frame,
  and ~2x to keep 60 FPS headroom in the heaviest scenes.

## 4. Why a generic translator is the wrong shape here (and what replaces it)

ZettaBridge translates the ARM32 CPU with Dynarmic and passes GLES/EGL through. Its own
published cost model is the problem:

| Workload | Slowdown vs native |
| --- | --- |
| integer mix | ~2x |
| sieve | ~3x |
| `memcpy` | ~3.5x |
| **double-FP loop** | **~22–26x** |

A 2010 3D engine's hot path is float/double transform and matrix math — precisely the
22–26x case — plus a great deal of small integer work at 2–3x. Measured, that is the
25.7 ms/frame above. A hand-written solution can attack both, and the project is unusually
well placed to do it, because:

**The engine is not stripped.** 31,021 named function symbols, 31,018 distinct starts,
1,628 vtables, 54,463 relocations, 867 original build-source filenames, and complete
ARM/Thumb mapping symbols. That is the property that makes mechanical translation tractable
and it is rare.

## 5. Three tiers, cheapest first

### Tier 1 — Tune the translator for this title (days)
Keep ZettaBridge, remove per-call overhead. Measured: **~6.57 M GL calls over 388 s ≈ 17,000
calls/s ≈ 680 per frame**, plus 38,252 native calls. Every crossing costs. Batching GL state
and redundant queries attacks the ~23% stall directly and is low-risk. Ceiling: probably
25% → not enough alone.

### Tier 2 — Native replacements for the hot functions (weeks) ⭐ recommended first real move
Keep the wrapper (so all existing compatibility fixes keep working) and replace the hottest
guest functions with real ARM64 code that the wrapper calls natively instead of JITing.
This is the direct answer to "tailor-made for this game".

The verification machinery already exists and is proven:

| Existing asset | State |
| --- | --- |
| `dh2_oracle.py` — executes the original ARM32 under Unicorn | works, 0 faults on the math module |
| `difftest.py` — original vs compiled, bit-exact | 27,250 comparisons, 0 mismatches |
| `auto_rewrite.py` / `verify_rewrites.py` | **2,989 machine-generated rewrites verified, 0 failures** |
| `port/engine-math` (20 vector/quaternion/matrix bodies) | built, 21,477 comparisons, 0 mismatches |
| cumulative verified | **3,009 functions, 63,118 comparisons, 0 mismatches** |

So a rewrite is not trusted because it compiles — it is trusted because it is executed
against the original code and compared bit-for-bit. Start with the double-FP transform/matrix
paths (the 22–26x case, where the payoff is) and the animation key search.

### Tier 3 — Full static recompilation, ARM32 → ARM64 (months)
Ahead-of-time translate every function once, link it, and drop the JIT entirely: no runtime
translation, no dispatch, no code cache. This is the N64Recomp / XenonRecomp / PS2Recomp
method, and the project's own assessment identified it as the route to a native, bridge-free
build. The unstripped symbol table is exactly what makes it feasible, and `leaf_finder` +
`auto_rewrite` already verified ~3,000 functions mechanically — while the audit also found
~200 virtual-dispatch stubs the leaf classifier wrongly accepts, which is fixable.

Tier 3 is where "hundreds of FPS" would come from if the engine allowed it; it does not, so
Tier 3 is best judged as *"60 FPS with headroom and no JIT"*, not as a frame-rate number.

## 6. What is needed to choose between tiers

We have the *size* of the problem (25.7 ms/frame, 77% of one core) but not yet its
*composition* — how much is JIT'd guest code vs the GL driver vs stall. That needs a
function-level profile, and `simpleperf` is present on the device but refuses to attach
without root:

```text
failed to open perf event file for event_type cpu-cycles:u: Permission denied
```

Two ways forward, both cheap:

1. Build our own APK with `android:debuggable="true"` (or `profileable`) so `simpleperf -p`
   is permitted, then attribute by DSO: `libzbridge.so` (translation + JIT'd code) vs
   `libGLESv2_adreno.so` (driver) vs the guest libraries.
2. A cheap proxy already available: the wrapper counts GL calls and native calls, and the
   stall fraction (~23%) bounds how much is driver-entry rather than CPU.

## 7. Reproducing these numbers

```sh
adb shell pidof local.dh2.fold7:guest
adb shell sh /data/local/tmp/threadcpu.sh 20      # per-thread CPU, root-free
adb shell "tail -3 <cache>/dh2-events.txt"        # frames + elapsedMs
adb shell "wm size 1092x984"                      # GPU A/B; then: wm size reset
```

`evidence/dh2work-r2/launch-test.txt` records eight consecutive launches after the
orientation-guard fix: **8 survived, 0 died, 0 SIGSEGV** (before the fix, four of five
launches died in the accelerometer/`nativeSetOrientation` fault).
