# Fold7 test 5: fix absolute paths during model loading

## Device evidence from Test 4

The supplied diagnostic ZIP covers 2026-10-01 21:46:15--21:46:49 (Asia/Jerusalem), on SM-F966B, Android 16. Test 4's guard did execute: it rejected the cache root ending in `/`. The earlier CFile basename out-of-range abort is absent. This run ends with SIGSEGV / status 139, replacing Test 3's SIGABRT / status 134.

Immediately before the fault, the engine prints a failed open for `prince_modular.bdae`. Its filename contains the cache root followed by another absolute `/storage/.../data/3d/characters/prince/prince_modular.bdae` path. The root is duplicated rather than the filename being relative.

The link register maps to `glitch::collada::COnDemandReader::read` at 0x60b2e4. The preceding instruction is an indirect call to the read-file object's seek method. The report has PC=0 and r0=0; its fault reporting is imprecise, so these registers are not an exact-instruction snapshot. Stack candidates include onDemand<char>::get, CMesh construction and CColladaFactory::createGeometry. They are a stack scan, not an unwound backtrace. Together with the failed model open, this strongly implicates a missing model input during deferred mesh loading.

There are 11,121 GL calls, an active context, and an earlier GL_INVALID_ENUM (0x0500). That GL error still needs separate tracing; it does not establish that a graphics-library failure caused this crash. The run uses fit16by9=false and preserveContext=false, with a 2184x1968 surface and context recreation after video playback. Unlike the Test 3 run, the settings were different. No graphics behavior is changed by Test 5.

## Root cause independently reproduced

The original `CFileSystem::open` finds a colon to decide whether to bypass its nonempty WorkingDirectory. This recognizes Windows-style absolute filenames, but does not recognize Android/POSIX leading `/`. Model input is opened initially through other paths, then deferred loading reopens the stored absolute filename. At that point this routine prefixes WorkingDirectory again.

The new probe sets the actual exported WorkingDirectory array, then calls the original ARM32 CFileSystem routine through the same ARM64 translator as the APK. Test 4 fails to open a real existing fixture when given its absolute name and a nonempty root. Test 5 opens it and reads the expected bytes. A separate pair reproduces the phone's lowercased `/storage/emulated/0/android/...` spelling using a filesystem alias. These tests use synthetic bytes to validate file access, not a real BDAE parse or GPU draw.

## Change

`patch_engine.py` checks the complete original engine SHA-256 and the exact 20 original bytes before replacing five ARM instructions at 0x56dd9c. The replacement keeps the prior colon case and adds a leading `/` case. Relative names continue through the original root-prefix code. No function addresses, file layouts or dynamic symbols move. There is no allocation and no new callback into the host. `engine_path_fix.S` and its linker script are the readable assembly source.

This is the first compatibility build that changes the game engine binary itself. Historical descriptions saying the engine is byte-identical refer to Tests 1--4. Test 4's directory guard and previous native/Java fixes remain. The Storm file wrapper additionally records successful or failed opens of `prince_modular.bdae`, preserving errno. Java reports that model's existence, size and first 32 bytes before engine startup. Those observations will distinguish another path failure from missing or incompatible cache content.

## Validation

- 13 rooted-path cases: original and fixed absolute names, the phone-style path, relative names, roots with/without a trailing slash, empty root, missing inputs, directory rejection and colon-path behavior.
- Five earlier file-open cases retain the reproduced original directory abort and verify the guarded behavior and valid bytes.
- Full native library loading, both native initialization entry points with an inert VM, existing shader/string/inline hooks, and 4096 engine helper inputs.
- Final package CRCs, engine/Storm hashes, bundled runtime equality, application/version metadata, signing and alignment checks.

See the saved test logs and JSON results for outcomes. Native probes do not exercise ART or Android's graphics driver. The Fold7 loading screen and gameplay still require a device retest. Russian menu text, aspect ratio, the earlier GL error, later gameplay and 16 KB host pages remain unresolved or unverified.

## Device check

1. Install Test 5 over Test 4, without uninstalling or importing the cache again.
2. Keep the settings from the last Test 4 run for the first comparison.
3. Repeat the same character/load action. If it loads, try movement and combat, then save/reload.
4. Export `DH2-test5-diagnostics.zip` after the run, whether successful or failed. Reopen the launcher after a crash, and export before starting another run.

The report should now show `DH2Model opened:` with one absolute cache root. A missing model probe or `DH2Model open failed:` will point to a remaining file or cache problem. A later crash may expose a separate engine or bridge issue.
