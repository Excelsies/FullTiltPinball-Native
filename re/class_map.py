#!/usr/bin/env python
"""Map Full Tilt's table part classes: part type -> constructor -> vtable -> methods.

Reads Ghidra's decompiled export (export/<EXE>.c, from ghidra_scripts/ExportAll.java): the part
switch in the table constructor ("case <type>: ... FUN_alloc(1,<size>) ... FUN_<ctor>(...)"), then
each constructor's vtable store ("*param_1 = &PTR_...") and the vtable's entries from the exe.
Known types are named as in SpaceCadetPinball's TPinballTable.cpp; the others are what the
decompilation has to add.

usage: class_map.py EXE_PATH EXPORT_C
"""
import re
import struct
import sys

import pefile

KNOWN = {1000: "TWall", 1001: "TPlunger", 1002: "TLight", 1003: "TFlipper L", 1004: "TFlipper R",
         1005: "TBumper", 1006: "TPopupTarget", 1007: "TDrain", 1010: "TWall", 1011: "TBlocker",
         1012: "TKickout", 1013: "TGate", 1014: "TKickback", 1015: "TRollover", 1016: "TOneway",
         1017: "TSink", 1018: "TFlagSpinner", 1019: "TSoloTarget", 1020: "TLightRollover",
         1021: "TRamp", 1022: "THole", 1023: "TDemo", 1024: "TTripwire", 1026: "TLightGroup",
         1028: "TComponentGroup", 1029: "TKickout (no ball)", 1030: "TLightBargraph",
         1031: "TSound", 1032: "TTimer", 1033: "TTextBox"}


def functions(export):
    """{address: body} from the export's '// ==== <addr> <name> size <n>' blocks."""
    blocks = re.split(r"^// ==== ([0-9a-f]{8}) \S+ size \d+$", open(export).read(), flags=re.M)
    return {int(blocks[i], 16): blocks[i + 1] for i in range(1, len(blocks), 2)}


def part_switch(funcs):
    """[(type, alloc size, ctor address)] from the function holding the part switch."""
    body = next(b for b in funcs.values() if "case 0x40b:" in b and "case 1000:" in b)
    out = []
    for case in re.split(r"\n\s*case ", body)[1:]:
        labels = [case.split(":")[0]]
        m_size = re.search(r"FUN_[0-9a-f]+\(1,(0x[0-9a-f]+|\d+)\)", case)
        m_ctor = re.findall(r"FUN_([0-9a-f]{8})\(puVar\d+,\(int\)param_1", case)
        if m_size and m_ctor:
            for label in labels:
                out.append((int(label, 0), int(m_size.group(1), 0), int(m_ctor[0], 16)))
    return out


def vtable_of(funcs, ctor):
    # __thiscall constructors store it as "*(undefined ***)this = &PTR_..."; others via param_1
    m = re.findall(r"\*(?:\(undefined \*\*\*\)this|param_1) = &PTR_[A-Z_]*([0-9a-f]{8});", funcs.get(ctor, ""))
    return int(m[-1], 16) if m else None  # last store = most derived class


def vtable_entries(pe, base, text, va, limit=64):
    rva = va - base
    data = pe.get_data(rva, 4 * limit)
    out = []
    for i in range(limit):
        target = struct.unpack_from("<I", data, 4 * i)[0]
        if not text[0] <= target < text[1]:
            break
        out.append(target)
    return out


def main():
    exe, export = sys.argv[1], sys.argv[2]
    pe = pefile.PE(exe)
    base = pe.OPTIONAL_HEADER.ImageBase
    t = next(s for s in pe.sections if s.Characteristics & 0x20000000)
    text = (base + t.VirtualAddress, base + t.VirtualAddress + t.Misc_VirtualSize)
    funcs = functions(export)
    parts = sorted(part_switch(funcs))
    vtables = sorted({v for v in (vtable_of(funcs, c) for _, _, c in parts) if v})
    # vtables sit back to back: one ends where the next known one starts
    limit = {v: (n - v) // 4 for v, n in zip(vtables, vtables[1:] + [vtables[-1] + 4 * 64])}
    tables = {v: vtable_entries(pe, base, text, v, limit[v]) for v in vtables}
    owners = {}
    for v, methods in tables.items():
        for m in methods:
            owners.setdefault(m, set()).add(v)
    seen = set()
    for ptype, size, ctor in parts:
        vt = vtable_of(funcs, ctor)
        methods = tables.get(vt, [])
        name = KNOWN.get(ptype, "** NOT IN DECOMPILATION **")
        repeat = " (same class as above)" if ctor in seen else ""
        seen.add(ctor)
        own = [m for m in methods if owners[m] == {vt}]
        print(f"{ptype} {name:28} size {size:#6x} ctor {ctor:#010x} vtable "
              f"{(hex(vt) if vt else '?'):10} methods {len(methods)} own {len(own)}{repeat}")
        if ptype not in KNOWN and not repeat:
            print("      own methods: " + " ".join(f"{m:#x}" for m in own))


if __name__ == "__main__":
    main()
