"""Side-by-side comparison of two runs (e.g. Wine vs native) marker by marker.

Each run dir holds run.json (written by harness.py run) and shots/<marker>.png. Both shots are
cropped to their target's "table" region (game area without menu bar / border) so the two
renderers line up; if the cropped sizes still differ, B is scaled to A's size and the report
says so. Metrics (ImageMagick ``compare``):
  ae_fuzz   - ImageMagick 7 ``-metric AE -fuzz F``. NOT a count of differing pixels. Measured
              (IM 7.1.2-34, synthetic 1-pixel images): with a fuzz, a colour channel whose
              difference is below about 0.113 of full scale (28 of 255 at 8 %; IM scales the fuzz
              by sqrt(2)) adds 0, and above it adds |difference| / full scale / 3. So a pixel that
              flips fully (black to white) adds 1.0, a pixel 40/255 off in every channel adds 0.157,
              a pixel 25/255 off adds 0. The value is therefore a magnitude-weighted sum in "pixel
              equivalents" (a fraction, never truncated), several times smaller than the number of
              pixels that differ visibly (a real DRAGON `new_game` crop: AE 1428.8 against 9123
              differing pixels at the same 8 % fuzz, the report's diff_px).
  ae_pct    - ae_fuzz as a percentage of all pixels (the figure the comparison limits are written in)
  diff_px   - the number of pixels with at least one channel differing by more than the fuzz
              fraction of full scale (8 % = 20.4 of 255), counted by this module on a
              difference image (independent of IM's AE rules); diff_px_pct is its share
  rmse      - normalised root-mean-square error over all channels (0 = identical, 1 = max)
status    - "required", or "reported only" for a marker listed in either target's ``report_only`` (targets.json)
A's crop is searched over +-align px for the offset with the smallest AE (ties: nearest the origin); the offset
is reported (measured: the native score panel sits 1 px right of Wine's relative to
the playfield, so a fixed crop would turn that into noise).
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path

from .session import HarnessError, load_targets


def _metric(a: Path, b: Path, metric: str, fuzz: str, diff: Path | None = None) -> float:
    cmd = ["magick", "compare", "-metric", metric]
    if fuzz:
        cmd += ["-fuzz", fuzz]
    cmd += [str(a), str(b), str(diff) if diff else "null:"]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if res.returncode not in (0, 1):                      # 1 = images differ, 2 = error
        raise HarnessError(f"compare failed: {res.stderr.strip()[:300]}")
    text = res.stderr.strip()
    # AE prints "N" (IM7 may add "(fraction)"); RMSE prints "abs (normalised)".
    if "(" in text and metric == "RMSE":
        return float(text.split("(")[1].split(")")[0])
    return float(text.split()[0])


def _diff_pixels(a: Path, b: Path, fuzz: str, mask: Path) -> int:
    """Pixels with at least one channel differing by more than ``fuzz`` (a percentage of full
    scale) = white pixels of ``mask``. Does not use IM's fuzzy comparison (its AE rules are not a
    pixel count, see the module docstring)."""
    if fuzz and not fuzz.endswith("%"):
        raise HarnessError(f"fuzz must be a percentage such as 8%, got {fuzz!r}")
    threshold = fuzz or "0%"
    subprocess.run(["magick", str(a), str(b), "-compose", "difference", "-composite",
                    "-channel", "RGB", "-separate", "+channel", "-evaluate-sequence", "Max",
                    "-threshold", threshold, "-type", "Grayscale", str(mask)],
                   check=True, capture_output=True, timeout=60)
    out = subprocess.run(["magick", str(mask), "-format", "%[fx:mean*w*h]", "info:"],
                         check=True, capture_output=True, text=True, timeout=60).stdout
    return int(round(float(out)))


def _crop(src: Path, region: list[int] | None, dst: Path, size: tuple[int, int] | None) -> None:
    cmd = ["magick", str(src)]
    if region:
        x, y, w, h = region
        # A region reaching outside the image (the alignment search tries x - 1 on regions at x = 0)
        # is padded with black so the crop keeps its w x h size and the content keeps its position:
        # left / top padding is spliced in before the crop, right / bottom padding added after it.
        cmd += ["-background", "black", "-gravity", "NorthWest"]
        if x < 0:
            cmd += ["-splice", f"{-x}x0"]
        if y < 0:
            cmd += ["-splice", f"0x{-y}"]
        cmd += ["-crop", f"{w}x{h}+{max(x, 0)}+{max(y, 0)}", "+repage", "-extent", f"{w}x{h}"]
    if size:
        cmd += ["-resize", f"{size[0]}x{size[1]}!"]
    # Always write 3-channel sRGB: magick otherwise re-encodes a crop whose pixels are all grey as
    # a grayscale PNG, and AE between a grey and a colour image counts whole pixels instead of
    # per-channel differences (measured), so the metric would depend on the content.
    cmd += ["-type", "TrueColor", "-define", "png:color-type=2"]
    subprocess.run(cmd + [str(dst)], check=True, capture_output=True, timeout=60)


def _size(path: Path) -> tuple[int, int]:
    out = subprocess.run(["magick", "identify", "-format", "%w %h", str(path)],
                         check=True, capture_output=True, text=True, timeout=30).stdout
    w, h = out.split()
    return int(w), int(h)


def compare_runs(run_a: Path, run_b: Path, out: Path, fuzz: str = "8%",
                 region: str = "table", align: int = 1) -> dict:
    targets = load_targets()
    meta = {}
    for run in (run_a, run_b):
        rj = run / "run.json"
        if not rj.exists():
            raise HarnessError(f"{rj} missing (not a harness run directory)")
        meta[run] = json.loads(rj.read_text(encoding="utf-8"))
    out.mkdir(parents=True, exist_ok=True)
    ta, tb = (targets[meta[r]["target"]] for r in (run_a, run_b))
    ra, rb = ta.get("regions", {}).get(region), tb.get("regions", {}).get(region)
    report_only = set(ta.get("report_only", [])) | set(tb.get("report_only", []))
    markers_a = {p.stem for p in (run_a / "shots").glob("*.png") if "." not in p.stem}
    markers_b = {p.stem for p in (run_b / "shots").glob("*.png") if "." not in p.stem}
    rows = []
    for marker in sorted(markers_a & markers_b):
        a, b = out / f"{marker}.a.png", out / f"{marker}.b.png"
        _crop(run_b / "shots" / f"{marker}.png", rb, b, None)
        size_b = _size(b)
        offset, best = (0, 0), None
        # Residual alignment search (A shifted). Candidates run nearest-to-origin first and only a
        # strictly smaller residual replaces the best, so an unshifted pair stays at (0, 0). Every
        # candidate crop has the region's size (out-of-image parts are black), so each is compared.
        shifts = sorted(((dx, dy) for dy in range(-align, align + 1) for dx in range(-align, align + 1)),
                        key=lambda d: (abs(d[0]) + abs(d[1]), d))
        for dx, dy in shifts:
            shifted = [ra[0] + dx, ra[1] + dy, ra[2], ra[3]] if ra else None
            _crop(run_a / "shots" / f"{marker}.png", shifted, a, None)
            if _size(a) != size_b:
                continue                              # no region: whole images of different size
            ae_try = _metric(a, b, "AE", fuzz)
            if best is None or ae_try < best:
                best, offset = ae_try, (dx, dy)
        shifted = [ra[0] + offset[0], ra[1] + offset[1], ra[2], ra[3]] if ra else None
        _crop(run_a / "shots" / f"{marker}.png", shifted, a, None)
        size_a = _size(a)
        scaled = size_b != size_a
        if scaled:
            _crop(b, None, b, size_a)
        diff = out / f"{marker}.diff.png"
        ae = _metric(a, b, "AE", fuzz, diff)
        mask = out / f"{marker}.mask.png"
        diff_px = _diff_pixels(a, b, fuzz, mask)
        rmse = _metric(a, b, "RMSE", "")
        side = out / f"{marker}.side.png"
        subprocess.run(["magick", str(a), str(b), str(diff), "-background", "gray30",
                        "-splice", "6x0", "+append", str(side)],
                       check=True, capture_output=True, timeout=60)
        total = size_a[0] * size_a[1]
        rows.append({"marker": marker, "status": "reported only" if marker in report_only else "required",
                     "size": list(size_a), "b_scaled": scaled,
                     "a_offset": list(offset),
                     "ae_fuzz": round(ae, 3), "ae_pct": round(100.0 * ae / total, 3),
                     "diff_px": diff_px, "diff_px_pct": round(100.0 * diff_px / total, 3),
                     "rmse": round(rmse, 5), "side_by_side": side.name})
    report = {"a": {"dir": str(run_a), "target": meta[run_a]["target"]},
              "b": {"dir": str(run_b), "target": meta[run_b]["target"]},
              "region": region, "fuzz": fuzz, "align": align, "markers": rows,
              "only_in_a": sorted(markers_a - markers_b),
              "only_in_b": sorted(markers_b - markers_a)}
    (out / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = [f"# Compare {report['a']['target']} (A) vs {report['b']['target']} (B)", "",
             f"Region `{region}`, fuzz {fuzz}, alignment search +-{align} px (A's crop shifted "
             "by the offset that minimises the difference). Side-by-side: A | B | diff (red = differs).",
             "", "AE = ImageMagick 7 `-metric AE -fuzz`: a magnitude-weighted sum in pixel equivalents "
             "(channels beyond the fuzz add |difference|/full scale/3 each; a fully flipped pixel is "
             "1.0), NOT a pixel count; `differing px` = pixels with a channel beyond the fuzz.",
             "", "| marker | status | size | A offset | B scaled | AE (magnitude-weighted px-eq) | AE % | "
             "differing px | differing px % | RMSE |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['marker']} | {r['status']} | {r['size'][0]}x{r['size'][1]} | {tuple(r['a_offset'])} | "
                     f"{r['b_scaled']} | "
                     f"{r['ae_fuzz']} | {r['ae_pct']} | {r['diff_px']} | {r['diff_px_pct']} | {r['rmse']} |")
    if report["only_in_a"] or report["only_in_b"]:
        lines += ["", f"Only in A: {report['only_in_a']}", f"Only in B: {report['only_in_b']}"]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report
