# Wine vs native test harness

Runs the original Full Tilt! executables under Wine and the native port on private Xvfb displays,
drives both with the same input script, takes screenshots at named markers, reads the originals'
rule state from memory, and compares the two sides. The native test hook and its `.uh` scripts are
described in [hook_scripts/README.md](hook_scripts/README.md); the overview of all checks is
[docs/TESTING.md](../../docs/TESTING.md).

No game data lives here. The tables are copied from the user's Full Tilt folder into
`re/work/game/<table>/` (git-ignored); runs go to directories under `re/work/`.

## Requirements

`Xvfb`, `xdotool`, ImageMagick 7 (`magick`, `import`), `wine`, Python 3.12 (stdlib only), and
`i686-w64-mingw32-gcc` for the Wine-side probe tools. The native binary defaults to
`bin/SpaceCadetPinball`.

## One-time setup

```sh
mkdir -p re/work/game
cp -r "$FULLTILT_DIR"/{CADET,DRAGON,PIRATE} re/work/game/   # FULLTILT_DIR = your Full Tilt! install folder; copy, never move
python re/test/harness.py init-prefix        # WINEPREFIX=re/work/wine-prefix, Wine audio off
FT_WINEPREFIX_AUDIO=<private dir> python re/test/harness.py init-audio-prefix  # optional: Wine audio on a null sink
```

The Wine-side tools are built with mingw into the tools directory that
`scripts/probe_run.py` names (`FTPEEK`, `FTCALL`); each `.c` file's header has its build line:

| Tool | Source | What it does |
|---|---|---|
| `ftpeek.exe` | `scripts/ftpeek.c` | Read-only sampler: reads 32-bit values (address chains) from the game process every N ms and prints changes |
| `ftpoke.exe` | `scripts/ftpoke.c` | Writes 32-bit values into the game process and reads them back (diagnosis only) |
| `ftcall.exe` | `scripts/ftcall.c` | Runs one stdcall function of the game in its process through a remote thread (diagnosis only) |

They run inside the same Wine prefix as the game: the Wine game process is re-parented to systemd,
so with `kernel.yama.ptrace_scope = 1` a Linux-side reader may not open its memory, while
`ReadProcessMemory` inside Wine works.

## harness.py

| Command | What it does |
|---|---|
| `harness.py start <target> [--display 90] [--hold S]` | fresh Xvfb (1024x768x24), launches the target, waits for its window, moves it to 0,0, focuses it, prints the window id; keeps it up for `--hold` seconds (0 = until Ctrl-C / SIGTERM), then tears down |
| `harness.py run <target> <script.json> [--display N] [--out DIR] [--settle S] [--keep-registry]` | start + replay + screenshots + teardown in one process; writes `run.json` (events, scheduled and actual step times), `shots/<marker>.png`, `shots/<marker>.<region>.png`, `target.log`, `xdotool.log` |
| `harness.py compare <runA> <runB> [--region table\|upper\|lower\|panel\|score] [--align N] [--fuzz 8%]` | per marker: crops both shots to the region, searches +-N px for the best offset, writes `<marker>.side.png` (A, B, difference) and `report.md` / `report.json` |
| `harness.py stop --display N [--audio]` | kills a leftover Xvfb on N and the prefix's wineserver |

Example (Space Cadet, Wine vs native):

```sh
python re/test/harness.py run wine-cadet   re/test/scripts/smoke.json --display 90 --out re/work/harness_runs/w
python re/test/harness.py run native-cadet re/test/scripts/smoke.json --display 91 --out re/work/harness_runs/n
python re/test/harness.py compare re/work/harness_runs/w re/work/harness_runs/n --region upper
```

### Targets

`targets.json` defines `wine-cadet`, `wine-dragon`, `wine-pirates`, `native-cadet`,
`native-dragon`, `native-pirates`, and the audio-on pairs `wine-dragon-audio`,
`wine-pirates-audio`, `native-dragon-audio`, `native-pirates-audio`. Each target gives the exe or
arguments, the data folder, the window title, the window size, the settings written for a native
run, and the screen regions (`table`, `upper`, `lower`, `panel`, `score`) used by `compare`.

`_displays` in `targets.json` is the display range the harness may use: `:90` to `:139`. It is read
by `ftharness/session.py`, so `harness.py`, `probe_run.py` and `hookrun.py` all refuse a display
outside it, and a display whose X lock file already exists.

`report_only` on a target lists markers that `compare` reports but does not judge. The audio-on
Skulduggery pair has `loaded` there: at that shot the original's own start-up light show is still
running and differs from run to run (six audio-on Wine runs against each other: 0.53-0.84 % upper,
0.44-1.32 % lower).

