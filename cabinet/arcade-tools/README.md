# Arcade cabinet tools

RetroPie 4.8 on a Raspberry Pi 3 B+, two DragonRise control boards (players 1 and 2) plus four
front buttons (EXIT, MODE, MENU, PLAY). Everything here is re-runnable.

## Where things live

| What | Where |
|---|---|
| System (read-only when locked) | partition 2, `/` |
| Saves, save states, arcade high scores | partition 3 `retrodata`: `/mnt/retrodata/saves/<ROM folder>/` |
| PSP saves | `/mnt/retrodata/ppsspp` (linked from `~/.config/ppsspp`) |
| All RetroPie settings, game lists, per-game controls, DS / Dreamcast / N64 / Doom saves | `/mnt/retrodata/configs`, bind-mounted on `/opt/retropie/configs` |
| Swap file | `/mnt/retrodata/swapfile` |
| Originals of everything changed | `~/old-setup/` |

## Locking

RetroPie menu: **Cabinet: Lock (read-only)** / **Cabinet: Unlock (maintenance mode)** (both restart).
Locked = raspi-config's overlay file system: changes to the system vanish at restart; everything on
`retrodata` is kept. Add games, themes or updates only while unlocked.

If a locked boot fails: put the SD card in a PC, and on the `boot` partition remove
`boot=overlay ` from `cmdline.txt` and the `initramfs ...-overlay` line from `config.txt`.

The `retrodata` file system must stay readable by the Pi's e2fsprogs 1.44: don't create or tune it
with newer tools' default features (such as `orphan_file`: the boot's file system check refuses it).

## Tools

| Tool | Does |
|---|---|
| `cabinetd.py` (+ `vpad.py`, `panes.py`, `overlay`, `cabinetd.service`, `power.py`) | Front buttons, MODE hotkey, button guide, shutdown confirm, banners. Text: `controls.json`. `power.py`: the safe shut down / restart (quit the game, EmulationStation saves its lists, power off / reboot); a failed step shows a message instead |
| `settings_menu.py` (+ `settings_pane.py`, `settings_help.json`, `audio.py`, `sysinfo.py`, `gamepause.py`, `cabinet_lock.py`, `menu_jobs.py`, `theme.py`, `theme_switch.py`, `overlay_pipe.py`) | Cabinet settings menu inside cabinetd: hold MENU 2 s (also in games). See below |
| `remap_controls.py` (+ `remap_standalone.py`) | Maps the cabinet buttons into EmulationStation and every emulator |
| `player_ports.py` | Run before AdvanceMAME / Reicast games (`runcommand-onstart.sh`): board numbers change between boots |
| `game_controls.py` | Launch menu > User Menu > Game controls: per-game buttons |
| `pinball_layouts.py` | Side (pinball) buttons = player 1 L2 / R2 (right one echoed by cabinetd); points them at each pinball game's flippers |
| `padmap.py` | Doom: one merged joystick with A/B renumbered (asks cabinetd); while a cabinet pane is up the game sees its stick centred, on close the stick's real position |
| Ports > Space Cadet Pinball, Dragon's Keep, Skulduggery | The FullTiltPinball-Native port (Full Tilt! Pinball tables on the SpaceCadetPinball engine), built in `~/build/<checkout>` (`build-release`, Release); binary + Full Tilt data (`CADET/`, `DRAGON/`, `PIRATES/`, each EXE next to its DAT) in `~/RetroPie/roms/ports/fulltilt/`; launch lines `configs/ports/{spacecadet,dragonskeep,skulduggery}/emulators.cfg` (`-table=cadet|dragon|pirates`); one shared settings / high-score file `/mnt/retrodata/ports/SpaceCadetPinball/imgui_pb.ini` (per-table score keys). The old upstream build (github.com/k4zmu2a/SpaceCadetPinball, `~/build/SpaceCadetPinball`) stays in `spacecadet/`, launch menu choice `spacecadet-upstream`. The binary keeps the name `SpaceCadetPinball`, so cabinetd's entry holds Z and / for the side buttons on all three |
| `theme_hints.py` | Button hints in every theme (`--remove` takes them out); menu: Cabinet: Update Button Hints |
| `n64_direct.py` | Battle for Naboo / Body Harvest on GLideN64 |
| `n64_dpad_stick.py` | Kirby 64: the stick also presses the N64 D-pad (the game ignores the analog stick); own data dir `configs/n64/dpad-on-stick/`, rebuilt at each launch |
| `migrate_to_retrodata.py`, `prune_gamelists.py` | One-time moves: saves to retrodata; dead game-list entries removed |
| `identify_buttons.py` -> `button-map.json` | On-screen button check (which board/button is which) |
| `snap` | Screenshot of the real screen (`snap out.ppm`) |
| `testing/` | Test aids: simulated presses, in-game tests, theme screenshots, boot timing, `settings_bench.py` (settings menu redraw time), `power_fail_test.py` (the failure messages of shut down / restart / lock, with nothing really done), `audio_fail_test.py` (the menu's messages when amixer / aplay or the pause list fail), `held_keys.py` (keys cabinetd's virtual keyboard / Doom joystick hold now), `menu_fault_test.py` (errors in the menu's drawing / overlay / lock result: cabinetd must survive), `theme_fail_test.py` (the Theme item's failure and restore paths) |

