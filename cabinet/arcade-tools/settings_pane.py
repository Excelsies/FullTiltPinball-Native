"""Draw the cabinet settings menu's panes in the panes.py look (same colours, fonts, corners).

Every view is written to OUT (panes.write_rgba) for the overlay. The list view keeps a base image
(panel, title, status, footer, row labels); a move redraws and rewrites only the rows and the chosen
item's description (a fixed area under the list) that changed.
"""
import json
import os

from PIL import ImageDraw

import panes
from panes import ACCENT, CHIP, CHOICE, CHOSEN, DIM, TEXT, canvas, centred, font, rounded, text_size

OUT = os.path.join("/dev/shm/cabinet", "settings.rgba")
BLACK = (0, 0, 0, 255)
W, TOP, ROW = 840, 120, 50  # kept small: redraw time grows with the pane's pixels
DESC_LINES, DESC_STEP, DESC_X = 3, 30, 40  # description area: lines, line step, side margin
DESC_TOP = 16  # gap from the last row to the first description line (a divider sits in it)
BOTTOM = DESC_TOP + DESC_LINES * DESC_STEP + 72
SEGMENTS = 20
MESSAGE_LINES = 14  # message view: detail lines after wrapping
FOOTER = "Stick: choose  ·  ◄ ► change  ·  A select  ·  B close"


class Fonts:
    def __init__(self):
        self.title, self.label, self.value = font(46, True), font(34, True), font(30)
        self.small, self.chip, self.line, self.desc = font(26), font(44, True), font(30), font(25)


F = Fonts()


def wrap(text, width=W - 2 * DESC_X, f=F.desc):
    """text in lines of at most width pixels in font f; default: the description area (may be more
    than DESC_LINES lines)."""
    probe = ImageDraw.Draw(panes.Image.new("RGBA", (1, 1)))
    lines, line = [], ""
    for word in text.split():
        trial = f"{line} {word}".strip()
        if line and text_size(probe, trial, f)[0] > width:
            lines.append(line)
            trial = word
        line = trial
    return lines + ([line] if line else [])


def load_help(path, keys):
    """{item: description lines} from the JSON file at path; problems (unreadable, missing, too long)
    are returned, not raised: the menu logs them and shows a note in the description area."""
    try:
        with open(path) as fh:
            texts = json.load(fh)
    except (OSError, ValueError) as e:
        return {}, [f"descriptions unreadable: {path}: {e}"]
    lines, problems = {}, []
    for key in keys:
        text = texts.get(key) if isinstance(texts, dict) else None
        if not isinstance(text, str) or not text.strip():
            problems.append(f"no description for '{key}' in {path}")
            continue
        lines[key] = wrap(text)
        if len(lines[key]) > DESC_LINES:
            problems.append(f"description of '{key}' needs {len(lines[key])} lines, the pane has "
                            f"{DESC_LINES}: shorten it in {path}")
            lines[key] = lines[key][:DESC_LINES - 1] + ["(description too long: see the log)"]
    return lines, problems


def _right(draw, x, y, text, f, fill):
    draw.text((x - text_size(draw, text, f)[0], y), text, font=f, fill=fill)


_in_file = [None]  # the ListPane whose image OUT holds now (None: another view wrote it)


def _write(img, owner=None):
    panes.write_rgba(img, OUT)
    _in_file[0] = owner