### Environment seams

Read when the module is imported. A variable that is set but names a missing path is an error.

- `FT_NATIVE_BIN=<path>`: the native binary for `native-*` targets (default `bin/SpaceCadetPinball`).
- `FT_WINEPREFIX=<dir>`: the Wine prefix for `wine-*` targets (default `re/work/wine-prefix`). A
  private copy (`cp -a`) lets two Wine runs use different displays at once.
- `FT_WINEPREFIX_AUDIO=<dir>`: the private prefix of the `wine-*-audio` targets. Required, no
  default: whoever sets it creates it with `init-audio-prefix` and deletes it afterwards. A run in a
  prefix whose audio driver is not `alsa`, or in the shared prefix, is an error.

`run.json` records what ran: the `launched` event has `binary` (native) or `prefix` (Wine).

### Input scripts (JSON)

```json
{"name": "smoke", "steps": [
  {"wait": 3.0}, {"shot": "start", "crops": ["score"]},
  {"wait": 0.2}, {"tap": "new_game"},
  {"down": "plunger"}, {"wait": 1.0}, {"up": "plunger"},
  {"tap": "left_flipper", "hold": 0.25},
  {"burst": "flip", "count": 5, "interval": 0.1}
]}
```

Steps run on an absolute schedule from the start of the replay. Each step has exactly one of
`wait`, `tap` (down, `hold` s, default 0.08, up), `down`, `up`, `shot` (optional `crops`), `burst`
(numbered shots). Key names are logical names or any X keysym:

| Name | Key |
|---|---|
| `left_flipper` / `right_flipper` | `z` / `slash` |
| `plunger` | `space` |
| `bump_left` / `bump_right` / `bump_bottom` | `x` / `period` / `Up` |
| `new_game` / `pause` / `fullscreen` / `player_controls` | `F2` / `F3` / `F4` / `F8` |
| `select_table` / `sounds` / `music` | `ctrl+t` / `ctrl+s` / `ctrl+m` (the originals' menu accelerators) |

A screenshot (`import -window`) blocks the replay for about 50-80 ms, so keep shots at least 0.2 s
away from key events; region crops are cut after the replay. `hookrun.py from-json` converts these
scripts to `.uh` scripts with the same schedule.

Scripts in `scripts/`: `smoke.json`, `first_frame.json` (markers `loaded`, `new_game`,
`ball_launched`), `first_frame_late.json` (plunger pressed 10 s after the new game, on a resting
ball), `ft_lifecycle.json`, `ft_two_player.json`, `ft_tilt_pause.json`, `newgame_*.json` and
`startup_burst.json` (light-show timing), `rest_check.json` (rest-state reads), and the Wine-side
diagnosis scripts that the generator `scripts/make_*_scripts.py` writes.

## probe_run.py: rule state of the originals

```sh
python re/test/scripts/probe_run.py wine-dragon <script.json> --watch re/test/scripts/watch_dragon.json \
    --display 90 --out DIR [--period-ms 50] [--anchor placed|window] [--window-poll S] [--rest-watch FILE]
```

Same script format as above, plus these steps:

| Step | Effect |
|---|---|
| `{"peek": "label"}` | log the latest sample of every watch value to `peeks.jsonl` |
| `{"rootshot": "name"}` | screenshot of the whole X screen (dialogs are separate windows) |
| `{"windows": "label"}` | the visible X windows (id, name, geometry) to `windows.jsonl`; shows a modal dialog of the original, such as its high-score name entry |
| `{"restpeek": "label", "watch": "<rest file>"}` | read every value of a typed rest file now (to `rest.jsonl`) |
| `{"call": "label", "func": "<hex>", "code": "<hex>", "part": "<chain>"}` | run one function of the original in its process (`ftcall.exe`); writes to the game, diagnosis only |

`ftpeek.exe` samples the watch values every `--period-ms` and logs every change to
`timeline.jsonl`. Watch files (`scripts/watch_<t>.json`) hold only addresses and offsets:

```json
{"table_global": "0x439b00",
 "values": [
   {"name": "mode", "tag": "0x439ce8", "field": 6},
   {"name": "multiball", "table": "0x192"},
   {"name": "ball_save", "addr": "0x434d2c"},
   {"name": "x", "chain": "43ad3c:5e:4:0"}]}
```

They read the same table fields as the native hook's dump keys; `selftest_watch_keys.py` checks
that the shared keys sit at the same offsets in both watch files.

**Script anchor.** With the default `--anchor placed`, script time 0 is when the window has been
found, moved, focused and settled, about 0.31 s after it appeared. With `--anchor window`, time 0 is
the moment the window search succeeded (accurate to `--window-poll`, e.g. 0.02). The original's
2000 ms start-up wait begins when its window appears, so with `--anchor window` the original's first
game starts at script time 2.0 s, the same time as a native hook script with
`init startup_wait 1`. Use `--anchor window` for every Wine run compared with such a script; the
same JSON then drives both sides unchanged (`hookrun.py from-json <json> --fixed-dt 10
--startup-wait`). A step scheduled before the window is focused is refused.

**Rest state.** `scripts/make_rest_watch.py <dragon|pirates> <control table dump>` writes
`scripts/rest_<t>.json` (every control tag with the fields of its class, typed) and
`hook_scripts/rest_<t>.uh` (native dumps `rest_1..3` at frames 300 / 350 / 400).
`scripts/rest_check.json` takes three Wine reads 0.5 s apart after 3 s.

## Wine vs native comparison (ftharness/wine_compare.py)

Also reached as `hookrun.py compare-wine` and `hookrun.py rest-state`. Exit 0 = at least one key
assessed and none failed, 1 = a FAIL, 2 = input error.

| Command | What it does |
|---|---|
| `compare-wine <native dumps.jsonl> <wine timeline.jsonl> [--keys k,..] [--order-only k,..] [--types watch.json] [--anchor key] [--tol-ms N] [--period-ms 50] [--out report.json]` | per key: the value sequences must be equal (order) and the gaps between transitions must agree within the tolerance (timing); also the first transition's delay from the anchor key (default `player_count`) and the order of events across keys. Prints `PASS`, `FAIL <reason>` or `NOT ASSESSED <reason>` per key |
| `rest-state <native dumps.jsonl> <wine rest.jsonl> [--label rest] [--types rest.json] [--exclude file]` | compares the `<label>_1.._3` samples; animated keys and excluded parts are NOT ASSESSED, an unreadable Wine value is a FAIL |
| `perturb <timeline.jsonl> <out> (--drop KEY N \| --delay KEY N MS \| --set KEY VALUE)` | a deterministic perturbed copy, for the self-tests |

`scripts/compare_*.py <native dumps.jsonl> <wine peeks.jsonl>` compares Wine `peek` rows with the
native dump rows nearest in time (used for the state before the first game starts).

Time mapping: Wine `t` = wall-clock seconds since the replay started (the original's game clock
runs on `timeGetTime()` and stops while paused); native `t` = frame x fixed_dt (script time, which
runs on while paused). Both follow the same schedule, so pauses line up. Both sides sample every
50 ms, so a transition is quantised on both sides; the default tolerance is 2 x period + 50 =
150 ms. Whether the original's clock keeps pace with the wall clock under Xvfb is not measured; if it
lagged, the deltas would grow with run length.

## How the targets are set up

- **Start-up.** All three executables go straight to the table window (no table-select dialog; the
  targets match the title by `^Full Tilt!`) and wait in a game for the launch. The window opens at
  x = -479 on a 1024x768 screen; with no window manager the harness moves it to 0,0. The table runs at
  the 1024x768 resolution: a 960x666 game area plus a 19 px menu bar drawn by Wine.
- **Registry.** `HKCU\Software\Cinematronics` is deleted before each Wine run (settings, last
  table, high scores); `--keep-registry` keeps it.
- **Native.** Run from the table's data folder with `-sw -noaudio` (and `-table=<t>` for the Full
  Tilt tables), with a fresh `XDG_DATA_HOME` per run whose `imgui_pb.ini` sets
  `Screen Resolution=2`, `ShowMenu=0`, `Linear Filtering=0`, sound and music off. Linear filtering is
  off because it blurs the 1:1 image.
- **Wayland.** `WAYLAND_DISPLAY` is removed and `SDL_VIDEODRIVER=x11` set for every child process;
  otherwise SDL opens the native window on the user's desktop instead of Xvfb.
- **Native resize.** Without a window manager the native window keeps its first layout (drawn for
  800x556) although X reports 960x666, so the harness resizes it from outside (961x667, then
  960x666) and the port lays out again at 960x666. Skipping the resize leaves the 800x556 layout
  with black margins for the whole run. For hook runs the resize is placed between shots: the hook
  build presents about 120 frames per second in real time whatever `init fixed_dt` says, so
  `Session._wait_nudge_gap` waits until the next missing shot is at least 0.4 s away (counted from
  the last shot file seen, or from the spawn), and `hookrun` reports a run as not assessable (exit 2)
  if a shot file was written during the resize. `run.json` records `shot_times`. A shot taken before
  the resize shows the pre-resize layout, which is the same in every run, so runs stay
  byte-comparable. `harness.py run` resizes at once. Self-test:
  `selftest_window_size.py --bin <hook binary> --display N`.
