# DH2 HD v1.0.2 — native ARM64 port shortlist

Target: replace the hottest guest functions of the **unmodified 32-bit armeabi-v7a
`libDungeonHunter2.so`** (SHA-256 `36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`,
15,938,284 bytes) with hand-written/frozen-native ARM64, verified bit-exactly against
the original by the existing oracle + differential loop.

Every figure below is one of three things, and is labelled as such:

* **[measured]** — reported by the device work (the numbers in the brief) or produced by
  executing the original ARM32 in Unicorn.
* **[derived]** — computed from the pristine binary or its local disassembly, reproducibly.
* **[inferred]** — a judgement; the reasoning and its weakness are stated.

Nothing here is a profiler measurement, because no profiler can attribute time inside the
JIT. Section 5 states exactly what that costs us.

---

## 1. Headline: the engine's FP is guest soft-float — the FP premise holds, through a different mechanism

> **Read §9 with this section.** My first draft of §1 concluded that because the engine has no
> FP opcodes, the FP cost model could not apply. **That was wrong.** The `__aeabi_*` helpers
> are guest ARM32 libc bodies (124–264 translated instructions each) and they *are* translated.
> §9 carries the evidence, the corrected cost model and the corrected ranking. §1's
> observations stand; only their interpretation changed.

**[derived] `libDungeonHunter2.so` contains essentially no floating-point instructions.**

* Local disassembly of the entire `.text` (1,489,002 instructions across 31,018 function
  starts, `DH2-recon/asm/engine-assembly.txt`, headers at line 2 of each record give
  `addr=… size=… mode=…`): the **only** mnemonics beginning with `v` in the whole file are
  `vaddl.s8`, `vaddl.u32`, `vhadd.s16`, `vhadd.u32`, `vhadd.u8`, `vld4.32` — six
  instructions, all of them SIMD-integer forms. There is **no** `vmul`, `vadd.f32`, `vsub.f32`,
  `vdiv`, `vsqrt`, `vcvt`, `vldr`, `vstr`, `vmov`, `fmuls`, `fadds`, `flds`.
* Instead the engine imports the **soft-float helper ABI**: 43 `__aeabi_*` symbols are
  undefined in `.dynsym` — `__aeabi_fadd`, `__aeabi_fsub`, `__aeabi_fmul`, `__aeabi_fdiv`,
  `__aeabi_fcmpeq/lt/le/gt/ge`, `__aeabi_dadd`, `__aeabi_dsub`, `__aeabi_dmul`,
  `__aeabi_ddiv`, `__aeabi_dcmp*`, `__aeabi_i2f`, `__aeabi_f2iz`, `__aeabi_f2d`,
  `__aeabi_d2f`, `__aeabi_i2d`, `__aeabi_d2iz`, `__aeabi_d2uiz`, `__aeabi_ui2f`,
  `__aeabi_ui2d`, `__aeabi_ul2d`, … (full list in Appendix A).
* `.ARM.attributes` for the engine is `Tag_ABI_VFP_args = 1` (FP *arguments* travel in VFP
  registers) with **no VFP arithmetic** — the "softfp, no FP instructions" build.
  By contrast `libStormGLOFT.so` is built for `cortex-a8` and does contain hard VFP, e.g.
  `recovered/native/assembly/libStormGLOFT.so/ast_expression-8fa05279fc6c-001.asm:1`
  region shows `vldr s0,[r4,#0x30]` / `vcvt.f64.f32 d0,s0` / `vmov r2,r3,d0`.
  `libnativeinterface.so` is `5TE`, also no VFP.

**[derived] Consequence — and where my own first reading of it was wrong.** My first draft
concluded from the above that "the helpers are imports, so they run as native ARM64 libgcc
and the translator never emulates FP". **That is false; §9 is the correction and carries the
evidence.** The helpers are exported by the **guest ARM32 bionic libc the wrapper itself
ships**, so they are 32-bit ARM code the JIT translates like everything else. Verified
independently here — the shipped sysroot library
`DH2Work-stage\compatibility\work\fold7-build\out\assets\zb\sysroot\system\lib\libc.so` is
`EM_ARM`/ELF32 and defines all of them as `STT_FUNC`:

| helper | address in guest libc | size | ARM instructions | ISA |
|---|---:|---:|---:|---|
| `__aeabi_fmul` | `0x000a06f4` | 496 B | **124** | ARM |
| `__aeabi_fadd` | `0x0009f1e8` | 684 B | **171** | ARM |
| `__aeabi_fdiv` | `0x0009faac` | 596 B | **149** | ARM |
| `__aeabi_fsub` | `0x000a0930` | 8 B | 3 | Thumb (tail-call into `__aeabi_fadd`) |
| `__aeabi_f2iz` | `0x0009fe0c` | 92 B | 43 | Thumb |
| `__aeabi_i2f` | `0x000a00e4` | 132 B | 33 | ARM |
| `__aeabi_f2d` | `0x0009fd00` | 164 B | 41 | ARM |
| `__aeabi_d2f` | `0x000a0938` | 388 B | 97 | ARM |
| `__aeabi_dmul` | `0x000a03a0` | 784 B | **196** | ARM |
| `__aeabi_dadd` | `0x0009ed98` | 1,056 B | **264** | ARM |
| `__aeabi_ddiv` | `0x0009f638` | 1,048 B | **262** | ARM |
| `__aeabi_d2iz` | `0x0009fda4` | 104 B | 50 | Thumb |

Those bodies contain 13–16 `vmov` register transfers each, used only to move float bits
between general and FP registers — the arithmetic underneath is integer soft-float
(normalise / shift / round / branch), which is precisely the family the translator's cost
model prices at 22–26×. **So the cost model does apply**, reached through
`bl`-into-guest-libc rather than through FP opcodes. The static census over the whole engine
is unchanged and still useful:

| helper class | call sites | share |
|---|---:|---:|
| `__aeabi_fmul` | 8,518 | 34.6% |
| `__aeabi_fadd` | 6,165 | 25.0% |
| float compares (`__aeabi_fcmp*`) | 3,110 | 12.6% |
| `__aeabi_fsub` | 3,025 | 12.3% |
| `__aeabi_fdiv` | 818 | 3.3% |
| `__aeabi_f2iz` | 640 | 2.6% |
| **all double-precision ops combined** | **675** | **2.7%** |
| conversions (`f2d`, `d2f`, `i2f`, `i2d`, `d2iz`, `d2uiz`, `d2lz`) | 1,012 | 4.1% |
| `libm` (`sqrtf`, `sin`, `cos`, `atan2`, `pow`, `sqrt`, …) | 337 | 1.4% |
| **total** | **24,622** | 100% |

**Corrected reading [inferred]:** the budget is **single-precision soft-float call volume** —
not double (2.7% of sites) and not raw integer volume. Ranking by
`Σ (call sites × the callee's measured instruction count)` promotes the FP-call-dense bodies
to the top of the list; §9.2 is that ranking, where `CCoronasSceneNode::render` (268
soft-float sites) comes second only to `Application::_CheckGamepad`.

**[derived] Corroboration from the sibling project.** `DH_sc-pr/recovered/native/symbols/libDungeonHunter2.so/summary.json`
lists 54,463 relocations and marks these helpers undefined; the engine defines only three
`__aeabi_*` bodies of its own (`__aeabi_llsr` `0x008be268`, `__aeabi_llsl` `0x008be284`,
`__aeabi_f2uiz` `0x008be2a0`). And the project's own verification harness supplies them from
the host: `DH_sc-pr/reports/engine-math-validation.json` →
`external_dependency_model = "Host C float/double arithmetic and libm for imported
__aeabi/libm; same transcendental implementations on both CPUs"`. That is a legitimate
verification model, but it means the reconstructed math module was **not** exercised against
a guest VFP implementation — there is none to exercise it against.

**[inferred] Reconciliation with the published cost model, corrected.** The brief quotes the
translator's own model: double-FP loop 22–26× slower than native, integer ≈2–3×,
memcpy ≈3.5×. My first draft argued the model could not apply because there are no guest FP
opcodes. The resolved position (§9) is that it **does** apply, via the guest libc soft-float
bodies. What the device split still constrains is the *aggregate*: with a measured 21.25 ms
guest slice and 3.88 ms translator slice **[measured: 82.7% / 15.1% of 25.7 ms]**, the
average translated instruction costs about `1 + 3.88/21.25 ≈ 1.18×` its native equivalent.
That average is what you get when a minority of instructions (the soft-float bodies, 124–264
instructions each) are catastrophically expensive and the majority map nearly 1:1. It also
means the win is concentrated: **the same average says a body that is 124 translated
instructions per call is ~100× more expensive than a body that is one native `fmul`.**

**[derived] The arithmetic ceiling.** The guest slice is `0.827 × 25.7 = 21.25 ms` of the
25.7 ms frame. If 100% of it were native: `25.7 − 21.25 = 4.45 ms → ≈225 FPS`. Removing 20%
of the guest slice: `21.45 ms → 46.6 FPS`. Removing 50%: `15.07 ms → 66.3 FPS`.
A single ported function can only be credited with *its* share of that 21.25 ms — but a
function that removes 152 soft-float calls removes 152 × ~150 translated instructions from
that slice, which is far more than its own byte count suggests.

**[derived] The arithmetic ceiling.** The guest slice is `0.827 × 25.7 = 21.25 ms` of the
25.7 ms frame. If 100% of it were native: `25.7 − 21.25 = 4.45 ms → ≈225 FPS`. Removing 20%
of the guest slice: `21.45 ms → 46.6 FPS`. Removing 50%: `15.07 ms → 66.3 FPS`.
A single ported function can only be credited with *its* share of that 21.25 ms.

---

## 2. Scope and inputs actually inspected

