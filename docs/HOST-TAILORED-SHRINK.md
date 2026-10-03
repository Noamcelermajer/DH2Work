# HOST-TAILORED-SHRINK.md — deleting the unreachable half of the compatibility host

**Scope.** Dungeon Hunter 2 HD v1.0.2 (32-bit `armeabi-v7a`) on the arm64-only Galaxy Z Fold7,
executed by ZettaBridge's Dynarmic AArch32→AArch64 JIT. This document records a **fork** of
ZettaBridge with the host surface this game cannot reach deleted, the proof that each deleted
piece is unreachable, and the device run that shows the game still boots and renders.

* **Fork:** `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\shrink\ZettaBridge`
  (upstream `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` + `zettabridge-dh2.patch`, notices retained;
  a `git diff HEAD` in that tree is the complete change set).
* **Build scripts:** `…\host\shrink\scripts\` (`sync.sh`, `build.sh`, `verify.sh`,
  `make_keep_list.py`, `prune.py`, `delete_files.py`, `prune_tests.py`, `device-run*.ps1`).
* **Pipeline:** unchanged from `docs/NATIVE-BUILD.md` / `DH2Work-toolchain\scripts\03-build.sh`
  (WSL2 Ubuntu, NDK r29 `linux-x86_64`, Boost 1.83 headers, CMake 4.2.3, Ninja, Release,
  arm64-v8a, android-29), except that `cli/zbrun` no longer exists, so only `zbridge` and
  `zbproxy` are built. `-DZB_BUILD_TESTS=OFF` is required (the same flag the verified pipeline
  already passes).

## 0. Result

| measure | before | after |
| --- | --- | --- |
| `libzbridge.so` bytes | **3 306 992** | **2 972 592** (−334 400, −10.11 %) |
| `libzbridge.so` SHA-256 | `25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` | `674e24f0d620d50651fee800b4d996fd221a60c77f9e314653e7c5deb0795b2d` |
| exported `STT_FUNC` (`.dynsym`) | 13 | 13 (identical set) |
| `DT_NEEDED` | `libGLESv2, libGLESv3, libEGL, libandroid, liblog, libm, libdl, libc` | `libGLESv2, libGLESv3, liblog, libm, libdl, libc` |
| host-call indices (`0x5A0000` table) | 579 | **92** |
| GLES dispatch arms | 258 | **92** |
| `GlBackend` driver methods | 259 GL + `set_error` + `invoke` | **92 GL + `glGetStringi`** + `set_error` + `invoke` |
| `gl_manual.cpp` | 1 539 lines, 48 manual bodies | 814 lines, 12 manual bodies |
| tracked source (`.c/.cpp/.h/.inc/.S`, no `third_party/`, no `docs/`) | 237 files, 43 807 lines | 162 files, 30 154 lines |
| `git diff HEAD` (pinned upstream) | — | 105 files, +250 / −14 917 |

The library exported exactly the same 13 functions before and after (`11 × Java_com_zettabridge_core_ZBridge_*`,
`Java_com_zettabridge_core_ZBridge_runtimeReport`, `zb_run_executable`), verified with
`llvm-readelf --dyn-syms` (`scripts/verify.sh`).

## 1. The import-surface fact every deletion rests on

From `docs/HOST-IMPORT-SURFACE.md`, all read out of the shipped bytes:

* §1.1 — the engine's 361 undefined symbols attribute to `libc 221`, **`libGLESv2 92`**, `libm 37`,
  `libstdc++ 9`, `liblog 2`; **`libEGL` and `libandroid` are absent**. `libStormGLOFT.so` names
  `libEGL.so`, `libandroid.so` and `libz.so` in `DT_NEEDED` but imports **0** symbols from each.
* §2.1 — `egl*` in `.rel.plt` = 0, `gl*` = 92; no `egl*` string exists anywhere in the engine.
* §2.3 — the deduplicated entry-point set is **92 GLES2 names, 0 EGL**.
* §2.4 — ships→needed: `libGLESv1_CM` 87→0, `libEGL` 46→0, `libandroid` 184→0,
  `libjnigraphics` 3→0.
* §3.4 — **92 of the 579 host-call indices are reachable** (all `libGLESv2.so`), 487 are not.
* §4.1 — 36 registered natives (32 engine + 4 `libnativeinterface`), unchanged by this work.
* §5.3/§5.4 — the syscall layer is *not* reducible from static analysis (a lower bound of 61,
  host-proven 147); it was therefore **kept whole** except for the watchdog slots (§2.5).

## 2. What was deleted, and why it is unreachable

Line numbers are from the **pre-deletion** fork (= the pristine stage checkout).
`HOST-ARCHITECTURE.md` §1.0 rows are cited where the plan already ruled the component droppable.

### 2.1 All EGL (100 % of it)

| deleted | evidence |
| --- | --- |
| `core/src/gl/host_egl.cpp` (251 lines), `core/include/zb/host_egl.h` (168) | the only `HostEgl` definition; it served indices 168–211 |
| `core/src/gl/egl_manual.cpp` (433), 21 `zbegl_manual_*` bodies at `:192-464` | §2.1: 0 EGL imports, no `egl*` string in the engine |
| `core/src/gen/egl_dispatch.inc` (275 lines, 44 `case ZB_EGL_HC_*` arms), `core/include/zb/egl_hostcalls.h`, `core/include/zb/egl_backend.h` | generated EGL host-call protocol; unreachable with the dispatcher |
| `core/android/egl_driver_backend.{h,cpp}` + `egl_driver_backend_overrides.inc` | the real `::egl*` forwards; nothing can call them once `HostEgl` is gone |
| EGL rows of `core/src/gen/hostcalls.inc` | indices 168–215 and 214–215; §3.4 counts them unreachable |
| `HostGl::EglContextProbe` + `gl_egl_context()` (`core/src/gl/host_gl.cpp:97-100`, `gl_driver_backend.cpp:84-86`) | the last `eglGetCurrentContext()` caller; removed so `libEGL.so` leaves `DT_NEEDED` |
| `target_link_libraries(zbridge … EGL android)` (`core/CMakeLists.txt:65`) | nothing left in the library calls `egl*` or `AAsset*`/`ANativeWindow*` |

### 2.2 `libandroid` / the whole NDK backend set (zero imports)

Deleted: `core/src/android/*` (7 files, 2 190 lines: looper, native window, configuration, input,
native activity, sensors, platform compat), `core/src/assets/host_assets.cpp`,
`core/android/{asset,configuration,input,looper,native_window}_driver_backend.*`,
`core/android/native_activity_glue.*`, `core/src/gen/{configuration,input}_{dispatch,driver}.inc`,
and the 19 matching `core/include/zb/host_*.h` / `*_backend.h` / `*_hostcalls.h` headers.

Evidence: §1.1/§2.4 — **zero** `AAsset*`, `ANativeWindow*`, `ALooper*`, `ASensor*`,
`AInputEvent*`, `AConfiguration*` imports anywhere in the corpus, so 184 (`libandroid`) + 3
(`libjnigraphics`) host-call indices have no caller; `HOST-ARCHITECTURE.md` §1.0 row 11 marks the
whole set DROP. The game is a Java `GLSurfaceView` activity, so `NativeActivity` support has no
user either (row 11 and §1.13).

### 2.3 8 of the 10 chain dispatchers

`core/src/jni/proxy_runtime.cpp:434-447` installed ten dispatchers (`HostGl`, `HostAssets`,
`HostNativeWindow`, `HostEgl`, `HostLooper`, `HostSensors`, `HostInput`, `HostConfiguration`,
`HostPlatformCompat`, `HostJni`). Only `HostGl` (92 GLES indices) and `HostJni` (`0xFB00+`,
`0xFC00+`) remain:

```cpp
runtime_->set_host_call_handler(
    [host_jni, host_gl](std::uint32_t index, GuestThread& thread) {
        if (host_gl != nullptr && host_gl->handle_host_call(index, thread)) return true;
        return host_jni->handle_host_call(index, thread);
    });
```

An index nobody claims still reaches the existing record path (`process.cpp:554-562`,
`r0 = 0` + `note_unimplemented_host_call`), which is exactly how the device run proves the
retained set is sufficient: `unimplemented-host-calls: 0`.

### 2.4 487 of 579 host-call indices, 300 of 392 GL entry points

`core/src/gen/hostcalls.inc` (580 lines) was pruned from 579 rows to the **92 reachable indices**,
and `core/include/zb/gl_hostcalls.h` regenerated as a **sparse** table with a binary search.

**Index values are preserved exactly.** The guest stub libraries
(`assets/zb/guest/lib/libGLESv2.so` and friends) trap with `svc #(0x5A0000 | index)` (§3.1) and are
shipped inside the game APK — `build_apk.py:81` copies them from the runtime bundle, they are not
rebuilt here — so the host table had to keep the original numbering rather than renumber 92 rows.
The retained set was extracted from `HOST-IMPORT-SURFACE.md` §3.4 and cross-checked row-by-row
against `hostcalls.inc` (`scripts/make_keep_list.py`: *"keep list: 92 indices, all libGLESv2.so,
all matching hostcalls.inc"*).

Then, mechanically (`scripts/prune.py`):

| file | before | after |
| --- | --- | --- |
| `core/src/gen/hostcalls.inc` | 579 rows | 92 rows (`unimplemented` path covers the rest) |
| `core/src/gen/gl_dispatch.inc` | 1 826 lines / 258 arms | 653 lines / 92 arms |
| `core/include/zb/gl_hostcalls.h` | 574 lines, array-indexed | 234 lines, 92 entries, binary search |
| `core/include/zb/gl_backend.h` | 259 GL virtuals | 92 + `glGetStringi` |
| `core/android/gl_driver_backend_overrides.inc` | 251 lines | 98 lines (92 forwards) |
| `core/src/gen/gl_manual.inc` | 48 declarations | 12 |
| `core/src/gl/gl_manual.cpp` | 1 539 lines / 48 bodies | 814 lines / 12 bodies |
| `core/include/zb/gl_ext_entries.inc` | 12 extension entry points | deleted (none imported) |

The 12 retained manual bodies are exactly the ones `HOST-ARCHITECTURE.md` §1.8 measured as needed
(`glGetFloatv`, `glGetIntegerv`, `glTexImage2D`, `glTexSubImage2D`, `glReadPixels`,
`glGetAttribLocation`, `glGetUniformLocation`, `glGetString`, `glShaderSource`,
`glVertexAttribPointer`, `glDrawArrays`, `glDrawElements`). The deleted GLES 3.0 bodies took their
private helpers with them (sync-object table, mapped-buffer mirroring, `clear_buffer_elements`,
`serve_uniform`, `guest_string_array`), and the GLES3-only `GlThreadState::Attribute::integer` flag
was removed with `glVertexAttribIPointer`, its only writer.

### 2.5 GL diagnostics, the hang watchdog, `cli/zbrun`

| deleted | evidence |
| --- | --- |
| `core/src/gl/gl_diagnostics.{cpp,h}` (1 657 + 46 lines) | opt-in instrumentation, gated by `ZB_GL_DIAGNOSTICS` (`core/android/guest_jni_runtime.h:48-49`); `HOST-ARCHITECTURE.md` §1.0 row 10 = DROP. Call sites removed from `host_gl.cpp:102,106`; `ZBridge.setGlDiagnostics` stays as a no-op (**`ZBridge.java:133` declares it and `LoadedPlugin.java:91` calls it**), so the Java surface still resolves |
| `core/src/hang_watchdog.cpp` (282) + `core/include/zb/hang_watchdog.h` (87) + `start_hang_watchdog()` (`proxy_runtime.cpp:466`) | row 15 = "default: drop". Its `record_thread_activity*` slots were written by `process.cpp:525,538,546,551` and `syscalls.cpp:1220-1226`; a grep proved **its own sampler was the only reader** (`snapshot_thread_activity`/`describe_thread_activity` are called nowhere else), so the slots went with the watchdog — which also removes one lock-free atomic write per syscall and per host call |
| `cli/zbrun/` and `add_subdirectory(cli/zbrun)` | `HOST-ARCHITECTURE.md` §1.0 row 21 and §1.13: it builds a bare `Process` with no host-call handler and no JNI runtime, so it cannot serve this game; it is not in the launcher bundle |
| `tests/host/` suites for all of the above (19 files, 2 436 lines) + their CMake targets | they can only test deleted code; `-DZB_BUILD_TESTS=OFF` is the pipeline's own setting |

## 3. The one deletion that had to be reverted: `glGetStringi`

`glGetStringi` is host-call index **278** and is **not** in the 92 reachable indices, so its
dispatch arm and its `hostcalls.inc` row are gone — a guest call to it now falls through to the
unimplemented path. **But the `GlBackend` method was kept**, because the retained `glGetString`
uses it *internally*:

```
core/src/gl/gl_manual.cpp  served_extensions()   -> backend().glGetStringi(GL_EXTENSIONS, i)
core/src/gl/gl_manual.cpp  glGetString(GL_EXTENSIONS) / glGetIntegerv(GL_NUM_EXTENSIONS)
```

Deleting it would have changed the answer to two *retained* entry points. The device run showed
why that matters: the guest's own log reports the context it created —

```
179104956332 tid=6338 surface created #1 EGL=android.opengl.EGLContext@372b8be8
  GL=OpenGL ES 3.2 V@0800.64.7 (GIT@f61dec9117, I68b0a64d7e, 1774510019) (Date:03/26/26)
  renderer=Adreno (TM) 830
```

— an **OpenGL ES 3.2** context (`dh2-events.txt`, report screen 3). In a GLES 3.x context
`glGetString(GL_EXTENSIONS)` is invalid and returns NULL, so the enumerated path is the *only* one
that produces an extension list. This is the deletion that was reverted, and the reason
`gl_backend.h` carries 93 GL methods while the dispatch carries 92.

**The one diagnostic casualty.** The EGL-current probe went with the EGL surface, so the runtime
report line `gl-egl-context-current` now prints `(unknown)` where the shipped host printed `yes`
(compare `_device-evidence\dh2work-r1\diag-longrun\zb-runtime-report.txt`). Every other report
category is intact; `gl-calls`, `gl-first-call`, `gl-first-error` and the crash/JNI sections still
populate.

## 4. Verification — what changed, not just that something did

### 4a. The build is still the verified one (control before editing)

The forked tree, built unmodified through `scripts/build.sh`, produced
**3 306 992 bytes / `25e8da7e…682d24`** — byte-identical to the shipped library and to
`NATIVE-BUILD.md` §4. The 25e8da7e baseline was taken first; every later number is a delta from it.

### 4b. Static verification of the shrunk library (`scripts/verify.sh`)

```
2972592 bytes
674e24f0d620d50651fee800b4d996fd221a60c77f9e314653e7c5deb0795b2d
ELF64  DYN (Shared object file)  AArch64
exported functions (.dynsym STT_FUNC, defined): 13     (the same 13)
DT_NEEDED: libGLESv2.so libGLESv3.so liblog.so libm.so libdl.so libc.so
eglSwapBuffers 0   eglGetProcAddress 0   eglCreateContext 0
ANativeWindow_ 0   AAssetManager_ 0      ALooper_ 0   ASensor_ 0
glBindAttribLocation 0   glMapBufferRange 0   glTexImage3D 0   glVertexAttribIPointer 0
glGetStringi 1          (retained, §3)
zetabridge-runtime-report 1   glActiveTexture 2
```

### 4c. Device run — installed, launched, watched it render

`scripts/device-run2.ps1` (`device\evidence2\run.txt`), Galaxy Z Fold7 SM-F966B, Android 16
(SDK 36), 2184×1968 unfolded, screen kept awake with `svc power stayon true`:

```
install: Performing Incremental Install … Success
installed: versionName=1.0-shrink24
processes: u0_a623 6015 … local.dh2.fold7 | u0_a623 6247 … local.dh2.fold7:guest
guest_alive=True
guest SIGSEGV lines      : 0
not-implemented hostcalls: 0
```

The APK was built by `fold7-build/build_apk.py` with `DH2_VERSION_CODE=24` (device was on 23),
`DH2_VERSION_NAME=1.0-shrink24`, `DH2_ANDROID_SDK_ROOT=C:\Android\Sdk`,
`DH2_JDK_ROOT=…\jdk-21.0.12.101-hotspot`, and the shrunk library staged in
`…\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so` (hash verified at
`674e24f0…` at stage time).

**It renders.** `device\evidence2\02-game.png` (screenshot 40 s after the LAUNCH GAME tap) is the
intro cinematic, subtitled *"And one prince comes to power on a tide of new evil."*

**Real numbers, from the runtime report the app displays (screenshots 05/06 in the same folder):**

```
registered-natives: 36
unimplemented-host-calls: 0
unimplemented-distinct: 0
first-unimplemented: (none)
guest-exit: (none)
gl-calls: 309264
gl-first-call: glGetIntegerv tid=6338
gl-first-error: glGetError 0x0500
egl-swaps: 0
native-calls: … GameRenderer.nativeRender=3947 … total=7000
guest-log: liblog: StormGLOFT Optimized shader: 49 … 125
1791049585758 tid=6338 render returned frames=3960 elapsedMs=129476
```

So: **309 264 GL host calls** went through the 92-entry dispatch, the engine's
`nativeRender` callback ran **3 947** times, and the render loop reports **3 960 frames in
129 476 ms** with steady-state deltas of 120 frames / 2 000 ms = **60 FPS** before the run was
paused (bringing the launcher to the front pauses the engine, as documented).
`guest-exit: (none)`, `unimplemented-host-calls: 0`, `0` `guest SIGSEGV` lines.

**The one pre-existing oddity, checked against the baseline.**
`gl-first-error: glGetError 0x0500` (GL_INVALID_ENUM) is **not** a regression: the project's own
pre-shrink long run reports the identical line
(`_device-evidence\dh2work-r1\diag-longrun\zb-runtime-report.txt`, alongside
`gl-calls: 6571031`). The code that produces it (`gl_pname_count`'s default arm + the
`glGetIntegerv` forward) is retained unchanged.

**Which library was under test.** The device is shared with other agents, and the installed APK
was replaced by an instrumented build (3 328 416 B library) minutes after this run, so the
post-hoc "hash the installed APK" check was inconclusive. The evidence that this run used the
shrunk library is (a) `versionName=1.0-shrink24` read back immediately after install and again in
the run log, and (b) `gl-egl-context-current: (unknown)` in the captured report where the shipped
host writes `yes` — the exact signature of the deleted EGL probe (§2.1, §3).

### 4d. The staged library was restored

The shipped `libzbridge.so` was copied out before staging and put back afterwards; the stage slot
is verified at `25e8da7e…682d24` / 3 306 992 B, i.e. bit-for-bit the file that shipped. The
shrunk library exists only under `…\host\shrink\`.

## 5. Deliberately NOT deleted

* **The syscall layer** (`core/src/syscalls.cpp`, 1 636 lines, 147 syscall numbers) and the
  generated `syscall_nrs_arm.h` / `syscall_names_arm.inc`. `HOST-IMPORT-SURFACE.md` §5.3/§5.4
  measures only a 61-number *lower bound* from static analysis (`mmap2`, `brk`, `futex` are
  reached indirectly) and says so explicitly; §5.4 lists the memory syscalls as mandatory. The
  device log confirms the layer is live (`unimplemented syscall sched_getparam/…` lines, which are
  pre-existing gaps, not shrink damage).
* **`libEGL.so` / `libandroid.so` / `libGLESv1_CM.so` as *loadable guest stubs*** —
  `libStormGLOFT.so`'s `DT_NEEDED` names them, so they must exist in the runtime bundle even
  though nothing is imported from them (`HOST-IMPORT-SURFACE.md` §1.1, §2.4 note). They are
  packaged from the bundle, untouched.
* **The JNI bridge, guest memory model, ELF loader/relocations, library mode (carrier/borrower),
  threads/TLS/signals, GLES marshalling and the launcher/plugin path** — the load-bearing half of
  the host, per `PORTING-POLICY.md`.
* **`cli/zbfix`** and the generator tools in `tools/` — not part of the built library. Caveat:
  `tools/gen_gles.py`, `gen_egl.py`, `gen_ndk.py` and `gen_stubs.py` are **not** re-run in this
  tree; their output is now hand-pruned (`hostcalls.inc`, `gl_dispatch.inc`, `gl_backend.h`,
  `gl_manual*.inc`, `gl_hostcalls.h`), and re-running them would restore the deleted surface.
  The `gen_*_check` CTest entries for the three GL/EGL/NDK generators were removed for that
  reason.

## 6. Limitations

1. **Not benchmarked for speed.** The deletions shrink code size, `DT_NEEDED` and the host-call
   hot path (a 92-row sparse lookup instead of a linear scan over 579 entries, one atomic write
   per syscall removed), but no frame-time comparison against the shipped host was made. The
   observed 60 FPS steady state matches the panel-rate finding in `docs/FRAME-LIMITER.md` and is
   not evidence of a speed-up.
2. **The host test suite was not run.** `-DZB_BUILD_TESTS=OFF` is the pipeline's own setting; the
   suites covering the deleted subsystems were removed and the remaining ones were not rebuilt.
3. **One run, ~2 minutes.** The 8-launch / 30-minute acceptance shape of
   `docs/PERFORMANCE-BASELINE.md` was not repeated; the claim here is "boots, renders, 3 960
   frames, no crash", not endurance.
4. **The device is shared.** Two other installs landed on the same package during this session;
   install/version evidence was captured inside the run window (§4c), and the final installed APK
   is another agent's build. The stage tree's `jniLibs` slot is the shipped library.
