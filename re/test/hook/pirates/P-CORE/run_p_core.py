#!/usr/bin/env python3
"""Check runner for this directory: runs the scripts twice on a hook binary and display, `hookrun.py compare` of the two
runs, plus target.log greps.
  run_p_core.py <hook-binary> <display> <out-dir>           pc_launch, pc_1max (real callers)
  run_p_core.py --probe <hook-binary> <display> <out-dir>   PROBE-ONLY: probe_*.uh, which need a probe build
Exit 0 = every check PASS. The probe scripts and their `pending` greps need a probe build with test-only stubs of
BumperControl, ShipSinkControl, HoldSinkControl and KickoutControl (not part of the tree); on a normal build the real
controls run instead, so probe_extra_ball / probe_jackpot never reach AwardExtraBall / AwardJackpot and --probe FAILS."""
import subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HOOKRUN = Path("re/test/hookrun.py").resolve()   # run from the repo root, like every hookrun command
# script -> (substrings target.log must contain, substrings it must not contain)
LOGS = {
    # Mode3LaunchController (pirates_ball.cpp) and SetModeLaunch are real definitions, so a normal build never logs them as
    # pending.
    "pc_launch": ([], ["pending Mode3LaunchController", "pending SetModeLaunch"]),
    "pc_1max": ([], ["pending table_add_extra_ball"]),
    "probe_extra_ball": ([], ["pending AwardExtraBall"]),
    "probe_jackpot": ([], ["pending AwardJackpot", "pending SpecialAddScore"]),
    "probe_cleanup": (["pending SetHideawayLights", "pending SetSkulduggeryLights", "pending SetLavaLights",
                       "pending RestoreHideawayLights", "pending RestoreLavaLights", "pending ResetLduggTargets",
                       "pending ResetSkuTargets", "pending ResetEryTargets"],
                      ["pending ModeStartCleanup", "pending ModeEndRestoreLights"]),
    "probe_cleanup_msg12": (["pending SetHideawayLights", "pending SetSkulduggeryLights"],
                            ["pending SetLavaLights", "pending ModeStartCleanup"]),
}


def run(binary, display, out, script):
    r = subprocess.run([sys.executable, str(HOOKRUN), "run", "--table", "pirates", "--bin", binary, "--display", display,
                        "--out", str(out), str(script)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return r.returncode


def main():
    args = sys.argv[1:]
    probe = args[:1] == ["--probe"]
    binary, display, out = args[probe], args[probe + 1], Path(args[probe + 2])
    bad = 0
    for script in sorted(HERE.glob("*.uh")):
        name = script.stem
        if name.startswith("probe_") != probe:
            continue
        a, b = out / name, out / (name + "_2")
        rc = (run(binary, display, a, script), run(binary, display, b, script))
        cmp_rc = subprocess.run([sys.executable, str(HOOKRUN), "compare", str(a), str(b)], stdout=subprocess.DEVNULL).returncode
        log = (a / "target.log").read_text(errors="replace") if (a / "target.log").exists() else ""
        must, mustnt = LOGS.get(name, ([], []))
        miss = [m for m in must if m not in log]
        extra = [m for m in mustnt if m in log]
        ok = rc == (0, 0) and cmp_rc == 0 and not miss and not extra
        bad += not ok
        print("%s %s: expect rc=%s compare=%d missing=%s unexpected=%s" % ("PASS" if ok else "FAIL", name, rc, cmp_rc, miss, extra))
    return 1 if bad else 0


sys.exit(main())
