#!/usr/bin/env python
"""Disassemble a range of an i386 PE file (capstone), e.g. code the Ghidra export lacks
(timer callbacks only referenced as immediates) or functions Ghidra decompiled badly.

usage: disasm.py EXE START_HEX LENGTH_HEX
  e.g. re/work/.venv/bin/python re/disasm.py re/work/bin/DRAGON.EXE 41bc50 30

Prints address, raw bytes and the instruction, one per line. Reads the file only.
"""
import sys

import capstone
import pefile


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    pe = pefile.PE(sys.argv[1], fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    start, length = int(sys.argv[2], 16), int(sys.argv[3], 16)
    code = pe.get_data(start - base, length)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(code, start):
        print(f"{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    main()
