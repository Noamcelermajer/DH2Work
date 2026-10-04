#!/usr/bin/env bash
# DH2Work: build and run the T32 ARMv8 patch test against a Dynarmic checkout.
#
# Usage:
#   build_and_run.sh --dynarmic <checkout> --build <build-dir> [--expect-undefined]
#
# --expect-undefined runs the same bytes in "pristine" mode, where every added instruction
# must raise Exception::UndefinedInstruction. Use it against a pristine pin checkout for the
# A/B evidence; use it without the flag against the patched tree.
#
# The T32-capable LLVM toolchain defaults to NDK r29, then falls back to the PATH tools.
# Override the directory with T32_TOOLCHAIN.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DYNARMIC=""
BUILD=""
EXTRA=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dynarmic) DYNARMIC="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --expect-undefined) EXTRA+=(--expect-undefined); shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -n "$DYNARMIC" ]] || { echo "--dynarmic is required" >&2; exit 2; }
[[ -n "$BUILD" ]] || { echo "--build is required" >&2; exit 2; }

if [[ -n "${T32_TOOLCHAIN:-}" ]]; then
    TOOL="${T32_TOOLCHAIN}"
elif [[ -d "$HOME/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin" ]]; then
    TOOL="$HOME/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin"
else
    TOOL=""
fi

if [[ -n "$TOOL" ]]; then
    CLANG="$TOOL/clang"
    OBJCOPY="$TOOL/llvm-objcopy"
    OBJDUMP="$TOOL/llvm-objdump"
else
    CLANG="${CLANG:-clang}"
    OBJCOPY="${OBJCOPY:-llvm-objcopy}"
    OBJDUMP="${OBJDUMP:-llvm-objdump}"
fi

WORK="$BUILD/generated"
mkdir -p "$WORK"

"$CLANG" --target=thumbv8a-none-none-eabi -c "$HERE/t32_armv8_seq.s" -o "$WORK/t32_armv8_seq.o"
"$OBJDUMP" -d --triple=thumbv8a "$WORK/t32_armv8_seq.o" > "$WORK/t32_armv8_seq.dis"
"$OBJCOPY" -O binary --only-section=.text "$WORK/t32_armv8_seq.o" "$WORK/t32_armv8_seq.bin"
python3 "$HERE/gen_seq_inc.py" "$WORK/t32_armv8_seq.bin" "$WORK/t32_armv8_seq.dis" "$WORK/t32_armv8_seq.inc"

cmake -S "$HERE" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DDYNARMIC_SRC="$DYNARMIC" \
    -DT32_GENERATED_DIR="$WORK"
cmake --build "$BUILD"

set +u
"$BUILD/t32_armv8_test" "${EXTRA[@]}"
