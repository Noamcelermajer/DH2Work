#!/bin/bash
# Run the guest suite on an aarch64 build of the host, which is the product architecture.
#
#   run-guests-aarch64.sh [build-dir]
#
# Everything here is off-device: the host is cross-built for aarch64 and executed under
# qemu-aarch64-static (install qemu-user-static). The guest binaries are 32-bit ARM and are
# executed BY dh2run through Dynarmic's arm64 backend -- they must never be handed to qemu
# directly, and the runner must not see run-guests.sh's --quiet flag (qemu would eat it).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
BUILD="${1:-${DH2_ARM64_BUILD:-$HOME/dh2-build/host-arm64}}"
GUESTS="${DH2_GUEST_OUT:-$HOME/dh2-build/guests}"
SYSROOT="${DH2_SYSROOT:-$HOME/zb-src/sysroot/system/lib}"

if [ ! -x "$BUILD/dh2run" ]; then
  echo "run-guests-aarch64: no dh2run in $BUILD; cross-build first, e.g." >&2
  echo "  cmake -S $REPO/host -B $BUILD -G Ninja -DCMAKE_SYSTEM_NAME=Linux \\" >&2
  echo "    -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \\" >&2
  echo "    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DDH2_DYNARMIC_DIR=... \\" >&2
  echo "    -DBoost_NO_SYSTEM_PATHS=ON -DBoost_INCLUDE_DIR=/tmp/dh2boost -DBoost_NO_BOOST_CMAKE=ON" >&2
  exit 1
fi
if ! command -v qemu-aarch64-static >/dev/null; then
  echo "run-guests-aarch64: qemu-aarch64-static is missing (apt install qemu-user-static)" >&2
  exit 1
fi

qemu-aarch64-static -L /usr/aarch64-linux-gnu "$BUILD/dh2selftest"

WRAPPER="$(mktemp)"
cat > "$WRAPPER" <<EOF
#!/bin/bash
shift
guest=\$1; shift
exec qemu-aarch64-static -L /usr/aarch64-linux-gnu "$BUILD/dh2run" --quiet "\$guest" "\$@"
EOF
chmod +x "$WRAPPER"

"$HERE/scripts/run-guests.sh" "$WRAPPER" "$GUESTS"
status=$?
rm -f "$WRAPPER"
exit $status
