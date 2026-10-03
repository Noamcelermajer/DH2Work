# HOST-RUNTIME-LINKER-THREADS.md — guest library mode, the linker path, threads and TLS

Subsystem deliverable for the tailored host. Code: `DH2Work-toolchain/host/rt/`
(header `include/dh2rt/`, implementation `src/`, tests `tests/`, read-only analysis
tools `tools/`). Guest memory is `host/mem` (dh2mem), compiled in **unmodified**.
Host rule for bring-up: **Linux x86_64 in WSL2**, no device, no emulator.

Result in one line: the real closure — the pristine engine, `ld-android.so` and the
bionic sysroot — loads, relocates and resolves with **59 623 relocations applied,
0 unresolved**, and guest threads are created, given a bionic-shaped TLS block and
joined through the same `clear_child_tid` futex the guest's own `pthread_join`
waits on: **395 checks, 15 cases, 0 failures**.

---

## 1. Build and run

```bash
bash host/rt/tools/build-wsl.sh            # or: cmake -S host/rt -B ~/rt-build -G Ninja && cmake --build ~/rt-build
~/rt-build/rtprobe --all                   # what the host resolved, quotable
~/rt-build/rttests                         # the suite
```

The real inputs are CMake cache variables with working defaults
(`DH2_RT_SYSROOT_LIB`, `DH2_RT_VENDOR_LIB`, `DH2_RT_ENGINE_DIR`, `DH2_RT_MEM_DIR`);
configure fails loudly rather than silently skipping a missing `libc.so`.

---

## 2. The decision this subsystem had to make: emulate the linker, or run it

**This host emulates the linker's behaviour. It does not use the guest `linker`.**

ZettaBridge's library mode does the opposite: it loads the guest `zbhost`
executable, starts it at the guest interpreter (`ld-android.so`), and delegates
`dlopen`/`dlsym` to the guest's own bionic running on a service thread
(`core/src/library_runtime.cpp:131-157, 361-399`). That is a good design for a
general-purpose layer, and it costs ZettaBridge a set of mechanisms this project
does not want:

| ZettaBridge mechanism needed because the guest linker relocates | Why this host does not need it |
| --- | --- |
| `tools/fix_guest_lib.py` rewriting `DT_TEXTREL` into `DT_ZB_TEXTREL` (`core/include/zb/elf_fixups.h:6-9`) | Nothing here relocates text: the four measured types all target writable segments in this corpus, and a text relocation is a hard, reported error |
| Keeping library code pages writable so the guest linker's text relocations succeed | Guest segment protections are real host `mprotect` state, verified by a write that must fault |
| A guest service thread, a handshake protocol (`zb_service_api`), carriers parked inside a guest host-call | Symbols are resolved in the host, eagerly, so there is no resolver to ask and no reason to defer |
| Patching guest libraries in place before the guest linker sees them | The guest files are read-only inputs, never modified |

The argument is measurement, not taste. The guest link surface here is six
relocation types in total (four in the engine, two more in `libc.so`/`libc++.so`),
and `HOST-IMPORT-SURFACE.md` already recorded that the engine's 49 132 + 4 969 +
351 + 11 relocations are `RELATIVE`, `ABS32`, `JUMP_SLOT` and `GLOB_DAT` and
nothing else. Emulating that is a few hundred lines with no hidden state;
delegating it means carrying the guest's dynamic linker, its namespace bookkeeping
and its file-patching contract. Emulation also keeps the property the project
already relies on: because `R_ARM_ABS32` pins symbol addresses (the 102-entry
builtin table in guest `libc.so`), a host that assigns biases itself knows exactly
where every pinned address is, whereas a guest linker that is fed patched files can
move them.

What "emulate" concretely means here: `Loader::load()` places the main object, the
interpreter object and the `DT_NEEDED` closure, assigns static TLS offsets, then
binds every relocation eagerly. `ld-android.so` is still named as the interpreter
object (`AT_BASE`, and the object that exports the `__loader_*` stubs the bionic
stack imports), but **its code is not entered by this tree** — no guest `linker`
instruction executes. See §9 for the honest cost of that choice.

---

## 3. What loads, and where it lands

