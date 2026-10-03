# Dynarmic independence — what the two patch files actually do, and what the new host must author itself

**Question answered here.** The current compatibility host (ZettaBridge) carries two patches applied
to its Dynarmic submodule. The replacement host must not ship or derive from ZettaBridge. So: what do
those patches do, does *this game* need the behaviour, and what do we have to write ourselves?

**Answer in one paragraph.** Neither patch is a backport: the pinned revision `86458a0b` **is** the
current tip of upstream `Vita3K/dynarmic`, and upstream still lacks all three changes today, so no
"newer pin" can replace them. Of the three changes, **one is needed** (the T32 ARMv8
load-acquire/store-release + CRC32 instruction family — reached by the guest *sysroot*, on the scudo
mutex/malloc and dynamic-linker hot paths, **not** by the game's own libraries, which contain zero of
these instructions), **one is needed as a genuine upstream bug fix** (the `!check_halt_on_memory_access`
guard in the ARM64 A32 backend, which the other three backends already have), and **one is not needed
at all** (the four Advanced SIMD narrowing instructions `VADDHN`/`VRADDHN`/`VSUBHN`/`VRSUBHN` — no
guest binary in this title contains a single architecturally-legal instance; the ZettaBridge
justification for it is the host's own Flutter/Skia UI, not this game). Dynarmic is **0BSD**, which
imposes no obligations on our own patches; the bundled externals' notices are the only licence work.

---

## 0. Verdict at a glance

| # | Change | Kind | Does this game reach it? | Decision |
|---|---|---|---|---|
| 0001-A | `A32AddressSpace::GenerateIR`: skip `A32GetSetElimination` when `check_halt_on_memory_access` | **(a) upstream bug fix** — an omission, not a feature | Yes, whenever the host runs with precise memory aborts | **Author it** (one line; mirrors three other backends) |
| 0001-B | T32 `LDAB/LDAH/LDAEX*/STLB/STLH/STLEX*/CRC32*` decoder + translators | **(b) missing-feature addition** (port of in-tree A32 code into the T32 frontend) | Yes — via the **sysroot** (`libc.so` 292, `linker` 943, `liblog.so` 8 confirmed sites); **zero** in the game's own three libraries | **Author it** |
| 0002 | A32 ASIMD `VADDHN/VRADDHN/VSUBHN/VRSUBHN` | **(b) missing-feature addition**, but its driver is host-side | **No** — 0 legal instances anywhere in the guest | **Do not author it** (re-measure if the new host itself runs 32-bit Skia/Flutter) |

Final verdict: **(c) we must author our own equivalent fixes** for 0001-A and 0001-B. Option (a)
"stock Dynarmic unchanged" fails on the sysroot. Option (b) "a specific later upstream revision" is
**impossible** — see §4.

---

## 1. Method, and two traps that had to be disarmed

Every number below is reproducible from the scratch scripts listed in the appendix. Two traps are
worth recording because they invalidated an earlier pass:

**Trap 1 — stripped ELFs have no `$a`/`$t` mapping symbols.** A linear ARM/Thumb disassembly must
choose an instruction set state. On `libStormGLOFT.so`, `libm.so`, `libc++.so`, `libz.so`,
`liblog.so`, `libnativeinterface.so` and the `linker` there are no mapping symbols, and both capstone
and `llvm-objdump` then silently default to **ARM**, turning Thumb-2 bytes into plausible-looking but
false ARM instructions (e.g. Thumb `movw r4, #0x400` → ARM `vaddhn.i16`, and a 16-bit Thumb `push`
prologue → `svc`). This produced a first pass full of phantom `vaddhn` hits.

*Fix.* The ARM ELF convention is that bit 0 of an `STT_FUNC` `st_value` is set for Thumb code. That
was validated against ground truth where both signals exist:

```
python mode2.py <libc.so> <libDungeonHunter2.so> ...
  libc.so             low-bit Thumb marker vs $a/$t mapping symbols: 2643/2643 agree
  libDungeonHunter2.so low-bit Thumb marker vs $a/$t mapping symbols: 31018/31018 agree
```

2643/2643 and 31018/31018 agreement makes the low bit a trustworthy state signal for the stripped
files too. All censuses below use it, and mask bit 0 off before slicing the function body.

**Trap 2 — capstone 5.0.7 cannot decode CRC32, in either state.** Verified by assembling with real
LLVM and feeding the bytes back:

```
clang --target=armv8a-none-none-eabi -c a32t.s   →  e1011041  crc32b r1,r1,r1
                                                    e1411241  crc32cw r1,r1,r1
capstone ARM:  0xe1011041 → CAPSTONE NO DECODE
               0xe1411241 → CAPSTONE NO DECODE
```

So a capstone census can never *confirm* a CRC32 instruction. The reference census therefore uses
**LLVM** (`llvm-objdump -d --triple={thumb,arm}v8a --section=.text`), which decodes the whole family,
and only accepts a decode when the address also falls inside a function whose ELF-declared state
matches the state that produced it (`verify2.py`). Capstone is used only for the independent
mode/state validation in Trap 1.

Besides that, every ARM-ARM-style encoding string used for the raw scan was asserted against bytes
produced by clang before use — **12/12 T32 patterns and 14/14 A32 patterns verified** (`raw.py`
`pattern_verification`). This is what makes the "zero raw hits" statements below sound: any real
instruction necessarily matches its own pattern, so a zero raw count is a hard upper bound of zero.

---

## 2. `dynarmic-0001-thumb32-armv8.patch` — three unrelated changes in one file

### 2.1 Hunk A — precise memory aborts in the ARM64 A32 backend (patch lines 1-15)

```c
-    if (conf.HasOptimization(OptimizationFlag::GetSetElimination)) {
+    if (conf.HasOptimization(OptimizationFlag::GetSetElimination) && !conf.check_halt_on_memory_access) {
         Optimization::A32GetSetElimination(ir_block, {.convert_nzc_to_nz = true});
```

* File/function: `src/dynarmic/backend/arm64/a32_address_space.cpp`, `A32AddressSpace::GenerateIR()`.
* The pinned tree at `a32_address_space.cpp:169` has the condition **without** the guard.
* The same tree has the guard on the other three backends:
  * `backend/arm64/a64_address_space.cpp:337`
  * `backend/x64/a32_interface.cpp:219`
  * `backend/x64/a64_interface.cpp:278`
* Effect: with `check_halt_on_memory_access` set, a memory access may halt the block mid-way. If
  `A32GetSetElimination` has been run, guest register writes from earlier instructions in the block
  have not been committed to the JIT state, so a precise memory abort observes stale guest registers.
* Live in the current host: `ZettaBridge/core/src/guest_thread.cpp:62` sets
  `cfg.check_halt_on_memory_access = precise_faults;` (and `:57` sets
  `cfg.arch_version = Dynarmic::A32::ArchVersion::v8`).

**Classification: (a) upstream bug fix.** This is not a feature or a host-specific hack — it is the
A32-on-ARM64 backend being the one of four that never received a guard the others have. It also is
**not** game-specific: any guest that takes a precise memory abort inside a multi-instruction block is
affected.

### 2.2 Hunk B1 — the ARMv8 A32 load-acquire / store-release family in T32 (patch lines 16-38, 68-99, 100-199)

Decoder rows added to `frontend/A32/decoder/thumb32.inc`: `LDAB`, `LDAH`, `LDAEX`, `LDAEXB`,
`LDAEXH`, `LDAEXD`, `STLB`, `STLH`, `STLEX`, `STLEXB`, `STLEXH`, `STLEXD`. Declarations in
`a32_translate_impl.h`; bodies in `thumb32_load_store_dual.cpp`. `LDAEXB/H/D` and `STLEXB/H/D`
forward to the existing `thumb32_LDREXB/H/D` / `thumb32_STREXB/H/D`, which is architecturally right:
the acquire/release variants are the same operation with stronger ordering, and Dynarmic's
exclusive-monitor callbacks already provide that ordering.

**The A32 (ARM-state) frontend already had all twelve.** Pinned tree:

* `frontend/A32/decoder/arm.inc:111-131` — `STL`, `STLEX`, `LDA`, `LDAEX`, `STLEXD`, `LDAEXD`,
  `STLB`, `STLEXB`, `LDAB`, `LDAEXB`, `STLH`, `STLEXH`, `LDAH`, `LDAEXH`.
* Implementations in `frontend/A32/translate/impl/synchronization.cpp:68-270`
  (`arm_LDAB` :69, `arm_LDAH` :83, `arm_LDAEX` :98, `arm_LDAEXB` :113, `arm_LDAEXD` :128,
  `arm_LDAEXH` :146, `arm_STLB` :176, `arm_STLH` :191, `arm_STLEXB` :206, `arm_STLEXD` :226,
  `arm_STLEXH` :249, `arm_STLEX` :270).

In T32, the pinned tree has **only two** of the family: `thumb32.inc:22` (`STL`) and `:23` (`LDA`),
with bodies at `thumb32_load_store_dual.cpp:129` and `:213`.

**Classification: (b) missing-feature addition** — the T32 frontend is incomplete relative to the
A32 frontend that ships in the same tree. Encodings were confirmed independently:

```
clang --target=thumbv8a-none-none-eabi -c t32.s ; llvm-objdump -d --triple=thumbv8a t32.o
  0: e8d3 1f8f   ldab   r1, [r3]        <-- same as the patch's declared encoding
  4: e8d3 1f9f   ldah   r1, [r3]
 10: e8d3 1fdf   ldaexh r1, [r3]
 2c: e8c3 12f4   stlexd r4, r1, r2, [r3]
```

### 2.3 Hunk B2 — T32 `CRC32`/`CRC32C` (patch lines 39-44, 48-67)

Decoder rows `111110101100nnnn1111dddd10zzmmmm` / `111110101101...` plus two wrappers in
`a32_crc32.cpp` that call the existing `CRC32Variant(*this, Cond::AL, sz, n, d, m, ...)`.

Again the A32 side already exists: `arm.inc:15-16` and `arm_CRC32`/`arm_CRC32C` in the same file.
Verified encodings (note capstone cannot decode these at all — §1 Trap 2):

```
clang --target=thumbv8a-none-none-eabi ; llvm-objdump --triple=thumbv8a
 30: fac1 f181   crc32b  r1, r1, r1
 34: fac1 f191   crc32h  r1, r1, r1
 38: fac1 f1a1   crc32w  r1, r1, r1
 3c: fad1 f181   crc32cb r1, r1, r1
```

**Classification: (b) missing-feature addition.** (T32 `HLT` and SHA1 remain missing upstream too —
noted in `upstream-modified/third_party/README.md:56-58` and not relevant here.)

---

## 3. `dynarmic-0002-asimd-narrowing.patch` — four Advanced SIMD instructions

`asimd.inc` un-comments and completes four entries that the pinned tree leaves disabled:

* pinned `frontend/A32/decoder/asimd.inc:62-66`:
  `//INST(asimd_VADDHN, ...)`, `//INST(asimd_VRADDHN, ...)`, `//INST(asimd_VSUBHN, ...)`,
  `//INST(asimd_VRSUBHN, ...)`
* the patch turns them into real patterns and adds `HighNarrowingOperation` plus four wrappers in
  `asimd_three_regs.cpp` (patch lines 41-49, 58-89, 98-112), built from
  `VectorAdd`/`VectorSub`, an optional rounding broadcast, `VectorLogicalShiftRight` and
  `VectorNarrow`.

The A64 frontend has the same four instructions already: `frontend/A64/decoder/a64.inc:751-765` and
`frontend/A64/translate/impl/simd_three_same.cpp:44` and `:508-520`. **Classification: (b)
missing-feature addition** — an A64 implementation mirrored into the A32 frontend.

Two facts I verified because the patch's decoder rows must serve **both** instruction set states:

1. **T32 NEON reaches the A32 table.** `translate_thumb.cpp:135-139` calls
   `DecodeASIMD<TranslatorVisitor>(ConvertASIMDInstruction(thumb_instruction))`, and
   `ConvertASIMDInstruction` (`translate_thumb.cpp:84-95`) maps the Thumb encoding onto the ARM
   encoding. Checked against real assembler output:

   ```
   T32  ef82 1402  vaddhn.i16 d1,q1,q1   → U=bit28=0 → 0xF2000000|0x00821402 = 0xF2821402
   A32  f2821402   vaddhn.i16 d1,q1,q1        (identical)
   T32  ff82 1402  vraddhn.i16            → U=1      → 0xF3821402  == A32 vraddhn.i16  ✓
   ```

   So the added `asimd_three_regs.cpp` entries do cover Thumb-32 code, not just ARM code.

2. **The size field is where the patch puts it, and the `sz == 0b11` guard is correct.**
   `clang --target=armv8a` / `--target=thumbv8a` encode `i16 → bits21-20 = 00`, `i32 → 01`,
   `i64 → 10`, and `i8` is rejected by the assembler as an invalid operand; A64 `addhn v1.8b` puts
   `size = 00`. The patch's `esize = 8 << sz; doubled = 2*esize` therefore matches the architecture's
   own pseudocode, and `sz == 0b11` really is UNDEFINED. (This matters for §5.2.)

---

## 4. Are they ZettaBridge-authored, or backports?

**They are authored, not backports, and no newer pin can replace them.** Commands and results:

```
git ls-remote --heads https://github.com/Vita3K/dynarmic
  86458a0bd369d63ba4c2ef812cacbb6c9080c065  refs/heads/master     <-- identical to our pin
  9a7e34bd221f04c01af5ce11b77fc62e5512c1a9  refs/heads/main
  65e8968ac60f77062559ed985577218a2c2a586c  refs/heads/aarch64  (plus fix-addw, fix-shift, fm,
                                                       gpl, vita3k, vmaxminnm)

git -C DH2Work-toolchain\dynarmic log --oneline -1
  86458a0b fix compilation with Clang 20
git -C DH2Work-toolchain\dynarmic status --porcelain
  (empty — the staged tree is pristine at the pin, patches NOT applied)
```

* **`master` HEAD *is* our pin.** There is no later upstream revision of the named upstream.
* `main` is a stale 2023 branch: the compare API reports
  `"status":"diverged","ahead_by":2,"behind_by":153` against `86458a0b`, and its only two commits are
  Vita3K-specific Vita tweaks (VZIP unpredictability, `STR` pre-index `n == t`). Nothing to do with
  our three changes.
* Current upstream file contents fetched from `raw.githubusercontent.com/Vita3K/dynarmic/master/`
  still show **all three changes absent**:
  * `frontend/A32/decoder/thumb32.inc` — still only `STL` and `LDA`; no `LDAB`/`CRC32`.
  * `frontend/A32/decoder/asimd.inc` — `VADDHN`/`VRADDHN`/`VSUBHN`/`VRSUBHN` still commented out.
  * `backend/arm64/a32_address_space.cpp` — `GenerateIR` still has the unguarded
    `if (conf.HasOptimization(OptimizationFlag::GetSetElimination))`.

**Consequence:** option (b) of the question is unreachable. Because the pin is upstream HEAD, "use a
newer pinned Dynarmic" buys nothing; and because the A32 frontend already contains the ARM-state
implementations of the whole 0001-B family, the T32 work is a mechanical port of code that exists in
the very same tree — it is exactly the kind of change we can write ourselves from the ARM ARM
without touching ZettaBridge's patch text.

*Not checked:* whether an upstream-adjacent fork (Skyline, yuzu) carries an equivalent T32 port.
Marked UNKNOWN in §8; it does not change the verdict, since our host is pinned to `Vita3K/dynarmic`.

### Note on where the patched checkout actually is

The task stated the patches "are also applied in the ZettaBridge checkout at
`...\ZettaBridge\third_party\dynarmic`". That path exists in the Windows stage tree but is **not an
initialized submodule** — it has no `.git` directory, and `git -C ...\third_party\dynarmic status`
walks up and reports the *parent* ZettaBridge repository instead:

```
Test-Path ...\ZettaBridge\third_party\dynarmic\.git   → False
git -C ...\ZettaBridge\third_party\dynarmic log --oneline -1
  7c647a4 AGENTS: hand over the Unity bring-up...      <-- ZettaBridge's own HEAD, not dynarmic's
git -C ...\ZettaBridge status --porcelain              → 13 modified files
```

Those 13 files are the pre-existing ZettaBridge project patch (`zettabridge-dh2.patch`; the same
13-file count is asserted by `DH2Work-toolchain\scripts\02-clone-and-patch.sh:24` and documented at
`DH2Work\docs\NATIVE-BUILD.md:58`) — they were already modified before this analysis and were not
touched by it. The *patched* Dynarmic checkout therefore only exists inside the WSL build tree
(`~/dh2/src/ZettaBridge/third_party/dynarmic`, created at `02-clone-and-patch.sh:26-34`).

This does not weaken any conclusion above, because the pin was checked directly and is pristine
(`DH2Work-toolchain\dynarmic`, clean at `86458a0b`), and because the applied diffs were read from the
patch files themselves and compared against both the pinned tree and current upstream `master`.

---

## 5. Does *this* game need them?

Guest inventory scanned (engine SHA-256 confirmed
`36498EB8180FFB74759E6305E9596DB999F18583D460F3B8534ABCB6022F5E80`):

* game libraries — `original\lib\armeabi-v7a\libDungeonHunter2.so`, `libStormGLOFT.so`,
  `libnativeinterface.so`
* guest sysroot — `assets\zb\sysroot\system\lib\{libc,libm,libc++,libz,liblog,libdl}.so` and
  `assets\zb\sysroot\system\bin\linker`. Provenance is recorded in the ZettaBridge tree:
  `docs/phase3-device-test.md` — *"The arm32 sysroot from the Android 17 GSI, in `sysroot/`"*,
  extracted by `tools/extract_sysroot.sh`; `docs/superpowers/specs/2026-09-13-guest-system-boundary-design.md`
  — *"libc++ and `/system/bin/linker` come from an AOSP arm32 build and run as translated"*.

### 5.1 `dynarmic-0001` — yes, but the requirement comes from the sysroot, not the game

State-matched LLVM census (`verify2.py`): **confirmed** patch-1 instructions per binary —

| binary | confirmed | composition |
|---|---:|---|
| `linker` | **943** | `ldaex` 416, `stlex` 369, `ldab` 54, `crc32cw` 30, `crc32w` 22, `crc32b` 18, `ldaexd` 11, `stlexd` 8, `ldaexh` 7, … |
| `libc.so` | **292** | `ldaex` 90, `stlex` 49, `crc32cw` 33, `ldaexd`/`stlexd` 22 each, `stlb` 20, `ldab` 14, … |
| `liblog.so` | **8** | `ldab` 6, `ldaex` 1, `stlex` 1 |
| `libDungeonHunter2.so` | **0** | — |
| `libStormGLOFT.so` | **0** | — |
| `libnativeinterface.so` | **0** | — |
| `libm.so`, `libc++.so`, `libz.so` | 0 | (`libz.so` has raw Thumb-decoded `crc32b` matches in a local function with no symbol; see §8) |

Sample sites, independently re-checked with LLVM:

```
llvm-objdump -d --triple=thumbv8a --section=.text --start-address=... <file>
  libc.so   0x418b6  _ZN5scudo11HybridMutex7tryLockEv : e8d0 1fef  ldaex r1, [r0]
  libc.so   0x3d8e4  android_mallopt                  : e8c0 4f8f  stlb  r4, [r0]
  libc.so   0x42e78  scudo Allocator<>               : fad2 f3a0  crc32cw r3, r2, r0
  linker    0x586a0  soinfo::set_dt_runpath          : fac2 f89d  crc32h r8, r2, sp
  linker    0x44c06  LoadTask::load                  : e8d0 0fef  ldaex ...
```

Reachability is therefore not in question: `libc.so` is loaded into the `:guest` process on every run,
the hits are inside `scudo`'s mutex/allocator paths and the dynamic linker's load path, and the
pinned Dynarmic has no decoder row for any of them — `translate_thumb.cpp:145-148` falls through
`DecodeThumb32` to `visitor.thumb32_UDF()`, i.e. an undefined-instruction exception. The T32 `CRC32`
case is worse than an outright miss: `0xFAC1` is a legal T32 32-bit prefix, so it is not rejected
early, it simply matches no row.

This also explains why the ZettaBridge README's "the linker alone has ~1000 of them" is right: my
independent count for the linker is 943.

**Two important qualifications.**

1. This is a property of the *sysroot we ship*, not of the game. The alternative to a modern AOSP
   ARM32 bionic on an arm64-only device is an older one; if we deliberately select/target an
   ARMv7-A-only arm32 bionic, 0001-B stops being necessary. Our own architecture document commits to
   the same class of sysroot — `DH2Work/docs/ARCHITECTURE.md:56-57` and `:72` list
   *"AOSP ARM32 bionic sysroot"* as the source of the guest `libc.so`, `libdl.so`, `libm.so`,
   `libz.so` and `linker`, all *"native ARM32, **translated**"*.
2. The engine's own instruction mix cannot reach it — this is a hard bound, not a sample. The engine
   has 31,018 `STT_FUNC` symbols covering the whole `.text` (5,961,032 bytes at `0x30ee00`), state
   assignment validated 31018/31018, 1,515,304 decoded instructions, and **zero raw pattern hits**
   for `LDAB/LDAH/LDAEX*/STLB/STLH/STLEX*` in either state and **zero** for T32 `CRC32/CRC32C`. The
   only engine raw hits in the whole scan were 8 `A32_CRC32` and 68 `A32_CRC32C`, against a uniform
   noise expectation of ~91 for that pattern's 18 wildcard bits; every sampled one decodes as ordinary
   Thumb, e.g.

   ```
   llvm-objdump -d --triple=thumbv8a --section=.text --start-address=0x8a3b10 libDungeonHunter2.so
     8a3b10: 4242    rsbs  r2, r0, #0        (owner _ZNSt6locale6globalERKS_, ELF says THUMB)
   ```

   So the engine is consistent with a build for a pre-ARMv8 target, which matches
   `docs/SOFTFLOAT-FINDING.md:8-29`.

### 5.2 `dynarmic-0002` — no, this game cannot reach it

State-matched LLVM census: **0** confirmed `VADDHN`/`VRADDHN`/`VSUBHN`/`VRSUBHN` in *every* binary —
engine, `libStormGLOFT.so`, `libnativeinterface.so`, `libc.so`, `libm.so`, `libc++.so`, `libz.so`,
`liblog.so`.

The single binary that produced candidates is the `linker`: 7 of them, and **all 7 are
`vraddhn.i64`/`vrsubhn.i64`** — `sz = 0b11`, which is architecturally UNDEFINED and rejected by the
patch's own `HighNarrowingOperation` (`if (sz == 0b11) return v.DecodeError();`). They also sit inside
a data region, as the linker's own mapping symbol shows:

```
llvm-objdump -d --triple=thumbv8a --start-address=0x120c80 --stop-address=0x120c98 linker
00120c58 <__dl_$d>:                       <-- $d = data
  120c82: ffee 7682   vrsubhn.i64 d23, q15, q1
  120c86: ffee 1d7a   <unknown>
```

So those bytes are not instructions to begin with, and even if they were, `sz == 0b11` is exactly the
case the patch declines to implement. They provide zero justification.

The same rejection applies to every other phantom: the `libStormGLOFT.so`, `libm.so`, `libc++.so`,
`liblog.so` and `libz.so` candidates were produced by decoding Thumb-2 code as ARM, and `verify2.py`
rejected them on the ELF-declared state (e.g. `0x4d6e8 vrsubhn.i16 decoded as a but ELF says t
(_mesa_float_to_half)`; `0x1d728 vaddhn.i16 decoded as a but ELF says t (log2)`).

**But NEON/VFP itself absolutely is executed** — so the reason is not "no SIMD", it is "not these four
instructions". Deduplicated census:

| binary | functions | instructions | `v*` | of which float/VFP |
|---|---:|---:|---:|---:|
| `libDungeonHunter2.so` | 31,018 | 1,515,304 | **10** (0.00%) | **0** |
| `libStormGLOFT.so` | 1,486 | 119,654 | 741 (0.62%) | 495 |
| `libc.so` | 2,643 | 153,936 | 3,499 (2.27%) | 438 |
| `libm.so` | 243 | 19,278 | 7,555 (**39.19%**) | 5,725 |
| `libc++.so` | 1,373 | 96,114 | 1,233 (1.28%) | 328 |
| `linker` | 4,153 | 387,922 | 8,992 (2.32%) | 1,938 |
| `libz.so` / `liblog.so` | 95 / 64 | 9,919 / 3,384 | 54 / 23 | 1 / 1 |
| `libnativeinterface.so` / `libdl.so` | 10 / 17 | 882 / 122 | 0 / 0 | 0 |

The engine's ten `v*` instructions are `vhadd.u8`(2), `vhadd.u32`(2), `vld4.32`(2), `vrsra.u64`,
`vhadd.s16`, `vaddl.u32`, `vaddl.s8` — integer SIMD only, no VFP, consistent with
`docs/SOFTFLOAT-FINDING.md:14-21` (which counted six on a slightly different instruction set).
`libStormGLOFT.so` genuinely runs hard-VFP float (`vcmpe.f32` 57, `vmov.f32` 55, `vmul.f32` 32,
`vdiv.f32` 26) and `libm.so` is 39% VFP-managed double-precision code — so an ASIMD/VFP-capable
implementation is required in general, just not *these four* narrowing opcodes.

The ZettaBridge justification for 0002 is host-side, not game-side:
`upstream-modified/third_party/README.md:72-75` says *"Flutter's Skia premultiplies decoded PNG alpha
with `vraddhn.i16`"*. Flutter/Skia is the wrapper application's own UI. I could **not** confirm that
linkage from the extracted bundle (no `libflutter.so`/`libapp.so`/Skia objects are present under
`ZettaBridge\build\launcher`; only `assets` and `jniLibs\arm64-v8a`) — see §8.

---

## 6. The verdict

**Option (c): we author our own equivalent fixes.** Specifics, in the order they matter:

**(1) T32 ARMv8 load/store acquire-release + `CRC32` family — author it, from the ARM ARM.**
Add the twelve `thumb32_*` decoder rows and their translator bodies, plus `thumb32_CRC32`/`CRC32C`.
Write it fresh against the ARM ARM and the in-tree A32 implementations (`arm.inc:111-131`,
`synchronization.cpp:68-270`, `arm.inc:15-16` / `arm_CRC32`), with your own comments and your own
names — do not copy the patch file's text or structure. The encodings are architectural facts, and
all 26 encodings used above were independently reproduced from clang/llvm-objdump.

*Verification:* (i) assemble every added encoding with `clang --target=thumbv8a` and assert
`llvm-objdump` round-trips it — this reproduces the 12/12 self-test in `raw.py`; (ii) semantic tests
for `LDAB`/`LDAEX`/`STLEX`/`CRC32` against the ARM ARM pseudocode using Dynarmic's own test harness
(the A32 forms already have reference behaviour in `synchronization.cpp` to diff against, and
`tests/` exists in the tree); (iii) an end-to-end run whose guest executes `scudo::HybridMutex::tryLock`
and the dynamic linker — i.e. boot the guest past the linker, which is exactly what fails today.

**(2) The `A32AddressSpace::GenerateIR` guard — author it, one line.**
`if (conf.HasOptimization(OptimizationFlag::GetSetElimination) && !conf.check_halt_on_memory_access)`.
This mirrors `a64_address_space.cpp:337` and `x64/a32_interface.cpp:219` verbatim in intent; one line,
no dependency on any AArch32 encoding.

*Verification:* with `check_halt_on_memory_access = true`, plant a guest memory abort in the middle of
a block that has already written a guest register, and assert the guest register state observed by the
callbacks matches the architectural value. This is the exact defect the guard addresses.

**(3) The ASIMD narrowing family — do not author it, and re-measure before ever adding it.**
No guest code in this title can reach it. Add it only if the new host itself executes 32-bit
Skia/Flutter (or another A32 guest that is measured to contain these opcodes).

### What would falsify this verdict

* **Against the "0001-B needed" conclusion.** Re-run the state-matched census against the *actual*
  sysroot the new host ships, and require a non-zero count:
  `python verify2.py <each shipped arm32 guest lib>`; if the shipped sysroot is an ARMv7-A-only
  build, the T32 ARMv8 rows become dead code and 0001-B should be dropped. Two further triggers to
  re-measure: (i) any relink/rebuild of `libStormGLOFT.so` — `ARCHITECTURE.md:124` says
  `patches/storm/storm_import_fix.c` is a *freestanding C replacement* linked into that library at
  `0xd3800`, and if it is ever compiled with a modern NDK targeting `armv8-a` it could *introduce*
  ARMv8 instructions where the pristine library has none (I measured the pristine library at 0); the
  documented guest-rewrite pipeline is safe on this axis, since
  `docs/GUEST-REWRITE-PIPELINE.md:301-303` pins it to `armv7a-linux-androideabi21` with
  `-march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16`; (ii) any replacement of the game's own
  libraries with builds targeting `armv8-a`.
* **Against the "0002 not needed" conclusion.** Find one architecturally-legal (`sz != 0b11`)
  `VADDHN`/`VRADDHN`/`VSUBHN`/`VRSUBHN` at a *state-matched* executed instruction boundary in any
  shipped guest binary. The test is `python verify2.py <binary>` returning a non-zero
  `patch2 CONFIRMED` count. Note this test is a strict superset check: the raw pattern scan is an
  upper bound, so a zero raw count is conclusive while a non-zero raw count still needs the
  state-matched confirmation.
* **Against the "0001-A needed" conclusion.** Show the new host runs with
  `check_halt_on_memory_access = false` (no precise guest faults) *and* never needs register state
  after a halting memory access. Given `docs/ARCHITECTURE.md:149` describes guest signals being
  delivered through a syscall layer and `:94-97` describes reporting registers on guest faults, this
  is unlikely — but it is the one measurement that would retire the guard.

---

## 7. Licence

**0BSD confirmed — not MIT.** `DH2Work-toolchain\dynarmic\LICENSE.txt` is 12 lines and is the BSD
Zero Clause text:

```
Copyright (C) 2017 merryhime <git@mary.rs>

Permission to use, copy, modify, and/or distribute this software for
any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES ...
```

There is **no** "the above copyright notice ... shall be included in all copies" clause — that
sentence is what distinguishes 0BSD from ISC/MIT, and its absence is the proof. (`LICENSE.txt` itself
carries no `SPDX-License-Identifier` line; the per-file `SPDX-License-Identifier: 0BSD` headers and
`README.md:215` — *"dynarmic is under a 0BSD license. See LICENSE.txt for more details."* — agree.)

**Obligations we take on by patching and shipping:**

* **From Dynarmic itself: none.** 0BSD permits use, copy, modification and distribution for any
  purpose, with or without fee, and imposes no notice-retention, no source-disclosure and no
  copyleft. We may keep our patches private and ship binaries without reproducing the licence text.
  We are not obliged to contribute anything back, and our patches do not change the licence of the
  combined work.
* **From Dynarmic's bundled externals: yes, real ones.** `README.md:217-410` lists them, and
  `NATIVE-BUILD.md:57` confirms we build with `DYNARMIC_USE_BUNDLED_EXTERNALS=ON`. These require
  notice retention (and, for the BSD forms, the standard warranty disclaimer) in binary
  distributions:
  * `biscuit` — MIT (`README.md:219`)
  * `catch` — Boost Software License 1.0 (`:236`) — requires the licence text to accompany the
    distribution
  * `fmt` — BSD-2-Clause (`:264`) — *"Redistributions in binary form must reproduce the above
    copyright notice"*
  * `mcl`, `oaknut` — MIT (`:292`)
  * `robin-map` — MIT (`:318`)
  * `xbyak` — BSD-3-Clause (`:344`) — note the third clause: *"Neither the name of the copyright
    owner nor the names of its contributors may be used to endorse or promote products"*
  * `zydis` — MIT (`:395`); the tree also carries `zycore` under `externals/`
* **Practical rule for our patch files.** Do not copy, port or paraphrase ZettaBridge's patch text,
  comments or structure — those are PolyForm-Noncommercial-licensed expression. Re-derive the change
  from the ARM ARM and from Dynarmic's own in-tree A32/A64 implementations, and keep our patch as our
  own file with our own commentary. Mark the files as modified (good practice; not a 0BSD condition).
  Keep the externals' `LICENSE`/`COPYING` files in the shipped tree, or reproduce their notices in an
  open-source-notices screen.

---

## 8. UNKNOWN — needs measurement

* **`libz.so`'s 33 raw `crc32b` matches.** `verify2.py` decoded them in the Thumb sweep inside
  `crc32_combine` but rejected them because the containing *local* function has no symbol in
  `.dynsym` (the library is stripped). All 95 declared `libz.so` functions are Thumb and there are no
  ARM functions, so the Thumb sweep is aligned and these are **probably** genuine ARMv8 `crc32b`
  instructions in the sysroot's zlib — which would be one more sysroot library needing 0001-B.
  UNKNOWN — needs a per-instruction confirmation pass over address ranges that no symbol covers.
* **The `libc++.so` / `libm.so` CRC32-style candidates** are in the same category for the same reason.
* **Whether ZettaBridge's Flutter/Skia layer is 32-bit guest code.** No `libflutter.so`, `libapp.so`
  or Skia object was found under `ZettaBridge\build\launcher` (only `assets\zb\*` and
  `jniLibs\arm64-v8a`). If the Flutter engine is a 64-bit host library it could never reach
  Dynarmic's A32 frontend, and the README's stated reason for 0002 would be unexplained. It does not
  affect the verdict for *this game*, which contains no legal instance of the four instructions.
  UNKNOWN — needs the wrapper APK's own `jniLibs` and any `libapp.so`/AOT snapshot to be inspected.
* **Faithfulness of my instruction census to Dynarmic's own decoder.** My census uses capstone and
  LLVM, not Dynarmic's decoder. For the specific mnemonics that matter, both agree with the
  architecture and with Dynarmic's `*.inc` patterns, and the raw pattern scan is an independent upper
  bound — but a definitive run would feed the guest `.text` through Dynarmic's own
  `disassembler`/`DecodeThumb32` tables directly. UNKNOWN — needs a small driver linked against the
  pinned tree.
* **Whether any upstream-adjacent fork (Skyline, yuzu) already has the T32 ARMv8 rows.** Not
  checked; irrelevant to a host pinned to `Vita3K/dynarmic`, but it would tell us whether the fix is
  a known-good port to compare against.

---

## Appendix — commands used

All scratch scripts live in `DH2Work-scratch9\` (nothing else was written; the single deliverable is
this file).

| Claim | Command |
|---|---|
| Pin is clean and equals master HEAD | `git -C DH2Work-toolchain\dynarmic log --oneline -1` ; `git -C ... status --porcelain` ; `git ls-remote --heads https://github.com/Vita3K/dynarmic` |
| `main` is 153 behind | `web_fetch https://api.github.com/repos/Vita3K/dynarmic/compare/master...main` |
| upstream still lacks all three changes | `web_fetch https://raw.githubusercontent.com/Vita3K/dynarmic/master/src/dynarmic/frontend/A32/decoder/{thumb32,asimd}.inc` and `.../backend/arm64/a32_address_space.cpp` |
| the guard exists on the other three backends | `grep check_halt_on_memory_access DH2Work-toolchain\dynarmic\src` |
| host runs with precise faults + `arch_version = v8` | `grep check_halt_on_memory_access ZettaBridge\core\src` ; read `core/src/guest_thread.cpp:53-64` |
| T32/A32 encodings verified against a real assembler | `raw.py` → `pattern_verification: T32 12/12, A32 14/14 ok` |
| low-bit Thumb marker is trustworthy | `python mode2.py <files>` → `2643/2643`, `31018/31018` agreement with `$a`/`$t` |
| capstone cannot decode CRC32 | `clang --target=armv8a -c a32t.s` then capstone on the emitted words |
| confirmed patch-1 sites per binary | `python verify2.py <all guests>` |
| patch-2 candidates are `sz == 0b11` / `$d` data | `python raw.py` size histogram + `llvm-objdump --triple=thumbv8a --start-address=0x120c80 linker` |
| engine's CRC32-pattern hits are plain Thumb | `llvm-objdump --triple=thumbv8a --section=.text --start-address=0x8a3b10 libDungeonHunter2.so` |
| NEON/VFP census | `python vv.py` |
| T32→A32 ASIMD conversion is exact for the narrowing family | assemble with `clang --target={arm,thumb}v8a` and compare `0xF2821402` etc. |
| licence | read `DH2Work-toolchain\dynarmic\LICENSE.txt` (12 lines) ; `README.md:215-410` |
