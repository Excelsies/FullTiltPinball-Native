"""Safe shut down / restart for cabinetd (the shutdown confirm and the settings menu).

One sequence for both: show the pane, quit the game, wait for it to go, then let EmulationStation
save its game lists (flag file for its launcher emulationstation.sh, which then runs sudo poweroff /
reboot, + SIGTERM), with systemctl poweroff / reboot as the fallback after WAIT; without
EmulationStation: systemctl directly. A failed step is logged and shown on screen
(SettingsMenu.failure) instead of ending cabinetd under the "Shutting down…" pane.
"""
import os
import pwd
import signal
import subprocess
import time

ES_BINARY = "/opt/retropie/supplementary/emulationstation/emulationstation"
WAIT = 20  # seconds for the game to quit, then for EmulationStation's launcher to power off / reboot
SYSTEMCTL = "systemctl"
# verb: (pane, EmulationStation launcher flag, what ES is asked to do, title of the failure message)
STEPS = {"poweroff": ("shutting-down", "/tmp/es-shutdown", "shut down", "Shut down failed"),
         "reboot": ("restarting", "/tmp/es-sysrestart", "restart the cabinet", "Restart failed")}


def es_pid(processes):
    """EmulationStation itself (its launcher scripts share the truncated process name), or None."""
    es = [p for p, (_, comm) in processes().items()
          if comm == "emulationstatio" and os.path.realpath(f"/proc/{p}/exe") == ES_BINARY]
    return es[0] if es else None


def make_flag(flag, uid, gid):
    """The launcher's flag file, owned by pi. Root in world-writable /tmp: O_NOFOLLOW (a link there
    fails with ELOOP instead of being written through) and fchown on the opened file, not the path."""
    fd = os.open(flag, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o644)
    try:
        os.fchown(fd, uid, gid)
    finally:
        os.close(fd)


class Power:
    def __init__(self, cab, running_game, processes, log):
        self.cab, self.running_game, self.processes, self.log = cab, running_game, processes, log

    def start(self, verb):
        """Cabinet.shutdown() ("poweroff") / Cabinet.restart() ("reboot")."""
        self.cab.show_pane(STEPS[verb][0])
        if self.running_game()[0]:
            self.cab.act("quit")
        deadline = time.monotonic() + WAIT
        self.cab.timers.append((time.monotonic() + 0.5, lambda: self.after_game(verb, deadline)))

    def after_game(self, verb, deadline):
        if self.running_game()[0] and time.monotonic() < deadline:
            self.cab.timers.append((time.monotonic() + 0.5, lambda: self.after_game(verb, deadline)))
            return
        _, flag, asked, _ = STEPS[verb]
        es = es_pid(self.processes)
        if not es:
            return self.final(verb)
        try:
            pi = pwd.getpwnam("pi")
            make_flag(flag, pi.pw_uid, pi.pw_gid)
            self.log(f"asking EmulationStation ({es}) to {asked}")
            os.kill(es, signal.SIGTERM)
        except (OSError, KeyError) as e:  # KeyError: no user pi
            return self.failed(verb, [f"Could not ask EmulationStation to {asked}: {e}"] + self.drop_flag(flag))
        self.cab.timers.append((time.monotonic() + WAIT, lambda: self.final(verb)))

    def drop_flag(self, flag):
        """After a failed step: a flag left behind would make the launcher reboot / power off the next
        time EmulationStation exits for any reason. Returns the problem lines for the message."""
        try:
            os.remove(flag)
        except FileNotFoundError:
            return []
        except OSError as e:
            self.log(f"could not remove {flag}: {e}")
            return [f"Could not remove {flag}: {e}", "Remove it before EmulationStation exits."]
        self.log(f"removed {flag}")
        return []

    def final(self, verb):
        self.log({"poweroff": "powering off", "reboot": "rebooting"}[verb])
        try:
            done = subprocess.run([SYSTEMCTL, verb], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, universal_newlines=True, timeout=30)
        except (OSError, subprocess.SubprocessError) as e:
            return self.failed(verb, [f"{SYSTEMCTL} {verb} could not run: {e}"])
        if done.returncode != 0:
            tail = [line for line in done.stdout.splitlines() if line.strip()][-3:]
            self.failed(verb, [f"{SYSTEMCTL} {verb} failed (rc {done.returncode})"] + tail)

    def failed(self, verb, lines):
        self.log(f"{verb} FAILED: {lines}")
        self.cab.menu.failure([STEPS[verb][3]] + lines + ["Details: journalctl -u cabinetd"])
