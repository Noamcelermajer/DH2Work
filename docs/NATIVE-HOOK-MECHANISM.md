# Native hook mechanism: running one guest ARM32 function as hand-written ARM64

Scope: how to make a *specific* guest ARM32 function execute as hand-written native ARM64 in
this codebase instead of being JIT-translated by the embedded Dynarmic, while keeping
`libzbridge.so` in the path so the existing compatibility fixes keep working.

Everything below is read from source. Inferences are marked **INFERRED**; unresolved items are
marked **UNKNOWN**.

## Path legend

| Alias | Path |
| --- | --- |
| `DYN` | `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\dynarmic\src` (Vita3K clone, HEAD `86458a0bd369d63ba4c2ef812cacbb6c9080c065` — matches the ZettaBridge pin) |
| `ZB` | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\research\ZettaBridge` (upstream `7c647a4f`, `zettabridge-dh2.patch` applied) |
| `PATCH` | `C:\Users\NacWorkstation\Documents\DH2Work\patches\zettabridge\zettabridge-dh2.patch` |
| `DOCS` | `C:\Users\NacWorkstation\Documents\DH2Work\docs` |
| `RECON` | `C:\Users\NacWorkstation\Documents\deepseek-harness\default-workspace\DH2-recon` |
| `MATH` | `C:\Users\NacWorkstation\Documents\DH_sc-pr\port\engine-math` |

**Verdict up front: no first-class mechanism exists.** There is no "call host function at guest
address X" facility in Dynarmic or in ZettaBridge. Two genuine *building blocks* exist — a
translation-time IR-injection hook that ZettaBridge does not use, and the SVC host-call channel —
and one of them must be extended by hand. A `libzbridge.so` rebuild is **mandatory** (§4).
The ABI answer is §3.

---

## 1. Does an existing mechanism exist?

**No.** Searched exhaustively; the negative result is the finding.

### 1.1 Dynarmic public interface — no address-substitution API

`DYN dynarmic/interface/A32/a32.h:20-106` is the entire public A32 JIT surface: `Run`, `Step`,
`ClearCache`, `InvalidateCacheRange`, `Reset`, `HaltExecution`, `ClearHalt`, `Regs`, `ExtRegs`,
`Cpsr`, `SetCpsr`, `Fpscr`, `SetFpscr`, `ClearExclusiveState`, `IsExecuting`, `DumpDisassembly`,
`Disassemble`. Nothing registers code or redirects execution at an address.

`DYN dynarmic/interface/A32/config.h:61-120` (`UserCallbacks`) is a *notification* interface:
`MemoryRead*/Write*`, `MemoryWriteExclusive*`, `IsReadOnlyMemory`, `InterpreterFallback`,
`CallSVC`, `ExceptionRaised`, `InstructionSynchronizationBarrierRaised`, `AddTicks`,
`GetTicksRemaining`. None substitutes a guest function.

Facilities that *look* like hooks but are not:

| Facility | Location | Why it is not a substitute hook |
| --- | --- | --- |
| `InterpreterFallback(pc, n)` | `DYN .../A32/config.h:104` | A *fallback* out of the JIT into Dynarmic's own interpreter, for instructions the JIT declined. No host code, no return-value channel. |
| `HaltReason` / `HaltExecution` / `ClearHalt` | `DYN .../A32/a32.h:59-65` | Asynchronous stop/continue of the run loop (used by `ZB core/src/signals.cpp:183` for guest signals). Not address-keyed. |
| `A32::Coprocessor` | `DYN dynarmic/interface/A32/coprocessor.h`, used at `ZB core/include/zb/cp15.h:14` | CP14/CP15 register transfers (`mrc`/`mcr`) only — not general code. |
| `ExclusiveMonitor` | `DYN dynarmic/interface/exclusive_monitor.h`, `ZB core/src/guest_thread.cpp:11` | Memory exclusives (`ldrex`/`strex`) only. |
| `hook_isb`, `hook_hint_instructions` | `DYN .../A32/config.h:207,211` | Only `ISB` and hint instructions. |
| `IR::Opcode::CallHostFunction` | emitter `DYN dynarmic/ir/ir_emitter.h:397-400`; arm64 emitter `DYN dynarmic/backend/arm64/emit_arm64.cpp:39` | Real ARM64 `bl` to a host function, but it is **internal IR**, unreachable from `Jit`, takes 0–3 `u64` args and returns **void** (`DYN .../opcodes.inc:8`). |
| `EmitCallTrampoline` family | `DYN dynarmic/backend/arm64/a32_address_space.cpp:193-221` | Private helpers that materialise calls to `UserCallbacks` methods inside generated code. Not exported, not address-keyed. |

### 1.2 The one real translation-time hook — and it is unused

`DYN dynarmic/frontend/A32/translate/translate_callbacks.h:25,29` declares:

* `PreCodeReadHook(is_thumb, pc, A32::IREmitter&)` — called before each instruction is read.
* `PreCodeTranslationHook(is_thumb, pc, A32::IREmitter&)` — called before each instruction is translated.

Call sites: `DYN .../translate/translate_arm.cpp:33,41` and `.../translate_thumb.cpp:114,124`.
Because the hook receives a live `A32::IREmitter&`, a callee *can* emit arbitrary IR at that PC —
including `CallHostFunction` — and `DYN dynarmic/frontend/A32/a32_ir_emitter.h:29-114` exposes
everything needed to read and write guest state (`GetRegister`/`SetRegister`:41,44;
`GetExtendedRegister`/`SetExtendedRegister`:42,45; `GetCpsr`/`SetCpsr`:57,58;
`ReadMemory*`/`WriteMemory*`:84-97; `CallSupervisor`:54; `ExceptionRaised`:55).

Three caveats that make this a *building block*, not a mechanism:

1. **ZettaBridge does not override it.** `ZB core/src/guest_thread.cpp:59` derives
   `GuestThread : Dynarmic::A32::UserCallbacks` and implements only `InterpreterFallback`
   (`:318`) and `ExceptionRaised` (`:336`). A grep for `PreCodeReadHook`/`PreCodeTranslationHook`
   across the whole ZettaBridge tree returns **zero** hits. The inherited defaults
   (`DYN .../A32/config.h:72,76`) are used, and `:72` returns `true`.
2. **The documented semantics are wrong.** `DYN .../A32/config.h:70-72` says "By returning true
   the callee precludes the translation of the instruction", but the caller
   (`DYN .../translate/translate_arm.cpp:33-36`) does `if (!hook(...)) { should_continue = false;
   break; }` and `translate_callbacks.h:23-24` says "By returning **false**". Trust the call site:
   **`false` ends the block, `true` continues.** A hook written to the comment would break every
   block it touched.
3. **It is per-PC IR injection, not substitution.** It cannot make an existing guest `bl` target
   native code; it only injects IR into the block whose translation visits that PC. It also fires
   for *every* instruction of the block up to the terminator, so the hook must match a narrow
   address set and be cheap.

### 1.3 ZettaBridge — the only guest↔host channel is SVC

There is no C++ facade for registering native code at a guest address. The guest→host path is:

* `svc #(0x5A0000 | index)`, `index < 0xFFFF` — `ZB core/src/process.cpp:69-70`
  (`kHostCallBase = 0x5A0000`); generators `ZB tools/gen_stubs.py:5,27`,
  `ZB tools/gen_jni.py:30`; documented `ZB CLAUDE.md:235`, `ZB docs/superpowers/specs/2026-09-13-guest-system-boundary-design.md:205`.
