#!/usr/bin/env python
"""Extract Full Tilt's table-rules data tables (upstream control.cpp's score_components /
simple_components / component tags) from CADET.EXE, DRAGON.EXE or PIRATES.EXE, name them against
SpaceCadetPinball's control.cpp and classify each table's handlers against CADET.EXE's.

In every exe the rules are static data, laid out as upstream's control.cpp declares them:
  tag             {char* name; TPinballComponent* component}            8 bytes
  score entry     {tag*; handler; int score_count; int* scores}          16 bytes (component_info)
  simple entry    tag*                                                   4 bytes
control::make_links (found by its machine code) gives the arrays' addresses and lengths:
  mov r,<score_components>; mov r,<count>; mov [TableG],eax; loop {add r,0x10; dec r; jne}
  mov esi,<simple_components>; ...; call make_component_link; cmp esi,<simple end>; jb

The reference exe (CADET.EXE) is mapped onto control.cpp by array position: its score entry i is
upstream's score_components[i], so its handler address gets the upstream function name and its
score arrays the upstream control_*_score* names; tag names, counts and scores are verified.
Each other exe's handlers are then fingerprinted the way fn_match.py does (instructions with
addresses / constants > 0xFFF masked) and compared with every CADET.EXE function:
  identical   same instructions and constants, same component tags referenced, equivalent
              callees (callee fingerprints equal) -> reuse the upstream function
  same-code   same masked instructions but other tags / callees / constants
  similar     best difflib ratio >= 0.80 against a CADET.EXE function
  new         no counterpart (table rules to reverse engineer)
Part types come from the table's .DAT (table_objects, attribute 1025; via dat_parts.py).

Function bounds come from the Ghidra export index (<EXE>.tsv from ghidra_scripts/ExportAll.java).

usage: control_tables.py --ref CADET.EXE --upstream control.cpp --export-dir EXPORT_DIR
                         [--dat-dir FULLTILT_DIR] [--annotate] [--names STEM=FILE ...]
                         --out-dir DIR [TARGET.EXE ...]
  --names dragon=names.json  manual handler names {"0x<addr>": [name, note]} (else auto from tags)
  --also-vs DRAGON.EXE       also classify each other target's handlers against this exe (key
                             "vs_<stem>" per handler: class / match / ratio / that exe's --names name)
  writes DIR/control_tables_<exe stem>.json for the reference and each target, and
  DIR/control_tables.md (readable summary); with --annotate also DIR/control_handlers_<stem>.c,
  the handlers' decompiled bodies from the export with tag globals named (keep under re/work/).
"""
import argparse
import difflib
import json
import os
import re
import struct
import sys

import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dat_parts import KNOWN, TABLES, groups, GROUP_NAME, SHORT_ARRAY  # noqa: E402
from fn_match import Exe, fingerprint, SIMILAR  # noqa: E402
from control_tables_report import annotate, markdown  # noqa: E402

PART_CLASS = {**KNOWN, **{1034: "TPlunger(flag 1)", 1035: "type1035", 1036: "type1036",
                             1037: "type1037", 1038: "type1038", 1042: "type1042",
                             1043: "type1043"}}
MAKE_LINKS = re.compile(rb"\x8b\x44\x24\x04\x56\x57([\xb8-\xbf])(....)([\xb8-\xbf])(....)\xa3(....)",
                        re.S)
SIMPLE_LOOP = re.compile(rb"\xbe(....)\x8b\x06\x83\xc6\x04\x50\xe8(....)\x81\xfe(....)", re.S)
HEX = re.compile(r"0x[0-9a-f]+")


def u32(b):
    return struct.unpack("<I", b)[0]


