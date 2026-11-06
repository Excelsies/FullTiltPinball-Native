#!/usr/bin/env python
"""Simulate Full Tilt part type 1038 (the table's "light_sweeps" component) on a table .DAT.

Reimplements the original engine's behaviour (DRAGON.EXE ctor 0x40c210, Message 0x40c660,
step 0x40c5b0, ring 0x40c110, cell 0x40c0a0; same code in CADET/PIRATES) from our spec
re/work/spec/part_1038.md, using only numbers read from the .DAT given on the command line:

* binning: every TLight (part type 1002) with attribute 501 (x, y) goes into a 15 x 25 cell grid
  laid over the table layer's bounds (min/max of the first three points of the "table" group's
  wall array, attribute 600);
* a sweep (Message 27 with value = state index) reads attributes 1600-1604 of that state of the
  light_sweeps group and lights expanding or contracting rings of cells around a centre cell.

Use it to check a port: the grid cells, the ring order and the per-step light lists must match.

usage:
  light_sweeps.py DAT                 grid summary and one line per sweep state
  light_sweeps.py DAT --grid          also print the 15 x 25 occupancy map (light count per cell)
  light_sweeps.py DAT --steps [N...]  also list, per timer step, the radius and the lights lit
                                      (all states, or only the given state indices)
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from datfile import Dat, table_objects  # noqa: E402

COLS, ROWS = 15, 25                      # 1038 ctor: +0x5a = 15, +0x5e = 25
LIGHT, SWEEPS = 1002, 1038


def f32(x):
    """Round to IEEE single precision (the original stores these values as float)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def float_attr(d, group, offset, attr_id):
    """loader::query_float_attribute (0x40d8d0): floats after the id in state `offset`, or None."""
    g = group + offset
    for arr in d.floats(g):
        if arr and int(math.floor(arr[0])) == attr_id:
            return arr[1:]
    return None


def visual_states(d, group):
    """loader::query_visual_states (0x40d110): attr 100 of the ShortArray, else 1."""
    s = d.shorts(group)
    return s[0][1] if s and s[0] and s[0][0] == 100 else 1


def table_bounds(d):
    """TTableLayer XMin, YMin, XMax, YMax (original ctor 0x418f90 / upstream TTableLayer ctor)."""
    g = d.lookup("table")
    for arr in d.floats(g):
        if arr and arr[0] == 600.0:
            p = arr[2:8]
            xs, ys = p[0::2], p[1::2]
            return min(xs), min(ys), max(xs), max(ys)
    raise SystemExit(f"{d.path}: 'table' group has no attribute 600 wall array")


