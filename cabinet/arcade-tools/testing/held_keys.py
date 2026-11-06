#!/usr/bin/env python3
"""Testing aid (root): which keys / buttons cabinetd's virtual devices hold down right now (EVIOCGKEY).

"Arcade Cabinet keys" = the emulator shortcut / pinball flipper keyboard (Z = 44, / = 53),
"Arcade Cabinet (merged)" = Doom's merged joystick (buttons as numbered in button-map.json).
usage: sudo held_keys.py
"""
import fcntl
import glob

EVIOCGKEY = (2 << 30) | (96 << 16) | (ord("E") << 8) | 0x18
DEVICES = {"Arcade Cabinet keys": 0, "Arcade Cabinet (merged)": 288}  # name: code offset shown


def held(node):
    buf = bytearray(96)
    with open(node, "rb") as fh:
        fcntl.ioctl(fh, EVIOCGKEY, buf)
    return [k for k in range(len(buf) * 8) if buf[k // 8] >> (k % 8) & 1]


def main():
    found = 0
    for name_file in sorted(glob.glob("/sys/class/input/event*/device/name")):
        name = open(name_file).read().strip()
        if name in DEVICES:
            node = "/dev/input/" + name_file.split("/")[4]
            print(f"{name} {node}: held {[k - DEVICES[name] for k in held(node)]}")
            found += 1
    if not found:
        print("no cabinetd virtual device (is cabinetd running?)")


if __name__ == "__main__":
    main()