class Image:
    """PE image with the control tables located and parsed."""

    def __init__(self, path, export_dir):
        self.path = path
        self.stem = os.path.splitext(os.path.basename(path))[0].lower()
        pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.img = pe.get_memory_mapped_image()
        self.exe = Exe(path)  # fn_match's disassembler + fingerprints
        # fn_match sizes run to the next detected start, which swallows following functions that
        # lack an ebp prologue; Ghidra's function bodies (export index) are exact.
        self.exe.functions = ghidra_functions(
            os.path.join(export_dir, os.path.basename(path) + ".tsv"), self.exe)
        self.locate()
        self.parse()

    def dword(self, va):
        return struct.unpack_from("<I", self.img, va - self.base)[0]

    def cstr(self, va, limit=64):
        off = va - self.base
        raw = self.img[off:off + limit].split(b"\0")[0]
        return raw.decode("latin-1")

    def locate(self):
        hits = list(MAKE_LINKS.finditer(self.img))
        if len(hits) != 1:
            raise SystemExit(f"{self.path}: make_links prologue found {len(hits)} times (want 1)")
        m = hits[0]
        imm = [u32(m.group(2)), u32(m.group(4))]
        if not (min(imm) < 0x1000 <= max(imm)):
            raise SystemExit(f"{self.path}: make_links immediates {imm} are not (count, array)")
        self.make_links = self.base + m.start()
        self.score_addr, self.score_count = max(imm), min(imm)
        self.table_global = u32(m.group(5))
        loop = list(SIMPLE_LOOP.finditer(self.img, m.end(), m.end() + 0x60))
        if len(loop) != 1:
            raise SystemExit(f"{self.path}: simple_components loop found {len(loop)} times (want 1)")
        s = loop[0]
        self.simple_addr, simple_end = u32(s.group(1)), u32(s.group(3))
        self.make_component_link = (self.base + s.start(2) + 4 + struct.unpack("<i", s.group(2))[0]) \
            & 0xFFFFFFFF
        if (simple_end - self.simple_addr) % 4 or simple_end <= self.simple_addr:
            raise SystemExit(f"{self.path}: bad simple_components bounds")
        self.simple_count = (simple_end - self.simple_addr) // 4

    def tag_name(self, tag):
        if self.dword(tag + 4) != 0:
            raise SystemExit(f"{self.path}: tag {tag:#x} has a non-zero component field")
        return self.cstr(self.dword(tag))

    def parse(self):
        self.tags = {}
        self.score_entries = []
        for i in range(self.score_count):
            va = self.score_addr + 16 * i
            tag, handler, count, scores = (self.dword(va + 4 * k) for k in range(4))
            if not self.exe.in_text(handler):
                raise SystemExit(f"{self.path}: score entry {i} handler {handler:#x} not in .text")
            if (count == 0) != (scores == 0):
                raise SystemExit(f"{self.path}: score entry {i}: count {count} / scores {scores:#x}")
            self.tags.setdefault(tag, self.tag_name(tag))
            values = [struct.unpack_from("<i", self.img, scores - self.base + 4 * k)[0]
                      for k in range(count)]
            self.score_entries.append({"index": i, "entry_addr": va, "tag_addr": tag,
                                       "tag": self.tags[tag], "handler": handler, "count": count,
                                       "scores_addr": scores, "scores": values})
        self.simple_entries = []
        for i in range(self.simple_count):
            tag = self.dword(self.simple_addr + 4 * i)
            self.tags.setdefault(tag, self.tag_name(tag))
            self.simple_entries.append({"index": i, "tag_addr": tag, "tag": self.tags[tag]})
        self.scan_unlisted_tags()

    def scan_unlisted_tags(self):
        """Tag-shaped structs between the first listed tag and the arrays that neither array
        lists (tags the rule code uses directly)."""
        lo, hi = min(self.tags), self.score_addr
        self.unlisted = {}
        for va in range(lo, hi, 4):
            ptr = self.dword(va)
            if va in self.tags or not lo - 64 <= ptr < va or self.dword(va + 4) != 0:
                continue
            name = self.cstr(ptr)
            if len(name) >= 2 and re.fullmatch(r"[A-Za-z0-9_]+", name) and \
                    self.img[ptr - self.base + len(name)] == 0:
                self.unlisted[va] = name
        self.all_tags = {**self.tags, **self.unlisted}

    def raw_tokens(self, addr):
        """Instructions with only image addresses masked (constants such as floats kept)."""
        size = self.exe.functions[addr]
        blob = self.exe.code[addr - self.exe.start:addr - self.exe.start + size]
        hi = self.base + len(self.img)
        return [f"{i.mnemonic} " + HEX.sub(lambda m: "A" if self.base <= int(m.group(), 16) < hi
                                           else m.group(), i.op_str)
                for i in self.exe.md.disasm(blob, addr)]

    def refs(self, addr):
        """(tag names referenced, call targets) of the function at addr, in instruction order."""
        size = self.exe.functions[addr]
        blob = self.exe.code[addr - self.exe.start:addr - self.exe.start + size]
        tags, calls = [], []
        for insn in self.exe.md.disasm(blob, addr):
            if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
                calls.append(int(insn.op_str, 16))
            for word in HEX.findall(insn.op_str):
                v = int(word, 16)
                if v in self.all_tags:
                    tags.append(self.all_tags[v])
                elif v - 4 in self.all_tags:
                    tags.append(self.all_tags[v - 4] + ".component")
            if insn.mnemonic in ("ret", "jmp") and insn.address + insn.size >= addr + size - 16:
                break
        return tags, calls


