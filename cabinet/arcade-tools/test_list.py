#!/usr/bin/env python3
"""Pick one known-starting game for every emulator the games actually use, for a controls check.

Effective emulator per game = its per-game choice in configs/all/emulators.cfg, else the
system default. "Starts" = PASS in launch-test results (fix-pass results override).
Prefers the highest-rated game in the EmulationStation gamelist. Also lists installed
emulators that no game uses.

usage: test_list.py
"""
import csv
import functools
import os
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fix_failures  # noqa: E402
import launch_test  # noqa: E402

CONFIGS = fix_failures.CONFIGS
HOME = fix_failures.HOME


def per_game_choices():
    choices = {}
    for line in open(fix_failures.EMU_CFG, newline=""):
        if "=" in line:
            k, v = line.split("=", 1)
            choices[k.strip()] = v.strip().strip('"')
    return choices


def resolve(system, rom):
    """(system label, emulators.cfg path, per-game key). Port scripts name their own config."""
    if system == "ports":
        m = re.search(r'_PORT_ "([^"]+)" "([^"]*)"', open(rom).read())
        assert m, f"port script without a runcommand _PORT_ line: {rom}"
        port, port_rom = m.groups()
        return f"ports/{port}", f"{CONFIGS}/ports/{port}/emulators.cfg", fix_failures.game_key(port, port_rom)
    return system, f"{CONFIGS}/{system}/emulators.cfg", fix_failures.game_key(system, rom)


def system_emulators(cfg):
    emus = {}
    for line in open(cfg):
        if "=" in line:
            k, v = line.split("=", 1)
            emus[k.strip()] = v.strip().strip('"')
    return emus


def passing_roms():
    status = {}
    for path in fix_failures.RESULT_FILES:
        if os.path.exists(path):
            with open(path, newline="") as fh:
                for r in csv.DictReader(fh):
                    status[r["rom"]] = r["status"]
    return {rom for rom, s in status.items() if s == "PASS"}


@functools.lru_cache(maxsize=None)
def gamelist_info(system):
    """rom basename -> (display name, rating) from whichever gamelist exists."""
    info = {}
    for d in fix_failures.GAMELIST_DIRS:
        gl = os.path.join(d, system, "gamelist.xml")
        if os.path.exists(gl):
            for g in ET.parse(gl).getroot().findall("game"):
                bn = os.path.basename(g.findtext("path") or "")
                info[bn] = (g.findtext("name") or bn, float(g.findtext("rating") or 0))
    return info


def main():
    choices, ok = per_game_choices(), passing_roms()
    picks, used, installed = {}, set(), {}
    for es_system, rom, _ in launch_test.games(set(), 0):
        system, cfg, key = resolve(es_system, rom)
        emus = system_emulators(cfg)
        installed.setdefault(system, set()).update(e for e in emus if e != "default")
        emu = choices.get(key, emus.get("default"))
        used.add((system, emu))
        if rom not in ok:
            continue
        bn = os.path.basename(rom)
        name, rating = gamelist_info(es_system).get(bn, (bn, 0))
        best = picks.get((system, emu))
        if best is None or (-rating, name.lower()) < (-best[1], best[0].lower()):
            picks[(system, emu)] = (name, rating, bn)

    print("system\temulator\tgame\tfile")
    for (system, emu) in sorted(used):
        name, _, bn = picks.get((system, emu), ("-- NO STARTING GAME --", 0, ""))
        print(f"{system}\t{emu}\t{name}\t{bn}")
    print("\ninstalled but used by no game:")
    for system in sorted(installed):
        unused = sorted(e for e in installed[system] if (system, e) not in used)
        if unused:
            print(f"{system}\t{', '.join(unused)}")


if __name__ == "__main__":
    main()
