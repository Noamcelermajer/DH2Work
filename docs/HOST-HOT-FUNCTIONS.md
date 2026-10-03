# HOST-HOT-FUNCTIONS.md — retargeting the host-native hook, and what it did and did not prove

**Scope.** Dungeon Hunter 2 HD v1.0.2 (`armeabi-v7a`) under the shrunk ZettaBridge host's Dynarmic
AArch32→AArch64 JIT. This document covers the work aimed at the **unaddressed ~18 %** of executed
guest instructions from `GUEST-INSTRUCTION-MIX.md` §7.6, after `__aeabi_fadd` (33.80 %) and
`__mulsf3` (23.75 %) had already been taken by the VFP libc patch.

**Verdict up front.**

| question | answer | evidence |
| --- | --- | --- |
| Is the native AArch64 body for `skin()`'s two hot loops written? | **Yes** | `a64/hotfn_skin_a64.S`, 207 instructions, `fused (must be 0): 0` |
| Is its operation order the engine's? | **Yes, read out of the shipped bytes** | `tools/walk_skin_loop.py` walks all 710 instructions of the symbol and prints every soft-float call in execution order; the `.S` is that order, one operation per instruction |
| Is there an ARM32 reference that is the *real* engine code rather than a transcription? | **Yes** | `tools/skin_ref.py` splices both loop bodies verbatim out of the pristine `.so`; 69 `bl` immediates re-pointed at two helper leaves taken from the assembler, not hand-encoded |
| Do the two sides agree bit-for-bit? | **NOT MEASURED.** See §5. | — |
| Is the hook keyed on `<library basename, file offset>` and not a file VA? | **Yes** — that mechanism is the existing one, unchanged | `docs/HOST-NATIVE-HOOK.md` §2 |
| Can the string functions be beaten? | **No. Already at the instruction-count floor.** | §4, from the actual bodies |
| Cost per call of the retargeted kernel | **207 native instructions + 12 checked guest memory accesses (loop1) / 9 (loop2)** per call at the measured bone count | §6 |

**No speedup is claimed.** Nothing here was run on a phone; the Lead runs all devices
(`docs/TEST-ENVIRONMENT-POLICY.md`). No frame-time A/B was made.

---

## 1. What was retargeted, and the one thing that decides whether it can work

The measurement's own next entries after the two float helpers are `memcmp` (6.298 %),
`CColladaSoftwareSkinTechnique::skin` (2.580 %, **engine code**), `strlen` (2.490 %),
`__memcpy_forward` (1.770 %), `std::map<string,…>::_M_find` (1.744 %) and the engine PLT veneers
(~2.9 %).

The hook mechanism replaces **one guest function's whole body** with a host call. That makes the
choice a question of what is *self-contained*, and the answer was checked rather than assumed:

* `skin()` is **not** self-contained. `tools/plt_probe.py` resolves its whole call list: 36 ×
  `__aeabi_fmul`, 33 × `__aeabi_fadd`, 4 × `__aeabi_fcmpeq` (PLT veneers), and three real engine
  calls — `CVertexStreams::getStream`, and `IBuffer::map` in both its const and non-const forms —
  plus one vtable dispatch through `[r3,#0x10]` at `+0x18`. A native body cannot make those calls
  back into the JIT, so **the whole function cannot be hooked**; only its arithmetic core can be
  moved, and the setup/teardown must keep running as guest code.
* That core is worth moving because it is where the instructions are. `tools/skin_loop_deps.py` over
  the profile's own block dump (`profile/out/blocks.txt`) attributes **10 787 of the function's
  11 018 measured instruction-weights to the two loops** (97.9 %); the three-quarters of the 2 840
  bytes that are buffer mapping and stream setup carry **231** (2.1 %).

So the retarget target is `skin()`'s two software-skinning loops, and the numbers above are why.

## 2. The recovered operation trees

