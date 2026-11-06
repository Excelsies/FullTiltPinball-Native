#!/usr/bin/env python
"""Relocation-aware byte comparison of one code region in two Full Tilt exes (same engine build).

Walks the region byte by byte in lockstep:
  - a 4-byte field that is a .reloc HIGHLOW fixup in both exes is an absolute address: it is
    recorded as a pair (A target, B target) and its delta is tallied per section;
  - any other differing byte is reported with the instruction that contains it (capstone, linear
    decode of the region) and the enclosing function (Ghidra export .tsv).
Then every recorded data target that lies outside the code section is compared: 8 bytes at the A
address vs 8 bytes at the B address (uninitialised .bss reads as zero on both sides); a 4-byte word
that is itself a relocated pointer in both exes is compared as an address (not by value).

usage:
  engine_lockstep.py A.EXE B.EXE EXPORT_DIR [--lo 0x401000] [--hi 0x422a80]
example (PIRATES vs DRAGON engine region):
  re/work/.venv/bin/python re/engine_lockstep.py re/work/bin/PIRATES.EXE re/work/bin/DRAGON.EXE \\
      re/work/export
"""
import bisect
import collections
import os
import struct
import sys

import capstone
import pefile


class Image:
    def __init__(self, path, export_dir):
        self.pe = pefile.PE(path)
        self.name = os.path.basename(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.mem = self.pe.get_memory_mapped_image()
        self.relocs = set()
        for block in getattr(self.pe, "DIRECTORY_ENTRY_BASERELOC", []):
            for entry in block.entries:
                if entry.type == 3:  # IMAGE_REL_BASED_HIGHLOW
                    self.relocs.add(self.base + entry.rva)
        self.sections = [(self.base + s.VirtualAddress, self.base + s.VirtualAddress + s.Misc_VirtualSize,
                          s.Name.rstrip(b"\0").decode(), bool(s.Characteristics & 0x20000000))
                         for s in self.pe.sections]
        self.fn_starts, self.fn_size = [], {}
        for line in open(os.path.join(export_dir, self.name + ".tsv")):
            cols = line.rstrip("\n").split("\t")
            if cols[0].startswith("EXTERNAL"):
                continue
            a = int(cols[0], 16)
            self.fn_starts.append(a)
            self.fn_size[a] = int(cols[2])
        self.fn_starts.sort()

    def byte(self, va):
        off = va - self.base
        return self.mem[off] if 0 <= off < len(self.mem) else None

    def read(self, va, n):
        off = va - self.base
        return bytes(self.mem[off:off + n]) if 0 <= off and off + n <= len(self.mem) else None

    def section(self, va):
        for lo, hi, name, code in self.sections:
            if lo <= va < hi:
                return name, code
        return None, False

    def function(self, va):
        i = bisect.bisect_right(self.fn_starts, va) - 1
        if i >= 0 and va < self.fn_starts[i] + max(self.fn_size[self.fn_starts[i]], 1):
            return self.fn_starts[i]
        return None


def import_names(img):
    names = {}
    for dll in getattr(img.pe, "DIRECTORY_ENTRY_IMPORT", []):
        for imp in dll.imports:
            names[imp.address] = (dll.dll.decode().lower(), (imp.name or b"").decode() or imp.ordinal)
    return names


def in_iat(img, va):
    d = img.pe.OPTIONAL_HEADER.DATA_DIRECTORY[12]  # IMAGE_DIRECTORY_ENTRY_IAT
    return img.base + d.VirtualAddress <= va < img.base + d.VirtualAddress + d.Size


def instruction_map(img, lo, hi):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    starts, text = [], {}
    va = lo
    while va < hi:  # bounded: va strictly increases by >= 1 each iteration
        insn = next(md.disasm(img.read(va, 16) or b"", va, 1), None)
        if insn is None:
            starts.append(va)
            text[va] = "(undecodable)"
            va += 1
            continue
        starts.append(va)
        text[va] = f"{insn.mnemonic} {insn.op_str}"
        va += insn.size
    return starts, text


def main():
    args = [a for a in sys.argv[1:]]
    opts = {"--lo": 0x401000, "--hi": 0x422a80}
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = int(args[i + 1], 16)
            del args[i:i + 2]
    if len(args) != 3:
        sys.exit(__doc__)
    a, b = Image(args[0], args[2]), Image(args[1], args[2])
    lo, hi = opts["--lo"], opts["--hi"]
    a_starts, a_text = instruction_map(a, lo, hi)
    b_starts, b_text = instruction_map(b, lo, hi)

    pairs, byte_diffs, reloc_mismatch = [], [], []
    va = lo
    while va < hi:  # bounded: va advances by 1 or 4 each iteration
        ra, rb = va in a.relocs, va in b.relocs
        if ra or rb:
            if ra != rb:
                reloc_mismatch.append(va)
                va += 1
                continue
            ta, tb = struct.unpack("<I", a.read(va, 4))[0], struct.unpack("<I", b.read(va, 4))[0]
            pairs.append((va, ta, tb))
            va += 4
            continue
        if a.byte(va) != b.byte(va):
            byte_diffs.append(va)
        va += 1

    print(f"region 0x{lo:x}-0x{hi:x}  A={a.name}  B={b.name}")
    print(f"relocated fields: {len(pairs)}  reloc-set mismatches: {len(reloc_mismatch)}  "
          f"other differing bytes: {len(byte_diffs)}")
    deltas = collections.Counter()
    for _, ta, tb in pairs:
        deltas[(a.section(ta)[0], ta - tb)] += 1
    print("\n# address deltas (A target - B target) by A section")
    for (sec, d), n in sorted(deltas.items(), key=lambda x: (str(x[0][0]), x[0][1])):
        print(f"  {sec or '?':8} {d:+#x}  x{n}")

    print("\n# differing non-relocated bytes: rel32 call/jmp target pairs (A target, B target: sites)")
    seen, branch_pairs, other = set(), collections.defaultdict(list), []
    for va in byte_diffs:
        ia = a_starts[bisect.bisect_right(a_starts, va) - 1]
        if ia in seen:
            continue
        seen.add(ia)
        ib = b_starts[bisect.bisect_right(b_starts, va) - 1]
        ma, mb = a_text[ia].split(" ", 1), b_text[ib].split(" ", 1)
        if (ia == ib and ma[0] == mb[0] and ma[0] in ("call", "jmp") and len(ma) == 2 and len(mb) == 2
                and ma[1].startswith("0x") and mb[1].startswith("0x")):
            branch_pairs[(int(ma[1], 16), int(mb[1], 16))].append(ia)
        else:
            other.append((ia, ib))
    for (ta, tb), sites in sorted(branch_pairs.items()):
        fa = a.function(ta)
        print(f"  {ta:#x} <- {tb:#x}  delta {ta - tb:+#x}  A fn start {('0x%x' % fa) if fa else '?'}  "
              f"x{len(sites)}: " + " ".join(f"{s:#x}" for s in sites[:8]) + (" ..." if len(sites) > 8 else ""))
    print("\n# other differing instructions")
    for ia, ib in other:
        fa = a.function(ia)
        print(f"  0x{ia:x} in fn {('0x%x' % fa) if fa else '?'}:  A: {a_text[ia]}   |  B(0x{ib:x}): {b_text[ib]}")
    for va in reloc_mismatch:
        print(f"  reloc only on one side at 0x{va:x}")

    print("\n# data targets whose contents differ (8 bytes, pointer words compared as pointers)")
    imports_a, imports_b = import_names(a), import_names(b)
    checked, ndiff, n_iat = set(), 0, 0
    for site, ta, tb in pairs:
        if (ta, tb) in checked:
            continue
        checked.add((ta, tb))
        if a.section(ta)[1]:
            continue  # code target: the delta table covers it
        if in_iat(a, ta) or ta in imports_a or ta + 4 in imports_a:
            n_iat += 1
            continue  # import address table slot: holds an unbound RVA, compared by import name below
        da, db = a.read(ta, 8), b.read(tb, 8)
        if da is None or db is None:
            print(f"  site 0x{site:x}: target 0x{ta:x}/0x{tb:x} unreadable")
            continue
        differs = False
        for k in (0, 4):
            wa, wb = da[k:k + 4], db[k:k + 4]
            if wa == wb:
                continue
            if (ta + k) in a.relocs and (tb + k) in b.relocs:
                continue  # pointer word: different address is expected
            differs = True
        if differs:
            ndiff += 1
            fa = a.function(site)
            print(f"  site 0x{site:x} (fn {('0x%x' % fa) if fa else '?'}): A 0x{ta:x} {da.hex()}  "
                  f"B 0x{tb:x} {db.hex()}")
    print(f"distinct data targets checked: {len(checked)}  IAT slots: {n_iat}  differing: {ndiff}")
    bad = [(ta, tb) for _, ta, tb in pairs if ta in imports_a and imports_a[ta] != imports_b.get(tb)]
    print(f"IAT slot references whose import name differs: {len(bad)}")
    for ta, tb in sorted(set(bad)):
        print(f"  A 0x{ta:x} {imports_a[ta]}  B 0x{tb:x} {imports_b.get(tb)}")


if __name__ == "__main__":
    main()
