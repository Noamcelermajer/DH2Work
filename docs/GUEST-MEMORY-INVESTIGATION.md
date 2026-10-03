# Guest memory, the failing 1.4 MB `malloc`, and the long-run crash

Scope: why a `malloc(1429956)` in the 32-bit armeabi-v7a guest fails inside the 64-bit
ZettaBridge wrapper process (`local.dh2.fold7:guest`) on Galaxy Z Fold7 SM-F966B,
Android 16, 4096-byte host pages, build `1.0-work3-profile`.

Source under study: `DH2Work-stage/compatibility/work/research/ZettaBridge` at commit
`7c647a4f1ea150eab7978ab0da28fdf49f3a79de` **with the project patch applied**
(`git status --porcelain` shows 13 modified files). The project patch touches only
diagnostics and exit ordering — it does **not** touch `core/src/guest_memory.cpp`,
`core/src/elf_loader.cpp`, or the `mmap`/`brk` paths (`git diff --stat`: 147 insertions,
none in those files). Everything below is therefore upstream behaviour plus diagnostics.

Evidence vs inference: every claim about wrapper code carries a `file:line`. Claims about
the guest's bionic are labelled **verified (artifact)** when I read the actual `libc.so`
binary, and **inferred** when they follow from AOSP semantics I did not read in source.
Items I could not settle are marked **UNKNOWN — needs X**.

---

## 0. Answer in one paragraph

