# Tier 2 constraint: no ZettaBridge and no Dynarmic patch

**Owner decision.** The goal for this phase is native-speed execution of hot guest code
**without forking either ZettaBridge or Dynarmic.** Anything that modifies translator source
is out of scope. Changing *inputs the app already ships* — the engine binary, the guest
libraries, the runtime bundle — is in scope.

## What this rules out

`docs/NATIVE-HOOK-MECHANISM.md` concluded that the only real seam in this codebase is
Dynarmic's `TranslateCallbacks::PreCodeReadHook` / `PreCodeTranslationHook`, which receives a
live `A32::IREmitter&` and can emit `IR::Opcode::CallHostFunction` (a real ARM64 `bl`). That
remains the correct answer to "how would you hook a guest address to a host function" — but it
requires **overriding those callbacks inside ZettaBridge**, which is precisely the fork now
excluded. **That recommendation is superseded for this phase.** Its other findings still
stand: no first-class address-substitution API exists in Dynarmic, `UserCallbacks` is
notification-only, and ZettaBridge's only existing guest→host channel is SVC with
`kHostCallBase 0x5A0000`.

## What is still available

Everything operates on **guest bytes** or on **files we ship**, so nothing here touches
translator source:

| Route | What changes | Why it can work |
| --- | --- | --- |
| **Guest sysroot `libc.so`** | the `__aeabi_*` helper bodies we ship in the runtime bundle | The engine's float is software float *in the guest*: 28,209 call sites into helpers whose bodies are 124–264 translated instructions each. VFP-based bodies would be a handful of instructions the JIT translates 1:1 |
| **Engine binary** | hot functions rewritten as ARM32 using real VFP/NEON | The JIT translates a VFP instruction instead of a call into soft float. Needs bit-exact differential verification, which the project's Unicorn oracle can provide |
| **Engine binary** | the frame limiter | The engine caps near 60 FPS; removing it is an engine patch, which is exactly how this project already ships reviewed byte patches |
| **Nothing (accepted)** | — | If no guest-only route reaches the target, the honest options are to accept a fork later or to move to static recompilation, which needs no translator at all |

## Why this is a coherent strategy rather than a compromise

The measured split is **82.7% JIT'd guest code, 15.1% translator, 0.16% GL driver**. Removing
translator overhead entirely would therefore address at most 15% — while rewriting the guest
code addresses the 82.7%. The excluded fork was the *cheap* route to a narrow slice; the
guest-only routes are more work but aim at the larger slice. The constraint is not
performance-hostile.

## Workstreams opened for this phase

1. `docs/SOFTFLOAT-VFP-REPLACEMENT.md` — replacing the guest libc `__aeabi_*` bodies with VFP,
   with bit-exactness argued case by case and, where possible, tested.
2. `docs/NO-FORK-NATIVE-OPTIONS.md` — an exhaustive survey of what can run host-native with a
   stock translator, and a plain verdict if the answer is "nothing".
3. `docs/GUEST-REWRITE-PIPELINE.md` — the tooling to rewrite one guest function, differentially
   verify it against the original under Unicorn, and emit a hash-pinned patch.
4. `docs/FRAME-LIMITER.md` — locating the ~60 FPS cap and determining whether it is vsync or
   engine-internal, and whether it is removable without an engine patch.

A route that cannot be verified is not a route. Every one of these must be validated against
the pristine binary, not argued.
