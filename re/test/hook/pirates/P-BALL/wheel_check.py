"""Skill-wheel check: skill_index changes by +1 mod 9, each value held 24..26 frames (the 0.2 s glyph timer plus the text-box tick; Wine's period is 0.251 s) once the wheel runs.
usage: wheel_check.py <run dir>   exit 0 = PASS"""
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1] + "/timeline.jsonl")]
seq = [(r["frame"], r["skill_index"]) for r in rows if "skill_index" in r]
steps = 0
bad = []
for (f0, v0), (f1, v1) in zip(seq, seq[1:]):
    if f0 < 150:
        continue
    steps += 1
    if (v0 + 1) % 9 != v1 or not 24 <= f1 - f0 <= 26:
        bad.append((f0, v0, f1, v1))
print("steps", steps, "bad", bad)
import glob
shots = sorted(glob.glob(sys.argv[1] + "/frames/*.bmp"))
unreadable = [p for p in shots if open(p, "rb").read(2) != b"BM"]
print("shots", len(shots), "unreadable", unreadable)
sys.exit(0 if steps >= 15 and not bad and len(shots) == 15 and not unreadable else 1)
