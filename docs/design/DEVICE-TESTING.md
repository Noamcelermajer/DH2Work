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

# Fold7 device acceptance procedure

This is the remaining gate for the user-requested working app. No row below has a phone pass result yet.

Record phone model, One UI version, Android build/security patch, CPU ABIs, host page size, APK SHA-256, cache SHA-256/size, available storage, and whether the test uses the cover or inner screen. The setup app's diagnostic report supplies several of these.

| Order | Action | Expected result | Evidence to retain |
| --- | --- | --- | --- |
| 1 | Install APK without removing the original game | Separate Fold7 Test app appears and opens | Install error or setup screenshot |
| 2 | Wait for preparation and open diagnostics | Libraries prepare; page size is 4096 | Full diagnostic report |
| 3 | Import complete cache ZIP from Downloads | Import completes; destination is shown | Cache hash, file layout, import message |
| 4 | Tap Launch game | Game activity reaches its menu | Video/screenshot and logs on failure |
| 5 | Start a new game and enter a level | Correct textures, UI, lighting and character rendering | Screenshot of any missing/incorrect graphics |
| 6 | Exercise movement, combat, inventory and menus | Touch coordinates and UI actions are correct | Reproduction for dropped/misaligned input |
| 7 | Play with sound; change volume and background/resume | Music/effects work and recover | Audio symptom and lifecycle sequence |
| 8 | Save, exit, force-stop app, reopen and load | Progress/preferences persist | Before/after saved state |
| 9 | Switch between cover and inner screens, fold/unfold and rotate where allowed | Activity remains usable; surface and touch recover | Sequence and screen recording |
| 10 | Lock/unlock phone; switch apps; return after memory pressure | No crash or unrecoverable black screen | Diagnostic report and lifecycle details |
| 11 | Run at least 30 minutes and transition through multiple areas | Stable gameplay with no sustained stalls or memory failures | Duration, battery/thermal observations, last action |
| 12 | Test offline/online startup and intended legitimate service flows | Behavior is understood; obsolete-service failures are separated from local crashes | Endpoint/error information, without credentials |
| 13 | Exercise multiplayer only after single-player passes | Permissions and connection behavior are documented | Permission prompts and connection results |

If the app reports a non-4096 page size, record it and stop the gameplay acceptance run. The existing build cannot resolve that issue through cache placement.

If it crashes, reopen **View / share diagnostic report**. Include the first failing step, what appeared onscreen, and whether it happened before or after cache import. With ADB available, collect a time-bounded logcat around reproduction; review it for unrelated personal information before sharing publicly.

## Result template

```text
APK SHA-256:
Phone/model:
Android / One UI / build:
Page size:
Cache SHA-256 / bytes / layout:
Screen (cover/inner):
First failing step:
Expected:
Observed:
Repeatable (attempts/failures):
Diagnostic report:
Supporting screenshots/logs:
```

Completion means the required device cases pass with recorded evidence, plus fixes and reruns for any actual failures. The existing host tests cannot establish this result.
