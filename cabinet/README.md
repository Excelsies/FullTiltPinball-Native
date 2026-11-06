# Arcade cabinet setup

This folder is a copy of the arcade cabinet's own tools and configuration: the button service, the
cabinet settings menu, the RetroPie menu entries and the launch configuration of the three Full Tilt!
pinball tables. The Raspberry Pi is the authoritative copy; this folder mirrors it. It holds no
RetroPie code, game data, ROMs, art, saves or high scores.

## Layout

| Here | On the Pi |
|---|---|
| `arcade-tools/` | `~/arcade-tools/` (the Pi's own `README.md` describes every tool there) |
| `arcade-tools/testing/` | `~/arcade-tools/testing/` (test aids) |
| `retropie/retropiemenu/cabinet-*.sh` | `~/RetroPie/retropiemenu/` (RetroPie menu entries) |
| `retropie/roms/ports/*.sh` | `~/RetroPie/roms/ports/` (Ports entries of the three tables) |
| `retropie/configs/ports/{spacecadet,dragonskeep,skulduggery}/emulators.cfg` | `/opt/retropie/configs/ports/...` (launch lines) |
| `retropie/configs/all/runcommand-onstart.sh` | `/opt/retropie/configs/all/` (runs before every game) |
| `retropie/configs/all/runcommand-menu/Game controls.sh` | `/opt/retropie/configs/all/runcommand-menu/` (launch menu > User Menu entry) |

Not in this folder:

- `~/arcade-tools/overlay` and `~/arcade-tools/snap`: binaries, built on the Pi from `overlay.c` and
  `snap.c` (see "Building overlay and snap").
- `/etc/systemd/system/cabinetd.service`: an installed copy of `arcade-tools/cabinetd.service`.
- `/etc/profile.d/05-cabinet-controls.sh`: written by `remap_standalone.py` (SDL gamepad layout).
- `~/old-setup/`: backups of the files the tools changed.
- `__pycache__/` folders.

## Hardware

- Raspberry Pi 3 B+ running RetroPie 4.8, HDMI output and sound (ALSA card 0, control `HDMI`).
- Two DragonRise control boards (USB vendor 0079, product 0006), one per player. Each has a stick
  and eight buttons: top row Y, X, top-right; bottom row B, A, bottom-right (R); START; COIN.
- Four front buttons, EXIT and MODE on player 1's board, MENU and PLAY on player 2's board.
- One side (pinball) button on each side of the cabinet: the left one on player 1's board, the right
  one on player 2's board.
- `arcade-tools/button-map.json` records which board button number each physical button sends
  (made with `identify_buttons.py`).

SD card partitions: 2 is the system (`/`, read-only when locked), 3 is `retrodata`, mounted on
`/mnt/retrodata`. `retrodata` holds the saves (`/mnt/retrodata/saves/`), the PSP saves, the swap
file, the pinball settings (`/mnt/retrodata/ports`), the cabinet settings (`/mnt/retrodata/cabinet`)
and the whole RetroPie configuration tree, which is bind-mounted on `/opt/retropie/configs`. It
must stay readable by the Pi's e2fsprogs 1.44, so it is not created or tuned with newer tools'
default features (such as `orphan_file`).

## Button service (cabinetd)

`arcade-tools/cabinetd.py` runs as root under systemd (`cabinetd.service`, enabled, `Restart=always`,
3 s delay). It reads both boards alongside EmulationStation and the emulators, sends emulator
shortcut keys through a virtual keyboard (`vpad.py`), and draws its panes with the `overlay` helper
on a DispmanX layer above everything. While a pane is open it grabs both boards, so neither the game
nor EmulationStation sees those presses. Pane images are drawn by `panes.py` from `controls.json`
into `/dev/shm/cabinet/` when the service starts.

What every button does (`controls.json`, which is also the text of the MODE button guide):

In the menus (EmulationStation):

| Button | Does |
|---|---|
| A | Play / select |
| B | Back |
| X | Jump to a random game |
| Y | Add to favourites |
| START | Main menu |
| COIN | Game list options |
| Stick left / right | Change system |
| Top-right / bottom-right | Page up / down |
| PLAY | Random game (in the system list: random system) |
| Any button at launch | Launch options and game controls |
| Hold MODE 2 s | Button guide |
| Hold MENU 2 s | Cabinet settings menu |
| Hold EXIT 3 s | Shut down (confirm) |
| Hold both STARTs 3 s | Shut down (confirm) |

In a game:

| Button | Does |
|---|---|
| EXIT | Quit the game |
| MENU (tap, 0.5 s or less) | The emulator's menu, sent on release |
| PLAY | Pause / resume |
| Left / right side | Pinball flippers (L2 / R2) |
| COIN or MODE + START | Quit the game |
| COIN or MODE + X | Emulator menu |
| COIN or MODE + B | Reset the game |
| COIN or MODE + top-right | Load saved state |
| COIN or MODE + bottom-right | Save state |
| Hold MODE 2 s | Button guide |
| Hold MENU 2 s | Cabinet settings menu |
| Hold both STARTs 3 s | Shut down (confirm) |

How it works:

- The keys sent to each emulator are in `EMULATORS` in `cabinetd.py`, by process name. COIN combos are
  only made by cabinetd for emulators that do not handle COIN + button themselves (RetroArch,
  Mupen64Plus and AdvanceMAME do).
- Quit: the emulator's quit key if it has one, then SIGTERM after 5 s, then SIGKILL after another 5 s.
  Emulators without a quit key get SIGTERM at once.
- PLAY in the menus presses X and then A on player 1's board (EmulationStation's random jump, then
  launch).
