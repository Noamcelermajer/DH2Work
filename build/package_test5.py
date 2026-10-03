#!/usr/bin/env python3
"""Preserve Test 5 sources, evidence and exact inputs as a verifiable snapshot."""
from pathlib import Path
import hashlib, json, shutil, zipfile, re
ROOT=Path(__file__).resolve().parent; WORK=ROOT.parent; BUILD=WORK/'fold7-build'
STAGE=ROOT/'staging-test5'; DEST=STAGE/'compatibility'
OLD=ROOT/'staging-test4/compatibility'
if DEST.exists():shutil.rmtree(DEST)
shutil.copytree(OLD,DEST)
db=DEST/'work/fold7-build'
for p in BUILD.iterdir():
 if p.is_file() and p.suffix!='.apk':shutil.copy2(p,db/p.name)
for name in ['java','guest-java','tests','notices','phone-test4']:
 shutil.copytree(BUILD/name,db/name,dirs_exist_ok=True)
shutil.copytree(BUILD/'game-tree/smali',db/'generated-game-smali',dirs_exist_ok=True)
shutil.rmtree(db/'generated-helper-smali')
shutil.copytree(BUILD/'packaged-test5-smali/smali_classes2',db/'generated-helper-smali')
shutil.copy2(BUILD/'out/AndroidManifest.xml',db/'host-AndroidManifest.xml')
shutil.copy2(Path(__file__),db/'package_test5.py')
note='''

## Current checkpoint: test 5

Test 4's device report confirms its directory guard ran, then shows a different failure while reopening the prince model: the cache root is duplicated. The original engine treats Android absolute paths as relative whenever WorkingDirectory is nonempty. A 20-byte ARM fix adds POSIX absolute-path recognition while retaining relative and colon-path behavior. The actual original/fixed engine tests confirm the failed open and correct byte reads after repair.

Thirteen path cases pass, as do five earlier file cases, native library/hook checks and final APK validation. The final model-open logging also passes two focused path probes. This is a new test APK; loading and gameplay on the Fold7 remain unverified. Russian menus, display behavior, the earlier GL error and 16 KB pages are still open. See [TEST5.md](work/fold7-build/TEST5.md).

[Download Test 5](https://github.com/Noamcelermajer/DH_sc/releases/tag/v1.0.2-fold7-test5). Install over Test 4, keep the same settings and existing cache, repeat the load, then export DH2-test5-diagnostics.zip. Test 5 modifies the engine binary itself; earlier statements that the engine is unchanged are historical.
'''
for name in ['README.md','FINDINGS.md','ISSUES.md','DEVICE-TESTING.md','BUILDING.md']:
 p=DEST/name;title,rest=p.read_text().split('\n',1)
 rest=rest.replace('## Current checkpoint:','## Historical checkpoint:')
 p.write_text(title+note+rest)
files=[]
for p in sorted(DEST.rglob('*')):
 if p.is_file() and p.name!='FILE-MANIFEST.json' and '.rsync-tmp' not in p.parts:
  data=p.read_bytes();files.append({'path':p.relative_to(STAGE).as_posix(),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
manifest=DEST/'FILE-MANIFEST.json'
manifest.write_text(json.dumps({'schema':1,'snapshot':'Fold7 test 5 compatibility work','files':files},indent=2)+'\n')
files.append({'path':manifest.relative_to(STAGE).as_posix(),'sha256':hashlib.sha256(manifest.read_bytes()).hexdigest(),'bytes':manifest.stat().st_size})
(ROOT/'test5-manifest.json').write_text(json.dumps(files,indent=2)+'\n')
dedup=ROOT/'compatibility-work-test5.zip';seen=set()
with zipfile.ZipFile(dedup,'w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('manifest.json',json.dumps({'files':files}))
 for row in files:
  if row['sha256'] not in seen:z.write(STAGE/row['path'],'blobs/'+row['sha256']);seen.add(row['sha256'])
full=WORK.parent/'deliverables/Dungeon-Hunter-2-Fold7-test5-work.zip'
with zipfile.ZipFile(full,'w',zipfile.ZIP_DEFLATED) as z:
 for row in files:z.write(STAGE/row['path'],row['path'])
s=(ROOT/'unpack_compatibility_test4.py').read_text().replace('test 4','test 5').replace('compatibility-work-test4.zip','compatibility-work-test5.zip')
oldhash=hashlib.sha256((ROOT/'compatibility-work-test4.zip').read_bytes()).hexdigest()
assert oldhash in s
s=s.replace(oldhash,hashlib.sha256(dedup.read_bytes()).hexdigest())
(ROOT/'unpack_compatibility_test5.py').write_text(s)
entries=[]
for row in files:
 path=row['path'];p=STAGE/path;prior=ROOT/'staging-test4'/path
 if p.suffix not in ['.py','.java','.c','.S','.ld','.md','.txt','.json','.patch','.log']:continue
 if p.name=='FILE-MANIFEST.json' or 'generated-' in path:continue
 if prior.exists() and hashlib.sha256(prior.read_bytes()).hexdigest()==row['sha256']:continue
 try:content=p.read_text()
 except UnicodeError:continue
 entries.append({'path':path,'mode':'100644','type':'blob','content':content})
entries.append({'path':'unpack_compatibility.py','mode':'100644','type':'blob','content':s})
(ROOT/'repo-test5-entries.json').write_text(json.dumps(entries))
apk=WORK.parent/'deliverables/Dungeon-Hunter-2-Fold7-test5.apk'
sums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in [apk,full])
(WORK.parent/'deliverables/SHA256SUMS-test5.txt').write_text(sums)
metadata={'files':len(files),'changed_browsable_files':len(entries),'dedup_bytes':dedup.stat().st_size,'full_bytes':full.stat().st_size,'apk_sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'dedup_sha256':hashlib.sha256(dedup.read_bytes()).hexdigest()}
(ROOT/'test5-package-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2))
