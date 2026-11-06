#!/usr/bin/env python3
"""Furnished rooms modelled piece by piece (project-owned; KNOWN_ISSUES MS-164).

The depth audit of 2026-10-28 listed 35 rooms whose furniture the generic 3D rules leave as a
picture on the floor. A first try read their pieces off their cells (battle_room_auto.py, engine
131de763): it proved against the drawing, but from the game's camera the pieces were boxes with
their drawing run down them, a counter stood as tall as the walls and hid the clerks behind it,
and it was reverted (MS-173). The GBA's projection cannot tell a tall front from a deep top (at 45
degrees both are the same rows of the drawing), so a piece's height has to be chosen, and the
game's camera, which looks down at 42 degrees with perspective (about 25 degrees at the back of a
room), shows a wrong choice at once.

These rooms are cut by hand as the 3DS project cut its rooms (voxel_building_specs.py, the
Pokemon Center, the Mart): each piece as tall as its drawn front, a counter or a table low enough
that whoever stands behind it is seen over it, a stool 6 rows, a plant a card; walls stand their
drawn face. Every piece and every room still reproduces the drawing pixel for pixel in the GBA's
projection (gen_voxel_buildings.py --verify), and each room is looked at from the game's camera
before it is listed. Their pieces stand in their own room only (`reuse_in`: none), so a tile
they share with another room does not stand them there - but for Team Magma's hideout's machines,
which stand in every room of the hideout (KNOWN_ISSUES E9).

Conventions as in voxel_building_specs.py: shapes are pixels of the room's drawing; `height` is
the rows of a piece's front, standing at its drawn foot, the rest of what it claims above that
its top, laid back at 45 degrees; `back` the Z its top runs back to; `leave` the colours it does
not claim where they run on out of its shape (the floor round a stool, the wall round a plant);
`fill` P fills what pieces in front hide of a wall from its own pixels P columns away; `side` the
stretch of drawing that dresses the faces the drawing never shows; side walls run ((x, z), (x, z))
facing into the room (west walls front to back, east walls back to front). Front (south) walls are
never stood up.
"""


def _helpers():
    """voxel_building_specs' piece helpers, imported when the specs are made (that module takes
    this one's specs at its end: importing it at the top would be circular)."""
    import voxel_building_specs as v
    return v.facet, v.own_shell, v.piece


# ── Mauville City's Game Corner ───────────────────────────────────────────
#
# 22 x 11 cells. From the top of the drawing:
#
#     rows  0-  8  the back wall's top band; east of x 288 the wall stands a cell further
#                  forward, its top rows 0-24 (white, then the band), its face rows 24-48
#     rows  8- 32  the wall's face: sky panels over a teal baseboard; behind the counter a shelf
#                  and the counter's back corners are drawn on it
#     rows 16- 48  two coin machines (x 0-32) against the wall, a vase (x 32-48), three potted
#                  plants (x 80-128)
#     rows 32- 72  the prize counter, a U round the clerks' floor (x 144-256): its arms x 144-159
#                  and 241-256, its bar's top rows 46-58, its front rows 58-72 - 14 rows, so the
#                  clerks behind it are seen over it from the game's camera
#     rows 47- 64  two benches against the wall that stands forward (x 288-352); a vase at 21,4
#     rows 80-152  two banks of slot machines back to back (x 32-64, x 112-144), their screens
#                  down their sides, facing the stools either side (walkable, as the Center's);
#                  the shadow under them, rows 152-158, is the floor's
#     rows 92-142  two roulette tables (x 224-256, x 288-320): the felt their top, a band and
#                  legs their front, rows 134-142
#     rows 160-176 the doormat (the floor's)

GC_WALL = ("62accd", "eee6f6", "31839c", "52526a", "7b7b8b", "cdcdc5", "bdcde6", "ffffff")
GC_FLOOR = ("d55231", "a44a41", "ac625a", "f65252")
GC_WALL_SIDE = (48, 8, 64, 32)     # one sky panel and its baseboard


def mauville_game_corner():
    facet, own_shell, piece = _helpers()
    stools = [piece("stool_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)],
                    6, leave=GC_FLOOR)
              for cy in range(6, 10) for cx in (1, 4, 6, 9)]
    return own_shell([
        # the tables only as deep as their drawn top: run back to their collision, the felt's
        # rows laid over again showed a second strip of felt behind the wheel
        piece("roulette_w", [(224, 92, 256, 142)], 8, leave=GC_FLOOR),
        piece("roulette_e", [(288, 92, 320, 142)], 8, leave=GC_FLOOR),
        piece("slots_w", [(32, 80, 64, 152)], 14, back=96, leave=GC_FLOOR),
        piece("slots_e", [(112, 80, 144, 152)], 14, back=96, leave=GC_FLOOR),
        piece("vase_e", [(336, 64, 352, 80)], 12, leave=GC_FLOOR),
    ] + stools + [
        piece("bench_w", [(288, 47, 320, 64)], 6, back=48, leave=GC_FLOOR + GC_WALL),
        piece("bench_e", [(320, 47, 352, 64)], 6, back=48, leave=GC_FLOOR + GC_WALL),
        # the counter: its bar and its arms one piece, its front as drawn
        piece("counter", [(144, 32, 159, 72), (241, 32, 256, 72), (144, 46, 256, 72)], 14),
        piece("machines", [(0, 16, 32, 48)], 22, back=32, leave=GC_WALL),
        piece("vase_w", [(32, 32, 48, 48)], 12, leave=GC_FLOOR),
        piece("plant_1", [(80, 16, 96, 48)], 28, leave=GC_WALL, card=True),
        piece("plant_2", [(96, 16, 112, 48)], 28, leave=GC_WALL, card=True),
        piece("plant_3", [(112, 16, 128, 48)], 28, leave=GC_WALL, card=True),
        # the walls: the back one, its face 24 rows standing on row 32; east of x 288 a cell
        # forward, on row 48; the room's sides where the floor reaches the map's edges
        piece("wall_e", [(288, 0, 352, 48)], 24, fill=16, side=GC_WALL_SIDE,
              walls=[((352, 48), (352, 176))]),
        piece("wall", [(0, 0, 288, 32)], 24, fill=16, side=GC_WALL_SIDE,
              walls=[((0, 176), (0, 48))]),
    ])


# ── Mossdeep City's Game Corner, ground floor ─────────────────────────────
#
# 12 x 10 cells. From the top of the drawing:
#
#     rows  0- 10  the back wall's top; rows 10-32 its face: a yellow band between wooden
#                  posts, a notice board (x 2-30), the doorway down (x 80-96)
#     rows  8- 40  a PC (x 160-176), its foot on the floor in front of the wall
#     rows 20- 62  two wooden benches (x 32-80, x 96-144), each with a tall end against the wall
#                  (x 32-48, x 128-144); between them a low yellow gate (x 80-96, rows 46-60)
#     rows 80-126  two glass tables (x 32-64, x 112-144), the stools round them (walkable)
#     rows 144-160 the doormat (the floor's)
#
# Its floor's shaded cells are drawn in the benches' own colours, so the room names no shade:
# each piece leaves the floor round it by its own colours.

