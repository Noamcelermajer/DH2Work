# Notices and licence layering

DH2Work is a private compatibility effort, not an official release and not a
distribution of the game. **No licence is granted here for the original game,
its assets, or the owner-supplied cache.** Read [`RIGHTS.md`](../RIGHTS.md)
first; it is the authoritative statement of provenance and publication limits
for this repository.

## What each part is under

| Component | Where it lives here | Terms |
| --- | --- | --- |
| Original Dungeon Hunter 2 HD v1.0.2 code, assets and cache | Private inputs only — **not present in this repository** | Retain their original Gameloft rights. No licence granted. The APK and cache must never be committed (see `.gitignore`) |
| ZettaBridge (host, launcher, core, CLI, tooling) | `upstream-modified/` (readable patched copies), `patches/zettabridge/`, `patches/16k-port/` | Source-available under **two cumulative** licences: PolyForm Noncommercial 1.0.0 **and** PolyForm Perimeter 1.0.1. Both texts are in [`UPSTREAM-LICENSE.txt`](UPSTREAM-LICENSE.txt) and, identically, in [`upstream-modified/LICENSE`](../upstream-modified/LICENSE) (identical SHA-256 `b569976e333118445d7d04aabaf248e526a1b07e8cf2277190f5cc12204f0611`). Selling it, shipping it in a product, preinstalling it in a ROM, or building a competing product requires a separate written licence from ZailoxTT |
| Dynarmic (A32/A64 JIT) | Downstream submodule, not vendored here | 0BSD. The pinned commit and the two local patches are documented in [`upstream-modified/third_party/README.md`](../upstream-modified/third_party/README.md) |
| Khronos OpenGL / EGL registries (`gl.xml`, `egl.xml`) and the stubs generated from them | Downstream, not vendored here | Apache-2.0; the headers are in the files themselves |
| AOSP ARM32 bionic sysroot (`libc.so`, `libdl.so`, `libm.so`, linker, ...) used as the **guest** runtime | Private runtime bundle, not in this repository | AOSP upstream licences apply to those Android system files |
| Android SDK / NDK / build-tools / JDK / emulator images / qemu | Private toolchains, not in this repository | Their own upstream terms |
| Lua 5.1.4 interpreter (`port/lua-runtime/vendor/`) | **Not imported** (reconstruction track) | MIT, per that module's `COPYRIGHT`; the game's own Lua scripts keep their cache provenance and are not covered by it |
| DH2Work's own scripts, patches, tests and documentation | `build/`, `patches/`, `tests/`, `docs/`, `app/` | Authored for this project; no repository-wide open-source licence is asserted over them either |

## Why there is no blanket licence file here

Every DH2Work file either (a) was authored for this compatibility project, (b) is
a patched copy of upstream ZettaBridge source, or (c) is binary-derived evidence
about the original game. Categories (a) and (b) cannot be relicensed by a single
top-level `LICENSE`, and category (c) carries the original rights. The two
upstream licence texts that *do* apply to vendored material are kept verbatim in
`notices/UPSTREAM-LICENSE.txt` and `upstream-modified/LICENSE`.

## Third-party patch notices

* `upstream-modified/third_party/patches/dynarmic-0001-thumb32-armv8.patch`
* `upstream-modified/third_party/patches/dynarmic-0002-asimd-narrowing.patch`

are applied to the pinned Dynarmic submodule per
`upstream-modified/third_party/README.md`, which records the pinned commit
`86458a0bd369d63ba4c2ef812cacbb6c9080c065` and what each patch adds
(AArch32 ARMv8 load-acquire/store-release and `CRC32`, and the four Advanced
SIMD high-narrowing add/subtract instructions). Their SHA-256 values do not match
the pins quoted in `patches/16k-port/TEST11-STRICT-REBUILD.md`, which refer to
the copies shipped inside the ZettaBridge checkout — see section 13 of
[`docs/STATUS.md`](../docs/STATUS.md).
