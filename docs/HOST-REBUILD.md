# HOST-REBUILD.md — rebuilding Track A (the ZettaBridge removal) from the repository

*2026-10-05. This records a rebuild, so it says plainly what was lost, what was written, and what
each claim rests on.*

## 1. Why there is a rebuild at all

Track A — DH2Work's own compatibility host — was developed in a private working tree
(`DH2Work-toolchain/host/…`) that was **never committed** to either public repository. The
documents it produced survived in this repository (`HOST-ARCHITECTURE.md`, `HOST-OWNHOST-PROGRESS.md`,
`HOST-MEMORY-SYSCALLS.md`, `HOST-ELF-LOADER.md`, `HOST-RUNTIME-LINKER-THREADS.md`,
`HOST-SYSCALLS-IMPL.md`, `HOST-RUN-HARNESS.md`, `HOST-P0-*.md`); the source did not. Nothing
on the machine this was rebuilt on is prior work: the only DH2 material that exists is what is in
[Noamcelermajer/DH2Work](https://github.com/Noamcelermajer/DH2Work) and
[Noamcelermajer/DH_sc](https://github.com/Noamcelermajer/DH_sc).

So this is a reconstruction from the design and the measurements those documents contain, written
fresh and verified from scratch. It is deliberately not a copy: it is a smaller host with the
same boundaries, built in the order the roadmap asks for.

## 2. What the rebuild contains

`host/` — the own host. `host/README.md` describes every component. In one line each:

| Component | What it is |
| --- | --- |
| `host/mem` (`guest_memory`) | One 4 GiB `PROT_NONE` reservation, per-page flags, checked access, fixed mappings |
| `host/loader` | ARM ELF32 `ET_EXEC`/`ET_DYN` loading, segment placement, prot narrowing, `R_ARM_RELATIVE` |
| `host/rt` (`initial_stack`, `cp15`) | The Linux argument block bionic parses out of bare SP; TPIDRURO/TPIDRURW for TLS |
| `host/p0` (`cpu`, `syscalls`) | Dynarmic A32 with our callbacks, precise faults, the ARM EABI syscall slice |
| `host/harness` (`dh2run`, `dh2selftest`) | Run a guest; check the host components |

Every design decision is stated in `host/README.md`. The two that matter most: **no fastmem and no
page table**, so a guest wild pointer is a reported guest fault rather than a host SIGSEGV; and
**`arch_version = v8` with precise memory aborts**, which is what makes our two Dynarmic patches
load-bearing rather than decorative.

## 3. The P0 gate, closed again on this machine

The gate is: **guest ARM32 code executes under our own host, on our own Dynarmic, with no
ZettaBridge code in the process.** Measured, not asserted:

```
$ dh2selftest
dh2selftest: 34 checks, 0 failures

$ dh2run --quiet guests/out/bionic world
hello arm32
argc=2 argv1=world
malloc=PASS
mmap=PASS
clock=PASS
pid=PASS
```

That last binary is an ordinary C program linked against **static bionic** with the Android NDK.
Nothing in it is game-specific: it must get through bionic's own `__libc_init` — reading the auxv
out of the initial stack image, installing the TLS thread pointer through CP15, setting up the
allocator — before `main` runs at all. It then exercises `printf`, `malloc` (1 MiB, poisoned and
checked), `mmap`/`munmap`, `clock_gettime` and `getpid`. It passes.

The guest suite as a whole, against the pinned upstream Dynarmic exactly as it is:

```
PASS hello            PASS hello_thumb     PASS args
PASS vfp              PASS exclusive       PASS memory
PASS bionic
FAIL armv8_t32 (exit 3, expected 0)
run-guests: 7 passed, 1 failed, 0 skipped
```

and against **our own patched build of the same pin**:

```
PASS hello            PASS hello_thumb     PASS args
PASS vfp              PASS exclusive       PASS memory
PASS armv8_t32        PASS bionic
run-guests: 8 passed, 0 failed, 0 skipped
```

`armv8_t32`'s numbers are not self-reported: `crc32b=0x2d02ef8d`, `crc32h=0xbe2612ff`,
`crc32w=0xdebb20e3` and `crc32cb=0xad7d5351`, `crc32cw=0xb798b438` were computed
independently in Python from the two reflected polynomials (0xEDB88320 and 0x82F63B78, no final
inversion) before the guest was ever run. It also asserts both halves of the exclusive contract:
a `stlex` after `clrex` must fail and must not write, and a `stlex` after `ldaex` must succeed
and must write.

`vfp` matters for its own reason: `sum=0x40c00000` is 6.0 and `product=0x410c0000` is 8.75, so
the guest really executed `vadd.f32`/`vmul.f32`/`vcmp.f32` — there is no soft-float shim in this
host, and the two operands are `volatile` so the compiler could not fold them away.

**The one failure is the point.** `armv8_t32` is built with `-march=armv8-a -mthumb` and executes
`ldab`, `ldah`, `ldaex`, `stlb`, `stlex`, `crc32b/h/w` and `crc32cb/cw`. Stock Dynarmic has no
T32 decoder rows for any of them, so the first one raises an undefined-instruction exception:

```
stop : guest exception 0 at pc 0x000101c0
run  : 34 instruction(s), 1 svc(s)
exit : stopped (3)
```

That is the exact behaviour `docs/DYNARMIC-INDEPENDENCE.md` predicted, reproduced here from our
own binary — and the 8/8 run above is the same suite on the same machine with nothing changed but
the Dynarmic build.

## 3b. The engine's own initialization now runs (the number the old tree reported)

`dh2boot` loads the real closure with our own linker and runs each module's `DT_INIT` and
`DT_INIT_ARRAY` under our own JIT. Against the original engine
(`36498eb8…`, the documented hash):

```
dh2boot: libDungeonHunter2.so
  linked       : 8 module(s), 59623 relocation(s) applied, 0 unresolved
  loader iface : shared_globals=0xefffc000
  thread       : static TLS 32 bytes, TP=0xff000020 [TP]=TP [TP+4]=TP errno=0
  libDungeonHunter2.so     539 initializer(s)
  ...          : 500 completed, 824314 instruction(s)
  progress     : 539 initializer(s) completed, 910463 instruction(s)
  initializers : 539 completed
```

**539 of 539 engine constructors complete.** The old tree's record is
`HOST-OWNHOST-PROGRESS.md`: "539 present, 539 entered, 539 completed, 824421 instructions" --
and 500 of ours complete at 824,314 instructions, which is the same run seen through the same
counter. Before this work the host could not load a shared object at all.

Reaching it needed four things that were each a wall in turn, and each is now a named,
testable piece:

| Need | Why the stop happened without it |
| --- | --- |
| The guest dynamic linker (`host/linker`) | 354 undefined functions per engine; nothing loaded |
| `__loader_shared_globals` and the 26-slot interface | libc reads its own globals through it; with the value zeroed the first constructor died reading `0x18` |
| The initial thread's control block at TP | bionic reads `errno` at `TP+0x29C` and its canary at `[TP-4]`; with TP zero it read `0x410` |
| `mremap` in the syscall layer | `__cxa_atexit` grows its pool with it, and without it every atexit registration reported a failure |

The engine's constructors issue 1,028 syscalls across 13 numbers (`mprotect` alone 988 times),
five `__loader_shared_globals` calls, and 910,463 guest instructions.

The next stop is honest and is not a wall: **libc.so's own first initializer**
(`libc.so+0x49035`, a read at `0x8`). The old tree never ran the dependency init arrays at all
-- its successful path was the engine's 539 constructors followed by a direct call to
`libc.so`'s `__libc_init` -- so this is territory the rebuild has reached and the old record
does not cover.

## 4. The custom Dynarmic

Our own patch set lives in `patches/dynarmic/`, applied by `host/scripts/fetch-dynarmic.sh` to the
pinned upstream checkout recorded in `third_party/dynarmic.pin`
(`86458a0bd369d63ba4c2ef812cacbb6c9080c065`). It carries exactly the two changes the independence
analysis found necessary and nothing else:

1. the `A32AddressSpace::GenerateIR` guard that skips `A32GetSetElimination` when
   `check_halt_on_memory_access` is set (the other three backends already have it), and
2. the T32 ARMv8 load-acquire/store-release and `CRC32`/`CRC32C` family.

The ASIMD narrowing family is deliberately **not** included: no guest binary in this title contains
an architecturally legal instance.

`host/scripts/fetch-dynarmic.sh` was run against a **pristine clone** of the pin: it checked out
`86458a0b`, applied the patch with `git apply`, and the host built and passed 8/8 against it. See
`patches/dynarmic/README.md` for the change and `patches/dynarmic/tests/` for the standalone
instruction test, which reports `T32-SUMMARY mode=patched pass=18 fail=0` and, on a pristine tree,
an 18/18 A/B proving each of those instructions is otherwise an undefined instruction.

## 5. What is honestly not rebuilt

Nothing below is claimed to work, and each is a named next step rather than a surprise:

* **The guest dynamic linker / PLT resolution.** `ET_DYN` images are mapped and their
  `R_ARM_RELATIVE` relocations applied, but an undefined symbol is not resolved.
  `load_elf32` *refuses* an image that needs a relocation type it cannot perform, instead of
  leaving it silently broken — the failure mode the older tree lost weeks to.
* **Threads, signals, futex waiting.** One guest thread. `futex` wait reports the value-changed
  case rather than blocking; a queued signal is acknowledged, not delivered.
* **The filesystem.** `open`/`stat`/`readlink` return `-ENOENT`.
* **JNI, GL/EGL marshalling, the Android lifecycle, ART.** Not started.
* **The arm64 product target.** The host in this tree was built and run on x86_64 Linux
  (Dynarmic's x64 backend) off-device. The *patched Dynarmic*'s arm64 backend has since been
  cross-compiled for aarch64 successfully, including the translation unit that carries the
  halt guard; the host itself has not been cross-built, and no arm64 result has been executed.
* **fastmem and `PreCodeReadHook`.** Deliberately not started; the roadmap says measure the first
  before building it.
* **The two documented debts the old tree still carried** — `region_containing`'s unexplained
  binary-search failure and `libc_globals +0x600` — do not arise here in the same shape, because
  this host uses a page-flag lookup rather than a sorted region list. That is not a fix; it is a
  different implementation that cannot have that failure mode.

## 6. Reproducing

```sh
host/scripts/fetch-dynarmic.sh                 # pinned upstream + our patches
DH2_DYNARMIC_DIR=$PWD/third_party/dynarmic host/scripts/build.sh
DH2_NDK=$HOME/android-ndk-r29 host/guests/build.sh
host/scripts/run-guests.sh <build>/dh2run host/guests/out
```

**Environment used:** WSL2 Ubuntu 22.04, clang 14.0.0, cmake 3.22.1, ninja 1.10.1, 12 cores;
Android NDK r29 for the guests; Dynarmic with `DYNARMIC_USE_BUNDLED_EXTERNALS=ON` so the build is
offline. No phone and no emulator were used.
