# Status: test-by-test progression

Everything below is drawn from the imported evidence and documents in this
repository. Where a document and its banner disagree, the newer standalone
document wins and the disagreement is called out in section 12.

**Summary of the single most important fact:** the last physical-device
evidence is Test 4 (a crash). Tests 5 through 11 and the two side investigations
are verified only on emulators or on a host. No recorded run shows Dungeon
Hunter 2 gameplaying on the Fold7.

## 1. Test 1 — first phone run, native startup

| | |
| --- | --- |
| Environment | Galaxy Z Fold7 `SM-F966B`, Android 16, arm64-v8a, 4096-byte pages |
| Verified on device | All three game libraries loaded; both recorded game/Storm JNI initializers succeeded; 36 natives registered; two Samsung licence-check JNI calls ran; no unimplemented host call; no guest exit |
| Failed | `Musicplayer.initMediaList` → `IllegalArgumentException: Invalid column *` (20:49:23 and 20:49:27) |
| Also observed | Zero GL calls and zero EGL swaps; `libdl.so`/`libc.so` failed opens were search-path probes |
| Evidence | `evidence/fold7-build/phone-report-test1-excerpt.txt` |
| Fixed by | Test 2 |

## 2. Test 2 — MediaStore projection repair

Change: `MediaQueries` requests explicit `_id, name` columns instead of a
one-element `*` projection, copies rows into an owned cursor, closes the
provider cursor, and returns an empty non-null cursor on null/denied/schema/db
failure so the legacy constructor can still call `nativeInitplayer`. Host
version `1.0-test2` / versionCode 2, same package and signing certificate.

| Verified | Not verified |
| --- | --- |
| 7 host regression cases (valid rows, explicit projection, null result, permission denial, invalid schema, db failure, cleanup including failure during iteration) | Any Android emulator or device run of the fixed build at the time of writing |
| Packaged DEX re-decoded: `MediaQueries.queryPlaylists` referenced, `*`/`managedQuery` absent from init, native initializer preserved | The real music library of the owner |
| Guest native libraries and host runtime byte-identical to Test 1; signing certificate, versionCode, alignment, package structure | GPU, audio, save/load, input, folding, gameplay |

Outcome on device: the owner's next report confirms the cinematic now completes
and the process then aborts on the fairy loading screen. Evidence:
`evidence/fold7-build/test2-regression-results.json`,
`evidence/fold7-build/media-query-tests.txt`,
`evidence/fold7-build/media-query-packaged-smali.txt`.

## 3. Test 3 — persistent diagnostics and option switches

Change (diagnostic build, versionCode 3, same key):

* abort/nonzero-exit reports now carry ARM32 registers, module-relative PC/LR,
  stack bytes and up to 16 executable-address candidates (explicitly a stack
  scan, **not** an unwound backtrace);
* a bounded 64-message guest-log tail captures guest stdout/stderr and Android
  liblog packets seen on the verified logd socket;
* recent distinct failed asset opens replace the first-four-only list;
* app-UID logcat rotation (three ~1 MiB files) plus Android exit history and
  available native/ANR traces, collected on return to the launcher;
* guest events for language, surface creation/dimensions, init/resize return,
  frame milestones, lifecycle callbacks and small character-data file hashes;
* **Export diagnostic ZIP** through the document picker (no root, no
  `READ_LOGS`);
* optional settings: **Prefer English** (on), **Fit game to 16:9** (off),
  **Keep graphics context during cinematics** (off).

| Verified | Not verified |
| --- | --- |
| 5 real ARM32 probes through the rebuilt ARM64 translator under QEMU: abort, multithreaded abort, multithreaded `exit(42)`, caught `SIGABRT` returning normally, `SIGSEGV` — correct statuses and persisted markers | ZIP export UX, One UI layout, audio continuity, actual presentation, game loading, sustained gameplay |
| Both original libraries still load; both `JNI_OnLoad` entry points pass with an inert VM; 4096 engine helper inputs; installed native hook checks | Real ART JNI bridge and GPU |
| 7 media-query regressions; package CRCs, nested libraries, upgrade version; packaged DEX re-decoded | |
| Packaging validation now rejects an incomplete nested APK before appending helper DEX | |

