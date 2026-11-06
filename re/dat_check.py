#!/usr/bin/env python
"""Replay SpaceCadetPinball's loading rules on a .DAT to find what upstream would reject.

usage:
  dat_check.py partcheck DAT [--3dpb]   partman::load_records' bitmap asserts (Full Tilt zMap
                                        resolution byte unless --3dpb)
  dat_check.py loadcheck DAT            loader::query_visual/material/kicker on every visual state
                                        of every part in table_objects (light/component groups
                                        load no visuals and are skipped)
"""
import struct
import sys

from datfile import BMP8, BMP16, Dat, table_objects

# loader.cpp: attribute ids query_visual / material / kicker accept (anything else -> error dialog)
VISUAL_SHORT_IDS = {100, 300, 304, 400, 406, 602, 1100, 1101, 1500}
MATERIAL_FLOAT_IDS = {301, 302, 304}
KICKER_FLOAT_IDS = {401, 402, 403, 404, 405, 406}
NO_VISUALS = {1026, 1028, 1030}  # TLightGroup, TComponentGroup, TLightBargraph: loadVisuals=false
UPSTREAM_TYPES = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1010, 1011, 1012, 1013, 1014, 1015,
                  1016, 1017, 1018, 1019, 1020, 1021, 1022, 1023, 1024, 1026, 1028, 1029, 1030, 1031,
                  1032, 1033}  # TPinballTable.cpp part switch


def visual_problems(d, state, offset):
    """What loader::query_visual(group, offset) would reject for this state group."""
    probs = []
    sa = d.shorts(state)
    if sa:
        arr, k = sa[0], 0
        while k < len(arr):
            aid = arr[k]
            if aid not in VISUAL_SHORT_IDS:
                probs.append(f"visual short attr {aid} (error 9)")
                break
            if aid == 100 and offset:
                probs.append("STATES (re)defined in a state (error 7)")
            if aid in (300, 400) and k + 1 < len(arr):
                ref = arr[k + 1]
                want, ok_ids = (300, MATERIAL_FLOAT_IDS) if aid == 300 else (400, KICKER_FLOAT_IDS)
                if not 0 <= ref < len(d.groups) or d.record(ref) != want:
                    probs.append(f"attr {aid} -> group {ref} is not record {want}")
                else:
                    fa = d.floats(ref)[0] if d.floats(ref) else []
                    j = 0
                    while j < len(fa):
                        fid = int(fa[j] // 1)
                        if fid not in ok_ids:
                            probs.append(f"group {ref} ({want}) float attr {fid}")
                            break
                        j += 4 if fid == 404 else 2  # 404 = id + vector3
            if aid in (304, 406, 1100, 1101) and k + 1 < len(arr) and d.record(arr[k + 1]) != 202:
                probs.append(f"attr {aid} -> group {arr[k + 1]} is not A_SOUND")
            k += 9 if aid == 1500 else 2
    fa = d.floats(state)
    if fa and fa[0] and fa[0][0] == 600.0:
        count = len(fa[0]) // 2 - 2
        n = int(fa[0][1] // 1) - 1
        if n not in (0, 1) and n != count:
            probs.append(f"# walls {n} doesn't match data size {count} (error 8)")
    return probs


def cmd_partcheck(path, full_tilt=True):
    """partman::load_records: Bitmap8bit header (14 bytes) Size + 14 == field size, Resolution <= 2;
    Bitmap16bit: [FT: resolution byte, 0xff -> 0, <= 2] header (14 bytes), Stride * Height * 2 == rest
    (otherwise partman skips it as an empty zMap, which only 3DPB data is known to need)."""
    d = Dat(path)
    n8 = n16 = bad = 0
    for i, g in enumerate(d.groups):
        for t, b in g:
            if t == BMP8:
                n8 += 1
                res, w, h, x, y, size = struct.unpack_from("<BhhhhI", b)
                if size + 14 != len(b) or res > 2:
                    print(f"group {i} {d.name(i)}: Bitmap8bit res {res} size {size} field {len(b)}")
                    bad += 1
            elif t == BMP16:
                n16 += 1
                off = 0
                if full_tilt:
                    res = b[0]
                    res = 0 if res == 0xFF else res
                    off = 1
                    if res > 2:
                        print(f"group {i} {d.name(i)}: zMap resolution {res}")
                        bad += 1
                w, h, stride = struct.unpack_from("<hhh", b, off)
                if stride * h * 2 != len(b) - off - 14:
                    print(f"group {i} {d.name(i)}: zMap {w}x{h} stride {stride} vs {len(b) - off - 14} bytes")
                    bad += 1
    print(f"{path}: {n8} Bitmap8bit, {n16} Bitmap16bit, {bad} problem(s) for partman "
          f"({'Full Tilt' if full_tilt else '3DPB'} mode)")


def cmd_loadcheck(path):
    """Replay TPinballComponent's visual loading (states 0..n-1) for every part in table_objects."""
    d = Dat(path)
    bad = 0
    for ptype, i in table_objects(d):
        if ptype in NO_VISUALS:
            continue
        tag = "" if ptype in UPSTREAM_TYPES else " [type not in upstream]"
        sa = d.shorts(i)
        states = sa[0][1] if sa and sa[0] and sa[0][0] == 100 and len(sa[0]) > 1 else 1
        if d.record(i) != 200:
            print(f"{ptype} {i} {d.name(i)}: not AN_OBJECT{tag}")
            bad += 1
            continue
        for off in range(states):  # TPinballComponent: query_visual(group, 0..states-1)
            st = i + off
            if off and (st >= len(d.groups) or d.record(st) != 201):
                print(f"{ptype} {i} {d.name(i)}: state {off} -> group {st} is not A_STATE{tag}")
                bad += 1
                continue
            for p in visual_problems(d, st, off):
                print(f"{ptype} {i} {d.name(i)} state {off}: {p}{tag}")
                bad += 1
    print(f"{path}: {len(table_objects(d))} parts, {bad} problem(s) for upstream's loader")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("partcheck", "loadcheck"):
        raise SystemExit(__doc__)
    if sys.argv[1] == "partcheck":
        cmd_partcheck(sys.argv[2], "--3dpb" not in sys.argv)
    else:
        cmd_loadcheck(sys.argv[2])


if __name__ == "__main__":
    main()
