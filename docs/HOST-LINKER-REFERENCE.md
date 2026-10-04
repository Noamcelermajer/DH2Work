# HOST-LINKER-REFERENCE.md — the guest dynamic linker, implementation-ready

Scope: the mechanism a host-side ARM32 Android dynamic linker must reproduce so the DH2
engine's <code>DT_NEEDED</code> closure loads, resolves, relocates and starts. This document
is the extraction, not a re-derivation: every claim carries a <code>file:line</code> citation
and a source tag. Where a mechanism could not be confirmed from the sources, it is marked
**UNVERIFIED** rather than smoothed over.

## 0. Citation legend, and the two designs this document reconciles

| Tag | Source | What it is |
| --- | --- | --- |
| **[ZB]** | <code>~/zb-src/...</code> (this machine, read-only) | ZettaBridge reference checkout. Runs the **real guest bionic linker**. |
| **[P]** | <code>docs/HOST-*.md</code> (measured findings from the earlier private tree <code>DH2Work-toolchain/host/...</code>) | The **emulated** linker. This is the design this spec targets. Where [P] cites private-tree code (<code>host/loader/...</code>, <code>host/rt/...</code>), the citation is reproduced from the doc, not re-read. |
| **[R]** | <code>host/...</code> (current repo, owned by another workstream) | Current in-repo state, cited so the implementer knows what already exists. |
| **UNVERIFIED** | — | Not confirmable from any source I could read. |

**The two designs, and why both appear below.** [ZB] does not implement a linker. Its
<code>Process::run</code> loads the main object and its <code>PT_INTERP</code> and starts at
the interpreter's entry; the guest's own bionic <code>linker</code> then walks
<code>DT_NEEDED</code>, resolves and relocates
(<code>~/zb-src/core/src/process.cpp:480-504</code>). Its <code>__loader_*</code> exports are
the real linker's (verified: <code>readelf -sW ~/zb-src/sysroot/system/bin/linker</code> shows
<code>__loader_shared_globals</code> at <code>0x42f19</code>, <code>__loader_dlopen</code> at
<code>0x428dd</code>). [P]/[R] instead **emulate** the linker in the host and never run guest
linker code (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:30-65</code>). The mechanisms below are
stated for the emulation, with the [ZB] counterpart cited where the reference is the only
prior art.

Notices: ZettaBridge is cumulative PolyForm Noncommercial 1.0.0 + PolyForm Perimeter 1.0.1
(<code>~/zb-src/LICENSE:1-15</code>); copying is permitted for this noncommercial project and
copied mechanisms must keep their notices (<code>docs/PORTING-POLICY.md:7-9,31-34</code>).
This document copies no source verbatim beyond short excerpts.

---

## 1. Module model

### 1.1 What a loaded module must carry

[ZB]'s host record is deliberately minimal — it is not a <code>soinfo</code>.
<code>LoadedElf</code> (<code>~/zb-src/core/include/zb/elf_loader.h:10-19</code>) holds only:
<code>bias</code>, <code>entry</code> (bit 0 = Thumb), <code>phdr</code>, <code>phnum</code>,
<code>load_start</code>, <code>load_end</code>, <code>is_dyn</code>, <code>interp</code>.
Everything a linker needs (dynamic table, symbol table, dependency list, relocation tables,
TLS) is the **guest** linker's state in [ZB], not the host's.

The emulation must therefore carry the full record. Working shape:
<code>LoadedImage</code> (<code>host/src/linker.hpp:13-43</code>) — bias, entry, phdr/phnum/phent,
load_start/end, <code>is_dyn</code>, <code>soname</code>, <code>needed</code>, dynamic table
address/size, <code>dt_strtab/strsz</code>, <code>dt_symtab/syment/nsyms</code>,
<code>dt_hash/gnu_hash</code>, <code>dt_rel/relsz/relent</code>,
<code>dt_jmprel/pltrelsz</code>, PT_TLS, init/fini arrays — plus a refcount/group for
<code>dlopen</code> handles and <code>DT_SYMBOLIC</code>/<code>BIND_NOW</code> flags
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:371-375,482-486</code>). The private tree's
<code>Module</code> keeps both coordinate systems explicitly (<code>Segment.vaddr</code>
link-time, <code>map_start/map_len</code> guest;
<code>docs/HOST-ELF-LOADER.md:152-179</code>) — do the same, because mixing them was three
separate bugs (<code>docs/HOST-ELF-LOADER.md:167-175</code>).

### 1.2 ET_DYN load bias and segment layout

Algorithm, page-granular [ZB] <code>load_elf</code>
(<code>~/zb-src/core/src/elf_loader.cpp:46-123</code>):

1. Walk <code>PT_LOAD</code> only. <code>lo = min(page_round_down(p_vaddr))</code>,
   <code>hi = max(page_round_up(p_vaddr + p_memsz))</code>; <code>span = hi - lo</code>
   (<code>:46-61</code>).
2. **ET_DYN:** <code>at = mem.find_free(span, dyn_limit)</code> — highest free run ending at
   or below the limit, never below <code>0x10000</code> (<code>:63-70</code>;
   <code>find_free</code> semantics <code>~/zb-src/core/include/zb/guest_memory.h:60-61</code>);
   <code>bias = at - lo</code> (<code>:70</code>). Guest address of any link-time value
   <code>v</code> is <code>v + bias</code>, and a segment maps at
   <code>page_round_down(p_vaddr + bias)</code>.
3. **ET_EXEC:** placed at its own addresses; refuse if the range is not free
   (<code>:71-74</code>).
4. Map the whole span anonymous <code>PROT_READ|PROT_WRITE</code> (<code>:77-80</code>), copy
   each <code>PT_LOAD</code>'s <code>p_filesz</code> bytes (<code>:83-85</code>), then apply
   per-segment protections from <code>p_flags</code> (<code>:95-102</code>). The anon map is
   why <code>.bss</code> and page tails are already zero.
5. <code>PT_PHDR</code> if present, else derive <code>phdr</code> from the
   <code>PT_LOAD</code> whose file range contains <code>e_phoff</code>
   (<code>:104-114</code>); <code>entry = e_entry + bias</code> (<code>:117</code>).