| Item | Value |
|---|---|
| engine `.so` analysed | `DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\libDungeonHunter2.so`, 15,938,284 B, SHA-256 `36498eb8…5e80` |
| function starts | 31,018 **[derived]** (`reports/summary.json`: 31,021 symbols / 31,018 distinct addresses) |
| instructions decoded | 1,489,002 across `.text` **[derived]** (`asm/engine-assembly-index.jsonl`); the sibling project's own count for the same ranges is 1,467,476 (`recovered/native/symbols/…/summary.json:instruction_count_per_unique_range`) — the difference is literal pools/mapped data that my dump decodes as instructions, so treat 1,489,002 as an upper bound |
| static call edges | 127,612 `bl`/`b`/`blx#imm` edges over 119,881 `bl` sites **[derived]** (`reports/calls.csv`) |
| virtual dispatch sites | 11,161 `mov lr,pc` + `ldr pc,[rN,#off]` pairs in 4,527 functions **[derived]** |
| PLT imports resolved | 351 / 351 **[derived]** — decoded with the same algorithm as `DH2-recon/tools/dh2_oracle.py:100-149` |
| vtables | 1,628 (`DH_sc-pr/recovered/native/symbols/libDungeonHunter2.so/vtables-001.json`) |

Scripts written for this report live in `DH2Work-scratch\`: `metrics_v2.py` (per-function
decode + PLT attribution), `pltmap.py` (veneer → import), `cover.py` (coverage re-check),
`final.py` / `mktable2.py` (ranking + table), `undefs.py`, `slots.py`, `budget.py`,
`imports.py`. No analysed tree was modified.

---

## 3. Method — how "likely hot" was derived without a working profiler

The device profiler reports 82.7% as anonymous executable memory, so per-function dynamic
attribution is impossible. Everything below is static, and every step is a fact about the
binary; the *interpretation* is where inference enters.

**(a) Ground truth for "per frame" — the frame call chain [derived].** The chain was read out
of the disassembly, not guessed:

```
Java_..._GameRenderer_nativeRender   0x005311a0   (JNI entry, 40 B)
  -> appUpdate                       0x00530fc8   (392 B; calls IDevice::run + Application::Update)
       -> glitch::IDevice::run()     0x00671b24   (event pump loop)
       -> Application::Update()      0x0032ccc4   (772 B)
            -> Application::_Update(int)   0x0032c438
            -> Application::_Draw()        0x0032ade8
            -> TouchScreenBase::ProcessEvents 0x0033c568
            -> PerfCounters::Update(float)    0x0031177c
