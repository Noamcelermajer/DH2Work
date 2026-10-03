#!/usr/bin/env python3
"""Rebuild the viewport-corrected private Test 10 guest from pinned Test 9.

Only the two DEX entries change. Owner-supplied game and cache bytes stay outside
Git; the exact original guest and every resulting DEX/ZIP hash are checked.
"""

import argparse
import hashlib
from pathlib import Path
import subprocess
import zipfile

from standalone_inputs import (
    TEST10_HELPER_DEX_SHA256,
    TEST10_PRIMARY_DEX_SHA256,
    verify_test9_guest,
    verify_test10_guest,
)

ROOT = Path(__file__).resolve().parent
GAME_CLASS = Path("smali/com/gameloft/android/GAND/GloftD2SS/DungeonHunter2.smali")
PHONE_CALL = "invoke-static {v1, v0}, Lcom/gameloft/android/GAND/GloftD2SS/DungeonHunter2;->nativeSetPhone(II)V"


def run(*args: Path | str) -> None:
    subprocess.run([str(arg) for arg in args], check=True)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch_initial_phone_size(path: Path) -> None:
    source = path.read_text(encoding="utf-8")
    start = source.index(".method protected onCreate(")
    end = source.index(".end method", start)
    method = source[start:end]
    if method.count(".locals 7") != 1 or method.count(PHONE_CALL) != 1:
        raise ValueError("Unexpected original onCreate layout")
    method = method.replace(".locals 7", ".locals 8", 1)
    method = method.replace(
        PHONE_CALL,
        """move v7, v1
    invoke-static {v1, v0}, Llocal/dh2/compat/GameTrace;->initialPhoneWidth(II)I
    move-result v1
    invoke-static {v7, v0}, Llocal/dh2/compat/GameTrace;->initialPhoneHeight(II)I
    move-result v0
    """ + PHONE_CALL,
    )
    path.write_text(source[:start] + method + source[end:], encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test9-guest", type=Path, required=True)
    parser.add_argument("--apktool-jar", type=Path, required=True)
    parser.add_argument("--android-jar", type=Path, required=True)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--javac", type=Path, required=True)
    parser.add_argument("--d8", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    verify_test9_guest(args.test9_guest)
    if args.output.resolve() == args.test9_guest.resolve():
        raise ValueError("Output must not replace the pinned Test 9 guest")
    if args.work_dir.exists() and any(args.work_dir.iterdir()):
        raise ValueError("Use an empty work directory for a clean rebuild")
    args.work_dir.mkdir(parents=True, exist_ok=True)
    decoded = args.work_dir / "decoded"
    assembled = args.work_dir / "apktool-built.apk"
    run(args.java, "-jar", args.apktool_jar, "d", "-r", args.test9_guest, "-o", decoded)
    patch_initial_phone_size(decoded / GAME_CLASS)
    run(args.java, "-jar", args.apktool_jar, "b", decoded, "-o", assembled)
    with zipfile.ZipFile(assembled) as built:
        primary_dex = built.read("classes.dex")
    if sha256(primary_dex) != TEST10_PRIMARY_DEX_SHA256:
        raise ValueError("Primary DEX differs from the reviewed patch")

    classes = args.work_dir / "classes"
    classes.mkdir()
    java_sources = sorted(path for path in (ROOT / "guest-java").rglob("*.java")
                          if path.name != "GameTrace.java")
    java_sources.append(ROOT / "test10-guest-java/local/dh2/compat/GameTrace.java")
    run(args.javac, "-source", "8", "-target", "8", "-cp", args.android_jar,
        "-d", classes, *java_sources)
    class_files = sorted(classes.rglob("*.class"))
    if len(class_files) < 6:
        raise ValueError("Expected all six compiled guest helper classes")
    dex_dir = args.work_dir / "helper-dex"
    dex_dir.mkdir()
    run(args.d8, "--min-api", "21", "--lib", args.android_jar,
        "--output", dex_dir, *class_files)
    helper_dex = (dex_dir / "classes.dex").read_bytes()
    if sha256(helper_dex) != TEST10_HELPER_DEX_SHA256:
        raise ValueError("Helper DEX differs from the reviewed patch")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".partial")
    replacements = {"classes.dex": primary_dex, "classes2.dex": helper_dex}
    try:
        with zipfile.ZipFile(args.test9_guest) as original, zipfile.ZipFile(temporary, "w") as updated:
            names = original.namelist()
            if len(names) != len(set(names)) or any(names.count(name) != 1 for name in replacements):
                raise ValueError("Pinned Test 9 has duplicate or missing DEX entries")
            for entry in original.infolist():
                updated.writestr(entry, replacements.get(entry.filename, original.read(entry)))
        verify_test10_guest(temporary)
        with zipfile.ZipFile(args.test9_guest) as original, zipfile.ZipFile(temporary) as updated:
            if original.namelist() != updated.namelist():
                raise ValueError("Guest entry layout changed")
            if any(original.read(name) != updated.read(name) for name in original.namelist()
                   if name not in replacements):
                raise ValueError("Repack changed an unrelated guest entry")
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Test 10 guest: {args.output} SHA-256 {sha256(args.output.read_bytes())}")


if __name__ == "__main__":
    main()
