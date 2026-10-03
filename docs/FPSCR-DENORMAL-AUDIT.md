# FPSCR `FZ`/`DN` and denormal audit

**Question this closes:** risk 1 and risk 2 of
[`SOFTFLOAT-VFP-REPLACEMENT.md`](SOFTFLOAT-VFP-REPLACEMENT.md) — *can `FPSCR.FZ` / `FPSCR.DN`
ever be non-zero in this guest, and does Dynarmic honour them?* If they can, VFP flushes
denormals and returns the default NaN where the shipped soft-float bodies do not, and the
bit-exactness claim breaks for those inputs.

**Verdict: safe as shipped — unconditionally, for the guest as it exists in this bundle.**
`FZ` and `DN` are **never set**: the guest contains exactly **10 FPSCR write instructions**, of
which 7 are masked and cannot reach bits 24–25; of the 3 that write the register unfiltered, the
two that matter (`fesetenv`, `feupdateenv`) are **unreachable**, and the third (`longjmp`) only
restores a value `setjmp` had read from FPSCR. Nothing else in the 21-library set (engine
included) touches FPSCR, `FPEXC` or `CPACR` at all. The
initial FPSCR is **0** — 0 by omission in the guest and 0 by value-initialisation in Dynarmic.
Dynarmic, however, **does** model and honour `FZ`/`DN`, and the differential harness now proves
the exposure is real: over an identical 8,858,370,048-comparison suite the exact VFP
replacement scores **0 mismatches at `FZ=0`** and **2,860,502,216 at `FZ=1`**. So the safety
argument rests entirely on the write census, not on Dynarmic ignoring the bits. A four-instruction
runtime guard that clears `FZ|DN` at helper entry is available and cheap if the bundle ever
changes (§6).

Everything below is marked **[measured]** (a command in §7 produced it), **[read]** (a source
citation), or **UNKNOWN**. No mismatch count appears here that was not produced by a run in §7.

---

## 1. Is `FZ`/`DN` ever actually set?

**No. There are 10 FPSCR write instructions in the whole guest, and the only two that write the
register unfiltered are unreachable.**

### 1.1 The census

Two independent methods agree exactly.

**Method A — mode-aware linear sweep.** `fpscr_validate.py` disassembles every sized `STT_FUNC`
symbol of all 21 libraries **in its own instruction set** (mode taken from `st_value & 1`) and
reports every `vmrs`/`vmsr`/`mrc`/`mcr`/`mcrr`/`mrrc` that mentions `fpscr`/`fpexc`/`fpsid`/
`mvfr`/`c1,c0,#2`. Result **[measured]**: 673 coprocessor transfers, of which **12 `vmsr` rows
= 10 distinct addresses**, all `vmsr fpscr, rN`; **zero** `mcr`/`mrc` to `p15,c1,c0,2` (CPACR),
**zero** `fpexc`/`fpsid`/`mvfr` access anywhere. Every other p15 access in the set is
`mrc p15,#0,rX,c13,c0,#3` (TPIDRURO/TLS) or a Capstone misdecode of data in `.text`.

**Method B — raw byte census.** `fpscr_writes.py` scans every executable **section** for both
encodings and gates the ARM 4-byte word form on ARM-mode containment (necessary: the ARM word
pattern also matches by accident inside Thumb-2 code — measured false positives in `libm`'s
`logf` and `powf`). Result **[measured]**: 827 transfers, **10 writes**, identical addresses.

| address | library | enclosing symbol | value written | filter |
|---|---|---|---|---|
| `0x0001ca48` | `libm.so` | `fesetenv` | `*(fenv_t*)r0` | **none — whole register** |
| `0x0001ca5c` | `libm.so` | `feclearexcept` | `FPSCR & ~excepts` | `bic` by `excepts` |
| `0x0001ca90` | `libm.so` | `fesetexceptflag` | `(*flagp & excepts) \| (FPSCR & ~excepts)` | `excepts` |
| `0x0001caa4` | `libm.so` | `feraiseexcept` | `FPSCR \| excepts` | `orr` by `excepts` |
| `0x0001cad4` | `libm.so` | `fesetround` | `(FPSCR & ~0xc00000) \| (r0 << 22)` | bits 22–23 only |
| `0x0001caec` | `libm.so` | `feholdexcept` | `FPSCR & ~0x9f` | bits 0–4, 7 only |
| `0x0001cb00` | `libm.so` | `feupdateenv` | `*(fenv_t*)r0` | **none — whole register** |
| `0x0001cb10` | `libm.so` | `feupdateenv` | `FPSCR \| (*env & 0x9f)` | bits 0–4, 7 |
| `0x0009f1dc` | `libc.so` | `__fe_raise_inexact` | `FPSCR \| 0x10` | bit 4 (IXC) only |
| `0x00068038` | `libc.so` | `longjmp` = `_longjmp` = `siglongjmp` | `*(jmp_buf+0x50)` | none, but the value is what `setjmp` read from FPSCR |

Evidence, `python dump.py libc.so 0x9f1cc 0x9f1e8` **[measured]** — the whole body of
`__fe_raise_inexact` at `libc.so:0x9f1d4`:

```
0x9f1d4  eef10a10  vmrs  r0, fpscr
0x9f1d8  e3800010  orr   r0, r0, #0x10      @ bit 4 = IXC, inexact
0x9f1dc  eee10a10  vmsr  fpscr, r0
0x9f1e0  e3a00000  mov   r0, #0
0x9f1e4  e12fff1e  bx    lr
```

