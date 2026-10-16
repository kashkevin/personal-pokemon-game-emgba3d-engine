#!/usr/bin/env python3
"""Regression check (Update 27): no animation background leaves a black block at the bottom of the 3D battle picture.

    tools/battlefx/black_rect_check.py RUN_DIR [RUN_DIR ...] [--list]

RUN_DIR: tools/battlefx/gallery.sh runs (their out/report.jsonl and captures). A capture is flagged when, in its lower fifth,
a black run 50 to 260 samples wide (of 316) has a bright background to its left and right at the same rows and stays black to the
picture's bottom edge: the shape of a move background's unfilled tile map rows (see gba_easy_draw.c, Update 27) shown where the
GBA's text box used to cover it, over the player's Pokemon. A background that is black on purpose (a dark scene) has no bright
background beside the black, and is not flagged.

Exit 0: none flagged. Exit 1: the flagged ids are listed.
"""
import glob
import json
import os
import re
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def canon_factory():
    nums = json.load(open(os.path.join(ROOT, "tools", "battlefx", "move_numbers.json")))
    inv = {}
    for k, v in nums.items():
        inv.setdefault(v, k)

    def canon(i):
        m = re.fullmatch(r"MOVEN_(\d+)", i)
        return inv.get(int(m.group(1)), i) if m else i

    return canon


def flagged(path):
    a = np.asarray(Image.open(path).convert("RGB").resize((316, 178))).astype(int)
    blk = a.max(axis=2) < 8
    bright = a.max(axis=2) >= 60
    # a frame whose stage is mostly black is a fade to or from black (the animation's first frames), not a hole in a background
    if bright[:100].mean() < 0.35:
        return False
    for y in range(120, 176):
        xs = np.where(blk[y])[0]
        if len(xs) < 40:
            continue
        runs = np.split(xs, np.where(np.diff(xs) != 1)[0] + 1)
        run = max(runs, key=len)
        if len(run) < 50 or len(run) > 260:
            continue
        left = bright[y, max(0, run[0] - 6):run[0]].sum() >= 4
        right = bright[y, run[-1] + 1:run[-1] + 7].sum() >= 4 or run[-1] >= 312
        down = blk[y:176, run[0] + 2:run[-1] - 1].mean() > 0.97
        if (left or run[0] <= 2) and right and down:
            return True
    return False


def main():
    dirs = [a for a in sys.argv[1:] if not a.startswith("--")]
    canon = canon_factory()
    hits, seen = {}, set()
    for d in dirs:
        out = os.path.join(d, "out")
        report = os.path.join(out, "report.jsonl")
        if not os.path.exists(report):
            continue
        for line in open(report):
            r = json.loads(line)
            ident = canon(r["id"])
            for s in r.get("shots", []):
                f = s.get("file")
                if f and os.path.isfile(os.path.join(out, f)):
                    seen.add(ident)
                    if flagged(os.path.join(out, f)):
                        hits[ident] = hits.get(ident, 0) + 1
    print(f"black_rect_check: {len(seen)} animations with captures, {len(hits)} with a black block at the bottom")
    for k in sorted(hits):
        print("  ", k, hits[k])
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
