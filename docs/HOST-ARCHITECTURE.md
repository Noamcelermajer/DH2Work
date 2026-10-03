# Host architecture: replacing ZettaBridge with a purpose-built Dynarmic host

**Status:** build plan, pre-implementation. Every claim about existing code carries a `file:line`.
Reasoned statements are marked **INFERRED**; things that cannot be settled without a measurement or a
device run are marked **UNKNOWN**. Measured numbers state the method that produced them.

**Governing constraint.** ZettaBridge is read as reference and **must not be shipped or derived
from**. Nothing in this plan vendors, copies, links, patches or modifies ZettaBridge. Where a
ZettaBridge artefact is the *only* convenient source of a non-ZettaBridge file (the ARM32 sysroot),
§1.5 requires re-deriving it from its actual upstream instead of copying it out of the ZettaBridge
bundle.

---

## Path legend

| Alias | Root |
| --- | --- |
| `ZB` | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\research\ZettaBridge` (upstream `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` + `zettabridge-dh2.patch`) |
| `DYN` | `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\dynarmic` (Vita3K clone at `86458a0bd369d63ba4c2ef812cacbb6c9080c065` — verified with `git rev-parse HEAD`) |
| `GAME` | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\original\lib\armeabi-v7a` |
| `SYSROOT` | `ZB\build\launcher\assets\zb\sysroot` (the extracted ARM32 bionic tree) |
| `BUNDLE` | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\fold7-build\runtime-bundle.zip` |
| `WORK` | `C:\Users\NacWorkstation\Documents\DH2Work` |
| `DOCS` | `WORK\docs` |
| `TOOLCHAIN` | `C:\Users\NacWorkstation\Documents\DH2Work-toolchain` |

**What was read vs measured.** Source was read with `read`/`grep`/`glob`. Counts marked *measured*
were produced by `pyelftools` 0.33 / `capstone` 5.0.9 over the actual shipped binaries; the scripts
and their JSON output are in `C:\Users\NacWorkstation\Documents\DH2Work-scratch7\`.

---

## 0. Executive summary

| Question | Answer |
| --- | --- |
| Can the ARM32 bionic sysroot be reused as shipped? | **Yes, the bytes — but re-derive them from AOSP, not from the ZettaBridge bundle** (§1.5). |
| How much of the 579-index SVC surface is reachable by this game? | **92 (15.9%)**; 487 indices (84.1%) are dead and can be dropped (§2.2). |
| How much of the GL/EGL stub surface? | **92 of 404 hostcall+extension GL/EGL entry points**; **zero** EGL, **zero** GLES1-only, **zero** libandroid/NDK (§2.3). |
| How many guest relocation types must the new ELF loader implement? | **Four** — `R_ARM_RELATIVE`, `R_ARM_ABS32`, `R_ARM_JUMP_SLOT`, `R_ARM_GLOB_DAT` (§1.2). |
| What is the hardest component? | The synthesized `JNIEnv`/`JavaVM` bridge plus guest thread/TLS/`pthread_*` emulation — not the JIT hook (§5.2). |
| Big-bang or incremental? | **Incremental, alongside the working build** (§5.4). |
| What is the fallback? | The current shipping APK, untouched, at 25 FPS (§5.5). |

---

## 1. Component inventory

Verdict vocabulary: **REIMPLEMENT** (we write it, informed by what the existing one must do),
**REIMPLEMENT-TRIM** (we write a strict subset), **RE-DERIVE** (obtain the same artefact from its
own upstream, not from ZettaBridge), **DROP** (not needed for this one game), **KEEP-OURS** (a
behaviour that must be preserved but lives in `WORK`, not in the host).

### 1.0 Inventory table

| # | Capability | Existing implementation (size) | Verdict |
| --- | --- | --- | --- |
| 1 | Guest memory model | `ZB core/src/guest_memory.cpp` (130 lines) + `guest_memory.h` (67) | REIMPLEMENT |
| 2 | Guest ELF loading | `ZB core/src/elf_loader.cpp` (125) | REIMPLEMENT |
| 3 | Relocations / dynamic symbols | `elf_fixups.cpp` (12 737 B) + `elf_symbols.cpp` (13 621 B) | REIMPLEMENT-TRIM to 4 reloc types |
| 4 | Guest linker, `dlopen`/`dlsym`, library mode | `library_runtime.cpp` (23 009 B) + `library_runtime.h` (108 lines) + `library_protocol.h` + `ZB guest/zbhost/zbhost.c` (98) | REIMPLEMENT (hardest) |
| 5 | Guest syscall layer | `ZB core/src/syscalls.cpp` (1 754 lines, 147 distinct syscall numbers) | REIMPLEMENT-TRIM to the measured set |
| 6 | ARM32 bionic sysroot | `SYSROOT` (10 files, 4 169 016 B) | RE-DERIVE from AOSP |
| 7 | Synthesized `JNIEnv`/`JavaVM` → ART | `core/src/jni/*` (2 752 lines over 16 files) + `ZB guest/zbjni/zbjni.c` (751) + generated `.S`/tables | REIMPLEMENT (largest) |
| 8 | Guest→host call channel (SVC) | `ZB core/src/process.cpp:67-87,514-568`, `ZB core/src/gen/hostcalls.inc` (579 entries) | REIMPLEMENT-TRIM to 92 |
| 9 | GLES/EGL marshalling | `gl_manual.cpp` (1 539) + generated + `gl_backend.h` (831) + `gl_driver_backend.cpp` + `host_gl.cpp` (117) + `host_egl.cpp` (251) + `egl_manual.cpp` (468) | REIMPLEMENT-TRIM to 92 GLES2 functions; **DROP EGL** |
| 10 | GL diagnostics | `ZB core/src/gl/gl_diagnostics.cpp` (1 768 lines, 89 581 B) | DROP |
| 11 | Asset / sensor / input / configuration / window / looper / native-activity backends | `ZB core/android/*` (2 314 lines) + `ZB core/src/android/*` | **DROP** (unreachable — §2.3) |
| 12 | Guest thread creation and TLS | `process.cpp:630-731` + `guest_thread.cpp` (345) + `guest_thread.h:117-172` + carrier/borrower in `library_runtime.h:81-106` | REIMPLEMENT |
| 13 | Signal handling | `ZB core/src/signals.cpp` (13 362 B) | REIMPLEMENT-TRIM |
| 14 | Crash reporting / runtime report | `runtime_report.cpp` (26 917 B) + `runtime_report.h` (12 406 B) | REIMPLEMENT-TRIM (~15% of it) |
| 15 | Hang watchdog | `ZB core/src/hang_watchdog.cpp` (12 667 B) | REIMPLEMENT-TRIM or drop (see below) |
| 16 | `/proc`, `/sys`, kuser page emulation | `ZB core/src/proc_files.cpp` (5 995 B), `process.cpp:157-171` | REIMPLEMENT-TRIM |
| 17 | CP15 / CP14 emulation | `ZB core/src/cp15.cpp` (2 468 B) + `cp15.h` | REIMPLEMENT |
| 18 | Guest entry stack / auxv | `ZB core/src/initial_stack.cpp` (71) | REIMPLEMENT (trivial) |
| 19 | Plugin / launcher loading path | `ZB android/launcher/app/src/main/java/**` (24 files, 2 863 lines) + `core/android/zbproxy.c` | REIMPLEMENT as our own app (see below) |
| 20 | Compatibility behaviours | `WORK app/guest-java/**`, `WORK app/launcher-java/**`, `WORK patches/**` | KEEP-OURS |
| 21 | Host CLI | `ZB cli/zbrun/main.cpp` (70 lines) | DROP (not a candidate platform: `ZB cli/zbrun/main.cpp:74-79` builds a bare `Process` with no host-call handler and no JNI runtime) |

### 1.1 Guest memory model → REIMPLEMENT

**Starting facts, verified.**

* One reservation, `kGuestSpaceSize = 1ULL << 32` (`ZB core/include/zb/guest_memory.h:8`) plus
  `kGuardSize = 64 * 1024` (`ZB core/src/guest_memory.cpp:14`), taken with
  `mmap(nullptr, kGuestSpaceSize + kGuardSize, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0)`
  (`ZB core/src/guest_memory.cpp:38-45`). **Confirmed exactly as briefed.**
* `kPageSize = 4096`, `kPageMask = 0xFFF` (`ZB core/include/zb/guest_memory.h:9-10`); the flag bitmap
  is one byte per 4 KiB page, i.e. **1 MiB** (`ZB core/src/guest_memory.cpp:38`).
* A guest address `g` is host address `base() + g` (`ZB core/include/zb/guest_memory.h:31-32`).
* `host_prot` maps guest `PROT_EXEC` to host `PROT_READ` only — "the JIT reads guest code through
  host memory, so guest EXEC implies host READ; host memory is never executable"
  (`ZB core/include/zb/guest_memory.h:27-29`, implemented at `core/src/guest_memory.cpp:31-36`).
* Guest `mmap` is served by **highest-fit-first**: `find_free` walks the bitmap downward from `limit`
  and returns the highest run of the requested length, never below `kLowestAllocPage = 0x10000 >> 12`
  (`ZB core/src/guest_memory.cpp:91-104`; documented at `guest_memory.h:50-51` and analysed at
  `DOCS/GUEST-MEMORY-INVESTIGATION.md:114-136`). **Confirmed.**
* Bounded regions: `kStackTop = 0xFF000000`, `kStackSize = 8 MiB`, `kMmapLimit = 0xFE000000`,
  `kExecutableLimit = 0x40000000`, `kMaxThreads = 256` (`ZB core/include/zb/process.h:31-36`).
  **`mmap_limit = 0xFE000000` confirmed.** It is a `static constexpr`, not configurable
  (`DOCS/GUEST-MEMORY-INVESTIGATION.md:148-149`).
* Fastmem is on for every thread: `cfg.fastmem_pointer = mem.base()`
  (`ZB core/src/guest_thread.cpp:58`).
* The unmap path never returns the range to the host OS — it `mmap`s `PROT_NONE` over it
  (`ZB core/src/guest_memory.cpp:82-89`).

**What we must reproduce.** The page-flag model and `find_free` are ~60 lines of mechanical code
and can be rewritten from the description above. The *non-mechanical* parts are the three behaviours
the investigation identified as load-bearing, and they are the reason this is REIMPLEMENT and not
"trivial":

1. `mmap` with an explicit hint is honoured if `addr >= 0x10000`, page-aligned,
   `addr + size <= mmap_limit` and the range is free; otherwise `find_free`
   (`ZB core/src/syscalls.cpp:294-342`, per `DOCS/GUEST-MEMORY-INVESTIGATION.md:94-104`).
2. A failed `brk` returns the **old** break rather than an error (`ZB core/src/syscalls.cpp:274-292`).
3. The 16 KiB-page problem: the shipped runtime requires a **4 KiB host page**, which
   `Dh2Activity` enforces before launch (`DOCS/ARCHITECTURE.md:152-155`). Our host must keep that
   precondition or adopt the unfinished 16 KiB design in `WORK patches/16k-port/*.patch`
   (`DOCS/ARCHITECTURE.md:128`) — see the note in §1.2.

**Genuine improvement available to us.** Because guest code is never executed natively
(`guest_memory.h:27-29`), our loader can keep guest `.text` pages **host-readable and
host-writable**. That removes the writability question that `NATIVE-HOOK-MECHANISM.md:123` flags as
UNKNOWN for the offline-patch route, and it makes §4.7 (runtime-installed SVC stubs) possible
without touching the engine file at all.

**Also inherited, not to be rediscovered:** the long-run failure mode. `DOCS/GUEST-MEMORY-INVESTIGATION.md:302-350`
concludes the best-supported explanation for the intermittent 1.4 MB `malloc` failure is 32-bit guest
address-space fragmentation, with ranked fixes at `:360-455`. Whatever we do about it, our host must
not make it worse, and a `mmap`-reuse improvement (option B, `:388-407`) is a legitimate early win we
get for free by owning the allocator.

### 1.2 ELF loading → REIMPLEMENT (four relocation types, measured)

`ZB core/src/elf_loader.cpp` is 125 lines and self-contained: validate ELF32/LE/`EM_ARM`
(`:29-33`), accept `ET_EXEC`/`ET_DYN` (`:34-37`), compute the `PT_LOAD` span (`:46-56`), place
`ET_DYN` at `mem.find_free(span, dyn_limit)` (`:63-74`), map the whole span anonymous RW
(`:77`), `memcpy` each `PT_LOAD` (`:83-85`), read `PT_INTERP` (`:86-93`), then re-protect each
segment per `p_flags` (`:95-102`), and derive `PT_PHDR` or synthesise it from `e_phoff`
(`:104-114`).

**Measured relocation census** (`pyelftools` over the pristine `GAME` libraries — the complete set,
since the shipped tree contains exactly three `armeabi-v7a` libraries):

| Library | Total relocs | `R_ARM_RELATIVE` | `R_ARM_ABS32` | `R_ARM_JUMP_SLOT` | `R_ARM_GLOB_DAT` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `libDungeonHunter2.so` | 54 463 | 49 132 | 4 969 | 351 | 11 |
| `libStormGLOFT.so` | 7 603 | 1 917 | 3 956 | 1 345 | 385 |
| `libnativeinterface.so` | 23 | 6 | 0 | 16 | 1 |

There are **no TLS relocations, no `R_ARM_COPY`, no `R_ARM_IRELATIVE`, and no ARM/Thumb branch
relocations** in any of the three. `NATIVE-PORT-SHORTLIST`-era figures are consistent:
`DOCS/PERFORMANCE-BASELINE.md:72-75` records 54 463 relocations for the engine, which matches the
measured total exactly. **Consequence: the guest relocation engine is four cases, not a bionic
linker.** That is a real, measured reduction in the cost of component 3.

**What `elf_symbols.cpp`/`elf_fixups.cpp` really do that we will need.** They walk the ELF program
headers, `PT_DYNAMIC`, symbol/string tables and `R_ARM_JUMP_SLOT` relocations "instead of private
linker fields" — that is the replacement for Storm's inline hook's `soinfo` load-bias read
(`ZB patches/storm/storm_import_fix.c`, described at `DOCS/ARCHITECTURE.md:124`). Our loader must
expose the same public-ELF-only symbol resolution so the Storm byte patch keeps working; the patch
resolves the exported `JNI_OnLoad` and subtracts its exact symbol VA
(`DOCS/ARCHITECTURE.md:123`), so **our loader must place and report symbol VAs identically in
convention** (bias = load address − lowest `p_vaddr`, matching `elf_loader.cpp:70`).

**16 KiB warning, inherited.** `DOCS/ARCHITECTURE.md:152-155`: "16 KiB ELF `LOAD` alignment alone
does not make the mapper 16 KiB-safe." Our loader inherits the 4 KiB requirement unless we adopt
`WORK patches/16k-port/*.patch` (`DOCS/ARCHITECTURE.md:128`, "Not integrated into the shipped
launcher"). Plan assumption: **4 KiB host pages**, enforced by our activity.

### 1.3 Guest linker, `dlopen`/`dlsym`, library mode → REIMPLEMENT (hardest subsystem)

This is where the guest stops being a plain process and becomes a *plugin*. `library_runtime.h`
documents the model:

* `LibraryRuntime` is process-lifetime; "Guest threads cannot be torn down, so after a successful
  `start()` the destructor logs and aborts" (`ZB core/include/zb/library_runtime.h:31-34`, and
  `ZB core/src/jni/proxy_runtime.cpp:450-453` aborts in the analogous destructor).
* `set_host_call_handler` "Receives every host call outside the runtime range 0xFE00-0xFEFF"
  (`library_runtime.h:42-43`); that range is `library_protocol.h:12-15` (READY, CARRIER PARK),
  per `DOCS/NO-FORK-NATIVE-OPTIONS.md:102`.
* `load_library` / `find_symbol` are guest `dlopen`/`dlsym`/`dlerror` on the service thread
  (`library_runtime.h:46-49`).
* The **carrier / borrower** model: a host thread that came from Java leases a guest pthread parked
  in PARK and calls into the guest on it (`library_runtime.h:55-58`, `:81-84`). Each lease costs
  **two processor ids and two JITs — a 2 MiB carrier JIT and a 32 MiB borrower JIT**
  (`library_runtime.h:81-84`), matching `kCarrierCodeCacheSize = 2 MiB` /
  `kDefaultCodeCacheSize = 32 MiB` (`ZB core/include/zb/guest_thread.h:35-36`).
* `is_borrower` exists because "a borrower came from Java and has its own real Android looper, so
  guest loopers prepared on it cannot be driven by us" (`library_runtime.h:69-74`).

**Why this must be reimplemented rather than assumed away.** The engine is loaded as a *library*
whose `JNI_OnLoad` is called by ART inside our own process; `RegisterNatives` must reach real ART;
and the guest calls back into Java. That is the carrier/borrower problem, and it is intrinsic, not a
ZettaBridge design choice. **INFERRED:** we can simplify it if measurement shows the game only ever
enters the guest from one Java thread at a time and never needs a guest looper; that has to be
measured, not assumed (see §2.6 and the P1 gate).

**`zbhost`** (`ZB guest/zbhost/zbhost.c`, 98 lines) is the ARM32 guest-side service entry. We need an
equivalent: a small ARM32 executable we build and ship inside the bundle, which the guest linker
starts and which parks in our READY host call. It is small; it must be ours.

### 1.4 Syscall layer → REIMPLEMENT-TRIM

`ZB core/src/syscalls.cpp` is 1 754 lines and dispatches **147 distinct ARM EABI syscall numbers**
via `case NR_*` labels (161 `case` labels including aliases), using numbers generated into
`ZB core/src/gen/syscall_nrs_arm.h` (426 lines / 423 `NR_*` constants, "Generated by
`tools/gen_syscalls.py` from the NDK arm EABI header"). The names table is
`ZB core/src/gen/syscall_names_arm.inc` (421 entries).

Measured composition of the dispatcher (function starts, `ZB core/src/syscalls.cpp`):

| Group | Representative entries (line) |
| --- | --- |
| read/write family | `sys_read_write:216`, `sys_pread_pwrite:227`, `sys_readv_writev:236`, `sys_lseek:768`, `sys_llseek:761` |
| memory | `sys_brk:274`, `sys_mmap2:294`, `sys_mremap:357`, `sys_munmap:424`, `sys_mprotect:435`, `sys_madvise:448`, `sys_msync`, `sys_mlock/munlock` |
| signals | `sys_rt_sigaction:457`, `sys_rt_sigprocmask:479`, `sys_sigaltstack:499`, `sys_send_signal:510`, `sys_sigsuspend:542`, `NR_rt_sigreturn`, `NR_sigreturn`, `NR_rt_sigtimedwait`, `NR_rt_sigpending` |
| time | `sys_clock_get:567`, `sys_gettimeofday:575`, `sys_clock_nanosleep:587`, `sys_nanosleep:597`, plus the `_time64` variants |
| stat / fs | `sys_stat_common:607`, `sys_statfs_common:614`, `sys_fstatat64:631`, `sys_statx:639`, `sys_path_call:784`, `sys_two_paths:792`, `NR_openat`, `NR_getdents64`, `NR_readlinkat`, `NR_mkdirat`, `NR_unlinkat`, `NR_renameat*`, `NR_faccessat*` |
| ioctl / fcntl / prctl / rlimit | `sys_ioctl:647`, `sys_fcntl64:665`, `sys_prctl:681`, `sys_ugetrlimit:746`, `sys_prlimit64:753` |
| threads / futex | `NR_clone`, `sys_futex:711`, `NR_set_tid_address`, `NR_set_robust_list`, `NR_sched_yield`, `NR_sched_getaffinity`, `NR_sched_setaffinity`, `NR_gettid` |
| polling | `sys_poll:802`, `sys_select:826`, `sys_epoll_wait:1059`, `NR_epoll_create1/ctl/pwait`, `NR_ppoll*`, `NR_pselect6*`, `NR_eventfd2`, `NR_timerfd_*` |
| sockets | `sys_sockaddr_in:860`, `sys_sockaddr_out:867`, `sys_sendto:884`, `sys_recvfrom:892`, `sys_setsockopt:918`, `sys_getsockopt:932`, `sys_sendmsg:1023`, `sys_recvmsg:1036`, plus `NR_socket/socketpair/bind/connect/listen/accept*/shutdown/getpeername/getsockname` |
| misc | `sys_uname:704`, `NR_getrandom`, `NR_memfd_create`, `NR_sysinfo`, `NR_personality`, `NR_ARM_cacheflush`, `NR_ARM_get_tls`, `NR_ARM_set_tls` |

**The three ARM-specific entries are mandatory for us regardless of trimming**:
`NR_ARM_cacheflush`, `NR_ARM_get_tls`, `NR_ARM_set_tls` are how ARM32 bionic uses
`__kuser_cancel_...`/`__ARM_NR_set_tls`; the guest `tp` is a guest VA that must land in `tpidruro`
(`ZB core/src/guest_thread.cpp:51`).

**How to determine the set we actually need — exactly, and not by estimate.** See §2.4. The static
route is *not* trustworthy at this scale; the runtime route is exact.

**Behaviours that are emulation, not forwarding** (so they must be re-derived, not copied by
inspection of the syscall name): the 32-bit struct translations `fill_stat64`
(`syscalls.cpp:175`), `sys_statfs_common` (`:614`), `read_timespec`/`write_timespec` (`:196`,`:211`),
the sockaddr/msghdr 32→64 conversions (`:860-1058`), and the `time64` duplicates. These are exactly
the places a from-scratch host gets silently wrong; they are also the places the differential
harness cannot help (they are host-facing, not guest-computable).

### 1.5 ARM32 bionic sysroot → RE-DERIVE (reuse the bytes, from AOSP)

**This is the question with the clearest answer, and the answer is: the sysroot is not ZettaBridge's.**

`ZB tools/extract_sysroot.sh:1-6` states it plainly:

```
# Extracts the arm32 bionic sysroot used by zbrun from an AOSP aosp_arm64 GSI (Android 17 QPR2).
URL=https://dl.google.com/developers/android/cinnamonbun/images/gsi/aosp_arm64-exp-CP41.260814.003.B1-16166531-e6cb3bc5.zip
SHA=e6cb3bc521fb4a8b4c8e62f8557c6ae0ff10662a6838cfb346a32ec9c9134e22
```

and it pulls exactly 10 files: `/system/bin/bootstrap/linker` and `/system/lib/bootstrap/{libc,libm,libdl,libdl_android}.so`
plus `/system/lib/{ld-android,liblog,libz,libc++,libstdc++}.so` (`extract_sysroot.sh:30-37`). Note the
comment at `:28-29`: "`/system/lib/{libc,libm,libdl}.so` are symlinks into the runtime APEX; the
bootstrap copies are regular files built from the same bionic."

**Measured contents of `SYSROOT`** (all `ELF32`, `EM_ARM`, `ET_DYN`):

| File | Bytes | Exported dynsyms | Undefined |
| --- | ---: | ---: | ---: |
| `system/bin/linker` | 2 059 744 | 30 | 3 |
| `system/lib/ld-android.so` | 2 868 | 26 | 1 |
| `system/lib/libc++.so` | 849 492 | 2 332 | 175 |
| `system/lib/libc.so` | 992 808 | 1 739 | 14 |
| `system/lib/libdl.so` | 5 452 | 17 | 14 |
| `system/lib/libdl_android.so` | 3 344 | 9 | 10 |
| `system/lib/liblog.so` | 42 964 | 64 | 89 |
| `system/lib/libm.so` | 127 092 | 297 | 7 |
| `system/lib/libstdc++.so` | 10 304 | 13 | 20 |
| `system/lib/libz.so` | 74 948 | 99 | 22 |
| **Total** | **4 169 016** | | |

Provenance is recorded in the files themselves: `libc.so` and `linker` both carry
`clang version 22.0.2` / `rustc version 1.96.0-dev (ac68faa20 2026-05-25)` in `.comment`, and
`libc++.so` carries `Android r29 14206865`. These are AOSP build artefacts from a Google GSI image,
**not** ZettaBridge-authored files.

**Verdict: RE-DERIVE.** We write our own ~40-line extractor with the same URL and the same pinned
SHA-256, and we do **not** copy the files out of the ZettaBridge tree. This is both the cleanest
provenance position and trivially reproducible. The `libc++.so` entry (849 492 B, 20% of the sysroot)
is likely droppable — nothing in `GAME`'s `DT_NEEDED` lists names it (the engine needs
`libc, libGLESv2, libstdc++, libm, libGLESv1_CM, libdl, liblog`; Storm adds `libz, libEGL, libandroid`)
— but confirm at P0 before trimming, since a guest stub library we build may need it.

**Note on `WORK build/build_apk.py:104-112`.** The current build duplicates four sysroot libraries
into `system/lib/arm/` and `system/lib/arm/bootstrap/` for the x86_64 emulator's ARM native-bridge
path. Our host does not need that emulator accommodation; it can be dropped, which also removes four
duplicate files from the manifest.

**Whether the licence permits redistribution.** AOSP bionic is Apache-2.0/BSD; the project already
ships third-party notices (`DOCS/LAYOUT.md:350-354` records the `notices/` directory, and
`WORK build/build_apk.py:174-184` writes `assets/licenses/*` including transitive dependency
licences). **INFERRED:** redistribution is fine, but this document is not a legal opinion — the
`WORK RIGHTS.md` caveat quoted at `DOCS/NO-FORK-NATIVE-OPTIONS.md:626` applies. Action: add an
explicit AOSP/bionic notice entry when the sysroot moves out of the ZettaBridge-derived bundle.

### 1.6 Synthesized `JNIEnv`/`JavaVM` bridge to ART → REIMPLEMENT (largest subsystem)

**Shape.** Three layers, only one of which is per-game:

1. **Guest-side ARM32 `libzbjni.so`** — `ZB guest/zbjni/zbjni.c` (751 lines) plus generated tables
   (`gen/hostcalls.S`, 59 `svc` stubs; `gen/hostcalls.h`; `gen/tables.inc`). This builds a real
   `JNINativeInterface` function table **in guest memory**, whose slots are SVC stubs, and publishes
   `zb_jni_guest_api {size, version, new_env_fn, free_env_fn, java_vm}` through a `Register` host call
   (`ZB core/include/zb/jni_protocol.h:35-42`). So "the synthesized `JNIEnv`" is a translated ARM32
   construct, not a host pointer table.
2. **The ABI between them** — `jni_protocol.h:11-14`: `0xFB00..0xFBFF` are *"`JNINativeInterface`
   slots configured as host stubs (`0xFB00 + slot`)"* and `0xFC00..0xFCFF` are *"the flat JNI host
   calls listed in `zb/jni_hostcalls.h`"*. The flat range holds **59 named slots**, `0xfc00u`
   (`Register`) through `0xfc3au` (`DetachCurrentThread`), followed by a
   `#define ZB_JNI_HC_COUNT 59u` — so the header has 60 `#define ZB_JNI_HC_*` lines but 59 are calls.
   (`DOCS/NO-FORK-NATIVE-OPTIONS.md:101`'s "59 named slots" is correct.) A shorty buffer is fixed at
   257 bytes (`jni_protocol.h:16-17`) and call kinds are virtual/non-virtual/static/new-object
   (`jni_protocol.h:19-23`).
3. **Host side** — `core/src/jni/*` (2 752 lines over 16 files: `descriptor`, `handles`,
   `host_jni`, `host_jni_data`, `host_jni_natives`, `host_jni_objects`, `host_jni_values`,
   `host_jni_vm`, `loader`, `mangle`, `native_call`, `native_thunks`, `proxy_runtime`, `shorty`,
   `thunks.S`) plus `core/android/jni_env_backend.{h,cpp}` (821 lines of `.cpp`).

**`JniEnvBackend` is a thin adapter over real JNI, and that is good news.** Every method is one JNI
call against the real `JNIEnv*`/`JavaVM*` of our process; `Env` values are host `JNIEnv*`, `Ref` are
`jobject`, `Id` are `jmethodID`/`jfieldID`
(`ZB core/android/jni_env_backend.h:13-17`). The header lists the complete set we must reimplement:
~50 overrides covering classes/ids, objects, `CallMethod`, fields, strings, arrays, global/weak/local
refs, local frames, exceptions, monitors, `RegisterNatives`, direct byte buffers, and
`AttachCurrentThread`/`DetachCurrentThread` (`jni_env_backend.h:19-70`).

The genuinely hard parts are the **reflection-based native-method discovery** (`declared_by_name`,
`declared_by_arguments`, `class_load_failure`, `Reflection` struct at `jni_env_backend.h:96-130`) and
the retained plugin class loader (`jni_env_backend.h:74-78`). The long-form JNI export path needs
`java.lang.invoke.MethodType.fromMethodDescriptorString` and falls back to enumeration below API 26
(`jni_env_backend.h:50-55, 118-127`). **Measured relevance:** the device report shows only two
distinct native methods called (`com/samsung/zirconia/NativeInterface.checkLicenseFile2=1`,
`checkLicenseFile=1`, `total=2`) while **36 natives are registered**
(`WORK evidence/fold7-build/phone-report-test1-excerpt.txt:17-18`, and again at
`WORK evidence/dh2work-r1/longrun/zb-runtime-report.txt:11`). So: *registration* is breadth-first and
must work for all 36; *invocation* is narrow. **INFERRED:** we can implement discovery only as far as
the 36 registrations demand and instrument the rest, but that must be measured, not assumed — the
36 signatures are exactly what the P1 gate inspects.

**The reverse direction already exists and is well specified.** `NativeRegs {x[8]; d[8]; stack; pad}`
— "the layout is shared with the assembly: x0-x7, the raw bits of d0-d7 (a float uses the low 32
bits), then the address of the caller's stack arguments" (`ZB core/include/zb/native_call.h:11-19`);
`GuestCall {r0-r3; stack}` (`:21-25`); `marshal_native_args` (`:30-35`); `store_native_result`
(`:37-40`). The ARM64 side is `ZB core/src/jni/thunks.S:28-61`, which saves x0-x7/d0-d7 and the
caller's stack pointer (`NATIVE-HOOK-MECHANISM.md:159-162`). **We must reimplement this, but we
inherit the specification** — it is the one part of the JNI bridge that is already written down
precisely enough to reimplement without reverse-engineering.

**`ZTier2` note:** `NATIVE-HOOK-MECHANISM.md:244` records that `HostJni::register_native` /
`NativeSlots` / the thunk pool map a **guest** function into an ART-callable ARM64 thunk — host→guest.
There is no inverse. Our native-replacement work (§4) is the inverse, and it is new code in both
hosts.

### 1.7 Guest→host call channel (SVC) → REIMPLEMENT-TRIM to 92 indices

**Mechanism, verified.**

* `kHostCallBase = 0x5A0000` (`ZB core/src/process.cpp:70`); the stub body is
  `svc #(0x5A0000 | index) ; bx lr` (`ZB guest/stubs/gen/libGLESv2.S:9-12`).
* The name table is generated: `ZB core/src/gen/hostcalls.inc` line 1 is
  `// Generated by tools/gen_stubs.py. Do not edit.`, and lines 2–580 hold **579 entries** with
  indices **0–578**, read into `kHostCallNames` at `ZB core/src/process.cpp:78-79` and scanned
  linearly by `host_call_name` (`:82-87`). **"579 guest→host SVC indices" confirmed exactly.**
* Dispatch: `if ((stop.swi & 0xFF0000u) == kHostCallBase)` (`process.cpp:523`), index from the low
  half (`:524`), the handler chain decides "served" (`:532`), an unimplemented index sets **`r0 = 0`
  and execution continues** (`:554-562`, with the once-per-index record at `:560`), and an
  unexpected SVC sets `r0 = -ENOSYS` (`:564-568`).
* The chain is installed exactly once, at `ZB core/src/jni/proxy_runtime.cpp:434-447`: ten fixed
  dispatchers — `HostGl`, `HostAssets`, `HostNativeWindow`, `HostEgl`, `HostLooper`, `HostSensors`,
  `HostInput`, `HostConfiguration`, `HostPlatformCompat`, `HostJni`.
* **Observed discrepancy worth recording:** the comment at `proxy_runtime.cpp:430-432` claims the
  ranges are "GLES 0-141, assets 142-159, windows 160-167, EGL 168-211, platform compatibility
  212-227, sensors 344-375". The generated table does not match that any more (measured: GLESv2
  0–343, libandroid 142–491, EGL 168–215, jnigraphics 217–219, GLESv1_CM 492–578). The comment is
  stale but harmless, because indices are globally unique and each handler returns false for indices
  it does not own. **Lesson for our host: generate the dispatch table and its documentation from one
  source, or this drift recurs.**
* The chain is the natural insertion point for native substitution
  (`DOCS/NO-FORK-NATIVE-OPTIONS.md:195-199`), and `Process::set_host_call_handler` is the one
  in-process C++ extension point (`ZB core/include/zb/process.h:29,52-54`; installed only before
  `start()`, per `ZB core/src/library_runtime.cpp:352-359`).

**Measured surface (the whole index space), reused from `DOCS/NO-FORK-NATIVE-OPTIONS.md:97-104` and
re-verified:**

| Range | Owner | Count |
| --- | --- | ---: |
| `0..578` | generated stub names (see §2.2 for the per-library split) | 579 |
| `0xFB00..0xFBFF` | `JNINativeInterface` slot stubs, `0xFB00 + slot` | `jni_protocol.h:13-14` |
| `0xFC00..0xFCFF` | flat JNI host calls | 59 named slots, `0xfc00`–`0xfc3a`, in `jni_hostcalls.h` |
| `0xFE00..0xFEFF` | library runtime (READY, CARRIER PARK) | `library_protocol.h:12-15` |
| `0x5AFFFF` | host→guest return immediate | `ZB core/include/zb/guest_thread.h:26-28` |
| `0xFFFF0F00` | `kHostReturnAddress` | `guest_thread.h:28` |

**Verdict.** The channel is ~150 lines of mechanical code plus a generated table. We reimplement it
with 92 entries (§2.2) and a generated table, which also removes the 0.16%-scale cost of a linear
scan over 579 entries on every host call — small, but free.

### 1.8 GLES/EGL marshalling → REIMPLEMENT-TRIM; EGL dropped

**Measured host-side surface:**

| Artefact | Measured |
| --- | --- |
| `ZB core/src/gen/hostcalls.inc` GL/EGL entries | `libGLESv2.so` **259**, `libGLESv1_CM.so` **87**, `libEGL.so` **46** |
| `ZB core/src/gen/gl_dispatch.inc` | **258** `case ZB_GL_HC_*` arms in `HostGl::dispatch` (`:1-2`) |
| `ZB core/src/gen/egl_dispatch.inc` | **44** `case ZB_EGL_HC_*` arms |
| `ZB core/include/zb/gl_ext_entries.inc` | **12** `ZB_GL_EXT_ENTRY` extension entries, not linkable NDK symbols, resolved through the real `eglGetProcAddress` on first use (`gl_ext_entries.inc:1-4`) |
| `ZB core/src/gl/gl_manual.cpp` | 1 539 lines, **48** `zbgl_manual_*` functions |
| `ZB core/src/gl/egl_manual.cpp` | 468 lines, **21** `zbegl_manual_*` functions |
| `ZB core/src/gl/gl_diagnostics.cpp` | 1 768 lines / 89 581 B, gated by `ZB_GL_DIAGNOSTICS=1` (`ZB core/android/guest_jni_runtime.h:45-50`) |

**Why 259 + 87 + 46 = 392 hostcall entries but 258 + 44 dispatch arms:** three names are declared in
the hostcall table and have **no** dispatch arm — `glEGLImageTargetTexture2DOES`,
`eglCreateImageKHR`, `eglDestroyImageKHR`. They therefore fall through to the "unimplemented" path at
`process.cpp:554-562` and return `r0 = 0`. Measured, not inferred. None of the three is imported by
this game (§2.3), so it does not matter to us, but it is a real gap in the existing host.

**The chain, exactly:** guest stub `svc #(0x5A0000|index)` → `Process` stop
(`process.cpp:523-532`) → `HostGl::handle_host_call` (`ZB core/include/zb/host_gl.h:119`) →
`HostGl::dispatch` (`host_gl.h:139`, generated `gl_dispatch.inc`) → `HostGl::Call` reads AAPCS32
words from the stopped register file / guest stack (`host_gl.h:24-29`, "Guest code passes AAPCS32
words in r0-r3 and on its stack", `host_gl.h:17`) → `GlBackend&` (`host_gl.h:142`) →
`GlDriverBackend`, which resolves the real function once and caches it:
`cached = reinterpret_cast<void*>(::eglGetProcAddress(name));`
(`ZB core/android/gl_driver_backend.cpp:49`). The host links `libGLESv2.so`, `libGLESv3.so`,
`libEGL.so`, `libandroid.so` directly (`DOCS/NATIVE-BUILD.md:342-343`).

**The one hardened seam, and it matters for us.** `eglGetProcAddress` never hands the guest a host
pointer: it accepts only names beginning `gl` or `egl` and resolves them through the **guest** linker,
returning a guest trap stub — "Only our own gl\*/egl\* stubs may be handed out, never an arbitrary
guest symbol" (`ZB core/src/gl/egl_manual.cpp:279-286`, quoted at `DOCS/NO-FORK-NATIVE-OPTIONS.md:84-90`;
implementation at `ZB core/src/gl/host_egl.cpp:160-182`, which tries `libEGL.so` then `libGLESv2.so`
against the guest linker). Our host must reproduce this, for the same reason: it is the door that
would otherwise let a guest address become a host pointer.