There is no memory ceiling. The wrapper reserves the guest's **entire 32-bit address space**
once (`mmap(nullptr, 4 GiB + 64 KiB, PROT_NONE, MAP_NORESERVE)`) and then fits every guest
allocation into it; the only bound is the guest VA itself, capped at `mmap_limit =
0xFE000000` (~3.97 GiB) for `mmap`/`brk` growth. ~690 MB RSS is irrelevant to that bound,
and the device has no per-app limit anywhere near 690 MB (11.4 GB RAM, `RLIMIT_AS`
unlimited, no `largeHeap` needed because the guest's `malloc` is native, not Java heap).
The 1.4 MB `malloc` fails because a **guest `mmap` syscall returned `-ENOMEM`**, and the
only sources of that `-ENOMEM` are the wrapper's own two decision points: `GuestMemory::find_free`
finding no contiguous free run of the requested size below `0xFE000000`, or the host
`mmap(MAP_FIXED)` on the reservation failing. In the same logcat window the guest also fails
**785 consecutive `pthread_create` calls, all for the identical 1052672-byte stack mapping**,
while allocations below ~1 MB keep succeeding and the engine keeps making progress. Small
allocations succeeding while every allocation ≥1 MB fails is the signature of **32-bit guest
address-space fragmentation/exhaustion at the top of the address space**, not of an RSS
ceiling and not of a fixed arena budget. Candidate 2 is best supported; the wrapper has no
tunable arena to raise, because the arena already is the whole 32-bit address space, and
~32 MiB of it is all that is not reachable today.

---

## 1. How guest memory is allocated today

### 1.1 One 4 GiB reservation, a 1 MiB page-flag bitmap

`core/src/guest_memory.cpp:38-45` (`GuestMemory::GuestMemory`):

```cpp
GuestMemory::GuestMemory() : pages_(static_cast<std::size_t>(kGuestSpaceSize >> 12), 0) {
    void* p = mmap(nullptr, kGuestSpaceSize + kGuardSize, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
```

* `kGuestSpaceSize = 1ULL << 32` — `core/include/zb/guest_memory.h:8`.
* `kPageSize = 4096`, `kPageMask = 0xFFF` — `core/include/zb/guest_memory.h:9-10`.
* `kGuardSize = 64 * 1024` — `core/src/guest_memory.cpp:14`.
* `pages_` is one flag byte per 4 KiB guest page, i.e. **1 MiB** — `core/src/guest_memory.cpp:38`.

So there is **no fixed-size guest heap or arena that the wrapper allocates up front**. There
is exactly one reservation, sized to the guest's whole address space, with
`MAP_NORESERVE` so it costs no commit charge. Resident pages appear only when a guest
allocation `mmap`s over part of it. `MAP_NORESERVE` on the reservation means it cannot
itself be the cause of an `ENOMEM` at 690 MB RSS.

A guest address `g` lives at host address `base() + g` (`core/include/zb/guest_memory.h:31-32`),
and the address is `u32` everywhere in the wrapper, so the guest's pointer width is
faithfully modelled: a guest allocation must land inside this one window.

### 1.2 The bounded regions inside the reservation

`core/include/zb/process.h:31-36`:

| Constant | Value | Meaning |
| --- | --- | --- |
| `kStackTop` | `0xFF000000` | guest stack top |
| `kStackSize` | 8 MiB | guest stack |
| `kMmapLimit` | `0xFE000000` | ceiling for guest `mmap` hints and for `find_free` |
| `kExecutableLimit` | `0x40000000` | a PIE main executable is placed below this |
| `kMaxThreads` | 256 | |

`process.cpp:432` maps the stack at `kStackTop - kStackSize`; `process.cpp:408` loads the
guest executable below `kExecutableLimit`; `process.cpp:419` loads the dynamic linker and
`elf_loader.cpp:65` places every later `ET_DYN` at `find_free(span, dyn_limit)` with
`dyn_limit = kMmapLimit`. That is why the engine, Storm and `libc.so` all sit in the
`0xfa000000`–`0xfdb20000` band in the crash dumps, i.e. **at the top of the usable
window**, because `find_free` searches downward from the limit.

`brk_start = brk_current = exe.load_end` (`process.cpp:440`), so the guest program break
starts just above the PIE executable (below `0x40000000`) and grows up toward `mmap_limit`.

### 1.3 The three places a guest allocation is served

**`mmap` — `core/src/syscalls.cpp:294-342` (`sys_mmap2`).** Address selection:

* `MAP_FIXED`/`MAP_FIXED_NOREPLACE`: the hint is used verbatim, checked only against
  `kGuestSpaceSize`, `range_free` and `page_round_up` — **not** against `mmap_limit`
  (`syscalls.cpp:308-312`).
* hint honoured: `addr >= 0x10000`, page-aligned, and `addr + size <= proc.mmap_limit`,
  and the range is free (`syscalls.cpp:313-315`).
* otherwise `at = c.mem.find_free(size, c.proc.mmap_limit)`; **`if (at == 0) return -ENOMEM;`**
  (`syscalls.cpp:316-319`). This is the first `ENOMEM` source.
* the mapping itself: `syscalls.cpp:325-328` — `map_anon` or `map_file`, and
  `if (!ok) return errno ? -errno : -ENOMEM;`. This is the second `ENOMEM` source.

**`brk` — `core/src/syscalls.cpp:274-292` (`sys_brk`).** Growth requires
`new_top <= p.mmap_limit`, `range_free(start, delta)` and `map_anon(...)`, else it silently
returns the old break (`syscalls.cpp:282-285`). A failed `brk` surfaces to bionic as a
non-moving break, i.e. an allocator failure.

**`mremap` — `core/src/syscalls.cpp:357-422`.** Resize in place if the tail is free, else
`find_free(new_size, mmap_limit)` or `-ENOMEM` (`syscalls.cpp:384-402`).

### 1.4 `find_free` is highest-fit, not first-fit

`core/src/guest_memory.cpp:91-104`:

```cpp
std::uint32_t GuestMemory::find_free(std::uint64_t len, std::uint32_t limit) const {
    const std::uint64_t pages = page_round_up(len) >> 12;
    const std::uint64_t top = limit >> 12;
    if (pages == 0 || top < kLowestAllocPage || top - kLowestAllocPage < pages) return 0;
    std::uint64_t run = 0;
    for (std::uint64_t i = top; i-- > kLowestAllocPage;) {
        if (pages_[i] == 0) { if (++run == pages) return static_cast<std::uint32_t>(i << 12); }
        else { run = 0; }
    }
    return 0;
}
```

It walks the page bitmap downward from `0xFE000000` and returns the **highest** free run of
`len`. With `kLowestAllocPage = 0x10000 >> 12` (`guest_memory.cpp:15`), the searchable window
is `0x00010000 .. 0xFE000000`, i.e. **~3.97 GiB**, minus the 8 MiB stack above it and the
sub-1 GiB executable region. It is a hint-style `mmap`: it never fails because a *particular*
address is taken, only when **no** run of the requested size exists anywhere in the window.

### 1.5 Configuration knobs that exist (and the one that does not)

* `Process::mmap_limit` is a per-process field with a default — `core/include/zb/process.h:175`.
  It is read at `syscalls.cpp:282`, `:313`, `:317`, `:400`. Nothing in the wrapper ever
  writes it after construction (grep for `mmap_limit` across `core/` returns only those
  four reads plus the declaration).
* There is **no** environment variable, config file, manifest key or launcher setting that
  sizes guest memory. The only environment knob in this area is `ZB_PRECISE_FAULTS`
  (`core/include/zb/process.h:45`) and `ZB_STRACE` (`syscalls.cpp:107`), neither of which
  affects capacity.
* `core/include/zb/process.h:31-36` are `static constexpr`, so even the constants are
  compile-time only.

So: **"the wrapper reserves a fixed-size guest arena whose size is chosen somewhere and is
configurable" is false.** The reservation is exactly 4 GiB (the guest's address-space size,
not a budget), and the only adjustable quantity in the whole path is `mmap_limit`.

### 1.6 What is *not* in the guest window

