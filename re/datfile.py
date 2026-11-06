#!/usr/bin/env python
"""Parser for Full Tilt / 3D Pinball .DAT files (PARTOUT(4.0)RESOURCE), shared by dat_dump.py and
dat_check.py. Format as read by SpaceCadetPinball's partman.cpp; field types from GroupData.h.

usage (library): from datfile import Dat, table_objects; d = Dat(path); d.lookup("table_objects")
"""
import struct

FIELD_SIZE = [2, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0]  # partman::_field_size
SHORT_VALUE, BMP8, GROUP_NAME, PALETTE, STRING, SHORT_ARRAY, FLOAT_ARRAY, BMP16 = 0, 1, 3, 5, 9, 10, 11, 12
TYPE_NAMES = {0: "ShortValue", 1: "Bitmap8bit", 2: "Unknown2", 3: "GroupName", 4: "Unknown4",
              5: "Palette", 6: "Unknown6", 7: "Unknown7", 8: "Unknown8", 9: "String",
              10: "ShortArray", 11: "FloatArray", 12: "Bitmap16bit"}
RECORD_NAMES = {200: "AN_OBJECT", 201: "A_STATE", 202: "A_SOUND", 300: "MATERIAL", 400: "KICKER"}


class Dat:
    def __init__(self, path):
        self.path = path
        data = open(path, "rb").read()
        sig = data[:21].split(b"\0")[0]
        if sig != b"PARTOUT(4.0)RESOURCE":
            raise SystemExit(f"{path}: not a PARTOUT(4.0)RESOURCE file ({sig!r})")
        self.app = data[21:71].split(b"\0")[0].decode("latin-1")
        self.description = data[71:171].split(b"\0")[0].decode("latin-1")
        _, n_groups, _, unknown = struct.unpack_from("<iHiH", data, 171)
        pos = 183 + unknown
        self.groups = []
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
            self.groups.append(entries)
        if pos != len(data):
            raise SystemExit(f"{path}: parsed {pos} of {len(data)} bytes")
        self.by_name = {}
        for i in range(len(self.groups)):
            name = self.name(i)
            if name is not None:
                self.by_name.setdefault(name, i)  # record_labeled() finds the first one

    def fields(self, i, etype):
        return [b for t, b in self.groups[i] if t == etype]

    def name(self, i):
        f = self.fields(i, GROUP_NAME)
        return f[0].split(b"\0")[0].decode("latin-1") if f else None

    def record(self, i):
        f = self.fields(i, SHORT_VALUE)
        return struct.unpack("<h", f[0])[0] if f else None

    def string(self, i):
        f = self.fields(i, STRING)
        return f[0].split(b"\0")[0].decode("latin-1") if f else None

    def shorts(self, i):
        return [list(struct.unpack(f"<{len(b) // 2}h", b)) for b in self.fields(i, SHORT_ARRAY)]

    def floats(self, i):
        return [list(struct.unpack(f"<{len(b) // 4}f", b)) for b in self.fields(i, FLOAT_ARRAY)]

    def lookup(self, key):
        if key.lstrip("-").isdigit():
            return int(key)
        if key not in self.by_name:
            raise SystemExit(f"{self.path}: no group named {key!r}")
        return self.by_name[key]


def table_objects(d):
    """[(part type, group)] from table_objects attribute 1025."""
    for s in d.shorts(d.lookup("table_objects")):
        if s and s[0] == 1025:
            return list(zip(s[1::2], s[2::2]))
    raise SystemExit(f"{d.path}: no table_objects/1025 list")
