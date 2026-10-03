# HOST-JNI-BRIDGE.md — the synthesized `JNIEnv`/`JavaVM` bridge

**Deliverable:** working code under `DH2Work-toolchain\host\jni\`, a standalone host-testable
component: a synthesized `JNIEnv`/`JavaVM` function table that 32-bit ARM guest code can call into,
with all **238** entries present and dispatchable, real handling for the 36 natives this game
registers and the 13 slots proven used, and host-runnable tests that call through the table.

**Measured headline — every check passes on x86_64 and on armv7a (qemu-arm):**

```
########## host x86_64
jni_coverage_test: JNINativeInterface 1864 bytes (8-byte slots), 233 entries, 4 reserved(null)
jni_coverage_test: JNIInvokeInterface 64 bytes, slots GetEnv=6 AttachCurrentThread=4
jni_coverage_test: 229 of 229 JNIEnv entries present and distinct, 4 reserved
jni_coverage_test: 171 JNIEnv+JavaVM entries invoked, 231 entries present and non-NULL, 0 failures
jni_coverage_test PASS
jni_natives_test: registered 36 of 36 natives
jni_natives_test: resolved 36 of 36 native signatures
jni_natives_test: 0 failures
jni_natives_test PASS
jni_abi_test: 0 failures
jni_abi_test PASS

########## armv7a under qemu-arm
jni_coverage_test: JNINativeInterface 932 bytes (4-byte slots), 233 entries, 4 reserved(null)
jni_coverage_test: JNIInvokeInterface 32 bytes, slots GetEnv=6 AttachCurrentThread=4
jni_coverage_test: 229 of 229 JNIEnv entries present and distinct, 4 reserved
jni_coverage_test: 171 JNIEnv+JavaVM entries invoked, 231 entries present and non-NULL, 0 failures
jni_coverage_test PASS
jni_natives_test: registered 36 of 36 natives
jni_natives_test: resolved 36 of 36 native signatures
jni_natives_test: 0 failures
jni_natives_test PASS
jni_abi_test: 0 failures
jni_abi_test PASS
```

Reproduce: `sh tools/record-results.sh` (both architectures), `sh tools/run-host-tests.sh`
(x86_64 only), `sh tools/build-arm32.sh` (rebuilds and runs the armv7a tests under qemu-arm).

**These are off-device results.** No device was touched, nothing was installed or launched. See
"Unverified without a device" for what that leaves open.

---

## 1. The 238-entry count, made exact

The task and `HOST-IMPORT-SURFACE.md` §4.3/§7 say "233 JNIEnv slots + 5 JavaVM functions = 238".
Both are ABI **slot** counts, and the two interfaces contain reserved members:

| interface | slots | functions | reserved |
| --- | --- | --- | --- |
| `JNINativeInterface` | **233** (0–232) | **229** | 4 (`reserved0..3`) |
| `JNIInvokeInterface` | **8** (0–7) | **5** | 3 (`reserved0..2`) |
| total | 241 | **234** | 7 |

The task's "238" is 233 + 5: the 233 JNIEnv slots plus the 5 JavaVM *functions*. This bridge
covers **all 241 slot indices** (every one dispatches) and all **234** callable entries, so the
238 is covered either way. The reserved slots are `NULL` in the table exactly as the platform
header lays them out — `check_table()` asserts that, and asserts the 229 + 5 entries are non-NULL
and pairwise **distinct** (a duplicated pointer would mean two slots share one operation).

The slot order is not typed by hand. `tools/gen_slots.py` parses `struct JNINativeInterface` and
`struct JNIInvokeInterface` out of the NDK r29 `jni.h` the guest was built against and generates
the whole surface; the generated table is then designator-initialised against that same header, so
**every entry's signature is a compile-time check** under `-Werror`. A jni.h bump that changed the
order or a type would fail the build, not the runtime.

## 2. What was adapted, and from where

ZettaBridge source read and adapted (`DH2Work-stage\…\ZettaBridge\`). Line numbers are into that
tree. Its licence notice ships as `LICENSE-ZettaBridge.txt`.

| this component | adapted from | what was kept |
| --- | --- | --- |
| `guest/zbjni/gen/tables.inc` → generated `src/gen/jni_guest_interface.inc` | `guest/zbjni/gen/tables.inc` (248 lines) | the full `JNINativeInterface` + `JNIInvokeInterface` designator table, including the reused-slot behaviour |
| `src/jni_guest_manual.c` | `guest/zbjni/zbjni.c:289-430` (`zbjni_call_v`, `ZBJNI_CALLS`) | the shorty-driven va_list → `jvalue[]` marshalling, and the reason it exists |
| shorty cache, `src/jni_bridge.c` | `guest/zbjni/zbjni.c:34-86` | the invariant that guest method ids are dense from 1 (ZettaBridge's page table is `id >> 10`) |
| the host-call switch → generated `src/gen/jni_host_switch.c` | `core/src/jni/host_jni.cpp:134-417` | one flat dispatch per slot, the recent-call ring buffer (`crash-recent-jni-calls`), the `Register` handshake |
| `include/jni_backend.h` | `core/include/zb/jni_backend.h` (`JniBackend`) | one backend call per JNI operation; opaque refs/ids; failures leave a pending exception |
| `include/jni_abi.h` word array | `core/src/jni/host_jni.cpp:152-163` (`JniCall::arg`), `core/src/jni/native_call.cpp` | the argument-word model that both directions share |
| `src/jni_host_ops.c` (229 bodies) | `core/src/jni/host_jni.cpp`, `host_jni_objects.cpp`, `host_jni_values.cpp`, `host_jni_data.cpp`, `host_jni_natives.cpp`, `host_jni_vm.cpp` | the operations themselves: ids, calls, fields, strings, arrays, references, natives, buffers |
| `src/jni_host_ops.c` buffer header | `guest/zbjni/zbjni.c:462-518` | the magic-in-front `Get*Elements` copy so a mismatched `Release` is caught, not corrupting |
| native slot table (`zjho_native_slot`) | `core/src/jni/native_thunks.cpp:43-84` (`NativeSlots`) | low allocation with released-slot reuse from the back |
| `tests/jni_native_dispatch.c` (test stand-in) | `core/src/jni/thunks.S`, `host_jni.cpp:419-476` (`call_native`) | the Java → guest direction as one decode-by-short signature dispatcher |
| `tests/jni_art_model.c` | `tests/host/mock_jvm.cpp` (1110 lines) + `mock_jvm_test.cpp:87-97` | the discipline (pending exceptions, opaque ids, modified UTF-8) and the UTF-8 byte assertions |
| `tests/jni_abi_test.cpp` word layout | `tests/host/jni_abi_test.cpp:52-130` | the same layout cases, read guest → host instead of host → guest |

Total: **8 417 lines** of bridge, generated tables and generators
(`tools/record-results.sh` prints the per-file breakdown; 4 585 of those lines are generated).

### What is deliberately different from ZettaBridge, and why

1. **The transport is a word array, not `svc`.** ZettaBridge's guest calls the host with
   `svc #(0x5AFC00 | index)` and the host reads the guest's r0–r3 and stack out of guest memory.
   The bridge here takes a `zjg_word*` and `zjg_word*` pair. That is what lets the *same source*
   be built and run on a Linux x86_64 host and on armv7a, which is the whole point of this
   component being testable at all. The trap/register binding is a thin adapter on either end and
   is not this component's business.
