"""Bonus-count amount check: usage count_check.py <run dir> <text id>=<amount> ...  (ids in order of appearance after frame 1700).
Reads text.jsonl; the amount is the number on the second line of the displayed text. exit 0 = PASS.
Expected amounts per script: pb_count.uh `3028=60000 3029=600000 3033=660000` (targets_hit 6, modes_played 3);
pb_count_one.uh `3028=60000`; pb_count_full.uh `3028=20000 3029=400000 3030=500000 3031=100000 3032=5000 3033=1025000`.
In pb_count_full a_drop5 at 205 is replaced as scorer by s_oneway3 at 207 (mode 4 start), and the MissionControl pre-hook (0x42d850) counts the replacement
again (targets_hit 2 -> 20,000; the same mechanism as pb_count); the one-way's mode 4 start adds a mode step with the poked
modes_played 1 (2 x 200,000 = 400,000); 20,000 + 400,000 + 500,000 + 100,000 + 5,000 = 1,025,000."""
import json, sys
want = [tuple(int(x) for x in a.split("=")) for a in sys.argv[2:]]
rows = [json.loads(l) for l in open(sys.argv[1] + "/text.jsonl")]
got = []
for r in rows:
    if r["op"] == "display" and r["ids"] and r["frame"] > 1700 and r["ids"][0] in {w[0] for w in want}:
        lines = (r["text"] or "").split("\n")
        got.append((r["ids"][0], int(lines[1]) if len(lines) > 1 and lines[1].strip().isdigit() else None))
print("want", want, "got", got)
sys.exit(0 if got == want else 1)