* Stub bodies are `svc #(0x5A0000|index) ; bx lr` — `ZB guest/stubs/gen/libGLESv2.S:9-12`.
  Reserved sub-ranges already in use: `0x5afc00+` (zbjni, `ZB guest/zbjni/gen/hostcalls.S:11+`),
  `0x5afd00` (chained host call), `0x5afe00/01/10/11`, `0x5affff` (`kHostReturnAddress`).
* The asynchronous side is `CallSVC` (`ZB core/src/guest_thread.cpp`, `DYN .../A32/config.h:107`),
  which stops the JIT so the host can inspect and edit the full register file.
* `ZB core/include/zb/process.h:52` documents an existing extension point: a
  `Process::HostCallHandler` "handles `svc #(0x5A0000 | index)` before the generated host-call
  table; returns true if it handled it". `ZB core/include/zb/library_runtime.h:42-43` reserves
  `0xFE00-0xFEFF` for host calls outside the generated range. **INFERRED:** a free sub-range such
  as `0x5af000-0x5af0ff` can carry native-substitution calls without colliding with the ranges
  above; verify against the full generated table before committing to an index.

### 1.4 The `zettabridge-dh2.patch` adds nothing here

All 13 files (`PATCH:1-321`) add runtime diagnostics (crash/stack-candidate reporting, guest log
capture, `--report`), path aliases, and one `build_guest.sh` link flag. No hook, trampoline or
substitution facility. The applied patch does not change the answer.

