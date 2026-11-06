#!/usr/bin/env python
"""Measure how much of Full Tilt's DRAGON.EXE / PIRATES.EXE is the same engine as CADET.EXE.

Space Cadet's CADET.EXE (Full Tilt, 32-bit) is what github.com/k4zmu2a/SpaceCadetPinball
decompiled. Functions found in each exe (call targets from the entry point, MSVC prologues,
code pointers in data) are fingerprinted with addresses masked, then matched against CADET.EXE:
  same      identical instruction sequence (engine code the decompilation already covers)
  similar   best match >= SIMILAR of the instruction sequence (engine code with small changes)
  new       no counterpart (table-specific logic that would have to be reverse engineered)

usage: fn_match.py FULLTILT_DIR [--list]
"""
import difflib
import hashlib
import os
import struct
import sys

import capstone
import pefile

SIMILAR = 0.80
TABLES = {"cadet": "CADET/CADET.EXE", "dragon": "DRAGON/DRAGON.EXE", "pirates": "PIRATE/PIRATES.EXE"}


class Exe:
    def __init__(self, path):
        pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        text = next(s for s in pe.sections if s.Characteristics & 0x20000000)  # executable
        self.code = text.get_data()[:text.Misc_VirtualSize]
        self.start = self.base + text.VirtualAddress
        self.end = self.start + len(self.code)
        self.entry = self.base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.data = [(self.base + s.VirtualAddress, s.get_data()) for s in pe.sections if s is not text]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.functions = self.find_functions()

    def in_text(self, addr):
        return self.start <= addr < self.end

    def find_functions(self):
        starts = {self.entry}
        code = self.code
        for i in range(len(code) - 3):  # push ebp; mov ebp, esp
            if code[i] == 0x55 and code[i + 1:i + 3] in (b"\x8b\xec", b"\x89\xe5"):
                starts.add(self.start + i)
        for base, blob in self.data:  # code pointers in data (vtables, callback tables)
            for i in range(0, len(blob) - 3, 4):
                v = struct.unpack_from("<I", blob, i)[0]
                if self.in_text(v):
                    starts.add(v)
        for insn in self.md.disasm(code, self.start):  # direct call targets
            if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
                target = int(insn.op_str, 16)
                if self.in_text(target):
                    starts.add(target)
        ordered = sorted(starts)
        return {a: (b if i + 1 < len(ordered) else self.end) - a
                for i, (a, b) in enumerate(zip(ordered, ordered[1:] + [self.end]))}

    def tokens(self, addr):
        """Instruction sequence with addresses / large constants masked."""
        size = self.functions[addr]
        blob = self.code[addr - self.start:addr - self.start + size]
        out = []
        for insn in self.md.disasm(blob, addr):
            ops = insn.op_str
            for word in ops.replace("[", " ").replace("]", " ").replace(",", " ").replace("+", " ").split():
                if word.startswith("0x") and int(word, 16) > 0xFFF:
                    ops = ops.replace(word, "A")
            out.append(f"{insn.mnemonic} {ops}")
            if insn.mnemonic in ("ret", "jmp") and insn.address + insn.size >= addr + size - 16:
                break
        return out


def fingerprint(tokens):
    return hashlib.sha1("\n".join(tokens).encode()).hexdigest()


def main():
    folder = sys.argv[1]
    exes = {name: Exe(os.path.join(folder, rel)) for name, rel in TABLES.items()}
    cadet = exes["cadet"]
    ref_tokens = {a: cadet.tokens(a) for a in cadet.functions}
    ref_hash = {}
    for a, t in ref_tokens.items():
        ref_hash.setdefault(fingerprint(t), []).append(a)
    by_len = sorted(ref_tokens.items(), key=lambda kv: len(kv[1]))
    print(f"cadet: {len(cadet.functions)} functions, {len(cadet.code)} code bytes")
    for name in ("dragon", "pirates"):
        exe = exes[name]
        stats = {"same": [0, 0], "similar": [0, 0], "new": [0, 0]}
        used = set()
        new_list = []
        for addr in sorted(exe.functions):
            t = exe.tokens(addr)
            size = exe.functions[addr]
            kind = "new"
            if fingerprint(t) in ref_hash:
                kind = "same"
                used.update(ref_hash[fingerprint(t)])
            elif len(t) >= 4:
                best = 0.0
                for ra, rt in by_len:
                    if abs(len(rt) - len(t)) > max(8, len(t) // 4):
                        continue
                    sm = difflib.SequenceMatcher(None, t, rt, autojunk=False)
                    if sm.quick_ratio() < SIMILAR:
                        continue
                    best = max(best, sm.ratio())
                    if best >= 0.99:
                        break
                if best >= SIMILAR:
                    kind = "similar"
            stats[kind][0] += 1
            stats[kind][1] += size
            if kind == "new":
                new_list.append((addr, size, len(t)))
        total = sum(v[1] for v in stats.values())
        print(f"\n{name}: {len(exe.functions)} functions, {total} code bytes")
        for kind, (n, b) in stats.items():
            print(f"  {kind:8} {n:5} functions  {b:7} bytes  {100 * b / total:5.1f}%")
        cadet_only = [a for a in cadet.functions if a not in used]
        print(f"  cadet functions with no identical copy in {name}: {len(cadet_only)} "
              f"({sum(cadet.functions[a] for a in cadet_only)} bytes)")
        if "--list" in sys.argv:
            for addr, size, n in sorted(new_list, key=lambda x: -x[1])[:40]:
                print(f"    new {addr:#010x} {size:6} bytes {n:5} insns")


if __name__ == "__main__":
    main()
