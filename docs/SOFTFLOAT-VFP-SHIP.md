# Shipping the VFP soft-float helper replacement — bytes, A/B, and what is still unproven

**Verdict in one line:** the patched guest `libc.so` exists, is byte-audited, and is bit-exact
against the shipped bodies over **18,353,231,360 comparisons on qemu-arm and a further
784,370,762 on the pinned Dynarmic** (0 mismatches on both, each with a negative control that
fails loudly); it **does** make the game faster on the device, but by **+11.3 %** on the robust
time-aggregated rate (28.17 → 31.36 FPS) rather than the modelled 42.8–44.7 % — and the earlier
"verified" replacement was **wrong for `__aeabi_fcmplt`/`fcmple`** (490,620 mismatches each in
67,108,864 comparisons), fixed here — see §3.3 and §5.3.

Everything marked **[measured]** was produced in this session. Everything marked **[inference]**
is reasoning. Everything marked **UNPROVEN** is a hole with the check that closes it.

Artifacts (all under `DH2Work-toolchain\vfp\`):

| artefact | path | sha256 |
|---|---|---|
| patcher | `patch\patch_libc.py` | — |
| replacement bodies | `harness\vfp-test\vfp.S` | — |
| assembled bodies | `harness\vfp-test\vfp.o` | `9f8f63681c16b9643785ddebc7794737e1e24470108ca32326bd1a777d74e06e` |
| pristine guest libc | `patch\libc.pristine.so` | `23ee728839ebb17bae3e9fc73034142be2466a6d694c0404ef5aeafdb1009069` |
| **patched guest libc** | `patch\libc.patched.so` | `d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8` |
| change manifest | `patch\manifest.json` | — |
| signed control APK | `out\dh2-control.apk` | `3adee6b734712a936d136868bedc1422070b31a97697754a095f588e48300b23` |
| signed patched APK | `out\dh2-patched.apk` | `b7637bcca277181121ab3ef965b97a3fc788fd91cc6cacbc7dfee48dc93ff1f0` |

Both APKs are versionCode **1103** (control) / **1104** (patched), per the Lead's addendum 8 to
the shared-device version-code allocation, signed with
`DH2Work-stage\compatibility\work\dh2-local-test.p12` / alias `dh2-local-test`, and carry the
**shipped** host library `lib/arm64-v8a/libzbridge.so`
`25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` / 3,306,992 B — untouched.

Version code 28 (the original allocation) is **not installable** on this handset any more: the
phone carries versionCode 1001 and Android 16 (SDK 36) rejects a downgrade for a non-debuggable
package, with `-d`/`pm install -r -d` both returning
`INSTALL_FAILED_VERSION_DOWNGRADE` **[measured]**. Hence 1103/1104.

---

## 1. The exact bytes changed

`patch\patch_libc.py` rewrites **only** the bytes of 20 symbol bodies inside
`assets/zb/sysroot/system/lib/libc.so`. Padding after the replacement body is left
byte-identical, so no byte of another symbol's body is touched even if one started inside the
old body's tail (the patcher asserts that too).

```
src sha256   23ee728839ebb17bae3e9fc73034142be2466a6d694c0404ef5aeafdb1009069  992,808 B
out sha256   d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8  992,808 B
bytes changed 742 of 992,808  = 0.075 %
pages changed 157, 158, 159, 160  (file offsets 0x9d000..0xa0fff)
```

| symbol | vaddr | old B | new B | old insns | new insns | changed B |
|---|---|---|---|---|---|---|
| `__aeabi_dadd` | 0x9ed98 | 1056 | 104 | 264 | 26 | 87 |
| `__aeabi_dcmpeq` | 0xa0f90 | 32 | 28 | 8 | 7 | 24 |
| `__aeabi_dcmpge` | 0xa0ff0 | 32 | 28 | 8 | 7 | 24 |
| `__aeabi_dcmpgt` | 0xa1010 | 32 | 28 | 8 | 7 | 24 |
| `__aeabi_dcmple` | 0xa0fd0 | 32 | 28 | 8 | 7 | 24 |
| `__aeabi_dcmplt` | 0xa0fb0 | 32 | 28 | 8 | 7 | 24 |
| `__aeabi_dcmpun` | 0x9f5fc | 60 | 28 | 15 | 7 | 26 |
| `__aeabi_ddiv` | 0x9f638 | 1048 | 104 | 262 | 26 | 93 |
| `__aeabi_dmul` | 0xa03a0 | 784 | 104 | 196 | 26 | 94 |
| `__aeabi_fadd` | 0x9f1e8 | 684 | 60 | 171 | 15 | 54 |
| `__aeabi_fcmpeq` | 0xa105c | 32 | 28 | 8 | 7 | 23 |
| `__aeabi_fcmpge` | 0xa10bc | 32 | 28 | 8 | 7 | 23 |
| `__aeabi_fcmpgt` | 0xa10dc | 32 | 28 | 8 | 7 | 23 |
| `__aeabi_fcmple` | 0xa109c | 32 | 28 | 8 | 7 | 23 |
| `__aeabi_fcmplt` | 0xa107c | 32 | 28 | 8 | 7 | 23 |
| `__aeabi_fcmpun` | 0x9eb78 | 28 | 28 | 7 | 7 | 21 |
| `__aeabi_fdiv` | 0x9faac | 596 | 60 | 149 | 15 | 57 |
| `__aeabi_fmul` | 0xa06f4 | 496 | 60 | 124 | 15 | 57 |
| `__aeabi_i2f` | 0xa00e4 | 132 | 16 | 33 | 4 | 9 |
| `__aeabi_ui2f` | 0xa02f4 | 124 | 16 | 31 | 4 | 9 |
| **total** | | | | **1332** | **215** | **742** |

`__aeabi_fsub` and `__aeabi_dsub` are **not** in the table and must not be: they are 8-byte
`eor r1,r1,#0x80000000 ; b <plt __addsf3>` / `eor r3,r3,#0x80000000 ; b <plt __adddf3>`
tail-calls at `0xa0930` / `0xa0928` **[measured]**, so patching the *add* bodies accelerates
subtraction for free and keeps the "negate `b` before the NaN rule" behaviour that the guarded
add bodies then see.

Aliasing is benign and useful: `__addsf3`=`__aeabi_fadd`, `__mulsf3`=`__aeabi_fmul`,
`__divsf3`=`__aeabi_fdiv`, `__adddf3`/`__muldf3`/`__divdf3`=`__aeabi_d*`,
`__floatsisf`=`__aeabi_i2f`, `__floatunsisf`=`__aeabi_ui2f`, `__unordsf2`/`__unorddf2`=the
`cmpun` helpers — every alias shares the patched address **and** the same `st_size` **[measured,
`manifest.json` → `aliases`]**. This is why the engine's own trace names `__mulsf3` at 23.7 %:
that body is now VFP.

### 1.1 ELF metadata is untouched (this is what makes an in-place patch safe)

`harness\tier3_check.py` compares the two files field by field **[measured]**:

```
size 992808 / 992808 same
elf header equal                    : True
segments equal                      : True
sections byte-identical             : ALL except .text   (.text header equal, data differs)
section count equal                 : True
dynsym entry-for-entry equal        : True   (0 differing: name/value/size/type/bind/shndx/other)
rel.plt equal                       : True
rel.dyn equal                       : True   (1338 entries)
dynamic equal                       : True
changed bytes                       : 742 of 992808 = 0.075 %
```

Symbol addresses are preserved exactly, which is required because `.data` holds a contiguous
run of 102 `R_ARM_ABS32` relocations (file `0xaa8a0..0xaaa3c`) pointing at those helpers. A
relink would invalidate it; a byte patch cannot.

---

## 2. Why these bodies are bit-exact

Soft-float AAPCS keeps every float in `r0`/`r1` and every double in `r0:r1`/`r2:r3`, so a VFP
body is a drop-in with no ABI change.

* **Arithmetic** (`fadd`, `fmul`, `fdiv`, `dadd`, `dmul`, `ddiv`) uses the guarded forms. A
  ~10-instruction integer pre-guard reproduces compiler-rt's NaN rule — *first operand wins,
  quiet bit forced* — before the VFP op runs, because ARM VFP instead prefers the **signalling**
  NaN. Without the guard this is not a nuance: bare `vmul.f32` differs on
  33,554,424 of 1,610,612,736 NaN/denormal/infinity-region comparisons **[measured, this
  session]**. `__subsf3` negates `b` *before* the rule, so the sub variants of the guard `eor`
  the sign first; the add bodies are what actually runs (see §1).
* **Compares** return 0/1 in `r0` (`vcmp` + `vmrs apsr_nzcv,fpscr` + conditional `mov`), with
  the conditions corrected in §3.3.
* **`__aeabi_i2f` / `__aeabi_ui2f`** are `vcvt.f32.s32/u32` and are exhaustively identical over
  all 2³² inputs each.

Deliberately **excluded**, each for a measured reason (`patch_libc.py` → `EXCLUDED`, and the
patcher refuses to run a symbol that is on both lists):

| symbol | why not |
|---|---|
| `__aeabi_f2d` | bare `vcvt.f64.f32` is not equivalent — the shipped body leaves a signalling NaN unquieted; 8,388,606 mismatches at FPSCR=0 |
| `__aeabi_f2iz` | `vcvt.s32.f32` saturates/returns 0 where compiler-rt returns `INT_MIN`; 33,554,429 mismatches over all 2³² inputs |
| `__aeabi_f2uiz` | NaN → `UINT_MAX` vs 0; 8,388,607 mismatches |
| `__aeabi_d2iz`, `__aeabi_d2uiz` | same out-of-range/NaN contract, domain 2⁶⁴, not exhaustively verified |
| `__aeabi_d2f` | domain 2⁶⁴; only structured coverage exists |
| `__aeabi_i2d`, `__aeabi_ui2d` | not verified at all |

---

## 3. Verification (off-device) — and the bug it found

### 3.1 The harness

Unicorn cannot execute a single ARM VFP instruction in this build (28 encodings × 33 CPU
models), so the instrument is `qemu-arm` under WSL plus a freestanding ARM32 differential
harness. It copies the guest libc's helper cluster `[0x9eb00,0xa5ae0)` **verbatim** into an RWX
buffer, fills only the 23 GOT slots the linker would fill, and calls pristine vs. replaced
bodies on identical inputs, comparing raw bit patterns. The `_p_` tests use the *second* copy
loaded from `blob_patched.bin` — the bytes as they exist **inside the shipped artifact**, not a
model of them.

Commands (WSL Ubuntu, `qemu-arm 10.2.1`, `arm-linux-gnueabihf-gcc 15.2.0`):

```bash
cd /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/vfp/harness/vfp-test
bash build.sh                       # assembles vfp.S -> vfp.o, links vfptest
W=16 BIN=./vfptest-ship bash sweep.sh <tests...>
```

### 3.2 Result — 33 tests on the shipped artifact **[measured]**

32 tests, **18,353,231,360 comparisons, 0 mismatches**; plus 1 negative control.

| test | comparisons | mismatches |
|---|---:|---:|
| `fmul_p_s` / `fadd_p_s` / `fsub_p_s` / `fdiv_p_s` (structured, all exponent pairs, both orders) | 4 × 67,108,864 | **0** |
| `fmul_p_r` / `fadd_p_r` / `fsub_p_r` / `fdiv_p_r` (random full domain) | 4 × 400,000,000 | **0** |
| `fmul_p_eb` / `fadd_p_eb` / `fsub_p_eb` / `fdiv_p_eb` (exhaustive over every zero, denormal, inf and NaN `a`, 2²⁵ values × 24 boundary `b`, both orders) | 4 × 1,610,612,736 | **0** |
| `fnan_p` / `fnan_add_p` (2²⁴ NaN payloads × 10 `b` × both orders) | 2 × 335,544,320 | **0** |
| `dmul_p_s` / `dadd_p_s` / `dsub_p_s` / `ddiv_p_s` (double structured) | 4 × 37,748,736 | **0** |
| `fcmpeq_p_s` … `fcmpun_p_s` (all six single compares) | 6 × 67,108,864 | **0** |
| `dcmpeq` … `dcmpun32` (all six double compares, 32-bit return prototype, NaN/inf boundaries included) | 6 × 37,945,600 | **0** |
| `i2f_p` / `ui2f_p` (exhaustive over all 2³² integers) | 2 × 4,294,967,296 | **0** |
| **negative control** `fmul_eb_plain` (bare `vmul.f32`, no guard) | 1,610,612,736 | **33,554,424** |

The negative control proves the harness and the guard are both live in the same run: with the
guard removed the same sweep fails loudly, e.g.
`fmul_eb_plain a=0x7fc00000 b=0x7f800010  ref=0x7fc00000 new=0x7fc00010`.

### 3.3 THE BUG: `vfp_fcmplt` / `vfp_fcmple` used signed-integer conditions **[measured]**

The previous evaluation's artifact-level sweep covered **only `__aeabi_fcmpeq`**
(`SOFTFLOAT-VFP-REPLACEMENT.md` §9.5, test list `fcmpeq_p_s`). Adding artifact tests for the
other five float compares and all six double compares found this:

```
fcmplt_p_s   67,108,864 compared   490,620 mismatches
        MISMATCH fcmplt_p_s a=0x00000000 b=0x7f800001  ref=0  new=1
fcmple_p_s   67,108,864 compared   490,620 mismatches
        MISMATCH fcmple_p_s a=0x00000000 b=0x7f800001  ref=0  new=1
```

After `VCMP` the flag states are `less N=1 Z=0 C=0 V=0`, `equal N=0 Z=1 C=1 V=0`,
`greater N=0 Z=0 C=1 V=0`, `unordered N=0 Z=0 C=1 V=1`. For the **unordered** state,
`LT` (`N != V`) is `0 != 1` = **true** and `LE` (`Z == 1 or N != V`) is **true** as well — so
`movlt`/`movle` return 1 where the shipped compiler-rt wrappers return 0. The correct
conditions are **`MI`** (`N == 1`, only "less") for `<` and **`LS`** (`C == 0 or Z == 1`) for
`<=`. `EQ`/`GE`/`GT`/`VS` were already right (`fcmpge_p_s`, `fcmpgt_p_s`, `fcmpun_p_s`,
`fcmpeq_p_s` were all green before and after).

`__aeabi_fcmplt` and `__aeabi_fcmple` are **both in the engine's 40 `__aeabi_*` imports**, so
the earlier patch list would have made every `a < b` and `a <= b` in the game true whenever an
operand was NaN. Fixed in `harness\vfp-test\vfp.S`; the pre-fix source and results are kept as
`patch\vfp.S.before-lt-le-fix` and
`harness\vfp-test\results-ship-cmp-BEFORE-LT-LE-FIX.txt`.

The same error was in `vfp_dcmplt`/`vfp_dcmple`. The project's double-compare test could not
have seen it: `test_double_ret32` swept exponents `0, 8, … 2040`, i.e. **no infinity and no
NaN**. That test now appends an explicit 16-value boundary set (±0, ±min/max denormal,
±min-normal, ±inf, ±qNaN, ±sNaN, all-ones) before sweeping.

### 3.4 The FPSCR gating condition, re-run on the tree that ships **[measured]**

`fpscr\fpscr_scan.py` (raw byte census) and `fpscr\fpscr_validate.py` (mode-aware symbol sweep,
no byte patterns) were re-pointed at `vfp\apkbuild\...\assets\zb` and re-run. Result: **10
`vmsr fpscr` in the whole set** — 2 in `libc.so` (`__fe_raise_inexact` `0x9f1dc`, `longjmp`
`0x68038`), 8 in `libm.so` (`0xfesetenv` `0x1ca48`, `feclearexcept`, `fesetexceptflag`,
`feraiseexcept`, `fesetround`, `feholdexcept`, `feupdateenv` ×2) — identical addresses and
counts to `FPSCR-DENORMAL-AUDIT.md`, with zero CPACR/FPEXC writes (FPSID reads only) and a
validation total of **673** transfers, again matching that document. So `FZ`/`DN` (bits 24–25)
still cannot be set by this bundle, and the safety argument transfers to the artifact that is
actually packaged. Raw output: `fpscr\scan-shipped.txt`, `fpscr\validate-shipped.txt`.

---

## 3.5 The same artifact run under **Dynarmic** (closing the "qemu is not the device" gap)

Everything in §3.1–§3.2 runs on qemu-arm. The device runs Dynarmic, and
`SOFTFLOAT-VFP-REPLACEMENT.md` §3.10 left "does Dynarmic's VFP behave like qemu's?" open. It is
closed here, off-device, on the pinned revision.

`DH2Work-toolchain\dynarmic` (A32-only, `libdynarmic.a` x86-64, built from commit
`86458a0bd369d63ba4c2ef812cacbb6c9080c065`) has a **byte-identical
`frontend/A32/translate/impl/vfp.cpp`** to the copy staged inside the host build
**[measured, `Get-FileHash`]** — the same translation unit ZettaBridge compiles.

`vfp\dynarmic\difftest.cpp` loads `blob.bin` at `0x10000000` and `blob_patched.bin` at
`0x10080000` into one Dynarmic A32 guest, fills only the GOT relocation slots, sets `FPSCR = 0`
(the audited guest condition), and calls both copies of each helper on identical operands.
Because the pristine bodies are **pure integer** code, Dynarmic executes them exactly; any
difference is therefore a property of the replacement under Dynarmic.

First, the control that proves the harness can see a divergence at all:

```
CONTROL    plain_vmul             100032 compared       2080 mismatches
      MISMATCH plain_vmul a=000000007fc00000 b=000000007f800100 ref=000000007fc00000 new=000000007fc00100
```

Bare `vmul.f32` **does** diverge under Dynarmic, on exactly the compiler-rt-vs-VFP NaN-selection
rule the guard exists to neutralise — so Dynarmic's floating-point behaviour is being exercised,
not bypassed. With the guard in place, every replaced body matches.

**Dynarmic differential result [measured, `dynarmic\dynarmic-run.txt`, 3 m 19 s wall]:**

```
CONTROL    plain_vmul           50331648 compared    1048572 mismatches
...
46 replacement-body entries    784370762 compared          0 mismatches
faults: 0
```

47 entries, **834,702,410 comparisons**. The 50,331,648-comparison negative control accounts
for all 1,048,572 mismatches; the **784,370,762 comparisons that exercise the replacement bodies
have 0 mismatches under Dynarmic** — including `bandxb` (a 1,048,576-value sweep over every
zero/denormal/infinity/NaN pattern of `a` against the 24-value boundary set, both operand
orders), 100,000,000 random full-domain pairs per float op, all twelve compares on
NaN/inf-bearing inputs, and `i2f`/`ui2f` on a 203,301-value sample.

This is the number that makes "it will run on the device" an *off-device* result rather than a
hope: the exact bytes that ship, executed by the exact JIT that runs them, reproduce the shipped
compiler-rt bodies bit for bit.

Source-level cross-check of the two semantics the replacement depends on **[read, not inferred]**:

* **`VCMP` flag state.** `backend/x64/emit_x64_floating_point.cpp:1431-1472` lowers `FPCompare`
  to `ucomiss` plus a hard-coded table `{greater 0010, less 1000, equal 0110, unordered 0011}`.
  Unordered is `N=0 Z=0 C=1 V=1` — the ARM ARM state, and the state §3.3's `MI`/`LS` fix is
  derived from. If Dynarmic had used the x86 flag ordering, `MI`/`LS` would have been wrong too.
* **NaN priority.** The arithmetic lowers to `mulss`/`addss`/`divss`, whose SNaN-wins behaviour
  is what the `plain_vmul` control above measured. **`FZ`/`DN`** are honoured through the host
  `FPCR` per `FPSCR-DENORMAL-AUDIT.md` §3, and the audit's census is re-confirmed for this
  bundle in §3.4.

Still **UNPROVEN** even after this: nothing about the *timing* — Dynarmic running the helpers
bit-exactly says nothing about how much faster they are on the device.

`RuntimeBundle.java:19-31` compares `assets/zb-version.txt` with the `.bundle-version` marker in
`files/zb` and returns early — skipping extraction — when they match. `build_apk.py:113-121`
only regenerates `zb-files.txt`/`zb-version.txt` on the *pinned guest* path; the locally rebuilt
guest path leaves whatever was already in `assets/zb`, which is exactly how a libc patch
becomes a no-op. `build\prepare_bundle.py` therefore regenerates both files from the bundle
contents using the same algorithm, **for every variant**, before the build.

**[measured]** in-APK payload, read back out of the signed APK by `build\verify_apk.py`, which
recomputes the token from the *packaged* bytes:

| variant | packaged `assets/zb/sysroot/system/lib/libc.so` | `assets/zb-version.txt` | token recomputed from packaged bytes matches |
|---|---|---|---|
| control | `23ee7288…09069` (pristine) | `352e152905e5608d30a5b8838dd1b060ec32af76293c492b73fc1abaac388b0f` | yes |
| patched | `d34addce…409e8` (patched) | `2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65` | yes |

`zb-files.txt` lists exactly the 19 `zb/**` entries present in the APK, and `verify_apk.py`
fails the build if either check or the shipped-host check does not hold.

**What is directly provable on this device and what is not.** The app is not debuggable and the
handset is not rooted, so `run-as`, `/data/data/local.dh2.fold7/files/zb/...` and
`/proc/<pid>/maps` are all Permission denied **[measured]**. The *installed* `base.apk` **is**
readable, so `harvest.ps1` pulls it after every install and records the token and libc sha256 it
actually carries (`out\installed-history.txt`). The argument is then:

1. both variants' packaged tokens are recomputed from the packaged bytes and differ from each
   other (§4 table) — the token is a real content hash, not a constant;
2. **[measured]** the build that was on the phone immediately before the first attempt (v1001,
   `perf-hw1000`) carried token `9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06`
   with the **pristine** libc `23ee7288…`. Its marker is therefore not my control token
   `352e1529…` and not my patched token `2dd2f3eb…` either, so the control install re-extracts
   and the patched install re-extracts again (control's marker is `352e1529…` ≠ `2dd2f3eb…`;
   the sequence is recorded in `out\installed-history.txt` by pulling the installed `base.apk`
   after every install);
3. §5.3's measured difference between the two arms is the behavioural confirmation that the new
   libc is the one running: the control and patched containers differ only in that one file, so a
   changed frame rate cannot come from anything else.

**UNPROVEN:** a direct read of `files/zb/.bundle-version` or of the extracted
`files/zb/sysroot/system/lib/libc.so`. Closing it needs either a debuggable/profileable-file
build, or one extra line in the *guest* `GameTrace.initialize()` that hashes
`context.getFilesDir()/zb/sysroot/system/lib/libc.so` into the group-readable plugin
`dh2-events.txt` (the same DEX is then in both arms, so the A/B stays valid).

---

## 5. Device A/B, in game

Device: Galaxy Z Fold7, serial `RFCY700ANZF`, unfolded 2184×1968, `svc power stayon true`.
Methodology, same for both arms:

* install (`adb install -r -d`), confirm the installed `base.apk` sha256 equals the artefact;
* launch `com.zettabridge.launcher.Dh2Activity`, tap LAUNCH GAME (1092, 807), then walk past the
  intro cinematics with the project's skip tap (1092, 1905);
* **the window starts only after `MyVideoView destroyed`** — the cinematic locks at 60.0 FPS and
  is excluded by construction, and again by the device-wall-clock window in `measure\batches.py`;
* in-game FPS is read from the engine's own trace
  `…/plugins/com.gameloft.android.GAND.GloftD2SS/dh2-events.txt`,
  `render returned frames=N elapsedMs=M`, per 120-frame batch: `FPS = Δframes / (ΔelapsedMs/1000)`;
* the trace is sampled every 10 s and merged (deduplicated by whole line) because a reinstall by
  another agent restarts the renderer mid-window; the installed `base.apk` is hashed at every
  sample, and the run is marked contaminated the moment it changes.

### 5.1 Runs attempted, and why most of them are not results **[measured]**

The device is shared with two other agents and every attempt so far has been either
contaminated or produced no frames at all. Recording them is the point: none of these is
reported as a win.

| # | arm | versionCode | outcome |
|---|---|---|---|
| 1 | control | 26 | **contaminated** — v27 `perfpin27` installed over it at 21:34:11, engine stopped at frames=960 |
| 2 | control | 30 | **contaminated** — v100 `perf-hw100` installed over it; only frames=1 recorded |
| 3 | control | 200 | **contaminated** — v300 `nativehook300` installed over it; only frames=1 recorded |
| 4 | control | 1100 | clean (`contaminated=False` for 110 s), but the engine rendered **exactly one frame** for the whole 200 s |
| 5 | control→patched | 1100/1101 | installs rejected: `INSTALL_FAILED_VERSION_DOWNGRADE` vs the installed 1001 |

The one stretch that did reach gameplay was during run 1, before the contaminating install, and
it is *not* usable as a pair — only the control arm was measured:

```
frames  d_ms  d_frames    FPS
   240 14644       120   8.194     loading
   360  2002       120  59.940  \
   480  1992       120  60.241   > panel-limited / light scene
   600  2003       120  59.910  /
   720  2513       120  47.752  \
   840  2408       120  49.834   > compute-bound
   960  2574       120  46.620  /
```

Two consequences that matter for reading the final A/B: the in-game baseline on this handset is
**~47–50 FPS, not the ~29 FPS the win model assumed**, and part of the frame budget is already
pinned at the **60 Hz panel cap**, which will truncate whatever the patch wins in those scenes.
Both are reasons to report raw per-120-frame batches rather than a single mean.

### 5.2 The run that produced no frames, and the prime suspect **[measured]**

In run 4 the arm installed correctly (installed `base.apk` sha256 equalled the artefact at every
sample), nothing installed over it, and the engine trace for the entire 200 s contains **one**
record. The mark sequence is:

```
start -> DungeonHunter2 created/resumed -> surface #1 -> engine running -> frames=1
      -> MyVideoView created -> (~20 s video) -> MyVideoView paused -> DungeonHunter2 resumed
      -> surface #2 -> MyVideoView destroyed -> nothing further
```

The renderer is recreated after the intro video but never draws a second frame. The only
difference from the run that *did* reach gameplay is the tap schedule: run 4 tapped the skip
position every 10 s for 90 s, run 1 used the single-shot sequence from
`profile\20-measure-device.ps1` (launch, +8 s tap LAUNCH GAME, +25 s tap skip once, +12 s tap
1092,984, then measure). This is **not** attributed to the patch: the control arm — pristine
`libc.so` — showed it too. It is recorded here so that a repeat of the A/B uses the single-shot
sequence, and so that "only frames=1 after `MyVideoView destroyed`" is recognised immediately as
a run with no data rather than as a slow warm-up. **Confirmed by the Lead's successful reversal
run (§5.3): launch, +9 s tap (1092, 807), then no further taps.**

### 5.3 The measurement that counts — the reversal A/B **[measured]**

The Lead ran the reversal pair on the shared device (`control → patched`, back-to-back, same
resumed save and level) using the working drive sequence — **launch, +9 s tap (1092, 807), then
NO further taps** — which is the fix for §5.2. Raw traces:

* `Dungeon hunter 2 Rework\_device-evidence\vfp-ab\rev-control.txt`
* `Dungeon hunter 2 Rework\_device-evidence\vfp-ab\rev-patched.txt`

The arms were verified by hashing `assets/zb/sysroot/system/lib/libc.so` **out of the installed
`base.apk`** after each install: control `23ee7288…` (pristine), patched `d34addce…` (patched).
The container was re-signed with `apktool` to raise the version code, so its APK sha256 differs
from `out\dh2-patched.apk`; the **payload is identical** — same libc sha256 and same bundle token
`2dd2f3eb…` — which is what the run depended on, and both were read back out of the installed
APK rather than assumed.
Every number below is reproduced by `measure\batches.py` + `measure\compare.py` on those two
files; nothing here is a model.

Per-120-frame batches, in order (fps, and the batch's own Δms):

```
idx |  control fps  d_ms |  patched fps  d_ms
  1 |    59.701    2010 |    58.881    2038
  2 |    58.910    2037 |     7.638   15710
  3 |    58.824    2040 |    21.622    5550
  4 |    58.852    2039 |    42.872    2799
  5 |    58.824    2040 |    48.880    2455
  6 |    16.809    7139 |    26.584    4514
  7 |    57.831    2075 |    26.275    4567
  8 |    57.582    2084 |    29.190    4111
  9 |    58.537    2050 |    56.899    2109
 10 |    57.143    2100 |    58.252    2060
 11 |    44.810    2678 |    57.252    2096
 12 |     9.225   13008 |    41.987    2858
 13 |    22.680    5291 |    42.268    2839
 14 |    23.933    5014 |    53.812    2230
 15 |    23.117    5191 |    56.845    2111
 16 |    21.142    5676 |    57.361    2092
 17 |    22.980    5222 |    55.866    2148
 18 |    35.950    3338 |    47.393    2532
 19 |    24.024    4995 |    25.884    4636
 20 |    24.038    4992 |    26.543    4521
 21 |    27.009    4443 |    29.880    4016
 22 |    21.053    5700 |    27.517    4361
 23 |    25.740    4662 |    58.766    2042
 24 |    22.351    5369 |    51.370    2336
 25 |    23.269    5157 |    24.341    4930
 26 |    22.676    5292 |    30.573    3925
 27 |    36.866    3255 |    45.541    2635
 28 |    26.207    4579 |    33.463    3586
 29 |    24.495    4899 |    28.083    4273
 30 |    22.426    5351 |    23.729    5057
 31 |    51.392    2335 |    29.376    4085
 32 |                 |    16.611    7224
```

**The answer depends on the statistic, and the spread is the honest result:**

| statistic | control | patched | change |
|---|---|---|---|
| **time-aggregated rate over the whole window** (Σframes / Σtime) | 3,720 frames / 132.1 s = **28.17 FPS** | 3,840 frames / 122.4 s = **31.36 FPS** | **+11.3 %** |
| **patched − control, whole-window rate** | — | — | **+3.19 FPS** |
| per-batch median (all batches) | 25.740 | 37.725 | +46.6 % |
| per-batch mean (all batches) | 35.432 | 38.799 | +9.5 % |
| per-batch median, first 13 control / first 6 patched discarded (n = 18 / 26) | 23.979 | 37.725 | **+57.3 %** |
| per-batch mean, same discard | 26.593 | 39.811 | **+49.7 %** |
| time-aggregated rate, same discard | 25.272 | 34.907 | +38.1 % |

The Lead's headline (**median +57.3 %, mean +49.7 %**) reproduces exactly, but only under that
asymmetric discard — 13 of 31 control batches (42 %) against 6 of 32 patched (19 %). The two
captures are **not scene-aligned**: control's panel-limited stretch is batches 1–5 and 7–10,
patched's is 9–11, 14–17 and 23–24. The control arm also reaches the 60 Hz cap (max 59.701,
one batch within 0.5 FPS of 60, nine within 1.5), so "0 % at ceiling in either arm" holds only
for an exact-60.0 tolerance. Per-batch statistics that ignore how long each batch took therefore
move by a factor of five depending on the alignment rule, while the time-aggregated rate — the
physically meaningful one — moves by **+11.3 %**.

**Statistical strength: weak.** Per-batch means are 35.43 ± 3.0 (s.e., n = 31) and 38.80 ± 2.6
(n = 32); the difference is 3.37 ± 3.97, i.e. not significant on this sample, and the slowest
batches overlap almost exactly (control 9.23 / 16.81 / 21.05 / 21.14 / 22.35 vs patched
7.64 / 16.61 / 21.62 / 23.73 / 24.34). What the reversal pair does establish is **direction**:
patched is faster in every aggregation tried, and it has fewer slow batches
(14 vs 18 below 30 FPS).

**Against the model.** The modelled win was `p × 21.25 ms × 0.889` = 10.99–11.50 ms/frame
(42.8–44.7 %, "≈29 → ≈50 FPS"). Measured: 3.19 FPS at a 28.17 FPS baseline, i.e. **+11.3 %**, or
about **a quarter of the model**; the static ceiling of 21.25 ms is not reachable, consistent
with §5.1's finding that helper bodies are a smaller share of the frame than the census assumed.
The direction is right and the mechanism is real; **the model's magnitude is falsified by this
measurement**, and the per-batch median figure should not be quoted as a frame win.

**UNPROVEN / next step:** a scene-locked A/B — same level segment, same input script, several
`control → patched → control → patched` alternations, comparing the same ordinal batches — would
pin the magnitude. Nothing in this session did that.

---

## 6. What remains unproven

1. ~~**Dynarmic's VFP, not qemu-arm's.**~~ **CLOSED off-device** — §3.5: the same artifact under
   the pinned Dynarmic is 784,370,762 comparisons / 0 mismatches, with a negative control that
   fails on exactly the NaN rule the guard neutralises. What remains open is only whether the
   *device* is running this Dynarmic revision, which §4's token argument and §5.3's measured
   difference both support but do not prove directly.
2. **Direct proof that the extracted `files/zb/.../libc.so` is the patched one** — §4. The
   installed `base.apk` is readable and was hashed (control `23ee7288…`, patched `d34addce…`),
   and the tokens differ, so `RuntimeBundle.install` cannot have taken its early return; but the
   on-device copy itself was not read back.
3. **The magnitude of the win.** §5.3: +11.3 % on the time-aggregated rate, +57.3 % on an
   asymmetrically-trimmed per-batch median, and not statistically significant on 31/32 batches.
   The model's 42.8–44.7 % is not supported. A scene-locked, alternating A/B is the missing
   experiment.
4. **Coverage gaps** deliberately left shipped: `f2d`, `f2iz`, `f2uiz`, `d2iz`, `d2uiz`, `d2f`,
   `i2d`, `ui2d` are **not** replaced; `__aeabi_d2f` and `__aeabi_i2d`/`ui2d` have only partial
   coverage and were excluded rather than rushed.
5. **FPSCR cumulative exception flags** are now set where the soft-float bodies never set them.
   Nothing in the engine reads FPSCR **[measured, §3.4]**, so nothing can observe it.
6. **The static win model is not a frame prediction.** With 1332 → 215 instructions across the
   replaced bodies, the *body* cost falls 83.9 %; the per-call engine veneer (3 instructions) and
   the guest `bl`/`bx lr` remain, so the per-site cost falls less — and §5.1/§5.3 show the frame
   share is smaller still.
7. **Everything here is one bundle.** The FPSCR argument and the ELF audit are properties of the
   bundle's contents; a sysroot swap invalidates them. Re-run `fpscr_scan.py` +
   `fpscr_validate.py` and `tier3_check.py` and fail the build if the write set grows or the
   metadata diverges.

---

## 7. Reproduce — every command

```powershell
# 1. stage a private build tree (reads DH2Work-stage, writes only under DH2Work-toolchain\vfp)
#    vfp\apkbuild\{fold7-build,research,downloads,patched,dh2-local-test.p12}

# 2. FPSCR census against exactly what ships
python DH2Work-toolchain\vfp\fpscr\fpscr_scan.py
python DH2Work-toolchain\vfp\fpscr\fpscr_validate.py

# 3. assemble the replacement bodies and run the differential harness
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/vfp/harness/vfp-test && bash build.sh"
wsl -d Ubuntu -- bash -lc "cd .../vfp/harness/vfp-test && W=16 BIN=./vfptest-ship bash sweep.sh <tests...>"

# 4. patch libc, emit the manifest and the harness blobs
python DH2Work-toolchain\vfp\patch\patch_libc.py `
  --src DH2Work-toolchain\vfp\patch\libc.pristine.so `
  --vfp-obj DH2Work-toolchain\vfp\harness\vfp-test\vfp.o `
  --out DH2Work-toolchain\vfp\patch\libc.patched.so `
  --manifest DH2Work-toolchain\vfp\patch\manifest.json `
  --blob-dir DH2Work-toolchain\vfp\harness\vfp-test

# 5. ELF metadata audit
python DH2Work-toolchain\vfp\harness\tier3_check.py <pristine> <patched>

# 5b. the same artifact under the pinned Dynarmic (host x86-64, WSL)
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/vfp/dynarmic && bash build.sh --run 0.05"

# 6. build both signed APKs (1103 = control, 1104 = patched, per the Lead's addendum 8)
pwsh -File DH2Work-toolchain\vfp\build\build_variant.ps1 -Variant control -VersionCode 1103
pwsh -File DH2Work-toolchain\vfp\build\build_variant.ps1 -Variant patched -VersionCode 1104

# 7. on device (only when no other agent holds the phone)
pwsh -File DH2Work-toolchain\vfp\measure\harvest.ps1 -Rounds 1 -WindowSeconds 100 -ArmSeconds 90
```
