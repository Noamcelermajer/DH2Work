# Irrlicht lineage mapping for `libDungeonHunter2.so`

**Engine under analysis.** Dungeon Hunter 2 HD v1.0.2 (Gameloft, 2011),
`libDungeonHunter2.so` — ELF32, `EM_ARM`, `ET_DYN`, 15,938,284 bytes, not stripped.
Primary copy: `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\libDungeonHunter2.so`.

**Claim under test.** The `glitch::video` / `glitch::scene` / `glitch::gui` /
`glitch::io` / `glitch::core` layer is a fork of the Irrlicht Engine, and the
`glitch::` namespace is what used to be `irr::`.

**Verdict in one line.** The `glitch::` namespace *is* a rename of `irr::`
(verified), the fork descends from the **Irrlicht 1.8 family, most probably
1.8.0–1.8.3** (medium-high confidence at family level, medium-low on the exact
point release), but by v1.0.2 the **video driver and the mesh/scene manager
interfaces had been rewritten to the point where no upstream C++ source is
ABI-compatible** — an upstream-grafting strategy is viable only as a *reference*,
not as a drop-in.

---

## 0. Evidence base and method

### 0.1 Binary evidence (read-only inputs used)

| Source | Path | Used for |
| --- | --- | --- |
| Pristine engine | `…\DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\libDungeonHunter2.so` | ELF header, `.comment`, raw string scan |
| Symbol table dump | `…\DH2Work-stage\compatibility\work\native-symbols\libDungeonHunter2-symbols.txt` | 32,574 lines; `.dynsym`/`.symtab` FUNC/OBJECT names, vtable symbols |
| Build filenames | `…\DH_sc-pr\recovered\native\symbols\libDungeonHunter2.so\build-source-filenames.json` | 867 original build-source basenames |
| Vtables | `…\libDungeonHunter2.so\vtables-001.json` | 1,628 vtables with slot targets and relocations |
| Function index | `…\libDungeonHunter2.so\function-index.csv` | 31,018 rows with `declared_size` |
| Strings | `…\libDungeonHunter2.so\strings-001.json` | 94,959 strings, section+address |
| Assembly | `…\DH_sc-pr\recovered\native\assembly\libDungeonHunter2.so\` | 3,625 per-class `.asm` listings (used for `getDriverType`) |

Reproduction scripts written for this analysis (scratch, outside DH2Work):
`C:\Users\NacWorkstation\Documents\DH2Work-scratch\{extract_vtables,find_version2,find_version3,compare2,reverse_diff,exact_diff,inventory_vt,dump_ifaces,dump_concrete,ns_breakdown,vt_by_size,parse_18_virtuals}.py`.

### 0.2 Upstream evidence — which mirror, and why

| Clone | URL / ref | Why |
| --- | --- | --- |
| `DH2Work-toolchain\irrlicht\svn2github-irrlicht.git` | `https://github.com/svn2github/irrlicht.git` (mirror clone) | **Primary.** A clone of `svn://svn.code.sf.net/p/irrlicht/code/` that preserves the SVN tree layout `tags/release-1.7`, `tags/release-1.8`, `trunk`, `branches/ogl-es`. This is what let me compare against **release trees directly**, not against a re-tagged GitHub fork. Caveat: the clone is stale (last commit 2015-01-16) and exposes **no** `CImageLoaderPVR.cpp` anywhere, so it cannot represent 1.8.1+. |
| `DH2Work-toolchain\irrlicht\zaki-ogl-es` | `https://github.com/zaki/irrlicht.git`, branch `ogl-es` | The OGL-ES branch lineage (`COGLES2Driver`, `EDT_OGLES2`) — the only plausible upstream source for GLES2 support. |
| `DH2Work-toolchain\irrlicht\minetest-irrlicht`, `…\mt-trunk` | `https://github.com/minetest/irrlicht.git` (branches `svn-trunk`, `svn-ogl-es`) | Cross-check of trunk/SVN content. |

**Failed upstream acquisition (recorded honestly).** The SourceForge release zips
were **not** obtainable: `https://downloads.sourceforge.net/project/irrlicht/Irrlicht%20SDK/1.8/1.8.5/irrlicht-1.8.5.zip`
(and 1.8.4 / 1.7.3) returned HTML landing pages, not archives (133,607 / 133,892 /
133,995-byte HTML documents beginning `<!doctype html>`); alternative SourceForge
edge hosts failed DNS or threw; `fossies.org` returned HTTP 401.
Consequently **all header quotes below are from the SVN tag `tags/release-1.8`,
whose `IrrCompileConfig.h` self-reports 1.8.0.** Statements about 1.8.1–1.8.5 are
explicitly marked as inference from the file inventory, not from those headers.

### 0.3 Verified vs inferred

Throughout this document: **[V]** = verified by direct reading of binary or
upstream file; **[I]** = inference from evidence; **[U]** = unresolved / not checked.

---

## 1. Which upstream Irrlicht version

### 1.1 The `glitch::` ↔ `irr::` rename is verified, not inferred **[V]**

The rename is literal and namespace-depth-preserving:

| Upstream (1.8) | Engine | Note |
| --- | --- | --- |
| `irr::IReferenceCounted` | `glitch::IReferenceCounted` | root namespace, not `glitch::core` |
| `irr::video::IVideoDriver` | `glitch::video::IVideoDriver` | |
| `irr::scene::ISceneNode` | `glitch::scene::ISceneNode` | |
| `irr::io::IFileSystem` | `glitch::io::IFileSystem` | |
| `irr::gui::IGUIEnvironment` | `glitch::gui::IGUIEnvironment` | |

Corroborating engine-side strings **[V]** (from `strings-001.json`):

```
_ZTVN6glitch5video12IVideoDriverE       ; vtable for glitch::video::IVideoDriver
_ZN6glitch17IReferenceCounted8onDeleteEv ; glitch::IReferenceCounted::onDelete()
_ZN6glitch2io13IIrrXMLReaderIcNS_17IReferenceCountedEED1Ev
_ZN6glitch5video12IVideoDriver13createTextureEPKcRKNS0_12STextureDescE
```

The XML reader is the giveaway: upstream 1.8 declares
`template <class char_type, class IReferenceCounted> class IIrrXMLReader`
(`include/irrXML.h`); the engine instantiates it as
`glitch::io::IIrrXMLReader<wchar_t, glitch::IReferenceCounted>`. The **class name
`IIrrXMLReader` was kept verbatim inside a namespace renamed to `glitch`** **[V]**.

### 1.2 The engine's own build tree proves a deep, pre-existing restructure **[V]**

Four `.rodata` assert strings embed the original relative source paths:

```
..\..\project_vs2005\Game/..\..\sources\Core\Irrlicht\SceneManager.cpp
..\..\project_vs2005\Game/..\..\sources\Core\Irrlicht\Nodes\RootSceneNode.cpp
..\..\project_vs2005\Game/..\..\sources\Core\Irrlicht\Nodes\Animators\AnimatorBlender.cpp
..\..\project_vs2005\Game/..\..\sources\Core\Irrlicht\../../Utils/StreamBuffer.h
```

plus `_GLOBAL__I_.._.._sources_Core_Irrlicht_{ColladaFactory,FileSystemBase,FileSystemWin32,IrrFactory,SceneManager}.cpp`
and `_GLOBAL__I_.._.._sources_Core_Irrlicht_Nodes_{MeshSceneNode,ShadowMeshSceneNode,SkyBoxMeshSceneNode,XrayMeshSceneNode,RootSceneNode}.cpp`.

Interpretation **[V for strings, I for layout]**: the fork lives at repo root
(`project_vs2005/Game`), the engine at `sources/Core/Irrlicht/`, with a sibling
`sources/Utils/`. Irrlicht's flat `source/Irrlicht/` layout was reorganised into
`Core/Irrlicht/Nodes/`, `Nodes/Animators/`, plus game-side directories. Build
system is Visual Studio 2005-era, though the shipped binary is Android/ARM.

### 1.3 Version discrimination by original build filenames **[V]**

