#!/usr/bin/env python
"""List the throw directions of every KICKER record (record type 400) in Full Tilt .DAT files and
say whether Full Tilt's loader normalisation changes them.

Full Tilt's loader::kicker (0x40d290, same code in CADET/DRAGON/PIRATES.EXE) ends both the 404
(direction vector) and the 410 (two points) paths with maths::normalize_2d on ThrowBallDirection
(X/Y scaled to unit length, Z untouched). Upstream's loader::kicker copies 404 without normalising
and does not know 410. This prints, per kicker group and direction attribute, the raw X/Y length;
a length that is not 1 means upstream and Full Tilt throw differently for that kicker.

usage: dat_kicker_dirs.py DAT...
"""
import math
import sys

from datfile import Dat

KICKER = 400
# float attribute id -> number of floats after the id (loader::kicker, upstream + FT 410)
SIZES = {401: 1, 402: 1, 403: 1, 404: 3, 405: 1, 406: 1, 410: 8}


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        d = Dat(path)
        changed = total = 0
        for g in range(len(d.groups)):
            if d.record(g) != KICKER or not d.floats(g):
                continue
            fa, j = d.floats(g)[0], 0
            while j < len(fa):
                aid = int(fa[j] // 1)
                n = SIZES.get(aid)
                if n is None:
                    print(f"{path}: group {g}: unknown kicker attr {aid}, stop")
                    break
                if aid in (404, 410):
                    v = fa[j + 1:j + 4] if aid == 404 else [fa[j + 5] - fa[j + 1], fa[j + 6] - fa[j + 2],
                                                            fa[j + 7] - fa[j + 3]]
                    xy = math.hypot(v[0], v[1])
                    total += 1
                    differs = abs(xy - 1.0) > 1e-6
                    changed += differs
                    print(f"{path}: group {g} attr {aid} dir=({v[0]:.4f},{v[1]:.4f},{v[2]:.4f}) "
                          f"|xy|={xy:.6f} {'NORMALISATION CHANGES IT' if differs else 'unit'}")
                j += 1 + n
        print(f"{path}: {total} direction attrs, {changed} not unit in X/Y")


if __name__ == "__main__":
    main()
