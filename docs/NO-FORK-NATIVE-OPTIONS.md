# Native execution of specific guest functions with a stock translator: exhaustive options

Scope: with **ZettaBridge and Dynarmic both unmodified** (no patch, no fork), is there *any* way to
make a specific hot ARM32 guest function execute as native ARM64 instead of being JIT-translated?

Method: read the pinned sources. Every claim below carries a `file:line`. Statements that are
reasoned rather than read are marked **INFERRED**; things that cannot be settled without a
measurement or a device run are marked **UNKNOWN**. Nothing here is a recommendation to break the
owner's constraint in `TIER2-CONSTRAINT.md:3-6`; the fork arithmetic in §6 is supplied because it
was asked for, not because it is being proposed here.

Path legend:

| Alias | Root |
| --- | --- |
| `ZB` | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\research\ZettaBridge` (upstream `7c647a4f…` + `zettabridge-dh2.patch` applied) |
| `DYN` | `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\dynarmic` (HEAD `86458a0b…`, matches the pin) |
| `PATCH` | `C:\Users\NacWorkstation\Documents\DH2Work\patches\zettabridge\zettabridge-dh2.patch` |
| `DOCS` | `C:\Users\NacWorkstation\Documents\DH2Work\docs` |
| `WORK` | `C:\Users\NacWorkstation\Documents\DH2Work` |

Checkout state was verified rather than assumed: `ZB` is at `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`
with exactly the 13 files of the patch dirty and nothing else; `DYN` is at
`86458a0bd369d63ba4c2ef812cacbb6c9080c065`.

---

## 0. Verdict

**There is exactly one route, and it is not the route the question is hoping for.**

| # | Route | Verdict |
| --- | --- | --- |
| 1 | Extension point in stock+patched ZettaBridge that dispatches a guest address to host code | **DOES NOT EXIST.** Not a config file, not a flag, not an environment variable, not a JNI entry point, not a plugin ABI. Enumerated exhaustively in §2. |
| 2 | Guest → synthesized JNIEnv → Java native method → **our own ARM64 `libdh2fast.so`**, loaded by ART | **VIABLE-WITH-CAVEATS.** Works today with zero ZettaBridge change, because the project's own patch already put the app's arm64 lib directory on the guest class loader's native search path. **But** one call costs a full JNI round trip through ART, so it can only help for **coarse-grained** boundaries (a call per frame / per subsystem), never for the per-operation work that is the actual 82.7%. |
| 3 | Any other guest-initiated host facility (SVC host-call table, GLES/EGL stubs, syscalls, `dlopen`/`dlsym`, `eglGetProcAddress`, `InterpreterFallback`) | **NOT VIABLE**, each for a specific, cited reason (§3). |
| 4 | Replace the input: the guest sysroot `libc.so`/`libm.so`, the guest libraries, the engine binary | **VIABLE, and this is the best alternative.** It makes the *translated* code cheaper rather than making it native. Cross-referenced to the sibling workstream in §4.2. |
| 5 | Static recompilation (drop the translator) | **VIABLE but a different project** — it is the only way to get *all* guest code native without a fork, because it needs no translator at all. |

**Why route 1 fails is structural, not an oversight.** ZettaBridge's guest program counter is a
32-bit address into guest memory; the JIT reads code only through
`GuestThread::MemoryReadCode` (`ZB core/src/guest_thread.cpp:313-316`), which requires the guest page
to be `kPageExec`, and every entry into translated code comes from `Dynarmic::A32::Jit::Run()`
(`DYN src/dynarmic/interface/A32/a32.h:29`). There are exactly two ways execution leaves translated
code into host C++: a `svc` (`CallSVC`, `DYN src/dynarmic/interface/A32/config.h:107`, handled at
`ZB core/src/guest_thread.cpp:328-334`) and an exception/fallback
(`DYN src/dynarmic/interface/A32/config.h:104,109`, handled at `ZB core/src/guest_thread.cpp:318-343`).
Both are compiled into `libzbridge.so` and both are keyed on a fixed value (an SVC immediate, an
exception kind) — neither carries a function pointer, and neither can be extended from outside the
library. Consequently **every route that a guest can initiate today ends in a dispatcher whose
behaviour is already fixed at build time.** The only host runtime the guest can reach that is *not*
a fixed dispatcher is ART, via JNI — and that is route 2.

---

## 1. What "the translator cannot dispatch to host code" means, mechanically

Four independent facts, each checkable:

1. **No public API registers code or redirects execution.** The entire A32 JIT surface is
   `DYN src/dynarmic/interface/A32/a32.h:20-106`: `Run`, `Step`, `ClearCache`,
   `InvalidateCacheRange`, `Reset`, `HaltExecution`, `ClearHalt`, `Regs`, `ExtRegs`, `Cpsr`,
   `SetCpsr`, `Fpscr`, `SetFpscr`, `ClearExclusiveState`, `IsExecuting`, `DumpDisassembly`,
   `Disassemble`. Nothing takes a host function pointer, a symbol name, or an address substitution.
2. **`UserCallbacks` is a notification interface.** `DYN src/dynarmic/interface/A32/config.h:61-120`:
   memory read/write, `IsReadOnlyMemory`, `InterpreterFallback`, `CallSVC`, `ExceptionRaised`,
   `InstructionSynchronizationBarrierRaised`, `AddTicks`, `GetTicksRemaining`, `GetTicksForCode`.
   All are things the JIT *tells* the host, not things the host tells the JIT.
3. **The one real seam is translation-time IR injection, and using it is the excluded fork.**
   `TranslateCallbacks::PreCodeReadHook` / `PreCodeTranslationHook`
   (`DYN src/dynarmic/frontend/A32/translate/translate_callbacks.h:25,29`), called from
   `DYN src/dynarmic/frontend/A32/translate/translate_arm.cpp:33,41` and
   `DYN src/dynarmic/frontend/A32/translate/translate_thumb.cpp:114,124`. They hand over a live
   `A32::IREmitter&`; `CallHostFunction` exists as IR (`DYN src/dynarmic/ir/opcodes.inc:8`) and the
   arm64 backend compiles it to a real `bl` (`DYN src/dynarmic/backend/arm64/emit_arm64.cpp:39`).
   ZettaBridge does not override either hook; the inherited defaults are no-ops
   (`DYN src/dynarmic/interface/A32/config.h:72,76`). Using this means editing ZettaBridge — excluded.
   Two details worth keeping if the constraint ever lifts: the return convention differs between the
   two declarations and the caller — `config.h:70-71` says "returning **true** precludes translation"
   while `translate_callbacks.h:23-24` says "returning **false**", and the call site
   (`translate_arm.cpp:33-36`) does `if (!hook(...)) { should_continue = false; break; }`, so **`false`
   ends the block**; and `CallHostFunction` returns `void`, so results must be staged in memory or
   written back with `SetRegister`.
4. **The one API that hands out a code address by name is deliberately hardened.** `eglGetProcAddress`
   is dispatched through `zbegl_manual_eglGetProcAddress`, which accepts only names beginning `gl` or
   `egl` and resolves them through the *guest* linker
   (`ZB core/src/gl/egl_manual.cpp:279-286`, `ZB core/src/gl/host_egl.cpp:160-182`). The comment is
   explicit: *"Only our own gl*/egl* stubs may be handed out, never an arbitrary guest symbol. The
   guest gets the address of a trap stub, never a host function pointer."*
   (`ZB core/src/gl/egl_manual.cpp:282-283`.)

A fifth fact that removes the last candidate: **the guest's host-call surface is fully enumerated and
contains nothing generic.** `kHostCallNames` is generated (`ZB core/src/gen/hostcalls.inc`, 579
entries, indices 0–578) and `host_call_name` is a linear scan over it (`ZB core/src/process.cpp:78-87`).
The complete index space is:

| Range | Owner | Count |
| --- | --- | --- |
| `0..578` | generated stub names: `libGLESv2.so` 259, `libGLESv1_CM.so` 87, `libEGL.so` 46, `libandroid.so` 184, `libjnigraphics.so` 3 | 579 (`ZB core/src/gen/hostcalls.inc:2-580`) |
| `0xFB00..0xFBFF` | `JNINativeInterface` slot stubs, `0xFB00 + slot` | `ZB core/include/zb/jni_protocol.h:13-14` |
| `0xFC00..0xFCFF` | flat JNI host calls | 59 named slots, `ZB core/include/zb/jni_hostcalls.h:4-63` |
| `0xFE00..0xFEFF` | library runtime (READY, CARRIER PARK) | `ZB core/include/zb/library_protocol.h:12-15` |
| `0x5AFFFF` | host→guest return immediate (`kHostReturnSwi`), emitted by `GuestThread::call` | `ZB core/include/zb/guest_thread.h:26-28`, `ZB core/src/guest_thread.cpp:164,173` |
| `0x5AFC00+` etc. | further generated sub-ranges reserved by the stub generators, per `DOCS/NATIVE-HOOK-MECHANISM.md:96-98` | not re-verified here |

Every one of them is a `case`/table row in code compiled into `libzbridge.so`. There is no
"call the host function named X" entry, no "invoke this host pointer" entry, and no host `dlsym`
anywhere in `core/` (the only `dlopen` string in the whole core is an error message for the *guest*
loader, `ZB core/src/library_runtime.cpp:138`).

The nearest thing to an extension point is `Process::set_host_call_handler`
(`ZB core/include/zb/process.h:29,52-54`) — but it is a `std::function` member set from C++, it can
only be installed before the runtime starts (`ZB core/src/library_runtime.cpp:352-359`), it is
installed exactly once by the build's own chain (`ZB core/src/jni/proxy_runtime.cpp:434-447`), and
changing what it points at requires recompiling `libzbridge.so`.

---

## 2. Every existing extension point that needs no source change

### 2.1 The complete inventory of externally settable knobs

This is the whole list. It was produced by grepping `getenv`, `ZB_`, and every `--` flag in `ZB core/`,
`ZB cli/`, `ZB android/`.

| Knob | Effect | Could it route guest code to host ARM64? |
| --- | --- | --- |
| `ZB_SYSROOT` env | default guest sysroot (`ZB cli/zbrun/main.cpp:29`) | No. It selects *guest* ARM32 files. But see §4.2 — it is the enabler for the best alternative. |
| `ZB_PRECISE_FAULTS` env | precise memory faults (`ZB core/src/process.cpp:173`) | No — a disassembler/CPU-behaviour toggle. |
| `ZB_INTERRUPT_SIGNAL` env | host signal used to interrupt a parked guest thread (`ZB core/src/signals.cpp:134`) | No. |
| `ZB_STRACE` env | syscall tracing (`ZB core/src/syscalls.cpp:107`) | No. |
| `ZB_GL_DIAGNOSTICS` env | read-back GL instrumentation, read once at engine construction (`ZB core/android/guest_jni_runtime.h:48`) | No. |
| `--sysroot DIR` | as `ZB_SYSROOT` (`ZB cli/zbrun/main.cpp:45-47`) | No. |
| `--report FILE` | persist the runtime report (`ZB cli/zbrun/main.cpp:48-50`) | No (§2.2). |
| `--precise-faults` | as `ZB_PRECISE_FAULTS` (`ZB cli/zbrun/main.cpp:51-53`) | No. |
| `--env NAME=VALUE` | guest-only environment (`ZB cli/zbrun/main.cpp:54-59`) | No. Guest env can set `LD_LIBRARY_PATH`/`LD_PRELOAD` for the *guest* linker — still ARM32. |
| `--alias FROM TO` | path alias, added by the project patch (`ZB cli/zbrun/main.cpp:60-65`) | No (§2.3). |
| `ZBridge.setPreciseFaults` | `setenv` then rebuild path (`ZB core/android/zbridge_jni.cpp:241-243`) | No. |
| `ZBridge.setGlDiagnostics` | `setenv` then rebuild path (`ZB core/android/zbridge_jni.cpp:247-249`) | No. |
| `ZBridge.setReportFile` | persist the report (`ZB core/android/zbridge_jni.cpp:192-199`) | No. |
| `ZBridge.addPathAlias` | path alias, added by the project patch (`ZB core/android/zbridge_jni.cpp:126-141`) | No. |
| `ZBridge.fixGuestLibrary` | rewrites `DT_NEEDED` to a basename and `DT_TEXTREL`→marker only (`ZB core/src/elf_fixups.cpp:220-256`) | No *by itself*, but it is a guest-input lever (§4). |
| `ZBridge.activatePlugin` / `onProxyLoaded` / `loadError` / `lastLoadError` / `runExecutable` / `runtimeReport` / `onNativeActivityCreated` | lifecycle, diagnostics, and the library-mode entry | No direct code-dispatch capability. |

There is no configuration file. No file in `ZB` is read as a settings input; the only file *written*
by the runtime is the report (`ZB core/src/runtime_report.cpp`, `write_runtime_report_to`). The
launcher's own `dh2-options.json` is a DH2Work concept, consumed by DH2Work Java, not by
`libzbridge.so` (`DOCS/ARCHITECTURE.md:166-168`).

### 2.2 The `--report` machinery

`--report FILE` installs a report observer and rewrites the file atomically on change
(`ZB cli/zbrun/main.cpp:48-50,75`). It is **write-only telemetry**: it records guest logs, exits,
register dumps, host-call names and the native-call census (`ZB core/include/zb/runtime_report.h:61-70`,
`PATCH:142-155`). It contains no input path, no command channel, and no code-loading behaviour.

### 2.3 The path-alias mechanism

Added by the project's own patch (`PATCH:7`, `PATCH:76-83`, `PATCH:105-121`) on top of
`Process::add_path_alias`, which is upstream (`ZB core/include/zb/process.h:139`,
`ZB core/src/process.cpp:247-256`). It rewrites a guest path prefix to a host path prefix for file
operations. It cannot introduce code: the file the guest then `mmap`s and jumps into is still guest
ARM32 bytes, translated by Dynarmic. Its genuine value is to let the project *substitute guest
inputs* — see §4.

### 2.4 The 12 JNI dyn-syms

`ZB core/android/zbridge_jni.cpp:72-251` defines exactly these, and `DOCS/NATIVE-BUILD.md:322-340`
confirms 13 exported `STT_FUNC` symbols (12 JNI + `zb_run_executable`, `ZB core/include/zb/zbridge.h:22`):

`runExecutable` (`:76`), `activatePlugin` (`:104`), `addPathAlias` (`:126`), `onProxyLoaded` (`:146`),
`onNativeActivityCreated` (`:163`), `loadError` (`:175`), `lastLoadError` (`:183`),
`setReportFile` (`:192`), `runtimeReport` (`:202`), `fixGuestLibrary` (`:209`), `setPreciseFaults`
(`:241`), `setGlDiagnostics` (`:247`).

**None of them accepts a host function pointer, a guest-address→host-function mapping, a symbol name
to invoke, or a plugin/`.so` to load.** `activatePlugin` takes a plugin root, a target SDK and a class
loader (`:104-123`). `onProxyLoaded` takes a proxy path (`:146-157`). `runExecutable` is the C entry
that starts a guest process (`ZB core/src/zbridge_c_api.cpp:10-28`). The export surface is also
deliberately minimal: `--gc-sections --strip-all --exclude-libs,ALL` (`ZB core/CMakeLists.txt:70`),
so no internal seam is reachable as a symbol from outside.

### 2.5 The SVC host-call channel

Mechanism: each generated guest stub is `svc #(0x5A0000 | index); bx lr` — the first entry,
`glActiveTexture`, is literally `svc #0x5a0000` (`ZB guest/stubs/gen/libGLESv2.S:9-12`) — with
`kHostCallBase = 0x5A0000` (`ZB core/src/process.cpp:70`).
The dispatcher is `ZB core/src/process.cpp:523-563`:

* `index = stop.swi & 0xFFFF` (`:524`),
* the handler chain is called and its return value decides "served" (`:532`, `:550-553`),
* an unimplemented index logs once and sets **`r0 = 0`, then execution continues** (`:554-562`),
* an unexpected SVC sets `r0 = -ENOSYS` (`:564-568`).

The chain itself (`ZB core/src/jni/proxy_runtime.cpp:434-447`) is ten fixed C++ dispatchers in a fixed
order: `HostGl`, `HostAssets`, `HostNativeWindow`, `HostEgl`, `HostLooper`, `HostSensors`, `HostInput`,
`HostConfiguration`, `HostPlatformCompat`, then `HostJni`. **This is the natural place a native
substitution would be added — inside the library.** From outside, an SVC can only reach behaviour that
is already compiled in.

Two consequences worth stating plainly:

* A free SVC index is *useless* without a handler. A guest cannot "claim" an index; the host decides.
* `Process::HostCallHandler` (as documented at `ZB core/include/zb/process.h:52-53`) is an
  **in-process C++ extension point**, not a guest-visible one. The previous analysis reached the same
  place (`DOCS/NATIVE-HOOK-MECHANISM.md:108-112`, `:124`).

### 2.6 The plugin / launcher Java surface — where one real capability already exists

