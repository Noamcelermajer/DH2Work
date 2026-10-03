# DH2Work — Dungeon Hunter 2 HD v1.0.2 on a 64-bit-only ARM Android phone

DH2Work is the **app + compatibility layer** that runs the original 32-bit ARM
(`armeabi-v7a`) Dungeon Hunter 2 HD v1.0.2 on an arm64-only Android device — in
particular a Galaxy Z Fold7 (`SM-F966B`), whose Android 16 system ships no
32-bit userspace at all.

The original ARM32 shared libraries are **not recompiled and not rewritten**.
They are loaded and executed in-process by the ZettaBridge host with the
Dynarmic ARM32→ARM64 (A32) JIT. DH2Work contains the wrapper app, the Java
guest-side compatibility helpers, the hash-pinned byte patches applied to the
original engine/Storm libraries, the launcher patches applied to the
ZettaBridge fork, the packaging/build scripts, the tests, and the device and
emulator evidence with its documentation.

This repository deliberately does **not** contain the separate full
source-reconstruction track (no `port/`, no `recovered/` decompiler/assembly
dumps, no `.pseudo.c` pseudocode, no from-scratch C++ engine sources).

---

## Read this first: honest status

**Playable gameplay has been demonstrated on emulators, not on a physical
phone.** The last physical-device evidence in this repository stops at Test 4
(SIGSEGV while reopening the prince model). Test 5 fixed that cause in code and
passed host/translator tests, but was **never confirmed on the Fold7**. Tests 6
through 11 and the 16 KiB work are emulator-only.

| Environment | Highest verified result | Evidence |
| --- | --- | --- |
| Galaxy Z Fold7 `SM-F966B`, Android 16, 4 KiB pages | Tests 1–4 only: native init, JNI registration, opening cinematic, then abort (Test 3 `SIGABRT`, Test 4 `SIGSEGV`). Test 5 never retested on the phone. | [evidence/phone-test3/](evidence/phone-test3/), [evidence/phone-test4/](evidence/phone-test4/) |
| Android 9 x86 AVD, original ARM32 guest via the OS native bridge | Animated menu, save selection, saved 3D level, movement/combat; save-job thread exhaustion removed by a one-instruction engine patch | [patches/thread-exhaustion/README.md](patches/thread-exhaustion/README.md) |
| Android 17 / API 37.0 x86_64 AVD, 4 KiB pages, arm64 wrapper + ZettaBridge | Full main menu, `Start Game` → saved level at 100 %, live 3D gameplay and HUD, joystick movement, working pause/return, updated save timestamp, cold-relaunch reload; combat damage (enemy + health bar removed) and English UI in Test 11 | [STANDALONE-TEST9-PATH.md](docs/design/STANDALONE-TEST9-PATH.md), [STANDALONE-TEST10-VIEWPORT.md](docs/design/STANDALONE-TEST10-VIEWPORT.md), [STANDALONE-TEST11-LANGUAGE.md](docs/design/STANDALONE-TEST11-LANGUAGE.md) |
| Android 17 / API 37.2 x86_64 AVD, **16 KiB** pages, experimental bridge | Strict experimental build reached title, saved-character menu, saved level, HUD and movement — but the 16 KiB mapper has six documented code-level blockers and the shipped launcher still refuses 16 KiB hosts | [patches/16k-port/README.md](patches/16k-port/README.md) |

Nothing here establishes: audio output, sustained/thermal performance, folded
vs. unfolded input alignment on real hardware, save semantics beyond the
observed checkpoint, multiplayer, or online service behaviour.

## Known defects and open issues

Reproduced and fixed (with hash-pinned patches and host tests):

* MediaStore query with a `*` projection crashed startup → `MediaQueries` requests explicit columns.
* Storm read private, obsolete bionic `soinfo` fields through a `dlopen` handle → symbol/PLT-based replacements.
* The original `CFile` aborted on a path with no basename (trailing separator) → `fopen` import guard.
* The original engine treated Android absolute paths as relative when `WorkingDirectory` was non-empty, duplicating the cache root → 20-byte ARM patch at engine VA `0x56dd9c`.
* A lowercased repeated cache root on deferred model reopen → `dh2_repeated_cache_root` retry in the Storm guard.
* Flat `qata/3d/textures/*.tga` requests that live under `data/3d/textures/` → texture-path fallback.
* Per-frame `pthread_create` for save jobs exhausting the guest address space → optional one-instruction engine patch to the existing synchronous path (diagnostic; not the shipped default).
* Saved-language preference: the native `getLanguage()` used `dh2_settings.savegame`, not the phone locale → `LanguagePreference` rewrites the four-byte `Language` field, with a byte-verified backup.

Still open / unverified:

* **16 KiB host pages.** The default runtime maps 4 KiB guest pages directly and `Dh2Activity` refuses any host page size other than 4096. `patches/16k-port/` is experimental and lists the remaining blockers (file-mapping materialisation, `madvise`/`msync`/`mremap`, fastmem disabled, bridge pointer audits, staging bounds, full 4 KiB/16 KiB suites).
* **Display geometry.** The `Fit game to 16:9` option is experimental; Test 9 saw a cropped menu reappear after a cold restart, and Test 10 added the fitted `nativeSetPhone` size plus a viewport restore that held across two cold relaunches. Fold/unfold and touch alignment on hardware are untested.
* **GL error.** A `GL_INVALID_ENUM` (0x0500) was observed during Test 5; its effect is unassessed. Some `.tga` opens are logged missing even while the level renders.
* **Android 17 x86_64 native bridge.** Test 7 aborted at ~272 s in `libndk_translation.so` (`berberis` `mmap`) and Test 8 in the engine at `libDungeonHunter2.so+0x60b2b8`; both are emulator address-space/translation-layer findings.
* **Toolchain transcript.** `javac` on the private Windows host prints an `AccessDeniedException` while closing Android 37 `android.jar` even though it exits 0 and emits the expected classes. Treat the class count/D8/APK checks as the real signal.
* **Legacy licensing, billing, Bluetooth and network endpoints** are untouched by design and untested.
* **Signing.** The documented PKCS#12 key and password are a deliberately non-production development fixture.

## Architecture in one diagram

```text
:main process                                :guest process
+------------------------------+             +------------------------------------------------------------+
| Dh2Activity (setup UI)       |  Plugin-    | GuestLaunchActivity / Stubs$*                              |
|   CacheArchive cache import  |  Switch     |   = the original game's activities (DungeonHunter2,         |
|   4 KiB host page-size guard |--Activity-->|     MyVideoView, GLiveMain, IGPActivity, ...)                |
|   dh2-options.json options   |             | PluginClassLoader                                          |
|   Dh2Diagnostics report/ZIP  |             |   = guest DEX from the bundled game.apk + helper DEX       |
|   LanguagePreference         |             |        |                                                   |
+------------------------------+             |        v                                                   |
                                             | libzbridge.so  (ARM64 host; ZettaBridge + DH2 patch)        |
                                             |   +-- guest ELF loader / guest linker emulation             |
                                             |   +-- Dynarmic A32 JIT  ==> real ARM32 bionic               |
                                             |   |     libDungeonHunter2.so, libStormGLOFT.so,             |
                                             |   |     libnativeinterface.so, libc.so, libdl.so, ...       |
                                             |   +-- guest syscalls    ==> host kernel + 64-bit bionic     |
                                             |   +-- guest JNI_OnLoad / RegisterNatives ==> ART (same proc) |
                                             |   +-- path aliases      ==> app-private data/lib/cache dirs |
                                             |   +-- GLES/EGL traps    ==> phone GL driver                 |
                                             |         passed through and marshalled, NOT translated       |
                                             +------------------------------------------------------------+
```

Key consequences of that boundary:

* Only the **guest CPU instructions** are translated. Filesystem syscalls, JNI,
  the Android lifecycle and GLES/EGL are handled by the host through explicit
  bridge paths, so a defect at those boundaries is not an instruction-translation
  defect.
* The guest runs in a **separate `:guest` process** so a translated-guest abort
  does not take down the setup UI that produces the diagnostic report.
* The original game keeps package `com.gameloft.android.GAND.GloftD2SS` as a
  *plugin*, so it installs alongside the real game without replacing it.

## Layout

| Path | Contents |
| --- | --- |
| `app/launcher-java/` | Host launcher Java: `Dh2Activity`, `Dh2Diagnostics`, `CacheArchive`, `LanguagePreference` |
| `app/guest-java/` | Guest-side Java helpers injected into the game DEX: `GamePaths`, `GameTrace`, `MediaQueries` (plus the Test 10 `GameTrace` variant) |
| `app/manifests/` | Host and guest `AndroidManifest.xml` templates |
| `patches/engine/` | `patch_engine.py`, `engine_path_fix.S`/`.ld`, patch report |
| `patches/storm/` | `patch_storm.py`, `storm_import_fix.c`, `storm_bias_fix.S`/`.ld`, `storm_path_repair.h`, patch report |
| `patches/game/` | `patch_game.py` (Java/smali + manifest + storage patching) and its report |
| `patches/zettabridge/` | `zettabridge-dh2.patch` — the cumulative host patch for the pinned upstream commit |
| `patches/16k-port/` | Experimental 16 KiB host-page mapper patches + strict rebuild recipe |
| `patches/thread-exhaustion/` | Optional one-instruction save-job engine patch and its packager |
| `upstream-modified/` | Readable copies of every upstream file the DH2 patch touches, plus `LICENSE` and `third_party/` notices |
| `build/` | Packaging/runtime/download/verification scripts |
| `tests/` | Host Java, C and Python tests (`tests/fold7-build`, `tests/16k-port`, `tests/thread-exhaustion`) |
| `docs/` | `LAYOUT.md`, `ARCHITECTURE.md`, `STATUS.md`, and the imported design/status documents under `docs/design/` |
| `evidence/` | Phone reports (`phone-test3`, `phone-test4`), emulator evidence, validation logs and machine-readable reports |
| `notices/` | Upstream licence text and the notice index |

