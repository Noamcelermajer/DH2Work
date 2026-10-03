# HOST-FIT-NATIVE — Native is the default, and the real cause of the Xiaomi bar

Private compatibility work on the Dungeon Hunter 2 port.  Everything below was
produced off-device (Windows host, **no phone and no emulator**).  Section 7
states plainly what is still unverified.

This revision exists because the previous one (**1110**, `auto` default,
16:9 target) **regressed a device that worked**: verified on hardware by the
user with a screenshot, `auto` letterboxed the 1.11:1 Fold7 to 16:9 and added
bars the older build never had.  That is fixed here by making "apply nothing"
the default and by removing the 16:9 target from `auto`.

Artifact: `Dungeon-Hunter-2-native-fit.apk`, version code **1111**, version name
`1.0-fit-native`, package `local.dh2.fold7`,
sha256 `3ab74df9e529deeb8909b4cc3b3a5f63a18f45d8701a67a0fbc6dbdce20de1d1`,
13,666,842 bytes.

---

## 1. Requirement 1 — Native mode, and it is the default

`native` is now mode index 0 and is what an absent, unknown or malformed option
resolves to.  In Native mode `GameTrace`:

- calls **no** `glViewport` at all (the call site is guarded by
  `!layout.isIdentity()`, and a Native layout is the identity);
- never overrides the size the guest's own `DisplayMetrics` reading produced
  **except** for the orientation repair in section 3, which is the identity on
  any display that already reports landscape metrics;
- does **not** wrap the `GameGLSurfaceView` in a frame (`viewReplaced=false`,
  `setContentView(view)` exactly as the unpatched guest did);
- leaves `touchScaleX/Y = 1, touchOffsetX/Y = 0`, so the touch listener applies
  neither `offsetLocation` nor `Matrix.setScale`, and the engine's own
  `onTouchEvent` receives the event unchanged.

`FitGeometry.Layout.isIdentity()` is the single predicate that defines the
guarantee: view box == logical size == viewport == the whole window at the
origin, with an identity touch mapping.  `GameTrace` consults it before every
side effect, and the harness asserts it independently.

### 1.1 The orientation repair is not a fit, and is conditional

One thing is still applied in Native mode, deliberately, because it is a
correctness fix rather than a fitting choice: **if and only if** the
`DisplayMetrics` reading is portrait (`widthPixels < heightPixels`), the two
numbers handed to the first `nativeSetPhone` are swapped into landscape order
(`FitGeometry.oriented`, `needsOrientationRepair`).  On the Fold7 the reading is
already 2184x1968, the repair is the identity, and the engine receives exactly
the numbers the unpatched guest passed it.  Section 3 explains why this is the
whole Xiaomi fix.

## 2. Requirement 2 — `auto` no longer imposes 16:9

`auto` and `native` now both return `FitGeometry.identity(...)`.  They are
**behaviourally identical**, and that is stated here rather than hidden.  The two
names are kept because the on-disk option value and the radio-button label are
already shipped, and because a future `auto` that does something *measured* can
be added without invalidating a saved choice.