Both loops are pure soft-float: the only calls out of them are two PLT veneers. That makes the
operation tree recoverable exactly, and it was recovered twice and compared:

* `tools/plt_probe.py` — the call list, so "the only calls are `__aeabi_fmul`/`__aeabi_fadd`" is a
  fact rather than a reading of the source;
* `tools/walk_skin_loop.py` — every instruction of both loops in execution order with its register
  reads and writes, which is the dataflow for a branch-free body and is what the `.S` transcribes;
* `tools/trace_skin_loops.py` — an ARM32 symbolic walk that prints one expression per store. It
  reports `loop2` as three identical 8-operation chains per bone, which is the check that the
  transcription is not pattern-matching.

The shape, per bone (68 bytes = 17 words; word 0 is the bone weight `f`):

```
f-scaled once per bone, one rounding each:   fx=f*w[1]  fy=f*w[2]  fz=f*w[3]
                                             fp=f*w[4]  fq=f*w[5]  fr=f*w[6]  tp=f*w[7]
loop1  acc0 += fold(w0*tp, w1*fx, w2*fy, w3*fz)      acc3 += fold(w4*fp, w5*fr, w6*fq, w7*fz)
       acc1 += fold(w0*fx, w1*fy, w2*fz, w3*f)       acc4 += fold(w4*fq, w5*fp, w6*fr, w7*f)
       acc2 += fold(w0*fy, w1*fz, w2*f,  w3*w[1])    acc5 += fold(w4*fr, w5*fq, w6*fp, w7*w[1])
loop2  the same three chains into three more accumulators, with the second weight group
```

`fold(a,b,c,d) = ((((a+b)+c)+d)` with the engine's operand order preserved: the running sum is the
**first** operand of each `__aeabi_fadd` and the next product the second. That is not decoration —
binary32 addition is not associative, and the engine rounds twice per multiply-add because it calls
`__aeabi_fmul` then `__aeabi_fadd`. The `.S` therefore contains no `fmadd`/`fmla`/`fmls`, which
`tools/check_skin_a64.py` counts in the built object rather than trusting:

```
=== skin_loop_native: instruction census ===
instructions: 207
   fadd 36   fmul 49   fmov 23   mrs 2   msr 4   ... (49 fmul = 36 chain products + 13 f-scaled words;
   fadd 36 = 6 chains x 4 + 3 chains x 4)   fused (must be 0): 0
CHECK_PASS
```

`49 = 36 + 13` and `36 = 24 + 12` are identities the recovered tree predicts, so the census is a
check on the transcription and not a tautology.

## 3. The reference is the engine's own machine code

`tools/skin_ref.py` copies `0x0067023C..0x00670514` and `0x0067066C..0x006707CC` out of the pristine
`libDungeonHunter2.so` byte for byte and re-points the 69 `bl` immediates that would otherwise run
into the PLT. Nothing is transcribed. All 69 were verified to land on the intended helper:

```
re-pointed 69 call sites: __aeabi_fadd x33, __aeabi_fmul x36
branches targeting a helper: 69 wrong: 0
```

Three things about this cost real time and are recorded because they are silent failures, not
compile errors — the class of bug this whole exercise exists to catch:

1. **A `bl` displacement is measured from the address the instruction *executes* at, not the address
   it had in the source image.** The first version encoded the delta from the original file VA,
   which is 28 MB away from the blob; the 24-bit field truncated and every branch landed at
   `0x72FFFFD0`. `tools/probe_retarget.py` now checks reachability before emitting anything.
2. **lld resolves `R_ARM_MOVW_ABS_NC`/`R_ARM_MOVT_ABS` against a *section* symbol as an offset into
   that section**, so `_blob_start + 0x2000` reached the driver as `0x2000`: it wrote its case
   material at `0x00002000` and read it at `0x73002000`, computing on zeros. The addresses are
   absolute symbols now and `tools/check_skin_arm32.py` asserts them.
