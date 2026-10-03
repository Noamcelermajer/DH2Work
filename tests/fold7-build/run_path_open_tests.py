"""Exercise the real ARM32 CFileSystem with a nonempty cache root."""
from pathlib import Path
import subprocess, os, json, argparse
parser=argparse.ArgumentParser()
parser.add_argument('--case',action='append',default=[])
options=parser.parse_args()
b=Path(__file__).resolve().parents[1]; w=b.parent; r=w/'research/ZettaBridge'
cc=w/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang'
subprocess.run([str(cc),'-O2','-Wall',str(b/'tests/dh2_file_open_probe.c'),'-ldl','-o',str(b/'dh2_file_open_probe')],check=True)
subprocess.run(['python',str(r/'tools/fix_guest_lib.py'),str(b/'probe-libs/libDungeonHunter2.so')],check=True)
fixtures=b/'file-fixtures'; fixtures.mkdir(exist_ok=True)
model=fixtures/'data/3d/characters/prince/prince_modular.bdae'
model.parent.mkdir(exist_ok=True,parents=True); model.write_bytes(b'probe synthetic model bytes\n')
(fixtures/'valid.bin').write_bytes(b'probe payload\n')
(fixtures/'Z:valid.bin').write_bytes(b'probe colon payload\n')
# The actual phone spelling is intentionally lowercased by the original engine.
phone='/storage/emulated/0/android/data/local.dh2.fold7/files/plugins/com.gameloft.android.gand.gloftd2ss'
cases=[
 ('test4-rooted-absolute',False,str(model),str(fixtures)+'/',0),
 ('test5-rooted-absolute',True,str(model),str(fixtures)+'/',1),
 ('test5-root-without-slash',True,str(model),str(fixtures),1),
 ('test5-rooted-relative',True,'data/3d/characters/prince/prince_modular.bdae',str(fixtures)+'/',1),
 ('test5-relative-root-without-slash',True,'valid.bin',str(fixtures),1),
 ('test5-empty-root-absolute',True,str(model),'',1),
 ('test4-phone-absolute',False,phone+'/data/3d/characters/prince/prince_modular.bdae',phone+'/',0),
 ('test5-phone-absolute',True,phone+'/data/3d/characters/prince/prince_modular.bdae',phone+'/',1),
 ('test5-phone-relative',True,'data/3d/characters/prince/prince_modular.bdae',phone+'/',1),
 ('test5-missing-absolute',True,str(fixtures/'missing.bin'),str(fixtures)+'/',0),
 ('test5-missing-relative',True,'missing.bin',str(fixtures)+'/',0),
 ('test5-directory',True,str(fixtures)+'/',str(fixtures)+'/',0),
 ('test5-colon-path',True,'Z:valid.bin',str(fixtures)+'/',1),
]
results=[]
for name,fixed,path,root,want in cases:
 if options.case and name not in options.case:continue
 libs=b/('probe-libs' if fixed else 'probe-libs-test4')
 args=[str(w/'toolchains/qemu8/usr/bin/qemu-aarch64-static'),'-E','LD_LIBRARY_PATH='+str(w/'host64-runtime/system/lib64'),str(r/'build/android-arm64/cli/zbrun/zbrun'),'--sysroot',str(r/'sysroot'),
       '--alias','/data/data/com.gameloft.android.GAND.GloftD2SS/lib/',str(libs)+'/',
       '--alias',phone+'/',str(fixtures)+'/',
       '--env','LD_LIBRARY_PATH='+str(r/'build/guest/lib')+':'+str(libs),
       '--report',str(b/('path-open-'+name+'.txt')),str(b/'dh2_file_open_probe'),str(libs),'1',path,str(want),root]
 with (b/('path-open-'+name+'.log')).open('w') as log:
  c=subprocess.run(args,cwd=fixtures,env=dict(os.environ,QEMU_LD_PREFIX=str(w/'host64-runtime')),stdout=log,stderr=subprocess.STDOUT,timeout=60)
 text=(b/('path-open-'+name+'.log')).read_text()
 assert c.returncode==0 and 'PASS file-open contract' in text,(name,c.returncode,text)
 results.append({'case':name,'expected_open':bool(want),'pass':True})
 print(name,'PASS',flush=True)
(b/('path-open-tests-final-smoke.json' if options.case else 'path-open-tests.json')).write_text(json.dumps(results,indent=2)+'\n')