MG_FLOOR = ("bda462", "d5b473", "eecda4", "a48b52")
MG_WALL_ONLY = ("ffe64a", "dec541", "836a52", "7b839c")
MG_WALL = MG_WALL_ONLY + ("d59c62", "eecda4", "a48b52", "d5b473", "bda462")
MG_WALL_SIDE = (176, 0, 192, 32)


def mossdeep_game_corner_1f():
    facet, own_shell, piece = _helpers()
    stools = [piece("stool_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)],
                    6, leave=MG_FLOOR)
              for (cx, cy) in ((2, 5), (3, 5), (1, 6), (1, 7), (7, 5), (8, 5), (9, 6), (9, 7))]
    return own_shell([
        piece("table_w", [(32, 92, 64, 126)], 8, leave=MG_FLOOR),
        piece("table_e", [(112, 92, 144, 126)], 8, leave=MG_FLOOR),
    ] + stools + [
        piece("bench_w", [(32, 44, 80, 62)], 8, back=48, leave=MG_FLOOR),
        piece("bench_e", [(96, 44, 144, 62)], 8, back=48, leave=MG_FLOOR),
        piece("gate", [(80, 46, 96, 60)], 4, leave=MG_FLOOR),
        piece("bench_w_end", [(32, 20, 48, 44)], 20, back=32, leave=MG_WALL_ONLY),
        piece("bench_e_end", [(128, 20, 144, 44)], 20, back=32, leave=MG_WALL_ONLY),
        piece("pc", [(160, 8, 176, 40)], 24, back=32, leave=MG_WALL),
        piece("wall", [(0, 0, 192, 32)], 22, fill=16, side=MG_WALL_SIDE,
              walls=[((0, 160), (0, 32)), ((192, 32), (192, 160))]),
    ])


# ── The Battle Frontier's lounges ─────────────────────────────────────────
#
# Two layouts for the nine lounges (LOUNGE1 13 x 8 cells, LOUNGE2 9 x 10), one kit:
#
#     rows  0- 32  the back wall: its white top and grey band, its face orange to yellow down to
#                  a chequered trim
#     rows 18- 46  a potted plant on every cell of the row in front of the wall but one, where a
#                  drinks machine stands (LOUNGE1 x 96-112, rows 13-40; LOUNGE2 x 64-80, rows
#                  29-55), its foot on the floor in front of it, its shadow below that the
#                  floor's; LOUNGE2's stands a cell out from the wall, its depth back to the
#                  wall laid with its grey, not its drawn top's rows over again
#     below        armchairs, walkable as the Center's stools, their backs drawn on their sides;
#                  LOUNGE1 two more plants down the middle (x 96-112), the mat at the front
#
# The black front corners are floor (`open`), the side walls run on to the front.

LOUNGE_FLOOR = ("0073ff", "00b4ff", "73deff")
LOUNGE_WALL = ("ffd573", "ffffff", "9494a4", "cdbdc5", "b46218", "e69441", "e6ac5a")
LOUNGE_WALL_SIDE = (0, 0, 16, 32)


def _lounge(w, h, plants, machine, chairs, extra=()):
    facet, own_shell, piece = _helpers()
    W, H = w * 16, h * 16
    pieces = [piece("chair_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)], 6,
                    leave=LOUNGE_FLOOR)
              for (cx, cy) in chairs]
    pieces += [piece("plant_%d_%d" % (x0 // 16, z1 // 16), [(x0, z0, x0 + 16, z1)], 28,
                     leave=LOUNGE_FLOOR + LOUNGE_WALL, card=True)
               for (x0, z0, z1) in plants + list(extra)]
    mx, mz0, mz1, lid = machine
    pieces += [
        piece("machine", [(mx, mz0, mx + 16, mz1)], 22, back=32, top=lid,
              leave=LOUNGE_WALL + LOUNGE_FLOOR),
        piece("wall", [(0, 0, W, 32)], 24, fill=16, side=LOUNGE_WALL_SIDE),
        # the side walls pieces of their own, drawn nowhere: a piece answers for the floor of
        # every cell its rectangle spans, and the chequered floor is two tiles
        piece("side_w", [], 24, side=LOUNGE_WALL_SIDE, walls=[((0, H), (0, 32))]),
        piece("side_e", [], 24, side=LOUNGE_WALL_SIDE, walls=[((W, 32), (W, H))]),
    ]
    return own_shell(pieces)


def lounge1():
    plants = [(x, 16, 48) for x in (16, 32, 48, 64, 80, 112, 128, 144, 160, 176)]
    chairs = [(0, 3), (2, 3), (4, 3), (10, 3), (12, 3), (0, 4), (2, 4), (4, 4), (8, 4), (10, 4),
              (12, 4), (0, 5), (8, 5), (10, 5), (12, 5), (8, 6), (10, 6)]
    return _lounge(13, 8, plants, (96, 13, 40, None), chairs,
                   extra=[(96, 48, 80), (96, 80, 112)])


def lounge2():
    plants = [(x, 16, 48) for x in (0, 16, 32, 48, 80, 96, 112, 128)]
    chairs = [(0, 3), (8, 3)] + [(cx, cy) for cy in range(4, 8) for cx in (0, 2, 6, 8)]
    return _lounge(9, 10, plants, (64, 29, 55, (67, 35, 77, 36)), chairs)


# ── The Battle Frontier's Exchange Service Corner ─────────────────────────
#
# 15 x 11 cells. From the top of the drawing:
#
#     rows  0- 32  the back wall: its grey top, its face orange with three emblems, a red
#                  baseboard (rows 24-32)
#     rows  8- 40  a PC (x 224-240), its foot on the floor in front of the wall
#     rows 16- 64  the prize shelves behind the counter: a tall one along the wall (x 64-176,
#                  its top rows 16-22), lower ones in front of it either side (x 64-96,
#                  x 144-176, rows 38-64)
#     rows 24-112  the counter, a U round the clerks' floor: its arms x 48-64 and 176-192 from
#                  the wall, its bar across the front, its red top rows 92-102 and its grey front
#                  rows 102-112 - 10 rows
#     rows 48- 96  two benches down the west wall (x 0-16, rows 48-96 and 112-144), walkable
#     rows 144-176 the mat; the black front corners are floor (`open`)

EX_FLOOR = ("0073ff", "00b4ff", "73deff", "0052de", "0039b4")
EX_WALL = ("ffd573", "e69441", "e6ac5a", "b46218", "bd2910", "ff6231", "de4a20", "52526a")
EX_WALL_SIDE = (16, 0, 32, 32)


def exchange_service_corner():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("bench_n", [(0, 48, 16, 96)], 6, leave=EX_FLOOR),
        piece("bench_s", [(0, 112, 16, 144)], 6, leave=EX_FLOOR),
        piece("counter", [(48, 24, 64, 112), (176, 24, 192, 112), (48, 92, 192, 112)], 10),
        piece("shelf_w", [(64, 38, 96, 64)], 14, leave=EX_FLOOR),
        piece("shelf_e", [(144, 38, 176, 64)], 14, leave=EX_FLOOR),
        piece("shelf_back", [(64, 16, 176, 48)], 24, back=32, leave=EX_FLOOR + EX_WALL),
        piece("pc", [(224, 8, 240, 40)], 24, back=32, leave=EX_WALL + EX_FLOOR),
        piece("wall", [(0, 0, 240, 32)], 24, fill=16, side=EX_WALL_SIDE),
        # the side walls answer for the black front corners, which are floor
        piece("side_w", [], 24, side=EX_WALL_SIDE, walls=[((0, 176), (0, 32))], cells=[(1, 10)]),
        piece("side_e", [], 24, side=EX_WALL_SIDE, walls=[((240, 32), (240, 176))],
              cells=[(13, 10)]),
    ])


