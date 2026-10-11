#!/usr/bin/env python3
"""The buildings no spec models, found on the maps and read off their own
drawing (project-owned: graphics overhaul, building details).

voxel_building_specs.py models a few dozen building types by hand, pixel for
pixel, and reuses them wherever the same drawing stands. Every other building
of Hoenn - a Slateport house, Lilycove's department store, the houses of
Fortree, Mossdeep and Pacifidlog, the Battle Frontier - was left lying flat on
the ground. This finds them and models each one the way the generator models
a hedge (voxel_building.Relief): a solid read column by column off the
drawing, its front the bottom rows of every column, standing as tall as the
building's facade, and its top the rows above, laid back to the north edge of
the cells it stands on. In the GBA's projection the model is its drawing,
pixel for pixel, whatever the facade's height; the height decides only how it
stands when seen from any other angle.

    a building: a door on an outdoor map (MB_ANIMATED_DOOR or
        MB_NON_ANIMATED_DOOR) under blocked cells of the town's own tileset
        or of the General tileset's house kit. Its front is the door's row,
        as far as the cells beside the door are drawn over (a fence lets the
        ground show); its cells go up from the front and out to the sides a
        little way (wings, eaves) but not sideways into leaves (the trees
        between two houses), and the row over its north edge that the drawing
        paints with its roof is its own too, standing on nothing. Two
        buildings whose roofs touch split the cells between them by their
        fronts; a one-cell-wide door is a gap in a row of trees.
    its facade: the rows from the ground up to its eave, the first long
        straight edge across the building over the door and its lintel
        (OVERRIDES, in pixels, where the drawing says otherwise)

    python3 auto_buildings.py --sheet OUT_DIR [--map MAP] [--modelled BIN]

draws every building found with its cells and its eave marked, to look over.
Those pictures are game art and are not committed.
"""

import argparse
import glob
import json
import os
import re
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voxel_building as vb  # noqa: E402

ROOT = vb.ROOT
OUTDOOR = ("MAP_TYPE_TOWN", "MAP_TYPE_CITY", "MAP_TYPE_ROUTE", "MAP_TYPE_OCEAN_ROUTE")
DOORS = ("MB_ANIMATED_DOOR", "MB_NON_ANIMATED_DOOR")
# A building is no wider or deeper than this (cells): a run of cells that goes
# on further is a cliff or a wall the door is cut into, not a building.
MAX_WIDTH = 20
MAX_DEPTH = 16
# A cell beside the door is the building's front when its drawing covers this
# much of it.
FRONT_COVER = 0.6
# A cell this much of whose drawing is leaves is a tree, not a wall.
LEAVES = 0.6
# The General tileset's own house kit (walls, door frames, roofs) and the
# Pokemon Center's and Mart's pieces: building cells though primary.
GENERAL_BUILDING = (set(range(0x008, 0x00D)) | set(range(0x010, 0x015)) | set(range(0x018, 0x01B))
                    | set(range(0x020, 0x023)) | set(range(0x028, 0x034)) | set(range(0x038, 0x03C))
                    | set(range(0x041, 0x044)) | set(range(0x048, 0x04C)) | set(range(0x050, 0x054))
                    | set(range(0x058, 0x05C)) | set(range(0x060, 0x064)))

# Per building, by map and door: what the drawing says that the search cannot.
#   "facade": its facade's height in pixels;
#   "skip": no building (a door in a cliff face, a tent, a hole);
#   "cells": extra (x, y) cells of the map that are the building's;
#   "drop": cells the search took that are not;
#   "top": the map row the building starts at (a cliff behind it is not it);
#   "cols": the map columns (first, last) it stands in (trees beside it).
OVERRIDES = {
    # Tall buildings: the drawing is nearly all facade, a sliver of roof.
    ("LilycoveCity", 27, 6): {"facade": 106},           # the department store
    ("LilycoveCity", 11, 5): {"facade": 40},            # the museum's lower storey
    ("EverGrandeCity", 18, 5): {"facade": 92},          # the Pokemon League
    ("MossdeepCity", 64, 15): {"facade": 80},           # the Space Center
    ("Route119", 6, 32): {"facade": 48},                # the Weather Institute
    ("BattleFrontier_OutsideWest", 11, 38): {"facade": 92},
    ("BattleFrontier_OutsideEast", 39, 29): {"facade": 36},
    # The Battle Tents: a dome standing on its drum.
    ("SlateportCity", 10, 12): {"facade": 40},
    ("VerdanturfTown", 3, 7): {"facade": 40},
    ("BattleFrontier_OutsideEast", 16, 14): {"facade": 228},   # the Battle Tower
    # Cliffs and trees the search runs into behind or beside a building.
    ("FallarborTown", 8, 7): {"top": 3},                 # the Battle Tent
    ("FallarborTown", 1, 6): {"top": 3},
    ("FallarborTown", 14, 7): {"top": 3},
    ("Route113", 33, 5): {"cols": (32, 35), "facade": 24},   # the glass workshop
    # Not buildings: a door in a cliff under Trainer Hill, Sky Pillar's top.
    ("Route111", 31, 113): {"skip": True},
    ("SkyPillar_Top", 16, 14): {"skip": True},
}
# Maps whose doors are cut into the rock round them (Sootopolis's crater):
# no building stands there to model.
SKIP_MAPS = {"SootopolisCity"}


