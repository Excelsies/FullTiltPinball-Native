# Testing

The port is checked against two references: the upstream behaviour for Space Cadet, and the
original executables (run under Wine) for Dragon's Keep and Skulduggery. All test code is in
`re/test/` and uses only the Python standard library.

| Part | Where | Reference |
|---|---|---|
| Native test hook: a build of the port that runs frame-stamped scripts with a fixed time step and a fixed seed, and logs state, sounds, text and frames | `SpaceCadetPinball/ft_test_hook.*`, `re/test/hookrun.py`, scripts in `re/test/hook_scripts/` and `re/test/hook/` | [re/test/hook_scripts/README.md](../re/test/hook_scripts/README.md) |
| Wine harness: the originals under Wine and the port side by side on Xvfb, screenshots, a memory probe of the originals' rule state, comparison tools | `re/test/harness.py`, `re/test/ftharness/`, `re/test/scripts/` | [re/test/README.md](../re/test/README.md) |
| Standalone checks: small C++ programs and Python self-tests | `re/test/native/`, `re/test/selftest_*.py`, the light-sweep check `re/test/*_sweep_check.py` | below |

## Builds

Two builds are used side by side; `FT_OUTPUT_DIR` keeps their binaries apart.

```sh
# release build -> bin/SpaceCadetPinball (the harness's native targets)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# hook build -> build-hook/bin/SpaceCadetPinball (hookrun.py --bin)
cmake -S . -B build-hook -DCMAKE_BUILD_TYPE=Release -DFT_TEST_HOOK=ON -DFT_OUTPUT_DIR=$PWD/build-hook/bin
cmake --build build-hook -j
```

With `FT_TEST_HOOK` off (the default) the hook compiles to empty inline functions. A hook build
behaves like the release build until `FT_TEST_SCRIPT` or `FT_TEST_FIFO` is set (with `FT_TEST_OUT`
for the output directory). While a script runs, the hook:

- gives every frame exactly `init fixed_dt` ms and steps the physics once per frame (no 1 ms
  slicing), so a script gives byte-identical output on every run;
- counts every main-loop pass as a frame and renders every frame, whether or not the window has
  focus;
- skips the Full Tilt tables' 2000 ms start-up wait, unless the script asks for it in script time
  (`init startup_wait 1`);
- applies inputs by frame number through the same functions the SDL event handler calls.

## Test data and displays

- Game data: a copy of the user's tables in `re/work/game/<CADET|DRAGON|PIRATE>/` (setup in
  re/test/README.md). The Full Tilt folder itself is only read.
- Displays: every run gets its own Xvfb display in the range `:90`-`:139` (`targets.json`
  `_displays`). The session code refuses a display outside the range or one whose X lock file
  exists, removes `WAYLAND_DISPLAY` and forces `SDL_VIDEODRIVER=x11`, so nothing opens on the user's
  desktop. Two runs that may overlap must use different displays (and different Wine prefixes, see
  `FT_WINEPREFIX`).
- Audio: muted everywhere. Native targets run with `-noaudio`, or with `SDL_AUDIODRIVER=dummy` for
  the audio-on targets. The shared Wine prefix has audio off; the audio-on Wine targets use a
  private prefix whose only ALSA device is the `null` plugin. `scripts/audio_probe.py` verifies
  that nothing reaches the host's audio server or devices.

## The checks and what each one shows

### Hook scripts (expectations and repeatability)

Every `.uh` script under `re/test/hook_scripts/` and `re/test/hook/` (about 400) is run twice on
the hook build of the current tree:

```sh
python re/test/hookrun.py run --table dragon --bin build-hook/bin/SpaceCadetPinball --display 101 \
    --out re/work/runs/a re/test/hook/dragon/D-CORE/<script>.uh
python re/test/hookrun.py run --table dragon --bin build-hook/bin/SpaceCadetPinball --display 101 \
    --out re/work/runs/b re/test/hook/dragon/D-CORE/<script>.uh
python re/test/hookrun.py compare re/work/runs/a re/work/runs/b
```

- `run` passes when every `expect` line holds. The expected values come from the original's code
  (part behaviour, scores, timers, sounds, text ids, rule state), so a pass shows that the part
  class or rule behaves as the original does in that scripted situation.
- `compare` passes when both runs give byte-identical output files, which shows that the run is
  deterministic (needed for the baseline comparisons below).
