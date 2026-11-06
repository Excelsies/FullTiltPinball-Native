#!/bin/bash
# Run one emulator command on the cabinet screen, screenshot it at the given seconds, then stop it.
# usage: trial.sh NAME "COMMAND" SECONDS...   (EmulationStation must be stopped)
# Shots: /tmp/trial-NAME-<sec>.ppm, emulator output: /tmp/trial-NAME.log
name=$1 cmd=$2
shift 2
log=/tmp/trial-$name.log
geom=$(fbset -s | awk '/geometry/ {print $2, $3}')
sudo openvt -f -c 1 -s -- sudo -u pi env TERM=linux bash -c "$cmd" >"$log" 2>&1 </dev/null
t=0
for at in "$@"; do
    sleep $((at - t)); t=$at
    ~/arcade-tools/snap "/tmp/trial-$name-$at.ppm" || echo "snap at ${at}s failed"
done
# Everything on tty1 except getty is the trial: stop it, then force.
for sig in TERM KILL; do
    pids=$(ps -t tty1 -o pid= | tr '\n' ' ')
    [ -n "${pids// }" ] && sudo kill -$sig $pids 2>/dev/null
    sleep 3
done
left=$(ps -t tty1 -o pid,comm=)
[ "$(echo "$left" | wc -l)" -gt 1 ] && echo "still on tty1: $left"
if [ "$(fbset -s | awk '/geometry/ {print $2, $3}')" != "$geom" ]; then
    tvservice -p >/dev/null; fbset -depth 8; fbset -depth 32 -g $geom $geom 32
fi
echo "trial $name done"
