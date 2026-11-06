"""Self-test of the compare report's reported-only markers.
Run: python re/test/selftest_report_only.py
Builds two synthetic harness run directories (run.json + one shot per marker, plain black images,
so no game or Wine is needed) for the real targets and checks the report of compare_runs:
`loaded` of the PIRATES audio-on pair is labelled "reported only", every other marker and every
other pair (default pair, DRAGON audio-on pair) stays "required", in report.json and report.md.
Exit 0 = pass, 1 = fail.
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ftharness.compare import compare_runs                     # noqa: E402

MARKERS = ["loaded", "new_game"]


def make_run(root: Path, target: str) -> Path:
    (root / "shots").mkdir(parents=True)
    (root / "run.json").write_text(json.dumps({"target": target}), encoding="utf-8")
    for m in MARKERS:
        subprocess.run(["magick", "-size", "1024x768", "xc:black", str(root / "shots" / f"{m}.png")],
                       check=True)
    return root


def status_of(a_target: str, b_target: str) -> dict[str, str]:
    with tempfile.TemporaryDirectory(prefix="ftreportonly") as tmp:
        t = Path(tmp)
        a, b = make_run(t / "a", a_target), make_run(t / "b", b_target)
        rep = compare_runs(a, b, t / "out", region="upper", align=0)
        md = (t / "out" / "report.md").read_text(encoding="utf-8")
        got = {r["marker"]: r["status"] for r in rep["markers"]}
        for m, s in got.items():
            if f"| {m} |" not in md or s not in md:
                raise SystemExit(f"FAIL report.md does not show status {s!r} of {m}")
        return got


def main() -> int:
    want = {
        ("wine-pirates-audio", "native-pirates-audio"): {"loaded": "reported only", "new_game": "required"},
        ("wine-pirates", "native-pirates"): {"loaded": "required", "new_game": "required"},
        ("wine-dragon-audio", "native-dragon-audio"): {"loaded": "required", "new_game": "required"},
    }
    fails = 0
    for (ta, tb), exp in want.items():
        try:
            got = status_of(ta, tb)
        except (KeyError, SystemExit) as exc:
            got = {"error": repr(exc)}
        ok = got == exp
        fails += not ok
        print("PASS" if ok else "FAIL", ta, tb, got)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
