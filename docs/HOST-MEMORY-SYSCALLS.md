# HOST-MEMORY-SYSCALLS.md — guest address space and syscall layer

Scope: the host-side component that gives a 32-bit armeabi-v7a guest a believable Linux
address space inside a 64-bit process running on Dynarmic. Code lives under
`DH2Work-toolchain/host/mem/`. Everything below was built and run on this machine;
every number is from an actual run, quoted verbatim.

Reference read for understanding only (never copied): `DH2Work-stage/compatibility/work/
research/ZettaBridge`, plus `DH2Work/docs/GUEST-MEMORY-INVESTIGATION.md`.

---

## 1. What this is

| File | Role |
|---|---|
| `include/dh2mem/guest_abi.h` | 32-bit ARM syscall numbers, `mmap`/`mprotect`/`madvise` flags, negative-errno encoding, and `mmap2`'s 4096-byte offset unit |
| `include/dh2mem/mem_backend.h`, `src/mem_backend.cpp` | the only code that touches the kernel: one reservation, fixed-placement ops, an in-process fake backend for tests |
| `include/dh2mem/address_space.h`, `src/address_space.cpp` | the guest address space: reserve, allocate, free, protect, query, diagnose |
| `include/dh2mem/bitmap_space.h`, `src/bitmap_space.cpp` | the reference host's allocator, reimplemented from its documented behaviour, as the "before" side of the A/B |
| `include/dh2mem/syscall.h`, `src/syscall.cpp` | guest→host translation for `mmap2`, `munmap`, `mprotect`, `mremap`, `brk`, `madvise`, `msync`, `mincore` |
| `include/dh2mem/diag.h`, `src/diag.cpp` | structured failure records with the numbers that make a failure diagnosable |
| `tests/` | host-runnable tests; no device, no emulator, no NDK |

Build and run (WSL2 Ubuntu; cmake 4.2.3, Ninja 1.13.2, g++ 15.2.0):

```bash
cd /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/mem
cmake -S . -B /root/memtest-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /root/memtest-build -j 12
cd /root/memtest-build && ctest --output-on-failure
```

The same CMakeLists cross-builds for `arm64-v8a` with the NDK r29 toolchain file used in
`DH2Work/docs/NATIVE-BUILD.md`; the host test build is the fast path and is what was run.

---

## 2. Design

### 2.1 The model

The guest believes it owns a 32-bit Linux address space. The host owns a 64-bit one. The
component keeps **guest identity addressing** — guest address `g` lives at host
`base + g` — because the JIT needs a flat translation and a faulting guest address must
be computable as `host_address - base`.

The difference from the reference host is *what a guest mapping is made of*. The
reference makes every guest mapping a host `mmap(MAP_FIXED)` and every guest `munmap` a
host `mmap(MAP_FIXED, PROT_NONE)` over the same range, so the number of host VMAs tracks
the number of guest mappings. This component makes guest mappings **regions in a
software arena** carved out of a single committed reservation.

### 2.2 One reservation, one host mapping

`AddressSpace::init()` reserves `2^32` bytes plus a 64 KiB guard with
`PROT_NONE | MAP_NORESERVE`, then (default policy `CommitPolicy::kWholeSpace`) remaps the
whole window once with `MAP_FIXED | MAP_NORESERVE | PROT_READ | PROT_WRITE`.

The consequence is the whole point: `commit_calls == 1` at startup and no host mapping
operation ever happens again for anonymous guest memory. `AddressSpace::unmap` is pure
bookkeeping — it does not call `munmap` or `mprotect`, it only flips region metadata, so
a guest that maps and unmaps a hundred thousand times still holds the same host VMAs.

Measured (`memtest --filter="whole window is committed"`; the absolute VMA count drifts by
one or two between runs because the process's own mappings vary, so the property asserted
is "unchanged", not a fixed number):

```
host VMAs with the guest window live: 42 before, 42 after 5000 map/unmap cycles
host mapping calls: reserve=1 commit=1 protect=0 map_fd=0
```

