# HOST-DLOPEN.md — the guest `dlopen`/`dlsym`/`dlclose`/`dlerror` bridge

Subsystem deliverable for the tailored host. Code: `DH2Work-toolchain/host/dl/`
(header `include/dh2dl/`, implementation `src/`, tests `tests/`, tools `tools/`).
Guest memory and the linker path are `host/mem` and `host/rt`, compiled in
**unmodified**. Host rule for bring-up: **Linux x86_64 in WSL2**, no device, no
emulator.

Result in one line: the guest's `libdl.so` exports are rewritten in place into the
host-call stubs this project already uses, so a guest `dlopen` reaches
`dh2rt::Loader`; **423 checks, 25 cases, 0 failures** on the host, and the guest
stub ABI **executes correctly on a real ARM32 core under `qemu-arm`**.

```console
$ bash host/dl/tools/build-wsl.sh
...
dltests: 423 checks, 25 cases, 0 failures
== the instruction the host writes, as the ARM assembler reads it
00000000 <stub_dlopen>:
   0:	df01      	svc	1
   2:	4770      	bx	lr
== and the bytes in the object file
 0000 01df7047                             ..pG
== running it under qemu-arm
stub_probe: dlopen index 1 -> host-encoded stub word 0x4770DF01 (svc halfword 0xDF01, bx-lr halfword 0x4770)
stub_probe: SIGILL at pc=0x1046E sp=0x408004F8  instruction=0xDF01 next=0x4770
stub_probe: PASS the installer's bytes execute as `svc #1 ; bx lr` with the stack unchanged
Test #1: dltests ..........................   Passed    6.14 sec
100% tests passed, 0 tests failed out of 1
```

---

## 1. Build and run

```bash
bash host/dl/tools/build-wsl.sh            # configure, build, run all three harnesses
# or by hand:
cmake -S host/dl -B ~/dl-build -G Ninja && cmake --build ~/dl-build
~/dl-build/dlprobe --all                   # what the bridge resolved, quotable
~/dl-build/dltests                         # the suite (also `ctest` in ~/dl-build)
qemu-arm /tmp/dh2dl-arm/stub_probe         # the ARM32 stub ABI check
```

The real inputs are CMake cache variables with working defaults
(`DH2_DL_SYSROOT`, `DH2_DL_VENDOR_LIB`, `DH2_DL_ENGINE_DIR`, and the `host/rt` and
`host/mem` source directories). Configure fails loudly rather than silently
skipping a missing `libc.so`, `libdl.so` or `libc++.so`.

---

## 2. The mechanism: replacing the guest's `dl*` exports, not the caller's PLT

### 2.1 What a guest `dlopen` call does today, measured

In the ZettaBridge sysroot the guest `libdl.so` exports are 16-byte Thumb
trampolines into the guest linker. `arm-linux-gnueabihf-objdump` on
`.../zb/sysroot/system/lib/libdl.so`:

```
00001910 <dlopen@@LIBC>:
    1910:	b580      	push	{r7, lr}
    1912:	4672      	mov	r2, lr
    1914:	f000 e8b4 	blx	1a80 <...>          @ PLT -> __loader_dlopen
    1918:	bd80      	pop	{r7, pc}
```

and the target is a trap. `ld-android.so` is a 3-segment stub whose `.text` is
**two bytes** (`fede`, a Thumb undefined instruction), and all 26 `__loader_*`
names are aliases of one address in it:

```
$ readelf -sW --dyn-syms ld-android.so | head -3
     1: 00001735     2 FUNC    GLOBAL DEFAULT    7 __loader_android_get_LD_LIBRARY_PATH
     2: 00001735     2 FUNC    GLOBAL DEFAULT    7 __loader_android_set_application_target_sdk_version
$ readelf -x .text ld-android.so
  0x00001734 fede                                       ..
$ readelf -lW ld-android.so | grep LOAD
  LOAD  0x000734 0x00001734 0x00001734 0x00002 0x00002 R E 0x1000
```

`host/rt`'s documented decision is that the guest linker's code is never entered
(docs/HOST-RUNTIME-LINKER-THREADS.md §2). So before this bridge existed, a guest
`dlopen` call pushed `lr`, loaded a garbage PC and faulted. **Nothing anywhere in
the tree implemented it**, which is what `host/rt` §7.1 lists first among its
unverified items.

### 2.2 The replacement

`dh2dl::install_dl_stubs` (`src/stubs.cpp`) finds each of the five exports the
guest can reach, matches its body against the two accepted trampoline shapes, and
overwrites it with the instruction pair the rest of this host already uses for
guest→host calls:

```
   Thumb `svc #index`   0xDF00 | index
   Thumb `bx  lr`       0x4770
