# host/ — DH2Work's own compatibility host (Track A)

This is the ZettaBridge replacement: the code that loads the original 32-bit ARM
`armeabi-v7a` Dungeon Hunter 2 libraries into a 4 GiB guest address space and executes them
under **our own Dynarmic build**, with no ZettaBridge source and no `libzbridge.so` in the
process.

It is a rebuild, not a fork. The Track A tree that produced the numbers in
`HOST-OWNHOST-PROGRESS.md` was never committed to this repository and is gone; this directory
is the reconstruction, written from the design and the measurements those documents record, and
verified from scratch on this machine. See `docs/HOST-REBUILD.md` for what was lost, what came
back, and what is honestly still missing.

## Layout

| Path | Contents |
| --- | --- |
| `include/dh2/guest_memory.hpp`, `src/guest_memory.cpp` | The 4 GiB guest address space: one `PROT_NONE` reservation, per-page flags, checked access |
| `include/dh2/loader.hpp`, `src/loader.cpp` | ARM ELF32 loading: `ET_EXEC`/`ET_DYN`, `PT_LOAD` placement, prot narrowing, `R_ARM_RELATIVE` |
| `include/dh2/initial_stack.hpp`, `src/initial_stack.cpp` | The Linux process-start stack image (argc/argv/envp/auxv) bionic parses out of bare SP |
| `include/dh2/cpu.hpp`, `src/cpu.cpp` | The guest CPU: Dynarmic A32 JIT, memory callbacks, precise faults, stop reporting |
| `include/dh2/cp15.hpp`, `src/cp15.cpp` | CP15 user-mode coprocessor: TPIDRURO/TPIDRURW (bionic TLS) and the old barrier encodings |
| `include/dh2/syscalls.hpp`, `src/syscalls.cpp` | The ARM EABI syscall slice a static ARM32 Android binary reaches, plus a census |
| `app/dh2run.cpp` | The guest runner: load, set up, run, report |
| `app/dh2selftest.cpp` | Host-side checks; builds a synthetic ARM ELF and executes it |
| `guests/` | The ARM32 guest test programs and their recorded expectations |
| `scripts/` | Fetch Dynarmic, build the host, run the guest suite |

## Build and run

```sh
# 1. Get the pinned Dynarmic and apply our own patches (0BSD; separate checkout).
host/scripts/fetch-dynarmic.sh                    # -> third_party/dynarmic

# 2. Build the host.
DH2_DYNARMIC_DIR=$PWD/third_party/dynarmic host/scripts/build.sh

# 3. Build the ARM32 guests with the Android NDK (needs DH2_NDK).
host/guests/build.sh

# 4. Run the guest suite against its recorded expectations.
host/scripts/run-guests.sh <build>/dh2run host/guests/out

# or a single guest:
<build>/dh2run host/guests/out/bionic world
```

`dh2selftest` runs under `ctest` from the build directory and needs no guest binaries.

## Design decisions worth stating

* **No fastmem, no page table.** Every guest memory access goes through our callbacks. It is the
  slower path, and it is the one where a guest wild pointer is a *reported guest fault* — with
  address, direction and PC — instead of a host SIGSEGV. The roadmap already lists the fastmem
  measurement as the next performance question; correctness first.
* **Precise memory aborts.** `check_halt_on_memory_access` is on, which is also what makes the
  first of our two Dynarmic patches necessary (see `docs/DYNARMIC-INDEPENDENCE.md`).
* **`arch_version = v8`.** The guest sysroot is a modern AOSP ARM32 bionic whose `libc.so` and
  `linker` really do contain ARMv8 T32 instructions; that is what the second patch adds.
* **The zero page is never mapped.** A guest null dereference faults.
* **Guest executable pages are never host-writable.** The JIT reads guest instructions through
  the callbacks, so the host stays W^X-clean.

## What this build does not have yet

Named so the next workstream does not have to rediscover them:

* **No guest dynamic linker.** `ET_DYN` images are mapped and their `R_ARM_RELATIVE`
  relocations applied, but undefined symbols are not resolved. `load_elf32` refuses an image
  that needs a relocation type it cannot perform, rather than leaving it silently unresolved.
* **No threads, signals, futex waiting, or JNI.** One guest thread; `futex` wait reports the
  value-changed case instead of blocking.
* **No filesystem layer.** `open`/`stat`/`readlink` return `-ENOENT`.
* **No GL/EGL marshalling**, no Android lifecycle, no ART.
* **No fastmem, no `PreCodeReadHook`.** Both are roadmap items, deliberately not started here.

## One emulation choice worth knowing

`getpid`/`gettid` return a **stable pid clamped to the Android platform's range**, not the host's
raw pid. bionic's 32-bit `pthread_mutex_t` stores the owner in a 16-bit field and refuses a
process whose pid exceeds 65535; Android's own `pid_max` is 32768, so on the real target this
never arises, but an off-device Linux host can easily have a much larger pid counter. The guest
is therefore given `host_pid` when it already fits and a small derived value otherwise. This was
found by the `bionic` guest test failing with
`32-bit pthread_mutex_t only supports pids <= 65535` after the host's pid counter crossed the
boundary -- a test that passed or failed depending on the host's uptime, which is exactly the
kind of flake the suite is meant to catch.

## Licence

Dynarmic is 0BSD. Our patches to it are ours. The guest test programs in `guests/` are ours.
Nothing here derives from ZettaBridge.
