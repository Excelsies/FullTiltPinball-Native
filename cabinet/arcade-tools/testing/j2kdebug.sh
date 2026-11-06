#!/bin/bash
# Testing aid: run the SDL joy2key with debug output on this terminal for 8 seconds.
python3 /opt/retropie/admin/joy2key/joy2key_sdl.py -d /dev/input/jsX kcub1 kcuf1 kcuu1 kcud1 0x0a 0x20 0x1b 0x09 kpp knp > /tmp/j2k-debug.log 2>&1
sleep 8
pkill -f "python3 /opt/retropie/admin/joy2key/joy2key_sdl.py -d"