def behaviours():
    """MB_ names by value (constants/metatile_behaviors.h)."""
    text = open(os.path.join(ROOT, "include", "constants", "metatile_behaviors.h"),
                encoding="utf-8").read()
    body = text[text.index("{"):text.index("}")]
    out, value = {}, 0
    for line in body.splitlines():
        line = line.strip().rstrip(",")
        if not line.startswith("MB_"):
            continue
        if "=" in line:
            name, number = [part.strip() for part in line.split("=")]
            value = int(number, 0)
        else:
            name = line
        out[value] = name
        value += 1
    return out


def catalogue_props():
    """{tileset symbol: metatile ids} the Diorama's own detail objects stand
    up (src/diorama/scene/props.c): a fence or a sign by a building is not
    part of it."""
    src = open(os.path.join(ROOT, "src", "diorama", "scene", "props.c"), encoding="utf-8").read()
    arrays = {}
    for m in re.finditer(r"static const struct DioramaProp (s\w+)\[\] = \{(.*?)\n\};", src, re.S):
        arrays[m.group(1)] = {int(x, 16) for x in re.findall(r"\b[A-Z_]+\((0x[0-9A-Fa-f]+)", m.group(2))}
    return {symbol: arrays.get(name, set())
            for symbol, name in re.findall(r'TILESET\("(\w+)", (s\w+)\)', src)}


class Map:
    def __init__(self, map_json, layout_index):
        self.json = map_json
        self.name = map_json["name"]
        self.art = vb.LayoutArt(map_json["layout"])
        self.layout_index = layout_index
        art = self.art
        self.attrs_p = vb.read_u16(art.attrs[art.primary])
        self.attrs_s = vb.read_u16(art.attrs[art.secondary])

    def cell(self, x, y):
        return self.art.blocks[y * self.art.w + x]

    def blocked(self, x, y):
        return (self.cell(x, y) & 0xC00) != 0

    def behaviour(self, x, y):
        m = self.art.metatile(x, y)
        a = self.attrs_p[m] if m < vb.NUM_PRIMARY else self.attrs_s[m - vb.NUM_PRIMARY]
        return a & 0xFF


