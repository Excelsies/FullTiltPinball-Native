#!/bin/bash
# Testing aid: run runcommand traced (timestamps) for one game. usage: rctrace.sh SYSTEM ROM
export PS4='+ $(date +%s.%N | cut -c7-15) '
bash -x /opt/retropie/supplementary/runcommand/runcommand.sh 0 _SYS_ "$1" "$2" 2>/tmp/rc-trace.log
