#!/usr/bin/env python3
"""Read rooms off their cells (battle_room_auto.py), build them and prove them; find the pieces
that keep a room from its proof.

    python3 tools/battle_env/auto_rooms.py OUT LAYOUT_ID... | @LIST_FILE [--repair N] [--previews]

Every listed layout without a spec of its own is read by battle_room_auto (with the pieces
battle_room_specs.AUTO_DROP leaves out), all the models are built once with the shared library's
generator, and each room is proved as gen_voxel_buildings.py --verify proves the hand-made ones:
every model against its drawing (exact, texel density), and the room composed with every model
standing in it against the room's drawing, pixel for pixel. A room that passes may go into
battle_room_specs.AUTO_ROOMS.

A room also fails when three or more walkable cells side by side lie under a level surface half a
tile up or more (a slab the GBA's camera cannot tell from the floor, but the battle's camera can;
a statue's head over the floor cell behind it covers one).

--repair N: for a room that fails, the cells whose pixels differ name the pieces that claim them
(the smallest one claiming each cell); those pieces are left out and the room is built and
proved again, up to N times. The pieces it left out are printed as AUTO_DROP entries.

Writes OUT/verify.json ({layout: {ok, models, notes, drop, log}}) and, with --previews, a
battle-camera and a field-camera picture of each room with its models (OUT/<layout>_auto.png).
TOOL (development: what it proves goes into the model library by hand).
"""

import argparse
import contextlib
import io
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diorama_buildings'))

import gen_voxel_buildings as g      # noqa: E402
import voxel_building as vb          # noqa: E402
import voxel_building_specs as specs  # noqa: E402
import battle_room_auto              # noqa: E402
import battle_room_specs             # noqa: E402
import expansion_assets              # noqa: E402
from PIL import Image                # noqa: E402


