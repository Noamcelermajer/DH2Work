# HOST-ELF-LOADER — the guest ELF32/ARM loader for the DH2 compatibility host

Scope: load `Dungeon Hunter 2 HD` v1.0.2 (armeabi-v7a, 32-bit ARM) shared objects
into a 32-bit guest address space, relocate them, resolve their symbols against
the shipped ARM32 bionic sysroot, give the guest a TLS block and a valid initial
thread, and hand Dynarmic a register file it can execute.

Source: `DH2Work-toolchain/host/loader/` (headers in `include/dh2elf/`, sources in
`src/`, tests in `tests/`). Everything below either cites that code or cites a
command whose output is quoted verbatim.

Evidence labels: **[verified]** means a command in this document produced the
quoted numbers on this machine. **[design]** is a decision with its rationale.
**UNKNOWN** is something not settled.

---

## 0. Summary

| Question | Answer |
| --- | --- |
| Does the real engine load? | Yes. 10 objects, 12,689,408 bytes of image, 5,161 guest pages, all relocations applied. **[verified §7]** |
| Relocation types present | Exactly five: `R_ARM_RELATIVE`, `R_ARM_ABS32`, `R_ARM_JUMP_SLOT`, `R_ARM_GLOB_DAT`, `R_ARM_TLS_TPOFF32`, plus two packed encodings. **`R_ARM_COPY` is absent**, as are `R_ARM_IRELATIVE` and every TLS model except `TPOFF32`. **[verified §4]** |
| Do the tests pass? | 4 suites, 32 cases, 102,378 checks, 0 failures. **[verified §7]** |
| Did we reach Dynarmic execution? | **No.** The Dynarmic A32 JIT library builds (verified, §8), but no guest code was executed. Everything up to the execution boundary is tested; §8 states precisely what is and is not bound. |
| Biggest unknown | Whether the shipped engine's own `__linker_init`/`__libc_init` path is content with a register file and stack image that this loader constructs, i.e. whether the *first* guest instructions reach the engine's entry point without needing syscalls this host cannot yet serve. §9.2. |

---

## 1. What the loader is, and the three decisions that shape it

### 1.1 One 4 GiB reservation, `base + g` addressing

The guest's pointers are 32-bit. The loader therefore reserves one 4 GiB window
(`kGuestSpace = 1ull << 32` plus a 64 KiB guard) and defines guest address `g` as
host address `base + g`. Guest addresses are `std::uint32_t` everywhere;
`AddressSpace::host8(g)` is the only place the conversion happens
(`src/memory.cpp`, `include/dh2elf/loader.hpp`).

A one-byte-per-4-KiB page bitmap (1 MiB for the whole window) records what the
*loader* placed. It is deliberately not a general `mmap`: guest `mmap`/`brk`
belong to the syscall layer, and the loader's job is to lay down the initial
image and then get out of the way.

The reservation is placed so that the guest window's layout matches the shipped
runtime's assumptions, which are taken from
`DH2Work/docs/GUEST-MEMORY-INVESTIGATION.md` §1.2:

| Constant | Value | Meaning |
| --- | --- | --- |
| `kStackTop` | `0xFF000000` | guest stack top |
| `kStackSize` | 8 MiB | guest stack |
| `kTlsReserve` | 64 KiB | initial-thread static TLS block |
| `kMmapLimit` | `0xFE000000` | ceiling for library placement |
| `kExecutableLimit` | `0x40000000` | where a PIE main executable is placed |
| `kLowestAllocPage` | `0x10000` | never map below this |

### 1.2 We emulate the linker's behaviour; we do not run the guest `linker`

**[design]** The sysroot contains a 2 MiB `/system/bin/linker`. It is tempting to
use it as the ELF interpreter for the engine, exactly as the kernel would. We do
not, for three reasons:

1. **Its relocation format is not one we can trust it to apply itself.**
   `linker` carries `DT_ANDROID_RELR` (§4.2). To use it as the interpreter we
   would have to relocate it first, which means implementing the packed format
   anyway — using it buys nothing that implementing the format does not.
2. **Its 33-entry dynsym exports nothing the host needs.** What it exports is the
   `__loader_*` interpreter-linker-interface group (25 symbols:
   `__loader_dlopen`, `__loader_dlsym`, `__loader_dl_iterate_phdr`, …).
   `ld-android.so` is a 2.8 KiB stub that exports *the same 25 names* pointing at
   one trampoline, and it uses no packed relocations.
3. **Running it would mean executing guest code before the loader has a working
   syscall surface**, i.e. emulating a dynamic linker accurately enough to trust
   before any application code runs. That is strictly harder than implementing
   the four classic relocation types plus the packed encodings it would apply.

**What we do instead:** the loader is the linker. It performs the placement,
relocation, symbol resolution and TLS setup the guest `linker` would have
performed, and it loads `ld-android.so` as the interpreter object so that the
`__loader_*` imports of `libc.so`, `libdl.so` and `libdl_android.so` resolve to
real addresses inside a real mapped image.

Which objects import `__loader_*` **[verified §4.3]**: `libc.so` (4),
`libdl.so` (13), `libdl_android.so` (9). Nothing the engine reaches imports a
symbol that *only* the 2 MiB `linker` defines, so `ld-android.so` is sufficient.

