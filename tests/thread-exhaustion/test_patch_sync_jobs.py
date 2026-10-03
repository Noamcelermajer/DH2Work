#!/usr/bin/env python3
"""Focused static regression against the exact locally supplied ARM32 engine."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from patch_sync_jobs import (  # noqa: E402
    INPUT_SHA256,
    NEW_TARGET,
    OLD_INSTRUCTION,
    OLD_TARGET,
    SITE,
    decode_arm_bne,
    encode_arm_bne,
    patch_bytes,
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="Test 5/6/7 libDungeonHunter2.so extracted outside Git")
    args = parser.parse_args()
    original = args.engine.read_bytes()
    assert hashlib.sha256(original).hexdigest() == INPUT_SHA256
    assert original[SITE : SITE + 4] == OLD_INSTRUCTION
    assert decode_arm_bne(SITE, OLD_INSTRUCTION) == OLD_TARGET
    assert encode_arm_bne(SITE, NEW_TARGET).hex() == "be01001a"
    patched = patch_bytes(original)
    assert patched[:SITE] == original[:SITE]
    assert patched[SITE + 4 :] == original[SITE + 4 :]
    assert decode_arm_bne(SITE, patched[SITE : SITE + 4]) == NEW_TARGET
    assert len(patched) == len(original)
    for invalid_target in (SITE + 1, SITE + 8 + 4 * (1 << 23)):
        try:
            encode_arm_bne(SITE, invalid_target)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid branch target was accepted")
    try:
        patch_bytes(original[:-1] + bytes([original[-1] ^ 1]))
    except ValueError as error:
        assert "SHA-256" in str(error)
    else:
        raise AssertionError("unrecognized engine was accepted")
    print("PASS: pinned ARM32 BNE reroutes one save-job path to existing synchronous call")


if __name__ == "__main__":
    main()
