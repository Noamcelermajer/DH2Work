# Architecture and layer boundaries

This document states which layers DH2Work **owns**, which layers come from
**upstream**, and what each boundary is responsible for. It exists so that a
failure can be attributed to the right layer instead of being blamed on "the
translator" in general.

## 1. The rule

The game's original `armeabi-v7a` shared libraries are the product. DH2Work does
not recompile them, does not convert their ELF machine type, and does not
rewrite their C++ object layouts or vtables. It only:

1. hosts them inside an ARM64 Android application,
2. translates their CPU instructions with Dynarmic,
3. forwards their non-CPU requests (files, JNI, GLES/EGL, syscalls, signals) to
   real Android through explicit bridge paths, and
4. byte-patches a small, hash-pinned number of instructions and import stubs in
   the two libraries where the original code assumed a 2010-era Android.

The from-scratch reconstruction of the engine in C++/ARM64 is a **different
project** and is not present in this repository.

## 2. Layer stack

```text
   [ DH2Work owns ] -------------------------------------------------------
   app/launcher-java        setup UI, cache import, diagnostics, options
   app/guest-java           helpers injected into the game's DEX
   app/manifests            host + guest AndroidManifest templates
   patches/game             Java/smali/storage patching of the guest APK
   patches/engine           byte patch to libDungeonHunter2.so
   patches/storm            byte patches to libStormGLOFT.so
   patches/zettabridge      zettabridge-dh2.patch  (host patch layer)
   patches/16k-port         experimental 16 KiB host-mapper patches
   patches/thread-exhaustion  optional save-job engine patch
   build/, tests/, docs/, evidence/, notices/
   ------------------------------------------------------------------------

   [ upstream ZettaBridge ] -----------------------------------------------
   android/launcher         launcher app, PluginStore, PluginClassLoader,
                            proxy mechanism, stub activities
   core/                    guest ELF loader + linker emulation, guest memory,
                            guest threads, syscall layer, signal delivery,
                            JNI runtime, GLES/EGL trap stubs and marshalling,
                            runtime report
   cli/zbrun                reference CLI host
   ------------------------------------------------------------------------

   [ upstream Dynarmic ] --------------------------------------------------
   A32/A64 frontends and IR, JIT backends, fastmem, exclusive monitors
   ------------------------------------------------------------------------

   [ platform ] -----------------------------------------------------------
   Android ART + 64-bit bionic (host side of :guest process)
   AOSP ARM32 bionic sysroot (the *guest* libc/libdl/libm/... that the
   translated code actually links against)
   Android's GLES/EGL drivers (guest GL is marshalled here, not translated)
   Android SDK/NDK/JDK and the pinned Khronos-registry stubs
   ------------------------------------------------------------------------
```

## 3. What runs where

| Element | Process | Language / ABI | Owner |
| --- | --- | --- | --- |
| `Dh2Activity`, `Dh2Diagnostics`, `CacheArchive`, `LanguagePreference` | `:main` (host app) | Java, ARM64 | DH2Work |
| `PluginSwitchActivity`, `GuestLaunchActivity`, `Stubs$*` | `:guest` | Java, ARM64 | upstream ZettaBridge |
| Guest DEX (`classes.dex`, `classes2.dex` of the bundled game) | `:guest` | guest bytecode run by ART | original game + DH2Work helpers |
| `libzbridge.so` and `libzbproxy.so` | `:guest` | native ARM64 | upstream ZettaBridge + `patches/zettabridge` |
| `libDungeonHunter2.so`, `libStormGLOFT.so`, `libnativeinterface.so` | `:guest` | native ARM32, **translated** | original game + hash-pinned byte patches |
| guest `libc.so`, `libdl.so`, `libm.so`, `libz.so`, linker | `:guest` | native ARM32, **translated** | AOSP ARM32 bionic sysroot |
| GLES/EGL drivers | `:guest` | native ARM64 | Android platform (reached through traps) |

Running the guest in a separate `:guest` process is deliberate: a translated
abort exits that process with a status while the `:main` setup activity survives
to collect and export the report (`Dh2Diagnostics`, app-UID logcat rotation,
`ApplicationExitInfo`).

## 4. The host patch layer (`patches/zettabridge/zettabridge-dh2.patch`)

Cumulative patch against upstream ZettaBridge
`7c647a4f1ea150eab7978ab0da28fdf49f3a79de`. It touches 13 files; the readable
post-patch copies live in `upstream-modified/`.

