#!/usr/bin/env python3
"""Launch one game on the cabinet screen and drive it with simulated board presses (testing aid).

usage (root, EmulationStation stopped): ingame_test.py SYSTEM ROM STEP...   (ports: ROM = port script)
  wait N           sleep N seconds
  snap NAME        screenshot to /tmp/it-NAME.ppm
  tap BOARD BTN       press and release button BTN on board p1 / p2
  combo BOARD H BTN   hold H, tap BTN, release H (same board)
  game             print what cabinetd sees running
Ends by reporting whether the game is still running; stops it if so.
"""
import os
import signal
import struct
import subprocess
import sys
import time

sys.path.insert(0, "/home/pi/arcade-tools")
import cabinetd  # noqa: E402
from press import board_event  # noqa: E402

EV = struct.Struct("llHHi")


def write(board, btn, value):
    with open(board_event(board), "wb", buffering=0) as f:
        f.write(EV.pack(0, 0, 1, 288 + btn, value) + EV.pack(0, 0, 0, 0, 0))


def tap(ev, btn):
    write(ev, btn, 1)
    time.sleep(0.15)
    write(ev, btn, 0)


def main():
    system, rom, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
    launch = ["bash", rom] if system == "ports" else \
        ["/opt/retropie/supplementary/runcommand/runcommand.sh", "0", "_SYS_", system, rom]
    subprocess.run(["openvt", "-f", "-c", "1", "-s", "--", "sudo", "-u", "pi", "env", "TERM=linux"] + launch,
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    words = " ".join(steps).split()
    i = 0
    while i < len(words):
        op = words[i]
        if op == "wait":
            time.sleep(float(words[i + 1])); i += 2
        elif op == "snap":
            subprocess.run(["/home/pi/arcade-tools/snap", f"/tmp/it-{words[i + 1]}.ppm"], check=True); i += 2
        elif op == "tap":
            tap(words[i + 1], int(words[i + 2])); i += 3
        elif op == "combo":
            ev, h, btn = words[i + 1], int(words[i + 2]), int(words[i + 3])
            write(ev, h, 1); time.sleep(0.2); tap(ev, btn); time.sleep(0.2); write(ev, h, 0); i += 4
        elif op == "game":
            print("running:", cabinetd.running_game(), flush=True); i += 1
        else:
            sys.exit(f"unknown step {op}")
    pid, name = cabinetd.running_game()
    print("at the end:", "still running " + str((pid, name)) if pid else "game has quit", flush=True)
    stop_launches()


def stop_launches():
    """Stop every runcommand launch and what it started (they may have lost the console)."""
    for sig in (signal.SIGTERM, signal.SIGKILL):
        procs = cabinetd.processes()
        roots = [p for p, (_, comm) in procs.items() if comm == "bash" and cabinetd.is_runcommand(p)]
        tree, stack = set(roots), list(roots)
        while stack:
            parent = stack.pop()
            for p, (pp, _) in procs.items():
                if pp == parent and p not in tree:
                    tree.add(p)
                    stack.append(p)
        for p in sorted(tree, reverse=True):  # children first
            try:
                os.kill(p, sig)
            except ProcessLookupError:
                pass
        if not tree:
            return
        time.sleep(3)


if __name__ == "__main__":
    main()
