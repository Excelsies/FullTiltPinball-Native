#!/usr/bin/env python3
"""Testing aid (root): the settings menu's messages when amixer / aplay cannot start or the pause
list (/run/cabinet/paused.json) cannot be written, on the real screen; cabinetd must keep running.

Stops cabinetd.service, runs cabinetd.py's Cabinet in its place for SECONDS with PATH = BIN (a test
folder holding only the tools the mode keeps), then starts the service again. In both modes the
pause list goes to a path that cannot be created, so a game is left running (not stopped).
  missing  no amixer, no aplay: start-up, opening the menu, mute and volume give messages
  aplay    amixer but no aplay (the volume tick fails); with DROP, amixer leaves BIN after DROP
           seconds, so the next volume change cannot start amixer
Watch with ~/arcade-tools/snap, press with press.py meanwhile.

usage: sudo audio_fail_test.py MODE SECONDS [DROP]
"""
import os
import shutil
import signal
import subprocess
import sys
import time

sys.path.insert(0, "/home/pi/arcade-tools")
import cabinetd  # noqa: E402
import gamepause  # noqa: E402

BIN = "/run/cabinet-test-bin"
TOOLS = {"missing": ["udevadm"], "aplay": ["udevadm", "amixer"]}  # udevadm: vpad makes its devices
NO_STATE = "/dev/null/cabinet/paused.json"  # makedirs fails: NotADirectoryError


def systemctl(*args):
    done = subprocess.run(["systemctl", *args, "cabinetd"])
    assert done.returncode == 0, f"systemctl {args} cabinetd: rc {done.returncode}"


def drop_amixer():
    os.remove(os.path.join(BIN, "amixer"))
    print("audio_fail_test: amixer removed from the test PATH", flush=True)


def main():
    assert os.geteuid() == 0 and len(sys.argv) in (3, 4) and sys.argv[1] in TOOLS, __doc__
    mode, seconds = sys.argv[1], int(sys.argv[2])
    assert not os.path.exists(BIN), f"{BIN} exists: a test run is going on or was killed (remove it)"
    os.makedirs(BIN)
    for tool in TOOLS[mode]:
        found = shutil.which(tool)
        assert found, f"{tool} not found"
        os.symlink(found, os.path.join(BIN, tool))
    gamepause.STATE = NO_STATE
    systemctl("stop")
    path = os.environ["PATH"]
    os.environ["PATH"] = BIN
    try:
        cab = cabinetd.Cabinet()
        if len(sys.argv) == 4:
            cab.timers.append((time.monotonic() + float(sys.argv[3]), drop_amixer))
        signal.signal(signal.SIGALRM, lambda *_: sys.exit(0))
        signal.alarm(seconds)
        print(f"audio_fail_test {mode}: running {seconds} s", flush=True)
        try:
            cab.run()
        finally:
            cab.menu.resume_game()
    finally:
        os.environ["PATH"] = path
        shutil.rmtree(BIN)
        systemctl("start")


if __name__ == "__main__":
    main()
