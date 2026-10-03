# DH2 Fold7 build-pipeline audit

Audit of the build pipeline that turns the original 2011 ARM32 APK
`Dungeon-Hunter-2-HD-v1-0-2.apk` plus the owner cache ZIP into the signed
ARM64 "ZettaBridge" wrapper APK.

* Live tree read: `C:\Users\NacWorkstation\Documents\DH_sc-pr` (the DH_sc **git worktree**).
  Relative paths in this document are relative to that worktree root unless stated otherwise.
* Write scope of this audit: `C:\Users\NacWorkstation\Documents\DH2Work\research\` only.
* Nothing was executed from the pipeline; no file was downloaded. Facts are either read
  from the tree, verified by hashing on-disk artifacts, listed out of ZIPs, or marked
  **UNKNOWN**.

Notation: `path:LINE` cites the file and line the fact came from.

---

## 0. Corrections to the "established facts"

| Claim in the brief | Verdict | Evidence |
| --- | --- | --- |
| DH_sc-pr is a worktree of the LIVE branch `reconstruction/continue-2026-10-02` | **Correct in content, wrong in git state.** The worktree is in **detached HEAD** at `fdfe758`, which happens to be exactly the current tip of `origin/reconstruction/continue-2026-10-02` (0 commits ahead / 0 behind). | `git worktree list` → `DH_sc-pr fdfe758 (detached HEAD)`; `git rev-list --left-right --count fdfe758...origin/reconstruction/continue-2026-10-02` → `0 0` |
| `build_apk.py` needs `research/ZettaBridge` at `7c647a4f1e…` with `zettabridge-dh2.patch` applied | **Correct.** Also needs the *modified* working tree (11 patched upstream files are still dirty after the patch). | `compatibility/work/fold7-build/build_apk.py:12`; verified checkout `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` in `DH2Work-stage/…/research/ZettaBridge`; `git status --porcelain` lists 12 `M` entries |
| `research/ZettaBridge/build/launcher` extracted from `runtime-bundle.zip` | **Correct**, and a fully materialised copy already exists on this machine (all 22 entries match `runtime-manifest.json`). | `STANDALONE-TEST7.md:14-16`; hash check of `DH2Work-stage/…/ZettaBridge/build/launcher` against `compatibility/work/fold7-build/runtime-manifest.json:1-90` — 22/22 OK |
| `downloads/hiddenapibypass-6.1.aar` + `hiddenapibypass.jar` required | **Correct**, and both are already staged on this machine with exactly the pinned sha256. Not inside any `compatibility-work-test*.zip`. | `build_apk.py:113,120,157`; `DH2Work-stage/compatibility/work/downloads/hiddenapibypass-6.1.aar` = 15041 B, sha256 `e3161dd2…324c` |
| `patched/res/drawable/icon.png` required | **Correct** (copied to `res/drawable/dh2_icon.png`). Present in the staged tree. | `build_apk.py:166`; `DH2Work-stage/compatibility/work/patched/res/drawable/icon.png` sha256 `148b3123a7f83afe…` |
| `runtime-bundle.zip` and `dh2-local-test.p12` recovered from `Dungeon-Hunter-2-Fold7-test5-work.zip` | **Correct**, and this audit additionally locates BOTH on this machine, plus the *whole* release asset under the name `test5-work.zip`. | `DH_toolchain/test5-work.zip` (28,744,794 B, 2195 entries, contains `compatibility/work/fold7-build/runtime-bundle.zip` and `compatibility/work/dh2-local-test.p12`) |
| Relocated staging root is `C:\Users\NacWorkstation\Documents\DH2Work-run\compatibility\work\` | **Correct but insufficient.** No single tree is usable: see §5.0. |
| `game-unsigned.apk` is only a "fallback" guest input | **Correct, and it is now dead for every Test ≥ 7.** All Test 7–11 builds must set a `DH2_TEST*_GUEST_APK`; `DH2_CACHE_ZIP` *requires* it. | `build_apk.py:24-29` |

Two additional corrections worth carrying forward:

* `compatibility/work/patched` is **not** a patched-native tree. Its three `.so` files are
  byte-identical to `decoded-original/lib/…`; the only differences from a plain apktool decode
  are `smali/local/dh2/compat/{CompatApplication,PhoneCompat}.smali`, the manifest, `apktool.yml`,
  and the deleted `lib/armeabi/` (all produced by `compatibility/work/patch_compat.py:19-45,61-119`).
* The native patches (`patch_engine.py`, `patch_storm.py`) are applied **by `patch_game.py` into
  `fold7-build/game-tree/`**, not into `patched/`.

---

## 1. End-to-end build graph

### 1.1 Where the scripts think they are

Every script derives its roots from its own location, so it runs correctly from any CWD
as long as the *tree* is right:

| Script | Lines that define the tree |
| --- | --- |
| `build_apk.py` | `ROOT=…/fold7-build` (L10), `WORK=ROOT.parent` = `compatibility/work` (L11), `ZB=WORK/'research/ZettaBridge'` (L12), `SDK=WORK/'android-sdk'` (L13), `BT=SDK/'build-tools/35.0.0'` (L14), `ANDROID=SDK/'platforms/android-35/android.jar'` (L15), `OUT=ROOT/'out'` (L16), `JDK=WORK/'toolchains/jdk-17*'` (L17) |
| `patch_game.py` | `source=ROOT.parent/'patched'` (L7) → **`patched/` must be a sibling of `fold7-build/`**; `target=ROOT/'game-tree'` (L8) |
| `patch_engine.py` | `original=ROOT.parent/'original/lib/armeabi-v7a/libDungeonHunter2.so'` (L11); toolchain at `ROOT.parent/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin'` (L17) |
| `patch_storm.py` | `--original-storm` default `ROOT.parent/'original/lib/armeabi-v7a/libStormGLOFT.so'` (L11); `--original-engine` default same dir (L12); `--toolchain-bin` default `…/linux-x86_64/bin` (L13) |
| `standalone_inputs.py` | pure constants + validators; no paths (L13-23) |

The documented CWD is `compatibility/work` (`BUILDING.md:29,52-60`).

### 1.2 The graph

