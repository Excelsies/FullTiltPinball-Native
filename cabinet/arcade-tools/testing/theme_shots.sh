#!/bin/bash
# Screenshot EmulationStation in each theme: the system list and one game list.
# usage: theme_shots.sh TAG THEME...   -> /tmp/shot-TAG-THEME-{system,games}.ppm
# Leaves EmulationStation running with the last theme given.
tag=$1; shift
settings=/opt/retropie/configs/all/emulationstation/es_settings.cfg
press() { sudo python3 ~/arcade-tools/testing/press.py p1 0.15 "$1"; }
for theme in "$@"; do
    [ -d "/etc/emulationstation/themes/$theme" ] || { echo "no theme $theme"; exit 1; }
    sudo systemctl stop getty@tty1; sleep 3  # EmulationStation saves its settings on exit
    sed -i "s|<string name=\"ThemeSet\" value=\"[^\"]*\" />|<string name=\"ThemeSet\" value=\"$theme\" />|" "$settings"
    grep -q "value=\"$theme\"" "$settings" || { echo "could not set theme $theme"; exit 1; }
    ~/arcade-tools/restart-menu >/dev/null || { echo "menu did not start for $theme"; exit 1; }
    sleep 55  # about 3,100 games: the menu takes most of a minute to load
    ~/arcade-tools/snap "/tmp/shot-$tag-$theme-system.ppm"
    press 9; sleep 6   # A: open the system's game list
    ~/arcade-tools/snap "/tmp/shot-$tag-$theme-games.ppm"
    press 7; sleep 2   # B: back to the system list
    echo "$theme done"
done
