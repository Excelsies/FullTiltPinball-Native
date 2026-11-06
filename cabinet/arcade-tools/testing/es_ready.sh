#!/bin/bash
# Testing aid: restart EmulationStation and report seconds until its menu replaces the dark
# loading screen (average screen brightness jumps).
start=$(date +%s.%N)
~/arcade-tools/restart-menu >/dev/null
for i in $(seq 1 60); do
    ~/arcade-tools/snap /tmp/es-ready.ppm
    b=$(python3 -c "from PIL import Image, ImageStat; print(int(ImageStat.Stat(Image.open('/tmp/es-ready.ppm').convert('L').resize((96, 54))).mean[0]))")
    if [ "$b" -gt 45 ]; then
        echo "menu showing after $(python3 -c "print(round($(date +%s.%N) - $start, 1))")s (brightness $b)"
        exit 0
    fi
    sleep 1.5
done
echo "menu not showing after 90s+"
