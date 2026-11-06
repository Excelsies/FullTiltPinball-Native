#!/usr/bin/env python3
"""Add the cabinet's button hints (controls.json "theme_hints") to every installed theme.

Each theme's systems all include one top-level file; a marked block is added (or replaced) just
before its closing </theme>, with one text element for the system list and one for the game
lists, placed where that theme has free space (LAYOUTS, checked by screenshot). Themes without a
layout are reported and left alone. The first time a file is changed its original goes to
~pi/old-setup/themes/. Re-runnable (also after theme updates); restart EmulationStation after.

usage: sudo theme_hints.py [--remove]   (--remove takes the hints out of every theme again)
"""
import json
import os
import re
import shutil
import sys

THEMES = "/etc/emulationstation/themes"
HERE = os.path.dirname(os.path.abspath(__file__))
CONTROLS = os.path.join(HERE, "controls.json")
BACKUP = os.path.expanduser("~pi/old-setup/themes")
START, END = "<!-- cabinet-hints: start (theme_hints.py) -->", "<!-- cabinet-hints: end -->"
GAMELIST_VIEWS = "basic, detailed, video, grid"

# Per theme: the file every system includes, the font, and per view: x y, w h (screen fractions),
# alignment, colour (RRGGBBAA), font size, and optionally hints per line (default: all on one).
# "games_extra": theme XML added to the game-list views (retrorama: its header art differs per
# system, so the hint goes at the foot of the game list, which is shortened slightly to make room).
LAYOUTS = {
    "back2basics": {"root": "theme.xml", "font": "./_assets/Roboto-Medium.ttf",
                    "system": ("0.40 0.006", "0.53 0.055", "right", "ffffffdd", "0.02", 2),
                    "games": ("0.40 0.006", "0.53 0.055", "right", "ffffffdd", "0.02", 2)},
    "bluray": {"root": "theme.xml", "font": "./_inc/fonts/Roboto-Medium.ttf",
               "system": ("0.29 0.958", "0.70 0.03", "right", "8ec9ecff", "0.018"),
               "games": ("0.25 0.958", "0.74 0.03", "right", "8ec9ecff", "0.018")},
    "carbon": {"root": "carbon.xml", "font": "./art/Cabin-Bold.ttf",
               "system": ("0.53 0.945", "0.46 0.05", "right", "bbbbbbff", "0.02", 2),
               "games": ("0.66 0.935", "0.33 0.06", "right", "bbbbbbff", "0.016", 2)},
    "carbon-2021": {"root": "theme.xml", "font": "${themeFont}",
                    "system": ("0.53 0.945", "0.46 0.05", "right", "bbbbbbff", "0.02", 2),
                    "games": ("0.66 0.935", "0.33 0.06", "right", "bbbbbbff", "0.016", 2)},
    "greenilicious": {"root": "theme.xml", "font": "./_inc/fonts/impact.ttf",
                      "system": ("0.28 0.008", "0.70 0.035", "right", "0d2600ff", "0.026"),
                      "games": ("0.28 0.008", "0.70 0.035", "right", "0d2600ff", "0.026")},
    "whitewood": {"root": "theme.xml", "font": "./_inc/fonts/impact.ttf",
                  "system": ("0.38 0.012", "0.42 0.06", "center", "000000ff", "0.024", 2),
                  "games": ("0.03 0.006", "0.60 0.03", "left", "000000ff", "0.022")},
    "retrorama": {"root": "theme.xml", "font": "./_art/comic.otf",
                  "system": ("0.01 0.008", "0.62 0.065", "left", "ffcc33ff", "0.022", 2),
                  "games": ("0.04 0.868", "0.415 0.058", "right", "ffcc33ff", "0.0155", 2),
                  "games_extra": '    <textlist name="gamelist">\n      <size>0.415 0.63</size>\n'
                                 '    </textlist>\n'},
}


def hint_text(pairs, per_line):
    hints = [f"{button}: {action}".upper() for button, action in pairs]
    lines = ["  |  ".join(hints[i:i + per_line]) for i in range(0, len(hints), per_line)]
    return "\n".join(lines)


def in_units(layout, resolution):
    """A theme that declares <resolution>W H</resolution> gives positions and font sizes in pixels."""
    pos, size, align, color, font_size = layout[:5]
    if resolution:
        w, h = resolution
        pixels = lambda pair: " ".join(str(round(float(v) * d)) for v, d in zip(pair.split(), (w, h)))
        pos, size, font_size = pixels(pos), pixels(size), str(round(float(font_size) * h))
    return pos, size, align, color, font_size


def text_element(name, pairs, layout, font, resolution):
    pos, size, align, color, font_size = in_units(layout, resolution)
    text = hint_text(pairs, layout[5] if len(layout) > 5 else len(pairs))
    return (f'    <text name="{name}" extra="true">\n'
            f"      <text>{text}</text>\n"
            f"      <pos>{pos}</pos>\n      <size>{size}</size>\n      <origin>0 0</origin>\n"
            f"      <alignment>{align}</alignment>\n      <color>{color}</color>\n"
            f"      <fontPath>{font}</fontPath>\n      <fontSize>{font_size}</fontSize>\n"
            f"      <zIndex>60</zIndex>\n    </text>\n")


def block(layout, hints, resolution):
    return (f"{START}\n"
            f'  <view name="system">\n'
            + text_element("cabinetHintsSystem", hints["system"], layout["system"], layout["font"], resolution)
            + f"  </view>\n"
            f'  <view name="{GAMELIST_VIEWS}">\n'
            + text_element("cabinetHintsGames", hints["games"], layout["games"], layout["font"], resolution)
            + layout.get("games_extra", "")
            + f"  </view>\n{END}\n")


def apply(theme, layout, hints, remove=False):
    path = os.path.join(THEMES, theme, layout["root"])
    text = open(path).read()
    assert text.count("</theme>") == 1, f"{path}: expected one </theme>"
    backup = os.path.join(BACKUP, theme, layout["root"])
    if not os.path.exists(backup):
        os.makedirs(os.path.dirname(backup), exist_ok=True)
        shutil.copy2(path, backup)
    if START in text:
        assert END in text, f"{path}: hint block start without end"
        text = text[:text.index(START)] + text[text.index(END) + len(END) + 1:]
    if not remove:
        found = re.search(r"<resolution>\s*(\d+)\s+(\d+)\s*</resolution>", text)
        resolution = tuple(int(v) for v in found.groups()) if found else None
        text = text.replace("</theme>", block(layout, hints, resolution) + "</theme>")
    tmp = path + ".tmp"
    open(tmp, "w").write(text)
    os.replace(tmp, path)


def main():
    assert os.geteuid() == 0, "run with sudo (themes are in /etc/emulationstation)"
    remove = "--remove" in sys.argv
    hints = json.load(open(CONTROLS))["theme_hints"]
    installed = sorted(d for d in os.listdir(THEMES) if os.path.isdir(os.path.join(THEMES, d)))
    for theme in installed:
        if theme in LAYOUTS:
            apply(theme, LAYOUTS[theme], hints, remove)
            print(f"hints {'removed' if remove else 'added'}: {theme}")
        else:
            print(f"NO LAYOUT for theme {theme}: hints not added (add it to LAYOUTS)")
    missing = sorted(set(LAYOUTS) - set(installed))
    if missing:
        print(f"layouts for themes that are not installed: {missing}")
    return 0 if set(installed) <= set(LAYOUTS) else 1


if __name__ == "__main__":
    sys.exit(main())