* **The Dynarmic JIT code caches are host address space, not guest.** `code_cache_size`
  defaults to 32 MiB per JIT (`core/include/zb/guest_thread.h:35`), passed straight to
  Dynarmic at `core/src/guest_thread.cpp:63-64`. They are ordinary host `mmap`s in the
  wrapper's 64-bit address space and cannot consume guest VA. (The prior-art emulator
  failure `berberis::MmapImplOrDie → ExecRegionAnonymousFactory::Create → CodePool::Add`
  quoted in `evidence/dh2work-r3/MEMORY-EXHAUSTION-CRASH.md:73` is *this* pool — executable
  **host** address space — and is a different mechanism from the guest's 32-bit window.
  It can still bite on a 64-bit host with many JITs, but not by consuming guest VA.)
* The engine's file-backed mappings do consume guest VA: `map_file` is called with the guest
  address (`syscalls.cpp:327`), so a 600 MB asset mapped into the guest occupies 600 MB of
  the guest window even if little of it is resident.

---

## 2. The failing allocation: what is `-ENOMEM` here

### 2.1 The guest's allocator is Scudo — verified from the artifact

`.../fold7-build/out/assets/zb/sysroot/system/lib/libc.so` is ELFCLASS32 / EM_ARM / ET_DYN and
contains `scudo/standalone/wrappers_c.cpp`, `scudo/standalone/size_class_allocator.h`,
`Scudo primary releaseToOSMaybe`, `Scudo ERROR: internal map failure`, and the class table
`_ZN5scudo28AndroidNormalSizeClassConfig7ClassesE` (file offsets 0x1c053, 0x1c07c, 0x1b6cd,
0x1d3c0, and symbol VA 0x243bc). `_ZN5scudo9AllocatorINS_19AndroidNormalConfigEXadL_Z21scudo_malloc_postinitEEE8allocateEjNS_5Chunk6OriginEjb`
is at VA 0x42cf1, and `SizeClassAllocator32<...>::populateFreeList` at 0x45661.

