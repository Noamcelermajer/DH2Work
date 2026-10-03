# Replacing the guest `__aeabi_*` soft-float bodies with VFP — evaluation

**Verdict in one line:** the route is legitimate, isolated, and mechanically sound; a
**tested, bit-exact** VFP replacement exists and removes **87–91 % of every arithmetic
helper's translated instructions**; the realistic frame win is **≈ 0.8–3 ms/frame**
(parametric table in §5), with an unphysical static ceiling of 21.3 ms; the biggest risk is
**not** the arithmetic but two facts nobody has measured yet — whether FPSCR.FZ/DN can ever
be non-zero in this guest, and what fraction of the frame's translated instructions the
helper bodies actually are.

Everything marked **[measured]** below was produced in this session by commands given in §9.
Everything marked **[inference]** is reasoning, not measurement. Everything marked
**UNKNOWN** is a hole with the check that closes it.

---

## 1. Feasibility: where the guest libc comes from, and is replacing it isolated?

### 1.1 The file

| fact | value | source |
|---|---|---|
| path | `assets/zb/sysroot/system/lib/libc.so` | directory listing of `…/ZettaBridge/build/launcher/assets/zb/sysroot/system/lib/` |
| size | 992,808 B | `Get-Item` |
| SHA-256 | `23ee728839ebb17bae3e9fc73034142be2466a6d694c0404ef5aeafdb1009069` | `Get-FileHash` — matches `SOFTFLOAT-CENSUS.md:14` |
| in the shipped zip | `assets/zb/sysroot/system/lib/libc.so`, CRC32 `0x7d277daa`, 22 entries total | `zipfile` on `DH2Work-stage\compatibility\work\fold7-build\runtime-bundle.zip` |

ELF facts **[measured]** (`tools` script, §9.1): `ELF32`, `EM_ARM`, `ET_DYN`, entry `0`,
`DT_SONAME = "libc.so"`, `DT_NEEDED = ld-android.so, libdl.so`, `DT_HASH` **and**
`DT_GNU_HASH` both present, 16 `verdef` entries (`libc.so`, `LIBC`, `LIBC_N`…`LIBC_36`,
`LIBC_37`, `LIBC_PRIVATE`, `LIBC_DEPRECATED`, `LIBC_PLATFORM`), 1,753 `dynsym` entries
(1,698 defined `STT_FUNC`, 14 undefined), `.symtab` **and** `.debug_frame` still present
(the file is not stripped).

### 1.2 It is a full standalone bionic, not a stub

The 14 undefined symbols are exactly the normal bionic set:

```
__loader_add_thread_local_dtor, __loader_android_get_exported_namespace,
__loader_remove_thread_local_dtor, __loader_shared_globals, __scudo_default_options,
android_dlopen_ext, android_get_application_target_sdk_version, dl_unwind_find_exidx,
dladdr, dlclose, dlerror, dlopen, dlsym
```

`__loader_*` come from the dynamic linker binary itself, `dl*` from `libdl.so`, both named in
`DT_NEEDED`. Nothing here is specific to the `__aeabi_*` helpers.

### 1.3 How ZettaBridge finds it