The bar-adding modes are de-emphasised, not deleted: their labels now say what
they do ("Letterbox - fixed 16:9, centred bars (adds bars)", "Stretch - fill the
window, distorted"), and the panel help text says Native is the default so a
display that already works is never modified.  An install upgrading from 1110
resolves to `native` unconditionally, so no working device can begin fitting
because of this upgrade.

## 3. What actually causes the Xiaomi bar — and it is not our fitting

The addendum's framing is confirmed and the mechanism is now pinned to specific
instructions in the shipped engine.

### 3.1 The engine's viewport is two globals written by `nativeSetPhone`

`lib/armeabi-v7a/libDungeonHunter2.so`, sha256
`84c880553295d111828400f0b6ed8ca6b238854489346d4b5a07a17996c78bd5`, 15,938,284
bytes, ELF32 ARM (`llvm-readobj --file-headers` → `Format: elf32-littlearm`,
`Machine: EM_ARM (0x28)`):

```
00532e78 <Java_com_gameloft_android_GAND_GloftD2SS_DungeonHunter2_nativeSetPhone>:
  532e78:  ldr  r1, [pc, #0x1c]
  532e7c:  ldr  r0, [pc, #0x1c]
  532e80:  add  r1, pc, r1
  532e84:  ldr  r12, [r1, r0]
  532e88:  ldr  r0, [pc, #0x14]
  532e8c:  str  r2, [r12]
  532e90:  ldr  r0, [r1, r0]
  532e94:  str  r3, [r0]
  532e98:  bx   lr
```

Nine instructions, no call: `nativeSetPhone(int w, int h)` does nothing but
store its two arguments into two globals.  Resolving the two GOT slots through
`llvm-readobj --relocations` and `llvm-nm -D` names them:

```
0099b100 D isScreenOriented
0099b114 D Width_Screen
0099b118 D Height_Screen
```

and the driver turns them into the viewport:

```
005b1b10 <...CCommonGLDriver<...>::ReinitDriverEv>:
  5b1b48:  ldr  r2, [r7, #0x60]        <- the width field driverInit filled
  5b1b4c:  ldr  r3, [r7, #0x64]        <- the height field
  5b1b50:  mov  r0, r4                 (r4 == 0)
  5b1b54:  mov  r1, r4                 (r4 == 0)
  5b1b58:  bl   0x30df74 <glViewport@plt>
```

### 3.2 The engine does **not** re-issue `glViewport` every frame

This was one of the previous workstream's unverified items, and it changes the
approach.  `grep` over the full disassembly finds **exactly two** `glViewport`
call sites in the entire binary:

```
  5b0b74:  bl  0x30df74 <glViewport@plt>     inside setViewportImpl
  5b1b58:  bl  0x30df74 <glViewport@plt>     inside ReinitDriver
```

Neither is inside a draw loop, so a correction is **not** overwritten each frame.
`setViewportImpl` is additionally redundant-state-filtered: it compares the
incoming rectangle (object fields `+0x240,+0x244,+0x248,+0x24c`) with its cached
copy and returns without calling GL when they match.  The engine's own
`glGetIntegerv` calls are for `GL_MAX_TEXTURE_SIZE` (0x8869) and extension
detection, **not** `GL_VIEWPORT`; the host, however, would support the query
(`gl_manual.cpp:365` declares `case 0x0BA2: // GL_VIEWPORT` as four elements).
This revision does not use it: the previous `auto` read the engine's rectangle
back and re-centred it, and that is precisely the mode that added bars.

### 3.3 Therefore the cause is the value of the first `nativeSetPhone`

The engine lays out for, and sets its viewport from, whatever it was first
handed — before the `GameGLSurfaceView` exists
(`generated-game-smali/.../DungeonHunter2.smali:1660-1688`).  The Xiaomi's own
diagnostics report our rectangle as identity, the surface as full-window, and
the engine drawing **1280 px wide in a 2381 px surface (54%)**, where 1280 is
the portrait width.  That is the signature of un-oriented metrics at that first
call, and it is upstream of every fit mode, which is exactly why no mode filled
the screen.

## 4. What was changed

Base: private copies of `DH2Work-toolchain\host\fit\` and its
`host\shrink\ZettaBridge` fork.  Nothing shared was written to.

1. **`guest-java/local/dh2/compat/FitGeometry.java`** — `NATIVE` added as mode 0;
   `AUTO` reduced to the identity; `Layout.isIdentity()` added as the guarantee
   predicate; `oriented`/`needsOrientationRepair` kept and documented;
   `engineViewportRect` added as the explicit model of section 3.1, with the
   disassembly addresses in its javadoc; `gcd`/`aspectOf` added.
2. **`guest-java/local/dh2/compat/FitOptions.java`** — `DEFAULT_MODE` is
   `native`; the legacy `fit16by9` fallback resolves to `native` (not `auto`).
3. **`guest-java/local/dh2/compat/GameTrace.java`** — Native short-circuits
   `onSurfaceSize`, `applyViewport`, `installContent` and the touch mapping;
   `glViewport` is additionally guarded by `!g.isIdentity()`; the read-back path
   (`readEngineViewport`) is **deleted**; `syncPhoneSize()` added; `applyPhone`
   logs the fact that it is writing the engine's `Width_Screen`/`Height_Screen`.
4. **`java/com/zettabridge/launcher/Dh2Options.java`** — `NATIVE` first in
   `MODES`/`LABELS`, `normaliseMode` falls back to `native`.
5. **`java/com/zettabridge/launcher/Dh2Activity.java`** — migration always
   writes `native`; new help text.
6. **Guest smali** (patched by `tools/patch_guest_native.py`, from the reviewed
   1110 decode): `onCreate` keeps the existing `setLogicalSize`→
   `nativeSetPhone(logicalWidth(), logicalHeight())` shape — that shape is what
   this revision wants, so it was accepted rather than rewritten — and
   `GameRenderer.onSurfaceCreated` gains one new hook:

   ```
   invoke-static {v1}, .../GameRenderer;->nativeInit(I)V
   invoke-static {}, Llocal/dh2/compat/GameTrace;->syncPhoneSize()V
   ```

   `onSurfaceChanged` is unchanged in shape (`resizePhone` before the engine's
   callback, `onSurfaceSize` after it).

### 4.1 The re-issue, and why it is safe

`resizePhone` (before the engine's `onSurfaceChanged`) and the new
`syncPhoneSize` (after the engine's `onSurfaceCreated`) both call
`nativeSetPhone` with the **real surface size**, and only when it differs from
what the engine already holds.  Because section 3.1 shows that call is a pure
two-global store and section 3.2 shows the viewport is not re-issued per frame,
telling the engine the truth after the view exists is enough to change the
rectangle it lays out for.  On a device that was already correct the sizes agree
and **the call does not happen at all**, which is why the Fold7 path is
untouched.

## 5. Off-device verification, quoted

### 5.1 Clean build

Rebuilt in the private tree with `DH2_JDK_ROOT` = Adoptium JDK 21,
`DH2_ANDROID_SDK_ROOT` = `C:\Android\Sdk`, `DH2_VERSION_CODE=1111`,
`DH2_VERSION_NAME=1.0-fit-native`.  Tail of `build/apk-build.log`:

```
Running C:\Android\Sdk\build-tools\35.0.0\zipalign.exe
Running C:\Android\Sdk\build-tools\35.0.0\apksigner.bat
Verifies
Verified using v3 scheme (APK Signature Scheme v3): true
Number of signers: 1
Running C:\Android\Sdk\build-tools\35.0.0\apksigner.bat
Running C:\Android\Sdk\build-tools\35.0.0\zipalign.exe
Built signed ARM64 local test package: ...\host\fitnative\out\Dungeon-Hunter-2-native-fit.apk
```

exit code 0 (only the usual `-source 8/17 is obsolete` javac notices and aapt2's
benign `W zip : WARNING: header mismatch` lines, which also appear in the shared
build's staging).

Post-build:

```
zipalign -c -P 16 4                       -> exit 0
apksigner verify --print-certs            -> v3 true, 1 signer,
     cert daa24cd98557703003fb4518d1ed8504dee071f9620596592819176677772081
aapt2 dump badging                        -> package: name='local.dh2.fold7'
                                             versionCode='1111' versionName='1.0-fit-native'
                                             native-code: 'arm64-v8a' minSdkVersion:'29'
                                             targetSdkVersion:'35'
                                             application-label:'Dungeon Hunter 2 - Fold7 Test'
sha256 3ab74df9e529deeb8909b4cc3b3a5f63a18f45d8701a67a0fbc6dbdce20de1d1   13,666,842 bytes
outer APK  34 entries, zipfile.testzip() -> None
nested guest 220 entries, zipfile.testzip() -> None
```

Artefacts that had to be preserved, re-hashed from the delivered APK
(`tools/check_shipped_apk.py`):

```
lib/arm64-v8a/libzbridge.so              5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd  3,015,352 B   PASS
assets/zb/sysroot/system/lib/libc.so     d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8    992,808 B   PASS (VFP-patched)
assets/zb-version.txt                    content 2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65  PASS
```

Guest dex actually shipped inside `assets/dh2/game.apk`:

```
classes.dex  sha256 f192962cf50a289da2a3074ad93e625c30162b10ff89f366ebdad45e1ba6ded8
             (reviewed 1110 classes.dex 0e959205fe2af1625d4395a7cd67d654c0839b638d94fb5ed257938eddfab3e7)
             hooks: setLogicalSize 1, logicalWidth 1, logicalHeight 1, resizePhone 1,
                    onSurfaceSize 1, syncPhoneSize 1, installContent 1, engineRunning 1
classes2.dex sha256 c768b335a17babf83f3ddd66d088a42edeab4777ef5d7867675b1d09ee92652c
             markers: FitGeometry, FitOptions, isIdentity, needsOrientationRepair, native
splice check  guest apk sha256 5d5370df9d7c5e39226569cc05f3b1a5b050c7b448dc7fe8f298f52343de2783
                            -> fd7a6403732f621833b415dd8204a6a87a40e7c9c426260d7cf345b7029b7e41
              classes.dex    0e959205… -> f192962c…
              every other nested entry compared byte-for-byte: identical
```

`syncPhoneSize` is the **only** difference in hook count versus 1110, which is
the audit trail for "one new hook, nothing else".

### 5.2 Shipped-APK static checks — `checks=29 failures=0  RESULT PASS`

```
PASS outer zip has no CRC errors
PASS outer zip has no duplicate entries
PASS lib/arm64-v8a/libzbridge.so sha256 pinned
PASS lib/arm64-v8a/libzbridge.so size 3015352
PASS assets/zb/sysroot/system/lib/libc.so sha256 pinned
PASS nested guest has 220 entries
PASS classes.dex contains setLogicalSize / logicalWidth / logicalHeight /
     resizePhone / onSurfaceSize / syncPhoneSize / installContent
PASS classes2.dex contains FitGeometry / FitOptions / isIdentity /
     needsOrientationRepair / native
PASS classes2.dex still contains the guarded glViewport call
PASS classes2.dex still contains the guarded touch transform
PASS classes2.dex contains the guard predicate isIdentity
PASS classes2.dex has zero glGetIntegerv references (no read-back path)
   classes2.dex glViewport=1 setScale=1 isIdentity=1 native=6
PASS launcher dex carries the Native label ("Native - change nothing")
```

Note on the shape of this evidence: the helper dex **must** contain a
`glViewport` call and a `Matrix.setScale` call, because the non-Native modes need
them.  What is asserted instead is that the identity predicate the guard is built
from survived dexing, and that the removed read-back is genuinely gone
(`glGetIntegerv` count 0).  Native cannot reach either call site unless
`isIdentity()` returns false, and 5.3 proves it does not.

### 5.3 Geometry and identity harness — `checks=1065 failures=0  RESULT PASS`

`java -cp build/test-classes FitGeometryTest`, full transcript in
`build/fit-geometry-test.txt`.  `view`/`logical`/`viewport` are
`x,y WxH` in surface pixels.

**The required displays, Native and Auto:**

```
1920x1080  native identity=true  viewport=0,0 1920x1080 logical=1920x1080 touch=1.0,1.0/0.0,0.0
1920x1080  auto   identity=true  viewport=0,0 1920x1080 logical=1920x1080 touch=1.0,1.0/0.0,0.0
1280x720   native identity=true  viewport=0,0 1280x720  logical=1280x720  touch=1.0,1.0/0.0,0.0
1280x720   auto   identity=true  viewport=0,0 1280x720  logical=1280x720  touch=1.0,1.0/0.0,0.0
2712x1220  native identity=true  viewport=0,0 2712x1220 logical=2712x1220 touch=1.0,1.0/0.0,0.0
2712x1220  auto   identity=true  viewport=0,0 2712x1220 logical=2712x1220 touch=1.0,1.0/0.0,0.0
2184x1968  native identity=true  viewport=0,0 2184x1968 logical=2184x1968 touch=1.0,1.0/0.0,0.0
2184x1968  auto   identity=true  viewport=0,0 2184x1968 logical=2184x1968 touch=1.0,1.0/0.0,0.0
```

**Native applies no transformation**, asserted exhaustively:

```
native identity asserted for 845 window/aspect combinations
```

13 widths x 13 heights x 5 aspect arguments, each requiring viewport `0,0 WxH`,
logical `WxH`, `visible` == the whole surface, and
`touchScale=1,1 / touchOffset=0,0`.  Assertions that pass for every case include
`native viewport == window`, `native logical == window`, `native touch is
identity`, `native adds no bars`, and `native == auto`.

**The addendum's acceptance case, named:**

```
PASS ADDENDUM: Fold7 2184x1968 auto produces identity
PASS ADDENDUM: Fold7 2184x1968 native produces identity
PASS ADDENDUM: Fold7 auto viewport == 0,0 2184x1968
PASS ADDENDUM: Fold7 auto adds no bar
PASS regression guard: letterbox on Fold7 is still 2184x1228 centred at y=370
PASS regression guard: that letterbox is NOT what auto does now
```

The last two are the point: the old `auto`'s output (2184x1228 with 370 px bars
top and bottom) is still reproducible **by asking for Letterbox**, and is now
provably not what the default does.

**Orientation repair:**

```
oriented(2184,1968) = 2184x1968  repair=false
oriented(1220,2712) = 2712x1220  repair=true
PASS Fold7 metrics 2184x1968 are left alone
PASS Fold7 needsOrientationRepair == false
PASS Xiaomi portrait metrics 1220x2712 become 2712x1220
PASS a square reading is not repaired
PASS degenerate metrics never yield a zero
```

**Engine model — the cause, and the repair:**

```
engine rect from oriented metrics 2712x1220 -> vp=0,0 2712x1220
engine rect from un-oriented metrics 1220x2712 -> vp=0,0 1220x2712
that rect spans 45.0% of the 2712-wide surface
aspectOf(2712,1220) = 678:305
PASS repaired engine rect is the whole surface
PASS buggy engine rect is portrait-sized
PASS buggy rect spans well under the whole surface width
PASS the repair changes the engine rect
PASS Fold7 first call is unchanged by the repair: rect 0,0 2184x1968
PASS Fold7 repair is the identity
```

**The non-default modes still behave** (Nearest to a panel's own ratio wins):

```
Xiaomi 2712x1220  letterbox   view=272,0 2168x1220   bars L/R 272/272 (symmetric)
Xiaomi 2712x1220  fillwidth   logical=2712x1525 vp=0,-152 2712x1525 overshoot 153/152
Xiaomi 2712x1220  fillheight  logical=2168x1220 vp=272,0 2168x1220 (pillarbox, no invented pixels)
Xiaomi 2712x1220  stretch     vp=0,0 2712x1220  touch scale X 2168/2712
Fold7  2184x1968  letterbox   2184x1228 centred at y=370
Fold7  2184x1968  fillheight  logical=3498x1968 overshoot 657/657
20:9   2400x1080  letterbox   view=240,0 1920x1080
Test 10 reproduction: oriented 2009x1080 letterbox = 44,0 1920x1080
```

**Structural invariants** asserted for all 96 mode/display/aspect cases: view
origin non-negative, view inside the window, view centred to 1 px, visible inside
the view, all sizes positive, overshoot symmetric to 1 px, and the touch mapping
equal to `logical/viewport` with `offset == -viewport origin`.

**Options round-trip** (launcher writer → guest reader, reflectively):

```
launcher mode list = native, auto, letterbox, fillwidth, fillheight, stretch
42 launcher mode/aspect combinations round-tripped
native json = {"preferEnglish":true,"fitMode":"native","fitAspect":"16:9","fit16by9":false,"preserveContext":false}
letterbox json = {"preferEnglish":true,"fitMode":"letterbox","fitAspect":"16:9","fit16by9":true,"preserveContext":false}
PASS launcher MODES[0] == native
PASS native json round-trips to native
PASS native json keeps the legacy key false
PASS letterbox json keeps the legacy key true
PASS legacy fit16by9=false resolves to native
PASS legacy fit16by9=true still resolves to letterbox
```

## 6. The artifact and how to install it

```
C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\fitnative\out\Dungeon-Hunter-2-native-fit.apk
sha256 3ab74df9e529deeb8909b4cc3b3a5f63a18f45d8701a67a0fbc6dbdce20de1d1
versionCode 1111, versionName 1.0-fit-native, package local.dh2.fold7
```

Upgrade in place — do **not** uninstall `local.dh2.fold7` (that drops the
imported cache and saves), and the version code only ever goes up:

```powershell
adb install -r --abi arm64-v8a "C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\fitnative\out\Dungeon-Hunter-2-native-fit.apk"
```

The launcher label is still `Dungeon Hunter 2 - Fold7 Test`; the title bar inside
the app reads `Dungeon Hunter 2`.

## 7. What to check on the device

**Fold7 first, because it is the regression that must be gone.**

1. **Fold7, Native (the default on a fresh or upgraded install).**  Expected:
   full-window 2184x1968 with **no bars at all**, exactly as the pre-fit build
   rendered.  `dh2-events.txt` must contain
   `fit native display=2184x1968 handed=2184x1968 orientationRepair=false identity=true viewportApplied=false touchRemapped=false viewReplaced=false`
   and then `fit native surface=2184x1968 viewportApplied=false touchRemapped=false`.
   If `orientationRepair=true` appears on the Fold7, stop: the repair fired where
   it must not.
2. **Fold7, Auto.**  Must look identical to Native — same full window, no bar.
3. **Fold7, Letterbox.**  Must reproduce the intentional 2184x1228 centred
   surface with 370 px bars; this is the deliberately preserved old behaviour.
4. **Xiaomi 17T, Native.**  Expected: the black column on the side is **gone**
   and the game fills the panel.  Look for
   `fit native display=<W>x<H> handed=<H>x<W> orientationRepair=true` (the
   handed numbers must be landscape even though the display numbers were
   portrait), then `fit phone surfacecreated=…` or
   `fit phone surfacechanged=…` reporting the engine's
   `Width_Screen/Height_Screen` as the full surface, and
   `fit native surface=2712x1220 viewportApplied=false touchRemapped=false`.
5. **Xiaomi 17T, any fill mode.**  Should no longer be needed.  If Native now
   fills, that confirms the diagnosis in section 3 and the fill modes can be
   ignored.
6. **Touch on the Xiaomi.**  Tap a HUD control at the far left and again at the
   far right; both must land where they are drawn.  In Native mode this is
   guaranteed by construction (identity mapping), so a miss means the engine's
   own layout is still wrong and the log line from (4) is the thing to send.
7. **Nothing else changed.**  English preference, the cinematics
   graphics-context setting, crash diagnostics and the cache installer behave as
   before.

## 8. Unverified until a device run — and what I ruled out

Nothing on a screen was observed.  No phone and no emulator were used.  Stated
plainly:

1. **That the Xiaomi now fills.**  This is the whole point of the change and it is
   *predicted*, not observed.  The prediction rests on the static facts in
   section 3 (the call is a two-global store; the viewport is issued from those
   globals; it is not re-issued per frame), and on the assumption that the
   engine reads its layout dimensions from the same two globals.  Sections 3.1
   and 3.2 are read from the binary; **the last step — that the engine's own
   content layout also follows those globals at draw time — is inferred from the
   Xiaomi's observed 1280-of-2381 signature, not proven by disassembly.**
2. **That the Xiaomi's `onCreate` really reads portrait metrics.**  The user's
   diagnostics say the engine renders the portrait width; the mechanism by which
   `getDefaultDisplay().getMetrics()` returns portrait values inside a
   landscape-locked activity on that device is not established here.  If the
   repair does not fire (`orientationRepair=false` on the Xiaomi), the readings
   were already landscape and the cause is elsewhere — the log line in section 7
   item 4 is designed to settle this in one run.
3. **The exact numbers.**  The modelled buggy rectangle spans 45.0% of a
   2712-wide surface; the device reported 1280 of 2381 (53.8%).  The direction
   and the "portrait width" character agree; the exact ratio depends on the
   device's real `DisplayMetrics` and on whether the reported 2381 is the
   surface or the usable window.  The harness asserts only the qualitative
   claims.
4. **That `syncPhoneSize` is needed at all.**  It may be redundant with
   `resizePhone`, which runs before the engine's own `onSurfaceChanged`.  It is a
   one-shot, no-op-when-equal call, so the cost of keeping it is one comparison;
   the device log will show which of the two actually changed the size.
5. **Whether a later `nativeSetPhone` is honoured.**  The engine stores the
   arguments unconditionally, so the globals do change; whether anything that has
   already cached a *derived* layout picks the new value up is the same unknown
   as item 1.
6. **That the letterboxed `SurfaceView` is really centred on screen.**  The
   arithmetic is proven (`view=272,0` etc.) and `FitFrame` places the child with
   an explicit `child.layout(x, y, …)` rather than gravity, but only a screenshot
   proves a real `SurfaceView` ends up where the arithmetic says.  This is *not*
   the core fix any more, because the default no longer letterboxes anything.
7. **Touch alignment in `fillwidth`/`fillheight`/`stretch`.**  Those modes have
   `viewport size == logical size` but `surface != logical`, so the mapping is a
   translation/scale derived from the geometry under the assumption that the
   engine consumes the coordinates it is handed as logical pixels.  Native, Auto
   and Letterbox are identity by construction and do not depend on it.
8. **`zb-version.txt` and the runtime bundle.**  The delivered APK carries the
   pinned content `2dd2f3eb…` and a byte-identical `zb-files.txt`; note that the
   *file* `assets/zb-version.txt` itself hashes to `86b6093c…` because it is the
   ASCII text of that token plus a newline.  The two are easily confused and were
   confused once during this work.
9. **The 16 KiB page path** and everything else outside the fit machinery:
   untouched.

## 9. Where the work lives (write scope respected)

```
DH2Work-toolchain\host\fitnative\                      (new; the only tree written to)
  stage\compatibility\work\fold7-build\                private copy of the stage tree
      guest-java\local\dh2\compat\FitGeometry.java     NATIVE + isIdentity + engine model
      guest-java\local\dh2\compat\FitOptions.java      default native
      guest-java\local\dh2\compat\GameTrace.java       Native short-circuits, syncPhoneSize
      java\com\zettabridge\launcher\Dh2Options.java    NATIVE first
      java\com\zettabridge\launcher\Dh2Activity.java   migration + help text
      game-unsigned.apk                                patched guest (private copy only)
  build\                                               logs, transcripts, assembled guest
  guest-work\                                          private decoded guest trees
  tests\FitGeometryTest.java                           JVM geometry + identity + options
  tools\patch_guest_native.py                          smali patch + audit of the hooks
  tools\splice_guest_dex.py                            dex splice (never in place)
  tools\check_shipped_apk.py                           shipped-APK assertions
  build\scan_dis.py, build\engine-arm32.dis            engine disassembly analysis
  out\Dungeon-Hunter-2-native-fit.apk                  the tester APK
DH2Work\docs\HOST-FIT-NATIVE.md                        this document
```

The shared staging slot
`DH2Work-stage\compatibility\work\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so`
was re-hashed after the build and is unchanged
(`5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd`,
3,015,352 B), as are the shared slot's VFP-patched
`assets\zb\sysroot\system\lib\libc.so`
(`d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8`) and the
pinned `2dd2f3eb…` version token.  The reviewed 1110 artifact
`host\fit\out\Dungeon-Hunter-2-fit-modes.apk` still hashes to
`f5416ac6c6d86b82fe0ff16b9273555ea786c5a5bb35ef0dda891c48e5321030`, and the
reviewed `host\fit` guest input still hashes to
`5d5370df9d7c5e39226569cc05f3b1a5b050c7b448dc7fe8f298f52343de2783`.  No file
under `DH2Work-stage` (shared), `host\fit`, `host\shrink\ZettaBridge`,
`host\p0arm`, `host\hotfn`, `host\integrate`, `DH_sc`, `DH_sc-pr`, or any other
part of `DH2Work` was modified.
