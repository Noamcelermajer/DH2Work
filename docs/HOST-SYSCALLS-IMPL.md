# HOST-SYSCALLS-IMPL.md — the syscall layer a 32-bit ARM guest needs, and how far startup gets

Subsystem deliverable for the tailored host. Code: `DH2Work-toolchain/host/syscalls/`
(`src/dh2sc.{hpp,cpp}`, `src/dl_bridge.{hpp,cpp}`, plus the guest CPU seam in
`src/loader_cpu.{hpp,cpp}` and the driver `src/startup.cpp`). Guest memory is
`host/loader`'s `AddressSpace`; Dynarmic is the JIT; nothing outside this tree was modified.

**Result in one line:** `__ARM_NR_set_tls` (the guest `linker`'s first syscall, at instruction
45) returns 0 and **the linker runs 145 instructions instead of 45**, reaching
`linker+0x5D5B4` = `__dl_async_safe_fatal_no_abort`; the engine's constructors still complete 8
of 539 and stop at `ld-android.so+0x1734`, which is a `udf` and not a syscall; both the arm64
and x86_64 Dynarmic backends report **27 checks, 0 failures**.

Everything below is quoted from a run on this machine. Logs are in `host/syscalls/logs/`.

---

## 1. What was built, and the three conventions that were got wrong first

| file | what it is |
|---|---|
| `src/dh2sc.hpp` / `src/dh2sc.cpp` | the layer: ARM EABI dispatch, the ARM private range, the memory syscalls over the loader's region table, a confined file layer, thread pointers, and the census |
| `src/dl_bridge.hpp` / `src/dl_bridge.cpp` | the host-call channel (`svc #(0x5A0000\|index)`), the `libdl.so` stub rewrite, and `dlopen`/`dlsym`/`dlclose`/`dlerror` against `dh2elf::Loader` |
| `src/loader_cpu.{hpp,cpp}` | `host/p0a`'s Dynarmic A32 core, plus the seam this workstream exists for: `CallSVC` → `SyscallLayer::dispatch`, the syscall layer's view of the register file, and the memory-change hook |
| `src/libcall_shim.{hpp,cpp}` | unchanged from `host/p0a`; the two dispatchers coexist because an SVC below `0x40+69` is a libcall and everything else is offered to the kernel |
| `src/startup.cpp` → `sysprobe` | the driver: sections 1–8 below |
| `src/sysinstr.cpp` → `sysinstr` | one instruction at a time through the same core |

Three conventions decide whether a syscall layer works, and all three were wrong in the first
build. They are recorded here because each failed as a *valid-looking wrong answer*:

1. **Return convention.** 32-bit ARM returns `-errno` in `r0`. `Result::err()` is the only way
   this layer produces a failure and it negates the value; an unknown number returns
   `-ENOSYS` (38), proved by a negative-control snippet rather than asserted.
2. **`movw` does not hold a syscall number.** `__ARM_NR_set_tls` is `0x000F0005`, which does
   not fit in `movw`'s 16-bit immediate. A single `movw r7, #5` is a *valid* instruction that
   asks for `close`, so the encoder emitted a plausible wrong syscall instead of failing. The
   driver now emits `movw`+`movt` when the number needs it, and
   `tools/verify-encodings.py` checks both forms against the NDK assembler.
3. **One register file, one owner.** The first build put an `Impl::syscalls` member next to
   `LoaderCpu::syscalls_`; `set_syscalls` wrote one and `CallSVC` read the other, so every
   guest SVC was reported as "not accepted by any dispatcher" while the layer sat correctly
   installed. There is now one pointer, on `LoaderCpu`, and `set_syscalls` installs the
   register file too, so a caller cannot wire the layer up and forget what it reads `r7` from.

---

## 2. The syscall inventory, and how it was derived

Two independent methods, and the difference between them is the point.

### 2.1 Static: what the guest's binaries contain

`tools/static-inventory.py` disassembles `libc.so` and `linker` in the mode their own `$a`/`$t`
mapping symbols declare (both are mixed ARM/Thumb — decoding them as pure ARM produced
plausible nonsense in an earlier audit), finds every `svc` site, and walks backwards for the
constant most recently loaded into `r7`. Names come from the ARM `asm/unistd-eabi.h` the
project's own NDK ships, via `tools/arm_nrs.py`, not from a list typed into the script.