---

## 2. Where the cheapest correct hook should be added

Two seams are viable; both need **only ZettaBridge changes, no Dynarmic patch** (§4).

| Option | Where | Effort | Verdict |
| --- | --- | --- | --- |
| **(a) Translation-time per-address lookup** | Override `PreCodeReadHook` in `GuestThread` (`ZB core/src/guest_thread.cpp`) | ~60–120 LOC in one new file + ~5 LOC wiring; needs `<dynarmic/frontend/A32/a32_ir_emitter.h>`, reachable because ZettaBridge uses `add_subdirectory` (`ZB CMakeLists.txt:25`) and the target's include dir is the whole `src/` tree (`DYN dynarmic/CMakeLists.txt:495-496`, `BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..`) | **Recommended for the first proof of concept.** No Dynarmic fork. Costs: hook fires per instruction (match cheaply); `CallHostFunction` returns void, so results must round-trip through memory or be written back via `SetRegister` around a host call whose outputs are staged in a scratch buffer. |
| **(b) Guest entry-point stub spliced into the ARM32 binary** | Offline byte patch of `libDungeonHunter2.so` entry bytes → a new SVC index → `Process::HostCallHandler` | ~40–80 LOC host + ~1 patcher using the existing pipeline | **Recommended for shipping.** Reuses the project's proven byte-patch layer (`DOCS/ARCHITECTURE.md:115-122`, §6: in-place instruction replacement with SHA-256 and original-byte verification, e.g. `patches/engine/patch_engine.py` replacing 5 ARM instructions / 20 bytes at VA `0x56dd9c`). Stub is 8 bytes (`svc #imm` + `bx lr`) so it fits in the function's own prologue; nothing moves, no allocation, no import-table change. **UNKNOWN:** whether the ARM32 mapping is writable at runtime — the offline file patch sidesteps this. Also must handle ARM vs Thumb entry state. |
| (c) Dynarmic dispatcher | `DYN dynarmic/backend/arm64/*`, block lookup | 150–400 LOC **plus a permanent Dynarmic fork** | **Reject.** The submodule is pinned and carries two required patches (`ZB third_party/README.md:14-22`); a third patch adds a rebase liability for no capability that (a)/(b) lack. Also `third_party/dynarmic` is deliberately never staged (`ZB docs/superpowers/plans/2026-09-15-phase4d-android-integration.md:15`). |
| (d) PLT / import veneer layer | `ZB guest/stubs/gen/*.S` + generated tables | low, but wrong layer | **Reject as the primary seam.** This layer only intercepts *imports*. A hot engine function is an internal `bl` inside the engine's own `.text` and never reaches the PLT; adding it to a stub library changes symbol resolution, not those call sites. |

**Recommended split:** build (a) first — it is the smallest change that exercises the whole ABI
(§3) and needs no guest patcher. Move to (b) once the shim is proven, because it matches the
project's existing, verified patch discipline and keeps the hook out of the translation hot path.

---

## 3. The ABI problem (the shim specification)

Guest code is ARM32 **AAPCS32 softfp**; native code is **AAPCS64**. A native replacement therefore
cannot be entered directly and needs a generated shim. The facts below are already established in
this project — cite them rather than rediscovering them.

### 3.1 Established facts

