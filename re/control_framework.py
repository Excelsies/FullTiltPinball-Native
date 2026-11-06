#!/usr/bin/env python
"""Map the table-rules ("control") module of a Full Tilt exe beyond its per-part handlers: every
function of the control region with its callers, callees, rc strings, component tags, TableG
fields, other globals (read / write), messages sent to components; plus, optionally, which state
a proposed split of the functions into implementation groups shares between groups.

The control region is the run of Ghidra functions starting at control::make_component_link (found
by control_tables.py's locator) and continuing while a function references a component tag, a
control-table handler, or TableG, or is called by / calls a function that does.

Facts come from two sources:
  machine code (capstone)   absolute data operands with access kind (r = read, w = write,
                            rw = read-modify-write, & = address taken), call targets
  Ghidra export (.c)        rc string ids passed to the string getter, component field accesses
                            "*(T *)(DAT_x + off)" (w when assigned), messages sent through the
                            component vtable "(**(code **)(*DAT_x + 4))(code, ...)"
TableG field names: Full Tilt offsets aligned with upstream TPinballTable members using CADET.EXE's
TPinballTable::Message Reset branch (0x419600, case 0x402) and CADET's table_set_* functions.

usage: control_framework.py EXE EXPORT_DIR [--names FILE] [--groups FILE] [--out-dir DIR]
  --names FILE    JSON {"0x<addr>": [name, note] | name} for handlers / framework functions
  --groups FILE   JSON {"group": ["0x<addr>", ...], ...}: report state shared between groups
  A names file may also carry framework names under "_framework" ({"0x<addr>": name}) and groups
  under "_groups" (then --groups can point at the same file), so one file serves both tools.
  writes DIR/control_framework_<stem>.json, DIR/control_framework_<stem>.md and
  DIR/control_region_<stem>.c (export bodies of the region, tags / strings / names resolved).
  Decompiled-material derived output: keep it under re/work/ (gitignored).
example:
  re/work/.venv/bin/python re/control_framework.py re/work/bin/DRAGON.EXE re/work/export \\
      --names re/work/spec/dragon_framework_names.json --out-dir re/work/spec
"""
import argparse
import json
import os
import re
import struct
import sys

import capstone
import pefile
from capstone import x86

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from control_tables import Image  # noqa: E402
from control_tables_report import rc_strings  # noqa: E402

TABLE_FIELDS = {  # Full Tilt TPinballTable offset -> upstream member (see docstring)
    0x15e: "ScoreMultiplier", 0x162: "ScoreAdded", 0x166: "ReflexShotScore", 0x16a: "BonusScore",
    0x16e: "BonusScoreFlag", 0x172: "JackpotScore", 0x176: "JackpotScoreFlag",
    0x17a: "FT_0x17a(reset 20000)", 0x17e: "FT_0x17e", 0x182: "FT_0x182", 0x186: "BallCount",
    0x18a: "MaxBallCount", 0x18e: "ExtraBalls", 0x192: "MultiballCount",
    0x196: "BallLockedCounter", 0x19a: "MultiballFlag", 0x19e: "UnknownP78",
    0x1a2: "ReplayActiveFlag", 0x1a6: "ReplayTimer", 0x1aa: "UnknownP81", 0x1ae: "UnknownP82",
    0x1b2: "TiltLockFlag"}
WRITE_MN = {"mov", "movzx", "movsx", "fstp", "fst", "fistp", "fist", "stosd", "setne", "sete"}
RMW_MN = {"inc", "dec", "add", "sub", "or", "and", "xor", "neg", "not", "shl", "shr", "sar", "adc",
          "sbb"}
DAT = r"_?DAT_([0-9a-f]{8})"
FIELD = re.compile(r"\*\([^()]*\*\)\((?:\(int\))?" + DAT + r" \+ (0x[0-9a-f]+|\d+)\)(\s*=(?!=))?")
DIRECT_W = re.compile(r"(?<![\w*(])" + DAT + r"\s*=(?!=)")
SEND = re.compile(r"\(\*\*\(code \*\*\)\(\*\(?(?:int \*\))?" + DAT +
                  r" \+ (0x[0-9a-f]+|\d+)\)\)\(([^,)]*)")