```
$ python tools/static-inventory.py
libc.so: 146843 instructions in 3145 mode ranges, 240 svc sites
  distinct numbers in r7 at svc #0: 230
  other svc sites: {'r7-unresolved': 3}
linker: 390587 instructions in 4146 mode ranges, 273 svc sites
  distinct numbers in r7 at svc #0: 229
  other svc sites: {'imm=0x54': 2, ... 'r7-unresolved': 3}
union across 2 object(s): 230 distinct numbers
```

Every one of the 230 resolved is a real ARM EABI number (the two that the header does not
define, 90 and 102, are printed as `UNKNOWN(no __NR_ in asm/unistd-eabi.h)` rather than given
an invented name). The census is a **lower bound on what the guest contains, not on what it
issues**: `libc.so` carries a stub per syscall it can make, and the 230 are those stubs.

It is also not trustworthy site by site in ARM mode. In ARM, `mov r7, #imm` is
`0xE3A070xx`, which is *also* a plausible data word, so a linear scan can read one and report a
syscall where none exists. The `r7-unresolved: 3` and the linker's 44 "other" SVC sites are
that effect. **Which is why the inventory below is the runtime one.**

### 2.2 Runtime: what the guest actually issues

The layer counts every issue, and `sysprobe` prints the census. This is the authoritative list.

```
$ bash scripts/capture.sh linker          # logs/sysprobe-linker-arm64.txt
[sc] priv __ARM_NR_set_tls         #983045 args=FDEA1FAC FDEE8F90 00000000 00000000 FDEA1FAC FDEA1FCC -> r0=00000000  ld-android.so+0x13B04C
[sc] eabi getpid                   #20     args=00000000 FDEE8F90 00000000 00000000 FDEA1FAC FDEA1FCC -> r0=00000000  ld-android.so+0x1386CC
  syscalls    : 2 issued, 2 distinct, 0 without an implementation
    private __ARM_NR_set_tls           #983045 calls=1     handled=1     last r0=0x00000000
    eabi    getpid                     #20     calls=1     handled=1     last r0=0x00000000
```

**Two syscalls is the whole of what the guest's own compiled code issues.** The rest of the
observed inventory comes from the ABI probes in section 3, which are guest ARM32 that the
driver assembles at run time:

```
    private __ARM_NR_set_tls         #983045 calls=1 handled=1 last r0=0x00000000
    private __ARM_NR_get_tls         #983046 calls=1 handled=1 last r0=0x70001000
    eabi    uname                    #122    calls=1 handled=1 last r0=0x00000000
    eabi    (no implementation)      #4095   calls=1 handled=0 last r0=0xFFFFFFDA
    eabi    openat                   #322    calls=1 handled=1 last r0=0xFFFFFFFE
```

**Observed syscall inventory, 7 numbers, 6 of them handled:**

| range | number | name | who issues it | result |
|---|---|---|---|---|
| private | `0x000F0005` | `__ARM_NR_set_tls` | the guest `linker`, instruction 45 | `r0 = 0` |
| private | `0x000F0006` | `__ARM_NR_get_tls` | ABI probe | `r0 =` the TP `set_tls` stored |
| EABI | 20 | `getpid` | the guest `linker`, instruction ~60 | `r0 = 0` |
| EABI | 122 | `uname` | ABI probe | `r0 = 0`, `sysname="Linux"`, `machine="armv7l"` |
| EABI | 322 | `openat` | ABI probe | `-ENOENT` with no root; a real fd with one |
| EABI | 4095 | *(none)* | ABI probe, negative control | `-ENOSYS` |
| EABI | 1..5 etc. | file calls driven directly | `sysprobe` section 3 | see §5 |

The engine's 8 completed constructors and bionic's `__libc_init` issue **zero** syscalls
between them (measured: `syscalls issued during constructors: 0`), and the reason is in §6.

**The set is smaller than the static census by two orders of magnitude, and that is the
finding:** for this guest, at this point in startup, the syscall surface is
`__ARM_NR_set_tls` and `getpid`. Everything the layer implements beyond those two is either
reachable only after the blockers in §6 clear, or reached only by a test.

