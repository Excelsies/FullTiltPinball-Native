#!/usr/bin/env python3
"""Mode-select checks that the hook `expect` grammar cannot express (text fields, the draw formula). No game text: the glyphs are the
code constants of the listing (StartMode4 'H', StartMode8 'I', StartMode5 'J', StartMode7 '333').

  check_pm.py target <run>    every mode-select target text (ids 3068..3075): time 2.0, font 0, not low priority; 10 of them
  check_pm.py start <run>     the glyph texts of the Start<Mode> functions (listing 508-617), one run directory of pm_start.uh;
                              the time -1 / font 0 hint displays of other callers are asserted by resource id (HINT_IDS)
Exit 0 = PASS, 1 = FAIL.
"""
import json
import sys
from pathlib import Path

MODE = [4, 8, 7, 5, 6, 9, 10, 11]


def lines(p):
    return [json.loads(x) for x in p.read_text().splitlines() if x.strip()]


def target(run):
    rows = [r for r in lines(run / "text.jsonl") if r["box"] == "info_text_box" and r["op"] == "display"
            and any(3068 <= i <= 3075 for i in r["ids"])]
    bad = [r for r in rows if not (r["time"] == 2.0 and r["font"] == 0 and r["low"] is False)]
    ok = len(rows) == 10 and not bad
    print("%s target texts: %d records (want 10), %d with wrong time/font/low" % ("PASS" if ok else "FAIL", len(rows), len(bad)))
    return ok


# Hint displays of the real callers that also land in a Start<Mode> window: info_text_box->Display(id, -1.0f, 0, false), i.e.
# time -1 / font 0 (the glyphs are time 1 or 2 / font 1). Asserted by (time, font) signature and resource id per index, never by
# text and never by widening the window. Each id is tied to the caller that shows it, in the ControlMissionComplete handler
# of the started mode (StartMode -> MissionControl) unless noted:
#   index 1 (mode 4):  3060 pirates_modes_a.cpp:218 (Mode4Controller);
#                      3076 pirates_ball.cpp:216 (Mode2IdleController ControlMissionStarted)
#   index 2 (mode 8):  3061 pirates_treasure.cpp:329 (Mode8Controller)
#   index 3 (mode 7):  3062 pirates_modes_b.cpp:271 (Mode7Controller)
#   index 4 (mode 5):  3063 pirates_modes_a.cpp:293 (Mode5Controller)
#   index 5 (mode 6):  3067 pirates_modes_a.cpp:418 (Mode6Controller)
#   index 6 (mode 9):  3064 pirates_modes_b.cpp:317 (Mode9Controller)
#   index 7 (mode 10): 3065 pirates_modes_b.cpp:361 (Mode10Controller)
#   index 8 (mode 11): 3066 pirates_treasure.cpp:407 (Mode11Controller)
# Oracle: a build without those callers shows no hint and FAILS here; an index must show exactly its
# listed ids, in order (a missing, extra or reordered hint fails).
HINT_TIME, HINT_FONT = -1, 0
HINT_IDS = {1: [3076, 3060], 2: [3061], 3: [3062], 4: [3063], 5: [3067], 6: [3064], 7: [3065], 8: [3066]}


def start(run):
    glyph = {1: ("H", 1.0), 2: ("I", 2.0), 3: ("333", 2.0), 4: ("J", 2.0)}
    text = [r for r in lines(run / "text.jsonl") if r["box"] == "info_text_box" and r["op"] == "display"]
    ok = True
    for k in range(1, 9):
        f = 200 + 40 * (k - 1)
        win = [r for r in text if f + 1 <= r["frame"] <= f + 12]
        hints = [r for r in win if r["time"] == HINT_TIME and r["font"] == HINT_FONT]
        got = [(r["text"], r["time"], r["font"], r["low"]) for r in win if r not in hints]
        want = [(glyph[k][0], glyph[k][1], 1, False)] if k in glyph else []
        hint_ids = [r["ids"] for r in hints]
        want_ids = [[i] for i in HINT_IDS.get(k, [])]
        good = got == want and hint_ids == want_ids and not any(r["low"] for r in hints)
        ok &= good
        print("%s start index %d: glyph texts %r (want %r), hint ids %r (want %r)"
              % ("PASS" if good else "FAIL", k, got, want, hint_ids, want_ids))
    return ok


if __name__ == "__main__":
    fn = {"target": target, "start": start}[sys.argv[1]]
    sys.exit(0 if fn(Path(sys.argv[2])) else 1)
