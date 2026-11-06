"""Self-test of ``compare.py --align``. Run: python re/test/selftest_compare_align.py SHOT.png [--real-run DIR]

Needs one real captured native screenshot (960x666), given as the first argument (no default: without it
the self-test prints "NOT ASSESSED: no stored run, pass --shot PATH"-style guidance and exits 2). The game
images stay in re/work; this file holds none. The self-test builds fake run directories from that capture in a scratch directory:
  B = the capture; A = the same capture shifted by one pixel (a row or a column of black added at the top /
  left, so A's image is also one pixel larger). The alignment search must find the offset that undoes the
  shift, and the residual must fall to the level of the unshifted pair (A = B).
The synthetic shifts splice one real capture; RealWineNativePair also uses a stored Wine and native capture pair
of different size (a Dragon's Keep first-frame run directory with wine/ and native/ subdirectories, given with
--real-run DIR; skipped when not given): Wine lower region y 353 with align 1 must find the
offset (0, 1) on every marker and give the same differing-pixel counts as y 354 with align 1.
Exit 0 = all pass, 1 = a check failed, 2 = no capture available (NOT ASSESSED).
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ftharness.compare import compare_runs                   # noqa: E402
from ftharness.session import WORK                           # noqa: E402

SCRATCH = WORK / "harness_runs" / "selftest_compare_align"
TARGET = "native-dragon"               # region "table" = [0, 20, 960, 646]: x = 0, so dx = -1 clips
SHOT: Path | None = None


def _find_shot() -> Path | None:
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        return Path(sys.argv.pop(1))
    return None


def _find_real_run() -> Path | None:
    if "--real-run" in sys.argv:
        i = sys.argv.index("--real-run")
        path = Path(sys.argv[i + 1])
        del sys.argv[i:i + 2]
        return path
    return None


def _magick(*args: str) -> None:
    subprocess.run(["magick", *args], check=True, capture_output=True, timeout=60)


def _make_run(name: str, image_cmd: list[str]) -> Path:
    run = SCRATCH / name
    (run / "shots").mkdir(parents=True)
    (run / "run.json").write_text(json.dumps({"target": TARGET}), encoding="utf-8")
    _magick(str(SHOT), *image_cmd, str(run / "shots" / "m.png"))
    return run


def _compare(a: Path, b: Path, align: int) -> dict:
    rep = compare_runs(a, b, SCRATCH / f"out_{a.name}_{align}", fuzz="8%", region="table", align=align)
    assert len(rep["markers"]) == 1
    return rep["markers"][0]


class AlignSearch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.rmtree(SCRATCH, ignore_errors=True)
        SCRATCH.mkdir(parents=True)
        cls.b = _make_run("b", [])
        cls.same = _make_run("a_same", [])
        cls.down = _make_run("a_down1", ["-background", "black", "-splice", "0x1"])    # 960x667
        cls.right = _make_run("a_right1", ["-background", "black", "-splice", "1x0"])  # 961x666
        cls.both = _make_run("a_both", ["-background", "black", "-splice", "1x1"])     # 961x667

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(SCRATCH, ignore_errors=True)

    def test_capture_has_texture(self):
        # a flat image would make every offset equal; the self-test needs the real capture to differ when shifted
        self.assertGreater(_compare(self.down, self.b, 0)["ae_fuzz"], 0)

    def test_unshifted_stays_at_origin(self):
        m = _compare(self.same, self.b, 1)
        self.assertEqual(m["a_offset"], [0, 0])
        self.assertEqual(m["ae_fuzz"], 0)

    def test_vertical_shift_found(self):
        base = _compare(self.same, self.b, 1)["ae_fuzz"]
        fixed = _compare(self.down, self.b, 0)["ae_fuzz"]
        m = _compare(self.down, self.b, 1)
        self.assertEqual(m["a_offset"], [0, 1])
        self.assertEqual(m["ae_fuzz"], base)
        self.assertLess(m["ae_fuzz"], fixed)

    def test_horizontal_shift_found_with_clipping_offsets(self):
        # x = 0 region: the search tries x = -1, which lies outside the image and must not abort the search
        m = _compare(self.right, self.b, 1)
        self.assertEqual(m["a_offset"], [1, 0])
        self.assertEqual(m["ae_fuzz"], 0)

    def test_diagonal_shift_found(self):
        m = _compare(self.both, self.b, 1)
        self.assertEqual(m["a_offset"], [1, 1])
        self.assertEqual(m["ae_fuzz"], 0)

    def test_search_radius_respected(self):
        m = _compare(self.both, self.b, 0)
        self.assertEqual(m["a_offset"], [0, 0])


REAL_RUN: Path | None = None


class RealWineNativePair(unittest.TestCase):
    """Real captures: Wine crop and native crop differ in size and sit one pixel apart vertically."""

    def setUp(self):
        if REAL_RUN is None or not (REAL_RUN / "wine").is_dir() or not (REAL_RUN / "native").is_dir():
            self.skipTest("NOT ASSESSED: no stored Wine/native pair, pass --real-run DIR")

    @staticmethod
    def _rows(lower_y: int, align: int) -> list:
        from ftharness import session
        targets = json.loads(session.TARGETS_FILE.read_text(encoding="utf-8"))
        for key, val in targets.items():
            if key.startswith("wine-"):
                val["regions"]["lower"][1] = lower_y
        private = SCRATCH / f"targets_{lower_y}.json"
        private.write_text(json.dumps(targets), encoding="utf-8")
        saved = session.TARGETS_FILE
        session.TARGETS_FILE = private
        try:
            rep = compare_runs(REAL_RUN / "wine", REAL_RUN / "native", SCRATCH / f"real_{lower_y}_{align}",
                               region="lower", align=align)
        finally:
            session.TARGETS_FILE = saved
        return rep["markers"]

    def test_y353_with_align_absorbs_the_one_pixel_shift(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        try:
            shifted, reference = self._rows(353, 1), self._rows(354, 1)
            self.assertTrue(shifted)
            self.assertEqual([m["a_offset"] for m in shifted], [[0, 1]] * len(shifted))
            self.assertEqual([m["diff_px"] for m in shifted], [m["diff_px"] for m in reference])
            self.assertEqual([m["ae_pct"] for m in shifted], [m["ae_pct"] for m in reference])
        finally:
            shutil.rmtree(SCRATCH, ignore_errors=True)


if __name__ == "__main__":
    REAL_RUN = _find_real_run()
    SHOT = _find_shot()
    if SHOT is None or not SHOT.is_file():
        print("NOT ASSESSED: no stored run, pass the screenshot PATH as the first argument (a captured native 960x666 screenshot)")
        sys.exit(2)
    print(f"capture: {SHOT}")
    unittest.main(argv=[sys.argv[0]], verbosity=2)
