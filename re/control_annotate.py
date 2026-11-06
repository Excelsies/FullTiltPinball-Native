#!/usr/bin/env python
"""Turn a control-region listing (control_framework.py output) into a ready-to-port listing:
message codes as upstream MessageCode names, float immediates decoded, TLight / TLightGroup /
1011 blocker / base / TPinballTable fields named, tags and functions named, rc strings reduced
to their ids, plus a per-function header (handler tags, callers, rule globals used).

Inputs (all generated or written earlier, under re/work/spec unless given):
  control_region_<t>.c        Ghidra bodies with tags / names resolved (control_framework.py)
  control_framework_<t>.json  per-function facts (handler tags, callers, globals, strings)
  control_tables_<t>.json     tag -> part class
  msgmap.json                 FT code -> upstream MessageCode per receiving class (ft_msgmap.py)
  names JSON                  proposed function names ({"0x<addr>": name | [name, note]},
                              optional "_framework" sub-dict)
  <t>_framework.md            rule-global meanings (section 6.1 table), optional
  EXE + Ghidra export         to recover message arguments Ghidra lost (call [reg+4] sites)

The listing keeps no game text: rc strings become "string NNNN" comments. The output is still
derived from decompiled code: write it under re/work/ (gitignored).

usage: control_annotate.py TABLE EXE EXPORT_DIR --names FILE [--names FILE ...]
                           [--spec-dir DIR] [--out FILE] [--framework-md FILE]
example:
  re/work/.venv/bin/python re/control_annotate.py dragon re/work/bin/DRAGON.EXE re/work/export \\
      --names re/work/spec/dragon_framework_names.json --framework-md re/work/spec/dragon_framework.md \\
      --out re/work/spec/port_listing_dragon.c
Prints a summary of what was left raw (counts per kind) on stdout.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ft_msgmap import Exe, call_sites, operand_value, receiver  # noqa: E402
from control_framework import TABLE_FIELDS  # noqa: E402
from control_tables_report import rc_strings  # noqa: E402

# DAT part class (control_tables part_class) -> msgmap receiving class (as ft_msgmap.TAG_CLASS)
TAG_CLASS = {"TFlipper L": "TFlipper", "TFlipper R": "TFlipper", "TKickout (no ball)": "TKickout",
             "TPlunger(flag 1)": "TPlunger", "type1034": "TPlunger", "type1035": "TRampExit",
             "type1036": "TAccelerator", "type1037": "TBall", "type1038": "TLightSweeps",
             "type1042": "TMagnet", "type1043": "TDiverter"}
# GetTypeId() return values (part_class_framework.md 1, slot 0)
TYPE_ID = {1000: "TWall", 1001: "TPlunger", 1002: "TLight", 1005: "TBumper", 1006: "TPopupTarget",
           1007: "TDrain", 1010: "TWall", 1011: "TBlocker", 1012: "TKickout", 1013: "TGate",
           1014: "TKickback", 1015: "TRollover", 1016: "TOneway", 1017: "TSink",
           1018: "TFlagSpinner", 1019: "TSoloTarget", 1020: "TLightRollover", 1021: "TRamp",
           1022: "THole", 1023: "TDemo", 1024: "TTripwire", 1026: "TLightGroup",
           1028: "TComponentGroup", 1029: "TKickout", 1030: "TLightBargraph", 1031: "TSound",
           1032: "TTimer", 1033: "TTextBox", 1034: "TPlunger(launcher)", 1035: "TRampExit",
           1036: "TAccelerator", 1037: "TBall", 1038: "TLightSweeps", 1041: "TFlipper",
           1042: "TMagnet", 1043: "TDiverter"}
LIGHT_FAMILY = {"TLight", "TLightGroup", "TLightBargraph", "TLightSweeps"}
NON_COLLISION = LIGHT_FAMILY | {"TSound", "TTimer", "TTextBox", "TComponentGroup", "TPinballTable"}
# vtable slots (part_class_framework.md 1)
SLOTS = {0x0: "GetTypeId", 0x4: "Message", 0x8: "port_draw", 0xc: "get_scoring",
         0x10: "put_scoring", 0x14: "ScalarDeletingDtor"}


def slot_name(cls, off):
    if off in SLOTS:
        return SLOTS[off]
    if cls == "TSound":
        return {0x18: "Play", 0x1c: "Duration"}.get(off)
    if cls in LIGHT_FAMILY:
        return {0x18: "Reset"}.get(off)
    if cls and cls not in NON_COLLISION:
        return {0x18: "Collision", 0x1c: "FieldEffect"}.get(off)
    return None


# component fields: base (part_class_framework.md 2), TLight / TLightGroup
# (part_tlight_tlightgroup.md 1.1 / 2.1), 1011 blocker (part_1011.md 3), TTextBox
# (part_ttimer_ttextbox.md), TTimer
BASE_FIELDS = {0x5: "ActiveFlag", 0x6: "MessageField", 0x2a: "Control"}
FIELDS = {
    "TLight": {0x42: "LightOnFlag", 0x46: "FlasherOnFlag", 0x4a: "ToggledOffFlag",
               0x4e: "ToggledOnFlag", 0x52: "TurnOffAfterFlashingFg", 0x56: "LightOnBmpIndex",
               0x5a: "TemporaryOverrideFlag", 0x5e: "UndoOverrideTimer", 0x72: "FlashLightOnFlag",
               0x76: "FlashTimer", 0x86: "TimeoutTimer"},
    "TLightGroup": {0x46: "List", 0x4a: "Timer1Time", 0x4e: "Timer1TimeDefault",
                    0x52: "MessageField2", 0x56: "AnimationFlag", 0x5a: "NotifyTimer",
                    0x5e: "Timer"},
    "TBlocker": {0x7e: "FtCurrentState", 0x82: "FtResetState", 0x86: "InitialDuration",
                 0x8a: "ExtendedDuration", 0x8e: "Timer", 0x92: "FtTargetState",
                 0x96: "FtStepDelay", 0x9a: "FtStepTimer", 0xae: "SoundIndex4",
                 0xb2: "SoundIndex3"},
    "TTextBox": {0x5e: "CurrentMessage"},
    "TTimer": {0x42: "Timer"},
}
# TPinballTable offsets not named by control_framework.TABLE_FIELDS (part_class_framework.md 6)
TABLE_EXTRA = {0x102: "PlayerCount", 0x106: "CurrentPlayer", 0x12a: "ComponentList",
               0x13e: "LightGroup", 0x152: "CollisionCompOffset",
               0x6a: "BallInDrainFlag"}  # TDrain sets it, tilt tests it
# engine functions the text functions call (part_ttimer_ttextbox.md overlay, part_1034.md 594 timer)
ENGINE_TEXT = {0x41f6c0: "timer::set", 0x41f900: "timer::kill", 0x41dc50: "TTextBox::Draw",
               0x41e070: "TTextBox::DisplayOverlay", 0x41e0e0: "TTextBox::EndOverlay"}
# per-table text functions just before the control region (dragon_framework.md 1.1,
# pirates_framework.md 3 row "text dispatcher"): credits / quote ticker (timer callback),
# ticker stop, TextEvent dispatcher (FT codes 0x57-0x6e, the port's TableRules::TextEvent hook)
PRE_REGION = {"dragon": {0x422a80: "FtCreditsTicker", 0x422b50: "FtCreditsStop",
                         0x422b80: "FtTextEvent"},
              "pirates": {0x422a90: "FtCreditsTicker", 0x422b60: "FtCreditsStop",
                          0x422b90: "FtTextEvent"}}
# engine / CRT functions called from the rules (control_tables_notes.md, engine_diff.md,
# loading_and_flippers.md, part_class_framework.md, part_1034.md, pirates_framework.md)
ENGINE = {
    "dragon": {0x41de40: "TTextBox::Display", 0x41d930: "TTextBox::Clear",
               0x42c76f: "crt_sprintf", 0x42c4cb: "crt_rand", 0x40eb50: "midi::play_track",
               0x40e9d0: "midi::load_track", 0x40e9b0: "midi::get_active_track",
               0x419f10: "TPinballTable::AddScore", 0x419fd0: "TPinballTable::ChangeBallCount",
               0x410ff0: "pb::chk_highscore", 0x4037a0: "assert_box", 0x402c00: "TBall::Disable",
               0x4029e0: "TBall::throw_ball", 0x422b80: "FtTextEvent", 0x409730: "get_rc_string"},
    "pirates": {0x41de40: "TTextBox::Display", 0x41d930: "TTextBox::Clear",
                0x42dfaf: "crt_sprintf", 0x42dd0b: "crt_rand", 0x40eb50: "midi::play_track",
                0x40e9d0: "midi::load_track", 0x40e9b0: "midi::get_active_track",
                0x419f10: "TPinballTable::AddScore", 0x419fd0: "TPinballTable::ChangeBallCount",
                0x410ff0: "pb::chk_highscore", 0x4037a0: "assert_box",
                0x402c00: "TBall::Disable", 0x4029e0: "TBall::throw_ball",
                0x422b90: "FtTextEvent", 0x409730: "get_rc_string",
                0x424210: "BallLockedInBrig"},  # not in the export: pirates_framework.md 3
}
CODE_PARAM_FIXED = {"handler": 1, "MissionControl": 1, "MissionControlSwitch": 2,
                    "table_control_handler": 1}
NUM = r"(?:0x[0-9a-f]+|\d+)"


def ffmt(bits):
    """Shortest decimal that round-trips the float32 bit pattern, as a C float literal."""
    v = struct.unpack("<f", struct.pack("<I", bits))[0]
    if v != v or v in (float("inf"), float("-inf")):
        return None
    for p in range(1, 10):
        s = f"{v:.{p}g}"
        # '1e+01' round-trips at p=1 for 10.0: keep looking for the plain form
        if struct.unpack("<f", struct.pack("<f", float(s)))[0] == v and "e" not in s:
            break
    if "e" in s:
        return None
    if "." not in s:
        s += ".0"
    return s + "f"


def float_like(bits):
    """Plausible float immediate: exponent of |v| in [2^-16, 2^24], short decimal form."""
    e = (bits >> 23) & 0xFF
    if not 0x6f <= e <= 0x97:
        return None
    s = ffmt(bits)
    return s if s and len(s.lstrip("-")) <= 9 else None


def dfmt_hi(hi):
    v = struct.unpack("<d", struct.pack("<Q", hi << 32))[0]
    return repr(v)


def balanced_args(s, i):
    """s[i] == '(' -> (args string, index after the matching ')')."""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return s[i + 1:j], j + 1
    return None, i


def split_args(a):
    out, depth, cur = [], 0, ""
    for ch in a:
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


PRINTF_CONV = re.compile(r"%[-+ #0]*(\*|\d+)?(?:\.(\*|\d+))?(?:hh|h|ll|l|L|I64|I32|I)?([diouxXcsSpneEfgGaA%])")
SUBREG = {"al": "eax", "ah": "eax", "ax": "eax", "bl": "ebx", "bh": "ebx", "bx": "ebx",
          "cl": "ecx", "ch": "ecx", "cx": "ecx", "dl": "edx", "dh": "edx", "dx": "edx",
          "si": "esi", "di": "edi", "bp": "ebp", "sp": "esp"}
REG32 = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"}
VSLOT_POPS = {0x0: 0, 0x4: 2, 0x8: 0, 0xc: 1, 0x10: 2, 0x14: 1}
MEMOP = re.compile(r"(?:(\w+) ptr )?\[(?:(e\w\w)(?: \+ (e\w\w)\*(\d))?|(e\w\w)\*(\d))?"
                   r"(?: ?([+-]) ?)?(0x[0-9a-f]+|\d+)?\]")
ARITH = {"add": "+", "sub": "-", "imul": "*", "and": "&", "or": "|", "xor": "^", "shl": "<<",
         "sar": ">>", "shr": ">>"}


def printf_slots(fmt):
    """Argument dwords a printf format consumes after (buffer, format): doubles take two."""
    n = 0
    for m in PRINTF_CONV.finditer(fmt):
        if m.group(3) == "%":
            continue
        n += (m.group(1) == "*") + (m.group(2) == "*")
        n += 2 if m.group(3) in "eEfgGaA" else 1
    return n


def paren(e):
    return e if re.fullmatch(r"[\w.>\-\[\]&*]+|\w+\(.*\)", e) else f"({e})"


class StackSim:
    """Forward symbolic run over one function's machine code (linear sweep; the state at a jump
    target is the state at its first jump, merged with the fall-through). Tracks the dwords on
    the stack by depth below the entry esp, the registers and the x87 stack as C-like expressions
    (None = unknown), so the cdecl pushes before a call can be read back as expressions."""

    def __init__(self, an):
        self.an = an
        self.pops = {}

    # ---- naming --------------------------------------------------------------------------
    def lit(self, v):
        if v < 0x1000000:
            return str(v)
        if v in self.an.tag_addr:
            return f"&tag_{self.an.tag_addr[v]}"
        n = self.an.call_name(v)
        if n:
            return f"&{n}"
        if v >= 0x80000000:
            return str(v - (1 << 32))
        s = float_like(v)
        return f"{v:#x} /* = {s} */" if s else f"{v:#x}"

    def ev(self, x):
        if isinstance(x, tuple):
            if x[0] == "lin":
                return x[1] if x[2] == 1 else f"{paren(x[1])} * {x[2]}"
            if x[0] == "phi":
                return f"either({', '.join(x[1])})"
            return {"addr": lambda p: f"&local_{p:x}", "dbl": lambda v: v, "flt": lambda v: v}.get(
                x[0], lambda _: None)(x[1])
        return None if x is None else (self.lit(x) if isinstance(x, int) else x)

    @staticmethod
    def lin(v):
        """(atom, k) when v is k * atom (MSVC builds constant multiplies from lea/shl/sub)."""
        if isinstance(v, tuple) and v[0] == "lin":
            return v[1], v[2]
        if isinstance(v, str):
            return v, 1
        return None

    @staticmethod
    def mk_lin(atom, k):
        return atom if k == 1 else ("lin", atom, k)

    def callee_pops(self, va):
        """Dwords the callee pops (ret N), 0 for cdecl, None when unknown."""
        if va not in self.pops:
            n = None
            size = self.an.exe.sizes.get(va)
            if size and size > 0:
                for ins in self.an.exe.insns(va, size):
                    if ins.mnemonic == "ret":
                        n = int(ins.op_str, 0) // 4 if ins.op_str else 0
                        break
            self.pops[va] = n
        return self.pops[va]

    # ---- operands ------------------------------------------------------------------------
    def reg(self, st, r):
        if r in REG32 and r != "esp":
            return st["regs"].get(r)
        return None

    def mem_loc(self, st, op):
        """('stack', pos) | ('expr', text) | None for a memory operand."""
        m = MEMOP.fullmatch(op)
        if not m:
            return None
        width, base, idx, scale, idx2, scale2, sign, disp = m.groups()
        d = int(disp, 0) if disp else 0
        if sign == "-":
            d = -d
        if base == "esp" and not idx:
            return ("stack", st["esp"] - d)
        cast = {"byte": "unsigned char", "word": "short", "qword": "double"}.get(width, "int")
        if base is None and idx2 is None:
            return ("expr", self.global_ref(d, cast))
        if base is None and d >= 0x400000:  # table in the EXE indexed by a register
            iv = self.reg(st, idx2)
            return ("expr", f"DAT_{d:08x}[{self.ev(iv)}]" if iv is not None and scale2 == "4"
                    and cast == "int" else None) if iv is not None else None
        b = self.reg(st, base) if base else None
        if base and b is None:
            return None
        if idx or idx2:
            iv = self.reg(st, idx or idx2)
            if iv is None:
                return None
            terms = ([paren(self.ev(b))] if base else []) + [f"{paren(self.ev(iv))} * {scale or scale2}"]
            if d:
                terms.append(str(d))
            return ("expr", f"*({cast} *)({' + '.join(terms)})")
        return ("expr", self.field_ref(self.ev(b), d, cast))

    def global_ref(self, va, cast):
        if va in self.an.slot_tag:
            return self.an.slot_tag[va]
        if va == self.an.table_global:
            return "TableG"
        if va in self.an.tag_addr:
            return f"tag_{self.an.tag_addr[va]}"
        return f"DAT_{va:08x}" if cast == "int" else f"*({cast} *)&DAT_{va:08x}"

    def field_ref(self, e, off, cast):
        if off == 0 and cast == "int":
            return f"*{paren(e)}"  # e.g. a component's vtable pointer
        if e in self.an.tag_class:
            cls = self.an.tag_class[e]
            name = FIELDS.get(cls, {}).get(off) or BASE_FIELDS.get(off)
            if name and not (name == "MessageField" and cast == "unsigned char"):
                return f"{e}->{name}"
            if off in (6, 7, 8, 9) and cast == "unsigned char":
                return f"((unsigned char *)&{e}->MessageField)[{off - 6}]"
        if e == "TableG":
            nm = TABLE_EXTRA.get(off) or TABLE_FIELDS.get(off)
            if nm:
                return "TableG->" + re.sub(r"\(.*", "", nm)
        return f"*({cast} *)((char *){paren(e)} + {off:#x})"

    def val(self, st, op):
        op = op.strip()
        if re.fullmatch(NUM, op):
            return int(op, 0) & 0xFFFFFFFF
        if op in REG32:
            return self.reg(st, op)
        if op in SUBREG:
            return None
        loc = self.mem_loc(st, op)
        if loc is None:
            return None
        if loc[0] == "expr":
            return loc[1]
        pos = loc[1]
        if pos in st["mem"]:
            return st["mem"][pos]
        if pos < 0 and pos % 4 == 0:
            return f"param_{-pos // 4}"
        return None

    def set_reg(self, st, r, v):
        if r in REG32 and r != "esp":
            st["regs"][r] = v
        elif r in SUBREG and SUBREG[r] != "esp":
            st["regs"][SUBREG[r]] = None

    def store(self, st, op, v):
        loc = self.mem_loc(st, op)
        if loc and loc[0] == "stack":
            st["mem"][loc[1]] = v
            if op.startswith("qword"):
                st["mem"][loc[1] - 4] = None

    # ---- the run -------------------------------------------------------------------------
    @staticmethod
    def copy(st):
        return {"esp": st["esp"], "regs": dict(st["regs"]), "mem": dict(st["mem"]),
                "fpu": list(st["fpu"])}

    def phi(self, x, y):
        """Value at a join: equal -> itself; two known values -> either(a, b) (tail-merged code,
        the value depends on the path); otherwise unknown."""
        if x == y:
            return x
        if x is None or y is None:
            return None
        alts = set()
        for v in (x, y):
            alts |= set(v[1]) if isinstance(v, tuple) and v[0] == "phi" else {self.ev(v)}
        if None in alts or len(alts) > 3 or any("either(" in a for a in alts):
            return None
        return ("phi", tuple(sorted(alts)))

    def merge(self, a, b):
        if a["esp"] != b["esp"]:
            return None
        return {"esp": a["esp"],
                "regs": {k: self.phi(v, b["regs"].get(k)) for k, v in a["regs"].items()},
                "mem": {k: self.phi(v, b["mem"].get(k)) for k, v in a["mem"].items()},
                "fpu": a["fpu"] if a["fpu"] == b["fpu"] else []}

    @staticmethod
    def drop_popped(st):
        for p in [p for p in st["mem"] if p > st["esp"]]:
            del st["mem"][p]

    def save(self, saved, t, st, here):
        """Record the state for a forward jump target, merged with earlier jumps to it
        (False = the stack depths disagree)."""
        if t <= here:
            return
        if t not in saved:
            saved[t] = self.copy(st)
        elif saved[t] is not False:
            saved[t] = self.merge(saved[t], st) or False

    def run(self, start, size, on_call):
        """Simulate [start, start+size); on_call(ins, st, after) is called before every call
        (after = the next three instructions)."""
        ins = self.an.exe.insns(start, size)
        saved = {}
        st = {"esp": 0, "regs": {}, "mem": {}, "fpu": []}
        for k, x in enumerate(ins):
            if x.address in saved:
                sv = saved[x.address]
                st = None if sv is False else self.copy(sv) if st is None else self.merge(st, sv)
            if st is None:
                continue
            st = self.step(x, st, saved, on_call, ins[k + 1:k + 4], start, size)
        return saved

    def step(self, x, st, saved, on_call, after, start, size):
        mn = x.mnemonic
        ops = [o.strip() for o in x.op_str.split(",")] if x.op_str else []
        if mn == "push":
            v = self.val(st, ops[0])
            st["esp"] += 4
            st["mem"][st["esp"]] = v
        elif mn == "pop":
            v = st["mem"].get(st["esp"])
            st["esp"] -= 4
            self.drop_popped(st)
            self.set_reg(st, ops[0], v)
        elif mn == "ret":
            return None
        elif mn == "jmp":
            m = re.fullmatch(r"dword ptr \[e\w\w\*4 \+ (0x[0-9a-f]+)\]", x.op_str)
            if m:
                tab = int(m.group(1), 16)
                for j in range(256):
                    t = self.an.exe.dword(tab + 4 * j)
                    if not start <= t < start + size:
                        break
                    self.save(saved, t, st, x.address)
            elif re.fullmatch(NUM, x.op_str):
                self.save(saved, int(x.op_str, 0), st, x.address)
            return None
        elif mn.startswith("j") or mn.startswith("loop"):
            if re.fullmatch(NUM, x.op_str):
                self.save(saved, int(x.op_str, 0), st, x.address)
        elif mn == "call":
            return self.call(x, st, on_call, after)
        elif mn in ("mov", "movzx", "movsx"):
            if ops[0] in REG32 or ops[0] in SUBREG:
                if ops[0] == "esp":
                    return None
                self.set_reg(st, ops[0], self.val(st, ops[1]))
            else:
                self.store(st, ops[0], self.val(st, ops[1]))
        elif mn == "lea":
            self.lea(st, ops)
        elif mn in ARITH or mn in ("inc", "dec", "neg", "not"):
            self.arith(st, mn, ops)
        elif mn == "cdq":
            a = st["regs"].get("eax")
            st["regs"]["edx"] = ("cdq", a) if a is not None else None
        elif mn in ("idiv", "div"):
            a, d, s = st["regs"].get("eax"), st["regs"].get("edx"), self.val(st, ops[0])
            ok = a is not None and s is not None and (mn == "div" and d == 0 or d == ("cdq", a))
            st["regs"]["eax"] = f"{paren(self.ev(a))} / {paren(self.ev(s))}" if ok else None
            st["regs"]["edx"] = f"{paren(self.ev(a))} % {paren(self.ev(s))}" if ok else None
        elif mn.startswith("f"):
            self.fpu(st, mn, ops)
        elif mn in ("cmp", "test", "nop", "wait", "int3", "cld", "std"):
            pass
        elif ops and (ops[0] in REG32 or ops[0] in SUBREG):
            if ops[0] == "esp":
                return None
            self.set_reg(st, ops[0], None)
        elif ops and "esp" in ops[0] and mn not in ("cmp", "test"):
            self.store(st, ops[0], None)
        return st

    def lea(self, st, ops):
        m = MEMOP.fullmatch(ops[1])
        if ops[0] == "esp":
            return
        if m and m.group(2) == "esp" and not m.group(3):
            d = int(m.group(8), 0) if m.group(8) else 0
            self.set_reg(st, ops[0], ("addr", st["esp"] - d))
            return
        if not m:
            self.set_reg(st, ops[0], None)
            return
        width, base, idx, scale, idx2, scale2, sign, disp = m.groups()
        parts = [(self.reg(st, r), int(s or 1)) for r, s in ((base, None), (idx or idx2, scale or scale2)) if r]
        ls = [self.lin(v) for v, _ in parts]
        if not disp and ls and all(ls) and len({a for a, _ in ls}) == 1:
            self.set_reg(st, ops[0], self.mk_lin(ls[0][0], sum(k * s for (_, k), (_, s) in zip(ls, parts))))
            return
        terms = []
        for r, s in ((base, None), (idx or idx2, scale or scale2)):
            if r is None:
                continue
            v = self.reg(st, r)
            if v is None or isinstance(v, tuple) and v[0] != "lin":
                self.set_reg(st, ops[0], None)
                return
            t = paren(self.ev(v))
            terms.append(t if s in (None, "1") else f"{t} * {s}")
        if disp:
            terms.append(("- " if sign == "-" else "") + str(int(disp, 0)))
        self.set_reg(st, ops[0], " + ".join(terms).replace("+ - ", "- ") if terms else None)

    def arith(self, st, mn, ops):
        dst = ops[0]
        if dst == "esp":
            v = self.val(st, ops[1]) if len(ops) > 1 else None
            if mn in ("add", "sub") and isinstance(v, int):
                st["esp"] += -v if mn == "add" else v
                self.drop_popped(st)
            return
        if dst not in REG32:
            if dst in SUBREG:
                self.set_reg(st, dst, None)
            else:
                self.store(st, dst, None)
            return
        a = self.reg(st, dst)
        if mn == "xor" and ops[1] == dst:
            st["regs"][dst] = 0
            return
        if mn in ("inc", "dec", "neg", "not"):
            r = None if a is None or isinstance(a, tuple) else {
                "inc": f"{paren(self.ev(a))} + 1", "dec": f"{paren(self.ev(a))} - 1",
                "neg": f"-{paren(self.ev(a))}", "not": f"~{paren(self.ev(a))}"}[mn]
            st["regs"][dst] = r
            return
        if mn == "imul" and len(ops) == 3:
            a, b = self.val(st, ops[1]), self.val(st, ops[2])
        else:
            b = self.val(st, ops[1]) if len(ops) > 1 else None
        la, lb = self.lin(a), self.lin(b)
        if la and isinstance(b, int) and mn in ("shl", "imul"):
            st["regs"][dst] = self.mk_lin(la[0], la[1] * (b if mn == "imul" else 1 << b))
            return
        if la and lb and la[0] == lb[0] and mn in ("add", "sub"):
            k = la[1] + lb[1] if mn == "add" else la[1] - lb[1]
            st["regs"][dst] = self.mk_lin(la[0], k) if k else 0
            return
        a, b = (self.ev(a) if isinstance(a, tuple) and a[0] == "lin" else a,
                self.ev(b) if isinstance(b, tuple) and b[0] == "lin" else b)
        if a is None or b is None or isinstance(a, tuple) or isinstance(b, tuple):
            st["regs"][dst] = None
            return
        if isinstance(a, int) and isinstance(b, int) and mn in ("add", "sub", "imul", "and", "or", "shl"):
            r = {"add": a + b, "sub": a - b, "imul": a * b, "and": a & b, "or": a | b,
                 "shl": a << b}[mn]
            st["regs"][dst] = r & 0xFFFFFFFF
            return
        st["regs"][dst] = f"{paren(self.ev(a))} {ARITH[mn]} {paren(self.ev(b))}"

    def fpu(self, st, mn, ops):
        f = st["fpu"]
        if mn in ("fld", "fild") and ops and "ptr" in ops[0]:
            v = self.val(st, ops[0])
            f.append(None if v is None else self.ev(v) if mn == "fild" else self.ev(v))
        elif mn in ("fstp", "fistp", "fst", "fist") and ops and "ptr" in ops[0]:
            v = f[-1] if f else None
            self.store(st, ops[0], None if v is None else ("dbl" if ops[0].startswith("qword") else "flt", v))
            if mn.endswith("p") and f:
                f.pop()
        elif mn in ("fmul", "fdiv", "fadd", "fsub") and ops and "ptr" in ops[0] and f:
            v = self.val(st, ops[0])
            f[-1] = None if v is None or f[-1] is None else \
                f"{paren(f[-1])} {ARITH.get(mn[1:], {'mul': '*', 'div': '/'}.get(mn[1:]))} {paren(self.ev(v))}"
        else:
            st["fpu"] = []

    @staticmethod
    def add_esp_after(after):
        """N of an 'add esp, N' that follows a call (lea / mov to other registers may come
        first), else None."""
        for y in after:
            if y.mnemonic == "add" and y.op_str.startswith("esp, "):
                return int(y.op_str[5:], 0)
            if y.mnemonic not in ("lea", "mov") or y.op_str.startswith(("esp", "dword ptr [esp")):
                return None
        return None

    def call(self, x, st, on_call, after):
        on_call(x, st, after)
        nxt = after[0] if after else None
        an = self.an
        if re.fullmatch(NUM, x.op_str):
            t = int(x.op_str, 0)
            pops = self.callee_pops(t)
            if pops is None:
                return None
            nargs = pops
            if pops == 0 and self.add_esp_after(after) is not None:
                nargs = self.add_esp_after(after) // 4
            elif pops == 0 and nxt is not None and nxt.mnemonic == "pop" and nxt.op_str == "ecx":
                nargs = 1
            args = [self.ev(st["mem"].get(st["esp"] - 4 * i)) for i in range(nargs)]
            name = an.call_name(t) or f"FUN_{t:08x}"
            if t == an.getter and args and args[0] and re.fullmatch(r"\d+", args[0]):
                r = f"get_rc_string({args[0]})"
            else:
                if re.match(r"T\w+::", name):
                    args = [self.ev(st["regs"].get("ecx"))] + args
                r = f"ret_of({name}({', '.join(a or '_' for a in args)}))"
            st["esp"] -= 4 * pops
        else:
            m = re.fullmatch(r"dword ptr \[(e\w\w)(?: \+ (0x[0-9a-f]+|\d+))?\]", x.op_str)
            vt = self.reg(st, m.group(1)) if m else None
            off = int(m.group(2) or "0", 0) if m else None
            if not (isinstance(vt, str) and vt.startswith("*")):
                # receiver merged over several paths: slots 0 / 4 pop the same on every class
                if off not in (0, 4):
                    return None
                st["esp"] -= 4 * VSLOT_POPS[off]
                self.drop_popped(st)
                st["regs"].update({"eax": None, "ecx": None, "edx": None})
                st["fpu"] = []
                return st
            recv = vt[1:]
            cls = an.tag_class.get(recv)
            sn = slot_name(cls, off)
            pops = VSLOT_POPS.get(off)
            if pops is None:
                pops = {"Play": 0, "Duration": 0, "Reset": 0, "Collision": 5, "FieldEffect": 2}.get(sn)
            if pops is None:
                return None
            args = [st["mem"].get(st["esp"] - 4 * i) for i in range(pops)]
            if sn == "Message" and all(a is not None for a in args):
                code = (an.code_name(cls, args[0]) if isinstance(args[0], int) and cls else None) \
                    or self.ev(args[0])
                v = ("0.0f" if args[1] == 0 else ffmt(args[1]) or hex(args[1])) \
                    if isinstance(args[1], int) else self.ev(args[1])
                r = f"ret_of({recv}->Message({code}, {v}))"
            else:
                r = f"ret_of({recv}->{sn or f'vt_{off:#x}'}({', '.join(self.ev(a) or '_' for a in args)}))"
            st["esp"] -= 4 * pops
        self.drop_popped(st)
        st["regs"].update({"eax": r, "ecx": None, "edx": None})
        st["fpu"] = []
        return st


class Annotator:
    def __init__(self, table, exe_path, export_dir, names, spec_dir, fw_md):
        self.table = table
        self.names = names
        self.engine = {**ENGINE.get(table, {}), **ENGINE_TEXT, **PRE_REGION.get(table, {})}
        self.msgmap = json.load(open(os.path.join(spec_dir, "msgmap.json")))
        self.fw = json.load(open(os.path.join(spec_dir, f"control_framework_{table}.json")))
        ct = json.load(open(os.path.join(spec_dir, f"control_tables_{table}.json")))
        self.tag_class = {t["name"]: TAG_CLASS.get(t["part_class"], t["part_class"])
                          for t in ct["tags"]}
        self.tag_addr = {t["addr"]: t["name"] for t in ct["tags"]}
        self.slot_tag = {t["addr"] + 4: t["name"] for t in ct["tags"]}
        self.funcs = {int(f["addr"], 16): f for f in self.fw["functions"]}
        self.globals_doc = self.read_globals(fw_md) if fw_md else {}
        self.exe = Exe(os.path.basename(exe_path)[:-4], os.path.dirname(exe_path), export_dir)
        self.export_dir = export_dir
        self.getter = int(self.fw["string_getter"], 16)
        self.table_global = int(self.fw["table_global"], 16)
        self.sprintf = next(a for a, n in self.engine.items() if n == "crt_sprintf")
        self.rc = rc_strings(exe_path)  # read to count printf arguments; never written out
        self.starts = sorted(self.exe.sizes)
        self.spr_log = []
        self.rec_log = []
        self.msg_check = collections.Counter()
        self.msg_fail = []
        self.pcls = None       # class of param_2 in the current control function
        self.code_vars = {}    # variables used as Message codes in the current function
        self.raw = collections.Counter()
        self.raw_examples = collections.defaultdict(list)
        cnt = collections.Counter(self.fname(a, f["name"]) for a, f in self.funcs.items())
        self.dup_names = {n for n, c in cnt.items() if c > 1}
        self.code_param = {}
        for a, f in self.funcs.items():
            n = f["name"]
            if n in CODE_PARAM_FIXED:
                self.code_param[n] = CODE_PARAM_FIXED[n]
            elif f["handler_of"] or re.fullmatch(r"Mode\d+\w*Controller", n):
                self.code_param[n] = 1

    def fname(self, a, default):
        return self.names.get(a, default)

    def call_name(self, a):
        """Listing name of a function address (region function, names JSON, engine), or None."""
        if a in self.funcs:
            n = self.fname(a, self.funcs[a]["name"])
            return n if n not in self.dup_names else f"{n}_{a:x}"
        return self.names.get(a) or self.engine.get(a)

    def note_raw(self, kind, example, fn):
        self.raw[kind] += 1
        if len(self.raw_examples[kind]) < 6:
            self.raw_examples[kind].append(f"{fn}: {example}")

    @staticmethod
    def read_globals(path):
        out = {}
        txt = open(path).read()
        sec = re.split(r"^### 6\.1", txt, flags=re.M)
        if len(sec) < 2:
            return out
        body = re.split(r"^### 6\.2", sec[1], flags=re.M)[0]
        for line in body.splitlines():
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 2 or not cells[0].startswith("0x"):
                continue
            # DRAGON: address | type | meaning | written | read; PIRATES: address | meaning | ...
            meaning = f"{cells[1]}: {cells[2]}" if len(cells) == 5 else cells[1]
            for a in re.findall(r"0x[0-9a-f]{6}", cells[0]):
                out[int(a, 16)] = meaning
        return out

    # ---- message code naming -------------------------------------------------------------
    def code_name(self, cls, code):
        """MessageCode name for FT code sent to class cls, or None."""
        c = self.msgmap["classes"].get(cls)
        e = c["codes"].get(hex(code)) if c else None
        if e is None and cls == "TLightBargraph":
            return self.code_name("TLightGroup", code)
        if e is None and cls in ("TLightGroup", "TComponentGroup"):
            # groups forward unhandled codes to their lights (part_tlight_tlightgroup.md G4)
            n = self.code_name("TLight", code)
            return n + " /*fwd to lights*/" if n else None
        if e is None:
            return None
        return "MessageCode::" + e["upstream"].replace("NEW:", "")

    def control_code(self, code, cls="control"):
        n = self.code_name(cls, code)
        if n:
            return n
        if cls == "control":
            for alt in ("table_control_handler", "TPinballTable"):
                n = self.code_name(alt, code)
                if n:
                    return n
        return None

    # ---- argument recovery from machine code --------------------------------------------
    def machine_sends(self, addr):
        out = collections.defaultdict(list)
        for fn, x, window, pushes in call_sites(self.exe, addr, addr + 1):
            # keep only the straight-line tail: nothing before the last call / ret / jmp
            cut = max([k + 1 for k, y in enumerate(window) if y.mnemonic in ("call", "ret", "jmp")],
                      default=0)
            window = window[cut:]
            pushes = [y for y in window if y.mnemonic == "push"]
            if not re.fullmatch(r"dword ptr \[e\w\w \+ 4\]", x.op_str) or len(pushes) < 2:
                continue
            code = operand_value(window, pushes[-1].op_str)
            val = operand_value(window, pushes[-2].op_str)
            rv = receiver(window, {k: v for k, v in self.slot_tag.items()})
            if code is None or rv is None:
                continue
            out[rv].append((code, val))
        return out

    # ---- crt_sprintf value arguments from machine code ---------------------------------
    def c_string(self, va):
        try:
            raw = self.exe.data(va, 256)
        except Exception:
            return None
        return raw.split(b"\0")[0].decode("latin-1") if b"\0" in raw else None  # None: .bss

    def fn_range(self, a):
        starts = self.starts
        k = starts.index(a) if a in starts else None
        end = starts[k + 1] if k is not None and k + 1 < len(starts) else a + self.exe.sizes.get(a, 0)
        return a, end - a

    def machine_calls(self, a):
        """([{addr, key, vals, err}] for every call to crt_sprintf in [a, next function),
        [(receiver, code, value, addr)] for every Message call there):
        key = rc string id of the format (or ('lit', va) for a format in the EXE's data),
        vals = rendered value arguments (None = not tracked), err = reason it is not recovered;
        Message code / value are ints when constant (None = not tracked)."""
        sim, sites = StackSim(self), []

        msgs = []

        def oc(x, st, after):
            mm = re.fullmatch(r"dword ptr \[(e\w\w) \+ 4\]", x.op_str)
            vt = sim.reg(st, mm.group(1)) if mm else None
            if isinstance(vt, str) and vt.startswith("*"):
                msgs.append((vt[1:], st["mem"].get(st["esp"]), st["mem"].get(st["esp"] - 4), x.address))
            if x.op_str != hex(self.sprintf):
                return
            raw = st["mem"].get(st["esp"] - 4)
            if isinstance(raw, int):
                key, fmt = ("lit", raw), self.c_string(raw)
            else:
                m = re.fullmatch(r"get_rc_string\((\d+)\)", raw if isinstance(raw, str) else "")
                key = int(m.group(1)) if m else None
                fmt = self.rc.get(key) if m else None
            site = {"addr": x.address, "key": key, "vals": [], "err": None}
            sites.append(site)
            pop = sim.add_esp_after(after)
            pop = pop // 4 - 2 if pop is not None else None
            if fmt is None and pop is None:
                site["err"] = "format string not known statically and no add esp after the call"
                return
            n = printf_slots(fmt) if fmt is not None else pop
            if fmt is None:
                site["note"] = "format filled at run time; count from the caller's add esp"
            if pop is not None and pop > n:
                site["note"] = f"the format has {n} conversions; sprintf ignores the extra pushed value"
                n = pop
            elif pop is not None and pop < n:
                site["err"] = f"format takes {n} value dwords but the caller pops {pop + 2}"
                return
            k = 0
            while k < n:
                v = st["mem"].get(st["esp"] - 4 * (2 + k))
                k += 2 if isinstance(v, tuple) and v[0] == "dbl" else 1
                site["vals"].append(sim.ev(v))
            if any(v is None for v in site["vals"]):
                site["err"] = "a value is computed on several paths (not tracked)"
        sim.run(*self.fn_range(a), oc)
        return sites, msgs

    @staticmethod
    def sig_tokens(expr):
        """Names and integer literals of a recovered value, to find its call in the body."""
        toks = set(re.findall(r"\b([A-Za-z_]\w*)\b", expr)) - {
            "ret_of", "either", "Message", "MessageCode", "param_1", "param_2", "param_3", "f"}
        ints = {int(n) for n in re.findall(r"(?<![\w.])(\d+)(?![\w.])", expr) if int(n) >= 2}
        return toks, ints

    def rewrite_sprintfs(self, body, fn, sites):
        calls = list(re.finditer(r"\bcrt_sprintf\(", body))
        if not calls:
            return body
        info = []
        prev = 0
        for m in calls:
            args, end = balanced_args(body, m.end() - 1)
            av = split_args(args or "")
            key = None
            if len(av) >= 2:
                lm = re.fullmatch(r"(?:\(char \*\))?&?(?:DAT|s_\w*?)_?(00[0-9a-f]{6})", av[1])
                if lm:
                    key = ("lit", int(lm.group(1), 16))
                else:
                    asg = re.findall(rf"\b{re.escape(av[1])} = (?:\([^()]*\))?get_rc_string\(({NUM})",
                                     body[:m.start()])
                    key = int(asg[-1], 0) if asg else None
            seg = body[prev:m.start()]
            info.append({"m": m, "end": end, "av": av, "key": key, "seg": seg,
                         "names": set(re.findall(r"\b[A-Za-z_]\w*\b", seg)),
                         "ints": {int(n, 0) for n in re.findall(r"(?<![\w.])(0x[0-9a-f]+|\d+)(?![\w.])", seg)}})
            prev = end
        assign = self.match_sprintfs(info, sites)
        out, i = [], 0
        for k, c in enumerate(info):
            out.append(body[i:c["end"] - 1])
            site, how = assign.get(k, (None, None))
            if len(c["av"]) > 2:
                pass  # Ghidra kept the value arguments
            elif site is None:
                self.spr_log.append((fn, None, "NOT RECOVERED", "no machine-code call matched"))
                out.append(" /*value args lost by Ghidra: not recovered, read the disassembly*/")
            elif site["err"]:
                self.spr_log.append((fn, site["addr"], "NOT RECOVERED", site["err"]))
                out.append(f" /*value args lost by Ghidra: read the disassembly at {site['addr']:#x}*/")
            elif site["vals"]:
                out.append(", " + ", ".join(site["vals"]))
                tag = "args from machine code" + ("; matched by order" if how == "order" else "") + \
                    (f"; {site['note']}" if site.get("note") else "")
                self.spr_log.append((fn, site["addr"], "recovered" + (" (by order)" if how == "order" else ""),
                                     ", ".join(site["vals"])))
                out.append(")")
                out.append(f" /*{tag}*/")
                i = c["end"]
                continue
            else:
                self.spr_log.append((fn, site["addr"], "no value args", "format has no conversions"))
            out.append(")")
            i = c["end"]
        out.append(body[i:])
        return "".join(out)

    def match_sprintfs(self, info, sites):
        """Body call index -> (site, how): same format key, then the value's call / names /
        literals found in the body text since the previous crt_sprintf; ties by address order."""
        assign = {}
        for key in sorted({c["key"] for c in info}, key=str):
            bs = [k for k, c in enumerate(info) if c["key"] == key]
            ms = [s for s in sites if s["key"] == key]
            if key is None or len(bs) != len(ms):
                continue
            ok = {}
            for k in bs:
                for j, s in enumerate(ms):
                    toks, ints = self.sig_tokens(", ".join(v or "" for v in s["vals"]))
                    ok[(k, j)] = toks <= info[k]["names"] and ints <= info[k]["ints"]
            free_b, free_m = list(bs), list(range(len(ms)))
            changed = True
            while changed:
                changed = False
                for k in list(free_b):
                    c = [j for j in free_m if ok[(k, j)]]
                    if len(c) == 1:
                        assign[k] = (ms[c[0]], "text")
                        free_b.remove(k)
                        free_m.remove(c[0])
                        changed = True
                for j in list(free_m):
                    c = [k for k in free_b if ok[(k, j)]]
                    if len(c) == 1 and j in free_m:
                        assign[c[0]] = (ms[j], "text")
                        free_b.remove(c[0])
                        free_m.remove(j)
                        changed = True
            same = len({tuple(ms[j]["vals"]) for j in free_m}) <= 1
            for k, j in zip(free_b, free_m):
                assign[k] = (ms[j], "text" if same else "order")
        return assign

    # ---- per-function rewrite ------------------------------------------------------------
    def value_arg(self, v, fn):
        if v in ("0", "0x0"):
            return "0.0f"
        m = re.fullmatch(r"0x([0-9a-f]{6,8})", v)
        if m:
            s = ffmt(int(m.group(1), 16))
            if s:
                return s
        if re.fullmatch(r"\d+", v):
            self.note_raw("message value: small integer literal (float bits?)", v, fn)
        return v

    def send(self, recv, cls, off, args, fn, recovered=None):
        sname = slot_name(cls, off)
        a = split_args(args)
        if sname is None:
            self.note_raw("vtable call: unknown slot", f"{recv} +{off:#x}", fn)
            return f"{recv}->vt_{off:#x}({args})"
        if sname != "Message":
            return f"{recv}->{sname}({args})"
        note = ""
        if len(a) < 2 and recovered:
            code, val = recovered
            self.rec_log.append((recv, code, val))
            a = [hex(code), hex(val) if val is not None else "?"]
            note = " /*args from machine code*/"
        elif len(a) < 2:
            self.note_raw("Message args lost by Ghidra (not recoverable)", f"{recv}->Message({args})", fn)
            return f"{recv}->Message({args} /*args lost by Ghidra*/)"
        code, val = a[0], a[1]
        if re.fullmatch(NUM, code):
            c = int(code, 0)
            n = self.code_name(cls, c) if cls else None
            if n is None and cls is None:
                n = self.unique_code(c)
            if n:
                code = n
            else:
                self.note_raw("message code not in msgmap for receiver class",
                              f"{cls}:{hex(c)}", fn)
                code = f"{code} /*FT code, {cls or 'unknown receiver'}*/"
        else:
            self.note_raw("message code is a variable", f"{recv}->Message({args})", fn)
            if re.fullmatch(r"[a-z]Var\d+", code) and cls:
                self.code_vars.setdefault(code, set()).add(cls)
        return f"{recv}->Message({code}, {self.value_arg(val, fn)}){note}"

    def unique_code(self, c):
        names = {e["upstream"] for cl in self.msgmap["classes"].values()
                 for k, e in cl["codes"].items() if int(k, 16) == c}
        if len(names) == 1:
            return "MessageCode::" + names.pop().replace("NEW:", "") + " /*receiver unknown; code unique*/"
        return None

    def rewrite_sends(self, body, fn, msends):
        out, i = [], 0
        pat = re.compile(
            r"\(\*\*\(code \*\*\)\("
            r"(?:\*(?:\(int \*\))?(?P<r>[A-Za-z_]\w*\.component|TableG|param_\d+|piVar\d+|unaff_\w+)"
            r"|\*\*\(int \*\*\)\(\(&PTR_PTR_(?P<tt>[0-9a-f]{8})\)\[(?P<ti>[^\]]+)\] \+ 4\)"
            r"|\*\*\(int \*\*\)\((?:\(int\))?TableG \+ 0x13e /\*\?\*/\))"
            r" \+ (?P<off>0x[0-9a-f]+|\d+)\)\)\s*\(")
        explicit = collections.Counter()
        for m in pat.finditer(body):
            if m.group("r") and m.group("r").endswith(".component"):
                args, _ = balanced_args(body, m.end() - 1)
                a = split_args(args or "")
                if len(a) == 2 and all(re.fullmatch(NUM, x) for x in a):
                    explicit[(m.group("r")[:-10], int(a[0], 0), int(a[1], 0))] += 1
        while True:
            m = pat.search(body, i)
            if not m:
                out.append(body[i:])
                break
            args, end = balanced_args(body, m.end() - 1)
            out.append(body[i:m.start()])
            off = int(m.group("off"), 0)
            r = m.group("r")
            if m.group("tt"):
                tt = int(m.group("tt"), 16)
                members = self.tag_table(tt)
                classes = {self.tag_class.get(t) for t in members}
                cls = classes.pop() if len(classes) == 1 else None
                recv = f"tagtab_{tt:08x}[{m.group('ti')}]"
                out.append(self.send(recv, cls, off, args, fn))
            elif r is None:
                out.append(self.send("TableG->LightGroup", "TLightGroup", off, args, fn))
            elif r.endswith(".component"):
                tag = r[:-10]
                cls = self.tag_class.get(tag)
                rec = None
                if off == 4 and len(split_args(args)) < 2:
                    a0 = split_args(args)
                    known = int(a0[0], 0) if a0 and re.fullmatch(NUM, a0[0]) else None
                    rec = self.recover(tag, msends, explicit, known)
                out.append(self.send(tag, cls, off, args, fn, rec))
            elif r == "TableG":
                out.append(self.send("TableG", "TPinballTable", off, args, fn))
            elif r == "param_2" and self.pcls:
                out.append(self.send(r, self.pcls, off, args, fn))
            else:
                self.note_raw("send to a non-tag receiver (class unknown)", r, fn)
                out.append(self.send(r, None, off, args, fn))
            i = end
        return "".join(out)

    def check_recovered(self, fn, msgs):
        """Each Message (code, value) taken from the linear window must also be a send to the
        same receiver in the stack simulation (an independent reading of the same bytes)."""
        for recv, code, val in self.rec_log:
            hit = [m for m in msgs if m[0] == recv and m[1] == code and m[2] == val]
            ok = "confirmed" if hit else "NOT CONFIRMED"
            self.msg_check[ok] += 1
            if not hit:
                self.msg_fail.append(f"{fn}: {recv} ({code:#x}, {val if val is None else hex(val)})")

    def annotate_code_vars(self, body):
        """var = LIT; where var is later the code of a Message to one receiver class."""
        for v, classes in self.code_vars.items():
            if len(classes) != 1:
                continue
            cls = next(iter(classes))

            def sub(m, cls=cls):
                n = self.code_name(cls, int(m.group(2), 0))
                return f"{m.group(1)}; /* {n} */" if n else m.group(0)
            body = re.sub(rf"\b({re.escape(v)} = ({NUM}));", sub, body)
        return body

    def recover(self, tag, msends, explicit, known=None):
        """The one (code, value) left for tag after removing the explicit decompiled sends
        (restricted to the decompiled code when Ghidra kept it but lost the value)."""
        rem = collections.Counter((c, v) for c, v in msends.get(tag, [])
                                  if known is None or c == known)
        for (t, c, v), n in explicit.items():
            if t == tag:
                if (c, v) in rem:
                    rem[(c, v)] -= n
        rem = {k: n for k, n in rem.items() if n > 0}
        if len(rem) == 1:
            return next(iter(rem))
        return None

    def tag_table(self, va):
        out = []
        for k in range(64):
            try:
                p = self.exe.dword(va + 4 * k)
            except Exception:
                break
            if p not in self.tag_addr:
                break
            out.append(self.tag_addr[p])
        return out

    def rewrite_fields(self, body, fn):
        def fld(m):
            tag, off = m.group(2), int(m.group(3), 0)
            cls = self.pcls if tag == "param_2" else self.tag_class.get(tag)
            name = FIELDS.get(cls, {}).get(off) or BASE_FIELDS.get(off)
            if name is None and 7 <= off <= 9:  # a byte of the int MessageField at +6
                return f"((unsigned char *)&{tag}->MessageField)[{off - 6}]"
            if name is None:
                self.note_raw("component field without a name", f"{tag}({cls})+{off:#x}", fn)
                return f"*({m.group(1)} *)((char *){tag} + {off:#x}) /*{cls} +{off:#x}*/"
            if name == "MessageField" and m.group(1) in ("byte", "undefined1", "char"):
                return f"((unsigned char *)&{tag}->MessageField)[0]"
            return f"{tag}->{name}"
        body = re.sub(r"\*\(([\w ]+?) \*\)\((?:\(int\))?([A-Za-z_]\w*)\.component \+ (" + NUM + r")\)",
                      fld, body)
        if self.pcls:  # param_2 of a control function = the component of its tag(s)
            body = re.sub(r"\*\(([\w ]+?) \*\)\((?:\(int\))?(param_2) \+ (" + NUM + r")\)", fld, body)
        # TLightGroup list: Count at +4, items at +8 (part_tlight_tlightgroup.md 1.3)
        body = re.sub(r"\*\(int \*\)\(([A-Za-z_]\w*)->List \+ 4\)", r"\1->List.size()", body)
        body = re.sub(r"\*\([\w ]+\*+\)\(([A-Za-z_]\w*)->List \+ 8 \+ ([^()]+?) \* 4\)",
                      r"\1->List[\2]", body)
        body = re.sub(r"\b([A-Za-z_]\w*)\.component\b", r"\1", body)
        body = re.sub(r"\btag:([A-Za-z_]\w*)", r"&tag_\1", body)
        return body

    def collapse_light_on(self, body):
        """((X->LightOnFlag == 0) && (X->ToggledOnFlag == 0)) && (X->FlasherOnFlag == 0) ->
        !X->light_on(); the != / || form -> X->light_on() (upstream TLight::light_on)."""
        changed = True
        while changed:
            changed = False
            for i in [k for k, ch in enumerate(body) if ch == "("]:
                inner, end = balanced_args(body, i)
                if inner is None or "LightOnFlag" not in inner:
                    continue
                flat = re.sub(r"[()\s]", "", inner)
                m = re.fullmatch(r"(\w+)->(\w+)(==|!=)0(&&|\|\|)\1->(\w+)(?P=c)0\4\1->(\w+)(?P=c)0".replace("(==|!=)", "(?P<c>==|!=)"), flat)
                if not m or {m.group(2), m.group(5), m.group(6)} != \
                        {"LightOnFlag", "ToggledOnFlag", "FlasherOnFlag"}:
                    continue
                if (m.group(3), m.group(4)) not in (("==", "&&"), ("!=", "||")):
                    continue
                neg = "!" if m.group(3) == "==" else ""
                body = body[:i] + f"({neg}{m.group(1)}->light_on())" + body[end:]
                changed = True
                break
        return body

    def rewrite_codes(self, body, fn, name):
        k = self.code_param.get(name)
        cls = "table_control_handler" if name == "table_control_handler" else "control"

        def cname(lit, where):
            n = self.control_code(int(lit, 0), cls)
            if n is None:
                self.note_raw("control code literal not in msgmap", f"{lit} ({where})", fn)
                return lit
            return n
        if k:
            p = f"param_{k}"
            body = re.sub(rf"\b{p} (==|!=) ({NUM})\b",
                          lambda m: f"{p} {m.group(1)} {cname(m.group(2), 'compare')}", body)
            body = re.sub(rf"\b{p} = ({NUM});", lambda m: f"{p} = {cname(m.group(1), 'assign')};", body)
            body = self.rewrite_switch(body, p, cname)
        # calls to control functions with a literal code: Name_addr(code, ...)
        for n, kk in self.code_param.items():
            def sub(m, kk=kk, n=n):
                args = split_args(m.group(2))
                if len(args) >= kk and re.fullmatch(NUM, args[kk - 1]):
                    c = "table_control_handler" if n == "table_control_handler" else "control"
                    nm = self.control_code(int(args[kk - 1], 0), c)
                    if nm:
                        args[kk - 1] = nm
                    else:
                        self.note_raw("control code literal not in msgmap",
                                      f"{args[kk - 1]} (call {n})", fn)
                return f"{m.group(1)}({','.join(args)})"
            body = re.sub(rf"\b({re.escape(n)}(?:_[0-9a-f]+)?)\(([^()]*(?:\([^()]*\)[^()]*)*)\)", sub, body)
        return body

    @staticmethod
    def rewrite_switch(body, p, cname):
        out, i = [], 0
        while True:
            m = re.search(rf"switch\({p}\) \{{", body[i:])
            if not m:
                out.append(body[i:])
                return "".join(out)
            s = i + m.end() - 1
            depth, j = 0, s
            for j in range(s, len(body)):
                depth += body[j] == "{"
                depth -= body[j] == "}"
                if depth == 0:
                    break
            seg = body[s:j + 1]
            # only cases of this switch (depth 1 inside it)
            res, d = [], 0
            for line in seg.splitlines(keepends=True):
                mm = re.match(rf"(\s*)case ({NUM}):", line)
                if mm and d == 1:
                    line = line.replace(f"case {mm.group(2)}:", f"case {cname(mm.group(2), 'case')}:", 1)
                d += line.count("{") - line.count("}")
                res.append(line)
            out.append(body[i:s] + "".join(res))
            i = j + 1

    def rewrite_names(self, body, fn):
        def sub(m):
            a = int(m.group(2), 16)
            if a in self.funcs:
                n = self.fname(a, self.funcs[a]["name"])
                return n if n not in self.dup_names else f"{n}_{a:x}"
            if a in self.names:
                return self.names[a]
            if a in self.engine:
                return self.engine[a]
            self.note_raw("function address without a name", m.group(0), fn)
            return m.group(0)
        body = re.sub(r"\b([A-Za-z]\w*?_|FUN_|func_0x)(?:00)?([0-9a-f]{6})\b(?=\()", self.name_cb(sub), body)
        return body

    def name_cb(self, sub):
        def f(m):
            pre = m.group(1)
            if pre in ("FUN_", "func_0x") or int(m.group(2), 16) in self.funcs:
                return sub(m)
            return m.group(0)
        return f

    def rewrite_misc(self, body, fn):
        # tag / tag-slot addresses passed as integers
        def taglit(m):
            v = int(m.group(1), 16)
            if v in self.tag_addr:
                return f"&tag_{self.tag_addr[v]}"
            if v in self.slot_tag:
                return f"&{self.slot_tag[v]} /*component slot*/"
            return m.group(0)
        body = re.sub(r"(?<![\w&_])(0x(?:00)?4[0-9a-f]{5})\b", taglit, body)
        # Ghidra string labels carry the text: keep the address only (the port reads the text from the EXE)
        body = re.sub(r"\bs_\w*?_(00[0-9a-f]{6})\b", r"exe_str_\1", body)
        # rc strings: keep the id only
        body = re.sub(r"get_rc_string\((" + NUM + r") /\* \"(?:[^\"\\]|\\.)*\" \*/\)",
                      lambda m: f"get_rc_string({int(m.group(1), 0)}) /* string {int(m.group(1), 0)} */",
                      body)
        # TPinballTable fields
        def tf(m):
            t, off, nm = m.group(1), int(m.group(2), 16), m.group(3)
            if nm == "?":
                nm = TABLE_EXTRA.get(off)
            if not nm:
                self.note_raw("TPinballTable field without a name", hex(off), fn)
                return m.group(0)
            nm = re.sub(r"\(.*", "", nm)
            return f"TableG->{nm}"
        body = re.sub(r"\*\(([\w ]+?) \*\)\(\(?(?:int\)?)?TableG \+ (0x[0-9a-f]+) /\*([^*]*)\*/\)", tf, body)
        body = re.sub(r"\(\*\*\(code \*\*\)\*(?:\(int \*\))?([A-Za-z_]\w*)\)\(\)", r"\1->GetTypeId()", body)
        # GetTypeId comparisons
        tvars = set(re.findall(r"\b(\w+) = [^;]*GetTypeId\(\)", body))

        def tid(m):
            v = int(m.group(3), 0)
            return f"{m.group(1)} {m.group(2)} {v} /*{TYPE_ID[v]}*/" if v in TYPE_ID else m.group(0)
        for v in tvars | {"GetTypeId()"}:
            body = re.sub(rf"({re.escape(v)}) (==|!=) ({NUM})\b", tid, body)
        # double high words: (double)((ulonglong)V << 0x20)
        dvars = set(re.findall(r"\(ulonglong\)(\w+) << 0x20", body))
        for v in dvars:
            body = re.sub(rf"\b({re.escape(v)} = 0x([0-9a-f]{{8}}));",
                          lambda m: f"{m.group(1)}; /* double hi word: {dfmt_hi(int(m.group(2), 16))} */",
                          body)
        # other float-like 32-bit immediates: annotate (not replaced)

        def fl(m):
            pre, lit = m.group(1), m.group(2)
            s = float_like(int(lit, 16))
            if not s:
                return m.group(0)
            if pre.endswith("*)"):
                self.note_raw("float bits cast to a pointer (Ghidra artefact)", lit, fn)
                return f"{pre}{lit} /* = {s}? Ghidra cast */"
            return f"{pre}{lit} /* = {s} */"
        lines = []
        for line in body.splitlines(keepends=True):
            if "double hi word" not in line:
                line = re.sub(r"((?:\(\w+ \*\))?)\b(0x[0-9a-f]{8})\b(?! /\*)", fl, line)
            lines.append(line)
        return "".join(lines)

    def header(self, f, name):
        a = int(f["addr"], 16)
        L = [f"// ==== {name}  (FT {f['addr']}, {f['size']} bytes)"]
        if f["handler_of"]:
            L.append("//   control function of: " + ", ".join(
                f"{t} ({self.tag_class.get(t, '?')})" for t in f["handler_of"]))
        callers = [self.fname(int(c, 16), self.funcs[int(c, 16)]["name"])
                   if c.startswith("0x") and int(c, 16) in self.funcs else c for c in f["callers_in_region"]]
        if callers:
            L.append("//   called by: " + ", ".join(callers))
        if f["callers_outside"]:
            L.append("//   called from engine: " + ", ".join(f["callers_outside"]))
        if f["strings"]:
            L.append("//   strings: " + ", ".join(str(s["id"]) for s in f["strings"]))
        gl = []
        for g, kind in f["globals"].items():
            va = int(g[4:], 16)
            doc = self.globals_doc.get(va)
            gl.append(f"{g} [{kind}]" + (f" = {doc}" if doc else ""))
        if gl:
            L.append("//   rule globals: " + "; ".join(gl))
        if f.get("constants"):
            L.append("//   read-only constants: " + "; ".join(f"{k} = {v}" for k, v in f["constants"].items()))
        if self.code_param.get(f["name"]):
            L.append(f"//   param_{self.code_param[f['name']]} = message code (FT literals shown as MessageCode)")
        return "\n".join(L) + "\n"

    def pre_region(self, src):
        """Listing entries for PRE_REGION functions, from the Ghidra export bodies with the same
        tag / string / TableG resolution control_framework.py applies to the region."""
        callees = {}
        exe_name = self.fw["exe"]
        for line in open(os.path.join(self.export_dir, exe_name + ".tsv")):
            c = line.rstrip("\n").split("\t")
            for t in c[3].split(",") if len(c) > 3 else []:
                if re.fullmatch(r"[0-9a-f]{8}", t):
                    callees.setdefault(int(t, 16), []).append(int(c[0], 16))
        lo, hi = (int(x, 16) for x in self.fw["region"])
        out = []
        for a, name in sorted(PRE_REGION.get(self.table, {}).items()):
            body = self.exe.funcs.get(a)
            if body is None:
                self.note_raw("pre-region function missing from the export", hex(a), name)
                continue
            body = body.lstrip("\n")
            body = re.sub(rf"FUN_{self.getter:08x}\(({NUM})\)",
                          lambda m: f"get_rc_string({int(m.group(1), 0)}) /* string {int(m.group(1), 0)} */", body)

            def sym(m):
                v = int(m.group(1), 16)
                if m.group(0).startswith("FUN_"):
                    return m.group(0)
                if v in self.slot_tag:
                    return self.slot_tag[v] + ".component"
                if v in self.tag_addr:
                    return "tag:" + self.tag_addr[v]
                return "TableG" if v == self.table_global else m.group(0)
            body = re.sub(r"(?:_?DAT|FUN|PTR)_([0-9a-f]{8})", sym, body)
            body = re.sub(r"TableG \+ (0x[0-9a-f]+)",
                          lambda m: f"TableG + {m.group(1)} /*{TABLE_FIELDS.get(int(m.group(1), 16), '?')}*/", body)
            callers = sorted(set(callees.get(a, [])) - {a})
            glob = {}
            for g in sorted(set(re.findall(r"\bDAT_([0-9a-f]{8})\b", body))):
                w = re.search(rf"\bDAT_{g}\s*=(?!=)", body)
                glob[f"DAT_{g}"] = "rw" if w and re.search(rf"\bDAT_{g}\b(?!\s*=[^=])", body) else "w" if w else "r"
            f = {"addr": f"{a:#x}", "name": name, "size": self.exe.sizes.get(a, 0), "handler_of": [],
                 "callers_in_region": [], "callers_outside": [],
                 "strings": [{"id": int(i)} for i in re.findall(r"get_rc_string\((\d+)\)", body)],
                 "globals": glob, "constants": {}}
            for c in callers:
                inside = lo <= c <= hi or c in PRE_REGION[self.table]
                (f["callers_in_region"] if inside else f["callers_outside"]).append(
                    self.call_name(c) if c in PRE_REGION[self.table] else f"{c:#x}")
            body = self.rewrite_names(body, name)
            # timer callbacks passed as addresses (the ticker re-arms itself)
            body = re.sub(r"(?<![\w&])0x(4[0-9a-f]{5})\b(?=\))",
                          lambda m: f"&{self.call_name(int(m.group(1), 16))}"
                          if self.call_name(int(m.group(1), 16)) else m.group(0), body)
            self.rec_log, self.code_vars, self.pcls = [], {}, None
            body = self.rewrite_sends(body, name, {})
            body = self.rewrite_fields(body, name)
            body = self.rewrite_misc(body, name)
            hdr = self.header(f, name)
            if name == "FtTextEvent":
                hdr += "//   param_1 = FT text code (0x57-0x6e): TableRules::TextEvent (D2), case labels kept as FT codes\n"
            out.append(hdr + "\n" + body.rstrip() + "\n\n\n")
        return out

    def run(self, region_path):
        src = open(region_path).read()
        parts = re.split(r"^// ---- .*\n", src, flags=re.M)
        out = [f"// {self.fw['exe']} rules: ready-to-port listing generated by re/control_annotate.py\n"
               "// from control_region / control_framework / msgmap / names (see port_listing_notes.md).\n"
               "// Derived from decompiled code: never commit. No game text: strings are ids only.\n"
               "// Conventions: TAG->Field / TAG->Message(MessageCode::X, f) on the tag's component;\n"
               "// tagtab_ADDR[i] = table of tags in the EXE; '/* = 1.5f */' marks a 32-bit immediate\n"
               "// that decodes to a short float (annotation only, the literal may be an integer).\n"
               "// crt_sprintf value arguments marked /*args from machine code*/ come from a stack simulation\n"
               "// of the call's pushes: ret_of(F(...)) = the value F returned at its call above (do not call\n"
               "// it again); either(a, b) = tail-merged code, the value depends on the path taken; '_' = an\n"
               "// argument of that earlier call the simulation did not track (Ghidra shows the call).\n"
               "// exe_str_ADDR = a string in the EXE's data at ADDR (text left out, read it from the EXE).\n\n"]
        tabs = sorted({int(t, 16) for t in re.findall(r"PTR_PTR_([0-9a-f]{8})\)\[", src)})
        for t in tabs:
            out.append(f"// tagtab_{t:08x} = {{{', '.join(self.tag_table(t))}}}\n")
        out.append("\n")
        pre = self.pre_region(src)
        if pre:
            out.append("// ---- per-table text functions before the control region (engine side of the D2\n"
                       "// TextEvent hook; codes are FT text codes 0x57-0x6e, msgmap has no names for them)\n\n")
            out.extend(pre)
            out.append("// ---- control region\n\n")
        for p in parts[1:]:
            m = re.match(r"// ==== ([0-9a-f]{8}) ", p)
            if not m:
                continue
            a = int(m.group(1), 16)
            f = self.funcs.get(a)
            if f is None:
                self.note_raw("region body without framework entry", hex(a), "?")
                continue
            name = self.fname(a, f["name"])
            if name in self.dup_names:
                name = f"{name}_{a:x}"
            body = p.split("\n", 1)[1]
            hcls = {self.tag_class.get(t) for t in f["handler_of"]}
            self.pcls = hcls.pop() if len(hcls) == 1 and self.code_param.get(f["name"]) == 1 else None
            if re.search(r"\(\*\*\(code \*\*\)\(\w+ \+ 4\)\)\(", body):
                self.note_raw("Message through a vtable held in a local (receiver merged by Ghidra)",
                              re.search(r"\(\*\*\(code \*\*\)\(\w+ \+ 4\)\)\([^;]*", body).group(0), name)
            msends = self.machine_sends(a) if re.search(r"\.component \+ 4\)\)\((?:[^,()]*)\)", body) else {}
            sites, msgs = self.machine_calls(a)
            self.rec_log = []
            body = self.rewrite_names(body, name)
            body = self.rewrite_codes(body, name, f["name"])
            self.code_vars = {}
            body = self.rewrite_sends(body, name, msends)
            body = self.annotate_code_vars(body)
            body = self.rewrite_sprintfs(body, name, sites)
            self.check_recovered(name, msgs)
            body = self.rewrite_fields(body, name)
            body = self.collapse_light_on(body)
            body = self.rewrite_misc(body, name)
            left = sorted(set(re.findall(r"\b(?:DAT|PTR_PTR|PTR_DAT|PTR)_[0-9a-f]{8}\b", body)))
            for g in left:
                self.raw["global symbol left as DAT_/PTR_ (rule global or table)"] += 1
            out.append(self.header(f, name) + body.rstrip() + "\n\n\n")
        return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("table", choices=["dragon", "pirates"])
    ap.add_argument("exe")
    ap.add_argument("export_dir")
    ap.add_argument("--names", action="append", default=[])
    ap.add_argument("--spec-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                       "work", "spec"))
    ap.add_argument("--framework-md")
    ap.add_argument("--out")
    a = ap.parse_args()
    names = {}
    for path in a.names:
        raw = json.load(open(path))
        for k, v in list(raw.items()) + list(raw.get("_framework", {}).items()):
            if k.startswith("0x"):
                names[int(k, 16)] = v[0] if isinstance(v, list) else v
    an = Annotator(a.table, a.exe, a.export_dir, names, a.spec_dir, a.framework_md)
    text = an.run(os.path.join(a.spec_dir, f"control_region_{a.table}.c"))
    out = a.out or os.path.join(a.spec_dir, f"port_listing_{a.table}.c")
    with open(out, "w") as f:
        f.write(text)
    print(f"{a.table}: wrote {out}")
    print("crt_sprintf value arguments (function, call address, status, values):")
    for fn, ad, st, v in an.spr_log:
        print(f"  {fn} {ad:#x} {st}: {v}" if ad else f"  {fn} - {st}: {v}")
    print("Message args from machine code vs the stack simulation: " +
          ", ".join(f"{k} {n}" for k, n in sorted(an.msg_check.items())))
    for e in an.msg_fail:
        print(f"  NOT CONFIRMED: {e}")
    print("left raw (count, kind, examples):")
    for k, n in an.raw.most_common():
        print(f"  {n:5d}  {k}")
        for e in an.raw_examples.get(k, []):
            print(f"           {e}")


if __name__ == "__main__":
    main()
