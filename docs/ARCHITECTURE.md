# Architecture

How the SpaceCadetPinball engine is extended so that it plays Dragon's Keep and Skulduggery as well
as Space Cadet.

## Background

All three Full Tilt! tables run on one engine: `CADET.EXE`, `DRAGON.EXE` and `PIRATES.EXE` are
builds of the same engine code with a different table-rules module linked in (measurements in
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)). Upstream SpaceCadetPinball follows the engine of
Windows XP's `pinball.exe`, which lacks some part types and features that Full Tilt's engine has
(they are present, unused, in `CADET.EXE` too). The port therefore adds:

1. the missing engine part types and engine behaviours (in `SpaceCadetPinball/`);
2. one rules module per table (in `SpaceCadetPinball/rules/`), the counterpart of Space Cadet's
   `control.cpp`;
3. run-time loading of each table's text from its original `.EXE`.

## Table selection and the Full Tilt gate

`tables.cpp` holds the table catalogue:

| Id | Table | DAT | Folders probed | High-score key prefix |
|---|---|---|---|---|
| `cadet` | Space Cadet | `CADET.DAT` | flat, `CADET/` | none (upstream keys) |
| `dragon` | Dragon's Keep | `DRAGON.DAT` | flat, `DRAGON/` | `DragonsKeep.` |
| `pirates` | Skulduggery | `PIRATES.DAT` | flat, `PIRATES/`, `PIRATE/` | `Skulduggery.` |
| `3dpb` | 3D Pinball | `PINBALL.DAT` | flat | none |
| `demo` | Full Tilt demo | `DEMO.DAT` | flat | none |

`tables::Select()` (called by `pb::SelectDatFile`) picks the table from `-table=<id>`, then the
`Table` option, then upstream's order over the flat layout, then Dragon's Keep and Skulduggery. A
table asked for by name is never replaced by another one. It sets `pb::BasePath`,
`pb::DatFileName`, `pb::FullTiltMode` and `pb::FullTiltDemoMode` as upstream did.

**The gate is `pb::IsFullTiltTable()`**: true for `dragon` and `pirates` only. It is deliberately
not `pb::FullTiltMode`, which upstream also sets for Full Tilt's own `CADET.DAT`. Every Full Tilt
behaviour that would change Space Cadet sits behind this gate, so Space Cadet keeps upstream's code
paths. Changes that are identical for Space Cadet's data (shown by data or code) are ungated.

## Engine part types

The table constructor (`TPinballTable.cpp`) builds the parts listed in the DAT. Part types added:

| DAT type | Class | What it is | Used by |
|---|---|---|---|
| 1003 / 1004 | `TFlipper` | Several flippers per side: `FlipperListL` / `FlipperListR`; a flipper key moves every flipper of its side | Dragon's Keep (2 right), Skulduggery (2 + 2) |
| 1034 | `TPlunger` (launcher flag) | Skulduggery's launcher: a press launches the ball, no pull-back; it takes the place of the 1001 plunger | Skulduggery |
| 1035 | `TRampExit` | Invisible line that switches a crossing ball between two collision layers and reports the direction to the rules | both |
| 1036 | `TAccelerator` | Invisible line that stops the ball and throws it along the kicker direction at the kicker's speed | both |
| 1037 | `TBall` | A ball placed by the DAT (captive ball): skipped by the stuck-ball check and the tilt drain, and it stays where it rolled on a reset | Skulduggery |
| 1038 | `TLightSweeps` | Light-show controller: bins every light into a grid and flashes rings of cells (one per table at most) | both |
| 1042 | `TMagnet` | Playfield magnet built on `TKickout`: its field pulls the ball until the centre trigger switches it off for a while | Skulduggery |
| 1043 | `TDiverter` | One-way wall the rules switch on and off; the back side lets the ball through and flashes | Dragon's Keep |

Every component also records the DAT type it was built from (`TypeId`), as the original's
`GetTypeId()` does. `DelayedMessage` is the original's helper that sends one message to a
component after 0.5 s.

## Engine behaviours behind the gate

On Dragon's Keep and Skulduggery the existing part classes follow Full Tilt's code where it differs
from upstream. The main ones:

| Area | Full Tilt behaviour (gated) |
|---|---|
| `TBlocker` (1011) | Multi-state blocker: up to 8 states, each with its own wall, a step timer, and a wait while a ball is in the way |
| `THole`, `TSink`, `TKickout`, `TKickback`, `TRamp` | Full Tilt's attributes (hold time, rise speed, exit height, eject masks, layer walls), eject sequences and notifications to the rules |
| `TLight`, `TLightGroup` | Full Tilt's active flag handling, light shows and per-player state; random draws through `ft_rand` |
| `TBumper`, `TWall`, `TPopupTarget`, `TSoloTarget`, `TDrain` | Timer handling on player change, tilt and disable as in Full Tilt |
| `TTextBox` | Full Tilt's message queue |
| `TPinballComponent` | Per-player copies of `MessageField`, swapped on player change |
| `TDemo` | The demo mode's 5 s watchdog that frees a stuck demo ball |
| Table | Third score accumulator (super jackpot, capped at 20,000,000); score multiplier stored as the factor; tilt timeout 15 s (upstream 30 s); nudging only while a ball is in play |
| Start-up | A 2000 ms wait after the window is shown, before the first game starts (as the original) |

Some additions are not gated because they cannot change Space Cadet: `TTimer` gained Full Tilt's
stop, time-left query and per-player handling (`CADET.DAT` has no timer part), text boxes accept a
second message font (`font2`, absent from `CADET.DAT`), and new message codes were added to
`MessageCode` without renumbering the existing ones.

The DAT loader (`loader.cpp`, `GroupData.cpp`, `TTableLayer.cpp`) gained what the two DATs need:
optional float attributes read silently, room for 70 sounds, the kicker attribute 410 and Full
Tilt's normalisation of kicker directions, a second bitmap per visual state (the alternate ball),
Full Tilt's table-width rule, and Full Tilt's decoding of spliced bitmaps at the row pitch the DAT
was authored for. A sound whose wave file did not load has duration 0 on the Full Tilt tables, as
in the original (sound durations drive several timers).

## Table rules

`control.cpp` keeps Space Cadet's rules unchanged. `control::Rules` points to a `TableRules`
object (`table_rules.h`) for Dragon's Keep and Skulduggery and is null for every other table:

- `control::CreateRules()` creates `DragonRules` or `PiratesRules` in `pb::init`, before the table
  is constructed; `control::DestroyRules()` deletes it after the table.
- The six entry points of `control.cpp` (`make_links`, `ClearLinks`, `handler`,
  `table_control_handler`, `pbctrl_bdoor_controller`, `UnstuckBall`) forward to `Rules` when it
  is set and run Space Cadet's code otherwise.
- The table sends `TableBuilt` once after the links are made (rules only); the rules load their
  music and set their new-game values there and in `NewGame`. Space Cadet's new-game defaults run
  only when `Rules` is null.
- Engine text events (pause, tilt, game over, credits, ...) go through
  `control::TextEvent(code)`: with rules set, the rules display the table's own text; otherwise the
  engine runs upstream's text code.
- Keys: on the Full Tilt tables the engine runs the original's `hidden<Tab>test` detector and
  passes each key to `Rules->BackdoorKey` with flags for the flipper keys. The Cheats menu lists the
  names `Rules->CheatNames()` returns.
- `Rules->DumpState()` reports rule state to the test hook.

Files in `SpaceCadetPinball/rules/`:

| File | Contents |
|---|---|
| `ft_common.*` | `FtRules`, the base class: the rules code identical in both executables (component links, scoring helpers, jackpot, ball save, the ticker). A missing link or text box is reported and aborts, never skipped |
| `dragon_rules.*`, `pirates_rules.*` | The `TableRules` entry points, the per-table rule state, the mission dispatcher, cheats and text events |
| `dragon_tables.cpp`, `pirates_tables.cpp` | The control tables (component tags, control functions, score arrays), generated by `re/rules_tables_gen.py`; do not edit by hand |
| `dragon_*.cpp`, `pirates_*.cpp` | One file per area of the playfield or group of modes (ball life cycle, bonus count, targets, sinks, ramps, modes, ...) |

Random numbers come from `ft_rand`: on the Full Tilt tables it is the MSVC `rand()` generator,
starting at state 1 when the table is loaded (the originals never reseed it), and engine and rules
share this one stream, as in the original. `RandFloat()` routes through it. Space Cadet keeps
`std::rand()`.

## Table text from the EXE

No game text is stored in the repository. On the Full Tilt tables `winmain.cpp` calls
`table_text::Load(tables::ExePath())` after the table is selected:

- `tables::ExePath()` looks for `<DAT stem>.EXE` / `.exe` (as found, then upper case) next to the
  DAT.
- `table_text.cpp` is a self-contained PE32 reader. It reads the string resources the rules use
  (the window title and the table messages); the window title is set from them.
