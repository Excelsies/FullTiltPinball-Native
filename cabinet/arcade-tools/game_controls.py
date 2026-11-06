#!/usr/bin/env python3
"""Launch menu > User Menu > "Game controls": set the buttons for one game in its own emulator.

RetroArch games: press the cabinet button wanted for each game button; saved as that game's remap
file (<system config>/<core name>/<game>.rmp, which RetroArch loads by itself), for both players.
Emulators with their own per-game control menus get directions to them; the standalone N64 and
Dreamcast emulators, which have none, can switch this one game to their RetroArch version.

usage (runcommand passes these): game_controls.py SYSTEM EMULATOR ROM COMMAND
Exit status for runcommand: 0 = stay in the menu, 1 = leave without launching, 2 = launch.
"""
import ctypes
import os
import re
import select
import shlex
import struct
import subprocess
import sys
import termios
import time

from fix_failures import game_key, write_choice
from remap_controls import button_check

CFG = "/opt/retropie/configs"
EVENT = struct.Struct("llHHi")
EV_KEY, BTN_FIRST = 1, 288
WAIT = 15  # seconds to wait for a press before keeping the button as it is
STAY, LEAVE, LAUNCH = 0, 1, 2
TITLE = "Game controls"
# RetroPad buttons as RetroArch numbers them in remap files; -1 = unused.
RETROPAD = {"b": 0, "y": 1, "select": 2, "start": 3, "up": 4, "down": 5, "left": 6, "right": 7,
            "a": 8, "x": 9, "l": 10, "r": 11, "l2": 12, "r2": 13}
# Cabinet buttons (EmulationStation names) and the RetroPad button each one is.
CABINET = {"b": ("B", "b"), "a": ("A", "a"), "y": ("Y", "y"), "x": ("X", "x"),
           "leftshoulder": ("TOP-RIGHT", "l"), "rightshoulder": ("BOTTOM-RIGHT", "r"),
           "start": ("START", "start"), "select": ("COIN", "select"),
           # side (pinball) buttons: player 1's L2 / R2 (remap_controls.py, cabinetd.py)
           "lefttrigger": ("LEFT SIDE", "l2"), "righttrigger": ("RIGHT SIDE", "r2")}
SIDES = ("lefttrigger", "righttrigger")
DPAD = [("left", "D-pad left"), ("right", "D-pad right"), ("up", "D-pad up"), ("down", "D-pad down")]
# Game buttons per kind of system: (RetroPad button, what the game/console calls it).
ARCADE = [("b", "Button 1"), ("a", "Button 2"), ("y", "Button 3"), ("x", "Button 4"),
          ("l", "Button 5"), ("r", "Button 6"), ("start", "Start"), ("select", "Coin")]
TWO = [("b", "B"), ("a", "A"), ("start", "Start"), ("select", "Select")]
SNES = [("b", "B"), ("a", "A"), ("y", "Y"), ("x", "X"), ("l", "L"), ("r", "R"), ("start", "Start"),
        ("select", "Select")]
MEGADRIVE = [("y", "A"), ("b", "B"), ("a", "C"), ("l", "X"), ("x", "Y"), ("r", "Z"), ("start", "Start"),
             ("select", "Mode")]
GAME_BUTTONS = {
    "arcade": ARCADE, "fba": ARCADE, "fbneo": ARCADE, "neogeo": ARCADE, "mame-libretro": ARCADE,
    "mame-mame4all": ARCADE, "cps1": ARCADE, "cps2": ARCADE, "cps3": ARCADE,
    "nes": TWO, "fds": TWO, "famicom": TWO, "gb": TWO, "gbc": TWO,
    "gba": TWO + [("l", "L"), ("r", "R")],
    "pcengine": [("b", "II"), ("a", "I"), ("start", "Run"), ("select", "Select")],
    "mastersystem": [("b", "1"), ("a", "2"), ("start", "Start / Pause")],
    "gamegear": [("b", "1"), ("a", "2"), ("start", "Start")],
    "snes": SNES, "sfc": SNES,
    "megadrive": MEGADRIVE, "genesis": MEGADRIVE, "segacd": MEGADRIVE, "sega32x": MEGADRIVE,
    "psx": [("b", "Cross"), ("a", "Circle"), ("y", "Square"), ("x", "Triangle"), ("l", "L1"),
            ("r", "R1"), ("l2", "L2"), ("r2", "R2"), ("start", "Start"), ("select", "Select")],
    "psp": [("b", "Cross"), ("a", "Circle"), ("y", "Square"), ("x", "Triangle"), ("l", "L"),
            ("r", "R"), ("start", "Start"), ("select", "Select")],
    "atari2600": [("b", "Fire"), ("start", "Reset"), ("select", "Select")],
}
DEFAULT = [("b", "B"), ("a", "A"), ("y", "Y"), ("x", "X"), ("l", "L"), ("r", "R"),
           ("l2", "L2"), ("r2", "R2"), ("start", "Start"), ("select", "Select")]
