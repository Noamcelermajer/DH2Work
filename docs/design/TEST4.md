# Fold7 test 4: stop the reproduced file-path abort

## What the owner's Test 3 report establishes

The report was exported after the 2026-10-01 21:22:17--21:23:09 device session (Asia/Jerusalem). The cinematic completed. The guest later terminated with signal 6 (SIGABRT), with 735 nativeRender calls and 40,389 GL calls. There were no reported missing host calls. Both experimental switches were enabled: fit16by9=true and preserveContext=true. There was one surface creation, with a 2184x1228 view, and rendering continued after the video Activity closed. This run therefore does not show a graphics-context recreation at that transition.

The native stack candidates are not a full unwound backtrace. Nevertheless, consecutive return addresses identify a specific executable failure chain:

| Reported engine address | Matching original instruction/caller |
| --- | --- |
| 0x8a28ea | Return after abort in std::__stl_throw_out_of_range |
| 0x56ca64 | Return after out_of_range in CFile's complete-object constructor |
| 0x56dde8 | Return after CFile construction in CFileSystem::open |
| 0x5704f8 | Return after CFileSystem::open in CReadFile::openFile |
| 0x570788 / 0x5707d8 | CReadFile constructor / createReadFile |

The libc addresses are FILE OFFSETS, not ELF virtual addresses. libc offset 0x4b7fe maps through its executable PT_LOAD to virtual address 0x4c7fe, in abort. Naively looking up the file offset as a virtual address gives the wrong symbol. The engine's text segment has matching offsets/virtual addresses.

## Independent reconstruction and reproduction

CFile constructs its stored pathname, finds the last slash or backslash, adds one, then checks `basename_position >= string_size`. That is the check behind this abort. An input ending with a path separator has no basename, so the original no-exceptions STL prints `basic_string` to buffered stdout and calls abort.

The new native probe calls the ORIGINAL exported CFileSystem::open, not a rewritten substitute. A real directory with a trailing slash reproduces exit 134 and the same 0x56ca64 / 0x56dde8 stack addresses. An ordinary existing file passes and its contents can be read. This is stronger evidence than a matching error number alone.

The phone's exact failing pathname is still unknown: Test 3 recorded only selected filename suffixes in its open history, excluding directory paths. Thus the directory-path trigger is a reproduced, strongly supported explanation, not yet confirmed by a phone guard message. A corrupted string could also reach the same check; we have not disabled that check.

## Narrow fix

The reviewed Storm import installer now also substitutes the ENGINE'S fopen import with a small C wrapper. An input ending in slash/backslash is rejected with null and EISDIR before the unsafe CFile constructor. The original caller already handles null: the native probe verifies original createReadFile returns null. All other fopen requests go to Storm's original bionic fopen import. opendir and openat are unchanged. The game engine binary remains byte-for-byte original; no assertions, licensing decisions or save files are changed.

The wrapper logs the rejected path to Android liblog AND unbuffered fd 2, which Test 3's runtime already captures. The engine's puts import also gets a forwarding logger so buffered STL abort reasons survive. Both helpers are position independent; the patcher rejects a generated GOT/data/BSS or dynamic-relocation section because a pasted machine-code fragment cannot rely on dynamic relocation of its own addresses. The final helpers fit in the existing checked executable gap in Storm.

## Verification and limits

`tests/run_file_open_tests.py` runs original ARM32 code through the same built ARM64 translator used by the APK, under a QEMU ARM64 host. It checks normal original file reading, the original directory abort, repaired normal reading, repaired directory rejection plus diagnostic persistence, and a missing-file failure. `run_native_probe.py` separately checks all three library loads, JNI_OnLoad with an inert VM, the installed shader/string/inline hooks and 4096 engine helper inputs. The APK verifier checks original engine hash, new Storm hash, runtime bytes, signatures/upgrade version, and ZIP integrity.

An initial helper build used a non-relocated GOT address; the native probe caught this before packaging. Hidden local helper symbols plus the new relocation-free gate fixed it. A CLI probe without Android logd also showed why logging only to liblog was insufficient; the final helper writes to captured fd 2 as well.

There is no attached Fold7, ART session or Android GPU here. No claim is made that the phone now passes the loading screen or that later gameplay is stable. The Russian language issue remains: the phone locale is en_GB and Prefer English was selected, but game/menu strings remain Russian. The Java language callback alone has not fixed that. Existing save/cache language behavior still needs separate tracing.

## Device check

1. Install Test 4 OVER Test 3. Do not uninstall, delete saves or reimport the cache.
2. Keep your last Test 3 settings unchanged for this comparison (your attached session used both display/context switches). The only gameplay compatibility change is the file-open guard.
3. Repeat the same character/load action that produced the fairy loading-screen failure.
4. If loading succeeds, try movement and combat, then save/reload. Export diagnostics whether it succeeds or fails.
5. If it fails, reopen the launcher and export DH2-test4-diagnostics.zip before another run. The report should reveal whether the guard rejected a path and whether a later failure is different.

Cache stays at `/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/`.

Reference: POSIX fopen only mandates EISDIR for directory opens requiring write access; one cannot assume a successful read-only fopen returned a regular file: https://pubs.opengroup.org/onlinepubs/007904975/functions/fopen.html . The fix is scoped to this game's file API, not a global change to libc semantics.
