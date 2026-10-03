# Standalone Test 8 save-job experiment

Test 8 changes only one ARM32 engine instruction so routine save jobs use an
existing synchronous path. The source and Android 9 direct-guest result are in
`compatibility/thread-exhaustion/`. That result reached interactive 3D play on
Android 9; it does not establish Android 17 wrapper behavior.

The optional wrapper build uses `DH2_TEST8_GUEST_APK` instead of
`DH2_TEST7_GUEST_APK`, with the same exact owner cache input and toolchain
requirements described in `STANDALONE-TEST7.md`. The input must be the
unsigned Test 8 guest with SHA-256
`8168af36b2d82cf6b897da2fe4ec382c816f6498840e3bb61aede2f23c877e20`.
Its engine SHA-256 is
`ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f`;
Storm and `classes2.dex` are byte-identical to Test 7. The builder rejects
any other guest and produces versionCode 8, versionName `1.0-test8-sync`.

An earlier local signed Android 17 diagnostic wrapper has SHA-256
`5219ac796ee4df23f80ef8930b02ab066d9b6c155f7359d3a9554a03f827c67f`
and size 448,470,789 bytes. APK signature verification, alignment, unique
ZIP-entry names and CRCs passed. The exact Test 8 guest and complete cache
hashes were rechecked inside the signed APK. Game and cache bytes remain local,
outside Git.

This wrapper includes the same API 37 x86_64 native-bridge namespace search
path and ARM32 sysroot aliases as the Test 7 standalone build. Its purpose is
to compare one engine scheduling change while retaining the rest of the
Android 17 wrapper setup.

On the Android 17/API 37.0 4096-byte-page x86_64 emulator, a Test 8 run
passed the opening cinematic and title screen and remained alive for more than
six minutes beyond the Test 7 Berberis abort. After tapping to continue, it
selected the owner's existing `wolf` save. The first prince model opened, then
deferred loading tried a lowercased, duplicated cache root and failed:

```text
/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/
/storage/emulated/0/android/data/local.dh2.fold7/files/plugins/com.gameloft.android.gand.gloftd2ss/
data/3d/characters/prince/prince_modular.bdae
```

The engine aborted at `libDungeonHunter2.so` offset `0x60b2b8`. The full local
log is `work/standalone-build/test8-api37-menu2-logcat.txt`. No Android 17
level gameplay was validated in this run. The narrow source fix for this
wrapper-specific repeated root is in `storm_path_repair.h`; Test 9 includes a
rebuilt Storm library to evaluate it without modifying the Test 8 guest.
