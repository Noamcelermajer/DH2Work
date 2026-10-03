#!/usr/bin/env python3
"""Repeat the complete-library load and Storm hook checks; not an ART/game test."""
from pathlib import Path
import subprocess,os,shutil,hashlib,sys
ROOT=Path(__file__).resolve().parent;WORK=ROOT.parent
repo=WORK/'research/ZettaBridge';native=WORK/'original/lib/armeabi-v7a'
if hashlib.sha256((native/'libDungeonHunter2.so').read_bytes()).hexdigest()!='36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80':
    raise SystemExit('Wrong engine input hash')
libs=ROOT/'probe-libs';libs.mkdir(exist_ok=True)
for p in native.glob('*.so'):shutil.copyfile(p,libs/p.name)
subprocess.run([sys.executable,str(ROOT/'patch_engine.py'),'--output',str(libs/'libDungeonHunter2.so')],check=True)
subprocess.run([sys.executable,str(ROOT/'patch_storm.py'),'--output',str(libs/'libStormGLOFT.so')],check=True)
subprocess.run([sys.executable,str(repo/'tools/fix_guest_lib.py'),*map(str,libs.glob('*.so'))],check=True)
cc=WORK/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang'
subprocess.run([str(cc),'-O2','-Wall',str(ROOT/'tests/dh2_load_probe.c'),'-o',str(ROOT/'dh2_load_probe'),'-ldl'],check=True)
env=dict(os.environ,QEMU_LD_PREFIX=str(WORK/'host64-runtime'))
args=[str(WORK/'toolchains/qemu8/usr/bin/qemu-aarch64-static'),'-E','LD_LIBRARY_PATH='+str(WORK/'host64-runtime/system/lib64'),
      str(repo/'build/android-arm64/cli/zbrun/zbrun'),'--sysroot',str(repo/'sysroot'),
      '--alias','/data/data/com.gameloft.android.GAND.GloftD2SS/lib/',str(libs)+'/',
      '--env','LD_LIBRARY_PATH='+str(repo/'build/guest/lib')+':'+str(libs),str(ROOT/'dh2_load_probe'),str(libs)]
with (ROOT/'native-load-test.log').open('w') as log:
    completed=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
print((ROOT/'native-load-test.log').read_text())
raise SystemExit(completed.returncode)
