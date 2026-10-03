#!/usr/bin/env python3
"""Verify the opt-in language edit preserves DH2 settings and the owner's cache."""

import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
OWNER_ZIP = Path(os.environ.get("DH2_OWNER_CACHE_ZIP", REPO.parent / "standalone-build/cache-input.zip"))
OWNER_MEMBER = "com.gameloft.android.GAND.GloftD2SS/files/dh2_settings.savegame"
OWNER_SHA256 = "3cb97b47cc9853d65dc596ccc0a8b715b82fe4794812435673a921b215f07c4a"
SETTINGS = "dh2_settings.savegame"
BACKUP = SETTINGS + ".before-prefer-english.bak"
KEYS = (
    "AAA_DONT_DELETE", "AllFaeries", "AllLevels", "AutoOrientation",
    "AutoTransmute", "DPad", "ForwardCam", "GOD", "GOD_MANA",
    "GiveGold", "HUDStyle", "Language", "LevelUp", "OneShotKill",
    "VolumeFX", "VolumeMusic",
)


def synthetic_settings() -> bytes:
    data = bytearray(struct.pack("<I", len(KEYS)))
    for key in KEYS:
        encoded = key.encode("ascii")
        data += struct.pack("<I", len(encoded)) + encoded
        data += struct.pack("<i", 2 if key == "Language" else 0)
    data += bytes((1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1))
    return bytes(data)


def run_java(classes: Path, root: Path, enabled: bool) -> str:
    result = subprocess.run(
        ["java", "-cp", str(classes), "com.zettabridge.launcher.LanguagePreferenceTest",
         str(root), str(enabled).lower()],
        check=True, text=True, capture_output=True,
    )
    return result.stdout.strip()


def verify_fixture(classes: Path, fixture: bytes) -> None:
    with tempfile.TemporaryDirectory(prefix="dh2-language-case-") as folder:
        root = Path(folder)
        settings = root / SETTINGS
        backup = root / BACKUP
        assert run_java(classes, root, True) == "ABSENT"
        settings.write_bytes(fixture)
        assert run_java(classes, root, False) == "DISABLED"
        assert settings.read_bytes() == fixture and not backup.exists()
        assert run_java(classes, root, True) == "UPDATED"
        expected = bytearray(fixture)
        assert expected[207:211] == b"\x02\x00\x00\x00"
        expected[207:211] = b"\x00\x00\x00\x00"
        assert settings.read_bytes() == expected
        assert backup.read_bytes() == fixture
        assert run_java(classes, root, True) == "ALREADY_ENGLISH"
        assert backup.read_bytes() == fixture
        assert not list(root.glob(".dh2-language-*.tmp"))

        settings.write_bytes(fixture)
        backup.write_bytes(b"partial")
        assert run_java(classes, root, True) == "UNRECOGNIZED"
        assert settings.read_bytes() == fixture


def main() -> None:
    javac = shutil.which("javac")
    if javac is None:
        raise RuntimeError("JDK required for host regression test")
    with tempfile.TemporaryDirectory(prefix="dh2-language-java-") as folder:
        classes = Path(folder)
        subprocess.run(
            [javac, "--release", "17", "-d", str(classes),
             str(ROOT / "java/com/zettabridge/launcher/LanguagePreference.java"),
             str(ROOT / "tests/LanguagePreferenceTest.java")], check=True,
        )
        fixture = synthetic_settings()
        assert len(fixture) == 294
        verify_fixture(classes, fixture)
        with tempfile.TemporaryDirectory(prefix="dh2-language-malformed-") as folder:
            root = Path(folder)
            damaged = bytearray(fixture)
            damaged[4:8] = struct.pack("<I", 999)
            (root / SETTINGS).write_bytes(damaged)
            assert run_java(classes, root, True) == "UNRECOGNIZED"
            assert (root / SETTINGS).read_bytes() == damaged
            assert not (root / BACKUP).exists()

        owner_checked = False
        if OWNER_ZIP.is_file():
            with zipfile.ZipFile(OWNER_ZIP) as archive:
                owner_settings = archive.read(OWNER_MEMBER)
            assert hashlib.sha256(owner_settings).hexdigest() == OWNER_SHA256
            assert len(owner_settings) == 294
            verify_fixture(classes, owner_settings)
            owner_checked = True
        print("PASS: disabled, absent, malformed, backup integrity, exact-byte edit,"
              " idempotency, atomic-temp cleanup; owner cache fixture:",
              "verified" if owner_checked else "not present (set DH2_OWNER_CACHE_ZIP)")


if __name__ == "__main__":
    main()
