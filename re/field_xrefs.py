#!/usr/bin/env python
"""List every instruction in an exe's .text that accesses memory at [register + OFFSET], i.e. the
readers and writers of a struct field, including code the Ghidra export has no function for
(timer callbacks and other code only reachable through pointers).

Linear-sweep disassembly (capstone, skipdata on), so a few hits inside data or misaligned code are
possible; confirm each one in the export or with a targeted disassembly. Offsets are matched on
the displacement only, so other structs with a field at the same offset also show up.

usage:
  field_xrefs.py EXE OFFSET[,OFFSET...] [EXPORT.c]

OFFSET is hex (0x153) or decimal. With EXPORT.c (re/work/export/<EXE>.c), each hit is labelled
with the export function that contains it, or "(not in export)".
"""
import bisect
import re
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs


def export_functions(path):
    starts = []
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            m = re.match(r"// ==== ([0-9a-f]{8}) (\S+) size (\d+)", line)
            if m:
                starts.append((int(m.group(1), 16), int(m.group(3)), m.group(2)))
    starts.sort()
    return starts


def containing(funcs, keys, addr):
    i = bisect.bisect_right(keys, addr) - 1
    if i >= 0:
        start, size, name = funcs[i]
        if addr < start + max(size, 1):
            return f"{name} (0x{start:x})"
    return "(not in export)"


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    pe = pefile.PE(sys.argv[1])
    offsets = [int(o, 0) for o in sys.argv[2].split(",")]
    funcs = export_functions(sys.argv[3]) if len(sys.argv) == 4 else []
    keys = [f[0] for f in funcs]
    base = pe.OPTIONAL_HEADER.ImageBase
    text = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"]
    if not text:
        raise SystemExit(f"{sys.argv[1]}: no .text section")
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.skipdata = True
    md.detail = True
    for sec in text:
        for ins in md.disasm(sec.get_data(), base + sec.VirtualAddress):
            if ins.id == 0:  # skipped data
                continue
            for op in ins.operands:
                if op.type == 3 and op.mem.base != 0 and op.mem.disp in offsets:  # X86_OP_MEM
                    where = containing(funcs, keys, ins.address) if funcs else ""
                    print(f"0x{ins.address:x}  {ins.mnemonic} {ins.op_str:40}  {where}")
                    break


if __name__ == "__main__":
    main()
