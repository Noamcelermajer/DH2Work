#!/usr/bin/env python3
"""Put the isolated save-job experiment in an unsigned disposable guest APK."""

from __future__ import annotations

import argparse
import hashlib
import io
import zipfile
from pathlib import Path

from patch_sync_jobs import INPUT_SHA256, patch_bytes


BASE_APK_SHA256 = "302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd"
STORM_SHA256 = "334c23b854327ea0315f5353ec89d16fc518963840bedb3753248952f02c7845"
ENGINE_NAME = "lib/armeabi-v7a/libDungeonHunter2.so"
STORM_NAME = "lib/armeabi-v7a/libStormGLOFT.so"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-apk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.base_apk.resolve() == args.output.resolve() or args.output.exists():
        parser.error("output must be a new path distinct from input")
    source = args.base_apk.read_bytes()
    if digest(source) != BASE_APK_SHA256:
        raise ValueError("unexpected unsigned Test 7 guest APK")
    with zipfile.ZipFile(io.BytesIO(source)) as old:
        if old.testzip() is not None:
            raise ValueError("base APK has a bad ZIP entry")
        names = old.namelist()
        if names.count(ENGINE_NAME) != 1 or names.count(STORM_NAME) != 1:
            raise ValueError("required native libraries are missing or duplicated")
        engine = old.read(ENGINE_NAME)
        if digest(engine) != INPUT_SHA256 or digest(old.read(STORM_NAME)) != STORM_SHA256:
            raise ValueError("base native libraries do not match the pinned build")
        patched = patch_bytes(engine)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output, "w") as new:
            for info in old.infolist():
                new.writestr(info, patched if info.filename == ENGINE_NAME else old.read(info))
    with zipfile.ZipFile(args.output) as result:
        if result.testzip() is not None or digest(result.read(ENGINE_NAME)) != digest(patched):
            raise ValueError("output APK failed integrity check")
    print(f"base_apk_sha256={BASE_APK_SHA256}")
    print(f"patched_engine_sha256={digest(patched)}")
    print(f"unsigned_apk_sha256={digest(args.output.read_bytes())}")


if __name__ == "__main__":
    main()
