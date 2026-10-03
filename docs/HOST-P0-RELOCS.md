# HOST-P0-RELOCS.md — the relocations apply, and the engine's own startup executes

**Milestone status: the gate's section (b) is closed, and guest startup code now runs.** The
relocation processing that `HOST-P0-ARM64.md` §6 localised to a byte is wired in from
`host/loader` (dh2elf), the soft-float/libm libcalls have a host runtime, and the first guest
startup code this project has ever executed — bionic's `__libc_init`, the engine's
`DT_INIT_ARRAY`, and the guest `linker`'s own entry point — now runs under Dynarmic and stops
at a **named instruction in a named file**, not at address 0.

Everything below is quoted from runs on this machine. Code: `DH2Work-toolchain/host/p0a/`
(a copy of `host/p0arm` plus the new seam). Logs: `host/p0a/logs/`.

---

## 0. The five answers

| Question | Answer |
|---|---|
| Do the relocations apply? | **Yes — 59,623 across 10 modules**, 0 deferred, 2 weak-undefined. The engine alone has **54,463**. Independently re-derived from the file by `scripts/reloc_census.py`. |
| Does the gate's section (b) pass? | **Yes — 12 checks, 0 failures**, on **arm64** and on **x86_64**, against the Unicorn oracle. It was 9 failures before, and all 9 were in this section. |
| Does the engine's own startup execute? | **Yes.** `DT_INIT_ARRAY`: 539 constructors present, **9 entered, 8 completed, 324 guest instructions**, then entry 8 stops at `ld-android.so+0x1734`. |
| Does bionic's `__libc_init` execute? | **Yes — 20 guest instructions**, then the same `ld-android.so+0x1734`. |
| Does the guest `linker`'s entry point execute? | **Yes — 45 guest instructions**, then `SVC #0x0` with `r7=0x000F0005` (`__ARM_NR_set_tls`). |

Where it stops, in one line: **`udf #0xFE` — the entire 2-byte body of `ld-android.so`'s
`__loader_*` trampoline, to which all 26 of that object's exports point (`st_value 0x1735`).**

And what broke on the way, which is a finding in its own right: **Dynarmic's exclusive
monitor.** The first guest instruction that used it — `ldaex r1,[r0]` in `libc.so`'s own
atomic path — killed the process (`SIGSEGV` inside JIT code on arm64, a clean
`assertion failed: conf.global_monitor != nullptr` on x86_64). See §7.

---

## 1. What was missing, and what now supplies it

The old chain, from `HOST-P0-ARM64.md` §6, re-confirmed here from the engine's own bytes
(`python scripts/reloc_census.py <engine>`):

```
  .rel.dyn      54112 entries  {'R_ARM_ABS32': 4969, 'R_ARM_GLOB_DAT': 11, 'R_ARM_RELATIVE': 49132}
  .rel.plt        351 entries  {'R_ARM_JUMP_SLOT': 351}
  total       54463
    DT_PLTGOT = 0x00994A98
    GOT entry 0x00994A98 is relocated?  NO -- not a relocation target
    GOT entry 0x00994A9C is relocated?  NO -- not a relocation target
    GOT entry 0x00994AA0 is relocated?  NO -- not a relocation target
    GOT entry 0x00994AA4 is relocated?  ['R_ARM_JUMP_SLOT']
```

That is the whole of the old failure, and it is now exact rather than inferred: `GOT[0]`,
`GOT[1]` and `GOT[2]` are **not relocation targets at all**. They are the dynamic linker's
private slots (`_DYNAMIC`, `link_map`, `_dl_runtime_resolve`), and only the guest linker
would ever write them. `GOT[3]` is the first `JUMP_SLOT`. So `.plt[0]`'s
`ldr pc,[GOT+8]!` reads a slot that is zero **in the file** — which is why the first imported
call produced `pc = 0`, `lr = 0x00994AA0`. The fix is not to populate `GOT[2]`; it is to bind
the PLT slots, so `.plt[0]` is never entered.

Two things were built for that:

1. **`dh2a::LoaderCpu`** (`src/loader_cpu.{hpp,cpp}`) — the same Dynarmic A32 core the gate
   already had, but reading and writing through `dh2elf::Loader`'s `AddressSpace`, i.e. through
   *the image the loader placed and relocated*. Mapped-ness and permissions come from a region
   table assembled from the ELF program headers plus the two blocks the harness claims; CP15
   (`mrc p15,0,Rt,c13,c0,3`) is served from the loader's thread pointer, and anything else on
   p15 halts with the register spelled out (returning `std::monostate` is
   `ASSERT_FALSE("Should raise coproc exception here")` on the x64 backend).
