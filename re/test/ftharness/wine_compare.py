"""Wine-vs-native comparison tools.

  python re/test/ftharness/wine_compare.py compare-wine <native dumps.jsonl> <wine timeline.jsonl>
         [--keys k,...] [--order-only k,...] [--types <watch.json>] [--anchor <key>]
         [--tol-ms N] [--period-ms 50] [--out report.json]
  python re/test/ftharness/wine_compare.py rest-state <native dumps.jsonl> <wine rest.jsonl>
         [--label rest] [--types <rest.json>] [--exclude <file>] [--out report.json]
  python re/test/ftharness/wine_compare.py perturb <timeline.jsonl> <out>
         (--drop KEY N | --delay KEY N MS | --set KEY VALUE)

Also reached as ``cli(argv)`` from hookrun.py. Stdlib only. Exit: 0 = at least one key assessed
and none failed, 1 = a key FAILed, 2 = input / usage error (message on stderr).

Two file formats are read on either side. *Native* lines come from the hook's dumps.jsonl and
carry ``frame`` (``tick`` lines for compare-wine, ``<label>_<n>`` lines for rest-state); their
float32 fields hold real numbers. *Wine* lines (probe_run.py timeline.jsonl / rest.jsonl) have no
``frame``; their f32 fields are raw 32-bit patterns because ftpeek reads 4 bytes. Typing
(``--types``: a watch / rest file whose ``values`` carry ``type`` i32 / u8 / f32 / armed) maps both
into one comparable integer.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

TREE_ROOT = Path(__file__).resolve().parents[3]


def resolve_input(path: str) -> Path:
    """A relative input path is looked up in the current directory first, then in the tree root
    (the directory holding re/); an absolute path is used as is. No match -> the caller's read error."""
    p = Path(path)
    if p.is_absolute() or p.exists():
        return p
    q = TREE_ROOT / p
    return q if q.exists() else p


NON_KEYS = {"t", "ms", "frame", "gt", "label"}
NOT_ASSESSED, PASS, FAIL = "NOT ASSESSED", "PASS", "FAIL"


class InputError(Exception):
    """Bad usage or unreadable input (exit 2)."""


# ---- reading and typing -------------------------------------------------------------------
def read_lines(path: str) -> list[dict]:
    try:
        lines = resolve_input(path).read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise InputError(f"cannot read {path}: {exc}") from exc
    rows = []
    for n, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except ValueError as exc:
            raise InputError(f"{path}:{n}: not JSON: {exc}") from exc
        if not isinstance(row, dict):
            raise InputError(f"{path}:{n}: not a JSON object")
        rows.append(row)
    return rows


def read_types(path: str | None) -> dict[str, str]:
    if not path:
        return {}
    try:
        data = json.loads(resolve_input(path).read_text(encoding="utf-8"))
        return {v["name"]: v["type"] for v in data["values"] if "type" in v}
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise InputError(f"cannot read types from {path}: {exc}") from exc


def f32_bits(value) -> int:
    return struct.unpack("<I", struct.pack("<f", float(value)))[0]


def normalise(value, typ: str | None, native: bool):
    """One comparable value. None (unreadable / absent) stays None."""
    if value is None or isinstance(value, bool) and typ is None:
        return value
    if typ == "f32":
        return f32_bits(value) if native else int(value) & 0xFFFFFFFF
    if typ == "u8":
        return int(value) & 0xFF
    if typ == "armed":
        return 1 if int(value) != 0 else 0
    if typ in (None, "i32"):
        return value
    raise InputError(f"unknown type {typ!r} (i32, u8, f32, armed)")


def is_native(rows: list[dict]) -> bool:
    return any("frame" in r for r in rows)


def typed_rows(rows: list[dict], types: dict[str, str]) -> list[dict]:
    native = is_native(rows)
    out = []
    for r in rows:
        out.append({k: (v if k in NON_KEYS or k.startswith("n_") and native else
                        normalise(v, types.get(k), native)) for k, v in r.items()})
    return out


# ---- compare-wine -------------------------------------------------------------------------
def sample_rows(path: str, types: dict[str, str]) -> list[dict]:
    rows = read_lines(path)
    if is_native(rows):
        rows = [r for r in rows if r.get("label") == "tick"]
    rows = typed_rows(rows, types)
    if len(rows) < 2:
        raise InputError(f"{path}: fewer than two samples ({len(rows)}); native files need "
                         f"'init dump_period 50' tick lines")
    for r in rows:
        if "t" not in r:
            raise InputError(f"{path}: sample without 't'")
    return rows


