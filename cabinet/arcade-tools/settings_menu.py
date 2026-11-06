"""The cabinet settings menu inside cabinetd.py: hold MENU 2 s (in the menus and in games).

Either player's stick: up / down choose, left / right change; A select / confirm, B back / close,
MENU or EXIT close. Views: list, confirm, info, guide, message, busy (locking / unlocking / theme).
Talks to cabinetd only through the Cabinet it is given (show_pane, close_pane, overlay, timers,
act, shutdown, restart) and the two functions passed in, so it never imports cabinetd (__main__).
power.py calls failure() back when a shut down / restart step fails. Confirmed actions and their
busy jobs: menu_jobs.py. cabinetd calls open / press / axis through guarded() (failure and every
timer use it too): an error is logged and shown, the game resumed, and cabinetd keeps running.
"""
import os
import statistics
import time
import traceback

import audio
import cabinet_lock
import gamepause
import menu_jobs
import power
import settings_pane
import sysinfo
import theme

HELP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_help.json")  # item descriptions
REPEAT_FIRST, REPEAT_NEXT = 0.45, 0.15  # stick held: first repeat, then every
RECHECK_AFTER = 10.0  # alsa-state restores the stored mixer at boot, in parallel with cabinetd
LABELS = {"volume": "Volume", "mute": "Mute", "quit": "Quit game", "theme": "Theme", "guide": "Button guide",
          "info": "System info", "lock": "Lock cabinet", "unlock": "Unlock cabinet", "restart": "Restart",
          "shutdown": "Shut down", "close": "Close"}


def log(*a):
    print(*a, flush=True)


def recover():
    """cabinetd start: resume a game, and continue EmulationStation's launcher, a killed cabinetd left stopped."""
    gamepause.recover(log)
    theme.recover(log)


