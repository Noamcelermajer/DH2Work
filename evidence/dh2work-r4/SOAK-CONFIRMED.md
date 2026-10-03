# r4-sync soak confirmed

Continuous automated run, no interaction after the initial launch.

| | r3 (unpatched, `45891aad…`) | **r4-sync (`ad33304f…`)** |
| --- | --- | --- |
| Longest continuous run | 428 s | **917 s (15.3 min)** |
| Frames rendered | 10,080 | **23,520** |
| Guest RSS at end | 684 MB | 684 MB |
| Crashes | 3, at ~6.5–7.1 min | **0** |

Event trace: `render returned frames=23520 elapsedMs=911348`.

`dumpsys activity exit-info local.dh2.fold7` contains exactly two `status=139` entries,
at **17:14:32** and **17:22:41** — both *before* the r4 install at **17:26:11**. There is no
crash entry after the install. The running guest has been the same pid since launch.

So the fix now has **15.3 minutes clean against a 7.1-minute failure ceiling** — 2.1× the
longest run that previously survived, with the identical test procedure.

## What this does and does not establish

**Does:** the per-frame `pthread_create` churn was the immediate cause of the fatal
`malloc` failure, and removing it (one ARM instruction, RVA `0x32c534`, `BNE` retargeted from
`0x32cb74` to the synchronous `Savegame::UpdateJobs()` path at `0x32cc34`) stops the crash on
this device for at least 15 minutes of continuous play.

**Does not:** prove the guest address window is no longer at risk. `VmSize` still sits at
22.25 GB and RSS at ~684 MB, so the window is still nearly full; the patch removed this churn
source without enlarging the window. The log buffer also still held 3,883 early
`pthread_create failures`, so churn is reduced rather than eliminated. A longer run and a
human-driven session across several **map loads** remain the right checks — map loading is
where the three earlier failures actually happened.