2. **A "word" is one machine word** (`include/jni_word.h`). On arm32 that is 4 bytes and a
   `jlong`/`jdouble` occupies two, low word first, exactly as AAPCS32 passes it. On x86_64 it is 8
   bytes and a wide value occupies one. Getting this wrong is the single most productive source of
   bugs in this component — five separate defects during bring-up were one truncation or another —
   so it is one macro, `ZJG_WIDE_WORDS`, and one pair of accessors.
3. **The backend is a C table, not a C++ interface**, because the guest side of this bridge is
   arm32 C and the tests must run without a C++ runtime on the guest.
4. **The environment identity is `uintptr_t`, not `uint32_t`.** It is a 32-bit guest address on
   arm32 and a 64-bit pointer on the host that runs the same tests.
5. **The bridge owns no handle tables.** ZettaBridge interns guest handles per thread
   (`core/src/jni/handles.*`) because its guest and host are separate address spaces; here a
   reference is the backend's own opaque `zjh_ref`, passed through.

## 3. The 36 natives

The list is **not typed into a test**: `tools/gen_natives.py` reads `Java_*` out of the `.dynsym`
of the shipped `libDungeonHunter2.so` and `libnativeinterface.so` and generates
`src/gen/native_symbols.inc`. It reports **36** — 32 + 4 — matching
`zb-runtime-report.txt`'s `registered-natives: 36`.

`jni_natives_test` then, for each of the 36:

1. registers it through slot 215 (`RegisterNatives`) exactly as the engine's `JNI_OnLoad` does;
2. asserts the bridge interned one slot per native and that each slot's stored guest address and
   signature are the ones registered (`zjh_native_guest_fn`, `zjh_native_signature`);
3. resolves it back through slot 113 (`GetStaticMethodID`) or slot 33 (`GetMethodID`) — all 36
   resolve — and asserts the shorty the bridge derived from the descriptor;
4. asserts an unregistered name (`noSuchNative`) does **not** resolve, because a bridge that
   answered every lookup would make a missing native undiagnosable.

Measured: `registered 36 of 36 natives` / `resolved 36 of 36 native signatures` / `PASS`.

**Their Java signatures are not knowable from these binaries.** A `Java_...` symbol gives the class
and method; the parameter list lives in the DEX and reaches the host at `RegisterNatives` time.
`tests/native_signatures.h` therefore records each of the 36 with a `recovery` flag: **1 = a
signature string measured in `.rodata` or at a resolved call site** (`HOST-IMPORT-SURFACE.md`
§4.2), **0 = assumed**. 23 of the 36 are measured; 13 are assumed and marked individually
(`nativeAccelerometer`, `nativeSetOrientation`, `nativeSetStopOnMusic`, `GLResLoader.nativeInit`,
`nativeOnTouch`, `GameRenderer.nativeConfig`, `nativeGetJNIEnv`, `GameRenderer.nativeInit`,
`storeLicenseKey`, and three more). A wrong assumed signature would be a wrong *test model*, not a
bridge defect — and it cannot be resolved without the game's DEX.

## 4. The 13 slots proven used

`HOST-IMPORT-SURFACE.md` §4.3 lists them. All 13 are implemented and exercised with real values
in `jni_natives_test`: 6 `FindClass`, 21 `NewGlobalRef`, 29 `NewObjectV`, 33 `GetMethodID`,
35 `CallObjectMethodV`, 80 `CallNonvirtualIntMethodV`, 92 `CallNonvirtualVoidMethodV`,
113 `GetStaticMethodID`, 115 `CallStaticObjectMethodV`, 118 `CallStaticBooleanMethodV`,
130 `CallStaticIntMethodV`, 142 `CallStaticVoidMethodV`, 169 `GetStringUTFChars`.

The slot indices are asserted as literals (`ZJG_JNIENV_GetMethodID == 33`,
`ZJG_JNIENV_GetStringUTFChars == 169`, …) so a jni.h reordering could not silently move them.

`GetStringUTFChars` (169) is the one the device ring buffer corroborates, and it is the one where
the bridge had a real defect worth recording: **JNI's `start`/`len` arguments count UTF-16
characters, not modified-UTF-8 bytes.** The bridge was passing the byte count, so for `"a\0b"`
(3 characters, 4 bytes) it asked the backend for 4 of 3 characters and the backend correctly
refused, leaving `GetStringUTFChars` returning NULL. The byte count sizes the buffer; the
character count is what the region call takes.

## 5. Tests, and what each one establishes

| test | establishes |
| --- | --- |
| `tests/jni_coverage_test.cpp` | all 233 + 8 slot indices present; 229 + 5 entries non-NULL and pairwise distinct; the reserved slots `NULL`; `sizeof(JNINativeInterface)` == slots × word and `offsetof(...,GetVersion)` == 4 words; **171 of the entries invoked through jni.h's own typed `_JNIEnv`/`_JavaVM` accessors**, each asserted to reach its *own* operation via a per-slot census |
| `tests/jni_natives_test.cpp` | the 36-native gate (§3), the 13 slots with real data (§4), the AudioTrack binding (`<init>(IIIIII)V`, `write([BII)I`, `play`, `pause`, `stop`, `release`), modified-UTF-8 round trip including an embedded NUL, exceptions and monitors, and that every slot has a name |
| `tests/jni_abi_test.cpp` | the ABI boundary for the argument and return types this game uses: `(IFFIFF)I`, `(J)V`, `(I)J`, `(I)D`, `(FI)F`, `(Ljava/lang/String;IIIIII)V`, `(IIII)I`, `()V`, `()Ljava/lang/String;` — each asserted word by word *inside the guest function*, so the compiler's own prologue and epilogue do the checking |

Why the coverage test is C++: jni.h's `_JNIEnv`/`_JavaVM` wrappers give every slot a statically
typed accessor, so each entry is called through the exact prototype its slot is declared with —
including `jlong` vs `jdouble` and the variadic and `va_list` forms. Calling through
`void(*)()` casts would compile and prove nothing.