The Java layer is *not* a translator seam, but it decides **where ART looks for native libraries**,
and that turns out to matter:

* `PluginClassLoader extends DexClassLoader` and, for a translated plugin, passes
  `hostNativeLibraryDir + File.pathSeparator + record.proxyDir()` as the library search path
  (`ZB android/launcher/app/src/main/java/com/zettabridge/launcher/PluginClassLoader.java:19-22`).
  That line is **the project's own patch** (`PATCH:36-49`) — it exists so Android's native loader
  picks an ARM64 namespace for the proxy instead of rejecting it (`DOCS/ARCHITECTURE.md:90`).
* `findLibrary` overrides ART's lookup and returns a path only for names present in the plugin's own
  ARM32 lib directory (`ZB …/PluginClassLoader.java:59-68`, `ZB …/PluginFiles.java:105-139`).
* The runtime bundle's asset list and version are **regenerated by DH2Work's own build script**, not
  by ZettaBridge: `WORK build/build_apk.py:113-121` walks `assets/zb/**` and rewrites `zb-files.txt`
  and `zb-version.txt`. See §4.1.

**Consequence — and this is the single positive finding of this document:** the guest's class loader
already searches the host app's `lib/arm64-v8a` directory, so an ARM64 library the project ships there
is reachable from guest Java **today, with no new ZettaBridge change**. That is route 2 (§3.1).

### 2.7 `cli/zbrun`

`ZB cli/zbrun/main.cpp:14-23` is the whole flag surface: `--sysroot`, `--report`, `--env`,
`--precise-faults`, `--alias` (the last two flags added by the patch, `PATCH:58-83`). It also
constructs a bare `zb::Process` (`ZB cli/zbrun/main.cpp:74-79`) with no host-call handler and no JNI
runtime — i.e. the CLI path cannot even serve GL/JNI host calls, so it is not a candidate platform
for this work. It runs only guest executables, ARM32, translated.

### 2.8 Compile-time-only seams (listed so the enumeration is complete)

These look like extension points and are not reachable without a rebuild:

| Seam | Location | Why it is not usable |
| --- | --- | --- |
| `LibraryRuntime::set_host_call_handler` | `ZB core/include/zb/library_runtime.h:42-43`, impl `ZB core/src/library_runtime.cpp:352-359` | C++ call, must precede `start()`, aborts if called after. |
| `LibraryRuntime::load_library` / `find_symbol` | `ZB core/include/zb/library_runtime.h:46-53` | Operates on the **guest** linker. Returns guest addresses. |
| `HostJni::register_native` / `NativeSlots` / thunk pool | `ZB core/include/zb/host_jni.h:83-85`, `ZB core/include/zb/native_thunks.h:17-66` | This *is* the address-mapping mechanism — but it maps a **guest** function (`NativeTarget::guest_function`, `ZB core/include/zb/native_thunks.h:22`, "Thumb bit included") into an ART-callable ARM64 thunk, i.e. the **host→guest** direction (`ZB core/src/jni/loader.cpp:136-150`). There is no inverse: nothing maps a host function to a guest address. |
| `JniLoader::load` | `ZB core/src/jni/loader.cpp:196-258` | Called when ART asks for a plugin library; binds the guest library's `Java_*` exports to guest code. |
| `ProxyLoadEngine` (virtual) | `ZB core/include/zb/proxy_runtime.h:72-103` | A C++ interface implemented by `GuestJniEngine` (`ZB core/include/zb/proxy_runtime.h:173-246`). Not a dynamic plugin: `GuestJniRuntime`'s members are concrete (`ZB core/android/guest_jni_runtime.h:33-58`) and nothing in `core/` dlopens anything. |
| `GlBackend` / `EglBackend` / `AssetBackend` / … | `ZB core/include/zb/gl_backend.h`, `egl_backend.h`, … | Same: C++ abstract classes, statically chosen. |
| `Dynarmic::A32::UserCallbacks` overrides | `ZB core/src/guest_thread.cpp:53-64` (`cfg.callbacks = this`) | Overriding them means editing ZettaBridge. |