Limits: main executable at <code>kExecutableLimit = 0x40000000</code>, every other
<code>ET_DYN</code> below <code>kMmapLimit = 0xFE000000</code>
(<code>~/zb-src/core/include/zb/process.h:31-36</code>;
<code>~/zb-src/core/src/process.cpp:482,493</code>). The emulation adds stricter validation
worth keeping: <code>max_align</code> from <code>p_align</code>, span measured **from the
page-aligned start** (measuring from <code>p_vaddr</code> shifts the bias by the page offset
and places modules inside the stack), <code>p_offset % 4096 == p_vaddr % 4096</code>
congruence, and the two-phase "map RW then copy then narrow" order
(<code>docs/HOST-ELF-LOADER.md:109-148,133-137</code>). A <code>PT_LOAD</code> without
<code>PF_R</code> is a hard error (<code>docs/HOST-ELF-LOADER.md:639-640</code>). The
protection narrowing is real host <code>mprotect</code>; a write to guest text must fault
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:310,343</code>).

### 1.3 Dependency order, loading, and cycles

**Load order = global-scope order = TLS module-id order.** In the emulation: main object then
interpreter object then <code>DT_NEEDED</code> **breadth-first**, per module in table order
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:92-96</code>;
<code>docs/HOST-ELF-LOADER.md:318-322</code>; implemented as a growing-index BFS in
<code>host/src/linker.cpp:277-289</code>). TLS module ids are 1-based in that same order
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:236-237</code>). Do not relocate anything until the
whole closure is mapped — an earlier draft wrote the initial stack first and every auxv entry
was zero because <code>AT_ENTRY/AT_BASE/AT_PHDR</code> were not yet known
(<code>docs/HOST-ELF-LOADER.md:88-105</code>).

<code>DT_NEEDED</code> names are matched by **basename**; a search path is tried in order,
first match wins (<code>host/src/linker.cpp:16-19,30-38</code>). [ZB]'s host does not do this
traversal at all — the guest linker does
(<code>~/zb-src/core/src/process.cpp:490-504</code>); [ZB] only rewrites absolute
<code>DT_NEEDED</code> values to their basename at import time so a modern bionic linker will
accept them (<code>~/zb-src/core/src/elf_fixups.cpp:220-241</code>).

**Cycles.** [ZB] has no host-side cycle handling because it has no host-side traversal. The
emulation's only cycle guard is deduplication by soname/basename before descending
(<code>host/src/linker.cpp:45-46,88,281</code>). Real linker cycle semantics — insert the
module into the global list before loading its dependencies so a back-edge resolves — are
**UNVERIFIED** in these sources. A <code>DT_NEEDED</code> cycle will terminate under the dedup
guard; whether symbol resolution then sees the module is not established here.

---

## 2. Symbol resolution

### 2.1 The reference's only symbol-visibility logic is a JNI export scan

[ZB] has **no runtime relocation resolver**. <code>dlopen</code>/<code>dlsym</code> are
delegated to guest bionic via the service API
(<code>~/zb-src/core/src/library_runtime.cpp:131-157</code>;
<code>~/zb-src/core/include/zb/library_protocol.h:37-48</code>). Its one symbol walk,
<code>scan_elf32_jni_exports</code>, is an import-time JNI-export discovery, not a linker
(<code>~/zb-src/core/src/elf_symbols.cpp:187-326</code>). Its filter is still the reference
statement of "exported" (<code>:306-324</code>):

* <code>st_shndx != SHN_UNDEF</code> (defined);
* binding <code>STB_GLOBAL</code> or <code>STB_WEAK</code>;
* type <code>STT_FUNC</code> or <code>STT_NOTYPE</code>;
* visibility <code>STV_DEFAULT</code> or <code>STV_PROTECTED</code>.

### 2.2 Scope and visibility rules the emulation must use (bionic, not glibc)

* An object exports a symbol only if it is defined (<code>st_shndx != SHN_UNDEF</code>),
  non-local, <code>STV_DEFAULT</code>, and address-carrying
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:170-173</code>).
* Lookup order: **global group first** (main object, interpreter, then closure in load order),
  then the **requesting object's local group** (itself, then its direct
  <code>DT_NEEDED</code>, breadth-first)
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:163-167</code>;
  <code>docs/HOST-ELF-LOADER.md:340-343</code>). Requiring the local group only for the
  requester is what stops a library resolving through a sibling's dependency.
* <code>DT_SYMBOLIC</code> suppresses the local-group pass and forces global lookup — live,
  because the engine and <code>libnativeinterface.so</code> set it
  (<code>docs/HOST-ELF-LOADER.md:344-345</code>).
* Weak vs strong: prefer a <code>STB_GLOBAL</code> definition over <code>STB_WEAK</code>
  (<code>host/src/linker.cpp:122-130</code>).
* <code>dlsym(handle, name)</code> **reverses** the order — local group first, then global
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:167-168</code>; group model
  <code>docs/HOST-DLOPEN.md:309-321</code>).

Discrepancy to resolve: [P]'s loader rule allows <code>STV_DEFAULT</code> **or
<code>STV_PROTECTED</code>** (<code>docs/HOST-ELF-LOADER.md:337-339</code>) while [P]'s runtime
tree says <code>STV_DEFAULT</code> (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:170-173</code>).
Pick one and assert it from the files; the JNI scan in [ZB] allows both
(<code>~/zb-src/core/src/elf_symbols.cpp:317</code>).

### 2.3 SysV hash and GNU hash

The reference implements **symbol-count extraction only**, never a hash lookup:

* **SysV <code>DT_HASH</code>:** <code>nbucket</code>/<code>nchain</code> from words 0/1;
  <code>symbol_count = nchain</code>; table size validated against the file-backed range
  (<code>~/zb-src/core/src/elf_symbols.cpp:101-121</code>).
* **GNU <code>DT_GNU_HASH</code>:** header words <code>nbuckets</code>, <code>symoffset</code>,
  <code>bloom_size</code>, <code>bloom_shift</code>; validate every bucket, then walk only the
  chain starting at the **highest** symbol index, because chains are consecutive and that
  chain ends at the last symbol. This bounds work to <code>buckets + chain</code> instead of
  <code>buckets * chain</code> on untrusted APK input
  (<code>~/zb-src/core/src/elf_symbols.cpp:123-183</code>, rationale <code>:145-148</code>).
  <code>count = symoffset</code> when no bucket is non-empty (<code>:163-166</code>).