**Why only 171 of 234 are invoked, and where the other 63 are covered.** 31 entries are the
C-variadic forms (`NewObject`, `Call*Method`). jni.h's accessor for each builds its own `va_list`
and forwards it, and a `va_list` is not constructible portably by a test, so those accessors are
not called *on the host*. They are not untested: the `va_list` entry next to each one
(`NewObjectV`, `Call*MethodV`, 32 entries) is a separate slot and is reached — the coverage test
invokes all 32 `...V` entries, and `jni_abi_test` and `jni_natives_test` drive the V path for real
through the guest-side wrappers in `src/jni_guest_manual.c` and the `...A` forms with explicit
`jvalue` arrays. The reserved slots are `NULL` by definition.

The variadic accessors themselves are additionally exercised **on arm32**, where they work:
`jni_abi_test` asserts `CallStaticIntMethod`, `CallStaticLongMethod` and a six-argument
`CallStaticIntMethod` against the same word-by-word records, and the run's call census shows
`CallStaticIntMethodV`, `CallStaticLongMethodV` and `CallStaticVoidMethodV` being reached. On
x86_64 the same block is compiled out — see §7.3 for the measurement behind that.

## 6. Left out deliberately

Per `PORTING-POLICY.md`, breadth that exists for other games was refused:

| refused | why |
| --- | --- |
| ZettaBridge's reflection-discovery loader (`core/src/jni/loader.cpp`, `find_declared_natives`, `mangle.cpp`, `shorty.cpp`, `descriptor.cpp`) | it exists to bind native exports by enumerating a class's declared methods through ART reflection. This host registers the 36 by name; the four reflection *slots* (7, 8, 9, 14) are implemented against the backend and return the JNI failure result when a backend declines, which is what ART does for a class it cannot resolve |
| `core/android/jni_env_backend.cpp` (the real ART-facing `JniBackend`) | it is the `JNIEnv*` wrapper for the Android-build host; here the seam is `zjh_backend` and the real implementation belongs to whichever host binds Dynarmic. `tests/jni_art_model.c` is the stand-in |
| guest handle interning (`core/src/jni/handles.*`) | needs two address spaces; here a reference is the backend's opaque `zjh_ref` |
| `host_jni_data.cpp`'s direct-buffer mirroring | it exists because ZettaBridge's guest cannot see Java-allocated memory outside its own reservation. No native in this game's 36 takes a direct buffer, and nothing in `HOST-IMPORT-SURFACE.md` imports `NewDirectByteBuffer`. The four buffer slots are implemented for a backend that has them; the mirror is not |
| the multi-thread `JniThread`/carrier machinery (`host_jni.cpp:141-296`) | it exists to lease a guest carrier per Android thread. This bridge is one address space and documented single-threaded; `ZJG_SHORTY_LOCK` would be the one-line change |

Also not copied: no ZettaBridge file was copied verbatim, so no ZettaBridge file notice needed to
travel; the adaptation is recorded by the `Adapted from` note at the head of each file. The
PolyForm Noncommercial + Perimeter notice is reproduced in `LICENSE-ZettaBridge.txt` because the
work is derived from it.

## 7. Not modelled, and unverified without a device

Honest list. Each item states what *is* implemented and what could not be checked.

1. **The full 233-slot set beyond the 13 proven.** All 233 are implemented and all 229 callable
   ones dispatch to their own operation, but `HOST-IMPORT-SURFACE.md` §7 is explicit that only 13
   are *proven permitted* and only two (`NewGlobalRef`, `GetMethodID`) are *confirmed exercised*
   on hardware. **Which of the remaining ~220 the engine actually touches is unverified and can
   only be closed on a device** with the slot census this bridge already exposes
   (`zjh_slot_hits`) — the numbers are there to be read; the run has not happened.
2. **`JavaVM` slots were never observed at all.** §7 records the static scan finding 0. All 5
   functions are implemented (`GetEnv` at index 6, `AttachCurrentThread` 4, and the 3 others), and
   `jni_coverage_test` calls every one, but **that `JNI_OnLoad`'s `GetEnv` path executes on
   hardware is inference from `jni-runtime-start: ok`, not a measurement.**
