#!/usr/bin/env python3
"""Rerunnable generator of the ts_*.uh hook scripts of this directory (Prison and Watch tower sinks, dragon_tower_sinks.cpp).

Expected values come from the listing / machine code of DRAGON.EXE (addresses in each script's header), never from a run of the port:
only the sound ids tabc10wave and the plunger are pinned from a run (marked in the scripts).
Usage: python3 gen_scripts.py <out dir>   (rerun after editing; the scripts in this directory are its output)
"""
import sys

OUT = sys.argv[1]
D = int(sys.argv[2]) if len(sys.argv) > 2 else None  # engine draws per frame (measured by ts_prison1_empty), None = not yet known
S_TABC04, S_TABC51 = 24, 43  # sound ids pinned in D-TOWERS/t_trip_prison.uh / t_trip_prison6.uh
S_TABC10, S_PLUNGER = 26, 51  # pinned from run (sounds.jsonl of ts_prison1_trap: tabc10wave and the plunger's relaunch sound)

KEYS = ("v_lite48.light_on v_lite48.message_field v_lite49.light_on v_lite49.message_field "
        "v_lite50.message_field a_sink18.active a_sink17.active s_plunger1.pullback_started "
        "v_lite65.flasher_on v_lite66.flasher_on v_lite49.flasher_on")
HDR = "init fixed_dt 10\ninit seed 1\n10 key down new_game\n11 key up new_game\n"


def w(name, comment, body):
    c = "".join("# " + l + "\n" for l in comment.strip().split("\n"))
    with open("%s/%s.uh" % (OUT, name), "w") as f:
        f.write(c + HDR + body.strip() + "\n250 quit\n")


def pre(mode, pokes=()):
    s = "190 poke info_text_box message_field %d\n" % mode
    for p in pokes:
        s += "190 " + p + "\n"
    return s + "199 dump before " + KEYS + "\n"


def post(sink, extra=""):
    return "200 event %s ControlCollision\n210 dump after %s\n" % (sink, KEYS) + extra


def trap(score):
    return ("210 expect n_score == %d delta 199\n"
            "210 expect multiball == -1 delta 199\n"
            "200 expect sound %d\n"
            "200 expect sound %d\n"
            "200 expect text info_text_box display id 3010\n"
            "200 expect none sound %d\n200 expect none sound %d\n" % (score, S_PLUNGER, S_TABC10, S_TABC04, S_TABC51))


def draws(n):
    """n = None: no draw expectation. Mode 6: engine background draws over frames 200..210 measured by the control ts_watch1_mode6 (no rule draw) = 14,
    so a script with one rule draw expects 14 + 1 (n_rand_draws delta over the same frames)."""
    if n is None:
        return ""
    return "210 expect n_rand_draws == %d delta 199\n" % (14 + n)


# ---------------------------------------------------------------- PrisonSinkControl1 (a_sink15) 0x426f90
w("ts_prison1_trap", """D-TSINKS PrisonSinkControl1 (FT 0x426f90, listing 3004-3050, code 0x426fbc-0x427048), branch A: mode != 6, v_lite48.message_field != 0.
tabc10wave; MultiballCount - 1; v_lite48 reset-and-turn-on; plunger relaunch 0.96; ScorePart(caller, 0) = 100,000 (acceptance); text 3010;
UpdateTowerLocks (v_lite48 not flashing -> a_sink18 inactive, v_lite48.message_field 0). The tabc10 sound id is pinned from a run.""",
  pre(2, ["poke v_lite48 message_field 1", "poke a_sink18 active 1"])
  + post("a_sink15", trap(100000)
         + "210 expect v_lite48.light_on == 1\n210 expect v_lite48.message_field == 0\n210 expect a_sink18.active == 0\n"
         + "210 expect v_lite49.message_field == 0\n" + draws(None)))
w("ts_prison1_empty", """D-TSINKS PrisonSinkControl1 branch B (listing 3053-3075, code 0x42704b-0x42706f): mode != 6, v_lite48.message_field == 0:
a_sink18 reset timer (-1), tabc04wave (sound 24 in this DAT, D-TOWERS t_trip_prison). No score, no text, no multiball change, no draw;
the tower state is untouched (a_sink18.active stays as poked). """,
  pre(4, ["poke a_sink18 active 1"])
  + post("a_sink15", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n200 expect sound %d\n"
         "200 expect none text info_text_box display\n210 expect a_sink18.active == 1\n210 expect v_lite48.light_on == 0\n"
         "200 expect none sound %d\n200 expect none sound %d\n" % (S_TABC04, S_PLUNGER, S_TABC51) + draws(None)))
