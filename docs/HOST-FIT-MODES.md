# HOST-FIT-MODES — where the render surface is really controlled, and the fit-mode selector

Private compatibility work on the Dungeon Hunter 2 port.  Everything below was
produced off-device (Windows host, no phone, no emulator).  Section 6 states
plainly what is still unverified.

Artifact under test: `Dungeon-Hunter-2-fit-modes.apk`, version code **1110**,
version name `1.0-fitmodes`, package `local.dh2.fold7`, sha256
`f5416ac6c6d86b82fe0ff16b9273555ea786c5a5bb35ef0dda891c48e5321030`, 13,662,746 bytes.

---

## 1. The mechanism that actually controls the render surface / viewport

### 1.1 There is no host-side surface geometry

The host `libzbridge` does not choose a rectangle.  It forwards the guest's
`glViewport` verbatim:

- `core/include/zb/gl_hostcalls.h:106` — `inline constexpr std::uint32_t ZB_GL_HC_glViewport = 141u;`
- `core/src/gen/gl_dispatch.inc:641-649` —
  `case ZB_GL_HC_glViewport: { const GLint x = call.scalar<GLint>(0); ... backend_.glViewport(x, y, width, height); return true; }`

Nothing between the guest and the driver rewrites the rectangle.  `ANativeWindow_setBuffersGeometry`
exists only as a generated guest stub (`guest/stubs/gen/libandroid.S:198-204`); it is
not used to size the game surface.  `GuestWindowStyle` only picks a theme and
`FEATURE_NO_TITLE` (`host/shrink/ZettaBridge/AGENTS.md:1038`), it sets no geometry.

So **the render rectangle is chosen entirely on the guest side**: by the Android
view hierarchy that owns the `GameGLSurfaceView`, and by whatever the engine
(`libDungeonHunter2`) decides from the size it was told.

### 1.2 The view/surface rectangle comes from `GameTrace.installContent`

`guest-java/local/dh2/compat/GameTrace.java:64-76` (reviewed tree) is injected
into the guest's `onCreate` by `patch_game.py:62` and is present in the shipped
guest dex:

```
game-tree/smali/com/gameloft/android/GAND/GloftD2SS/DungeonHunter2.smali:1685
    invoke-static {p0, v0}, Llocal/dh2/compat/GameTrace;->installContent(Landroid/app/Activity;Landroid/view/View;)V
```

`installContent` wraps the `GameGLSurfaceView` in a `FrameLayout` whose
`onMeasure` shrinks the child to a 16:9 box; when `fit` is false it calls
`activity.setContentView(view)` and the surface is the whole window.

**The claim that `fit16by9` is only logged and never acted on is false in this
tree.**  It is read at `GameTrace.java:25`, traced at `:27`, **and consumed** at:

- `GameTrace.java:65` — `if(!fit){activity.setContentView(view);return;}` (the letterbox),
- `GameTrace.java:100` — `public static boolean fitEnabled(){return fit;}`, called from
  `game-tree/smali/.../GameRenderer.smali:125-128`, which re-sends the surface size
  to the engine whenever fit is on.

The real device runs prove it acted:

```
phone-test3/dh2-events.txt:18   ... start locale=en_GB preferEnglish=true fit16by9=true preserveContext=true
phone-test3/dh2-events.txt:18   ... surface size=2184x1228 view=2184x1228     <-- 16:9 box, 2184/1228 = 1.7785
phone-test4/dh2-events.txt:18   ... start ... fit16by9=false ...
phone-test4/dh2-events.txt:18   ... surface size=2184x1968 view=2184x1968     <-- full window
TEST4.md:5                      "...There was one surface creation, with a 2184x1228 view..."
```

### 1.3 What was genuinely missing — this is the actual bug

Two things were never controlled, and together they produce the reported symptom.

**(a) The engine's logical size is set once, from an un-oriented, un-fitted
`DisplayMetrics` reading, before the surface exists.**

```
game-tree/smali/com/gameloft/android/GAND/GloftD2SS/DungeonHunter2.smali:1671-1675
    iget v1, v0, Landroid/util/DisplayMetrics;->widthPixels:I
    iget v0, v0, Landroid/util/DisplayMetrics;->heightPixels:I
    invoke-static {v1, v0}, Lcom/gameloft/android/GAND/GloftD2SS/DungeonHunter2;->nativeSetPhone(II)V
```
The `GameGLSurfaceView` is only constructed afterwards, at `:1677-1679`, and
`installContent` runs at `:1685`.  The engine takes its GL viewport from that
first call and does not re-derive it later — `STANDALONE-TEST10-VIEWPORT.md:13-18`:

> "The original guest called `nativeSetPhone(1080,2009)` from portrait
> `DisplayMetrics` before it created that view. The engine then initialized a
> 1080×2009 GL viewport. Its later `onSurfaceChanged(1920,1080)` left the GL
> viewport at 1080×2009. ... the first phone-size call also determined cached
> engine projection/layout dimensions."

The guest activity is landscape-locked (`game-AndroidManifest.xml:18`,
`android:screenOrientation="landscape"` for `.DungeonHunter2`), so a portrait
reading at `onCreate` is already the wrong orientation, and the fitted 16:9
rectangle was never passed at all.  On a 2.223:1 panel (Xiaomi 17T, 2712x1220
landscape) the engine is handed portrait numbers, lays out its own 16:9 content
from the origin of the surface, and leaves the rest black — **on the right only**,
because GL viewport coordinates are measured from the bottom-left corner and
nothing in the old code ever moved the rectangle off `(0,0)`.

**(b) No code anywhere centres the rectangle.**  The reviewed `installContent`
relied on `FrameLayout` gravity, and there was no arithmetic `(W-w)/2` anywhere
in the tree; the engine's own rectangle was never touched at all.

The host can be asked what rectangle it ended up with: `core/src/gl/gl_manual.cpp:361`
declares `GL_VIEWPORT` (0x0BA2) as a four-element query, so `glGetIntegerv(GL_VIEWPORT)`
from the guest returns the driver's real rectangle, and
`core/src/gl/gl_shadow.cpp:120-123` shows `glViewport`/`glGetIntegerv` in the
host-call table as `Policy::Drop` / `Policy::Invalidate`.  That is the hook the
new Auto mode uses.

**Summary.**  The 16:9 path was alive but half-wired: it resized the *view* and
never told the *engine*, and it never centred anything.  Placement came from the
engine drawing at the surface origin.  The fix therefore has to (i) give the
engine the same rectangle the view gets, at the first `nativeSetPhone`, and
(ii) place the rectangle explicitly at `(W-w)/2, (H-h)/2`, and (iii) use the same
numbers for `glViewport` and for touch.

### 1.4 Touch space

`GameGLSurfaceView.smali:192-260` reads the raw view coordinates and passes them
straight to the engine:

```
    invoke-virtual {p1, v1}, Landroid/view/MotionEvent;->getX(I)F   -> float-to-int v1
    invoke-virtual {p1, v2}, Landroid/view/MotionEvent;->getY(I)F   -> float-to-int v2
    invoke-static/range {v0 .. v6}, .../GameGLSurfaceView;->nativeOnTouch(IIIJII)V
```

Touch is therefore expressed in the surface's own pixels.  This is why the design
below makes the surface equal the engine's logical size for every fit mode: the
mapping is then the identity *by construction*, and no engine-specific touch
formula has to be guessed.

---

## 2. Design

One pure function decides everything.  `local.dh2.compat.FitGeometry`
(no Android imports, compiled for the guest and unit-tested on the JVM) returns a
`Layout` containing

| field | meaning |
| --- | --- |
| `viewX,viewY,viewW,viewH` | the SurfaceView box inside the window, always `>= 0` and centred |
| `logicalW,logicalH` | the size handed to `nativeSetPhone` |
| `viewportX,viewportY,viewportW,viewportH` | the `glViewport` rectangle, surface coordinates |
| `visibleX,visibleY,visibleW,visibleH` | viewport ∩ surface, what the user actually sees |
| `touchScaleX/Y`, `touchOffsetX/Y` | logical = scale × (surface + offset) |

Modes (window `W x H`, target aspect `A = aw/ah`):

| mode | surface box | engine logical | viewport | result |
| --- | --- | --- | --- | --- |
| `auto` (default) | full window | full window | engine's own rect, re-centred | fills the real panel; whatever rectangle the engine picks is centred |
| `letterbox` | largest `A`-rect inside the window, centred | = box | `(0,0,boxW,boxH)` | bars on the long axis, symmetric |
| `fillwidth` | full window | `(W, W·ah/aw)` | `(0,(H-lh)/2,W,lh)` | fills the width; crops top/bottom when `lh > H` |
| `fillheight` | full window | `(H·aw/ah, H)` | `((W-lw)/2,0,lw,H)` | fills the height; crops left/right when `lw > W` |
| `stretch` | full window | `A`-rect inside the window | `(0,0,W,H)` | fills the window, non-uniform |

