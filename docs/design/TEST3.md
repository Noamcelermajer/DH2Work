# Fold7 test 3: capture the post-cinematic loading abort

Test 2 reaches the cinematic on SM-F966B / Android 16 / ARM64 / 4096-byte pages. The owner clarified that **the cinematic completes; the game crashes during the following loading screen when a small fairy appears**. The cinematic itself is not the reported failure. Test 3 is a diagnostic and compatibility-option build; the loading crash is not yet claimed fixed.

## Evidence and limits

The phone recorded 322 `nativeRender` calls, 18,078 GL calls with a current context, 36 registered natives, zero unimplemented host calls and `guest exited with status 134`. The latest recorded successful cache open was `data/PyData/character_properties_pystructnames.bin`. This does not establish that this file is corrupt or that the cache is complete. The four libdl/libc failures were earlier search-path probes, not proof of a missing runtime library.

Status 134 is consistent with SIGABRT. Source inspection found the bridge's pending-signal termination path only recorded the numeric exit and skipped the existing register dump. A multithreaded `exit_group` also exited before persisting its status. Both reporting gaps are corrected. These changes preserve guest termination; they do not suppress an assertion or bypass an error.

The game uses Java `GLSurfaceView`, which creates/presents its EGL context outside the translated EGL wrappers. Consequently zero bridge-counted swaps and `current=0 gl=...` did not establish a graphics-thread fault. Test 3 labels this observation limit explicitly and records Java renderer milestones separately. A returned render callback is not proof of a displayed frame.

## Changes

- Native abort/nonzero-exit reports now include ARM32 registers, module-relative PC/LR, stack bytes and at most 16 executable-address candidates from the stack. Candidates are explicitly not an unwound backtrace. Existing fault reports retain their first state.
- A bounded 64-message tail captures guest stdout/stderr writes and Android liblog `writev` packets to the verified logd socket. Critical messages are persisted immediately; ordinary log updates are throttled. This remains useful if app logcat is unavailable. It does not capture messages liblog filters out before issuing a write.
- Recent distinct failed asset opens replace the old four-entry, first-failures-only list.
- App-UID logcat capture rotates three roughly 1 MiB files. Android exit history and available native/ANR traces are collected on return to the launcher. A translated guest abort often exits the host normally with a status and therefore need not produce an Android native tombstone; the guest report is essential.
- Guest events record language selection, surface creation and dimensions, initialization/resize return, frame milestones, lifecycle callbacks, and the size/hash of selected small character-data files. A missing speculative companion file is only a probe result, not a declared cache error.
- **Export diagnostic ZIP** saves logs via Android's document picker. Reopening the launcher keeps the last failed run. Starting a fresh run preserves one previous run. Export before running again if possible. No root, READ_LOGS permission or broad storage access is requested.
- The same package/signing key and versionCode 3 support installing over test 2. Existing cache and saves remain in place.

## Selectable compatibility settings

**Prefer English** is enabled by default. It makes the original `Get_PhoneLanguage` callback return its existing English value, 0, and records the original value. Inspection found the Java callback already defaults unsupported phone languages to English; it has no Russian branch. Native saved preferences or localized cache content may still override this. The option does not replace localized assets or edit saves.

**Fit game to 16:9** is experimental and off by default. The supplied Storm native patch has `orig_width=1280.0` and `orig_height=720.0`; it overrides the engine's reported phone dimensions and has viewport/scissor hooks. A fitted, centered view avoids stretching the entire square inner Fold display. When enabled, the original native phone-size callback is updated with the actual surface size before the original resize callback, so its scaling inputs refer to the fitted view. Touch alignment, clipping and fold transitions still require device testing.

**Keep graphics context during cinematics** is off by default, preserving the test 2 baseline after the owner's clarification. It requests `setPreserveEGLContextOnPause(true)` when selected. Two GLMediaPlayer native initializations in test 2 make context recreation worth observing, but do not establish it as the crash cause. Android may still recreate a lost context.

## Validation

- Five real ARM32 probes ran through the rebuilt ARM64 translator under QEMU: abort, multithreaded abort, multithreaded exit(42), caught SIGABRT returning normally, and SIGSEGV. Correct statuses and persistent state/log markers were checked. The handled signal remained handled.
- The original complete engine and patched Storm library still load; both JNI_OnLoad entry points pass with an inert VM; 4096 original engine helper inputs and the installed native hook checks pass. These are not ART or gameplay tests.
- Seven media-query regressions still pass.
- The signed APK's CRCs, nested libraries, ARM64 host, runtime bundle, upgrade version and helper references are checked. Packaged DEX is decoded again to inspect the actual hooks. Test 2 and test 3 signing-certificate digests match.
- Packaging validation caught an incomplete intermediate APK and a temporary bundle-copy artifact. The final build was serialized, rebuilt from the complete nested APK, checked again, and the packager now rejects incomplete input before appending its helper DEX.
- No Fold7 or Android GPU is attached to this environment. ZIP export, One UI UI/layout, audio continuity, actual presentation, game loading and sustained gameplay remain device acceptance items.

## Next phone run

1. Install test 3 over test 2; do not uninstall or reimport the cache.
2. Keep both display/context checkboxes off for the first run. Prefer English can remain on.
3. Reproduce the fairy loading-screen crash. Reopen the launcher and choose **Export diagnostic ZIP**. Save it in Downloads and attach it to the conversation.
4. Include a screenshot of the resolution problem, and whether the phone was folded or unfolded. If testing 16:9 separately, export the baseline first and change only that option.
5. If the crash persists, use the recorded fatal message, ARM32 PC/LR and module offsets to isolate the failing routine and test a targeted repair. Do not simply ignore aborts or infer an incomplete cache from the last successful open.

Cache remains:
`/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/`

Android API references used for the implementation:
- https://developer.android.com/reference/android/app/ApplicationExitInfo
- https://developer.android.com/reference/android/app/ActivityManager#getHistoricalProcessExitReasons(java.lang.String,int,int)
- https://developer.android.com/reference/android/opengl/GLSurfaceView#setPreserveEGLContextOnPause(boolean)
