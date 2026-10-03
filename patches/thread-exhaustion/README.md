# Save-job thread exhaustion diagnostic

This is an **isolated emulator experiment**, not part of the Test 7 guest or the
baseline compatibility build. The patched game engine is proprietary and must be
supplied locally; this directory contains only the patch source and a static test.

## Evidence

On the Android 9 x86 AVD, Test 6 printed `pthread_create failed: couldn't
allocate 1040384-bytes mapped space: Out of memory` and `Not Created` 3,188
times from 22:25:32 through 22:26:28. The log first shows these failures while
the animated main menu is running, before Single Player loading. The process
still had about 25 threads. Raising AVD RAM from 2 GB to 4 GB did not remove
the failure: the game process had about 4.17 GB virtual size and 315 MB
resident at the Single Player submenu, while the guest had more than 3 GB
available after the later abort. The final abort was in Android 9's
`libndk_translation.so` while it requested a 131,072-byte arena block. This
supports process address-space pressure; it does not prove which allocation
caused it.

The exact Test 5/6/7 engine retains symbols. `updateJob_thread::Start()` at
RVA `0x317ef8` calls `pthread_create` at `0x317f24`; when it fails, it logs
`Not Created` and returns `-1`. Its thread entry `threadfun2` at `0x317fbc`
calls `Savegame::UpdateJobs()` at `0x314734`. In `Application::_Update(int)`,
the per-frame path branches from `0x32c534` to the worker start call at
`0x32cb74`. A state-17 path in that same function already calls
`Savegame::UpdateJobs()` synchronously at `0x32cc34` and rejoins the update
flow at `0x32c538`.

## Experiment

The patch changes only the `BNE` at RVA `0x32c534`: its destination changes
from the worker start call to the existing synchronous save-job call. It is
hash-pinned to the Test 5/6/7 engine SHA-256
`45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4`.
The replacement bytes are `be 01 00 1a`. The script verifies the ELF32 ARM
executable mapping, original bytes and old branch target, and writes a new
library without changing the input.

```text
python compatibility/thread-exhaustion/tests/test_patch_sync_jobs.py <extracted-libDungeonHunter2.so>
python compatibility/thread-exhaustion/patch_sync_jobs.py --input <extracted-libDungeonHunter2.so> --output <new-diagnostic-libDungeonHunter2.so> --report <report.json>
python compatibility/thread-exhaustion/package_sync_trial.py --base-apk <unsigned-test7-guest.apk> --output <new-unsigned-diagnostic.apk>
```

The packaging command accepts only the pinned unsigned Test 7 guest and
verifies the engine and Storm hashes. Sign its output with a **new local
development key** before installing it on a disposable AVD. Do not commit the
output APK or key.

The static test verifies that exactly this one instruction changes and the
branch reaches the original synchronous call. Save-job file I/O may now block
a render frame, and the state-17 synchronous path may have assumptions that
differ from other game states. Keep the Test 7 baseline separate while
assessing save behavior and frame timing.

## Android 9 emulator result, 2026-10-02

The locally signed diagnostic guest used the Test 7 Storm library plus this
one-instruction engine patch. It was installed with a **new disposable
development signing key** on the Android 9 x86 AVD with 4 GB RAM. The original
cache ZIP's 6,833 files (648,357,710 bytes) were copied in again, and the
Prince model's SHA-256 matched the source. Neither the APK nor the key is in
Git.

The cinematic, title, animated menu and saved character loaded. Single Player
advanced through loading step 36 at 100%; after a tap it reached step 37 and
rendered the saved 3D level with the player, an enemy, and touch controls. A
tap in the level caused a visible combat/movement change. The game process
was still alive roughly 30 seconds later at 26 threads, 2,172,492 KiB virtual
size and 339,312 KiB resident size. Its final log contained **zero**
`pthread_create failed`, `Not Created`, `Memory exhausted`, or `Fatal signal`
entries. The baseline Test 6 log had 3,188 failed thread creates and aborted
before gameplay on the same Android 9 image. The screenshot is saved as
`outputs/DH2-android9-gameplay-sync-diagnostic.png` in the task workspace.

The diagnostic APK hashes are: unsigned `8168af36b2d82cf6b897da2fe4ec382c816f6498840e3bb61aede2f23c877e20`,
signed `d23680d7d544e33ae1d6c4ee2c26ff7c7b173fb0a46aa1bc99742e271e7f1e16`,
and patched engine `ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f`.
The full log and APKs remain outside Git under the task's `work/emulator-test/thread-investigation/`.

This demonstrates a short gameplay run through the direct ARM32 guest on
Android 9. It has not checked whether synchronous save jobs persist correctly,
whether every level loads, or whether frame timing remains acceptable. The
Android 17 ARM64 ZettaBridge wrapper has a different translation stack and
must be tested separately before adopting this change there.

ZettaBridge's host uses a separate 64-bit process address space and reserves
guest memory there; each guest thread also gets its own Dynarmic JIT cache.
The Android 9 direct-guest address limit does not by itself establish a
ZettaBridge failure on Android 17. The same repeated worker scheduling is a
specific behavior to inspect if the Android 17 wrapper reaches the game loop.
