#!/usr/bin/env python
"""Dump Full Tilt's part-class vtables with named slots, base classes and override markers.

The original engine (CADET.EXE / DRAGON.EXE / PIRATES.EXE) lays out part-class virtuals like
this (see re/work/spec/part_class_framework.md for the evidence):

  slot 0  GetTypeId()            returns the DAT part type (0 for the two base classes)
  slot 1  Message(code, value)
  slot 2  port_draw()
  slot 3  get_scoring(index)
  slot 4  put_scoring(count, int* scores)
  slot 5  scalar deleting destructor (~T + optional operator delete)
  -- TPinballComponent ends here (6 slots) --
  slot 6  Collision(ball, nextPos, direction, distance, edge)   TCollisionComponent
  slot 7  FieldEffect(ball, vecDst)                             TCollisionComponent
  slot 6  Reset()                    light family (TLight, TLightGroup, TLightBargraph, 1038)
  slot 6/7 Play() / Duration()       TSound only
  slot 8  Repaint()                  1037 (ball class) only; a TEdgeSegment sub-vtable follows

Every vtable stored by a function in the Ghidra export ("... = &PTR_FUN_<addr>;") is listed,
bounded by the next vtable or the first non-code word (at most 16 words). Slot 0's type id is
decoded from the machine code ("mov eax, imm32; ret" or "xor eax, eax; ret"). The base
classes are found from the part switch: TLight's (1002) constructor calls the
TPinballComponent constructor first, TBumper's (1005) calls the TCollisionComponent
constructor first; each base vtable is the one its constructor stores. Entries equal to the
base's entry at the same slot are marked "=", own overrides "*". A class's base
(TPinballComponent or TCollisionComponent) is decided by whether its slot-5 destructor
reaches the TCollisionComponent destructor.

usage: part_vtables.py EXE_PATH EXPORT_C [--all]
  --all   also list vtables that are not part classes (edge segments, table, ...) unfiltered
"""
import re
import struct
import sys

import pefile

from class_map import KNOWN, functions, part_switch

# GetTypeId values beyond class_map's DAT names: 1037 is a DAT part type whose objects go into the
# table's ball list (TBall-like); 1039-1041 are only returned by GetTypeId (no DAT part uses them).
KNOWN = {**KNOWN, 1026: "TLightGroup", 1028: "TComponentGroup", 1030: "TLightBargraph",
         1034: "TPlunger (flag 1)", 1037: "TBall-like (NEW)", 1039: "TPinballTable",
         1040: "TTableLayer", 1041: "TFlipper"}
SLOTS = ["GetTypeId", "Message", "port_draw", "get_scoring", "put_scoring", "~dtor(del)",
         "Collision", "FieldEffect"]
MAX_SLOTS = 16


def code_range(pe):
    base = pe.OPTIONAL_HEADER.ImageBase
    t = next(s for s in pe.sections if s.Characteristics & 0x20000000)
    return base, (base + t.VirtualAddress, base + t.VirtualAddress + t.Misc_VirtualSize)


def type_id(pe, base, fn):
    """Constant returned by a 'mov eax, imm32; ret' / 'xor eax, eax; ret' stub, else None."""
    b = pe.get_data(fn - base, 6)
    if b[0] == 0xB8 and b[5] == 0xC3:
        return struct.unpack_from("<I", b, 1)[0]
    if b[:3] == b"\x33\xc0\xc3":
        return 0
    return None


def vtable_stores(funcs):
    """{vtable address: [functions storing it]} from the export."""
    out = {}
    for fn, body in funcs.items():
        for v in re.findall(r"= &PTR_FUN_([0-9a-f]{8});", body):
            out.setdefault(int(v, 16), []).append(fn)
    return out


def read_vtable(pe, base, text, va, limit):
    data = pe.get_data(va - base, 4 * limit)
    out = []
    for i in range(limit):
        target = struct.unpack_from("<I", data, 4 * i)[0]
        if not text[0] <= target < text[1]:
            break
        out.append(target)
    return out


