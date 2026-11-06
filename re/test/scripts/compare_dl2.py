"""Start-up Wine-vs-native comparison: the Wine `peek` rows (probe_run.py peeks.jsonl, t = seconds on the window anchor)
against the native dump row nearest in time (dumps.jsonl of `init startup_wait 1`, t = seconds, one row per 5 frames).

  python re/test/scripts/compare_dl2.py <native dumps.jsonl> <wine peeks.jsonl> [--keys k1,k2,...] [--name dragon]
        [--max-dt 0.03]

One line per Wine row: label, Wine values, native values at the nearest t, SAME / DIFF. Last line ALL SAME (exit 0) or
N DIFF (exit 1). A missing key, an empty input, or a nearest native row further than --max-dt away is an error (exit 2).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

KEYS = "jackpot,super_jackpot,player_count,ball_count,mode"


def rows(path: str) -> list[dict]:
    out = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    if not out:
        raise SystemExit(f"error: {path} has no rows")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("native")
    ap.add_argument("wine")
    ap.add_argument("--keys", default=KEYS)
    ap.add_argument("--name", default="")
    ap.add_argument("--max-dt", type=float, default=0.03)
    args = ap.parse_args()
    keys = [k for k in args.keys.split(",") if k]
    native, wine = rows(args.native), rows(args.wine)
    diffs = 0
    for w in wine:
        n = min(native, key=lambda r: abs(r["t"] - w["t"]))
        if abs(n["t"] - w["t"]) > args.max_dt:
            print(f"error: no native row within {args.max_dt} s of Wine t={w['t']} (nearest t={n['t']})", file=sys.stderr)
            return 2
        for k in keys:
            if k not in w or k not in n:
                print(f"error: key {k} missing in row {w.get('label')}", file=sys.stderr)
                return 2
        wv, nv = [w[k] for k in keys], [n[k] for k in keys]
        same = wv == nv
        diffs += not same
        print(f"{args.name} {w.get('label')} wine {wv} native(t={n['t']:.2f}) {nv} {'SAME' if same else 'DIFF'}".strip())
    print("ALL SAME" if not diffs else f"{diffs} DIFF")
    return 1 if diffs else 0


if __name__ == "__main__":
    raise SystemExit(main())
