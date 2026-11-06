"""Lock / unlock the cabinet from the settings menu: the same steps and checks as the RetroPie menu
entries ~/RetroPie/retropiemenu/cabinet-lock.sh and cabinet-unlock.sh (raspi-config's overlay file
system). cabinetd runs as root, so no sudo. The caller restarts the cabinet after a success.
"""
import os
import subprocess
import time

LOG = "/run/cabinet/lock.log"
LIMIT = 180  # seconds; raspi-config + update-initramfs took about 22 s when locking before
RASPI_CONFIG, CMDLINE, CONFIG = "raspi-config", "/boot/cmdline.txt", "/boot/config.txt"


def _boot_overlay(path):
    with open(path, errors="replace") as fh:
        return "boot=overlay" in fh.read().split()


def is_locked():
    return _boot_overlay("/proc/cmdline")


def pending():
    """True when /boot already asks for the other state at the next restart."""
    return _boot_overlay(CMDLINE) != is_locked()


def problems(kind):
    """What is missing after raspi-config ran ([] = done), as cabinet-lock.sh / -unlock.sh check it."""
    if kind == "unlock":
        return [f"{CMDLINE} still has boot=overlay"] if _boot_overlay(CMDLINE) else []
    initrd = f"initrd.img-{os.uname().release}-overlay"
    missing = [] if _boot_overlay(CMDLINE) else [f"{CMDLINE} has no boot=overlay"]
    if not os.path.isfile(f"/boot/{initrd}"):
        missing.append(f"/boot/{initrd} is missing")
    with open(CONFIG, errors="replace") as fh:
        if not any(line.startswith(f"initramfs {initrd}") for line in fh):
            missing.append(f"{CONFIG} has no 'initramfs {initrd}' line")
    return missing


class Job:
    """raspi-config nonint enable_overlayfs / disable_overlayfs, run in the background and polled."""

    def __init__(self, kind):
        """OSError (log file, raspi-config missing) when it cannot start: the caller shows it."""
        assert kind in ("lock", "unlock"), kind
        self.kind, self.started = kind, time.monotonic()
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        self.out = open(LOG, "w")
        action = "enable_overlayfs" if kind == "lock" else "disable_overlayfs"
        try:
            self.proc = subprocess.Popen([RASPI_CONFIG, "nonint", action], stdin=subprocess.DEVNULL,
                                         stdout=self.out, stderr=subprocess.STDOUT)
        except OSError:
            self.out.close()
            raise

    def poll(self):
        """None while running, else (ok, lines): lines = what to show (rc, checks, output tail)."""
        rc = self.proc.poll()
        if rc is None and time.monotonic() - self.started < LIMIT:
            return None
        if rc is None:
            self.proc.kill()
            rc = self.proc.wait()
            timed_out = [f"raspi-config did not finish within {LIMIT} s (stopped)"]
        else:
            timed_out = []
        self.out.close()
        try:
            checks = problems(self.kind)
        except (OSError, ValueError) as e:
            checks = [f"could not check the result: {e}"]
        try:
            with open(LOG, errors="replace") as fh:  # raspi-config's output may not be UTF-8
                tail = [line.rstrip() for line in fh if line.strip()][-4:]
        except (OSError, ValueError) as e:
            tail = [f"no raspi-config output: {e}"]
        missing = timed_out + ([] if rc == 0 else [f"raspi-config rc {rc}"]) + checks
        return not missing, missing + tail
