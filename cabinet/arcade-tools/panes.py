#!/usr/bin/env python3
"""Draw the cabinet's pop-up panes (button guide, shutdown confirm, game-start reminder).

The guide's text comes from controls.json, the one list of what every button does.
Writes OUT/<name>.rgba (uint32 width, uint32 height, RGBA rows) and, with --png, a PNG of each.

usage: panes.py OUT_DIR [--png]
"""
import json
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
CONTROLS = os.path.join(HERE, "controls.json")
FONT, BOLD = "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"
PANEL, EDGE, TEXT, DIM = (16, 20, 34, 236), (255, 196, 0, 255), (240, 240, 240, 255), (160, 168, 190, 255)
CHIP, CHIP_TEXT, ACCENT = (52, 60, 88, 255), (255, 255, 255, 255), (255, 196, 0, 255)
CHOICE, CHOSEN = (52, 60, 88, 255), (255, 196, 0, 255)


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)


def text_size(draw, text, f):
    return draw.textsize(text, font=f)  # Pillow 5.4 on the Pi


def rounded(draw, box, radius, fill, outline=None, width=0):
    """Pillow 5.4 has no rounded_rectangle: build one from rectangles and corner circles."""
    x0, y0, x1, y1 = box
    layers = [(outline, 0), (fill, width)] if outline else [(fill, 0)]
    for colour, inset in layers:
        a0, b0, a1, b1, r = x0 + inset, y0 + inset, x1 - inset, y1 - inset, max(radius - inset, 1)
        draw.rectangle((a0 + r, b0, a1 - r, b1), fill=colour)
        draw.rectangle((a0, b0 + r, a1, b1 - r), fill=colour)
        for cx, cy in ((a0, b0), (a1 - 2 * r, b0), (a0, b1 - 2 * r), (a1 - 2 * r, b1 - 2 * r)):
            draw.ellipse((cx, cy, cx + 2 * r, cy + 2 * r), fill=colour)


def canvas(w, h):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    rounded(draw, (0, 0, w - 1, h - 1), 28, PANEL, EDGE, 4)
    return img, draw


def centred(draw, w, y, text, f, fill):
    tw, _ = text_size(draw, text, f)
    draw.text(((w - tw) // 2, y), text, font=f, fill=fill)


def guide(controls):
    w, h = 1760, 960
    img, draw = canvas(w, h)
    centred(draw, w, 34, controls["title"], font(58, True), ACCENT)
    col_w, top = (w - 120) // 2, 130
    chip_f, desc_f, head_f = font(25, True), font(28), font(36, True)
    longest = max(len(s["rows"]) for s in controls["sections"])
    step = min(56, (h - top - 60 - 80) // longest)  # rows fit above the footer
    for i, section in enumerate(controls["sections"]):
        x, y = 60 + i * (col_w + 20), top
        draw.text((x, y), section["heading"], font=head_f, fill=TEXT)
        y += 60
        for chip, desc in section["rows"]:
            tw, th = text_size(draw, chip, chip_f)
            rounded(draw, (x, y, x + tw + 28, y + 44), 12, CHIP)
            draw.text((x + 14, y + (44 - th) // 2 - 2), chip, font=chip_f, fill=CHIP_TEXT)
            draw.text((x + tw + 44, y + 6), desc, font=desc_f, fill=TEXT)
            y += step
    centred(draw, w, h - 62, controls["footer"], font(28), DIM)
    return img


def shutdown(chosen, game_running):
    w, h = 1000, 400
    img, draw = canvas(w, h)
    centred(draw, w, 50, "Are you sure you want to shut down?", font(46, True), TEXT)
    if game_running:
        centred(draw, w, 115, "The game will be closed first.", font(30), DIM)
    for label, x in (("No", 220), ("Yes", 560)):
        on = label == chosen
        rounded(draw, (x, 190, x + 220, 280), 18, CHOSEN if on else CHOICE)
        f = font(44, True)
        tw, th = text_size(draw, label, f)
        draw.text((x + (220 - tw) // 2, 235 - th // 2 - 4), label, font=f, fill=(0, 0, 0, 255) if on else TEXT)
    centred(draw, w, 318, "Stick left / right to choose  ·  A to confirm  ·  B to cancel", font(28), DIM)
    return img


def message(text):
    w, h = 800, 200
    img, draw = canvas(w, h)
    f = font(48, True)
    centred(draw, w, (h - text_size(draw, text, f)[1]) // 2 - 6, text, f, TEXT)
    return img


def toast(text):
    """A small banner for the bottom of the screen while a game runs."""
    f = font(34, True)
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    tw, th = text_size(probe, text, f)
    w, h = tw + 80, 84
    img, draw = canvas(w, h)
    draw.text((40, (h - th) // 2 - 4), text, font=f, fill=TEXT)
    return img


def panes():
    controls = json.load(open(CONTROLS))
    out = {"guide": guide(controls), "shutting-down": message("Shutting down…"), "restarting": message("Restarting…"),
           "toast": toast(controls["toast"]), "launch": toast(controls["launch_toast"])}
    for chosen in ("No", "Yes"):
        for running in (False, True):
            out[f"shutdown-{chosen.lower()}{'-game' if running else ''}"] = shutdown(chosen, running)
    return out


def write_rgba(img, path):
    """OSError (e.g. /dev/shm full): path is unchanged and no tmp file is left taking space."""
    tmp = path + ".tmp"
    try:
        with open(tmp, "wb") as fh:
            fh.write(struct.pack("<II", *img.size))
            fh.write(img.tobytes())
        os.replace(tmp, path)
    except OSError:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def main():
    assert len(sys.argv) >= 2, __doc__
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    for name, img in panes().items():
        write_rgba(img, os.path.join(out_dir, f"{name}.rgba"))
        if "--png" in sys.argv:
            img.save(os.path.join(out_dir, f"{name}.png"))
    print(f"panes written to {out_dir}")


if __name__ == "__main__":
    main()
