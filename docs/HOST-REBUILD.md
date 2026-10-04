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

### Every initializer in the closure now completes

```
libc.so                  5 initializer(s)
libc++.so                1 initializer(s)
libDungeonHunter2.so     539 initializer(s)
initializers : 545 completed
instructions : 940,913
syscalls     : 1,072 across 17 numbers        (exit 0, no stop)
```

and then bionic's launch function runs to the application entry point:

```
__libc_init : calling 0xef5420fd with raw_args=0xfefffe30 slingshot=0xef3e7000
slingshot   : reached at pc 0xef3e7004 argc=0 argv=0xfefffe38 envp=0xfefffe3c
```

That is the state `HOST-OWNHOST-PROGRESS.md` records as its own last stop --
"reached the application entry point bionic called (slingshot) at pc=0xFDE05002: r0=0x00000000
r1=0xFEFFFF08 r2=0xFEFFFF0C" -- reproduced here: same zero `argc`, same argv/envp shape, on our
own host.

Three defects stood between the 539 and the slingshot, and each was measured:

1. **`tls[TLS_SLOT_THREAD_ID]` must point at a separate `pthread_internal_t`.** It pointed at
   the TLS block, so bionic's main-thread registration wrote the thread-list pointers over
   `tls[0]` and `tls[1]`; `__get_thread()` then read zero and the next dereference faulted at
   `0x8`. Found with a 64-bit write watch, after a mapping watch had ruled out a `MAP_FIXED`
   remap -- which zeroes a page without any write callback firing.
2. **The documented `libc_shared_globals` offset table does not match this sysroot's `libc.so`.**
   The host wrote an mmap threshold of `0x20000` at `+0x520`; `getenv` reads the environment
   pointer from that slot, found `0x20000` and faulted dereferencing it. Offsets are now only
   written where this binary has been shown to use them, and the rest of the block stays zero.
   `HOST-LINKER-REFERENCE.md` flagged that layout as UNVERIFIED and was right to.
3. **`[TP-4]` must hold the thread pointer.** `__libc_preinit_impl` ends with
   `mrc p15,0,r0,c13,c0,3` / `ldr r0,[r0,#-4]` / `strb r5,[r0,#0xb49]`; with that word left
   zero the store landed at `0xb49`.

What the closure asks for and does not get is named, not implied:

```
paths tried : faccessat(/proc/self/exe), fstatat64(/dev/__properties__),
              openat(/dev/__properties__)    -- all -ENOENT; there is no filesystem yet
```

### The JNI surface is reachable

The engine is a Java library's native half: it exports no `main`, but it does export
`JNI_OnLoad` at `0x53224c` (32 bytes) and 28,085 other functions, including 14
`Java_com_gameloft_android_GAND_GloftD2SS_DungeonHunter2_*` natives. `host/jni` presents the two
structures JNI is built on -- a `JavaVM` whose first member points at the
`JNIInvokeInterface` table (8 slots) and a `JNIEnv` whose first member points at the
`JNINativeInterface` table (233 slots) -- both full of ARM stubs served from the SVC callback,
exactly like the loader interface.

```
JNI          : JavaVM=0xef3e7000 JNIEnv=0xef3e7100
JNI_OnLoad   : calling 0xefaf824c(vm=0xef3e7000, reserved=0)
JNI_OnLoad   : returned 0x00010004          (JNI_VERSION_1_4)
JNI          : 0 native-interface call(s), 0 invoke-interface call(s)
```

It returns `JNI_VERSION_1_4` and touches the table **zero times**, which is itself the finding:
this engine does not register its natives, so ART was resolving the 14
`Java_com_gameloft_...` symbols by name. Driving the game therefore means calling those natives
directly, with real Java-side objects -- the class/method registry that does not exist yet.

### The engine's drive sequence, and the next wall