```

Real run, `dlprobe --stubs`:

```
== guest libdl.so trampolines, before and after
libdl.so     dlopen               st_value 0xfd4c8911  pattern thumb-push-mov-blx-pop  writes 8 byte(s)
    before: 4672b580 e8b4f000
    after : 4770df01 00000000   (svc #(0x5a0001) ; bx lr)
    guest image reads back: 4770df01 (matches)
libdl.so     dlsym                st_value 0xfd4c8923  pattern thumb-push-mov-blx-pop  writes 16 byte(s)
    before: 4672b580 e8bcf000 b580bd80 f0004673
    after : 4770df02 00000000 00000000 00000000   (svc #(0x5a0002) ; bx lr)
libdl.so     dlclose              st_value 0xfd4c893f  pattern thumb-push-blx-pop  writes 16 byte(s)
    before: f000b580 bd80e8c6 f000b580 bd80e8ca
    after : 4770df03 00000000 00000000 00000000   (svc #(0x5a0003) ; bx lr)
libdl.so     dlerror              st_value 0xfd4c891b  pattern thumb-push-blx-pop  writes 8 byte(s)
    before: f000b580 bd80e8b8
    after : 4770df04 00000000   (svc #(0x5a0004) ; bx lr)
libdl.so     android_dlopen_ext   st_value 0xfd4c8957  pattern thumb-push-mov-blx-pop  writes 8 byte(s)
    before: 4673b580 e8d2f000
    after : 4770df05 00000000   (svc #(0x5a0005) ; bx lr)
```

Three things in that output are load-bearing and were each wrong at least once
while this was being written:

**The write span is computed, not a constant.** In this sysroot the stubs are
**10 bytes apart** (`dlopen` 0x1910, `dlerror` 0x191a, `dlsym` 0x1922). A fixed
16-byte write zeroes the first six bytes of the *next function*. The symptom is
not a crash at patch time; it is `dlerror` failing later, which is exactly the class
of failure this project refuses to leave to a device run. The installer therefore
takes the next of the module's own `dl*` exports as a hard bound (8 for `dlopen`
and `dlerror`, 16 for the rest), and the test asserts that every span stays inside
its own stub.

**`bx lr` and no push.** The original blocks pushed `{r7,lr}` and popped
`{r7,pc}`; the replacement never pushes, because the host function is entered
*instead of* the guest linker and must leave the guest stack as the ABI says it
found it. The `qemu-arm` probe asserts the stack pointer is unchanged inside the
trap.

**Rewriting the export also fixes pointers that relocation pinned.** `libc.so`
carries a 102-entry `R_ARM_ABS32` table naming its builtins (docs/HOST-RUNTIME-LINKER-THREADS.md
§2), so a `dlopen` reached from inside libc goes through a pinned absolute pointer
in libc's own data, not through libdl's PLT. Patching PLT slots would miss it;
patching the export does not. The test re-derives this from the relocated image:

```
-- libc.so's pinned ABS32 pointer to dlopen lands on the patched export
   note libc.so holds 1 pointer(s) to libdl.so's dlopen (1 in a writable segment); the
        R_ARM_ABS32 table is why they had to be fixed by rewriting the export rather
        than the PLT slot
   note first at 0x4250545660
```

### 2.3 `ld-android.so`'s `__loader_*` names are deliberately left alone

They are not trampolines; they are 26 aliases of a 2-byte address, so they cannot
hold per-function stubs. The installer recognises that and refuses:

```
-- a non-trampoline export is skipped, not overwritten
   note ld-android.so 0x1734 (.text = fede) matches no stub pattern; 0 name(s) skipped,
        0 patched
```

and the positive half of the same check, so the matcher cannot pass by rejecting
everything:

```
   note the matcher accepts the pristine trampoline from libdl.so's file
        (thumb-push-mov-blx-pop) and rejects the encoded host call now in its place
```

### 2.4 The ABI is confirmed on a real ARM32 core, not on paper

`tools/stub.S` + `tools/stub_probe.c` are built for `arm-linux-gnueabihf` and run
under `qemu-arm`. The stub body comes from `tools/stub_bytes.inc`, which
`tools/gen-stub-abi.py` generates **from `dh2dl/abi.hpp`**, so the numbers the ARM
program asserts are the ones the installer computes.

```
$ arm-linux-gnueabihf-objdump -d stub.o
00000000 <stub_dlopen>:
   0:	df01      	svc	1
   2:	4770      	bx	lr
$ arm-linux-gnueabihf-objdump -s -j .text stub.o
 0000 01df7047                             ..pG
$ qemu-arm ./stub_probe
stub_probe: dlopen index 1 -> host-encoded stub word 0x4770DF01 (svc halfword 0xDF01, bx-lr halfword 0x4770)
stub_probe: SIGILL at pc=0x1046E sp=0x408004F8  instruction=0xDF01 next=0x4770
stub_probe: PASS the installer's bytes execute as `svc #1 ; bx lr` with the stack unchanged
```

The instruction after the `svc` is checked too (`next=0x4770`), which is the
return path. Two measured corrections came out of this probe and are recorded in
the source: the trap is **SIGILL**, not SIGSYS (an earlier revision expected SIGSYS
and qemu died with "Illegal instruction"), and the saved `arm_pc` is the
instruction *after* the faulting one, so the `svc` is at `pc - 2`.

### 2.5 The encoding, and why it has an external oracle

`dh2dl::encode_arm_svc` was wrong in **seven** successive revisions while this was
being written (`<< 8`, `<< 4`, immediate fields at bits 22..19 and 15..8, and three
more). Not one produced a compiler diagnostic, and several matched index 0 while
breaking index 1. What catches all of them is a test that compares the encoder
against ZettaBridge's own shipped guest stub library, `zb/guest/lib/libGLESv2.so`,
whose first four stubs are:

```
00003270 <glActiveTexture>:
    3270:	ef5a0000 	svc	0x005a0000
    3274:	e12fff1e 	bx	lr
00003278 <glAttachShader>:
    3278:	ef5a0001 	svc	0x005a0001
```

The host's host-call channel is not a new invention: **92 GL entry points reach the
host through these very bytes**, and the bridge joins the same channel with
indices 1..5 rather than inventing a second one.

```
-- the host-call encoding, checked against ZettaBridge's shipped GL stub
   note .../zb/guest/lib/libGLESv2.so first .text bytes: 0xEF5A0000 0xE12FFF1E
   note ZettaBridge's guest GL stub library encodes its first four host calls with the
        same svc/bx-lr pair this bridge writes into libdl.so
```

**The index allocation is provisional and needs one project-wide table.** The
measured occupants (docs/HOST-ARCHITECTURE.md §1.7) are `0..578` (generated GL/EGL/
android/jnigraphics stubs), `0xFB00..0xFBFF` (JNI slot stubs), `0xFC00..0xFCFF`
(flat JNI host calls), `0xFE00..0xFEFF` (library runtime READY/PARK) and `0x5AFFFF`
(host→guest return). This bridge uses **1..5**
(`dh2dl::DlCall`: `Dlopen`=1, `Dlsym`=2, `Dlclose`=3, `Dlerror`=4,
`AndroidDlopenExt`=5) and `6..15` are reserved. `host/p0`'s Dynarmic `CallSVC`
currently reports "no guest kernel in P0" (`host/p0/src/host_cpu.cpp:117-121`);
`DlRuntime::dispatch_svc` is the piece that plugs in there, and it returns false for
an index it does not own so a caller can chain it — the same contract ZettaBridge's
handler chain uses (`core/src/library_runtime.cpp:304-311`).

---

## 3. Handles: a handle is a guest `soinfo*`, not an integer

### 3.1 The measurement that decided it

The engine's companion module reads fields of the value `dlopen` returned. From the
pristine `libStormGLOFT.so`, at `dlopen@plt` call sites 0x438d8
(`_Z20hook_inline_functionPKcib`) and 0x38054 (`_Z11PatchOpcodePKcm`):

```
   438d8:	ebffb992 	bl	31f28 <dlopen@plt>
   438dc:	e3500000 	cmp	r0, #0
   438e0:	08bd88f0 	popeq	{r4, r5, r6, r7, fp, pc}
   438e4:	e590008c 	ldr	r0, [r0, #140]      @ r0 = handle[0x8c]
   438e8:	e3500000 	cmp	r0, #0
   438ec:	0a000007 	beq	43910               @ "... is not available"
   438f0:	e0805005 	add	r5, r0, r5          @ r5 = handle[0x8c] + file offset
   4394c:	ebffbad4 	bl	324a4 <_Z10InlineHookPvPFvP7pt_regsE@plt>
```

and the same shape at 0x38060 (`ldr r0,[r0,#140]` / `add r4,r0,r4` / `SafeMemRead`
/ `SafeMemWrite`). The game adds a **file** offset to `handle[0x8c]` and then
patches that guest address, so field 0x8c must be the module's **load bias** — the
value this host calls `Module::load_bias`. An integer handle would dereference to
garbage and the engine would either fault or silently skip every hook it installs.

Four call sites call `dlopen@plt` (0x37214, 0x38054, 0x3937c, 0x438d8) and one
calls `dlsym@plt` (0x393d8); `dlclose` is imported but has no call site. `readelf
-rW` on the pristine file gives the GOT slots they reach:
`0x000dcb14 dlopen`, `0x000dcbf8 dlsym`, `0x000ddf38 dlclose`.

### 3.2 What the bridge hands back

One anonymous guest page per module, holding a `soinfo`-shaped block
(`dh2dl/abi.hpp` §4): bionic's `char name[128]` at offset 0, our magic and
generation at 0x40/0x44, and the **measured** load bias at 0x8c. Handles are
therefore guest addresses, `dlsym` validates them by re-reading the magic out of
guest memory rather than trusting the table, and an integer can never be accepted
by mistake. `dlprobe --open` shows what the guest would read:

```
SONAME in the sysroot   libz.so -> handle 0xfd3d0000  bias 0xfd3d1000  refcount 1
    soinfo[0x00..0x0c] = 7a62696c 006f732e 00000000 00000000   [0x8c] = 0xfd3d1000 (= Module::load_bias 0xfd3d1000)
guest path through the map
    /data/data/com.gameloft.android.GAND.GloftD2SS/lib/libnativeinterface.so -> handle 0xfd3ca000  bias 0xfd3cb000
    soinfo[0x00..0x0c] = 6e62696c 76697461 746e6965 61667265   [0x8c] = 0xfd3cb000
engine companion
    .../armeabi-v7a/libStormGLOFT.so -> handle 0xfd2da000  bias 0xfd2e2000
    soinfo[0x00..0x0c] = 5362696c 6d726f74 464f4c47 6f732e54   [0x8c] = 0xfd2e2000
open twice: 0xfd3d0000 and 0xfd3d0000 -- the same soinfo, refcounted
```

The soname reads correctly out of the guest image (`7a62696c 006f732e` is
`"libz.so"`). An earlier revision put the magic at offset 4 and turned that into
`"libzH2LD"`; the test caught it by reading the guest image instead of believing
the handle table.

**The engine's own arithmetic is reproduced.** `test_soinfo_offsets_survive_the_patch`
does what 0x438f0 does — `handle[0x8c] + <file offset>` — and asserts the result is
executable guest memory inside `libStormGLOFT.so`:

```
   note handle[0x8c] + 0x35df0 = 0x...  is executable guest memory inside libStormGLOFT.so
```

---

## 4. Symbols: scope, visibility and the `libdl.so` fact

### 4.1 The scope model

`DlRuntime` keeps the authoritative group model in `dh2dl/namespace.hpp` rather
than calling `dh2rt::Loader::lookup_from_handle`, because the loader's global scope
is fixed at load time (docs/HOST-RUNTIME-LINKER-THREADS.md §7.9) while
`RTLD_GLOBAL` genuinely changes it. Defining `RTLD_GLOBAL` as *"append to the
global group"* and re-binding is both correct and cheap, so it is what the bridge
does — the loader is used for per-object symbol *lookup*, which it does well (hash
tables, export rule), and not for scope.

`dlsym(handle, name)` searches the handle's own group first (the object, then its
`DT_NEEDED` breadth-first), then the global group; `RTLD_DEFAULT` searches the
global group; namespace visibility filters both.

### 4.2 The `libdl.so` fact, re-derived rather than assumed

`libc.so` does **not** export `dlsym` — its own table has an undefined `dlsym` and
its 1 936-name `.dynsym` exports none. The sysroot's export census:

```
libdl.so   exports: dlopen  dlsym  dlclose  dlerror  android_dlopen_ext  dlvsym
                    dladdr  dl_iterate_phdr  android_get_LD_LIBRARY_PATH
ld-android.so exports: __loader_dlopen  __loader_dlsym  __loader_dlclose
                    __loader_dlerror  __loader_android_dlopen_ext  (26 names)
every other object exports: none of them
```

All of `libdl.so`'s `dl*` exports are **WEAK** (`readelf -sW`: `WEAK DEFAULT`), so
the export rule cannot mean "global binding only" — `dh2rt::symbol_is_export`
requires defined, non-local, `STV_DEFAULT` and an address-carrying type, and weak
satisfies "non-local". The tests re-derive the rule from the files:

```
-- visibility: libc.so does not export dlsym, libdl.so does
   note dlsym is exported by libdl.so at 0x4249651491 and by no other object the closure loads
   note __loader_dlsym is at 0x4250699573, a different object from the export the guest calls
-- an absent symbol returns null with a non-empty dlerror
   note dlerror: dlsym("libz.so", "this_symbol_does_not_exist_in_libz"): symbol not found
   note guest dlerror buffer 0x4248629248 holds "dlsym("libz.so", "no_such_symbol_in_libz") ..."
```

and every resolved address is compared against a **direct scan of that library's
own `.dynsym`**, recomputed by the test from the file:

```
-- dlsym address == a direct scan of that library's dynsym
   note inflate in libz.so -> 0x4248702657 (dynsym st_value 0x65217, bind 1)
   note deflate in libz.so -> 0x4248670569 (dynsym st_value 0x33129, bind 1)
   note crc32   in libz.so -> 0x4248665773 (dynsym st_value 0x28333, bind 1)
   note malloc  in libc.so -> 0x4250109481 (dynsym st_value 0x255529, bind 1)
   note dlopen  in libdl.so -> 0x4249651473 (dynsym st_value 0x6417, bind 2)
   note dlsym   in libdl.so -> 0x4249651491 (dynsym st_value 0x6435, bind 2)
   note dlclose in libdl.so -> 0x4249651519 (dynsym st_value 0x6463, bind 2)
   note dlerror in libdl.so -> 0x4249651483 (dynsym st_value 0x6427, bind 2)
```

`inflate` resolving into **libz.so** through the handle, while `RTLD_DEFAULT
inflate` resolves into the **main object**, is the same scope order `host/rt`'s
`rtprobe` reports, and it is the reason a handle-based lookup and a global lookup
are different operations:

```
RTLD_DEFAULT   inflate -> 0xfdc3cf48
   note RTLD_DEFAULT inflate -> 0x4257468232 (the main object, not libz.so: the engine
        links zlib statically)
RTLD_NEXT      inflate -> bad state: dlsym(RTLD_NEXT) needs the caller's guest PC;
                          not modelled without the JIT
```

---

## 5. Names, paths and namespaces

### 5.1 Path translation, reproduced from the host that already did it

ZettaBridge never hands a guest path to the host filesystem: it keeps a prefix map
(`core/src/process.cpp:89-105`), longest-prefix aliases (`:247-257`) and a
translate-with-fallback (`:259-297`). `host/rt` inherited only the *search
directories*, not the translation, which is sufficient for `DT_NEEDED` (always a
bare soname) and insufficient for `dlopen` — measured: `libStormGLOFT.so` embeds
`/data/data/com.gameloft.android.GAND.GloftD2SS/lib/libStormGLOFT.so` and
`.../libDungeonHunter2.so` and passes them to `dlopen@plt`. `dh2dl::PathMap`
reproduces the map, with one deliberate difference: ZettaBridge falls through to
the *device* file when the sysroot lacks the path (correct for fonts and timezone
data, `:281-292`), while for a **loader** a miss must become a reported `dlerror`.
It is therefore a switch, off by default.

```
-- dlopen a /data/data/... guest path through the path map
   note /data/data/com.gameloft.android.GAND.GloftD2SS/lib/libnativeinterface.so ->
        .../original/lib/armeabi-v7a/libnativeinterface.so
-- dlopen by path rather than by SONAME
   note path .../lib/libz.so -> the same object as the SONAME, one handle, resolved path intact
   note a missing guest path: i/o error: cannot open /data/data/com.example/lib/libnope.so
        (tried .../armeabi-v7a/com.example/lib/libnope.so .../guest/lib/libnope.so
         .../sysroot/system/lib/libnope.so .../system/bin/libnope.so .../armeabi-v7a/libnope.so)
```

The dlopen search order is the loader's own list, so `dlopen` and `DT_NEEDED`
cannot disagree about which `libz.so` they mean:

```
== the guest search order dlopen uses
    .../zb/guest/lib
    .../zb/sysroot/system/lib
    .../zb/sysroot/system/bin
    .../original/lib/armeabi-v7a
```

`LD_LIBRARY_PATH` is honoured ahead of that list, split on `:`, with empty entries
**dropped rather than expanded to the current directory** — silently expanding `""`
is a real loader bug (`dlopen("libc.so")` in a build tree).

### 5.2 Namespaces

`android_create_namespace` is modelled to the depth this game can exercise
(`dh2dl/namespace.hpp`): a name, an ordered search list, `search_default`, a
`visible` flag and `isolated`, plus `android_link_namespaces` edges and bionic's
visibility rule (same namespace, or an explicit link, or a visible non-isolated
provider). The guest handle is a real guest page holding a magic and the id. The
test drives all of it:

```
-- namespace search order decides which copy of a shadowed name is found
   note dlopen search order: .../guest/lib : .../sysroot/system/lib : .../armeabi-v7a
   note created namespace 1, guest handle 0x00000000
   visible(default -> plugins) = 0, visible(plugins -> default) = 1
```

`android_dlopen_ext` reads the struct at its declared 32-bit offsets from guest
memory, reflects the honoured flags back into the caller's `flags` word exactly as
bionic does, and refuses the two cases it cannot honour **with the reason
attached** rather than silently searching a path:

```
library_path=/data/data/.../libnativeinterface.so flags=0x20 -> handle 0xfd3ca000
ANDROID_DLEXT_USE_LIBRARY_FD -> unsupported relocation: ANDROID_DLEXT_USE_LIBRARY_FD needs
    fd-based loading, which dh2rt::Loader does not model
ANDROID_DLEXT_USE_NAMESPACE -> bad state: ANDROID_DLEXT_USE_NAMESPACE is modelled only as a
    search order; dlopen through a namespace handle is not wired to dh2rt::Loader
```

---

## 6. A newly loaded module goes through the packed decoders

`libc++.so` carries Android's PACKED relocation tables (`DT_ANDROID_REL` APS2 and
`DT_ANDROID_RELR`) and is load-bearing: `liblog.so` imports 13 symbols from it. The
bridge reaches it through `dh2rt::Loader::load_object` + `load_dependencies`, so a
late module's closure is decoded by the same code the initial load uses.

The test makes that concrete rather than asserting it: it builds a loader whose
search order cannot satisfy `liblog.so`'s need for `libc++.so` at start-up, proves
the premise, then widens the order and loads again so the object goes through
`load_object` for the first time.

```
-- a dlopen that pulls libc++.so in goes through the packed decoders
   note the premise: library not found: cannot find libc++.so (needed by liblog.so) in 2 search directories
   note libc++.so packed tables: DT_ANDROID_REL 7072 bytes, 2645 packed relocations applied
   note libc++.so census: R_ARM_ABS32=1921 R_ARM_TLS_DTPMOD32=1 R_ARM_GLOB_DAT=190
        R_ARM_JUMP_SLOT=420 R_ARM_RELATIVE=533
```

2 645 = 2 112 + 533, matching the independent decoder in
`host/rt/tools/packed_relocs.py` and docs/HOST-RUNTIME-LINKER-THREADS.md §3.3
exactly. Loading a module at run time, `dlprobe --late` on a fresh process:

```
== a module loaded at run time
modules 10 -> 14, relocations applied 59623 -> 67301
handle 0xfd2e1000, libStormGLOFT.so, 5 initialiser(s) recorded, 5 not run
    DT_INIT_ARRAY entry -> 0xfd333e00 (guest)
    DT_INIT_ARRAY entry -> 0xfd333e39 (guest)
    DT_INIT_ARRAY entry -> 0xfd333e61 (guest)
    DT_INIT_ARRAY entry -> 0xfd335045 (guest)
    DT_INIT_ARRAY entry -> 0xfd3350d9 (guest)
```

Four modules arrived (libStormGLOFT, libz, libEGL, libandroid) and 7 678
relocations were applied for that one call.

---

## 7. Where every caller comes from

### 7.1 The guest's own call sites, from the pristine binaries

| Object | Import | Address | Call sites | Evidence |
| --- | --- | --- | --- | --- |
| `libStormGLOFT.so` | `dlopen@LIBC` | `st_value 0x1911` | **0x37214, 0x38054, 0x3937c, 0x438d8** | `readelf -rW` GOT 0x000dcb14; `bl 31f28 <dlopen@plt>` at each of the four; `arm-linux-gnueabihf-objdump` |
| `libStormGLOFT.so` | `dlsym@LIBC` | `st_value 0x1923` | **0x393d8** | GOT 0x000dcbf8; `bl 321d4 <dlsym@plt>` |
| `libStormGLOFT.so` | `dlclose@LIBC` | `st_value 0x193f` | none | GOT 0x000ddf38; the import has no call site |
| `libStormGLOFT.so` | `dlerror` | not imported | — | the guest calls it only after a failed `dlsym`; the bridge still provides it |
| `libc.so` | `dlopen`, `dlsym`, `dlclose`, `dlerror`, `android_dlopen_ext`, `dladdr` | — | through the pinned `R_ARM_ABS32` builtin table (102 entries) | `readelf -sW --dyn-syms libc.so`; one pinned pointer to `dlopen` found in the relocated image |
| `libDungeonHunter2.so`, `libnativeinterface.so`, all vendor stubs, all other sysroot objects | none | — | — | `readelf -sW --dyn-syms` over the whole closure |

`docs/HOST-IMPORT-SURFACE.md:1063` already recorded this as the corpus's only
dynamic-lookup surface: *"The only dynamic lookup surface in the whole corpus is
`libStormGLOFT.so`'s three imports (`dlopen`, `dlsym`, `dlclose`) — worth tracing at
runtime once"*. The callers are now identified, and the task's warning is
vindicated: 0x38054 and 0x438d8 **dereference the handle**, so the handle had to be
a `soinfo*`.

### 7.2 What the bridge binds, measured after the fact

```
== the engine's own dl* call sites, and what they now reach
dlopen     GOT 0xfd3beb14 = 0xfd4c8911  (libdl.so export 0xfd4c8911, +0x001911)  callers 0x37214 0x38054 0x3937c 0x438d8
           the export's first 4 bytes: 4770df01 -> a bridge host-call stub
dlsym      GOT 0xfd3bebf8 = 0xfd4c8923  (libdl.so export 0xfd4c8923, +0x001923)  callers 0x393d8
           the export's first 4 bytes: 4770df02 -> a bridge host-call stub
dlclose    GOT 0xfd3bff38 = 0xfd4c893f  (libdl.so export 0xfd4c893f, +0x00193f)  callers (no call site in this object)
           the export's first 4 bytes: 4770df03 -> a bridge host-call stub
```

That is the whole chain: the engine's four `dlopen` call sites and its one `dlsym`
call site reach a host-call stub, and behind it is `dh2rt::Loader`.

### 7.3 The bridge's own pieces, with their sources

| File | What it is | Adapted from |
| --- | --- | --- |
| `include/dh2dl/abi.hpp`, `src/abi.cpp` | the SVC host-call ABI, the `dl*` index block, the `soinfo` layout, the `android_dlopen_ext_args` offsets | ZettaBridge `core/src/process.cpp:67-87,514-568` (`kHostCallBase`, the stop/dispatch shape) and `guest/stubs/gen/libGLESv2.S:9-12` (the stub body) |
| `include/dh2dl/pathmap.hpp`, `src/pathmap.cpp` | guest→host path translation and the dlopen search order | ZettaBridge `core/src/process.cpp:89-105,247-297` |
| `include/dh2dl/namespace.hpp`, `src/namespace.cpp` | the namespace model: search order, `search_default`, visibility, links | bionic `linker_namespace.cpp` semantics; the shape follows ZettaBridge's separation of the bionic stack from the app-private directory (`core/src/process.cpp:120-296`) |
| `include/dh2dl/stubs.hpp`, `src/stubs.cpp` | the trampoline matcher and the in-place export replacement | original; the instruction pair is the project's existing host-call channel |
| `include/dh2dl/dl.hpp`, `src/dl.cpp` | `dlopen`/`dlsym`/`dlclose`/`dlerror`/`android_dlopen_ext` over `dh2rt::Loader`, the group model, the handle table | ZettaBridge `core/src/library_runtime.cpp:131-157` (the `dlopen`/`dlsym`/`dlerror` call sequence and its error reporting) |
| `src/dlprobe.cpp` | the CLI whose output this document quotes | `host/rt/src/rtprobe.cpp`'s shape |
| `tests/` | `harness`, `t_open`, `t_symbol`, `t_stubs`, `t_late`, `main` | `host/rt/tests/`'s harness and fixture shape |
| `tools/stub.S`, `tools/stub_probe.c`, `tools/gen-stub-abi.py`, `tools/build-wsl.sh` | the `qemu-arm` ABI check and the one-command build | original; the harness choice is docs/TEST-ENVIRONMENT-POLICY.md §3 |

---

## 8. Test results

`~/dl-build/dltests` — **423 checks, 25 cases, 0 failures**, 3.6 s wall clock
(`ctest`: `1/1 Passed`, reported 6.1 s including its own startup).

| Case | Evidence | Independence |
| --- | --- | --- |
| `dlopen` by SONAME | non-null guest handle, `soinfo` magic and soname read back out of the guest image | the test reads guest memory, not the handle table |
| `dlopen` twice | equal handles, refcount 2, both resolve a symbol | bionic returns one `soinfo*` per object |
| `dlopen` by path | resolved path equals the path asked for, same object | — |
| `dlopen` a `/data/data/...` guest path | maps to the engine directory through the prefix map | the path is the one the engine's own strings contain |
| `dlopen` the module with the call sites | `libStormGLOFT.so` loads, 5 initialisers recorded | its GOT then provably points at the patched exports |
| `RTLD_NOLOAD` | present succeeds, absent fails with a reason | — |
| `dlopen(NULL)` | names the main object | — |
| `dlsym` address | equals `owner_bias + st_value` from a direct `.dynsym` walk by the test | the test re-implements the walk from the file |
| visibility | `libc.so` exports no `dlsym`; `libdl.so` does; `__loader_dlsym` is a different object | re-derived from the files |
| absent symbol | null, non-empty `dlerror`, and the message readable at the guest buffer address the dispatcher returns | the buffer is read out of guest memory |
| invalid handle | rejected; a handle whose magic is scribbled over is rejected too, and works again once restored | — |
| `RTLD_DEFAULT` / `RTLD_NEXT` | default resolves to the main object; next is refused with a named reason | — |
| pinned ABS32 pointers | a pointer to `dlopen` exists in `libc.so`'s writable image and lands on the patched export | found by scanning the relocated image |
| the encoding | the encoder reproduces `libGLESv2.so`'s own first four stub words; ARM and Thumb round-trip; the two forms are different words | external oracle (the vendor stub library) |
| the stubs | 5 exports patched, byte-for-byte in the guest image, page left executable and **not** writable, every write span inside its own stub, 0 skipped | guest image, not the installer's record |
| non-trampolines | `ld-android.so`'s 2-byte `.text` matches no pattern; the pristine trampoline from the file does | both directions |
| the engine's call sites | the three GOT slots point at the patched exports | `readelf -rW` addresses from the pristine file |
| `handle[0x8c]` | equals `Module::load_bias`, and `+0x35df0` is executable guest memory | reproduces the engine's own arithmetic |
| the dispatcher | owns 1..5, refuses 259 / 0xFC00 / 0xFE00, and performs a real `dlopen`/`dlsym`/`dlerror` through the guest ABI | — |
| late load | modules and relocations grow; the new module's census is the measured one | per-module counters recomputed from the module |
| packed decoders | `libc++.so` reaches the late path and its packed census matches the measured numbers | `packed_relocations()` is decoded and counted independently |
| namespaces | search order, `search_default`, visibility before and after `link` | — |
| `android_dlopen_ext` | struct offsets round-trip; modelled case loads; fd and namespace cases are refused with reasons | — |
| `dlclose` | decrements, does not unload, refuses an invalid handle | the non-feature is asserted as behaviour |
| the ARM32 ABI | `qemu-arm`: `svc #1` executes, the next halfword is `bx lr`, the stack is unchanged | a real ARM core, and the numbers come from `dh2dl/abi.hpp` |

Reproduce:

```bash
bash host/dl/tools/build-wsl.sh
~/dl-build/dlprobe --stubs      # the export replacement, before and after
~/dl-build/dlprobe --open       # handles, twice, by path, by guest path, absent
~/dl-build/dlprobe --symbols    # scope, the libdl.so fact, a miss, RTLD_DEFAULT/NEXT
~/dl-build/dlprobe --late       # a runtime module, its census, its closure
~/dl-build/dlprobe --ext        # android_dlopen_ext and namespaces
python3 host/dl/tools/gen-stub-abi.py --emit-inc   # the ABI the qemu-arm probe uses
```

---

## 9. What is not modelled

Stated plainly, because each of these is a place a device run can still fail.

1. **No guest instruction executes in the host suite, and no Dynarmic is linked.**
   `host/dl` has no JIT. The dispatcher is exercised at the ABI level (host C calls
   `dispatch_svc` with the guest's argument registers, and the string/struct reads
   go through the real guest address space), and the instruction encoding is
   exercised on a real ARM32 core under `qemu-arm`. What is *not* executed is the
   step in between: a guest `svc` stopped by Dynarmic and routed to
   `dispatch_svc`. `host/p0/src/host_cpu.cpp:117-121` is where that call has to be
   added; `host/dl` cannot add it because `host/p0` is outside this deliverable's
   write scope.
2. **`dlclose` does not unload.** It decrements a reference count and returns 0.
   Unloading would need guest thread TLS teardown, `.fini_array`, the ARM unwind
   tables the engine reads out of the loaded image, and the pinned `R_ARM_ABS32`
   pointers in `libc.so` that name symbols in other modules. **Reference counting is
   therefore present; unloading is deliberately omitted, and this is an explicit
   omission rather than an oversight.** The engine imports `dlclose` and has no call
   site for it.
3. **Initialisers are recorded, not run.** `DT_INIT` and `DT_INIT_ARRAY` are parsed
   into guest addresses per module (`DlModule::init_functions`) and reported as
   `init_pending`; `DlRuntime::set_init_runner` is the seam through which the JIT
   layer runs them. Measured on the modules this game asks for:
   `libStormGLOFT.so` has 5 (24 bytes of `.init_array`, one slot zero),
   `libc++.so` has 1, `libnativeinterface.so` / `libGLES*` / `libEGL` / `libz.so`
   have none. Running a guest initialiser before `libc` is initialised would be
   worse than not running it, so the order is the JIT layer's problem and is
   documented rather than guessed at.
4. **`RTLD_NEXT` is refused, not guessed.** It needs the calling object, which is
   the guest's return address on the guest stack. Nothing in this corpus imports it.
5. **`dlvsym` is not implemented.** This guest has no version scripts to honour, and
   returning an unversioned symbol would be a false answer. `libdl.so` exports it
   and no object in the closure imports it.
6. **`ANDROID_DLEXT_USE_LIBRARY_FD` is refused.** `dh2rt::Loader` reads paths, not
   descriptors; loading an APK's uncompressed `.so` through an fd needs the loader
   to change, not this bridge. The only object that imports `android_dlopen_ext` is
   `libc.so` (its own `__libc_dlopen` path); no engine or vendor object does, so
   this is a refusal with a name rather than a gap in a reachable path.
7. **`ANDROID_DLEXT_USE_NAMESPACE` is refused.** Namespaces are modelled as a
   search order plus a visibility graph; the wire from a guest
   `android_namespace_t*` to `dh2rt::Loader`'s search list is not built, and
   accepting the pointer while ignoring it would be a lie about what was searched.
   Same importer census as above: `libc.so` only.
8. **`dladdr`, `dl_iterate_phdr`, `dl_unwind_find_exidx` and
   `android_get/update_LD_LIBRARY_PATH` are not implemented.** `libc.so` imports
   `dladdr`; nothing in the closure imports the others. `dladdr` is the one that
   matters to the engine's exception handling and is the next most valuable item.
9. **RTLD_GLOBAL promotion re-binds eagerly, and the re-bind is not proven to be
   observable.** `dlopen(..., RTLD_GLOBAL)` appends to the global group and re-runs
   `Loader::relocate_module` over the already-relocated objects so that a symbol
   which was unresolved before the promotion now resolves. It is idempotent for
   `REL` semantics, but nothing in the corpus calls `dlopen` with `RTLD_GLOBAL`
   (`libStormGLOFT.so` passes `1`, `RTLD_LAZY`), so this path is exercised only by
   unit checks on the group model, not by the engine.
10. **Static TLS is recomputed across the whole load order on every `dlopen`.**
    That is what `host/rt` does at start-up and it is correct for modules without
    `PT_TLS`. Measured: none of the modules this game's `dlopen` surface reaches has
    `PT_TLS` (`libStormGLOFT.so`, `libnativeinterface.so`, `libz.so`,
    `libGLESv2.so`, `libEGL.so`, `libandroid.so` all report `TLS` count 0). If a
    future `dlopen` pulls in a module *with* `PT_TLS`, the offsets of every module
    above it shift and existing guest threads' blocks — which `host/rt` sized at
    thread creation — would no longer match. That is a real hazard, it is not
    reachable today, and it is recorded here rather than fixed because the fix
    belongs in `host/rt`'s TLS layer.
11. **The first two bytes of a patched stub's alignment padding are zeroed only
    when the bound allows it.** `dlerror` and `android_dlopen_ext` in this sysroot
    have 8 bytes of span with no padding, so bytes 8..15 keep whatever the compiler
    emitted. Nothing branches there — the replacement is `svc ; bx lr` and the
    block's original `pop {r7,pc}` is gone — but the bytes are not cleaned.
12. **`dlerror`'s buffer is one guest page per *host thread*, never freed.** It is
    allocated on first use with `map_anon_anywhere` (a page, of which 512 bytes are
    used) and lives for the life of the thread. A host that creates and destroys
    many guest threads leaks a page each; this game creates few.
13. **A vaddr→file-offset conversion has to test `file_off + (vaddr - s.vaddr + len) <=
    s.filesz` per segment, in the right order.** `dh2rt::Segment` keeps link-time
    `vaddr`/`file_off` and adds placement-only `map_start`/`map_len`
    (`host/rt/include/dh2rt/module.hpp:26-46`), so the conversion is legitimate —
    but libdl.so's first `PT_LOAD` covers vaddr `0x0..0x908` and its second covers
    `0x1908..0x1b40`, so a check written as `vaddr >= s.vaddr && vaddr + len <=
    s.vaddr + s.filesz` finds the right segment only when it does not stop at the
    first match by accident. An earlier revision of
    `test_skipped_and_unrecognised` got this wrong, landed on the wrong bytes, and
    "passed" the matcher on a neighbour's trampoline; the shipped check reconstructs
    the pristine bytes from the pre-patch records instead and compares them to the
    file's own bytes.

---

## 10. Not verified without a device

1. **No guest code has run against this bridge.** The highest-value next step is
   three lines in `host/p0`: route Dynarmic's `CallSVC` into
   `DlRuntime::dispatch_svc` and let the engine's 0x438d8 site run. Everything
   downstream of that is already tested; the SVC delivery itself is not.
2. **The engine's runtime `dlopen` argument is still unknown.** The four call sites
   are identified and their GOT slots are provably bound, but the *name* the engine
   passes is computed at run time (`dlsym` at 0x393d8 is called with the handle from
   the `dlopen` at 0x3937c, and `docs/HOST-IMPORT-SURFACE.md:1064` already flags that
   "a `dlsym` for a symbol name built at runtime would not appear in any of the
   analysis above"). The bridge resolves any name the search order can find, so the
   remaining risk is a name that is *not* a file — e.g. a symbol the engine expects
   `libDungeonHunter2.so` to export for itself.
3. **`handle[0x8c]` is inferred from two read sites, not from bionic's header
   revision.** Both sites add a file offset to it and then read or patch that guest
   address, which is unambiguous about *what the value is* and says nothing about
   which bionic field it is on Android 14. If a third site reads a different offset
   of the handle, it will read zeros.
4. **Device placement differs.** Every address in this document comes from this
   build's deterministic highest-fit placement below `0xfe000000`; the device's own
   address-space use shifts all of them. Only the *relationships* (handle 0x8c ==
   bias, GOT slot == export address) are claimed.
5. **Nothing here has been run on the phone**, and per
   docs/TEST-ENVIRONMENT-POLICY.md it should not be: the phone is the Lead's, and
   the ANR behaviour recorded in the project status makes an unplanned install
   actively harmful.

---

## 11. Files

| Path | What it is |
| --- | --- |
| `host/dl/include/dh2dl/abi.hpp` | the SVC host-call ABI, the index block, the `soinfo` layout, the `android_dlopen_ext_args` offsets |
| `host/dl/include/dh2dl/pathmap.hpp`, `src/pathmap.cpp` | guest→host path translation and the dlopen search order |
| `host/dl/include/dh2dl/namespace.hpp`, `src/namespace.cpp` | the namespace model |
| `host/dl/include/dh2dl/stubs.hpp`, `src/stubs.cpp` | the trampoline matcher and the export replacement |
| `host/dl/include/dh2dl/dl.hpp`, `src/dl.cpp` | the five entry points over `dh2rt::Loader`, the group model, the handle table, the SVC dispatcher |
| `host/dl/src/abi.cpp` | the `android_dlopen_ext_args` layout in one place |
| `host/dl/src/dlprobe.cpp` | the CLI whose output this document quotes |
| `host/dl/tests/` | `harness`, `t_open`, `t_symbol`, `t_stubs`, `t_late`, `main` |
| `host/dl/tools/build-wsl.sh` | configure, build, run all three harnesses |
| `host/dl/tools/stub.S`, `stub_probe.c`, `stub_bytes.inc`, `stub_abi.h`, `gen-stub-abi.py` | the `qemu-arm` ARM32 ABI check and the generator that keeps it in step with the header |
| `host/dl/CMakeLists.txt` | builds `dlcore` (with `host/rt` and `host/mem` compiled in unmodified), `dlprobe`, `dltests` and `ctest` |

Nothing under `host/mem`, `host/loader`, `host/rt`, `host/p0`, `host/jni`, the rest
of `DH2Work`, `DH_sc`, `DH_sc-pr` or `DH2Work-stage` was modified. `host/loader` is
not used at all: `host/rt` carries the relocation types this bridge needs, and
`host/loader`'s interface is settled and deliberately left alone.
