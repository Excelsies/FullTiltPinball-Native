#!/usr/bin/env python3
"""Simulate cabinet presses for testing (root): hold the given board buttons together, release.

usage: press.py BOARD SECONDS BUTTON...      e.g. press.py p1 0.15 9  (player 1's board, A)
       press.py BOARD axis AXIS VALUE SECONDS  push the stick, then back to centre (127)
BOARD is p1 or p2 (found by USB port: event numbers change between boots). Buttons are numbered
as in button-map.json (evdev code 288 + number).
"""
import json
import os
import struct
import sys
import time

EV = struct.Struct("llHHi")  # struct input_event on 32-bit ARM


def send(f, etype, code, value):
    f.write(EV.pack(0, 0, etype, code, value) + EV.pack(0, 0, 0, 0, 0))


def board_event(board):
    """/dev/input/eventN of player 1's or 2's board, via its USB port in button-map.json."""
    rows = json.load(open(os.path.expanduser("~pi/arcade-tools/button-map.json")))
    path = next(r["path"] for r in rows if r["label"] == f"PLAYER {board[1]}: START")
    event = os.path.realpath(f"/dev/input/by-path/{path[:-len('-joystick')]}-event-joystick")
    assert os.path.exists(event), f"no board for {board}: {event}"
    return event


def main():
    assert sys.argv[1] in ("p1", "p2"), __doc__
    dev = board_event(sys.argv[1])
    with open(dev, "wb", buffering=0) as f:
        if sys.argv[2] == "axis":
            axis, value, secs = int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
            send(f, 3, axis, value)
            time.sleep(secs)
            send(f, 3, axis, 127)
            return
        secs, buttons = float(sys.argv[2]), [288 + int(b) for b in sys.argv[3:]]
        assert buttons, __doc__
        for b in buttons:
            send(f, 1, b, 1)
        time.sleep(secs)
        for b in buttons:
            send(f, 1, b, 0)


if __name__ == "__main__":
    main()
