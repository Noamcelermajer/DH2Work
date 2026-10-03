# GUEST-INSTRUCTION-MIX.md — what the guest actually *executes* per frame

**Status: METHOD AND INSTRUMENTATION COMPLETE; THE DEVICE NUMBER IS NOT YET MEASURED.**
Everything below marked **[M]** was measured on this machine; everything marked **[P]** is
pending and says exactly what blocks it. No number in this document is estimated, inferred from
symbol names, or carried over from another document unless it is labelled as such.

The question this answers: *of the guest instructions the JIT actually executes per frame, what
fraction is inside soft-float helper bodies versus the engine's own code?* Static call-site
counts (`SOFTFLOAT-CENSUS.md`: 28 209 `__aeabi_*` sites) cannot answer it, because a call site
says nothing about how often it runs.

---

## 0. Result so far

| quantity | value | how |
|---|---|---|
| instrumented host builds | yes | **[M]** §2 |
| the measurement itself | **not yet taken** | **[P]** §7 — the phone left `adb` before the capture |

The instrumentation is built, signed into an installable APK, and the analyser is validated
against a synthetic ground truth. What is missing is one device session, and the only reason it
is missing is that the phone is not currently attached.

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
| `apply-instrumentation.py` | the Dynarmic-side edits (arm64 backend), anchored so a missing/ambiguous match is fatal |
| `apply-zetta-patch.py` | the ZettaBridge-side glue (arm the sampler, dump the guest load map) |
| `dynarmic/zb_profile_hook.h/.cpp` | the sampler: gate, ring, per-thread time accounting, text dump |
| `zetta/profile.h/.cpp` | arming, the output path, the mapping table |
| `01-build.sh` | the documented NDK r29 build (NATIVE-BUILD.md Blocks H/I) against the staged tree |
| `10-build-apk.ps1` | wraps the instrumented `libzbridge.so` into a signed APK |
| `20-measure-device.ps1`, `21-wait-and-measure.sh` | install, capture, pull, analyse |
| `analyze-guest-pc.py` | the histogram: three-way split, top-N functions, VFP ceiling |
| `selftest-estimator.py` | proves the estimator and analyser recover a *known* split |

### 2.1 The emitted sample point

`EmitArm64` (`third_party/dynarmic/src/dynarmic/backend/arm64/emit_arm64.cpp`, just after
`ebi.entry_point`) now emits this at **every translated basic-block entry**:

```asm
MOV  w17, #<block PC>              ; the block's own first guest instruction
STR  w17, [x28, #prof_last_pc]     ; what the analyser will attribute the sample to
STR  w17, [x28, #regs+15*4]        ; see 2.3: this is a correctness fix, not bookkeeping
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

Eleven host instructions on the common path, no call and no relocation. Everything is addressed
off `x28`, the `A32JitState` pointer, so the generated code never needs a 64-bit absolute
address; the gate lives *inside* `A32JitState`, which is ordinary host memory, unlike the code
cache.

Register choice is argued, not guessed: `abi.h` reserves `X28`=state, `X27`=halt, `X26`=ticks,
`X25`=fastmem, `X24`=pagetable. `X19`–`X23` and `X9`–`X15` are register-allocation targets, but
at a block entry point the IR block header has no live values, so `X22` (the last allocatable
callee-saved register, named nowhere else in the arm64 backend) and `X16`/`X17` (the ABI scratch
registers) are free. The stub saves and restores `X22` anyway.

The scaffolding is behind `UserConfig::zb_profile_sampling`, which is false in every shipped
configuration, and the whole feature is one `#ifdef`-free `if` in the emitter. **This is
throwaway measurement code and the shipped `libzbridge.so` does not contain it** — the shipped
library is still byte-identical to `25e8da7e…682d24`.

### 2.2 Sampling design and why it is unbiased

* **One ticket per block, not per chain.** The obvious cheap hook — the dispatcher, which runs
  once per `GetOrEmit` — is wrong: a chain of linked blocks reaches `GetOrEmit` only at its
  start, so every sample would land on a chain-start block (loop headers, call targets, function
  entries) and the histogram would be biased towards precisely the code the worklist cares
  about. The prologue above is emitted at each block *entry*, so the ticket is burned exactly
  once per block execution.
* **The gap is random.** When a ticket runs out, the hook grants
  `U{K/2 … 3K/2}` with `K` = `ZB_PROFILE_INTERVAL` (default 3 000 guest instructions), from a
  deterministic xorshift32 so a run is reproducible. A random post-decrement threshold makes the
  sample point a uniform draw over executed instructions: which block a sample lands on depends
  only on the instructions executed so far, not on the block's own size.
* **Then one instruction inside that block is chosen uniformly**, by the analyser, using the
  block's exact instruction count (`prof_last_size`, computed by the translator's own
  start/end locations). So the composition is: uniform block execution × uniform instruction
  inside it = a uniform draw over executed guest instructions. No block-start bias, and no
  assumption about block size.
* **A sample is not a snapshot.** The hook records the block that *just finished*, its exact
  instruction count, and the **nanoseconds this thread spent since the previous sample**
  (`std::chrono::steady_clock`). So the same ring yields both an instruction-count split and an
  independent wall-clock split; where they disagree, instructions-per-nanosecond differs between
  the regions and the report says so instead of hiding it.
