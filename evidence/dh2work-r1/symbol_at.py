"""Symbolise (library, offset) pairs for a DH2 guest crash and disassemble the site.

usage: symbol_at.py <lib> <hex-offset> [<lib> <hex-offset> ...]
"""
import bisect
import sys

from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB


def symbols(path):
    with open(path, "rb") as handle:
        elf = ELFFile(handle)
        table = elf.get_section_by_name(".symtab") or elf.get_section_by_name(".dynsym")
        rows = []
        if table is not None:
            for sym in table.iter_symbols():
                if sym["st_info"]["type"] in ("STT_FUNC", "STT_NOTYPE") and sym["st_value"]:
                    rows.append((sym["st_value"], sym["st_size"], sym.name))
        rows.sort()
    return rows


def main():
    pairs = list(zip(sys.argv[1::2], sys.argv[2::2]))
    for path, offset_text in pairs:
        offset = int(offset_text, 16)
        rows = symbols(path)
        starts = [r[0] for r in rows]
        index = bisect.bisect_right(starts, offset) - 1
        print(f"\n===== {path}\n      offset 0x{offset:x}")
        if index >= 0:
            value, size, name = rows[index]
            print(f"      inside {name} (0x{value:x}+0x{offset - value:x}, size={size})")
        else:
            print("      no preceding symbol (library likely stripped at this range)")

        with open(path, "rb") as handle:
            elf = ELFFile(handle)
            segments = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]

            def read(vaddr, size):
                for seg in segments:
                    start = seg["p_vaddr"]
                    if start <= vaddr < start + seg["p_filesz"]:
                        handle.seek(seg["p_offset"] + (vaddr - start))
                        return handle.read(size)
                raise ValueError(hex(vaddr))

            base = max(0, offset - 0x30)
            try:
                code = read(base, 0x70)
            except ValueError as exc:
                print(f"      cannot map to file: {exc}")
                continue
            # ARM/Thumb selection must follow the mapping symbols, whose st_size is 0.
            thumb = index >= 0 and rows[index][2].startswith("$t")
            mode = CS_MODE_THUMB if thumb else CS_MODE_ARM
            md = Cs(CS_ARCH_ARM, mode)
            print(f"      mode={'thumb' if thumb else 'arm'}")
            for insn in md.disasm(code, base):
                marker = "  <== fault" if insn.address == offset else ""
                print(f"      0x{insn.address:08x}  {insn.bytes.hex():<10} {insn.mnemonic:<8} {insn.op_str}{marker}")


if __name__ == "__main__":
    main()
