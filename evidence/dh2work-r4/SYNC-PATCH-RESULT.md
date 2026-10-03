# r4-sync: the one-byte thread-churn fix clears the crash that ended every session

## Result

| Build | Engine | Longest run | Outcome |
| --- | --- | ---: | --- |
| r3-profile | `45891aad…` (unpatched) | 428 s | **died 3×**, at 388 s, 428 s, ~440 s |
| **r4-sync** | **`ad33304f…`** (one byte changed) | **718 s and still running** | **0 SIGSEGV** |

r4-sync reached **18,600 frames / 718.5 s (12.0 minutes)** with the guest alive at
**683 MB RSS**, and the process exit history contains no crash after the r4 install — the
only `status=139` entry belongs to the previous r3 run at 17:22:41.

Same automated procedure for both: force-stop, launch, tap LAUNCH GAME, then idle while
sampling memory every 12 s. So the comparison is like-for-like.

## The change

One 32-bit ARM instruction, at RVA `0x32c534`:

```text
old  8e 01 00 1a   (BNE 0x32cb74 — start the updateJob worker thread)
new  be 01 00 1a   (BNE 0x32cc34 — the existing synchronous Savegame::UpdateJobs() path)
```

`patch_sync_jobs.py` pins `INPUT_SHA256 = 45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4`,
which **is** this build's engine, so it is the intended input and it applied without
adjustment. `Application::_Update(int)` already contains a state-17 path that calls
`Savegame::UpdateJobs()` synchronously at `0x32cc34` and rejoins the update flow at
`0x32c538`; the patch simply routes the per-frame path there instead of spawning a thread.

Notably, the patched output hashes to
`ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f` — **exactly the pinned
`TEST8_ENGINE_SHA256`** — so the project's earlier Test 8 *was* this single change, and this
build reproduces a previously pinned artefact from source plus one byte.

## Why it fixes the crash

The crash mechanism, established separately:

1. The guest is **one 4 GiB reservation** (`guest_memory.cpp:38-45`), bounded only by
   `mmap_limit = 0xFE000000`.
2. The engine spawned an update-job **thread per frame**, and after a few minutes every
   `pthread_create` failed — the captured guest log shows **785 consecutive
   `pthread_create failed: couldn't allocate 1052672-bytes mapped space`** over 34 s, all the
   same size, plus `malloc(4194304)` failing — while allocations under 1 MB still succeeded.
3. `malloc` then returned NULL, the engine stored through it unchecked, and the fault landed
   in guest wide stdio (`__vfwscanf` region, libc ≈ `0x66dfc`–`0x675d8`).

Removing the per-frame thread removes the churn that filled the guest window, so step 2 never
happens.

## Build identity

| Field | Value |
| --- | --- |
| versionCode / versionName | 18 / `1.0-work4-sync` |
| APK | 13,765,146 B, SHA-256 `f03fd8ecade45d70e037f0341f3f9aa4088089e2148d610ce39b696dff3ad375` |
| Nested engine (verified inside the shipped APK) | `ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f` |
| Storm | `2489c037…` (unchanged) |
| Device | SM-F966B, Android 16, 4096-byte pages |

## Caveats — this is promising, not yet proven

* **One 12-minute run.** The previous builds failed three times out of three at 6.5–7.1
  minutes, so a single clean 12-minute run is meaningful, but it is one run.
* **The window is still nearly full.** `VmSize` still sits at 22.25 GB and RSS at ~683 MB.
  The patch removes *this* churn source; it does not enlarge the guest window. If another
  source of thread or mapping churn exists, the same failure can return.
* **Some `pthread_create` failures still occur**: the log buffer held 3,883 of them early in
  the run, decaying only as the ring rotated (3,883 → 3,150 → 2,275 → 1,443). So thread churn
  is reduced, not eliminated.
* **Needs a longer, human-driven soak** that loads several maps, which is the interaction the
  crash was originally reported against.
* The engine hash is now `ad33304f…`, deliberately **not** the pinned `45891aad…`. This is an
  engine byte patch in the same spirit as the Test 5 path fix, and it should be recorded as
  such wherever engine identity is asserted.
* `patch_sync_jobs.py` self-reports `runtime_validated: false`; that refers to the original
  emulator context, and the run above is the device validation it lacked.
