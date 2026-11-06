"""Self-test of the native window size at capture time.
Run: python re/test/selftest_window_size.py --bin <hook build binary> --display N [--runs 8]

With --script it runs any hook script instead (for example the hook_scripts scripts U9, U13b, U11b, U15);
with --baseline every frame must also equal the baseline frame byte for byte.
Runs the Space Cadet built-in hook script (shots at frames 5, 70, 130) --runs times on the display and requires
every frame of every run to be 960x666 and every run's frames to be byte-identical to the first run's. 
Exit 0 = pass, 1 = fail, 2 = cannot run (NOT ASSESSED).
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hookrun                                                 # noqa: E402
from ftharness.session import WORK                             # noqa: E402

BUILTIN_SCRIPT = """init window 960 666
init fixed_dt 16.667
init seed 7
5 shot start
10 key down new_game
11 key up new_game
70 shot new_game
80 key down plunger
130 shot plunger_pulled
131 key up plunger
132 quit
"""


def size(p: Path) -> str:
    return subprocess.run(["magick", "identify", "-format", "%wx%h", str(p)], check=True,
                          capture_output=True, text=True).stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--display", type=int, required=True)
    ap.add_argument("--runs", type=int, default=8)
    ap.add_argument("--script", help="hook script to run instead of the built-in one (e.g. hook_scripts/U15_two_player.uh)")
    ap.add_argument("--table", default="cadet")
    ap.add_argument("--baseline", help="directory of baseline frames/*.bmp every run must equal byte for byte")
    a = ap.parse_args()
    root = WORK / "harness_runs" / "selftest_window_size"
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    if a.script:
        script = Path(a.script).resolve()
    else:
        script = root / "builtin.uh"
        script.write_text(BUILTIN_SCRIPT, encoding="utf-8")
    bad, first = [], None
    if a.baseline:
        first = {f.stem: f.read_bytes() for f in Path(a.baseline).glob("*.bmp")}
        if not first:
            print(f"no baseline frames in {a.baseline}: cannot assess")
            return 2
    for i in range(a.runs):
        ns = argparse.Namespace(out=str(root / f"run{i}"), bin=a.bin, table=a.table, display=a.display,
                                timeout=60, script=str(script))
        rc = hookrun.run(ns)
        if rc != 0:
            print(f"run {i}: hookrun rc {rc}: cannot assess")
            return 2
        frames = sorted((root / f"run{i}" / "frames").glob("*.bmp"))
        sizes = {f.stem: size(f) for f in frames}
        data = {f.stem: f.read_bytes() for f in frames}
        first = first or data
        ok = set(sizes.values()) == {"960x666"} and data == first
        print(f"run {i}: {sizes} identical-to-{'baseline' if a.baseline else 'run0'}={data == first} -> {'PASS' if ok else 'FAIL'}")
        if not ok:
            bad.append(i)
    print(f"{a.runs - len(bad)}/{a.runs} runs PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
