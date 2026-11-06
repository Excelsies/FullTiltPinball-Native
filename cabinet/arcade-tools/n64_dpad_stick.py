#!/usr/bin/env python3
"""Kirby 64: the cabinet stick also presses the N64 D-pad.

Kirby 64 ignores the analog values the cabinet stick sends through Mupen64Plus (menus and Kirby
don't move) but reacts to the N64 D-pad. remap_controls.py leaves the D-pad empty on purpose
(stick = analog only), so these games get their own Mupen64Plus data dir, n64/dpad-on-stick/:
links to every file of the normal one except InputAutoCfg.ini, which is a copy with the boards'
D-pad on the stick axes as well. The copy is keyed by board name like the original, so it holds
whichever SDL number a board gets. The emulator entry rebuilds the copy at every launch, so it
follows any later button change. Otherwise the entry runs what mupen64plus.sh AUTO runs for these
games (GLideN64, 320x240). Re-runnable.

usage: n64_dpad_stick.py          add the emulator entry and point GAMES at it
       n64_dpad_stick.py --sync   rebuild n64/dpad-on-stick/ (the emulator entry runs this)
"""
import os
import re
import sys

from remap_controls import CFG, DEVICE, N64_AUTOCFG

N64_DIR = f"{CFG}/n64"
DATA = f"{N64_DIR}/dpad-on-stick"
AUTOCFG = os.path.basename(N64_AUTOCFG)
DPAD = {"DPad U": "axis(1-)", "DPad D": "axis(1+)", "DPad L": "axis(0-)", "DPad R": "axis(0+)"}
NAME = "mupen64plus-GLideN64-dpadstick"
COMMAND = (f"python3 {os.path.abspath(__file__)} --sync && "
           "SDL_VIDEO_RPI_SCALE_MODE=1 /opt/retropie/emulators/mupen64plus/bin/mupen64plus --noosd "
           "--set Video-GLideN64[UseNativeResolutionFactor]\\=1 --set Video-GLideN64[EnableLegacyBlending]\\=True "
           "--windowed --resolution 320x240 --rsp mupen64plus-rsp-hle.so --gfx mupen64plus-video-GLideN64.so "
           f"--audio mupen64plus-audio-sdl.so --configdir {N64_DIR} --datadir {DATA} %ROM%")
GAMES = ["Kirby 64 - The Crystal Shards (USA).n64"]
ROMS = os.path.expanduser("~pi/RetroPie/roms/n64")


def sync():
    os.makedirs(DATA, exist_ok=True)
    for name in os.listdir(N64_DIR):
        src, link = os.path.join(N64_DIR, name), os.path.join(DATA, name)
        if name != AUTOCFG and os.path.isfile(src) and not os.path.lexists(link):
            os.symlink(src, link)
    text = open(N64_AUTOCFG).read()
    start = text.index(f"[{DEVICE}]")
    end = text.index(f"; {DEVICE}_END", start)
    section = text[start:end]
    for key, value in DPAD.items():
        section, n = re.subn(rf"^{re.escape(key)} = .*$", f"{key} = {value}", section, flags=re.M)
        if not n:
            section = section.rstrip("\n") + f"\n{key} = {value}\n"
    tmp = os.path.join(DATA, AUTOCFG + ".tmp")
    open(tmp, "w").write(text[:start] + section + text[end:])
    os.replace(tmp, os.path.join(DATA, AUTOCFG))


def install():
    from fix_failures import game_key, write_choice
    emulators = f"{N64_DIR}/emulators.cfg"
    lines = [l for l in open(emulators) if l.split("=", 1)[0].strip() != NAME]
    lines.append(f'{NAME} = "{COMMAND}"\n')
    open(emulators, "w").writelines(lines)
    sync()
    for rom in GAMES:
        assert os.path.isfile(os.path.join(ROMS, rom)), f"missing ROM: {rom}"
        write_choice(game_key("n64", rom), NAME)
        print(f"{rom} -> {NAME}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--sync"]:
        sync()
    elif not sys.argv[1:]:
        install()
    else:
        sys.exit(__doc__)