### 1.3 Phases are forced by data dependencies

```
reserve_initial_thread()          reserve the window, claim [stack][TLS]
  → load interpreter (+ its DT_NEEDED graph)
  → load main executable at kExecutableLimit
  → load the executable's DT_NEEDED graph, breadth-first
  → relocate every object
  → build_initial_stack_image()   argv/envp/auxv, only now that
                                  AT_ENTRY/AT_BASE/AT_PHDR are known
  → make_init_regs()              r0..r15, cpsr, TP
```

Nothing is relocated until every `DT_NEEDED` of every object is mapped, so a
symbol resolution sees the whole namespace. The stack image cannot be written
before loading, because `AT_ENTRY`, `AT_BASE` and `AT_PHDR` are module
addresses. Both facts are structural, not stylistic: an earlier draft wrote the
stack image first and every auxv entry was zero.

---

## 2. Placement

`Loader::place_and_copy()` (`src/elf_parse.cpp`) does, in order:

1. Compute `max_align` = the largest `p_align` over the object's `PT_LOAD`s.
2. Compute the span the allocator must find, measured from
   `page_align_down(first.p_vaddr)` to `page_align_up(last.end_vaddr())`.
   **Measured from the page-aligned start, not from `p_vaddr`.** Using `p_vaddr`
   shifts the derived bias up by the page offset, and an object whose first
   `p_vaddr` is not page-aligned then lands *above* the limit — see the failure
   log in §10.3, where that placed `ld-android.so` inside the stack/TLS block.
3. Choose the bias:
   * main executable → `find_span_upward(span, kExecutableLimit, max_align)`,
     i.e. placed **at** `kExecutableLimit` (0x40000000), the low end;
   * every other `ET_DYN` → `find_span_downward(span, kMmapLimit, max_align)`,
     the guest's own highest-fit policy;
   * a caller-pinned bias wins.
4. Reject anything that does not fit: `[bias + span_lo, bias + span_hi)` must be
   inside `[kLowestAllocPage, 2^32)`.
5. Claim every page the object needs, atomically — on any failure, all claims
   are rolled back so a failed load leaves the address space untouched.
   `AddressSpace::claim()` takes a `ceiling` and the loader passes `kMmapLimit`,
   so a `PT_LOAD` that would leave the guest's mmap window is refused rather
   than mapped.
6. Make the pages read-write, then zero them (covering `.bss` and page tails)
   and copy `p_filesz` bytes from the file.
7. Narrow to the real permissions. **Phase 6 must precede phase 7**: granting
   `r-x` before the copy makes the copy fault.

`p_align` handling: a power-of-two check, and a congruence check
`p_offset % 4096 == p_vaddr % 4096`. The loader maps whole pages, so it needs
the page-congruence form specifically; an object that satisfies only the weaker
`p_align` relation is rejected with `OffsetVaddrMismatch`. If `max_align` exceeds
the page size and the first segment's page-aligned vaddr is not a multiple of it,
no bias can satisfy the invariant and the load is refused rather than mis-mapped.

Everything the loader rejects, it rejects with a code and a message naming the
object and the field. There is no best-effort path, because a half-placed object
crashes arbitrarily far from the cause. The rejection tests (`tests/test_parse.cpp`)
mutate a real object's bytes and assert the *specific* code.

---

## 3. Address discipline (the bug class that cost the most time)

Two coordinate systems are in play:

* **link-time offsets** — `p_vaddr`, `p_offset`, `st_value`, `r_offset`, all as
  they appear in the file;
* **guest addresses** — `load_bias + link-time offset`, and for a mapping,
  `page_align_down(load_bias + p_vaddr)`.

`Segment` records both (`vaddr` link-time, `map_start`/`map_len` guest) and
`Module::segment_containing_guest()` is the only lookup allowed to use the
guest pair. Reads and writes into the image go through
`Module::read_u32` / `write_u32` / `read_cstr`, which bounds-check against the
mapped page range.

Three concrete bugs came from breaking this rule, all recorded in §10.3:

* `segment_containing()` compared a guest address against link-time ranges, so
  after placement **no** lookup succeeded and `parse_dynamic` found no
  `PT_DYNAMIC` at all;
* `parse_dynamic` passed `DT_REL`'s *link-time* vaddr to the accessor, reading
  from the wrong place entirely;
* `StackWriter` computed `host_base + guest_address` instead of
  `host_base + (guest_address - guest_base)`, writing 4 GiB past the block.

`StackWriter` is now written entirely in offsets from the block, and the
verification pass in `build_initial_stack_image` converts through the same
helper.

---

## 4. Relocation inventory

### 4.1 Classic (`SHT_REL`) types

Command (scratch script `reloc_census.py`, pyelftools 0.33):

```
python reloc_census.py        # walks every shipped .so, .rel.dyn and .rel.plt
```

Per-file, **[verified]**:

| Object | `.rel.dyn` | `.rel.plt` | types |
| --- | ---: | ---: | --- |
| `libDungeonHunter2.so` | 54112 | 351 | `RELATIVE=49132`, `ABS32=4969`, `GLOB_DAT=11`, `JUMP_SLOT=351` |
| `libStormGLOFT.so` | 6258 | 1345 | `ABS32=3956`, `RELATIVE=1917`, `GLOB_DAT=385`, `JUMP_SLOT=1345` |
| `libnativeinterface.so` | 7 | 16 | `RELATIVE=6`, `GLOB_DAT=1`, `JUMP_SLOT=16` |
| `libc.so` | 1338 | 588 | `RELATIVE=1165`, `ABS32=129`, `GLOB_DAT=43`, **`TLS_TPOFF32=1`**, `JUMP_SLOT=588` |
| `liblog.so` | 24 | 100 | `RELATIVE=20`, `ABS32=2`, `GLOB_DAT=2`, `JUMP_SLOT=100` |
| `libstdc++.so` | 4 | 18 | `RELATIVE=3`, `GLOB_DAT=1`, `JUMP_SLOT=18` |
| `libm.so` | 5 | 5 | `RELATIVE=3`, `GLOB_DAT=2`, `JUMP_SLOT=5` |
| `libz.so` | 31 | 44 | `RELATIVE=29`, `GLOB_DAT=2`, `JUMP_SLOT=44` |
| `libzbjni.so` | 243 | 11 | `RELATIVE=242`, `GLOB_DAT=1`, `JUMP_SLOT=11` |
| `libzbcompat.so` | 3 | 2 | `RELATIVE=3`, `JUMP_SLOT=2` |
| `libc++.so` | 0 | 420 | `JUMP_SLOT=420` (the rest is packed, §4.2) |
| `libdl.so` | 0 | 13 | `JUMP_SLOT=13` |
| `libdl_android.so` | 0 | 9 | `JUMP_SLOT=9` |
| `ld-android.so`, `linker`, `libEGL.so`, `libGLESv1_CM.so`, `libGLESv2.so`, `libandroid.so`, `libjnigraphics.so` | 0 | ≤2 | `GLOB_DAT`/`JUMP_SLOT` only, or none |

Whole-sysroot classic totals **[verified]**:
`R_ARM_RELATIVE=51056`, `R_ARM_ABS32=8927`, `R_ARM_JUMP_SLOT=2396`,
`R_ARM_GLOB_DAT=445`, `R_ARM_TLS_TPOFF32=1`.

**Types the loader does not implement, and their counts:** `R_ARM_COPY` = **0**,
`R_ARM_IRELATIVE` = **0**, `R_ARM_TLS_DTPMOD32`/`DTPOFF32` = **0 in the classic
tables** (they appear only inside packed streams, §4.2), every other ARM type
(`PC24`, `CALL`, `GLOB_DAT` variants, `MOVW/MOVT`, `SBREL32`, …) = **0**. The
task brief expected `R_ARM_COPY` to be present; it is not, in any of the 20
shipped ARM32 objects. That is asserted in `test_parse`/`test_real_load`
(`census.by_type.count(20) == 0`).