# Emulators with their own per-game control menus (name prefix -> directions).
OWN_MENUS = {
    "advmame": "Start the game and press MENU. Choose \"Input (this game)\", pick an action and "
               "press the button you want. AdvanceMAME keeps it for this game only.",
    "drastic": "Start the game and press MENU, then \"Configure Controls\". DraStic's controls "
               "may apply to every DS game, not just this one.",
    "lzdoom": "Start the game, press START for the menu, then Options > Customize Controls.",
    "eduke32": "Start the game, press START for the menu, then Options > Controls.",
}
# Standalone emulators with no per-game controls, and the RetroArch version to offer instead.
SWITCH_TO = {"mupen64plus": "lr-mupen64plus", "reicast": "lr-flycast"}


def dialog(*args):
    """Run dialog on the screen; returns (exit status, the choice it printed)."""
    with open("/dev/tty", "w") as tty:
        proc = subprocess.run(["dialog", "--no-collapse", "--title", TITLE] + list(args), stdout=tty,
                              stderr=subprocess.PIPE, universal_newlines=True)
    return proc.returncode, proc.stderr.strip()


def flush_keys():
    """The joystick-to-keyboard helper turns presses into keys too: drop them."""
    termios.tcflush(sys.stdin.fileno(), termios.TCIFLUSH)


def core_name(command):
    """RetroArch keeps game remaps under the core's own name (its library_name)."""
    so = shlex.split(command)[shlex.split(command).index("-L") + 1]

    class SystemInfo(ctypes.Structure):
        _fields_ = [("library_name", ctypes.c_char_p), ("library_version", ctypes.c_char_p),
                    ("valid_extensions", ctypes.c_char_p), ("need_fullpath", ctypes.c_bool),
                    ("block_extract", ctypes.c_bool)]
    info = SystemInfo()
    ctypes.CDLL(so).retro_get_system_info(ctypes.byref(info))
    return info.library_name.decode()


def remap_path(system, command, rom):
    text = open(f"{CFG}/{system}/retroarch.cfg").read()
    found = re.search(r'^input_remapping_directory\s*=\s*"([^"]+)"', text, re.M)
    folder = found.group(1) if found else f"{CFG}/{system}/"
    game = os.path.splitext(os.path.basename(rom))[0]
    return os.path.join(folder, core_name(command), game + ".rmp")


def read_layout(path):
    """{cabinet RetroPad button: RetroPad button it sends}; no file = each sends itself."""
    layout = {pad: RETROPAD[pad] for _, pad in CABINET.values()}
    if os.path.exists(path):
        for line in open(path):
            found = re.match(r'input_player1_btn_(\w+) = "(-?\d+)"', line)
            if found and found.group(1) in layout:
                layout[found.group(1)] = int(found.group(2))
    return layout


def write_layout(path, layout):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    lines = [f'input_player{p}_btn_{pad} = "{target}"\n' for p in (1, 2) for pad, target in layout.items()]
    tmp = path + ".tmp"
    open(tmp, "w").writelines(lines)
    os.replace(tmp, path)


def describe(layout, game_buttons):
    """One line per cabinet button: the game button it presses."""
    pad_of = {number: pad for pad, number in RETROPAD.items()}
    label = dict(game_buttons)
    rows = []
    for name, pad in CABINET.values():
        target = layout[pad]
        does = "(unused)" if target < 0 else label.get(pad_of[target], pad_of[target].upper())
        rows.append(f"  {name:<14} {does}")
    return "\n".join(rows)


def wait_for_press(event, cabinet_by_number, exit_button):
    """The cabinet button pressed on player 1's board: a CABINET key, "exit", or None (timed out)."""
    fd = os.open(event, os.O_RDONLY | os.O_NONBLOCK)
    try:
        while select.select([fd], [], [], 0)[0]:  # drop anything already queued
            os.read(fd, EVENT.size * 64)
        deadline = time.monotonic() + WAIT
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], deadline - time.monotonic())[0]:
                break
            data = os.read(fd, EVENT.size * 64)
            for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
                etype, code, value = EVENT.unpack_from(data, i)[2:]
                if etype == EV_KEY and value == 1:
                    n = code - BTN_FIRST
                    if n == exit_button:
                        return "exit"
                    if n in cabinet_by_number:
                        return cabinet_by_number[n]
        return None
    finally:
        os.close(fd)
        time.sleep(0.3)
        flush_keys()


