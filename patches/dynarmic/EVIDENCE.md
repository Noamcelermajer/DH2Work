# Evidence

All work was off-device, in WSL `Ubuntu-22.04` on an x86_64 host:

```
Ubuntu clang version 14.0.0-1ubuntu1.1
cmake version 3.22.1
ninja 1.10.1
12 cores
```

* pristine pin checkout: `~/dh2/third_party/dynarmic` at
  `86458a0bd369d63ba4c2ef812cacbb6c9080c065`
* patched working copy: `~/dh2/dynarmic-custom` (commit
  `8bf3b2ac DH2Work: T32 ARMv8 acquire/release + CRC32, and arm64 A32 halt guard`)
* patch under test:
  `patches/dynarmic/0001-dh2work-t32-armv8-and-arm64-halt-guard.patch`

## 1. The patch applies cleanly to a pristine pin checkout

```
$ git -C ~/dh2/third_party/dynarmic apply --check \
    /mnt/c/Users/noamc/OneDrive/Desktop/DH2Work/patches/dynarmic/0001-dh2work-t32-armv8-and-arm64-halt-guard.patch
apply --check OK
```

Applied to a fresh copy of the pristine tree, the result is byte-identical to the source
under test:

```
$ rm -rf /tmp/dynarmic-applycheck && cp -a ~/dh2/third_party/dynarmic /tmp/dynarmic-applycheck
$ git -C /tmp/dynarmic-applycheck apply .../0001-dh2work-t32-armv8-and-arm64-halt-guard.patch
applied to fresh pin checkout: OK
$ diff -r --exclude=.git ~/dh2/dynarmic-custom/src /tmp/dynarmic-applycheck/src
src trees identical: OK
```

Patch stat:

```
 src/dynarmic/CMakeLists.txt                   |   1 +
 src/dynarmic/backend/arm64/a32_address_space.cpp       |   6 +-
 src/dynarmic/frontend/A32/decoder/thumb32.inc |  20 +++
 src/dynarmic/frontend/A32/translate/impl/a32_translate_impl.h   |  14 ++
 src/dynarmic/frontend/A32/translate/impl/thumb32_crc32.cpp      |  77 ++++++++++
 src/dynarmic/frontend/A32/translate/impl/thumb32_load_store_dual.cpp | 144 ++++++++++++++++++
 6 files changed, 261 insertions(+), 1 deletion(-)
```

## 2. Clean x86_64 RelWithDebInfo build — zero errors

```
$ rm -rf ~/dh2/build-patched
$ cmake -S ~/dh2/dynarmic-custom -B ~/dh2/build-patched -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON -DDYNARMIC_TESTS=OFF \
    -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF
...
-- Generating done
-- Build files have been written to: /home/nac/dh2/build-patched
configure exit 0
$ ninja -C ~/dh2/build-patched
...
[228/229] Building CXX object .../emit_x64_floating_point.cpp.o
[229/229] Linking CXX static library src/dynarmic/libdynarmic.a
build exit 0
-rw-r--r-- 1 nac nac 77098378 ... /home/nac/dh2/build-patched/src/dynarmic/libdynarmic.a
```

## 3. Clean x86_64 Debug build — zero errors

```
$ rm -rf ~/dh2/build-patched-debug
$ cmake -S ~/dh2/dynarmic-custom -B ~/dh2/build-patched-debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON -DDYNARMIC_TESTS=OFF \
    -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF
configure exit 0
$ ninja -C ~/dh2/build-patched-debug
[229/229] Linking CXX static library src/dynarmic/libdynarmic.a
build exit 0
-rw-r--r-- 1 nac nac 120695040 ... /home/nac/dh2/build-patched-debug/src/dynarmic/libdynarmic.a
```

## 4. Encodings come from the real assembler

`build_and_run.sh` assembles `tests/t32_armv8_seq.s` with
`clang --target=thumbv8a-none-none-eabi`; `gen_seq_inc.py` turns the object and its
disassembly into the byte table the test loads. The generated listing:

```
       0: e8d7 5f8f    	ldab	r5, [r7]
       4: e8d7 5f9f    	ldah	r5, [r7]
       8: e8d7 5fef    	ldaex	r5, [r7]
       c: e8d7 5fcf    	ldaexb	r5, [r7]
      10: e8d7 5fdf    	ldaexh	r5, [r7]
      14: e8d7 56ff    	ldaexd	r5, r6, [r7]
      18: e8c7 5f8f    	stlb	r5, [r7]
      1c: e8c7 5f9f    	stlh	r5, [r7]
      20: e8c7 5fe9    	stlex	r9, r5, [r7]
      24: e8c7 5fc9    	stlexb	r9, r5, [r7]
      28: e8c7 5fd9    	stlexh	r9, r5, [r7]
      2c: e8c7 56f9    	stlexd	r9, r5, r6, [r7]
      30: fac9 f587    	crc32b	r5, r9, r7
      34: fac9 f597    	crc32h	r5, r9, r7
      38: fac9 f5a7    	crc32w	r5, r9, r7
      3c: fad9 f587    	crc32cb	r5, r9, r7
      40: fad9 f597    	crc32ch	r5, r9, r7
      44: fad9 f5a7    	crc32cw	r5, r9, r7
      48: e8d7 afef    	ldaex	r10, [r7]
      4c: e8d7 afcf    	ldaexb	r10, [r7]
      50: e8d7 afdf    	ldaexh	r10, [r7]
      54: e8d7 abff    	ldaexd	r10, r11, [r7]
```

