DUNGEON HUNTER 2 — EXPERIMENTAL ARM32 COMPATIBILITY BUILD

Status: partial compatibility work, NOT a verified modern-Android port.
Built from the APK supplied in this conversation. No game assets were obtained
from another source. The supplied cache ZIP was unavailable and was not read.

WHAT WAS DONE

* Decoded all resources and Dalvik bytecode with apktool 2.12.1.
* Exported Java representations of 357 classes using Androguard 4.1.4. These
  are reverse-engineered representations, not the original project source and
  not promised to compile as Java. Smali is the rebuild source.
* Audited ELF architectures, shared-library dependencies and imported symbols.
* Set targetSdkVersion to 24, addressing the low-target installation floor
  documented for Android 15. This is NOT a migration to the latest target SDK.
* Set minSdkVersion to 21; this experimental package no longer targets Android 2.x.
* Guarded 11 calls for device ID, subscriber ID and phone number against denied
  access. On denial, the helper returns null, as an unavailable identifier;
  no identifier is fabricated and no Android permission restriction is bypassed.
* Added an Application initializer calling getExternalFilesDir(null) so Android
  creates the app-specific external directory before legacy startup I/O.
* Explicitly declared the legacy Apache HTTP framework library, optional.
* Preserved older UI acceleration behavior and opted out of activity resizing.
* Explicitly declared existing component export behavior and native extraction.
* Removed an incomplete armeabi directory that contained only a DRM helper.
* Rebuilt, aligned and signed with a new local test key. v1/v2/v3 signatures verify.

WHAT WAS NOT DONE

* The native game engine was NOT decompiled to reconstructable C/C++, ported to
  ARM64, or recompiled. All three retained ARM32 libraries are byte-identical
  to those in the input APK. There is no arm64-v8a, x86 or x86_64 engine.
* No dependencies were replaced with arbitrary libraries of matching names.
  System dependencies must be provided by the Android device's compatible ABI.
* No game content or cache was imported, converted or verified.
* No device installation, launch, rendering, sound, input, save, lifecycle,
  Bluetooth multiplayer or online-service test has been performed.
* Existing licensing and billing decisions were not replaced by success stubs.
  Identifier unavailability may still affect old account/licensing flows.
* This was a compatibility audit, not a complete security review of the input.

HARD BLOCKERS AND REMAINING RISKS

1. ARM64-only phones cannot load the retained ARM32 engine directly. A manifest
   change, renamed library folder or newer SDK cannot translate that machine
   code. A genuine port would need suitable native source / ARM64 libraries,
   substantial native-code reconstruction, or a separately validated translation
   environment. None of those has been supplied or implemented here.
2. Native load segments use 4096-byte alignment. No native 16 KB page support
   has been added or claimed. A future ARM64 port must also address page sizes.
3. The cache ZIP is required to inspect asset layout, texture formats and the
   expected game data. The APK's raw game-data paths primarily reference:
   /sdcard/Android/data/com.gameloft.android.GAND.GloftD2SS/files
   The correct content layout inside that directory is not yet verified.
4. The input includes libStormGLOFT.so, an existing native patch library with
   graphics/texture hooks. It was preserved; its runtime behavior is unverified.
5. Some Java and native code still uses hard-coded external paths. Creating
   the standard directory does not prove all save and asset operations work.
6. Old network services, legacy Bluetooth behavior, identifier-dependent SDKs
   and native graphics assumptions may need additional work after log capture.

DEPENDENCIES FOUND

libDungeonHunter2.so (ARM32, 15,938,284 bytes):
  libc.so, libGLESv2.so, libstdc++.so, libm.so, libGLESv1_CM.so, libdl.so, liblog.so
libStormGLOFT.so (ARM32, 907,744 bytes):
  liblog.so, libz.so, libm.so, libdl.so, libEGL.so, libGLESv1_CM.so, libGLESv2.so,
  libandroid.so, libc.so, libstdc++.so
libnativeinterface.so (ARM32, 11,320 bytes):
  libc.so, libstdc++.so, libm.so, libdl.so

Java includes legacy Gameloft glue, Samsung Zirconia/Plasma components,
Apache HTTP usage and pre-existing save restoration code. These are compiled
components, not a Gradle/Maven dependency manifest that can simply be upgraded.
See original-audit.json for the full native dependency and symbol inventory.

VALIDATION

validation.json records 21 successful static checks, including parsed manifest
values, unchanged native libraries and assets, helper call sites in the rebuilt
DEX, ZIP CRCs and signature presence. signature-verification.txt records separate
cryptographic signature and alignment verification. These checks establish
package integrity, NOT game playability.

BEFORE INSTALLING

The package ID is unchanged but the signing key is different. Android will not
install this over a copy signed by someone else. Back up existing saves before
considering removal of the old copy: uninstalling can delete its saves/data.
Do not treat this test APK as a completed or verified upgrade.

For the next step, supply the missing cache ZIP and the exact phone model and
Android version. If ADB is already set up, these read-only commands give useful
compatibility details:

  adb shell getprop ro.product.model
  adb shell getprop ro.build.version.release
  adb shell getprop ro.product.cpu.abilist
  adb shell getprop ro.product.cpu.abilist32
  adb shell getconf PAGE_SIZE

An empty abilist32 means this ARM32 package is not a direct-run candidate on
that Android system. If installation and launch are later tested, use the game
normally and capture the crash buffer with:

  adb logcat -b crash -d

REBUILDING THE INCLUDED PATCHED TREE

Requires Java 17+ and Python 3. The downloaded Java tools are not bundled.
The download-tools.py helper downloads the exact pinned tools from their
upstream release URLs and checks the hashes recorded during this build.

  python download-tools.py
  java -jar tools/apktool.jar b patched -o rebuilt-unsigned.apk
  java -jar tools/uber-apk-signer.jar --apks rebuilt-unsigned.apk --ks dh2-local-test.p12 --ksAlias dh2-local-test --ksPass dh2-local-test-only --ksKeyPass dh2-local-test-only --out rebuilt-signed

The included local TEST keystore/password are intentionally provided so this
user can sign follow-up experimental builds consistently. It is not Gameloft's
key and must not be reused for other applications or production distribution.

To reproduce the patch from the original APK, decode the original to "decoded"
in a fresh directory, copy patch_compat.py there, and run it. It creates
"patched" and a reviewable compatibility.patch. The original APK is supplied
separately by the user and is not included again in this kit.

patch_compat.py, decompile_java.py and verify_build.py also document the steps
used in the working directory. verify_build.py expects the original APK in
../upload/ and the signed build in signed/, as used during this session.

PRIMARY ANDROID REFERENCES

https://developer.android.com/about/versions/15/behavior-changes-all
https://developer.android.com/games/optimize/64-bit
https://developer.android.com/guide/practices/page-sizes
https://developer.android.com/training/data-storage/app-specific
https://developer.android.com/about/versions/10/privacy/changes
https://developer.android.com/about/versions/pie/android-9.0-changes-28
