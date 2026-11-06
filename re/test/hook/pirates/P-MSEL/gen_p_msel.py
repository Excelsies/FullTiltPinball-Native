#!/usr/bin/env python3
"""Generates the mode-select hook scripts of this directory (pm_target, pm_light, pm_start, pm_skusel; Skulduggery mode select).

Rerunnable:  python gen_p_msel.py [outdir]   (default: the directory of this file).
Expected values come from the port listing, never from a first run:
  - mode light i (0..7) = v_lite552..559 (the lights of the modes below, in the same order),
  - mode of index i = {4, 8, 7, 5, 6, 9, 10, 11} (table 0x43d0f0), selected_mode = i + 1,
  - the target a_spot102 shows text id 3068 + i (0xbfc.. in the original) and scores 50,000,
  - draws: the MSVC LCG (state*214013+2531011, bits 16..30), `seed s` makes the next draw's value
    r = ((s*214013+2531011) >> 16) & 0x7fff; the index is (r*n)>>15 over the n unlit lights (listing 2021-2109).
"""
import sys
from pathlib import Path

LIGHTS = [552, 553, 554, 555, 556, 557, 558, 559]
MODE = [4, 8, 7, 5, 6, 9, 10, 11]
WATCH = " ".join("v_lite%d.flasher_on v_lite%d.light_on" % (n, n) for n in LIGHTS)
WATCH += " v_lite607.flasher_on mode_timer.time_left_ms mode_timer.timer a_spot8.message_field"
HEAD = """init fixed_dt 10
init seed 1
init watch %s
10 key down new_game
11 key up new_game
""" % WATCH


def lcg(seed):
    return (((seed * 214013 + 2531011) & 0xffffffff) >> 16) & 0x7fff


def lcg_n(seed, n):
    """The n-th output after `seed s` (n = 1: the first draw)."""
    st = seed
    for _ in range(n):
        st = (st * 214013 + 2531011) & 0xffffffff
    return (st >> 16) & 0x7fff


def pause(f):
    """Toggle the pause: a paused tick runs no pb::frame, so the engine takes no draw and the counts below are the rules' own."""
    return ["%d key down pause" % f, "%d key up pause" % (f + 1)]


def reset(f, mode):
    """All mode lights, the map pieces and the selection cleared; the info box mode and the timer set."""
    out = ["%d msg v_lite%d TLightResetAndTurnOff 0" % (f, n) for n in LIGHTS]
    out += ["%d msg v_lite607 TLightResetAndTurnOff 0" % f,
            "%d msg map_piece_light_group TLightResetAndTurnOff 0" % f,
            "%d msg mode_timer TTimerResetTimer -1.0" % f,
            "%d poke mode_timer message_field 0" % f,
            "%d poke info_text_box message_field %d" % (f, mode)]
    return out


def lit(f, idxs):
    return ["%d poke v_lite%d light_on 1" % (f, LIGHTS[i]) for i in idxs]