```

Every function reachable from these addresses is tagged Tier A. The renderer then leaves the
static call graph: `StateMachine::Draw()` (`0x0033a100`) dispatches `ldr pc,[r3,#0x1c]`, i.e.
a vtable slot, and virtually every scene-node and driver method is reached the same way.
That is why static in-degree is *zero* for the entire render path
(`SceneManager::drawAll`, `CSceneManager::renderLists`, `CSkinnedMesh::skin`,
`CBatchDriver::thisAppendBatch` all show `indeg = 0`).

**(b) Recovering per-frame methods from vtable slots [derived].** For each method of interest
its byte offset inside its class vtable was read from `vtables-001.json`, and every
`ldr pc,[rN,#off]` in the engine was counted per offset. Examples that pin a method to the
per-frame path:

| method | slot | engine-wide dispatch sites at that slot |
|---|---|---:|
| `ISceneNode::render(void*)` (incl. `BaseMeshSceneNode<CSkinnedMeshSceneNode>::render`) | `+0x38` | 357 |
| `ISceneNode::onAnimate(unsigned int)` | `+0x30` | 187 |
| `ISceneNode::updateAbsolutePosition(bool)` | `+0xd4` | 16 (+45 vtable entries carry it) |
| `CModularSkinnedMesh::skin(unsigned)` / `CSkinnedMesh::skin(unsigned)` | `+0x54` | 131 |
| `IAnimationTrackEx::getBlendedValue(void*,float*,int,void*,float)` | `+0x38` | 54 |
| `CSceneManager::renderLists(IVideoDriver*)` | `+0x64` | 158 |

Methods whose only entry is such a slot are tagged Tier B.

**(c) Cost model used for ranking [inferred].** Each function gets

```
cost = 3.0 x (sum of backward-branch spans, "loops3")
     + 0.30 x instruction_count            ("base": straight-line translation volume)
     + weighted sum of PLT call sites      ("callw")
     + 1.0 x virtual dispatch sites        ("virt")
```

* `loops3` exists because a loop body is translated once but executed N times; 3 passes is an
  explicit, arbitrary assumption. It is the dominant term for bulk-data functions and is the
  main reason a model like this can be wrong.
* call weights (fdiv ×6, ddiv ×8, libm ×3, integer divide ×4, plain libc ×2, everything else
  in the FP family ×1–2) treat each helper call as **dispatch overhead**, per section 1 —
  explicitly *not* as 22–26× arithmetic emulation.
* `0.30 x` on instruction count is a floor for functions with no loops: they still have to be
  translated.

**(d) What is per-frame and what is not.** Three of the four biggest "cost" functions in the
whole engine are load/one-shot code and were deliberately demoted out of the table:
`glitch::video::pixel_format::convert` `0x005f95ac` (17,128 B, 71 loops),
`pixel_format::(anonymous namespace)::convertPacked` `0x005f4fe8` (17,860 B), and the
`convertPackedImpl<…>` family at `0x005eeef4`, `0x005f0f60`, `0x005f2fdc` (≈8.3 KB each).
They decode textures; `0x0041...`-style loaders and `CImageLoader*::loadImage` are likewise
one-shot. The device profile agrees: the GL driver is 0.16% and texture work is not
per-frame. Conversely `CBillboardSceneNode::render` has only 79 helper calls but *is* in the
render walk, so it stays.

**(e) What would confirm or falsify all of it.**, in order of value:
1. **A JIT-side trace with guest PC → symbol attribution.** The wrapper owns the translation
   cache; a per-guest-PC counter (even a 64-entry hash histogram of `R15` at block dispatch)
   dumped once per second would convert every Tier A/B row into a measurement. This is the
   single highest-value instrument and needs no engine change.
2. **A sampling profiler inside the JIT'd region that maps the guest PC back through the
   engine's own symbol table** (the `.symtab` is intact, so `addr → name` is exact).
3. **Differential wall-clock A/B**: port one function, keep a switch, and measure frame time
   with it native vs translated. Because the port is bit-exact-verified, this is safe to do
   per function. That is the only way to replace the cost model's assumed loop multiplier.
4. **Counters at the driver seam** (`glDrawElements`/`glUniform*` calls per frame) to bound
   the render-driver share independently of the JIT.

Until (1) or (3) exists, every row below is a *hypothesis with a stated basis*, and the
ranking is a worklist, not a result.

---

## 4. Ranked shortlist

Tiers: **A** = on the per-frame call chain read from the disassembly; **B** = entered only
through a vtable slot that the per-frame path uses; **C** = pure math kernels reached by
static calls from A/B code (listed separately at the bottom).

`cost` is the model of §3(c); `share` is that cost as a percentage of the 219-row Tier A/B
set. `soft-float calls` is the exact number of `bl` sites to `__aeabi_*` imports inside the
function body (dbl = the double-precision subset); `other imports` excludes `__aeabi_*`.
`covered` refers to section 7.

| # | tier | function (demangled) | addr | bytes | insns | in-deg | loops (max) | virt | soft-float calls (dbl) | other imports | est. cost | share | covered |
|---|:--:|---|---|---:|---:|---:|---|---:|---|---:|---:|---:|---|
| 1 | A | Application::_CheckGamepad() | `0x00321164` | 6080 | 1520 | 1 | 34 (1485) | 0 | 152 (0) | 0 | 86560 | 25.61% | - |
| 2 | A | Application::_Update(int) | `0x0032c438` | 2188 | 546 | 1 | 13 (476) | 0 | 3 (0) | 1 | 12274 | 3.63% | - |
| 3 | A | glitch::scene::CSceneManager::registerNodeForRendering(glitch::scene::ISceneNode*, boost::intrusive_ptr<glitch::video::CMaterial> const&, void*, glitch::scene::E_SCENE_NODE_RENDER_PASS, glitch::core::vector3d<float> const*, int) | `0x0058f348` | 1604 | 401 | 0 | 21 (285) | 0 | 0 (0) | 1 | 10352 | 3.06% | - |
| 4 | A | SceneManager::_renderLists(glitch::video::IVideoDriver*) | `0x003587a0` | 2832 | 708 | 1 | 15 (593) | 0 | 0 (0) | 0 | 10340 | 3.06% | - |
| 5 | A | PerfCounters::Draw() | `0x00311914` | 3088 | 772 | 1 | 13 (704) | 1 | 59 (0) | 3 | 10272 | 3.04% | - |
| 6 | A | TouchScreenBase::ProcessEvents() | `0x0033c568` | 1776 | 444 | 3 | 10 (333) | 0 | 6 (0) | 1 | 5844 | 1.73% | - |
| 7 | A | glitch::scene::CSceneManager::renderLists(glitch::video::IVideoDriver*) | `0x00590660` | 2144 | 536 | 0 | 7 (469) | 0 | 0 (0) | 4 | 5545 | 1.64% | - |
| 8 | A | glitch::scene::CBatchMesh::sort(glitch::video::IVideoDriver const*) | `0x0057c19c` | 2988 | 747 | 1 | 13 (359) | 0 | 0 (0) | 7 | 5104 | 1.51% | - |
| 9 | A | glitch::scene::CBatchSceneNode::renderSolidBatch(glitch::video::IVideoDriver*, unsigned int) | `0x005800e8` | 1032 | 258 | 1 | 7 (201) | 2 | 0 (0) | 2 | 3518 | 1.04% | - |
| 10 | A | Savegame::UpdateJobs() | `0x00314734` | 1480 | 352 | 3 | 10 (224) | 2 | 0 (0) | 5 | 3445 | 1.02% | - |
| 11 | A | CXPlayerManager::Update() | `0x0052e784` | 752 | 188 | 1 | 10 (129) | 0 | 0 (0) | 3 | 2321 | 0.69% | - |
| 12 | A | Application::Update() | `0x0032ccc4` | 772 | 193 | 1 | 7 (134) | 0 | 3 (0) | 1 | 1881 | 0.56% | - |
| 13 | A | Application::_Draw() | `0x0032ade8` | 1116 | 264 | 1 | 5 (187) | 2 | 0 (0) | 1 | 1760 | 0.52% | - |
| 14 | A | glitch::scene::CBatchSceneNode::flushTransparentBatch(glitch::video::IVideoDriver*) | `0x005804f0` | 688 | 172 | 1 | 3 (122) | 0 | 0 (0) | 2 | 1109 | 0.33% | - |
| 15 | A | RootSceneNode::UpdateEnlargedViewFrustum(glitch::scene::CSceneManager*) | `0x0035c51c` | 616 | 154 | 1 | 5 (78) | 0 | 9 (0) | 0 | 991 | 0.29% | - |
| 16 | A | appUpdate | `0x00530fc8` | 392 | 98 | 1 | 2 (72) | 0 | 0 (0) | 2 | 447 | 0.13% | - |
| 17 | A | PerfCounters::Update(float) | `0x0031177c` | 408 | 102 | 1 | 3 (52) | 0 | 1 (0) | 1 | 380 | 0.11% | - |
| 18 | A | LightSetManager::Update() | `0x0040d5a0` | 564 | 141 | 1 | 1 (84) | 1 | 3 (0) | 2 | 305 | 0.09% | - |
| 19 | A | glitch::scene::ISceneNode::updateAbsolutePosition(bool) | `0x00597c60` | 248 | 62 | 7 | 2 (49) | 0 | 0 (0) | 1 | 267 | 0.08% | - |
| 20 | A | SceneManager::drawAll(glitch::scene::ISceneNode*) | `0x00359338` | 364 | 91 | 0 | 1 (66) | 0 | 0 (0) | 3 | 231 | 0.07% | - |
| 21 | A | glitch::scene::CAnimatedMeshSceneNode::buildFrameNr(unsigned int) | `0x006f5fd8` | 504 | 126 | 1 | 1 (35) | 0 | 28 (0) | 0 | 199 | 0.06% | - |
| 22 | A | glitch::scene::CSceneManager::update(float, bool) | `0x0058b9f0` | 232 | 58 | 0 | 1 (38) | 0 | 3 (0) | 0 | 137 | 0.04% | - |
| 23 | A | glitch::scene::CSceneManager::drawAll(glitch::scene::ISceneNode*) | `0x0058b7f4` | 152 | 38 | 0 | 1 (28) | 0 | 0 (0) | 0 | 95 | 0.03% | - |
| 24 | A | PlayerManager::Update() | `0x00378fb4` | 208 | 52 | 6 | 1 (24) | 0 | 0 (0) | 0 | 88 | 0.03% | - |
| 25 | A | SceneManager::_drawAll(glitch::scene::ISceneNode*) | `0x003592b0` | 136 | 34 | 1 | 1 (24) | 0 | 0 (0) | 0 | 82 | 0.02% | - |
| 26 | A | glitch::scene::CAnimatedMeshSceneNode::setCurrentFrame(float) | `0x006f6af8` | 132 | 33 | 0 | 0 (0) | 0 | 8 (0) | 0 | 26 | 0.01% | - |
| 27 | A | TouchScreenBase::update(double) | `0x0033b334` | 168 | 42 | 1 | 0 (0) | 0 | 2 (2) | 0 | 17 | 0.00% | - |
| 28 | A | EventManager::Update(double) | `0x0033900c` | 132 | 33 | 1 | 0 (0) | 0 | 0 (0) | 0 | 10 | 0.00% | - |
| 29 | A | StateMachine::Update(double) | `0x0033a7f4` | 92 | 23 | 2 | 0 (0) | 0 | 0 (0) | 0 | 7 | 0.00% | - |
| 30 | A | StateMachine::Draw() const | `0x0033a100` | 72 | 18 | 1 | 0 (0) | 0 | 0 (0) | 0 | 5 | 0.00% | - |
| 31 | A | StateMachine::Draw2D() const | `0x0033a148` | 52 | 13 | 1 | 0 (0) | 0 | 0 (0) | 0 | 4 | 0.00% | - |
| 32 | A | Java_com_gameloft_android_GAND_GloftD2SS_GameRenderer_nativeRender | `0x005311a0` | 40 | 10 | 0 | 0 (0) | 0 | 0 (0) | 0 | 3 | 0.00% | - |
| 33 | A | glitch::scene::CAnimatedMeshSceneNode::onAnimate(unsigned int) | `0x006f6540` | 36 | 9 | 0 | 0 (0) | 0 | 0 (0) | 0 | 3 | 0.00% | - |
| 34 | B | glitch::video::CProgrammableGLDriver<glitch::video::CGLSLShaderHandler>::commitCurrentMaterialAutomaticParameters(glitch::video::CGLSLShader const*, glitch::video::CVertexStreams const*, unsigned char const*) | `0x005b78b8` | 4628 | 1157 | 1 | 53 (856) | 0 | 35 (0) | 55 | 54530 | 16.13% | - |
| 35 | B | glitch::video::CBatchDriver::thisAppendBatch(boost::intrusive_ptr<glitch::video::CVertexStreams const> const&, glitch::video::CPrimitiveStream const&, boost::intrusive_ptr<glitch::scene::CMeshBuffer const> const&) | `0x005a6fe0` | 6816 | 1704 | 1 | 24 (1612) | 4 | 39 (0) | 168 | 48506 | 14.35% | - |
| 36 | B | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode(glitch::scene::ISceneNode*, unsigned int) | `0x006c7f1c` | 2832 | 708 | 0 | 9 (598) | 0 | 130 (0) | 1 | 8331 | 2.46% | - |
| 37 | B | glitch::scene::CSceneNodeAnimatorCameraMaya::animateNode(glitch::scene::ISceneNode*, unsigned int) | `0x006c9934` | 2396 | 599 | 0 | 8 (528) | 0 | 98 (0) | 0 | 7450 | 2.20% | - |
| 38 | B | void glitch::video::CProgrammableGLDriver<glitch::video::CGLSLShaderHandler>::commitCurrentMaterialParametersAux<glitch::video::CGlobalMaterialParameterManager>(glitch::video::CGLSLShader const*, glitch::video::CGlobalMaterialParameterManager const*, glitch::video::SShaderParameterBinding const*, glitch::video::SShaderParameterBinding const*) | `0x005b2fac` | 1524 | 381 | 1 | 14 (240) | 0 | 8 (0) | 14 | 6134 | 1.81% | - |
| 39 | B | void glitch::video::CProgrammableGLDriver<glitch::video::CGLSLShaderHandler>::commitCurrentMaterialParametersAux<glitch::video::CMaterial>(glitch::video::CGLSLShader const*, glitch::video::CMaterial const*, glitch::video::SShaderParameterBinding const*, glitch::video::SShaderParameterBinding const*) | `0x005b4da0` | 1440 | 360 | 4 | 14 (238) | 0 | 8 (0) | 14 | 6053 | 1.79% | - |
| 40 | B | glitch::collada::detail::CColladaSoftwareSkinTechnique::skin(glitch::collada::SSkinBuffer&, glitch::scene::CMeshBuffer*) | `0x0066ff48` | 2840 | 710 | 0 | 13 (591) | 0 | 73 (0) | 0 | 5918 | 1.75% | - |
| 41 | B | glitch::collada::CCoronasSceneNode::render(void*) | `0x006e5d14` | 4904 | 1226 | 0 | 2 (1194) | 1 | 268 (1) | 6 | 5544 | 1.64% | - |
| 42 | B | glitch::collada::CParticleSystemSceneNode::render(void*) | `0x006572b8` | 1308 | 327 | 0 | 4 (282) | 3 | 0 (0) | 11 | 2229 | 0.66% | - |
| 43 | B | glitch::collada::CGlitchNewParticleSystemSceneNode::render(void*) | `0x00643ea8` | 1308 | 327 | 0 | 4 (282) | 3 | 0 (0) | 11 | 2229 | 0.66% | - |
| 44 | B | glitch::scene::CBillboardSceneNode::render(void*) | `0x00580f58` | 2036 | 509 | 1 | 1 (494) | 1 | 79 (1) | 2 | 1799 | 0.53% | - |
| 45 | B | glitch::scene::CSkyBoxSceneNode::render(void*) | `0x006ce53c` | 1156 | 289 | 0 | 4 (260) | 1 | 15 (0) | 3 | 1654 | 0.49% | - |
| 46 | B | XrayModularSkinnedMeshSceneNode::render(void*) | `0x00363138` | 1084 | 271 | 0 | 3 (179) | 0 | 0 (0) | 1 | 1646 | 0.49% | - |

rows in ranked set: 219 ; sum of est. cost: 337989

### 4.1 The matrix/quaternion kernels — the brief's first category

These are the functions the brief expected to be the 22–26× case. They are *not* the
costliest under §3(c) (they are straight-line, so `loops3 ≈ 0`), but they are the densest
concentration of compiler-emitted soft-float calls in the engine, they are called from the
per-frame driver path, and they are the cheapest possible ports (no loops, no allocation,
pure value semantics). **[derived]:**

| function | mangled | addr | bytes | insns | soft-float calls (dbl) | callers |
|---|---|---|---:|---:|---|---|
| `glitch::core::detail::CMatrix4Base<float>::mult(CMatrix4Base<float> const&) const` | `_ZNK6glitch4core6detail12CMatrix4BaseIfE4multERKS3_` | `0x0035e118` | 1,992 | 498 | **112** (0) — 64 `fmul`, 48 `fadd` | `CMatrix4<float>::operator*` `0x0035e998` (88 B, 0 helper calls of its own) → 4 in-degree |
| `glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck(…)` | `_ZN6glitch4core6detail12CMatrix4BaseIfE20setbyproduct_nocheckERKS3_S5_` | `0x0040ea54` | 1,632 | 408 | **112** (0) — 64 `fmul`, 48 `fadd` | `multEq` `0x0050f6d8`, `SViewFrustum::setTransformState` `0x00582348`, `setbyproduct` `0x005822c8`, `CameraBase::GetScreenCoord` `0x0040f714` |
| `glitch::core::CMatrix4<float>::getInverse(CMatrix4<float>&) const` | `_ZNK6glitch4core8CMatrix4IfE10getInverseERS2_` | `0x003232c0` | 2,080 | 520 | **130** (0) — 79 `fmul`, 38 `fsub`, 11 `fadd`, 1 `fdiv` | 6 in-degree; reached from `CProgrammableGLDriver…` `0x005b78b8` |
| `CMatrix4<float>::getInverse(…)` second instance | same demangled name | `0x00663ce8` | 2,120 | 530 | **0** — this body contains 130 double-precision op *instructions* but no `__aeabi_*` calls, i.e. it is a **different implementation** (double-arithmetic inline form), not an ICF twin | 0 in-degree |
| `glitch::core::quaternion::slerp(quaternion,quaternion,float)` | `_ZN6glitch4core10quaternion5slerpES1_S1_f` | `0x00612d00` | 980 | 245 | 56 (0) + `sinf`×5 | 8 in-degree |
| `glitch::core::quaternion::operator*(quaternion const&) const` | `_ZNK6glitch4core10quaternionmlERKS1_` | `0x0060dd34` | 448 | 112 | 28 (0) | 13 in-degree |
| `glitch::core::quaternion::operator*(vector3d<float> const&) const` | `_ZNK6glitch4core10quaternionmlERKNS_8vector3dIfEE` | `0x0035bc90` | 532 | 133 | 31 (0) | 4 in-degree |
| `glitch::core::vector3d<float>::normalize()` | `_ZN6glitch4core8vector3dIfE9normalizeEv` | `0x0035e8e0` | 184 | 46 | 10 (0) + `sqrtf` | **62 in-degree**, already reconstructed |
| `glitch::core::quaternion::set(float,float,float)` | `_ZN6glitch4core10quaternion3setEfff` | `0x0035c9d8` | 564 | 141 | 26 (**23**) + `sin`×3, `cos`×3 | 11 in-degree, already reconstructed |
| `glitch::core::CMatrix4<float>::getRotationDegrees() const` | `_ZNK6glitch4core8CMatrix4IfE18getRotationDegreesEv` | `0x00432bbc` | 668 | 167 | 26 (**19**) + `atan2`×3 | 3 in-degree, already reconstructed |
| `glitch::core::quaternion::operator=(CMatrix4<float> const&)` | `_ZN6glitch4core10quaternionaSERKNS_8CMatrix4IfEE` | `0x0050eeb4` | 676 | 169 | 48 (0) + `sqrtf`×4 | 7 in-degree, already reconstructed |

**Read this honestly.** `quaternion::set` and `CMatrix4::getRotationDegrees` are the only
matrix-family bodies with material *double* content (23 and 19 double-op call sites), and
both are **already reconstructed and bit-exact verified** (section 7). Everything else in the
table above is single precision. If the translator's 22–26× double figure is real and applies
to this program at all, the total pool it could apply to is 675 static double call sites
engine-wide — not a useful optimisation budget. If it is not real here, section 1's reading
holds and the budget is instruction volume.

### 4.2 The animation/skinning path

The skinning kernels are large, loop-dense, and reached per frame through
`ISceneNode::render` (+0x38) → `onPrepareBufferForRendering` → `skin`:

| function | addr | bytes | insns | loops (max span) | soft-float calls | note |
|---|---|---:|---:|---|---:|---|
| `glitch::collada::detail::CColladaSoftwareSkinTechnique::skin(SSkinBuffer&, CMeshBuffer*)` | `0x0066ff48` | 2,840 | 710 | 13 (591) | 73 | the CPU skinning kernel; reached via `onPrepareBufferForRendering` |
| `glitch::collada::CModularSkinnedMesh::skin(unsigned int)` | `0x00647fcc` | 896 | 224 | 4 (192) | 0 | `+0x54` slot, 131 dispatch sites |
| `glitch::collada::CSkinnedMesh::skin(unsigned int)` | `0x00664ee0` | 292 | 73 | 0 | 0 | thin wrapper, 2 virtual sites |
| `CColladaHardwareQuatSkinTechnique::skin` | `0x0066dc40` | 652 | 163 | 2 (131) | 0 | |
| `CColladaHardwareMatrixSkinTechnique::skin` | `0x0066c278` | 432 | 108 | 1 (85) | 0 | |
| `CColladaHardwareTextureSkinTechnique::skin` | `0x0066ec14` | 452 | 113 | 2 (81) | 0 | |
| `glitch::collada::CModularSkinnedMesh::onPrepareBufferForRendering(…)` | `0x00647774` | 332 | 83 | 2 (65) | 0 | per-frame driver |
| `SkinnedMeshSceneNode::prepareSkinForRendering()` | `0x0035ad8c` | 208 | 52 | 0 | 0 | `+0x118` slot (29 dispatch sites); `ModularSkinnedMeshSceneNode::` twin at `0x0035acb4` |

The keyframe accessor/search bodies that were reconstructed by `port/asset-payloads`
(`SAnimationAccessor::getKeyTime`, `findKeyFrameNo<…>`, `findKeyFrameNoEx<…>`,
`SAnimationSegment::getData`, `animation_track::CInterpreter<…>`) form the *other* half of
this path; their coverage status is in section 7.

### 4.3 The render-driver seam

`glitch::video::IVideoDriver` has 134 virtual slots, fully resolved
(`DH2-recon/STATUS.md:364-366`). The concrete implementation the device runs is
`CProgrammableGLDriver<CGLSLShaderHandler>` (wrapped by `CCommonGLDriver<…>`), reached
through `CSceneManager::renderLists` → `IVideoDriver::setMaterial` / driver `draw`:

| function | addr | bytes | insns | loops (max span) | soft-float | other imports | evidence |
|---|---|---:|---:|---|---:|---:|---|
| `CProgrammableGLDriver<CGLSLShaderHandler>::commitCurrentMaterialAutomaticParameters(…)` | `0x005b78b8` | 4,628 | 1,157 | 53 (856) | 35 | 55 | **41 `glUniformMatrix4fv`** sites; caller `0x005b8acc` |
| `CProgrammableGLDriver<…>::commitCurrentMaterialParametersAux<CGlobalMaterialParameterManager>(…)` | `0x005b2fac` | 1,524 | 381 | 14 (240) | 8 | 14 | `glUniform4fv`×3, `glUniformMatrix4fv`×2 |
| `CProgrammableGLDriver<…>::commitCurrentMaterialParametersAux<CMaterial>(…)` | `0x005b4da0` | 1,440 | 360 | 14 (238) | 8 | 14 | 4 in-degree |
| `CBatchDriver::thisAppendBatch(…)` | `0x005a6fe0` | 6,816 | 1,704 | 24 (1,612) | 39 | **168** | largest loop body in the engine; the 168 include **158 `puts`** sites — confirm whether logging is live in the shipping build before porting (§5.5) |
| `CMaterialRendererManager::endMaterialRenderer()` | `0x005dddb4` | 4,204 | 1,051 | 25 (994) | 0 | 0 | 5 callers incl. `SDefaultEndOfBatchCallback::finalize` `0x0058d3d4`; its own callee `CMaterialRendererManager::autoAddAndBindParameter` `0x005dd250` (1,044 B, 11 loops) |
| `CSceneManager::registerNodeForRendering(…)` | `0x0058f348` | 1,604 | 401 | 21 (285) | 0 | 1 | the render-list walk |
| `CSceneManager::renderLists(IVideoDriver*)` | `0x00590660` | 2,144 | 536 | 7 (469) | 0 | 4 | `+0x64` slot |
| `SceneManager::_renderLists(IVideoDriver*)` | `0x003587a0` | 2,832 | 708 | 15 (593) | 0 | 0 | the fork's own render-list walk |

Note the mismatch to flag to whoever owns the device build: `CProgrammableGLDriver` issues
only 41 `glUniformMatrix4fv` sites statically, and the GL driver is **0.16%** of CPU
**[measured]**. Unless that measurement excludes driver-side CPU work (uniform packing,
state caching), the commit path is *not* where 82.7% of the time can be. It is in the table
because the brief asks for it and because 53 loops × 856-instruction span is a lot of guest
code if it does run per material per frame.

### 4.4 Pure math kernels reached from A/B code (Tier C)

| function | addr | bytes | insns | in-deg | soft-float (dbl) | covered |
|---|---|---:|---:|---:|---|---|
| `quaternion::operator*(vector3d<float> const&) const` | `0x0035bc90` | 532 | 133 | 4 | 31 (0) | engine-math (verified) |
| `quaternion::set(float,float,float)` | `0x0035c9d8` | 564 | 141 | 11 | 26 (23) | - |
| `quaternion::fromAngleAxis(float, vector3d<float> const&)` | `0x00410e48` / `0x0060cdbc` | 92 / 104 | 23 / 26 | 2 / 18 | 3 / 4 (+`sinf`,`cosf`) | engine-math (verified) |
| `quaternion::getMatrix(CMatrix4<float>&) const` | `0x00432984` / `0x005602d0` (the `_transposed` overload) | 436 | 109 | 4 / 5 | 23 (0) | engine-math (both verified) |
| `quaternion::getMatrix() const` | `0x006d162c` | 36 | 9 | 2 | 0 | engine-math (verified) |
| `CMatrix4<float>::getRotationDegrees() const` | `0x00432bbc` | 668 | 167 | 3 | 26 (19) + `atan2`×3 | engine-math (verified, 750 cases) |
| `quaternion::toEulerDegree(vector3d<float>&)` | `0x00432e58` | 144 | 36 | 2 | 0 | engine-math (verified) |
| `quaternion::operator=(CMatrix4<float> const&)` | `0x0050eeb4` | 676 | 169 | 7 | 48 (0) + `sqrtf`×4 | engine-math (verified) |
| `quaternion::normalize()` | `0x0035c8f0` | 232 | 58 | 3 | 13 (0) + `sqrtf` | engine-math (verified) |
| `vector3d<float>::normalize()` | `0x0035e8e0` | 184 | 46 | 62 | 10 (0) + `sqrtf` | engine-math (verified) |
| `vector3d<float>::operator/(…)` / `operator/=` | `0x0059a294` / `0x005a24b4` | 80 / 68 | 20 / 17 | 3 / 2 | 3 / 3 | engine-math (verified) |
| `vector3d<float>::rotateXYBy/YZBy/XZBy(double, …)` | `0x00630bfc` / `0x00630cf8` / `0x00630de8` | 252 / 240 / 256 | 63 / 60 / 64 | 11 / 10 / 12 | 15 / 15 / 15 (double angle argument) | engine-math (verified) |
| `vector3d<float>::getHorizontalAngle() const` | `0x006c7b6c` | 388 | 97 | 2 | 0 | engine-math (verified) |
| `quaternion::slerp(quaternion,quaternion,float)` | `0x00612d00` | 980 | 245 | 8 | 56 (0) + `sinf`×5 | engine-math (verified, 1,000 cases) |

### 4.5 Everything else with large per-frame-priced volume (Tier B/C, not covered)

| function | addr | bytes | insns | loops (max) | soft-float | why it is listed |
|---|---|---:|---:|---|---:|---|
| `Application::_CheckGamepad()` | `0x00321164` | 6,080 | 1,520 | 34 (1,485) | 152 | 199 `bl` per call; on the per-frame chain |
| `Application::_Update(int)` | `0x0032c438` | 2,188 | 546 | 13 (476) | 3 | per-frame update fan-out (36 callees) |
| `PerfCounters::Draw()` | `0x00311914` | 3,088 | 772 | 13 (704) | 59 | per-frame HUD; 23 `fmul`, 12 `i2f`, 10 `f2iz` |
| `TouchScreenBase::ProcessEvents()` | `0x0033c568` | 1,776 | 444 | 10 (333) | 6 | per-frame input |
| `CBatchMesh::sort(IVideoDriver const*)` | `0x0057c19c` | 2,988 | 747 | 13 (359) | 0 | per-frame batch sort; `memcpy`×5, `memmove`×2 |
| `CBatchSceneNode::renderSolidBatch` / `flushTransparentBatch` | `0x005800e8` / `0x005804f0` | 1,032 / 688 | 258 / 172 | 7 (201) / 3 (122) | 0 / 0 | per-frame batch draw; 2 virtual sites each |
| `CBillboardSceneNode::render(void*)` | `0x00580f58` | 2,036 | 509 | 1 (494) | 79 | 29 `fadd`, 24 `fsub`, 23 `fmul` |
| `CSceneNodeAnimatorCameraFPS/Maya::animateNode(…)` | `0x006c7f1c` / `0x006c9934` | 2,832 / 2,396 | 708 / 599 | 9 (598) / 8 (528) | 130 / 98 | per-frame camera animators (`+0x24` slot) |
| `RootSceneNode::UpdateEnlargedViewFrustum(…)` | `0x0035c51c` | 616 | 154 | 5 (78) | 9 | per-frame frustum update; calls `CCameraSceneNode::recalculateMatrices` `0x00583280` (692 B, 33 imports) |
| `Savegame::UpdateJobs()` | `0x00314734` | 1,480 | 352 | 10 (224) | 0 | per-frame (job pump); `fprintf`×2 |
| `CCoronasSceneNode::render(void*)` | `0x006e5d14` | 4,904 | 1,226 | 2 (**1,194**) | **268** | the single largest loop body on the per-frame render path |
| `CColladaSoftwareSkinTechnique::skin` | `0x0066ff48` | 2,840 | 710 | 13 (591) | 73 | see 4.2 |

### 4.6 Demoted (cost is real, per-frame relevance is not)

`pixel_format::convert` `0x005f95ac` (17,128 B / 71 loops), `pixel_format::convertPacked`
`0x005f4fe8` (17,860 B / 70 loops), `convertPackedImpl<…>` `0x005eeef4` / `0x005f0f60` /
`0x005f2fdc` (≈8.3 KB each), `CImageLoaderBMP::loadImage` `0x006034ec`,
`CImageLoaderPng::loadImage` `0x006050dc`, `CImageLoaderPVR::loadTextureHeader` `0x00605b10`,
`CTerrainSceneNode::loadHeightMap` `0x006d4454`, `createMeshWithTangents` `0x0059c970`,
`createSphereMesh`/`createCylinderMesh`/`createConeMesh`/`createCubeMesh`
`0x006d7808`/`0x006d945c`/`0x006d9fc4`/`0x006d8cb4`, `CGlitchNewParticleSystemSceneNode::initParticleSystem`
`0x00642e58`, `CBatchMesh::save`/`load` `0x0057cd48`/`0x0057db94`, `glitch::io::save`/`loadVS`,
`CColladaFactory::createParticleSystem` `0x00634c64`, `createMaterialRendererForProfile<…>`
`0x006357a0`/`0x006361e8`. All are asset/geometry construction or serialisation. They win on
any volume metric; they run at load, not per frame **[inferred]** (and the 0.16% GL figure
**[measured]** is consistent with that).

---

## 5. Weaknesses of this method (explicit)

1. **No dynamic attribution at all.** Cost ranks *how much code exists and how it is
   structured*, not how often it runs. A 6 KB function called once per frame outranks a 60-byte
   function called 100,000 times per frame; the model cannot tell them apart.
2. **The `3.0 × loop-span` multiplier is an assumption, not a measurement.** It drives the
   ranking of every loop-heavy body. If the true average iteration count is 1 instead of 3,
   the bulk-data functions move down and the straight-line math kernels move up. Section 3(e)
   item 3 is the fix.
3. **Virtual calls hide the call graph.** 11,161 dispatch sites (vs 119,881 direct `bl`) are
   invisible to in-degree. `indeg = 0` in the table therefore means "never called directly",
   *not* "never called". All of Tier B is in that state.
4. **Symbol-name drift.** The engine was linked with ICF/folding; `DH2-recon/STATUS.md:113-118`
   documents a vector-destructor symbol sitting over a `return 0x2710` body. Every address in
   this table came from the disassembly and the ELF symbol table, and the coverage re-check in
   §7 re-hashed raw bytes, so the *bytes* are trustworthy; the *names* are the linker's claim.
   Known consequence: `engine-resources` has 217 byte-identical twins — e.g. the 8-byte body at
   `0x0056eb80` (`CMemoryReadFile::isAllInMemory`) is byte-identical to bodies at `0x00733a44`,
   `0x00739f34`, `0x00455908`, `0x00888fc8`, `0x005148f0`, `0x0039f37c`, `0x003e3e14`,
   `0x00888718`, which carry different symbols.
5. **`puts`/`fprintf` sites.** `CBatchDriver::thisAppendBatch` (`0x005a6fe0`) contains 158
   static `puts` call sites and `Savegame::UpdateJobs` two `fprintf`. If those are live in the
   shipping build, a large share of that function's cost is logging, and porting it natively
   would make the logging *faster* rather than removing it — check `DebugSwitches::load()`
   (`0x00337888`, reached from `appUpdate`) before trusting any ranking that includes them.
6. **The 15.1% translator figure cannot be validated here.** It is a device measurement with an
   attribution the device cannot make either; section 1 uses it only for an order-of-magnitude
   ceiling, and states the tension with the published cost model rather than resolving it.

---

## 6. Recommended first three functions

Chosen for (i) per-frame certainty, (ii) narrow, side-effect-free semantics that the existing
verification loop can gate bit-exactly, and (iii) a defensible share of the 21.25 ms guest
slice. Estimated removal is given as `f × 21.25 ms` where `f` is the function's share of the
guest slice — and `f` is the unknown, so a *range* is given with the assumption stated.

### 1. `_ZNK6glitch4core6detail12CMatrix4BaseIfE4multERKS3_` — `CMatrix4Base<float>::mult`, `0x0035e118`, 1,992 B

* **[derived]** 498 instructions, no loops, 112 soft-float call sites (64 `__aeabi_fmul`,
  48 `__aeabi_fadd`), plus 2 `memmove`; the classic 4×4 multiply with a full matrix staged on
  the stack (prologue at `0x0035e118` spills 16 words before the arithmetic).
* **[derived]** One static caller, `CMatrix4<float>::operator*` `0x0035e998` (88 B), which has
  4 in-degree; the operand layout is already fixed by the verified `engine-math` port
  (`port/engine-math/README.md:22`: "matrix stores sixteen floats followed by a byte at
  offset 64").
* **[inferred]** Highest per-call helper density of any function in the engine, and matrices
  are multiplied once per node with a transform change per frame. **Confidence that it is
  called per frame: high. Confidence that it is a large share of the frame: low** — there is
  no measurement of how many times per frame it runs.
* **Why first:** it is the cheapest possible port (no loops, no allocation, no branches on
  data), it is the exact body the 22–26× claim is about, and it is trivially expressible as
  ARM64 `fmla`/`fmul` on 32 registers — one instruction per product instead of a call.
  **Estimated removal: 0.1–0.5 ms (0.5–2.5% of the 21.25 ms guest slice)** under a guess of
  500–2,000 calls/frame; the call-site reduction per invocation is 112 → 0.

### 2. `_ZNK6glitch4core8CMatrix4IfE10getInverseERS2_` — `CMatrix4<float>::getInverse`, `0x003232c0`, 2,080 B

* **[derived]** 520 instructions, 1 loop, **130 soft-float call sites** (79 `fmul`, 38 `fsub`,
  11 `fadd`, 1 `fdiv`), 1 `memcpy`, 6 in-degree including
  `CProgrammableGLDriver<CGLSLShaderHandler>::commitCurrentMaterialAutomaticParameters`
  `0x005b78b8` — i.e. it sits on the driver-side matrix path, which is a per-frame seam by
  construction.
* **[derived]** A **second, different** `getInverse` body sits at `0x00663ce8` (2,120 B, 530
  instructions, 0 in-degree) and contains **no `__aeabi_*` calls at all** — it uses inline
  double arithmetic instead of the soft-float helper calls. Same demangled name, different
  implementation: decide which one the shipping build actually links before porting, and do
  not assume one covers the other.
* **[inferred]** Matrix inverses on the render path are typically per-camera/per-node, not
  per-vertex. **Confidence that it runs per frame: medium-high** (its caller is per-material).
  **Confidence in the share: low.**
* **Why second:** 130 call sites collapse to ~130 arithmetic instructions, it has exactly one
  data-dependent branch structure (the complement-expansion loop), and the verified
  `getRotationDegrees`/`operator=` ports already pin down the matrix layout and the
  row/column convention this function must match — so the differential harness work is
  mostly done.
  **Estimated removal: 0.1–0.4 ms (0.5–2%)**, driven by call count, not by arithmetic.

### 3. `_ZN11Application13_CheckGamepadEv` — `Application::_CheckGamepad`, `0x00321164`, 6,080 B

* **[derived]** On the per-frame chain by direct evidence: the call graph records the edge
  `0x0032c438 (Application::_Update(int)) --call--> 0x00321164`, and `Application::_Update(int)`
  is itself called from `Application::Update()` `0x0032ccc4`, which is called from
  `appUpdate` `0x00530fc8`
  (`DH2-recon/reports/calls.csv`; `DH2Work-scratch/metrics2.json` `funcs["321164"].callers == ["0x32c438"]`).
* **[derived]** 1,520 instructions, 199 `bl` sites of which **152 are soft-float**
  (76 `__aeabi_fadd`, 38 `__aeabi_fmul`, 38 `__aeabi_fcmpge`) — the single densest
  soft-float function on the per-frame path, and the highest `cost` in the model.
* **[derived]** The body is one repeated shape per axis/button:
  `ldr r1,[r4,#0x2e0]; ldr r0,[r4,#0x2e4]; bl __aeabi_fadd; mov r1,#0x3f800000; bl __aeabi_fadd;
  mov r1,#0x3f000000; bl __aeabi_fmul; bl __aeabi_fcmpge;` — three helper calls plus a compare
  for what native ARM64 does with two `fadd`/`fmul` and one `fcmp`. Repeated ~38 times.
* **[derived] It has exactly one local callee.** Of its 199 `bl` sites, 152 go to `__aeabi_*`
  imports and the remaining 47 all go to the same address, `0x00338ebc`
  (`EventManager::Raise(IEvent const&) const`, 336 B, 84 instructions, in-degree 9). So the
  observable side effect is "raise an input event" — which is what an event-driven engine
  should do — and there is no hidden dispatch or allocation inside it.
* **[inferred]** A gamepad poll runs every frame by construction (there is no event to wait
  for). This is the most defensible per-frame certainty in the whole table.
  **Confidence that it runs every frame: very high. Confidence in its share: low** — the model
  ranks it first only because `loops3` is large; if the body really is straight-line, its share
  is its instruction count (1,520 of ~1.49 M static instructions) times its call count, i.e.
  likely **0.3–0.5 ms total**, not the 5.4 ms the 25.6% model share implies. It is listed third
  rather than first precisely because that model share is not trustworthy.
* **Why third anyway:** every one of its 199 call sites is either an import or the one local
  helper `EventManager::Raise`, and it stores only into its own `this` fields — which makes it
  the least risky large body to port, and it is the only Tier A function whose entire
  arithmetic cost is soft-float calls (152) of which **zero** are double precision. Porting it
  is also the sharpest available test of section 1: if the optimised build's frame time barely
  moves, the "FP helpers are native, the cost is instruction volume" reading is confirmed; if
  it moves by the model's prediction, the published cost model applies after all.
  **Estimated removal: 0.3–0.5 ms (1.5–2.5% of the guest slice)**, i.e. ≈2% of the frame —
  about 25.7 → 25.2 ms.

**Sanity check on all three.** Together they remove roughly **0.5–1.4 ms of 25.7 ms
(≈2–5%)**, taking ~25 FPS to ~25.5–26.4 FPS — real, worth doing, and *nowhere near* the
frame budget. Nothing in this shortlist can be expected to produce a large FPS jump on its
own; only cumulative coverage of the guest slice can (section 1's ceiling: 20% of the slice →
46.6 FPS). The correct target for a "performance port" is therefore the top ~20–40 functions
in section 4 by evidence tier, ported and differentially verified in dependency order, with
the JIT-side guest-PC histogram of section 3(e) built *first* so the order can be corrected
after the first ten.

---

## 7. Coverage cross-check — what is already reconstructed and differentially verified

Three modules in the sibling project already contain originals translated to C++ with a
differential harness. **[derived] I independently re-read the pristine `.so` bytes at every
recorded address and re-computed SHA-256** (`DH2Work-scratch/cover.py`); all 86 recorded
addresses hash-match the module's own recorded hash:

| module | records | hash match | complete bodies | partial bodies | recorded comparisons | report |
|---|---:|---:|---:|---:|---:|---|
| `DH_sc-pr/port/engine-math` | 20 | **20 / 20** | 20 | 0 | **21,477** (0 mismatches) | `reports/engine-math-validation.json` |
| `DH_sc-pr/port/engine-resources` | 36 | **36 / 36** | 35 | 1 (`File::Init()` whole-buffer branch) | **10,449** stream comparisons + 5,154,412 + 27,812 pointer comparisons, 0 mismatches | `reports/engine-resources-validation.json` |
| `DH_sc-pr/port/asset-payloads` | 30 | **30 / 30** | 26 declared + 4 partial-evidence records | 4 (`inner animation-data relocation only`) | **271,970** (0 mismatches) | `reports/asset-payloads-arm-validation.json` |
| **distinct addresses** | **86** | 86 / 86 | 84 | 2 | | |

Per-module declared counts confirm the brief's numbers: `engine-math` declares
`reconstructed_physical_function_starts = 20`; `engine-resources` declares
`complete_function_count = 35` / `partial_function_count = 1`;
`asset-payloads` declares `complete_function_count = 26` /
`partial_evidence_function_count = 4` (`original-functions.json`, top level).
No address is claimed by two modules.

* **`engine-math`** (`port/engine-math/README.md:3-5`, `:55`): "**20 original physical function
  starts** … `original-functions.json` maps each entry point to its original mangled symbol,
  ELF address, size and machine-code hash"; validation "passed **21,477 original-ARM32 versus
  compiled-ARM64 comparisons**, with zero mismatches. It executed all **1,704 ARM instruction
  addresses** in the twenty original function ranges"
  (`reports/engine-math-validation.json`: `total_comparisons = 21477`, `mismatches = 0`,
  sum of `original_instruction_addresses_executed` = 1,704 = sum of `original_arm_words_in_range`).
  Covered addresses — the complete 20-row list from
  `port/engine-math/original-functions.json`, each re-hashed by me against the pristine bytes
  and each matched to its disassembly record (`<addr> <symbol> <bytes>`):
  `0x0035bc90` `quaternion::operator*(vector3d<float> const&)` 532;
  `0x0035c8f0` `quaternion::normalize()` 232;
  `0x0035c9d8` `quaternion::set(float,float,float)` 564;
  `0x0035e8e0` `vector3d<float>::normalize()` 184;
  `0x00410e48` `quaternion::fromAngleAxis` 92;
  `0x00432984` `quaternion::getMatrix(CMatrix4<float>&) const` 436;
  `0x00432bbc` `CMatrix4<float>::getRotationDegrees() const` 668;
  `0x00432e58` `quaternion::toEulerDegree(vector3d<float>&)` 144;
  `0x0050eeb4` `quaternion::operator=(CMatrix4<float> const&)` 676;
  `0x005602d0` `quaternion::getMatrix_transposed(CMatrix4<float>&) const` 436;
  `0x0059a294` `vector3d<float>::operator/(…) const` 80;
  `0x005a24b4` `vector3d<float>::operator/=(…)` 68;
  `0x0060cdbc` `quaternion::fromAngleAxis` (second instance) 104;
  `0x0060dd34` `quaternion::operator*(quaternion const&) const` 448;
  `0x00612d00` `quaternion::slerp(quaternion,quaternion,float)` 980;
  `0x00630bfc` / `0x00630cf8` / `0x00630de8` `vector3d<float>::rotateXYBy/YZBy/XZBy(double, …)` 252/240/256;
  `0x006c7b6c` `vector3d<float>::getHorizontalAngle() const` 388;
  `0x006d162c` `quaternion::getMatrix() const` 36.
  **Do not redo these** — the two `getInverse` bodies and `CMatrix4Base<float>::mult` /
  `setbyproduct_nocheck` are the matrix work that is *not* covered.
  Caveat worth carrying: `README.md:63-65` records that 16 of the 19 recovered pseudocode
  bodies contain `WARNING: Subroutine does not return` at FP-helper calls — the Ghidra export
  could not model the `__aeabi_*` returns, which is the same soft-float finding as §1 arriving
  from the other direction.
* **`asset-payloads`** covers the animation accessor/search layer and the mesh readers. The 30
  records split by their own `scope` field into **26 `complete function`** and 4 partial
  evidence records (`0x00645588` `CMesh::CMesh` — `deferred buffer selection and mesh fields
  only`; `0x0060bee4` `SAnimationSegment::getData` — `inner animation-data relocation only`;
  `0x006bccf0` `addStream` and `0x006bcf80` `CMeshBuffer::CMeshBuffer` — stream/primitive field
  scopes only). Validation: `complete_accessor_search_functions = 26`,
  `partial_mesh_relocation_evidence_functions = 4`, `comparisons = 271970`, `mismatches = 0`.
  Complete coverage includes `0x00669e00`–`0x0066a004` accessor getters (`getOutput`
  in-degree 147, `getDefaultValue` 124, `hasDefaultValue` 122, `getKeyTime` 6, `getOffsets`,
  `getScales`, `getStart`, `getEnd`, `getLength`, `getAnimator`, `getType`, `getChannel`,
  `getChannelsCount`, `getTimeInternalType`, `getInterpolationType`, `getOffsetScaleType`,
  `getTarget`), `0x00669f0c` `getKeyTime(int,int)`, and the eight
  `findKeyFrameNo`/`findKeyFrameNoEx` instantiations at `0x0066a1f0`, `0x0066a2ac`,
  `0x0066a6d0`, `0x0066a788`, `0x0066a7c4`, `0x0066abb8`, `0x0066ac78`, `0x0066acb4`.
  **These are the keyframe-path functions the brief asks about, and they are already done** —
  what is *not* done is everything that consumes them:
  `animation_track::CInterpreter<…>` / `CVirtualEx<…>` / `CApplyValueEx<…>` (≈60 small bodies
  in the `0x0061xxxx`–`0x0062xxxx` range), `CBlender<quaternion,1,…>` `0x006130d4`,
  `CVector3dEx::getBlendedValueEx` `0x006e3fb0`, and the animator `animateNode` bodies.
  The `CMeshBuffer::CMeshBuffer` pair `0x006bcf80` / `0x006bd9f8` is the one to watch: same
  demangled name, both 2,680 B, but they are **not** byte-identical twins (`0x006bcf80` is a
  partial record with in-degree 0, `0x006bd9f8` has in-degree 2), so treat them as two bodies.
* **`engine-resources`** covers the reader layer, not the render path:
  `CMemoryReadFile::*` `0x0056eb6c`–`0x0056ecc4` (11 bodies, all in-degree 0),
  `CLimitReadFile::*` `0x006b44e8`–`0x006b46f0` (8 bodies, all in-degree 0),
  `CColladaDatabase::get*` `0x0060e2a8`–`0x0060e4a0` (16 one-line accessors, in-degree 0–6),
  and `glitch::res::File::Init()` `0x0069a40c` (1,108 B, 277 insns, 11 loops) whose `scope`
  field reads `whole-buffer branch only` — the one partial record. Note the in-degree
  asymmetry: the animation accessors run hot (`SAnimationAccessor::getOutput` 147,
  `hasDefaultValue` 122, `getDefaultValue` 124) while every reader method has in-degree 0 —
  consistent with readers being reached through the `IReadFile` interface rather than by name.

**Bottom line for the cross-check:** the *leaf math* and the *asset/keyframe accessor layer*
are done and verified; the *consumers* — matrix composition, skinning loops, batch/driver
commit, animator and scene-node update — are not. The shortlist therefore overlaps the
existing work in the Tier C rows only, which are marked `covered` and should be skipped
(10 of the 12 rows in §4.4 are already verified).

---

## 8. Reproduce

All commands are read-only against the analysed trees; scratch output goes to
`C:\Users\NacWorkstation\Documents\DH2Work-scratch\`.

```powershell
# per-function decode + PLT attribution        -> metrics2.json
python metrics_v2.py
# PLT veneer -> import name (351/351)          -> pltmap.json
python pltmap.py
# coverage re-check of the three port modules  -> coverage.json
python cover.py
# import census per candidate                  -> imports_per_fn.json
python imports.py
# ranking + the table in section 4             -> final.json, table2.md
python final.py ; python mktable2.py
# the arithmetic in section 1
python budget.py
# section 9: where the soft-float helpers live, and what they cost
python helpers.py          # guest-libc symbol report
python helpercost.py       # sizes / instruction counts / VFP use -> helpers.json
python rank3.py            # corrected ranking by measured soft-float cost
```

Spot checks used while writing this:

```powershell
# engine has no VFP arithmetic: the only v-prefixed mnemonics in 1,489,002 instructions
python -c "import re,collections;c=collections.Counter(m.group(2) for m in filter(None,(re.match(r'^0x[0-9a-f]+\s+(\S+)\s*(.*)$',l.rstrip()) for l in open(r'C:\Users\NacWorkstation\Documents\deepseek-harness\default-workspace\DH2-recon\asm\engine-assembly.txt',encoding='utf-8',errors='replace'))));print(sorted(k for k in c if k[0]=='v'))"
# -> ['vaddl.s8', 'vaddl.u32', 'vhadd.s16', 'vhadd.u32', 'vhadd.u8', 'vld4.32']
# undefined soft-float imports
python undefs.py
# capstone listing of any function body:  python disx.py <lib> <0xADDR> <insn-count>
python disx.py libDungeonHunter2.so 0x35e118 120
python disx.py libDungeonHunter2.so 0x321164 120
```

Appendix A — undefined `__aeabi_*` imports of `libDungeonHunter2.so` (43):
`__aeabi_atexit, __aeabi_d2f, __aeabi_d2iz, __aeabi_d2lz, __aeabi_d2uiz, __aeabi_dadd,
__aeabi_dcmpeq, __aeabi_dcmpge, __aeabi_dcmpgt, __aeabi_dcmple, __aeabi_dcmplt, __aeabi_dcmpun,
__aeabi_ddiv, __aeabi_dmul, __aeabi_dsub, __aeabi_f2d, __aeabi_f2iz, __aeabi_fadd,
__aeabi_fcmpeq, __aeabi_fcmpge, __aeabi_fcmpgt, __aeabi_fcmple, __aeabi_fcmplt, __aeabi_fdiv,
__aeabi_fmul, __aeabi_fsub, __aeabi_i2d, __aeabi_i2f, __aeabi_idiv, __aeabi_idivmod,
__aeabi_ldivmod, __aeabi_lmul, __aeabi_ui2d, __aeabi_ui2f, __aeabi_uidiv, __aeabi_uidivmod,
__aeabi_ul2d, __aeabi_uldivmod, __aeabi_unwind_cpp_pr0, __aeabi_unwind_cpp_pr1` plus the three
non-FP entries. The engine defines only `__aeabi_llsr` (`0x008be268`), `__aeabi_llsl`
(`0x008be284`) and `__aeabi_f2uiz` (`0x008be2a0`) itself; `__aeabi_f2uiz` is a 21-instruction
clamp-and-shift routine reached from in-degree 116 sites and from
`CSceneManager::update` `0x0058b9f0` (`bl 0x8be2a0` at `0x0058ba28`).

Appendix B — the per-frame chain, addresses quoted in section 3(a):
`0x005311a0 → 0x00530fc8 → {0x0032ccc4, 0x00671b24}`;
`0x0032ccc4 → {0x0032ade8, 0x0032c438, 0x0033c568, 0x0031177c}`;
`0x0032c438 → {0x00321164, 0x0052e784, 0x00378fb4, 0x0033900c, 0x0033a7f4, 0x0033b334,
0x0033d744, 0x00314734, 0x0035c51c, 0x00532164…}` (36 callees recorded in `calls.csv`).

---

## 9. Correction and corrected ranking (soft-float call cost, measured)

### 9.1 What changed

Two independently-checkable facts, both established after the first draft:

1. **The `__aeabi_*` helpers are guest ARM32 code, not host-native.** They are exported by
   the ARM32 bionic `libc.so` the wrapper ships as its sysroot
   (`…\fold7-build\out\assets\zb\sysroot\system\lib\libc.so`, `EM_ARM`, ELF32), while the
   engine leaves them undefined in `.dynsym` and reaches them through its 351 PLT veneers.
   The engine defines only three `__aeabi_*` bodies itself (`0x008be268` `__aeabi_llsr`,
   `0x008be284` `__aeabi_llsl`, `0x008be2a0` `__aeabi_f2uiz`, a 21-instruction clamp-and-shift).
2. **Each helper body is large and branch-heavy.** Measured by disassembling the sysroot
   library at the addresses in its own `.dynsym`: `__aeabi_fmul` 496 B / **124 ARM
   instructions**, `__aeabi_fadd` 684 B / **171**, `__aeabi_fdiv` 596 B / **149**,
   `__aeabi_dmul` 784 B / **196**, `__aeabi_dadd` 1,056 B / **264**, `__aeabi_ddiv` 1,048 B /
   **262**. Every one contains 13–16 `vmov` register transfers, i.e. it moves float bits
   through the FP register file but does the arithmetic in integer code.
   (Scripts: `DH2Work-scratch\helpers.py`, `helpercost.py`.)

Combining them with the engine-wide static call census gives the arithmetic that matters:

```
Σ over the 12 helpers I measured: sites × callee instructions = 2,450,313
engine .text (translated):                                       1,467,476
```

i.e. **the soft-float call sites in this engine statically reference ~1.7× as many
instructions as the entire engine's own code.** These are static counts, not dynamic ones —
the real per-frame figure depends on how often each site executes, which still needs the
instrument of §3(e) — but the ratio is the reason to rank by soft-float density.

### 9.2 Corrected cost model and ranking

Cost of a function, replacing §3(c)'s call weight:

```
cost = Σ over its __aeabi_* call sites (callee_instruction_count)   [measured per callee]
     + 3.0 × (sum of backward-branch spans)                        [unchanged, inferred]
     + 0.30 × instruction_count                                    [translation volume]
```

Over the 132 candidate functions that contain at least one `__aeabi_*` call site, this
decomposes as **soft-float 41.4% / loop spans 57.4% / straight-line 1.2%** — so soft-float
call cost is comparable to *all* loop iteration in these bodies, which is the strongest
available argument that the FP-call path deserves to lead the port.

Ranking (top 30 of the 132; `sf#` = number of soft-float call sites in the body):

| # | addr | tier | sf# | soft-float cost | loops3 | base | total | function |
|---:|---|---|---:|---:|---:|---:|---:|---|
| 1 | `0x00321164` | A | 152 | 19,228 | 85,800 | 456 | 105,484 | `Application::_CheckGamepad()` |
| 2 | `0x006e5d14` | B | **268** | **36,219** | 4,626 | 368 | 41,213 | `glitch::collada::CCoronasSceneNode::render(void*)` |
| 3 | `0x005b78b8` | B | 35 | 3,793 | 54,003 | 347 | 58,143 | `CProgrammableGLDriver<…>::commitCurrentMaterialAutomaticParameters(…)` |
| 4 | `0x005a6fe0` | B | 39 | 564 | 47,577 | 511 | 48,652 | `CBatchDriver::thisAppendBatch(…)` |
| 5 | `0x006c7f1c` | B | 130 | 14,098 | 7,857 | 212 | 22,167 | `CSceneNodeAnimatorCameraFPS::animateNode(…)` |
| 6 | `0x006c9934` | B | 98 | 9,928 | 7,074 | 180 | 17,182 | `CSceneNodeAnimatorCameraMaya::animateNode(…)` |
| 7 | `0x0066ff48` | B | 73 | 10,155 | 5,559 | 213 | 15,927 | `CColladaSoftwareSkinTechnique::skin(…)` |
| 8 | `0x00311914` | A | 59 | 5,079 | 9,915 | 232 | 15,226 | `PerfCounters::Draw()` |
| 9 | `0x00580f58` | B | 79 | 8,033 | 1,482 | 153 | 9,668 | `CBillboardSceneNode::render(void*)` |
| 10 | `0x0032c438` | A | 3 | 106 | 12,102 | 164 | 12,372 | `Application::_Update(int)` |
| 11 | `0x006cc830` | B | 57 | 6,556 | 21 | 88 | 6,665 | `CSceneNodeAnimatorFollowSpline::animateNode(…)` |
| 12 | `0x005b2fac` | B | 8 | 628 | 5,976 | 114 | 6,718 | `CProgrammableGLDriver<…>::commitCurrentMaterialParametersAux<CGlobalMaterialParameterManager>(…)` |
| 13 | `0x005b4da0` | B | 8 | 628 | 5,901 | 108 | 6,637 | `CProgrammableGLDriver<…>::commitCurrentMaterialParametersAux<CMaterial>(…)` |
| 14 | `0x006e3fb0` | B | 22 | 2,428 | 258 | 43 | 2,729 | `animation_track::CVector3dEx::getBlendedValueEx(…)` |
| 15 | `0x006cab34` | B | 28 | 2,023 | 1,491 | 74 | 3,588 | `CSceneNodeAnimatorCollisionResponse::animateNode(…)` |
| 16 | `0x006cbc50` | B | 17 | 2,306 | 0 | 22 | 2,328 | `CSceneNodeAnimatorFlyCircle::animateNode(…)` |
| 17 | `0x006f5fd8` | A | 28 | 1,532 | 105 | 38 | 1,675 | `CAnimatedMeshSceneNode::buildFrameNr(unsigned int)` |
| 18 | `0x006e3420` | B | 10 | 1,475 | 54 | 27 | 1,556 | `animation_track::CTextureTransformEx::getBlendedValue(…)` |
| 19 | `0x006e395c` | B | 10 | 1,475 | 0 | 23 | 1,498 | `animation_track::CTextureTransformEx::applyBlendedValue(…)` |
| 20 | `0x006cd594` | B | 10 | 1,369 | 0 | 17 | 1,386 | `CSceneNodeAnimatorRotation::animateNode(…)` |
| 21 | `0x006cc138` | B | 11 | 1,337 | 204 | 22 | 1,563 | `CSceneNodeAnimatorFlyStraight::animateNode(…)` |
| 22 | `0x006e41ec` | B | 10 | 1,260 | 0 | 15 | 1,275 | `animation_track::CVector3dEx::getBlendedValueEx(…)` (second body) |
| 23 | `0x0033c568` | A | 6 | 400 | 5,697 | 133 | 6,230 | `TouchScreenBase::ProcessEvents()` |
| 24 | `0x0035c51c` | A | 9 | 332 | 927 | 46 | 1,305 | `RootSceneNode::UpdateEnlargedViewFrustum(…)` |
| 25 | `0x006ce53c` | B | 15 | 309 | 1,530 | 87 | 1,926 | `CSkyBoxSceneNode::render(void*)` |
| 26 | `0x0032ccc4` | A | 3 | 229 | 1,815 | 58 | 2,102 | `Application::Update()` |
| 27 | `0x006f7330` | B | 1 | 43 | 1,029 | 62 | 1,134 | `CAnimatedMeshSceneNode::render(void*)` |
| 28 | `0x0035e118` | C | **112** | **16,144** | 27 | 149 | 16,320 | `CMatrix4Base<float>::mult(…)` |
| 29 | `0x003232c0` | C | **130** | **11,980** | 21 | 156 | 12,157 | `CMatrix4<float>::getInverse(…)` |
| 30 | `0x0040ea54` | C | **112** | **16,144** | 0 | 122 | 16,266 | `CMatrix4Base<float>::setbyproduct_nocheck(…)` |

(Full list: `DH2Work-scratch\rank3.py`. `CMatrix4Base<float>::mult` and
`setbyproduct_nocheck` tie at 16,144, the second-highest soft-float cost in the engine;
`getInverse` is 11,980. They appear low in *table order* only because the table sorts by
total and their loop-span term is near zero. Note the helpers differ in size by up to 57x —
`__aeabi_fsub` is an 8-byte Thumb tail-call into `__aeabi_fadd` (3 instructions measured)
while `__aeabi_fmul` is 124 and `__aeabi_fadd` 171 — so a function's soft-float cost must be
summed per helper, not multiplied by one constant.)

### 9.3 The matrix kernels are the best ratio in the engine

| function | addr | soft-float call sites | soft-float cost | its own instructions | leverage |
|---|---|---:|---:|---:|---:|
| `CMatrix4Base<float>::setbyproduct_nocheck(…)` | `0x0040ea54` | 112 (64 mul, 48 add) | 16,144 | 408 | **39.6×** |
| `CMatrix4Base<float>::mult(…)` | `0x0035e118` | 112 (64 mul, 48 add) | 16,144 | 498 | **32.4×** |
| `quaternion::slerp(…)` | `0x00612d00` | 56 mul + 5 `sinf` (already ported) | 6,878 | 245 | 28.1× |
| `CCoronasSceneNode::render(void*)` | `0x006e5d14` | 268 (129 add, 110 mul, 24 sub, …) | 36,219 | 1,226 | 29.5× |
| `CMatrix4<float>::getInverse(…)` | `0x003232c0` | 130 (79 mul, 38 sub, 11 add) | 11,980 | 520 | **23.0×** |
| `Application::_CheckGamepad()` | `0x00321164` | 152 (76 add, 38 mul, 38 cmp) | 19,228 | 1,520 | 12.7× |

"Leverage" = soft-float call cost ÷ the function's own instruction count: how much translated
work the port deletes per instruction it has to reimplement. The matrix kernels win on every
axis at once — highest leverage, zero loops, pure value semantics, no allocation, and their
memory layout is already pinned by the verified `engine-math` port. That is why the
recommendation in §6 puts `mult` first even though `CCoronasSceneNode::render` deletes more.

### 9.4 Effect on §6's recommendation

The order in §6 stands, with one change of reason:

* **`CMatrix4Base<float>::mult` `0x0035e118` — still first, now for a measured reason.**
  Not "the 22–26× case in the abstract" but "64 × 124 + 48 × 171 = **16,144** translated
  instructions of pure soft-float overhead per invocation, deleted by a body of 498
  instructions that becomes ~150 ARM64 instructions". Its identical-cost sibling
  `setbyproduct_nocheck` `0x0040ea54` (same 112 sites, 408 own instructions, leverage 39.6×)
  should be ported in the same sitting.
* **`CMatrix4<float>::getInverse` `0x003232c0` — still second**, 130 sites but only 11,980
  instructions of soft-float cost, because 38 of its 130 sites are `__aeabi_fsub`, the
  3-instruction Thumb tail-call. It stays second because 130 call sites still vanish.
* **`Application::_CheckGamepad` `0x00321164` — still third**, and now the top of the
  corrected ranking as well (152 sites, 19,228). It remains third in *porting order* only
  because the two matrix kernels are cheaper to port and easier to verify.

**`CCoronasSceneNode::render(void*)` `0x006e5d14` should be the fourth port**, not buried in a
table: 268 soft-float call sites is the largest count in the engine, its 1,194-instruction
inner loop is the largest on the per-frame path, and it is reached through the
`ISceneNode::render` slot (+0x38) that the per-frame path uses 357 times. It is harder to
verify bit-exactly than the matrix kernels (it writes through buffers), which is the only
reason it is not in the first three.

### 9.5 What is still unmeasured

Everything in §5 still applies. The corrected model is built from **static call-site counts
times measured callee sizes** — it says how much code *would* run if a site executes once per
call, not how often the site executes per frame. The corrected ranking is a better-ordered
worklist than §4, not a profile. The instrument in §3(e) item 1 (a guest-PC histogram in the
JIT) remains the single change that would turn any of these rows into a measurement.
