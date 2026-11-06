#!/usr/bin/env python3
"""Map all cabinet buttons (from identify_buttons.py) into EmulationStation and every emulator.

1. es_input.cfg: the DragonRise entry uses 4.8 names (leftshoulder/rightshoulder instead of the
   old pageup/pagedown, which 4.8's emulator config scripts ignore) and the joystick also
   counts as the left analog stick (N64/PSX/Dreamcast games need it).
2. RetroPie's own inputconfiguration.sh regenerates RetroArch, Reicast and Mupen64Plus configs.
3. Mupen64Plus gets an arcade layout: bottom row A B Z, top row C-down C-up R, stick = analog only.
4. RetroArch per-system files that lost their #include of the global config are restored, and
   the old owner's MAME 2003 / MAME 2000 control files are moved aside (game defaults apply).
5. remap_standalone.py maps the emulators RetroPie's tool skips (DraStic, AdvanceMAME, ...).
Backups of every touched file go to ~/old-setup/pre-remap/ (each file once). Re-runnable.

usage: remap_controls.py   (then restart EmulationStation)
"""
import json
import os
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

import remap_standalone
import vpad

HOME = os.path.expanduser("~")
CFG = "/opt/retropie/configs"
ES_INPUT = f"{CFG}/all/emulationstation/es_input.cfg"
ES_TEMP = f"{CFG}/all/emulationstation/es_temporaryinput.cfg"
INPUTCONF = "/opt/retropie/supplementary/emulationstation/scripts/inputconfiguration.sh"
N64_AUTOCFG = f"{CFG}/n64/InputAutoCfg.ini"
BUTTON_MAP = f"{HOME}/arcade-tools/button-map.json"
BACKUP = f"{HOME}/old-setup/pre-remap"
DEVICE = "DragonRise Inc.   Generic   USB  Joystick  "
# MAME cores whose saved cfg files carried the old owner's control changes (MAME 2010's have none).
MAME_CFG_DIRS = {core: f"{HOME}/RetroPie/roms/arcade/{core}/cfg" for core in ("mame2003", "mame2000")}
TO_BACKUP = [ES_INPUT, N64_AUTOCFG, f"{CFG}/all/retroarch/autoconfig", f"{CFG}/dreamcast/mappings",
             f"{CFG}/n64/mupen64plus.cfg"] + remap_standalone.FILES
RA_INCLUDE = '#include "/opt/retropie/configs/all/retroarch.cfg"'
# Player 1 labels from the button check -> EmulationStation input names (both boards are wired alike).
LABELS = {"top row, LEFT button (Y)": "y", "top row, MIDDLE button (X)": "x",
          "top row, RIGHT button": "leftshoulder", "bottom row, LEFT button (B)": "b",
          "bottom row, MIDDLE button (A)": "a", "bottom row, RIGHT button (R)": "rightshoulder",
          "START": "start", "COIN": "select"}
AXES = {"up": ("1", "-1"), "down": ("1", "1"), "left": ("0", "-1"), "right": ("0", "1")}
# N64 arcade layout, by ES input name.
N64 = {"b": "A Button", "a": "B Button", "rightshoulder": "Z Trig",
       "y": "C Button D", "x": "C Button U", "leftshoulder": "R Trig"}


def backup(paths):
    """Copy each path into BACKUP the first time only, so the backup keeps the original."""
    for src in paths:
        dest = os.path.join(BACKUP, src.lstrip("/"))
        if os.path.exists(src) and not os.path.exists(dest):
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            (shutil.copytree if os.path.isdir(src) else shutil.copy2)(src, dest)
            print(f"backed up {src}")