* The engine imports the soft-float helper ABI (43 `__aeabi_*` undefined symbols) and emits **no**
  floating-point instructions; `.ARM.attributes` has `Tag_ABI_VFP_args = 1`, so float *arguments*
  pass in VFP registers while no VFP arithmetic exists — `DOCS/SOFTFLOAT-FINDING.md:14-25`.
* Convention: "float arguments and returns travel as 32-bit bit patterns in `r0-r3` / `s0-s15` via
  the VFP-less ABI" — `RECON/tools/dh2_oracle.py:7-8`.
* Doubles occupy **core-register pairs**: `_d()` is documented "Read a softfp double from a
  register pair starting at `r{base}`" (`RECON/tools/dh2_oracle.py:256-262`); `_one_d` = "Single
  double arg in `r0:r1`, double result in `r0:r1` (softfp)" (`:335-337`); `_two_d` = "Two doubles
  in `r0:r1` / `r2:r3`, double result in `r0:r1`" (`:346-348`); the `__aeabi_d*` table repeats it
  (`:412-424`). The oracle's `call()` takes "up to 4 integer / bit-pattern values placed in
  `r0..r3`", explicit `regs` overrides for softfp pairs, and a stack vector for "arguments that
  did not fit in `r0-r3`" (`:219-227`).
* `__aeabi_fcmp*` / `__aeabi_dcmp*` return a **1/0 integer in `r0`**, not condition flags
  (oracle `:274-285, 404-411`).
* **Struct-by-value return uses a hidden sret pointer in `r0` with `this` shifted to `r1`** (base
  AAPCS softfp), explicitly *not* the VFP-variant HFA rule; confirmed empirically — `RECON/STATUS.md:404-406`.
* The inverse direction is already implemented and tested: `struct NativeRegs { x[8]; d[8]; stack; }`
  — "the layout is shared with the assembly: x0-x7, the raw bits of d0-d7 (a float uses the low 32
  bits), then the address of the caller's stack arguments" (`ZB core/include/zb/native_call.h:11-19`);
  `GuestCall { r0-r3; stack }` (`:21-25`); `marshal_native_args` (`:30-35`);
  `store_native_result` "Stores the guest result (r0, r1) in `regs.x[0]` or `regs.d[0]`" (`:37-40`);
  the asm (`ZB core/src/jni/thunks.S:28-61`) saves x0-x7/d0-d7 and the caller's stack pointer.

### 3.2 What the generated shim must do

The shim is **per function signature**, not one generic trampoline: argument classes are not
recoverable from a raw guest address, so each hooked function needs a signature record
(cf. `NativeTarget`'s `guest_function` + `shorty`, `ZB core/include/zb/native_thunks.h:20-29`).

1. **Marshal `r0-r3` ↔ `x0-x7`.** Guest word *k* → AAPCS64 argument *k* (zero-extended), for up to
   four integer/pointer arguments. Do not assume a 1:1 reuse of `x0-x3` alone: a 5th+ guest word
   already sits on the stack.
2. **Stack arguments.** Guest word 5+ lives at `[sp]` onward in the guest frame; AAPCS64 puts
   argument 9+ at `[sp]`. Both directions need an offset-correcting copy — this is what
   `NativeRegs::stack` models on the reverse path (`ZB core/include/zb/native_call.h:11-13`).
3. **64-bit return pair `r0:r1`.** AAPCS64 returns a 64-bit value in `x0`; the shim must split it
   back to `r0 = low32`, `r1 = high32`.
4. **Hidden sret pointer.** If the native function returns a struct by value, guest `r0` is the
   destination pointer and `this` shifted to `r1` (`RECON/STATUS.md:404-406`). The shim must pass
   `x0 = sret` and shift remaining arguments up by one — *not* apply the HFA rule.
5. **Softfp single/double in core pairs.** `float` → low 32 bits of a guest word; `double` →
   `r{n}:r{n+1}` (`RECON/tools/dh2_oracle.py:256-262,335-355,412-424`). AAPCS64 uses `s0-s7`/`d0-d7`
   for arguments 1–8 by class, so the shim must *repack by class*, not by position: an `(int, double)`
   guest signature consumes `r0` then `r1:r2` while AAPCS64 uses `x0` then `d0`.
6. **Float arguments may arrive in `s0-s15`.** `Tag_ABI_VFP_args = 1`
   (`DOCS/SOFTFLOAT-FINDING.md:20-21`) means a *float* argument may be passed in a VFP register
   rather than in `r0-r3`. The oracle hedges exactly this way — "as 32-bit bit patterns in `r0-r3`
   / `s0-s15`" (`dh2_oracle.py:7-8`) — because the split is **per call site and per signature**.
   **UNKNOWN, and the single largest correctness risk:** whether a given hooked function receives
   its float arguments in core registers or in `s0-s15` cannot be assumed; it must be read off the
   function's prologue/call sites in the disassembly before writing its shim. Getting this wrong
   produces silent wrong values, not a crash.
7. **Predicate helpers return integers.** Treat `__aeabi_fcmp*`/`__aeabi_dcmp*` results as `r0 ∈ {0,1}`
   (`dh2_oracle.py:404-411`), and never as CPSR flags.
8. **Preserve the guest's callee-saved state.** The shim must leave `r4-r11`, `sp` and (unless the
   replacement intentionally changes control flow) `CPSR` exactly as the guest ABI requires, and
   must not consume or rely on the guest's condition-flag state. The native body runs on the host
   stack; the shim must not expose that stack to guest code.