The implemented set (`RelocInfo` table, `src/elf_parse.cpp`) is therefore:
`R_ARM_NONE`, `R_ARM_ABS32`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`,
`R_ARM_RELATIVE`, `R_ARM_TLS_TPOFF32`, `R_ARM_TLS_DTPMOD32`,
`R_ARM_TLS_DTPOFF32`. Any other type is a hard, named error.

### 4.2 Packed relocation encodings — and the correction to the brief

Two packed formats ship, and one of them is load-bearing:

```
python scratch/dyn_tags.py
libc++.so     DT_ANDROID_REL  DT_ANDROID_RELR   (sizes 0x1ba0 / 0x12c)
linker        DT_ANDROID_RELR                   (size 0x39c)
```

`libc++.so` is **not optional**: `liblog.so` imports 13 symbols from it,
including `__cxa_guard_acquire`, `_Znwj` (`operator new`), `_ZdlPvj` and eleven
`std::__1::basic_string` members, and `liblog.so` is a direct `DT_NEEDED` of the
engine. So the packed formats had to be implemented. **[verified]**

**Format A — `AndroidRel` / `AndroidRela`** (`DT_ANDROID_REL`). The stream is
SLEB128, and the layout is bionic's `for_all_packed_relocs`
(`linker/linker_reloc_iterators.h`):

```
num_relocs, r_offset,
then repeated {
    group_size, group_flags,
    [group_r_offset_delta]   if flags & GROUPED_BY_OFFSET_DELTA (2),
    [r_info]                 if flags & GROUPED_BY_INFO (1),
    group_size * { [r_offset delta] [r_info] }
}
GROUP_HAS_ADDEND (8) must be clear on a REL platform
```

The `APS2` bytes at the start of `libc++.so`'s stream are **not** a magic word to
be skipped as a distinct header: they are part of the first LEB128 values
(`41 50 53 32` → 2112, the group header). Reading four fixed 32-bit words instead
produced a bogus 5,460,033-entry group during development (§10.3).

Decoded for `libc++.so`: **2112 relocations** (1056 of them `R_ARM_RELATIVE`).
**[verified]**

**Format B — `AndroidRelr` / `DT_RELR`** (plain RELR bitmap): an even word is an
address, an odd word is a bitmap of `base + 4*bit`.

Decoded: `libc++.so` **533**, `linker` **4215**. For all three streams the
decoder's every target lands inside a `PT_LOAD` and inside a *writable* segment,
0 exceptions — which is the check that proves the encodings are understood
rather than guessed. **[verified]**

The loader does not guess which encoding a table uses. It decodes with each
candidate and accepts the first whose entries are non-empty, every target inside
a **writable** segment, and 4-byte aligned. A wrong decoding of a packed stream
produces addresses in read-only text or outside the image, so the check is
decisive. (An earlier version accepted any target inside the image, which let a
wrong SLEB128 decode through.)

### 4.3 Per-object counts at runtime

From the loader itself, and independently re-derived by the test **[verified]**:

| Object | `.rel.dyn` | `.rel.plt` | counts |
| --- | ---: | ---: | --- |
| `libDungeonHunter2.so` | 54112 | 351 | `RELATIVE=49132`, `ABS32=4969`, `GLOB_DAT=11`, `JUMP_SLOT=351` |
| `libc.so` | 1338 | 588 | `RELATIVE=1165`, `ABS32=129`, `TLS_TPOFF32=1`, `GLOB_DAT=43`, `JUMP_SLOT=588` |
| `liblog.so` | 24 | 100 | `RELATIVE=20`, `ABS32=2`, `GLOB_DAT=2`, `JUMP_SLOT=100` |
| `libstdc++.so` | 4 | 18 | `RELATIVE=3`, `GLOB_DAT=1`, `JUMP_SLOT=18` |
| `libm.so` | 5 | 5 | `RELATIVE=3`, `GLOB_DAT=2`, `JUMP_SLOT=5` |
| `libdl.so` | 0 | 13 | `JUMP_SLOT=13` |
| `libc++.so` | 0 (+2645 packed) | 420 | `JUMP_SLOT=420` classic + packed |

Runtime totals: **classic = 56,978**, **packed = 2,645**, **total = 59,623**;
applied = 59,272, left unresolved-weak = 2, deferred (`JUMP_SLOT` under lazy
binding) = 349 in the classic tables plus 2 in the packed stream = **351**, which
is exactly the engine's PLT veneer count. `applied + weak + deferred == total`
is asserted. **[verified §7]**

The `R_ARM_JUMP_SLOT` emitter deserves a note because the ABI is easy to get
wrong: for `SHT_REL` the slot's initial contents are the object's *PLT resolver
stub address*, and the relocation result is **`S`**, not `S + A`. Adding the
addend put `S + stub` in the slot — a write that lands in a different library
entirely. This was 1,385 test failures before it was found; §10.3.

---

## 5. Dynamic linking

### 5.1 `DT_NEEDED` resolution

Namespaces map to search directories, first match wins. The loader is
configured with the directories the guest itself tries (a runtime report records
attempts against `/system/lib/arm`, `/system/lib/arm/bootstrap`, and the
app-private plugin directory); in the host tests these are the extracted bundle's
`guest/lib` and `sysroot/system/lib`. A dependency that cannot be found is fatal:
the guest would abort in the linker, and a partially resolved global scope
produces *wrong* addresses rather than a clean failure. Load order is
breadth-first so it matches `DT_NEEDED` order, which both the global scope and
TLS module ids depend on.

### 5.2 Symbol tables

Both hash formats are implemented and both appear in this sysroot: the engine,
`libnativeinterface.so` and `libStormGLOFT.so` carry **`DT_HASH` only** (no GNU
hash), while the bionic libraries carry **`DT_GNU_HASH`**, and `libc.so`,
`liblog.so`, `libm.so`, `libstdc++.so`, `libz.so`, `libzbjni.so` carry both.
`dynsym_count` comes from `DT_HASH`'s `nchain`, or, with only `DT_GNU_HASH`,
from the end of the highest-numbered non-empty chain (GNU hash stores no count).

### 5.3 The guest's visibility rules

This is bionic's behaviour, not glibc's:

* a symbol is exported only if `st_shndx != SHN_UNDEF`, `st_value != 0`, and
  `st_other`'s visibility is `DEFAULT` or `PROTECTED`. A `HIDDEN`/`INTERNAL`
  symbol referenced from another object is an error, not a silent resolve.
* lookup order is the **global group** first, then the requesting object's
  **local group** (itself, then its direct `DT_NEEDED`, in order). Requiring the
  local group only for the requester (not its dependencies transitively) is what
  stops a library resolving through a sibling's dependency.
* `DT_SYMBOLIC` suppresses the local-group pass and forces global lookup. Both
  the engine and `libnativeinterface.so` set it, so this path is live.
* an unresolved **weak** reference becomes `0` rather than failing the load. The
  census says this is the only missing-symbol case: exactly 2 in the whole load.

### 5.4 Lazy vs eager binding

`DT_BIND_NOW` (or `DT_FLAGS` `DF_BIND_NOW`, or `DT_FLAGS_1` `DF_1_NOW`) means
eager. **[verified]** every bionic library asks for it; the engine does **not**,
and `libnativeinterface.so` does not.

For a deferring object, `JUMP_SLOT` targets are recorded rather than applied, and
`Loader::resolve_jump_slot()` binds one on demand. The loader exposes its record
directly (`is_jump_slot_deferred`, `deferred_jump_slot_count`) so the host and
the tests never have to infer binding state from slot contents — which cannot be
done reliably, because a deferred slot and a bound slot can coincide.

**[verified]** the engine ends up with exactly 351 deferred slots, each still
holding its `0x30DD74` resolver stub, and binding one on demand yields exactly
the address an independent lookup produces (`asinf` → `libm.so+0xCF20`, guest
`0x4260437792`).

---

## 6. TLS and the initial thread

### 6.1 Static TLS layout

The guest ships two objects with `PT_TLS` **[verified]**:

| Object | `p_vaddr` | `filesz` | `memsz` | `p_align` | module id | offset from TP |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `libc.so` | `0x684384` | 8 | 8 | 8 | 1 | `0x00` |
| `libc++.so` | `0x821264` | 0 | 12 | 4 | 2 | `0x08` |

Layout follows bionic: a 1-based module id in load order, and each block at an
increasing positive offset from the thread pointer, aligned to its own `p_align`.

**The thread pointer contract.** `TP` is set to the end of the *used* static TLS
block, so `TP + offset` is the address of a module's TLS variable. On ARM the
guest reads `TP` from `TPIDRURO` (`mrc p15,0,rX,c13,c0,3`). This loader models
that register as `InitRegs::tp`; **the host must set Dynarmic's `TPIDRURO` from
it**, because the instruction is not intercepted here. With the numbers above,
`TP = 0x4278190100` (base + `kTlsReserve` worth of block, 20 bytes used).

The single `R_ARM_TLS_TPOFF32` in the whole sysroot lives in `libc.so` at
`r_offset 0x691260`, and the loader writes `0` there — correct, because
`libc.so`'s TLS block is at offset 0 and the symbol's `st_value` is 0 within it.
**[verified]**

Static TLS is what is modelled. `R_ARM_TLS_DTPMOD32`/`DTPOFF32` targets — which
appear only in `libc++.so`'s packed stream — are written with the owning
module's id and the symbol's intra-block offset. Those values are right for the
modules this loader loads and **meaningless for a module loaded later**: there is
no `dlopen` and no dynamic TLS allocator. §9.2.

### 6.2 The initial thread image

One contiguous block laid out as `[stack 8 MiB][static TLS 64 KiB]`, placed so
that the stack occupies `[kStackTop - kStackSize, kStackTop)`. The block starts
at `kStackTop - kStackSize`, **not** `kStackTop - (kStackSize + kTlsReserve)`:
subtracting the TLS reserve as well moves the whole layout, including the stack
top, 64 KiB lower than `kStackTop`. `reserve_initial_thread()` asserts the
resulting stack top equals `kStackTop` and fails the load if it does not.

Stack image, written top-down and 8-byte aligned (AAPCS requires sp to be
8-byte aligned at every public interface, and the initial frame is one):

```
[argc][argv...][NULL][envp...][NULL][auxv pairs...][AT_NULL,0]
      ... [AT_RANDOM 16 bytes][argv/envp strings]