`FitGeometry.compute` uses the truncating division the shipped 16:9 helper used
(`(long)width*9/16`), so the reviewed surfaces are reproduced exactly.

**Auto is model-free by construction.**  It does not consult a device list or
even the aspect ratio: it asks the host for the engine's actual rectangle and
re-issues it centred —

```
x = (surfaceW - rectW) / 2
y = (surfaceH - rectH) / 2
```

— and it also sends the *oriented* display metrics (`max,min`, justified by the
landscape lock at `game-AndroidManifest.xml:18`) at the first `nativeSetPhone`,
because a pre-rotation portrait reading is the root cause in 1.3(a).
Any future device is covered: nothing is hardcoded.

`letterbox`/`fillwidth`/`fillheight`/`stretch` use the manual aspect override
(`16:9`, `20:9`, `21:9`, `1.11:1`, `4:3`, or `auto` → 16:9), which is reduced to
lowest terms (`21:9` → `7:3`, same ratio).

### 2.1 Plumbing, end to end

Guest helper (`guest-java/local/dh2/compat/`):

- `FitGeometry.java` — the pure geometry above (new).
- `FitOptions.java` — Android-free reader for `dh2-options.json` (new).  Keeps the
  legacy key: `fit16by9=true` resolves to `letterbox`, absent/false to `auto`.
- `GameTrace.java` — `setLogicalSize` (called from `onCreate` before
  `nativeSetPhone`), `logicalWidth/Height`, `resizePhone` (before the engine's
  `onSurfaceChanged`), `onSurfaceSize` (after it), `FitFrame` (explicit centred
  letterbox container), `wrap` (viewport + touch), and the touch listener.

Guest smali, patched by `host/fit/tools/patch_guest_fitmodes.py` (private copy of
the decoded guest; only `classes.dex` differs from the staged input):

```
DungeonHunter2.onCreate, before the first nativeSetPhone:
    invoke-static {v1, v0}, Llocal/dh2/compat/GameTrace;->setLogicalSize(II)V
    invoke-static {}, Llocal/dh2/compat/GameTrace;->logicalWidth()I     -> v1
    invoke-static {}, Llocal/dh2/compat/GameTrace;->logicalHeight()I    -> v0
    invoke-static {v1, v0}, ...->nativeSetPhone(II)V                    (unchanged call)

GameRenderer.onSurfaceChanged (replaces the old fitEnabled()/nativeSetPhone(p2,p3) block):
    invoke-static {p2, p3}, Llocal/dh2/compat/GameTrace;->resizePhone(II)V
    invoke-virtual {p0, p2, p3}, ...->nativeOnSurfaceChanged(II)V        (unchanged call)
    invoke-static {p2, p3}, Llocal/dh2/compat/GameTrace;->onSurfaceSize(II)V
```

The second hook runs **after** the engine's callback, which is the ordering that
made the reviewed Test 10 viewport override work, so the corrected rectangle is
the one the engine leaves in place.

Launcher (`java/com/zettabridge/launcher/`):

- `Dh2Options.java` (new, Android-free) — mode list, labels, aspect normaliser, and
  the exact JSON text.
- `Dh2Activity.java` — the dead checkbox at old line 94 is replaced by a
  `RadioGroup` of the five modes plus a manual-aspect `EditText`; both persist to
  `SharedPreferences` on change.  `configurePath()` writes
  `Dh2Options.json(preferEnglish, fitMode, fitAspect, preserveContext)`.
  **The rewrite-on-launch behaviour is kept**: `configurePath()` is called from
  `launchGame()` as well as from preparation, so a toggled mode takes effect on
  the next session.  Installs upgrading from the checkbox build migrate
  `fit16by9v2=true` → `letterbox`, otherwise → `auto`.

`dh2-options.json` as written for Letterbox:

```json
{"preferEnglish":true,"fitMode":"letterbox","fitAspect":"16:9","fit16by9":true,"preserveContext":false}
```

The legacy `fit16by9` key is still emitted so an older guest bundle keeps its
reviewed behaviour.

### 2.2 Centring and touch use the same numbers

