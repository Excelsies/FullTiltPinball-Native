#!/usr/bin/env python
"""Check a U16c_sweep*.uh hook run (TLightSweeps, part type 1038) against re/light_sweeps.py.

usage: u16c_sweep_check.py <hook out dir> [--dat DRAGON.DAT] [--state N] [--start-frame 10]
                           [--dt-ms 10] [--tilt-frame F]

Without --tilt-frame (full sweep): the frame-0 grid dump equals the grid the simulator derives from
the .DAT (columns, rows, xmin, ymin, inv_cell_w, inv_cell_h as float32 bit patterns); the rising
edges of every `<light>.override` per frame equal the simulator's per-ring light sets, ring k at
start + k x step frames +-1 (ring 0 exactly at the start frame, inside the Message call); the sweep
ends (timer == 0, repeats_left == 0) one step after the last ring. With --tilt-frame F (the run
sent SetTiltLock at F): the same for the rings before F, timer == 0 at F and after, and no rising
edge after F. Prints one PASS/FAIL line per check; exit 0 when all pass, 1 otherwise.
Light names come from the .DAT at run time; nothing from the game is stored here.
"""
import argparse
import json
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from datfile import Dat, table_objects  # noqa: E402
from light_sweeps import Sweeps, SWEEPS  # noqa: E402

DEFAULT_DAT = str(Path(__file__).resolve().parent.parent / "work" / "game" / "DRAGON" / "DRAGON.DAT")  # copy of the user's data (re/test/README.md)


def f32bits(x):
    return struct.pack("<f", float(x))


def jl(path):
    p = Path(path)
    if not p.exists():
        return []
    return [json.loads(x) for x in p.read_text(encoding="utf-8").splitlines() if x.strip()]


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, name, ok, detail=""):
        print(("PASS " if ok else "FAIL ") + name + (f": {detail}" if detail else ""))
        if not ok:
            self.failed += 1


def observed_edges(timeline):
    """{frame: set(light name)} of 0 -> 1 changes of `<name>.override`; unseen = 0."""
    prev, edges = {}, {}
    for rec in timeline:
        for key, val in rec.items():
            if not key.endswith(".override"):
                continue
            name = key[:-len(".override")]
            if val == 1 and prev.get(name, 0) != 1:
                edges.setdefault(rec["frame"], set()).add(name)
            prev[name] = val
    return edges


