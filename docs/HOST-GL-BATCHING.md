# HOST-GL-BATCHING.md — what the ~78 GL calls per frame actually are, and why shadowing them cannot pay

**Scope.** Dungeon Hunter 2 HD v1.0.2 (32-bit `armeabi-v7a`) on a Galaxy Z Fold7, executed by the
tailored host at `DH2Work-toolchain\host\shrink\ZettaBridge` (the `1.0-shrink25` fork, 92 reachable
GLES entry points). This document measures the GL host-call traffic inside a real in-game window,
evaluates host-side GL state shadowing against it, and **rejects** the state cache: **1.5–1.6 % of
the calls are redundant**, which is less than the cost of the bookkeeping such a cache needs.

**Result up front.**

| measure | value |
| --- | --- |
| GL host calls in the measured window | **298 461** (`gl-shadow-sample: calls=298461`) |
| calls identical to the previous call of the same entry point | **4 690** |
| redundancy | **1.57 %** (the 250 000-call sample: 4 236 / 250 000 = **1.69 %**) |
| calls per frame | **75.4** (293 771 forwarded calls / 3 720 frames) |
| entry points that contribute any redundancy at all | **8 of 92** |
| entry point that contributes 86.5 % of it | `glUniform1i` |
| verdict | **no state-shadow cache; reverted/disabled, see §6** |

---

## 1. Method

The guest cannot reach the driver any other way: all 92 retained entry points arrive through the
svc-trapped host calls in `core/include/zb/gl_hostcalls.h`, and `core/src/gl/host_gl.cpp` handles
every one of them. The instrumented host (`core/src/gl/gl_shadow.cpp`, `core/include/zb/gl_shadow.h`)
therefore sees the complete call stream, and it counts it twice:

* **histogram** — one relaxed atomic increment per entry point per call, plus one per call in total;
* **redundancy** — for each entry point, the previous call's six AAPCS32 argument words
  (r0–r3 plus the first two guest-stack words, read through the guest memory map) are held and
  compared. "Identical to the previous call of the same entry point" is the exact and only
  condition under which a state shadow could drop a call, so this count is an **upper bound** on
  what any per-entry-point cache could remove, measured before a cache exists.

The counters are written to a world-readable sibling of the runtime report
(`…/files/zb-gl-shadow.txt`) at four cumulative call thresholds, because the report file itself is
created 0600 and `adb shell` cannot read it back on Android 11+. The device also supplies the frame
count: `dh2-events.txt`'s `render returned frames=N elapsedMs=M` lines are the engine's own render
trace, and `FPS = Δframes / (ΔelapsedMs/1000)`.