class Sweeps:
    def __init__(self, d, group):
        self.d, self.group = d, group
        xmin, ymin, xmax, ymax = table_bounds(d)
        self.x0, self.y0 = xmin, ymin
        self.cell_w = f32(f32(xmax - xmin) / 15.0)
        self.cell_h = f32(f32(ymax - ymin) / 25.0)
        self.inv_w = f32(1.0 / self.cell_w)
        self.inv_h = f32(1.0 / self.cell_h)
        self.grid = [[[] for _ in range(COLS)] for _ in range(ROWS)]
        self.skipped = []
        lights = [g for t, g in table_objects(d) if t == LIGHT]
        for g in reversed(lights):       # ctor walks the master light group from Count-1 to 0
            pos = float_attr(d, g, 0, 501)
            if pos is None:
                self.skipped.append(g)
                continue
            col, row = self.cell(pos[0], pos[1])
            self.grid[row][col].append(g)

    @staticmethod
    def clamp_floor(v, hi):
        """t = min(floor(v), hi); t <= 0 -> 0 (the inlined macro in 0x40c210 / 0x40c660)."""
        t = min(math.floor(v), hi)
        return 0 if t <= 0 else int(t)

    def cell(self, x, y):
        col = self.clamp_floor((x - self.x0) * self.inv_w, COLS - 1)
        row = self.clamp_floor((y - self.y0) * self.inv_h, ROWS - 1)
        return col, row

    def lights_at(self, col, row):
        """0x40c0a0: bounds-checked cell -> lights that get Message(24, LightOnTime)."""
        if 0 <= col < COLS and 0 <= row < ROWS:
            return list(reversed(self.grid[row][col]))   # sent from Count-1 down to 0
        return []

    @staticmethod
    def ring(cx, cy, r):
        """0x40c110: midpoint-circle ring of radius r; yields cells in the original call order."""
        i, c, lim = 0, r * r, (r - 1) * r
        while True:
            yield from ((cx + i, cy - r), (cx - i, cy - r), (cx + i, cy + r), (cx - i, cy + r),
                        (cx + r, cy - i), (cx - r, cy - i), (cx + r, cy + i), (cx - r, cy + i))
            i += 1
            c += -2 * i + 1
            if c <= lim:
                r2 = r * 2
                r -= 1
                lim -= r2 - 2
            if not i <= r:
                return

    def params(self, state):
        d, g = self.d, self.group
        step = float_attr(d, g, state, 1602)
        step = step[0] if step else f32(0.1)
        on = float_attr(d, g, state, 1604)
        on = on[0] if on else step
        centre = float_attr(d, g, state, 1600)
        cx, cy = self.cell(centre[0], centre[1]) if centre else (0, 0)
        expand = float_attr(d, g, state, 1601)
        expand = int(expand[0]) if expand else 0
        repeats = float_attr(d, g, state, 1603)
        repeats = int(repeats[0]) if repeats else 1
        a2, b2 = cx * cx, cy * cy
        c2, d2 = (COLS - 1 - cx) ** 2, (ROWS - 1 - cy) ** 2
        max_r = int(math.sqrt(f32(max(a2 + b2, c2 + d2, c2 + b2, a2 + d2))))
        return dict(step=step, on=on, centre=centre, cx=cx, cy=cy, expand=expand,
                    repeats=repeats, max_r=max_r)

    def run(self, state, max_steps=10000):
        """Message(27, state) then every timer tick (0x40c5b0). Returns (params, steps, end) where
        steps = [(time, radius, [light groups])] and end = time of the last tick, which draws
        nothing and sends ControlTimerExpired."""
        p = self.params(state)
        radius = 0 if p["expand"] else p["max_r"]
        repeats = p["repeats"]
        steps, t = [], 0.0
        for _ in range(max_steps):
            done = False
            if p["expand"]:
                radius += 1
                done = radius > p["max_r"]
            else:
                radius -= 1
                done = radius < 0
            if done:
                repeats -= 1
                if repeats <= 0:
                    return p, steps, t
                radius = 0 if p["expand"] else p["max_r"]
            lit = []
            for col, row in self.ring(p["cx"], p["cy"], radius):
                lit.extend(self.lights_at(col, row))
            steps.append((t, radius, lit))
            t += p["step"]
        raise SystemExit(f"state {state}: no end after {max_steps} steps")


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        raise SystemExit(__doc__)
    d = Dat(args[0])
    show_grid = "--grid" in args
    only = None
    if "--steps" in args:
        k = args.index("--steps")
        only = [int(a) for a in args[k + 1:] if a.lstrip("-").isdigit()] or "all"
    groups = [g for t, g in table_objects(d) if t == SWEEPS]
    if not groups:
        raise SystemExit(f"{d.path}: no part of type 1038 in table_objects")
    for g in groups:
        s = Sweeps(d, g)
        n_lights = sum(len(c) for row in s.grid for c in row)
        print(f"group {g} {d.name(g)!r}: table bounds x0 {s.x0:.4f} y0 {s.y0:.4f}, cell "
              f"{s.cell_w:.4f} x {s.cell_h:.4f}; {n_lights} lights binned, "
              f"{len(s.skipped)} without attribute 501 {[d.name(x) for x in s.skipped]}")
        if show_grid:
            for row in range(ROWS):
                print("  " + " ".join(f"{len(s.grid[row][col]) or '.':>2}" for col in range(COLS)))
        n_states = visual_states(d, g)
        state = 0
        while state < n_states and (state == 0 or d.record(g + state) == 201):
            p, steps, end = s.run(state)
            print(f"  state {state:2}: centre {p['centre'][:2] if p['centre'] else None} -> cell "
                  f"({p['cx']},{p['cy']}) {'expand' if p['expand'] else 'contract'} x{p['repeats']} "
                  f"step {p['step']:.3f}s on {p['on']:.3f}s max radius {p['max_r']}: "
                  f"{len(steps)} steps, ControlTimerExpired at {end:.2f}s, "
                  f"{len({x for _, _, lit in steps for x in lit})} distinct lights")
            if only == "all" or (only and state in only):
                for t, r, lit in steps:
                    print(f"    t {t:5.2f} r {r:2}: {' '.join(d.name(x) or str(x) for x in lit)}")
            state += 1
        print(f"  (attribute 100 says {n_states} visual states; {state} state groups follow)")


if __name__ == "__main__":
    main()