5000 guest map/unmap cycles, zero additional host mappings, zero host `mprotect` calls.

`CommitPolicy::kWindowed` is kept as the degradation path: if the one-shot whole-space
commit is refused (a strict-overcommit host), the arena commits each region separately,
which restores reference-like VMA growth but keeps every semantic guarantee. Measured:

```
windowed policy: 256 host commits for 256 guest mappings
```

The access check stays metadata-driven (`accessible()` walks regions), so the design does
not depend on host page protections to model guest protection. Pages are left to fault in
on first guest touch, exactly as with per-mapping commits, so RSS behaviour is unchanged.

### 2.3 Placement: first-fit from the bottom

`find_free(size, limit, policy)` supports two policies because the reference host's
choice is the thing under measurement:

* `kLowest` (default): first fit from `lowest_alloc` upward. This is what the component
  does for automatic placement.
* `kHighest`: the highest run below `mmap_limit`, which is the reference host's policy —
  it packs every new mapping against the ceiling.

The default is `kLowest` because it is measurably better on every property that matters
here, not by taste:

1. **It cannot pad the window from the top down.** The reference host's libraries and
   every later streaming allocation are placed by a downward scan, so the top of the
   window fills first and stays packed; the device dumps show libraries, heap and engine
   all living in `0xfa000000..0xfdc00000`, the top ~60 MiB of a 3.845 GiB window.
2. **The bottom of the window is never mapped by the guest**, so first-fit always finds
   the largest untouched run first. In `memtest_real`, after the A/B workload the arena
   still holds a **4050 MiB contiguous free run** out of a 4 GiB window.
3. It is a single linear scan of a region vector that stays short (see 2.5).
4. It is the same policy the kernel uses for an un-hinted `mmap`, so guest-side
   assumptions about where an un-hinted mapping lands are not inverted.

`kAuto` tries `kLowest`, and only if that fails tries `kHighest`, recording a retry note
if the second one succeeds. That "lowest-fit found no run, highest-fit succeeded" line is
a different bug report from "the window is genuinely out of contiguous space", and the
two are distinguishable in the diagnostics.

### 2.4 The brk window is reserved, not free

`brk` must grow contiguously or not at all, and the heap must never collide with an
allocated mapping. `set_brk_window` holds the heap window as `Region::Kind::kReserved`:
automatic placement skips it (`find_free` only considers `kFree`), while an explicit
`MAP_FIXED` request can still claim part of it. A distinct kind is required because two
free regions with the same protection coalesce — an earlier version of this file used
`kFree` and the reservation was silently erased by the next coalesce, which one of the
tests caught.

### 2.5 Keep the host mapping count down, deliberately

Every `mmap`/`munmap`/`mprotect` that reaches the host is a VMA create, split or destroy,
and `vm.max_map_count` is 65530 by default on Android. The device denies reading that
sysctl, so this component treats the VMA count as a first-class cost and makes it small
by construction:

* one host mapping for anonymous memory, ever;
* adjacent regions with the same kind and protection are coalesced on every insert, so
  the region list — and therefore the VMA count — is bounded by the number of distinct
  *kinds and protections*, not by the number of guest mappings;
* `protect()` splits only where the protection actually changes and merges back
  immediately, so toggling `PROT_WRITE` on a window does not accumulate regions;
* file-backed mappings get their own VMA (they must), so `map_file` is the only path that
  grows the count.

Measured A/B on a real 4 GiB window, identical workload (8440 placements: 1200 × 1 MiB
thread stacks, 6 small allocations per frame, a 4 MiB streaming buffer every 30 frames):

```
reference (bitmap)     placements=8440  failures=0    stack-fails=0    largest-run now=4236996608  peak=4238049280   host-VMAs 42->46
reference total host mapping ops: 16876
redesign (arena)       placements=8440  failures=0    stack-fails=0    largest-run now=4245319680  peak=4246372352   host-VMAs 41->41
redesign total host mapping ops: 17
redesign commit windows: 3, arena capacity high-water: 8 regions
```