`rtprobe --load`, real run (bias and ranges from the guest's own highest-fit policy
below the mmap ceiling):

```
== placement (highest free run below 0xfe000000)
module                       bias      start        end   segs   needed      tls
libDungeonHunter2.so   0xfd5ca000 0xfd5ca000 0xfe000000      2        7        -
    seg r-x  0xfd5ca000..0xfdf20000  filesz 0x955130 memsz 0x955130
    seg rw-  0xfdf20130..0xfe000000  filesz 0x04954c memsz 0x0df500
ld-android.so          0xfd5c7000 0xfd5c7000 0xfd5ca000      3        0        -
libc.so                0xfd4fa000 0xfd4fa000 0xfd5c7000      4        2      8 B
libGLESv2.so           0xfd4f5000 0xfd4f5000 0xfd4fa000      2        0        -
libstdc++.so           0xfd4f1000 0xfd4f1000 0xfd4f5000      3        1        -
libm.so                0xfd4cf000 0xfd4cf000 0xfd4f1000      4        1        -
libGLESv1_CM.so        0xfd4cc000 0xfd4cc000 0xfd4cf000      2        0        -
libdl.so               0xfd4c7000 0xfd4c7000 0xfd4cc000      4        1        -
liblog.so              0xfd4ba000 0xfd4ba000 0xfd4c7000      4        4        -
libc++.so              0xfd3e6000 0xfd3e6000 0xfd4ba000      4        3     12 B
modules 10, mapped 12689408 bytes, relocations seen 59623 applied 59623 unresolved 0 weak-undefined 2
```

Load order is the guest's: main object, interpreter, then `DT_NEEDED`
breadth-first. `libz.so` is deliberately *not* in this closure — nothing the
engine needs requires it (`libStormGLOFT.so` does, and that object is not loaded
here); its absence is asserted rather than assumed, because the expectation table
lists exactly the objects that must be present.

`ld-android.so` is the smallest object in the set (three segments, 0 needing
relocations); `libc++.so` is pulled in by `liblog.so`, which the engine needs, and
it is the only object carrying Android's *packed* relocation tables.

### 3.1 Relocation census, per module, against the measured numbers

```
per module               RELATIVE      ABS32  JUMP_SLOT   GLOB_DAT    TPOFF32    DTPMOD32
libDungeonHunter2.so        49132       4969        351         11          0           0
ld-android.so                   0          0          0          0          0           0
libc.so                      1165        129        588         43          1           0
libGLESv2.so                    0          0          0          0          0           0
libstdc++.so                    3          0         18          1          0           0
libm.so                         3          0          5          2          0           0
libGLESv1_CM.so                 0          0          0          0          0           0
libdl.so                        0          0         13          0          0           0
liblog.so                      20          2        100          2          0           0
libc++.so                     533       1921        420        190          0           1
```

Every one is applied (`relocs_applied == relocs_seen`) and none is unresolved.

### 3.2 The relocation types, and the two subtleties that matter

| Type | Implementation | Subtlety |
| --- | --- | --- |
| `R_ARM_RELATIVE` (23) | `*slot = load_bias + in_place_word` | The addend is the word already in the slot; 49 132 in the engine alone |
| `R_ARM_ABS32` (2) | `*slot = S + A` | `A` is in the slot and is *not* zero in this corpus: `&sym + 4` is common |
| `R_ARM_GLOB_DAT` (21) | `*slot = S + A` | Keeps its addend |
| `R_ARM_JUMP_SLOT` (22) | `*slot = S` **(addend discarded)** | The slot initially holds the PLT resolver-stub address; adding it writes `S + stub`, a silent corruption. The independent implementation in `host/loader/src/dynamic.cpp:1111-1119` documents the same trap |
| `R_ARM_TLS_TPOFF32` (19) | `*slot = module_tls_offset + st_value + A` | One in the whole sysroot (`libc.so`, `.got`, symbol index 0, addend 0) → the module's own static TLS base, `-8` |
| `R_ARM_TLS_DTPMOD32` (17) | `*slot = tls_module_id of the defining object` | One, inside `libc++.so`'s packed table; ids are 1-based in load order (`libc.so` = 1, `libc++.so` = 2) |

