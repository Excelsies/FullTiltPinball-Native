#!/usr/bin/env python
"""Disassemble code ranges of one Full Tilt exe and check that other exes hold the same code there.

The part-class framework sits at the same addresses in CADET.EXE, DRAGON.EXE and PIRATES.EXE; this
answers "is function X really the same in all three?" for one range at a time, including code the
Ghidra export has no function for (e.g. timer callbacks only referenced by a push imm32).

Per range, each other exe is classed as
  identical    same bytes
  equivalent   same instructions; the only differences are relocated operands (bytes covered by
               the PE base-relocation table, i.e. data/vtable addresses) and rel32 call/jmp targets
               (the CRT and the per-table control module moved). The differing targets are listed.
  DIFFERENT    anything else; the first differing instructions are printed.

usage: disasm_cmp.py BIN_DIR ADDR[:END] [ADDR[:END] ...] [--exes A.EXE,B.EXE,...] [--export DIR]
                     [--list]
  BIN_DIR    folder with the .EXE files (e.g. re/work/bin)
  ADDR[:END] hex start (and exclusive end). Without END the function size is taken from
             <export>/<first exe>.tsv (ExportAll.java index) when the address is a function
             entry there, else disassembly stops after the first ret (at most 4096 bytes).
  --exes     comma-separated exe names; the first is the reference (default
             DRAGON.EXE,CADET.EXE,PIRATES.EXE)
  --export   folder with the .tsv indexes (default: none, so only the ret rule applies)
  --list     also print the reference disassembly of every range
example:
  re/work/.venv/bin/python re/disasm_cmp.py re/work/bin 0x4165a0 0x4166a0 --export re/work/export --list
"""
import os
import sys

import capstone
import pefile

MAX_SCAN = 4096


class Image:
    def __init__(self, path):
        self.name = os.path.basename(path)
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.relocated = set()  # VAs of every byte a base relocation patches
        for block in getattr(self.pe, "DIRECTORY_ENTRY_BASERELOC", []):
            for entry in block.entries:
                if entry.type == pefile.RELOCATION_TYPE["IMAGE_REL_BASED_HIGHLOW"]:
                    self.relocated.update(range(self.base + entry.rva, self.base + entry.rva + 4))

    def read(self, va, size):
        return self.pe.get_data(va - self.base, size)


def tsv_sizes(export_dir, exe_name):
    sizes = {}
    if export_dir:
        path = os.path.join(export_dir, exe_name + ".tsv")
        if os.path.exists(path):
            for line in open(path, encoding="latin-1"):
                cols = line.rstrip("\n").split("\t")
                if len(cols) >= 3 and cols[2].isdigit():
                    sizes[int(cols[0], 16)] = int(cols[2])
    return sizes


def disasm(md, img, start, end):
    """Instructions in [start, end); end None = up to and including the first ret."""
    limit = end if end is not None else start + MAX_SCAN
    out = []
    for ins in md.disasm(img.read(start, limit - start), start):
        out.append(ins)
        if end is None and ins.mnemonic in ("ret", "retf"):
            break
    if end is not None and (not out or out[-1].address + out[-1].size != end):
        raise SystemExit(f"{img.name} {start:#x}:{end:#x}: range does not end on an instruction boundary")
    return out


def compare(ref_ins, ref_img, other_img):
    """('identical'|'equivalent'|'DIFFERENT', notes)."""
    if not ref_ins:
        return "DIFFERENT", ["empty range"]
    start, end = ref_ins[0].address, ref_ins[-1].address + ref_ins[-1].size
    ref_bytes, other_bytes = ref_img.read(start, end - start), other_img.read(start, end - start)
    if ref_bytes == other_bytes:
        return "identical", []
    notes, verdict = [], "equivalent"
    for ins in ref_ins:
        a, b = ref_bytes[ins.address - start:][:ins.size], other_bytes[ins.address - start:][:ins.size]
        if a == b:
            continue
        diff = [ins.address + i for i in range(ins.size) if a[i] != b[i]]
        if all(va in ref_img.relocated and va in other_img.relocated for va in diff):
            continue  # data / vtable address
        if ins.mnemonic in ("call", "jmp") and a[0] in (0xe8, 0xe9) and a[0] == b[0] and ins.size == 5:
            tgt_b = ins.address + 5 + int.from_bytes(b[1:5], "little", signed=True)
            notes.append(f"{ins.address:#x} {ins.mnemonic} {ins.op_str} -> {other_img.name} {tgt_b:#x}")
            continue
        verdict = "DIFFERENT"
        notes.append(f"{ins.address:#x} {ins.mnemonic} {ins.op_str}  ({a.hex()} vs {b.hex()})")
        if len(notes) >= 8:
            break
    return verdict, notes


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = {}
    it = iter(argv)
    for a in it:
        if a in ("--exes", "--export"):
            opts[a] = next(it, None)
            args.remove(opts[a])
        elif a == "--list":
            opts[a] = True
        elif a.startswith("--"):
            raise SystemExit(f"unknown option {a}\n{__doc__}")
    if len(args) < 2:
        raise SystemExit(__doc__)
    bin_dir, ranges = args[0], args[1:]
    names = (opts.get("--exes") or "DRAGON.EXE,CADET.EXE,PIRATES.EXE").split(",")
    images = [Image(os.path.join(bin_dir, n)) for n in names]
    sizes = tsv_sizes(opts.get("--export"), names[0])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for spec in ranges:
        start_s, _, end_s = spec.partition(":")
        start = int(start_s, 16)
        end = int(end_s, 16) if end_s else (start + sizes[start] if start in sizes else None)
        ref = disasm(md, images[0], start, end)
        last = ref[-1].address + ref[-1].size if ref else start
        print(f"== {start:#x}:{last:#x} ({last - start} bytes, reference {images[0].name})")
        if "--list" in opts:
            for ins in ref:
                print(f"   {ins.address:08x}  {ins.bytes.hex():22s} {ins.mnemonic} {ins.op_str}")
        for img in images[1:]:
            verdict, notes = compare(ref, images[0], img)
            print(f"   {img.name}: {verdict}")
            for n in notes:
                print(f"      {n}")


if __name__ == "__main__":
    main(sys.argv[1:])
