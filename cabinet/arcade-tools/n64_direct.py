#!/usr/bin/env python3
"""Run the N64 games RetroPie's launcher refuses on GLideN64 instead.

mupen64plus.sh quits at once for any ROM whose name contains "naboo" or "body" ("these games
need RSP-LLE"), and the lr-mupen64plus fallback only shows a black screen. GLideN64 has its own
code for these games; with frame buffer emulation off their textures draw (checked by screenshot).
Adds one N64 emulator entry that starts Mupen64Plus directly with the arguments mupen64plus.sh
uses on a Pi, and points those games at it. Re-runnable.

usage: n64_direct.py
"""
import os

from fix_failures import CONFIGS, game_key, write_choice

N64_EMULATORS = f"{CONFIGS}/n64/emulators.cfg"
NAME = "mupen64plus-GLideN64-direct"
COMMAND = ("SDL_VIDEO_RPI_SCALE_MODE=1 /opt/retropie/emulators/mupen64plus/bin/mupen64plus --noosd "
           "--windowed 320x240 --set Video-GLideN64[UseNativeResolutionFactor]\\=1 "
           "--set Video-GLideN64[EnableFBEmulation]\\=False --rsp mupen64plus-rsp-hle.so "
           "--gfx mupen64plus-video-GLideN64.so --audio mupen64plus-audio-sdl.so "
           f"--configdir {CONFIGS}/n64 --datadir {CONFIGS}/n64 %ROM%")
GAMES = ["Star Wars Episode I - Battle for Naboo (USA).n64", "Body Harvest (USA).n64"]
ROMS = os.path.expanduser("~/RetroPie/roms/n64")


def add_emulator():
    lines = [l for l in open(N64_EMULATORS) if l.split("=", 1)[0].strip() != NAME]
    lines.append(f'{NAME} = "{COMMAND}"\n')
    open(N64_EMULATORS, "w").writelines(lines)


def main():
    add_emulator()
    for rom in GAMES:
        assert os.path.isfile(os.path.join(ROMS, rom)), f"missing ROM: {rom}"
        write_choice(game_key("n64", rom), NAME)
        print(f"{rom} -> {NAME}")


if __name__ == "__main__":
    main()
