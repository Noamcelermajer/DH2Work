#!/usr/bin/env python3
"""Build an isolated, hash-pinned ARM32 save-job scheduling experiment.

The input is the Test 5/6/7 engine, extracted locally from the guest APK.
This never patches the baseline package or writes to the input file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


INPUT_SHA256 = "45891aad9e7a5b1d84a5218f91926a04bd13a62ce104cb29beca7c70228c93c4"
SITE = 0x32C534
OLD_TARGET = 0x32CB74  # updateJob_thread::Start()
NEW_TARGET = 0x32CC34  # existing Savegame::UpdateJobs() call
OLD_INSTRUCTION = bytes.fromhex("8e01001a")


def encode_arm_bne(site: int, target: int) -> bytes:
    delta = target - (site + 8)
    if delta % 4:
        raise ValueError("ARM branch target must be word-aligned")
    words = delta // 4
    if not -(1 << 23) <= words < (1 << 23):
        raise ValueError("ARM branch target is out of range")
    return struct.pack("<I", 0x1A000000 | (words & 0xFFFFFF))


def decode_arm_bne(site: int, instruction: bytes) -> int:
    if len(instruction) != 4:
        raise ValueError("ARM instruction must be four bytes")
    word = struct.unpack("<I", instruction)[0]
    if word >> 24 != 0x1A:
        raise ValueError("instruction is not ARM BNE")
    displacement = word & 0xFFFFFF
    if displacement & 0x800000:
        displacement -= 1 << 24
    return site + 8 + 4 * displacement


def check_executable_mapping(data: bytes, site: int, target: int) -> None:
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x01\x01":
        raise ValueError("input is not little-endian ELF32")
    if struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("input is not ARM ELF")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phentsize = struct.unpack_from("<H", data, 42)[0]
    phnum = struct.unpack_from("<H", data, 44)[0]
    if phentsize != 32 or phoff + phentsize * phnum > len(data):
        raise ValueError("invalid ELF program headers")
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, _, p_flags, _ = struct.unpack_from(
            "<IIIIIIII", data, phoff + i * phentsize
        )
        if (
            p_type == 1
            and p_offset == 0
            and p_vaddr == 0
            and (p_flags & 1)
            and site + 4 <= p_filesz
            and target + 4 <= p_filesz
        ):
            return
    raise ValueError("patch site or target is outside the pinned executable mapping")


def patch_bytes(data: bytes) -> bytes:
    actual = hashlib.sha256(data).hexdigest()
    if actual != INPUT_SHA256:
        raise ValueError(f"unexpected engine SHA-256: {actual}")
    check_executable_mapping(data, SITE, NEW_TARGET)
    if data[SITE : SITE + 4] != OLD_INSTRUCTION:
        raise ValueError("original branch bytes changed")
    if decode_arm_bne(SITE, OLD_INSTRUCTION) != OLD_TARGET:
        raise ValueError("original branch target changed")
    replacement = encode_arm_bne(SITE, NEW_TARGET)
    output = bytearray(data)
    output[SITE : SITE + 4] = replacement
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="locally extracted Test 5/6/7 libDungeonHunter2.so")
    parser.add_argument("--output", required=True, type=Path, help="new diagnostic library path")
    parser.add_argument("--report", type=Path, help="optional JSON patch report path")
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("output must differ from input")
    if args.output.exists() or (args.report is not None and args.report.exists()):
        parser.error("output or report already exists")
    original = args.input.read_bytes()
    patched = patch_bytes(original)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(patched)
    report = {
        "purpose": "emulator-only synchronous save-job scheduling diagnostic",
        "input_sha256": INPUT_SHA256,
        "output_sha256": hashlib.sha256(patched).hexdigest(),
        "site_rva": f"0x{SITE:x}",
        "old_target_rva": f"0x{OLD_TARGET:x}",
        "new_target_rva": f"0x{NEW_TARGET:x}",
        "old_bytes": OLD_INSTRUCTION.hex(),
        "new_bytes": patched[SITE : SITE + 4].hex(),
        "changed_bytes": sum(a != b for a, b in zip(original, patched)),
        "runtime_validated": False,
    }
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
