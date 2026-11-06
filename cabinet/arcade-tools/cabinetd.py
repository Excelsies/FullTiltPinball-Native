#!/usr/bin/env python3
"""Cabinet button service (root, systemd cabinetd.service).

Reads both control boards alongside EmulationStation and the emulators, and adds:
  EXIT quits the game; held 3 s with no game running: shutdown confirm
  MENU (tap of up to MENU_TAP s, sent on release) opens the emulator's menu; held 2 s: the cabinet
      settings menu (settings_menu.py: volume, mute, quit or theme, guide, system info, lock / unlock,
      restart, shut down); a press in between does nothing
  PLAY pauses / resumes, or with no game starts a random game
  COIN (when the emulator doesn't do it itself) or MODE + START / X / B / top-right / bottom-right
      = quit / menu / reset / load state / save state
  MODE held 2 s: the button guide; both STARTs held 3 s: shutdown confirm (default No)
  the right side (pinball) button is echoed onto player 1's board (vpad.SIDE_ECHO_BUTTON)
  banners (input not taken): "any button now: launch options & game controls" while runcommand's
  launch prompt waits; a reminder of the button guide once a game has run TOAST_AFTER s
Emulators get their own shortcut keys from a virtual keyboard (EMULATORS below). While a pane is up
the boards are grabbed so the game and EmulationStation don't see those presses. padmap.py asks
for a merged, renumbered joystick over SOCKET (Doom).

usage: cabinetd.py
"""
import json
import os
import select
import signal
import socket
import sys
import time

import panes
import power
from overlay_pipe import Overlay, OverlayError
import settings_menu
import vpad

HERE = os.path.dirname(os.path.abspath(__file__))
BUTTON_MAP = os.path.join(HERE, "button-map.json")
PANES_DIR = "/dev/shm/cabinet"
SOCKET = "/run/cabinet/pad.sock"
GUIDE_HOLD, EXIT_HOLD, STARTS_HOLD, MENU_HOLD = 2.0, 3.0, 3.0, 2.0
MENU_TAP = 0.5  # seconds: a longer MENU press is a (maybe aborted) hold, never the emulator's menu
QUIT_GRACE = 5  # seconds an emulator gets to quit on its own key before SIGTERM, then SIGKILL
TOAST_AFTER, TOAST_FOR = 5.0, 4.0  # emulators take a few seconds to show their first frame
LAUNCH_FOR = 3.0  # runcommand's launch prompt listens for 2 s
GAME_POLL, LAUNCH_POLL = 1.0, 0.3  # seconds between looks for a game / for the launch prompt
ROLES = {"top row, LEFT button (Y)": "Y", "top row, MIDDLE button (X)": "X", "top row, RIGHT button": "L",
         "bottom row, LEFT button (B)": "B", "bottom row, MIDDLE button (A)": "A",
         "bottom row, RIGHT button (R)": "R", "START": "START", "COIN": "COIN"}
COMBOS = {"START": "quit", "X": "menu", "B": "reset", "L": "load", "R": "save"}

# Linux key codes
ESC, TAB, Q, P, H, M, F1, F2, F3, F4, F5, F7, F9, KP_SLASH = 1, 15, 16, 25, 35, 50, 59, 60, 61, 62, 63, 65, 67, 98
Z, SLASH = 44, 53
RA = KP_SLASH  # RetroArch's keyboard hotkey-enable (remap_standalone.py sets input_enable_hotkey)
# What each emulator understands, by process name (15 characters at most). No "quit" key: SIGTERM.
# coin_native: the emulator already does COIN + button itself. "start": a plain START press.
# "hold": cabinet buttons that act as a key for as long as they are held (pinball flippers).
EMULATORS = {
    "retroarch": {"coin_native": True, "quit": [RA, ESC], "menu": [RA, F1], "pause": [RA, P],
                  "reset": [RA, H], "load": [RA, F4], "save": [RA, F2]},
    "mupen64plus": {"coin_native": True, "quit": [ESC], "pause": [P], "reset": [F9], "load": [F7], "save": [F5]},
    "advmame": {"coin_native": True, "quit": [ESC], "menu": [TAB], "pause": [P]},
    "drastic": {"quit": [Q], "menu": [M], "load": [F7], "save": [F5]},
    "lzdoom": {"menu": [ESC]},
    "eduke32": {"menu": [ESC]},
    # Space Cadet Pinball: no quit key (Esc asks "Exit the game?"); SDL turns SIGTERM into a clean quit
    "SpaceCadetPinba": {"pause": [F3], "menu": [F9], "start": [F2],
                        "hold": {("LSIDE", 0): Z, ("RSIDE", 0): SLASH}},
}
ALL_KEYS = sorted({k for e in EMULATORS.values() for v in e.values() if isinstance(v, list) for k in v} |
                  {k for e in EMULATORS.values() for k in e.get("hold", {}).values()})
