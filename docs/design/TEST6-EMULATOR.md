# Emulator Test 6: repeated cache root on model reopen

This records the historical Test 6 build. The current Storm source also includes the Test 7 texture-path fallback; see `TEST7-EMULATOR.md` for the current emulator candidate.

The Android 9 x86 Google APIs emulator runs the directly installed ARM32 Test 5 guest through Android's translation layer. Its cinematic, splash, saved slot, and menu display. The 2026-10-02 log shows a successful open of `prince_modular.bdae`, followed by a failed reopen with a duplicated cache root. The second copy is lowercased by the original engine. The next engine operation crashes while opening the menu model.

The Storm import guard now retries a failed read-only `fopen` only when the requested path contains two identical `/storage/emulated/0/Android/data/<package>/files/` roots, ignoring ASCII case. It removes the second root and keeps the first root's exact spelling. The existing root-directory rejection happens before this retry. Paths with one root, relative names, different package roots, write modes, and nonmatching paths keep their original behavior. The fallback reports `DH2FileGuard recovered repeated root:` when it succeeds.

## Rebuild the emulator guest

The original APK supplies `libStormGLOFT.so` and `libDungeonHunter2.so`; the unsigned Test 5 guest supplies the manifest, classes, resources, and existing Test 5 engine patch. Extract the two original libraries, then run `patch_storm.py` with `--original-storm`, `--original-engine`, `--toolchain-bin`, and `--output`. The script verifies both original library SHA-256 hashes and the pinned patch site, compiles the ARM32 guard using Android NDK r29, and records the patched Storm hash in `storm-patch-report.json`.

The Python build dependencies are `capstone` and `pyelftools`. They can be installed in a task-local virtual environment; the host regression test uses a C11 compiler. On Windows, point `--toolchain-bin` at NDK r29's `toolchains/llvm/prebuilt/windows-x86_64/bin` directory. The patch script also retains its original Linux NDK default.

Run `package_emulator_guest.py --base-apk <unsigned-test5-guest.apk> --patched-storm <patched-libStormGLOFT.so> --output <unsigned-test6-guest.apk>`. It verifies that the Test 5 engine and Storm libraries match their pinned hashes and replaces only Storm. Align and sign the output with the local emulator test key before installing it. Do not add a signed test APK or original proprietary libraries to Git.

This worktree's Test 6 unsigned APK was produced at `work/emulator-test/test6-guest-unsigned.apk` relative to the task directory. Its SHA-256 is `85a7ececd975af57f7651dca1d7401af499b77467d814ac632afa91c3300edbc`; the new Storm library SHA-256 is `98eecf6d8b13be98a3d615db4584d67f2f59843deb0396516e72fe99601f868a`.

## Verification

`tests/test_repeated_cache_root.c` passes under host GCC with `-std=c11 -Wall -Wextra -Werror`. It checks the exact emulator path, ordinary absolute and relative paths, a directory, different packages, an absent child, unrelated double slashes, and capacity bounds. The ARM32 guard also compiles under NDK r29 with `-Wall -Wextra -Werror`; the linker validates that the expanded code fits its pinned executable gap and contains no runtime data relocations. The generated unsigned APK passes ZIP CRC verification. Emulator runtime behavior must be checked after signing and installing it.
