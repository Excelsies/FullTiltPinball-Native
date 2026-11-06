"""Frame-to-frame activity of a newgame_burst.json run: python burst_activity.py <run dir> <x> <y> <w> <h> [keydown index of the F2, default 0; 'none' = no key, times from replay start]

Crops each burst shot (marker ng_NN) to the region, forces 3-channel sRGB, compares consecutive crops with
ImageMagick `compare -metric AE -fuzz 10%` and prints `<seconds after F2>:<value>` pairs. The value is IM 7's
fuzz-weighted AE (a magnitude-weighted sum, see README), used here only as a relative activity measure.
The F2 time is the first keydown of run.json."""
import json
import subprocess
import sys
from pathlib import Path

run = Path(sys.argv[1])
x, y, w, h = (int(v) for v in sys.argv[2:6])
log = json.loads((run / "run.json").read_text())["log"]
steps = [e for e in log if e.get("event") == "step" and e.get("marker", "").startswith("ng_")]
f2 = 0.0 if sys.argv[6:7] == ["none"] else [e for e in log if e.get("action") == "keydown"][int(sys.argv[6]) if len(sys.argv) > 6 else 0]["actual"]
prev, out = None, []
for e in steps:
    crop = run / "shots" / (e["marker"] + ".crop.png")
    subprocess.run(["magick", str(run / "shots" / (e["marker"] + ".png")), "-crop", f"{w}x{h}+{x}+{y}",
                    "+repage", "-type", "TrueColor", str(crop)], check=True)
    if prev:
        res = subprocess.run(["magick", "compare", "-metric", "AE", "-fuzz", "10%", str(prev), str(crop), "null:"],
                             capture_output=True, text=True).stderr
        out.append((round(e["actual"] - f2, 2), float(res.split()[0])))
    prev = crop
print(" ".join(f"{t}:{int(v)}" for t, v in out))