# ── The Battle Pike's lobby ───────────────────────────────────────────────
#
# 11 x 13 cells. From the top of the drawing:
#
#     rows  0- 64  the back wall, a red stage curtain to the floor, a pink cushion drawn on it
#     rows 40- 72  in front of it a television (x 8-40), a monitor (x 128-144) and a PC
#                  (x 160-176), their feet on the floor
#     below        fourteen copper lanterns on posts, each a cell of base (blocked) under a cell
#                  of lamp: cards, as a plant is; the mat at the front
#
# The floor's lantern cells and shaded cells are not its tile (0x2C2): pixels of theirs that
# are not the tile's are marks, laid flat by the pieces that cover them.

PIKE_FLOOR = ("a48b31", "cdac31", "7b6a31")
PIKE_CURTAIN = ("c50000", "7b3100", "ff0000", "523131", "9c8b31", "ff9462", "ffd531", "b45241")
PIKE_LANTERNS = [(3, 4), (7, 4), (4, 5), (6, 5), (1, 7), (9, 7), (0, 8), (3, 8), (7, 8), (10, 8),
                 (1, 10), (9, 10), (3, 12), (7, 12)]
PIKE_SIDE = (16, 0, 32, 44)       # plain curtain (lower, the lobby's TV and the cushions)


def pike_lobby():
    facet, own_shell, piece = _helpers()
    lanterns = [piece("lantern_%d_%d" % (cx, cy),
                      [(cx * 16, cy * 16 - 16, cx * 16 + 16, cy * 16 + 16)], 28,
                      leave=PIKE_FLOOR, card=True)
                for (cx, cy) in PIKE_LANTERNS]
    return own_shell(lanterns + [
        piece("tv", [(8, 46, 40, 72)], 16, back=64, leave=PIKE_CURTAIN + PIKE_FLOOR),
        piece("monitor", [(128, 42, 144, 72)], 20, back=64, leave=PIKE_CURTAIN + PIKE_FLOOR),
        piece("pc", [(160, 38, 176, 72)], 24, back=64, leave=PIKE_CURTAIN + PIKE_FLOOR),
        piece("wall", [(0, 0, 176, 64)], 48, fill=16, side=PIKE_SIDE),
        piece("side_w", [], 48, side=PIKE_SIDE, walls=[((0, 208), (0, 64))]),
        piece("side_e", [], 48, side=PIKE_SIDE, walls=[((176, 64), (176, 208))]),
    ])


# ── Scott's house at the Battle Frontier ──────────────────────────────────
#
# 6 x 8 cells, the lounges' kit: the back wall rows 0-32; an armchair (5, 3), walkable; a
# potted plant (x 80-96, rows 64-96); the mat at the front, black front corners.

def scotts_house():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("chair", [(80, 48, 96, 64)], 6, leave=LOUNGE_FLOOR),
        piece("plant", [(80, 64, 96, 96)], 28, leave=LOUNGE_FLOOR + LOUNGE_WALL, card=True),
        piece("wall", [(0, 0, 96, 32)], 24, fill=16, side=LOUNGE_WALL_SIDE),
        piece("side_w", [], 24, side=LOUNGE_WALL_SIDE, walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 24, side=LOUNGE_WALL_SIDE, walls=[((96, 32), (96, 128))]),
    ])


# ── The Battle Frontier's reception gate ──────────────────────────────────
#
# 9 x 14 cells, a hall the red carpet runs down (x 48-96). From the top of the drawing:
#
#     rows  0- 32  the back wall either side of the way out (x 48-96, the doorway drawn dark):
#                  the doorway stands no wall, the player walks into it
#     rows 40- 96  and 152-208: a reception desk either side of the carpet, a U opening to the
#                  room's side, its top pale yellow and its fronts grey (x 0-32, x 112-144);
#                  two lamp posts beside each (x 32-48, x 96-112), a cell of base under a cell
#                  of lamp: cards
#     rows 96-144  potted plants (x 16-48, rows 104-144; x 96-144, rows 88-128), armchairs
#                  (walkable) down the walls
#
# Its floor is three tiles and the carpet: pixels not the first tile's are marks.

GATE_FLOOR = ("bdcde6", "dee6ee")
GATE_WALL = ("ffd573", "9494a4", "ffffff", "e69441", "cdbdc5", "e6ac5a", "ff6231", "de4a20",
             "b46218", "bd2910")
GATE_WALL_SIDE = (0, 0, 16, 32)


def reception_gate():
    facet, own_shell, piece = _helpers()
    # each post its base cell and the cell above (a box reaching higher took the bottom of the
    # pot north of the posts at 2,10 onto the post's card)
    posts = [piece("post_%d_%d" % (cx, cy), [(cx * 16, cy * 16 - 16, cx * 16 + 16, cy * 16 + 16)],
                   24, leave=GATE_FLOOR, card=True)
             for (cx, cy) in ((2, 3), (2, 5), (6, 3), (6, 5), (2, 10), (2, 12), (6, 10), (6, 12))]
    chairs = [piece("chair_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)], 6,
                    leave=GATE_FLOOR)
              for (cx, cy) in ((0, 6), (0, 7), (0, 8), (8, 8))]
    return own_shell(posts + chairs + [
        piece("desk_nw", [(0, 40, 32, 96)], 10, leave=GATE_FLOOR),
        piece("desk_ne", [(112, 40, 144, 96)], 10, leave=GATE_FLOOR),
        piece("desk_sw", [(0, 152, 32, 208)], 10, leave=GATE_FLOOR),
        piece("desk_se", [(112, 152, 144, 208)], 10, leave=GATE_FLOOR),
        piece("plant_w", [(16, 104, 48, 144)], 28, leave=GATE_FLOOR, card=True),
        piece("plant_e", [(96, 88, 144, 128)], 28, leave=GATE_FLOOR, card=True),
        piece("wall_w", [(0, 0, 48, 32)], 24, fill=16, side=GATE_WALL_SIDE),
        piece("wall_e", [(96, 0, 144, 32)], 24, fill=16, side=GATE_WALL_SIDE),
        piece("side_w", [], 24, side=GATE_WALL_SIDE, walls=[((0, 224), (0, 32))]),
        piece("side_e", [], 24, side=GATE_WALL_SIDE, walls=[((144, 32), (144, 224))]),
    ])


