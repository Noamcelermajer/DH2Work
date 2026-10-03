# HOST-OWNHOST-PROGRESS.md — the merge, the two walls, and how far startup now gets

**What this is.** `host/loaderif/` and `host/syscalls/` landed in separate copies of the same
base (`host/p0a`) and neither tree had both advances. They are now one tree,
`DH2Work-toolchain/host/merge1/`, and this is what that tree does when it runs the real
closure with the host's own loader driving it.

**Result in one line.** The merge works, and the two walls are both gone: **all 539 of the
engine's constructors complete** (was 8 of 539), **bionic's `__libc_init` runs to its own
`slingshot`** (was one instruction), and the next stop is the **guest linker's own fatal path**
— which is a *diagnosed* stop with a named cause, not a wall.

Everything below is quoted from runs on this machine. Logs: `host/merge1/logs/`.

---

## 0. The merge: what was in each tree, and what the merged `CallSVC` does

| | `host/loaderif` | `host/syscalls` | merged |
|---|---|---|---|
| `__loader_*` table (`loader_if.{hpp,cpp}`) | yes | — | yes |
| syscall layer (`dh2sc.{hpp,cpp}`) | — | yes | yes |
| `dl` host-call channel (`dl_bridge.{hpp,cpp}`) | — | yes | yes |
| libcall shim | yes | yes (identical file) | yes |
| `LoaderCpu` | `loader_if_dispatch` hop | `set_syscalls`, `set_set_elimination` | **both, one `CallSVC`** |
| the `udf` at `ld-android.so+0x1734` | gone | still reached | **gone** |
| `__ARM_NR_set_tls` | no syscall layer | returns 0, linker runs 145 | **returns 0** |

`LoaderCpu::CallSVC` now offers every SVC to four disjoint owners, in this order:

```
     1. the libcall shim            SVC #0x40 .. 0x40+69
     2. the `__loader_*` table      SVC #0x100 .. 0x100+25      <- host/loaderif's hop
     3. the syscall layer           SVC #0, and #0x5A0000|index <- host/syscalls' hop
     4. a registered host point     SVC #(whatever the driver named)
```

The ranges are disjoint, so no dispatcher can take another's immediate. The merge is a
*source* merge, not a link of two binaries: `src/loader_cpu.cpp` is `host/syscalls`' copy with
the `loader_if_dispatch` hop added at step 2, and `src/loader_if.cpp` is `host/loaderif`'s copy
unmodified. `src/startup_loaderif.cpp` is `host/loaderif`'s own driver, kept buildable so the
merge can be checked against that tree's published numbers as well as this one's.

**Four merge defects were found by running it, and each is a class worth naming.**

1. **A lost region.** The first merged build interposed the `__loader_*` interface and then
   *replaced* the region list instead of extending it, dropping the libcall shim's page. The
   engine's constructor 8 then stopped with `NoExecuteFault at 0xFDE0B130` — the shim stub it
   had just been relocated to call (`__aeabi_atexit`), 24 instructions in. Region bookkeeping
   is load-bearing; `regions` is now cumulative and only ever appended to.
2. **A cache drop from inside a JIT callback.** The syscall layer's `mmap2` calls
   `note_memory_changed()`, which called `Jit::ClearCache()` — and it runs *inside* Dynarmic's
   `CallSVC`, i.e. from host code the JIT itself emitted. The block that code lives in is the
   block being freed. Measured: the engine's first `mmap2` (`r7=0xC0`, 0x80000 bytes) reached
   the memset and the process died with `Unhandled SIGSEGV at pc 0x4974c8`, inside the host's
   own `__memset_aarch64_mops`. The cache drop is now **deferred**: `note_memory_changed()`
   sorts the region table immediately and sets a flag the run loop consumes between translated
   blocks, which is the only place it is safe.
3. **Claimed but not mapped.** `AddressSpace::claim` records pages in the loader's bitmap and
   nothing else — the 4 GiB reservation is `PROT_NONE`. Every other component in this project
   follows a claim with `dh2elf::virtual_protect`; the syscall layer's four claimers (`brk`,
   `mmap2`, `mremap`, and `mprotect`'s table) did not. That is the *same* SIGSEGV as (2), and
   it is a host fault, not a guest one: the memset wrote to `0x00010000`, a guest page the
   bitmap said was claimed and the MMU still said was inaccessible. `SyscallLayer::claim_guest`
   now does claim + protect in one place, with the permission taken from the guest's own `prot`.