2. **`dh2a::interpose_libcall_shim`** + **`src/libcall_shim.cpp`** — a page of ARM stubs
   (`svc #0x40+i; bx lr`) and host implementations of the guest's softfp/libm imports, used
   both as an interposition over the relocated image and as the only way to make an
   engine-only load's imports land anywhere but nowhere.

`host/loader` is used **unmodified** as a static library (`dh2elf`, its five sources listed in
`p0a/CMakeLists.txt`; it has no CMakeLists of its own). Nothing under `host/loader`,
`host/p0arm`, `host/p0`, `host/mem`, `host/rt`, `host/harness` or `DH2Work` was written to.

### A defect in the loader that this work hit but did not fix

`AddressSpace::allocate_block()` zeroes the block with `std::memset` **before** any `mprotect`,
and the 4 GiB reservation is `PROT_NONE`, so the first call to it would die inside its own
`memset`. `p0a` therefore claims and protects explicitly (`claim_rw()` in `src/startup.cpp`,
and `interpose_libcall_shim`). `allocate_block` has no caller in `host/loader`'s own tests, so
this is untested code rather than a regression. It is recorded here because `host/loader` is
read-only input for this workstream and the fix belongs to its owner.

---

## 2. The relocation seam, measured

`p0a` bootstraps the real closure through `dh2elf::Loader::bootstrap()` with the interpreter
`ld-android.so` and the guest's own search path (the app-private `guest/lib` first, then
`system/lib`). Full log: `host/p0a/logs/p0a-arm64.txt`.

```
  modules      : 10
    ld-android.so            bias 0xFDFFD000  [0xFDFFD000, 0xFE000000)  3 seg  rel.dyn 0  rel.plt 0  packed 0  BIND_NOW
    libDungeonHunter2.so     bias 0x40000000  [0x40000000, 0x40A36000)  2 seg  rel.dyn 54112  rel.plt 351  packed 0  SYMBOLIC
    libc.so                  bias 0xFDF30000  [0xFDF30000, 0xFDFFD000)  4 seg  rel.dyn 1338  rel.plt 588  packed 0  BIND_NOW
    libGLESv2.so             bias 0xFDF2B000  [0xFDF2B000, 0xFDF30000)  2 seg  rel.dyn 0  rel.plt 0  packed 0  BIND_NOW
    libstdc++.so             bias 0xFDF27000  [0xFDF27000, 0xFDF2B000)  3 seg  rel.dyn 4  rel.plt 18  packed 0  BIND_NOW
    libm.so                  bias 0xFDF05000  [0xFDF05000, 0xFDF27000)  4 seg  rel.dyn 5  rel.plt 5  packed 0  BIND_NOW
    libGLESv1_CM.so          bias 0xFDF02000  [0xFDF02000, 0xFDF05000)  2 seg  rel.dyn 0  rel.plt 0  packed 0  BIND_NOW
    libdl.so                 bias 0xFDEFD000  [0xFDEFD000, 0xFDF02000)  4 seg  rel.dyn 0  rel.plt 13  packed 0  BIND_NOW
    liblog.so                bias 0xFDEF0000  [0xFDEF0000, 0xFDEFD000)  4 seg  rel.dyn 24  rel.plt 100  packed 0  BIND_NOW
    libc++.so                bias 0xFDE1C000  [0xFDE1C000, 0xFDEF0000)  4 seg  rel.dyn 2645  rel.plt 420  packed 2645  BIND_NOW
  bytes mapped : 12689408   pages claimed 5161
  relocations  : total 59623  applied 59623  weak-unresolved 2  deferred 0
    libDungeonHunter2.so     R_ARM_ABS32        seen 4969    applied 4969    weak 0
    libDungeonHunter2.so     R_ARM_GLOB_DAT     seen 11      applied 11      weak 0
    libDungeonHunter2.so     R_ARM_JUMP_SLOT    seen 351     applied 351     weak 0
    libDungeonHunter2.so     R_ARM_RELATIVE     seen 49132   applied 49132   weak 0
    libc.so                  R_ARM_ABS32        seen 129     applied 129     weak 0
    libc.so                  R_ARM_TLS_TPOFF32  seen 1       applied 1       weak 0
    libc.so                  R_ARM_GLOB_DAT     seen 43      applied 43      weak 1
    libc.so                  R_ARM_JUMP_SLOT    seen 588     applied 588     weak 1
    libc.so                  R_ARM_RELATIVE     seen 1165    applied 1165    weak 0
    ... (libstdc++.so, libm.so, libdl.so, liblog.so as measured) ...
    libc++.so                R_ARM_JUMP_SLOT    seen 420     applied 420     weak 0
    libc++.so                R_ARM_RELATIVE     seen 2645    applied 2645    weak 0
  [PASS] the closure's relocation total is the measured 59,623
  [PASS] every relocation was written (eager binding; 59,623 seen, 2 of them weak-undefined)
  [PASS] eager binding: zero JUMP_SLOT left deferred
```