```
                         ┌──────────────────────────────────────────────┐
                         │ (a) Downloads\Dungeon-Hunter-2-HD-v1-0-2.apk │
                         │     10,269,872 B                             │
                         │     sha256 32c2d027…a4c200  (VERIFIED)       │
                         └───────────────────┬──────────────────────────┘
                                             │  [1] apktool 2.12.1  d
                                             │      (UNKNOWN exact CLI; see §2.4)
                                             ▼
                       compatibility/work/decoded/  (605 files)
                                             │
             ┌───────────────────────────────┴───────────────────────────────┐
             │ [2] patch_compat.py   source=decoded  dest=patched            │
             │     (copy; manifest app name/uses-library/exported;           │
             │      apktool.yml minSdk 21 + targetSdk 24 + vc 103;           │
             │      11 TelephonyManager calls → PhoneCompat; adds            │
             │      CompatApplication.smali + PhoneCompat.smali;             │
             │      rm lib/armeabi; writes compatibility.patch)             │
             ▼                                                               │
   compatibility/work/patched/  (602 files; libs still ORIGINAL)             │
             │                                                               │
             │ [3] patch_game.py   source=patched  target=fold7-build/game-tree
             │     · 31 path-literal rewrites → GamePaths.resolve()  (L12-19)
             │     · CompatApplication.onCreate → GamePaths.initialize (L20-24)
             │     · SUtils.getSDFolder reference-eq → String.equals   (L27-31)
             │     · Musicplayer.initMediaList → MediaQueries.queryPlaylists (L35-48)
             │     · GameGLSurfaceView.setRenderer → GameTrace.wrap    (L50-55)
             │     · Get_PhoneLanguage → GameTrace.phoneLanguage       (L56-59)
             │     · setContentView → GameTrace.installContent         (L60-62)
             │     · GameRenderer.onSurfaceChanged → nativeSetPhone fit (L63-72)
             │     · subprocess patch_storm.py --output game-tree/lib/armeabi-v7a/libStormGLOFT.so   (L73)
             │     · subprocess patch_engine.py --output game-tree/lib/armeabi-v7a/libDungeonHunter2.so (L74)
             ▼                                                               │
   compatibility/work/fold7-build/game-tree/                                 │
      lib/armeabi-v7a/libStormGLOFT.so     sha256 334c23b8…c7845  (= TEST7_STORM)
      lib/armeabi-v7a/libDungeonHunter2.so sha256 45891aad…c93c4  (= TEST7_ENGINE)
      lib/armeabi-v7a/libnativeinterface.so  untouched (180b582c…)
             │                                                               │
             │ [4] java -jar tools/apktool.jar b fold7-build/game-tree \
             │        -o fold7-build/game-unsigned.apk
             │     (Apktool 2.12.1; smali→classes.dex; aapt2 for res;
             │      imports assets, lib, unknown files)
             ▼
   fold7-build/game-unsigned.apk   [ZIP: AndroidManifest.xml, classes.dex,
                                    res/, resources.arsc, assets/, i18n/,
                                    unknown/, lib/armeabi-v7a/{3 libs}]
             │
             │ ── Test 5 gate ──────────────────────────────────────────────
             │  engine must be 45891aad… AND Storm must be
             │  f031bdd99b1963bc60357872ad583addbf9ac2a8446e3b4d32af1bdd7b5fdb2f
             │  (package_emulator_guest.py:37-41)
             ▼
        [Test 5 guest]
             │
             │ [5] package_emulator_guest.py --base-apk <Test5 unsigned guest> \
             │        --patched-storm <game-tree Storm> --output <Test7 guest>
             │     (rewrites ONLY lib/armeabi-v7a/libStormGLOFT.so)
             ▼
   work/emulator-test/test7-guest-unsigned.apk
       sha256 302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd   (= TEST7_GUEST)
             │
             ├─────────────────────────────────────────────────────────────────────┐
             │                                                                     │
             │ [6] DH2_TEST7_GUEST_APK=<Test7 guest>                                │
             │     DH2_CACHE_ZIP=<complete cache>                                   │
             ▼                                                                     ▼
 ┌───────────────────────────────────────────────────────────┐      [6'] alternate guest chain
 │ build_apk.py  (run as `python fold7-build/build_apk.py`)   │      (all made FROM the Test 7 guest)
 │                                                            │       ├ Test 8: thread-exhaustion/patch_sync_jobs.py → engine
 │ A. verify pinned guest + cache hashes          L30-40      │       │          (0x32C534 BNE 0x32CB74→0x32CC34, bytes
 │ B. versionCode/Name from which env var is set   L41-44     │       │          8e01001a→be01001a; patch_sync_jobs.py:18-21)
 │    (test10→14/1.0-test11-language, 9→9/1.0-test9-path,     │       │          then package_sync_trial.py:26-54
 │     8→8/1.0-test8-sync, 7→7/1.0-test7, none→5/1.0-test5)    │       │          → TEST8_GUEST 8168af36…877e20
 │ C. (only if NO guest env var) javac+d8 guest-java →        │       ├ Test 9: repack_test9_guest.py (swap Storm only)
 │    guest-dex, appended as classes2.dex          L56-64,79  │       │          → TEST9_GUEST 310adb71…2733f1
 │ D. assets ← ZB/build/launcher/assets            L65-67      │       └ Test10: repack_test10_guest.py (apktool d -r / b,
 │    assets/dh2/game.apk ← guest_source           L68-73      │                  smali + GameTrace recompile, two DEXes)
 │    assert classes2.dex present exactly once     L74-77      │                  → TEST10_GUEST 57cefd15…ba2b7e
 │    assets/dh2/guest.sha256                      L80        │
 │    assets/dh2/cache.zip + cache.sha256          L81-83      │
 │    /system/lib/arm{,/bootstrap} sysroot aliases L89-98      │
 │    assets/zb-files.txt + zb-version.txt         L99-107     │
 │                                                            │
 │ E. javac 17 (ZettaBridge launcher java + fold7-build/java,  │
 │    -cp android.jar:hiddenapibypass.jar)         L109-114    │
 │    jar cf launcher.jar                          L115-116    │
 │    d8 --release --min-api 29 jar + hiddenapi jar L117-120   │
 │                                                            │
 │ F. manifest surgery (package local.dh2.fold7, vc/vn,        │
 │    uses-sdk 29/35, 4 permissions, label/icon,               │
 │    extractNativeLibs, apache legacy, .→com.zettabridge.     │
 │    launcher, LibraryActivity→Dh2Activity, tools:node=remove)│
 │                                                 L122-143   │
 │ G. licenses/ into assets                        L145-160    │
 │ H. aapt2 compile res/drawable/dh2_icon.png       L162-168   │
 │    aapt2 link --manifest -I android.jar -A assets           │
 │              → out/dh2-unsigned.apk              L169       │
 │    append launcher *.dex and                                │
 │    lib/arm64-v8a/*.so from ZB/build/launcher/jniLibs L170-172│
 │ I. zipalign -f -P 16 4 → out/dh2-aligned.apk     L173-174   │
 │ J. apksigner sign --ks dh2-local-test.p12                   │
 │    --ks-pass pass:dh2-local-test-only            L177       │
 │    apksigner verify --verbose, zipalign -c       L178-179   │
 │ K. build-result.json (apk sha256/bytes, ZB HEAD,            │
 │    guest+cache hashes, device_tested:false)      L180       │
 └───────────────────────────┬───────────────────────────────┘
                             ▼
       ../deliverables/Dungeon-Hunter-2-Android17-test11-language.apk
                     (448,474,954 B; signed, v3, 16 KiB-aligned)
                             │
                             ▼
                  verify_package.py  (test 5 defaults only; see §1.4)
```

### 1.3 Step-by-step table

