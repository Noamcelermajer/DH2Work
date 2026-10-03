# Fold7 test 2 — fix the reported MediaStore startup crash

The owner's test 1 report from 2026-10-01 establishes real-device progress on **SM-F966B, Android 16, arm64-v8a, 4096-byte pages**. All three libraries loaded, both recorded game/Storm JNI initializers succeeded, 36 natives registered, and no unimplemented host call or guest exit was recorded. Two Samsung license-check JNI calls ran. There were zero GL calls and zero EGL swaps.

The activity then failed in `Musicplayer.initMediaList` with `IllegalArgumentException: Invalid column *`. It happened twice at 20:49:23 and 20:49:27 in the supplied report. The `libdl.so` / `libc.so` failed-open entries are search attempts; the report also records successful runtime initialization. They are not the exception causing this startup failure.

## Change

The original query passes a one-element projection containing `*`. Android's projection argument expects column names. The legacy consumer only reads `_id` and `name`, so test 2 requests those columns explicitly through `ContentResolver.query`.

The new `MediaQueries` helper copies the result to a small owned cursor and closes the provider cursor. Null results, denied media access, invalid schema and database errors produce an empty non-null playlist cursor. This allows the old constructor to initialize its arrays and still call `nativeInitplayer`. It handles the user's optional personal music library; it does not replace the game's cache audio or bypass licensing.

The host version is now `1.0-test2` / versionCode 2. It uses the same package and signing certificate as test 1. The preparation revision changes so the bundled repaired game is reimported. PluginStore preserves the plugin's private `data` directory, and the existing external cache is retained. The setup screen and diagnostic header identify test 2 and include a media-query status file.

## Checks performed

- Seven host regression cases cover valid rows, explicit projection, null provider result, permission denial, invalid schema, database failure and cursor cleanup, including a failure during iteration. These use provider doubles, not an Android emulator.
- The compiled nested DEX is decoded again to verify the call to `MediaQueries.queryPlaylists`, absence of `*` / `managedQuery` in initialization, and preservation of the native initializer call.
- All guest native libraries and the host native runtime are compared against test 1 and remain byte-identical.
- Signing certificate equality, versionCode increase, APK alignment/signature and package structure are checked.

See `test2-regression-results.json`, `media-query-tests.txt`, `media-query-packaged-smali.txt` and `validation.json` for exact evidence.

## Install / next device check

Install test 2 **over test 1 without uninstalling or clearing app data**. Open the setup app, wait for preparation, then tap Launch game. An already imported cache does not need to be imported again.

The reported exception is fixed in code and verified in the package; the repaired build still requires a phone retest. If another failure occurs, send the new report headed `DH2 Fold7 test 2`, including `dh2-media-status.txt`. Old timestamped test 1 errors may still appear in the historical error log. The next expected evidence is successful music initialization followed by the first GL/EGL calls or a concrete later startup error.

The phone report resolves the uncertainty about this device's 4 KB page size. It does not establish GPU compatibility, audio, save/load, input, folding behavior or gameplay.

API references: https://developer.android.com/reference/android/content/ContentResolver#query(android.net.Uri,java.lang.String[],java.lang.String,java.lang.String[],java.lang.String)
and https://developer.android.com/training/data-storage/shared/media