| File | Change and reason |
| --- | --- |
| `android/.../core/ZBridge.java` | Adds `native void addPathAlias(String,String)`. |
| `android/.../launcher/LoadedPlugin.java` | After activating the DH2 plugin, registers guest→host path aliases for `/data/data/<pkg>/`, `/data/user/0/<pkg>/`, their `lib/` subdirectories, and the legacy `/sdcard` + `/storage/emulated/0` `Android/data/<pkg>/files/` cache roots, all pointing at app-private directories. Also creates the external cache directory. |
| `android/.../launcher/PluginClassLoader.java` | Passes the host `nativeLibraryDir` **before** the proxy directory as the translated plugin's native search path, so Android's native loader selects an ARM64 namespace (and, on an x86_64 emulator, a bridged one) instead of rejecting the proxy as the wrong ELF machine. |
| `core/android/guest_jni_runtime.h` | Exposes `add_path_alias` on the JNI runtime. |
| `core/android/zbridge_jni.cpp` | Implements `Java_..._ZBridge_addPathAlias` with validation (both prefixes must be absolute, directory-terminated) and a clear exception when called before activation. |
| `core/include/zb/process.h` | Adds `thread_report()`, makes `mm_mutex_` mutable, adds a one-shot `crash_state_reported_` flag. |
| `core/include/zb/runtime_report.h` | Adds a bounded guest-log tail (`note_guest_log`), raises the retained failed-open list from 4 to 16 entries. |
| `core/src/process.cpp` | Reports registers/module-relative PC-LR/stack bytes for a **nonzero guest exit**, not only for a fault signal; reports from the stopped JIT's dispatcher rather than a host signal handler; collects up to 16 executable-address **candidates** from the stack (explicitly not an unwound backtrace). |
| `core/src/runtime_report.cpp` | Keeps the most recent distinct failed opens instead of the first four; stores 64 guest-log lines; prints them in the report; labels the "EGL context created outside the guest EGL bridge" observation instead of reporting a misleading thread mismatch. |
| `core/src/signals.cpp` | Records a pending-signal termination in the runtime report, so `128+signo` exits are not silent. |
| `core/src/syscalls.cpp` | Mirrors guest `write`/`writev` to fd 1/2 and to the verified liblog `/dev/socket/logdw` peer into the bounded report tail (so buffered STL abort reasons survive an abort); in `exit_group` for a multithreaded guest, requests the exit status **before** `std::_Exit` so the status is persisted. |
| `tools/build_guest.sh` | Links the `zlib_dynamic` guest test against `-lz`. |

Design rule for this layer: it adds **observability and path routing**. It does
not suppress assertions, bypass licensing/billing, or mask guest errors.

## 5. The guest-Java helper layer (`app/guest-java/`)

Injected as an extra DEX next to the original game classes:

| File | Responsibility |
| --- | --- |
| `GamePaths.java` | Resolves the legacy absolute paths the game computes to the wrapper's app-private directories. |
| `MediaQueries.java` | Replaces the original one-element `*` projection playlist query with an explicit `_id, name` query, copies rows into an owned cursor, and always returns a non-null empty cursor on failure. |
| `GameTrace.java` | Records guest-side milestones (language, surface creation/dimensions, renderer callbacks, lifecycle, model probes) and the phone-language callback value. |
| `test10/local/dh2/compat/GameTrace.java` | Test 10 variant that computes the largest 16:9 rectangle inside the real `DisplayMetrics`, calls `nativeSetPhone` with the fitted size when fit is enabled, and restores the GL viewport after the original renderer callback. |

## 6. The byte-patch layer

All patches verify input SHA-256 and original bytes, and refuse a different game
build.

| Patch | Target | Mechanism |
| --- | --- | --- |
| `patches/engine/patch_engine.py` (+ `engine_path_fix.S`/`.ld`) | `libDungeonHunter2.so` VA `0x56dd9c` | Replaces 5 ARM instructions (20 bytes) so `CFileSystem::open` recognises a leading `/` as absolute while keeping the original colon-path and relative-path behaviour. No addresses, layouts or dynamic symbols move; no allocation, no new host callback. |
| `patches/storm/patch_storm.py` (+ `storm_bias_fix.S`/`.ld`) | `libStormGLOFT.so` VA `0x438e4` | Replaces the inline hook's `ldr r0,[r0,#0x8c]` load-bias read of private bionic `soinfo` fields with a PIC stub that resolves the exported `JNI_OnLoad` and subtracts its exact symbol VA. |
| `patches/storm/storm_import_fix.c` | `libStormGLOFT.so` import-hook path (`0x371e4` → stub at `0xd3800`) | Freestanding C replacement that walks the engine's public ELF program headers, `PT_DYNAMIC`, symbol/string tables and `R_ARM_JUMP_SLOT` relocations instead of private linker fields. |
| `patches/storm/storm_path_repair.h` | Storm `fopen` import guard | Rejects trailing-separator paths with `null`/`EISDIR`; retries an exact repeated Android cache root after a failed read-only open; retries flat `qata/3d/textures/*.tga` as `data/3d/textures/*.tga`; logs buffered STL `puts` output to liblog and fd 2. |
| `patches/game/patch_game.py` | Guest APK (Java/smali, manifest, storage) | The launcher/storage compatibility layer inside the original game package: path resolution, `SDFolder` preference, empty-string comparison, denied-telephony guards, manifest adjustments. |
| `patches/thread-exhaustion/patch_sync_jobs.py` | `libDungeonHunter2.so` RVA `0x32c534` | Optional diagnostic: changes one `BNE` so per-frame save jobs take the existing synchronous state-17 path instead of spawning a thread per frame. |
| `patches/16k-port/*.patch` | ZettaBridge guest-memory, `MADV_WIPEONFORK`, `MADV_DONTNEED` | Experimental 16 KiB host-page support: keep the 4 KiB guest ABI, track host pages and four guest subpages separately, materialise private file mappings, disable fastmem on 16 KiB hosts. **Not integrated into the shipped launcher.** |