LIGHT_ON = {0x42, 0x46, 0x4e}  # TLight flags read together by the inlined light_on() test
FIELD_NAMES = {0x5: "(ActiveFlag)", 0x6: "(MessageField)"}


def hx(v):
    return f"{v:#x}"


class Region:
    def __init__(self, exe_path, export_dir, names):
        self.img = Image(exe_path, export_dir)
        self.names = names
        img = self.img
        self.funcs = sorted(img.exe.functions)
        self.handlers = {e["handler"] for e in img.score_entries}
        self.handler_tags = {}
        for e in img.score_entries:
            self.handler_tags.setdefault(e["handler"], []).append(e["tag"])
        src = open(os.path.join(export_dir, os.path.basename(exe_path) + ".c")).read()
        parts = re.split(r"^(// ==== ([0-9a-f]{8}) \S+ size \d+)$", src, flags=re.M)
        self.bodies = {int(parts[i + 1], 16): parts[i] + parts[i + 2] for i in range(1, len(parts), 3)}
        self.getter = next((a for a, b in self.bodies.items()
                            if "LoadStringA(" in b and "(short param_1)" in b), None)
        self.strings = rc_strings(exe_path)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        pe = pefile.PE(exe_path, fast_load=True)
        self.sections = [(img.base + x.VirtualAddress, img.base + x.VirtualAddress + x.Misc_VirtualSize,
                          bool(x.Characteristics & 0x80000000)) for x in pe.sections]
        self.data_lo = img.base + 0x1000
        self.data_hi = img.base + len(img.img)
        self.info = {a: self.scan(a) for a in self.funcs}
        self.callers = {}
        for a, i in self.info.items():
            for c in i["calls"]:
                self.callers.setdefault(c, set()).add(a)
        self.region = self.find_region()

    def name(self, a):
        n = self.names.get(a)
        if n:
            return n
        if a in self.handlers:
            return "handler(" + ",".join(self.handler_tags[a]) + ")"
        return f"FUN_{a:08x}"

    def tag_of(self, va):
        """'name.component' for a tag's component slot, 'tag:name' for the tag itself."""
        tags = self.img.all_tags
        if va - 4 in tags:
            return tags[va - 4] + ".component"
        if va in tags:
            return "tag:" + tags[va]
        return None

    def scan(self, a):
        img = self.img
        size = img.exe.functions[a]
        blob = img.exe.code[a - img.exe.start:a - img.exe.start + size]
        calls, data = [], []
        for ins in self.md.disasm(blob, a):
            if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                calls.append(int(ins.op_str, 16))
            for k, op in enumerate(ins.operands):
                if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    va = op.mem.disp & 0xFFFFFFFF
                    if not self.data_lo <= va < self.data_hi or img.exe.in_text(va):
                        continue
                    if k == 0 and len(ins.operands) > 1 and ins.mnemonic in WRITE_MN:
                        kind = "w"
                    elif k == 0 and ins.mnemonic in WRITE_MN - {"mov", "movzx", "movsx"}:
                        kind = "w"
                    elif k == 0 and ins.mnemonic in RMW_MN:
                        kind = "rw"
                    else:
                        kind = "r"
                    data.append((va, kind, op.size))
                elif op.type == x86.X86_OP_IMM and ins.mnemonic != "call" and \
                        not ins.mnemonic.startswith("j"):
                    va = op.imm & 0xFFFFFFFF
                    if self.data_lo <= va < self.data_hi and not img.exe.in_text(va):
                        data.append((va, "&", 0))
        body = self.bodies.get(a, "")
        strings = []
        if self.getter:
            for m in re.finditer(rf"FUN_{self.getter:08x}\((0x[0-9a-f]+|\d+)\)", body):
                strings.append(int(m.group(1), 0))
        fields = []
        for m in FIELD.finditer(body):
            fields.append((int(m.group(1), 16), int(m.group(2), 0), "w" if m.group(3) else "r"))
        sends = []
        for m in SEND.finditer(body):
            sends.append((int(m.group(1), 16), int(m.group(2), 0), m.group(3).strip()))
        return {"size": size, "calls": calls, "data": data, "strings": strings, "fields": fields,
                "sends": sends}

    def read_only(self, va):
        sec = next((s for s in self.sections if s[0] <= va < s[1]), None)
        return sec is not None and not sec[2]

    def const_value(self, va, size):
        off = va - self.img.base
        if size == 8:
            return struct.unpack_from("<d", self.img.img, off)[0]
        if size == 4:
            return round(struct.unpack_from("<f", self.img.img, off)[0], 6)
        return int.from_bytes(self.img.img[off:off + max(size, 1)], "little")

    def touches_control(self, a):
        i = self.info[a]
        if a in self.handlers or any(c in self.handlers for c in i["calls"]):
            return True
        return any(self.tag_of(va) or va == self.img.table_global for va, _, _ in i["data"])

    def find_region(self):
        """Contiguous run from make_component_link of functions that touch control state or
        call / are called by such a function (anywhere in the exe)."""
        touch = {a for a in self.funcs if self.touches_control(a)}
        k = self.funcs.index(self.img.make_component_link)
        region = []
        while k < len(self.funcs):
            a = self.funcs[k]
            linked = any(c in touch for c in self.info[a]["calls"]) or \
                any(r in touch for r in self.callers.get(a, ()))
            if region and a not in touch and not linked:
                break
            region.append(a)
            k += 1
        return region

    def global_name(self, va):
        t = self.tag_of(va)
        if t:
            return t
        if va == self.img.table_global:
            return "TableG"
        return f"DAT_{va:08x}"

    def summary(self, a):
        i = self.info[a]
        rs = set(self.region)
        tg = self.img.table_global
        state, comps, tablef, consts = {}, set(), {}, {}
        for va, kind, size in i["data"]:
            t = self.tag_of(va)
            if t and t.endswith(".component"):
                comps.add(t[:-10])
            elif va == tg or t:
                continue
            elif self.read_only(va) and kind == "r":
                consts[f"DAT_{va:08x}"] = self.const_value(va, size)
            else:
                state.setdefault(va, set()).add(kind)
        cfields = {}
        light_on = set()
        for va, off, kind in i["fields"]:
            if va == tg:
                tablef.setdefault(off, set()).add(kind)
                continue
            t = self.tag_of(va)
            base = t[:-10] if t and t.endswith('.component') else self.global_name(va)
            if off in LIGHT_ON and kind == "r":
                light_on.add(base)
                continue
            key = f"{base}+{hx(off)}" + FIELD_NAMES.get(off, "")
            cfields.setdefault(key, set()).add(kind)
        sends = {}
        for va, off, code in i["sends"]:
            t = self.tag_of(va)
            key = t[:-10] if t and t.endswith(".component") else self.global_name(va)
            slot = "msg" if off == 4 else f"vt+{hx(off)}"
            sends.setdefault(key, []).append(f"{slot}({code})" if slot != "msg" else code)
        return {
            "addr": hx(a), "name": self.name(a), "size": i["size"],
            "handler_of": self.handler_tags.get(a, []),
            "callers_in_region": sorted(hx(c) for c in self.callers.get(a, ()) if c in rs),
            "callers_outside": sorted(hx(c) for c in self.callers.get(a, ()) if c not in rs),
            "calls_in_region": sorted({hx(c) for c in i["calls"] if c in rs}),
            "calls_outside": sorted({hx(c) for c in i["calls"] if c not in rs}),
            "strings": [{"id": s, "text": self.strings.get(s, "?")} for s in i["strings"]],
            "components": sorted(comps),
            "component_fields": {k: "".join(sorted(v)) for k, v in sorted(cfields.items())},
            "table_fields": {f"{hx(k)} {TABLE_FIELDS.get(k, '?')}": "".join(sorted(v))
                             for k, v in sorted(tablef.items())},
            "globals": {f"DAT_{k:08x}": "".join(sorted(v)) for k, v in sorted(state.items())},
            "constants": consts,
            "light_on_reads": sorted(light_on),
            "sends": sends,
        }

    def listing(self, out_path):
        def sym(m):
            v = int(m.group(1), 16)
            t = self.tag_of(v)
            if m.group(0).startswith("FUN_") and v in self.info:
                n = self.name(v)
                return n if n.startswith("FUN_") else f"{n}_{v:x}"
            if t and not m.group(0).startswith("FUN_"):
                return t
            if v == self.img.table_global and not m.group(0).startswith("FUN_"):
                return "TableG"
            return m.group(0)

        def fld(m):
            off = int(m.group(1), 0)
            return f"TableG + {m.group(1)} /*{TABLE_FIELDS.get(off, '?')}*/"

        with open(out_path, "w") as f:
            f.write(f"// {os.path.basename(self.img.path)} control region {hx(self.region[0])}.."
                    f"{hx(self.region[-1])}: Ghidra export bodies, tags / strings / names resolved.\n"
                    "// Decompiled material: never commit.\n\n")
            for a in self.region:
                body = self.bodies.get(a, f"// ==== {a:08x} (no export body)\n")
                if self.getter:
                    body = re.sub(rf"FUN_{self.getter:08x}\((0x[0-9a-f]+|\d+)\)",
                                  lambda m: f"get_rc_string({m.group(1)} /* "
                                            f"{json.dumps(self.strings.get(int(m.group(1), 0), '?'))} */)",
                                  body)
                body = re.sub(r"(?:_?DAT|FUN|PTR)_([0-9a-f]{8})", sym, body)
                body = re.sub(r"TableG \+ (0x[0-9a-f]+)", fld, body)
                n = self.name(a)
                f.write(f"// ---- {n}\n" + body + "\n")


