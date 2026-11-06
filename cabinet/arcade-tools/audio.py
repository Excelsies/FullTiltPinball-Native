"""Cabinet master volume for settings_menu.py: the ALSA 'HDMI' control on card 0, and its saved value.

Volume is in amixer's mapped percent (-M, the alsamixer scale, so 50 % sounds about half). Mute is
the control's playback switch, so un-muting restores the level by itself. The chosen values live on
retrodata (kept while the cabinet is locked) and are applied when cabinetd starts.
EmulationStation never touches this mixer (it looks for a 'PCM' control that card 0 does not have).
"""
import json
import math
import os
import re
import struct
import subprocess
import wave

CARD, CONTROL = "0", "HDMI"
SETTINGS = "/mnt/retrodata/cabinet/settings.json"
TICK = "/dev/shm/cabinet/tick.wav"
STEP = 5


class AudioError(Exception):
    pass


def _run(args):
    """Run a short command, raise AudioError with its output when it fails (or cannot start)."""
    try:
        proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    except OSError as e:  # e.g. amixer missing: a message in the menu, cabinetd keeps running
        raise AudioError(f"{args[0]} could not start: {e.strerror or e}")
    if proc.returncode != 0:
        raise AudioError(f"{' '.join(args)}: rc {proc.returncode}: {proc.stdout.strip()[-200:]}")
    return proc.stdout


def read_mixer():
    """(volume %, muted) as the mixer has them now."""
    out = _run(["amixer", "-c", CARD, "-M", "sget", CONTROL])
    vol, switch = re.search(r"\[(\d+)%\]", out), re.search(r"\[(on|off)\]", out)
    if not vol or not switch:
        raise AudioError(f"amixer sget {CONTROL}: unexpected output: {out.strip()[-200:]}")
    return int(vol.group(1)), switch.group(1) == "off"