Order matters and is fixed: **packed tables first, then `DT_REL`, then
`DT_JMPREL`**, because a packed `RELATIVE` entry can write a GOT slot that a later
`GLOB_DAT` overwrites (and the reverse).

Anything else — `COPY`, `IRELATIVE`, `RELA`, packed `DT_ANDROID_RELA` — is a hard,
named error. There is no best-effort path: a relocation that silently wrote
nothing is the failure mode that costs days.

### 3.3 Packed Android relocations are decoded, not ignored

`libc++.so` is the one object that carries them, and it carries both encodings.
`src/loader.cpp` implements an `APS2`/LEB128 decoder (`DT_ANDROID_REL`) and an
`RELR` bitmap decoder (`DT_ANDROID_RELR`). The counts are cross-checked by an
independent Python decoder (`tools/packed_relocs.py`):

```
DT_ANDROID_REL: 7072 packed bytes -> 2112 relocations
    R_ARM_ABS32: 1921
    R_ARM_GLOB_DAT: 190
    R_ARM_TLS_DTPMOD32: 1
DT_ANDROID_RELR: 300 packed bytes -> 533 R_ARM_RELATIVE
combined packed census: {'R_ARM_ABS32': 1921, 'R_ARM_GLOB_DAT': 190,
                         'R_ARM_TLS_DTPMOD32': 1, 'R_ARM_RELATIVE': 533}
```

The C++ decoder was adapted from the in-progress `host/loader`
(`src/dynamic.cpp:327-505`), with one deliberate difference: the RELR `where`
cursor is tracked per the format definition instead of being reset per base word,
so a run of consecutive bitmap words decodes correctly.

### 3.4 Symbol resolution

The rule follows the guest's linker: the **global group first** (main object, the
interpreter, then the closure in load order), and only then the requesting object's
**local group** (breadth-first over its own `DT_NEEDED`). A symbol defined in the
requesting object still goes through the global scope unless the object is
symbolic. `dlsym(handle, name)` (`lookup_from_handle`) reverses that, which is what
a handle means: local group first, then global.

Exports only: defined, non-local, `STV_DEFAULT`, and an address-carrying type.
Hidden and internal symbols are not exported, which is why `dlsym` on `libc.so`
returns **`libdl.so`'s** `dlsym` rather than libc's non-exported stub — and the test
re-derives that from the files with the same rule instead of trusting the loader.