* **Sampling cannot dominate.** A sample costs one hash-free linear probe of a 64-entry table
  and one clock read, and happens once per `K` instructions; the common path costs 11 host
  instructions per block. There is no per-sample allocation and no lock on the hot path.
* **Sampling stops by itself.** Generated code clears its own gate when the ticket runs out, and
  a 20 ms host thread re-arms it. If that thread ever fails to run, sampling stops rather than
  draining the ring.

### 2.3 A correctness fix the design forced

`regs[15]` is *not* always up to date at a block entry. Every terminal writes the next PC —
`LinkBlock` (`emit_arm64_a32.cpp:79-80`), `LinkBlockFast` (`:91-92`), `A32BXWritePC` (`:564`,
`:576`), `A32CheckMemoryAbort` (`:177-178`) — **except the return-stack-buffer pop**
(`:96-116`), which branches straight to its target when the guess hits and never writes
`regs[15]`. Since `ReturnStackBuffer` is part of `all_safe_optimizations`
(`interface/optimization_flags.h:55`), and every guest function return goes through it, reading
`regs[15]` alone would have mis-attributed a large fraction of blocks to a stale PC. The
prologue therefore stores the block's own PC into `regs[15]` as well. The RSB's own comparison
(`:109`) still reads the *pre-store* value, so that check is unaffected.

This is exactly the kind of failure mode that would have survived a casual implementation and
produced a plausible-looking, wrong hot list. It was found by reading the terminals, not by
running.

## 3. The output format

Two files, both derived from the one path the launcher already gives libzbridge
(`ZBridge.setReportFile`), so no new Java and no environment plumbing is needed:

* `zb-runtime-report.txt.guestpc` — a header plus one line per sample:
  `pc size block_id flags delta_ns thread_key`;
* `zb-runtime-report.txt.guestpc.maps` — the guest load map, written from
  `Process::file_mappings_`, the same table the crash report resolves against:
  `map <start> <length> <offset> <is_vaddr> <path>`.

`zb_profile_register()` arms each newly constructed JIT's gate from `EmitPrelude`, because the
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

**This validates the estimator and the file format. It is not a measurement of the game and is
not reported as one.**

## 5. The instrumented build

