#!/usr/bin/env python
"""Pair every function of one Full Tilt exe with its counterpart in another and diff the changed ones.

Uses Ghidra's function boundaries (export/<EXE>.tsv from ghidra_scripts/ExportAll.java) instead of
fn_match.py's prologue scan, so vtable-only methods and non-standard prologues are included.

Matching (A = new exe, e.g. DRAGON.EXE; B = reference, e.g. CADET.EXE):
  1. same     identical masked instruction sequence (addresses / constants > 0xFFF masked),
              unique on both sides
  2. order    unmatched functions lying between two consecutive anchor pairs are paired by
              similarity inside that gap (same build, same link order)
  3. global   anything left is compared against every unmatched B function
A pair with ratio >= --min (default 0.5) is "similar"; otherwise the A function is "new" and an
unpaired B function is "gone". Data addresses are mapped A->B by aligning the raw operands of
paired functions, so the decompiled diff only shows real changes.

usage:
  fn_diff.py A.EXE A_EXPORT_DIR B.EXE B_EXPORT_DIR                  table of all pairs (TSV)
  fn_diff.py A.EXE A_EXPORT_DIR B.EXE B_EXPORT_DIR --changed        only similar / new / gone
  fn_diff.py A.EXE A_EXPORT_DIR B.EXE B_EXPORT_DIR --diff 0x41a290  decompiled unified diff of one
                                                                     A function vs its B pair
  fn_diff.py A.EXE A_EXPORT_DIR B.EXE B_EXPORT_DIR --data          identical functions that read
                                                                     initialised data that differs
  options: --min RATIO
example:
  re/work/.venv/bin/python re/fn_diff.py re/work/bin/DRAGON.EXE re/work/export \\
      re/work/bin/CADET.EXE re/work/export --changed
"""
import bisect
import difflib
import hashlib
import os
import re
import sys

import capstone
import pefile

HEX = re.compile(r"0x[0-9a-f]+")


