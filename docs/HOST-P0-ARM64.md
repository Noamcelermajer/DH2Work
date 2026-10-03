# HOST-P0-ARM64.md — the P0 boot gate built and executed on an **arm64** Dynarmic backend

**Milestone status: the arm64 half of the gap is closed; the gate itself still FAILs on section
(b), and exactly why is now a measurement rather than a hypothesis.**

What changed relative to `HOST-P0-BOOT.md`:

* Patch `0001-A` — the arm64 `A32AddressSpace::GenerateIR` guard, previously *authored but never
  compiled* — is now **compiled for aarch64 with the Linux NDK clang and linked into the binary
  that runs**, and the generated code is shown beside the pristine build of the same file with the
  same flags (§2).
* The P0 harness now **runs on an arm64 Dynarmic backend** and the probe **returns through the
  address in `lr`** with a CRC32 that matches the offline oracle (§3).
* The oracle is no longer calibrated to the host. The ARM CRC32W recurrence is now checked against
  the **real AArch64 `crc32*` instructions** executed on the host, and against the published
  CRC-32/ISO-HDLC check value (§4).
* The old "wrong exit" is explained by its actual cause, which was neither `PopRSBHint` nor the
  return-stack buffer (§5).
* The remaining failure is section (b), the engine function, and it fails **identically on x86_64**.
  Its cause is proven from the engine's own file contents (§6).

Everything below is quoted from a run on this machine. Logs are in `host/p0arm/logs/`.

---

## 1. What builds and runs

Two builds now exist, and the difference between them matters:

| Build | Toolchain | Runs? |
|---|---|---|
| `host/p0` → Linux x86_64 (`scripts/build-wsl.sh`) | WSL gcc/clang | yes (the reference gate) |
| `host/p0arm` → **Android arm64-v8a, dynamic** (`scripts/build-arm64.sh`) | Linux NDK r29 clang | **no** — needs `/system/bin/linker64`; built by an earlier attempt, never executed |
| `host/p0arm` → **aarch64, static** (`scripts/build-arm64-linux.sh`) | Linux NDK r29 clang | **yes** — runs under this container's `qemu-aarch64` |

The Linux NDK is required and is the reason this is a WSL build at all: the Windows NDK ships only
`windows-x86_64` prebuilts. From the configure log of the running build:

```
-- Check for working CXX compiler: /root/dh2/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ - skipped
-- Target architecture: arm64
```

The static build, verbatim (`scripts/final-arm64-run.sh` → `logs/build-and-run-arm64.txt`):

```
== 0001-A guard in /root/p0arm/src/dynarmic:
   if (HasOptimization(OptimizationFlag::GetSetElimination) && !conf.check_halt_on_memory_access)) {

== configuring aarch64 (static, so it runs under this container's qemu-aarch64)
-- Generating done (0.1s)
-- Build files have been written to: /root/p0arm/build/android-arm64-static

== compiling p0boot (+ the arm64 Dynarmic backend, which is where 0001-A lives)
[1/2] Linking CXX executable p0crc32t32
[2/2] Linking CXX executable p0probediag

== artifact
   /root/p0arm/build/android-arm64-static/p0boot: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, for Android 28, built by NDK r29 (14206865), with debug_info, not stripped
     Class:                             ELF64
     Type:                              EXEC (Executable file)
     Machine:                           AArch64

== running the arm64 gate under qemu-aarch64 (qemu-aarch64 version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.2))
```

(`[1/2]`/`[2/2]` because this is an incremental rebuild: only the two executables that link
`libdynarmic.a` were left to relink. A cold build reports `[198/198]`.)

The link is genuinely the arm64 backend and **not** the x64 one. Counted in the linked binary, and
the guard site inside it (`llvm-objdump` of `p0boot` itself, not of the object file):

```
== AArch64 backend symbols in the linked p0boot: 1281
== x86_64 backend symbols in the linked p0boot:  0
00000000002feb14 T Dynarmic::Backend::Arm64::A32AddressSpace::EmitPrelude()
00000000003046bc T Dynarmic::Backend::Arm64::A32AddressSpace::GetEmitConfig()

== guard site inside the LINKED p0boot:
  303f60: 36180128     	tbz	w8, #0x3, 0x303f84   ; !GetSetElimination -> skip
  303f64: 39515289     	ldrb	w9, [x20, #0x454]    ; conf.check_halt_on_memory_access
  303f68: 370000e9     	tbnz	w9, #0x0, 0x303f84   ; set -> skip            <-- 0001-A
  303f84: 36200108     	tbz	w8, #0x4, 0x303fa4   ; next optimization flag
```

