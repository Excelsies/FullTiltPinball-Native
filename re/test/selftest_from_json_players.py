"""Self-test of hookrun.py from-json, one player then two by menu. Conversion only, no display.
Run: python re/test/selftest_from_json_players.py        Exit 0 = pass, 1 = fail.
"""
from __future__ import annotations

import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hookrun  # noqa: E402

SCRIPT = {"steps": [
    {"wait": 3.0}, {"tap": "alt+o"}, {"wait": 0.7}, {"tap": "p"}, {"wait": 0.7}, {"tap": "2"}, {"wait": 0.7},
    {"tap": "new_game"}, {"wait": 2.0}, {"peek": "after"}]}
FAILS: list[str] = []


def convert(*extra: str) -> list[str]:
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "s.json"
        p.write_text(json.dumps(SCRIPT), encoding="utf-8")
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = hookrun.main(["from-json", str(p), "--fixed-dt", "10", *extra])
        if rc != 0:
            FAILS.append(f"from-json {extra} rc={rc}")
        return buf.getvalue().splitlines()


def check(name: str, ok: bool, got) -> None:
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"  got {got}"))
    if not ok:
        FAILS.append(name)


default = convert()
check("default keeps D18 header", "init option Players 2" in default, default)
check("default has no startup_wait", "init startup_wait 1" not in default, default)
check("default F2 stays a key", any(x.endswith("key down new_game") for x in default), default)

late = convert("--late-players", "--startup-wait")
check("late: no option header", not any(x.startswith("init option") for x in late), late)
check("late: startup_wait header", "init startup_wait 1" in late, late)
# taps at 3.0 (alt+o) +0.08+0.7 = 3.78 (p) +0.08+0.7 = 4.56 ('2')   -> frame 456
check("late: menu 2 -> msg NewGame 2 at frame 456", "456 msg table NewGame 2" in late, late)

check("late: later F2 becomes msg NewGame 2 (a key would start Options.Players = 1)",
      any(x.endswith("msg table NewGame 2") and not x.startswith("456 ") for x in late)
      and not any("key" in x and "new_game" in x for x in late), late)

plain = {"steps": [{"wait": 1.0}, {"tap": "F2"}, {"wait": 1.0}]}
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "s.json"
    p.write_text(json.dumps(plain), encoding="utf-8")
    buf = io.StringIO()
    with redirect_stdout(buf):
        hookrun.main(["from-json", str(p), "--fixed-dt", "10", "--late-players"])
    out = buf.getvalue().splitlines()
check("late: F2 without a menu choice stays a key", "100 key down new_game" in out, out)
sys.exit(1 if FAILS else 0)
