"""One test session: a private Xvfb display, one target process, its window, and teardown.

A target is either the original Windows exe under Wine (prefix re/work/wine-prefix) or the
native port (bin/SpaceCadetPinball) run from a table's data directory. Seams: environment
FT_NATIVE_BIN (native binary), FT_WINEPREFIX (Wine prefix) and FT_WINEPREFIX_AUDIO (the private
prefix of the "audio": "null-sink" targets, whose Wine audio driver is alsa on a null PCM); the
display range comes from targets.json "_displays". Every resource the
session starts is recorded and released by Session.close(), which callers reach through
``with Session(...)`` so teardown also runs on exceptions and SIGINT/SIGTERM.
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent.parent          # re/test
REPO = TEST_DIR.parent.parent                               # repo root
WORK = REPO / "re" / "work"
TARGETS_FILE = TEST_DIR / "targets.json"


class HarnessError(RuntimeError):
    """Any failure the harness reports to its caller (never swallowed)."""


def _env_path(var: str, default: Path) -> Path:
    """Seam: the path in environment variable ``var`` when it is set, else ``default``. A variable
    that is set but empty is an error (never a silent default): it maps to a path that cannot exist,
    so the use site reports it. Whether the path exists is checked where it is used
    (``_start_native`` / ``_start_wine``), with an error naming the variable."""
    if var not in os.environ:
        return default
    value = os.environ[var]
    if not value.strip():
        return Path(f"<{var} is set but empty>")
    return Path(value).expanduser().resolve()


def _read_displays() -> range:
    """Inclusive display range from targets.json ``_displays`` [first, last]."""
    try:
        data = json.loads(TARGETS_FILE.read_text(encoding="utf-8"))
        first, last = data["_displays"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise HarnessError(f"{TARGETS_FILE}: cannot read _displays [first, last]: {exc}") from exc
    if not (isinstance(first, int) and isinstance(last, int) and 0 < first <= last):
        raise HarnessError(f"{TARGETS_FILE}: _displays must be [first, last] ints, got "
                           f"{data['_displays']!r}")
    return range(first, last + 1)


# Module names read at call time: harness.py and hookrun.py may reassign them.
PREFIX = _env_path("FT_WINEPREFIX", WORK / "wine-prefix")
# No default: the audio prefix is a private copy owned by whoever sets FT_WINEPREFIX_AUDIO (they create it
# with `harness.py init-audio-prefix` and delete it). A shared path would have no owner.
PREFIX_AUDIO = _env_path("FT_WINEPREFIX_AUDIO", Path("<FT_WINEPREFIX_AUDIO is not set>"))
ALSA_NULL_CONF = TEST_DIR / "audio" / "asound_null.conf"
NATIVE_BIN = _env_path("FT_NATIVE_BIN", REPO / "bin" / "SpaceCadetPinball")
ALLOWED_DISPLAYS = _read_displays()


def load_targets() -> dict:
    with open(TARGETS_FILE, encoding="utf-8") as fh:
        return {k: v for k, v in json.load(fh).items() if not k.startswith("_")}


def x_env(display: int) -> dict:
    """Environment pinned to the Xvfb display. WAYLAND_DISPLAY is removed and SDL forced to
    X11: on a Wayland session SDL (sdl2-compat over SDL3) otherwise opens the native
    window on the real Wayland desktop instead of Xvfb."""
    env = {k: v for k, v in os.environ.items() if k != "WAYLAND_DISPLAY"}
    env.update({"DISPLAY": f":{display}", "SDL_VIDEODRIVER": "x11", "SDL_VIDEO_DRIVER": "x11"})
    return env


def wine_env(display: int, target: dict | None = None) -> dict:
    """Environment of a Wine process. A target with ``"audio": "null-sink"`` gets the private audio
    prefix and the private ALSA configuration (null PCM); PULSE_SERVER points at a socket that does
    not exist, so even a driver fallback could not reach the user's PipeWire/PulseAudio."""
    audio = bool(target) and target.get("audio") == "null-sink"
    env = x_env(display)
    env.update({
        "WINEPREFIX": str(PREFIX_AUDIO if audio else PREFIX),
        "WINEDEBUG": "-all",
        "WINEDLLOVERRIDES": "mscoree,mshtml=",
    })
    if audio:
        env.update({"ALSA_CONFIG_PATH": str(ALSA_NULL_CONF),
                    "PULSE_SERVER": "unix:/nonexistent/ft-harness-no-pulse"})
    return env