def button_check():
    """(player 1 buttons by ES name, front buttons, event device per player) from the button check."""
    rows = {r["label"]: r for r in json.load(open(BUTTON_MAP))}
    missing = [l for l, r in rows.items() if r["button"] is None]
    assert not missing, f"button check is missing: {missing}"
    buttons = {name: rows[f"PLAYER 1: {label}"]["button"] for label, name in LABELS.items()}
    # Side (pinball) buttons as player 1's L2 / R2: the left one is on player 1's board, the right
    # one on player 2's, which cabinetd.py echoes onto player 1's board.
    left_side = rows["LEFT side of the cabinet: side button"]
    assert left_side["path"] == rows["PLAYER 1: START"]["path"], "left side button is not on player 1's board"
    buttons["lefttrigger"] = left_side["button"]
    buttons["righttrigger"] = vpad.SIDE_ECHO_BUTTON
    assert vpad.SIDE_ECHO_BUTTON not in [r["button"] for r in rows.values() if r["path"] == left_side["path"]], \
        "the side-button echo number is in use on player 1's board"
    front = {"exit": rows["FRONT panel: EXIT"]["button"], "mode": rows["FRONT panel: MODE"]["button"]}
    events = {}
    for p in (1, 2):
        js_link = rows[f"PLAYER {p}: START"]["path"]
        event = os.path.realpath(f"/dev/input/by-path/{js_link[:-len('-joystick')]}-event-joystick")
        assert os.path.exists(event), f"no event device for player {p}: {event}"
        events[p] = event
    return buttons, front, events


def fix_retroarch_includes():
    """Per-system retroarch.cfg must include the global one (a full dump ignores global changes)."""
    for system in sorted(os.listdir(CFG)):
        path = os.path.join(CFG, system, "retroarch.cfg")
        if system != "all" and os.path.isfile(path) and RA_INCLUDE not in open(path).read():
            backup([path])
            open(path, "w").write(
                "# Settings made here will only override settings in the global retroarch.cfg "
                f'if placed above the #include line\n\ninput_remapping_directory = "{CFG}/{system}/"'
                f"\n\n{RA_INCLUDE}\n")
            print(f"restored standard {path}")


def reset_mame_controls():
    """One-time per core: files MAME writes after the reset use the new mapping; keep them."""
    for core, cfg_dir in MAME_CFG_DIRS.items():
        dest = os.path.join(BACKUP, f"{core}-cfg")
        if os.path.isdir(cfg_dir) and os.listdir(cfg_dir) and not os.path.exists(dest):
            shutil.move(cfg_dir, dest)
            os.makedirs(cfg_dir)
            print(f"moved {core} per-game settings to {dest}")


def device_config(buttons):
    ic = ET.Element("inputConfig", type="joystick", deviceName=DEVICE,
                    deviceGUID="03000000790000000600000010010000")
    for name, num in buttons.items():
        ET.SubElement(ic, "input", name=name, type="button", id=str(num), value="1")
    for d, (axis, value) in AXES.items():
        ET.SubElement(ic, "input", name=d, type="axis", id=axis, value=value)
        ET.SubElement(ic, "input", name=f"leftanalog{d}", type="axis", id=axis, value=value)
    return ic


def update_es_input(ic):
    tree = ET.parse(ES_INPUT)
    root = tree.getroot()
    for old in [c for c in root if c.get("deviceName") == DEVICE]:
        root.remove(old)
    root.append(ic)
    tree.write(ES_INPUT)
    temp = ET.Element("inputList")
    temp.append(ic)
    ET.ElementTree(temp).write(ES_TEMP)


def n64_layout(buttons):
    text = open(N64_AUTOCFG).read()
    start = text.index(f"[{DEVICE}]")
    end = text.index(f"; {DEVICE}_END", start)
    section = text[start:end]
    keys = {v: f"button({buttons[k]})" for k, v in N64.items()}
    keys.update({"DPad U": "", "DPad D": "", "DPad L": "", "DPad R": "",
                 "C Button L": "", "C Button R": "", "L Trig": "",
                 "X Axis": "axis(0-,0+)", "Y Axis": "axis(1-,1+)", "Start": f"button({buttons['start']})"})
    for key, value in keys.items():
        line = f"{key} = {value}"
        section, n = re.subn(rf"^{re.escape(key)} = .*$", line, section, flags=re.M)
        if not n:
            section = section.rstrip("\n") + f"\n{line}\n"
    open(N64_AUTOCFG, "w").write(text[:start] + section + text[end:])


def main():
    buttons, front, events = button_check()
    backup(TO_BACKUP)
    ic = device_config(buttons)
    update_es_input(ic)
    subprocess.run(["bash", INPUTCONF], check=True)
    n64_layout(buttons)
    fix_retroarch_includes()
    reset_mame_controls()
    remap_standalone.apply(buttons, front, events)
    print("mapped:", buttons, "front:", front, "players:", events)


if __name__ == "__main__":
    main()
