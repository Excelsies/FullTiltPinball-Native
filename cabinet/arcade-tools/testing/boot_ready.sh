#!/bin/bash
# Testing aid (run right after boot): report the uptime at which the menu is visible, i.e. the
# boot splash video has finished and EmulationStation's screen is up (bright, not the dark loader).
for i in $(seq 1 120); do
    if ! pgrep -f "omxplayer.*splashscreens" >/dev/null && pgrep -x emulationstatio >/dev/null; then
        ~/arcade-tools/snap /tmp/boot-ready.ppm 2>/dev/null
        b=$(python3 -c "from PIL import Image, ImageStat; print(int(ImageStat.Stat(Image.open('/tmp/boot-ready.ppm').convert('L').resize((96, 54))).mean[0]))" 2>/dev/null)
        if [ -n "$b" ] && [ "$b" -gt 45 ]; then
            echo "menu visible at uptime $(cut -d' ' -f1 /proc/uptime)s (brightness $b)"
            exit 0
        fi
    fi
    sleep 1
done
echo "menu not visible by uptime $(cut -d' ' -f1 /proc/uptime)s"