Lookup path: `.hash` (what bionic's own `dlsym` walks) when the object has one,
then `.gnu.hash` (necessary for `ld-android.so`, `libc++.so` and `libdl_android.so`,
which have no `.hash`), then a linear scan for objects with neither. A hash table
that *is* present is authoritative: a miss in it means "not defined here", so the
linear scan is not run on top of it.

```
== symbol resolution (what guest dlsym would return)
   malloc               -> 0xfd538629 in libc.so            (+0x03e629)
   free                 -> 0xfd5384c5 in libc.so            (+0x03e4c5)
   pthread_create       -> 0xfd551b65 in libc.so            (+0x057b65)
   pthread_join         -> 0xfd552649 in libc.so            (+0x058649)
   pthread_mutex_lock   -> 0xfd552c39 in libc.so            (+0x058c39)
   dlopen               -> 0xfd4c8911 in libdl.so           (+0x001911)
   dlsym                -> 0xfd4c8923 in libdl.so           (+0x001923)
   sin                  -> 0xfd4e93e0 in libm.so            (+0x01a3e0)
   inflate              -> 0xfdc3cf48 in libDungeonHunter2.so (+0x672f48)
   memcpy               -> 0xfd562280 in libc.so            (+0x068280)
   __errno              -> 0xfd546709 in libc.so            (+0x04c709)
   mmap                 -> 0xfd5639b7 in libc.so            (+0x0699b7)
```

`inflate` resolving into the engine is correct and is evidence the scope order is
right: the engine defines zlib statically, so the *main object* wins over `libz.so`
(which in any case is not loaded here).

The engine's own GOT slots are bound, and the values are what the guest expects:
`glBindBuffer` and `glSampleCoverage` land in the ZettaBridge guest GL stubs, which
is how this host avoids reimplementing 300 GL entry points it does not need.

---

## 4. Threads and TLS

### 4.1 The layout was measured, not assumed

Everything here comes from reading the guest's own `libc.so`
(`tools/tls_reloc_context.py`, `tools/tp_offset_scan.py`):

| Measurement | Value |
| --- | --- |
| `PT_TLS` in `libc.so` | `vaddr 0xa7160 filesz 8 memsz 8 align 8`, template `ce 79 d9 ac 00 00 00 00` |
| TLS relocations in `libc.so` | exactly one `R_ARM_TLS_TPOFF32`, in `.got`, symbol index 0, addend 0 |
| `mrc p15,0,Rt,c13,c0,3` sites in `libc.so` | 48; the follow-on immediate accesses are `[tp,+4]` ×16, `[tp,+0x18]` ×12, `[tp,-4]` ×8 |
| `__errno` | `mrc TP; ldr r0,[r0,#4]; add r0,#0x29c` → errno is a *field* of the object TP points at, and slot 1 holds TP itself |
| `pthread_self` | `mrc TP; bx lr` → **TP is the `pthread_internal_t`** |
| `libc++.so`, `libstdc++.so`, `libm.so`, `liblog.so`, `libz.so` | 0 `mrc c13,c0` sites at all |

Two consequences, both implemented:

1. **Static TLS lives below the thread pointer** (ELF variant I). `libc.so`'s own
   eight bytes hold the stack canary and its code reads them as `[tp, #-4]`, which
   only works if `libc.so`'s block ends exactly at TP. `tls.cpp` therefore assigns
   offsets cumulatively **in load order** (`offset = -cursor`), which gives
   `libc.so` `-8` — the value that makes its own baked accesses correct.
2. **TP is a bionic control block**, not a bare TCB: `tls_slot[0] = TP`,
   `tls_slot[1] = TP`, `errno` at `TP + 0x29c`, and at least `0x2a0` bytes of
   `pthread_internal_t` behind it. The block is `[module static TLS][control
   block]`, TP 16-byte aligned, with the padding between the last static block and
   TP (which the ABI allows, because module offsets are recorded relative to TP).

Module ids for `R_ARM_TLS_DTPMOD32` are 1-based in load order (`libc.so` = 1,
`libc++.so` = 2), matching bionic's convention.

```
[rt] static TLS: libc.so           8 bytes align 8   -> tp-8 (module id 1)
[rt] static TLS: libc++.so        12 bytes align 4   -> tp-20 (module id 2)
   note tp 0xfd2e5020, static TLS 32 bytes, control block 672 bytes, block [0xfd2e5000,0xfd2e6000)
      libc.so TLS: template word 0xacd979ce, canary slot [tp-4] = 0x00000000
```

`static TLS 32 bytes` is `8 + 12` rounded up to the 16-byte TP alignment; the
control block is exactly `0x2a0` = `kPthreadInternalSize`; the template word was
copied out of the mapped `libc.so` image and the canary slot is at `tp-4`, the
address `libc.so`'s own code uses.

### 4.2 What a guest thread is

`GuestThread` (src/thread.cpp) is the mechanism adapted from ZettaBridge's
`Process::clone_thread` (`core/src/process.cpp:658-691`) and `finish_thread`
(`core/src/process.cpp:702-711`):

* a guest stack from the top of the mmap window downward, SP 16-byte aligned;
* a TLS block from §4.1, with the bionic control block filled in;
* a register file: `r0..r3` = arguments, `r13` = SP, `r14` = the return trap
  (`kHostReturnAddress` = `0xFFFF0F00`, exactly ZettaBridge's address), `r15` = the
  entry with only the T bit following bit 0 of the entry value;
* `CLONE_CHILD_SETTID` written by the child before it runs;
* on exit, `CLONE_CHILD_CLEARTID` cleared *and the futex woken* — which is precisely
  what bionic's `pthread_join` sleeps on, so `join()` is a real futex wait and not a
  host-side flag;
* the `.bss`-style zeroing and the whole-space clearing that make a reloaded module
  set deterministic.

The initial thread of the process is the kernel's contract, not a clone: the stack
image is `argc`, `argv[]`, NULL, `envp[]`, NULL, `auxv`, `AT_NULL`, with
`AT_RANDOM`, `AT_PLATFORM` (`v8l`) and `AT_EXECFN` appended, and the register file
is `r0 = sp` (pointing at `argc`), `sp`, `lr = 0`, `pc = entry`. The image is
verified after it is written (top word is `argc`, `argv` and `envp` are
NULL-terminated, `AT_NULL` present) rather than trusted.

`__ARM_NR_set_tls` / `__ARM_NR_get_tls` are one-liners over the thread pointer
(`set_tls`/`tls()`), because the guest's own bionic start-up sets TP itself; the
host's block is what makes guest code runnable before that happens and for threads
the host creates.

### 4.3 Carrier and borrower

`CarrierPool` (src/carrier.cpp) adapts `Process::create_borrower`
(`core/src/process.cpp:353-370`), `destroy_borrower` (`:378-393`) and
`LibraryRuntime::Carrier` (`core/src/library_runtime.cpp:442-582`): a parked guest
thread is published, a borrowing host thread takes a lease that copies the
carrier's register file and TLS, and the carrier stays parked (its state is not
disturbed — the test asserts exactly that). A second lease on the same host thread
is refused, and a lease released on a different host thread is refused *and*
invalidated rather than corrupting the carrier.

The tailored difference: this host does not spawn the carrier with the guest's
`pthread_create` (that needs the JIT and the syscall layer). A carrier here is a
`GuestThread` parked in the caller's park primitive, which is what the carrier
actually is — a stack, a TLS block and a register file.

---

## 5. Test results

`~/rt-build/rttests` — **395 checks, 15 cases, 0 failures**. Raw output is quoted
throughout this document; the load-bearing lines:

```
-- load the real closure (engine + ld-android + bionic sysroot + vendor stubs)
   note modules in load order (main object first):
      libDungeonHunter2.so   bias 0xfd5ca000  [0xfd5ca000,0xfe000000)  2 segments  needed 7  tls -
      ...
      libc++.so              bias 0xfd3e6000  [0xfd3e6000,0xfd4ba000)  4 segments  needed 3  tls 12B
-- segment permissions and .bss zeroing, from the mapped image
-- relocation census per module against the measured counts
      libDungeonHunter2.so   RELATIVE  49132 ABS32  4969 JUMP_SLOT   351 GLOB_DAT   11 TPOFF32 0  (applied 54463, unresolved 0)
      libc.so                RELATIVE   1165 ABS32   129 JUMP_SLOT   588 GLOB_DAT   43 TPOFF32 1  (applied 1926, unresolved 0)
      ...
   note total relocations applied across the closure: 59623
-- every R_ARM_RELATIVE equals the file's addend plus the module bias
      libDungeonHunter2.so   RELATIVE re-derived 49132/49132
      libc.so                RELATIVE re-derived 1165/1165
      libstdc++.so           RELATIVE re-derived 3/3
      libm.so                RELATIVE re-derived 3/3
      liblog.so              RELATIVE re-derived 20/20
-- every bound symbol relocation names a symbol the target object defines
      symbol relocations re-resolved from the files: 1552/1554
      of those, 0 are symbol-index-0 (self) slots
      of those, 2 are weak-undefined zero slots
-- libc++.so's packed Android tables are decoded and land in the image
      packed entries 2645: RELATIVE 533, ABS32 1921, GLOB_DAT 190, DTPMOD32 1 (applied 2645)
      packed RELATIVE re-derived 533/533; packed symbol relocations re-resolved 2111/2111
-- every R_ARM_ABS32 points into a loaded module
      ABS32 sites 5100, inside a module 5100, self-relative (file+bias) 0
-- create and join guest threads; distinct TLS per thread
      4 guest threads created and joined; TPs 0xfd2e1020 0xfd1e0020 0xfd0df020 0xfcfde020
rttests: 395 checks, 15 cases, 0 failures
```

What each test actually proves, and how it is kept independent of the code under
test:

| Case | Evidence | Independence |
| --- | --- | --- |
| Closure loads | 10 modules, load order, biases, ranges | Expectation table of modules that *must* be present |
| Placement | ranges inside `[0x10000, 0xFE000000+PAGE)`, pairwise disjoint | Recomputed from the mapped modules, not the loader's counters |
| Permissions | text readable and **not** writable; writable data writable; a forked child's write to engine text must die with `SIGSEGV` | The host OS enforces it (`mprotect`), the child proves it |
| `.bss` | every `memsz > filesz` tail reads back zero | Read out of the mapped image |
| Census | per-module counts equal the measured numbers | `tools/elf_survey.py` on the same files |
| `RELATIVE` | all 50 323 classic sites (and, separately, the 533 packed ones) equal `file_word + bias` | The test re-reads the *file* and maps vaddr→file offset itself |
| Symbol relocations | all 1 554 classic + 2 111 packed ABS32/GLOB_DAT/JUMP_SLOT targets name a symbol the target module's *file* defines at exactly that offset (plus the ABI's addend rule, plus weak-undefined → 0) | The test walks each module's `.dynsym` from the file |
| Packed tables | 2 645 entries, counts match an independent Python decoder | `tools/packed_relocs.py` |
| Symbol resolution | address == `owner_bias + st_value` for 14 symbols across 7 objects | The test re-implements the scope order and export rule itself |
| Scope | global group before the requester's local group; `sin` from `libc.so` is `libm.so`'s | Asserted against `lookup_global` |
| Initial thread | `r0 == sp`, `sp` 8-byte aligned, `lr == 0`, `pc == entry`, T bit follows bit 0, `argc/argv/envp/auxv` shape and strings, `AT_NULL`, 16 readable bytes at `AT_RANDOM` | Read back out of guest memory |
| TLS | `libc.so` offset is exactly `-memsz`; TP 16-byte aligned; slot 0 and slot 1 are TP; errno at `+0x29c` is 0; the template word was copied; the canary address is `tp-4` | The layout rule is stated as an equation derived from the disassembly, and the copied bytes are compared with the file |
| Create/join | 4 threads, distinct TPs and TIDs, `CLONE_CHILD_SETTID` visible, `clear_child_tid` cleared by the exit path, each entry ran on its own host thread, a second `start()` refused | Futex-based join, host thread identity recorded |
| Carrier lease | lease copies regs/TLS and leaves the carrier untouched; double lease refused; wrong-thread release refused | — |

