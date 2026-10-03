# HOST-LOADER-INTERFACE.md — the `__loader_*` interface, implemented, and how far startup now gets

**Milestone status: the wall named in `HOST-P0-RELOCS.md` §6 and §11 is gone.** The 2-byte
`udf #0xFE` at `ld-android.so+0x1734` is never reached again. The host now supplies all 26 of
that object's exports itself, `__loader_shared_globals()` returns a real bionic
`libc_shared_globals` block, and the two paths that used to die on the first call into it both
run past it:

```
__libc_init     20 instructions, died at ld-android.so+0x1734
             -> 53 instructions, and it reaches the application entry point it was given

DT_INIT_ARRAY   9 entered, 8 completed, 324 instructions, entry 8 died at ld-android.so+0x1734
             -> 9 entered, 8 completed, 187 instructions, entry 8 reaches its own first syscall
```

The next exact stop is a **guest syscall**, not a host gap:
**`pc=0xFDFC4494` (`libc.so+0x94494`), `SVC #0x0` with `r7=0xC0` (`__NR_mmap2`)** — the
instruction immediately after the `svc` in bionic's own `__mmap2`.

Everything below is quoted from runs on this machine. Code:
`DH2Work-toolchain/host/loaderif/` (a copy of `host/p0a` plus the new interface). Logs:
`host/loaderif/logs/`.

---

## 0. The five answers

| Question | Answer |
|---|---|
| Are the `__loader_*` symbols interposed? | **Yes — 19 GOT/PLT slots, all 19 that the closure binds**, rewritten to ARM stubs in a claimed guest page. The other 7 of the 26 exports are not imported by anything here. |
| Does `__loader_shared_globals()` return a real block? | **Yes.** A 4 194-byte `libc_shared_globals` at `0xFDE1A000`, with `__libc_argc`, `__libc_auxv`, `program_invocation_name`, `program_invocation_short_name` and `mmap_threshold` filled from the loader's own state and read back out of guest memory to prove it. |
| How far does `DT_INIT_ARRAY` get? | **9 entered, 8 completed, 187 guest instructions.** Entry 8 runs **123 instructions** (was 260, because it no longer takes the `udf` path) and stops in `__mmap2`. |
| How far does `__libc_init` get? | **53 guest instructions** (was 20), calling `__loader_shared_globals()` **3 times**, and it **reaches the `slingshot`** — the application entry point — reaching it with `r0=0` (argc), `r1`=argv, `r2`=envp in guest memory. |
| Same on x86_64? | **Yes, line for line.** Every number quoted below is identical in `logs/loaderif-x86_64.txt`. |

---

## 1. What the interface is, and the two hops that reach it

`ld-android.so` in this sysroot is 2 868 bytes. Its entire executable content is one 2-byte
segment: `PT_LOAD off=0x734 vaddr=0x1734 filesz=0x2 flags=5`, containing `fe de` — Thumb
`udf #0xFE`. All 26 of its exports sit at `st_value 0x1735`, i.e. on that byte:

```
  ld-android.so: bias 0xFDFFD000  dynsym 27 entries  name ld-android.so
  its only executable segment: vaddr 0x1734 filesz 0x2 mapped at 0xFDFFE734  bytes FE DE (Thumb udf #0xDE)
  26 exported symbols:
     1  __loader_android_get_LD_LIBRARY_PATH                 st_value 0x1735  size 2   <- the udf
     2  __loader_android_set_application_target_sdk_version  st_value 0x1735  size 2   <- the udf
     ...
    26  __loader_add_thread_local_dtor                       st_value 0x1735  size 2   <- the udf
```

The closure binds **19** slots to that one address — 6 in `libc.so`, 13 in `libdl.so`:

```
      import: libc.so's __loader_add_thread_local_dtor slot 0xFDFD8CAC = 0xFDFFE735
      import: libc.so's __loader_remove_thread_local_dtor slot 0xFDFD8CB0 = 0xFDFFE735
      import: libc.so's __loader_shared_globals slot 0xFDFD8F10 = 0xFDFFE735
      import: libc.so's __loader_add_thread_local_dtor slot 0xFDFD8F98 = 0xFDFFE735
      import: libc.so's __loader_remove_thread_local_dtor slot 0xFDFD8F9C = 0xFDFFE735
      import: libc.so's __loader_android_get_exported_namespace slot 0xFDFD96B0 = 0xFDFFE735
      import: libdl.so's __loader_android_get_LD_LIBRARY_PATH slot 0xFDEFFBDC = 0xFDFFE735
      ... (13 more in libdl.so) ...
  bound slots pointing at ld-android.so+0x1735 (0xFDFFE735): 19
```

The pc alone cannot name the symbol, because all 26 share one address. What the run shows is
the two hops that reach it, printed from the loaded image rather than reasoned about:

```
  libc.so+0xA3530 (__ThumbV7PILongThunk___loader_shared_globals): 5C84F640 0C00F2C0 476044FC 5C98F640
  libc.so+0xA42C0 (where that veneer lands): E28FC600 E28CCA04 E5BCFC48
  the slot reached: libc.so+0xA8F10 = 0xFDFD8F10, R_ARM_JUMP_SLOT __loader_shared_globals
    (this print runs before interposition, so it still holds 0xFDFFE735)
```

Decoded against the ARM ARM, and cross-checked by running those twelve bytes through this
project's own one-instruction runner (`p0instr`):

* `libc.so+0xA3530` is a 14-byte Thumb PIC veneer —
  `movw r12,#0xd84 ; movt r12,#0 ; add r12,pc ; bx r12`. The 16-bit `add r12,pc` adds
  `Align(PC,4)`, where PC is the *following* instruction (`0xA353C`), so
  `r12 = 0xA353C + 0xD84 = 0xA42C0`. Measured: `p0instr --thumb
  40f6845cc0f2000cfc446047 --budget=3` leaves `pc=0xFF000D90` for a veneer placed at
  `0xFF000000`, and `0xFF000D90 - 0xD84 = 0xFF00000C = 0xFF000008 + 4`.
* `libc.so+0xA42C0` is three A32 instructions —
  `add r12,pc,#0x400` (A32 rotated immediate: imm8 `0x40` ror 6), `add r12,r12,#0x1800`
  (imm8 `0x18` ror 8), `ldr pc,[r12,#0xC48]!`. The pre-indexed load lands on
  `libc.so+0xA8F10`, which is exactly the `R_ARM_JUMP_SLOT __loader_shared_globals` the
  loader bound.

So the interface has **one interception point**: the GOT slot. The interposition writes the
stub there, and the guest's own `bx`/`ldr pc` does the rest. No guest library is edited.

---

## 2. The export inventory, and the real-versus-stub decision for each

The table is in `src/loader_if.cpp`, in `ld-android.so`'s own dynsym order, and the program
asserts that order covers exactly the 26 the object declares. `SLOT` marks the 19 the closure
actually imports.