- The right side button is echoed onto player 1's board as button 5 (a number neither board uses),
  so one-player games see both side buttons on one board.
- The shutdown confirm opens on No; the stick chooses No / Yes, A confirms, B or EXIT cancels.
  Any button closes the button guide.
- Banners that take no input: "Press any button now for launch options & game controls" while
  runcommand's launch prompt waits, and "Hold MODE for the button guide" for 4 s once a game has run
  for 5 s.
- MENU: a tap of 0.5 s or less sends the emulator's menu key when it is let go; a hold of 2 s opens
  the cabinet settings menu and sends nothing to the emulator. A press let go between the two does
  nothing.
- `padmap.py` (Doom) asks cabinetd over `/run/cabinet/pad.sock` for one merged joystick of both
  boards with renumbered buttons; cabinetd grabs the boards and feeds the merged joystick while the
  game runs. While a pane is open the game sees the stick centred; when it closes, the stick's real
  position is passed on.
- Shut down and restart (`power.py`): quit the game (up to 20 s), then let EmulationStation save its
  game lists and power off or reboot (flag file `/tmp/es-shutdown` or `/tmp/es-sysrestart` for its
  launcher, then SIGTERM), with `systemctl poweroff` / `reboot` after another 20 s. Without
  EmulationStation it calls `systemctl` directly. A failed step shows a message instead.

## Cabinet settings menu

Hold MENU 2 s, in the menus or in a game (`settings_menu.py`, drawn by `settings_pane.py`). Either
player's stick: up / down choose (the list wraps), left / right change; holding the stick repeats.
A selects, B goes back (in the list: closes), MENU or EXIT close from any view except while a lock,
unlock or theme change runs. The chosen item's description (`settings_help.json`, at most 3 lines)
shows under the list; a status line shows problems and whether the game is paused.

| Item | Does |
|---|---|
| Volume | Master volume: ALSA control `HDMI` on card 0, 0-100 % in 5 % steps on the alsamixer scale (`amixer -M`); each change plays a short 1 kHz tick. Changing the volume while muted un-mutes. |
| Mute | The control's playback switch (A or left / right); the level is kept. |
| Quit game | Only while a game runs: closes the menu, then the same quit as EXIT. |
| Theme | Only in the menus (no game running or starting, EmulationStation running): EmulationStation's theme. Left / right pick one of the installed themes (`/etc/emulationstation/themes`, `~/.emulationstation/themes`; the row shows "(in use)" for the current one), A asks to confirm, then the game list restarts with it (about half a minute, see below). |
| Button guide | The MODE guide. |
| System info | IP address, CPU temperature, under-voltage / throttling (`vcgencmd get_throttled`), lock state, free space on the system and on `retrodata`, uptime. A source that fails shows "unavailable" in its row. |
| Lock cabinet / Unlock cabinet | Whichever applies; "(restart pending)" when `/boot` already asks for the other state. Confirm, then `raspi-config nonint enable_overlayfs` / `disable_overlayfs` in the background (at most 180 s, output in `/run/cabinet/lock.log`) with the same checks as the RetroPie menu entries; on success the cabinet restarts, on failure a message says why and nothing restarts. |
| Restart | Confirm, then the safe restart (`power.py`). |
| Shut down | Confirm, then the safe shutdown (`power.py`). |
| Close | Closes the menu. |

Every confirm opens on No and adds "The game will be closed first." while a game runs.

