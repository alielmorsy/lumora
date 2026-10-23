#!/system/bin/sh
# Usage: ctrl.sh <package> <activity> <tag> <switch:0|1>. Idle CPU over time, optionally one switching round after the first sample.
PKG=$1; ACT=$2; OUT=/data/local/tmp/ctrl_$3.txt
exec > "$OUT" 2>&1
am force-stop "$PKG"; sleep 2
am start -W -n "$PKG/$ACT" >/dev/null
sleep 8
PID=$(pidof "$PKG")
up() { cut -d' ' -f1 /proc/uptime; }
pcpu() { awk '{print $14+$15}' /proc/$PID/stat; }
freqs() { cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq | tr '\n' ' '; }
idle() {
  u0=$(up); c0=$(pcpu); sleep 15; u1=$(up); c1=$(pcpu)
  echo "$1 $(awk -v a=$c0 -v b=$c1 -v s=$u0 -v e=$u1 'BEGIN{printf "%.1f", (b-a)/(e-s)}')% freq: $(freqs)"
}
idle t0
if [ "$4" = 1 ]; then
  for x in 405 675 945 135; do input tap $x 2200; sleep 0.6; done
  sleep 3
fi
idle t1; idle t2; idle t3
echo done