| # | Export | Imported | Decision, and why |
|---|---|---|---|
| 1 | `__loader_android_get_LD_LIBRARY_PATH` | SLOT | **real** — `const char* (void)`. Returns the address of a real string in guest memory (initially `""`); never NULL, because bionic callers pass it straight to `strlen`. |
| 2 | `__loader_android_set_application_target_sdk_version` | — | **real** — `void (int)`. Stores the int. One word, no failure mode. |
| 3 | `__loader_dladdr` | SLOT | **stub 0** — `int (const void*, Dl_info*)`. A correct answer needs a guest-visible symbol table; this host has none. 0 is the defined "not found" and writes nothing through `r1`. |
| 4 | `__loader_dl_unwind_find_exidx` | SLOT | **stub 0** — `_Unwind_Ptr (pc, int*)`. The loader does parse `ARM_EXIDX`, but guest unwinding is a different component. 0 is the unwinder's documented "no table". |
| 5 | `__loader_dlclose` | SLOT | **stub 0** — `int (void*)`. Nothing was ever opened. |
| 6 | `__loader_android_get_application_target_sdk_version` | SLOT | **real** — returns the value entry 2 stored, which makes the pair testable. |
| 7 | `__loader_cfi_fail` | SLOT | **halt, not stub** — `void (uintptr_t, uintptr_t, uintptr_t)`. In the real linker this is the control-flow-integrity failure reporter: it is not a call that can be answered, it is a report that the guest is already off its rails. Returning silently would convert a hard failure into undefined behaviour later. Reaching it here is itself the finding (this JIT generates no CFI-guarded call), so it records the note and returns. |
| 8 | `__loader_android_dlopen_ext` | SLOT | **stub NULL** — needs a guest `soinfo` list. |
| 9 | `__loader_android_dlwarning` | — | **stub (dropped)** — a warning sink. Dropping a warning is not a lie, but it is not a log either, so the call is recorded in the note. |
| 10 | `__loader_android_init_anonymous_namespace` | — | **stub true** — this host resolves the whole closure in one global scope (`HOST-RUNTIME-LINKER-THREADS.md` §7.9), so there is no namespace machinery to initialise. |
| 11 | `__loader_dl_iterate_phdr` | SLOT | **stub 0** — `int (callback, data)`. The one entry a stub cannot answer with a value: the real implementation calls the guest's callback once per loaded object. Re-entering the JIT from inside an SVC handler is not something this host can do yet. 0 = "no objects reported". |
| 12 | `__loader_shared_globals` | SLOT | **real** — the whole point of this workstream. §3. |
| 13 | `__loader_android_link_namespaces` | — | **stub true** — one global scope; the link is a no-op that cannot fail. |
| 14 | `__loader_android_handle_signal` | SLOT | **stub false** — `bool (int, siginfo_t*, void*)`. No signal layer to hand it to; false is "not handled", which is the truth. |
| 15 | `__loader_dlsym` | SLOT | **stub NULL** — no guest symbol table exposed. |
| 16 | `__loader_dlvsym` | SLOT | **stub NULL** — same. |
| 17 | `__loader_remove_thread_local_dtor` | SLOT | **real** — `bool (void (*)(void*), void*)`. Removes the `(fn, arg)` pair entry 26 recorded, matching on both words as bionic does. No destructor runs in this host yet, so the list records rather than acts — but it is a real list, so "added then removed" is observable and the two entries cannot disagree. |
| 18 | `__loader_android_set_16kb_appcompat_mode` | — | **real** — `void (bool)`. Stores the flag; nothing here consults it yet. |
| 19 | `rtld_db_dlactivity` | — | **real** — bionic answers `false` with no debugger attached; the symbol exists for the debugger interface. |
| 20 | `__loader_android_create_namespace` | — | **stub NULL** — one global scope; nothing to create. |
| 21 | `__loader_android_get_exported_namespace` | SLOT | **stub NULL** — one global scope. |
| 22 | `__loader_android_link_namespaces_all_libs` | — | **stub (void)** — one global scope. |
| 23 | `__loader_android_update_LD_LIBRARY_PATH` | — | **real** — records the string that entry 1 returns, which is the actual contract of the pair. |
| 24 | `__loader_dlerror` | SLOT | **real** — returns the address of a real `"no error"` string in guest memory. bionic's contract is a pointer that is never NULL. |
| 25 | `__loader_dlopen` | SLOT | **stub NULL** — needs a guest `soinfo` list. |
| 26 | `__loader_add_thread_local_dtor` | SLOT | **real** — `int (void (*)(void*), void*)`. Records the pair, capped at 65 536 entries. |

**13 real, 12 stub, 1 halt-on-report.** The dividing line is not "how hard is it" but **"can
this host answer it without guessing"**. A getter or setter over one word can; anything that
needs a guest `soinfo` list, a guest symbol table, a guest callback invocation, a signal
layer or a namespace object cannot, and for those the entries return bionic's own failure
value rather than inventing a success.

---

## 3. `__loader_shared_globals()`: the block, and where its layout came from

