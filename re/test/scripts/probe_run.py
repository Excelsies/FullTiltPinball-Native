"""Replay an input script against a Wine target and read rule state from the game's memory.

  python re/test/scripts/probe_run.py wine-dragon re/test/scripts/dragon_start.json \
      --watch re/test/scripts/watch_dragon.json --display 90 --out DIR [--period-ms 50]

Same script format as harness.py (wait / tap / down / up / shot / burst) plus one step kind:
  {"peek": "label"}      read every watch value now and log it (peeks.jsonl)
  {"rootshot": "name"}   screenshot of the whole X screen (dialogs are separate windows)
  {"windows": "label"}   list the visible X windows (id, name, geometry) now and append to windows.jsonl (this
                         shows whether a modal Win32 dialog of the original, e.g. its high-score name entry, is up)
  {"call": "label", "func": "423060", "code": "4a", "part": "43b114"}   run the original's stdcall function
                         func(code, *part) once in the game process (ftcall.exe: remote thread with a stub; part = ftpeek chain
                         whose 32-bit value is the part pointer). Writes to the game: use only in diagnosis scripts
                         (e.g. the control dispatcher with ControlCollision 0x4a, as the native hook's `event`); the result
                         line goes to calls.jsonl. Needs ftcall.exe (ftcall.c) next to ftpeek.exe.
  {"restpeek": "label", "watch": "<rest file>"}   (watch optional, see rest_watch) read every value of a typed rest file
                         (make_rest_watch.py) now, one ftpeek.exe run per batch of <= 64 specs
                         while the game sits still; appends {"label", "t", <name>: value} to
                         rest.jsonl (i32 / u8 masked / f32 as its 32-bit pattern / armed 0|1;
                         a '?' read is kept as null and counted in the stdout summary)
A sampler (ftpeek.exe, built from ftpeek.c with mingw, run in the same Wine prefix) reads the
watch values every --period-ms and logs every change to timeline.jsonl (mode transitions, ball
counts, jackpot); "peek" records the latest sample. Read only: nothing is written to the game.
The Wine game process is re-parented to systemd, so a Linux-side /proc/<pid>/mem reader is
refused under kernel.yama.ptrace_scope = 1 (measured); ReadProcessMemory inside Wine
works through wineserver.

Watch file (addresses come from the Ghidra analysis of the exe; no game data in it):
  {"table_global": "0x439b00",               # address of the TPinballTable* global
   "values": [
     {"name": "mode", "tag": "0x439ce8", "field": 6},   # tag -> component (tag+4) -> int at +6
     {"name": "multiball", "table": "0x192"},           # TableG + offset
     {"name": "ball_save", "addr": "0x434d2c"}]}        # plain int32 global
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TEST_DIR))
from ftharness.replay import KeyPipe, capture, crop_regions, keysym, STEP_KINDS  # noqa: E402
from ftharness.session import HarnessError, Session, wine_env, x_env                            # noqa: E402

KINDS = STEP_KINDS | {"peek", "rootshot", "restpeek", "windows", "call"}
MAX_STEPS = 10000


FTCALL = TEST_DIR.parent / "work" / "evidence" / "reference" / "2026-11-01" / "tools" / "ftcall.exe"
FTPEEK = TEST_DIR.parent / "work" / "evidence" / "reference" / "2026-11-01" / "tools" / "ftpeek.exe"


ANCHORS = ("placed", "window")


def script_t0(sess: Session, anchor: str) -> float:
    """Monotonic time that is script time 0. "placed" (default, as every recorded reference): when Session.start
    returned, 0.30 s after the window was found (the move / focus / 0.3 s settle). "window": when the window search
    succeeded - the instant the original's 2000 ms start-up wait is measured from (README), so a
    script time T is T seconds after the wait began, as in a native hook script (`init startup_wait 1`)."""
    if anchor not in ANCHORS:
        raise HarnessError(f"anchor must be one of {ANCHORS}, got {anchor!r}")
    t = sess.t_window if anchor == "window" else sess.t_placed
    if t <= 0:
        raise HarnessError(f"session has no {anchor!r} time (start() not completed)")
    return t


def check_after_focus(sess: Session, t0: float, when: float) -> None:
    """A step scheduled at script time `when` must not fire before the window is placed and focused (possible only
    with anchor "window", whose time 0 is earlier): fail instead of sending keys into an unfocused window."""
    if t0 + when < sess.t_placed - 1e-6:
        raise HarnessError(f"script time {when:.3f} s is {sess.t_placed - t0 - when:.3f} s before the window was "
                           f"focused (anchor window): start the script later")


MAX_SPECS = 64                       # ftpeek.c MAX_SPECS


def _num(x) -> int:
    """A field offset: an int, or a string such as '0x7e' (rest files)."""
    return x if isinstance(x, int) else int(x, 0)


def peek_specs(watch: dict) -> tuple[list[str], list[str]]:
    """Watch values -> ftpeek spec chains (see ftpeek.c) and their names."""
    tg = int(watch["table_global"], 16)
    names, specs = [], []
    for v in watch["values"]:
        names.append(v["name"])
        if "chain" in v:                                 # raw ftpeek chain, e.g. "43ad3c:5e:4:0"
            specs.append(v["chain"])
        elif "tag" in v:                                   # tag+4 = component*, then the field
            specs.append(f"{int(v['tag'], 16) + 4:x}:{_num(v.get('field', 6)):x}")
        elif "table" in v:
            specs.append(f"{tg:x}:{int(v['table'], 16):x}")
        else:
            specs.append(f"{int(v['addr'], 16):x}")
    return names, specs


def rest_watch(step: dict, args) -> dict:
    """The rest file of a restpeek step: the step's "watch" (relative: next to this file), else --rest-watch, else
    rest_<table>.json next to --watch (target wine-dragon -> rest_dragon.json)."""
    path = step.get("watch") or args.rest_watch or \
        Path(args.watch).parent / f"rest_{args.target.split('-', 1)[-1]}.json"
    path = Path(path)
    if not path.is_absolute():                  # a script's relative "watch" means: next to probe_run.py
        path = Path(__file__).resolve().parent / path
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise HarnessError(f"restpeek: cannot read rest file {path}: {exc}") from exc


def normalise_rest(value: int | None, typ: str | None) -> int | None:
    """ftpeek prints signed 32-bit values; map to the comparable form of wine_compare.py."""
    if value is None:
        return None
    if typ == "u8":
        return value & 0xFF
    if typ == "f32":
        return value & 0xFFFFFFFF
    if typ == "armed":
        return 1 if value != 0 else 0
    return value


def restpeek(sess: Session, watch: dict, label: str, t0: float, out) -> int:
    """One read of every value in a rest file; returns the number of '?' reads."""
    names, specs = peek_specs(watch)
    types = [v.get("type") for v in watch["values"]]
    row = {"label": label, "t": round(time.monotonic() - t0, 3)}
    unreadable = 0
    for i in range(0, len(specs), MAX_SPECS):
        batch = specs[i:i + MAX_SPECS]
        res = subprocess.run(["wine", str(FTPEEK), sess.target["exe"], "1", "1", *batch],
                             capture_output=True, text=True, env=wine_env(sess.display, sess.target),
                             timeout=120)
        lines = [ln for ln in res.stdout.splitlines() if ln.strip()]
        parts = lines[0].split("\t") if lines else []
        if res.returncode != 0 or len(parts) != len(batch) + 1:
            raise HarnessError(f"restpeek: ftpeek rc={res.returncode}, {len(parts) - 1} values for "
                               f"{len(batch)} specs: {res.stderr.strip()[:200]}")
        for j, v in enumerate(parts[1:]):
            raw = None if v == "?" else int(v)
            unreadable += raw is None
            row[names[i + j]] = normalise_rest(raw, types[i + j])
    out.write(json.dumps(row) + "\n")
    out.flush()
    return unreadable


def list_windows(sess: Session) -> list[dict]:
    """Visible windows of the target's display: id, name, geometry (xdotool; a window that vanishes while
    the list is read is skipped, not guessed)."""
    ids = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "."], capture_output=True, text=True,
                         env=x_env(sess.display), timeout=20).stdout.split()
    rows = []
    for wid in ids:
        name = subprocess.run(["xdotool", "getwindowname", wid], capture_output=True, text=True,
                              env=x_env(sess.display), timeout=20)
        geo = subprocess.run(["xdotool", "getwindowgeometry", "--shell", wid], capture_output=True, text=True,
                             env=x_env(sess.display), timeout=20)
        if name.returncode or geo.returncode:
            continue
        g = dict(ln.split("=", 1) for ln in geo.stdout.split() if "=" in ln)
        rows.append({"id": int(wid), "name": name.stdout.strip(), **{k.lower(): int(v) for k, v in g.items()
                                                                   if k in ("X", "Y", "WIDTH", "HEIGHT")}})
    return rows


class Sampler:
    """Runs ftpeek.exe inside the target's Wine prefix; keeps the latest sample and writes every
    change to timeline.jsonl as {t (s since replay start), name: value, ...}."""

    def __init__(self, sess: Session, watch: dict, out: Path, period_ms: int, t0: float):
        self.names, specs = peek_specs(watch)
        self.latest: dict = {}
        self.t0 = t0
        self.timeline = open(out / "timeline.jsonl", "w")
        self.err = open(out / "ftpeek.log", "w")
        self.proc = subprocess.Popen(
            ["wine", str(FTPEEK), sess.target["exe"], str(period_ms), "100000000", *specs],
            stdout=subprocess.PIPE, stderr=self.err, text=True, env=wine_env(sess.display, sess.target),
            start_new_session=True)
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        for line in self.proc.stdout:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != len(self.names) + 1:
                continue
            vals = {n: (None if v == "?" else int(v)) for n, v in zip(self.names, parts[1:])}
            self.latest = vals
            self.timeline.write(json.dumps({"t": round(time.monotonic() - self.t0, 3),
                                            "ms": int(parts[0]), **vals}) + "\n")
            self.timeline.flush()

    def close(self) -> None:
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGTERM)
            self.proc.wait(timeout=10)
        self.thread.join(timeout=5)
        self.timeline.close()
        self.err.close()


def run(args) -> int:
    script = json.loads(Path(args.script).read_text(encoding="utf-8"))
    steps = script["steps"]
    if not steps or len(steps) > MAX_STEPS:
        raise HarnessError("script needs 1..10000 steps")
    for i, step in enumerate(steps):
        if len(KINDS & set(step)) != 1:
            raise HarnessError(f"step {i} must have exactly one of {sorted(KINDS)}: {step}")
    watch = json.loads(Path(args.watch).read_text(encoding="utf-8"))
    out = Path(args.out).resolve()
    sess = Session(args.target, args.display, out, window_poll=args.window_poll)
    status = "fail"
    with sess:
        t0 = script_t0(sess, args.anchor)
        sess.note("script_t0", anchor=args.anchor, t_since_window_s=round(t0 - sess.t_window, 4))
        peeks = open(out / "peeks.jsonl", "w")
        rest = open(out / "rest.jsonl", "w")
        wins = open(out / "windows.jsonl", "w")
        calls = open(out / "calls.jsonl", "w")
        rest_unreadable = 0
        sampler = Sampler(sess, watch, out, args.period_ms, t0)
        keys = KeyPipe(sess)
        crops, sched = [], 0.0

        def at(when: float) -> None:
            check_after_focus(sess, t0, when)
            delay = t0 + when - time.monotonic()
            if delay > 0:
                time.sleep(delay)

        try:
            for step in steps:
                if "wait" in step:
                    sched += float(step["wait"])
                elif "tap" in step:
                    at(sched)
                    keys.send(f"keydown {keysym(step['tap'])}")
                    sched += float(step.get("hold", 0.08))
                    at(sched)
                    keys.send(f"keyup {keysym(step['tap'])}")
                elif "down" in step:
                    at(sched)
                    keys.send(f"keydown {keysym(step['down'])}")
                elif "up" in step:
                    at(sched)
                    keys.send(f"keyup {keysym(step['up'])}")
                elif "shot" in step or "burst" in step:
                    names = [step["shot"]] if "shot" in step else \
                        [f"{step['burst']}_{n:02d}" for n in range(int(step.get("count", 5)))]
                    for name in names:
                        at(sched)
                        capture(sess, name)
                        crops.append((name, step.get("crops", [])))
                        if "burst" in step:
                            sched += float(step.get("interval", 0.1))
                elif "rootshot" in step:
                    at(sched)
                    (out / "shots").mkdir(exist_ok=True)
                    subprocess.run(["import", "-display", f":{sess.display}", "-window", "root",
                                    str(out / "shots" / f"{step['rootshot']}.png")],
                                   check=True, capture_output=True, timeout=30)
                elif "windows" in step:
                    at(sched)
                    wins.write(json.dumps({"label": step["windows"], "t": round(time.monotonic() - t0, 3),
                                           "windows": list_windows(sess)}) + "\n")
                    wins.flush()
                elif "call" in step:
                    at(sched)
                    done = subprocess.run(
                        ["wine", str(FTCALL), sess.target["exe"], step["func"], step["code"], step["part"]],
                        capture_output=True, text=True, env=wine_env(sess.display, sess.target), timeout=60)
                    calls.write(json.dumps({"label": step["call"], "t": round(time.monotonic() - t0, 3),
                                            "rc": done.returncode, "out": done.stdout.strip(),
                                            "err": done.stderr.strip()[-200:]}) + "\n")
                    calls.flush()
                    if done.returncode:
                        raise HarnessError(f"call {step['call']}: ftcall rc={done.returncode}: {done.stderr.strip()[-200:]}")
                elif "restpeek" in step:
                    at(sched)
                    rest_unreadable += restpeek(sess, rest_watch(step, args), step["restpeek"],
                                                t0, rest)
                elif "peek" in step:
                    at(sched)
                    peeks.write(json.dumps({"label": step["peek"],
                                            "t": round(time.monotonic() - t0, 3),
                                            **sampler.latest}) + "\n")
                    peeks.flush()
                if sess.proc.poll() is not None:
                    raise HarnessError(f"target exited rc={sess.proc.returncode}")
            status = "pass"
        finally:
            keys.close()
            peeks.close()
            rest.close()
            wins.close()
            calls.close()
            sampler.close()
        for name, names in crops:
            crop_regions(sess, name, names)
    (out / "run.json").write_text(json.dumps({"target": args.target, "script": args.script,
                                              "status": status, "log": sess.log},
                                             indent=2), encoding="utf-8")
    print(json.dumps({"out": str(out), "status": status, "rest_unreadable": rest_unreadable}))
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("target")
    p.add_argument("script")
    p.add_argument("--watch", required=True)
    p.add_argument("--rest-watch", help="rest file for restpeek steps without a 'watch'")
    p.add_argument("--display", type=int, default=90,
                   help="Xvfb display; the range comes from targets.json _displays")
    p.add_argument("--out", required=True)
    p.add_argument("--anchor", choices=ANCHORS, default="placed",
                   help="script time 0: 'placed' (default, matches the recorded references) or 'window' = when the "
                        "window was found, the same instant a native `init startup_wait 1` script starts (README)")
    p.add_argument("--window-poll", type=float, default=0.1,
                   help="seconds between window searches; the 'window' anchor is late by up to this (default 0.1)")
    p.add_argument("--period-ms", type=int, default=50, help="ftpeek sampling period")
    try:
        return run(p.parse_args())
    except HarnessError as exc:
        print(f"probe_run: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
