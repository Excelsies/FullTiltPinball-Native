#!/usr/bin/env python
"""Per part type: which DAT float attributes (and how many visual states) each Full Tilt table uses.

A part in a PARTOUT(4.0) .DAT is the head group named in table_objects (ShortValue 200) plus the
state groups that follow it (ShortValue 201). Each float array (field type 11) starts with its
attribute id. The engine code is the same in all three exes, so an attribute that DRAGON or
PIRATES uses for a part type but CADET never does marks an engine code path that Space Cadet does
not exercise - a likely gap in the SpaceCadetPinball decompilation, which was checked against
Space Cadet's data only.

usage: dat_attrs.py FULLTILT_DIR [TYPE ...]
  prints, per part type, the attributes only DRAGON / PIRATES use, with the part names using them,
  and the maximum number of states per table; with TYPE arguments, only those types.
example:
  re/work/.venv/bin/python re/dat_attrs.py "$FULLTILT_DIR" 1011 1022
"""
import collections
import os
import struct
import sys

from dat_parts import KNOWN, TABLES, groups, table_objects

SHORT_VALUE, GROUP_NAME, FLOAT_ARRAY = 0, 3, 11
HEAD, STATE = 200, 201


def parts(path):
    """[(type, name, {attr: count}, n_states)] for every entry of table_objects."""
    gs = list(groups(path))
    out = []
    for ptype, gi in table_objects(path):
        attrs = collections.Counter()
        name = None
        states = 0
        i = gi
        while i < len(gs):
            entries = gs[i]
            kind = next((struct.unpack("<h", b)[0] for t, b in entries if t == SHORT_VALUE), None)
            if i > gi and kind != STATE:
                break
            states += 1
            for t, b in entries:
                if t == GROUP_NAME and name is None:
                    name = b.split(b"\0")[0].decode("latin-1")
                elif t == FLOAT_ARRAY and len(b) >= 4:
                    attrs[int(struct.unpack_from("<f", b)[0])] += 1
            i += 1
        out.append((ptype, name or f"group{gi}", attrs, states))
    return out


def main():
    folder = sys.argv[1]
    only = {int(x) for x in sys.argv[2:]}
    tables = {name: parts(os.path.join(folder, rel)) for name, rel in TABLES.items()}
    used = {name: collections.defaultdict(set) for name in tables}
    users = {name: collections.defaultdict(list) for name in tables}
    states = {name: collections.Counter() for name in tables}
    for name, plist in tables.items():
        for ptype, pname, attrs, n in plist:
            states[name][ptype] = max(states[name][ptype], n)
            for a in attrs:
                used[name][ptype].add(a)
                users[name][(ptype, a)].append(pname)
    all_types = sorted(set().union(*(u.keys() for u in used.values())))
    for ptype in all_types:
        if only and ptype not in only:
            continue
        label = KNOWN.get(ptype, "(not in decompilation)")
        lines = []
        for name in ("dragon", "pirates"):
            extra = sorted(used[name][ptype] - used["cadet"][ptype])
            for a in extra:
                who = users[name][(ptype, a)]
                lines.append(f"  {name:8} attr {a:5}  x{len(who):<3} e.g. {', '.join(sorted(set(who))[:4])}")
        st = ", ".join(f"{n} {states[n][ptype]}" for n in tables if states[n][ptype])
        if lines or only:
            print(f"{ptype} {label}  (max states: {st})")
            print("\n".join(lines) if lines else "  no attributes beyond CADET's")


if __name__ == "__main__":
    main()
