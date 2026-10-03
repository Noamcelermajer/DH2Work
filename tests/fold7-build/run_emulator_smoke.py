#!/usr/bin/env python3
"""Boot an existing AVD, install the APK, and collect launcher smoke evidence.

Run emulator and adb together: some workspaces isolate networking per command.
This does not supply the game cache or validate loading/gameplay on a Fold7.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', required=True, type=Path)
    p.add_argument('--avd-home', required=True, type=Path)
    p.add_argument('--avd', required=True)
    p.add_argument('--apk', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--software-cpu', action='store_true')
    p.add_argument('--boot-timeout', type=int, default=600)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    for name in ('result.json', 'install.txt', 'launch.txt', 'device-properties.txt',
                 'launcher-ui.xml', 'launcher.png', 'logcat.txt'):
        (a.out / name).unlink(missing_ok=True)
    sdk = a.sdk.resolve()
    env = dict(os.environ, ANDROID_SDK_ROOT=str(sdk),
               ANDROID_AVD_HOME=str(a.avd_home.resolve()))
    adb_base = [str(sdk / 'platform-tools/adb'), '-s', 'emulator-5554']
    result = {'apk_sha256': hashlib.sha256(a.apk.read_bytes()).hexdigest(),
              'avd': a.avd, 'software_cpu': a.software_cpu,
              'booted': False, 'installed': False, 'launcher_ready': False,
              'gameplay_tested': False, 'fold7_tested': False}

    def run(args, timeout=30, binary=False):
        return subprocess.run(args, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=timeout,
                              text=not binary,
                              encoding='utf-8' if not binary else None,
                              errors='replace' if not binary else None)

    def adb(*args, **kwargs):
        return run(adb_base + list(args), **kwargs)

    def save_logcat():
        try:
            r = adb('logcat', '-d', timeout=30)
            (a.out / 'logcat.txt').write_text(r.stdout + r.stderr, encoding='utf-8')
        except Exception as error:
            result['logcat_error'] = str(error)

    proc = None
    try:
        run([str(sdk / 'platform-tools/adb'), 'start-server'])
        args = [str(sdk / 'emulator/emulator'), '-avd', a.avd,
                '-no-window', '-no-audio', '-no-boot-anim', '-no-snapshot',
                '-no-metrics', '-gpu', 'swiftshader_indirect', '-cores', '2',
                '-memory', '2048', '-port', '5554', '-feature', '-Vulkan']
        if a.software_cpu:
            args += ['-accel', 'off']
        (a.out / 'command.json').write_text(json.dumps(args, indent=2) + '\n')
        with (a.out / 'emulator.log').open('w') as log:
            proc = subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT)
        start = time.monotonic()
        last_report = -30
        while time.monotonic() - start < a.boot_timeout:
            if proc.poll() is not None:
                raise RuntimeError('Emulator exited with status ' + str(proc.returncode))
            try:
                r = adb('shell', '-n', 'getprop', 'sys.boot_completed', timeout=10)
                if r.returncode == 0 and r.stdout.strip() == '1':
                    result['booted'] = True
                    result['boot_seconds'] = round(time.monotonic() - start, 1)
                    break
            except subprocess.TimeoutExpired:
                pass
            elapsed = int(time.monotonic() - start)
            if elapsed - last_report >= 30:
                print('Waiting for Android boot:', elapsed, 'seconds', flush=True)
                last_report = elapsed
            time.sleep(3)
        if not result['booted']:
            raise RuntimeError('Android boot timed out')
        print('Android boot completed', result['boot_seconds'], 'seconds', flush=True)
        r = adb('shell', '-n', 'getprop')
        (a.out / 'device-properties.txt').write_text(r.stdout)
        print('Installing APK with full transfer', flush=True)
        r = adb('install', '--no-incremental', '-r', str(a.apk.resolve()), timeout=240)
        (a.out / 'install.txt').write_text(r.stdout + r.stderr)
        result['installed'] = r.returncode == 0 and 'Success' in r.stdout
        if not result['installed']:
            raise RuntimeError('APK installation failed: ' + r.stdout + r.stderr)
        print('APK installed', flush=True)
        r = adb('shell', '-n', 'am', 'start', '-W', '-n',
                'local.dh2.fold7/com.zettabridge.launcher.Dh2Activity', timeout=90)
        (a.out / 'launch.txt').write_text(r.stdout + r.stderr)
        if r.returncode:
            raise RuntimeError('Launcher start failed: ' + r.stdout + r.stderr)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            adb('shell', '-n', 'uiautomator', 'dump', '/sdcard/dh2-smoke-ui.xml', timeout=40)
            r = adb('shell', '-n', 'cat', '/sdcard/dh2-smoke-ui.xml', timeout=20)
            (a.out / 'launcher-ui.xml').write_text(r.stdout)
            try:
                texts = [n.get('text', '') for n in ET.fromstring(r.stdout).iter('node')]
            except ET.ParseError:
                texts = []
            if any(t.startswith('Ready.') for t in texts):
                result['launcher_ready'] = True
                break
            if any('Preparation failed' in t for t in texts):
                result['launcher_error'] = next(t for t in texts if 'Preparation failed' in t)
                break
            time.sleep(3)
        r = adb('exec-out', 'screencap', '-p', timeout=30, binary=True)
        if r.returncode == 0 and r.stdout.startswith(b'\x89PNG'):
            (a.out / 'launcher.png').write_bytes(r.stdout)
        if not result['launcher_ready'] and 'launcher_error' not in result:
            result['launcher_error'] = 'Ready status not observed before timeout'
        print('Launcher ready:', result['launcher_ready'], flush=True)
    except Exception as e:
        result['error'] = str(e)
        print('Smoke test stopped:', e, flush=True)
    finally:
        save_logcat()
        (a.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        if proc is not None and proc.poll() is None:
            try:
                adb('emu', 'kill', timeout=10)
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
        print(json.dumps(result, indent=2), flush=True)
    return 0 if result['launcher_ready'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