867 basenames were compared against the `.cpp/.c` basenames of each upstream
release tree. Raw overlap is low (150–153 of ~350–395 upstream files) because the
overlap set is dominated by the game's own 500+ files; the **discriminating**
signal is which upstream files are *absent* and which *later* files are *present*.

Decisive markers, each checked in the SVN mirror **[V]**:

| Marker | 1.7.2 | 1.8 (tag) | Engine | Reading |
| --- | --- | --- | --- | --- |
| `CImageLoaderDDS.cpp` | absent | **present** | **present** | 1.8-only addition; engine has it ⇒ **not 1.7** |
| `CImageLoaderPCX/PPM/PSD/RGB/WAL.cpp` | present | present | **absent** | engine pruned them (4 helpers dropped) |
| `CImageLoaderPVR.cpp` | absent | absent | **present** | engine-only / Android-port addition **[I: added in 1.8.1+; not directly verifiable — see 0.2]** |
| `CImageLoaderATC.cpp` | absent | absent | **present** | engine-only (Android ATC) |
| `CSceneLoaderIrr.cpp` | absent | **present** | absent | added upstream in 1.8 (commit `7ef2f54e`); engine kept its own XML path instead |
| `CAnimatedMeshHalfLife.cpp`, `CSMFMeshFileLoader.cpp`, `CWADReader.cpp`, `CSceneLoaderIrr.cpp` | – | 1.8 additions | **absent** | engine pruned 1.8's new loaders ⇒ fork predates their adoption |
| `CTRNormalMap.cpp`, `CTRStencilShadow.cpp` | – | 1.8 additions | **absent** | software-reference path dropped entirely |
| `irrXML.cpp` (standalone reader TU) | present | absent | absent | consistent with 1.8 header-template `IIrrXMLReader<wchar_t,…>` |
| `CColladaFileLoader.cpp` | present | present | **absent** | engine replaced Irrlicht's Collada loader with `glitch::collada::CColladaFactory` (+105 collada vtables) |

### 1.4 What the engine itself says about its driver generation **[V]**

`glitch::video::COpenGLES2Driver::getDriverType()` is 8 bytes:

```
005aefa4  08 00 a0 e3    mov r0, #8
005aefa8  1e ff 2f e1    bx lr
```
(`…\native\assembly\libDungeonHunter2.so\glitch_video_COpenGLES2Driver-602c654b281c-001.asm`)

Comparing `EDriverTypes.h`:

| Tree | enum tail | `EDT_OGLES2` value |
| --- | --- | --- |
| `tags/release-1.8` | `EDT_NULL, EDT_SOFTWARE, EDT_BURNINGSVIDEO, EDT_DIRECT3D8, EDT_DIRECT3D9, EDT_OPENGL, EDT_COUNT` | **no OGLES at all** |
| `branches/ogl-es` (SVN) | … `EDT_OPENGL, EDT_OGLES1, EDT_OGLES2, EDT_COUNT` | `EDT_OGLES2 == 8` |
| `zaki/irrlicht` `ogl-es` (mod. trunk) | … `EDT_OPENGL, EDT_OGLES1, EDT_OGLES2, EDT_WEBGL1, EDT_COUNT` | `EDT_OGLES2 == 8` |

So the return value 8 lands exactly on `EDT_OGLES2` of the SVN `ogl-es` branch.
But the engine contains **zero** `EDT_*` symbols and **zero** `EDT_*` strings
**[V]**, so the engine's `E_DRIVER_TYPE` is a Glitch-authored enum, and 8 is
coincidence-of-ordering rather than inheritance. Note this also means the engine
is **not** a descendant of `tags/release-1.8` *as shipped*, which has no GLES
driver whatsoever — GLES2 support must come from the ogl-es branch or from
Gameloft's own work.

### 1.5 Version conclusion

> **The engine's Irrlicht ancestry is the 1.8 family.** Confidence:
> **high (~0.85)** that it is 1.8-line rather than 1.7-line — driven by
> `CImageLoaderDDS.cpp` presence (1.8-only **[V]**), the header-template
> `IIrrXMLReader<wchar_t, IReferenceCounted>` form, and the pruning pattern that
> exactly matches 1.8's loader set minus its newest additions.
> **Medium (~0.5)** for the specific point release. The best-supported single
> choice is **1.8.0** (the SVN `tags/release-1.8` content I could actually read),
> with 1.8.1–1.8.3 equally consistent. **1.8.5 is less likely [I]**: the engine
> lacks `CSceneLoaderIrr`-style 1.8-era additions and the whole `CTR*` software
> path, and 1.8.5's Android/EGL surface code has no counterpart in the engine;
> however I could not obtain 1.8.5 sources, so this is **[U]** and should not be
> relied on.
> **The GLES2 driver is *not* inherited from upstream 1.8** — it is a rewrite
> (see §2.1). Version pinning therefore only constrains the *scene/gui/io/core*
> skeleton, not the renderer.

---

## 2. Concrete mapping table

Notation: **slots** = vtable entries in the engine as extracted from
`vtables-001.json` (header words `offset-to-top`, `typeinfo` already excluded).
Upstream counts are **virtual method declarations parsed from the 1.8 headers**
(`parse_18_virtuals.py`), so a like-for-like comparison needs `upstream + 1`
(each engine vtable opens with two destructor entries, then `onDelete`).
Verdict vocabulary: **match** / **approximate** / **diverged** / **absent** / **engine-only**.

### 2.1 `glitch::video` — the renderer (the important part)

| Engine symbol / class | Slots | Upstream Irrlicht (1.8) | Verdict |
| --- | --- | --- | --- |
| `glitch::video::IVideoDriver` | **134** (`_ZTVN6glitch5video12IVideoDriverE` @ `0x977330`) | `include/IVideoDriver.h`, class `irr::video::IVideoDriver`, **115** virtuals | **diverged (rewritten)** |
| `glitch::video::CNullDriver` | **138** (`@0x977d40`) | `source/Irrlicht/CNullDriver.h` | **diverged** — engine adds 4 slots (`getProcessBuffer`, `releaseProcessBuffer`, `createBinding`, `flush`); upstream `CNullDriver` is a 1000+-line concrete driver, engine's is a thin base |
| `glitch::video::CBatchDriver` | **138** (`@0x977100`) | **no upstream counterpart** | **engine-only** (batch/deferred draw-call driver) |
| `glitch::video::CCommonGLDriverBase` | **134** (`@0x10006112`) | **no upstream counterpart** | **engine-only**; vtable is identical in shape to `IVideoDriver` |
| `glitch::video::CCommonGLDriver<CProgrammableGLDriver<CGLSLShaderHandler>, detail::CProgrammableGLFunctionPointerSet>` | **135** (`@0x9926840`) | no upstream counterpart; loosely `COGLES2Driver.cpp` / `COpenGLDriver.cpp` | **engine-only** |
| `glitch::video::CProgrammableGLDriver<CGLSLShaderHandler>` | **135** (`@0x9925992`) | no counterpart; loosely `COpenGLSLMaterialRenderer` / `COGLES2MaterialRenderer` | **engine-only** |
| `glitch::video::COpenGLES2Driver` | **135** (`@0x977b18`/`0x9927448`) | `branches/ogl-es/source/Irrlicht/COGLES2Driver.cpp` | **diverged** — template-stack composition + full render-state API (see below) |
| `glitch::video::ITexture` | **9** (`@0x978210`) | `include/ITexture.h`, **8** virtuals | **approximate** (engine adds `IReferenceCounted::onDelete` already counted ⇒ effectively like-for-like) |
| `glitch::video::IBuffer` | **9** (`@0x9770d0`) | `include/IBuffer.h` **exists in 1.8**? **[U]** — not exercised | **approximate/unresolved** |
| `glitch::video::C2DDriver` | 3 (`@0x9770b8`) | no counterpart | engine-only |
| `glitch::video::CTextureManager` | (38 syms) | `source/Irrlicht/CTextureManager.cpp` | **approximate** — engine adds `createTextureFromImage`, `getTextureInternal`, `addTexture(…, E_TEXTURE_LAYOUT)` |
| `glitch::video::CImageLoader*` (`BMP`,`JPG`,`PNG`,`TGA`,`DDS`,`PVR`,`ATC`), `CImageWriter*` (`JPG`,`PNG`,`TGA`) | 9 / 5 each | `source/Irrlicht/CImageLoader*.cpp`, `CImageWriter*.cpp` | **near-match** — same class names; loader set pruned to what the game ships |
| `glitch::video::S3DVertex` | — | `include/S3DVertex.h`: `S3DVertex{Pos,Normal,Color,TCoords}`, `S3DVertex2TCoords`, `S3DVertexTangents`, keyed by `E_VERTEX_TYPE{EVT_STANDARD,EVT_2TCOORDS,EVT_TANGENTS}` | **absent / replaced** — engine has **no `S3DVertex`, no `E_VERTEX_TYPE`**; it uses `glitch::video::E_VERTEX_ATTRIBUTE` + `E_VERTEX_ATTRIBUTE_VALUE_TYPE` with `CVertexStreams`/`SVertexStream` |
| `glitch::video::IVideoDriver::createTexture` | — | 1.8: `ITexture* addTexture(const io::path&, IImage*)` and `ITexture* getTexture(...)`, `IVideoDriver::createTexture` does **not** exist | **diverged** — engine: `_ZN6glitch5video12IVideoDriver13createTextureEPKcRKNS0_12STextureDescE` = `createTexture(const char*, const STextureDesc&)`, back-ended by `CNullDriver::createTextureImpl(const char*, const STextureDesc&)` |
| `glitch::video::SMaterial` | — | `include/SMaterial.h` | **replaced by `glitch::video::CMaterial`** (refcounted class, no vtable ⇒ non-polymorphic **[V]**, 441 symbol hits) |
| `glitch::video::IMaterialRenderer` | — | `include/IMaterialRenderer.h` | **absent** — `IMaterialTechniqueMapsReader` + `CMaterialRendererManager` take its role |
| `glitch::video::IVideoDriver::IFramebuffer`, `IRenderTarget`, `IRenderBuffer`, `IMultipleRenderTarget`, `IVideoDriver::ICompileData` | 6 / 6 / 4 / 13 / 3 | no upstream counterpart | engine-only nest types |