`GameTrace.applyViewport` is the only place that calls `glViewport`, and it sets
the touch transform from the *same* `Layout` in the same call, so a rendered
rectangle and the input mapping cannot drift.  Because the viewport size always
equals the logical size, the touch transform reduces to

```
logical = (surface + touchOffset) × touchScale,   touchOffset = (-viewportX, -(viewH - viewportY - viewportH))
```

which is exactly the identity for Auto-with-no-correction and for Letterbox, and
for Auto's centring it is the expected `-(W-w)/2` translation.  The mouse/touch
listener is only installed on the GLSurfaceView and returns
`view.onTouchEvent(event)` unchanged, so the engine's own handler still runs.

---

## 3. Off-device verification, quoted

### 3.1 Clean build

Re-run from the private copy (`DH2Work-toolchain\host\fit\stage\compatibility\work\fold7-build`,
`DH2_JDK_ROOT` = Adoptium JDK 21, `DH2_ANDROID_SDK_ROOT` = `C:\Android\Sdk`,
`DH2_VERSION_CODE=1110`, `DH2_VERSION_NAME=1.0-fitmodes`).  Tail of `build/apk-build.log`:

```
Running C:\Android\Sdk\build-tools\35.0.0\apksigner.bat
Verified using v3 scheme (APK Signature Scheme v3): true
Number of signers: 1
Running C:\Android\Sdk\build-tools\35.0.0\zipalign.exe
Built signed ARM64 local test package: C:\Users\...\DH2Work-toolchain\host\fit\out\Dungeon-Hunter-2-fit-modes.apk
```
exit code 0 (only `-source 8/17 is obsolete` javac notices and aapt2's benign
`W zip : WARNING: header mismatch` lines, which also appear in the shared build's
own staging).

Post-build checks:

```
zipalign -c -P 16 4  -> exit 0
apksigner verify     -> exit 0, v3 true, 1 signer, cert daa24cd98557703003fb4518d1ed8504dee071f9620596592819176677772081
aapt2 dump badging   -> package: name='local.dh2.fold7' versionCode='1110' versionName='1.0-fitmodes'
                        native-code: 'arm64-v8a'  minSdkVersion:'29'  targetSdkVersion:'35'
zipfile.testzip()    -> None for the outer APK (34 entries) and for the nested guest (220 entries)
```

Artefacts that had to be preserved, and were (hashes identical before and after
this work, and identical to the ones the task pinned):

```
lib/arm64-v8a/libzbridge.so                      5c769c9de128e36f4e65431e21564fabc51ffae894b48794cf768f1ef0bf57dd  (3,015,352 B)
assets/zb/sysroot/system/lib/libc.so             d34addcec84fc69ef53a0c827ed41e38b096e3d381d16a6444dee911796409e8  (VFP-patched)
assets/zb-version.txt content                    2dd2f3ebcf68e2077ea24226e337c25417bc527118f3b9c416f664f845bc2d65
```

Guest dex actually shipped inside `assets/dh2/game.apk`:

```
classes.dex  sha256 0e959205fe2af1625d4395a7cd67d654c0839b638d94fb5ed257938eddfab3e7
             (original staged guest a13a3a614588d55bf7cc236fdab23a6a6dfda85547e5a117e7064ea572405f98)
             hooks present: setLogicalSize 1, logicalWidth 1, logicalHeight 1,
                            resizePhone 1, onSurfaceSize 1, installContent 1, engineRunning 1
classes2.dex sha256 872e7415446051ed77ebb91d51c42542955f97b868fc0d15a0a8ce511a4c13df
             (GameTrace, FitGeometry, FitOptions, setLogicalSize, resizePhone, onSurfaceSize, GamePaths)
```
Only `classes.dex` differs between the staged guest and the patched guest; every
other nested entry is byte-identical (checked by the patch script).

### 3.2 The option round-trips through `dh2-options.json`

`FitGeometryTest` drives the launcher's writer into the guest's reader for all
five modes × five aspect strings, plus the legacy and malformed cases:

```
== dh2-options.json round-trip
   letterbox json = {"preferEnglish":true,"fitMode":"letterbox","fitAspect":"16:9","fit16by9":true,"preserveContext":false}
   (25 mode/aspect combinations asserted equal after parse)
   letterbox keeps the legacy fit16by9=true key          PASS
   auto writes the legacy fit16by9=false key             PASS
   legacy fit16by9=true resolves to letterbox            PASS
   legacy fit16by9=false resolves to auto                PASS
   empty object defaults to auto 16:9                    PASS
   truncated json falls back to defaults                 PASS
```

