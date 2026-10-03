#!/usr/bin/env python3
"""Snapshot reviewed test4 work; preserve older releases and unrelated reconstruction."""
from pathlib import Path
import hashlib,json,zipfile,shutil,subprocess
ROOT=Path(__file__).resolve().parent;WORK=ROOT.parent;BUILD=WORK/'fold7-build';REPO=WORK/'research/ZettaBridge'
STAGE=ROOT/'staging-test4';DEST=STAGE/'compatibility'
if DEST.exists():shutil.rmtree(DEST)
shutil.copytree(ROOT/'staging-test3/compatibility',DEST,ignore=shutil.ignore_patterns('.rsync-tmp'))
# Runtime inputs exactly match the final packaged APK.
runtime=REPO/'build/launcher';inventory={}
with zipfile.ZipFile(BUILD/'runtime-bundle.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(runtime.rglob('*')):
  if p.is_file() and '.rsync-tmp' not in p.parts:
   data=p.read_bytes();name=p.relative_to(runtime).as_posix();z.writestr(name,data);inventory[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
(BUILD/'runtime-manifest.json').write_text(json.dumps(inventory,indent=2)+'\n')
patch=subprocess.check_output(['git','-C',str(REPO),'diff','7c647a4f1ea150eab7978ab0da28fdf49f3a79de','--','.',':(exclude)AGENTS.md',':(exclude)third_party/dynarmic'])
(BUILD/'zettabridge-dh2.patch').write_bytes(patch)
head=subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()
result=json.loads((BUILD/'build-result.json').read_text());result['runtime_source_commit']=head;result['runtime_upstream_base']='7c647a4f1ea150eab7978ab0da28fdf49f3a79de'
(BUILD/'build-result.json').write_text(json.dumps(result,indent=2)+'\n')
(BUILD/'runtime-build-final.txt').write_text('Native source commit: '+head+'\nNative compile: passed. Guest build: passed.\nLauncher bundle: regenerated without temporary-copy artifacts; checker passed.\nFinal APK runtime bytes verified by verify_package.py.\n')
# Refresh authored sources and reports; generated build dirs are not source.
for p in BUILD.iterdir():
 if p.is_file() and p.suffix.lower() not in ['.apk']:
  shutil.copy2(p,DEST/'work/fold7-build'/p.name)
for name in ['java','guest-java','tests','notices','phone-test3']:
 shutil.copytree(BUILD/name,DEST/'work/fold7-build'/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.rsync-tmp'))
shutil.copytree(BUILD/'game-tree/smali',DEST/'work/fold7-build/generated-game-smali',dirs_exist_ok=True,ignore=shutil.ignore_patterns('.rsync-tmp'))
shutil.copytree(BUILD/'packaged-test3-smali/smali_classes2',DEST/'work/fold7-build/generated-helper-smali',dirs_exist_ok=True,ignore=shutil.ignore_patterns('.rsync-tmp'))
shutil.copy2(BUILD/'out/AndroidManifest.xml',DEST/'work/fold7-build/host-AndroidManifest.xml')
shutil.copy2(BUILD/'game-tree/AndroidManifest.xml',DEST/'work/fold7-build/game-AndroidManifest.xml')
changed=subprocess.check_output(['git','-C',str(REPO),'diff','--name-only','7c647a4f1ea150eab7978ab0da28fdf49f3a79de','HEAD'],text=True).splitlines()
for name in changed:
 if name=='third_party/dynarmic':continue
 p=REPO/name
 if p.is_file():dest=DEST/'upstream-modified'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
for name in ['README.md','FINDINGS.md','ISSUES.md','DEVICE-TESTING.md','BUILDING.md']:
 p=DEST/name;s=p.read_text();title,rest=s.split('\n',1)
 note="\n\n## Current checkpoint: test 4\n\nThe owner's Test 3 diagnostics identify the original engine's CFile filename bounds check. An independent original-ARM32 caller probe reproduces the same abort with a trailing directory path. Test 4 adds a narrow engine fopen guard and persistent STL error logging. All five file-opening regressions, native library/hook probes and APK checks pass. The exact phone path and loading-screen repair still require a Fold7 retest. See [TEST4.md](work/fold7-build/TEST4.md) and [BRIDGE-ASSESSMENT.md](work/fold7-build/BRIDGE-ASSESSMENT.md).\n\n[Download test 4](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test4). Install over Test 3, retain cache and saves, repeat the same load with the same options, then export diagnostics. Historical sections below remain for provenance.\n"
 if name=='BUILDING.md':note+='\nThe current native source commit is `'+head+'` (local provenance); apply the cumulative `zettabridge-dh2.patch` to upstream base `7c647a4f1ea150eab7978ab0da28fdf49f3a79de`. The runtime bundle contains the exact test 4 binaries. Use the included JDK 17 toolchain for apktool; run dependent build/package steps sequentially and stop on errors. `build_apk.py` now verifies the nested APK before adding helper DEX. `tests/test_diagnostics.py` runs deliberate abort/exit/fault probes.\n'
 p.write_text(title+note+rest)
# Store this packaging recipe for future handoffs.
shutil.copy2(Path(__file__),DEST/'work/fold7-build/package_test4.py')
files=[]
for p in sorted(DEST.rglob('*')):
 if p.is_file() and p.name!='FILE-MANIFEST.json' and '.rsync-tmp' not in p.parts:
  data=p.read_bytes();files.append({'path':p.relative_to(STAGE).as_posix(),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
(DEST/'FILE-MANIFEST.json').write_text(json.dumps({'schema':1,'snapshot':'Fold7 test 4 compatibility work','files':files},indent=2)+'\n')
p=DEST/'FILE-MANIFEST.json';files.append({'path':p.relative_to(STAGE).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size})
(ROOT/'test4-manifest.json').write_text(json.dumps(files,indent=2)+'\n')
seen=set();dedup=ROOT/'compatibility-work-test4.zip'
with zipfile.ZipFile(dedup,'w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('manifest.json',json.dumps({'files':files}))
 for row in files:
  if row['sha256'] in seen:continue
  z.write(STAGE/row['path'],'blobs/'+row['sha256']);seen.add(row['sha256'])
full=WORK.parent/'deliverables/Dungeon-Hunter-2-Fold7-test4-work.zip'
with zipfile.ZipFile(full,'w',zipfile.ZIP_DEFLATED) as z:
 for row in files:z.write(STAGE/row['path'],row['path'])
# Reuse the verified, traversal-safe restorer against the new exact snapshot.
s=(ROOT/'unpack_compatibility_test3.py').read_text().replace('test 3','test 4').replace('compatibility-work-test3.zip','compatibility-work-test4.zip').replace('d6bf579c31f638fb10a96854d21f40c8ceac4af0023e57977c73abc5ce214a46',hashlib.sha256(dedup.read_bytes()).hexdigest())
(ROOT/'unpack_compatibility_test4.py').write_text(s)
# Keep prior browsable authored files and add all new authored sources/reports, not bulk decompilation.
paths={r['path'] for r in json.loads((ROOT/'repo-test3-entries.json').read_text())}
for row in files:
 name=row['path'];p=STAGE/name
 if (name.startswith('compatibility/upstream-modified/') or name.startswith('compatibility/work/fold7-build/')) and p.suffix in ['.py','.java','.c','.S','.ld','.md','.txt','.json','.patch','.log'] and 'generated-' not in name and p.name!='FILE-MANIFEST.json':
  paths.add(name)
entries=[]
for name in sorted(paths):
 p=STAGE/name
 if p.is_file():
  try:content=p.read_text()
  except UnicodeError:continue
  entries.append({'path':name,'mode':'100644','type':'blob','content':content})
entries.append({'path':'unpack_compatibility.py','mode':'100644','type':'blob','content':s})
(ROOT/'repo-test4-entries.json').write_text(json.dumps(entries))
assets=[WORK.parent/'deliverables/Dungeon-Hunter-2-Fold7-test4.apk',full]
sums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in assets)
(WORK.parent/'deliverables/SHA256SUMS-test4.txt').write_text(sums)
metadata={'files':len(files),'browsable_entries':len(entries),'dedup_bytes':dedup.stat().st_size,'full_bytes':full.stat().st_size,'apk':result,'dedup_sha256':hashlib.sha256(dedup.read_bytes()).hexdigest()}
(ROOT/'test4-package-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n');print(json.dumps(metadata,indent=2))