def first_call(body):
    m = re.search(r"FUN_([0-9a-f]{8})\(", body.split("{", 1)[1])
    return int(m.group(1), 16) if m else None


def reaches(funcs, fn, target, depth):
    """True if fn calls target directly or through at most depth levels of calls."""
    frontier, seen = {fn}, {fn}
    for _ in range(depth):
        nxt = set()
        for f in frontier:
            nxt |= {int(c, 16) for c in re.findall(r"FUN_([0-9a-f]{8})\(", funcs.get(f, ""))}
        if target in nxt:
            return True
        frontier = nxt - seen
        seen |= nxt
    return False


def last_vtable(body):
    m = re.findall(r"= &PTR_FUN_([0-9a-f]{8});", body)
    return int(m[-1], 16) if m else None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    exe, export = sys.argv[1], sys.argv[2]
    show_all = "--all" in sys.argv[3:]
    pe = pefile.PE(exe)
    base, text = code_range(pe)
    funcs = functions(export)
    stores = vtable_stores(funcs)
    starts = sorted(stores)
    tables = {}
    for v, nxt in zip(starts, starts[1:] + [starts[-1] + 4 * MAX_SLOTS]):
        tables[v] = read_vtable(pe, base, text, v, min(MAX_SLOTS, (nxt - v) // 4))

    ctors = {}
    for ptype, _size, ctor in part_switch(funcs):
        ctors.setdefault(ctor, []).append(ptype)
    by_type = {t: c for c, ts in ctors.items() for t in ts}
    light_base = first_call(funcs[by_type[1002]])       # TPinballComponent ctor
    coll_base = first_call(funcs[by_type[1005]])        # TCollisionComponent ctor
    pc_vt, cc_vt = last_vtable(funcs[light_base]), last_vtable(funcs[coll_base])
    cc_dtor = first_call(funcs[tables[cc_vt][5]])
    print(f"TPinballComponent  ctor {light_base:#x}  vtable {pc_vt:#x}  "
          f"{' '.join(hex(x) for x in tables[pc_vt])}")
    print(f"TCollisionComponent ctor {coll_base:#x}  vtable {cc_vt:#x}  "
          f"{' '.join(hex(x) for x in tables[cc_vt])}")
    print("slots: " + "  ".join(f"{i}={n}" for i, n in enumerate(SLOTS)))
    print("'=' inherited from the base named in column 'base', '*' own override; base found by "
          "following the slot-5 destructor chain; ctor '(...)' = functions storing the vtable\n")

    for v in starts:
        entries = tables[v]
        if not entries:
            continue
        tid = type_id(pe, base, entries[0])
        is_part = tid is not None and len(entries) <= 9 and (tid == 0 or 1000 <= tid < 1100)
        if not is_part and not show_all:
            continue
        own_ctor = [f for f in stores[v] if last_vtable(funcs[f]) == v and f in ctors]
        coll = v == cc_vt or (v != pc_vt and len(entries) > 5
                              and reaches(funcs, entries[5], cc_dtor, 3))
        basevt = tables[cc_vt] if coll else tables[pc_vt]
        bname = "Coll" if basevt is tables[cc_vt] else "Comp"
        name = KNOWN.get(tid, "base" if tid == 0 else ("?" if tid is None else "NEW"))
        if v == pc_vt:
            name = "TPinballComponent"
        elif v == cc_vt:
            name = "TCollisionComponent"
        marks = []
        for i, e in enumerate(entries):
            mark = "=" if i < len(basevt) and e == basevt[i] and v not in (pc_vt, cc_vt) else "*"
            marks.append(f"{mark}{e:06x}")
        ctor_s = ",".join(hex(c) for c in own_ctor) or (
            "(" + ",".join(hex(f) for f in sorted(stores[v])) + ")")
        tid_s = str(tid) if tid is not None else "?"
        print(f"{v:#x} type {tid_s:>4} {name:22} base {bname} slots {len(entries)} "
              f"ctor {ctor_s:10} " + " ".join(marks))


if __name__ == "__main__":
    main()