| # | Script | Working dir expectation | Inputs | Outputs | Env vars |
| --- | --- | --- | --- | --- | --- |
| 1 | `apktool.jar d` (external; 2.12.1) | any | `Dungeon-Hunter-2-HD-v1-0-2.apk` | `compatibility/work/decoded/` | — |
| 2 | `compatibility/work/patch_compat.py` | `compatibility/work` | `decoded/` | `patched/`, `compatibility.patch`, `patch-summary.json` | — |
| 3 | `fold7-build/patch_game.py` | `compatibility/work` | `patched/`, `original/lib/armeabi-v7a/*.so`, NDK r29 (`toolchains/android-ndk-r29`), `engine_path_fix.{S,ld}`, `storm_bias_fix.{S,ld}`, `storm_import_fix.c`, python `capstone`+`pyelftools` | `fold7-build/game-tree/`, `engine-patch-report.json`, `storm-patch-report.json`, `game-patch-report.json` | — |
| 3a | `fold7-build/patch_engine.py` | called by 3 | `original/lib/armeabi-v7a/libDungeonHunter2.so` (must be `36498eb8…f5e80`) | engine `.so` with 20 bytes replaced at VA `0x56dd9c` → `45891aad…c93c4` | `--output` |
| 3b | `fold7-build/patch_storm.py` | called by 3 | `original/…/libStormGLOFT.so` (`be6beaab…91e1`) + companion engine | Storm `.so`: RX segment extended `0xd37e2`→`0xd4363`, stub at `0xd3800`, branch at file offset `0x438e4`, import-hook at `0x371e4` → `334c23b8…c7845` | `--output`, `--toolchain-bin` |
| 4 | `apktool.jar b` (2.12.1) | `compatibility/work` | `fold7-build/game-tree/` | `fold7-build/game-unsigned.apk` | `JAVA_HOME` (JDK 17) |
| 5 | `fold7-build/package_emulator_guest.py` | anywhere | Test 5 unsigned guest + rebuilt Storm | Test 7 unsigned guest `302ae407…027bd` | — |
| 6 | `fold7-build/build_apk.py` | `compatibility/work` (tree-relative; any CWD works) | pinned guest, cache ZIP, ZettaBridge tree + build/launcher, SDK, JDK, hiddenapibypass AAR+JAR, `patched/res/drawable/icon.png` | signed APK in `../deliverables/`, `out/*`, `build-result.json` | `DH2_TEST7/8/9/10_GUEST_APK`, `DH2_CACHE_ZIP`, `DH2_ANDROID_SDK_ROOT`, `DH2_ANDROID_JAR`, `DH2_JDK_ROOT`, `DH2_OUTPUT_APK` |
| 7 | `fold7-build/verify_package.py` | `compatibility/work` | `../deliverables/Dungeon-Hunter-2-Fold7-test5.apk` (hardcoded), `research/ZettaBridge/build/launcher`, `android-sdk/build-tools/35.0.0/aapt`, `native-load-test.log`, the two patch reports | `apk-badging.txt`, `validation.json` | — |

### 1.4 `verify_package.py` is Test-5-only

`verify_package.py:7` hardcodes `deliverables/Dungeon-Hunter-2-Fold7-test5.apk`, and
`:37` asserts `versionCode='5'` / `versionName='1.0-test5'`. It therefore cannot validate a
Test 7–11 deliverable without editing. It also asserts (L29-30) that every `assets/zb/**`
entry in the APK is byte-identical to the extracted `build/launcher` tree.

---

## 2. The guest-APK chain (the important open question)

### 2.1 Verdict

> **The Test 7 guest is reproducible from the original APK plus in-repo material alone.
> No private intermediate is strictly required — `patched/` is fully derivable from
> `decoded/` by `patch_compat.py`, and all three native inputs are hash-pinned and shipped
> in the public snapshot ZIP. Test 8/9/10 guests are derived from Test 7 by in-repo
> scripts.**
>
> The real blockers are **tool presence and tool-exactness**, not missing private bytes:
> `apktool 2.12.1` is not on this machine, and the two engine/Storm `.so` rebuilds need
> `android-ndk-r29` (Linux prebuilt tree) which is not on this machine either.
>
> Because `build_apk.py` validates only the **inner** engine/Storm/dex hashes
> (`standalone_inputs.py:61-77`) and **never compares the assembled guest APK against
> `TEST7_GUEST_SHA256`**, byte-identity of `game-unsigned.apk` is not a hard gate for a
> Test 7 rerun — but it *is* required for the Test 8/9/10/11 chain, whose scripts do
> check the outer SHA-256 (`package_sync_trial.py:33`, `repack_test9_guest.py:20`,
> `repack_test10_guest.py:65`). For those, **apktool 2.12.1 output must be byte-exact.**

### 2.2 What each step is

**(0) Test 5 unsigned guest — the implicit root.** Never named by any script; only by
`package_emulator_guest.py:12` ("Unsigned Test 5 guest APK, before zipalign and signing")
and by the two hardcoded hashes at `package_emulator_guest.py:40-41`. It is the apktool
build of `game-tree` from step [4] above, with the `classes2.dex` helper appended
(`build_apk.py:79`). Two independent copies of this artifact survive on this machine
(§5.0), so it can be re-derived or reused without rebuilding anything.

**(1) Test 7 guest — `package_emulator_guest.py`.** Reads the Test 5 guest, refuses any
APK containing `APK Sig Block 42` (L18-19) or any `META-INF/` entry (L35-36), asserts one
engine and one Storm entry (L33-34), asserts engine == `engine-patch-report.json.output_sha256`
(L37-38) and Storm == `f031bdd99b1963bc60357872ad583addbf9ac2a8446e3b4d32af1bdd7b5fdb2f`
(L40-41), then rewrites **only** the Storm entry (L45) and re-verifies. That one
substitution is the entire Test 6→Test 7 delta in the guest: docs say Test 7 adds the
`qata`→`data` texture fallback inside Storm (`TEST7-EMULATOR.md:5`; the guard source is
`storm_path_repair.h`, wired in by `storm_import_fix.c`).

*Tools:* Python 3 `zipfile`/`hashlib` only. No apktool, no javac/d8 — the DEXes are copied
verbatim from the Test 5 guest.

**(2) Test 8 guest — `compatibility/thread-exhaustion/`.** `patch_sync_jobs.py:72-84`
verifies the input engine is `45891aad…c93c4`, verifies the ARM32 little-endian ELF and the
pinned executable PT_LOAD, verifies the site bytes `8e 01 00 1a` at RVA `0x32C534` decode to
a BNE to `0x32CB74`, then writes `be 01 00 1a` (BNE → `0x32CC34`,
`Savegame::UpdateJobs()` synchronous path). `package_sync_trial.py:33` requires the base APK
to be exactly the Test 7 guest (`302ae407…`), `:42` re-checks engine and Storm, then rewrites
only the engine entry (`:46-48`). Output `8168af36…877e20`, engine `ad33304f…b5f5f`
(`thread-exhaustion/README.md:77-79`). *Tools:* Python only.

**(3) Test 9 guest — `repack_test9_guest.py`.** Verifies input is the pinned Test 8 guest
(L20), verifies the patched Storm is `2489c037…502de` (L21), requires unique ZIP names and
exactly one Storm entry (L29-30), rewrites **only** Storm while preserving every `ZipInfo`
(L31-33), re-verifies the result against `TEST9_GUEST_SHA256` (L34) and re-diffs every other
entry (L35-39). *Tools:* the Storm rebuild needs `patch_storm.py` + NDK r29;
the repack itself is Python only (`STANDALONE-TEST9-PATH.md:25-37`).

**(4) Test 10 guest — `repack_test10_guest.py`.** The only step that *rebuilds* DEXes:

* `apktool d -r <test9 guest>` (L73) — `-r` = no resources, so `resources.arsc`/`res/` are not touched;
* text patch of `smali/com/gameloft/android/GAND/GloftD2SS/DungeonHunter2.smali`
  (`GAME_CLASS`, L22): `.locals 7`→`.locals 8` and the single `nativeSetPhone` call is
  wrapped with `GameTrace.initialPhoneWidth/initialPhoneHeight` (L34-51);
* `apktool b` (L75) → new `classes.dex`, which **must** hash to
  `03c71b7a981b15ac8d28129d9a0abe38d9356d8890c0d4f1374cbac654b9df72` (L76-79);
* `javac -source 8 -target 8` over `guest-java/**` **minus** `GameTrace.java`, plus the
  Test 10 `GameTrace.java` from `test10-guest-java/` (L83-87) → ≥6 classes (L88-90);