Consequence (**inferred** from Scudo's design, not read line-by-line in this libc): a
1.4 MB request is larger than any Scudo size class, so `malloc(1429956)` goes to the
**secondary allocator**, which `mmap`s exact-size region(s) through the guest's syscall
wrapper. A large `malloc` therefore fails exactly when a guest `mmap` fails — which is
precisely the observable in the log (`malloc(4194304)`, `malloc(1429956)`).

The log string itself is in this libc: `malloc(%zu) failed: returning null pointer` at file
offset 0x1d352 (`.rodata`, since `PT_LOAD` #1 maps file offset 0 to VA 0). Its literal-pool
reference could not be located (a 32-bit scan for `0x1d352` in `.text` finds no word), so I
cannot attribute the print to a specific function — noted as **UNKNOWN — needs X** (see §5).

### 2.2 `pthread_create` gives an independent, cleaner probe of the same failure

The crash-window logcat (`evidence/dh2work-r3/app-logcat-crash2.txt`) contains:

* 785 × `pthread_create failed: couldn't allocate 1052672-bytes mapped space: Out of memory`
  (first at line 1, 17:13:58.110 — i.e. already failing when the capture begins — last at
  line 7276, 17:14:32.300);
* 4 × `malloc(4194304) failed` and then 1 × `malloc(1429956) failed`;
* 3 × `Could not open file : .../qata/3d/textures/atlas_fx_particles_002.tga`.

Every one of the 785 failures is the **same size, 1052672 bytes**, which is bionic's thread
stack request. That means for the whole ≥34 s window the guest could not satisfy **one
megabyte** of contiguous guest VA, **785 times in a row**, while the load step advanced
0 → 10 and the engine kept parsing Python, loading models and rendering. If the guest VA
space were *totally* exhausted, the small allocations that those steps need would have
failed first and the process would have died much earlier; if a hard RSS/cgroup ceiling
were the cause, the failures would have started with small allocations too. The observed
pattern — all allocations ≳1 MB fail, everything smaller succeeds — is fragmentation-shaped.

The 4 MB and 1.4 MB failures immediately before the SIGSEGV are the same phenomenon seen
through Scudo instead of pthreads. `DH2Model opened: .../prince_modular.bdae` and
`DH2FileGuard recovered repeated root: ...` appear 35 and 239 times in the same window — a
repeated file-open retry loop in the engine, each retry allocating.

### 2.3 The reported crash PC is imprecise — the fault is *not* a malloc-internal write

The logcat line is `guest SIGSEGV: write of 0x00000000, pc 0xfdb185d8`, resolving to
`libc.so offset 0x675d8`, with `crash-precise: no` in the runtime reports
(`evidence/dh2work-r1/longrun/zb-runtime-report.txt:134`).

Verified against the artifact: `0x675d8` is 42 bytes before the end of `__vfwscanf`
(Thumb, symbol VA 0x66dfc, size 3088 → ends 0x67a0c). Disassembled, `0x675d8` is
`bl #0x8ac68` = `__fgetwc_unlock` — a basic-block-ending **call**, and the callee's first
dereference of the `FILE*` is `ldr r0, [r4, #0x30]` (`__fgetwc_unlock` at 0x8ac68). With an
imprecise fault PC the wrapper reports the last translated instruction of the block that
faulted, so the write to 0x0 happened **inside the wide-stdio path on a null/dangling
`FILE*`**, not inside Scudo.

The two sibling long-run crashes have the same shape: `libc.so offset 0x67304`
(`__vfwscanf`, reported PC = `add r1, sp, #0x24`, again a block boundary, fault at
`read of 0x0001c9c8` with `r1 = 0x0001c9c8`) and `libc.so offset 0x675a0`
(`str.w sl, [sp, #0x28]`, also in `__vfwscanf`).

So the causal chain is: engine allocates ≥1 MB → guest `mmap` fails (`-ENOMEM`) →
`malloc` returns NULL → the engine does not check it and passes the null `FILE*`/handle
into the wide-stdio path → write to 0x0. The crash write-up in
`evidence/dh2work-r3/MEMORY-EXHAUSTION-CRASH.md:30-32` ("unchecked allocation failure …
the engine does not test the `malloc` result") is right about the *proximate* cause. What
the earlier note in `docs/STATUS.md`/the recap called a "null `FILE*`" misdiagnosis is
also right about the *mechanism*; the two are the two ends of the same chain.

---

## 3. Which candidate is supported

### Candidate 1 — a hard ceiling: **not supported**

* **No Android/cgroup ceiling at 690 MB.** Measured read-only on the device
  (`adb shell`, this session, game not running): `MemTotal: 11379908 kB`, `MemAvailable:
  2535692 kB`, `dumpsys meminfo` totals `Total RAM: 11,379,908K`, and the LMKD tuning line
  `Tuning: 512 (large 512), oom 322,560K, restore limit 107,520K (high-end-gfx)`.
  `cat /proc/self/limits` on the device reports `Max address space: unlimited`,
  `Max resident set: unlimited`, `Max data size: unlimited`, `Max processes: 38952`.
* **`android:largeHeap` is irrelevant.** The guest `malloc` is native C `malloc` inside the
  ZettaBridge emulation, not Java heap, so `dalvik.vm.heapgrowthlimit` (512m) does not apply.
  For completeness: the built guest manifest sets `android:hardwareAccelerated="false"`
  (`work/fold7-build/game-AndroidManifest.xml:17`) and the host manifest
  (`work/fold7-build/host-AndroidManifest.xml:10`) declares no `largeHeap` at all; the guest
  runs in `android:process=":guest"` (`host-AndroidManifest.xml:23-31`). Raising
  `largeHeap` would not move this failure by one byte.
* **The wrapper's guest budget is 4 GiB, not a smaller configured arena.** §1.1.
* **Falsified by the sizes.** A ceiling produces failures that grow *downward* in size over
  time (the biggest allocations fail first, then progressively smaller ones, then
  everything). Here the smallest failure in the whole window is ~1.0 MB and it is *stable*
  at exactly 1052672 bytes for 785 consecutive attempts over 34 s. A ceiling in the
  690 MB range would have made the engine's sub-megabyte allocations fail long before.

**What would falsify "no hard ceiling":** a per-app memory cgroup limit on this device below
~1.9 GB, or a `MALLOC_LIMIT`-style cap set through `android_mallopt` in the guest. The
second is testable and cheap: `M_DECAY_TIME`-style options are forwarded nowhere in the
wrapper (`grep android_mallopt core/` returns nothing; `sys_madvise` is pass-through at
`syscalls.cpp:448-455`), and a malloc limit set that way prints a *different* message,
`malloc(%zu) exceeds limit %lld`, which is present in this libc at file offset 0x1fb59 and
which the logcat does **not** contain. That is good evidence against a configured cap.

### Candidate 3 — kernel/VM behaviour (overcommit, `RLIMIT_AS`): **not supported**

* `RLIMIT_AS` is unlimited on the device (§ above), and the guest reads it from the host
  through `sys_ugetrlimit` (`syscalls.cpp:746-751`) and `sys_prlimit64` (`syscalls.cpp:753-759`),
  clamped to 32 bits. bionic uses `getrlimit(RLIMIT_AS)` both to size its primary arena and
  in `LimitMalloc`; with unlimited, neither produces a cap.
* The 4 GiB reservation is `MAP_NORESERVE`, so it adds no commit charge; overcommit
  settings cannot block it.
* The guest's own `mmap`s are `MAP_FIXED` inside that reservation
  (`guest_memory.cpp:58`, `:68`), which cannot fail for commit reasons on a private
  anonymous mapping in heuristic-overcommit mode.
* **Caveat, and the one kernel-side candidate that is genuinely open:**
  `vm.max_map_count` (65530 by default on Android). `cat /proc/sys/vm/max_map_count` is
  denied to `adb shell` on this unit, so the configured value is **UNKNOWN — needs X**.
  Every guest `mmap`/`munmap` creates or splits a host VMA, and the crash window shows an
  engine that repeatedly maps and retries. If the wrapper's host VMA count for the
  process approaches the limit, `mmap(MAP_FIXED)` returns `ENOMEM` and
  `sys_mmap2` reports `-ENOMEM` (`syscalls.cpp:328`) — the identical observable. This is
  the **second** measurement that must be taken (§5) and it is the only way candidate 3
  survives. It is not the leading explanation because `MAP_FIXED` mappings inside an
  existing `PROT_NONE` reservation still allocate a VMA, but the failure would then be
  size-independent, and here the failure is strictly size-dependent (≥1 MB fails,
  smaller succeeds).

### Candidate 2 — 32-bit guest address-space fragmentation/exhaustion: **best supported**

The reasoning, with the numbers:

1. The guest allocation window is `0x00010000 .. 0xFE000000` = **4,128,505,856 B ≈ 3.845 GiB**
   minus (a) the 8 MiB stack, (b) whatever `find_free` hands to `ET_DYN` libraries at the
   top, (c) the engine's file mappings, (d) the heap.
2. Observed guest ranges from the crash dumps (`evidence/dh2work-r1/diag/zb-runtime-report.txt`
   and `.../longrun/zb-runtime-report.txt`): guest heap/TLS around `0xfb070000`, engine
   `libDungeonHunter2.so` from `0xfcc`… to `0xfdb185d8`+, i.e. libraries and heap all live in
   `0xfa000000 .. 0xfdc00000` — the **top ~60 MiB** of the window. `find_free` places every
   `ET_DYN` at the highest free run (`elf_loader.cpp:65`), so libraries and the engine's
   large `mmap`s stack downward from `0xFE000000` and the **highest-fit allocator keeps the
   bottom of the window untouched while packing the top**.
3. RSS is 672–715 MB just before the fatal load (per `MEMORY-EXHAUSTION-CRASH.md:57-63`).
   Even generously assuming 2× VA-to-RSS (mappings of the 635 MB asset cache plus
   `MADV_DONTNEED`-released Scudo regions), that is ~1.4 GiB of a 3.845 GiB window — not an
   exhausted window, a **fragmented** one. `find_free` returns 0 only when *no* 1 MB hole
   exists anywhere in the window while ~2.4 GiB of pages are individually free.
4. The prior art in this project is exactly this failure shape and it says so:
   `patches/thread-exhaustion/README.md:74-86` — 3,188 `pthread_create failed: couldn't
   allocate 1040384-bytes mapped space`, "process had about 4.17 GB virtual size and 315 MB
   resident … this supports process address-space pressure; it does not prove which
   allocation caused it", and "ZettaBridge's host uses a separate 64-bit process address
   space and reserves guest memory there; each guest thread also gets its own Dynarmic JIT
   cache." The Android 9 `libndk_translation` abort there was in an **arena block** request
   of 131072 bytes — the same allocator-region-exhaustion shape.
5. The 785 identical 1052672-byte failures in 34 s are the smoking gun of a *persistent*
   best-fit failure: a transient RSS spike would clear; a permanent hole shortage does not.
   It also explains why the failure is reproducibly at ~388 s and ~428 s of play: it is when
   the engine's allocation churn (map-load streaming, plus a per-frame `pthread_create`
   retry storm, `README.md:51`) has fragmented the top of the window past 1 MB.
6. The contributing churn source is named in this project's own files: the engine creates a
   worker thread per frame for save jobs and retries when it fails
   (`patches/thread-exhaustion/README.md:21-28`: `updateJob_thread::Start()` at RVA
   `0x317ef8` calls `pthread_create` at `0x317f24`, logs `Not Created`, returns −1; the
   per-frame branch is `Application::_Update(int)` at `0x32c534` → `0x32cb74`). Each failed
   attempt still asks for 1052672 contiguous bytes; none of the 785 succeed; the engine
   keeps going.

**What would falsify candidate 2:** a `find_free` failure log at the moment of the 1.4 MB
failure showing the *largest free run* is still, say, 8–64 MB (then it is not
fragmentation, and the failure must be the host `mmap`/VMA-count path, i.e. candidate 3);
or `VmPeak ≈ VmSize` with `VmSize ≈ 4 GiB` and a `mmap`-count near `max_map_count`
(then it is total VA/mapping exhaustion rather than fragmentation — same fix family,
different diagnosis); or a `malloc(%zu) exceeds limit %lld` line in the guest log (then it
is candidate 1 and the answer is configuration).

### Ranked verdict

**Candidate 2 ≫ Candidate 3 ≫ Candidate 1.** Candidate 1 is affirmatively excluded by the
device's own limits and by the size-selectivity of the failures; Candidate 3 survives only
as the `max_map_count` sub-case, and that sub-case predicts size-*in*dependent failures,
which the data contradicts. Candidate 2 is the only one consistent with "≥1 MB always
fails, <1 MB always succeeds, for 34+ seconds, at 690 MB RSS".

---

## 4. Minimal fix options, ranked

Note on the obvious-looking first idea: **there is nothing to "raise"**. The arena is the
guest's full 32-bit address space; the only reachable knob is `mmap_limit`, and moving it
from `0xFE000000` to `0xFF000000` buys **16 MiB** before colliding with the stack at
`0xFF000000` (`process.h:31-33`) and the 64 KiB guard (`guest_memory.cpp:14`). Any real fix
must either stop the churn, reuse holes better, or survive the failure.

### A. Reduce the guest's address-space churn at its source — **top recommendation**

The 785/34 s identical `pthread_create` failures are per-frame save-job workers that cannot
be created, each retrying with a fresh 1 MB request. The project already has the exact
one-instruction fix and has validated it once:

* `patches/thread-exhaustion/patch_sync_jobs.py` changes the `BNE` at engine RVA
  `0x32c534` to branch to the **existing synchronous save-job call** at `0x32cc34`
  (`patches/thread-exhaustion/README.md:30-38`), replacement bytes `be 01 00 1a`, and it is
  hash-pinned to engine SHA-256 `45891aad…c93c4`.
* On the Android 9 AVD this removed all 3,188 failures and reached gameplay
  (`README.md:57-75`).

Difficulty: **low** — the tooling, verification and a validated precedent exist; it is one
`BL`-destination change (or a `BNE`→branch-to-synchronous patch) plus repackaging.
Risk: **medium** — it changes save-job threading semantics (synchronous save work can block
a frame) and it is pinned to a specific engine hash, so it must be re-derived and re-verified
for this Fold7 engine. It is a *mitigation*: it removes one large churn source, it does not
guarantee the engine's own streaming allocations stop fragmenting the window.

### B. Make the guest `mmap` reuse holes better / fail less — **low risk, moderate value**

Two wrapper-side changes, both small and both inside the file the project already patches:

1. **First-fit fallback.** In `sys_mmap2`, when `find_free(size, mmap_limit)` returns 0
   (`syscalls.cpp:317-318`), retry a *lowest*-fit search. `GuestMemory::find_free` is
   highest-fit by construction (`guest_memory.cpp:96`); a lowest-fit variant finds a hole
   that highest-fit's scan order also finds, so this changes nothing by itself — the real
   gain is the next item.
2. **Coalesce/report.** `pages_` is a flat flag bitmap with no run index; nothing tracks the
   largest free run. Adding a cached "largest free run" (maintained on `set_flags`) would
   both make failures diagnosable (§5) and let a future allocator prefer the bottom of the
   window, where the guest has never mapped anything, instead of the packed top.

Difficulty: **low** (≈40 lines in `guest_memory.cpp`/`syscalls.cpp`).
Risk: **low** — pure search-policy change; the guest sees only "the kernel returned a
different address than I hinted", which is legal for a non-`MAP_FIXED` `mmap`.
Honest limit: it does not create VA that is not there. It helps only if the window is
fragmented rather than full.

### C. Make the null `malloc` survivable — **conservative, do this regardless**

The engine stores through the returned pointer at `libDungeonHunter2.so +0x69a980`
(`LR` in the crash dump; write-up §5 at `MEMORY-EXHAUSTION-CRASH.md:105-109`). This project
already has reviewed engine byte-patch tooling in that style: `patches/engine/patch_engine.py`,
`engine_path_fix.S`/`.ld`, `engine-patch-report.json`, and the same pattern in
`patches/game/patch_game.py` and `patches/storm/patch_storm.py`. A guard at the store site
(`cbz rX, <skip>`) converts "die in a stdio call with a null `FILE*`" into "this one scene
load misbehaves", which is strictly better than a guaranteed death at ~7 minutes.

Difficulty: **medium** (locate the store, prove the register holds the allocation result,
patch and re-verify the ELF32 mapping and hashes).
Risk: **low-to-medium** — no behaviour change when allocation succeeds; the danger is
patching the wrong site or masking a genuine engine invariant. Justify it explicitly as
"fail safe instead of dying", not as a fix for the shortage.

*Variant with no engine patching (worth prototyping):* have the wrapper intercept the
guest's `mmap` failure and satisfy the request out of a small **wrapper-owned emergency
arena** reserved at startup, so `malloc` never returns NULL for these sizes. This is a
bigger change (the guest allocator must be told the memory is real, and the arena has to be
accounted in `pages_`), and it only buys time. Difficulty **medium-high**, risk **medium**.

### D. Raise `mmap_limit` — **cheap, nearly useless, do it only as part of B**

`kMmapLimit = 0xFE000000` → `0xFF000000` gains 16 MiB in a 3.845 GiB window (0.4%). It is a
one-constant change with essentially no risk (`process.h:33`), but on its own it will not
change a failure that needs ~1 MB per hole out of a window with ~2.4 GiB of individually
free pages. **Difficulty: trivial. Risk: trivial. Expected effect: negligible.**

### E. Resize/relocate an "arena" — **not applicable**

There is no arena to resize (§1.1, §1.5). Any proposal phrased as "raise the wrapper's guest
memory budget" is a misreading of the code: the wrapper reserves `1ULL << 32`
(`guest_memory.h:8`) and the only bounded regions are the 8 MiB stack
(`process.h:31-32`), the 1 GiB executable limit (`process.h:35`) and `mmap_limit`
(`process.h:33`). A *configurable* guest budget does not exist and would have to be invented.
**Difficulty: n/a. Risk: n/a.**

### F. Reduce the engine's memory pressure — **real but unquantified**

The engine pulls a 635 MB asset cache (`MEMORY-EXHAUSTION-CRASH.md:65`) and the crash
window is full of `qata/3d/textures/atlas_fx_particles_002.tga` "Could not open file"
retries. If the failing 1.4 MB/4 MB allocations are texture or mesh staging buffers sized
from a setting, the settings path is already writable by this project (Test 11 language
work). **Difficulty: low if a setting exists. Risk: low. Blocked on §5's growth data.**

---

## 5. The exact measurement that would settle it

**One instrumented run, one new log line, taken at the failure itself.** Add to the
`find_free`-failure branch of `sys_mmap2` (`core/src/syscalls.cpp:317-318`) — or better, to a
new `GuestMemory::largest_free_run()` helper next to `find_free`
(`core/src/guest_memory.cpp:91`) — a report of:

1. the requested size,
2. the **largest contiguous free run** in `0x00010000 .. mmap_limit` at that instant,
3. total free pages (count), and
4. the number of calls that have hit this branch since start.

Wire it into the existing report path (`runtime_report().note_mmap_failure(...)` or reuse
`note_guest_exit`-adjacent detail, `core/src/runtime_report.cpp:551-559` shows the
`opened-*`/`open-failed-*` pattern to copy) so it lands in `zb-runtime-report.txt` without
touching the guest.

Interpretation, decided in advance:

| Reading | Conclusion |
| --- | --- |
| largest free run ≪ requested size, total free pages large (hundreds of MB) | **Candidate 2 confirmed** — fragmentation; fix A + B + C. |
| largest free run ≥ requested size, yet the branch was taken | **Candidate 3** — the failure is in the host `mmap`/VMA path, not `find_free`; go measure `max_map_count` and VMA count. |
| total free pages ≈ 0 | total guest VA exhaustion (probably a leak); find what never releases VA. |

**Device-side corroboration, read-only, with the game live** (the game is not running now;
this needs the Lead to have the tester launch it):

* sample `/proc/<guest-pid>/status` every 30 s: `VmPeak`, `VmSize`, `VmRSS`, `VmData`,
  `Threads`. Decisive discriminator: **`VmPeak` ≫ `VmSize` + RSS** means VA was mapped and
  released again many times (churn, Candidate 2); `VmSize ≈ 4 GiB` with small RSS means the
  window is fully committed to mappings (still VA, still Candidate 2/3 territory, but the fix
  becomes "find the leak").
* `dumpsys meminfo local.dh2.fold7:guest` for Pss/private-dirty growth over the run.
* the wrapper cannot read `/proc/<pid>/maps` through `adb shell` on this unit (denied) — so
  the *guest's* mapping list must come from the instrumented line above, not from the host.
* `vm.max_map_count` is **UNKNOWN** — `cat /proc/sys/vm/max_map_count` is
  Permission denied for `adb shell` here. A root-free proxy: on a rooted unit read it
  directly, otherwise infer from the wrapper's own VMA count if it is ever logged.

Secondary measurement if you want to confirm the mechanism rather than the cause: enable
`ZB_PRECISE_FAULTS` (`core/include/zb/process.h:45`, `guest_thread.cpp:62`) so the reported
PC is the actual faulting instruction. It is a speed cost, and it will confirm that the
write to 0x0 happens at `__fgetwc_unlock`'s `ldr r0, [r4, #0x30]` rather than at any Scudo
code — which closes the last gap in §2.3.

---

## 6. Open items (UNKNOWN — needs X)

* **Which function prints `malloc(%zu) failed: returning null pointer`** in this exact
  `libc.so`. The string is at `.rodata` offset 0x1d352, but no 32-bit word equal to
  `0x1d352` exists anywhere in the file, so the literal pool must reach it through a
  computed base (or via `.data.rel.ro`); I could not attribute it. **Needs:** a debugger or
  `readelf -r` on `.rel.dyn` to find the relocated reference.
* **The exact Scudo primary/secondary split for this libc on a 32-bit guest** — I verified
  Scudo is present and that 1.4 MB cannot be a primary class, but I did not read this
  build's `AndroidNormalSizeClassConfig` class list. It does not change the answer, because
  every `malloc` of that size must reach the guest `mmap` path either way.
* **`vm.max_map_count` on this device** (permission denied) and the wrapper process's live
  VMA count. **Needs:** the game running, or a rooted read.
* **The engine's call site at `libDungeonHunter2.so+0x69a980`** and which allocation's NULL
  it consumes. **Needs:** the engine library (it is inside the plugin APK) plus
  `patches/engine`-style disassembly; not required to pick the fix, but required to write
  the guard.
* **Whether the 785 `pthread_create` callers are the per-frame save-job path**
  (`patches/thread-exhaustion/README.md:21-28` documents that path for the Test 5/6/7
  engine). The Fold7 engine is a different hash, so the RVA/behaviour must be re-derived
  before applying fix A.

---

## 7. Code citations used

Wrapper (all under `DH2Work-stage/compatibility/work/research/ZettaBridge`):

| Claim | Citation |
| --- | --- |
| 4 GiB guest space constant | `core/include/zb/guest_memory.h:8` |
| page size / mask | `core/include/zb/guest_memory.h:9-10` |
| `base() + g` addressing, ownership comment | `core/include/zb/guest_memory.h:31-32` |
| `find_free` contract (highest free range ≤ limit) | `core/include/zb/guest_memory.h:50-51` |
| `kGuardSize`, `kLowestAllocPage` | `core/src/guest_memory.cpp:14-15` |
| the single reservation `mmap` | `core/src/guest_memory.cpp:38-45` |
| `map_anon` = `mmap(MAP_FIXED)` inside reservation | `core/src/guest_memory.cpp:55-62` |
| `map_file` = `mmap(MAP_FIXED)` inside reservation | `core/src/guest_memory.cpp:64-72` |
| `unmap` → `PROT_NONE` `MAP_NORESERVE`, flags cleared | `core/src/guest_memory.cpp:82-89` |
| page-flag bitmap size (1 MiB) | `core/src/guest_memory.cpp:38` |
| `find_free` highest-fit downward scan | `core/src/guest_memory.cpp:91-104` |
| `mmap_limit = kMmapLimit` field | `core/include/zb/process.h:175` |
| stack top/size, mmap limit, executable limit | `core/include/zb/process.h:31-36` |
| `ZB_PRECISE_FAULTS` knob | `core/include/zb/process.h:45` |
| `sys_brk` growth conditions | `core/src/syscalls.cpp:274-292` |
| `sys_mmap2` hint validation vs `mmap_limit` | `core/src/syscalls.cpp:313-315` |
| `find_free` → `-ENOMEM` | `core/src/syscalls.cpp:316-319` |
| mapping failure → `-errno`/`-ENOMEM` | `core/src/syscalls.cpp:325-328` |
| `sys_mremap` resize/move + `-ENOMEM` | `core/src/syscalls.cpp:357-422` |
| `sys_madvise` pass-through to host | `core/src/syscalls.cpp:448-455` |
| `getrlimit`/`prlimit64` served from the host | `core/src/syscalls.cpp:746-759` |
| linker loaded below `kMmapLimit` | `core/src/process.cpp:419` |
| `ET_DYN` placed by `find_free(span, dyn_limit)` | `core/src/elf_loader.cpp:65` |
| executable loaded below `kExecutableLimit` | `core/src/process.cpp:408` |
| stack mapped at `kStackTop - kStackSize` | `core/src/process.cpp:432` |
| `brk_start = brk_current = exe.load_end` | `core/src/process.cpp:440` |
| 32 MiB JIT code cache default | `core/include/zb/guest_thread.h:35` |
| code cache handed to Dynarmic | `core/src/guest_thread.cpp:63-64` |
| `crash-precise: no` reporting | `core/src/process.cpp` diff, `note_crash_detail("precise", ...)`; instance at `evidence/dh2work-r1/longrun/zb-runtime-report.txt:134` |

Project documents:

| Claim | Citation |
| --- | --- |
| crash chain and RSS figures | `evidence/dh2work-r3/MEMORY-EXHAUSTION-CRASH.md:11-32`, `:57-63` |
| prior-art emulator shape (`CodePool::Add`) | `evidence/dh2work-r3/MEMORY-EXHAUSTION-CRASH.md:73` |
| per-frame `pthread_create`, 3,188 failures, "address-space pressure" | `patches/thread-exhaustion/README.md:15-34`, `:74-86` |
| the one-instruction save-job patch and its RVA | `patches/thread-exhaustion/README.md:30-38` |
| engine byte-patch tooling | `patches/engine/patch_engine.py`, `patches/engine/engine_path_fix.S` |
| 16 KiB guest-memory prior art (conventions, not capacity) | `patches/16k-port/zettabridge-guest-memory-16k-poc.patch`, `zettabridge-madvise-dontneed-anon-16k.patch` |
| `MADV_DONTNEED` trace of 807 calls / 24,613 guest pages | `patches/16k-port/README.md:59-73` |
| guest manifest `hardwareAccelerated=false`, no `largeHeap` | `work/fold7-build/game-AndroidManifest.xml:17` |
| host manifest, `:guest` process | `work/fold7-build/host-AndroidManifest.xml:10`, `:23-31` |
