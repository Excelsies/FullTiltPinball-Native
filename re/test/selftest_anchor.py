"""Self-test of probe_run's script anchor. No display, no Wine.
Run: python re/test/selftest_anchor.py        Exit 0 = pass, 1 = fail.
"""
from __future__ import annotations

import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
import probe_run as pr                       # noqa: E402
from ftharness.session import HarnessError   # noqa: E402

FAILS = []


def check(name, ok, got=None):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"  got {got}"))
    if not ok:
        FAILS.append(name)


def raises(fn, *a) -> bool:
    try:
        fn(*a)
    except HarnessError:
        return True
    return False


sess = SimpleNamespace(t_window=100.0, t_placed=100.305)
check("placed anchor = t_placed", abs(pr.script_t0(sess, "placed") - 100.305) < 1e-9)
check("window anchor = t_window", abs(pr.script_t0(sess, "window") - 100.0) < 1e-9)
check("unknown anchor refused", raises(pr.script_t0, sess, "start"))
check("unset time refused", raises(pr.script_t0, SimpleNamespace(t_window=0.0, t_placed=0.0), "window"))
check("window anchor: step at 3.0 s is after focus", not raises(pr.check_after_focus, sess, 100.0, 3.0))
check("window anchor: step at 0.2 s is before focus (refused)", raises(pr.check_after_focus, sess, 100.0, 0.2))
check("placed anchor: step at 0 is fine", not raises(pr.check_after_focus, sess, 100.305, 0.0))
sys.exit(1 if FAILS else 0)
