"""Nearest-symbol lookup for a list of (library, hex-offset) pairs; arch-agnostic."""
import bisect
import sys

from elftools.elf.elffile import ELFFile


def load(path):
    handle = open(path, "rb")
    elf = ELFFile(handle)
    rows = []
    for section_name in (".symtab", ".dynsym"):
        table = elf.get_section_by_name(section_name)
        if table is None:
            continue
        for sym in table.iter_symbols():
            if sym["st_value"]:
                rows.append((sym["st_value"], sym["st_size"], sym.name))
    rows.sort()
    return rows


for path, offset_text in zip(sys.argv[1::2], sys.argv[2::2]):
    offset = int(offset_text, 16)
    rows = load(path)
    starts = [r[0] for r in rows]
    index = bisect.bisect_right(starts, offset) - 1
    if index >= 0:
        value, size, name = rows[index]
        inside = offset < value + size if size else None
        print(f"{path.split('/')[-1]} +0x{offset:x} -> {name} "
              f"(0x{value:x}+0x{offset - value:x}, size={size}, inside={inside})")
    else:
        print(f"{path.split('/')[-1]} +0x{offset:x} -> no symbol at or below")