class SettingsMenu:
    def __init__(self, cab, running_game, processes):
        self.cab, self.running_game, self.processes = cab, running_game, processes
        self.view, self.keys, self.cursor, self.choice, self.asking = None, [], 0, "No", None
        self.list_pane, self.jobs, self.picker = settings_pane.ListPane(), menu_jobs.Jobs(self, log), theme.Picker()
        self.status, self.paused, self.warned = "", [], False
        self.dirs, self.gen, self.redraws = {}, 0, []
        self.vol = audio.Volume(log)  # applies the saved volume
        self.help, problems = settings_pane.load_help(HELP, LABELS)
        for text in problems:
            log(f"settings: {text}")
        self.later(RECHECK_AFTER, self.recheck)

    # ---- errors: cabinetd never ends because of the menu ----------------------------------
    def later(self, delay, fn):
        self.cab.timers.append((time.monotonic() + delay, lambda: self.guarded(fn)))

    def guarded(self, fn, *args):
        try:
            return fn(*args)
        except Exception:  # noqa: BLE001 - logged and shown; SystemExit (SIGTERM) still ends cabinetd
            self.fault(traceback.format_exc())

    def fault(self, trace):
        """A job keeps its busy pane (and its polling); otherwise a message, or the menu closed."""
        log(f"settings: error (cabinetd keeps running):\n{trace}")
        if self.jobs.running():
            self.view = "busy"  # input ignored until the job's result is shown
            return
        if not self.view:  # not open (e.g. failed while opening): only make sure no game stays stopped
            return self.resume_game()
        try:
            self.message(["Something went wrong", trace.strip().splitlines()[-1][:120],
                          "Details: journalctl -u cabinetd"])
        except Exception:  # noqa: BLE001
            log(f"settings: the error message failed too, closing the menu:\n{traceback.format_exc()}")
            self.leave()
            self.cab.close_pane()  # boards released first; a failing overlay is replaced

    # ---- volume ---------------------------------------------------------------------------
    def recheck(self):
        self.vol.recheck()
        if self.view == "list":
            self.draw()

    def change_volume(self, step):
        if self.vol.volume is None:
            return self.message(["The volume cannot be changed", "The mixer could not be read (see the log)."])
        error = self.vol.change(step, self.draw)  # draws the new value while amixer runs
        if error:
            return self.message(["Volume could not be set", error])

    def toggle_mute(self):
        error = self.vol.toggle_mute()
        if error:
            return self.message(["Mute could not be changed", error])
        self.draw()

    # ---- game pause -----------------------------------------------------------------------
    def pause_game(self, pid, name):
        self.status = ""
        if pid and name in gamepause.PAUSE_OK:
            self.paused = gamepause.pause(pid, self.processes(), log)  # [] if it could not (logged)
            self.status = "Game paused" if self.paused else "Could not pause the game (see the log)"
            log(f"settings: {'paused' if self.paused else 'could not pause'} {name} {self.paused}")
            self.later(0.2, self.check_paused)
        elif pid:
            self.status = "The game keeps running while this menu is open"

    def check_paused(self):
        states = {p: gamepause.state(p) for p in self.paused}
        if self.paused and not all(s in ("T", "t", "gone") for s in states.values()):
            log(f"settings: could not pause the game: process states {states}")
            self.status = "Could not pause the game"
            if self.view == "list":
                self.draw()

    def resume_game(self):
        if self.paused:
            gone = gamepause.resume(self.paused)
            log(f"settings: resumed {self.paused} (ended while paused: {gone})")
            self.paused = []

    # ---- views ----------------------------------------------------------------------------
    def open(self):
        if self.view:
            return
        pid, name = self.running_game()
        self.pause_game(pid, name)
        self.vol.sync()
        self.items(pid, name)
        log(f"settings menu opened ({self.status or 'no game'})")
        self.set_view("list")

    def items(self, game, name):
        """Theme only in the menus: no game, runcommand not launching one ("starting"), ES running."""
        menus = name is None and power.es_pid(self.processes) is not None
        if menus:
            self.picker.load(log)
        lock = "unlock" if cabinet_lock.is_locked() else "lock"
        self.keys = ["volume", "mute"] + (["quit"] if game else []) + (["theme"] if menus else []) + \
            ["guide", "info", lock, "restart", "shutdown", "close"]
        self.cursor, self.redraws = 0, []

    def failure(self, lines):
        """power.py: a shut down / restart step failed (logged there). A or B: the list; MENU / EXIT close."""
        self.guarded(self._failure, lines)

    def _failure(self, lines):
        self.status = ""
        self.items(*self.running_game())
        self.message(lines)

    def set_view(self, view):
        self.view, self.dirs = view, {}
        self.gen += 1
        if view == "list":
            self.draw()

    def show(self):
        if self.cab.pane == "settings":
            self.cab.overlay.show("settings")
        else:
            self.cab.show_pane("settings")

    def draw(self):
        start = time.monotonic()
        vol, note = self.vol, self.lock_note()
        values = {"volume": ("volume", vol.volume, vol.muted) if vol.volume is not None else "unavailable",
                  "mute": "On" if vol.muted else "Off", "lock": note, "unlock": note, "theme": self.picker.label()}
        status = "  ·  ".join(vol.problems[-1:] + ([self.status] if self.status else []))
        items = [(LABELS[k], values.get(k)) for k in self.keys]
        key = self.keys[self.cursor]
        help_lines = self.help.get(key, [f"(no description for '{key}': see the log)"])
        self.list_pane.draw(items, self.cursor, status[:70], help_lines)
        self.show()
        self.redraws.append((time.monotonic() - start) * 1000)

    def lock_note(self):
        try:
            return "(restart pending)" if cabinet_lock.pending() else None
        except (OSError, ValueError) as e:
            log(f"settings: lock state unreadable: {e}")
            return "(state unknown: see the log)"

    def message(self, lines, footer="A or B: back", view="message"):
        settings_pane.message(lines, footer)
        self.set_view(view)
        self.show()

    def close(self, warn=True):
        error = self.vol.save()
        if error and warn and not self.warned:  # say so once; the next close goes through
            self.warned = True
            return self.message(["The volume could not be saved", error, "It stays until the next restart."])
        self.warned = False
        self.leave()
        self.cab.close_pane()
        first, moves = (self.redraws or [0])[0], self.redraws[1:] or [0]
        log(f"settings menu closed (open {first:.0f} ms; {max(len(self.redraws) - 1, 0)} redraws: median "
            f"{statistics.median(moves):.0f} ms, max {max(moves):.0f} ms)")

    def leave(self):
        """Every way out: resume the game first (a stopped process would not see a quit)."""
        self.view = None
        self.gen += 1
        self.resume_game()

    # ---- input ----------------------------------------------------------------------------
    def press(self, role):
        if self.view == "busy":
            return
        if role in ("MENU", "EXIT"):
            return self.close()
        if self.view in ("guide", "info"):
            return self.set_view("list")
        if self.view == "message":
            return self.set_view("list") if role in ("A", "B") else None
        if self.view == "confirm":
            if role == "B" or (role == "A" and self.choice == "No"):
                self.set_view("list")
            elif role == "A":
                self.jobs.confirmed(self.asking)
            return
        if role == "B":
            self.close()
        elif role == "A":
            self.select(self.keys[self.cursor])

    def axis(self, event, code, value):
        if code not in (0, 1) or not self.view:
            return
        way = -1 if value < 64 else 1 if value > 192 else None
        if self.dirs.get((event, code)) == way:
            return
        self.dirs[(event, code)] = way
        self.gen += 1
        if way:
            self.move(code, way)
            self.repeat_later(self.gen, code, way, REPEAT_FIRST)

    def repeat_later(self, gen, code, way, delay):
        def again():
            if gen == self.gen and self.view == "list":
                self.move(code, way)
                self.repeat_later(gen, code, way, REPEAT_NEXT)
        self.later(delay, again)

    def move(self, code, way):
        if self.view == "list" and code == 1:
            self.cursor = (self.cursor + way) % len(self.keys)
            self.draw()
        elif self.view == "list":
            key = self.keys[self.cursor]
            if key == "volume":
                self.change_volume(way)
            elif key == "mute":
                self.toggle_mute()
            elif key == "theme":
                self.picker.step(way)
                self.draw()
        elif self.view == "confirm" and code == 0 and self.choice != ("No" if way < 0 else "Yes"):
            self.choice = "No" if way < 0 else "Yes"
            self.draw_confirm()

    def select(self, key):
        if key == "mute":
            self.toggle_mute()
        elif key == "quit":
            pid, name = self.running_game()
            if not pid:
                self.keys.remove("quit")
                self.cursor = 0
                return self.message(["No game is running"])
            log(f"settings: quit game {name} ({pid})")
            self.close(warn=False)  # logged; the game must be resumed before it is asked to quit
            self.cab.act("quit")
        elif key == "theme":
            self.select_theme()
        elif key == "guide":
            self.set_view("guide")
            self.cab.overlay.show("guide")
        elif key == "info":
            settings_pane.info(sysinfo.rows(log))
            self.set_view("info")
            self.show()
        elif key in menu_jobs.CONFIRMS:
            self.asking, self.choice = key, "No"
            self.set_view("confirm")
            self.draw_confirm()
        elif key == "close":
            self.close()

    def select_theme(self):
        name = self.picker.pick()
        if not name:
            return self.message(["Themes unavailable", self.picker.problem or "", "Details: journalctl -u cabinetd"])
        if name == self.picker.used:
            return self.message(["This theme is already in use", "Stick left / right to choose another."])
        self.asking, self.choice = "theme", "No"
        self.set_view("confirm")
        self.draw_confirm()

    def draw_confirm(self):
        title, lines = menu_jobs.confirm_text(self.asking, self.running_game()[0], self.picker.pick())
        settings_pane.confirm(title, lines, self.choice)
        self.show()
