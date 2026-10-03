# HOST-RUN-HARNESS.md — running guest ARM32 code off-device

**What this document is for.** Until now, no component of the replacement host had
executed a single guest ARM32 instruction. `host/loader` loaded and relocated the
real engine and the whole bionic sysroot and then stopped at the execution
boundary; `host/rt`'s documented cheapest next step was "link the prebuilt Dynarmic
plus a cp15 shim and execute `mrc p15,0,r0,c13,c0,3; bx lr`, asserting it returns
the installed thread pointer". That step has now been taken, and made reusable:
this is the documented harness under
`DH2Work-toolchain\host\harness\`, how to build it, and the recipes it exists to
serve — every one of them with a real command line and the output it produced.

**Verified on this machine:** WSL2 Ubuntu, kernel `6.18.33.2-microsoft-standard-WSL2`,
20 CPUs, 15 GB RAM; pinned Dynarmic `86458a0bd369d63ba4c2ef812cacbb6c9080c065`
(unmodified); CMake 4.2.3, Ninja 1.13.2, GCC 15.2.0; Boost 1.90 headers at
`/usr/include`. Host is Linux x86_64, so Dynarmic uses its **x64** backend.

Nothing in this document touches the phone or any emulator. Nothing here writes
outside `host/harness/` and this file.

---

## 0. The known first test, and what it prints

```
$ /root/dh2-harness/build/dh2run --arm 700f1dee1eff2fe1 --tp 0xdeadb000
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 2
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
r0          : 0xdeadb000  (unsigned 3735924736)
r1          : 0x00000000
...                                          (r2..r12 all 0x00000000; trimmed)
sp          : 0x70080000
lr          : 0x7ffffff0
pc          : 0x7ffffff0  (ARM)
coprocessor : mrc p15, 0, <Rt>, c13, c0, 3   [served: TPIDRURO]
faults      : none
```

The two instruction words are `mrc p15, 0, r0, c13, c0, 3` and `bx lr`; the byte
string `700f1dee1eff2fe1` is what `tools/asm.sh` prints for
`tests/guest/cp15_read_tp.s`. `--tp 0xdeadb000` installs the guest thread pointer,
and the guest reads it back: **a guest ARM32 instruction executed on the host, and
CP15 returned the value the host installed.** The same probe passes in Thumb state
(the encoding is identical, the `bx lr` is not), which matters because the project's
sysroot is mostly Thumb.

`host/harness/tests/selftest.cpp` is the regression net around this and everything
below: **44 checks, 0 failures**, covering ARM and Thumb CP15, TPIDRURW vs
TPIDRURO, a refused CP15 register, condition flags, load/store, fault attribution,
VFP, Thumb-16, breakpoints, the step limit, SVC, BKPT, the exclusive monitor and the
guest address space. Every instruction byte in it comes from an assembler, and
`tools/derive-encodings.sh` re-derives them all.

---

## 1. Build it: one command

```bash
wsl -d Ubuntu -- bash /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/harness/tools/build-wsl.sh
```

Verified transcript (abridged where marked; `--clean` re-stages from the pin):

```
== staging the pinned Dynarmic tree
   from /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/dynarmic
   to   /root/dh2-harness/src/dynarmic
   2159 files staged
   dynarmic revision marker:
     86458a0b fix compilation with Clang 20
== configuring (/root/dh2-harness/build)
-- Target architecture: x86_64
-- Found Boost: /usr/include (found suitable version "1.90", minimum required is "1.57")
-- dh2 harness: Dynarmic src      = /root/dh2-harness/src/dynarmic
-- dh2 harness: Dynarmic lib      = 
-- dh2 harness: Boost headers     = /usr/include
== building dh2run dh2selftest
[167/169] Linking CXX static library libdh2harness.a
[168/169] Linking CXX executable dh2selftest
[169/169] Linking CXX executable dh2run

