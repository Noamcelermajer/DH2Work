#!/usr/bin/env python3
"""Build the private DH2 test wrapper from reviewed sources and pinned inputs."""
from pathlib import Path
import os,subprocess,shutil,zipfile,json,hashlib,xml.etree.ElementTree as ET
from standalone_inputs import (COMPLETE_CACHE_SHA256, TEST7_GUEST_SHA256,
                               TEST8_GUEST_SHA256, TEST9_GUEST_SHA256, TEST10_GUEST_SHA256,
                               copy_verified_cache, verify_test7_guest,
                               verify_test8_guest, verify_test9_guest, verify_test10_guest)

ROOT=Path(__file__).resolve().parent
WORK=ROOT.parent
ZB=WORK/'research/ZettaBridge'
SDK=Path(os.environ.get('DH2_ANDROID_SDK_ROOT',WORK/'android-sdk'))
BT=SDK/'build-tools/35.0.0'
ANDROID=Path(os.environ.get('DH2_ANDROID_JAR',SDK/'platforms/android-35/android.jar'))
OUT=ROOT/'out';OUT.mkdir(exist_ok=True)
JDK=Path(os.environ['DH2_JDK_ROOT']) if 'DH2_JDK_ROOT' in os.environ else next((WORK/'toolchains').glob('jdk-17*'))
ENV=dict(os.environ,JAVA_HOME=str(JDK));ENV['PATH']=str(JDK/'bin')+os.pathsep+ENV['PATH']
test7_input = os.environ.get('DH2_TEST7_GUEST_APK')
test8_input = os.environ.get('DH2_TEST8_GUEST_APK')
test9_input = os.environ.get('DH2_TEST9_GUEST_APK')
test10_input = os.environ.get('DH2_TEST10_GUEST_APK')
cache_input = os.environ.get('DH2_CACHE_ZIP')
if sum(bool(value) for value in (test7_input,test8_input,test9_input,test10_input)) > 1:
    raise SystemExit('Choose exactly one pinned DH2_TEST*_GUEST_APK')
guest_input = test10_input or test9_input or test8_input or test7_input
# Bundling the owner cache is normally limited to the pinned, device-verified
# guests.  DH2Work rebuilds its own guest from the owner's original APK, which is
# a new configuration, so it must be opted into explicitly rather than silently
# weakening the guard for the historical pinned builds.
allow_unpinned_bundle = os.environ.get('DH2_ALLOW_UNPINNED_BUNDLED_CACHE') == '1'
if cache_input and not guest_input and not allow_unpinned_bundle:
    raise SystemExit('DH2_CACHE_ZIP requires a pinned standalone guest APK '
                     '(set DH2_ALLOW_UNPINNED_BUNDLED_CACHE=1 to bundle the cache '
                     'alongside a locally rebuilt guest)')
guest_source = Path(guest_input) if guest_input else ROOT/'game-unsigned.apk'
if test7_input:
    verify_test7_guest(guest_source)
if test8_input:
    verify_test8_guest(guest_source)
if test9_input:
    verify_test9_guest(guest_source)
if test10_input:
    verify_test10_guest(guest_source)
if cache_input:
    from standalone_inputs import verify_sha256
    verify_sha256(Path(cache_input), COMPLETE_CACHE_SHA256)
version_code, version_name = ((14, '1.0-test11-language') if test10_input else
                              ((9, '1.0-test9-path') if test9_input else
                              ((8, '1.0-test8-sync') if test8_input else
                               ((7, '1.0-test7') if test7_input else (5, '1.0-test5')))))
# A new revision must be able to raise its identity without editing this script,
# and an on-device upgrade keeps the imported cache and saves only while
# versionCode does not go backwards.  These overrides are authoritative.
if os.environ.get('DH2_VERSION_CODE'):
    version_code = int(os.environ['DH2_VERSION_CODE'])