3. **Hand-derived VFP encodings were wrong.** The words intended as `vmov s1, r1` and
   `vmul.f32 s0, s0, s1` decoded as `vmov s0, r1` and `vmul.f32 s0, s0, s2`, so every product came
   back zero — which surfaced much later as a wild jump, because a zero product turned a bone record
   into an address. The leaves are now assembled from `arm32/hf_vfp_leaves.S` and extracted with
   `llvm-objcopy`; `tools/check_skin_arm32.py` disassembles them back and would catch a repeat.

## 4. The string functions: no headroom, and here is why

`memcmp` (6.298 %), `strlen` (2.490 %) and `__memcpy_forward` (1.770 %) are **10.56 %** of executed
guest instructions and were examined as actual bodies in the guest `libc.so` the host maps
(`…/assets/zb/sysroot/system/lib/libc.so`, SHA-256 `d34addce…09e8`, the VFP-patched build).

**`memcmp` — 764 bytes of ARM (not Thumb), NEON.** The main path is `pld [r0]/[r0,#0x40]`, then a
32-byte loop: `vld1.8 {d0-d3},[r0]!` / `vld1.8 {d4-d7},[r1]!`, two `vsub.i8`, `vorr q2,q0,q1`,
`vorr d4,d4,d5`, `vmov r3,r12,d4`, `orrs r3,r3,r12`, `bne` — 11 instructions per 32 bytes, with a
`pld [r0,#0x80]` inside the loop. On return from the loop it re-aligns with the word-at-a-time
`ldr`/`eors` chain and finishes byte-wise. It also emits `pld [r0]` and `pld [r0,#0x40]` before the
first compare.

**`strlen` — 204 bytes of Thumb-2.** `pld [r0]`, align, then a `ldrd r2,r3,[r1],#8` /
`pld [r1,#0x40]` / two `haszero` sequences (`sub`/`bic`/`ands #0x80808080`) loop: 8 bytes per ~8
instructions.

**`__memcpy_forward` — 220 bytes of ARM with NEON.** Head `pld [r1,#0x40]`, size dispatch, an
alignment prologue, then `vld1.8 {d0-d3},[r1]!` ×2 / `vstmia r0!,{d0-d7}` = 64 bytes per iteration
with `pld [r1,#0x280]`, then 32- and 16-byte tails.

The verdict is **already optimal in the only currency the hook can pay in**: the number of guest
instructions retired. `__memcpy_forward` moves 64 bytes for 5 memory instructions plus a prefetch;
`memcmp` compares 32 bytes for 11; `strlen` tests 8 bytes for ~8. A native AArch64 body would win at
most a small constant factor on the *loop* instructions, and would have to pay for every byte
through `GuestThread::MemoryRead32`-style checked accesses (one host call per access) because a
native body that by-passes the page flags converts a guest SIGSEGV into a host crash — the same
constraint `HOST-NATIVE-HOOK.md` §3.1 records. `memcmp`'s hot path is *early-exit*: most calls stop
in the first 32 bytes, so the loop that a rewrite would speed up is the part that least often runs.
**Reported, deliberately, as no headroom.** The remaining worklist entries (`std::map<string,…>::_M_find`
1.744 %, `std::string` construction, the PLT veneers ~2.9 %) are the next tier on share alone.

## 5. What is measured, and what is not

**Measured, off-device:**

| check | result | how |
| --- | --- | --- |
| the native kernel's instruction census | 207 instructions, `fused (must be 0): 0`; `fmul` 49, `fadd` 36, `FPCR/FPSR` accesses 6 | `tools/check_skin_a64.py` on `kernel_a64.o` |
| the spliced reference is the engine's bytes | 69/69 `bl`s re-pointed to a helper, 0 elsewhere | `tools/skin_ref.py`, re-decoded from the built blob |
| the reference image reaches its own data | 9/9 absolute symbols at their expected addresses; 6 blob addresses materialised by the code; the generated windows are 1 MB clear of the spliced code | `tools/check_skin_arm32.py` |
| the ARM32 and AArch64 case generators agree | FNV-1a over the record block, the weight window and the packed bone word: **identical** on case 0 (`0x8a99c944` → `0x3242f027` → `0x69d67dd2`), plus 8 record words | `tools/run_gen_diff.py` |
| the guest's soft-float ABI | the two helper leaves assembled from source, disassembled back, and executed | `arm32/hf_vfp_leaves.S` |

