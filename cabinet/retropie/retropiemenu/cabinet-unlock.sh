#!/bin/bash
# RetroPie menu entry: unlock the system (maintenance mode) so changes to it are kept - for adding
# games, themes or updates. Lock again afterwards. Restarts the cabinet.
T="Cabinet: Unlock"
if ! grep -q "boot=overlay" /proc/cmdline; then
    dialog --title "$T" --msgbox "The cabinet is already unlocked (maintenance mode).\n\nUse \"Cabinet: Lock\" when you are done." 9 60
    exit 0
fi
dialog --title "$T" --yesno "Unlock the cabinet for maintenance?\n\nChanges to the system are kept until you lock it again. The cabinet restarts." 10 64 || exit 0
out=$(sudo raspi-config nonint disable_overlayfs 2>&1)
if grep -q "boot=overlay" /boot/cmdline.txt; then
    dialog --title "$T" --msgbox "Unlocking failed:\n\n$(echo "$out" | tail -8)" 20 72
    exit 1
fi
dialog --title "$T" --infobox "Unlocked. Restarting..." 5 40
sleep 2
sudo reboot
