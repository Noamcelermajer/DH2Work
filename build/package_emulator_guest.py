#!/usr/bin/env python3
"""Put a rebuilt Storm guard into an unsigned Test 5 guest for emulator testing."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--base-apk', type=Path, required=True,
                    help='Unsigned Test 5 guest APK, before zipalign and signing')
parser.add_argument('--patched-storm', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.base_apk.resolve() == args.output.resolve():
    raise SystemExit('Output must differ from the base APK')
if b'APK Sig Block 42' in args.base_apk.read_bytes():
    raise SystemExit('Pass an unsigned guest APK; this base contains an APK signature block')

engine_name = 'lib/armeabi-v7a/libDungeonHunter2.so'
storm_name = 'lib/armeabi-v7a/libStormGLOFT.so'
engine_report = json.loads((ROOT/'engine-patch-report.json').read_text())
storm_report = json.loads((ROOT/'storm-patch-report.json').read_text())
digest = lambda b: hashlib.sha256(b).hexdigest()
patched = args.patched_storm.read_bytes()
if digest(patched) != storm_report['output_sha256']:
    raise SystemExit('Storm build differs from the just-verified patch report')
with zipfile.ZipFile(args.base_apk) as source:
    if source.testzip() is not None:
        raise SystemExit('Base APK has a damaged ZIP entry')
    names = source.namelist()
    if names.count(engine_name) != 1 or names.count(storm_name) != 1:
        raise SystemExit('Expected one of each exact ARM32 engine library')
    if any(name.startswith('META-INF/') for name in names):
        raise SystemExit('Pass an unsigned guest APK; this script cannot preserve a signature')
    if digest(source.read(engine_name)) != engine_report['output_sha256']:
        raise SystemExit('Base APK is not the pinned Test 5 engine build')
    # The Test 5 library was built from the same pinned original source.
    if digest(source.read(storm_name)) != 'f031bdd99b1963bc60357872ad583addbf9ac2a8446e3b4d32af1bdd7b5fdb2f':
        raise SystemExit('Base APK is not the pinned Test 5 Storm build')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w') as target:
        for member in source.infolist():
            target.writestr(member, patched if member.filename == storm_name else source.read(member))
with zipfile.ZipFile(args.output) as result:
    if result.testzip() is not None or digest(result.read(storm_name)) != digest(patched):
        raise SystemExit('New guest APK failed ZIP verification')
print(json.dumps({'unsigned_apk': str(args.output),
                  'apk_sha256': digest(args.output.read_bytes()),
                  'engine_sha256': engine_report['output_sha256'],
                  'storm_sha256': digest(patched)}, indent=2))