class Exe:
    def __init__(self, path, export_dir):
        pe = pefile.PE(path)
        self.path = path
        self.name = os.path.basename(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        text = next(s for s in pe.sections if s.Characteristics & 0x20000000)
        self.start = self.base + text.VirtualAddress
        self.code = text.get_data()[:text.Misc_VirtualSize]
        self.lo = self.base
        self.hi = self.base + pe.OPTIONAL_HEADER.SizeOfImage
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.size, self.calls = {}, {}
        for line in open(os.path.join(export_dir, self.name + ".tsv")):
            entry, _name, size, calls = (line.rstrip("\n").split("\t") + [""])[:4]
            if entry.startswith("EXTERNAL"):
                continue
            a = int(entry, 16)
            self.size[a] = int(size)
            self.calls[a] = [int(c, 16) for c in calls.split(",") if c and not c.startswith("EXTERNAL")]
        self.c_path = os.path.join(export_dir, self.name + ".c")
        self._insns = {}

    def insns(self, addr):
        """[(mnemonic, op_str)] of the function's contiguous body."""
        if addr not in self._insns:
            off = addr - self.start
            blob = self.code[off:off + self.size[addr]] if off >= 0 else b""
            self._insns[addr] = [(i.mnemonic, i.op_str) for i in self.md.disasm(blob, addr)]
        return self._insns[addr]

    def is_addr(self, v):
        return self.lo <= v < self.hi

    def tokens(self, addr):
        return [f"{m} {HEX.sub(lambda x: 'A' if self.is_addr(int(x.group(), 16)) else x.group(), o)}"
                for m, o in self.insns(addr)]

    def decompiled(self):
        blocks = re.split(r"^// ==== ([0-9a-f]{8}) \S+ size \d+$", open(self.c_path).read(), flags=re.M)
        return {int(blocks[i], 16): blocks[i + 1] for i in range(1, len(blocks), 2)}


def fp(tokens):
    return hashlib.sha1("\n".join(tokens).encode()).hexdigest()


def ratio(ta, tb):
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    return sm.ratio() if sm.quick_ratio() >= 0.3 else 0.0


def best_pairs(a, b, cand_a, cand_b, min_ratio):
    """Greedy best-first pairing of two candidate lists by token similarity."""
    scored = []
    for x in cand_a:
        tx = a.tokens(x)
        for y in cand_b:
            ty = b.tokens(y)
            if min(len(tx), len(ty)) * 2 < max(len(tx), len(ty)) * min_ratio:
                continue
            r = ratio(tx, ty)
            if r >= min_ratio:
                scored.append((r, x, y))
    out, used_a, used_b = {}, set(), set()
    for r, x, y in sorted(scored, reverse=True):
        if x not in used_a and y not in used_b:
            out[x] = (y, r)
            used_a.add(x)
            used_b.add(y)
    return out


def match(a, b, min_ratio):
    """{a_addr: (b_addr, ratio)} for paired functions."""
    hb = {}
    for y in b.size:
        hb.setdefault(fp(b.tokens(y)), []).append(y)
    ha = {}
    for x in a.size:
        ha.setdefault(fp(a.tokens(x)), []).append(x)
    pairs = {}
    for h, xs in ha.items():
        ys = hb.get(h, [])
        if len(xs) == 1 and len(ys) == 1:
            pairs[xs[0]] = (ys[0], 1.0)
        elif xs and len(xs) == len(ys):  # duplicates (thunks, stubs): pair in address order
            for x, y in zip(sorted(xs), sorted(ys)):
                pairs[x] = (y, 1.0)
    # anchors in address order on both sides (keep only the monotonic chain)
    anchors = sorted((x, y) for x, (y, _) in pairs.items())
    chain = []
    for x, y in anchors:
        if not chain or y > chain[-1][1]:
            chain.append((x, y))
    xs_sorted = sorted(a.size)
    ys_sorted = sorted(b.size)
    used_b = {y for y, _ in pairs.values()}
    bounds = [(a.start - 1, b.start - 1)] + chain + [(1 << 32, 1 << 32)]
    for (x0, y0), (x1, y1) in zip(bounds, bounds[1:]):
        ca = [x for x in xs_sorted[bisect.bisect_right(xs_sorted, x0):bisect.bisect_left(xs_sorted, x1)]
              if x not in pairs]
        cb = [y for y in ys_sorted[bisect.bisect_right(ys_sorted, y0):bisect.bisect_left(ys_sorted, y1)]
              if y not in used_b]
        if ca and cb and len(ca) * len(cb) <= 4000:
            for x, (y, r) in best_pairs(a, b, ca, cb, min_ratio).items():
                pairs[x] = (y, r)
                used_b.add(y)
    rest_a = [x for x in xs_sorted if x not in pairs]
    rest_b = [y for y in ys_sorted if y not in used_b]
    for x, (y, r) in best_pairs(a, b, rest_a, rest_b, min_ratio).items():
        pairs[x] = (y, r)
    return pairs


def data_map(a, b, pairs):
    """{a_addr: b_addr} for every address operand that lines up in paired functions."""
    votes = {}
    for x, (y, _) in pairs.items():
        ia, ib = a.insns(x), b.insns(y)
        sm = difflib.SequenceMatcher(None, a.tokens(x), b.tokens(y), autojunk=False)
        for tag, i0, i1, j0, _j1 in sm.get_opcodes():
            if tag != "equal":
                continue
            for k in range(i1 - i0):
                va = [int(h, 16) for h in HEX.findall(ia[i0 + k][1])]
                vb = [int(h, 16) for h in HEX.findall(ib[j0 + k][1])]
                for p, q in zip(va, vb):
                    if a.is_addr(p) and b.is_addr(q):
                        votes.setdefault(p, {}).setdefault(q, 0)
                        votes[p][q] += 1
    return {p: max(qs, key=qs.get) for p, qs in votes.items()}


def initialised(exe_path):
    """[(start, end, bytes)] of the exe's initialised non-code sections."""
    pe = pefile.PE(exe_path)
    base = pe.OPTIONAL_HEADER.ImageBase
    return [(base + s.VirtualAddress, base + s.VirtualAddress + len(s.get_data()), s.get_data())
            for s in pe.sections
            if not s.Characteristics & 0x20000000 and s.SizeOfRawData
            and s.Name.rstrip(b"\0") in (b".rdata", b".data")]


def read_at(sections, va, n):
    for lo, hi, blob in sections:
        if lo <= va < hi:
            return blob[va - lo:va - lo + n]
    return None


def data_check(a, b, pairs):
    """Print operands of identical ('same') functions whose initialised data differs A vs B.

    Compares 16 bytes at each lined-up address (to the first NUL for strings); 4-byte values that
    are themselves addresses are compared through the A->B address map, so relocated pointers do
    not count as differences. Catches engine code that is identical but reads a changed constant.
    """
    amap = data_map(a, b, pairs)
    amap.update({x: y for x, (y, _) in pairs.items()})
    sa, sb = initialised(a.path), initialised(b.path)
    users = {}
    for x, (y, r) in pairs.items():
        if r != 1.0:
            continue
        for k, (ma, mb) in enumerate(zip(a.insns(x), b.insns(y))):
            for p, q in zip([int(h, 16) for h in HEX.findall(ma[1])], [int(h, 16) for h in HEX.findall(mb[1])]):
                if a.is_addr(p) and b.is_addr(q):
                    users.setdefault((p, q), set()).add(x)
    for (p, q), xs in sorted(users.items()):
        da, db = read_at(sa, p, 16), read_at(sb, q, 16)
        if da is None or db is None or da == db:
            continue
        same = len(da) == len(db)
        for i in range(0, min(len(da), len(db)) - 3, 4):
            va = int.from_bytes(da[i:i + 4], "little")
            vb = int.from_bytes(db[i:i + 4], "little")
            if va != vb and not (a.is_addr(va) and amap.get(va) == vb):
                same = False
                break
        if not same:
            print(f"A {p:08x} {da.hex()}  B {q:08x} {db.hex()}  used by A " +
                  " ".join(f"{x:08x}" for x in sorted(xs)))


def normalise(text, amap, own):
    """Rewrite A's FUN_/DAT_/PTR_/LAB_ names into B's addresses (unmapped ones get a '?')."""
    def sub(m):
        prefix, h = m.group(1), m.group(2)
        v = int(h, 16)
        if v in amap:
            return f"{prefix}{amap[v]:08x}"
        return m.group(0) if own else f"{prefix}{h}?"
    return re.sub(r"\b(FUN_|DAT_|PTR_[A-Za-z_]*|LAB_|s_\w*_|u_\w*_)([0-9a-f]{8})\b", sub, text)


def main():
    args = sys.argv[1:]
    min_ratio = 0.5
    if "--min" in args:
        i = args.index("--min")
        min_ratio = float(args[i + 1])
        del args[i:i + 2]
    flags = [x for x in args if x.startswith("--")]
    pos = [x for x in args if not x.startswith("--")]
    a, b = Exe(pos[0], pos[1]), Exe(pos[2], pos[3])
    pairs = match(a, b, min_ratio)
    if "--diff" in flags:
        target = int(pos[4], 16)
        amap = data_map(a, b, pairs)
        amap.update({x: y for x, (y, _) in pairs.items()})
        da, db = a.decompiled(), b.decompiled()
        if target not in pairs:
            print(f"{target:#x}: no counterpart in {b.name} (new)")
            print(normalise(da[target], amap, False))
            return
        y, r = pairs[target]
        left = normalise(da[target], amap, False).splitlines()
        right = db[y].splitlines()
        print(f"--- {b.name} {y:#010x}\n+++ {a.name} {target:#010x} (ratio {r:.3f}; A names mapped to B)")
        for line in difflib.unified_diff(right, left, lineterm="", n=3):
            if not line.startswith(("---", "+++")):
                print(line)
        return
    if "--data" in flags:
        data_check(a, b, pairs)
        return
    used_b = {y for y, _ in pairs.values()}
    print("a_addr\tsize\tkind\tb_addr\tratio")
    for x in sorted(a.size):
        if x in pairs:
            y, r = pairs[x]
            kind = "same" if r == 1.0 else "similar"
            if kind == "same" and "--changed" in flags:
                continue
            print(f"{x:08x}\t{a.size[x]}\t{kind}\t{y:08x}\t{r:.3f}")
        else:
            print(f"{x:08x}\t{a.size[x]}\tnew\t-\t-")
    for y in sorted(b.size):
        if y not in used_b:
            print(f"-\t{b.size[y]}\tgone\t{y:08x}\t-")


if __name__ == "__main__":
    main()
