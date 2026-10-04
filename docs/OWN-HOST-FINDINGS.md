# The own host: findings

This document is the record of the own host (`host/`) -- the track that runs the original 32-bit
ARM engine **without ZettaBridge and without `libzbridge.so`**, on a self-authored build of
Dynarmic. It states what was measured, what each wall turned out to be, and what is still not
established.

Everything below is off-device. No phone and no emulator were used for this track.

---

## 1. The result

The real `libDungeonHunter2.so` -- SHA-256
`36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`, taken from the shipping
`Dungeon-Hunter-2-HD-v1-0-2.apk` -- is loaded and run by `host/` with the game's own cache:

```
initializers : 545 completed
JNI          : cached JNIEnv seeded at 0xeffbc518 = 0xef3e4100
nativeGameRenderer : returned after 2 instruction(s)
nativeConfig       : returned after 2 instruction(s)
nativeInit         : returned r0=0x00000001 after 13,103,112 instruction(s)
nativeResize       : returned after 5 instruction(s)
nativeRender       : returned r0=0x00000001 after 4,310 instruction(s)
nativeRender       : returned r0=0x00000001 after 20,297,122 instruction(s)
instructions : 34,345,838
JNI          : 28 native-interface calls, 1 method called back into Java
GL           : 104 calls into 43 imported entry points
```

The engine's own initialization completes, it builds its GL pipeline, and it sustains a frame
loop. `--frames 30` drives 30 frames with no fault:

```
nativeInit : returned r0=0x00000001 after 13,102,907 instruction(s)
frames     : 29 frame(s) driven, 222,634,462 instruction(s), slowest 58,540,350
instructions : 236,683,038
GL         : 160 calls into 43 imported entry points
```

Frame cost climbs from 4,310 instructions on the first to 58.5M at its heaviest, which is the
engine doing real work rather than idling in a stub.

> One correction in the interest of the record: `--frames` was first committed as an option that
> was parsed and never used, so a run described as 60 frames was in fact two. It is implemented
> now, and the numbers above come from it.

| Gate | Result |
| --- | --- |
| Load the engine | 8 modules, 59,623/59,623 relocations applied (2,645 packed), 0 unresolved |
| Initializers | 539 `DT_INIT_ARRAY` entries; 545 initializers across the closure |
| bionic reaches the app | `__libc_init` -> slingshot at `pc=0xef3e2004` |
| JNI surface | `JNI_OnLoad` -> `0x00010004`; 25 Java methods requested and answered |
| GL marshalling | 92 imported entry points answerable by name; 43 used; 104 calls |
| arm64 Android | builds with NDK r26d against bionic and **runs under qemu**: 34/34 selftest, 8/8 guest suite |
| No ZettaBridge code | `git grep -il zettabridge -- host` is empty |

---

## 2. What the host is

`host/` is a from-scratch compatibility host. It is not a fork of the bridge: the only third-party
component is Dynarmic, pinned and patched, and the patch is ours.

```
host/include/dh2/   guest_memory  loader  initial_stack  cp15  cpu  syscalls
                    linker  loader_if  jni  gl  vfs  random
host/src/           ... implementations
host/app/           dh2run  dh2selftest  dh2link  dh2boot
host/scripts/       fetch-dynarmic  build  build-android  run-guests
                    run-guests-aarch64  reproduce
host/guests/        ARM32 guests and expected output
host/cmake/         the Android toolchain used with the system clang
patches/dynarmic/   the T32 ARMv8 + arm64 halt-guard patch
```

The seam between guest and host is the `svc` instruction, with disjoint immediate ranges:

| Range | Meaning |
| --- | --- |
| `#0x100+i` | `__loader_*` interface (26 slots, bionic's linker ABI) |
| `#0x200+i` | `JNINativeInterface` (233 slots) |
| `#0x300+i` | `JNIInvokeInterface` (8 slots) |
| `#0x400+i` | one per unresolved imported symbol (92 GL/EGL entry points) |
| `#0x7f` | return trampoline |
| `#0x80` | slingshot probe |

Opening the shared libraries the engine needs (`libGLESv2.so`, `libGLESv1_CM.so`,
`libnativeinterface.so`) is **not** done: the imports resolve to generated stubs instead, which is
what makes the 92-entry GL table possible. See section 6 for what that costs.

---

## 3. The walls, and what each one actually was

This is the part worth reading. Every entry below was found by measurement -- an access trace, a
write watch, or a disassembly -- not by guessing.

### 3.1 The slingshot had to be found

bionic's `__libc_init` ends by jumping to the application entry point. Nothing calls it, so the
host provides a slingshot page; the engine's own code reaches it at `0xef3e2004`, at which point
the host has control with the engine's image loaded and initialized.

### 3.2 No 32-bit userspace, so bionic's loader ABI is the host's job

The guest is bionic-linked. `libc.so` calls into its dynamic linker through 26 `__loader_*`
function pointers that normally live in a table the linker fills in. The host builds that table,
plus `libc_shared_globals`, `__libc_preinit`, and the TLS layout.

Three of those details were wrong first and each produced a specific, diagnosable failure:

* `tls[TLS_SLOT_THREAD_ID]` must point at a **separate** `pthread_internal_t`, not back at the
  thread pointer. Pointing it at TP made bionic's main-thread registration overwrite
  `tls[0]`/`tls[1]`.
* The documented `libc_shared_globals` offsets do **not** match this sysroot's `libc.so`.
  `getenv` reads `__environ` from `+0x520`, and writing the mmap threshold there broke it.
  Only offsets proven to be used by this binary are written.
* `[TP-4]` must hold the thread pointer: `__libc_preinit_impl` does `ldr r0,[r0,#-4]` and then
  `strb` through it.

### 3.3 Packed relocations, reported wrongly once

`DT_ANDROID_REL` is `"APS2"` + a SLEB128 declared count + one more SLEB header field, then
groups. The first implementation claimed every relocation had been applied while silently skipping
libc++'s packed relocations. It now counts 2,645 and was validated against `llvm-readelf -r`
(2,112 entries, 0 mismatches, 7,072 bytes consumed). The correction is its own commit.

### 3.4 `JNI_OnLoad` is not the entry point

The recovered Java in the project's source-recovery package gives the order the DEX drives the
engine: `GameRenderer`'s constructor calls `nativeGameRenderer()` then `nativeConfig()`;
`DungeonHunter2.nativeInit(int)` is the Activity's start; `nativeRender()` runs once per frame.

`JNI_OnLoad` runs, calls `VoxSetJavaVMC(vm)` and `NVThreadInit(vm)`, and returns
`JNI_VERSION_1_4` -- **without making a single JNI call**, because this engine resolves its
natives by symbol name rather than registering them.

### 3.5 The cached JNIEnv global

Every one of the engine's JNI natives reads its `JNIEnv` from a global that the engine's own JNI
plumbing would have cached. There is no ART, so the host seeds it. Two mistakes here:

* The first attempt seeded `engine+0x99c52c`, which is initialised `.data` (it holds the constant
  `0x22422224`) with no relocation targeting it. The access trace named the real chain instead:
  GOT slot `engine+0x99952c` -> the global -> the env.
* The second attempt seeded it only before the Activity's `nativeInit`. The renderer's natives
  run **first**, so `nativeGet_PhoneManufacturer` read a null env. It is now seeded before any
  engine code runs, and `GameRenderer.nativeInit` returns.

### 3.6 GL: a name-carrying stub table

The generated stubs were `mov r0,#0; bx lr`, and that is what stopped the first frame:
`glitch::video::CCommonGLDriver<...>::genericDriverInit` calls `glGetString(GL_VERSION)` and
immediately scans the result. The stubs now carry their symbol name
(`svc #(0x400+index) ; bx lr`) and `host/gl` answers by name. The effect:

| | before | after |
| --- | ---: | ---: |
| `GameRenderer.nativeInit` | 45,388 instructions, then a null dereference | **13,103,112** |
| `GameRenderer.nativeRender` | fault after 25 instructions | **returns** |

The bridge answers the calls whose return value decides whether the renderer continues:
`glCreateShader`/`glCreateProgram` hand out distinct names, `glGetShaderiv`/`glGetProgramiv`
report `GL_COMPILE_STATUS`/`GL_LINK_STATUS` true, `glCheckFramebufferStatus` reports complete,
`glGetUniformLocation` is non-negative, and `glGetIntegerv` reports plausible ES 2.0 limits.

### 3.7 `_llseek`, and the syscall argument ABI

With the real cache the engine opened its assets and then died on `malloc(4294967271)` -- a
negative size. The cause was **`_llseek` (syscall 140) not being implemented at all**: 153 calls
in one run. bionic uses it on 32-bit ARM for its large-file seek and the engine's asset streams
use it heavily; without it a stream reported a size of `-1`.

Implementing it introduced a second bug, caught by FreeType's own log
(`FT_Stream_ReadAt: invalid read; expected 128 bytes, got 0`): the first version read the fifth
argument off the stack. **The ARM syscall ABI passes up to seven arguments in `r0`-`r6`**, and
bionic's `syscall()` wrapper loads exactly that many before `svc`. Reading the stack returned a
leftover stack pointer as the `whence`, so every seek landed at EOF and every read returned zero.
The fix is `r4`.

### 3.8 A filesystem, and mapping the right descriptor

The engine loads its own content by name. `host/vfs` plus real
`openat`/`open`/`read`/`pread64`/`close`/`lseek`/`fstat64`/`stat64`/`fstatat64`/`faccessat`
give it a read-only view of a directory. Verified by the engine's own requests:

```
open(/data/tweaker/player_light.tweaker_xml) -> fd 3 (7602 bytes)
open(DebugSwitches.savegame)                 -> fd 8 (665 bytes)
open(shaders.pak)                            -> fd 14 (44194 bytes)
open(#/system/fonts/droidsans.ttf)           -> fd 15 (757076 bytes)
open(#/system/fonts/._droidsans.ttf)         -> -ENOENT
open(#/system/fonts/.AppleDouble/droidsans.ttf) -> -ENOENT
```

Those last two are the engine's own font loader probing macOS resource-fork paths -- real code
running, not a stub. `mmap2` also had to be fixed to map the **host** descriptor for a file the
guest opened through this view, rather than passing the guest's descriptor number to the host.

### 3.9 Defects in the host's own instrumentation

Two of the longest detours were caused by diagnostics that lied:

* The write watch covered 8/16/32/64-bit stores but **not the exclusive (`STREX`) path**, so a
  write could happen invisibly. Both paths are watched now.
* Watches replaced each other instead of accumulating. Both the CPU and the guest memory layer
  now hold several ranges.

Both are worth stating because each produced a confident wrong conclusion before being found.

---

## 4. Building and reproducing

```
host/scripts/reproduce.sh [--clean] [--arm64]     # DH2_ENGINE=<path> for the engine steps
host/scripts/build-android.sh --static --run      # Android arm64, and run it under qemu
```

Verified from a clean checkout: Dynarmic fetched at the pin and patched, host built, guests built,
then 8/8 guest suite, 34/34 self test, 59,623/59,623 relocations, 539 `init_array` entries,
545/545 initializers, `JNI_OnLoad` -> `0x00010004`, and the engine's natives returning.

Writing `reproduce.sh` immediately found that `fetch-dynarmic.sh` was not idempotent: a second run
failed with "patch does not apply" instead of recognising an already-patched tree.

### Targets

| Target | Toolchain | Verified |
| --- | --- | --- |
| x86_64 Linux | clang 14, bundled externals | 8/8 guests, 34/34 selftest, engine runs |
| aarch64 Linux | `aarch64-linux-gnu-g++`, under `qemu-aarch64-static` | 8/8 guests, 34/34 selftest |
| Android arm64 | NDK r26d, static bionic, under `qemu-aarch64-static` | 8/8 guests, 34/34 selftest |

The aarch64 and Android builds matter for one specific reason: they exercise Dynarmic's **arm64**
backend, which is the product backend, including the T32 ARMv8 instructions our own patch adds.
On x86_64 those instructions are tested on the x64 backend only.

### Android toolchain findings

* NDK r29 does not build this tree: the bundled fmt 10 fails with
  `call to consteval function ... is not a constant expression`. This is **not** a clang-version
  problem -- clang 14 fails identically on the Android target while succeeding on Linux -- it is
  r29's libc++ being newer than fmt 10 expects. r26d (clang 17) builds it.
* Boost 1.74 needs `-DBOOST_NO_CXX98_FUNCTION_BASE` under C++20.
* Recent NDKs ship no `/system/bin/linker64`, so a dynamically linked Android binary cannot be
  executed off-device. Linking `-static` against bionic makes it runnable, which is what turns
  "it links" into "it passes its tests".

---

## 5. What is *not* established

Stated plainly, because the numbers above do not imply any of it:

* **No real-device run of the own host.** Everything here is off-device. The Android artifact has
  never executed on a phone.
* **No rendered image.** GL is *answered*, not implemented. There is no software rasteriser and no
  `libGLESv2.so` loaded, so nothing is drawn. "The renderer builds its pipeline" means the engine
  made the calls and accepted the answers.
* **No audio.** No OpenSL ES or AudioTrack backing.
* **One guest thread.** The futex path reports the value-changed case rather than blocking, so a
  second thread would not be sequenced correctly.
* **No networking.** `connect` is the only remaining unimplemented syscall (2 calls), and every
  Gameloft Live call is answered with "not happening".
* **No timing model.** `clock_gettime` is real, but there is no frame limiter or vsync.
* **The engine has never been driven past two frames.** Frame 2 does 20.3M instructions; frames 3+
  have not been observed.
* **Save writing.** Only reads are implemented; a save the engine writes is dropped, so save
  semantics are unverified.

---

## 6. Where this is going next

The own host is a working, reproducible, off-device harness that runs the real engine on three
architectures. The next step is the one it cannot take itself: **run the Android arm64 build on a
real device**.

That means, in order:

1. Package the host as a launchable Android artifact (the engine, the cache layout, and the JNI
   surface the Java side would call).
2. Replace the answered GL entry points with a real GL context -- either the platform's
   `libGLESv2.so` through the loader, or a software backend -- so a frame produces pixels.
3. Re-establish the Java side: the 25 methods the engine requests are answered by the host today;
   on a device they should reach the real Activity.

The engine's own content (the cache) and the engine itself are **not** committed to this
repository. They are the property of their rights holders; supply them at run time
(`--root`, `DH2_ENGINE`).