- **Input.** One long-lived `xdotool -` process reads key commands from a pipe (XTEST to the focused
  window); measured lateness against the schedule is at most 0.12 ms.
- **Teardown.** `Session.close()` runs from `with` / `finally` and on SIGINT / SIGTERM:
  `wineserver -k` for the prefix, kill the target's and Xvfb's process groups, `wineserver -w`. An
  error or a SIGTERM mid-run leaves no Xvfb, Wine or game process and no X lock file.

## Audio

- **Audio off changes the original's timing.** `init-prefix` sets Wine's audio driver to none.
  Without a sound device the original creates no DirectSound object, loads no waves and stores a
  duration of 0 for every sound, so every timer driven by a sound's duration (the new-game light
  show, the ball feed about 1 s after F2) is 0. The port does the same on the Full Tilt tables: a
  sound whose wave did not load (as with `-noaudio`) has duration 0, so the default audio-off pairs
  share this timing. Space Cadet keeps upstream's durations from the WAV headers. The audio-on pairs
  below test the timing with sound.
- **Audio-on targets.** `wine-*-audio` run in the private audio prefix with Wine's `alsa` driver,
  `ALSA_CONFIG_PATH=re/test/audio/asound_null.conf` (the only PCM is ALSA's `null` plugin) and
  `PULSE_SERVER` pointed at a missing socket. `native-*-audio` drop `-noaudio` and set
  `SDL_AUDIODRIVER=dummy`, `Sounds=1`, `Music=1`. Nothing is audible and no system audio setting,
  service or daemon changes.
- `scripts/audio_probe.py <target> --display N --out DIR` checks a target: whether it has audio
  (the original's DirectSound pointer and sound durations, read with ftpeek; the native SDL audio
  thread) and that nothing reached the host (no PipeWire streams or clients, no open `/dev/snd`,
  pulse or pipewire descriptors).
- **New-game light show** (audio on, F2 after the original's start-up show, resolution 0.1 s): F2
  to the end of the show / to the ball feed is 2.8 s / 3.8 s on Dragon's Keep and 1.7 s / 2.7 s on
  Skulduggery, the same natively and under Wine. With audio on, the original also plays a start-up
  light show of its own in the first seconds after the window appears; an F2 during that show stops
  it and skips the new game's show (ball feed about 1.0 s after F2). Audio-on comparisons therefore
  press F2 after about 5 s (`newgame_burst_late.json` uses 8.2 s) or use `init startup_wait 1` on
  the native side.

## Measured properties

- **Alignment (Space Cadet).** The playfield's upper half matches with Wine = native + 19 rows, the
  lower half with + 18 rows (a 1 px seam near native y 330); the score panel art is 1 px further
  right natively. The regions `upper`, `lower`, `panel`, `score` and `--align` absorb this. With
  `--align 0`, Wine's `lower` region must start at y 354 (`targets.json`). The cause of the seam
  (port or Wine) is not known. `selftest_compare_align.py` checks the alignment search.
- After alignment, static frames differ in 0.1-0.3 % of playfield pixels (lights, attract
  animation); the text panels differ in glyph placement, so `panel` is reported, not judged.
- **Report metric.** `AE` is ImageMagick 7's `-metric AE -fuzz 8%`, a magnitude-weighted sum in
  pixel equivalents, not a pixel count; it is several times smaller than the number of visibly
  differing pixels, which the report lists separately (`differing px`). Limits written as "0.5 %"
  are AE %.
- **Determinism.** Neither side repeats after the ball is launched in free-running (harness) runs:
  the original steps its physics with a wall-clock delta and has no fixed timestep. Frames before
  the launch are pixel-identical run to run on both sides. Wine vs native comparisons are therefore
  meaningful for deterministic states (start-up, UI, text, rule state after scripted events), not
  for frames after free ball motion. Native runs become repeatable with the hook's `init fixed_dt`
  and `init seed`.
- **Early plunger press.** An early press of `first_frame.json` lands on the still-bouncing ball;
  the original launches it in most runs. Launch behaviour is compared with `first_frame_late.json`
  or with repeated press tests (`scripts/*_press.json`).
- **High-score dialog.** After a game over with a high score, the original shows a modal name dialog
  (a separate window, visible with a `windows` step); a Wine run that must continue
  past it has to dismiss it.

## Self-tests

No display needed unless noted: `selftest_anchor.py`, `selftest_from_json_players.py`,
`selftest_report_only.py`, `selftest_watch_keys.py`, `selftest_compare_align.py [shot.png]` (needs
one real native shot), `selftest_window_size.py` (needs a display and a hook binary).