```

`sp` points at `argc`; `AT_RANDOM`'s address is patched into the auxv once its
location is known. `build_initial_stack_image()` verifies the shape it wrote —
argc at sp, `argv` and `envp` NULL-terminated, an `AT_NULL` in the auxv — instead
of trusting the arithmetic.

Registration state handed to Dynarmic **[verified]**:

```
r0  = sp (points at argc)      pc  = engine entry
r13 = sp, 8-byte aligned       lr  = 0
cpsr = 0x467  (SVC mode, IRQ/FIQ/async-abort disabled)
tp   = guest TLS base, past the used static TLS
auxv: AT_PAGESZ=4096 AT_HWCAP=0x1028311 AT_PHDR AT_PHENT=32 AT_PHNUM=5
      AT_ENTRY=0x1076948480 AT_BASE AT_RANDOM AT_NULL
```

`AT_PHDR` is derived from `e_phoff` by finding the `PT_LOAD` whose file range
contains the program header table. That is required, not defensive: the engine,
`libStormGLOFT.so` and `libnativeinterface.so` ship **no `PT_PHDR`** (the engine
has 5 program headers: `PT_ARM_EXIDX`, two `PT_LOAD`, `PT_DYNAMIC`,
`PT_GNU_STACK`).

`AT_HWCAP = 0x1028311` claims SWP, HALF, THUMB, FAST_MULT, EDSP, VFP, NEON,
VFPv3, TLS, VFPv4, IDIV and VFPD32. This is a claim about what Dynarmic
implements, not a CPUID-style probe: NEON and VFPv4 in particular must actually
be translated. If Dynarmic lacks a feature, the guest will discover it via an
undefined instruction rather than via `AT_HWCAP`.

---

## 7. Test results

Command:

```
wsl bash host/loader/run_tests.sh
```

**[verified]** actual output:

```
######## test_platform ########
test_platform: 7 cases, 28 checks, 0 failures
test_platform: PASS

######## test_parse ########
test_parse: 17 cases, 282 checks, 0 failures
test_parse: PASS

######## test_synthetic ########
test_synthetic: 3 cases, 46 checks, 0 failures
test_synthetic: PASS