## 5. Patched tree — per-instruction semantic results

```
$ tests/build_and_run.sh --dynarmic ~/dh2/dynarmic-custom --build ~/dh2/test-patched
```

```
T32-SELFTEST crc-reference PASS
T32-INSN ldab PASS
T32-INSN ldah PASS
T32-INSN ldaex PASS
T32-INSN ldaexb PASS
T32-INSN ldaexh PASS
T32-INSN ldaexd PASS
T32-INSN stlb PASS
T32-INSN stlh PASS
T32-INSN stlex PASS
T32-INSN stlexb PASS
T32-INSN stlexh PASS
T32-INSN stlexd PASS
T32-INSN crc32b PASS
T32-INSN crc32h PASS
T32-INSN crc32w PASS
T32-INSN crc32cb PASS
T32-INSN crc32ch PASS
T32-INSN crc32cw PASS
T32-SUMMARY mode=patched pass=18 fail=0
```

Exit code 0. The test asserts, per instruction: the destination register or the stored
memory contents, and for the exclusive stores that the status register reads 0. The CRC
cases are compared against a reference reflected-CRC implementation pinned to the
published `"123456789"` check values (`0xCBF43926` ISO, `0xE3069283` Castagnoli); the
self-check line is `T32-SELFTEST crc-reference PASS`.

## 6. A/B — the same bytes are undefined on the pristine pin

```
$ tests/build_and_run.sh --dynarmic ~/dh2/third_party/dynarmic \
    --build ~/dh2/test-pristine --expect-undefined
```

```
T32-SELFTEST crc-reference PASS
T32-AB ldab PASS
T32-AB ldah PASS
T32-AB ldaex PASS
T32-AB ldaexb PASS
T32-AB ldaexh PASS
T32-AB ldaexd PASS
T32-AB stlb PASS
T32-AB stlh PASS
T32-AB stlex PASS
T32-AB stlexb PASS
T32-AB stlexh PASS
T32-AB stlexd PASS
T32-AB crc32b PASS
T32-AB crc32h PASS
T32-AB crc32w PASS
T32-AB crc32cb PASS
T32-AB crc32ch PASS
T32-AB crc32cw PASS
T32-SUMMARY mode=pristine pass=18 fail=0
```

Exit code 0. In this mode each test loads exactly one of the same 4-byte encodings into
Thumb state and asserts `ExceptionRaised(pc, Exception::UndefinedInstruction)`, with no
other exception, no interpreter fallback and no SVC. Every added encoding therefore takes
the `thumb32_UDF()` path on the pinned revision.

## 7. The arm64 halt-guard hunk was not independently compiled here

The guard change is in the **arm64** backend
(`src/dynarmic/backend/arm64/a32_address_space.cpp`), which the required x86_64 build
does not compile. Two attempts to compile that backend on this machine failed for reasons
unrelated to the patch:

* `-DARCHITECTURE=arm64` with the host x86_64 compiler configures
  (`-- Target architecture: arm64`) but cannot compile oaknut's inline aarch64 assembly
  (`oaknut/code_block.hpp`: `dsb ish`, `isb`, `ic ivau`), and several unrelated arm64
  sources fail first (e.g. `backend/arm64/address_space.cpp:30`,
  `emit_arm64_memory.cpp` `FakeCall::call_pc`).
* Cross-compiling with the NDK r29 toolchain stops at
  `Could NOT find Boost (missing: Boost_INCLUDE_DIR)` because the NDK sysroot hides the
  host headers; pointing it at the host Boost then fails inside
  `boost/container_hash/hash.hpp` because the host Boost uses `std::unary_function`,
  which the NDK libc++ removes at C++20.

The hunk itself only extends an `if` condition with `&& !conf.check_halt_on_memory_access`;
the identical condition is compiled by the x86_64 build in
`backend/x64/a32_interface.cpp`, and the A64 arm64 backend
(`backend/arm64/a64_address_space.cpp`) carries it verbatim. It could not be verified
beyond that on this machine.

## 8. Also not verified

* No phone and no emulator were used (off-device policy).
* The acquire/release *ordering* is architectural: it comes from Dynarmic's global
  exclusive monitor callbacks. The test is single-threaded, so it asserts the
  data/status results, not an observed memory-ordering effect.
* No end-to-end guest boot (that is the host workstream's gate, not this patch set's).