| | value |
|---|---|
| source | `DH2Work-toolchain\profile\` + `/root/dh2prof/src/ZettaBridge` (throwaway copy) |
| toolchain | NDK r29 `linux-x86_64`, CMake/Ninja, Boost 1.83 headers, exactly `NATIVE-BUILD.md` Blocks H/I |
| instrumented `libzbridge.so` | 3 325 672 B, SHA-256 `f2a8ebe740f576b69ebb6c4f7666fea3c62acfa7c1c0744b29d57ce05dc59302` (kept at `profile\out\libzbridge-instrumented.so`) |
| shipped (unchanged) | 3 306 992 B, SHA-256 `25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` |
| markers in the binary | `strings` finds `zb-profile: sampling armed, interval=%llu capacity=%llu -> %s` |
| APK | `profile\out\dh2-instrumented.apk`, 13 773 338 B, SHA-256 `8ee51db4c070b98d74c60e76138c7d07cdf1407c4d74d958efc20d3c205288d5` |
| signature | v3, certificate SHA-256 `daa24cd98557703003fb4518d1ed8504dee071f9620596592819176677772081` |

The APK was produced with the **sanctioned builder**
(`DH2Work-stage\compatibility\work\fold7-build\build_apk.py`, copied to
`profile\apkbuild\`), so its Java/DEX/manifest side is the same source state as the installed
app and only the one `.so` differs. Its signer certificate is byte-for-byte the same identity as
the installed app's, so the upgrade is `adb install -r` with no uninstall and no data loss
(verified with `apksigner verify --print-certs` against `phone\installed-after.apk`). **[M]**

## 6. Reproduce

```powershell
wsl -d Ubuntu -- bash /mnt/c/.../profile/00-setup.sh     # stage + patch
wsl -d Ubuntu -- bash /mnt/c/.../profile/01-build.sh     # build the instrumented .so
.\10-build-apk.ps1                                        # sign it into an APK
.\20-measure-device.ps1 -Wait                             # install, capture, analyse
python selftest-estimator.py --libc <sysroot>/system/lib/libc.so --libm <...>/libm.so   # estimator proof
```

## 7. The measurement itself — NOT TAKEN

**What is missing and why:** the phone (`SM-F966B`, serial `RFCY700ANZF`, Android 16, SDK 36)
enumerated on `adb` exactly once during this work, with `local.dh2.fold7` and
`local.dh2.fold7:guest` running, and then disappeared from `adb devices` and has not returned
across repeated `adb reconnect` / `reconnect offline` attempts (checked at the end of this work
and empty). Installing and driving the app requires the device; there is no emulator in this
environment, and `simpleperf`'s `unknown` blob exists precisely because there is no other way to
reach guest code. So the three numbers the brief asks for — the measured split, the top-40 list,
and the resulting VFP frame win — are **not available**, and reporting them from the sampler's
design or from the static census would be exactly the failure the brief warns against.

Two further gates were also still open when the device vanished, and they are why this is not
merely a scheduling problem:

1. **Install approval.** The brief requires asking the Lead before installing anything, and a
   manual tester was on the device (`local.dh2.fold7` and its `:guest` process were both running
   when it enumerated). The approval request went to the Lead; no answer had come back. The APK is
   built and signed so that the install is a single `adb install -r`, but it has not been run.
2. **A comparable scene.** The baseline (82.68 % `unknown`, 25.7 ms/frame) was taken over a
   specific 10 s render window. The capture should be driven through the same kind of scene for a
   comparable duration, which needs someone at the device.

The moment the device is back and those two gates are cleared, the capture is one command
(`profile\20-measure-device.ps1 -Wait`) and the analyser fills a table of this shape:

| region | instructions | share |
|---|---:|---:|
| engine (`libDungeonHunter2.so`, `libStormGLOFT.so`, `libnativeinterface.so`) | — | — |
| sysroot helper bodies (`__aeabi_*`, libm) | — | — |
| other guest (`linker`, libc non-FP, GL stubs) | — | — |
| unattributed | — | — |

### 7.1 What the measurement will and will not settle

It **will** settle the split of executed instructions between engine code and soft-float helper
bodies, and produce the by-function hot list from the same samples. It **will not** settle
whether a per-function port is *worth* doing, because execution frequency is necessary but not
sufficient: a function that is 30 % of executed instructions but mostly memcpy-shaped is a
different proposition from one that is arithmetic-bound. The split also does not by itself prove
the 87–91 % per-helper elimination figure, which comes from a bit-exact VFP replacement measured
elsewhere; it supplies the *share* that figure multiplies.

### 7.2 The measured accounting will be checked, not trusted

Three independent consistency checks are built in and will be reported:

1. `sum(recorded sample sizes)` versus the library's own `total_instructions` counter — their
   ratio should equal the mean block size, which is also observable directly;
2. instruction share versus wall-clock share per region — a large divergence would mean the
   sampled instructions-per-nanosecond is very different in the two regions, and the report will
   say which one is which rather than silently quoting the friendlier number;
3. the framing check: sampling stops when the ticket runs out, so the sampler cannot silently
   keep working during idle time.

## 8. Limitations, stated explicitly

1. **Not yet measured on the device.** See §7. Nothing in this document should be quoted as a
   device number.
2. **Function-level resolution only, and only for symbols.** A sample names a guest PC; the
   analyser maps it with the ELF symbol table, so code in a stripped region, a PLT veneer, or a
   gap between symbols lands in "unattributed" (the self-test put 0.13 % there). Static
   functions are visible because the engine is unstripped, but code that has no symbol at all is
   not attributed to a name.
3. **The `__aeabi_*`/libm boundary is a symbol-name boundary.** The "helper body" bucket is the
   set of sysroot `libc.so`/`libm.so` symbol ranges whose names match the soft-float and libm
   families. Soft-float code inlined into libc functions that are not named `__aeabi_*` counts
   as "other", and a VFP replacement would not touch it. The bucket is therefore a lower bound
   on helper-body instructions in that one specific sense.
4. **Instrumentation overhead.** Eleven extra host instructions per translated block, plus one
   host call per 3 000 guest instructions. At a plausible ~10 guest instructions per block and
   ~2.5 G blocks per device-second this is a few percent of host CPU. It does not change the
   *split* (the same blocks execute either way), but it does perturb absolute timing, so the
   measured `delta_ns` shares should be read as approximate and the instruction shares as the
   primary result. The overhead will be quantified by comparing `render returned frames=N
   elapsedMs=M` on the instrumented build against the same scene on the shipped build; both
   numbers are already produced by the guest's own event trace.
5. **Threads.** The histogram is over all guest instructions from all guest threads, and each
   record carries the `A32JitState` pointer that identifies a thread, so a per-thread breakdown
   is available; the *frame* split the brief asks about is the render thread's, and the report
   will say which threads contributed what rather than silently aggregating.
6. **Sampling is instruction-triggered, not timer-triggered.** A guest thread that spends a long
   time inside one block (a big `memcpy`-like loop) is sampled once per ticket crossing, not once
   per unit time, so the sample *count* is proportional to instructions rather than to time. That
   is the right weighting for an instruction-share question and the wrong one for a latency
   question; the `delta_ns` column is what covers latency, and it is subject to (4).
7. **Code that never runs is invisible, correctly.** This is the point of the exercise, but it
   means the histogram cannot distinguish "cold because unused" from "cold because reached only
   on level load" within a single session; that is what the session's scene and duration are for.
8. **The instrumented library is deliberately not shippable.** It is a measurement artefact. The
   shipped `libzbridge.so` is unchanged and still hashes `25e8da7e…682d24`; the instrumented
   source edits live only under `DH2Work-toolchain\profile\` and `/root/dh2prof`.
9. **The `maps` file is per session.** Guest library base addresses change between process
   starts, so a sample file is only analysable together with the `maps` file from the same run.