Reproduce the tooling output the document quotes:

```bash
python host/rt/tools/elf_survey.py                 # per-object PT_TLS, hash tables, census
python host/rt/tools/tls_reloc_context.py          # TLS templates and TLS relocation addends
python host/rt/tools/tp_offset_scan.py             # TP-relative access directions (the layout evidence)
python host/rt/tools/packed_relocs.py              # independent packed-relocation decoder
```

---

## 6. What was adapted, and from where

| This tree | Adapted from | What was kept |
| --- | --- | --- |
| `src/module.cpp` `parse_module`, `place_module` | ZettaBridge `core/src/elf_loader.cpp:8-131` | validation, PT_LOAD span, highest-free-run placement for `ET_DYN`, map-anonymous → copy → protect order |
| `src/module.cpp` `DynSym` | ZettaBridge `core/src/elf_symbols.cpp:1-186` | dynamic-table access without section headers |
| `src/loader.cpp` `locate`, `Namespace` | ZettaBridge `core/src/process.cpp:120-296` | search directories, and keeping the bionic stack apart from the app-private directory |
| `src/loader.cpp` `load`, `load_dependencies` | ZettaBridge `core/src/process.cpp:400-430` | main → interpreter → `DT_NEEDED` breadth-first |
| `src/loader.cpp` relocations + census | ZettaBridge `core/src/elf_loader.cpp` + `docs/HOST-IMPORT-SURFACE.md`; `GLOB_DAT`/`JUMP_SLOT` addend rule cross-checked against `host/loader/src/dynamic.cpp:1111-1128` | the four types, the per-type census, the "refuse what is not implemented" contract |
| `src/loader.cpp` packed decoders | `host/loader/src/dynamic.cpp:327-505` | APS2/LEB128 and RELR decoding (with the RELR cursor fix) |
| `src/tls.cpp`, `src/thread.cpp` | ZettaBridge `core/src/process.cpp:658-711`, `core/include/zb/guest_thread.h:12-19`, `core/src/cp15.cpp:36-52` | `CLONE_SETTLS` / `CHILD_SETTID` / `CHILD_CLEARTID` semantics, the futex join, `kHostReturnAddress`/`kHostReturnSwi`, TPIDRURO as the thread pointer |
| `src/thread.cpp` `build_initial_stack` | ZettaBridge `core/src/initial_stack.cpp:22-61` | strings pushed down from the stack top, then argc/argv/envp/auxv |
| `src/carrier.cpp` | ZettaBridge `core/src/library_runtime.cpp:442-582`, `core/src/process.cpp:353-393` | carrier publication, borrower register/TLS copy, single-lease-per-host-thread, release on the owning thread |
| `src/space.cpp` | this project's `host/mem` (`dh2mem`), compiled in **unmodified** | reservation, region arena, placement, real `mprotect` |

