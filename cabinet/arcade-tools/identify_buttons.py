#!/usr/bin/env python3
"""Ask for each physical cabinet button on screen and record which joystick button it sends.

Writes ~/arcade-tools/button-map.json: [{"label", "device", "path", "button"}] (button null =
skipped after 20 s). Run on the cabinet screen with EmulationStation stopped:
  sudo openvt -f -c 1 -s -w -- sudo -u pi ~/arcade-tools/identify_buttons.py
"""
import glob
import json
import os
import select
import struct
import subprocess
import time

EVENT = struct.Struct("<IhBB")  # time ms, value, type, number
JS_BUTTON, JS_INIT = 0x01, 0x80
WAIT = 20
OUT = os.path.expanduser("~/arcade-tools/button-map.json")
ROW = ["top row, LEFT button (Y)", "top row, MIDDLE button (X)", "top row, RIGHT button",
       "bottom row, LEFT button (B)", "bottom row, MIDDLE button (A)", "bottom row, RIGHT button (R)",
       "START", "COIN"]
PROMPTS = ([f"PLAYER 1: {b}" for b in ROW] + [f"PLAYER 2: {b}" for b in ROW] +
           ["LEFT side of the cabinet: side button", "RIGHT side of the cabinet: side button",
            "FRONT panel: EXIT", "FRONT panel: MODE", "FRONT panel: MENU", "FRONT panel: PLAY"])


def by_path():
    """js device -> stable USB port path, so the map survives re-enumeration."""
    paths = {}
    for link in glob.glob("/dev/input/by-path/*-joystick"):
        if not link.endswith("-event-joystick"):
            paths[os.path.realpath(link)] = os.path.basename(link)
    return paths


def show(text):
    subprocess.run(["dialog", "--infobox", text, "7", "60"], check=True)


def drain(fds):
    while select.select(fds, [], [], 0.2)[0]:
        for fd in select.select(fds, [], [], 0)[0]:
            os.read(fd, EVENT.size * 64)


def next_press(fds, names):
    deadline = time.monotonic() + WAIT
    while time.monotonic() < deadline:
        for fd in select.select(fds, [], [], deadline - time.monotonic())[0]:
            _, value, etype, number = EVENT.unpack(os.read(fd, EVENT.size))
            if etype == JS_BUTTON and value == 1:
                return names[fd], number
    return None, None


def main():
    devices = sorted(glob.glob("/dev/input/js*"))
    assert devices, "no joysticks found"
    paths = by_path()
    fds = [os.open(d, os.O_RDONLY) for d in devices]
    names = dict(zip(fds, devices))
    results = []
    try:
        for i, label in enumerate(PROMPTS, 1):
            drain(fds)  # ignore init events and bounces from the previous press
            show(f"Button {i} of {len(PROMPTS)}\n\nPress:  {label}\n\n(wait {WAIT}s to skip)")
            dev, button = next_press(fds, names)
            results.append({"label": label, "device": dev, "path": paths.get(dev), "button": button})
            show(f"{label}\n\n= {dev} button {button}" if dev else f"{label}\n\nskipped")
            time.sleep(0.8)
    finally:
        for fd in fds:
            os.close(fd)
    with open(OUT, "w") as fh:
        json.dump(results, fh, indent=1)
    seen = {}
    for r in results:
        if r["device"]:
            seen.setdefault((r["device"], r["button"]), []).append(r["label"])
    dupes = [v for v in seen.values() if len(v) > 1]
    show("All done - thank you!\n\n" + (f"Same button pressed twice: {dupes}" if dupes else "Saved."))
    time.sleep(4)


if __name__ == "__main__":
    main()
