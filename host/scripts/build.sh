#!/bin/bash
# Configure and build the DH2Work own host.
#
#   build.sh [build-dir] [extra cmake args...]
#
# DH2_DYNARMIC_DIR must point at the patched Dynarmic checkout (scripts/fetch-dynarmic.sh).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
DYNARMIC_DIR="${DH2_DYNARMIC_DIR:-$REPO/third_party/dynarmic}"
BUILD_DIR="${1:-${DH2_BUILD_DIR:-$HOME/dh2-build/host}}"
shift || true

if [ ! -f "$DYNARMIC_DIR/CMakeLists.txt" ]; then
  echo "build: DH2_DYNARMIC_DIR ($DYNARMIC_DIR) is not a Dynarmic checkout." >&2
  echo "build: run host/scripts/fetch-dynarmic.sh first." >&2
  exit 1
fi

cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${DH2_BUILD_TYPE:-RelWithDebInfo}" \
  -DCMAKE_C_COMPILER="${DH2_CC:-clang}" \
  -DCMAKE_CXX_COMPILER="${DH2_CXX:-clang++}" \
  -DDH2_DYNARMIC_DIR="$DYNARMIC_DIR" \
  "$@"

cmake --build "$BUILD_DIR" -j "${DH2_JOBS:-$(nproc)}"
echo "build: done"
echo "build: dh2run      -> $BUILD_DIR/dh2run"
echo "build: dh2selftest -> $BUILD_DIR/dh2selftest"
