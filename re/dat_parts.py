#!/usr/bin/env python
"""List the table part types each Full Tilt table's .DAT uses (format from SpaceCadetPinball's
partman.cpp / TPinballTable.cpp), against the types the decompiled engine can build.

usage: dat_parts.py FULLTILT_DIR
"""
import collections
import os
import struct
import sys

FIELD_SIZE = [2, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0]  # partman::_field_size
GROUP_NAME, SHORT_ARRAY = 3, 10
# object types TPinballTable's constructor builds (TPinballTable.cpp)
KNOWN = {1000: "TWall", 1001: "TPlunger", 1002: "TLight", 1003: "TFlipper L", 1004: "TFlipper R",
         1005: "TBumper", 1006: "TPopupTarget", 1007: "TDrain", 1010: "TWall", 1011: "TBlocker",
         1012: "TKickout", 1013: "TGate", 1014: "TKickback", 1015: "TRollover", 1016: "TOneway",
         1017: "TSink", 1018: "TFlagSpinner", 1019: "TSoloTarget", 1020: "TLightRollover",
         1021: "TRamp", 1022: "THole", 1023: "TDemo", 1024: "TTripwire", 1026: "TLightGroup",
         1028: "TComponentGroup", 1029: "TKickout (no ball)", 1030: "TLightBargraph",
         1031: "TSound", 1032: "TTimer", 1033: "TTextBox"}
TABLES = {"cadet": "CADET/CADET.DAT", "dragon": "DRAGON/DRAGON.DAT", "pirates": "PIRATE/PIRATES.DAT"}


def groups(path):
    data = open(path, "rb").read()
    sig = data[:21].split(b"\0")[0]
    assert sig == b"PARTOUT(4.0)RESOURCE", f"{path}: not a PARTOUT file ({sig})"
    _, n_groups, _, unknown = struct.unpack_from("<iHiH", data, 171)
    pos = 183 + unknown
    for _ in range(n_groups):
        count = data[pos]
        pos += 1
        entries = []
        for _ in range(count):
            etype = data[pos]
            pos += 1
            size = FIELD_SIZE[etype]
            if size < 0:
                size = struct.unpack_from("<I", data, pos)[0]
                pos += 4
            entries.append((etype, data[pos:pos + size]))
            pos += size
        yield entries
    assert pos == len(data), f"{path}: parsed {pos} of {len(data)} bytes"


def table_objects(path):
    for entries in groups(path):
        names = [b.split(b"\0")[0].decode("latin-1") for t, b in entries if t == GROUP_NAME]
        if names == ["table_objects"]:
            for t, b in entries:
                if t == SHORT_ARRAY:
                    shorts = struct.unpack(f"<{len(b) // 2}h", b)
                    if shorts[0] == 1025:
                        return list(zip(shorts[1::2], shorts[2::2]))
    raise SystemExit(f"{path}: no table_objects list")


def main():
    folder = sys.argv[1]
    for name, rel in TABLES.items():
        objects = table_objects(os.path.join(folder, rel))
        types = collections.Counter(t for t, _ in objects)
        unknown = {t: n for t, n in types.items() if t not in KNOWN}
        print(f"{name}: {len(objects)} parts, {len(types)} types; types the engine lacks: "
              f"{unknown or 'none'}")
        print("   " + ", ".join(f"{KNOWN.get(t, t)}x{n}" for t, n in sorted(types.items())))


if __name__ == "__main__":
    main()
