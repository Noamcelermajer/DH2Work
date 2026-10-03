# Private Test 10 viewport correction on Android 17

Test 10 builds on the pinned Test 9 ARM32 guest and ARM64 ZettaBridge wrapper.
The original game APK and complete cache ZIP are owner-supplied private inputs;
neither their bytes nor the signed output belong in the public source tree.
See `STANDALONE-TEST7.md` and [`RIGHTS.md`](../../../RIGHTS.md) for the base runtime recipe and
provenance limits. All tests below used emulators, not a Fold7 or other phone.

## Cause and source change

With the existing **Fit game to 16:9** setting enabled, the Android 17/API 37.0
4 KiB emulator gave the game a 1920×1080 `GLSurfaceView`. The original guest
called `nativeSetPhone(1080,2009)` from portrait `DisplayMetrics` before it
created that view. The engine then initialized a 1080×2009 GL viewport. Its
later `onSurfaceChanged(1920,1080)` left the GL viewport at 1080×2009. A
standalone experiment that corrected only the GL viewport still rendered a
cropped menu, showing that the first phone-size call also determined cached
engine projection/layout dimensions.

`repack_test10_guest.py` makes one narrow change to the original guest
`DungeonHunter2.onCreate`: it passes the fitted 1920×1080 dimensions to
`nativeSetPhone` when fit is enabled. The helper computes the largest 16:9
rectangle inside the actual display metrics and logs the original/fitted
dimensions. On each surface resize, the helper restores the GL viewport to
that fitted surface after the original renderer callback. When fit is off, the
helper passes the original phone dimensions and leaves the original viewport
behavior alone. The wrapper keeps a user checkbox for disabling fit; Test 10
sets it on by default for a clean install and writes the choice before guest
launch. The graphics-context-preservation setting remains off by default.

## Rebuild from pinned inputs

The exact unsigned Test 9 input has SHA-256
`310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1`.
Invoke `repack_test10_guest.py` with `--test9-guest`, `--apktool-jar`,
`--android-jar`, `--java`, `--javac`, `--d8`, an **empty** `--work-dir`, and
`--output`. It decodes without resources, replaces only the reviewed
`nativeSetPhone` call in smali, compiles the Test 10 Java helper, and copies
only `classes.dex` and `classes2.dex` into the pinned guest ZIP. It compares
every other entry with Test 9 and requires these hashes:

| Artifact | SHA-256 |
| --- | --- |
| Test 10 unsigned guest APK | `57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e` |
| Test 10 primary `classes.dex` | `03c71b7a981b15ac8d28129d9a0abe38d9356d8890c0d4f1374cbac654b9df72` |
| Test 10 helper `classes2.dex` | `bba5f019caf2a0cc0c6f6c8a8f673dc69ec792355af272b7f5020693820d3efe` |
| Owner cache ZIP | `3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679` |

To build the ARM64 wrapper, set `DH2_TEST10_GUEST_APK` to that unsigned Test 10
guest, `DH2_CACHE_ZIP` to the owner ZIP, and the normal `DH2_ANDROID_SDK_ROOT`,
`DH2_ANDROID_JAR`, and `DH2_JDK_ROOT` inputs described in
`STANDALONE-TEST7.md`. Set `DH2_OUTPUT_APK` to a private output path and run
`build_apk.py` in the restored compatibility tree. The builder verifies the
pinned nested guest and cache hashes, compiles from a clean classes/DEX
directory, aligns, signs with its local development key, and checks the
signature and alignment. The local Windows JDK 17 compiler may emit an
`AccessDeniedException` while closing `android.jar` despite exit code zero;
check class count, D8 output, build exit code, and the APK validations rather
than treating that compiler transcript alone as success.

## Emulator evidence, 2026-10-02

Screenshot filenames below identify local task outputs supplied with the
handoff; the images are not committed to this source repository.

An initial signed Test 10 candidate with the fit checkbox enabled manually
had SHA-256
`19f08bad0c03177ed9ee790b00a93caf5084a75be1991b9b6031d576196171c7`.
On Android 17/API 37.0 x86_64 with 4096-byte pages, the guest logged
`initial phone=1080x2009 fitted=1920x1080` and a 1920×1080 view, surface,
and corrected GL viewport. It rendered a full-width title and full saved
character menu. With **Keep graphics context during cinematics** off, two
force-stop/cold-relaunch cycles again rendered the full character menu with
**Start Game** visible. The second menu screenshot is
`outputs/DH2-android17-test10-cold2-full-menu.png`, SHA-256
`f13f740b2d8b05679c56864bb62d166ac343c70b5dbb7417fa70e9e833ee1341`.
The first cold relaunch entered the saved Swamps level and responded to a
joystick swipe and action tap. Before-input and after-input screenshots are
`outputs/DH2-android17-test10-cold1-gameplay-before-input.png` (SHA-256
`5425257b983f219e1012da5c004a4cd6c86e3a376cd964a9ac8a57f8f1c87cfd`)
and `outputs/DH2-android17-test10-cold1-gameplay-after-input.png` (SHA-256
`e79e2cb1515ad75d4db9698807c0a4935e3540a0c2cf810e4c137edcbf827588`).
The player moved from near the moth at screen center to the right-hand edge,
with a corresponding camera shift.

