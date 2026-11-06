#!/usr/bin/env python3
"""Drop game-list entries whose file is not on the card (EmulationStation skips them anyway).

The previous owner's scraped lists name over 100,000 games that are not here; EmulationStation
parses and logs every one at each start. Only entries whose <path> does not exist are removed, so
no game disappears from the menus. Each list's original is kept in ~/old-setup/pre-prune/ the first
time. Re-runnable. Run with EmulationStation stopped (it rewrites lists when it quits).

usage: prune_gamelists.py [--dry-run]
"""
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ES_SYSTEMS = "/opt/retropie/configs/all/emulationstation/es_systems.cfg"
ES_LISTS = "/opt/retropie/configs/all/emulationstation/gamelists"
BACKUP = os.path.expanduser("~/old-setup/pre-prune")
DRY = "--dry-run" in sys.argv


def system_paths():
    root = ET.parse(ES_SYSTEMS).getroot()
    return {s.findtext("name"): os.path.expanduser(s.findtext("path")) for s in root.findall("system")}


def resolve(path, rom_dir):
    path = os.path.expanduser(path.strip())
    return os.path.normpath(os.path.join(rom_dir, path[2:]) if path.startswith("./") else path)


def prune(list_path, rom_dir):
    # some lists start with spaces before <?xml (EmulationStation accepts that; ElementTree does not)
    root = ET.fromstring(open(list_path, "rb").read().lstrip(b" \t\r\n\xef\xbb\xbf"))
    tree = ET.ElementTree(root)
    dead = [e for e in root if e.tag in ("game", "folder") and e.findtext("path")
            and not os.path.exists(resolve(e.findtext("path"), rom_dir))]
    kept = len([e for e in root if e.tag in ("game", "folder")]) - len(dead)
    if dead and not DRY:
        backup = os.path.join(BACKUP, os.path.relpath(list_path, "/"))
        if not os.path.exists(backup):
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copy2(list_path, backup)
        for e in dead:
            root.remove(e)
        tmp = list_path + ".tmp"
        tree.write(tmp, encoding="utf-8", xml_declaration=True)
        os.replace(tmp, list_path)
    return len(dead), kept


def main():
    es = subprocess.run(["pgrep", "-x", "emulationstatio"], stdout=subprocess.DEVNULL).returncode == 0
    assert DRY or not es, "stop EmulationStation first (it rewrites game lists when it quits)"
    paths = system_paths()
    total_dead = total_kept = 0
    for system in sorted(os.listdir(ES_LISTS)):
        list_path = os.path.join(ES_LISTS, system, "gamelist.xml")
        if not os.path.isfile(list_path):
            continue
        if system not in paths:
            print(f"{system}: not a system in es_systems.cfg, left alone")
            continue
        try:
            dead, kept = prune(list_path, paths[system])
        except ET.ParseError as e:  # EmulationStation's parser is more lenient: don't touch it
            print(f"{system}: CANNOT READ {list_path} ({e}), left alone")
            continue
        total_dead += dead
        total_kept += kept
        if dead:
            print(f"{system}: {dead} missing removed, {kept} kept")
    print(f"{'[dry run] ' if DRY else ''}total: {total_dead} entries for missing files removed, {total_kept} kept")


if __name__ == "__main__":
    main()