def series(rows: list[dict], key: str):
    """(values with the initial value first, transition times). Transition = sample whose value
    differs from the previous sample's."""
    vals = [rows[0][key]]
    times = []
    for r in rows[1:]:
        if r[key] != vals[-1]:
            vals.append(r[key])
            times.append(float(r["t"]))
    return vals, times


def check_key(key, nat, wine, order_only, tol, anchors):
    nv, nt = nat
    wv, wt = wine
    if nv != wv:
        i = next((i for i in range(min(len(nv), len(wv))) if nv[i] != wv[i]), min(len(nv), len(wv)))
        return FAIL, f"order: value sequences differ at index {i} (native {nv[i:i + 3]}, wine {wv[i:i + 3]})"
    if not nt:
        return PASS, "constant"
    if order_only:
        return PASS, f"order only ({len(nt)} transitions)"
    worst = 0.0
    for i in range(1, len(nt)):
        d = abs((wt[i] - wt[i - 1]) - (nt[i] - nt[i - 1])) * 1000
        worst = max(worst, d)
        if d > tol:
            return FAIL, (f"timing: transition {i + 1} follows transition {i} by "
                          f"{(nt[i] - nt[i - 1]) * 1000:.0f} ms native, {(wt[i] - wt[i - 1]) * 1000:.0f} ms wine "
                          f"(delta {d:.0f} ms > {tol:.0f})")
    if anchors is not None:
        (an, aw) = anchors
        d = abs((wt[0] - aw) - (nt[0] - an)) * 1000
        worst = max(worst, d)
        if d > tol:
            return FAIL, (f"timing: first transition is {(nt[0] - an) * 1000:.0f} ms after the anchor native, "
                          f"{(wt[0] - aw) * 1000:.0f} ms wine (delta {d:.0f} ms > {tol:.0f})")
    return PASS, f"{len(nt)} transitions, worst timing delta {worst:.0f} ms"


def cross_order(timed: dict, tol_s: float) -> dict[str, str]:
    """Two events more than tol apart on both sides must keep their order. Returns key -> reason."""
    events = [(k, i, n[i], w[i]) for k, (n, w) in timed.items() for i in range(len(n))]
    bad: dict[str, str] = {}
    for a in range(len(events)):
        ka, _, na, wa = events[a]
        for b in range(a + 1, len(events)):
            kb, _, nb, wb = events[b]
            if ka == kb:
                continue
            if abs(na - nb) > tol_s and abs(wa - wb) > tol_s and (na < nb) != (wa < wb):
                bad.setdefault(ka, f"cross-key order against {kb}")
                bad.setdefault(kb, f"cross-key order against {ka}")
    return bad


def compare_wine(args) -> int:
    types = read_types(args.types)
    nrows = sample_rows(args.native, types)
    wrows = sample_rows(args.wine, types)
    tol = args.tol_ms if args.tol_ms is not None else 2 * args.period_ms + 50
    nkeys = {k for r in nrows for k in r} - NON_KEYS
    wkeys = {k for r in wrows for k in r} - NON_KEYS
    nkeys = {k for k in nkeys if not k.startswith("n_")} if is_native(read_lines(args.native)) else nkeys
    wkeys = {k for k in wkeys if not k.startswith("n_")} if is_native(read_lines(args.wine)) else wkeys
    wanted = set(args.keys.split(",")) if args.keys else None
    verdicts: dict[str, tuple[str, str]] = {}
    both = sorted(nkeys & wkeys)
    for k in sorted(nkeys ^ wkeys):
        if wanted is None or k in wanted:
            verdicts[k] = (NOT_ASSESSED, f"only in the {'native' if k in nkeys else 'wine'} file")
    for k in (wanted or set()) - nkeys - wkeys:
        verdicts[k] = (NOT_ASSESSED, "in neither file")
    both = [k for k in both if wanted is None or k in wanted]
    order_only = set(args.order_only.split(",")) if args.order_only else set()
    ser = {}
    for k in both:
        try:
            ser[k] = (series(nrows, k), series(wrows, k))
        except KeyError:
            verdicts[k] = (NOT_ASSESSED, "missing in some samples")
    anchor = args.anchor or "player_count"
    anchors = None
    if anchor in ser and ser[anchor][0][1] and ser[anchor][1][1]:
        anchors = (ser[anchor][0][1][0], ser[anchor][1][1][0])
    elif args.anchor:
        raise InputError(f"anchor {args.anchor!r} has no transition on both sides")
    for k, (n, w) in ser.items():
        verdicts[k] = check_key(k, n, w, k in order_only, tol, anchors)
    timed = {k: (ser[k][0][1], ser[k][1][1]) for k in ser
             if verdicts[k][0] == PASS and k not in order_only and ser[k][0][1]
             and len(ser[k][0][1]) == len(ser[k][1][1])}
    for k, why in cross_order(timed, tol / 1000).items():
        verdicts[k] = (FAIL, why)
    return report(verdicts, args.out, {"tol_ms": tol, "anchor": anchor if anchors else None,
                                       "native_samples": len(nrows), "wine_samples": len(wrows)})


