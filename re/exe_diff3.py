#!/usr/bin/env python
"""Three-way function comparison of one Full Tilt exe against reference exes (default: PIRATES.EXE
against CADET.EXE and DRAGON.EXE), using Ghidra's function boundaries from ExportAll.java's .tsv.

Each function's instructions are fingerprinted with image addresses masked (call/jmp targets,
absolute operands inside the image), then the target's functions are classed as
  same@     identical to the reference function at the same address (engine code, unmoved)
  same      identical to a reference function elsewhere (moved)
  changed   no identical copy; best match >= SIMILAR (difflib ratio) -- the best reference shown
  new       no counterpart in any reference
Per function the reference(s) it matches are listed (C = first ref, D = second, ...), so
"same D" without C means: shared with DRAGON but not in CADET.

usage: exe_diff3.py BIN_DIR EXPORT_DIR [TARGET [REF ...]] [--all]
  BIN_DIR     folder with the .EXE files (e.g. re/work/bin)
  EXPORT_DIR  folder with <EXE>.tsv from ExportAll.java (e.g. re/work/export)
  TARGET      default PIRATES.EXE; REF default CADET.EXE DRAGON.EXE
  --all       list every function, not only the ones without an identical copy in the first ref
"""
import difflib
import os
import sys

import capstone
import pefile

SIMILAR = 0.80


class Exe:
    def __init__(self, bin_dir, export_dir, name):
        self.name = name
        pe = pefile.PE(os.path.join(bin_dir, name))
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.image_end = self.base + pe.OPTIONAL_HEADER.SizeOfImage
        self.sections = [(self.base + s.VirtualAddress, s.get_data()) for s in pe.sections]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.functions = {}  # entry -> (name, size)
        for line in open(os.path.join(export_dir, name + ".tsv")):
            cols = line.rstrip("\n").split("\t")
            if len(cols) >= 3 and all(c in "0123456789abcdef" for c in cols[0]):
                self.functions[int(cols[0], 16)] = (cols[1], int(cols[2]))
        self.tokens = {a: self._tokens(a, s) for a, (_, s) in self.functions.items()}

    def _bytes(self, addr, size):
        for start, blob in self.sections:
            if start <= addr < start + len(blob):
                return blob[addr - start:addr - start + size]
        return b""

    def _tokens(self, addr, size):
        out = []
        for insn in self.md.disasm(self._bytes(addr, size), addr):
            words = insn.op_str.replace("[", " ").replace("]", " ").replace(",", " ").replace("+", " ").replace("-", " ").split()
            ops = insn.op_str
            for w in words:
                if w.startswith("0x") and self.base <= int(w, 16) < self.image_end:
                    ops = ops.replace(w, "A")
            out.append(f"{insn.mnemonic} {ops}")
        return tuple(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        raise SystemExit(__doc__)
    bin_dir, export_dir = args[0], args[1]
    target_name = args[2] if len(args) > 2 else "PIRATES.EXE"
    ref_names = args[3:] or ["CADET.EXE", "DRAGON.EXE"]
    target = Exe(bin_dir, export_dir, target_name)
    refs = [Exe(bin_dir, export_dir, n) for n in ref_names]
    tags = [n[0] for n in ref_names]
    index = []  # per ref: tokens -> [addresses]
    for r in refs:
        d = {}
        for a, t in r.tokens.items():
            d.setdefault(t, []).append(a)
        index.append(d)

    counts = {}
    rows = []
    for addr in sorted(target.functions):
        name, size = target.functions[addr]
        t = target.tokens[addr]
        matches = []
        kind = "new"
        for tag, d in zip(tags, index):
            if t in d:
                matches.append(tag + ("@" if addr in d[t] else f"@{d[t][0]:x}"))
        if matches:
            kind = "same@" if matches[0].endswith("@") and matches[0].startswith(tags[0]) else "same"
            detail = " ".join(matches)
        else:
            best = (0.0, None, None)
            for tag, r in zip(tags, refs):
                cands = [addr] if addr in r.tokens else []
                cands += [a for a, rt in r.tokens.items() if abs(len(rt) - len(t)) <= max(8, len(t) // 4)]
                for ra in cands:
                    rt = r.tokens[ra]
                    sm = difflib.SequenceMatcher(None, t, rt, autojunk=False)
                    if sm.real_quick_ratio() < SIMILAR or sm.quick_ratio() < SIMILAR:
                        continue
                    ratio = sm.ratio()
                    if ratio > best[0]:
                        best = (ratio, tag, ra)
                if best[0] >= 0.999:
                    break
            if best[0] >= SIMILAR:
                kind = "changed"
                detail = f"best {best[1]}@{best[2]:x} {best[0]:.3f}"
            else:
                detail = ""
        in_first = bool(matches) and matches[0].startswith(tags[0])
        counts.setdefault(kind, [0, 0])
        counts[kind][0] += 1
        counts[kind][1] += size
        rows.append((addr, name, size, len(t), kind, detail, in_first))

    total = sum(b for _, b in counts.values())
    print(f"{target_name}: {len(target.functions)} functions, {total} bytes; refs: "
          + ", ".join(f"{tag}={n}" for tag, n in zip(tags, ref_names)))
    for kind in ("same@", "same", "changed", "new"):
        n, b = counts.get(kind, [0, 0])
        print(f"  {kind:8} {n:5} functions {b:7} bytes {100 * b / max(total, 1):5.1f}%")
    print("\naddress    name                     bytes insns kind     matches")
    for addr, name, size, n, kind, detail, in_first in rows:
        if in_first and "--all" not in sys.argv:
            continue
        print(f"{addr:08x}  {name:24} {size:6} {n:5} {kind:8} {detail}")


if __name__ == "__main__":
    main()
