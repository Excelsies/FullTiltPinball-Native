#!/usr/bin/env python3
"""Independent oracle for PickUnlitSpell on a d_spells_award.uh run.

usage: check_spells.py <run dir> [seed]

The draw of an award at frame f is the first one of that frame (events run before pb::frame), i.e. draw number
n_rand_draws(f-1) + 1 of the MSVC LCG (state = state * 214013 + 2531011, value = (state >> 16) & 0x7fff, seeded with the
`init seed`). Expected spell index = unlit[(r * count) >> 15] over the lights v_lite28 / 29 / 31 / 32 that were off at f-1.
Prints one line per award and exits 1 on any mismatch. Stdlib only, no game data.
"""
import json
import sys

LIGHTS = ["v_lite28", "v_lite29", "v_lite31", "v_lite32"]
AWARD_FRAMES = [240, 480, 720]  # frame 960 (bank 4): all four slots lit, no award (checked by d_spells_award.uh)


def lcg(seed, n):
    state = seed & 0xFFFFFFFF
    value = 0
    for _ in range(n):
        state = (state * 214013 + 2531011) & 0xFFFFFFFF
        value = (state >> 16) & 0x7FFF
    return value


def on(dump, name, rest):
    # light_on() = LightOnFlag || ToggledOnFlag || FlasherOnFlag; the hook shows the first and last directly, a toggled-on
    # light (TLightTurnOnTimed) only through its sprite: any bitmap other than the all-off one at frame 150 counts as lit.
    return (dump.get(name + ".light_on", 0) == 1 or dump.get(name + ".flasher_on", 0) == 1
            or dump.get(name + ".bitmap") != rest[name])


def main():
    run = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    dumps = {}
    with open(run + "/dumps.jsonl") as f:
        for line in f:
            j = json.loads(line)
            dumps[j["frame"]] = j
    rest = {n: dumps[150][n + ".bitmap"] for n in LIGHTS}
    bad = 0
    for fr in AWARD_FRAMES:
        before, after = dumps[fr - 1], dumps[fr]
        unlit = [i for i, n in enumerate(LIGHTS) if not on(before, n, rest)]
        draw_no = before["n_rand_draws"] + 1
        r = lcg(seed, draw_no)
        want = unlit[(r * len(unlit)) >> 15] if unlit else -1
        got_on = [i for i, n in enumerate(LIGHTS) if on(after, n, rest) and not on(before, n, rest)]
        # spell 3 (v_lite32) is lit by the real BlockerFControl(disable) (dragon_shop.cpp:43-52), like the others
        got = got_on[0] if got_on else None
        ok = (got == want) and len(got_on) <= 1 and after["spells"] == before["spells"] + 1
        print("frame %d: unlit %s draw #%d r=%d -> expected spell %d, lights newly on %s, spells %d->%d, extra draws in frame %d: %s"
              % (fr, unlit, draw_no, r, want, got_on, before["spells"], after["spells"],
                 after["n_rand_draws"] - before["n_rand_draws"], "PASS" if ok else "FAIL"))
        bad += 0 if ok else 1
    sys.exit(1 if bad else 0)


main()