993× fewer host mapping operations, and a flat VMA count where the reference grew by four
over the same run. Both survived this workload, so this is a cost measurement, not a
survival claim. The reduction is what removes `max_map_count` as a candidate cause; it
does not by itself prove that candidate was the device's problem.

### 2.6 Failure is a first-class, numbered event

This is the part the reference host lacked and the part the device crash needed. Every
failure path builds a `MemDiag` and records it:

```
guest-mem failure #3: reason=fragmented op=mmap page=4096
  requested           : 256.0 KiB (262144 B)  (64 pages)
  largest free run    : 64.0 KiB (65536 B)  at guest 0x00010000   [allocatable window]
  free in window      : 64.0 KiB (65536 B)
  largest run anywhere: 128.0 KiB (131072 B)  at guest 0x00000000
  total free          : 16.062 MiB (16842752 B)  (4112 pages)
  live mapped         : 127.875 MiB (134086656 B)  in 1 regions
  hint                : 0x00000000  host errno: 0
```

Design points that make that record useful:

* **`FailReason` distinguishes causes**: `not-enough-total` (nothing left in the
  allocatable window) versus `fragmented` (free memory exists, no run is big enough)
  versus `host-map-failed` (the host refused the mapping; errno captured) versus
  `already-mapped` / `bad-range` / `bad-size`. The reference host collapses all of these
  into `-ENOMEM`, which is exactly why the device investigation had to infer the cause.
* **`free_below_limit_bytes` is reported separately from `total_free_bytes`.** Automatic
  placement cannot use anything above `mmap_limit`, so the reserved stack band must not
  be counted as available; the whole-space figure is still reported, labelled
  `largest run anywhere`, so the difference is visible.
* **The allocatable-window figures are the ones the reason is derived from.** `reason` is
  computed from `free_below_limit_bytes == 0 ? not-enough-total : fragmented` — the
  numbers and the verdict cannot disagree.
* `fail_reason_name()`, `format_mem_diag()`, `format_bytes()` and a capped process-wide
  `DiagSink` make the record printable and assertable in tests.
* Failures are counted by axis in the syscall layer (`fail_fragmented`,
  `fail_total_exhausted`, `fail_host_mapping`, `fail_bad_args`), so a guest-visible
  failure rate can be attributed without a debugger.

### 2.7 Syscall translation

`SyscallState` translates the 32-bit ARM conventions. The places where a naive port goes
silently wrong, and what this does instead:

| Area | Convention | Handling |
|---|---|---|
| `mmap2` offset | **4096-byte units**, not bytes, regardless of the guest's page size | scaled by `kGuestMmap2OffsetUnit`, then required to be a whole number of guest pages; a 16 KiB guest's `mmap2(..., pgoffset=1)` is `EINVAL` |
| return values | negative errno in the low 32 bits of `r0` | `SyscallResult.ret` is `int32_t`, filled with `-errno` |
| addresses | 32-bit; `addr + size` can wrap | all range checks are 64-bit (`window_contains`), so a wrapped range cannot look empty or in-bounds |
| non-`MAP_FIXED` hint | a hint, honoured only if it happens to be free, else ignored | hint used when it lands inside a free run with room; otherwise the policy decides. A hint is *not* rejected just because it is not page-aligned |
| `MAP_FIXED` | replaces what is there | the emulator's region list is the only record of live guest pages, so replacing live memory would destroy a mapping the guest still holds. A fixed mapping over live memory is refused with `already-mapped` and reported; over free or reserved memory it works |
| `MAP_FIXED_NOREPLACE` | `EEXIST` if anything is mapped | `already-mapped` |
| `brk` | grows contiguously or returns the old break; below `brk_start` is a *query* | growth fails by not moving; a value below `brk_start` returns the current break without moving |
| `mremap` | move/resize; `MREMAP_DONTUNMAP` unsupported | resize in place, else move with a copy; moving a `MAP_SHARED` file mapping is refused rather than silently detached; `MREMAP_DONTUNMAP` is `EINVAL` |
| `madvise` | `MADV_DONTNEED`/`MADV_FREE` on unmapped memory is a legal no-op | no-op without a host call when nothing is live; otherwise the host range is rounded **out** to the host page size, so a guest decision about one page cannot leak into a neighbour |
| `msync`/`mincore` | only meaningful for file mappings; residency is per page | `msync` on anonymous memory returns 0 without a host call; `mincore` re-slices the host residency vector to guest pages (on a coarser host, a guest page is resident only if all its host pages are) |
| `/dev/zero`, `MAP_ANONYMOUS` | anonymous memory | served as anonymous memory, never as a file mapping, so it costs no host VMA |