## 7. Boundaries that are *not* translation

* **GLES/EGL.** Guest `gl*`/`egl*` calls are trapped by generated stubs and
  marshalled to the phone's real driver. The driver runs natively and is never
  interpreted by Dynarmic. Consequently a wrong-looking frame is a marshalling,
  layout or shader problem, not an A32 decoding problem. The game also uses a
  Java `GLSurfaceView`, so EGL context creation and buffer swaps happen outside
  the guest trap path — the bridge therefore counts zero swaps even when frames
  are presented. The patch layer labels that explicitly in the report.
* **Filesystem.** Guest file paths are the *guest's* view. Two distinct
  redirections exist: host-side path aliases inside `libzbridge.so` (section 4)
  and the Storm `fopen` import guard (section 6). The engine's own
  `CFileSystem::open` is a third, guest-side path computation that the engine
  byte patch corrects.
* **JNI and ART.** Guest `JNI_OnLoad`/`RegisterNatives` reach ART in the same
  `:guest` process. Test evidence records 36 registered natives and executed
  render/touch/accelerometer/audio callbacks, which proves dispatch for those
  calls, not every signature, pointer, exception or callback lifetime.
* **Syscalls and signals.** Guest syscalls are serviced by the ZettaBridge
  syscall layer against 64-bit bionic; guest signals are delivered by the
  bridge. `SIGABRT`/status 134 and `SIGSEGV`/status 139 in the reports are guest
  statuses reported by the bridge, not necessarily tombstones.
* **Memory.** The **guest** ABI is 4 KiB pages. The shipped runtime maps guest
  pages directly and therefore requires a 4 KiB **host** page size, which
  `Dh2Activity` enforces before launch. 16 KiB ELF `LOAD` alignment alone does
  not make the mapper 16 KiB-safe.
* **Guest threads.** Each guest thread gets its own Dynarmic JIT cache. Save-job
  threads that are created per frame therefore consume guest address space;
  that is the observed Test 6 failure and the reason for the optional
  synchronous-path patch.

## 8. Launch sequence

1. `Dh2Activity` (in `:main`) copies the bundled guest APK to the plugin store if
   its SHA-256 revision changed, or accepts an imported cache ZIP through the
   document picker (`CacheArchive`).
2. The setup screen writes `dh2-options.json` (`preferEnglish`, `fit16by9`,
   `preserveContext`) and the game's `SDFolder` preference, pointing at
   `<externalFilesDir>/plugins/com.gameloft.android.GAND.GloftD2SS/`.
3. `Launch game` checks the host page size (4096), applies the saved-language
   preference (`LanguagePreference`), then starts `PluginSwitchActivity`.
4. `GuestLaunchActivity` and the `Stubs$*` activities, all in `:guest`, launch
   the original game activities from the plugin DEX.
5. `PluginClassLoader` resolves the guest classes; ZettaBridge activates the
   plugin, registers the DH2 path aliases and loads `libzbridge.so`.
6. `libzbridge.so` loads the guest ARM32 libraries and the ARM32 bionic sysroot,
   then translates and executes them; the DH2 byte patches and helper DEX are
   already in place from steps 1 and 2.
7. On abort/exit, `:guest` dies with a status; the runtime report and app-UID
   logcat are collected on return to `:main` and exported via
   `Dh2Diagnostics`.

## 9. Deliberately out of scope

* Reconstructing engine functions in C/C++ or ARM64 (separate track; the
  historical four-function prototype is not included here).
* Replacing Dynarmic with another CPU translator.
* Removing Storm's graphics/resize hooks.
* Changing licensing, billing or store-identity decisions.
* Shipping the game APK or the game cache.