def set_up(path, system, buttons_cfg):
    buttons, front, events = buttons_cfg
    cabinet_by_number = {buttons[es]: es for es in CABINET}
    game_buttons = GAME_BUTTONS.get(system, DEFAULT)
    layout = read_layout(path)
    for pad, label in game_buttons:
        target = RETROPAD[pad]
        now = [CABINET[es][0] for es, (_, src) in CABINET.items() if layout[src] == target]
        dialog("--infobox", f"Game button:  {label}\n\nPress the cabinet button you want for it."
               f"\n(now: {', '.join(now) or 'none'})\n\nEXIT = no button for it\n"
               f"wait {WAIT} seconds = keep it as it is", "11", "56")
        pressed = wait_for_press(events[1], cabinet_by_number, front["exit"])
        if pressed is None:
            continue
        for _, src in CABINET.values():  # this game button now comes from one cabinet button only
            if layout[src] == target:
                layout[src] = -1
        if pressed != "exit":
            layout[CABINET[pressed][1]] = target
    status, _ = dialog("--yes-label", "Save", "--no-label", "Cancel", "--yesno",
                       f"Buttons for this game:\n\n{describe(layout, game_buttons + DPAD)}\n\nSave them?",
                       "22", "60")
    if status == 0:
        write_layout(path, layout)
        dialog("--msgbox", "Saved. They apply every time this game starts.", "7", "56")


def set_sides(path, system):
    """Pick what each side button presses; unlike set_up, that game button keeps its usual button."""
    choices = GAME_BUTTONS.get(system, DEFAULT) + DPAD + [(None, "Nothing")]
    layout = read_layout(path)
    for side in SIDES:
        name, pad = CABINET[side]
        items = [x for i, (_, label) in enumerate(choices, 1) for x in (str(i), label)]
        status, choice = dialog("--cancel-label", "Back", "--menu", f"{name} button (pinball) does:",
                                "20", "50", "13", *items)
        if status != 0:
            return
        target = choices[int(choice) - 1][0]
        layout[pad] = RETROPAD[target] if target else -1
    status, _ = dialog("--yes-label", "Save", "--no-label", "Cancel", "--yesno",
                       f"Buttons for this game:\n\n{describe(layout, choices[:-1])}\n\nSave them?", "22", "60")
    if status == 0:
        write_layout(path, layout)
        dialog("--msgbox", "Saved. They apply every time this game starts.", "7", "56")


def retroarch_menu(system, rom, command):
    path = remap_path(system, command, rom)
    buttons_cfg = button_check()
    game_buttons = GAME_BUTTONS.get(system, DEFAULT)
    while True:
        items = ["1", "Set this game's buttons (press each one)", "2", "Show this game's buttons",
                 "3", "Set the side (pinball) buttons"]
        if os.path.exists(path):
            items += ["4", "Go back to the standard buttons"]
        game = os.path.splitext(os.path.basename(rom))[0]
        status, choice = dialog("--cancel-label", "Back", "--menu", game, "14", "60", "5", *items)
        if status != 0:
            return STAY
        if choice == "1":
            set_up(path, system, buttons_cfg)
        elif choice == "2":
            source = "this game's own layout" if os.path.exists(path) else "the standard layout"
            dialog("--msgbox", f"Cabinet button   does in this game ({source}):\n\n"
                   f"{describe(read_layout(path), game_buttons + DPAD)}", "19", "60")
        elif choice == "3":
            set_sides(path, system)
        elif choice == "4":
            os.remove(path)
            dialog("--msgbox", "This game uses the standard buttons again.", "7", "56")


def main():
    system, emulator, rom, command = sys.argv[1:5]
    flush_keys()
    if "/retroarch" in command and " -L " in command:
        return retroarch_menu(system, rom, command)
    for prefix, text in OWN_MENUS.items():
        if emulator.startswith(prefix):
            dialog("--msgbox", f"{emulator} has its own control settings:\n\n{text}", "12", "64")
            return STAY
    for prefix, other in SWITCH_TO.items():
        if emulator.startswith(prefix):
            status, _ = dialog("--yesno", f"{emulator} can't change buttons for a single game.\n\n"
                               f"Switch this game to {other} (RetroArch), which can? It may run "
                               "slower or differently. You can switch back from the launch menu "
                               "(\"Select emulator for ROM\").", "12", "64")
            if status != 0:
                return STAY
            write_choice(game_key(system, rom), other)
            dialog("--msgbox", f"This game now uses {other}. Start it again and open "
                   "Game controls to set its buttons.", "8", "64")
            return LEAVE
    dialog("--msgbox", f"{emulator} can't change buttons for a single game.", "7", "64")
    return STAY


if __name__ == "__main__":
    sys.exit(main())