def group_report(reg, rows, groups):
    """State touched per group; items written by one group and read/written by another."""
    owner = {}
    for g, addrs in groups.items():
        for x in addrs:
            a = int(x, 16)
            if a in owner:
                raise SystemExit(f"groups: {x} in both {owner[a]} and {g}")
            owner[a] = g
    by_addr = {int(r["addr"], 16): r for r in rows}
    unassigned = [hx(a) for a in reg.region if a not in owner]
    unknown = [hx(a) for a in owner if a not in by_addr]
    items = {}  # state item -> {group: kinds}

    def add(item, g, kinds):
        items.setdefault(item, {}).setdefault(g, set()).update(kinds)

    for a, g in owner.items():
        r = by_addr.get(a)
        if r is None:
            continue
        for k, v in r["globals"].items():
            add(k, g, v)
        for k, v in r["component_fields"].items():
            add(k, g, v)
        for k, v in r["table_fields"].items():
            add("TableG." + k, g, v)
    calls = {}
    for a, g in owner.items():
        r = by_addr.get(a)
        if r is None:
            continue
        for c in r["calls_in_region"]:
            cg = owner.get(int(c, 16), "unassigned")
            if cg != g:
                calls.setdefault((g, cg), set()).add(f"{r['name']} -> {reg.name(int(c, 16))}")
    shared = {}
    for item, gs in items.items():
        writers = [g for g, k in gs.items() if "w" in k]
        if len(gs) > 1 and writers:
            shared[item] = {g: "".join(sorted(k)) for g, k in sorted(gs.items())}
    return {"unassigned": unassigned, "unknown": unknown, "shared": dict(sorted(shared.items())),
            "cross_calls": {f"{a} -> {b}": sorted(v) for (a, b), v in sorted(calls.items())}}


