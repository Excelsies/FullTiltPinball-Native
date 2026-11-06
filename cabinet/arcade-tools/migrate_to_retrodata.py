#!/usr/bin/env python3
"""Move everything games write onto the retrodata partition (so the system can be locked read-only).

  1. mount retrodata (partition 3) at /mnt/retrodata via /etc/fstab
  2. the whole RetroPie config tree (settings, per-game controls, DS / Dreamcast / N64 / Doom saves,
     EmulationStation lists and favourites) copied there and bind-mounted back over
     /opt/retropie/configs; the old copy underneath is left untouched as a fallback
  3. RetroArch saves and save states -> /mnt/retrodata/saves/<ROM folder name>/ (save files next
     to ROMs move there; the arcade cores' folders inside ROM folders move with them)
  4. AdvanceMAME's hi / nvram / ... folders and the PSP save folder move there, links left behind
  5. game lists kept in ROM folders move into EmulationStation's own list folder (now on retrodata)
Every file is copied, checked against the original, then the original removed; nothing is
overwritten. Re-runnable: finished steps are skipped. Run as root with EmulationStation stopped.

usage: sudo migrate_to_retrodata.py [--dry-run]
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys

DATA = "/mnt/retrodata"
PARTUUID = "16b3fe58-03"
CONFIGS = "/opt/retropie/configs"
ROMS = "/home/pi/RetroPie/roms"
SAVES = f"{DATA}/saves"
PSP_SAVES = f"{DATA}/ppsspp"
HOME_PSP = "/home/pi/.config/ppsspp"
ES_LISTS = f"{CONFIGS}/all/emulationstation/gamelists"
BACKUP = "/home/pi/old-setup/pre-retrodata"
SAVE_EXT = re.compile(r"\.(srm|state\d*|auto|rtc|ldci|eep|mpk|sra|fla)$", re.I)
CORE_DIRS = {"fbneo", "fba", "mame2000", "mame2003", "mame2010"}  # arcade cores' save folders
ADVMAME_DIRS = ["diff", "hi", "inp", "memcard", "nvram", "snap", "sta"]
RA_SETTINGS = {"savefile_directory": SAVES, "savestate_directory": SAVES,
               "sort_savefiles_by_content_enable": "true", "sort_savestates_by_content_enable": "true",
               "sort_savefiles_enable": "false", "sort_savestates_enable": "false"}
PI = (1000, 1000)
DRY = "--dry-run" in sys.argv


def say(*a):
    print(("[dry run] " if DRY else "") + " ".join(str(x) for x in a), flush=True)


def run(*cmd):
    if not DRY:
        subprocess.run(cmd, check=True)


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def move_file(src, dest):
    """Copy, compare, then remove the original. A different file already at dest stops everything."""
    if os.path.exists(dest):
        assert sha1(dest) == sha1(src), f"different file already at {dest} (from {src})"
    elif not DRY:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(src, dest)
        st = os.stat(src)
        os.chown(dest, st.st_uid, st.st_gid)
        assert sha1(dest) == sha1(src), f"copy of {src} does not match"
    if not DRY:
        os.remove(src)


def move_tree(src, dest):
    count = 0
    for root, _, files in os.walk(src):
        for f in files:
            s = os.path.join(root, f)
            if os.path.islink(s):
                continue
            move_file(s, os.path.join(dest, os.path.relpath(s, src)))
            count += 1
    if not DRY:
        for root, dirs, _ in os.walk(src, topdown=False):  # only empty folders are left
            for d in dirs:
                p = os.path.join(root, d)
                if os.path.isdir(p) and not os.path.islink(p) and not os.listdir(p):
                    os.rmdir(p)
        if not os.listdir(src):
            os.rmdir(src)
    return count


def chown_tree(path):
    if not DRY:
        subprocess.run(["chown", "-R", f"{PI[0]}:{PI[1]}", path], check=True)


def fstab_line(line):
    text = open("/etc/fstab").read()
    if line.split()[1] not in [l.split()[1] for l in text.splitlines() if l.strip() and not l.startswith("#")]:
        say("fstab +", line)
        if not DRY:
            shutil.copy2("/etc/fstab", os.path.join(BACKUP, "fstab"))
            open("/etc/fstab", "a").write(line + "\n")


# 1 -------------------------------------------------------------------------------------------
def mount_data():
    fstab_line(f"PARTUUID={PARTUUID}  {DATA}  ext4  defaults,noatime  0  2")
    if not os.path.ismount(DATA):
        if not DRY:
            os.makedirs(DATA, exist_ok=True)
        run("mount", DATA)
    if not DRY:
        assert os.path.ismount(DATA), f"{DATA} did not mount"
        os.makedirs(SAVES, exist_ok=True)
        os.chown(SAVES, *PI)


# 2 -------------------------------------------------------------------------------------------
def configs_onto_data():
    if os.path.ismount(CONFIGS):
        say("configs already on retrodata")
        return
    dest = f"{DATA}/configs"
    say(f"copying {CONFIGS} -> {dest}")
    run("rsync", "-aHAX", f"{CONFIGS}/", f"{dest}/")
    if not DRY:
        diff = subprocess.run(["rsync", "-aHAXn", "--checksum", "--itemize-changes", f"{CONFIGS}/", f"{dest}/"],
                              stdout=subprocess.PIPE, universal_newlines=True, check=True).stdout
        assert not diff.strip(), f"configs copy differs:\n{diff[:2000]}"
    fstab_line(f"{dest}  {CONFIGS}  none  bind,x-systemd.requires-mounts-for={DATA}  0  0")
    run("mount", CONFIGS)
    if not DRY:
        assert os.path.ismount(CONFIGS), "configs bind mount failed"


# 3 -------------------------------------------------------------------------------------------
def set_ini(path, values):
    """key = "value" lines; per-system files take overrides above their #include line."""
    text = open(path).read()
    for key, value in values.items():
        line = f'{key} = "{value}"'
        text, n = re.subn(rf"^{re.escape(key)}\s*=.*$", line, text, flags=re.M)
        if not n:
            if re.search(r"^#include", text, re.M):
                text = re.sub(r"^#include", line + "\n#include", text, count=1, flags=re.M)
            else:
                text = text.rstrip("\n") + f"\n{line}\n"
        assert re.search(rf"^{re.escape(line)}$", text, re.M), f"{path}: could not set {key}"
    if not DRY:
        open(path, "w").write(text)


