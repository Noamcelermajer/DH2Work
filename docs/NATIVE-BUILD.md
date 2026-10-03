# NATIVE-BUILD.md — rebuilding an arm64-v8a `libzbridge.so` from the pinned ZettaBridge source

Goal: turn the **prebuilt-only** `libzbridge.so` (shipped inside `runtime-bundle.zip`) into a
**repeatable build** so the performance work can modify and re-ship it. This document is the
exact sequence, verified end to end on this machine.

**Result (verified):** the library built by this procedure is **byte-identical** to the one
shipped today — SHA-256 `25e8da7e…682d24`, 3 306 992 B. That is the strongest available proof
that the recipe below reproduces the shipped artefact (see [Verification](#verification)).

---

## 0. Host choice, and why

**Host used: WSL2 Ubuntu 26.04 (Linux x86_64).** This is the intended host, and the reasons are
concrete, not stylistic:

| Requirement | Linux/WSL | Windows |
|---|---|---|
| `tools/build_guest.sh`, `tools/make_launcher_bundle.sh` | run as-is | must be reimplemented; both are POSIX `sh` and hardcode `NDK_HOST=${NDK_HOST:-linux-arm64}` and `$NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin/armv7a-android-…clang` |
| `build_runtime.py` host assumptions | matches | `NDK_HOST='linux-x86_64'` (`build_runtime.py:8`) |
| NDK r29 prebuilt tree on disk | downloaded fresh | only `windows-x86_64` exists; **the Windows NDK cannot be used for this build** |
| `/mnt/c` compile speed | avoided (build tree is in `~/dh2`) | n/a |
| 20 cores / 15 GB RAM | available | available |

The Windows NDK at `C:\Users\NacWorkstation\Documents\DH2-toolchain\ndk\android-ndk-r29`
(host tag `windows-x86_64`) was **not** used. A Linux NDK r29
(`linux-x86_64`) was downloaded separately — a Windows NDK has no Linux prebuilt toolchain
directory, and the `.sh` scripts invoke Linux-style binaries directly.

**WSL state used:** distro `Ubuntu`, running as `root`, kernel `6.18.33.2-microsoft-standard-WSL2`,
20 CPUs, 15 GB RAM, 953 GB free on `/`. Network from WSL works (verified with
`curl https://dl.google.com/…` → HTTP 200).

**Build tree location: `~/dh2` inside the WSL filesystem, not `/mnt/c`.** `/mnt/c` is a 9p/drvfs
mount and is very slow for many-small-file compiles; the Dynarmic + fmt + oaknut + mcl build is
~185 Ninja steps over thousands of files. Inputs are copied *in* and artefacts copied *out*
(§4). Total WSL disk used: ~6.5 GB (NDK zip 747 MiB + extracted NDK + Boost source + tree).

---

## 1. Versions recorded on this machine

| Component | Version / pin | Notes |
|---|---|---|
| Host OS | Ubuntu 26.04 LTS, WSL2 kernel `6.18.33.2-microsoft-standard-WSL2` | 20 cores, 15 GB RAM |
| Android NDK | **r29, `29.0.14206865`** (`Pkg.Revision`), `linux-x86_64` | zip SHA-256 `4abbbcdc842f3d4879206e9695d52709603e52dd68d3c1fff04b3b5e7a308ecf`, 783 549 481 B |
| NDK clang | `clang version 21.0.0` (`Android 13989888, +pgo, +bolt, +lto, +mlgo, based on r563880c`) | `/root/dh2/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/clang` |
| CMake | **4.2.3** (Ubuntu package) | `build_runtime.py` used `python -m cmake`; not needed here |
| Ninja | **1.13.2** | |
| GCC (host) | 15.2.0 (Ubuntu 15.2.0-16ubuntu1) | build-essential, for nothing more than the host-side generator scripts |
| Python (WSL) | 3.14.4 | used by `find_package(Python3)` → `tools/check_zbproxy.py` POST_BUILD check |
| git | 2.53.0 | |
| ZettaBridge | **`7c647a4f1ea150eab7978ab0da28fdf49f3a79de`** | `7c647a4 AGENTS: hand over the Unity bring-up, with what is ruled out` |
| Dynarmic | **`86458a0bd369d63ba4c2ef812cacbb6c9080c065`** | `86458a0b fix compilation with Clang 20`, from `https://github.com/Vita3K/dynarmic` |
| Boost | **1.83.0** headers only (`BOOST_VERSION 108300`) | tarball SHA-256 `c0685b68dd44cc46574cce86c4e17c0f611b15e195be9848dfd0769a0a207628`, 137.9 MiB |
| Dynarmic externals | fmt 10.1.0, oaknut, mcl, zydis — pulled by `git submodule update --init --recursive` | `DYNARMIC_USE_BUNDLED_EXTERNALS=ON` |
| Project patches | `zettabridge-dh2.patch` (13 files), `dynarmic-0001-thumb32-armv8.patch`, `dynarmic-0002-asimd-narrowing.patch` (7 files) | |

Boost is genuinely required: `third_party/dynarmic/CMakeLists.txt` calls
`find_package(Boost …)` and the configure step reports
`Found Boost: …/boost/usr/include (found suitable version "1.83.0", minimum required is "1.57")`.

---

## 2. Exact command sequence

> Run every block in a WSL Ubuntu shell (`wsl -d Ubuntu`). Paths are literal; `$HOME` is `/root`
> here. **Nothing outside `~/dh2` and the two output paths in §4 is modified.**

### Block A — host packages (1 command)

```bash
sudo apt-get update -y && sudo apt-get install -y cmake ninja-build build-essential curl unzip git python3 tar xz-utils ca-certificates file
```

`cmake` and `ninja-build` are the two things a bare WSL Ubuntu image lacks (`git`, `python3`,
`tar` were already present).

### Block B — directory layout (1 command)

```bash
mkdir -p ~/dh2/{toolchains,src,logs}
```

### Block C — Android NDK r29, Linux x86_64 (3 commands, ~750 MB download)

```bash
cd ~/dh2/toolchains
curl -fSL --retry 5 -o android-ndk-r29-linux.zip https://dl.google.com/android/repository/android-ndk-r29-linux.zip
echo "4abbbcdc842f3d4879206e9695d52709603e52dd68d3c1fff04b3b5e7a308ecf  android-ndk-r29-linux.zip" | sha256sum -c -
unzip -q android-ndk-r29-linux.zip
```

The `sha256sum -c` line is the check: it printed `android-ndk-r29-linux.zip: OK`.

### Block D — Boost 1.83 headers only (3 commands, ~138 MB download)

```bash
cd ~/dh2/toolchains
curl -fSL --retry 5 -o boost_1_83_0.tar.gz https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz
mkdir -p boost-src && tar -xzf boost_1_83_0.tar.gz -C boost-src
mkdir -p boost/usr/include && cp -a boost-src/boost_1_83_0/boost boost/usr/include/
```

Only headers are needed, because `Boost_INCLUDE_DIR` is header-only here. This lands at the
`boost/usr/include` layout `build_runtime.py:7` expects. (Boost is header-only for Dynarmic, so
no `bootstrap.sh`/`b2` run is required.)

### Block E — pinned ZettaBridge source + project patch (5 commands)

```bash
cd ~/dh2/src
git clone --no-hardlinks "file:///mnt/c/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge" ZettaBridge
cd ZettaBridge
git checkout -q 7c647a4f1ea150eab7978ab0da28fdf49f3a79de
git apply /mnt/c/Users/NacWorkstation/Documents/DH2Work/patches/zettabridge/zettabridge-dh2.patch
```

Expected result: `git status --porcelain | wc -l` → **13** modified files, `git diff --stat` →
`13 files changed, 147 insertions(+), 12 deletions(-)`.

> **Never build in `DH2Work-stage\…\ZettaBridge`.** That pristine checkout is used to build the
> shipped APK. This recipe works on a clone.

The `git clone` is used (rather than `cp -a`) because it is the only way to get a clean,
non-corrupt `.git` and therefore a *verifiable* patch application (`git apply --check`,
`git diff --stat`). It also carries the `origin` remote
(`https://github.com/ZailoxTT/ZettaBridge.git`) if you prefer to clone from GitHub directly —
the result is the same commit.

### Block F — Dynarmic submodule + its two patches (4 commands, network)

```bash
cd ~/dh2/src/ZettaBridge
git submodule update --init --recursive
cd third_party/dynarmic
git apply /mnt/c/Users/NacWorkstation/Documents/DH2Work/upstream-modified/third_party/patches/dynarmic-0001-thumb32-armv8.patch /mnt/c/Users/NacWorkstation/Documents/DH2Work/upstream-modified/third_party/patches/dynarmic-0002-asimd-narrowing.patch
```

`--recursive` matters: it pulls Dynarmic's own externals (fmt, oaknut, mcl, zydis) that
`DYNARMIC_USE_BUNDLED_EXTERNALS=ON` requires. Verified result:
`git -C third_party/dynarmic status --porcelain | wc -l` → **7** files, and
`git rev-parse HEAD` → `86458a0bd369d63ba4c2ef812cacbb6c9080c065`.

The two patches applied cleanly to the pin with `git apply --check` first.

### Block G — seed the arm32 sysroot (2 commands)

`build/` is **gitignored** (`.gitignore:2`), so a fresh clone has neither
`build/launcher/assets/zb/sysroot` nor the `sysroot/` symlink target that
`build_runtime.py:9-10` and `make_launcher_bundle.sh:36-41` need. It is extracted from the game
APK, not derived from ZettaBridge source, so it must be copied in:

```bash
cd ~/dh2/src/ZettaBridge
cp -a /mnt/c/Users/NacWorkstation/Documents/DH2Work-stage/compatibility/work/research/ZettaBridge/build/launcher/assets/zb/sysroot ./sysroot
```

(`sysroot` is also in `.gitignore:9`, which is why `build_runtime.py` copies it rather than
tracking it.)

### Block H — CMake configure (1 command)

```bash
export NDK="$HOME/dh2/toolchains/android-ndk-r29" NDK_HOST=linux-x86_64
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DZB_BUILD_TESTS=OFF \
  -DBoost_INCLUDE_DIR="$HOME/dh2/toolchains/boost/usr/include" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

Same flags as `build_runtime.py:11-14`. Configure output confirms
`Found Boost … 1.83.0`, `Found Python3: /usr/bin/python3`, and `Version: 10.1.0` (bundled fmt).

### Block I — build the arm64 targets (1 command, the long step)

```bash
cd ~/dh2/src/ZettaBridge
cmake --build build/android-arm64 --target zbridge zbproxy zbrun -j 16
```

**185 Ninja steps, ~70 s wall clock on 20 cores** (Dynarmic dominates). `build_runtime.py:15`
uses `-j 4`; `-j 16` was used here purely for speed and does not change the output.
`zbridge` is the deliverable; `zbproxy` is the arm64 proxy; `zbrun` is the host CLI.

`--target zbridge` triggers the `check_zbproxy.py` POST_BUILD check
(`core/CMakeLists.txt:80-82`) which must pass.

### Block J — guest (arm32) artefacts (1 command)

```bash
cd ~/dh2/src/ZettaBridge
NDK="$HOME/dh2/toolchains/android-ndk-r29" NDK_HOST=linux-x86_64 ./tools/build_guest.sh
```

Ran unmodified: exit 0, last line `guest build ok`. It produced 21 test executables and 20
guest `.so`s under `build/guest/`, including the three required artefacts
`build/guest/zbhost`, `build/guest/lib/libzbcompat.so`, `build/guest/lib/libzbjni.so`. Only
4 deprecation warnings (`mcr p15…` in `cp15_barrier_dynamic.c`).

### Block K — launcher bundle (1 command)

```bash
cd ~/dh2/src/ZettaBridge
NDK_HOST=linux-x86_64 ./tools/make_launcher_bundle.sh
```

Ran unmodified: exit 0, `launcher bundle PASS` (that line is `tools/check_launcher_bundle.py`),
`7.4M build/launcher`.

### Block L — copy the artefacts out (7 commands)

```bash
OUT=/mnt/c/Users/NacWorkstation/Documents/DH2Work-toolchain/native-build/out
mkdir -p "$OUT/android-arm64" "$OUT/guest/lib" "$OUT/launcher-bundle"
cd ~/dh2/src/ZettaBridge
cp build/android-arm64/core/libzbridge.so build/android-arm64/core/libzbproxy.so build/android-arm64/cli/zbrun/zbrun "$OUT/android-arm64/"
cp build/guest/zbhost "$OUT/guest/"
cp build/guest/lib/libzbcompat.so build/guest/lib/libzbjni.so build/guest/lib/libGLESv2.so build/guest/lib/libGLESv1_CM.so build/guest/lib/libEGL.so build/guest/lib/libandroid.so build/guest/lib/libjnigraphics.so "$OUT/guest/lib/"
cp -a build/launcher/. "$OUT/launcher-bundle/"
sha256sum "$OUT/android-arm64/libzbridge.so"
```

Every command in Blocks A–L is a plain shell command; the whole recipe is
**33 commands**: A 1, B 1, C 4, D 4, E 5, F 4, G 2, H 1, I 1, J 1, K 1, L 8. Seventeen of them
(C, D, E, F) are the downloads, clone and patch applications; the ones that actually cost time
are blocks C, D, F and I.

Block H assumes the shell is still in `~/dh2/src/ZettaBridge` from Block G; run
`cd ~/dh2/src/ZettaBridge` first if it is not.

---

## 3. What changed relative to `build_runtime.py`, and why

`build_runtime.py` is the reference driver and was followed except for these deltas:

| # | Change | Why |
|---|---|---|
| 1 | `cmake` instead of `python -m cmake` | `build_runtime.py:11,15` calls `sys.executable -m cmake`, which requires the `cmake` **PyPI wheel**. Ubuntu's `cmake` 4.2.3 package does not install a `cmake` Python module, so `python3 -m cmake` fails with `No module named cmake`. Substituting the `cmake` binary produces identical configure/build behaviour. |
| 2 | `-j 16` instead of `-j 4` | `build_runtime.py:15` hardcodes `-j 4`. It only affects wall-clock. |
| 3 | Linux NDK r29 downloaded fresh instead of reusing the Windows NDK | The Windows NDK at `DH2-toolchain\ndk\android-ndk-r29` has only `prebuilt/windows-x86_64`. `build_runtime.py:8` and `build_guest.sh:20-23` require a Linux prebuilt tree. |
| 4 | `sysroot/` copied from the Windows stage tree | `build_runtime.py:9-10` copies from `repo/build/launcher/assets/zb/sysroot`, which does not exist in a fresh clone because `build/` is gitignored. The stage tree (`DH2Work-stage\…\ZettaBridge\build\launcher\assets\zb\sysroot`) is the same content, extracted from the game APK. |
| 5 | Boost obtained from the upstream tarball, headers only | The `PIPELINE.md` reference is `libboost-dev.deb`. The tarball gives the identical `boost/` header tree at `boost/usr/include`; nothing in the build links a Boost library. |
| 6 | Source came from a `git clone` of the stage tree + `git apply` of the patch, not from a copy of the dirty working tree | Gives a verifiable patch application and a pristine `.git`. The stage tree's working tree itself was **not** touched. |
| 7 | Steps split into named scripts under `DH2Work-toolchain\scripts\` | `01-bootstrap.sh`, `02-clone-and-patch.sh`, `03-build.sh`, `04-collect.sh`, `05-verify.sh`, `06-verify-pyelftools.py`, `07-versions.sh`. Long steps were run as background jobs and polled. |

`build_guest.sh` and `make_launcher_bundle.sh` were **not** modified: `NDK_HOST` was supplied via
the environment (`export NDK_HOST=linux-x86_64`) exactly as `build_runtime.py:8` does.

---

## 4. Outputs

### Primary deliverable

| Path | Size | SHA-256 |
|---|---|---|
| `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\native-build\out\android-arm64\libzbridge.so` | 3 306 992 B | `25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` |

### Full output tree (`DH2Work-toolchain\native-build\out\`)

| Path | Size (B) | SHA-256 |
|---|---|---|
| `android-arm64/libzbridge.so` | 3 306 992 | `25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24` |
| `android-arm64/libzbproxy.so` | 7 152 | `aa570093a188d288edf072cac1a5553c426824884ad416d7ce0ab3f4d71f9205` |
| `android-arm64/zbrun` | 45 501 040 | `78c71fb8a1cc3b0aa84d4e28920fdaef17e13fae4f61b8c718d5e6eae7b2a51e` |
| `guest/zbhost` | 6 324 | `0547fb40bab92800e0354c84eba25afdde5c1142a956581ad49a677806f478f7` |
| `guest/lib/libzbcompat.so` | 3 764 | `43d99a49d681cd63110fb1e921e0281756a31c332b3eeddc193f490abb23925b` |
| `guest/lib/libzbjni.so` | 35 744 | `9bd1f0d25f8dd9e84a59ee44193d1b936e223baaac1b521e05f1ae4f7132f9e4` |
| `guest/lib/libGLESv2.so` | 24 668 | `f3534fbe0019ab387d643edf03ec1a9323e19768aa6e4850c39979c1c037ad5b` |
| `guest/lib/libGLESv1_CM.so` | 13 164 | `d6b5ef042fb0814d5634428b64e4941720030d29165ad4eb8d0addf48ee5b9cf` |
| `guest/lib/libEGL.so` | 5 432 | `63411c309d471448a8eece533ec7771f18b29a07fa07cefce7b5b34de729829b` |
| `guest/lib/libandroid.so` | 21 200 | `dee509a55055fa1a134b5800ce407c241c5862e81157cda9943e82be9e7c103f` |
| `guest/lib/libjnigraphics.so` | 1 588 | `e78baaf39c3e8c8a2389a2a00a2e7d470ad54c71246fc6c44e67954ccca322c5` |
| `launcher-bundle/…` | — | full `build/launcher` tree (20 files), including `jniLibs/arm64-v8a/libzbridge.so` and `assets/zb-version.txt` = `9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06` |

`zbrun` is large because it is an unstripped host CLI and is not part of the launcher bundle.

### Cross-check against the shipped prebuilt

Three independent copies hash identically:

```
shipped (runtime-bundle.zip → jniLibs/arm64-v8a/libzbridge.so)  25E8DA7E…682D24
stage prebuilt (ZettaBridge/build/launcher/jniLibs/arm64-v8a/)  25E8DA7E…682D24
rebuilt by this procedure                                       25E8DA7E…682D24
```

The bundle's `assets/zb-version.txt` also matches the value this build recomputes
(`9ae1e3d9e4275803f94376788c50d05067b785c5d98578f1b90cfdf01174da06`), i.e. every one of the
20 bundled files is reproduced bit-for-bit, not just `libzbridge.so`.

---

## 5. Verification

### 5a. Valid aarch64 shared object, expected JNI entry points — `python` + `pyelftools`

`DH2Work-toolchain\scripts\06-verify-pyelftools.py` (run with the Windows `python`, which has
`pyelftools`) — **RESULT: ALL CHECKS PASSED**:

```
size    : 3306992 bytes
sha256  : 25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24
class   : 64-bit (ELF64)
endian  : little
type    : ET_DYN
machine : EM_AARCH64
  OK   ELF64
  OK   e_machine == EM_AARCH64
  OK   e_type == ET_DYN (shared object)
  OK   .dynsym present
  OK   SONAME == libzbridge.so
```

`.dynsym` defines exactly **13** `STT_FUNC` symbols, and the 12 required JNI entry points are all
present (`--gc-sections --strip-all --exclude-libs,ALL` keeps the export surface deliberately
minimal):

```
Java_com_zettabridge_core_ZBridge_activatePlugin
Java_com_zettabridge_core_ZBridge_addPathAlias
Java_com_zettabridge_core_ZBridge_fixGuestLibrary
Java_com_zettabridge_core_ZBridge_lastLoadError
Java_com_zettabridge_core_ZBridge_loadError
Java_com_zettabridge_core_ZBridge_onNativeActivityCreated
Java_com_zettabridge_core_ZBridge_onProxyLoaded
Java_com_zettabridge_core_ZBridge_runExecutable
Java_com_zettabridge_core_ZBridge_runtimeReport
Java_com_zettabridge_core_ZBridge_setGlDiagnostics
Java_com_zettabridge_core_ZBridge_setPreciseFaults
Java_com_zettabridge_core_ZBridge_setReportFile
zb_run_executable
```

`DT_NEEDED`: `libGLESv2.so`, `libGLESv3.so`, `libEGL.so`, `libandroid.so`, `liblog.so`,
`libm.so`, `libdl.so`, `libc.so`. `DT_SONAME`: `libzbridge.so`.

The guest artefacts are correctly **32-bit `EM_ARM`** (`zbhost`, `libzbcompat.so`, `libzbjni.so`)
— they are the ARM32 code ZettaBridge's Dynarmic AArch32→AArch64 JIT executes, so arm32 is the
expected and required class.

### 5b. Same, with the NDK's `llvm-readelf`

`NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -h` gives
`Class: ELF64`, `Type: DYN (Shared object file)`, `Machine: AArch64`, `Flags: 0x0`, and
`--dyn-syms` reproduces the 13 exported functions above. Script:
`DH2Work-toolchain\scripts\05-verify.sh`.

### 5c. Proof the project patch is actually in the binary

`zettabridge-dh2.patch` adds `core/src/runtime_report.cpp`,
`core/include/zb/runtime_report.h`, `cli/zbrun/main.cpp --report FILE`,
`ZBridge.runtimeReport` / `ZBridge.setReportFile`, and the guest-log/exit reporting in
`core/src/signals.cpp`. Those additions leave string literals that are **absent from the
unpatched upstream** `runtime_report.cpp`, and all of them are found in the built `.rodata`:

| Marker string | Occurrences in `libzbridge.so` |
|---|---|
| `zettabridge-runtime-report 1` | 1 |
| `unimplemented-host-calls` | 1 |
| `unimplemented-distinct` | 1 |
| `registered-natives` | 1 |
| `native-calls:` | 1 |
| `guest-log: ` | 1 |
| `setReportFile` | 1 |
| `runtimeReport` | 1 |

Strongest form of the same evidence: the built library is **byte-identical** to the shipped
`runtime-bundle.zip` copy, so the patch content is not merely "some marker is present" but
exactly the artefact in production.

### 5d. Patch application was verified, not assumed

* `zettabridge-dh2.patch`: `git apply --check` passed, then applied → `13 files changed,
  147 insertions(+), 12 deletions(-)` at commit `7c647a4f…`.
* `dynarmic-0001`, `dynarmic-0002`: `git apply --check` passed on
  `86458a0b…`, then applied → 7 files:
  `a32_address_space.cpp`, `A32/decoder/asimd.inc`, `A32/decoder/thumb32.inc`,
  `A32/translate/impl/a32_crc32.cpp`, `a32_translate_impl.h`, `asimd_three_regs.cpp`,
  `thumb32_load_store_dual.cpp`.
* `tools/check_zbproxy.py` (POST_BUILD) and `tools/check_launcher_bundle.py` both passed.

---

## 6. Limitations — what was **not** verified

Read this before treating the build as ready to ship.

1. **The rebuilt library has not been run on a device.** Nothing here executes `libzbridge.so`.
   There is no Android device or emulator in this environment, and the ZettaBridge host test
   suite was deliberately not built (`-DZB_BUILD_TESTS=OFF`, matching `build_runtime.py:13`).
   No `ctest` was run. Runtime behaviour is therefore *inferred*, not measured.
2. **The output is a pristine rebuild, not a modified one.** No functional change was made, so
   this build cannot by itself demonstrate that *edited* ZettaBridge code produces a working
   library. It proves the toolchain, the source state, and the patch set are correct and
   complete — which is the enabling step, not the optimisation.
3. **"Byte-identical" is a strong signal but not a runtime guarantee.** Bit-for-bit equality
   with the shipped `libzbridge.so` means the artefact is the same one the team has presumably
   already run. It does not prove it *still* runs, and it means this rebuild carries any latent
   bug the shipped library has. It also means **the build is deterministic for this exact
   toolchain**: a different NDK (e.g. r28 or r30), CMake, or clang patch level will almost
   certainly produce a different hash, and byte-identity should not be expected from a
   differently-provisioned machine.
4. **Drop-in for the shipped library: yes, evidenced both ways.** Same SONAME
   (`libzbridge.so`), same 13-symbol export surface, same `DT_NEEDED` set, same size, and
   identical bytes — the arm64 `jniLibs/arm64-v8a/libzbridge.so` slot in the launcher bundle is
   the same file. The other four bundle members needed by the guest side were also extracted
   from `runtime-bundle.zip` and hashed directly against this build, and all four match:

   | File | `runtime-bundle.zip` vs rebuilt |
   |---|---|
   | `guest/zbhost` | `0547fb40bab92800…` = `0547fb40bab92800…` **MATCH** |
   | `guest/lib/libzbcompat.so` | `43d99a49d681cd63…` = `43d99a49d681cd63…` **MATCH** |
   | `guest/lib/libzbjni.so` | `9bd1f0d25f8dd9e8…` = `9bd1f0d25f8dd9e8…` **MATCH** |
   | `host/libzbproxy.so` | `aa570093a188d288…` = `aa570093a188d288…` **MATCH** |

   (The same conclusion follows independently from the `zb-version.txt` agreement, which is a
   hash-of-hashes over all 20 bundled files.) So this is drop-in at the byte level for the whole
   runtime payload. Still *not* verified: that the shipped library was itself correct, or that
   any device behaviour is unchanged — see points 1 and 3.
5. **`build_guest.sh` and `make_launcher_bundle.sh` fully succeeded — nothing was worked around.**
   Both exited 0 unmodified, with `NDK_HOST=linux-x86_64` supplied by the environment, exactly
   as `build_runtime.py:8` intends. `build_guest.sh` did require the arm32 `sysroot/` to be
   seeded first (Block G), but that is a prerequisite `build_runtime.py:9-10` also performs, not
   a workaround. The only real workarounds were the `cmake`-binary substitution and the Linux
   NDK download (deltas 1 and 3 in §3).
6. **The arm32 sysroot is not reproducible from source.** It comes from the game APK. This
   recipe copies it from `DH2Work-stage\…\ZettaBridge\build\launcher\assets\zb\sysroot`
   (`system/bin/linker`, `libc.so`, `libc++.so`, …). Anyone rebuilding must have that directory
   (or re-extract it with `tools/extract_sysroot.sh`). Its provenance is outside this build.
7. **`build/launcher/assets/zb/sysroot` must exist for the cross-check in §4 to be meaningful.**
   It is the only non-source input.
8. **Boost was confirmed *used* but is not linked.** `find_package(Boost)` succeeded and Dynarmic's
   CMake requires it; no Boost library is linked into `libzbridge.so`. Do not expect a Boost
   version bump to change the binary.
9. **Environment specifics.** `root` user, WSL2, `~/dh2` on `/dev/sdd` (ext4, 953 GB free). A
   non-root user needs `sudo` for Block A and a different `$HOME`. If you build under `/mnt/c`
   instead, expect the 185-step build to be dramatically slower.
10. **Windows was never attempted for the actual build**, so the claim "Windows would need the
    `.sh` scripts reimplemented" is a code-reading conclusion, supported by
    `build_guest.sh:19-23` and `build_runtime.py:8`, not by a failed Windows attempt.
    The Windows NDK at `DH2-toolchain\ndk\android-ndk-r29` was left untouched.

---

## 7. Reproduce-it script set

All scripts live in `C:\Users\NacWorkstation\Documents\DH2Work-toolchain\scripts\` and are the
executable form of §§2–5:

| Script | Does |
|---|---|
| `01-bootstrap.sh` | Blocks A–D (apt, NDK r29 Linux + sha256 check, Boost headers) |
| `02-clone-and-patch.sh` | Blocks E–F (clone, dh2 patch, dynarmic submodule + 2 patches, with `--check` gates) |
| `03-build.sh` | Blocks G–K (sysroot seed, cmake configure, build, `build_guest.sh`, `make_launcher_bundle.sh`) |
| `04-collect.sh` | Block L (copy out + size/sha256 manifest) |
| `05-verify.sh` | `llvm-readelf` ELF/JNI/marker verification |
| `06-verify-pyelftools.py` | `pyelftools` verification (Windows `python`) |
| `07-versions.sh` | Prints every version in §1 and the source-tree state |

To rebuild from scratch: delete `~/dh2`, then run `01` → `02` → `03` → `04` in that order, each
as a background job, then `05`/`06` to verify.
