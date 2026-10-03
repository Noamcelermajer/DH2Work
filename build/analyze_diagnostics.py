#!/usr/bin/env python3
"""Map report FILE OFFSETS through PT_LOAD before looking up original ELF symbols."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
import re,json,zipfile,argparse
p=argparse.ArgumentParser();p.add_argument('zip',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
b=Path(__file__).resolve().parent;w=b.parent
libs={'libDungeonHunter2.so':w/'original/lib/armeabi-v7a/libDungeonHunter2.so','libc.so':b/'out/assets/zb/sysroot/system/lib/libc.so'}
with zipfile.ZipFile(a.zip) as z:text=z.read('zb-runtime-report.txt').decode()
rows=[]
for line in text.splitlines():
 if not line.startswith(('crash-pc:','crash-lr:','crash-stack-candidate-')):continue
 match=re.search(r'/([^/ ]+) (offset|vaddr) (0x[0-9a-f]+)',line)
 if not match or match[1] not in libs:continue
 n,kind,x=match.groups();off=int(x,16);va=off
 with libs[n].open('rb') as f:
  e=ELFFile(f)
  if kind=='offset':
   seg=next(s for s in e.iter_segments() if s['p_type']=='PT_LOAD' and s['p_offset']<=off<s['p_offset']+s['p_filesz'])
   va=off+seg['p_vaddr']-seg['p_offset']
  symbols=[s for sec in e.iter_sections() if sec['sh_type'] in ('SHT_DYNSYM','SHT_SYMTAB') for s in sec.iter_symbols() if s['st_info']['type']=='STT_FUNC' and (s['st_value']&~1)<=va<(s['st_value']&~1)+s['st_size']]
  symbol=max(symbols,key=lambda s:s['st_value']) if symbols else None
  rows.append({'report':line,'module':n,'reported_offset':x,'elf_vaddr':hex(va),'function':symbol.name if symbol else None,'function_offset':hex(va-(symbol['st_value']&~1)) if symbol else None,'warning':'Stack scan candidates can be stale; this is not an unwound backtrace.'})
a.output.write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2))