**The single most important structural fact [V].** The engine's `IVideoDriver`
vtable was dumped slot-by-slot. Of its **134** entries, only 13 resolve to real
`glitch::video::IVideoDriver::` definitions (`~IVideoDriver` ×2, `onDelete`,
`beginScene`, `endScene`, `draw3DLine`, `draw3DLines`, `draw3DTriangle`,
`draw3DBox`, `draw2DLine`, `draw2DLines`, `draw`, `getMaximalPrimitiveCount`,
`checkDriverReset`, `setMaxTextureSize`, `pushRenderTarget`, `popRenderTarget`,
`onResize`, `setOption`, `removeUnused`, `getProcessBuffer`,
`releaseProcessBuffer`, `createBinding`, `flush`, `onMaterialDestroyed`,
`onShaderDestroyed`). **96 of the 134 slots are `__cxa_pure_virtual` stubs** —
i.e. the linker proved nothing in the binary calls them. Not one upstream
convenience method name survives: there is **no** `addTexture`, `getTexture`,
`draw2DImage`, `drawMeshBuffer`, `setMaterial`, `setTransform`,
`getMaterialRenderer`, `getMeshManipulator`, `createScreenShot`,
`getExposureVideoData`, `queryFeature`, `getDriverAttributes` in the
`IVideoDriver` vtable. Upstream's slot order (`beginScene`, `endScene`,
`queryFeature`, `disableFeature`, …) is **not** the engine's order (engine:
`beginScene`, `endScene`, then straight into `draw3DLine`).

**What replaced it [V].** `COpenGLES2Driver`'s 135 slots are a *declarative
render-state machine* with paired getter/setter virtuals — a design that does not
exist anywhere in upstream Irrlicht 1.8:

```
 44- 45  getBlendEnable / setBlendEnable          72- 73  get/setDitherEnable
 46- 47  getBlendColor / setBlendColor            74- 75  get/setLineWidth
 48- 49  getBlendEquation / setBlendEquation      76- 77  get/setPointSize
 50- 51  getBlendFunc / setBlendFunc              78- 81  get/setPolygonModeFront|Back
 52- 53  getColorMask / setColorMask              82- 89  get/setPolygonOffset*
 54- 55  getClearColor / setClearColor            90- 97  get/setSample* (coverage/alpha)
 56- 61  get/setCullFaceEnable|CullFace|FrontFace 98-101  get/setScissor*
 62- 71  get/setDepthTest|Func|Mask|ClearDepth|Range  102-119 get/setStencil*
120-121  getRenderState / setRenderState(detail::driver::SRenderState const&)
122      setViewportImpl          128  drawImpl(...)
123      getMaxUserClipPlanes     129  createTextureImpl(char const*, STextureDesc const&)
124-125  getProcessBuffer/releaseProcessBuffer
126      createBinding            130-131 commitCurrentMaterial / commitMaterialRenderer
127      flush                    132-133 onMaterialDestroyed / onShaderDestroyed
134      COpenGLES2Driver::swapBuffersImpl(int)
```
`getRenderState`/`setRenderState` over a bundled `SRenderState` plus
`commitCurrentMaterial` is a deliberate draw-call/state-change batching design —
this is where the engine's performance work already lives **[I]**, and it is the
part with **no upstream equivalent to graft from**.

### 2.2 `glitch::scene`

