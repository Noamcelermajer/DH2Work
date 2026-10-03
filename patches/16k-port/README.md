# 16 KiB host-page mapper proof of concept

This directory holds experimental patches for public ZettaBridge commit `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`. They remain separate from the supported DH2 build. An isolated local test APK was built with these patches and a temporarily changed page guard; that APK is not a validated game release. The repository builder's 4096-byte host-page launch guard remains in force.

## What the patch does

- Keeps the ARM32 guest ABI at 4096-byte pages and detects the host kernel page size independently.
- On a 16384-byte host, allocates complete host pages while tracking the four guest subpages separately. Mapping, protection and unmapping one guest subpage preserve the bytes and guest flags of its neighbors.
- Materializes `MAP_PRIVATE` file mappings so a 4096-byte file offset can be used on a 16384-byte host. `MAP_SHARED` file mappings fail explicitly with `ENOTSUP` in this prototype.
- Disables Dynarmic's direct fastmem pointer for 16 KiB hosts so translated accesses should use the checked guest-memory callbacks.
- Leaves the original 4 KiB host path intact.

The separate `zettabridge-madvise-wipeonfork-16k.patch` handles one verified guest syscall issue. It returns success for `MADV_WIPEONFORK` on a 16 KiB host after checking that the guest range is mapped. ZettaBridge refuses guest `fork` (`clone` without `CLONE_VM|CLONE_THREAD`), so this fork-only advice has no effect on supported guest execution. Passing the 4 KiB-aligned range through to the host kernel returned `EINVAL` and made Android bionic abort.

The experimental `zettabridge-madvise-dontneed-anon-16k.patch` adds guest mapping origin tracking. On a 16 KiB host, `MADV_DONTNEED` clears only the requested 4 KiB guest pages when every page is proven private anonymous. It refuses file, shared anonymous, and unknown origins with `ENOTSUP`, rather than discarding adjacent pages or silently changing file mapping semantics. It is applied after the mapper and `MADV_WIPEONFORK` patches. Other `madvise` operations still need review.

## Reproduce the focused test

Use the pinned ZettaBridge source and apply `zettabridge-guest-memory-16k-poc.patch`. The source test is `tests/guest_memory_coarse_test.cpp` in this directory. The build needs Android NDK r29 and an Android x86_64 emulator that reports `16384` from `adb shell getconf PAGE_SIZE`.

```powershell
git clone https://github.com/ZailoxTT/ZettaBridge.git ZettaBridge-16k-test
cd ZettaBridge-16k-test
git checkout 7c647a4f1ea150eab7978ab0da28fdf49f3a79de
git apply path/to/DH_sc/compatibility/16k-port/zettabridge-guest-memory-16k-poc.patch
git apply path/to/DH_sc/compatibility/16k-port/zettabridge-madvise-wipeonfork-16k.patch
git apply path/to/DH_sc/compatibility/16k-port/zettabridge-madvise-dontneed-anon-16k.patch

$ndkBin = 'path/to/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'
$test = 'path/to/DH_sc/compatibility/16k-port/tests/guest_memory_coarse_test.cpp'
& (Join-Path $ndkBin 'x86_64-linux-android35-clang++.cmd') -std=c++20 -O2 -fPIE -pie -static-libstdc++ -I core/include $test core/src/guest_memory.cpp core/src/log.cpp -llog -o guest_memory_coarse_test
adb -s emulator-5558 push guest_memory_coarse_test /data/local/tmp/guest_memory_coarse_test
adb -s emulator-5558 shell 'chmod 755 /data/local/tmp/guest_memory_coarse_test; /data/local/tmp/guest_memory_coarse_test'
```

Use the serial reported by `adb devices` for the 16 KiB emulator; `emulator-5558` was the local test serial. The test creates and removes a temporary file under `/data/local/tmp`.

## Focused mapper result, 2026-10-02

The patches applied cleanly in sequence to a fresh worktree of the pinned commit. The NDK r29 x86_64 test executable ran on the Android API 37.2 16 KiB emulator (`sdk_gphone16k_x86_64`). `getconf PAGE_SIZE` returned `16384`. All 35 checks passed with zero failures:

- two adjacent 4 KiB guest mappings preserve each other's contents inside one 16 KiB host page;
- read-only protection and unmap apply to only one guest subpage according to guest metadata;
- remapping zeros the selected guest subpage without erasing its neighbor;
- two private file mappings at 4 KiB host-unaligned offsets contain the expected bytes and preserve adjacent mappings;
- unsupported shared file mapping returns `ENOTSUP`;
- private anonymous `MADV_DONTNEED` handling zeros exactly one selected 4 KiB guest page and preserves its neighbor in the same 16 KiB host page;
- file, shared anonymous and unknown guest origins are refused without changing their bytes;
- unmapping and remapping a full host page yields zeroed memory.

This is a memory-component result. The test directly exercises the checked discard helper used by the syscall, but does not execute the entire ARM32 syscall path. The executable is x86_64 Android, and the patched `guest_thread.cpp` was not part of it.

## Isolated Android 17 16 KiB runtime trial, 2026-10-02

