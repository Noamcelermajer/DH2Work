#!/usr/bin/env python3
from pathlib import Path
import io,json,hashlib,zipfile,subprocess
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parent;WORK=ROOT.parent
apk=WORK.parent/'deliverables/Dungeon-Hunter-2-Fold7-test5.apk';checks={}
with zipfile.ZipFile(apk) as z:
    checks['apk_crc_ok']=z.testzip() is None
    names=z.namelist();checks['no_duplicate_zip_entries']=len(names)==len(set(names))
    checks['only_arm64_host_libraries']=all(n.startswith('lib/arm64-v8a/') for n in names if n.startswith('lib/') and n.endswith('.so'))
    elf=ELFFile(io.BytesIO(z.read('lib/arm64-v8a/libzbridge.so')))
    checks['host_is_aarch64']=elf['e_machine']=='EM_AARCH64'
    checks['host_loads_align_16k']=all(s['p_align']>=16384 for s in elf.iter_segments() if s['p_type']=='PT_LOAD')
    checks['native_alias_api_exported']=any(s.name=='Java_com_zettabridge_core_ZBridge_addPathAlias' for s in elf.get_section_by_name('.dynsym').iter_symbols())
    with zipfile.ZipFile(io.BytesIO(z.read('assets/dh2/game.apk'))) as game:
        checks['guest_crc_ok']=game.testzip() is None
        checks['engine_path_fix_packaged']=hashlib.sha256(game.read('lib/armeabi-v7a/libDungeonHunter2.so')).hexdigest()==json.loads((ROOT/'engine-patch-report.json').read_text())['output_sha256']
        patch=json.loads((ROOT/'storm-patch-report.json').read_text())
        checks['storm_fix_packaged']=hashlib.sha256(game.read('lib/armeabi-v7a/libStormGLOFT.so')).hexdigest()==patch['output_sha256']
        checks['path_helper_present']=b'Llocal/dh2/compat/GamePaths;' in game.read('classes2.dex')
        checks['game_trace_helper_present']=b'Llocal/dh2/compat/GameTrace;' in game.read('classes2.dex')
        checks['game_trace_helper_referenced']=b'Llocal/dh2/compat/GameTrace;' in game.read('classes.dex')
        checks['media_query_helper_present']=b'Llocal/dh2/compat/MediaQueries;' in game.read('classes2.dex')
        checks['media_query_helper_referenced']=b'Llocal/dh2/compat/MediaQueries;' in game.read('classes.dex')
    checks['setup_and_cache_classes_present']=all(c in z.read('classes.dex') for c in [b'Lcom/zettabridge/launcher/Dh2Activity;',b'Lcom/zettabridge/launcher/CacheArchive;',b'Lcom/zettabridge/launcher/Dh2Diagnostics;'])
    for name in z.namelist():
        if name.startswith('assets/zb/'):
            assert z.read(name)==(WORK/'research/ZettaBridge/build/launcher'/name).read_bytes(),name
    checks['runtime_matches_tested_build']=True
badging=subprocess.check_output([str(WORK/'android-sdk/build-tools/35.0.0/aapt'),'dump','badging',str(apk)],text=True)
(ROOT/'apk-badging.txt').write_text(badging)
checks['application_id_unique']="package: name='local.dh2.fold7'" in badging
checks['main_activity_correct']="launchable-activity: name='com.zettabridge.launcher.Dh2Activity'" in badging
checks['min_sdk_29']="sdkVersion:'29'" in badging
checks['target_sdk_35']="targetSdkVersion:'35'" in badging
checks['upgrade_version_5']="versionCode='5'" in badging and "versionName='1.0-test5'" in badging
checks['native_hook_test_passed']='PASS shader and GL-string GOT hooks and original inline hook are installed' in (ROOT/'native-load-test.log').read_text()
assert all(checks.values()),checks
report={'checks':checks,'sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'bytes':apk.stat().st_size,'cache_test_cases':13,'native_probe_inputs':4096,'native_entry_points_tested_with_inert_vm':True,'android_art_tested':False,'phone_tested':False,'gameplay_tested':False,'requires_host_page_size':4096}
(ROOT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
