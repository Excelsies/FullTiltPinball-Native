#!/usr/bin/env python3
"""Generate the Full Tilt control tables of one table (rerunnable; the output is checked in as rules/<t>_tables.cpp).

    python re/rules_tables_gen.py <dragon|pirates> <control_tables_<t>.json> --out-cpp <rules/<t>_tables.cpp>
        --out-externs <block for rules/<t>_rules.h> [--names <t>_handler_names.json]

Input: the control table dump of control_tables.py (part names, part classes, score arrays) and the control-function names
(`--names`, default `<t>_handler_names.json` beside the dump). Output, deterministic (no dates, no host paths):
  --out-cpp      tags (component_tag<T>), the `T*&` reference of every tag, score arrays, score_components,
                 simple_components; everything in `namespace <table>`;
  --out-externs  the matching declarations (tags, references, the two arrays, every table-specific control function)
                 to paste into the table's rules header. The control functions of rules/ft_common.h
                 (BallSaveLightControl, LaunchOneWayControl, RebounderControl1/2, NullControl) are referenced as ft::X.
Emits part names and integers only. Any inconsistency (unknown part class, unnamed handler, bad identifier,
entry count differing from the dump header) aborts with exit code 2; nothing is guessed.
"""
import argparse
import json
import os
import re
import sys

# control_tables `part_class` -> engine class (a copy of ft_msgmap.TAG_CLASS plus the identity for plain classes).
TAG_CLASS = {"TFlipper L": "TFlipper", "TFlipper R": "TFlipper", "TKickout (no ball)": "TKickout",
             "TPlunger(flag 1)": "TPlunger", "type1035": "TRampExit", "type1036": "TAccelerator",
             "type1037": "TBall", "type1038": "TLightSweeps", "type1042": "TMagnet",
             "type1043": "TDiverter"}
PLAIN_CLASSES = {"TLight", "TSound", "TSoloTarget", "TLightGroup", "TSink", "TTripwire", "TBlocker", "TRollover",
                 "TBumper", "TLightRollover", "TWall", "TRamp", "TOneway", "TFlipper", "TKickout", "TKickback",
                 "TTextBox", "TDrain", "TPlunger", "THole", "TTimer", "TPopupTarget"}
# Handlers implemented once in rules/ft_common.{h,cpp}, by the name the names JSON gives them.
COMMON = {"BallSaveLightControl", "LaunchOneWayControl", "RebounderControl1", "RebounderControl2"}
NULL_CONTROL_NAMES = {"NullLightControl", "NullControl"}
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class GenError(Exception):
    pass


def engine_class(part_class):
    cls = TAG_CLASS.get(part_class, part_class)
    if cls not in PLAIN_CLASSES and cls not in set(TAG_CLASS.values()):
        raise GenError("unknown part class %r" % part_class)
    return cls


def ident(name):
    if not IDENT.match(name):
        raise GenError("not a C identifier: %r" % name)
    return name


def load(table, path, names_path):
    with open(path) as f:
        data = json.load(f)
    with open(names_path) as f:
        names = json.load(f)
    for key in ("tags", "score_entries", "simple_entries", "score_components", "simple_components"):
        if key not in data:
            raise GenError("%s: key %r missing" % (path, key))
    if len(data["score_entries"]) != data["score_components"]["count"]:
        raise GenError("score entry count differs from the dump header")
    if len(data["simple_entries"]) != data["simple_components"]["count"]:
        raise GenError("simple entry count differs from the dump header")
    return data, names


def build_model(table, data, names):
    """Unique C++ names per tag (a repeated tag name is a separate tag: suffix _2, _3, ...)."""
    seen = {}
    tag_by_addr = {}
    tags = []
    for t in data["tags"]:
        base = ident(t["name"])
        seen[base] = seen.get(base, 0) + 1
        var = base if seen[base] == 1 else "%s_%d" % (base, seen[base])
        info = {"name": t["name"], "var": var, "cls": engine_class(t["part_class"]), "addr": t["addr"]}
        tags.append(info)
        tag_by_addr[t["addr"]] = info
    used_vars = [t["var"] for t in tags]
    if len(set(used_vars)) != len(used_vars):
        raise GenError("generated tag names collide")

    def lookup(entry):
        info = tag_by_addr.get(entry["tag_addr"])
        if info is None or info["name"] != entry["tag"]:
            raise GenError("entry tag %r has no tag record" % entry["tag"])
        return info

    handler_names = {}
    for h in data["handlers"]:
        key = hex(h["addr"])
        if key not in names:
            raise GenError("handler %s has no name in the names JSON" % key)
        handler_names[h["addr"]] = ident(names[key][0])
    arrays = {}   # (addr, scores) -> variable
    score = []
    for e in data["score_entries"]:
        info = lookup(e)
        if e["handler"] not in handler_names:
            raise GenError("entry %s: handler %#x unknown" % (e["tag"], e["handler"]))
        fn = handler_names[e["handler"]]
        if e["count"] != len(e["scores"]) or (e["count"] == 0) != (e["scores_addr"] == 0):
            raise GenError("entry %s: score count / address inconsistent" % e["tag"])
        arr = None
        if e["count"]:
            key = (e["scores_addr"], tuple(e["scores"]))
            if key not in arrays:
                arrays[key] = "%s_scores_%d" % (table, len(arrays))
            arr = arrays[key]
        score.append({"tag": info, "fn": fn, "count": e["count"], "arr": arr, "index": e["index"]})
    simple = [lookup(e) for e in data["simple_entries"]]
    linked = [s["tag"]["var"] for s in score] + [t["var"] for t in simple]
    if sorted(linked) != sorted(used_vars):
        raise GenError("every tag must be in exactly one of the two arrays")
    return tags, score, simple, arrays


