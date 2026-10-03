"""Validate private, owner-supplied inputs before a local standalone APK build.

This module contains only expected hashes. It never downloads or stores the game
APK or cache in the Git checkout; callers choose explicit local input paths.
"""

from pathlib import Path
import hashlib
import shutil
import zipfile


TEST7_GUEST_SHA256 = "302ae407d27dc6b94501e3e92c64dd9817b4742b3a45f7e134413f4cd40027bd"
TEST7_ENGINE_SHA256 = "45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4"
TEST7_STORM_SHA256 = "334c23b854327ea0315f5353ec89d16fc518963840bedb3753248952f02c7845"
TEST8_GUEST_SHA256 = "8168af36b2d82cf6b897da2fe4ec382c816f6498840e3bb61aede2f23c877e20"
TEST8_ENGINE_SHA256 = "ad33304fe17654ff5e05606323d977c89687c96bc8ce4722983ec6e092bb5f5f"
TEST9_GUEST_SHA256 = "310adb7117104fbb5de0f0542586e3eb9d3e23fe8f06b30d8036faea442733f1"
TEST9_STORM_SHA256 = "2489c037d75cd2a3c994b7bc349aac767c34f96acf25a79188a72c77dc7502de"
TEST10_GUEST_SHA256 = "57cefd15cba47116a98fa96e406ba8d8a4ef90fb0e82185802a8f09210ba2b7e"
TEST10_PRIMARY_DEX_SHA256 = "03c71b7a981b15ac8d28129d9a0abe38d9356d8890c0d4f1374cbac654b9df72"
TEST10_HELPER_DEX_SHA256 = "bba5f019caf2a0cc0c6f6c8a8f673dc69ec792355af272b7f5020693820d3efe"
COMPLETE_CACHE_SHA256 = "3fdf4e4c21d45a780a7c35fb4042abde0e88e76bf75416aad1f227481560b679"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_sha256(path: Path, expected: str) -> None:
    actual = sha256_file(path)
    if actual != expected:
        raise ValueError(f"{path.name}: SHA-256 {actual} differs from pinned {expected}")


def verify_test7_guest(path: Path) -> None:
    _verify_guest(path, TEST7_GUEST_SHA256, TEST7_ENGINE_SHA256, TEST7_STORM_SHA256)


def verify_test8_guest(path: Path) -> None:
    _verify_guest(path, TEST8_GUEST_SHA256, TEST8_ENGINE_SHA256, TEST7_STORM_SHA256)


def verify_test9_guest(path: Path) -> None:
    _verify_guest(path, TEST9_GUEST_SHA256, TEST8_ENGINE_SHA256, TEST9_STORM_SHA256)


def verify_test10_guest(path: Path) -> None:
    _verify_guest(path, TEST10_GUEST_SHA256, TEST8_ENGINE_SHA256, TEST9_STORM_SHA256)
    with zipfile.ZipFile(path) as archive:
        if hashlib.sha256(archive.read("classes.dex")).hexdigest() != TEST10_PRIMARY_DEX_SHA256:
            raise ValueError("Test 10 primary DEX differs from the pinned viewport patch")
        if hashlib.sha256(archive.read("classes2.dex")).hexdigest() != TEST10_HELPER_DEX_SHA256:
            raise ValueError("Test 10 helper DEX differs from the pinned viewport patch")


def _verify_guest(path: Path, expected_outer: str, expected_engine: str, expected_storm: str) -> None:
    verify_sha256(path, expected_outer)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError("Pinned guest has a damaged ZIP entry")
        expected = {
            "lib/armeabi-v7a/libDungeonHunter2.so": expected_engine,
            "lib/armeabi-v7a/libStormGLOFT.so": expected_storm,
        }
        names = archive.namelist()
        if any(names.count(name) != 1 for name in expected):
            raise ValueError("Guest must have exactly one engine and one Storm library")
        if any(name.startswith("META-INF/") for name in names):
            raise ValueError("Pass the unsigned guest, not a signed APK")
        for name, digest in expected.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != digest:
                raise ValueError(f"Guest contains the wrong {name}")


def copy_verified_cache(source: Path, destination: Path) -> None:
    """Publish the exact complete ZIP without leaving a partial asset on error."""
    verify_sha256(source, COMPLETE_CACHE_SHA256)
    if source.resolve() == destination.resolve():
        raise ValueError("Cache source and bundled destination must differ")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".partial")
    try:
        with source.open("rb") as src, temporary.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1024 * 1024)
        verify_sha256(temporary, COMPLETE_CACHE_SHA256)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
