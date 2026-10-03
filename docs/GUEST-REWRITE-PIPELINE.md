# Guest rewrite pipeline — VFP replacements for hot soft-float functions

**What this is.** A working pipeline that takes one original ARM32 function out of
the pristine DH2 engine, gets a VFP replacement for it, **proves the replacement
bit-identical by executing both**, and packages the result as a hash-pinned engine
patch in the same style as the reviewed patch already in `DH2Work/patches/engine/`.

**The constraint it works under.** ZettaBridge and Dynarmic may not be patched, so
the only lever is guest bytes. The engine contains no floating-point instructions
at all; every `a*b` is `bl __aeabi_fmul` into a guest libc body of ~124 translated
instructions. A VFP rewrite replaces that call with one translated instruction —
but only if it is bit-exact, which is the entire job of this pipeline.

Code: `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\rewrite-pipeline\`
(see its `README.md` for the file map and the how-to).

---

## 1. What works end to end, and what the worked example measured

`pipeline.py` runs four stages on one candidate and this is a real transcript:

```
stage 1/4  disassemble the original
function   _ZNK6glitch4core10quaternionmlERKS1_
address    0x0060dd34   mode=arm   size=448 B   112 instructions
imports    __aeabi_fmul x16, __aeabi_fsub x6, __aeabi_fadd x6
VFP insns  none (soft-float body)

stage 2/4  assemble the candidate        (NDK clang, .text pinned at 0x0060dd34)
stage 3/4  differential test
engine xchk 48 cases on the ORIGINAL body: PASS
cases       2319 (319 edge/special + 2000 random), seed=20631002
mismatches  0 / 2319  ->  PASS
stage 4/4  package the patch
replacement 164 B of the original 448 B at 0x0060dd34 (41 instructions)
output sha  afb56d36b9684f4cfaea050387a3f3af5ae5c37e5a1b4e638f40461d946cc4a7
bytes changed 164, all inside the 164-byte site: True
```

### The function

`glitch::core::quaternion::operator*(quaternion const&) const` at `0x0060dd34`
— 448 B, 112 instructions, 28 soft-float calls (16 `__aeabi_fmul`, 6 `fsub`,
6 `fadd`), no branches, no indirect calls, output 16 bytes through a hidden sret
in `r0`. It is cited in `DH2Work/docs/SOFTFLOAT-CENSUS.md` §2.2 (row 22, weighted
density 1105.7).

### The recovered formula (this is the whole rewrite)

`tools/sf_recover.py` walks the soft-float calls and rebuilds the tree; the
grouping it prints is the original's, and reproducing it is what makes the
rewrite exact:

```
out[3] = ((((this3*p3) - (this0*p0)) - (this1*p1)) - (this2*p2))
out[0] = ((((this0*p3) + (this3*p0)) + (this2*p1)) - (this1*p2))
out[1] = ((((this1*p3) + (this3*p1)) + (this0*p2)) - (this2*p0))
out[2] = ((((this2*p3) + (this3*p2)) + (this1*p0)) - (this0*p1))
```

`candidates/quat_mul.S` is that tree, one `vmul`/`vadd`/`vsub` per operation, in
that order, never fused. 41 instructions, 164 bytes, 40 of them VFP.

### The measured result

| quantity | value |
| --- | --- |
| differential cases | **2,319** (319 edge/special + 2,000 random, seed 20631002) |
| mismatches | **0** |
| fields compared per case | r0-r12, sp, lr, exit pc, APSR flags, all 32 s-registers, every written page |
| engine cross-check before the verdict | 48/48 cases identical on the pristine body |
| edge cases passed | all 319: ±0, ±inf, quiet and signalling NaNs with payloads, min/max denormals, the 2²³/2²⁴ rounding boundaries |
| original, translated instructions per call | **3,168** (140 body + 3,028 for the 28 helper calls) |
| replacement, translated instructions per call | **41** |
| reduction | **77.3×**, i.e. 98.7 % of the translated instructions removed |

The 3,028 figure is not hand-waving: the oracle services `__aeabi_*` on the host,
so the helper bodies never execute under emulation and a naive count would report
140. `tools/dh2_cost.py` adds them back from the measured sizes in
`NATIVE-PORT-SHORTLIST.md` §9 (`__aeabi_fmul` 124 instructions, `fadd` 171,
`fsub` 3, …).

**This is not a device measurement.** The device numbers (25.7 ms/frame, 82.7 %
JIT ⇒ a 21.3 ms JIT slice) are from the device work in the brief. Converting a
per-call instruction ratio into milliseconds requires this function's share of
the frame, which no profiler can attribute inside the JIT. The pipeline reports
the ratio and stops there.

### The negative control, which is the point

`candidates/quat_mul_wrong.S` is the same function with `out[0]`'s addition
reassociated — algebraically identical, bit-different on about 1.6 % of inputs:

```
original:  ((b0*a3 + b3*a0) + b2*a1) - b1*a2
control:   (b0*a3 + b3*a0) + (b2*a1 - b1*a2)