Manual aspect parsing:

```
   16:9       -> 16:9       20:9 -> 20:9       21:9 -> 7:3 (same ratio)
   4:3        -> 4:3        1.11:1 -> 111:100  16000:9000 -> 16:9
   auto -> 16:9    garbage -> 16:9    0:9 -> 16:9
```

### 3.3 Geometry unit test — real numbers

`java -cp build/test-classes FitGeometryTest` → **`checks=613 failures=0`, `RESULT PASS`**
(full transcript: `host/fit/build/fit-geometry-test.txt`).

Required aspects, `auto` / `letterbox` / `fillwidth` / `fillheight` / `stretch`
with the 16:9 target.  `view=x,y WxH`, `vp=x,y WxH` is the GL viewport,
`overshoot L/R/T/B` is how much logical space is off-surface on each side.

**Exact 16:9 — 1920x1080** (identical for 1280x720):

```
auto       view=0,0 1920x1080  logical=1920x1080  vp=0,0 1920x1080     overshoot 0/0/0/0  touch identity
letterbox  view=0,0 1920x1080  logical=1920x1080  vp=0,0 1920x1080     overshoot 0/0/0/0  touch identity
fillwidth  view=0,0 1920x1080  logical=1920x1080  vp=0,0 1920x1080     overshoot 0/0/0/0  touch identity
fillheight view=0,0 1920x1080  logical=1920x1080  vp=0,0 1920x1080     overshoot 0/0/0/0  touch identity
stretch    view=0,0 1920x1080  logical=1920x1080  vp=0,0 1920x1080     overshoot 0/0/0/0  touch identity
```
An exact 16:9 window gains no bars, no crop, and an identity touch mapping in
every mode — asserted for both exact cases.

**20:9 — 2400x1080** (19.5:9 2340x1080 shown where it differs):

```
auto       view=0,0 2400x1080      logical=2400x1080      vp=0,0 2400x1080        bars 0/0/0/0
letterbox  view=240,0 1920x1080    logical=1920x1080      vp=0,0 1920x1080        bars L/R 240/240, T/B 0/0
           (2340x1080 -> view=210,0 1920x1080, bars L/R 210/210)
fillwidth  view=0,0 2400x1080      logical=2400x1350      vp=0,-135 2400x1350     overshoot T/B 135/135, visible 0,0 2400x1080
           (2340x1080 -> logical 2340x1316, vp=0,-118, overshoot T/B 118/118)
fillheight view=0,0 2400x1080      logical=1920x1080      vp=240,0 1920x1080      visible 240,0 1920x1080, touch offset X -240
stretch    view=0,0 2400x1080      logical=1920x1080      vp=0,0 2400x1080        touch scale X 0.8
```

**1.11:1 Fold7 — 2184x1968:**

```
auto       view=0,0 2184x1968   logical=2184x1968   vp=0,0 2184x1968    bars 0/0/0/0, touch identity
letterbox  view=0,370 2184x1228 logical=2184x1228   vp=0,0 2184x1228    bars T/B 370/370, touch identity
fillwidth  view=0,0 2184x1968   logical=2184x1228   vp=0,370 2184x1228  visible 0,370 2184x1228
fillheight view=0,0 2184x1968   logical=3498x1968   vp=-657,0 3498x1968 overshoot L/R 657/657, touch offset X +657
stretch    view=0,0 2184x1968   logical=2184x1228   vp=0,0 2184x1968    touch scale Y 0.62398374
```

**2.223:1 Xiaomi 17T — 2712x1220** (the reported device):

```
auto       view=0,0 2712x1220   logical=2712x1220   vp=0,0 2712x1220     bars 0/0/0/0, touch identity
letterbox  view=272,0 2168x1220 logical=2168x1220   vp=0,0 2168x1220     bars L/R 272/272  (not 544 on the right)
fillwidth  view=0,0 2712x1220   logical=2712x1525   vp=0,-152 2712x1525  overshoot T/B 153/152, visible 0,0 2712x1220
fillheight view=0,0 2712x1220   logical=2168x1220   vp=272,0 2168x1220   visible 272,0 2168x1220, touch offset X -272
stretch    view=0,0 2712x1220   logical=2168x1220   vp=0,0 2712x1220     touch scale X 0.79941
```

