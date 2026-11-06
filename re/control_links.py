#!/usr/bin/env python
"""Dump a Full Tilt table's control links: the component tags its table-rules module looks up by
name, with each one's control function and scores (upstream control.cpp: score_components /
component_info {Tag, Control{ControlFunc, ScoreCount, Scores}} and the plain tag list).

Finds control::make_links in Ghidra's export (ExportAll.java): the function that walks a table
of 16-byte entries, stores "entry + 4" at component+0x2a and calls the component's virtual +0x10
(put_scoring). Then reads from the exe
  entry  = [tag*, ControlFunc, ScoreCount, Scores*]   tag = [char* name, TPinballComponent* comp]
and the second, plain list of tag pointers walked right after it.
Every tag is printed with the address of its comp slot (tag + 4): that is the DAT_xxxxxxxx the
decompiled table code reads when it uses the component.

usage: control_links.py EXE_PATH EXPORT_C
"""
import re
import struct
import sys

import pefile


def make_links(export):
    """(address, entries table, entry count, tag list start, tag list end) from the export."""
    blocks = re.split(r"^// ==== ([0-9a-f]{8}) \S+ size \d+$", open(export).read(), flags=re.M)
    for i in range(1, len(blocks), 2):
        body = blocks[i + 1]
        if "+ 0x2a) = ppuVar" not in body or "+ 0x10))" not in body:
            continue
        # the loop counter and the table pointer are set in either order before the first loop
        head = body.split("do {")[0]
        n = re.search(r"iVar\d+ = (0x[0-9a-f]+|\d+);", head)
        lst = re.findall(r"ppuVar\d+ = &PTR_PTR_([0-9a-f]{8});", body)
        end = re.search(r"while \(ppuVar\d+ < [&]?[A-Za-z_]*?([0-9a-f]{8})\)", body)
        if n and len(lst) >= 2 and end:
            return int(blocks[i], 16), int(lst[0], 16), int(n.group(1), 0), int(lst[1], 16), int(end.group(1), 16)
    raise SystemExit("make_links not found in export")


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase

    def dword(self, va):
        return struct.unpack("<I", self.pe.get_data(va - self.base, 4))[0]

    def cstr(self, va):
        return self.pe.get_data(va - self.base, 64).split(b"\0")[0].decode("latin-1")


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    img = Image(sys.argv[1])
    fn, table, count, lst, lst_end = make_links(sys.argv[2])
    print(f"make_links {fn:#x}: {count} scored components at {table:#x}, tag list {lst:#x}-{lst_end:#x}")
    print("tag        comp slot  name                           control    scores")
    for k in range(count):
        tag, func, n, scores = (img.dword(table + 16 * k + 4 * j) for j in range(4))
        vals = [img.dword(scores + 4 * j) for j in range(n)] if n and scores else []
        print(f"{tag:#x}  {tag + 4:#x}  {img.cstr(img.dword(tag)):30} {func:#x}  {vals}")
    print("plain tags (no control function):")
    for va in range(lst, lst_end, 4):
        tag = img.dword(va)
        print(f"{tag:#x}  {tag + 4:#x}  {img.cstr(img.dword(tag))}")


if __name__ == "__main__":
    main()
