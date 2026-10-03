# Shipping the VFP soft-float helper replacement — bytes, A/B, and what is still unproven

**Verdict in one line:** the patched guest `libc.so` is built, byte-audited and differentially
verified against the shipped bodies over **18,353,231,360 artifact-level comparisons with 0
mismatches**; the earlier "verified" replacement was **wrong for `__aeabi_fcmplt`/`fcmple`**
(490,620 mismatches each in 67,108,864 comparisons) and that is fixed here — see §3.3;
the on-device A/B and its contamination handling are in §5.

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
| signed control APK | `out\dh2-control.apk` | `5549602a0bdb340d39669b35f0e5c4ad22f4f4f14e10c544f880ffe82e10d004` |
| signed patched APK | `out\dh2-patched.apk` | `7e1baf629f22ba1991d19728f1cc2a927e2ed3f4f75855b7148718323996151d` |

Both APKs are versionCode **28** (`DH2_VERSION_CODE=28`, per the shared-device allocation),
signed with `DH2Work-stage\compatibility\work\dh2-local-test.p12` / alias `dh2-local-test`, and
carry the **shipped** host library `lib/arm64-v8a/libzbridge.so`
`25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` / 3,306,992 B — untouched.

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

## 4. The version token, and how extraction is verified

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
2. the patched install is preceded by the control install, whose marker is therefore the
   control token `352e15…`; the patched APK's token is `2dd2f3…`, so
   `RuntimeBundle.install` cannot take the early return and must re-extract;
3. §5's FPS/behaviour change is the behavioural confirmation that the new libc is the one
   running — and an A/B/A repeat (patched → control again) reverses it.

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

**DEVICE RESULT: see `out\batches-control.json` / `out\batches-patched.json`.** *(filled in
below once the harvest completes; a contaminated run is reported as contaminated, not as a
number.)*

---

## 6. What remains unproven

1. **Dynarmic's VFP, not qemu-arm's.** Every "0 mismatches" above is against a faithful ARMv7
   VFP model. The device executes the guest through Dynarmic, and `SOFTFLOAT-VFP-REPLACEMENT.md`
   §3.10 could not read the pinned revision's FP semantics. The on-device run is a
   *behavioural* smoke test (the game must run and render correctly), not a bit-exactness proof.
2. **Direct proof that the extracted `files/zb/.../libc.so` is the patched one** — §4.
3. **Coverage gaps** deliberately left shipped: `f2d`, `f2iz`, `f2uiz`, `d2iz`, `d2uiz`, `d2f`,
   `i2d`, `ui2d` are **not** replaced; `__aeabi_d2f` and `__aeabi_i2d`/`ui2d` have only partial
   coverage and were excluded rather than rushed.
4. **FPSCR cumulative exception flags** are now set where the soft-float bodies never set them.
   Nothing in the engine reads FPSCR **[measured, §3.4]**, so nothing can observe it.
5. **The static win model is not a frame prediction.** With 1332 → 215 instructions across the
   replaced bodies, the *body* cost falls 83.9 %; the per-call engine veneer (3 instructions) and
   the guest `bl`/`bx lr` remain, so the per-site cost falls less. The measured A/B is the only
   number quoted as a frame win.
6. **Everything here is one bundle.** The FPSCR argument and the ELF audit are properties of
   `zb-version.txt 9ae1e3d9…`'s contents; a sysroot swap invalidates them. Re-run
   `fpscr_scan.py` + `fpscr_validate.py` and `tier3_check.py` and fail the build if the write
   set grows or the metadata diverges.

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

# 6. build both signed APKs at versionCode 28
pwsh -File DH2Work-toolchain\vfp\build\build_variant.ps1 -Variant control -VersionCode 28
pwsh -File DH2Work-toolchain\vfp\build\build_variant.ps1 -Variant patched -VersionCode 28

# 7. on device (only when no other agent holds the phone)
pwsh -File DH2Work-toolchain\vfp\measure\harvest.ps1 -Rounds 1 -WindowSeconds 100 -ArmSeconds 90
```
