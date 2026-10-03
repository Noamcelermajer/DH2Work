# GUEST-INSTRUCTION-MIX.md — what the guest actually *executes* per frame

**Status: MEASURED on the device (2026-10-03). The engine/sysroot split is 23.8 % / 76.2 % of
executed guest instructions, and 59.5 % of all executed guest instructions are inside named
soft-float/libm helper bodies (`__aeabi_fadd` 33.8 % and `__mulsf3` 23.7 % on their own).**
Everything below marked **[M]** was measured on this machine or this device; everything marked
**[P]** is pending and says exactly what blocks it. No number in this document is estimated,
inferred from symbol names, or carried over from another document unless it is labelled as such.

The question this answers: *of the guest instructions the JIT actually executes per frame, what
fraction is inside soft-float helper bodies versus the engine's own code?* Static call-site
counts (`SOFTFLOAT-CENSUS.md`: 28 209 `__aeabi_*` sites) cannot answer it, because a call site
says nothing about how often it runs.

---

## 0. Result

| quantity | value | how |
|---|---|---|
| **engine share of executed guest instructions** | **23.808 %** | **[M]** §7.2 |
| **sysroot (`libc.so` + `libm.so`) share** | **76.172 %** | **[M]** §7.2 |
| other guest (linker, GL/JNI stubs) | 0.019 % | **[M]** §7.2 |
| of which **named `__aeabi_*`/libm helper bodies** | **59.457 %** | **[M]** §7.2 |
| same split by measured wall clock (idle gaps capped) | engine 20.280 % / sysroot 79.699 %, helpers 60.224 % | **[M]** §7.3 |
| VFP-replacement frame win implied (`p × 21.25 ms × 0.889`) | **10.99 – 11.50 ms** (42.8 – 44.7 % of a 25.7 ms frame) by instructions; **11.13 – 11.65 ms** by wall clock | **[M]** §7.5 |
| the number in the brief's prior ("win 3–12 %") | **demolished**: the helper share is ~3.7× the top of the range the prior allowed | **[M]** §7.5 |
| the ranked worklist in `SOFTFLOAT-CENSUS.md` §2 | **does not survive**: 4 of its top 50 are in the measured top 50, and its highest-weighted helpers (`__aeabi_dmul`/`dadd`/`ddiv`, `powf`, `tanf`) are measured at ranks 53/76/259/401/349 | **[M]** §7.7 |
| sample counts | 166 218 records / 166 154 analysed, 0 dropped, interval 131 072 guest instructions, 99.963 % accounting | **[M]** §7.2 |
| block-size field verified against disassembly | 88/104 checkable blocks: 58 match a terminator rule exactly, 0 exceed their own terminator | **[M]** §7.4 |

Two earlier device runs of the *same* instrumentation are also on record and are why the window
matters: over process boot + intro cinematic + level load the split was engine 86.6 % /
sysroot 13.4 %, and the ring filled within ~20 s of process start. A frame-level number must come
from a gameplay window, which is what §7.1 describes.

---

## 1. Why a guest-PC histogram, and not a profiler

`simpleperf` attributes 82.7 % of frame CPU to `unknown`, because the JIT's code cache is
anonymous executable memory that has no symbols. The host does not have this problem: its own
crash path already resolves a guest PC to `library + offset`
(`core/src/process.cpp:223-233`, `Process::describe_address`), and the engine is not stripped
(31 021 named functions, `SOFTFLOAT-CENSUS.md:15`). So the missing piece is not symbolisation —
it is *sampling*, and inside the JIT the only honest sampling point is the guest PC.

The instrumentation therefore takes a **guest-PC histogram**, one sample per translated block,
and attributes each sample through the same table the crash report uses.

## 2. The instrumentation