for sd, ix, on, off, r in ((1, 0, "v_lite65", "v_lite66", 41), (10000, 1, "v_lite66", "v_lite65", 32694)):
    w("ts_prison1_mode6_seed%d" % sd, """D-TSINKS PrisonSinkControl1 branch C (listing 3078-3121, code 0x427075-0x4270ef), mode 6: tabc51wave (sound 43) and ONE draw
RandIndex(2) = (r * 2) >> 15 (0x427087). `seed %d` runs in the frame of the event before it, so the sink's draw is the first: MSVC rand() after
srand(%d) is %d (independent calculation: h = h * 214013 + 2531011, r = (h >> 16) & 0x7fff) -> index %d -> %s flashes (code 7, 1.0 s)
and %s is not touched by this call."""
      % (sd, sd, r, ix, on, off),
      pre(6) + "200 seed %d\n" % sd
      + post("a_sink15", "200 expect sound %d\n200 expect n_score == 0 delta 199\n210 expect %s.flasher_on == 1\n"
             "210 expect %s.flasher_on == 0\n210 expect multiball == 0 delta 199\n" % (S_TABC51, on, off)
             + draws(1)))

# ---------------------------------------------------------------- PrisonSinkControl2 (a_sink18) 0x427100
w("ts_prison2_trap", """D-TSINKS PrisonSinkControl2 (FT 0x427100, listing 3129-3171, code 0x427146-0x4271e1), branch A: mode == 2, v_lite48.message_field != 0:
the same trap as PrisonSinkControl1 (100,000, text 3010, MultiballCount - 1, plunger relaunch, UpdateTowerLocks).""",
  pre(2, ["poke v_lite48 message_field 1", "poke a_sink18 active 1"])
  + post("a_sink18", trap(100000) + "210 expect v_lite48.light_on == 1\n210 expect v_lite48.message_field == 0\n210 expect a_sink18.active == 0\n" + draws(None)))
w("ts_prison2_update", """D-TSINKS PrisonSinkControl2 branch B (listing 3172-3190, code 0x4271e4-0x4271fd): mode == 2, v_lite48.message_field == 0:
a_sink18 reset timer (0.0), then UpdateTowerLocks (0x423be0). v_lite49 is flashing, so UpdateTowerLocks arms the Watch tower
(a_sink17.active 1, v_lite49.message_field 1, a_sink18.active 0). No score, no sound, no text.""",
  pre(2, ["msg v_lite49 TLightFlasherStartTimed -1", "poke a_sink18 active 1"])
  + post("a_sink18", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n210 expect a_sink17.active == 1\n"
         "210 expect v_lite49.message_field == 1\n210 expect a_sink18.active == 0\n200 expect none sound %d\n200 expect none text info_text_box display\n" % S_TABC04
         + draws(None)))
w("ts_prison2_other", """D-TSINKS PrisonSinkControl2 branch C (listing 3192-3214, code 0x427124-0x427140): mode not 2 and not 6: a_sink18 reset timer (-1), then
ResetTowerLocks (0x423b80): both tower sinks inactive and the three tower lights' message_field 0.""",
  pre(4, ["poke v_lite48 message_field 1", "poke v_lite49 message_field 1", "poke a_sink18 active 1", "poke a_sink17 active 1"])
  + post("a_sink18", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n210 expect a_sink18.active == 0\n"
         "210 expect a_sink17.active == 0\n210 expect v_lite48.message_field == 0\n210 expect v_lite49.message_field == 0\n"
         "200 expect none text info_text_box display\n" + draws(None)))
for sd, ix, on, off, r in ((1, 0, "v_lite65", "v_lite66", 41), (10000, 1, "v_lite66", "v_lite65", 32694)):
    w("ts_prison2_mode6_seed%d" % sd, """D-TSINKS PrisonSinkControl2 branch D (listing 3217-3267, code 0x427203-0x42727d), mode 6: as PrisonSinkControl1's mode 6:
tabc51wave, one draw RandIndex(2) (0x427215), seed %d -> rand %d -> index %d -> %s flashes, %s not."""
      % (sd, r, ix, on, off),
      pre(6) + "200 seed %d\n" % sd
      + post("a_sink18", "200 expect sound %d\n200 expect n_score == 0 delta 199\n210 expect %s.flasher_on == 1\n"
             "210 expect %s.flasher_on == 0\n210 expect multiball == 0 delta 199\n" % (S_TABC51, on, off) + draws(1)))