Structured for extension: `invoke(sysno, args, nargs)` dispatches by ARM syscall number
and returns `handled = false` for anything it does not own, so file and process calls can
be added as separate tables without touching the memory ones. `FileProvider` abstracts the
guest fd table; `HostFileProvider` (guest fds are host fds) is the honest single-process
mapping.

### 2.8 Page size

The component is page-size agnostic at runtime. `detect_host_pages()` reads
`sysconf(_SC_PAGESIZE)`; nothing assumes 4096. Guest page size is the accounting unit, and
every range handed to the host is rounded outward to the host page size. Host-side
alignment is asserted on every fixed operation, so an unaligned host call is a test
failure rather than a silent corruption. `--page-size=N` overrides it, which is how both
4 KiB and 16 KiB are verified.

---

## 3. Why it differs from the reference

| | Reference host | This component |
|---|---|---|
| Guest allocation unit | one host `mmap(MAP_FIXED)` per guest mapping | a region in an arena |
| Host mappings for anonymous memory | grows with guest mappings (VMA per mapping, split on `munmap`) | one, ever |
| Guest `munmap` | host `mmap(MAP_FIXED, PROT_NONE)` | metadata only |
| Placement | highest-fit: scan down from `mmap_limit` | first-fit up from `lowest_alloc`, with `kHighest` available and measured |
| Capacity bound | guest VA up to `mmap_limit`; no budget, no knob | same window, plus a real region arena with a reported high-water mark |
| Exhaustion reporting | `-ENOMEM` from two indistinguishable places | a numbered `MemDiag` with requested size, largest free run in the allocatable window, free in window, largest run anywhere, total free, live bytes, region counts, cause, host errno |
| VMA count | implicit, unmeasured | counted, asserted in tests, and reduced by ~10³× on the A/B workload |
| brk window | `range_free` check on each grow | reserved kind that survives coalescing |
| MAP_FIXED over live memory | `MAP_FIXED` replaces it | refused and reported (`already-mapped`) |

The design keeps the reference's good ideas — one 4 GiB `MAP_NORESERVE` reservation,
identity addressing, `mmap_limit` as the automatic-placement ceiling — and changes what a
guest mapping *is*.

---

## 4. Test results

Run on WSL2 Ubuntu, host page size 4096, kernel
`6.18.33.2-microsoft-standard-WSL2`. Build: `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`,
g++ 15.2.0, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. No warnings in the library;
the tests compile warning-free too.

```
$ ctest --output-on-failure
    Start 1: memtest_guest4k
1/4 Test #1: memtest_guest4k ..................   Passed    0.41 sec
    Start 2: memtest_guest16k
2/4 Test #2: memtest_guest16k .................   Passed    0.49 sec
    Start 3: memtest_host_native
3/4 Test #3: memtest_host_native ..............   Passed    0.43 sec
    Start 4: memtest_real
4/4 Test #4: memtest_real .....................   Passed    0.39 sec

100% tests passed, 0 tests failed out of 4
Total Test time (real) =   1.72 sec
```