**Verdict.** REIMPLEMENT-TRIM to 92 GLES2 entry points. Of those, the *manual* ones (which need
guest-memory argument handling, read-back, or state tracking) are measured at:

| Manual function used by the engine | Line in `gl_manual.cpp` | Lines |
| --- | ---: | ---: |
| `glGetFloatv` | 572 | 3 |
| `glGetIntegerv` | 576 | 10 |
| `glTexImage2D` | 587 | 16 |
| `glTexSubImage2D` | 604 | 16 |
| `glReadPixels` | 621 | 13 |
| `glGetAttribLocation` | 669 | 6 |
| `glGetUniformLocation` | 676 | 6 |
| `glGetString` | 683 | 41 |
| `glShaderSource` | 725 | 35 |
| `glVertexAttribPointer` | 761 | 19 |
| `glDrawArrays` | 781 | 64 |
| `glDrawElements` | 846 | 11 |

**12 of the 92 need manual handling; the other 80 are mechanical** — plain
`backend_.glX(scalar args…)` arms, regenerable from the Khronos registry the project already pins
(`ZB tools/gen_gles.py`; `DOCS/ARCHITECTURE.md:59-60` "the pinned Khronos-registry stubs"). ≈ **240 of
the 1 539 lines** of `gl_manual.cpp` carry over conceptually.

