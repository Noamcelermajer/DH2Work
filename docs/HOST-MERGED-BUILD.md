# HOST-MERGED-BUILD.md — the three surviving optimisations in one build, verified off-device

**Scope.** Merge the three surviving Track-B changes for Dungeon Hunter 2 HD v1.0.2
(`armeabi-v7a` guest, Dynarmic AArch32→AArch64 JIT on an arm64 Galaxy Z Fold7) into one tree and
one APK, and prove off-device that it builds and that what is packaged is what was verified. **No
device work was done here** — per `docs/TEST-ENVIRONMENT-POLICY.md` the phone belongs to the Lead,
and per the current addendum it is assigned to the VFP agent.

**Result in one line.** One tree (`host/integrate/ZettaBridge`, = the shrunk fork as of
2026-10-03 22:08 + one warning-suppression edit) and one signed APK at versionCode **1002**
(`host/integrate/out/dh2-merged1002.apk`); the VFP differential harness re-run **on the bytes
inside that APK** reports **18,353,231,360 comparisons / 0 mismatches**, the host library builds
with **0 warnings from this tree's own sources**, the packaged bundle token is the patched
`2dd2f3eb…` (not the pre-patch `9ae1e3d9…`), the hook is keyed on **library basename + file
offset** (checked by an executed test), and **no display-mode / `Surface.setFrameRate` code exists
anywhere in the tree or the APK**.

---

## 0. Artefacts

Everything below lives under `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\integrate\`
unless stated otherwise. That directory is the only place this work writes, plus this document.

| artefact | path | sha256 | size |
|---|---|---|---|
| merged host source tree | `ZettaBridge/` | base snapshot aggregate `804aec63…` + 1 file (see §2.1) | 337 files |
| built host library | `build/libzbridge.so` | `5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd` | 3,015,352 B |
| patched guest libc | `vfp/libc.patched.so` | `d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8` | 992,808 B |
| pristine guest libc (input) | `vfp/libc.pristine.so` | `23ee728839ebb17bae3e9fc73034142be2466a6d694c0404ef5aeafdb1009069` | 992,808 B |
| **signed APK** | `out/dh2-merged1002.apk` | `b4b38dd8de701821003f21bc138057e5079b536615c6d762a32ef2cac1fe1c23` | 13,654,554 B |
| host build log (clean) | `out/host-build-final.log` | — | 117 warnings, all vendored `fmt` |
| APK build log | `out/apk-build.log` | — | — |
| VFP sweep result | `out/vfp-sweep-final.txt` | byte-identical to the shipped `results-ship-final.txt` | — |
| APK payload verification | `out/verify.json` | 12/12 checks true, `problems: []` | — |
| hook key test output | `out/hook-key-test.txt` | PASSED, 0 failures | — |

Rebuilds of the APK are not bit-identical: the toolchain writes fresh ZIP entry timestamps, so
this session produced three same-size APKs with three different sha256 (`8987589f…`, `5f49b2f4…`,
`b4b38dd8…`). The *payload* is what identifies the build, and that is what §4 checks.

---

## 1. What merged, and where each piece lives

The base was `host\shrink\ZettaBridge` (the shrunk fork), copied to `host\integrate\ZettaBridge`.
File lists and content were compared before and after; the merged tree is the base **plus one
edited file** (§2.1).

### 1.1 VFP guest-libc patch (priority 1)

This change is **guest-side**, not host-side: it rewrites bytes inside the runtime bundle's
`assets/zb/sysroot/system/lib/libc.so`, so it lands in the *staging* tree, not in the host source
tree. The patcher is `vfp/patch_libc.py`, the replacement bodies are the assembler in
`vfp/vfp-test/vfp.S` (assembled `vfp.o`, sha256 `9f8f63681c16b9643785ddebc7794737e1e24470108ca32326bd1a777d74e06e`,
identical to the shipped artefact), and the patched output is `vfp/libc.patched.so`.

Re-running the patcher from the pristine input reproduces the shipped patch exactly: 20 symbols,
**742 bytes changed of 992,808 (0.075 %)**, pages 157–160, `1332 → 215` instructions, out sha256
`d34addce…` — the same number the VFP workstream recorded.

The hard-won constraints are preserved **because the patcher is the one that encodes them**, and
this build did not re-derive them:

* `f2d`, `f2iz`, `f2uiz`, `d2iz`, `d2uiz`, `d2f`, `i2d`, `ui2d` are in the patcher's `EXCLUDED`
  set (each with its measured divergence), and the patcher **asserts** that no symbol is on both
  lists;
* `fsub`/`dsub` are not patched: they are 8-byte `eor; b __adds*3/__addd*3` aliases, and the
  shared **add** bodies are what got patched, so subtraction is accelerated for free and keeps the
  "negate `b` first" NaN rule;
* the compares use `MI`/`LS` (not `LT`/`LE`) — `vfp.S` is the post-fix source, and this is the
  behaviour the 0-mismatch sweep re-confirms below;
* `assets/zb-version.txt` **is regenerated from the packaged bytes** (§2.3), which is the one thing
  that stops `RuntimeBundle.java:19-31` from returning early and shipping the pristine library.

### 1.2 Native ARM64 hook (`PreCodeReadHook` + `CallHostFunction`)

Present in the fork and now in the merged tree:

```
core/CMakeLists.txt:36-39,67   src/native_hook.cpp  src/native_hook_a64.S
                               src/native_hook_ir.cpp  src/native_hook_reference.cpp
                               (-ffp-contract=off on the reference)