### 2.9 Section 2 verdict

**No extension point in stock-plus-project-patch ZettaBridge can make a guest address execute host
ARM64 code.** The knobs listed in §2.1 are diagnostics, path routing, and lifecycle. The two seams
that could (`set_host_call_handler`, `PreCodeReadHook`) are C++ compiled into `libzbridge.so`.
The Java layer contributes exactly one capability — an ARM64 load path for guest Java — which is
real but does not touch the translator (§3.1).

---

## 3. Guest-initiated routes

### 3.1 JNI: guest ARM32 → synthesized JNIEnv → Java native method → our ARM64 library

**Verdict: VIABLE-WITH-CAVEATS. This is the only route that runs our ARM64 code, and it needs no
ZettaBridge change.**

Shape of the mechanism, all of it already present:

1. The guest gets a JNIEnv and JavaVM from `libzbjni.so`, which publishes
   `zb_jni_guest_api { new_env_fn, free_env_fn, java_vm }` (`ZB core/include/zb/jni_protocol.h:27-34`).
   Guest code uses the ordinary `JNIEnv` function table; each call becomes an SVC host call:
   `0xFC00+` flat calls, 59 of them (`ZB core/include/zb/jni_hostcalls.h:4-63`), or a
   `0xFB00 + slot` stub for the native interface (`ZB core/include/zb/jni_protocol.h:10-16`).
2. `HostJni::handle_host_call` serves that range against ART
   (`ZB core/include/zb/host_jni.h:46-48`, `ZB core/include/zb/host_jni.h:29-48`).
3. Guest **Java** runs on real ART (`DOCS/ARCHITECTURE.md:69`), and evidence records 36 registered
   natives plus executed render/touch/accelerometer/audio callbacks (`DOCS/ARCHITECTURE.md:144-147`).
4. The project injects its own Java into the guest APK as an extra DEX: the canonical sources are
   `WORK app/guest-java/` (`WORK README.md:106-107`, `DOCS/ARCHITECTURE.md:104-113`) and the build
   compiles them and writes them into the game APK as `classes2.dex`
   (`WORK build/build_apk.py:71-78`, `:88-93`). So we can add a class with a `native` method and a
   `static { System.loadLibrary("dh2fast"); }` block.
5. The ARM64 `libdh2fast.so` is found because the guest class loader's native search path is
   `hostNativeLibraryDir : proxyDir` (`ZB …/PluginClassLoader.java:19-22`, patch `PATCH:36-49`) and
   ART falls back to `nativeLibraryDirectories` when `findLibrary` returns null
   (`ZB …/PluginClassLoader.java:59-68`). **INFERRED** in one respect: the fallback-to-search-path
   behaviour is AOSP `Runtime.loadLibrary0`/`nativeLoad` semantics, read from memory of the platform,
   not verified on this device. **UNKNOWN** until measured.
6. Shipping the extra `.so` is a DH2Work build-script change only: `WORK build/build_apk.py:191-193`
   writes `lib/arm64-v8a/*.so` from `ZB/build/launcher/jniLibs/arm64-v8a`.

Two ways the guest can reach it:

* **Preferred: the guest's call site is patched to call a Java native method.** Guest ARM32 code makes
  a `CallStatic*Method` through its JNIEnv. Requires a guest byte patch at the call site (allowed —
  §4).
* **Alternative: an ARM32 stub we ship replaces the function body and calls the Java method.** Same
  cost, same requirement, marginally simpler to verify.

**Per-call cost, honestly.** This is a *stack* of crossings, not one: guest `svc` → `Process::dispatch_stop`
(`ZB core/src/process.cpp:500-563`) → `HostJni` → `JniBackend` over the real ART `JNIEnv`
(`ZB core/include/zb/jni_backend.h`, `ZB core/android/jni_env_backend.cpp`) → ART's JNI transition →
ART's native-method dispatch → our ARM64 function → back. ZettaBridge does not publish a measurement
of this, and none exists in the repository. **INFERRED** bound, with the reasoning shown:

| Quantity | Value | Basis |
| --- | --- | --- |
| GL host-call round trip (guest `svc` → host C++ → guest) | ~0.5–1.5 µs | **INFERRED**: ~680 GL calls/frame (`DOCS/PERFORMANCE-BASELINE.md:80-81`) and the whole translator slice is 3.9 ms/frame (`DOCS/PERFORMANCE-BASELINE.md:120-131`), which must also pay block dispatch, marshalling and returns |
| A JNI call through `HostJni`→ART | strictly more than the above; ~2–10 µs | **INFERRED**: adds JNIEnv slot dispatch, the 32-bit handle tables (`ZB core/include/zb/jni_handles.h`), `JniBackend` reflection, ART attach/transition, method resolution, native dispatch and an exception check. **UNKNOWN** without measurement |

**Therefore the decision rule.** A native replacement can only win if the translated work it removes
per call exceeds the crossing. Two reference points:

* Average translated instruction ≈ `1.18×` native, but a soft-float helper body is 171–264 decoded
  instructions per call (`DOCS/NATIVE-PORT-SHORTLIST.md:113-119`, `DOCS/SOFTFLOAT-CENSUS.md:820`),
  i.e. roughly `0.2–0.7 µs` of translated time at plausible rates. **INFERRED.**
* So at *helper* granularity a host round trip is a **pessimisation or, at best, a wash** — which is
  exactly the conclusion the sibling workstream reached independently
  (`DOCS/SOFTFLOAT-FINDING.md:110-113`).