Theme change (`theme.py`, `theme_switch.py`): EmulationStation's launcher (`emulationstation.sh`)
is held (SIGSTOP), EmulationStation is asked to quit (it saves its game lists), and `ThemeSet` in
`/opt/retropie/configs/all/emulationstation/es_settings.cfg` is replaced atomically while no
EmulationStation runs (a copy of the file before the change:
`/mnt/retrodata/cabinet/es_settings.cfg.before-theme`). The launcher then continues and starts
EmulationStation again (flag file `/tmp/es-restart`). The menu shows a busy pane until
EmulationStation is ready and still names the new theme. If it does not close, start or get ready
in time, the previous file is put back the same way and the menu says which theme is in use; if
that fails too, it says so. The launcher is never left stopped: if cabinetd is killed during a
change, its next start continues the launcher (`/run/cabinet/es-held.json`).

Errors: an error inside the menu (drawing a pane, the overlay, reading a lock result) is logged with
its traceback (`journalctl -u cabinetd`) and shown as "Something went wrong" with its last line;
A or B go back to the list and cabinetd keeps running. A running lock, unlock or theme change is
still finished and its result shown. An error before the menu appears resumes the game it paused.
An overlay that died is replaced (`overlay_pipe.py`), so the next pane works again. If even the
error message cannot be shown, the menu closes (boards released, game resumed).

Persistence: volume and mute are written to `/mnt/retrodata/cabinet/settings.json`
(`{"volume": N, "muted": true|false}`, kept while locked) when the menu closes, and before a
confirmed lock, unlock, restart or shutdown, only if they changed. The file is written atomically.
cabinetd applies the saved values when it starts and again 10 s later if the mixer differs from
them (the boot's ALSA restore runs in parallel); if the volume or mute was changed in the menu
before then, that check keeps the menu's values instead. If saving fails, the menu says so once;
closing again goes through, and the value stays until the next restart.

Paused games: while the menu is open, the game's whole process tree is stopped (SIGSTOP) and
continued (SIGCONT) when it closes, for the pinball tables (`SpaceCadetPinba`), RetroArch,
Mupen64Plus and DraStic (`PAUSE_OK` in `gamepause.py`). Other emulators keep running and the status
line says so. The stopped process ids are kept in `/run/cabinet/paused.json`; when cabinetd starts
it continues any game listed there, and on a normal stop it continues the paused game itself.

EmulationStation's own volume slider has no effect on this cabinet: it looks for a mixer control
`PCM`, which the HDMI card does not have. Its `AudioDevice` setting is left as it is.

## Lock and unlock

Locked is raspi-config's overlay file system: the system partition is mounted read-only under a
RAM layer, so changes to it are gone at the next restart. Everything on `retrodata` is kept: saves,
high scores, settings, per-game controls, game lists and the whole `/opt/retropie/configs` tree.
Games, themes, tool changes and updates are added only while unlocked.

- RetroPie menu: Cabinet: Lock (read-only) (`cabinet-lock.sh`) and Cabinet: Unlock (maintenance
  mode) (`cabinet-unlock.sh`) confirm, run raspi-config, check `/boot/cmdline.txt`,
  `/boot/config.txt` and the overlay initramfs, and restart. Both report "already locked" /
  "already unlocked" when there is nothing to do.
- The settings menu's Lock / Unlock item does the same.
- Cabinet: Update Button Hints (`cabinet-hints.sh`) puts the button hints back into every theme
  (`theme_hints.py`) and restarts EmulationStation; it refuses while locked.
- If a locked boot fails: put the SD card in a PC and, on the `boot` partition, remove
  `boot=overlay ` from `cmdline.txt` and the `initramfs ...-overlay` line from `config.txt`.

## The pinball tables

Space Cadet, Dragon's Keep and Skulduggery run on this project's port (FullTiltPinball-Native).

- Build: on the Pi, the checkout of this repository lives in `~/build/<checkout>`. It builds in
  `~/build/<checkout>/build-release` (CMake, Release, Unix Makefiles), and the binary is written to
  `~/build/<checkout>/bin/SpaceCadetPinball`.
- Install: the binary goes to `~/RetroPie/roms/ports/fulltilt/SpaceCadetPinball`, next to the Full
  Tilt! data folders `CADET/`, `DRAGON/` and `PIRATES/`, each holding the table's EXE next to its
  DAT (plus its BMP and sound files). The game data is the user's own copy and is not in this
  repository.
- Launch: Ports > Space Cadet Pinball, Dragon's Keep, Skulduggery (`retropie/roms/ports/*.sh`) call
  runcommand with the port names `spacecadet`, `dragonskeep` and `skulduggery`. Their
  `emulators.cfg` lines run the binary with `-table=cadet`, `-table=dragon` or `-table=pirates` and
  `-fullscreen`, with `XDG_DATA_HOME=/mnt/retrodata/ports` and an `SDL_GAMECONTROLLERCONFIG` for the
  boards (SDL A, B, X, Y = the board's bottom-middle, bottom-left, top-left and top-middle buttons;
  shoulders = top-right and bottom-right; d-pad = stick).
- Space Cadet's `emulators.cfg` also has `spacecadet-upstream`, the unmodified upstream
  SpaceCadetPinball build in `~/RetroPie/roms/ports/spacecadet/` (built in
  `~/build/SpaceCadetPinball`), chosen from the launch menu; the default is `spacecadet`.
- Settings and high scores: one file for all three tables,
  `/mnt/retrodata/ports/SpaceCadetPinball/imgui_pb.ini`, with per-table high-score keys.
- Controls: the binary's process name is `SpaceCadetPinba` (15 characters), so cabinetd's entry
  applies to all three tables. While a side button is held cabinetd holds Z (left) or / (right) on
  its virtual keyboard; START sends F2 (new game), PLAY F3 (pause), a MENU tap F9 (the game's menu
  bar). There is no quit key (Esc only asks "Exit the game?"), so EXIT ends the game with SIGTERM,
  which SDL turns into a clean quit.

## Deploying a changed tool

1. Unlock: RetroPie menu > Cabinet: Unlock, or the settings menu's Unlock cabinet (the cabinet
   restarts).
