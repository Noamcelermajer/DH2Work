# Test 11 saved-language preference

The Test 10 setup screen checked **Prefer English**, but the tested game menu
still rendered Russian. Its Java `Get_PhoneLanguage()` hook returned English
(`0`), while the original native `SavegameManager::getLanguage()` preferred
the `Language` value in `dh2_settings.savegame`. The owner-provided cache has
`Language=2`. Its `data/text/menu.english` contains English strings, and its
`data/text/menu.german` contains Russian strings. These are facts about this
owner archive; the private cache and original game APK are not committed.

Test 11 applies the checkbox to the installed settings copy immediately before
launching the guest. With the checkbox off it reads or changes nothing. With
it on, it accepts only the observed 16-key settings layout, a `Language` int32
in the expected position, and the 14 trailing tutorial bytes. If the current
value is nonzero, it writes the exact original file once to
`dh2_settings.savegame.before-prefer-english.bak`, verifies the backup, changes
only that four-byte value to zero, and atomically replaces the installed
settings file. The owner ZIP and character/level saves are untouched. An
unknown format or storage failure leaves the settings as they were and does
not prevent game launch; the diagnostic session records the outcome.

The native behavior is visible in
`recovered/native/decompiled/libDungeonHunter2.so/functions-006.pseudo.c`:
`__loadOptions`, `getLanguage`, and `loadSettings` use the saved option, while
`__saveOptions` and `__saveTutorials` write the option records followed by 14
bytes. The earlier phone-language hook is in `GameTrace.java`. The specific
owner settings fixture is 294 bytes, SHA-256
`3cb97b47cc9853d65dc596ccc0a8b715b82fe4794812435673a921b215f07c4a`;
only the four-byte field at offsets 207–210 is eligible to change. For this
fixture, `02 00 00 00` becomes `00 00 00 00`, so only byte 207 differs.
The focused host test checks that exact fixture when the
owner ZIP is locally available, plus disabled, absent, malformed, backup,
idempotency, and byte-preservation cases:

```text
python compatibility/work/fold7-build/tests/test_language_preference.py
```

The complete host Java source set (28 files) compiled cleanly with pinned
JDK 17 against Android 17/API 37.0 `android.jar` and
`hiddenapibypass.jar`, producing 42 classes. D8 converted that host JAR plus
the hidden-API dependency to one `classes.dex` with minimum API 29. This is
source/build validation; emulator results for the signed APK are below.

## Build identity

Test 11 uses the same verified unsigned Test 10 guest APK and owner cache ZIP
as Test 10. Their SHA-256 values are respectively
`57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e`
and `3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`.
In the restored private compatibility build tree, provide these as
`DH2_TEST10_GUEST_APK` and `DH2_CACHE_ZIP`, along with the JDK 17 and Android
SDK inputs described in `STANDALONE-TEST7.md`; run the reviewed `build_apk.py`.
This revision builds version code **14**, version name
`1.0-test11-language`, and defaults to the private output filename
`Dungeon-Hunter-2-Android17-test11-language.apk`. Set `DH2_OUTPUT_APK` for a
different private destination.

## Android 17 emulator results

The signed 4 KiB Test 11 package is 448,474,954 bytes, SHA-256
`5eab6ee211bf711b2041b1da1392cb7b94a727ed1f3faa8226f044d02fb7919c`.
It was installed as a version 14 upgrade over the saved Test 10 app on the
API 37.0 emulator. The settings backup exactly matched the original 294-byte
file, Language changed 2→0 with only byte 207 differing, and the launch
diagnostic reported `UPDATED`. The saved-character menu, level-loading tips,
HUD labels and pause menus displayed English. Single Player resumed the saved
**The Boglands – Ancient Prison** level; joystick movement changed the player
and camera, and two attack-button taps removed a nearby Bog Moth and its
health bar. Pause → Main Menu → Yes changed the visible save time from 02:03
to 04:03. After force-stop/cold relaunch of the same APK, the wrapper reported
`ALREADY_ENGLISH`, the English menu retained 04:03, and Single Player loaded
the saved level and accepted joystick movement again. The guest process
remained alive. Screenshots and the signed APK are local output artifacts;
these observations were not taken on a physical phone.

A separate strict 16 KiB Test 11 package was signed and alignment-checked for
the API 37.2 emulator. It is 448,474,954 bytes, SHA-256
`d688b2a9f4da0c387ea1ddb0448d9de95dd3e00cdac224fca1b1a6e01fa2898e`.
Its first cold run recognized the same settings layout, made a byte-exact
backup, changed only byte 207, and displayed an English saved-character menu.
Start Game → Single Player loaded the saved **The Boglands – Ancient Prison**
level, and a joystick swipe visibly moved the player/camera while the guest
process remained alive. Pause → Main Menu → Yes returned to an English menu and
updated the visible last-save time from 02:11 to 04:21. After force-stop/cold
relaunch of this same installed APK, the wrapper reported `ALREADY_ENGLISH`,
the English saved-character menu appeared again with `10/02 04:21`, and the
guest's own `PlayerLastSave` log agreed. Start Game → Single Player reentered
the saved 3D level, the HUD appeared, and a joystick swipe again visibly moved
the player/camera while the guest process remained alive. The captured guest
log had no Scudo abort, fatal exception, signal 6 or strict origin-refusal
marker. The 16 KiB startup and level load each took several minutes. Full save
semantics and physical-device operation remain unverified.

The already published signed Test 10 APK has SHA-256
`02ba96298aa2639e1bd3c3a34f0b54447756d85aed9e8ed85b3d53bf726964aa`
and version code 13. That hash identifies the prior source revision. Its
source is pinned at Git commit `9723cf0` and its private-input build recipe is
`STANDALONE-TEST10-VIEWPORT.md`; check out that commit to rebuild Test 10.
The local development signing key is private, so byte-identical signed output
also depends on retaining that key and the same packaging inputs. The Test 11
source change does not retroactively change the published Test 10 artifact.

This is a compatibility wrapper around the original ARM32 engine, not a
complete source-built game. The ownership and license limits in `RIGHTS.md`
still apply. No Fold7 or other physical device was used for this finding.