The recovered Java (in the project's source-recovery package) gives the order the DEX drives the
engine, and it is not `JNI_OnLoad`:

```java
// DungeonHunter2 (the Activity)
public static native void nativeInit(int i);
public static native void nativeSetPhone(int w, int h);
public static native void nativeResume(int i);
public static native int  nativegetState(int i);
// GameRenderer (the GLSurfaceView.Renderer)
public GameRenderer(Context c) { f26a = c; nativeGameRenderer(); nativeConfig(); }
public static native void nativeInit(int i);
public static native void nativeResize(int i, int i2);
public static native void nativeRender();          // once per frame
```

`nativeInit` is the first call that is static and takes one int, so it needs nothing but a
`jclass`. Its prologue is ARM code that reaches JNI through an indirection:

```
5325d4: ldr  r5, [got]        ; r5 = engine+0x99c52c, a slot nativeInit reads
5325e0: ldr  r3, [r5]         ; r3 = the JNIEnv the engine expects to be cached here
5325ec: mov  r0, r3
5325f0: ldr  r3, [r3]         ; r3 = env->functions
5325f8: ldr  pc, [r3, #0x54]  ; call slot 21
```

**This is where the rebuild currently stops.** `JNI_OnLoad` runs and returns `JNI_VERSION_1_4`
after calling `VoxSetJavaVMC(vm)` and `NVThreadInit(vm)`, but neither of those makes a single
JNI call, so nothing caches an `env` and the slot `nativeInit` reads is not a valid pointer to
one. Seeding that slot with a host-built holder does not fix it, which means the slot is not the
cached-env global the disassembly first suggests.

### nativeInit runs, and the JNI surface it needs is now enumerated

The indirection is a global reached through a GOT slot, and the access trace named the exact
chain rather than the disassembly guess:

```
read 4 at 0xeff5f52c  ok        ; GOT slot engine+0x99952c
read 4 at 0xeffbc518  ok        ; the cached JNIEnv global -- zero
read 4 at 0x00000000  REFUSED
```

An earlier attempt seeded `engine+0x99c52c` and did nothing, because that word is initialised
`.data` (it holds the constant `0x22422224`) and no relocation targets it. With the global the
trace actually names seeded, `nativeInit` **runs to completion**:

```
nativeInit : returned (r0=0x00000000) after 326 instruction(s)
JNI        : 26 native-interface call(s)
     [ 21] NewGlobalRef      calls=1
     [113] GetStaticMethodID calls=25
```

Those 25 lookups are the JNI surface, in call order, and `host/jni` records the names and
signatures:

| | |
| --- | --- |
| platform | `sendAppToBackground ()V`, `Exit ()V`, `openBrowser (Ljava/lang/String;)V`, `Get_PhoneLanguage ()I`, `Get_PhoneManufacturer ()I`, `Get_PhoneModel ()I`, `isWifiAlive ()I`, `isSupportMM ()I` |
| rendering | `OpenGLive (I)V`, `OpenIGP (I)V` |
| store/DRM | `unlockDemo ()I`, `lockDemo ()V`, `DisableLaunchGame ()I`, `IncreaseLaunchTimes ()V`, `NotifyTrophy (I)V` |
| Gameloft Live | `VZIsInProgress ()I`, `VZIsErrorOcurred ()I`, `VZRequestLogin ()V`, `VZRequestPurchaseGame ()V`, `VZGetGamePrice ()[B`, `VZGetGameName ()[B`, `VZGetLastServerMsg ()[B`, `VZInitMobileNetwork ()V`, `VZIsMobileNetworkReady ()I`, `VZRestoreNetworkState ()V` |

That is the whole Java side the engine needs, and most of it is answerable without a network or a
store: "not in progress", "no error", "wifi is up", "manufacturer: Google", and so on. The next
gate is to make those IDs real and implement the `CallStatic<Type>Method` slots so the engine can
call them.

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

---

## 7. GL marshalling opens, and a frame completes

The generated GL/EGL stubs used to be `mov r0,#0; bx lr`, and that is what stopped the first
frame: `glitch::video::CCommonGLDriver<...>::genericDriverInit` calls `glGetString(GL_VERSION)`
and immediately scans the result.

```
5b3b18: movw r0, #0x1f02      ; GL_VERSION
5b3b28: bl   glGetString
5b3b38: ldrsb r3, [r0]        ; scan the returned string  -> r0 was null
```

The stubs now carry their symbol name -- each is `svc #(0x400 + index) ; bx lr` -- and
`host/gl` answers by name, so all **92** imported entry points are addressable. The difference is
not marginal:

| | before | after |
| --- | ---: | ---: |
| `GameRenderer.nativeInit` | 45,388 instructions, then a null dereference | **4,586,736 instructions** |
| `GameRenderer.nativeRender` | fault after 25 instructions | **returns, 3,793 instructions** |

`glGetString` returns real strings (vendor, renderer, GL version, GLSL version, extension list),
and the engine is seen doing the ordinary first-frame setup: 46 calls across 27 entry points --
`glEnable`/`glDisable`, blend state, depth state, culling, `glClear*`, `glViewport`,
`glScissor`, `glGenBuffers`/`glBindBuffer`, `glGenTextures`, `glPixelStorei`,
`glGetIntegerv`.

The `CallStatic<Type>Method` slots also dispatch by name now, answering the 25 Java methods the
engine asks for, so the Java side no longer returns a null id for every lookup.

The next stop was a read at `0x24` inside `GameRenderer.nativeInit` (`engine+0x61b0b8`): the
engine was deep in asset loading and had nowhere to load from.

## 8. A filesystem layer, and the honest blocker

`host/vfs` plus real `openat`/`open`/`read`/`pread64`/`close`/`lseek`/`fstat64`/`stat64`/
`fstatat64`/`faccessat` in the syscall layer give the engine a read-only view of a directory
(`--root`). Verified by letting the engine itself drive it:

```
filesystem   : 1 root(s)
open(/data/tweaker/player_light.tweaker_xml) -> fd 3 (31 bytes)
open(DebugSwitches.savegame)                 -> fd 8 (8 bytes)
open(shaders.pak)                            -> fd 14 (4096 bytes)
open(#/system/fonts/droidsans.ttf)           -> fd 15 (757076 bytes)
open(#/system/fonts/._droidsans.ttf)         -> -ENOENT
open(#/system/fonts/.AppleDouble/droidsans.ttf) -> -ENOENT
open(#/system/fonts/droidsans.ttf/..namedfork/rsrc) -> -ENOENT
```

Those last three are the engine's own font loader probing macOS resource-fork paths, which is a
useful sign that it is running real code rather than a stub. `GameRenderer.nativeInit` now runs
**5,065,945 instructions** (up from 4,586,736) and `nativeRender` still completes a frame.

**The blocker is an input this machine does not have.** The files it asks for by name are the
game's own content -- `shaders.pak`, `gameswf_effects.bdae`, and the rest of the cache. The
project's roadmap already calls the cache an owner-supplied private input, and it is not present
here (the one downloads directory that might have held it is empty). With placeholder bytes in
those names the engine reads them happily and then faults on a bogus pointer, because placeholder
bytes are not a shader package.

So the honest position: the host now has everything it needs *structurally* to let the engine load
its content -- loader, linker, TLS, loader interface, JNI, GL, filesystem -- and stops where the
content itself would have to be real. Booting to gameplay off-device needs the cache supplied;
everything up to that line is measured and committed.