## Cabinet settings menu

Hold **MENU 2 s** in the menus or in a game (a short MENU tap, up to 0.5 s, still opens the
emulator's menu, on release; a longer press that is let go before 2 s does nothing). Either
player's stick: up / down choose, left / right change; **A** select, **B** back / close; MENU or
EXIT close. The chosen item's description shows under the list (texts: `settings_help.json`, at
most 3 lines each).

| Item | Does |
|---|---|
| Volume | Master volume: ALSA control `HDMI` on card 0, 0-100 % in 5 % steps (alsamixer scale, `amixer -M`), short tick to hear it |
| Mute | The control's switch; the level is kept, un-muting (or changing the volume) brings it back |
| Quit game | Only while a game runs: the same quit as EXIT |
| Theme | Only in the menus (no game running or starting): EmulationStation's theme. Stick left / right picks one of the installed themes (`/etc/emulationstation/themes`, `~/.emulationstation/themes`), A asks to confirm (default No), then the game list restarts with it (about half a minute; `theme_switch.py`, see below) |
| Button guide | The MODE guide |
| System info | IP address, CPU temperature, under-voltage / throttling (`vcgencmd get_throttled`), lock state, free space, uptime |
| Lock / Unlock cabinet | Confirm (default No), then the same `raspi-config nonint enable_overlayfs` / `disable_overlayfs` and checks as the RetroPie menu entries; restarts |
| Restart | Confirm (default No), then like shut down: quit the game, EmulationStation saves its lists, reboot |
| Shut down | Confirm (default No), then the usual safe shutdown |

- Volume and mute are stored in `/mnt/retrodata/cabinet/settings.json` (kept while locked) and
  applied when cabinetd starts, and again 10 s later if the boot's ALSA restore changed them.
- EmulationStation's own volume slider does nothing on this cabinet: it looks for a mixer control
  `PCM`, which the HDMI card does not have (`es_log.txt`: "VolumeControl::init() - Failed to find
  mixer elements!"). Leave ES's `AudioDevice` as it is (setting it to `HDMI` would make ES write its
  slider value over the cabinet's).
- Theme change: EmulationStation's launcher (`emulationstation.sh`) is held (SIGSTOP), EmulationStation
  is asked to quit (it saves its game lists), `ThemeSet` in
  `/opt/retropie/configs/all/emulationstation/es_settings.cfg` is replaced while none runs (a copy of
  the file before: `/mnt/retrodata/cabinet/es_settings.cfg.before-theme`), and the launcher starts it
  again (`/tmp/es-restart`). The menu waits until it is ready with the new theme; if it is not, the
  previous file is put back the same way. If cabinetd is killed meanwhile, its next start continues
  the launcher (`/run/cabinet/es-held.json`). Test aid: `testing/theme_fail_test.py`.
- An error inside the menu (drawing, the overlay, a lock result that cannot be read) is logged with
  its traceback and shown ("Something went wrong"); cabinetd keeps running, a paused game is resumed,
  a running lock / theme job is still finished. Test aid: `testing/menu_fault_test.py`.
- While the menu is open, Space Cadet / Dragon's Keep / Skulduggery, RetroArch, Mupen64Plus and
  DraStic are paused (the process is stopped and continued; the sound stops cleanly and comes
  back). Other emulators keep running; the menu says so. If cabinetd is killed while a game is
  paused, it continues the game when it restarts (`/run/cabinet/paused.json`).
