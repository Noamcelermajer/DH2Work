#!/usr/bin/env python3
"""Static package/integrity checks. These are NOT Android runtime tests."""
import hashlib
import json
from pathlib import Path
import zipfile
from loguru import logger
logger.disable('androguard')
from androguard.core.apk import APK
from androguard.core.dex import DEX

root = Path(__file__).resolve().parent
source = root.parent / 'upload/Dungeon-Hunter-2-HD-v1-0-2.apk'
candidate = root / 'signed/Dungeon-Hunter-2-compat-arm32-experimental-aligned-signed.apk'
apk = APK(str(candidate))
checks = {}
checks['valid_apk_structure'] = apk.is_valid_APK()
checks['package_unchanged'] = apk.get_package() == 'com.gameloft.android.GAND.GloftD2SS'
checks['minimum_sdk_21'] = apk.get_min_sdk_version() == '21'
checks['target_sdk_24'] = apk.get_target_sdk_version() == '24'
checks['version_code_103'] = apk.get_androidversion_code() == '103'
checks['launcher_preserved'] = apk.get_main_activity() == 'com.gameloft.android.GAND.GloftD2SS.Zirconia_DRM'
checks['no_added_permissions'] = set(apk.get_permissions()) <= set(APK(str(source)).get_permissions())
ns = '{http://schemas.android.com/apk/res/android}'
app = apk.get_android_manifest_xml().find('application')
checks['startup_application_registered'] = app.get(ns + 'name') == 'local.dh2.compat.CompatApplication'
checks['native_library_extraction_enabled'] = app.get(ns + 'extractNativeLibs') == 'true'
checks['legacy_http_dependency_declared'] = any(
    x.get(ns + 'name') == 'org.apache.http.legacy' for x in app.findall('uses-library'))
with zipfile.ZipFile(source) as original, zipfile.ZipFile(candidate) as rebuilt:
    checks['zip_crc'] = rebuilt.testzip() is None
    native = [n for n in rebuilt.namelist() if n.startswith('lib/')]
    checks['exactly_three_armv7_libraries'] = set(native) == {
        'lib/armeabi-v7a/libDungeonHunter2.so', 'lib/armeabi-v7a/libStormGLOFT.so',
        'lib/armeabi-v7a/libnativeinterface.so'}
    checks['native_libraries_byte_identical'] = all(original.read(n) == rebuilt.read(n) for n in native)
    preserved = [n for n in original.namelist() if n.startswith(('assets/', 'res/raw/', 'i18n/'))]
    checks['assets_raw_data_translations_unchanged'] = all(original.read(n) == rebuilt.read(n) for n in preserved)
    dex = DEX(rebuilt.read('classes.dex'))
    classes = {c.get_name(): c for c in dex.get_classes()}
    checks['compat_classes_present'] = all(n in classes for n in (
        'Llocal/dh2/compat/CompatApplication;', 'Llocal/dh2/compat/PhoneCompat;'))
    helper_calls = 0
    unguarded_calls = 0
    startup_call = False
    for name, klass in classes.items():
        for method in klass.get_methods():
            for instruction in method.get_instructions():
                output = instruction.get_output()
                if 'Llocal/dh2/compat/PhoneCompat;->' in output:
                    helper_calls += 1
                if name != 'Llocal/dh2/compat/PhoneCompat;' and any(
                    'Landroid/telephony/TelephonyManager;->' + m + '(' in output
                    for m in ('getDeviceId', 'getSubscriberId', 'getLine1Number')):
                    unguarded_calls += 1
                if name == 'Llocal/dh2/compat/CompatApplication;' and '->getExternalFilesDir(' in output:
                    startup_call = True
    checks['eleven_phone_calls_redirected'] = helper_calls == 11
    checks['no_remaining_unguarded_identifier_calls'] = unguarded_calls == 0
    checks['startup_creates_external_directory'] = startup_call
checks['v1_signature_present'] = apk.is_signed_v1()
checks['v2_signature_present'] = apk.is_signed_v2()
checks['v3_signature_present'] = apk.is_signed_v3()
result = {'checks': checks, 'static_checks_passed': all(checks.values()),
          'output_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
          'runtime_testing': 'NOT PERFORMED: no connected Android device or emulator',
          'cache_verification': 'NOT PERFORMED: uploaded cache ZIP unavailable',
          'arm64_support': False, 'modern_android_playability': 'UNVERIFIED'}
(root / 'validation.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['static_checks_passed'] else 1)