| Engine symbol / class | Slots | Upstream Irrlicht (1.8) | Verdict |
| --- | --- | --- | --- |
| `glitch::scene::ISceneNode` | **73** (incl. 2×`IReferenceCounted` secondary base; ✓70–72 are thunks) | `include/ISceneNode.h`; declared-only count via §2.6 ≈ **37** like-for-like | **diverged (+~34 virtuals)** |
| `glitch::scene::IMeshSceneNode` | **75** | `include/IMeshSceneNode.h`, 5 virtuals (+ base) | **diverged** |
| `glitch::scene::IAnimatedMeshSceneNode` | **85** | `include/IAnimatedMeshSceneNode.h` | **diverged** |
| `glitch::scene::CAnimatedMeshSceneNode` | **85** | `source/Irrlicht/CAnimatedMeshSceneNode.cpp` | diverged |
| `glitch::scene::CSceneManager` | **46** | `source/Irrlicht/CSceneManager.cpp` implements `irr::scene::ISceneManager` (96 virtuals) | **diverged/flattened** |
| `glitch::scene::ISceneManager` | **absent** | `include/ISceneManager.h` | **absent** — the `I*`/`C*` manager split was collapsed to a single concrete `CSceneManager` **[V: no `_ZTVN6glitch5scene13ISceneManagerE`, no `…ISceneManager…` symbol at all]** |
| `glitch::scene::IMesh` | **12** (slots 3–10 are stubs; only `IMesh::getUserProperty()` real) | `include/IMesh.h`, 10 virtuals | **diverged** |
| `glitch::scene::CMesh` | **12** | `source/Irrlicht/CMesh.cpp` | **diverged** — `getMaterial(idx)`/`getMaterialVertexAttributeMap(idx)`/`setMaterial(idx, ip<CMaterial>, ip<CMaterialVertexAttributeMap>)` replace `IMeshBuffer`-centric access |
| `glitch::scene::IMeshBuffer` | **absent** | `include/IMeshBuffer.h`, **27** virtuals | **absent** |
| `glitch::scene::SMeshBuffer`, `SMeshBufferLightMap` | **absent** | `include/SMeshBuffer.h`, `include/SMeshBufferLightMap.h` (template impls of `IMeshBuffer`) | **absent** — confirmed by exhaustive scan: **0** occurrences of the string `SMeshBuffer` in a 32,574-line symbol dump **[V]** |
| `glitch::scene::CMeshBuffer` | 3 slots (thin) | `include/CMeshBuffer.h` `template<class T> class CMeshBuffer : public IMeshBuffer` | **diverged** — engine's is a non-template refcounted container keyed by `glitch::collada::SMesh` + `SBufferConfig` |
| `glitch::scene::CBatchMesh`, `CBatchSceneNode`, `CAppendMeshBuffer` | 80 (batch node) | no counterpart | **engine-only** (mesh batching/compilation) |
| `glitch::scene::CBatchSceneNode` | **80** | — | engine-only |
| `glitch::scene::CSceneNodeAnimator*` (10 classes) | — | `CSceneNodeAnimatorCameraFPS/Maya/CollisionResponse/Delete/FlyCircle/FlyStraight/FollowSpline/Rotation/Texture` | **near-match** — filenames identical |
| `glitch::scene::CParticle*` (emitters/affectors) | — | same filenames upstream | **near-match** (engine drops `CParticleScaleAffector`, adds `CParticleSizeAffector`, `CParticleSpinAffector`, `CParticleBillboardBakerModel`, `CParticleGenericBakerModel`) |
| `glitch::scene::CSkyBoxSceneNode`, `CTerrainSceneNode`, `CShadowVolumeSceneNode`, `CTextSceneNode`, `CDummyTransformationSceneNode`, `CEmptySceneNode`, `CLightSceneNode`, `CBillboardSceneNode` | 74 / 75 / 78 / 85 / 75 / 73 / 73 / 93 | same filenames upstream | **near-match by name, slot counts inflated** |
| `glitch::core::rect<T>`, `vector3d<T>`, `CMatrix4<T>`, `aabbox3d<T>`, `dimension2d<T>`, `quaternion`, `triangle3d<T>`, `plane3d<T>` | — | `include/rect.h`, `vector3d.h`, `matrix4.h`, `aabbox3d.h`, `dimension2d.h`, `quaternion.h`, `triangle3d.h`, `plane3d.h` | **near-match (names)** — engine writes `glitch::core::CMatrix4<float>` where upstream writes `core::matrix4` (typedef of `CMatrix4<f32>`); `dimension2d<int>` where upstream prefers `dimension2d<u32>` |
| `glitch::scene::CCollada*`, `ICollada*` (105 vtables) | — | `CColladaFileLoader.cpp` + `CColladaMeshWriter.cpp` only | **diverged** — a full in-house Collada pipeline replaced Irrlicht's loader |

### 2.3 `glitch::gui`

| Engine symbol / class | Slots | Upstream Irrlicht (1.8) | Verdict |
| --- | --- | --- | --- |
| `glitch::gui::IGUIEnvironment` | **66** | `include/IGUIEnvironment.h`, **60** virtuals | **diverged (+~6)** |
| `glitch::gui::CGUIEnvironment` | **122** (incl. `IGUIElement` secondary base) | `source/Irrlicht/CGUIEnvironment.cpp` | **diverged** — adds TTF API (`getTTFont(const char*, u32)`, `getTTFont(IReadFile*, u32)`, `removeTTFont*`, `removeTTFontFace*`) and `OnPostRender(u32)`; these are **not upstream 1.8** (`CGUITTFont.cpp` is not in the 1.8 tree — it is a widely used community/Android-port file) **[V]** |
| `glitch::gui::IGUIElement` | **50** | `include/IGUIElement.h` | diverged |
| `CGUIButton`, `CGUICheckBox`, `CGUIColorSelectDialog`, `CGUIComboBox`, `CGUIContextMenu`, `CGUIEditBox`, `CGUIFileOpenDialog`, `CGUIFont`, `CGUIImage`, `CGUIInOutFader`, `CGUIListBox`, `CGUIMenu`, `CGUIMeshViewer`, `CGUIMessageBox`, `CGUIModalScreen`, `CGUIScrollBar`, `CGUISkin`, `CGUISpinBox`, `CGUISpriteBank`, `CGUIStaticText`, `CGUITabControl`, `CGUITable`, `CGUIToolBar`, `CGUIWindow` | 59 gui vtables | same filenames upstream | **near-match by name**; **absent upstream & present in engine**: `CGUITTFont.cpp`. **Present upstream & absent in engine**: `CGUIImageList.cpp`, `CGUITreeView.cpp` |
| `glitch::gui::ICursorControl` | 12 (`_ZTVN6glitch3gui14ICursorControlE`) | `include/ICursorControl.h` | diverged (moved into `glitch::gui`) |

### 2.4 `glitch::io`

| Engine symbol / class | Slots | Upstream Irrlicht (1.8) | Verdict |
| --- | --- | --- | --- |
| `glitch::io::IFileSystem` | **27** | `include/IFileSystem.h`, **32** virtuals | **diverged (−5)** |
| `glitch::io::CFileSystem` | **27** (`@0x974340`) | `source/Irrlicht/CFileSystem.cpp` | **diverged** — same slot count as its own interface |
| `glitch::io::IIrrXMLReader<wchar_t, glitch::IReferenceCounted>` / `<char, …>` | — | `include/irrXML.h` `IIrrXMLReader<char_type, IReferenceCounted>` | **near-match** — template signature preserved verbatim (allocation via `CXMLReaderImpl<char_type, IReferenceCounted>`) |
| `glitch::io::CXMLAttributesReader`, `CXMLAttributesWriter` | — | no counterpart | engine-only |
| `glitch::io::CPakReader`, `CZipReader`, `CLimitReadFile`, `CMemoryReadFile`, `CMemoryWriteFile`, `CReadFile`, `CWriteFile`, `CFileList`, `CXMLReader`, `CXMLWriter` | — | same filenames upstream | **near-match** |
| `CTarReader.cpp`, `CMountPointReader.cpp` | — | present upstream | **absent** in engine |

Engine `CFileSystem` vtable, verbatim order **[V]**:
`createAndOpenFile`, `createMemoryReadFile`, `createAndWriteFile`,
`addZipFileArchive`, `addFolderFileArchive`, `addPakFileArchive`,
`addObfuscationFileMap`, `removeFileArchive`, `getWorkingDirectory`,
`changeWorkingDirectoryTo`, `getAbsolutePath`, `getFileDir`, `getFileBasename`,
`createFileList`, `existFile`(char*), `existFile(string)`, `createXMLReader`×2,
`createXMLReaderUTF8`×2, `createXMLWriter`×2, `createEmptyAttributes`, `clear`.
Divergences: upstream has `createMemoryWriteFile`, `addFileArchive`, `getFileArchiveCount`,
`removeFileArchive(u32)`, `removeFileArchive(const IFileArchive*)`, `moveFileArchive`,
`getFileArchive`, `addArchiveLoader`, `getArchiveLoaderCount`, `getArchiveLoader`,
`flattenFilename`, `getRelativeFilename`, `createEmptyFileList`,
`setFileListSystem` — **all gone**; engine adds `addObfuscationFileMap(const char*, unsigned char, string)`
and a `std::basic_string`-typed `existFile` overload.

### 2.5 `glitch::core` and the root namespace

