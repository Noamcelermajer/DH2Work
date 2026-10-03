> Test 2 update: the Fold7 report confirms 4096-byte pages and native library/JNI startup. A MediaStore query crash was identified and repaired; the repaired APK awaits a device retest. See [TEST2](work/fold7-build/TEST2.md). The initial test 1 evidence below is retained historically.

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

# Findings and evidence

## Inputs and architecture

The inspected APK is Dungeon Hunter 2 HD v1.0.2, package `com.gameloft.android.GAND.GloftD2SS`, SHA-256 `32c2d027b585a42547311cd95da6a3975fdb3174e663e513d42a7f49d1a4c200`.

All three game libraries are ARM32. The main engine contains 31,021 nonempty defined function symbols representing about 31,018 distinct address values. Its symbols are useful, but changing ELF machine type or the ABI directory cannot translate instructions, pointers, C++ object layouts or vtables. DWARF covers support code rather than the whole game implementation.

An independent prototype translates four small functions to ARM64 and has 45,330 differential cases recorded in `work/native-port/translation-results.json`. That proves only those tested routines. The installed package instead hosts the original ARM32 engine in an ARM64 translation runtime.

## Two observed native initialization crashes

Both were in `libStormGLOFT.so`, whose hooks treated `dlopen` results as pointers to old private bionic `soinfo` fields. Modern linker handles are opaque. The host probe reproduced crashes before the fixes.

| Fault | Original location | Repair |
| --- | --- | --- |
| Inline hook reads `handle + 0x8c` as load bias | instruction VA `0x438e4`, function `0x43894` | ARM32 PIC stub resolves exported `JNI_OnLoad` and subtracts the exact engine symbol VA `0x53224c` |
| Import hook reads private linker symbol/hash/relocation fields | function VA `0x371e4` | Freestanding C replacement walks the engine's public ELF program headers, `PT_DYNAMIC`, symbol/string tables and `R_ARM_JUMP_SLOT` relocations |

The replacements occupy verified zero padding starting at Storm VA `0xd3800`. The RX segment ends at `0xd3a7b` after the patch. The exact target has a writable GOT and no GNU_RELRO. These details are specific to this binary pair; the patch checks the input hashes and original instruction bytes.

| Library | SHA-256 |
| --- | --- |
| Original/unchanged engine | `36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80` |
| Original Storm | `be6beaab782944e5ce39cca8e850fd03de654e4236c9329621043adcbee291e1` |
| Patched Storm | `d7598baec2cb3267a6b41253678623ee0fc5d9e75a58d43b5c476e1140744fb0` |

`work/fold7-build/native-load-before-import-fix.log` preserves the remaining crash after the first fix. `native-load-test.log` records the successful probe after both repairs. The probe checks installation of `glShaderSource`, `glGetString`, and the original engine inline hook; graphics rendering itself was not exercised.

## Java, storage and runtime integration

- Guarded 11 restricted telephony calls in seven classes in the earlier ARM32 experiment.
- Changed 31 legacy absolute-path lookups to resolve against the actual game Context.
- Initialized `DungeonHunter2Prefs/SDFolder` and fixed empty-string content comparison.
- Added native aliases for original private library, private data and cache paths.
- Added a document-picker ZIP importer with staging, rollback, wrapper-directory detection, path validation and expansion limits.
- Added a separate setup activity, runtime/page-size checks and user-triggered diagnostics.
- Preserved existing licensing/billing decisions. No remote game service was tested.

## Validation and its limits

| Evidence | Result | Limit |
| --- | --- | --- |
| Complete ARM64 runtime build, guest libraries and APK package | Passed | Compilation alone is not gameplay |
| All three game libraries under QEMU-hosted ARM64 translator | Loaded | Reference Linux host, not the phone |
| Engine and Storm `JNI_OnLoad` with inert VM value | Passed | Real ART JNI bridge remains untested |
| Original engine integer/Lua helper checks | 4,096 inputs passed | Small helper coverage only |
| Graphics hook installation | Passed | No actual EGL/GPU rendering |
| Cache archive unit tests | 13 passed | Actual game cache not exercised |
| Package verification | 17 checks passed | Does not validate Android lifecycle behavior |
| Signing / APK alignment | Verified | Test key; real installation still needed |
| Fold7 gameplay, save/load, audio, folding | Not run | Requires actual device testing |

Exact output hashes and machine-readable booleans are in `work/fold7-build/validation.json`. Reports remain historical evidence from the original build, including their original absolute workspace paths.
