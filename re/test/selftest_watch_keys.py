"""Self-test of the Wine watch files.
Run: python re/test/selftest_watch_keys.py
Every key the hook dumps natively that has a Wine counterpart in the DRAGON watch file must be read
at the same table offset in the PIRATES watch file (same engine build, same TPinballTable layout),
and PIRATES must watch super_jackpot at table offset 0x17a (the original resets it to 20000 with
jackpot at NewGame, so compare-wine can assess it). Exit 0 = pass, 1 = fail.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent / "scripts"
SHARED_TABLE_KEYS = ["player_count", "current_player", "multiplier", "jackpot", "ball_count",
                     "extra_balls", "multiball", "tilt_lock"]


def table_keys(name: str) -> dict[str, str]:
    data = json.loads((SCRIPTS / name).read_text(encoding="utf-8"))
    return {v["name"]: v["table"] for v in data["values"] if "table" in v}


def main() -> int:
    dragon, pirates = table_keys("watch_dragon.json"), table_keys("watch_pirates.json")
    fails = []
    for k in SHARED_TABLE_KEYS:
        if dragon.get(k) is None or dragon.get(k) != pirates.get(k):
            fails.append(f"{k}: dragon {dragon.get(k)} pirates {pirates.get(k)}")
    if pirates.get("super_jackpot") != "0x17a":
        fails.append(f"super_jackpot: pirates {pirates.get('super_jackpot')}, want 0x17a")
    for f in fails:
        print("FAIL", f)
    print("PASS" if not fails else f"{len(fails)} FAIL")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