| Engine symbol / class | Upstream | Verdict |
| --- | --- | --- |
| `glitch::IReferenceCounted` (**root** namespace, 16+1 chars in mangling `_ZN6glitch17IReferenceCounted…`) | `include/IReferenceCounted.h`, `irr::IReferenceCounted` | **near-match** — vtable is `{~IReferenceCounted ×2, onDelete}` and `glitch::IReferenceCounted::onDelete()` is a real, external definition **[V]**. But upstream's `grab()`/`drop()`/`getReferenceCount()` are **not** used; the engine wraps everything in `boost::intrusive_ptr<T>` **[V]** |
| `glitch::IEventReceiver` | `include/IEventReceiver.h`, `irr::IEventReceiver::OnEvent(const SEvent&)` | **match** — engine vtable `_ZTV14IEventReceiver` @ `0x95c328` has exactly 3 slots: 2 destructors + `OnEvent` **[V]**. Note the *unqualified* symbol root `_ZTV14IEventReceiver` (no `glitch`), i.e. `IEventReceiver` sits at global scope |
| `glitch::IDevice`, `glitch::ITimer`, `glitch::IOSOperator`, `glitch::ILogger`, `glitch::CTimer`, `glitch::COSOperator`, `glitch::CLogger`, `glitch::CIrrFactory` | `include/IDevice.h`, `ITimer.h`, `IOSOperator.h`, `ILogger.h`, `CTimer.cpp`, `COSOperator.cpp`, `CLogger.cpp`, `CIrrFactory` | near-match by name, all hoisted to the `glitch::` root and `CAndroidOSDevice.cpp` replaces `CIrrDeviceAndroid/Stub/Linux/Win32/SDL/FB/Console/WinCE` |
| `glitch::core::SAllocator<T, glitch::memory::E_MEMORY_HINT>` | `irr::core::irrAllocator<T>` | **diverged** — a *hinted* allocator, threaded through the STL via `std::basic_string<char, std::char_traits<char>, glitch::core::SAllocator<char,(glitch::memory::E_MEMORY_HINT)0>>` **[V]** |
| `glitch::core::string<T, TAllocator>` | `include/irrString.h` | **replaced** — the engine's `string` is `std::basic_string` + `SAllocator`, **not** Irrlicht's copy-on-write `irr::core::string` **[V]** |
| `glitch::core::hashString`, `glitch::core::createQuantizedBuffer` | `include/irrString.h` `core::hash` | approximate |

### 2.6 `glitch::collada`, `glitch::ps`, and the rest of the layer **[V]**

Function/byte census parsed from `function-index.csv` (31,018 rows, 5,960,434
declared bytes — note this is `declared_size` summed over function rows, so it is
a text-size proxy, not a section size):

| Namespace | Functions | Bytes | Share | Vtables |
| --- | ---: | ---: | ---: | ---: |
| *(game/other)* | 17,084 | 3,327,834 | 55.83% | — |
| `glitch::` root & other | 4,874 | 576,044 | 9.66% | 9 |
| `gameswf` (third-party SWF) | 2,624 | 609,332 | 10.22% | — |
| **`glitch::video`** | **1,203** | **320,060** | **5.37%** | **66** |
| `glitch::collada` | 1,159 | 227,780 | 3.82% | 105 |
| `vox` (LucasArts iMUSE) | 1,092 | 168,828 | 2.83% | — |
| **`glitch::scene`** | **942** | **281,296** | **4.72%** | **94** |
| **`glitch::io`** | **747** | **115,744** | **1.94%** | **61** |
| **`glitch::gui`** | **604** | **191,704** | **3.22%** | **59** |
| `glitch::ps` (particles) | 351 | 72,724 | 1.22% | 44 |
| **`glitch::core`** | **245** | **52,896** | **0.89%** | — |
| `glitch::debugger` / `res` / `os` | 59 | 11,352 | 0.19% | 1 |
| **All `glitch::*`** | **10,198** | **1,852,468** | **31.08%** | — |

The `video+scene+gui+io+core` subset alone is **3,741 functions / 961,700 bytes /
16.14%** — within 1 function and 8 bytes of the figures already recorded for the
Irrlicht-lineage layer, so the earlier estimate is independently confirmed
**[V]**. Note the whole `glitch::` namespace is twice that size (`collada`,
`ps`, plus game code also living under `glitch`).

Binary-wide inventory: **1,628 vtables, 32,574 symbol-table lines, 31,018
function rows, 867 build-source filenames, 94,959 strings**; `.comment` reports
**`GCC: (GNU) 4.4.3`** (repeated per translation unit) **[V]**.

---

## 3. Where upstream source can replace or inform the engine — and where it cannot

### 3.1 Cannot replace (ABI-hard blockers) **[V]**

1. **Every interface vtable has a different size and a different slot order.**
   `IVideoDriver` 134 vs 115, `ISceneNode` 73 vs ~37, `IGUIEnvironment` 66 vs 60,
   `IFileSystem` 27 vs 32, `IMesh` 12 vs 10, `ITexture` 9 vs 8. There is no
   version of upstream whose virtual table matches; a vtable is an ABI contract,
   so mixing object files across the two would mis-dispatch silently.
2. **The renderer abstraction was inverted.** Upstream `IVideoDriver` is *the*
   drawing API (`draw2DImage`, `drawMeshBuffer`, `drawVertexPrimitiveList`,
   `setMaterial`, `setTransform`). The engine's `IVideoDriver` has none of those;
   geometry arrives as `CVertexStreams` + `CPrimitiveStream` + `CDriverBinding`
   via `draw(...)`, and all state goes through the §2.1 getter/setter block. Any
   upstream driver code (even a pure-math one) references upstream call shapes.
3. **The mesh model is different.** `IMeshBuffer` / `SMeshBuffer<video::S3DVertex>`
   / `SMeshBufferLightMap` do not exist. The engine keys meshes by
   `CMesh::getMaterial(u32)` + `getMaterialVertexAttributeMap(u32)` and by
   `glitch::video::E_VERTEX_ATTRIBUTE`-driven `CVertexStreams`. Upstream
   `S3DVertex`'s fixed `Pos/Normal/Color/TCoords` layout does not appear anywhere.
4. **No `ISceneManager`.** Upstream's mesh/scene code is written against
   `irr::scene::ISceneManager`; the engine has only `CSceneManager`. Upstream
   files that take an `ISceneManager*` cannot even be compiled against the
   engine's headers without an adapter.
5. **`irr::core::string` ≠ the engine's string.** The engine aliases
   `std::basic_string<char, std::char_traits<char>, glitch::core::SAllocator<char,(E_MEMORY_HINT)0>>`
   **[V]** where upstream uses a COW `core::string` with `c_str()` conventions.
   Every upstream signature mentioning `io::path` (which is
   `core::string<c8>` upstream) differs.
6. **Ownership model differs.** Upstream `IReferenceCounted::grab()/drop()`;
   engine `boost::intrusive_ptr<T>` **[V]**. Upstream singletons
   (`createDevice`, `IVideoDriver` owned by `CIrrDeviceStub`, `CFileSystem`
   created once) have no counterpart: the engine's `Application` class owns an
   `IDevice`, and `Application::RegisterForIrrlichtEvents` /
   `UnRegisterForIrrlichtEvents` **[V]** show the event wiring is
   `Application`-centric, not receiver-owned.

### 3.2 The specific seams the task asked about — corrected

* **"`IVideoDriver` is explicitly designed as a swappable backend" — true
  upstream, and the engine used exactly that seam.** Upstream ships
  `CNullDriver`, `CSoftwareDriver`, `CSoftwareDriver2`, `CD3D8Driver`,
  `CD3D9Driver`, `COpenGLDriver`, `COGLESDriver`, `COGLES2Driver` behind one
  `IVideoDriver`, selected by `E_DRIVER_TYPE`. The engine exploited this:
  `CCommonGLDriverBase` (134 slots) → `CCommonGLDriver<…>` (135) →
  `CProgrammableGLDriver<CGLSLShaderHandler>` (135) → `COpenGLES2Driver` (135),
  plus sibling `CNullDriver` (138), `CBatchDriver` (138), `C2DDriver` (3)
  **[V]**. **But the seam itself was redefined by the fork**, so the swappability
  is internal to the engine, not shared with upstream.
* **"mesh buffers are `SMeshBuffer`/`SMeshBufferLightMap`" — no longer true in
  this fork.** Both are **absent** (0 symbol hits) **[V]**; `CMeshBuffer` is a
  bespoke non-template container.