- Two longer strings (the quotes and the credits ticker) live in the EXE's data section. They are
  read only from the two recorded retail builds, identified by file size and SHA-256; any other
  build gets no quotes or credits text.
- A missing or unreadable EXE stops the game with an error box. There is no fallback text.

## Music

`midi.cpp` keeps Space Cadet's three fixed tracks (`TABA1`..`TABA3`) and adds named tracks for the
Full Tilt tables: the rules call `midi::load_track(name)` for each of their six tracks (from
`TableBuilt`) and play them by handle; a missing track gives handle 0, which the rules replace with
their default track, as the original does.

File lookup under `SOUND/`: with Full Tilt data the `.MDS` file is tried before `.MID` (Full Tilt
plays only `.MDS`; Skulduggery's folder also ships `.MID` copies). Each name is tried as given, in
lower case and then in upper case, because the rules ask for lower-case names and the data ships
upper-case ones. Each `.MDS` is converted to MIDI in memory (a `.MID` is read as it is) when the
track is loaded; what is kept depends on the backend, chosen once in `music_init`:

- **fluidsynth** (`music_fluidsynth.*`, compiled when CMake finds fluidsynth's development files:
  `FT_MUSIC_FLUIDSYNTH`; used when the mixer's output is 16-bit stereo and one of SDL_mixer's
  SoundFonts, `Mix_GetSoundFonts`, exists). Tracks are kept as MIDI bytes; data that is not MIDI
  (fewer than 12 bytes, or not starting with `MThd`: the test `Mix_LoadMUS_RW` applies) gives no
  track, so an empty or foreign file is a missing track with either backend. `music_init` creates one
  synthesizer for the process (at the sample rate of the opened mixer) and, when the `Music` option
  is on, loads the SoundFonts into it; otherwise they load at the first play. The synthesizer
  renders into SDL_mixer's music hook (`Mix_HookMusic`), which SDL_mixer calls on a cleared buffer
  before it mixes the sound effects, as with its own music. Every play stops the previous one,
  creates a new player from the track's bytes, loops it and attaches the hook again. A stop
  detaches the hook, deletes the player, resets the synthesizer (`fluid_synth_system_reset`:
  controllers, programs, reverb and chorus) and then sends "all sounds off" to every channel, so no
  voice of the stopped track sounds in the next play, not even its release. The second call is
  needed with fluidsynth 2.x: there the reset's own "all sounds off" is followed by an "all notes
  off" (the reset's basic-channel set-up), and since voice changes are queued for the renderer, the
  voices, still marked as playing, are put back into their release phase. Detaching first matters
  because the player's timer runs inside the synthesizer's rendering. The main thread uses the
  synthesizer only while the hook is detached; while it is attached only the hook (SDL's audio
  thread) does, and a volume change waits for the next audio callback (or the next play) to apply
  it. SDL_mixer's `Mix_VolumeMusic` holds its device's audio lock instead; that lock is not
  reachable from outside SDL_mixer (`SDL_LockAudio` locks the legacy device 1, not the device
  `Mix_OpenAudio` opens). The gain is the one SDL_mixer's fluidsynth backend sets for the music
  volume (full volume 1.2 up to SDL_mixer 2.6.x, 1.0 from 2.8.0, by `Mix_Linked_Version`), so music
  has the level that backend gives it. The synthesizer is created in `music_init` because
  fluidsynth's one-time set-up draws from the C library's `rand()`, which Space Cadet's random
  numbers use: the SDL_mixer backend creates its first synthesizer at the same point (loading
  `TABA1` in `music_init`).
- **SDL_mixer** (no fluidsynth in the build, as on Windows, or the conditions above fail): each track
  is loaded as a `Mix_Music` (`Mix_LoadMUS_RW`) and played with `Mix_PlayMusic`. SDL_mixer's
  fluidsynth backend creates a synthesizer per `Mix_Music`, and before SDL_mixer 2.8.1 (or with
  fluidsynth 1.x) `Mix_PlayMusic` does not rewind a track that played before (README, Known
  limitations).

Either way the rules see the same handles, `ActiveKey` / `NextKey` / `IsPlaying` decide what plays,
and the test hook logs the same `track` events. `midi: music via ...` in the log names the backend.

## Physics stepping

`pb::frame(dt, maxSliceMs)` can split a frame's physics step: the ball and flipper integration, the
tick counter and the timers run in `ceil(dt / maxSliceMs)` slices (at most 16 per frame), while the
idle timer, nudge, rendering and score update run once per frame. The main loop passes 1 ms for
every table. The original's uncapped main loop steps its physics in about 1-2 ms, and the ball's
first-order integration makes the plunger rebound depend on the step size: with whole-frame steps
an early plunger press (on a ball still bouncing after the feed) does not launch the ball the way
the originals do. Test-hook runs pass 0 (one step of the whole fixed frame time), so
scripted runs stay deterministic.

## Rendering upload

The playfield is drawn into an off-screen bitmap (`vscreen`) and uploaded to an SDL streaming
texture. Every writer of that bitmap declares the area it changed (`gdrv_bitmap8::MarkDirty`), and
`BlitToTexture` uploads only those rectangles with `SDL_UpdateTexture`. Overlapping rectangles are
merged; more than 16 collapse into their bounding box; a renderer reset forces a full upload. On
the Raspberry Pi's GLES2 driver this replaces a full-screen upload per frame (about 14 ms) with
well under 1 ms. A test build with `FT_VSCREEN_VERIFY` defined keeps a shadow copy and reports
any pixel that changed without being declared.

## Windows build

The Windows build compiles the same sources. Its platform-specific code is upstream's:

- `pch.h`: `PathSeparator` is `\` and `PlatformDataPaths` holds only a null entry, which the search
  skips, so the table search list is the working directory, the executable's directory and the
  settings directory (SDL's pref path, `%APPDATA%\SpaceCadetPinball\`), and ends there.
- `SpaceCadetPinball.cpp`: the `WinMain` entry point (GUI subsystem) and `fopenu`, which converts a
  UTF-8 path to `_wfopen`. All file access in the port goes through `fopenu`.
- `ft_test_hook.cpp` (POSIX headers) compiles to nothing without `FT_TEST_HOOK`, which the Windows
  build never sets.
- Build: `CMakeLists.txt` takes the SDL libraries from `Libs/SDL2` and `Libs/SDL2_mixer`, links
  libgcc, libstdc++ and winpthread statically (`-static` under MinGW) and copies `SDL2.dll` and
  `SDL2_mixer.dll` next to the executable. `mingwcc.cmake` is the MinGW-w64 toolchain file (it picks
  `x86_64-w64-mingw32-g++-posix` or else `-g++`, and adds `Libs/` to `CMAKE_FIND_ROOT_PATH`).
  `Platform/Windows/build-mingw.sh` downloads the pinned SDL libraries and runs the build.
- Music: the Windows build has no fluidsynth (CMake looks for it only on Unix systems other than
  macOS, and SDL_mixer's MinGW build has no fluidsynth backend), so `midi.cpp` plays the converted
  MIDI through SDL_mixer, which uses Timidity and then Windows' MIDI synthesizer.

## Other engine changes

- `imgui_impl_sdl.cpp`: `SDL_CaptureMouse` is called only when the wanted state changes, and never
  again after it fails once (video drivers without mouse capture, such as the Pi's, otherwise put
  an SDL error into the log every frame).
- `CMakeLists.txt`: the port's sources, the `FT_TEST_HOOK` option (off by default) and
  `FT_OUTPUT_DIR` (the binary's output directory, so a hook build does not overwrite `bin/`).
- `ft_test_hook.*`: the native test hook, compiled only with `FT_TEST_HOOK` (see
  [TESTING.md](TESTING.md)); without it every hook call is an empty inline function.

## Space Cadet

Space Cadet runs upstream's engine and `control.cpp`. A disassembly comparison of every shared
engine function the port changes found `CADET.EXE` equivalent to `DRAGON.EXE`, so Full Tilt's own
Space Cadet has the Full Tilt behaviours listed above; the port keeps upstream's behaviour for
Space Cadet anyway. The changes that do reach Space Cadet are:

- physics in 1 ms slices (all tables);
- the plunger's release boost lasts 0.2 s with Full Tilt data, as in `CADET.EXE` (upstream
  0.025 s; 3D Pinball data keeps upstream's value). Together with the slicing this makes an early
  plunger press launch the ball as the original does;
- the music lookup above (identical result for the `CADET` folder, which has only `.MDS` files);
- the fluidsynth music backend above: one synthesizer instead of three, every play of a track starts
  at its beginning, a stop ends every voice. The synthesizer is created where the SDL_mixer backend
  creates its first one, so Space Cadet's `rand()` sequence is the same with both backends; the
  track logic is upstream's;
- the mouse-capture change and the changed-area texture upload (output pixel-identical);
- the Table menu and option, and the `-table=` command-line option.
