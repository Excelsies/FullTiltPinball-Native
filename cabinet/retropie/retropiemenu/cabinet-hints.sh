#!/bin/bash
# RetroPie menu entry: put the cabinet button hints back into every theme (needed after a theme
# is installed or updated), then restart EmulationStation so they show.
if grep -q "boot=overlay" /proc/cmdline; then
    dialog --title "Cabinet button hints" --msgbox "The cabinet is locked: theme changes would be undone at the next restart.\n\nUnlock it first (Cabinet: Unlock), then run this again." 10 66
    exit 1
fi
out=$(sudo python3 /home/pi/arcade-tools/theme_hints.py 2>&1)
rc=$?
if [ $rc -ne 0 ]; then
    dialog --cr-wrap --no-collapse --title "Cabinet button hints" --msgbox "Some themes could not be updated:\n\n$out" 20 76
    exit $rc
fi
dialog --cr-wrap --no-collapse --title "Cabinet button hints" --msgbox "$out\n\nEmulationStation will now restart to show them." 18 76
# EmulationStation quits after this script ends; its launcher sees the flag and starts it again.
touch /tmp/es-restart
[ "$(id -u)" -eq 0 ] && chown pi:pi /tmp/es-restart  # the launcher (user pi) removes it
pkill -TERM -x -f /opt/retropie/supplementary/emulationstation/emulationstation
