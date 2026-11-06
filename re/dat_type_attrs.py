#!/usr/bin/env python
"""Dump every float attribute of every part of the given part types, per table and per state.

A part is the head group named in table_objects plus the state groups (record 201) that follow it.
Prints part name, state index, attribute id and values. Reads the .DAT files only; output is game
data, so redirect it under re/work/ (never commit it).

usage: dat_type_attrs.py FULLTILT_DIR TYPE [TYPE ...] [--attrs 407,408,...]
example:
  re/work/.venv/bin/python re/dat_type_attrs.py "$FULLTILT_DIR" 1022 \\
      --attrs 407,408,701,703,1304,1306
"""
import os
import sys

from datfile import Dat, table_objects

TABLES = {"cadet": "CADET/CADET.DAT", "dragon": "DRAGON/DRAGON.DAT", "pirates": "PIRATE/PIRATES.DAT"}
STATE = 201


def part_groups(d, head):
    """Head group index followed by its state groups."""
    out = [head]
    i = head + 1
    while i < len(d.groups) and d.record(i) == STATE:
        out.append(i)
        i += 1
    return out


def main():
    args = sys.argv[1:]
    only = None
    if "--attrs" in args:
        k = args.index("--attrs")
        only = {int(a) for a in args[k + 1].split(",")}
        del args[k:k + 2]
    if len(args) < 2:
        raise SystemExit(__doc__)
    root, types = args[0], {int(t) for t in args[1:]}
    for table, rel in TABLES.items():
        d = Dat(os.path.join(root, rel))
        for ptype, group in table_objects(d):
            if ptype not in types:
                continue
            groups = part_groups(d, group)
            print(f"{table} {ptype} {d.name(group)} (states {len(groups) - 1 if len(groups) > 1 else 1})")
            for state, g in enumerate(groups):
                for arr in d.floats(g):
                    if not arr:
                        continue
                    attr = int(arr[0])
                    if only is not None and attr not in only:
                        continue
                    vals = ", ".join(f"{v:g}" for v in arr[1:])
                    print(f"  g{g} s{state} attr {attr}: [{vals}]")


if __name__ == "__main__":
    main()