The emulation's documented lookup path is <code>.hash</code> (when present), then
<code>.gnu.hash</code> (needed by <code>ld-android.so</code>, <code>libc++.so</code>,
<code>libdl_android.so</code>), then a linear dynsym scan only when neither is present; a
present hash table is authoritative, so a miss means "not defined here"
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:175-179</code>). Both formats occur in this sysroot
(<code>docs/HOST-ELF-LOADER.md:324-331</code>). <code>dynsym_count</code> comes from
<code>DT_HASH</code>'s <code>nchain</code>, or from the end of the highest-numbered non-empty
GNU chain (<code>docs/HOST-ELF-LOADER.md:330-331</code>).

**Current-repo gap:** <code>host/src/linker.cpp:51-71</code> indexes every dynsym entry 1..n
into a <code>std::map</code> (a linear scan at index time) and <code>find_symbol</code>
(<code>:73-77</code>) never consults <code>dt_hash</code>/<code>dt_gnu_hash</code>, even though
<code>parse_dynamic</code> records them (<code>host/src/loader.cpp:197-198,232-261</code>). An
implementer who wants the documented hash path must add it.

### 2.4 Undefined symbols

* A **weak** undefined reference resolves to <code>0</code> and does not fail the load
  (<code>host/src/linker.cpp:130</code>; <code>docs/HOST-ELF-LOADER.md:346-347</code>).
  Measured: exactly 2 in the whole closure (<code>docs/HOST-P0-RELOCS.md:121</code>).
* A **strong** undefined reference is recorded as <code>"&lt;module&gt;: &lt;name&gt;"</code>
  and the load is not <code>ok()</code> (<code>host/src/linker.cpp:139</code>;
  <code>host/src/linker.hpp:37,49</code>). An earlier tree refused the relocation outright
  with a named error (<code>docs/HOST-P0-RELOCS.md:259-262</code>).
* A symbol index of <code>0</code> is <code>STN_UNDEF</code>: the value is the addend alone
  (<code>host/src/linker.cpp:100</code>).
* The reference reports malformed symbol input as <code>ElfSymbolStatus::Error</code> with a
  message (<code>~/zb-src/core/src/elf_symbols.cpp:69-75</code>), but never reports an
  unresolved *link* symbol; that is the guest linker's job there.

---

## 3. Relocations

### 3.1 The reference has no relocation emitters

[ZB]'s host-side ELF code maps segments (<code>~/zb-src/core/src/elf_loader.cpp</code>),
inspects symbols (<code>~/zb-src/core/src/elf_symbols.cpp</code>) and rewrites two dynamic tags
in place (<code>~/zb-src/core/src/elf_fixups.cpp</code>) — it never applies a relocation. All
relocation work is the guest bionic linker's. The one tag rewrite worth knowing: absolute
<code>DT_NEEDED</code> string indices are moved to the basename
(<code>~/zb-src/core/src/elf_fixups.cpp:220-241</code>), and
<code>DT_TEXTREL</code>/<code>DF_TEXTREL</code> become the private marker
<code>DT_ZB_TEXTREL = 0x60005A42</code> so the host can keep those code pages writable for the
guest linker's text relocations (<code>~/zb-src/core/src/elf_fixups.cpp:242-277</code>;
<code>~/zb-src/core/include/zb/elf_fixups.h:9-12</code>). The emulation does not do this: it
never writes guest files, and a text relocation is a hard error
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:41-46</code>;
<code>docs/HOST-ELF-LOADER.md:662-664</code>).

### 3.2 Classic SHT_REL types the emulation must handle

Every type writes to <code>slot = r_offset + module.bias</code>
(<code>host/src/linker.cpp:167</code>; <code>host/src/loader.cpp:289,297</code>). For a
<code>REL</code> platform the addend <code>A</code> is the 4-byte word already in the slot
**except** for <code>JUMP_SLOT</code> (below).

| Type | Value written | Notes / citation |
| --- | --- | --- |
| <code>R_ARM_NONE</code> (0) | nothing | counted only (<code>host/src/linker.cpp:177-179</code>) |
| <code>R_ARM_RELATIVE</code> (23) | <code>A + bias</code>, A = in-place word | the in-place word *is* the addend; 49,132 in the engine alone (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:124</code>; <code>host/src/linker.cpp:180-193</code>) |
| <code>R_ARM_ABS32</code> (2) | <code>S + A</code>, A = in-place word | A is *not* zero in this corpus: <code>&amp;sym + 4</code> is common (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:125</code>; <code>host/src/linker.cpp:194-208</code>) |
| <code>R_ARM_GLOB_DAT</code> (21) | <code>S + A</code> (keeps the addend) | [P] measurement (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:126</code>). **Discrepancy:** current <code>host/src/linker.cpp:209-218</code> writes <code>S</code> only and never reads the slot. ARM ELF ABI says <code>S + A</code>; read the slot. |
| <code>R_ARM_JUMP_SLOT</code> (22) | <code>S</code> — **addend discarded** | the slot initially holds the object's PLT resolver-stub address; <code>S + stub</code> silently lands in a different library. 1,385 test failures before it was found (<code>docs/HOST-ELF-LOADER.md:302-306,727-729</code>; <code>docs/HOST-RUNTIME-LINKER-THREADS.md:127</code>; <code>host/loader/src/dynamic.cpp:1111-1119</code> per <code>docs/HOST-ELF-LOADER.md:302</code>) |
| <code>R_ARM_TLS_TPOFF32</code> (19) | module static-TLS offset + <code>st_value</code> + A | variant I, block below TP. One instance in the sysroot (<code>libc.so</code> <code>.got</code>, symbol 0, addend 0). Two private trees disagree on the concrete value — see section 4.3. (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:128</code>; <code>host/src/linker.cpp:220-255</code>) |
| <code>R_ARM_TLS_DTPMOD32</code> (17) | 1-based module id of the defining object | one instance, inside <code>libc++.so</code>'s packed table (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:129,236-237</code>; <code>host/src/linker.cpp:241-242</code>) |
| <code>R_ARM_TLS_DTPOFF32</code> (18) | <code>st_value + A</code> inside the module block | <code>host/src/linker.cpp:243-244</code> |