def report(verdicts: dict, out: str | None, meta: dict) -> int:
    for k in sorted(verdicts):
        v, why = verdicts[k]
        print(f"{v} {k}: {why}" if why else f"{v} {k}")
    counts = {v: sum(1 for x in verdicts.values() if x[0] == v) for v in (PASS, FAIL, NOT_ASSESSED)}
    print(f"summary: {counts[PASS]} PASS, {counts[FAIL]} FAIL, {counts[NOT_ASSESSED]} NOT ASSESSED")
    if out:
        Path(out).write_text(json.dumps({**meta, "summary": counts,
                                         "keys": {k: {"verdict": v[0], "reason": v[1]}
                                                  for k, v in sorted(verdicts.items())}},
                                        indent=1) + "\n", encoding="utf-8")
    if counts[FAIL]:
        return 1
    if not counts[PASS]:
        print("no key was assessed", file=sys.stderr)
        return 2
    return 0


# ---- rest-state ---------------------------------------------------------------------------
def rest_samples(path: str, label: str, types: dict[str, str]) -> dict[str, list]:
    rows = read_lines(path)
    native = is_native(rows)
    by = {r.get("label"): r for r in rows}
    want = [f"{label}_{n}" for n in (1, 2, 3)]
    missing = [w for w in want if w not in by]
    if missing:
        raise InputError(f"{path}: no sample labelled {', '.join(missing)}")
    got = [typed_rows([by[w]], types)[0] for w in want]
    keys = {k for r in got for k in r} - NON_KEYS
    if native:
        keys = {k for k in keys if not k.startswith("n_")}
    return {k: [r.get(k) for r in got] for k in keys}


def read_exclude(path: str | None) -> dict[str, str]:
    if not path:
        return {}
    try:
        lines = resolve_input(path).read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise InputError(f"cannot read {path}: {exc}") from exc
    out = {}
    for line in lines:
        if line.strip() and not line.lstrip().startswith("#"):
            part, _, why = line.strip().partition(" ")
            out[part] = why.strip() or "excluded"
    return out


def rest_state(args) -> int:
    types = read_types(args.types)
    nat = rest_samples(args.native, args.label, types)
    wine = rest_samples(args.wine, args.label, types)
    exclude = read_exclude(args.exclude)
    verdicts = {}
    for k in sorted(set(nat) | set(wine)):
        if k not in wine:
            verdicts[k] = (NOT_ASSESSED, "not in the wine file")
            continue
        w = wine[k]
        if any(v is None for v in w):
            verdicts[k] = (FAIL, "wine read returned '?' (bad spec)")
        elif len(set(w)) > 1:
            verdicts[k] = (NOT_ASSESSED, f"animated on the wine side {w}")
        elif k not in nat or any(v is None for v in nat[k]):
            verdicts[k] = (NOT_ASSESSED, "native field not provided yet (null / absent)")
        elif len(set(nat[k])) > 1:
            verdicts[k] = (NOT_ASSESSED, f"animated on the native side {nat[k]}")
        elif nat[k][0] == w[0]:
            verdicts[k] = (PASS, "")
        else:
            verdicts[k] = (FAIL, f"native {nat[k][0]} != wine {w[0]}")
        if verdicts[k][0] == FAIL and "(bad spec)" not in verdicts[k][1] and k.split(".")[0] in exclude:
            verdicts[k] = (NOT_ASSESSED, f"excluded: {exclude[k.split('.')[0]]} ({verdicts[k][1]})")
    animated = sum(1 for v in verdicts.values() if "animated" in v[1])
    print(f"animated keys: {animated}")
    return report(verdicts, args.out, {"label": args.label, "animated": animated})


