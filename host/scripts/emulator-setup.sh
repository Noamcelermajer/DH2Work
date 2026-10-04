#!/bin/bash
# Create the emulator device profiles used by emulator-sweep.sh.
#
#   emulator-setup.sh
#
# Needs an SDK with an android-30 google_apis x86_64 system image and the AVD "dh2test" present as
# the template. Each AVD differs only in the device profile: screen size, density and RAM, which
# is what makes the engine take a different path.
set -euo pipefail

AVD="${ANDROID_AVD_HOME:-$HOME/.android/avd}"
TEMPLATE="$AVD/dh2test.avd/config.ini"
[ -f "$TEMPLATE" ] || { echo "emulator-setup: no template AVD at $TEMPLATE" >&2; exit 1; }

mkavd() {
  name=$1; width=$2; height=$3; dpi=$4; ram=$5; device=$6
  rm -rf "$AVD/$name.avd"
  mkdir -p "$AVD/$name.avd"
  sed -E "s|^hw.lcd.width.*|hw.lcd.width=$width|; s|^hw.lcd.height.*|hw.lcd.height=$height|; \
          s|^hw.lcd.density.*|hw.lcd.density=$dpi|; s|^hw.ramSize.*|hw.ramSize=$ram|; \
          s|^hw.device.name.*|hw.device.name=$device|; s|^skin.name.*|skin.name=${width}x${height}|; \
          s|^skin.path.*|skin.path=_no_skin|; s|^AvdId.*|AvdId=$name|; \
          s|^avd.ini.displayname.*|avd.ini.displayname=$name|" \
      "$TEMPLATE" > "$AVD/$name.avd/config.ini"
  grep -q "^AvdId" "$AVD/$name.avd/config.ini" || echo "AvdId=$name" >> "$AVD/$name.avd/config.ini"
  cat > "$AVD/$name.ini" <<EOF
avd.ini.encoding=UTF-8
path=$AVD/$name.avd
path.rel=avd/$name.avd
target=android-30
EOF
  echo "emulator-setup: $name (${width}x${height} ${dpi}dpi ${ram}MB)"
}

mkavd dh2-nexus5 1080 1920 480 2048 "Nexus 5"
mkavd dh2-tablet 2560 1600 320 3072 "Nexus 10"
mkavd dh2-small   480  800 240 1024 "Nexus S"
mkavd dh2-fold   2208 1768 420 4096 "Pixel Fold"
echo "emulator-setup: done; run host/scripts/emulator-sweep.sh"
