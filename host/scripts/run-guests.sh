#!/bin/bash
# Run every built guest under dh2run and compare stdout with the recorded expectation.
#
#   run-guests.sh <dh2run> <guest-dir> [--accept]
#
# Expectations live in guests/expected/<name>.out (stdout) and <name>.exit (exit status).
# --accept writes the current output as the expectation, which is only legitimate once each
# value has been independently checked.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN="${1:?usage: run-guests.sh <dh2run> <guest-dir> [--accept]}"
GUESTS="${2:?usage: run-guests.sh <dh2run> <guest-dir> [--accept]}"
ACCEPT="${3:-}"
EXPECTED="$HERE/guests/expected"
mkdir -p "$EXPECTED"

pass=0
fail=0
skip=0

run_one() {
  local name="$1"; shift
  local binary="$GUESTS/$name"
  if [ ! -x "$binary" ]; then
    echo "SKIP $name (not built)"
    skip=$((skip + 1))
    return
  fi
  local guest_out
  guest_out="$("$RUN" --quiet "$binary" "$@" 2>&1)"
  local status=$?

  if [ "$ACCEPT" = "--accept" ]; then
    printf '%s\n' "$guest_out" > "$EXPECTED/$name.out"
    printf '%s\n' "$status" > "$EXPECTED/$name.exit"
    echo "ACCEPT $name (exit $status)"
    return
  fi
  if [ ! -f "$EXPECTED/$name.out" ]; then
    echo "SKIP $name (no expectation recorded)"
    skip=$((skip + 1))
    return
  fi
  local want_out want_status
  want_out="$(cat "$EXPECTED/$name.out")"
  want_status="$(cat "$EXPECTED/$name.exit" 2>/dev/null || echo 0)"
  if [ "$guest_out" = "$want_out" ] && [ "$status" = "$want_status" ]; then
    echo "PASS $name"
    pass=$((pass + 1))
  else
    echo "FAIL $name (exit $status, expected $want_status)"
    diff <(printf '%s\n' "$want_out") <(printf '%s\n' "$guest_out") | head -20
    fail=$((fail + 1))
  fi
}

run_one hello
run_one hello_thumb
run_one args alpha beta
run_one vfp
run_one exclusive
run_one memory
run_one armv8_t32
run_one bionic world

echo "run-guests: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