`memtest` has 60 registered cases; 55 run in the reduced-window suite (the other five are
the real-dimension A/B, which runs in `memtest_real`). Both page-size instantiations
report `55 passed, 0 failed`:

```
$ ./memtest --page-size=4096   ->  55 passed, 0 failed
$ ./memtest --page-size=16384  ->  55 passed, 0 failed
$ ./memtest_real               ->  28 passed, 0 failed
```

### 4.1 Exhaustion is reported, not fatal

Real 4 GiB window, filling everything below `mmap_limit` in 4 MiB pieces:

```
usable window 3.969 GiB (4261347328 B); mapped before failure 3.965 GiB (4257218560 B) (99%)
guest-mem failure #1: reason=fragmented op=mmap page=4096
  requested           : 4.000 MiB (4194304 B)  (1024 pages)
  largest free run    : 3.938 MiB (4128768 B)  at guest 0xfdc10000   [allocatable window]
  free in window      : 3.938 MiB (4128768 B)
  largest run anywhere: 35.938 MiB (37683200 B)  at guest 0xfdc10000
  total free          : 36.000 MiB (37748736 B)  (9216 pages)
  live mapped         : 3.965 GiB (4257218560 B)  in 1 regions
```

The failure arrives after the arena has handed out **99%** of the usable window: the
remaining 3.938 MiB is real free memory that cannot hold the 4 MiB request, so the reason
is `fragmented`, not `not-enough-total` — and the two numbers that make that verdict
checkable (largest run 3.938 MiB, request 4 MiB) are both in the record. The address space
keeps working afterwards: the test releases a mapping and immediately re-allocates into
the same address. The failure is a returned `-ENOMEM` with a record, never a crash — that
is the difference from the device, where the engine stored through a null pointer returned
by `malloc`.

The genuinely-empty case is reachable too, and is reported as its own reason:

```
  requested           : 2.000 MiB (2097152 B)  (512 pages)
  largest free run    : 0 B  at guest 0x00000000   [allocatable window]
  free in window      : 0 B
  largest run anywhere: 64.0 KiB (65536 B)  at guest 0x00000000
  ... reason=not-enough-total
```

Fragmentation is a different reason, and the two are distinguishable:

```
  requested           : 256.0 KiB (262144 B)  (64 pages)
  largest free run    : 64.0 KiB (65536 B)  at guest 0x00010000   [allocatable window]
  free in window      : 64.0 KiB (65536 B)
  ... reason=fragmented
```

### 4.2 The churn pattern that killed the device

`memtest_real`'s A/B workload is the device's pattern: 1200 frames, each with a
1052672-byte thread-stack request (bionic's arm32 stack size, the exact size that failed
785 consecutive times on the device) plus six small allocations, plus a 4 MiB streaming
buffer every 30 frames. 8440 placements in total. Result for both allocators:

```
reference (bitmap)     placements=8440  failures=0    stack-fails=0
redesign (arena)       placements=8440  failures=0    stack-fails=0
```

Reduced-window version, 2500 frames, with the stack window and a library band pinned as on
device:

```
2500 thread-stack allocations of 1.004 MiB (1052672 B): 0 failures
free after churn: 108.000 MiB (113246208 B), largest run 107.938 MiB (113180672 B)
```

After the real-dimension workload is fully released:

```
after the workload: free 4067 MiB, largest run 4050 MiB, 3 free regions
```

A 4 GiB window still holds a **4050 MiB contiguous run** after the churn. This is the
property the device lost, and it is the property `kLowest` placement is chosen for.

Honest reading: **both** allocators survived this workload in this test. The test does not
reproduce the device failure and does not claim the reference allocator is broken in
general. What it does reproduce is the churn shape, and what it measures is the host
mapping cost (§2.5) and the retained contiguity.

### 4.3 Alignment and boundary correctness

* Every automatic placement is asserted page-aligned, `>= lowest_alloc`, `< mmap_limit`,
  and not crossing `mmap_limit` — over randomised sizes.