def ghidra_functions(tsv, exe):
    """{entry: body size} from ExportAll.java's index (entry, name, size, callees)."""
    if not os.path.exists(tsv):
        raise SystemExit(f"{tsv}: missing; export the exe with ghidra_scripts/ExportAll.java first")
    out = {}
    for line in open(tsv):
        f = line.rstrip("\n").split("\t")
        addr = int(f[0], 16)
        if exe.in_text(addr):
            out[addr] = int(f[2])
    return out


def dat_part_types(dat_path):
    """{group name: part type} for the parts in the table's table_objects list."""
    all_groups = list(groups(dat_path))
    names = []
    for entries in all_groups:
        n = [b.split(b"\0")[0].decode("latin-1") for t, b in entries if t == GROUP_NAME]
        names.append(n[0] if n else None)
    objects = None
    for entries, name in zip(all_groups, names):
        if name == "table_objects":
            for t, b in entries:
                if t == SHORT_ARRAY:
                    shorts = struct.unpack(f"<{len(b) // 2}h", b)
                    if shorts[0] == 1025:
                        objects = list(zip(shorts[1::2], shorts[2::2]))
    if objects is None:
        raise SystemExit(f"{dat_path}: no table_objects list")
    return {names[g]: t for t, g in objects if 0 <= g < len(names) and names[g]}


def parse_upstream(path):
    src = open(path, encoding="utf-8").read()
    tags = {v: (cls, name) for cls, v, name in
            re.findall(r'component_tag<(\w+)> (\w+) = \{"([^"]+)"\};', src)}
    arrays = {n: [int(x) for x in body.replace(" ", "").split(",") if x]
              for n, body in re.findall(r"int (control_\w+)\[\d*\] = \{([^}]*)\};", src)}
    sc = src.split("component_info control::score_components[")[1].split("};")[0]
    score = [{"tag_var": t, "handler": h, "count": int(c), "scores_var": None if s == "nullptr" else s}
             for t, h, c, s in re.findall(r"component_info\{(\w+), \{(\w+), (\d+), (\w+)\}\}", sc)]
    simple = re.findall(r"&(\w+),",
                        src.split("component_tag_base* control::simple_components[")[1].split("};")[0])
    return tags, arrays, score, simple