def piece_cells(pc, layout):
    """The cells a piece claims: its shapes' and its side walls' columns."""
    cells = set()
    for shape in pc.get('shape') or ():
        if len(shape) == 4 and all(isinstance(v, (int, float)) for v in shape):
            x0, y0, x1, y1 = shape
        else:   # a polygon
            xs, ys = [p[0] for p in shape], [p[1] for p in shape]
            x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
        for cy in range(int(y0) // 16, (int(y1) - 1) // 16 + 1):
            for cx in range(int(x0) // 16, (int(x1) - 1) // 16 + 1):
                cells.add((cx, cy))
    for seg in pc.get('walls') or ():
        (xa, za), (xb, zb) = seg[0], seg[1]
        col = 0 if xa <= 0 else layout.w - 1
        for cy in range(int(min(za, zb)) // 16, (int(max(za, zb)) - 1) // 16 + 1):
            cells.add((col, cy))
    return cells


def culprit_cells(out, layout):
    """The cells with pixels that differ, from room_check's picture (the drawing, 4 px, the
    composed room with the differing pixels magenta; 3x)."""
    path = os.path.join(out, layout.id.lower() + '_room_check.png')
    if not os.path.exists(path):
        return set()
    im = Image.open(path).convert('RGB')
    im = im.resize((im.width // 3, im.height // 3), Image.NEAREST)
    px = im.load()
    W, H = layout.w * 16, layout.h * 16
    return {(x // 16, y // 16) for y in range(H) for x in range(W)
            if px[W + 4 + x, y] == (255, 0, 255)}


def prove(layouts, out, drops):
    """Build every room's models once; prove each room. {layout: result}."""
    modelled = {s['interior']['layout'] for s in specs.SPECS
                if 'interior' in s and not s.get('auto')}
    autos, results = [], {}
    for lid in layouts:
        if lid in modelled:
            results[lid] = {'ok': None, 'skip': 'has a spec of its own'}
            continue
        sp = battle_room_auto.auto_room_spec('auto_' + lid[len('LAYOUT_'):].lower(), lid,
                                             drops.get(lid, ()), reuse_in=layouts)
        if sp is None:
            results[lid] = {'ok': None, 'skip': 'nothing to stand'}
            continue
        autos.append(sp)
    names = {sp['name'] for sp in autos}
    keep = [s for s in specs.SPECS if s.get('name') not in names
            and s.get('interior', {}).get('layout') not in {sp['interior']['layout'] for sp in autos}]
    saved = specs.SPECS
    specs.SPECS = keep + autos
    try:
        g.DIAGNOSTICS = out
        models = g.build_models(None)
    finally:
        specs.SPECS = saved
    for sp in autos:
        lid = sp['interior']['layout']
        mine = [m for m in models if m.spec.get('interior') == sp['name']]
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            # each model against its drawing (and the room with its own models) ...
            g.verify(mine, out)
            exact = all('wrong=0 missing=0 extra=0' in l and ' 0 bad' in l
                        for l in buf.getvalue().splitlines() if 'exact:' in l)
            # ... and the room composed with every model that stands in it
            bad = g.room_check(models, lid, out)
        results[lid] = {'ok': bool(mine) and exact and bad == 0, 'models': len(mine),
                        'bad_pixels': bad, 'notes': sp['notes'],
                        'drop': list(drops.get(lid, ())),
                        'pieces': [pc['name'] for pc in battle_room_auto.analyse(
                            lid, drop=drops.get(lid, ()))[1]],
                        'log': [l for l in buf.getvalue().splitlines() if l.startswith('    ')][:12]}
    return results, models


def raised_floor(models, layout_id):
    """The room's walkable cells that a model lays a level surface over, half a tile up or more:
    a slab where the battlers and the camera stand (the proof, from the GBA's camera, cannot
    see one). [(x, y, height)]."""
    import battle_room_auto
    layouts_json = expansion_assets.emerald_layouts(vb.ROOT)
    layout = vb.LayoutArt(layout_id)
    level = {}
    for m in models:
        for l2, px, py, ground, name, odd in g.find_placements(m, layouts_json):
            if name != layout_id:
                continue
            for (tri, shade, tag) in m.mesh.tris:
                ys = [v[1] / 16.0 for v in tri]
                if max(ys) - min(ys) > 0.02 or max(ys) < 0.5:
                    continue        # not level, or low
                xs = [px + v[0] / 16.0 for v in tri]
                zs = [py + v[2] / 16.0 for v in tri]
                for cy in range(int(min(zs) + 0.1), int(max(zs) - 0.1) + 1):
                    for cx in range(int(min(xs) + 0.1), int(max(xs) - 0.1) + 1):
                        level[(cx, cy)] = max(level.get((cx, cy), 0.0), max(ys))
    of = battle_room_auto._behaviours(layout)
    out = []
    for (cx, cy), h in sorted(level.items()):
        if not (0 <= cx < layout.w and 0 <= cy < layout.h):
            continue
        cell = layout.blocks[cy * layout.w + cx]
        if (cell >> 10) & 3 == 0 and not battle_room_auto.LEVEL.match(of(cell & 0x3FF)):
            out.append((cx, cy, round(h, 2)))
    return out


def largest_region(cells):
    """The most cells joined side by side: a statue's head over the floor behind it covers one
    cell, a slab many."""
    cells, best = set(cells), 0
    while cells:
        todo, n = [cells.pop()], 0
        while todo:
            x, y = todo.pop()
            n += 1
            for c in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if c in cells:
                    cells.remove(c)
                    todo.append(c)
        best = max(best, n)
    return best


def previews(models, layouts, out):
    layouts_json = expansion_assets.emerald_layouts(vb.ROOT)
    for lid in layouts:
        layout = vb.LayoutArt(lid)
        override, items = {}, []
        for m in models:
            for l2, px, py, ground, name, odd in g.find_placements(m, layouts_json):
                if name != lid:
                    continue
                w, h = m.cells
                for j in range(h):
                    for i in range(w):
                        override[(px + i, py + j)] = ground
                items.append((vb.model_world_tris(m, px, py), m.art))
        ground, gtex = vb.ground_tris(layout, 0, 0, layout.w, layout.h, override, None)
        shots = []
        tx, tz = layout.w / 2.0, layout.h / 2.0
        for cx, cz, pitch, dist in ((tx, tz, 30.0, 9.33), (tx, tz + 1, 45.0, max(14.0, layout.h * 0.9))):
            cam = vb.Camera((cx, 0.0, cz), pitch=pitch, yaw=0.0, distance=dist)
            shots.append(vb.render_scene(cam, [(ground, gtex)] + items, scale=1))
        sheet = Image.new('RGB', (shots[0].width * 2, shots[0].height))
        for k, im in enumerate(shots):
            sheet.paste(im, (k * im.width, 0))
        sheet.save(os.path.join(out, lid[len('LAYOUT_'):].lower() + '_auto.png'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('layouts', nargs='+')
    ap.add_argument('--repair', type=int, default=0)
    ap.add_argument('--previews', action='store_true')
    args = ap.parse_args()
    layouts = []
    for a in args.layouts:
        layouts += [l.strip() for l in open(a[1:]) if l.strip()] if a.startswith('@') else [a]
    os.makedirs(args.out, exist_ok=True)
    drops = {k: list(v) for k, v in battle_room_specs.AUTO_DROP.items()}
    results, models = prove(layouts, args.out, drops)
    for round_ in range(args.repair):
        failing = [lid for lid, r in results.items() if r.get('ok') is False]
        if not failing:
            break
        changed = []
        for lid in failing:
            layout = vb.LayoutArt(lid)
            bad = culprit_cells(args.out, layout)
            _, pieces, _ = battle_room_auto.analyse(lid, drop=drops.get(lid, ()))
            claims = [(pc['name'], piece_cells(pc, layout)) for pc in pieces]
            blamed = set()
            for cell in bad:
                owners = [(len(cells), name) for name, cells in claims if cell in cells]
                if owners:
                    blamed.add(min(owners)[1])
            if blamed:
                drops.setdefault(lid, [])
                drops[lid] += sorted(blamed - set(drops[lid]))
                changed.append(lid)
            print('repair %d: %s: %d cells differ, leaving out %s' % (
                round_ + 1, lid, len(bad), sorted(blamed)), flush=True)
        if not changed:
            break
        again, models = prove(changed, args.out, drops)
        results.update(again)
    for lid in layouts:
        if results[lid].get('ok') is not None:
            raised = raised_floor(models, lid)
            results[lid]['raised_floor'] = raised
            biggest = largest_region([(x, y) for x, y, _ in raised])
            if biggest > 2:
                results[lid]['ok'] = False
                print('%s: %d walkable cells side by side under a level surface half a tile up or '
                      'more (a slab), of %s' % (lid, biggest, raised[:8]), flush=True)
    if args.previews:
        previews(models, [l for l in layouts if results.get(l, {}).get('ok') is not None], args.out)
    json.dump(results, open(os.path.join(args.out, 'verify.json'), 'w'), indent=1)
    for lid in layouts:
        r = results[lid]
        print('%-52s %s %s' % (lid, {True: 'ok', False: 'FAILED', None: 'skip'}[r.get('ok')],
                               r.get('skip') or '%d models, %d px differ%s' % (
                                   r['models'], r['bad_pixels'],
                                   ', drop %s' % r['drop'] if r['drop'] else '')))
    ok = [lid for lid in layouts if results[lid].get('ok')]
    print('\n%d of %d proved' % (len(ok), len(layouts)))
    dropped = {lid: results[lid]['drop'] for lid in ok if results[lid]['drop']}
    if dropped:
        print('AUTO_DROP entries:')
        for lid, names in sorted(dropped.items()):
            print('    %r: %r,' % (lid, names))


if __name__ == '__main__':
    main()
