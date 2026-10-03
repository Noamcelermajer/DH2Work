# Android emulator testing

The Test 5 release is published at `v1.0.2-fold7-test5`. Its application code and
APK are unchanged by this supplementary test setup.

## Observed result, 2026-10-01

Android SDK Platform Tools 37.0.1 and Android Emulator 37.2.12 were installed in
the Linux x86_64 workspace. The Google APIs Android 11 / API 30 x86_64 system
image, revision 16, reports `x86_64,x86,arm64-v8a,armeabi-v7a,armeabi` and
`libndk_translation.so` as its native bridge.

The workspace has no KVM acceleration or exposed hardware GPU. The AVD used
software CPU emulation and SwiftShader, with two virtual CPUs, 2048 MB RAM and
a 720x1280 display. It reached `sys.boot_completed=1` after 224.1 seconds in the
recorded smoke run. These conditions do not reproduce the Fold7's Android 16
system or graphics driver.

Installing the exact released APK with a full streamed transfer failed:

```
cmd: Failure calling service package: Broken pipe (32)
```

The game was not launched. This failure occurred in emulator APK installation;
it is not evidence of the game's model-loading crash. The attempt to capture
Android logcat also timed out. The result is recorded in
`emulator-smoke-test5.json`. Native file-path regression results in `TEST5.md`
remain separate from Android/UI testing.

Two previously uploaded complete cache ZIPs were found in the saved files, but
both transfer endpoints returned HTTP 502 / connection refused. The cache was
not available for an emulator loading test. Do not interpret this as a bad or
missing model in the user's phone cache.

## Repeat the launcher smoke test

Use Python 3.9 or later, Android SDK platform-tools and emulator, and an existing
AVD. The AVD must support the APK's ARM64 ABI. Prefer a host with working hardware
acceleration. Stop any existing instance of the chosen AVD first.

```sh
python tests/run_emulator_smoke.py \
  --sdk /path/to/android-sdk \
  --avd-home /path/to/avd-directory \
  --avd DH2_API30 \
  --apk /path/to/Dungeon-Hunter-2-Fold7-test5.apk \
  --out /path/to/new-smoke-results
```

Add `--software-cpu` only when deliberately testing without CPU acceleration.
The runner starts a headless emulator on port 5554 and runs adb in the same
process environment, which matters in workspaces with isolated networking.
It waits for Android boot, installs the APK, opens the launcher, checks for its
Ready status, and captures the screen/UI/logs when possible. It then stops the
emulator. Known output files in `--out` are replaced on each run.

This checks the launcher only. It does not import the game cache, execute the
native engine, test the failing character load, or validate gameplay. A
successful launcher result would not establish that the nested native
translation works on this emulator. Fold7 loading and gameplay still require
the device comparison described in `TEST5.md`.
