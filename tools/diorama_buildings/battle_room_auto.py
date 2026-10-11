#!/usr/bin/env python3
"""A first reading of a room no one has modelled piece by piece (battle-environment overhaul).

For the many rooms battles take place in, a room's pieces are read off its cells the way the
hand-made rooms (voxel_building_specs.py) are cut, so that the room stands up in 3D without a
spec of its own:

  * walls: in each column, a run of blocked cells drawn with something (not the black outside
    a room) whose top is the outside (black, or the map's edge) and whose foot is the floor is
    a wall's face as the GBA draws it - it stands up on its foot, its whole drawn height (up
    to `MAX_WALL` rows; above that the drawing is the wall's top, laid back). Neighbouring
    columns with the same run are one wall; a doorway (floor in the wall's rows) parts them.
  * edges: blocked cells beside the outside that are not a wall's face - a side wall's top
    seen from above, the edge of a stage - stand as tall as the room's walls, their own
    drawing their top.
  * things: the other blocked cells, a run whose top is floor (a statue, a pole, a machine,
    a maze block), are furniture: they stand on their foot, `THING` rows of front at most,
    their top running back to where their collision starts; the floor cell above one, where
    its drawing goes on (a statue's head over the floor behind it), is its too.
  * the room's sides, which the GBA never draws: where the floor reaches the map's left or
    right edge, a side wall from the back wall to the front, dressed with the wall's panels.

Front (south) walls are never stood up. The pieces are ordinary interior pieces
(gen_voxel_buildings.interior_specs), so they reproduce the drawing exactly in the GBA's
projection, in the field and in a battle alike; a piece that would claim no pixel of its own
is dropped. Hand-made specs (battle_room_specs.py) take precedence for the rooms they model.
"""

import collections
import os
import re
import struct

import voxel_building as vb


def _helpers():
    import voxel_building_specs as v   # at call time: that module imports the battle rooms
    return v.own_shell, v.piece

MAX_WALL = 48       # rows of a wall's face that stand; more of a drawn wall is its top
THING = 20          # rows of front a piece of furniture stands at most
DARK = 40           # a pixel at most this bright is the black outside a room


_BEHAVIOURS = None


def _behaviour_names():
    global _BEHAVIOURS
    if _BEHAVIOURS is None:
        text = open(os.path.join(vb.ROOT, "include/constants/metatile_behaviors.h")).read()
        body = text[text.index("MB_NORMAL"):]
        body = body[:body.index("}")]
        _BEHAVIOURS = [n.split("=")[0].strip() for n in re.findall(r"(MB_\w+[^,\n]*)", body)]
    return _BEHAVIOURS


# Blocked cells that are no thing standing: water (a pool, a fountain's basin - its surface is
# the floor's level), the void of a hole, a waterfall's face.
LEVEL = re.compile(r"MB_(POND_WATER|INTERIOR_DEEP_WATER|DEEP_WATER|OCEAN_WATER|SOOTOPOLIS_DEEP_WATER|"
                   r"WATERFALL|NO_SURFACING|SEAWEED\w*|\w+_CURRENT|SHALLOW_WATER|PUDDLE|"
                   r"CRACKED_FLOOR_HOLE|LAVARIDGE_GYM_B1F_WARP|HOT_SPRINGS|ICE|THIN_ICE|CRACKED_ICE)$")