# ---- perturb ------------------------------------------------------------------------------
def perturb(args) -> int:
    rows = read_lines(args.timeline)
    if args.set:
        key, value = args.set
        for r in rows:
            if key not in r:
                raise InputError(f"key {key!r} not in the file")
            r[key] = json.loads(value)
    else:
        key = (args.drop or args.delay)[0]
        n = int((args.drop or args.delay)[1])
        if any(key not in r for r in rows):
            raise InputError(f"key {key!r} not in every line")
        idx = [i for i in range(1, len(rows)) if rows[i][key] != rows[i - 1][key]]
        if not 1 <= n <= len(idx):
            raise InputError(f"{key} has {len(idx)} transitions, not {n}")
        start = idx[n - 1]
        end = idx[n] if n < len(idx) else len(rows)
        before, after = rows[start - 1][key], rows[start][key]
        if args.drop:
            for r in rows[start:end]:
                r[key] = before
        else:
            ms = float(args.delay[2])
            t_new = float(rows[start]["t"]) + ms / 1000
            held = [r for r in rows[start:end] if float(r["t"]) < t_new]
            for r in held:
                r[key] = before
            pos = start + len(held)
            if pos < end and float(rows[pos]["t"]) == round(t_new, 3):
                rows[pos][key] = after
            else:                               # no sample at the new time: add one
                new = dict(rows[pos - 1])
                new.update({"t": round(t_new, 3), key: after})
                if "ms" in new:
                    new["ms"] = int(new["ms"] + ms)
                rows.insert(pos, new)
    Path(args.out).write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
    return 0


# ---- cli ----------------------------------------------------------------------------------
def build_parser():
    import argparse
    p = argparse.ArgumentParser(prog="wine_compare", description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compare-wine")
    c.add_argument("native"); c.add_argument("wine")
    c.add_argument("--keys"); c.add_argument("--order-only", dest="order_only")
    c.add_argument("--types"); c.add_argument("--anchor")
    c.add_argument("--tol-ms", dest="tol_ms", type=float)
    c.add_argument("--period-ms", dest="period_ms", type=float, default=50)
    c.add_argument("--out")
    r = sub.add_parser("rest-state")
    r.add_argument("native"); r.add_argument("wine")
    r.add_argument("--label", default="rest"); r.add_argument("--types")
    r.add_argument("--exclude"); r.add_argument("--out")
    q = sub.add_parser("perturb")
    q.add_argument("timeline"); q.add_argument("out")
    g = q.add_mutually_exclusive_group(required=True)
    g.add_argument("--drop", nargs=2, metavar=("KEY", "N"))
    g.add_argument("--delay", nargs=3, metavar=("KEY", "N", "MS"))
    g.add_argument("--set", nargs=2, metavar=("KEY", "VALUE"))
    return p


def split_joined_options(argv: list[str]) -> list[str]:
    """One token "--opt value" (an unsplit shell variable, a quoted pair) -> "--opt", "value", split at the
    first whitespace; "--opt=value" and a plain "--opt", "value" pass unchanged, so a value holding spaces
    stays whole in the equals form."""
    out = []
    for tok in argv:
        head = tok.split("=", 1)[0]
        if tok.startswith("--") and len(head) > 2 and any(c.isspace() for c in head):
            opt, _, val = tok.partition(" ") if " " in head else tok.partition("\t")
            out += [opt, val.strip()]
        else:
            out.append(tok)
    return out


def cli(argv: list[str]) -> int:
    try:
        args = build_parser().parse_args(split_joined_options(argv))
    except SystemExit as exc:                    # argparse usage error -> 2, help -> 0
        return int(exc.code or 0)
    try:
        return {"compare-wine": compare_wine, "rest-state": rest_state, "perturb": perturb}[args.cmd](args)
    except InputError as exc:
        print(f"wine_compare: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
