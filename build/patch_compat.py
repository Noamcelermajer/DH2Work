#!/usr/bin/env python3
"""Apply bounded Android compatibility changes to an apktool-decoded DH2 tree.

This does not translate or rebuild ARM native machine code.
"""
from pathlib import Path
import difflib
import json
import re
import shutil
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
source = ROOT / 'decoded'
dest = ROOT / 'patched'
if dest.exists():
    raise SystemExit('patched already exists; use a fresh work directory')
shutil.copytree(source, dest)
NS = 'http://schemas.android.com/apk/res/android'
ET.register_namespace('android', NS)
A = lambda name: '{' + NS + '}' + name
manifest = dest / 'AndroidManifest.xml'
tree = ET.parse(manifest)
root = tree.getroot()
app = root.find('application')
assert app is not None and app.get(A('name')) is None
app.set(A('name'), 'local.dh2.compat.CompatApplication')
# Preserve the original pre-API-14 UI acceleration behavior. GLES uses its own surface.
app.set(A('hardwareAccelerated'), 'false')
app.set(A('extractNativeLibs'), 'true')
app.set(A('resizeableActivity'), 'false')
ET.SubElement(app, 'uses-library', {A('name'): 'org.apache.http.legacy', A('required'): 'false'})
for activity in app.findall('activity'):
    # Preserve the original implied exported state explicitly.
    activity.set(A('exported'), 'true' if activity.find('intent-filter') is not None else 'false')
ET.indent(tree, space='    ')
tree.write(manifest, encoding='utf-8', xml_declaration=True)
yml = dest / 'apktool.yml'
text = yml.read_text()
assert '  minSdkVersion: 8\n' in text
text = text.replace('  minSdkVersion: 8\n', '  minSdkVersion: 21\n  targetSdkVersion: 24\n')
text = text.replace('  versionCode: 102\n', '  versionCode: 103\n')
text = text.replace('  versionName: 1.0.2\n', '  versionName: 1.0.2-compat1-experimental\n')
yml.write_text(text)

methods = ('getDeviceId', 'getSubscriberId', 'getLine1Number')
pattern = re.compile(r'invoke-virtual \{([vp]\d+)\}, Landroid/telephony/TelephonyManager;->('
                     + '|'.join(methods) + r')\(\)Ljava/lang/String;')
changes = []
for path in sorted((dest / 'smali').rglob('*.smali')):
    text = path.read_text()
    replacement, count = pattern.subn(
        lambda m: 'invoke-static {' + m[1] + '}, Llocal/dh2/compat/PhoneCompat;->'
        + m[2] + '(Landroid/telephony/TelephonyManager;)Ljava/lang/String;', text)
    if count:
        path.write_text(replacement)
        changes.append({'file': str(path.relative_to(dest)), 'guarded_phone_calls': count})
assert sum(c['guarded_phone_calls'] for c in changes) == 11

target = dest / 'smali/local/dh2/compat'
target.mkdir(parents=True)
(target / 'CompatApplication.smali').write_text('''.class public Llocal/dh2/compat/CompatApplication;
.super Landroid/app/Application;

.method public constructor <init>()V
    .locals 0
    invoke-direct {p0}, Landroid/app/Application;-><init>()V
    return-void
.end method

.method public onCreate()V
    .locals 2
    invoke-super {p0}, Landroid/app/Application;->onCreate()V
    # Ask Android to create the app-specific external directory before legacy I/O.
    :try_start
    const/4 v0, 0x0
    invoke-virtual {p0, v0}, Landroid/content/Context;->getExternalFilesDir(Ljava/lang/String;)Ljava/io/File;
    move-result-object v0
    if-eqz v0, :unavailable
    invoke-virtual {v0}, Ljava/io/File;->isDirectory()Z
    move-result v1
    if-eqz v1, :unavailable
    return-void
    :try_end
    .catch Ljava/lang/RuntimeException; {:try_start .. :try_end} :failed
    :failed
    move-exception v0
    :unavailable
    const-string v0, "DH2Compat"
    const-string v1, "App-specific external storage is unavailable; game data may not load."
    invoke-static {v0, v1}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;)I
    return-void
.end method
''')
helper = '.class public final Llocal/dh2/compat/PhoneCompat;\n.super Ljava/lang/Object;\n'
for method in methods:
    helper += f'''
.method public static {method}(Landroid/telephony/TelephonyManager;)Ljava/lang/String;
    .locals 1
    if-eqz p0, :unavailable
    :try_start
    invoke-virtual {{p0}}, Landroid/telephony/TelephonyManager;->{method}()Ljava/lang/String;
    move-result-object v0
    :try_end
    .catch Ljava/lang/SecurityException; {{:try_start .. :try_end}} :denied
    return-object v0
    :denied
    move-exception v0
    :unavailable
    # Android may withhold hardware identifiers. Do not fabricate an identifier.
    const/4 v0, 0x0
    return-object v0
.end method
'''
(target / 'PhoneCompat.smali').write_text(helper)

# This ABI directory only contains a DRM helper, with no engine; it is not runnable.
shutil.rmtree(dest / 'lib/armeabi')

diff = []
for path in sorted(dest.rglob('*')):
    if not path.is_file() or path.suffix not in ('.smali', '.xml', '.yml'):
        continue
    rel = path.relative_to(dest)
    if rel.parts[0] in ('original', 'build', 'dist'):
        continue
    old = source / rel
    before = old.read_text() if old.exists() else ''
    after = path.read_text()
    if before != after:
        diff.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                        fromfile='original/' + str(rel), tofile='patched/' + str(rel)))
(ROOT / 'compatibility.patch').write_text(''.join(diff))
(ROOT / 'patch-summary.json').write_text(json.dumps(changes, indent=2))
print(json.dumps({'changed_phone_calls': sum(c['guarded_phone_calls'] for c in changes),
                  'target_sdk': 24, 'native_engine_modified': False}, indent=2))
