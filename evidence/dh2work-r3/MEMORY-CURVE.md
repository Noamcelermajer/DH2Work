# The memory curve: the guest stops being able to *map*, not to *fit*

Measured on Galaxy Z Fold7 SM-F966B, build `1.0-work3-profile`, sampling
`/proc/<guest-pid>/status` every 12 s from launch until the crash. Full log:
`memwatch.txt`.

| time | VmPeak GB | VmSize GB | VmRSS MB | VmSwap MB | frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| 17:17:16 | 22.07 | 21.95 | 296.9 | 29.8 | — |
| 17:17:28 | 22.23 | 22.09 | 382.3 | 29.8 | — |
| 17:17:40 | 22.23 | 22.14 | 476.6 | 29.8 | 120 |
| 17:17:53 | 22.29 | 22.18 | 546.4 | 29.8 | 240 |
| 17:18:05 | 22.37 | 22.18 | 566.3 | 29.8 | 360 |
| 17:18:53 | 22.37 | 22.19 | 596.3 | 29.8 | 1200 |
| 17:19:18 | 22.37 | **22.30** | 663.2 | 29.8 | 2040 |
| 17:19:30 → 17:22:32 | 22.37 | **22.25 flat** | 667 → **684** | 29.8 | 2400 → 7320 |
| 17:22:44 | — | — | — | — | **died** |

```text
W/libc    (18939): malloc(4194304) failed: returning null pointer
W/libc    (18939): malloc(1708308) failed: returning null pointer
I/zbridge (18939): guest SIGSEGV: write of 0x00000000, pc 0xfdaea5d8
```

## What the shape rules out

* **Not physical memory.** RSS peaks at **684 MB** on a device with **11.4 GB total, 946 MB
  free, 3.3 GB available**. And `malloc` failed for **4 MB**, then 1.7 MB.
* **Not `RLIMIT_AS`.** `/proc/<pid>/limits` on the app process reports
  `Max address space  unlimited  unlimited  bytes`, and `Max resident set` unlimited too.
* **Not a leak of *virtual* memory.** `VmSize` **stops changing** at ~22.25 GB while RSS keeps
  growing another ~20 MB, and then every allocation fails at a constant `VmSize`.

The process **loses the ability to create new mappings** while still having physical memory,
address-space limit headroom, and already-mapped pages it can still touch. The two
observations to explain are therefore:

1. `VmSize` freezes and `VmPeak == VmSize` (no growth at all after ~60 s).
2. `malloc` of 4 MB fails even though RSS is 684 MB and the device has 3 GB available.

## Leading hypothesis: the process hits `vm.max_map_count`

If the wrapper obtains guest memory with **one host mapping per allocation**, then the
process runs out of *mappings*, not bytes. When the count reaches the kernel's cap every
subsequent `mmap` fails with `ENOMEM`, the libc allocator correctly returns NULL, and
`VmSize` freezes at whatever it had reached — exactly the observed signature.

The arithmetic is consistent: 22.25 GB over a typical Android `vm.max_map_count` of 65,530
is **≈ 356 KB per mapping**, a very plausible average for this engine's allocation pattern.

Corroborating evidence:

* The project previously hit the same *failure mode* on an emulator — `berberis::MmapImplOrDie
  → ExecRegionAnonymousFactory::Create → CodePool::Add`, i.e. an `mmap` that failed while
  memory was available.
* This project has already patched guest memory handling three times
  (`zettabridge-guest-memory-16k-poc.patch`, `zettabridge-madvise-dontneed-anon-16k.patch`,
  `zettabridge-madvise-wipeonfork-16k.patch`) — the memory path is known and already touched.
* `VmData` for the app process is ~3.78 GB while `VmLib` is only ~196 MB, so the bulk of the
  reservation is anonymous data — consistent with many small anonymous mappings.

**Not yet verified.** `vm.max_map_count` and `/proc/<pid>/maps` are both denied to
`adb shell` on this device (`Permission denied`), so the mapping count could not be read
directly, and the hypothesis rests on the shape plus the arithmetic. The decisive check is in
the wrapper source: does guest memory come from a per-allocation `mmap`, or from a
pre-reserved arena with its own sub-allocator? A `statm`-style count is available
(`/proc/<pid>/statm` works where `/maps` does not), and `data` there is in pages.