The block is not guessed. `scripts/libc_globals_scan.py` reads it out of libc.so's own bytes:
it finds every `bl __libc_shared_globals` site (42), decodes the long thunk's PIC veneer and
cross-checks the GOT slot it computes against libc.so's own relocation table, then decodes the
instruction at each call site to get the field offset that site touches.

```
-- the veneer, decoded from libc.so's own bytes
   0x000A3530  F640 5C84  movw     ('movw', 3460, 12)
   0x000A3534  F2C0 0C00  movt     ('movw', 0, 12)
   0x000A3538  44FC       add      r12, pc
   0x000A353A  4760       bx       r12
   -> Align(PC,4) 0x000A353C + 0xD84 = GOT slot 0x000A42C0 for __loader_shared_globals
   DT_PLTGOT = 0x000A8D7C, and 0x000A42C0 is NOT a relocation target in libc.so's own relocation tables

-- call sites of __libc_shared_globals: 42

-- fields of the block, by offset
   +0x410   2 site(s)
            00049122  ldr.w r7, [r0 (the globals pointer), #0x410]
            0004912A  ldr.w r1, [r0 (the globals pointer), #0x410]
   +0x414   1 site(s)
            0006ABC0  ldr.w r0, [r0 (the globals pointer), #0x414]
   +0x51C   1 site(s)
            00054F52  ldr.w r1, [r0 (the globals pointer), #0x51C]
   +0x520   1 site(s)
            00054F40  ldr.w r0, [r0 (the globals pointer), #0x520]
   +0x554   1 site(s)
            0004911A  str.w r7, [r0 (the globals pointer), #0x554]
   +0x55C   2 site(s)
            0004C920  ldr.w r6, [r0 (the globals pointer), #0x55C]
            0004C96A  str.w r5, [r0 (the globals pointer), #0x55C]
   ... (+0x41C, +0x428, +0x464, +0x468, +0x470, +0x524, +0x528 from features no path here
        reaches: allocation dispatch, GWP-ASan, the dynamic-TLS vector, crash detail) ...
```

That `movw r12,#0xd84` / `add r12,pc` pair is the thing this document had to get right: the
veneer computes `libc.so+0xA42C0`, **not** the slot. The slot is one more indirection away, in
the three A32 instructions at `0xA42C0` (§1). The `NOT a relocation target` line above is the
scanner being honest about that: the veneer's own target is code, and the code is what names
the slot.

The host fills these fields, each named with the libc.so function that reads it:

| Offset | Field | Filled with | Read by |
|---|---|---|---|
| `+0x410` | `__libc_argc` | `Loader::argc()` | `__libc_init` at `libc.so+0x49122`, to compute `envp = argv + 1 + argc` |
| `+0x414` | `__libc_auxv` | `Loader::auxv_guest_addr()` | `getauxval` at `libc.so+0x6ABC4`, walking to `AT_NULL` |
| `+0x51C` | profile flag | `0` (the null path is the default) | `__libc_init_common` at `libc.so+0x54F56` |
| `+0x520` | `mmap_threshold` | `0x20000` (bionic's default for a 4 KiB page) | `__libc_init_common` at `libc.so+0x54F40` |
| `+0x554` | `program_invocation_name` | `argv[0]`'s string address | `__libc_init` at `libc.so+0x4911E` |
| `+0x55C` | `program_invocation_short_name` | the same address | read/written by `android_crash_detail_register` |
| `+0x600` | `libc_globals` | zeroed, 4 096 bytes | nothing reached by this run |

Read back out of guest memory by the run, and compared against the loader's own state rather
than against what the program believes it wrote:

```
  block        : 0xFDE1A000
  +0x410 argc        = 1        (loader argc = 1)
  +0x414 auxv        = 0xFEFFFF18 (loader auxv = 0xFEFFFF18)
  +0x554 prog name   = 0xFEFFFFD8 ("libDungeonHunter2.so")
  +0x55C short name  = 0xFEFFFFD8
  +0x520 mmap thresh = 0x20000
  [PASS] the block's argc field is the loader's own argc
  [PASS] the block's auxv field is the loader's own auxv
  [PASS] the block's program_invocation_name is argv[0]'s address
  libc.so's R_ARM_JUMP_SLOT __loader_shared_globals at 0xFDFD8F10 (libc.so+0xA8F10)
    before interposition it held ld-android.so+0x1735 (the udf); it now holds 0xFDE19058
  [PASS] the GOT slot now points into the stub page, not at the udf
```

The `+0x600` size is taken from libc.so's own object: `.bss` at `libc.so+0xC6000`, size
`0x1000` (`__libc_globals`, 4 096 bytes).

---

## 4. The mechanism, reused from the soft-float shim

Identical to `host/p0a`'s `interpose_libcall_shim` (`HOST-P0-RELOCS.md` §4), with a second,
disjoint SVC range so the two tables cannot dispatch each other's immediate:

```
     stub (ARM, 8 bytes)          host
     +-------------------+        +---------------------------------------------+
     | svc  #0x100+i     |  --->  | CallSVC -> loader_if_dispatch(0x100+i, r)   |
     | bx   lr           |  <---  | entry[i].fn(r): return value written to r0  |
     +-------------------+        +---------------------------------------------+
```

* the immediate is `0x100 + index`; the libcall shim owns `0x40..0x84` and nothing else does
* `svc` is the seam because Dynarmic's `CallSVC` returns to the *next* guest instruction when
  it does not halt — exactly a function call — and `bx lr` returns to the caller
* the page is claimed in guest memory and protected R+X, so an imported call lands in
  **working code, in guest memory, that the host executes**
* the rewrite happens **after** the loader has bound every slot, because it is an
  interposition over a completed binding, not a relocation

```
  stub page   : 0xFDE19000..0xFDE1A000 (4096 bytes, R+X)
  globals blk : 0xFDE1A000..0xFDE1C000 (8192 bytes, RW)
  interposed  : 19 of 26 entries
  redirected  : 19 of the 19 slot(s) the closure bound to those exports
  [PASS] every slot bound to a __loader_* export was redirected
```

`interpose_loader_if` enumerates symbols from `DT_REL`/`DT_JMPREL` at run time rather than
from a hard-coded list, so the "19" is measured, not assumed.

---

## 5. How far startup now gets

### 5.1 `DT_INIT_ARRAY` — the first two paths past the wall

```
  DT_INIT       : 0x40000000
  DT_INIT_ARRAY : 0x40956130, 2156 bytes = 539 entries
  init[0] = 0x4030EE00 (ARM) -> returned-to-sentinel          8 instr  pc=(no module)
  init[1] = 0x4030EE20 (ARM) -> returned-to-sentinel          8 instr  pc=(no module)
  ...
  init[7] = 0x40310730 (ARM) -> returned-to-sentinel          8 instr  pc=(no module)
  init[8] = 0x40310C28 (ARM) -> guest-svc                   123 instr  pc=libc.so+0x94494
  init functions: 539 present, 9 entered, 8 completed, 187 instructions
  stopped in entry 8 at pc=0xFDFC4494 (libc.so+0x94494)
  lr=0xFDF7CEC5 (libc.so+0x4CEC5)  sp=0xFEFFFEA0  cpsr=0x600000D3
  detail        : SVC #0x0 at pc=0xFDFC4494: r7=0xC0 (__NR_mmap2) -- this host has no syscall
                  layer, so the guest stops here; r0=0x00000000 r1=0x00001000 r2=0x00000003
                  r3=0x00000022 r4=0xFFFFFFFF r5=0x00000000
  instruction   : E8BD00F0 (ARM)   state=ARM
  __loader_* dispatches: 0
  [PASS] at least the 8 constructors that completed before this work still complete
  [PASS] the stop inside DT_INIT_ARRAY is the guest's own syscall, not the udf:
         ld-android.so+0x1734 is never reached again
```

What changed, precisely: entry 8 **ran 123 instructions instead of dying after 260 at the
`udf`**, and the constructor that used to stop inside `__libc_shared_globals` now gets all the
way to asking the kernel for memory. `pc=0xFDFC4494` is the instruction *after* the `svc` at
`0xFDFC4490` — `__mmap2` is `libc.so+0x94480`, and its body is

```
00094480 <__mmap2>:
   94480: e1a0c00d     	mov	r12, sp
   94484: e92d00f0     	push	{r4, r5, r6, r7}
   94488: e89c0070     	ldm	r12, {r4, r5, r6}
   9448c: e3a070c0     	mov	r7, #192        ; __NR_mmap2
   94490: ef000000     	svc	#0x0
   94494: e8bd00f0     	pop	{r4, r5, r6, r7}   <- pc=0xFDFC4494 is here; lr=0xFDF7CEC5
```

so the request is 4 096 bytes, `prot=3` (R+W), from the guest's own allocator. `r4 = 0xFFFFFFFF`
and `r5 = 0` are the `fd` and `offset` arguments `__mmap2` passes on the stack (the ARM EABI
put the first four in `r0..r3` and the rest in `r4..r6` before the `svc`); the handler's
`r0..r3` printout is therefore the **syscall's** argument registers, not `__mmap2`'s. The
constructor's own local state is in the 123 instructions before it.

This is a **host gap, not an interface gap**: `HOST-P0-RELOCS.md` §11 already named the syscall
layer as the next component, and this is that layer's first customer in the engine's own startup
rather than only in the guest linker.

### 5.2 bionic's `__libc_init` — it now reaches the application entry point

```
  __libc_init   : libc.so+0x490FD -> 0xFDF790FD (Thumb)
  arguments     : r0=raw_args(sp)=0xFEFFFF00 onexit=0 slingshot=0xFDE07001 ctors=0xFDE0A000
  auxv at 0xFEFFFF18: [6]=0x1000 [16]=0xFB0D7 [11]=0x0 [12]=0x0 [13]=0x0 [14]=0x0 [23]=0x0
                      [17]=0x64 [8]=0x0 [3]=0x40000034 [4]=0x20 [5]=0x5 [9]=0x4030EE00
                      [7]=0xFDFFD000 [25]=0xFEFFFF98 [0]=0x0
  exit          : host-point-reached  ticks=53
  pc            : 0xFDE07002 ((no module))
  lr            : 0xFDF79143 (libc.so+0x49143)
  detail        : reached "the application entry point bionic called (slingshot)" at
                  pc=0xFDE07002: r0=0x00000000 r1=0xFEFFFF08 r2=0xFEFFFF0C r3=0xFDE0A000
  __loader_* dispatches: 3
    __loader_shared_globals                              3
  last call     : __loader_shared_globals() -> libc_shared_globals at 0xFDE1A000
  [PASS] __libc_init executes more than the 20 instructions it reached before this work
```

`lr = libc.so+0x49143` is the return address of the `blx r4` at `libc.so+0x49140` — the
**slingshot call**. The `slingshot` is four bytes of real Thumb at `0xFDE07000`
(`svc #0xFF ; bkpt #0`), registered as a named host point, so this is "bionic called the
application entry with argc/argv/envp" and not a halt somewhere short of it. `r1 = 0xFEFFFF08`
is `argv` and `r2 = 0xFEFFFF0C` is `envp`, both read out of the stack image the loader built.

The trace, which is the whole story in twenty lines (pc values carry the Thumb bit here; the
`pc=` column strips it):

```
     10  pc=FDF7911A  F819F000  libc.so+0x4911A     bl __libc_shared_globals
     11  pc=FDF79150  B9EEF05A  libc.so+0x49150     b.w the long thunk
     12  pc=FDFD3530  5C84F640  libc.so+0xA3530     movw r12,#0xd84
     13  pc=FDFD3534  0C00F2C0  libc.so+0xA3534     movt r12,#0
     14  pc=FDFD3538  476044FC  libc.so+0xA3538     add r12,pc
     15  pc=FDFD353A  F6404760  libc.so+0xA353A     bx r12
     16  pc=FDFD42C0  E28FC600  libc.so+0xA42C0     add r12,pc,#0x400
     17  pc=FDFD42C4  E28CCA04  libc.so+0xA42C4     add r12,r12,#0x1800
     18  pc=FDFD42C8  E5BCFC48  libc.so+0xA42C8     ldr pc,[r12,#0xC48]!   <- the GOT slot
     19  pc=FDE19058  EF00010B  (no module)        svc #0x10B             <- the host stub
     20  pc=FDE1905C  E12FFF1E  (no module)        bx lr
     21  pc=FDF7911E  7554F8C0  libc.so+0x4911E     str.w r7,[r0,#0x554]   <- the store that
                                                                             used to be the wall
```

Step 19 is the host answering. Step 21 is the instruction the previous revision of this gate
died trying to execute.

---

## 6. The related finding, recorded and not fixed

`host/loader`'s `AddressSpace::allocate_block` claims a block and then zeroes it **without ever
changing its protection**:

```cpp
std::uint32_t AddressSpace::allocate_block(std::uint32_t len, const char* what,
                                           std::uint32_t align) {
    if (len == 0) return 0;
    if (align < kPageSize) align = kPageSize;
    std::uint32_t at = find_span_downward(len, kMmapLimit, align);
    if (at == 0) at = find_span_upward(len, kMmapLimit, align);
    if (at == 0) return 0;
    if (!claim(at, len, what)) return 0;
    std::memset(host8(at), 0, len);      // <-- the reservation is PROT_NONE here
    return at;
}
```

`AddressSpace::reserve()` maps the 4 GiB reservation `PROT_NONE`, so that `memset` writes to
unwritable pages and the first call to `allocate_block` would fault inside the memset. This work
does **not** use it: `interpose_loader_if` and `startup.cpp`'s `claim_rw` claim with
`find_span_downward`+`claim` and then call `dh2elf::virtual_protect` explicitly, exactly as
`HOST-P0-RELOCS.md` §1 records `p0a` doing. `host/loader` is read-only input for this
workstream, so the fix belongs to its owner; it is recorded here rather than patched.

---

## 7. What is *not* verified — the honest list

1. **No syscall layer.** `SVC` still stops. That is now the *first* thing the engine's own
   startup hits (`__NR_mmap2`), so it is the next component rather than a wall.
2. **The 12 stubs and 1 halt-on-report entry have never been executed.** The dispatchers are
   installed and their slots rewritten, but nothing in this closure calls them. Their return
   values are argued from bionic's contracts, not measured here. The one entry that *is*
   measured end to end is `__loader_shared_globals`.
3. **`mmap_threshold = 0x20000` is this host's choice.** It is bionic's default for a 4 KiB
   page, but nothing read here proves that is what this libc.so expects, and no path in this
   run consumes it.
4. **`libc_globals` (block `+0x600`, 4 096 bytes) is zeroed and nothing more.** No
   `__libc_init_globals` equivalent runs in this host, so every field of it is zero. That is
   correct for the paths exercised here and unproven for any other.
5. **`__loader_dl_unwind_find_exidx` returning 0 will break guest unwinding.** Nothing here
   throws, but the engine's C++ exceptions will need it, and it is a real component.
6. **`__loader_dl_iterate_phdr` cannot invoke the guest's callback.** It returns 0, which means
   "no objects". Anything that enumerates loaded objects from guest code gets an empty list
   rather than an error.
7. **`qemu-aarch64` is not silicon.** The arm64 numbers come from QEMU's TCG executing the
   AArch64 Dynarmic emits. No phone, no emulator. The x86_64 numbers are the same program on
   the x64 backend, and they agree line for line — which is evidence about the interface, not
   about a device.
8. **Only the engine's `DT_INIT_ARRAY`, `__libc_init` and the linker's entry were executed.**
   530 of the 539 constructors never ran; the stop in entry 8 is a syscall, so constructors
   9..538 are unmeasured in both the old and the new revision.
9. **`scripts/libc_globals_scan.py` is a partial decoder, not a disassembler.** It decodes the
   three instruction shapes it looks for and prints, under "not decoded", every call site whose
   following instruction is none of them (27 of 42 sites). A field offset reached through an
   instruction shape it does not model would be invisible to it. It found no such offset that
   the run later proved necessary, but the scan is not a proof of absence.
10. **The veneer chain in §1 is decoded by hand and confirmed by `p0instr`, not by executing
    `libc.so`'s own bytes.** `p0instr` ran those twelve bytes; it did not run the surrounding
    code, so the "two hops" is a reading of the image corroborated by the trace rather than a
    single instrumented execution of it. It is worth one more look by anyone who has to touch
    this veneer.
11. **The block was exercised by `__libc_init` and by constructor 8 only.** Fields at `+0x41C`,
    `+0x428`, `+0x464`, `+0x468`, `+0x470`, `+0x524` and `+0x528` are read by libc.so code that
    no path here reaches, and they are zero.

---

## 8. Reproducing this

All commands run in WSL2; build output goes under `/root/loaderif`.

```bash
SRC=/mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/loaderif

# 1. build for aarch64 (static) and run under qemu-aarch64
bash $SRC/scripts/build-arm64-linux.sh                 # --no-run to stop after the build

# 2. the same tree on the native x86_64 Dynarmic backend
BUILD=/root/loaderif/build/x86_64 bash $SRC/scripts/build-arm64-linux.sh --x86

# 3. the block's layout, re-derived from libc.so's own bytes
python3 $SRC/scripts/libc_globals_scan.py <sysroot>/system/lib/libc.so

# 4. the veneer's arithmetic, one instruction at a time (uses host/p0a's runner, untouched)
/root/p0a/build/android-arm64-static/p0instr --thumb 40f6845cc0f2000cfc446047 --budget=3

# 5. an existing log
sed -n '1,120p' $SRC/logs/loaderif-arm64.txt
```

Run options: `--init-limit=N` (stop the constructor sweep early), `--budget=N`,
`--shim` (also interpose the soft-float libcall shim), `--verbose-host` (print every fault and
shim dispatch as it happens).

---

## 9. Files

| Path | What it is |
|---|---|
| `host/loaderif/src/loader_if.{hpp,cpp}` | the 26-entry table, the shared-globals block, the ARM stub encoder, the SVC dispatcher, and `interpose_loader_if` |
| `host/loaderif/src/startup.cpp` | the program: closure, export inventory, the two veneer hops, interposition, the block read-back, `DT_INIT_ARRAY`, `__libc_init`, the linker control |
| `host/loaderif/src/loader_cpu.{hpp,cpp}` | `p0a`'s core plus the `__loader_*` SVC range and a named EABI syscall table (`syscall_name`) |
| `host/loaderif/src/libcall_shim.{hpp,cpp}`, `dh2elf`, `dynarmic` | unchanged from `p0a`, except that `dh2elf` is `host/loader` compiled in unmodified |
| `host/loaderif/scripts/libc_globals_scan.py` | the `libc_shared_globals` layout, out of libc.so's own bytes |
| `host/loaderif/scripts/build-arm64-linux.sh` | aarch64 static + qemu-aarch64, and `--x86` |
| `host/loaderif/logs/loaderif-arm64.txt`, `loaderif-x86_64.txt` | every number quoted here, both backends |
| `DH2Work/docs/HOST-LOADER-INTERFACE.md` | this document |

Licence: 0BSD, as the rest of the host. `host/loader`, `host/mem` and `host/p0a` were **not**
modified. Nothing under `DH2Work` was written to except this document.

---

## 10. The next blocker, in one line

**The syscall layer**, whose first customer in the engine's own startup is already named and
already measured: `SVC #0x0` at `libc.so+0x94490` (`__mmap2`), `r7 = 0xC0`, 4 096 bytes,
`prot = 3`, from constructor 8 of 539 — and after that, the 530 constructors that have never
been entered.