if os.environ.get('DH2_VERSION_NAME'):
    version_name = os.environ['DH2_VERSION_NAME']

def run(args):
    program=Path(args[0])
    if os.name=='nt' and not program.exists():
        program=next((Path(str(program)+suffix) for suffix in ('.exe','.bat','.cmd') if Path(str(program)+suffix).exists()),program)
    print('Running',str(program),flush=True)
    subprocess.run([str(program),*[str(a) for a in args[1:]]],check=True,env=ENV)

# The historical unsigned guest lacks the helper dex and needs it appended.
# The pinned Test 7 guest already has classes2.dex; appending another would
# create an ambiguous duplicate ZIP entry and could load stale helper code.
if not guest_input:
    guest_classes=OUT/'guest-classes'
    if guest_classes.exists():shutil.rmtree(guest_classes)
    guest_classes.mkdir()
    run([JDK/'bin/javac','-source','8','-target','8','-cp',ANDROID,'-d',guest_classes,*sorted((ROOT/'guest-java').rglob('*.java'))])
    guest_dex=OUT/'guest-dex'
    if guest_dex.exists():shutil.rmtree(guest_dex)
    guest_dex.mkdir()
    run([BT/'d8','--min-api','21','--lib',ANDROID,'--output',guest_dex,*sorted(guest_classes.rglob('*.class'))])
assets=OUT/'assets'
if assets.exists():shutil.rmtree(assets)
shutil.copytree(ZB/'build/launcher/assets',assets,ignore=shutil.ignore_patterns('.rsync-tmp'))
(assets/'dh2').mkdir()
game=assets/'dh2/game.apk'
with zipfile.ZipFile(guest_source) as source_game:
    assert source_game.testzip() is None
    assert {'AndroidManifest.xml','classes.dex','lib/armeabi-v7a/libDungeonHunter2.so'}.issubset(source_game.namelist()), 'Incomplete nested game input'
shutil.copyfile(guest_source,game)
if guest_input:
    with zipfile.ZipFile(game) as z:
        assert z.namelist().count('classes2.dex') == 1, 'Pinned guest helper dex missing or duplicated'
        assert b'Llocal/dh2/compat/GamePaths;' in z.read('classes2.dex'), 'Guest path helper missing'
else:
    with zipfile.ZipFile(game,'a',zipfile.ZIP_DEFLATED) as z:z.write(guest_dex/'classes.dex','classes2.dex')
(assets/'dh2/guest.sha256').write_text(hashlib.sha256(game.read_bytes()).hexdigest()+'\n',encoding='ascii')
if cache_input:
    copy_verified_cache(Path(cache_input), assets/'dh2/cache.zip')
    (assets/'dh2/cache.sha256').write_text(COMPLETE_CACHE_SHA256+'\n',encoding='ascii')

# The Android 17 x86_64 emulator's ARM native-bridge linker configuration looks
# under /system/lib/arm[/bootstrap].  The pinned GSI sysroot stores the same
# ARM32 files under /system/lib.  Supply both paths in the private runtime
# bundle so zbhost can start without a rooted emulator or device setup.
if guest_input:
    sysroot_lib = assets/'zb/sysroot/system/lib'
    arm_lib = sysroot_lib/'arm'
    arm_lib.mkdir()
    for library in sorted(sysroot_lib.glob('*.so')):
        shutil.copyfile(library, arm_lib/library.name)
    bootstrap = arm_lib/'bootstrap'
    bootstrap.mkdir()
    for name in ('libc.so', 'libm.so', 'libdl.so', 'libdl_android.so'):
        shutil.copyfile(sysroot_lib/name, bootstrap/name)
    runtime_files = sorted(p.relative_to(assets).as_posix() for p in (assets/'zb').rglob('*') if p.is_file())
    (assets/'zb-files.txt').write_text(''.join(name+'\n' for name in runtime_files), encoding='ascii')
    runtime_identity = hashlib.sha256()
    for name in runtime_files:
        runtime_identity.update(name.encode('ascii')+b'\0')
        with (assets/name).open('rb') as source:
            for block in iter(lambda: source.read(1024*1024), b''):
                runtime_identity.update(block)
    (assets/'zb-version.txt').write_text(runtime_identity.hexdigest()+'\n', encoding='ascii')