* **"there is a software-reference path in `CSoftwareDriver`/`CTR*` texture and
  vertex pipelines that may be a directly reusable, already-optimised reference
  implementation" — this path is entirely absent from the engine.** Exhaustive
  scan of the 32,574-line symbol dump returns **0** hits for `CSoftwareDriver`,
  `CSoftwareDriver2`, `CSoftwareTexture`, `CSoftwareTexture2`, `CTRTexture*`,
  `CTRGouraud*`, `CTRFlat*`, `CZBuffer`, `CDepthBuffer`, `IBurningShader`,
  `CBurningShader_Raster_Reference` **[V]**; correspondingly none of the ~40
  `CTR*.cpp` / `CBurningShader*.cpp` filenames appear among the 867 build
  sources **[V]**. The engine never links the Burning/software rasteriser.
  **Therefore it cannot be "directly reused" in-place** — but as *upstream
  source* it remains exactly what §3.3 describes.

### 3.3 Where upstream is genuinely useful

**A. As a pinned algorithmic reference for the math the port wants to
accelerate.** The software path is the single best-documented, license-clean
statement of Irrlicht's matrix/transform/lighting math:

| Upstream file (1.8) | What it gives you |
| --- | --- |
| `source/Irrlicht/CSoftwareDriver2.cpp`, `CTRTextureGouraud2.cpp`, `CTRGouraud2.cpp`, `CTRTextureLightMap2_M*.cpp`, `CTRNormalMap.cpp`, `CTRStencilShadow.cpp` | perspective-correct rasterisation with full transform/lighting: the reference for what "correct" means |
| `source/Irrlicht/CSoftwareDriver.cpp`, `CTRTextureGouraud.cpp`, `CTRTextureFlat.cpp`, `CTRFlat.cpp` | the simpler path, easier to read |
| `include/matrix4.h` (`matrix4`/`CMatrix4<f32>`) | buildTextureTransform, buildProjectionMatrixOrthoLH/PerspectiveFovLH, getInverse, transformVect/Plane/Box — same semantics the engine's `glitch::core::CMatrix4<float>` must preserve |
| `include/coreutil.h` | `core::fast_atof`, `core::round`, `core::clamp`, `core::isPointInRect`, `core::getVectorAngle` |
| `source/Irrlicht/CMeshManipulator.cpp` | `recalculateNormals`, `createMeshWithTangents`, `transform`, `scale`, `flipSurfaces`, `createCompactMesh` — the engine's `glitch::scene::recalculateNormals`/`transform`/`scale`/`CMeshConnectivity` are clearly descendants |
| `source/Irrlicht/COctreeSceneNode.cpp`, `CMeshSceneNode.cpp`, `CSceneCollisionManager.cpp`, `CTriangleSelector.cpp`, `COctreeTriangleSelector.cpp` | broad-phase/render-order patterns |
| `source/Irrlicht/CVertexBuffer.cpp`, `CIndexBuffer.cpp`, `include/CVertexBuffer.h`, `CMeshBuffer.h` | the *original* vertex/index-buffer abstraction, useful as a contrast to `CVertexStreams` |
| `source/Irrlicht/COpenGLCacheHandler.cpp`, `COpenGLCoreCacheHandler.h` (ogl-es), `COGLES2MaterialRenderer.h` | the upstream attempt at exactly the state-caching problem the engine solves with `SRenderState`/`getRenderState`/`setRenderState`/`commitCurrentMaterial` |
| `media/Shaders/COGLES2*.vsh/.fsh` (ogl-es) | shader sources for each `E_MATERIAL_TYPE` — directly comparable against the engine's embedded GLSL |

**B. As a *skeleton and semantics* donor for the parts that kept upstream
filenames.** 88 engine build filenames exist identically in the 1.8 tree
(§1.3), covering `CGUI*` (24 files), `CParticle*` (11), `CSceneNodeAnimator*` (9),
`CImageLoader*`/`CImageWriter*` (8), `CSceneManager`, `CMeshSceneNode`,
`CTerrainSceneNode`, `CShadowVolumeSceneNode`, `CSkyBoxSceneNode`,
`CTextSceneNode`, `CTriangleSelector`, `CFileSystem`, `CXMLReader/Writer`,
`CZipReader`, `CPakReader`, `CVideoModeList`, `COSOperator`, `CLogger`,
`CAttributes`, `CImage`. For these, upstream 1.8 is a faithful **algorithmic
spec** for a reimplementation, and often a whitespace-level diff target.

**C. As a vocabulary source for reverse engineering.** Because the rename is
mechanical (§1.1), 88 matching filenames + ~150 matching class names let you name
unknown `glitch::` functions with high confidence, which is what makes the
mapping table in §2 possible at all.

**D. As directly-vendorable, already-optimised C code.**
`source/Irrlicht/{zlib,libpng,jpeglib,bzip2}/**` and `aesGladman/**` are
self-contained C with no Irrlicht types, and the engine **already contains**
zlib 1.2.3 + libpng 1.2.32 (strings `inflate 1.2.3 Copyright 1995-2005 Mark
Adler`, `deflate 1.2.3 Copyright … Jean-loup Gailly`, `libpng version 1.2.32 -
September 18, 2008`) **[V]**. Upstream is useful here only to *upgrade* those
versions, not to add them.

---

## 4. Version-locked upstream file inventory (what is worth pulling)

All paths are relative to an upstream Irrlicht tree root (SVN layout
`<tag>/include/…`, `<tag>/source/Irrlicht/…`). "Tag" = the ref in the
`svn2github/irrlicht` mirror from which the file content was verified.

### 4.1 Licence

**The whole Irrlicht library is zlib — confirmed [V].** The canonical text lives at

* `tags/release-1.8/doc/irrlicht-license.txt` — verbatim zlib:
  > `Copyright (C) 2002-2012 Nikolaus Gebhardt`
  > `This software is provided 'as-is', without any express or implied warranty. …`
  > `Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions: 1. The origin of this software must not be misrepresented … 2. Altered source versions must be plainly marked as such … 3. This notice may not be removed or altered from any source distribution.`

* and identically in every source header, e.g. `tags/release-1.8/include/irrlicht.h` lines 1–25
  (`irrlicht.h -- interface of the 'Irrlicht Engine' / Copyright (C) 2002-2012 Nikolaus Gebhardt / This software is provided 'as-is' …`).

Other licences in the same tree are for the *bundled* decoders, not for
Irrlicht: `doc/jpglib-license.txt` (IJG), `doc/libpng-license.txt` (libpng),
`doc/bzip2-license.txt` (BSD-4-clause-like), plus `source/Irrlicht/libpng/LICENSE`
and `source/Irrlicht/bzip2/LICENSE`.

**Practical consequence for DH2Work [V for licence, I for action].** Irrlicht is
**not** copyleft: no source-disclosure obligation attaches to the game. The zlib
terms only require (a) not claiming authorship, (b) marking altered versions, and
(c) retaining the notice. Today [`notices/NOTICES.md`](../notices/NOTICES.md) and
[`notices/UPSTREAM-LICENSE.txt`](../notices/UPSTREAM-LICENSE.txt) record
ZettaBridge, Dynarmic and Khronos but **do not mention Irrlicht at all** — that
is a gap worth closing once any upstream Irrlicht file is actually vendored, and
`doc/irrlicht-license.txt` should be copied verbatim alongside it.

### 4.2 Files worth pulling

**Tier 1 — interfaces / ABI ground truth** (any of `tags/release-1.7`, `1.7.1`,
`1.7.2`, `1.8`; use `1.8` to match the engine generation)

