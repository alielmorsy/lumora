#!/system/bin/sh
# Usage: leak.sh <package> <activity> <tag>. Idle CPU before/after rounds of tab switching.
PKG=$1; ACT=$2; OUT=/data/local/tmp/leak_$3.txt
exec > "$OUT" 2>&1
am force-stop "$PKG"; sleep 2
am start -W -n "$PKG/$ACT" >/dev/null
sleep 8
PID=$(pidof "$PKG")
up() { cut -d' ' -f1 /proc/uptime; }
pcpu() { awk '{print $14+$15}' /proc/$PID/stat; }
idle() {
  u0=$(up); c0=$(pcpu); sleep 15; u1=$(up); c1=$(pcpu)
  echo "idle$1 $(awk -v a=$c0 -v b=$c1 -v s=$u0 -v e=$u1 'BEGIN{printf "%.1f", (b-a)/(e-s)}')% rss=$(awk '/VmRSS/{print $2}' /proc/$PID/status)kB"
}
switch() {
  i=0
  while [ $i -lt 10 ]; do
    for x in 405 675 945 135; do input tap $x 2200; sleep 0.5; done
    i=$((i+1))
  done
  sleep 3
}
idle 0; switch; idle 40; switch; idle 80; switch; idle 120
echo done
