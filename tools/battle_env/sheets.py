#!/usr/bin/env python3
"""Contact sheets of a battle-environment survey, for reviewing the pictures by eye.

    python3 tools/battle_env/sheets.py SURVEY_DIR OUT_DIR [--per 12] [--cols 3] [--width 480]
        [--compare OTHER_SURVEY_DIR]

One sheet per environment category (and its continuation), each tile the battle at the action
menu with its map, battle and the measured shares written under it. With --compare, each tile
is the other survey's picture (left, "before") beside this one's (right, "after").
TEST TOOL.
"""

import argparse
import json
import os

from PIL import Image, ImageDraw


def label(res):
    cap = res.get('capture') or {}
    band = cap.get('band', {}) if isinstance(cap, dict) else {}
    return '%s  [%s]  %s\nvoid %.2f floor %.2f upright %.2f raised %.2f depth %.0f' % (
        res['map'][4:], res['category'], res['label'], band.get('void', -1), band.get('floor', -1),
        band.get('upright', -1), band.get('raised', -1), band.get('depth_median', -1))


def tile(path, width):
    try:
        im = Image.open(path).convert('RGB')
    except OSError:
        im = Image.new('RGB', (960, 540), (60, 0, 0))
    return im.resize((width, int(im.height * width / im.width)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('survey')
    ap.add_argument('out')
    ap.add_argument('--per', type=int, default=12)
    ap.add_argument('--cols', type=int, default=3)
    ap.add_argument('--width', type=int, default=480)
    ap.add_argument('--compare', default=None)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    results = json.load(open(os.path.join(args.survey, 'results.json')))
    before = {}
    if args.compare:
        for r in json.load(open(os.path.join(args.compare, 'results.json'))):
            before[(r['map'], r['label'])] = r
    by_cat = {}
    for r in results:
        by_cat.setdefault(r['category'], []).append(r)
    made = []
    for cat, rs in sorted(by_cat.items()):
        rs.sort(key=lambda r: r['map'])
        for page in range(0, len(rs), args.per):
            chunk = rs[page:page + args.per]
            tw = args.width * (2 if args.compare else 1)
            th = int(args.width * 9 / 16) + 30
            rows = (len(chunk) + args.cols - 1) // args.cols
            sheet = Image.new('RGB', (args.cols * (tw + 6), rows * (th + 6)), (24, 24, 30))
            d = ImageDraw.Draw(sheet)
            for k, r in enumerate(chunk):
                x, y = (k % args.cols) * (tw + 6), (k // args.cols) * (th + 6)
                cap = os.path.join(r['dir'], 'cap.png')
                if args.compare:
                    b = before.get((r['map'], r['label']))
                    left = tile(os.path.join(b['dir'], 'cap.png'), args.width) if b else \
                        Image.new('RGB', (args.width, int(args.width * 9 / 16)), (40, 40, 40))
                    sheet.paste(left, (x, y))
                    sheet.paste(tile(cap, args.width), (x + args.width, y))
                else:
                    sheet.paste(tile(cap, args.width), (x, y))
                d.text((x + 3, y + th - 29), label(r) + ('' if r['status'] == 'captured' else '  ' + r['status']),
                       fill=(235, 235, 235) if r['status'] == 'captured' else (255, 120, 120))
            path = os.path.join(args.out, '%s-%02d.png' % (cat, page // args.per + 1))
            sheet.save(path)
            made.append(path)
    print('\n'.join(made))


if __name__ == '__main__':
    main()