core/src/guest_thread.cpp:327  bool GuestThread::PreCodeReadHook(bool, VAddr pc, IREmitter& ir) {
core/src/guest_thread.cpp:328      return !native_hook_emit(*this, is_thumb, pc, ir);
core/src/process.cpp:204           native_hook_note_mapping(start, length, offset, path);
core/src/process.cpp:217           native_hook_forget_mapping(start, length);
core/src/native_hook.cpp:176       {"quat-mul", "libDungeonHunter2.so", 0x0060dd34u, &zb_native_hook_quat_mul},
core/src/native_hook.cpp:195       const std::string name = base_name(path);
core/src/native_hook.cpp:199       const std::uint32_t pc = native_hook_resolve_pc(target.file_offset, start, length, offset);
```

**Keying.** The target is `<basename, file offset>`; the runtime PC is computed when the guest's
linker maps the library and is cleared when the range goes away. No Dynarmic patch is involved.
`out/hook-key-test.txt` runs the **real** resolver (`core/src/native_hook_ir.cpp`, the function
`native_hook.cpp:199` calls) on x86_64 off-device, with the engine's real PT_LOAD read out of the
shipped `libDungeonHunter2.so` (`p_offset = p_vaddr = 0`, `p_filesz = 0x955130`, ET_DYN):

```
device mapping start 0xfc880000 -> the real entry PC       got 0xfce8dd34 want 0xfce8dd34  ok
a second mapping start 0x12345000 follows the mapping      got 0x12952d34 want 0x12952d34  ok
runtime address used as a file offset does not match       got 0x00000000 want 0x00000000  ok
file offset past the end of the mapping does not match     got 0x00000000 want 0x00000000  ok
file offset below the mapping's file position does not match got 0x00000000 want 0x00000000  ok
a later PT_LOAD (nonzero p_offset) subtracts the file position got 0xfc881000 want 0xfc881000  ok
the first byte of the mapping resolves to its start        got 0xfc880000 want 0xfc880000  ok

HOOK KEY TEST PASSED (0 failures)
```

The third line is the failure mode the constraint warns about: a hook keyed on the *file VA*
(`0x0060dd34`) or on a fixed runtime address (`0xFCE8DD34`) resolves to 0 inside the real mapping
and would silently never fire. The first line is the address that target really executes at.

### 1.3 CPU affinity + priority (affinity/priority part only)

`core/src/thread_perf.{h,cpp}` is present and **live**: `core/src/gl/host_gl.cpp:91` calls
`note_gl_host_call()` on every GL host call (one thread-local counter, one property read per 4096
calls). It reads the device topology from sysfs, pins the *calling* thread — the one that executes
`gl*`, i.e. the guest's GLThread — to the fastest CPU the app is allowed on, and renices it
(`debug.zb.perf.pin` defaults to on, `debug.zb.perf.nice` to −8). Every kernel call is checked and
a refusal is recorded, not fatal.

**The display-mode and `Surface.setFrameRate` code is not merged, and is not present anywhere.**

```
$ grep -rn "setFrameRate\|preferredDisplayModeId\|preferredRefreshRate\|peakRefreshRate\|
            setFrameRateCategory\|Display.getRefreshRate"  <tree>  (excluding third_party)
NONE - no display-mode / Surface.setFrameRate code in the merged tree
```

The same five needles are searched as raw bytes in every entry of the signed APK (host payload and
guest payload separately) by `scripts/verify_apk.py`: **0 hits everywhere**
(`out/verify.json` → `display_mode_hits_host_payload`, `display_mode_hits_guest_payload`).

### 1.4 GL state shadowing — present as code, off

`core/src/gl/gl_shadow.cpp` is compiled (it also carries the call histogram the runtime report
prints), but the drop path is **flag-gated and off**:

```
core/src/gl/gl_shadow.cpp:178   std::atomic<bool> g_drop_armed{false};
core/src/gl/gl_shadow.cpp:191   "%s/zb-gl-shadow-enable"   (read from the runtime-report directory)
core/src/gl/gl_shadow.cpp:217   set_drop_armed(read_arm_flag());
core/src/gl/gl_shadow.cpp:346   if (!g_drop_armed.load(...)) { ...measure only, forward... return true; }
```

The gate can only be armed by a file containing `1` in the app's own report directory; nothing in
the packaged payload creates it. `verify_apk.py` asserts the gate filename is present (so the drop
path is provably the gated one) — `gl_shadow_gate_string_hits: 1`. The superseded
`core/android/gl_shadow_gate.cpp` is deliberately **not** in `core/CMakeLists.txt` (its own header
says so). A device run should show `gl-shadow-drop-path: disarmed`.

---

## 2. What conflicted, and how each conflict was resolved

### 2.1 The base fork moved under the merge (real, and it is still moving)

The copy was taken at 21:59. By 22:08 three files differed that I had not touched:
`core/include/zb/native_hook_ir.h`, `core/src/native_hook_ir.cpp`, `core/src/native_hook.cpp`.
The native-hook workstream had refactored the target→PC arithmetic out of `native_hook.cpp` into a
new `native_hook_resolve_pc()` in `native_hook_ir.cpp`, explicitly so a host test can run the real
resolver. That is *better* for verification, not worse, so it was merged rather than ignored:
re-copied the three files, rebuilt, re-verified.

**Resolution, and the snapshot it defines.** The merged tree equals
`host\shrink\ZettaBridge` as of **2026-10-03 22:08:09**, aggregate identity
`804aec631fb2397030ac80dca8b8187c39c634c877ff4cd9112c855952da8302` (sha256 over the sorted
`relative-path sha256(file)` lines of all 337 files), **plus one edited file** (§2.2). The fork is
being edited concurrently by other agents, so this is a documented point-in-time snapshot, not a
moving target. The only file that differs from that snapshot is `core/src/gl/gl_shadow.cpp`.

### 2.2 One own-source warning vs "clean build with no warnings"

The clean build produced **117 `warning:` lines, 116 of them from the vendored dependency**
(`third_party/dynarmic/externals/fmt/include/fmt/format.h:4429`,
`-Wdeprecated-literal-operator` — a pre-existing upstream artefact, untouched by this merge, and
the only warning kind present). Exactly **one** came from this tree's own code:

```
core/src/gl/gl_shadow.cpp:73:5: warning: array designators are a C99 extension [-Wc99-designator]
   73 |     [ZB_GL_HC_glActiveTexture] = Policy::Drop,
```

`[INDEX] = value` on an array is C99; C++ only allows it out of order as an extension. Resolved by
replacing the designated-initializer table with an equivalent `constexpr` fill
(`make_policy_table()`), which is **spelling only**:

* the enum's first enumerator is `Drop`, so unlisted entries were *already* `Drop`; the fill loop
  sets exactly that, and every listed entry keeps its listed value. (The old comment above the
  table claimed unlisted entries were `Invalidate`. They were not. That latent discrepancy is
  documented in place and **left exactly as it was** — this module's drop path is disabled and the
  merge changes no behaviour.)
* Verified, not assumed: a `static_assert` probe compiled with the same clang 21 the library is
  built with confirms both the unlisted default and the listed values; the probe also reproduces
  the `-Wc99-designator` warning that the edit removes.
* Strongest evidence: the library built from the edited tree is **byte-identical** to the library
  built from the unedited tree — `b6bfe0e4a429665c252adffc096a32a4c71e3cd73431d0fa248c8f9603c2510d`
  / 3,015,240 B both times. (That hash changed to `5c769c9d…` / 3,015,352 B only when §2.1's hook
  refactor was merged, which is a code change, not this edit.)

After the edit and the §2.1 merge, the final clean build is **117 warnings, 0 from this tree's own
sources, 0 errors** (`out/host-build-final.log`).

The APK-side build adds two notes that are **not** from this merge and appear in the builder's own
historical logs (`fold7-build\build.log`, `build-test3/4/5.log`):
`warning: [options] bootstrap class path not set in conjunction with -source 8` and
`MediaQueries.java uses or overrides a deprecated API` (javac). `zipalign` also prints
`W zip : WARNING: header mismatch` (24 lines) while reading the archive it is aligning; the
post-alignment check the builder runs itself (`zipalign -c -P 16 4 <apk>`) exits 0 with **no
output**, and `apksigner verify` passes v3. Re-aligning the finished APK prints nothing, so this is
the aligner fixing up the entries the builder appends with Python's `zipfile` after `aapt2 link`,
not a defect in the merged content.

### 2.3 The VFP patch is invisible to `build_apk.py` (the trap the task named)

`build_apk.py` copies `research/ZettaBridge/build/launcher/assets` into the APK and regenerates
`zb-files.txt` / `zb-version.txt` **only on the pinned-guest path** (`build_apk.py:103-121`). This
build uses the locally rebuilt guest (`guest_input = None`), so those two assets would have been
carried over stale — and a stale `zb-version.txt` makes `RuntimeBundle.java:19-31` skip extraction
and ship the pristine libc.

**Resolution:** `scripts/prepare_bundle.py` installs the patched libc and regenerates both files
from the bundle contents using exactly `build_apk.py`'s algorithm (sorted `zb/**` names, NUL,
contents → one SHA-256). Result on the packaged bytes:

| | value |
|---|---|
| token in the bundled assets (**my** stage) | `2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65` |
| token in the shared staging tree (pre-patch) | `9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06` |
| token recomputed from the signed APK's own bytes | `2dd2f3eb…` (**matches**) |

The patched token independently equals the one the VFP workstream recorded for its own patched
bundle (`SOFTFLOAT-VFP-SHIP.md` §4), so the identity of the bundle is confirmed by two independent
routes.

### 2.4 The shared staging tree is in concurrent use

`DH2Work-stage\compatibility\work` is being built in by another agent while this merge ran: at
22:04:47 its `fold7-build/build-result.json` named a *different* product,
`host\perf\apk\dh2-perf-hw1102.apk` (13,654,554 B, sha256 `b0cd1f06…`), and earlier in the session
the shared library slot held a 2,981,360-byte library rather than the shipped 3,306,992-byte one.

**Resolution:** never write into the shared tree. The APK was built by the *same unmodified*
builder (`build_apk.py` sha256 `EC3AF835A83969423769D2246BEE479B2E36DC52DFB24A1C81B0A8361EEDAE71`,
byte-identical to the shared copy) executed from a private copy of the minimal staging tree at
`host/integrate/stage`. The shared slot was **not touched**, and at the end of this work it is
verified to be the shipped library:

```
research/ZettaBridge/build/launcher/jniLibs/arm64-v8a/libzbridge.so
  25E8DA7EDBCBA1D4CFE828E506A03F1E6DD6E7FFA2948A91EB2CA6F300682D24   3,306,992 B
research/ZettaBridge/build/launcher/assets/zb/sysroot/system/lib/libc.so
  23EE728839EBB17BAE3E9FC73034142BE2466A6D694C0404EF5AEAFDB1009069  (pristine)
assets/zb-version.txt = 9ae1e3d9…                                     (pre-patch)
```

### 2.5 Two version codes above 1002 already exist (needs a Lead decision)

1002 was mandated for this build and is used. But the perf workstream's build in the shared staging
tree is named `dh2-perf-hw1102.apk`, i.e. **1102**. If 1102 reaches the device before this APK,
installing 1002 is a *downgrade*: `adb install -r` will refuse it and `-d` (allow downgrade) would
be required. See §6 step 0 — check the installed code first and stop if it is above 1002.

---

## 3. What each change should contribute

None of these are frame predictions; the device run is the only measurement. The model column is
what the earlier workstream measured or modelled, repeated here so the device result can be read
against it.

| change | mechanism | expected contribution | basis |
|---|---|---|---|
| VFP guest-libc patch | 20 soft-float `__aeabi_*` bodies replaced with NaN-guarded VFP; 1332 → 215 instructions in those bodies (−83.9 % body cost) | the largest single win: 59.5 % of executed guest instructions are inside those bodies, and every call also loses the compiler-rt veneer/loop | `SOFTFLOAT-VFP-REPLACEMENT.md`, `GUEST-INSTRUCTION-MIX.md`; ~11 ms/frame was the earlier model (`TWO-TRACKS.md`) |
| native hook (quat-mul) | the 448-byte / 112-instruction guest `quaternion::operator*` is not translated at all; the block emits one host `blr` into a hand-written AArch64 body plus `bx lr` | removes a hot per-vertex/per-object call from the JIT; on device it was already bit-exact over 9,232 executions | `NATIVE-HOOK-MECHANISM.md`, `GUEST-REWRITE-PIPELINE.md` |
| affinity + priority | the one thread carrying 82.7 % of frame CPU is pinned to the fastest *allowed* CPU (cpu6/7, cap 1024) and niced to −8 | removes landing on / migrating to the cap-765 slow class for a single-thread-bound workload | `HOST-PERF-HARDWARE.md`, `zb/thread_perf.h` |
| GL state shadowing | **excluded** — 1.57 % measured redundancy | nothing; its drop path stays disarmed | `HOST-GL-BATCHING.md` |
| display mode / `setFrameRate` | **excluded** — caused black letterbox bars and the platform refuses the refresh change; vendor-capped at 60 Hz | nothing | addendum items 16; `FRAME-LIMITER.md` |

---

## 4. Verification actually performed (off-device, quoted)

### 4.1 The host library builds clean, from scratch

`bash scripts/build-host.sh` removes the build directory, configures with the pipeline's own flags
(NDK r29 clang 21.0.0, `arm64-v8a`, `android-29`, Release, `ZB_BUILD_TESTS=OFF`, CMake 4.2.3,
Ninja) and builds `zbridge` + `zbproxy`:

```
[172/172] Linking CXX shared library core/libzbridge.so
=== artefacts ===
-rwxr-xr-x 1 root root 3015352 Oct  3 22:09 .../core/libzbridge.so
5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd  .../core/libzbridge.so
BUILD_DONE
warning: lines total             = 117
own-code warnings (not third_party/) = 0
errors                            = 0
```

The 117 vendored warnings are a single kind, `-Wdeprecated-literal-operator` in
`third_party/dynarmic/externals/fmt/include/fmt/format.h:4429`; the dependency is byte-identical to
upstream (`third_party/` is a build input that this work never edited). `check_zbproxy: OK` also
ran as part of the build.

### 4.2 The VFP differential harness, re-run on the bytes inside the signed APK

The harness is `vfp/vfp-test` (rebuilt from source in this tree: `arm-linux-gnueabihf-gcc 15.2.0`
for the bodies, `qemu-arm 10.2.1` to execute ARM32 VFP — Unicorn cannot execute a single ARM VFP
instruction). Its two inputs are the shipped helper cluster and the cluster **as it exists inside
the signed APK**:

```
$ python scripts/extract_blob.py vfp/libc.pristine.so            vfp/vfp-test/blob.bin
blob.bin:         28640 bytes  vaddr 0x9eb00..0xa5ae0  sha256 b20cc0ba4c6cf7f6d21562d939f1454121730e7ef7d0bd958e81a66ab2338f48
$ python scripts/extract_blob.py out/packaged-libc.so           vfp/vfp-test/blob_patched.bin
blob_patched.bin: 28640 bytes  vaddr 0x9eb00..0xa5ae0  sha256 821aa01e5de15d3f152c8f0bb37da9857e61753a6dc46969dd1e4ba0e67837b2
```

`out/packaged-libc.so` is extracted **from the signed APK**
(`assets/zb/sysroot/system/lib/libc.so`, sha256 `d34addce…`), and the APK was rebuilt afterwards
without touching the libc: the blob regenerated from the final APK is byte-identical
(`821aa01e…`), so the sweep result below applies to the final package.

`W=16 bash scripts/run-vfp-sweep.sh <33 tests>` (`out/vfp-sweep-final.txt`):

```
positive tests: 32
comparisons   : 18353231360
mismatches    : 0
SWEEP DONE    : 1
```

and the file is **byte-identical** to the VFP workstream's own `results-ship-final.txt`
(0 mismatches on all 32 positive tests; the negative control `fmul_eb_plain` still reports its
expected 33,554,424 mismatches, which is what proves the guard and the instrument are live in the
same run).

### 4.3 The signed APK carries exactly that payload

`python scripts/verify_apk.py --apk out/dh2-merged1002.apk --expect out/expect.json`
→ **12/12 checks true, `problems: []`** (`out/verify.json`):

```
libc in APK          d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8  (patched)
host library in APK  5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd / 3,015,352 B
bundle token         2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65
token recomputed     2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65  (match)
versionCode/Name     1002 / 1.0-merged1002
zb-files.txt vs APK  19 entries, exact match
display-mode needles 0 hits in the host payload, 0 in the guest payload
hook basename key    "libDungeonHunter2.so" present
hook file offset     bytes 34 dd 60 00 present (LE32 and LE64)
hook mapped VA       bytes 34 dd e8 fc absent (0 hits)  <- an address-keyed hook would carry this
gl-shadow gate       "zb-gl-shadow-enable" present (the drop path is the gated one)
```

`apksigner verify` (run by the builder) reports `Verified using v3 scheme: true`, and the builder's
own `zipalign -c -P 16 4` step exits 0.

### 4.4 The hook key, executed (not just inspected)

`out/hook-key-test.txt`: 7/7 assertions on the real resolver, `HOOK KEY TEST PASSED (0 failures)` —
quoted in full in §1.2.

---

## 5. What is NOT verified until the device run

Honest list; nothing here is claimed as working.

1. **In-game FPS for this combination.** No frame number in this document was measured. The VFP
   win, the hook and the pinning have each been measured or modelled separately; the *merged* build
   has not run anywhere with a GPU.
2. **That the guest really executes the patched VFP bodies on device.** The sweep is bit-exact
   against a faithful ARMv7 VFP model (qemu-arm); the device executes the guest through Dynarmic.
   `SOFTFLOAT-VFP-SHIP.md` §6 records the same gap.
3. **That `files/zb/.../libc.so` on the device is the patched file.** The app is not debuggable and
   the handset is not rooted, so the extracted bundle cannot be read back. The evidence available
   is the token argument in §2.3 (the marker cannot match, so extraction must run) plus the
   behavioural change.
4. **That the hook fires on device in this build.** Only the translation-time log line and the
   report counters can prove it: `native hook: resolved …`, `native hook: emitting host call for
   the block at …`, `native hook quat_mul: first host-native call …`,
   `native-hook-quat-mul-calls`, `native-hook-quat-mul-asm-vs-reference-mismatches: 0`.
   Off-device I can only prove the keying and that the code is linked in.
5. **Whether pinning helps or hurts here.** `debug.zb.perf.pin` defaults to on, so the merged build
   pins without an A/B. The device run should record the `gl-perf-*` report lines; if the numbers
   look worse than the unpinned baseline, the honest next step is a pin-off A/B, not a reinterpretation.
6. **That nothing regressed in the untouched host surface** (JNI/GL syscall layer). The merged
   build is the same shrink fork plus three additions, but no host test suite was run:
   `ZB_BUILD_TESTS=OFF` is the pipeline's own setting (the same one the verified builds used).
7. **APK-side toolchain diagnostics.** 116 vendored `fmt` warnings and the `zipalign` header-mismatch
   lines are pre-existing/benign as far as the checks show, but they were not bisected against a
   pre-merge build of the same tree.
8. **That 1002 is installable at all** at the time the Lead runs — see §6 step 0 and §2.5.

---

## 6. Checklist for the Lead's single device session

**Package:** `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\integrate\out\dh2-merged1002.apk`
(13,654,554 B, sha256 `b4b38dd8de701821003f21bc138057e5079b536615c6d762a32ef2cac1fe1c23`,
`versionCode 1002`, `versionName 1.0-merged1002`, package `local.dh2.fold7`).

**Step 0 — do not install blind (30 s).** The perf workstream has produced a 1102 build in the
shared staging tree, so the phone may already carry a code above 1002:

```powershell
adb -s RFCY700ANZF shell dumpsys package local.dh2.fold7 | Select-String versionCode
```

* if it reports ≤ 1002 → install normally (step 1);
* if it reports > 1002 → **stop and ask for a re-cut at a higher code.** Do not downgrade (the
  allocation rule in `TWO-TRACKS.md`), and do not uninstall (it wipes the imported cache).

**Step 1 — install (never uninstall, never `pm clear`).**

```powershell
adb -s RFCY700ANZF install -r -d `
  C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\integrate\out\dh2-merged1002.apk
adb -s RFCY700ANZF shell dumpsys package local.dh2.fold7 | Select-String versionCode,versionName
```

Confirm `versionCode=1002 versionName=1.0-merged1002` before playing. Keep the screen awake
(`adb shell svc power stayon true`).

**Step 2 — what to ask the user to do.** Launch the app (`com.zettabridge.launcher.Dh2Activity`),
tap **LAUNCH GAME**, let the intro cinematics play (they lock at 60.0 FPS and are excluded), then
**play normally for at least 3 minutes** in a scene that was slow before — the warm-light /
many-object scenes that previously sat at ~29 average. Ask them to keep playing without pausing or
backgrounding (bringing the launcher forward pauses the engine), and to note the in-game clock so
the window can be aligned with the trace.

**Step 3 — evidence to capture.**

1. `adb -s RFCY700ANZF shell dumpsys package local.dh2.fold7 | Select-String versionCode` (in the
   run window, not after another agent installs).
2. The engine trace `…/plugins/com.gameloft.android.GAND.GloftD2SS/dh2-events.txt`. The FPS
   readout is the `render returned frames=N elapsedMs=M` lines. **Per 120-frame batch:**

   ```
   FPS = Δframes / (ΔelapsedMs / 1000)        Δ = (N₂−N₁, M₂−M₁) between consecutive lines
   ```

   Batch on `Δframes = 120` (`FPS = 120 / (ΔelapsedMs/1000)`, i.e. 60.0 FPS at ΔM = 2000 ms).
   **Exclude the cinematic:** the window starts only after `MyVideoView destroyed`, and any batch
   that reads exactly 60.0 FPS inside the opening sequence is the cinematic lock, not gameplay —
   report the in-game batches separately and say which lines were excluded and why.
3. The runtime report the app can display (screens): `native-hook-*` counters and details, the
   `gl-perf-*` lines (`gl-perf-topology`, `gl-perf-allowed`, `gl-perf-tid`, `gl-perf-action`,
   `gl-perf-core`, `gl-perf-affinity`, `gl-perf-nice`, `gl-perf-active`),
   `gl-shadow-drop-path: disarmed`, `unimplemented-host-calls`, `guest-exit`, and the
   `gl-calls` / `native-calls` totals.
4. Logcat for the hook lines: `adb logcat -d | Select-String 'native hook|perf: render thread|perf: topology|perf: pin|perf: renice'`.
   Expect `native hook: resolved quat-mul libDungeonHunter2.so+0x60dd34 -> pc 0x…`, then
   `native hook: emitting host call for the block at …`, then
   `native hook quat_mul: first host-native call r0=… r1=… r2=… fpcr=0x…`.
5. A screenshot of the game actually rendering (the black-letterbox check from the display-mode
   experiment must **not** reappear) and `adb shell ps -A | Select-String local.dh2.fold7` to show
   the guest process is alive.
6. If anything crashes: `adb logcat -d | Select-String 'SIGSEGV|DEBUG|backtrace'` and the crash
   report path, plus `guest-exit` from the report. Report a crash as a crash.

**Step 4 — what to compare it against.** The pre-merge baseline for the same scene shape is
**avg 28.9 / median 36.6 FPS** (warm-light scenes 47–50) against the hard 60 Hz ceiling. Report
per-batch FPS, the batch count, and the scene. A number without the excluded cinematic and without
the batch count is not comparable.

**Step 5 — restore state afterwards.** If the session ends on this APK that is fine (never
uninstall); the shared staging slot is already the shipped `25e8da7e…` / 3,306,992 B and needs no
action.

---

## 7. Reproduce

```powershell
$I = 'C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\integrate'

# 1. base + merge (already done): host\integrate\ZettaBridge is the shrink fork snapshot
#    804aec63... (2026-10-03 22:08) plus the gl_shadow.cpp portability edit.

# 2. host library: clean configure + build in a private WSL tree
wsl -d Ubuntu -- bash $I/scripts/sync-host.sh
wsl -d Ubuntu -- bash $I/scripts/build-host.sh            # -> out/host-build-final.log (0 own warnings)

# 3. VFP patch, from the pristine libc (deterministic; reproduces d34addce...)
cd $I\vfp
python patch_libc.py --src libc.pristine.so --vfp-obj vfp-test\vfp.o `
  --out libc.patched.so --manifest manifest.json --blob-dir vfp-test
python tier3_check.py libc.pristine.so libc.patched.so    # ELF metadata: ALL except .text

# 4. stage the library + the patched libc and regenerate the bundle identity
wsl -d Ubuntu -- bash -lc 'cp /root/dh2/integrate/ZettaBridge/build/android-arm64/core/libzbridge.so /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/integrate/build/libzbridge.so'
Copy-Item $I\build\libzbridge.so $I\stage\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so -Force
python $I\scripts\prepare_bundle.py --stage $I\stage --patched-libc $I\vfp\libc.patched.so --report $I\out\bundle.json

# 5. build the signed APK at versionCode 1002 (private stage, shared slot untouched)
pwsh -File $I\scripts\build-apk.ps1

# 6. verify the packaged payload
python $I\scripts\verify_apk.py --apk $I\out\dh2-merged1002.apk --expect $I\out\expect.json --report $I\out\verify.json

# 7. differential sweep on the bytes inside the signed APK
python $I\scripts\extract_blob.py $I\vfp\libc.pristine.so $I\vfp\vfp-test\blob.bin
python $I\scripts\extract_blob.py $I\out\packaged-libc.so $I\vfp\vfp-test\blob_patched.bin
wsl -d Ubuntu -- bash $I/scripts/run-vfp-sweep.sh fmul_p_s fadd_p_s fsub_p_s fdiv_p_s `
  fmul_p_r fadd_p_r fsub_p_r fdiv_p_r fmul_p_eb fadd_p_eb fsub_p_eb fdiv_p_eb `
  dmul_p_s dadd_p_s dsub_p_s ddiv_p_s dcmpeq dcmplt32 dcmple32 dcmpge32 dcmpgt32 dcmpun32 `
  fcmpeq_p_s fcmplt_p_s fcmple_p_s fcmpge_p_s fcmpgt_p_s fcmpun_p_s i2f_p ui2f_p `
  fnan_p fnan_add_p fmul_eb_plain

# 8. the hook key, executed
wsl -d Ubuntu -- bash $I/scripts/build-hook-key-test.sh    # -> HOOK KEY TEST PASSED
```