4. **A TCB nobody was filling in.** See §2 — this is the one that unblocked startup.

---

## 1. Wall 1 — the "4-byte stack underflow", and what it actually was

### 1.1 What the stop was

```
  __libc_init = libc.so+0x490FD -> guest 0xFDF790FD (Thumb state)
  arguments  : r0=raw_args=sp=0xFEFFFF00, onexit=0, slingshot=0xFDE05001, ctors=0xFDE0E000
  exit       : guest-memory-fault  ticks=1
  pc         : 0xFDF790FC (libc.so+0x490FC)
  detail     : write of 4 byte(s) at 0xFEFFFEFC is outside every mapped region
```

`ticks=1`, and `pc` is the address of `__libc_init`'s **first** instruction. Instrumenting the
memory callbacks (`DH2_LC_DEBUG_WRITE`, a hook this tree carries) shows what "one instruction"
really is:

```
      [wr] a=FEFFFEEC n=4 v=0        pc=FDF790FC sp=FEFFFF00 -> FAULT
      [wr] a=FEFFFEF0 n=4 v=0        pc=FDF790FC sp=FEFFFF00 -> FAULT
      [wr] a=FEFFFEF4 n=4 v=0        pc=FDF790FC sp=FEFFFF00 -> FAULT
      [wr] a=FEFFFEF8 n=4 v=0        pc=FDF790FC sp=FEFFFF00 -> FAULT
      [wr] a=FEFFFEFC n=4 v=20000000 pc=FDF790FC sp=FEFFFF00 -> FAULT
```

Five stores, one per register, from `push {r4,r5,r6,r7,lr}` at `0xFDF790FC` — Dynarmic's arm64
backend emits the decrement and the five stores separately, which is why the reported `sp` is
still the pre-push value and why the fourth store is at `0xFEFFFEF8`, not the fifth. So the
guest's *first* write, to its own stack, was refused.

### 1.2 The stack was not short, and the block was not misplaced

`host/rt`'s measured layout is what this host builds, and the run confirms every element of it:

```
  thread thr : stack_base=0xFE800000 stack_top=0xFF000000 tls_base=0xFF000000 tls_size=20 tp=0xFF000014 initial_sp=0xFEFFFF00
  tls        : TP 0xFF000014  [TP]=0xFF000014  [TP+4]=0xFF000014  errno(TP+0x29C)=0  (page 0xFF000000, mprotect span 0x2B4)
```

* the initial-thread block is `[KStackTop-8MiB, KStackTop)` = `0xFE800000..0xFF000000`, exactly
  8 MiB, and the static TLS sits **below** TP at `[0xFF000000, 0xFF000014)`;
* libc.so's canary slot `[tp-4]` = `0xFF000010` is inside that block, as measured;
* `errno` at `TP+0x29C` is a field of the control block at `TP`, which is the measured
  `pthread_internal_t` layout.

`0xFEFFFEFC` is inside the block, not below it, and the region table a different part of the
same core printed does contain it:

```
  page for 0xFEFFFEFC: claimed=yes; region=initial-thread stack+TLS prot=6
  region 0xFE800000..0xFF010000 prot=6 initial-thread stack+TLS   <-- CONTAINS THE ADDRESS
```

**So the fault was not the layout. It was the lookup.** The callbacks found the region with a
`std::upper_bound` binary search over a table that is maintained sorted — and returned null.
Measured, with both lookups in the same call:

```
      [fault] write of 4 byte(s) at 0xFEFFFEEC is outside every mapped region
      [fault] pc=FDF790FC sp=FEFFFF00 regions=69 r=0x0 again=0x0 again_prot=0 a+len=FEFFFEF0
```

`region_containing` is now a **linear scan** (69 regions; the cost is nothing). With that one
change the same five stores succeed and `__libc_init` runs to completion. The binary-search
version's failure is reproducible and I did not reduce it to a single cause: the table it is
handed contains the region, and the same search over the same vector returns null. What is
*not* in doubt is the effect, and the fix cannot have that failure mode — a lookup that is
silently wrong exactly when an ordering invariant is broken is worse than one that is slower
and cannot be.

**Reporting rule this establishes:** "the write is N bytes below the block" is a symptom, and
the block's arithmetic was right. The census of *all* writes the instruction makes, and the
table the callback consults, are what name the cause.

### 1.3 The next stop, and the TCB nothing was filling in

With the lookup fixed, `__libc_init` stops one instruction further along, in constructor 8 of
539 rather than in `__libc_init` at all:

```
  init[8] = 0x40310C28 (ARM) -> guest-memory-fault  15659 insns  pc=0xFDF7C714 (libc.so+0x4C714)
      write of 4 byte(s) at 0x0000029C is outside every mapped region
  registers  : r0=0xFFFFFFFF r1=0x00000000 ... r7=0x00010000
  insns      : at 0xFDF7C714: 1F70EE1D 68494240 029CF8C1 30FFF04F
```

The relocation was pointing at `0x29C`, which is `errno`'s offset with a zero thread pointer.
The guest's own libc.so says why:

```
0004c714 <__set_errno_internal>:
   4c714: ee1d 1f70   mrc  p15, #0x0, r1, c13, c0, #0x3   ; r1 = TP
   4c71a: 6849        ldr  r1, [r1, #0x4]                  ; r1 = *(TP+4)
   ...                str  r0, [r1, #0x29c]               ; errno is a field of *that*
```

`dh2elf` builds the initial thread's **stack image** and leaves the static TLS block zeroed. In
a real process bionic's own `__init_tls` fills the control block; this host emulates the linker
rather than running it, so **nothing did**. `*(TP+4)` was 0. This is the measured-layout gap the
task named, and it is 4 bytes wide in the same sense: the block was there, the *slot in it* was
not. `startup.cpp`'s `setup_initial_tls()` now writes the measured layout before any guest
instruction runs:

```
  tls         : TP 0xFF000014  [TP]=0xFF000014  [TP+4]=0xFF000014  errno(TP+0x29C)=0
```

One trap worth recording: that write is `mprotect` over `[TP, TP+0x2A0)`, which is not
page-aligned, and `mprotect` refuses an unaligned address. It is aligned down to the page
containing TP and spans `0x2B4` bytes; the first attempt printed
`could not make the control block writable -- refusing` and was silently a no-op.

**After this, all 539 constructors complete.**

---

## 2. How far startup gets now

Default run (`merge1 --dump-cpu`), real output:

```
  tls         : TP 0xFF000014  [TP]=0xFF000014  [TP+4]=0xFF000014  errno(TP+0x29C)=0
  init functions: 539 present, 539 entered, 539 completed, 824421 instructions
  syscalls issued during constructors: 27
  __libc_init = libc.so+0x490FD -> guest 0xFDF790FD (Thumb state)
  exit       : host-point-reached  ticks=53
  pc         : 0xFDE05002 ((no module))
  lr         : 0xFDF79143 (libc.so+0x49143)          <- the slingshot call's return address
  detail     : reached "the application entry point bionic called (slingshot)" at pc=0xFDE05002:
               r0=0x00000000 r1=0xFEFFFF08 r2=0xFEFFFF0C r3=0xFDE0E000
```

* **constructors: 539 of 539** (was 8), 824,421 guest instructions, 27 syscalls — and the census
  is a real one rather than a token: `getrandom` ×2, `clock_gettime`, `sched_getaffinity`,
  `mmap2` ×8, `prctl` ×8, `munmap` ×2, `futex` ×1, `fcntl64` ×4. The engine's own allocator is
  mapping and unmapping guest memory through the layer, so the syscall layer is now exercised
  by the engine's startup rather than only by its own ABI probe:

```
  init functions: 539 present, 539 entered, 539 completed, 824421 instructions
  syscalls issued during constructors: 27
    eabi    getrandom                  #384    calls=2     handled=2     last r0=0x00000004
    eabi    clock_gettime              #263    calls=1     handled=1     last r0=0x00000000
    eabi    sched_getaffinity          #242    calls=1     handled=1     last r0=0x00000004
    eabi    mmap2                      #192    calls=8     handled=8     last r0=0x00380000
    eabi    prctl                      #172    calls=8     handled=8     last r0=0xFFFFFFEA
    eabi    munmap                     #91     calls=2     handled=2     last r0=0x00000000
    eabi    futex                      #240    calls=1     handled=1     last r0=0xFFFFFFDA
    eabi    fcntl64                    #221    calls=4     handled=4     last r0=0xFFFFFFF7
```
* **`__libc_init`: 53 instructions and it reaches the application entry point** with
  `argc=0`, `argv=0xFEFFFF08`, `envp=0xFEFFFF0C`, i.e. the three arguments bionic's slingshot
  takes, read out of the stack image the loader built;
* **`DT_INIT_ARRAY`: 9 entries reached before this merge's own stop; with it, all 539.**