Two CMake defects had to be fixed for the arm64 build to work at all, both in
`host/p0arm/CMakeLists.txt`:

1. **Boost reached only `p0boot`.** `ir/terminal.h` includes `boost/variant.hpp`; on a native build
   a system Boost covers it, but a cross-compilation has none, so the other executables failed with
   `fatal error: 'boost/variant.hpp' file not found` (observed on `p0probediag` and `p0crc32t32`;
   `p0decodecheck`, `p0crc32`, `p0crcenc` and `p0regions` link `dynarmic` the same way). A
   `dh2_p0_boost()` helper now applies the pinned header tree to every target.
2. **`-static` and `ANDROID_STL=c++_static`** are what make the binary runnable here. Nothing else
   in the build changed.

---

## 2. `0001-A` is compiled, in aarch64, into the object the gate links

"Present in the source" is not the claim; the claim is that the guard is in the machine code the
gate executes. The build cannot notice either way — with or without the guard the arm64 backend
compiles, links and passes the same tests — so the evidence has to be the object code.

`scripts/prove-0001a.sh` takes the exact compile command ninja used for
`backend/arm64/a32_address_space.cpp` in the Android arm64 build (real flags, real sysroot, real
PCH), runs it again with the **pinned pristine** copy of the same file, and compares
`A32AddressSpace::GenerateIR`:

```
patched : 86 instructions
pristine: 84 instructions
```

Same function, same compiler, same flags. The two extra instructions are the guard:

```
patched                                    pristine
b0: ldr   w8, [x20, #0x314]   ; conf.optimizations
b4: tbz   w8, #0x3, 0xd8      ; if !GetSetElimination skip
b8: ldrb  w9, [x20, #0x454]   ; conf.check_halt_on_memory_access
bc: tbnz  w9, #0x0, 0xd8      ; if precise faults, skip   <-- 0001-A
c0..d0:    call A32GetSetElimination
d8: tbz   w8, #0x4, 0xf8      ; next optimization flag
                                           b0: ldr   w8, [x20, #0x314]
                                           b4: tbz   w8, #0x3, 0xd0
                                           b8..c8: call A32GetSetElimination
                                           cc: ldr   w8, [x20, #0x314]
                                           d0: tbz   w8, #0x4, 0xf0
```

`tbnz w9, #0x0, 0xd8` jumps **past** the `A32GetSetElimination` call when
`check_halt_on_memory_access` is set: that is `&& !conf.check_halt_on_memory_access`, compiled. The
pristine file's own source confirms what is missing there (`a32_address_space.cpp:169`):

```
169:    if (conf.HasOptimization(OptimizationFlag::GetSetElimination)) {
170:        Optimization::A32GetSetElimination(ir_block, {.convert_nzc_to_nz = true});
```

**What this does not prove.** The guard's *behaviour* — that a precise memory abort now observes
committed guest registers — is still not tested. That test is `DYNARMIC-INDEPENDENCE.md` §6(2), and
it is listed as an open blocker in §7.

---

## 3. The gate on arm64: the probe's result

Run: `host/p0arm/scripts/build-arm64-linux.sh` (build) then
`host/p0arm/scripts/capture-arm64.sh p0boot-arm64.txt p0boot`. Full log:
`host/p0arm/logs/p0boot-arm64.txt`.