def find(map_, door, props, names, owned):
    """The cells of the building over `door`, or None (no building there,
    or one a spec already models)."""
    art = map_.art
    x, y = door
    if y == 0:
        return None
    key = (map_.name, x, y)
    over = OVERRIDES.get(key, {})
    if over.get("skip"):
        return None
    taken = props.get(art.secondary, set())

    def part(cx, cy):
        if not (0 <= cx < art.w and 0 <= cy < art.h):
            return False
        m = art.metatile(cx, cy)
        if m in taken or (m < vb.NUM_PRIMARY and m not in GENERAL_BUILDING):
            return False
        return map_.blocked(cx, cy) or names.get(map_.behaviour(cx, cy)) in DOORS

    if not part(x, y - 1):
        return None
    ground = ground_of(map_, {(x, y)})
    ground_tiles = art.ground_tiles(ground)
    ground_px = art.ground_pixels(ground)
    looks = {}

    def look(cx, cy):
        """(how much of the cell the drawing covers, how much of that is
        leaves)"""
        if (cx, cy) not in looks:
            img = art.building_art(cx, cy, 1, 1, ground_tiles, ground_px=ground_px)
            drawn = [p for p in img.getdata() if p[3] >= 128]
            green = sum(1 for (r, g, b, a) in drawn if g > r + 16 and g > b + 8)
            looks[(cx, cy)] = (len(drawn) / 256.0, green / float(max(1, len(drawn))))
        return looks[(cx, cy)]

    # Its front: the door's row, as far as the cells left and right of the
    # door are the building's - drawn over all or nearly all of the cell (a
    # fence beside a house lets the ground show through).
    front = [x]
    for step in (-1, 1):
        cx = x + step
        while part(cx, y) and look(cx, y)[0] >= FRONT_COVER and len(front) < MAX_WIDTH:
            front.append(cx)
            cx += step
    # Then up from the front and out to the sides, never south of it: a
    # cell beside one of the building's is the building's unless it is
    # leaves (the trees between two houses); straight up a column, leaves are
    # the building's too (a tree house's crown).
    cells = {(cx, y) for cx in front}
    # Wings and eaves reach out past the front, but not far: a row of
    # buildings whose roofs touch is not one building.
    reach = max(3, max(front) - min(front) + 1)
    lo, hi = min(front) - reach, max(front) + reach
    todo = deque(cells)
    while todo:
        cx, cy = todo.popleft()
        for nx, ny, up in ((cx, cy - 1, True), (cx - 1, cy, False), (cx + 1, cy, False)):
            if (nx, ny) in cells or ny >= y or not lo <= nx <= hi or not part(nx, ny):
                continue
            if not up and look(nx, ny)[1] >= LEAVES:
                continue
            cells.add((nx, ny))
            todo.append((nx, ny))
            if len(cells) > MAX_WIDTH * MAX_DEPTH:
                return {"cells": cells, "too_big": True}
    cells |= {tuple(c) for c in over.get("cells", ())}
    cells -= {tuple(c) for c in over.get("drop", ())}
    if "top" in over:
        cells = {c for c in cells if c[1] >= over["top"]}
    if "cols" in over:
        cells = {c for c in cells if over["cols"][0] <= c[0] <= over["cols"][1]}
    xs = [c[0] for c in cells]
    ys = [c[1] for c in cells]
    if max(xs) - min(xs) + 1 > MAX_WIDTH or max(ys) - min(ys) + 1 > MAX_DEPTH:
        return {"cells": cells, "too_big": True}
    if any((cx, cy) in owned for cx, cy in cells):
        return None
    return {"cells": cells, "too_big": False, "front": (min(front), max(front))}


def ground_of(map_, cells):
    """The commonest walkable metatiles round the building's cells: its
    ground (up to three)."""
    art = map_.art
    count = {}
    for cx, cy in cells:
        for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1),
                       (cx + 1, cy + 1), (cx - 1, cy + 1)):
            if (nx, ny) in cells or not (0 <= nx < art.w and 0 <= ny < art.h):
                continue
            if map_.blocked(nx, ny):
                continue
            m = art.metatile(nx, ny)
            count[m] = count.get(m, 0) + 1
    return [m for m, _ in sorted(count.items(), key=lambda kv: -kv[1])[:3]] or [0x001]


def building_art(map_, cells, ground=None):
    """(x0, y0, w, h), the owned cells within it, the ground metatiles and
    the drawing with its ground transparent."""
    art = map_.art
    x0, y0 = min(c[0] for c in cells), min(c[1] for c in cells)
    w = max(c[0] for c in cells) - x0 + 1
    h = max(c[1] for c in cells) - y0 + 1
    owned = {(cx - x0, cy - y0) for cx, cy in cells}
    if ground is None:
        ground = ground_of(map_, cells)
    img = art.building_art(x0, y0, w, h, art.ground_tiles(ground), cells=owned,
                           ground_px=art.ground_pixels(ground))
    return (x0, y0, w, h), owned, ground, img


def _glassy(c):
    r, g, b = c
    return b - r >= 40 and g - r >= 24 and g >= 112


def facade_height(img, door_u):
    """The facade's height in pixels: from the bottom of the drawing up to
    the eave - the first long straight edge across the building above the
    door and above every window."""
    W, H = img.size
    px = img.load()
    solid = [[px[u, v][3] >= 128 for v in range(H)] for u in range(W)]
    bottom = H
    # The door and its lintel: a facade is at least this tall.
    lowest = bottom - 20
    limit = lowest
    best = None
    for v in range(limit, 3, -1):
        both = [u for u in range(W) if solid[u][v] and solid[u][v - 1]]
        if len(both) < W * 0.5:
            continue
        changed = sum(1 for u in both
                      if sum(abs(a - b) for a, b in zip(px[u, v][:3], px[u, v - 1][:3])) > 72)
        if changed >= 0.75 * len(both):
            best = v
            break
    if best is None:
        best = limit
    return max(16, bottom - best)


