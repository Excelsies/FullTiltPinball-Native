"""Pause the running game while the cabinet settings menu is open: SIGSTOP its process tree, SIGCONT
on close. A stopped emulator executes nothing and sees no key, so it cannot end up in a different
state (a toggle pause key could un-pause a game the player had paused, or open a menu).
Only emulators measured on the cabinet (design.md section 5) are paused. The stopped pids are kept in
STATE (tmpfs) so the next cabinetd start resumes them if cabinetd was killed while a game was paused.
"""
import json
import os
import signal

# Process names (comm, 15 characters) measured safe: audio stops cleanly and re-starts by itself.
PAUSE_OK = {"SpaceCadetPinba", "retroarch", "mupen64plus", "drastic"}
STATE = "/run/cabinet/paused.json"


def tree(pid, procs):
    """pid first, then its descendants (breadth first) from {pid: (ppid, comm)}."""
    found, i = [pid], 0
    while i < len(found):
        found += [p for p, (pp, _) in procs.items() if pp == found[i] and p not in found]
        i += 1
    return found


def state(pid):
    """Process state letter (T = stopped), "gone" if it has ended."""
    try:
        with open(f"/proc/{pid}/stat") as fh:
            return fh.read().rsplit(")", 1)[1].split()[0]
    except (OSError, IndexError):
        return "gone"


def _write_state(pids):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + ".tmp", "w") as fh:
        json.dump(pids, fh)
    os.replace(STATE + ".tmp", STATE)


def pause(pid, procs, log):
    """Stop the game's tree (parent first); returns the pids. The caller checks state() later.
    [] (logged, nothing stopped) if STATE cannot be written: a game cabinetd could not resume after
    a crash is worse than one left running under the menu."""
    pids = tree(pid, procs)
    try:
        _write_state(pids)  # before stopping: a crash right after still leaves the list
    except OSError as e:
        log(f"pause: {STATE} not written ({e}); the game keeps running")
        return []
    for p in pids:
        try:
            os.kill(p, signal.SIGSTOP)
        except ProcessLookupError:
            continue
    return pids


def resume(pids):
    """SIGCONT in reverse order; returns the pids that had already ended."""
    gone = []
    for p in reversed(pids):
        try:
            os.kill(p, signal.SIGCONT)
        except ProcessLookupError:
            gone.append(p)
    if os.path.exists(STATE):
        os.remove(STATE)
    return gone


def recover(log):
    """At cabinetd start: resume anything a killed cabinetd left stopped."""
    if not os.path.exists(STATE):
        return
    try:
        with open(STATE) as fh:
            pids = [int(p) for p in json.load(fh)]
    except (OSError, ValueError, TypeError) as e:
        log(f"recover: {STATE} unreadable ({e}); removed")
        os.remove(STATE)
        return
    gone = resume(pids)
    log(f"recover: SIGCONT {[p for p in pids if p not in gone]} (ended already: {gone})")
