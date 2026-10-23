#!/system/bin/sh
# Usage: bench.sh <package> <activity> <tag>. Writes raw samples to /data/local/tmp/bench_<tag>.txt
PKG=$1; ACT=$2; OUT=/data/local/tmp/bench_$3.txt
exec > "$OUT" 2>&1

input keyevent KEYCODE_WAKEUP

for i in 1 2 3 4 5 6; do
  am force-stop "$PKG"; sleep 2
  echo "start $i $(am start -W -n "$PKG/$ACT" | grep TotalTime)"
  sleep 4
done
sleep 8

PID=$(pidof "$PKG")
LAYER=$(dumpsys SurfaceFlinger --list | grep -o "SurfaceView\[$PKG/[^]]*\](BLAST)#[0-9]*" | head -1)
echo "pid $PID"
echo "layer $LAYER"

up() { cut -d' ' -f1 /proc/uptime; }
pcpu() { awk '{print $14+$15}' /proc/$PID/stat; }
scpu() { awk '/^cpu /{print $2+$3+$4+$7+$8, $2+$3+$4+$5+$6+$7+$8}' /proc/stat; }
threads() { for t in /proc/$PID/task/*; do awk -v n="$(cat $t/comm)" '{print "thr", n, $14+$15}' $t/stat; done; }

# Polls SurfaceFlinger every second (its ring holds ~128 frames) while the workload runs.
phase() {
  name=$1; shift
  dumpsys SurfaceFlinger --latency-clear "$LAYER" >/dev/null
  threads | sed "s/^/$name /"
  echo "begin $name $(up) $(pcpu) $(scpu)"
  "$@" &
  W=$!
  n=0
  while [ $n -lt 20 ]; do
    sleep 1
    dumpsys SurfaceFlinger --latency "$LAYER" | sed "s/^/lat $name /"
    n=$((n+1))
  done
  wait $W
  echo "end $name $(up) $(pcpu) $(scpu)"
  threads | sed "s/^/$name /"
  dumpsys meminfo "$PID" | sed "s/^/mem $name /"
}

idle() { sleep 19; }
scroll() {
  i=0
  while [ $i -lt 9 ]; do
    input swipe 540 1700 540 700 300
    input swipe 540 700 540 1700 300
    i=$((i+1))
  done
}
tabs() {
  i=0
  while [ $i -lt 4 ]; do
    for x in 405 675 945 135; do input tap $x 2200; sleep 0.9; done
    i=$((i+1))
  done
}

phase idle idle
phase scroll scroll
phase tabs tabs
input tap 135 2200; sleep 2
phase idle2 idle
echo done
