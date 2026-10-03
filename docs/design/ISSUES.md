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

# Open issues and acceptance gates

Status as of Fold7 test 1, 2026-10-01. No actual phone result has been supplied in this thread.

| ID | Priority | Issue | Evidence / next action | Completion condition |
| --- | --- | --- | --- | --- |
| DH2-01 | Blocker | Host page-size compatibility | Runtime maps 4 KB guest pages directly. Read the diagnostic report's page size. If 16 KB, redesign guest memory mapping/protection and validate translated execution. | Successful native execution on the phone's actual page size |
| DH2-02 | Blocker | Android framework and ART/JNI integration | Host probe used an inert VM. Install, prepare, import, launch; capture Java/native reports and logcat where possible. | Real guest activity, JNI callbacks and lifecycle reach the game menu without errors |
| DH2-03 | Blocker | Correct, complete cache | Importer's unit tests do not establish asset completeness or texture compatibility. Use the complete original archive and record its hash/layout. | Game loads menu and levels without missing/corrupt assets |
| DH2-04 | High | EGL/GLES/shader/texture compatibility | Hooks install in host tests; no GPU rendering tested. Record first frame, renderer strings, shader errors and visual defects. | Correct menu and level rendering on both screens |
| DH2-05 | High | Input and Fold7 lifecycle | Touch, back, resize, fold/unfold, rotation, background/resume and process recreation have not been exercised. | No stuck input, lost surface, crash or lost progress in the device checklist |
| DH2-06 | High | Persistence | Private paths were mapped but real saves/preferences were not round-tripped on Android. | New game, save, force-stop and reload retain progress |
| DH2-07 | High | Audio and sustained performance | Sound, mixer initialization, frame pacing, thermal and memory behavior need a phone. | Audible correct output and stable sustained gameplay |
| DH2-08 | Medium | Legacy licensing/network services | Original decisions remain intact. Old endpoints and identity assumptions may fail independently of CPU compatibility. | Intended legitimate user flow is tested and failures documented |
| DH2-09 | Medium | Bluetooth/multiplayer permissions | Legacy declarations exist; modern runtime permissions and multiplayer are untested. | Supported multiplayer flow succeeds, or feature limitations are clearly documented |
| DH2-10 | Medium | Rebuild portability | Build scripts use a fixed local directory layout and downloadable toolchains. Upstream commit is pinned. A clean second-machine rebuild was not run. | Independent rebuild with recorded versions and equivalent package validation |
| DH2-11 | Scope | Full native ARM64 rewrite | Four helper routines are translated; the complete engine is not. | Separate subsystem reconstruction effort with ABI/layout/behavior tests |

These IDs are a tracked engineering checklist in the repository, not a claim that corresponding GitHub issue objects have already been created. A failure should retain its symptom, device build, report, cache hash, exact APK hash and smallest reproduction. Do not mark an issue resolved based only on a successful build.