### 2.3 The set the layer implements anyway

`name_of()` and the dispatch table are one array, so "has an implementation" and "what is it
called" cannot drift. The implemented numbers, by what the implementation actually claims:

| claim | numbers |
|---|---|
| **real**, over the loader's address space | `brk` 45, `mmap2` 192, `munmap` 91, `mprotect` 125, `mremap` 163, `madvise` 220, `msync` 144, `mincore` 219, `mlock` 150, `munlock` 151, `mlockall` 152, `munlockall` 153, `membarrier` 389 |
| **real**, confined to a host scratch root | `open` 5, `openat` 322, `close` 6, `read` 3, `write` 4, `writev` 146, `lseek` 19, `fstat64` 197, `fstatat64` 327, `getdents64` 217, `readlinkat` 332, `getcwd` 183, `chdir` 12, `ioctl` 54, `fcntl64` 221 |
| **real**, from the host clock | `gettimeofday` 78, `clock_gettime` 263, `clock_getres` 264, `nanosleep` 162, `clock_nanosleep` 265, `getitimer` 105, `setitimer` 104 |
| **real**, elsewhere | `exit` 1, `exit_group` 248, `getrandom` 384, `ugetrlimit` 191, `prlimit64` 369, `uname` 122, `sysinfo` 116, `prctl` 172, `set_tid_address` 256, `set_robust_list` 338, `statfs64` 266, `fstatfs64` 267 |
| **modelled constant**, and the census says so | identity: `getpid` 20, `gettid` 224, `getppid` 64, `getuid32`/`getgid32`/`geteuid32`/`getegid32` 199–202, `getpgid` 132, `setpgid` 57, `setsid` 66, `umask` 60, `getgroups32` 205 — all 0 |
| **modelled constant** | scheduler: `sched_yield` 158, `sched_get_priority_max`/`min` 159/160 (`0` for SCHED_OTHER, `99` otherwise), `sched_getparam` 155, `sched_getscheduler` 157 (SCHED_OTHER), `sched_getaffinity` 242 (**one CPU**, which is a measurement: one monitor slot, one thread), `sched_setaffinity`/`sched_setscheduler`/`sched_setparam`/`getpriority`/`setpriority` accepted |
| **explicit refusal, named** | `clone` 120 → `EAGAIN`, `futex` 240 → `ENOSYS`, `tgkill` 268 → `ESRCH`, `mmap2` of a file → `ENODEV`, `MREMAP_DONTUNMAP` → `EINVAL`, `prlimit64` *changes* → `EPERM`, `ioctl` → `ENOTTY`, a path escaping the root → `EPERM` |
| **no entry** | anything else → `-ENOSYS`, counted as a gap rather than as handled |

The distinction between "handled" and "modelled" is in the census, not in a comment: a
`calls=N handled=N` line for `sched_getaffinity` means an implementation ran and answered; a
`handled=0` line means the guest got `-ENOSYS` and the layer is telling on itself.

### 2.4 `set_elimination`, which is not an optimisation knob

A guest sequence that sets six registers and then executes `svc #0` loses those writes on the
arm64 backend: Dynarmic reasons that nothing in the *translated* code reads them, because the
instruction that reads them is a host callback. The guest then traps with **correct**
`pc`/`r7`/`lr` and **zeroed** argument registers — the least diagnosable shape a failure can
take, and exactly what the first file-I/O probe did. `LoaderCpu::set_set_elimination(false)`
clears `OptimizationFlag::GetSetElimination`, and the ABI test sets it, so the test is
independent of which backend runs it. This is the same guard patch `0001-A` addresses
(`a32_address_space.cpp`, `!conf.check_halt_on_memory_access`).

---

## 3. The wiring host/dl documented as missing

`host/dl`'s own header says its `dispatch_svc` is never reached because "nothing routes
Dynarmic's `CallSVC` into it". Two distinct things were missing:

1. **The routing.** `LoaderCpu::CallSVC` now offers every SVC to, in order: the libcall shim
   (`0x40..0x84`), then `SyscallLayer::dispatch` (which itself gives the `0x5A0000|index`
   channel first refusal through a chain callback), then a registered host point, and finally
   stops with a named report. The pc is already past the SVC when the callback runs, `r0` is
   written and the guest continues — no return sequence is emulated.