def prefix_audio_driver(prefix: Path) -> str:
    """Value of HKCU\\Software\\Wine\\Drivers "Audio" in the prefix's user.reg ('' = none set)."""
    text = (prefix / "user.reg").read_text(encoding="utf-8", errors="replace")
    in_section = False
    for line in text.splitlines():
        if line.startswith("["):
            in_section = line.lower().startswith("[software\\\\wine\\\\drivers]")
        elif in_section and line.startswith('"Audio"='):
            return line.split("=", 1)[1].strip().strip('"')
    return ""


def run_checked(cmd: list[str], env: dict | None = None, timeout: float = 60) -> str:
    res = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
    if res.returncode != 0:
        raise HarnessError(f"{cmd[0]} failed rc={res.returncode}: {res.stderr.strip()[:400]}")
    return res.stdout


@dataclass
class Session:
    target_name: str
    display: int
    out_dir: Path
    geometry: str = "1024x768x24"
    window_timeout: float = 30.0
    window_poll: float = 0.1              # seconds between window searches; the "window" script anchor is accurate to this
    reset_registry: bool = True
    target: dict = field(default_factory=dict)
    xvfb: subprocess.Popen | None = None
    proc: subprocess.Popen | None = None
    wid: str = ""
    log: list = field(default_factory=list)
    t0: float = 0.0
    # hookrun: (frame, shot file) per shot of the script in frame order. The resize starts only when the next
    # unwritten shot is at least nudge_clear s away (see _wait_nudge_gap).
    nudge_shots: list | None = None
    nudge_step: float = 0.5               # seconds after each of the two resizes
    nudge_clear: float = 0.0              # required gap (s) between the resize start and the next shot
    nudge_frame_s: float = 0.0            # real seconds per game frame (the hook paces frames, not fixed_dt)
    nudge_frame0_min: float = 0.0         # earliest frame 0 after the spawn, for the shots before any is observed
    nudge_span: tuple | None = None       # (time.time() start, end) of the resize, for the harness self-check
    nudge_pre: set | None = None          # shot files that existed when the resize started
    t_spawn: float = 0.0
    t_spawn_wall: float = 0.0
    t_window: float = 0.0                 # time.monotonic() when the window search succeeded (script anchor "window")
    t_placed: float = 0.0                 # time.monotonic() when start() finished placing / focusing it ("placed")

    # ---- lifecycle -------------------------------------------------------------------
    def __enter__(self) -> "Session":
        self._install_signal_handlers()
        try:
            self.start()
        except BaseException:
            self.close()
            raise
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _install_signal_handlers(self) -> None:
        def _raise(signum, _frame):
            raise KeyboardInterrupt(f"signal {signum}")
        signal.signal(signal.SIGTERM, _raise)
        signal.signal(signal.SIGINT, _raise)

    def note(self, event: str, **data) -> None:
        entry = {"t": round(time.monotonic() - self.t0, 4), "event": event, **data}
        self.log.append(entry)

    def start(self) -> str:
        if self.display not in ALLOWED_DISPLAYS:
            r = ALLOWED_DISPLAYS
            raise HarnessError(f"display :{self.display} is not one of the harness displays "
                               f"{r.start}-{r.stop - 1}")
        targets = load_targets()
        if self.target_name not in targets:
            raise HarnessError(f"unknown target {self.target_name!r}; known: {sorted(targets)}")
        self.target = targets[self.target_name]
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.t0 = time.monotonic()
        self._start_xvfb()
        if self.target["kind"] == "wine":
            self._start_wine()
        elif self.target["kind"] == "native":
            self._start_native()
        else:
            raise HarnessError(f"target kind {self.target['kind']!r} unsupported")
        self.wid = self._wait_window()
        self._place_and_focus()
        self.t_placed = time.monotonic()
        return self.wid

    def close(self) -> None:
        """Always safe to call; releases everything this session started."""
        if self.target.get("kind") == "wine":
            subprocess.run(["wineserver", "-k"], env=wine_env(self.display, self.target),
                           capture_output=True, timeout=30)
        for p in (self.proc, self.xvfb):
            if p is not None and p.poll() is None:
                try:
                    os.killpg(p.pid, signal.SIGTERM)
                    p.wait(timeout=10)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    os.killpg(p.pid, signal.SIGKILL)
                    p.wait(timeout=10)
        if self.target.get("kind") == "wine":
            subprocess.run(["wineserver", "-w"], env=wine_env(self.display, self.target),
                           capture_output=True, timeout=30)
        self.note("teardown")
        self.proc = self.xvfb = None

    # ---- pieces ------------------------------------------------------------------------
    def _start_xvfb(self) -> None:
        lock = Path(f"/tmp/.X{self.display}-lock")
        if lock.exists():
            raise HarnessError(f"display :{self.display} is in use ({lock}); run `harness.py stop "
                               f"--display {self.display}` first")
        logf = open(WORK / "logs" / f"harness_xvfb{self.display}.log", "w")
        self.xvfb = subprocess.Popen(
            ["Xvfb", f":{self.display}", "-screen", "0", self.geometry, "-nolisten", "tcp"],
            stdout=logf, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self.xvfb.poll() is not None:
                raise HarnessError(f"Xvfb :{self.display} exited rc={self.xvfb.returncode}")
            if subprocess.run(["xdotool", "getdisplaygeometry"], capture_output=True,
                              env=x_env(self.display)).returncode == 0:
                self.note("xvfb_ready", display=self.display, geometry=self.geometry)
                return
            time.sleep(0.1)
        raise HarnessError(f"Xvfb :{self.display} not ready after 10 s")

    def _data_dir(self) -> Path:
        d = WORK / "game" / self.target["data_dir"]
        if not d.is_dir():
            raise HarnessError(f"table data missing: {d} (copy it from the user's FULLTILT folder)")
        return d

    def _start_wine(self) -> None:
        env = wine_env(self.display, self.target)
        prefix = Path(env["WINEPREFIX"])
        if self.target.get("audio") == "null-sink":
            self._check_audio_prefix(prefix)
        elif not (prefix / "system.reg").exists():
            via = "FT_WINEPREFIX" if "FT_WINEPREFIX" in os.environ else "`harness.py init-prefix`"
            raise HarnessError(f"Wine prefix missing (system.reg not found; {via}): {prefix}")
        if self.reset_registry:
            # Fresh Full Tilt settings each run (resolution, players, high scores, last table).
            subprocess.run(["wine", "reg", "delete", r"HKCU\Software\Cinematronics", "/f"],
                           env=env, capture_output=True, timeout=60)
            self.note("registry_reset")
        data = self._data_dir()
        logf = open(self.out_dir / "target.log", "w")
        self.proc = subprocess.Popen(["wine", self.target["exe"]], cwd=data, env=env,
                                     stdout=logf, stderr=subprocess.STDOUT, start_new_session=True)
        self.note("launched", cmd=f"wine {self.target['exe']}", cwd=str(data), prefix=str(prefix),
                  audio=self.target.get("audio", "off"))

    def _check_audio_prefix(self, prefix: Path) -> None:
        """An audio target runs only in a private copy whose Wine audio driver is alsa (never in the
        shared prefix, whose driver is empty); a missing or wrong prefix is an error, no fallback."""
        if "FT_WINEPREFIX_AUDIO" not in os.environ:
            raise HarnessError("FT_WINEPREFIX_AUDIO is not set: the audio Wine prefix is a private copy "
                               "(create it with `harness.py init-audio-prefix`, delete it when done)")
        via = "FT_WINEPREFIX_AUDIO"
        if not (prefix / "system.reg").exists():
            raise HarnessError(f"audio Wine prefix missing (system.reg not found; {via}): {prefix}")
        if prefix == PREFIX:
            raise HarnessError(f"audio target would use the shared prefix {PREFIX}; use a private "
                               f"copy ({via})")
        driver = prefix_audio_driver(prefix)
        if driver != "alsa":
            raise HarnessError(f"audio prefix {prefix}: HKCU\\Software\\Wine\\Drivers Audio is "
                               f"{driver!r}, expected 'alsa' ({via})")
        if not ALSA_NULL_CONF.is_file():
            raise HarnessError(f"missing ALSA null configuration {ALSA_NULL_CONF}")

    def _start_native(self) -> None:
        if not NATIVE_BIN.is_file():
            via = "FT_NATIVE_BIN" if "FT_NATIVE_BIN" in os.environ else "build it first"
            raise HarnessError(f"native binary missing ({via}): {NATIVE_BIN}")
        data = self._data_dir()
        xdg = self.out_dir / "xdg"                       # fresh per run: default options
        prefs = xdg / "SpaceCadetPinball"
        prefs.mkdir(parents=True, exist_ok=True)
        ini = ["[Pinball][Settings]"] + [f"{k}={v}" for k, v in self.target.get("ini", {}).items()]
        (prefs / "imgui_pb.ini").write_text("\n".join(ini) + "\n", encoding="utf-8")
        env = dict(x_env(self.display), XDG_DATA_HOME=str(xdg))
        env.update(self.target.get("env", {}))
        cmd = [str(NATIVE_BIN)] + self.target.get("args", [])
        logf = open(self.out_dir / "target.log", "w")
        self.proc = subprocess.Popen(cmd, cwd=data, env=env, stdout=logf,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        self.t_spawn = time.monotonic()
        self.t_spawn_wall = time.time()
        self.note("launched", cmd=" ".join(cmd), cwd=str(data), binary=str(NATIVE_BIN))

    def xdo(self, *args: str) -> str:
        return run_checked(["xdotool", *args], env=x_env(self.display))

    def window_geometry(self, wid: str) -> tuple[int, int, int, int]:
        out = self.xdo("getwindowgeometry", "--shell", wid)
        vals = dict(line.split("=", 1) for line in out.split())
        return int(vals["X"]), int(vals["Y"]), int(vals["WIDTH"]), int(vals["HEIGHT"])

    def _wait_window(self) -> str:
        """Largest visible window whose title matches the target's regex (fresh display: ours)."""
        pattern = self.target.get("title", ".")
        min_w, min_h = self.target.get("min_window", [300, 300])
        deadline = time.monotonic() + self.window_timeout
        while time.monotonic() < deadline:
            if self.proc is not None and self.proc.poll() is not None and \
                    self.target["kind"] == "native":
                raise HarnessError(f"target exited rc={self.proc.returncode} before its window "
                                   f"appeared (see {self.out_dir / 'target.log'})")
            res = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", pattern],
                                 env=x_env(self.display),
                                 capture_output=True, text=True)
            best = None
            for wid in res.stdout.split():
                try:
                    x, y, w, h = self.window_geometry(wid)
                except HarnessError:
                    continue                              # window vanished between calls
                if w >= min_w and h >= min_h and (best is None or w * h > best[1]):
                    best = (wid, w * h, (x, y, w, h))
            if best:
                self.t_window = time.monotonic()
                name = self.xdo("getwindowname", best[0]).strip()
                self.note("window", wid=best[0], name=name, geometry=best[2])
                return best[0]
            time.sleep(self.window_poll)
        raise HarnessError(f"no window matching {pattern!r} within {self.window_timeout} s")

    def _wait_nudge_gap(self) -> None:
        """Block until the next shot whose file is still missing is at least nudge_clear s away, or no shot is
        left. The time of a shot is anchored to the observed mtime of the last shot file written (plus the
        frames between them at nudge_frame_s); before any shot is observed it is bounded from below by the
        spawn time plus nudge_frame0_min plus its frame times. A shot due sooner is waited for first."""
        deadline = time.monotonic() + self.window_timeout
        anchor = None                                 # (frame, wall time) of the last observed shot
        for frame, path in self.nudge_shots:
            while not path.exists():
                if anchor is None:
                    eta = self.t_spawn_wall + self.nudge_frame0_min + frame * self.nudge_frame_s
                else:
                    eta = anchor[1] + (frame - anchor[0]) * self.nudge_frame_s
                if time.time() + self.nudge_clear <= eta:
                    self.note("nudge_gate_open", before=path.name)
                    return
                if time.monotonic() > deadline:
                    raise HarnessError(f"{path} not written within {self.window_timeout} s (window nudge waits for it)")
                if self.proc is not None and self.proc.poll() is not None:
                    return                            # the script finished first: nothing left to nudge
                time.sleep(0.002)
            anchor = (frame, path.stat().st_mtime)
        self.note("nudge_gate_open", before="end")

    def _place_and_focus(self) -> None:
        """No window manager on Xvfb: move the window to 0,0 (Full Tilt opens partly off-screen
        on a 1024x768 screen) and give it X input focus so XTEST keys reach it."""
        self.xdo("windowmove", self.wid, "0", "0")
        if "window_size" in self.target:
            w, h = self.target["window_size"]
            if self.nudge_shots is not None:
                # Hook runs: the resize must not overlap a shot (README, window size at capture time).
                self._wait_nudge_gap()
            # Native SDL under Xvfb without a window manager keeps its first (800x556) layout after
            # SDL_SetWindowSize; a second resize from outside delivers the size event.
            t0 = time.time()
            self.nudge_pre = {p.name for _, p in (self.nudge_shots or []) if p.exists()}   # captured before it
            try:
                self.xdo("windowsize", self.wid, str(w + 1), str(h + 1))
                time.sleep(self.nudge_step)
                self.xdo("windowsize", self.wid, str(w), str(h))
                time.sleep(self.nudge_step)
            except HarnessError:
                if self.proc is None or self.proc.poll() is None:
                    raise
                self.note("exited_during_resize")      # the script quit while the resize ran: nothing left to size
            self.nudge_span = (t0, time.time())
            if self.proc is None or self.proc.poll() is None:
                now = self.window_geometry(self.wid)[2:]
                if tuple(now) != (w, h):
                    raise HarnessError(f"window is {now[0]}x{now[1]}, target window_size is {w}x{h}")
        if self.nudge_shots is not None and self.proc is not None and self.proc.poll() is not None:
            self.note("placed_after_exit")                 # hook run whose script already quit
            return
        self.xdo("windowfocus", self.wid)
        time.sleep(0.3)
        self.note("placed", geometry=self.window_geometry(self.wid))