```
== (a) hand-written ARM32/Thumb-2 probe executed by Dynarmic ==
  guest code   0x10020000  28 bytes (7 Thumb-2 instructions), R+X
  guest data   0x10024000  16384 bytes, R+W
  return guard 0x10010000  unmapped (readable=0, faults=1)
  [PASS] the LR sentinel page is not guest memory (so fetching it faults)

  call(r0=0, r1=0x10024000, r2=16384)  [full buffer]
    exit      : returned-to-sentinel  (ticks=20488, pc=0x10010000)
    detail    : NoExecuteFault at pc=0x10010000 (sentinel=0x10010000, lr=0x10010000, sp=0xFEF00000, cpsr=0x60000010, r0=0x489BB175)
    r0        : 0x489BB175
    oracle    : 0x489BB175   (ARM CRC32W recurrence, see oracle/gen_golden.py)
  [PASS] guest returned through the sentinel
  [PASS] guest CRC32 equals the offline oracle
    run-vs-step: run=returned-to-sentinel step=returned-to-sentinel step_pc=0x10010000 step_r0=0x489BB175
  [PASS] the same probe returns through the sentinel when single-stepped

  call(r0=0, r1=0x10024000, r2=16)  [first 16 bytes]
    exit      : returned-to-sentinel  (ticks=28, pc=0x10010000)
    r0        : 0x742973B7
    oracle    : 0x742973B7
  [PASS] guest returned through the sentinel
  [PASS] guest CRC32 equals the offline oracle
```

`Jit::Run()` leaves the probe at the address in `lr`: `lr = 0x10010000`, the halt PC is
`0x10010000`, and the callback classifies it `returned-to-sentinel` rather than `no-execute-fault`.
The instruction-fetch trace behind that single window (`p0probediag`, arm64) shows the guest
walking its own 28 bytes and then fetching the sentinel — no drift, no second block:

```
--- Run(), all optimizations on (the gate's default core)
   exit=returned-to-sentinel pc=0x10010000 ticks=28 r0=0x742973B7 (oracle 0x742973B7)  windows=1
   instruction fetches (16):
     0x10020000 0x10020000 0x10020004 0x10020004 0x10020008 0x10020008 0x1002000C 0x1002000C
     0x10020010 0x10020010 0x10020014 0x10020008 0x10020008 0x10020014 0x10020018 0x10010000
```

And all thirteen lengths agree between `Run()` and `Step()`, on arm64
(`host/p0arm/logs/p0probediag-arm64.txt`):

```
-- CRC32 vs the ARM CRC32W recurrence, by length (Run and Step)
      bytes       oracle          run         step    ticks
          4 0xADDB1348 0xADDB1348  0xADDB1348        13 returned-to-sentinel returned-to-sentinel
         16 0x742973B7 0x742973B7  0x742973B7        28 returned-to-sentinel returned-to-sentinel
       4096 0xF3B00345 0xF3B00345  0xF3B00345      5128 returned-to-sentinel returned-to-sentinel
      16384 0x489BB175 0x489BB175  0x489BB175     20488 returned-to-sentinel returned-to-sentinel
```

---

## 4. Does the oracle agree? Yes — and it is no longer fitted to the host

`oracle/gen_golden.py` was written from the ARM ARM, but its self-checks asserted the two numbers
the host had already produced, which makes the gate's comparison circular. It is no longer the only
witness. `src/crc32_ground_truth.cpp` (target `p0crc32truth`, aarch64 only) executes the **real
AArch64 CRC32 instructions** and compares them with a byte-serial recurrence written out again in
C++:

```
-- crc32w: instruction vs recurrence
  crc32w(0x00000000, 0x5CDB3043) = 0xADDB1348   recurrence 0xADDB1348   ok
  crc32w(0x989238E2, 0x5CDB3043) = 0x4676DD3D   recurrence 0x4676DD3D   ok
  crc32w(0xFFFFFFFF, 0xFFFFFFFF) = 0x00000000   recurrence 0x00000000   ok
  ... 48 instruction-vs-recurrence comparisons, 0 mismatches ...

-- the standard check value, through the instruction
  crc32b loop over "123456789", seed 0xFFFFFFFF, final xor = 0xCBF43926 (CRC-32/ISO-HDLC check value 0xCBF43926)
  [PASS] crc32b composed with the standard complement gives CRC-32/ISO-HDLC

-- the gate's two comparisons
  crc32w stream, first 16 bytes : 0x742973B7   (gen_golden.py literal 0x742973B7)
  crc32w stream, full 16384     : 0x489BB175   (gen_golden.py literal 0x489BB175)
  [PASS] first-16 value from the instruction equals the oracle
  [PASS] full-buffer value from the instruction equals the oracle

GROUND TRUTH: PASS (0 failures)
```

Three arguments, one of them external to this project:

1. the AArch64 `crc32w`/`crc32h`/`crc32b` instructions agree with the ARM ARM recurrence on every
   vector tried (48 comparisons);
2. the *published* CRC-32/ISO-HDLC check value `0xCBF43926` for `"123456789"` is reproduced by
   composing `crc32b` with the standard complement — that ties the un-complemented instruction to
   a number published outside this project;
3. ARM CRC32W is defined with **no inversion**: composing `crc32w` with `^0xFFFFFFFF` in and out is
   what produces the standard CRC-32 that `zlib.crc32` returns. `gen_golden.py` had it right;
   `HOST-P0-BOOT.md` §4's `oracle : 0x989238E2` was the zlib value and was the wrong comparison.

### The "CRC mismatch" was two bugs in the probes, not a backend defect

* `src/crc32_t32_probe.cpp` complemented the state in and out **and** XORed the seed with the whole
  data word. It also reused one `Jit` across four cases whose code bytes differ at the same PC, so
  cases 2–4 re-ran case 1's compiled block. Together these turned four correct results into four
  `MISMATCH` lines, including `crc32h`/`crc32b` reporting `0x00000000` for registers the running
  block never wrote. Fixed (`host/p0arm/logs/p0crc32t32-arm64.txt`):

  ```
    reference self-check: arm_crc32w(0, 0x5CDB3043) = 0xADDB1348 (want 0xADDB1348)
    crc32w r2, r0, r1   r2= 0xADDB1348  want 0xADDB1348  ok
    crc32w r2, r0, r1   r2= 0x4676DD3D  want 0x4676DD3D  ok
    crc32h r3, r2, r1   r3= 0x98633961  want 0x98633961  ok
    crc32b r5, r4, r3   r5= 0xEFD5102A  want 0xEFD5102A  ok
    0 mismatches
  T32 CRC32: PASS
  ```

  These four values are independently confirmed by the ground-truth program above
  (`crc32b(0,0x5CDB3043)=0xEFD5102A`, `crc32h(0x12345678,0x5CDB3043)=0x98633961`).
* `src/probe_diag.cpp` carried the same wrong recurrence, which is why its length sweep printed a
  `*` beside every correct result. Corrected; it now also carries the external anchor
  (`CRC-32/ISO-HDLC via CRC32B on "123456789": 0xCBF43926 ... OK`).

So: **there is no evidence of a CRC32 emission defect in either backend.** What looked like one was
a reference implementation that computed a different function.

---

## 5. The old wrong exit, and what it actually was

`HOST-P0-BOOT.md` §4 recorded `NoExecuteFault at pc=0x10034000` with `lr=0x10028000` and blamed the
epilogue's `PopRSBHint` terminal. That was wrong, and the layout comment now in
`src/guest_memory.hpp` records why:

> The sentinel is *not* interior to any other region, and that is load-bearing. It was previously
> `0x10028000` while the data buffer was mapped as `0x10024000 + 0x10000` (R+W), so the sentinel
> page was inside the data mapping. `map_anon_at` is deliberately idempotent … so the R-only
> sentinel mapping silently did nothing, the page stayed R+W, and `MemoryReadCode` returned
> `0x00000000` for it instead of faulting. The guest then executed the data region as a stream of
> Thumb NOPs until it ran off the end of that mapping at `0x10034000`, which is exactly the
> "NoExecuteFault at pc=0x10034000" reported in `docs/HOST-P0-BOOT.md` section 4.

The arithmetic closes: the old data mapping ended at `0x10034000`, and the reported fault address
*is* `0x10034000`. The guest never left the probe at a wrong address — the **host's exit condition
was not an exit condition**, and the run drained its tick budget walking off the end of a buffer.
`0x10034000 − 0x10028000 = 0xC000`, i.e. 12,288 four-byte steps, against the reported
`ticks=12316`. The `lr`/`rsb_ptr`/`PopRSBHint` theory in §4 was a red herring, and disabling
`ReturnStackBuffer`/`BlockLinking` changed nothing for the same reason.

Two other real defects were found and fixed in `host/p0arm/src` while closing this:

* **`ExceptionRaised` did not stop `Run()`.** With no `HaltExecution`, the JIT keeps translating new
  blocks after the fault; the sentinel fault was overwritten by later ones. With
  `HaltExecution(UserDefined1)` the exit is classified immediately.
