> Test 2 update: the Fold7 report confirms 4096-byte pages and native library/JNI startup. A MediaStore query crash was identified and repaired; the repaired APK awaits a device retest. See [TEST2](work/fold7-build/TEST2.md). The initial test 1 evidence below is retained historically.

## Current checkpoint: test 5

Test 4's device report confirms its directory guard ran, then shows a different failure while reopening the prince model: the cache root is duplicated. The original engine treats Android absolute paths as relative whenever WorkingDirectory is nonempty. A 20-byte ARM fix adds POSIX absolute-path recognition while retaining relative and colon-path behavior. The actual original/fixed engine tests confirm the failed open and correct byte reads after repair.

Thirteen path cases pass, as do five earlier file cases, native library/hook checks and final APK validation. The final model-open logging also passes two focused path probes. This is a new test APK; loading and gameplay on the Fold7 remain unverified. Russian menus, display behavior, the earlier GL error and 16 KB pages are still open. See [TEST5.md](work/fold7-build/TEST5.md).

[Download Test 5](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test5). Install over Test 4, keep the same settings and existing cache, repeat the load, then export DH2-test5-diagnostics.zip. Test 5 modifies the engine binary itself; earlier statements that the engine is unchanged are historical.

## Historical checkpoint: test 4

The owner's Test 3 diagnostics identify the original engine's CFile filename bounds check. An independent original-ARM32 caller probe reproduces the same abort with a trailing directory path. Test 4 adds a narrow engine fopen guard and persistent STL error logging. All five file-opening regressions, native library/hook probes and APK checks pass. The exact phone path and loading-screen repair still require a Fold7 retest. See [TEST4.md](work/fold7-build/TEST4.md) and [BRIDGE-ASSESSMENT.md](work/fold7-build/BRIDGE-ASSESSMENT.md).

[Download test 4](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test4). Install over Test 3, retain cache and saves, repeat the same load with the same options, then export diagnostics. Historical sections below remain for provenance.

The current native source commit is `3094fd150ad7fe5891fd3741fd2f43892424277f` (local provenance); apply the cumulative `zettabridge-dh2.patch` to upstream base `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`. The runtime bundle contains the exact test 4 binaries. Use the included JDK 17 toolchain for apktool; run dependent build/package steps sequentially and stop on errors. `build_apk.py` now verifies the nested APK before adding helper DEX. `tests/test_diagnostics.py` runs deliberate abort/exit/fault probes.

## Historical checkpoint: test 3

The owner's test 2 completes the cinematic and aborts at the subsequent fairy loading screen. Test 3 adds persistent native diagnostics and ZIP export, with optional language/display/context settings. The crash cause and gameplay stability remain unconfirmed. Read [TEST3.md](work/fold7-build/TEST3.md) for the exact changes and phone procedure, and [BRIDGE-ASSESSMENT.md](work/fold7-build/BRIDGE-ASSESSMENT.md) for the independent compatibility assessment. Install test 3 over test 2 and export the diagnostic ZIP after reproducing the failure; retain the existing cache.

[Download test 3](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test3). Older sections below preserve the earlier investigation.

The current native source commit is `3094fd150ad7fe5891fd3741fd2f43892424277f` (local provenance); apply the cumulative `zettabridge-dh2.patch` to upstream base `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`. The runtime bundle contains the exact test 3 binaries. Use the included JDK 17 toolchain for apktool; run dependent build/package steps sequentially and stop on errors. `build_apk.py` now verifies the nested APK before adding helper DEX. `tests/test_diagnostics.py` runs deliberate abort/exit/fault probes.

# Build and reproduce

The script root is `compatibility/work`. Scripts derive paths from their location. Run there with Linux x86_64 and leave generated output in the layout they expect. This is the original build recipe, with evidence; a clean external-machine rebuild remains open.

| Dependency | Recorded version / location |
| --- | --- |
| Android NDK | r29 / 29.0.14206865, `work/toolchains/android-ndk-r29` |
| SDK platform | 35, `work/android-sdk/platforms/android-35` |
| SDK build tools | 35.0.0, `work/android-sdk/build-tools/35.0.0` |
| Java | Temurin 17.0.20.1+1, `work/toolchains/jdk-17*` |
| apktool | 2.12.1, `work/tools/apktool.jar` |
| Python | 3.12+, pyelftools 0.33, capstone 5.0.9 |
| Native build | CMake 4.4.3, Ninja, Boost 1.83 headers |
| HiddenApiBypass | 6.1 AAR plus extracted `classes.jar` as `work/downloads/hiddenapibypass.jar` |
| ZettaBridge | `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`, `work/research/ZettaBridge` |
| Dynarmic | `86458a0bd369d63ba4c2ef812cacbb6c9080c065` with both bundled upstream patches |
| Reference native tests | qemu-aarch64-static 8.2.2 + extracted ARM64 AOSP bionic runtime |
| Independent prototype | Zig 0.13, Unicorn 2.1.4; see its README and requirements |

Download source URLs, sizes and hashes are recorded in `work/fold7-build/download-hashes.json` and `work/tool-manifest.json`. Preserve symlinks when extracting the NDK. SDK/NDK/JDK/QEMU/GSI archives are external dependencies, not repository source.

1. Clone the pinned upstream into `work/research/ZettaBridge` and initialize its submodules.
2. Apply `work/fold7-build/zettabridge-dh2.patch` from that checkout. Both Dynarmic patches are retained under `upstream-modified/third_party/patches`; apply them to the pinned submodule as upstream instructs.
3. For the existing tested runtime, extract `work/fold7-build/runtime-bundle.zip` into `work/research/ZettaBridge/build/launcher`. Compare the included runtime manifest. `build_runtime.py` provides the native rebuild path once dependencies are installed.
4. Install the dependencies into the recorded layout; extract HiddenApiBypass's JAR and use the pinned apktool download script.
5. Run from `work`:

```sh
python fold7-build/patch_game.py
java -jar tools/apktool.jar b fold7-build/game-tree -o fold7-build/game-unsigned.apk
mkdir -p ../deliverables
python fold7-build/build_apk.py
python fold7-build/verify_package.py
```

`patch_storm.py` is pinned to the exact native inputs already preserved in the historical `patched/lib/armeabi-v7a` tree. Do not apply the binary patch to a different game build. The generated smali in the snapshot lets a reviewer inspect the final transformations without running the script.

`run_native_probe.py` additionally requires the reference bionic/QEMU layout. Its actual pass log is preserved. Cache importer tests are standalone Java tests in `work/fold7-build/tests`. The source-kit historical README documents earlier ARM32-only work; it is not the Fold7 installation recommendation.

The script's generated build metadata may report the integration checkout's HEAD as upstream after patches are committed. Use the explicit base pin above and the recorded `build-result.json` to distinguish the base from local integration commit `6e7c65f`.

The included PKCS#12 key and documented password are development fixtures for this test package. Generate a separate appropriately managed signing identity for any production release.
