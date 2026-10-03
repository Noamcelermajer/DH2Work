# Layout and old-path -> new-path mapping

DH2Work holds only the **app + compatibility layer** for running the original
32-bit ARM Dungeon Hunter 2 HD v1.0.2 on an arm64-only Android phone. Every file
in this repository was imported from the `DH_sc` live branch worktree
(`DH_sc-pr`, PR #1 `reconstruction/continue-2026-10-02`) under `compatibility/`,
plus the repository-root `RIGHTS.md`.

* Files imported: **240** (every one listed below).
* Content: byte-identical to the committed blob in `DH_sc-pr` (see section 2).
* Nothing was rewritten, reformatted or summarised; the only changes are the
  destination directory and, for six files, a rename that keeps the filename
  self-describing in its new directory (marked `renamed` in the table).

## 1. Directory map

| New path | Purpose |
| --- | --- |
| `app/launcher-java/` | Host launcher Java that runs in the `:main` process: setup UI, cache import, page-size guard, diagnostics, saved-language preference |
| `app/guest-java/` | Java helpers injected into the guest game DEX (`local.dh2.compat`) |
| `app/manifests/` | Host and guest `AndroidManifest.xml` templates |
| `patches/engine/` | Engine byte patch plus its readable assembly/linker source and report |
| `patches/storm/` | Storm byte patches (load-bias stub, import-hook replacement, `fopen` import guard) plus report |
| `patches/game/` | Guest-APK Java/smali/manifest/storage patcher plus report |
| `patches/zettabridge/` | The cumulative host patch applied to the pinned upstream ZettaBridge commit |
| `patches/16k-port/` | Experimental 16 KiB host-page mapper patches, the focused component test source and the strict rebuild recipe |
| `patches/thread-exhaustion/` | Optional one-instruction save-job engine patch and its packager |
| `build/` | Packaging, runtime build, download-pin verification and package verification scripts |
| `tests/` | `tests/fold7-build`, `tests/16k-port`, `tests/thread-exhaustion` |
| `docs/` | `LAYOUT.md`, `ARCHITECTURE.md`, `STATUS.md` and `docs/design/` (imported documents) |
| `evidence/` | `phone-test3`, `phone-test4`, `emulator-test5`, `fold7-build`, `legacy-arm32`, plus two top-level manifests |
| `notices/` | `UPSTREAM-LICENSE.txt` and `NOTICES.md` |
| `upstream-modified/` | Readable post-patch copies of the upstream files the DH2 host patch touches, with upstream `LICENSE` and `third_party/` notices |
| `RIGHTS.md` | Provenance, rights and publication limits |

## 2. Content integrity note (line endings)

The `DH_sc-pr` worktree was checked out with `core.autocrlf=true`, so its text
files on disk carry CRLF while the committed blobs — and every SHA-256 pinned in
the imported documents — are LF. This import restores the **canonical blob
bytes**: each of the 240 files was verified byte-for-byte against
`git show HEAD:<path>` in `DH_sc-pr`. Spot checks against the pins recorded in
the imported docs:

| File | SHA-256 | Matches the pin in |
| --- | --- | --- |
| `patches/zettabridge/zettabridge-dh2.patch` | `7ce58278723b96fb39230e9f44caed4b8654cb5da0a6624bd0381f65ffdc74e8` | `docs/design/STANDALONE-TEST11-LANGUAGE.md`, `patches/16k-port/TEST11-STRICT-REBUILD.md` |
| `patches/16k-port/zettabridge-guest-memory-16k-poc.patch` | `867c6dee2ee79d7033d296c51bcf73e1115459718d0dff234322d73ddee33813` | `patches/16k-port/TEST11-STRICT-REBUILD.md` |
| `patches/16k-port/zettabridge-madvise-wipeonfork-16k.patch` | `4ef64f7aad6d6cce802332fe68731d34e0c02d06c2ef358f87a2a0a1ea74a455` | same |
| `patches/16k-port/zettabridge-madvise-dontneed-anon-16k.patch` | `15d6bf3273b641e7519547045c063cfa751b1b632820f34d05afdb5bc0c7c977` | same |

The two Dynarmic patch copies under `upstream-modified/third_party/patches/` do
**not** hash to the values pinned in `patches/16k-port/TEST11-STRICT-REBUILD.md`
(`6e16df08...`, `fea886fa...`); those pins refer to the copies shipped inside the
ZettaBridge checkout, not to these `DH_sc` copies. Verify before building.

## 3. Rewritten path references

Imported documents were not edited, so references inside them still name their
original locations. Translate them with this table:

| Reference seen in an imported document | Actual location here |
| --- | --- |
| `compatibility/work/fold7-build/<script>.py` | `build/<script>.py` for the build/packaging scripts; `patches/engine|storm|game/` for the patch scripts |
| `compatibility/work/fold7-build/tests/...` | `tests/fold7-build/...` |
| `compatibility/work/fold7-build/java/...` | `app/launcher-java/...` |
| `compatibility/work/fold7-build/guest-java/...` | `app/guest-java/...` |
| `compatibility/work/fold7-build/*.md` | `docs/design/*.md` |
| `compatibility/work/fold7-build/*.log`, `*.json`, probe `*.txt` | `evidence/fold7-build/` |
| `compatibility/work/fold7-build/phone-test3|4/...` | `evidence/phone-test3|4/...` |
| `compatibility/work/fold7-build/runtime-bundle.zip`, `game-unsigned.apk` | not in Git (private inputs; see `.gitignore`) |
| `compatibility/16k-port/...` | `patches/16k-port/...` and `tests/16k-port/...` |
| `compatibility/thread-exhaustion/...` | `patches/thread-exhaustion/...` and `tests/thread-exhaustion/...` |
| `compatibility/upstream-modified/...` | `upstream-modified/...` |
| `compatibility/evidence/emulator-test5/...` | `evidence/emulator-test5/...` |
| `compatibility/BUILDING.md`, `FINDINGS.md`, `ISSUES.md`, `DEVICE-TESTING.md` | `docs/design/` (same filenames) |
| `compatibility/README.md`, `CONTENTS.md` | `docs/design/COMPATIBILITY-README.md`, `docs/design/CONTENTS.md` |
| `compatibility/work/README.txt`, `README-ARM32-HISTORICAL.txt` | `docs/design/historical/` |
| `RIGHTS.md` | `RIGHTS.md` (unchanged) |
| `../../RIGHTS.md`, `../../../RIGHTS.md` (from a moved document) | `RIGHTS.md` at the repository root; the relative depth changed because the source documents moved one level deeper *and* the tree was reorganised |
| `tests/guest_memory_coarse_test.cpp` (from `16k-port/README.md`) | `tests/16k-port/guest_memory_coarse_test.cpp` |
| `../work/fold7-build/<doc>.md` (from `16k-port/README.md`) | `docs/design/<doc>.md` |
| `port/`, `recovered/`, `tools/`, `reports/` | **not imported** — reconstruction track (section 5) |

> **Build-script caveat.** Every script derives its input root from its own
> location (`ROOT = Path(__file__).parent; WORK = ROOT.parent`). The copies in
> `build/` therefore do not run in place from this repository; restore the
> private `compatibility/work` tree described in
> `docs/design/STANDALONE-TEST7.md` to execute them.

## 4. Complete mapping table

### `app/` — Application sources (host launcher Java, guest Java helpers, manifest templates) (10 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/work/fold7-build/guest-java/local/dh2/compat/GamePaths.java` | `app/guest-java/local/dh2/compat/GamePaths.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/guest-java/local/dh2/compat/GameTrace.java` | `app/guest-java/local/dh2/compat/GameTrace.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/guest-java/local/dh2/compat/MediaQueries.java` | `app/guest-java/local/dh2/compat/MediaQueries.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test10-guest-java/local/dh2/compat/GameTrace.java` | `app/guest-java/test10/local/dh2/compat/GameTrace.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/java/com/zettabridge/launcher/CacheArchive.java` | `app/launcher-java/com/zettabridge/launcher/CacheArchive.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/java/com/zettabridge/launcher/Dh2Activity.java` | `app/launcher-java/com/zettabridge/launcher/Dh2Activity.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/java/com/zettabridge/launcher/Dh2Diagnostics.java` | `app/launcher-java/com/zettabridge/launcher/Dh2Diagnostics.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/java/com/zettabridge/launcher/LanguagePreference.java` | `app/launcher-java/com/zettabridge/launcher/LanguagePreference.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-AndroidManifest.xml` | `app/manifests/game-AndroidManifest.xml` |  |
| `DH_sc-pr/compatibility/work/fold7-build/host-AndroidManifest.xml` | `app/manifests/host-AndroidManifest.xml` |  |

### `patches/` — Byte patches, the host patch layer and the experimental patch sub-projects (23 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/16k-port/README.md` | `patches/16k-port/README.md` |  |
| `DH_sc-pr/compatibility/16k-port/TEST11-STRICT-REBUILD.md` | `patches/16k-port/TEST11-STRICT-REBUILD.md` |  |
| `DH_sc-pr/compatibility/16k-port/test11-strict-page-guard.patch` | `patches/16k-port/test11-strict-page-guard.patch` |  |
| `DH_sc-pr/compatibility/16k-port/zettabridge-guest-memory-16k-poc.patch` | `patches/16k-port/zettabridge-guest-memory-16k-poc.patch` |  |
| `DH_sc-pr/compatibility/16k-port/zettabridge-madvise-dontneed-anon-16k.patch` | `patches/16k-port/zettabridge-madvise-dontneed-anon-16k.patch` |  |
| `DH_sc-pr/compatibility/16k-port/zettabridge-madvise-wipeonfork-16k.patch` | `patches/16k-port/zettabridge-madvise-wipeonfork-16k.patch` |  |
| `DH_sc-pr/compatibility/work/fold7-build/engine-patch-report.json` | `patches/engine/engine-patch-report.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/engine_path_fix.S` | `patches/engine/engine_path_fix.S` |  |
| `DH_sc-pr/compatibility/work/fold7-build/engine_path_fix.ld` | `patches/engine/engine_path_fix.ld` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch_engine.py` | `patches/engine/patch_engine.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-patch-report.json` | `patches/game/game-patch-report.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch_game.py` | `patches/game/patch_game.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch_storm.py` | `patches/storm/patch_storm.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/storm-patch-report.json` | `patches/storm/storm-patch-report.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/storm_bias_fix.S` | `patches/storm/storm_bias_fix.S` |  |
| `DH_sc-pr/compatibility/work/fold7-build/storm_bias_fix.ld` | `patches/storm/storm_bias_fix.ld` |  |
| `DH_sc-pr/compatibility/work/fold7-build/storm_import_fix.c` | `patches/storm/storm_import_fix.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/storm_path_repair.h` | `patches/storm/storm_path_repair.h` |  |
| `DH_sc-pr/compatibility/thread-exhaustion/README.md` | `patches/thread-exhaustion/README.md` |  |
| `DH_sc-pr/compatibility/thread-exhaustion/package_sync_trial.py` | `patches/thread-exhaustion/package_sync_trial.py` |  |
| `DH_sc-pr/compatibility/thread-exhaustion/patch_sync_jobs.py` | `patches/thread-exhaustion/patch_sync_jobs.py` |  |
| `DH_sc-pr/compatibility/thread-exhaustion/validation.json` | `patches/thread-exhaustion/validation.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/zettabridge-dh2.patch` | `patches/zettabridge/zettabridge-dh2.patch` |  |

### `build/` — Packaging, runtime, download and verification scripts (16 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/work/fold7-build/analyze_diagnostics.py` | `build/analyze_diagnostics.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build_apk.py` | `build/build_apk.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build_runtime.py` | `build/build_runtime.py` |  |
| `DH_sc-pr/compatibility/work/decompile_java.py` | `build/decompile_java.py` |  |
| `DH_sc-pr/compatibility/work/download-tools.py` | `build/download-tools.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package_emulator_guest.py` | `build/package_emulator_guest.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package_test3.py` | `build/package_test3.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package_test4.py` | `build/package_test4.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package_test5.py` | `build/package_test5.py` |  |
| `DH_sc-pr/compatibility/work/patch_compat.py` | `build/patch_compat.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/repack_test10_guest.py` | `build/repack_test10_guest.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/repack_test9_guest.py` | `build/repack_test9_guest.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/run_native_probe.py` | `build/run_native_probe.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/standalone_inputs.py` | `build/standalone_inputs.py` |  |
| `DH_sc-pr/compatibility/work/verify_build.py` | `build/verify_build.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/verify_package.py` | `build/verify_package.py` |  |

### `tests/` — Host Java / C / Python tests (16 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/16k-port/tests/guest_memory_coarse_test.cpp` | `tests/16k-port/guest_memory_coarse_test.cpp` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/CacheArchiveTest.java` | `tests/fold7-build/CacheArchiveTest.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/LanguagePreferenceTest.java` | `tests/fold7-build/LanguagePreferenceTest.java` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/dh2_file_open_probe.c` | `tests/fold7-build/dh2_file_open_probe.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/dh2_file_probe.c` | `tests/fold7-build/dh2_file_probe.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/dh2_load_probe.c` | `tests/fold7-build/dh2_load_probe.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/diagnostic_probe.c` | `tests/fold7-build/diagnostic_probe.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/run_emulator_smoke.py` | `tests/fold7-build/run_emulator_smoke.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/run_file_open_tests.py` | `tests/fold7-build/run_file_open_tests.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/run_path_open_tests.py` | `tests/fold7-build/run_path_open_tests.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/test_diagnostics.py` | `tests/fold7-build/test_diagnostics.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/test_language_preference.py` | `tests/fold7-build/test_language_preference.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/test_media_queries.py` | `tests/fold7-build/test_media_queries.py` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/test_repeated_cache_root.c` | `tests/fold7-build/test_repeated_cache_root.c` |  |
| `DH_sc-pr/compatibility/work/fold7-build/tests/test_standalone_inputs.py` | `tests/fold7-build/test_standalone_inputs.py` |  |
| `DH_sc-pr/compatibility/thread-exhaustion/tests/test_patch_sync_jobs.py` | `tests/thread-exhaustion/test_patch_sync_jobs.py` |  |

### `docs/` — Documents (this file, ARCHITECTURE, STATUS and the imported design/status docs) (22 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/work/fold7-build/ANDROID_TESTING.md` | `docs/design/ANDROID_TESTING.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/BRIDGE-ASSESSMENT.md` | `docs/design/BRIDGE-ASSESSMENT.md` |  |
| `DH_sc-pr/compatibility/BUILDING.md` | `docs/design/BUILDING.md` |  |
| `DH_sc-pr/compatibility/README.md` | `docs/design/COMPATIBILITY-README.md` | renamed |
| `DH_sc-pr/compatibility/CONTENTS.md` | `docs/design/CONTENTS.md` | renamed |
| `DH_sc-pr/compatibility/DEVICE-TESTING.md` | `docs/design/DEVICE-TESTING.md` |  |
| `DH_sc-pr/compatibility/FINDINGS.md` | `docs/design/FINDINGS.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/README.txt` | `docs/design/FOLD7-BUILD-README.txt` | renamed |
| `DH_sc-pr/compatibility/ISSUES.md` | `docs/design/ISSUES.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/STANDALONE-TEST10-VIEWPORT.md` | `docs/design/STANDALONE-TEST10-VIEWPORT.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/STANDALONE-TEST11-LANGUAGE.md` | `docs/design/STANDALONE-TEST11-LANGUAGE.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/STANDALONE-TEST7.md` | `docs/design/STANDALONE-TEST7.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/STANDALONE-TEST8-SYNC.md` | `docs/design/STANDALONE-TEST8-SYNC.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/STANDALONE-TEST9-PATH.md` | `docs/design/STANDALONE-TEST9-PATH.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST2.md` | `docs/design/TEST2.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST3.md` | `docs/design/TEST3.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST4.md` | `docs/design/TEST4.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST5.md` | `docs/design/TEST5.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST6-EMULATOR.md` | `docs/design/TEST6-EMULATOR.md` |  |
| `DH_sc-pr/compatibility/work/fold7-build/TEST7-EMULATOR.md` | `docs/design/TEST7-EMULATOR.md` |  |
| `DH_sc-pr/compatibility/work/README.txt` | `docs/design/historical/FOLD7-WORK-README.txt` | renamed |
| `DH_sc-pr/compatibility/work/README-ARM32-HISTORICAL.txt` | `docs/design/historical/README-ARM32-HISTORICAL.txt` | renamed |

### `evidence/` — Device and emulator evidence, validation logs and machine-readable reports (140 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/HANDOFF_VERIFICATION.json` | `evidence/HANDOFF_VERIFICATION.json` |  |
| `DH_sc-pr/compatibility/REFERENCE_RUNTIME_MANIFEST.json` | `evidence/REFERENCE_RUNTIME_MANIFEST.json` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/MANIFEST.json` | `evidence/emulator-test5/MANIFEST.json` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/command.json` | `evidence/emulator-test5/command.json` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/device-properties.txt` | `evidence/emulator-test5/device-properties.txt` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/initial-boot-logcat.txt` | `evidence/emulator-test5/initial-boot-logcat.txt` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/initial-emulator.log` | `evidence/emulator-test5/initial-emulator.log` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/install.txt` | `evidence/emulator-test5/install.txt` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/result.json` | `evidence/emulator-test5/result.json` |  |
| `DH_sc-pr/compatibility/evidence/emulator-test5/smoke-emulator.log` | `evidence/emulator-test5/smoke-emulator.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/apk-badging.txt` | `evidence/fold7-build/apk-badging.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build-result.json` | `evidence/fold7-build/build-result.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build-test3.log` | `evidence/fold7-build/build-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build-test4.log` | `evidence/fold7-build/build-test4.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build-test5.log` | `evidence/fold7-build/build-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/build.log` | `evidence/fold7-build/build.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/cache-tests.txt` | `evidence/fold7-build/cache-tests.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-abort.log` | `evidence/fold7-build/diagnostic-abort.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-abort.txt` | `evidence/fold7-build/diagnostic-abort.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-caught.log` | `evidence/fold7-build/diagnostic-caught.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-caught.txt` | `evidence/fold7-build/diagnostic-caught.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-exit.log` | `evidence/fold7-build/diagnostic-exit.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-exit.txt` | `evidence/fold7-build/diagnostic-exit.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-fault.log` | `evidence/fold7-build/diagnostic-fault.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-fault.txt` | `evidence/fold7-build/diagnostic-fault.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-tests.json` | `evidence/fold7-build/diagnostic-tests.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-tests.log` | `evidence/fold7-build/diagnostic-tests.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-threads.log` | `evidence/fold7-build/diagnostic-threads.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/diagnostic-threads.txt` | `evidence/fold7-build/diagnostic-threads.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/download-hashes.json` | `evidence/fold7-build/download-hashes.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/emulator-smoke-test5.json` | `evidence/fold7-build/emulator-smoke-test5.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-baseline-directory.log` | `evidence/fold7-build/file-open-baseline-directory.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-baseline-directory.txt` | `evidence/fold7-build/file-open-baseline-directory.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-baseline-file.log` | `evidence/fold7-build/file-open-baseline-file.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-baseline-file.txt` | `evidence/fold7-build/file-open-baseline-file.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-directory.log` | `evidence/fold7-build/file-open-fixed-directory.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-directory.txt` | `evidence/fold7-build/file-open-fixed-directory.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-file.log` | `evidence/fold7-build/file-open-fixed-file.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-file.txt` | `evidence/fold7-build/file-open-fixed-file.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-missing.log` | `evidence/fold7-build/file-open-fixed-missing.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-fixed-missing.txt` | `evidence/fold7-build/file-open-fixed-missing.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-tests-test5.log` | `evidence/fold7-build/file-open-tests-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-tests.json` | `evidence/fold7-build/file-open-tests.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-open-tests.log` | `evidence/fold7-build/file-open-tests.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-probe-0.txt` | `evidence/fold7-build/file-probe-0.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-probe-1.txt` | `evidence/fold7-build/file-probe-1.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/file-probe-2.txt` | `evidence/fold7-build/file-probe-2.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-build-test3.log` | `evidence/fold7-build/game-build-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-build-test4.log` | `evidence/fold7-build/game-build-test4.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-build-test5.log` | `evidence/fold7-build/game-build-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/game-build.log` | `evidence/fold7-build/game-build.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/media-query-packaged-smali.txt` | `evidence/fold7-build/media-query-packaged-smali.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/media-query-tests-test3.txt` | `evidence/fold7-build/media-query-tests-test3.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/media-query-tests.txt` | `evidence/fold7-build/media-query-tests.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-fixups.log` | `evidence/fold7-build/native-fixups.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-load-before-import-fix.log` | `evidence/fold7-build/native-load-before-import-fix.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-load-test.log` | `evidence/fold7-build/native-load-test.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-probe-test3.log` | `evidence/fold7-build/native-probe-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-probe-test4.log` | `evidence/fold7-build/native-probe-test4.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/native-probe-test5.log` | `evidence/fold7-build/native-probe-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package-check-test3.log` | `evidence/fold7-build/package-check-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package-check-test4.log` | `evidence/fold7-build/package-check-test4.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package-check-test5.log` | `evidence/fold7-build/package-check-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/package-check.log` | `evidence/fold7-build/package-check.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch-test3.log` | `evidence/fold7-build/patch-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch-test4.log` | `evidence/fold7-build/patch-test4.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch-test5.log` | `evidence/fold7-build/patch-test5.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/patch.log` | `evidence/fold7-build/patch.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test4-phone-absolute.log` | `evidence/fold7-build/path-open-test4-phone-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test4-phone-absolute.txt` | `evidence/fold7-build/path-open-test4-phone-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test4-rooted-absolute.log` | `evidence/fold7-build/path-open-test4-rooted-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test4-rooted-absolute.txt` | `evidence/fold7-build/path-open-test4-rooted-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-colon-path.log` | `evidence/fold7-build/path-open-test5-colon-path.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-colon-path.txt` | `evidence/fold7-build/path-open-test5-colon-path.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-directory.log` | `evidence/fold7-build/path-open-test5-directory.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-directory.txt` | `evidence/fold7-build/path-open-test5-directory.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-empty-root-absolute.log` | `evidence/fold7-build/path-open-test5-empty-root-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-empty-root-absolute.txt` | `evidence/fold7-build/path-open-test5-empty-root-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-missing-absolute.log` | `evidence/fold7-build/path-open-test5-missing-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-missing-absolute.txt` | `evidence/fold7-build/path-open-test5-missing-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-missing-relative.log` | `evidence/fold7-build/path-open-test5-missing-relative.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-missing-relative.txt` | `evidence/fold7-build/path-open-test5-missing-relative.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-phone-absolute.log` | `evidence/fold7-build/path-open-test5-phone-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-phone-absolute.txt` | `evidence/fold7-build/path-open-test5-phone-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-phone-relative.log` | `evidence/fold7-build/path-open-test5-phone-relative.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-phone-relative.txt` | `evidence/fold7-build/path-open-test5-phone-relative.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-relative-root-without-slash.log` | `evidence/fold7-build/path-open-test5-relative-root-without-slash.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-relative-root-without-slash.txt` | `evidence/fold7-build/path-open-test5-relative-root-without-slash.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-root-without-slash.log` | `evidence/fold7-build/path-open-test5-root-without-slash.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-root-without-slash.txt` | `evidence/fold7-build/path-open-test5-root-without-slash.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-rooted-absolute.log` | `evidence/fold7-build/path-open-test5-rooted-absolute.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-rooted-absolute.txt` | `evidence/fold7-build/path-open-test5-rooted-absolute.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-rooted-relative.log` | `evidence/fold7-build/path-open-test5-rooted-relative.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-test5-rooted-relative.txt` | `evidence/fold7-build/path-open-test5-rooted-relative.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-tests-final-smoke.json` | `evidence/fold7-build/path-open-tests-final-smoke.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-tests-final-smoke.log` | `evidence/fold7-build/path-open-tests-final-smoke.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-tests.json` | `evidence/fold7-build/path-open-tests.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/path-open-tests.log` | `evidence/fold7-build/path-open-tests.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-report-test1-excerpt.txt` | `evidence/fold7-build/phone-report-test1-excerpt.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-report-test2-excerpt.txt` | `evidence/fold7-build/phone-report-test2-excerpt.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/runner-smoke.log` | `evidence/fold7-build/runner-smoke.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/runtime-build-final.txt` | `evidence/fold7-build/runtime-build-final.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/runtime-build-test3.log` | `evidence/fold7-build/runtime-build-test3.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/runtime-manifest.json` | `evidence/fold7-build/runtime-manifest.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test2-dex-inspection.log` | `evidence/fold7-build/test2-dex-inspection.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test2-regression-results.json` | `evidence/fold7-build/test2-regression-results.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test3-dex-inspection.log` | `evidence/fold7-build/test3-dex-inspection.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test3-packaged-smali-checks.txt` | `evidence/fold7-build/test3-packaged-smali-checks.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test3-signature-check.txt` | `evidence/fold7-build/test3-signature-check.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test4-payload-check.txt` | `evidence/fold7-build/test4-payload-check.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test4-restore-verification.txt` | `evidence/fold7-build/test4-restore-verification.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test4-signature-check.txt` | `evidence/fold7-build/test4-signature-check.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test5-dex-inspection.log` | `evidence/fold7-build/test5-dex-inspection.log` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test5-signature-check.txt` | `evidence/fold7-build/test5-signature-check.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/test5-upgrade-check.txt` | `evidence/fold7-build/test5-upgrade-check.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/validation.json` | `evidence/fold7-build/validation.json` |  |
| `DH_sc-pr/compatibility/work/compatibility.patch` | `evidence/legacy-arm32/compatibility.patch` |  |
| `DH_sc-pr/compatibility/work/decompilation-errors.json` | `evidence/legacy-arm32/decompilation-errors.json` |  |
| `DH_sc-pr/compatibility/work/original-audit.json` | `evidence/legacy-arm32/original-audit.json` |  |
| `DH_sc-pr/compatibility/work/patch-summary.json` | `evidence/legacy-arm32/patch-summary.json` |  |
| `DH_sc-pr/compatibility/work/signature-verification.txt` | `evidence/legacy-arm32/signature-verification.txt` |  |
| `DH_sc-pr/compatibility/work/tool-manifest.json` | `evidence/legacy-arm32/tool-manifest.json` |  |
| `DH_sc-pr/compatibility/work/validation.json` | `evidence/legacy-arm32/validation.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/dh2-events.txt` | `evidence/phone-test3/dh2-events.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/dh2-exits.txt` | `evidence/phone-test3/dh2-exits.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/dh2-logcat.txt` | `evidence/phone-test3/dh2-logcat.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/dh2-media-status.txt` | `evidence/phone-test3/dh2-media-status.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/dh2-session.txt` | `evidence/phone-test3/dh2-session.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/previous-run/dh2-exits.txt` | `evidence/phone-test3/previous-run/dh2-exits.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/stack-symbols.json` | `evidence/phone-test3/stack-symbols.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/stack-symbols.txt` | `evidence/phone-test3/stack-symbols.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test3/zb-runtime-report.txt` | `evidence/phone-test3/zb-runtime-report.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/dh2-events.txt` | `evidence/phone-test4/dh2-events.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/dh2-exits.txt` | `evidence/phone-test4/dh2-exits.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/dh2-logcat.txt` | `evidence/phone-test4/dh2-logcat.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/dh2-media-status.txt` | `evidence/phone-test4/dh2-media-status.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/dh2-session.txt` | `evidence/phone-test4/dh2-session.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/previous-run/dh2-exits.txt` | `evidence/phone-test4/previous-run/dh2-exits.txt` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/stack-symbols.json` | `evidence/phone-test4/stack-symbols.json` |  |
| `DH_sc-pr/compatibility/work/fold7-build/phone-test4/zb-runtime-report.txt` | `evidence/phone-test4/zb-runtime-report.txt` |  |

### `notices/` — Upstream licence text and the notice index (1 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/work/fold7-build/UPSTREAM-LICENSE.txt` | `notices/UPSTREAM-LICENSE.txt` | renamed |

### `upstream-modified/` — Readable copies of every upstream ZettaBridge file the DH2 patch touches (11 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/compatibility/upstream-modified/AGENTS.md` | `upstream-modified/AGENTS.md` |  |
| `DH_sc-pr/compatibility/upstream-modified/LICENSE` | `upstream-modified/LICENSE` |  |
| `DH_sc-pr/compatibility/upstream-modified/android/launcher/app/src/main/java/com/zettabridge/core/ZBridge.java` | `upstream-modified/android/launcher/app/src/main/java/com/zettabridge/core/ZBridge.java` |  |
| `DH_sc-pr/compatibility/upstream-modified/android/launcher/app/src/main/java/com/zettabridge/launcher/LoadedPlugin.java` | `upstream-modified/android/launcher/app/src/main/java/com/zettabridge/launcher/LoadedPlugin.java` |  |
| `DH_sc-pr/compatibility/upstream-modified/cli/zbrun/main.cpp` | `upstream-modified/cli/zbrun/main.cpp` |  |
| `DH_sc-pr/compatibility/upstream-modified/core/android/guest_jni_runtime.h` | `upstream-modified/core/android/guest_jni_runtime.h` |  |
| `DH_sc-pr/compatibility/upstream-modified/core/android/zbridge_jni.cpp` | `upstream-modified/core/android/zbridge_jni.cpp` |  |
| `DH_sc-pr/compatibility/upstream-modified/third_party/README.md` | `upstream-modified/third_party/README.md` |  |
| `DH_sc-pr/compatibility/upstream-modified/third_party/patches/dynarmic-0001-thumb32-armv8.patch` | `upstream-modified/third_party/patches/dynarmic-0001-thumb32-armv8.patch` |  |
| `DH_sc-pr/compatibility/upstream-modified/third_party/patches/dynarmic-0002-asimd-narrowing.patch` | `upstream-modified/third_party/patches/dynarmic-0002-asimd-narrowing.patch` |  |
| `DH_sc-pr/compatibility/upstream-modified/tools/build_guest.sh` | `upstream-modified/tools/build_guest.sh` |  |

### `RIGHTS.md` — Provenance and rights statement (1 files)

| Original path (`DH_sc-pr/`) | New path (`DH2Work/`) | |
| --- | --- | --- |
| `DH_sc-pr/RIGHTS.md` | `RIGHTS.md` |  |

Total mapped files: **240**.

## 5. Deliberately not imported

| Excluded from `DH_sc` / `DH_sc-pr` | Reason |
| --- | --- |
| `port/`, `recovered/` (incl. `*.pseudo.c`, assembly and decompiler dumps) | The separate full-source-reconstruction track; explicitly out of scope for DH2Work |
| `compatibility-work-test2..5.zip` | Bulk historical snapshots of the work tree; superseded by this tree and by the private restore recipe |
| `compatibility/work/native-port/` (four-function ARM64 prototype, engine inventory/audit extracts) | Prototype of translating engine functions to ARM64, i.e. the reconstruction direction, not the wrapper app |
| `tools/recover_*.py`, `tools/trace_*.py`, `tools/verify_*import*.py`, `tools/ghidra/`, `tools/tests/` | Recovery/tracing tooling for the reconstruction track |
| repository root `docs/` (`PORTING.md`, `RECOVERY-*`, reconstruction `STATUS.md`, ...), `reports/` | Reconstruction-track documentation and traces |
| `LOCAL_AGENT_HANDOFF.md`, `RECONSTRUCTION-HANDOFF.md`, `unpack_compatibility.py`, `tools/prepare_local_agent.py`, `requirements.txt` | Restore/bootstrap material for the reconstruction-track snapshot archives, which are not shipped here |
| `compatibility/work/decoded-original/`, `work/patched/`, `work/history/`, `work/decompiled-java/` | Original proprietary game material and historical ARM32 trees; private inputs, not source |
| `game-unsigned.apk`, `cache.zip`, `runtime-bundle.zip`, `*.p12` | Private game inputs, prebuilt runtime and development signing material (see `.gitignore`) |

Two notes on that list:

* `upstream-modified/**` was requested in full and is imported in full, including
  `upstream-modified/AGENTS.md` (110 KB of upstream agent documentation; it
  contains no game bytes).
* Nothing above was deleted from `DH_sc`/`DH_sc-pr`; it simply stays there.

Private inputs that must be supplied locally to build are listed in
`README.md` and in `docs/design/BUILDING.md`.
