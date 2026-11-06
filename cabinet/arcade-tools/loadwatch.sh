#!/bin/bash
# Launch a game like EmulationStation does and log temperature, CPU clock, throttle flags and
# CPU use every 3 s, then stop it. usage: loadwatch.sh SECONDS SYSTEM ROM  (ES must be stopped)
secs=$1 system=$2 rom=$3
sudo openvt -f -c 1 -s -- sudo -u pi env TERM=linux \
    /opt/retropie/supplementary/runcommand/runcommand.sh 0 _SYS_ "$system" "$rom" >/dev/null 2>&1 </dev/null
prev=($(head -1 /proc/stat))
for ((t = 3; t <= secs; t += 3)); do
    sleep 3
    cur=($(head -1 /proc/stat))
    idle=$(( (cur[4] + cur[5]) - (prev[4] + prev[5]) ))
    total=0; for i in 1 2 3 4 5 6 7 8; do total=$((total + cur[i] - prev[i])); done
    prev=("${cur[@]}")
    top=$(ps -t tty1 -o pcpu=,comm= --sort=-pcpu | head -1)
    printf "%3ss %s %s %s cpu=%s%% top:%s\n" $t "$(vcgencmd measure_temp)" \
        "$(vcgencmd measure_clock arm | awk -F= '{printf "%dMHz", $2/1000000}')" \
        "$(vcgencmd get_throttled)" $((100 * (total - idle) / total)) "$top"
done
~/arcade-tools/snap "/tmp/loadwatch.ppm"
for sig in TERM KILL; do
    pids=$(ps -t tty1 -o pid= | tr '\n' ' ')
    [ -n "${pids// }" ] && sudo kill -$sig $pids 2>/dev/null
    sleep 3
done
geom="1920 1080"
if [ "$(fbset -s | awk '/geometry/ {print $2, $3}')" != "$geom" ]; then
    tvservice -p >/dev/null; fbset -depth 8; fbset -depth 32 -g $geom $geom 32
fi
