#!/usr/bin/env python
"""Full Tilt message code -> upstream MessageCode map, per receiving class.

Extracts, for CADET.EXE / DRAGON.EXE / PIRATES.EXE:
  * per part class (vtables as in part_vtables.py; Message = vtable slot 1) the code values its
    Message accepts: 'case' labels and '==' / '!=' tests on param_1 in the Ghidra export, plus
    jump-table switches decoded from the machine code (jmp [r*4+T], optional byte index table,
    bias and bound read from the instructions before it);
  * every virtual Message call (push code; call [reg+4]) with a constant code, its receiver
    resolved to a control tag (tag + 4 = component slot) and that tag's DAT part class;
  * every control::handler(code, component) call with a constant code (engine -> rules), and the
    codes each control function / table control handler compares its code parameter with.
Then checks the hand-made pairing table MAP (FT code -> upstream enum name, or NEW:<name>) against
what was extracted: every extracted code must be mapped, every mapped code must be seen.

The pairing table holds enum names and addresses only (no game text, no decompiled code).

usage: ft_msgmap.py [--bin DIR] [--export DIR] [--dat-dir DIR] [--out-json F] [--out-md F] [--dump]
  defaults: re/work/bin, re/work/export, $FULLTILT_DIR or else re/work/game (the Full Tilt! data folder, read only),
  re/work/spec/msgmap.json, re/work/spec/msgmap.md
  --dump  print the extracted raw data (cases, sends) instead of writing outputs
"""
import argparse
import json
import os
import re
import struct
import sys

import capstone
import pefile

from class_map import functions, part_switch
from control_tables import PART_CLASS, dat_part_types
from part_vtables import KNOWN as VT_KNOWN, code_range, read_vtable, type_id, vtable_stores, MAX_SLOTS

HERE = os.path.dirname(os.path.abspath(__file__))
EXES = {"CADET": "CADET/CADET.DAT", "DRAGON": "DRAGON/DRAGON.DAT", "PIRATES": "PIRATE/PIRATES.DAT"}
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = False


def class_name(tid):
    names = {**VT_KNOWN, 1003: "TFlipper", 1029: "TKickout", 1034: "TPlunger1034",
             1035: "T1035", 1036: "T1036", 1037: "T1037Ball", 1038: "T1038", 1042: "T1042",
             1043: "T1043", 1041: "TFlipper"}
    return "TPinballComponent" if tid == 0 else names.get(tid, f"type{tid}")


