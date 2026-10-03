# "Fit game to 16:9" confirmed on a physical screen (2026-10-03)

Prior project documentation listed this as open, twice over:

> "Aspect ratio. The game is a 16:9 title; on a 20:9 display the surface was the full
> screen and the frame is stretched. A 16:9 surface constraint is written and packaged as
> a separate build (`Dungeon-Hunter-2-Test6-aspect.apk`) but **has not been confirmed on a
> screen**."

and in the local handoff: "inner-screen aspect/touch alignment" as an unresolved item.

## Result: it works

Device Galaxy Z Fold7 **SM-F966B**, build `1.0-work3-profile` (versionCode 17). With the
unfolded inner panel active (2184x1968 — an aspect of **1.11:1**, nowhere near 16:9, so the
distortion would be severe if unhandled), the option was enabled and the game launched.

Guest event trace (`dh2-events.txt`):

```text
start locale=en_GB preferEnglish=false fit16by9=true preserveContext=false
surface created #1 EGL=… GL=OpenGL ES 3.2 renderer=Adreno (TM) 830
surface initialization returned
surface size=2184x1228 view=2184x1228
surface resize returned
render returned frames=1 elapsedMs=464
```

`2184 x 1228` is **exactly 16:9** (2184 / 1228 = 1.7785; 16/9 = 1.7778). The wider window
is 2184x1968, so the game is rendering into a 2184x1228 letterboxed view centred in the
panel rather than being stretched to fill it.

Visual confirmation: `fit16by9.png` in this directory, a device screenshot of the opening
cinematic. The cinematic frame is bounded top and bottom by black bars and its content is
undistorted, with the subtitle line below it.

## Why it took two things to work

The App layer implements it in two halves, and both are needed:

1. **Host/guest Java** — `GameTrace.installContent()` wraps the content view in a
   `FrameLayout` whose `onMeasure` computes `h = width*9/16`, falling back to
   `w = height*16/9` when that would overflow, and centres the child.
2. **Guest smali** — `patch_game.py` rewrites `GameRenderer.onSurfaceChanged` so that
   `nativeSetPhone(w, h)` is called only while `GameTrace.fitEnabled()` is true, which is
   what tells the engine the real render dimensions. Without it the engine would still
   think it owns the full panel.

The option stays **off by default** (`fit16by9=false`), and the default
`preferEnglish=false` in this run reflects the checkbox state at the time, not a defect.

## Still open on this axis

* Touch alignment under the letterbox is **not** verified — a screenshot cannot show
  whether input coordinates are mapped into the 2184x1228 view or still span the panel.
  That needs a gameplay interaction test.
* Behaviour while folded (cover display, 2520x1080) is unverified for this option.
* Font/HUD scaling at the reduced view height is unverified.
