# HOST-P0-BOOT.md — the P0 boot gate: what was built, what was measured, where it stopped

**Milestone status: NOT reached.** The gate does not pass. This document says exactly what *was*
measured, what blocked the milestone, and what the next step is. The headline positive result is
that patch 0001 is verified at the frontend level and that real guest ARM32 code from the pristine
engine and the shipped sysroot is loaded and executed by Dynarmic through this host's own memory
layer. The headline negative result is that the hand-written probe does not return to its LR
sentinel, so the "executes and the result matches the oracle" claim cannot be made.

All numbers below were produced on this machine (WSL2 Ubuntu, gcc 15.2, 20 cores) by
`host/p0/scripts/build-wsl.sh`. Nothing was run on a device and no device attestation is implied.

---

## 1. What exists

| Path | What it is |
|---|---|
| `host/p0/patches/0001-armv8-acquire-release-crc32-and-precise-faults.patch` | our Dynarmic patch: 5 files, +254/−1 |
| `host/p0/scripts/apply-dynarmic-patches.sh` | applies it to a staged tree, with revision and drift checks |
| `host/p0/scripts/build-wsl.sh` | stages the pin, patches, self-checks, builds, runs |
| `host/p0/scripts/check-decoder-rows.py` | every Thumb-2 pattern is 32 bits; every added name is declared and defined |
| `host/p0/scripts/check-decoder-arity.py` | every pattern's field count matches its translator's arity |
| `host/p0/scripts/check-decoder-conflicts.py` | no added row collides with an existing one |
| `host/p0/scripts/derive-t32-patterns.py`, `try-crc-rows.py` | derive the patterns from assembler bytes; test candidates against the table |
| `host/p0/scripts/verify-encodings.sh` | clang/llvm-objdump round-trip of all added encodings |
| `host/p0/scripts/run-oracle.sh` | runs the Unicorn oracle (`DH2-recon/tools/dh2_oracle.py`) |
| `host/p0/src/` | the host: guest memory adapter, minimal ELF32 loader, Dynarmic A32 core, census, gate |
| `host/p0/scratch/` | the diagnostic tools used below; not part of the deliverable |

Build recipe: `bash host/p0/scripts/build-wsl.sh [--clean]`. It stages a copy of
`DH2Work-toolchain/dynarmic` (pin `86458a0b`), applies patch 0001, runs the three self-checks,
builds a patched Dynarmic in-tree plus `p0boot` and `p0decodecheck`, then runs both.

---

## 2. Patch 0001 — authored here, verified here

Pinned tree `86458a0bd369d63ba4c2ef812cacbb6c9080c065`, `src/dynarmic/frontend/A32/decoder/thumb32.inc`
sha256 `3be66be8c6953d32c914410b3d734c16624e147d2de45e4b9916a9525967192e` (LF form). The patch
applies with `git apply --check` and produces 5 files, +254/−1. No ZettaBridge text is used.

**Change A** — `backend/arm64/a32_address_space.cpp`: `A32GetSetElimination` is skipped when
`check_halt_on_memory_access` is set, mirroring the guard the other three backends already carry.
This is not compiled on x86_64, so the gate below neither exercises nor can regress it; it is
recorded as **authored but unverified** (verification belongs with an arm64 build).

**Change B** — the T32 ARMv8 acquire/release family and `CRC32`/`CRC32C`: 14 instructions, decoder
rows plus translator bodies. Verified two ways:

* `scripts/verify-encodings.sh`: `clang --target=thumbv8a` assembles 20 source lines and
  `llvm-objdump` round-trips them against a captured expected file. OK.
* `p0decodecheck`: feeds those same 20 assembler encodings to the patched frontend and asserts the
  decoder returns the intended matcher **with the intended mask**:

```
[PASS] ldab r1, [r3]   (0xE8D31F8F) -> LDAB
...
[PASS] stlexd r4, r1, r2, [r3]  (0xE8C312F4) -> STLEXD
[PASS] crc32b r1, r1, r1  (0xFAC1F181) -> CRC32  mask=0xFFF0F0C0 expect=0xFAC0F080
[PASS] crc32h r1, r1, r1  (0xFAC1F191) -> CRC32  mask=0xFFF0F0C0 expect=0xFAC0F080
[PASS] crc32w r1, r1, r1  (0xFAC1F1A1) -> CRC32  mask=0xFFF0F0C0 expect=0xFAC0F080
[PASS] crc32cw r3, r2, r0 (0xFAD2F3A0) -> CRC32C mask=0xFFF0F0C0 expect=0xFAD0F080
[PASS] the probe's CRC32 at byte offset 16 decodes as CRC32 (0xFAC0F0A3)
DECODE CHECK: PASS (20 encodings)
```