class Exe:
    def __init__(self, name, bindir, exportdir):
        self.name = name
        self.pe = pefile.PE(os.path.join(bindir, name + ".EXE"))
        self.base, self.text = code_range(self.pe)
        self.funcs = functions(os.path.join(exportdir, name + ".EXE.c"))
        self.sizes = {}
        for line in open(os.path.join(exportdir, name + ".EXE.tsv")):
            f = line.split("\t")
            self.sizes[int(f[0], 16)] = int(f[2])

    def data(self, va, n):
        return self.pe.get_data(va - self.base, n)

    def dword(self, va):
        return struct.unpack("<I", self.data(va, 4))[0]

    def insns(self, start, size):
        """Linear disassembly of [start, start+size), stopping at a decoded jump table."""
        out = []
        stop = start + size
        for ins in MD.disasm(self.data(start, size), start):
            if ins.address >= stop:
                break
            out.append(ins)
            m = re.match(r"dword ptr \[e\w\w\*4 \+ (0x[0-9a-f]+)\]", ins.op_str)
            if ins.mnemonic == "jmp" and m:
                stop = min(stop, int(m.group(1), 16))
        return out

    def regions(self):
        """Ghidra functions plus the gaps between them (timer callbacks referenced only as
        immediates have no Ghidra function), as (start, size); gaps skip int3/nop padding."""
        out = []
        starts = sorted(self.sizes)
        for a, b in zip(starts, starts[1:] + [self.text[1]]):
            out.append((a, self.sizes[a]))
            g = a + self.sizes[a]
            if self.sizes[a] <= 0 or g >= b:
                continue
            gap = self.data(g, b - g)
            k = 0
            while k < len(gap) and gap[k] in (0xCC, 0x90):
                k += 1
            if k < len(gap):
                out.append((g + k, b - g - k))
        return out

    # ---------------------------------------------------------------- part classes
    def message_fns(self):
        """{class name: Message function} for every part-class vtable."""
        stores = vtable_stores(self.funcs)
        starts = sorted(stores)
        out = {}
        for v, nxt in zip(starts, starts[1:] + [starts[-1] + 4 * MAX_SLOTS]):
            ent = read_vtable(self.pe, self.base, self.text, v, min(MAX_SLOTS, (nxt - v) // 4))
            if len(ent) < 2:
                continue
            tid = type_id(self.pe, self.base, ent[0])
            if tid is None or len(ent) > 9 or not (tid == 0 or 1000 <= tid < 1100):
                continue
            name = class_name(tid)
            if tid == 0:
                name = "TCollisionComponent" if len(ent) == 8 else "TPinballComponent"
            out[name] = ent[1]
        return out


def code_param(body):
    """Name of the message-code parameter: the first int parameter after 'this'."""
    m = re.search(r"\w+ (?:__thiscall|__cdecl|__stdcall|__fastcall)? ?\w+\(([^)]*)\)", body)
    if not m:
        return "param_1"
    params = [p.strip().split()[-1].lstrip("*") for p in m.group(1).split(",") if p.strip()]
    if "__thiscall" in body.split("(")[0] and len(params) > 1:
        return params[1]
    return params[0] if params else "param_1"


NUM = r"(-?0x[0-9a-f]+|-?\d+)"


def ghidra_cases(body, ranges=None):
    """Code values a decompiled function compares its code parameter with (case labels, ==, !=).
    Range tests (<, <=, >, >=) are collected into 'ranges' when given."""
    var = code_param(body)
    vals = set()
    for m in re.finditer(r"\bcase " + NUM + ":", body):
        vals.add(int(m.group(1), 0))
    for m in re.finditer(r"\b" + var + r" [!=]= " + NUM + r"\b", body):
        vals.add(int(m.group(1), 0))
    for m in re.finditer(r"\b" + NUM + r" [!=]= " + var + r"\b", body):
        vals.add(int(m.group(1), 0))
    if ranges is not None:
        for m in re.finditer(r"\b" + var + r" (<=?|>=?) " + NUM + r"\b", body):
            ranges.add(f"{var}{m.group(1)}{m.group(2)}")
        for m in re.finditer(r"\b" + NUM + r" (<=?|>=?) " + var + r"\b", body):
            ranges.add(f"{m.group(1)}{m.group(2)}{var}")
    return vals


def imm(op):
    try:
        return int(op, 0)
    except ValueError:
        return None


def jump_tables(exe, start, size):
    """[(case value, target)] of every jmp [r*4+T] switch in the function (machine code)."""
    ins = exe.insns(start, size)
    out = []
    for i, x in enumerate(ins):
        m = re.match(r"dword ptr \[(e\w\w)\*4 \+ (0x[0-9a-f]+)\]", x.op_str)
        if x.mnemonic != "jmp" or not m:
            continue
        reg, table = m.group(1), int(m.group(2), 16)
        idx_table, bound, bias, default = None, None, 0, None
        key = reg
        for y in reversed(ins[max(0, i - 10):i]):
            ops = [o.strip() for o in y.op_str.split(",")]
            mi = re.match(r"byte ptr \[(e\w\w) \+ (0x[0-9a-f]+)\]", ops[-1]) if len(ops) == 2 else None
            if y.mnemonic in ("mov", "movzx") and mi and idx_table is None and \
                    ops[0][-1] == key[-2] or (mi and y.mnemonic == "movzx" and idx_table is None):
                idx_table, key = int(mi.group(2), 16), mi.group(1)
                continue
            if y.mnemonic == "ja" and default is None:
                default = imm(y.op_str)
            elif y.mnemonic == "cmp" and ops[0] == key and bound is None and imm(ops[1]) is not None:
                bound = imm(ops[1])
            elif y.mnemonic == "add" and ops[0] == key and bound is not None and imm(ops[1]) is not None:
                v = imm(ops[1])
                bias = -(v - (1 << 32) if v >= 1 << 31 else v)
                break
            elif y.mnemonic == "sub" and ops[0] == key and bound is not None and imm(ops[1]) is not None:
                bias = imm(ops[1])
                break
            elif y.mnemonic == "lea" and ops[0] == key and bound is not None:
                ml = re.match(r"\[e\w\w ([+-]) (0x[0-9a-f]+)\]", ops[1])
                if ml:
                    bias = int(ml.group(2), 16) * (1 if ml.group(1) == "-" else -1)
                break
        if bound is None:
            continue
        for k in range(bound + 1):
            slot = exe.data(idx_table + k, 1)[0] if idx_table is not None else k
            target = exe.dword(table + 4 * slot)
            if target != default:
                out.append((k + bias, target))
    return out


def message_cases(exe, fn, ranges=None):
    vals = {v: None for v in ghidra_cases(exe.funcs.get(fn, ""), ranges)}
    for v, t in jump_tables(exe, fn, exe.sizes.get(fn, 0)):
        vals[v] = t
    return vals


# -------------------------------------------------------------------- sends
def resolve_reg(window, reg):
    """Constant or memory source last assigned to reg in a straight-line window (newest last)."""
    for y in reversed(window):
        ops = [o.strip() for o in y.op_str.split(",")]
        if len(ops) == 2 and ops[0] == reg and y.mnemonic == "mov":
            return ops[1]
        if len(ops) == 2 and ops[0] == reg and y.mnemonic == "xor" and ops[1] == reg:
            return "0x0"
    return None


def call_sites(exe, lo, hi):
    """Yield (region start, call insn, preceding window, pushes in it) for every call in
    [lo, hi), Ghidra functions and the gaps between them."""
    for fn, size in exe.regions():
        if not lo <= fn < hi or size <= 0:
            continue
        ins = exe.insns(fn, size)
        for i, x in enumerate(ins):
            if x.mnemonic != "call":
                continue
            window = ins[max(0, i - 14):i]
            pushes = [y for y in window if y.mnemonic == "push"]
            yield fn, x, window, pushes


def operand_value(window, op):
    if op is None:
        return None
    v = imm(op)
    if v is not None:
        return v
    if re.fullmatch(r"e\w\w", op):
        src = resolve_reg(window, op)
        return imm(src) if src else None
    return None


def receiver(window, exe_tags):
    """Tag name of the component in ecx, from 'mov ecx, [slot]' or 'mov ecx, reg' chains."""
    src = resolve_reg(window, "ecx")
    for _ in range(3):
        if src is None:
            return None
        m = re.fullmatch(r"dword ptr \[(0x[0-9a-f]+)\]", src)
        if m:
            return exe_tags.get(int(m.group(1), 16), "@" + m.group(1))
        if re.fullmatch(r"e\w\w", src):
            src = resolve_reg(window, src)
            continue
        return src
    return src


def message_sends(exe, tags, lo, hi):
    """Virtual Message calls (call [reg+4]) with their code, value and receiver."""
    out = []
    for fn, x, window, pushes in call_sites(exe, lo, hi):
        if not re.fullmatch(r"dword ptr \[e\w\w \+ 4\]", x.op_str) or not pushes:
            continue
        code = operand_value(window, pushes[-1].op_str)
        if code is None:
            continue
        out.append({"site": x.address, "fn": fn, "code": code, "recv": receiver(window, tags)})
    return out


def handler_sends(exe, handler, lo, hi):
    out = []
    for fn, x, window, pushes in call_sites(exe, lo, hi):
        if imm(x.op_str) != handler or not pushes:
            continue
        code = operand_value(window, pushes[-1].op_str)
        out.append({"site": x.address, "fn": fn, "code": code})
    return out


def direct_sends(exe, targets, lo, hi):
    """Direct calls target(code, component) to rules functions with a constant code."""
    out = []
    for fn, x, window, pushes in call_sites(exe, lo, hi):
        t = imm(x.op_str)
        if t not in targets or not pushes:
            continue
        code = operand_value(window, pushes[-1].op_str)
        if code is not None:
            out.append({"site": x.address, "fn": fn, "target": t, "code": code})
    return out


def handler_callees(exe, handler, lo, hi):
    """Functions control::handler calls inside the table module (the rules hook, MissionControl)."""
    out = set()
    for x in exe.insns(handler, exe.sizes[handler]):
        t = imm(x.op_str) if x.mnemonic == "call" else None
        if t is not None and lo <= t < hi:
            out.add(t)
    return out


# -------------------------------------------------------------------- tables
def load_tags(name, specdir, datdir):
    """{component slot address: (tag name, part class)} for the exe's control tags."""
    slots = {}
    js = os.path.join(specdir, f"control_tables_{name.lower()}.json")
    if os.path.exists(js):
        d = json.load(open(js))
        for t in d["tags"]:
            slots[t["addr"] + 4] = (t["name"], t["part_class"])
        handlers = sorted({e["handler"] for e in d["score_entries"]})
        return slots, handlers, d["make_links"]
    # PIRATES: control_links output + DAT part types
    types = dat_part_types(os.path.join(datdir, EXES[name]))
    handlers, make_links = set(), None
    for line in open(os.path.join(specdir, "pirates_raw", f"control_links_{name}.txt")):
        m = re.match(r"(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)(?:\s+(0x[0-9a-f]+))?", line)
        if m:
            t = types.get(m.group(3))
            slots[int(m.group(2), 16)] = (m.group(3), PART_CLASS.get(t, str(t)))
            if m.group(4):
                handlers.add(int(m.group(4), 16))
        mm = re.match(r"make_links (0x[0-9a-f]+)", line)
        if mm:
            make_links = int(mm.group(1), 16)
    return slots, sorted(handlers), make_links


def find_handler(exe, make_links):
    """control::handler = the function right after make_links (CADET 0x422bc0, DRAGON 0x423060)."""
    nxt = min(a for a in exe.sizes if a > make_links)
    body = exe.funcs.get(nxt, "")
    if "+ 0x10))" not in body and "0x3fd" not in body:
        raise SystemExit(f"{exe.name}: control::handler not found after make_links {make_links:#x}")
    return nxt


def extract(args):
    raw = {}
    specdir = os.path.dirname(os.path.abspath(args.out_json))
    for name in EXES:
        exe = Exe(name, args.bin, args.export)
        slots, handlers, make_links = load_tags(name, specdir, args.dat_dir)
        tags = {a: f"{n}:{c}" for a, (n, c) in slots.items()}
        mf = exe.message_fns()
        handler = find_handler(exe, make_links)
        classes = {}
        for cls, fn in sorted(mf.items()):
            rng = set()
            cases = message_cases(exe, fn, rng)
            classes[cls] = {"message": fn, "cases": {v: t for v, t in sorted(cases.items())},
                            "ranges": sorted(rng)}
        sends = message_sends(exe, tags, exe.text[0], exe.text[1])
        hs = handler_sends(exe, handler, exe.text[0], exe.text[1])
        module_end = exe.text[1]   # rules module runs from make_links into the CRT; scan to .text end
        hooks = handler_callees(exe, handler, make_links, module_end)
        ds = direct_sends(exe, set(handlers) | hooks, make_links, module_end)
        ctl = {}
        for h in handlers:
            ctl[h] = sorted(ghidra_cases(exe.funcs.get(h, "")))
        raw[name] = {"classes": classes, "sends": sends, "handler": handler,
                     "handler_sends": hs, "control_cases": ctl, "make_links": make_links,
                     "hooks": sorted(hooks), "direct_sends": ds}
    return raw


def dump(raw):
    for name, r in raw.items():
        print(f"===== {name}  control::handler {r['handler']:#x}")
        for cls, c in r["classes"].items():
            cs = " ".join(f"{v:#x}" + ("" if t is None else f"@{t:x}") for v, t in c["cases"].items())
            print(f"  {cls:20} Message {c['message']:#x}: {cs}  ranges {' '.join(c['ranges'])}")
        print("  -- handler sends")
        for s in r["handler_sends"]:
            print(f"    {s['site']:#x} fn {s['fn']:#x} code {s['code'] if s['code'] is None else hex(s['code'])}")
        print("  -- message sends (code: receivers)")
        by = {}
        for s in r["sends"]:
            cls = (s["recv"] or "?").split(":")[-1] if s["recv"] and ":" in s["recv"] else (s["recv"] or "?")
            by.setdefault((cls, s["code"]), []).append(s["site"])
        for (cls, code), sites in sorted(by.items(), key=lambda k: (str(k[0][0]), k[0][1])):
            print(f"    {cls:22} {code:#6x} x{len(sites):<3} " + " ".join(f"{a:x}" for a in sites[:6]))
        print(f"  -- direct rules calls (hooks {' '.join(hex(h) for h in r['hooks'])})")
        dd = {}
        for s in r["direct_sends"]:
            dd.setdefault((s["target"], s["code"]), []).append(s["site"])
        for (t, c), sites in sorted(dd.items()):
            print(f"    -> {t:#x} code {c:#x} x{len(sites):<3} " + " ".join(f"{a:x}" for a in sites[:6]))
        print("  -- control function cases")
        allc = {}
        for h, cs in r["control_cases"].items():
            for c in cs:
                allc.setdefault(c, []).append(h)
        for c, hs in sorted(allc.items()):
            print(f"    {c:#6x} x{len(hs):<3} " + " ".join(f"{a:x}" for a in hs[:8]))


# ==================================================================== pairing table
# FT code -> (upstream MessageCode name | "NEW:<name>", how it was paired). Paired by the body's
# effect against SpaceCadetPinball/*.cpp; see re/work/spec/msgmap.md for the evidence notes.
# Proposed value of every NEW member: FT code + 0x100 (private codes land in 0x100-0x156, above
# upstream's 0-67; public ones in 0x4f7/0x4fd, above upstream's 1000-1024): no collision.
NEW_OFFSET = 0x100

PUBLIC = {
    0x3e8: ("LeftFlipperInputPressed", "same value; table 0x419600 -> left flipper group"),
    0x3e9: ("LeftFlipperInputReleased", "same value"),
    0x3ea: ("RightFlipperInputPressed", "same value"),
    0x3eb: ("RightFlipperInputReleased", "same value"),
    0x3ec: ("PlungerInputPressed", "same value; table forwards to the plunger"),
    0x3ed: ("PlungerInputReleased", "same value"),
    0x3f0: ("Pause", "same value; table broadcasts 0x3f0-0x3f2"),
    0x3f1: ("Resume", "same value; CADET MissionControl 0x42a260 compares 0x3f1 where upstream has Resume"),
    0x3f2: ("LooseFocus", "same value"),
    0x3f3: ("SetTiltLock", "same value"),
    0x3f4: ("ClearTiltLock", "same value; rules send it to TableG"),
    0x3f5: ("StartGamePlayer1", "same value; table self-send 0x419855"),
    0x3f6: ("NewGame", "same value; table -> Demo"),
    0x3f7: ("NEW:TableBuilt", "only table ctor 0x41a290 -> table_control_handler at 0x41ab2f; no part Message handles it"),
    0x3f8: ("PlungerFeedBall", "+1: TPlunger feed timer self-send 0x418916; CADET PlungerControl compares 0x3f8"),
    0x3f9: ("PlungerStartFeedTimer", "+1: drain rules -> plunger; CADET PlungerControl compares 0x3f9"),
    0x3fa: ("PlungerLaunchBall", "+1: pb launch 0x412a97 -> plunger (upstream pb.cpp PlungerLaunchBall)"),
    0x3fb: ("PlungerRelaunchBall", "+1: rules UnstuckBall / drain -> plunger"),
    0x3fc: ("PlungerRelaunchBall", "table only: replay timer (upstream TPinballTable case PlungerRelaunchBall); no literal sender in any exe"),
    0x3fd: ("NEW:ControlReloadScores", "control::handler branch (put_scoring before ControlFunc); no literal sender in any exe"),
    0x3fe: ("PlayerChanged", "+2"),
    0x3ff: ("SwitchToNextPlayer", "+2; rules -> TableG"),
    0x400: ("GameOver", "+2; EndGame timer 0x419570 broadcasts it"),
    0x402: ("Reset", "+2"),
}
LIGHT = {c: (n, "same value (TLight 0x40a490 jump table, order and bodies as upstream)") for c, n in {
    0: "TLightTurnOff", 1: "TLightTurnOn", 2: "TLightGetLightOnFlag", 3: "TLightGetFlasherOnFlag",
    4: "TLightFlasherStart", 5: "TLightApplyMultDelay", 6: "TLightApplyDelay",
    7: "TLightFlasherStartTimed", 8: "TLightTurnOffTimed", 9: "TLightTurnOnTimed",
    11: "TLightSetOnStateBmpIndex", 12: "TLightIncOnStateBmpIndex", 13: "TLightDecOnStateBmpIndex",
    14: "TLightResetTimed", 15: "TLightFlasherStartTimedThenStayOn",
    16: "TLightFlasherStartTimedThenStayOff", 17: "TLightToggleValue",
    18: "TLightResetAndToggleValue", 19: "TLightResetAndTurnOn", 20: "TLightResetAndTurnOff",
    21: "TLightToggle", 22: "TLightResetAndToggle", 23: "TLightSetMessageField"}.items()}
LIGHT.update({
    0x18: ("TLightFtTmpOverrideOn", "FT 24 = upstream -24: sprite BmpArr[1] (+0x66), undo timer 0x40a420"),
    0x19: ("TLightFtTmpOverrideOff", "FT 25 = upstream -25: sprite BmpArr[0] (+0x62)"),
    0x1a: ("TLightFtResetOverride", "FT 26 = upstream -26: kill undo timer, restore sprite"),
})
GROUP = {c + 4: (n, "+4 (TLightGroup 0x40af80 jump table 0x1c-0x32, same order as upstream 24-46)")
         for c, n in {24: "TLightGroupStepBackward", 25: "TLightGroupStepForward",
                      26: "TLightGroupAnimationBackward", 27: "TLightGroupAnimationForward",
                      28: "TLightGroupLightShowAnimation", 29: "TLightGroupGameOverAnimation",
                      30: "TLightGroupRandomAnimationSaturation",
                      31: "TLightGroupRandomAnimationDesaturation",
                      32: "TLightGroupOffsetAnimationForward",
                      33: "TLightGroupOffsetAnimationBackward", 34: "TLightGroupReset",
                      35: "TLightGroupTurnOnAtIndex", 36: "TLightGroupTurnOffAtIndex",
                      37: "TLightGroupGetOnCount", 38: "TLightGroupGetLightCount",
                      39: "TLightGroupGetMessage2", 40: "TLightGroupGetAnimationFlag",
                      41: "TLightGroupResetAndTurnOn", 42: "TLightGroupResetAndTurnOff",
                      43: "TLightGroupRestartNotifyTimer", 44: "TLightGroupFlashWhenOn",
                      45: "TLightGroupToggleSplitIndex", 46: "TLightGroupStartFlasher"}.items()}
GROUP[0x26] = ("TLightGroupReset", "+4: table StartGamePlayer1/Reset send 0x26 where upstream sends TLightGroupReset")
GROUP[0x29] = ("TLightGroupGetOnCount", "+4: case counts lights with +0x42 set")
GROUP[0x31] = ("TLightGroupToggleSplitIndex", "+4: case calls control::handler(code) at 0x40baf6 like upstream")
GROUP[0x32] = ("TLightGroupStartFlasher", "+4: next_light_up() -> TLightFlasherStart(4)")
ENABLE = {
    0x4b: ("NEW:FtPartEnable", "Full Tilt enable; ActiveFlag (+5) = 1 (TKickback: clears +0x86 and resets)"),
    0x4c: ("NEW:FtPartDisable", "Full Tilt disable; ActiveFlag = 0 (TKickback: sets +0x86)"),
}
CONTROL = {
    0x00: ("TLightGroupNull", "same value: CADET direct HyperspaceLightGroupControl(0) 0x4241f9; upstream has the case but no such call site (UNVERIFIED which upstream site it is)"),
    0x13: ("TLightResetAndTurnOn", "same value: CADET direct ExtraBallLightControl calls (upstream x2)"),
    0x2d: ("TLightGroupResetAndTurnOn", "+4: CADET direct light-group control calls (upstream x3)"),
    0x37: ("TBlockerDisable", "+4: DRAGON direct calls to blocker control fns 0x425910/0x425a10/0x425b10"),
    0x38: ("TBlockerEnable", "+4: CADET DrainBallBlockerControl 0x423480 (upstream TBlockerEnable direct calls); DRAGON blocker control fns"),
    0x01: ("TFlipperExtend", "same value: TFlipper 0x408100 -> control::handler(1) at 0x40814b"),
    0x0b: ("TBumperSetBmpIndex", "TBumper Message passes its code to control::handler 0x41424a (register; UNVERIFIED which of 0xb-0xd)"),
    0x31: ("TLightGroupToggleSplitIndex", "+4: TLightGroup 0x40baf6 forwards its code"),
    0x33: ("TLightGroupCountdownEnded", "+4: TLightBargraph timer 0x40be54; CADET MissionControl compares 0x33"),
    0x3d: ("NEW:ControlBlockerMoveDone", "FT only: TBlocker step timer 0x414f00 when the frame reaches the target (0x414f1e)"),
    0x3e: ("TGateDisable", "+9: TGate forwards its code at 0x4154bc (register; UNVERIFIED)"),
    0x3f: ("TGateEnable", "+9: as 0x3e"),
    0x42: ("ControlBallCaptured", "+9: THole::Collision 0x416a63"),
    0x44: ("ControlBallReleased", "+10: THole eject 0x416dfc"),
    0x46: ("ControlTimerExpired", "+10: TLight/TBlocker/TKickback/TSink/TDrain/TTimer/TTextBox timer callbacks"),
    0x47: ("ControlNotifyTimerExpired", "+10: TLightGroup 0x40af60 and TComponentGroup 0x413750 notify-timer callbacks"),
    0x49: ("ControlSpinnerLoopReset", "+11: TFlagSpinner 0x414768"),
    0x4a: ("ControlCollision", "+11: every Collision handler"),
    0x4b: ("ControlEnableMultiplier", "+11: CADET direct calls GravityWellKickoutControl / MultiplierLightGroupControl"),
    0x4c: ("ControlDisableMultiplier", "+11: CADET direct call MultiplierLightGroupControl 0x4249d3"),
    0x4d: ("NEW:ControlScorerReplaced", "FT only: score helpers send it to the previous scoring part's control fn"),
    0x4f: ("ControlMissionComplete", "+13: SetMode/enter-mode calls to MissionControl (CADET 0x42a260 x35)"),
    0x50: ("ControlMissionStarted", "+13: EndGame timer 0x4195e3 (upstream EndGame_timeout); PIRATES 0x42d8ab turns info-box 0x46 into 0x50 like upstream MissionControl"),
    0x51: ("NEW:ControlModeResume", "FT only (DRAGON): resume the saved quest from the restart window"),
    0x52: ("NEW:ControlModeEnd", "FT only: end the current mode (PIRATES hook 0x42d850 then enters mode 2 with 0x4f)"),
    0x53: ("NEW:ControlBallDrained", "FT only: drain rules -> MissionControl(0x53, drain)"),
    0x54: ("NEW:ControlRampPlaneLayer", "FT only: TRamp::Collision 0x417925"),
    0x55: ("NEW:ControlBallToLayer1", "FT only: TRamp 0x4179a7, TRampExit 0x41b7a2"),
    0x56: ("NEW:ControlBallToLayer2", "FT only: TRamp 0x4179e6, TRampExit 0x41b782"),
    0x3ec: PUBLIC[0x3ec], 0x3ed: PUBLIC[0x3ed], 0x3f1: PUBLIC[0x3f1], 0x3f2: PUBLIC[0x3f2],
    0x3f3: PUBLIC[0x3f3], 0x3f8: PUBLIC[0x3f8], 0x3f9: PUBLIC[0x3f9], 0x3fa: PUBLIC[0x3fa],
    0x3fb: PUBLIC[0x3fb], 0x3fd: PUBLIC[0x3fd], 0x3fe: PUBLIC[0x3fe], 0x402: PUBLIC[0x402],
}
TABLE_CONTROL = {c: PUBLIC[c] for c in (0x3f3, 0x3f6, 0x3f7, 0x400)}

PRIV = {
    "TFlipper": {1: ("TFlipperExtend", "same value"), 2: ("TFlipperRetract", "same value")},
    "TBumper": {0xb: ("TBumperSetBmpIndex", "same value"), 0xc: ("TBumperIncBmpIndex", "same value"),
                0xd: ("TBumperDecBmpIndex", "same value")},
    "TComponentGroup": {0x34: ("TComponentGroupResetNotifyTimer", "+4: restarts notify timer 0x413750")},
    "TPopupTarget": {0x35: ("TPopupTargetDisable", "+4"), 0x36: ("TPopupTargetEnable", "+4")},
    "TSoloTarget": {0x35: ("TSoloTargetDisable", "+4"), 0x36: ("TSoloTargetEnable", "+4")},
    "TBlocker": {
        0x37: ("TBlockerDisable", "+4: move to frame -1 (timed if value > 0), disable sound"),
        0x38: ("TBlockerEnable", "+4: move to the last frame, enable sound"),
        0x39: ("NEW:TBlockerSetFrame", "FT only: frame = (int)value now, stop the step timer"),
        0x3a: ("NEW:TBlockerMoveToFrame", "FT only: step towards frame (int)value on timer 0x414f00"),
        0x3b: ("NEW:TBlockerEnableNow", "FT only: SetFrame(last) + enable sound"),
        0x3c: ("NEW:TBlockerDisableNow", "FT only: SetFrame(-1) + disable sound"),
        0x45: ("TBlockerRestartTimeout", "+10: kill/restart timer 0x414ee0 (-> 0x46)"),
    },
    "TGate": {0x3e: ("TGateDisable", "+9"), 0x3f: ("TGateEnable", "+9")},
    "TKickout": {0x40: ("TKickoutRestartTimer", "+9: start kick timer (value or default)"),
                 0x4e: ("NEW:TKickoutDestroyBall", "FT only: drop the held ball (Disable, table+0x192--); no literal sender in any exe"),
                 **ENABLE},
    "TKickback": dict(ENABLE),
    "TMagnet": dict(ENABLE),
    "TDiverter": {0x37: ("TBlockerDisable", "+4: diverter accepts blocker disable"),
                  0x38: ("TBlockerEnable", "+4: diverter accepts blocker enable (PlayerChanged re-sends 0x37/0x38)"),
                  0x3e: ("TGateDisable", "+9: the code Dragon's rules send"),
                  0x3f: ("TGateEnable", "+9: the code Dragon's rules send"), **ENABLE},
    "THole": {0x43: ("NEW:THoleEjectBall", "FT only: eject the held ball after (value) s, now if value <= 0"),
              0x3f8: ("PlungerFeedBall", "THole-internal: eject now, re-queued by 0x4131a0 while blocked (timer 0x416b87)")},
    "TSink": {7: ("TSinkUnknown7", "same value; ignored by FT and upstream TSink"),
              0x41: ("TSinkResetTimer", "+9: same body as upstream")},
    "TTimer": {0x45: ("TTimerResetTimer", "+10: same body as upstream"),
               0x48: ("NEW:TTimerGetTimeLeft", "FT only: returns remaining time in milliseconds, (int)floor(max(left, 0.0) * 1000.0) (fmul by the double 1000.0 at 0x4377a0; PIRATES 0x4387a0)")},
    "TLightSweeps": {0x1b: ("NEW:TLightSweepsStart", "FT only: start sweep (int)value (part_1038.md)")},
    "TLightBargraph": {0x29: ("TLightGroupGetOnCount", "+4: returns TimeIndex like upstream"),
                       0x31: ("TLightGroupToggleSplitIndex", "+4: restart bargraph at index")},
}
# classes whose Message forwards unknown codes: (forward target dicts, note)
FORWARD = {"TLightGroup": [LIGHT], "TLightBargraph": [GROUP, LIGHT], "TComponentGroup": [PRIV["TBumper"], LIGHT]}
# extracted values that are not message codes
NOT_CODES = {"TPinballTable": {0: "player-index jump table 0x419c95", 1: "same", 2: "same", 3: "same"}}
DISPLAY = {"T1035": "TRampExit", "T1036": "TAccelerator", "T1037Ball": "TBall", "T1038": "TLightSweeps",
           "T1042": "TMagnet", "T1043": "TDiverter", "TPlunger1034": "TPlunger"}
TAG_CLASS = {"TFlipper L": "TFlipper", "TFlipper R": "TFlipper", "TKickout (no ball)": "TKickout",
             "TPlunger(flag 1)": "TPlunger", "type1035": "TRampExit", "type1036": "TAccelerator",
             "type1037": "TBall", "type1038": "TLightSweeps", "type1042": "TMagnet",
             "type1043": "TDiverter"}


def lookup(cls, code):
    dicts = [PRIV.get(cls, {})]
    if cls == "TLight":
        dicts.append(LIGHT)
    if cls == "TLightGroup":
        dicts.append(GROUP)
    dicts += FORWARD.get(cls, [])
    dicts.append(PUBLIC)
    for d in dicts:
        if code in d:
            return d[code]
    return None


def build(raw):
    """Per-class map from the extracted data; returns (classes, problems)."""
    classes, problems = {}, []
    for exe, r in raw.items():
        for cname, c in r["classes"].items():
            cls = DISPLAY.get(cname, cname)
            ent = classes.setdefault(cls, {"message": hex(c["message"]), "codes": {}})
            for code, tgt in c["cases"].items():
                if code in NOT_CODES.get(cls, {}):
                    continue
                e = ent["codes"].setdefault(code, {"handled": True, "evidence": set(), "sent_by": {}})
                e["handled"] = True
                e["evidence"].add(f"case@{tgt:#x}" if tgt else f"Message {c['message']:#x}")
        for s in r["sends"]:
            recv = s["recv"] or ""
            if ":" not in recv:
                continue
            cls = TAG_CLASS.get(recv.split(":", 1)[1], recv.split(":", 1)[1])
            if cls not in classes:
                continue
            e = classes[cls]["codes"].setdefault(s["code"], {"handled": False, "evidence": set(), "sent_by": {}})
            e["sent_by"].setdefault(exe, []).append(s["site"])
    ctl = classes.setdefault("control", {"message": "control::handler / control functions", "codes": {}})
    for exe, r in raw.items():
        for s in r["handler_sends"] + r["direct_sends"]:
            if s["code"] is None:
                continue
            e = ctl["codes"].setdefault(s["code"], {"handled": True, "evidence": set(), "sent_by": {}})
            e["sent_by"].setdefault(exe, []).append(s["site"])
    for code, ev in ((0x3f8, "TPlunger forwards its code to control::handler at 0x418d25"),
                     (0x3f9, "TPlunger forwards its code at 0x418d25; CADET PlungerControl compares it"),
                     (0x3fa, "TPlunger forwards its code at 0x418d25"),
                     (0x3fb, "TPlunger forwards its code at 0x418d25"),
                     (0x3fd, "control::handler compares 0x3fd (CADET 0x422bd8, DRAGON 0x42307f, PIRATES 0x42309f)")):
        ctl["codes"].setdefault(code, {"handled": True, "evidence": set(), "sent_by": {}})["evidence"].add(ev)
    tch = classes.setdefault("table_control_handler", {"message": "control::table_control_handler", "codes": {}})
    for code in TABLE_CONTROL:
        tch["codes"].setdefault(code, {"handled": True, "evidence": {"dragon_framework.md 5.1"}, "sent_by": {}})
    out = {}
    for cls, ent in sorted(classes.items()):
        rows = {}
        for code, e in sorted(ent["codes"].items()):
            if cls == "control":
                m = CONTROL.get(code)
            elif cls == "table_control_handler":
                m = TABLE_CONTROL.get(code)
            else:
                m = lookup(cls, code)
            if m is None:
                problems.append(f"{cls}: FT code {code:#x} has no pairing")
                m = ("UNMAPPED", "")
            name = m[0]
            row = {"upstream": name, "note": m[1], "handled_by_message": e["handled"],
                   "evidence": sorted(e["evidence"]),
                   "sent_at": {x: [hex(a) for a in sorted(set(v))[:4]] + (["..."] if len(set(v)) > 4 else [])
                               for x, v in sorted(e["sent_by"].items())}}
            if name.startswith("NEW:"):
                row["proposed_value"] = hex(code + NEW_OFFSET)
            rows[f"{code:#x}"] = row
        out[cls] = {"message": ent["message"], "codes": rows}
    for cls, d in PRIV.items():
        for code in d:
            if cls in out and f"{code:#x}" not in out[cls]["codes"]:
                problems.append(f"{cls}: mapped FT code {code:#x} never seen")
    return out, problems


NOTES = """## Resolved contradictions

(a) Plunger / system codes: **+1 for the plunger codes, 0x3fc is the table's replay timer only.**
TPlunger::Message 0x418950 handles 0x3f8, 0x3f9, 0x3fa, 0x3fb and not 0x3fc (case set above, same in
all three exes). CADET's PlungerControl compares 0x3f8 / 0x3f9 where upstream PlungerControl compares
PlungerFeedBall (1015) / PlungerStartFeedTimer (1016). The plunger feed timer self-sends 0x3f8
(0x418916), pb's launch path sends 0x3fa (0x412a97, upstream pb.cpp PlungerLaunchBall), the rules
send 0x3f9 and 0x3fb. Only TPinballTable::Message has `case 0x3fc` (kill +0x1a6, start replay
timer, +0x1a2 = 1: upstream's `case PlungerRelaunchBall` body in TPinballTable). A raw imm32 search
for 0x3fc finds no sender in any exe (only the part-type stub 0x41ae91, a stack offset 0x41e7ea,
string id 1020 at 0x421ece, and PIRATES GetTypeId compares 0x42b12f/0x42d063). Upstream also never
sends PlungerRelaunchBall to the table (control.cpp sends it to the plunger only). So part_1034 /
control_tables_notes are right; loading_and_flippers A.8 and part_class_framework section 4
("PlungerRelaunchBall = 0x3fc") are wrong for the plunger. The +2 shift starts at 0x3fe
(PlayerChanged); FT 1015 (0x3f7) and 1021 (0x3fd) have no upstream counterpart.

(b) 0x3f7 = **NEW:TableBuilt**. Sent once, by the table ctor 0x41a290 after make_links, directly
to control::table_control_handler (0x41ab2f; CADET 0x422c10, DRAGON 0x424260, PIRATES 0x4230c0).
No part Message handles 0x3f7 and nothing broadcasts it. (0x3f7 also appears as a string id at
0x40f3df and as part type 1015's GetTypeId stub: not messages.)

(c) 0x47 = **ControlNotifyTimerExpired** (upstream 61). Disassembly: 0x40af60 `mov eax,[esp+8];
push eax; push 0x47; mov [eax+0x5a],0; call control::handler; ret 8` is TLightGroup's notify-timer
callback (clears the group's timer field, like upstream TLightGroup::NotifyTimerExpired), and
0x413750 does the same with field +0x4a for TComponentGroup (registered by TComponentGroup
Message 0x34 = ResetNotifyTimer). Identical bytes in CADET/DRAGON/PIRATES (same addresses). CADET's
rules compare 0x47 in four functions (0x423840 0x424480 0x424d60 0x424f50), matching the four
ControlNotifyTimerExpired uses in upstream control.cpp.

(d) 0x4d = **NEW:ControlScorerReplaced** (rules-private). Sent only through control::handler by
the score helpers (DRAGON 0x4230be/0x423105/0x423168, PIRATES 0x4232de/0x423325/0x423388) to the
previously scoring component, so it reaches that component's control function, never a part
Message. DRAGON: no control function reacts. PIRATES: four do (a_ramp8 0x428b70, a_ramp5 0x428d80,
s_trip3 0x429210, s_trip8 0x429400). CADET never sends it. (0x4d in the cheat-key switch functions
is the ASCII 'M', see below.)

(e) Rules-private and FT-only codes, proposed per D3 (value = FT code + 0x100):
0x4f/0x50 are upstream ControlMissionComplete/ControlMissionStarted (+13, see table);
0x51 ControlModeResume, 0x52 ControlModeEnd, 0x53 ControlBallDrained, 0x3d ControlBlockerMoveDone,
0x4d ControlScorerReplaced, 0x54 ControlRampPlaneLayer, 0x55 ControlBallToLayer1,
0x56 ControlBallToLayer2, 0x1b TLightSweepsStart, 0x43 THoleEjectBall, 0x48 TTimerGetTimeLeft,
0x4b/0x4c FtPartEnable/FtPartDisable (on parts; on the rules side 0x4b/0x4c are
ControlEnable/DisableMultiplier), TBlocker 0x39-0x3c, 0x4e TKickoutDestroyBall, 0x3f7 TableBuilt,
0x3fd ControlReloadScores. Earlier proposals (part_1038 TLightSweepsStart = 27, part_1035
ControlBallToLayer1 = 85 ...) keep their names; the value rule here replaces their values so that
no NEW member can equal an upstream value.

## Shift check (critic's suggestions)

| range | suggested | result |
|---|---|---|
| light group upstream 24-47 | FT 28-51 | confirmed: jump table 0x1c-0x32 = 24-46 in order; 47 CountdownEnded = 0x33 (bargraph timer) |
| gate/kickout/sink 53-56 | FT 62-65 | confirmed: TGate 0x3e/0x3f, TKickout 0x40, TSink 0x41 |
| TSoloTarget 49/50 | FT 0x35/0x36 | confirmed (and TPopupTarget); TBlocker 51/52 = 0x37/0x38, TComponentGroup 48 = 0x34 |
| control 57-67 | FT +9..+13 | 57 +9, 58 +10, 59 (TBlocker/TTimer restart) +10, 60 +10, 61 +10, 62 +11, 63 +11, 64/65 +11, 66/67 +13 |

## Not message codes (excluded)

- TPinballTable 0..3: jump table at 0x419c95 switches on the player index (ebx), not on the code.
- CADET 0x4262e0, DRAGON 0x4282c0, PIRATES 0x42a830 compare 0x31, 0x42, 0x45, 0x4d, 0x4e, 0x51,
  0x52, 0x54, 0x55...: a switch on typed characters (cheat keys), not on message codes.
- The rules hook's 0x3ee / 0x3f9 / 0x3fb compares (DRAGON 0x42c020, PIRATES 0x42d850) test the
  caller's GetTypeId (1006 / 1017 / 1019), not the code.

## Open / UNVERIFIED

- TBumper (0x41424a) and TGate (0x4154bc) pass a register as the code to control::handler; that it
  is their own Message code (as upstream) is inferred from the upstream bodies, not traced.
- 0x3fd and 0x4e and table 0x3fc have no literal sender; a computed sender was not searched for.
- Closed: TTimer 0x48 scale constant 0x4377a0 = 1000.0 (double; PIRATES 0x4387a0 = 1000.0), so 0x48
  returns milliseconds (part_ttimer_ttextbox.md T-table; known_unknowns.md B4).
- PIRATES never sends 0x43 (THole eject); how its holes release was not traced.
- D4: FT TDrain handles SetTiltLock and FT THole waits for 0x43; upstream does neither. Gate such
  behaviour to the Full Tilt tables when porting.
"""


def write_md(path, out, problems, raw):
    L = ["# Full Tilt message code -> upstream MessageCode map (generated by re/ft_msgmap.py)", "",
         "Decompiled-derived evidence addresses: never commit. Regenerate with",
         "`re/work/.venv/bin/python re/ft_msgmap.py`. Engine Message functions and their case sets",
         "are identical in CADET/DRAGON/PIRATES (checked by the tool); addresses below are DRAGON's",
         "unless an exe is named (engine addresses are the same in all three).", "",
         "control::handler: " + ", ".join(f"{e} {r['handler']:#x}" for e, r in raw.items()) + ".", "",
         "`upstream` is the SpaceCadetPinball MessageCode member to translate the FT literal to;",
         "`NEW:` members are proposals (value = FT code + 0x100). `msg` = handled by the class's",
         "Message (else only sent to it); `sent` = example send sites per exe.", ""]
    if problems:
        L += ["## Coverage problems", ""] + [f"- {p}" for p in problems] + [""]
    else:
        L += ["Coverage: every extracted code of every class is paired; every paired private code was seen.", ""]
    L += ["## Map per receiving class", ""]
    for cls, ent in out.items():
        L += [f"### {cls} (Message {ent['message']})", "", "| FT | upstream | new value | msg | evidence | sent | note |",
              "|---|---|---|---|---|---|---|"]
        for code, r in ent["codes"].items():
            sent = "; ".join(f"{x[0]} {' '.join(v)}" for x, v in r["sent_at"].items())
            L.append(f"| {code} ({int(code, 16)}) | {r['upstream']} | {r.get('proposed_value', '')} | "
                     f"{'y' if r['handled_by_message'] else '-'} | {' '.join(r['evidence'][:2])} | {sent} | {r['note']} |")
        L.append("")
    L.append(NOTES)
    open(path, "w").write("\n".join(L) + "\n")


def check_identical(raw):
    base = {c: v["cases"] for c, v in raw["CADET"]["classes"].items()}
    return [f"{e}: {c} cases differ from CADET" for e, r in raw.items()
            for c, v in r["classes"].items() if base.get(c) != v["cases"]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(HERE, "work", "bin"))
    ap.add_argument("--export", default=os.path.join(HERE, "work", "export"))
    ap.add_argument("--dat-dir", default=os.environ.get("FULLTILT_DIR") or os.path.join(HERE, "work", "game"))
    ap.add_argument("--out-json", default=os.path.join(HERE, "work", "spec", "msgmap.json"))
    ap.add_argument("--out-md", default=os.path.join(HERE, "work", "spec", "msgmap.md"))
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()
    raw = extract(args)
    if args.dump:
        dump(raw)
        return
    out, problems = build(raw)
    problems = check_identical(raw) + problems
    new = {}
    for cls, ent in out.items():
        for code, r in ent["codes"].items():
            if r["upstream"].startswith("NEW:"):
                new.setdefault(r["upstream"][4:], {"value": r["proposed_value"], "ft_code": code,
                                                   "receivers": []})["receivers"].append(cls)
    doc = {"generated_by": "re/ft_msgmap.py", "new_offset": hex(NEW_OFFSET),
           "control_handler": {e: hex(r["handler"]) for e, r in raw.items()},
           "classes": out, "new_members": new, "problems": problems}
    with open(args.out_json, "w") as f:
        json.dump(doc, f, indent=1)
    write_md(args.out_md, out, problems, raw)
    print(f"wrote {args.out_json} and {args.out_md}: {len(out)} classes, "
          f"{sum(len(e['codes']) for e in out.values())} codes, {len(new)} new members, "
          f"{len(problems)} problems")
    for p in problems:
        print("  PROBLEM", p)


if __name__ == "__main__":
    main()