Measured whole-closure inventory: classic **56,978** + packed **2,645** = **59,623**;
<code>RELATIVE 50,323</code> (classic) + <code>ABS32 5,100</code>,
<code>JUMP_SLOT 1,495</code>, <code>GLOB_DAT 59</code>, <code>TLS_TPOFF32 1</code> at the end
state (<code>docs/HOST-ELF-LOADER.md:296-300,524-531</code>). Per-module census
<code>docs/HOST-RUNTIME-LINKER-THREADS.md:104-118</code>.

### 3.3 Packed Android encodings (load-bearing, not optional)

<code>libc++.so</code> carries both, and it is required: <code>liblog.so</code> imports 13
symbols from it and <code>liblog.so</code> is a direct <code>DT_NEEDED</code> of the engine
(<code>docs/HOST-ELF-LOADER.md:239-242</code>).

* **<code>DT_ANDROID_REL</code> / AndroidRel** — SLEB128 stream, bionic
  <code>for_all_packed_relocs</code> layout: <code>num_relocs, r_offset,</code> then repeated
  <code>{group_size, group_flags, [offset delta], [r_info], group_size * {delta, r_info}}</code>;
  <code>GROUP_HAS_ADDEND</code> must be clear on a REL platform
  (<code>docs/HOST-ELF-LOADER.md:244-259</code>). The leading bytes are **not** a magic header
  — <code>APS2</code> is part of the first LEB128 values; reading four fixed words produced a
  bogus 5,460,033-entry group (<code>docs/HOST-ELF-LOADER.md:259-262</code>). Decoded
  <code>libc++.so</code>: 2,112 entries, 1,056 <code>RELATIVE</code>
  (<code>docs/HOST-ELF-LOADER.md:264</code>).
* **<code>DT_ANDROID_RELR</code> / <code>DT_RELR</code>** — plain RELR bitmap: even word =
  address, odd word = bitmap of <code>base + 4*bit</code>
  (<code>docs/HOST-ELF-LOADER.md:267-269</code>). Decoded <code>libc++.so</code> 533,
  <code>linker</code> 4,215 (<code>:270</code>). Track the RELR <code>where</code> cursor per
  the format definition rather than resetting it per base word
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:156-159</code>).
* **Decoder acceptance test:** decode with each candidate and accept the first whose entries
  are non-empty, 4-byte aligned, and all inside a **writable** segment. Accepting merely
  "inside the image" let a wrong SLEB128 interpretation through
  (<code>docs/HOST-ELF-LOADER.md:274-280,739-741</code>).

Combined packed census: <code>ABS32 1,921</code>, <code>GLOB_DAT 190</code>,
<code>TLS_DTPMOD32 1</code>, <code>RELATIVE 533</code>
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:146-153</code>).

**Current-repo gap:** <code>host/src/linker.cpp:143-269</code> applies only
<code>DT_REL</code> and <code>DT_JMPREL</code>; no packed decoder exists. This is the largest
missing piece.

### 3.4 Application order and refusals

Order is fixed: **packed tables first, then <code>DT_REL</code>, then
<code>DT_JMPREL</code>** — a packed <code>RELATIVE</code> can write a GOT slot a later
<code>GLOB_DAT</code> overwrites (and the reverse)
(<code>docs/HOST-RUNTIME-LINKER-THREADS.md:131-133</code>). Anything else is a hard, named
error: <code>R_ARM_COPY</code> (0 in this corpus), <code>R_ARM_IRELATIVE</code> (0),
<code>R_ARM_PC24/CALL/MOVW/MOVT/SBREL32</code> (all 0), <code>DT_RELA</code>, packed
<code>DT_ANDROID_RELA</code>, TLSDESC, and a <code>DT_RELENT</code> other than
<code>sizeof(Elf32_Rel)</code> (<code>docs/HOST-ELF-LOADER.md:216-227,635-647</code>;
<code>docs/HOST-RUNTIME-LINKER-THREADS.md:135-137</code>; <code>host/src/linker.cpp:146-149</code>).
No best-effort path: "a relocation that silently wrote nothing is the failure mode that costs
days" (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:136-137</code>).

---

## 4. The bionic-facing interface

### 4.1 The <code>__loader_*</code> table