2. **A guest-side body that raises the call.** In this sysroot `libdl.so`'s `dlopen` is a
   Thumb trampoline into the guest linker, whose body is `udf`. `dh2sc::DlBridge::install()`
   rewrites the five `dl*` exports of the loaded `libdl.so` to `svc #(0x5A0000|index) ; bx lr`
   and records the 16 bytes it replaced.

The test reads the immediate back **out of guest memory** rather than from the encoder, and
normalises the two encodings (ARM `svc #imm24` carries the base in its comment field; Thumb
`svc #imm8` carries the whole index and no base):

```
$ bash scripts/capture.sh arm64 -- --root=/tmp/dh2root     # logs/sysprobe-arm64.txt
  dh2sc::DlBridge: host-call channel 0x5A0000 | index, 5 export(s) rewritten
    libdl.so           dlopen               at 0xFDEFE911 (Thumb) call index 1, replaced 8 byte(s): 4770DF01 00000000 ...
    libdl.so           dlsym                at 0xFDEFE923 (Thumb) call index 2, replaced 16 byte(s): 4770DF02 00000000 ...
    libdl.so           dlclose              at 0xFDEFE93F (Thumb) call index 3, replaced 16 byte(s): 4770DF03 00000000 ...
    libdl.so           dlerror              at 0xFDEFE91B (Thumb) call index 4, replaced 8 byte(s): 4770DF04 00000000 ...
    libdl.so           android_dlopen_ext   at 0xFDEFE957 (Thumb) call index 5, replaced 8 byte(s): 4770DF05 00000000 ...
    the stub's own instruction at 0xFDEFE910 is word 0x4770DF01 (Thumb) -> SVC immediate 0x5A0001
    the immediate reached the dispatcher: yes; dlopen("liblog.so") -> r0=0xFDEF0000
    opens=1 syms=0 errors=0 last_error=""
  [PASS] the dl bridge's SVC immediate reached its dispatcher
  [PASS] libdl.so's exports were rewritten to host-call stubs
```

**Does the `dl` path work?** The routing works and is proved end to end: a `svc` word that a
guest would execute reaches `DlBridge::dispatch`, `dlopen` resolves an already-loaded object
and returns its load bias (`0xFDEF0000`, which is what this game reads out of a handle —
`libStormGLOFT.so` dereferences `handle[0x8c]` as the module bias, measured in
`HOST-IMPORT-SURFACE.md` §4.2). What is **not** proved: that the *engine's* `dlopen` reaches
it, because nothing in this startup path calls `dlopen` (`host/dl`'s own note records the same
absence). And the routing is wired into `sysprobe`'s section 8 only — a product run has to
install the bridge and call `set_chain`, which is one line but is not exercised by any guest
this workstream runs.

