#!/usr/bin/env python3
"""A cave tileset's rock, stood up as cave wall: the rules compiler's pins for it.

    python3 tools/battle_env/cave_pins.py gTileset_MeteorFalls [--write] [--sheet OUT.png]
    python3 tools/battle_env/cave_pins.py gTileset_Cave --compare

Reads every layout that uses the tileset as its secondary (data/layouts/layouts.json) and picks
the tileset's own metatiles that are cave rock: placed impassable (nearly) everywhere, with no
behaviour of their own (MB_NORMAL: not water, a ladder, a hole, a waterfall, a cracked floor),
not named by the game's code or scripts (setmetatile could change them while the map is up),
not dark void (the black past a cave's edge stays a flat shadow), not a plain fill or showing
the layouts' water or sky in a fifth of its drawing or more (the edge of a pool, the summit's
sky), and part of a rock mass - in
most placements joined side by side to at least five more impassable cells or to the map's
edge, which leaves out a lone boulder or an object drawn on the floor. Each becomes a pin {archetype "cliff", pool "terrain",
height 1}: the rules compiler stands the cell up one cell high as the cliffs of a route are,
with the rock's own drawing on its faces.

--write writes data/diorama/tilesets/<name>.json (<name> from the tileset: gTileset_MeteorFalls
-> meteor_falls.json); an existing file is left alone unless it is one this tool wrote (its
pins' reasons say so). --compare prints how the selection differs from the file there now.
--sheet draws the chosen and the passed-over metatiles for review by eye.

The pins apply wherever the tileset is drawn, in the field and in a battle alike: only tilesets
drawn in caves alone are meant for this tool (not one a route shares, such as gTileset_Lavaridge).
TOOL (its output is data the game's build uses).
"""

import argparse
import warnings
import collections
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diorama_buildings'))

warnings.filterwarnings("ignore", category=DeprecationWarning)
import voxel_building as vb          # noqa: E402
import battle_room_auto as auto      # noqa: E402

REASON = ("Cave rock (blocked, MB_NORMAL, part of a rock mass): stands as cave wall "
          "(battle-environment overhaul).")
BLOCKED_SHARE = 0.9    # of its placements impassable
MASS_SHARE = 0.5       # of its placements in a rock mass
MASS_CELLS = 6         # impassable cells joined side by side, at least, or one at the map's edge


def file_name(tileset):
    name = tileset[len('gTileset_'):] if tileset.startswith('gTileset_') else tileset
    return re.sub(r'(?<=[a-z0-9])([A-Z])', r'_\1', name).lower() + '.json'


def layouts_of(tileset):
    entries = json.load(open(os.path.join(ROOT, 'data/layouts/layouts.json')))['layouts']
    return [e['id'] for e in entries if e.get('secondary_tileset') == tileset]


def survey(tileset):
    """Per secondary metatile (local id): placements, impassable ones, rock-mass ones, its
    behaviour, and whether it is drawn as void."""
    dynamic = auto.dynamic_metatiles()
    stats = collections.defaultdict(lambda: {'uses': 0, 'blocked': 0, 'mass': 0, 'layouts': set()})
    behaviour = {}
    void = {}
    drawings = {}
    images = {}
    fills = set()      # the blues of water and sky in these layouts
    sample = None
    for lid in layouts_of(tileset):
        try:
            L = vb.LayoutArt(lid)
        except Exception as e:  # a layout whose data is not in this tree
            print('skip %s: %s' % (lid, e), file=sys.stderr)
            continue
        sample = sample or L
        of = auto._behaviours(L)
        W, H = L.w, L.h

        def blocked(x, y):
            return (L.blocks[y * W + x] >> 10) & 3 != 0

        # the impassable cells' masses: joined side by side, their size, whether one reaches
        # the map's edge (the rock goes on past it)
        mass = [[0] * W for _ in range(H)]
        seen = [[False] * W for _ in range(H)]
        for y0 in range(H):
            for x0 in range(W):
                if seen[y0][x0] or not blocked(x0, y0):
                    continue
                cells, stack, edge = [], [(x0, y0)], False
                seen[y0][x0] = True
                while stack:
                    x, y = stack.pop()
                    cells.append((x, y))
                    edge = edge or x in (0, W - 1) or y in (0, H - 1)
                    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] and blocked(nx, ny):
                            seen[ny][nx] = True
                            stack.append((nx, ny))
                big = edge or len(cells) >= MASS_CELLS
                for x, y in cells:
                    mass[y][x] = big

        for y in range(H):
            for x in range(W):
                m = L.blocks[y * W + x] & 0x3FF
                if auto.LEVEL.match(of(m)) or flat_fill(L, m, images):
                    top = colours(L, m, images).most_common(1)[0][0]
                    if top is not None and top[2] > top[0] + 40 and top[2] > top[1] + 10:
                        fills.add(top)    # water's or the sky's blue
                if m < 512 or (tileset, m - 512) in dynamic:
                    continue
                s = stats[m - 512]
                s['uses'] += 1
                s['layouts'].add(lid)
                if blocked(x, y):
                    s['blocked'] += 1
                    if mass[y][x]:
                        s['mass'] += 1
                if m - 512 not in behaviour:
                    behaviour[m - 512] = of(m)
                    drawings[m - 512] = (L, m)
    # what the drawing says, once every layout's water and fills are known
    for k, (L, m) in drawings.items():
        void[k] = drawn_as(L, m, fills, images)
    return stats, behaviour, void, sample


