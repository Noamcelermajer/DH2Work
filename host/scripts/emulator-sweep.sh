#!/bin/bash
# Run the own host inside Android emulators, on every device profile, and report.
#
#   emulator-sweep.sh [avd ...]        (default: every dh2* AVD)
#
# Needs: an SDK with the android-30 google_apis x86_64 image, the AVDs from emulator-setup.sh,
# an x86_64 Android build of the host, the engine .so and the game cache, and qemu-user is NOT
# involved -- the host runs natively under the emulator, and Dynarmic executes the ARM32 guest.
#
# Environment:
#   DH2_ANDROID_X86_BUILD  build dir (default ~/dh2-build/host-android-x86_64)
#   DH2_ENGINE             the engine .so
#   DH2_CACHE              the directory holding shaders.pak, *.bdae, data/, res/ ...
#   DH2_GUEST_SYSROOT      the bionic sysroot the host loads the closure from
#   DH2_FRAMES             frames to drive (default 2)
set -euo pipefail

SDK="${ANDROID_SDK_ROOT:-$HOME/android-sdk}"
ADB="$SDK/platform-tools/adb"
EMULATOR="$SDK/emulator/emulator"
BUILD="${DH2_ANDROID_X86_BUILD:-$HOME/dh2-build/host-android-x86_64}"
ENGINE="${DH2_ENGINE:-}"
CACHE="${DH2_CACHE:-}"
SYSROOT="${DH2_GUEST_SYSROOT:-$HOME/zb-src/sysroot/system/lib}"
FRAMES="${DH2_FRAMES:-2}"
PORT=5554
SERIAL=emulator-$PORT

[ -x "$BUILD/dh2boot" ] || { echo "sweep: no dh2boot in $BUILD" >&2; exit 1; }
[ -n "$ENGINE" ] && [ -f "$ENGINE" ] || { echo "sweep: set DH2_ENGINE" >&2; exit 1; }
[ -n "$CACHE" ] && [ -d "$CACHE" ] || { echo "sweep: set DH2_CACHE" >&2; exit 1; }

AVDS=("$@")
if [ ${#AVDS[@]} -eq 0 ]; then
  for ini in "${ANDROID_AVD_HOME:-$HOME/.android/avd}"/dh2*.ini; do
    [ -f "$ini" ] && AVDS+=("$(basename "$ini" .ini)")
  done
fi

# Strip so the push is quick; the emulator does not need debug info.
STAGE="$(mktemp -d)"
cp "$BUILD/dh2boot" "$STAGE/"
if command -v llvm-strip >/dev/null; then llvm-strip "$STAGE/dh2boot" 2>/dev/null || true; fi

printf '%-17s %-5s %-6s %-12s %-10s %-12s %-11s %s\n' DEVICE BOOT INIT NATIVEINIT FRAMES INSTR GL EXIT

for avd in "${AVDS[@]}"; do
  pkill -f qemu-system-x86_64 2>/dev/null || true; sleep 3
  "$ADB" kill-server >/dev/null 2>&1 || true; "$ADB" start-server >/dev/null 2>&1 || true
  nohup "$EMULATOR" -avd "$avd" -no-window -no-audio -no-boot-anim -no-snapshot \
    -gpu swiftshader_indirect -no-metrics -port "$PORT" > "$STAGE/$avd.log" 2>&1 &

  booted=""
  for _ in $(seq 1 30); do
    sleep 8
    # The boot flag alone is not enough: adb goes offline again briefly while the framework
    # starts, and a push during that window loses the connection.
    if [ "$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; then
      booted=1; break
    fi
  done
  if [ -z "$booted" ]; then printf '%-17s BOOT-FAILED\n' "$avd"; pkill -f qemu-system-x86_64 || true; continue; fi
  sleep 12
  "$ADB" wait-for-device >/dev/null 2>&1 || true

  "$ADB" -s "$SERIAL" shell mkdir -p /data/local/tmp/dh2
  "$ADB" -s "$SERIAL" push "$STAGE/dh2boot" /data/local/tmp/dh2/ >/dev/null
  "$ADB" -s "$SERIAL" push "$ENGINE" /data/local/tmp/dh2/libDungeonHunter2.so >/dev/null
  "$ADB" -s "$SERIAL" push "$SYSROOT" /data/local/tmp/dh2/sysroot >/dev/null
  for attempt in 1 2 3; do
    if "$ADB" -s "$SERIAL" push "$CACHE" /data/local/tmp/dh2/files >/dev/null 2>&1; then break; fi
    [ "$attempt" = 3 ] && { printf '%-17s PUSH-FAILED\n' "$avd"; }
    sleep 10
  done

  "$ADB" -s "$SERIAL" shell "cd /data/local/tmp/dh2 && chmod +x dh2boot && \
    ./dh2boot --frames $FRAMES --io-trace 0 --root files --sysroot sysroot --stub libDungeonHunter2.so \
    > o.txt 2>&1; echo EXIT=\$? >> o.txt" >/dev/null

  report() { "$ADB" -s "$SERIAL" shell "grep -oE '$1' /data/local/tmp/dh2/o.txt" | tr -d '\r' | head -1; }
  printf '%-17s %-5s %-6s %-12s %-10s %-12s %-11s %s\n' "$avd" ok \
    "$(report 'initializers : [0-9]+' | grep -oE '[0-9]+')" \
    "$(report 'nativeInit       : returned r0=0x[0-9a-f]+ after [0-9,]+' | grep -oE '[0-9,]+$')" \
    "$(report 'frames       : [0-9]+' | grep -oE '[0-9]+')" \
    "$(report 'instructions : [0-9]+' | grep -oE '[0-9]+')" \
    "$(report 'GL           : [0-9]+ call' | grep -oE '[0-9]+')" \
    "$(report 'EXIT=[0-9]+')"

  pkill -f qemu-system-x86_64 2>/dev/null || true; sleep 3
done

rm -rf "$STAGE"
echo "sweep: done"
