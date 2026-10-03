# The engine's floating point is software float living *inside the guest* — and that is the real bottleneck

This note corrects the performance model in `PERFORMANCE-BASELINE.md` §4 and supersedes the
"start with the double-FP paths because ZettaBridge's model says 22–26×" reasoning. The
conclusion (float/matrix/transform math is the top target) survives, but for a completely
different and much stronger reason.

## 1. The engine contains almost no floating-point instructions

Derived from the binary by a parallel analysis of the pristine
`libDungeonHunter2.so` and its local disassembly (`DH2-recon/asm/engine-assembly.txt`,
1,489,002 instructions):

* Across the **entire** `.text`, the only mnemonics beginning with `v` are
  `vaddl.s8`, `vaddl.u32`, `vhadd.s16`, `vhadd.u32`, `vhadd.u8`, `vld4.32` — six
  instructions, none of them floating point. **Zero** `vmul.f32`, `vadd.f32`, `vldr`,
  `vmov`, `vcvt`, `fmuls`, `fadds`.
* Instead the engine imports the **soft-float helper ABI**: 43 `__aeabi_*` symbols are
  undefined in its `.dynsym`.
* `.ARM.attributes` for the engine is `Tag_ABI_VFP_args = 1` — floating-point *arguments*
  pass in VFP registers, but no VFP arithmetic is emitted.
* Static call-site census over the engine: **24,622 call sites** into software FP
  helpers/libm — `__aeabi_fmul` 8,518; `__aeabi_fadd` 6,165; `__aeabi_fsub` 3,025; float
  comparisons 3,110; `__aeabi_fdiv` 818; `__aeabi_f2iz` 640. All double-precision
  operations combined are only **675 sites (2.7%)**.

So the engine is compiled for a target without hardware FP and does every arithmetic
operation through a helper call. `libStormGLOFT.so` is different — it is built for
cortex-a8 with real VFP code — and `libnativeinterface.so` for 5TE.

## 2. The helpers are guest ARM32 code, so they are translated too

This is the part that matters, and it is decisive. The helpers are **not** host functions.
They are exported by the guest sysroot's own ARM32 libc:

| Symbol | Guest `libc.so` offset | Size |
| --- | ---: | ---: |
| `__aeabi_fadd` | `0x9f1e8` | `0x2ac` (684 B) |
| `__aeabi_dmul` | `0xa03a0` | `0x310` (784 B) |
| `__aeabi_fmul` | `0xa06f4` | `0x1f0` (496 B) |

That library is the ARM32 bionic the wrapper loads into the guest, so **its code is
executed by Dynarmic like everything else in the guest**. The helper is not a fast path —
it is a second translation target.

And soft-float is implemented in **integer bit manipulation**: normalisation, shift/mask,
rounding and branch-heavy exponent handling. That is close to the worst possible workload
for a JIT, and it is the same shape as the cost model's "double-FP loop" entry — the
mechanism is different, the consequence is the same.

**Net cost of one `a * b` in the engine today:**

```text
guest `bl` -> boundary/veneer -> ARM32 __aeabi_fmul (0x1f0 = 496 bytes of
soft-float integer code, translated by Dynarmic) -> return
```

against a single native `fmul` instruction if the same body ran as ARM64.

## 3. Why this diverges from the parallel shortlist analysis

A concurrent analysis reached the correct observation in §1 but drew the opposite
conclusion — that because "there are no guest FP instructions to translate", the 82.7%
guest slice must be integer/memory/control-flow volume, and that the target is therefore
"integer/loop bodies, not vector/quaternion math".

That inference misses §2: the arithmetic *is* translated, just as integer soft-float inside
guest libc rather than as FP opcodes. The 8,518 `__aeabi_fmul` call sites are not cheap
native calls — each one descends into ~500 bytes of translated ARM32 bit-twiddling.

The practical ranking is therefore unchanged in direction and strengthened in magnitude:
**rank by single-precision arithmetic density times per-frame execution** — vertex
stream transformation, matrix multiply/compose, quaternion ops and skinning — and treat
double-precision as a rounding error (2.7% of sites).

## 4. Budget arithmetic

From the device profile: 25.7 ms/frame total, of which ~21.25 ms is the translated guest
slice (~82.7%) and ~3.9 ms is the translator itself (~15.1%).

| Guest code made native | Frame cost | Implied rate |
| --- | ---: | ---: |
| none (today) | 25.7 ms | ~25 FPS |
| 20% | 21.5 ms | ~46 FPS |
| 50% | 15.1 ms | ~66 FPS |
| 100% (theoretical floor) | 4.45 ms | ~225 FPS |

Two consequences:

* The "hundreds of FPS" ceiling is real but only in the limit where *all* guest code
  becomes native — which is the tier-3 static-recompilation endpoint, not something a
  handful of ported functions reaches.
* **~50% of the guest slice is enough to clear 60 FPS**, and that is the goal. Since the
  engine's own limiter already sits at ~60 FPS (light scenes measured ~58.9), a held 60 is
  the realistic prize.
* Because soft-float is a *call per operation*, eliminating it inside a hot body removes far
  more than the body's own instruction share — a function that is 100 translated
  instructions but makes 40 soft-float calls is really ~20,000 translated instructions.

## 5. What this changes

1. `PERFORMANCE-BASELINE.md` §4's ZettaBridge cost-model table is **not** the applicable
   model; replace it with the per-operation soft-float call cost above.
2. The shortlist should be built on soft-float call density, which can be measured
   statically and exactly (`__aeabi_*` call sites per function) — this is a much better
   proxy than "looks like math" and does not need a working profiler.
3. The single highest-value measurement now becomes: **per-function `__aeabi_*` call-site
   counts**, especially in the per-frame scene/driver/vertex paths. That is a cheap static
   analysis over the existing disassembly.
4. Replacing the guest's `__aeabi_*` with native ARM64 implementations is a tempting
   shortcut and is **not** sufficient on its own: the call crossing still happens per
   operation. The win requires the *arithmetic itself* to move into native code, i.e.
   native bodies that use real `fmul`/`fadd`.