`docs/LAYOUT.md` is the complete old-path → new-path mapping (every imported
file, including the ones moved between directories here): see
[docs/LAYOUT.md](docs/LAYOUT.md).

## How to build and run

DH2Work is source-only. It intentionally ships **no** game APK, **no** game
cache and **no** prebuilt runtime bundle. To produce an installable package you
must supply the private inputs and the toolchains described in the imported
build documents:

* [docs/design/BUILDING.md](docs/design/BUILDING.md) and [docs/design/FOLD7-BUILD-README.txt](docs/design/FOLD7-BUILD-README.txt) — the original build recipe, tool versions and pinned hashes.
* [docs/design/STANDALONE-TEST7.md](docs/design/STANDALONE-TEST7.md) — the current standalone (ARM64 wrapper + bundled guest + bundled cache) recipe and its environment variables.
* [docs/design/STANDALONE-TEST10-VIEWPORT.md](docs/design/STANDALONE-TEST10-VIEWPORT.md), [docs/design/STANDALONE-TEST11-LANGUAGE.md](docs/design/STANDALONE-TEST11-LANGUAGE.md) — the viewport and language revisions, with their pinned input hashes.
* [patches/16k-port/TEST11-STRICT-REBUILD.md](patches/16k-port/TEST11-STRICT-REBUILD.md) — the experimental 16 KiB rebuild.
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layer boundaries and what DH2Work owns.
* [docs/STATUS.md](docs/STATUS.md) — per-test progression and verified/unverified items.

Minimum inputs: the owner's original DH2 HD v1.0.2 APK, the complete game cache
ZIP, Android SDK build-tools 35.0.0 + platform `android.jar`, JDK 17,
HiddenApiBypass 6.1, NDK r29 for the byte patches, and a ZettaBridge checkout of
`7c647a4f1ea150eab7978ab0da28fdf49f3a79de` with the pinned Dynarmic submodule.
See [RIGHTS.md](RIGHTS.md) before publishing anything built from them.

Short form of the Test 7+ recipe (details and hashes in the linked docs):

```sh
git clone https://github.com/ZailoxTT/ZettaBridge.git research/ZettaBridge
git -C research/ZettaBridge checkout 7c647a4f1ea150eab7978ab0da28fdf49f3a79de
git -C research/ZettaBridge apply /path/to/DH2Work/patches/zettabridge/zettabridge-dh2.patch
# initialize the Dynarmic submodule and apply both pinned Dynarmic patches
# extract the prebuilt runtime bundle into research/ZettaBridge/build/launcher
export DH2_TEST10_GUEST_APK=...   # or DH2_TEST7/8/9_GUEST_APK
export DH2_CACHE_ZIP=...
export DH2_ANDROID_SDK_ROOT=... DH2_ANDROID_JAR=... DH2_JDK_ROOT=...
python folder-containing/build_apk.py
python folder-containing/verify_package.py
```

> **Path caveat.** Every build script derives its input root from its own
> location (`ROOT = Path(__file__).parent; WORK = ROOT.parent`) and therefore
> assumes the original `compatibility/work/fold7-build/` layout. The copies in
> `build/` are the reviewed sources; to *run* them, restore a private
> `compatibility/work` tree as the build docs describe rather than executing
> them from this repository's reorganised tree. `docs/LAYOUT.md` records the
> translation.

On the device: install the wrapper (package `local.dh2.fold7`), let it prepare
the bundled or imported cache, then **Launch game**. The cache destination on
the primary user is

```text
/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/
```

Keep the original game installed; the wrapper does not replace it.
[docs/design/DEVICE-TESTING.md](docs/design/DEVICE-TESTING.md) holds the device
acceptance checklist.

## Rights, licences and provenance

This repository grants **no** licence to the original game or its assets, and
it is not an official release. ZettaBridge is source-available under
PolyForm Noncommercial 1.0.0 and PolyForm Perimeter 1.0.1; Dynarmic is 0BSD;
the AOSP ARM32 bionic sysroot and the Khronos registry-derived stubs keep their
own terms. See [RIGHTS.md](RIGHTS.md) and
[notices/NOTICES.md](notices/NOTICES.md) (with
[notices/UPSTREAM-LICENSE.txt](notices/UPSTREAM-LICENSE.txt) and
[upstream-modified/LICENSE](upstream-modified/LICENSE)).
