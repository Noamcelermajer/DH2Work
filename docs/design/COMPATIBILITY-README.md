# Dungeon Hunter 2 — Fold7 compatibility work

## Current checkpoint: test 5

Test 4's device report confirms its directory guard ran, then shows a different failure while reopening the prince model: the cache root is duplicated. The original engine treats Android absolute paths as relative whenever WorkingDirectory is nonempty. A 20-byte ARM fix adds POSIX absolute-path recognition while retaining relative and colon-path behavior. The actual original/fixed engine tests confirm the failed open and correct byte reads after repair.

Thirteen path cases pass, as do five earlier file cases, native library/hook checks and final APK validation. The final model-open logging also passes two focused path probes. This is a new test APK; loading and gameplay on the Fold7 remain unverified. Russian menus, display behavior, the earlier GL error and 16 KB pages are still open. See [TEST5.md](work/fold7-build/TEST5.md).

[Download Test 5](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test5). Install over Test 4, keep the same settings and existing cache, repeat the load, then export DH2-test5-diagnostics.zip. Test 5 modifies the engine binary itself; earlier statements that the engine is unchanged are historical.

## Historical checkpoint: test 4

The owner's Test 3 diagnostics identify the original engine's CFile filename bounds check. An independent original-ARM32 caller probe reproduces the same abort with a trailing directory path. Test 4 adds a narrow engine fopen guard and persistent STL error logging. All five file-opening regressions, native library/hook probes and APK checks pass. The exact phone path and loading-screen repair still require a Fold7 retest. See [TEST4.md](work/fold7-build/TEST4.md) and [BRIDGE-ASSESSMENT.md](work/fold7-build/BRIDGE-ASSESSMENT.md).

[Download test 4](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test4). Install over Test 3, retain cache and saves, repeat the same load with the same options, then export diagnostics. Historical sections below remain for provenance.

## Historical checkpoint: test 3

The owner's test 2 completes the cinematic and aborts at the subsequent fairy loading screen. Test 3 adds persistent native diagnostics and ZIP export, with optional language/display/context settings. The crash cause and gameplay stability remain unconfirmed. Read [TEST3.md](work/fold7-build/TEST3.md) for the exact changes and phone procedure, and [BRIDGE-ASSESSMENT.md](work/fold7-build/BRIDGE-ASSESSMENT.md) for the independent compatibility assessment. Install test 3 over test 2 and export the diagnostic ZIP after reproducing the failure; retain the existing cache.

[Download test 3](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test3). Older sections below preserve the earlier investigation.

This directory preserves the work from the compatibility-build investigation, including the exact original decoded Android layer, historical ARM32 changes, the ARM64-hosted wrapper, native fixes, independent ARM64 assembly prototypes, and their recorded tests.

**Current result: an installable test APK, not a device-validated playable port.** It uses ZettaBridge/Dynarmic ARM32 translation with targeted assembly/C repairs. It is not a full manual translation of the engine.

Download the APK and source archives from [Fold7 test 1](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test1).

| Material | Location |
| --- | --- |
| Findings and native crash fixes | [FINDINGS.md](FINDINGS.md) |
| Open issues and acceptance gates | [ISSUES.md](ISSUES.md) |
| Phone test procedure and result template | [DEVICE-TESTING.md](DEVICE-TESTING.md) |
| Rebuild and dependency setup | [BUILDING.md](BUILDING.md), [work/fold7-build/README.txt](work/fold7-build/README.txt) |
| New Java UI, cache importer, guest path helper | [work/fold7-build](work/fold7-build) |
| Hash-pinned native assembly/C fixes | `work/fold7-build/storm_*`, `patch_storm.py` |
| Runtime changes | [work/fold7-build/zettabridge-dh2.patch](work/fold7-build/zettabridge-dh2.patch), [upstream-modified](upstream-modified) |
| Original decoded APK and decompiled Java | [work/decoded-original](work/decoded-original), [work/decompiled-java](work/decompiled-java) |
| Historical ARM32 compatibility tree | [work/patched](work/patched), [work/compatibility.patch](work/compatibility.patch) |
| Final generated game smali / manifests | `work/fold7-build/generated-game-smali`, `*-AndroidManifest.xml` |
| Independent four-function ARM64 prototype | [work/native-port](work/native-port) |
| Recorded validation and failures | `work/fold7-build/*test*.log`, `validation.json`, `cache-tests.txt`, [work/history](work/history) |
| File-by-file provenance | [FILE-MANIFEST.json](FILE-MANIFEST.json) |
| Inventory boundaries and rights | [CONTENTS.md](CONTENTS.md), [work/notices](work/notices) |

## Test 2 and the phone report

[Read the crash diagnosis, fix and validation](work/fold7-build/TEST2.md). Test 1 reached native initialization on SM-F966B / Android 16 with 4096-byte pages, then failed in the personal-playlist MediaStore query. Test 2 fixes that query and retains the existing cache when installed over test 1. Download [test 2](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test2).

## Install and cache

1. Install `Dungeon-Hunter-2-Fold7-test.apk`. Package `local.dh2.fold7` installs alongside the original game.
2. Keep the complete original cache ZIP in Downloads.
3. Open the test app, wait for preparation, tap **Import cache ZIP**, select the archive, and then **Launch game**.
4. If it fails, reopen the app and use **View / share diagnostic report**.

The app chooses and displays its cache directory. On the primary Android user it is normally:

```text
/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/
```

No manual Android/data access is required. Reimport replaces the test application's external cache folder, so retain the original ZIP. A complete compatible cache is still required; this investigation did not inspect or test the uploaded cache.

**This build requires 4 KB host memory pages.** It detects and refuses unsupported sizes. Its 16 KB ELF alignment does not make the runtime's memory mapper compatible with a 16 KB host.

The local development signing key included in the work snapshot is deliberately a test-only key, not the publisher key. Do not use it for production signing. Original game rights and third-party licenses remain applicable; this is not a blanket open-source license.