* Request sizes are rounded up, never truncated: a `ps + 1` request consumes exactly two
  pages.
* A zero-length `mmap` is `bad-range`/`EINVAL`, never a huge allocation.
* A fixed mapping at `2^32 - ps` succeeds; one that would cross `2^32` is refused, in a 4 GiB
  window, so the wrap is exercised with the real constant.
* Address 0 is refused for a fixed request and never chosen for a hint, while an explicit
  request for the lowest usable page is honoured.
* `accessible()` is exact at both ends of a mapping (last byte in, first byte out).
* Unaligned addresses and sizes are refused.
* Host alignment is asserted on every fixed operation.

### 4.4 Region bookkeeping

* Page-granular fill of the whole window: 28656 mappings coalesce into **one** live region
  (`live_region_count == 1`), which is the coalescing claim in executable form.
* 5000 map/unmap/protect cycles: VMA count unchanged (42 → 42), `protect_calls == 0` for
  a workload that never changes protection.
* 4000 mixed-size operations on a 128 MiB window: arena region-store capacity high-water
  **598** regions (bounded, and the check asserts a bound rather than a value, so it does
  not pin a running maximum), with an invariant check after the run.
* A reserve/fill/release cycle returns the arena to exactly one free region covering the
  whole window, so the metadata is exact and self-cleaning:

  ```
  filled 111 MiB in 1 live regions; after release: 1 free regions, largest run 128 MiB,
  arena capacity high-water 6
  ```

* The same 400-placement workload at 4 KiB and 16 KiB produces identical totals — same
  placement count, same first address, same free total, same region count, same largest
  run:

  ```
  page 4096: 400 placements, first at 0x65536, free after 128.000 MiB (134217728 B) in 1
             regions, largest run 128.000 MiB (134217728 B)
  page 16384: 400 placements, first at 0x65536, free after 128.000 MiB (134217728 B) in 1
             regions, largest run 128.000 MiB (134217728 B)
  ```

* Also at 16 KiB: a 4 KiB guest request costs exactly one 16 KiB page and unmapping it
  restores the window exactly; `madvise` on a sub-page range still reaches the host as one
  whole host page (asserted by counting host calls); and on a 4 KiB host with a 16 KiB
  guest page, `mincore` ANDs the host pages of each guest page rather than reporting a
  quarter-resident guest page as resident.

### 4.5 Bugs the tests caught in this implementation

Recorded because they are the failure modes a port of this kind hits, and because each one
is now a test:

1. `Region::end()` computed `va + pages * page_size` in 32 bits, so a region covering the
   whole 4 GiB window ended at **0**. Every placement below the first mapping then failed
   with a bogus `fragmented`. Fixed to 64-bit with a comment explaining why it must not
   narrow; a 64-bit `min64` replaced a 32-bit `min32` at the same call site.
2. `region_index_cover(va)` returned -1 for an address exactly at a region boundary — the
   first address of every fresh mapping. Hints were silently ignored and fixed placements
   fell back to automatic placement. Fixed, and the boundary case is tested.
3. `add_region` deleted the region it should have split, losing the head free run on every
   insertion. Rewritten as a single-pass split-and-coalesce; region bookkeeping is verified
   after every test case by `invariants_ok`.
4. `strip_free_run` refused to split a *reserved* region, so a fixed mapping inside the brk
   window silently truncated it.
5. A `MAP_FIXED` request over live memory was accepted and replaced live regions, because
   the "carve the free run" step fails over live memory and the failure was ignored. Now
   refused with `already-mapped` (`range_replaceable`).
6. `advise()` rounded a sub-host-page range down to zero length and dropped it silently.
   Now rounds outward and still issues exactly one host page.

---

## 5. What is not modelled

Stated plainly, because a compatibility host's value depends on knowing where it lies.