def north_row(map_, cells, ground):
    """Cells over the building's north edge that the player walks on but the
    drawing paints with the building (the top of a roof seen at 45 degrees):
    part of its drawing, standing on nothing."""
    art = map_.art
    tops = {}
    for cx, cy in cells:
        tops[cx] = min(tops.get(cx, cy), cy)
    ground_tiles = art.ground_tiles(ground)
    ground_px = art.ground_pixels(ground)
    out = set()
    for cx, top in tops.items():
        cy = top - 1
        if cy < 0 or map_.blocked(cx, cy) or art.metatile(cx, cy) < vb.NUM_PRIMARY:
            continue
        img = art.building_art(cx, cy, 1, 1, ground_tiles, ground_px=ground_px)
        if sum(1 for a in img.getchannel("A").getdata() if a >= 128) >= 24:
            out.add((cx, cy))
    return out


def auto_specs(taken, only_map=None):
    """Generator specs (gen_voxel_buildings.py) for every building no spec
    models: `taken` maps a layout id to the cells the spec models stand on.
    Each is a Relief read off its own drawing, standing where it was found."""
    names = behaviours()
    props = catalogue_props()
    layouts = [l for l in json.load(open(os.path.join(ROOT, "data", "layouts", "layouts.json")))["layouts"]]
    index_of = {l["id"]: i + 1 for i, l in enumerate(layouts) if l}
    specs, done = [], set()
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "maps", "*", "map.json"))):
        data = json.load(open(path, encoding="utf-8"))
        if data.get("map_type") not in OUTDOOR or "Frlg" in data["name"] or data["name"] in SKIP_MAPS:
            continue
        if only_map and data["name"] != only_map:
            continue
        map_ = None
        found_here = []
        for warp in data.get("warp_events", []):
            if map_ is None:
                map_ = Map(data, index_of[data["layout"]])
            x, y = warp["x"], warp["y"]
            if ((data["layout"], x, y) in done or names.get(map_.behaviour(x, y)) not in DOORS
                    or any((x, y - 1) in f["cells"] for (_, f) in found_here)):
                continue
            found = find(map_, (x, y), props, names, taken.get(data["layout"], set()))
            if found is None:
                continue
            if found["too_big"]:
                print("auto buildings: %s door %d,%d: no building's shape (%d cells), left flat"
                      % (data["name"], x, y, len(found["cells"])))
                continue
            if max(c[0] for c in found["cells"]) == min(c[0] for c in found["cells"]):
                # one cell wide: a gap in a row of trees (a wood's way in)
                continue
            found_here.append(((x, y), found))
        # Two buildings whose roofs touch: each cell goes to the one whose
        # front is nearer below it.
        owner = {}
        for k, (_, f) in enumerate(found_here):
            for c in f["cells"]:
                owner.setdefault(c, []).append(k)
        for k, ((x, y), f) in enumerate(found_here):
            def gap(c, front):
                return max(front[0] - c[0], c[0] - front[1], 0)
            cells = {c for c in f["cells"]
                     if min(owner[c], key=lambda j: (gap(c, found_here[j][1]["front"]), j)) == k}
            if len(cells) < 2:
                continue
            ground = ground_of(map_, cells)
            body_top = min(c[1] for c in cells)
            cells = cells | north_row(map_, cells, ground)
            for c in cells:
                done.add((data["layout"], c[0], c[1]))
            (x0, y0, w, h), owned, _, img = building_art(map_, cells, ground)
            over = OVERRIDES.get((data["name"], x, y), {})
            height = over.get("facade") or facade_height(img, (x - x0) * 16)
            relief = {"height": height, "back": (body_top - y0) * 16, "auto": True}
            band = roof_band(h * 16 - height)
            if band is not None:
                relief["fill"] = band
            specs.append({
                "name": "auto_%s_%d_%d" % (data["name"].lower(), x, y),
                "layout": data["layout"],
                "rect": (x0, y0, w, h),
                "ground": ground,
                "owned": owned,
                "relief": relief,
            })
    return specs


def roof_band(roof_rows):
    """The drawing's rows (v0, v1) a roof's hidden depth is laid with: the
    middle of the roof, between the outline at its back and the eave at its
    front, so the depth the drawing does not show repeats roof tiles rather
    than the eave (which, repeated, read as a second roof from above). None
    for a roof too shallow to have a middle."""
    if roof_rows < 12:
        return None
    return (int(roof_rows * 0.3), max(int(roof_rows * 0.3) + 4, int(roof_rows * 0.65)))


