# Two tracks, and the rule that keeps them from interfering

**Owner clarification.** Removing ZettaBridge remains the **real goal**. Improving the
ZettaBridge build is a **separate, personal-need track** — wanted on its own merits, and not
to be confused with, or allowed to slow, the removal.

## Track A — remove ZettaBridge (the real goal)

**End state:** the APK ships no `libzbridge.so`, no ZettaBridge-derived source, and boots the
game on our own host.

Components, all building toward that: `host/mem` (guest address space + memory syscalls),
`host/loader` (ELF32/ARM), `host/rt` (linker emulation, threads, TLS), `host/jni` (synthesized
`JNIEnv`/`JavaVM`), `host/p0` (first guest ARM32 execution under Dynarmic), then GL
marshalling (92 entry points), the Android target, launcher integration, and finally
`PreCodeReadHook`.

**The gate that everything currently waits behind: nothing has executed a single guest ARM32
instruction.** Three independent components — the loader, the memory layer and the runtime —
each stop at exactly that boundary, and each verifies its state *as state*. P0 exists to
close it.

## Track B — improve the ZettaBridge build (personal need)

**End state:** a tailored, faster fork. Already delivered: **31% less source, 92 of 579
host-call indices, GL entry points 392 → 92, running on device with 0 SIGSEGV.**

In flight: the full `__aeabi_*` **VFP libc patch** (~11 ms/frame modelled), **CPU affinity +
120 Hz display mode**, and **GL redundant-state elimination**.

## Where they genuinely interfere, and the fix

**One phone.** Every result that matters needs a device run, and agents have been installing
over each other mid-measurement — the sampler lost a window to an unrelated install, and the
shrink result was briefly unverifiable because its APK got replaced minutes later. Version
codes have also collided (`24`, `25`) causing a downgrade refusal.

**Allocation rule — claim a version code, do not pick one:**

| Workstream | Track | versionCode |
| --- | --- | ---: |
| VFP libc patch ship | B | **26** |
| CPU affinity + 120 Hz | B | **27** |
| GL redundant-state elimination | B | **28** |
| Native ARM64 hook | B (fork) | **29** |
| Own-host P0 / integration | A | **30+** |

One install at a time. If a run is interrupted by another agent's install, re-install and
retry rather than reporting a failure.

## Where they genuinely help each other

Measured findings are **track-agnostic** and both tracks consume them: the instruction-mix
split, the FPSCR/denormal audit, the exact import surface, the packed-relocation discovery,
the frame-limiter conclusion.

**And one change advances both.** The VFP `__aeabi_*` libc patch is a **guest-side** patch —
it works identically on the ZettaBridge fork and on our own host when that boots. It is
currently the single largest measured win in the project (**59.5% of executed guest
instructions are in those helper bodies**) and it is not a ZettaBridge improvement at all.
Neither track owns it; both benefit.

## What must not happen

- Track B must not become the destination. The fork is a **base and a hedge** — a good one,
  since it already runs the game — but it is 30,154 lines of ZettaBridge lineage that we
  would then own and maintain forever.
- The VFP patch and the hook must not be blocked behind Track A. Both are shippable on the
  fork today, and both are real performance wins the player will feel.
- No claim from one track may be presented as progress on the other.