`gl_manual.cpp` defines **48** manual functions in total, so the engine needs only a quarter of them.
Five that the existing host implements manually but this engine does **not** import — and which are
therefore droppable — are `glBindAttribLocation`, `glGetBooleanv`, `glGetUniformfv`,
`glGetUniformiv` and `glGetVertexAttribPointerv` (verified by intersecting the engine's 92 measured
imports against the manual function list; the earlier assumption that `glBindAttribLocation` was
among the 92 was wrong, which is exactly why the census is a program, not a reading).

**DROP: all EGL (46 hostcall entries + 21 manual functions), all diagnostics (1 768 lines), and all
GLES1-only entry points (87).** Justification is measured in §2.3: the engine and Storm import
**zero** `egl*` symbols; the engine imports **zero** symbols from `libGLESv1_CM.so` despite naming it
in `DT_NEEDED`; and `WORK build/build_apk.py:200` copies only `jniLibs/arm64-v8a/*.so` while the
guest GLSurfaceView does the EGL work in Java. This is consistent with the independent finding at
`DOCS/FRAME-LIMITER.md:378-381`: "the guest never calls `eglSwapInterval` (zero EGL imports)".

### 1.9 Guest thread creation and TLS → REIMPLEMENT

**Facts, verified.** `GuestThread` is the Dynarmic callback object and is `final`:
`class GuestThread final : public Dynarmic::A32::UserCallbacks`
(`ZB core/include/zb/guest_thread.h:59`). It runs **one JIT per guest thread**, configured at
`ZB core/src/guest_thread.cpp:48-65`: `cfg.callbacks = this` (`:54`), `global_monitor` (`:55`),
`processor_id` (`:56`), `arch_version = v8` (`:57`), `fastmem_pointer = mem.base()` (`:58`),
`coprocessors[15] = cp15_` (`:59`), `define_unpredictable_behaviour = true` (`:60`),
`enable_cycle_counting = false` (`:61`), `check_halt_on_memory_access = precise_faults` (`:62`),
`code_cache_size` (`:63`).

Two performance-relevant details we must carry over knowingly:

* **Cycle counting is off** (`:61`) and the callbacks do nothing: `AddTicks` is empty and
  `GetTicksRemaining()` returns 0 (`guest_thread.h:154-155`). There is no per-block budget, which is
  why the JIT runs a whole block per dispatch.
* **`precise_faults` costs ~2×** on integer-heavy code because it disables Dynarmic's
  `GetSetElimination` (`guest_thread.h:61-63`). Default is `$ZB_PRECISE_FAULTS`
  (`ZB core/include/zb/process.h:45-46`). Our host must default it **off** for speed and be able to
  turn it on for debugging — the same trade, made explicitly.

**TLS.** The guest thread's thread pointer is emulated as a 32-bit `tpidruro_` exposed through
`Cp15` (`guest_thread.cpp:51`) and read/written via `tls()` / `set_tls()`
(`guest_thread.h:74-75`), with a `tpidrurw_` alongside (`guest_thread.h:163-164`). The guest's `tp`
arrives via `NR_ARM_set_tls` (§1.4). A `Carrier` exposes `guest_tls()` to the host
(`library_runtime.h:96`).

**Guest threads, creation and exit.** `Process::clone_thread:658`, `thread_main:693`,
`finish_thread:702`, `unregister_thread:713`, `wait_for_threads:723`, `exit_host_process:728`
(`ZB core/src/process.cpp`), with `Process::register_thread:395`, `allocate_processor_id:312`
(ids bounded by `kMaxThreads = 256`, `process.h:36`), and the borrower path
`create_borrower:353` / `destroy_borrower:378`. The per-thread kernel state that must be reproduced
is enumerated in `guest_thread.h:116-136`: `sigmask`, `syscall_restartable`, `restart_pc`,
`restart_regs[8]`, `saved_sigmask`, `altstack`, `clear_child_tid`, `exit_status`, `call_depth`,
`child_code_cache_size`, `tid`.

**Measured demand from this game.** The engine imports **20 `pthread_*` symbols** —
`pthread_attr_init/destroy/setdetachstate`, `pthread_create`, `pthread_exit`, `pthread_join`,
`pthread_self`, `pthread_key_create`, `pthread_getspecific`, `pthread_setspecific`,
`pthread_kill`, `pthread_getschedparam/setschedparam`, `pthread_mutex_init/destroy/lock/trylock/unlock`,
`pthread_mutexattr_init/settype`. These are imports from the guest's own `libc.so`, so they are
**guest-executed**, not host calls; what the host must provide is `clone`, TLS, and the futex
plumbing underneath. Storm imports 11 more (`pthread_cond_wait/broadcast`, `pthread_key_delete`,
…) — note **no `pthread_create`**, consistent with Storm being called from the engine's threads.

**Documented failure mode to design against.** `DOCS/ARCHITECTURE.md:156-159`: "Each guest thread
gets its own Dynarmic JIT cache. Save-job threads that are created per frame therefore consume guest
address space; that is the observed Test 6 failure and the reason for the optional synchronous-path
patch" (`WORK patches/thread-exhaustion/patch_sync_jobs.py`, `DOCS/ARCHITECTURE.md:127`). Per-frame
thread creation is a known hazard and our allocator must not regress it.

### 1.10 Signals and crash reporting → REIMPLEMENT-TRIM

**Signals, mechanism (verified).** `ZB core/src/signals.cpp` (13 362 B):

* `Process::install_host_signal_forwarding` (`:147-161`) installs `forward_host_signal` (`:69`) with
  `SA_SIGINFO` and **deliberately no `SA_RESTART`** — "blocked host syscalls return EINTR to the
  guest" (`:150-151`).
* A dedicated host interrupt signal with an **empty** handler — `void interrupt_handler(int) {}`
  with the comment "the empty body is the point" (`:145`) — is installed with `sa_flags = 0` so the
  interrupted syscall ends in `EINTR` (`:155-160`). `host_interrupt_signal()` (`:132`) is
  overridable by `ZB_INTERRUPT_SIGNAL`; `GuestThread::begin_blocking`/`end_blocking`/`interrupt`
  (`guest_thread.h:97-105`) are what make a guest signal reach a thread asleep in a futex. The
  header explains why this matters (`guest_thread.h:97-100`): without it, "a guest that stops its
  threads with a signal (the Boehm collector of IL2CPP does) never proceeds."
* Guest signals are **emulated**, not forwarded: `sigactions` table, `deliver_signal` (`:192`),
  `dispatch_pending_signals` (`:163`), `sigreturn` (`:280`), `deliver_fault` (`:176`), with the
  JIT stopped via `HaltExecution`/`ClearHalt` (`DOCS/NATIVE-HOOK-MECHANISM.md:50` cites
  `ZB core/src/signals.cpp:183`).
* Pending signals are stored per thread as an atomic bitmask plus a 65-entry `siginfo32` array
  (`guest_thread.h:168-169`), and a thread's blocked mask is `sigmask` (`guest_thread.h:117`).

**Measured demand from this game.**

| Module | signal-ish imports |
| --- | --- |
| `libDungeonHunter2.so` | `abort`, `pthread_kill`, `sigaction` |
| `libStormGLOFT.so` | `abort`, `raise`, `sigaction`, `sigsetjmp`, `siglongjmp` |
| `libnativeinterface.so` | none |