* `d8 --min-api 21` → helper dex, which **must** hash to
  `bba5f019caf2a0cc0c6f6c8a8f673dc69ec792355af272b7f5020693820d3efe` (L91-97);
* rewrites exactly `classes.dex` and `classes2.dex` (L101-108), re-verifies the outer
  SHA-256 and every untouched entry (L109-115).

*Tools:* JDK (`java`, `javac`), `apktool.jar`, `android.jar`, build-tools `d8`
(`STANDALONE-TEST10-VIEWPORT.md:33-47`).

**(5) Test 11 "guest" — there is none.** Test 11 reuses the Test 10 guest unchanged
(`STANDALONE-TEST11-LANGUAGE.md:47-50`); the language change lives entirely in the wrapper's
`java/com/zettabridge/launcher/LanguagePreference.java` and `build_apk.py`'s version
bump to 14.

### 2.3 Tool matrix for the guest chain

| Step | apktool | javac | d8 | NDK r29 | uber-apk-signer | Python |
| --- | --- | --- | --- | --- | --- | --- |
| decoded tree (`decoded/`) | **d** | — | — | — | — | — |
| `patched/` (`patch_compat.py`) | — | — | — | — | — | yes (`zipfile`/`ET`/`re`) |
| `game-tree/` (`patch_game.py`) | — | — | — | **yes** | — | yes (`capstone`, `pyelftools`) |
| `game-unsigned.apk` (Test 5) | **b** | — | — | — | — | — |
| Test 7 guest | — | — | — | — | — | yes |
| Test 8 guest | — | — | — | — | — | yes |
| Test 9 guest | — | — | — | **yes** (Storm only) | — | yes |
| Test 10 guest | **d + b** | **yes** | **yes** | — | — | yes |
| wrapper (`build_apk.py`) | — | **yes** | **yes** | — | — | yes (`zipfile`, `xml.etree`) |
| signing | — | — | — | — | **not used** — `build_apk.py:177` uses build-tools `apksigner` | — |

