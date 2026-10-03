#!/usr/bin/env python3
"""Run real ARM32 aborts through the APK's rebuilt ARM64 translator under QEMU."""
from pathlib import Path
import os,subprocess,json
ROOT=Path(__file__).resolve().parents[1];WORK=ROOT.parent;repo=WORK/'research/ZettaBridge'
cc=WORK/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang'
probe=ROOT/'diagnostic_probe'
subprocess.run([str(cc),'-O0','-g',str(ROOT/'tests/diagnostic_probe.c'),'-o',str(probe)],check=True)
env=dict(os.environ,QEMU_LD_PREFIX=str(WORK/'host64-runtime'))
base=[str(WORK/'toolchains/qemu8/usr/bin/qemu-aarch64-static'),'-E','LD_LIBRARY_PATH='+str(WORK/'host64-runtime/system/lib64'),str(repo/'build/android-arm64/cli/zbrun/zbrun'),'--sysroot',str(repo/'sysroot')]
results=[]
for case,status in [('abort',134),('threads',134),('exit',42),('caught',0),('fault',139)]:
 report=ROOT/('diagnostic-'+case+'.txt')
 p=subprocess.run(base+['--report',str(report),str(probe),case],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
 (ROOT/('diagnostic-'+case+'.log')).write_text(p.stdout)
 text=report.read_text()
 assert p.returncode==status,(case,p.returncode,p.stdout)
 assert 'diagnostic write marker' in text and 'diagnostic  writev marker' in text,(case,text)
 if status:assert 'crash-registers:' in text and 'crash-pc:' in text,(case,text)
 if case in ['abort','threads']:assert 'guest terminated by signal 6' in text,(case,text)
 if case=='caught':assert 'crash-registers:' not in text and 'guest terminated' not in text,(case,text)
 results.append({'case':case,'exit':status,'persisted':True})
 print('PASS',case,status,flush=True)
(ROOT/'diagnostic-tests.json').write_text(json.dumps(results,indent=2)+'\n')
