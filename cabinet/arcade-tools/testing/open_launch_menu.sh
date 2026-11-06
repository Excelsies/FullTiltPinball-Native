#!/bin/bash
# Testing aid: wait until runcommand's joy2key is up (the launch prompt is listening), then push the
# stick down once to open the launch menu. Prints how long it waited.
for i in $(seq 1 60); do
    pgrep -f joy2key_sdl.py >/dev/null && break
    sleep 0.1
done
echo "joy2key after $((i / 10)).$((i % 10))s"
sleep 0.2
sudo python3 /home/pi/arcade-tools/testing/press.py p1 axis 1 255 0.15