Device result (owner's report, 2026-10-01 21:22:17–21:23:09 Asia/Jerusalem):
cinematic completed, 735 `nativeRender` calls, 40,389 GL calls, one surface
creation at 2184×1228, guest exited with **status 134 (SIGABRT)**. Stack
candidates mapped to `std::__stl_throw_out_of_range` → `CFile` complete-object
constructor → `CFileSystem::open` → `CReadFile::openFile`.

## 4. Test 4 — file-open guard for trailing-separator paths

Independent reproduction: `CFile` builds the stored pathname, finds the last
slash/backslash, adds one, and checks `basename_position >= string_size`; a path
ending in a separator has no basename, so the no-exceptions STL prints and calls
`abort`. A new probe calls the **original** exported `CFileSystem::open` (not a
substitute) and reproduces exit 134 and the same `0x56ca64`/`0x56dde8`
addresses with a real trailing-slash directory; an ordinary existing file
passes.

Change: the Storm import installer substitutes the **engine's** `fopen` import
with a small C wrapper that rejects a trailing slash/backslash with `null` +
`EISDIR` before the unsafe constructor; all other requests go to Storm's
original bionic `fopen`; `opendir`/`openat` untouched; the engine binary stays
byte-identical. The wrapper logs to liblog **and** unbuffered fd 2, and the
engine's `puts` import becomes a forwarding logger so buffered STL abort reasons
survive. Helpers are position independent; the patcher rejects a generated
GOT/data/BSS or dynamic-relocation section.

| Verified | Not verified |
| --- | --- |
| `tests/fold7-build/run_file_open_tests.py`: original normal read, original directory abort, repaired normal read, repaired directory rejection + diagnostic persistence, missing-file failure | The phone's exact failing pathname (Test 3 logged only selected filename suffixes, not directory paths) |
| `build/run_native_probe.py`: three library loads, both `JNI_OnLoad` with an inert VM, installed shader/string/inline hooks, 4096 engine helper inputs | ART session, Android GPU, the loading screen, later gameplay |
| APK: original engine hash, new Storm hash, runtime bytes, signatures, upgrade version, ZIP integrity | |

Device result (owner's report, 2026-10-01 21:46:15–21:46:49): the guard **did**
run (it rejected the cache root ending in `/`) and the `CFile` abort is absent,
but the run ended with **SIGSEGV / status 139** while reopening
`prince_modular.bdae`, whose name contained the cache root twice. LR mapped to
`glitch::collada::COnDemandReader::read` at `0x60b2e4`. 11,121 GL calls, active
context, an earlier `GL_INVALID_ENUM` (0x0500), settings `fit16by9=false`,
`preserveContext=false`, 2184×1968 surface with context recreation after video.

## 5. Test 5 — absolute-path fix in the engine binary (first engine edit)

Reproduced root cause: `CFileSystem::open` looks for a colon to decide whether to
bypass its nonempty `WorkingDirectory`, so it recognises Windows-style absolute
names but not a POSIX leading `/`. Deferred model loading reopens the stored
absolute name and the routine prefixes the root again.

Change: `patches/engine/patch_engine.py` verifies the complete original engine
SHA-256 and the exact 20 original bytes, then replaces five ARM instructions at
`0x56dd9c`, keeping the colon case and adding a leading-`/` case; relative names
still take the original root-prefix path. No addresses, layouts or dynamic
symbols move; no allocation, no new host callback.
`patches/engine/engine_path_fix.S` and its linker script are the readable source.
The Storm wrapper additionally logs `prince_modular.bdae` opens (with errno) and
Java reports the model's existence, size and first 32 bytes before engine start.

| Verified | Not verified |
| --- | --- |
| 13 rooted-path cases: fixed/original absolute names, the phone-style lowercased path, relative names, roots with and without a trailing slash, empty root, missing inputs, directory rejection, colon-path behaviour | **Any phone run.** "Loading and gameplay on the Fold7 remain unverified" |
| 5 earlier file-open cases (reproduced original directory abort, guarded behaviour, valid bytes) | Russian menu text, aspect ratio, the `GL_INVALID_ENUM`, later gameplay, 16 KB host pages |
| Full library load, both `JNI_OnLoad` with inert VM, existing hooks, 4096 helper inputs | |
| Package CRCs, engine/Storm hashes, bundled runtime equality, version metadata, signing, alignment | |

Host artifact: `evidence/fold7-build/build-result.json` — APK SHA-256
`e6b81ec649e25bb32c6ec7f3f477d5ef1c2a79b7af43b7ca7643518c5f5e1b8d`,
13,769,952 bytes, `device_tested: false`, `gameplay_tested: false`.

## 6. Test 6 — repeated cache root (Android 9 x86 AVD)

Environment: Android 9 x86 Google APIs AVD running the **directly installed
ARM32 guest** through Android's own translation layer — a different stack from
the arm64 ZettaBridge wrapper.

Observed: cinematic, splash, saved slot and menu display; a successful open of
`prince_modular.bdae`, then a failed reopen with a duplicated, lowercased
`/storage/emulated/0/Android/data/<package>/files/` root; the next engine
operation crashes while opening the menu model.

Change: the Storm import guard retries a failed read-only `fopen` only when the
requested path contains two identical `/storage/emulated/0/Android/data/<package>/files/`
roots (ASCII-case-insensitive), removing the second root and keeping the first
root's exact spelling. Root-directory rejection happens first; single-root,
relative, different-package, write-mode and non-matching paths are unchanged.
Success logs `DH2FileGuard recovered repeated root:`.

| Verified | Not verified |
| --- | --- |
| `tests/fold7-build/test_repeated_cache_root.c` under host GCC `-std=c11 -Wall -Wextra -Werror` (exact emulator path, ordinary absolute/relative paths, a directory, different packages, absent child, unrelated double slashes, capacity bounds) | Emulator runtime behaviour after signing/installing |
| NDK r29 compiles the ARM32 guard with `-Wall -Wextra -Werror`; linker proves the code fits the pinned executable gap and has no runtime data relocations | |

Artifacts: unsigned Test 6 guest `85a7ececd975af57f7651dca1d7401af499b77467d814ac632afa91c3300edbc`,
patched Storm `98eecf6d8b13be98a3d615db4584d67f2f59843deb0396516e72fe99601f868a`.

## 7. Test 7 — texture-path fallback (Android 9 x86 AVD)

Observed: Test 6 reached the animated menu and Single Player loading; during
loading step 6, four `.tga` requests failed under `files/qata/3d/textures/`. The
supplied cache has all four exact basenames under `files/data/3d/textures/` and
has no `qata` root. The translator then aborted with
`Memory exhausted: requested 131072 bytes`, preceded by thousands of failed
thread-creation attempts — so the missing textures are **not** established as
the abort's cause.

Change: after a failed `r`/`rb` open, and only for a flat `.tga` under the exact
Android app-cache `3d/textures` directory, `qata` is rewritten to `data` and the
open retried; success logs `DH2FileGuard recovered texture path:`. The Test 6
repeated-root and directory guards remain.

| Verified | Not verified |
| --- | --- |
| Host C11 regression for all four observed paths plus negatives, `-Wall -Wextra -Werror`; NDK r29 ARM32 compile with the same flags; patch script verifies original hashes, patch site, executable capacity and absence of runtime data relocations; unsigned APK ZIP CRC and library-hash verification | Whether the missing textures matter visually; whether the abort was address-space pressure |

Artifacts: unsigned Test 7 guest
`302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd`, Storm
`334c23b854327ea0315f5353ec89d16fc518963840bedb3753248952f02c7845`, engine
`45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4`.

## 8. Test 7 standalone — first Android 17 x86_64 wrapper run

Environment: official Android 17 / API 37.0 x86_64 emulator, 4096-byte pages,
wrapper installed fresh with `--abi arm64-v8a`. Private inputs: Test 7 guest
(`302ae407...`) and the owner cache ZIP
(`3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`).

Reached: `Ready. Cache available`, 6834 files / ~663 MiB imported; after
**Launch Game**, bridged proxy loading, guest JNI runtime startup, **32 engine
natives bound**, Storm loading and engine shader processing; screenshots show the
opening cinematic and then the DH title with a loading spinner.

Did **not** reach: the menu.

Abort at ~272 s process uptime, in the emulator's ARM64→x86_64 translation
layer:

```text
berberis: frameworks/libs/binary_translation/base/mmap_posix.cc:128:
  CHECK failed: 0xffffffffffffffff != 0xffffffffffffffff
libndk_translation.so: berberis::MmapImplOrDie ->
  ExecRegionAnonymousFactory::Create -> CodePool::Add
```

Last sampled status: 34 threads, VmSize 38,726,092 KiB, VmRSS 950,040 KiB; no
`pthread_create failed`, `Not Created` or `Memory exhausted` entry in that run.
On the 16 KiB emulator the launcher's page-size guard blocked launch. No Fold7
device test.

Two build details recorded here: the recipe adds ARM32 sysroot aliases at
`/system/lib/arm` and `/system/lib/arm/bootstrap` (the API 37 emulator's
`ld.config.arm.txt` needs them or guest `zbhost` reports `libdl.so not found`),
and the host patch makes Android's native loader choose an ARM64/bridged
namespace via the host `nativeLibraryDir`.

## 9. Test 8 — synchronous save-job engine instruction (Android 17 x86_64)

One ARM32 engine instruction changed so per-frame save jobs use the existing
synchronous state-17 path. Guest SHA-256 `8168af36...`, engine `ad33304f...`;
Storm and `classes2.dex` byte-identical to Test 7; versionCode 8,
`1.0-test8-sync`.

Result: passed the opening cinematic and title screen and stayed alive more than
six minutes beyond Test 7's Berberis abort; selected the owner's existing `wolf`
save; the first prince model opened; then deferred loading tried a **lowercased,
duplicated** cache root and failed — the engine aborted at
`libDungeonHunter2.so` offset `0x60b2b8`. No Android 17 gameplay was validated in
this run. Signed diagnostic wrapper SHA-256 `5219ac79...`, 448,470,789 bytes;
signature, alignment, unique ZIP entries and CRCs passed.

## 10. Test 9 — repeated-root retry for the wrapper layout (Android 17 x86_64)

Retains Test 8's engine patch and rebuilds only `libStormGLOFT.so` with a revised
`dh2_repeated_cache_root` rule that also accepts the wrapper's exact
`<host>/files/plugins/com.gameloft.android.GAND.GloftD2SS/` suffix, requiring a
case-insensitive root match **and** a child filename after it; unrelated nested
paths stay rejected. Guest `310adb71...`, Storm `2489c037...`, versionCode 9.
Signed APK `5f6b31b8988231df0dc9987f656347aae940b462baed91d8e9cadb2de6eaa143`,
448,470,858 bytes.

Verified on the API 37.0 4096-byte-page emulator:

* 6,835 cache files imported (including generated options and the completion
  marker holding the pinned cache-ZIP hash);
* cinematic → character selection, with logcat repeatedly showing
  `DH2FileGuard recovered repeated root` and successful prince model opens —
  the precise Test 8 failure is fixed;
* with the default `fit16by9=false`, the 2424×1080 display cropped the menu
  (`Start Game` offscreen); enabling **Fit game to 16:9** gave a 1920×1080
  surface and the full main menu;
* `Start Game` → `Single Player` opened the owner's `wolf` save, loaded the
  swamp level to 100 % and displayed live 3D gameplay and HUD; a joystick drag
  moved the prince and the camera; pause opened a working menu; confirming a
  return to the main menu updated the visible last-save timestamp from
  `18.04 06:05` to `02.10 00:07`, matching the guest's `PlayerLastSave` log;
* after pause/return, a force-stop and relaunch retained the marker and the
  updated timestamp.

Not verified / still open from this run: the cold restart displayed the
character menu cropped again even though `dh2-options.json` kept
`fit16by9=true`; loading that save again after the cold restart was not tested;
attack-button taps were sent but stills did not establish a hit; some `.tga`
opens are logged missing while the level renders; the 16 KiB guard is unchanged.

## 11. Test 10 — viewport correction, and Test 11 — saved-language preference

### Test 10 (versionCode 13, `dh2-options.json` fit default on)

Cause: with fit enabled the guest called `nativeSetPhone(1080,2009)` from
portrait `DisplayMetrics` *before* creating the 1920×1080 `GLSurfaceView`, so the
engine initialised a 1080×2009 GL viewport and the later
`onSurfaceChanged(1920,1080)` did not correct it. Correcting only the GL viewport
still rendered a cropped menu, proving the first phone-size call also fixes
cached engine projection/layout dimensions.

Change: `build/repack_test10_guest.py` replaces only the reviewed `nativeSetPhone`
call in `DungeonHunter2.onCreate` smali; the helper computes the largest 16:9
rectangle inside the real display metrics, logs original/fitted sizes, and
restores the GL viewport after each original surface-resize callback. With fit
off, the original sizes and viewport behaviour are untouched.

| Pinned artifact | SHA-256 |
| --- | --- |
| Test 9 input guest | `310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1` |
| Test 10 unsigned guest | `57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e` |
| Test 10 primary `classes.dex` | `03c71b7a981b15ac8d28129d9a0abe38d9356d8890c0d4f1374cbac654b9df72` |
| Test 10 helper `classes2.dex` | `bba5f019caf2a0cc0c6f6c8a8f673dc69ec792355af272b7f5020693820d3efe` |
| Final signed default-fit APK | `02ba96298aa2639e1bd3c3a34f0b54447756d85aed9e8ed85b3d53bf726964aa` |

Verified on the API 37.0 4 KiB emulator: fresh install imported 6,835 files
(~663 MiB) with the marker matching the owner ZIP; full character menu with
`Start Game` visible; two force-stop/cold-relaunch cycles still showed the full
menu; `Start Game` → `Single Player` loaded the saved Swamps level; three action
taps removed a nearby moth **and its health bar**; a joystick swipe moved the
player and the camera; pause → main menu → confirm returned to the full menu with
last save `02.10. 02:03`, and a cold relaunch loaded the saved level again. The
nearby enemy respawned on re-entry, so this is **checkpoint persistence, not
exact combat-state restoration**. Outer APK 49 unique entries, nested guest 220;
v3 signature, `zipalign -c -P 16 4`, all CRCs. Not established: every level,
other devices, 16 KiB pages.

### Test 11 (versionCode 14, `1.0-test11-language`)

Cause: the Java `Get_PhoneLanguage()` hook returned English (`0`), but the native
`SavegameManager::getLanguage()` preferred the `Language` value in
`dh2_settings.savegame`; the owner cache has `Language=2`, and in that archive
`data/text/menu.english` holds English while `data/text/menu.german` holds
Russian.

Change: `app/launcher-java/.../LanguagePreference.java` applies the checkbox to
the installed settings copy immediately before launch. With the box off it reads
or changes nothing. With it on it accepts only the observed 16-key layout, a
`Language` int32 in the expected position and the 14 trailing tutorial bytes;
if the value is nonzero it writes the original file once to
`dh2_settings.savegame.before-prefer-english.bak`, verifies the backup, changes
only that four-byte value to zero and atomically replaces the settings file.
Unknown format or storage failure leaves settings as they were and does not
prevent launch; the outcome is recorded. For the 294-byte owner fixture
(SHA-256 `3cb97b47cc9853d65dc596ccc0a8b715b82fe4794812435673a921b215f07c4a`)
only byte 207 differs (`02 00 00 00` → `00 00 00 00`).

| Verified | Not verified |
| --- | --- |
| `tests/fold7-build/test_language_preference.py`: exact fixture (when the owner ZIP is locally available), disabled, absent, malformed, backup, idempotency, byte-preservation | Full save semantics; any physical-device operation |
| Source/build validation: 28 host Java sources compiled with pinned JDK 17 against Android 17/API 37 `android.jar` + `hiddenapibypass.jar` → 42 classes; D8 → one `classes.dex`, minApi 29 | |
| 4 KiB API 37.0 emulator: v14 upgrade over Test 10; backup byte-exact; `Language` 2→0 with only byte 207 changed; diagnostic `UPDATED`; English saved-character menu, loading tips, HUD labels and pause menus; `Single Player` resumed the saved **The Boglands – Ancient Prison** level; movement and two attack taps removed a Bog Moth and its health bar; visible save time 02:03 → 04:03; cold relaunch reported `ALREADY_ENGLISH` with 04:03 and loaded the level again | |
| Strict 16 KiB API 37.2 emulator package (448,474,954 bytes, SHA-256 `d688b2a9f4da0c387ea1ddb0448d9de95dd3e00cdac224fca1b1a6e01fa2898e`): same settings result, English menu, saved level loaded, joystick movement, pause → main menu → confirm updated 02:11 → 04:21, cold relaunch `ALREADY_ENGLISH` with `10/02 04:21` and the level re-entered; no Scudo abort, fatal exception, signal 6 or strict origin-refusal marker; 16 KiB startup and level load each took **several minutes** | |

## 12. Side investigations

### 12.1 Save-job thread exhaustion (`patches/thread-exhaustion/`)

Android 9 x86 AVD evidence: `pthread_create failed: couldn't allocate
1040384-bytes mapped space: Out of memory` and `Not Created` **3,188 times**
(22:25:32–22:26:28), starting while the animated menu ran; the process still had
~25 threads; raising AVD RAM from 2 GB to 4 GB did not remove it (process VmSize
~4.17 GB, RSS 315 MB, >3 GB guest-available after the later abort); the final
abort was in Android 9's `libndk_translation.so` requesting a 131,072-byte arena
block. This supports address-space pressure without proving which allocation
caused it.

Patch: the `BNE` at engine RVA `0x32c534` is retargeted to the existing
synchronous `Savegame::UpdateJobs()` call at `0x32cc34` (replacement bytes
`be 01 00 1a`), hash-pinned to engine
`45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4`.

Result on the Android 9 AVD with 4 GB RAM and a disposable key: cinematic, title,
animated menu and saved character loaded; Single Player reached loading step 37
at 100 %; the saved 3D level rendered with player, enemy and touch controls; a
tap produced a visible combat/movement change; the process was alive ~30 s later
at 26 threads, VmSize 2,172,492 KiB, RSS 339,312 KiB, with **zero**
`pthread_create failed` / `Not Created` / `Memory exhausted` / `Fatal signal`
entries. Unsigned APK `8168af36...`, signed `d23680d7...`, patched engine
`ad33304f...`. Not checked: whether synchronous save jobs persist correctly,
every level, frame timing. Test 8 adopted this one-instruction change on the
Android 17 wrapper and survived ~6 minutes longer than Test 7 before hitting a
different failure.

### 12.2 16 KiB host pages (`patches/16k-port/`)

The shipped launcher maps 4 KiB guest pages directly and refuses a host page size
other than 4096; every recorded 16 KiB result used an isolated copy whose guard
was changed only in that copy.

| Stage | Result |
| --- | --- |
| Focused component test, API 37.2 `sdk_gphone16k_x86_64` (`PAGE_SIZE` 16384) | **35/35 checks pass** — adjacent 4 KiB guest mappings inside one 16 KiB host page, per-subpage protection/unmap/remap, private file mappings at 4 KiB-unaligned offsets, `ENOTSUP` for shared file mappings, `MADV_DONTNEED` on exactly one private-anonymous guest page preserving its neighbour, refusal for file/shared/unknown origins. x86_64 process; the ARM32 syscall path and the patched `guest_thread.cpp` were not part of it |
| First isolated ARM64 runtime trial | `libzbridge.so` `e18028f2...`, 16 KiB-aligned `LOAD` segments, signed APK `3c138043...`: guest linker, `libnativeinterface.so`, 32 JNI natives, Storm, GL shader processing — then guest bionic Scudo reported `corrupted chunk header ... memory corruption or a double free` and the process ended with signal 6. Nearby kernel logs showed repeated `do_madvise: addr ... not page aligned`. **Does not establish 16 KiB compatibility** |
| Broad-zeroing diagnostic | Zeroing every accessible guest range avoided the Scudo abort and let title, saved-character menu and a saved 3D level load, but is unsafe for file/shared mappings and was never shipped. A trace through saved-level loading saw 807 `MADV_DONTNEED` calls over 24,613 guest 4 KiB pages, all tagged anonymous; private vs shared could not be distinguished |
| Strict origin-tracking runtime trial | Strict bridge `dfca14dc...`, `0x4000`-aligned `LOAD` segments, signed APK `80ff2756...` (v3 signature, `zipalign -c -P 16 -v 4`): full-width interactive title and saved-character menu, entered the saved Swamps level, 100 % at 01:45:52 UTC and `SHOW OF THE HUD` at 01:46:17 UTC; the first run was cut short because the installed package was replaced by an older local APK (external, not a guest crash). The repeat run reached the same saved level at 100 % with a complete 3D scene, a 1.8 s joystick drag visibly moved player and camera, the pause button opened the full pause menu, `Main Menu` + confirm produced the guest's `GoToMainMenu` log sequence, and the returned menu showed the updated last save `02.10. 02:11`. Guest alive at the end, no Scudo abort and no origin refusal |
| Strict Test 11 package (API 37.2 16 KiB) | See section 11: English menu, saved level loaded, movement, save update, cold relaunch |

Six code-level blockers remain documented before this can be integrated: private
file-mapping materialisation is a snapshot (`SIGBUS` beyond EOF, later file
changes, shared mappings/writeback unimplemented); only `MADV_DONTNEED` for
proven private anonymous ranges is handled (`msync`, other `madvise`, `mremap`
still need guest-aware handling); Dynarmic was rebuilt with fastmem disabled and
boundary access/exclusive/fault/signal behaviour is unproven; JNI/GL/syscall
bridges hold direct guest pointers and long-lived mapped buffers needing a
permission/lifetime/concurrency audit; large mappings need bounded,
failure-atomic staging; the complete guest and host suites must run on both 4 KiB
and 16 KiB systems plus a real 16 KiB ARM64 runtime.

## 13. Contradictions and stale statements found in the imported documents

These are recorded rather than silently resolved. Nothing was edited.

1. **"No actual phone report or gameplay result exists yet."**
   `docs/design/CONTENTS.md` states this, but `evidence/phone-test3/` and
   `evidence/phone-test4/` contain real phone reports, and
   `docs/design/STANDALONE-TEST9/10/11` record emulator gameplay. `CONTENTS.md`
   describes the older Test 1-era snapshot and is stale.
2. **"Current checkpoint: test 5" banners.** `COMPATIBILITY-README.md`,
   `BUILDING.md`, `FINDINGS.md`, `ISSUES.md` and `DEVICE-TESTING.md` all open
   with a Test 5 banner and say Fold7 loading/gameplay "remain unverified" — the
   later standalone Test 7–11 documents are absent from those banners.
3. **"This build requires 4 KB host memory pages"** (`COMPATIBILITY-README.md`,
   `FOLD7-BUILD-README.txt`) alongside the extensive 16 KiB work in
   `patches/16k-port/`. Both are true only because the 16 KiB work is isolated
   and experimental; the README does not mention it.
4. **Three different "current" source commits.** `docs/design/BUILDING.md` cites
   `3094fd150ad7fe5891fd3741fd2f43892424277f` (Test 3/4 era; also
   `evidence/fold7-build/build-result.json`), `BUILDING.md` also cites local
   integration commit `6e7c65f`, `STANDALONE-TEST10-VIEWPORT.md` cites
   `9723cf0` for the Test 10 artifact, and `patches/16k-port/TEST11-STRICT-REBUILD.md`
   says to use `8b47d20` or a descendant. They refer to different points in time
   but none of them is labelled as historical in all places.
5. **`zettabridge-dh2.patch` hash.** `evidence/HANDOFF_VERIFICATION.json`
   records `97109771bbf071864a4e9aa8d73536b63be6ea3f50387871ae9420d2187cf3ae`
   for the "runtime cumulative patch", while `STANDALONE-TEST11-LANGUAGE.md` and
   `patches/16k-port/TEST11-STRICT-REBUILD.md` pin
   `7ce58278723b96fb39230e9f44caed4b8654cb5da0a6624bd0381f65ffdc74e8`. The file
   in this repository hashes to the latter (see `docs/LAYOUT.md` section 2); the
   handoff manifest describes the Test 5-era revision.
6. **Dynarmic patch hashes.** The copies under
   `upstream-modified/third_party/patches/` hash to
   `12b65214e8626fff46c6077180e9e1e743e281532577b09c4278ca9684ce15dc` and
   `25616b55eef59e0ffeb25c37016b093f067349e5069ad13370788057c2bad674`, not to
   the `6e16df08...`/`fea886fa...` values pinned in `TEST11-STRICT-REBUILD.md`.
   Those pins name the copies inside the ZettaBridge checkout.
7. **Page-size guard and the 16 KiB Test 11 result.**
   `STANDALONE-TEST11-LANGUAGE.md` describes both a 4 KiB package and a "separate
   strict 16 KiB Test 11 package ... for the API 37.2 emulator"; the ordinary
   builder's guard remains 4096, so only the isolated copy can run that package.
   The document says so, but the two hashes sit in the same paragraph.
8. **`FOLD7-BUILD-README.txt`** says "The main libDungeonHunter2.so remains
   byte-identical to the supplied engine" and the compatibility README repeats
   that claim historically. Test 5 changed that engine binary, and
   `TEST5.md`/`FINDINGS.md` explicitly mark the earlier statement as historical.

## 14. Acceptance gates still open

From `docs/design/ISSUES.md` (DH2-01 … DH2-11) and
`docs/design/DEVICE-TESTING.md`: host page size on the target device, real
ART/JNI integration, a complete verified cache, EGL/GLES/shader/texture
correctness, input and fold/unfold lifecycle, persistence across force-stop,
audio and sustained performance, legacy licensing/network services,
Bluetooth/multiplayer permissions, rebuild portability, and the (separate)
full native ARM64 rewrite. None of these has a phone pass result recorded in
this repository.