cases       519 (319 edge/special + 200 random)
mismatches  14 / 519  ->  FAIL
  memory (0x71001000, 'bytes differ', [0], 1)      <- one byte, in the output
```

The pipeline rejects it, reports exactly which byte diverged, and
`dh2_package.py` refuses to package it. A rewrite that is *expected* to fail and
does fail is the evidence that a PASS means something.

---

## 2. The ABI assumptions this pipeline encodes, and where each was verified

Everything in this table was confirmed **by experiment on this machine**
(`scratch/abi_experiments.py` reproduces all of it); nothing is taken from a
document. The conventions themselves are inherited from the project oracle rather
than re-derived — see `DH2-recon/STATUS.md` §0 and §4.

| # | assumption | how it was verified |
| --- | --- | --- |
| 1 | **AAPCS32 softfp**: float arguments and returns are raw 32-bit values in r0-r3, never in s0-s15 | the original body loads its float operands with `ldr r0,[r1,#4]`; the oracle's `__aeabi_f*` implementations read r0/r1 and return in r0, and they produce the project's recorded results |
| 2 | a by-value 16-byte return uses a **hidden sret pointer in r0**, shifting `this` to r1 | `quaternion::operator*` writes `[r0,#8]` as its first action and ends `mov r0,r4`/`pop {…,pc}`; the same convention is what `DH2-recon/STATUS.md` records as a fixed harness defect |
| 3 | soft-float arithmetic is **IEEE-754 binary32 round-to-nearest-even with no flush-to-zero** | `__aeabi_fmul(0x00000001, 1.0)` ⇒ `0x00000001`; `(0x007FFFFF, 1.0)` ⇒ `0x007FFFFF`; `(−0, 1.0)` ⇒ `0x80000000` |
| 4 | a **signalling NaN is quieted with its payload preserved** | `__aeabi_fmul(0x7F800001, 1.0)` ⇒ `0x7FC00001` |
| 5 | **r1-r3 and the condition flags are caller-saved** and are left as helper scratch | after a call, r1 holds the last helper's second argument, not the caller's value; the flags at exit come from the last comparison inside the helper |
| 6 | **VFP state is scratch**; the soft-float body never touches it | seeding s0/s31 and running the original leaves them exactly as seeded |
| 7 | a VFP rewrite must **not fuse** multiply and add | `vmla`/`vfma` round once; the original calls `__aeabi_fmul` then `__aeabi_fadd`, which round twice. `vfp_exec.py` refuses fused forms by name instead of approximating them |
| 8 | the **condition flags at entry belong to the caller** | the reviewed `engine_path_fix.S` relies on this and says so in its comment |

Because of 5 and 6, the harness takes an explicit **observable contract** per
candidate instead of demanding that every register match. The default is the ABI:
`r0`, `r4`-`r11`, `sp`; flags and VFP registers are **measured and reported as
informational**, and a spec can promote any of them to a requirement. The stack
frame the body pushes is declared `volatile`, so a 32-byte push and a 4-byte
frame are not reported as a memory divergence.

This is a deliberate trade: it stops the harness emitting false FAILs, at the
cost of a spec that could in principle under-specify. The worked example's spec
is 8 lines and every deviation it tolerates is printed on every run
(`informational (ABI-legal, not failures): regs=2 flags=z vfp=10 mem-pages=1`).

---

## 3. What the pipeline actually checks

For every case, both sides execute **from the same virtual address in the same
mapped image**, with the same stack, the same initial register file, and the same
pre-written memory blobs. The only difference is the bytes at the function's own
address:

* **original side** — the pristine engine bytes under Unicorn;
* **candidate side** — the assembled replacement, then the pristine bytes for the
  remainder of the object, so a short candidate can never leave a gap.

The harness refuses to run if the bytes it mapped at the site are not the
candidate's (a region-shadowing bug made a correct rewrite look like a crash
during development, so it is now a hard assertion).

Compared per case:

1. **r0-r12, sp, lr** and the exit pc, against the observable contract;
2. **APSR condition flags** N/Z/C/V (and Q/GE), informational by default;
3. **all 32 VFP registers** s0-s31 as raw bit patterns — seeded with distinctive
   values, so any accidental dependency on incoming VFP state shows up;
4. **every 4 KiB page either side wrote**, byte for byte, with the differing
   offsets printed; writes to a declared `volatile` range (the stack frame) are
   reported separately;
5. **the sequence of imports called**, so a replacement that silently reaches a
   helper instead of computing is visible.

Case generation: an `edge_cases: "auto"` set sweeps ±0, ±inf, quiet and signalling
NaN payloads, min/max denormals and the 2²³/2²⁴ rounding boundaries through every
float slot of every input blob, plus whole-blob sweeps; then random cases mixing
a bounded uniform range with raw 32-bit patterns (which is what reaches denormals
and huge exponents).

Two guard rails:

* **Engine cross-check.** Before any verdict, the pristine body is executed on
  *both* engines and the states are required to be identical. If they disagree
  the pipeline reports failure of the harness, not of the rewrite.
* **Negative control.** Documented above; a candidate expected to fail must fail.

---

## 4. The engine problem, stated plainly

**Unicorn 2.1.4's ARM target executes no VFP instruction at all**, in every CPU
model it exposes:

```
vmul.f32 s2,s0,s1    Invalid instruction (UC_ERR_INSN_INVALID)
vldr s4,[r0]         Invalid instruction (UC_ERR_INSN_INVALID)
vstr s4,[r0]         Invalid instruction (UC_ERR_INSN_INVALID)
vmov s0,r0           Invalid instruction (UC_ERR_INSN_INVALID)
add r0,r0,#1         executes
```

The `vfp_adds`/`vfp_sqrts` symbols inside `unicorn.dll` carry the qemu AArch64
naming (`vfp_get_fpscr_arm`) and belong to the AArch64 translator, not to an
AArch32 VFP unit.

So the original (all soft-float) side runs on real emulation, and the candidate
side runs on `tools/vfp_exec.py`: a small interpreter for exactly the instruction
subset this pipeline emits, which decodes **capstone's** parse of each word and
evaluates one IEEE-754 binary32 round-to-nearest-even operation per instruction,
with correct NaN quieting and payload propagation. Three independent checks keep
it honest, and all three run on every invocation of `tools/test_vfp_exec.py` and
in `pipeline.py --selfcheck`:

| check | what it proves | current result |
| --- | --- | --- |
| decode table vs capstone | every operation selector maps to the right mnemonic | agrees on all 32 selectors |
| operand decode vs capstone | operation, every register index, the memory offset | 11 memory + 6 arith forms match |
| executor vs the project oracle | the same bits the original ARM32 produces | 200/200 random quaternion products identical |
| engine cross-check | the executor and Unicorn agree on non-VFP code | 48/48 cases identical |

Both the decode table and the operand decoding were originally hand-derived and
were **wrong** — the table decoded `vmul` as `vnmla`, the `D`-bit extension was
missed so `s19` aliased `s9`, and the `VLDR` immediate was read from the wrong
field, loading from 104 bytes past the pointer. Each produced silent wrong
answers with no error, and each was caught only because the executor is compared
against something external. That is why those checks are mandatory rather than
advisory.

---

## 5. What this pipeline genuinely cannot verify

Listed honestly, because a PASS from this pipeline is only as strong as this list.

1. **The device itself.** Everything here is emulation plus host IEEE-754. If the
   device's ZettaBridge/Dynarmic fp environment runs with **flush-to-zero or
   default-NaN** enabled (an FPSCR/FPCR setting this pipeline has no way to
   observe), then VFP operations on subnormals would diverge from the soft-float
   helper, which does not flush. Zero denormal mismatches were measured under the
   modelled environment, so the rewrite is exact *there*; a device-side run is the
   only way to close this. This is the single biggest caveat.
2. **Whether the JIT maps VFP to NEON 1:1.** The speedup argument assumes one
   translated VFP instruction costs roughly one native instruction. The pipeline
   measures translated-instruction counts, not time; the 77× is a count ratio and
   the milliseconds remain unmeasured.
3. **Behaviour that depends on engine state the harness does not model.** The
   harness maps the ELF's `PT_LOAD` segments, a stack, a scratch arena and a bump
   heap, and implements the project oracle's import table. A body that reads a
   global initialised by a constructor, a static that a previous call mutated, a
   vtable slot, a mutex, a thread id, or any memory the loader would have laid
   out differently is **not** covered. Such a candidate can pass here and fail on
   device.
