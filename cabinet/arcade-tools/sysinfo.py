"""System information rows for the cabinet settings menu: [(label, text)], each source on its own.

A source that fails shows "unavailable (reason)" in its row and is logged by the caller.
"""
import os
import subprocess

import cabinet_lock

THROTTLE_BITS = {0: "Under-voltage", 1: "CPU speed capped", 2: "Throttled", 3: "Soft temperature limit"}


def _run(args):
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True,
                          timeout=5)
    if proc.returncode != 0:
        raise OSError(f"{args[0]} rc {proc.returncode}: {proc.stdout.strip()[-120:]}")
    return proc.stdout.strip()


def ip_addresses():
    return "   ".join(_run(["hostname", "-I"]).split()) or "no network"


def temperature():
    with open("/sys/class/thermal/thermal_zone0/temp") as fh:
        return f"{int(fh.read()) / 1000:.0f} °C"


def throttling(raw=None):
    """vcgencmd get_throttled (throttled=0x50005) in words."""
    raw = raw or _run(["vcgencmd", "get_throttled"])
    mask = int(raw.split("=", 1)[1], 16)
    now = [f"{name} NOW" for bit, name in THROTTLE_BITS.items() if mask & (1 << bit)]
    since = [name for bit, name in THROTTLE_BITS.items() if mask & (1 << (bit + 16)) and not mask & (1 << bit)]
    if not now and not since:
        return "OK (no problems since start-up)"
    return ", ".join(now + ([", ".join(since) + " since start-up"] if since else []))


def lock_state():
    text = "Locked (read-only)" if cabinet_lock.is_locked() else "Unlocked (maintenance mode)"
    return text + (" - changes at next restart" if cabinet_lock.pending() else "")


def _size(n):
    return f"{n / 1e9:.1f} GB" if n >= 1e9 else f"{n / 1e6:.0f} MB"


def free_space(path):
    st = os.statvfs(path)
    return f"{_size(st.f_bavail * st.f_frsize)} free of {_size(st.f_blocks * st.f_frsize)}"


def uptime():
    with open("/proc/uptime") as fh:
        minutes = int(float(fh.read().split()[0]) // 60)
    return f"{minutes // 1440} d {minutes // 60 % 24} h {minutes % 60} min" if minutes >= 1440 else \
        f"{minutes // 60} h {minutes % 60} min"


def rows(log):
    system_note = " (locked: changes are held in memory)" if cabinet_lock.is_locked() else ""
    sources = [("IP address", ip_addresses), ("CPU temperature", temperature), ("Power", throttling),
               ("Lock state", lock_state), ("System space", lambda: free_space("/") + system_note),
               ("Saves & settings", lambda: free_space("/mnt/retrodata")), ("Running for", uptime)]
    out = []
    for label, fn in sources:
        try:
            out.append((label, fn()))
        except (OSError, ValueError, IndexError, subprocess.SubprocessError) as e:
            log(f"settings: system info {label} unavailable: {e}")
            out.append((label, f"unavailable ({e})"[:70]))
    return out
