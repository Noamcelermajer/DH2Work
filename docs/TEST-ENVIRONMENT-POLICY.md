# Test environment policy

**Owner decision.** Subagents test **off-device**. The **Lead alone** tests on the physical
phone. This exists because device contention has already cost three failed attempts — a lost
gameplay window, a shrink result that briefly could not be verified, and colliding version
codes.

## What subagents use (all off-device)

1. **`zbrun` — the host CLI, built for Linux x86_64 in WSL2.** This is the primary harness.
   ZettaBridge ships `cli/zbrun`, and it runs the guest under Dynarmic **with no Android and
   no phone**. It is good for exactly what most workstreams need: guest execution, the loader,
   the memory layer, syscall handling, instruction counting, and correctness A/Bs. Note the
   prebuilt binary at `DH2Work-toolchain\native-build\out\android-arm64\zbrun` is an
   **android-arm64** build and will not run on Windows; build the host target in WSL instead.
   A previous workstream already validated its pipeline this way.
2. **Component unit tests.** Every component built so far carries real ones — `host/mem`
   (ctest 4/4, `memtest` 55/55 at both page sizes, `memtest_real` 28/28), `host/loader` (32
   cases, 102,378 checks), `host/rt` (395 checks, 15 cases).
3. **The run harness at `DH2Work-toolchain\host\harness\`** — `dh2run` executes guest ARM32
   under Dynarmic **on the host**, including **VFP**. `dh2selftest` is 44 checks / 0 failures.
   This is now the preferred way to execute a guest instruction or function off-device.
   **Correction to what this document previously said:** ARM **VFP** no longer requires
   `qemu-arm`. Confirmed by execution — `vadd.f32 2.5f + 3.5f` gives `r0 = 0x40c00000 = 6.0`.
   It also runs real engine functions to a breakpoint.

   `qemu-arm` (with `gcc-arm-linux-gnueabihf`) remains a useful independent second opinion for
   ARM32 semantics, and still matters because **Unicorn 2.1.4 cannot execute a single ARM VFP
   instruction** on any of its 33 ARM CPU models — so anything VFP-related must not be built on
   Unicorn.

   Known limits of `dh2run`: no relocation processing yet (so any function that `bl`s through a
   PLT diverges — use `host/loader`'s 59,623 relocations and `Dh2UserCallbacks` as the seam),
   no syscall layer (`SVC` stops), one core and one thread, and no Thumb-32 disassembly trace.
4. **`dh2_oracle.py`** (Unicorn) for differential testing of ARM32 integer code against the
   pristine engine.

## What requires the phone — Lead only

Real GPU rendering, **in-game FPS**, display mode and refresh rate, ART/JNI behaviour, and
anything Adreno-specific. If a result depends on those, it is a Lead job, run **serialised**
with the version-code allocation in `TWO-TRACKS.md`.

## Android emulator: not currently available

There is **no emulator installed** — no `C:\Android\Sdk\system-images`, no AVDs, no
`emulator.exe`. The project has prior emulator work (`TEST6-EMULATOR.md`,
`TEST7-EMULATOR.md`, `package_emulator_guest.py`), but the SDK components are not present now.

If it is wanted, the trade is real and should be decided deliberately:
- An **arm64 system image on an x86_64 host** is full software CPU emulation. The guest is
  already emulated once by Dynarmic; this emulates the host too. Expect it to be very slow,
  and it would still not provide a faithful Adreno GL driver.
- An **x86_64 system image** requires building the host for **x86_64 Android**. Dynarmic
  does have an x64 backend (`InterpreterFallback` is emitted only by `a32_emit_x64.cpp`), so
  this is possible — but it is genuine porting work, not a setup step.

**Recommendation:** use `zbrun` for everything it can reach and treat the emulator as
unjustified unless a workstream specifically needs GL behaviour without a phone.

## Consequence for agents

Do not request device time for anything measurable off-device. State plainly what you could
not verify off-device and why, rather than installing on the phone. "Verified under `zbrun` on
host; device behaviour unverified" is the expected shape of a report.