# Launch plumbing, never "the game": runcommand, its menus and joystick-to-keyboard helper.
WRAPPERS = {"runcommand.sh", "bash", "sh", "sudo", "dialog", "timeout", "python3", "sleep", "joy2key",
            "tput", "clear", "fbset", "tvservice"}


def log(*a):
    print(*a, flush=True)


def processes():
    """{pid: (ppid, comm)} for every live process."""
    out = {}
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open(f"/proc/{d}/stat") as fh:
                    head, rest = fh.read().rsplit(")", 1)
                out[int(d)] = (int(rest.split()[1]), head.split("(", 1)[1])
            except (OSError, IndexError, ValueError):
                continue
    return out


def is_runcommand(pid):
    """runcommand.sh runs under bash, so its process name is "bash": check the script argument."""
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as fh:
            return any(a.endswith(b"/runcommand.sh") for a in fh.read().split(b"\0"))
    except OSError:
        return False


def running_game():
    """(pid, name) of the emulator runcommand started, (None, None) in the menus."""
    procs = processes()
    roots = [p for p, (_, comm) in procs.items() if comm == "bash" and is_runcommand(p)]
    if not roots:
        return None, None
    children = {}
    for p, (pp, _) in procs.items():
        children.setdefault(pp, []).append(p)
    found, stack = [], list(roots)
    while stack:
        for c in children.get(stack.pop(), []):
            found.append(c)
            stack.append(c)
    emus = [p for p in found if procs[p][1] not in WRAPPERS]
    known = [p for p in emus if procs[p][1] in EMULATORS]
    pid = (known or emus or [None])[0]
    return pid, procs[pid][1] if pid else "starting"


def launch_prompt_up():
    """runcommand starts its joystick-to-keyboard helper just before the launch prompt."""
    for p, (_, comm) in processes().items():
        if comm == "python3":
            try:
                with open(f"/proc/{p}/cmdline", "rb") as fh:
                    if b"joy2key" in fh.read():
                        return True
            except OSError:
                continue
    return False


def board_layout():
    """Event device per player and {(player board, button): (role, player)} from the button check."""
    rows = json.load(open(BUTTON_MAP))
    events, layout = {}, {}
    for r in rows:
        assert r["button"] is not None, f"button check skipped {r['label']}"
        event = os.path.realpath(f"/dev/input/by-path/{r['path'][:-len('-joystick')]}-event-joystick")
        label = r["label"]
        if label.startswith("PLAYER "):
            player, role = int(label[7]), ROLES[label.split(": ", 1)[1]]
            events[player] = event
        elif label.startswith("FRONT panel: "):
            player, role = 0, label.split(": ", 1)[1]
        else:
            player, role = 0, "LSIDE" if label.startswith("LEFT") else "RSIDE"
        layout[(event, r["button"])] = (role, player)
    assert set(events) == {1, 2}, f"need both players' boards: {events}"
    return events, layout


