"""Disassemble the DH2 crash call chain: nativeSetOrientation -> SetFinalOrientation
-> SavegameManager::getAutoReorientation, and resolve the pointer being dereferenced.
"""
import sys

from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

ENGINE = sys.argv[1] if len(sys.argv) > 1 else (
    r"C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work"
    r"\original\lib\armeabi-v7a\libDungeonHunter2.so"
)

WINDOWS = [
    ("nativeSetOrientation", 0x533500, 84),
    ("SetFinalOrientation", 0x530A6C, 240),
    ("getAutoReorientation", 0x46D4D8, 60),
]

with open(ENGINE, "rb") as handle:
    elf = ELFFile(handle)
    segments = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]

    def read(vaddr, size):
        for seg in segments:
            start = seg["p_vaddr"]
            if start <= vaddr < start + seg["p_filesz"]:
                handle.seek(seg["p_offset"] + (vaddr - start))
                return handle.read(size)
        raise ValueError(hex(vaddr))

    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for name, base, size in WINDOWS:
        print(f"\n===== {name} @ 0x{base:08x} ({size} bytes) =====")
        code = read(base, size)
        for insn in md.disasm(code, base):
            note = ""
            # Resolve pc-relative literal loads so external globals are visible.
            if insn.mnemonic == "ldr" and "[pc" in insn.op_str:
                try:
                    disp = int(insn.op_str.split("#")[-1].rstrip("]"), 0)
                    literal_addr = insn.address + 8 + disp
                    word = int.from_bytes(read(literal_addr, 4), "little")
                    note = f"   ; literal @0x{literal_addr:08x} = 0x{word:08x}"
                except Exception as exc:  # pragma: no cover - diagnostic aid
                    note = f"   ; literal unresolved ({exc})"
            print(f"0x{insn.address:08x}  {insn.bytes.hex():<10} {insn.mnemonic:<8} {insn.op_str}{note}")
