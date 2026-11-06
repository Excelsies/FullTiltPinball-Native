#!/bin/bash
# RetroPie menu entry: lock the system read-only (raspi-config's overlay file system). Saves,
# settings, game controls and game lists live on the retrodata partition and are kept; any other
# change is undone at the next restart. Restarts the cabinet.
T="Cabinet: Lock"
if grep -q "boot=overlay" /proc/cmdline; then
    dialog --title "$T" --msgbox "The cabinet is already locked." 7 50
    exit 0
fi
dialog --title "$T" --yesno "Lock the cabinet (read-only)?\n\nSaves, settings, game controls and game lists are kept as usual. Anything else changed on the system is undone at each restart, so add games, themes or updates before locking (or unlock first).\n\nThe cabinet restarts." 14 66 || exit 0
dialog --title "$T" --infobox "Locking... (about a minute)" 5 40
out=$(sudo raspi-config nonint enable_overlayfs 2>&1)
initrd="/boot/initrd.img-$(uname -r)-overlay"
if ! grep -q "boot=overlay" /boot/cmdline.txt || [ ! -f "$initrd" ] || ! grep -q "^initramfs $(basename "$initrd")" /boot/config.txt; then
    dialog --title "$T" --msgbox "Locking failed, the cabinet was not changed:\n\n$(echo "$out" | tail -8)" 20 72
    exit 1
fi
dialog --title "$T" --infobox "Locked. Restarting..." 5 40
sleep 2
sudo reboot