class ListPane:
    """The list view. A stick move redraws and rewrites only the bands (rows, description) that
    changed: the overlay's file is patched in place, which keeps a move well under 150 ms."""

    def __init__(self):
        self.key, self.base, self.img, self.drawn = None, None, None, {}

    def _base(self, labels, status):
        h = TOP + ROW * len(labels) + BOTTOM
        img, draw = canvas(W, h)
        centred(draw, W, 18, "Cabinet settings", F.title, ACCENT)
        if status:
            centred(draw, W, 76, status, F.small, DIM)
        for i, label in enumerate(labels):
            draw.text((56, TOP + i * ROW + 4), label, font=F.label, fill=TEXT)
        divider = TOP + ROW * len(labels) + DESC_TOP // 2 - 4
        draw.rectangle((DESC_X, divider, W - DESC_X, divider + 1), fill=CHIP)
        centred(draw, W, h - 48, FOOTER, F.small, DIM)
        return img

    def draw(self, items, cursor, status, description):
        """items: [(label, value)]; value None, a text, or ("volume", percent, muted);
        description: the chosen item's lines (wrap()), at most DESC_LINES. If drawing or writing fails
        (raised to the menu), the next draw writes the whole pane again."""
        try:
            self._draw(items, cursor, status, description)
        except Exception:
            _in_file[0] = None
            raise

    def _draw(self, items, cursor, status, description):
        assert len(description) <= DESC_LINES, description
        key = (tuple(label for label, _ in items), status)
        whole = key != self.key or _in_file[0] is not self
        if key != self.key:
            self.key, self.base = key, self._base(key[0], status)
        if whole:
            self.img, self.drawn = self.base.copy(), {}
        bands = {i: (TOP + i * ROW, (label, value, i == cursor)) for i, (label, value) in enumerate(items)}
        bands["desc"] = (TOP + ROW * len(items) + DESC_TOP, tuple(description))
        changed = [(k, y, state) for k, (y, state) in bands.items() if self.drawn.get(k) != state]
        draw = ImageDraw.Draw(self.img)
        for k, y, state in changed:
            self.img.paste(self.base.crop((0, y, W, y + self._height(k))), (0, y))
            if k == "desc":
                self._description(draw, y, state)
            else:
                self._row(draw, y, *state)
            self.drawn[k] = state
        if whole:
            return _write(self.img, self)
        fd = os.open(OUT, os.O_WRONLY)
        try:
            for k, y, _ in changed:  # rows are full width, so a band is one run of bytes
                band = self.img.crop((0, y, W, y + self._height(k))).tobytes()
                if os.pwrite(fd, band, 8 + y * W * 4) != len(band):
                    raise OSError(f"short write to {OUT}")
        finally:
            os.close(fd)

    @staticmethod
    def _height(band):
        return DESC_LINES * DESC_STEP if band == "desc" else ROW

    def _row(self, draw, y, label, value, on):
        if on:
            rounded(draw, (24, y, W - 24, y + 46), 14, CHOSEN)
            draw.text((56, y + 4), label, font=F.label, fill=BLACK)
        if isinstance(value, tuple):
            self._volume(draw, y, value[1], value[2], on)
        elif value:
            _right(draw, W - 56, y + 7, value, F.value, BLACK if on else DIM)

    @staticmethod
    def _description(draw, top, lines):
        for i, line in enumerate(lines):
            draw.text((DESC_X, top + i * DESC_STEP), line, font=F.desc, fill=TEXT)

    @staticmethod
    def _volume(draw, y, percent, muted, on):
        text = "muted" if muted else f"{percent} %"
        _right(draw, W - 56, y + 7, text, F.value, BLACK if on else (DIM if muted else TEXT))
        right, lit = W - 56 - 118, round(percent / (100 / SEGMENTS))
        full = (105, 105, 115, 255) if muted else (BLACK if on else ACCENT)
        empty = (150, 112, 0, 255) if on else CHIP
        for s in range(SEGMENTS):
            x = right - (SEGMENTS - s) * 13
            draw.rectangle((x, y + 11, x + 9, y + 35), fill=full if s < lit else empty)


def confirm(title, lines, choice):
    """Title, detail lines, No / Yes chips (choice = "No" or "Yes"), as the shutdown confirm."""
    w, top = 1000, 112
    chips = top + 44 * len(lines) + 24
    h = chips + 90 + 110
    img, draw = canvas(w, h)
    centred(draw, w, 46, title, font(44, True), TEXT)
    for i, line in enumerate(lines):
        centred(draw, w, top + 44 * i, line, F.line, DIM)
    for label, x in (("No", 220), ("Yes", 560)):
        on = label == choice
        rounded(draw, (x, chips, x + 220, chips + 90), 18, CHOSEN if on else CHOICE)
        tw, th = text_size(draw, label, F.chip)
        draw.text((x + (220 - tw) // 2, chips + 45 - th // 2 - 4), label, font=F.chip, fill=BLACK if on else TEXT)
    centred(draw, w, h - 72, "Stick left / right to choose  ·  A to confirm  ·  B back", F.small, DIM)
    _write(img)


def info(rows):
    """System info: [(label, text)]."""
    w, top, step = 1180, 120, 52
    h = top + step * len(rows) + 80
    img, draw = canvas(w, h)
    centred(draw, w, 30, "System info", font(46, True), ACCENT)
    for i, (label, text) in enumerate(rows):
        y = top + i * step
        draw.text((50, y), label, font=font(30, True), fill=DIM)
        draw.text((330, y), text, font=F.line, fill=TEXT)
    centred(draw, w, h - 56, "Any button: back", F.small, DIM)
    _write(img)


def message(lines, footer="A or B: back"):
    """First line large, the rest small (details, error output: wrapped, at most MESSAGE_LINES lines);
    footer None while busy."""
    w, small = 1100, font(26)
    body = [part for line in lines[1:] for part in wrap(line, w - 100, small)][:MESSAGE_LINES]
    h = 70 + 64 + 40 * len(body) + (90 if footer else 50)
    img, draw = canvas(w, h)
    centred(draw, w, 50, lines[0], font(44, True), TEXT)
    for i, line in enumerate(body):
        centred(draw, w, 130 + 40 * i, line, small, DIM)
    if footer:
        centred(draw, w, h - 62, footer, F.small, DIM)
    _write(img)
