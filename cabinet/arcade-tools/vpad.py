"""Linux input plumbing for cabinetd.py: virtual keyboard, merged joystick, grabbing the boards.

Button numbers are the boards' own (0-11, as in button-map.json); evdev codes are 288 + number.
"""
import fcntl
import os
import struct
import subprocess
import time

EVENT = struct.Struct("llHHi")  # struct input_event on 32-bit ARM
ABSINFO = struct.Struct("6i")
USER_DEV = struct.Struct("80sHHHHI" + "64i" * 4)  # struct uinput_user_dev
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN_FIRST, BUTTONS = 288, 12  # BTN_TRIGGER.., the boards' 12 buttons
AXES = (0, 1)  # ABS_X, ABS_Y
UI_DEV_CREATE, UI_DEV_DESTROY = 0x5501, 0x5502
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045567
EVIOCGRAB = 0x40044590
KEY_HOLD = 0.1  # seconds a tapped key stays down: emulators poll once per frame
# The right side (pinball) button is wired to player 2's board; cabinetd.py echoes it onto player
# 1's board as this button, which neither board has, so one-player games see both side buttons.
SIDE_ECHO_BUTTON = 5


def abs_info(fd, axis):
    """(value, minimum, maximum, fuzz, flat, resolution) of one axis."""
    return ABSINFO.unpack(fcntl.ioctl(fd, 0x80184540 + axis, bytes(ABSINFO.size)))


def parse_mapping(text):
    swap = {int(a): int(b) for a, b in (pair.split(":") for pair in text.split(","))}
    assert sorted(swap) == sorted(swap.values()), f"mapping must swap buttons, not drop them: {text}"
    assert all(0 <= n < BUTTONS for n in swap), f"buttons are 0-{BUTTONS - 1}: {text}"
    return swap


def write_event(fd, etype, code, value):
    os.write(fd, EVENT.pack(0, 0, etype, code, value))
    os.write(fd, EVENT.pack(0, 0, EV_SYN, 0, 0))


def grab(fd, on):
    fcntl.ioctl(fd, EVIOCGRAB, int(on))


def _create(name, keys, info=None):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    for ev in (EV_SYN, EV_KEY) + ((EV_ABS,) if info else ()):
        fcntl.ioctl(fd, UI_SET_EVBIT, ev)
    for key in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, key)
    absmax, absmin, absfuzz, absflat = ([0] * 64 for _ in range(4))
    for a, (_, lo, hi, fuzz, flat, _) in (info or {}).items():
        fcntl.ioctl(fd, UI_SET_ABSBIT, a)
        absmin[a], absmax[a], absfuzz[a], absflat[a] = lo, hi, fuzz, flat
    vendor, product = (0x0079, 0x0006) if info else (0x1209, 0x0001)
    os.write(fd, USER_DEV.pack(name.encode(), 3, vendor, product, 1, 0, *absmax, *absmin, *absfuzz, *absflat))
    fcntl.ioctl(fd, UI_DEV_CREATE)
    # udev gives the new device nodes the "input" group a moment later; games open them as pi.
    subprocess.run(["udevadm", "settle", "--timeout=5"], check=True)
    return fd


def destroy(fd):
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    os.close(fd)


class Keyboard:
    """A virtual keyboard for sending emulators their own shortcut keys."""

    def __init__(self, keys):
        self.fd = _create("Arcade Cabinet keys", sorted(set(keys)))

    def tap(self, keys):
        """Press keys in order (a modifier first), hold briefly, release in reverse."""
        for k in keys:
            write_event(self.fd, EV_KEY, k, 1)
        time.sleep(KEY_HOLD)
        for k in reversed(keys):
            write_event(self.fd, EV_KEY, k, 0)


class MergedPad:
    """Both boards in, one virtual joystick out, buttons renumbered by `swap`: a button is down
    if any board holds it, an axis follows whichever board is pushed furthest from centre."""

    def __init__(self, boards, swap):
        info = {a: abs_info(boards[0], a) for a in AXES}
        self.fd = _create("Arcade Cabinet (merged)", range(BTN_FIRST, BTN_FIRST + BUTTONS), info)
        self.swap = swap
        self.buttons = {b: set() for b in boards}
        self.axes = {b: {} for b in boards}
        self.centre = {a: (i[1] + i[2]) // 2 for a, i in info.items()}
        self.sent = {}
        for b in boards:  # a new uinput axis reads 0 (full left/up) until it is set
            for a in AXES:
                self.feed(b, EV_ABS, a, abs_info(b, a)[0])

    def feed(self, board, etype, code, value):
        if etype == EV_KEY and BTN_FIRST <= code < BTN_FIRST + BUTTONS:
            n = code - BTN_FIRST
            (self.buttons[board].add if value else self.buttons[board].discard)(n)
            out = (EV_KEY, BTN_FIRST + self.swap.get(n, n), int(any(n in b for b in self.buttons.values())))
        elif etype == EV_ABS and code in AXES:
            self.axes[board][code] = value
            c = self.centre[code]
            out = (EV_ABS, code, max((a.get(code, c) for a in self.axes.values()), key=lambda x: abs(x - c)))
        else:
            return
        if self.sent.get(out[:2]) != out[2]:
            self.sent[out[:2]] = out[2]
            write_event(self.fd, *out)

    def sync_axes(self, centred):
        """Under a pane the stick's moves are not fed: when a pane opens, the axes go to centre (the game
        stops turning / walking); when it closes, they take each board's real position again."""
        for board in self.axes:
            for a in AXES:
                self.feed(board, EV_ABS, a, self.centre[a] if centred else abs_info(board, a)[0])

    def close(self):
        destroy(self.fd)
