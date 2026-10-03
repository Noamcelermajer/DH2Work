> Test 2 update: the Fold7 report confirms 4096-byte pages and native library/JNI startup. A MediaStore query crash was identified and repaired; the repaired APK awaits a device retest. See [TEST2](work/fold7-build/TEST2.md). The initial test 1 evidence below is retained historically.

# Preservation scope

The repository snapshot contains all authored code, targeted upstream modifications, reconstruction exports, inventories, native prototype code and output, recorded tests, build logs, notices, historical Android patches, and final generated smali/manifests from this compatibility investigation. `FILE-MANIFEST.json` inventories every snapshot file with size and SHA-256.

It includes the tested runtime bundle, exact native inputs and deliberately non-production signing fixture already distributed in the source kit. `work/decoded-original` preserves the original decoded APK resources/smali, while `work/patched` is the earlier compatibility baseline. `work/fold7-build` holds the new work.

The release holds the installable APK, original source-kit snapshot, checksum file and complete-work snapshot. The repository's automatic GitHub source archives at the initial test tag reflect the commit at the time the APK was published; use the named complete-work asset or current main branch for this later documentation/source import.

Large reproducible external downloads (SDK, NDK, JDK, GSI, QEMU, Boost), third-party git checkout caches, duplicate unsigned APKs/object files, and Python bytecode caches are not copied into the snapshot. Their versions/hashes, relevant patches and build instructions preserve the useful reproducibility information. The complete cache ZIP is not present in this workspace and is not included. No actual phone report or gameplay result exists yet.

The independent source-reconstruction work described elsewhere in the repository has its own evidence and scope. This directory does not claim to have performed that work or inspected its cache inputs. Cross-reference the repository's recovery reports when they become available.

Game material retains its original provenance and rights. ZettaBridge has cumulative source-available PolyForm Noncommercial/Perimeter terms; Dynarmic and the remaining dependencies retain their own notices. Preserve `work/notices`, `work/fold7-build/UPSTREAM-LICENSE.txt` and the upstream notices. This snapshot grants no blanket license to the original game.
