#!/usr/bin/env python
"""Find table parts by place or by type in a Full Tilt / 3D Pinball .DAT (uses datfile.py).

A part's geometry is every float attribute of its group and of its visual-state groups
(state n > 0 is group + n and must be an A_STATE (201) record, as loader::state_id checks; the
state count is attribute 100, as loader::query_visual_states reads it). Float arrays are read as (attribute id, values...) and
every consecutive value pair is tried as an (x, y) point, so wall polygons (600), positions
(408, 501), ramp planes (1300/1301) etc. all match. This is a locating aid, not a parser of each
attribute's real layout: check hits with `dat_dump.py group`.

usage:
  dat_near.py DAT X Y [RADIUS]     parts with a point within RADIUS (default 1.0) of (X, Y),
                                   nearest first: part index, type, group, name, distance,
                                   attribute and group of the closest point
  dat_near.py DAT --type TYPE      parts of that type: index, group, name and its float attributes
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from datfile import Dat, table_objects  # noqa: E402


def state_groups(d, group):
    count = 1
    for s in d.shorts(group):
        if len(s) >= 2 and s[0] == 100:
            count = max(1, s[1])
    out = [group]
    for n in range(1, count):  # loader::state_id rejects a state group that is not A_STATE (201)
        if group + n >= len(d.groups) or d.record(group + n) != 201:
            break
        out.append(group + n)
    return out


def nearest_point(d, group, x, y):
    best = None
    for g in state_groups(d, group):
        for f in d.floats(g):
            if not f:
                continue
            vals = f[1:]
            for k in range(len(vals) - 1):
                dist = math.hypot(vals[k] - x, vals[k + 1] - y)
                if best is None or dist < best[0]:
                    best = (dist, int(f[0]), g)
    return best


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    d = Dat(sys.argv[1])
    parts = table_objects(d)
    if sys.argv[2] == "--type":
        if len(sys.argv) != 4:
            raise SystemExit(__doc__)
        want = int(sys.argv[3])
        for i, (ptype, group) in enumerate(parts):
            if ptype == want:
                print(f"part {i} type {ptype} group {group} name {d.name(group)!r}")
                for f in d.floats(group):
                    print(f"    float attr {int(f[0]) if f else '?'}: {[round(v, 4) for v in f[1:]]}")
                for s in d.shorts(group):
                    print(f"    short attrs: {s}")
        return
    if len(sys.argv) not in (4, 5):
        raise SystemExit(__doc__)
    x, y = float(sys.argv[2]), float(sys.argv[3])
    radius = float(sys.argv[4]) if len(sys.argv) == 5 else 1.0
    hits = []
    for i, (ptype, group) in enumerate(parts):
        best = nearest_point(d, group, x, y)
        if best is not None and best[0] <= radius:
            hits.append((best[0], i, ptype, group, best[1], best[2]))
    for dist, i, ptype, group, attr, g in sorted(hits):
        print(f"part {i:3d} type {ptype} group {group:4d} name {d.name(group)!r:24} "
              f"d={dist:.3f} (attr {attr} in group {g})")


if __name__ == "__main__":
    main()