class Cabinet:
    def __init__(self):
        events, self.layout = board_layout()
        self.p1_event = events[1]
        self.boards = {os.open(e, os.O_RDWR | os.O_NONBLOCK): e for e in sorted(set(events.values()))}
        self.fd_of = {e: fd for fd, e in self.boards.items()}
        self.keys = vpad.Keyboard(ALL_KEYS)
        self.overlay = Overlay(PANES_DIR, log)
        self.held = {}  # (role, player) -> press time
        self.used = set()  # held buttons already consumed by a combo or a pane
        self.flippers = {}  # side button held -> the "hold" key its press sent (None: none)
        self.pane = None  # None, "guide", "confirm", "settings", "shutting-down", "restarting"
        self.choice, self.game_at_confirm = "No", False
        self.stick = {}
        self.timers = []  # (when, function)
        self.merge, self.client, self.grabbed = None, None, False
        self.game_since, self.toasted, self.toast_on, self.next_poll = None, False, False, 0
        self.launch_hinted = False
        os.makedirs(os.path.dirname(SOCKET), exist_ok=True)
        if os.path.exists(SOCKET):
            os.remove(SOCKET)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(SOCKET)
        os.chmod(SOCKET, 0o666)
        self.server.listen(1)
        self.menu = settings_menu.SettingsMenu(self, running_game, processes)
        self.power = power.Power(self, running_game, processes, log)

    # ---- outputs -------------------------------------------------------------------------
    def set_grab(self):
        want = bool(self.merge or self.pane)
        if want != self.grabbed:
            for fd in self.boards:
                vpad.grab(fd, want)
            self.grabbed = want

    def show_pane(self, name):
        self.toast_on = False  # the pane replaces it
        self.overlay.show(name)
        self.pane = "confirm" if name.startswith("shutdown-") else name
        self.used.update(self.held)  # buttons held now don't count as the pane's first press
        if self.merge:  # the stick's moves under the pane are not passed on: the game sees it centred
            self.merge.sync_axes(True)
        self.set_grab()

    def close_pane(self):
        """Boards first: even if the overlay fails, the game and EmulationStation get their input back."""
        self.pane = None
        if self.merge:
            self.merge.sync_axes(False)
        self.set_grab()
        try:
            self.overlay.hide()
        except OverlayError as e:  # it died (replaced, nothing shown) or could not hide
            log(f"closing the pane: {e}")

    def act(self, action):
        pid, name = running_game()
        if not pid:
            return
        keys = EMULATORS.get(name, {}).get(action)
        log(f"{action} -> {name} ({pid}): {'keys ' + str(keys) if keys else 'signal' if action == 'quit' else 'none'}")
        if keys:
            self.keys.tap(keys)
        if action == "quit":
            first = time.monotonic() + (QUIT_GRACE if keys else 0)
            self.timers.append((first, lambda: self.escalate(pid, signal.SIGTERM)))
            self.timers.append((first + QUIT_GRACE, lambda: self.escalate(pid, signal.SIGKILL)))

    def watch_game(self, now):
        """Banners: while runcommand's launch prompt waits for a button, the launch-options hint;
        once per game, TOAST_AFTER seconds in, the button-guide reminder."""
        if now < self.next_poll:
            return
        pid, name = running_game()
        launching = name == "starting"
        self.next_poll = now + (LAUNCH_POLL if launching else GAME_POLL)
        if not pid and not launching:
            self.game_since, self.toasted, self.launch_hinted = None, False, False
            return
        if launching:
            if not self.launch_hinted and not self.pane and launch_prompt_up():
                self.launch_hinted = True
                self.show_toast("launch", LAUNCH_FOR)
            return
        self.game_since = self.game_since or now
        if not self.toasted and not self.pane and now - self.game_since >= TOAST_AFTER:
            self.toasted = True
            self.show_toast("toast", TOAST_FOR)

    def show_toast(self, name, seconds):
        self.toast_on = True
        self.overlay.toast(name)
        self.timers.append((time.monotonic() + seconds, self.end_toast))

    def end_toast(self):
        if self.toast_on and not self.pane:
            self.overlay.hide()
        self.toast_on = False

    def escalate(self, pid, sig):
        if os.path.exists(f"/proc/{pid}"):
            log(f"stopping emulator {pid} with {signal.Signals(sig).name}")
            os.kill(pid, sig)

    def random_game(self):
        """EmulationStation's own random jump (X), then A: launches it in a game list."""
        p1 = self.fd_of[self.p1_event]
        x = next(b for (e, b), (r, p) in self.layout.items() if e == self.p1_event and r == "X")
        a = next(b for (e, b), (r, p) in self.layout.items() if e == self.p1_event and r == "A")
        for when, button, value in ((0, x, 1), (0.1, x, 0), (0.8, a, 1), (0.9, a, 0)):
            self.timers.append((time.monotonic() + when,
                                lambda b=button, v=value: vpad.write_event(p1, vpad.EV_KEY, vpad.BTN_FIRST + b, v)))

    def shutdown(self):
        """Safe shutdown (power.py): quit the game, EmulationStation saves its lists, power off."""
        self.power.start("poweroff")

    def restart(self):
        """The same sequence ending in a reboot: the settings menu's Restart, Lock and Unlock."""
        self.power.start("reboot")

    # ---- inputs --------------------------------------------------------------------------
    def press(self, button):
        role, player = button
        if self.pane:
            self.used.add(button)  # consumed: no MENU tap on release, no hold afterwards
        if self.pane == "settings":
            self.menu.guarded(self.menu.press, role)  # a menu error is shown, never ends cabinetd
            return
        if self.pane == "guide":
            self.close_pane()
            return
        if self.pane == "confirm":
            if role == "A" and self.choice == "Yes":
                self.shutdown()
            elif role in ("A", "B", "EXIT"):
                self.close_pane()
            return
        if self.pane:
            return
        pid, name = running_game()
        if not pid:
            if role == "PLAY":
                self.random_game()
            return
        if role in ("EXIT", "PLAY"):  # MENU: on release, if it was not held for the settings menu
            self.act({"EXIT": "quit", "PLAY": "pause"}[role])
        elif player == 1 and role in COMBOS:
            mode = ("MODE", 0) in self.held
            coin = ("COIN", 1) in self.held and not EMULATORS.get(name, {}).get("coin_native")
            if mode or coin:
                self.used.update({("MODE", 0), ("COIN", 1)} & set(self.held))
                self.act(COMBOS[role])
            elif role == "START" and "start" in EMULATORS.get(name, {}):
                self.act("start")

    def axis(self, event, code, value):
        if self.pane == "settings":
            self.menu.guarded(self.menu.axis, event, code, value)
            return
        if self.pane != "confirm" or code != 0:
            return
        side = "left" if value < 64 else "right" if value > 192 else None
        if side and self.stick.get(event) != side:
            self.choice = "No" if side == "left" else "Yes"
            self.overlay.show(f"shutdown-{self.choice.lower()}{'-game' if self.game_at_confirm else ''}")
        self.stick[event] = side

    def check_holds(self, now):
        if self.pane:
            return
        held = {b: now - t for b, t in self.held.items() if b not in self.used}
        starts = [self.held.get(("START", p)) for p in (1, 2)]
        confirm = (held.get(("EXIT", 0), 0) >= EXIT_HOLD and not running_game()[0]) or \
            (None not in starts and now - max(starts) >= STARTS_HOLD)
        if confirm:
            self.choice, self.game_at_confirm = "No", bool(running_game()[0])
            self.show_pane(f"shutdown-no{'-game' if self.game_at_confirm else ''}")
        elif held.get(("MODE", 0), 0) >= GUIDE_HOLD:
            self.show_pane("guide")
        elif held.get(("MENU", 0), 0) >= MENU_HOLD:
            self.used.add(("MENU", 0))  # this hold is spent, even if the menu fails to open
            self.menu.guarded(self.menu.open)

    def board_event(self, fd, etype, code, value):
        event = self.boards[fd]
        # Releases always reach the merged joystick: a button held when a pane opens (MENU, MODE) is let go
        # under the pane, and must not stay down in the game.
        if self.merge and (not self.pane or (etype == vpad.EV_KEY and value == 0)):
            self.merge.feed(fd, etype, code, value)
        if etype == vpad.EV_ABS:
            self.axis(event, code, value)
            return
        if etype != vpad.EV_KEY or not vpad.BTN_FIRST <= code < vpad.BTN_FIRST + vpad.BUTTONS:
            return
        button = self.layout.get((event, code - vpad.BTN_FIRST))
        if not button:
            return
        if button == ("RSIDE", 0) and value in (0, 1):
            vpad.write_event(self.fd_of[self.p1_event], vpad.EV_KEY, vpad.BTN_FIRST + vpad.SIDE_ECHO_BUTTON, value)
        if button[0] in ("LSIDE", "RSIDE") and value in (0, 1):
            # Flipper keys: a press under a pane is not sent, a release always is (the key its press
            # sent), so a flipper held when a pane opens and let go under it does not stay down.
            if value == 0:
                key = self.flippers.pop(button, None)
            else:
                key = None if self.pane else EMULATORS.get(running_game()[1], {}).get("hold", {}).get(button)
                self.flippers[button] = key
            if key:
                vpad.write_event(self.keys.fd, vpad.EV_KEY, key, value)
        if value == 1:
            self.held[button] = time.monotonic()
            self.press(button)
        elif value == 0:
            if button == ("MENU", 0) and button not in self.used and not self.pane:
                held_for = time.monotonic() - self.held.get(button, time.monotonic())
                if held_for <= MENU_TAP:
                    self.act("menu")  # a tap: the emulator's menu (nothing in EmulationStation)
                else:
                    log(f"MENU held {held_for:.1f} s: no emulator menu (a tap is up to {MENU_TAP} s)")
            self.held.pop(button, None)
            self.used.discard(button)

    # ---- padmap.py clients -----------------------------------------------------------------
    def accept(self):
        conn, _ = self.server.accept()
        conn.settimeout(2)
        request = conn.recv(256).decode().strip()
        if self.merge or not request.startswith("MAP "):
            conn.sendall(b"busy\n" if self.merge else b"bad request\n")
            conn.close()
            return
        self.merge = vpad.MergedPad(list(self.boards), vpad.parse_mapping(request[4:]))
        self.client = conn
        self.set_grab()
        conn.sendall(b"ready\n")
        log(f"merged joystick on: {request}")

    def end_merge(self):
        self.client.close()
        self.client = None
        self.merge.close()
        self.merge = None
        self.set_grab()
        log("merged joystick off")

    # ---- main loop -------------------------------------------------------------------------
    def run(self):
        log("cabinet service ready")
        while True:  # a service: runs until systemd stops it
            now = time.monotonic()
            due = [t for t in self.timers if t[0] <= now]
            self.timers = [t for t in self.timers if t[0] > now]
            for _, fn in sorted(due, key=lambda t: t[0]):
                fn()
            self.check_holds(now)
            self.watch_game(now)
            waits = [t[0] - now for t in self.timers] + ([0.1] if self.held else []) + [self.next_poll - now]
            fds = list(self.boards) + [self.server] + ([self.client] if self.client else [])
            ready = select.select(fds, [], [], max(0, min(waits)))[0]
            for fd in ready:
                if fd is self.server:
                    self.accept()
                elif fd is self.client:
                    if not self.client.recv(64):
                        self.end_merge()
                else:
                    data = os.read(fd, vpad.EVENT.size * 64)
                    assert data, f"control board {self.boards[fd]} disappeared"
                    for i in range(0, len(data) - vpad.EVENT.size + 1, vpad.EVENT.size):
                        self.board_event(fd, *vpad.EVENT.unpack_from(data, i)[2:])


def main():
    assert os.geteuid() == 0, "cabinetd needs root (uinput, grabbing the boards, shutdown)"
    for name, img in panes.panes().items():
        os.makedirs(PANES_DIR, exist_ok=True)
        panes.write_rgba(img, os.path.join(PANES_DIR, f"{name}.rgba"))
    settings_menu.recover()  # a game (or ES's launcher) a killed cabinetd left stopped
    cab = Cabinet()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # systemctl stop / restart: run the finally
    try:
        cab.run()
    finally:
        cab.menu.resume_game()
        cab.menu.jobs.release()


if __name__ == "__main__":
    sys.exit(main())