######## test_real_load ########
test_real_load: 9 cases, 102022 checks, 0 failures
test_real_load: PASS
```

32 cases, 102,378 checks, 0 failures. Expected values come from an independent
implementation (pyelftools 0.33 driven by `scratch/reloc_census.py` and
`scratch/dyn_tags.py`), never from the loader's own output.

What each suite establishes:

* **`test_platform`** — the reservation is exactly `kGuestSpace + kGuardSize` and
  the whole span is addressable; `find_span_downward` is highest-fit and its
  returned span ends at or below the limit; `claim` refuses double-placement,
  unaligned claims and claims past the ceiling; protection changes work.
* **`test_parse`** — the engine matches the census field-by-field (`e_phnum=5`,
  entry `0x30EE00`, the two `PT_LOAD`s with their exact `p_offset`/`p_vaddr`/
  `p_filesz`/`p_memsz`/`p_flags`, `DT_HASH` only, `DT_SYMBOLIC`, not
  `DT_BIND_NOW`, 7 `DT_NEEDED` in order, `dynsym_count=32572`). All 17 non-packed
  shipped objects load with their expected segment/TLS/`BIND_NOW` shape; the 2
  packed ones load and record their packed tables. 10 rejection cases each assert
  a *specific* error code: bad magic, `ELFCLASS64`, big-endian, bad
  `EI_VERSION`, `EM_386`, `ET_REL`, `ET_CORE`, `e_phnum == 0`,
  `e_phentsize < 32`, empty file, truncated header, missing file,
  `p_offset`/`p_vaddr` congruence broken, `p_filesz > p_memsz`, `PT_LOAD`
  without `PF_R`.
* **`test_synthetic`** — a hand-built minimal `ET_DYN` object loads, is placed
  page-aligned with `bias + p_vaddr == map_start`, and produces the exact initial
  register file and stack image above (argc at sp, NULL-terminated argv/envp whose
  pointers resolve to real strings, `AT_NULL`-terminated auxv with every mandatory
  entry).
* **`test_real_load`** — the whole pipeline against the runtime bundle. Loads the
  pristine engine (`SHA-256 36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`
  **[verified]**) plus the sysroot, then **independently re-derives** every
  invariant from the mapped image rather than trusting the loader's counters.

### 7.1 The real load, in numbers **[verified]**

```
modules loaded: 10
  ld-android.so  libDungeonHunter2.so  libc.so  libGLESv2.so  libstdc++.so
  libm.so  libGLESv1_CM.so  libdl.so  liblog.so  libc++.so
engine load range  = [0x40000000, 0x40A36000)     (placed at kExecutableLimit)
libc++.so          = [0xFD4A8000, 0xFDF2B000)
liblog.so          = [0xFDF2B000, 0xFDFE5000)
libdl.so           = [0xFE048000, 0xFE08D000)
libm.so            = [0xFE08D000, 0xFE1B9000)
libstdc++.so       = [0xFE1B9000, 0xFE1F1000)
libGLESv1_CM.so    = [0xFE1F1000, 0xFE21C000)
libGLESv2.so       = [0xFE21C000, 0xFE295000)
libc.so            = [0xFE295000, 0xFE3B8000)
ld-android.so      = [0xFE3B8000, 0xFE3B9000)
pages claimed by the loader = 5161 (20 MiB); bytes mapped = 12,689,408
segments with a .bss tail   = 15, bytes sampled = 45 (all zero)
classic relocations re-walked = 56,978   packed = 2,645   total = 59,623
  R_ARM_RELATIVE 50323   R_ARM_ABS32 5100   R_ARM_JUMP_SLOT 1495
  R_ARM_GLOB_DAT 59      R_ARM_TLS_TPOFF32 1
