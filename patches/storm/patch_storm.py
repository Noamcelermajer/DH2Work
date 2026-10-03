#!/usr/bin/env python3
"""Exact-build, reviewed ARM32 fix for Storm's obsolete private-linker ABI use."""
from pathlib import Path
import hashlib,os,struct,subprocess,json,argparse
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM

# Resolve the pinned NDK's per-host prebuilt tree so this runs unchanged on the
# recorded Linux build host and on Windows; --toolchain-bin overrides it.
HOST_TAG='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--original-storm',type=Path,default=ROOT.parent/'original/lib/armeabi-v7a/libStormGLOFT.so')
parser.add_argument('--original-engine',type=Path,default=ROOT.parent/'original/lib/armeabi-v7a/libDungeonHunter2.so')
parser.add_argument('--toolchain-bin',type=Path,default=ROOT.parent/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt'/HOST_TAG/'bin')
args=parser.parse_args()
original=args.original_storm
expected='be6beaab782944e5ce39cca8e850fd03de654e4236c9329621043adcbee291e1'
if hashlib.sha256(original.read_bytes()).hexdigest()!=expected:
    raise SystemExit('Storm library differs from the supplied build; refusing an address-based patch.')
engine=args.original_engine
if hashlib.sha256(engine.read_bytes()).hexdigest()!='36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80':
    raise SystemExit('Companion engine differs from the build used for symbol and GOT addresses.')
# Freeze the original hash in patch metadata and check the replacement instruction,
# symbol addresses, dynamic name and unused executable gap before touching bytes.
with original.open('rb') as f:
    elf=ELFFile(f);sym=elf.get_section_by_name('.dynsym')
    hook=next(s for s in sym.iter_symbols() if s.name=='_Z20hook_inline_functionPKcib')
    assert hook['st_value']==0x43894 and hook['st_size']==352
    loads=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    assert loads[0]['p_offset']==0 and loads[0]['p_vaddr']==0 and loads[0]['p_flags']==5
    oldend=loads[0]['p_filesz'];nextoff=loads[1]['p_offset']
    phoff=elf['e_phoff'];phsize=elf['e_phentsize']
    header_index=next(i for i,s in enumerate(elf.iter_segments()) if s['p_type']=='PT_LOAD')
toolchain=args.toolchain_bin
if (toolchain/'clang.exe').is_file():
    compiler=[str(toolchain/'clang.exe'),'--target=armv7a-linux-androideabi21']
    linker=toolchain/'ld.lld.exe'
    objcopy=toolchain/'llvm-objcopy.exe'
else:
    compiler=[str(toolchain/'armv7a-linux-androideabi21-clang')]
    linker=toolchain/'ld.lld'
    objcopy=toolchain/'llvm-objcopy'
out=ROOT/'out';out.mkdir(exist_ok=True)
subprocess.run([*compiler,'-c',str(ROOT/'storm_bias_fix.S'),'-o',str(out/'storm-bias.o')],check=True)
subprocess.run([*compiler,'-marm','-Os','-fPIC','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-c',str(ROOT/'storm_import_fix.c'),'-o',str(out/'storm-import.o')],check=True)
objects=[str(out/'storm-bias.o'),str(out/'storm-import.o')]
subprocess.run([str(linker),'-T',str(ROOT/'storm_bias_fix.ld'),*objects,'-o',str(out/'storm-fixes.elf')],check=True)
subprocess.run([str(objcopy),'-O','binary',str(out/'storm-fixes.elf'),str(out/'storm-bias.bin')],check=True)
with (out/'storm-fixes.elf').open('rb') as f:
    fixelf=ELFFile(f)
    assert not any(sec['sh_size'] and sec.name in ('.got','.got.plt','.data','.bss','.rel.dyn') for sec in fixelf.iter_sections()), 'Embedded patch must be position independent without runtime relocations'
    import_va=next(s['st_value'] for s in fixelf.get_section_by_name('.symtab').iter_symbols() if s.name=='storm_import_fix')
stub=(out/'storm-bias.bin').read_bytes();start=0xd3800;end=start+len(stub)
data=bytearray(original.read_bytes())
assert data[0x438e4:0x438e8]==bytes.fromhex('8c0090e5')
assert data[0x9a06:0x9a11]==b'JNI_OnLoad\0'
assert oldend<=start and end<nextoff and not any(data[start:end])
assert len(stub)<nextoff-start,len(stub)
# ARM unconditional branch: PC is the instruction address + 8.
delta=(start-(0x438e4+8))//4
struct.pack_into('<I',data,0x438e4,0xea000000|(delta&0xffffff))
assert data[0x371e4:0x371e8]==bytes.fromhex('f04f2de9')
import_delta=(import_va-(0x371e4+8))//4
struct.pack_into('<I',data,0x371e4,0xea000000|(import_delta&0xffffff))
data[start:end]=stub
header=phoff+header_index*phsize
struct.pack_into('<II',data,header+16,end,end)
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(data)
instructions=[f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(stub,start)]
assert any('bl #0x321d4' in x for x in instructions)
assert any('b #0x438e8' in x for x in instructions)
report={'input_sha256':expected,'output_sha256':hashlib.sha256(data).hexdigest(),'patched_va':'0x438e4','old_instruction':'ldr r0, [r0, #0x8c]','replacement':'branch to symbol-based load-bias resolver','import_hook_entry_patch':'0x371e4','import_hook_replacement':hex(import_va),'import_hook_algorithm':'Walk PT_DYNAMIC, DT_SYMTAB, DT_STRTAB and DT_JMPREL in the loaded engine ELF; replace matching R_ARM_JUMP_SLOT','stub_va':hex(start),'stub_bytes':len(stub),'original_rx_end':hex(oldend),'extended_rx_end':hex(end),'instructions':instructions,'file_guard':'Reject trailing directory separators; retry exact repeated Android cache roots after failed read-only fopen; retry flat qata/3d/textures/*.tga as data/3d/textures/*.tga after failed read-only fopen; log buffered STL puts reasons','scope':'Exact supplied DH2/Storm pair only; no licensing decisions modified'}
(ROOT/'storm-patch-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Patched Storm legacy linker handle dereference:',args.output)