## Why this is the fixable kind of bug

If it is a mapping-count cap, the fix is **inside the wrapper we can now rebuild**: carve
guest memory from one large pre-reserved arena and sub-allocate inside it, so the mapping
count stops tracking the guest's allocation count. That is a change to ZettaBridge, and the
native build now reproduces the shipped `libzbridge.so` byte-for-byte, so it is buildable and
verifiable today.

The alternative — raising the sysctl — needs root and would not travel to a stock device.

## Also established

* The crash reproduced for the **third** time, at ~7 minutes, with `malloc` failure preceding
  it on both fully captured runs. Three for three is a reliable repro.
* `malloc(4194304)` failing first (4 MB) and then a smaller one shows the engine retries and
  keeps going before finally writing through a null — so the failure is not a single
  unlucky allocation.

## Correction: this hypothesis is ranked below one with better evidence

A concurrent code-level investigation (`docs/GUEST-MEMORY-INVESTIGATION.md`) established the
actual mechanism and found stronger evidence for **guest 32-bit VA exhaustion/fragmentation
inside the wrapper's single 4 GiB guest reservation**, which outranks the `vm.max_map_count`
idea above (that one predicts *size-independent* failure, and the observed failures are
size-dependent).

Mechanism, verified in code:

* the guest is **one 4 GiB reservation**, `mmap(nullptr, 4 GiB + 64 KiB, PROT_NONE,
  MAP_NORESERVE)`, with guest address `g` at `base + g`
  (`core/src/guest_memory.cpp:38-45`, `core/include/zb/guest_memory.h:8`);
* every guest allocation is served inside it, bounded only by `mmap_limit = 0xFE000000`
  (`core/include/zb/process.h:31-36`, enforced at `core/src/syscalls.cpp:313/317/282/400`) —
  **no arena budget and no configuration knob**;
* guest `mmap` can fail in exactly two places: `find_free(size, mmap_limit) == 0`
  (`syscalls.cpp:317-318`), or the host `mmap(MAP_FIXED)` failing (`syscalls.cpp:325-328`).
  `find_free` is highest-fit downward (`guest_memory.cpp:91-104`).

The stronger evidence is in the captured guest log, which I had not parsed for this:
**785 consecutive `pthread_create failed: couldn't allocate 1052672-bytes mapped space`**
over 34 seconds — all the *same* size — alongside `malloc(4194304)` and then
`malloc(1429956)` failing **while every allocation below 1 MB still succeeds** and load steps
0→10 keep progressing. Size-selective and persistent, with the process still alive for
another 34 s: that is fragmentation/upper-window exhaustion, not a ceiling and not
overcommit.

## Correction: the proximate fault IS a wide-stdio null `FILE*`

I claimed earlier that the memory finding *overrode* the project's recorded blocker. That was
too strong and is corrected here.

The reported `pc libc+0x675d8` is imprecise (`crash-precise: no`) and resolves into
**`__vfwscanf`** (the `bl __fgetwc_unlock` call, symbol VA `0x66dfc`) — which is the *same
wide-stdio region* as the documented `0x675a0` in the `vfwprintf` wrapper. So the project's
proximate description — a null/dangling `FILE*` dereferenced in wide stdio — was
**essentially correct**, and the fault being a *write* of `0x0` is consistent with it.

What is new is the **root cause upstream of it**: the engine's `malloc` returns NULL because a
guest `mmap` failed, and the engine propagates NULL onward instead of checking it. The
documented proximate diagnosis and this root cause are not in conflict; they are different
layers of the same crash.

## The measurement that settles it

Instrument the `find_free`-failure branch (`syscalls.cpp:317`) to write, at failure: the
requested size, the **largest contiguous free run**, and total free pages, into
`zb-runtime-report.txt`. Then:

* `run << request` with large total free → **fragmentation confirmed**;
* `run >= request` → the host `mmap`/VMA path is failing instead, and `vm.max_map_count`
  becomes the question again (it is `Permission denied` to `adb shell` on this device, so it
  is currently UNKNOWN).

That instrumentation is a change to `syscalls.cpp`, so it needs a `libzbridge.so` rebuild —
which is now proven reproducible byte-for-byte, so it is available today.