3. **The variadic (non-V) accessors: measured, asymmetric, and one case still open.** These were
   the deepest rabbit hole of the bring-up, so the result is stated as measured rather than
   inferred.
   * **armv7a — they work.** `jni_abi_test` asserts `CallStaticIntMethod` with four ints,
     `CallStaticLongMethod` with a `jlong`, and a six-argument `CallStaticIntMethod` against the
     same word-by-word guest records the A-form cases assert, and the run's call census shows
     `CallStaticIntMethodV`, `CallStaticLongMethodV`, `CallStaticVoidMethodV` being reached. A
     `va_list` on arm32 is a plain pointer into the caller's register save area, so it survives
     being carried in one word and re-copied.
   * **x86_64 — they fault inside `va_arg`.** A `va_list` there is a 24-byte descriptor whose
     register-save-area fields are only populated by `va_start`; copied into a word and
     re-`va_copy`'d, it is not walkable. The block is compiled out on the host (`#ifdef __arm__`
     in `tests/jni_abi_test.cpp`) and the rest of the suite uses the `...A` and `...V` forms. This
     is a property of the *host* build, not of the arm32 guest the bridge is for.
   * **Still open: a variadic `double` argument** (`CallStaticDoubleMethod(cls, id, 3.0)`). On
     arm32 it returns the wrong value; on x86_64 it is unreachable. It is asserted on neither, and
     it is the one case in this component that is implemented and known-wrong rather than merely
     unverified. Everything else about the double path *is* verified on both builds, through the
     A form and through the `...V` form (`(D)D` in `jni_natives_test`, `(I)D` in `jni_abi_test`) —
     including the `zjh_bits_d` defect that only the arm32 build exposed, where a 32-bit
     `zjg_word` truncated the double's bit pattern before `zjh_out64` split it into `r0:r1`.
4. **No real ART.** The backend exercised here is a 967-line stand-in modelling only this game's
   Java surface (one `AudioTrack` class, the ProGuard-renamed statics, `Context`, `Activity`,
   `String`, the two reflection classes). Class loading, the verifier, the GC, JNI reference
   tables, `java.lang.ref` semantics and the exception hierarchy are **not** modelled. Wiring the
   real `JNIEnv*` is the seam `zjh_backend`; that work is not done and is not in this component.
5. **`GetObjectRefType`** returns `JNIInvalidRefType` unconditionally. The backend keeps no
   reference table, so "the JVM cannot say" is the only honest answer; ART reports the same for a
   reference it did not issue.
6. **Threading.** Single-threaded by construction. `ZettaBridge`'s per-thread state
   (`JniThread`, carriers, the lock-free shorty page table) is not reproduced. A guest that calls
   these slots from two threads at once is **unverified and expected to be unsafe**.
7. **The trap adapter.** `zjg_dispatch` is a C call here. Binding it to Dynarmic's `svc` handler /
   `JniCall::arg`-equivalent register extraction is the host integration step and is outside this
   component.
8. **Java signatures of 13 of the 36 natives** (§3) — assumed, marked, unresolved without the DEX.
9. **The PC/offset rule from the standing status.** `libDungeonHunter2.so` is `ET_DYN` mapped at
   `0xfc880000`, so a file offset is not a runtime address (`0x60dd34` executes at `0xFCE8DD34`).
   Nothing in this component is address-keyed — it registers 36 natives by *name* and calls guest
   addresses the runtime hands it — so this rule does not bite here; noting it because a
   native-address lookup added later would have to resolve `base + offset`.

## 8. Files

```
host/jni/
  CMakeLists.txt              host x86_64 and armv7a builds, one source set
  include/  jni_word.h jni_abi.h jni_backend.h jni_bridge.h
  src/      jni_bridge.c jni_host_ops.c jni_guest_manual.c
            jni_bridge_internal.h jni_host_internal.h jni_guest_internal.h
  src/gen/  generated: jni_slots.h jni_slot_names.h jni_guest_entries.c
            jni_guest_interface.inc jni_host_switch.c jni_host_ops.h jni_host_ops.c
            native_symbols.inc
  tests/    jni_coverage_test.cpp jni_natives_test.cpp jni_abi_test.cpp
            jni_art_model.c jni_native_dispatch.c native_signatures.h
  tools/    gen_slots.py gen_natives.py run-host-tests.sh build-arm32.sh
            record-results.sh compile_check.sh
  jni_include/jni.h           the NDK r29 platform header the tables are generated from
  LICENSE-ZettaBridge.txt     the notice for the adapted material
```

Regenerate the tables after any `jni.h` change:

```
python tools/gen_slots.py <path-to-jni.h> src/gen
python tools/gen_natives.py <engine.so> <libnativeinterface.so> > src/gen/native_symbols.inc
```