def fn_ref(fn):
    if fn in COMMON:
        return "ft::" + fn
    if fn in NULL_CONTROL_NAMES:
        return "ft::NullControl"
    return fn


def emit(table, data, names):
    tags, score, simple, arrays = build_model(table, data, names)
    classes = sorted({t["cls"] for t in tags})
    fns = sorted({s["fn"] for s in score if s["fn"] not in COMMON and s["fn"] not in NULL_CONTROL_NAMES})
    ns = table

    cpp = ["// Generated by re/rules_tables_gen.py from the %s control table dump. Do not edit; rerun the generator." % table,
           '#include "pch.h"', '#include "control.h"', '#include "rules/ft_common.h"']
    cpp += ['#include "%s.h"' % c for c in classes]
    cpp += ['#include "rules/%s_rules.h"' % table, "", "namespace %s {" % ns, "", "// tags: %d" % len(tags)]
    for t in tags:
        cpp.append('component_tag<%s> %s_tag = {"%s"};' % (t["cls"], t["var"], t["name"]))
    cpp += ["", "// references to the linked components"]
    for t in tags:
        cpp.append("%s*& %s = %s_tag.Component;" % (t["cls"], t["var"], t["var"]))
    cpp += ["", "// score arrays (shared by the entries that use the same values)"]
    for (addr, vals), var in sorted(arrays.items(), key=lambda kv: kv[1]):
        cpp.append("static const int %s[%d] = {%s};" % (var, len(vals), ", ".join(str(v) for v in vals)))
    cpp += ["", "component_info score_components[%d] =" % len(score), "{"]
    for s in score:
        cpp.append("\tcomponent_info{%s_tag, {%s, %d, %s}}," % (s["tag"]["var"], fn_ref(s["fn"]), s["count"],
                                                              s["arr"] or "nullptr"))
    cpp += ["};", "", "component_tag_base* simple_components[%d] =" % len(simple), "{"]
    for t in simple:
        cpp.append("\t&%s_tag," % t["var"])
    cpp += ["};", "", "}", ""]

    ext = ["// Generated by re/rules_tables_gen.py (%s): paste into rules/%s_rules.h; needs control.h and ft_common.h." % (table, table)]
    ext += ["class %s;" % c for c in classes]
    ext += ["namespace %s {" % ns, "// control functions of the table (the shared ones are ft::* in ft_common.h)"]
    ext += ["void %s(MessageCode, TPinballComponent*);" % f for f in fns]
    ext += ["extern component_info score_components[%d];" % len(score),
            "extern component_tag_base* simple_components[%d];" % len(simple), "// tags"]
    ext += ["extern component_tag<%s> %s_tag;" % (t["cls"], t["var"]) for t in tags]
    ext += ["// references to the linked components"]
    ext += ["extern %s*& %s;" % (t["cls"], t["var"]) for t in tags]
    ext += ["}", ""]
    return "\n".join(cpp), "\n".join(ext)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("table", choices=("dragon", "pirates"))
    ap.add_argument("tables_json")
    ap.add_argument("--out-cpp", required=True)
    ap.add_argument("--out-externs", required=True)
    ap.add_argument("--names")
    a = ap.parse_args(argv)
    names_path = a.names or os.path.join(os.path.dirname(os.path.abspath(a.tables_json)), "%s_handler_names.json" % a.table)
    try:
        data, names = load(a.table, a.tables_json, names_path)
        cpp, ext = emit(a.table, data, names)
    except (GenError, OSError, ValueError, KeyError) as e:
        print("rules_tables_gen: %s" % e, file=sys.stderr)
        return 2
    for path, text in ((a.out_cpp, cpp), (a.out_externs, ext)):
        with open(path, "w") as f:
            f.write(text)
    print("%s: %d tags, %d score entries, %d simple entries" % (a.table, len(data["tags"]), len(data["score_entries"]),
                                                              len(data["simple_entries"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
