#!/usr/bin/env python3
"""Fix the games that failed launch_test.py, then find a working emulator for the rest.

1. DS games saved as raw .nds with a .zip name: rename to .nds (gamelist paths follow).
2. Zips carrying __MACOSX junk (RetroArch loads the ._ resource file): rewrite without it.
3. Every game still failing: try each other emulator of its system as the per-game choice
   in configs/all/emulators.cfg, keep the first that starts; restore the old choice if none does.
Originals of rewritten files go to ~/old-setup/rom-originals. Results: --out (default ~/launch-test/fix-results.csv).
Re-runnable: each step only acts on files/games that still need it.

usage: fix_failures.py [--hold SECONDS] [--results IN] [--out OUT] [--console]
       (run with EmulationStation stopped; --console as for launch_test.py)
"""
import argparse
import csv
import os
import re
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launch_test  # noqa: E402  (same directory)

HOME = os.path.expanduser("~")
CONFIGS = "/opt/retropie/configs"
EMU_CFG = f"{CONFIGS}/all/emulators.cfg"
GAMELIST_DIRS = [f"{CONFIGS}/all/emulationstation/gamelists", f"{HOME}/RetroPie/roms"]
ORIGINALS = f"{HOME}/old-setup/rom-originals"
# Launch results in the order they were produced; later files override earlier ones per game.
RESULT_FILES = [f"{HOME}/launch-test/{n}.csv"
                for n in ("results", "fix-results", "console-results", "console-fix-results")]
NDS_LOGO = bytes.fromhex("24ffae51699aa221")  # first bytes of the Nintendo logo at 0xC0


def keep_original(system, path):
    dest = os.path.join(ORIGINALS, system, os.path.basename(path))
    if not os.path.exists(dest):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(path, dest)


def is_raw_nds(path):
    with open(path, "rb") as fh:
        fh.seek(0xC0)
        return not zipfile.is_zipfile(path) and fh.read(8) == NDS_LOGO


def rename_in_gamelists(system, old, new):
    for d in GAMELIST_DIRS:
        gl = os.path.join(d, system, "gamelist.xml")
        if os.path.exists(gl):
            text = open(gl, encoding="utf-8").read()
            fixed = text.replace(f"./{old}<", f"./{new}<")
            if fixed != text:
                shutil.copy2(gl, gl + ".pre-fixpass")
                open(gl, "w", encoding="utf-8").write(fixed)
                print(f"  gamelist updated: {gl}")


def fix_raw_nds(path):
    new = path[:-4] + ".nds"
    if os.path.exists(new):
        raise SystemExit(f"refusing: {new} already exists")
    os.rename(path, new)
    rename_in_gamelists("nds", os.path.basename(path), os.path.basename(new))
    print(f"renamed {os.path.basename(path)} -> .nds")
    return new


def strip_macosx(system, path):
    keep_original(system, path)
    tmp = path + ".tmp"
    with zipfile.ZipFile(path) as src, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as dst:
        for info in src.infolist():
            if not info.filename.startswith("__MACOSX"):
                dst.writestr(info, src.read(info))
    with zipfile.ZipFile(tmp) as check:
        assert check.testzip() is None, f"rewritten zip is corrupt: {tmp}"
    os.replace(tmp, path)
    print(f"stripped __MACOSX from {os.path.basename(path)}")


def game_key(system, rom):
    # runcommand: clean_name "${SYSTEM}_${ROM_BN}" (basename without extension)
    bn = os.path.splitext(os.path.basename(rom))[0]
    return re.sub(r"[^a-zA-Z0-9_\-]", "", f"{system}_{bn}".replace("/", "_"))


def read_choice(key):
    for line in open(EMU_CFG, newline=""):
        if line.split("=", 1)[0].strip() == key:
            return line.split("=", 1)[1].strip().strip('"')
    return None


def write_choice(key, emu):
    """Set (emu) or remove (None) one per-game line; the file uses CRLF line endings."""
    lines = [l for l in open(EMU_CFG, newline="") if l.split("=", 1)[0].strip() != key]
    if emu:
        lines.append(f'{key} = "{emu}"\r\n')
    tmp = EMU_CFG + ".tmp"
    open(tmp, "w", newline="").writelines(lines)
    os.replace(tmp, EMU_CFG)


def candidates(system):
    names = [l.split("=", 1)[0].strip() for l in open(f"{CONFIGS}/{system}/emulators.cfg")]
    return [n for n in names if n and n != "default" and "neocd" not in n]


def try_emulators(system, rom, command, hold, console):
    key = game_key(system, rom)
    original = read_choice(key)
    for emu in candidates(system):
        if emu == original:
            continue
        write_choice(key, emu)
        status, rc, secs = launch_test.run_one(rom, command, hold, console)
        print(f"  {emu}: {status} ({secs}s)", flush=True)
        if status == "PASS":
            return emu
    write_choice(key, original)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hold", type=float, default=12)
    ap.add_argument("--results", default=RESULT_FILES[0])
    ap.add_argument("--out", default=RESULT_FILES[1])
    ap.add_argument("--console", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(EMU_CFG + ".pre-fixpass"):
        shutil.copy2(EMU_CFG, EMU_CFG + ".pre-fixpass")
    commands = {s: c for s, _, c in launch_test.games(set(), 0)}

    failed = []
    with open(args.results, newline="") as fh:
        for r in csv.DictReader(fh):
            if r["status"] == "FAIL":
                failed.append((r["system"], r["rom"]))

    with open(args.out, "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["system", "rom", "fix", "status"])
        for i, (system, rom) in enumerate(failed, 1):
            print(f"[{i}/{len(failed)}] {system} {os.path.basename(rom)}", flush=True)
            fix = []
            if rom.endswith(".zip") and os.path.exists(rom) and system == "nds" and is_raw_nds(rom):
                rom = fix_raw_nds(rom)
                fix.append("renamed .nds")
            if not os.path.exists(rom):
                alt = rom[:-4] + ".nds"
                rom = alt if os.path.exists(alt) else rom
            if zipfile.is_zipfile(rom) and any(n.startswith("__MACOSX")
                                               for n in zipfile.ZipFile(rom).namelist()):
                strip_macosx(system, rom)
                fix.append("stripped __MACOSX")
            status, _, _ = launch_test.run_one(rom, commands[system], args.hold, args.console)
            if status != "PASS":
                emu = try_emulators(system, rom, commands[system], args.hold, args.console)
                if emu:
                    status = "PASS"
                    fix.append(f"emulator {emu}")
            print(f"  => {status} {' + '.join(fix) or 'no fix found'}", flush=True)
            w.writerow([system, rom, " + ".join(fix), status])
            out.flush()
    print("ALL DONE", flush=True)


if __name__ == "__main__":
    main()