**Why not use `host/dl`'s `dlcore` directly?** Because it owns a `dh2rt::Loader` with its own
guest address space, so every handle, `dlerror` buffer and symbol address it returned would be
valid in a *different* guest than the one the JIT is executing. Addresses from the wrong
address space are worse than no answer. `dh2sc::DlBridge` is a smaller bridge over the same
`dh2elf::Loader` the JIT runs on; the ABI constants, the stub encoding and the write-span rule
are `host/dl`'s, and the span bound exists because that tree recorded the defect it prevents
(this sysroot's Thumb stubs are 10 bytes apart, so a 16-byte write destroys the next one).

---

## 4. The ABI, executed rather than asserted

Section 3 of `sysprobe` claims one page of guest memory, writes five ARM32 snippets into it
and runs them through the same core as everything else. The snippets are printed out of guest
memory, and every word in them is re-derived from the NDK assembler by
`tools/verify-encodings.py` (`14 ok, 0 mismatched`).

```
$ bash scripts/capture.sh arm64 -- --root=/tmp/dh2root
3. the syscall layer's ABI: hand-assembled ARM32, executed
  snippet 0xFDF00000: E3007005 E340700F EF000000 E12FFF1E
  snippet 0xFDF00010: E3007006 E340700F EF000000 E12FFF1E
  snippet 0xFDF00020: E300707A 00000000 EF000000 E12FFF1E
  snippet 0xFDF00030: E3007FFF 00000000 EF000000 E12FFF1E
  snippet 0xFDF00040: E3007142 00000000 EF000000 E12FFF1E
  __ARM_NR_set_tls(0x70001000) -> r0=0x00000000
  [PASS] __ARM_NR_set_tls returns 0
  [PASS] the layer's thread pointer is the value the guest wrote
  [PASS] the CP15 shim and the syscall layer agree on one thread pointer
  __ARM_NR_get_tls()           -> r0=0x70001000
  [PASS] __ARM_NR_get_tls returns what set_tls stored
  uname(...)                   -> r0=0x00000000 sysname="Linux" machine="armv7l"
  [PASS] uname fills the guest's struct with the guest's own architecture
  syscall 0xFFF (no such call) -> r0=0xFFFFFFDA (-38)
  [PASS] an unknown syscall returns -ENOSYS in r0 (32-bit ARM's negative-errno rule)
  openat(AT_FDCWD, "/etc/passwd", 0) -> r0=0xFFFFFFFE (-2)
  [PASS] with a root, an absolute guest path resolves inside the root, not on the host
```

Note what `set_tls` proves: the value the guest wrote is the value the CP15 shim serves from
then on, so `mrc p15,0,Rt,c13,c0,3` and `__ARM_NR_get_tls` cannot disagree. That is the one
invariant this layer shares with the CPU.

---

## 5. The file layer, driven directly

No guest code in this image issues a file syscall (§2.2), so the file half is proved by calling
the same implementations `dispatch` calls, with guest memory as the argument and result space.
That covers the logic, not the routing.

```
$ bash scripts/capture.sh arm64 -- --root=/tmp/dh2root
  the file syscalls, driven directly (root "/tmp/dh2root"):
    openat(AT_FDCWD, "/level.txt", O_RDONLY) -> 100
    read(fd, buf, 32)                          -> 0x0000000F (15)
  [PASS] read returns the file's 15 bytes ("dungeon hunter\n")
    the buffer now holds: "dungeon hunter
"
  [PASS] the bytes read are the file's own bytes
    fstat64(fd, &st)                           -> 0x00000000, st_size=15 (read at +0x30 of the 104-byte struct)
  [PASS] fstat64 fills the ARM kernel stat64 at the offsets the guest reads
    lseek(fd, 3, SEEK_SET)                     -> r0=0x00000003 r1=0x00000000
  [PASS] lseek returns a 64-bit offset in r0:r1, as the 32-bit ABI requires
    getcwd(buf, 32)                            -> 0xFDE0C500, "/"
  [PASS] getcwd answers with the guest's own root, not the host's working directory
    openat(AT_FDCWD, "/mevel.txt", 0)          -> 0xFFFFFFFE (-2)
  [PASS] a missing file in the root is ENOENT
    openat(AT_FDCWD, "/../etc/passwd", 0)      -> 0xFFFFFFFF (-1)
  [PASS] a path that escapes the root is EPERM, and never resolves on the host
    close(fd)                                  -> 0x00000000
  [PASS] close succeeds
  [PASS] closing the same fd twice is EBADF, because the fd table is real
```

Three decisions worth naming, because each is a place a port is silently wrong:

* **`st_size` is at `+0x30`, not `+0x28`.** The ARM kernel's `stat64` is
  `{u64 dev; u32 pad; u32 ino; u32 mode; u32 nlink; u32 uid; u32 gid; u64 rdev; u32 pad; i64 size; ...}`
  — 104 bytes with `size` at 48. An x86-64-shaped struct reads the wrong field and reports a
  file size of 0, which a guest-sized buffer then turns into a short read that looks like EOF.
* **`lseek` returns 64 bits in `r0:r1`.** It calls `set_r1` explicitly rather than relying on a
  stale high word.
* **A guest path is confined.** `..` is `EPERM`, every path is resolved under the root, and
  with no root configured every `open*` is `ENOENT` — the guest never sees the host's own
  filesystem by accident. `chdir` is accepted and *recorded*, because every path is resolved
  against the root, and claiming otherwise would be a lie about every subsequent open.

---

## 6. How far startup now gets, and the exact next stop

Run: `bash scripts/capture.sh arm64 -- --root=/tmp/dh2root` (arm64 static under
`qemu-aarch64`), `capture.sh x86_64` for the cross-backend comparison. Both: **27 checks,
0 failures**.

### 6.1 Section 7 — the guest linker, where the first syscall came from

Before: 45 instructions, then `SVC #0` with `r7=0x000F0005`, reported as "no guest kernel".
Now:

```
  linker      : bias 0xFDEAA000 [0xFDEAA000, 0xFE000000) entry 0xFDF05BC0 rel.dyn 2 rel.plt 2 packed 4215
  relocations : total 4219 applied 4219  relocate_all -> ok
[sc] priv __ARM_NR_set_tls         #983045 ... -> r0=00000000
[sc] eabi getpid                   #20     ... -> r0=00000000
  exit        : guest-memory-fault  ticks=145
  pc          : 0xFDF075B4 (ld-android.so+0x5D5B4)   lr=0xFDF075D7
  detail      : read of 4 byte(s) at 0x00154B30 is outside every mapped region
  r0..r3      : 0x00000000 0xFDEB11C8 0xFDEA1D1C 0x00000000  sp=0xFDEA1D08
  syscalls    : 2 issued, 2 distinct, 0 without an implementation
  [PASS] the linker got past instruction 45, where it stopped at SVC #0 before
```

**`set_tls` returns 0 and the linker proceeds 100 more instructions** (45 → 145), issuing
`getpid` on the way. The next exact stop:

| | |
|---|---|
| `pc` | `0xFDF075B4` = `linker+0x5D5B4` (the log's `ld-android.so+0x5D5B4` is a reporting artefact: the two objects' mappings overlap and the label walks the closure first) |
| function | `__dl_async_safe_fatal_no_abort`, at its first instruction (`sub sp, #0xC`, `80b5` = `push {r7,lr}`) — resolved by `tools/where.py` |
| `lr` | `0xFDF075D7` = `linker+0x5D5D7`, i.e. inside that same fatal-report routine |
| instruction | the Thumb halfword pair at `0x5D5B4`; the fault itself is the preceding `str r0, [r7, #0x34]`, which addresses `0x00154B30` |
| what it means | the guest linker reached **its own fatal error path** and then faulted writing to its output buffer. It did not fail on a syscall: `0x00154B30` is a low address, not a syscall argument, and the routine it is in exists to print a message. |
| what it needs next | `host/loader`'s loader to satisfy it rather than the guest linker: this host *emulates* the linker by design (`HOST-RUNTIME-LINKER-THREADS.md` §2), and running the guest's own linker is the opposite choice. The bridge is the 26 `__loader_*` stubs of §6.3. |

This is a measurement of a different subsystem's boundary, not a syscall gap: `syscalls: 2
issued, 2 distinct, 0 without an implementation`.

### 6.2 Sections 5 and 6 — the engine's constructors, and `__libc_init`

```
  init[0..7] = 0x4030EE00, 0x4030EE20, 0x4030EE40, 0x4030EE64, 0x403103D4, 0x403103F4, 0x40310420, 0x40310730
      -> returned-to-sentinel, 8 insns each
  init[8] = 0x40310C28 (ARM) -> undefined-instruction       260 insns  pc=0xFDFFE734 (ld-android.so+0x1734)
      UndefinedInstruction at pc=0xFDFFE734 (sentinel=0x20000000 lr=0xFDF9ABC5 sp=0xFEFFFCB0 cpsr=0x200000F3 r0=0x00000010 r1=0x00000001 r2=0x00000000)
  init functions: 539 present, 9 entered, 8 completed, 324 instructions
  syscalls issued during constructors: 0
```

Unchanged from `host/p0a`, and the reason is now named rather than inferred: entry 8's `lr` is
`libc.so+0x11ABC5`, i.e. inside bionic, and the pc it died at is `ld-android.so+0x1734` — the
2-byte `udf` body of that file's `__loader_*` trampolines. Every bound GOT slot pointing there
is printed by the driver and they are all `__loader_*` (`__loader_add_thread_local_dtor`,
`__loader_dlopen`, …). **The constructor calls into the loader shim, the shim's body is a
trap, and no syscall is involved.**

`__libc_init`:

```
  __libc_init = libc.so+0x490FD -> guest 0xFDF790FD (Thumb state)
  arguments  : r0=raw_args=sp=0xFEFFFF00, onexit=0, slingshot=0xFDE0A001, ctors=0xFDE0E000 (zeroed)
  exit       : guest-memory-fault  ticks=1
  pc         : 0xFDF790FC (libc.so+0x490FC)
  detail     : write of 4 byte(s) at 0xFEFFFEFC is outside every mapped region
  r0..r3     : 0xFEFFFF00 0x00000000 0xFDE0A001 0xFDE0E000   sp=0xFEFFFEEC lr=0x20000000 cpsr=0x000000F3
  syscalls   : 0 issued, 0 distinct, 0 without an implementation
```

One instruction in, `__libc_init` writes through a stack pointer that has moved down to
`sp-0x10`; `0xFEFFFEFC` is 4 bytes *below* the start of the initial-thread block
(`0xFEFFF000`). The same routine ran 20 instructions under `host/p0a` because that run passed a
different `sp`. **This is a stack the guest linker would have built, not a syscall the layer
refuses** — reported, not asserted, and named in §8.

### 6.3 Every backend, every check

```
$ tail -2 logs/sysprobe-arm64.txt        host/syscalls: 27 checks, 0 failures   (exit status 0)
$ tail -2 logs/sysprobe-x86_64.txt       host/syscalls: 27 checks, 0 failures   (exit status 0)
$ tail -2 logs/sysprobe-linker-arm64.txt host/syscalls: 18 checks, 0 failures   (exit status 0)
```

The arm64 and x86_64 logs are byte-identical except for the artifact banner and the build
paths, which is the property worth having: the layer's behaviour does not depend on which
Dynarmic backend emits the guest's code. The runs before the `set_set_elimination` fix did
differ (§2.4) — the arm64 build passed the `set_tls` check but failed the file probe, the x86_64
build passed both — and finding that was the single most valuable thing this workstream did.

The section that is **not** a check, deliberately: `__libc_init`'s stop is printed with a `note`
line rather than a `[PASS]`/`[FAIL]`, because it is neither this layer's progress nor this
layer's bug, and asserting it either way would be a false claim in one direction or the other.

---

## 7. Reproducing this

```bash
# build + run on arm64 (Linux NDK r29 clang -> static aarch64 -> qemu-aarch64)
bash host/syscalls/scripts/build-arm64-linux.sh

# the same tree, native x86_64 (the x64 Dynarmic backend, for contrast)
bash host/syscalls/scripts/build-arm64-linux.sh --x86

# run a build against the real inputs (no long command line to quote)
bash host/syscalls/scripts/run.sh                       # --arm64, --build DIR, -- ARGS
bash host/syscalls/scripts/run.sh --arm64 -- --root=/tmp/dh2root --syscall-trace

# every log this document quotes, written by the run itself
bash host/syscalls/scripts/capture.sh arm64  -- --root=/tmp/dh2root
bash host/syscalls/scripts/capture.sh x86_64 -- --root=/tmp/dh2root
bash host/syscalls/scripts/capture.sh linker        # the linker's trace, with --syscall-trace
bash host/syscalls/scripts/capture.sh static        # the static census (Windows Python: capstone+pyelftools)

# the two tools that turn a claim into a check
python host/syscalls/tools/static-inventory.py      # what the guest's binaries contain
python host/syscalls/tools/verify-encodings.py      # every hand-written ARM word, vs the NDK assembler
python host/syscalls/tools/where.py <object> <offset>   # object+offset -> function name + disassembly
```

The file fixture for the `--root` runs is one 15-byte file:

```bash
rm -rf /tmp/dh2root && mkdir -p /tmp/dh2root && printf "dungeon hunter\n" > /tmp/dh2root/level.txt
```

The arm64 build reuses `host/p0a`'s staged Dynarmic at `/root/p0arm/src/dynarmic` (pin
`86458a0b` + patch `0001-A`) rather than re-staging it; `scripts/build-arm64-linux.sh` asserts
the 0001-A guard is present before it configures, and refuses to build without it.

**Host rules honoured.** Linux x86_64 in WSL2 for the build and the x86_64 run; the arm64 run
is the same tree compiled by the Linux NDK clang into a static aarch64 Android binary and
executed by this container's `qemu-aarch64`. **No phone was used and no emulator was
installed.** Nothing under `host/p0a`, `host/p0`, `host/p0arm`, `host/mem`, `host/loader`,
`host/rt`, `host/jni`, `host/dl`, `host/harness`, `host/shrink`, `host/integrate`, `DH2Work`,
`DH_sc`, `DH_sc-pr` or `DH2Work-stage` was modified.

---

## 8. What is unverified, and what would settle it

1. **The full syscall set is not exercised.** 7 numbers were observed; ~90 have an
   implementation. The unexercised ones are unexercised *because nothing reaches them*: the
   engine's constructors and `__libc_init` issue zero syscalls, and the guest linker issues two.
   Fixing §6.2's `__loader_*` trap is what would put the file, time and memory syscalls under a
   real guest. Until then, "implemented" for `mremap`, `mincore`, `getdents64`, `clock_gettime`
   and the rest means "written and reasoned about", not "observed working".
2. **A file syscall has never been issued by guest code through the SVC path.** §5 proves the
   implementations; §4's `openat` snippet proves the routing. The two are not joined by a run.
3. **`futex` and `clone` are refusals, so guest threads do not exist here.** `host/rt` has
   clone semantics, a real futex join and a measured TLS layout, but nothing wires them to the
   JIT in this tree. `pthread_create` from the engine would get `EAGAIN` from `clone` and the
   guest's own error path. **The single biggest unknown is the futex operation encoding:**
   bionic's futex wrapper applies its own `FUTEX_*_PRIVATE` numbering, and whether the shipped
   `libc.so` translates it before the `svc` or not decides whether a private futex op arrives
   as 128 or 129. The static inventory did not settle it (the stub's `r7` never resolved) and no
   run has reached a futex, so this is unresolved rather than merely untested. A guest thread
   that calls `pthread_mutex_lock` is what would answer it.
4. **`mmap2` is anonymous-only.** A file-backed mapping is `ENODEV`, deliberately: this host has
   no page cache for guest files, and serving one as anonymous memory would be a silent lie
   about what the guest will read. `MAP_SHARED`, `shmem`, `memfd_create` and `msync` on a file
   mapping are therefore not modelled.
5. **`mprotect` refuses a range that is not exactly one region.** A partial protection change
   would have to split a region; refusing is the safe half of the two wrong answers, but it is a
   divergence from a kernel a guest could notice.
6. **The file layer is confined rather than general.** There is no `stat`, `unlinkat`, `mkdirat`,
   `renameat`, `fstatat64` on a directory, or `O_DIRECT`; `chdir` does not change resolution;
   `ioctl` is always `ENOTTY`. A guest that expects a real root filesystem (bionic reading
   `/system/etc/ld.config.arm.txt`, or the engine's asset path) will not find one until the
   loader work of §6.2 lands.
7. **The CP15 thread pointer and the layer's thread pointer are the same value, but only one
   thread exists.** `set_tls` is honoured verbatim; a guest that sets a malformed TP gets no
   complaint, which matches the kernel but means a TLS bug will surface later and elsewhere.
8. **`prlimit64` refuses changes** (`EPERM`) and reports a fixed table on read; `ugetrlimit`
   returns 32-bit `rlim_t` values that were not checked against what this guest's bionic
   expects.
9. **`sysinfo` and `statfs64`/`fstatfs64` return zeroed structs of a plausible size.** They are
   accepted rather than refused, and the census marks them handled — a guest that reads uptime
   or free space gets zeros.
10. **`dlopen` is wired but nothing calls it.** §3. A product run must install
    `DlBridge::install()` and `set_chain` on the live core. `dlclose` returns `-1` rather than
    unmapping a live module, and `android_dlopen_ext`'s `ext` struct is refused.
11. **The `ld-android.so+…` label in the census for a linker pc is wrong** (it prints the
    closure's module that overlaps that address, not the linker's) — a reporting defect in
    `sysprobe`'s `describe()`, named here so the log is read with it in mind. `tools/where.py`
    gives the right answer.
12. **`qemu-aarch64` is not hardware**, and the arm64 evidence is one run of a static binary
    under QEMU's TCG. No device executed any of this.