2. Copy the changed files to the same place on the Pi, for example
   `scp arcade-tools/settings_menu.py pi@<cabinet>:arcade-tools/`.
3. For a change to cabinetd or a module it loads: `sudo systemctl restart cabinetd`, then check
   `journalctl -u cabinetd -b` for "cabinet service ready" and no traceback. A changed
   `cabinetd.service` is copied to `/etc/systemd/system/` first, followed by
   `sudo systemctl daemon-reload`.
4. Lock again (the cabinet restarts).

Files under `/opt/retropie/configs` are on `retrodata`, so changes to them are kept even while
locked.

## Building overlay and snap

Both are small C programs using the Pi's VideoCore DispmanX API, built on the Pi (unlocked, so the
binaries are kept):

```sh
cd ~/arcade-tools
gcc -O2 -Wall -o overlay overlay.c -I/opt/vc/include -L/opt/vc/lib -lbcm_host
gcc -O2 -o snap snap.c -I/opt/vc/include -L/opt/vc/lib -lbcm_host
```

- `overlay` shows the panes: it reads `show FILE`, `toast FILE` and `hide` on stdin (FILE: uint32
  width, uint32 height, RGBA rows), answers `ok` (or `error`) to each, and scales images down to fit
  the current video mode. cabinetd starts it and talks to it over its stdin / stdout.
- `snap out.ppm` writes a screenshot of the real screen (all DispmanX layers).

## Other tools in arcade-tools

| Tool | Does |
|---|---|
| `remap_controls.py` (+ `remap_standalone.py`) | Maps the cabinet buttons into EmulationStation and every emulator |
| `player_ports.py` | Points AdvanceMAME and Reicast at the boards' current event devices; `runcommand-onstart.sh` runs it before their games |
| `game_controls.py` | Launch menu > User Menu > Game controls (`Game controls.sh`): per-game buttons |
| `pinball_layouts.py` | Points the side buttons (player 1 L2 / R2) at each RetroArch pinball game's flippers |
| `padmap.py` | Doom: one merged joystick with renumbered buttons, made by cabinetd |
| `theme_hints.py` | Button hints in every EmulationStation theme (`--remove` takes them out) |
| `n64_direct.py`, `n64_dpad_stick.py` | N64 emulator entries for games that need them |
| `identify_buttons.py` | On-screen button check, writes `button-map.json` |
| `migrate_to_retrodata.py`, `prune_gamelists.py`, `reorder_systems.py`, `update-console-order` | Setup steps: everything games write onto `retrodata`; game-list entries without a file removed; the console order |
| `launch_test.py`, `fix_failures.py`, `test_list.py`, `console-pass.sh`, `trial.sh`, `loadwatch.sh`, `restart-menu` | Launch every game and fix failures; screenshot or load-log one emulator run; restart EmulationStation |
| `testing/` | Simulated presses, in-game tests, theme screenshots, boot timing, settings menu redraw timing, failure-message checks, faults in the menu's drawing / overlay / lock result (`menu_fault_test.py`: cabinetd must survive), the Theme item's failure and restore paths (`theme_fail_test.py`), keys held by cabinetd's virtual devices |

## Known limitations

- Only the settings menu catches its own errors. An error elsewhere in cabinetd, such as the overlay
  failing while it shows the button guide, the shutdown confirm or a banner, ends cabinetd; systemd
  starts it again 3 s later.
