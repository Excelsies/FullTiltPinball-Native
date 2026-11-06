#!/usr/bin/env python
"""Inspect Full Tilt / 3D Pinball .DAT files (PARTOUT(4.0)RESOURCE) against SpaceCadetPinball.

Prints only structure, names and numbers from the file; it never writes anything. Loader-rule
checks (what upstream would reject) are in dat_check.py; the parser is datfile.py.

usage:
  dat_dump.py summary  DAT...                 header, record-type and field-type counts
  dat_dump.py sounds   DAT [SOUND_DIR]        A_SOUND (202) records: group, name, file (exists?)
  dat_dump.py group    DAT NAME_OR_INDEX...   every field of the given groups (arrays decoded)
  dat_dump.py names    DAT                    index, record type and name of every named group
  dat_dump.py attrs    DAT... [--src DIR]     attribute ids per record type, per file; with --src,
                                              marks ids the upstream source never mentions as a
                                              number literal (textual heuristic only)
"""
import collections
import os
import re
import struct
import sys

from datfile import (BMP8, FLOAT_ARRAY, GROUP_NAME, RECORD_NAMES, SHORT_ARRAY, SHORT_VALUE, STRING,
                     TYPE_NAMES, Dat)


def cmd_summary(paths):
    for p in paths:
        d = Dat(p)
        rec = collections.Counter(d.record(i) for i in range(len(d.groups)))
        ft = collections.Counter(t for g in d.groups for t, _ in g)
        print(f"{p}\n  app {d.app!r}  description {d.description!r}  groups {len(d.groups)}")
        print("  records: " + ", ".join(f"{RECORD_NAMES.get(r, r)}={n}" for r, n in sorted(rec.items(), key=str)))
        print("  fields:  " + ", ".join(f"{TYPE_NAMES[t]}={n}" for t, n in sorted(ft.items())))


def cmd_sounds(path, sound_dir=None):
    d = Dat(path)
    have = {f.upper(): f for f in os.listdir(sound_dir)} if sound_dir else {}
    n = 0
    for i in range(len(d.groups)):
        if d.record(i) == 202:
            n += 1
            f = d.string(i)
            mark = ""
            if sound_dir:
                mark = "  ok" if f and os.path.basename(f.replace("\\", "/")).upper() in have else "  MISSING"
            print(f"{i:5} {d.name(i) or '-':24} {f!r}{mark}")
    print(f"{n} A_SOUND records (upstream loader::sound_list holds 64)")


def cmd_group(path, keys):
    d = Dat(path)
    for key in keys:
        i = d.lookup(key)
        print(f"group {i} name {d.name(i)!r} record {d.record(i)}")
        for t, b in d.groups[i]:
            if t == SHORT_ARRAY:
                v = struct.unpack(f"<{len(b) // 2}h", b)
                print(f"  ShortArray[{len(v)}] {list(v[:48])}{' ...' if len(v) > 48 else ''}")
            elif t == FLOAT_ARRAY:
                v = struct.unpack(f"<{len(b) // 4}f", b)
                print(f"  FloatArray[{len(v)}] {[round(x, 4) for x in v[:32]]}{' ...' if len(v) > 32 else ''}")
            elif t in (SHORT_VALUE, 2):
                print(f"  {TYPE_NAMES[t]} {struct.unpack('<h', b)[0]}")
            elif t in (GROUP_NAME, STRING):
                text = b.split(b"\0")[0].decode("latin-1")
                print(f"  {TYPE_NAMES[t]} {text!r}")
            elif t == BMP8:
                res, w, h = struct.unpack_from("<Bhh", b)
                print(f"  Bitmap8bit res {res} {w}x{h} ({len(b)} bytes)")
            else:
                print(f"  {TYPE_NAMES[t]} ({len(b)} bytes)")


def cmd_names(path):
    d = Dat(path)
    for i in range(len(d.groups)):
        if d.name(i) is not None:
            print(f"{i:5} {RECORD_NAMES.get(d.record(i), d.record(i))!s:9} {d.name(i)}")


def attr_ids(d):
    """{(record, 'S'|'F', id): count}: first value of every ShortArray / FloatArray, by record type
    (every entry id for MATERIAL / KICKER arrays). Note a score/ballcount ShortArray starts with a
    bitmap group index, not an attribute id."""
    out = collections.Counter()
    for i in range(len(d.groups)):
        rec = d.record(i)
        for s in d.shorts(i):
            if s:
                out[(rec, "S", s[0])] += 1
        for f in d.floats(i):
            if f and rec in (300, 400):  # MATERIAL / KICKER: one array of (id, value...) entries
                j = 0
                while j < len(f):
                    fid = int(f[j] // 1)
                    out[(rec, "F", fid)] += 1
                    j += {404: 4, 410: 9}.get(fid, 2)  # FT loader 0x40d290: 404 id+vec3, 410 id+8
            elif f:
                out[(rec, "F", int(f[0] // 1))] += 1
    return out


def upstream_mentions(src):
    """Numbers and string literals in the upstream source's .cpp/.h files."""
    nums, strs = set(), set()
    for fn in os.listdir(src):
        if fn.endswith((".cpp", ".h")) and not fn.startswith("imgui"):
            text = open(os.path.join(src, fn), encoding="utf-8", errors="replace").read()
            nums.update(int(n) for n in re.findall(r"\b(\d{3,4})\b", text))
            strs.update(re.findall(r'"([A-Za-z0-9_]+)"', text))
    return nums, strs


def cmd_attrs(paths, src=None):
    dats = [Dat(p) for p in paths]
    ids = [attr_ids(d) for d in dats]
    keys = sorted(set().union(*ids), key=lambda k: (str(k[0]), k[1], k[2]))
    nums, strs = upstream_mentions(src) if src else (None, None)
    print("record    kind id    " + " ".join(f"{os.path.basename(p)[:10]:>10}" for p in paths)
          + ("  upstream" if src else ""))
    for k in keys:
        rec, kind, aid = k
        mark = ""
        if src:
            mark = "  yes" if aid in nums else "  NEVER MENTIONED"
        print(f"{RECORD_NAMES.get(rec, rec)!s:9} {'short' if kind == 'S' else 'float'} {aid:5} "
              + " ".join(f"{c[k]:10}" for c in ids) + mark)
    if src:
        for d in dats:
            missing = sorted(s for s in strs if s in d.by_name) if strs else []
            print(f"{d.path}: {len(missing)} group names upstream mentions are present")


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "summary":
        cmd_summary(args)
    elif cmd == "sounds":
        cmd_sounds(*args[:2])
    elif cmd == "group":
        cmd_group(args[0], args[1:])
    elif cmd == "names":
        cmd_names(args[0])
    elif cmd == "attrs":
        src = None
        if "--src" in args:
            k = args.index("--src")
            src = args[k + 1]
            args = args[:k] + args[k + 2:]
        cmd_attrs(args, src)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
