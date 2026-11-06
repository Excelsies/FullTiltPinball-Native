"""Change EmulationStation's theme safely (the settings menu's Theme item, via menu_jobs.py).

EmulationStation (ES) may write es_settings.cfg when it exits, so the file is replaced only while no
ES runs: ES's launcher emulationstation.sh is held (SIGSTOP) so it cannot start the next ES, ES is
asked to quit (SIGTERM: it saves its game lists), the file is replaced, the launcher continues and,
finding the /tmp/es-restart flag, starts ES again; then ES must be ready and still say the new theme.
If that fails, the previous file is put back the same way (or, with no ES and no launcher left,
written directly and ES started like ~/arcade-tools/restart-menu does).
Polled like cabinet_lock.Job: poll() is None while it runs, then (ok, lines). The launcher is never
left stopped: release() on every way out, theme.recover() at cabinetd start.
"""
import os
import pwd
import signal
import subprocess
import time

import gamepause
import power
import theme
from theme import FLAG, SETTINGS, ThemeError

ES_LOG = "/opt/retropie/configs/all/emulationstation/es_log.txt"  # ES starts a new file (new inode)
EXIT_WAIT, START_WAIT, READY_WAIT, SETTLE = 30, 20, 90, 3  # seconds; ES is ready about 23 s after start
STOP_CHECKS = 50  # x 10 ms for the launcher to show as stopped
GETTY = ["systemctl", "restart", "getty@tty1"]  # tty1's autologin runs ES (restart-menu)


def alive(pid):
    return pid is not None and gamepause.state(pid) not in ("Z", "gone")


def is_launcher(pid):
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as fh:
            return any(a.endswith(b"/emulationstation.sh") for a in fh.read().split(b"\0"))
    except OSError:
        return False