def retroarch_saves():
    say("RetroArch: saves and states ->", SAVES + "/<ROM folder>")
    set_ini(f"{CONFIGS}/all/retroarch.cfg", RA_SETTINGS)
    # PSP keeps PPSSPP's own folder layout (PSP/SAVEDATA ...): one folder, not sorted
    set_ini(f"{CONFIGS}/psp/retroarch.cfg", {"savefile_directory": PSP_SAVES,
                                             "sort_savefiles_by_content_enable": "false"})


def rom_saves():
    moved = cores = 0
    for root, dirs, files in os.walk(ROMS):
        rel = os.path.relpath(root, ROMS)
        if rel != "." and os.path.basename(root) in CORE_DIRS and rel.count(os.sep) == 1:
            system = rel.split(os.sep)[0]
            cores += move_tree(root, os.path.join(SAVES, system, os.path.basename(root)))
            dirs[:] = []
            continue
        for f in files:
            if SAVE_EXT.search(f):
                move_file(os.path.join(root, f), os.path.join(SAVES, os.path.basename(root), f))
                moved += 1
    chown_tree(SAVES)
    say(f"save files moved: {moved}; files from arcade core folders: {cores}")


# 4 -------------------------------------------------------------------------------------------
def move_and_link(src, dest):
    if os.path.islink(src):
        assert os.path.realpath(src) == dest, f"{src} links somewhere unexpected"
        return 0
    n = move_tree(src, dest) if os.path.isdir(src) else 0
    if not DRY:
        os.makedirs(dest, exist_ok=True)
        os.chown(dest, *PI)
        if os.path.isdir(src):
            assert not os.listdir(src), f"{src} not empty after the move"
            os.rmdir(src)
        os.symlink(dest, src)
        os.lchown(src, *PI)
    say(f"{src} -> {dest} ({n} files, link left)")
    return n


def other_saves():
    for d in ADVMAME_DIRS:
        move_and_link(f"{ROMS}/mame-advmame/{d}", f"{SAVES}/mame-advmame/{d}")
    move_and_link(HOME_PSP, PSP_SAVES)
    chown_tree(SAVES)
    chown_tree(PSP_SAVES)


# 5 -------------------------------------------------------------------------------------------
def gamelists():
    moved = 0
    for system in sorted(os.listdir(ROMS)):
        src = os.path.join(ROMS, system, "gamelist.xml")
        if not os.path.isfile(src):
            continue
        dest = os.path.join(ES_LISTS, system, "gamelist.xml")
        if os.path.exists(dest) and sha1(dest) != sha1(src):
            keep = os.path.join(BACKUP, "es-gamelists", system, "gamelist.xml")
            say(f"older list in EmulationStation's folder for {system} saved to {keep}")
            if not DRY:
                os.makedirs(os.path.dirname(keep), exist_ok=True)
                shutil.move(dest, keep)
        move_file(src, dest)
        moved += 1
    chown_tree(ES_LISTS)
    say(f"game lists moved: {moved}")


def main():
    assert os.geteuid() == 0, "run with sudo"
    es = subprocess.run(["pgrep", "-x", "emulationstatio"], stdout=subprocess.DEVNULL).returncode == 0
    assert DRY or not es, "stop EmulationStation first (sudo systemctl stop getty@tty1)"
    if not DRY:
        os.makedirs(BACKUP, exist_ok=True)
    mount_data()
    configs_onto_data()
    retroarch_saves()
    rom_saves()
    other_saves()
    gamelists()
    say("done")


if __name__ == "__main__":
    main()
