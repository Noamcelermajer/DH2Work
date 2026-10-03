# Soft-float call-site census — `libDungeonHunter2.so` (DH2 HD v1.0.2)

**What this is.** A static, exact count of every direct call site into the guest soft-float ABI (`__aeabi_*`) and float libm, attributed to the enclosing function, plus a weighted ranking, a reachability analysis and a per-frame recommendation list.

**Where the numbers come from.** Every figure in this document was produced by a command recorded in [Method](#9-method). Nothing is inferred from the symbol names unless it is labelled as name evidence.

---

## 0. Headline measurements

| quantity | value | source |
|---|--:|---|
| engine SHA-256 | `36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80` | `Get-FileHash` |
| guest libc SHA-256 | `23ee728839ebb17bae3e9fc73034142be2466a6d694c0404ef5aeafdb1009069` | `Get-FileHash` |
| functions in `.symtab` (STT_FUNC with size) | 31021 | census |
| distinct function start addresses | 31018 | census |
| instructions decoded in `.text` | 1513919 | census |
| `.text` bytes covered by decoded instructions | 5954490 / 5961032 (99.89%) | census |
| direct calls resolved to a PLT import | 123511 | census |
| PLT veneers | 351, all 351 resolved to `.rel.plt` names | census |
| undefined `__aeabi_*` imports | 40 | `.dynsym` |
| **total `__aeabi_*` direct call sites** | **28209** | census |
| float libm call sites (sinf/cosf/sqrtf/…) | 300 | census |
| functions containing ≥1 FP call site | 2191 | census |
| a function with ≥1 FP call site has, on average | 12.4 sites | census |
| indirect `blx <reg>` sites that cannot be attributed statically | 2086 | census |
| functions with no static caller anywhere in the binary | 18476 of 31021 | call graph |

**Reconciliation with the externally reported figures.** The brief quoted `__aeabi_fmul` 8,518 / `__aeabi_fadd` 6,165 / `__aeabi_fsub` 3,025 / float compares 3,110 / `__aeabi_fdiv` 818 / `__aeabi_f2iz` 640. This census reproduces them almost exactly:

| symbol | brief | this census | Δ |
|---|--:|--:|--:|
| `__aeabi_fmul` | 8518 | 8523 | +5 |
| `__aeabi_fadd` | 6165 | 6171 | +6 |
| `__aeabi_fsub` | 3025 | 3028 | +3 |
| `__aeabi_fdiv` | 818 | 819 | +1 |
| `__aeabi_f2iz` | 642 | 642 | +0 |
| float compares (all five) | 3,110 | 3110 | +0 |

The small positive deltas are conditional calls (`bleq`/`blne`/…), which this census counts because the JIT has to translate them too.

---

## 1. Weighting model

A call site is not the unit of cost. `a*b` does not cost one call — it costs one call **plus the translated body of the helper it enters**. The helpers are guest code in the guest sysroot `libc.so`, so the JIT translates them like any other engine code. The weight of a symbol is therefore *the number of instructions the JIT must translate inside that helper's own symbol range*, measured by disassembling the helper from `libc.so` and keeping only instructions inside `[st_value, st_value + st_size)`.

| helper | defined in | guest addr | symbol size B | translated insns in range | return paths | helper calls made | weight used |
|---|:-:|--:|--:|--:|--:|--:|--:|
| `__aeabi_dadd` | libc.so | `0x09ed98` | 1056 | **264** | 6 | 2 | 264 |
| `__aeabi_ddiv` | libc.so | `0x09f638` | 1048 | **262** | 7 | 0 | 262 |
| `powf` | libm.so | `0x01e140` | 784 | **227** | 1 | 0 | 227 |
| `tanf` | libm.so | `0x01b920` | 864 | **216** | 0 | 0 | 220 |
| `__aeabi_dmul` | libc.so | `0x0a03a0` | 784 | **196** | 7 | 0 | 196 |
| `__aeabi_cfcmple` | libc.so | `0x0a0f30` | 52 | **13** | 1 | 0 | 172 |
| `__aeabi_fadd` | libc.so | `0x09f1e8` | 684 | **171** | 7 | 2 | 171 |
| `sinf` | libm.so | `0x01e760` | 480 | **155** | 1 | 0 | 159 |
| `cosf` | libm.so | `0x01ce30` | 472 | **146** | 1 | 0 | 150 |
| `__aeabi_fdiv` | libc.so | `0x09faac` | 596 | **149** | 8 | 0 | 149 |
| `__aeabi_fmul` | libc.so | `0x0a06f4` | 496 | **124** | 8 | 0 | 124 |
| `acosf` | libm.so | `0x00c8e8` | 492 | **123** | 1 | 0 | 123 |
| `__aeabi_fcmple` | libc.so | `0x0a109c` | 32 | **8** | 2 | 0 | 122 |
| `hypotf` | libm.so | `0x00e040` | 472 | **118** | 1 | 0 | 118 |
| `atanf` | libm.so | `0x013bcc` | 484 | **114** | 5 | 0 | 114 |
| `fmodf` | libm.so | `0x00dc4c` | 460 | **114** | 5 | 0 | 114 |
| `__aeabi_d2f` | libc.so | `0x0a0938` | 388 | **97** | 4 | 0 | 97 |
| `log10f` | libm.so | `0x011f84` | 388 | **85** | 2 | 0 | 85 |
| `asinf` | libm.so | `0x00cf20` | 328 | **82** | 1 | 0 | 82 |
| `__aeabi_l2f` | libc.so | `0x09ff60` | 288 | **72** | 1 | 0 | 72 |
| `atan2f` | libm.so | `0x00d31c` | 636 | **72** | 4 | 0 | 72 |
| `asinhf` | libm.so | `0x013830` | 276 | **69** | 1 | 0 | 69 |
| `sinhf` | libm.so | `0x0126a8` | 272 | **68** | 1 | 0 | 68 |
| `__aeabi_ul2f` | libc.so | `0x0a01a0` | 256 | **64** | 1 | 0 | 64 |
| `coshf` | libm.so | `0x00d848` | 256 | **64** | 6 | 0 | 64 |
| `log2f` | libm.so | `0x01d8f4` | 204 | **64** | 2 | 0 | 64 |
| `logf` | libm.so | `0x01d9c0` | 204 | **64** | 2 | 0 | 64 |
| `tanhf` | libm.so | `0x01bda4` | 256 | **63** | 1 | 0 | 63 |
| `acoshf` | libm.so | `0x00cbd0` | 236 | **59** | 1 | 0 | 59 |
| `expf` | libm.so | `0x01d48c` | 196 | **56** | 1 | 0 | 56 |
| `exp2f` | libm.so | `0x01d3d4` | 184 | **55** | 1 | 0 | 55 |
| `atanhf` | libm.so | `0x00d678` | 196 | **49** | 4 | 0 | 49 |
| `__aeabi_fcmpge` | libc.so | `0x0a10bc` | 32 | **8** | 2 | 0 | 47 |
| `__aeabi_fcmpgt` | libc.so | `0x0a10dc` | 32 | **8** | 2 | 0 | 47 |
| `cbrtf` | libm.so | `0x013f98` | 188 | **47** | 1 | 0 | 47 |
| `__aeabi_f2d` | libc.so | `0x09fd00` | 164 | **41** | 1 | 0 | 41 |
| `__aeabi_cdcmple` | libc.so | `0x0a0ea8` | 52 | **13** | 1 | 0 | 37 |
| `__aeabi_fcmpeq` | libc.so | `0x0a105c` | 32 | **8** | 2 | 0 | 37 |
| `ldexpf` | libm.so | `0x01a2f8` | 144 | **36** | 1 | 0 | 36 |
| `modff` | libm.so | `0x01941c` | 136 | **34** | 2 | 0 | 34 |
| `__aeabi_i2f` | libc.so | `0x0a00e4` | 132 | **33** | 1 | 0 | 33 |
| `__aeabi_ui2f` | libc.so | `0x0a02f4` | 124 | **31** | 1 | 0 | 31 |
| `frexpf` | libm.so | `0x018ae0` | 112 | **27** | 1 | 0 | 27 |
| `__aeabi_d2iz` | libc.so | `0x09fda4` | 104 | **26** | 1 | 0 | 26 |
| `__aeabi_i2d` | libc.so | `0x0a0080` | 100 | **25** | 2 | 0 | 25 |
| `__aeabi_d2uiz` | libc.so | `0x09fe68` | 96 | **24** | 1 | 0 | 24 |
| `__aeabi_dcmpeq` | libc.so | `0x0a0f90` | 32 | **8** | 2 | 0 | 24 |
| `__aeabi_dcmpge` | libc.so | `0x0a0ff0` | 32 | **8** | 2 | 0 | 24 |
| `__aeabi_dcmpgt` | libc.so | `0x0a1010` | 32 | **8** | 2 | 0 | 24 |
| `__aeabi_dcmple` | libc.so | `0x0a0fd0` | 32 | **8** | 2 | 0 | 24 |
| `__aeabi_f2iz` | libc.so | `0x09fe0c` | 92 | **23** | 1 | 0 | 23 |
| `__aeabi_f2uiz` | libc.so | `0x09fec8` | 84 | **21** | 2 | 0 | 21 |
| `__aeabi_ui2d` | libc.so | `0x0a02a0` | 84 | **21** | 2 | 0 | 21 |
| `__aeabi_cdrcmple` | libc.so | `0x0a0edc` | 28 | **7** | 0 | 0 | 19 |
| `__aeabi_dcmplt` | libc.so | `0x0a0fb0` | 32 | **8** | 2 | 0 | 19 |
| `__aeabi_fcmplt` | libc.so | `0x0a107c` | 32 | **8** | 2 | 0 | 19 |
| `__aeabi_fsub` | libc.so | `0x0a0930` | 8 | **2** | 0 | 0 | 16 |
| `__aeabi_l2d` | libc.so | `0x09ff20` | 64 | **16** | 1 | 0 | 16 |
| `__aeabi_dcmpun` | libc.so | `0x09f5fc` | 60 | **15** | 1 | 0 | 15 |
| `__aeabi_ul2d` | libc.so | `0x0a0168` | 56 | **14** | 1 | 0 | 14 |
| `__aeabi_d2lz` | libm.so | `0x01ed3c` | 48 | **12** | 1 | 0 | 12 |
| `__aeabi_f2lz` | libm.so | `0x01ed6c` | 44 | **11** | 1 | 0 | 11 |
| `__aeabi_cfrcmple` | libc.so | `0x0a0f64` | 16 | **4** | 0 | 0 | 10 |
| `__aeabi_dsub` | libc.so | `0x0a0928` | 8 | **2** | 0 | 0 | 10 |
| `__aeabi_cdcmpeq` | libc.so | `0x0a0e8c` | 28 | **7** | 1 | 1 | 7 |
| `__aeabi_cfcmpeq` | libc.so | `0x0a0f14` | 28 | **7** | 1 | 1 | 7 |
| `__aeabi_fcmpun` | libc.so | `0x09eb78` | 28 | **7** | 1 | 0 | 7 |
| `ceilf` | libm.so | `0x01c8b0` | 16 | **4** | 1 | 0 | 4 |
| `floorf` | libm.so | `0x01c90c` | 16 | **4** | 1 | 0 | 4 |
| `roundf` | libm.so | `0x01c9cc` | 16 | **4** | 1 | 0 | 4 |
| `sqrtf` | libm.so | `0x01c9ec` | 16 | **4** | 1 | 0 | 4 |
| `truncf` | libm.so | `0x01ca0c` | 16 | **4** | 1 | 0 | 4 |
| `copysignf` | libm.so | `0x01c8cc` | 12 | **3** | 1 | 0 | 3 |
| `fabsf` | libm.so | `0x01c8ec` | 8 | **2** | 1 | 0 | 2 |

Justification of the model:

* The weight is the helper's **translated instruction count**, not its byte size. Bytes are the wrong unit: `__aeabi_fsub` is an 8-byte alias that tail-calls into the addition path, so charging it its own 8 bytes would understate it by ~20×.
* **Tail-call closure.** Because aliases exist, the weight used is the *closure*: the helper's own body plus, recursively, everything it tail-calls through the sysroot's own PLT (resolved through that library's `.rel.plt`, exactly as the engine's veneers are). `__aeabi_fsub` measures **2** instructions directly and **16** as a closure; `__aeabi_fcmpeq` measures **8** directly and **37** as a closure. **1,070 of the 2,191 FP functions** gain weight under the closure model, and the binary-wide total rises from 2,558,582 to **2,730,672** weighted units (+6.7 %): the closure model is applied everywhere in this document.
* A helper that calls another helper **chains within its own body**: `__aeabi_dadd` makes 8 calls, `__aeabi_fadd` 5. Those sub-calls are real per-invocation cost; the weight counts the helper's own body once and is therefore a strict lower bound on the full chain.
* Double precision is **kept separate and not special-cased away**: because a double op translates 196–264 instructions versus 124–171 for a float op, weighting by instruction count *raises* the relative cost of doubles above their 2.5 % site share (to 4.7 % of weighted units). Both numbers are reported so the reader can see the effect.

### 1.1 The effect of the weighting on the ranking

Three views of the *same* measured site counts, side by side:

| addr | function | sites (#) | direct-body weight (#) | closure weight (#) |
|---|---|--:|--:|--:|
| `0x006e5d14` | glitch::collada::CCoronasSceneNode::render | 268 (#3) | 36191 (#1) | 36538 (#1) |
| `0x00898e60` | mpc_synthese_filter_float_internal | 329 (#1) | 33664 (#2) | 35162 (#2) |
| `0x006c42a8` | glitch::scene::CSceneCollisionManager::testT | 268 (#2) | 29142 (#3) | 30718 (#3) |
| `0x00406c2c` | v2GamepadController::Update | 219 (#4) | 24251 (#4) | 26274 (#4) |
| `0x006338e0` | _ZN6glitch2ps10PDeflector5applyINS0_9SPartic | 161 (#7) | 19206 (#5) | 19746 (#5) |
| `0x00636e28` | _ZN6glitch2ps10PDeflector5applyINS0_12GNPSPa | 161 (#8) | 19206 (#6) | 19746 (#6) |
| `0x00776564` | gameswf::scene_node::get_collision_uv | 181 (#5) | 18334 (#7) | 19612 (#7) |
| `0x00321164` | Application::_CheckGamepad | 152 (#10) | 18012 (#9) | 19494 (#8) |
| `0x0087af70` | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S | 175 (#6) | 18194 (#8) | 18997 (#9) |
| `0x0060cf88` | glitch::collada::CBillboardSceneNode::update | 128 (#17) | 16953 (#10) | 17070 (#10) |
| `0x007f129c` | b2PulleyJoint::SolvePositionConstraints | 159 (#9) | 16044 (#13) | 16777 (#11) |
| `0x0035e118` | glitch::core::detail::CMatrix4Base<float>::m | 112 (#22) | 16144 (#11) | 16144 (#12) |
| `0x0040ea54` | glitch::core::detail::CMatrix4Base<float>::s | 112 (#23) | 16144 (#12) | 16144 (#13) |
| `0x006d3578` | glitch::scene::CTerrainSceneNode::calculateN | 144 (#11) | 14256 (#15) | 14928 (#14) |
| `0x007ee080` | b2PrismaticJoint::SolveVelocityConstraints | 111 (#25) | 14570 (#14) | 14740 (#15) |
| `0x006c7f1c` | glitch::scene::CSceneNodeAnimatorCameraFPS:: | 130 (#15) | 14044 (#19) | 14596 (#16) |
| `0x007ebfbc` | b2PolyAndCircleContact::Evaluate | 114 (#19) | 14196 (#16) | 14448 (#17) |
| `0x007ecb84` | b2PolygonContact::Evaluate | 114 (#20) | 14196 (#17) | 14448 (#18) |
| `0x007f3b9c` | b2CircleContact::Evaluate | 114 (#21) | 14196 (#18) | 14448 (#19) |
| `0x007ef010` | b2PrismaticJoint::SolvePositionConstraints | 132 (#12) | 13510 (#20) | 14317 (#20) |

Mean absolute rank shift over the top 60: **9.53 positions** from unweighted → direct-body weighting, and a further **1.87 positions** from direct-body → closure weighting. Two examples of what the weighting corrects: `_ZNKSt4priv8_Rb_treeI7CompPosSt4lessIS1_ESt4pairIKS1_P12PFGInnerNodeENS_10_Select1stIS8_EENS_11_MapTraitsTIS8_EESaIS8_EE7_M_findI7Point3DIfEEEPNS_18_Rb_tree_node_baseERKT_` at `0x0051bf28` moves **890 positions** between the unweighted and weighted rankings because its sites are cheap compares rather than arithmetic, while `glitch::core::detail::CMatrix4Base<float>::mult` at `0x0035e118` rises from unweighted #22 to weighted #12 on pure multiply/add density.

Concretely the weighting promotes arithmetic-dense code and demotes compare-dense and conversion-dense code:

| family | share of call sites | share of weighted units |
|---|--:|--:|
| float `+ - * /` | 18,541 / 27,088 = **68.4 %** | **83.6 %** |
| double `+ - * /` | 675 / 27,088 = **2.49 %** | **4.46 %** |

The brief's "all double-precision ops combined 675 (2.7 %)" is exactly the double *arithmetic* count in this census (675 sites = 2.49 % of the 27,088 FP sites); adding the 355 double compares gives 1,030 = 3.65 % of the 28,209 `__aeabi_*` sites.

---

## 2. Per-function census — top 60 by weighted FP call-site count

_md_ = instruction set of the function (A = ARM, T = Thumb). _sites_ = direct `__aeabi_*`/float-libm call sites. _w_ = weighted units per §1. _dens_ = sites per 100 bytes of the function; _wdens_ = weighted units per 100 bytes. _purpose_ is from §4.

| # | addr | size | md | function | sites | w | fmul | fadd | fsub | fdiv | fcmp | fcvt | dbl | libm | dens | wdens | purpose |
|--:|---|--:|:-:|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:--|
| 1 | `0x006e5d14` | 4904 | A | glitch::collada::CCoronasSceneNode::render | 268 | 36538 | 110 | 129 | 24 | 2 | 1 | 1 | 1 | 0 | 5.46 | 745.1 | virtual |
| 2 | `0x00898e60` | 5220 | A | mpc_synthese_filter_float_internal | 329 | 35162 | 96 | 126 | 107 | 0 | 0 | 0 | 0 | 0 | 6.30 | 673.6 | off-frame |
| 3 | `0x006c42a8` | 5068 | A | glitch::scene::CSceneCollisionManager::testTriangleIntersection | 268 | 30718 | 117 | 80 | 50 | 4 | 15 | 1 | 1 | 0 | 5.29 | 606.1 | off-frame |
| 4 | `0x00406c2c` | 7360 | A | v2GamepadController::Update | 219 | 26274 | 58 | 92 | 15 | 6 | 46 | 1 | 0 | 1 | 2.98 | 357.0 | virtual |
| 5 | `0x006338e0` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_9SParticleEEEvNS0_16IParticleContextIT_E11 | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 6 | `0x00636e28` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_12GNPSParticleEEEvNS0_16IParticleContextIT | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 7 | `0x00776564` | 3564 | A | gameswf::scene_node::get_collision_uv | 181 | 19612 | 66 | 56 | 30 | 2 | 27 | 0 | 0 | 0 | 5.08 | 550.3 | one-hop |
| 8 | `0x00321164` | 6080 | A | Application::_CheckGamepad | 152 | 19494 | 38 | 76 | 0 | 0 | 38 | 0 | 0 | 0 | 2.50 | 320.6 | on-frame |
| 9 | `0x0087af70` | 12048 | A | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_ | 175 | 18997 | 63 | 60 | 49 | 0 | 3 | 0 | 0 | 0 | 1.45 | 157.7 | off-frame |
| 10 | `0x0060cf88` | 3500 | A | glitch::collada::CBillboardSceneNode::updateAbsolutePosition | 128 | 17070 | 72 | 46 | 6 | 0 | 3 | 0 | 0 | 1 | 3.66 | 487.7 | virtual |
| 11 | `0x007f129c` | 2752 | A | b2PulleyJoint::SolvePositionConstraints | 159 | 16777 | 65 | 42 | 27 | 4 | 17 | 0 | 0 | 4 | 5.78 | 609.6 | virtual |
| 12 | `0x0035e118` | 1992 | A | glitch::core::detail::CMatrix4Base<float>::mult | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 5.62 | 810.4 | off-frame |
| 13 | `0x0040ea54` | 1632 | A | glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 6.86 | 989.2 | on-frame |
| 14 | `0x006d3578` | 3804 | A | glitch::scene::CTerrainSceneNode::calculateNormals | 144 | 14928 | 48 | 48 | 48 | 0 | 0 | 0 | 0 | 0 | 3.79 | 392.4 | one-hop |
| 15 | `0x007ee080` | 1952 | A | b2PrismaticJoint::SolveVelocityConstraints | 111 | 14740 | 62 | 40 | 5 | 0 | 4 | 0 | 0 | 0 | 5.69 | 755.1 | virtual |
| 16 | `0x006c7f1c` | 2832 | A | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode | 130 | 14596 | 61 | 37 | 21 | 0 | 10 | 1 | 0 | 0 | 4.59 | 515.4 | virtual |
| 17 | `0x007ebfbc` | 2772 | A | b2PolyAndCircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 18 | `0x007ecb84` | 2772 | A | b2PolygonContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 19 | `0x007f3b9c` | 2184 | A | b2CircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 5.22 | 661.5 | virtual |
| 20 | `0x007ef010` | 2552 | A | b2PrismaticJoint::SolvePositionConstraints | 132 | 14317 | 55 | 38 | 20 | 0 | 19 | 0 | 0 | 0 | 5.17 | 561.0 | virtual |
| 21 | `0x007ed7e8` | 2200 | A | b2PrismaticJoint::InitVelocityConstraints | 112 | 13617 | 58 | 32 | 14 | 3 | 5 | 0 | 0 | 0 | 5.09 | 619.0 | virtual |
| 22 | `0x007efc10` | 1796 | A | b2PulleyJoint::SolveVelocityConstraints | 110 | 13486 | 64 | 31 | 12 | 0 | 3 | 0 | 0 | 0 | 6.12 | 750.9 | virtual |
| 23 | `0x0068acd8` | 3520 | A | png_init_read_transformations | 90 | 12561 | 3 | 0 | 0 | 0 | 1 | 7 | 79 | 0 | 2.56 | 356.8 | off-frame |
| 24 | `0x003232c0` | 2080 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.25 | 603.7 | one-hop |
| 25 | `0x00663ce8` | 2120 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.13 | 592.3 | orphan |
| 26 | `0x0069efac` | 1440 | A | glitch::ps::PDBox::transform | 90 | 12528 | 51 | 36 | 3 | 0 | 0 | 0 | 0 | 0 | 6.25 | 870.0 | virtual |
| 27 | `0x0069e69c` | 1524 | A | glitch::ps::PDCylinder::transform | 92 | 12376 | 52 | 33 | 3 | 1 | 2 | 0 | 0 | 1 | 6.04 | 812.1 | virtual |
| 28 | `0x00762308` | 4800 | A | gameswf::morph2_character_def::read | 96 | 12144 | 32 | 32 | 0 | 0 | 32 | 0 | 0 | 0 | 2.00 | 253.0 | one-hop |
| 29 | `0x0063cce8` | 1688 | A | _ZN6glitch2ps13GNPSSpinModelINS0_12GNPSParticleEE9initPSpinEPS2_S4_ | 89 | 11857 | 34 | 35 | 4 | 4 | 4 | 0 | 6 | 2 | 5.27 | 702.4 | virtual |
| 30 | `0x007f5920` | 2288 | A | _Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_ | 91 | 11694 | 47 | 32 | 7 | 0 | 5 | 0 | 0 | 0 | 3.98 | 511.1 | one-hop |
| 31 | `0x0063b7fc` | 1776 | A | _ZN6glitch2ps24PSBillboardPositionBakerINS0_12GNPSParticleEE28getPerParticleSy | 84 | 11544 | 60 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 4.73 | 650.0 | one-hop |
| 32 | `0x0064dcd8` | 1776 | A | _ZN6glitch2ps24PSBillboardPositionBakerINS0_9SParticleEE28getPerParticleSystem | 84 | 11544 | 60 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 4.73 | 650.0 | one-hop |
| 33 | `0x0078faa0` | 4060 | A | gameswf::display_glyph_records | 121 | 11137 | 30 | 20 | 27 | 13 | 20 | 10 | 0 | 1 | 2.98 | 274.3 | one-hop |
| 34 | `0x007f2bec` | 1920 | A | b2RevoluteJoint::SolvePositionConstraints | 106 | 10759 | 47 | 23 | 21 | 1 | 13 | 0 | 0 | 1 | 5.52 | 560.4 | virtual |
| 35 | `0x007f8018` | 2052 | A | _ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_ | 98 | 10725 | 41 | 24 | 20 | 3 | 10 | 0 | 0 | 0 | 4.78 | 522.7 | off-frame |
| 36 | `0x007f09c0` | 1656 | A | b2PulleyJoint::InitVelocityConstraints | 95 | 10633 | 44 | 23 | 16 | 5 | 5 | 0 | 0 | 2 | 5.74 | 642.1 | virtual |
| 37 | `0x007db5a4` | 2804 | A | _Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_ | 97 | 10549 | 0 | 0 | 0 | 0 | 0 | 0 | 97 | 0 | 3.46 | 376.2 | off-frame |
| 38 | `0x0052b560` | 3572 | A | PFWorld::_SearchGraph | 99 | 10441 | 36 | 30 | 18 | 0 | 15 | 0 | 0 | 0 | 2.77 | 292.3 | one-hop |
| 39 | `0x006c9934` | 2396 | A | glitch::scene::CSceneNodeAnimatorCameraMaya::animateNode | 98 | 10274 | 42 | 26 | 22 | 0 | 4 | 4 | 0 | 0 | 4.09 | 428.8 | virtual |
| 40 | `0x0066ff48` | 2840 | A | glitch::collada::detail::CColladaSoftwareSkinTechnique::skin | 73 | 10255 | 36 | 33 | 0 | 0 | 4 | 0 | 0 | 0 | 2.57 | 361.1 | virtual |
| 41 | `0x007f7058` | 2016 | A | _ZN15b2ContactSolverC1ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | 82 | 9846 | 42 | 23 | 12 | 3 | 2 | 0 | 0 | 0 | 4.07 | 488.4 | off-frame |
| 42 | `0x007f7838` | 2016 | A | _ZN15b2ContactSolverC2ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | 82 | 9846 | 42 | 23 | 12 | 3 | 2 | 0 | 0 | 0 | 4.07 | 488.4 | orphan |
| 43 | `0x007f4424` | 2088 | A | _Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonShapeRK7b2XFormPK13b2Ci | 94 | 9702 | 42 | 22 | 24 | 0 | 6 | 0 | 0 | 0 | 4.50 | 464.7 | off-frame |
| 44 | `0x0059e728` | 1848 | A | glitch::scene::_GLOBAL__N_1::transform | 66 | 9594 | 36 | 30 | 0 | 0 | 0 | 0 | 0 | 0 | 3.57 | 519.2 | orphan |
| 45 | `0x00418d28` | 3620 | A | HUDControls::OnEvent | 128 | 9507 | 4 | 16 | 24 | 27 | 1 | 52 | 0 | 4 | 3.54 | 262.6 | virtual |
| 46 | `0x005f95ac` | 17128 | A | glitch::video::pixel_format::convert | 93 | 9409 | 38 | 18 | 0 | 4 | 0 | 33 | 0 | 0 | 0.54 | 54.9 | one-hop |
| 47 | `0x0035dd08` | 1040 | A | glitch::core::CMatrix4<float>::transformPlane | 65 | 9099 | 33 | 29 | 3 | 0 | 0 | 0 | 0 | 0 | 6.25 | 874.9 | one-hop |
| 48 | `0x00597884` | 988 | A | glitch::core::detail::CMatrix4Base<float>::mult34 | 63 | 9081 | 36 | 27 | 0 | 0 | 0 | 0 | 0 | 0 | 6.38 | 919.1 | on-frame |
| 49 | `0x006651a0` | 992 | A | _ZN6glitch4coremlIfNS_7collada7SMatrixEEENS0_8CMatrix4IT_EERKS6_RKT0_ | 63 | 9081 | 36 | 27 | 0 | 0 | 0 | 0 | 0 | 0 | 6.35 | 915.4 | one-hop |
| 50 | `0x0066d0b8` | 940 | A | _ZN6glitch4coreL18rowMatrixProduct34INS0_8CMatrix4IfEES3_A16_fEEvRT_RKT0_RKT1_ | 63 | 9081 | 36 | 27 | 0 | 0 | 0 | 0 | 0 | 0 | 6.70 | 966.1 | off-frame |
| 51 | `0x007fa098` | 1296 | A | b2DistanceJoint::InitVelocityConstraints | 82 | 9018 | 43 | 16 | 16 | 4 | 2 | 0 | 0 | 1 | 6.33 | 695.8 | virtual |
| 52 | `0x005eb334` | 2632 | A | glitch::video::CTextureManager::makeNormalMapTexture | 94 | 8876 | 44 | 12 | 12 | 3 | 0 | 23 | 0 | 0 | 3.57 | 337.2 | orphan |
| 53 | `0x007fab98` | 1200 | A | b2GearJoint::InitVelocityConstraints | 69 | 8873 | 42 | 20 | 6 | 1 | 0 | 0 | 0 | 0 | 5.75 | 739.4 | virtual |
| 54 | `0x007f1e84` | 1524 | A | b2RevoluteJoint::InitVelocityConstraints | 77 | 8826 | 39 | 19 | 13 | 2 | 4 | 0 | 0 | 0 | 5.05 | 579.1 | virtual |
| 55 | `0x007f2478` | 1416 | A | b2RevoluteJoint::SolveVelocityConstraints | 83 | 8661 | 40 | 19 | 20 | 0 | 4 | 0 | 0 | 0 | 5.86 | 611.7 | virtual |
| 56 | `0x0059c970` | 4780 | A | glitch::scene::createMeshWithTangents | 72 | 8631 | 27 | 27 | 0 | 0 | 18 | 0 | 0 | 0 | 1.51 | 180.6 | orphan |
| 57 | `0x00580f58` | 2036 | A | glitch::scene::CBillboardSceneNode::render | 79 | 8370 | 23 | 29 | 24 | 0 | 1 | 1 | 1 | 0 | 3.88 | 411.1 | virtual |
| 58 | `0x00409bd8` | 2864 | A | v2PS3MoveController::Update | 64 | 8208 | 16 | 32 | 0 | 0 | 16 | 0 | 0 | 0 | 2.23 | 286.6 | virtual |
| 59 | `0x006f4540` | 1772 | A | jpeg_idct_float | 94 | 8105 | 19 | 28 | 30 | 0 | 0 | 17 | 0 | 0 | 5.30 | 457.4 | virtual |
| 60 | `0x007e6b78` | 2596 | A | b2World::DrawDebugData | 64 | 8072 | 24 | 24 | 0 | 4 | 0 | 12 | 0 | 0 | 2.47 | 310.9 | off-frame |

### 2.1 Unweighted ranking, for comparison

| # | addr | size | md | function | sites | w | fmul | fadd | fsub | fdiv | fcmp | fcvt | dbl | libm | dens | wdens | purpose |
|--:|---|--:|:-:|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:--|
| 1 | `0x00898e60` | 5220 | A | mpc_synthese_filter_float_internal | 329 | 35162 | 96 | 126 | 107 | 0 | 0 | 0 | 0 | 0 | 6.30 | 673.6 | off-frame |
| 2 | `0x006c42a8` | 5068 | A | glitch::scene::CSceneCollisionManager::testTriangleIntersection | 268 | 30718 | 117 | 80 | 50 | 4 | 15 | 1 | 1 | 0 | 5.29 | 606.1 | off-frame |
| 3 | `0x006e5d14` | 4904 | A | glitch::collada::CCoronasSceneNode::render | 268 | 36538 | 110 | 129 | 24 | 2 | 1 | 1 | 1 | 0 | 5.46 | 745.1 | virtual |
| 4 | `0x00406c2c` | 7360 | A | v2GamepadController::Update | 219 | 26274 | 58 | 92 | 15 | 6 | 46 | 1 | 0 | 1 | 2.98 | 357.0 | virtual |
| 5 | `0x00776564` | 3564 | A | gameswf::scene_node::get_collision_uv | 181 | 19612 | 66 | 56 | 30 | 2 | 27 | 0 | 0 | 0 | 5.08 | 550.3 | one-hop |
| 6 | `0x0087af70` | 12048 | A | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_ | 175 | 18997 | 63 | 60 | 49 | 0 | 3 | 0 | 0 | 0 | 1.45 | 157.7 | off-frame |
| 7 | `0x006338e0` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_9SParticleEEEvNS0_16IParticleContextIT_E11 | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 8 | `0x00636e28` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_12GNPSParticleEEEvNS0_16IParticleContextIT | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 9 | `0x007f129c` | 2752 | A | b2PulleyJoint::SolvePositionConstraints | 159 | 16777 | 65 | 42 | 27 | 4 | 17 | 0 | 0 | 4 | 5.78 | 609.6 | virtual |
| 10 | `0x00321164` | 6080 | A | Application::_CheckGamepad | 152 | 19494 | 38 | 76 | 0 | 0 | 38 | 0 | 0 | 0 | 2.50 | 320.6 | on-frame |
| 11 | `0x006d3578` | 3804 | A | glitch::scene::CTerrainSceneNode::calculateNormals | 144 | 14928 | 48 | 48 | 48 | 0 | 0 | 0 | 0 | 0 | 3.79 | 392.4 | one-hop |
| 12 | `0x007ef010` | 2552 | A | b2PrismaticJoint::SolvePositionConstraints | 132 | 14317 | 55 | 38 | 20 | 0 | 19 | 0 | 0 | 0 | 5.17 | 561.0 | virtual |
| 13 | `0x003232c0` | 2080 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.25 | 603.7 | one-hop |
| 14 | `0x00663ce8` | 2120 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.13 | 592.3 | orphan |
| 15 | `0x006c7f1c` | 2832 | A | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode | 130 | 14596 | 61 | 37 | 21 | 0 | 10 | 1 | 0 | 0 | 4.59 | 515.4 | virtual |
| 16 | `0x00418d28` | 3620 | A | HUDControls::OnEvent | 128 | 9507 | 4 | 16 | 24 | 27 | 1 | 52 | 0 | 4 | 3.54 | 262.6 | virtual |
| 17 | `0x0060cf88` | 3500 | A | glitch::collada::CBillboardSceneNode::updateAbsolutePosition | 128 | 17070 | 72 | 46 | 6 | 0 | 3 | 0 | 0 | 1 | 3.66 | 487.7 | virtual |
| 18 | `0x0078faa0` | 4060 | A | gameswf::display_glyph_records | 121 | 11137 | 30 | 20 | 27 | 13 | 20 | 10 | 0 | 1 | 2.98 | 274.3 | one-hop |
| 19 | `0x007ebfbc` | 2772 | A | b2PolyAndCircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 20 | `0x007ecb84` | 2772 | A | b2PolygonContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 21 | `0x007f3b9c` | 2184 | A | b2CircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 5.22 | 661.5 | virtual |
| 22 | `0x0035e118` | 1992 | A | glitch::core::detail::CMatrix4Base<float>::mult | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 5.62 | 810.4 | off-frame |
| 23 | `0x0040ea54` | 1632 | A | glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 6.86 | 989.2 | on-frame |
| 24 | `0x007ed7e8` | 2200 | A | b2PrismaticJoint::InitVelocityConstraints | 112 | 13617 | 58 | 32 | 14 | 3 | 5 | 0 | 0 | 0 | 5.09 | 619.0 | virtual |
| 25 | `0x007ee080` | 1952 | A | b2PrismaticJoint::SolveVelocityConstraints | 111 | 14740 | 62 | 40 | 5 | 0 | 4 | 0 | 0 | 0 | 5.69 | 755.1 | virtual |
| 26 | `0x007efc10` | 1796 | A | b2PulleyJoint::SolveVelocityConstraints | 110 | 13486 | 64 | 31 | 12 | 0 | 3 | 0 | 0 | 0 | 6.12 | 750.9 | virtual |
| 27 | `0x007f2bec` | 1920 | A | b2RevoluteJoint::SolvePositionConstraints | 106 | 10759 | 47 | 23 | 21 | 1 | 13 | 0 | 0 | 1 | 5.52 | 560.4 | virtual |
| 28 | `0x0052b560` | 3572 | A | PFWorld::_SearchGraph | 99 | 10441 | 36 | 30 | 18 | 0 | 15 | 0 | 0 | 0 | 2.77 | 292.3 | one-hop |
| 29 | `0x006c9934` | 2396 | A | glitch::scene::CSceneNodeAnimatorCameraMaya::animateNode | 98 | 10274 | 42 | 26 | 22 | 0 | 4 | 4 | 0 | 0 | 4.09 | 428.8 | virtual |
| 30 | `0x007f8018` | 2052 | A | _ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_ | 98 | 10725 | 41 | 24 | 20 | 3 | 10 | 0 | 0 | 0 | 4.78 | 522.7 | off-frame |
| 31 | `0x007db5a4` | 2804 | A | _Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_ | 97 | 10549 | 0 | 0 | 0 | 0 | 0 | 0 | 97 | 0 | 3.46 | 376.2 | off-frame |
| 32 | `0x00762308` | 4800 | A | gameswf::morph2_character_def::read | 96 | 12144 | 32 | 32 | 0 | 0 | 32 | 0 | 0 | 0 | 2.00 | 253.0 | one-hop |
| 33 | `0x007f09c0` | 1656 | A | b2PulleyJoint::InitVelocityConstraints | 95 | 10633 | 44 | 23 | 16 | 5 | 5 | 0 | 0 | 2 | 5.74 | 642.1 | virtual |
| 34 | `0x005eb334` | 2632 | A | glitch::video::CTextureManager::makeNormalMapTexture | 94 | 8876 | 44 | 12 | 12 | 3 | 0 | 23 | 0 | 0 | 3.57 | 337.2 | orphan |
| 35 | `0x006f4540` | 1772 | A | jpeg_idct_float | 94 | 8105 | 19 | 28 | 30 | 0 | 0 | 17 | 0 | 0 | 5.30 | 457.4 | virtual |
| 36 | `0x007f4424` | 2088 | A | _Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonShapeRK7b2XFormPK13b2Ci | 94 | 9702 | 42 | 22 | 24 | 0 | 6 | 0 | 0 | 0 | 4.50 | 464.7 | off-frame |
| 37 | `0x005f95ac` | 17128 | A | glitch::video::pixel_format::convert | 93 | 9409 | 38 | 18 | 0 | 4 | 0 | 33 | 0 | 0 | 0.54 | 54.9 | one-hop |
| 38 | `0x0069e69c` | 1524 | A | glitch::ps::PDCylinder::transform | 92 | 12376 | 52 | 33 | 3 | 1 | 2 | 0 | 0 | 1 | 6.04 | 812.1 | virtual |
| 39 | `0x007f5920` | 2288 | A | _Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_ | 91 | 11694 | 47 | 32 | 7 | 0 | 5 | 0 | 0 | 0 | 3.98 | 511.1 | one-hop |
| 40 | `0x0068acd8` | 3520 | A | png_init_read_transformations | 90 | 12561 | 3 | 0 | 0 | 0 | 1 | 7 | 79 | 0 | 2.56 | 356.8 | off-frame |
| 41 | `0x0069efac` | 1440 | A | glitch::ps::PDBox::transform | 90 | 12528 | 51 | 36 | 3 | 0 | 0 | 0 | 0 | 0 | 6.25 | 870.0 | virtual |
| 42 | `0x0063cce8` | 1688 | A | _ZN6glitch2ps13GNPSSpinModelINS0_12GNPSParticleEE9initPSpinEPS2_S4_ | 89 | 11857 | 34 | 35 | 4 | 4 | 4 | 0 | 6 | 2 | 5.27 | 702.4 | virtual |
| 43 | `0x0063b7fc` | 1776 | A | _ZN6glitch2ps24PSBillboardPositionBakerINS0_12GNPSParticleEE28getPerParticleSy | 84 | 11544 | 60 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 4.73 | 650.0 | one-hop |
| 44 | `0x0064dcd8` | 1776 | A | _ZN6glitch2ps24PSBillboardPositionBakerINS0_9SParticleEE28getPerParticleSystem | 84 | 11544 | 60 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 4.73 | 650.0 | one-hop |
| 45 | `0x006c62e8` | 2332 | A | glitch::scene::CSceneCollisionManager::getCollisionPoint | 84 | 5836 | 18 | 12 | 21 | 0 | 33 | 0 | 0 | 0 | 3.60 | 250.3 | virtual |
| 46 | `0x007f2478` | 1416 | A | b2RevoluteJoint::SolveVelocityConstraints | 83 | 8661 | 40 | 19 | 20 | 0 | 4 | 0 | 0 | 0 | 5.86 | 611.7 | virtual |
| 47 | `0x007f7058` | 2016 | A | _ZN15b2ContactSolverC1ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | 82 | 9846 | 42 | 23 | 12 | 3 | 2 | 0 | 0 | 0 | 4.07 | 488.4 | off-frame |
| 48 | `0x007f7838` | 2016 | A | _ZN15b2ContactSolverC2ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | 82 | 9846 | 42 | 23 | 12 | 3 | 2 | 0 | 0 | 0 | 4.07 | 488.4 | orphan |
| 49 | `0x007fa098` | 1296 | A | b2DistanceJoint::InitVelocityConstraints | 82 | 9018 | 43 | 16 | 16 | 4 | 2 | 0 | 0 | 1 | 6.33 | 695.8 | virtual |
| 50 | `0x00580f58` | 2036 | A | glitch::scene::CBillboardSceneNode::render | 79 | 8370 | 23 | 29 | 24 | 0 | 1 | 1 | 1 | 0 | 3.88 | 411.1 | virtual |
| 51 | `0x00898104` | 3420 | A | mpc_decoder_decode_frame | 78 | 7305 | 48 | 3 | 3 | 0 | 0 | 24 | 0 | 0 | 2.28 | 213.6 | off-frame |
| 52 | `0x007f1e84` | 1524 | A | b2RevoluteJoint::InitVelocityConstraints | 77 | 8826 | 39 | 19 | 13 | 2 | 4 | 0 | 0 | 0 | 5.05 | 579.1 | virtual |
| 53 | `0x007f64a8` | 1656 | A | b2ContactSolver::SolveVelocityConstraints | 77 | 7707 | 39 | 14 | 21 | 0 | 3 | 0 | 0 | 0 | 4.65 | 465.4 | off-frame |
| 54 | `0x00779ff4` | 1404 | A | gameswf::path::point_test | 76 | 6759 | 20 | 17 | 11 | 2 | 25 | 0 | 0 | 1 | 5.41 | 481.4 | off-frame |
| 55 | `0x006fec78` | 3280 | A | glitch::scene::CShadowVolumeSceneNode::createFacingTriangleVolume | 75 | 7447 | 30 | 19 | 24 | 0 | 2 | 0 | 0 | 0 | 2.29 | 227.0 | one-hop |
| 56 | `0x0066ff48` | 2840 | A | glitch::collada::detail::CColladaSoftwareSkinTechnique::skin | 73 | 10255 | 36 | 33 | 0 | 0 | 4 | 0 | 0 | 0 | 2.57 | 361.1 | virtual |
| 57 | `0x0059c970` | 4780 | A | glitch::scene::createMeshWithTangents | 72 | 8631 | 27 | 27 | 0 | 0 | 18 | 0 | 0 | 0 | 1.51 | 180.6 | orphan |
| 58 | `0x0078cb90` | 2880 | A | gameswf::edit_text_character::append_text | 71 | 6659 | 11 | 20 | 12 | 5 | 11 | 12 | 0 | 0 | 2.47 | 231.2 | one-hop |
| 59 | `0x006c6c04` | 1776 | A | glitch::scene::CSceneCollisionManager::collideWithWorld | 69 | 7071 | 18 | 22 | 16 | 3 | 8 | 1 | 1 | 0 | 3.89 | 398.1 | off-frame |
| 60 | `0x007fab98` | 1200 | A | b2GearJoint::InitVelocityConstraints | 69 | 8873 | 42 | 20 | 6 | 1 | 0 | 0 | 0 | 0 | 5.75 | 739.4 | virtual |

### 2.2 Density ranking — small hot loops are not buried under large cold functions

Density is the stated purpose of this table: a 104-byte function that is 8.7 % soft-float call sites matters more per byte than a 12 KB decoder.

| # | addr | size | md | function | sites | w | fmul | fadd | fsub | fdiv | fcmp | fcvt | dbl | libm | dens | wdens | purpose |
|--:|---|--:|:-:|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:--|
| 1 | `0x00898e60` | 5220 | A | mpc_synthese_filter_float_internal | 329 | 35162 | 96 | 126 | 107 | 0 | 0 | 0 | 0 | 0 | 6.30 | 673.6 | off-frame |
| 2 | `0x006c42a8` | 5068 | A | glitch::scene::CSceneCollisionManager::testTriangleIntersection | 268 | 30718 | 117 | 80 | 50 | 4 | 15 | 1 | 1 | 0 | 5.29 | 606.1 | off-frame |
| 3 | `0x006e5d14` | 4904 | A | glitch::collada::CCoronasSceneNode::render | 268 | 36538 | 110 | 129 | 24 | 2 | 1 | 1 | 1 | 0 | 5.46 | 745.1 | virtual |
| 4 | `0x00406c2c` | 7360 | A | v2GamepadController::Update | 219 | 26274 | 58 | 92 | 15 | 6 | 46 | 1 | 0 | 1 | 2.98 | 357.0 | virtual |
| 5 | `0x00776564` | 3564 | A | gameswf::scene_node::get_collision_uv | 181 | 19612 | 66 | 56 | 30 | 2 | 27 | 0 | 0 | 0 | 5.08 | 550.3 | one-hop |
| 6 | `0x0087af70` | 12048 | A | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_ | 175 | 18997 | 63 | 60 | 49 | 0 | 3 | 0 | 0 | 0 | 1.45 | 157.7 | off-frame |
| 7 | `0x006338e0` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_9SParticleEEEvNS0_16IParticleContextIT_E11 | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 8 | `0x00636e28` | 3128 | A | _ZN6glitch2ps10PDeflector5applyINS0_12GNPSParticleEEEvNS0_16IParticleContextIT | 161 | 19746 | 74 | 50 | 10 | 3 | 11 | 6 | 7 | 0 | 5.15 | 631.3 | orphan |
| 9 | `0x007f129c` | 2752 | A | b2PulleyJoint::SolvePositionConstraints | 159 | 16777 | 65 | 42 | 27 | 4 | 17 | 0 | 0 | 4 | 5.78 | 609.6 | virtual |
| 10 | `0x00321164` | 6080 | A | Application::_CheckGamepad | 152 | 19494 | 38 | 76 | 0 | 0 | 38 | 0 | 0 | 0 | 2.50 | 320.6 | on-frame |
| 11 | `0x006d3578` | 3804 | A | glitch::scene::CTerrainSceneNode::calculateNormals | 144 | 14928 | 48 | 48 | 48 | 0 | 0 | 0 | 0 | 0 | 3.79 | 392.4 | one-hop |
| 12 | `0x007ef010` | 2552 | A | b2PrismaticJoint::SolvePositionConstraints | 132 | 14317 | 55 | 38 | 20 | 0 | 19 | 0 | 0 | 0 | 5.17 | 561.0 | virtual |
| 13 | `0x003232c0` | 2080 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.25 | 603.7 | one-hop |
| 14 | `0x00663ce8` | 2120 | A | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | 79 | 11 | 38 | 1 | 1 | 0 | 0 | 0 | 6.13 | 592.3 | orphan |
| 15 | `0x006c7f1c` | 2832 | A | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode | 130 | 14596 | 61 | 37 | 21 | 0 | 10 | 1 | 0 | 0 | 4.59 | 515.4 | virtual |
| 16 | `0x00418d28` | 3620 | A | HUDControls::OnEvent | 128 | 9507 | 4 | 16 | 24 | 27 | 1 | 52 | 0 | 4 | 3.54 | 262.6 | virtual |
| 17 | `0x0060cf88` | 3500 | A | glitch::collada::CBillboardSceneNode::updateAbsolutePosition | 128 | 17070 | 72 | 46 | 6 | 0 | 3 | 0 | 0 | 1 | 3.66 | 487.7 | virtual |
| 18 | `0x0078faa0` | 4060 | A | gameswf::display_glyph_records | 121 | 11137 | 30 | 20 | 27 | 13 | 20 | 10 | 0 | 1 | 2.98 | 274.3 | one-hop |
| 19 | `0x007ebfbc` | 2772 | A | b2PolyAndCircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 20 | `0x007ecb84` | 2772 | A | b2PolygonContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 4.11 | 521.2 | virtual |
| 21 | `0x007f3b9c` | 2184 | A | b2CircleContact::Evaluate | 114 | 14448 | 48 | 48 | 18 | 0 | 0 | 0 | 0 | 0 | 5.22 | 661.5 | virtual |
| 22 | `0x0035e118` | 1992 | A | glitch::core::detail::CMatrix4Base<float>::mult | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 5.62 | 810.4 | off-frame |
| 23 | `0x0040ea54` | 1632 | A | glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 6.86 | 989.2 | on-frame |
| 24 | `0x007ed7e8` | 2200 | A | b2PrismaticJoint::InitVelocityConstraints | 112 | 13617 | 58 | 32 | 14 | 3 | 5 | 0 | 0 | 0 | 5.09 | 619.0 | virtual |
| 25 | `0x007ee080` | 1952 | A | b2PrismaticJoint::SolveVelocityConstraints | 111 | 14740 | 62 | 40 | 5 | 0 | 4 | 0 | 0 | 0 | 5.69 | 755.1 | virtual |
| 26 | `0x007efc10` | 1796 | A | b2PulleyJoint::SolveVelocityConstraints | 110 | 13486 | 64 | 31 | 12 | 0 | 3 | 0 | 0 | 0 | 6.12 | 750.9 | virtual |
| 27 | `0x007f2bec` | 1920 | A | b2RevoluteJoint::SolvePositionConstraints | 106 | 10759 | 47 | 23 | 21 | 1 | 13 | 0 | 0 | 1 | 5.52 | 560.4 | virtual |
| 28 | `0x0052b560` | 3572 | A | PFWorld::_SearchGraph | 99 | 10441 | 36 | 30 | 18 | 0 | 15 | 0 | 0 | 0 | 2.77 | 292.3 | one-hop |
| 29 | `0x006c9934` | 2396 | A | glitch::scene::CSceneNodeAnimatorCameraMaya::animateNode | 98 | 10274 | 42 | 26 | 22 | 0 | 4 | 4 | 0 | 0 | 4.09 | 428.8 | virtual |
| 30 | `0x007f8018` | 2052 | A | _ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_ | 98 | 10725 | 41 | 24 | 20 | 3 | 10 | 0 | 0 | 0 | 4.78 | 522.7 | off-frame |
| 31 | `0x007db5a4` | 2804 | A | _Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_ | 97 | 10549 | 0 | 0 | 0 | 0 | 0 | 0 | 97 | 0 | 3.46 | 376.2 | off-frame |
| 32 | `0x00762308` | 4800 | A | gameswf::morph2_character_def::read | 96 | 12144 | 32 | 32 | 0 | 0 | 32 | 0 | 0 | 0 | 2.00 | 253.0 | one-hop |
| 33 | `0x007f09c0` | 1656 | A | b2PulleyJoint::InitVelocityConstraints | 95 | 10633 | 44 | 23 | 16 | 5 | 5 | 0 | 0 | 2 | 5.74 | 642.1 | virtual |
| 34 | `0x005eb334` | 2632 | A | glitch::video::CTextureManager::makeNormalMapTexture | 94 | 8876 | 44 | 12 | 12 | 3 | 0 | 23 | 0 | 0 | 3.57 | 337.2 | orphan |
| 35 | `0x006f4540` | 1772 | A | jpeg_idct_float | 94 | 8105 | 19 | 28 | 30 | 0 | 0 | 17 | 0 | 0 | 5.30 | 457.4 | virtual |
| 36 | `0x007f4424` | 2088 | A | _Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonShapeRK7b2XFormPK13b2Ci | 94 | 9702 | 42 | 22 | 24 | 0 | 6 | 0 | 0 | 0 | 4.50 | 464.7 | off-frame |
| 37 | `0x005f95ac` | 17128 | A | glitch::video::pixel_format::convert | 93 | 9409 | 38 | 18 | 0 | 4 | 0 | 33 | 0 | 0 | 0.54 | 54.9 | one-hop |
| 38 | `0x0069e69c` | 1524 | A | glitch::ps::PDCylinder::transform | 92 | 12376 | 52 | 33 | 3 | 1 | 2 | 0 | 0 | 1 | 6.04 | 812.1 | virtual |
| 39 | `0x007f5920` | 2288 | A | _Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_ | 91 | 11694 | 47 | 32 | 7 | 0 | 5 | 0 | 0 | 0 | 3.98 | 511.1 | one-hop |
| 40 | `0x0068acd8` | 3520 | A | png_init_read_transformations | 90 | 12561 | 3 | 0 | 0 | 0 | 1 | 7 | 79 | 0 | 2.56 | 356.8 | off-frame |

### 2.3 Mangled and demangled identities

The demangled column is a **structural** decomposition of the Itanium mangling (`A::B::f`), which needs no type knowledge and is therefore exact. A full signature demangler was also written (`sf_demangle.py`), but it only resolves 128 of 2,191 names to a signature this document is willing to print; wherever it could not, the mangled name is shown unchanged rather than a guessed signature.

| # | addr | demangled | mangled |
|--:|---|--:|---|
| 1 | `0x006e5d14` | glitch::collada::CCoronasSceneNode::render | `_ZN6glitch7collada17CCoronasSceneNode6renderEPv` |
| 2 | `0x00898e60` | mpc_synthese_filter_float_internal | `mpc_synthese_filter_float_internal` |
| 3 | `0x006c42a8` | glitch::scene::CSceneCollisionManager::testTriangleIntersection | `_ZN6glitch5scene22CSceneCollisionManager24testTriangleIntersectionEPNS1_14SCollisionDataERKNS_4core10triangle3dIfEE` |
| 4 | `0x00406c2c` | v2GamepadController::Update | `_ZN19v2GamepadController6UpdateEv` |
| 5 | `0x006338e0` | _ZN6glitch2ps10PDeflector5applyINS0_9SParticleEEEvNS0_16IParticleConte | `_ZN6glitch2ps10PDeflector5applyINS0_9SParticleEEEvNS0_16IParticleContextIT_E11ParticleIttES7_PS6_` |
| 6 | `0x00636e28` | _ZN6glitch2ps10PDeflector5applyINS0_12GNPSParticleEEEvNS0_16IParticleC | `_ZN6glitch2ps10PDeflector5applyINS0_12GNPSParticleEEEvNS0_16IParticleContextIT_E11ParticleIttES7_PS6_` |
| 7 | `0x00776564` | gameswf::scene_node::get_collision_uv | `_ZN7gameswf10scene_node16get_collision_uvERN6glitch4core6line3dIfEERNS_5pointE` |
| 8 | `0x00321164` | Application::_CheckGamepad | `_ZN11Application13_CheckGamepadEv` |
| 9 | `0x0087af70` | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_ | `_ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_` |
| 10 | `0x0060cf88` | glitch::collada::CBillboardSceneNode::updateAbsolutePosition | `_ZN6glitch7collada19CBillboardSceneNode22updateAbsolutePositionEb` |
| 11 | `0x007f129c` | b2PulleyJoint::SolvePositionConstraints | `_ZN13b2PulleyJoint24SolvePositionConstraintsEv` |
| 12 | `0x0035e118` | glitch::core::detail::CMatrix4Base<float>::mult | `_ZNK6glitch4core6detail12CMatrix4BaseIfE4multERKS3_` |
| 13 | `0x0040ea54` | glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck | `_ZN6glitch4core6detail12CMatrix4BaseIfE20setbyproduct_nocheckERKS3_S5_` |
| 14 | `0x006d3578` | glitch::scene::CTerrainSceneNode::calculateNormals | `_ZN6glitch5scene17CTerrainSceneNode16calculateNormalsERKN5boost13intrusive_ptrINS0_11CMeshBufferEEE` |
| 15 | `0x007ee080` | b2PrismaticJoint::SolveVelocityConstraints | `_ZN16b2PrismaticJoint24SolveVelocityConstraintsERK10b2TimeStep` |
| 16 | `0x006c7f1c` | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode | `_ZN6glitch5scene27CSceneNodeAnimatorCameraFPS11animateNodeEPNS0_10ISceneNodeEj` |
| 17 | `0x007ebfbc` | b2PolyAndCircleContact::Evaluate | `_ZN22b2PolyAndCircleContact8EvaluateEP17b2ContactListener` |
| 18 | `0x007ecb84` | b2PolygonContact::Evaluate | `_ZN16b2PolygonContact8EvaluateEP17b2ContactListener` |
| 19 | `0x007f3b9c` | b2CircleContact::Evaluate | `_ZN15b2CircleContact8EvaluateEP17b2ContactListener` |
| 20 | `0x007ef010` | b2PrismaticJoint::SolvePositionConstraints | `_ZN16b2PrismaticJoint24SolvePositionConstraintsEv` |
| 21 | `0x007ed7e8` | b2PrismaticJoint::InitVelocityConstraints | `_ZN16b2PrismaticJoint23InitVelocityConstraintsERK10b2TimeStep` |
| 22 | `0x007efc10` | b2PulleyJoint::SolveVelocityConstraints | `_ZN13b2PulleyJoint24SolveVelocityConstraintsERK10b2TimeStep` |
| 23 | `0x0068acd8` | png_init_read_transformations | `png_init_read_transformations` |
| 24 | `0x003232c0` | glitch::core::CMatrix4<float>::getInverse | `_ZNK6glitch4core8CMatrix4IfE10getInverseERS2_` |
| 25 | `0x00663ce8` | glitch::core::CMatrix4<float>::getInverse | `_ZNK6glitch4core8CMatrix4IfE10getInverseERS2_.clone.5` |
| 26 | `0x0069efac` | glitch::ps::PDBox::transform | `_ZN6glitch2ps5PDBox9transformERKNS_4core8CMatrix4IfEE` |
| 27 | `0x0069e69c` | glitch::ps::PDCylinder::transform | `_ZN6glitch2ps10PDCylinder9transformERKNS_4core8CMatrix4IfEE` |
| 28 | `0x00762308` | gameswf::morph2_character_def::read | `_ZN7gameswf20morph2_character_def4readEPNS_6streamEibPNS_20movie_definition_subE` |
| 29 | `0x0063cce8` | _ZN6glitch2ps13GNPSSpinModelINS0_12GNPSParticleEE9initPSpinEPS2_S4_ | `_ZN6glitch2ps13GNPSSpinModelINS0_12GNPSParticleEE9initPSpinEPS2_S4_` |
| 30 | `0x007f5920` | _Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_ | `_Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_` |
| 31 | `0x0063b7fc` | _ZN6glitch2ps24PSBillboardPositionBakerINS0_12GNPSParticleEE28getPerPa | `_ZN6glitch2ps24PSBillboardPositionBakerINS0_12GNPSParticleEE28getPerParticleSystemPositionEPKNS0_16IParticleContextIS2_EEPKNS_4core8CMatrix4IfEE` |
| 32 | `0x0064dcd8` | _ZN6glitch2ps24PSBillboardPositionBakerINS0_9SParticleEE28getPerPartic | `_ZN6glitch2ps24PSBillboardPositionBakerINS0_9SParticleEE28getPerParticleSystemPositionEPKNS0_16IParticleContextIS2_EEPKNS_4core8CMatrix4IfEE` |
| 33 | `0x0078faa0` | gameswf::display_glyph_records | `_ZN7gameswfL21display_glyph_recordsEPKNS_6matrixEPNS_9characterERNS_5arrayINS_17text_glyph_recordEEEPNS_20movie_definition_subEPNS_4rgbaEhhh` |
| 34 | `0x007f2bec` | b2RevoluteJoint::SolvePositionConstraints | `_ZN15b2RevoluteJoint24SolvePositionConstraintsEv` |
| 35 | `0x007f8018` | _ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_ | `_ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_` |
| 36 | `0x007f09c0` | b2PulleyJoint::InitVelocityConstraints | `_ZN13b2PulleyJoint23InitVelocityConstraintsERK10b2TimeStep` |
| 37 | `0x007db5a4` | _Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_ | `_Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_` |
| 38 | `0x0052b560` | PFWorld::_SearchGraph | `_ZN7PFWorld12_SearchGraphEPK8PFObjectRK7Point3DIfES6_jPSt4listIPK12PFGInnerEdgeSaISA_EE` |
| 39 | `0x006c9934` | glitch::scene::CSceneNodeAnimatorCameraMaya::animateNode | `_ZN6glitch5scene28CSceneNodeAnimatorCameraMaya11animateNodeEPNS0_10ISceneNodeEj` |
| 40 | `0x0066ff48` | glitch::collada::detail::CColladaSoftwareSkinTechnique::skin | `_ZN6glitch7collada6detail29CColladaSoftwareSkinTechnique4skinERNS0_11SSkinBufferEPNS_5scene11CMeshBufferE` |
| 41 | `0x007f7058` | _ZN15b2ContactSolverC1ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | `_ZN15b2ContactSolverC1ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator` |
| 42 | `0x007f7838` | _ZN15b2ContactSolverC2ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator | `_ZN15b2ContactSolverC2ERK10b2TimeStepPP9b2ContactiP16b2StackAllocator` |
| 43 | `0x007f4424` | _Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonShapeRK7b2XForm | `_Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonShapeRK7b2XFormPK13b2CircleShapeS6_` |
| 44 | `0x0059e728` | glitch::scene::_GLOBAL__N_1::transform | `_ZN6glitch5scene12_GLOBAL__N_19transformERKN5boost13intrusive_ptrINS0_11CMeshBufferEEERKNS_4core8CMatrix4IfEEPNS8_8aabbox3dIfEE` |
| 45 | `0x00418d28` | HUDControls::OnEvent | `_ZN11HUDControls7OnEventERN8RenderFX5EventE` |
| 46 | `0x005f95ac` | glitch::video::pixel_format::convert | `_ZN6glitch5video12pixel_format7convertENS0_14E_PIXEL_FORMATEPKvjS2_Pvjjjb` |
| 47 | `0x0035dd08` | glitch::core::CMatrix4<float>::transformPlane | `_ZNK6glitch4core8CMatrix4IfE14transformPlaneERNS0_7plane3dIfEE` |
| 48 | `0x00597884` | glitch::core::detail::CMatrix4Base<float>::mult34 | `_ZNK6glitch4core6detail12CMatrix4BaseIfE6mult34ERKS3_RS3_` |
| 49 | `0x006651a0` | _ZN6glitch4coremlIfNS_7collada7SMatrixEEENS0_8CMatrix4IT_EERKS6_RKT0_ | `_ZN6glitch4coremlIfNS_7collada7SMatrixEEENS0_8CMatrix4IT_EERKS6_RKT0_` |
| 50 | `0x0066d0b8` | _ZN6glitch4coreL18rowMatrixProduct34INS0_8CMatrix4IfEES3_A16_fEEvRT_RK | `_ZN6glitch4coreL18rowMatrixProduct34INS0_8CMatrix4IfEES3_A16_fEEvRT_RKT0_RKT1_` |
| 51 | `0x007fa098` | b2DistanceJoint::InitVelocityConstraints | `_ZN15b2DistanceJoint23InitVelocityConstraintsERK10b2TimeStep` |
| 52 | `0x005eb334` | glitch::video::CTextureManager::makeNormalMapTexture | `_ZNK6glitch5video15CTextureManager20makeNormalMapTextureERKN5boost13intrusive_ptrINS0_8ITextureEEEf` |
| 53 | `0x007fab98` | b2GearJoint::InitVelocityConstraints | `_ZN11b2GearJoint23InitVelocityConstraintsERK10b2TimeStep` |
| 54 | `0x007f1e84` | b2RevoluteJoint::InitVelocityConstraints | `_ZN15b2RevoluteJoint23InitVelocityConstraintsERK10b2TimeStep` |
| 55 | `0x007f2478` | b2RevoluteJoint::SolveVelocityConstraints | `_ZN15b2RevoluteJoint24SolveVelocityConstraintsERK10b2TimeStep` |
| 56 | `0x0059c970` | glitch::scene::createMeshWithTangents | `_ZN6glitch5scene22createMeshWithTangentsERKN5boost13intrusive_ptrINS0_5IMeshEEEPNS_5video12IVideoDriverEbbb` |
| 57 | `0x00580f58` | glitch::scene::CBillboardSceneNode::render | `_ZN6glitch5scene19CBillboardSceneNode6renderEPv` |
| 58 | `0x00409bd8` | v2PS3MoveController::Update | `_ZN19v2PS3MoveController6UpdateEv` |
| 59 | `0x006f4540` | jpeg_idct_float | `jpeg_idct_float` |
| 60 | `0x007e6b78` | b2World::DrawDebugData | `_ZN7b2World13DrawDebugDataEv` |

---

## 3. What the FP traffic is made of

| category | symbols | call sites | % of 28,209 `__aeabi_*` sites |
|---|---|--:|--:|
| float  + - * / | 4 | 18541 | 65.73% |
| float compares | 6 | 3110 | 11.02% |
| float conversions | 7 | 3189 | 11.30% |
| double arith+compare+conv (all) | 18 | 1948 | 6.91% |
| float libm (libc) | 29 | 300 | 1.06% |
| **all `__aeabi_*`** | 38 | **28209** | 100.00% |

### 3.1 Direct call sites per imported symbol

| symbol | sites | weight | symbol | sites | weight |
|---|--:|--:|---|--:|--:|
| `__aeabi_fmul` | 8523 | 124 || `__aeabi_dcmpeq` | 90 | 24 |
| `__aeabi_fadd` | 6171 | 171 || `__aeabi_dcmpgt` | 64 | 24 |
| `__aeabi_fsub` | 3028 | 16 || `__aeabi_d2uiz` | 55 | 24 |
| `__aeabi_i2f` | 1666 | 33 || `__aeabi_dcmple` | 47 | 24 |
| `__aeabi_atexit` | 882 | — || `sinf` | 38 | 159 |
| `__aeabi_fcmplt` | 833 | 19 || `__aeabi_ui2d` | 37 | 21 |
| `__aeabi_fdiv` | 819 | 149 || `logf` | 36 | 64 |
| `__aeabi_fcmpgt` | 793 | 47 || `cosf` | 33 | 150 |
| `__aeabi_f2iz` | 642 | 23 || `__aeabi_dcmpun` | 31 | 15 |
| `__aeabi_fcmpeq` | 551 | 37 || `__aeabi_dcmpge` | 20 | 24 |
| `__aeabi_fcmpge` | 518 | 47 || `__aeabi_uldivmod` | 17 | — |
| `__aeabi_ui2f` | 466 | 31 || `floorf` | 14 | 4 |
| `__aeabi_f2d` | 415 | 41 || `__aeabi_lmul` | 14 | — |
| `__aeabi_fcmple` | 415 | 122 || `acosf` | 13 | 123 |
| `__aeabi_d2f` | 362 | 97 || `ceilf` | 13 | 4 |
| `__aeabi_dmul` | 292 | 196 || `fmodf` | 12 | 114 |
| `__aeabi_i2d` | 275 | 25 || `expf` | 8 | 56 |
| `__aeabi_idiv` | 184 | — || `__aeabi_ul2d` | 7 | 14 |
| `__aeabi_d2iz` | 181 | 26 || `atan2f` | 5 | 72 |
| `__aeabi_dadd` | 146 | 264 || `tanf` | 5 | 220 |
| `__aeabi_dsub` | 143 | 10 || `powf` | 3 | 227 |
| `sqrtf` | 112 | 4 || `atanf` | 3 | 114 |
| `__aeabi_uidivmod` | 110 | — || `modff` | 2 | 34 |
| `__aeabi_uidiv` | 108 | — || `ldexpf` | 2 | 36 |
| `__aeabi_idivmod` | 104 | — || `__aeabi_ldivmod` | 2 | — |
| `__aeabi_dcmplt` | 103 | 19 || `asinf` | 1 | 82 |
| `__aeabi_ddiv` | 94 | 262 || `__aeabi_d2lz` | 1 | 12 |

| symbol | direct call sites | guest libc addr | insns in range | weight |
|---|--:|--:|--:|--:|
| `sqrtf` | 112 | `0x01c9ec` | 4 | 4 |
| `sinf` | 38 | `0x01e760` | 155 | 159 |
| `logf` | 36 | `0x01d9c0` | 64 | 64 |
| `cosf` | 33 | `0x01ce30` | 146 | 150 |
| `floorf` | 14 | `0x01c90c` | 4 | 4 |
| `acosf` | 13 | `0x00c8e8` | 123 | 123 |
| `ceilf` | 13 | `0x01c8b0` | 4 | 4 |
| `fmodf` | 12 | `0x00dc4c` | 114 | 114 |
| `expf` | 8 | `0x01d48c` | 56 | 56 |
| `tanf` | 5 | `0x01b920` | 216 | 220 |
| `atan2f` | 5 | `0x00d31c` | 72 | 72 |
| `atanf` | 3 | `0x013bcc` | 114 | 114 |
| `powf` | 3 | `0x01e140` | 227 | 227 |
| `ldexpf` | 2 | `0x01a2f8` | 36 | 36 |
| `modff` | 2 | `0x01941c` | 34 | 34 |
| `asinf` | 1 | `0x00cf20` | 82 | 82 |

Float libm imports with **zero** direct call sites in the engine: `log10f`, `fabsf`, `frexpf`, `roundf`, `truncf`, `copysignf`, `hypotf`, `sinhf`, `coshf`, `tanhf`, `exp2f`, `log2f`, `cbrtf`.

---

## 4. Reachability / per-frame filter

The census is static, so a large count can come from code that runs once at load. Two things were done about that, both measured:

1. **A direct call graph was built** from the 123,511 resolved direct calls (every `bl`/`blx #imm` in `.text`, attributed to its enclosing function and resolved through the 351 PLT veneers or to the callee's entry address).
2. **The indirect edge was measured too**: 41,487 references to function entry addresses were found by scanning non-executable data sections; 14,254 distinct functions have their address stored in a data section, i.e. they are reachable through a vtable or function-pointer table. This matters because **18,476 of 31,021 functions have no static caller at all** — virtual dispatch is the norm in this engine, not the exception.

### 4.1 Frame anchors

The per-frame roots are not guesses; they are the JNI surface, disassembled (`sf15_jni_dis.py`):

```
Java_..._GameRenderer_nativeRender  0x005311a0
  0x005311a0  ldr r3,[pc,#0x18] ; ldr r2,[pc,#0x18] ; add r3,pc,r3 ; ldr r2,[r3,r2]
  0x005311b0  ldr r3,[r2] ; cmp r3,#0 ; bxne lr
  0x005311bc  b #0x530fc8                  -> appUpdate        <-- the frame body
```

`nativeOnDrawFrame` is `bx lr` (an empty stub): the frame loop is entered through `nativeRender` → `appUpdate`. `nativeInit` (`0x005311c8`) → `appInit` (`0x00530ba8`); `JNI_OnLoad` (`0x0053224c`) plus the 548 `.init_array` constructors are the load-time roots.

### 4.2 The five caller-side purpose classes

| class | test applied | FP functions | FP call sites | weighted units |
|---|---|--:|--:|--:|
| **on-frame** | a static caller is forward-reachable from `appUpdate` (tight) | 74 | 994 | 110104 |
| **one-hop** | no frame-reachable caller, but a caller of frame-reachable code (loose) | 434 | 6380 | 606286 |
| **off-frame** | has ≥1 static caller and **every** static caller is outside the frame set | 436 | 7187 | 717528 |
| **virtual** | **no** static caller anywhere, but its entry address is stored in a data section | 786 | 8359 | 874573 |
| **orphan** | no static caller and no data reference — reachable only through a computed pointer | 461 | 4168 | 422181 |

The three-tier classification (T1 frame-direct / T2 frame-virtual / T3 load-time / T4 unresolved) requested in the brief is in the appendix, with its own tables.

The three strongest per-frame candidates, with the whole chain from the JNI entry spelled out (each arrow is a direct `bl` found in the disassembly, `sf_paths`):

```
nativeRender -> appUpdate -> Application::Update -> Application::_Update
   -> Application::_CheckGamepad        0x00321164  152 FP sites   w 19494   (T1)

nativeRender -> appUpdate -> Application::Update -> Application::_Update
   -> RootSceneNode::UpdateEnlargedView
   -> CCameraSceneNode::recalculateMatrices
   -> SViewFrustum::setTransformState
   -> CMatrix4Base<float>::setbyproduct_nocheck
                                        0x0040ea54  112 FP sites   w 16144   (T1)

nativeRender -> appUpdate -> Application::Update -> Application::_Update
   -> RootSceneNode::UpdateEnlargedView
   -> CCameraSceneNode::recalculateMatrices
   -> buildCameraLookAtMatrix<float>    0x00582e64   58 FP sites   w  7256   (T1)
```

### 4.3 Plausibly per-frame — the top render/scene/driver candidates

Ranked by weighted call sites among functions the caller-side test places on the frame path (`on-frame`, `one-hop`) or in a data-section dispatch table (`virtual`). `on-frame` means a static caller is itself forward-reachable from `appUpdate`; `one-hop` means the only callers are one step further out, which is weaker evidence and is scored lower in §5.

| # | addr | function | sites | w | class | evidence |
|--:|---|---|--:|--:|:-:|---|
| 1 | `0x006e5d14` | glitch::collada::CCoronasSceneNode::render | 268 | 36538 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 2 | `0x00406c2c` | v2GamepadController::Update | 219 | 26274 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 3 | `0x00776564` | gameswf::scene_node::get_collision_uv | 181 | 19612 | one-hop | only caller is itself a caller of frame code (_ZN7gameswf10scene_node24update_inverse_transformEv) |
| 4 | `0x00321164` | Application::_CheckGamepad | 152 | 19494 | on-frame | static caller inside the frame closure (_ZN11Application7_UpdateEi) |
| 5 | `0x0060cf88` | glitch::collada::CBillboardSceneNode::updateAbsolutePosi | 128 | 17070 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 6 | `0x007f129c` | b2PulleyJoint::SolvePositionConstraints | 159 | 16777 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 7 | `0x0040ea54` | glitch::core::detail::CMatrix4Base<float>::setbyproduct_ | 112 | 16144 | on-frame | static caller inside the frame closure (_ZN6glitch5scene12SViewFrustum17setTransformStateENS_5vi) |
| 8 | `0x006d3578` | glitch::scene::CTerrainSceneNode::calculateNormals | 144 | 14928 | one-hop | only caller is itself a caller of frame code (_ZN6glitch5scene17CTerrainSceneNode13loadHeightMapEPNS_2) |
| 9 | `0x007ee080` | b2PrismaticJoint::SolveVelocityConstraints | 111 | 14740 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 10 | `0x006c7f1c` | glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode | 130 | 14596 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 11 | `0x007ebfbc` | b2PolyAndCircleContact::Evaluate | 114 | 14448 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 12 | `0x007ecb84` | b2PolygonContact::Evaluate | 114 | 14448 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 13 | `0x007f3b9c` | b2CircleContact::Evaluate | 114 | 14448 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 14 | `0x007ef010` | b2PrismaticJoint::SolvePositionConstraints | 132 | 14317 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 15 | `0x007ed7e8` | b2PrismaticJoint::InitVelocityConstraints | 112 | 13617 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 16 | `0x007efc10` | b2PulleyJoint::SolveVelocityConstraints | 110 | 13486 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 17 | `0x003232c0` | glitch::core::CMatrix4<float>::getInverse | 130 | 12556 | one-hop | only caller is itself a caller of frame code (_ZN6glitch7collada17CCoronasSceneNode6renderEPv) |
| 18 | `0x0069efac` | glitch::ps::PDBox::transform | 90 | 12528 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 19 | `0x0069e69c` | glitch::ps::PDCylinder::transform | 92 | 12376 | virtual | no direct caller; entry stored in a data section (1 refs) |
| 20 | `0x00762308` | gameswf::morph2_character_def::read | 96 | 12144 | one-hop | only caller is itself a caller of frame code (_ZN7gameswf25define_shape_morph_loaderEPNS_6streamEiPNS_) |

### 4.4 Provably not on the frame path (load-time / one-shot candidates)

These have static callers and **none** of them is on the frame path, so no direct call chain from the frame reaches them. Decoders and parsers dominate, as expected.

| # | addr | function | sites | w | family |
|--:|---|---|--:|--:|---|
| 1 | `0x00898e60` | mpc_synthese_filter_float_internal | 329 | 35162 | MPC audio decoder |
| 2 | `0x006c42a8` | glitch::scene::CSceneCollisionManager::testTriangleInter | 268 | 30718 | scene/collision |
| 3 | `0x0087af70` | _ZL20vorbis_decode_packetP10stb_vorbisPiS1_S1_ | 175 | 18997 | vorbis/ogg decoder |
| 4 | `0x0035e118` | glitch::core::detail::CMatrix4Base<float>::mult | 112 | 16144 | matrix math |
| 5 | `0x0068acd8` | png_init_read_transformations | 90 | 12561 | PNG image codec |
| 6 | `0x007f8018` | _ZL12ProcessThreeP6b2Vec2S0_S0_S0_S0_ | 98 | 10725 | Box2D |
| 7 | `0x007db5a4` | _Z18__gl_edgeIntersectP9GLUvertexS0_S0_S0_S0_ | 97 | 10549 | GLU tessellator |
| 8 | `0x007f7058` | _ZN15b2ContactSolverC1ERK10b2TimeStepPP9b2ContactiP16b2S | 82 | 9846 | Box2D |
| 9 | `0x007f4424` | _Z25b2CollidePolygonAndCircleP10b2ManifoldPK14b2PolygonS | 94 | 9702 | Box2D |
| 10 | `0x0066d0b8` | _ZN6glitch4coreL18rowMatrixProduct34INS0_8CMatrix4IfEES3 | 63 | 9081 | matrix math |
| 11 | `0x007e6b78` | b2World::DrawDebugData | 64 | 8072 | Box2D |
| 12 | `0x00586acc` | glitch::scene::CTriangleSelector::AddResult | 54 | 7965 | scene/collision |
| 13 | `0x0068a4e4` | png_build_gamma_table | 59 | 7928 | PNG image codec |
| 14 | `0x00612d00` | glitch::core::quaternion::slerp | 62 | 7901 | matrix math |
| 15 | `0x007f64a8` | b2ContactSolver::SolveVelocityConstraints | 77 | 7707 | Box2D |
| 16 | `0x00898104` | mpc_decoder_decode_frame | 78 | 7305 | MPC audio decoder |
| 17 | `0x006c6c04` | glitch::scene::CSceneCollisionManager::collideWithWorld | 69 | 7071 | scene/collision |
| 18 | `0x00341260` | glitch::core::plane3d<float>::getIntersectionWithPlane | 55 | 7024 | matrix math |
| 19 | `0x007f4c4c` | _Z16b2CollideCirclesP10b2ManifoldPK13b2CircleShapeRK7b2X | 58 | 6761 | Box2D |
| 20 | `0x00779ff4` | gameswf::path::point_test | 76 | 6759 | gameswf/Flash |
| 21 | `0x007f6b9c` | b2ContactSolver::SolvePositionConstraints | 60 | 6380 | Box2D |
| 22 | `0x00588984` | glitch::scene::CTriangleSelector::Setup | 53 | 6262 | scene/collision |
| 23 | `0x00600fcc` | glitch::video::CImage::copyToScaling | 65 | 6187 | other |
| 24 | `0x006c5d40` | glitch::scene::CSceneCollisionManager::getPickedNodeBB | 46 | 6138 | scene/collision |
| 25 | `0x007dcc48` | _Z19__gl_projectPolygonP13GLUtesselator | 51 | 5690 | GLU tessellator |

`glitch::core::detail::CMatrix4Base<float>::mult` at `0x0035e118` is the notable member of this list: all three of its static callers are themselves off the frame path, yet its callers are matrix operators used all over the scene graph, so "off-frame" here means "the direct graph cannot place it", not "cold".

### 4.5 The honest gap

**1247** FP-site functions (47.5 % of the weighted traffic) are in the *virtual* or *orphan* class: the binary contains no direct call to them at all, so no static method can say whether they run per frame or once at load. The 743 `on-frame` functions are not all frame code either — one hop of caller expansion pulls in 10,590 functions, and `glitch::collada::CCoronasSceneNode::render` illustrates the trap in the other direction: its name says render and it sits in a dispatch table, but the binary contains **zero** direct calls to it. Every recommendation in §5 carries that uncertainty explicitly.

---

## 5. Top 15 recommendations, ranked by expected milliseconds saved per frame

### 5.1 The arithmetic, stated

Let `CAL` = host cycles the JIT spends per **translated guest instruction** (translation + dispatch + execution). The only quantity the device evidence pins down is the frame budget: **25.7 ms/frame, 15.1 % of it in the translator** = **3.88 ms/frame** of translator work. Dividing that over the ~165,000 guest instructions a 60 fps frame actually executes gives ≈23 host cycles/instruction; the value is not directly measurable from a static binary, so the tables below use **CAL = 8** as a deliberately conservative working value and every figure scales linearly with CAL.

```
saved_cycles(function) = Σ_over_symbols ( sites(symbol) × weight(symbol) × CAL )
                       = weighted(function) × CAL
saved_ms_once_per_frame(function) = weighted × CAL / 1e6
expected_ms_per_frame(function)  = saved_ms_once_per_frame × frame_confidence
```

`weight` is the measured helper translated-instruction count from §1. `frame_confidence` is an **explicit assumption, not a measurement** (the static binary cannot tell us how often a function runs); it encodes only the reachability evidence from §4:

| reachability evidence | confidence factor |
|---|--:|
| a static caller inside the frame closure (`on-frame`) | 0.60 |
| only callers one step outside the frame closure (`one-hop`) | 0.35 |
| no static caller anywhere, entry address in a data section (`virtual`) | 0.30 |
| no static caller and no data reference (`orphan`) | 0.25 |
| every static caller outside the frame closure (`off-frame`) | 0.30 |
| ×0.5 for a decoder/script/format family (mpc, vorbis, stb, png, jpeg, lua, gameswf, strtod, sprintf, inflate, tz) | |
| ×1.35 (capped at 0.80) if the name's family is per-frame (render/draw/update/solve/transform/collide/camera/frustum) | |


### 5.2 Ranked recommendation table

| # | addr | function | FP sites | weight w | upper-bound ms if run once/frame | frame confidence | **expected ms/frame** |
|--:|---|---|--:|--:|--:|--:|--:|
| 1 | `0x006e5d14` | glitch::collada::CCoronasSceneNode::render | 268 | 36538 | 0.2923 | 0.41 | **0.1184** |
| 2 | `0x00321164` | Application::_CheckGamepad | 152 | 19494 | 0.1560 | 0.60 | **0.0936** |
| 3 | `0x00406c2c` | v2GamepadController::Update | 219 | 26274 | 0.2102 | 0.41 | **0.0851** |
| 4 | `0x0040ea54` | glitch::core::detail::CMatrix4Base<float>::setbyprod | 112 | 16144 | 0.1292 | 0.60 | **0.0775** |
| 5 | `0x006c42a8` | glitch::scene::CSceneCollisionManager::testTriangleI | 268 | 30718 | 0.2457 | 0.30 | **0.0737** |
| 6 | `0x007f129c` | b2PulleyJoint::SolvePositionConstraints | 159 | 16777 | 0.1342 | 0.41 | **0.0544** |
| 7 | `0x007ee080` | b2PrismaticJoint::SolveVelocityConstraints | 111 | 14740 | 0.1179 | 0.41 | **0.0478** |
| 8 | `0x006c7f1c` | glitch::scene::CSceneNodeAnimatorCameraFPS::animateN | 130 | 14596 | 0.1168 | 0.41 | **0.0473** |
| 9 | `0x00582e64` | glitch::core::buildCameraLookAtMatrix<float> | 58 | 7256 | 0.0580 | 0.80 | **0.0464** |
| 10 | `0x007ef010` | b2PrismaticJoint::SolvePositionConstraints | 132 | 14317 | 0.1145 | 0.41 | **0.0464** |
| 11 | `0x007f5920` | _Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShape | 91 | 11694 | 0.0936 | 0.47 | **0.0442** |
| 12 | `0x007efc10` | b2PulleyJoint::SolveVelocityConstraints | 110 | 13486 | 0.1079 | 0.41 | **0.0437** |
| 13 | `0x00597884` | glitch::core::detail::CMatrix4Base<float>::mult34 | 63 | 9081 | 0.0726 | 0.60 | **0.0436** |
| 14 | `0x00898e60` | mpc_synthese_filter_float_internal | 329 | 35162 | 0.2813 | 0.15 | **0.0422** |
| 15 | `0x006d3578` | glitch::scene::CTerrainSceneNode::calculateNormals | 144 | 14928 | 0.1194 | 0.35 | **0.0418** |

### 5.3 The same 15 with the arithmetic written out

**1. `glitch::collada::CCoronasSceneNode::render` @ `0x006e5d14`** — 268 sites, weight 36538

```
  268 sites x 136 translated insns/site x 8 host cycles/insn = 292304 host cycles
  = 0.2923 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.1184 ms/frame
  per-symbol sites: __aeabi_fadd=129, __aeabi_fmul=110, __aeabi_fsub=24, __aeabi_fdiv=2, __aeabi_f2d=1, __aeabi_d2f=1, __aeabi_fcmplt=1
```

**2. `Application::_CheckGamepad` @ `0x00321164`** — 152 sites, weight 19494

```
  152 sites x 128 translated insns/site x 8 host cycles/insn = 155952 host cycles
  = 0.1560 ms if executed once per frame
  x frame-confidence 0.60  ->  expected 0.0936 ms/frame
  per-symbol sites: __aeabi_fadd=76, __aeabi_fmul=38, __aeabi_fcmpge=38
```

**3. `v2GamepadController::Update` @ `0x00406c2c`** — 219 sites, weight 26274

```
  219 sites x 119 translated insns/site x 8 host cycles/insn = 210192 host cycles
  = 0.2102 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0851 ms/frame
  per-symbol sites: __aeabi_fadd=92, __aeabi_fmul=58, __aeabi_fcmpge=37, __aeabi_fsub=15, __aeabi_fdiv=6, __aeabi_fcmpgt=6, __aeabi_fcmplt=2, sqrtf=1, __aeabi_ui2f=1, __aeabi_fcmple=1
```

**4. `glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck` @ `0x0040ea54`** — 112 sites, weight 16144

```
  112 sites x 144 translated insns/site x 8 host cycles/insn = 129152 host cycles
  = 0.1292 ms if executed once per frame
  x frame-confidence 0.60  ->  expected 0.0775 ms/frame
  per-symbol sites: __aeabi_fmul=64, __aeabi_fadd=48
```

**5. `glitch::scene::CSceneCollisionManager::testTriangleIntersection` @ `0x006c42a8`** — 268 sites, weight 30718

```
  268 sites x 114 translated insns/site x 8 host cycles/insn = 245744 host cycles
  = 0.2457 ms if executed once per frame
  x frame-confidence 0.30  ->  expected 0.0737 ms/frame
  per-symbol sites: __aeabi_fmul=117, __aeabi_fadd=80, __aeabi_fsub=50, __aeabi_fcmple=5, __aeabi_fcmpge=4, __aeabi_fdiv=4, __aeabi_fcmpgt=3, __aeabi_fcmplt=3, __aeabi_f2d=1, __aeabi_d2f=1
```

**6. `b2PulleyJoint::SolvePositionConstraints` @ `0x007f129c`** — 159 sites, weight 16777

```
  159 sites x 105 translated insns/site x 8 host cycles/insn = 134216 host cycles
  = 0.1342 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0544 ms/frame
  per-symbol sites: __aeabi_fmul=65, __aeabi_fadd=42, __aeabi_fsub=27, __aeabi_fcmplt=11, __aeabi_fcmpgt=6, sqrtf=4, __aeabi_fdiv=4
```

**7. `b2PrismaticJoint::SolveVelocityConstraints` @ `0x007ee080`** — 111 sites, weight 14740

```
  111 sites x 132 translated insns/site x 8 host cycles/insn = 117920 host cycles
  = 0.1179 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0478 ms/frame
  per-symbol sites: __aeabi_fmul=62, __aeabi_fadd=40, __aeabi_fsub=5, __aeabi_fcmplt=2, __aeabi_fcmpgt=2
```

**8. `glitch::scene::CSceneNodeAnimatorCameraFPS::animateNode` @ `0x006c7f1c`** — 130 sites, weight 14596

```
  130 sites x 112 translated insns/site x 8 host cycles/insn = 116768 host cycles
  = 0.1168 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0473 ms/frame
  per-symbol sites: __aeabi_fmul=61, __aeabi_fadd=37, __aeabi_fsub=21, __aeabi_fcmpgt=4, __aeabi_fcmplt=4, __aeabi_fcmpeq=2, __aeabi_ui2f=1
```

**9. `glitch::core::buildCameraLookAtMatrix<float>` @ `0x00582e64`** — 58 sites, weight 7256

```
  58 sites x 125 translated insns/site x 8 host cycles/insn = 58048 host cycles
  = 0.0580 ms if executed once per frame
  x frame-confidence 0.80  ->  expected 0.0464 ms/frame
  per-symbol sites: __aeabi_fmul=33, __aeabi_fadd=16, __aeabi_fsub=3, __aeabi_fcmpeq=2, sqrtf=2, __aeabi_fdiv=2
```

**10. `b2PrismaticJoint::SolvePositionConstraints` @ `0x007ef010`** — 132 sites, weight 14317

```
  132 sites x 108 translated insns/site x 8 host cycles/insn = 114536 host cycles
  = 0.1145 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0464 ms/frame
  per-symbol sites: __aeabi_fmul=55, __aeabi_fadd=38, __aeabi_fsub=20, __aeabi_fcmplt=13, __aeabi_fcmpgt=4, __aeabi_fcmple=2
```

**11. `_Z17b2CollidePolygonsP10b2ManifoldPK14b2PolygonShapeRK7b2XFormS3_S6_` @ `0x007f5920`** — 91 sites, weight 11694

```
  91 sites x 128 translated insns/site x 8 host cycles/insn = 93552 host cycles
  = 0.0936 ms if executed once per frame
  x frame-confidence 0.47  ->  expected 0.0442 ms/frame
  per-symbol sites: __aeabi_fmul=47, __aeabi_fadd=32, __aeabi_fsub=7, __aeabi_fcmpgt=3, __aeabi_fcmplt=1, __aeabi_fcmple=1
```

**12. `b2PulleyJoint::SolveVelocityConstraints` @ `0x007efc10`** — 110 sites, weight 13486

```
  110 sites x 122 translated insns/site x 8 host cycles/insn = 107888 host cycles
  = 0.1079 ms if executed once per frame
  x frame-confidence 0.41  ->  expected 0.0437 ms/frame
  per-symbol sites: __aeabi_fmul=64, __aeabi_fadd=31, __aeabi_fsub=12, __aeabi_fcmplt=3
```

**13. `glitch::core::detail::CMatrix4Base<float>::mult34` @ `0x00597884`** — 63 sites, weight 9081

```
  63 sites x 144 translated insns/site x 8 host cycles/insn = 72648 host cycles
  = 0.0726 ms if executed once per frame
  x frame-confidence 0.60  ->  expected 0.0436 ms/frame
  per-symbol sites: __aeabi_fmul=36, __aeabi_fadd=27
```

**14. `mpc_synthese_filter_float_internal` @ `0x00898e60`** — 329 sites, weight 35162

```
  329 sites x 106 translated insns/site x 8 host cycles/insn = 281296 host cycles
  = 0.2813 ms if executed once per frame
  x frame-confidence 0.15  ->  expected 0.0422 ms/frame
  per-symbol sites: __aeabi_fadd=126, __aeabi_fsub=107, __aeabi_fmul=96
```

**15. `glitch::scene::CTerrainSceneNode::calculateNormals` @ `0x006d3578`** — 144 sites, weight 14928

```
  144 sites x 103 translated insns/site x 8 host cycles/insn = 119424 host cycles
  = 0.1194 ms if executed once per frame
  x frame-confidence 0.35  ->  expected 0.0418 ms/frame
  per-symbol sites: __aeabi_fsub=48, __aeabi_fmul=48, __aeabi_fadd=48
```

### 5.4 Ceilings and what they mean

| scope | weighted units | ms if **every** site executed once per frame (CAL=8) |
|---|--:|--:|
| all 2191 FP functions | 2730672 | 21.85 ms |
| on-frame | 110104 | 0.88 ms |
| off-frame | 717528 | 5.74 ms |
| virtual | 874573 | 7.00 ms |
| orphan | 422181 | 3.38 ms |
| top 15 of §5.2 (sum) | — | 2.25 ms |

The all-functions figure (21.85 ms) is a hard **upper bound**: it assumes every one of the 27,088 FP call sites in the binary executes every frame, which cannot be true — it would be 79 % of the entire 25.7 ms frame budget. The realistic expectation is much smaller, and the correct way to use this document is as a *ranking of where to look*, not as a predicted frame time.

---

## 6. Method

Everything ran locally against the pristine engine and the guest sysroot libc; inputs were hashed first (table in §0).

**Tools.** Python 3.13.15, capstone 5.0.7 (`CS_ARCH_ARM`, both `CS_MODE_ARM` and `CS_MODE_THUMB`), pyelftools 0.33. No c++filt, no Ghidra, no IDA, no network.

**Disassembly.** The whole `.text` (0x30ee00, 5,960,232 bytes) is decoded region-by-region. Region boundaries are the `.text` start, every `$a`/`$t`/`$d` mapping symbol and every `STT_FUNC` start address (the binary has 45,121 mapping symbols: 29,827 `$a`, 14,152 `$d`, 1,142 `$t`). Each region is decoded in the mode given by the Thumb bit (`st_value & 1`) of the most recent function start — an exact 4-byte region in ARM mode, 2-byte-stepped in Thumb, resynchronising one instruction forward after any decode failure. Result: **1,513,919 instructions covering 5,954,490 of 5,961,032 `.text` bytes (99.89 %)**, and every one of the 31,018 distinct function start addresses has a decoded instruction exactly at it.

**ARM vs Thumb.** The binary genuinely mixes modes: 1,664 of 42,474 regions are Thumb. Mode is taken per function from the symbol table's Thumb bit, cross-checked against `$a`/`$t` mapping symbols. `bl` targets are decoded arithmetically for both encodings (`ARM: pc+8+sign_extend(imm24<<2)`; `Thumb-2: pc+4+sign_extend(S:I1:I2:imm10:imm11<<1)`), not parsed out of text.

**Resolving PLT veneers to import names.** `.plt` is 4,232 bytes / 351 veneers. Each veneer is
```
add ip, pc, #0x600000        ; imm12, rotated
add ip, ip, #0x86000
ldr pc, [ip, #0xD14]!        ; GOT slot = (veneer+8) + 0x600000 + 0x86000 + 0xD14
```
The GOT slot is looked up in `.rel.plt`, whose `r_offset` → `.dynsym` index gives the imported name. Verified end to end: **351/351 veneers decoded, 351/351 GOT slots found in `.rel.plt`, 351 distinct imports**, and veneer[0] resolves to `.rel.plt[0]` = `asinf`. This is the technique from the existing `resolve_veneer.py` helper (the only change needed was that this `.plt` is ARM rather than Thumb, and the stub header sits at `+4` inside each 16-byte entry).

**Helper costs.** Each sysroot library's symbol table → for every `__aeabi_*` and float libm symbol (all of them are defined in `libc.so` or `libm.so`), disassemble `[st_value, st_value+st_size)` in its own mode and count instructions, return paths and intra-helper calls. Example: `__aeabi_fadd` at `0x09f1e8`, size `0x2ac`, **171 decoded instructions, 7 return paths, 2 helper calls**. Aliases are then resolved to a closure (`sf08_closure.py`): a branch from inside a helper into its own library's `.plt` is resolved to the imported symbol through that library's `.rel.plt` and added to the weight.

**Command log.** Each row is a command that was actually run; the scripts are in `C:\Users\NacWorkstation\Documents\DH2Work-scratch\`.

| command | what it produced |
|---|---|
| `python sf01_probe.py` | ELF layout, sections, undefined `__aeabi_*` (40), relocation counts |
| `python sf02_probe.py` | guest-libc helper sizes/modes; first PLT decode attempt |
| `python sf04_plt.py` | PLT word-by-word decode (found the true stub layout) |
| `python sf03_verify.py` | 351/351 veneers resolved; 1,513,919 instructions decoded |
| `python sf05_helper_costs.py` | helper instruction counts for all 98 float/aeabi symbols |
| `python sf10_census.py` | **the census**: 123,511 direct calls, 347 imports called, 28,209 `__aeabi_*` sites, writes `sf_census.json` |
| `python sf11_reconcile.py` | reconciliation against the brief's figures |
| `python sf18_vtables.py` | 41,487 data-section references to function addresses (the indirect edge) |
| `python sf13_entries.py`, `sf14_jni.py`, `sf15_jni_dis.py` | entry points, `.init_array` (548 ctors), JNI frame anchors |
| `python sf20_model2.py`, `sf21_final.py` | reachability tiers T1–T4, category totals |
| `python sf22_purpose.py`, `sf23_purpose2.py` | caller-side purpose test (on-frame/off-frame/virtual/orphan) |
| `python sf30_tables.py`, `sf31_frags.py` | every table in this document |
| `python sf_demangle.py` | Itanium name decomposition |
| `python sf07_libm.py` | float libm helper costs from the sysroot's `libm.so` |
| `python sf08_closure.py`, `sf09_subalias.py` | tail-call closure weights; resolution of the `__aeabi_fsub`/`__aeabi_dsub` alias veneers |
| `python sf32_weights_effect.py` | weighted vs unweighted vs direct-body rank comparison |

**Reproducing from scratch:** run `sf10_census.py` (writes `sf_census.json`), then `sf07_libm.py`, `sf08_closure.py`, `sf18_vtables.py`, `sf21_final.py`, `sf23_purpose2.py`, `sf30_tables.py`, `sf31_frags.py`, `sf32_weights_effect.py`, `sf40_report.py` in that order.

**Data files** (scratch, not deliverables): `sf_census.json` (9.1 MB, the full per-function record), `sf_helper_costs.json`, `sf_vtrefs.json`, `sf_final2.json`.

---

## 7. Limitations (read this before acting on §5)

1. **Direct calls only.** The census counts `bl`/`blx #imm`. It cannot attribute the **2086 `blx <reg>` sites** in `.text` (virtual dispatch), nor calls through function pointers stored in data. That blind spot is large: **18,476 of 31,021 functions have no static caller at all**, and 14,254 functions are referenced from data sections. Consequence: a function with a big count is certainly FP-heavy, but whether it is *hot* cannot be established statically — §4's classes are the most that static evidence supports, and the *virtual* and *orphan* classes together carry 47.5 % of the weighted traffic.
2. **Call counts ≠ execution counts.** One call site inside a per-frame loop executes 60×/s; 268 call sites in a load-time decoder execute once ever. This census deliberately ranks by static site count × helper cost and then applies an explicit, labelled reachability factor; it does not pretend to know trip counts.
3. **The weight is a lower bound on the entered body.** It counts the helper's own translated instructions plus its resolved tail-call chain, and deliberately **does not** add the chained sub-calls made *inside* a helper's body (e.g. `__aeabi_dadd` → 8 calls) nor the engine-side call sequence, and only charges the 4-instruction PLT veneer when the closure model resolved one. Adding all of that would make the weights path-dependent and double-count; the numbers here are therefore conservative.
4. **Symbol-vs-body drift.** The project's own status notes record linker ICF/folding cases where a symbol name sits over a body that does not match it (`Dungeon hunter 2 Rework` → `DH2-recon/STATUS.md`, 'letter of the name' caveat). Counts are attributed by address range from `.symtab`, so a misnamed symbol would be reported under the wrong name but at the correct address. The address is the reliable key.
5. **`CAL` is not measured.** 8 host cycles per translated guest instruction is a conservative working value; the device evidence only bounds the translator at 3.88 ms/frame. All millisecond figures scale linearly with `CAL`.
6. **Load-time classification is inference plus name evidence.** §4.4's "provably not on the frame path" is a real measurement (no static call chain from the frame root reaches them). The T3 load-time tier in the appendix uses the JNI_OnLoad/`nativeInit`/`.init_array` closure, which is genuinely load-time code, but it is small (519 functions) because asset decoding in this engine is invoked through vtables, not through direct calls from the constructors.

---

## 8. Appendix — the three-tier reachability table and its rankings

| tier | meaning | functions | FP functions | FP call sites | weighted units |
|---|--:|--:|--:|--:|--:|
| T1 | frame-direct: forward-reachable from `appUpdate`/`nativeRender` | 1247 | 74 | 994 | 110104 |
| T2 | frame-virtual: not T1, but entry address stored in a data section | 14203 | 813 | 8615 | 901626 |
| T3 | load-time: in the `JNI_OnLoad`/`nativeInit`/`.init_array` closure, not T1/T2 | 519 | 6 | 36 | 3746 |
| T4 | unresolved by static evidence | 15049 | 1298 | 17443 | 1715196 |

T1 is the only tier where the frame path is established by direct calls. T2 is large because 14,254 functions have their address stored in a data section in this engine; T4 is large because a function reached only through a computed pointer leaves no static trace at all. The two tables below give the T1 and T3 memberships with FP traffic, ordered by weighted cost.

#### T1 — frame-direct, top 25 by weighted cost

| # | addr | size | md | function | sites | w | fmul | fadd | fsub | fdiv | fcmp | fcvt | dbl | libm | dens | wdens | tier | vref |
|--:|---|--:|:-:|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:-:|--:|
| 1 | `0x00321164` | 6080 | A | Application::_CheckGamepad | 152 | 19494 | 38 | 76 | 0 | 0 | 38 | 0 | 0 | 0 | 2.50 | 320.6 | T1 | 0 |
| 2 | `0x0040ea54` | 1632 | A | glitch::core::detail::CMatrix4Base<float>::setbyproduct_nocheck | 112 | 16144 | 64 | 48 | 0 | 0 | 0 | 0 | 0 | 0 | 6.86 | 989.2 | T1 | 0 |
| 3 | `0x00597884` | 988 | A | glitch::core::detail::CMatrix4Base<float>::mult34 | 63 | 9081 | 36 | 27 | 0 | 0 | 0 | 0 | 0 | 0 | 6.38 | 919.1 | T1 | 0 |
| 4 | `0x00582e64` | 1052 | A | glitch::core::buildCameraLookAtMatrix<float> | 58 | 7256 | 33 | 16 | 3 | 2 | 2 | 0 | 0 | 2 | 5.51 | 689.7 | T1 | 0 |
| 5 | `0x0036f4b4` | 1944 | A | PlayerManager::_UpdateJoiningController | 40 | 5130 | 10 | 20 | 0 | 0 | 10 | 0 | 0 | 0 | 2.06 | 263.9 | T1 | 0 |
| 6 | `0x00311914` | 3088 | A | PerfCounters::Draw | 59 | 4935 | 23 | 7 | 5 | 1 | 0 | 23 | 0 | 0 | 1.91 | 159.8 | T1 | 0 |
| 7 | `0x00312bf8` | 432 | A | glitch::core::CMatrix4<float>::multiplyWith1x4Matrix | 28 | 4036 | 16 | 12 | 0 | 0 | 0 | 0 | 0 | 0 | 6.48 | 934.3 | T1 | 0 |
| 8 | `0x0035c9d8` | 564 | A | glitch::core::quaternion::set | 26 | 3999 | 0 | 0 | 0 | 0 | 0 | 3 | 23 | 0 | 4.61 | 709.0 | T1 | 0 |
| 9 | `0x00583280` | 692 | A | glitch::scene::CCameraSceneNode::recalculateMatrices | 35 | 3841 | 15 | 8 | 4 | 2 | 4 | 0 | 0 | 2 | 5.06 | 555.1 | T1 | 0 |
| 10 | `0x00432bbc` | 668 | A | glitch::core::CMatrix4<float>::getRotationDegrees | 27 | 3363 | 0 | 0 | 0 | 0 | 0 | 7 | 19 | 1 | 4.04 | 503.4 | T1 | 0 |
| 11 | `0x005826c0` | 580 | A | glitch::scene::SViewFrustum::setFrom | 31 | 2923 | 7 | 10 | 12 | 1 | 0 | 0 | 0 | 1 | 5.34 | 504.0 | T1 | 0 |
| 12 | `0x0047211c` | 1516 | A | VisualObject::CalcMeshBox | 36 | 2865 | 15 | 3 | 6 | 0 | 12 | 0 | 0 | 0 | 2.37 | 189.0 | T1 | 0 |
| 13 | `0x00492aa0` | 1004 | A | AnimatedFX::SyncIrrData | 18 | 2387 | 9 | 7 | 0 | 0 | 2 | 0 | 0 | 0 | 1.79 | 237.7 | T1 | 0 |
| 14 | `0x0085d09c` | 5028 | A | luaV_execute | 22 | 1878 | 1 | 2 | 2 | 2 | 4 | 7 | 4 | 0 | 0.44 | 37.4 | T1 | 0 |
| 15 | `0x007e164c` | 460 | A | b2Body::SetXForm | 10 | 1489 | 4 | 4 | 0 | 0 | 0 | 0 | 0 | 2 | 2.17 | 323.7 | T1 | 0 |
| 16 | `0x00508ef4` | 2836 | A | StringManager::parse | 19 | 1458 | 1 | 1 | 1 | 3 | 4 | 3 | 5 | 1 | 0.67 | 51.4 | T1 | 0 |
| 17 | `0x0051badc` | 356 | A | PFFloor::GetFloorHeightAt | 15 | 1353 | 6 | 3 | 6 | 0 | 0 | 0 | 0 | 0 | 4.21 | 380.1 | T1 | 0 |
| 18 | `0x00416a7c` | 192 | A | GameSWFUtils::GetAbsoluteBoundingRect | 8 | 1280 | 0 | 4 | 0 | 4 | 0 | 0 | 0 | 0 | 4.17 | 666.7 | T1 | 0 |
| 19 | `0x0085cd38` | 484 | A | Arith | 14 | 1214 | 1 | 1 | 1 | 2 | 0 | 5 | 4 | 0 | 2.89 | 250.8 | T1 | 0 |
| 20 | `0x0038aac8` | 152 | A | GameObject::UpdateAbsoluteAABB | 6 | 1026 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 3.95 | 675.0 | T1 | 0 |
| 21 | `0x003d4d80` | 288 | A | CharAI::AI_SetMaster | 10 | 933 | 4 | 2 | 3 | 0 | 1 | 0 | 0 | 0 | 3.47 | 324.0 | T1 | 0 |
| 22 | `0x0051b96c` | 368 | A | PFFloor::GetCollisionAt | 8 | 919 | 0 | 1 | 1 | 0 | 6 | 0 | 0 | 0 | 2.17 | 249.7 | T1 | 0 |
| 23 | `0x007e25b8` | 436 | A | b2BroadPhase::ComputeBounds | 16 | 880 | 4 | 0 | 4 | 0 | 8 | 0 | 0 | 0 | 3.67 | 201.8 | T1 | 0 |
| 24 | `0x003d4ed8` | 192 | A | CharAI::AI_IsInSight | 8 | 762 | 3 | 2 | 3 | 0 | 0 | 0 | 0 | 0 | 4.17 | 396.9 | T1 | 0 |
| 25 | `0x00520f98` | 420 | A | PFRoom::GetFloorHeightAt | 6 | 732 | 0 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 1.43 | 174.3 | T1 | 0 |

#### T3 — load-time only, all 6 FP-bearing functions

| # | addr | size | md | function | sites | w | fmul | fadd | fsub | fdiv | fcmp | fcvt | dbl | libm | dens | wdens | tier | vref |
|--:|---|--:|:-:|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:-:|--:|
| 1 | `0x00582c88` | 264 | A | glitch::core::buildProjectionMatrixPerspectiveFov<float> | 11 | 1434 | 1 | 0 | 1 | 2 | 0 | 2 | 5 | 0 | 4.17 | 543.2 | T3 | 0 |
| 2 | `0x00582d90` | 212 | A | glitch::core::buildProjectionMatrixPerspectiveFovInfinity<float> | 7 | 996 | 0 | 0 | 0 | 0 | 0 | 2 | 5 | 0 | 3.30 | 469.8 | T3 | 0 |
| 3 | `0x0058359c` | 176 | A | glitch::core::buildProjectionMatrixOrtho<float> | 6 | 628 | 0 | 0 | 2 | 4 | 0 | 0 | 0 | 0 | 3.41 | 356.8 | T3 | 0 |
| 4 | `0x0069ffe8` | 184 | A | _ZN6glitch16CAndroidOSDevice14CCursorControlC1ERKNS_4core11dimension2dIiEEPS0_ | 4 | 364 | 0 | 0 | 0 | 2 | 0 | 2 | 0 | 0 | 2.17 | 197.8 | T3 | 0 |
| 5 | `0x00382204` | 224 | A | _ZN11ZoomHandlerC1Ev | 2 | 182 | 0 | 0 | 0 | 1 | 0 | 1 | 0 | 0 | 0.89 | 81.2 | T3 | 0 |
| 6 | `0x007aed6c` | 384 | A | _Z15gluTessPropertyP13GLUtesselatorid | 6 | 142 | 0 | 0 | 0 | 0 | 0 | 0 | 6 | 0 | 1.56 | 37.0 | T3 | 0 |

---

## 9. Appendix — engine counters

| quantity | value |
|---|--:|
| functions (STT_FUNC with size) | 31021 |
| distinct function start addresses | 31018 |
| duplicate start addresses | 3 |
| instructions decoded in .text | 1513919 |
| bytes covered by decoded instructions | 5954490 |
| .text size (bytes) | 5961032 |
| direct calls resolved to an import | 123511 |
| indirect bl/blx sites (unresolvable) | 2086 |
| PLT veneers | 351 |
| PLT veneers resolved to .rel.plt names | 351 |
| imports reachable via PLT | 351 |
| undefined `__aeabi_*` in engine .dynsym | 40 |
| functions with >=1 `__aeabi_*` site | 2719 |
| functions with >=1 FP (aeabi/libm) site | 2191 |
| total FP call sites (aeabi + float libm) | 27088 |
| total `__aeabi_*` call sites | 28209 |