**Eager binding is a requirement here, not a preference**, and section 4 of the run measures
why: under lazy binding a deferred `R_ARM_JUMP_SLOT` keeps the file's addend, which is the
object's *link-time* resolver-stub address (`0x0030DD74` for the engine) and is **not biased**
by the loader. At a bias of `0x40000000` that is a jump to `0x0030DD74`, which is nothing. The
measured line:

```
  deferred __aeabi_fmul PLT slot libDungeonHunter2.so+0x994FF0 = 0x0030DD74  <- the file's resolver stub, NOT biased; a jump here lands at 0x0030DD74 (nothing)
```

`libc++.so`'s packed Android tables are decoded too (2,645 entries: 2,112 `DT_ANDROID_REL` +
533 `DT_RELR`), which is why `libc++.so` reports `packed 2645`.

The four imports `normalize` reaches are now bound to real code, on both hosts:

```
  the four imports normalize reaches:
    __aeabi_fmul     GOT libDungeonHunter2.so+0x994FF0 = 0xFDFD06F4   resolves to libc.so+0x000A06F4
    __aeabi_fadd     GOT libDungeonHunter2.so+0x994F58 = 0xFDFCF1E8   resolves to libc.so+0x0009F1E8
    __aeabi_fcmpeq   GOT libDungeonHunter2.so+0x994B50 = 0xFDFD105C   resolves to libc.so+0x000A105C
    sqrtf            GOT libDungeonHunter2.so+0x994BD8 = 0xFDF219EC   resolves to libm.so+0x0001C9EC
  [PASS] all four of normalize's imports have a nonzero, loaded GOT slot
```

`GOT[2]` is *still zero* in this run, which is the point: the chain that used to end at
address 0 is broken at the slot, not at the resolver.

---

## 3. Section (b) of the gate: 12 checks, 0 failures

`vector3d<float>::normalize` at link address `0x0035E8E0` + the engine's bias
`0x40000000`. The four cases are the oracle's own (`dh2_oracle.py --selftest`; see §3.1):

```
  normalize(3,4,0)  exit=returned-to-sentinel ticks=226 r0=0xFDE0C000
    (3.0,4.0,0.0) -> (0.600000, 0.800000, 0.000000)   oracle (0.600000, 0.800000, 0.000000)  max|delta|=0
    [PASS] returned through the sentinel
    [PASS] r0 is the argument it was given
    [PASS] matches the Unicorn oracle

  normalize(1,2,2)  exit=returned-to-sentinel ticks=226 r0=0xFDE0C000
    (1.0,2.0,2.0) -> (0.333333, 0.666667, 0.666667)   oracle (0.333333, 0.666667, 0.666667)  max|delta|=2.98e-07
    [PASS] ...
  section (b): 12 checks, 0 failures
```

and the same 12/12 in `host/p0a/logs/p0a-x86_64.txt`, i.e. on the x64 backend, with identical
numbers. Instruction counts differ between the two only where the *guest* code called into
bionic (`ticks=226` for `(3,4,0)`), and not at all in the arithmetic.

The engine's `DT_INIT_ARRAY` is at `0x40956130` and its entries are relocated pointers — the
run prints the first four so that is visible rather than assumed:

```
  init_array[0] = 0x4030EE00  (inside the engine image)
  init_array[1] = 0x4030EE20  (inside the engine image)
  init_array[2] = 0x4030EE40  (inside the engine image)
  init_array[3] = 0x4030EE64  (inside the engine image)
```

