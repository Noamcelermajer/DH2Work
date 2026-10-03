# HOST-NATIVE-HOOK.md — running one guest ARM32 function as native ARM64

**Scope.** Dungeon Hunter 2 HD v1.0.2 (`armeabi-v7a`) on the arm64-only Galaxy Z Fold7, executed by
the shrunk ZettaBridge host's Dynarmic AArch32→AArch64 JIT. The device measurement that motivates
this work is that **82.7 % of frame CPU is JIT-translated guest code** (15.1 % host, 0.16 % GL
driver, 25.7 ms single-threaded per frame), so moving a hot guest function out of the JIT is the
only structural fix for that share.

**What was built.** A translation-time hook, installed in the fork at
`DH2Work-toolchain/host/shrink/ZettaBridge`, that claims one guest function's entry PC during
translation and emits an `IR::Opcode::CallHostFunction` in place of its body, plus a hand-written
AArch64 implementation of
`glitch::core::quaternion::operator*(quaternion const&) const` (`0x0060dd34` in the file image)
and the softfp → AAPCS64 shim it needs.

**Verdict up front.**

| question | answer | evidence |
| --- | --- | --- |
| Is the hook installed in the fork and does it build? | **Yes** — `libzbridge.so` 3 015 352 B, SHA-256 `5c769c9d…57dd`, still exactly 13 exported `STT_FUNC` | §6 |
| Does the emitted block contain what the mechanism claims? | **Yes, measured** through Dynarmic's own translator and `DumpBlock()`: one `CallHostFunction` of the zero-extended `r0-r2`, then `A32BXWritePC(lr)` + `PopRSBHint`, plus a terminal and **zero** `A32SetRegister` | §5, quoted dump |
| Is the native body bit-exact with the ARM32 the engine executes? | **Yes** — 2 308 cases, **0 differing lanes**, ARM32 shipped body (qemu-arm) vs native AArch64 body (qemu-aarch64) | §4.1 |
| Does the hooked path execute on the device? | **NOT VERIFIED.** The library was installed and the game booted and rendered with it, but every run window in which I measured was killed by another agent's install or by the app losing focus, and the hooked PC had not been translated before that happened. See §7.1. | §7 |

---

## 1. The mechanism, and why this one

`Dynarmic::A32::TranslateCallbacks::PreCodeReadHook(is_thumb, pc, A32::IREmitter&)` is called
before each instruction is **read**, and it hands over a live IR emitter. `GuestThread` derives
from `A32::UserCallbacks`, which derives from `TranslateCallbacks`, and the host already hands that
object to Dynarmic (`cfg.callbacks = this`, `core/src/guest_thread.cpp`), so the hook is an
override plus one call:

```cpp
bool GuestThread::PreCodeReadHook(bool is_thumb, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter& ir) {
    return !native_hook_emit(*this, is_thumb, pc, ir);
}
```

Three facts, each checked in this tree rather than assumed:

* **No Dynarmic patch.** `CallHostFunction` already exists (`ir/ir_emitter.h:397-400`), the AArch64
  backend already emits it as `mov x16, <imm>` + `blr x16` (`backend/arm64/emit_arm64.cpp:39-45`),
  and `Inst::MayHaveSideEffects()` lists it (`ir/microinstruction.cpp:546`), so it survives IR
  dead-code elimination. `--gc-sections` keeps the host entry because the target table takes its
  address.
* **Returning `false` ends the block.** `translate_arm.cpp:33-36` does
  `if (!tcb->PreCodeReadHook(...)) { should_continue = false; break; }`; the comment on
  `config.h:70` saying the opposite is wrong. Ending the block **mandates** setting the terminal,
  or `ASSERT_MSG(block.HasTerminal())` at `translate_arm.cpp:77` fires. The hook sets it.
* **The hook fires per translation, not per execution.** It costs nothing per frame; the emitted
  block is cached like any other.

