# The long-run crash is guest memory exhaustion during a map load, not a null `FILE*`

Reproduced twice on device. The second run is fully root-caused, and it **overrides the
blocker recorded by the project**.

## 1. The crash, with the cause printed immediately before it

Galaxy Z Fold7 **SM-F966B**, build `1.0-work3-profile`. Guest log
(`dh2-logcat.txt` on device, captured 2026-10-03 17:14:32):

```text
17:14:32.300  _DEBUG_OUT: **************************************m_loadStep 10
17:14:32.320  W libc    : malloc(1429956) failed: returning null pointer
17:14:32.323  zbridge  : guest SIGSEGV: write of 0x00000000, pc 0xfdb185d8
   r0  00000020  r1  ea306ac0  r2  0000001c  r3  00000000
   r4  fa393a14  r5  0008840c
   r13 fb0764d0  r14 fce5e980  r15 fdb185d8
   cpsr 20030010  tid 10953  guest tid 10965
   pc in …/zb/sysroot/system/lib/libc.so offset 0x675d8
   lr in …/plugins/…/lib/libDungeonHunter2.so offset 0x69a980
```

In order, on three consecutive lines:

1. the engine is at **`m_loadStep 10`** — a map/scene load step, exactly the documented
   crash context ("while a new map loads");
2. guest libc reports **`malloc(1429956)` failed and returned NULL** — 1.4 MB;
3. the engine **writes to `0x00000000`** and the guest dies.

This is an **unchecked allocation failure**: the engine does not test the `malloc` result and
stores through the null pointer. The fault is a `write` of `0x0`, which is why it is a
different signature from every other crash seen on this title.

## 2. This overrides the recorded blocker

Project documentation attributes the ~5-minute crash to:

> "the engine calls a wide stdio print with a null `FILE*` (fault in `libc.so` at `0x675a0`,
> `lr 0x64c21` inside the `vfwprintf` wrapper). Both script files for the map had already
> opened successfully, so this is during script or map initialisation, not a missing asset."

The recorded libc offset is **`0x675a0`**; this crash is at libc **`0x675d8`** — 56 bytes
away, i.e. the *same region of libc*, so it is very likely the same defect that was
misdiagnosed from the offset alone. The evidence above is explicit and does not need
inference: guest libc prints the failed `malloc`, and the fault is a **write to NULL**, not
a call into a wide-stdio print with a bad `FILE*`. No `FILE*` is involved, and there is no
`vfwprintf` frame — the return address is in `libDungeonHunter2.so`, not in a stdio wrapper.

The earlier run in this session (388 s, `read of 0x0001c9c8`, engine
`createMeshCopy`→`IBuffer::copy`→`memcpy`, libc `0x67304`) is most likely the same
underlying condition seen through a different call site: those libc offsets `0x67304`,
`0x675a0` and `0x675d8` all sit in the same allocator region. **Both long runs died after
~6.5–7.1 minutes of play**, at a point where the engine was allocating for a scene.

## 3. Why the allocation fails: guest memory pressure

Measured on this device, the guest process's resident set:

| Observation | RSS |
| --- | ---: |
| after the orientation fix, during play | ~517 MB |
| long run, mid-game | ~688 MB |
| just before the fatal load | ~672–715 MB |

The engine is a 2011 title with a 635 MB asset cache and it grows to several hundred
megabytes of resident guest memory. Something then cannot satisfy 1.4 MB. Two candidate
mechanisms, not yet separated:

* **A hard ceiling.** The app process hits its Android memory limit or the wrapper's guest
  heap/mmap budget, and `malloc` correctly returns NULL.
* **Guest address-space fragmentation.** The guest is 32-bit; the wrapper maps guest memory
  into a 64-bit process, and the earlier emulator failure on this project was the same
  shape — `berberis::MmapImplOrDie` / `CodePool::Add`, i.e. executable-address-space
  exhaustion. If the guest's 32-bit arena is fragmented, a 1.4 MB request can fail while
  plenty of total memory is free.

This is a **memory** problem, and it is independent of the CPU-translation performance
work — it will end every long session regardless of how fast the CPU becomes.

## 4. Why this matters more than the frame rate

It converts a "playable demo" into a session that dies after about seven minutes, every
time. Two consecutive long runs both died in a map load. Fixing this is worth more than any
Tier-2 port, because a 60 FPS build that still dies at seven minutes is not usable.

## 5. Next measurements and fix directions

Measure first, in this order:

1. **Sample the guest's memory across a whole run** (`/proc/<pid>/status` `VmSize`/`VmPeak`
   and `VmRSS`, plus `ps` RSS) to see whether the fatal point is a ceiling or a saw-tooth
   from fragmentation, and to identify the growth curve.
2. **Check the app's memory ceiling**: `dumpsys meminfo local.dh2.fold7:guest`, the
   `largeHeap` manifest flag, and the device's per-app limit. If the ceiling is the
   manifest, raising `largeHeap` or the wrapper's guest arena budget may be a one-line fix.
3. **Ask whether the guest can be told to allocate less.** The engine pulls a 635 MB cache;
   if the fatal step is a texture/mesh upload whose size is derived from a setting, the
   settings file is already ours to write (see the Test 11 language work).

Candidate fixes, cheapest first:

* **Raise the ceiling** if the failure is a limit — manifest `largeHeap`, or the wrapper's
  guest memory configuration (`compatibility/16k-port/zettabridge-guest-memory-16k-poc.patch`
  is existing prior art for guest-memory changes in this project).
* **Make the allocation failure survivable.** The engine writes through a NULL returned by
  `malloc`. A 3-instruction guard is not possible here in the same way as the Test 5 path
  patch — this is a null *store* inside the engine at `libDungeonHunter2.so +0x69a980`, so
  it would need an engine byte patch in the same style as `engine_path_fix.S`, and it must
  be justified as "fail safe instead of dying" rather than as a fix for the memory shortage.
* **Reduce memory pressure** — the real fix, but it needs the growth curve from step 1.

## 6. Evidence in this directory

| File | Contents |
| --- | --- |
| `events-crash2.txt` | guest event trace; last render milestone `frames=10080 elapsedMs=427974` |
| `app-logcat-crash2.txt` | 602 KB of guest logcat including the `malloc` failure and the register dump |
| `longrun-monitor.txt` | the death detector's record: died at 17:14:41 after 462 s of monitoring |

No `DH2-diagnostics` ZIP was exported for this run (the export is a manual step in the
launcher), so there is no `zb-runtime-report.txt` for it — the register dump above came from
the guest logcat, which the app maintains independently.
