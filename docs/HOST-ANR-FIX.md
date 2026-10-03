# HOST-ANR-FIX — bounding the guest `onPause` wait that ANRs the Xiaomi 17T

**Status: built and verified off-device only. No phone, no emulator was used.**
Everything below is either guest/engine bytes read on this machine or output of a
command quoted in this file. Device success criteria remain the Lead's to check:
reach the menu with no ANR, and load a map without a black screen.

| item | value |
| --- | --- |
| APK | `DH2Work-toolchain\host\anrfix\out\Dungeon-Hunter-2-anrfix-v1114.apk` |
| package | `local.dh2.fold7` |
| versionCode / versionName | **1114** / `1.0-anrfix-onpause` (previous good build: 1113) |
| bytes / sha256 | 13,666,842 / `8c25753dd93884f775ec93754b02f8cd6b5526750c494e52c770616e13dd0233` |
| guest input (unchanged) | `game-unsigned-pristine.apk` sha256 `5d5370df9d7c5e39…`, 10,243,544 B |
| nested guest dex | 528,432 B (1113 baseline: 528,360 B, +72 B) |
| write scope | `DH2Work-toolchain\host\anrfix\` only, plus this document |

---

## 1. What the `onPause` sleep actually is

`patched/smali/.../DungeonHunter2.smali` and the decoded tree used for this build
carry the same method. The sleep is only **10 ms**, and it is **not** a delay —
it is the poll interval of an unbounded wait loop:

```smali
.method protected onPause()V
    .locals 2

    :goto_0
    invoke-static {}, Lcom/gameloft/android/GAND/GloftD2SS/DungeonHunter2;->nativeCanInterrupt()I
    move-result v0
    if-nez v0, :cond_0
    const-wide/16 v0, 0xa          # 10 ms
    :try_start_0
    invoke-static {v0, v1}, Ljava/lang/Thread;->sleep(J)V
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0
    goto :goto_0                    # <-- unconditional back edge
    :catch_0
    move-exception v0
    goto :goto_0
    :cond_0
    invoke-super {p0}, Landroid/app/Activity;->onPause()V
    sget-object v0, Lcom/gameloft/android/GAND/GloftD2SS/DungeonHunter2;->I:Landroid/opengl/GLSurfaceView;
    invoke-virtual {v0}, Landroid/opengl/GLSurfaceView;->onPause()V
    return-void
.end method
```

The shipped 1113 guest dex says the same thing (`dexdump -d`, `java.lang.Thread.sleep`
is `method@0d31` there):

```
                |[033ce8] com.gameloft.android.GAND.GloftD2SS.DungeonHunter2.onPause:()V
 7100 6102 0000 |0000: invoke-static {}, …DungeonHunter2;.nativeCanInterrupt:()I
 0a00           |0003: move-result v0
 3900 0a00      |0004: if-nez v0, 000e
 1600 0a00      |0006: const-wide/16 v0, #int 10
 7120 310d 1000 |0008: invoke-static {v0, v1}, Ljava/lang/Thread;.sleep:(J)V
 28f5           |000b: goto 0000          <-- back edge, no bound
 0d00           |000c: move-exception v0
 28f3           |000d: goto 0000
 6f10 1400 0200 |000e: invoke-super {v2}, Landroid/app/Activity;.onPause:()V
 6200 4c00      |0011: sget-object v0, …DungeonHunter2;.I:Landroid/opengl/GLSurfaceView;
 6e10 de00 0000 |0013: invoke-virtual {v0}, Landroid/opengl/GLSurfaceView;.onPause:()V
 0e00           |0016: return-void