**Left-anchored engine rectangle, the exact reported symptom.**  The engine's
rectangle is fed to `FitGeometry.centre` as it would come back from
`glGetIntegerv(GL_VIEWPORT)`:

```
centre(2712x1220, engine rect 0,0 2168x1220)
  -> viewport=272,0 2168x1220  visible=272,0 2168x1220  touch offset=-272,0
centre(1920x1080, engine rect 0,0 1080x2009)     (an oversized rect: symmetric crop)
  -> viewport=420,-464 1080x2009  visible=420,0 1080x1080  touch offset=-420,+465
```
An engine rectangle that already fills the surface returns the identity
(`viewport=0,0`, `touchIdentity=true`), so a correctly-behaving device is not
perturbed.

Asserts applied to every mode of every case (613 total): `view` origin
non-negative, `view` inside the window, `view` centred to 1 px,
`visible` origin non-negative and centred to 1 px, horizontal and vertical
overshoot symmetric to 1 px, positive logical/viewport sizes, and for
auto/letterbox `surface size == logical size` with an identity touch mapping.
The 1 px tolerance is only the integer truncation of an odd difference
(153/152 above).

Reviewed surfaces reproduced from the new math:

```
Fold7 2184x1968 + letterbox(16:9) -> view 0,370 2184x1228   == phone-test3/dh2-events.txt:18 "surface size=2184x1228 view=2184x1228"
portrait 1080x2009 -> oriented 2009x1080 + letterbox -> view 44,0 1920x1080
                                                            == STANDALONE-TEST10-VIEWPORT.md "fitted=1920x1080"
```

**Mode caveat, by design.**  A fill mode only crops when the window is on the
matching side of the target aspect; otherwise it degenerates to a centred
letterbox/pillarbox instead of inventing pixels.  Concretely: `fillwidth` on the
Fold7's 1.11:1 window is a 2184x1228 letterbox, and `fillheight` on the Xiaomi's
2.223:1 window is a 2168x1220 pillarbox.  To crop, pick the fill mode that
matches the panel: `fillwidth` for panels wider than 16:9, `fillheight` for
panels narrower than 16:9.

---

## 4. The artifact and how to install it

```
C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\fit\out\Dungeon-Hunter-2-fit-modes.apk
sha256 f5416ac6c6d86b82fe0ff16b9273555ea786c5a5bb35ef0dda891c48e5321030
versionCode 1110 (installed build is 1108), versionName 1.0-fitmodes, package local.dh2.fold7
```

Upgrade in place — do **not** uninstall `local.dh2.fold7` (that would drop the
imported cache and saves), and the version code only ever goes up:

```powershell
adb install -r --abi arm64-v8a "C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\fit\out\Dungeon-Hunter-2-fit-modes.apk"
```

The launcher label is still `Dungeon Hunter 2 - Fold7 Test` (the pinned-guest
build path is what renames it to `Dungeon Hunter 2`); the title bar inside the
app reads `Dungeon Hunter 2`.

## 5. What to check on the device

1. **The selector exists.**  Open the app: under "Prefer English" there is now
   "Render fit mode" with five radio buttons — Auto (default), Letterbox,
   Fill width, Fill height, Stretch — and a "Manual aspect override" text box.
   The old "Fit game to 16:9" checkbox is gone.
2. **The option reaches the guest.**  Cache dir `Android/data/local.dh2.fold7/files/plugins/<game>/dh2-options.json`
   holds `"fitMode":"..."`; after each launch `.../dh2-events.txt` starts with
   `start locale=... preferEnglish=... fit16by9=... preserveContext=... fitMode=... fitAspect=... aspect=W:H`.
   Change the mode, launch again, confirm the new value — this is the
   rewrite-on-launch fix.
3. **Xiaomi 17T, Auto (default).**  Expect the log line
   `fit geometry predicted display=<W>x<H> window=2712x1220 ...` and then, on
   session end, `fit viewport stage=... auto engine=x,y WxH ...` showing the
   rectangle it centred.  Expected visual: no black column on the right; if the
   engine keeps its own 16:9 rectangle, it should now be `x=272` with equal black
   on both sides.
4. **Xiaomi 17T, Letterbox.**  Expect `fit frame window=2712x1220 ... view=272,0 2168x1220 ...`
   and a symmetric 272 px bar on each side (previously the whole 544 px was on
   the right).  Confirm the bars are equal, not just present.