The emitted block is byte-for-byte the IR that `TranslatorVisitor::arm_BX(ConditionAL, Reg::R14)`
produces for `bx lr` (`impl/a32_branch.cpp:68-81`): publish the guest's LR as the next PC, then
`IR::Term::PopRSBHint` to pop the return-stack-buffer entry the caller's `bl` pushed. A mismatched
or absent RSB entry falls through to the dispatcher on PC, which is still correct.

Why not the alternatives: an SVC byte-patch stub needs the guest `.text` to be writable (the host
deliberately keeps it read-only, `core/include/zb/guest_memory.h:27-29`) and a patched guest
binary; Dynarmic backend work needs a permanent fork. The translation hook is the smallest change
that moves a body out of the JIT and needs no guest bytes.

## 2. The problem that actually decides whether the hook fires: address identity

**A file virtual address is not a runtime address.** `libDungeonHunter2.so` is `ET_DYN`; the
guest's own linker maps its first `PT_LOAD` wherever it likes. On this device, read out of the
host's synthetic `/proc/self/maps` on the phone:

```
map fc880000 956000 0 0 /data/data/local.dh2.fold7/files/plugins/…/lib/libDungeonHunter2.so
map fd1d6000 4a000 955000 0 /data/data/local.dh2.fold7/files/plugins/…/lib/libDungeonHunter2.so
```

so the function the pipeline froze at file offset `0x0060dd34` executes at **`0xFCE8DD34`**.

My first two device runs hooked the literal `0x0060dd34` and reported **0 emissions** — the failure
mode is a silent no-op, not a crash: the guest is simply translated as before. The fix keys the
hook on `<library basename, offset in file>` and resolves the runtime PC from the mapping records
the host already keeps for crash reporting:

* `native_hook_targets()` holds `{"quat-mul", "libDungeonHunter2.so", 0x0060dd34, entry}`;
* `Process::record_file_mapping()` now also calls `native_hook_note_mapping(start, length, offset,
  path)`, which resolves `runtime_pc = start + (file_offset - offset)` for every target inside the
  mapped range and logs/report-notes the resolution; `forget_mappings()` clears it again, so an
  unloaded or re-mapped library cannot leave a stale PC behind;
* the per-instruction path is then one relaxed atomic load and a compare.

Until a library is mapped, `runtime_pc` is 0 and the hook is inert, i.e. the fork translates exactly
as it did before this file existed.

## 3. The shim

### 3.1 What has to be converted

The guest is **AAPCS32 softfp**: `r0` is the hidden sret pointer (the returned quaternion, which
the original also returns in `r0`), `r1` is `this`, `r2` is the argument quaternion
(`docs/GUEST-REWRITE-PIPELINE.md` §2). The host function is **AAPCS64**, and Dynarmic's
`CallHostFunction` fixes its shape at `void(*)(u64,u64,u64)`:

1. **Register marshalling.** The three guest argument registers arrive in `x0`, `x1`, `x2`
   zero-extended — the IR proves it (§5). Guest pointers are 32-bit and are zero-extended, never
   truncated from a host pointer.
2. **No context argument.** `CallHostFunction` carries three `u64`s and no `this`, and all three
   are already the guest's arguments. The shim therefore finds its `GuestThread` through
   `zb::current_guest_thread()`, a thread-local set by a scope at the top of `GuestThread::run()`
   (re-entrant, restored on every path). This is sound because a Dynarmic block belongs to exactly
   one JIT: the callbacks that emitted it are that `GuestThread`'s, and only that `GuestThread` can
   run it.
3. **Memory access stays checked.** The shim reads and writes guest memory with
   `GuestThread::MemoryRead32/MemoryWrite32`, the same callbacks the JIT itself uses, so an
   inaccessible page records the fault and halts the JIT exactly as translated code would, and the
   shim returns without computing or writing anything when `pending_stop()` says a fault was
   recorded. The raw `mem.base() + vaddr` fast path is deliberately not used: a native body that
   by-passes the page flags would convert a guest SIGSEGV into a host crash.