**The `udf` is gone.** `ld-android.so+0x1734` is never reached: the `__loader_*` interface's
19 bound slots are redirected into a claimed stub page, read back out of guest memory:

```
  interposed   : 19 of 26 entries; not imported: 9
  slots now in the stub page: 19 of 19; still at the udf: 0
  [+0x410 argc = 1] [+0x414 auxv = 0xFEFFFF18] [+0x554 prog name] [+0x55C short name] [+0x520 mmap thresh = 0x20000]
```

**Checks: `host/syscalls: 25 checks, 0 failures`** (the merged driver's own census), and the
loader-interface half's driver still builds and runs as `loaderif_check`.

---

## 3. Wall 2 — why the guest linker aborts

### 3.1 The stop

```
  7. the guest linker's entry: __ARM_NR_set_tls, and what comes next
  linker      : bias 0xFDEAA000 [0xFDEAA000, 0xFE000000) entry 0xFDF05BC0 rel.dyn 2 rel.plt 2 packed 4215
  [sc] priv __ARM_NR_set_tls   #983045 -> r0=00000000
  [sc] eabi getpid             #20     -> r0=00000000
  exit        : guest-memory-fault  ticks=145
  pc          : 0xFDF075B4 (linker+0x5D5B4)   lr=0xFDF075D7 (linker+0x5D5D7)
  detail      : read of 4 byte(s) at 0x00154B30 is outside every mapped region
```

Read as Thumb (the object is 92% Thumb, and `llvm-objdump` asked for a mid-section range decodes
it as ARM and prints nonsense — which is why this tree carries `tools/thumbdis.py`):

```
  0005D5B4  B083              sub sp, #0xC
  0005D5B6  B580              push {r7,lr}
  0005D5B8  B083              sub sp, #0xC
  0005D5BA  4684              mov r12, r0
  0005D5BC  A805              add r0, sp, #0x14
  0005D5BE  C00E              stmia r0!, {r1,r2,r3}
  0005D5C0  AA05              add r2, sp, #0x14
  0005D5C2  4661              mov r1, r12
  0005D5C4  480B              ldr r0, [pc, #0x2C]      ; -> 0x0005D5F4
  0005D5C6  4478              add r0, pc               ; -> 0x0005D5F4
  0005D5C8  6800              ldr r0, [r0, #0x0]
  0005D5CA  6800              ldr r0, [r0, #0x0]        ; <- the fault
```

`0x0005D5F4` holds `0x000E8966`; `bias + 0xE8966` holds `0x00145F30`; and the word at
`0x00145F30` is **`0x00154B30`** — a link-time address inside the linker's own `.bss`
(`.bss` is `0x14A000..0x1559E4`). So the routine is
`*(*(GOT-style word))`, and what it got was an address in the object's own link-time image.

### 3.2 What I ruled out, with measurements

* **"The loader's RELR never ran."** It ran and it is correct. Decoding `.relr.dyn`
  (`SHT_ANDROID_RELR`, `0x840`/`0x39C`) independently — `tools/relr.py`, `tools/relrlist.py`,
  and in-run — gives **4215 slots**, and the loader reports **4215 applied**. Every marked slot
  holds *its file addend plus the load bias*, checked in-run, with the arithmetic visible:
  `link+0x140E70`: file addend `0x00009DD6` + `0xFDEAA000` = `0xFDEB3DD6` = the runtime word.
  A second claim in that check — "4138 slots still hold a link-time value" — is a **bug in the
  check, not a finding**: those words carry non-zero addends, so `value >= base` is the wrong
  test (`link+0x140E74` is `0x93A8E960`, which is `0x00006960 + 0xFDEAA000`). The correct test,
  file addend + bias, matches for every slot examined.
* **"A RELR bitmap I mis-decoded."** Both decodings of the same 0x39C bytes — the RELR bitmap
  and the Android packed form the loader also tries — produce the same 4215 offsets, which is
  what makes the "the loader decoded it as the other format" theory testable and false.
* **"The word is below the entry point, so it is data."** It is at `0x00145F30`, which is
  inside `.got` (`0x145EF0..0x1463C4`) — a slot the loader writes and the guest code loads.

### 3.3 The finding

**`0x00145F30` is not marked by `.relr.dyn` and has no entry in `.rel.dyn` or `.rel.plt`**
(`tools/packed.py`, `tools/dyn.py`, `tools/relrlist.py` all agree; `tools/pokescan.py` finds
exactly one word in the whole object holding `0x00154B30`, and that word is this one, at
`0x00145F30`, with `reloc=NONE`). It is a **writable word that points into the object's own
mapped image and that no relocation in the object covers** — so it keeps its link-time value
and the guest dereferences it.

The class is not one word. The same shape repeats across the linker's `.got`
(`0x0012099D`, `0x00143850`, `0x001439F0` …). Measured: a pass that rebases every writable word
whose value points into this object's own image and which no relocation marked changes **5247
words**, and the linker then runs **2599 instructions instead of 145** — past its fatal path,
past `writev` (`2 calls, handled`), `mmap2`, `getpid`/`gettid`/`getuid32`, and into a *named*
environment failure instead of a wild dereference:

```
  rebase pass  : 5247 writable word(s) pointed into this object and no relocation covered them; rebased
  [sc] eabi writev  #146 -> r0=0000002A
  Could not find a PHDR: broken executable?
  libc: Could not find a PHDR: broken executable?
  [sc] eabi (no implementation) #281 -> r0=FFFFFFDA
  [sc] eabi (no implementation) #175 -> r0=FFFFFFDA
  [sc] eabi (no implementation) #363 -> r0=FFFFFFDA
  [sc] eabi (no implementation) #174 -> r0=FFFFFFDA
  [sc] eabi exit_group #248 args=0000007F -> the linker exits 127
```

That is the guest linker *reporting its own diagnosis*, which is what the task asked for, and it
names the next check: `__linker_init` cannot find the `PT_PHDR` of the executable it is
bootstrapping. Its auxv is this host's own and says `AT_PHDR = 0x40000034` — the **engine's**
program headers, because the engine is what this host loaded as the main object
(`auxv at 0xFEFFFF18: [3]=0x40000034`). A real Android linker bootstraps the *executable's*
`soinfo` first and expects that PHDR to describe the object it is running for; here the
"executable" is a shared library the loader placed, and the linker's own view of what it is
booting does not match what this host built. **That is an environment/EMULATION boundary, not a
relocation bug** — and `HOST-RUNTIME-LINKER-THREADS.md` §2 already chose the other side of it
("this host *emulates* the linker by design"), which is why the engine's own startup, which is
what the product needs, does not go through the guest linker at all.

**The rebase pass is off by default.** It is a heuristic over data words that only the object's
own relocation tables can classify, it is right about the one word measured and unproven about
the other 5246, and it changes the linker's `.data` rather than the host's behaviour. It is
behind `--rebase-gap-words` so the number above is reproducible and the default run stays
honest.

**The next exact stop, with the default run:** the linker's fatal path,
`pc=0xFDF075B4` (`linker+0x5D5B4`), `lr=0xFDF075D7`, the Thumb halfwords `B083 B580` at that
address, and the faulting read is the `ldr r0,[r0,#0x0]` at `linker+0x5D5CA` addressing
`0x00154B30`. Nothing in the **engine's** startup stops any more: all 539 constructors complete
and `__libc_init` reaches the slingshot.

---

## 4. The two "worth doing if reached" items

* **`libc_globals` at block `+0x600`.** Still zeroed by the interface, and this run says the
  answer to "does the guest read it" is **no for every path reached here**: all 539
  constructors and all 53 instructions of `__libc_init` complete with it zero, and the only
  fields `libc.so` read in this run are `+0x410`, `+0x414`, `+0x520`, `+0x554`, `+0x55C`, all
  of them filled. `HOST-LOADER-INTERFACE.md` §7.4's list of offsets that no path reaches
  (`+0x41C`, `+0x428`, `+0x464`, `+0x468`, `+0x470`, `+0x524`, `+0x528`) is unchanged. It is
  still debt, not a blocker.
* **`dl_unwind_find_exidx` returns 0.** Not reached. Nothing threw, and the stop is a memory
  fault in the linker's fatal path, not an unwinder call. Still debt.
* The syscall layer's region list and the JIT's are still two copies — `regions_ref()` is the
  core's and the JIT's table is a snapshot taken at construction, so a region a syscall adds
  (`mmap2`, `brk`) is visible to the *syscall layer's* own reads and not to the JIT's memory
  callbacks until the core is rebuilt. Nothing in this run touches an `mmap2`ed page through
  the guest, so it did not bite; it is the same class as merge defect 1 and it is next on the
  list.

---

## 5. What is not verified — the honest list

1. **`qemu-aarch64` is not silicon.** The arm64 numbers are QEMU's TCG executing the AArch64
   Dynarmic emits. No phone, no emulator, no device executed any of this.
2. **The binary-search failure in `region_containing` is reproducible and unreduced.** The
   table contains the region, the same search over the same vector returns null, and a linear
   scan over the same vector finds it. The fix is not in doubt; the mechanism is. Anyone who
   puts a binary search back into that path should reproduce it first.
3. **The rebase pass is a heuristic.** 5247 words, one of them proven necessary by the stop it
   removes; the rest are unclassified. Off by default.
4. **"539 of 539 constructors complete" does not mean the engine started.** A constructor that
   returns to the sentinel has completed *as executed here*; whether it did the work a device
   would do depends on the 27 syscalls it made and on the fields of the interface that are
   stubs (12 of 26 entries, `HOST-LOADER-INTERFACE.md` §7.2).
5. **The `dl` path is still wired but unused by the engine**, and the merged driver installs
   `DlBridge::install()` in its own section only. A product run must install it on the live
   core.
6. **The syscall layer's ~90 implementations are still mostly unexercised.** This run observed
   27 issues across the constructors against 7 before — real progress, and still a small
   fraction of the table. `futex`, `clone`, file I/O through the SVC path and everything
   reached only by a test remain "written and reasoned about".
7. **The `__loader_*` stubs have still never been executed.** 19 slots are redirected and read
   back out of guest memory, but nothing in this closure calls 12 of the 26 entries, and the
   ones it does call (`__loader_shared_globals`, `__loader_add_thread_local_dtor`) are the
   real ones.
8. **The guest linker's `PHDR` stop is diagnosed, not fixed.** "The linker's view of what it is
   bootstrapping does not match what this host built" is what `writev` printed. Anything that
   needs the guest linker to complete its own `__linker_init` will need that addressed, and
   the project's stated design is not to run the guest linker at all.
9. **Only the engine's `DT_INIT_ARRAY` and `__libc_init` were executed.** The engine's own
   entry (`DT_INIT` = `0x40000000`) and everything after it — the JNI surface, the plugin
   entry — were not reached by this driver.

---

## 6. Reproducing this

```bash
# build the merge for aarch64 (static) and run it under qemu-aarch64
bash host/merge1/scripts/build-arm64-linux.sh

# the same, without running
bash host/merge1/scripts/build-arm64-linux.sh --no-run

# the x64 Dynarmic backend, for contrast
bash host/merge1/scripts/build-arm64-linux.sh --x86

# the two runs this document quotes
B=/root/merge1/build/android-arm64-static
$B/merge1 --engine=<engine> --sysroot=<sysroot> --guest-lib=<guestlib> --dump-cpu
$B/merge1 --engine=<engine> --sysroot=<sysroot> --guest-lib=<guestlib> --dump-cpu --rebase-gap-words

# the tools that turned each claim into a measurement (all read the guest's own bytes)
python3 host/merge1/tools/dyn.py      <linker>            # its dynamic + relocation tables
python3 host/merge1/tools/relr.py     <linker> --slot 0x145F30
python3 host/merge1/tools/relrlist.py <linker> --lo 0x140E70 --hi 0x149400
python3 host/merge1/tools/packed.py   <linker> --slot 0x145F30
python3 host/merge1/tools/pokescan.py <linker> 0x154B30
python3 host/merge1/tools/thumbdis.py <linker> 0x5D5B4 12
bash    host/merge1/scripts/xcallers.sh 0x5D5B4

# the runtime knobs the diagnosis used, all off by default
DH2_LC_DEBUG_WRITE=FEFFFEE0,FEFFFF00   # every guest write in a range, with pc/lr/sp
DH2_LC_DEBUG_FAULT=1                   # the region table the core consults at a refusal
DH2_SC_DEBUG_SVC=1                     # the first 64 SVCs with swi/r7/pc
```

**Files.** `host/merge1/src/loader_cpu.cpp` (the four-hop `CallSVC`, the linear region lookup,
the deferred cache drop), `src/dh2sc.cpp` (`claim_{guest}`, the protect-after-claim fix),
`src/startup.cpp` (`setup_initial_tls`, the loader-interface section, the diagnostics),
`src/loader_if.cpp` (unmodified from `host/loaderif`), `src/startup_loaderif.cpp` (that tree's
driver, kept buildable), `tools/` and `scripts/` (the readers above).

**Licence.** 0BSD, as the rest of the host. Nothing outside `host/merge1/` was modified. No
phone was used and no emulator was installed.