- Oracles beside some scripts check more complex outputs (`python <oracle> <run dir>`).
- Scripts that test the hook itself (`sc_bad_*`, `sc_text_refused`) expect exit code 3.
- Scripts whose header starts with `STUB-ONLY` (one Dragon's Keep script in `hook_scripts/`) or
  `PROBE-ONLY` (`hook/pirates/P-CORE/probe_*.uh`) need test-only stubs and fail by construction on
  the full tree (hook_scripts/README.md).

### Space Cadet stays upstream

Run for any change that could reach Space Cadet. The baseline is a build of the tree without the
change (for example a copy of the source in `re/work/`).

- **Pre-launch frames.** Baseline and candidate release binaries run the same input
  (`scripts/smoke.json`; markers `start`, `new_game`, `plunger_pulled`; regions `table` and
  `panel`), and `harness.py compare --fuzz 0% --align 0` must report 0 differing pixels. With hook
  builds the same frames are taken with `shot` at fixed frame numbers and must be byte-identical.
  Shows that start-up, menus, text and the pre-launch table are unchanged.
- **Seeded runs.** Baseline and candidate hook builds run the same Space Cadet scripts
  (`init fixed_dt`, `init seed`, frame-stamped input, e.g. `sc_smoke.uh` and the other seeded
  Space Cadet scripts in `hook_scripts/`), and `hookrun.py compare` must report every output file
  identical. Shows that Space Cadet's play, including ball motion at a fixed step, is unchanged.
  Both builds must carry the same hook and log code, so that only the change under test differs;
  two runs of the baseline must also be identical, otherwise the result is not assessable.

Hook runs do not slice the physics step, so the seeded runs do not cover the 1 ms slicing that
normal play uses on every table.

### Dragon's Keep and Skulduggery against the originals

- **First frames.** `harness.py run wine-<t> scripts/first_frame.json` and
  `harness.py run native-<t> scripts/first_frame.json`, then `harness.py compare --region upper`
  and `--region lower` with `--align 0 --fuzz 8%`: the `loaded` and `new_game` markers pass at
  0.5 % AE or less per region (Space Cadet's own Wine vs native difference is 0.1-0.3 %), with the
  side-by-side images opened. `panel` is reported only. Shows that the table loads, draws and sets up
  a new game as the original does.
- **Rest state.** `probe_run.py wine-<t> scripts/rest_check.json --watch scripts/watch_<t>.json`
  against `hookrun.py run --table <t> hook_scripts/rest_<t>.uh`, compared with
  `hookrun.py rest-state <native dumps.jsonl> <wine rest.jsonl> --types scripts/rest_<t>.json`.
  Every tagged part's state (blocker state, light flags, active flags, message fields, class fields)
  must equal the original's. Shows that loading, table set-up and the rules' set-up of every part
  match.
- **Rule-state timelines.** A harness JSON script runs under Wine with `probe_run.py` (rule state
  sampled every 50 ms) and natively as `hookrun.py from-json <json> --fixed-dt 10 --startup-wait`
  with `init dump_period 50`; `hookrun.py compare-wine` then requires the same sequence of values
  per key and transition gaps within 150 ms. Shows that the rules move through the same states
  (players, balls, jackpots, modes, ball save) in the same order and at the same times.
- **Launch behaviour.** Repeated plunger presses at fixed times after the ball feed, counted on
  both sides (launched or not); the native counts must fall inside the originals' range. Shows the
  plunger and physics stepping behave as the original for early presses.

What cannot be compared with Wine: ball motion after a launch (the original steps its physics with
wall-clock time, so neither side repeats), and anything the original's run-to-run spread already
exceeds (measure Wine against Wine first).

### Windows build under Wine

`Platform/Windows/build-mingw.sh` builds `bin-win/` (README, Build for Windows). The build is run
under Wine on Xvfb as a smoke and play check; there is no scripted comparison for it.

1. Copy `bin-win/SpaceCadetPinball.exe`, `SDL2.dll`, `SDL2_mixer.dll` and the game folders
   (`CADET/`, `DRAGON/`, `PIRATE/`, copied from the user's folder) into one scratch directory.
2. Create a private 64-bit prefix (`WINEARCH=win64 wineboot`), set
   `HKCU\Software\Wine\Drivers` `Audio=alsa` with `wine reg add`, and run with
   `ALSA_CONFIG_PATH=re/test/audio/asound_null.conf`, `PULSE_SERVER` and `PIPEWIRE_REMOTE` pointing
   at missing sockets, `WAYLAND_DISPLAY` unset and `DISPLAY` set to a free Xvfb display of the
   harness's range. Nothing is audible and no system audio setting changes. Delete the prefix
   afterwards.
3. Start `wine SpaceCadetPinball.exe -table=cadet|dragon|pirates` from the scratch directory. The log
   (stdout) shows the renderer (`direct3d`), the parts built (340/340 Space Cadet, 332/332 Dragon's
   Keep, 438/438 Skulduggery), and for the Full Tilt tables `RT_STRING ok, 113 wanted strings`,
   `sha256 matches recorded` and the quotes and credits `result=ok`. `xdotool` sends F2, then Space
   held for 1.5 s after about 9-12 s (Wine runs at a lower frame rate, so the ball feed comes
   later than natively; an earlier press falls into the new-game light show), then F3 to pause.
   Each window carries its table's own title: Space Cadet the title it is created with, the Full
   Tilt tables the title string read from their `.EXE`.
4. Check the audio isolation with `/proc/<pid>/fd` (no `/dev/snd`, pulse or pipewire descriptor)
   and `/proc/<pid>/maps` (`winealsa` and `libasound` mapped, so the audio device was opened).
   Settings land in `<prefix>/drive_c/users/<user>/AppData/Roaming/SpaceCadetPinball/imgui_pb.ini`.

Compared with the native release build driven by the same script, window captures of the three
tables differ by at most 0.9 % of the pixels, all in animation (blinking lights, ticker, attract
text, ball position after launch); geometry, colours and fonts are identical. Ball motion differs
run to run as in any comparison after a launch.

### Music

`re/test/music_check.py` runs the release build of each table three times on its own Xvfb displays
(N, N+1, N+2) with SDL's dummy audio driver: music on, music off, and music on with a SoundFont
path that does not exist (the baseline without any synthesizer). It presses F2 (new game), F3 twice
after the track has looped (pause, resume), F6 twice (music off, on) and F2 again, samples the
memory in use (VmRSS + VmSwap) every 0.25 s and counts fluidsynth's voice warnings in the log.

```sh
python re/test/music_check.py --bin bin/SpaceCadetPinball --soundfont <file.sf2> --display 120 \
    --out re/work/runs/music
```

It passes when the memory music costs is the same on all three tables (within 12 MB: one
synthesizer, whatever the number of tracks), music off costs at most 20 MB before F6 switches it on
(no SoundFont loaded), memory does not grow from the first restart on, and no `Ringbuffer full` /
`Failed to allocate a synthesis process` line appears. A build without fluidsynth fails the first
two conditions. The voice-warning flood itself happens only with SDL_mixer < 2.8.1 or fluidsynth 1.x
(the Raspberry Pi's libraries), so on a PC with newer libraries the warning count shows only that
nothing else floods.

`re/test/native/music_backend_check.sh <file.sf2> <out-dir>` (after a hook build with fluidsynth)
links `music_backend_check.cpp` against the hook build's objects, with the fluidsynth and
`Mix_HookMusic` calls wrapped (`--wrap`), and drives `midi` with SDL's dummy driver. It checks that
an empty, a too short and a non-MIDI track file give `load_track` 0 (as `Mix_LoadMUS_RW` gives no
music for them) and a MIDI file a handle; that while the music hook is attached the main thread makes
no synthesizer call, with volume changes every 0.2 ms during twelve plays; and that after each
restart no voice of the earlier play is still listed once the new play's first callback has rendered.
Exit 0 = pass, 1 = fail, 2 = cannot run (no fluidsynth backend or no SoundFont).

Which track plays is the same logic for both backends: hook runs with audio on (dummy driver,
`Music=1`, a SoundFont in `SDL_SOUNDFONTS`) log the same `track` events as with the baseline build.
Audio-on hook runs depend on wall-clock timing where the harness resizes the window, so only their
logs, not their frames, are compared between builds.

### Linux builds stay unchanged

Windows-only code sits in `_WIN32` branches, `mingwcc.cmake` (used only with
`-DCMAKE_TOOLCHAIN_FILE`) and `Platform/Windows/`. After a change to shared build files, rebuild the
release and hook builds and run the Space Cadet checks above.

### Standalone checks

- `re/test/native/ft_rand_check.cpp` and `ft_common_rand_check.cpp`: the random generator and the
  rules' random helpers against literal values of the MSVC generator. Build lines are in each
  file's header.
- `re/test/native/ft_common_null_scorer_check.sh <out-dir>`: the shared scoring helpers with a null
  scorer; links against the hook build's objects.
- `re/test/native/music_backend_check.sh <file.sf2> <out-dir>`: the fluidsynth music backend (Music,
  above); links against the hook build's objects.
- `re/test/*_sweep_check.py <hook run dir>`: a light-sweep hook run against `re/light_sweeps.py`,
  an independent simulation of part type 1038 from the DAT.
- `re/test/selftest_*.py`: self-tests of the harness (re/test/README.md).
- `FT_VSCREEN_VERIFY`: defining it in a build makes the renderer report any pixel that changed
  without being declared to the changed-area upload.