**Verdict: REIMPLEMENT-TRIM, but not DROP.** `sigaction` + `pthread_kill` + `raise` are all present,
and Storm's `sigsetjmp`/`siglongjmp` means the guest really does longjmp out of a signal context.
The *timed-wait* and *queued-signal* corner (`rt_sigtimedwait`, `rt_sigqueueinfo`,
`rt_tgsigqueueinfo`) is **UNKNOWN** and should be probed with a first-use counter rather than
implemented speculatively.

**Crash reporting.** `RuntimeReport` (`ZB core/include/zb/runtime_report.h:30`) is heavily bounded:
`kMaxDistinctHostCalls = 16` (`:32`), `kMaxLibraries = 32` (`:33`), `kMaxNativeCalls = 32` (`:58`),
`kMaxOpenedPaths = 16` and `kMaxFailedOpens = 16` (`:70-71`), with notes for unimplemented host calls
(`:47`), proxy load (`:48-49`), `JNI_OnLoad` (`:50`), registered natives (`:51`), guest exit (`:61`),
a bounded guest-log tail (`:63`), watched opens (`:72-73`), and GL observations (`:79-88`). The DH2
patch extended the failed-open list from 4 to 16 and added the 64-line guest-log tail
(`DOCS/ARCHITECTURE.md:94`), and made `process.cpp` report registers + module-relative PC-LR + stack
candidates for a **nonzero guest exit**, not only for a fault signal (`DOCS/ARCHITECTURE.md:95`,
implementation `ZB core/src/process.cpp:583,733,748`).

**Verdict: REIMPLEMENT-TRIM ≈ 15%.** We keep the guest-exit reason, the register dump, the
module-relative PC/LR, the bounded failed-open list and the guest-log tail — these are precisely the
instruments that make the 30-minute/8-map-load acceptance test diagnosable
(`DOCS/PERFORMANCE-BASELINE.md:154-157`). We drop the GL-diagnostics cross-references (component 10)
and the looper/watch/egl-object detail categories.

**The hang watchdog** (`ZB core/src/hang_watchdog.cpp`, 12 667 B) detects a wedged guest. It is
diagnostic-only; keep it only if the P1/P3 gates show hangs we cannot otherwise see. **Default:
drop, re-add on evidence.**

### 1.11 Plugin / launcher loading path → REIMPLEMENT as our own app

**What exists and why.** `ZB android/launcher/**` is 24 Java files / 2 863 lines. Its job is to host
an *arbitrary* guest APK behind a proxy:

* `PluginClassLoader extends DexClassLoader`; for a translated plugin it passes
  `hostNativeLibraryDir + File.pathSeparator + record.proxyDir()` as the library search path
  (`ZB android/launcher/app/src/main/java/com/zettabridge/launcher/PluginClassLoader.java:19-22`) —
  that line is **the project's own patch** (`DOCS/NO-FORK-NATIVE-OPTIONS.md:213-217`) and exists so
  Android's native loader picks an ARM64 namespace instead of rejecting the ARM32 proxy.
* `findLibrary` returns a path only for names present in the plugin's own ARM32 lib directory
  (`PluginClassLoader.java:59-68`).
* Bundle integrity is `RuntimeBundle.java`: `zb-files.txt` + `zb-version.txt` + a `.bundle-version`
  marker (`:17-19`), asset extraction with special handling for `guest/zbhost` and
  `sysroot/system/bin/linker` (`:44-51`), and a required-member check listing
  `sysroot/system/bin/linker`, `guest/zbhost`, `guest/lib/{libzbcompat,libzbjni,libGLESv2,libEGL,libandroid,libjnigraphics}.so`
  and `host/libzbproxy.so` (`:71-79`).
* `libzbproxy.so` is an ARM64 proxy stub whose whole purpose is to satisfy ART's ELF-machine check
  for an ARM32 library name (`ZB core/android/zbproxy.c`, 7 152 B built).

**Verdict: REIMPLEMENT, and simplify aggressively.** We control the APK, so we do not need a
*plugin store*, a *plugin switch activity*, a *package-manager hook*, a *class-loader factory*, or
hidden-API bypass for generality. Minimum viable launcher: one activity, `DexClassLoader` over the
game's DEX, `System.loadLibrary` for our host, and a proxy stub for the guest library names that ART
must accept. **INFERRED:** this removes most of `activeplugin/plugin*/packageManagerHook/hiddenApi/…`
but the proxy mechanism itself is load-bearing and must be re-derived from its ABI requirement, not
copied.

**A stronger option, worth costing at P0.** Merge the guest DEX and guest `.so` names into our own
APK and eliminate the proxy indirection entirely. That removes component 19's whole reason to exist,
at the cost of a larger packaging change and a re-verification of the guest manifest edits already
made by `WORK patches/game/patch_game.py` (`DOCS/ARCHITECTURE.md:126`). Present as an option, not a
default: it is where a rewrite can quietly lose a compatibility behaviour.

**The launcher is not a translator seam** — the honest negative result, already established at
`DOCS/NO-FORK-NATIVE-OPTIONS.md:208-226` and `:250-256`, and it still holds: the Java layer
contributes exactly one real capability (an ARM64 load path visible to guest Java), and that
capability is not needed once we own the native host.

### 1.12 Compatibility behaviours already validated → KEEP-OURS

These live in `WORK`, not in ZettaBridge, and must survive the host swap. Each is a *validated
behaviour*, which means each is also a regression test.

| Behaviour | Where | Evidence of validation |
| --- | --- | --- |
| **Path aliases** (`/data/data/<pkg>/`, `/data/user/0/<pkg>/`, their `lib/`, and the legacy `/sdcard` + `/storage/emulated/0` Android cache roots → app-private dirs) | `ZB .../launcher/LoadedPlugin.java:95-107` (registers them), API at `ZB .../core/ZBridge.java:57`, host side `ZB core/src/process.cpp:247-256` + `ZB core/include/zb/process.h:139`, validation in `ZB core/android/zbridge_jni.cpp:126-141` | `DOCS/ARCHITECTURE.md:89`; `DOCS/NO-FORK-NATIVE-OPTIONS.md:157-164` |
| **Repeated-cache-root collapse** — the engine can emit a path containing the Android cache root twice, second copy lowercased | `WORK patches/storm/storm_path_repair.h:34-61` (`dh2_repeated_cache_root`, accepts only the *same* Android cache root repeated, ASCII-case-insensitively, and keeps the first spelling) with `dh2_cache_root_suffix` at `:17-31` and the trailing-separator guard at `:7-16` | live in the long-run guest log: `WORK evidence/dh2work-r1/longrun/zb-runtime-report.txt:16,18,20` — "DH2FileGuard recovered repeated root: …" |
| **MediaStore repair** — the original one-element `*` projection is rejected by modern MediaProvider as `IllegalArgumentException: Invalid column *` | `WORK app/guest-java/local/dh2/compat/MediaQueries.java:16-19` (explicit `{"_id","name"}` projection), `:34-36` (non-null empty cursor so the legacy constructor and `nativeInitplayer` still run) | device failure captured at `WORK evidence/fold7-build/phone-report-test1-excerpt.txt:29-36`; fix recorded at `DOCS/STATUS.md:23-42` |
| **Language preference** — an English preference written into the game's own settings blob, preserving the 14 one-byte tutorial flags appended after `__saveOptions` | `WORK app/launcher-java/com/zettabridge/launcher/LanguagePreference.java:13-22` (settings + backup names, `MAX_SETTINGS_BYTES = 65536`), `:26` `apply`, `:42-50` backup verification with `CREATE_NEW`, `:64-66` atomic-replace requirement, `:78` `languageValueOffset`, `:94-95` the 14-flag comment | `DOCS/STATUS.md:306-332` (Test 11) |
| **Legacy absolute-path resolution** | `WORK app/guest-java/local/dh2/compat/GamePaths.java:7-9,21` | `DOCS/ARCHITECTURE.md:110` |
| **Engine byte patch** — `CFileSystem::open` treats a leading `/` as absolute while preserving colon-path and relative behaviour | `WORK patches/engine/patch_engine.py` + `engine_path_fix.S/.ld`, VA `0x56dd9c`, 5 ARM instructions / 20 bytes, SHA-256 + original-byte verified | `DOCS/ARCHITECTURE.md:122`; `DOCS/FRAME-LIMITER.md:388-397` |
| **Storm bias fix + import fix** | `WORK patches/storm/patch_storm.py` (VA `0x438e4`), `storm_import_fix.c` (`0x371e4` → stub at `0xd3800`), `storm_path_repair.h` | `DOCS/ARCHITECTURE.md:123-125` |
| **Guest Java/manifest/storage patching** | `WORK patches/game/patch_game.py` | `DOCS/ARCHITECTURE.md:126` |
| **Optional save-job synchronisation** | `WORK patches/thread-exhaustion/patch_sync_jobs.py`, RVA `0x32c534` | `DOCS/ARCHITECTURE.md:127`; `DOCS/STATUS.md:335-362` |
| **Frame-rate finding** | the ~60 FPS plateau is the 60 Hz panel, not an engine limiter; no engine instruction exists to patch | `DOCS/FRAME-LIMITER.md:12-26,77-102,337-397` |

**A note on the observable baseline these must match.** Everything above is only *validated* relative
to a known-good run: `8 survived, 0 died, 0 SIGSEGV` over eight consecutive launches
(`DOCS/PERFORMANCE-BASELINE.md:154-157`, evidence `WORK evidence/dh2work-r2/launch-test.txt`) and a
388-second run at 25–27 FPS with 6.57 M GL calls and 38 252 native calls
(`DOCS/PERFORMANCE-BASELINE.md:80-81`). Those are the numbers the new host has to reproduce, which is
why §3 makes them gates rather than aspirations.

### 1.13 What the constraint excludes, and one correction

* **Do not read `build/verify_package.py:38` as evidence about the Dynarmic hook.** It checks
  `native-load-test.log` for *Storm's own* shader and GL-string GOT hooks and Storm's original inline
  hook — a guest-side hook, not `PreCodeReadHook`. There is no evidence anywhere in this project that
  the Dynarmic translation-time hook has ever been exercised. `DOCS/NATIVE-HOOK-MECHANISM.md:75-77`
  states it directly: "A grep for `PreCodeReadHook`/`PreCodeTranslationHook` across the whole
  ZettaBridge tree returns **zero** hits."
* **`cli/zbrun` is not a candidate platform.** It constructs a bare `Process` with no host-call
  handler and no JNI runtime (`ZB cli/zbrun/main.cpp:74-79`), so it cannot serve GL or JNI host calls
  (`DOCS/NO-FORK-NATIVE-OPTIONS.md:228-234`). Our host has no CLI; it is an app.
* **`ZB core/src/hang_watchdog.cpp`, `gl_diagnostics.cpp`, the 16 K port, and the socket/epoll
  syscall group are all read-for-reference only.** None is required to run this game; two are
  performance or diagnostic luxuries and one is an unfinished experiment
  (`DOCS/ARCHITECTURE.md:128`, "Not integrated into the shipped launcher").

---

## 2. What only-this-game scope buys — measured

### 2.1 Method (reproducible, and it is the method that matters)

The census is a **binary analysis over hash-pinned inputs**, not an estimate:

1. For each shipped guest module, read `.dynsym` and every relocation section with `pyelftools`.
   A name is *reachable* only if it is both `SHN_UNDEF` in `.dynsym` **and** carries an
   `R_ARM_JUMP_SLOT` or `R_ARM_GLOB_DAT` relocation — i.e. the loader will actually bind it.
2. Map each reachable name to its SVC index through the generated table
   (`ZB core/src/gen/hostcalls.inc`, or our own regenerated equivalent).
3. Take the union across every module in the shipped game tree (the tree contains exactly three
   `armeabi-v7a` libraries — verified by listing `GAME`), and subtract from the 579 indices.
4. Close the *dynamic* hole separately: check for `dlopen`/`dlsym`/`dlclose` imports and for
   `eglGetProcAddress` in the module; then, on device, log every name the guest linker resolves and
   every name `eglGetProcAddress` is asked for. A statically closed set is only closed if step 4 is
   also clean.
5. Hash-pin each input (`SHA-256`) so the census is reproducible and a game or patch change
   invalidates it loudly.

Scripts and raw output: `DH2Work-scratch7/census.py`, `census.py`'s `census.json`. Engine SHA-256 is
recorded independently at `DOCS/FRAME-LIMITER.md:5-6`
(`36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`).

### 2.2 The SVC-index answer: 92 of 579

**Measured per-library split of the 579 entries in `ZB core/src/gen/hostcalls.inc`** (indices and
first-occurrence line numbers in that file):

| Library | Entries | Index range | First line | Reachable by this game |
| --- | ---: | --- | ---: | ---: |
| `libGLESv2.so` | 259 | 0–343 | 2 | **92** |
| `libandroid.so` | 184 | 142–491 | 144 | **0** |
| `libGLESv1_CM.so` | 87 | 492–578 | 494 | **0** |
| `libEGL.so` | 46 | 168–215 | 170 | **0** |
| `libjnigraphics.so` | 3 | 217–219 | 219 | **0** |
| **Total** | **579** | 0–578 | | **92** |

**Reachability result.** `libDungeonHunter2.so` imports **92** symbols the host provides — all from
`libGLESv2.so`. `libStormGLOFT.so` imports **7**, and all 7 are a strict subset of the engine's 92
(`glGetError`, `glGetShaderiv`, `glGetString`, `glScissor`, `glShaderSource`, `glTexImage2D`,
`glViewport`). Union = **92**. `libnativeinterface.so` imports **0**. **So 487 of 579 indices
(84.1%) are dead for this title and can be dropped.**

**Zero EGL, confirmed twice over:** neither the engine nor Storm has a single `egl*` undefined
symbol, although Storm lists `libEGL.so` in `DT_NEEDED` and the engine does not. This independently
corroborates `DOCS/FRAME-LIMITER.md:79-102` ("the guest never touches EGL").

**Zero `libandroid.so`, which is the larger win.** The engine and Storm import **no** `AAsset*`,
`ANativeWindow_*`, `ALooper_*`, `ASensor*`, `AInputEvent*`, `AMotionEvent*` or `AConfiguration_*`
symbol. Measured content of the 184 `libandroid.so` indices, by band:

| Index band | Content |
| --- | --- |
| 142–167 | `AAssetDir_*`, `AAssetManager_fromJava`, `AAsset_*` |
| 212–227 | `ANativeWindow_lock/unlockAndPost`, `ALooper_*` |
| 344–374 | `ASensorEventQueue_*`, `ASensorManager_*`, `ASensor_*` |
| 375–449 | `AInputEvent_*`, `AMotionEvent_*` |
| 450–491 | `AConfiguration_get*/set*` |

**Consequence: the entire asset, native-window, looper, sensor, input, configuration and
platform-compat backend set — `ZB core/android/*` (2 314 lines) plus `ZB core/src/android/*` and the
ten-dispatcher chain's middle eight entries (`ZB core/src/jni/proxy_runtime.cpp:434-447`) — is
unreachable for this game and can be dropped.** The accelerometer and touch paths this title uses go
through Java (`SensorManager`, `GLSurfaceView`), i.e. through JNI, not through the NDK backend. That
is consistent with the `:guest` process's Java `GLSurfaceView` noted at `DOCS/ARCHITECTURE.md:135-138`
and with the accelerometer/`nativeSetOrientation` fault being a JNI-side event
(`DOCS/PERFORMANCE-BASELINE.md:157`).