* **`map_anon_at` returned 0 for both "mapped at address 0" and "failed"**, so
  `if (map_anon_at(...) == 0) fail` reported a *successful* load of the engine's first `PT_LOAD`
  (`p_vaddr == 0`) as a failure. The engine never loaded, on either host. `map_anon_at` now reports
  success through a `bool*` and `guest_module.cpp`/`main.cpp` use it. With the fix, section (b)
  actually runs for the first time:

  ```
  == (b) real guest function from the pristine engine ==
    PT_LOAD segs : 2, 10706944 bytes mapped at link-time addresses
    symbols read : 36752
    symbol       : _ZN6glitch4core8vector3dIfE9normalizeEv = 0x0035E8E0 (st_value, Thumb bit clear)
    [PASS] symbol table points at the oracle's address
    [PASS] st_value says ARM state (bit 0 clear)
  ```

---

## 6. What the gate still fails on, and why — measured

The gate exits `P0 GATE: FAIL (9 checks failed)` on **both** hosts, and the failure text is
identical on both. All nine are in section (b); sections (a) and the census pass.
`host/p0arm/logs/p0boot-x86_64.txt:58` and `host/p0arm/logs/p0boot-arm64.txt`:

```
  normalize(3,4,0)  call(r0=0x70000000)  exit=undefined-instruction  ticks=15
    (3.0,4.0,0.0) -> (3.000000, 4.000000, 0.000000)   oracle (0.600000, 0.800000, 0.000000)
    detail    : UndefinedInstruction at pc=0x00000000 (sentinel=0x10010000, lr=0x00994AA0, sp=0xFEEFFFE4, cpsr=0x00000010, r0=0x40400000)
```

`lr=0x00994AA0` is the fingerprint. From the engine's own `PT_DYNAMIC` and file bytes:

```
DT_PLTGOT   0x994a98            DT_RELSZ   432896 (54,463 REL entries)
DT_JMPREL   0x30d27c  (351)     DT_RELCOUNT 49132

GOT (file contents) at 0x994a98:
  00993a98: b049 9900 0000 0000 0000 0000 74dd 3000
            GOT[0]    GOT[1]    GOT[2]    GOT[3]
            0x9949b0  0        0         0x0030dd74  (= .plt[0])
```

`0x994AA0 = DT_PLTGOT + 8 = GOT[2]`, and `GOT[2]` is **zero**. The chain is exact: `bl <plt stub>`
→ the stub's GOT slot holds `0x0030dd74` (the lazy-binding seed, the address of `.plt[0]`) → `.plt[0]`
executes `ldr pc, [GOT+8]!` → `pc = 0`, `lr = GOT+8 = 0x994AA0` → the guest executes the ELF header
at address 0 and faults. P0 processes **zero** of the 54,463 relocations, so the PLT resolver slot
that only the dynamic linker fills is empty. This is the "no relocation processing" limitation
stated in `TEST-ENVIRONMENT-POLICY.md`, now localised to a byte.

The four imports `normalize` needs (from `.rel.plt`, indices 339/301/43/77) are
`__aeabi_fmul`, `__aeabi_fadd`, `__aeabi_fcmpeq`, `sqrtf` — so making (b) pass needs both the
relocations and a runtime for soft-float libcalls plus `sqrtf`. `host/loader` (59,623 relocations)
and `Dh2UserCallbacks` are the documented seam; that is a new milestone, not this gap, and it was
deliberately not attempted here.

---

## 7. Ordered remaining blockers

1. **Relocation processing, then a libcall runtime.** Section (b) needs `DT_REL`/`DT_JMPREL` applied
   (54,463 entries; `GOT[1]`/`GOT[2]` and every `R_ARM_JUMP_SLOT`) and host implementations of
   `__aeabi_fadd`/`__aeabi_fmul`/`__aeabi_fcmpeq`/`sqrtf`. Until then the gate cannot be green, on
   either architecture. Seam: `host/loader` + `Dh2UserCallbacks`.
2. **The behavioural test for 0001-A** (`DYNARMIC-INDEPENDENCE.md` §6(2)): plant a guest memory
   abort mid-block after a guest register write, with `check_halt_on_memory_access = true`, and
   assert the register state the callback observes. Today 0001-A is proven *compiled* (§2), not
   proven *effective*.
