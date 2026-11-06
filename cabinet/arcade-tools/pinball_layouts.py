#!/usr/bin/env python3
"""Point the side (pinball) buttons at each pinball game's flippers.

Only the two side-button lines of a game's RetroArch remap file change; anything else set for the
game (Game controls) is kept. Re-running puts the side buttons back to these choices; change one
game from the launch menu (Game controls > Set the side (pinball) buttons).
Targets are RetroPad names (game_controls.RETROPAD). Choices come from the games' manuals or
guides and were checked on the cabinet screen where noted.

usage: pinball_layouts.py
"""
import os

from fix_failures import game_key, read_choice
from game_controls import CFG, CABINET, RETROPAD, read_layout, remap_path, write_layout

ROMS = os.path.expanduser("~/RetroPie/roms")
# (system, ROM file, left side presses, right side presses)
PINBALL = [
    ("snes", "Super Pinball - Behind the Mask (USA).zip", "l", "r"),            # manual
    ("gba", "Mario Pinball Land (USA, Australia).zip", "l", "r"),               # GBA L / R flippers
    ("gba", "Sonic Pinball Party (USA) (En,Ja,Fr,De,Es,It).zip", "l", "r"),
    ("gba", "Pinball of the Dead, The (USA).zip", "l", "r"),
    ("nes", "Pinball Quest (USA).zip", "left", "a"),                            # D-pad left / A
    ("tg16", "Alien Crush (USA).zip", "left", "a"),                             # D-pad / I (StrategyWiki)
    ("tg16", "Devil's Crush - Naxat Pinball (USA).zip", "left", "a"),           # same scheme as Alien Crush
]


def emulator_command(system, rom):
    emus = {}
    for line in open(f"{CFG}/{system}/emulators.cfg"):
        if "=" in line:
            key, value = line.split("=", 1)
            emus[key.strip()] = value.strip().strip('"')
    emulator = read_choice(game_key(system, rom)) or emus["default"]
    return emulator, emus[emulator]


def main():
    left, right = CABINET["lefttrigger"][1], CABINET["righttrigger"][1]
    for system, name, left_does, right_does in PINBALL:
        rom = os.path.join(ROMS, system, name)
        assert os.path.isfile(rom), f"missing ROM: {rom}"
        emulator, command = emulator_command(system, rom)
        assert "/retroarch" in command, f"{name}: {emulator} is not RetroArch"
        path = remap_path(system, command, rom)
        layout = read_layout(path)
        layout[left], layout[right] = RETROPAD[left_does], RETROPAD[right_does]
        write_layout(path, layout)
        print(f"{system:6} {name}: left side = {left_does}, right side = {right_does}")


if __name__ == "__main__":
    main()