9. **Write results back through the JIT's register file.** Under option (a), the emitted IR must
   `SetRegister`/`SetExtReg` the outputs, because `CallHostFunction` is void
   (`DYN dynarmic/ir/opcodes.inc:8`) — stage host outputs in a scratch buffer whose guest address
   the IR can read, then write the registers. Under option (b), the SVC handler edits
   `Jit::Regs()`/`ExtRegs()` directly (`DYN dynarmic/interface/A32/a32.h:68-71`).
10. **Keep the wrapper.** The compatibility fixes (path aliases, GLES/EGL marshalling, the byte
    patches) stay in `libzbridge.so`; only the selected body moves. Do not re-implement them natively.

### 3.3 What the 20-function math port establishes (do not repeat its mistakes)

`MATH/README.md` records the same problem already solved for 20 physical function starts:

* It fixes **explicit port types and C-linkage entry points** rather than reusing original headers
  (`MATH/README.md:5`), and pins layouts with static assertions (`:22`).
* It preserves **behavioural quirks a generic library would lose**: sequential float rounding in
  normalization, degrees not radians with double-then-float trig, original branch thresholds, and
  deliberately transposed matrix overloads (`MATH/README.md:11-24`). Reproduce these, do not "correct" them.
* It validates **bit-exactly**: "All non-NaN floats compare bit for bit; NaNs compare by
  classification because their sign/payload can vary across CPUs"; it also checks output guards,
  unmodified inputs, padding bytes and **restored stack pointers** (`MATH/README.md:57`) — all of
  which the shim touches.
* It keeps "ARM64 code and all data/stack pointers above 4 GiB" (`MATH/README.md:57`). **A shim must
  do the same**: guest pointers are 32-bit and must be zero-extended, never truncated from a host pointer.
* Open ABI defect not to inherit: `quaternion::slerp` is **unverified** — two candidate ABIs were
  tested and neither reproduced the original (`RECON/STATUS.md:409-418`). Treat any function whose
  by-value argument passing is unsettled as UNKNOWN before writing its shim.
* Trap: 16 of 19 functions' decompiler pseudocode claims "Subroutine does not return" at `__aeabi_*`
  calls (`MATH/README.md:63-65`). Write shims from **assembly**, not from decompiler output.

---

## 4. Is rebuilding `libzbridge.so` mandatory?

**Yes.** Every viable seam is C++ inside `libzbridge.so` or inside Dynarmic: the `PreCodeReadHook`
override (option a) and the SVC host-call handler (option b) are both compiled into it, and
`CallSVC` dispatch lives in `ZB core/src/process.cpp:69-70`. There is no configuration file, no
plugin ABI, and no JNI entry point that accepts a host function pointer (the DH2 patch's only JNI
addition is `addPathAlias`, `PATCH:7,106-121`). A guest-side-only change cannot help either: guest
code is ARM32 and by definition JIT-translated, so an ARM32 stub can only *ask* the host to run
native code — through a handler inside `libzbridge.so`.

