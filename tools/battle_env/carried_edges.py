#!/usr/bin/env python3
"""Checks 3D battle pictures for a picture row smeared across the window's side margins.

The 3D battle carries the GBA picture's edge colours out to the window's sides (gl_battle.c
DrawCarriedOut, from gba_easy_draw.c SceneEdge). Only the background may be carried: a battler
reaching the picture's edge - a move's copy of it on BG1 or BG2, a large sprite-pack frame such as
Wailord's back or Rayquaza's tail - must not be, or it shows as a flat line across the world.

A row above the text box whose whole left or right margin is a single colour is reported (the 3D
world there is never one flat colour; the text box's own rows are carried out on purpose and are
not checked). Exit status 1 when any shot has such a row.

usage: carried_edges.py [--picture X0,Y0,X1,Y1] SHOT.png...
  --picture  the GBA picture's place in the window (default: the shot's .json from
             PKMN_TEST_CAPTURE, else 80,40,560,360: a 640x360 window at window scale 2)
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image


def picture_box(png: Path, given):
    meta = png.with_suffix('.json')
    if given is None and meta.exists():
        return json.loads(meta.read_text())['env']['camera']['picture']
    return given or [80.0, 40.0, 560.0, 360.0]


def flat_rows(png: Path, box):
    x0, y0, x1, y1 = box
    scale = (x1 - x0) / 240.0
    text_box = y1 - 48 * scale
    image = Image.open(png).convert('RGB')
    width = image.size[0]
    pixels = image.load()
    rows = []
    for y in range(int(y0), int(text_box)):
        for side, (a, b) in (('left', (0, int(x0))), ('right', (int(x1), width))):
            if b - a < 8:
                continue
            colours = {pixels[x, y] for x in range(a, b)}
            if len(colours) == 1:
                rows.append((y, side, next(iter(colours))))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--picture', type=lambda s: [float(v) for v in s.split(',')])
    parser.add_argument('shots', nargs='+', type=Path)
    args = parser.parse_args()
    bad = 0
    for png in args.shots:
        rows = flat_rows(png, picture_box(png, args.picture))
        detail = ', '.join(f'y{y} {side} {colour}' for y, side, colour in rows[:8])
        print(f'{png}: {len(rows)} flat margin rows' + (f': {detail}' if rows else ''))
        bad += bool(rows)
    print(f'{bad} of {len(args.shots)} shots with a carried-out row')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
