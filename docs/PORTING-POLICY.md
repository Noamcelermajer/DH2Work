# Porting policy

**Owner decision, superseding the "no ZettaBridge code" rule and the "no fork" framing.**

## What is allowed

- **Reading and copying ZettaBridge source is permitted**, including its Dynarmic patch text. The material is AI-generated and the owner has no objection to reuse.
- **Dynarmic may be used freely, patched, or forked.** It is 0BSD, so this was always licence-clean; it is now also policy-clean.
- **ZettaBridge may be used as an implementation reference** for how each mechanism is done — the guest linker, the JNI bridge, GL marshalling, the syscall layer, thread and TLS setup.

## What is still the goal

**A tailored host for this one game, not a generic compatibility layer.** The owner's position, which this document records rather than re-argues: *a tailored approach will and always be the better approach.* ZettaBridge is general-purpose — it carries breadth for many games, and this project has already measured how much of that breadth is dead weight here.

So the discipline is **copy the load-bearing mechanisms, not the generic breadth**. The distinction is concrete and already measured in `HOST-IMPORT-SURFACE.md` and `HOST-ARCHITECTURE.md`:

| Adapt from ZettaBridge | Drop |
| --- | --- |
| The synthesized `JNIEnv`/`JavaVM` bridge — the hardest, least diagnosable subsystem | All EGL (46 indices, 21 manual functions) — this engine imports **zero** EGL |
| Guest linker / library mode, carrier and borrower | `libandroid` and the NDK backend set — **zero** imports: assets, native window, looper, sensor, input, configuration (2,314 lines plus 8 of 10 chain dispatchers) |
| Thread, TLS and signal handling | 487 of 579 host-call indices — unreachable |
| Syscall translation for the reachable set | 300 of 392 GL entry points — this game needs **92** |
| The GLES marshalling generator's shape | GL diagnostics (1,768 lines), `cli/zbrun`, the hang watchdog |

That is the tailored approach: reuse the parts where correctness is hard-won and hard to debug, and refuse the parts that exist for games we will never run.

## Why this matters to the schedule

The previous policy made the JNI/ART bridge and the guest linker **reimplement-from-scratch** work — `HOST-ARCHITECTURE.md` sized it at roughly 4,500–6,000 lines of P1 with the JNI bridge as the biggest and least diagnosable risk. Adapting a working implementation instead of re-deriving it is the difference between the hardest part of the project being speculative and being mechanical.

## Licence

Unchanged in substance. ZettaBridge's PolyForm Noncommercial and Perimeter terms permit modification and sharing for non-commercial purposes, which is what this project is, and the notice already ships as `assets/licenses/ZettaBridge.txt` via `build_apk.py`. Copied files must keep their notices. Dynarmic is 0BSD with no notice obligation of its own, though its bundled externals under `DYNARMIC_USE_BUNDLED_EXTERNALS=ON` do carry notices that must be retained.