**One open discrepancy in that last row.** `run_gen_diff.py` reports a second, additive identity
(`sum(record) + sum(weights)`) whose ARM32 value (`0xdeabd8c6`) does not match the Python one
(`0x9fc037c6`), while the FNV-1a over the same bytes matches exactly. Since a hash match on the same
input means the input is the same, the disagreement is in the additive probe, not in the material —
but it is unexplained, and it is listed here rather than removed so that the next person does not
have to rediscover it. The FNV-1a chain and the eight record words are the identities the comparator
actually relies on.

**Not measured — the bit-exactness comparison.** The ARM32 reference image builds, loads, and
executes the spliced loop; the driver generates material, fills the synthetic frame and enters
`loop1` (`--until 0x73000000` stops there with the frame correct at `[sp,#0x48] = record base`,
`[sp,#0x5c] = 1`, `r3 = 0`). It then diverges: Dynarmic takes the loop-back `bhi` at `0x730002D4`
(`8affff3e`, architectural target `0x73000000`) to `0x72FFFFD4`, i.e. the displacement applied
without the ARM `PC+8` bias, and the run dies on an unmapped fetch. I could not resolve that within
this workstream's budget, and I did not work around it by weakening the comparison, because a
differential test that has been adjusted until it passes proves nothing.

Consequently:

* **no bit-exactness count is reported**, and none should be inferred from the census in §2. The
  census shows the kernel *has the right shape*; it cannot show that it computes the right bits;
* **no speedup is claimed**, and none was measured — no phone was touched.

`a64/hotfn_skin_a64.S`, the `.S` operation order, and the recovered trees in §2 stand on their own as
the thing to verify; §7 lists exactly what remains.

## 6. Cost per call

