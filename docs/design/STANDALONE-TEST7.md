# Private standalone Test 7 build and Android 17 emulator result

This source recipe builds a locally signed ARM64 wrapper containing the hash-pinned
Test 7 guest APK and the owner's complete cache ZIP. The game and cache bytes are
private build inputs and are not part of the Git source tree. See `RIGHTS.md` for
provenance and publication limits.

## Inputs and build

Restore the compatibility work tree with `tools/prepare_local_agent.py` as described
in `LOCAL_AGENT_HANDOFF.md`. In that restored tree, check out public ZettaBridge
commit `7c647a4f1ea150eab7978ab0da28fdf49f3a79de` as
`research/ZettaBridge`, apply `fold7-build/zettabridge-dh2.patch`, and extract the
verified `fold7-build/runtime-bundle.zip` to
`research/ZettaBridge/build/launcher`. The bundle provides the prebuilt ARM64
translator, ARM32 guest helper and pinned Android 17 GSI sysroot; this recipe does
not rebuild those native binaries. Supply Android SDK build tools 35.0.0, an
Android platform `android.jar`, JDK 17, and HiddenApiBypass 6.1's AAR and
`classes.jar` in `downloads/` as the existing `build_apk.py` expects.

Set `DH2_TEST7_GUEST_APK` to the **unsigned** Test 7 guest, SHA-256
`302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd`.
Set `DH2_CACHE_ZIP` to the complete owner cache ZIP, SHA-256
`3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679`.
Set `DH2_ANDROID_SDK_ROOT`, `DH2_ANDROID_JAR`, `DH2_JDK_ROOT` to the installed
tool locations. `DH2_OUTPUT_APK` may select the local signed output path. Then
run `python fold7-build/build_apk.py` from the restored `compatibility/work`
directory. `standalone_inputs.py` checks the complete guest and cache hashes,
including the Test 7 engine and Storm libraries, before any APK is signed.

The wrapper imports the bundled cache into its app-owned external files directory
on first run when no prior cache files exist. A SHA-256 asset identifies the
nested guest APK for safe reimport after an update. After successful bundled
import, a completion marker records the pinned cache ZIP hash. The marker
attests to that import, not continuing integrity of every extracted file.
Preexisting files are preserved; a cache with only the `data/` and `shaders.pak`
sentinels is labeled "completeness not verified." Partial preexisting caches
are not overwritten automatically. The manually selected cache ZIP route
remains available and its result is also labeled unverified.

The build preserves the Test 7 guest's single `classes2.dex`. It adds ARM32
sysroot aliases at `/system/lib/arm` and `/system/lib/arm/bootstrap` inside the
runtime asset bundle. Those paths match the Android 17 x86_64 emulator's
`/system/etc/ld.config.arm.txt`; without them its guest `zbhost` reports
`libdl.so not found`. The runtime asset list and version digest are regenerated
so an existing installation receives the added files on upgrade.

The ZettaBridge source patch also passes the installed wrapper's ARM64
`nativeLibraryDir` followed by the proxy directory as the translated plugin
class loader's native search path. Android's native loader uses that path to
select a bridged namespace on an x86_64 emulator. With a null path or only the
app-private proxy path, it selects x86_64 and rejects the proxy as
`EM_AARCH64 instead of EM_X86_64`. The ARM64 host path selects a normal ARM64
namespace on an ARM64 device.

## Validation, 2026-10-02

An earlier local signed APK built from this recipe had SHA-256
`26a5460eae8f3adb43ad8e6886a77afd08d024bfed26bbe3120324ab8269ee65`
and size 448,470,789 bytes. `apksigner verify` passed with v3 signing, `zipalign
-c` passed, every ZIP entry name is unique, and all ZIP CRCs passed. The two
ARM32 alias locations, guest APK, and cache ZIP each appear exactly once.
The focused synthetic cache-copy/hash tests pass. JDK 17's `javac` on this
Windows host returned exit code 0 and emitted 40 class files, but also printed
an `AccessDeniedException` while closing the Android 37 `android.jar` ZIP. D8
produced launcher DEX and the installed app ran; treat that diagnostic as a
toolchain issue, not as a clean compiler transcript.

On an official Android 17/API 37.0 x86_64 emulator with 4096-byte pages, the
APK was installed fresh with `--abi arm64-v8a` after uninstalling prior test
data. The adb daemon was returned to non-root mode before install. First launch
reported `Ready. Cache available` with 6834 files (including generated options)
and about 663 MiB in app-owned external storage. After **Launch Game**, logcat
confirmed bridged proxy loading, guest JNI runtime startup, 32 engine natives
bound, Storm loading, and engine shader processing. Screenshots showed the
opening cinematic and then the Dungeon Hunter title with a loading spinner.
The app did not reach the menu in this run. At about 272 seconds process uptime,
Android's x86_64 native bridge aborted while generating ARM64 translated code:

```text
berberis: frameworks/libs/binary_translation/base/mmap_posix.cc:128:
  CHECK failed: 0xffffffffffffffff != 0xffffffffffffffff
libndk_translation.so: berberis::MmapImplOrDie ->
  ExecRegionAnonymousFactory::Create -> CodePool::Add
```

The last sampled process status before the abort had 34 threads, VmSize
38,726,092 KiB and VmRSS 950,040 KiB. Logcat contained no
`pthread_create failed`, `Not Created`, or `Memory exhausted` entry for this
run. The abort points to the emulator's ARM64-to-x86_64 translation layer;
the log does not expose the failing `mmap` size, requested address or errno.
The original game, wrapper, guest runtime and emulator translation layer each
remain possible contributors to address-space pressure. No menu or gameplay
was validated on Android 17 by this Test 7 run.

On the 16,384-byte-page Android 17 emulator, bundled cache setup also completed,
but the wrapper's explicit page-size guard blocked game launch. The 16 KiB
translator port remains separate. No Fold7 device test was run.
