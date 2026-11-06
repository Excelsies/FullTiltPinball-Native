"""Runner for the native test hook: resolve a .uh script, run the hook build on a
private Xvfb display, check the `expect` lines, compare runs byte for byte, convert harness JSON scripts.

  python re/test/hookrun.py run --table cadet --bin <binary> --display 105 --out <dir> [--timeout S] script.uh
  python re/test/hookrun.py check <out>
  python re/test/hookrun.py compare <outA> <outB>
  python re/test/hookrun.py from-json <script.json> --fixed-dt 16.667 [--table t]
  python re/test/hookrun.py compare-wine ... | rest-state ...     (dispatch to ftharness/wine_compare.py)

Script format and output layout: see hook_scripts/README.md. Stdlib only; no game text lives here.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ftharness import session as sm  # noqa: E402

TEST_DIR = Path(__file__).resolve().parent
REPO = TEST_DIR.parent.parent
MSGMAP = REPO / "re" / "work" / "spec" / "msgmap.json"
COMPARED = ["dumps.jsonl", "timeline.jsonl", "sounds.jsonl", "text.jsonl"]
KEYMAP = {"z": "left_flipper", "slash": "right_flipper", "space": "plunger", "x": "bump_left",
          "period": "bump_right", "Up": "bump_bottom", "F2": "new_game", "F3": "pause"}
FRAME_S = 1 / 120.0        # real seconds per game frame, independent of fixed_dt (README)
FRAME0_MIN_S = 0.10       # earliest frame 0 after the spawn (README)
NUDGE_CLEAR_S = 0.4        # the resize (two steps of 0.1 s plus xdotool) needs this gap before the next shot
LOGICAL = set(KEYMAP.values()) | {"demo"}


class RunError(RuntimeError):
    """Resolution or run problem: exit code 2."""


# ---- script resolution ---------------------------------------------------------------------
def find_enum(bin_path: Path) -> dict[str, int]:
    """MessageCode members parsed from SpaceCadetPinball/TPinballComponent.h of the tree that built bin."""
    root = next((p for p in [bin_path.resolve(), *bin_path.resolve().parents]
                 if (p / "SpaceCadetPinball" / "TPinballComponent.h").exists()), REPO)
    text = (root / "SpaceCadetPinball" / "TPinballComponent.h").read_text(encoding="utf-8")
    body = text[text.index("enum class MessageCode"):]
    body = body[body.index("{") + 1:body.index("};")]
    out: dict[str, int] = {}
    for m in re.finditer(r"^\s*([A-Za-z_]\w*)\s*=\s*(-?(?:0x[0-9a-fA-F]+|\d+))\s*,", body, re.M):
        out[m.group(1)] = int(m.group(2), 0)
    if not out:
        raise RunError(f"no MessageCode members parsed under {root}")
    return out


class Resolver:
    def __init__(self, enum: dict[str, int], msgmap: dict | None):
        self.enum, self.msgmap = enum, msgmap

    def _mapped(self, cls: str, code: str) -> int:
        if self.msgmap is None:
            raise RunError(f"{MSGMAP} missing: cannot resolve ft:{code}")
        info = self.msgmap["classes"].get(cls)
        if info is None:
            raise RunError(f"msgmap has no class {cls!r}")
        key = hex(int(code, 16))
        entry = info["codes"].get(key)
        if entry is None:
            raise RunError(f"FT code {key} is unmapped for {cls}")
        name = entry["upstream"].split(":")[-1]
        return self.named(name)

    def named(self, name: str) -> int:
        if name not in self.enum:
            raise RunError(f"unknown MessageCode {name!r}")
        return self.enum[name]

    def code(self, tok: str, cmd: str, target: str) -> int:
        if tok.startswith("num:"):
            return int(tok[4:], 0)
        if tok.startswith("ft:"):
            spec = tok[3:]
            if cmd == "event":
                return self._mapped("table_control_handler" if target == "table" else "control", spec)
            if "@" not in spec:
                if target == "table":
                    return self._mapped("TPinballTable", spec)
                raise RunError(f"msg {target}: write ft:<hex>@<Class> (the receiving class)")
            hexv, cls = spec.split("@", 1)
            return self._mapped(cls, hexv)
        try:
            return int(tok, 0)
        except ValueError:
            return self.named(tok)


def watch_names(path: str) -> list[str]:
    """`@<file>` in a script: a relative path is looked up in the current directory, then in the tree root
    (spec 2.7 writes `@re/test/scripts/rest_<t>.json`), not next to the script."""
    from ftharness.wine_compare import resolve_input
    data = json.loads(resolve_input(path).read_text(encoding="utf-8"))
    return ["?" + v["name"] for v in data["values"]]


def resolve(src: Path, resolver: Resolver, base: Path) -> tuple[list[str], list[str], dict]:
    """Returns (resolved lines, expect lines, options)."""
    resolved, expects, opts = [], [], {}
    for raw in src.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "init":
            if len(w) >= 2 and w[1] == "option":
                if len(w) != 4 or w[2] != "Players" or not w[3].isdigit() or not 1 <= int(w[3]) <= 4:
                    raise RunError(f"bad init option line (only 'Players' 1-4): {line}")
                opts["Players"] = int(w[3])
                continue
            if len(w) >= 2 and w[1] == "watch":
                out = []
                for a in w[2:]:
                    out += watch_names(a[1:]) if a.startswith("@") else [a]
                resolved.append(" ".join(["init", "watch", *out]))
                continue
            resolved.append(line)
            continue
        if not w[0].split("-")[0].isdigit():
            raise RunError(f"line does not start with a frame: {line}")
        if len(w) > 1 and w[1] == "expect":
            expects.append(line)
            continue
        if len(w) > 1 and w[1] in ("event", "msg") and len(w) >= 4:
            w[3] = str(resolver.code(w[3], w[1], w[2]))
        elif len(w) > 1 and w[1] == "text" and len(w) == 3:
            w[2] = str(int(w[2], 0))
        elif len(w) > 1 and w[1] == "dump":
            w = w[:3] + [x for a in w[3:] for x in (watch_names(a[1:]) if a.startswith("@") else [a])]
        resolved.append(" ".join(w))
    return resolved, expects, opts


# ---- run -----------------------------------------------------------------------------------
def run(args) -> int:
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    bin_path = Path(args.bin).resolve()
    src = Path(args.script).resolve()
    try:
        enum = find_enum(bin_path)
        msgmap = json.loads(MSGMAP.read_text(encoding="utf-8")) if MSGMAP.exists() else None
        lines, expects, opts = resolve(src, Resolver(enum, msgmap), src.parent)
        targets = sm.load_targets()
        name = f"native-{args.table}"
        if name not in targets:
            raise RunError(f"unknown table {args.table!r}")
        displays = json.loads(sm.TARGETS_FILE.read_text())["_displays"]
        sm.ALLOWED_DISPLAYS = range(displays[0], displays[1] + 1)
        if not bin_path.exists():
            raise RunError(f"binary missing: {bin_path}")
    except (RunError, OSError, ValueError, KeyError) as e:
        print(f"hookrun: {e}", file=sys.stderr)
        return 2
    (out / "script.uh").write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
    (out / "script.resolved.uh").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "script.expect.json").write_text(json.dumps({"expects": expects}), encoding="utf-8")
    (out / "exit_code.txt").unlink(missing_ok=True)

    target = dict(targets[name])
    target["ini"] = dict(target.get("ini", {}), **({"Players": opts["Players"]} if "Players" in opts else {}))
    target["env"] = {"FT_TEST_SCRIPT": str(out / "script.resolved.uh"), "FT_TEST_OUT": str(out)}
    sm.load_targets = lambda: {**targets, name: target}
    sm.NATIVE_BIN = bin_path
    sess = sm.Session(name, args.display, out, geometry="1024x768x24")
    sess.nudge_shots = sorted((int(m.group(1)), out / "frames" / f"{m.group(2)}.bmp")
                              for ln in lines if (m := re.match(r"^(\d+)\s+shot\s+(\S+)", ln)))
    sess.nudge_step = 0.1
    sess.nudge_clear = NUDGE_CLEAR_S
    sess.nudge_frame_s = FRAME_S
    sess.nudge_frame0_min = FRAME0_MIN_S
    rc = None
    problem = None
    try:
        sess._install_signal_handlers()
        try:
            sess.start()
        except sm.HarnessError as e:
            problem = str(e)
        if sess.proc is not None:
            try:
                rc = sess.proc.wait(timeout=args.timeout)
            except Exception:
                problem = f"timeout after {args.timeout} s: process killed"
    finally:
        sess.close()
    span = sess.nudge_span
    if span and rc is not None:
        late = [f.name for f in (out / "frames").glob("*.bmp") if f.name not in sess.nudge_pre and span[0] <= f.stat().st_mtime <= span[1] + 0.05]
        if late:
            problem = f"shot(s) {late} written during the window resize {span[0]:.2f}-{span[1]:.2f}: size not reliable"
            rc = None
    (out / "run.json").write_text(json.dumps({"table": args.table, "display": args.display, "bin": str(bin_path),
                                              "exit_code": rc, "problem": problem, "log": sess.log,
                                              "shot_times": {f.name: round(f.stat().st_mtime - sess.t_spawn_wall, 3) for f in (out / "frames").glob("*.bmp")}}, indent=2))
    if rc is None:
        print(f"hookrun: run error: {problem}", file=sys.stderr)
        return 2
    (out / "exit_code.txt").write_text(str(rc))
    return check_dir(out)


# ---- check ---------------------------------------------------------------------------------
def jl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x.strip()]


class State:
    def __init__(self, out: Path):
        self.timeline = jl(out / "timeline.jsonl")
        self.dumps = jl(out / "dumps.jsonl")
        self.sounds = jl(out / "sounds.jsonl")
        self.text = jl(out / "text.jsonl")

    def value(self, key: str, frame: int):
        """Timeline state at the end of `frame`, overlaid by dump lines up to that frame."""
        val, found = None, False
        for rec in self.timeline:
            if rec["frame"] > frame:
                break
            if key in rec:
                val, found = rec[key], True
        for rec in self.dumps:
            if rec["frame"] <= frame and key in rec:
                val, found = rec[key], True
        if not found:
            raise KeyError(key)
        return val


def parse_frames(tok: str) -> tuple[int, int]:
    a, _, b = tok.partition("-")
    return int(a), int(b or a)


def eval_expect(line: str, st: State) -> str:
    w = line.split()
    lo, hi = parse_frames(w[0])
    body = w[2:]
    if body[0] in ("sound", "track", "text") or (body[0] == "none" and body[1] in ("sound", "track", "text")):
        none = body[0] == "none"
        kind, rest = (body[1], body[2:]) if none else (body[0], body[1:])
        if kind == "sound":
            hits = [r for r in st.sounds if r["kind"] == "sound" and lo <= r["frame"] <= hi
                    and (not rest or r["id"] == int(rest[0]))]
        elif kind == "track":
            hits = [r for r in st.sounds if r["kind"] == "track" and lo <= r["frame"] <= hi
                    and (not rest or r["track"] == int(rest[0]))]
        else:
            box, op, extra = rest[0], rest[1], rest[2:]
            hits = [r for r in st.text if lo <= r["frame"] <= hi and r["box"] == box and r["op"] == op]
            while extra:
                k, v = extra[0], int(extra[1])
                hits = [r for r in hits if (r["code"] == v if k == "code" else v in r["ids"])]
                extra = extra[2:]
        ok = (not hits) if none else bool(hits)
        return "PASS" if ok else f"FAIL {'unexpected event' if none else 'no matching event'} in frames {lo}-{hi}"
    key, op = body[0], body[1]
    try:
        v = float(st.value(key, hi))
        if "delta" in body:
            v -= float(st.value(key, int(body[body.index("delta") + 1])))
            body = body[:body.index("delta")]
    except KeyError:
        return f"FAIL key {key!r} not in the outputs"
    except (TypeError, ValueError):
        return f"FAIL key {key!r} is not numeric at frame {hi}"
    a = [float(x) for x in body[2:]]
    ok = {"==": lambda: v == a[0], "!=": lambda: v != a[0], "~=": lambda: abs(v - a[0]) <= a[1],
          "in": lambda: a[0] <= v <= a[1]}[op]()
    return "PASS" if ok else f"FAIL {key} {op} {' '.join(body[2:])}: got {v:g}"


def check_dir(out: Path) -> int:
    st = State(out)
    expects = json.loads((out / "script.expect.json").read_text())["expects"]
    want = 0
    for line in (out / "script.uh").read_text().splitlines():
        w = line.split("#", 1)[0].split()
        if w[:2] == ["init", "expect_exit"]:
            want = int(w[2])
    rows = []
    ec = out / "exit_code.txt"
    got = int(ec.read_text()) if ec.exists() else None
    rows.append("PASS exit code %d" % got if got == want else f"FAIL exit code {got} (expected {want})")
    for line in expects:
        try:
            rows.append(f"{eval_expect(line, st)}  :: {line}")
        except (IndexError, KeyError, ValueError) as e:
            rows.append(f"FAIL malformed expect ({e!r})  :: {line}")
    (out / "expect.txt").write_text("\n".join(rows) + "\n")
    print("\n".join(rows))
    return 1 if any(r.startswith("FAIL") for r in rows) else 0


def check(args) -> int:
    out = Path(args.out).resolve()
    if not (out / "script.expect.json").exists():
        print(f"hookrun: {out} is not a run directory", file=sys.stderr)
        return 2
    return check_dir(out)


# ---- compare -------------------------------------------------------------------------------
def compare(args) -> int:
    a, b = Path(args.a), Path(args.b)
    rc = 0
    names = COMPARED + sorted(f"frames/{p.name}" for p in (a / "frames").glob("*.bmp")) \
        if (a / "frames").is_dir() else COMPARED
    names += sorted(f"frames/{p.name}" for p in (b / "frames").glob("*.bmp")
                    if f"frames/{p.name}" not in names) if (b / "frames").is_dir() else []
    for n in names:
        fa, fb = a / n, b / n
        if not fa.exists() or not fb.exists():
            res = f"FAIL missing in {'A' if not fa.exists() else 'B'}"
        else:
            res = "PASS" if fa.read_bytes() == fb.read_bytes() else "FAIL bytes differ"
        rc |= res.startswith("FAIL")
        print(f"{res}  {n}")
    return rc


# ---- from-json -----------------------------------------------------------------------------
def from_json(args) -> int:
    script = json.loads(Path(args.script).read_text(encoding="utf-8"))
    print("\n".join(convert_json(script, args.fixed_dt, args.late_players, args.startup_wait)))
    return 0


def convert_json(script: dict, dt: float, late_players: bool = False, startup_wait: bool = False) -> list[str]:
    """Harness JSON script -> .uh lines. late_players: the menu choice `alt+o, p, <n>` does not
    write `init option Players <n>` (the start-up game stays one player, as in the original); the choice is the
    `msg table NewGame <n>` line only, and every later `new_game` key becomes `msg table NewGame <n>` too, because
    a F2 key would run replay_level with Options.Players = 1. startup_wait adds `init startup_wait 1`."""
    steps = script["steps"]
    out = ["init window 960 666", f"init fixed_dt {dt:g}", "init dump_period 50"]
    if startup_wait:
        out.append("init startup_wait 1")
    menu_n = None
    sched = 0.0

    def fr(t: float) -> int:
        return round(t * 1000 / dt)

    last = 0
    i = 0
    while i < len(steps):
        s = steps[i]
        f = fr(sched)
        if "wait" in s:
            sched += float(s["wait"])
        elif "tap" in s and s["tap"] == "alt+o":
            seq = [x.get("tap") for x in steps[i:i + 8] if "tap" in x]
            n = next((x for x in seq[2:3] if x in ("1", "2", "3", "4")), None)
            if seq[:2] != ["alt+o", "p"] or n is None:
                raise RunError("menu tap sequence alt+o, p, <n> not recognised")
            if not late_players:
                out.insert(3, f"init option Players {n}")
            menu_n = n
            j = i
            taps = 0
            while taps < 3:                       # advance sched through the three taps and their waits
                st = steps[j]
                if "tap" in st:
                    taps += 1
                    if taps == 3:
                        out.append(f"{fr(sched)} msg table NewGame {n}")
                    sched += float(st.get("hold", 0.08))
                elif "wait" in st:
                    sched += float(st["wait"])
                j += 1
            i = j
            continue
        elif "tap" in s or "down" in s or "up" in s:
            name = s.get("tap") or s.get("down") or s.get("up")
            name = KEYMAP.get(name, name)
            if late_players and menu_n is not None and name == "new_game" and "tap" in s:
                out.append(f"{f} msg table NewGame {menu_n}")
                sched += float(s.get("hold", 0.08))
            elif name in LOGICAL:
                if "tap" in s:
                    hold = float(s.get("hold", 0.08))
                    out.append(f"{f} key down {name}")
                    out.append(f"{fr(sched + hold)} key up {name}")
                    sched += hold
                else:
                    out.append(f"{f} key {'down' if 'down' in s else 'up'} {name}")
            elif len(name) == 1 and "tap" in s:
                out.append(f"{f} type {name}")
                sched += float(s.get("hold", 0.08))
            else:
                raise RunError(f"key {name!r} has no hook equivalent")
        elif "peek" in s:
            out.append(f"{f} dump {s['peek']}")
        elif "shot" in s or "rootshot" in s:
            out.append(f"{f} shot {s.get('shot') or s['rootshot']}")
        elif "burst" in s:
            for n in range(int(s.get("count", 5))):
                out.append(f"{fr(sched)} shot {s['burst']}_{n:02d}")
                sched += float(s.get("interval", 0.1))
        last = max(last, fr(sched))
        i += 1
    out.append(f"{last + 1} quit")
    head = [x for x in out if x.startswith("init")]
    body = sorted((x for x in out if not x.startswith("init")), key=lambda x: int(x.split()[0]))
    return head + body


def dispatch_wine(argv: list[str]) -> int:
    try:
        from ftharness import wine_compare
    except ImportError:
        print("hookrun: ftharness/wine_compare.py is missing (unit UT provides it); "
              f"cannot run {argv[0]}", file=sys.stderr)
        return 2
    return wine_compare.cli(argv)


def main(argv: list[str]) -> int:
    if argv and argv[0] in ("compare-wine", "rest-state"):
        return dispatch_wine(argv)
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--table", required=True, choices=["cadet", "dragon", "pirates"])
    r.add_argument("--bin", required=True)
    r.add_argument("--display", type=int, required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--timeout", type=float, default=120)
    r.add_argument("script")
    c = sub.add_parser("check")
    c.add_argument("out")
    m = sub.add_parser("compare")
    m.add_argument("a")
    m.add_argument("b")
    j = sub.add_parser("from-json")
    j.add_argument("script")
    j.add_argument("--fixed-dt", type=float, required=True)
    j.add_argument("--table", default="cadet")
    j.add_argument("--late-players", action="store_true",
                   help="menu player choice starts n players by msg, not by `init option Players` (README)")
    j.add_argument("--startup-wait", action="store_true",
                   help="emit `init startup_wait 1` (Dragon / Pirates: NewGame at script time 2.0 s)")
    args = p.parse_args(argv)
    try:
        return {"run": run, "check": check, "compare": compare, "from-json": from_json}[args.cmd](args)
    except RunError as e:
        print(f"hookrun: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
