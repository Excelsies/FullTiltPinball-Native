#!/usr/bin/env python3
"""Point AdvanceMAME and Reicast at the boards' current /dev/input/eventN numbers.

Those two emulators pick players by event number, and the numbers change between boots
(event6/7 one time, event0/1 the next). runcommand-onstart.sh runs this before their games.

usage: player_ports.py
"""
import remap_standalone
from remap_controls import button_check


def main():
    buttons, _, events = button_check()
    remap_standalone.advmame(buttons, events)
    remap_standalone.reicast(events)
    print("players:", events)


if __name__ == "__main__":
    main()
