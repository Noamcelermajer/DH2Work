DUNGEON HUNTER 2 - GALAXY Z FOLD7 COMPATIBILITY TEST BUILD
Target requested: Galaxy Z Fold7, One UI 8.5. Date: 2026-10-01.

WHAT YOU CAN INSTALL

Dungeon-Hunter-2-Fold7-test.apk is a signed ARM64 Android app containing the
user-supplied game and an ARM32-to-ARM64 translation runtime. It uses package
local.dh2.fold7, so it installs separately from the original game.

This is a complete test package, not a proven playable port. Android ART,
graphics, sound, input, saved games, multiplayer, online services and fold/unfold
behavior have NOT been tested on a device. It is not a full handwritten ARM64
rewrite. ZettaBridge/Dynarmic translate the native engine in process. Independent
game-specific native fixes and Java/storage changes are included.

INSTALL AND CACHE

1. Install the APK. Keep your original installation and saved games.
2. Keep the complete original cache ZIP in Downloads or another location the
   Android document picker can access. Do not split it.
3. Open Dungeon Hunter 2 - Fold7 Test and wait for preparation to finish.
4. Tap Import cache ZIP and select the archive. Keep this screen open until done.
5. Tap Launch game.

The app extracts the cache to its own storage automatically. On the primary user:

  /storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/

The actual destination is displayed in the app. Manual copying into Android/data
is not required. This differs from the original APK's directory because the
game runs inside the ARM64 wrapper. The path resolver and native aliases route
old accesses to this directory.

The importer accepts normal ZIP archives containing extracted game files. It
recognizes raw roots and common Android/data/<package>/files wrappers, removes
redundant single wrapper directories, and preserves internal file structure.
It rejects unsafe paths, empty/invalid archives and archives of OBB files.
Your actual cache archive has not been inspected, so its texture variant,
completeness and exact structure remain unverified. Import success establishes
that files were extracted, not that they are the correct game assets.

Reimport replaces this test build's external cache directory. Keep the original
archive. The importer preserves the previous directory if extraction fails.

IF THE GAME FAILS

Reopen the setup app and use View / share diagnostic report. It includes device
model, Android version, CPU ABIs, page size, Java errors and the native runtime
report. Send the report back with the visible symptom or a screenshot. Report
sharing happens only when you tap Share and choose a destination.

There is no connected phone or real Android app environment in this workspace.
The remaining acceptance gate is installation, launch and gameplay on the Fold7.

NATIVE BUGS FOUND AND FIXED

The supplied libStormGLOFT.so accessed Android's old private soinfo structure
through a dlopen handle. Modern bionic uses opaque handles. Two paths crashed:

* hook_inline_function read load_bias at handle + 0x8c.
* hook_import_function read private symbol/hash/relocation fields at offsets
  0xac, 0xb0, 0xb4, 0xbc, 0xc0, 0xc8 and 0xcc.

The first was replaced with a small ARM32 assembly sequence that resolves the
engine's exported JNI_OnLoad and subtracts its verified symbol address. The
second was reconstructed in freestanding C to walk the loaded ELF's public
program headers, dynamic symbol/string tables and PLT relocations.

Both replacements occupy verified unused space in Storm's executable segment.
Two branch instructions redirect the affected paths, and the segment extent is
updated. Existing graphics hooks remain active. This patch is hash-pinned to
the exact supplied Storm/game pair. It must not be reused on another version.
The main libDungeonHunter2.so remains byte-identical to the supplied engine.

JAVA AND RUNTIME CHANGES

* 31 legacy absolute-path lookups now resolve through the actual game Context.
* App initialization sets the SDFolder preference to the accessible data root.
* getSDFolder now compares empty-string contents, not Java reference identity.
* Native path aliases handle the original /data/data/<package>/lib and cache
  paths inside the translator, including Storm's own library loading.
* The prior denied-telephony-access guards and directory initialization remain.
* The ARM64 host declares the optional legacy Apache HTTP library and preserves
  the game's cleartext networking behavior. No network service was tested.
* The game's existing licensing/billing decisions were not changed.

VALIDATION ACTUALLY PERFORMED

