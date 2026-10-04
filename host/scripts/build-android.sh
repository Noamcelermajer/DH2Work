#!/bin/bash
# Build the host as an Android/arm64 binary.
#
#   build-android.sh [build-dir]
#
# Needs ANDROID_NDK_ROOT (default ~/android-ndk-r29) and DH2_DYNARMIC_DIR (default
# ~/dh2/dynarmic-patched). The NDK's bin directory goes on PATH because the toolchain asks clang
# for -fuse-ld=lld and the system ld cannot link aarch64.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
BUILD="${1:-${DH2_ANDROID_BUILD:-$HOME/dh2-build/host-android}}"
NDK="${ANDROID_NDK_ROOT:-$HOME/android-ndk-r29}"
DYNARMIC="${DH2_DYNARMIC_DIR:-$HOME/dh2/dynarmic-patched}"

export ANDROID_NDK_ROOT="$NDK"
export PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"

[ -d /tmp/dh2boost/boost ] || { mkdir -p /tmp/dh2boost && cp -r /usr/include/boost /tmp/dh2boost/; }

cmake -S "$HERE" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/android-clang-toolchain.cmake" \
  -DANDROID_API="${ANDROID_API:-24}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
  -DDH2_DYNARMIC_DIR="$DYNARMIC" \
  -DBoost_NO_SYSTEM_PATHS=ON -DBoost_INCLUDE_DIR=/tmp/dh2boost -DBoost_NO_BOOST_CMAKE=ON
cmake --build "$BUILD" -j "${JOBS:-12}"

echo "build-android: artifacts in $BUILD"
for binary in dh2run dh2selftest dh2link dh2boot; do
  [ -f "$BUILD/$binary" ] && printf '  %-12s %s\n' "$binary" "$(file -b "$BUILD/$binary" | cut -c1-60)"
done
