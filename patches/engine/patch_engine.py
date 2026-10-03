#!/usr/bin/env python3
"""Pinned ARM32 fix: recognize POSIX absolute paths in CFileSystem::open."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

# The pinned NDK ships one prebuilt tree per host.  Resolve the host tag so the
# same script runs on the recorded Linux build host and on Windows without an
# edit; --toolchain-bin still overrides it explicitly.
HOST_TAG = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64'
ROOT = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--toolchain-bin', type=Path,
               default=ROOT.parent / 'toolchains/android-ndk-r29/toolchains/llvm/prebuilt' / HOST_TAG / 'bin')
a = p.parse_args()
original = ROOT.parent / 'original/lib/armeabi-v7a/libDungeonHunter2.so'
expected = '36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80'
data = bytearray(original.read_bytes())
assert hashlib.sha256(data).hexdigest() == expected, 'Unrecognized engine'
start, end = 0x56dd9c, 0x56ddb0
assert data[start:end].hex() == '2400000a60009de5033060e0010073e32000000a'
tc = a.toolchain_bin
# Match patch_storm.py: the Windows NDK exposes clang.exe/ld.lld.exe/llvm-objcopy.exe
# and needs an explicit --target, while the Unix NDK provides a suffixed driver.
if (tc/'clang.exe').is_file():
    compiler = [str(tc/'clang.exe'), '--target=armv7a-linux-androideabi21']
    linker = tc/'ld.lld.exe'
    objcopy = tc/'llvm-objcopy.exe'
else:
    compiler = [str(tc/'armv7a-linux-androideabi21-clang')]
    linker = tc/'ld.lld'
    objcopy = tc/'llvm-objcopy'
out = ROOT / 'out'
out.mkdir(exist_ok=True)
subprocess.run([*compiler, '-c', str(ROOT/'engine_path_fix.S'), '-o', str(out/'engine-path.o')], check=True)
subprocess.run([str(linker), '-T', str(ROOT/'engine_path_fix.ld'), str(out/'engine-path.o'), '-o', str(out/'engine-path.elf')], check=True)
subprocess.run([str(objcopy), '-O', 'binary', str(out/'engine-path.elf'), str(out/'engine-path.bin')], check=True)
patch = (out/'engine-path.bin').read_bytes()
assert len(patch) == end-start
data[start:end] = patch
a.output.parent.mkdir(exist_ok=True, parents=True)
a.output.write_bytes(data)
report = {'input_sha256': expected, 'output_sha256': hashlib.sha256(data).hexdigest(),
          'patch_vaddr': hex(start), 'patch_bytes': len(patch), 'replacement_hex': patch.hex(),
          'instructions': [f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(patch,start)],
          'scope': 'CFileSystem::open path classification only; existing colon and relative-path behavior retained'}
(ROOT/'engine-patch-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
