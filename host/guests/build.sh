#!/bin/bash
# Build the ARM32 guest test programs with the Android NDK.
#
#   build.sh [output-dir]
#
# The freestanding guests link no C library at all, so a failure names the loader/JIT/syscall
# layer; bionic is the P0 gate and links static bionic.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-${DH2_GUEST_OUT:-$HERE/out}}"
NDK="${DH2_NDK:-$HOME/android-ndk-r29}"
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$BIN/armv7a-linux-androideabi21-clang"

if [ ! -x "$CC" ]; then
  echo "guests: no NDK clang at $CC (set DH2_NDK)" >&2
  exit 1
fi
mkdir -p "$OUT"

ARM="-march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16"
COMMON="-O2 -fno-stack-protector -fno-builtin -Wl,--build-id=none"
FREE="-nostdlib -static -no-pie -Wl,-e,_start -I$HERE/src -Wno-unused-command-line-argument"
START="$HERE/src/start.S"

echo "guests: freestanding ARM"
$CC $ARM $FREE $COMMON "$START" "$HERE/src/hello.c"     -o "$OUT/hello"
$CC $ARM $FREE $COMMON "$START" "$HERE/src/args.c"      -o "$OUT/args"
$CC $ARM $FREE $COMMON "$START" "$HERE/src/vfp.c"       -o "$OUT/vfp"
$CC $ARM $FREE $COMMON "$START" "$HERE/src/exclusive.c" -o "$OUT/exclusive"
$CC $ARM $FREE $COMMON "$START" "$HERE/src/memory.c"    -o "$OUT/memory"

echo "guests: freestanding Thumb-2"
$CC $ARM -mthumb $FREE $COMMON "$START" "$HERE/src/hello.c" -o "$OUT/hello_thumb"

echo "guests: Thumb-2 ARMv8 (needs DH2Work's Dynarmic patch)"
$CC -march=armv8-a -mthumb -mfloat-abi=softfp -mfpu=neon-vfpv4 $FREE $COMMON \
    "$START" "$HERE/src/armv8_t32.c" -o "$OUT/armv8_t32"

echo "guests: static bionic (the P0 gate)"
$CC $ARM -static -O2 "$HERE/src/bionic.c" -o "$OUT/bionic"

echo "guests: built into $OUT"
ls -1 "$OUT"