* Built the complete ARM64 runtime with NDK r29 and the required ARM32 support
  libraries. All imported native symbols are accounted for in those libraries.
* Built, aligned and signed the APK; the Android SDK verifier accepts its v3
  signature. Host libraries are ARM64 and have 16 KiB ELF load alignment.
* Executed the same ARM64 translator using QEMU as a developer reference host,
  against the real ARM32 Android runtime and all three supplied game libraries.
* Ran 4,096 original-engine integer inputs, covering log2 and Lua encoding.
* Called the game and Storm JNI_OnLoad entry points with an inert JavaVM value.
  These entry points retain the VM / initialize native state without using its
  methods. This does not validate the real ART JNI bridge.
* Confirmed the shader and GL-string GOT hooks and original inline hook were
  actually installed after the fixes. Initialization previously crashed.
* Passed 13 cache tests covering folder layouts, exact bytes, invalid paths,
  invalid ZIPs, OBB refusal, cancellation and replacement/rollback.
* Verified packaged hashes, nested ZIP CRCs, ABI, manifest and helper inclusion.

The runtime currently maps 4 KiB guest pages directly and therefore requires a
4 KiB host page size. The app reports and refuses an unsupported page size before
launch. 16 KiB ELF alignment alone does NOT establish 16 KiB runtime support.
The Fold7's actual page size has not been measured in this session.

REBUILD

Use Linux x86_64, Python 3.12+, Java 17, Android SDK platform 35/build-tools 35.0.0,
and NDK r29. Python dependencies: pyelftools 0.33 and capstone 5.0.9. The core also
uses CMake/Ninja and Boost headers. Paths below are relative to this kit's root.

The kit contains the earlier decoded/patched game sources, exact native inputs,
test signing key, new sources/scripts, the ZettaBridge integration diff, and a
prebuilt runtime bundle. It does not contain the cache, SDK/NDK, or upstream git
checkouts. download-hashes.json pins the downloaded artifacts used here.

1. Clone https://github.com/ZailoxTT/ZettaBridge into research/ZettaBridge and
   check out 7c647a4f1ea150eab7978ab0da28fdf49f3a79de.
2. Apply fold7-build/zettabridge-dh2.patch from that checkout.
3. Extract runtime-bundle.zip into research/ZettaBridge/build/launcher. This
   supplies the exact tested runtime files. Their checksums are recorded inside
   runtime-manifest.json. For a native rebuild, initialize the Dynarmic submodule,
   apply both patches in third_party/patches, and follow the upstream build steps.
   The ARM32 sysroot can be recovered from the supplied runtime bundle.
4. Install tools in android-sdk and toolchains/android-ndk-r29; put Java 17 under
   toolchains/jdk-17*. Get HiddenApiBypass 6.1 from Maven Central into downloads/,
   including the AAR and its extracted classes.jar named hiddenapibypass.jar.
   The earlier download-tools.py fetches the pinned apktool JAR into tools/.
5. Run:

   python fold7-build/patch_game.py
   java -jar tools/apktool.jar b fold7-build/game-tree -o fold7-build/game-unsigned.apk
   python fold7-build/build_apk.py
   python fold7-build/verify_package.py

The APK is written to ../deliverables. Create that directory if needed.
The deliberately local test key/password are unchanged from the previous kit;
they are not Gameloft's signing key and are only for this personal test build.

Full native-load testing additionally requires the ARM64 AOSP bionic reference
runtime and qemu-aarch64-static (developer testing only, not shipped in the APK).
The probe source is tests/dh2_load_probe.c, its observations are in native-load-test.log,
and the previous crash is recorded in native-load-before-import-fix.log.

UPSTREAM / NOTICES

ZettaBridge is source-available under cumulative PolyForm Noncommercial 1.0.0
and PolyForm Perimeter 1.0.1 terms. This is a private game compatibility test,
not a general launcher release or a commercial product. Upstream notices and
third-party information are included. Dynarmic uses 0BSD. Android system files
originate from AOSP; their upstream licenses continue to apply.

References used for Android storage and receiver behavior:
https://developer.android.com/training/data-storage/app-specific
https://developer.android.com/about/versions/14/behavior-changes-14