def target():
    """ModeSelectTargetControl (0x4273b0) and SelectNextMode (0x425850) through event a_spot102 ControlCollision."""
    o = ["# P-MSEL: ModeSelectTargetControl + SelectNextMode + SetModeLight/SelectModeIndex/ClearSelectedModeLight",
         "# (listing 3107-3174, 2021-2109, 1919-2020). P1 prologue, then one scenario per 40 frames from frame 200: reset at F, seed + event at F+1.",
         "# Expected values: see gen_p_msel.py (generated; rerun it instead of editing). Pending stubs on the path:",
         "# Mode2IdleController (P-BALL) and the other Mode<n>Controller of MissionControlSwitch: neutral, no checked effect.",
         "# D32 reconciliation (SR-P3c): S13 enters mode 6 through StartMode6 (NMAX with selected index 5) instead of poking the info box:",
         "# the poke left Mode6Controller without its level text (set by its ControlMissionComplete, pirates_modes_a.cpp:414-417) and",
         "# the merged ControlMissionStarted branch aborted at its defensive Fatal (pirates_modes_a.cpp:426-427)."]
    o.append(HEAD.rstrip("\n"))
    o.append("100 type hidden\\ttest")   # the NMAX cheat of S13 (mode 6 through the real StartMode6)
    o.append("# paused from frame 190 (no engine draws in a paused tick); unpaused again for the timer scenarios S12-S18")
    o.extend(pause(190))
    f = 200
    sc = []   # (name, setup lines, seed, draws, selected index or None (unchanged), text index)

    def scen(name, lit_idx, seed, draws, sel, text_idx, extra_setup=(), pre_sel=0, post=()):
        nonlocal f
        o.append("# %s" % name)
        o.extend(reset(f, 2))
        o.extend(lit(f, lit_idx))
        o.extend("%d %s" % (f, s) for s in extra_setup)
        if pre_sel:
            o.append("%d poke mode_timer message_field %d" % (f, pre_sel))
        o.append("%d seed %d" % (f + 1, seed))
        o.append("%d event a_spot102 ControlCollision" % (f + 1))
        e = f + 1
        o.append("%d expect n_rand_draws == %d delta %d" % (e, draws, f))
        o.append("%d expect selected_mode == %d" % (e, sel))
        o.append("%d expect n_score == 50000 delta %d" % (e, f))
        o.append("%d expect v_lite607.flasher_on == 1" % e)
        o.append("%d expect text info_text_box display id %d" % (e, 3068 + text_idx))
        for n in post:
            o.append("%d expect %s" % (e, n))
        f += 40
        return e

    # S1-S4: nothing lit: n = 7 candidates in index order; r from the seed picks the index
    for name, seed, idx in (("S1 all unlit, seed 1 -> index 0", 1, 0), ("S2 all unlit -> index 1", 987654321, 1),
                            ("S3 all unlit -> index 4", 4000000, 4), ("S4 all unlit -> index 6", 20000, 6)):
        r = lcg(seed)
        assert (r * 7) >> 15 == idx, (name, r)
        scen(name + " (r=%d)" % r, [], seed, 1, idx + 1, idx,
             post=["v_lite%d.flasher_on == 1" % LIGHTS[idx]])
    # S5: lit 552, 554 -> candidates [1,3,4,5,6]; seed 4000000: r=20691 -> (r*5)>>15 = 3 -> candidate 5
    r = lcg(4000000)
    assert (r * 5) >> 15 == 3
    scen("S5 lit 0,2 -> candidates [1,3,4,5,6], index 3 -> 5 (r=%d)" % r, [0, 2], 4000000, 1, 6, 5,
         post=["v_lite%d.flasher_on == 1" % LIGHTS[5]])
    # S6: only index 2 unlit -> a single candidate (one draw is still taken)
    scen("S6 only index 2 unlit -> 2", [0, 1, 3, 4, 5, 6], 1, 1, 3, 2, post=["v_lite554.flasher_on == 1"])
    # S7: all seven lit, none flashing -> index 7, no draw
    scen("S7 all lit, none flashing -> 7, no draw", [0, 1, 2, 3, 4, 5, 6], 1, 0, 8, 7, post=["v_lite559.flasher_on == 1"])
    # S8: all seven lit, index 3 flashing -> returns 3 and leaves the selection (poked 7) alone
    scen("S8 all lit, 555 flashing -> returns 3, selection untouched (7)", [0, 1, 2, 4, 5, 6], 1, 0, 7, 3,
         extra_setup=["msg v_lite555 TLightFlasherStart 0"], pre_sel=7, post=["v_lite559.flasher_on == 0"])
    # S9: every map piece lit -> index 7, no draw
    scen("S9 map pieces complete -> 7, no draw", [], 1, 0, 8, 7, extra_setup=["msg map_piece_light_group TLightTurnOn 0"],
         post=["v_lite559.flasher_on == 1"])
    # modes other than 2: light + score only
    for name, mode in (("S10 mode 3: no selection, no text", 3), ("S11 mode 12: no selection, no text", 12)):
        o.append("# " + name)
        o.extend(reset(f, mode))
        o.append("%d poke mode_timer message_field 4" % f)
        o.append("%d event a_spot102 ControlCollision" % (f + 1))
        e = f + 1
        o.append("%d expect selected_mode == 4" % e)
        o.append("%d expect n_score == 50000 delta %d" % (e, f))
        o.append("%d expect v_lite607.flasher_on == 1" % e)
        o.append("%d expect n_rand_draws == 0 delta %d" % (e, f))
        o.append("%d expect none text info_text_box display" % e)
        f += 40
    # the time extension in modes 4..11 (listing 3150-3166): left + 10 s; unchanged outside
    f = (f // 100 + 1) * 100
    o.extend(pause(f))
    f += 20
    for name, mode, grows in (("S12 mode 4 (lower bound)", 4, True), ("S13 mode 6", 6, True), ("S14 mode 11 (upper bound)", 11, True),
                              ("S15 mode 3 (below)", 3, False), ("S16 mode 12 (above)", 12, False), ("S17 mode 2 (select)", 2, False)):
        if mode == 6:
            # Mode 6 is entered through the real start: the selected index 5 (mode_timer's MessageField) and the NMAX cheat in
            # mode 2 run StartSelectedMode -> StartMode6 -> StartMode(6) -> Mode6Controller(ControlMissionComplete), which
            # sets the level text (pirates_modes_a.cpp:414-417). The 5.0 s timer is set after the start, 10 frames
            # later, so the 50-frame distance to the event is the same as in the other scenarios.
            o.append("# %s: entered through StartMode6 (NMAX with selected index 5), 5.0 s timer set at F+11, event at F+61 (4.5 s left)" % name)
            o.extend(reset(f, 2))
            o.append("%d poke mode_timer message_field 5" % f)
            for k, ch in enumerate("NMAX"):
                o.append("%d type %s" % (f + 1 + 2 * k, ch))
            o.append("%d expect mode == 6" % (f + 10))
            o.append("%d msg mode_timer TTimerResetTimer 5.0" % (f + 11))
            e = f + 61
            o.append("%d event a_spot102 ControlCollision" % e)
            o.append("%d expect mode_timer.time_left_ms in 14440 14510" % e)
            o.append("%d expect n_score == 50000 delta %d" % (e, f + 60))
            f += 100
            continue
        o.append("# %s: 5.0 s timer set at F, event at F+50 (4.5 s left)" % name)
        o.extend(reset(f, mode))
        o.append("%d msg mode_timer TTimerResetTimer 5.0" % (f + 1))
        o.append("%d event a_spot102 ControlCollision" % (f + 51))
        e = f + 51
        if grows:
            o.append("%d expect mode_timer.time_left_ms in 14440 14510" % e)
        else:
            o.append("%d expect mode_timer.time_left_ms in 4400 4510" % e)
        o.append("%d expect n_score == 50000 delta %d" % (e, f + 50))
        f += 100
    o.append("# S18 mode 5 with no timer running: nothing is started")
    o.extend(reset(f, 5))
    o.append("%d event a_spot102 ControlCollision" % (f + 1))
    o.append("%d expect mode_timer.timer == 0" % (f + 1))
    o.append("%d expect mode_timer.time_left_ms == 0" % (f + 1))
    o.append("%d expect n_score == 50000 delta %d" % (f + 1, f))
    o.append("%d quit" % (f + 20))
    return "\n".join(o) + "\n"


def lightscript():
    """SetModeLight on/off + ClearSelectedModeLight + SelectNextMode through MissionControl (MissionControlSwitch)."""
    o = ["# P-MSEL: SetModeLight (0x4255a0), ClearSelectedModeLight (0x425700), SelectModeIndex (0x4256d0), SelectNextMode via",
         "# MissionControlSwitch (P-G1): ControlMissionComplete -> SetModeLight(mode, 1) (flasher on, selection cleared),",
         "# ControlModeEnd -> +750000 jackpot (+20000 pre-hook), SetModeLight(mode, 0) (light steady), SelectNextMode, mode 2 and",
         "# SetModeLight(2, 1) -> SelectNextMode again (two draws; the final selection comes from the second).",
         "# Generated by gen_p_msel.py."]
    o.append(HEAD.rstrip("\n"))
    o.append("# paused from frame 190: the draw counts are the rules' own; `seed` before ControlModeEnd fixes both draws r1, r2")
    o.extend(pause(190))
    f = 200
    for j, m in enumerate(MODE):
        idx = MODE.index(m)
        sel = (idx + 3) % 8            # an unrelated selected index whose light is flashing before the call
        o.append("# mode %d (light v_lite%d); stale selection index %d (v_lite%d flashing)" % (m, LIGHTS[idx], sel, LIGHTS[sel]))
        o.extend(reset(f, m))
        o.append("%d poke mode_timer message_field %d" % (f, sel + 1))
        o.append("%d msg v_lite%d TLightFlasherStartTimed -1.0" % (f, LIGHTS[sel]))
        o.append("%d event a_spot102 ControlMissionComplete" % (f + 1))
        o.append("%d expect v_lite%d.flasher_on == 1" % (f + 1, LIGHTS[idx]))
        o.append("%d expect v_lite%d.flasher_on == 0" % (f + 1, LIGHTS[sel]))
        o.append("%d expect selected_mode == 0" % (f + 1))
        o.append("%d expect mode == %d" % (f + 1, m))
        seed = 7919 * (j + 1) * 1000003 % 2000000011
        cand = [i for i in range(7) if i != idx]
        r2 = lcg_n(seed, 2)
        want = cand[(r2 * len(cand)) >> 15] + 1
        o.append("# seed %d: r1=%d r2=%d, %d candidates -> index %d" % (seed, lcg_n(seed, 1), r2, len(cand), want - 1))
        o.append("%d seed %d" % (f + 20, seed))
        o.append("%d event a_spot102 ControlModeEnd" % (f + 20))
        o.append("%d expect selected_mode == %d" % (f + 20, want))
        o.append("%d expect mode == 2" % (f + 20))
        o.append("%d expect jackpot == 770000 delta %d" % (f + 20, f + 19))
        o.append("%d expect v_lite%d.light_on == 1" % (f + 20, LIGHTS[idx]))
        o.append("%d expect v_lite%d.flasher_on == 0" % (f + 20, LIGHTS[idx]))
        o.append("%d expect n_rand_draws == 2 delta %d" % (f + 20, f + 19))
        f += 50
    # modes without a light: the selection is cleared by SetModeLight(13, 1) ...
    # Mode13Controller (pirates_brig.cpp:186-196) with no locked ball sets mode 2
    # and sends ControlMissionComplete again (MissionControl, pirates_rules.cpp:204-208): SetModeLight(2, 1) and SelectNextMode choose the
    # next mode at once. The draw is pinned by a `seed` line before the event (not the engine's
    # running RNG, which would be a first-run value): r = lcg(seed), index = (r*7)>>15 over the 7 unlit candidates (SelectModeIndex),
    # selected_mode = index + 1 and exactly that light flashes; the 7 other mode lights must stay off.
    o.append("# mode 13 (no light) with no locked ball: mode 13 returns to mode 2 at once and the next mode is selected (merged P-BRIG, pirates_brig.cpp:186-196)")
    o.extend(reset(f, 13))
    o.append("%d poke mode_timer message_field 3" % f)
    o.append("%d msg v_lite554 TLightFlasherStartTimed -1.0" % f)
    seed13 = 4000000
    idx13 = (lcg(seed13) * 7) >> 15
    assert idx13 == 4, idx13
    o.append("# seed %d: r=%d, 7 candidates -> index %d (v_lite%d)" % (seed13, lcg(seed13), idx13, LIGHTS[idx13]))
    o.append("%d seed %d" % (f + 1, seed13))
    o.append("%d event a_spot102 ControlMissionComplete" % (f + 1))
    o.append("%d expect mode == 2" % (f + 1))
    o.append("%d expect selected_mode == %d" % (f + 1, idx13 + 1))
    for n in LIGHTS:
        o.append("%d expect v_lite%d.flasher_on == %d" % (f + 1, n, 1 if n == LIGHTS[idx13] else 0))
    o.append("%d quit" % (f + 20))
    return "\n".join(o) + "\n"


def start():
    """StartSelectedMode (0x425a50) and Start<Mode> (0x423480..0x4235f0) through the NMAX cheat (BackdoorKey, mode 2)."""
    o = ["# P-MSEL: StartSelectedMode + the eight Start<Mode> (listing 508-617, 2110-2146) through NMAX in mode 2 (P-G1 BackdoorKey).",
         "# Per selected index k: mode = {4,8,7,5,6,9,10,11}[k-1], the selection is cleared (SetModeLight(mode, 1)), that mode's light",
         "# flashes, no_hold_start = 1; mode 10 also clears a_spot8's MessageField (poked to 5 first). The glyphs of modes 4, 8, 7",
         "# and 5 and their time / font are checked by check_pm.py (start). ModeRunningLightOn (P-CORE) is a",
         "# pending stub here: neutral. Generated by gen_p_msel.py."]
    o.append(HEAD.rstrip("\n"))
    o.append("100 type hidden\\ttest")
    f = 200
    for k in range(1, 9):
        m = MODE[k - 1]
        o.append("# selected index %d -> mode %d" % (k, m))
        o.extend(reset(f, 2))
        o.append("%d poke mode_timer message_field %d" % (f, k))
        o.append("%d poke a_spot8 message_field 5" % f)
        for c, ch in enumerate("NMAX"):
            o.append("%d type %s" % (f + 1 + 2 * c, ch))
        e = f + 12
        o.append("%d expect mode == %d" % (e, m))
        o.append("%d expect selected_mode == 0" % e)
        o.append("%d expect no_hold_start == 1" % e)
        o.append("%d expect v_lite%d.flasher_on == 1" % (e, LIGHTS[MODE.index(m)]))
        o.append("%d expect a_spot8.message_field == %d" % (e, 0 if m == 10 else 5))
        o.append("%d dump start_%d" % (e, k))
        f += 40
    o.append("%d quit" % (f + 20))
    return "\n".join(o) + "\n"


def skusel():
    """CheckSkulduggeryStart (0x425de0). Its callers are the drop-target banks (pirates_drop_banks.cpp)."""
    o = ["# P-MSEL: CheckSkulduggeryStart (listing 2277-2296). Merged tree: the caller is the real LduggTargetControl1",
         "# (P-DROPS, pirates_drop_banks.cpp:201), no test-only edit. Event on a_drop5 (type 1006: the pre-hook adds targets_hit 1 / jackpot 20000).",
         "# letters = v_lite1.MessageField. (letters & 0x7ff) != 0x7ff: nothing; == 0x7ff (also with higher bits set): no_hold_start = 1",
         "# and StartSelectedMode runs (mode 8 for selected index 2).",
         "# D32 reconciliation (SR-P3c): no_hold_start is 1 from frame 0 (Mode3LaunchController, pirates_ball.cpp:251, P-BALLr); the first scenario",
         "# clears it through the real Mode2IdleController (pirates_ball.cpp:211) before checking that a 10-of-11 hit leaves it 0.",
         "# Generated by gen_p_msel.py."]
    o.append(HEAD.rstrip("\n"))
    f = 200
    for name, letters, start_ in (("10 of 11 letters", 2046, False), ("all 11 letters", 2047, True), ("all 11 and bit 11", 4095, True),
                                  ("none", 0, False)):
        o.append("# " + name)
        if letters == 2046:
            # no_hold_start is 1 from frame 0 (Mode3LaunchController, ControlMissionComplete,
            # pirates_ball.cpp:251), so the "not set by a 10-of-11 hit" check needs its own pre-state: mode 2 and the real
            # ControlMissionComplete run Mode2IdleController (0x42ab90, pirates_ball.cpp:211), which clears it.
            o.append("%d poke info_text_box message_field 2" % (f - 3))
            o.append("%d expect no_hold_start == 1" % (f - 3))
            o.append("%d event a_spot102 ControlMissionComplete" % (f - 2))
            o.append("%d expect no_hold_start == 0" % (f - 1))
        o.extend(reset(f, 2))
        o.append("%d poke mode_timer message_field 2" % f)
        o.append("%d poke v_lite1 message_field %d" % (f, letters))
        o.append("%d poke info_text_box message_field 2" % f)
        o.append("%d event a_drop5 ControlCollision" % (f + 1))
        e = f + 5
        if start_:
            o.append("%d expect mode == 8" % e)
            o.append("%d expect no_hold_start == 1" % e)
            o.append("%d expect selected_mode == 0" % e)
            o.append("%d expect v_lite553.flasher_on == 1" % e)
        else:
            o.append("%d expect mode == 2" % e)
            o.append("%d expect selected_mode == 2" % e)
            o.append("%d expect v_lite553.flasher_on == 0" % e)
            if letters == 2046:
                o.append("%d expect no_hold_start == 0" % e)
        f += 40
    o.append("%d quit" % (f + 20))
    return "\n".join(o) + "\n"


if __name__ == "__main__":
    d = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    for name, text in (("pm_target.uh", target()), ("pm_light.uh", lightscript()), ("pm_start.uh", start()),
                       ("pm_skusel.uh", skusel())):
        (d / name).write_text(text)
        print("wrote", d / name)