def colours(L, m, images):
    key = (L.primary, L.secondary, m)
    if key not in images:
        images[key] = collections.Counter(p[:3] if p[3] >= 128 else None
                                          for p in L.cell_image(m).convert('RGBA').getdata())
    return images[key]


def flat_fill(L, m, images):
    c = colours(L, m, images)
    return c.most_common(1)[0][1] >= 0.9 * sum(c.values())


def drawn_as(L, m, fills, images):
    """Why a metatile's drawing is no rock, or None: 'void' (the black past a cave's edge),
    'fill' (a plain fill: the sky at Navel Rock's summit, a sheet of lava), 'water' (the
    colour of the layouts' water or sky showing in a fifth of it or more - a pool's rocky
    corner, a boulder in the sea)."""
    c = colours(L, m, images)
    n = sum(c.values())
    dark = sum(k for p, k in c.items() if p is None or (p[0] <= auto.DARK and p[1] <= auto.DARK
                                                         and p[2] <= auto.DARK + 8))
    if dark >= 0.9 * n:
        return 'void'
    if flat_fill(L, m, images):
        return 'fill'
    if sum(k for p, k in c.items() if p in fills) >= 0.2 * n:
        return 'water'
    return None


def choose(stats, behaviour, void):
    rock, passed = [], []
    for m, s in sorted(stats.items()):
        why = None
        if behaviour.get(m) != 'MB_NORMAL':
            why = behaviour.get(m)
        elif void.get(m):
            why = void[m]
        elif s['blocked'] < BLOCKED_SHARE * s['uses']:
            why = 'passable %d of %d' % (s['uses'] - s['blocked'], s['uses'])
        elif s['mass'] < MASS_SHARE * s['uses']:
            why = 'alone %d of %d' % (s['uses'] - s['mass'], s['uses'])
        (passed if why else rock).append((m, why))
    return [m for m, _ in rock], passed


def sheet(path, tileset, sample, rock, passed):
    from PIL import Image, ImageDraw
    items = [(m, True, '') for m in rock] + [(m, False, why) for m, why in passed]
    cols, S = 16, 3
    cw, ch = 16 * S + 6, 16 * S + 22
    img = Image.new('RGB', (cols * cw, ((len(items) + cols - 1) // cols) * ch + 4), (36, 36, 44))
    d = ImageDraw.Draw(img)
    for i, (m, chosen, why) in enumerate(items):
        x, y = (i % cols) * cw, (i // cols) * ch
        im = sample.cell_image(m + 512).convert('RGB').resize((16 * S, 16 * S), Image.NEAREST)
        img.paste(im, (x + 3, y + 3))
        d.rectangle((x + 1, y + 1, x + 16 * S + 4, y + 16 * S + 4),
                    outline=(80, 220, 120) if chosen else (200, 80, 80))
        d.text((x + 3, y + 16 * S + 6), ('%d' % m) + ('' if chosen else ' ' + why[:7]),
               fill=(230, 230, 230))
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tileset')
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--compare', action='store_true')
    ap.add_argument('--sheet')
    args = ap.parse_args()
    stats, behaviour, void, sample = survey(args.tileset)
    if not stats:
        sys.exit('no layout uses %s as its secondary tileset' % args.tileset)
    rock, passed = choose(stats, behaviour, void)
    path = os.path.join(ROOT, 'data/diorama/tilesets', file_name(args.tileset))
    print('%s: %d layouts, %d metatiles placed, %d cave rock' % (
        args.tileset, len(layouts_of(args.tileset)), len(stats), len(rock)))
    if args.compare and os.path.exists(path):
        have = {p['metatile'] for p in json.load(open(path))['pins']}
        print('only in %s: %s' % (os.path.basename(path), sorted(have - set(rock))))
        print('only chosen now: %s' % sorted(set(rock) - have))
    if args.sheet and sample is not None:
        sheet(args.sheet, args.tileset, sample, rock, passed)
        print('sheet:', args.sheet)
    if args.write:
        if os.path.exists(path):
            old = json.load(open(path))
            if any(p.get('reason') != REASON for p in old.get('pins', [])):
                sys.exit('%s has pins this tool did not write; not replacing it' % path)
        data = {'schemaVersion': 2, 'kind': 'tileset', 'tileset': args.tileset,
                'pins': [{'metatile': m, 'reason': REASON,
                          'action': {'archetype': 'cliff', 'pool': 'terrain', 'height': 1}}
                         for m in rock]}
        with open(path, 'w') as f:
            json.dump(data, f, indent=2)
            f.write('\n')
        print('wrote', path)


if __name__ == '__main__':
    main()