def map_reference(ref, upstream):
    """Name the reference exe's entries after control.cpp by position; return mismatches."""
    tags, arrays, score, simple = upstream
    problems = []
    if len(score) != ref.score_count or len(simple) != ref.simple_count:
        problems.append(f"array lengths: exe {ref.score_count}/{ref.simple_count}, "
                        f"upstream {len(score)}/{len(simple)}")
    notes = []
    names, handler_names = {}, {}
    for e, u in zip(ref.score_entries, score):
        cls, uname = tags[u["tag_var"]]
        e["upstream_tag_var"], e["upstream_class"] = u["tag_var"], cls
        e["upstream_handler"], e["upstream_scores_var"] = u["handler"], u["scores_var"]
        if uname != e["tag"]:
            problems.append(f"score[{e['index']}] tag {e['tag']} != upstream {uname}")
        if u["count"] != e["count"]:
            problems.append(f"score[{e['index']}] count {e['count']} != upstream {u['count']}")
        if u["scores_var"] and arrays[u["scores_var"]][:e["count"]] != e["scores"]:
            problems.append(f"score[{e['index']}] scores {e['scores']} != {u['scores_var']}")
        handler_names.setdefault(e["handler"], set()).add(u["handler"])
        names.setdefault(u["handler"], set()).add(e["handler"])
    for e, var in zip(ref.simple_entries, simple):
        e["upstream_tag_var"], e["upstream_class"] = var, tags[var][0]
        if tags[var][1] != e["tag"]:
            problems.append(f"simple[{e['index']}] tag {e['tag']} != upstream {tags[var][1]}")
    for a, n in handler_names.items():
        if len(n) != 1:  # the exe shares one function where upstream has several
            notes.append(f"handler {a:#x} serves upstream {' and '.join(sorted(n))} "
                         f"(one function in the exe, separate functions upstream)")
    for n, a in names.items():
        if len(a) != 1:
            problems.append(f"upstream {n} maps to several addresses {[hex(x) for x in a]}")
    ref.upstream_names = {a: "/".join(sorted(n)) for a, n in handler_names.items()}
    used = {u["tag_var"] for u in score} | set(simple)
    for var in sorted(set(tags) - used):
        notes.append(f"upstream tag {var} (\"{tags[var][1]}\") is in neither upstream array")
    return problems, notes


def camel(stem):
    return "".join(p[:1].upper() + p[1:] for p in stem.split("_") if p)


def suggest_name(img, entries, part_types):
    stems, classes = [], []
    for e in entries:
        stem = re.sub(r"\d+$", "", re.sub(r"^a_", "", e["tag"]))
        if stem not in stems:
            stems.append(stem)
        cls = PART_CLASS.get(part_types.get(e["tag"]), "")
        if cls.startswith("T") and cls not in classes:
            classes.append(cls)
    name = "".join(camel(s) for s in stems[:2])
    if len(classes) == 1:
        short = re.sub(r"[ (].*", "", classes[0][1:])
        if short.lower() not in name.lower():
            name += short
    return name + "Control"