**Honest discrepancy with the brief.** The brief says "91 GL functions". The measurement gives
**92** distinct undefined `libGLESv2.so` symbols with jump-slot/glob-dat relocations. The engine has
351 `R_ARM_JUMP_SLOT` relocations and 361 undefined symbols; `DOCS/NATIVE-PORT-SHORTLIST.md` ("PLT
veneer → import name, 351/351") matches the 351 figure. The one-symbol difference is most likely a
counting convention (a versioned or duplicated symbol name) rather than a real disagreement. **Action:
treat 92 as the measured number and re-derive it from the hash-pinned `libDungeonHunter2.so` at
P0, printing the list, so the census is self-verifying.**

### 2.3 The GL-stub answer: 92 of 404, and the guest-stub cross-check

Three different counts are all defensible depending on what is being counted; the brief's "~490" sits
between them:

| Definition | Count |
| --- | ---: |
| GL/EGL hostcall-table entries (`libGLESv2` 259 + `libGLESv1_CM` 87 + `libEGL` 46) | **392** |
| … plus the 12 dynamically resolved extension entries (`gl_ext_entries.inc`) | **404** |
| **Guest stub functions actually generated** (`libGLESv2.S` 259 + `libGLESv1_CM.S` 145 + `libEGL.S` 46) | **450** |
| … plus the 12 extension entries | **462** |
| All generated guest stub functions across all five libraries (450 + `libandroid.S` 184 + `libjnigraphics.S` 3) | **637** |
| Distinct SVC indices those 637 functions use | **579** |

The 637-vs-579 gap is exactly **58**, and it reconciles cleanly: `libGLESv1_CM.S` carries 145 stubs
while only 87 indices belong to `libGLESv1_CM.so` in the hostcall table, so 58 GLES1 stub names share
a GLESv2 index (e.g. `glActiveTexture` is `svc #0x5a0000` in both files — measured). That is a
consistency check on the census, not a discrepancy.

**Reachable: 92.** Of those, 80 are mechanical and 12 need manual handling (§1.8). **Unreachable and
droppable: 312 of 404 (77%)** of the hostcall+extension surface, and 545 of 637 (86%) of the
generated guest stub functions. Note that zero of the 12 extension entries is imported by either
guest module — verified, since a `gl*`/`egl*` undefined symbol with no hostcall entry would have been
reported as unresolved, and none was.

### 2.4 The syscall set: how to get it exactly (and why the static route is not enough here)

The GL/SVC census works because imports are a **closed, relocated** set. Syscalls are not: ARM32
bionic dispatches them from translated guest code and the number is materialised in a register, not
in a relocation.

**The static route, and its measured failure mode.** A `capstone` linear sweep for `svc` with a
backward window looking for `mov r7, #imm` is *unreliable* and I measured why:

1. Capstone's `disasm()` generator **stops at the first undecodable word unless `skipdata` is
   enabled**; without it the sweep silently reports zero sites. (Measured: zero `svc` in
   `libc.so`'s `.text` without `skipdata`, versus 240 occurrences of the literal ARM `svc #0`
   encoding `00 00 00 EF` in the same file.)
2. Even with `skipdata`, a sweep that does not switch ARM/Thumb mode per `$a`/`$t` mapping symbol
   produces enormous false positives: decoding `libDungeonHunter2.so` in both modes gave **82 340**
   apparent `svc` sites, of which 82 334 had no recoverable `r7` (i.e. almost all are misaligned
   decode artefacts). A correct static pass must be **mapping-symbol-driven**, in the style the
   project already uses to get "complete ARM/Thumb mapping symbols"
   (`DOCS/PERFORMANCE-BASELINE.md:72-75`), and must handle `ldr r7, [pc, …]` literal-pool loads.

**The authoritative route is runtime and it is exact.** Our host *is* the syscall dispatcher, so a
per-number first-use counter gives the answer with no analysis at all. The existing host already has
both primitives to copy the idea from: `Process::first_time(key)`
(`ZB core/src/process.cpp:193`) and `RuntimeReport::note_unimplemented_host_call`
(`process.cpp:560`, `runtime_report.h:47`). `ZB_STRACE` already exists as a tracing hook
(`ZB core/src/syscalls.cpp:107`).

**Gate.** Add a generated `syscall_seen[423]` flag array to the dispatcher, log the used set on exit,
and read it off after the same 30-minute/8-map-load run the acceptance gate already uses
(`DOCS/PERFORMANCE-BASELINE.md:154-157`). Start P1 by implementing the ~40 numbers the sysroot's
`libc.so`/`libm.so` visibly need (openat/read/write/close/lseek/stat/fstat/mmap2/munmap/mprotect/
brk/mremap/gettimeofday/clock_gettime/nanosleep/futex/clone/set_tls/rt_sigaction/rt_sigprocmask/
ioctl/getdents64/getrandom/uname/prlimit64/sched_getaffinity/…), then implement only what the
counter says is missing. **Do not publish a static syscall count as fact**; the `syscall_scan2.py`
outputs in scratch are retained only as the falsification record above.

### 2.5 JNI, threads, signals: the same treatment

| Surface | Exchange rate available | Method |
| --- | --- | --- |
| JNI `JNINativeInterface` slots (`0xFB00+slot`) | the 36 registrations are measured (`WORK evidence/fold7-build/phone-report-test1-excerpt.txt:17`); the *used* slot set is unknown | counter per slot, read after P2; blocks a claim that we can skip the long tail of `JniEnvBackend` |
| Flat JNI host calls (`0xFC00..0xFCFF`) | 59 named slots (`0xfc00`–`0xfc3a`) over a 256-slot range | counter; note that `CallMethodA` (`0xfc10`) subsumes hundreds of Java call shapes |
| `pthread_*` | 20 imports (engine) + 11 (Storm), enumerated in §1.9 | static imports are a valid *upper* bound here, unlike syscalls, because they are relocated |
| Signals | `sigaction`, `pthread_kill`, `raise`, `sigsetjmp`, `siglongjmp` (table in §1.10) | static for presence, runtime for `rt_sigtimedwait`/`rt_sigqueueinfo` |
| Guest `dlopen`/`dlsym` | `dlopen`/`dlsym`/`dlclose` are imported **only by Storm**; the engine imports none | static + runtime log of the requested names (the count is tiny) |
| Guest relocation types | **4** (measured, §1.2) | static, exact |

### 2.6 What must **not** be shrunk — the anti-overfitting list

Trimming by measured reachability is correct only for surfaces whose closure is *provable from a
relocation*. The following must stay generic even though today's census says "not used":

1. **`mmap`/`brk`/`mremap` semantics and the 4 GiB allocator.** The guest's Scudo allocator decides
   what it calls, at runtime, as a function of load. `DOCS/GUEST-MEMORY-INVESTIGATION.md:302-350`
   shows the failure class is *fragmentation-dependent* — the call pattern in hour two is not the
   call pattern in minute one. Keep the whole memory syscall family.
2. **The signal path and the EINTR/interrupt design.** A crash-path optimisation that only appears
   on an abort is exactly the code you cannot test away.
3. **`JNIEnv` exception propagation and local-reference frames.** ART will not tell you when you got
   them wrong; it will produce a subtly different app.
4. **`eglGetProcAddress` name filtering.** Dropping it is a safe-looking simplification that turns a
   guest address into a host pointer (`ZB core/src/gl/egl_manual.cpp:279-286`).
5. **The four compatibility behaviours.** They are in `WORK`, they are validated, and they are the
   difference between "it launched" and "it played for 30 minutes".
6. **The `syscall_restartable` / `restart_pc` / `restart_regs` machinery**
   (`guest_thread.h:117-127`). The comment is explicit that a guest which trusts `SA_RESTART`
   semantics and does not retry will fail — "Unity treats an interrupted semaphore wait as a failure,
   and mono and FMOD signal often enough that it happens within seconds"
   (`guest_thread.h:118-122`). This title is not Unity, but the pattern is exactly the kind of thing
   DH2's engine could also do.

### 2.7 The re-census gate (applies to every phase)

Before each phase's acceptance run: re-run the census from the **exact** shipped binaries, compare
SHA-256 of each input and the resulting index set to the previous run, and fail loudly on any
difference. The census is only valid for the build it was taken from; the engine and Storm are
already hash-pinned by the byte-patch layer (`DOCS/ARCHITECTURE.md:117-118`), so this costs nothing
to enforce.

---

## 3. Phased plan with acceptance gates

Naming: the new host library is `libdh2host.so`; the new project tree is `dh2host`. Every phase ends
at something demonstrable **on the device**. Every gate has an explicit fallback, and every fallback
is the same object: the currently-shipping APK, which is never modified during this programme.

### P0 — Toolchain, provenance, and a build of our own (no behaviour)

**Deliverable.** `dh2host` CMake project vendoring Dynarmic at the pin; our own `sysroot-extract.sh`
with the AOSP URL and SHA-256 from `ZB tools/extract_sysroot.sh:4-5`; build scripts adapted from the
seven verified ones (`DOCS/NATIVE-BUILD.md:455-467`).

**Why it is nearly free.** The toolchain is already verified end-to-end and reproduces the shipped
`libzbridge.so` **byte for byte**, SHA-256 `25e8da7e…682d24`, 3 306 992 B, 185 Ninja steps, ~70 s on
20 cores (`DOCS/NATIVE-BUILD.md:7-9`, `:187`). Host is WSL2 Ubuntu, NDK r29 `29.0.14206865`
linux-x86_64, Boost 1.83 headers only, CMake ≥ 3.22 / 3.12 minima, Ninja 1.13.2
(`DOCS/NATIVE-BUILD.md:15-58`). The three deltas that mattered (use the `cmake` binary not
`python -m cmake`; download the **Linux** NDK; seed the sysroot) are recorded at
`DOCS/NATIVE-BUILD.md:239-254` and do not recur.

**Gate P0.** (a) `libdh2host.so` builds as `ELF64 / EM_AARCH64 / ET_DYN` and passes the same
pyelftools checks as `DOCS/NATIVE-BUILD.md:303-343`, and its SHA-256 **differs** from
`25e8da7e…682d24` (i.e. it is not a ZettaBridge build). (b) Our sysroot extractor reproduces all ten
files with SHA-256 equal to the copies inside `BUNDLE` (proving the AOSP source is the real source),
and the sysroot no longer originates from the ZettaBridge tree. (c) The provenance note is written:
Dynarmic is **0BSD** — the header of the pinned sources says so
(`DYN src/dynarmic/frontend/A32/translate/translate_callbacks.h:3`,
`DYN src/dynarmic/frontend/A32/a32_ir_emitter.h:4`), consistent with
`DOCS/NO-FORK-NATIVE-OPTIONS.md:634-639`; the sysroot is AOSP. Nothing in our tree is ZettaBridge-derived.

**Fallback.** None needed; nothing ships.

### P1 — The engine loads and runs its own native init (headless, no GL)

**Deliverable.** Components 1–7 and 12–18 in minimum viable form: guest memory, ELF loader + the four
relocations, guest linker/library mode + our ARM32 service stub, the measured syscall subset, JNIEnv
bridge + `RegisterNatives` + `JNI_OnLoad`, guest threads + TLS + `clone`, minimal signals, a trimmed
runtime report, and a minimal launcher activity that loads the guest DEX.

**Gate P1 — the exact numbers to reproduce**, taken from
`WORK evidence/fold7-build/phone-report-test1-excerpt.txt:6-21`:

| Counter | Required value | Source |
| --- | --- | --- |
| proxy-loads / failures | 3 / 0 | `:8-9` |
| proxy-loaded | `libnativeinterface.so`, `libDungeonHunter2.so`, `libStormGLOFT.so` with their JNI versions | `:10-12` |
| jni-onload-calls | 2, one per translated engine library | `:13-15` |
| **registered-natives** | **36** | `:17` |
| unimplemented-host-calls / distinct | 0 / 0 | `:18-19` |
| native-calls | `NativeInterface.checkLicenseFile2=1`, `checkLicenseFile=1`, total 2 | `:21` |

Plus: the engine reaches its own init and calls `nativeSetPhone` / `nativeSetOrientation` without
faulting, with no GL context (the `GLSurfaceView` is not yet wired). **What proves it:** our own
runtime report printing the same counters with the same values, from a run on the same device
(`SM-F966B`, Android 16, `arm64-v8a`, page size 4096 — `:4-6`, `:7`).

**Why this gate is the right one.** These counters are the only *falsifiable* claim the existing host
makes about correctness at this stage: 36 natives means the JNI table, the reflection-based
discovery and the thunk pool all work; 2 `JNI_OnLoad`s means the guest linker and relocation engine
work; 0 unimplemented host calls means our trimmed 92-index table covers everything the engine
touched before GL. If we get 35 natives or 1 `JNI_OnLoad`, we know exactly which subsystem is wrong.

**Fallback.** The current APK. Additionally, each subsystem can be swapped back one at a time in a
side-by-side `:guest` process, because `WORK build/build_apk.py:113-121` re-walks `assets/zb/**` and
rewrites the manifest and version hash, so **any** guest file can be substituted with no host change
(`DOCS/NO-FORK-NATIVE-OPTIONS.md:411-421`).

### P2 — First frame rendered

**Deliverable.** Component 9 (92 GLES2 entry points, 12 manual), generated guest stub library for
exactly those 92, the hardened `eglGetProcAddress`, and the Java-side GL context (which the
`GLSurfaceView` owns, not us — `DOCS/ARCHITECTURE.md:135-138`).

**Gate P2.** A screenshot of the real first frame, plus a call census:
`gl-calls > 0` and of the right order — the existing build measures **~680 GL calls per frame**
(6.57 M calls over 388 s, `DOCS/PERFORMANCE-BASELINE.md:80-81`) — and our report must show
`egl-swaps > 0`. **This is a genuine improvement, not just parity:** the existing host counts zero
swaps "even when frames are presented", which the patch layer has to label explicitly because the
guest never enters the EGL bridge (`DOCS/ARCHITECTURE.md:136-138`). Owning the swap accounting is a
free win in observability.

**What proves it.** A pixel-diff against the current build at the same activity state, plus a GL
function histogram that should be dominated by the draw/uniform/texture path on 92 functions.

**Fallback.** The current APK; and within P2, per-function, any of the 92 stubs can be turned back
into the "unimplemented → `r0 = 0`" behaviour (`ZB core/src/process.cpp:554-562`), which is exactly
how the existing host degrades.

### P3 — Gameplay

**Deliverable.** Input and lifecycle through JNI (not NDK), guest thread churn under load, the four
compatibility behaviours verified live, and the memory allocator's behaviour over a long session.

**Gate P3 — the strongest available regression oracle in this project:**
**8 consecutive launches, 8 survived, 0 died, 0 SIGSEGV, over ~30 minutes with 8 map loads**, matching
`DOCS/PERFORMANCE-BASELINE.md:154-157` and `WORK evidence/dh2work-r2/launch-test.txt`. Plus, from the
guest log, the *same* compatibility lines reciting the same paths: `DH2FileGuard recovered repeated
root: …` and `DH2Model opened: …`
(`WORK evidence/dh2work-r1/longrun/zb-runtime-report.txt:16,18,20`), a working playlist query, and the
saved-language value taking effect.

**What proves it.** An evidence artefact in the shape of `WORK evidence/dh2work-r2/launch-test.txt`
plus a long-run report in the shape of `WORK evidence/dh2work-r1/longrun/zb-runtime-report.txt`. The
run must also stay clear of the known memory failure class described in
`DOCS/GUEST-MEMORY-INVESTIGATION.md:302-350`.

**Fallback.** The current APK. Additionally, the optional engine patches
(`WORK patches/thread-exhaustion`, `DOCS/ARCHITECTURE.md:127`) remain available and are unchanged by
the host swap.

### P4 — `PreCodeReadHook` routes ONE function to native ARM64

**Deliverable.** §4 in full: the hook override, the address→native map, the per-signature AAPCS32→
AAPCS64 shim, and one integer-only leaf function verified bit-exact.

**Gate P4.**
1. **The native body executed.** A per-target entry counter increments in the exact proportion the
   target's call sites predict — this is the anti-false-positive check, because a hook that never
   fires also never breaks anything.
2. **Bit-exactness off-device, before the device run.** Against the Unicorn oracle, with the
   established discipline: bit-for-bit for non-NaN floats, NaNs by classification, and compare **all
   16 registers plus memory**, not just `r0` — comparing only `r0` has already produced false passes
   in this project (`DOCS/NATIVE-HOOK-MECHANISM.md:294-305`; the harness is
   `RECON/tools/dh2_oracle.py` + `difftest.py`, recorded at 27 250 comparisons / 0 mismatches and
   21 477 ARM32-vs-ARM64 comparisons / 0 mismatches, `DOCS/PERFORMANCE-BASELINE.md:90-98`).
3. **No cadence regression.** Frame time measured the same way as `DOCS/PERFORMANCE-BASELINE.md:13-18`
   must not worsen; a hook that costs more than it saves must fail this gate.
4. **The JIT share moved.** simpleperf on a `DH2_PROFILEABLE=1` build
   (`WORK build/build_apk.py:164-170`) re-measures the 82.68 / 15.11 / 0.16 split
   (`DOCS/PERFORMANCE-BASELINE.md:121-131`). One function will not move 82.7% measurably; the gate is
   that the *direction* is right and the mechanism is proven.

**Fallback, and it is automatic by construction:** the hook matches a narrow address set and
**delegates to the base implementation for every other PC**, and a target can be removed from the
table by data, not code. The requirement from `DOCS/NATIVE-HOOK-MECHANISM.md:291-293` is binding:
"make the failure mode *fall through to the original translation*, never a trap — a wrong hook must
degrade to today's behaviour." P4's fallback is therefore not "revert the build"; it is "empty the
target table and re-run the P3 gate".

### P5 — Display-mode control (the 60 Hz ceiling)

**Deliverable.** Our own activity requests a higher refresh mode for the window's lifetime.

**Gate P5.** The decisive experiment already specified: launch once with the panel at 120 Hz and re-run
the same 120-frame batch measurement; if the plateau moves up, the vsync attribution is proven
(`DOCS/FRAME-LIMITER.md:405-420`). `dumpsys display` must show the requested mode active for our UID.

**Why this is easier for us than for the current build.** `DOCS/FRAME-LIMITER.md:346-374` lists three
project-controlled surfaces, and one of them is a dead end: `WORK build/build_apk.py:157-163` records
that **aapt2 rejects `android:preferredDisplayModeId`** — "attribute `android:preferredDisplayModeId`
not found" — and that it is a `WindowManager.LayoutParams` field, not a framework manifest attribute,
so "any fix must be programmatic … and must target the guest's own Stubs activity, which is created by
the ZettaBridge launcher rather than by our code. Left unimplemented deliberately." With our own
launcher activity, the programmatic path is ours: set the field on our own window before
`setContentView`, and no injection into `ZB .../GuestWindowStyle.java:42-53` is required. **This is
the second concrete thing "a host we own" buys, after the hook.**

**Caveat to carry.** `DOCS/FRAME-LIMITER.md:28-31` marks the vsync *identity* as a strong inference
that "has not been proven by a live A/B run"; P5 is that A/B run.

**Fallback.** A toggle; at 60 Hz the game is exactly as it is today.

### P6 — Scale-out, and (optionally) promote the hook off the translation path

**Deliverable.** N hot targets native — the ranked shortlist starts with
`CMatrix4Base<float>::mult` (`0x0035e118`, 1 992 B), `CMatrix4<float>::getInverse` (`0x003232c0`,
2 080 B) and `Application::_CheckGamepad` (`0x00321164`, 6 080 B)
(`DOCS/NATIVE-PORT-SHORTLIST.md:465-513`), with the corrected cost model at `:745-820` and the
matrix kernels identified as "the best ratio in the engine" (`:803`).

**Gate P6.** The 82.7% guest share falls, measured the same way, and frame time moves toward the
1.6× cut that a held 60 FPS requires (`DOCS/PERFORMANCE-BASELINE.md:52-53`). A full static
recompilation (Tier 3, `DOCS/PERFORMANCE-BASELINE.md:104-113`) is the eventual landing place, and this
host is its precondition.

**Fallback.** Each target is data. If a target's ABI turns out to be unsettled, it is removed and the
JIT takes it back.

**Optional promotion, specified in §4.7:** move the hook from translation time to an
SVC-stub installed into guest text at load time. Owning the loader makes guest text host-writable,
which removes the UNKNOWN that blocked the offline-patch route at
`DOCS/NATIVE-HOOK-MECHANISM.md:123`.

---

## 4. `PreCodeReadHook` integration design

**This section inherits §3 of `DOCS/NATIVE-HOOK-MECHANISM.md` and does not re-derive it.** That
document already enumerated the ABI facts and the ten shim requirements from source. They are
restated in §4.5 only as a *contract* for the code we write, with the one fact that must never be
guessed restated verbatim from it.

### 4.1 Where it hooks — and why this is cheaper than the previous analysis assumed

Three verified facts make the wiring trivial:

1. **The hook is already on the object we hand to Dynarmic.**
   `struct UserCallbacks : public TranslateCallbacks` (`DYN src/dynarmic/interface/A32/config.h:61`),
   and `PreCodeReadHook` / `PreCodeTranslationHook` are virtuals of that base
   (`DYN src/dynarmic/frontend/A32/translate/translate_callbacks.h:25,29`), overridden with no-op
   defaults in `UserCallbacks` (`config.h:72`, `:76`). The JIT takes the callbacks through
   `UserConfig::callbacks` (`config.h:122-123`), and the existing host wires it with a single
   assignment: `cfg.callbacks = this` (`ZB core/src/guest_thread.cpp:54`).
2. **Therefore no new object, no new plumbing, and no Dynarmic patch.** In our host, the override
   goes on the same class that already implements `MemoryRead*`, `MemoryReadCode`, `CallSVC` and
   `ExceptionRaised` — the exact override set is visible at
   `ZB core/include/zb/guest_thread.h:138-155` (12 memory callbacks, `MemoryReadCode`,
   `InterpreterFallback`, `CallSVC`, `ExceptionRaised`, `AddTicks`, `GetTicksRemaining`). We add two
   declarations and two definitions. The previous estimate of ~60–120 LOC for wiring
   (`DOCS/NATIVE-HOOK-MECHANISM.md:122`) is an over-estimate once the inheritance is read.
3. **The emitter hands us everything we need.** `A32::IREmitter : public IR::IREmitter`
   (`DYN src/dynarmic/frontend/A32/a32_ir_emitter.h:29`), so the hook's `A32::IREmitter& ir`
   parameter exposes both the A32 conveniences (`GetRegister(Reg)` `:41`, `SetRegister` `:44`,
   `GetCpsr` `:57`, `ReadMemory32` `:87`, `CallSupervisor` `:54`,
   `DYN .../a32_ir_emitter.h`) **and** the generic IR emitter's `CallHostFunction`
   (`DYN src/dynarmic/ir/ir_emitter.h:397-400`), which the ARM64 backend compiles to a genuine
   `bl`: `EmitIR<IR::Opcode::CallHostFunction>` does `PrepareForCall(args[1],args[2],args[3])`,
   `MOV(Xscratch0, args[0].GetImmediateU64())`, `BLR(Xscratch0)`
   (`DYN src/dynarmic/backend/arm64/emit_arm64.cpp:39-47`).

**The two correction factors that must be built in from line one.**

* **Return convention.** `config.h:70-71` says "By returning **true** the callee precludes the
  translation of the instruction." That comment is **wrong**. The call site is
  `if (!tcb->PreCodeReadHook(false, arm_pc, visitor.ir)) { should_continue = false; break; }`
  (`DYN src/dynarmic/frontend/A32/translate/translate_arm.cpp:33-36`, and identically for Thumb at
  `translate_thumb.cpp:114-115`), and the interface header agrees with the call site
  (`translate_callbacks.h:23-24`). **`false` ends the block; `true` continues.** A hook written to
  `config.h`'s comment breaks every block it touches. This is `NATIVE-HOOK-MECHANISM.md:78-83`,
  restated because getting it backwards is a total failure, not a subtle one.
* **A terminal is mandatory when we return `false`.** With `should_continue == false`, the
  `if (should_continue)` at `translate_arm.cpp:68` is skipped, so the translator does **not** emit a
  `LinkBlockFast` terminal — and line 77 is
  `ASSERT_MSG(block.HasTerminal(), "Terminal has not been set")`. In a release build that assert may
  be compiled out and the block reaches the backend with no terminal. **Our hook must always
  `SetTerm` before returning `false`.** For a normal function return that means
  `ir.BXWritePC(ir.GetRegister(Reg::LR))` (which updates `current_location` and handles the T-bit
  via `BranchWritePC`, `DYN src/dynarmic/frontend/A32/a32_ir_emitter.cpp`) followed by
  `ir.SetTerm(IR::Term::LinkBlockFast(ir.current_location))`
  (`DYN src/dynarmic/ir/ir_emitter.h:402`, terminal kind at `DYN src/dynarmic/ir/terminal.h:55`).

**Cost model, stated correctly.** The hook fires **per instruction translated**, i.e. once per
instruction per *block translation*, not once per execution. Blocks are translated lazily and cached
in a 32 MiB code cache per thread (`ZB core/include/zb/guest_thread.h:35`), and
`enable_cycle_counting = false` with `GetTicksRemaining()` returning 0 (`guest_thread.cpp:61`,
`guest_thread.h:154-155`) means the JIT runs whole blocks per dispatch. In steady state the cache
holds the working set, so the hook's total cost over a session is bounded by the total *distinct*
translated instruction count — roughly the engine's own instruction count (15.9 MB of code, ≈4 M
instructions) plus the sysroot fraction actually executed. **INFERRED:** that is a one-off cost on the
order of a second or less, not a per-frame cost — which changes the trade-off the previous analysis
drew ("the hook fires per instruction … so the address match must be a page or hash fast path",
`DOCS/NATIVE-HOOK-MECHANISM.md:86-88`). The lookup must still be O(1), but for translation throughput,
not for frame time.

