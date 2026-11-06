#!/usr/bin/env python3
"""Run a game with both cabinet boards merged into one joystick whose buttons are renumbered.

For emulators with hard-wired joystick buttons (LZDoom's menus accept only buttons 0 and 1,
which are the side button and EXIT on this cabinet). The cabinet service (cabinetd.py) owns the
boards: it makes the merged joystick while this connection is open, then this process becomes
the game and the connection (inherited) closes when the game exits.

usage: padmap.py FROM:TO[,FROM:TO...] COMMAND [ARGS...]   (button numbers as in button-map.json)
"""
import os
import socket
import sys

SOCKET = "/run/cabinet/pad.sock"
READY_TIMEOUT = 10


def main():
    assert len(sys.argv) >= 3, __doc__
    conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    conn.settimeout(READY_TIMEOUT)
    try:
        conn.connect(SOCKET)
        conn.sendall(f"MAP {sys.argv[1]}\n".encode())
        reply = conn.recv(64).decode().strip()
    except OSError as e:
        sys.exit(f"padmap: cabinet service not reachable at {SOCKET}: {e}")
    if reply != "ready":
        sys.exit(f"padmap: cabinet service refused the joystick: {reply or 'no reply'}")
    os.set_inheritable(conn.fileno(), True)  # the merged joystick lives as long as the game
    os.execvp(sys.argv[2], sys.argv[2:])


if __name__ == "__main__":
    main()
