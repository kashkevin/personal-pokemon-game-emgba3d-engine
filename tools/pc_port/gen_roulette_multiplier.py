#!/usr/bin/env python3
"""Draw the multiplier picture of the PC game's Roulette (graphics/roulette/multiplier_pc.png; the GBA game keeps multiplier.png).

The picture has five 32x16 frames: nothing, then the multipliers of the colour row's first hit (and of nothing else), the
second, the third and the last (a single square always pays the last). The GBA's are x3, x4, x6 and x12; the PC game's are
include/platform/casino_rates.h's CASINO_ROULETTE_MULT_1..4 (two digits at most). The frame's panel, the "x" and the
palette are the original's (tools/pc_port/data/roulette_multiplier_gba.png); only the number is drawn again.

    python3 tools/pc_port/gen_roulette_multiplier.py            # writes graphics/roulette/multiplier_pc.png
    python3 tools/pc_port/gen_roulette_multiplier.py --check    # exits 1 when the picture is not what the rates give
"""
import argparse
import os
import re
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ORIGINAL = os.path.join(ROOT, "tools/pc_port/data/roulette_multiplier_gba.png")
TARGET = os.path.join(ROOT, "graphics/roulette/multiplier_pc.png")
RATES = os.path.join(ROOT, "include/platform/casino_rates.h")

GLYPHS = {
    "0": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "3": [".###.", "#...#", "....#", "..##.", "....#", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#.", "...#."],
    "5": ["#####", "#....", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", "...#.", ".##.."],
}
BACKGROUND, BORDER, EDGE = 5, 10, 1             # palette indexes of the panel, its edge and the number's dark edge
COLOURS = {1: (4, 3), 2: (8, 11), 3: (15, 14), 4: (6, 12)}   # (top half, bottom half) of the number of frames 1 to 4


def multipliers():
    text = open(RATES, encoding="utf-8").read()
    return [int(re.search(r"#define CASINO_ROULETTE_MULT_%d (\d+)" % i, text).group(1)) for i in (1, 2, 3, 4)]


def render():
    mult = multipliers()
    assert all(0 < m < 100 for m in mult) and mult == sorted(mult), mult
    im = Image.open(ORIGINAL).copy()
    px = im.load()
    for frame in (1, 2, 3, 4):
        y0 = frame * 16
        for y in range(y0 + 3, y0 + 13):
            for x in range(14, 28):
                px[x, y] = BACKGROUND
        for x in range(14, 28):
            px[x, y0 + 13] = BORDER             # the panel's lower edge under the number
        digits = str(mult[frame - 1])
        left = 15 if len(digits) == 2 else 18
        ink = {}
        for n, digit in enumerate(digits):
            for dy, row in enumerate(GLYPHS[digit]):
                for dx, ch in enumerate(row):
                    if ch == "#":
                        ink[(left + n * 6 + dx, y0 + 4 + dy)] = COLOURS[frame][0 if dy < 4 else 1]
        for (x, y) in list(ink):                  # a dark edge round the number, as the original's
            for ex in (-1, 0, 1):
                for ey in (-1, 0, 1):
                    if (x + ex, y + ey) not in ink and 14 <= x + ex < 28:
                        px[x + ex, y + ey] = EDGE
        for (x, y), colour in ink.items():
            px[x, y] = colour
    return im


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    im = render()
    if args.check:
        current = Image.open(TARGET)
        same = im.tobytes() == current.tobytes() and im.getpalette() == current.getpalette()
        print("multiplier.png matches the rates" if same else "multiplier.png is not what the rates give")
        return 0 if same else 1
    im.save(TARGET, optimize=False)
    print("wrote", TARGET, "for", multipliers())
    return 0


if __name__ == "__main__":
    sys.exit(main())