4. **`float` aggregates cross the ABI boundary through memory here, and through registers there.**
   The guest keeps the two quaternions in guest memory (they are passed by reference), while the
   AArch64 body is written as an AAPCS64 **HFA** function: `b` in `s0-s3`, `a` in `s4-s7`, result
   in `s0-s3`. The shim loads the eight 32-bit patterns from guest memory into a `Float4` pair and
   stores the returned HFA into the sret block. A float passed **by value** in `r0-r3` (softfp)
   would need the same repacking plus a per-signature record; none of the functions hooked here has
   one, and §7.1 lists it as unproven rather than pretended.
5. **No guest register is written except PC.** The emitted block contains no `A32SetRegister`
   (§5), so `r0` (the sret pointer), `r4-r11` and `sp` keep their values by construction — the
   observable contract the rewrite pipeline's spec declares for this function.

### 3.2 The floating-point environment is part of the ABI here

Dynarmic maps the **guest FPSCR onto the host FPCR** for the whole of `Run()`
(`backend/arm64/a32_address_space.cpp:252-256` sets FPCR from the block's location descriptor, and
`:346-347` restores the host's own FPCR at the end), and it does **not** save FPCR around host
calls. So the ambient FPCR during a `CallHostFunction` is "whatever the guest's FPSCR mode bits
say", which is not guaranteed to be round-to-nearest with flush-to-zero off.

The engine's arithmetic is soft-float (its `__aeabi_f*` helpers are integer code — verified by
disassembling the sysroot's `libc.so`, §4.2), so it never flushes denormals. The native body
therefore **saves FPCR and FPSR, forces FPCR = 0 (RN, FZ = 0, DN = 0, no traps) for its duration,
and restores both**, which makes its result independent of the ambient environment and leaves the
guest-visible FPSCR flags exactly as the soft-float path left them (its helpers set none). This is
not decoration: the same body without the pinning produces 39 substantive mismatches at
`FPCR.FZ=1` and 7 141 at `RMode=Z` (§4.1).

### 3.3 The native body

`core/src/native_hook_a64.S`, 40 instructions, 28 of them scalar FP:

```
stp q8,q9,[sp,#-32]! ; mrs x9,fpcr ; mrs x10,fpsr ; msr fpcr,xzr
16 x fmul, 6 x fadd, 6 x fsub         (one rounding per operation, never fused)
4 x fmov  (result HFA into s0-s3)
msr fpcr,x9 ; msr fpsr,x10 ; ldp q8,q9,[sp],#32 ; ret
```

It is the rewrite pipeline's recovered tree transliterated one operation per instruction, with the
original's statement grouping and operand order preserved:

```
out[3] = ((((b3*a3) - (b0*a0)) - (b1*a1)) - (b2*a2))
out[0] = ((((b0*a3) + (b3*a0)) + (b2*a1)) - (b1*a2))
out[1] = ((((b1*a3) + (b3*a1)) + (b0*a2)) - (b2*a0))
out[2] = ((((b2*a3) + (b3*a2)) + (b1*a0)) - (b0*a1))
```

Grouping is not cosmetic (binary32 addition is not associative) and fusion is not allowed
(`__aeabi_fmul` then `__aeabi_fadd` round twice; a fused multiply-add rounds once). `s16-s23` are
callee-saved and are spilled, so the body is safe to call from anywhere, not only through
Dynarmic's `PrepareForCall`.

A second, independently compiled implementation of the same tree
(`core/src/native_hook_reference.cpp`, built with `-ffp-contract=off`) is called beside the
assembly on every invocation and compared bit-for-bit, so a device run reports the assembly's
agreement with clang's codegen on **real game inputs**, not only on synthetic cases:
counters `native-hook-quat-mul-calls` and `native-hook-quat-mul-asm-vs-reference-mismatches`.

## 4. Evidence

### 4.1 Bit-exactness: the native ARM64 body against the ARM32 the engine actually executes

The reference is not a re-implementation. 2 308 cases (308 edge/special values swept through every
float slot of both operands, plus 2 000 random cases mixing a bounded uniform range with raw 32-bit
patterns, seed 20631002) have their expected 16 output bytes produced by **executing the original
ARM32 body** at `0x60dd34` under Unicorn (`DH2-recon/tools/dh2_oracle.py`).

Two independent emulated ISAs then run the same case file and dump 16 bytes per case:

| side | what it is | engine |
| --- | --- | --- |
| A | the ARM32 body the **shipped engine** executes: `rewrite-pipeline/out/quat_mul/quat_mul.bin` (164 B), `incbin`'d and called | `qemu-arm -cpu cortex-a15` (Unicorn cannot execute ARM VFP, `docs/GUEST-REWRITE-PIPELINE.md` §4) |
| B | the native AArch64 body the hook calls, compiled from the same `.S` the library builds | `qemu-aarch64` |

```
ARM32 body (shipped engine bytes, qemu-arm)
cases                : 2308
differs from oracle  : 14 cases, 46 lanes (NaN-class 46, substantive 0) PASS

   a64-pinned (shipped)         : 2308 cases, 14 differing cases, 46 differing lanes (NaN-class 46, substantive 0) PASS
   a64-nopin  (control)         : 2308 cases, 77 differing cases, 293 differing lanes (NaN-class 254, substantive 39) FAIL
   a64-wrong  (control)         : 2308 cases, 176 differing cases, 208 differing lanes (NaN-class 46, substantive 162) FAIL
   pinned body, all fpcr modes  : 9232 cases, 56 differing cases, 184 differing lanes (NaN-class 184, substantive 0) PASS
nopin control substantive mismatches  : 7219 (expect > 0)
wrong-formula control substantive     : 648 (expect > 0)

ARM32 shipped body (qemu-arm) vs native AArch64 body (qemu-aarch64)
cases                     : 2308
cases with any difference : 0
differing lanes           : 0 (NaN-class 0, substantive 0)
VERDICT: IDENTICAL on every case
```

Three things are worth reading carefully.

1. **A vs B: 0 differences in 2 308 cases.** The native AArch64 body is bit-identical to the ARM32
   code the engine executes, including the 14 invalid-operation cases that differ from the Python
   oracle.
2. **The same 14 cases differ from the oracle on both sides, and they are the oracle's fault.**
   They are exactly the invalid operations with no NaN operand (`inf + -inf`, or overflow to `inf`
   followed by `inf - inf`). The oracle implements `__aeabi_fadd` as Python double arithmetic
   (`_binc(lambda a, b: a + b)`) on this Windows x86-64 host, where `-inf + inf` yields the x86
   "QNaN floating-point indefinite" `0xFFC00000`; ARM's default NaN is `0x7FC00000`. The guest's
   **own** helper agrees with the native body: disassembling `__aeabi_fadd` in the arm32 bionic
   `libc.so` the host maps into the guest shows it loading that literal —

   ```
   9f25c: vldr s0, [pc, #556]        @ -> 0x9f490
   9f490: .word 0x7fc00000
   ```

   So the 14 lanes are the reference model's host artifact, and both ARM implementations produce
   0x7FC00000. They are reported as NaN-class, never hidden: 184 lanes differ from the oracle
   across the four FPCR modes, 0 are substantive.
3. **Both negative controls are detected.** An unpinned variant of the same body fails on
   flush-to-zero and rounding-mode differences (39 and 7 141 substantive mismatches), and a
   reassociated `out[0]` — algebraically identical, bit-different — fails 162 substantive lanes in
   every mode. A PASS from this harness therefore means something.

On the device hardware itself (Galaxy Z Fold7, before the device was handed back to the other
agents) the same binary ran all 2 308 cases × 4 ambient FPCR modes under an *Android* arm64 static
build: `pinned body, all fpcr modes : 9232 cases … (NaN-class 184, substantive 0) PASS`, with the
same two controls failing. Identical results on the real CPU and under qemu.

### 4.2 The mechanism's IR, off-device, through Dynarmic's own translator

`nativehook-test/ir_test.cpp` runs the real `A32::Translate()` over a synthetic page with a
`TranslateCallbacks` wired exactly as `GuestThread`'s override is (claim the PC, emit, return
false). It is a host x86_64 build of the Dynarmic frontend only — no guest, no JIT, no device — and
prints Dynarmic's own `DumpBlock()`:

```
translating a block at the hooked pc 0xfce8dd34
-- Dynarmic's own dump of the block the hook installs --
Block: location={00000000fce8dd34}
cycles=0, entry_cond=al
[00007630218ed010] noname = GetRegister r2 (uses: 1)
[00007630218ed078] noname = ZeroExtendWordToLong %<…ed010> (uses: 1)
[00007630218ed0e0] noname = GetRegister r1 (uses: 1)
[00007630218ed148] noname = ZeroExtendWordToLong %<…ed0e0> (uses: 1)
[00007630218ed1b0] noname = GetRegister r0 (uses: 1)
[00007630218ed218] noname = ZeroExtendWordToLong %<…ed1b0> (uses: 1)
[00007630218ed280]          CallHostFunction #0x5aa285b1a980, %<…ed218>, %<…ed148>, %<…ed078> (uses: 0)
[00007630218ed2e8] noname = GetRegister lr (uses: 1)
[00007630218ed350]          BXWritePC %<…ed2e8> (uses: 0)
terminal = PopRSBHint{}
-- checks --
   [ok] PreCodeReadHook claimed the target pc exactly once
   [ok] translation stopped at the hook: no instruction of the guest body was read
   [ok] the block has a terminal (translate_arm.cpp:77 asserts this)
   [ok] exactly one CallHostFunction is emitted
   [ok] no second CallHostFunction
   [ok] its first argument is the immediate address of the host entry point
   [ok] argument 1 is the zero-extended guest r0 (from A32GetRegister)
   [ok] argument 2 is the zero-extended guest r1 (from A32GetRegister)
   [ok] argument 3 is the zero-extended guest r2 (from A32GetRegister)
   [ok] A32BXWritePC is emitted (the `bx` half of `bx lr`)
   [ok] A32BXWritePC's operand is the guest LR (r14)
   [ok] the terminal is IR::Term::PopRSBHint (identical to TranslatedVisitor arm_BX on r14)
   [ok] no A32SetRegister: the emitted block writes no guest register except PC
-- target resolution (mapping records from the device) --
   [ok] libDungeonHunter2.so at 0xfc880000 + 0x0060dd34 resolves to 0xFCE8DD34
   [ok] and not to the file virtual address the first two device runs hooked
   [ok] the library's second PT_LOAD record does not resolve this target
   [ok] a mapping that does not contain the offset resolves nothing
   [ok] a record whose file offset is past the target resolves nothing
   [ok] file offset 0 resolves to the mapping start
IR_TEST_PASS (0 failure(s))
```

