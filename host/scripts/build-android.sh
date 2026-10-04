#!/bin/bash
# Build the host as an Android/arm64 binary, and (with --run) execute it off-device under qemu.
#
#   build-android.sh [--static] [--run] [build-dir]
#
# Needs ANDROID_NDK_ROOT (an NDK whose clang builds this tree; r26d is known good) and
# DH2_DYNARMIC_DIR (default ~/dh2/dynarmic-patched).
#
# Two things this script exists to record, both of which cost time to find:
#
#   * The NDK must not be too new. NDK r29 fails on the fmt 10 bundled with Dynarmic
#     ("call to consteval function ... is not a constant expression"). That is not a clang-version
#     problem -- clang 14 fails identically -- it is r29's libc++ being newer than fmt 10 expects.
#     r26d (clang 17) builds it.
#   * Boost 1.74 needs BOOST_NO_CXX98_FUNCTION_BASE under C++20, or its container_hash uses the
#     std::unary_function that C++17 removed.
#
# --static links against bionic statically. Recent NDKs ship no /system/bin/linker64, so a
# dynamically linked Android binary cannot be executed off-device; a static one can, which is what
# turns "it links" into "it runs and passes its tests".
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD=""
STATIC=""
RUN=""
for argument in "$@"; do
  case "$argument" in
    --static) STATIC=1 ;;
    --run) RUN=1 ;;
    -*) echo "build-android: unknown option $argument" >&2; exit 2 ;;
    *) BUILD="$argument" ;;
  esac
done
BUILD="${BUILD:-${DH2_ANDROID_BUILD:-$HOME/dh2-build/host-android${STATIC:+-static}}}"
NDK="${ANDROID_NDK_ROOT:-$HOME/android-ndk-r26d}"
DYNARMIC="${DH2_DYNARMIC_DIR:-$HOME/dh2/dynarmic-patched}"

export ANDROID_NDK_ROOT="$NDK"
[ -d "$NDK" ] || { echo "build-android: no NDK at $NDK" >&2; exit 1; }
[ -d /tmp/dh2boost/boost ] || { mkdir -p /tmp/dh2boost && cp -r /usr/include/boost /tmp/dh2boost/; }

LINK_FLAGS=""
[ -n "$STATIC" ] && LINK_FLAGS="-static"

cmake -S "$HERE" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="${ANDROID_API:-android-24}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
  -DCMAKE_CXX_FLAGS="-DBOOST_NO_CXX98_FUNCTION_BASE" \
  ${LINK_FLAGS:+-DCMAKE_EXE_LINKER_FLAGS=$LINK_FLAGS} \
  -DDH2_DYNARMIC_DIR="$DYNARMIC" \
  -DBoost_NO_SYSTEM_PATHS=ON -DBoost_INCLUDE_DIR=/tmp/dh2boost -DBoost_NO_BOOST_CMAKE=ON
cmake --build "$BUILD" -j "${JOBS:-12}"

echo "build-android: artifacts in $BUILD"
for binary in dh2run dh2selftest dh2link dh2boot; do
  [ -f "$BUILD/$binary" ] && printf '  %-12s %s\n' "$binary" "$(file -b "$BUILD/$binary" | cut -c1-60)"
done

if [ -n "$RUN" ]; then
  echo "build-android: running under qemu"
  qemu-aarch64-static "$BUILD/dh2selftest"
  WRAPPER="$(mktemp)"
  cat > "$WRAPPER" <<EOF
#!/bin/bash
shift
guest=\$1; shift
exec qemu-aarch64-static "$BUILD/dh2run" --quiet "\$guest" "\$@"
EOF
  chmod +x "$WRAPPER"
  "$HERE/scripts/run-guests.sh" "$WRAPPER" "${DH2_GUEST_OUT:-$HOME/dh2-build/guests}"
  rm -f "$WRAPPER"
fi
