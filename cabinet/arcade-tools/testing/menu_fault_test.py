#!/usr/bin/env python3
"""Testing aid (root): faults in the settings menu's drawing / overlay / lock path, on the real screen.
cabinetd must survive each one: a message on screen (or the pane closed), boards released, nothing
on /boot changed, the game (if any) not left stopped.

Stops cabinetd.service, runs cabinetd.py's Cabinet in its place, feeds it board events in-process
(the same board_event() the service's loop calls), then starts the service again. Nothing is
really shut down, restarted or locked: EmulationStation is left out of power.py, systemctl is a
missing program, raspi-config is a test script and /boot is never written.
  enospc     every pane file write fails with ENOSPC (full /dev/shm) from the menu's opening on
  drawerror  a row drawing raises ValueError on a stick move (PIL error)
  overlay    the overlay process is killed while the menu is open, then the stick moves
  lockdecode Lock / Unlock: raspi-config writes bytes that are not UTF-8 into its log, rc 0
Prints what happened at each step and "RESULT survived" / "RESULT DIED" (run() raised).
Snapshots: /tmp/mft-MODE-N.ppm.

usage: sudo menu_fault_test.py MODE
"""
import errno
import os
import subprocess
import sys
import time
import traceback

sys.path.insert(0, "/home/pi/arcade-tools")
import cabinet_lock  # noqa: E402
import cabinetd  # noqa: E402
import gamepause  # noqa: E402
import panes  # noqa: E402
import power  # noqa: E402
import settings_pane  # noqa: E402
import vpad  # noqa: E402

MODES = ("enospc", "drawerror", "overlay", "lockdecode")
FAKE_DIR = "/run/cabinet-fault-test"
SNAP = "/home/pi/arcade-tools/snap"


def systemctl(*args):
    done = subprocess.run(["systemctl", *args, "cabinetd"])
    assert done.returncode == 0, f"systemctl {args} cabinetd: rc {done.returncode}"


class Driver:
    """Feeds board events into the Cabinet from its own timers."""

    def __init__(self, cab, name):
        """name: of the snapshots /tmp/NAME-N.ppm"""
        self.cab, self.name, self.t, self.shots = cab, name, time.monotonic() + 0.5, 0
        self.fd = {}
        for (event, button), (role, player) in cab.layout.items():
            self.fd[(role, player)] = (cab.fd_of[event], button)
        for p, role in ((1, "START"), (2, "START")):
            self.fd[("stick", p)] = (self.fd[(role, p)][0], None)

    def at(self, delay, fn, *args):
        self.t += delay
        self.cab.timers.append((self.t, lambda: fn(*args)))

    def button(self, role, player, secs=0.15, gap=0.5):
        fd, b = self.fd[(role, player)]
        self.at(0, self.cab.board_event, fd, vpad.EV_KEY, vpad.BTN_FIRST + b, 1)
        self.at(secs, self.cab.board_event, fd, vpad.EV_KEY, vpad.BTN_FIRST + b, 0)
        self.t += gap

    def stick(self, code, value, player=1, gap=0.5):
        fd = self.fd[("stick", player)][0]
        self.at(0, self.cab.board_event, fd, vpad.EV_ABS, code, value)
        self.at(0.15, self.cab.board_event, fd, vpad.EV_ABS, code, 127)
        self.t += gap

    def goto(self, key):
        """Move the cursor down to key (counted when this step runs)."""
        def go():
            steps = self.cab.menu.keys.index(key) - self.cab.menu.cursor
            fd = self.fd[("stick", 1)][0]
            for i in range(steps):
                when = time.monotonic() + 0.3 * i
                self.cab.timers.append((when, lambda: self.cab.board_event(fd, vpad.EV_ABS, 1, 255)))
                self.cab.timers.append((when + 0.15, lambda: self.cab.board_event(fd, vpad.EV_ABS, 1, 127)))
        self.at(0, go)
        self.t += 0.3 * 10 + 0.5

    def note(self, text):
        self.at(0, self.report, text)

    def report(self, text):
        cab, menu = self.cab, self.cab.menu
        self.shots += 1
        shot = f"/tmp/{self.name}-{self.shots}.ppm"
        rc = subprocess.run([SNAP, shot], stdout=subprocess.DEVNULL).returncode
        pid, name = cabinetd.running_game()
        game = f"{name} {pid} state {gamepause.state(pid)}" if pid else "none"
        print(f"[{text}] pane={cab.pane} view={menu.view} grabbed={cab.grabbed} paused={menu.paused} "
              f"game={game} overlay alive={cab.overlay.proc.poll() is None} snap={shot} rc {rc}", flush=True)