def start_volume(percent):
    """Start setting the volume (returns the running amixer); finish_volume() checks it."""
    assert 0 <= percent <= 100, percent
    try:
        return subprocess.Popen(["amixer", "-q", "-c", CARD, "-M", "sset", CONTROL, f"{percent}%"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    except OSError as e:
        raise AudioError(f"amixer could not start: {e.strerror or e}")


def finish_volume(proc, percent):
    out, _ = proc.communicate()
    if proc.returncode != 0:
        raise AudioError(f"amixer sset {CONTROL} {percent}%: rc {proc.returncode}: {out.strip()[-200:]}")
    got, _ = read_mixer()
    if abs(got - percent) > 1:  # the control stores 0.01 dB steps: mapped % may round by one
        raise AudioError(f"asked for {percent} %, the mixer reads {got} %")


def set_volume(percent):
    finish_volume(start_volume(percent), percent)


def set_muted(muted):
    _run(["amixer", "-q", "-c", CARD, "sset", CONTROL, "mute" if muted else "unmute"])
    if read_mixer()[1] != muted:
        raise AudioError(f"the mixer did not {'mute' if muted else 'unmute'}")


def load():
    """Saved {"volume", "muted"}, None if never saved; AudioError if the file is unusable."""
    try:
        with open(SETTINGS) as fh:
            data = json.load(fh)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as e:
        raise AudioError(f"{SETTINGS}: {e}")
    vol, muted = (data.get("volume"), data.get("muted")) if isinstance(data, dict) else (None, None)
    if not isinstance(vol, int) or isinstance(vol, bool) or not 0 <= vol <= 100 or not isinstance(muted, bool):
        raise AudioError(f"{SETTINGS}: expected volume 0-100 and muted true/false, got {data!r}")
    return {"volume": vol, "muted": muted}


def save(state):
    """Write the settings atomically (retrodata is a real disk: tmp file, fsync, rename)."""
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    tmp = SETTINGS + ".tmp"
    with open(tmp, "w") as fh:
        json.dump({"volume": state["volume"], "muted": state["muted"]}, fh)
        fh.write("\n")
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, SETTINGS)


def apply(state):
    set_volume(state["volume"])
    set_muted(state["muted"])


def write_tick():
    """A 50 ms 1 kHz beep (5 ms fades) for hearing the new volume."""
    os.makedirs(os.path.dirname(TICK), exist_ok=True)
    rate, n, fade = 48000, 2400, 240
    frames = bytearray()
    for i in range(n):
        env = min(1.0, i / fade, (n - 1 - i) / fade)
        s = int(0.3 * 32767 * env * math.sin(2 * math.pi * 1000 * i / rate))
        frames += struct.pack("<hh", s, s)
    with wave.open(TICK + ".tmp", "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    os.replace(TICK + ".tmp", TICK)


class Tick:
    """Plays the tick on card 0 (a free substream: the VideoCore mixes it with the game / menu)."""

    def __init__(self):
        self.proc = None

    def reap(self):
        """None while idle or playing, else an error text if the last tick failed."""
        if not self.proc or self.proc.poll() is None:
            return None
        proc, self.proc = self.proc, None
        out = proc.stdout.read().strip()
        proc.stdout.close()
        return None if proc.returncode == 0 else f"aplay rc {proc.returncode}: {out[-160:]}"

    def play(self):
        """Start a tick unless one is still playing; returns reap()'s error of the previous one."""
        error = self.reap()
        if self.proc is None:
            try:
                self.proc = subprocess.Popen(["aplay", "-q", "-D", f"plughw:{CARD},0", TICK],
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             universal_newlines=True)
            except OSError as e:  # aplay missing: no tick, the volume itself is set
                return error or f"aplay could not start: {e.strerror or e}"
        return error


class Volume:
    """The menu's volume / mute, the saved copy, and problems for the menu's status line."""

    def __init__(self, log):
        self.log, self.problems, self.tick = log, [], Tick()
        self.volume, self.muted, self.saved = None, False, None
        self.changed = False  # set by a change in the menu: only then is the file written
        try:
            write_tick()
        except OSError as e:
            self.problem("No preview sound", e)
        try:
            self.saved = load()
        except AudioError as e:
            self.problem("Saved volume unreadable", e)
        if self.saved:
            try:
                apply(self.saved)
                log(f"settings: volume {self.saved['volume']} % muted {'yes' if self.saved['muted'] else 'no'} applied")
            except AudioError as e:
                self.problem("Saved volume not applied", e)
        elif not self.problems:
            log("settings: no saved volume yet, mixer left as it is")
        self.sync()

    def problem(self, text, detail):
        """Log text and detail; the menu's status line shows the short text."""
        self.log(f"settings: {text}: {detail}")
        shown = f"{text} (see the log)"
        if shown not in self.problems:
            self.problems.append(shown)

    def sync(self):
        """Take the mixer's values (something else may have changed them); ours if within rounding."""
        try:
            vol, self.muted = read_mixer()
        except AudioError as e:
            self.volume = None
            return self.problem("Mixer unreadable", e)
        if self.volume is None or abs(vol - self.volume) > 1:
            self.volume = self.saved["volume"] if self.saved and abs(vol - self.saved["volume"]) <= 1 else vol

    def recheck(self):
        """alsa-state restores its stored mixer at boot in parallel with cabinetd: undo that, logged.
        What cabinetd last set wins: the menu's values after a change there (not saved until the menu
        closes), else the saved ones; never an older value over the user's change."""
        want, which = ({"volume": self.volume, "muted": self.muted}, "the menu's values") \
            if self.changed and self.volume is not None else (self.saved, "saved values")
        if not want:
            return
        try:
            now = read_mixer()
            if abs(now[0] - want["volume"]) > 1 or now[1] != want["muted"]:
                apply(want)
                self.log(f"settings: mixer changed after start to {now} (alsa restore?), re-applied {which} {want}")
            else:
                self.log(f"settings: mixer still as cabinetd set it 10 s after start ({which} {want})")
        except AudioError as e:
            self.problem("Volume not re-applied", e)
        self.sync()

    def save(self):
        """Store changed values; returns an error text or None."""
        state = {"volume": self.volume, "muted": self.muted}
        if self.volume is None or not self.changed or state == self.saved:
            return None
        try:
            save(state)
        except OSError as e:
            self.log(f"settings: saving {state} to {SETTINGS} failed: {e}")
            return str(e)[:80]
        self.saved, self.changed = state, False
        self.problems = [p for p in self.problems if not p.startswith("Saved volume")]
        self.log(f"settings: saved {state}")
        return None

    def change(self, step, redraw):
        """A +/- STEP change: amixer starts, redraw() shows the new value meanwhile, then the mixer is
        checked and the tick played. Error text or None (also None when nothing changes)."""
        new = min(100, max(0, (round(self.volume / STEP) + step) * STEP))
        if new == self.volume and not self.muted:
            return None
        try:
            proc = start_volume(new)
        except AudioError as e:  # amixer did not start: nothing changed
            self.log(f"settings: volume {new} %: {e}")
            return str(e)[:80]
        was_muted, self.changed = self.muted, True
        self.volume, self.muted = new, False
        redraw()  # overlaps amixer
        try:
            finish_volume(proc, new)
            if was_muted:
                set_muted(False)
        except AudioError as e:
            self.log(f"settings: volume {new} %: {e}")
            self.sync()
            return str(e)[:80]
        self.log(f"settings: volume {new} %{' (unmuted)' if was_muted else ''}")
        error = self.tick.play()
        if error:
            self.problem("No preview sound (aplay failed)", error)
        return None

    def toggle_mute(self):
        try:
            set_muted(not self.muted)
        except AudioError as e:
            self.log(f"settings: mute: {e}")
            return str(e)[:80]
        self.muted, self.changed = not self.muted, True
        self.log(f"settings: {'muted' if self.muted else 'unmuted'}")
        return None