`FZ` is bit 24 and `DN` is bit 25 (`src/dynarmic/frontend/A32/FPSCR.h:66-73` **[read]**), so:
`feclearexcept`, `fesetexceptflag`, `feraiseexcept`, `fesetround`, `feholdexcept` and
`__fe_raise_inexact` **cannot** set them (their masks never include bits 24–25) — this confirms
the carried-forward finding. That leaves three writes that could in principle carry `FZ`/`DN`
into the register: `fesetenv`, `feupdateenv`, and the `longjmp` restore.

### 1.2 Reachability of the three whole-register writers

`fpscr_callers.py` resolves every direct `bl`/`blx` in every sized FUNC symbol and attributes
in-library call sites; imports are taken from `.dynsym` `SHN_UNDEF` (which does not depend on
PLT layout).

**`fesetenv`** — only callers in the entire set **[measured]**:
```
nearbyint   at 0x197e4      nearbyintf  at 0x19864      nearbyintl  at 0x198f0
```
**`feupdateenv`** — only callers **[measured]**:
```
llrint at 0x1cb98   llrintf at 0x1cc2c   lrint at 0x1ccc8   lrintf at 0x1cd5c
```
(same callers for `feholdexcept` and `feclearexcept`, and `fetestexcept` from the same four;
`feraiseexcept` from `lround` only.)

**Neither the entry points nor their callers is reachable.** `fpscr_callers.py` reports, for
every library in the set (`imported by: -`), that **no module imports `fesetenv`, `feupdateenv`,
`feholdexcept`, `fesetexceptflag`, `feclearexcept`, `feraiseexcept`, `fesetround`, `fegetenv`,
`fegetround`, `fetestexcept`, `fegetexceptflag`, `nearbyint`, `nearbyintf`, `nearbyintl`,
`rint`, `rintf`, `lrint`, `lrintf`, `lrintl`, `llrint`, `llrintf`, `llrintl`, `lround`,
`lroundf`, `lroundl`, `llround`, `llroundf` or `llroundl`** **[measured]**. The only in-library
callers of `nearbyint*`/`lrint*`/`llrint*`/`lround*` are their own `l`-suffixed aliases; the
only in-library caller of `rint` is `scalb`, which is itself imported by nobody. So there is no
path — direct, PLT-routed or alias-mediated — from any guest module into `fesetenv` or
`feupdateenv`.

**`longjmp` (= `_longjmp` = `siglongjmp`, all at `libc.so:0x67f00`) is reachable** — the engine
imports `longjmp` — and it *does* write the whole register **[measured]**:

```
0x68034  502090e5  ldr   r2, [r0, #0x50]
0x68038  102ae1ee  vmsr  fpscr, r2
```

but the value is exactly what `setjmp`/`__sigsetjmp` stored there from FPSCR
(`sigsetjmp` reads FPSCR at `libc.so:0x67de8` **[measured]**). A save/restore pair cannot
*introduce* `FZ=1`; it can only reintroduce a value that was already in FPSCR. It is therefore
not a source, only a propagation path.

`fesetexceptflag`, `fesetround`, `fegetenv`, `fegetround`, `fegetexceptflag`, `setjmp` and
`siglongjmp` have **no caller and no importer anywhere in the set** **[measured]**.

### 1.3 Direct writes outside `libm`/`libc`

**None.** `libDungeonHunter2.so` (the engine), `libnativeinterface.so`, `libStormGLOFT.so`,
`libc++.so`, `libstdc++.so`, `libz.so`, `liblog.so`, `libdl.so`, `ld-android.so`,
`libdl_android.so`, `linker`, `libzbcompat.so`, `libzbjni.so`, `libandroid.so`, `libEGL.so`,
`libGLESv1_CM.so`, `libGLESv2.so`, `libjnigraphics.so` and `guest/zbhost` contain **zero**
`vmsr fpscr` and zero FPEXC/CPACR access **[measured]** — this was *verified, not assumed*:

* `libDungeonHunter2.so`'s `.symtab` has **31,021** sized `STT_FUNC` symbols; the mode-aware
  sweep disassembled all of them and found **0** FPSCR transfers.
* A raw byte scan of the engine's executable **segments** did flag two words
  (`0x008f5930`, `0x008f639c`), but both are in **`.rodata`** — `ft_adobe_glyph_list` is a
  `STT_OBJECT` of 54,791 bytes — not code. Restricting the scan to `SHF_EXECINSTR` sections
  removes them.
* `fpscr_gaps.py` sweeps the **complement** of every sized FUNC range in both instruction sets,
  so a write hiding in an unsized symbol or a jump table would surface: **0** `vmsr fpscr` in
  the gaps of all 21 libraries (`libDungeonHunter2.so`: 5,965,264 exec bytes, 99.92 % covered,
  4,830 gap bytes, 0 hits) **[measured]**.

### 1.4 Propagation is by inheritance only

