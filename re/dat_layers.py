#!/usr/bin/env python
"""Collision layers and per-type part attributes of a Full Tilt / 3D Pinball table .DAT.

A ball only collides with edges whose collision group shares a bit with its CollisionMask.
Edge groups come from attribute 602 in a part's visual states (loader::query_visual: each
602 value n ORs in 1 << n; no 602 at all means group 1). Layer-switching parts (TRamp 1021,
TSink 1017, Full Tilt type 1035, ...) name layers by bit number in their float attributes
(1301/1302/1303, first float after the id). This tool lists both views, so a layer bit can be
tied to the named parts that live on it. Reads the .DAT only; prints names and numbers.

usage:
  dat_layers.py layers DAT            for every layer bit: parts whose visual states set it
                                      (602), then parts whose float attrs 1301-1303 name it
  dat_layers.py parts  DAT TYPE       every table_objects part of TYPE: group, name, and each
                                      float attribute (id + values) of its state-0 group

example:
  re/work/.venv/bin/python re/dat_layers.py parts  .../DRAGON/DRAGON.DAT 1035
  re/work/.venv/bin/python re/dat_layers.py layers .../PIRATE/PIRATES.DAT
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from datfile import Dat, table_objects  # noqa: E402

LAYER_FLOAT_IDS = (1301, 1302, 1303)
# float attrs naming more than one layer: (part type, attr id) -> value indexes after the id
# (type 1035 attr 1301 = [layerA, layerB, 2.0, x0, y0, x1, y1, unused], ctor 0x41b4f0)
MULTI_LAYER = {(1035, 1301): (0, 1)}
PAIR_IDS = {100, 300, 304, 400, 406, 602, 1100, 1101}  # loader::query_visual short-array ids


def visual_states(d, g):
    """loader::query_visual_states: shorts[1] if the first short array starts with 100, else 1."""
    for s in d.shorts(g):
        if s and s[0] == 100:
            return s[1]
    return 1


def collision_bits(d, g):
    """Bits ORed in by attribute 602 over all visual states (state 0 = g, state k = g + k)."""
    bits = set()
    for k in range(visual_states(d, g)):
        state = g + k
        if state >= len(d.groups):
            break
        for s in d.shorts(state):
            i = 0
            while i + 1 < len(s):  # same walk as query_visual; stops on an id it does not know
                if s[i] == 1500:
                    i += 9
                    continue
                if s[i] not in PAIR_IDS:
                    break
                if s[i] == 602:
                    bits.add(s[i + 1])
                i += 2
    return bits


def label(d, ptype, g):
    return f"{ptype}:{d.name(g) or g}"


def cmd_layers(path):
    d = Dat(path)
    by_602 = collections.defaultdict(list)
    by_attr = collections.defaultdict(list)
    for ptype, g in table_objects(d):
        for b in collision_bits(d, g):
            by_602[b].append(label(d, ptype, g))
        for f in d.floats(g):
            if f and int(f[0]) in LAYER_FLOAT_IDS and len(f) > 1:
                for k in MULTI_LAYER.get((ptype, int(f[0])), (0,)):
                    by_attr[int(f[1 + k])].append(f"{label(d, ptype, g)}[{int(f[0])}.{k}]")
    for b in sorted(set(by_602) | set(by_attr)):
        print(f"layer bit {b} (mask {1 << b:#x})")
        print(f"  602 ({len(by_602[b])}): {' '.join(by_602[b]) or '-'}")
        print(f"  float attrs ({len(by_attr[b])}): {' '.join(by_attr[b]) or '-'}")


def cmd_parts(path, ptype):
    d = Dat(path)
    for t, g in table_objects(d):
        if t != ptype:
            continue
        print(f"group {g} {d.name(g)!r} states {visual_states(d, g)} 602 bits "
              f"{sorted(collision_bits(d, g)) or 'none (group 1)'}")
        for f in d.floats(g):
            print(f"  attr {int(f[0])}: {[round(x, 4) for x in f[1:]]}")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "layers":
        cmd_layers(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "parts":
        cmd_parts(sys.argv[2], int(sys.argv[3]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
