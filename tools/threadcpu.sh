#!/system/bin/sh
# Per-thread CPU accounting for the DH2 guest process, root-free via /proc.
# /proc/<pid>/task/<tid>/stat: field 14 = utime, 15 = stime.  After stripping
# "pid (comm) " the remaining list starts at field 3, so utime/stime are ${12}/${13}.
DUR=${1:-6}
PID=$(pidof local.dh2.fold7:guest 2>/dev/null | tr ' ' '\n' | head -1)
if [ -z "$PID" ]; then echo "guest not running"; exit 1; fi
HZ=$(getconf CLK_TCK 2>/dev/null || echo 100)
SNAP=/data/local/tmp/dh2-snap.$$
: > "$SNAP"

snap() {
  for T in /proc/$PID/task/*; do
    TID=${T##*/}
    S=$(cat $T/stat 2>/dev/null) || continue
    [ -z "$S" ] && continue
    REST=${S#*) }
    set -- $REST
    echo "$TID ${12} ${13}" >> "$SNAP"
  done
}

snap
COUNT=$(wc -l < "$SNAP")
echo "sample PID=$PID threads=$COUNT duration=${DUR}s HZ=$HZ"
sleep "$DUR"
TOTAL=0
for T in /proc/$PID/task/*; do
  TID=${T##*/}
  S=$(cat $T/stat 2>/dev/null) || continue
  [ -z "$S" ] && continue
  REST=${S#*) }
  set -- $REST
  U2=${12}; S2=${13}
  LINE=$(grep "^$TID " "$SNAP")
  [ -z "$LINE" ] && continue
  set -- $LINE
  U1=${2}; S1=${3}
  DT=$(( (U2 - U1) + (S2 - S1) ))
  TOTAL=$((TOTAL + DT))
  PCT=$(awk -v d=$DT -v h=$HZ -v w=$DUR 'BEGIN{printf "%.1f", 100.0*d/(h*w)}')
  NAME=$(cat /proc/$PID/task/$TID/comm 2>/dev/null)
  echo "$PCT%  tid=$TID  $NAME"
done | sort -rn | head -14
awk -v t=$TOTAL -v h=$HZ -v w=$DUR 'BEGIN{printf "--- total guest CPU: %.1f%% of one core over %ss\n", 100.0*t/(h*w), w}'
rm -f "$SNAP"