# ── Ever Grande City's Hall of Fame ───────────────────────────────────────
#
# 15 x 17 cells, a dark hall in the black. From the top of the drawing:
#
#     rows  0- 32  the back wall, green screens on it, either side of an alcove (x 88-152)
#                  whose back wall, a cell deeper in the drawing, holds the big screen (rows
#                  26-40); a ledge along the wall's foot (rows 32-44) runs on down both sides of
#                  the room as its pale border - the floor's
#     rows 40- 58  a low step across the alcove's mouth (x 80-160)
#     rows 56- 88  the recording machine (x 98-142): its screen and its two striped posts its
#                  top, its keyboard and base its front

HOF_WALL_SIDE = (32, 8, 48, 32)


def hall_of_fame():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("machine", [(98, 56, 142, 88)], 14),
        piece("step", [(80, 40, 160, 58)], 8),
        piece("wall_alcove", [(88, 0, 152, 40)], 32, fill=16, side=HOF_WALL_SIDE),
        piece("wall_w", [(8, 0, 88, 32)], 24, fill=16, side=HOF_WALL_SIDE),
        piece("wall_e", [(152, 0, 232, 32)], 24, fill=16, side=HOF_WALL_SIDE),
    ])


# ── Lilycove Department Store's rooftop ───────────────────────────────────
#
# 18 x 12 cells, open to the sky, a railing round it (left drawn on the floor, as thin as it is).
# Two drinks machines (x 144-176, rows 0-30) against the north railing, the stairwell's hut
# (x 192-240, rows 16-64: its white roof, its front with the dark way down), two planters down
# the west railing (x 16-48), two benches (x 80-112, x 144-176, rows 80-96) and two chairs
# (x 256-272) - nothing walked on but the floor.

ROOF_FLOOR = ("8b8b8b", "b4b4a4", "b4de8b", "d5ded5", "9cd57b")


def rooftop():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("bench_w", [(80, 80, 112, 96)], 6, leave=ROOF_FLOOR),
        piece("bench_e", [(144, 80, 176, 96)], 6, leave=ROOF_FLOOR),
        piece("chair_n", [(256, 64, 272, 80)], 6, leave=ROOF_FLOOR),
        piece("chair_s", [(256, 80, 272, 96)], 6, leave=ROOF_FLOOR),
        piece("planter_n", [(16, 40, 48, 64)], 10, leave=ROOF_FLOOR),
        piece("planter_s", [(16, 72, 48, 96)], 10, leave=ROOF_FLOOR),
        piece("machines", [(144, 0, 176, 30)], 22),
        piece("hut", [(192, 16, 240, 64)], 24),
    ])


# ── Three small houses on the generic building tiles ──────────────────────
#
# The Safari Zone's rest house (10 x 9 cells: the black outside over the wall rows 0-32, the
# wall's face rows 32-64 with two pictures, the floor below) and the Sootopolis Mystery Events
# house (the wall's face rows 0-32): a table with its cloth, chairs either side of it (walkable,
# their backs drawn on their outer sides), cabinets and plants against the wall. Each table is
# as low as a table, each chair 6 rows. (The diving treasure hunters' house on Route 124 repeats
# the tiles of voxel_building_specs' house kit, whose table, chairs, plants and cabinets stand in
# it already.)

HOUSE_FLOOR = ("8b8b8b", "bdb431", "ded552", "9c9410")
HOUSE_WALL = ("d5d5b4", "d5d5ac", "b4b4a4", "629c8b", "ffffff")


def _chairs(piece, cells, floor):
    return [piece("chair_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)], 6,
                  leave=floor)
            for (cx, cy) in cells]


def safari_rest_house():
    facet, own_shell, piece = _helpers()
    side = (0, 32, 16, 64)
    return own_shell(_chairs(piece, [(2, 5), (2, 6), (5, 5), (5, 6)], HOUSE_FLOOR) + [
        piece("table", [(48, 80, 80, 106)], 10, leave=HOUSE_FLOOR),
        piece("wall", [(0, 32, 160, 64)], 32, fill=16, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, 144), (0, 64))]),
        piece("side_e", [], 32, side=side, walls=[((160, 64), (160, 144))]),
    ])


SOOT_FLOOR = ("8b8b8b", "b4b4a4", "73738b")


