#!/usr/bin/env python3
"""Map the cabinet buttons into the emulators RetroPie's input tool does not configure.

Called by remap_controls.py (which passes the button numbers); safe to re-run.
  SDL gamepad layout (EDuke32 reads it) - /etc/profile.d/05-cabinet-controls.sh
  EDuke32, LZDoom, DraStic, AdvanceMAME, Reicast (player ports; player_ports.py redoes these per launch)
Arcade button order (AdvanceMAME): bottom row B A R = buttons 1 2 3, top row Y X L = 4 5 6.
"""
import os
import re
import subprocess

CFG = "/opt/retropie/configs"
GUID = "03000000790000000600000010010000"
PROFILE_D = "/etc/profile.d/05-cabinet-controls.sh"
EDUKE = f"{CFG}/ports/duke3d/eduke32.cfg"
LZDOOM = f"{CFG}/ports/doom/lzdoom.ini"
DOOM_EMULATORS = f"{CFG}/ports/doom/emulators.cfg"
PADMAP = os.path.expanduser("~/arcade-tools/padmap.py")
DRASTIC = f"{CFG}/nds/drastic/config/drastic.cfg"
ADVMAME = f"{CFG}/mame-advmame/advmame.rc"
REICAST_CFG = f"{CFG}/dreamcast/emu.cfg"
REICAST_MAP = f"{CFG}/dreamcast/mappings/evdev_DragonRise Inc.   Generic   USB  Joystick  .cfg"
RETROARCH = f"{CFG}/all/retroarch.cfg"
FILES = [EDUKE, LZDOOM, DOOM_EMULATORS, DRASTIC, ADVMAME, REICAST_CFG, REICAST_MAP, RETROARCH]
UNSET = 65535  # DraStic: not mapped
DRASTIC_QUIT_KEY = 113  # SDL 1.2 key 'q' (cabinetd.py sends it)
RA_HOTKEY_KEY = "divide"  # keypad '/', unused by RetroArch (cabinetd.py sends it)


def set_keys(path, values, fmt, after=None):
    """Replace 'key<sep>value' lines (fmt % (key, value)); append missing ones after `after`."""
    text = open(path).read()
    for key, value in values.items():
        line = fmt % (key, value)
        pattern = rf"^{re.escape(key)}\s*=.*$" if "=" in fmt else rf"^{re.escape(key)} .*$"
        text, n = re.subn(pattern, line.replace("\\", r"\\"), text, flags=re.M)
        if not n:
            if after and after in text:
                text = text.replace(after, f"{after}\n{line}", 1)
            else:
                text = text.rstrip("\n") + f"\n{line}\n"
    open(path, "w").write(text)


def sdl_layout(b):
    mapping = (f"{GUID},Arcade Cabinet,a:b{b['b']},b:b{b['a']},x:b{b['y']},y:b{b['x']},"
               f"back:b{b['select']},start:b{b['start']},leftshoulder:b{b['leftshoulder']},"
               f"rightshoulder:b{b['rightshoulder']},dpup:-a1,dpdown:+a1,dpleft:-a0,dpright:+a0,"
               "platform:Linux,")
    body = ("# Arcade cabinet button layout for SDL gamepad games (written by remap_standalone.py)\n"
            f'export SDL_GAMECONTROLLERCONFIG="{mapping}"\n')
    subprocess.run(["sudo", "tee", PROFILE_D], input=body.encode(), stdout=subprocess.DEVNULL, check=True)


def eduke32():
    # SDL gamepad button numbers: 0 A, 1 B, 2 X, 3 Y, 4 Back, 9 L, 10 R, 11-14 D-pad up/down/left/right
    binds = {0: "Fire", 1: "Open", 2: "Jump", 3: "Crouch", 4: "Map", 9: "Inventory",
             10: "Next_Weapon", 11: "Move_Forward", 12: "Move_Backward", 13: "Turn_Left", 14: "Turn_Right"}
    set_keys(EDUKE, {f"ControllerButton{n}": f'"{v}"' for n, v in binds.items()}, "%s = %s", "[Controls]")


def lzdoom(b):
    # LZDoom's menus take only joystick buttons 0 (select) and 1 (back): the side button and
    # EXIT here. padmap.py runs it with A and B renumbered to 0 and 1 (both boards as one pad).
    assert b["a"] != 1 and b["b"] != 0, "A/B already sit on LZDoom's menu buttons"
    swap = {b["a"]: 0, 0: b["a"], b["b"]: 1, 1: b["b"]}
    mapping = ",".join(f"{k}:{v}" for k, v in swap.items())
    text = open(DOOM_EMULATORS).read()
    text, n = re.subn(r"(\S*padmap\.py \S+ )?/opt/retropie/ports/lzdoom/lzdoom",
                      f"{PADMAP} {mapping} /opt/retropie/ports/lzdoom/lzdoom", text)
    assert n, f"no LZDoom command in {DOOM_EMULATORS}"
    open(DOOM_EMULATORS, "w").write(text)
    # JoyN is (virtual) button N-1. Stick: left/right turns, up/down moves.
    joy = lambda name: f"Joy{swap.get(b[name], b[name]) + 1}"
    binds = {joy("b"): "+attack", joy("a"): "+use", joy("y"): "weapprev", joy("x"): "weapnext",
             joy("leftshoulder"): "+strafe", joy("rightshoulder"): "+speed",
             joy("start"): "menu_main", joy("select"): "togglemap"}
    text = open(LZDOOM).read()
    start = text.index("[Doom.Bindings]")
    end = text.find("\n[", start)
    end = len(text) if end < 0 else end
    section = re.sub(r"^Joy\d+=.*\n", lambda m: m.group() if m.group().split("=")[0] in binds else "",
                     text[start:end], flags=re.M)  # drop binds left on the old button numbers
    open(LZDOOM, "w").write(text[:start] + section + text[end:])
    set_keys(LZDOOM, {"use_joystick": "true"}, "%s=%s")
    set_keys(LZDOOM, binds, "%s=%s", "[Doom.Bindings]")
    # SDL still lists the two (silent, grabbed) boards first: the virtual pad is JS:2.
    for n in range(3):
        if f"[Joy:JS:{n}]" not in open(LZDOOM).read():
            open(LZDOOM, "a").write(f"\n[Joy:JS:{n}]\nAxis0map=0\nAxis1map=2\n")