def markdown(reg, rows, groups_out):
    L = [f"# {os.path.basename(reg.img.path)} control region (generated by re/control_framework.py)",
         "", "Decompiled-material derived: keep under re/work/.", "",
         f"Region {hx(reg.region[0])}..{hx(reg.region[-1])}: {len(reg.region)} functions, "
         f"{sum(reg.info[a]['size'] for a in reg.region)} bytes; "
         f"{len([a for a in reg.region if a in reg.handlers])} control-table handlers.", "",
         "## Functions", "",
         "| addr | bytes | name | called from | strings | TableG fields | globals |",
         "|---|---|---|---|---|---|---|"]
    for r in rows:
        calls = ", ".join(r["callers_in_region"][:6]) + (" ..." if len(r["callers_in_region"]) > 6 else "")
        if r["callers_outside"]:
            calls += " / ext " + ", ".join(r["callers_outside"])
        L.append(f"| {r['addr']} | {r['size']} | {r['name']} | {calls} | "
                 f"{', '.join(hx(s['id']) for s in r['strings'])} | "
                 f"{', '.join(k + ':' + v for k, v in r['table_fields'].items())} | "
                 f"{', '.join(k + ':' + v for k, v in r['globals'].items())} |")
    gl = {}
    for r in rows:
        for k, v in r["globals"].items():
            gl.setdefault(k, []).append(f"{r['name']}:{v}")
    L += ["", "## Non-component globals (r read, w write, rw modify, & address taken)", ""]
    for k in sorted(gl):
        L.append(f"- {k}: " + "; ".join(gl[k]))
    cf = {}
    for r in rows:
        for k, v in r["component_fields"].items():
            cf.setdefault(k, []).append(f"{r['name']}:{v}")
    L += ["", "## Component fields read / written by rule code", ""]
    for k in sorted(cf):
        L.append(f"- {k}: " + "; ".join(cf[k]))
    lo, co = {}, {}
    for r in rows:
        for k in r["light_on_reads"]:
            lo.setdefault(k, []).append(r["name"])
        for k, v in r["constants"].items():
            co.setdefault(f"{k} = {v}", []).append(r["name"])
    L += ["", "## Lights tested with the inlined light_on() (fields +0x42/+0x46/+0x4e)", ""]
    L += [f"- {k}: " + ", ".join(v) for k, v in sorted(lo.items())]
    L += ["", "## Read-only constants", ""]
    L += [f"- {k}: " + ", ".join(v) for k, v in sorted(co.items())]
    if groups_out:
        L += ["", "## Group split check", "",
              f"- unassigned functions: {', '.join(groups_out['unassigned']) or 'none'}",
              f"- unknown addresses in groups file: {', '.join(groups_out['unknown']) or 'none'}",
              "", "### State shared between groups (written by at least one)", ""]
        for k, v in groups_out["shared"].items():
            L.append(f"- {k}: " + ", ".join(f"{g}:{x}" for g, x in v.items()))
        L += ["", "### Calls across groups", ""]
        for k, v in groups_out["cross_calls"].items():
            L.append(f"- {k}: " + "; ".join(v))
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("exe")
    ap.add_argument("export_dir")
    ap.add_argument("--names")
    ap.add_argument("--groups")
    ap.add_argument("--out-dir", default=".")
    a = ap.parse_args()
    names = {}
    if a.names:
        raw = json.load(open(a.names))
        for k, v in list(raw.items()) + list(raw.get("_framework", {}).items()):
            if not k.startswith("_"):
                names[int(k, 16)] = v[0] if isinstance(v, list) else v
    reg = Region(a.exe, a.export_dir, names)
    rows = [reg.summary(x) for x in reg.region]
    groups_out = None
    if a.groups:
        groups = json.load(open(a.groups))
        groups_out = group_report(reg, rows, groups.get("_groups", groups))
    stem = reg.img.stem
    os.makedirs(a.out_dir, exist_ok=True)
    with open(os.path.join(a.out_dir, f"control_framework_{stem}.json"), "w") as f:
        json.dump({"exe": os.path.basename(a.exe), "region": [hx(reg.region[0]), hx(reg.region[-1])],
                   "string_getter": hx(reg.getter) if reg.getter else None,
                   "table_global": hx(reg.img.table_global), "functions": rows,
                   "groups": groups_out}, f, indent=1)
    with open(os.path.join(a.out_dir, f"control_framework_{stem}.md"), "w") as f:
        f.write(markdown(reg, rows, groups_out))
    reg.listing(os.path.join(a.out_dir, f"control_region_{stem}.c"))
    print(f"{stem}: region {hx(reg.region[0])}..{hx(reg.region[-1])} ({len(reg.region)} functions)"
          f" -> {a.out_dir}/control_framework_{stem}.{{json,md}}, control_region_{stem}.c")
    if groups_out:
        print(f"groups: {len(groups_out['shared'])} shared state items, "
              f"{len(groups_out['unassigned'])} unassigned functions")


if __name__ == "__main__":
    main()