3. **`arch_version` does not gate A32 decoding** (recorded by the gate's own control output), so
   `Dynarmic::A32::ArchVersion::v7` cannot serve as a negative control for patch 0001-B. The
   discriminating control remains a build-level one: on the pinned pristine tree `thumb32.inc` has
   no CRC32 row and `0xFAC0` falls through `DecodeThumb32` to `thumb32_UDF`.
4. Everything P0 has never had: guest dynamic linker, syscall layer, TLS, more than one thread,
   guest signals. Unchanged by this work.

---

## 8. What is *not* verified

* **The arm64 Android emulator was not used.** Headless boot of an arm64 system image on x86_64 was
  judged not worth the time: Android's arm64 emulator on an x86_64 host is also QEMU TCG, i.e. the
  same implementation this container's `qemu-aarch64` already provides. No phone was touched.
* **`qemu-aarch64` is not hardware.** The AArch64 instructions Dynarmic emits (including `crc32w`)
  are executed by QEMU's TCG, not by silicon. A Cortex-A device could in principle differ; the
  architecture, not the implementation, is what was tested.
* **The dynamic, bionic-linked arm64 build (`scripts/build-arm64.sh`) was not executed** — it needs
  `/system/bin/linker64`. It builds (198/198), and that build is the one that would ship.
* **The engine function never ran to completion**, so no engine-vs-Unicorn comparison was made and
  `dh2_oracle.py` was not exercised against this host by this work. The oracle comparison in §4 is
  the CRC32W one from `oracle/gen_golden.py`.
* **`0001-B`'s decoder rows are still verified at the frontend only** (`p0decodecheck`, 20/20
  encodings, unchanged and re-run by this build) plus the T32 CRC32 semantics in §4. No guest
  instruction in the shipped images was executed through them end-to-end.
* **`check_halt_on_memory_access`'s effect is unmeasured** (§7.2). The arm64 build sets it — the
  guard in §2 is compiled with it — but P0 never takes a mid-block precise abort, so the code path
  the guard protects was not entered.
* **The `host/p0arm/logs/` files are the raw stdout of one run each**; nothing was re-run to
  variance, and no statistical claim is made.

---

## 9. Reproducing this

All commands are run inside WSL2 (Ubuntu, `qemu-user-binfmt` registered). Inputs are read from the
Windows tree over `/mnt/c`; build output goes under `/root/p0arm`.

```bash
# 0. everything in one pass: assert 0001-A, build aarch64, run the gate under qemu-aarch64
bash .../host/p0arm/scripts/final-arm64-run.sh      # -> host/p0arm/logs/build-and-run-arm64.txt

# 1. build the aarch64 gate and run it under qemu-aarch64
bash /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/p0arm/scripts/build-arm64-linux.sh

# 2. the same, with the output captured into host/p0arm/logs/
bash .../host/p0arm/scripts/capture-arm64.sh p0boot-arm64.txt p0boot

# 3. probe diagnosis on arm64: Run() vs Step(), all 13 lengths, fetch trace
bash .../host/p0arm/scripts/capture-arm64.sh p0probediag-arm64.txt p0probediag

# 4. the real AArch64 crc32* instructions vs the ARM ARM recurrence (aarch64 only)
bash .../host/p0arm/scripts/capture-arm64.sh crc32truth-arm64.txt p0crc32truth

# 5. 0001-A compiled for aarch64, patched vs pristine
bash .../host/p0arm/scripts/prove-0001a.sh

# 6. the reference x86_64 gate, for the (b) comparison. Run against the same source tree as
#    the arm64 build so the two logs differ only by host architecture:
#      cmake -S .../host/p0arm -B /root/p0/build/p0diag <same -D flags as diag-wsl.sh>
#      cmake --build /root/p0/build/p0diag --target p0boot -j 20
#      /root/p0/build/p0diag/p0boot --engine=... > host/p0arm/logs/p0boot-x86_64.txt 2>&1
#    (scripts/build-wsl.sh is the other, longer route: it re-stages the tree under /root/p0.)
```

Staged patched tree: `/root/p0arm/src/dynarmic` (pin `86458a0b`, patch `0001` applied, 5 files,
+265/−1). Build directory: `/root/p0arm/build/android-arm64-static`.