The final default-fit signed APK was built separately as version code 13:

| Artifact | SHA-256 | Bytes |
| --- | --- | ---: |
| `Dungeon-Hunter-2-Android17-test10-default-fit.apk` | `02ba96298aa2639e1bd3c3a34f0b54447756d85aed9e8ed85b3d53bf726964aa` | 448,470,858 |

A clean uninstall/install with `--abi arm64-v8a` completed on the same 4 KiB
emulator. First launch imported 6,835 cache files (including generated
options) occupying about 663 MiB; `.dh2-bundled-cache.sha256` matched the
pinned owner ZIP hash. The fresh wrapper showed fit checked and context
preservation unchecked. Before launching the guest, `dh2-options.json` held
`{"preferEnglish":true,"fit16by9":true,"preserveContext":false}`; the guest
logged `start ... fit16by9=true preserveContext=false` and the 1080×2009 to
1920×1080 initial fit. The installed package reported version code 13,
`primaryCpuAbi=arm64-v8a`, and remained alive during the run.

This exact final APK displayed a full character menu with **Start Game**
visible. The fresh-install menu screenshot is
`outputs/DH2-android17-test10-final-fresh-full-menu.png`, SHA-256
`2d1a2684bebf8b8031adaacff6fa560d3cd7220a1346cfa16c9b1670276311bc`.
Start Game → Single Player loaded the saved Swamps level. Before input, a moth
and its full health bar were visible beside the player in
`outputs/DH2-android17-test10-final-gameplay-before-input.png`, SHA-256
`03cd2595820e562af7d6b6cd1a9ccabf3cda269a2eeb493db1283f23732b03d9`.
After three action-button taps, that moth and health bar disappeared while
the process stayed alive; screenshot
`outputs/DH2-android17-test10-final-combat-after-taps.png`, SHA-256
`bae3bac62a933ab4b0d84e257d58b9f72b10fc9f0a9200811f73729ec5e7a8f1`.
A joystick swipe then visibly moved the player right and shifted the camera;
`outputs/DH2-android17-test10-final-gameplay-after-movement.png`, SHA-256
`7853cd9e1aa79f08f2b25f643dbedf5f616aee13eb3c709560f0a9ae78f0d3ed`.

The builder finished with exit code zero. The final APK passed v3
`apksigner verify`, `zipalign -c -P 16 4`, and CRC checks for every outer and
nested guest ZIP entry. The outer APK had 49 unique entries; the nested guest
had 220 unique entries. The signer is the local development key, certificate
SHA-256 `daa24cd98557703003fb4518d1ed8504dee071f9620596592819176677772081`.

The exact final APK also passed one save-return and cold-relaunch sequence on
the API 37.0 4 KiB emulator. Pause → Main Menu → confirmation returned to the
full character menu with last save `02.10. 02:03`. After a force-stop and cold
relaunch, the same saved WOLF/Swamps character and timestamp remained visible;
Start Game → Single Player loaded the saved Swamps 3D level again. A joystick
swipe moved the player and camera while the guest PID remained alive. The
guest logged `fit16by9=true preserveContext=false`, initial phone dimensions
1080×2009 fitted to 1920×1080, a recreated surface and 1920×1080 viewport.
The nearby enemy respawned on reentry, so this is checkpoint persistence, not
exact combat-state restoration. Screenshots in task outputs:

| Screenshot | SHA-256 |
| --- | --- |
| `DH2-android17-test10-pause-menu.png` | `2d85f92a26b0d9d43aaad23184ff1c03cffe6ff572e6fb368683551dd22d60ec` |
| `DH2-android17-test10-returned-saved-menu.png` | `91d087c7651280d01e9caa8dcdf24839ed3bcf017ebee6d6725746b826192736` |
| `DH2-android17-test10-saved-level-after-cold-relaunch.png` | `80ea82c6c06c57d815517da03bb64fbae95fcc80fb62a22062635845b3e121d8` |
| `DH2-android17-test10-saved-level-after-cold-movement.png` | `e6c5805697ec2fde6fd1055d7fee38ed52a9ee7dea148c106838700a5541712e` |

This test establishes a working emulator path for the tested cache/save and
short level interaction. It does not establish compatibility across every
level, device, or 16 KiB page configuration. The separate 16 KiB translator
investigation is documented under `compatibility/16k-port`.

The signed Test 10 hash above belongs to source commit `9723cf0`. The later
saved-language fix has distinct version code 14 and is documented in
`STANDALONE-TEST11-LANGUAGE.md`; it does not change that Test 10 artifact.
