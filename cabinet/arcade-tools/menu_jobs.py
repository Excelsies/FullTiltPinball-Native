"""The settings menu's confirmed actions (settings_menu.py): the confirm texts, then shut down /
restart (power.py), or a job shown with a busy pane and polled until it ends: lock / unlock
(cabinet_lock.Job, then a restart) and the theme change (theme_switch.Switch). Both jobs have one
contract: poll() is None while running, then (ok, lines).
"""
import traceback

import cabinet_lock
import power
import theme_switch
from theme import ThemeError

POLL, RESTART_AFTER = 0.5, 1.5
GAME_NOTE = "The game will be closed first."
DOING = {"lock": ("Locking", "Locked"), "unlock": ("Unlocking", "Unlocked")}
CONFIRMS = {
    "lock": ("Lock the cabinet (read-only)?", ["Saves, settings, game controls and game lists are kept.",
                                               "Other system changes are undone at each restart.",
                                               "The cabinet restarts."]),
    "unlock": ("Unlock the cabinet for maintenance?", ["System changes are kept until you lock it again.",
                                                      "The cabinet restarts."]),
    "restart": ("Restart the cabinet?", []),
    "shutdown": ("Shut down the cabinet?", []),
    "theme": ("Switch to {}?", ["The game list restarts (about half a minute)."]),
}


def confirm_text(key, game, name):
    """(title, lines) of the confirm pane; name: the theme chosen."""
    title, lines = CONFIRMS[key]
    note = [GAME_NOTE] if game else []
    return title.format(name), lines[:-1] + note + lines[-1:] if lines else note


class Jobs:
    def __init__(self, menu, log):
        self.menu, self.log, self.job = menu, log, None

    def running(self):
        """A job started and its result not handled yet (the menu shows the busy pane)."""
        return self.job is not None

    def release(self):
        """A theme change holding EmulationStation's launcher lets it go (cabinetd ending, errors)."""
        if isinstance(self.job, theme_switch.Switch):
            self.job.release()

    def confirmed(self, key):
        menu, log = self.menu, self.log
        error = menu.vol.save()
        if error:
            log(f"settings: going on without saving the volume ({error})")
        log(f"settings: {key} confirmed")
        if key in ("shutdown", "restart"):
            menu.leave()
            return menu.cab.shutdown() if key == "shutdown" else menu.cab.restart()
        if key == "theme":
            if menu.running_game() != (None, None) or not power.es_pid(menu.processes):
                log("settings: theme: a game is starting or EmulationStation is not running")
                return menu.message(["A game is starting: theme not changed"])
            busy, failed = "Changing theme… (about half a minute)", "Theme not changed"
        else:
            busy, failed = f"{DOING[key][0]}… (about a minute)", f"{DOING[key][0]} could not start - not restarting"
        try:
            self.job = theme_switch.Switch(menu.picker.pick(), menu.processes, log) if key == "theme" \
                else cabinet_lock.Job(key)
        except (OSError, ThemeError) as e:
            log(f"settings: {key} could not start: {e}")
            return menu.message([failed, str(e)])
        menu.later(POLL, self.poll)  # first: polled even if the busy pane cannot be drawn
        menu.message([busy], None, "busy")

    def poll(self):
        try:
            result = self.job.poll()
        except Exception:  # noqa: BLE001 - the result cannot be evaluated: a failure, never a restart
            self.log(f"settings: {self.job.kind}: checking the result failed:\n{traceback.format_exc()}")
            self.release()
            result = False, ["Could not check the result (see the log)"]
        if result is None:
            return self.menu.later(POLL, self.poll)
        job, self.job = self.job, None
        ok, lines = result
        self.log(f"settings: {job.kind} {'done' if ok else 'FAILED'}: {lines}")
        if job.kind == "theme":
            self.menu.picker.load(self.log)
            return self.menu.message(lines if ok else ["Theme not changed"] + lines[:6])
        doing, done = DOING[job.kind]
        if not ok:
            return self.menu.message([f"{doing} failed - not restarting"] + lines[:6])
        self.menu.message([f"{done}. Restarting…"], None, "busy")
        self.menu.later(RESTART_AFTER, self.restart_after_lock)

    def restart_after_lock(self):
        self.menu.leave()
        self.menu.cab.restart()
