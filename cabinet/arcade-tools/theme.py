"""EmulationStation's theme for the cabinet settings menu's Theme item (only offered outside games).

Picker: the installed themes (read at menu open) and the one in use (ThemeSet in es_settings.cfg).
The change itself is theme_switch.Switch; recover() continues an ES launcher a killed cabinetd left
stopped during a change (its pid is kept in HELD).
"""
import json
import os
import re
import signal

import gamepause

THEME_DIRS = ("/etc/emulationstation/themes", "/home/pi/.emulationstation/themes")  # where ES looks
SETTINGS = "/opt/retropie/configs/all/emulationstation/es_settings.cfg"
BACKUP = "/mnt/retrodata/cabinet/es_settings.cfg.before-theme"
HELD = "/run/cabinet/es-held.json"  # while theme_switch holds ES's launcher stopped (see write_held)
FLAG = "/tmp/es-restart"  # emulationstation.sh: ES exited with this flag -> start ES again
LINE = re.compile(r'(<string name="ThemeSet" value=")([^"]*)("\s*/>)')
SHORT = 22  # characters of a theme name shown in the menu row


class ThemeError(Exception):
    """A reason to show in the menu."""


def installed():
    """Sorted theme folder names of both ES theme folders; ThemeError if none can be read."""
    names, errors = set(), []
    for top in THEME_DIRS:
        try:
            names.update(n for n in os.listdir(top) if not n.startswith(".") and os.path.isdir(os.path.join(top, n)))
        except FileNotFoundError:
            continue
        except OSError as e:
            errors.append(f"{top}: {e.strerror or e}")
    if not names:
        raise ThemeError("; ".join(errors) or "no themes installed")
    return sorted(names)


def read_settings():
    with open(SETTINGS, "rb") as fh:
        return fh.read()


def current(data=None):
    """ThemeSet's value; ThemeError if the line is missing, OSError if the file cannot be read."""
    found = LINE.search((read_settings() if data is None else data).decode("utf-8", "replace"))
    if not found:
        raise ThemeError(f"no ThemeSet line in {SETTINGS}")
    return found.group(2)


def with_theme(data, name):
    text = data.decode("utf-8", "replace")
    new, n = LINE.subn(lambda m: m.group(1) + name + m.group(3), text, count=1)
    return (new if n else text.rstrip("\n") + f'\n<string name="ThemeSet" value="{name}" />\n').encode()


def write_file(path, data, mode, uid, gid):
    """Atomic replace: tmp file in the same folder (mode, owner), fsync, rename. OSError: path unchanged."""
    tmp = path + ".cabinet-tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    try:
        if os.write(fd, data) != len(data):
            raise OSError(f"short write to {tmp}")
        os.fchmod(fd, mode)
        os.fchown(fd, uid, gid)
        os.fsync(fd)
    except OSError:
        os.close(fd)
        os.remove(tmp)
        raise
    os.close(fd)
    os.replace(tmp, path)


def write_held(launcher, asked):
    """Written before the launcher is stopped (asked False) and before ES is asked to quit (True)."""
    with open(HELD + ".tmp", "w") as fh:
        json.dump({"launcher": launcher, "asked": asked}, fh)
    os.replace(HELD + ".tmp", HELD)


def recover(log):
    """cabinetd start: continue a launcher a killed cabinetd left stopped. If ES had been asked to
    quit, the flag stays (ES starts again); if not, ES runs on and the flag is removed (it would turn
    ES's next quit, reboot or shut down into a restart of ES)."""
    try:
        with open(HELD) as fh:
            held = json.load(fh)
        pid, asked = int(held["launcher"]), held["asked"] is not False
    except FileNotFoundError:
        return
    except (OSError, ValueError, KeyError, TypeError) as e:
        log(f"theme recover: {HELD} unreadable ({e})")
        pid, asked = None, True
    if pid:
        try:
            os.kill(pid, signal.SIGCONT)
            log(f"theme recover: SIGCONT ES launcher {pid} (now {gamepause.state(pid)}); ES asked to quit: {asked}")
        except OSError as e:
            log(f"theme recover: SIGCONT {pid}: {e}")
    for path in ([] if asked else [FLAG]) + [HELD]:
        try:
            os.remove(path)
        except FileNotFoundError:
            pass
        except OSError as e:
            log(f"theme recover: could not remove {path}: {e}")


class Picker:
    """The Theme row: candidates cycled with the stick, starting at the theme in use."""

    def __init__(self):
        self.names, self.used, self.index, self.problem = [], None, 0, "not read yet"

    def load(self, log):
        try:
            self.names, self.used, self.problem = installed(), current(), None
        except (OSError, ThemeError) as e:
            self.names, self.problem = [], str(e)
            return log(f"settings: themes unavailable: {e}")
        self.index = self.names.index(self.used) if self.used in self.names else 0

    def step(self, way):
        if self.names:
            self.index = (self.index + way) % len(self.names)

    def pick(self):
        return self.names[self.index] if self.names else None

    def label(self):
        name = self.pick()
        if not name:
            return "unavailable"
        short = name if len(name) <= SHORT else name[:SHORT] + "…"
        return f"{short} (in use)" if name == self.used else short
