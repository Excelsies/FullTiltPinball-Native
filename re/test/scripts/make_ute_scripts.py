"""Generate the Wine diagnosis scripts: start-up samples, the PIRATES high-score trace and the
pre-hook double-count timelines. Addresses and field offsets only (no game data). Rerunnable, output is deterministic.

  python re/test/scripts/make_ute_scripts.py [--play-flip <play_flip.json>]

Writes next to this file: dl2_startup_wine.json, watch_pirates_p11.json, p11_gameover_wine.json (needs --play-flip, the
playtest script that plays one PIRATES game to game over), watch_dragon_ute.json / watch_pirates_ute.json.
PIRATES globals: time_ticks 0x43a740 (PIRATES.EXE.c 1042), info_text_box tag 0x43ad38 (component pointer at tag+4):
+0x5e queue head, node +4 text pointer, node +8 time, node +0xc EndTick (part_ttimer_ttextbox.md B); +0x66 overlay
text, +0x12e overlay state; mode_timer tag from control_tables_pirates.json +0x42 Timer id.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def chain(name: str, spec: str) -> dict:
    """A raw ftpeek chain (hex numbers joined by ':'), carried as an 'addr'-less value: probe_run.peek_specs only
    knows tag/table/addr, so chains are written as 'chain'."""
    return {"name": name, "chain": spec}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--play-flip", default=None)
    args = ap.parse_args()

    # Start-up: the start-up show (jackpot 20,000) is on screen from the window up to NewGame at script time 2.0 s
    # (anchor window). Five pre-NewGame peeks, then two after.
    steps = []
    t = 0.0
    for lab, at in (("pre_0_5", 0.5), ("pre_0_8", 0.8), ("pre_1_1", 1.1), ("pre_1_4", 1.4), ("pre_1_7", 1.7),
                    ("post_2_4", 2.4), ("post_3_0", 3.0)):
        steps += [{"wait": round(at - t, 3)}, {"peek": lab}]
        t = at
    (HERE / "dl2_startup_wine.json").write_text(json.dumps(
        {"name": "dl2_startup_wine", "description": "D-L2 Wine half: peeks before and after NewGame (run with "
         "--anchor window --window-poll 0.02).", "steps": steps}, indent=1) + "\n")

    # P-11: PIRATES watch + text box / timer state.
    w = json.loads((HERE / "watch_pirates.json").read_text())
    ib = 0x43ad38 + 4
    extra = [chain("time_ticks", "43a740"),
             chain("tb_mode", f"{ib:x}:6"),
             chain("tb_head", f"{ib:x}:5e"),
             chain("tb_head_time", f"{ib:x}:5e:8"),
             chain("tb_head_end", f"{ib:x}:5e:c"),
             chain("tb_text0", f"{ib:x}:5e:4:0"),
             chain("tb_text1", f"{ib:x}:5e:4:4"),
             chain("tb_text2", f"{ib:x}:5e:4:8"),
             chain("tb_overlay_state", f"{ib:x}:12e"),
             chain("tb_overlay0", f"{ib:x}:66"),
             chain("tb_timer_id", f"{ib:x}:13a")]
    w["values"] = [v for v in w["values"] if v["name"] in ("mode", "ball_count", "last_ball", "player_count",
                                                            "jackpot", "tilt_lock")] + extra
    w["_comment"] = "UTe P-11 trace: PIRATES info_text_box state and the global tick counter (see make_ute_scripts.py)"
    (HERE / "watch_pirates_p11.json").write_text(json.dumps(w, indent=1) + "\n")

    # Pre-hook double count: the original's control dispatcher (DRAGON 0x423060, PIRATES 0x423080:
    # MissionControl pre-hook, then the part's control function) run with ControlCollision 0x4a for the parts of the
    # native d_prehook.uh / p_prehook.uh, one call at a time, with the jackpot, targets_hit and the last scorer
    # (DRAGON 0x434d20, PIRATES 0x435d44) read 0.4 s after each call. ftcall.exe (ftcall.c) makes the calls.
    for table, func, scorer, parts, seq in (
            ("dragon", "423060", "434d20", ("a_spot4", "a_sink1"),
             ["a_spot4", "a_spot4", "a_spot4", "a_sink1", "a_sink1"]),
            ("pirates", "423080", "435d44", ("a_spot102", "a_drop5", "a_drop10"),
             ["a_spot102", "a_drop5", "a_drop10"])):
        tags = {e["name"]: e["addr"] for e in json.loads(
            (HERE.parent.parent / "work" / "spec" / f"control_tables_{table}.json").read_text())["tags"]}
        parts = {n: tags[n] for n in parts}                 # tag addresses of the parts (component pointer at tag+4)
        w = json.loads((HERE / f"watch_{table}.json").read_text())
        w["values"] = [v for v in w["values"] if v["name"] in ("mode", "selected_quest", "saved_quest", "ball_count", "targets_hit", "jackpot",
                                                               "super_jackpot")]
        w["values"].append({"name": "last_scorer", "addr": "0x" + scorer})
        w["_comment"] = "UTe pre-hook trace: jackpot, targets_hit and the last scorer pointer (make_ute_scripts.py)"
        (HERE / f"watch_{table}_ute.json").write_text(json.dumps(w, indent=1) + "\n")
        steps = [{"wait": 4.0}, {"peek": "base"}]
        for i, name in enumerate(seq):
            steps += [{"call": f"{i}_{name}", "func": func, "code": "4a", "part": f"{parts[name] + 4:x}"},
                      {"wait": 0.4}, {"peek": f"{i}_{name}"}]
        (HERE / f"prehook_{table}_wine.json").write_text(json.dumps(
            {"name": f"prehook_{table}_wine", "description": "UTe: pre-hook double count on the original (probe_run.py "
             "--anchor window --watch watch_<t>_ute.json)", "steps": steps}, indent=1) + "\n")

    if args.play_flip:
        s = json.loads(Path(args.play_flip).read_text())
        steps = []
        taps = [i for i, st in enumerate(s["steps"]) if "tap" in st]
        fine = set(taps[::8])                       # game over lands anywhere in the play phase: a list about every 1.2 s
        for i, st in enumerate(s["steps"]):         # a window list before every shot of the play phase
            if "shot" in st:
                steps.append({"windows": "play_" + st["shot"]})
            elif i in fine:
                steps.append({"windows": f"play_t{i:04d}"})
            steps.append(st)
        for n in range(1, 31):                      # 30 s after the last play step, once per second
            steps += [{"wait": 1.0}, {"windows": f"over_{n:02d}"}, {"peek": f"over_{n:02d}"}]
            if n in (1, 4, 10, 29):
                steps.append({"rootshot": f"over_{n:02d}"})
            if n in (1, 10, 29):                    # direct one-shot read (a fresh ftpeek), independent of the sampler
                steps.append({"restpeek": f"over_{n:02d}", "watch": "watch_pirates_p11.json"})
        s["steps"] = steps
        s["name"] = "p11_gameover_wine"
        (HERE / "p11_gameover_wine.json").write_text(json.dumps(s, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