This is the strongest statement available off-device about the mechanism: the hook's own code,
run through Dynarmic's own translator, emits exactly the block described in §1 — and the guest body
is never read. The second half of the same test covers the address arithmetic from §2 with the
mapping numbers read off the phone, including the negative case (a file virtual address is not a
runtime address) and both out-of-range cases.

### 4.3 What is at the hooked site in the engine this build ships

The engine packed into the launcher APK **already contains the rewrite pipeline's 164-byte AArch32
VFP replacement** at this offset:

```
candidate bytes            : 164
shipped prefix == candidate: True
shipped tail   == pristine : True
shipped first 16 bytes     : 008a91ed018ad1ed029a91ed039ad1ed   (vldr s16,[r1]; vldr s17,[r1,#4]; …)
candidate first 16 bytes   : 008a91ed018ad1ed029a91ed039ad1ed
```

That matters for how this work is valued, and the honest accounting is in §7.2.

## 5. What fires when, and what it costs

| event | when | cost |
| --- | --- | --- |
| `PreCodeReadHook` | once per instruction **translated** | one relaxed atomic add and a compare, per target, per instruction translated |
| hook match → IR emission | once per block created at the resolved PC (and again after a code-cache invalidation of that range) | one host `blr` site per block; a log line and a report note |
| the host call | once per **execution** of the hooked function | ~40 AArch64 instructions + FPCR/FPSR save/restore, 8 checked 32-bit reads and 4 checked 32-bit writes, plus the C++ reference cross-check |
| the original body | never — translation stopped before the first instruction was read, so it is neither translated nor executed | — |

