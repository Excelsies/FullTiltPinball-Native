#!/usr/bin/env python3
"""Testing aid (root): the on-screen messages for a failed shut down / restart / lock, on the real
screen, without shutting down, restarting or locking anything.

Stops cabinetd.service, runs cabinetd.py's Cabinet in its place for SECONDS, then starts the service
again. In EVERY mode EmulationStation is left out of the sequence (its launcher would really power
off / reboot), systemctl is a command that fails and raspi-config is never the real one:
  poweroff    calls Cabinet.shutdown() after 1 s; systemctl = /bin/false (rc 1)
  reboot      systemctl = a missing program; choose Restart in the settings menu (press.py)
  lock-start  raspi-config = a missing program; choose Lock in the settings menu
  lock-check  raspi-config = /bin/true, /boot/config.txt = a missing file; choose Lock in the menu
  flag        calls Cabinet.shutdown() after 1 s with EmulationStation = a pid that cannot exist
              (os.kill fails) and the launcher flag = TEST_FLAG (never the real /tmp/es-*);
              'flag link' first makes TEST_FLAG a symlink (O_NOFOLLOW must refuse it)
Watch with ~/arcade-tools/snap, press with press.py meanwhile.

usage: sudo power_fail_test.py MODE SECONDS [link]
"""
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, "/home/pi/arcade-tools")
import cabinet_lock  # noqa: E402
import cabinetd  # noqa: E402
import power  # noqa: E402

MODES = ("poweroff", "reboot", "lock-start", "lock-check", "flag")
TEST_FLAG = "/tmp/cabinet-test-es-flag"
LINK_TARGET = "/tmp/cabinet-test-link-target"


def systemctl(*args):
    done = subprocess.run(["systemctl", *args, "cabinetd"])
    assert done.returncode == 0, f"systemctl {args} cabinetd: rc {done.returncode}"


def main():
    assert os.geteuid() == 0 and len(sys.argv) in (3, 4) and sys.argv[1] in MODES, __doc__
    mode, seconds, link = sys.argv[1], int(sys.argv[2]), sys.argv[3:] == ["link"]
    power.es_pid = lambda processes: None
    if mode == "flag":
        dead = int(open("/proc/sys/kernel/pid_max").read())  # pids are < pid_max: no such process
        power.es_pid = lambda processes: dead
        power.STEPS = {v: (pane, TEST_FLAG) + tuple(rest) for v, (pane, _, *rest) in power.STEPS.items()}
        if link:
            os.symlink(LINK_TARGET, TEST_FLAG)
    power.SYSTEMCTL = "/bin/false" if mode == "poweroff" else "/nonexistent/systemctl"
    cabinet_lock.RASPI_CONFIG = "/nonexistent/raspi-config" if mode == "lock-start" else "/bin/true"
    if mode == "lock-check":
        cabinet_lock.CONFIG = "/nonexistent/config.txt"
    systemctl("stop")
    try:
        cab = cabinetd.Cabinet()
        if mode in ("poweroff", "flag"):
            cab.timers.append((time.monotonic() + 1, cab.shutdown))
        signal.signal(signal.SIGALRM, lambda *_: sys.exit(0))
        signal.alarm(seconds)
        print(f"power_fail_test {mode}: running {seconds} s", flush=True)
        try:
            cab.run()
        finally:
            cab.menu.resume_game()
    finally:
        systemctl("start")


if __name__ == "__main__":
    main()
