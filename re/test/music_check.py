"""Music memory and restart check: runs the release build of one or more tables with audio on (SDL's
dummy driver, nothing audible) on private Xvfb displays, restarts the music with real keys, and
measures the memory in use (VmRSS + VmSwap, so swapping does not hide any) and fluidsynth's voice
warnings.

  python re/test/music_check.py --bin bin/SpaceCadetPinball --soundfont <file.sf2> --display 120 \
      --out re/work/runs/music [--tables cadet,dragon,pirates] [--seconds 52]

Each table runs three cases, in parallel per table on displays N, N+1, N+2:
  on    Music=1, SDL_SOUNDFONTS=<soundfont>
  off   Music=0, SDL_SOUNDFONTS=<soundfont>
  nosf  Music=1, SDL_SOUNDFONTS=<missing file>: no MIDI synthesizer can load, the memory baseline
Keys (seconds after the window is placed): F2 new game at 6; F3 pause / resume at 40 / 42 (after the
track has looped); F6 music off / on at 44 / 46; F2 new game at 48. Memory is sampled every 0.25 s.

Pass when (MB = 2^20 bytes; music cost = memory(case) - memory(nosf) at the end of the run):
  - the music cost with music on differs between the tables by at most 12 MB (one synthesizer
    whatever the number of tracks a table loads);
  - the music cost with music off, before F6 switches it on, is at most 20 MB (no SoundFont loaded;
    an empty synthesizer is about 13 MB with fluidsynth 2.6);
  - memory with music on grows by at most 8 MB from the first restart (t=40) to the end;
  - the game's log (stdout and stderr) has no "Ringbuffer full" / "Failed to allocate a synthesis
    process" line.
Exit 0 = pass, 1 = fail, 2 = cannot run. Writes <out>/<table>-<case>/{target.log,memory.json}
and <out>/summary.json. Stdlib only.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ftharness import session as sm  # noqa: E402

DATA_DIRS = {"cadet": "CADET", "dragon": "DRAGON", "pirates": "PIRATE"}
TITLES = {"cadet": "Pinball", "dragon": "^Full Tilt!", "pirates": "^Full Tilt!"}
KEYS = [(6, "F2"), (40, "F3"), (42, "F3"), (44, "F6"), (46, "F6"), (48, "F2")]
FIRST_RESTART_S = 40
FIRST_TOGGLE_S = 44
FLOOD = ("Ringbuffer full", "Failed to allocate a synthesis process")
COST_SPREAD_MB, OFF_COST_MB, GROWTH_MB = 12, 20, 8
MB = 1024 * 1024


def footprint(pid: int) -> int:
    """VmRSS + VmSwap of pid in bytes (memory in use whether or not the system has swapped part of it
    out). Raises HarnessError when the process is gone or the fields are missing."""
    try:
        fields = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines())
        return (int(fields["VmRSS"].split()[0]) + int(fields["VmSwap"].split()[0])) * 1024
    except (OSError, ValueError, KeyError) as e:
        raise sm.HarnessError(f"cannot read the memory of process {pid}: {e!r}") from e


def make_target(table: str, case: str, soundfont: Path, out: Path) -> dict:
    """Harness target of one table and case (session.py native target format)."""
    fonts = str(soundfont) if case != "nosf" else str(out / "no-such-soundfont.sf2")
    return {"kind": "native", "data_dir": DATA_DIRS[table], "args": ["-sw", f"-table={table}"],
            "title": TITLES[table], "min_window": [300, 300],
            "ini": {"Sounds": 1, "Music": 0 if case == "off" else 1, "ShowMenu": 0},
            "env": {"SDL_AUDIODRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "SDL_SOUNDFONTS": fonts}}


def run_case(table: str, case: str, display: int, out: Path, seconds: float) -> dict:
    """One run of one table and case (target music-<table>-<case>); returns its measurements. Raises
    HarnessError when it cannot run. Runs in a worker thread, so the session is closed here, not
    through Session.__enter__ (which installs signal handlers, main thread only)."""
    out.mkdir(parents=True, exist_ok=True)
    name = f"music-{table}-{case}"
    sess = sm.Session(name, display, out)
    samples: list[tuple[float, int]] = []
    try:
        sess.start()
        pid = sess.proc.pid
        t0 = time.monotonic()
        for at, key in KEYS + [(seconds, None)]:
            while time.monotonic() - t0 < at:
                if sess.proc.poll() is not None:
                    raise sm.HarnessError(f"{name}: exited rc={sess.proc.returncode} at "
                                          f"{time.monotonic() - t0:.1f} s (see {out / 'target.log'})")
                samples.append((round(time.monotonic() - t0, 2), footprint(pid)))
                time.sleep(0.25)
            if key:
                sess.xdo("windowfocus", sess.wid)
                sess.xdo("key", key)
    finally:
        sess.close()
    log = (out / "target.log").read_text(encoding="utf-8", errors="replace").splitlines()
    flood = sum(1 for line in log if any(f in line for f in FLOOD))
    after = [mem for t, mem in samples if t >= FIRST_RESTART_S]
    result = {"table": table, "case": case, "mem_end": samples[-1][1], "mem_peak": max(r for _, r in samples),
              "mem_before_toggle": [mem for t, mem in samples if t < FIRST_TOGGLE_S][-1],
              "growth": (after[-1] - after[0]) if after else None, "flood_lines": flood,
              "midi_lines": [line for line in log if line.startswith("midi:") and "track" not in line
                             and "opened" not in line]}
    (out / "memory.json").write_text(json.dumps({"samples": samples, **result}, indent=1), encoding="utf-8")
    return result


def judge(results: dict) -> list[str]:
    """Failure messages (empty = pass) for results[table][case]."""
    fails = []
    cost = {t: (c["on"]["mem_end"] - c["nosf"]["mem_end"]) / MB for t, c in results.items()}
    if len(cost) > 1 and max(cost.values()) - min(cost.values()) > COST_SPREAD_MB:
        fails.append(f"music cost differs between tables by more than {COST_SPREAD_MB} MB: "
                     + ", ".join(f"{t} {v:.1f} MB" for t, v in cost.items()))
    for t, c in results.items():
        off = (c["off"]["mem_before_toggle"] - c["nosf"]["mem_before_toggle"]) / MB
        if off > OFF_COST_MB:
            fails.append(f"{t}: music off still costs {off:.1f} MB")
        growth = c["on"]["growth"]
        if growth is None or growth / MB > GROWTH_MB:
            fails.append(f"{t}: memory with music on grew {growth} bytes after the first restart")
        for case, r in c.items():
            if r["flood_lines"]:
                fails.append(f"{t}/{case}: {r['flood_lines']} fluidsynth voice warnings")
    return fails


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--soundfont", required=True)
    p.add_argument("--display", type=int, required=True, help="first of three displays")
    p.add_argument("--out", required=True)
    p.add_argument("--tables", default="cadet,dragon,pirates")
    p.add_argument("--seconds", type=float, default=52)
    a = p.parse_args(argv)
    binary, soundfont, out = Path(a.bin).resolve(), Path(a.soundfont).resolve(), Path(a.out).resolve()
    tables = a.tables.split(",")
    if not binary.is_file() or not soundfont.is_file() or any(t not in DATA_DIRS for t in tables):
        print(f"music_check: binary {binary} / soundfont {soundfont} missing or unknown table in {tables}",
              file=sys.stderr)
        return 2
    sm.NATIVE_BIN = binary
    ours = {f"music-{t}-{c}": make_target(t, c, soundfont, out / f"{t}-{c}")
            for t in tables for c in ("on", "off", "nosf")}
    known = sm.load_targets()
    sm.load_targets = lambda: {**known, **ours}          # Session.start looks targets up by name
    results: dict = {t: {} for t in tables}
    errors: list[str] = []

    def worker(case: str, display: int) -> None:
        for t in tables:
            try:
                results[t][case] = run_case(t, case, display, out / f"{t}-{case}", a.seconds)
            except (sm.HarnessError, OSError) as e:
                errors.append(f"{t}/{case}: {e}")

    threads = [threading.Thread(target=worker, args=(case, a.display + i))
               for i, case in enumerate(("on", "off", "nosf"))]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    if errors:
        print("music_check: NOT ASSESSED\n  " + "\n  ".join(errors), file=sys.stderr)
        return 2
    fails = judge(results)
    (out / "summary.json").write_text(json.dumps({"results": results, "fails": fails}, indent=1), encoding="utf-8")
    for t, c in results.items():
        print(f"{t}: " + ", ".join(f"{k} end {r['mem_end'] / MB:.1f} MB (before F6 {r['mem_before_toggle'] / MB:.1f})"
                                    f" peak {r['mem_peak'] / MB:.1f} MB flood {r['flood_lines']}"
                                    for k, r in sorted(c.items())))
        print(f"  music lines (on): {c['on']['midi_lines']}")
    print("PASS" if not fails else "FAIL\n  " + "\n  ".join(fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
