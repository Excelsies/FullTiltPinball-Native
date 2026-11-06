"""Side-by-side test harness: original Full Tilt exes under Wine vs the native port, on Xvfb.

  python re/test/harness.py init-prefix                 # create re/work/wine-prefix once
  python re/test/harness.py init-audio-prefix           # private copy with Wine audio on a null sink
  python re/test/harness.py start  wine-cadet --display 90 --hold 30
  python re/test/harness.py run    wine-cadet re/test/scripts/smoke.json --display 90
  python re/test/harness.py compare <runA> <runB> [--out DIR]
  python re/test/harness.py stop   --display 90           # clean up a display left behind

See re/test/README.md. Targets (exe, data dir, window title, regions) live in targets.json.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ftharness import session as session_mod                 # noqa: E402
from ftharness.compare import compare_runs                   # noqa: E402
from ftharness.replay import load_script, replay             # noqa: E402
from ftharness.session import (PREFIX, PREFIX_AUDIO, WORK, HarnessError, Session,  # noqa: E402
                               run_checked, wine_env)

RUNS = WORK / "harness_runs"


def allowed_displays() -> range:
    """Display range from targets.json "_displays" [first, last].

    Session.start() checks ftharness.session.ALLOWED_DISPLAYS, so main() sets that module value
    from here too; session.py reads the same range at import, so entry points that bypass harness.py
    (scripts/probe_run.py) get it as well."""
    data = json.loads(session_mod.TARGETS_FILE.read_text(encoding="utf-8"))
    first, last = data["_displays"]
    if not (isinstance(first, int) and isinstance(last, int) and 0 < first <= last):
        raise HarnessError(f"targets.json _displays must be [first, last] ints, got {data['_displays']!r}")
    return range(first, last + 1)


def init_prefix(display: int) -> None:
    """wineboot under a temporary Xvfb, then disable Wine audio (silent runs). NOT timing-neutral:
    without a sound device the original loads no wave, so every sound duration is 0 and the
    NewGame light show / ball feed run shorter than with audio (measured, README).
    The audio-on targets (``init_audio_prefix``) match native when the F2 comes after the original's
    own startup light show (about 5 s after the window appears, scripts/newgame_burst_late.json);
    an F2 at 3.2 s lands inside that show and skips the new game's show (README Audio)."""
    env = wine_env(display)
    xvfb = subprocess.Popen(["Xvfb", f":{display}", "-screen", "0", "1024x768x24",
                             "-nolisten", "tcp"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        time.sleep(1.0)
        run_checked(["wineboot", "-i"], env=env, timeout=600)
        run_checked(["wine", "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Audio",
                     "/t", "REG_SZ", "/d", "", "/f"], env=env)
    finally:
        subprocess.run(["wineserver", "-k"], env=env, capture_output=True, timeout=30)
        subprocess.run(["wineserver", "-w"], env=env, capture_output=True, timeout=30)
        xvfb.terminate()
        xvfb.wait(timeout=10)


def init_audio_prefix(display: int) -> None:
    """Private copy of the shared prefix whose Wine audio driver is alsa. Output goes nowhere: the
    audio targets run it with ALSA_CONFIG_PATH=re/test/audio/asound_null.conf (null PCM). Refuses to
    overwrite an existing copy (delete it yourself)."""
    if "FT_WINEPREFIX_AUDIO" not in os.environ:
        raise HarnessError("FT_WINEPREFIX_AUDIO is not set: name the private directory to create "
                           "(its owner is whoever sets it; delete it when done)")
    if PREFIX_AUDIO == PREFIX:
        raise HarnessError(f"FT_WINEPREFIX_AUDIO equals the shared prefix {PREFIX}")
    if PREFIX_AUDIO.exists():
        raise HarnessError(f"{PREFIX_AUDIO} exists; delete it first (rm -r is yours to run)")
    if not (PREFIX / "system.reg").exists():
        raise HarnessError(f"shared prefix missing: {PREFIX} (run init-prefix)")
    run_checked(["cp", "-a", str(PREFIX), str(PREFIX_AUDIO)], timeout=600)
    target = {"audio": "null-sink"}
    env = wine_env(display, target)
    xvfb = subprocess.Popen(["Xvfb", f":{display}", "-screen", "0", "1024x768x24",
                             "-nolisten", "tcp"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        time.sleep(1.0)
        run_checked(["wine", "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Audio",
                     "/t", "REG_SZ", "/d", "alsa", "/f"], env=env)
    finally:
        subprocess.run(["wineserver", "-k"], env=env, capture_output=True, timeout=30)
        subprocess.run(["wineserver", "-w"], env=env, capture_output=True, timeout=30)
        xvfb.terminate()
        xvfb.wait(timeout=10)


def run_dir(target: str, script_name: str, out: str | None) -> Path:
    if out:
        return Path(out).resolve()
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return RUNS / f"{stamp}_{target}_{script_name}"


def write_run_json(sess: Session, extra: dict) -> None:
    data = {"target": sess.target_name, "display": sess.display, "geometry": sess.geometry,
            "wid": sess.wid, "log": sess.log, **extra}
    (sess.out_dir / "run.json").write_text(json.dumps(data, indent=2), encoding="utf-8")


def cmd_start(args) -> int:
    out = run_dir(args.target, "start", args.out)
    with Session(args.target, args.display, out, reset_registry=not args.keep_registry) as sess:
        print(sess.wid, flush=True)
        write_run_json(sess, {"mode": "start"})
        deadline = time.monotonic() + args.hold if args.hold else None
        while deadline is None or time.monotonic() < deadline:
            if sess.proc is not None and sess.proc.poll() is not None:
                print(f"target exited rc={sess.proc.returncode}", file=sys.stderr)
                break
            time.sleep(0.2)
    return 0


def cmd_run(args) -> int:
    script = load_script(Path(args.script))
    out = run_dir(args.target, script["name"], args.out)
    status = "fail"
    sess = Session(args.target, args.display, out, reset_registry=not args.keep_registry)
    try:
        with sess:
            if args.settle:
                time.sleep(args.settle)
            records = replay(sess, script)
            status = "pass"
            lates = [r["late_ms"] for r in records if "late_ms" in r]
            summary = {"keys": len(lates),
                       "late_ms_max": max(lates) if lates else 0.0,
                       "late_ms_mean": round(sum(lates) / len(lates), 2) if lates else 0.0}
            print(json.dumps({"out": str(out), "wid": sess.wid, "timing": summary}))
    finally:
        write_run_json(sess, {"mode": "run", "script": str(Path(args.script).resolve()),
                              "status": status})
    return 0


def cmd_compare(args) -> int:
    a, b = Path(args.run_a).resolve(), Path(args.run_b).resolve()
    out = Path(args.out).resolve() if args.out else a.parent / f"compare_{a.name}__vs__{b.name}"
    report = compare_runs(a, b, out, fuzz=args.fuzz, region=args.region, align=args.align)
    print((out / "report.md").read_text(encoding="utf-8"))
    return 0 if report["markers"] else 1


def cmd_stop(args) -> int:
    """Kill whatever is left on a harness display: Wine server of our prefix, then Xvfb."""
    target = {"audio": "null-sink"} if args.audio else None
    subprocess.run(["wineserver", "-k"], env=wine_env(args.display, target), capture_output=True,
                   timeout=30)
    lock = Path(f"/tmp/.X{args.display}-lock")
    if lock.exists():
        pid = int(lock.read_text().strip())
        try:
            os.kill(pid, 15)
            print(f"killed Xvfb :{args.display} pid {pid}")
        except ProcessLookupError:
            lock.unlink(missing_ok=True)
            print(f"removed stale lock {lock}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("init-prefix", help=f"create the Wine prefix {PREFIX}")
    p.add_argument("--display", type=int, default=90)
    for name in ("start", "run"):
        p = sub.add_parser(name)
        p.add_argument("target")
        if name == "run":
            p.add_argument("script")
            p.add_argument("--settle", type=float, default=0.0,
                           help="seconds to wait after the window appears before replay")
        else:
            p.add_argument("--hold", type=float, default=0.0,
                           help="seconds to keep the target up (0 = until SIGINT/SIGTERM)")
        p.add_argument("--display", type=int, default=90)
        p.add_argument("--out")
        p.add_argument("--keep-registry", action="store_true",
                       help="Wine: keep HKCU\\Software\\Cinematronics from earlier runs")
    p = sub.add_parser("compare")
    p.add_argument("run_a")
    p.add_argument("run_b")
    p.add_argument("--out")
    p.add_argument("--fuzz", default="8%")
    p.add_argument("--region", default="table")
    p.add_argument("--align", type=int, default=1,
                   help="residual alignment search radius in px (0 = fixed crops)")
    p = sub.add_parser("init-audio-prefix", help=f"copy {PREFIX} to {PREFIX_AUDIO}, Wine audio alsa")
    p.add_argument("--display", type=int, default=90)
    p = sub.add_parser("stop")
    p.add_argument("--display", type=int, default=90)
    p.add_argument("--audio", action="store_true", help="the wineserver of the audio prefix")
    args = ap.parse_args()
    try:
        displays = allowed_displays()
        session_mod.ALLOWED_DISPLAYS = displays
        if args.cmd in ("start", "run") and args.display not in displays:
            raise HarnessError(f"display :{args.display} is not one of the harness displays "
                               f"{displays.start}-{displays.stop - 1}")
        if args.cmd == "init-prefix":
            init_prefix(args.display)
            return 0
        if args.cmd == "init-audio-prefix":
            init_audio_prefix(args.display)
            return 0
        return {"start": cmd_start, "run": cmd_run, "compare": cmd_compare,
                "stop": cmd_stop}[args.cmd](args)
    except HarnessError as exc:
        print(f"harness: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