(For the record, `0x4030EE00` is the engine's own `e_entry`. Anything that assumes
`p_vaddr == p_offset` reads `.init_array` at the wrong file offset and gets ASCII instead of
pointers; the loader reads through the segment map, which is why the values above are
coherent.)

### 3.1 The oracle agrees, and it is the oracle that defines the expected values

`C:\Users\NacWorkstation\Documents\deepseek-harness\default-workspace\DH2-recon\tools\dh2_oracle.py`
run against the same pristine engine, saved as `host/p0a/logs/oracle-selftest.txt`:

```
PLT imports mapped: 351
  normalize(3,4,0)  -> (0.6, 0.8, 0.0)              returns self=True  match_expected=True  err=None
  normalize(1,0,0)  -> (1.0, 0.0, 0.0)              returns self=True  match_expected=True  err=None
  normalize(0,0,0)  -> (0.0, 0.0, 0.0)              returns self=True  match_expected=True  err=None
  normalize(1,2,2)  -> (0.333333, 0.666667, 0.666667) returns self=True  match_expected=True  err=None
```

So the comparison in §3 is Dynarmic's result against Unicorn's, on the same function, with the
same four inputs. The oracle hooks the engine's 351 PLT imports with Python implementations;
this host binds the same slots to the shipped bionic code, which is a strictly stronger claim
about those four names and a different one about libm's last bit.

---

## 4. The libcall shim

The engine is softfp, so `normalize` reaches `__aeabi_fmul`, `__aeabi_fadd`,
`__aeabi_fcmpeq` and `sqrtf` through its PLT. `src/libcall_shim.cpp` provides 69 host
implementations with the guest's own register conventions (float args in `r0`/`r1`, result in
`r0`; doubles in `r0:r1`/`r2:r3`) and 69 ARM stubs, one page of them:

```
     shim stub (ARM state, 8 bytes each)        host
     +---------------------------+              +----------------------------------+
     | svc  #0x40+i              |  --------->  | CallSVC -> entry[i].fn(r0..r15)  |
     | bx   lr                   |  <---------  | result in r0 / r0:r1             |
     +---------------------------+              +----------------------------------+
```

`svc` is the seam because Dynarmic's `CallSVC` callback returns to the *next* guest
instruction when it does not halt, which is exactly a function call, and the PLT veneer does
not touch `lr`, so `bx lr` returns to the caller. The shim page is claimed in the guest
address space and protected R+X, so an imported call lands in **working code, in guest
memory, that the host executes** — which is what the milestone asked for.

Interposed over the relocated closure: **76 GOT slots** (67 distinct shim names, in 10
modules; the two names the closure never imports are `__aeabi_f2uiz`, which the engine
defines itself, and `log10f`). Re-running the same four cases:

```
  normalize(3,4,0)  shimmed: exit=returned-to-sentinel   ticks=102      max|delta|=0
  normalize(1,0,0)  shimmed: exit=returned-to-sentinel   ticks=102      max|delta|=0
  normalize(1,2,2)  shimmed: exit=returned-to-sentinel   ticks=102      max|delta|=2.98e-07
  normalize(0,0,0)  shimmed: exit=returned-to-sentinel   ticks=59       max|delta|=0
  host shim dispatches: 39
    __aeabi_fadd       8
    __aeabi_fmul       21
    __aeabi_fdiv       3
    __aeabi_fcmpeq     4
    sqrtf              3
  [PASS] every normalize case still matches the oracle through the shim
```

`ticks=102` versus `226` is the whole difference between "call bionic and let it do the work"
and "call the host": the same answer, 124 fewer guest instructions.

### 4.1 Engine-only: the case where the shim is not an alternative but the only path

With **only** `libDungeonHunter2.so` loaded and no sysroot, the loader refuses the object's
symbol relocations outright — correctly, because there is nothing to resolve them against:

```
  engine-only: module at bias 0xFD5CA000, rel.dyn 54112 rel.plt 0
  relocate_all(defer) -> undefined-symbol: libDungeonHunter2.so: R_ARM_ABS32 at 0x0095b028 needs undefined non-weak symbol "__cxa_pure_virtual"
  relocations applied 0
  ...
  after interposition: libDungeonHunter2.so+0x994FF0 = 0xFD5B9010 (shim stub in page 0xFD5B9000)
  interposed 67 symbol(s), page 4096 bytes
  normalize(3,4,0) engine-only+shim: exit=returned-to-sentinel ticks=102 max|delta|=0 r0=0xFD5BA000
    __aeabi_fadd       2
    __aeabi_fmul       6
    __aeabi_fdiv       1
    __aeabi_fcmpeq     1
    sqrtf              1
  [PASS] with only the engine loaded, the shim makes the imported call land in working code and normalize matches the oracle
```

