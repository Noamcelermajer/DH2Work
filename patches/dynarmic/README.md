# Dynarmic patches for the DH2Work host

These are the Dynarmic changes the DH2Work compatibility host authors itself, with a
self-contained test that exercises the new instructions and an A/B that proves the patch
is what makes them work.

Upstream: <https://github.com/Vita3K/dynarmic>, pinned at
`86458a0bd369d63ba4c2ef812cacbb6c9080c065` (0BSD). The changes are not a backport: that
revision is upstream `master` HEAD and upstream still lacks both of them.

## The patch

* `0001-dh2work-t32-armv8-and-arm64-halt-guard.patch` — one file, two independent
  changes. It applies with `git apply` to a pristine pin checkout (checked), and also
  with `git am`, since it is `git format-patch` output.

### Hunk 1 - arm64 A32 backend: precise-halt guard for GetSetElimination

File: `src/dynarmic/backend/arm64/a32_address_space.cpp`, `A32AddressSpace::GenerateIR`.

The `A32GetSetElimination` IR pass removes redundant guest register moves. It is only
sound while no guest memory access can stop the block half-way: when the host asks for
precise memory aborts (`UserConfig::check_halt_on_memory_access = true`), a data access
may return to the host, and any guest register write that the pass has not committed yet
is then reported to the callbacks stale. The A64 arm64 backend
(`backend/arm64/a64_address_space.cpp`) and both x64 backends
(`backend/x64/a32_interface.cpp`, `backend/x64/a64_interface.cpp`) already guard their
elimination pass exactly this way; the A32 arm64 backend was the only one of the four
without the guard. The patch adds `&& !conf.check_halt_on_memory_access`.

**Why this game needs it.** The DH2Work host runs with precise guest faults so it can
report the faulting address and guest registers (`docs/ARCHITECTURE.md`). The analysis in
`docs/DYNARMIC-INDEPENDENCE.md` (§0 row 0001-A, §2.1) classifies this as an upstream
omission, not a host-specific hack, and it is independent of the instruction work.

### Hunk 2 - T32 (Thumb-2) ARMv8 acquire/release + CRC32

Files:

* `src/dynarmic/frontend/A32/decoder/thumb32.inc` - the decoder rows.
* `src/dynarmic/frontend/A32/translate/impl/a32_translate_impl.h` - the visitor
  declarations.
* `src/dynarmic/frontend/A32/translate/impl/thumb32_load_store_dual.cpp` - translator
  bodies for LDAB, LDAH, LDAEX, LDAEXB, LDAEXH, LDAEXD, STLB, STLH, STLEX, STLEXB,
  STLEXH, STLEXD.
* `src/dynarmic/frontend/A32/translate/impl/thumb32_crc32.cpp` - translator bodies for
  CRC32 and CRC32C in byte, halfword and word sizes (new file, registered in
  `src/dynarmic/CMakeLists.txt`).

The A32 (ARM-state) frontend already implements the whole family
(`frontend/A32/decoder/arm.inc`, `frontend/A32/translate/impl/synchronization.cpp`,
`frontend/A32/translate/impl/a32_crc32.cpp`). In T32 the pinned tree had only `STL` and
`LDA`, so every other encoding matched no row and reached `thumb32_UDF()`, i.e. an
undefined-instruction exception.

**Why this game needs it - the census** (`docs/DYNARMIC-INDEPENDENCE.md` §5.1). The
requirement comes from the guest **sysroot**, not from the game's own libraries (which
contain none of these instructions). State-matched LLVM census, confirmed sites:

| binary | confirmed | composition (excerpt) |
|---|---:|---|
| `linker` | 943 | ldaex 416, stlex 369, ldab 54, crc32cw 30, crc32w 22, crc32b 18, ldaexd 11, stlexd 8, ldaexh 7 |
| `libc.so` | 292 | ldaex 90, stlex 49, crc32cw 33, ldaexd/stlexd 22 each, stlb 20, ldab 14 |
| `liblog.so` | 8 | ldab 6, ldaex 1, stlex 1 |
| `libDungeonHunter2.so` | 0 | - |
| `libStormGLOFT.so` | 0 | - |
| `libnativeinterface.so` | 0 | - |

