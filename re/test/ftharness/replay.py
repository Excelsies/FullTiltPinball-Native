"""Input scripts: load, validate, replay with xdotool (XTEST) and capture screenshots.

Script format (JSON):
  {"name": "smoke",
   "steps": [
     {"wait": 2.0},                                   # advance the schedule by 2 s
     {"tap": "new_game"},                             # key down, hold (default 0.08 s), key up
     {"tap": "plunger", "hold": 1.2},
     {"down": "left_flipper"}, {"wait": 0.3}, {"up": "left_flipper"},
     {"shot": "after_plunge", "crops": ["score"]},    # screenshot named by marker
     {"burst": "flip", "count": 5, "interval": 0.1}   # numbered shots flip_00.. every 0.1 s
   ]}
Key names are logical (keymap below) or any X keysym (``F2``, ``z``, ``slash``).
Steps run on an absolute schedule (time since replay start); every action logs its scheduled
and actual time, so input jitter is measured on each run.
"""
from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path

from .session import HarnessError, Session, x_env

# Full Tilt defaults (default key codes 90/191/32/88 for left flipper, right flipper, plunger and
# left bump, string table ids 1012-1015 of CADET/DRAGON/PIRATES.EXE; menu accelerators
# F2/F3/F4/F8/Ctrl+T) and upstream options.cpp defaults (same keys).
KEYMAP = {
    "left_flipper": "z", "right_flipper": "slash", "plunger": "space",
    "bump_left": "x", "bump_right": "period", "bump_bottom": "Up",
    "new_game": "F2", "pause": "F3", "fullscreen": "F4", "player_controls": "F8",
    "select_table": "ctrl+t", "sounds": "ctrl+s", "music": "ctrl+m",
}
STEP_KINDS = {"wait", "tap", "down", "up", "shot", "burst"}


def load_script(path: Path) -> dict:
    with open(path, encoding="utf-8") as fh:
        script = json.load(fh)
    steps = script.get("steps")
    if not isinstance(steps, list) or not steps:
        raise HarnessError(f"{path}: 'steps' must be a non-empty list")
    for i, step in enumerate(steps):
        kinds = STEP_KINDS & set(step)
        if len(kinds) != 1:
            raise HarnessError(f"{path}: step {i} must have exactly one of {sorted(STEP_KINDS)}: {step}")
    script.setdefault("name", path.stem)
    return script


def keysym(name: str) -> str:
    return KEYMAP.get(name, name)


def capture(sess: Session, marker: str) -> dict:
    shots = sess.out_dir / "shots"
    shots.mkdir(exist_ok=True)
    png = shots / f"{marker}.png"
    t = time.monotonic()
    subprocess.run(["import", "-display", f":{sess.display}", "-window", sess.wid, str(png)],
                   check=True, capture_output=True, timeout=30)
    return {"marker": marker, "file": str(png.relative_to(sess.out_dir)),
            "capture_s": round(time.monotonic() - t, 4)}


def crop_regions(sess: Session, marker: str, crops: list[str]) -> None:
    """Region crops run after the replay so they never delay scheduled input."""
    regions = sess.target.get("regions", {})
    shots = sess.out_dir / "shots"
    for crop in crops:
        if crop not in regions:
            raise HarnessError(f"target {sess.target_name} has no region {crop!r}")
        x, y, w, h = regions[crop]
        subprocess.run(["magick", str(shots / f"{marker}.png"), "-crop", f"{w}x{h}+{x}+{y}",
                        "+repage", str(shots / f"{marker}.{crop}.png")],
                       check=True, capture_output=True, timeout=30)


class KeyPipe:
    """One long-lived ``xdotool -`` reading commands from stdin: each key event costs a pipe
    write (sub-millisecond) instead of a process spawn (~14 ms measured)."""

    def __init__(self, sess: Session):
        self.errlog = open(sess.out_dir / "xdotool.log", "w")
        self.proc = subprocess.Popen(["xdotool", "-"], stdin=subprocess.PIPE, text=True,
                                     stdout=self.errlog, stderr=subprocess.STDOUT,
                                     env=x_env(sess.display))

    def send(self, line: str) -> None:
        if self.proc.poll() is not None:
            raise HarnessError(f"xdotool exited rc={self.proc.returncode} (see xdotool.log)")
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.stdin.close()
            self.proc.wait(timeout=10)
        self.errlog.close()
        if self.proc.returncode != 0:
            raise HarnessError(f"xdotool exited rc={self.proc.returncode} (see xdotool.log)")


def replay(sess: Session, script: dict) -> list[dict]:
    """Replay every step; returns the timing records (also appended to the session log)."""
    records: list[dict] = []
    crops: list[tuple[str, list[str]]] = []
    keys = KeyPipe(sess)
    start = time.monotonic()
    sched = 0.0

    def at(when: float) -> float:
        delay = start + when - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        return time.monotonic() - start

    def key(action: str, name: str, when: float) -> None:
        actual = at(when)
        keys.send(f"{action} {keysym(name)}")
        records.append({"action": action, "key": name, "sched": round(when, 4),
                        "actual": round(actual, 4), "late_ms": round((actual - when) * 1000, 2)})

    def shot(marker: str, step: dict) -> None:
        actual = at(sched)
        rec = capture(sess, marker)
        rec.update(sched=round(sched, 4), actual=round(actual, 4))
        records.append(rec)
        crops.append((marker, step.get("crops", [])))

    try:
        for step in script["steps"]:
            if "wait" in step:
                sched += float(step["wait"])
            elif "tap" in step:
                hold = float(step.get("hold", 0.08))
                key("keydown", step["tap"], sched)
                key("keyup", step["tap"], sched + hold)
                sched += hold
            elif "down" in step:
                key("keydown", step["down"], sched)
            elif "up" in step:
                key("keyup", step["up"], sched)
            elif "shot" in step:
                shot(step["shot"], step)
            elif "burst" in step:
                for n in range(int(step.get("count", 5))):
                    shot(f"{step['burst']}_{n:02d}", step)
                    sched += float(step.get("interval", 0.1))
            if sess.proc is not None and sess.proc.poll() is not None:
                raise HarnessError(f"target exited rc={sess.proc.returncode} during replay")
    finally:
        keys.close()
        for r in records:
            sess.note("step", **r)
    for marker, names in crops:
        crop_regions(sess, marker, names)
    return records