| Upstream path | Why |
| --- | --- |
`include/IVideoDriver.h`, `include/ITexture.h`, `include/IImage.h`, `include/IImageLoader.h`, `include/IImageWriter.h`, `include/IVideoModeList.h`, `include/IRenderTarget.h`, `include/IMaterialRenderer.h`, `include/IMaterialRendererServices.h`, `include/IGPUProgrammingServices.h` | the documented *intended* semantics of the renderer the engine rewrote; needed to prove each divergence
`include/ISceneNode.h`, `include/ISceneManager.h`, `include/IMeshSceneNode.h`, `include/IAnimatedMeshSceneNode.h`, `include/ICameraSceneNode.h`, `include/ILightSceneNode.h`, `include/IBillboardSceneNode.h`, `include/IParticleSystemSceneNode.h`, `include/ISceneNodeAnimator.h`, `include/ITriangleSelector.h`, `include/ISceneCollisionManager.h`, `include/IMeshManipulator.h` | scene-layer contract
`include/IMesh.h`, `include/IMeshBuffer.h`, `include/SMeshBuffer.h`, `include/SMeshBufferLightMap.h`, `include/CMeshBuffer.h`, `include/CDynamicMeshBuffer.h`, `include/IDynamicMeshBuffer.h`, `include/CVertexBuffer.h`, `include/CIndexBuffer.h`, `include/S3DVertex.h`, `include/SMaterial.h`, `include/EVertexAttributes.h` | the mesh/vertex model that was replaced; `S3DVertex.h` + `EVertexAttributes.h` are the key contrast
`include/IGUIEnvironment.h`, `include/IGUIElement.h`, `include/IGUISkin.h`, `include/IGUIFont.h`, `include/IGUIFontBitmap.h`, `include/IGUIElementFactory.h`, `include/IGUIImageList.h` | GUI contract
`include/IFileSystem.h`, `include/IFileArchive.h`, `include/IFileList.h`, `include/IReadFile.h`, `include/IWriteFile.h`, `include/IXMLReader.h`, `include/IXMLWriter.h`, `include/irrXML.h`, `include/IrrXML.h` | IO contract; `irrXML.h` is the direct ancestor of `glitch::io::IIrrXMLReader`
`include/IReferenceCounted.h`, `include/IAttributes.h`, `include/IAttributeExchangingObject.h`, `include/IEventReceiver.h`, `include/IDevice.h`, `include/ITimer.h`, `include/ILogger.h`, `include/IOSOperator.h`, `include/ICursorControl.h`, `include/IContextManager.h`, `include/EDriverTypes.h`, `include/EMaterialTypes.h`, `include/EDriverFeatures.h` | core/root contracts and enum values
`include/rect.h`, `include/vector2d.h`, `include/vector3d.h`, `include/matrix4.h`, `include/quaternion.h`, `include/aabbox3d.h`, `include/dimension2d.h`, `include/plane3d.h`, `include/triangle3d.h`, `include/line3d.h`, `include/coreutil.h`, `include/irrString.h`, `include/irrArray.h`, `include/irrMap.h`, `include/irrList.h`, `include/heap.h`, `include/irrMath.h`, `include/irrTypes.h`, `include/irrlicht.h`, `include/IrrCompileConfig.h` | template math + containers; `matrix4.h`/`coreutil.h`/`irrMath.h` are the highest-value math donors

**Tier 2 — reusable algorithm reference**

| Upstream path | Why |
| --- | --- |
`source/Irrlicht/CSoftwareDriver2.cpp` `CTRTextureGouraud2.cpp` `CTRGouraud2.cpp` `CTRTextureGouraudAdd2.cpp` `CTRTextureGouraudAlpha2.cpp` `CTRTextureGouraudVertexAlpha2.cpp` `CTRTextureLightMap2_*.cpp` `CTRTextureDetailMap2.cpp` `CTRNormalMap.cpp` `CTRStencilShadow.cpp` `CTRTextureWire2.cpp` `CZBuffer.cpp` `CDepthBuffer.cpp` `IBurningShader.cpp` `CBurningShader_Raster_Reference.cpp` | **the already-optimised software reference path** for matrix/transform/lighting/rasterisation; the cleanest statement of intended output
`source/Irrlicht/CSoftwareDriver.cpp` `CTRFlat.cpp` `CTRFlatWire.cpp` `CTRGouraud.cpp` `CTRGouraudWire.cpp` `CTRTextureFlat.cpp` `CTRTextureFlatWire.cpp` `CTRTextureGouraud.cpp` `CTRTextureGouraudAdd.cpp` `CTRTextureGouraudAlpha.cpp` `CTRTextureGouraudAlphaNoZ.cpp` `CTRTextureGouraudNoZ.cpp` `CTRTextureGouraudWire.cpp` `CTRTextureLightMap2_*.cpp` `CSoftwareTexture.cpp` `CSoftwareTexture2.cpp` | the simpler software path (easier to port)
`source/Irrlicht/CMeshManipulator.cpp` `CMesh.cpp` `CMeshBuffer.cpp` `CVertexBuffer.cpp` `CIndexBuffer.cpp` `CMeshSceneNode.cpp` `CAnimatedMeshSceneNode.cpp` `CSkinnedMesh.cpp` `CSceneCollisionManager.cpp` `CTriangleSelector.cpp` `COctreeTriangleSelector.cpp` `CMetaTriangleSelector.cpp` `CTerrainTriangleSelector.cpp` `COctreeSceneNode.cpp` `CSceneManager.cpp` | mesh/scene algorithms; direct analogues of engine behaviour
`source/Irrlicht/CNullDriver.cpp` `CTextureManager.cpp` `COpenGLCacheHandler.cpp` `COpenGLCoreCacheHandler.h` `COpenGLDriver.cpp` `COpenGLExtensionHandler.cpp` `COpenGLTexture.cpp` `COpenGLMaterialRenderer.h` `COpenGLSLMaterialRenderer.cpp` `COpenGLShaderMaterialRenderer.cpp` `COpenGLNormalMapRenderer.cpp` `COpenGLParallaxMapRenderer.cpp` | driver mechanics and the upstream state-cache design
`source/Irrlicht/CGUIEnvironment.cpp` `CGUISkin.cpp` `CGUIFont.cpp` `CGUIButton.cpp` `CGUIEditBox.cpp` `CGUIListBox.cpp` `CGUIStaticText.cpp` `CGUIImage.cpp` `CGUIWindow.cpp` `CGUISpriteBank.cpp` `CGUIElementFactory*` | GUI semantics for the 24 near-match `CGUI*` files
`source/Irrlicht/CFileSystem.cpp` `CXMLReader.cpp` `CXMLWriter.cpp` `irrXML.cpp` `CZipReader.cpp` `CPakReader.cpp` `CLimitReadFile.cpp` `CMemoryReadFile.cpp` `CMemoryWriteFile.cpp` `CReadFile.cpp` `CWriteFile.cpp` `CFileList.cpp` `CXMLAttributesReader.cpp` `CXMLAttributesWriter.cpp` | IO semantics
`source/Irrlicht/CImageLoader{BMP,DDS,JPG,PNG,PVR,TGA}.cpp` `CImageWriter{JPG,PNG,TGA}.cpp` `CImage.cpp` `CColorConverter.cpp` | codec semantics for the exact loader set the engine kept
`source/Irrlicht/CParticle*.cpp` `CSceneNodeAnimator*.cpp` `CBillboardSceneNode.cpp` `CSkyBoxSceneNode.cpp` `CShadowVolumeSceneNode.cpp` `CTextSceneNode.cpp` `CTerrainSceneNode.cpp` `CDummyTransformationSceneNode.cpp` `CEmptySceneNode.cpp` `CLightSceneNode.cpp` `CCameraSceneNode.cpp` `CSceneManagerRootNode`-analogues | near-match node/animator behaviour
`media/Shaders/COGLES2*.vsh` `media/Shaders/COGLES2*.fsh` (from `branches/ogl-es`) | per-material GLSL, directly comparable to the engine's shaders

**Tier 3 — vendorable third-party (already inside the engine)** **[V]**

`source/Irrlicht/zlib/**` (1.2.3), `source/Irrlicht/libpng/**` (1.2.32),
`source/Irrlicht/jpeglib/**`, `source/Irrlicht/bzip2/**`, `source/Irrlicht/aesGladman/**`
— self-contained C, no Irrlicht types. Licence: zlib (bundled copy), libpng,
IJG, bzip2, aesGladman respectively.

---

## 5. Honest assessment of an upstream-grafting strategy

**Bottom line.** Upstream Irrlicht can be grafted into DH2Work as **documentation,
algorithmic reference and license-clean codec source**, but **not** as a drop-in
replacement for any `glitch::video` / `glitch::scene` / `glitch::gui` /
`glitch::io` component. The realistic upside is bounded: the engine already owns
the parts that matter for performance (the state-batching GLES2 driver), and the
parts upstream could inform are the parts the engine *did not fork* (small
`CGUI*`, `CParticle*`, `CSceneNodeAnimator*`, image codecs).