The sysroot's `libc.so` is loaded on every run and its hits sit in scudo's mutex and
allocator hot paths; the dynamic linker's load path is full of them. Without these decoder
rows the guest stops on the first one with an undefined-instruction exception.

Semantics follow the in-tree ARM-state code: the non-exclusive forms move data with an
ordered access, and the exclusive forms go through `ExclusiveReadMemory*` /
`ExclusiveWriteMemory*` with an ordered access type, which is what the global exclusive
monitor turns into acquire/release ordering. Operand combinations the architecture calls
UNPREDICTABLE (PC operands, a status register aliasing the base or data register) are
reported through the unpredictable-instruction callback.

The patch deliberately does **not** include the Advanced SIMD narrowing family
(VADDHN/VRADDHN/VSUBHN/VRSUBHN). The census in `docs/DYNARMIC-INDEPENDENCE.md` §0 row 0002
and §5.2 found zero architecturally-legal instances in any guest binary, so change 3 of
that analysis stays out.

### How the encodings were derived

Every row was taken from the ARM ARM and checked against the real assembler
(`clang --target=thumbv8a-none-none-eabi` plus `llvm-objdump -d --triple=thumbv8a`).
The test's `tests/t32_armv8_seq.s` is that self-check, kept in the tree and re-assembled
on every test build.

## Applying the patch

```sh
git clone https://github.com/Vita3K/dynarmic.git /tmp/dynarmic
git -C /tmp/dynarmic checkout 86458a0bd369d63ba4c2ef812cacbb6c9080c065
git -C /tmp/dynarmic apply /path/to/patches/dynarmic/0001-dh2work-t32-armv8-and-arm64-halt-guard.patch
# or: git -C /tmp/dynarmic am /path/to/patches/dynarmic/0001-dh2work-t32-armv8-and-arm64-halt-guard.patch
```

## Building and testing

x86_64 RelWithDebInfo (the project's proven configure):

```sh
cmake -S /tmp/dynarmic -B /tmp/dynarmic-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON -DDYNARMIC_TESTS=OFF \
  -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF
ninja -C /tmp/dynarmic-build
```

Use `-DCMAKE_BUILD_TYPE=Debug` for the second configuration.

The test under `tests/` builds Dynarmic from a checkout of your choice and runs the
semantic checks:

```sh
patches/dynarmic/tests/build_and_run.sh --dynarmic /tmp/dynarmic --build ~/t32-patched
```

The A/B, against a pristine pin checkout:

```sh
patches/dynarmic/tests/build_and_run.sh --dynarmic /tmp/pristine-dynarmic \
  --build ~/t32-pristine --expect-undefined
```

`--expect-undefined` asserts that all 18 encodings raise
`Exception::UndefinedInstruction` on the unpatched decoder. `EVIDENCE.md` records the
exact runs and their outputs.

## Test layout

| file | role |
|---|---|
| `tests/t32_armv8_seq.s` | the 18 instructions plus four LDAEX setup forms, assembled by the real T32 assembler |
| `tests/gen_seq_inc.py` | turns the object and its disassembly into raw bytes plus a name/offset table |
| `tests/t32_armv8_test.cpp` | minimal A32 UserCallbacks over a flat guest buffer; semantic checks and the pristine A/B mode |
| `tests/CMakeLists.txt` | builds a chosen Dynarmic checkout and links the test against it |
| `tests/build_and_run.sh` | assemble, generate, configure, build, run |

## Ownership and licence

Dynarmic is 0BSD, so the patch adds no notice obligations of its own. The patch text,
comments and structure are original to DH2Work: the changes were re-derived from the ARM
ARM and from Dynarmic's own in-tree ARM-state implementations, not copied or paraphrased
from any third-party patch.