**How [ZB] "finds" it:** it does not. The real <code>system/bin/linker</code> (2,059,744
bytes, <code>ET_DYN</code>, entry <code>0x5bbc0</code>; verified <code>readelf</code>) defines
the functions, and <code>ld-android.so</code> is only a 2,868-byte stub that lists the names
so <code>libc.so</code>/<code>libdl.so</code> can link against them. Verified with
<code>readelf</code>: <code>ld-android.so</code> has one load segment
<code>off 0x734 vaddr 0x1734 filesz 0x2 flags R E</code>, bytes <code>fe de</code> (Thumb
<code>udf #0xFE</code>), and all 26 <code>__loader_*</code> exports sit at
<code>st_value 0x1735</code>. The stub is a deliberate trap if its code runs; the real
linker's definitions win the lookup.

**How the emulation must find it:** enumerate the loaded <code>ld-android.so</code>'s own
dynsym exports in dynsym order and assert the name list covers exactly the 26 the object
declares; then interpose at the **one interception point**, the GOT/PLT slot. The closure
binds 19 slots (6 in <code>libc.so</code>, 13 in <code>libdl.so</code>) to the single stub
address; rewrite those slots to ARM stubs after every slot is bound. No guest library is
edited. (<code>docs/HOST-LOADER-INTERFACE.md:79-96,234-250</code>.)

**Full slot list** (<code>docs/HOST-LOADER-INTERFACE.md:105-132</code>; SLOT = imported by
this closure). Decisions: **R** = real implementation, **S** = stub returning bionic's own
failure value, **H** = halt/report.

| # | Export | SLOT | Decision | Detail |
| --- | --- | --- | --- | --- |
| 1 | <code>__loader_android_get_LD_LIBRARY_PATH</code> | yes | R | <code>const char*(void)</code>, real guest string, never NULL |
| 2 | <code>__loader_android_set_application_target_sdk_version</code> | — | R | <code>void(int)</code>; stores the int |
| 3 | <code>__loader_dladdr</code> | yes | S | <code>0</code> ("not found"); no guest symbol table |
| 4 | <code>__loader_dl_unwind_find_exidx</code> | yes | S | <code>0</code> = unwinder's "no table" |
| 5 | <code>__loader_dlclose</code> | yes | S | <code>0</code>; nothing was opened |
| 6 | <code>__loader_android_get_application_target_sdk_version</code> | yes | R | returns #2 |
| 7 | <code>__loader_cfi_fail</code> | yes | H | report, not a call to answer; returning silently hides a hard failure |
| 8 | <code>__loader_android_dlopen_ext</code> | yes | S | NULL; needs a guest <code>soinfo</code> list |
| 9 | <code>__loader_android_dlwarning</code> | — | S | warning sink; dropped and recorded |
| 10 | <code>__loader_android_init_anonymous_namespace</code> | — | S(true) | one global scope |
| 11 | <code>__loader_dl_iterate_phdr</code> | yes | S | <code>0</code> = "no objects reported"; cannot re-enter JIT from an SVC handler |
| 12 | <code>__loader_shared_globals</code> | yes | R | the whole point; section 4.2 |
| 13 | <code>__loader_android_link_namespaces</code> | — | S(true) | one global scope |
| 14 | <code>__loader_android_handle_signal</code> | yes | S(false) | no signal layer to hand it to |
| 15 | <code>__loader_dlsym</code> | yes | S | NULL |
| 16 | <code>__loader_dlvsym</code> | yes | S | NULL |
| 17 | <code>__loader_remove_thread_local_dtor</code> | yes | R | <code>bool(fn,arg)</code>, matches both words |
| 18 | <code>__loader_android_set_16kb_appcompat_mode</code> | — | R | <code>void(bool)</code>; stores |
| 19 | <code>rtld_db_dlactivity</code> | — | R | <code>false</code> (no debugger) |
| 20 | <code>__loader_android_create_namespace</code> | — | S | NULL; one global scope |
| 21 | <code>__loader_android_get_exported_namespace</code> | yes | S | NULL; one global scope |
| 22 | <code>__loader_android_link_namespaces_all_libs</code> | — | S(void) | one global scope |
| 23 | <code>__loader_android_update_LD_LIBRARY_PATH</code> | — | R | records the string #1 returns |
| 24 | <code>__loader_dlerror</code> | yes | R | real <code>"no error"</code> string, never NULL |
| 25 | <code>__loader_dlopen</code> | yes | S | NULL; needs a guest <code>soinfo</code> list |
| 26 | <code>__loader_add_thread_local_dtor</code> | yes | R | <code>int(fn,arg)</code>, capped 65,536 entries |

**13 real, 12 stub, 1 halt.** The dividing line is "can this host answer without guessing";
the 12 stubs and the halt entry had **never been executed** as of the source doc
(<code>docs/HOST-LOADER-INTERFACE.md:134-138,387-390</code>).

Stub encoding: 8 bytes, <code>svc #0x100+i ; bx lr</code>, immediate <code>0x100 + index</code>
(disjoint from the libcall shim's <code>0x40..0x84</code>)
(<code>docs/HOST-LOADER-INTERFACE.md:225-239</code>;
<code>docs/HOST-OWNHOST-PROGRESS.md:29-36</code>).

### 4.2 <code>libc_shared_globals</code> / <code>libc_globals</code>

<code>__loader_shared_globals()</code> returns a guest-visible block; <code>libc.so</code>
reaches it through the same <code>movw/movt/add r12,pc/bx r12</code> veneer into an A32 thunk
and then a GOT slot, so the block is found by decoding <code>libc.so</code>'s own bytes, not
guessed (<code>docs/HOST-LOADER-INTERFACE.md:142-183</code>). Layout measured by scanning all
42 call sites of <code>__libc_shared_globals</code> in <code>libc.so</code>
(<code>docs/HOST-LOADER-INTERFACE.md:144-177</code>):

| Offset | Field | Must be filled with | Read by |
| --- | --- | --- | --- |
| <code>+0x410</code> | <code>__libc_argc</code> | loader <code>argc</code> | <code>__libc_init</code> <code>libc.so+0x49122</code> (to compute <code>envp = argv + 1 + argc</code>) |
| <code>+0x414</code> | <code>__libc_auxv</code> | guest address of the auxv | <code>getauxval</code> <code>libc.so+0x6ABC4</code>, walks to <code>AT_NULL</code> |
| <code>+0x51C</code> | profile flag | <code>0</code> | <code>__libc_init_common</code> <code>libc.so+0x54F56</code> |
| <code>+0x520</code> | <code>mmap_threshold</code> | <code>0x20000</code> (bionic default for a 4 KiB page) | <code>__libc_init_common</code> <code>libc.so+0x54F40</code> |
| <code>+0x554</code> | <code>program_invocation_name</code> | address of <code>argv[0]</code>'s string | <code>__libc_init</code> <code>libc.so+0x4911E</code> |
| <code>+0x55C</code> | <code>program_invocation_short_name</code> | same address | <code>android_crash_detail_register</code> |
| <code>+0x600</code> | <code>libc_globals</code> | zeroed, 4,096 bytes | nothing reached by the measured run |

Offsets that <code>libc.so</code> code touches but no reached path exercises:
<code>+0x41C</code>, <code>+0x428</code>, <code>+0x464</code>, <code>+0x468</code>,
<code>+0x470</code>, <code>+0x524</code>, <code>+0x528</code> — all left zero
(<code>docs/HOST-LOADER-INTERFACE.md:175-176,417-419</code>). Read-back verification compares
the block to loader state, not to what the program believes it wrote
(<code>docs/HOST-LOADER-INTERFACE.md:200-213</code>).

**Open inconsistency (UNVERIFIED):** the same source calls the block "4,194-byte"
(<code>docs/HOST-LOADER-INTERFACE.md:32</code>) but allocates 8,192 bytes
(<code>:243</code>) and places a 4,096-byte <code>libc_globals</code> at <code>+0x600</code>,
which ends at <code>0x1600</code> = 5,632 (<code>:195,215-216</code>). 4,194 &lt; 5,632, so
the size figure cannot be right for the layout shown. Re-derive the struct size from
<code>libc.so</code>'s own <code>__libc_globals</code> (<code>.bss</code> size
<code>0x1000</code>) and the largest field offset before allocating.

### 4.3 Thread pointer (TPIDRURO) and TLS

Bionic reads its thread pointer with <code>mrc p15, 0, Rt, c13, c0, 3</code> (TPIDRURO). The
host must serve that register and have it set **before any guest instruction runs**.

* Register plumbing: <code>Cp15</code> serves <code>c13,c0,3</code> (read) and
  <code>c13,c0,2</code> (TPIDRURW) (<code>~/zb-src/core/include/zb/cp15.h:11-13</code>;
  <code>~/zb-src/core/src/cp15.cpp:46-52</code>); the
  <code>__ARM_NR_set_tls</code>/<code>__ARM_NR_get_tls</code> syscalls read/write it
  (<code>~/zb-src/core/src/syscalls.cpp:1384-1390</code>);
  <code>GuestThread::tls()</code> is that register
  (<code>~/zb-src/core/include/zb/guest_thread.h:74-75,163</code>). The emulation must
  additionally set Dynarmic's TPIDRURO from its initial-thread state — the <code>mrc</code>
  is not intercepted elsewhere (<code>docs/HOST-ELF-LOADER.md:382-387</code>; current repo:
  <code>host/src/cpu.cpp:60-61</code>, <code>host/src/cp15.cpp:33-52</code>,
  <code>host/src/syscalls.cpp:207</code>).
* The kuser helper page at <code>0xFFFF0000</code> provides
  <code>__kuser_get_tls</code> <code>0xFFFF0FE0</code> as
  <code>mrc p15,0,r0,c13,c0,3 ; bx lr</code>, <code>__kuser_cmpxchg</code>
  <code>0xFFFF0FC0</code>, <code>__kuser_memory_barrier</code> <code>0xFFFF0FA0</code>,
  <code>__kuser_helper_version</code> <code>0xFFFF0FFC</code> = **3** (no cmpxchg64)
  (<code>~/zb-src/core/src/process.cpp:136-168</code>). An earlier design doc says version 2
  (<code>docs/superpowers/plans/2026-09-14-phase2-guest-linker.md:169</code>); the code is
  authoritative.
* Static TLS layout (variant I): blocks live **below** TP, cumulative in load order, aligned
  to each module's <code>p_align</code>. Measured constraint: <code>libc.so</code>'s stack
  canary is read as <code>[tp, #-4]</code>, which forces <code>libc.so</code>'s 8-byte block
  to end exactly at TP and gives offset <code>-8</code>; <code>libc++.so</code> is below it.
  TP is a bionic <code>pthread_internal_t</code>, not a bare TCB:
  <code>[TP]=TP</code>, <code>[TP+4]=TP</code>, <code>errno</code> at <code>TP+0x29C</code>,
  at least <code>0x2A0</code> bytes of control block; TP is 16-byte aligned
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:209-249</code>; confirmed by the later run
  <code>docs/HOST-OWNHOST-PROGRESS.md:104-112,143-181</code>). **This control block must be
  written before guest code runs** — leaving it zero made the first
  <code>__set_errno_internal</code> write to <code>0x29C</code> and was the stop that blocked
  startup (<code>docs/HOST-OWNHOST-PROGRESS.md:143-181</code>).
* **Contradiction to resolve:** the other private tree models static TLS as *positive* offsets
  past a TP at the end of the used block (<code>libc.so</code> offset <code>0x00</code>,
  <code>R_ARM_TLS_TPOFF32</code> writes <code>0</code>,
  <code>docs/HOST-ELF-LOADER.md:368-398</code>). The runtime tree's <code>[tp,-4]</code>
  canary measurement is the stronger constraint and matches ELF variant I
  (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:225-234</code>). The current repo's
  <code>host/src/linker.cpp:246</code> computes
  <code>tls_offsets_[m] + symbol_value - tls_total_ + addend</code>, i.e. a negative (variant
  I) offset; keep that convention and make the <code>mprotect</code> that makes the block
  writable page-aligned (mprotect refuses an unaligned address;
  <code>docs/HOST-OWNHOST-PROGRESS.md:176-179</code>).
* Dynamic TLS (<code>R_ARM_TLS_DTPMOD32</code>/<code>DTPOFF32</code>,
  <code>__tls_get_addr</code>, DTV) is **not modelled**; the static values are correct only
  for bootstrap-time modules (<code>docs/HOST-ELF-LOADER.md:394-398,622-628</code>;
  <code>docs/HOST-RUNTIME-LINKER-THREADS.md:403-409</code>).

---

## 5. The libcall shim and the SVC host-call convention

An SVC is the seam because Dynarmic's <code>CallSVC</code> callback returns to the **next**
guest instruction when it does not halt, which is exactly a function call; the stub's
<code>bx lr</code> returns to the caller (<code>docs/HOST-P0-RELOCS.md:226-230</code>). The
stub is 8 bytes: <code>svc #&lt;imm&gt; ; bx lr</code>, placed in a page claimed in guest
memory and protected R+X, so an imported call lands in working guest code the host executes
(<code>docs/HOST-P0-RELOCS.md:219-224</code>;
<code>docs/HOST-LOADER-INTERFACE.md:234-238</code>). Current
<code>host/src/cpu.cpp:193-203</code> already records the SVC and stops at the next
instruction.

**Immediate map (verified from the sources).** Ranges are disjoint and must stay so.

| Range | Owner | Source |
| --- | --- | --- |
| <code>0x40 .. 0x40+69</code> = <code>0x40..0x84</code> | softfp/libm **libcall shim** | <code>docs/HOST-OWNHOST-PROGRESS.md:32</code>; <code>docs/HOST-P0-RELOCS.md:218-224</code> |
| <code>0x100 .. 0x100+25</code> = <code>0x100..0x119</code> | <code>__loader_*</code> table | <code>docs/HOST-LOADER-INTERFACE.md:233-234</code>; <code>docs/HOST-OWNHOST-PROGRESS.md:33</code> |
| <code>0x0</code> (with <code>r7</code> = number), and <code>0x5A0000&#124;index</code> | guest syscall layer / host calls | <code>docs/HOST-OWNHOST-PROGRESS.md:29-36</code> |
| <code>0x1..0x5</code> (<code>6..0xF</code> reserved) | <code>dl*</code> bridge (<code>dlopen</code>=1, <code>dlsym</code>=2, <code>dlclose</code>=3, <code>dlerror</code>=4, <code>android_dlopen_ext</code>=5) | <code>docs/HOST-DLOPEN.md:227-237</code> |
| <code>0x5AFFFF</code> | host-to-guest call return trap | <code>~/zb-src/core/include/zb/guest_thread.h:27-28</code>; <code>~/zb-src/core/src/process.cpp:137-139,588-596</code> |
| <code>0x5A0000&#124;index</code> (generated stubs) | [ZB] GL/EGL/android host calls | <code>~/zb-src/guest/stubs/gen/libGLESv2.S:9-12</code>; <code>~/zb-src/core/src/process.cpp:70-71,597-636</code> |
| <code>0x5AFE00</code> / <code>0x5AFE01</code> | [ZB] library-runtime READY / PARK | <code>~/zb-src/guest/zbhost/zbhost.c:22-30</code>; <code>~/zb-src/core/include/zb/library_protocol.h:12-15</code> |

**Which symbols the libcall shim interposes.** The engine is softfp, so it reaches
<code>__aeabi_fmul</code>, <code>__aeabi_fadd</code>, <code>__aeabi_fcmpeq</code>,
<code>sqrtf</code>, etc. through its PLT. The shim provides **69** host implementations with
the guest AAPCS32 softfp register convention (float args in <code>r0</code>/<code>r1</code>,
result in <code>r0</code>; doubles in <code>r0:r1</code>/<code>r2:r3</code>) and 69 ARM stubs
(<code>docs/HOST-P0-RELOCS.md:211-217</code>). Interposed over the relocated closure: **76 GOT
slots** covering **67 distinct names** across 10 modules; the two names never imported are
<code>__aeabi_f2uiz</code> (the engine defines it) and <code>log10f</code>
(<code>docs/HOST-P0-RELOCS.md:232-234</code>). The full 67-name list is not reproduced in the
source doc; mark any name not listed there **UNVERIFIED**.

**Return path.** The dispatcher calls the host function and leaves its result in
<code>r0</code> (or <code>r0:r1</code> for a double); it writes the chosen slot and resumes at
the instruction after the <code>svc</code>; the stub's <code>bx lr</code> then returns to the
original caller. The host function is entered *instead of* the guest function and must leave
the guest stack as it found it — the replacement stubs never push
(<code>docs/HOST-P0-RELOCS.md:219-230</code>; <code>docs/HOST-DLOPEN.md:134-138</code>). The
<code>dl</code> stubs are the same pair, Thumb-encoded (<code>svc #index</code> =
<code>0xDF00|index</code>, <code>bx lr</code> = <code>0x4770</code>), and the <code>svc</code>
trap is **SIGILL**, not SIGSYS; the saved <code>arm_pc</code> is the instruction after the
<code>svc</code> (<code>docs/HOST-DLOPEN.md:94-120,173-197</code>). The <code>dl*</code>
replacement is an **export rewrite**, not a PLT rewrite, because <code>libc.so</code> carries
a 102-entry <code>R_ARM_ABS32</code> builtin table pinning <code>dlopen</code> absolutely;
measure each write span from the next export, not a constant 16 bytes
(<code>docs/HOST-DLOPEN.md:125-152</code>).

---

## 6. The traps that cost the earlier workstream time — as rules

1. **One region table, or rebuild the JIT's snapshot.** The JIT's memory callbacks hold a
   table snapshotted at construction while the syscall layer mutates another; a region added
   by <code>mmap2</code>/<code>brk</code> is invisible to the JIT until the core is rebuilt.
   Treat this as known debt and prefer a single shared region table
   (<code>docs/HOST-OWNHOST-PROGRESS.md:357-362</code>). Corollary: region bookkeeping is
   **cumulative**; a merge that *replaced* the region list instead of appending dropped the
   shim page and the engine faulted on the stub it had just been relocated to call
   (<code>docs/HOST-OWNHOST-PROGRESS.md:44-50</code>).
2. **Never drop the code cache from inside a JIT callback.**
   <code>note_memory_changed()</code> called <code>Jit::ClearCache()</code> from inside
   Dynarmic's <code>CallSVC</code>, i.e. from emitted host code whose own block was being
   freed; the first guest <code>mmap2</code> died in the host's <code>memset</code>. Defer:
   sort the region table immediately and set a flag the **run loop** consumes between
   translated blocks — the only safe place
   (<code>docs/HOST-OWNHOST-PROGRESS.md:51-58</code>).
3. **Claim without protect is not a mapping.** <code>AddressSpace::claim</code> only records
   pages; the 4 GiB reservation is <code>PROT_NONE</code>, so a following
   <code>memset</code> faults on a page the bitmap says is claimed. Every claimer
   (<code>brk</code>, <code>mmap2</code>, <code>mremap</code>, <code>mprotect</code>) must
   claim **and** protect in one helper using the guest's own <code>prot</code>
   (<code>docs/HOST-OWNHOST-PROGRESS.md:59-65</code>;
   <code>docs/HOST-LOADER-INTERFACE.md:353-377</code>).
4. **The region lookup is a linear scan on purpose.** A binary search over the
   sorted-by-construction table returned null for an address the table demonstrably
   contained (<code>regions=69</code>); a linear scan found it, and <code>__libc_init</code>
   then ran to completion. The failure is reproducible and unreduced. Do not reintroduce a
   binary search without reproducing it first
   (<code>docs/HOST-OWNHOST-PROGRESS.md:122-141</code>).
5. **A non-null exclusive monitor is mandatory.** The first <code>ldaex r1,[r0]</code>
   (<code>libc.so</code>'s own atomic path) killed the process with
   <code>global_monitor == nullptr</code>; on x86_64 Dynarmic asserts, on arm64 the emitted
   code dereferences null. Construct <code>Dynarmic::ExclusiveMonitor(N)</code> and assign
   <code>cfg.global_monitor</code> (<code>docs/HOST-P0-RELOCS.md:399-451</code>). The current
   repo already does (<code>host/src/cpu.cpp:55-58</code>).
6. **Addressing discipline.** Keep link-time offsets and guest addresses in separate fields;
   only the module's own <code>segment_containing_guest</code> may compare guest addresses.
   Three bugs came from breaking this (<code>docs/HOST-ELF-LOADER.md:152-179</code>). The
   related one-offs: <code>JUMP_SLOT</code> as <code>S + A</code>
   (<code>:727-729</code>), placement from <code>p_vaddr</code> instead of its page-aligned
   value (<code>:715-720</code>), protections before the copy
   (<code>:704-706</code>), and a 32-bit sum that wraps near the top of the window
   (<code>:736-738</code>).
7. **Guest <code>R_ARM_ABS32</code> pins absolute addresses, which argues for host-side bias
   assignment.** Because those relocation sites pin absolute addresses, the host that assigns
   biases knows where every one is, while a guest linker fed patched files can move them
   (<code>docs/HOST-RUNTIME-LINKER-THREADS.md:53-58</code>).

---

## 7. Current repo state and the gaps this spec must close

Already present in <code>host/</code> (read-only for this document):
<code>map_elf</code>/<code>parse_dynamic</code> (<code>host/src/loader.cpp:37-263</code>),
<code>GuestLinker</code> with
<code>NONE/RELATIVE/ABS32/GLOB_DAT/JUMP_SLOT/TLS_*</code>
(<code>host/src/linker.cpp:143-269</code>), BFS <code>DT_NEEDED</code> closure and static-TLS
assignment (<code>:271-309</code>), <code>Dynarmic</code> A32 core with CP15 and exclusive
monitor (<code>host/src/cpu.cpp:52-70</code>, <code>host/src/cp15.cpp</code>), initial stack
image (<code>host/src/initial_stack.cpp:14-75</code>), and an ARM EABI syscall slice
(<code>host/src/syscalls.hpp:25-63</code>).

Missing relative to the mechanism above: packed Android relocations (section 3.3);
<code>__loader_*</code> interposition and the <code>libc_shared_globals</code> block (section
4.2); the pre-guest TPIDRURO/control-block setup (section 4.3) —
<code>host/src/loader.cpp</code> maps <code>PT_TLS</code> but nothing writes the bionic control
block; the libcall-shim SVC range and host implementations (section 5);
<code>dlopen</code>/<code>dlsym</code> at run time;
<code>DT_SYMBOLIC</code>/<code>BIND_NOW</code> handling; <code>.hash</code>/<code>.gnu.hash</code>
lookup (section 2.3); <code>PT_GNU_RELRO</code>; and symbol versioning (absent from this
corpus). <code>host/src/linker.cpp:146-149</code> already refuses an unsupported
<code>DT_RELENT</code>; keep the hard-error discipline for every type it does not implement.

---

## 8. UNVERIFIED

1. <code>DT_NEEDED</code> **cycle** semantics: no host-side cycle handling exists in [ZB]; the
   emulation only dedups by name. Whether a back-edge sees the module is not established
   (section 1.3).
2. The **4,194-byte** <code>libc_shared_globals</code> size contradicts the
   <code>+0x600</code> + 4,096 layout in the same source; re-derive the size from
   <code>libc.so</code> (section 4.2).
3. <code>R_ARM_GLOB_DAT</code> addend: [P] says <code>S + A</code>, current
   <code>host/src/linker.cpp</code> writes <code>S</code>; the slot is never read in the
   current code. Resolve against the ABI and the corpus (section 3.2).
4. <code>STV_PROTECTED</code> exportability: allowed in one [P] tree and [ZB]'s scan, excluded
   in the other [P] tree (section 2.2).
5. Static-TLS sign/convention: the two private trees disagree (<code>-8</code> below TP vs
   <code>0x00</code> past TP). Only the <code>[tp,-4]</code> canary constraint is measured
   (section 4.3).
6. The libcall shim's full 67-name list is not reproduced in the available docs; only the
   examples and the two never-imported names are verified (section 5).
7. The 12 <code>__loader_*</code> stubs and the 1 halt entry have never been executed
   (<code>docs/HOST-LOADER-INTERFACE.md:387-390</code>).
8. <code>mmap_threshold = 0x20000</code> is a host choice, not a measured
   <code>libc.so</code> requirement (<code>docs/HOST-LOADER-INTERFACE.md:389-391</code>).
9. <code>libc_globals</code> at <code>+0x600</code> is zeroed only; no
   <code>__libc_init_globals</code> equivalent runs
   (<code>docs/HOST-LOADER-INTERFACE.md:392-394</code>).
10. <code>__loader_dl_iterate_phdr</code> returning 0 breaks any guest that enumerates loaded
    objects (<code>docs/HOST-LOADER-INTERFACE.md:397-399</code>).
11. Packed <code>DT_ANDROID_REL</code> decoding: the exact group-flag semantics are reproduced
    from the source doc's reading of bionic <code>for_all_packed_relocs</code>; I did not read
    bionic itself (<code>docs/HOST-ELF-LOADER.md:244-262</code>).
12. The private-tree code cited by [P] (<code>host/loader/...</code>, <code>host/rt/...</code>,
    <code>host/dl/...</code>) is not present on this machine; its line numbers are taken from
    the DH2 documents, not re-read.

## 9. Sources and notices

Primary reference: <code>~/zb-src</code> (ZettaBridge). Files read in full for this document:
<code>core/src/library_runtime.cpp</code>, <code>core/include/zb/library_runtime.h</code>,
<code>core/include/zb/library_protocol.h</code>, <code>core/src/elf_symbols.cpp</code>,
<code>core/src/elf_fixups.cpp</code>, <code>core/src/elf_loader.cpp</code>, plus
<code>core/src/process.cpp</code>, <code>core/src/initial_stack.cpp</code>,
<code>core/include/zb/cp15.h</code>, <code>core/src/cp15.cpp</code>,
<code>core/include/zb/guest_thread.h</code>, <code>core/include/zb/elf_loader.h</code>,
<code>core/include/zb/elf_symbols.h</code>, <code>core/include/zb/elf_fixups.h</code>,
<code>core/include/zb/native_call.h</code>, <code>guest/zbhost/zbhost.c</code>,
<code>guest/stubs/gen/libGLESv2.S</code>, and <code>readelf</code> on the bundled
<code>sysroot</code>.

Cross-checked DH2Work documents: <code>docs/HOST-ELF-LOADER.md</code>,
<code>docs/HOST-LOADER-INTERFACE.md</code>,
<code>docs/HOST-RUNTIME-LINKER-THREADS.md</code>, <code>docs/HOST-DLOPEN.md</code>,
<code>docs/HOST-OWNHOST-PROGRESS.md</code>, <code>docs/HOST-P0-RELOCS.md</code>,
<code>docs/PORTING-POLICY.md</code>.

Licence: ZettaBridge is cumulative PolyForm Noncommercial 1.0.0 + PolyForm Perimeter 1.0.1
(<code>~/zb-src/LICENSE:1-15</code>). Its mechanisms may be adapted for this noncommercial
project, and copied mechanisms must retain their notices
(<code>docs/PORTING-POLICY.md:7-9,31-34</code>).
