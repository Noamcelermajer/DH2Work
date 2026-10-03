# Is ZettaBridge suitable for DH2 on this Fold7?

Test 3 update: its abort stack maps to an original-engine filename bounds check. Original ARM32 caller probes reproduce the same stack with a directory path. Test 4 supplies a narrow file-open guard; the phone retest remains required. See [TEST4.md](TEST4.md). This evidence favors repairing that boundary before replacing the CPU translator.

The present evidence supports continuing with a **locally audited, game-specific compatibility build**, not accepting upstream claims as proof of DH2 support. It does not yet support calling this a stable port.

| Boundary | Evidence in this project | Unresolved risk / next discriminator |
| --- | --- | --- |
| ARM32 execution on ARM64 | Original libraries load; independent helper probes match; phone runs into game loading | Untested instruction sequences, races and long-session performance |
| Bionic/linker | Two Storm accesses to obsolete private soinfo fields were independently repaired and probed | Modern allocator/stdio/thread behavior can expose old assumptions; capture the abort message |
| JNI/ART | Phone registers 36 natives and executes render, accelerometer, touch and audio initialization callbacks | Successful dispatch does not prove every signature, pointer, exception or callback lifetime is correct |
| Graphics | 18,078 GL calls and a current context; user sees the game/cinematic | GL_INVALID_ENUM, texture formats, shader rewrites, context lifetime and viewport mapping remain unverified |
| Assets and scripts | Numerous cache opens succeed; last recorded open is character-properties metadata | Matching asset revision, full reads, binary-schema consistency and heap corruption need the fatal trace |
| Audio/video | Cinematic completes; native audio initialization runs | Sound playback, mixer callbacks and return-to-game behavior are not fully tested |
| Display and input | Original Storm patch assumes a 1280x720 logical size | Folded/unfolded aspect ratio and touch geometry need actual-size logs and screenshots |
| Persistence/lifecycle | Imported cache survives normal updates | Save/reload, suspend/resume, interruptions and fold transitions need acceptance runs |

## Why not replace the translator immediately?

A different CPU translator would still need the Android JNI, graphics, filesystem, signal and lifecycle boundaries. A failure caused by a game assertion, mismatched cache, stale renderer state or the supplied Storm patch would follow those components. The next useful decision is based on a concrete fatal message and module offset, not a repository's compatibility list.

The most productive targeted paths are:

1. If the trace lands in a game parser/assertion, reconstruct that routine from its ARM instructions, validate its expected data and reproduce it with a minimal input before editing it.
2. If modern bionic reports allocator, mutex or stdio misuse, identify the original assumption and fix the narrow boundary. Do not disable allocator checks to mask corruption.
3. If the fault is a JNI/GLES marshaling error, correct that bridge path and add an isolated original-ARM32 versus host test for it.
4. If an instruction or concurrency defect is reproducible independently of Android, compare the small guest program against an independent emulator/reference and repair or replace that component.
5. If rendering is otherwise sound, replace unnecessary Storm graphics/resize hooks gradually with reviewed implementations and compare output/input on the phone. Removing all of Storm at once would also remove existing shader/texture compatibility behavior.

## Manual native reconstruction

Reconstructing selected functions in C/C++ or ARM64 is viable and already exists in this project, including the separate engine-math checkpoint in `port/engine-math`. A complete engine port also requires classes and memory layouts, loaders, scripting, graphics, audio, threading and JNI integration. A handful of correctly translated functions is not a replacement for that work. Expanding the port around an identified failing boundary is more testable than rewriting unrelated assembly while this crash is opaque.

## What would justify calling it stable?

First reproduce and fix this exact fairy loading-screen failure. Then pass repeated cold/warm starts, the intro-to-gameplay transition, character selection, movement/combat, area changes, menus/inventory, saving and reloading, audio, pause/resume, screen lock, and folded/unfolded input alignment. Run at least a 30-minute session with memory/frame timing recorded, followed by a saved-game reload. Those gates must run on SM-F966B; host probes alone cannot establish them. Test other Android devices separately rather than generalizing from one phone.
