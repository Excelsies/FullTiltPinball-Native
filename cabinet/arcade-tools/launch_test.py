#!/usr/bin/env python3
"""Launch every game with its system's EmulationStation command and record whether it starts.

PASS = emulator still running after HOLD seconds; FAIL = runcommand exited early.
Results append to results.csv; games already recorded are skipped, so the run resumes.

--console runs launches straight on this process's terminal: start it on the cabinet's own
screen (openvt -c 1) for emulators that refuse to start without the local console
(AdvanceMAME, DraStic, Reicast, standalone Mupen64Plus). After each launch it kills every
process left on that terminal, since some of those emulators outlive runcommand.

usage: launch_test.py [--hold SECONDS] [--limit N] [--per-system N] [--systems a,b]
                      [--roms file1,file2] [--results PATH] [--console]
"""
import argparse
import csv
import os
import re
import shlex
import shutil
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

ES_SYSTEMS = "/opt/retropie/configs/all/emulationstation/es_systems.cfg"
RC_LOG = "/dev/shm/runcommand.log"
OUT = os.path.expanduser("~/launch-test")
SKIP_SYSTEMS = {"retropie"}  # RetroPie's own menu scripts, not games
# Process names that are launch plumbing, not an emulator (comm is truncated to 15 chars).
WRAPPERS = {"script", "bash", "sh", "dialog", "sleep", "fbi", "runcommand.sh", "tput", "timeout"}


def games(systems_filter, per_system):
    for s in ET.parse(ES_SYSTEMS).getroot().findall("system"):
        name, path = s.findtext("name"), s.findtext("path")
        exts = tuple(e.lower() for e in (s.findtext("extension") or "").split())
        if name in SKIP_SYSTEMS or not exts or not os.path.isdir(path):
            continue
        if systems_filter and name not in systems_filter:
            continue
        command = s.findtext("command")
        files = sorted(f for f in os.listdir(path)
                       if f.lower().endswith(exts) and os.path.isfile(os.path.join(path, f)))
        for f in files[:per_system] if per_system else files:
            yield name, os.path.join(path, f), command


def descendants(root):
    """All live descendant pids of root (runcommand under script starts its own session)."""
    children = {}
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open(f"/proc/{d}/stat") as fh:
                    ppid = int(fh.read().rsplit(")", 1)[1].split()[1])
            except (OSError, IndexError, ValueError):
                continue
            children.setdefault(ppid, []).append(int(d))
    found, stack = [], [root]
    while stack:
        for c in children.get(stack.pop(), []):
            found.append(c)
            stack.append(c)
    return found


def stop_tree(proc):
    """TERM every process under the launch, then KILL whatever lingers."""
    for sig, wait in ((signal.SIGTERM, 8), (signal.SIGKILL, 5)):
        for pid in descendants(proc.pid):
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + wait
        while descendants(proc.pid) and time.monotonic() < deadline:
            time.sleep(0.5)
        if not descendants(proc.pid):
            break
    if proc.stdin:
        proc.stdin.close()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def stat_fields(pid):
    """Fields after the comm in /proc/<pid>/stat: [state, ppid, pgrp, session, tty_nr, ...]."""
    with open(f"/proc/{pid}/stat") as fh:
        return fh.read().rsplit(")", 1)[1].split()


def kill_tty_stragglers():
    """Kill everything on our controlling terminal except this process and its ancestors."""
    me, keep = os.getpid(), set()
    tty = stat_fields(me)[4]
    pid = me
    while pid > 1:
        keep.add(pid)
        pid = int(stat_fields(pid)[1])

    def stragglers():
        found = []
        for d in os.listdir("/proc"):
            if d.isdigit() and int(d) not in keep:
                try:
                    if stat_fields(d)[4] == tty:
                        found.append(int(d))
                except (OSError, IndexError):
                    continue
        return found

    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in stragglers():
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        while stragglers() and time.monotonic() < deadline:
            time.sleep(0.5)
    left = stragglers()
    assert not left, f"could not clear the console: {left}"


def fb_geometry():
    out = subprocess.run(["fbset", "-s"], stdout=subprocess.PIPE, universal_newlines=True, check=True).stdout
    return re.search(r"geometry (\d+) (\d+)", out).groups()


