#!/usr/bin/env python3
"""Replace only Storm in the pinned private Test 8 guest to make Test 9."""

import argparse
from pathlib import Path
import zipfile

from standalone_inputs import (TEST9_STORM_SHA256, sha256_file, verify_sha256, verify_test8_guest,
                               verify_test9_guest)

STORM = "lib/armeabi-v7a/libStormGLOFT.so"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test8-guest", type=Path, required=True)
    parser.add_argument("--patched-storm", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    verify_test8_guest(args.test8_guest)
    verify_sha256(args.patched_storm, TEST9_STORM_SHA256)
    if args.output.resolve() == args.test8_guest.resolve():
        raise ValueError("Output must not replace the pinned Test 8 input")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".partial")
    try:
        with zipfile.ZipFile(args.test8_guest) as original, zipfile.ZipFile(temporary, "w") as updated:
            names = original.namelist()
            if len(names) != len(set(names)) or names.count(STORM) != 1:
                raise ValueError("Test 8 guest has duplicate or missing ZIP entries")
            for entry in original.infolist():
                data = args.patched_storm.read_bytes() if entry.filename == STORM else original.read(entry)
                updated.writestr(entry, data)
        verify_test9_guest(temporary)
        with zipfile.ZipFile(args.test8_guest) as original, zipfile.ZipFile(temporary) as updated:
            if original.namelist() != updated.namelist() or updated.namelist().count("classes2.dex") != 1:
                raise ValueError("Guest entry layout changed")
            if any(original.read(name) != updated.read(name) for name in original.namelist() if name != STORM):
                raise ValueError("Repack changed an unrelated guest entry")
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Test 9 guest: {args.output} SHA-256 {sha256_file(args.output)}")


if __name__ == "__main__":
    main()