4. **Calls that are not in the oracle's import table.** Any import the oracle has
   no implementation for marks the run `unsupported` and the pipeline reports a
   **skip**, never a PASS. A candidate that calls *anything* is refused outright by
   the VFP engine — a replacement that needs a callee is a linked program, not a
   rewrite.
5. **Indirect control flow.** `ldr pc,[…]`, `blx rN`, `mov lr,pc` and `bx rN` are
   reported by the disassembler and are outside what the harness models. A
   function reached only through a vtable slot is testable in isolation, but its
   *dispatch* is not; and if the body's behaviour depends on which object the
   vtable belongs to, the case generator cannot know that.
6. **Function boundaries taken on trust.** Sizes come from `st_size` (or the next
   symbol). Where linker ICF/folding has attached a name to an unrelated body (the
   project has seen this) the *bytes* are still the truth and the disassembly
   still governs, but the spec's name may be wrong.
7. **Rounding modes other than round-to-nearest-even**, FPSCR exception traps, and
   anything that reads FPSCR. The executor refuses what it does not model rather
   than approximating it.
8. **`vdiv`, `vsqrt` and the fused forms.** Refused by name with a reason. A
   rewrite that needs `vdiv` needs its own rounding discussion (and its own oracle
   comparison), not a silent host call.
9. **Intra-function branches.** `sf_recover.py` is straight-line only; it stops
   and says so at the first branch. A body with a data-dependent path needs a
   per-path case set that this pipeline does not yet generate.
10. **The candidate's own correctness as a program.** The harness proves the
    candidate computes the same function as the original **for the inputs it was
    given**, under the observable contract the spec declares. Under-specifying the
    contract (dropping `r1` from `regs`, widening `volatile`, promoting `flags` to
    `false`) weakens the proof, and nothing in the tool stops a spec author from
    doing that.

---

## 6. Reproducing everything in this document

```bat
set PY=C:\Users\NacWorkstation\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
cd C:\Users\NacWorkstation\Documents\DH2Work-toolchain\rewrite-pipeline

%PY% pipeline.py --selfcheck                        :: decoder + oracle checks
%PY% pipeline.py --spec candidates\quat_mul.json    :: 2,319 cases, package
%PY% tools\dh2_test.py --spec candidates\quat_mul.json --cases 0    :: 319 edge cases only
%PY% tools\dh2_test.py --spec candidates\quat_mul_wrong.json        :: must FAIL
%PY% tools\dh2_cost.py --spec candidates\quat_mul.json              :: 3168 -> 41
%PY% scratch\abi_experiments.py                     :: every ABI fact in §2
%PY% tools\sf_recover.py --elf <pristine.so> --addr 0x0060dd34 ^
      --arg r0=out --arg r1=this --arg r2=p          :: the recovered tree
```

The pristine engine used throughout is
`DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\libDungeonHunter2.so`
with SHA-256 `36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`;
both the test driver and the packager refuse to run against anything else.

Toolchain: Android NDK r29 `clang.exe`/`ld.lld.exe`/`llvm-objcopy.exe` targeting
`armv7a-linux-androideabi21` with `-march=armv7-a -mfloat-abi=softfp
-mfpu=vfpv3-d16 -ffp-contract=off -fno-fast-math`. The host tag is resolved the
way the reviewed engine patch had to resolve it. `objcopy -O binary` prepends any
allocated section it finds, so `.ARM.attributes` is removed explicitly and the
linked `.text` address is read back from the ELF and required to equal the
original function's address — otherwise a patch can silently land 80 bytes early.

## 7. What a patch looks like

`out/quat_mul/quat_mul-patched.so` — 15,938,284 bytes, SHA-256
`afb56d36b9684f4cfaea050387a3f3af5ae5c37e5a1b4e638f40461d946cc4a7`, differing
from pristine in exactly 164 bytes, all inside `[0x0060dd34, 0x0060ddd8)`, with
the remaining 284 bytes of the original body left byte-identical.
`out/quat_mul/quat_mul-patch-report.json` records the site address, both hashes,
the original and replacement bytes, and the instruction counts.

The site bytes start:

```
008a91ed 018ad1ed 029a91ed 039ad1ed 00aa92ed 01aad2ed 02ba92ed 03bad2ed
 vldr s16,[r1]   vldr s17,[r1,#4]  vldr s18,[r1,#8]  vldr s19,[r1,#0xc]
```