Nothing from ZettaBridge's EGL, GL, `libandroid`, looper, sensor, input or
configuration backends is present (measured zero imports for this engine).
ZettaBridge's licence notice is retained in `host/rt/NOTICE.md`, and every file
names its source in its header comment. Nothing under `host/mem`, `host/loader`,
`host/p0`, `host/jni`, `DH2Work-stage` or the rest of `DH2Work` was modified.

---

## 7. What is not modelled

Stated plainly, because each of these is a place where a device run can still fail:

1. **No guest `linker` executes.** `ld-android.so` is named in `AT_BASE` and
   exports the `__loader_*` stubs that the bionic stack imports, but the guest's
   dynamic linker never runs in this tree. Consequences: the guest's own
   `__loader_dlopen`/`dlsym` bookkeeping (its `soinfo` list, its namespaces) is not
   populated by this host, and `dlopen` issued from guest code is not wired to this
   loader. If the engine calls `dlopen` at run time, that call needs a host-call or
   syscall-side bridge into `Loader::load_object`; the surface is 7 `DT_NEEDED`
   entries closing over 10 objects today, so that bridge is small but it does not
   exist yet.
2. **Dynamic TLS is not modelled.** Every module with `PT_TLS` gets a *static*
   block and its `R_ARM_TLS_DTPMOD32` gets a module id, but there is no dynamic
   thread vector: a guest `__tls_get_addr` call — `libc++.so`'s 12-byte
   zero-initialised block is reachable that way and by nothing else — would consult
   libc's `g_tls_modules`/DTV, which this host never builds. `libc.so`'s own TLS is
   not affected: it is read by baked TP-relative access and by its own GOT slot,
   both of which are correct here.