### 4.2 Guest address → native ARM64 function

**A data-driven target table, not a code path.**

```
struct NativeTarget {
    uint32_t guest_va;      // function entry, T-bit stripped
    bool     is_thumb;      // from the ELF ARM/Thumb mapping symbols, never guessed
    uint8_t  signature_id;  // index into the signature table
    void*    body;          // the C++/asm ARM64 implementation
    const char* name;       // for logs and the hit counter
};
```

Keyed on `(pc, is_thumb)`:

* `pc` arrives as `visitor.ir.current_location.PC()` (`translate_arm.cpp:30`), and `is_thumb` is a
  separate argument (`translate_arm.cpp:33`, `translate_thumb.cpp:114`). **Do not assume whether the
  T-bit is present in `pc`** — normalise defensively (`pc & ~1u`) and log the observed `(pc, is_thumb)`
  for a known target on the first translation. **UNKNOWN, cheap to settle, must be settled before the
  lookup key is frozen.**
* **The mode must come from the ELF mapping symbols** (`$a`/`$t`/`$d`), which this project already
  treats as complete and reliable (`DOCS/PERFORMANCE-BASELINE.md:72-75`). Getting the mode wrong means
  the hook never fires — a silent no-op, not a crash.

**Lookup structure.** The hook runs on every translated instruction, so use a two-level
page directory rather than a hash: `page_map[pc >> 12]` → `nullptr`, or a small per-page bitmap of
hooked slots. A 4-byte-granularity bitmap for ARM pages is 128 bytes; a 2-byte-granularity bitmap for
Thumb pages is 256 bytes. With tens of hooked pages, the whole structure is kilobytes, and the check
is one array index plus one mask test at essentially zero cost. (A sorted array plus binary search is
an acceptable v1; `std::unordered_map` at every instruction is not, even though the total is
affordable — see §4.1's cost model.)

**What "maps to a native function" means, precisely.** The hook does not redirect a `bl`. It fires
when the *translator visits the target's entry PC*, injects a call to the native body plus write-back
IR, and ends the block. The guest's own `bl target` at the call site is translated normally and
transfers control to a block that begins at `target` — and that block's translation is the one we
replace. **Therefore a hooked function must be a whole-function replacement**: partial inlining
inside a caller is the job of a future static recompiler, not of this hook. That is
`NATIVE-HOOK-MECHANISM.md:84-88`'s caveat 3, and it is a design boundary, not a defect.

### 4.3 The pinned ABI facts that drive the design (inherited)

From `DOCS/NATIVE-HOOK-MECHANISM.md` §3, not re-derived here:

* `.ARM.attributes` is `Tag_ABI_VFP_args = 1` with **no VFP arithmetic**
  (`DOCS/SOFTFLOAT-FINDING.md:14-25`; 43 `__aeabi_*` undefined symbols in the engine — the census in
  §1.2 measures 40 for the engine, 10 for Storm, 3 for `libnativeinterface`).
* Float arguments travel "as 32-bit bit patterns in `r0-r3` / `s0-s15` via the VFP-less ABI"
  (`RECON/tools/dh2_oracle.py:7-8`).
* Doubles occupy **core-register pairs** (`dh2_oracle.py:256-262`, `:335-355`, `:412-424`).
* Struct-by-value return uses a **hidden sret pointer in `r0` with `this` shifted to `r1`**, base
  AAPCS softfp, explicitly **not** the HFA rule, confirmed empirically (`RECON/STATUS.md:404-406`).
* `__aeabi_fcmp*`/`__aeabi_dcmp*` return **1/0 in `r0`**, not flags (`dh2_oracle.py:404-411`).
* The inverse direction is already implemented and testable: `NativeRegs`, `GuestCall`,
  `marshal_native_args`, `store_native_result` (`ZB core/include/zb/native_call.h:11-40`), assembly
  at `ZB core/src/jni/thunks.S:28-61`.

**And the one fact that must never be guessed**, quoted because it is the single largest correctness
risk and it fails *silently*:

> `Tag_ABI_VFP_args = 1` means a *float* argument may be passed in a VFP register rather than in
> `r0-r3`. The oracle hedges exactly this way — "as 32-bit bit patterns in `r0-r3` / `s0-s15`" —
> because the split is **per call site and per signature**. **UNKNOWN … whether a given hooked
> function receives its float arguments in core registers or in `s0-s15` cannot be assumed; it must be
> read off the function's prologue/call sites in the disassembly before writing its shim. Getting this
> wrong produces silent wrong values, not a crash.**
> — `DOCS/NATIVE-HOOK-MECHANISM.md:185-192`

**Design consequence, adopted as a rule:** P4's first target is chosen to be **integer/pointer only**,
so it has no float argument and the split cannot bite. A target may only be promoted into the table
after its call sites have been disassembled and its argument classification written into its signature
record. This is a *gate on data*, not a code review item.

### 4.4 The emission sequence

For a matched target with signature `sig`, at guest PC `P`:

```
// 1. Read the guest's argument words. GetRegister returns IR::U32 values that are live
//    in the JIT's register file; packaging them into u64 is IR, not host work.
r0 = ir.GetRegister(Reg::R0); r1 = ir.GetRegister(Reg::R1);
r2 = ir.GetRegister(Reg::R2); r3 = ir.GetRegister(Reg::R3);
sp = ir.GetRegister(Reg::SP);

// 2. Pack four guest words into two u64 arguments. CallHostFunction takes at most three
//    u64 arguments plus the function pointer, and its IR opcode is VOID, so the only
//    host->guest data channel is guest memory. Pack2x32To1x64 exists for exactly this.
a1 = ir.Pack2x32To1x64(r0, r1);          // ir_emitter.h:80
a2 = ir.Pack2x32To1x64(r2, r3);
a3 = ir.ZeroExtendToLong(sp);            // 5th+ argument words live at [sp]

// 3. Call the per-target ARM64 body. 'fn' must be a compile-time constant: the backend
//    emits MOV(Xscratch0, imm64) / BLR Xscratch0 (emit_arm64.cpp:39-47).
ir.CallHostFunction(&thunk_<target>, a1, a2, a3);       // ir_emitter.h:397-400

// 4. Read the staged result back out of guest memory and write it into the guest register
//    file. CallHostFunction is Void, so this round trip is mandatory, not stylistic.
ir.SetRegister(Reg::R0, ir.ReadMemory32(SCRATCH + 0, IR::AccType::Normal));
// r1 too when the signature returns 64 bits (long long / double bit pattern)

// 5. Terminate the block exactly as the guest function would have returned.
ir.BXWritePC(ir.GetRegister(Reg::LR));
ir.SetTerm(IR::Term::LinkBlockFast(ir.current_location));
return false;   // false ENDS the block (translate_arm.cpp:33-36). Never true.
```

**Why `r0-r3` + `sp` and nothing else.** `sp` is what the thunk needs beyond the four argument words,
because guest argument word 5+ is at `[sp]` (requirement 2 of
`NATIVE-HOOK-MECHANISM.md:172-175`). `LR` is needed only for the *return*, which step 5 handles in
IR; the thunk never needs it. This is exactly why three `u64` arguments are enough despite
`CallHostFunction`'s arity limit: **two packed register pairs plus SP is the complete input set for a
softfp AAPCS32 call.**

**The scratch page.** A dedicated guest scratch region is required because IR cannot see host memory
— `ReadMemory*` takes a guest `vaddr` (`DYN .../a32_ir_emitter.h:87`, and the only other host→IR
channels are `GetRegister` and constants). Design requirements:

* It must be **mapped and readable in the guest page table**, because fastmem is on
  (`guest_thread.cpp:58`) and an IR read of an unmapped guest address takes the memory-abort path.
* It must be **per guest thread**, or guarded — the process has 37 threads
  (`DOCS/PERFORMANCE-BASELINE.md:22`) and save-job threads exist (`DOCS/ARCHITECTURE.md:156-159`).
  Cheapest correct scheme: one scratch page per guest thread, carved from a reserved arena inside the
  guest window at thread creation, with the page address stored on the thread object.
* It must never be visible to the guest's own allocator — reserve it in the same place the host
  reserves its own special regions (`map_kuser_page`, `ZB core/src/process.cpp:157-171`).

**Why the thunk writes results through the guest base, not into `Jit::Regs()`.** Writing
`jit_->Regs()[0]` from the thunk at runtime is wrong even though it looks simpler: Dynarmic's arm64
register allocator caches guest registers in host registers across a block and does not reload them
from `Regs()` at a `LinkBlockFast` terminal (the array is only authoritative at `Run()` boundaries).
A behind-the-back write would be silently dropped or clobbered. The stated round trip exists for a
concrete reason — it is `NATIVE-HOOK-MECHANISM.md:199-203`'s requirement 9, arrived at independently
here and therefore worth stating as settled.

### 4.5 The shim: AAPCS32-softfp → AAPCS64

**The ten requirements from `DOCS/NATIVE-HOOK-MECHANISM.md:170-205` are the contract, inherited
verbatim and not re-derived.** Our code must satisfy all ten:

1. `r0-r3` ↔ `x0-x7`, zero-extended, for up to four integer/pointer arguments — with the explicit
   warning not to reuse `x0-x3` alone.
2. Stack arguments copied both directions with an offset correction.
3. 64-bit return pair `r0:r1` → `x0` on the way in, split back on the way out.
4. Hidden sret pointer in `r0`, `this` shifted to `r1`, **not** the HFA rule.
5. Softfp single/double repacked **by class, not by position** — an `(int, double)` guest signature
   consumes `r0` then `r1:r2` while AAPCS64 uses `x0` then `d0`.
6. **Float arguments may arrive in `s0-s15`** — per call site, per signature, unverifiable at
   runtime, silent on failure. Read off the disassembly. **Rule: not in the table until classified.**
7. Predicate helpers return `r0 ∈ {0,1}`, never flags.
8. Preserve callee-saved guest state (`r4-r11`, `sp`, and CPSR unless deliberately changed); do not
   expose the host stack to guest code.
9. Write results back through the JIT — via the scratch round trip of §4.4 in our design, or
   `Jit::Regs()`/`ExtRegs()` under an SVC handler (§4.7).
10. **Keep the wrapper.** The compatibility fixes stay in the host; only the selected body moves.
    `NATIVE-HOOK-MECHANISM.md:204-205` is binding and interacts with §1.12: the path aliases, the
    GL/EGL marshalling, Storm's repair guard and the engine byte patch must all still be in the path.

**What the requirement-8 point means mechanically in our design:** the ARM64 body is an ordinary
AAPCS64 function. It preserves `x19-x29`/`sp` by the platform ABI, it never sees the guest stack
pointer as its own `sp`, and guest pointers are zero-extended 32-bit values that must never be
truncated from a host pointer — the invariant `MATH/README.md:57` recorded and
`NATIVE-HOOK-MECHANISM.md:220-221` warns about ("ARM64 code and all data/stack pointers above 4 GiB").

**Shim generation, not hand-writing.** The shim is per **signature**, not per function
(`NATIVE-HOOK-MECHANISM.md:166-168`), so:

* A small signature table expresses each distinct parameter class list, with the sret flag, the
  return class, and — for any signature containing a float or double — a mandatory
  `vfp_args_at_call_sites: bool` field that must be filled from disassembly.
* A generator emits the packing/unpacking code, or a generic marshal driven by the signature record
  is used (slower but one implementation to audit). **Start generic**; only specialise if a target's
  call rate justifies it.
* The signature record is **hash-pinned alongside the target's VA and original bytes**, so a target
  cannot silently be re-pointed at a different function or a different ABI by a game update.

**What "keep the wrapper" means for the ABI check.** Because the wrapper stays, the *compiled*
`libzbridge`-style native-call path (`NativeRegs`, `thunks.S`) is still exercised for
`RegisterNatives`, so the two directions can be cross-checked against each other: the host→guest
marshal is already validated by the 36 registered natives at P1 (`ZB core/include/zb/native_call.h:11-40`).
That is a free oracle for the inverse direction's register layouts.

### 4.6 Failure modes and the degradation contract

| Failure | Symptom | Design response |
| --- | --- | --- |
| Hook return convention inverted | Every block it touches ends early or never ends; wholesale breakage, likely a crash from a missing terminal | Code comment cites `translate_arm.cpp:33-36`, not `config.h:70-71`. Unit-test the hook on a known function with an assertion that the block has a terminal. |
| Mode (ARM/Thumb) wrong for a target | The hook never fires; the pure-JIT path silently continues — a *no-op*, indistinguishable from "no win" | Take the mode from ELF mapping symbols; require the P4 hit-counter gate to be non-zero before any performance claim. |
| Missing terminal on `false` | `ASSERT_MSG` fires in a debug build; undefined block in a release build | `SetTerm` is unconditional before every `false` return. |
| Float argument in the wrong place | **Silent wrong values, no fault** | Integer-only first target; per-signature `vfp_args` classification from disassembly; bit-exact differential test with all-registers-plus-memory comparison. |
| Scratch page race between guest threads | Intermittent wrong results under thread churn | Per-thread scratch, allocated at thread creation. |
| Wrong sret / `this` shift | Wrong results for struct-returning functions, or corruption of the caller's frame | Only add sret signatures after the by-value ABI is settled; `quaternion::slerp` is recorded as an **open ABI defect not to inherit** (`RECON/STATUS.md:409-418`). |
| Target's entry bytes are also a branch destination from inside the function | The native body runs on a path the original would not have taken | This is legitimate (the hook replaces the *function*, and re-entry at entry is re-entry); but a target whose entry is *mid-function* must not be hooked. Enforce by requiring the target's VA to be a known function start from the symbol table. |
| The native body re-enters guest code | Guest re-entrancy under a stopped JIT; possible deadlock | The first targets must be **pure leaf functions**. `NATIVE-HOOK-MECHANISM.md:284-285`: "Keep it a pure function of its arguments; do not re-enter guest code." |

### 4.7 The alternative we should keep in our pocket (and why owning the host unlocks it)

`NATIVE-HOOK-MECHANISM.md:120-129` contrasts the hook with a second seam: splice `svc #imm; bx lr`
(8 bytes) into the guest function's entry and handle the index in the host's SVC dispatcher. It
preferred that shape **for shipping**, for two reasons that no longer fully apply to us:

* It avoids the translation hot path — irrelevant under §4.1's corrected cost model.
* It reuses the project's proven byte-patch discipline — still true and still valuable
  (`DOCS/ARCHITECTURE.md:115-128`).

But it carried one **UNKNOWN**: "whether the ARM32 mapping is writable at runtime — the offline file
patch sidesteps this" (`NATIVE-HOOK-MECHANISM.md:123`). **Owning the loader removes that UNKNOWN.**
The existing host maps guest code host-readable only, deliberately (`ZB core/include/zb/guest_memory.h:27-29`),
because it never needs to write it. Our loader can map the guest's executable segments
readable+writable, because **guest code is never executed natively in either host** — it is only read
by `MemoryReadCode` (`ZB core/src/guest_thread.cpp:313-316`) and by fastmem. Consequences:

* Hook stubs can be installed **at load time from a data file**, with no offline byte patch of the
  engine — so the target set is configuration, not a patch, and adding a target does not require
  re-running `WORK patches/engine/patch_engine.py`.
* `svc` handling runs with the JIT **stopped** (`CallSVC`, `ZB core/src/guest_thread.cpp:328-334`;
  stop kinds at `guest_thread.h:43-53`), so the handler can read and write
  `Jit::Regs()`/`ExtRegs()` (`ZB core/src/guest_thread.cpp:69-75`) directly — **no IR gymnastics, no
  scratch page, and requirement 9's write-back problem disappears.**
* The cost is one SVC crossing per call (the existing host already pays this for every GL call:
  ~680/frame at `DOCS/PERFORMANCE-BASELINE.md:80-81`), versus zero crossings for the hook.

**Recommendation on ordering, stated plainly:** build `PreCodeReadHook` **first** (P4), because it
needs no guest-text mutation, it exercises the whole ABI, it is the smaller and more reversible
change, and it is what the brief asks to be designed. Keep the SVC-stub route as **P6's promotion
step**, taken only if the hook's per-block-translation cost or its block-termination constraints bite
in practice — and take it *because our own loader makes it safe*, not because it was inherited.

---

## 5. Cost and risk

### 5.1 Effort estimate

Sizes below are **our new code**, informed by the measured sizes of the components being replaced
(§1.0), not copies of them. Calibration anchor: the toolchain, the patch pipeline and the differential
verification harness all already exist and are verified (`DOCS/NATIVE-BUILD.md:7-9`,
`DOCS/PERFORMANCE-BASELINE.md:90-98`), so P0 is days, not weeks.

| Phase | Work | New code (approx.) | Effort |
| --- | --- | ---: | --- |
| P0 | Project skeleton, our own sysroot extractor, build/bundle scripts, provenance + notices, re-census tooling | ~400 LOC + scripts | **2–4 days** |
| P1 | Guest memory + ELF loader + 4 relocations + symbol resolution; guest linker/library mode + ARM32 service stub + carrier/borrower; measured syscall subset; JNIEnv bridge + discovery + `RegisterNatives` + `JNI_OnLoad`; guest threads + TLS + `clone`; minimal signals; trimmed runtime report; minimal launcher | ~4 500–6 000 LOC | **5–8 weeks** |
| P2 | 92 GLES2 entry points (80 generated + 12 manual), guest stub library, hardened `eglGetProcAddress`, driver cache, swap accounting | ~1 000–1 300 LOC | **3–4 weeks** |
| P3 | Input/lifecycle through JNI, thread churn under load, memory-pressure behaviour, the four compatibility behaviours live, long-run hardening | ~600–1 000 LOC (mostly fixes) | **2–4 weeks** |
| P4 | Hook override, target table + page-directory lookup, signature table, shim generator/marshal, scratch pages, one integer-only target, differential verification | ~700–1 100 LOC | **2–3 weeks** |
| P5 | Programmatic display-mode request in our own activity | ~50 LOC + measurement | **2–4 days** |
| P6 | Scale-out over the ranked shortlist; optional promotion to RW-text SVC stubs; profiling loop | ~1 500–3 000 LOC depending on how many targets and whether the shim is specialised | **4–8 weeks** |
| — | **Total to the P4 gate (a hook-proven host that plays)** | **~7 000–9 500 LOC** | **~11–16 weeks** of focused single-engineer work |
| — | **Total to performance parity-plus (P6)** | **~9 000–13 000 LOC** | **~4–6 months** |

**The honest headline: the host is ~4–6 months, and the thing that buys the 82.7% is the *last* two
weeks of it.** P4 cannot be reached without P1–P3, because you cannot hook a function in a host that
does not run the game.

### 5.2 The components most likely to blow up (ranked)

1. **The synthesized `JNIEnv`/`JavaVM` bridge plus guest thread/TLS/`pthread_*` emulation
   (components 7, 12).** ~2 752 lines of `core/src/jni/*` plus the carrier/borrower model plus
   reflection-based native discovery (`ZB core/android/jni_env_backend.h:96-130`) — and the inverse
   marhsalling at `ZB core/include/zb/native_call.h:11-40` + `ZB core/src/jni/thunks.S:28-61`. This is
   where "36 registered natives" (`WORK evidence/fold7-build/phone-report-test1-excerpt.txt:17`) is
   won or lost, and it is the largest single subsystem. It is also the least diagnosable: a wrong
   local-reference frame or a swallowed pending exception produces a subtly different app, not a
   crash. **Mitigation: make P1's counter gate fail loudly and specifically, and treat each of the 36
   registrations as a named test case.**
2. **The guest linker + library mode + the 4 GiB allocator under pressure (components 1, 4).**
   `DOCS/GUEST-MEMORY-INVESTIGATION.md:302-350` documents a real, load-dependent failure class that
   took a full investigation to attribute. Our allocator is new code on a known-hard problem.
   **Mitigation: keep `find_free`'s highest-fit semantics exactly (`guest_memory.cpp:91-104`), adopt
   the ranked fix from `DOCS/GUEST-MEMORY-INVESTIGATION.md:388-407` early rather than late, and gate
   P3 on the 30-minute run.**
3. **Signal emulation and the EINTR/interrupt design (component 13).** The existing design has a
   non-obvious invariant — a reserved host signal with an *empty* handler and no `SA_RESTART`, purely
   so a guest signal can reach a thread asleep in a futex (`ZB core/src/signals.cpp:145-160`,
   `ZB core/include/zb/guest_thread.h:97-105`). Getting this wrong produces hangs under thread churn,
   which is the hardest class of bug to attribute to its cause.
4. **The per-signature VFP argument split (requirement 6).** Explicitly identified as the single
   largest correctness risk because it fails **silently** (`NATIVE-HOOK-MECHANISM.md:185-192`).
   **Mitigation is procedural, not technical:** integer-only first target, classification from
   disassembly before promotion, all-registers-plus-memory differential testing
   (`NATIVE-HOOK-MECHANISM.md:294-305`).
5. **The 32-bit struct/sockaddr/msghdr translations in the syscall layer (`syscalls.cpp:175-1058`).**
   These cannot be differentially tested against the guest because they are host-facing. They are
   individually small and collectively easy to get subtly wrong. **Mitigation: implement them from
   the ARM EABI struct definitions in `ZB core/include/zb/guest_abi.h` semantics, and test each with
   a purpose-built guest test binary — the project already has this pattern
   (`ZB guest/tests/syscalls_dynamic.c`, `signal*`, `threads_dynamic.c`, `mremap_dynamic.c`,
   `filemap_dynamic.c` at `ZB tools/build_guest.sh`) and the expected outputs
   (`ZB guest/tests/expected/*`).** Reimplementing those tests' *expectations* is cheap and is the
   highest-value verification asset available; it is also the one asset that is ZettaBridge-authored,
   so it must be re-derived from the host's observable behaviour rather than copied.
6. **Build and packaging regressions.** The build is byte-reproducible today
   (`DOCS/NATIVE-BUILD.md:7-9`) and the packaging has hard-won invariants: 4 KiB page enforcement
   (`DOCS/ARCHITECTURE.md:152-155`), `extractNativeLibs=true` and the ARM64 `jniLibs` copy
   (`WORK build/build_apk.py:145,200`), the manifest rewrite and the `Stubs$SingleInstance` /
   `Stubs$Landscape` activity renaming (`WORK build/build_apk.py:147-156`). **Mitigation: keep the
   existing packaging script and swap only the host payload
   (`WORK build/build_apk.py:113-121,191-200`), which is precisely the substitution lever
   `DOCS/NO-FORK-NATIVE-OPTIONS.md:411-421` documents.**

### 5.3 Regression risk against the current baseline

The baseline to protect is concrete: **~30 minutes, 8 map loads, no crash**, `8 survived, 0 died,
0 SIGSEGV` over eight consecutive launches (`DOCS/PERFORMANCE-BASELINE.md:154-157`,
`WORK evidence/dh2work-r2/launch-test.txt`), at 25–27 FPS with a 60 Hz ceiling
(`DOCS/PERFORMANCE-BASELINE.md:13-22`, `DOCS/FRAME-LIMITER.md:59-73`).

Three risk statements, honestly separated:

* **Loud regression risk (a crash or a launch failure): moderate and well-instrumented.** The
  counters in §3's P1 gate and the 8-launch protocol in P3 catch these on the first run, and the
  fallback is a working APK.
* **Silent regression risk (the app runs but is subtly wrong): this is the real danger, and it is
  high.** Every one of §1.12's ten behaviours is a place where a plausible-looking reimplementation
  produces a game that loads the wrong asset, loses the player's language, or fails after 20 minutes.
  The mitigation is to make each one an *observable*: the same guest-log lines, the same paths, the
  same playlist behaviour, the same saved-language value. §3's P3 gate is written that way on purpose.
* **Attribution risk: high without the report.** `DOCS/ARCHITECTURE.md:75-78` explains why the guest
  runs in a separate `:guest` process: "a translated abort exits that process with a status while the
  `:main` setup activity survives to collect and export the report". Our host must reproduce that
  separation and that reporting, or a failure becomes unattributable — which is exactly the class of
  problem `DOCS/ARCHITECTURE.md` was written to prevent.

### 5.4 Recommendation: **incremental, alongside the working build.** Not big-bang.

Five reasons, each grounded:

1. **The failure mode is a slow-burn fidelity loss, not a hard error.** The failure class in
   `DOCS/GUEST-MEMORY-INVESTIGATION.md:302-350` appears after sustained play. A big-bang rewrite is
   tested against a *new* baseline and has nothing to diff against; an incremental one is diffed
   against a baseline measured on the same device with the same protocol
   (`DOCS/PERFORMANCE-BASELINE.md:7-22,154-157`).
2. **The substitution lever already exists and is one file.** `WORK build/build_apk.py:113-121`
   re-walks `assets/zb/**` and rewrites the manifest and version hash, and `:191-200` is the only
   place the APK's `lib/arm64-v8a/*.so` come from — so the guest payload, the sysroot and the host
   library can be swapped independently, with no change to the other side
   (`DOCS/NO-FORK-NATIVE-OPTIONS.md:411-423`). Incremental is *cheap here*, which is unusual and
   should be used.
3. **The existing build is a free, continuously-available oracle.** Eight clean launches, 6.57 M GL
   calls, 38 252 native calls, a known FPS distribution, a known GL-call-per-frame figure. Big-bang
   throws that away for the entire duration of the rewrite; incremental keeps it and can A/B at every
   gate.
4. **The riskiest subsystem (JNI/threads) has no partial-credit path in a big-bang.** In an
   incremental build, P1 can land with, say, 30 of 36 natives and still produce an attributable
   failure. In a big-bang, "it does not start" tells you almost nothing.
5. **The performance work does not need the rewrite to start.** P4's ABI work, the shim generator and
   the differential verification can all proceed against the offline Unicorn oracle
   (`DOCS/PERFORMANCE-BASELINE.md:90-98`) while P1–P3 are still being written, so calendar time
   overlaps instead of serialising.

**What must *not* be done.** Do not build a patched ZettaBridge to prove the hook. It would be a
derived work, the constraint forbids it, and it buys nothing: the hook proof is a P4 gate **inside our
own host**, and the ABI confidence it needs comes from the offline oracle plus the P1 native-call
cross-check, neither of which requires touching ZettaBridge's source. Read ZettaBridge, cite
ZettaBridge, and do not build it.

### 5.5 What is kept as the fallback

* **The shipping APK, byte-for-byte unmodified, for the whole programme.** It runs at 25 FPS with 8/8
  clean launches. This is the fallback for every gate.
* **The `WORK` patch layer and all four compatibility behaviours** (§1.12) — they are host-agnostic,
  they are validated, and they keep working across the swap because they operate on guest bytes and
  guest Java.
* **The verified build pipeline** (`DOCS/NATIVE-BUILD.md:453-469`, the seven scripts) — it becomes
  the *reference* implementation of the toolchain, and P0 reuses its three non-obvious deltas
  (`DOCS/NATIVE-BUILD.md:239-254`).
* **The offline differential verification harness** (`dh2_oracle.py`, `difftest.py`,
  `auto_rewrite.py`, `verify_rewrites.py`, `port/engine-math`) with its recorded
  3 009 functions / 63 118 comparisons / 0 mismatches (`DOCS/PERFORMANCE-BASELINE.md:90-98`) — this
  is the asset that makes P4 and P6 trustworthy, and it is independent of which host runs the game.
* **The optional save-job synchronisation patch** (`WORK patches/thread-exhaustion/patch_sync_jobs.py`,
  `DOCS/ARCHITECTURE.md:127`) if per-frame thread creation causes trouble in the new allocator.
* **The tier-2 guest-only routes** — the `__aeabi_*` → VFP body replacement in the sysroot
  (`DOCS/SOFTFLOAT-FINDING.md`, `DOCS/TIER2-CONSTRAINT.md:27,42`) and hot-function ARM32 rewriting
  (`DOCS/TIER2-CONSTRAINT.md:28`, `DOCS/PERFORMANCE-BASELINE.md:104-113`). These attack the same 82.7%
  **without any host change at all**, and they compose with the new host: a guest-side VFP rewrite and
  a host-side native replacement are not alternatives, and the former is available today.

---

## 6. Open items (UNKNOWN — each with the cheapest way to settle it)

| # | Unknown | Settled by |
| --- | --- | --- |
| 1 | Is the Thumb bit present in the `pc` passed to `PreCodeReadHook`? | Log `(pc, is_thumb)` for a known target on first translation. Minutes. |
| 2 | The exact reachable syscall-number set | Per-number first-use counter, read off after the P3 run. Exact, not analytic. |
| 3 | The exact used `JNINativeInterface` slot set and flat-JNI host-call set | Per-slot / per-index counters, read after P2. |
| 4 | Whether the game ever needs a guest `ALooper` on a borrower thread | `is_borrower` semantics (`library_runtime.h:69-74`); instrument and observe. If never, the carrier/borrower model may collapse to something much simpler. |
| 5 | The per-call-site VFP/core-register split for each hooked function | Disassembly per function, before promotion. Binding rule: no float target until classified. |
| 6 | Whether the 12 extension GL entries (`gl_ext_entries.inc`) are ever resolved at runtime | The host already gates `eglGetProcAddress` (`ZB core/src/gl/egl_manual.cpp:279-286`); log requested names. |
| 7 | Whether `preferredDisplayModeId` / `Surface.setFrameRate` actually lifts the plateau | The live A/B experiment `DOCS/FRAME-LIMITER.md:414-420` prescribes; P5. |
| 8 | Whether hooking N functions moves the 82.7% figure measurably | simpleperf on a `DH2_PROFILEABLE=1` build (`WORK build/build_apk.py:164-170`), same method as `DOCS/PERFORMANCE-BASELINE.md:116-131`. |
| 9 | Whether the merged-APK option (no plugin/proxy layer) is safe | Cost it at P0; the proxy mechanism's ABI requirement must be re-derived, not assumed away. |
| 10 | Whether a rebuilt host has *ever* been run | It has not, for ZettaBridge (`DOCS/NATIVE-BUILD.md:392-399`: "no rebuilt library has ever been executed"). **Our programme's P1 gate is the first time this happens at all**, which raises the value of P1 and the value of the fallback. |

---

## Appendix A — measured census, as run

All numbers below were produced by `pyelftools` 0.33 / `capstone` 5.0.9 on
`C:\Users\NacWorkstation\Documents\DH2Work-scratch7\`; raw JSON retained there (`census.json`,
`syscall_scan2.json`) as the falsification record for §2.4.

**A.1 Hostcall table** — `ZB core/src/gen/hostcalls.inc`: 579 entries, indices 0–578, lines 2–580.
Per library: `libGLESv2.so` 259 (0–343, first at line 2), `libandroid.so` 184 (142–491, line 144),
`libGLESv1_CM.so` 87 (492–578, line 494), `libEGL.so` 46 (168–215, line 170),
`libjnigraphics.so` 3 (217–219, line 219). 259 + 184 + 87 + 46 + 3 = 579. ✔

**A.2 Guest stub libraries** — `ZB guest/stubs/gen/`: `libGLESv2.S` 259 `svc`, `libGLESv1_CM.S` 145,
`libEGL.S` 46, `libandroid.S` 184, `libjnigraphics.S` 3 = **637 stub functions**, using **579
distinct** indices (58 GLES1 names share a GLESv2 index). ✔

**A.3 Guest imports (the reachability answer)**

| Module | `DT_NEEDED` | Undefined | Jump-slot relocs | Host-provided imports |
| --- | --- | ---: | ---: | --- |
| `libDungeonHunter2.so` | `libc, libGLESv2, libstdc++, libm, libGLESv1_CM, libdl, liblog` | 361 | 351 | **92** (all `libGLESv2`) |
| `libStormGLOFT.so` | `liblog, libz, libm, libdl, libEGL, libGLESv1_CM, libGLESv2, libandroid, libc, libstdc++` | 126 | 1 345 | **7** (subset of the 92) |
| `libnativeinterface.so` | `libc, libstdc++, libm, libdl` | 19 | 16 | **0** |

Union of host-provided imports = **92**. `egl*` imports = 0 in all three. `AAsset*` imports = 0 in all
three. Imports resolving to a GLES1-only index = 0 in all three. `dlopen`/`dlsym`/`dlclose` imported
by Storm only. `__aeabi_*` undefined: 40 / 10 / 3.

**A.4 Relocations** — §1.2's table. Four types total across all three modules.

**A.5 Runtime bundle** — `BUNDLE` has 22 entries, 7 595 660 B uncompressed:
`jniLibs/arm64-v8a/libzbridge.so` (3 306 992), the ten sysroot files (4 169 016),
`assets/zb/guest/zbhost` (6 324), the seven guest stub/compat libraries
(`libzbridge`-side: `libzbcompat.so` 3 764, `libzbjni.so` 35 744, `libGLESv2.so` 24 668,
`libGLESv1_CM.so` 13 164, `libEGL.so` 5 432, `libandroid.so` 21 200, `libjnigraphics.so` 1 588),
`assets/zb/host/libzbproxy.so` (7 152), `assets/zb-files.txt` (551), `assets/zb-version.txt` (65).
The version hash is `9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06`
(`DOCS/NATIVE-BUILD.md:281`).

**A.6 Code size of what is being replaced** (relevant scope signal, `ZB` tree):
`core/src` 58 files / 18 945 lines; `core/include` 56 / 4 943; `core/android` 23 / 2 314;
`core/src/jni` 16 / 2 752; `core/src/gl` 6 / 3 872; `core/src/gen` 12 / 5 127;
`android/launcher/.../java` 24 / 2 863; `guest` 42 / 8 122; `tools` 12 / 2 340.
The explicit translation-unit list is `ZB core/CMakeLists.txt:1-46` (45 entries).

## Appendix B — provenance and licence position

| Artefact | Origin | Position |
| --- | --- | --- |
| **Dynarmic** | `https://github.com/Vita3K/dynarmic` at `86458a0bd369d63ba4c2ef812cacbb6c9080c065` (verified with `git rev-parse HEAD`) | **0BSD** — per-file SPDX headers (`DYN src/dynarmic/frontend/A32/translate/translate_callbacks.h:3`, `.../a32_ir_emitter.h:4`), consistent with `DOCS/NO-FORK-NATIVE-OPTIONS.md:634-639`. Free to use and patch; notice obligations are minimal and the project already ships Dynarmic licences (`WORK build/build_apk.py:177-178`). |
| **ARM32 bionic sysroot** | AOSP GSI `aosp_arm64-exp-CP41.260814.003.B1-16166531-e6cb3bc5`, pinned SHA-256, fetched by our own extractor modelled on `ZB tools/extract_sysroot.sh:1-38` | AOSP/bionic (Apache-2.0/BSD). **Re-derive it; do not copy it out of the ZettaBridge tree.** Add an explicit notice entry (§1.5). |
| **ZettaBridge** | PolyForm Noncommercial 1.0.0 + Perimeter 1.0.1, cumulative (`DOCS/NO-FORK-NATIVE-OPTIONS.md:596-644`) | **Read as reference only. Not shipped, not copied, not linked, not patched, not built.** No file in the new tree may originate from `ZB`, and no build step may read from it. The path census and ABI facts in this document are *facts about a binary and a source tree*, recorded with citations — the same status as `DOCS/NATIVE-HOOK-MECHANISM.md` and `DOCS/NO-FORK-NATIVE-OPTIONS.md`, which this document extends. |
| **The game** | Dungeon Hunter 2 HD v1.0.2, owner-supplied | Out of scope for this document; the game APK and cache are not shipped by the project (`DOCS/ARCHITECTURE.md:189`). |

This is not a legal opinion; the caveat at `DOCS/NO-FORK-NATIVE-OPTIONS.md:626` (citing
`WORK RIGHTS.md:9`) applies.