5. **Fold7, Letterbox.**  Expect `fit frame window=2184x1968 ... view=0,370 2184x1228 ...`
   — equal 370 px bars top and bottom, reproducing the reviewed 2184x1228 surface.
6. **Touch alignment.**  In Letterbox and Auto, tap a HUD control near the left
   edge and again near the right edge; both must register where they are drawn.
   `dh2-events.txt` should show no `fit frame placement ... predicted=...`
   mismatch line (that line only appears if the real placement disagreed with the
   predicted one).
7. **Nothing else changed.**  English preference, the cinematics graphics-context
   setting, crash diagnostics and the cache installer behave as before.

## 6. Unverified until a device run

Everything in this list is why this document does not claim the symptom is fixed:

1. **The engine's real viewport.**  Auto's whole premise is that
   `glGetIntegerv(GL_VIEWPORT)` through the host-call path returns the driver's
   rectangle in surface pixels.  That path is *read* here, never executed.  If it
   returns zeros or throws, Auto logs
   `fit viewport stage=... auto: engine viewport unreadable, left unchanged` and
   the old placement stands; the other four modes do not depend on it.
2. **When the engine sets its viewport.**  The override runs after the engine's
   `onSurfaceCreated`/`onSurfaceChanged` and once after the first frame.  If the
   engine re-issues `glViewport` at the start of every frame, Auto's centring
   would be overwritten each frame and would not stick.  The reviewed evidence
   (Test 10: the viewport stayed at its init value across a surface change) says
   it is set once, but that has not been re-checked on a phone.
3. **Whether the engine accepts a later `nativeSetPhone`.**  `resizePhone` re-sends
   the logical size when it differs from the first call.  Test 10's finding is
   that the *first* call is what caches the projection, so the orientation fix at
   the first call should be the one that matters; whether a resize re-send helps
   or is ignored is unmeasured.
4. **That the letterboxed SurfaceView is really centred on screen.**  The
   arithmetic is proven (`view=272,0` etc.) and `FitFrame` places the child with
   an explicit `child.layout(x, y, ...)` rather than gravity, but the only proof
   that a SurfaceView ends up centred on a real device is a screenshot.  This is
   the core fix, so it is the first thing to look at.
5. **Touch alignment in `fillwidth` / `fillheight` / `stretch`.**  Those modes have
   `viewport size == logical size` but `surface != logical`, so the mapping is a
   translation/scale derived from the geometry under the assumption that the
   engine consumes the coordinates it is handed as logical pixels.  Auto and
   Letterbox are identity by construction and do not depend on that assumption;
   the fill/stretch modes do.  The exact offsets are in section 3.3 so a device
   run can confirm or contradict them.
6. **The 16 KiB page and other device paths.**  Unrelated to this change and not
   touched.
7. **Real rendering of any mode.**  No device or emulator was used; the geometry,
   the option round-trip, the dex contents, the build and the signature are
   verified, and nothing here was observed on a screen.

## 7. Where the work lives (write scope respected)

```
DH2Work-toolchain\host\fit\
  stage\compatibility\work\fold7-build\        private copy of the stage tree
      guest-java\local\dh2\compat\FitGeometry.java      (new)
      guest-java\local\dh2\compat\FitOptions.java       (new)
      guest-java\local\dh2\compat\GameTrace.java        (rewritten)
      java\com\zettabridge\launcher\Dh2Options.java     (new)
      java\com\zettabridge\launcher\Dh2Activity.java    (selector)
      game-unsigned.apk                                 (patched guest, private copy only)
  stage\compatibility\work\research\ZettaBridge\  private copy of the shrink fork
  tools\patch_guest_fitmodes.py                     smali patch + dex splice
  tests\FitGeometryTest.java                        JVM geometry + round-trip test
  build\  apk-build.log, fit-geometry-test.txt, guest-decoded\, game-fitmodes.apk
  out\    Dungeon-Hunter-2-fit-modes.apk
```
No file under `DH2Work-stage\compatibility\work` (shared), `host\shrink\ZettaBridge`,
`host\integrate`, `host\p0`, `host\jni`, `host\dl`, `DH2Work` (other than this one
document) or `DH_sc`/`DH_sc-pr` was modified; the shared staging slot's
`libzbridge.so`, VFP `libc.so` and `zb-version.txt` were re-hashed afterwards and
are unchanged.