- The guest path `/system/` is mapped onto the sysroot's `system/` by
  [`process.cpp:95-105`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/core/src/process.cpp#L95-L105)
  (`kPathMappings`), and the sysroot prefix is applied for file opens at
  [`process.cpp:275-290`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/core/src/process.cpp#L275-L290).
- The runtime layout is `files/zb/{sysroot,guest/zbhost,guest/lib}` and the only files whose
  *presence* is validated are `sysroot/system/bin/linker`, `guest/zbhost`,
  `guest/lib/libzbcompat.so`, `guest/lib/libzbjni.so`
  ([`proxy_runtime.cpp:117-130`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/core/src/jni/proxy_runtime.cpp#L117-L130)).
  **`libc.so` is not on that required list.**
- Guest `LD_LIBRARY_PATH` is `guest/lib:<plugin>/lib`, and the linker loads `libc.so` from
  the sysroot's `system/lib` because the engine's `DT_NEEDED[0]` is `libc.so`
  ([`proxy_runtime.cpp:132-141`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/core/src/jni/proxy_runtime.cpp#L132-L141);
  engine `DT_NEEDED` = `libc.so, libGLESv2.so, libstdc++.so, libm.so, libGLESv1_CM.so, libdl.so, liblog.so` **[measured]**).
- There is **no `ld.config`/`ld.config.txt`** in the bundle — the sysroot contains only
  `system/bin/linker` and `system/lib/*`.

**Conclusion:** the guest libc is a plain content file inside the app's own runtime bundle,
loaded by name, and nothing in ZettaBridge pins, hashes or validates it. Replacing it is a
content change, not a wrapper change. **No ZettaBridge or Dynarmic source is touched, and
none needs to be.**

### 1.4 …but "rebuilt libc.so" in the literal sense is not practical

`.comment` **[measured]**:

```
rustc version 1.96.0-dev (ac68faa20 2026-05-25) (Android Rust Toolchain version 15669187-linux-x86)
Android (15682573, +pgo, +bolt, +lto, +mlgo, based on r596125) clang version 22.0.2
Linker: LLD 22.0.2
```

A from-scratch rebuild of *this* bionic needs clang 22 + LLD 22 + an Android Rust 1.96
toolchain + the exact PGO/BOLT profile. Reproducing it is a project, not a patch, and the
result would still have to reproduce the symbol versioning, `DT_HASH`/`DT_GNU_HASH` layout and
`.ARM.exidx` — i.e. exactly the things that make the byte patch safe (§2).

**Therefore the recommended shape is not "rebuild", it is "surgical in-place replacement of
the helper bodies inside the existing file".** This is the same shape as the already-reviewed
precedent in `DH2Work\patches\engine\` (a 20-byte ARM patch with an `.S`/`.ld` and a
hash-pinned patcher). The result is a 992,808-byte file that differs in **5,564 bytes
(0.56 %)** **[measured]**, all inside the 28 symbol bodies being replaced, with **zero bytes
changed anywhere else** (`make_patched_libc.py` asserts this).

---

## 2. Is it safe to change only the helper bodies?

### 2.1 Names matter, addresses matter too — and both are preserved

- **Callers resolve by name.** The engine's 351 PLT veneers resolve through the engine's own
  `.rel.plt` (`SOFTFLOAT-CENSUS.md:20`); the engine imports 40 `__aeabi_*` names
  **[measured]** from its `.dynsym`. Only **`libc.so`** defines them in the whole bundle
  **[measured]** — `libm.so`, `libstdc++.so`, `libc++.so` define none — so there is no
  precedence ambiguity.
- **But there is a real address-pinned consumer *inside* `libc.so`.** `.rel.dyn` contains a
  contiguous run of **102 `R_ARM_ABS32` (type 2) relocations in `.data` at file offsets
  `0xaa8a0 … 0xaaa3c`** whose targets are `__sF`, `__sglue` and then the entire
  compiler-rt/`__aeabi_*` builtins set in near-alphabetical order **[measured]**. No
  instruction in `.text`/`.plt` references the array by immediate; **UNKNOWN — needs** a
  literal-pool + read-watchpoint study to identify its consumer. Whatever it is, it is a
  table of *code pointers*, so **helper addresses must not move**. The in-place patch
  preserves every `st_value` exactly, so the table stays valid. A relink-and-relayout would
  have to be re-verified against it.
- **No runtime FP dispatch exists.** `dynsym` contains **zero `STT_GNU_IFUNC`** symbols
  **[measured]**, so the exported address of `__aeabi_fmul` *is* the soft-float body. The
  premise of the whole exercise (the helpers are really executed) survives this check.

### 2.2 ELF metadata is untouched

`.dynsym`, `.gnu.hash`, `.hash`, `.gnu.version`, `.gnu.version_d/r`, `.rel.dyn`, `.rel.plt`,
`.ARM.exidx` and all section/segment headers are byte-identical — the patch changes only code
bytes inside existing `STT_FUNC` ranges. Symbol lookup, version matching and hash tables cannot
notice.

### 2.3 `.ARM.exidx`

`.ARM.exidx` has 1,800 entries (`0x16714`, `0x3840` B). Exactly **one** entry's PREL31 target
falls in the whole FP helper cluster: the entry for `0x9eb18` (`__eqsf2`/`__ltsf2`/…), at
`.ARM.exidx` offset `0x19e8c`, `w0 = 0x84c8c` **[measured]**. `0x9eb18` is also inside the
patched region only for `__aeabi_fcmpun` (which sits at `0x9eb78`, a different entry point) —
in the recommended patch list `0x9eb18` is **not** modified, so that entry stays exactly valid.
Even where an entry's *length* word becomes stale (a body shrinking inside its own
`st_size`), the start address is unchanged, the replacement never throws, and the unwinder's
only use of the entry is to find the FDE for an exception that cannot occur. Low risk; the
verification plan in §6 includes an explicit check.

### 2.4 The one thing that *will* silently defeat the patch

[`RuntimeBundle.java:19-31`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/android/launcher/app/src/main/java/com/zettabridge/launcher/RuntimeBundle.java#L19-L31)
compares `assets/zb-version.txt` (currently the single line
`9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06`) against a `.bundle-version`
marker in `files/zb/`, and **returns early — skipping extraction entirely — if they match**.
Files are then installed read-only ([`RuntimeBundle.java:50`](C:/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/android/launcher/app/src/main/java/com/zettabridge/launcher/RuntimeBundle.java#L50)).

So: **the patch must also change `assets/zb-version.txt`**, or upgraded installs keep the old
libc while fresh installs get the new one. `zb-version.txt` is an opaque token, not a hash of
the bundle — nothing verifies the bundle's contents, so changing it is safe and is *required*.

---

## 3. The arithmetic question: is VFP bit-exact against the existing soft-float?

### 3.1 What the existing bodies actually are

Disassembly **[measured]** shows the helpers are **LLVM compiler-rt's soft-float builtins**,
not generic integer soft-float:

- `__aeabi_fadd` @ `0x9f1e8` (684 B, 171 insns) **is** `__addsf3` (same address);
  `__aeabi_fmul` @ `0xa06f4` (496 B, 124) is `__mulsf3`; `__aeabi_fdiv` @ `0x9faac`
  (596 B, 149) is `__divsf3`; `__aeabi_dadd` @ `0x9ed98` (1056 B, 264) is `__adddf3`;
  `__aeabi_dmul` @ `0xa03a0` (784 B, 196) is `__muldf3`; `__aeabi_ddiv` @ `0x9f638`
  (1048 B, 262) is `__divdf3`.
- `__eqsf2`/`__nesf2`/`__ltsf2`/`__lesf2`/`__cmpsf2` are **five aliases of one 48-byte body at
  `0x9eb18`** (12 insns); `__gesf2`/`__gtsf2` alias `0x9eb48`. This is the exact shape of
  compiler-rt `comparesf2.c`.
- `__aeabi_fcmp*` are 8-instruction wrappers that `bl` through `libc`'s own `.plt` into those
  bodies and turn the three-way result into 0/1.
- **`__aeabi_fsub` and `__aeabi_dsub` are 8-byte tail-calls** (`eor r1,r1,#0x80000000;
  b <plt __addsf3>` / `… __adddf3>`) — *not* independent implementations. This has a direct
  design consequence (§3.5).

Two consequences worth stating plainly:

1. **`SOFTFLOAT-CENSUS.md:106` records `__aeabi_fsub` weight = 16 and `:113`
   `__aeabi_dsub` weight = 10.** Both are wrong: measured closures are **173** and **266**
   (2-byte body + the 171/264-instruction add path). The census under-weights float
   subtraction by ~10×. The compare helpers disagree the other way (census 37 for
   `__aeabi_fcmpeq`, 122 for `__aeabi_fcmple`, 172 for `__aeabi_cfcmple`; recomputed 20 for
   both `fcmpeq` and `fcmple`) — the cause was not chased, but neither the census's nor my
   number includes the 3-instruction PLT stub that the wrapper actually enters, so a real
   compare call costs 3 instructions more than either model says. Recomputing every closure
   from the real file raises the binary-wide static total from the census's 2,730,672 to
   **3,142,934 weighted units** **[measured, §9.4]**, and the "all FP sites once per frame at
   CAL=8" ceiling from 21.85 ms to **25.14 ms — i.e. above the entire 25.7 ms frame**. That
   alone shows the CAL=8 ceiling cannot be read as a frame prediction (§5).
2. **The shipped libc already contains the exact lowering proposed here.** A hidden local
   symbol `__aeabi_cfcmpeq_check_nan` at `0xa0f74` is literally
   `vmov s2,r0; mov r0,#0; vmov s0,r1; vcmp.f32 s2,s0; vmrs apsr_nzcv,fpscr; movwvs r0,#1;
   bx lr` **[measured]**. The library was compiled for a VFP-capable target; VFP is not alien
   to it.

### 3.2 Can it be tested at all? (The harness stated in the brief cannot.)

**Unicorn 2.1.4 as installed here cannot execute a single ARM VFP instruction.** I probed 28
distinct VFP encodings (`vmov`, `vadd/vsub/vmul/vdiv.f32/.f64`, `vcmp`, `vmrs`, `vldr/vstr`,
`vcvt`, `vneg`, `vabs`, `vsqrt`, `vpush`, `vdup`) against **all 33 ARM CPU models** exposed by
the build, including Cortex-A15/A9/A8/A7, after writing CPACR and running the
`mrc/mcr CPACR + vmsr FPEXC` enable sequence. **All 33 models reject all of them with
`UC_ERR_INSN_INVALID`** (the enable sequence itself dies with `UC_ERR_EXCEPTION`)
**[measured, §9.2]**. This matches the upstream issues
[#571](https://github.com/unicorn-engine/unicorn/issues/571),
[#704](https://github.com/unicorn-engine/unicorn/issues/704),
[#1080](https://github.com/unicorn-engine/unicorn/issues/1080). The brief's premise that
"Unicorn 2.1.4 … does support ARM VFP" is false for this build.

This also explains a latent property of the existing harness: `__aeabi_fmul`'s **normal
return path** contains `vmov s0, r0`, so the *original* helper cannot be executed under
Unicorn either — the project's oracle never noticed because it *implements* the helpers on the
host instead of executing them
([`DH2-recon/STATUS.md:321-331`](C:/Users/NacWorkstation/Documents/deepseek-harness/default-workspace/DH2-recon/STATUS.md#L321-L331)).
That is a workable workaround for the oracle, but it cannot A/B two *implementations*.

**What I did instead.** WSL Ubuntu 26.04 is present, `qemu-user` and
`gcc-arm-linux-gnueabihf` install from the network, and **qemu-arm executes ARM VFP
correctly** (verified: `vmul.f32` 3.0×4.0 = `0x41400000`, `vcvt.s32.f32` 3.5 = 3) **[measured,
§9.3]**. I built a freestanding ARM32 differential harness that:

1. copies the guest libc's helper cluster **verbatim** — `[0x9eb00, 0xa5ae0)`, 28,640 bytes,
   file offsets `0x9db00..0xa4ae0` — into a mapped RWX buffer;
2. fills the GOT slots the dynamic linker would fill (23 `R_ARM_JUMP_SLOT` entries, all of
   which resolve to symbols **inside** the copied range — `__addsf3` = `__aeabi_fadd`,
   `__eqsf2` = `0x9eb18`, …) — **no code byte is altered**, only relocation data;
3. verified the region is self-contained: **0 PC-relative loads and 0 branches leave it**
   **[measured]**;
4. calls the pristine bodies and the VFP replacements on identical inputs and compares raw
   bit patterns.

### 3.3 Measured result 1 — a *plain* VFP replacement is **not** bit-exact

`vmul.f32` alone, against the shipped `__aeabi_fmul`, over the structured operand set
(every one of the 256 exponents × 2 signs × 16 mantissa samples = 8,192 values, all pairs,
67,108,864 comparisons) **[measured]**:

| | |
|---|---|
| comparisons | 67,108,864 |
| mismatches | **205** |

Every mismatch has the same shape, e.g.

```
fmul  a=0x7fc00000  b=0x7f800001   shipped=0x7fc00000   plain vmul.f32=0x7fc00001
fmul  a=0x7fc00001  b=0x7fffffff   shipped=0x7fc00001   plain vmul.f32=0x7fffffff
fmul  a=0x7fc00000  b=0xff800001   shipped=0x7fc00000   plain vmul.f32=0xffc00001
```

**The divergence is precisely the NaN-selection rule.**

- compiler-rt (shipped): *first operand wins.* If `|a| > +inf` return `a | quiet`;
  else if `|b| > +inf` return `b | quiet`.
- ARM VFP (and therefore Dynarmic): *signalling NaN wins.* A signalling NaN operand is
  quieted and propagated in preference to a quiet NaN in the other position.

They agree whenever `a` is a NaN, whenever both are quiet, and whenever both are signalling.
They **disagree exactly when `a` is a quiet NaN and `b` is a signalling NaN** — and only in
the NaN payload (the result is NaN either way; no finite value is ever wrong).

`__aeabi_fsub` adds a second, independent divergence: because `__subsf3` negates `b` *before*
applying the rule, the shipped code returns `quiet(-b)`, i.e. **with `b`'s sign flipped**.
My first exact variant got this wrong and the harness caught it:

```
fsub  a=0x00000000  b=0x7f800001   shipped=0xffc00001   naive-exact=0x7fc00001   (244,860 mismatches)
```

### 3.4 Measured result 2 — a **bit-exact** VFP replacement exists and is tested

Adding a 10-instruction integer pre-guard that reproduces compiler-rt's rule (check `a` first,
then `b`, forcing the quiet bit; for `sub`, flip `b`'s sign first) makes the replacement exact.
`vfp_fmul_ex` is 15 instructions:

```
bic   r2, r0, #0x80000000
movw  r3, #0 ; movt r3, #0x7f80       @ 0x7f800000
cmp   r2, r3
orrhi r0, r0, #0x400000               @ quiet(a)   -- measured: shipped libc does the same
bxhi  lr
bic   r2, r1, #0x80000000
cmp   r2, r3
orrhi r0, r1, #0x400000               @ quiet(b)   (sub: eorhi r0,r1,#0x80000000 first)
bxhi  lr
vmov  s0, r0 ; vmov s1, r1 ; vmul.f32 s0, s0, s1 ; vmov r0, s0 ; bx lr
```

**Measured so far (all bit-exact, 0 mismatches):**

| test | comparisons | mismatches |
|---|---:|---:|
| `fmul` exact, structured (all exponent pairs) | 67,108,864 | **0** |
| `fadd` exact, structured | 67,108,864 | **0** |
| `fsub` exact, structured (incl. the sign fix) | 67,108,864 | **0** |
| `fdiv` exact, structured | 67,108,864 | **0** |
| `fadd`/`fsub`/`fmul`/`fdiv` exact, random full-domain | 4 × 400,000,000 | **0** |
| `fadd`/`fsub`/`fmul`/`fdiv` exact, **exhaustive over every zero, denormal, infinity and NaN `a`** (2²⁵ values) × 24 boundary `b` × both operand orders | 4 × 1,610,612,736 | **0** |
| `fadd`/`fmul` exact, NaN payload space (2²⁴) × 10 `b` × both orders | 2 × 335,544,320 | **0** |
| `dadd`/`dsub`/`dmul`/`ddiv` exact, structured (all exponent pairs) | 4 × 37,748,736 | **0** |
| `dmul` exact, random full-domain | 200,000,000 | **0** |
| `__aeabi_i2f`, `__aeabi_ui2f`, exhaustive over all 2³² integers | 2 × 4,294,967,296 | **0** |
| **total (exact variants)** | **17,922,904,576 ≈ 1.79 × 10¹⁰** | **0** |

And the *non*-exact comparisons that bound the risk:

| test | comparisons | mismatches |
|---|---:|---:|
| **plain** `vmul.f32` (no NaN guard), structured | 67,108,864 | **205** |
| **plain** `vmul.f32` (no NaN guard), exhaustive over every NaN/denormal/inf/zero `a` | 1,610,612,736 | **33,554,424** |
| naive `vcvt.s32.f32` for `__aeabi_f2iz`, exhaustive | 4,294,967,296 | **33,554,429** |
| naive `vcvt.u32.f32` for `__aeabi_f2uiz`, exhaustive | 4,294,967,296 | **8,388,607** |

**Artifact-level re-run (`*_p_*`).** The same comparisons with the "new" side being the function
*as it exists inside the generated `libc.patched.so`* — same GOT-fill machinery, patched byte
image, not the assembly source **[measured]**:

| artifact test | comparisons | mismatches |
|---|---:|---:|
| `__aeabi_fmul` / `fadd` / `fsub` / `fdiv`, structured | 4 × 67,108,864 | **0** |
| `__aeabi_fmul` / `fadd` / `fsub` / `fdiv`, random full-domain | 4 × 400,000,000 | **0** |
| `__aeabi_fmul` / `fadd` / `fsub` / `fdiv` over every zero/denormal/inf/NaN `a` | 4 × 1,610,612,736 | **0** |
| `__aeabi_dmul` / `dadd` / `dsub` / `ddiv`, structured | 4 × 37,748,736 | **0** |
| `__aeabi_fcmpeq` (r0 result) | 67,108,864 | **0** |
| `__aeabi_dcmpeq` (r0 result) | 37,748,736 | **0** |
| `__aeabi_d2f` (r0 result) | 37,748,736 | **0** |
| **artifact total (exact helpers)** | **≈ 8.5 × 10⁹** | **0** |

So the numbers above describe the artifact that would ship, not a model of it.
The harness also caught two of its *own* design defects while doing this — a 64-bit return
prototype applied to `__aeabi_dcmpeq`/`__aeabi_d2f`, which return in r0 only (18,868,225
**false** mismatches), and a `fcmpeq` test that compared flags instead of r0 — both fixed and
re-run green.

### 3.5 Constraints the patch list must respect

- **`__aeabi_fsub`/`__aeabi_dsub` must NOT be patched**: they are 8 bytes and the replacement
  is 64/100 bytes. They do not need to be: they are `eor` + tail-call into `__addsf3`/
  `__adddf3`, so patching the *add* body accelerates subtraction for free, and — importantly —
  it keeps the sign-flip behaviour that the add-side NaN rule then sees. (An earlier draft that
  patched `fsub` directly **failed the size check**, `64 > 8`.)
- Everything on the recommended list fits: the smallest target is `__aeabi_fcmpun` at 28 B
  (replacement 28 B) and `__aeabi_fcmpeq` at 32 B (replacement 28 B) **[measured]**.

### 3.6 `__aeabi_fcmp*`: flags, not just r0

The brief says the compare helpers "return 1/0 in r0 rather than setting condition flags".
That is true of the **five `__aeabi_fcmpXX`** helpers, which return in r0 — a VFP lowering is
`vcmp.f32; vmrs apsr_nzcv,fpscr; mov r0,#0; mov<cc> r0,#1; bx lr` (7 instructions replacing
8 + a 12-instruction `__eqsf2` body + a PLT hop).

It is **not** true of the `__aeabi_cfcmple` / `__aeabi_cfcmpeq` / `__aeabi_cdcmple` family,
which *do* set NZCV. Those are **not imported by the engine** **[measured]** (the engine's
import list contains only the 40 names in §9.6) — they are used inside `libc.so` itself. I
measured their exact flag contract on the shipped body:

| | `__aeabi_cfcmple` shipped | `vcmp.f32; vmrs apsr_nzcv,fpscr` |
|---|---|---|
| a < b | `N=0 Z=0 C=0 V=0` (0x0) | `N=1 Z=0 C=0 V=0` (0x8) |
| a == b | `0x6` | `0x6` |
| a > b | `0x2` | `0x2` |
| unordered | `0x2` | `N=0 Z=0 C=1 V=1` (0x3) |

So the natural VFP lowering is **not** flag-identical: it differs in **N** for "less" and in
**V** for "unordered". It *is* identical for the `LS`/`HI` (`C`,`Z`) conditions, which is the
contract this ABI is built on. To be exactly identical regardless, mask:

```
vcmp.f32 s0, s1 ; vmrs r2, fpscr ; and r2, r2, #0x60000000 ; msr apsr_nzcvq, r2 ; bx lr
```

which reproduces 0x0/0x6/0x2/0x2 exactly **[tested, §9.5 `cfcmple`/`cdcmple`]**. Since the
engine does not import these, the mask is cheap insurance for `libc`'s own callers.

**Who actually calls them: only two places, both in `libc.so` itself.** No module in the
runtime bundle *or* in the engine's `original/lib/armeabi-v7a/` imports
`__aeabi_cfcmple`, `__aeabi_cfcmpeq`, `__aeabi_cfrcmple`, `__aeabi_cdcmple`,
`__aeabi_cdcmpeq` or `__aeabi_cdrcmple` **[measured]**. The only callers in `libc.so`'s own
`.text` are `__aeabi_cfcmpeq` (`bne <plt __aeabi_cfcmple>` at `0xa0f24`) and
`__aeabi_cfrcmple` (`b <plt __aeabi_cfcmple>` at `0xa0f70`), and both merely forward the
flags. **Risk 6 is therefore near-zero in practice**; the masked form still removes the
question entirely. **UNKNOWN — needs** the same condition-code scan over `libc.so`'s callers
if anyone later adds a link to one of these symbols.

### 3.7 The remaining semantics the brief asked about

| property | shipped soft-float | VFP with FPSCR default (FZ=0, DN=0, RMode=0) | tested? |
|---|---|---|---|
| rounding | round-to-nearest-even, with sticky bit and a denormal-result path (`lsls r0,r0,#k; orrne r2,r2,#1` before the guard/round step) | round-to-nearest-even | **yes** — 0 mismatches across every exponent pair, both signs, boundary mantissas |
| denormals | **not** flushed; denormal inputs are normalised (`clz`; `sublo r6,r1,#8`) and denormal results are produced by a shift path | **not** flushed with FZ=0; ARMv7 VFP handles denormals in hardware | **yes** — exponent field 0 is in every structured operand set, 0 mismatches |
| ±0 | `+0 + -0 = +0` handled explicitly (`vmoveq.f32 s2,s0`) | IEEE-754 default | **yes** (0 and `0x80000000` are in every operand set and in the exhaustive-`a` `b` list) |
| infinities | IEEE | IEEE | **yes** |
| NaN quieting / payload | first operand, quiet bit forced | signalling-NaN priority, quieted | **no for plain VFP**, **yes for the guarded variant** (0 mismatches, incl. a 2³⁴-bit NaN-space sweep) |
| sNaN *trapping* | none (pure integer) | none unless FPSCR enable bits are set — they are 0 in a fresh guest | **[inference]** |
| FPSCR cumulative flags | **never set** by the soft-float bodies | `vcmp`/arithmetic set IoC/DzC/OvC/UnC/InxC | not tested; see §7 |
| FZ / DN | irrelevant to the shipped code | **would break denormal/NaN equivalence** | **UNKNOWN — needs** runtime measurement |
| conversions `f2iz`, `f2uiz`, `d2iz`, `d2uiz` | compiler-rt "out of range → sentinel" | `vcvt` saturates / returns 0 | **yes — and they DIVERGE. Do not replace them: §3.8.** |
| conversions `i2f`, `ui2f` | compiler-rt | `vcvt.f32.s32/u32` | **yes — exhaustive over all 2³², identical** |
| `__aeabi_cfcmple` flags | C=!(a<b), Z=(a==b), N=V=0 | C/Z same, N/V differ | **yes**, measured (§3.6) |

### 3.8 Measured result 3 — the integer conversions are **not** substitutable by `vcvt`

Exhaustive over all 2³² float bit patterns **[measured, `f2iz` / `f2uiz` / `i2f` / `ui2f` in
§9.5]**:

| helper | comparisons | mismatches | example |
|---|---:|---:|---|
| `__aeabi_f2iz` | 4,294,967,296 | **33,554,429** | `a = 0x4f000000` (2³¹) → shipped `0x80000000` (INT_MIN sentinel) vs `vcvt.s32.f32` `0x7fffffff` (saturate) |
| `__aeabi_f2uiz` | 4,294,967,296 | **8,388,607** | `a = 0x7f800010` (NaN) → shipped `0xffffffff` vs `vcvt.u32.f32` `0x00000000` |
| `__aeabi_i2f` | 4,294,967,296 | **0** | — |
| `__aeabi_ui2f` | 4,294,967,296 | **0** | — |

The float→int helpers implement compiler-rt's "out of range → sentinel" contract (`INT_MIN`
for `__fixsfsi`, `UINT_MAX` for `__fixunssfsi`, including for NaN), while ARM VFP `VCVT`
saturates toward the representable end and returns 0 for NaN. These are different *observable
values*, not a NaN-payload nuance.

**Confirmed on the artifact, not just the source:** with the "new" side being the
`__aeabi_f2iz`/`__aeabi_f2uiz` bodies *inside `libc.patched.so`*, the exhaustive sweeps
reproduce the identical mismatch counts — **33,554,429** and **8,388,607** respectively over
all 2³² inputs **[measured]**. This is why they are excluded from the recommended patch list,
and it is a concrete demonstration that the artifact-level tests (§6 Tier 2) are load-bearing.

**Recommendation: leave `__aeabi_f2iz`, `__aeabi_f2uiz`, `__aeabi_d2iz`, `__aeabi_d2uiz`,
`__aeabi_l2f`, `__aeabi_ul2f` at their shipped bodies in the first pass.** They are the
*cheapest* helpers anyway (21–26 instructions for the single-precision ones: `f2iz` 23,
`f2uiz` 21), so excluding them **all** costs ≈ 5 % of the weighted saving (they are 149,997 of
3,142,411 weighted units and ≈ 3,800 of 26,900 call sites), and excluding only the two
measured-divergent float→int helpers (`f2iz`, `f2uiz`) costs **0.5 %** (14,766 units). A guarded
VFP form is possible — test `|a| >= 2³¹` (float: `|a| >= 0x4f000000`) or NaN before converting
and return the sentinel — and must be verified exhaustively in the same way before it ships.
`i2f`/`ui2f` are exhaustively identical; `f2d`, `d2f`, `i2d`, `ui2d`, `d2iz`, `d2uiz` have no
measured divergence on the domains tested so far but are **not** exhaustively covered.

### 3.9 The harness found two real bugs in the replacement

Worth recording, because it is the argument for the verification plan in §6:

1. **`__subsf3` flips `b`'s sign before the NaN rule**, so an "exact" `vsub.f32` guard that
   returns `quiet(b)` is wrong for `b` = signalling NaN (244,860 mismatches in a 6.7 × 10⁷
   structured sweep). Fixed by `eor r0,r1,#0x80000000` in the guard.
2. **The double-precision guards used `r14` as a scratch register** before `bx lr`, so every
   `__aeabi_d*` replacement returned to a garbage address — a hard crash, caught as an
   immediate SIGSEGV by every double-precision test the moment it ran. Fixed by
   `push {r4,lr}` … `pop {r4,pc}`.

Neither would have been caught by reading the code, and (2) would have crashed the game on the
first double-precision operation. It was caught because the test executes the *patched ELF*,
not a model of it (§6 Tier 2).

### 3.10 **The emulated VFP semantics are Dynarmic's — and I could not read them**

The guest is ARM32, but the JIT translates VFP to the host. Whatever Dynarmic implements *is*
the semantics that will run.

- ZettaBridge's `third_party/dynarmic/` is an **empty directory** in this checkout — it is a
  submodule fetched at configure time — with two local patches on top:
  `third_party/patches/dynarmic-0001-thumb32-armv8.patch` (9,926 B) and
  `dynarmic-0002-asimd-narrowing.patch` (6,594 B) **[measured]**, added by `CMakeLists.txt:20-25`
  with `DYNARMIC_FRONTENDS = A32`.
- **UNKNOWN — needs X:** the exact Dynarmic revision that ZettaBridge builds, and from it
  Dynarmic's `A32::FPSCR` reset value and its `FZ`/`DN` handling in `VFP`/`NEON` translation.
  The check is: read the pinned submodule SHA out of `.gitmodules` + the lock/fetch step, fetch
  `src/dynarmic/backend/.../fp/` and `src/dynarmic/frontend/A32/translate/impl/vfp.cpp` at that
  revision, and confirm (a) `FPSCR` resets to `0`, (b) `FZ`/`DN` are honoured, (c) the two
  VFP-related patches do not touch floating-point behaviour. *If Dynarmic deviates from the
  ARM ARM here, the bit-exactness measured on qemu-arm does not transfer*, and only an
  on-device (or Dynarmic-hosted) differential run settles it.
- **UNKNOWN — needs X:** whether some *other* guest module sets `FPSCR.FZ` or `DN`. The engine
  has no FP instructions at all (`SOFTFLOAT-FINDING.md:8-25`), so it cannot; but
  `libStormGLOFT.so` is built for cortex-a8 with real VFP (`SOFTFLOAT-FINDING.md:29`), and
  `libm.so` is VFP-native. A standard `-O2` build does not touch FPSCR; `fesetenv`/`fesetround`
  do. The check is a tiny instrumentation: add a counter to the patched helper that records
  `FPSCR.FZ|DN` on entry and dump it after a real play session — deliberately
  *inside the thing we are already patching*, so it costs no ZettaBridge change.

---

## 4. What must be preserved, in one list

| item | must be preserved? | in-place patch | relink/rebuild |
|---|---|---|---|
| `DT_SONAME`, `DT_NEEDED` | yes | ✅ identical | must be re-checked |
| exported symbol names, versions (`LIBC`, `LIBC_N`, …) | yes | ✅ identical | re-check |
| `DT_HASH` / `DT_GNU_HASH` layout | yes | ✅ identical | re-generated |
| helper `st_value` (address) | **yes** — a 102-entry `R_ARM_ABS32` table in `.data` points at them, plus `__subsf3`/`__mulsf3`/… share addresses with the `__aeabi_*` names | ✅ identical by construction | must be re-verified |
| helper `st_size` | no, but keeping it costs nothing | ✅ identical | changes |
| `.ARM.exidx` | only the function *start* address | ✅ start unchanged | re-generated |
| `.plt` GOT slots for `__eqsf2`, `__addsf3`, … | yes | ✅ untouched (the compare wrappers bypass them once patched) | re-generated |
| `assets/zb-version.txt` | **yes — must be *changed*** | required | required |

---

## 5. Expected win

### 5.1 Per-site instruction counts (measured)

| helper | shipped insns | closure actually entered | replacement insns | reduction |
|---|---:|---:|---:|---:|
| `__aeabi_fmul` | 124 | 124 | 15 | **87.9 %** |
| `__aeabi_fadd` | 171 | 171 | 15 | **91.2 %** |
| `__aeabi_fsub` | 2 | 2 + 171 = **173** | 2 + 15 = 17 | **90.2 %** |
| `__aeabi_fdiv` | 149 | 149 | 15 | **89.9 %** |
| `__aeabi_dmul` | 196 | 196 | 25 | **87.2 %** |
| `__aeabi_dadd` | 264 | 264 | 25 | **90.5 %** |
| `__aeabi_dsub` | 2 | 2 + 264 = **266** | 2 + 25 = 27 | **89.8 %** |
| `__aeabi_ddiv` | 262 | 262 | 25 | **90.5 %** |
| `__aeabi_fcmpeq/lt/le/ge/gt` | 8 (incl. the `bl`) | 8 + 3 (PLT stub) + 12 (`__eqsf2`) = **23** | 7 | **69.6 %** |
| `__aeabi_fcmpun` | 7 | 7 | 7 | 0 % (already minimal; not engine-imported) |
| `__aeabi_f2iz / f2uiz` — **excluded, §3.8** | 23 / 21 | same | (4, but not bit-exact) | do not replace |
| `__aeabi_i2f / ui2f` | 33 / 31 | same | 4 | **≈88 %** (exhaustively identical) |
| `__aeabi_f2d / d2f / i2d / ui2d / d2iz / d2uiz` — not yet exhaustively verified | 41 / 97 / 25 / 21 / 26 / 24 | same | 4 | **≈83–96 %** if verified |

Aggregated over the whole engine with the recomputed closure weights (§9.4), weighting each
symbol by its direct call-site count from `SOFTFLOAT-CENSUS.md:433-461`:

| | weighted units | ms if every site ran once/frame (CAL=8, per `SOFTFLOAT-CENSUS.md:608`) |
|---|---:|---:|
| replaced symbols, today | 2,992,374 | 23.94 |
| replaced symbols, patched | 332,684 | 2.66 |
| **removed** | **2,659,690** | **21.28** |
| **reduction** | **88.9 %** | |
| not-replaced remainder (the conversions of §3.8, compares that are already minimal, libm) | 150,560 | 1.20 |
| **corrected binary-wide total, patched** | **483,244** | **3.87** (from 3,142,934 / 25.14) |

The replaced set is: the six real arithmetic bodies (`fadd`, `fmul`, `fdiv`, `dadd`, `dmul`,
`ddiv` — 2,877,782 units, **96.2 %** of the saving, because `fsub`/`dsub` inherit the add
bodies), plus the five `fcmpeq/lt/le/ge/gt` (62,200 units) and the five double compares
(13,764 units). `fsub` and `dsub` appear in the arithmetic figures via their closure.

### 5.2 The honest ceiling

**The 21.28 ms figure is not a frame prediction and must not be repeated as one.** It assumes
every one of the 27,088 FP call sites in the binary executes exactly once per frame at 8 host
cycles per translated instruction. The recomputed un-patched ceiling (25.14 ms) *exceeds the
entire 25.7 ms frame*, which proves the CAL=8 model cannot be read as a frame time. It is a
**static share**, useful only as a ratio.

Three defensible readings, in increasing optimism:

| reading | basis | expected ms/frame |
|---|---|---:|
| **conservative** | only the census's `on-frame` class (110,104 units, `SOFTFLOAT-CENSUS.md:792`), × 88.9 % | **0.78** |
| **central** | sum of the census's top-15 *expected* ms/frame (2.25 ms, `SOFTFLOAT-CENSUS.md:796`) with the 88.9 % reduction applied to the replaced share | **≈ 2.0** |
| **parametric** | frame saving = `p × 21.25 ms × 0.889`, where `p` = dynamic share of the 21.25 ms translated guest slice spent inside helper bodies | below |

| `p` | 10 % | 20 % | **30 %** | 40 % | 50 % |
|---|---:|---:|---:|---:|---:|
| ms/frame saved | 1.9 | 3.8 | **5.7** | 7.6 | 9.4 |

`p` is **UNKNOWN — needs X**: the dynamic per-frame entry count of each helper. It is the
single measurement that would convert this whole note from a static ratio into a frame number.
The cheap way to get it is *inside the thing we are already replacing*: build a
counting variant of the patched `libc.so` (increment a `.bss` word at each helper entry), run
one frame of the game, and dump the words over the existing debug channel. No ZettaBridge patch
is involved. Note the census found **no 80/20** — the top function is 1.3 % of the static
traffic (`36,538 / 2,730,672`, `SOFTFLOAT-CENSUS.md:138`), consistent with the brief — so `p`
cannot be inferred from a handful of hot functions; it needs the whole-binary count.

### 5.3 What this route *cannot* remove

Per FP call site, the following guest instructions **remain** after the patch, because they
live in the engine (which this route does not touch):

```
engine:  bl <veneer>                        1 instruction
         add ip,pc,#0x600000                 \
         add ip,ip,#0x86000                   > the engine's PLT veneer, 3 instructions
         ldr pc,[ip,#0xd14]!                 /
guest :  <replacement body>                 15-25 instructions
         bx lr / pop {pc}                   1-2 instructions
```

The engine's `.plt` is `0x30dd74`, 4,232 B = a 20-byte header + **351 veneers of exactly 3
instructions / 12 bytes each** **[measured]**. So the irreducible per-site overhead after the
patch is ≈ 5–6 translated instructions that were always there. Per-site cost therefore goes
from ≈ 130–180 to ≈ 21–31 instructions: an **≈ 80–84 % reduction of the whole per-site cost**,
slightly less than the 88 % body-only ratio. This is exactly the caveat
`SOFTFLOAT-FINDING.md:110-113` already flagged ("the call crossing still happens per
operation") — the difference is that the *body* is now VFP-native rather than 124–264
integer instructions, so the caveat is no longer fatal.

Against the project's 60 FPS goal: the census's own budget table
(`SOFTFLOAT-FINDING.md:81-87`) says ~50 % of the guest slice is needed to clear 60 FPS. This
route removes ≈ 0.89 of whatever share the helper bodies hold, so it clears 60 FPS only if
`p ≈ 56 %`. At the conservative/central readings it is a **1–3 ms improvement, not a fix** —
useful, measurable, and cheap, but not sufficient on its own.

---

## 6. Concrete verification plan

The existing differential harness
([`DH2-recon/STATUS.md:0`](C:/Users/NacWorkstation/Documents/deepseek-harness/default-workspace/DH2-recon/STATUS.md),
`tools/dh2_oracle.py`, `tools/difftest.py`, 27,250 + 34,692 comparisons at 0 mismatches) is the
right *discipline* but the wrong *tool* for this: its oracle implements the helpers on the host
(§3.2), so it can neither execute the shipped bodies nor execute VFP. The plan below keeps the
project's methodology ("no rewrite counts until it has been differentially verified
bit-exactly") and replaces only the instrument.

**Tier 0 — build the reference (done, §9.3).** Copy `[0x9eb00, 0xa5ae0)` of the guest
`libc.so` **verbatim** into an RWX buffer and fill only the 23 GOT slots. Assert: 0 branches
and 0 PC-relative loads leave the region; the patched file differs from the original in 0
bytes outside the target symbol ranges.

**Tier 1 — exhaustive where exhaustive is possible.** For unary helpers the domain is 2³²:

| helper | domain | feasible |
|---|---|---|
| `__aeabi_f2iz`, `__aeabi_f2uiz` | all 2³² float bit patterns | yes — **done, and they diverge (§3.8)** |
| `__aeabi_i2f`, `__aeabi_ui2f` | all 2³² int bit patterns | yes — **done, 0 mismatches** |
| `__aeabi_i2d`, `__aeabi_ui2d` | all 2³² | yes — not yet run |
| `__aeabi_f2d` | all 2³² | yes — not yet run |
| `__aeabi_d2f`, `__aeabi_d2iz`, `__aeabi_d2uiz` | 2⁶⁴ | no — structured + random |
| `__aeabi_fcmpun`, `__aeabi_dcmpun` | 2⁶⁴ | no — structured + NaN-space sweep |

For binary ops 2⁶⁴ is out of reach; the **exhaustive-in-one-operand** form
(`all 2³² values of a × 24 boundary values of b`) is the strongest tractable statement:

| helper | coverage | comparisons |
|---|---|---|
| `fadd`, `fmul` | exhaustive `a`, 24 boundary `b` (0, ±0, ±min-denormal, ±max-denormal, ±min-normal, ±1, ±0.5, ±2, ±max-finite, ±inf, ±qNaN, ±sNaN, ±2^-9 boundaries, 2²³, 2²⁴−1) | 2³² × 24 × 2 = 2.06 × 10¹¹ |
| `fsub`, `fdiv` | as above | as above |
| `dadd`, `dmul` | exhaustive *half* of `a` (2³²) × 16 boundary `b` | 6.9 × 10¹⁰ |
| any of the above | 10⁸–10⁹ full-domain random pairs | — |

**Domain completeness is the argument, not the count.** The structured set
`{both signs} × {all 256 exponents} × {0, 1, 2, 0x3fffff, 0x400000, 0x400001, 0x7ffffe,
0x7fffff, + random} ` crosses **every exponent pair** — including denormal×denormal,
denormal×normal, overflow and NaN — and every rounding-boundary mantissa. That is where a
VFP/soft-float divergence can hide. The remaining "arbitrary mantissa × arbitrary mantissa"
space is plain IEEE round-to-nearest on both sides and is covered by the random sweeps.

**Tier 2 — the artifact, not the source.** Every test is repeated with the "new" side being the
function *as it exists in the patched `libc.so`* (`*_p_*` tests, §9.5), loaded through the same
GOT-fill machinery. This catches patch-offset errors, size overruns and stale blobs — the class
of bug that produced a real failure in this session (a wrong `vaddr − 0x1000` file offset
silently shifted the whole copied region).

**Tier 3 — the whole-library metadata check (done).** `tier3_check.py` compares the patched
file against the original field by field **[measured]**:

```
size 992808 / 992808 same
elf header equal: True
segments equal  : True
sections (.dynsym .dynstr .gnu.hash .hash .gnu.version .gnu.version_d .gnu.version_r
          .rel.dyn .rel.plt .ARM.exidx .rodata .ARM.extab .dynamic .got .got.plt .data
          .init_array .fini_array .ARM.attributes .symtab .strtab .shstrtab .debug_frame)
          byte-identical: ALL
dynsym entry-for-entry equal (name/value/size/type/bind/shndx/other): True   (0 differing)
rel.plt equal: True
dynamic equal: True
changed bytes: 5564 of 992808 = 0.560 %   in exactly 4 x 4 KiB pages (file 0x9d000..0xa1000)
```

`.text` is the only section that differs, and only inside the 28 target symbol bodies. The
remaining Tier-3 item is a runtime `dlopen`/`dlsym` smoke test of the patched file.

**Tier 4 — the two measurements that decide the risk (§7).**
1. Instrument the patched helper entries to count, per frame: (a) invocations, (b) `FPSCR.FZ|DN`
   on entry, (c) how many operands are signalling NaNs. That turns `p`, the FZ/DN risk and the
   sNaN risk from unknowns into numbers, at zero ZettaBridge-patch cost.
2. Run the same differential suite **hosted on Dynarmic** (built from ZettaBridge's pinned
   revision, unpatched, as a library) rather than qemu-arm. Same harness, same operand sets;
   this is the only run that tests the semantics that will actually execute on device.

**Tier 5 — on device.** Existing per-frame timing methodology (`PERFORMANCE-BASELINE.md`), plus
a screenshot/log diff because NaN payload changes are invisible to timing.

---

## 7. Risk register

| # | risk | measured? | severity | mitigation |
|---|---|---|---|---|
| 1 | **FPSCR.FZ or DN is non-zero** somewhere in the guest, so the VFP helpers flush denormals / return the default NaN where the soft-float code does not | **no** | **high if it happens** — silently changes numerics in the physics/collision code that the census ranks highest | Tier-4 instrumentation; or make the guarded helpers also clear FZ/DN in FPSCR on entry (+4 instructions). Do this before shipping. |
| 2 | **Dynarmic's VFP differs from qemu-arm's** (NaN priority, FZ default, FPSCR reset value) | **no** | high — invalidates §3.4 | Tier-4.2 (hosted on the pinned Dynarmic). Until then, §3.4 is "bit-exact on a faithful ARMv7 VFP model", not "bit-exact under Dynarmic". |
| 3 | **Signalling NaNs appear at runtime**; with the *plain* VFP form the payload/priority differs, with the guarded form it does not | partially (the divergence is characterised exactly) | low with the guard, medium without | ship the guarded form (15 insns, still 88 % cheaper); count sNaNs in Tier 4.1 |
| 4 | `.data` **ABS32 table** at `0xaa8a0` (102 entries) whose consumer is unidentified | no | low — addresses are preserved | Tier-3 + a read watchpoint on the table under qemu/Unicorn |
| 5 | **`zb-version.txt` not bumped** → patch silently absent on upgraded installs | no | medium (a whole release of "no effect") | bump the version token; add a boot-time log of the libc SHA |
| 6 | `__aeabi_cfcmple` **N/V flag** divergence | yes, characterised | low (engine does not import it) | use the masked form; run the condition-code scan over all call sites |
| 7 | **FPSCR cumulative exception flags** now become set where they never were | no | low — nothing in the engine reads FPSCR; only `fetestexcept` would notice, and the engine is soft-float | accept; note in the release log |
| 8 | A future ZettaBridge update replaces the bundled libc | — | medium — the patch disappears | the byte patch must be a reproducible, hash-pinned build step (the `DH2Work\patches\engine\` pattern) applied to the bundle, not a hand edit |
| 9 | **The integer conversions are not substitutable** (`f2iz` out-of-range → INT_MIN vs saturation; `f2uiz` NaN → UINT_MAX vs 0) | **yes, exhaustively** | eliminated by excluding them | keep `f2iz`/`f2uiz`/`d2iz`/`d2uiz`/`l2f`/`ul2f` as shipped; costs ≈ 5 % of the saving. A guarded form needs the same exhaustive verification. |
| 10 | Two defects were already found *in the replacement itself* (§3.9): the `sub` NaN sign and an `lr` clobber that would have crashed every double-precision call | yes, by the harness | n/a — fixed | this is the argument for §6 Tier 2 (test the patched ELF, not the source) |

---

## 8. Alternatives, compared

**(a) Leave libc alone; patch the engine's call sites to inline VFP.** *Do not do this first.*
Pros: removes the call, the engine veneer and the guest PLT hop as well — so it wins the
remaining ≈ 5–6 instructions/site that route (this one) cannot, and it is not limited to
helpers. Cons: 28,209 sites in a 15.9 MB binary, each needing a correct in-place substitute
whose register pressure, flags and literal-pool slots must be reconstructed — that is a
disassembly-and-relocation problem at a scale where a single mistake is a silent miscompute;
and it is irreversible in a way a helper-body swap is not. **The right order is: do (this
route) first, because it is 28 patches instead of 28,209 and can be verified exhaustively, and
then apply call-site inlining only to the handful of functions the census ranks highest** —
where it is a targeted optimisation with a small review surface.

**(b) Provide a different libc entirely (`-mfpu=neon-vfpv4`, or any rebuild).** *Strictly worse
than (this route).* A rebuild forfeits every guarantee in §4 for no arithmetic gain: the
helpers can only be as fast as `vmul.f32` + a NaN guard, which is what (this route) already
is. It also has to reproduce clang 22 / LLD 22 / Rust 1.96 / PGO+BOLT (§1.4), the symbol
versioning, the hash tables, the `.ARM.exidx` and the `.data` pointer table. `-mfpu=neon-vfpv4`
would additionally introduce NEON state (`Q` registers, `FPSCR.FZ16`) that the guest currently
never touches. Note the shipped libc is *already* VFP-capable — the target FPU is not the
constraint; the constraint is that 82 % of its exported FP surface is compiler-rt soft-float
code.

**(c) Do nothing.** The honest baseline. Worth stating what it costs: a single `a*b` in the
engine is currently ~130–180 translated guest instructions, of which 124 are to make one
`vmul.f32` happen. Given 28,209 call sites and a 21.25 ms guest slice, the *existence* of the
inefficiency is certain even though its *size* is not (§5.2). "Do nothing" is defensible only
if `p` turns out to be small — which is exactly the measurement in Tier 4.1, and it is cheap
enough that the answer should be bought before deciding.

---

## 9. Method — every command

### 9.1 ELF / symbol / relocation facts

`python` 3.13.15 with pyelftools 0.33, capstone 5.0.7 (scratch scripts under
`DH2Work-scratch4\`): `inspect_libc.py`, `dyn_info2.py`, `dis_helper.py`, `scan_helpers.py`,
`region_scan.py`, `detail_scan.py`, `pin_check.py`, `table_probe.py`, `providers.py`,
`win_model.py`.

```powershell
python DH2Work-scratch4\inspect_libc.py      # segments, sections, dynamic, undefined set
python DH2Work-scratch4\dyn_info2.py         # SONAME / NEEDED / verdef / ARM attributes / .comment
python DH2Work-scratch4\scan_helpers.py      # per-helper insns + external branches
python DH2Work-scratch4\region_scan.py       # proves every needed PLT stub resolves inside the copy range
python DH2Work-scratch4\providers.py         # only libc.so defines the helpers in the bundle
python DH2Work-scratch4\win_model.py         # recomputed closure weights + the win model
```

### 9.2 Unicorn cannot execute VFP (this build)

```powershell
python DH2Work-scratch4\vfp_probe.py        # 28 VFP encodings on Cortex-A15 -> all UC_ERR_INSN_INVALID
python DH2Work-scratch4\vfp_models2.py      # all 33 UC_CPU_ARM_* models decode vadd.f32 -> NONE
python DH2Work-scratch4\vfp_enable.py       # CPACR+FPEXC enable sequence -> UC_ERR_EXCEPTION
```

### 9.3 The qemu-arm differential harness

```powershell
python DH2Work-scratch4\gen_blob.py            # blob.bin + GOT table (verbatim libc bytes)
python DH2Work-scratch4\make_patched_libc.py   # libc.patched.so + blob_patched.bin
wsl -d Ubuntu -- bash -lc "cd .../vfp-test && bash build.sh"
```

`gen_blob.py` asserts `file_offset(vaddr) - 0x1000` mapping and that the copied range has no
out-of-range PC-relative loads; `make_patched_libc.py` asserts that **0 bytes changed outside
the 28 target symbol bodies**. Result: 5,564 changed bytes in a 992,808-byte file.

### 9.4 Closure-weight recomputation (the census correction)

`win_model.py` walks each symbol's `[st_value, st_value+st_size)`, decodes it, and for every
branch leaving that range resolves the PLT stub through `libc`'s own `.rel.plt` and recurses.

```
__aeabi_fsub   census 16   recomputed 173   (2 + __addsf3's 171)
__aeabi_dsub   census 10   recomputed 266   (2 + __adddf3's 264)
binary-wide    census 2,730,672  ->  recomputed 3,142,934 weighted units
```

### 9.5 The sweep

```bash
W=16 bash sweep3.sh fmul_ex_s fadd_ex_s fsub_ex_s fdiv_ex_s \
    fmul_ex_r fadd_ex_r fsub_ex_r fdiv_ex_r fmul_ex_ea fadd_ex_ea \
    fnan_ex fnan_add_ex f2iz f2uiz i2f ui2f fcmpeq cfcmple cdcmple \
    dmul_ex_s dadd_ex_s dsub_ex_s ddiv_ex_s dmul_ex_r dnan_ex dmul_ex_ea \
    fmul_s fadd_s fsub_s fdiv_s dmul_s dadd_s
# plus the artifact-level repeats:
W=16 bash sweep3.sh fmul_p_s fadd_p_s fsub_p_s fdiv_p_s fmul_p_r fadd_p_r fsub_p_r fdiv_p_r \
    fmul_p_ea fnan_p fnan_add_p dmul_p_s dadd_p_s dsub_p_s ddiv_p_s \
    f2iz_p f2uiz_p i2f_p ui2f_p d2f_p_s fcmpeq_p_s dcmpeq_p_s
```

`*_p_*` = "new" side is the function **inside `libc.patched.so`**; all others = the
`vfp.S` replacement compiled to the same ABI.
Per-test definitions: `_s` = structured operand set (all exponent pairs × boundary mantissas);
`_ea` = exhaustive in `a` × 24 boundary `b`; `_r` = random full-domain; `nan` = 2²⁴-bit NaN
payload space × representative `b`, both operand orders.

### 9.6 The engine's 40 `__aeabi_*` imports

```
atexit d2f d2iz d2lz d2uiz dadd dcmpeq dcmpge dcmpgt dcmple dcmplt dcmpun ddiv dmul dsub
f2d f2iz fadd fcmpeq fcmpge fcmpgt fcmple fcmplt fdiv fmul fsub i2d i2f idiv idivmod
ldivmod lmul ui2d ui2f uidiv uidivmod ul2d uldivmod unwind_cpp_pr0 unwind_cpp_pr1
```

(Note: no `__aeabi_fcmpun`, no `__aeabi_frsub`, no `__aeabi_c*`.)

---

## 10. Recommendation

1. **Do the in-place helper replacement.** 28 symbol bodies tested (5,564 bytes changed, 0
   bytes changed elsewhere); ship the 20-symbol subset recommended in §5.1 — the six real
   arithmetic bodies, the ten compares, `fcmpun`/`dcmpun` and `i2f`/`ui2f`. Addresses are
   preserved, no ZettaBridge or Dynarmic change is needed, and the measured effect is a
   **88.9 %** reduction of the weighted helper-body cost with **0 mismatches in
   ≈ 1.79 × 10¹⁰ comparisons** on the exact variants (§3.4).
2. **Use the NaN-guarded forms, never the bare `vmul.f32`.** The guard costs ~10 instructions
   and converts a known, characterised divergence (33.5 M mismatching pairs over the
   NaN/denormal/infinity region alone) into measured bit-exactness.
3. **Do not patch `__aeabi_fsub`/`__aeabi_dsub`** — they are 8 bytes; they are `eor` +
   tail-call and inherit the patched add bodies.
4. **Do not patch the integer conversions in this pass** (§3.8): `f2iz` and `f2uiz` measurably
   disagree with `vcvt` on out-of-range and NaN inputs. `i2f`/`ui2f` are exhaustively identical
   and may be replaced.
5. **Bump `assets/zb-version.txt`**, or the patch will not reach upgraded installs
   (`RuntimeBundle.java:19-31`).
6. **Before shipping, close risks 1 and 2** (§7): measure `FPSCR.FZ|DN` at helper entry in a
   live session, and re-run the differential suite hosted on ZettaBridge's pinned Dynarmic.
   Until risk 2 is closed, "bit-exact" means "bit-exact against a faithful ARMv7 VFP model",
   which is what qemu-arm provides — not what the device will run.
7. **Then** use the census's top-15 ranking for targeted call-site inlining (alternative (a)),
   which is where the remaining ≈ 5–6 instructions/site live.