Written to `DH2Work-toolchain\profile\`, built into `/root/dh2prof` (WSL ext4, throwaway).
Nothing in `DH2Work`, `DH_sc`, `DH_sc-pr` or `DH2Work-stage` is modified: the builder and its
inputs are *copied* into `profile\apkbuild\` first.

| file | what it does |
|---|---|
| `00-setup.sh` | copies the prepared checkout into `/root/dh2prof`, drops the copied `build/`, installs the two hook files, applies the anchored edits |
| `11-reinstall-hook-sources.sh` | refreshes just the four hook sources in an already-staged tree (re-running `00-setup.sh` would re-apply patches to a patched tree and fail loudly) |
| `apply-instrumentation.py` | the Dynarmic-side edits (arm64 backend), anchored so a missing/ambiguous match is fatal |
| `apply-zetta-patch.py` | the ZettaBridge-side glue (arm the sampler, dump the guest load map) |
| `dynarmic/zb_profile_hook.h/.cpp` | the sampler: gate, ring, per-thread time accounting, live snapshot, reset trigger |
| `zetta/profile.h/.cpp` | arming, the output path, the mapping table |
| `01-build.sh` | the documented NDK r29 build (NATIVE-BUILD.md Blocks H/I) against the staged tree |
| `10-build-apk.ps1` | wraps the instrumented `libzbridge.so` into a signed APK |
| `20-measure-device.ps1`, `21-wait-and-measure.sh` | install, capture, pull, analyse |
| `analyze-guest-pc.py` | the histogram: split, load-bias recovery, per-thread and per-library tables, top-N functions, VFP ceiling |
| `selftest-estimator.py` | proves the estimator and analyser recover a *known* split |
| `verify-block-sizes.py` | checks the recorded per-block instruction count against the real disassembly |

### 2.0 The four defects that stopped the first build from emitting anything

The instrumentation of §2.1 was built, installed and loaded, and still never wrote a sample
file. That was not one bug but four independent ones, each of which alone was fatal (the first
three are in the instrumentation, the fourth only appears on a device):

1. **The gate was never armed.** ZettaBridge set the JIT state pointer with
   `cfg.zb_profile_state = zb::profile_attach(jit_.get())` *after*
   `jit_ = std::make_unique<Dynarmic::A32::Jit>(cfg)`. `Jit::Jit(UserConfig conf)` and
   `Jit::Impl(Jit*, A32::UserConfig conf)` both take the config **by value**, and
   `A32AddressSpace` holds `const A32::UserConfig conf;` — its own copy. The assignment could
   never reach the copy that `EmitPrelude` reads, so `zb_profile_register()` never ran and
   `prof_gate.enabled` stayed 0 for every thread: the emitted prologue always short-circuited at
   its first test. Fixed by arming through `zb::profile_attach()` itself.
2. **The block size was never written.** `prof_last_size`/`prof_last_id` had offsets in
   `EmitConfig`, were read by the hook, and were written by *nothing*: the emitted prologue
   stored only the block PC. Every record therefore carried `size = 0`, and the analyser — which
   is right to do so — discarded all of them as implausible. Fixed by counting the guest
   instructions the frontend translates into a block
   (`IR::Block::GuestInstructionCount`, incremented in both A32 translators next to
   `CycleCount`) and storing that count in the stub.
3. **The dump needed a clean exit the capture never provides.** The ring was written from
   `Process::profile_finish()` (`run()` returning, `exit_host_process`, the abort path), but the
   capture stops the app with `am force-stop`, i.e. SIGKILL. Fixed by snapshotting the ring from
   the sampler thread every 15 s, written to `<samples>.tmp` and renamed, so a killed process
   still leaves a complete file.
4. A fourth, found only on the device: ZettaBridge runs **more than one `Process::run` in the
   same host process** (a bootstrap guest exits, the game's guest starts). The first run's
   `profile_finish()` stopped *and joined* the sampler thread, and the second run re-armed the
   gates but had no thread left to re-arm them after each thread burned its first ticket. The
   symptom was a sample file frozen at 701 records. Fixed by making the sampler thread live for
   the whole process and never joining it from the dump path.

### 2.1 The emitted sample point

`EmitArm64` (`third_party/dynarmic/src/dynarmic/backend/arm64/emit_arm64.cpp`, just after
`ebi.entry_point`) now emits this at **every translated basic-block entry**:

```asm
MOV  w17, #<this block's PC, bit 0 = the guest T flag>
STR  w17, [x28, #prof_last_pc]
MOV  w17, #<this block's guest instruction count>
STR  w17, [x28, #prof_last_size]
LDR  w16, [x28, #gate.enabled]     ; sampling on for this thread?
CBZ  w16, .continue
LDR  w16, [x28, #gate.ticket]
CBZ  w16, .continue
SUB  w16, w16, #1
STR  w16, [x28, #gate.ticket]
CBNZ w16, .continue
MOV  x0, x28
STR  x22, [sp, #spill]             ; save the one register the C hook may not clobber
MOV  x16, #<zb_profile_hook_msm>
BLR  x16
LDR  x22, [sp, #spill]
.continue:
```

Thirteen host instructions on the common path, no call and no relocation. Everything is
addressed off `x28`, the `A32JitState` pointer, so the generated code never needs a 64-bit
absolute address; the gate lives *inside* `A32JitState`, which is ordinary host memory, unlike
the code cache. `ebi.entry_point` is taken *before* this sequence, so a block reached by a
linked branch (which never returns to the dispatcher) burns a ticket too.

`regs[15]` is deliberately **not** touched. The first implementation stored the block PC there
and reasoned about the return-stack-buffer terminal; recording the block's own PC makes that
unnecessary, and the store was guest-visible state the measurement did not need to perturb.

### 2.2 Sampling design and why it is unbiased

* **One ticket per block, not per chain.** The obvious cheap hook — the dispatcher, which runs
  once per `GetOrEmit` — is wrong: a chain of linked blocks reaches `GetOrEmit` only at its
  start, so every sample would land on a chain-start block (loop headers, call targets, function
  entries) and the histogram would be biased towards precisely the code the worklist cares
  about. The prologue above is emitted at each block *entry*, so the ticket is burned exactly
  once per block execution.
* **The gap is random.** When a ticket runs out, the hook grants
  `U{K/2 … 3K/2}` with `K` = `ZB_PROFILE_INTERVAL` (default **131 072** guest instructions,
  chosen in §7.1), from a deterministic xorshift32 so a run is reproducible. A random
  post-decrement threshold makes the sample point a uniform draw over executed instructions:
  which block a sample lands on depends only on the instructions executed so far, not on the
  block's own size.
* **Then one instruction inside that block is chosen uniformly**, by the analyser, using the
  block's exact instruction count. So the composition is: uniform block execution × uniform
  instruction inside it = a uniform draw over executed guest instructions. No block-start bias,
  and no assumption about block size.
* **A sample is not a snapshot.** The hook records the block at whose entry the ticket expired,
  its exact instruction count, and the **nanoseconds this thread spent since the previous
  sample** (`std::chrono::steady_clock`). The same ring therefore yields both an instruction
  split and an independent wall-clock split; §7.3 compares them.
* **Sampling cannot dominate.** A sample costs one hash-free linear probe of a 64-entry table
  and one clock read, once per `K` instructions; the common path costs 13 host instructions per
  block. There is no per-sample allocation and no lock on the hot path.
* **Sampling stops by itself.** Generated code clears its own gate when the ticket runs out, and
  a 20 ms host thread re-arms it. If that thread ever fails to run, sampling stops rather than
  draining the ring.

### 2.3 A pure measurement window, and why it needs a trigger

The ring fills at ~1 000 samples/s on the render thread, so it is full long before the scene
under study is on screen — measured: the whole 1 Mi ring filled during boot and the intro
cinematic on the first device run (§0). There is no IPC into the guest, so the sampler polls for
a trigger file next to its output: `touch <samples>.reset` empties the ring, counters and
per-thread state, and the sampler deletes the file again, so one touch means one reset. That is
what produced the window in §7.

## 3. The output format

Two files, both derived from the one path the launcher already gives libzbridge
(`ZBridge.setReportFile`), so no new Java and no environment plumbing is needed:

* `zb-runtime-report.txt.guestpc` — a header plus one line per sample:
  `pc size block_id flags delta_ns thread_key`, where `pc` carries the guest T flag in bit 0
  (so the analyser knows whether to draw instructions 4 or 2 bytes apart) and `flags` is that
  same bit;
* `zb-runtime-report.txt.guestpc.maps` — the guest load map, written from
  `Process::file_mappings_`, the same table the crash report resolves against:
  `map <start> <length> <offset> <is_vaddr> <path>`;

plus, transiently, `zb-runtime-report.txt.guestpc.reset` (the window trigger of §2.3).

The mapping file is refreshed from `Process::record_file_mapping()`, i.e. on the guest thread
that owns the mmap, so it is complete without a second thread racing that vector — the guest
linker has mapped the engine's libraries long before the interesting part of a run. Both files
are written to `.tmp` and renamed, so a SIGKILL cannot leave a half-written file where a
complete one was.

`zb_profile_register()` arms each newly constructed JIT from `zb::profile_attach()`, because the
gate has to be valid before the thread's first block runs; `~GuestThread` unregisters it before
the state is freed.

## 4. Validation of the estimator (no device needed)

`selftest-estimator.py` takes the **real** guest sysroot libraries, builds blocks from their real
symbol tables (so the block-size distribution is the real one), walks a random schedule with an
injected region mix, applies the sampler's rule literally, and feeds the records to the real
analyser. 120 000 samples over a 3 686 106-block walk:

| | engine | sysroot |
|---|---:|---:|
| injected **region-choice probability** | 62.000 % | 38.000 % |
| injected **instruction share** that implies (exact expectation, mean block 61.8 vs 63.2 instructions) | 61.486 % | 38.514 % |
| ground truth over the walked prefix (285 147 206 / 174 245 044 instructions) | 62.071 % | 37.929 % |
| **recovered by the analyser** | **61.579 %** | **38.299 %** |

The recovered instruction shares sit within 0.5 pp of the walked prefix's true shares and within
0.1 pp of the exact expectation for the injected process — i.e. the estimator reproduces a known
answer, and the residual is consistent with the ~120 000-sample error. 0.122 % of drawn
instructions fell in a gap between symbols and were reported as unattributed rather than
attributed by guesswork. Two internal identities fall out of the same run and both are the checks
that the per-record `size` really is a block size:

* `sum(sample sizes) / total_instructions = 19 883 344 / 459 392 250 = 4.328 %`. Sampling every
  `K`=3 000 instructions *without* size bias would have put ~3.26 % of instructions in the ring
  (`120 000 x 124.6 / 459 392 250`); the measured 4.33 % is larger by exactly the factor by which
  the sample is size-biased, `E[N^2]/E[N]^2 = 165.69 / 124.63 = 1.33`, because a random
  instruction is more likely to fall in a large block. That size bias is the estimator working as
  designed, not an error: the analyser draws a uniformly random instruction *inside* the sampled
  block, which cancels it exactly.
* mean executed block 124.63 instructions over 3 686 106 blocks; mean **sampled** block 165.69.
  The gap between those two numbers is the size bias, quantified.

Note the deliberate trap this test exposes: the *instruction* share is not the region-choice
probability, because the two regions have different mean block sizes. An analyser that quoted
62.000 % would be wrong. **[M]**

Re-run after the analyser gained load-bias recovery, per-library and per-thread tables, the
idle-gap time cap and the block dump (400 000 records over a 12 294 298-block walk, exact
expectation 61.486 % / 38.514 %, walked-prefix ground truth 62.045 % / 37.955 %): recovered
**61.577 % / 38.298 %**, i.e. the same answer to 0.05 pp of the exact expectation, and the load
bias it picked for `libc.so` was `0x0` (2073/2271 probe samples inside a named symbol) rather
than the mapping start — the scoring rule, which the earlier run could not have known about,
correctly declines to shift an address space that is already in link-time coordinates. **[M]**

**This validates the estimator and the file format. It is not a measurement of the game and is
not reported as one.**

## 5. The instrumented build

| | value |
|---|---|
| source | `DH2Work-toolchain\profile\` + `/root/dh2prof/src/ZettaBridge` (throwaway copy) |
| toolchain | NDK r29 `linux-x86_64`, CMake/Ninja, Boost 1.83 headers, exactly `NATIVE-BUILD.md` Blocks H/I |
| **instrumented `libzbridge.so` (the one used for §7)** | **3 328 416 B, SHA-256 `03af00ff5ad024b1f5aeb771725dd9481436a43a1e67d01fb97396cc6350415e`** (kept at `profile\out\libzbridge-instrumented.so`) |
| earlier instrumented builds, kept for traceability | 3 325 672 B `f2a8ebe7…` (gate dead, size unwritten: emitted nothing); 3 328 280 B `345fa785…` (gate armed, sampler joined by the first dump); 3 328 824 B `86de2b5c…` (reset trigger added, sampler still joined) |
| shipped (unchanged) | 3 306 992 B, SHA-256 `25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` |
| markers in the binary | `strings` finds `zb-profile: sampling armed, interval=%llu capacity=%llu -> %s`, `zb-profile: ring reset -> %s`, `zb-profile: %s` (logcat tag `zbridge`) |
| APK | `profile\out\dh2-instrumented.apk`, 13 777 434 B, SHA-256 `fb21191d34e6ae711a10dc62bead71a32e28b9bcb2016fb95ce19998ad777fbb`, versionCode **24**, versionName `1.0-test5` (`DH2_VERSION_CODE=24 DH2_VERSION_NAME=1.0-test5 .\10-build-apk.ps1`) |
| signature | v3, certificate SHA-256 `daa24cd98557703003fb4518d1ed8504dee071f9620596592819176677772081` |
| device | `SM-F966B` (Galaxy Z Fold7), Android 16 SDK 36, serial `RFCY700ANZF`; installed lib verified on device as `03af00ff…` (`sha256sum /data/app/…/lib/arm64/libzbridge.so`) |

The APK was produced with the **sanctioned builder**
(`DH2Work-stage\compatibility\work\fold7-build\build_apk.py`, copied to
`profile\apkbuild\`), so its Java/DEX/manifest side is the same source state as the installed
app and only the one `.so` differs. Its signer certificate is byte-for-byte the same identity as
the installed app's, so the upgrade is `adb install -r` with no uninstall and no data loss
(verified with `apksigner verify --print-certs`). **[M]**

At the end of this work `profile\apkbuild\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so`
was put back to the shipped library (`25e8da7e…682d24`; the instrumented copy is kept as
`profile\apkbuild\libzbridge.so.shipped`'s counterpart in `profile\out\`), and
`DH2Work-stage\compatibility\work\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\` was
never written to at all. **[M]**

## 6. Reproduce

```powershell
wsl -d Ubuntu -- bash /mnt/c/.../profile/00-setup.sh     # stage + patch (fresh tree)
wsl -d Ubuntu -- bash /mnt/c/.../profile/11-reinstall-hook-sources.sh   # later: hook edits only
wsl -d Ubuntu -- bash /mnt/c/.../profile/01-build.sh     # build the instrumented .so
$env:DH2_VERSION_CODE='24'; .\10-build-apk.ps1            # sign it into an APK (versionCode >= 24)
adb install -r out\dh2-instrumented.apk
# launch, tap through the launcher and the intro, wait for gameplay, then open the window:
adb shell "touch /sdcard/Android/data/local.dh2.fold7/files/zb-runtime-report.txt.guestpc.reset"
# ... play for 2-3 minutes; the file is rewritten every 15 s while the run is live ...
# pull it (adb pull is broken on this machine: it cannot create its own log file):
cmd /c "adb exec-out cat <samples> > out\run.guestpc"
python analyze-guest-pc.py out\run.guestpc out\run.guestpc.maps `
  --sysroot  <assets>\zb\sysroot `
  --libroot  <assets>\zb `
  --libroot  <game-tree>\lib\armeabi-v7a `
  --census ..\DH2Work\docs\SOFTFLOAT-CENSUS.md --top 40 --out out\analysis.md `
  --dump-blocks out\blocks.txt
wsl -d Ubuntu -- python3 verify-block-sizes.py out/blocks.txt <ndk>/llvm-objdump --top 25
python selftest-estimator.py --libc <sysroot>/libc.so --libm <sysroot>/libm.so   # estimator proof
```

## 7. The measurement

### 7.1 The window

A `--guestpc` file is only a measurement if the window is stated, so: **[M]**

* Instrumented build installed as versionCode 24 (`03af00ff…`, verified on the device), app
  force-stopped and relaunched, launcher driven with taps through to the game.
* Gameplay confirmed by screenshot before the window opened (character in a dungeon level,
  enemy + HUD, engine rendering at 60 FPS).
* Window opened with `touch …guestpc.reset` at 20:53, closed when the file was pulled **152 s**
  later. Everything above this point in the run (boot, intro cinematic, level load) had been
  discarded by the reset.
* 166 218 records, 166 154 analysed (the live snapshot leaves the newest 64 reservations out),
  **0 dropped**, 0 rejected as implausible, interval 131 072 guest instructions.
* 10 guest threads registered; one thread key (`b4000079632a4730`) carried **99.42 %** of the
  instructions, no other thread reached 1 %. So the split below is a frame-thread split, not an
  all-threads average.
* Implied executed instructions `166 218 × 131 072 ≈ 2.18 × 10^10` in 152 s ≈ **1.4 × 10^8
  guest instructions/s**. The guest's own event trace reports 120 frames per 2 000 ms in this
  session (`dh2-events.txt`), i.e. **≈ 2.4 × 10^6 guest instructions per frame** — consistent
  with 21.25 ms of JIT time per frame at ~110 M instructions/s.

### 7.2 The split, by executed guest instructions

| region | instructions | share | ns (idle-capped) | time share |
|---|---:|---:|---:|---:|
| engine (`libDungeonHunter2.so`, `libStormGLOFT.so`, `libnativeinterface.so`) | 101 667 | **23.808 %** | 29 955 596 167 | 20.280 % |
| sysroot (`libc.so` 324 974 + `libm.so` 296) | 325 270 | **76.172 %** | 117 720 961 643 | 79.699 % |
| other guest (`linker`, GL/JNI stubs, liblog) | 83 | 0.019 % | 30 403 906 | 0.021 % |
| unmapped | 0 | 0.000 % | 0 | 0.000 % |
| **total** | **427 020** | **100 %** | **147 706 961 716** | **100 %** |

* accounting: recorded `sum(size)` = 427 020 vs the library's own `total_instructions` =
  427 180 → **99.963 %**;
* **95.084 %** of sampled instructions fall inside a named symbol (4.916 % unresolved: PLT
  veneers and inter-symbol gaps);
* of the sysroot bucket, **253 894 instructions = 59.457 %** of *all* executed guest instructions
  are in bodies whose names match the `__aeabi_*`/libm families — `__aeabi_fadd` alone is
  **33.798 %**, `__mulsf3` **23.748 %**.

### 7.3 The same split, by wall clock (independent channel)

The per-sample `delta_ns` is the thread's own elapsed time since its previous sample, so an idle
thread dumps its whole gap onto one record. Uncorrected, 50.496 % of all recorded nanoseconds
sit in single records above 20 ms (one audio `DecodeBlock` alone held 48.6 %), which makes the
raw time column useless. With records above 20 ms excluded from the time totals only (the
analyser's `--max-delta-ms`, default 20 ms; the instruction totals are untouched):

| quantity | instructions | wall clock |
|---|---:|---:|
| engine | 23.808 % | 20.280 % |
| sysroot | 76.172 % | 79.699 % |
| named helper bodies | 59.457 % | 60.224 % |

The two channels agree to 3.5 pp. They are computed from different columns of the same
records — the instruction counts come from `size`, the times from `delta_ns` — so the agreement
is a check on the split, not a tautology, and it is the check the design promised in §7.2 of the
previous revision.

### 7.4 Checks on the one input the sampler computes itself

`size` is the only quantity in the estimator that is the instrument's own claim: the number of
guest instructions in the block whose entry burned the ticket. `verify-block-sizes.py` reads the
block dump, disassembles each of the most-sampled blocks *of the very library the sample came
from* (forcing ARM or Thumb with `objdump --triple=…` from the recorded T flag) and counts
instructions up to the first guest terminator. Over the 25 most-sampled blocks of every library
(104 blocks, 88 checkable): **[M]**

* **0 blocks exceed the instructions up to their own terminator.** This is the direction that
  would matter: a `size` larger than the block can be would inflate that region's share.
* 46 match exactly the plain rule ("instructions up to and including the first instruction that
  writes the PC");
* 12 match exactly the second rule Dynarmic ends blocks on: the A32 translator also ends a block
  when the conditional-execution state changes. `__mulsf3` (recorded size 9) is the clean
  example: its 9 instructions are exactly the prefix before its first `…hs` instruction at
  `+0x24`, and that predicated instruction starts the next block;
* 30 are shorter than both rules and are single-instruction blocks: the hot ones are the
  `svc #0x5a008d` host-call stubs of the GL/JNI shim libraries, which the translator ends a
  block on by construction (and the engine's PLT veneers, `add; add; ldr pc`, match the plain
  rule exactly at size 3);
* 16 could not be checked (libraries whose load bias could not be recovered, below).

The checker itself was wrong twice before it was right, and both mistakes are worth recording
because they are the same mistake the sampler could have made: it first counted instructions from
the *drawn* instruction rather than the block entry, and it disassembled Thumb code as ARM
(producing two "impossible" verdicts that were its own). Both are fixed in the committed script.

**Load bias.** The sampler records the address the library is actually mapped at; `st_value` is a
link-time address, and the two differ by the load bias. The analyser therefore derives
candidate biases from the map entries and the ELF `PT_LOAD`s, scores each by how many probe
samples land in a named symbol, and uses the winner — reporting the candidates and their scores
so a wrong pick is visible. It matters: with no bias subtraction, engine samples scored
**0/4000** inside a named symbol; with the recovered bias, **3919/4000** in the boot run and
**1030/1238** here. `libDungeonHunter2.so` is the library used for symbolisation, and the runtime
`fix_guest_library` fixups rewrite dynamic-table entries and textrel markers rather than code, so
they do not move the functions the symbol table describes.

### 7.5 What the number implies

With `p` = the helper share and the brief's own arithmetic (`p × 21.25 ms × 0.889`, elimination
87–91 %):

* by executed instructions, `p = 0.59457` → **10.99 – 11.50 ms per frame** (42.8 – 44.7 % of a
  25.7 ms frame);
* by measured wall clock, `p = 0.60224` → **11.13 – 11.65 ms per frame** (43.3 – 45.3 %).

The brief's prior — "without it the win is only known to lie between 3 % and 12 %" — corresponds
to `p ∈ [0.04, 0.16]`. The measured `p` is ~3.7× the top of that band, so the prior should be
dropped rather than reconciled: it was an assumption about how often the helpers are *reached*,
and the answer is that two of them are the hottest code in the game. Because
`__aeabi_fadd` + `__mulsf3` alone are 57.5 % of all executed guest instructions, the highest
value change is a fast path for those two bodies (or inlining VFP adds/multiplies at their call
sites) — see §7.6 for what else is worth touching.

### 7.6 Top 40 guest functions by executed-instruction share

`time share` is the idle-capped wall-clock share of the same function.

| # | guest addr | size | region | kind | instructions | share | time share | function |
|---:|---|---:|---|---|---:|---:|---:|---|
| 1 | `0x0009f1e8` | 684 | sysroot | softfloat | 144325 | 33.798% | 35.306% | `__aeabi_fadd` |
| 2 | `0x000a06f4` | 496 | sysroot | softfloat | 101410 | 23.748% | 21.662% | `__mulsf3` |
| 3 | `0x00069540` | 764 | sysroot | sysroot | 26894 | 6.298% | 8.135% | `memcmp` |
| 4 | `0x0066ff48` | 2840 | engine | engine | 11018 | 2.580% | 1.049% | `glitch::collada::detail::CColladaSoftwareSkinTechnique::skin(glitch::collada::detail::SSkinBuffer*, glitch::scene::CMeshBuffer*)` |
| 5 | `0x00069441` | 204 | sysroot | sysroot | 10631 | 2.490% | 1.451% | `strlen` |
| 6 | `0x00068540` | 220 | sysroot | sysroot | 7558 | 1.770% | 1.895% | `__memcpy_forward` |
| 7 | `0x004c4998` | 368 | engine | engine | 7449 | 1.744% | 1.616% | `std::priv::_Rb_tree<std::string,…>::_M_find<char const*>` (map<string,map<string,int>>) |
| 8 | `0x0009f1b8` | 28 | sysroot | sysroot | 7056 | 1.652% | 0.637% | `__fe_getround` |
| 9 | `0x003116e8` | 72 | engine | engine | 6633 | 1.553% | 0.875% | `std::string::_M_range_initialize<char const*>` |
| 10 | `0x00414484` | 500 | engine | engine | 4763 | 1.115% | 1.018% | `std::priv::_Rb_tree<std::string,…>::_M_find<char const*>` (map<string,int>) |
| 11 | `0x003140ec` | 52 | engine | engine | 4692 | 1.099% | 0.524% | `std::string::string(char const*, std::allocator<char> const&)` |
| 12 | `0x0009f1d4` | 20 | sysroot | sysroot | 4211 | 0.986% | 0.875% | `__fe_raise_inexact` |
| 13 | `0x0031167c` | 108 | engine | engine | 3709 | 0.869% | 0.985% | `std::priv::_String_base<char,…>::_M_allocate_block(unsigned)` |
| 14 | `0x0009eb18` | 48 | sysroot | sysroot | 3699 | 0.866% | 2.208% | `__nesf2` |
| 15 | `0x00068280` | 652 | sysroot | sysroot | 2819 | 0.660% | 1.403% | `memmove` |
| 16 | `0x0009eb48` | 48 | sysroot | sysroot | 2636 | 0.617% | 1.539% | `__gtsf2` |
| 17 | `0x007d94f8` | 572 | engine | engine | 2571 | 0.602% | 0.330% | `render_handler_glitch::draw_mesh_primitive(int, void const*, int, unsigned int const*, int)` |
| 18 | `0x0009faac` | 596 | sysroot | softfloat | 2560 | 0.600% | 0.557% | `__divsf3` |
| 19 | `0xfcb8ed6c` | 0 | engine | PLT veneer | 2232 | 0.523% | 0.444% | `<libDungeonHunter2.so>` `.plt` |
| 20 | `0xfcb8ed70` | 0 | engine | PLT veneer | 2217 | 0.519% | 0.509% | `<libDungeonHunter2.so>` `.plt` |
| 21 | `0xfcb8ed74` | 0 | engine | PLT veneer | 2205 | 0.516% | 0.423% | `<libDungeonHunter2.so>` `.plt` |
| 22 | `0x00337288` | 380 | engine | engine | 2188 | 0.512% | 0.557% | `std::map<std::string,bool>::operator[]<std::string>` |
| 23 | `0x00887068` | 1020 | engine | engine | 2053 | 0.481% | 0.055% | `vox::VoxNativeSubDecoderIMAADPCM::DecodeBlock(void*, vox::SegmentState*)` |
| 24 | `0xfcb8eba4` | 0 | engine | PLT veneer | 1986 | 0.465% | 0.369% | `<libDungeonHunter2.so>` `.plt` |
| 25 | `0xfcb8ebac` | 0 | engine | PLT veneer | 1977 | 0.463% | 0.356% | `<libDungeonHunter2.so>` `.plt` |
| 26 | `0xfcb8eba8` | 0 | engine | PLT veneer | 1917 | 0.449% | 0.361% | `<libDungeonHunter2.so>` `.plt` |
| 27 | `0x00318254` | 68 | engine | engine | 1781 | 0.417% | 0.394% | `std::string::~string()` |
| 28 | `0x003369a8` | 212 | engine | engine | 1682 | 0.394% | 0.503% | `std::priv::_Rb_tree<std::string,…>::_M_find<std::string>` (map<string,bool>) |
| 29 | `0x00597884` | 988 | engine | engine | 1501 | 0.352% | 0.500% | `glitch::core::detail::CMatrix4Base<float>::mult3(CMatrix4Base<float> const&, CMatrix4Base<float>&)` |
| 30 | `0x007d6b54` | 648 | engine | engine | 1477 | 0.346% | 0.161% | `render_handler_glitch::fill_style::apply(glitch::video::IVideoDriver*, BufferedRenderer*, Vertex, int)` |
| 31 | `0x000a00e4` | 132 | sysroot | softfloat | 1176 | 0.275% | 0.490% | `__floatsisf` |
| 32 | `0x0050d49c` | 640 | engine | engine | 1100 | 0.258% | 0.208% | `glitch::scene::CBatchSceneNode::addVisibleSegments<glitch::scene::SFrustumBoxIntersector>(unsigned, SFrustumBoxIntersector)` |
| 33 | `0x006651a0` | 992 | engine | engine | 1011 | 0.237% | 0.124% | `glitch::core::ml<float, glitch::collada::SMatrix>(CMatrix4<float> const&, SMatrix const&)` |
| 34 | `0x00795130` | 936 | engine | engine | 794 | 0.186% | 0.137% | `gameswf::cxform::clamp()` |
| 35 | `0x007d7094` | 296 | engine | engine | 781 | 0.183% | 0.073% | `BufferedRenderer::queueIndexedTriangles(boost::intrusive_ptr<glitch::video::CVertexStreams> const&, unsigned short const*, int)` |
| 36 | `0x000a105c` | 32 | sysroot | softfloat | 780 | 0.183% | 0.430% | `__aeabi_fcmpeq` |
| 37 | `0x0069f570` | 388 | engine | engine | 665 | 0.156% | 0.025% | `InterpolateColours(int const*, int const*, int const*, int const*, int, int, int, int*)` |
| 38 | `0x000a107c` | 32 | sysroot | softfloat | 658 | 0.154% | 0.326% | `__aeabi_fcmplt` |
| 39 | `0x000a10bc` | 32 | sysroot | softfloat | 642 | 0.150% | 0.464% | `__aeabi_fcmpge` |
| 40 | `0x000a10dc` | 32 | sysroot | softfloat | 637 | 0.149% | 0.288% | `__aeabi_fcmpgt` |

Read as a worklist: after the two dominant float helpers, the next three are **string/memory
code** (`memcmp` 6.3 %, `strlen` 2.5 %, `__memcpy_forward` 1.8 %) and the engine's
`std::map<std::string,…>::_M_find` / `std::string` construction cluster (≈ 7 % together) —
i.e. a scene-graph or resource lookup that runs per frame and does string-keyed map lookups. The
soft-float comparison helpers (`__aeabi_fcmpeq/lt/ge/gt`, `__nesf2`, `__gtsf2`, `__fe_getround`,
`__fe_raise_inexact` ≈ 5 % together) are the same VFP story as the arithmetic ones. Engine PLT
veneers are ≈ 2.9 % on their own, which is a call-overhead observation, not arithmetic.

### 7.7 Against the static worklist (`SOFTFLOAT-CENSUS.md` §2, `NATIVE-PORT-SHORTLIST.md`)

The same session answers whether the existing ranked worklist survives. It does not, and the
reason is structural rather than a matter of degree: **the census ranks callers by weighted static
call-site count, and execution frequency is not a function of that.**

* Of the census's 105 ranked entries, **32 appear anywhere in the measured top 1155** and 73 do
  not appear at all; the median measured rank of those that do is **316**.
* Of the census's top 50, **4 are in the measured top 50** (3 in the top 40, 1 in the top 20).
* The census's own weighting model (weight = instructions in the helper's symbol range) makes the
  *largest helpers* rank highest, and the game does not run them: `__aeabi_dmul` `0xa03a0` is
  measured rank **53**, `__aeabi_dadd` `0x9ed98` rank **76**, `__aeabi_ddiv` `0x9f638` rank
  **259**, `powf` `0x1e761` rank **401**, `tanf` rank **349** — together well under 1 % of
  executed instructions, while the *small* single-precision bodies they displaced are the top 2
  (`__aeabi_fadd` 33.8 %, `__mulsf3` 23.7 %). Only 10 double-precision/libm entries appear in the
  entire measured top 1155.
* The three measured top-40 engine functions that *are* census entries are real
  (`CMatrix4Base<float>::mult3` static #3 → measured #29, `CColladaSoftwareSkinTechnique::skin`
  static #40 → measured #4, `glitch::core::ml<float,SMatrix>` static #49 → measured #33), so the
  census is not wrong about what exists — it is wrong about what *runs*.

The worklist should be re-derived from §7.6: a fast path for `__aeabi_fadd` and `__mulsf3`
(or VFP at their call sites) is 57.5 % of executed guest instructions on its own, and the next
tier is `memcmp`/`strlen`/`memcpy` plus the string-keyed map lookups, none of which the FP
call-site census can see.


### 7.8 What this measurement does not settle

It does not settle whether a per-function port is *worth* doing: execution frequency is
necessary but not sufficient, and a function that is 6 % of executed instructions but mostly
`memcmp`-shaped is a different proposition from one that is arithmetic-bound. It also does not
re-measure the 87–91 % per-helper elimination figure, which comes from a bit-exact VFP
replacement measured elsewhere; it supplies the *share* that figure multiplies.

## 8. Limitations, stated explicitly

1. **One window, one scene.** The numbers are for a 152 s gameplay window in one dungeon level
   on one device, opened by the trigger of §2.3 after boot, intro and level load were discarded.
   The same instrumentation over boot + intro + load gave engine 86.6 % / sysroot 13.4 %, so the
   number is a property of the window as much as of the game, and any other window needs its own
   capture.
2. **Function-level resolution only, and only for symbols.** A sample names a guest PC; the
   analyser maps it with the ELF symbol table. 4.9 % of instructions here land in PLT veneers or
   gaps between symbols and are attributed to the library, not to a name (§7.2).
3. **The `__aeabi_*`/libm boundary is a symbol-name boundary.** The "helper body" bucket is the
   set of sysroot `libc.so`/`libm.so` symbol ranges whose names match the soft-float and libm
   families. Soft-float code inlined into libc functions that are not named `__aeabi_*` counts as
   "other", and a VFP replacement would not touch it. The bucket is a lower bound in that one
   specific sense — but note the bucket's *upper* side: `__fe_getround`/`__fe_raise_inexact` are
   inside it and are exception/rounding bookkeeping, so replacing a helper call with one VFP
   instruction removes those too, which is what the 87–91 % figure is about.
4. **Instrumentation overhead.** Thirteen extra host instructions per translated block plus one
   host call per 131 072 guest instructions. The block-entry stores are unconditional, so they
   cost the same whether or not a ticket expires, and with a mean executed block of ~2.6
   instructions this is a real (if uniform) tax on the measured code — it does not change *which*
   blocks execute, so it does not change the split, but absolute timings and the `delta_ns`
   column are best read as approximate. A same-scene A/B against the shipped library was **not**
   run: `dh2-events.txt` has the engine's own `render returned frames=/elapsedMs=` trace for both,
   but this work did not drive the shipped build through the same window.
5. **Threads.** The histogram covers all guest threads; here one thread held 99.42 % of the
   instructions, so the split is effectively that thread's. The per-thread table in the report
   shows which threads contributed what instead of silently aggregating.
6. **Sampling is instruction-triggered, not timer-triggered.** A thread that spends a long time
   inside one block is sampled once per ticket crossing, not once per unit time, so sample
   *counts* are proportional to instructions rather than time. That is the right weighting for an
   instruction-share question and the wrong one for a latency question; the `delta_ns` column is
   what covers latency, and it needs the idle-gap cap of §7.3 to mean anything.
7. **Code that never runs is invisible, correctly.** The histogram cannot distinguish "cold
   because unused" from "cold because reached only on level load" within a single session; that is
   what the window and duration are for.
8. **The symbol files are local copies, not device copies.** The device's guest libraries live in
   the app's private files directory (`/data/data/local.dh2.fold7/files/…`), the app is not
   debuggable (`run-as` refuses), and the device has no root here, so the analyser resolves the
   map paths to the local libraries the guest APK was built from. The evidence that this is
   right: the load-bias probe puts 1030/1238 engine probe samples inside named symbols where a
   mismatched file would put ~none, and the disassembly check of §7.4 lands exactly on terminator
   boundaries for the hottest blocks. It is not a byte-for-byte proof.
9. **Two `fun:`-level caveats on the hot list.** PLT veneers are listed as their own entries
   because they have no symbol; they are the engine's import stubs, and their presence in the top
   20 says call overhead is ~2.9 % of executed instructions, not that 2.9 % sits in one function.
   And `size` for a veneer entry is 3 instructions, the whole veneer, so its share is real.
10. **The instrumented library is deliberately not shippable.** It is a measurement artefact. The
    shipped `libzbridge.so` is unchanged and still hashes `25e8da7e…682d24`; the instrumented
    source edits live only under `DH2Work-toolchain\profile\` and `/root/dh2prof`. The device end
    state is **not** this work's to state: the instrumented versionCode-24 build was installed and
    verified for the capture, and while this document was being written a concurrent install of a
    different build (`1.0-shrink25`, `libzbridge.so` `674e24f0…`, 2 972 592 B) replaced it, so the
    measurement's inputs are the artefacts in `profile\out\` (the pulled sample file, the maps
    file, the analysis and the block dump), not whatever is installed now.

9. **The `maps` file is per session.** Guest library base addresses change between process
   starts, so a sample file is only analysable together with the `maps` file from the same run.