def modelled_cells(path):
    """{layout index: cells} a model of buildings.bin (VXB7) stands on."""
    import struct

    blob = open(path, "rb").read()
    (pages, models, page_models, placements, height_bytes, masks, vertices,
     variants, _) = struct.unpack_from("<HHHHHHIHH", blob, 4)
    head = 4 + struct.calcsize("<HHHHHHIHH")
    body = head + 8 * pages
    model = [struct.unpack_from("<BBHIII", blob, body + 16 * i) for i in range(models)]
    page_model = [struct.unpack_from("<HHhh", blob, body + 16 * models + 8 * i)
                  for i in range(page_models)]
    place_at = body + 16 * models + 8 * page_models
    heights = place_at + 16 * placements
    out = {}
    for i in range(placements):
        layout, pm, x, y = struct.unpack_from("<HHHH", blob, place_at + 16 * i)
        w, h, _, _, _, first = model[page_model[pm][0]]
        cells = out.setdefault(layout, set())
        for cy in range(h):
            for cx in range(w):
                if blob[heights + first + cy * w + cx] != 0xFF:
                    cells.add((x + cx, y + cy))
    return out


def all_buildings(only=None, modelled=None):
    """Every unmodelled building of the outdoor maps: (map, door, found).
    `modelled`: modelled_cells() of the specs' models, whose buildings are
    left to them."""
    names = behaviours()
    props = catalogue_props()
    modelled = modelled or {}
    layouts = [l for l in json.load(open(os.path.join(ROOT, "data", "layouts", "layouts.json")))["layouts"]]
    index_of = {l["id"]: i + 1 for i, l in enumerate(layouts) if l}
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "maps", "*", "map.json"))):
        data = json.load(open(path, encoding="utf-8"))
        if data.get("map_type") not in OUTDOOR or "Frlg" in data["name"]:
            continue
        if only and data["name"] != only:
            continue
        map_ = None
        seen = set()
        for warp in data.get("warp_events", []):
            if map_ is None:
                map_ = Map(data, index_of[data["layout"]])
            x, y = warp["x"], warp["y"]
            if names.get(map_.behaviour(x, y)) not in DOORS:
                continue
            found = find(map_, (x, y), props, names, modelled.get(map_.layout_index, set()))
            if found is None or (x, y) in seen:
                continue
            seen |= found["cells"]
            out.append((map_, (x, y), found))
    return out


def sheet(out_dir, only=None, modelled=None):
    from PIL import Image, ImageDraw

    os.makedirs(out_dir, exist_ok=True)
    for map_, (dx, dy), found in all_buildings(only, modelled):
        cells = found["cells"]
        x0, x1 = min(c[0] for c in cells) - 1, max(c[0] for c in cells) + 2
        y0, y1 = min(c[1] for c in cells) - 1, max(c[1] for c in cells) + 2
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(map_.art.w, x1), min(map_.art.h, y1)
        img = Image.new("RGB", ((x1 - x0) * 16, (y1 - y0) * 16))
        for cy in range(y0, y1):
            for cx in range(x0, x1):
                img.paste(map_.art.cell_image(map_.art.metatile(cx, cy)).convert("RGB"),
                          ((cx - x0) * 16, (cy - y0) * 16))
        scale = 3
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        draw = ImageDraw.Draw(img)
        if not found["too_big"]:
            (bx, by, bw, bh), owned, ground, bimg = building_art(map_, cells)
            height = facade_height(bimg, (dx - bx) * 16)
            eave = ((by + bh - y0) * 16 - height) * scale
            draw.line(((bx - x0) * 16 * scale, eave, (bx + bw - x0) * 16 * scale, eave),
                      fill=(255, 40, 40), width=2)
            found["facade"] = height
        for cx, cy in cells:
            r = ((cx - x0) * 16 * scale, (cy - y0) * 16 * scale,
                 (cx - x0 + 1) * 16 * scale - 1, (cy - y0 + 1) * 16 * scale - 1)
            draw.rectangle(r, outline=(255, 0, 255) if found["too_big"] else (255, 220, 0))
        draw.rectangle(((dx - x0) * 16 * scale + 4, (dy - y0) * 16 * scale + 4,
                        (dx - x0 + 1) * 16 * scale - 5, (dy - y0 + 1) * 16 * scale - 5),
                       outline=(0, 255, 255), width=3)
        img.save(os.path.join(out_dir, "%s_%d_%d.png" % (map_.name, dx, dy)))
        print("%-22s door %2d,%2d  cells %3d  facade %s %s" % (
            map_.name, dx, dy, len(cells), found.get("facade"), "TOO BIG" if found["too_big"] else ""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sheet")
    parser.add_argument("--map")
    parser.add_argument("--modelled", help="buildings.bin: leave its buildings out")
    args = parser.parse_args()
    if args.sheet:
        sheet(args.sheet, args.map, modelled_cells(args.modelled) if args.modelled else None)


if __name__ == "__main__":
    main()
