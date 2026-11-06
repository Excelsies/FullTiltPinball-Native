# Native test hook scripts (`.uh`)

Reference for the script format, the commands and the output of the native test hook
(`SpaceCadetPinball/ft_test_hook.cpp`) and its runner `re/test/hookrun.py`. Overview and the
checks built on it: [docs/TESTING.md](../../../docs/TESTING.md).

## Where scripts live

| Path | Scripts |
|---|---|
| `re/test/hook_scripts/` | Engine part classes, Space Cadet smoke and seeded runs, rest-state scripts (`rest_<t>.uh`), and scripts that test the hook's own error handling (`sc_bad_*`, `sc_text_refused`) |
| `re/test/hook/dragon/<area>/` | Dragon's Keep rules, one folder per rules area |
| `re/test/hook/pirates/<area>/` | Skulduggery rules, one folder per rules area |

Some folders also hold a Python oracle that checks a run's output files (for example
`hook/dragon/D-SPELLS/check_spells.py`, `hook/pirates/P-BALL/count_check.py`,
`hook/pirates/P-MSEL/check_pm.py`) or a generator that writes the folder's scripts
(`gen_p_msel.py`, `hook/dragon/D-TSINKS/gen_scripts.py`). Oracles read only the run's output files.

## Running

The binary must be built with `-DFT_TEST_HOOK=ON` (see docs/TESTING.md). Then:

    python re/test/hookrun.py run --table cadet|dragon|pirates --bin <hook binary> --display <N> --out <dir> [--timeout S] <script.uh>
    python re/test/hookrun.py check <dir>          # re-evaluate the expect lines of a finished run
    python re/test/hookrun.py compare <dirA> <dirB> # byte comparison of two runs
    python re/test/hookrun.py from-json <harness script.json> --fixed-dt <ms> [--table t] [--startup-wait] [--late-players]

`run` resolves the script, starts the binary on a private Xvfb display through
`ftharness/session.py` (table data from `re/work/game/<table>/`, a fresh settings file, the window
resized to 960 x 666), waits for it to exit, then runs `check`. Exit code 0: every expectation passed
and the binary's exit code is the expected one; 1: a FAIL; 2: a run or resolution error.

`compare` compares `dumps.jsonl`, `timeline.jsonl`, `sounds.jsonl`, `text.jsonl` and every
`frames/*.bmp` byte for byte and prints PASS / FAIL per file.

## Script format

A text file of lines `<frame> <command> [args]` in ascending frame order. `#` starts a comment.
Header lines start with `init` and carry no frame.

A *tick* is one pass of the main loop after the table is loaded (and after the `init window` wait).
`frame` is the tick number, from 0. Commands of frame f run at the start of tick f, before the
physics step; `dump`, `shot` and the timeline are taken at its end. Ticks are counted while the
game is paused too, so a script can pause and resume. Script time `t` = frame x fixed_dt / 1000 s;
game time `gt` = the sum of the frame times actually passed to the game (it stops while paused).

### Header

| Line | Effect |
|---|---|
| `init fixed_dt <ms>` | every frame gets exactly `<ms>` of game time instead of the measured frame time |
| `init seed <n>` | seeds the random stream (`ft_rand::Seed`): the MSVC generator on Dragon's Keep and Skulduggery, `std::srand` on Space Cadet |
| `init window <w> <h>` | tick 0 waits until the renderer output is `<w>` x `<h>` (30 s limit). Scripts that take shots or are compared with Wine use `init window 960 666` |
| `init dump_period <ms>` | a `dumps.jsonl` line labelled `tick` every `<ms>` of script time (a multiple of fixed_dt). `50` matches the Wine probe's sampling |
| `init watch <key>...` | adds part-field keys to every dump and to the timeline; `@<file>` = every name in a watch/rest JSON file (expanded by hookrun); `*.<field>` = that field of every component that has it |
| `init startup_wait 1` | Dragon's Keep / Skulduggery only: runs the original's 2000 ms start-up wait in script time (2000 / fixed_dt frames that show the table and take dumps, then the first game starts). Needs a fixed_dt that divides 2000. Without it, frame 0 is already after the first game's start |
| `init expect_exit <code>` | the exit code `check` expects (default 0) |
| `init option Players <n>` | read by hookrun, not the binary: writes `Players` = n (1-4) into the run's settings file |

### Commands

| Command | Effect |
|---|---|
| `key down\|up <name>` | a key through the same functions the SDL key handler calls. Names: `left_flipper`, `right_flipper`, `plunger`, `bump_left`, `bump_right`, `bump_bottom`, `new_game`, `pause`; `demo` toggles demo mode |
| `type <chars>` | one character per tick, as typed keys (cheats, backdoor keys). Escapes: `\s` space, `\t` Tab, `\\` backslash |
| `event <part> <code>` | `control::handler(code, part)`: what a part reports to the table rules. `table` = `control::table_control_handler(code)` |
| `msg <part> <code> <value>` | `part->Message(code, value)`; `table` = the table's `Message` |
| `text <code>` | `control::TextEvent(code)`; refused (exit 3) on Space Cadet, which has no rules object |
| `poke <part> <field> <value>` | sets `message_field`, `active`, `light_on` (with the sprite update) or a field the class accepts through `HookSet` |
| `place <ball> <x> <y> <z> [<dx> <dy> <speed> [<hexmask>]]` | sets a ball's position (and direction, speed, collision mask) and activates it; 4, 7 or 8 arguments |
| `hold <ball> on\|off` | while on, the ball is put back to its last `place` values before each frame |
| `dump <label> [<key>...]` / `dump <label> @<file>` | one `dumps.jsonl` line with the standard keys, the watched keys and the listed ones |
| `shot <name>` | captures the frame to `frames/<name>.bmp` after rendering, before the present |
| `quit` | ends the run (exit code 0) |
| `<f1>[-<f2>] expect ...` | an expectation checked by hookrun after the run (stripped before the binary sees the script), see below |