classes=OUT/'classes'
if classes.exists():shutil.rmtree(classes)
classes.mkdir()
sources=sorted((ZB/'android/launcher/app/src/main/java').rglob('*.java'))+sorted((ROOT/'java').rglob('*.java'))
classpath=str(ANDROID)+os.pathsep+str(WORK/'downloads/hiddenapibypass.jar')
run([JDK/'bin/javac','-source','17','-target','17','-cp',classpath,'-d',classes,*sources])
jar=OUT/'launcher.jar'
run([JDK/'bin/jar','cf',jar,'-C',classes,'.'])
dex=OUT/'dex'
if dex.exists():shutil.rmtree(dex)
dex.mkdir()
run([BT/'d8','--release','--min-api','29','--lib',ANDROID,'--output',dex,jar,WORK/'downloads/hiddenapibypass.jar'])

# Generate fully qualified component names before changing the application ID.
NS='http://schemas.android.com/apk/res/android';TOOLS='http://schemas.android.com/tools'
ET.register_namespace('android',NS)
a=lambda n:'{'+NS+'}'+n
tree=ET.parse(ZB/'android/launcher/app/src/main/AndroidManifest.xml');manifest=tree.getroot()
manifest.set('package','local.dh2.fold7');manifest.set(a('versionCode'),str(version_code));manifest.set(a('versionName'),version_name)
ET.SubElement(manifest,'uses-sdk',{a('minSdkVersion'):'29',a('targetSdkVersion'):'35'})
for perm in ['android.permission.ACCESS_WIFI_STATE','android.permission.CHANGE_WIFI_STATE','android.permission.BLUETOOTH','android.permission.BLUETOOTH_ADMIN']:
    ET.SubElement(manifest,'uses-permission',{a('name'):perm})
app=manifest.find('application');app.set(a('label'),'Dungeon Hunter 2' if guest_input else 'Dungeon Hunter 2 - Fold7 Test');app.set(a('icon'),'@drawable/dh2_icon');app.set(a('extractNativeLibs'),'true');app.set(a('usesCleartextTraffic'),'true')
ET.SubElement(app,'uses-library',{a('name'):'org.apache.http.legacy',a('required'):'false'})
for node in [app,*list(app)]:
    name=node.get(a('name'),'')
    if name.startswith('.'):node.set(a('name'),'com.zettabridge.launcher'+name)
    if node.get(a('taskAffinity')):node.set(a('taskAffinity'),node.get(a('taskAffinity')).replace('com.zettabridge.launcher','local.dh2.fold7'))
    if node.get('{'+TOOLS+'}node')=='remove':app.remove(node)
for node in app.findall('activity'):
    if node.get(a('name'))=='com.zettabridge.launcher.LibraryActivity':
        node.set(a('name'),'com.zettabridge.launcher.Dh2Activity')
        node.set(a('configChanges'),'orientation|screenSize|screenLayout|smallestScreenSize|keyboardHidden')
    else:node.set(a('exported'),'false')
manifest_path=OUT/'AndroidManifest.xml';tree.write(manifest_path,encoding='utf-8',xml_declaration=True)

# Ship upstream licensing notices with the personal compatibility build.
notices=assets/'licenses';notices.mkdir(exist_ok=True)
shutil.copyfile(ZB/'LICENSE',notices/'ZettaBridge.txt')
shutil.copyfile(ZB/'third_party/README.md',notices/'Third-party.txt')
for i,p in enumerate((ZB/'third_party/dynarmic').glob('LICENSE*')):
    if p.is_file():shutil.copyfile(p,notices/('Dynarmic-'+str(i)+'.txt'))
