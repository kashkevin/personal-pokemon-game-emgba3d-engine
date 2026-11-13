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
    its drawing goes on (a statue's head over the floor behind it), is its too - but not a
    floor cell that is only the floor shaded or patterned in its own colours, apart from its
    bottom row (KNOWN_ISSUES MS-227).
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


def _stairwell():
    import voxel_building_specs as v
    return v.stairwell

MAX_WALL = 48       # rows of a wall's face that stand; more of a drawn wall is its top
THING = 20          # rows of front a piece of furniture stands at most
DARK = 40           # a pixel at most this bright is the black outside a room


# Blocked cells that stand as a thing of their own, not joined to the blocked cells beside them.
# The Sootopolis Gym 1F's two ice rocks at the foot of an ice ledge (0x216, the rock on the ice's
# highlight) were read with the ledge's column as one piece, the ledge standing a row out with the
# rock drawn up its face, where the room's six rocks on the open ice stand alone (KNOWN_ISSUES
# MS-227). Its two rocks behind a ledge (11, 9 and 9, 19) are not here: standing alone they would
# have no front on the floor and lie flat behind the ledge's 20 rows, hidden; as one piece with
# the ledge they lie on its top.
ALONE = {
    "LAYOUT_SOOTOPOLIS_CITY_GYM_1F": {(5, 6), (7, 17)},
}

# Cells the game's scripts change that stand with the room's wall all the same (`solid` leaves
# out every cell the game names, dynamic_metatiles). The Battle Pike's rooms have a curtain in the
# middle of the back wall, its frames set by setmetatile when the Pike Queen comes out of it (in
# the normal room only, the one time it moves). Left out, the curtain lay back with the room's
# drawing between the wall standing on either side of it, black over it where the 2D game draws
# the curtain's top (KNOWN_ISSUES MS-236). It stands with the wall as drawn, open with the stage
# behind it; the people who leave through it walk into it. While the Queen's curtain moves its
# cells are not the drawing its pieces were made from, so the renderer leaves those pieces out
# (buildings.c DioramaBuildings_Validate) and the room's own drawing shows the curtain's frames;
# they stand again when it is open as drawn (the same metatiles). The passable cell at its foot
# (4, 3) stays floor.
_PIKE_CURTAIN = {(4, 1), (3, 2), (4, 2), (5, 2), (3, 3), (4, 3), (5, 3)}
STAND = {
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL": _PIKE_CURTAIN,
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS": _PIKE_CURTAIN,
}

# Rooms whose walls stand no taller than this many rows, the rest of a wall's drawing laid back as
# its top (MAX_WALL elsewhere). Each column's wall run stands to its own foot: in the Sootopolis
# Gym 1F the back wall is drawn 32 rows tall on row 2, its two waterfalls 48 on row 3 and its thick
# side walls are runs down to row 14, so the waterfalls and the side walls stood 48 tall a row and
# more in front of a 32-row wall. At the GBA's 45 degrees their tops line up; from the 3D cameras
# their tops stood over the wall's beside them, the outside black between: a notch over columns 3
# and 13 and a strip beside each side wall (KNOWN_ISSUES MS-237). At the back wall's own 32 every
# piece's top meets the wall's at the same height and depth: the waterfalls' and the side walls'
# top rows lie back from the wall's top, as the drawing has them at 45 degrees.
LEVEL_WALL = {
    "LAYOUT_SOOTOPOLIS_CITY_GYM_1F": 32,
}

# Stairwells modelled by hand, by their metatiles (the secondary tileset's): the Facility tileset's
# flight up, three cells wide on a back wall's two rows - Mt. Pyre's floors 1F-5F, Team Aqua's
# hideout B1F and B2F, the Space Center 1F. The reader stood its outer columns (the posts and the
# lintel's ends, 32 rows) on the wall's foot and its middle column, the lintel one row tall over the
# flight's own cell, a row back and 16 rows tall: at the GBA's 45 degrees their tops line up, but
# from the 3D cameras the lintel's top lay below the posts', the outside black over it and down the
# posts' inner edges (a wall's face has no sides), and, beside a floor's taller side wall whose top
# starts a row behind the stairwell's face, in a wedge between the two (KNOWN_ISSUES MS-237; the
# owner's decision #56 of 2026-11-13: a model of the stairwell by hand closing its sides, the walls
# round it keeping their heights). By hand (stairwell_pieces): the frame - the lintel, 12 rows, and
# the posts, 8 columns each - one wall on the stairwell's foot, its doorway open; the doorway's
# recess behind it, its sides and back in the flight's dark, as a house's stairs are
# (voxel_building_specs.stairwell); and a taller wall beside it carries its side on to the frame's
# face (a cheek, its top on the row the walls' tops are drawn on). At 45 degrees the room is its
# drawing again; nothing rises above the walls' tops; the walls round it keep their heights.
STAIRWELL = {
    "gTileset_Facility": ((0x2B3, 0x2B4, 0x2B5), (0x2BB, 0x2BC, 0x2BD)),
}
STAIRWELL_LINTEL = 12   # rows of the lintel over the doorway (its white top, its face, its shadow)
STAIRWELL_POST = 8      # columns of each post beside the doorway


def stairwells(layout, walls):
    """[(x, y)] of the stairwells (STAIRWELL) a layout has whose columns the reader read as the
    frame's walls - the posts' runs two rows from the wall's top, the middle's one - and the
    (column, ya, yb) runs they stand for; a stairwell read otherwise is left to the reader."""
    tiles = STAIRWELL.get(layout.secondary)
    found, runs = [], set()
    if not tiles:
        return found, runs
    for y in range(layout.h - 1):
        for x in range(layout.w - 2):
            if not all(layout.metatile(x + i, y) == tiles[0][i]
                       and layout.metatile(x + i, y + 1) == tiles[1][i] for i in range(3)):
                continue
            mine = [(x, y, y + 2), (x + 1, y, y + 1), (x + 2, y, y + 2)]
            if all(c in walls.get((ya, yb), ()) for (c, ya, yb) in mine):
                found.append((x, y))
                runs.update(mine)
    return found, runs


_BEHAVIOURS = None


def _behaviour_names():
    global _BEHAVIOURS
    if _BEHAVIOURS is None:
        text = open(os.path.join(vb.ROOT, "include/constants/metatile_behaviors.h")).read()
        body = text[text.index("MB_NORMAL"):]
        body = body[:body.index("}")]
        # A name a comment mentions is no value of its own (KNOWN_ISSUES MS-66: MB_INTERIOR_DEEP_WATER's comment names
        # MB_DEEP_WATER, and every behaviour from MB_WATERFALL on read as the one before it - ice as a Mart's sign, sand
        # as ice).
        body = re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", body), flags=re.S)
        _BEHAVIOURS = [n.split("=")[0].strip() for n in re.findall(r"(MB_\w+[^,\n]*)", body)]
    return _BEHAVIOURS


# Blocked cells that are no thing standing: water (a pool, a fountain's basin - its surface is
# the floor's level), the void of a hole, a waterfall's face.
LEVEL = re.compile(r"MB_(POND_WATER|INTERIOR_DEEP_WATER|DEEP_WATER|OCEAN_WATER|SOOTOPOLIS_DEEP_WATER|"
                   r"WATERFALL|NO_SURFACING|SEAWEED\w*|\w+_CURRENT|SHALLOW_WATER|PUDDLE|"
                   r"CRACKED_FLOOR_HOLE|LAVARIDGE_GYM_B1F_WARP|HOT_SPRINGS|ICE|THIN_ICE|CRACKED_ICE)$")
# Of those, ice is a floor the room is walked on (the Sootopolis Gym's), not a surface under it: its cells are the
# room's ground, as water's are not.
ICE_FLOOR = re.compile(r"MB_(ICE|THIN_ICE|CRACKED_ICE)$")


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


def analyse(layout_id, taken=(), drop=()):
    """(ground metatiles, pieces, notes) for a layout; `taken`: cells the pieces of rooms
    modelled before it already stand in (left to them); `drop`: pieces left out by name (a
    piece the proof found not to reproduce its drawing, battle_room_specs.AUTO_DROP)."""
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
    stand = STAND.get(layout_id, set())
    max_wall = min(MAX_WALL, LEVEL_WALL.get(layout_id, MAX_WALL))
    solid = [[blocked(x, y) and not void[y][x] and not level[y][x] and (x, y) not in taken
              and (not changes(meta(x, y)) or (x, y) in stand) for x in range(W)] for y in range(H)]

    counts = collections.Counter(meta(x, y) for y in range(H) for x in range(W)
                                 if floor[y][x] and (not level[y][x] or ICE_FLOOR.match(behaviour(meta(x, y)))))
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

    ground_colours = {p[:3] for g in ground_px for p in g}

    def floor_shade(x, y):
        """Whether a floor cell differs from the floor only as the floor shaded or patterned: every
        pixel that differs is one of the floor's own colours and none is in its bottom row, where a
        thing's drawing would run on into it from the cell below (KNOWN_ISSUES MS-227: the Fortree
        Gym's shade north of its hedges, the Sootopolis Gym's ice highlights north of its rocks)."""
        px = _cell_pixels(L, meta(x, y), cache)
        for i, p in enumerate(px):
            if all(g[i][:3] != p[:3] for g in ground_px) and (i >= 15 * 16 or p[:3] not in ground_colours):
                return False
        return True

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
    # A lift's or a cabin's door in a wall (KNOWN_ISSUES MS-236): its own cell is one the game's
    # door table names (field_door.c), so `solid` leaves it out, and its column was no wall: it
    # lay back with the room's drawing between the upright wall on either side of it, the outside
    # black over it. The door's animation draws over the map, its metatile unchanged, and the
    # renderer opens a door on the model that stands on its cell (FindDoor), so a run of door
    # columns whose cells, the door's own counted, make the same wall as the columns on both
    # sides of it is that wall. A door beside no such wall stays as it was (opening flat, V6).
    runs_at = collections.defaultdict(set)      # a column's wall runs (a column may have several)
    for run, cols in walls.items():
        for c in cols:
            runs_at[c].add(run)

    def door_run(x):
        """The column's run counting its door cells solid, when it has a door: (ya, yb) or None."""
        cells = [y for y in range(H) if behaviour(meta(x, y)) == "MB_ANIMATED_DOOR" and blocked(x, y)
                 and not void[y][x] and not level[y][x] and (x, y) not in taken]
        if not cells:
            return None
        part = [solid[y][x] or y in cells for y in range(H)]
        y = 0
        while y < H and not part[y]:
            y += 1
        ya = y
        while y < H and part[y]:
            y += 1
        yb = y
        if not (ya <= min(cells) and max(cells) < yb):
            return None
        return (ya, yb) if (ya == 0 or void[ya - 1][x]) and yb < H and floor[yb][x] else None

    x = 0
    while x < W:
        run = door_run(x)
        if run is None or run in runs_at[x]:
            x += 1
            continue
        end = x
        while end + 1 < W and door_run(end + 1) == run and run not in runs_at[end + 1]:
            end += 1
        # a door's dark top (a cabin's: the doorway's black over the door, read as the outside)
        # is the wall's too when the wall beside it starts there
        for side in sorted(runs_at[x - 1]) if x > 0 else ():
            if side[1] == run[1] and side[0] < run[0] \
                    and all(void[yy][c] for c in range(x, end + 1) for yy in range(side[0], run[0])):
                run = side
                break
        if x > 0 and end + 1 < W and run in runs_at[x - 1] and run in runs_at[end + 1]:
            for c in range(x, end + 1):
                walls[run].append(c)
                runs_at[c].add(run)
                wall_cells.update((c, yy) for yy in range(*run))
            notes.append("door in the wall: columns %d-%d, rows %d-%d" % (x, end, run[0], run[1] - 1))
        x = end + 1
    # the room's wall height: the most common standing face
    heights = collections.Counter()
    for (ya, yb), cols in walls.items():
        heights[min(max_wall, (yb - ya) * 16)] += len(cols)
    wall_h = heights.most_common(1)[0][0] if heights else 32
    # stairwells modelled by hand (STAIRWELL): their columns' runs are theirs, not walls'
    hand_stairs, hand_runs = stairwells(L, walls)
    for (sx, sy) in hand_stairs:
        notes.append("stairwell by hand: columns %d-%d, rows %d-%d" % (sx, sx + 2, sy, sy + 1))

    pieces = []
    # things and edges: connected blocked cells that are not a wall's face
    seen = set()
    things = []
    alone = ALONE.get(layout_id, set())
    for y in range(H):
        for x in range(W):
            if not solid[y][x] or (x, y) in wall_cells or (x, y) in seen:
                continue
            comp, todo = [], [(x, y)]
            seen.add((x, y))
            while todo:
                cx, cy = todo.pop()
                comp.append((cx, cy))
                if (cx, cy) in alone:
                    continue
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < W and 0 <= ny < H and solid[ny][nx] and (nx, ny) not in wall_cells \
                            and (nx, ny) not in seen and (nx, ny) not in alone:
                        seen.add((nx, ny))
                        todo.append((nx, ny))
            things.append(comp)

    def runs_to_floor(comp):
        """Whether any column of the component ends on the floor (it has a front to stand); a
        thing standing alone (ALONE) in front of it stands on the floor it would."""
        cells = set(comp)
        for (cx, cy) in comp:
            if (cx, cy + 1) not in cells and cy + 1 < H and (floor[cy + 1][cx] or (cx, cy + 1) in alone):
                return True
        return False

    def wall_pieces():
        out = []
        for k, ((ya, yb), cols) in enumerate(sorted(walls.items(), key=lambda kv: (kv[0], kv[1][0]))):
            cols = sorted(c for c in cols if (c, ya, yb) not in hand_runs)
            if not cols:
                continue
            # contiguous runs of columns
            start = prev = cols[0]
            for c in cols[1:] + [None]:
                if c is not None and c == prev + 1:
                    prev = c
                    continue
                x0, x1 = start * 16, (prev + 1) * 16
                tall = min(max_wall, (yb - ya) * 16)
                shape = [(x0, ya * 16, x1, yb * 16)]
                side = (x0, max(ya * 16, yb * 16 - tall), x0 + 16, yb * 16)
                out.append(piece("wall_%d_%d" % (start, ya), shape, tall, fill=16, foot=yb * 16,
                                 side=side))
                if c is not None:
                    start = prev = c
        return out

    def one_foot(comp):
        """A component as pieces that each stand on one foot: as it is when its cells fill its
        rectangle (a statue, a machine); else its columns' runs of cells, neighbouring columns
        with the same run one piece - a piece stands on its lowest row and lays the rest of its
        drawing back as its top, which over a maze's or a ring's floor would be a slab."""
        cells = set(comp)
        xs = [x for x, _ in comp]
        ys = [y for _, y in comp]
        if len(cells) == (max(xs) - min(xs) + 1) * (max(ys) - min(ys) + 1):
            return [comp]
        runs = []          # (ya, yb, x)
        for x in sorted(set(xs)):
            col = sorted(y for cx, y in comp if cx == x)
            start = prev = col[0]
            for y in col[1:] + [None]:
                if y is not None and y == prev + 1:
                    prev = y
                    continue
                runs.append((start, prev + 1, x))
                if y is not None:
                    start = prev = y
        out, cur = [], None
        for ya, yb, x in sorted(runs):
            if cur is not None and cur[0] == (ya, yb) and cur[1][-1] == x - 1:
                cur[1].append(x)
            else:
                cur = ((ya, yb), [x])
                out.append(cur)
        return [[(x, y) for x in cols for y in range(ya, yb)] for (ya, yb), cols in out]

    def thing_pieces():
        out = []
        for k, comp in enumerate(part for whole in things for part in one_foot(whole)):
            if not runs_to_floor(comp):
                notes.append("left flat: %d cells at %s (no front on the floor)" % (len(comp), comp[0]))
                continue
            cells = set(comp)
            beside_out = any(void[ny][nx] for (cx, cy) in comp
                             for (nx, ny) in ((cx + 1, cy), (cx - 1, cy), (cx, cy - 1))
                             if 0 <= nx < W and 0 <= ny < H)
            rects = [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16) for (cx, cy) in comp]
            # its drawing over the floor cell north of it (not the floor shaded there: it stood the
            # piece 20 rows tall with the floor laid on its top, MS-227)
            for (cx, cy) in comp:
                if (cx, cy - 1) not in cells and cy - 1 >= 0 and floor[cy - 1][cx] \
                        and differs_from_floor(cx, cy - 1) and not floor_shade(cx, cy - 1):
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

    def stairwell_pieces(walls_built):
        """The stairwells' frames and recesses (STAIRWELL), and the cheeks of the taller walls
        beside them: a wall standing on the stairwell's top row whose top starts behind the
        frame's face (a floor's side wall, 48 rows of face on a longer run) carries its side on
        from where its top starts to the frame's face, its top on that row at 45 degrees (h = z -
        the row), drawn with the wall's own edge column as its side is (vb.Cheek)."""
        stairwell = _stairwell()
        out = []
        for (sx, sy) in hand_stairs:
            X, Y = sx * 16, sy * 16
            foot = Y + 32
            lintel, post = Y + STAIRWELL_LINTEL, STAIRWELL_POST
            # the doorway's recess, its far ends open; its sides and back the flight's dark (the
            # doorway's top rows under the lintel)
            recess = stairwell("stairwell_%d_%d" % (sx, sy), X + post, X + 48 - post, foot, lintel,
                               foot - lintel, (X + post + 1, lintel, X + 48 - post - 1, Y + 16), Y)
            # the doorway's drawing is the flight's, never the room's floor
            recess["not_floor"] = [(X + post, lintel, X + 48 - post, foot)]
            out.append(recess)
            out.append(piece("stairwell_frame_%d_%d" % (sx, sy),
                             [(X, Y, X + 48, lintel), (X, lintel, X + post, foot),
                              (X + 48 - post, lintel, X + 48, foot)],
                             32, fill=16, foot=foot, side=(X, Y, X + 16, foot)))
            for c, x_edge, facing in ((sx - 1, X, 1), (sx + 3, X + 48, -1)):
                for pc in walls_built:
                    (x0, y0, x1, y1) = pc["shape"][0]
                    if not (x0 <= c * 16 < x1 and y0 == Y):
                        continue
                    tall, foot_n = pc["height"], pc["foot"]
                    back_n = Y + tall if foot_n - Y > tall else foot_n
                    if back_n > foot:
                        u = x1 - 0.5 if facing > 0 else x0 + 0.5
                        pc.setdefault("cheeks", []).append((x_edge, foot, back_n, Y, u, facing))
                        notes.append("%s: cheek at x %d, z %d-%d" % (pc["name"], x_edge, foot, back_n))
        return out

    pieces += thing_pieces()
    built = wall_pieces()
    pieces += built
    pieces += stairwell_pieces(built)
    # the room's sides where its floor reaches the map's edge
    back_foot = min((yb for (ya, yb) in walls), default=None)
    if back_foot is not None:
        sample = next(((cols[0] * 16, yb * 16 - min(max_wall, (yb - ya) * 16), cols[0] * 16 + 16, yb * 16)
                       for (ya, yb), cols in sorted(walls.items())), None)
        for edge_x, name in ((0, "side_w"), (W - 1, "side_e")):
            rows = [y for y in range(back_foot, H) if floor[y][edge_x]]
            if not rows:
                continue
            # The wall's cells are drawn with the room's floor (ground[0]): it runs along the
            # longest stretch with no other floor in its column (a shaded variant, a mat), which
            # would lose its own drawing under it. It closes the room's floor (KNOWN_ISSUES
            # MS-232): it starts past the outside and a wall's face in its column (the black
            # beside a gym's alcove, the back wall's own cells, where it stood behind the back
            # wall and its far end showed over it), the outside ends it, and a stretch with no
            # floor in it (furniture against the edge) is none.
            stretches, start = [], None
            for y in range(back_foot, max(rows) + 2):
                if start is None and y <= max(rows):
                    if void[y][edge_x] or (edge_x, y) in wall_cells:
                        continue
                    start = y
                other = y <= max(rows) and floor[y][edge_x] and not level[y][edge_x] \
                    and meta(edge_x, y) != ground[0]
                outside = y <= max(rows) and void[y][edge_x]
                if other or outside or y == max(rows) + 1:
                    if start is not None and y > start \
                            and any(floor[r][edge_x] for r in range(start, y)):
                        stretches.append((start, y))
                    start = None
            if not stretches:
                continue
            first, end = max(stretches, key=lambda s: (s[1] - s[0], -s[0]))
            if (first, end) != (back_foot, max(rows) + 1):
                notes.append("%s runs rows %d-%d (other floor, the outside or a wall's face in its column)"
                             % (name, first, end - 1))
            front = end * 16
            xpx = 0 if edge_x == 0 else W * 16
            seg = ((xpx, front), (xpx, first * 16)) if edge_x == 0 else ((xpx, first * 16), (xpx, front))
            pieces.append(piece(name, [], wall_h, side=sample, walls=[seg]))
    for name in drop:
        if any(pc["name"] == name for pc in pieces):
            notes.append("left out: %s" % name)
    pieces = [pc for pc in pieces if pc["name"] not in drop]
    return ground, own_shell(pieces), notes


def auto_room_spec(name, layout_id, drop=(), reuse_in=None):
    """A room spec whose pieces are read when the room is built, around the furniture of the
    rooms built before it (gen_voxel_buildings.interior_specs calls them). `reuse_in`: the
    rooms its pieces may stand in besides its own (the rooms proved with it); every room that
    repeats one of their tiles when None."""
    drop = tuple(drop)
    ground, pieces, notes = analyse(layout_id, drop=drop)
    if not pieces:
        return None
    return {"name": name, "auto": True, "notes": notes,
            "interior": {"layout": layout_id, "ground": ground,
                         "reuse_in": None if reuse_in is None else frozenset(reuse_in),
                         "pieces": lambda layout, taken: analyse(layout_id, taken, drop)[1]}}