`normalize` returns the oracle's value with **zero relocations applied and no sysroot**, because
its four imported calls are now host code. That is the `host/harness` limit
(`UndefinedInstruction at pc=0x00994ff0`, `HOST-RUN-HARNESS.md` §4.1) turned into a working
path for exactly the imports the gate needed.

---

## 5. The engine's own startup

### 5.1 `DT_INIT_ARRAY` — the engine's 539 static constructors

```
  DT_INIT      : 0x40000000
  DT_INIT_ARRAY: 0x40956130, 2156 bytes = 539 function pointers
  init[0] = 0x4030EE00 (ARM state) ...
      -> returned-to-sentinel, 8 instructions, pc=0x20000000 ((no module))
  ...
  init[7] = 0x40310730 (ARM state) ...
      -> returned-to-sentinel, 8 instructions, pc=0x20000000 ((no module))
  init[8] = 0x40310C28 (ARM state) ...
      -> undefined-instruction, 260 instructions, pc=0xFDFFE734 (ld-android.so+0x1734)
  init functions: 539 present, 9 entered, 8 completed, 324 instructions
  stopped in entry 8 at pc=0xFDFFE734 (ld-android.so+0x1734): UndefinedInstruction at pc=0xFDFFE734
    (sentinel=0x20000000 lr=0xFDF9ABC5 sp=0xFEFFFCB0 cpsr=0x200000F3 r0=0x00000010 r1=0x00000001 r2=0x00000000)
```

Entries 0..7 are the `_GLOBAL__I_...` translation-unit initialisers and are 8 instructions
each — `ldr`/`mov`/`add`/`str`/`str`/`str`/`bx lr`, exactly as `HOST-P0-ARM64.md` §6 shows at
`e_entry`. Entry 8 is a real constructor: it runs **260 instructions**, calls into `libc.so`
(`lr = 0xFDF9ABC5 = libc.so+0x6ABC5`) and dies there.

### 5.2 bionic's `__libc_init`

```
  __libc_init = libc.so+0x490FD -> guest 0xFDF790FD (Thumb state)
  arguments  : r0=raw_args=sp=0xFEFFFF00, onexit=0, slingshot=0xFDE0B001 (guest stub: svc #0xFF), ctors=0xFDE0E000 (zeroed)
  auxv       : AT_PHDR=0x40000034 AT_PAGESZ=0x1000 AT_HWCAP=0xFB0D7 AT_ENTRY=0x4030EE00
  exit       : undefined-instruction  ticks=20
  pc         : 0xFDFFE734 (ld-android.so+0x1734)
  detail     : UndefinedInstruction at pc=0xFDFFE734 (sentinel=0x20000000 lr=0xFDF7911F sp=0xFEFFFEE8 cpsr=0x000000F3 r0=0xFDFD8C8C r1=0x00000000 r2=0xFDE0B001)
  TPIDRURO   : 0xFF000014 was served by the shim on request
  [PASS] __libc_init executed at least one guest instruction
```

`__libc_init(raw_args, onexit, slingshot, ctors)` exists to call the application's entry and
then `exit()`, so handing it `slingshot = 0` would `blx r4` into address 0 and prove nothing.
It is given four bytes of real Thumb in the guest address space instead — `svc #0xFF; bkpt #0`
— and `ctors` points at a zeroed block so bionic's `ldr r1,[ctors,#8]; cbz r1` takes the
"no structors" path for the right reason. `__libc_init` never reaches the slingshot: it dies
20 instructions in, at the same place.

The register file at the stop is worth reading: `r0 = 0xFDFD8C8C` is a pointer into `libc.so`'s
data, `r2 = 0xFDE0B001` is the slingshot it was handed. `lr = libc.so+0x4911F` is the return
address of the `bl` at `libc.so+0x4911A`, i.e. the call to the veneer at `libc.so+0x49150`,
whose first act is bionic's `__libc_shared_globals()`.

### 5.3 The guest `linker`'s own entry point

`/system/bin/linker` (whose `DT_SONAME` is `ld-android.so`, which is why the module prints
under that name) loads and relocates with no sysroot at all:

```
  linker      : bias 0xFDEAA000 [0xFDEAA000, 0xFE000000) entry 0xFDF05BC0 rel.dyn 2 rel.plt 2 packed 4215
  relocations : total 4219 applied 4219  relocate_all -> ok
  exit        : guest-svc  ticks=45
  pc          : 0xFDFE504C (ld-android.so+0x13B04C)
  detail      : SVC #0x0 at pc=0xFDFE504C (no guest kernel in this harness; r0=0xFDEA1FAC r1=0xFDEE8F90 r2=0x00000000 r7=0x000F0005)
  [PASS] the guest linker's entry point executed guest instructions
```

This is the `__linker_init` path by address: `e_entry = 0x5BBC0` is inside the linker, and 45
instructions later it issues **`SVC #0` with `r7 = 0x000F0005` = `__ARM_NR_set_tls`** — the
kernel's thread-pointer syscall, which on a real device is the very first thing the linker
does. `r0`/`r1` are already pointers inside its own mapping. That is as clean a stopping point
as this harness could ask for, and it is the documented next component rather than a mystery.

---

## 6. Where it stops, exactly

```
  import: libc.so's __loader_shared_globals     slot 0xFDFD8F10 = 0xFDFFE735
  import: libc.so's __loader_add_thread_local_dtor   slot 0xFDFD8CAC = 0xFDFFE735
  import: libdl.so's __loader_dlopen            slot 0xFDEFFBE0 = 0xFDFFE735
  ... (26 names in total, all at the same address) ...
```

Every one of `ld-android.so`'s 26 exports shares one address:

```
     1: 00001735     2 FUNC    GLOBAL DEFAULT    7 __loader_android_get_LD_LIBRARY_PATH
     ...
    25: 00001735     2 FUNC    GLOBAL DEFAULT    7 __loader_dlopen
    26: 00001735     2 FUNC    GLOBAL DEFAULT    7 __loader_add_thread_local_dtor
```

and the object's third `PT_LOAD` is a **2-byte executable segment** at `vaddr 0x1734`
containing `fe de` — Thumb `udf #0xFE`:

```
PT_LOAD off=0x0    vaddr=0x0    filesz=0x734 memsz=0x734 flags=4   (R)
PT_LOAD off=0x734  vaddr=0x1734 filesz=0x2   memsz=0x2   flags=5   (R+X)   <-- fede
PT_LOAD off=0x738  vaddr=0x2738 filesz=0x48  memsz=0x8c8 flags=6   (RW)
```

So the wall is not an unimplemented instruction, a mis-decoded one, or a memory fault: it is
`ld-android.so` **trapping on purpose**, because that object is a stub whose code is never
meant to run — it exists so the bionic stack has something to import `__loader_*` from, and the
2 MiB guest `linker` is what really implements those names.

Which `__loader_*` call it is, from the call sites rather than from the shared pc:

* `__libc_init` at `libc.so+0x4911A` calls `libc.so+0x49150` (a four-instruction veneer,
  `movw ip,#imm; movt ip,#0; add ip,pc; bx ip`) and the next thing it does at
  `libc.so+0x4911E` is `str.w r7,[r0,#0x554]`, a store into the shared-globals block;
* the engine's constructor 8 arrives from `libc.so+0x6ABC0` (`bl libc.so+0x49150`) and returns
  to `libc.so+0x6ABC4` (`ldr.w r0,[r0,#0x414]`), again a shared-globals field read.

Both paths call the same thing first: bionic's `__libc_shared_globals()`, which libc
implements as a call to the interpreter's `__loader_shared_globals()`. **Implementing that one
symbol, plus a `libc_shared_globals` struct, is what unblocks both.** The pc cannot name the
symbol (all 26 share it); the call sites can, and they agree.

---

## 7. The defect the startup exposed: no exclusive monitor

The first time any of this ran with entry 8 in the loop, the host died:

```
  init[8] = 0x40310C28 (ARM state) ...
Unhandled SIGSEGV at pc 0x00007e2ae4f1300c
qemu: uncaught target signal 11 (Segmentation fault) - core dumped
```

No memory fault had been reported — the guest had not faulted. `p0instr`, a new
one-instruction runner over the *same* core (`src/p0instr.cpp`), reduced it to a single
encoding in a single run:

```
$ p0instr --thumb d0e8ef1f7047 --r0=0x10000000 --no-monitor
first word  : 0x1FEFE8D0
global_monitor: DELIBERATELY NULL (see --no-monitor)
Unhandled SIGSEGV at pc 0x000072c8a711d00c                       (arm64)
```