No per-frame hook overhead exists: the hook is a translation-time callback, so the only per-frame
cost is the host call itself. Nothing in the emitted block touches a guest register other than PC.

## 6. Static verification of the built library

```
=== library ===
3015352 bytes
5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd
=== zb_quat_mul_a64: instruction census ===
instructions: 40 ; fmul: 16 ; fadd: 6 ; fsub: 6 ; fused (must be 0): 0 ; FPCR-FPSR accesses: 5
=== hook entry points present in the object files ===
zb_native_hook_quat_mul 1 ; zb_quat_mul_a64 2 ; zb_quat_mul_reference 2
=== exported dynamic functions (must still be the same 13) ===
13
```

The shrunk fork's baseline is 2 972 592 B / `674e24f0…`; the shipped host is 3 306 992 B /
`25e8da7e…`. The hook adds 42 760 B over the shrunk baseline (the `fmt`/`mcl` headers the IR
emitter drags in, the AArch64 body, the C++ reference and the report plumbing) and is still
291 640 B smaller than the shipped host. The staged `jniLibs` slot was restored to the shipped
`25e8da7e…` afterwards and verified there.

## 7. Honest statement of what is unproven

### 7.1 The device-side firing is unverified

The library was installed and run on the phone (`versionName 1.0-nativehook27/28/300`,
`libzbridge.so` at the hashes in §6); with it installed the game boots and **renders** — the
Dungeon Hunter 2 loading screen, `0` `guest SIGSEGV` lines, `0` unimplemented host calls, the
`:guest` process alive, `gl-calls` climbing — and the report plumbing shows the host's own
counters. What no run of mine captured is the *hooked path executing*, because:

* the first two runs hooked the file VA `0x0060dd34`, which never matches at runtime (§2) — that
  bug is found, fixed and unit-tested, but the fix itself has only been exercised off-device;