`FZ`/`DN` can only propagate once set, and nothing sets them. For completeness, the three
ZettaBridge paths that move FPSCR between threads are copies of an existing value
**[read]**: `process.cpp:672` (`clone`/`fork`: `child->set_fpscr(parent.fpscr())`),
`process.cpp:362` (borrowed thread: `borrower->set_fpscr(carrier.fpscr())`),
`signals.cpp:293` (signal return: `thread.set_fpscr(uc.uc_regspace[1 + kVfpWords])`, i.e. from
the guest's own `ucontext`). None of them can fabricate `FZ`.

---

## 2. What is the guest's initial `FPSCR`?

**The guest never initialises it, and Dynarmic value-initialises it to 0.**

**Guest side [measured]:** there is **no FPSCR, FPEXC or CPACR access anywhere in `libc.so` or
`linker`** — the only two places a bionic would enable/seed the FPU. Specifically:

* No `mcr p15, #0, rX, c1, c0, #2` (CPACR write) and no `mrc` of it, in either instruction set,
  in `linker`, `libc.so`, `ld-android.so`, `libdl.so` or `libdl_android.so` (§1.1 methods A and
  B). The recurring `mrc p15,#0,rX,c13,c0,#3` is the TPIDRURO/TLS read used by `__get_tls`,
  `__errno`, `pthread_self`, `clone`, `__start_thread`, `__libc_init_common`,
  `__libc_preinit_impl`, `__dl___libc_init_main_thread_late/final`, …
* No `vmsr FPEXC` (so no `FPEXC.EN` dance either). Android's ARM32 ABI requires VFP, the kernel
  enables CP10/CP11 at `execve`, and this libc has no FPU bring-up code at all.
* No `.init_array` constructor and no `__libc_init` variant writes FPSCR — the sweep covers
  every sized FUNC symbol, which includes every constructor.

So on real hardware a fresh process would get whatever reset value the kernel leaves; under
this emulator it gets Dynarmic's value.

**Dynarmic side [read]:**
* `src/dynarmic/backend/arm64/a32_jitstate.h:28` — `u32 upper_location_descriptor;` carries the
  FPSCR mode bits and has **no** default member initialiser.
* `src/dynarmic/backend/arm64/a32_interface.cpp:155` — `A32JitState current_state{};`. Braced
  aggregate initialisation value-initialises the omitted member, so
  `upper_location_descriptor == 0`, hence `Fpscr() == 0`.
* `src/dynarmic/backend/arm64/a32_interface.cpp:75-77` — `void Reset() { current_state = {}; }`,
  same result.
* `src/dynarmic/backend/arm64/a32_jitstate.cpp:64-66` — `Fpscr()` returns
  `(upper_location_descriptor & 0xffff0000) | fpsr | fpsr_nzcv`; with both halves zero the
  result is 0.
* ZettaBridge constructs the JIT at
  [`guest_thread.cpp:53-64`](../../DH2Work-stage/compatibility/work/research/ZettaBridge/core/src/guest_thread.cpp)
  and never calls `set_fpscr` for a fresh thread.

**Resulting initial state: `FZ=0`, `DN=0`, `RMode=0b00` (round to nearest even), all five
exception-enable bits 0 (masked/trapped-none), all cumulative flags 0, `Len=1`, `Stride=1`.**
This is the ARM EABI default, but established here from *this* libc and *this* Dynarmic rather
than assumed.

---

## 3. What does Dynarmic do with `FZ`/`DN`?

**It models them, it stores what `VMSR FPSCR` writes, and it honours them — by programming the
host `FPCR`.** This is the pessimistic case, and it is the reason §1 had to be answered by
enumeration rather than by reading the emulator.

* **The bits exist.** `src/dynarmic/frontend/A32/FPSCR.h:66-73` defines `DN()` = bit 25 and
  `FTZ()` = bit 24; `:179` masks writes to `0xFFF79F9F` (both bits kept).
  `src/dynarmic/common/fp/fpcr.h:47-64` defines the same two bits on the emulator's own
  `FP::FPCR`, constructed directly from the guest FPSCR.
* **They are part of the block identity.** `src/dynarmic/frontend/A32/a32_location_descriptor.h:32`
  — `static constexpr u32 FPSCR_MODE_MASK = 0x07F70000;` includes bits 24 and 25; `:37` and
  `:90` mask the FPSCR into the descriptor, so `FZ=1` and `FZ=0` are different basic blocks.
* **`VMSR FPSCR` stores the guest value.** `src/dynarmic/frontend/A32/translate/impl/vfp.cpp:1130-1146`
  (`vfp_VMSR`) ends the block with `ir.SetFpscr(ir.GetRegister(t))`. The arm64 emitter
  `src/dynarmic/backend/arm64/emit_arm64_a32.cpp:669-688` masks with `0x07f70000` — keeping
  bits 24 and 25 — and stores into `upper_location_descriptor`; a separate mask (`0x0800009f`)
  takes the cumulative flags. `A32JitState::SetFpscr`
  (`src/dynarmic/backend/arm64/a32_jitstate.cpp:68-72`) with `FPCR_MASK = FPSCR_MODE_MASK`
  (`:61`) does the same for the C++ entry point.
* **They reach the host FPU.** The A32 run/step prelude
  `src/dynarmic/backend/arm64/a32_address_space.cpp:252-256` (and again `:290-294` for `Step`)
  does
  ```
  LDR  Wscratch0, [Xstate, offsetof(A32JitState, upper_location_descriptor)]
  AND  Wscratch0, Wscratch0, 0xffff0000
  MRS  Xscratch1, FPCR
  STR  Xscratch1, [SP, offsetof(StackLayout, save_host_fpcr)]
  MSR  FPCR, Xscratch0
  ```
  AArch64 `FPCR` has `FZ` at bit 24 and `DN` at bit 25 — the same positions as the ARMv7
  `FPSCR` — so the guest's `FZ`/`DN` are written straight into the host control register, and
  the host then flushes denormals and produces default NaNs for the `FADD`/`FMUL`/`VCVT`/…
  instructions the arm64 backend emits. There are no `FZ`/`DN`/`flushtozero`/`defaultNaN`
  references anywhere in `src/dynarmic/backend/arm64/` **[measured by grep]** precisely because
  the host register does the work.
* **Nothing gates it off.** The per-block `FP::FPCR` used by the emitter is built from the guest
  FPSCR (`emit_context.h:45-48` with `a32_address_space.cpp:401`:
  `FP::FPCR{A32::LocationDescriptor{location}.FPSCR().Value()}`), used with the default
  `fpcr_controlled = true`. ZettaBridge leaves `cfg.unsafe_optimizations` at its default `false`
  (`guest_thread.cpp:53-64`), and `interface/A32/config.h:138,151` confirm safe optimizations
  only.
* The AArch64-only `ASIMDStandardValue()` — which forces `FZ=DN=1` — is applied only on the
  `fpcr_controlled = false` path (`emit_context.h:47`), used for A64 ASIMD and A32 ASIMD, **not**
  for A32 VFP.
* Dynarmic's software FP path also consults the bits
  (`src/dynarmic/common/fp/unpacked.cpp:43,96` for `FZ`, `process_nan.cpp:34-35` for `DN`), so
  the behaviour is consistent whichever path an operation takes.

**One fidelity nuance, in the safe direction** **[read]**: `EmitA32SetFpscr` updates only the
JIT state; the host `FPCR` is re-programmed at the next `Jit::Run()`/`Step()` entry
(`a32_address_space.cpp:256`), not immediately at a mid-run `VMSR`. An `fesetenv` inside a long
run would therefore be delayed. This can only *reduce* exposure, and it is moot here because
`fesetenv` is unreachable.

---

## 4. Do the shipped `__aeabi_*` bodies flush subnormals?

**They implement IEEE-754 binary32 with full subnormal support; they do not flush, and they do
not consult `FZ`/`DN` at all.** Established by reading the body, and then confirmed by
measurement (§5).

`__aeabi_fmul` = `__mulsf3` at `libc.so:0xa06f4` (496 B, ARM). `python dump.py libc.so 0xa06f4 0xa0810`
**[measured]** shows the denormal handling explicitly — this is compiler-rt's `__mulsf3`
normalisation, not a flush:

```
0xa07c4  020555e3   cmp   r5, #0x800000     @ is |a| below the smallest normal?
0xa07c8  121f6fe1   clz   r1, r2            @ count leading zeros of the mantissa
0xa07d4  08604132   sublo r6, r1, #8        @ shift amount
0xa07d8  09506132   rsblo r5, r1, #9        @ exponent correction
0xa07e0  1226a0e1   lsl   r2, r2, r6        @ normalise a's mantissa
0xa07e8  130f6fe1   clz   r0, r3            @ ... and symmetrically for b
0xa07f8  1331a0e1   lsl   r3, r3, r1
0xa07fc  0201a0e3   mov   r0, #0x80000000
0xa0800  021582e3   orr   r1, r2, #0x800000
```

A subnormal *input* is normalised with `clz` + `lsl` and an exponent correction; a subnormal
*result* is produced by the corresponding shift path (the tail of the body, from `0xa0810`,
contains no `cmp …; mov #0` "flush to zero" branch anywhere). `__aeabi_fdiv`, `__aeabi_dadd`,
`__aeabi_dmul`, `__aeabi_ddiv` are the same compiler-rt shape
([`SOFTFLOAT-VFP-REPLACEMENT.md`](SOFTFLOAT-VFP-REPLACEMENT.md) §3.1 **[read]**).

The only VFP instructions these bodies contain are bit-pattern moves used to load constants and
to return — `vmov s0, r0` / `vmov r0, s0` (e.g. `0xa0754`, `0xa0758`, `0xa0770`, `0xa0774`) and
`vldr s0, [pc, #0x12c]` (`0xa07ac`, into `vmov r0, s0` at `0xa07b0`). None is arithmetic, so
none can be affected by `FZ`/`DN`.

**Corroborating the carried-forward measurements from the same bodies [read]:**
`__aeabi_fmul(0x00000001, 1.0) == 0x00000001`, `(0x007FFFFF, 1.0) == 0x007FFFFF`, and a
signalling NaN is quieted with its payload preserved — i.e. RNE with no flush, and independent
of `FZ`/`DN` (measured again in §5.1: the *reference* column is unchanged at `FZ=1`).

**One qualification this audit adds.** `__aeabi_fadd`/`__aeabi_dadd` are **not** unconditionally
fixed-RNE: `python fpscr_callers.py` and `dump.py` show `__addsf3` calling `__fe_getround`
(`libc.so:0x9f1b8`, body `vmrs r1, fpscr; ubfx r1, r1, #0x16, #2; …table lookup`, i.e. it reads
`FPSCR[22:23]`) at `0x9f3d8` and `__fe_raise_inexact` at `0x9f454`. `__mulsf3`/`__divsf3`/
`__muldf3`/`__divdf3` do **not** read FPSCR (no `vmrs` inside them **[measured]**). Measured
consequence at `RMode != 0`: §5.4.

---

## 5. Differential test, extended to the risk cases

Harness: the existing qemu-arm differential rig
(`DH2Work-scratch4\vfp-test\`, copied to `DH2Work-toolchain\fpscr\vfp-test\` so that nothing
outside the allowed scratch area is modified), extended in three ways:

1. **`vfp_set_fpscr(uint32_t)` / `vfp_get_fpscr()`** added to `vfp.S`; the driver takes the FPSCR
   to install as an optional 5th argument and sets it **before** any comparison, so the shipped
   body and the VFP replacement run under the same FPSCR.
2. **`test_denorm_band`** — exhaustive in *both* operands: `a` over all 2^24 bit patterns with
   exponent field 0 (both zeros and all 2·(2^23−1) subnormals) × 16 values straddling the
   smallest-normal boundary × both operand orders = **536,870,912** comparisons per helper.
3. **`test_smallest_normal`** — 20 values around `0x007fffff`/`0x00800000` crossed with each
   other and with every single-bit subnormal; **21,120** comparisons per helper.
4. **`test_f2d_exhaustive`** — a unary helper over **all 2^32** float bit patterns, as requested.

### 5.1 The sensitivity control (proof that the FZ test can fail)

`qemu-arm -cpu max ./vfptest nominal 0 1` **[measured]**, with `__aeabi_fmul` (the shipped body)
called in the same process:

```
FZ=0  vfp_fmul(min_denormal, 1.0) = 00000001  (soft-float ref = 00000001)
FZ=1  fpscr now                     = 01000000
FZ=1  vfp_fmul(min_denormal, 1.0) = 00000000  (soft-float ref = 00000001)
DN=1  vfp_fmul(sNaN, 1.0)         = 7fc00000  (soft-float ref = 7fc00001)
```

The harness really sets `FPSCR`, qemu really honours it, and the shipped body really ignores it.
Without this control a "0 mismatches at `FZ=0`" result would prove nothing.

### 5.2 The paired result — identical suite, only `FPSCR.FZ` differs

Twelve tests, the six exact (NaN-guarded) replacements over the denormal band, the exponent band
and the structured set. Same binaries, same seeds, same worker split; only `argv[4]` changes.

| test | coverage | comparisons | mismatches `FZ=0` | mismatches `FZ=1` |
|---|---|---:|---:|---:|
| `fmul_dd` | exhaustive 2^24 denormal `a` × 16 `b` × 2 orders | 536,870,912 | **0** | 100,663,284 |
| `fadd_dd` | as above | 536,870,912 | **0** | 436,207,580 |
| `fsub_dd` | as above | 536,870,912 | **0** | 436,207,580 |
| `fdiv_dd` | as above | 536,870,912 | **0** | 494,927,844 |
| `fmul_eb` | exhaustive 2^25 zero/denormal/inf/NaN `a` × 24 `b` × 2 orders | 1,610,612,736 | **0** | 402,653,148 |
| `fadd_eb` | as above | 1,610,612,736 | **0** | 268,435,432 |
| `fsub_eb` | as above | 1,610,612,736 | **0** | 268,435,432 |
| `fdiv_eb` | as above | 1,610,612,736 | **0** | 446,693,334 |
| `fmul_ex_s` | structured, all 256 exponents × both signs × 16 mantissas | 67,108,864 | **0** | 3,085,226 |
| `fadd_ex_s` | as above | 67,108,864 | **0** | 43,062 |
| `fsub_ex_s` | as above | 67,108,864 | **0** | 43,004 |
| `fdiv_ex_s` | as above | 67,108,864 | **0** | 3,107,290 |
| **total** | | **8,858,370,048** | **0** | **2,860,502,216** |

Divergence examples, printed by the harness (`FZ=1` only) **[measured]**:

```
fmul_dd   a=0000000000000010 b=000000003f800000  ref=0000000000000010  new=0000000000000000
fadd_dd   a=0000000000000000 b=0000000000000001  ref=0000000000000001  new=0000000000000000
fdiv_dd   a=0000000000000001 b=0000000000000000  ref=000000007f800000  new=000000007fc00000
fmul_ex_s a=0000000000800000 b=0000000033800001  ref=0000000000000001  new=0000000000000000
fdiv_ex_s a=0000000000000000 b=0000000000000001  ref=0000000000000000  new=000000007fc00000
```

Every one is the same mechanism: the shipped body returns the subnormal (or the correct
denormal/infinity result), the VFP instruction flushes the subnormal operand/result to zero —
and for `fdiv` the flush turns `0/x` into `0/0`, producing the default NaN `0x7fc00000`.

### 5.3 `DN=1` is harmless; the rest of the `FPSCR=0` suite

**`FPSCR = 0x02000000` (`DN=1`) — 3,892,314,112 comparisons, 0 mismatches** across `fnan_ex`
(2^24 NaN payloads × 10 `b` × both orders), `fnan_add_ex`, `fmul_dd`, `fadd_dd`, `fdiv_dd` and
`fmul_eb` **[measured]**. The guarded replacements return `quiet(a)`/`quiet(b)` before reaching
VFP, so default-NaN mode never engages. `DN` is therefore not a risk for the shipped patch list
even if it were set; `FZ` is.

**Zero-mismatch results at the real `FPSCR = 0`, in addition to §5.2** **[measured]**:

| test | comparisons | mismatches |
|---|---:|---:|
| `fmul_dd_plain` (unguarded `vmul.f32`) over the denormal band | 536,870,912 | **0** |
| `fmul_p_dd`, `fadd_p_dd`, `fdiv_p_dd` — bodies **inside `libc.patched.so`** | 3 × 536,870,912 | **0** |
| `fmul_sb`, `fadd_sb`, `fsub_sb`, `fdiv_sb` — smallest-normal boundary | 4 × 21,120 | **0** |
| `i2d`, `ui2d` — exhaustive, all 2^32 integers | 2 × 4,294,967,296 | **0** |

**Total at `FPSCR = 0` with zero mismatches: 19,595,872,768 comparisons.** (8,858,370,048 from
§5.2 + 2,147,483,648 from the three artifact denormal-band tests + 84,480 smallest-normal +
4,294,967,296 `i2d` + 4,294,967,296 `ui2d`.)

The plain (unguarded) forms reproduce the carried-forward NaN-priority divergence at `FPSCR=0`
and are **not** to be shipped: structured set, 67,108,864 comparisons each —
`fmul_s` **205**, `fadd_s` **205**, `fdiv_s` **205**, `fsub_s` **245,065** mismatches.

### 5.4 Two divergences this audit found that were not previously measured

**(a) `__aeabi_f2d` is not substitutable by bare `vcvt.f64.f32`.** Exhaustive over all 2^32
float bit patterns at `FPSCR = 0`: **8,388,606 mismatches / 4,294,967,296** **[measured]**:

```
f2d  a=000000007f800010  ref=7ff0000200000000  new=7ff8000200000000
f2d  a=000000007f800020  ref=7ff0000400000000  new=7ff8000400000000
```

The shipped body extends a signalling NaN **without setting the quiet bit**; `vcvt.f64.f32`
quiets it. `__aeabi_f2d` *is* one of the engine's 40 imports, so if it is ever added to the
patch list it needs the same NaN guard shape as the arithmetic ops (check `|a| > +inf` first).
It is not on the recommended list today. At `FZ=1` the same sweep gives **25,165,820**
mismatches = the 8,388,606 NaN cases + 16,777,214 = 2·(2^23−1) subnormal inputs flushed
(`a=0x00000010 → ref 0x36e0000000000000, new 0`) **[measured]** — the cleanest possible
demonstration of the `FZ` exposure.

**(b) The shipped `__addsf3`/`__adddf3` do not track `FPSCR.RMode` the way VFP does.** With
`RMode` non-zero the exact replacements diverge badly **[measured]**: at `FPSCR=0x00400000`
(round toward +∞) `fadd_s` 32,276,145 / 67,108,864 and `fadd_eb` 167,772,140 / 1,610,612,736;
at `0x00800000` (toward −∞) `fadd_s` 32,321,295 and `fadd_eb` 167,772,152; at `0x00c00000`
(toward zero) `fadd_s` 32,296,971. Example: `fadd_s a=0x00800000 b=0x00800001 ref=0x01000000
new=0x01000001` — the shipped body returns the round-to-nearest-even result where VFP rounds up.
`fmul_s` also diverges (29,407,273) purely because `__mulsf3` is fixed-RNE while VFP follows the
host `FPCR`: `a=0x00800000 b=0x00000001 ref=0x00000000 new=0x00000001`. **This does not affect
shipping either** — `RMode` is set only by `fesetround` (no caller, no importer) and by
`fesetenv`/`feupdateenv` (unreachable), and the initial value is `RMode=0`. It does mean the
"soft-float is IEEE RNE" premise is exact only at `RMode=0`. The precise mechanism by which
`__fe_getround`'s table maps `RMode` was **not** chased; the divergence is measured either way.

---

## 6. Verdict

**Safe as shipped, unconditionally — for this bundle (`zb-version.txt`
`9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06`), with the patch list of
[`SOFTFLOAT-VFP-REPLACEMENT.md`](SOFTFLOAT-VFP-REPLACEMENT.md) §10 and no additions.**

The chain of the argument, each link measured or read:

1. `FZ` and `DN` are bits 24 and 25 of `FPSCR` (`dynarmic/frontend/A32/FPSCR.h:66-73`).
2. The guest contains **10** `vmsr fpscr` instructions, no others; **7** of them cannot touch
   bits 24–25 by construction (§1.1 — `feclearexcept`, `fesetexceptflag`, `feraiseexcept`,
   `fesetround`, `feholdexcept`, `__fe_raise_inexact`, and `feupdateenv`'s second write, whose
   mask is `0x9f`).
3. The remaining **3** write the register unfiltered — `fesetenv`, `feupdateenv`, `longjmp`.
   `fesetenv` and `feupdateenv` are **not imported and not called by anything in the 21-library
   set**; `longjmp` writes the register but only with a value `setjmp` read from it (§1.2).
4. The engine and every other module have **zero** FPSCR/FPEXC/CPACR access, verified over
   31,021 engine symbols and over the complement of all symbol ranges (§1.3).
5. Nothing initialises FPSCR, and Dynarmic value-initialises it to **0** (§2).
6. `FZ`/`DN`, *if* set, would matter: Dynarmic honours them via the host `FPCR` (§3) and the
   measured cost is **2,860,502,216 mismatches in 8,858,370,048 comparisons at `FZ=1` vs 0 at
   `FZ=0`** (§5.2).

**Condition under which it stops being safe, and whether it is detectable at runtime.** The
argument is a property of the *bundle's contents*, not of the guest's behaviour, so the thing to
guard is a bundle change (a new `libm`/`libc`, a new third-party `.so`, a ZettaBridge update
that swaps the sysroot), not a runtime state. Two independent detections, both cheap:

* **Static, in the build:** re-run `fpscr_writes.py` + `fpscr_validate.py` against the new
  sysroot and fail the build if the write set grows or if `fesetenv`/`feupdateenv` acquires an
  importer. This is exact, and it is the check that actually closes the question.
* **Runtime, in the thing already being patched:** 4 extra instructions in the replacement
  prologue —
  `vmrs r3, fpscr; tst r3, #0x03000000; bicne r3, r3, #0x03000000; vmsrne fpscr, r3` — clears
  `FZ|DN` at helper entry and leaves them cleared for the rest of the process. Leaving them
  cleared (rather than restoring) is sound *only* because §4 shows the shipped bodies never read
  either bit, so no caller can observe the difference; it would be wrong to treat `RMode` the
  same way, because `__addsf3` does read `RMode` (`libc.so:0x9f3d8`). Cost: 4 of ~15 translated
  instructions, ~0.5 %. If a future bundle is unverifiable, ship this form.
* A boot-time one-line log of `FPSCR` (ZettaBridge already exposes `GuestThread::fpscr()`) is a
  third, weaker option — it observes rather than prevents, and cannot see a mid-frame write.

**Not needed:** anything about `DN` (measured harmless even when set, §5.3), and anything about
Dynarmic's `FZ`/`DN` default (it is 0, §2).

**What would change this answer:** a guest module that imports `fesetenv`/`feupdateenv`, a direct
`vmsr fpscr` appearing outside the 10 sites, or a bundle whose `libm` is a different build.
Nothing else — including NaNs, sNaN payloads, `±0`, `±inf`, the smallest-normal boundary and
every subnormal in the 2^24-pattern exponent-0 band — produced a single mismatch at `FPSCR = 0`.

---

## 7. Method — every command

Python 3.13.15, capstone 5.0.7, pyelftools 0.33, invoked as `python`. Scratch scripts and
harness copy live under `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\fpscr\`.

**Static census and reachability**

```powershell
cd C:\Users\NacWorkstation\Documents\DH2Work-toolchain\fpscr
python fpscr_validate.py      # mode-aware sweep of every sized FUNC symbol, 21 libs -> 673 rows, 12 vmsr, 0 CPACR/FPEXC
python fpscr_writes.py        # raw byte census of both encodings, mode-gated     -> 827 transfers, 10 writes
python fpscr_gaps.py          # complement of all sorted FUNC ranges, both modes -> 0 writes in gaps
python fpscr_callers.py       # per-library call graph + .dynsym UND importers   -> callers/importers of the fe* set
python dump.py libm.so  0x1ca30 0x1cb18   # the whole fe* cluster, ARM
python dump.py libc.so  0x9f1b0 0x9f1e8   # __fe_getround + __fe_raise_inexact
python dump.py libc.so  0x9f3c0 0x9f480   # __addsf3 calling __fe_getround / __fe_raise_inexact
python dump.py libc.so  0x67cc0 0x67d60   # setjmp / sigsetjmp (FPSCR save)
python dump.py libc.so  0x67f00 0x68050   # longjmp / siglongjmp (FPSCR restore)
python dump.py libc.so  0x9f060 0x9f1e8   # __adddf3 call sites
python dump.py libc.so  0xa06f4 0xa0810   # __mulsf3 subnormal normalisation path
python dump.py libm.so  0x1da00 0x1da30   # logf  false positive (Thumb-2)
python dump.py libm.so  0x1e1d0 0x1e200   # powf  false positive (Thumb-2)
python dump.py libDungeonHunter2.so 0x8f5920 0x8f5948  # .rodata false positive
```

**Dynarmic (pinned `86458a0bd369d63ba4c2ef812cacbb6c9080c065`, `git log -1` confirms, working
tree clean)** — `grep`/`read` over `src/dynarmic/`: `frontend/A32/FPSCR.h`,
`frontend/A32/a32_location_descriptor.h`, `frontend/A32/translate/impl/vfp.cpp`,
`backend/arm64/a32_jitstate.{h,cpp}`, `backend/arm64/a32_address_space.cpp`,
`backend/arm64/a32_interface.cpp`, `backend/arm64/emit_arm64_a32.cpp`,
`backend/arm64/emit_arm64_floating_point.cpp`, `backend/arm64/emit_context.h`,
`common/fp/fpcr.h`, `common/fp/unpacked.cpp`, `common/fp/process_nan.cpp`,
`interface/A32/config.h`; and `grep -r '\bFZ\b|\bDN\b|denormal' src/dynarmic/backend/arm64/`
→ no matches.

**Differential harness (WSL2 Ubuntu, `qemu-arm` 10.2.1, `arm-linux-gnueabihf-gcc` 15.2.0, 20 cores)**

```bash
wsl -d Ubuntu -- bash -lc "cd /mnt/c/.../DH2Work-toolchain/fpscr/vfp-test && bash build2.sh"
wsl -d Ubuntu -- bash -lc "cd /mnt/c/.../DH2Work-toolchain/fpscr/vfp-test && qemu-arm -cpu max ./vfptest nominal 0 1"
# the paired suite (FZ=0 vs FZ=1) and the DN/RMode/exhaustive runs:
wsl -d Ubuntu -- bash -lc "cd /mnt/c/.../DH2Work-toolchain/fpscr/vfp-test && \
  W=16 FPSCR=00000000 TAG=fz0_12tests bash sweep6.sh fmul_dd fadd_dd fsub_dd fdiv_dd \
     fmul_eb fadd_eb fsub_eb fdiv_eb fmul_ex_s fadd_ex_s fsub_ex_s fdiv_ex_s ; \
  W=16 FPSCR=01000000 TAG=fz1_12tests bash sweep6.sh <same 12> ; \
  W=16 FPSCR=02000000 TAG=dn1_set bash sweep6.sh fnan_ex fnan_add_ex fmul_dd fadd_dd fdiv_dd fmul_eb ; \
  W=16 FPSCR=00000000 bash sweep6.sh fmul_dd_plain fmul_p_dd fadd_p_dd fdiv_p_dd \
     fmul_sb fadd_sb fsub_sb fdiv_sb fmul_s fadd_s fsub_s fdiv_s f2d i2d ui2d ; \
  W=16 FPSCR=01000000 bash sweep6.sh f2d ; \
  W=16 FPSCR=00400000 bash sweep6.sh fadd_s fmul_s fadd_ex_s fadd_eb ; \
  W=16 FPSCR=00800000 bash sweep6.sh fadd_s fadd_ex_s fadd_eb ; \
  W=16 FPSCR=00c00000 bash sweep6.sh fadd_s fadd_ex_s"
```

`W` must be a power of two — the driver's worker mask requires it — so 16 workers, not 20.
Per-run output is kept as `vfp-test/results_<TAG>.txt`; `results_fz0_12tests.txt` and
`results_fz1_12tests.txt` are the 8,858,370,048-comparison pair quoted in §5.2, and
`results_dn1_set.txt` is the `DN=1` result. The block above lists the **test lists and FPSCR per
invocation**; the runs were issued as consecutive `bash sweep6.sh <list>` calls inside chained
`wsl -d Ubuntu -- bash -lc` sessions (the test names, not the FPSCR, select each measurement), so
attribute a number to the `FPSCR=`/`TAG=` line that precedes its test list. Raw worker output for
every run is left in `vfp-test/s6_<test>_<worker>.txt`.

**Harness changes made (all inside the scratch copy)** — `vfp.S`: added `vfp_set_fpscr` /
`vfp_get_fpscr`; `test.c`: added the optional 5th argument, `test_denorm_band`,
`test_smallest_normal`, `test_f2d_exhaustive`, the FZ/DN sensitivity printout in `nominal`, and
dispatch entries for `*_dd`, `*_dd_plain`, `*_p_dd`, `*_sb`, `f2d`, `i2d`, `ui2d`, `*_p`.
`build2.sh` and `sweep6.sh` are the corrected build/sweep drivers.

---

## 8. Scope, provenance and one disclosure

**What this audit does not cover.**
* **The device.** Everything here is qemu-arm (a faithful ARMv7 VFP model) plus source reading of
  the pinned Dynarmic. No measurement was taken on the Fold7, and none is needed for the
  question as posed — §1 and §2 are properties of the bytes and of the source. A device run would
  add nothing to the `FZ`/`DN` answer, because the answer is "the guest never writes the bits".
* **`libStormGLOFT.so` and other VFP-native modules.** They execute real VFP already; the audit
  only established that they never touch FPSCR (0 writes, 84–86 reads). How their own FP
  behaviour interacts with `FZ` is a separate question with the same answer (the bit is never set).
* **The `__fe_getround` `RMode` mapping.** The divergence at `RMode != 0` is measured (§5.4b);
  *why* the shipped bodies do not follow the mode was not chased, because it cannot matter (the
  mode cannot be changed).
* **Exhaustive-in-both-operands for binary helpers** is 2^64 and was not attempted; the denormal
  band is exhaustive in `a` and structured in `b`, and the exponent-band test is exhaustive in
  `a` over all 2^25 zero/denormal/inf/NaN patterns. That is the tractable strongest statement.

**Disclosure — one file outside the allowed scratch area was touched, unintentionally.** While
setting up the scratch copy I ran a `build.sh` that had the *original* harness directory
hard-coded, so it rebuilt `C:\Users\NacWorkstation\Documents\DH2Work-scratch4\vfp-test\vfptest`
and `vfp.o` from the **unmodified** `test.c`/`vfp.S` in that directory. Consequences:

* `DH2Work-scratch4\vfp-test\test.c` and `vfp.S` are **unmodified** — SHA-256
  `EF4D11EDC9BAC30856CDFBD6AA9E5663344F42304E7812CD9EE10377B14E1647` and
  `CACCEC854AAB4839D1BDAF224BABDCD33F951A28E27E11AFF6229623C8753F1A`, their pre-existing
  content; the extended copies live only under `DH2Work-toolchain\fpscr\vfp-test\`.
* `vfptest` was overwritten: 493,584 → 493,692 bytes, i.e. it now equals the size of the
  pre-existing `vfptest4`. It is recoverable at any time by rebuilding from those same unchanged
  sources with the same `build.sh` and the same `arm-linux-gnueabihf-gcc` 15.2.0.
* `vfp.o` is 3,164 bytes, its previous size.
* No other file anywhere was modified. All new work is under
  `DH2Work-toolchain\fpscr\` (scripts, the `vfp-test\` copy, `writes*.txt`, `validate*.txt`,
  `scan_callers.txt`, and the sweep result files) plus this document.

**Artefacts.** `DH2Work-toolchain\fpscr\{fpscr_validate.py, fpscr_writes.py, fpscr_callers.py,
fpscr_gaps.py, fpscr_scan.py, fpscr_totals.py, dump.py}`; `fpscr\vfp-test\{test.c, vfp.S,
build2.sh, sweep6.sh, blob.bin, blob_patched.bin, libc.patched.so, vfptest}` and its
`results_*.txt` / `s6_*.txt` outputs.