for p in (ZB/'third_party/dynarmic/externals').rglob('*'):
    if p.is_file() and p.name.upper().startswith(('LICENSE','COPYING')):
        relative=str(p.relative_to(ZB/'third_party/dynarmic/externals')).replace('/','_')
        shutil.copyfile(p,notices/('Dependency-'+relative))
for p in (ROOT/'notices').glob('*'):
    if p.is_file():shutil.copyfile(p,notices/p.name)
with zipfile.ZipFile(WORK/'downloads/hiddenapibypass-6.1.aar') as z:
    for name in z.namelist():
        if 'LICENSE' in name.upper() or 'NOTICE' in name.upper():
            (notices/Path(name).name).write_bytes(z.read(name))

unsigned=OUT/'dh2-unsigned.apk'
res=OUT/'res/drawable'
if res.parent.exists():shutil.rmtree(res.parent)
res.mkdir(parents=True)
shutil.copyfile(WORK/'patched/res/drawable/icon.png',res/'dh2_icon.png')
compiled_res=OUT/'compiled-res.zip'
run([BT/'aapt2','compile','--dir',OUT/'res','-o',compiled_res])
run([BT/'aapt2','link','-o',unsigned,'--manifest',manifest_path,'-I',ANDROID,'-A',assets,compiled_res])
with zipfile.ZipFile(unsigned,'a',zipfile.ZIP_DEFLATED) as z:
    for p in dex.glob('*.dex'):z.write(p,p.name)
    for p in (ZB/'build/launcher/jniLibs/arm64-v8a').glob('*.so'):z.write(p,'lib/arm64-v8a/'+p.name)
aligned=OUT/'dh2-aligned.apk'
run([BT/'zipalign','-f','-P','16','4',unsigned,aligned])
deliverable=Path(os.environ['DH2_OUTPUT_APK']) if 'DH2_OUTPUT_APK' in os.environ else WORK.parent/'deliverables'/('Dungeon-Hunter-2-Android17-test11-language.apk' if test10_input else ('Dungeon-Hunter-2-Android17-test9-path.apk' if test9_input else ('Dungeon-Hunter-2-Android17-test8-sync.apk' if test8_input else ('Dungeon-Hunter-2-Android17-test7.apk' if test7_input else 'Dungeon-Hunter-2-Fold7-test5.apk'))))
deliverable.parent.mkdir(parents=True,exist_ok=True)
run([BT/'apksigner','sign','--ks',WORK/'dh2-local-test.p12','--ks-key-alias','dh2-local-test','--ks-pass','pass:dh2-local-test-only','--key-pass','pass:dh2-local-test-only','--out',deliverable,aligned])
run([BT/'apksigner','verify','--verbose',deliverable])
run([BT/'zipalign','-c','-P','16','4',deliverable])
(ROOT/'build-result.json').write_text(json.dumps({'apk':str(deliverable),'sha256':hashlib.sha256(deliverable.read_bytes()).hexdigest(),'bytes':deliverable.stat().st_size,'upstream_commit':subprocess.check_output(['git','-c',f'safe.directory={ZB.resolve()}','-C',str(ZB),'rev-parse','HEAD'],text=True).strip(),'host_abi':'arm64-v8a','guest_abi':'armeabi-v7a','guest_input_sha256':TEST10_GUEST_SHA256 if test10_input else (TEST9_GUEST_SHA256 if test9_input else (TEST8_GUEST_SHA256 if test8_input else (TEST7_GUEST_SHA256 if test7_input else None))),'cache_bundled':bool(cache_input),'cache_input_sha256':COMPLETE_CACHE_SHA256 if cache_input else None,'device_tested':False,'gameplay_tested':False},indent=2)+'\n')
print('Built signed ARM64 local test package:',deliverable,flush=True)