_SCREEN = []


def restore_screen():
    """A force-closed emulator never restores the video mode it set: put the original back."""
    if not _SCREEN:
        _SCREEN.append(fb_geometry())
    x, y = _SCREEN[0]
    if fb_geometry() != (x, y):
        subprocess.run(["tvservice", "-p"], stdout=subprocess.DEVNULL, check=True)
        subprocess.run(["fbset", "-depth", "8"], check=True)
        subprocess.run(["fbset", "-depth", "32", "-g", x, y, x, y, "32"], check=True)
        assert fb_geometry() == (x, y), "could not restore the screen resolution"


def run_one(rom, command, hold, console=False):
    if os.path.exists(RC_LOG):
        os.remove(RC_LOG)
    # Use the system's own es_systems <command>, as EmulationStation does (%ROM% shell-escaped).
    # runcommand redirects the emulator's stdin from /dev/tty: on the console that is the real
    # terminal; otherwise give it a pty via script(1), whose stdin stays an open pipe (EOF
    # on it would end the session early).
    if console:
        restore_screen()  # records the starting resolution on the first launch
    cmd = command.replace("%ROM%", shlex.quote(rom))
    argv, stdin = (["bash", "-c", cmd], None) if console else \
        (["script", "-qec", cmd, "/dev/null"], subprocess.PIPE)
    start = time.monotonic()
    proc = subprocess.Popen(argv, stdin=stdin, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=hold)
        status, rc = "FAIL", proc.returncode
        if proc.stdin:
            proc.stdin.close()
    except subprocess.TimeoutExpired:
        # Still running is only a PASS if a real emulator is up, not runcommand stuck in a dialog.
        status, rc = ("PASS", "") if emulator_running(proc.pid) else ("FAIL", "stuck-no-emulator")
        stop_tree(proc)
    if console:
        kill_tty_stragglers()
        restore_screen()
    return status, rc, round(time.monotonic() - start, 1)


def emulator_running(root):
    for pid in descendants(root):
        try:
            with open(f"/proc/{pid}/comm") as fh:
                if fh.read().strip() not in WRAPPERS:
                    return True
        except OSError:
            continue
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hold", type=float, default=12)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--per-system", type=int, default=0)
    ap.add_argument("--systems", default="")
    ap.add_argument("--roms", default="", help="only these file names (comma-separated)")
    ap.add_argument("--results", default=os.path.join(OUT, "results.csv"))
    ap.add_argument("--console", action="store_true")
    args = ap.parse_args()
    if args.console:
        assert os.isatty(0), "--console needs to run on a terminal (openvt -c 1)"

    os.makedirs(os.path.join(OUT, "fail-logs"), exist_ok=True)
    results = args.results
    only = set(filter(None, args.roms.split(",")))
    done = set()
    if os.path.exists(results):
        with open(results, newline="") as fh:
            done = {(r["system"], r["rom"]) for r in csv.DictReader(fh)}

    todo = [g for g in games(set(filter(None, args.systems.split(","))), args.per_system)
            if g[:2] not in done and (not only or os.path.basename(g[1]) in only)]
    if args.limit:
        todo = todo[:args.limit]
    print(f"{len(done)} already recorded, {len(todo)} to test", flush=True)

    new_file = not os.path.exists(results)
    with open(results, "a", newline="") as fh:
        w = csv.writer(fh)
        if new_file:
            w.writerow(["system", "rom", "status", "returncode", "seconds"])
        for i, (system, rom, command) in enumerate(todo, 1):
            status, rc, secs = run_one(rom, command, args.hold, args.console)
            w.writerow([system, rom, status, rc, secs])
            fh.flush()
            if status == "FAIL" and os.path.exists(RC_LOG):
                log = os.path.join(OUT, "fail-logs", f"{system}__{os.path.basename(rom)}.log")
                shutil.copy(RC_LOG, log)
            print(f"[{i}/{len(todo)}] {status} {system} {os.path.basename(rom)} ({secs}s)", flush=True)
    print("ALL DONE", flush=True)


if __name__ == "__main__":
    main()
