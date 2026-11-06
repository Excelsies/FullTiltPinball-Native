"""Audio probe for one harness target: does the target really have audio, and did anything reach
the host's audio server or device?

  python re/test/scripts/audio_probe.py <target> --display N --out DIR [--settle 6]

Starts the target in a Session, presses F2 (new game, which loads the table's sounds), waits
``--settle`` seconds, then writes DIR/audio_probe.json:
- Wine targets: the original's DirectSound object pointer, the sound_list count and the number
  of sounds with a non-zero duration, read with ftpeek.exe (globals from targets.json
  "audio_probe": DirectSound pointer, sound_list base, count). Without a sound device the
  original loads no wave: pointer 0, every duration 0.
- Native targets: whether the command line has -noaudio, the "could not open audio device" line
  of target.log, whether libSDL2_mixer is mapped, and the SDL audio thread names.
- Host side, three samples (after start, after F2, after settle): every PipeWire/PulseAudio
  stream or client and every open file descriptor of the target's processes (those of the
  target's session) that points at /dev/snd, a pulse or pipewire socket. "leak" is true when a
  stream of one of those processes exists or a descriptor points there; PipeWire clients without
  a stream are counted separately (SDL's PipeWire audio plug of a -noaudio native run).
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ftharness import session as session_mod  # noqa: E402
from ftharness.session import HarnessError, Session, load_targets, wine_env  # noqa: E402

FTPEEK = Path(__file__).resolve().parents[2] / "work" / "evidence" / "reference" / "2026-11-01" / "tools" / "ftpeek.exe"
SOUND_STRIDE = 0x14
DURATION_OFF = 0xC


def session_pids(sid: int) -> list[int]:
    """Pids whose POSIX session id is ``sid`` (the harness starts each target with setsid)."""
    pids = []
    for d in Path("/proc").iterdir():
        if d.name.isdigit():
            try:
                stat = (d / "stat").read_text()
                if int(stat.rsplit(")", 1)[1].split()[3]) == sid:
                    pids.append(int(d.name))
            except (OSError, ValueError, IndexError):
                continue
    return pids


def fd_links(pids: list[int]) -> list[dict]:
    hits = []
    for pid in pids:
        try:
            comm = (Path("/proc") / str(pid) / "comm").read_text().strip()
            for fd in (Path("/proc") / str(pid) / "fd").iterdir():
                try:
                    link = os.readlink(fd)
                except OSError:
                    continue
                if "/dev/snd" in link or "pulse" in link or "pipewire" in link:
                    hits.append({"pid": pid, "comm": comm, "fd": fd.name, "link": link})
        except OSError:
            continue
    return hits


def pipewire_objects() -> list[dict]:
    res = subprocess.run(["pw-dump"], capture_output=True, text=True, timeout=30)
    if res.returncode != 0:
        raise HarnessError(f"pw-dump failed rc={res.returncode}: {res.stderr.strip()[:200]}")
    out = []
    for o in json.loads(res.stdout):
        t = o["type"].split(":")[-1]
        if t not in ("Node", "Client"):
            continue
        p = (o.get("info") or {}).get("props", {})
        if t == "Node" and "Stream" not in p.get("media.class", ""):
            continue
        out.append({"type": t, "id": o["id"], "name": p.get("application.name") or p.get("node.name"),
                    "pid": p.get("application.process.id") or p.get("pipewire.sec.pid"),
                    "binary": p.get("application.process.binary")})
    return out


def host_sample(label: str, sess: Session) -> dict:
    pids = session_pids(sess.proc.pid)
    pw = pipewire_objects()
    mine = [o for o in pw if o["pid"] is not None and int(o["pid"]) in pids]
    return {"label": label, "target_pids": len(pids), "pipewire_objects": len(pw),
            "pipewire_objects_of_target": mine, "fds_to_audio": fd_links(pids)}


def peek(sess: Session, exe: str, specs: list[str]) -> list[int | None]:
    res = subprocess.run(["wine", str(FTPEEK), exe, "1", "1", *specs], capture_output=True,
                         text=True, env=wine_env(sess.display, sess.target), timeout=120)
    lines = [ln for ln in res.stdout.splitlines() if ln.strip()]
    parts = lines[0].split("\t") if lines else []
    if res.returncode != 0 or len(parts) != len(specs) + 1:
        raise HarnessError(f"ftpeek rc={res.returncode}: {res.stderr.strip()[:200]} / {res.stdout[:200]}")
    return [None if v == "?" else int(v) for v in parts[1:]]


def wine_audio(sess: Session) -> dict:
    ap = sess.target["audio_probe"]
    exe = sess.target["exe"]
    dsound, count = peek(sess, exe, [ap["dsound"], ap["count"]])
    base = int(ap["list"], 16)
    n = max(0, min((count or 0) - 1, 62))
    specs = [f"{base + i * SOUND_STRIDE + DURATION_OFF:x}" for i in range(1, n + 1)]
    raw = peek(sess, exe, specs) if specs else []
    durs = [struct.unpack("<f", struct.pack("<i", v))[0] if v is not None else None for v in raw]
    nonzero = [(i + 1, round(d, 3)) for i, d in enumerate(durs) if d]
    # NewGame's sound: table object pointer global -> +ng_sound_off = SoundIndex1 (export: NewGame passes
    # *(this+0x5e) to the play_sound function whose result is the light-show timer), then that entry's duration.
    ng_idx, = peek(sess, exe, [f"{ap['table']}:{ap['ng_sound_off']}"])
    ng_dur = None
    if ng_idx is not None and 0 < ng_idx < (count or 0):
        raw_ng, = peek(sess, exe, [f"{base + ng_idx * SOUND_STRIDE + DURATION_OFF:x}"])
        ng_dur = None if raw_ng is None else struct.unpack("<f", struct.pack("<i", raw_ng))[0]
    zero_idx = [i + 1 for i, d in enumerate(durs) if not d]
    return {"directsound_ptr": dsound, "directsound_ptr_hex": f"{(dsound or 0) & 0xffffffff:#x}",
            "sound_list_count": count, "durations_read": len(durs), "durations_read_indices": "1..%d" % len(durs),
            "durations_nonzero": len(nonzero), "durations_zero_indices": zero_idx,
            "durations_nonzero_sample": nonzero[:12],
            "newgame_sound_index": ng_idx, "newgame_sound_duration": ng_dur}


def native_audio(sess: Session) -> dict:
    pids = session_pids(sess.proc.pid)
    log = (sess.out_dir / "target.log").read_text(errors="replace")
    threads, mixer = set(), False
    for pid in pids:
        base = Path("/proc") / str(pid)
        try:
            threads |= {(t / "comm").read_text().strip() for t in (base / "task").iterdir()}
            mixer |= "libSDL2_mixer" in (base / "maps").read_text()
        except OSError:
            continue
    return {"noaudio_flag": "-noaudio" in sess.target.get("args", []),
            "sdl_audiodriver_env": sess.target.get("env", {}).get("SDL_AUDIODRIVER"),
            "open_audio_failed_line": "Could not open audio device" in log,
            "mixer_library_mapped": mixer,
            "audio_threads": sorted(t for t in threads if "udio" in t or "SDL" in t)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("--display", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--settle", type=float, default=6.0)
    ap.add_argument("--set-env", action="append", default=[], metavar="K=V",
                    help="native only: extra environment for the target (negative controls)")
    args = ap.parse_args()
    out = Path(args.out).resolve()
    if args.target not in load_targets():
        raise HarnessError(f"unknown target {args.target}")
    if args.set_env:
        patched = load_targets()
        patched[args.target] = dict(patched[args.target], env=dict(
            patched[args.target].get("env", {}), **dict(kv.split("=", 1) for kv in args.set_env)))
        session_mod.load_targets = lambda: patched
    samples = []
    with Session(args.target, args.display, out) as sess:
        time.sleep(3.0)
        samples.append(host_sample("loaded", sess))
        sess.xdo("key", "F2")
        time.sleep(2.0)
        samples.append(host_sample("new_game+2s", sess))
        time.sleep(args.settle)
        samples.append(host_sample("settled", sess))
        result = {"target": args.target, "kind": sess.target["kind"]}
        result.update(wine_audio(sess) if sess.target["kind"] == "wine" else native_audio(sess))
    result["host_samples"] = samples
    mine = [o for s in samples for o in s["pipewire_objects_of_target"]]
    result["pipewire_streams_of_target"] = sum(o["type"] == "Node" for o in mine)
    result["pipewire_clients_of_target"] = sum(o["type"] == "Client" for o in mine)
    result["fds_to_audio"] = sum(len(s["fds_to_audio"]) for s in samples)
    # leak = a stream (something that could be heard) or an open /dev/snd, pulse or pipewire
    # descriptor; a bare PipeWire client without a stream is reported separately.
    result["leak"] = bool(result["pipewire_streams_of_target"] or result["fds_to_audio"])
    (out / "audio_probe.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "host_samples"}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except HarnessError as exc:
        print(f"audio_probe: {exc}", file=sys.stderr)
        sys.exit(2)
