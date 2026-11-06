#!/usr/bin/env python3
"""Testing aid (root): the settings menu's Theme item when the change fails, on the real screen.

Stops cabinetd.service, runs cabinetd.py's Cabinet in its place, drives the menu in-process (as
menu_fault_test.py does: MENU held, Theme, stick to TARGET, A, Yes), then starts the service again.
EmulationStation really restarts (once or twice); nothing is shut down, restarted or locked
(systemctl for power.py is a missing program).
  writefail  writing es_settings.cfg fails: "Theme not changed", ES back with the theme in use
  notready   ES never reports ready with TARGET (8 s limit): the previous file is put back,
             ES restarted with it
  noes       EmulationStation not found: the Theme item is not in the list
  nothemes   no theme folder readable: the row says "unavailable", A shows why
Prints each step's state, the theme_switch log lines, and at the end ThemeSet and ES's pid.
Snapshots: /tmp/tft-MODE-N.ppm.

usage: sudo theme_fail_test.py MODE [TARGET]   (TARGET: an installed theme, default carbon)
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, "/home/pi/arcade-tools")
import cabinetd  # noqa: E402
import power  # noqa: E402
import theme  # noqa: E402
import theme_switch  # noqa: E402
from menu_fault_test import Driver, systemctl  # noqa: E402

MODES = {"writefail": 60, "notready": 95, "noes": 4, "nothemes": 4}  # seconds to wait after Yes


def patch(mode):
    if mode == "writefail":
        real = theme.write_file

        def failing(path, *args):
            if path == theme.SETTINGS:
                raise OSError(30, "Read-only file system (test)")
            return real(path, *args)
        theme.write_file = failing
    elif mode == "notready":
        real_poll, real_ready = theme_switch.Switch.poll, theme_switch.Switch.ready

        def poll(self):
            theme_switch.READY_WAIT = 90 if self.restoring else 8
            return real_poll(self)
        theme_switch.Switch.poll = poll
        theme_switch.Switch.ready = lambda self: self.restoring and real_ready(self)
    elif mode == "noes":
        power.es_pid = lambda processes: None
    elif mode == "nothemes":
        theme.THEME_DIRS = ("/nonexistent/themes",)


def plan(d, mode, target):
    cab = d.cab
    d.button("MENU", 0, secs=2.4, gap=0.8)
    d.note(f"menu open: items {'with' if mode != 'noes' else 'without'} Theme expected")
    d.at(0, lambda: print(f"items: {cab.menu.keys}", flush=True))
    if mode == "noes":
        d.button("B", 1)
        d.note("closed")
        d.t += 1
        return d.at(0, sys.exit, 0)
    d.goto("theme")
    if mode == "nothemes":
        d.note("Theme row: unavailable")
        d.button("A", 1)
        d.note("after A: the reason")
        d.button("B", 1)
        d.button("B", 1)
        d.t += 1
        return d.at(0, sys.exit, 0)

    def to_target():
        steps = cab.menu.picker.names.index(target) - cab.menu.picker.index
        fd = d.fd[("stick", 1)][0]
        for i in range(steps % len(cab.menu.picker.names)):
            at = time.monotonic() + 0.3 * i
            cab.timers.append((at, lambda: cab.board_event(fd, 3, 0, 255)))
            cab.timers.append((at + 0.15, lambda: cab.board_event(fd, 3, 0, 127)))
    d.at(0, to_target)
    d.t += 0.3 * 7 + 0.5
    d.note(f"Theme row on {target}")
    d.button("A", 1)
    d.note("confirm (No chosen)")
    d.stick(0, 255)
    d.button("A", 1, gap=1.5)
    d.note("busy after Yes")
    for _ in range(MODES[mode] // 15):
        d.t += 15
        d.note("waiting")
    d.note("result")
    d.button("A", 1)
    d.note("after A: list")
    d.button("B", 1)
    d.t += 1
    d.at(0, sys.exit, 0)


def main():
    assert os.geteuid() == 0 and len(sys.argv) in (2, 3) and sys.argv[1] in MODES, __doc__
    mode, target = sys.argv[1], (sys.argv[2:] or ["carbon"])[0]
    assert target in theme.installed() and target != theme.current(), f"{target}: not installed or in use"
    power.SYSTEMCTL = "/nonexistent/systemctl"
    before = theme.current()
    patch(mode)
    systemctl("stop")
    try:
        cab = cabinetd.Cabinet()
        plan(Driver(cab, f"tft-{mode}"), mode, target)
        print(f"theme_fail_test {mode}: running (ThemeSet {before}, target {target})", flush=True)
        try:
            cab.run()
        except SystemExit:
            pass
        finally:
            cab.menu.resume_game()
            cab.menu.jobs.release()
    finally:
        systemctl("start")
    es = power.es_pid(cabinetd.processes) if mode != "noes" else "(not checked)"
    held = subprocess.run(["ps", "-o", "pid,stat,args", "-C", "emulationstatio"], stdout=subprocess.PIPE,
                          universal_newlines=True).stdout
    print(f"end: ThemeSet {theme.current()} (was {before}); ES pid {es}\n{held}", flush=True)


if __name__ == "__main__":
    main()