== dh2selftest
dh2selftest -- the DH2 off-device execution harness tests itself
(instruction bytes: tests/guest/*.s, re-derived by tools/derive-encodings.sh)
...                                                            (44 checks; trimmed)
44 check(s), 0 failure(s)

== the known first test, as dh2run prints it
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 2
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
r0          : 0xdeadb000  (unsigned 3735924736)
...
coprocessor : mrc p15, 0, <Rt>, c13, c0, 3   [served: TPIDRURO]
faults      : none

binaries: /root/dh2-harness/build/dh2run  /root/dh2-harness/build/dh2selftest
```

**What the previous agent recorded, and what was actually needed.** The notes said
the A32 frontend needs `DYNARMIC_FRONTENDS=A32`, Boost headers, and a FindBoost
shim. All three are real, and one more thing is worth writing down:

| Claim | Status | Detail |
|---|---|---|
| `DYNARMIC_FRONTENDS=A32` | **required** | The A64 translator is dead weight here and is the part that fails to configure on a host without oaknut. Set `FORCE` in `CMakeLists.txt`. |
| Boost headers required | **required** | `find_package(Boost 1.57 REQUIRED)` at the top of Dynarmic's `CMakeLists.txt`; headers only (`boost/variant.hpp`, `boost/icl/*`). |
| FindBoost shim required | **required on this host, for two reasons** | CMake 4.2.3 **removed** the `FindBoost` module, and a headers-only Boost has no `BoostConfig.cmake`, so CONFIG mode cannot find it either. `cmake/FindBoost.cmake` satisfies both from a plain include directory and reports the version it actually found. |
| `DYNARMIC_USE_BUNDLED_EXTERNALS=ON` | required | fmt/mcl/oaknut/zydis are in the pinned tree, so the build is offline. |
| `DYNARMIC_USE_PRECOMPILED_HEADERS=OFF` | added here | A PCH turns a toolchain mismatch into an unrelated compile error. |

Two ways to supply Dynarmic, both supported: `-DDH2_HARNESS_DYNARMIC_SRC=<tree>`
(build in-tree — the default, reproducible path) or
`-DDH2_HARNESS_DYNARMIC_LIB=<libdynarmic.a> -DDH2_HARNESS_DYNARMIC_INC=<tree>/src`.
The pre-existing prebuilt archive at
`DH2Work-toolchain\dynarmic\build-a32\src\dynarmic\libdynarmic.a` (16,035,360 bytes)
works with the second form.

**No Dynarmic patch is needed for anything in this document.** The pinned revision
is used unmodified: the cp15 thread-pointer read goes through Dynarmic's
`Coprocessor` interface, not through its frontend, and the one flag that would need
patch 0001-A (`check_halt_on_memory_access`) is guarded correctly on the x64 backend
already. Patch 0001-A matters only for an **arm64** host; see
`DYNARMIC-INDEPENDENCE.md` §2.1 and `--imprecise-faults` below.

---

## 2. Recipe — execute one instruction and see the result

### 2.1 Get the bytes from a real assembler

Never hand-write these bytes. The project has already paid for one round of
ARM/Thumb confusion (`DYNARMIC-INDEPENDENCE.md` §1: Thumb `movw r4,#0x400` decodes
as ARM `vaddhn.i16`, a 16-bit `push` prologue as `svc`).

```bash
$ bash tools/asm.sh tests/guest/vfp_add.s
source : .../harness/tests/guest/vfp_add.s
state  : as declared in the source (.arm / .thumb)
bytes  : 10 0a 00 ee 90 1a 00 ee 20 1a 30 ee 10 0a 11 ee
1e ff 2f e1
hex    : 100a00ee901a00ee201a30ee100a11ee1eff2fe1
dh2run : dh2run --arm 100a00ee901a00ee201a30ee100a11ee1eff2fe1
size   : 20 byte(s)
```

`tools/asm.sh FILE.s [arm|thumb]` also takes `--dis` to disassemble the object back
with `llvm-objdump` as a second, independent signal. It uses the Linux NDK clang at
`~/dh2/toolchains/android-ndk-r29/.../bin/clang` (override with `DH2_NDK_CLANG`).

### 2.2 Run it

```bash
$ dh2run --thumb 012001307047 --trace
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 3
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
r0          : 0x00000002  (unsigned 2)
...
pc          : 0x7ffffff0  (ARM)
coprocessor : (never consulted -- the guest executed no coprocessor instruction)
faults      : none
pc=00010000  2001      movs r0, #1
pc=00010002  3001      adds r0, #1
pc=00010004  4770      bx lr
```

`--trace` prints one line per executed instruction with the pc, the raw encoding
and, for ARM and 16-bit Thumb, the mnemonic. (Dynarmic's public disassembler has no
32-bit Thumb entry point, so a Thumb-2 encoding is shown as raw halfwords rather
than as a possibly-wrong mnemonic — deliberate, per §1 of the independence doc.)

### 2.3 The CP15 first test (this is `host/rt`'s documented next step)

```bash
$ dh2run --arm 700f1dee1eff2fe1 --tp 0xdeadb000
# ... and the same instruction in Thumb state, where only the return differs:
$ dh2run --thumb 1dee700f7047 --tp 0xdeadb000
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 2
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
r0          : 0xdeadb000  (unsigned 3735924736)
r1          : 0x00000000
...                                          (r2..r12 all 0x00000000; trimmed)
sp          : 0x70080000
lr          : 0x7ffffff0
pc          : 0x7ffffff0  (ARM)
coprocessor : mrc p15, 0, <Rt>, c13, c0, 3   [served: TPIDRURO]
faults      : none
```

Both are asserted in `dh2selftest`:
`ARM: r0 == the installed thread pointer  0xdeadb000 == 0xdeadb000` and
`Thumb: r0 == the installed thread pointer 0xdeadb000`.

The shim answers exactly `c13,c0,3` (TPIDRURO), `c13,c0,2` (TPIDRURW) and the three
pre-ARMv7 `c7` barrier encodings, and **records every CP15 operation the translator
asked about** — the `coprocessor :` lines in the output. That census is how a
workstream finds out that its guest touches a register this shim refuses.

### 2.4 VFP — the thing the Unicorn oracle cannot do at all

`TEST-ENVIRONMENT-POLICY.md` §3: *"Unicorn 2.1.4 cannot execute a single ARM VFP
instruction on any of its 33 ARM CPU models"*, and `qemu-arm` was therefore the only
option for float work. Dynarmic executes VFP on the x86_64 host:

```bash
$ dh2run --arm 100a00ee901a00ee201a30ee100a11ee1eff2fe1 \
         --reg r0=0x40200000 --reg r1=0x40600000 --f32 r0 -q
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 5
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
f32 r0      : 0x40c00000 == 6
coprocessor : (never consulted -- the guest executed no coprocessor instruction)
faults      : none
```

`vmov s0,r0` / `vmov s1,r1` / `vadd.f32 s2,s0,s1` / `vmov r0,s2` / `bx lr`:
`2.5f + 3.5f = 6.0f` as `0x40c00000`. `--f32 rN,sN` prints a labelled float reading,
and `--show-ext` dumps all of `s0..s31` and `d0..d15`.

This means **softfp float comparisons no longer need qemu-arm**: set the register
with the argument's bit pattern (`--reg r0=0x40200000`) and read the result's bits
out of `r0`, exactly as `dh2_oracle.py` does for its softfp ABI.

---

## 3. Recipe — run the guest up to a breakpoint address

### 3.1 In hand-assembled code

```bash
$ dh2run --thumb 012001307047 --until 0x00010004 -q
stop        : Breakpoint -- pc reached breakpoint 0x00010004
instructions: 2
```

It stops **before** executing the instruction at the breakpoint (`--until` is a
pre-execution check), so the register file is the state the guest had on arrival.
`--steps N` bounds the run independently; `--until` is repeatable and takes a
comma-separated list.

### 3.2 In the real engine

```bash
$ dh2run --elf <engine> --sym _ZN14ObjectSearcher10PlayerList5ResetEv \
         --args 0x71000000 --until 0x0038d528 -q --dump 0x71000000:8
stop        : Breakpoint -- pc reached breakpoint 0x0038d528
instructions: 1
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
entry state : state from $a mapping symbol covers the address
coprocessor : (never consulted -- the guest executed no coprocessor instruction)
faults      : none
memory 0x71000000 .. +0x8:
  71000000  00 00 00 00 00 00 00 00                           |........|
```

`--elf` maps the object's `PT_LOAD` segments at their link addresses and `--sym`
resolves a function symbol. The first instruction (`mov r3,#0`) ran; the `str r3,[r0,#8]`
at `0x0038d528` did not, so the scratch word is still zero. **Relocations are not
applied** — see §6.2 for what that costs and what it looks like when it bites.

**Instruction-set state is never guessed silently.** Every `--elf` run prints the
`entry state` line, and `--sym-info` shows both signals:

```bash
$ dh2run --elf <engine> --sym-info _ZNK22Script_UnlockCharacter10IsBlockingEv
symbol        : _ZNK22Script_UnlockCharacter10IsBlockingEv
st_value      : 0x0045574c   (bit 0 = 0 -> ARM or unspecified)
st_size       : 8
section       : .text
symbol table  : .dynsym
mapping symbol: $a
state by $a/$t: ARM
decision      : state from $a mapping symbol covers the address
```

On a *stripped* library there are no `$a`/`$t` symbols and the low bit is the only
signal; the harness says so in the same line rather than defaulting quietly.

### 3.3 Runtime addresses vs file addresses

`--load-bias` moves the image so that pcs come out as the runtime addresses a device
log shows. The engine is `ET_DYN` mapped at `0xfc880000` on the device, so file
offset `0x60dd34` (link vaddr, since the engine links at 0) executes at
`0xFCE8DD34`:

```bash
$ dh2run --elf <engine> --sym _ZNK22Script_UnlockCharacter10IsBlockingEv \
         --load-bias 0xfc880000 --trace -q
stop        : Returned -- pc reached the return sentinel 0x7ffffff0
instructions: 2
...
pc=fccd574c  e3a00000  mov r0, #0
pc=fccd5750  e12fff1e  bx lr
```

`0xfccd574c == 0xfc880000 + 0x0045574c`. Any address-keyed comparison against a
device log must do this subtraction or addition; a bias only moves the image, it
still applies no relocations.

---

## 4. Recipe — compare a guest function's output against `dh2_oracle.py`

The oracle executes the pristine engine under Unicorn and has been verified over
3,009 functions / 63,118 comparisons. The harness executes the same engine under
Dynarmic. Two independent implementations of AArch32 semantics, same function, same
arguments — that is a real differential test, and it is what most workstreams
actually want to be able to say.

**The defaults were chosen to make this trivial:** both tools put `sp` at
`0x70080000`, scratch at `0x71000000`, and the return sentinel at `0x7FFFFFF0`, and
both run the object at its link addresses.

```bash
# on Windows Python (where the oracle's bundled Unicorn lives)
$ cd DH2Work-toolchain\host\harness
$ python tools\dh2_vs_oracle.py --sym _ZNK22Script_UnlockCharacter10IsBlockingEv
function   : _ZNK22Script_UnlockCharacter10IsBlockingEv
address    : 0x0045574c  (ARM)
oracle     : dh2_oracle.py (Unicorn 2.1.4)
harness    : dh2run (Dynarmic A32)  ->  stop=Returned steps=2
             pc reached the return sentinel 0x7ffffff0
oracle err : None

reg   oracle       dh2run       
r0    0x00000000   0x00000000   match
...                                              (r1..lr; all match)
lr    0x7ffffff0   0x7ffffff0   match

all compared registers and memory match.
```

With memory in the comparison, and an explicit seed:

```bash
$ python tools\dh2_vs_oracle.py --sym _ZN14ObjectSearcher10PlayerList5ResetEv \
      --args 0x71000000 --dump 0x71000000:16 --steps 64
...
mem 0x71000000: oracle 00000000000000000000000000000000
             dh2run 00000000000000000000000000000000   match

all compared registers and memory match.
```

`--seed ADDR=HEXBYTES` writes the same bytes into guest memory in both tools before
the call, `--dump ADDR:LEN` compares memory afterwards, and `--steps` bounds the
harness side. The script converts the Windows path to `/mnt/c/...` for you and calls
`wsl` with an argument vector, so nothing goes through a shell.

### 4.1 What a *disagreement* looks like, and how to read it

The oracle hooks the engine's 351 PLT imports with Python implementations; the
harness applies no relocations. A function that calls an import therefore diverges,
and the tool says so rather than showing a bare mismatch:

```bash
$ python tools\dh2_vs_oracle.py --sym 0x0035e8e0 --args 0x71000000 \
      --seed 0x71000000=000040400000804000000000 --dump 0x71000000:12 --steps 2000
harness    : dh2run (Dynarmic A32)  ->  stop=Exception steps=15
             UndefinedInstruction at pc=0x00994ff0
oracle err : None
imports    : oracle hooked 11 import call(s): __aeabi_fmul, __aeabi_fmul, __aeabi_fadd, ...

reg   oracle       dh2run       
r0    0x71000000   0x40400000   DIFFER
...
8 difference(s). Before treating one as a harness bug: check the oracle's
imports_called list (dh2run applies no relocations, so an imported call is
expected to diverge), and check the mapped-region list dh2run reports under "faults".
```

`vector3d<float>::normalize` ran 15 instructions, reached its first `bl` into an
unrelocated PLT slot, and stopped with a named undefined-instruction exception at
`pc=0x00994ff0` — which is **exactly the boundary this harness has and `host/loader`
does not.** To run that function, load and relocate it with `host/loader` (59,623
relocations, 102,378 checks) and drive Dynarmic from there; the harness's
`dh2h` callbacks are the seam. It is better for the harness to stop and say
`UndefinedInstruction at pc=0x00994ff0` than to return a plausible wrong answer.

---

## 5. Recipe — read a failure instead of guessing

**A load through an unmapped address** (`ldr r0,[r0]` with `r0 = 0x50000000`):

```bash
$ dh2run --arm 000090e51eff2fe1 --reg r0=0x50000000 -q
stop        : MemoryFault -- read of 4 byte(s) at 0x50000000 is outside every mapped
              region (instruction 0, faulting instruction at pc=0x00010000)
instructions: 1
cpsr        : 0x00000010  N=0 Z=0 C=0 V=0 T=0 mode=USR
coprocessor : (never consulted -- the guest executed no coprocessor instruction)
fault       : read of 4 byte(s) at 0x50000000 at instruction 0
```

The fault names the address, the width, the direction, the instruction count **and
the pc of the instruction that faulted** — which is only possible because the run
loop single-steps (§8). There is no silent `return 0`.

**A CP15 register the shim refuses** (`mrc p15,0,r0,c1,c0,0`, MIDR — privileged):

```bash
$ dh2run --arm 100f11ee1eff2fe1 -q
stop        : Exception -- mrc p15, 0, <Rt>, c1, c0, 0: the harness coprocessor shim
              implements only p15 c13,c0,2 (TPIDRURW), p15 c13,c0,3 (TPIDRURO) and the
              three p15 c7 barrier encodings
instructions: 1
```

That is a design decision worth knowing: on the x64 backend, Dynarmic implements
"coprocessor exception" as `ASSERT_FALSE("Should raise coproc exception here")`
(`backend/x64/a32_emit_x64.cpp:844`), so the textbook answer — return
`std::monostate` — **aborts the harness process**. The shim returns a callback that
halts instead, which turns "the guest touched a register I do not implement" into a
named stop with the register spelled out. This was found by running it, not by
reading it: the first self-test run died with
`assertion failed: false / Message:Should raise coproc exception here`.

**SVC** stops with `Svc` and says the harness has no syscall layer. **WFI/WFE/SEV**
and **BKPT** are reported by name (`--hints-as-nops` lets the hints behave as the
architecture's no-ops). **A Dynarmic instruction with no JIT path** stops with
`InterpreterFallback` rather than silently interpreting.

**`--json`** emits the same facts as a single machine-readable object (stop,
detail, steps, pc, thumb, cpsr, `regs`, `coprocessors`, `faults`, `memory`), which
is how `tools/dh2_vs_oracle.py` consumes it and how another tool should.

---

## 6. What the harness is not

1. **Not `host/mem`.** Guest memory is a flat 4 GiB anonymous mapping plus a named
   region list — the dumbest thing that can be correct, so a mismatch is the
   guest's behaviour and not the mapper's. To test `host/mem` under execution, give
   `Dh2UserCallbacks` a second implementation over it; the Dynarmic-facing surface
   is the eight `MemoryRead*`/`MemoryWrite*` methods.
2. **No relocations, no dynamic linking, no constructors.** `--elf` maps `PT_LOAD`
   segments. A self-contained function runs; anything reaching the GOT/PLT stops in
   a way that names itself (§4.1). For the relocated engine, `host/loader`.
3. **No syscall layer.** `SVC` stops. That is the host's job.
4. **One core, one thread.** One exclusive monitor, one thread pointer. LDREX/STREX
   is modelled as a compare-and-swap against the expected value, which is exactly
   right for one core and honestly wrong for two — and a one-core harness cannot
   create the two-thread case in the first place.
5. **Not a speed tool.** The run loop single-steps for exactness; `Run()`-based
   batching would be faster and would miss mid-block breakpoints. `host/perf` and
   `zbrun` are the throughput tools (see §7).
6. **No Thumb-32 disassembly in `--trace`** (see §2.2).

---

## 7. `zbrun`: the Linux x86_64 host build

`TEST-ENVIRONMENT-POLICY.md` names `zbrun` as the primary harness but warns that the
shipped binary is an **android-arm64** build. One now exists for the host.

### 7.1 Build it: one command

```bash
wsl -d Ubuntu -- bash /mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/host/harness/tools/zbrun-host.sh
```

Verified transcript:

```
== staging ZettaBridge from /root/dh2/src/ZettaBridge
   2544 files staged
== using the staged tree's own Dynarmic at .../src/ZettaBridge/third_party/dynarmic
== configuring (/root/dh2-harness/zbrun-host/build)
-- Target architecture: x86_64
-- Found Boost: /usr/include (found suitable version "1.90", minimum required is "1.57")
-- zbrun host: 37 core source file(s), thunks.S replaced
-- zbrun host: target zbrun_host -> .../build/zbrun_host
== building zbrun_host
[199/201] Linking CXX static library libzbcore_host.a
[200/201] Linking CXX executable zbrun_host

== zbrun_host, no arguments (expect the usage text and exit 2)
usage: zbrun [--sysroot DIR] [--env NAME=VALUE]... <arm32-executable> [args...]
  --sysroot DIR     arm32 Android system files (default: $ZB_SYSROOT)
  --report FILE     persist guest log tail, exits and register diagnostics
  --env NAME=VALUE  set a variable for the guest only; the host dynamic loader never
                    sees it (use for guest LD_DEBUG, LD_LIBRARY_PATH, ...)
  --precise-faults  exact guest state at memory faults (slower; also $ZB_PRECISE_FAULTS=1)
exit=2
```

The whole of `zbcore` compiles for x86_64 — a syntax-only sweep of all 36
non-Android `core/src/*.cpp` files with plain `g++ -std=c++20` gives `ok=36 fail=0`.
Only three deltas were needed, and none of them touches the ZettaBridge tree (the
source is *copied* into `~/dh2-harness/zbrun-host/src`; the original checkout is
read-only input):

| # | Delta | Why |
|---|---|---|
| 1 | `core/src/android/*` excluded | Those are the Android driver backends and are in the Android-only `libzbridge` target, not in `zbcore`. |
| 2 | `core/src/jni/thunks.S` replaced by `zbrun-host/thunks_host_x86_64.cpp` | **A design boundary, not a syntax problem.** The thunk pool works on an arm64 host because a guest `bl` to a thunk lands in host code whose register file is close enough to the guest's to save-and-dispatch. On x86_64 the guest register file lives in Dynarmic's JIT state, so a native entry point cannot recover it; the guest must be intercepted inside Dynarmic. The replacement defines the same three symbols with `kNativeThunkCount * 8` bytes of ARM `UDF #0`, so a guest reaching a registered native method *traps by name* instead of executing junk. |
| 3 | `DYNARMIC_FRONTENDS=A32`, `DYNARMIC_TESTS=OFF`, `DYNARMIC_USE_BUNDLED_EXTERNALS=ON` | Exactly what ZettaBridge's own top-level `CMakeLists.txt` force-sets. |

### 7.2 It runs real guest ARM32 executables

ZettaBridge ships 21 guest test executables built by its own `tools/build_guest.sh`.
Run under the host `zbrun_host` with the stage sysroot, **15 of 21 exit with the
status their source asks for** (`hello_static` and `hello_dynamic` deliberately
return 7 and 3 after all their checks pass):

```
$ SYS=<stage>/ZettaBridge/build/launcher/assets/zb/sysroot
$ Z=/root/dh2-harness/zbrun-host/build/zbrun_host
$ G=/root/dh2/src/ZettaBridge/build/guest
$ for t in ...; do timeout 90 $Z --sysroot "$SYS" $G/$t; done

asimd_narrow_static      exit=0    asimd narrow ok
bench_dynamic            exit=0    total       1.203s
cp15_barrier_dynamic     exit=0    cp15 barriers survived: 7
cxx_dynamic              exit=1    library "libzbthrow.so" not found: needed by main executable
filemap_dynamic          exit=1    open failed
fpmicro_dynamic          exit=0    f toint  0.000s
hello_dynamic            exit=3    malloc=PASS          (exit 3 is the source's own return)
hello_static             exit=7    pid=PASS             (exit 7 is the source's own return)
host_call_static         exit=1    (no stdout; needs a host-call registration the CLI does not install)
kuser_dynamic            exit=0    get_tls=PASS
log_dynamic              exit=0    log=called
mremap_dynamic           exit=0    read-only move kept contents: 1
or_dlopen_dynamic        exit=2    usage: or_dlopen_dynamic <dir with lib*.so>
signals_dynamic          exit=134  float=PASS           (SIGABRT in the guest signal path)
sigrestart_dynamic       exit=0    sem_wait returned 0 errno 0 after 1 signals
sigwake_dynamic          exit=0    SIGUSR2 blocked again: 1
syscalls_dynamic         exit=0    nprocs=PASS
tbh_static               exit=0    tbh near=17 distant=98
threads_dynamic          exit=0    distinct=PASS
zlib_dynamic             exit=0    crc32 56fbd263 adler32 76f7723c
zbhost                   exit=2    (needs arguments)
```

What that covers, in terms the project cares about: **guest threads**
(`threads_dynamic`), **syscalls** (`syscalls_dynamic`), the **dynamic linker** plus
`dlopen` (`hello_dynamic` — "hello dynamic arm32 / argc=1 / file=PASS / dlopen=PASS /
clock=PASS / malloc=PASS"), **CP15 barriers** (`cp15_barrier_dynamic`), the
**kuser TLS helper** (`kuser_dynamic` — `version=PASS / cmpxchg=PASS / barrier=PASS
/ get_tls=PASS`, and the TLS read is the same TPIDRURO path §2.3 covers), **zlib
with its ARMv8 CRC32 instructions** (`zlib_dynamic`:
`packed 22827 bytes, restored 262144, identical: 1`), **ASIMD narrowing** and
**Thumb TBH jump tables**, and a **benchmark that runs to completion**
(`bench_dynamic`, 1.2 s of guest work).

The full command and output for the CP15/TLS one (`[zb]` lines are the patched tree's
sysroot-provenance notes, kept here because they are part of the transcript):

```bash
$ $Z --sysroot "$SYS" $G/kuser_dynamic
[zb] sysroot has no /system/etc/ld.config.arm.txt; using the device file
[zb] sysroot has no /linkerconfig/ld.config.txt; using the device file
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
WARNING: linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
[zb] sysroot has no /system/etc/ld.config.txt; using the device file
[zb] sysroot has no /system/etc/ld.config.txt; using the device file
[zb] sysroot has no /odm/lib; using the device file
[zb] sysroot has no /vendor/lib; using the device file
[zb] sysroot has no /odm/lib/libnetd_client.so; using the device file
[zb] sysroot has no /vendor/lib/libnetd_client.so; using the device file
version=PASS
cmpxchg=PASS
barrier=PASS
get_tls=PASS
```

and the same for the sysroot's zlib, which exercises the ARMv8 CRC32 path:

```bash
$ $Z --sysroot "$SYS" $G/zlib_dynamic
packed 22827 bytes, restored 262144, identical: 1
crc32 56fbd263 adler32 76f7723c
```

### 7.3 What does not work, and why

* `signals_dynamic` aborts (`exit=134`, SIGABRT) after `mask=PASS`, `altstack=PASS`,
  `float=PASS`. Guest signal delivery is the one place where an aarch64-host
  assumption is structural — this is the most likely real porting gap and the first
  thing to look at.
* `host_call_static` exits 1 with no output: it needs a host-call registration the
  CLI does not install.
* `cxx_dynamic`, `filemap_dynamic`, `or_dlopen_dynamic` need fixtures/arguments;
  `or_dlopen_dynamic` reports `FAIL` for its probes even when given a library
  directory, so it is not merely a missing argument.
* **Native methods are not reachable** on x86_64 for the reason in §7.1 item 2.
* **The real engine was not run.** `zbrun` takes a guest *executable*; the engine is
  a shared library loaded by the Android plugin. Driving the engine off-device is
  `host/p0`'s milestone, not this one, and this document does not claim it.

### 7.4 Reporting shape

"Verified under `dh2run` and `zbrun_host` on the Linux x86_64 host; device behaviour
unverified" is the honest sentence for anything in this document. No phone was used
and no emulator was installed.

---

## 8. Why the harness single-steps, and other decisions you may want to change

1. **`Jit::Step()` per instruction, not `Jit::Run()`.** `Run()` checks a halt only
   after a whole translated block, so a breakpoint inside a block would be missed
   and `--steps N` would overshoot by up to a block. Stepping costs throughput and
   buys exactness, which is the right trade for a tool whose output is used as
   evidence. (`Jit::Step()` also ORs `HaltReason::Step` into the JIT halt word and
   leaves it set; the loop clears the reasons it owns before every step.)
2. **`check_halt_on_memory_access = true`** so the register file at a fault is
   architecturally meaningful. Correct on the x64 backend as pinned; an **arm64**
   host needs patch 0001-A first (`DYNARMIC-INDEPENDENCE.md` §2.1).
   `--imprecise-faults` turns it off.
3. **No page table, no fastmem.** Every access goes through the callbacks, which is
   what makes the region check exact.
4. **The shim never signals a coprocessor exception** (§5).
5. **One shim per coprocessor slot 0..15**, so `mcr p14, ...` is reported with the
   right number instead of reaching that assert.

---

## 9. File map

```
DH2Work-toolchain\host\harness\
  README.md                     the map, the design decisions, the limitations
  CMakeLists.txt                the two Dynarmic paths, the FindBoost decision, targets
  cmake\FindBoost.cmake         headers-only Boost shim (CMake 4 removed FindBoost)
  include\dh2h\                 guest_ram, cp15_tls, host_cpu, elf_image, hex
  src\                          their implementations + dh2run_main.cpp
  tests\selftest.cpp            44 checks
  tests\guest\*.s               the probes, one instruction sequence per file
  tools\build-wsl.sh            build + test the harness            (one command)
  tools\asm.sh                  assemble a snippet -> the bytes dh2run takes
  tools\derive-encodings.sh     re-derive every constant the selftest hard-codes
  tools\dh2_vs_oracle.py        differential vs dh2_oracle.py       (Windows Python)
  tools\zbrun-host.sh           build cli/zbrun for Linux x86_64    (one command)
  zbrun-host\CMakeLists.txt     the host build of zbcore + zbrun, without modifying the tree
  zbrun-host\thunks_host_x86_64.cpp   the x86_64 replacement for thunks.S, and why
```

To extend: add a probe to `tests/guest/`, run `tools/derive-encodings.sh`, and add
the constants and assertions to `tests/selftest.cpp`. The build output lives in
`~/dh2-harness` (harness) and `~/dh2-harness/zbrun-host` (zbrun) — never under
`/mnt/c`, because Dynarmic is ~200 compile steps and the 9p mount is slow.

**Licence.** The harness is 0BSD. Dynarmic is 0BSD. ZettaBridge source is *read*,
staged by copy, and adapted only through files that carry their own notes; per
`PORTING-POLICY.md` its notices are kept in the shipped tree, and no ZettaBridge
source is copied into `host/harness/`.