* every later window was cut short by other agents installing over the package (`dh2-exits.txt`:
  `reason=16 … due to installPackageLI` at 21:32:23, 21:34:11, plus a version code that jumped from
  200 to 1000 inside 40 s of my polling run) or by the app being force-stopped; and the engine
  spends a long time in its loading phase before it reaches code that calls this function
  (`nativeRender=59` in 60 s of loading versus 3 947 in the reference run's 129 s).

So the correct claim is: **the hook is installed, compiles, and its emitted IR is verified
off-device; the arithmetic is verified bit-exact against the shipped ARM32 bytes on both emulated
ISAs and against the original ARM32 on the device CPU; and the game runs with the hooked library
installed. The hooked path executing inside the running game is not verified.** The counters that
would show it (`native-hook-resolved`, `native-hook-hook-invocations`,
`native-hook-blocks-emitted`, `native-hook-quat-mul-calls`, `native-hook-quat-mul-asm-vs-reference-mismatches`)
are in place and pullable with `adb shell cat
/sdcard/Android/data/local.dh2.fold7/files/zb-runtime-report.txt`, and the hook also logs
`native hook: resolved …`, `native hook: emitting host call …` and
`native hook quat_mul: first host-native call r0=… r1=… r2=… fpcr=…` to logcat, which on this
ROM works for this package (the `zbridge` tag lines arrive).

### 7.2 The performance case for hooking *this* address on *this* build is not established

The rewrite pipeline's 3 168 → 41 translated-instruction figure is a property of the **pristine**
engine. The engine this build ships already carries that 41-instruction VFP body at the site
(§4.3), so on the device the hook replaces ~41 translated AArch32 instructions with ~40 native
AArch64 instructions plus 12 checked memory callbacks, the reference cross-check and the FPCR
save/restore. That is a mechanism proof, not a measured speed-up, and no frame-time comparison
against the same build without the hook was made. Reaching the 82.7 % JIT share needs many
functions hooked, not this one; and for a function the shipped build has already rewritten to VFP,
the win must be re-measured rather than assumed.

### 7.3 Scope limits of the shim

* **Float arguments by value, doubles, and stack arguments are not exercised.** The hooked
  signature is three pointers; the HFA repacking works, but a softfp `float` passed in `r0-r3`, a
  `double` in a core-register pair, an argument beyond `r3`, or a hidden-sret signature that shifts
  `this` to `r1` would each need a per-signature record and its own bit-exactness test.
* **More than three guest argument registers cannot be forwarded** through
  `CallHostFunction`'s three `u64`s without a staging buffer in guest memory; not implemented.
* **One target.** The table has one entry; a second function is a table row plus a shim, but the
  table is a compile-time array, not a configuration surface.
* **Re-entrancy.** The host body must not re-enter guest code: `CallHostFunction` runs on the JIT's
  stack with the block's register state spilled, and nothing in this shim calls back into the JIT
  except through the checked memory callbacks.
* **The `thread_local` `GuestThread`** is set only for the duration of `GuestThread::run()`. A
  Dynarmic block invoked without `run()` (there is no such path in this host) would find it null
  and the shim would log and return rather than compute.
* **FPCR pinning is per call.** A guest that actually read FPSCR between two calls would see the
  flags its soft-float helpers set (none), not the flags the native body's FP operations raised —
  which is the intended fidelity, but it is a deliberate choice, not an accident.

### 7.4 Repository state, and what came from other agents

* The fork is edited concurrently by other agents (the GL shadow work, `thread_perf.cpp`, additional
  GL host-call indices). The library in §6 was built from a snapshot that includes their edits; two
  in-flight states would not compile at all (a missing `#include "zb/gl_hostcalls.h"` in
  `gl_manual.cpp`, an undeclared `maybe_write_histogram_file`) and the private build root adds that
  include locally so the native-hook build is not blocked by an unrelated edit
  (`scripts/nh-sync.sh`). This is why the build scripts use a private WSL tree rather than the
  shared one: two `rsync --delete` overlays racing each other corrupt the tree.
* The report file is now written `0664` instead of `0600` (plus `fchmod`, because the mode argument
  only applies at creation and the temporary can outlive a 0600 writer) so that a run report can be
  read off the device with `adb shell cat`. The file lives in the app's own external files
  directory next to `dh2-session.txt`/`dh2-logcat.txt`, which are already group-readable.
* `adb pull` cannot create files in this workspace (the file sandbox denies the adb child process),
  and `adb exec-out screencap -p` prepends a device warning to the PNG stream on this ROM; the run
  scripts compensate for both.

## 8. Reproducing

```powershell
# 1. build the arm64 library with the hook (private WSL tree; never the shared one)
wsl -d Ubuntu -- bash /mnt/c/…/host/shrink/scripts/nh-sync.sh
wsl -d Ubuntu -- bash /mnt/c/…/host/shrink/scripts/nh-build.sh      # -> core/libzbridge.so 5c769c9d…

# 2. off-device: the native body vs the ARM32 body the engine ships, both under qemu
wsl -d Ubuntu -- bash /mnt/c/…/host/shrink/nativehook-test/qemu_ab.sh

# 3. off-device: the emitted IR, through Dynarmic's own translator
wsl -d Ubuntu -- bash /mnt/c/…/host/shrink/nativehook-test/build_ir_test.sh

# 4. the case file itself (expected bytes from the pristine ARM32 body under Unicorn)
python …\host\shrink\nativehook-test\gen_cases.py
```

Files: `ZettaBridge/core/{include/zb/native_hook.h,include/zb/native_hook_ir.h,src/native_hook.cpp,
src/native_hook_ir.cpp,src/native_hook_a64.S,src/native_hook_reference.cpp}`, plus the
`GuestThread::PreCodeReadHook` override, the `Process::record_file_mapping`/
`forget_mappings` notifications, the `native-hook-*` report section and the CMake wiring.
Tests and harnesses: `host/shrink/nativehook-test/`, device scripts: `host/shrink/scripts/`.