### 5.1 Why replacement fails **[V]**

* **Vtable divergence is universal and silent.** 134 vs 115 (`IVideoDriver`),
  73 vs ~37 (`ISceneNode`), 66 vs 60 (`IGUIEnvironment`), 27 vs 32
  (`IFileSystem`), and different slot order in every case. There is no upstream
  version whose virtual table matches, so object files from the two lineages can
  never be linked or substituted interchangeably.
* **96 of 134 `IVideoDriver` slots are `__cxa_pure_virtual` stubs.** The
  interface is a *shim*: it declares upstream-shaped virtuals that nothing calls,
  while the real API is `draw(CVertexStreams, CPrimitiveStream, CDriverBinding**,
  CMeshBuffer)` + `getRenderState/setRenderState`. Upstream driver code calls the
  shim slots — which are guaranteed to abort.
* **Missing load-bearing abstractions.** No `ISceneManager`, no `IMeshBuffer`, no
  `SMeshBuffer`/`SMeshBufferLightMap`, no `S3DVertex`/`E_VERTEX_TYPE`, no
  `IMaterialRenderer`, no `SMaterial`. Upstream scene/mesh/particle code will not
  even compile against the engine's headers without substantial adapters.

### 5.2 The named risks, with the evidence that bears on each

| Risk | Assessment |
| --- | --- |
| **Name / ABI mangling: `glitch::` vs `irr::`** | **Real but mechanical, and *fatal to linking* by itself [V].** Namespaces change every mangled symbol (`_ZN6glitch5video…` vs `_ZN3irr5video…`). Even a perfect source port yields *different* symbols, so mixed linking cannot reuse upstream `.o`s. A `namespace irr` → `namespace glitch` alias would fix names but not the vtable differences, which are the actual blocker. |
| **STLport vs libstdc++** | **Confirmed as a real architectural wedge [V].** The engine has **no** Irrlicht containers in signatures — it uses `std::basic_string<char, std::char_traits<char>, glitch::core::SAllocator<char,(glitch::memory::E_MEMORY_HINT)0>>`, `std::vector<T, SAllocator<T,…>>`, `std::map<…, SStringLess, SAllocator<…>>`, `std::priv::` internals (STLport's namespace) *and* `std::__false_type`/`std::random_access_iterator_tag` (libstdc++ names) side by side. Upstream 1.8 uses `irr::core::string`, `irr::core::array`, `irr::core::map`. **Any upstream source touching strings/containers must be re-typed**, and the STLport↔libstdc++ mix already present in the binary means container ABI is not something to reason about casually. |
| **`-fno-rtti` / `-fno-exceptions`** | **Partially verified, and not a blocker either way.** `typeinfo.json` in the inventory is **3 bytes** — effectively empty **[V]** — i.e. no RTTI-heavy dispatch survives, though I did not disassemble for `__cxa_throw`/`dynamic_cast` targets, so treat the exact flag pair as **[U]**. Note the engine *does* reference `__cxa_pure_virtual` (many times) and `Application`-level events, so exceptions/RTTI were not blindly stripped from the toolchain. This matters less than the vtable issue: matching flags is necessary but not sufficient for grafting. |
| **Singleton ownership** | **Real [V].** Upstream builds the world in `createDevice()` → `CIrrDeviceStub` owns `IVideoDriver`, `ISceneManager`, `IGUIEnvironment`, `IFileSystem`; `IReferenceCounted::grab()/drop()` manage lifetime. The engine instead has `glitch::Application` (with `RegisterForIrrlichtEvents`/`UnRegisterForIrrlichtEvents`), `glitch::IDevice`/`CAndroidOSDevice`, `Application::InitWin32(IDevice*)`, and wraps everything in **`boost::intrusive_ptr`** rather than `grab/drop`. Grafted upstream code that expects to create or own a device/driver will fight the engine's ownership graph. |
| **Third-party sprawl inside the "engine"** | **Under-appreciated risk [V].** The `glitch::` layer is interleaved with `gameswf` (2,624 funcs / 609 kB), LucasArts `vox` iMUSE (1,092 / 169 kB), a 105-vtable in-house Collada pipeline, zlib/libpng/libjpeg, and `tinyxml`/`SlimXml`/`stb_vorbis`. Upstream Irrlicht's own dependencies (`irrXML.cpp`, `aesGladman`, `bzip2`) partly **collide** with these. Any vendoring must be checked against duplicates, not just against the ABI. |

### 5.3 What I would actually recommend

1. **Treat upstream 1.8 as a pinned reference checkout, not a source of objects**
   (`DH2Work-toolchain\irrlicht\svn2github-irrlicht.git` at `tags/release-1.8`,
   plus `zaki/irrlicht@ogl-es` for shaders and the GLES2 driver design). Record
   the exact refs and this document's §0.2 acquisition caveats.
2. **Do the performance port against the engine's own seam**
   (`getRenderState`/`setRenderState`/`SRenderState`/`commitCurrentMaterial`/
   `CPrimitiveStream`/`CVertexStreams`/`CDriverBinding`), using
   `CTR*`/`CSoftwareDriver2` and `matrix4.h`/`coreutil.h` only to **validate
   semantics and numeric behaviour**. That is where a real speed-up can come from;
   upstream has no faster implementation of the engine's own batching design.
3. **Vendor upstream only where it is genuinely self-contained**: the Tier-3
   codec trees, and — if the small `CGUI*`/`CParticle*`/`CSceneNodeAnimator*`
   files are ever re-derived — as a diff target with the `glitch::`/container/
   `intrusive_ptr` adaptations applied up front.
4. **Close the licence-notice gap** (§4.1): add Irrlicht (zlib) plus its
   `doc/irrlicht-license.txt` to `notices/` before any upstream file is committed.

### 5.4 Confidence summary

| Claim | Confidence |
| --- | --- |
| `glitch::` is a rename of `irr::` within an Irrlicht fork | **very high** (~0.97) |
| Fork is the **1.8** generation, not 1.7 | **high** (~0.85) |
| Specific point release (best candidate **1.8.0**) | **medium** (~0.5); 1.8.1–1.8.3 equally consistent, **1.8.5 less likely** but unverified |
| GLES2 driver / renderer is **not** inherited from upstream 1.8 | **very high** (~0.95) |
| No upstream C++ source is ABI-compatible with the engine's `glitch::` layer | **very high** (~0.95) |
| Absence of `SMeshBuffer`, `IMeshBuffer`, `S3DVertex`, `ISceneManager`, `CSoftwareDriver`, `CTR*` in the engine | **certain** (exhaustive symbol scans, 0 hits) |
| Irrlicht library licence is zlib | **certain** (`tags/release-1.8/doc/irrlicht-license.txt` + every header) |

### 5.5 Open items **[U]**

* Obtain a verified 1.8.5 (or 1.8.1/1.8.2/1.8.3) source tree to pin the point
  release precisely; SourceForge was unreachable and no GitHub mirror exposing
  those tags was found in this session. The SVN mirror stops in 2015 and exposes
  only `tags/release-1.8`.
* Confirm the exact build flags (`-fno-rtti`, `-fno-exceptions`, `-fno-threadsafe-statics`)
  from the 32 DWARF compilation units (`licensing/online glue, STLport, libgcc` —
  no gameplay, so DWARF cannot help with the engine itself).
* Confirm whether `include/IBuffer.h` exists in upstream 1.8 under another name
  (`CIndexBuffer.h`/`CVertexBuffer.h` are the 1.8 candidates); the engine's
  `glitch::video::IBuffer` (9 slots) was not matched to an upstream header.
* Establish which party added `CImageLoaderPVR.cpp`/`CImageLoaderATC.cpp`
  (Gameloft vs a 1.8.x upstream Android port) — it is the strongest remaining
  lever for narrowing the point release.