class Switch:
    kind = "theme"

    def __init__(self, target, processes, log):
        """ThemeError (shown; nothing changed) when it cannot start."""
        self.processes, self.log, self.target = processes, log, target
        self.launcher, self.restoring, self.revived, self.failure = None, False, False, []
        try:
            self.original = theme.read_settings()
            self.stat = os.stat(SETTINGS)
            self.old = theme.current(self.original)
            theme.write_file(theme.BACKUP, self.original, 0o644, 0, 0)
        except OSError as e:
            raise ThemeError(f"could not read / back up the settings: {e}") from e
        if any(c in target for c in '"<>&') or \
                not any(os.path.isdir(os.path.join(d, target)) for d in theme.THEME_DIRS):
            raise ThemeError(f"theme folder '{target}' not found")
        self.begin(target)

    def begin(self, name):
        """Hold the launcher, ask ES to quit. ThemeError: nothing changed, launcher running."""
        procs, es = self.processes(), power.es_pid(self.processes)
        launcher = procs.get(es, (None,))[0]
        if not es or not is_launcher(launcher):
            raise ThemeError("EmulationStation or its launcher is not running")
        try:
            pi = pwd.getpwnam("pi")
            power.make_flag(FLAG, pi.pw_uid, pi.pw_gid)
            self.hold(launcher)
            self.inode = os.stat(ES_LOG).st_ino
            theme.write_held(launcher, True)
            os.kill(es, signal.SIGTERM)
        except (OSError, KeyError, ThemeError) as e:
            self.release()
            self.drop_flag()
            raise ThemeError(f"could not restart EmulationStation: {e}") from e
        self.name, self.es, self.phase, self.since = name, es, "exit", time.monotonic()
        self.log(f"theme: {self.old} -> {name}: launcher {launcher} held, ES {es} asked to quit")

    def hold(self, launcher):
        theme.write_held(launcher, False)  # first: a killed cabinetd's successor continues it
        os.kill(launcher, signal.SIGSTOP)
        self.launcher = launcher
        for _ in range(STOP_CHECKS):
            if gamepause.state(launcher) == "T":
                return
            time.sleep(0.01)
        raise ThemeError(f"launcher {launcher} did not stop")

    def release(self):
        """Continue the launcher if held (every way out, also the menu's error guard)."""
        if self.launcher:
            try:
                os.kill(self.launcher, signal.SIGCONT)
            except OSError as e:
                self.log(f"theme: SIGCONT launcher {self.launcher}: {e}")
            self.log(f"theme: launcher {self.launcher} continued (now {gamepause.state(self.launcher)})")
            self.launcher = None
        try:
            os.remove(theme.HELD)
        except FileNotFoundError:
            pass
        except OSError as e:
            self.log(f"theme: could not remove {theme.HELD}: {e}")

    def drop_flag(self):
        try:
            os.remove(FLAG)
        except FileNotFoundError:
            pass
        except OSError as e:
            self.log(f"theme: could not remove {FLAG}: {e}")

    def edit(self):
        """No ES runs now: write the new file (the original bytes when restoring)."""
        data = self.original if self.restoring else theme.with_theme(self.original, self.name)
        try:
            theme.write_file(SETTINGS, data, self.stat.st_mode & 0o7777, self.stat.st_uid, self.stat.st_gid)
            got = theme.current()
        except (OSError, ThemeError) as e:
            self.log(f"theme: writing {SETTINGS} failed: {e}")
            self.failure.append(f"Could not write the settings: {e}")
            self.name, self.restoring = self.old, True  # the file is unchanged: ES comes back as it was
            return
        self.log(f"theme: {SETTINGS} now says {got}")

    def poll(self):
        waited = time.monotonic() - self.since
        if self.phase == "exit":
            if alive(self.es):
                if waited < EXIT_WAIT:
                    return None
                self.release()  # the flag stays: ES starts again whenever it does quit
                if self.restoring:
                    return self.give_up(f"EmulationStation did not close within {EXIT_WAIT} s")
                return self.end(False, [f"EmulationStation did not close within {EXIT_WAIT} s: theme not changed"])
            self.edit()
            self.release()
            return self.next("start")
        if self.phase == "start":
            es = power.es_pid(self.processes)
            if es and es != self.es:
                self.es, self.log_ino, self.log_pos, self.lists, self.notes = es, None, 0, False, []
                return self.next("ready")
            return None if waited < START_WAIT else self.failed("EmulationStation did not start again")
        if self.phase == "ready":
            if not alive(self.es):
                return self.failed("EmulationStation ended while starting")
            if self.ready():
                self.log(f"theme: ES {self.es} ready after {waited:.0f} s; {len(self.notes)} theme / lvl0 lines in "
                         f"its log (warnings), first: {self.notes[:3]}")
                return self.next("settle")
            return None if waited < READY_WAIT else self.failed(f"not ready within {READY_WAIT} s")
        if self.phase != "settle" or waited < SETTLE:
            return None
        try:
            got = theme.current()
        except (OSError, ThemeError) as e:
            got = f"unreadable ({e})"
        if not alive(self.es) or got != self.name:
            return self.failed(f"EmulationStation ended or the setting changed (it says {got})")
        if self.restoring:
            return self.end(False, self.failure + [f"EmulationStation is back with {self.old}"])
        return self.end(True, [f"Theme changed to {self.name}"])

    def next(self, phase):
        self.phase, self.since = phase, time.monotonic()
        self.log(f"theme: {phase}")
        return None

    def ready(self):
        """ES's new log has a game list line and, after it, the joysticks added again (its menu is up)."""
        try:
            ino = os.stat(ES_LOG).st_ino
            if ino == self.inode:
                return False
            if ino != self.log_ino:
                self.log_ino, self.log_pos, self.lists, self.notes = ino, 0, False, []
            with open(ES_LOG, "rb") as fh:
                fh.seek(self.log_pos)
                chunk = fh.read()
        except OSError as e:
            self.log(f"theme: {ES_LOG}: {e}")
            return False
        chunk = chunk[:chunk.rfind(b"\n") + 1]  # whole lines only
        self.log_pos += len(chunk)
        up = False
        for line in chunk.decode("utf-8", "replace").splitlines():
            self.lists = self.lists or "gamelist.xml" in line
            up = up or (self.lists and "Added known joystick" in line)
            if "theme" in line.lower() or "lvl0" in line:
                self.notes.append(line.strip()[-160:])
        return up

    def failed(self, reason):
        """The new theme did not come up: put the previous file back (ES restarted the same way, or,
        with no ES left, the file written directly and ES started once)."""
        self.log(f"theme: {self.name}: {reason}")
        if not self.restoring:
            self.failure.append(f"Theme {self.name} did not start ({reason})")
            self.restoring = True
            if power.es_pid(self.processes):
                try:
                    self.begin(self.old)
                    return None
                except ThemeError as e:
                    return self.give_up(str(e))
        if self.revived or power.es_pid(self.processes):
            return self.give_up(reason)
        self.revived, self.name = True, self.old
        try:
            theme.write_file(SETTINGS, self.original, self.stat.st_mode & 0o7777, self.stat.st_uid, self.stat.st_gid)
            self.log(f"theme: no ES running: {SETTINGS} restored directly")
            if not any(is_launcher(p) for p in self.processes()):
                self.start_es()
        except (OSError, subprocess.SubprocessError) as e:
            return self.give_up(str(e))
        return self.next("start")

    def give_up(self, reason):
        return self.end(False, self.failure + [f"{self.old} could not be restored: {reason}",
                                               "Details: journalctl -u cabinetd"])

    def start_es(self):
        done = subprocess.run(GETTY, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, timeout=30)
        self.log(f"theme: no ES launcher: {' '.join(GETTY)}: rc {done.returncode} {done.stdout.strip()[-120:]}")
        if done.returncode != 0:
            raise OSError(f"{' '.join(GETTY)} rc {done.returncode}")
        self.es = None

    def end(self, ok, lines):
        self.release()
        self.phase = "done"
        self.log(f"theme: {'done' if ok else 'FAILED'}: {lines}")
        return ok, lines
