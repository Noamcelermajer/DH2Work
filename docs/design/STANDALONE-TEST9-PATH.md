# Standalone Test 9 repeated-root path experiment

Test 9 retains Test 8's one-instruction synchronous save-job engine patch and
rebuilds only `libStormGLOFT.so` with a narrow model-path retry. The Android 17
Test 8 wrapper reached the title screen, then aborted when a deferred model
open repeated and lowercased the entire wrapper cache root. The original
`dh2_repeated_cache_root` rule accepted only a direct guest cache ending
`<package>/files/`. The revised rule also accepts the wrapper's exact
`<host>/files/plugins/com.gameloft.android.GAND.GloftD2SS/` suffix. It requires
the two roots to match case-insensitively and a child filename after them;
unrelated nested paths remain rejected. The source is in
`storm_path_repair.h`, with regression cases in
`tests/test_repeated_cache_root.c`.

The optional build input `DH2_TEST9_GUEST_APK` must point to the unsigned,
owner-derived Test 9 guest APK with SHA-256
`310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1`.
Its Storm SHA-256 is
`2489c037d75cd2a3c994b7bc349aac767c34f96acf25a79188a72c77dc7502de`.
The Test 8 engine and guest helper DEX are byte-identical. The builder pins the
guest and cache ZIP before signing a versionCode 9 wrapper. The private game
and cache bytes remain outside Git; see `RIGHTS.md` and
`STANDALONE-TEST7.md` for the local build prerequisites and provenance.

To reproduce the Test 9 guest, run the existing `patch_storm.py` from a
restored `compatibility/work/fold7-build` tree, supplying the owner's original
Storm and engine libraries, `--toolchain-bin` pointing to the Android NDK LLVM
bin directory, and `--output` for the rebuilt Storm library. Then run:

```text
python repack_test9_guest.py --test8-guest <pinned-test8-unsigned.apk> \
  --patched-storm <rebuilt-libStormGLOFT.so> \
  --output <test9-unsigned.apk>
```

The repacker checks both exact input hashes, ZIP CRCs, unique entries, the
single helper DEX, unchanged non-Storm entries and final Test 9 SHA-256.

The local signed Test 9 APK is
`work/standalone-build/compatibility/deliverables/Dungeon-Hunter-2-Android17-test9-path-cachemarker.apk`,
448,470,858 bytes, SHA-256
`5f6b31b8988231df0dc9987f656347aae940b462baed91d8e9cadb2de6eaa143`.
`apksigner verify` confirmed v3 signing, and `zipalign -c` passed. Its ZIP
entries are unique with valid CRCs; its bundled guest, cache ZIP and cache
revision asset match the pinned SHA-256 values. The clean build emitted 40
launcher class files and D8 produced one wrapper `classes.dex`. JDK 17's
`javac` still printed an `AccessDeniedException` while closing Android 37.2
`android.jar` despite exit code 0; bytecode generation, D8 and installation
were independently checked.

On a clean Android 17/API 37.0 x86_64 emulator with 4096-byte pages, the APK
installed with `--abi arm64-v8a`. Bundled cache import completed: 6,835 files
including generated options and the new completion marker, which contained
the pinned cache ZIP SHA-256. The setup UI showed "Bundled cache import
completed". The marker proves that this ZIP finished importing; it is not a
later cryptographic scan of extracted files. No Fold7 device was tested.

The first game launch completed the cinematic and reached character selection,
where logcat repeatedly recorded `DH2FileGuard recovered repeated root` and
successful prince model opens. That confirms the narrow Test 9 path change
passes the precise Test 8 failure. With the wrapper's default `fit16by9=false`,
the 2424x1080 emulator display cropped the game menu vertically and put
`Start Game` above the visible area. Returning to the setup screen, enabling
**Fit game to 16:9**, and launching again produced a 1920x1080 game surface and
the full main menu. This setting is needed for the current emulator layout;
it remains an experimental user option in the tested APK.

From that full menu, `Start Game` and `Single Player` opened the owner's `wolf`
save, loaded the swamp level to 100%, and displayed live 3D gameplay and HUD.
A joystick drag moved the prince several tiles and shifted the camera. Pause
opened a working menu; confirming return to the main menu updated the visible
last-save timestamp from `18.04 06:05` to `02.10 00:07`, matching the
`PlayerLastSave` log line. The guest stayed alive after these steps with 34
threads and about 1.37 GiB VmRSS at the sampled moment. Representative local
screenshots are `outputs/DH2-android17-test9-fit-menu2.png`,
`outputs/DH2-android17-test9-gameplay-before-input.png`,
`outputs/DH2-android17-test9-gameplay-after-move.png`, and
`outputs/DH2-android17-test9-save-return.png`. The full local log is
`work/standalone-build/test9-api37-gameplay-logcat.txt`.

After pausing and returning to the main menu, a process force-stop/relaunch
retained the bundled-cache marker and showed the updated `02.10 00:07` save
timestamp. However, this cold restart displayed the character menu cropped
again with `Start Game` offscreen, even though `dh2-options.json` retained
`fit16by9=true` and guest events reported a 1920x1080 surface. The menu layout
is therefore not yet reliable across restarts. The saved data persisted, but
loading that save again after the cold restart was not tested.

As one bounded diagnostic, enabling **Keep graphics context during
cinematics** alongside the 16:9 option and relaunching showed the full menu
again with the retained save timestamp. The guest event log reported
`preserveContext=true` and a 1920x1080 surface; screenshot:
`outputs/DH2-android17-test9-preserve-menu.png`. A single successful relaunch
after changing the option does not establish reliable cold-start layout, since
the prior 16:9-only relaunch had also worked once before cropping recurred.

Attack-button taps were sent, but still images did not establish a hit or
damage. Some `.tga` texture opens were logged as missing even while the level
rendered; their visual effect and longer play stability remain unassessed.
The 16 KiB page-size guard was not changed for this build. This result covers
one saved level and a short interaction on the 4096-byte-page Android 17
emulator, not every level or ARM64 phone behavior.
