#!/usr/bin/env python3
"""Testing aid: time the settings menu's redraw per stick move (draw + write, and the overlay's show),
with the real item descriptions (settings_help.json) in the description area.

Uses a second overlay process (shows the pane on screen for a few seconds; nothing else should
be showing a pane). The real menu also logs its own redraw times when it closes (journal:
"settings menu closed (redraws N, median X ms, max Y ms)").

usage: sudo settings_bench.py [MOVES]
"""
import os
import statistics
import subprocess
import sys
import time

sys.path.insert(0, "/home/pi/arcade-tools")
import settings_pane  # noqa: E402

settings_pane.OUT = "/dev/shm/cabinet/settings-bench.rgba"  # never the menu's own file (patched in place)

KEYS = ["volume", "mute", "theme", "guide", "info", "lock", "restart", "shutdown", "close"]
ITEMS = ["Volume", "Mute", "Theme", "Button guide", "System info", "Lock cabinet", "Restart",
         "Shut down", "Close"]
THEMES = ["back2basics", "bluray", "carbon", "carbon-2021 (in use)", "greenilicious", "retrorama", "whitewood"]


def main():
    moves = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    overlay = subprocess.Popen(["/home/pi/arcade-tools/overlay"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               universal_newlines=True)
    pane, draw_ms, show_ms = settings_pane.ListPane(), [], []
    help_lines, problems = settings_pane.load_help("/home/pi/arcade-tools/settings_help.json", KEYS)
    assert not problems, problems
    for i in range(moves):
        items = [(label, None) for label in ITEMS]
        items[0] = ("Volume", ("volume", (i * 5) % 105, False))
        items[1] = ("Mute", "Off")
        items[2] = ("Theme", THEMES[i % len(THEMES)])  # the Theme row cycling, as the stick does
        t = time.monotonic()
        pane.draw(items, i % len(ITEMS), "Game paused", help_lines[KEYS[i % len(ITEMS)]])
        t2 = time.monotonic()
        overlay.stdin.write(f"show {settings_pane.OUT}\n")
        overlay.stdin.flush()
        reply = overlay.stdout.readline().strip()
        assert reply == "ok", f"overlay: {reply or 'no reply'}"
        draw_ms.append((t2 - t) * 1000)
        show_ms.append((time.monotonic() - t2) * 1000)
    # A patched file must equal a full redraw of the same state (the last move's items).
    with open(settings_pane.OUT, "rb") as fh:
        patched = fh.read()
    settings_pane.ListPane().draw(items, (moves - 1) % len(ITEMS), "Game paused", help_lines[KEYS[(moves - 1) % len(ITEMS)]])
    with open(settings_pane.OUT, "rb") as fh:
        assert fh.read() == patched, "patched pane differs from a full redraw"
    print("patched pane file == full redraw: yes")
    overlay.stdin.close()
    rc = overlay.wait()
    total = [a + b for a, b in zip(draw_ms, show_ms)]
    for name, v in (("draw+write", draw_ms), ("overlay show", show_ms), ("total", total)):
        print(f"{name:12s} min {min(v):6.1f}  median {statistics.median(v):6.1f}  max {max(v):6.1f} ms")
    print(f"first move (base drawn) {total[0]:.1f} ms; overlay rc {rc}; moves {moves}")
    os.remove(settings_pane.OUT)
    return 0 if statistics.median(total[1:]) < 150 else 1


if __name__ == "__main__":
    sys.exit(main())