**Shared memory.** `MAP_SHARED` file mappings are tracked, but `mremap` of a shared file
mapping is refused with `EINVAL` rather than silently detaching it from the file. There is
no `shm`, no `memfd` semantics beyond an fd, no shared-memory coherency between guest
"processes" (there is only one). `msync` on such a mapping does reach the host.

**File-backed mappings.** A file mapping is a real host `mmap` of the fd, so it costs a
host VMA and its own bookkeeping. `mmap` of a file is supported through `FileProvider`, but
the tests exercise only the fd-resolution path and the refusal paths; no test opens a real
file, and file-size truncation (`SIGBUS` past EOF), `MAP_POPULATE`, huge pages and
`MAP_LOCKED`/`mlock` are not modelled (`mlock`/`munlock` syscall numbers are defined but
not dispatched).

**`fork`.** Single address space, single process. `brk`, the mapping list and the fd table
are process-global and there is no COW, no `MREMAP_DONTUNMAP`, no `fork`-aware anything.
`mincore` reports host residency, which is right for a single process only.

**Process and file syscalls.** Not implemented beyond the memory calls: no `read`/`write`
beyond what the host layer does elsewhere, no `open`/`close` translation, no signals, no
`prlimit64`/`ugetrlimit` (so a guest reading `RLIMIT_AS` gets whatever the host layer
provides). `invoke()` returns `handled = false` for all of them by design.

**Protection is metadata plus host `mprotect`.** Guest `PROT_NONE` does not make the host
page inaccessible, because the whole window is committed read-write and access is gated by
`accessible()`. A guest that bypasses its own metadata (the JIT does not) would not fault
where a kernel would. `PROT_GROWSDOWN`/`PROT_GROWSUP` are refused with `EINVAL` rather than
modelled. `PROT_EXEC` implies host read (the JIT reads guest code) and never host execute,
so JIT-and-W^X separation is the JIT's concern, not this layer's.

**`MAP_FIXED` over live memory is refused, not emulated.** A real kernel replaces the
mapping; this refuses with `EEXIST`-like semantics because the region list is the only
record of live guest pages. A guest that depends on `MAP_FIXED` clobbering its own live
mapping would see a failure instead.

**Partial host-page granularity.** When the host page is coarser than the guest's, a
sub-host-page guest range is rounded outward. This is correct but not free: a guest
`mprotect` of a 4 KiB sub-range of a 16 KiB host page affects the whole host page. No
guest-side API can express that, so it is documented rather than worked around.

**Diagnostics are process-wide and single-threaded.** `DiagSink` is a singleton with a
capped in-memory ring and optional stderr output. A multi-process host needs one sink per
guest process.

**Not measured on device.** Everything here ran on a host. No Android device or emulator
executed any of it, so `vm.max_map_count` on the target unit remains unknown, and the claim
"the VMA count is no longer a plausible cause" rests on the host-side count (993× fewer
host mapping operations, flat VMA count) plus the argument, not on a device measurement.

**The real 4 GiB `kWholeSpace` commit is not verified on Android.** The host test does
commit 4 GiB+ with `MAP_NORESERVE` and it succeeds on this kernel. Whether an Android
device's overcommit policy permits it is exactly the kind of thing the `kWindowed`
fallback exists for, and that fallback is tested, but the device path is not.

---

## 6. Reproducing the numbers

```bash
cd /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/mem
cmake -S . -B /root/memtest-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
  cmake --build /root/memtest-build -j 12
cd /root/memtest-build

./memtest --list                     # 60 cases
./memtest --page-size=4096           # 55 passed, 0 failed
./memtest --page-size=16384          # 55 passed, 0 failed
./memtest_real                       # 28 passed, 0 failed; the A/B numbers in §2.5 and §4.2
./memtest --filter="host VMAs"       # the VMA-count measurement in §2.2
./memtest --filter="diagnosable"     # the fill-to-capacity diagnostic in §4.1
```

`memtest_real` commits the full 4 GiB guest window with `MAP_NORESERVE`, so its RSS stays
in the low single-digit MiB; the reported figures include that.