```
$ p0instr --thumb d0e8ef1f7047 --r0=0x10000000 --no-monitor      (x86_64)
assertion failed: conf.global_monitor != nullptr
```

`d0e8ef1f` is Thumb **`ldaex r1,[r0]`** — one of the ARMv8 acquire/release instructions patch
0001-B adds, at `libc.so+0x418B6` inside bionic's own `LDAEX`/`STREX` atomic path. The cause is
not the patch: `UserConfig::global_monitor` was left null, and it is not optional. On x86_64
Dynarmic asserts; on arm64 the emitted code dereferences the null monitor and the process dies
inside JIT code at a page offset of `0x00c` — which is why four crashes in a row reported
`...0000c`.

The fix is one object (`dh2a::LoaderCpu::build_jit`):

```cpp
    I.monitor = std::make_unique<Dynarmic::ExclusiveMonitor>(1);
    cfg.global_monitor = use_monitor_ ? I.monitor.get() : nullptr;
    cfg.processor_id = 0;
```

and the same instruction, either backend, now runs and reports the fault it should:

```
$ p0instr --thumb d0e8ef1f7047 --r0=0x10000000
run 0       : exit=guest-memory-fault ticks=1 pc=0xFF000000
              detail: read of 4 byte(s) at 0x10000000 is outside every mapped region
              r0=0x10000000 r1=0x00000000 r2=0x00000000 r3=0x00000000 cpsr=0x000000F3
```

`--no-monitor` exists so that this failure stays reproducible rather than becoming a story.
(Note that `check_halt_on_memory_access` — the thing patch `0001-A` guards — is **not** the
cause: the crash reproduces with `--imprecise` too.)

---

## 8. What is *not* verified

* **`qemu-aarch64` is not silicon.** The arm64 numbers come from QEMU's TCG executing the
  AArch64 Dynarmic emits. No phone and no Android emulator were used.
* **No syscall layer.** `SVC` stops. The linker's stop at `__ARM_NR_set_tls` is the first
  syscall the guest makes and the host's answer to it does not exist yet.
* **No `__loader_*` implementation.** That is the next wall and it is a host component (a
  `libc_shared_globals` block plus the 25 stub entry points), not a loader or JIT problem.
* **`__libc_init` is not what ART does to this engine in production.** The engine is a shared
  library; the real startup is `DT_INIT_ARRAY` followed by `JNI_OnLoad`. `__libc_init` was
  executed because the milestone names it, and the `ctors` argument supplied is a zeroed block
  rather than a real bionic `structors` chain — the `cbz` path is taken, but for a reason the
  host constructed.
* **Only one function was compared against the oracle.** 3,009 functions were compared in the
  recon project; this run compares one, in four cases, on two backends.
* **The `__loader_*` call is identified from the call site, not from the pc.** All 26 exports
  share one address, so the pc alone cannot name the symbol. The two independent call sites
  both point at `__libc_shared_globals()`; that is inference from disassembly, not from a
  trace of the resolver.
* **`AT_HWCAP` is still the loader's constant**, `0xFB0D7` as printed here (SWP, HALF, THUMB,
  FAST_MULT, VFP, EDSP, NEON, VFPv3, TLS, VFPv4, IDIV, VFPD32). It is a claim about what
  Dynarmic implements, and the guest believes it. Separately: `HOST-ELF-LOADER.md` §6.2 prints
  it as `0x1028311`, which is the **decimal** value 1,028,311 wearing a `0x` prefix; the mask
  is `0xFB0D7`.
* **`0001-A`'s behaviour is still unproven.** This work loads the guard and takes precise
  faults through it, but nothing here constructs the mid-block abort-after-a-register-write
  case that `DYNARMIC-INDEPENDENCE.md` §6(2) specifies. §7 above does *not* provide it: the
  monitor crash reproduced with precise faults off.
* **The trace is a sample, not a census.** `--trace` single-steps the first N instructions;
  the instruction totals come from Dynarmic's tick callbacks (`GetTicksForCode() == 1`), not
  from a per-instruction log.
* **Section 3's interposition replaces 76 GOT slots in every module**, including `libc.so`'s
  own soft-float path. Sections 2 and 3/5/6 are therefore two different images: section 2 is
  the shipped bionic code, and everything after it runs with the shim replacing 67 names.
  Both are measured; neither is the other.