An isolated ARM64 `libzbridge.so` was built with NDK r29 from the pinned ZettaBridge commit, the existing DH2 translator patch, the mapper patch above, the separate `MADV_WIPEONFORK` patch, and both pinned Dynarmic patches: `third_party/patches/dynarmic-0001-thumb32-armv8.patch` and `third_party/patches/dynarmic-0002-asimd-narrowing.patch`. Reverse-apply checks confirmed both patches in the private native source tree. The binary's SHA-256 was `e18028f24efd9447036b6f0abdb5c49abefa2840026c91dfede12597e2a5efca`; all ELF `LOAD` segments had 16 KiB alignment. A private Test 9 wrapper APK bundled this library and the owner's verified game/cache inputs. Its page guard was changed **only in the isolated build copy** to admit 16 KiB hosts. That signed test APK had SHA-256 `3c138043b6b12300ae4d92b49748a9d3e14fc935a52bc63c78be03e2bc101bfd`.

On the official Android 17/API 37.2 `sdk_gphone16k_x86_64` emulator, `getconf PAGE_SIZE` returned `16384`. The APK installed, the wrapper opened, and **Launch Game** loaded the ARM64 translator through Android's x86 emulator native bridge. The guest linker started, `libnativeinterface.so` loaded, the original game engine bound 32 JNI natives, `libStormGLOFT.so` loaded, and the GL thread reached shader processing. This is real runtime progress beyond the page guard, but no menu or gameplay was reached.

The GL thread then aborted: guest bionic Scudo reported `corrupted chunk header ... memory corruption or a double free`. Nearby kernel logs contain repeated `do_madvise: addr ... not page aligned` for other 4 KiB guest ranges. Those calls are distinct from the handled `MADV_WIPEONFORK`; the log alone does not prove they caused the Scudo failure. The guest process ended with signal 6. The isolated test therefore **does not establish 16 KiB compatibility**. Its APK and private cache were not added to Git. No Fold7 or physical device was tested.

## Restricted private-anonymous trial, 2026-10-02

The unaligned host `MADV_DONTNEED` calls were investigated in a separate diagnostic build. Zeroing every accessible guest range avoided the Scudo abort and allowed title, saved-character menu, and a saved 3D level to load. That broad zeroing is unsafe: a file mapping must reload its original bytes on discard, and a shared mapping has different semantics. It was used only to identify the failure boundary and was never added to the repository or offered as a game build. A provenance trace through saved-level loading observed 807 `MADV_DONTNEED` calls spanning 24,613 guest 4 KiB pages. Every targeted page was tagged anonymous by that trace; file and unknown counts were zero. That first trace could not distinguish private from shared anonymous mappings or preserve origin after `mremap`, so it was not sufficient to ship the broad workaround.

The stricter patch in this directory tracks private anonymous, shared anonymous, file, and unknown guest origins. The syscall's checked helper clears only proven private anonymous pages and rejects the other origins. The 35-check component test on the official Android 17/API 37.2 `sdk_gphone16k_x86_64` emulator passed, including direct zeroing and refusal cases. For a private runtime test, the patch was combined with the existing DH2 translator and pinned Dynarmic patches, plus a verified Test 10 guest APK that corrects the game's initial size and full-width layout. The guest input SHA-256 was `57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e`; the owner's cache ZIP SHA-256 was `3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`.

The private strict bridge SHA-256 was `dfca14dc9b1646297cf933cff402984ece49a1e8fa24f4ca84e9781a88982d58`; its ELF `LOAD` segments all have `0x4000` alignment. The private signed APK SHA-256 was `80ff275679368ab0141aa445654c033d5619b3f77fb37e3beaf52c6c894e0f90`. Its embedded guest and bridge hashes match the verified inputs; `zipalign -c -P 16 -v 4` and `apksigner verify --verbose` passed, with APK signature scheme v3. The isolated wrapper had its page-size guard changed to require 16 KiB, and its fit option enabled. The normal builder retains its 4 KiB guard.

On emulator fingerprint `google/sdk_gphone16k_x86_64/emu64xa16k:17/CP41.260828.004.A7/16296984:userdebug/dev-keys`, `getconf PAGE_SIZE` returned `16384`. This strict trial reached a full-width interactive title and saved-character menu. The game accepted title and menu touches, entered saved Swamps level loading, reached 100% at 01:45:52 UTC, and logged `SHOW OF THE HUD` after a touch at 01:46:17 UTC. Before a 3D frame could be captured, the installed package was replaced at 01:46:34 UTC by an older local version 6 test APK, ending the strict guest process. The replacement was external to the game run, so it is not evidence of a guest crash or a completed playable level. The strict APK was reinstalled for a repeat check. The menu screenshot is `DH2-Android17-16k-strict-test10-character-menu.png` in the task outputs, SHA-256 `b48be0d49d2423e33ec056cd40c144ebfa141933a67bd8b5a23f89bbca2db3e9`; the title screenshot SHA-256 is `633a34cf9058b93a1fedab2aa0f17e38b8a651659775377a08539201d0f2564c`. At that point, visible level completion and movement remained unverified; the repeat run below addressed them.

