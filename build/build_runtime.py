#!/usr/bin/env python3
"""Build the patched upstream core and support libraries using local toolchains."""
from pathlib import Path
import subprocess,os,sys,shutil
ROOT=Path(__file__).resolve().parent;WORK=ROOT.parent
repo=WORK/'research/ZettaBridge';ndk=WORK/'toolchains/android-ndk-r29'
boost=WORK/'toolchains/boost/usr/include'
env=dict(os.environ,NDK=str(ndk),NDK_HOST='linux-x86_64')
if not (repo/'sysroot').exists():
    shutil.copytree(repo/'build/launcher/assets/zb/sysroot',repo/'sysroot')
subprocess.run([sys.executable,'-m','cmake','-S',str(repo),'-B',str(repo/'build/android-arm64'),'-G','Ninja',
               '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a',
               '-DANDROID_PLATFORM=android-29','-DCMAKE_BUILD_TYPE=Release','-DZB_BUILD_TESTS=OFF',
               '-DBoost_INCLUDE_DIR='+str(boost),'-DCMAKE_POLICY_VERSION_MINIMUM=3.5'],check=True,env=env)
subprocess.run([sys.executable,'-m','cmake','--build',str(repo/'build/android-arm64'),'--target','zbridge','zbproxy','zbrun','-j','4'],check=True,env=env)
subprocess.run([str(repo/'tools/build_guest.sh')],check=True,env=env)
subprocess.run([str(repo/'tools/make_launcher_bundle.sh')],check=True,env=env)