`<part>` is a component's group name in the DAT. Codes may be written as a `MessageCode` member name
(parsed from `SpaceCadetPinball/TPinballComponent.h` of the tree that built the binary), as
`num:<n>`, or as `ft:<hex>` (a Full Tilt code translated through `re/work/spec/msgmap.json`, which
`re/ft_msgmap.py` writes); for `msg` an FT code names the receiving class, `ft:<hex>@<Class>`. The
resolved script is kept as `script.resolved.uh`.

A `?` before a key in `dump` or `init watch` marks a key that may not resolve: it is written as
`null` and noted in `hook.log`. Any other unknown command, bad argument or missing part ends the run
with exit code 3 and the reason in `hook.log`. A script without `quit` ends with exit code 4 after
its last frame + 1000. The cheat-mode keys of the main loop (g, o, y, F1, F10) cannot be reached
through `type` or `key`.

FIFO mode: with `FT_TEST_FIFO=<named pipe>` instead of a script, the same commands without frame
numbers are applied at the next tick. It is for interactive diagnosis only, not repeatable, and the
exit-4 rule does not apply (send `quit`).

### Expectations

    <frame> expect <key> == <v>           # also !=, ~= <v> <tol>, in <lo> <hi>
    <frame> expect <key> == <v> delta <f2> # the change since the end of frame <f2>
    <f1>-<f2> expect sound <id>
    <f1>-<f2> expect track <n>
    <f1>-<f2> expect text <box> display|clear [code <c>] [id <n>]
    <f1>-<f2> expect none sound|track|text ...

Values are the key's value at the end of the frame. Text is matched by text code and string id,
never by the text itself, so no game text appears in a script.

## Dump keys

| Keys | Source |
|---|---|
| `player_count`, `current_player`, `multiplier`, `jackpot`, `super_jackpot`, `ball_count`, `extra_balls`, `multiball`, `tilt_lock` | the table object; the same fields the Wine probe reads (`re/test/scripts/watch_<t>.json`) |
| rule keys (`mode`, `ball_save`, `drain_state`, ... and per-table ones) | `Rules->DumpState()`; absent on Space Cadet |
| `<part>.<field>` | one field of a part (only when watched or listed) |
| `n_frame`, `n_score`, `n_cheat`, `n_rand_draws`, `n_rand_last`, `n_active_balls`, `n_placed_balls`, `n_ball<i>_{x,y,z,dx,dy,vx,vy,speed,active,mask,placed}` | native-only keys, ignored when comparing with Wine |

Part fields: every component has `active` and `message_field`; lights have `light_on`,
`flasher_on`, `override`, `bitmap`; the plunger has `boost`, `max_pullback`, `pullback_started`;
classes add their own through `HookGet` (blocker state, ramp-exit groups, accelerator speeds,
light-sweep grid, diverter flash time, magnet hold-off, timer and kickback times, ...).

## Output files

One directory per run (`--out`):

| File | Content |
|---|---|
| `script.uh`, `script.resolved.uh` | the script as given and as the binary ran it |
| `dumps.jsonl` | one line per `dump` and per `dump_period` tick: `label`, `frame`, `t`, `gt` and the keys |
| `timeline.jsonl` | one line per frame in which a dumped key changed |
| `sounds.jsonl` | every sound played (`id`, `source`) and every music track started; logged even with audio off |
| `text.jsonl` | every text-box display and clear: box, text, time, font, the text code and the string ids fetched. The text exists only in these run directories |
| `frames/<name>.bmp` | `shot` captures |
| `hook.log` | errors and resolution notes, no wall-clock times |
| `run.json`, `target.log` | hookrun's session log and the binary's stdout/stderr |
| `expect.txt` | one PASS / FAIL line per expectation and for the exit code |

Floats are printed with `%.9g`, so equal values give equal text and two identical runs give
identical files.

## Scripts that do not run on the full tree

- The Dragon's Keep script in this folder whose header starts with `STUB-ONLY` needs a test-only
  stub of the rules object; on a build with the real rules it fails by construction. Its header
  describes the stub. Its checks are covered by the real-rules and flipper scripts for Dragon's Keep
  in this folder and by scripts under `hook/dragon/`.
- `hook/pirates/P-CORE/probe_*.uh` need test-only stubs of some Skulduggery control functions
  (`run_p_core.py --probe`); on the full tree they fail by construction. The functions they probe are
  covered by scripts under `hook/pirates/P-DROPS/` and `hook/pirates/P-CORE/` (`pc_launch.uh`,
  `pc_1max.uh`).
