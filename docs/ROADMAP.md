# Roadmap and current state

*2026-10-04. This is the **current** state; `STATUS.md` remains the historical test-by-test
record and is not superseded. Everything here is measured or explicitly marked as not.*

---

# Part 1 — Where the project stands

## What this is

Dungeon Hunter 2 HD v1.0.2 ships only 32-bit **armeabi-v7a** libraries and runs on an
arm64-only Galaxy Z Fold7. Two tracks, deliberately separated in `TWO_TRACKS` (see
`TWO-TRACKS.md`):

- **Track A — the real goal: our own compatibility host**, built on **Dynarmic**
  (AArch32→AArch64 JIT, **0BSD**). End state: no third-party layer in the build.
- **Track B — improve the existing host** (a tailored fork). A personal-need track that
  **must not become the destination**.

Rules: `PORTING-POLICY.md` (what may be copied — ZettaBridge source may be adapted, Dynarmic
may be forked), `TEST-ENVIRONMENT-POLICY.md` (subagents test off-device; the Lead owns the
phone), `TWO-TRACKS.md` (device-use and version-code rules).

## Track A — the engine's own initialization now runs to completion

```
relocations          59,623 / 59,623 applied, 0 deferred, 2 weak-undefined
DT_INIT_ARRAY        539 present, 539 entered, 539 COMPLETED, 824,421 instructions
__libc_init          reaches bionic's application entry point, r0 = 0
engine allocator     27 real syscalls: mmap2 x8, prctl x8, fcntl64 x4, munmap x2,
                     getrandom x2, clock_gettime, sched_getaffinity, futex
internal checks      25, 0 failures
```

This was `8 of 539` constructors before the last two workstreams.

| Component | Evidence |
| --- | --- |
| Guest memory + memory syscalls | memtest 55/55 at 4 KiB **and** 16 KiB; `memtest_real` 28/28 |
| ELF32 loader | 32 cases, **102,378 checks** |
| Runtime linker / threads / TLS | **395 checks**; 4 real guest threads joined |
| `__loader_*` interface | 13 real, 12 stub, 1 halt — the `udf` wall is gone |
| Syscall layer | works; **runtime inventory is 2 syscalls, not 230** |
| JNI bridge | 233 slots, 36 natives; passes x86_64 **and** armv7a |
| `dlopen` bridge | 423 checks; proven end to end — returns the load bias at `handle[0x8c]` |
| Soft-float/libm libcall shim | 69 entries, 76 GOT slots interposed |
| Off-device executor | `dh2run` runs guest ARM32 **incl. VFP**, 44/44 |
| Dynarmic patch `0001-A` | arm64 backend builds — **1,281 `Arm64::` symbols, 0 `X64`** |

**Nothing renders, and Track A runs on zero devices today.**

## Track B — two wins banked, the rest closed honestly

| Change | Result |
| --- | --- |
| **VFP soft-float replacement** (guest `libc.so`, 20 bodies, 742 bytes) | **+11.3%** in-game FPS (28.17 → 31.36, time-aggregated) |
| **Shrunk host** | source **43,807 → 30,154 lines**; host calls **579 → 92**; GL entries **392 → 92** |

**Closed with evidence — do not re-open without new data:**

| Avenue | Verdict |
| --- | --- |
| `memcmp`/`strlen`/`__memcpy_forward` (10.56% of execution) | **already optimal** |
| GL state shadowing | 1.57% redundancy |
| GL call batching | GL driver is **0.16%** of frame time |
| 120 Hz display mode | **vendor-capped at 60 Hz** |
| CPU affinity / priority | **neutral** |
| `skin` retarget | native body built, **no bit-exactness claim** |
| Xiaomi 12-minute load | **not** shaders, **not** the SoC — an ANR/pause cycle |

## The measured profile

```
frame CPU:   82.7% JIT'd guest code / 15.1% host / 0.16% GL driver
guest code:  23.8% engine / 76.2% sysroot
             59.5% of ALL executed guest instructions are soft-float and libm bodies
in-game FPS: ~29 avg / ~37 median, against a hard 60 Hz ceiling
```

---

# Part 2 — Roadmap

## Track A, in order

1. **JIT region-table staleness — next.** The JIT's region table is a **snapshot**, so a page
   mapped by `mmap2` is visible to the syscall layer's reads but **not to the JIT callbacks**.
   This is exactly the class that produces a fault far from its cause.
2. **GL marshalling — 92 entry points.** Nothing renders without it. The shrink work already
   proved only 92 of 392 are reachable and that the generator's shape is mechanical.
3. **Android target + launcher integration.** Everything so far runs as a Linux-host binary;
   the product target is arm64 Android.
4. **The assembly** — wire the components into one process that boots the game.
5. **`PreCodeReadHook`** for host-native hot functions — 2 declarations + 2 definitions, **no
   Dynarmic patch required**. This is the only route that attacks the 82.7%.

**Gate definition for each:** demonstrable, quoted, on arm64. "Relocations apply, startup
executes N instructions, stops at `<pc>` with `<instruction>`" is the reporting shape that has
worked.

## Track B, remaining

Small by measurement. The only unexplored lever of consequence is **JIT memory-access cost**:
every guest load/store goes through checked callbacks, charged per access inside translated
code. Nobody has separated that out of the 82.7%. Dynarmic's fastmem/unsafe options bypass it
at the cost of turning a guest fault into a host crash. **Measure it before building it.**

## Known debts, all named

| Debt | Consequence |
| --- | --- |
| `dl_unwind_find_exidx` returns 0 | guest unwinding breaks the first time the engine **throws**, presenting as an unrelated crash |
| `libc_globals +0x600` zeroed, nothing fills it | unread so far; will bite when something reads it |
| `host/loader`'s `allocate_block` memsets a `PROT_NONE` block with no `mprotect` | faults on first use; worked around, not fixed |
| `region_containing` binary-search failure | **reproducible and unreduced** — reproduce before restoring a binary search |
| Dynarmic loop-back branch lands at `0x72FFFFD4` not `0x73000000` | unexplained; would matter well beyond the `skin` experiment if it is a translator defect |
| 16 KiB page support | the shipped launcher **hard-refuses** anything but 4096; `host/mem` already passes at both sizes |
| VFP magnitude unpinned | +11.3% is robust; the +57% median was a windowing artifact and is retracted; no scene-locked A/B has been run |
| `FZ`/`DN` denormal audit | cleared for the current bundle; **gate any new sysroot** by re-running the two census scripts |

## Environment

- **Off-device** (subagents): `zbrun` host build, `dh2run` guest executor (ARM32 **including
  VFP**), `qemu-arm`, the Unicorn oracle, component test suites. arm64 results use a **static
  aarch64 Android binary under `qemu-aarch64`** via binfmt — the product target, unlike x86_64.
- **Phone** (Lead only): in-game FPS, display behaviour, ART/JNI, anything device-specific.
- **Unicorn cannot execute a single ARM VFP instruction** — nothing float-related may be built
  on it.
- An arm64 Android emulator is installed (AVD `dh2`, `pixel_fold`) but **has never booted**.

## The rule that has mattered most

**Test any geometry or lifecycle change against the device that already works first.** Twice
this session a change made blind broke a working phone: an `auto` fit mode regressed the Fold7
with letterbox bars it had never had, and an unconditional launcher `finish()` skipped the
cache-import menu. Both were caught by the user, not by our tests. The Fold7 is the
regression guard; the Xiaomi is the bug.