def drastic(b, front):
    btn = lambda n: 1024 + n
    up, down, left, right = 1217, 1153, 1216, 1152  # axis 1-, 1+, 0-, 0+
    c = {"UP": up, "DOWN": down, "LEFT": left, "RIGHT": right,
         "A": btn(b["a"]), "B": btn(b["b"]), "X": btn(b["x"]), "Y": btn(b["y"]),
         "L": btn(b["leftshoulder"]), "R": btn(b["rightshoulder"]),
         "START": btn(b["start"]), "SELECT": btn(b["select"]),
         # MENU / EXIT / MODE combos come from cabinetd.py as keys (MODE is a hotkey now)
         "MENU": UNSET, "QUIT": UNSET,
         "UI_UP": up, "UI_DOWN": down, "UI_LEFT": left, "UI_RIGHT": right,
         "UI_SELECT": btn(b["a"]), "UI_BACK": btn(b["b"]), "UI_EXIT": btn(front["exit"]),
         "UI_PAGE_UP": btn(b["leftshoulder"]), "UI_PAGE_DOWN": btn(b["rightshoulder"]),
         "UI_SWITCH": btn(b["x"])}
    for t in ("UP", "DOWN", "LEFT", "RIGHT", "PRESS"):
        c[f"TOUCH_CURSOR_{t}"] = UNSET  # no second stick on the cabinet
    set_keys(DRASTIC, {f"controls_b[CONTROL_INDEX_{k}]": v for k, v in c.items()}, "%s = %s")
    set_keys(DRASTIC, {"controls_a[CONTROL_INDEX_QUIT]": DRASTIC_QUIT_KEY}, "%s = %s")


def retroarch_hotkey():
    # cabinetd.py holds this key with the shortcuts it sends; COIN stays RetroArch's own hotkey.
    set_keys(RETROARCH, {"input_enable_hotkey": f'"{RA_HOTKEY_KEY}"'}, "%s = %s")


def advmame(b, events):
    # AdvanceMAME numbers joysticks in /dev/input/eventN order, not by player: look each one up.
    order = ["b", "a", "rightshoulder", "y", "x", "leftshoulder"]  # MAME buttons 1-6
    by_number = sorted(events.values(), key=lambda e: int(e.rsplit("event", 1)[1]))
    # The "event" driver has a built-in profile for this board model that hides 6 of its 12
    # buttons; the "raw" driver sees all 12, numbered like the button check (checked with advj).
    m = {"device_joystick": "raw"}
    for p in (1, 2):
        joy = by_number.index(events[p])
        for d, axe in (("left", 0), ("right", 0), ("up", 1), ("down", 1)):
            m[f"input_map[p{p}_{d}]"] = f"joystick_digital[{joy},0,{axe},{d}]"
        for i, name in enumerate(order, 1):
            m[f"input_map[p{p}_button{i}]"] = f"joystick_button[{joy},{b[name]}]"
        m[f"input_map[start{p}]"] = f"joystick_button[{joy},{b['start']}]"
        m[f"input_map[coin{p}]"] = f"joystick_button[{joy},{b['select']}]"
    p1 = by_number.index(events[1])
    # Quit at once like every other emulator (safequit asks first), and drive AdvanceMAME's
    # own menus from player 1 (auto picks joystick 0, which is player 2's board here).
    m["misc_safequit"] = "no"
    for d, axe in (("left", 0), ("right", 0), ("up", 1), ("down", 1)):
        m[f"input_map[ui_{d}]"] = f"joystick_digital[{p1},0,{axe},{d}]"
    m["input_map[ui_select]"] = f"joystick_button[{p1},{b['a']}]"
    m["input_map[ui_cancel]"] = (f"keyboard[0,esc] or joystick_button[{p1},{b['select']}] "
                                 f"joystick_button[{p1},{b['start']}]")
    set_keys(ADVMAME, m, "%s %s")


def reicast(events):
    # Maple port 0 = player 1. Reicast stores ports by event device, and the boards' event numbers
    # change between boots: drop old entries (player_ports.py re-runs this before each launch).
    text = open(REICAST_CFG).read()
    open(REICAST_CFG, "w").write(re.sub(r"^maple_/dev/input/event\d+ = .*\n", "", text, flags=re.M))
    ports = {f"maple_{events[1]}": 0, f"maple_{events[2]}": 1,
             "device2": 0, "device2.1": 1, "device2.2": 8}
    set_keys(REICAST_CFG, ports, "%s = %s", "[input]")
    # No quit button here: both boards share this file, so it would make PLAY quit as well as
    # EXIT. cabinetd.py quits Reicast on EXIT / COIN or MODE + START.
    set_keys(REICAST_MAP, {"btn_escape": ""}, "%s = %s")


def apply(buttons, front, events):
    sdl_layout(buttons)
    eduke32()
    lzdoom(buttons)
    drastic(buttons, front)
    advmame(buttons, events)
    reicast(events)
    retroarch_hotkey()
    print("standalone emulators mapped: EDuke32, LZDoom, DraStic, AdvanceMAME, Reicast")
