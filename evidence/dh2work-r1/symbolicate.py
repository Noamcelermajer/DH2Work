"""Symbolicate and disassemble the DH2Work r1 crash addresses."""
import bisect
import sys

from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

ENGINE = sys.argv[1] if len(sys.argv) > 1 else (
    r"C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work"
    r"\original\lib\armeabi-v7a\libDungeonHunter2.so"
)

TARGETS = [
    (0x46D4D8, "crash-pc"),
    (0x530A90, "crash-lr"),
    (0x99A11C, "stack-cand-1"),
    (0x533540, "stack-cand-2"),
]

with open(ENGINE, "rb") as handle:
    elf = ELFFile(handle)
    symtab = elf.get_section_by_name(".symtab")
    funcs = []
    for sym in symtab.iter_symbols():
        if sym["st_info"]["type"] == "STT_FUNC" and sym["st_value"]:
            funcs.append((sym["st_value"], sym["st_size"], sym.name))
    funcs.sort()
    starts = [f[0] for f in funcs]

    print("=== symbolication ===")
    for addr, label in TARGETS:
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0:
            value, size, name = funcs[i]
            end = value + size if size else 0
            inside = "IN" if (not size or addr < end) else "AFTER-END"
            print(f"{label}: 0x{addr:08x} -> {name} "
                  f"(0x{value:08x}+0x{addr - value:x}, size={size}, {inside})")
        else:
            print(f"{label}: 0x{addr:08x} -> no preceding function symbol")

    # Locate the containing PT_LOAD so we can slice real bytes for disassembly.
    def file_offset_of(vaddr):
        for seg in elf.iter_segments():
            if seg["p_type"] != "PT_LOAD":
                continue
            start = seg["p_vaddr"]
            if start <= vaddr < start + seg["p_filesz"]:
                return seg["p_offset"] + (vaddr - start)
        return None

    print("\n=== disassembly around crash-pc 0x46d4d8 ===")
    base = 0x46D4D8
    off = file_offset_of(base)
    with open(ENGINE, "rb") as fh:
        fh.seek(off - 0x40)
        code = fh.read(0xC0)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for insn in md.disasm(code, base - 0x40):
        marker = "  <== crash-pc" if insn.address == base else ""
        print(f"0x{insn.address:08x}  {insn.bytes.hex():<10} {insn.mnemonic:<8} {insn.op_str}{marker}")