def sootopolis_house():
    facet, own_shell, piece = _helpers()
    side = (80, 0, 96, 32)      # a stretch of plain wall (the corner's is a round window)
    return own_shell(_chairs(piece, [(6, 4), (6, 5), (9, 4), (9, 5)], SOOT_FLOOR) + [
        piece("table", [(112, 64, 144, 96)], 10, leave=SOOT_FLOOR),
    ] + [piece("plant_%d" % cx, [(cx * 16, 16, cx * 16 + 16, 48)], 28,
               leave=SOOT_FLOOR + HOUSE_WALL, card=True) for cx in (2, 3, 4)] + [
        piece("stove", [(112, 14, 128, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        piece("cabinet", [(128, 14, 144, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        piece("sink", [(144, 14, 176, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        piece("wall", [(0, 0, 176, 32)], 24, fill=16, side=side),
        piece("side_w", [], 24, side=side, walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 24, side=side, walls=[((176, 32), (176, 128))]),
    ])


# ── The Battle Pike's corridor and its room of three paths ────────────────
#
# The lobby's kit on a pink floor: the curtain to the floor (rows 0-64), its pink cushions over
# the doorways the player is walked through (a cell of floor at row 3, in front of the curtain's
# foot); blue pedestals with a gold ball and copper lanterns, each a cell of base under a cell of
# top: cards. The red carpet and the mats are the floor's.

PIKE_PINK = ("e6737b", "ff9ca4", "b45241")


def _pike_room(w, h, posts):
    facet, own_shell, piece = _helpers()
    W, H = w * 16, h * 16
    cards = [piece("post_%d_%d" % (cx, cy), [(cx * 16, cy * 16 - 16, cx * 16 + 16, cy * 16 + 16)],
                   28, leave=PIKE_PINK, card=True)
             for (cx, cy) in posts]
    return own_shell(cards + [
        piece("wall", [(0, 0, W, 64)], 48, fill=16, side=PIKE_SIDE),
        piece("side_w", [], 48, side=PIKE_SIDE, walls=[((0, H), (0, 64))]),
        piece("side_e", [], 48, side=PIKE_SIDE, walls=[((W, 64), (W, H))]),
    ])


def pike_corridor():
    return _pike_room(14, 8, [(3, 4), (9, 4), (4, 6), (8, 6), (2, 7), (10, 7)])


def pike_three_paths():
    return _pike_room(13, 11, [(0, 4), (4, 4), (8, 4), (12, 4), (3, 10), (8, 10)])


# ── Devon Corporation's lobby (Rustboro City, 1F) ─────────────────────────
#
# 19 x 9 cells. The lobby (rows 80-144) runs the width of the map; its back wall (rows 48-80:
# an orange top band, a tan face with windows over a red baseboard) is broken by two alcoves
# that go back to the map's top, the black outside between them:
#
#     x 48-144   the reception: its back wall (rows 2-32), two glass display jars against it
#                (x 48-64, x 128-144), the counter across its mouth (rows 62-80: a white top,
#                its front 8 rows on legs)
#     x 208-256  the way up: a door in its back wall (rows 8-32), the warp at 14,1
#
# The alcoves' side walls are drawn as their tops, orange strips 4 pixels wide (x 44-48,
# 144-148, 204-208, 256-260): they stand as tall as the walls.

DEVON_FLOOR = ("cd837b", "deaca4", "bd6252")
DEVON_WALL = ("acb494", "9c9c9c", "835a31", "e69c62", "6a7b41", "839473", "414a6a", "c55a41")
DEVON_WALL_SIDE = (64, 4, 80, 32)


def devon_lobby():
    facet, own_shell, piece = _helpers()
    # a strip is a side wall's top: at 30 rows it runs from the alcove's back wall to the lobby's
    # (foot 78, behind the lobby wall's face; the cell below holds that foot)
    strips = [piece("edge_%d" % x0, [(x0, 0, x0 + 4, 48)], 30, solid=True, side=DEVON_WALL_SIDE,
                    foot=78, cells=[(x0 // 16, 4)])
              for x0 in (44, 144, 204, 256)]
    return own_shell([
        piece("counter", [(48, 62, 128, 80)], 8, leave=DEVON_FLOOR),
        piece("jar_w", [(48, 22, 64, 48)], 16, back=32, leave=DEVON_WALL + DEVON_FLOOR),
        piece("jar_e", [(128, 22, 144, 48)], 16, back=32, leave=DEVON_WALL + DEVON_FLOOR),
    ] + strips + [
        piece("wall_reception", [(48, 0, 144, 32)], 30, fill=16, side=DEVON_WALL_SIDE),
        piece("wall_door", [(208, 0, 256, 32)], 30, fill=16, side=DEVON_WALL_SIDE),
        piece("wall_w", [(0, 48, 44, 80)], 28, fill=16, side=DEVON_WALL_SIDE),
        piece("wall_mid", [(148, 48, 204, 80)], 28, fill=16, side=DEVON_WALL_SIDE),
        piece("wall_e", [(260, 48, 304, 80)], 28, fill=16, side=DEVON_WALL_SIDE),
        piece("side_w", [], 28, side=DEVON_WALL_SIDE, walls=[((0, 144), (0, 80))]),
        piece("side_e", [], 28, side=DEVON_WALL_SIDE, walls=[((304, 80), (304, 144))]),
    ])


# ── Mauville City's bike shop ─────────────────────────────────────────────
#
# 12 x 9 cells. The shop (x 0-96): its back wall (rows 0-32, bike parts hung on it), a shelf of
# parts against it (x 0-32), the counter (x 32-96: its orange top rows 38-58, its blue front
# rows 58-70) with the till on its west end; down the west side a rack of six bikes (x 0-24,
# rows 40-138), each a card standing where it is parked, and the rack's white frame behind them.
# A partition (x 96-104, drawn as its top) parts it from the owner's room behind (x 104-192:
# its wall rows 0-24 with two windows, two beds against it), whose south wall stands either side
# of its doorway (x 96-128, x 160-192, rows 60-88); three more bikes down the east wall.

BIKE_FLOOR = ("83948b", "acbdac", "cdcdcd", "d5ded5")
BIKE_WALL = ("acac8b", "eeeede", "cdcdb4", "dedebd", "947b6a", "000000")
BIKE_WALL_SIDE = (48, 4, 64, 32)


def bike_shop():
    facet, own_shell, piece = _helpers()
    bikes = [piece("bike_w%d" % i, [(0, 42 + 16 * i, 24, 58 + 16 * i)], 16, leave=BIKE_FLOOR,
                   card=True) for i in range(6)]
    bikes += [piece("bike_e%d" % i, [(160, 80 + 16 * i, 192, 96 + 16 * i)], 16, leave=BIKE_FLOOR,
                    card=True) for i in range(3)]
    return own_shell(bikes + [
        piece("rack", [(16, 44, 32, 142)], 4, leave=BIKE_FLOOR),
        piece("counter", [(32, 38, 96, 70)], 12, leave=BIKE_FLOOR),
        piece("till", [(32, 22, 48, 40)], 12, base=12, leave=BIKE_WALL + BIKE_FLOOR),
        piece("shelf", [(0, 8, 32, 40)], 24, back=24, leave=BIKE_WALL + BIKE_FLOOR),
        # the beds from their frames' top (rows 19-39): the windows above them are the wall's
        piece("bed_w", [(112, 19, 144, 40)], 8, leave=BIKE_WALL + BIKE_FLOOR),
        piece("bed_e", [(160, 19, 192, 40)], 8, leave=BIKE_WALL + BIKE_FLOOR),
        # the partition's top: it runs from the back wall to the room's south wall (foot 84)
        piece("edge_partition", [(96, 8, 104, 60)], 24, solid=True, side=BIKE_WALL_SIDE,
              foot=84, cells=[(6, 5)]),
        piece("wall_room_w", [(96, 60, 128, 88)], 24, fill=16, side=BIKE_WALL_SIDE),
        piece("wall_room_e", [(160, 60, 192, 88)], 24, fill=16, side=BIKE_WALL_SIDE),
        piece("wall_room", [(104, 0, 192, 24)], 18, fill=16, side=BIKE_WALL_SIDE),
        piece("wall", [(0, 0, 96, 32)], 24, fill=16, side=BIKE_WALL_SIDE),
        piece("side_e", [], 18, side=BIKE_WALL_SIDE, walls=[((192, 24), (192, 64))]),
    ])


# ── The Battle Frontier's Ranking Hall ────────────────────────────────────
#
# 53 x 15 cells: a hall (x 304-544) with a room either side (x 48-224, x 624-800) joined by
# corridors (rows 144-176), every wall a brown face over the black outside, at five depths.
# The walls are read off the cells as the room tool reads them (battle_room_auto: a run of
# blocked cells from the black to the floor is a wall's face, standing its drawn height); the
# furniture is cut by hand: nine record kiosks (grey cabinets, a blue screen on their front),
# the hall's display case (x 400-448, rows 128-170), and eleven green seats, walkable.

RANK_FLOOR = ("bdc5cd", "d5d5de", "e6eeee", "f6ffff")
RANK_KIOSKS = [(26, 4), (8, 6), (44, 6), (23, 7), (29, 7), (6, 8), (10, 8), (42, 8), (46, 8)]
RANK_SEATS = [(3, 7), (3, 9), (5, 12), (6, 12), (10, 12), (11, 12), (21, 4), (31, 4), (42, 12),
              (44, 12), (46, 12)]


def ranking_hall():
    facet, own_shell, piece = _helpers()
    import battle_room_auto
    walls = [pc for pc in battle_room_auto.analyse("LAYOUT_BATTLE_FRONTIER_RANKING_HALL")[1]
             if pc["name"].startswith(("wall_", "side_"))]
    kiosks = [piece("kiosk_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 32)], 20,
                    leave=RANK_FLOOR)
              for (cx, cy) in RANK_KIOSKS]
    seats = [piece("seat_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 32)], 12,
                   leave=RANK_FLOOR)
             for (cx, cy) in RANK_SEATS]
    return own_shell(seats + kiosks + [
        piece("display", [(400, 128, 448, 170)], 12, leave=RANK_FLOOR),
    ] + walls)



# ── The Battle Factory's lobby ────────────────────────────────────────────
#
# 19 x 12 cells. From the top of the drawing:
#
#     rows  0- 72  the back wall, drawn to the map's top: white panels with vents over a dark
#                  band, a red baseboard (rows 65-72); four pillars standing out of it (x 32-48,
#                  96-112, 192-208, 256-272); two doorways (x 48-96, x 208-256: jambs either side
#                  of a dark opening) whose middle cell (4,4 and 14,4) the attendants walk into
#                  with the player: the doorway stands no wall; between the middle pillars a
#                  yellow-striped shutter (x 112-192, rows 40-60) over cells no one walks
#     rows 66- 90  a notice board on legs (x 136-168) before the shutter: left as drawn (its
#                  legs stand in the row the player reads it from)
#     rows 72-128  four desks, each an L: a long orange-framed top from the wall (x 32-48,
#                  112-128, 176-192, 256-272) and a bar across its front (x 32-64, 80-128,
#                  176-224, 240-272; its top rows 108-120, its front rows 120-128 - 8 rows); a
#                  monitor on the outer desks' long tops (x 32-48, 176-192, rows 100-120), flat
#                  on them
#     plants     five potted plants (x 16-32 and x 272-304, rows 66-96; x 128-144 and x 160-176,
#                  rows 99-127): cards; a PC (x 0-16, rows 72-104)
#     rows 160-192 two bins (x 240-256, x 272-288); the red runner down the east edge (x 288-304,
#                  walkable) and the mats are the floor's
#
# Its floor is two tiles in turn (0x201, 0x202): the second's pixels that are not the first's are
# marks, laid flat by the pieces whose cells hold them.

FAC_FLOOR = ("ffffff", "d5d5c5", "acac8b")
FAC_WALL = ("ffffff", "d5d5c5", "52526a", "9494a4", "ff6231", "bd2910", "de4a20", "ac5a31",
            "cdbdc5", "62627b")
FAC_WALL_SIDE = (0, 0, 16, 72)      # a panel of the wall west of the first pillar, top to foot


def factory_lobby():
    facet, own_shell, piece = _helpers()
    desks = [piece("desk_%d" % x0, [(arm, 72, arm + 16, 128), (x0, 108, x1, 128)], 8,
                   leave=FAC_FLOOR)
             for (arm, x0, x1) in ((32, 32, 64), (112, 80, 128), (176, 176, 224), (256, 240, 272))]
    plants = [piece("plant_%d_%d" % (x0 // 16, z1 // 16), [(x0, z0, x0 + 16, z1)], 28,
                    leave=FAC_FLOOR + FAC_WALL, card=True)
              for (x0, z0, z1) in ((16, 64, 96), (272, 64, 96), (288, 64, 96), (128, 96, 128),
                                   (160, 96, 128))]
    bins = [piece("bin_%d" % x0, [(x0, 164, x0 + 16, 192)], 16, leave=FAC_FLOOR)
            for x0 in (240, 272)]
    return own_shell(desks + plants + bins + [
        piece("pc", [(0, 72, 16, 104)], 24, back=72, leave=FAC_WALL + FAC_FLOOR),
        # the doorways' cells (x 64-80, x 224-240) cut out of the walls under their lintels
        # (rows 0-50), each wall standing on its foot (row 72)
        piece("wall_w", [(0, 0, 64, 72), (64, 0, 80, 50)], 72, fill=16, foot=72,
              side=FAC_WALL_SIDE),
        piece("wall_mid", [(80, 0, 224, 72), (224, 0, 240, 50)], 72, fill=16, foot=72,
              side=FAC_WALL_SIDE),
        piece("wall_e", [(240, 0, 304, 72)], 72, fill=16, side=FAC_WALL_SIDE),
        piece("side_w", [], 72, side=FAC_WALL_SIDE, walls=[((0, 192), (0, 72))]),
        piece("side_e", [], 72, side=FAC_WALL_SIDE, walls=[((304, 72), (304, 192))]),
    ])



# ── The Battle Palace's lobby ─────────────────────────────────────────────
#
# 25 x 12 cells. From the top of the drawing:
#
#     rows  0- 80  the back wall, drawn to the map's top: a gold cornice, red panels under gold
#                  arches between gold pillars, a dark red and gold baseboard (rows 64-80); the
#                  door to the Palace (x 72-104, its cell 5,4 walked into: painted on the wall,
#                  as the Pike's doorways), a second door's frame (x 296-328) and an arch round a
#                  PC (x 214-248) - the PC (x 224-240, rows 56-88) stands before the wall
#     rows 64- 90  a notice board on legs (x 152-184): left as drawn, its legs in the row the
#                  player reads it from
#     rows 80-120  four white tables with gold legs either side of the attendants, each an L of
#                  three panels (x 32-80, 96-144, 256-304, 320-368; the deep panel two cells, rows
#                  80-112), a grey apron and the legs their front, rows 112-120 - 8 rows
#     rows 108-136 a glass sign on a stand before each outer table (x 32-48, x 256-272): cards
#     below       the pink floor, the mats (the floor's)

PAL_FLOOR = ("bd8ba4", "a46283", "944162")
PAL_WALL = ("cd4a20", "cdb429", "ffde83", "e6c552", "7b2910", "a43918")
PAL_WALL_SIDE = (32, 0, 48, 80)     # a red panel and its baseboard, cornice to foot


def palace_lobby():
    facet, own_shell, piece = _helpers()
    signs = [piece("sign_%d" % x0, [(x0, 108, x0 + 16, 136)], 28, leave=PAL_FLOOR, card=True)
             for x0 in (32, 256)]
    tables = [piece("table_%d" % x0, [(deep, 80, deep + 24, 120), (x0, 96, x0 + 48, 120)], 8,
                    leave=PAL_FLOOR)
              for (deep, x0) in ((32, 32), (120, 96), (256, 256), (344, 320))]
    return own_shell(signs + tables + [
        piece("pc", [(224, 56, 240, 88)], 24, back=80, leave=PAL_WALL + PAL_FLOOR),
        piece("wall_w", [(0, 0, 128, 80)], 80, fill=16, side=PAL_WALL_SIDE),
        piece("wall_mid", [(128, 0, 272, 80)], 80, fill=16, side=PAL_WALL_SIDE),
        piece("wall_e", [(272, 0, 400, 80)], 80, fill=16, side=PAL_WALL_SIDE),
        piece("side_w", [], 80, side=PAL_WALL_SIDE, walls=[((0, 192), (0, 80))]),
        piece("side_e", [], 80, side=PAL_WALL_SIDE, walls=[((400, 80), (400, 192))]),
    ])


# ── Team Magma's hideout: its machines ────────────────────────────────────
#
# KNOWN_ISSUES E9. The hideout is a cave: the rules stand its rock (its walls are no room's, so
# battle_room_specs.AUTO_ROOMS leaves it out) and its machines lay drawn on the floor of the alcoves
# they stand in, where a 3D battle's stage stood a foe on one. Two machines of the Lavaridge tileset,
# each 2 x 3 cells against the rock, drawn alike in every room of the hideout:
#
#     the server   a white top with a grille (rows 0-23), a khaki front with vents and the white
#                  band over them (rows 24-47), its corners cut
#     the console  a disc on its top (rows 0-23), a grey front with a screen and keys (rows 24-47)
#
# Each stands its drawn front, 24 rows, and is as deep as its drawn top, 24 rows: the box the
# drawing shows, a disc or a grille on its top once (run back to the rock's foot, where its
# collision starts, the top's rows laid over again showed a second disc behind the first). The
# floor behind it, which the box hides at 45 degrees, is bare (`under`): the marks the generator
# would carry under a piece from either side of it are the rock's here, not a rug's. The second room
# of 2F has both machines, twice (x 144-208 and 432-496, rows 32-80); a room's own pieces do not
# stand again in it, so all four are cut there. The rest of the hideout's rooms stand them where
# their pixels are drawn again (reuse_in): the colours round them, the rock's and the floor's, are
# never a machine's.

MAGMA_GROUND = ("d59c8b", "bd8373", "9c6252", "833120", "621000")
MAGMA_ROOMS = frozenset(("LAYOUT_MAGMA_HIDEOUT_1F", "LAYOUT_MAGMA_HIDEOUT_2F_1R",
                         "LAYOUT_MAGMA_HIDEOUT_3F_1R", "LAYOUT_MAGMA_HIDEOUT_3F_2R",
                         "LAYOUT_MAGMA_HIDEOUT_3F_3R", "LAYOUT_MAGMA_HIDEOUT_4F",
                         "LAYOUT_MAGMA_HIDEOUT_2F_3R"))


def magma_hideout_machines():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("%s_%s" % (kind, side), [(x0, 32, x0 + 32, 80)], 24, solid=True, leave=MAGMA_GROUND,
              under=False)
        for side, x in (("w", 144), ("e", 432))
        for kind, x0 in (("server", x), ("console", x + 32))
    ])


# The hideout's other machines (E9's leftovers), each drawn alike wherever it stands:
#
#     the long machine of 3F's second room (x 160-272, rows 96-144: 7 x 3 cells), the server's
#                  kin: a white top with its emblem (rows 0-30), a khaki front with a screen, keys
#                  and slats (rows 31-47), its corners cut - it stands its drawn front, 17 rows, as
#                  deep as its drawn top
#     the dome and the drill rigs (2 x 2 cells each): a base on two tracks - the side panels its
#                  top (rows 5-22), a band and the tracks its front (rows 23-31, 9 rows), the floor
#                  between the tracks - and on the base, at its front, a drum (x 9-22): the dome's
#                  ribbed side or the drill's bit its front (rows 10-22, 13 rows), the dome's white
#                  disc or the drill's motor its top (rows 0-9). The drum is cut first, so the base
#                  keeps the rest.
#
# The dome lay flat in 4F (three) and stood as a block of the cave's rock in 3F's third room (one);
# the drill stood as rock in 2F's third room (four); in 3F's second room (six domes, seven drills)
# and 4F (four drills) the rules stood both as boxes two tiles tall with the drawing run down
# them. Cut in 3F 3R (the dome) and 2F 3R (the four drills: a room's own pieces do not stand again
# in it), they stand in every room their pixels are drawn in, all alike.

MAGMA_ALL = MAGMA_ROOMS | {"LAYOUT_MAGMA_HIDEOUT_2F_2R"}


def magma_rig(kind, x0, y0):
    """A dome or drill rig at (x0, y0) of the room's drawing: its drum, then its base."""
    facet, own_shell, piece = _helpers()
    at = "%s_%d_%d" % (kind, x0 // 16, y0 // 16)
    return [
        piece(at + "_drum", [(x0 + 9, y0, x0 + 23, y0 + 23)], 13, base=9, solid=True,
              leave=MAGMA_GROUND, under=False),
        piece(at + "_base", [(x0, y0, x0 + 32, y0 + 32)], 9, solid=True, leave=MAGMA_GROUND,
              under=False),
    ]


def magma_long_machine():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("long_machine", [(160, 96, 272, 144)], 17, solid=True, leave=MAGMA_GROUND,
              under=False),
    ])


# 4F's small console (13,18-19), the last of the hideout's machines that lay flat: a panel with a
# screen and keys (rows 16-31 of its two cells), its khaki top (rows 9-15) and a white ball on a
# collar (rows 0-8, x 3-12), the whole of it on the cells' upper layer - over the lava's edge (the
# upper cell's lower layer is lava, a rim at the floor's level: buildings.c SinkLava sinks only
# the bare lava round it) and the floor (the lower cell's). It is no room's piece: no room is
# modelled on 4F, and a modelled room would stand the machines reused there on its plain floor
# instead of each cell's own paint (find_placements). Like the sea's rocks it stands where it is
# drawn, on each cell's own lower layer (`at` and `quads`: the model stands for both cells'
# upper layers), so the lava behind it is the lava the cartridge draws there. One box read off
# its drawing as the other consoles: its front 16 rows on its foot, as deep as its drawn top, the
# ball laid on its top as their discs (the ball's columns run deeper by its rows). Its sides and
# back take its drawing, projected, not a tile of its front (not `solid`): the lava the ball's
# collar leaves showing under its round edge is a gap through it at 45 degrees, as a railing's
# gaps are. It waited on E13: standing, it moved the 3D battles in front of it off the floor and
# onto the lava, until the stage search took lava for no ground (battle_stage.c noFeet); now they
# stand on the floor beside it.

MAGMA_CONSOLE_AT = ("LAYOUT_MAGMA_HIDEOUT_4F", 13, 18)


def magma_small_console():
    import voxel_building as vb
    lid, x, y = MAGMA_CONSOLE_AT
    layout = vb.LayoutArt(lid)
    art = vb.Image.new("RGBA", (16, 32), (0, 0, 0, 0))
    px = art.load()
    for j in range(2):
        for q, sub in enumerate(layout.subtiles(layout.metatile(x, y + j), 1)):
            for k, (rgb, index) in enumerate(sub):
                if index:
                    px[(q & 1) * 8 + k % 8, j * 16 + (q >> 1) * 8 + k // 8] = rgb + (255,)

    def parts():
        import gen_voxel_buildings as gen     # (it imports the specs: not at the top)
        return [vb.Relief("console", art, 16, gen.pick_side(art, 16), foot=32)]

    return {"name": "magma_hideout_4f_small_console", "layout": lid, "rect": (x, y, 1, 2),
            "ground": [0x3A2], "art": art, "parts": parts,
            "exact": [(0, 0, 16, 32)], "at": [MAGMA_CONSOLE_AT],
            "quads": {(0, 0): 15, (0, 1): 15}}


def furnished_room_specs():
    """The specs, made when voxel_building_specs asks for them (its SPECS end with them)."""
    return [
        {
            "name": "mauville_game_corner",
            "interior": {"layout": "LAYOUT_MAUVILLE_CITY_GAME_CORNER", "ground": [0x202],
                         "shade": [0x203, 0x204, 0x205, 0x20C, 0x20D, 0x214, 0x215],
                         "reuse_in": frozenset(),
                         "pieces": mauville_game_corner()},
        },
        {
            "name": "mossdeep_game_corner_1f",
            "interior": {"layout": "LAYOUT_MOSSDEEP_CITY_GAME_CORNER_1F", "ground": [0x20A],
                         "shade": [], "reuse_in": frozenset(),
                         "pieces": mossdeep_game_corner_1f()},
        },
        {
            "name": "frontier_lounge1",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_LOUNGE1", "ground": [0x3C8],
                         "open": [(0, 112, 16, 128), (192, 112, 208, 128)],
                         "reuse_in": frozenset(), "pieces": lounge1()},
        },
        {
            "name": "frontier_lounge2",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_LOUNGE2", "ground": [0x3C8],
                         "open": [(0, 144, 16, 160), (128, 144, 144, 160)],
                         "reuse_in": frozenset(), "pieces": lounge2()},
        },
        {
            "name": "frontier_exchange",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_EXCHANGE_SERVICE_CORNER",
                         "ground": [0x3B8],
                         "open": [(0, 144, 16, 176), (16, 160, 32, 176), (224, 144, 240, 176),
                                  (208, 160, 224, 176)],
                         "reuse_in": frozenset(), "pieces": exchange_service_corner()},
        },
        {
            "name": "frontier_pike_lobby",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_LOBBY", "ground": [0x2C2],
                         "reuse_in": frozenset(), "pieces": pike_lobby()},
        },
        {
            "name": "frontier_scotts_house",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_SCOTTS_HOUSE", "ground": [0x3C8],
                         "open": [(0, 112, 16, 128), (80, 112, 96, 128)],
                         "reuse_in": frozenset(), "pieces": scotts_house()},
        },
        {
            "name": "frontier_reception_gate",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_RECEPTION_GATE", "ground": [0x05E],
                         "reuse_in": frozenset(), "pieces": reception_gate()},
        },
        {
            "name": "hall_of_fame",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_HALL_OF_FAME", "ground": [0x2F6],
                         "reuse_in": frozenset(), "pieces": hall_of_fame()},
        },
        {
            "name": "store_rooftop",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_ROOFTOP",
                         "ground": [0x24A], "reuse_in": frozenset(), "pieces": rooftop()},
        },
        {
            "name": "safari_rest_house",
            "interior": {"layout": "LAYOUT_SAFARI_ZONE_REST_HOUSE", "ground": [0x223],
                         "reuse_in": frozenset(), "pieces": safari_rest_house()},
        },
        {
            "name": "sootopolis_events_house",
            "interior": {"layout": "LAYOUT_SOOTOPOLIS_CITY_MYSTERY_EVENTS_HOUSE_1F",
                         "ground": [0x235], "reuse_in": frozenset(),
                         "pieces": sootopolis_house()},
        },
        {
            "name": "frontier_pike_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_CORRIDOR",
                         "ground": [0x2C1], "reuse_in": frozenset(), "pieces": pike_corridor()},
        },
        {
            "name": "frontier_pike_three_paths",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_THREE_PATH_ROOM",
                         "ground": [0x2C1], "reuse_in": frozenset(),
                         "pieces": pike_three_paths()},
        },
        {
            "name": "devon_lobby",
            "interior": {"layout": "LAYOUT_RUSTBORO_CITY_DEVON_CORP_1F", "ground": [0x380],
                         "reuse_in": frozenset(), "pieces": devon_lobby()},
        },
        {
            "name": "bike_shop",
            "interior": {"layout": "LAYOUT_MAUVILLE_CITY_BIKE_SHOP", "ground": [0x23F],
                         "reuse_in": frozenset(), "pieces": bike_shop()},
        },
        {
            "name": "frontier_factory_lobby",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_LOBBY",
                         "ground": [0x201], "shade": [0x203, 0x204],
                         "reuse_in": frozenset(), "pieces": factory_lobby()},
        },
        {
            "name": "frontier_palace_lobby",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_PALACE_LOBBY",
                         "ground": [0x201], "shade": [0x203, 0x207, 0x20f],
                         "reuse_in": frozenset(), "pieces": palace_lobby()},
        },
        {
            "name": "frontier_ranking_hall",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_RANKING_HALL", "ground": [0x20A],
                         "reuse_in": frozenset(), "pieces": ranking_hall()},
        },
        {
            "name": "magma_hideout_2f_2r",
            "interior": {"layout": "LAYOUT_MAGMA_HIDEOUT_2F_2R", "ground": [0x3A2],
                         "reuse_in": MAGMA_ROOMS, "pieces": magma_hideout_machines()},
        },
        {
            # (these two before the second room of 3F: their rigs stand there as everywhere)
            "name": "magma_hideout_3f_3r",
            "interior": {"layout": "LAYOUT_MAGMA_HIDEOUT_3F_3R", "ground": [0x3A2],
                         "reuse_in": MAGMA_ALL, "pieces": magma_rig("dome", 288, 320)},
        },
        {
            "name": "magma_hideout_2f_3r",
            "interior": {"layout": "LAYOUT_MAGMA_HIDEOUT_2F_3R", "ground": [0x308],
                         "reuse_in": MAGMA_ALL,
                         "pieces": [pc for (x, y) in ((11, 11), (22, 11), (44, 10), (55, 10))
                                    for pc in magma_rig("drill", x * 16, y * 16)]},
        },
        {
            "name": "magma_hideout_3f_2r",
            "interior": {"layout": "LAYOUT_MAGMA_HIDEOUT_3F_2R", "ground": [0x308],
                         "reuse_in": MAGMA_ALL, "pieces": magma_long_machine()},
        },
        magma_small_console(),
    ]