```

**What depends on the loop finishing:** only the two statements after it — the
framework `super.onPause()` and `GLSurfaceView.onPause()`. Nothing in the engine
is called by `onPause`, and no engine API waits for it. `nativePause` is called
from `GameGLSurfaceView.onWindowFocusChanged`, not from here.

### `nativeCanInterrupt()` is one global load

`host/fitnative/build/engine-arm32.dis` (libDungeonHunter2.so, symbols intact):

```
00532ea8 <Java_com_gameloft_android_GAND_GloftD2SS_DungeonHunter2_nativeCanInterrupt>:
  532ea8: ldr r3, [pc, #0x10]
  532eac: ldr r2, [pc, #0x10]
  532eb0: add r3, pc, r3
  532eb4: ldr r2, [r3, r2]
  532eb8: ldr r0, [r2]        # r0 = *(int*)GOT[0x2398]
  532ebc: bx lr
```

Six instructions: return one 32-bit global. No lock, no work, no timeout.

That global has exactly **one writer**, `appUpdate` (0x530fc8), which is the
engine's per-frame update and is reached only from `GameRenderer.nativeRender`:

```
005311a0 <Java_..._GameRenderer_nativeRender>:
  5311a0: ldr r3, [pc, #0x18]
  5311a4: ldr r2, [pc, #0x18]      # GOT[0x2940]
  5311b0: ldr r3, [r2]
  5311b4: cmp r3, #0
  5311b8: bxne lr                  # engine "skip update" set -> return, no frame
  5311bc: b 0x530fc8 <appUpdate>

00530fc8 <appUpdate>:
  531040: ldr r8, [r4, r2]         # &GOT[0x2398]
  531048: str r6, [r8]             # flag = 0   (r6 == 0)
  53104c: ldr r0, [r3]
  531050: bl 0x32ccc4 <_ZN11Application6UpdateEv>
  531054: mov r3, #1
  531058: str r3, [r8]             # flag = 1
```

So `onPause` waits for **the next completed engine frame**: `0` is stored
immediately before `Application::Update()` and `1` immediately after it returns.
If the GL thread is not drawing, or is inside an `Update()` that never returns,
**nothing in the process can ever set that flag again** — the wait is unbounded
by construction, not "slow". That is the "stall, not computation" the logcat
shows, and it is why the whole method is a main-thread ANR risk regardless of SoC.

### What starts the first `onPause` during startup

The intro movie is a separate activity owned by the game itself:
`GLMediaPlayer` line 812–828 builds `new Intent(DungeonHunter2.this, MyVideoView.class)`
and calls `DungeonHunter2.startActivity(...)` (`ResumeMovie()` does the same at
line 136–156). Starting it pauses `DungeonHunter2`, so the guest's own
`onPause` runs while `Application::Update()` is still doing first-run work —
exactly when the flag is most likely to be `0`. `MyVideoView.onPause` even logs
`"**************onPause introvideo"`.

Combined with the device facts (Xiaomi input-dispatch timeout 5000 ms, Fold7
10000 ms) this is the whole difference between the two phones: the same unbounded
wait is survivable at 10 s and fatal at 5 s.

---

## 2. What was changed, and why capping instead of removing

`anrfix/patch/patch_onpause_cap.py` rewrites only that one method. It keeps the
`nativeCanInterrupt()` poll and the original 10 ms `Thread.sleep`, and bounds the
loop two independent ways:

* **500 ms wall clock** — `deadline = System.nanoTime() + 0x1dcd6500` (500,000,000 ns);
* **80-poll backstop** — so the loop also terminates if `Thread.sleep` never
  actually sleeps (its catch path jumps straight back to the loop head).

Then it falls through to the original tail (`super.onPause()` +
`GLSurfaceView.onPause()`).

Shipped dex (`dexdump -d`, file addresses stripped):

```
                |[033d00] com.gameloft.android.GAND.GloftD2SS.DungeonHunter2.onPause:()V
 7100 2b0d 0000 |0000: invoke-static {}, Ljava/lang/System;.nanoTime:()J
 0b02           |0003: move-result-wide v2
 1700 0065 cd1d |0004: const-wide/32 v0, #1dcd6500          # 500 ms
 bb02           |0007: add-long/2addr v2, v0                 # v2:v3 = deadline
 1204           |0008: const/4 v4, #0
 7100 6102 0000 |0009: invoke-static {}, …nativeCanInterrupt:()I
 0a00           |000c: move-result v0
 3900 1900      |000d: if-nez v0, 0026
 d804 0401      |000f: add-int/lit8 v4, v4, #1
 1300 5000      |0011: const/16 v0, #80
 3404 0300      |0013: if-lt v4, v0, 0016
 2811           |0015: goto 0026                            # polls exhausted -> leave
 7100 2b0d 0000 |0016: invoke-static {}, Ljava/lang/System;.nanoTime:()J
 0b00           |0019: move-result-wide v0
 3105 0002      |001a: cmp-long v5, v0, v2
 3a05 0a00      |001c: if-ltz v5, 0026                      # deadline reached -> leave
 1600 0a00      |001e: const-wide/16 v0, #int 10
 7120 320d 1000 |0020: invoke-static {v0, v1}, Ljava/lang/Thread;.sleep:(J)V
 28e6           |0023: goto 0009                            # bounded back edge
 0d00           |0024: move-exception v0
 28e4           |0025: goto 0009
 6f10 1400 0600 |0026: invoke-super {v6}, Landroid/app/Activity;.onPause:()V
 6200 4c00      |0029: sget-object v0, …DungeonHunter2;.I:Landroid/opengl/GLSurfaceView;
 6e10 de00 0000 |002b: invoke-virtual {v0}, Landroid/opengl/GLSurfaceView;.onPause:()V
 0e00           |002e: return-void
        catches       : 1
          0x0020 - 0x0023
            Ljava/lang/Exception; -> 0x0024
```

`registers` rose 3 → 7 (`.locals 6`), instruction count 23 → 47 code units.

**Why cap and not delete.** Deleting the wait would change pause timing on the
device that currently works: the loop is the engine's own "wait until the frame
is finished" handshake, and the Fold7 evidence shows it normally converges
(`nativeCanInterrupt=75` calls against `nativePause=3` in the phone-test3
report, i.e. of the order of 25 polls ≈ 250 ms per pause). A 500 ms budget keeps
that handshake intact and still leaves 10x margin under the 5000 ms timeout;
in the worst case the main thread gives up and runs the same two tail statements
the loop would have run anyway, so no behaviour is lost other than the wait.

**Why this also breaks the freeze cycle.** Lifecycle callbacks are serialized on
the main thread, so a blocked `onPause` also blocks the queued `onResume`, and
`DungeonHunter2.onResume` is the only caller of `GLSurfaceView.onResume()`. If
the GL thread has been stopped (`GLSurfaceView.onPause()` or a destroyed
surface), the flag can only be set again by a frame, and frames can only resume
after `onResume` — which cannot be delivered until `onPause` returns. Bounding
`onPause` is what makes that cycle recoverable rather than permanent.

---

## 3. The map-loading black screen

**It is not the sleep, and it is not a dead engine either: it is a separate
guest code path that paints black whenever the window has no focus, coupled to
the same stall.**

`GameRenderer.onDrawFrame` (`patched/smali/.../GameRenderer.smali` lines 78–121):

```smali
sget-boolean v0, Lcom/gameloft/android/GAND/GloftD2SS/GameRenderer;->e:Z
if-nez v0, :cond_0
    invoke-interface {p1, v1, v1, v1, v1}, Ljavax/microedition/khronos/opengles/GL10;->glClearColor(FFFF)V
    const/16 v0, 0x4100
    invoke-interface {p1, v0}, Ljavax/microedition/khronos/opengles/GL10;->glClear(I)V
:cond_0
…
invoke-static {}, Lcom/gameloft/android/GAND/GloftD2SS/GameRenderer;->nativeRender()V
```

`GameRenderer.e` is set from the focus callback
(`GameGLSurfaceView.onWindowFocusChanged(Z)`, lines 452–529), which is also the
call site that matches the ANR reason string `Waited 5000ms for
FocusEvent(hasFocus=false)`:

```smali
sput-boolean p1, Lcom/gameloft/android/GAND/GloftD2SS/GameRenderer;->e:Z
if-nez p1, :cond_1
    invoke-static {}, …GLMediaPlayer;->stopAllSounds()V
    invoke-static {v3}, …DungeonHunter2;->nativePause(I)V      # v3 = 1
    …
:cond_3
    invoke-static {v3}, …DungeonHunter2;->nativeResume(I)V
```

So: focus lost → the engine is paused (`nativePause(1)` → `appPause` →
`Application::Pause()` + `OnInterruptPause()`, all on the **main thread**) and
every frame clears the framebuffer to black until focus returns and
`nativeResume(1)` runs. Additionally `nativeOpenIGM` sets the `GOT[0x2940]` flag
that makes `nativeRender` return immediately, so the engine stops updating
entirely while that flag is set.

What turns a temporary black frame into a *permanent* black screen is the stall:
once `DungeonHunter2.onPause` blocks the main thread, `onResume` is never
delivered, `GLSurfaceView.onResume()` never runs, `onDrawFrame` is never called
again, and the last cleared (black) frame is what stays on screen.

**Verdict, with the evidence split stated explicitly:** the *stall* is the
unbounded `onPause` wait (section 1) and is fixed here. The *black pixels* come
from a distinct code path — the `GameRenderer.e == false` clear-to-black, reached
from the focus event the ANR traces name. They are two mechanisms sharing one
trigger (focus loss). Not proven on device either way, so if a map still shows
black after this build, the next step is to log
`GameGLSurfaceView.onWindowFocusChanged` / `GameRenderer.e` / the `nativeResume(1)`
call, which distinguishes "focus never came back" from "the GL thread never
restarted".

---

## 4. Other main-thread blocking found in the load path

`onPause` is the only blocker the device logs demonstrate: the three reported ANR
traces all have the same stack ending in `DungeonHunter2.onPause`. The following
are additional main-thread paths that *can* exceed 5 s and would then produce the
same ANR reason; none of them is observed in a trace, so none is claimed as a
cause, and none was changed.

| where | what runs on the main thread | evidence |
| --- | --- | --- |
| `GameGLSurfaceView.onWindowFocusChanged(false)` | `nativePause(1)` → `appPause` → `Application::Pause()` + `OnInterruptPause()`; also `GLMediaPlayer.stopAllSounds()`. Unbounded, fires on every focus loss (including the ANR dialog's own focus theft) — the strongest second suspect | `GameGLSurfaceView.smali:477-481`; `appPause` at `engine-arm32.dis:584596` |
| `DungeonHunter2.onResume` | `nativeGetGameMusicVolume()` (JNI), `GLSurfaceView.onResume()`, `SensorManager.registerListener` | `DungeonHunter2.smali:2021-2087` |
| `DungeonHunter2.onSensorChanged` | `nativeAccelerometer(FFF)` at `SENSOR_DELAY_GAME` (~50 Hz) straight into the engine, on the main looper | `DungeonHunter2.smali:2099-2141`; registered in `onResume` |
| `DungeonHunter2.onCreate` → `Musicplayer.initMediaList()` | `MediaPlayList.<init>` → `MediaPlayer.setDataSource/prepare/start` and a 100 ms `Thread.sleep` with `seekTo`, i.e. synchronous media I/O during startup | `DungeonHunter2.smali:1776`; `MediaPlayList.smali:463-490` |
| `GameGLSurfaceView.onTouchEvent` | `nativeOnTouch` per event (main thread) | `GameGLSurfaceView` touch path |
| `GLUtils/HTTP` | `Object.wait(J)` and `Thread.join()` on the main thread for network work | `GLUtils/HTTP.smali:206,329` |
| `GLMediaPlayer` | `MediaPlayer.prepare()` ×3 | `GLMediaPlayer.smali:1003,1048,1112` |
| `GameInstaller` (cache import only) | 14 `Thread.sleep` sites | `installer/GameInstaller.smali` |

The engine's own `nanosleep`/`usleep`/`select` call sites
(`DH2Work-scratch6/sleep_callers.json`) are on engine threads and do not block the
main thread by themselves; they only matter through the JNI entry points listed
above.

---

## 5. Off-device verification actually run

Build (all output under `host/anrfix/logs/`, APK in `host/anrfix/out/`):

```
python build/build_anrfix.py
  1 patch onPause cap    -> changed=true, method body sha256 9ca3b8cd5ef8e2d2…, 500 ms / 80 polls
  2 guest assemble+splice-> classes.dex 0e959205fe2af162… -> 18a53176bc35e3b8…
  3 staged guest         -> game-unsigned.apk e1578cde10bd640a…, 10,243,560 B
  4 outer apk            -> Dungeon-Hunter-2-anrfix-v1114.apk, apksigner v3 verified, zipalign -c ok
  5 check_shipped_apk.py -> checks=29 failures=0 RESULT PASS
                            lib/arm64-v8a/libzbridge.so 5c769c9de128e36f… 3,015,352 B  (unchanged)
                            assets/zb/sysroot/system/lib/libc.so d34addcec84fc69e… 992,808 B (unchanged)
                            assets/dh2/... runtime identity token 2dd2f3eb… (unchanged)
```

Dex-level A/B against the shipped 1113 build
(`python tools/verify_anrfix.py --apk out/Dungeon-Hunter-2-anrfix-v1114.apk
--baseline-dex build/baseline1113-classes.dex`, baseline extracted from
`DH2Work-build\deliverables\Dungeon-Hunter-2-NATIVE-ONE-v2.apk`):

```
  PASS class/method/field inventory identical (no method added, removed or renamed)
  PASS every instruction stream outside onPause is unchanged (addresses/pool indexes excluded)
       -> the only behavioural change in the whole guest dex is DungeonHunter2.onPause
  PASS polls nativeCanInterrupt exactly once
  PASS keeps the original 10 ms Thread.sleep poll
  PASS computes a 500 ms wall-clock deadline (0x1dcd6500 ns)
  PASS exits when now >= deadline (cmp-long + if-ltz)
  PASS has the 80-poll iteration backstop (const/16 80 + if-lt)
  PASS both give-up paths reach the original pause tail
  PASS try/catch still covers only the sleep
  PASS baseline back edge is unconditional: goto 0000
  PASS baseline has no deadline and no iteration bound
RESULT PASS   (checks=9 failures=0)
```

The comparison ignores the dexdump file-address column and dex pool indexes
(`method@…`, `string@…`), because adding `System.nanoTime` grows the sorted string
pool and shifts every later index by one; 109 dump lines differ only by such a
shift. Everything the verifier can compare exactly — the class/method/field
inventory and every instruction mnemonic and operand outside `onPause` — is
identical.

Signature/zip/identity:

```
aapt2 dump badging  -> package: name='local.dh2.fold7' versionCode='1114' versionName='1.0-anrfix-onpause'
                       launchable-activity: com.zettabridge.launcher.Dh2Activity
apksigner verify    -> Verifies; APK Signature Scheme v3: true
zipalign -c -P 16 4 -> exit 0
sha256              -> 8c25753dd93884f775ec93754b02f8cd6b5526750c494e52c770616e13dd0233
```

Preserved by construction (the build is the current good tree; only the guest
`classes.dex` changed):

* **Native geometry by default** — launcher and helper dex are byte-identical to
  1113 (`classes2.dex` and the launcher dex are built from the same untouched
  sources, and `check_shipped_apk.py` re-asserts the native-mode tokens);
* **Xiaomi orientation repair at the engine's first `nativeSetPhone`** — the
  reviewed `onCreate` hook is verified and untouched (patch step 2 prints
  *"onCreate already routes nativeSetPhone through GameTrace"*), and the
  `onSurfaceCreated` one-shot `syncPhoneSize` hook is re-applied exactly as 1113;
* **launcher `finish()` only on tapping "Launch game"** — the launcher Java was
  not touched at all;
* the staging slot `DH2Work-stage\…\research\ZettaBridge\build\launcher\jniLibs\arm64-v8a\libzbridge.so`
  was **read, never written** (build step 5 re-verifies its hash and size);
* the VFP-patched `libc.so` `d34addce…` and the runtime token `2dd2f3eb…` are the
  pinned ones in the shipped APK.

## 6. What is unverified — stated plainly

* **No device and no emulator was run.** "500 ms; the loop can no longer block
  the main thread past the ANR threshold on this path; device unverified."
* Whether the Xiaomi still ANRs from `GameGLSurfaceView.onWindowFocusChanged` →
  `nativePause(1)` (section 4) — untouched.
* Whether the map-loading black screen is fully gone. The cap removes the stall
  that prevents `onResume`/`GLSurfaceView.onResume()`, but the black-clear path in
  `GameRenderer.onDrawFrame` is deliberately left alone.
* Runtime behaviour of a 500 ms budget in the field: if a real pause legitimately
  needed longer than 500 ms of waiting, the tail now runs earlier. The Fold7
  evidence (≈25 polls per pause) suggests it does not, but that is inference from
  a call count, not a measurement.
* Everything else in `README.md` — audio, thermo, multiplayer — is unchanged.

## 7. Reproducing the build

```powershell
$A = "C:\Users\NacWorkstation\Documents\DH2Work-toolchain\host\anrfix"
python "$A\build\build_anrfix.py"                       # patch -> guest -> outer APK -> checks
python "$A\tools\verify_anrfix.py" --apk "$A\out\Dungeon-Hunter-2-anrfix-v1114.apk" `
       --baseline-dex "$A\build\baseline1113-classes.dex"
```

`anrfix\work\` is a private copy of the current good tree
(`host\fitnative\stage\compatibility\work`), `anrfix\guest-work\decoded` a copy of
its decoded guest tree, and `anrfix\build\game-unsigned-pristine.apk` the pinned
guest input. Nothing outside `anrfix\` and this document was written.