### Three real bugs the self-checks caught, and why the checks exist

Writing these rows by hand was wrong three times, and the compiler's symptom for all three is
`expression '<throw-expression>' is not a constant expression` inside `decoder_detail.h`, naming no
row. In order of discovery:

1. **A 31-character pattern** (`STLEXD`). The decoder expands the bitstring at compile time, so a
   short pattern shifts every field after the mistake. `check-decoder-rows.py` checks widths.
2. **Field-count mismatch.** This decoder opens a new translator argument at *every change of
   character* in the pattern, so `...dddd1000mmmm` is a different number of arguments than
   `...ddddzzzzmmmm`. `check-decoder-arity.py` computes the count the decoder will derive and
   compares it with the translator's parameter count.
3. **Encoding collisions, twice.** The first CRC32 row (`...dddd00zzmmmm`, the A32 field order) is
   *the same row* as `SSUB8`, and `...ddddzz10mmmm` is the same row as `SHSUB8` while only ever
   matching `CRC32W`. `check-decoder-conflicts.py` reports rows that agree on every bit both
   constrain. `scripts/try-crc-rows.py` enumerates candidates against the pinned table and the
   assembler bytes; `...dddd10zzmmmm` is the only candidate that matches all three sizes and
   collides with nothing.

The conflict checker itself had a bug worth recording: it first compared
`mask_a & mask_b & (value_a ^ value_b)`, which reports a collision for every pair that merely
leaves the same bits wildcard. The correct comparison is between the two patterns'
constrained bits: `(mask_a & value_a) ^ (mask_b & value_b)`.

---

## 3. The host, and what it did execute

`host/p0` reuses `host/mem` as the guest address space (identity mapping, one host reservation) and
adds an adapter, a minimal ELF32 loader, a Dynarmic A32 core, and the census. It does **not** use
`host/loader`: that component owns its own reservation and its interface is in flux, so P0 places
segments itself at the addresses the images were linked for, with no relocation (the engine is
`ET_DYN` with a zero base, so guest address == link-time address, and the functions P0 enters are
self-contained).

Measured, from the gate's own output:

* Guest window reserved: 4 GiB, host page 4096, guest page 4096.
* The pristine engine is mapped at its link-time addresses; its first `PT_LOAD` is
  `[0x00000000, 0x00956000)`, which is why the probe's own pages live at `0x10020000` rather than
  `0x00020000` (the earlier layout was swallowed by that segment, and the engine load failed).
* The sysroot loads and is scanned. This is the strongest evidence that patch 0001-B is needed by
  the shipped guest, and that this build supplies it:

```
system/bin/linker  : 1390028 executable bytes mapped
                   : CRC32/CRC32C candidates 10, decodable by this host 10
                   : acquire/release candidates 1334, decodable 23
system/lib/libc.so : 828273 executable bytes mapped
                   : CRC32/CRC32C candidates 0, decodable by this host 0
                   : acquire/release candidates 677, decodable 39
engine             : CRC32/CRC32C candidates 0, acquire/release candidates 0
```

The engine's zero is the expected result (`DYNARMIC-INDEPENDENCE.md` §5.1 measures the same thing
by a different method). The acquire/release *candidate* counts are an upper bound from a coarse
mask; the CRC32 counts are from the exact mask, so "10 candidates, 10 decodable" is the number to
trust. This is a smaller count than the documentation's 943 for the linker because the scan is
limited to each `PT_LOAD`'s `memsz` window and uses one mask, not a state-matched disassembly.

* **Real guest ARM32 executes under Dynarmic through this host.** A separate single-step harness
  (`scratch/trace_probe.cpp`) walks the probe and shows the loop running four iterations and
  `bx lr` reaching the sentinel at `0x10028000`:

```
 0: pc=0x10020000 insn=0xB430          (push {r4,r5})
 3: pc=0x10020008 insn=0x42AC          (cmp r4, r5)
 6: pc=0x10020010 insn=0xFAC0F0A3      (crc32w r0, r0, r3)
23: pc=0x10020008  ...                (loop exits when r4 == 0x10024010)
25: pc=0x10020016 insn=0x4770BC30      (pop {r4,r5})
26: pc=0x10020018 insn=0xBF004770      (bx lr) -> 0x10028000
reached the sentinel
```

* And the IR the JIT builds for `crc32w` is correct after the arity fix:

```
%8 = CRC32ISO32 %6, %7
SetRegister r0, %8
```

---

## 4. Where it stopped, and the exact blocker

**The gate fails.** `p0boot`'s probe run does not return through the sentinel and does not produce
the oracle's CRC:

```
call(r0=0, r1=0x10024000, r2=16)  [first 16 bytes]
  exit   : no-execute-fault  (ticks=12316, pc=0x10034004)
  detail : NoExecuteFault at pc=0x10034000 (sentinel=0x10028000, lr=0x10028000,
           sp=0xFEF00000, cpsr=0x60000010, r0=0x742973B7)
  r0     : 0x742973B7
  oracle : 0x989238E2
[FAIL] guest returned through the sentinel
[FAIL] guest CRC32 equals the offline oracle
```

`lr` is the sentinel and `0x10028000` is mapped read-only, so the fault address should have been
`0x10028000`. It is not, and `r4`/`r5` read as zero (stale) at the fault, while the same values are
correct in the single-step trace. Two facts narrow it:

* The probe's epilogue IR terminal is `PopRSBHint{}` (the return-stack-buffer prediction). With
  `OptimizationFlag::ReturnStackBuffer` or `BlockLinking` removed the fault address is unchanged, so
  the optimisation is not the whole story.
* The single-step harness reaches the sentinel; `p0boot`'s own path does not. That harness differs
  from `HostCpu` in that it calls `Jit::Step()` with `GetTicksRemaining()` returning 1, i.e. it never
  runs a multi-block `Run()` window.

**What blocked the milestone:** I could not get `Jit::Run()` to leave the probe's epilogue at the
address in `lr`, and I could not get the guest CRC32 to equal the oracle even though the IR shows
`CRC32ISO32` with the right operands. Both need Dynarmic's block-exit and CRC32 emission to be
traced further than I got. Because the probe does not return, I also cannot yet say whether the
CRC32 mismatch is an arithmetic bug or a consequence of the wrong exit; the loop and the sentinel
were never both satisfied in one run.

**Not verified at all:** patch 0001-A (the arm64 guard) is uncompiled on this host. There is no
arm64 build, no Android build, no guest dynamic linker, no syscall layer, no TLS, and no relocation
(beyond mapping at the linked addresses).

---

## 5. What to do next

1. Bisect `Run()` vs `Step()` on the probe epilogue with `Dynarmic`'s verbose IR and a
   PC-written-every-instruction hook. The `PopRSBHint` terminal and the A32 `rsb_ptr` /
   `upper_location_descriptor` interaction is the specific place to look.
2. Independently, check the x64 CRC32 emission for `CRC32ISO32` (the ISO-HDLC 32-bit variant): the
   probe's observed `0x742973B7` for the first 16 bytes matches none of `zlib.crc32`, the
   non-reflected CRC-32, or `CRC32C`, nor any word/byte-reversal of them
   (`scratch/search_crc.py`). That is consistent with a mis-emitted instruction, not with a
   byte-order convention.
3. Then re-check the oracle comparison, and only then re-state the milestone.
4. Build for arm64 and verify patch 0001-A with the precise-abort test described in
   `DYNARMIC-INDEPENDENCE.md` §6(2).

---

## 6. Biggest risk

That the milestone is being pursued on the x86_64 backend while the product target is arm64, and the
one Dynarmic change that the product *must* have (0001-A) is in the backend x86_64 never compiles —
so a green P0 on this host would still say nothing about the arm64 code path, and the arm64 path is
where the guest actually has to run. A secondary risk is the opposite direction: a subtle x64
back-end misbehaviour (as the epilogue and CRC32 results above hint) could be mistaken for a host
bug and drive work into `host/p0` instead of into the backend.