def fake_raspi_config():
    os.makedirs(FAKE_DIR, exist_ok=True)
    script = os.path.join(FAKE_DIR, "raspi-config")
    with open(script, "w") as fh:
        fh.write("#!/bin/sh\nprintf 'test raspi-config %s\\n\\377\\376 not utf-8\\n' \"$*\"\nexit 0\n")
    os.chmod(script, 0o755)
    for name, text in (("cmdline.txt", "console=tty1 root=PARTUUID=test\n"), ("config.txt", "# test\n")):
        with open(os.path.join(FAKE_DIR, name), "w") as fh:
            fh.write(text)
    cabinet_lock.RASPI_CONFIG = script
    cabinet_lock.CMDLINE = os.path.join(FAKE_DIR, "cmdline.txt")
    cabinet_lock.CONFIG = os.path.join(FAKE_DIR, "config.txt")


def enospc(*_):
    raise OSError(errno.ENOSPC, "No space left on device (test)")


def plan(d, mode):
    cab = d.cab
    menu_hold = lambda: d.button("MENU", 0, secs=2.4, gap=0.8)  # noqa: E731
    if mode == "enospc":
        real_write = panes.write_rgba
        d.at(0, setattr, panes, "write_rgba", enospc)
        menu_hold()
        d.note("after MENU held 2.4 s, pane writes failing")
        d.stick(1, 255)
        d.note("after a stick move")
        d.button("B", 1)
        d.note("after B")
        d.at(0, setattr, panes, "write_rgba", real_write)
        menu_hold()
        d.note("writes work again: menu opens")
        d.button("B", 1)
        d.note("after B")
    elif mode == "drawerror":
        menu_hold()
        d.note("menu open")
        real = settings_pane.ListPane._row

        def bad_row(self, draw, y, label, value, on):
            if on and label != "Volume":
                raise ValueError("test: PIL drawing failed")
            return real(self, draw, y, label, value, on)
        d.at(0, setattr, settings_pane.ListPane, "_row", bad_row)
        d.stick(1, 255)
        d.note("after a stick move (row drawing raises)")
        d.at(0, setattr, settings_pane.ListPane, "_row", real)
        d.button("A", 1)
        d.note("after A")
        d.button("B", 1)
        d.note("after B")
    elif mode == "overlay":
        menu_hold()
        d.note("menu open")
        d.at(0, lambda: (cab.overlay.proc.kill(), cab.overlay.proc.wait()))
        d.note("overlay killed")
        d.stick(1, 255)
        d.note("after a stick move")
        d.button("A", 1)
        d.note("after A")
        d.button("B", 1)
        d.note("after B (closed)")
        d.t += 1.5  # the snapshot above blocks the loop for about a second
        menu_hold()
        d.note("menu opened again")
        d.button("B", 1)
        d.note("after B")
    elif mode == "lockdecode":
        fake_raspi_config()
        menu_hold()
        d.goto("unlock" if cabinet_lock.is_locked() else "lock")
        d.note("on Lock / Unlock")
        d.button("A", 1)
        d.stick(0, 255)
        d.note("confirm, Yes chosen")
        d.button("A", 1, gap=3.0)
        d.note("3 s after Yes (fake raspi-config ended)")
        d.button("A", 1)
        d.note("after A")
        d.button("B", 1)
        d.note("after B")
    d.t += 1.0
    d.at(0, sys.exit, 0)


def main():
    assert os.geteuid() == 0 and len(sys.argv) == 2 and sys.argv[1] in MODES, __doc__
    mode = sys.argv[1]
    power.es_pid = lambda processes: None
    power.SYSTEMCTL = "/nonexistent/systemctl"
    cabinet_lock.RASPI_CONFIG = "/nonexistent/raspi-config"
    cabinet_lock.CMDLINE = cabinet_lock.CONFIG = "/nonexistent/boot-file"
    boot_before = subprocess.run("sha256sum /boot/cmdline.txt /boot/config.txt", shell=True,
                                 stdout=subprocess.PIPE, universal_newlines=True).stdout
    systemctl("stop")
    died = None
    try:
        cab = cabinetd.Cabinet()
        plan(Driver(cab, f"mft-{mode}"), mode)
        print(f"menu_fault_test {mode}: running", flush=True)
        try:
            cab.run()
        except SystemExit:
            pass
        except Exception:  # what would have ended the service
            died = traceback.format_exc()
        finally:
            cab.menu.resume_game()
        if died:
            print(f"run() raised (cabinetd would have exited):\n{died}", flush=True)
            if cab.grabbed:
                for fd in cab.boards:
                    vpad.grab(fd, False)
            cab.overlay.proc.kill()
        print(f"end: pane={cab.pane} grabbed={cab.grabbed} paused={cab.menu.paused} "
              f"stopped game pids left={[p for p in cab.menu.paused if gamepause.state(p) == 'T']}")
    finally:
        systemctl("start")
        boot_after = subprocess.run("sha256sum /boot/cmdline.txt /boot/config.txt", shell=True,
                                    stdout=subprocess.PIPE, universal_newlines=True).stdout
        print(f"/boot unchanged: {boot_before == boot_after}")
        print(f"RESULT {'DIED' if died else 'survived'}", flush=True)


if __name__ == "__main__":
    main()