def _behaviours(layout):
    """Each metatile's behaviour name, primary and secondary."""
    names = _behaviour_names()

    def read(symbol):
        path = layout.attrs.get(symbol)
        if not path or not os.path.exists(path):
            return []
        raw = open(path, "rb").read()
        return [v & 0xFF for v in struct.unpack("<%dH" % (len(raw) // 2), raw[:len(raw) // 2 * 2])]

    prim, sec = read(layout.primary), read(layout.secondary)

    def of(m):
        table, i = (prim, m) if m < 512 else (sec, m - 512)
        b = table[i] if i < len(table) else 0
        return names[b] if b < len(names) else "MB_%d" % b
    return of


_DYNAMIC = None


def dynamic_metatiles():
    """{(tileset symbol, metatile)}: the metatiles the game's code or scripts name - set by
    setmetatile or by a field special (a gym's barriers, an Elite Four room's lights, the Trick
    House's switches, a cracked floor). A cell drawn with one may change while the map is up,
    so no auto piece stands on it (the terrain draws it as it is; a piece that does stand on a
    changed cell is left out at run time, buildings.c DioramaBuildings_Validate)."""
    global _DYNAMIC
    if _DYNAMIC is not None:
        return _DYNAMIC
    labels = {}
    for m in re.finditer(r"#define\s+(METATILE_(\w+?)_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                         open(os.path.join(vb.ROOT, "include/constants/metatile_labels.h")).read()):
        labels[m.group(1)] = ("gTileset_" + m.group(2), int(m.group(3), 0))
    used = set()
    roots = [os.path.join(vb.ROOT, "src"), os.path.join(vb.ROOT, "data/scripts"), os.path.join(vb.ROOT, "data/maps")]
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.endswith((".c", ".inc")) and "frlg" not in f.lower() and "Frlg" not in dirpath:
                    try:
                        text = open(os.path.join(dirpath, f), errors="replace").read()
                    except OSError:
                        continue
                    used.update(re.findall(r"\bMETATILE_\w+", text))
    _DYNAMIC = {labels[n] for n in used if n in labels}
    return _DYNAMIC


def _cell_pixels(layout, m, cache):
    if m not in cache:
        im = layout.cell_image(m).convert("RGBA")
        cache[m] = list(im.getdata())
    return cache[m]


def analyse(layout_id, taken=()):
    """(ground metatiles, pieces, notes) for a layout; `taken`: cells the pieces of rooms
    modelled before it already stand in (left to them)."""
    own_shell, piece = _helpers()
    L = vb.LayoutArt(layout_id)
    taken = set(taken)
    W, H = L.w, L.h
    cache = {}

    def meta(x, y):
        return L.blocks[y * W + x] & 0x3FF

    def blocked(x, y):
        return (L.blocks[y * W + x] >> 10) & 3 != 0

    def is_void(x, y):
        px = _cell_pixels(L, meta(x, y), cache)
        dark = sum(1 for p in px if p[3] < 128 or (p[0] <= DARK and p[1] <= DARK and p[2] <= DARK + 8))
        return dark >= 0.9 * len(px)

    behaviour = _behaviours(L)
    dynamic = dynamic_metatiles()

    def changes(m):
        return ((L.primary if m < 512 else L.secondary), m) in dynamic

    void = [[is_void(x, y) for x in range(W)] for y in range(H)]
    level = [[bool(LEVEL.match(behaviour(meta(x, y)))) for x in range(W)] for y in range(H)]
    floor = [[(not blocked(x, y) or level[y][x]) and not void[y][x] for x in range(W)]
             for y in range(H)]
    solid = [[blocked(x, y) and not void[y][x] and not level[y][x] and (x, y) not in taken
              and not changes(meta(x, y)) for x in range(W)] for y in range(H)]

    counts = collections.Counter(meta(x, y) for y in range(H) for x in range(W)
                                 if floor[y][x] and not level[y][x])
    if not counts:
        return [], [], ["no floor"]
    total = sum(counts.values())
    ground = [m for m, c in counts.most_common(3) if c >= 0.2 * total] or [counts.most_common(1)[0][0]]
    ground_px = [_cell_pixels(L, m, cache) for m in ground]

    def differs_from_floor(x, y):
        """Whether a cell's drawing has pixels the floor does not (something drawn on it)."""
        px = _cell_pixels(L, meta(x, y), cache)
        for i, p in enumerate(px):
            if all(g[i][:3] != p[:3] for g in ground_px):
                return True
        return False

    notes = []
    walls = {}           # (ya, yb) -> list of columns
    wall_cells = set()
    for x in range(W):
        y = 0
        while y < H:
            if not solid[y][x]:
                y += 1
                continue
            ya = y
            while y < H and solid[y][x]:
                y += 1
            yb = y
            top_out = ya == 0 or void[ya - 1][x]
            foot_floor = yb < H and floor[yb][x]
            if top_out and foot_floor:
                walls.setdefault((ya, yb), []).append(x)
                wall_cells.update((x, yy) for yy in range(ya, yb))
    # the room's wall height: the most common standing face
    heights = collections.Counter()
    for (ya, yb), cols in walls.items():
        heights[min(MAX_WALL, (yb - ya) * 16)] += len(cols)
    wall_h = heights.most_common(1)[0][0] if heights else 32

    pieces = []
    # things and edges: connected blocked cells that are not a wall's face
    seen = set()
    things = []
    for y in range(H):
        for x in range(W):
            if not solid[y][x] or (x, y) in wall_cells or (x, y) in seen:
                continue
            comp, todo = [], [(x, y)]
            seen.add((x, y))
            while todo:
                cx, cy = todo.pop()
                comp.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < W and 0 <= ny < H and solid[ny][nx] and (nx, ny) not in wall_cells \
                            and (nx, ny) not in seen:
                        seen.add((nx, ny))
                        todo.append((nx, ny))
            things.append(comp)

    def runs_to_floor(comp):
        """Whether any column of the component ends on the floor (it has a front to stand)."""
        cells = set(comp)
        for (cx, cy) in comp:
            if (cx, cy + 1) not in cells and cy + 1 < H and floor[cy + 1][cx]:
                return True
        return False

    def wall_pieces():
        out = []
        for k, ((ya, yb), cols) in enumerate(sorted(walls.items(), key=lambda kv: (kv[0], kv[1][0]))):
            cols = sorted(cols)
            # contiguous runs of columns
            start = prev = cols[0]
            for c in cols[1:] + [None]:
                if c is not None and c == prev + 1:
                    prev = c
                    continue
                x0, x1 = start * 16, (prev + 1) * 16
                tall = min(MAX_WALL, (yb - ya) * 16)
                shape = [(x0, ya * 16, x1, yb * 16)]
                side = (x0, max(ya * 16, yb * 16 - tall), x0 + 16, yb * 16)
                out.append(piece("wall_%d_%d" % (start, ya), shape, tall, fill=16, foot=yb * 16,
                                 side=side))
                if c is not None:
                    start = prev = c
        return out

    def thing_pieces():
        out = []
        for k, comp in enumerate(things):
            if not runs_to_floor(comp):
                notes.append("left flat: %d cells at %s (no front on the floor)" % (len(comp), comp[0]))
                continue
            cells = set(comp)
            beside_out = any(void[ny][nx] for (cx, cy) in comp
                             for (nx, ny) in ((cx + 1, cy), (cx - 1, cy), (cx, cy - 1))
                             if 0 <= nx < W and 0 <= ny < H)
            rects = [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16) for (cx, cy) in comp]
            # its drawing over the floor cell north of it
            for (cx, cy) in comp:
                if (cx, cy - 1) not in cells and cy - 1 >= 0 and floor[cy - 1][cx] \
                        and differs_from_floor(cx, cy - 1):
                    rects.append((cx * 16, cy * 16 - 16, cx * 16 + 16, cy * 16))
            if not any(differs_from_floor(cx, cy) for (cx, cy) in comp):
                notes.append("left flat: %d cells at %s (drawn as floor)" % (len(comp), comp[0]))
                continue
            top = min(cy for _, cy in comp) * 16
            foot = max(cy for _, cy in comp) * 16 + 16
            x0 = min(cx for cx, _ in comp) * 16
            drawn = foot - min(r[1] for r in rects)
            if beside_out:
                # as tall as the room's walls, with a drawn top left to lay back
                tall = min(wall_h, max(8, drawn - 8))
                name = "edge_%d_%d" % (comp[0][0], comp[0][1])
            else:
                tall = max(8, min(THING, drawn - 8))
                name = "thing_%d_%d" % (comp[0][0], comp[0][1])
            out.append(piece(name, rects, tall, back=top if drawn - tall < foot - top else None,
                             solid=True, side=(x0, foot - min(tall, 16), x0 + 16, foot)))
        return out

    pieces += thing_pieces()
    pieces += wall_pieces()
    # the room's sides where its floor reaches the map's edge
    back_foot = min((yb for (ya, yb) in walls), default=None)
    if back_foot is not None:
        sample = next(((cols[0] * 16, yb * 16 - min(MAX_WALL, (yb - ya) * 16), cols[0] * 16 + 16, yb * 16)
                       for (ya, yb), cols in sorted(walls.items())), None)
        for edge_x, name in ((0, "side_w"), (W - 1, "side_e")):
            rows = [y for y in range(back_foot, H) if floor[y][edge_x]]
            if not rows:
                continue
            front = (max(rows) + 1) * 16
            xpx = 0 if edge_x == 0 else W * 16
            seg = ((xpx, front), (xpx, back_foot * 16)) if edge_x == 0 else ((xpx, back_foot * 16), (xpx, front))
            pieces.append(piece(name, [], wall_h, side=sample, walls=[seg]))
    return ground, own_shell(pieces), notes


def auto_room_spec(name, layout_id):
    """A room spec whose pieces are read when the room is built, around the furniture of the
    rooms built before it (gen_voxel_buildings.interior_specs calls them)."""
    ground, pieces, notes = analyse(layout_id)
    if not pieces:
        return None
    return {"name": name, "auto": True, "notes": notes,
            "interior": {"layout": layout_id, "ground": ground,
                         "pieces": lambda layout, taken: analyse(layout_id, taken)[1]}}