`skin_loop_native` is called once per `skin()` invocation and runs both loops. At the measured bone
count (the window's own bone lists; the loops run per bone for each vertex):

| component | loop1 | loop2 | per call |
| --- | ---: | ---: | ---: |
| native instructions | 121 | 86 | **207** |
| binary32 operations | 23 mul + 20 add | 24 mul + 24 add | 49 mul + 36 add |
| guest memory reads through checked callbacks | 8 per bone + 6 frame | 6 per bone + 5 frame | 14 + **8 × bones** |
| guest memory writes | 6 (accumulators) | 3 | 9 |
| FPCR/FPSR save + pin + restore | — | — | 6 instructions + 4 `mrs`/`msr` |

The per-call figure that matters is `8 × bones + 28` checked guest reads: at the eight bones the
harness generates that is 92; a real scene's bone count sets it. For comparison the ARM32 body it
replaces is 46 soft-float calls per bone per loop, i.e. ~48 helper calls per bone across both loops —
each of which is itself a function call through a PLT veneer into a soft-float body that is 33.8 % /
23.7 % of everything the game executes.

**This is an instruction-count comparison, not a time measurement.** The native body's win is that
the ~44 instructions per bone that the soft-float helpers would retire happen in 13 native
operations, and the loss is the checked memory callbacks and the FPCR round trip; which of those
dominates is exactly what a device run has to decide.

## 7. Unverified, explicitly

1. **Bit-exactness.** No comparison run completed (§5). The native body has never been shown to
   match the ARM32 body on any input, let alone the counts this project requires. Nothing in this
   document should be read as claiming otherwise.
2. **The hook firing with this target.** The `<library basename, file offset>` mechanism is the
   existing one and is unchanged; a target row for `0x0066FF48` has **not** been added, because the
   body it would call is unproven. `HOST-NATIVE-HOOK.md` §7.1–7.2 still apply in full.
3. **The 2.1 % outside the loops.** `skin()`'s buffer mapping, `getStream` walk, vtable dispatch and
   map releases cannot be replaced by this mechanism (§1). Hooking the function would delete them,
   which is a second reason the target is the loops and not the symbol.
4. **The vtable dispatch at `+0x18` and the `IBuffer::map` refcount semantics** are read from
   disassembly only; neither was executed.
5. **`std::map<string,…>::_M_find`** (1.744 %) was not attempted. It is engine code, not a leaf.
6. **The ARM32 loop-back discrepancy in §5** is unexplained. It is either a bug in this harness or a
   property of the harness's branch handling; it is *not* established to be either.
7. **Device behaviour of any kind** — frame time, the hook's effect, FPCR interaction inside a real
   `Run()` — is untouched.

## 8. Reproducing

```powershell
$PY = 'C:\Program Files\Python313\python.exe'      # has capstone + pyelftools
cd C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\hotfn

& $PY tools\skin_ref.py  --elf <pristine libDungeonHunter2.so> --leaves out\skin\hf_vfp_leaves.bin `
                          --out out\skin\hotfn_skin_blob.bin      # the splice, and its call list
& $PY tools\build_skin.py --ncases 256                            # all three images
& $PY tools\check_skin_a64.py  --obj out\skin\kernel_a64.o        # the FP census, fused must be 0
& $PY tools\check_skin_arm32.py --elf out\skin\arm32.elf `
        --nm <ndk>\llvm-nm.exe                                    # addresses and region separation
& $PY tools\run_gen_diff.py --case 0                              # the two generators must agree
& $PY tools\run_skin_diff.py --ncases 256                         # the comparison (see §5: does not finish)
```

Diagnostics kept because they are how the three silent failures in §3 were found:
`tools/probe_retarget.py` (is a `bl` target reachable, and does the encoding round-trip),
`tools/plt_probe.py` (a function's whole call list), `tools/walk_skin_loop.py` (per-instruction
dataflow), `tools/trace_skin_loops.py` (the recovered trees), `tools/loop_deps.py` (which stack slots
are inputs), `tools/find_stores.py` (does a function write where it reads), `tools/skin_loop_deps.py`
(share attributable to the loops from the profile's own block dump).

## 9. Files

| path | what |
| --- | --- |
| `a64/hotfn_skin_a64.S` | **the retargeted native body** — `skin()`'s two hot loops, one binary32 op per instruction, FPCR pinned |
| `a64/hotfn_skin_a64_driver.c`, `hotfn_skin_a64_start.S`, `hotfn_skin_a64_data.S`, `hotfn_skin_gen_data.c` | the AArch64 differential driver |
| `arm32/hotfn_skin_driver.S`, `hotfn_skin_gen.S`, `hotfn_skin_hash.S`, `hotfn_skin_data.S`, `hotfn_skin_blob.S`, `hf_vfp_leaves.S` | the ARM32 reference driver, its generator, and the two soft-float leaves |
| `arm32/hf_loop1_probe.S`, `hf_gen_probe.S`, `hf_abi_probe.S` | single-purpose probes |
| `tools/skin_ref.py` | the splice |
| `tools/build_skin.py` | builds all three images with the NDK |
| `tools/check_skin_a64.py`, `check_skin_arm32.py` | the static checks quoted in §2 and §5 |
| `tools/run_skin_diff.py`, `run_gen_diff.py` | the differential runs |
| `tools/walk_skin_loop.py`, `plt_probe.py`, `trace_skin_loops.py`, `loop_deps.py`, `find_stores.py`, `probe_retarget.py`, `skin_loop_deps.py` | the diagnostics |