The repeat strict run reached the same saved Swamps level at 100% and displayed a complete 3D scene with the player, HUD, enemies, and controls on 02:07 UTC. A 1.8-second joystick drag from Android screen coordinate `(500, 885)` to `(645, 780)` was logged by the guest and visibly moved the player and camera while the process stayed alive. The before/after screenshots in the task outputs are `DH2-Android17-16k-strict-test10-gameplay-before-movement.png` (SHA-256 `ace09696611ad43909126db5636d28b5da91cb471028aab74f9c65bb43dc3276`) and `DH2-Android17-16k-strict-test10-gameplay-after-movement.png` (SHA-256 `a4437b865f4c281ee340f47ede90226b5e2d04832702f368b71e97d68435774b`). The pause button opened the full in-game pause menu (`DH2-Android17-16k-strict-test10-pause-menu.png`, SHA-256 `605ac81c6a8627d28d634b9468be7009d520723ff824cdf60360450e19510276`). Selecting Main Menu and confirming it produced the guest's `GoToMainMenu` log sequence. This proves a specific emulator gameplay and input path, not general stability or real ARM64 16 KiB compatibility. Persistence after terminating and reopening the guest, repeated cold launches, 4 KiB regressions, and guest/host boundary semantics still need separate verification. The standard wrapper's 4 KiB page guard remains in force; this strict 16 KiB package is experimental.

The return transition completed and the saved-character menu displayed `02.10. 02:11` as the last save, replacing `18.04. 06:05`; the guest log likewise reported `PlayerLastSave = 02.10.     02:11`. The returned-menu screenshot is `DH2-Android17-16k-strict-test10-saved-menu-return.png` (SHA-256 `cac42ad4fae3a1b7f671e964aad303ff3561f842b031a5d3cd144f0a167c8dfd`). This confirms the updated save as reported by the running game after returning to menu. At the end of the check, the guest process was still alive and its captured log had no Scudo abort or strict `MADV_DONTNEED` origin refusal.

### Rebuild the isolated strict package

Start with pinned ZettaBridge commit `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` in a private checkout. Apply, in order, `zettabridge-guest-memory-16k-poc.patch`, `zettabridge-madvise-wipeonfork-16k.patch`, and `zettabridge-madvise-dontneed-anon-16k.patch` from this directory, then `compatibility/work/fold7-build/zettabridge-dh2.patch`. Apply both translator patches, `third_party/patches/dynarmic-0001-thumb32-armv8.patch` and `third_party/patches/dynarmic-0002-asimd-narrowing.patch`, to the pinned Dynarmic checkout. Build `libzbridge.so` for `arm64-v8a` with NDK r29 and API 35. In a separate copy of `compatibility/work`, place that library at `research/ZettaBridge/build/launcher/jniLibs/arm64-v8a/libzbridge.so`; change only that copy's `fold7-build/java/com/zettabridge/launcher/Dh2Activity.java` page guard to admit 16384-byte pages and enable its 16:9 fit option. Set `DH2_TEST10_GUEST_APK` and `DH2_CACHE_ZIP` to the pinned inputs with the hashes above, along with `DH2_ANDROID_SDK_ROOT`, `DH2_ANDROID_JAR`, `DH2_JDK_ROOT`, and a private `DH2_OUTPUT_APK` path; run `python fold7-build/build_apk.py` from the copied `compatibility/work`. The resulting experimental APK hash must match the one above before using this exact runtime result. The ordinary builder and its page guard were not changed by this trial. For the separate Test11 English candidate, see [TEST11-STRICT-REBUILD.md](TEST11-STRICT-REBUILD.md).

## Code-level blockers before app integration

1. The private-file materialization is a snapshot and does not reproduce all Linux `mmap` behavior, especially `SIGBUS` for accesses wholly beyond EOF or observation of later file changes. Shared file mappings and writeback are unimplemented. The guest linker and game must be traced for these cases before integration.
2. The experimental patch handles `MADV_DONTNEED` only for proven private anonymous guest ranges. `sys_msync` and other `madvise` requests may still forward 4 KiB guest ranges to the 16 KiB host. They need guest-aware handling; `mremap` and mapping replacement require a full regression suite.
3. Dynarmic was rebuilt with fastmem disabled and exercised far enough to start the guest engine. Tests still must prove translated loads, stores, instruction fetch, exclusive operations, guest fault addresses, and signal delivery at boundaries between differently protected 4 KiB subpages.
4. JNI, GL and syscall host bridges include direct guest-base pointers and long-lived mapped buffers. They need an audit for permission checks, mapping lifetime, and concurrent map/unmap. The prototype's per-page flag vector is not synchronized for concurrent modifications.
5. The materialization currently stages each private file mapping in memory before commit. Large mappings need bounded staging and failure-atomic behavior before production use.
6. Run the complete translator's guest and host suites on both 4 KiB and 16 KiB Android systems, then build and test the ARM64 standalone app on a real 16 KiB ARM64 runtime environment. Check every native ELF and APK ZIP alignment separately.

Do not remove the wrapper's page-size guard or package this patch as a supported game runtime until those blockers are resolved and the gameplay path is tested.