**Conclusion for route 2:** it is a real, no-fork way to run ARM64 code, and it is worth using only
where a *single* crossing replaces milliseconds of work — a whole subsystem offload (e.g. "once per
frame, hand the skinning/animation update to ARM64"). It cannot deliver the per-operation arithmetic
that the profile says is the cost. It also inherits a hard constraint: the ARM64 side must not
re-enter the guest, and the boundary must be coarse enough that 2–10 µs is noise.

### 3.2 The generated GLES/EGL passthrough entry points

**Verdict: NOT VIABLE as a code-dispatch channel.** Exact counts, computed from
`ZB core/src/gen/hostcalls.inc`: **579** generated entries across five libraries — `libGLESv2.so`
259, `libGLESv1_CM.so` 87, `libEGL.so` 46, `libandroid.so` 184, `libjnigraphics.so` 3; of these
**392** are `gl*`/`egl*`. Indices 0–578 are contiguous-ish and fully accounted for by
`GlHostCall*`, `kEglHostCall*`, asset/window/input/sensor/configuration ranges and the append-only
platform-compat block (`ZB core/src/jni/proxy_runtime.cpp:430-433`,
`ZB core/include/zb/gl_hostcalls.h:13-22`, `ZB core/include/zb/egl_hostcalls.h:10-12`).

Each stub is `svc #(0x5A0000 | index); bx lr` emitted as guest ARM32 into a guest stub library
(`ZB guest/stubs/gen/libGLESv2.S:9-12`, generated per `ZB guest/stubs/gen/libGLESv2.S:1`), and each
index has exactly one compiled host handler that
marshals to the real driver. A guest cannot add an index. **The "abuse a rarely used GL name as a
native-call trigger" variant is also rejected**: the name table is generated from the Khronos registry
at ZettaBridge build time (`ZB third_party/README.md:5-10`) and cannot be extended; the host handler
for an existing name does fixed marshalling, not dispatch to a pointer we supply; and it would pay
the same round trip as §3.1 with none of the flexibility. (A host-side *interposition* of
`libGLESv2.so` via the APK's `lib/arm64-v8a` is **UNKNOWN** — I did not verify that Android's linker
would prefer an app-supplied `libGLESv2.so` over the platform one, and even if it would, it buys a
worse version of §3.1.)

### 3.3 Forwarded syscalls

**Verdict: NOT VIABLE.** `handle_syscall` (`ZB core/src/syscalls.cpp:1212-1246`) services guest
syscalls against 64-bit bionic, and its number switch ends in `default: res = -ENOSYS;`
(`ZB core/src/syscalls.cpp:1730-1732`). The constants for `execve`, `execveat`, `ptrace` and
`process_vm_readv/writev` exist in the generated header
(`ZB core/src/gen/syscall_nrs_arm.h:18,28,339-340,350`) but have **no case**, so they return
`-ENOSYS`. `clone` is restricted to `CLONE_VM|CLONE_THREAD` and `fork` is explicitly refused
(`ZB core/src/syscalls.cpp:1239-1246`); the guest linker's debuggerd fork/exec crash handlers are
deliberately not installed (`ZB core/src/syscalls.cpp:465-472`). No syscall takes a host function
pointer, and none can start or reach host ARM64 code. A guest *can* `mmap` a file `PROT_EXEC` and jump
to it — but the JIT reads those bytes as ARM32 (§1, `ZB core/src/guest_thread.cpp:313-316`), so the
file must be ARM32.

### 3.4 `dlopen` / `dlsym` inside the guest

**Verdict: NOT VIABLE for ARM64, USEFUL for guest substitution.** Guest `dlopen`/`dlsym`/`dlerror`
resolve through the **guest** linker, the ARM32 bionic in the sysroot
(`DOCS/ARCHITECTURE.md:72`), driven on the service thread via the `zb_service_api` function table
(`ZB core/include/zb/library_protocol.h:37-48`, `ZB core/src/library_runtime.cpp:401-425`). Handing
that linker an AArch64 ELF cannot work: it is an ELF32/EM_ARM loader. Handing it an ARM32 library we
supply works and changes *what* gets translated, which is §4.

### 3.5 `eglGetProcAddress` — considered, and explicitly closed

`ZB core/src/gl/egl_manual.cpp:279-286` restricts the query to `gl*`/`egl*` names and resolves them
through `HostEgl::stub_address`, which looks the name up in the guest libraries
(`ZB core/src/gl/host_egl.cpp:160-182`). A non-`gl`/`egl` name returns 0 and is recorded as a miss
(`:287-308`). The design intent is stated in the source (`:282-283`) and in the header
(`ZB core/include/zb/host_egl.h:50-53`). There is no way to make it return a host pointer.

### 3.6 `InterpreterFallback` — a seam that does not fire on this backend

`ZB core/src/guest_thread.cpp:318-326` implements `InterpreterFallback` as a fault, not as an
interpreter. Regardless of that: **the arm64 backend never emits a call to it.** The only emissions in
the pinned tree are in the x64 backend (`DYN src/dynarmic/backend/x64/a32_emit_x64.cpp:1133`,
`DYN src/dynarmic/backend/x64/a64_emit_x64.cpp:600`); the arm64 backend emits `CallSVC`
(`DYN src/dynarmic/backend/arm64/a32_address_space.cpp:217`) and nothing for `InterpreterFallback`.
So this hook is **dead code on the shipped target** and is not a seam at all, let alone one reachable
without a rebuild. (Verified by a full grep of `DYN src`: four hits total, none in
`backend/arm64/`.)

### 3.7 Route 2 and 3 summary

| Route | Reaches host ARM64? | Needs a ZettaBridge change? | Per-call cost |
| --- | --- | --- | --- |
| JNI → Java native method → our `.so` | **Yes** | **No** | 2–10 µs **INFERRED** |
| GLES/EGL stub indices | No (fixed handlers) | — | — |
| Syscalls | No | — | — |
| Guest `dlopen`/`dlsym` | No (ELF32 loader) | — | — |
| `eglGetProcAddress` | No (hardened) | — | — |
| `InterpreterFallback` | No (never emitted on arm64) | — | — |
| Any free SVC index | Only if a handler exists | **Yes** | ~0.5–1.5 µs **INFERRED** |

---

## 4. Replace-the-input routes

These change *what* the translator translates. They cannot make anything native, but they are the only
no-fork routes that attack the measured 82.7% guest slice
(`DOCS/PERFORMANCE-BASELINE.md:120-131`).

### 4.1 The enabler: the runtime bundle is project-generated

This is worth stating explicitly because it removes most of the friction people expect:

* `WORK build/build_apk.py:79-81` seeds `assets/` from `ZB/build/launcher/assets`, then the project
  writes into it freely.
* `WORK build/build_apk.py:113-121` **re-walks `assets/zb/**` and rewrites `zb-files.txt` and
  `zb-version.txt`**. So *any* file the project drops into `assets/zb/...` — including new files —
  becomes part of the manifest and the version hash with **no ZettaBridge change whatsoever**.
  (`ZB tools/make_launcher_bundle.sh` has a hardcoded list, but that script is bypassed for the
  project's own APK, because `build_apk.py` recomputes the manifest afterwards.)
* `WORK build/build_apk.py:191-193` is the only place the APK's `lib/arm64-v8a/*.so` come from, and
  it is a DH2Work file.

So: **the guest sysroot (`system/bin/linker`, `system/lib/libc.so`, `libm.so`, `libdl.so`, …), the
guest libraries (`zbhost`, `libzbcompat.so`, `libzbjni.so`, `libGLESv2.so`, `libGLESv1_CM.so`,
`libEGL.so`, `libandroid.so`, `libjnigraphics.so`) and the engine `.so` are all project-controlled
inputs.** The bundle layout and required members are documented at
`ZB core/include/zb/proxy_runtime.h:35-49` and `ZB android/launcher/app/src/main/java/com/zettabridge/launcher/RuntimeBundle.java:70-80`.

### 4.2 Guest sysroot `libc.so` / `libm.so` — the `__aeabi_*` bodies

The engine's floating point is software float **inside the guest**: 43 undefined `__aeabi_*` symbols,
`Tag_ABI_VFP_args = 1`, zero VFP arithmetic instructions, and 24,622 static call sites into soft-float
helpers/libm (`DOCS/SOFTFLOAT-FINDING.md:14-25`). Those helper bodies live in the **guest sysroot's
ARM32 `libc.so`** (`__aeabi_fadd` at `0x9f1e8`, 684 B; `__aeabi_dmul` at `0xa03a0`, 784 B;
`__aeabi_fmul` at `0xa06f4`, 496 B — `DOCS/SOFTFLOAT-FINDING.md:34-44`), i.e. they are Dynarmic
translation targets, not host functions (`DOCS/SOFTFLOAT-FINDING.md:31-44`).

**Verdict: VIABLE and the best available lever.** Replacing those bodies with ARM32 that uses real
VFP/NEON turns a body of 171 decoded instructions for `__aeabi_fadd` (`DOCS/SOFTFLOAT-CENSUS.md:820`)
— 124–264 translated instructions across the family per `DOCS/TIER2-CONSTRAINT.md:27` — into a handful
of translated instructions, at **zero additional call cost** — the call site already exists. It
requires no ZettaBridge change at all, because `system/lib/libc.so` and `libm.so` are already in the
bundle (`ZB tools/make_launcher_bundle.sh:14-25`, the `SYSROOT_FILES` list) and the project regenerates
the manifest (`WORK build/build_apk.py:113-121`).

**This is a separate workstream and is not duplicated here** — `TIER2-CONSTRAINT.md:42` names it as
`docs/SOFTFLOAT-VFP-REPLACEMENT.md` (not yet present in `DOCS` as of this writing). Cross-reference
only: the reason it is superior to any host-native route is that the crossing already exists, so the
win is not eaten by a round trip (`DOCS/SOFTFLOAT-FINDING.md:110-113`).

### 4.3 Guest libraries we ship

`zbhost`, `libzbcompat.so` (mandatory preload), `libzbjni.so` and the four generated stub libraries
are built by `ZB tools/build_guest.sh` and packaged as assets
(`ZB tools/make_launcher_bundle.sh`, `RuntimeBundle.java:70-80`). They are ARM32, so any change is a
change to translated code — but they are **entirely replaceable from DH2Work** via §4.1. Uses:
cheaper `__aeabi` PLT paths, an extra guest stub library for a guest-side rewrite, or a cheaper
`libzbjni.so` JNIEnv table if route 2 is ever used.

### 4.4 The engine binary itself

The engine (and Storm) are already byte-patched offline with SHA-256 and original-byte verification
(`DOCS/ARCHITECTURE.md:115-128`; `patches/engine/patch_engine.py`, `patches/storm/patch_storm.py`).
Rewriting hot guest functions as ARM32 that uses VFP/NEON is the second half of the same idea: the
translator then translates fewer, cheaper instructions. Bit-exactness is checkable with the existing
Unicorn oracle and differential harness (`DOCS/PERFORMANCE-BASELINE.md:90-98`). The verification
pipeline is named as a workstream in `TIER2-CONSTRAINT.md:46` (`docs/GUEST-REWRITE-PIPELINE.md`, not
yet present in `DOCS`).

### 4.5 The guest linker

`system/bin/linker` is a project-controlled bundle member (`RuntimeBundle.java:71`,
`WORK build/build_apk.py:104-112`). Replacing it changes *resolution policy* (which guest library
provides which symbol, e.g. forcing `__aeabi_*` to a library we ship) but not the fact that the
result is ARM32 and translated. Worth recording as a lever; low expected value versus §4.2, where the
symbols already resolve to a file we can simply overwrite.

### 4.6 What replace-the-input can never do

Make a guest instruction execute as ARM64. The translator's input is ARM32 by definition, and every
lever here changes the *bytes* it reads, not *whether* it reads them. This is the ceiling of the
no-fork strategy as currently specified (`DOCS/TIER2-CONSTRAINT.md:20-30`), and it is not a
performance-hostile ceiling: the fork was the cheap route to a narrow slice (the translator's 15.1%),
whereas these routes aim at the 82.7% (`DOCS/TIER2-CONSTRAINT.md:32-38`).

---

## 5. Reality check on the premise

**A stock translator fundamentally cannot dispatch to host native code.** Not "does not currently" —
*cannot*, without a change inside the library, because:

1. The JIT's only source of instructions is guest memory via `MemoryReadCode`
   (`ZB core/src/guest_thread.cpp:313-316`), and the only transitions out of generated code are the
   fixed `CallSVC` / `ExceptionRaised` callbacks
   (`DYN src/dynarmic/interface/A32/config.h:104-109`, handled at
   `ZB core/src/guest_thread.cpp:318-343`).
2. Those callbacks are C++ virtual functions resolved at build time against ZettaBridge's own
   `GuestThread` (`ZB core/src/guest_thread.cpp:53-64`). There is no registration API on
   `Dynarmic::A32::Jit` (`DYN src/dynarmic/interface/A32/a32.h:20-106`).
3. ZettaBridge's host-call dispatch is a fixed compile-time table and a fixed compile-time chain
   (`ZB core/src/gen/hostcalls.inc`, `ZB core/src/jni/proxy_runtime.cpp:434-447`), with no host
   `dlsym` anywhere in `core/`.
4. The guest's 32-bit pointer space cannot carry a host pointer, and the one host API that returns a
   code address by name refuses to return anything but a guest trap stub
   (`ZB core/src/gl/egl_manual.cpp:282-286`). Additionally, Math porting already established that
   "ARM64 code and all data/stack pointers [are] above 4 GiB" (`MATH/README.md:57`, quoted at
   `DOCS/NATIVE-HOOK-MECHANISM.md:220-221`).

So the owner's three-way choice stands, and it is a genuine choice rather than a false one:

| Option | What it buys | What it costs |
| --- | --- | --- |
| **Stay guest-only** (this phase's constraint) | Attacks the 82.7% slice; no translator maintenance; no licence question; reuses the proven byte-patch + differential-verification pipeline | Hard work per function, and it can never reach native *speed* — only fewer/cheaper translated instructions |
| **Accept a small fork** | The only way to route a guest address to host ARM64 with a cheap (~0.5–1.5 µs) SVC crossing instead of a JNI round trip. Cost quantified in §6 | A patch that grows beyond diagnostics; a rebuild that is already verified reproducible (`DOCS/NATIVE-BUILD.md:7-9`) |
| **Static recompilation** | All guest code native, no translator in the loop at all, no fork, and the endpoint where "hundreds of FPS" becomes theoretically real (`DOCS/SOFTFLOAT-FINDING.md:88-92`) | Months, not weeks (`DOCS/PERFORMANCE-BASELINE.md:104-113`) |

The one thing that is *not* on the menu is "no fork, but host-native hot functions anyway". The only
no-fork host-native execution available today is route 2, and its crossing cost confines it to coarse
boundaries.

---

## 6. What a fork would actually cost

Asked for explicitly, so it is quantified rather than asserted. Two shapes, both tiny in absolute
terms; the interesting part is *which files* and *which project* they touch.

### 6.1 Shape A — guest-entry stub → a new SVC index → a handler in ZettaBridge

Reuses the project's own proven patch discipline: replace the first instructions of a target function
with `svc #imm; bx lr` (8 bytes) and handle the index in `libzbridge.so`
(`DOCS/NATIVE-HOOK-MECHANISM.md:123`). Host side:

| File | Change | Loci |
| --- | --- | --- |
| `core/include/zb/native_hooks.h` | **new** — target table + `handle_native_hook(index, thread)` declaration | ~30–50 |
| `core/src/native_hooks.cpp` | **new** — signature-keyed marshalling (r0–r3 ↔ x0–x7, softfp pairs, sret) and the registered bodies | ~60–150 |
| `core/CMakeLists.txt` | add the new source to the `zbcore` list (the list is explicit, `core/CMakeLists.txt:1-46`) | +1 |
| `core/src/jni/proxy_runtime.cpp` | one line in the chain before `host_jni` (`:434-447`) | +1–2 |

Total: **2 new files + 2 edited files, ~100–200 LOC**, plus the guest-side patcher.

### 6.2 Shape B — translation-time per-address lookup (`PreCodeReadHook`)

| File | Change | Loci |
| --- | --- | --- |
| `core/include/zb/guest_thread.h` | declare the override | +2 |
| `core/src/guest_thread.cpp` | implement it; match a narrow address set; emit `CallHostFunction` + `SetRegister` write-back; terminate the block on a hit | ~40–80 |
| `core/include/zb/native_hooks.h` / `core/src/native_hooks.cpp` | **new** — the same target table and bodies as Shape A | ~90–200 |
| `core/CMakeLists.txt` | add the new source | +1 |

Total: **2 new files + 3 edited files, ~130–280 LOC.** The hook fires **per instruction**
(`DYN src/dynarmic/frontend/A32/translate/translate_arm.cpp:33`), so the address match must be a page
or hash fast path; `CallHostFunction` returns `void` (`DYN src/dynarmic/ir/opcodes.inc:8`), so results
round-trip through a scratch buffer or `SetRegister`. **No Dynarmic change is needed for either shape**
(`DOCS/NATIVE-HOOK-MECHANISM.md:265-266`).

### 6.3 How that interacts with the existing `zettabridge-dh2.patch`

Facts, not estimates:

* The patch is 13 files, `147 insertions(+), 12 deletions(-)` (`DOCS/NATIVE-BUILD.md:120-121`),
  readable in full at `PATCH:1-321`. It adds observability, path routing, and one link flag; the design
  rule is stated at `DOCS/ARCHITECTURE.md:101-102`.
* **It already modifies `core/src/process.cpp` and `core/src/syscalls.cpp`** (`PATCH:156-202`,
  `PATCH:263-308`) — i.e. it already edits the SVC dispatcher's neighbourhood. A hook patch is
  therefore *incremental to an accepted practice*, not a new category of change.
* **It touches no `core/CMakeLists.txt`** — because it adds no translation unit. Shape A/B would,
  by one line each.
* The build and patch application are **verified end to end and byte-reproducible**: the rebuilt
  `libzbridge.so` is byte-identical to the shipped one, SHA-256 `25e8da7e…682d24`, 3 306 992 B, and all
  20 bundled files hash identically (`DOCS/NATIVE-BUILD.md:7-9`, `:285-297`). 185 Ninja steps, ~70 s on
  20 cores (`DOCS/NATIVE-BUILD.md:187`). The pipeline is a 33-command recipe with scripts
  (`DOCS/NATIVE-BUILD.md:229-232`, `:453-469`).
* **The pin is fixed and upstream is not tracked.** The build clones the stage tree, checks out
  `7c647a4f…` and applies the patch (`DH2Work-toolchain/scripts/02-clone-and-patch.sh`, `PIN=` and
  `PATCH=` lines), and `DOCS/NATIVE-BUILD.md:115-117` shows the same. So there is **no rebase
  liability against a moving upstream** — the "stay compatible with upstream" objection is already
  spent: the project ships a modified `libzbridge.so` and always has.

**Quantified objection, therefore:** the marginal cost of the fork the owner is weighing is
**2–3 new files, 1–5 edited files, ~100–280 lines, no new Dynarmic change, one rebuild through an
already-verified pipeline, and no upstream-sync obligation**. The real cost is not the patch size; it
is the *permanent* obligation to keep `libzbridge.so` rebuildable (toolchain, NDK, Boost, sysroot) and
to re-verify after any change — and that obligation **already exists** today
(`DOCS/NATIVE-BUILD.md:392-449` lists exactly these limitations, including that no rebuilt library has
ever been run on a device).

### 6.4 ZettaBridge fork vs Dynarmic fork

These are materially different, and the difference is not size — it is *provenance*.

| | ZettaBridge (C++) | Dynarmic (submodule) |
| --- | --- | --- |
| Licence | PolyForm Noncommercial 1.0.0 + Perimeter 1.0.1, cumulative (`ZB LICENSE:3-9`) | **0BSD**, not MIT (`DYN LICENSE.txt:1-4`; `ZB third_party/README.md:16`) |
| Existing project-authored changes | **13 files, 147 insertions** (`DOCS/NATIVE-BUILD.md:120-121`, `PATCH:1-321`) | **Zero.** The two patches are ZettaBridge's own, tracked upstream at the pin (`ZB third_party/README.md:17,25,60`; tracked in git at `7c647a4f`, verified with `git ls-tree HEAD third_party/patches/`) |
| Who maintains the patch today | the project (`WORK patches/zettabridge/zettabridge-dh2.patch`) | the ZettaBridge author; the project only applies them, from a vendored copy in `WORK upstream-modified/third_party/patches/` |
| Upstream relationship | pinned commit, not tracked | pinned commit (`86458a0b…`), submodule, and ZettaBridge's own convention is "**the submodule pointer is never staged**" (`ZB AGENTS.md:1378`) — patches only |
| What a further change means | one more hunk in a file that already exists; same build | a **new patch file** and a **new apply step** — the project's first authored Dynarmic change |
| Blast radius of a mistake | integration layer: host-call table, JIT callbacks | the CPU translator itself, shared with A64, with two existing local patches to keep applying |
| Minimum viable change | ~100–280 LOC in `core/` (Shape A or B, §6.1–6.2), **no Dynarmic change at all** | larger by construction: a public registration API on `A32::Jit` plus block-lookup interception (`DOCS/NATIVE-HOOK-MECHANISM.md:124`, "150–400 LOC **plus a permanent Dynarmic fork**") |

So: **for this objective a Dynarmic fork is strictly worse than a ZettaBridge fork** — it is bigger,
it touches a component the project has never modified, it has to be re-applied on every submodule
update, and it buys nothing the ZettaBridge seam does not already provide
(`DOCS/NATIVE-HOOK-MECHANISM.md:124`, which rejected it for exactly these reasons). The licence is
*less* restrictive on the Dynarmic side, so licence is not the reason to avoid it; complexity is.

### 6.5 Licence position

Read from the files, not assumed:

* **ZettaBridge is source-available, not open source.** `ZB LICENSE:3-12`: PolyForm Noncommercial
  1.0.0 **and** PolyForm Perimeter 1.0.1, *cumulative*; reading, studying, modifying and sharing are
  permitted **for noncommercial purposes**. `LICENSE:45-47` and `LICENSE:121-123` grant the Changes
  and New Works License explicitly.
* **The scope of permitted use does not depend on how large the modification is.** The same clause
  covers a 147-line patch and a 15,000-line fork. What the Perimeter licence restricts is *use*:
  `LICENSE:129-135` — "Any purpose is a permitted purpose, except for providing to others any product
  that competes with the software … For example, a product may compete even if it provides its
  functionality via any kind of interface … and even if it is provided free of charge." That is a
  question about what the built product *is*, and it is unchanged by fork size. **This file is not a
  legal opinion** (cf. `WORK RIGHTS.md:9`).
* **The distribution obligation is a notice obligation, and it is already being met.** Both licences
  require conveying the terms and any `Required Notice:` lines (`LICENSE:41`, `LICENSE:117`). The
  licensor supplied no `Required Notice:` line — a grep of `ZB LICENSE` for `Required Notice` returns
  only the boilerplate examples at `:43` and `:119`. The APK ships the licence text:
  `WORK build/build_apk.py:166-169` copies `ZB/LICENSE` to `assets/licenses/ZettaBridge.txt`
  (and `third_party/README.md` to `Third-party.txt`), plus Dynarmic's `LICENSE*` at `:170-171` and
  transitive dependency licences at `:172-181`.
* **Dynarmic is 0BSD**, not MIT. `DYN LICENSE.txt:1-4` is the 0BSD grant ("Permission to use, copy,
  modify, and/or distribute this software for any purpose with or without fee is hereby granted"),
  and `ZB third_party/README.md:16` labels it "License: 0BSD". 0BSD imposes no attribution condition
  in the grant itself — weaker obligations than MIT. Patching or forking Dynarmic therefore creates
  **no new distribution obligation**; the notice the project already ships
  (`build_apk.py:170-171`) exceeds what 0BSD requires.
* Practical upshot: **licence is not the binding constraint on the fork decision.** Distribution of a
  modified, rebuilt `libzbridge.so` is already happening under the same two licences; a larger
  modification changes nothing about that. What *would* be a licence question is commercial use or
  shipping something marketed as a substitute for ZettaBridge (`LICENSE:129-135`), and neither is a
  function of patch size.

---

## 7. Recommendation

1. **Do not spend effort looking for a no-fork hook.** It does not exist; §2 and §5 are exhaustive and
   the reasons are structural. Record the "no" and move on.
2. **Pursue the replace-the-input routes**, in this order:
   a. the guest sysroot `__aeabi_*` → VFP bodies (highest leverage, zero extra call cost, already a
      workstream: `DOCS/TIER2-CONSTRAINT.md:42`, cross-referenced in §4.2);
   b. guest binary rewriting of hot bodies via the existing verified patch pipeline
      (`DOCS/PERFORMANCE-BASELINE.md:90-98`, `:104-113`);
   c. the ~60 FPS frame limiter if it is engine-internal (`DOCS/TIER2-CONSTRAINT.md:29`).
3. **Keep route 2 (§3.1) in the toolbox for coarse offload only**, and only after measuring its real
   per-call cost. It is the one way to run ARM64 code today, and it is free of translator changes.
4. **If the fork is ever reconsidered, shape A/B in §6.1–6.2 is the whole cost** — ~100–280 lines in
   `core/`, no Dynarmic change, through a build pipeline that already reproduces the shipped binary
   byte for byte. Weigh it against the permanent rebuild obligation, not against the line count.

### What would change this answer

* **A measurement of the JNI round trip** (§3.1). If it came in near a plain SVC crossing (~1 µs), the
  no-fork native route becomes usable at much finer granularity and this document's conclusion about
  route 2 would need revisiting. **UNKNOWN** today.
* **A dynamic per-frame call count for the chosen hot functions.** The shortlist currently ranks by
  static helper-call density and in-degree (`DOCS/NATIVE-PORT-SHORTLIST.md:201-215`) and explicitly
  notes that a per-guest-PC counter is the missing measurement (`:227`). Without it, no
  host-native route can be sized.
* **A working device profiler on a rebuilt library.** `DOCS/NATIVE-BUILD.md:392-399` records that no
  rebuilt `libzbridge.so` has ever been executed. Any fork cost estimate carries that caveat.

---

## 8. Source index (everything read for this document)

ZettaBridge (read-only, unmodified): `core/include/zb/{process,library_runtime,library_protocol,
proxy_runtime,host_jni,jni_protocol,jni_hostcalls,jni_loader,jni_backend,native_thunks,native_call,
guest_thread,gl_hostcalls,egl_hostcalls,host_egl,zbridge,elf_fixups}.h`,
`core/src/{process,guest_thread,library_runtime,syscalls,signals,elf_fixups,zbridge_c_api}.cpp`,
`core/src/gen/hostcalls.inc`, `core/src/jni/{proxy_runtime,loader,native_thunks}.cpp`,
`core/src/gl/{egl_manual,host_egl}.cpp`, `core/android/{zbridge_jni.cpp,guest_jni_runtime.h}`,
`core/CMakeLists.txt`, `cli/zbrun/main.cpp`, `android/launcher/app/src/main/java/com/zettabridge/
{core/ZBridge.java,launcher/{PluginClassLoader,PluginFiles,RuntimeBundle,GuestRuntime}.java}`,
`tools/{gen_stubs.py,make_launcher_bundle.sh,build_guest.sh}`, `third_party/README.md`, `LICENSE`,
`AGENTS.md`, `guest/stubs/gen/*.S`, `guest/zbjni/*`.

Dynarmic (`86458a0b…`): `src/dynarmic/interface/A32/{a32.h,config.h}`,
`src/dynarmic/frontend/A32/translate/{translate_callbacks.h,translate_arm.cpp,translate_thumb.cpp}`,
`src/dynarmic/ir/opcodes.inc`, `src/dynarmic/backend/arm64/{emit_arm64.cpp,a32_address_space.cpp}`,
`src/dynarmic/backend/x64/{a32_emit_x64.cpp,a64_emit_x64.cpp}`, `LICENSE.txt`.

DH2Work: `docs/{NATIVE-HOOK-MECHANISM,TIER2-CONSTRAINT,SOFTFLOAT-FINDING,SOFTFLOAT-CENSUS,
PERFORMANCE-BASELINE,NATIVE-PORT-SHORTLIST,NATIVE-BUILD,ARCHITECTURE}.md`,
`patches/zettabridge/zettabridge-dh2.patch`, `build/{build_apk.py,build_runtime.py}`,
`RIGHTS.md`, `notices/`, `DH2Work-toolchain/scripts/02-clone-and-patch.sh`.