What a rebuild needs, and what exists on this machine:

| Requirement | Status | Evidence |
| --- | --- | --- |
| NDK r29 | **Present**, but **windows-x86_64** | `C:\Users\NacWorkstation\Documents\DH2-toolchain\ndk\android-ndk-r29` (+ `android-ndk-r29-windows.zip`) |
| CMake | **Present** — 4.4.3 Windows (`C:\Program Files\CMake\bin\cmake.exe`); also **4.2.3 in WSL** | measured. Version minima are fine: ZettaBridge `cmake_minimum_required(VERSION 3.22)` (`ZB CMakeLists.txt:1`), Dynarmic `3.12` (dynarmic repo root `CMakeLists.txt:1`) — both ≥ 3.5, so CMake 4.x's removal of pre-3.5 compatibility does not bite |
| Ninja | **Present** — `C:\Program Files\WinGet\Links\ninja.exe`; also **1.13.2 in WSL** | measured |
| Boost | **Tarball present, NOT extracted** | `C:\Users\NacWorkstation\Documents\DH2-toolchain\boost_1_83_0.tar.gz`; `boost_1_83_0\` does not exist. Required by `DYN dynarmic/CMakeLists.txt:143` (`find_package(Boost 1.57 REQUIRED)`) |
| Dynarmic submodule | **NOT initialised** | `ZB third_party/dynarmic\` is empty; `ZB .gitmodules` declares it (`https://github.com/Vita3K/dynarmic`) |
| Dynarmic source at the pin | **Present elsewhere** | `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\dynarmic` is a full tree at HEAD `86458a0b…` with vendored `externals/` populated (fmt, mcl, oaknut, biscuit, robin-map all non-empty) |
| `third_party/patches` (both) | **Present** | `ZB third_party/patches/dynarmic-0001-thumb32-armv8.patch`, `dynarmic-0002-asimd-narrowing.patch`; must be applied per `ZB third_party/README.md:22` |
| WSL2 Ubuntu, 20 cores | **Present**, and has `git`, `python3` 3.14.4, `tar`, **plus cmake 4.2.3 and ninja 1.13.2** | measured. This **corrects the briefing**, which said WSL has no cmake/ninja |
| JDK / launcher app build | `C:\Users\NacWorkstation\Documents\DH2-toolchain\jdk` | present |

Gaps and cautions:

* **Boost must be extracted** before configuring; the tarball alone is not enough.
* The NDK is **windows-x86_64**. Building under WSL would cross a Windows/Linux toolchain boundary
  (the NDK's drivers are `.exe`); the low-risk route is a **native Windows build** with the
  Windows CMake/Ninja/NDK. **UNKNOWN:** whether the project has a documented Windows
  `libzbridge.so` build recipe — the documented tooling is bash-oriented
  (`ZB tools/build_guest.sh`, `RECON`/`MATH` scripts are `.sh`). Resolve this before scheduling a
  build; it is the most likely source of wasted days.
* Do **not** touch the ZettaBridge checkout's submodule state. Populate the submodule from the
  existing clone at the pin and apply both patches, leaving the checkout itself unmodified.
* **No Dynarmic patch is required** for options (a) or (b) — the hook and `CallHostFunction` already
  exist in the pinned tree. This is the main reason to prefer them over option (c).
* The rebuild only replaces `libzbridge.so`. The DH2 patch's launcher-side changes
  (`PATCH:1-49`) are unaffected, so existing compatibility fixes keep working.

---

## 5. Minimal proof-of-concept plan

Smallest change that routes **one** guest function to native ARM64 and proves bit-exactness.

1. **Pick the target.** Use the static proxy from `DOCS/SOFTFLOAT-FINDING.md:107-109`: rank
   functions by `__aeabi_*` call-site density per frame. Choose a *leaf* function with only
   integer/pointer arguments and a 32-bit return first — it avoids §3.2 items 3–6 entirely and
   isolates the mechanism. Do **not** start with a float or struct-returning function, or anything
   whose by-value argument passing is unsettled (`RECON/STATUS.md:409-418`).
2. **Freeze the original.** Record the function's ELF VA, size and machine-code hash — the method
   `MATH` uses (`MATH/README.md:5`, `original-functions.json`), and the guard the byte-patch layer
   already enforces (`DOCS/ARCHITECTURE.md:117-118`).
3. **Write the native body** as C/C++ compiled **into `libzbridge.so`**, with an explicit signature.
   Keep it a pure function of its arguments; do not re-enter guest code.
4. **Add the hook** (option (a) for the PoC): override `PreCodeReadHook` in
   `GuestThread : Dynarmic::A32::UserCallbacks` (`ZB core/src/guest_thread.cpp:59`), match the
   target's entry PC, and — on a match only — emit IR that reads `r0-r3`
   (`DYN .../a32_ir_emitter.h:41`), calls the native body, writes the result back (`:44`), and
   terminates the block so the JIT resumes at `lr`. Mind the inverted return convention
   (`DYN .../translate/translate_arm.cpp:33-36`): `true` continues the block. Delegate to the base
   hook for every other PC, and make the failure mode *fall through to the original translation*,
   never a trap — a wrong hook must degrade to today's behaviour.
5. **Verify bit-exactness with the existing differential harness** — disassemble → rewrite → compile
   → execute the **original** ARM32 under emulation → compare bit-exactly
   (`RECON/STATUS.md:430-432`). `RECON/tools/dh2_oracle.py` runs the pristine ARM32 in Unicorn
   (`:1-10`) and already implements the whole softfp convention and `__aeabi_*`/libm imports
   (`:286-473`), so it is the reference side; `RECON/tools/difftest.py` is the comparator
   (`RECON/STATUS.md:317-318`). Recorded scale: **27,250 comparisons, 0 mismatches**
   (`RECON/STATUS.md:11,384`); **21,477 ARM32-vs-ARM64 comparisons, 0 mismatches**
   (`MATH/README.md:55`).
6. **Compare correctly.** Bit-for-bit for non-NaN floats, NaNs by classification only
   (`MATH/README.md:57`). Compare **all 16 registers plus memory**, not just `r0` — `r0`-only
   comparison already produced false passes here (`RECON/STATUS.md:141-146`). Include edge values,
   signed zero and randomized inputs (`RECON/STATUS.md:78,384`).
7. **Then, and only then, measure.** Confirm frame cost moves toward the published budget
   (`DOCS/SOFTFLOAT-FINDING.md:81-87`) and that the JIT share (82.7%) actually falls — the aggregate
   decides whether to promote the mechanism.
8. **Promotion path.** Re-express as an option-(b) SVC stub so the hook leaves the translation hot
   path, reusing the byte-patch pipeline's verification discipline (`DOCS/ARCHITECTURE.md:115-122`).

**UNKNOWN / needs work before scaling:** (i) the per-signature VFP-argument split (§3.2 item 6);
(ii) a documented Windows arm64 `libzbridge.so` build recipe (§4); (iii) whether `PreCodeReadHook`
overhead per instruction is acceptable, or the per-PC lookup must be a page-level fast path;
(iv) the exact free SVC index range (§1.3); (v) the hooked function's call-site argument
classifications, which must come from disassembly, not from decompiler output
(`MATH/README.md:63-65`).

---

## Single biggest risk

The mechanism is cheap and the seam is real, but **the ABI is where this silently fails**:
`Tag_ABI_VFP_args = 1` with no VFP arithmetic means a float argument may arrive in `s0-s15`
*or* as a bit pattern in `r0-r3`, this split is per signature and per call site, and nothing at
runtime tells the shim which one applies. A wrong guess does not fault — it computes on garbage and
looks like a port bug. Mitigation: read the split off each function's disassembly, start the PoC
with integer-only signatures (§5.1), and require full-register-plus-memory bit-exactness against
the Unicorn oracle (risk (i) in §5).
