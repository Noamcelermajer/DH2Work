# r6-vfp: first build carrying a native VFP rewrite (measured on device)

**Build:** versionCode 20, `1.0-work6-vfp`, APK sha256
`3809642cf33454b38911eedf412e5c0aeb336d5c1390eb97e578eafaf7deff9c`.
Engine sha256 `84c880553295d111828400f0b6ed8ca6b238854489346d4b5a07a17996c78bd5`
(was `ad33304f...`). Exactly 164 bytes changed, all at RVA `0x60dd34`, the verified
site of `glitch::core::quaternion::operator*(quaternion const&) const`.

**What changed.** The rewrite pipeline's verified VFP replacement of that one function:
448 B / 112 ARM instructions / 28 soft-float calls becomes 164 B / 41 instructions, 40 of
them VFP. Verified before shipping: 2,319 comparisons, 0 mismatches, covering r0-r12, sp,
lr, flags, all 32 VFP registers and all written memory; the negative control (an
algebraically identical reassociation, ~1.6 percent bit-different) is rejected 14/519.

Both pre-existing patches were confirmed intact after applying it: the r4 thread patch at
`0x32c534` = `be01001a` and the Test 5 path fix region. The applied bytes were checked
against the spec's `original_bytes_hex` before writing, so the site is proven correct
rather than assumed.

## Measured result: stable, no crash, and within noise

45 consecutive 120-frame batches over roughly 15 minutes:

| stat | r6-vfp | r4-sync baseline |
|---|---|---|
| median batch rate | ~25 FPS | 26.77 FPS |
| max | 54.2 FPS | 58.88 FPS |
| min (excluding startup) | 20.9 FPS | 4.05 FPS |

The first two batches were slow (2.9 and 5.9 FPS) because the plugin cache was being
re-extracted after the upgrade - the guest wrote 19.6 MB in that window. That is startup
cost, not a regression, and it is why a single early sample is not evidence.

No crash: the crash buffer is empty, and the guest is healthy at VmSize 23.28 GB, VmRSS
656 MB, CPU 75 percent.

## The honest reading

**A single rewritten function is not measurable.** r6 is statistically indistinguishable
from r4 on frame rate. That is exactly what the cost model predicted, and it is why the
static 77x translate-cost reduction on this one call path must not be quoted as a frame
win. What this run does establish is different and still valuable:

1. The pipeline's output **ships and runs stably in a real build** - no crash, no hang,
   no memory regression, both prior patches intact.
2. The **bit-exactness claim survives contact with the device** for this function: the
   game renders normally and the math-driven visuals are not corrupted.
3. Applying a guest byte patch to our engine, on top of two existing patches, is
   **mechanically safe** when done by offset with the original bytes verified first.

## What would actually move the needle

The full `__aeabi_*` libc patch: 5,564 bytes across 28 helper bodies, removing 88.9
percent of the weighted helper-body cost, with a central estimate of about 2.0 ms/frame
(parametric: `p x 21.25 ms x 0.889`, where `p` is the unmeasured share of frame
instructions inside those bodies).

Two blockers before shipping that one:
1. **The FPSCR.FZ/DN verdict** - whether the guest can ever enable flush-to-zero, which
   would break denormal equivalence. Audit in progress.
2. **`assets/zb-version.txt` must be bumped.** `RuntimeBundle.java:19-31` compares that
   token against a `.bundle-version` marker and skips extraction when it matches, so
   patching `libc.so` without bumping it means every upgraded install silently keeps the
   old library. Nothing validates libc.so's contents.