3. **Lazy binding is not modelled; every `JUMP_SLOT` is bound eagerly.** Lazy
   binding in bionic needs the guest's PLT resolver, which needs the *guest*
   linker's symbol tables — exactly the state §2 chose not to create. The engine
   asks for lazy binding (no `DT_BIND_NOW`); it gets eager binding, which is a
   superset of the same answers. `set_lazy_jump_slots` is accepted and recorded but
   does not defer anything.
4. **Only REL relocations, and only on ARM32.** `DT_RELA`, packed
   `DT_ANDROID_RELA`, `R_ARM_COPY` and `R_ARM_IRELATIVE` are hard errors. None
   appear in this corpus (measured), so this is a refusal, not a gap.
5. **No `init_array`/`fini_array`/`DT_INIT` execution.** The tables are parsed and
   available on `Module`, but running them is the process layer's job, not the
   loader's.
6. **No `GNU_RELRO` handling.** The engine has no `PT_GNU_RELRO`; if a future
   object does, its `.data.rel.ro` stays writable. Harmless, but not modelled.
7. **The kernel's kuser helper page is not modelled**, only the single page
   containing the host return trap (`0xFFFF0F00`), which is mapped read+execute and
   holds the ARM-mode `svc #0x5AFFFF` stub. A guest that calls a kuser helper
   (`__kuser_get_tls`, `__kuser_cmpxchg`) would fault.
8. **`AT_HWCAP` is a constant** (`0x1FB0D6`, the same mask ZettaBridge advertises),
   not read from the device. Nothing here consults it, but bionic's ifunc selection
   will.
9. **No RTLD_GLOBAL/dlopen-time namespaces.** The global scope is fixed at load
   time; an object added later is local to its group.