# ---------------------------------------------------------------- WatchSinkControl1 (a_sink13) 0x427290
w("ts_watch1_trap", """D-TSINKS WatchSinkControl1 (FT 0x427290, listing 3275-3321, code 0x4272de-0x427379), branch A: mode == 2, v_lite49.message_field != 0:
tabc10wave, MultiballCount - 1, v_lite49 reset-and-turn-on, plunger relaunch, ScorePart(caller, 0) = 200,000 (acceptance), text 3010,
UpdateTowerLocks (a_sink17.active 0, v_lite49.message_field 0).""",
  pre(2, ["poke v_lite49 message_field 1", "poke a_sink17 active 1"])
  + post("a_sink13", trap(200000) + "210 expect v_lite49.light_on == 1\n210 expect v_lite49.message_field == 0\n210 expect a_sink17.active == 0\n"
         "210 expect v_lite48.light_on == 0\n" + draws(None)))
w("ts_watch1_empty", """D-TSINKS WatchSinkControl1 branch B (listing 3323-3340, code 0x42737c-0x4273a3): mode == 2, v_lite49.message_field == 0:
tabc04wave and a_sink17 reset timer (-1). No score, text, multiball change; tower state untouched.""",
  pre(2, ["poke a_sink17 active 1"])
  + post("a_sink13", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n200 expect sound %d\n"
         "200 expect none text info_text_box display\n210 expect a_sink17.active == 1\n210 expect v_lite49.light_on == 0\n" % S_TABC04 + draws(None)))
w("ts_watch1_other", """D-TSINKS WatchSinkControl1 branch C (listing 3342-3360, code 0x4272b4-0x4272db): mode not 2 and not 6: tabc04wave, a_sink17 reset timer (-1).
No score, text, multiball change; tower state untouched.""",
  pre(4, ["poke v_lite49 message_field 1", "poke a_sink17 active 1"])
  + post("a_sink13", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n200 expect sound %d\n"
         "200 expect none text info_text_box display\n210 expect a_sink17.active == 1\n210 expect v_lite49.message_field == 1\n" % S_TABC04 + draws(None)))
w("ts_watch1_mode6", """D-TSINKS WatchSinkControl1 branch D (listing 3362-3375, code 0x4273a6-0x4273bd), mode 6: only a_sink17 reset timer (-1): no sound, no
score, no text, no draw, tower state untouched.""",
  pre(6, ["poke v_lite49 message_field 1", "poke a_sink17 active 1"])
  + post("a_sink13", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n200 expect none sound %d\n200 expect none sound %d\n"
         "200 expect none text info_text_box display\n210 expect a_sink17.active == 1\n210 expect v_lite49.message_field == 1\n" % (S_TABC04, S_TABC51) + draws(0)))

# ---------------------------------------------------------------- WatchSinkControl2 (a_sink17) 0x4273d0
w("ts_watch2_trap", """D-TSINKS WatchSinkControl2 (FT 0x4273d0, listing 3384-3445, code 0x427408-0x4274a3), branch B: mode == 2, v_lite49.message_field != 0:
the same trap as WatchSinkControl1 (200,000, text 3010, MultiballCount - 1, plunger relaunch, UpdateTowerLocks).""",
  pre(2, ["poke v_lite49 message_field 1", "poke a_sink17 active 1"])
  + post("a_sink17", trap(200000) + "210 expect v_lite49.light_on == 1\n210 expect v_lite49.message_field == 0\n210 expect a_sink17.active == 0\n" + draws(None)))
w("ts_watch2_update", """D-TSINKS WatchSinkControl2 branch C (listing 3447-3465, code 0x4274a6-0x4274bf): mode == 2, v_lite49.message_field == 0: a_sink17 reset
timer (0.0), UpdateTowerLocks. v_lite48 is flashing, so UpdateTowerLocks arms the prison-window tower (a_sink18.active 1, a_sink17.active 0,
v_lite48.message_field 1). No score, no sound, no text.""",
  pre(2, ["msg v_lite48 TLightFlasherStartTimed -1", "poke a_sink17 active 1"])
  + post("a_sink17", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n210 expect a_sink18.active == 1\n"
         "210 expect a_sink17.active == 0\n210 expect v_lite48.message_field == 1\n200 expect none sound %d\n200 expect none text info_text_box display\n" % S_TABC04
         + draws(None)))
w("ts_watch2_other", """D-TSINKS WatchSinkControl2 branch A (listing 3384-3403, code 0x4273e9-0x427402): mode != 2: a_sink17 reset timer (0.0), ResetTowerLocks:
both tower sinks inactive, the three tower lights' message_field 0. No score, no sound, no text.""",
  pre(4, ["poke v_lite48 message_field 1", "poke v_lite49 message_field 1", "poke a_sink18 active 1", "poke a_sink17 active 1"])
  + post("a_sink17", "210 expect n_score == 0 delta 199\n210 expect multiball == 0 delta 199\n210 expect a_sink18.active == 0\n"
         "210 expect a_sink17.active == 0\n210 expect v_lite48.message_field == 0\n210 expect v_lite49.message_field == 0\n"
         "200 expect none text info_text_box display\n" + draws(None)))
