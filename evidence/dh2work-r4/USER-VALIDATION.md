# User-driven validation: 30 minutes, 8 map loads, no crash

**Source: reported by the project owner, not instrumented by an agent.** Recorded here
because it is the acceptance test that matters, and it is distinct from the instrumented
evidence in `SOAK-CONFIRMED.md`.

## What was reported

* Played on the physical Fold7 for **about 30 minutes** on the **r4-sync** build
  (versionCode 18, engine `ad33304f…`).
* **8 map loads** completed.
* **No crash.**
* The session ended because the **user stopped it deliberately** — not a fault.

An earlier note in this session wrongly attributed the end of that run to an agent's
`am force-stop`. The user has clarified that they stopped it themselves. Corrected.

## Why this specific result matters more than the instrumented soak

Map loading is precisely where every previous build died. Three consecutive runs ended at
`m_loadStep 10` with a failed `malloc` followed by a store through the null pointer, at
~6.5–7.1 minutes. The fix — one ARM instruction at RVA `0x32c534`, routing the per-frame
`BNE` away from a thread-spawning path to the existing synchronous `Savegame::UpdateJobs()`
path at `0x32cc34` — removes the per-frame `pthread_create` churn that exhausted the guest's
single 4 GiB window.

The instrumented evidence showed the *absence* of a crash over 15.3 minutes of continuous
rendering (23,520 frames, zero SIGSEGV, no `status=139` after the install). This report adds
what instrumentation could not: **repeated map loads actually completing, in human hands,
for twice the previous failure timeout.**

## What it still does not establish

* It is **user-reported**, without a captured diagnostic export from that run. A
  `DH2-diagnostics` ZIP from a long session would put it on the same evidential footing as
  the automated soak. Worth capturing next time via the launcher's **EXPORT DIAGNOSTIC ZIP**
  (it lands in `/sdcard/Dh2_crashes/` on this device).
* The guest address window is still nearly full (`VmSize` 22.25 GB, RSS ~684 MB) and the log
  still contained 3,883 early `pthread_create` failures — so the churn is **reduced, not
  eliminated**. Another churn source could reintroduce the failure.
* Saves, reload, quests, audio and the later game remain untested.
* Nothing here speaks to the 180°-rotated 2D UI observed separately in
  `ui-rotated.png`, whose relationship to the orientation guard is still unknown.
