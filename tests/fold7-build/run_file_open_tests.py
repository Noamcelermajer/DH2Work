from pathlib import Path
import subprocess,os,json
b=Path(__file__).resolve().parents[1];w=b.parent;r=w/'research/ZettaBridge'
cc=w/'toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang'
subprocess.run([str(cc),'-O2','-Wall',str(b/'tests/dh2_file_open_probe.c'),'-ldl','-o',str(b/'dh2_file_open_probe')],check=True)
fixtures=b/'file-fixtures';fixtures.mkdir(exist_ok=True);(fixtures/'valid.bin').write_text('probe payload\n')
base=[str(w/'toolchains/qemu8/usr/bin/qemu-aarch64-static'),'-E','LD_LIBRARY_PATH='+str(w/'host64-runtime/system/lib64'),str(r/'build/android-arm64/cli/zbrun/zbrun'),'--sysroot',str(r/'sysroot'),'--alias','/data/data/com.gameloft.android.GAND.GloftD2SS/lib/',str(b/'probe-libs')+'/', '--env','LD_LIBRARY_PATH='+str(r/'build/guest/lib')+':'+str(b/'probe-libs')]
env=dict(os.environ,QEMU_LD_PREFIX=str(w/'host64-runtime'))
cases=[('baseline-file',0,str(fixtures/'valid.bin'),1,0),('baseline-directory',0,str(fixtures)+'/',0,134),('fixed-file',1,str(fixtures/'valid.bin'),1,0),('fixed-directory',1,str(fixtures)+'/',0,0),('fixed-missing',1,str(fixtures/'missing.bin'),0,0)]
results=[]
for name,patched,path,want,status in cases:
 log=b/('file-open-'+name+'.log');report=b/('file-open-'+name+'.txt')
 with log.open('w') as f:
  c=subprocess.run(base+['--report',str(report),str(b/'dh2_file_open_probe'),str(b/'probe-libs'),str(patched),path,str(want)],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=60)
 text=log.read_text();assert c.returncode==status,(name,c.returncode,text)
 if not status: assert 'PASS file-open contract' in text,(name,text)
 if name=='baseline-directory':
  evidence=report.read_text();assert 'offset 0x56ca64' in evidence and 'offset 0x56dde8' in evidence,evidence
 if name=='fixed-directory': assert 'DH2FileGuard rejected directory-shaped path' in report.read_text() and 'errno=21' in text
 results.append({'case':name,'status':c.returncode,'pass':True});print(name,'PASS',flush=True)
(b/'file-open-tests.json').write_text(json.dumps(results,indent=2)+'\n')