* **The loader's `AddressSpace::allocate_block` defect (§1) is recorded, not fixed**, and no
  test in `host/loader` exercises it.
* **No guest thread, no signals, no dynamic TLS, no `dlopen`, no constructors beyond
  `DT_INIT_ARRAY`, no JNI.** Unchanged by this work.

---

## 9. Reproducing this

All commands run in WSL2; build output goes under `/root/p0a`.

```bash
P0A=/mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/p0a

# 1. build for aarch64 (static) and run P0-A under qemu-aarch64
bash $P0A/scripts/build-arm64-linux.sh                 # --no-run to stop after the build

# 2. the same tree on the native x86_64 Dynarmic backend, for the cross-backend comparison
bash $P0A/scripts/build-arm64-linux.sh --x86

# 3. capture every log quoted above into host/p0a/logs/
bash $P0A/scripts/capture.sh all

# 4. one instruction at a time (the exclusive-monitor finding, and any future one)
/root/p0a/build/android-arm64-static/p0instr --thumb d0e8ef1f7047 --r0=0x10000000
/root/p0a/build/android-arm64-static/p0instr --thumb d0e8ef1f7047 --r0=0x10000000 --no-monitor
/root/p0a/build/x86_64/p0instr            --thumb d0e8ef1f7047 --r0=0x10000000 --no-monitor

# 5. parse-only check of the new sources with the real toolchain, in about two seconds
bash $P0A/scripts/syntax-check.sh libcall_shim.cpp loader_cpu.cpp startup.cpp p0instr.cpp

# 6. the engine's own relocation tables, independently of the host (Windows Python + pyelftools)
python $P0A/scripts/reloc_census.py <engine>

# 7. the oracle's expected values for section (b)
cd .../DH2-recon/tools && python dh2_oracle.py --elf <engine> --selftest
```

Useful run options (`p0a`): `--trace=N` (single-step the first N instructions and print them),
`--budget=N`, `--init-one=N` (run one `DT_INIT_ARRAY` entry), `--verbose-steps`,
`--verbose-host` (print every fault), `--imprecise`, `--no-monitor`, `--no-shim`,
`--no-engine-only`.

---

## 10. Files

| Path | What it is |
|---|---|
| `host/p0a/src/loader_cpu.{hpp,cpp}` | Dynarmic A32 over `dh2elf::Loader`'s `AddressSpace`: region table for permissions, CP15 thread pointer, the exclusive monitor, the fault classification, the trace, and the shim interposition |
| `host/p0a/src/libcall_shim.{hpp,cpp}` | the 69 softfp/libm libcalls, their ARM stubs and the SVC dispatcher |
| `host/p0a/src/startup.cpp` | the P0-A program: relocation census, section (b), the shim, the engine-only load, `DT_INIT_ARRAY`, `__libc_init`, the linker's entry |
| `host/p0a/src/p0instr.cpp` | one snippet at a time through the same core; `--no-monitor` keeps the null-monitor failure reproducible |
| `host/p0a/CMakeLists.txt` | builds `dh2elf` from `host/loader`'s five sources (read-only input) and the three new targets |
| `host/p0a/scripts/build-arm64-linux.sh` | aarch64 static + qemu-aarch64, and `--x86` for the native backend |
| `host/p0a/scripts/capture.sh` | every log in `host/p0a/logs/`, captured by the run itself |
| `host/p0a/scripts/syntax-check.sh` | parse-only compile with the NDK toolchain and the real include paths |
| `host/p0a/scripts/reloc_census.py` | the engine's relocation tables from the file, with pyelftools |
| `host/p0a/scripts/recon*.sh`, `disasm.py` | the reconnaissance behind §1 and §6, kept so the claims can be re-derived |
| `host/p0a/logs/` | `p0a-arm64.txt`, `p0a-x86_64.txt`, `p0instr-*.txt`, `oracle-selftest.txt` |

Licence: 0BSD, as the rest of the host. `host/loader` and `host/mem` are compiled in
unmodified; `host/p0arm` is the source this tree was copied from and is unmodified too.

---

## 11. The next blocker, in one line

**Implement the `__loader_*` interface — starting with `__loader_shared_globals()` and a
bionic `libc_shared_globals` block — and add a syscall layer; the first guest syscall is
already named (`SVC #0`, `r7 = 0x000F0005`, `__ARM_NR_set_tls`).**
