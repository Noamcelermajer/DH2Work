# Rebuild the experimental 16 KiB Test11 package

This is the source-only delta from the [Test11 standalone build](../work/fold7-build/STANDALONE-TEST11-LANGUAGE.md). Use DH_sc commit `8b47d20` or a descendant with the same Test11 builder and `LanguagePreference.java`. Keep the ordinary `compatibility/work` source and its 4096-byte launch guard unchanged. All owner media, toolchains, runtime bundles, and signing material stay in a private build directory outside Git.

## Pinned inputs and native source

The Test11 builder still takes `DH2_TEST10_GUEST_APK` (SHA-256 `57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e`) and `DH2_CACHE_ZIP` (SHA-256 `3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`). The builder checks both hashes. It emits versionCode `14`, versionName `1.0-test11-language`.

Start from public ZettaBridge commit `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`; its Dynarmic submodule is pinned to `86458a0bd369d63ba4c2ef812cacbb6c9080c065`. Initialize the submodule. Apply these ZettaBridge patches **in order**:

| Patch | SHA-256 |
| --- | --- |
| `zettabridge-guest-memory-16k-poc.patch` | `867c6dee2ee79d7033d296c51bcf73e1115459718d0dff234322d73ddee33813` |
| `zettabridge-madvise-wipeonfork-16k.patch` | `4ef64f7aad6d6cce802332fe68731d34e0c02d06c2ef358f87a2a0a1ea74a455` |
| `zettabridge-madvise-dontneed-anon-16k.patch` | `15d6bf3273b641e7519547045c063cfa751b1b632820f34d05afdb5bc0c7c977` |
| `../work/fold7-build/zettabridge-dh2.patch` | `7ce58278723b96fb39230e9f44caed4b8654cb5da0a6624bd0381f65ffdc74e8` |

Apply **both** patches from the pinned ZettaBridge `third_party/patches` directory to the Dynarmic submodule: `dynarmic-0001-thumb32-armv8.patch` (SHA-256 `6e16df089d3b3d9aceb817e9bb50c159a6c8347c3b01919d36c3101a4ec359b0`) and `dynarmic-0002-asimd-narrowing.patch` (SHA-256 `fea886fa5f003d722f6847cb9a3dccd0f05a31f9491cf0a1ffc2ebc0d52de336`). The private source tree used for the tested binary passed reverse-apply checks for both.

Build ZettaBridge's `zbridge` target with CMake/Ninja, Android NDK r29, Boost headers, and `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-35 -DCMAKE_BUILD_TYPE=Release -DZB_BUILD_TESTS=OFF`. The tested `libzbridge.so` had SHA-256 `dfca14dc9b1646297cf933cff402984ece49a1e8fa24f4ca84e9781a88982d58` and all ELF `LOAD` alignments were `0x4000`.

## Assemble the isolated wrapper

1. Prepare a normal private Test11 `compatibility/work` build as described in the linked Test11 instructions, then copy that prepared work directory to a separate private directory. Keep its runtime files and local signing material in the copy; do not add them to Git.
2. From the root of the **copy**, apply [`test11-strict-page-guard.patch`](test11-strict-page-guard.patch) with `git apply`. The patch changes only `Dh2Activity`'s host-page check from 4096 to 16384. Keep the Test11 `LanguagePreference.java` and versionCode 14 builder untouched. The checked-in 4096-byte guard remains the default build.
3. Put the rebuilt 16 KiB `libzbridge.so` into the copy's `research/ZettaBridge/build/launcher/jniLibs/arm64-v8a/`. Keep the rest of the prepared runtime bundle unchanged.
4. Set `DH2_TEST10_GUEST_APK`, `DH2_CACHE_ZIP`, `DH2_ANDROID_SDK_ROOT`, `DH2_ANDROID_JAR`, `DH2_JDK_ROOT`, and `DH2_OUTPUT_APK` in the build environment. Give `DH2_OUTPUT_APK` a new filename so the tested Test10 package is not overwritten. Run `python fold7-build/build_apk.py` from the copied work directory.

The builder signs and checks the APK with `apksigner verify`, then checks `zipalign -c -P 16 4`. Independently verify versionCode 14, arm64-v8a, the packaged bridge hash, the two pinned owner-input hashes, and the compiled 16384-byte guard before installation. The exact signed APK tested on Android 17/API 37.2 16 KiB emulator had SHA-256 `d688b2a9f4da0c387ea1ddb0448d9de95dd3e00cdac224fca1b1a6e01fa2898e`; another local signing key or build environment can change the APK hash. Treat this as an experimental emulator build while the [16 KiB port blockers](README.md#code-level-blockers-before-app-integration) remain open.