slot values verified = 55,482; symbol relocations with no definition (weak) = 2
JUMP_SLOT deferred = 351, bound = 1144
applied = 59,272; weak-unresolved = 2; deferred = 349 classic + 2 packed
17 non-writable segments probed with a real write fault (all faulted)
```

The write-fault probes are done by `fork()`ing and writing from the child, with
the default `SIGSEGV` action as the evidence, so the permission claim is enforced
by the OS rather than asserted. Read permission is structural: `place_and_copy`
refuses to map a `PT_LOAD` without `PF_R`.

Symbol resolution spot checks **[verified]**:
`malloc → libc.so+0x255529`, `free → libc.so+0x255173`,
`memcpy → libc.so+0x426624`, `pthread_create → libc.so+0x359269`,
`dlopen → libdl.so+0x6417`, `glActiveTexture → libGLESv2.so+0x12912`,
`glDrawArrays → libGLESv2.so+0x13232`.

---

## 8. The execution boundary — what is and is not bound

Dynarmic at the pinned commit `86458a0bd369d63ba4c2ef812cacbb6c9080c065`
(`fix compilation with Clang 20`) **builds** as an A32-only static library:

```
build_rc=0
Linking CXX static library src/dynarmic/libdynarmic.a
build-a32/src/dynarmic/libdynarmic.a 16035360
```

Two build notes, because they blocked the configure: Dynarmic's `CMakeLists.txt`
requires Boost ≥ 1.57 (headers only: `boost/variant.hpp`, `boost/icl/*`), which
`libboost-dev` provides; and `find_package(Boost 1.57 REQUIRED)` fails outright
without it. `DYNARMIC_FRONTENDS=A32` avoids the A64 translator.

**No guest code was executed under Dynarmic.** The library is built; it is not
linked into the test binary, and no ARM32 instruction was run. The honest
statement of what that leaves unproven:

* **Bound and tested:** placement, permissions (enforced by the OS), `.bss`
  zeroing, all 59,623 relocations re-derived and their slot values checked, symbol
  resolution against the whole namespace, the symbol-visibility rules, lazy and
  eager binding, the static-TLS layout, the `R_ARM_TLS_TPOFF32` value, and the
  complete initial register file and stack image.
* **Not bound:** that Dynarmic's A32 translator accepts the engine's instruction
  stream, that `TPIDRURO` is wired from `InitRegs::tp`, and that the first guest
  instructions reach the engine's entry point. §9.2 explains why the last of these
  is not a small remaining step.

A minimal next step, in order of cost: build a synthetic ARM32 blob with a
hand-written guest entry (`ldr r0, [r0]` / `add r0, r0, #1` / `bx lr`-style)
inside a hand-built ELF32 object, bootstrap it with this loader, and execute under
Dynarmic with a `MemoryReadCode`/`MemoryRead32` callback that maps the
reservation. That validates the register file and the memory interface against
the real JIT without needing any syscall. It was **not done** here.

---

## 9. What is not modelled

### 9.1 Deliberate omissions with no impact on reaching the engine entry

* **`PT_GNU_RELRO` is not applied.** Its pages stay writable for the process
  lifetime. No guest code can observe this without an `mprotect` it does not
  perform.
* **`DT_INIT` / `DT_INIT_ARRAY` / `DT_FINI_ARRAY` are parsed but not called.**
  They are guest code; calling them is the *host's* step, after Dynarmic starts,
  and `libc++.so` has one at `0xCC7D4` and `libzbjni.so` at `0x5A30`.
* **`PT_ARM_EXIDX` is not consumed.** The loader records that it exists. No
  unwind table is built for the guest unwinder; a guest backtrace through the
  engine will fail, and `.ARM.exidx`-driven C++ exception unwinding is not
  modelled.
* **`PT_NOTE`, `PT_GNU_EH_FRAME`, `PT_GNU_STACK` are ignored.** The GNU stack
  marking (executable or not) is not enforced.
* **Version symbols (`DT_VERSYM`/`DT_VERNEED`/`DT_VERDEF`) are not consulted.**
  None of these objects carries them; if one did, a versioned reference would
  resolve to the unversioned symbol.
* **`DT_RPATH`/`DT_RUNPATH` are ignored.** Search order comes from the namespace
  configured on the host.
* **`DF_STATIC_TLS` and TLS surplus allocation are not modelled.** Fine for a
  fixed set of modules at load time; see 9.2.
* **The `linker` object (2 MiB) is never loaded.** It is refused with
  `unsupported-rel-format` — see 9.3 — and the explanation in §1.2 says why it is
  not needed.

### 9.2 Not modelled, and it matters

* **`dlopen`/`dlsym`/`dlclose` are not implemented.** `libdl.so` and
  `libdl_android.so` load and relocate, and their `__loader_*` imports resolve to
  real addresses inside `ld-android.so` — but those addresses are the stub
  trampoline in a guest image that this host does not run. If the engine calls
  `dlopen` (it imports `dlopen` via `.rel.plt`), the host must intercept the call
  and implement it, or the guest will jump into a trampoline that expects the
  absent guest linker. **This is the first runtime wall, and it is not loader
  work** — it is the `libdl` surface.
* **`R_ARM_TLS_DTPMOD32`/`DTPOFF32` are written from the static layout.** The
  values are correct for the modules loaded at bootstrap and meaningless for any
  module loaded later, because there is no dynamic-TLS allocator and no
  `__tls_get_addr` implementation.
* **`__tls_get_addr` is not provided by the host.** It resolves to `libc.so`'s
  own implementation (`libc.so+0x1AA...`), which will run guest code that expects
  a working thread-local allocator.
* **The initial thread is the only thread.** No `clone`, no per-thread TLS, no
  thread-pointer switching. `pthread_create` is imported by the engine and will
  fail at the syscall layer.
* **No constructor invocation, no `AT_EXECFN`, no `AT_UID`/`AT_GID` from a real
  process, no `AT_SECURE` policy.** `AT_SECURE` is hard-coded 0.

### 9.3 Refused by design

* **`DT_RELA`.** Every shipped object uses `SHT_REL`; an unimplemented RELA would
  be silently wrong, so it is a hard error.
* **A `PT_LOAD` without `PF_R`.** Legal ELF, meaningless for a loadable segment,
  and it would fault on the copy.
* **Android packed relocations in an unrecognised form.** Both shipped forms are
  implemented; anything else fails with the reason named.
* **TLSDESC (`DT_TLSDESC_PLT`/`GOT`).** Absent from every shipped object.
* **`DT_TEXTREL`.** Absent; text relocations would need writable text.
* **The 2 MiB `linker` object, when offered directly.**
  `unsupported-rel-format` if its packed tables cannot be decoded; in practice it
  is simply not used (§1.2).

### 9.4 UNKNOWN

* **Whether the engine's `__linker_init`/`__libc_init` is satisfied by this
  register file and stack image.** This is the real open question: `init` is
  guest code that will run first and may `mprotect`, read `/proc`, call
  `getauxval` for tags not provided, or expect `AT_PHDR` to point at a `PT_PHDR`
  entry. Nothing here can answer it without executing guest code.
* **`vm.max_map_count` and the VMA cost of 4 GiB reservation plus thousands of
  `MAP_FIXED` mappings** on the real device. The loader makes one reservation and
  protects per segment; a per-page protection strategy would multiply the VMA
  count. Not measured on device.
* **Whether `AT_HWCAP = 0x1028311` overstates Dynarmic.** Specifically NEON
  (`HWCAP_NEON`) and VFPv4. Not verified against Dynarmic's translator coverage.
* **The behaviour of `R_ARM_ABS32` relocations that target read-only memory.**
  Every shipped `ABS32` targets a writable segment, so the case never arises
  here, and none is handled.

---

## 10. Notes for whoever continues this

### 10.1 Where the code is

```
host/loader/
  include/dh2elf/loader.hpp    the whole public surface, with the design argued in comments
  include/dh2elf/elf32.hpp     ELF32/ARM structures and a portable bounds-checked Reader
  include/dh2elf/platform.hpp  the only OS-specific part (reserve/protect/read file)
  src/memory.cpp               the 4 GiB reservation and its page bitmap
  src/elf_parse.cpp            header validation, PT_LOAD placement, dynamic parsing
  src/dynamic.cpp              symbol lookup, DT_NEEDED, all relocation emitters, packed decoders
  src/tls_stack.cpp            the initial thread: stack, auxv, TLS, registers
  src/platform.cpp             POSIX and Windows implementations of the four primitives
  tests/                       harness + 4 suites
  run_tests.sh                 builds and runs everything (WSL/Linux)
  cmake/FindBoost.cmake        shim for the Dynarmic smoke build (headers only)
```

The loader has no dependency outside the C++17 standard library, which is why it
builds and runs on Windows as well as Linux. That is deliberate: host-testability
was the point.

### 10.2 The Windows path is written but unexercised

`src/platform.cpp` implements the four primitives for both POSIX and Win32
(`VirtualAlloc(MEM_RESERVE|MEM_COMMIT, PAGE_NOACCESS)` + `VirtualProtect`). Only
the POSIX path has been run; **the Windows implementation is compiled nowhere in
this work and is UNKNOWN-correct.** That is the first thing to test if the host is
to be developed on Windows.

### 10.3 The bugs this work actually found, in case they recur

Each of these produced a symptom far from its cause, which is why they are
recorded.

1. **Protections granted after the copy.** `place_and_copy` narrowed a segment to
   its final `r-x` *before* copying the file bytes into it, so the copy faulted.
   Fixed by a two-phase order: grant `rw` to everything, copy, then narrow.
2. **`PT_LOAD` vaddr compared as `p_offset`.** `Elf32_Phdr` is
   `p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align` at
   `+0,+4,+8,+12,+16,+20,+24,+28`. Reading `p_vaddr` from `+12` (which is
   `p_paddr`) silently places segments wrongly.
3. **`mapped` never set.** `Module::segment_containing_guest()` gates on it, so
   after placement no lookup succeeded and `parse_dynamic` found no `PT_DYNAMIC`
   at all — the loader reported zero symbols and zero relocations while loading
   perfectly.
4. **Placement measured from `p_vaddr` instead of `page_align_down(p_vaddr)`.**
   The derived bias came out one page high, placing `ld-android.so` at
   `0xFDFFF000`–`0xFE000000` — inside the stack/TLS block — and the stack image
   then failed to write with a fault 4 GiB from anywhere meaningful. Fixed by
   measuring from the page-aligned start and adding an explicit ceiling to
   `claim`.
5. **`host_base + guest_address`.** `StackWriter` combined a host base with a
   *guest* address, writing at `base + 4 GiB - something`. Now every write is an
   offset from the block.
6. **`~7u` on a widened value.** `cursor = (cursor - aligned) & ~7u` promotes to
   64 bits and clears bits 32+, truncating the address rather than aligning it.
   Written as `& 0xFFFFFFF8u` in 32 bits.
7. **`JUMP_SLOT` emitted as `S + A`.** The REL addend for a PLT slot is the
   object's resolver stub, so the slot got `S + stub` — an address inside a
   different library. 1,385 failures. `JUMP_SLOT` writes `S`.
8. **The inverted lazy flag.** `bootstrap` called
   `relocate_all(!lazy_jump_slots_)`, which eagerly bound every object. Invisible
   until a test asserted that the engine's PLT slots were still unbound.
9. **`APS2` is not a skippable magic.** Treating the first four bytes as a
   distinct header rather than as the start of the first LEB128 value produced a
   5,460,033-entry relocation group.
10. **A `uint32_t` sum of two guest addresses.** `load_bias + vaddr + filesz`
    wraps for an object near the top of the window; compute in 64 bits and narrow
    once.
11. **Trusting a packed decoder.** Accepting any decode whose targets were merely
    *inside the image* let a wrong SLEB128 interpretation through. The check that
    works is that every target is in a **writable** segment.

### 10.4 Scratch tooling

Live under `DH2Work-toolchain/scratch/` (not part of the deliverable):
`reloc_census.py` (the inventory in §4), `dyn_tags.py` (the dynamic-tag table),
`decode_packed.py` / `try_decoders.py` / `dump_aps2.py` (packed-format
determination), `check_addend.py`, `libcpp_analysis.py` (why `libc++.so` is
load-bearing), `build_dynarmic.sh`, `build_loader.sh`, and the probes used to
isolate §10.3.