def classify(img, ref):
    """Handler inventory of img; against ref when img is not the reference itself."""
    ref_by_fp = {}
    for a in ref.exe.functions:
        ref_by_fp.setdefault(fingerprint(ref.exe.tokens(a)), []).append(a)
    by_len = sorted(((a, ref.exe.tokens(a)) for a in ref.exe.functions), key=lambda kv: len(kv[1]))
    fp_cache = {}

    def fp(exe, a):
        key = (id(exe), a)
        if key not in fp_cache:
            fp_cache[key] = fingerprint(exe.tokens(a)) if a in exe.functions else f"raw:{a:#x}"
        return fp_cache[key]

    handlers = {}
    for e in img.score_entries:
        handlers.setdefault(e["handler"], []).append(e)
    out = []
    for addr, entries in sorted(handlers.items()):
        tags, calls = img.refs(addr)
        rec = {"addr": addr, "size": img.exe.functions[addr], "entries": [e["index"] for e in entries],
               "entry_tags": [e["tag"] for e in entries], "tags_referenced": tags,
               "calls": calls}
        if img is ref:
            rec["class"], rec["upstream_name"] = "reference", ref.upstream_names.get(addr)
            out.append(rec)
            continue
        toks = img.exe.tokens(addr)
        same = ref_by_fp.get(fingerprint(toks), [])
        if same:
            def deep(a):  # (tags equal, callees equivalent) against CADET function a
                rtags, rcalls = ref.refs(a)
                return rtags, rcalls, rtags == tags and len(rcalls) == len(calls) and all(
                    fp(img.exe, c) == fp(ref.exe, r) for c, r in zip(calls, rcalls))
            raw = img.raw_tokens(addr)
            ranked = sorted(same, key=lambda a: (not deep(a)[2], ref.raw_tokens(a) != raw,
                                                 a not in ref.upstream_names, a))
            best = ranked[0]
            rtags, rcalls, exact = deep(best)
            exact = exact and ref.raw_tokens(best) == raw  # constants too
            rec["also_matches"] = [a for a in ranked[1:] if deep(a)[2] and ref.raw_tokens(a) == raw]
            callees_ok = len(rcalls) == len(calls) and all(
                fp(img.exe, c) == fp(ref.exe, r) for c, r in zip(calls, rcalls))
            rec["match"] = best
            rec["match_ratio"] = 1.0
            rec["class"] = "identical" if exact else "same-code"
            if rtags != tags:
                rec["ref_tags_referenced"] = rtags
            if not callees_ok:
                rec["callee_differences"] = [[c, r] for c, r in zip(calls, rcalls)
                                             if fp(img.exe, c) != fp(ref.exe, r)]
        else:
            best, ratio = None, 0.0
            if len(toks) >= 4:
                for ra, rt in by_len:
                    if abs(len(rt) - len(toks)) > max(8, len(toks) // 4):
                        continue
                    sm = difflib.SequenceMatcher(None, toks, rt, autojunk=False)
                    if sm.quick_ratio() < SIMILAR:
                        continue
                    r = sm.ratio()
                    if r > ratio:
                        best, ratio = ra, r
            rec["class"] = "similar" if ratio >= SIMILAR else "new"
            if best is not None:
                rec["match"], rec["match_ratio"] = best, round(ratio, 3)
        m = rec.get("match")
        rec["upstream_name"] = ref.upstream_names.get(m) if m is not None else None
        out.append(rec)
    return out


def dump(img, part_types, handlers, problems):
    def pt(name):
        t = part_types.get(name)
        return {"part_type": t, "part_class": PART_CLASS.get(t) if t else None}

    tags = [dict(addr=a, name=n, listed=a in img.tags,
                 in_score=[e["index"] for e in img.score_entries if e["tag_addr"] == a],
                 in_simple=[e["index"] for e in img.simple_entries if e["tag_addr"] == a], **pt(n))
            for a, n in sorted(img.all_tags.items())]
    return {"exe": os.path.basename(img.path), "image_base": img.base,
            "make_links": img.make_links, "make_component_link": img.make_component_link,
            "table_global": img.table_global,
            "score_components": {"addr": img.score_addr, "count": img.score_count, "entry_size": 16},
            "simple_components": {"addr": img.simple_addr, "count": img.simple_count},
            "tags": tags, "score_entries": [dict(e, **pt(e["tag"])) for e in img.score_entries],
            "simple_entries": [dict(e, **pt(e["tag"])) for e in img.simple_entries],
            "handlers": handlers, "verification_problems": problems}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ref", required=True, help="CADET.EXE (mapped onto control.cpp)")
    ap.add_argument("--upstream", required=True, help="SpaceCadetPinball/control.cpp")
    ap.add_argument("--export-dir", required=True, help="Ghidra export folder with <EXE>.tsv")
    ap.add_argument("--dat-dir", help="Full Tilt folder (CADET/, DRAGON/, PIRATE/) for part types")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--annotate", action="store_true",
                    help="also write DIR/control_handlers_<stem>.c (export bodies, tags resolved)")
    ap.add_argument("--names", action="append", default=[], metavar="STEM=FILE",
                    help='manual handler names for an exe: JSON {"0x<addr>": [name, note]}')
    ap.add_argument("--also-vs", metavar="EXE",
                    help="second reference: classify the other targets' handlers against it too")
    ap.add_argument("targets", nargs="*")
    a = ap.parse_args()
    names = dict(n.split("=", 1) for n in a.names)
    second = Image(a.also_vs, a.export_dir) if a.also_vs else None
    if second is not None:  # classify() reports ref.upstream_names: give it the --names names
        manual2 = json.load(open(names[second.stem])) if second.stem in names else {}
        second.upstream_names = {int(k, 16): (v[0] if isinstance(v, list) else v)
                                 for k, v in manual2.items() if not k.startswith("_")}
    ref = Image(a.ref, a.export_dir)
    problems, notes = map_reference(ref, parse_upstream(a.upstream))
    # classifier check: the reference against a second load of itself must be all identical
    self_check = [r for r in classify(Image(a.ref, a.export_dir), ref)
                  if r["class"] != "identical" or r["match"] != r["addr"]]
    if self_check:
        problems.append(f"classifier self-check: {len(self_check)} reference handlers not matched "
                        f"to themselves: {[hex(r['addr']) for r in self_check]}")
    print(f"classifier self-check ({ref.stem} vs itself): {len(self_check)} mismatches")
    results = {}
    for img in [ref] + [Image(p, a.export_dir) for p in a.targets]:
        types = {}
        if a.dat_dir:
            rel = TABLES.get(img.stem)
            if rel is None:
                raise SystemExit(f"no .DAT known for {img.stem} (dat_parts.TABLES)")
            types = dat_part_types(os.path.join(a.dat_dir, rel))
        if img is ref and types:
            for e in ref.score_entries + ref.simple_entries:
                got = re.sub(r" (L|R|\(.*\))$", "", PART_CLASS.get(types.get(e["tag"]), "?"))
                if got != e["upstream_class"]:
                    problems.append(f"{e['tag']}: .DAT part class {got} != upstream "
                                    f"component_tag<{e['upstream_class']}>")
        handlers = classify(img, ref)
        if second is not None and img is not ref and img.stem != second.stem:
            for r, r2 in zip(handlers, classify(img, second)):
                assert r["addr"] == r2["addr"]
                r[f"vs_{second.stem}"] = {"class": r2["class"], "match": r2.get("match"),
                                          "match_ratio": r2.get("match_ratio"),
                                          "name": r2.get("upstream_name")}
        for r in handlers:
            r["suggested_name"] = suggest_name(img, [img.score_entries[i] for i in r["entries"]], types)
            r["proposed_name"], r["proposed_note"] = r["suggested_name"], "auto (tag names)"
        if img.stem in names:
            manual = {int(k, 16): v for k, v in json.load(open(names[img.stem])).items()
                      if not k.startswith("_")}
            unknown = set(manual) - {r["addr"] for r in handlers}
            if unknown:
                raise SystemExit(f"{names[img.stem]}: not handlers of {img.stem}: "
                                 f"{sorted(hex(x) for x in unknown)}")
            for r in handlers:
                if r["addr"] in manual:
                    r["proposed_name"], r["proposed_note"] = manual[r["addr"]]
        d = dump(img, types, handlers, problems if img is ref else [])
        if img is ref:
            d["verification_notes"] = notes
        results[img.stem] = (img, d)
        os.makedirs(a.out_dir, exist_ok=True)
        if a.annotate:
            annotate(img, os.path.join(a.export_dir, os.path.basename(img.path) + ".c"), handlers,
                     os.path.join(a.out_dir, f"control_handlers_{img.stem}.c"))
        out = os.path.join(a.out_dir, f"control_tables_{img.stem}.json")
        with open(out, "w") as f:
            json.dump(d, f, indent=1)
        print(f"{img.stem}: score {img.score_count} @ {img.score_addr:#x}, simple {img.simple_count} "
              f"@ {img.simple_addr:#x}, tags {len(img.all_tags)}, handlers {len(handlers)} -> {out}")
    print(f"reference verification: {len(problems)} problems, {len(notes)} notes")
    for line in problems + notes:
        print("  " + line)
    md = os.path.join(a.out_dir, "control_tables.md")
    with open(md, "w") as f:
        f.write(markdown(results, ref.stem))
    print(f"-> {md}")


if __name__ == "__main__":
    main()