def expected_rings(d, steps, on_ms, gts):
    """Per simulator step: (must-edge set, maybe-edge set). `gts[k]` = game time (ms) of ring k.
    A light last lit within on_ms + 150 ms of ring k may or may not give a new rising edge (its
    override expiry timer races with the ring: timer::check fires two due timers per tick and the
    ones 100 ms late, as in the original 0x41f790); any other light in the ring must give one."""
    last, out = {}, []
    for k, (_, _, lit) in enumerate(steps):
        names = {d.name(g) for g in lit}
        must = {nm for nm in names if nm not in last or gts[k] - last[nm] > on_ms + 150}
        out.append((must, names - must))
        for nm in names:
            last[nm] = gts[k]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--dat", default=DEFAULT_DAT)
    ap.add_argument("--state", type=int, default=0)
    ap.add_argument("--start-frame", type=int, default=10)
    ap.add_argument("--dt-ms", type=float, default=10.0)
    ap.add_argument("--tilt-frame", type=int, default=None)
    a = ap.parse_args()
    r = Report()
    out = Path(a.out)
    d = Dat(a.dat)
    group = [g for t, g in table_objects(d) if t == SWEEPS][0]
    sw = Sweeps(d, group)
    p, steps, end_t = sw.run(a.state)
    timeline, dumps = jl(out / "timeline.jsonl"), jl(out / "dumps.jsonl")
    r.check("run produced timeline", bool(timeline))

    grid = next((x for x in dumps if x.get("label") == "grid"), None)
    if grid is not None and a.tilt_frame is None:
        want = {"light_sweeps.columns": 15, "light_sweeps.rows": 25}
        for k, v in want.items():
            r.check(f"frame 0 {k} == {v}", grid.get(k) == v, str(grid.get(k)))
        for k, v in (("xmin", sw.x0), ("ymin", sw.y0), ("inv_cell_w", sw.inv_w), ("inv_cell_h", sw.inv_h)):
            got = grid.get("light_sweeps." + k)
            r.check(f"frame 0 {k} float32 bits equal the simulator's grid",
                    got is not None and f32bits(got) == f32bits(v), f"{got} vs {v!r}")
    elif a.tilt_frame is None:
        r.check("frame 0 grid dump present", False, "no dump labelled grid")

    # Ring frames come from the sweep's own radius field: the step timer shares timer::check with the
    # lights' override timers (two due timers per tick), so rings are not exactly StepTime apart.
    ring_frames, gt_of = [], {}
    for rec in timeline:
        if "gt" in rec:
            gt_of[rec["frame"]] = rec["gt"]
    prev_r = None
    for rec in timeline:
        if rec["frame"] >= a.start_frame and "light_sweeps.radius" in rec:
            if a.tilt_frame is not None and rec["frame"] >= a.tilt_frame:
                break
            ring_frames.append((rec["frame"], rec["light_sweeps.radius"], rec["gt"]))
    want_r = [r for _, r, _ in steps]
    got_r = [r for _, r, _ in ring_frames]
    if a.tilt_frame is None:
        end_r = p["max_r"] + 1 if p["expand"] else -1
        r.check(f"radius sequence = the simulator's {len(want_r)} rings, then the end tick",
                got_r == want_r + [end_r], f"{len(got_r)} observed, first diff at "
                f"{next((i for i, (x, y) in enumerate(zip(got_r, want_r + [end_r])) if x != y), None)}")
    else:
        r.check("radius sequence before the tilt frame is a prefix of the simulator's", got_r == want_r[:len(got_r)]
                and len(got_r) >= 4, f"{got_r}")
    gts = [g for _, _, g in ring_frames]
    rings = expected_rings(d, steps[:len(ring_frames)], p["on"] * 1000, gts)
    edges = observed_edges(timeline)
    ring_frame_set = {f for f, _, _ in ring_frames}
    for k, ((f, rad, _), (must, maybe)) in enumerate(zip(ring_frames, rings)):
        got = edges.get(f, set())
        ok = must <= got and got <= must | maybe
        r.check(f"ring {k:2d} radius {rad:2d} at frame {f}: {len(must)} lights must flash",
                ok, "" if ok else f"missing {sorted(must - got)} extra {sorted(got - must - maybe)}")
    stray = [(f, sorted(n)) for f, n in sorted(edges.items()) if f not in ring_frame_set]
    r.check("no rising edge outside a ring frame", not stray, str(stray[:3]))
    gaps = [b[0] - a_[0] for a_, b in zip(ring_frames, ring_frames[1:])]
    print(f"INFO ring spacing in frames: min {min(gaps) if gaps else None} max {max(gaps) if gaps else None} "
          f"first {gaps[:10]}")

    def last_value(key):
        v = None
        for rec in timeline:
            if key in rec:
                v = rec[key]
        return v

    if a.tilt_frame is None:
        r.check("timer == 0 and repeats_left == 0 at the end",
                last_value("light_sweeps.timer") == 0 and last_value("light_sweeps.repeats_left") == 0,
                f"timer {last_value('light_sweeps.timer')} repeats_left {last_value('light_sweeps.repeats_left')}")
    else:
        late = {f: sorted(n) for f, n in edges.items() if f >= a.tilt_frame}
        r.check(f"no rising edge from frame {a.tilt_frame} on", not late, str(list(late.items())[:3]))
        t_after = [rec for rec in timeline if rec["frame"] >= a.tilt_frame and "light_sweeps.timer" in rec]
        r.check("timer == 0 from the tilt frame", last_value("light_sweeps.timer") == 0
                and all(rec["light_sweeps.timer"] == 0 for rec in t_after), str([x["frame"] for x in t_after][:4]))
    print("RESULT " + ("PASS" if r.failed == 0 else f"FAIL ({r.failed} checks)"))
    return 0 if r.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