`uber-apk-signer` is pinned in `compatibility/work/tool-manifest.json:7-11` and referenced by
the historical ARM32 recipe (`compatibility/work/history/README.txt`, "REBUILDING THE INCLUDED
PATCHED TREE"), but **the Fold7 pipeline does not use it** — `build_apk.py` signs with
`apksigner` and aligns with `zipalign` (`:173-179`).

### 2.4 What is NOT recorded (honest gaps)

* **UNKNOWN — the exact `apktool d` command line that produced `decoded/`.** No `.log`,
  script or doc in the tree records it. `compatibility/work/history/README.txt:58-59` says
  only "decode the original to `decoded` in a fresh directory". The `b` side is recorded:
  `game-build.log:1` = `Using Apktool 2.12.1 on Dungeon-Hunter-2-HD-v1-0-2.apk with 8 threads`,
  `game-build.log:10` = `Built apk into: dh2-work/fold7-build/game-unsigned.apk`, i.e.
  `java -jar tools/apktool.jar b fold7-build/game-tree -o fold7-build/game-unsigned.apk`
  (`BUILDING.md:56`).
* **UNKNOWN — whether apktool 2.12.1 rebuild is byte-deterministic on a different host.**
  It very probably is *within one apktool version*: `apktool.yml` pins the resolved
  framework, apktool writes fixed entry order and fixed mtimes, and it clears the
  ZIP UTF-8 flag (`general purpose bit 11`) on every entry it writes; the appended
  `classes2.dex` keeps bit 11 and gets mtime `(1980,1,1,0,0,0)`, which is exactly Python
  `zipfile`'s "no date" default. Both surviving local guest APKs show this signature
  (`classes.dex` flag_bits `2056` = 2048|8, `classes2.dex` flag_bits `0`, mtime 1980).
  Not directly verified because apktool 2.12.1 is absent.
* **UNKNOWN — the exact host/dir of the original apktool decode.** `decoded-original/apktool.yml`
  records the framework metadata, but the snapshot does not record the host path.
  `fold7-build/game-build-test4.log:10` shows a Linux scratch path
  (`/workspace/scratch/283fa0994529/dh2-work/…`), which is the historical build host.
* **UNKNOWN — a `repack_test8_guest.py`.** There is none; Test 8 comes from
  `thread-exhaustion/package_sync_trial.py`, which lives outside `fold7-build/`.

---

## 3. Missing-input table

"Present" = verified on this machine and hashed during this audit. Paths are absolute.

| Input | Required path (relative to `compatibility/work`) | Pinned sha256 / size | Status on this machine | Public URL |
| --- | --- | --- | --- | --- |
| Original game APK | any; `DH2_*` inputs are explicit paths | `32c2d027b585a42547311cd95da6a3975fdb3174e663e513d42a7f49d1a4c200`, 10,269,872 B (`LOCAL_AGENT_HANDOFF.md:58`) | **PRESENT** `C:\Users\NacWorkstation\Downloads\Dungeon-Hunter-2-HD-v1-0-2.apk` (hash re-verified) | Not public (proprietary Gameloft payware). Owner-supplied. |
| Owner cache ZIP | `DH2_CACHE_ZIP` | `3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`, 433,189,197 B (`standalone_inputs.py:23`) | **PRESENT** `C:\Users\NacWorkstation\Downloads\Dungeon-Hunter-2-HD-v1-0-2-cache.zip` (hash re-verified) | Not public (game data). |
| `hiddenapibypass-6.1.aar` | `downloads/hiddenapibypass-6.1.aar` | `e3161dd21c97a4540b1698a33f7062aeaa1450008e1e2176070e5380f7a6324c`, 15,041 B (`download-hashes.json:18-21`) | **PRESENT** `…\DH2Work-stage\compatibility\work\downloads\hiddenapibypass-6.1.aar` (hash re-verified) | **`https://repo1.maven.org/maven2/org/lsposed/hiddenapibypass/hiddenapibypass/6.1/hiddenapibypass-6.1.aar`** — coordinates `org.lsposed.hiddenapibypass:hiddenapibypass:6.1`, Maven Central, listed size **15041** matching the pin. (LSPosed GitHub releases have no `6.1` tag — `/releases/tag/6.1` is HTTP 404 — so Maven Central is the real source. Also reachable via GitHub Packages: `https://maven.pkg.github.com/LSPosed/AndroidHiddenApiBypass`.) |
| `hiddenapibypass.jar` | `downloads/hiddenapibypass.jar` | not pinned anywhere; must equal `classes.jar` **inside** the AAR | **PRESENT** `…\DH2Work-stage\compatibility\work\downloads\hiddenapibypass.jar`, 15,685 B, sha256 `6f50c4d202acb8152901716df3a1f94b2181e33aa0d14c9bdb2579cbc21c0832` — **byte-identical to the AAR's `classes.jar`** (verified) | Derive: `python -c "import zipfile;zipfile.ZipFile('hiddenapibypass-6.1.aar').extract('classes.jar','downloads')"` |
| `apktool_2.12.1.jar` | `tools/apktool.jar` | `66cf4524a4a45a7f56567d08b2c9b6ec237bcdd78cee69fd4a59c8a0243aeafa` (`compatibility/work/tool-manifest.json:4`) | **MISSING** — absent from both `DH2Work-run/…/tools` and `DH2Work-stage/…/tools` (empty dirs) | `https://github.com/iBotPeaches/Apktool/releases/download/v2.12.1/apktool_2.12.1.jar` (`tool-manifest.json:5`) |
| `uber-apk-signer-1.3.0.jar` | `tools/uber-apk-signer.jar` | `e1299fd6fcf4da527dd53735b56127e8ea922a321128123b9c32d619bba1d835` (`tool-manifest.json:9`) | **MISSING**, and **not needed** for the Fold7 path (§2.3) | `https://github.com/patrickfav/uber-apk-signer/releases/download/v1.3.0/uber-apk-signer-1.3.0.jar` |
| Android SDK build-tools 35.0.0 | `android-sdk/build-tools/35.0.0` | — | **PRESENT as an installed SDK, wrong path.** `ANDROID_HOME=C:\Android\Sdk`; `build-tools\35.0.0\{aapt,aapt2,d8.bat,apksigner.bat,zipalign.exe}` all present. Must be pointed at with `DH2_ANDROID_SDK_ROOT=C:\Android\Sdk` or copied/junctioned into `<work>\android-sdk`. | `https://dl.google.com/android/repository/build-tools_r35_linux.zip` (pinned `bd3a4966…1d88`, `download-hashes.json:14-17`) |
| `android.jar` (platform 35) | `android-sdk/platforms/android-35/android.jar` | — | **PRESENT** `C:\Android\Sdk\platforms\android-35\android.jar` (27,092,450 B). Point at it with `DH2_ANDROID_JAR`. | `https://dl.google.com/android/repository/platform-35_r02.zip` (pinned `0988caca…c8e0`, `download-hashes.json:6-9`) |
| JDK 17 | `toolchains/jdk-17*` or `DH2_JDK_ROOT` | Temurin 17.0.20.1+1 (`BUILDING.md:36`) | **PRESENT but outside the tree** `C:\Users\NacWorkstation\Documents\DH2-toolchain\jdk\jdk-17.0.20.1+1` (verified `openjdk version "17.0.20.1"`). Implied default (`<work>\toolchains\jdk-17*`) does **not** exist. | `https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.20.1%2B1/OpenJDK17U-jdk_x64_linux_hotspot_17.0.20.1_1.tar.gz` (pinned `3808d1d1…6e08e`, 193,252,603 B, `download-hashes.json:2-5`; the local `DH2-toolchain/temurin17.zip` is the Windows build) |
| NDK r29 (29.0.14206865) | `toolchains/android-ndk-r29` | `android-ndk-r29-linux.zip` = `4abbbcdc842f3d4879206e9695d52709603e52dd68d3c1fff04b3b5e7a308ecf`, 783,549,481 B (`download-hashes.json:30-33`) | **MISSING.** No `android-ndk*` directory anywhere under `C:\`, `D:\`, `C:\Android`, `C:\Users\NacWorkstation\Documents`. Needed **twice**: once as linux-x86_64 (hardcoded by `patch_engine.py:17`), once as a Windows-usable compiler iff you rebuild with `patch_storm.py`'s `.exe` branch (`patch_storm.py:34-41`). | `https://dl.google.com/android/repository/android-ndk-r29-linux.zip` (Google NDK repo; `download-hashes.json` records the hash but not the URL) |
| `runtime-bundle.zip` | `fold7-build/runtime-bundle.zip` | 3,039,699 B, sha256 `a204a9f0613604f2c66177686a0022360729ea0b7f14388a5e06c72a2a44ec0a`; 22 entries, all in `runtime-manifest.json` | **PRESENT** `…\DH2Work-run\compatibility\work\fold7-build\runtime-bundle.zip`; also inside `DH2-toolchain\test5-work.zip` at `compatibility/work/fold7-build/runtime-bundle.zip`; also already **extracted** at `…\DH2Work-stage\…\research\ZettaBridge\build\launcher` (22/22 verified) | Public release asset `https://github.com/Noamcelermajer/DH_sc/releases/download/v1.0.2-fold7-test5/Dungeon-Hunter-2-Fold7-test5-work.zip` |
| `dh2-local-test.p12` | `dh2-local-test.p12` | 3,544 B, sha256 `edb30177b00d0e744719cf89743530ca9b39b6f0a0b8a15a798b0003caa4b4c3`; PKCS12 alias `dh2-local-test`, pass `dh2-local-test-only`, cert SHA-256 `DAA24CD98557703003FB4518D1ED8504DEe071F9620596592819176677772081` | **PRESENT** `…\DH2Work-run\compatibility\work\dh2-local-test.p12` (password + cert fingerprint verified with `keytool`) | Same release asset |
| `fold7-build/patched/` | `compatibility/work/patched/` | 602 files | **PRESENT** `…\DH2Work-stage\compatibility\work\patched\` | In-repo snapshot `compatibility-work-test5.zip` (blob store) / restorable with `unpack_compatibility.py` |
| `fold7-build/original/lib/armeabi-v7a/*` | `compatibility/work/original/lib/armeabi-v7a/` | engine `36498eb8…`, Storm `be6beaab…`, nativeinterface `180b582c…` (`tools/prepare_local_agent.py:17-21`) | **PRESENT** `…\DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\` (all three hashes re-verified) | In-repo snapshot: `compatibility-work-test5.zip`, or `python tools/prepare_local_agent.py <new-dir>` |
| `decoded-original/` | `compatibility/work/decoded-original/` | 605 files | **PRESENT** `…\DH2Work-stage\compatibility\work\decoded-original\` | In-repo snapshot |
| Pinned Test 7 guest | `DH2_TEST7_GUEST_APK` | `302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd` (`standalone_inputs.py:13`) | **MISSING.** No local APK matches it. Closest surviving artifacts are the two **Test 5** guests (§5.0), from which Test 7 is rebuilt by `package_emulator_guest.py`. | Not public. Rebuild per §2 / §6. |
| Pinned Test 10 guest | `DH2_TEST10_GUEST_APK` | `57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e` (`standalone_inputs.py:20`) | **MISSING** — this is the Test 11 blocker. Requires the Test 10 guest build (§2.2 step 4). | Not public. |
| Test 8 guest | `DH2_TEST8_GUEST_APK` | `8168af36b2d82cf6b897da2fe4ec382c816f6498840e3bb61aede2f23c877e20` (`standalone_inputs.py:16`) | **MISSING** (needed only to rebuild Test 9) | Not public. |
| Test 9 guest | `DH2_TEST9_GUEST_APK` | `310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1` (`standalone_inputs.py:18`) | **MISSING** (needed only to rebuild Test 10) | Not public. |
| ZettaBridge checkout | `research/ZettaBridge` @ `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` + `zettabridge-dh2.patch` | patch sha256 **`d6e5d19f5102e0c2705707e94c04867500bbb195301f8facbbc95d7442ccc219`** as checked out with `core.autocrlf=true` (this is the CRLF hash; the pinned LF hash is `7ce58278723b96fb39230e9f44caed4b8654cb5da0a6624bd0381f65ffdc74e8` per `docs/LAYOUT.md:47` — this audit did **not** re-verify the LF form) | **PRESENT** at `…\DH2Work-stage\compatibility\work\research\ZettaBridge`, correct commit + 12 dirty patched files | `https://github.com/ZailoxTT/ZettaBridge.git` (`LOCAL_AGENT_HANDOFF.md:229`) |
| Dynarmic submodule | `research/ZettaBridge/third_party/dynarmic` @ `86458a0bd369d63ba4c2ef812cacbb6c9080c065` | — | **MISSING / EMPTY** (gitlink recorded, working dir empty; matches `git submodule status` output `-86458a0b…`) | Needed only for a native rebuild (§4) |
| Boost 1.83 headers | `toolchains/boost/usr/include` | `libboost-dev.deb` `747b364a7d578babed9efa4c40446be4ba3470ac5b2c9951b0e3bde3d7bb47d6`, 10,746,098 B (`download-hashes.json:34-37`) | **MISSING** | `build_runtime.py:7` requires it; only for a native rebuild |
| Python `capstone==5.0.9`, `pyelftools==0.33` | env | `BUILDING.md:38` | capstone **5.0.7** and pyelftools 0.33 installed under Python **3.13.15** (repo asks for 3.12+ / capstone 5.0.9). Likely fine for `patch_engine.py`'s trivial `Cs.disasm` use (`patch_engine.py:30`), but the pin is not met. | `pip install pyelftools==0.33 capstone==5.0.9` |
| `compatibility-work-test2/3/4/5.zip` | repo root | 16,071,270 / 16,300,470 / 16,376,095 / 16,454,234 B | **PRESENT** in `DH_sc-pr` and `DH_sc`; hashes: `6141b907…1637`, `d6bf579c…4a46`, `b9703e5d…17be`, `9ce51901…2440` (`9ce51901…` matches `tools/prepare_local_agent.py:16`). test2/3/4 are superseded historical snapshots — only test5 is consumed (`unpack_compatibility.py:6-7`, `prepare_local_agent.py:47`). **`hiddenapibypass-6.1.aar` is NOT inside any of them** (§3.1). | In-repo. |

### 3.1 Is `hiddenapibypass-6.1.aar` inside the `compatibility-work-test*.zip` files?

**No.** Each of the four ZIPs carries a `manifest.json` listing every file with path/bytes/sha256
plus a `blobs/<sha256>` dedup store. This audit searched all four manifests for the AAR's blob
sha256 `e3161dd21c97a4540b1698a33f7062aeaa1450008e1e2176070e5380f7a6324c` and for any
`hiddenapi`/`apktool`/`uber-apk` path: **zero hits** in all four.

| ZIP | entries | files in manifest | unique blobs | AAR blob present | `tools/`+`downloads/` paths |
| --- | --- | --- | --- | --- | --- |
| `compatibility-work-test2.zip` | 1063 | 2057 | 1062 | no | 1 (`upstream-modified/tools/build_guest.sh`) |
| `compatibility-work-test3.zip` | 1107 | 2102 | 1106 | no | 1 |
| `compatibility-work-test4.zip` | 1143 | 2141 | 1142 | no | 1 |
| `compatibility-work-test5.zip` | 1195 | 2195 | 1194 | no | 1 |

The AAR is therefore an **external** input. It must be fetched from Maven Central (§3 table) —
or borrowed from the copy already on this machine at
`C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\downloads\`.

---

## 4. Native rebuild path (`build_runtime.py`) — optional

`compatibility/work/fold7-build/build_runtime.py:1-17` requires:

| Requirement | Where it is required | Note |
| --- | --- | --- |
| NDK r29 at `toolchains/android-ndk-r29` | `:6`, `:8` (`NDK`, `NDK_HOST='linux-x86_64'`) | used via `-DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake` |
| Boost 1.83 headers at `toolchains/boost/usr/include` | `:7`, `:14` (`-DBoost_INCLUDE_DIR=…`) | |
| CMake + Ninja, invoked as `python -m cmake` | `:11-15` | `-G Ninja`, `-DANDROID_ABI=arm64-v8a`, `-DANDROID_PLATFORM=android-29`, `-DCMAKE_BUILD_TYPE=Release`, `-DZB_BUILD_TESTS=OFF`, `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (so: `pip install cmake ninja`, or a `cmake` python package) |
| ZettaBridge tree with the DH2 patch applied | `:6` | `repo=WORK/'research/ZettaBridge'` |
| ARM32 sysroot | `:9-10` | auto-populated by copying `repo/build/launcher/assets/zb/sysroot` → `repo/sysroot` when absent |
| Dynarmic submodule `86458a0bd369d63ba4c2ef812cacbb6c9080c065` | `BUILDING.md:42`, `LOCAL_AGENT_HANDOFF.md:266-268` | `build_runtime.py` does not init it; do it by hand |
| Both `third_party/patches` | `STANDALONE-TEST7.md:49-50`, `TEST11-STRICT-REBUILD.md:18` | `dynarmic-0001-thumb32-armv8.patch` (pin `6e16df08…c359b0`) and `dynarmic-0002-asimd-narrowing.patch` (pin `fea886fa…de336`); both files exist under `…/research/ZettaBridge/third_party/patches/` |

Build steps it runs: `cmake configure` (`:11-14`) → `cmake --build … --target zbridge zbproxy zbrun -j 4`
(`:15`) → `tools/build_guest.sh` (`:16`) → `tools/make_launcher_bundle.sh` (`:17`).

**A native rebuild is optional.** The released bundle already contains the prebuilt
ARM64 host library and the ARM32 guest pieces:

```
jniLibs/arm64-v8a/libzbridge.so   3,306,992 B  sha256 25e8da7edbcba1d4cfe828e506a03f1e6dd6e7ffa2948a91eb2ca6f300682d24   (runtime-manifest.json:86-89)
assets/zb/host/libzbproxy.so          7,152 B  sha256 aa570093a188d288edf072cac1a5553c426824884ad416d7ce0ab3f4d71f9205   (:34-37)
assets/zb/guest/zbhost                6,324 B  sha256 0547fb40bab92800e0354c84eba25afdde5c1142a956581ad49a677806f478f7   (:30-33)
assets/zb/guest/lib/*.so           6 libraries
assets/zb/sysroot/system/{bin/linker,lib/*.so}  9 files  (Android 17 GSI ARM32 sysroot)
assets/zb-files.txt, assets/zb-version.txt
```

So `STANDALONE-TEST7.md:16-17` ("this recipe does not rebuild those native binaries") is the
normal path; `build_runtime.py` exists for provenance/repeatability only. Note the **16 KiB**
variant needs its own host rebuild (`TEST11-STRICT-REBUILD.md:20`: `libzbridge.so`
sha256 `dfca14dc9b1646297cf933cff402984ece49a1e8fa24f4ca84e9781a88982d58`, all `PT_LOAD`
align `0x4000`, built with `-DANDROID_PLATFORM=android-35`) plus three extra ZettaBridge
patches applied **in order** before `zettabridge-dh2.patch`.

---

## 5. Reproduction on Windows

### 5.0 There is no single usable tree today — assemble one

| Need | Available at |
| --- | --- |
| `patched/`, `original/lib/`, `decoded-original/`, `downloads/hiddenapibypass.{aar,jar}`, `research/ZettaBridge` (correct commit + `build/launcher` fully extracted, 22/22 manifest-verified) | `C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\` |
| `fold7-build/runtime-bundle.zip` (the one file `DH2Work-stage` lacks) | `C:\Users\NacWorkstation\Documents\DH2Work-run\compatibility\work\fold7-build\runtime-bundle.zip` |
| Current scripts (`repack_test9_guest.py`, `repack_test10_guest.py`, `test10-guest-java/`, `LanguagePreference.java`, current `build_apk.py`) | `C:\Users\NacWorkstation\Documents\DH_sc-pr\compatibility\work\fold7-build\` |
| `tools/apktool.jar` | **absent** — must be fetched |
| `toolchains/android-ndk-r29` | **absent** — only needed to rebuild Test 9/10 Storm, or if you re-run `patch_game.py` |
| JDK 17 | `C:\Users\NacWorkstation\Documents\DH2-toolchain\jdk\jdk-17.0.20.1+1` |
| SDK 35 + `android.jar` | `C:\Android\Sdk` |
| Original APK / cache ZIP | `C:\Users\NacWorkstation\Downloads\Dungeon-Hunter-2-HD-v1-0-2.apk` / `…-cache.zip` |
| **Test 5 unsigned guest (bonus)** — two independent surviving copies, both with the pinned Test 5 engine `45891aad…` and Storm `f031bdd9…`: `…\phone\apk-release\assets\dh2\game.apk` (sha256 `6f1c00a2…`) and `…\phone\apk-installed\assets\dh2\game.apk` (sha256 `071f3bd9…`), each nested in a 44-entry wrapper APK (`tools\DH2-Test6-WORKING.apk`, `phone\installed-{base,after}.apk`). Their 220-entry layout and compression profile match the archived Test 5 guest exactly. | `C:\Users\NacWorkstation\Documents\deepseek-harness\default-workspace\` |

### 5.1 Build the work tree

```powershell
# 0. Define the roots (edit to taste)
$STAGE = 'C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work'
$RUN   = 'C:\Users\NacWorkstation\Documents\DH2Work-run\compatibility\work'
$SRC   = 'C:\Users\NacWorkstation\Documents\DH_sc-pr\compatibility\work'
$WORK  = 'C:\Users\NacWorkstation\Documents\DH2-build\compatibility\work'   # new tree
New-Item -ItemType Directory -Force -Path $WORK | Out-Null

# 1. private/derived inputs + already-materialised ZettaBridge launcher
robocopy $STAGE $WORK /E /XD tools /XF dh2-local-test.p12 | Out-Null   # brings patched, original, decoded-original, downloads, research
# 2. current scripts and current source overlays from the live branch
robocopy "$SRC\fold7-build" "$WORK\fold7-build" /E /XD out game-tree | Out-Null
# 3. the runtime bundle (missing from DH2Work-stage)
Copy-Item "$RUN\fold7-build\runtime-bundle.zip" "$WORK\fold7-build\runtime-bundle.zip" -Force
Copy-Item "$RUN\dh2-local-test.p12" "$WORK\dh2-local-test.p12" -Force

# 4. tools/apktool.jar  (only pip-free download in the pipeline)
Invoke-WebRequest -Uri 'https://github.com/iBotPeaches/Apktool/releases/download/v2.12.1/apktool_2.12.1.jar' `
                  -OutFile "$WORK\tools\apktool.jar"
# expected sha256 66cf4524a4a45a7f56567d08b2c9b6ec237bcdd78cee69fd4a59c8a0243aeafa

# 5. sanity: the launcher tree must still match runtime-manifest.json (22 files)
python - <<'PY'
import hashlib,json,os
work=os.environ['WORK']; base=os.path.join(work,'research','ZettaBridge','build','launcher')
man=json.load(open(os.path.join(work,'fold7-build','runtime-manifest.json')))
bad=[k for k,v in man.items() if hashlib.sha256(open(os.path.join(base,k.replace('/',os.sep)),'rb').read()).hexdigest()!=v['sha256']]
print('launcher mismatches:', bad or 'none')
PY
```

### 5.2 Rebuild the Test 10 guest (the Test 11 prerequisite)

Only needed if you do not keep a pinned Test 10 guest. Requires walking Test 7 → 8 → 9 → 10.

```powershell
$env:DH2_JDK_ROOT = 'C:\Users\NacWorkstation\Documents\DH2-toolchain\jdk\jdk-17.0.20.1+1'
$env:JAVA_HOME    = $env:DH2_JDK_ROOT
$JDK   = $env:DH2_JDK_ROOT
$BT    = 'C:\Android\Sdk\build-tools\35.0.0'
$ANDROIDJAR = 'C:\Android\Sdk\platforms\android-35\android.jar'
$NDK   = "$WORK\toolchains\android-ndk-r29\toolchains\llvm\prebuilt\linux-x86_64\bin"   # only for Test 9

# --- Test 5 guest (skip if you reuse the surviving copy in §5.0)
#   from a tree whose fold7-build/game-tree exists:
cd $WORK
& $JDK\bin\java.exe -jar tools\apktool.jar b fold7-build\game-tree -o fold7-build\game-unsigned.apk
#   NOTE: this emits the *builder's* game-unsigned.apk WITHOUT classes2.dex; test7 packaging
#   requires the Test 5 guest that already carries classes2.dex (see §2.2 step 0).

# --- Test 7 guest
python fold7-build\package_emulator_guest.py `
  --base-apk          <Test5-unsigned-guest-with-classes2.apk> `
  --patched-storm     fold7-build\game-tree\lib\armeabi-v7a\libStormGLOFT.so `
  --output            fold7-build\out\test7-guest-unsigned.apk
# expect sha256 302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd

# --- Test 8 guest
python compatibility\thread-exhaustion\patch_sync_jobs.py `
  --input  fold7-build\game-tree\lib\armeabi-v7a\libDungeonHunter2.so `
  --output fold7-build\out\libDungeonHunter2-sync.so --report fold7-build\out\sync-report.json
# expect engine ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f
python compatibility\thread-exhaustion\package_sync_trial.py `
  --base-apk fold7-build\out\test7-guest-unsigned.apk `
  --output   fold7-build\out\test8-guest-unsigned.apk
# expect sha256 8168af36b2d82cf6b897da2fe4ec382c816f6498840e3bb61aede2f23c877e20

# --- Test 9 guest
python fold7-build\patch_storm.py --original-storm ..\original\lib\armeabi-v7a\libStormGLOFT.so `
  --original-engine ..\original\lib\armeabi-v7a\libDungeonHunter2.so `
  --toolchain-bin $NDK --output fold7-build\out\libStormGLOFT-test9.so
# expect storm 2489c037d75cd2a3c994b7bc349aac767c34f96acf25a79188a72c77dc7502de
python fold7-build\repack_test9_guest.py --test8-guest fold7-build\out\test8-guest-unsigned.apk `
  --patched-storm fold7-build\out\libStormGLOFT-test9.so `
  --output fold7-build\out\test9-guest-unsigned.apk
# expect sha256 310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1

# --- Test 10 guest
python fold7-build\repack_test10_guest.py `
  --test9-guest  fold7-build\out\test9-guest-unsigned.apk `
  --apktool-jar  tools\apktool.jar `
  --android-jar  $ANDROIDJAR `
  --java         $JDK\bin\java.exe `
  --javac        $JDK\bin\javac.exe `
  --d8           $BT\d8.bat `
  --work-dir     fold7-build\out\t10work `
  --output       fold7-build\out\test10-guest-unsigned.apk
# expect sha256 57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e
```

Steps 5.2 must run **sequentially** and stop on the first error (`BUILDING.md:17`), because
each stage hard-hashes the previous stage's output.

### 5.3 The Test 11 wrapper

```powershell
$env:DH2_TEST10_GUEST_APK = "$WORK\fold7-build\out\test10-guest-unsigned.apk"
$env:DH2_CACHE_ZIP        = 'C:\Users\NacWorkstation\Downloads\Dungeon-Hunter-2-HD-v1-0-2-cache.zip'
$env:DH2_ANDROID_SDK_ROOT = 'C:\Android\Sdk'
$env:DH2_ANDROID_JAR      = 'C:\Android\Sdk\platforms\android-35\android.jar'
$env:DH2_JDK_ROOT         = 'C:\Users\NacWorkstation\Documents\DH2-toolchain\jdk\jdk-17.0.20.1+1'
$env:DH2_OUTPUT_APK       = 'C:\Users\NacWorkstation\Documents\DH2-build\deliverables\Dungeon-Hunter-2-Android17-test11-language.apk'
$env:PATH = "$env:DH2_JDK_ROOT\bin;$env:PATH"

cd $WORK
python fold7-build\build_apk.py
```

Notes and gotchas, all verified from source:

* `DH2_JDK_ROOT` is **mandatory on Windows**: without it, `build_apk.py:17` globs
  `<work>\toolchains\jdk-17*`, and that directory does not exist here — a missing JDK makes
  `JDK=next(...)` raise `StopIteration` before any useful error.
* `DH2_ANDROID_SDK_ROOT` names the SDK **root** (the script appends `build-tools/35.0.0`,
  `:14`), so it must be `C:\Android\Sdk`, not the `build-tools` directory.
* Exactly **one** `DH2_TEST*_GUEST_APK` may be set (`build_apk.py:24-25`); precedence is
  test10 → test9 → test8 → test7 (`:26`).
* `DH2_CACHE_ZIP` **requires** a pinned guest (`:27-28`); it is hash-checked at `:38-40` and
  copied atomically by `standalone_inputs.py:80-93`.
* versionCode/versionName are chosen by which env var you set, not by a flag — test10 gives
  **14 / `1.0-test11-language`** (`:41-44`).
* `DH2_OUTPUT_APK` controls the destination (`:175`); default is
  `<compatibility>/deliverables/Dungeon-Hunter-2-Android17-test11-language.apk`.
* `build_apk.py` calls `git rev-parse HEAD` inside `research/ZettaBridge` with
  `-c safe.directory=…` (`:180`), so that checkout must be a git repo.
* Expect the recorded JDK-17 nuisance: `javac` may print `AccessDeniedException` while
  closing `android.jar` and still exit 0 (`STANDALONE-TEST7.md:63-67`).
* To also produce the experimental **16 KiB** package, see
  `compatibility/16k-port/TEST11-STRICT-REBUILD.md:22-27`: copy the prepared work dir to a new
  private dir, `git apply` `test11-strict-page-guard.patch` from the copy's root, drop the
  rebuilt 16 KiB `libzbridge.so` into `<copy>/research/ZettaBridge/build/launcher/jniLibs/arm64-v8a/`,
  then run the same `build_apk.py` with a fresh `DH2_OUTPUT_APK`.
* `verify_package.py` cannot be pointed at the Test 11 APK without editing (§1.4). Verify by
  hand instead: `apksigner verify --verbose`, `zipalign -c -P 16 4`, outer + nested ZIP CRC
  and uniqueness, inner `libDungeonHunter2.so` / `libStormGLOFT.so` / `classes.dex` /
  `classes2.dex` hashes, `assets/dh2/{guest.sha256,cache.sha256,zb-files.txt,zb-version.txt}`.

### 5.4 Reproducibility risks (ranked)

1. **`.so` byte-exactness depends on NDK r29's `ld.lld`/`llvm-objcopy`.** `README.txt:120-121`
   warns "do not guess that a newer toolchain produces identical embedded machine code".
   `patch_engine.py:14` asserts only the *input* hash; `patch_storm.py:16-21` likewise.
   `package_emulator_guest.py:27-28,37-38` and `verify_package.py:18-20` compare the
   *output* to the reports, so a drifted toolchain fails loudly — it does not silently
   produce a wrong guest.
2. **apktool 2.12.1 output must be byte-exact for the Test 8/9/10 chain**, since those scripts
   hash the whole APK. The ZIP-metadata signature observed in the surviving Test 5 guests
   suggests apktool's own output is deterministic for a fixed version.
3. **Windows vs Linux toolchain.** `patch_engine.py:17` hardcodes the NDK's
   `linux-x86_64` prebuilt bin directory and invokes `armv7a-linux-androideabi21-clang`
   (a Linux shell script) at `:20` — that will not execute on Windows. Either run step 3
   under WSL (the recorded build host is Linux x86_64, `BUILDING.md:29`), or use a Windows
   NDK with a directory junction named `linux-x86_64` — but `patch_engine.py` has **no**
   `.exe` fallback, unlike `patch_storm.py:34-41`.
4. `d8`/`javac` versions are not pinned beyond "JDK 17"/build-tools 35, so DEX bytes can differ
   across toolchain patch levels.

---

## 6. Citation index (files read for this audit)

Core pipeline
`compatibility/work/fold7-build/build_apk.py` (1-181) ·
`standalone_inputs.py` (1-93) ·
`patch_game.py` (1-76) ·
`patch_engine.py` (1-33) ·
`patch_storm.py` (1-73) ·
`repack_test9_guest.py` (1-47) ·
`repack_test10_guest.py` (1-123) ·
`package_emulator_guest.py` (1-52) ·
`package_test3.py` / `package_test4.py` / `package_test5.py` ·
`verify_package.py` (1-42) ·
`build_runtime.py` (1-17) ·
`tests/test_standalone_inputs.py`

Patchers and manifests outside `fold7-build`
`compatibility/work/patch_compat.py` (1-137) ·
`compatibility/work/download-tools.py` (1-20) ·
`compatibility/work/tool-manifest.json` (1-12) ·
`compatibility/thread-exhaustion/{patch_sync_jobs.py,package_sync_trial.py,README.md}` ·
`tools/prepare_local_agent.py` (1-116) ·
`unpack_compatibility.py` (1-24) ·
`compatibility/16k-port/TEST11-STRICT-REBUILD.md` (1-29)

Data
`fold7-build/download-hashes.json` (1-38) ·
`fold7-build/runtime-manifest.json` (1-90) ·
`compatibility/REFERENCE_RUNTIME_MANIFEST.json` (1-55) ·
`fold7-build/{engine,storm,game}-patch-report.json` · `fold7-build/build-result.json` ·
`fold7-build/validation.json`

Docs
`compatibility/BUILDING.md` (1-68) ·
`compatibility/work/README.txt` (100-159) ·
`fold7-build/STANDALONE-TEST7.md` (1-98) ·
`STANDALONE-TEST8-SYNC.md` (1-46) ·
`STANDALONE-TEST9-PATH.md` (1-102) ·
`STANDALONE-TEST10-VIEWPORT.md` (1-149) ·
`STANDALONE-TEST11-LANGUAGE.md` (1-106) ·
`fold7-build/TEST7-EMULATOR.md` (1-9) ·
`LOCAL_AGENT_HANDOFF.md` (1-357) ·
`DH2Work/docs/LAYOUT.md` (1-120) ·
`compatibility/work/history/README.txt`

On-disk verification performed in this audit (hashes recomputed, ZIPs listed):
`Downloads\Dungeon-Hunter-2-HD-v1-0-2.apk` ·
`Downloads\Dungeon-Hunter-2-HD-v1-0-2-cache.zip` ·
`DH2-toolchain\{temurin17.zip,jdk\jdk-17.0.20.1+1,test5-work.zip}` ·
`DH2Work-run\…/{runtime-bundle.zip,dh2-local-test.p12}` ·
`DH2Work-stage\…/{patched,original,decoded-original,downloads,research/ZettaBridge,build/launcher}` ·
`DH_sc-pr\compatibility-work-test{2,3,4,5}.zip` ·
`deepseek-harness\default-workspace\phone\{apk-release,apk-installed,nested-installed,nested-release}` ·
`…\tools\DH2-Test6-WORKING.apk` · `C:\Android\Sdk`