10. **Guest-at-`mmap` interoperation.** The loader places modules itself; it does
    not consult a guest `brk` or reserve a heap window, and it will not know about
    guest mappings made by the syscall layer afterwards.

---

## 8. Not verified without a device

1. **No guest instruction has been executed.** This tree contains no JIT; the
   register file, TLS block and stack image are verified as *state*, and a thread's
   entry body in the tests is host C++ reading that state, not translated ARM32.
   What a device adds: that Dynarmic can start at `pc = interpreter->entry_guest()`
   with these registers, and that guest code's first `mrc p15,0,Rt,c13,c0,3` returns
   the TP the host installed. The single highest-value next step is a two-line test
   that links Dynarmic and executes `mrc`+`bx lr`.
2. **`libc.so`'s errno path assumes `TP` is a `pthread_internal_t`.** That is what
   `pthread_self` and `__errno` disassembly show, and the block is sized and zeroed
   accordingly, but no guest `pthread_internal_t` field other than the two slots and
   errno is initialised: `join_state`, `attr`, the cached pid, the mutex/condvar
   fields are all zero. Real bionic fills them in `pthread_create`, so the *initial*
   thread is the exposure. If the engine's first thread calls `pthread_join` or
   `pthread_detach` on itself before creating threads, zero is not a valid
   `join_state`.
3. **TLS offsets are derived from one measured constraint plus the ABI rule.** The
   constraint is `libc.so`'s `[tp, #-4]` canary access; it pins `libc.so`'s offset
   to `-8`. `libc++.so`'s 12-byte block is placed below it by the ABI's cumulative
   rule and nothing in `libc++.so` reads TP directly, so its exact offset is
   unexercised — it matters only through the dynamic-TLS path that §7.2 says is not
   modelled.
4. **The initial thread's stack image is verified, not consumed.** Nothing has yet
   walked the `argc/argv/envp/auxv` the host wrote, so `AT_RANDOM` is "16 readable
   bytes" and not "the seed bionic uses for the stack guard".
5. **No device timing or memory data.** Placement is deterministic here (same
   biases in every run of this build); the device's own address space use (loaded
   ZettaBridge-free host, GL driver mappings) can shift the highest-fit choices,
   which is fine for correctness but changes every quoted address.
6. **`libz.so` is not in this closure**, so `libStormGLOFT.so`'s needs are not
   exercised. Adding it is a search-directory entry away; its census is not asserted
   here.
7. **The 2 MiB guest `linker` is never loaded.** It is not needed by §2's design;
   whether anything the engine does at run time assumes a populated guest `soinfo`
   list is exactly what §7.1 flags.

---

## 9. Files

| Path | What it is |
| --- | --- |
| `host/rt/include/dh2rt/rt.hpp` | guest address-space policy, clone flags, ARM private syscalls, bionic TCB constants, errors |
| `host/rt/include/dh2rt/space.hpp`, `src/space.cpp` | the guest address space as this tree uses it (dh2mem underneath) |
| `host/rt/include/dh2rt/module.hpp`, `src/module.cpp` | ELF32 parse, placement, protection, dynamic symbol access |
| `host/rt/include/dh2rt/loader.hpp`, `src/loader.cpp` | namespaces, dependency closure, the six relocation types, packed decoders, symbol resolution |
| `host/rt/include/dh2rt/tls.hpp`, `src/tls.cpp` | static TLS assignment and the bionic control block |
| `host/rt/include/dh2rt/thread.hpp`, `src/thread.cpp` | guest thread: stack, TLS, register file, clone/join, initial stack image, return trap |
| `host/rt/include/dh2rt/carrier.hpp`, `src/carrier.cpp` | carrier/borrower lease |
| `host/rt/src/rtprobe.cpp` | the CLI that prints everything quoted above |
| `host/rt/tests/` | `harness`, `t_load`, `t_symbols`, `t_thread`, `main` |
| `host/rt/tools/` | `elf_survey.py`, `tls_reloc_context.py`, `tp_offset_scan.py`, `packed_relocs.py`, `build-wsl.sh` |
| `host/rt/NOTICE.md` | retained notices (ZettaBridge, and dh2mem's relationship) |