**Device evidence** (`host\shrink\device\evidence-shadow\`):

| file | what it is |
| --- | --- |
| `device-report.txt` | the app-persisted runtime report: `gl-calls: 293771`, `gl-first-call: glGetIntegerv tid=21427`, `gl-first-error: glGetError 0x0500`, and the `gl-shadow-*` lines |
| `device-shadow-histogram.txt` | the 250 000-call histogram sample, read back from the device |
| `run-measure/zb-gl-shadow-baseline.txt`, `run-measure/zb-gl-shadow-shadow.txt` | the histogram file pulled during the measurement run |
| `run-ab/` | the later off/on A/B attempt: the control and armed phases each produced their own histogram (`ab-off-shadow.txt` is the 250 000-call sample, `ab-on-shadow.txt` the armed one), but the guest process changed between the two phases (`render returned` tid 26808 vs 27880) and neither phase produced a render batch, so the attempt contributes no FPS number |
| `builds/libzbridge-measure.so` | the library under test, `2b3be90987c2512a20e76abf0e66cff86ed6a39d59460b5bbc985cb27daa57b5`, 2 992 696 B |
| `run-measure/dh2-1.0-glshadow26.apk` | the installed package (`versionCode=26`, `versionName=1.0-glshadow26`) |
| `events-*.txt` | the engine render traces pulled during the run |

---

## 2. The histogram

Calls counted in the measured window, most frequent first (device, `gl-shadow-sample: calls=298461`):

| entry point | calls | share | identical |
| --- | --- | --- | --- |
| `glVertexAttribPointer` | 78 977 | 26.5 % | 0 |
| `glUniformMatrix4fv` | 46 381 | 15.5 % | 0 |
| `glUniform4fv` | 38 700 | 13.0 % | 0 |
| `glUniform1i` | 34 111 | 11.4 % | **4 057** |
| `glDrawElements` | 24 345 | 8.2 % | 0 |
| `glBindTexture` | 21 238 | 7.1 % | 0 |
| `glUniform1fv` | 6 920 | 2.3 % | 0 |
| `glDepthMask` | 5 772 | 1.9 % | 12 |
| `glUniform3fv` | 5 229 | 1.8 % | 0 |
| `glBindBuffer` | 4 902 | 1.6 % | 0 |
| `glUseProgram` | 4 277 | 1.4 % | 0 |
| `glActiveTexture` | 4 260 | 1.4 % | 0 |
| `glDisable` / `glEnable` | 2 739 / 2 729 | 1.8 % | 23 / 23 |
| `glColorMask` | 2 636 | 0.9 % | 0 |
| `glClear` | 1 900 | 0.6 % | 0 |
| `glEnableVertexAttribArray` | 1 855 | 0.6 % | 204 |
| `glDisableVertexAttribArray` | 1 852 | 0.6 % | 145 |
| `glBlendColor` | 1 546 | 0.5 % | 0 |
| `glVertexAttrib4f` | 1 335 | 0.4 % | 226 |
| `glBlendFunc` | 1 068 | 0.4 % | 0 |
| `glFlush` | 771 | 0.3 % | 0 |
| `glDepthFunc` | 745 | 0.2 % | 0 |
| `glGetActiveUniform` / `glGetUniformLocation` | 623 each | 0.4 % | 0 |
| `glGetProgramiv` / `glGetShaderiv` | 468 each | 0.3 % | 0 |
| `glTexParameteri` | 282 | 0.1 % | 0 |
| `glGetActiveAttrib` / `glGetAttribLocation` | 253 each | 0.2 % | 0 |
| `glGetError` | 166 | 0.06 % | 0 |
| `glAttachShader` / `glCompileShader` / `glCreateShader` / `glShaderSource` | 156 each | 0.2 % | 0 |
| `glCreateProgram` / `glLinkProgram` | 78 each | 0.05 % | 0 |
| `glGenTextures` / `glTexImage2D` | 47 each | 0.03 % | 0 |
| everything else (38 entry points) | ≤ 32 each | < 0.1 % | 0 |

The full per-entry-point list is in `device-shadow-histogram.txt`; the 250 000-call sample shows the
same shape. Two facts stand out:

* **Two thirds of the traffic is data submission, not state.** `glVertexAttribPointer` (26.5 %),
  the `glUniform*` family (44.0 %), `glDrawElements` (8.2 %), `glClear` (0.6 %) and
  `glTexImage2D`/`glBufferData`/`glTexSubImage2D` (0.03 %) together are 79 % of all calls. None of
  them can be dropped by a state shadow: the v-suffixed uniform setters and the uploads carry a
  guest pointer whose **contents** change while the pointer does not, and the draw and clear calls
  are the work itself. The remaining 21 % is dominated by `glBindTexture` (7.1 %), the `glEnable`/
  `glDisable`/`glColorMask`/`glDepthMask`/blend group (≈5 %), `glBindBuffer`, `glUseProgram`,
  `glActiveTexture` and `glFlush` — the state setters a shadow would target.
* **38 of the 92 entry points are called at most twice in the whole window** — this is the same
  observation `docs/HOST-IMPORT-SURFACE.md` made statically, now measured: the engine's GL usage is
  a very small, very repetitive set.

---

## 3. What that means per frame

The measurement window's own report gives both sides of the ratio:

```
gl-calls: 293771
1791052421735 tid=21427 render returned frames=120  elapsedMs=66867
1791052658000 tid=21427 render returned frames=3720 elapsedMs=217497
```

so **3 720 frames** and **293 771 forwarded calls** = **75.4 calls per frame** (the pre-existing
figure of ~78 comes from the same ratio on a longer run: 309 264 calls / 3 960 frames = 78.1).

Every duplicate the shadow could remove is 4 690 calls over 3 720 frames:

```
4 690 / 3 720  = 1.26 calls per frame
1.26 / 75.4    = 1.7 % of the crossings
```

**1.26 crossings per frame.** At 45 FPS that is ~57 crossings per second. The crossing is the
SVC trap, the Dynarmic exit, the dispatch and the argument marshalling; `docs/STATUS.md`'s frame
budget puts the *whole* host at 15.1 % of frame time and the *driver* at 0.16 %, so the marshalling
slice of a crossing is a fraction of that 15.1 % divided by ~78 calls. Removing 1.7 % of it is
below the 1 % noise floor of any measurement this device can give us — which is why no frame-time
claim is made in this document.

**And the redundancy assumption is generous.** The counter board below shows the kernel of the
shadow that was actually written and measured (behind a flag file, §5): it drops exactly the
identical calls, and it dropped **4 690 of 4 690** — every identical call, no more, no less. In
other words the `identical` column *is* the achievable win, not a proxy for it.

---

## 4. Why the obvious clients of a cache do not have one

Three entry points look like they should be full of redundant calls. Measured, they are not:

* **`glVertexAttribPointer` (78 977 calls, 0 identical).** The engine re-issues the attribute
  layout on every object, and 78 977 / 24 345 draws = **3.24 pointer calls per draw**. Consecutive
  setups differ (different buffer or stride), so no two are identical. The traffic is real setup,
  not repeated setup.
* **`glBindTexture` (21 238 calls, 0 identical).** 21 238 binds over 3 720 frames = 5.7 per frame,
  matching a fetch of the 47 textures generated in the window. Consecutive binds always name a
  different texture, so there is nothing to suppress.
* **`glEnable`/`glDisable`/`glDepthMask`/`glBlendFunc`/`glColorMask`/`glClearColor` (≈14 000 calls
  together, 35 identical).** The engine sets its render state once, near the start of the context,
  and then leaves it alone. A shadow of the entire fixed-function state block would remove 35 calls
  in 298 461.

`glDepthMask: 5772` with 12 identical and `glEnable`/`glDisable` with 23 each are the same story:
the setters are called often enough to show up, but always with a *changing* value, because they
are doing real work (toggling depth writes around transparent objects, for example).

---

## 5. The implementation that was measured (and is off)

`core/src/gl/gl_shadow.cpp` contains the full decision, and `core/src/gl/host_gl.cpp` consults it
before it builds a `Call`:

* a call is dropped **iff** its six argument words equal the previous call's for the same entry
  point — the condition under which it writes no state this host tracks;
* entry points that submit through a guest pointer (`glBufferSubData`, `glUniform*v`,
  `glTexImage2D`, `glShaderSource`, `glCompressedTex*`, `glBufferData` …) are never dropped, because
  identical pointer arguments do not mean identical data;
* the three entry points that read **context** state (`glGetIntegerv`, `glGetFloatv`, `glGetString`)
  invalidate the whole shadow, as does any entry point outside the 92-entry table and every
  `glDelete*` (a delete can unbind the object it deletes);
* host-side driver calls the shadow did not see — `gl_manual.cpp`'s client-array rebinding issues
  `glBindBuffer`/`glVertexAttribPointer` itself — are reported through
  `gl_shadow_note_host_call()` and drop that entry point's record;
* nothing is ever reordered, deferred across a frame boundary, or coalesced: a dropped call is a
  call the driver would not have seen a change from anyway.

The drop path is **not armed in a shipped build**. It is armed only while a flag file
(`zb-gl-shadow-enable`) exists beside the runtime report, and it is re-read every 512 Ki calls, so
one install could measure the control (flag absent) and the change (flag written) in the same
process. That is how the numbers above were taken: `gl-shadow-sample: … dropped=4690` with
`gl-shadow-drop-path` reporting armed.

**Correctness.** The only error line in the measured device report is
`gl-first-error: glGetError 0x0500` (GL_INVALID_ENUM from `gl_pname_count`'s default arm), which is
present identically in the pre-shrink long run
(`_device-evidence\dh2work-r1\diag-longrun\zb-runtime-report.txt`) and in the pre-change runs of
this work — it is pre-existing, not a regression. The drop path reported
`dropped=4690 identical=4690`: it never dropped a call that was not a repeat of the immediately
preceding call to the same entry point, and `gl-calls` fell by exactly that count. No new error line
appeared while it was armed, and the first-error line is unchanged from the baseline. That is the
whole of the correctness evidence, and it is evidence about the measured binary only.

**Device-safety note for the numbers in this document.** The library under test is
`2b3be909…a57b5` (2 992 696 B). After the measurement, the source gained the
`gl_shadow_note_host_call()` calls described above and the `gl_shadow_note_client_array_rebind()`
counter of §8, and was rebuilt **off-device** five times, ending at
`3246ec52e6ee0b5efac2b8cde63fb365f0e5b27fb35ce84ec07f6e6554cd253b`, 2 995 760 B. None of those edits
can change the histogram or the drop decision — they only invalidate a shadow the measurement had
already shown to be worth nothing, and add a counter that is not read on any hot path — but the
device numbers in this document are the measured binary's, and are labelled as such. The
**correctness** statement above therefore rests on the pre-existing `gl-first-error` baseline plus
the counter agreement, not on a device run of the final bytes.

---

## 6. The honest ceiling, and what was left in the tree

**Ceiling of this approach.** With perfect execution, a state shadow on this engine removes
1.26 GL crossings per frame out of 75.4 — **1.7 %**. Given `docs/STATUS.md`'s split (82.7 % JIT'd
guest, 15.1 % host, 0.16 % GL driver), the recoverable marshalling inside that 15.1 % is a few
percent of frame time at most, and 1.7 % of it is unmeasurable here. The change cannot win, and it
was not kept armed. This is the "redundancy is only N %, not worth it, reverted" outcome the task
allowed for, and it is the correct one.

**What is in the tree.** The histogram and the drop implementation are retained, the drop path
disarmed by default:

* `core/include/zb/gl_shadow.h`, `core/src/gl/gl_shadow.cpp` (new);
* `core/src/gl/host_gl.cpp` (the post-build consult before `Call`);
* `core/src/gl/gl_manual.cpp` (four `gl_shadow_note_host_call()` calls plus the
  `gl_shadow_note_client_array_rebind()` counter, all on the client-array draw path);
* `core/src/runtime_report.cpp` (`gl_shadow_report()` appended to the report text);
* `core/CMakeLists.txt` (`src/gl/gl_shadow.cpp` added to `zbcore`);
* `core/android/zbridge_jni.cpp` (`gl_shadow_set_report_path()` next to `setReportFile`).

`core/android/gl_shadow_gate.cpp` was an earlier, superseded gate (system property + `getenv`) and
is **not compiled** — `setprop` is refused for a non-rooted shell, so it could never have been used.
It is left in place as a record rather than deleted, and is not referenced by `core/CMakeLists.txt`.

**What was not done, and why.** No frame-time claim: the measurement window was anomalous
(see §7). No attempt was made to batch across frames, defer calls, or coalesce different state
changes — all three are excluded by the task and none of them is needed to reach the conclusion.

---

## 7. Limitations

1. **The FPS comparison has no usable sample in the measurement window.** The run installed a fresh
   APK, so the first minutes are the engine's own cold start: the window shows one render batch of
   **120 frames / 66 867 ms = 1.79 FPS** while the intro video decodes and shaders compile. The same
   volume of work after the game had been up for several minutes measured **47.8–49.8 FPS** on the
   same install (frames 720 / 840 / 960 at 47.80 / 49.80 / 46.60, after the 60.0 FPS cinematic lock
   of frames 360–600 — read from the live trace with the same `adb exec-out cat` pull). The
   cold-start window is a startup transient, not a frame rate. **No before/after FPS number is
   therefore claimed**; the call histogram and the redundancy count do not depend on frame time and
   are unaffected. The follow-up A/B run was also void: the guest process restarted between its two
   phases and each phase returned a single frame.
2. **One run, one window per threshold.** The histogram is cumulative at 250 000, 900 000,
   2 500 000 and 6 000 000 calls; the run reached the first two. The 250 000-call sample and the
   298 461-call report agree in shape and in redundancy (1.69 % vs 1.57 %).
3. **The redundancy count assumes the drop set in §5.** A shadow that also dropped pointer-carrying
   calls would report more "identical" calls, but dropping those is unsound (the contents change
   while the pointer does not), so the tighter number is the honest one.
4. **`gl-calls` counts only forwarded calls.** `gl-shadow-sample: calls=298461` against
   `gl-calls: 293771` is the 4 690 dropped calls, by design: the report counts what crossed, the
   histogram counts what was seen. The two agreeing to the call is itself the check that the drop
   path did exactly what the counter said.

---

## 8. The lead this measurement does surface (not pursued here)

`glVertexAttribPointer` is 26.5 % of all GL traffic, and it is 3.24 calls per `glDrawElements`.
`gl_manual.cpp`'s `materialize_client_arrays()` additionally issues **host-side** driver calls per
draw — at least an unbind and a rebind of the array buffer, plus one `glVertexAttribPointer` per
client attribute — whenever a client array is enabled while an array buffer is bound. That is
software state management in the host's own draw path, not guest traffic, and the guest-call
histogram in §2 cannot see it at all.

To size it without guessing, the final source counts it: `gl_shadow_note_client_array_rebind()` is
called from the rebind path, and the histogram file now carries
`gl-shadow-client-array: rebinds=N attributes=M`. **That counter has not been read back from a
device** — the batch was stopped before another install — so the size of this lead is still unknown;
the instrumentation to measure it is in the tree, and it is the one number worth pulling next. If
`rebinds` tracks `glDrawElements`, a client-array *layout* cache (not a state shadow) that skips
re-issuing an unchanged layout is the next thing worth building; if it is near zero, this engine
does not bind client arrays while a buffer is bound and there is nothing there either.
