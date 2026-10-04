#!/bin/bash
# Reproduce every number docs/HOST-REBUILD.md claims, from a clean checkout.
#
#   reproduce.sh [--clean] [--arm64]
#
#   --clean   refetch and rebuild Dynarmic and the guests from scratch
#   --arm64   additionally cross-build for aarch64 and run the suite under qemu
#
# The engine path comes from DH2_ENGINE; without it the engine-dependent steps are skipped with
# a note rather than quietly omitted. This script exists because the previous own-host tree was
# never committed and was lost, so nothing here may depend on state outside the repository.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CLEAN=""
ARM64=""
for argument in "$@"; do
  case "$argument" in
    --clean) CLEAN=1 ;;
    --arm64) ARM64=1 ;;
    *) echo "reproduce: unknown argument $argument" >&2; exit 2 ;;
  esac
done

DYNARMIC="${DH2_DYNARMIC_DIR:-$REPO/third_party/dynarmic}"
BUILD="${DH2_BUILD_DIR:-$HOME/dh2-build/reproduce}"
GUESTS="${DH2_GUEST_OUT:-$HOME/dh2-build/reproduce-guests}"
ENGINE="${DH2_ENGINE:-}"

step() { printf '\n=== %s ===\n' "$1"; }

step "1/6 Dynarmic at the pin, with our patches"
if [ -n "$CLEAN" ] && [ -d "$DYNARMIC" ]; then rm -rf "$DYNARMIC"; fi
"$HERE/scripts/fetch-dynarmic.sh" "$DYNARMIC"

step "2/6 the host"
if [ -n "$CLEAN" ] && [ -d "$BUILD" ]; then rm -rf "$BUILD"; fi
DH2_DYNARMIC_DIR="$DYNARMIC" DH2_BUILD_DIR="$BUILD" "$HERE/scripts/build.sh"

step "3/6 the ARM32 guests"
if [ -n "$CLEAN" ] && [ -d "$GUESTS" ]; then rm -rf "$GUESTS"; fi
DH2_GUEST_OUT="$GUESTS" "$HERE/guests/build.sh" >/dev/null
echo "guests built into $GUESTS"

step "4/6 host self test"
"$BUILD/dh2selftest"

step "5/6 guest suite"
"$HERE/scripts/run-guests.sh" "$BUILD/dh2run" "$GUESTS"

step "6/6 the real engine"
if [ -z "$ENGINE" ]; then
  echo "DH2_ENGINE is not set: skipping the engine steps."
  echo "Set it to the original libDungeonHunter2.so (SHA-256"
  echo "36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80) to run them."
else
  echo "--- dh2link: every relocation ---"
  "$BUILD/dh2link" --quiet --sysroot "${DH2_SYSROOT:-$HOME/zb-src/sysroot/system/lib}" --stub "$ENGINE"
  echo "--- dh2boot: every initializer, then the engine's own entry ---"
  "$BUILD/dh2boot" --sysroot "${DH2_SYSROOT:-$HOME/zb-src/sysroot/system/lib}" --stub "$ENGINE" 2>&1 |
    grep -E "initializers :|instructions :|JNI_OnLoad|slingshot|nativeInit|nativeRender|GL  "
fi

if [ -n "$ARM64" ]; then
  step "7/6 the aarch64 product architecture"
  "$HERE/scripts/run-guests-aarch64.sh"
fi

printf '\nreproduce: done\n'
