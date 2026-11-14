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
# the floor tiles' dark borders round a plant: a plant's card leaves them to the floor (the object
# audit of 2026-11-14: kept, each pot stood before a dark-blue square)
LOUNGE_TILE_EDGES = ("0052de", "0039b4")
LOUNGE_WALL = ("ffd573", "ffffff", "9494a4", "cdbdc5", "b46218", "e69441", "e6ac5a")
LOUNGE_WALL_SIDE = (0, 0, 16, 32)


def _lounge(w, h, plants, machine, chairs, extra=()):
    facet, own_shell, piece = _helpers()
    W, H = w * 16, h * 16
    pieces = [piece("chair_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16)], 6,
                    leave=LOUNGE_FLOOR)
              for (cx, cy) in chairs]
    pieces += [piece("plant_%d_%d" % (x0 // 16, z1 // 16), [(x0, z0, x0 + 16, z1)], 28,
                     leave=LOUNGE_FLOOR + LOUNGE_TILE_EDGES + LOUNGE_WALL, card=True)
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
        piece("plant", [(80, 64, 96, 96)], 28, leave=LOUNGE_FLOOR + LOUNGE_TILE_EDGES + LOUNGE_WALL,
              card=True),
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
# 18 x 12 cells, open to the sky, a railing round it. Until the object audit of 2026-11-14 the
# railing lay drawn on the floor; now it stands (the owner: railings with real thickness and
# joined corners): the north and south rails, drawn from the front - an orange handrail over grey
# balusters - stand their drawn face (rows 8-22, 103-118), and the rails down the sides, drawn as
# their handrail's line from above, stand as boxes as tall (14 rows) and as thick as the line (6-7
# pixels): the west rail (x 9-16) from the north rail to the south, the short rail beside the
# stairwell's hut (x 245-251), the stretch east of it (x 250-278, drawn from the front, rows 42-56)
# and the east rail (x 272-278) down to the south rail. Their sides, which the drawing never shows,
# carry a stretch of the north rail's balusters.
# Two drinks machines (x 144-176, rows 0-30) against the north railing, the stairwell's hut
# (x 192-240, rows 16-64: its white roof, its front with the dark way down), two planters down
# the west railing (x 16-48), two benches (x 80-112, x 144-176, rows 80-96) and two chairs
# (x 256-272) - nothing walked on but the floor.

ROOF_FLOOR = ("8b8b8b", "b4b4a4", "b4de8b", "d5ded5", "9cd57b")
ROOF_RAIL_SIDE = (32, 8, 48, 22)    # the north rail: a handrail over two balusters


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
        # the rails: down the sides first (their corners theirs), then along the rows
        piece("rail_w", [(9, 8, 16, 118)], 14, side=ROOF_RAIL_SIDE),
        piece("rail_hut", [(245, 8, 251, 42)], 14, side=ROOF_RAIL_SIDE),
        piece("rail_e", [(272, 42, 279, 118)], 14, side=ROOF_RAIL_SIDE),
        piece("rail_n", [(16, 8, 245, 22)], 14, side=ROOF_RAIL_SIDE),
        piece("rail_ne", [(251, 42, 272, 56)], 14, side=ROOF_RAIL_SIDE),
        piece("rail_s", [(16, 103, 272, 118)], 15, side=ROOF_RAIL_SIDE),
    ])


# ── Lilycove Department Store's floors ────────────────────────────────────
#
# 18 x 8 cells each, the Mart's Shop tiles, no model until now: the back wall, the lift, the
# counters and the coolers lay drawn on the floor (the 3D object audit of 2026-11-14). From the top
# of the drawing, on every floor:
#
#     rows  0- 32  the back wall: a grey top band (rows 0-8) and its face (rows 8-32, pale panels
#                  over a salmon baseboard), its whole drawn height standing; on it the lift (x
#                  24-56, its doors walked into: painted on the wall, as the Palace's door) and
#                  windows, a clock, a glass case or an emblem; the floor's sign under the lift
#                  (rows 30-42) is the floor's
#     doorways     the stairs and the door the player walks into (cells 13,1 and 16,1): cut out
#                  of the wall under their lintels (rows 0-13), their drawing seen through them
#     sides        the side walls the drawing never shows, dressed with a plain panel
#
# 1F, 3F and 5F: a counter before the wall round the clerks (the floor between them and the wall
# theirs): its arms from the wall's foot (rows 32-54, their fronts rows 54-64), the bar or glass
# case between them (its top rows 44-54, its front 54-64) - 10 rows; a till on the west arm and a
# till on the east (rows 26-44), each 8 rows on the counter. 1F: two produce stands down the west
# wall (x 0-32: their fronts 10 rows, the vegetables their top) and three plants (its two stools
# are the Center's, tile for tile, and stand as they are). 5F: an L counter at the east (x 224-288: its arm rows 76-128, its glass case rows 76-96).
# 2F: a U counter round the clerks (x 96-192): its arms' tops rows 77-118 and fronts 118-128, the
# glass case between them its top rows 78-86 and its front 86-96 - 10 rows, the clerks seen over
# it; a till on the west arm; two potted plants (x 272-288): cards. The coolers each side (x 32-64,
# x 224-256) are the Mart's display stands, tile for tile, and stand here as they are
# (gen_voxel_buildings.reuse_pieces).

STORE_GREEN = ("a4eebd", "9cd57b", "ffffff", "b4b4a4", "cdffd5", "d5ded5", "b4de8b", "8b8b8b")
STORE_WALL_SIDE = (64, 0, 80, 32)      # a plain panel of the back wall, band to baseboard


# The doorways' openings as drawn (inside their frames' posts), under their lintels (rows 0-13):
# cell 13's x 211-222 and cell 16's x 262-274
STORE_OPENINGS = {13: (211, 222), 16: (262, 274)}


def _store_walls(doorways, east_from=32):
    """The back wall, standing its drawn face (32 rows) on its foot, row 32, the doorways the player
    walks into (cells `doorways`) cut out of it under their lintels - the lintel in the wall, so its
    top runs on unbroken (set back to the doorway's top, as battle_room_auto stands one, the wall's
    top had a black notch over each doorway); and the side walls, from the back wall's foot to the
    front."""
    facet, own_shell, piece = _helpers()
    walls, x = [], 0
    for n, cell in enumerate(sorted(doorways)):
        a, b = STORE_OPENINGS[cell]
        walls.append(piece("wall_%d" % n, [(x, 0, a, 32), (a, 0, b, 13)], 32, fill=16, foot=32,
                           side=STORE_WALL_SIDE))
        x = b
    walls.append(piece("wall_%d" % len(doorways), [(x, 0, 288, 32)], 32, fill=16, side=STORE_WALL_SIDE))
    return walls + [
        piece("side_w", [], 32, side=STORE_WALL_SIDE, walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=STORE_WALL_SIDE, walls=[((288, east_from), (288, 128))]),
    ]


def U_SHAPE(west, east):
    return [(west, 77, west + 16, 128), (east, 77, east + 16, 128), (west + 16, 77, east, 96)]


def WALL_SHAPE(west, east):
    return [(west, 32, west + 16, 64), (east, 32, east + 16, 64), (west + 16, 44, east, 64)]


def _u_counter(west, east, floor):
    """A U counter in the room (2F, 4F): its arms (x west and east, 16 wide) rows 77-128, the glass
    case between them rows 77-96, 10 rows of front; the till on the west arm, before the counter
    it stands on (a piece claims what no piece before it has)."""
    facet, own_shell, piece = _helpers()
    return [
        piece("till", [(west + 2, 102, west + 16, 118)], 8, base=10, leave=floor),
        # the whole counter its own: its top is drawn in the floor's own pale green and white,
        # and the floor's colours ran into the west arm past the till
        piece("counter", U_SHAPE(west, east), 10, claim=U_SHAPE(west, east)),
    ]


def _wall_counter(west, east, floor):
    """A counter before the back wall (1F, 3F, 5F): its arms from the wall's foot, rows 32-64, the
    bar or glass case between them rows 44-64, 10 rows of front; a till on each arm."""
    facet, own_shell, piece = _helpers()
    return [
        piece("till_w", [(west + 2, 38, west + 16, 54)], 8, base=10, leave=floor),
        piece("till_e", [(east + 2, 26, east + 16, 44)], 8, base=10, leave=floor),
        # the arms from the wall's foot: their tops' ends drawn over the wall's baseboard (rows
        # 24-32) are the wall's, painted on it (standing there, the arm's top met the wall's
        # face and the room did not give back its drawing)
        piece("counter", WALL_SHAPE(west, east), 10, claim=WALL_SHAPE(west, east)),
    ]


STORE_ORANGE = ("f6c539", "e69c39", "f6e673", "cd8320", "ac5a20")
STORE_CREAM = ("ffffcd", "dedea4", "ffc5ac", "cdffcd", "acdeb4", "e6a483", "bdbd83")


def store_1f():
    facet, own_shell, piece = _helpers()
    # the stands' fronts are drawn in the floor's own oranges: each claims its whole shape
    stands = [piece("stand_%d" % z0, [(0, z0, 32, z0 + 24)], 10, claim=[(0, z0, 32, z0 + 24)])
              for z0 in (72, 104)]
    plants = [piece("plant_%d" % x0, [(x0, 96, x0 + 16, 128)], 28, leave=STORE_ORANGE, card=True)
              for x0 in (224, 256, 272)]
    return own_shell(stands + plants + _wall_counter(96, 176, STORE_ORANGE)
                     + _store_walls((16,)))


def store_2f():
    facet, own_shell, piece = _helpers()
    plants = [piece("plant_%d" % z0, [(272, z0, 288, z0 + 32)], 28, leave=STORE_GREEN, card=True)
              for z0 in (48, 96)]
    return own_shell(plants + _u_counter(96, 176, STORE_GREEN)
                     + _store_walls((13, 16)))


def store_3f():
    return own_shell_of(_wall_counter(96, 176, STORE_GREEN)
                        + _store_walls((13, 16)))


def _short_coolers(xs, floor):
    """The shorter coolers of 4F and 5F (two cells deep, rows 79-112): the Mart's display stands
    without their middle row of goods, cut as the Mart cuts those - a stand per cell's width, 14
    rows of front, its goods its top, back to where its collision starts (row 80)."""
    facet, own_shell, piece = _helpers()
    return [piece("cooler_%d" % x0, [(x0, 79, x0 + 16, 112)], 14, back=80, leave=floor) for x0 in xs]


def store_4f():
    return own_shell_of(_short_coolers((16, 32, 208, 224, 240, 256), STORE_GREEN)
                        + _u_counter(96, 160, STORE_GREEN)
                        + _store_walls((13, 16)))


def store_5f():
    facet, own_shell, piece = _helpers()
    return own_shell(_short_coolers((96, 112, 160, 176), STORE_CREAM)
                     + _wall_counter(80, 176, STORE_CREAM) + [
        piece("counter_ne", [(224, 76, 240, 128), (240, 76, 288, 96)], 10, leave=STORE_CREAM),
    ] + _store_walls((13, 16), east_from=96))


def own_shell_of(pieces):
    facet, own_shell, piece = _helpers()
    return own_shell(pieces)


# ── Lilycove Museum ───────────────────────────────────────────────────────
#
# No model until the object audit of 2026-11-14: its walls, the paintings on them and its exhibits
# lay drawn on the floor (the paintings slanted with the floor from the player's camera).
#
# 1F, 21 x 14 cells. The back wall (rows 0-32: a cream face, a white line, a pink baseboard) with
# its paintings and the stairs up (cell 16,1, cut out of it under its lintel); a partition from it (x 208-224), drawn as its top (rows 0-48) and its south end (rows
# 48-80): a wall 32 rows tall, 16 thick, to row 80; the gallery wall (x 0-144, rows 72-112: its
# top rows 72-80, its face with two paintings rows 80-112 - 8 thick) turning south at x 144-160
# (its top rows 72-112, its end rows 112-144); a display platform (x 240-320: its top rows 110-150,
# its front 150-160) with a reclining sculpture on it (x 256-304, rows 112-136); two statues
# (cards); a low rope barrier round an exhibit space (x 16-112, rows 157-224: 8 rows tall, its four
# sides meeting at the corners - the sides down the columns first, so they reach the north side's
# face).
#
# 2F, 22 x 13 cells. The back wall (rows 0-32), the stairs down (cell 13,1); the wall stands a cell
# further forward at both ends (x 0-64, x 288-352: its top rows 0-16, its face rows 16-48); a statue
# on a pedestal either side of the stairs (x 168-192, x 232-256, rows 14-48: cards before the
# wall); five display cabinets (x 64 wide), each its painting on its front - 24 rows, standing on
# the edge of its collision (rows 88-112 and 152-176), the light strip of its base in front of it
# the floor's (the walkable row before it) - and its top the 16 rows above.

MUSEUM_FLOOR = ("e6e68b", "ffffcd", "decd73", "cdb462", "7b6229")
MUSEUM_WALL = ("e6e68b", "eeac94", "414a6a", "cdb462", "7b6229", "ffffff", "d5947b")
MUSEUM_WALL_SIDE = (112, 0, 128, 32)      # a plain stretch of the back wall, top to baseboard
MUSEUM2_FLOOR = ("e6e6e6", "c5c5c5", "9c9c9c", "d5d5d5", "b4b4b4")
MUSEUM2_WALL = ("9cbdac", "739483", "4a6a5a", "525252", "c5c5c5")
MUSEUM2_WALL_SIDE = (96, 0, 112, 32)


def museum_1f():
    facet, own_shell, piece = _helpers()
    # the back wall either side of the partition, the stairs (cell 16,1) cut out of it under their
    # lintel (rows 0-8): the lintel in the wall, so its top runs on unbroken
    walls = [piece("wall_w", [(0, 0, 208, 32)], 32, fill=16, side=MUSEUM_WALL_SIDE),
             piece("wall_e", [(224, 0, 255, 32), (255, 0, 273, 8), (273, 0, 336, 32)], 32, fill=16,
                   foot=32, side=MUSEUM_WALL_SIDE)]
    return own_shell([
        # the sculpture before the platform it lies on (a piece claims what no piece before it has)
        piece("sculpture", [(256, 112, 304, 136)], 12, base=10, leave=MUSEUM_FLOOR),
        piece("platform", [(240, 110, 320, 160)], 10, leave=MUSEUM_FLOOR),
        piece("statue_w", [(240, 176, 256, 208)], 32, leave=MUSEUM_FLOOR, card=True),
        piece("statue_e", [(288, 176, 304, 192)], 16, leave=MUSEUM_FLOOR, card=True),
        # the barrier's cream is the floor's own: each side claims its whole shape
        piece("barrier_w", [(16, 160, 32, 216)], 8, claim=[(16, 160, 32, 216)]),
        piece("barrier_e", [(96, 160, 112, 216)], 8, claim=[(96, 160, 112, 216)]),
        piece("barrier_n", [(16, 157, 112, 176)], 8, claim=[(16, 157, 112, 176)]),
        piece("barrier_s", [(16, 208, 112, 224)], 8, claim=[(16, 208, 112, 224)]),
        piece("partition_n", [(208, 0, 224, 80)], 32, side=MUSEUM_WALL_SIDE),
        piece("partition_gallery_e", [(144, 72, 160, 144)], 32, side=MUSEUM_WALL_SIDE),
        piece("partition_gallery", [(0, 72, 144, 112)], 32, side=MUSEUM_WALL_SIDE),
        piece("side_w", [], 32, side=MUSEUM_WALL_SIDE, walls=[((0, 224), (0, 32))]),
        piece("side_e", [], 32, side=MUSEUM_WALL_SIDE, walls=[((336, 32), (336, 224))]),
    ] + walls)


def museum_2f():
    facet, own_shell, piece = _helpers()
    # the back wall, the stairs (cell 13,1) cut out of it under their arch's top (rows 0-8)
    walls = [piece("wall_w", [(64, 0, 208, 32), (208, 0, 224, 8)], 32, fill=16, foot=32,
                   side=MUSEUM2_WALL_SIDE),
             piece("wall_e", [(224, 0, 288, 32)], 32, fill=16, side=MUSEUM2_WALL_SIDE)]
    cabinets = [piece("cabinet_%d_%d" % (x0, z0), [(x0, z0 - 40, x0 + 64, z0)], 24,
                      leave=MUSEUM2_FLOOR)
                for (x0, z0) in ((16, 112), (144, 112), (272, 112), (80, 176), (208, 176))]
    return own_shell(cabinets + [
        piece("statue_w", [(168, 14, 192, 48)], 34, leave=MUSEUM2_WALL + MUSEUM2_FLOOR, card=True),
        piece("statue_e", [(232, 14, 256, 48)], 34, leave=MUSEUM2_WALL + MUSEUM2_FLOOR, card=True),
        piece("wall_cw", [(0, 0, 64, 48)], 32, fill=16, side=MUSEUM2_WALL_SIDE),
        piece("wall_ce", [(288, 0, 352, 48)], 32, fill=16, side=MUSEUM2_WALL_SIDE),
        piece("side_w", [], 32, side=MUSEUM2_WALL_SIDE, walls=[((0, 208), (0, 48))]),
        piece("side_e", [], 32, side=MUSEUM2_WALL_SIDE, walls=[((352, 48), (352, 208))]),
    ] + walls)


# ── The harbors (Lilycove's and Slateport's) ──────────────────────────────
#
# One layout, 24 x 15 cells, no model until the object audit of 2026-11-14: its back wall and the
# railing round the dock lay drawn on the floor. The back wall (rows 0-64: its top band rows 0-8,
# its face with windows rows 8-64 - 56 rows, a warehouse wall); the railing round the dock's water,
# 16 rows tall: its north rail at the water's edge (rows 64-80, before the wall), its west rail
# drawn as its handrail from above (x 48-56, a box from the north rail's face to the south rail's),
# its south rail either side of the boarding gap (rows 160-176), the crate yard's rail (x 272-280)
# and the crates (x 280-384) as one stack, 16 rows. The water is the terrain's.

HARBOR_FLOOR = ("8bb4ac", "a4cdbd", "628b83", "739c8b", "9c8b94", "62627b")
HARBOR_WALL_SIDE = (16, 0, 32, 64)
HARBOR_RAIL_SIDE = (64, 64, 80, 80)       # the north rail: its handrail over two balusters


def harbor():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("rail_w", [(48, 64, 56, 176)], 16, side=HARBOR_RAIL_SIDE),
        piece("rail_yard", [(272, 160, 280, 240)], 16, side=HARBOR_RAIL_SIDE),
        piece("rail_n", [(56, 64, 384, 80)], 16, side=HARBOR_RAIL_SIDE),
        piece("rail_sw", [(56, 160, 112, 176)], 16, side=HARBOR_RAIL_SIDE),
        piece("rail_se", [(152, 160, 272, 176)], 16, side=HARBOR_RAIL_SIDE),
        piece("crates", [(280, 160, 384, 240)], 16, side=HARBOR_RAIL_SIDE),
        piece("wall", [(0, 0, 384, 64)], 56, fill=16, side=HARBOR_WALL_SIDE),
        piece("side_w", [], 56, side=HARBOR_WALL_SIDE, walls=[((0, 240), (0, 64))]),
    ])


# ── The Seaside Cycling Road's gates ──────────────────────────────────────
#
# One layout, 15 x 6 cells, the store's Shop tiles: the back wall (rows 0-32, a clock and a notice on
# it), a counter before it as the store's 1F (arms x 80-96 and 160-176 from the wall's foot, the bar
# between them rows 44-64, 10 rows), two produce stands (x 96-160, rows 72-96: 10 rows), two plants.

def cycling_gate():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("stand_w", [(96, 72, 128, 96)], 10, claim=[(96, 72, 128, 96)]),
        piece("stand_e", [(128, 72, 160, 96)], 10, claim=[(128, 72, 160, 96)]),
        piece("plant_w", [(0, 64, 16, 96)], 28, leave=STORE_ORANGE, card=True),
        piece("plant_e", [(224, 64, 240, 96)], 28, leave=STORE_ORANGE, card=True),
        piece("counter", [(80, 32, 96, 64), (160, 32, 176, 64), (96, 44, 160, 64)], 10,
              leave=STORE_ORANGE),
        piece("wall", [(0, 0, 240, 32)], 32, fill=16, side=STORE_WALL_SIDE),
        piece("side_w", [], 32, side=STORE_WALL_SIDE, walls=[((0, 96), (0, 32))]),
        piece("side_e", [], 32, side=STORE_WALL_SIDE, walls=[((240, 32), (240, 96))]),
    ])


# ── The cable car stations (Route 112's and Mt. Chimney's) ────────────────
#
# One layout, 13 x 12 cells, no model until the object audit of 2026-11-14: its back wall, the
# railings round the two boarding pits, the sign over them, the ticket machine and the bin lay
# drawn on the floor. The back wall (rows 0-32, its pillars painted); the yellow platform and the
# dark track before it are the floor's; each pit (x 0-96 and 112-208, rows 72-118) has a rail on its
# north side (rows 72-82: 10 rows), one down its side by the walkway between them (a strip from
# above, a box 10 rows tall reaching the north rail) and one on its south side (rows 104-118: 14
# rows); the sign stands on the west pit's north rail (x 32-80, rows 66-82: a card); the ticket
# machine (x 192-208, rows 112-144: 24 rows of front), the bin (rows 146-170) and two benches (x
# 0-16: 6 rows, walked on) stand on the floor.

STATION_FLOOR = ("acb4bd", "8b94a4", "dedede", "de4a00", "414a6a", "62627b")
STATION_WALL_SIDE = (40, 0, 56, 32)
STATION_RAIL_SIDE = (8, 104, 24, 118)     # the south rail: its handrail over a baluster


def cable_car_station():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("sign", [(32, 66, 80, 82)], 16, leave=STATION_FLOOR, card=True),
        piece("rail_side_w", [(88, 72, 96, 118)], 10, side=STATION_RAIL_SIDE),
        piece("rail_side_e", [(112, 72, 120, 118)], 10, side=STATION_RAIL_SIDE),
        piece("rail_nw", [(0, 72, 88, 82)], 10, side=STATION_RAIL_SIDE),
        piece("rail_ne", [(120, 72, 208, 82)], 10, side=STATION_RAIL_SIDE),
        piece("rail_sw", [(0, 104, 88, 118)], 14, side=STATION_RAIL_SIDE),
        piece("rail_se", [(120, 104, 208, 118)], 14, side=STATION_RAIL_SIDE),
        piece("machine", [(192, 112, 208, 144)], 24, leave=STATION_FLOOR),
        piece("bin", [(192, 146, 208, 170)], 16, leave=STATION_FLOOR),
        piece("bench_n", [(0, 120, 16, 144)], 6, leave=STATION_FLOOR),
        piece("bench_s", [(0, 146, 16, 176)], 6, leave=STATION_FLOOR),
        piece("wall", [(0, 0, 208, 32)], 32, fill=16, side=STATION_WALL_SIDE),
        piece("side_w", [], 32, side=STATION_WALL_SIDE, walls=[((0, 192), (0, 32))]),
        piece("side_e", [], 32, side=STATION_WALL_SIDE, walls=[((208, 32), (208, 192))]),
    ])


# ── The Battle Tents' lobby (Fallarbor's, Slateport's, Verdanturf's) ───────
#
# One layout, 13 x 10 cells, no model until the object audit of 2026-11-14: its counters, the
# display table and the terminal lay drawn on the floor. The back wall (rows 0-30, the door to the
# battles painted on it, as the Palace's: the attendant walks the player into it); the tent's
# curtains down both sides (x 0-30 and 176-208) stand as side walls at x 16 and 192, dressed with
# the curtain; two C counters round the attendants (x 32-96 and 112-176, their bottom arms rows
# 76-96: 8 rows of front, the yellow their top); the glass display table before the west counter
# (x 56-90, rows 78-104) and the terminal by the east curtain (x 176-192, rows 56-92, 28 rows).

TENT_FLOOR = ("cdd5ff", "b4acf6")
TENT_WALL_SIDE = (40, 0, 56, 30)
TENT_CURTAIN = (0, 30, 16, 62)


def tent_lobby():
    facet, own_shell, piece = _helpers()
    west = [(32, 30, 64, 48), (32, 48, 48, 76), (32, 76, 96, 96)]
    east = [(144, 30, 176, 48), (160, 48, 176, 76), (112, 76, 176, 96)]
    return own_shell([
        piece("table", [(56, 78, 90, 104)], 12, leave=TENT_FLOOR),
        piece("terminal", [(176, 56, 192, 92)], 28, leave=TENT_FLOOR),
        piece("counter_w", west, 8, claim=west),
        piece("counter_e", east, 8, claim=east),
        piece("wall", [(0, 0, 208, 30)], 30, fill=16, side=TENT_WALL_SIDE),
        piece("side_w", [], 30, side=TENT_CURTAIN, walls=[((16, 160), (16, 30))]),
        piece("side_e", [], 30, side=TENT_CURTAIN, walls=[((192, 30), (192, 160))]),
    ])


# ── The Battle Tents' corridor (Fallarbor's, Slateport's, Verdanturf's) ────
#
# One layout, 5 x 8 cells, no model until the object audit of 2026-11-14: its back wall lay drawn on
# the floor. The wall (rows 0-32: its top band, panels and red baseboard) with the door to the battle
# room painted on it (x 24-56: the attendant walks the player into it, as the lobby's); the warp pad
# at the front is the floor's.

TENT_CORRIDOR_WALL_SIDE = (0, 0, 8, 32)


def tent_corridor():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("wall", [(0, 0, 80, 32)], 32, fill=16, side=TENT_CORRIDOR_WALL_SIDE),
        piece("side_w", [], 32, side=TENT_CORRIDOR_WALL_SIDE, walls=[((0, 128), (0, 32))]),
        piece("side_e", [], 32, side=TENT_CORRIDOR_WALL_SIDE, walls=[((80, 32), (80, 128))]),
    ])


# ── The lifts (the Battle Tower's and Trainer Hill's; the Department Store's) ─
#
# Two layouts of the same tiles, 5 x 7 cells (LAYOUT_BATTLE_ELEVATOR) and 5 x 6 (the store's), no
# model until the object audit of 2026-11-14. The lift's back wall (rows 8-48, under 8 rows of the
# dark outside) with its window and handrail painted on it; the floor and its mat from row 48.

LIFT_WALL_SIDE = (1, 8, 5, 48)       # the plain panel inside the wall's left edge (its navy lines read as bars)


def lift(front):
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("wall", [(0, 8, 80, 48)], 40, fill=16, side=LIFT_WALL_SIDE),
        piece("side_w", [], 40, side=LIFT_WALL_SIDE, walls=[((0, front), (0, 48))]),
        piece("side_e", [], 40, side=LIFT_WALL_SIDE, walls=[((80, 48), (80, front))]),
    ])


# ── The Battle Tower's corridors (to the battle room; to the multi battle room) ─
#
# Two layouts, 17 x 5 cells each, no model until the object audit of 2026-11-14. The back wall
# (rows 0-32: panels over a blue or red baseboard) with the doors painted on it - the lift's (x 120-
# 150) and the battle rooms' (walked into, as the Battle Tents' door); the plain corridor's two potted
# plants at its ends (cells 0 and 16, rows 48-80) stand as cards; the floor's Poke Ball marks are the
# floor's.

TOWER_FLOOR = ("00b4ff", "73deff", "0073ff", "0052de")


def tower_corridor(side, plants):
    facet, own_shell, piece = _helpers()
    return own_shell([piece("plant_%d" % x0, [(x0, 48, x0 + 16, 80)], 30, leave=TOWER_FLOOR, card=True)
                      for x0 in plants] + [
        piece("wall", [(0, 0, 272, 32)], 32, fill=16, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, 80), (0, 32))]),
        piece("side_e", [], 32, side=side, walls=[((272, 32), (272, 80))]),
    ])


# ── Slateport's Oceanic Museum 1F ─────────────────────────────────────────
#
# 20 x 9 cells, no model until the object audit of 2026-11-14: its walls, cases, tanks and counters
# lay drawn on the floor. The back wall (rows 0-32) with the fossil tank, the depth-measuring
# machine and the plaques painted on it, its doorway up to 2F (cell 6,1, walked into) cut out under
# its arch: the dark well x 93-115 from row 7. Against the wall two glass cases (x 144-168 and 192-
# 216, rows 0-40): 32 rows of front, their lids laid back to the wall. On the floor two glass tanks
# (x 32-48: 22 rows of front under their tops), three glass boxes on stands (x 240-256 and 288-304:
# 17 rows) and the two reception counters, each a U open to the front (its arms 16 wide down to the
# room's front row, its bar rows 94-112; 8 rows of front). The shadows and the mat are the floor's.

MUSEUM_FLOOR = ("4aa4a4", "7bbdb4", "a4d5c5", "006a73", "208b94")
MUSEUM_WALL_SIDE = (0, 0, 16, 32)


def _museum_counter(name, west):
    facet, own_shell, piece = _helpers()
    shape = [(west, 94, west + 16, 144), (west + 48, 94, west + 64, 144), (west + 16, 94, west + 48, 112)]
    return piece(name, shape, 8, leave=MUSEUM_FLOOR)


def oceanic_museum_1f():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("case_w", [(144, 0, 168, 40)], 32, back=32, leave=MUSEUM_FLOOR),
        piece("case_e", [(192, 0, 216, 40)], 32, back=32, leave=MUSEUM_FLOOR),
        piece("tank_n", [(32, 48, 48, 80)], 22, leave=MUSEUM_FLOOR),
        piece("tank_s", [(32, 96, 48, 128)], 22, leave=MUSEUM_FLOOR),
        piece("box_w", [(240, 53, 256, 80)], 17, leave=MUSEUM_FLOOR),
        piece("box_ne", [(288, 53, 304, 80)], 17, leave=MUSEUM_FLOOR),
        piece("box_se", [(288, 101, 304, 128)], 17, leave=MUSEUM_FLOOR),
        _museum_counter("counter_w", 80),
        _museum_counter("counter_e", 176),
        piece("wall_w", [(0, 0, 93, 32), (93, 0, 115, 7)], 32, fill=16, foot=32, side=MUSEUM_WALL_SIDE),
        piece("wall_e", [(115, 0, 320, 32)], 32, fill=16, side=MUSEUM_WALL_SIDE),
        piece("side_w", [], 32, side=MUSEUM_WALL_SIDE, walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=MUSEUM_WALL_SIDE, walls=[((320, 32), (320, 144))]),
    ])


# ── Slateport's Oceanic Museum, 2F ─────────────────────────────────────────
#
# 20 x 9 cells, no model until the object audit of 2026-11-14: its cases, tables, stands and the pillar
# lay drawn on the floor. The back wall (rows 0-32, the tall glass tank set in it and the plaques painted
# on it), its doorway down (cell 6,1, walked into) cut out under its arch as 1F's; three glass cases
# against it (x 128-160, 240-272, 288-320: 16 rows of front, their glass tops back to the wall); the
# white pillar standing out of it (x 160-176: 32 rows, its top from the wall's foot); two display tables
# (x 32-96: 7 rows) with the models on them (6-8 rows more); two computers on their stands (16 rows); two
# glass tubes (x 288-304: 23 rows, their lids their tops); the ship model on its plinth (the plinth 7
# rows, the model 6 more on it). The shadows are the floor's.

MUSEUM_TABLE = ("fff6de", "eedeac")              # the display tables' top
MUSEUM_PLINTH = ("ffffff", "c5cdf6", "94a4ee")   # the ship model's plinth's top


def oceanic_museum_2f():
    facet, own_shell, piece = _helpers()
    fl = MUSEUM_FLOOR
    return own_shell([
        piece("model_sub", [(40, 48, 63, 63)], 8, base=7, leave=MUSEUM_TABLE),
        piece("model_sphere", [(66, 50, 78, 60)], 8, base=7, leave=MUSEUM_TABLE),
        piece("model_ship", [(48, 96, 80, 112)], 6, base=7, leave=MUSEUM_TABLE),
        piece("table_n", [(32, 46, 96, 80)], 7, leave=fl),
        piece("table_s", [(32, 94, 96, 128)], 7, leave=fl),
        piece("ship", [(210, 105, 240, 117)], 6, base=7, leave=MUSEUM_PLINTH),
        piece("plinth", [(208, 112, 240, 128)], 7, leave=fl),
        piece("pc_w", [(192, 56, 216, 80)], 16, leave=fl),
        piece("pc_e", [(240, 56, 264, 80)], 16, leave=fl),
        piece("tube_n", [(288, 48, 304, 80)], 23, leave=fl),
        piece("tube_s", [(288, 96, 304, 128)], 23, leave=fl),
        piece("case_w", [(128, 16, 160, 40)], 16, leave=fl),
        piece("case_mid", [(240, 16, 272, 40)], 16, leave=fl),
        piece("case_e", [(288, 16, 320, 40)], 16, leave=fl),
        piece("pillar", [(160, 0, 176, 64)], 32, leave=fl),
        piece("wall_w", [(0, 0, 93, 32), (93, 0, 115, 7)], 32, fill=16, foot=32, side=MUSEUM_WALL_SIDE),
        piece("wall_e", [(115, 0, 320, 32)], 32, fill=16, side=MUSEUM_WALL_SIDE),
        piece("side_w", [], 32, side=MUSEUM_WALL_SIDE, walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=MUSEUM_WALL_SIDE, walls=[((320, 32), (320, 144))]),
    ])


# ── The Battle Tower's multi partner room ─────────────────────────────────
#
# 21 x 15 cells, no model until the object audit of 2026-11-14. The back wall (rows 0-32, the lift's
# door painted on it as the corridors'), two benches against the room's east edge (x 320-336: 8
# rows), four potted plants at the front corners (cards); the floor's darker strips are the floor's.

def tower_multi_partner_room():
    facet, own_shell, piece = _helpers()
    side = (96, 0, 112, 32)
    return own_shell([piece("plant_%d" % x0, [(x0, 208, x0 + 16, 240)], 30, leave=TOWER_FLOOR, card=True)
                      for x0 in (0, 16, 304, 320)] + [
        piece("bench_n", [(320, 64, 336, 112)], 8, leave=TOWER_FLOOR),
        piece("bench_s", [(320, 144, 336, 192)], 8, leave=TOWER_FLOOR),
        piece("wall", [(0, 0, 336, 32)], 32, fill=16, side=side),
        piece("side_w", [], 32, side=side, walls=[((0, 240), (0, 32))]),
        piece("side_e", [], 32, side=side, walls=[((336, 32), (336, 240))]),
    ])


# ── The Battle Arena's corridor ───────────────────────────────────────────
#
# 18 x 14 cells, no model until the object audit of 2026-11-14 (V: its island of shoji screens lay
# flat round the player's path). The player walks round a T of screens: its front (x 48-240, rows
# 128-192: 64 rows, the screens and their frames) under a black top, the T's stem between the two
# back walls (x 96-192) black up to the room's back. Two blocks of 64 rows: the stem's top runs back
# to the room's back (z 0), the wings' to the blocked cells' back (z 80) - the black cells are the
# island, not floor. The back walls either side (rows 0-48, 48 rows of screens), side walls, four
# small potted plants in the corners (cards); the green mats and the island's shadow are the floor's.

ARENA_FLOOR = ("decd83", "d5b46a", "eee6a4")
ARENA_WALL_SIDE = (16, 0, 32, 48)
ARENA_ISLAND_SIDE = (112, 128, 128, 192)


def arena_corridor():
    facet, own_shell, piece = _helpers()
    plants = [piece("plant_%d_%d" % (x0, z0), [(x0, z0, x0 + 16, z0 + 16)], 15, leave=ARENA_FLOOR, card=True)
              for x0 in (0, 272) for z0 in (48, 208)]
    return own_shell(plants + [
        piece("block_stem", [(96, 0, 192, 192)], 64, back=0, side=ARENA_ISLAND_SIDE),
        piece("block_wings", [(48, 80, 96, 192), (192, 80, 240, 192)], 64, back=80, side=ARENA_ISLAND_SIDE),
        piece("wall_w", [(0, 0, 96, 48)], 48, fill=16, side=ARENA_WALL_SIDE),
        piece("wall_e", [(192, 0, 288, 48)], 48, fill=16, side=ARENA_WALL_SIDE),
        piece("side_w", [], 48, side=ARENA_WALL_SIDE, walls=[((0, 224), (0, 48))]),
        piece("side_e", [], 48, side=ARENA_WALL_SIDE, walls=[((288, 48), (288, 224))]),
    ])


# ── The Battle Dome's corridor ────────────────────────────────────────────
#
# 48 x 7 cells, no model until the object audit of 2026-11-14: its back wall (rows 0-64: the dome's
# windows, their white beams and sills over its panels, their doors and red base band, all painted
# on it - the attendant walks the player along) lay drawn on the floor. The wall stands in six
# pieces, one to each 8-cell repeat of its pattern; side walls, a door's plain jamb for their face;
# the floor and its mat are the floor's.

DOME_WALL_SIDE = (73, 0, 77, 64)


def dome_corridor():
    facet, own_shell, piece = _helpers()
    return own_shell([piece("wall_%d" % n, [(n * 128, 0, n * 128 + 128, 64)], 64, fill=16, side=DOME_WALL_SIDE)
                      for n in range(6)] + [
        piece("side_w", [], 64, side=DOME_WALL_SIDE, walls=[((0, 112), (0, 64))]),
        piece("side_e", [], 64, side=DOME_WALL_SIDE, walls=[((768, 64), (768, 112))]),
    ])


# ── The Pokemon League 1F (Ever Grande City) ───────────────────────────────
#
# 19 x 12 cells, the Pokemon Center's tiles in a wider room, with no model of its own until the object
# audit of 2026-11-14: only the Center's machines, tables, stools, plants and escalator stood (the
# Center's pieces, found by their tiles); its back wall and its two counters lay drawn on the floor, the
# nurse and the clerk seen whole over a low strip (V). As the Center's: the back wall standing its
# drawn face (rows 0-30, its foot row 32) with its chamfered corners and the side walls the drawing
# never shows; the doorway on to the Elite Four (x 142-178, its dark from row 7, walked into) cut out
# of it under its lintel; the two L counters - the nurse's (x 0-95) and the clerk's (x 225-304), each a
# bar (its top rows 45-52, its trim and lavender front rows 53-62: 10 rows, the Center's counter's
# height, which the Center's Poke Balls standing on them here are made for) and an arm from the
# wall's foot (row 32; its top's end drawn over the wall's baseboard is the wall's, painted on it, as
# the Department Store's).

LEAGUE_WALL_SIDE = (112, 0, 128, 32)     # a plain panel of the back wall, band to foot


def league_1f():
    facet, own_shell, piece = _helpers()
    nurse = [(80, 32, 95, 63), (0, 45, 95, 63)]
    clerk = [(225, 32, 240, 63), (225, 45, 304, 63)]
    return own_shell([
        piece("counter_w", nurse, 10, claim=nurse),
        piece("counter_e", clerk, 10, claim=clerk),
        facet("corner_w", (0, 15, 47), (16, -1, 31), 32, walls=[((0, 192), (0, 47))],
              side=LEAGUE_WALL_SIDE, cells=[(0, 11)]),
        facet("corner_e", (288, -1, 31), (304, 15, 47), 32, walls=[((304, 47), (304, 192))],
              side=LEAGUE_WALL_SIDE, cells=[(18, 11)]),
        piece("wall_w", [(16, 0, 142, 32), (142, 0, 178, 7)], 32, fill=16, foot=32, side=LEAGUE_WALL_SIDE),
        piece("wall_e", [(178, 0, 288, 32)], 32, fill=16, side=LEAGUE_WALL_SIDE),
    ])


# ── The Battle Dome's lobby ─────────────────────────────────────────────────
#
# 23 x 17 cells, no model until the object audit of 2026-11-14: its wall, counters and rope barriers lay
# drawn on the floor. The back wall (rows 0-80: a white band, pale panels, pillars, a red baseboard -
# 80 rows, as drawn) with its two doors painted on it (x 72-104 and 264-296: the attendants walk the
# player into them, as the Battle Tents' door). Each side, mirrored: an L counter round its attendant -
# an arm with an angled end (x 32-56 or 312-336, rows 112-160) and a bar (x 32-80 or 288-336, its top
# to row 168, its front rows 169-175: 7 rows) - with a display on the bar (x 64-80 or 288-304); a short
# counter (x 96-144 or 224-272, 7 rows of front) with a terminal or a glass case on it. Between them
# the PC (x 176-192) and the rope barriers round it, and two more against the side walls: thin posts
# and ropes, cards. The potted plants by the entrance are cards; the mats are the floor's.

DOME_FLOOR = ("8bcdac", "9ceecd", "7bac8b")
DOME_WALL_SIDE = (160, 0, 176, 80)       # a plain panel of the back wall, band to baseboard
# the counters' yellow top and white edge round what stands on them
DOME_COUNTER = ("ffde83", "ffffff")
DOME_PLANTS = [(1, 13), (2, 13), (3, 13), (4, 13), (19, 13), (20, 13), (21, 13), (22, 13),
               (1, 15), (2, 15), (17, 15), (18, 15), (19, 15), (20, 15), (21, 15), (22, 15)]


def dome_lobby():
    facet, own_shell, piece = _helpers()
    plants = [piece("plant_%d_%d" % (cx, cy), [(cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 32)], 28,
                    leave=DOME_FLOOR, card=True) for (cx, cy) in DOME_PLANTS]
    l_west = [(32, 112, 56, 176), (32, 160, 80, 176)]
    l_east = [(312, 112, 336, 176), (288, 160, 336, 176)]
    return own_shell(plants + [
        piece("display_w", [(64, 148, 80, 168)], 12, base=7, leave=DOME_COUNTER),
        piece("display_e", [(288, 148, 304, 168)], 12, base=7, leave=DOME_COUNTER),
        piece("terminal_w", [(112, 148, 128, 168)], 12, base=7, leave=DOME_COUNTER),
        piece("case_e", [(233, 145, 263, 168)], 8, base=7, leave=DOME_COUNTER),
        piece("counter_w", l_west, 7, claim=l_west),
        piece("counter_e", l_east, 7, claim=l_east),
        piece("desk_w", [(96, 155, 144, 176)], 7, claim=[(96, 155, 144, 176)]),
        piece("desk_e", [(224, 155, 272, 176)], 7, claim=[(224, 155, 272, 176)]),
        piece("pc", [(176, 136, 192, 168)], 24, leave=DOME_FLOOR),
        piece("ropes_n", [(144, 127, 224, 144)], 16, leave=DOME_FLOOR, card=True),
        piece("ropes_s", [(144, 159, 224, 176)], 16, leave=DOME_FLOOR, card=True),
        piece("ropes_w", [(0, 159, 32, 176)], 16, leave=DOME_FLOOR, card=True),
        piece("ropes_e", [(336, 159, 368, 176)], 16, leave=DOME_FLOOR, card=True),
        piece("wall", [(0, 0, 368, 80)], 80, fill=16, side=DOME_WALL_SIDE),
        piece("side_w", [], 80, side=DOME_WALL_SIDE, walls=[((0, 272), (0, 80))]),
        piece("side_e", [], 80, side=DOME_WALL_SIDE, walls=[((368, 80), (368, 272))]),
    ])


# ── The Battle Palace's corridor ─────────────────────────────────────────────
#
# 17 x 14 cells, no model until the object audit of 2026-11-14: its back wall lay drawn on the floor.
# The wall (rows 0-64: the orange face with its arched windows between golden pillars) with the two
# doors to the battle rooms painted on it (x 88-120 and 152-184: the attendant walks the player into
# them, as the Palace lobby's). The walkway's stone kerbs, the flower beds and the water either side are
# the floor's (the flowers stand as the props they are); the room's sides are the water's edge, open
# as drawn: no side walls.

PALACE_CORRIDOR_WALL_SIDE = (48, 0, 64, 64)     # a plain stretch of the wall, top to foot


def palace_corridor():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("wall", [(0, 0, 272, 64)], 64, fill=16, side=PALACE_CORRIDOR_WALL_SIDE),
    ])


# ── Slateport's Pokemon Fan Club ───────────────────────────────────────────
#
# 14 x 11 cells, no model until the object audit of 2026-11-14: everything in it lay drawn on the floor.
# Its tiles keep the floor, the rug, the furniture's shadows and the wall on their lower layer and every
# piece of furniture on the upper, so each piece stands on its cells' own floor (`under`: own). The back
# wall is drawn from 8 rows down the first cell (the black over it is nothing) to its foot 40 rows down,
# as Rustboro's school's (a picture and the club's emblem on it); two pilasters stand 4 rows proud of it
# (x 32-48, x 176-192, their feet at row 44). Against it a low cabinet (x 0-32: 13 rows of front, its top
# back to the wall), two tall bookcases (x 192-224: 22 rows, their tops back to the wall), two plants (cards). On the rug the glass
# table (x 81-143: 14 rows of front, its legs and its shelf, the glass its top) and three sofas - walked
# on, as the Center's stools are, so 6 rows as its stools: whoever sits on them sinks in as onto a stool
# (the chairman on the north one), the backs laid on their tops. Two more plants at the front corners.

FANCLUB_FLOOR = ("f6eeb4", "ded58b", "c5bd7b", "ee836a", "cd6a6a", "ffcd94")   # the floor, the rug, its shadows
FANCLUB_WALL = ("e6d583", "f6eecd", "9c9c8b", "a49441", "d5a46a")
FANCLUB_WALL_SIDE = (64, 8, 80, 40)      # a plain stretch of the wall, top to foot


def slateport_fan_club():
    facet, own_shell, piece = _helpers()
    plants = [piece("plant_%d_%d" % (x0 // 16, z1 // 16), [(x0, z1 - 32, x0 + 16, z1)], 28,
                    leave=FANCLUB_FLOOR + FANCLUB_WALL, card=True)
              for (x0, z1) in ((48, 48), (160, 48), (0, 176), (208, 176))]
    return own_shell(plants + [
        piece("sofa_n", [(80, 73, 144, 96)], 6, leave=FANCLUB_FLOOR),
        piece("sofa_w", [(48, 96, 64, 128)], 6, leave=FANCLUB_FLOOR),
        piece("sofa_e", [(160, 96, 176, 128)], 6, leave=FANCLUB_FLOOR),
        piece("table", [(81, 98, 143, 131)], 14, leave=FANCLUB_FLOOR),
        piece("cabinet", [(0, 27, 32, 48)], 13, leave=FANCLUB_FLOOR + FANCLUB_WALL),
        piece("bookcases", [(192, 18, 224, 47)], 22, leave=FANCLUB_FLOOR + FANCLUB_WALL),
        piece("pilaster_w", [(32, 8, 48, 44)], 32, leave=FANCLUB_FLOOR),
        piece("pilaster_e", [(176, 8, 192, 44)], 32, leave=FANCLUB_FLOOR),
        piece("wall", [(0, 8, 224, 40)], 32, fill=16, foot=40, side=FANCLUB_WALL_SIDE),
        piece("side_w", [], 32, side=FANCLUB_WALL_SIDE, walls=[((0, 176), (0, 40))]),
        piece("side_e", [], 32, side=FANCLUB_WALL_SIDE, walls=[((224, 40), (224, 176))]),
    ])


# ── Lilycove's Pokemon Trainer Fan Club ───────────────────────────────────
#
# 12 x 14 cells, a house's room (the generic building's tiles): the houses' bookcase, chest, television,
# cupboard, chairs, one table and its teapot stood in it already, found by their tiles, but its back wall
# with its two posters, the two partitions across the room and its two white sofas lay drawn on the floor
# (the object audit of 2026-11-14), and the second table - its tiles not the house's, no teapot on it -
# stood only as its blocked cells. The back wall its drawn 32 rows, as a house's. The partitions (x 0-47
# and x 145-192): their white tops rows 107-120, their faces down to their feet at row 144 - 24 rows, a
# half wall whoever stands behind it is seen over. The sofas, against the back wall (x 160-192) and the
# west partition (x 0-32): 8 rows of front, their backs laid on their tops - walked on as the Center's
# stools are, and whoever sits on them (the boy on the west one) sinks in as onto a stool. The second
# table as the house's (11 rows). The tiles keep the floor, the rug, the wall and the partitions on their
# lower layer and the furniture on the upper, so each piece stands on its cells' own floor.

TRAINER_CLUB_FLOOR = ("ded552", "bdb431", "8b8b8b", "9c9410")   # the house kit's planks and their shade
TRAINER_CLUB_RUG = ("ffcd8b", "f6f6a4")
TRAINER_CLUB_WALL = ("d5d5b4", "b4b4a4", "ffffff", "629c8b")    # plaster, trim, baseboard
TRAINER_CLUB_WALL_SIDE = (48, 0, 64, 32)     # a plain stretch of the back wall, top to foot


def trainer_fan_club():
    facet, own_shell, piece = _helpers()
    fl = TRAINER_CLUB_FLOOR
    return own_shell([
        piece("sofa_ne", [(160, 24, 192, 48)], 8, leave=fl + TRAINER_CLUB_WALL),
        piece("sofa_w", [(0, 136, 32, 160)], 8, leave=fl),
        piece("table_w", [(66, 144, 94, 177)], 11, leave=TRAINER_CLUB_RUG, solid=True, foot=177),
        piece("partition_w", [(0, 107, 45, 108), (0, 108, 46, 109), (0, 109, 47, 144)], 24, fill=16,
              side=(32, 119, 44, 144)),
        piece("partition_e", [(147, 107, 192, 108), (146, 108, 192, 109), (145, 109, 192, 144)], 24, fill=16,
              side=(148, 119, 160, 144)),
        piece("wall", [(0, 0, 192, 32)], 32, fill=16, foot=32, side=TRAINER_CLUB_WALL_SIDE),
        piece("side_w", [], 32, side=TRAINER_CLUB_WALL_SIDE, walls=[((0, 224), (0, 32))]),
        piece("side_e", [], 32, side=TRAINER_CLUB_WALL_SIDE, walls=[((192, 32), (192, 224))]),
    ])


# ── Stern's Shipyard, ground floor (Slateport) ─────────────────────────────
#
# 21 x 15 cells, no model until the object audit of 2026-11-14: everything in it lay drawn on the
# floor, its people standing on flat stools. The office (x 0-96): its back wall, the stairs up cut out
# of it under their lintel (the opening x 40-72), a cabinet with a model ship on it against the wall,
# three desks (8 rows) with their screens standing on them (6 more), five stools (6 rows, walked on as
# the Center's), a whiteboard on its legs (a card, 30 rows). Between the office and the workshop a wall
# runs down the room (x 96-112), drawn as its long white top: 30 rows tall, the gap where the two rooms
# meet at rows 9-10. The workshop: its back wall (three consoles against it, signs and hooks on it), an
# inner wall across it (x 112-240, 27 rows) with a post at its end, the small machine and the engine and
# its pump before it, three lattice girders standing on end (cards, 45-61 rows: the floor drawn between
# their bars), and the lumber - beams laid lengthwise in piles whose ends face the room, each pile its
# ends' height (8-24 rows), its top running back over its cells. The tiles keep the floor and the walls
# on their lower layer and the furniture on the upper, so each piece stands on its cells' own floor.

SHIPYARD_FLOOR = ("739c8b", "628b83", "a4cdbd", "8bb4ac", "9c8b94",   # the office's tiles
                  "6a7b41", "839473", "acb494", "dedec5")              # the workshop's
SHIPYARD_WALL = ("ffffff", "dedede", "8b94a4", "6a7b7b")
SHIPYARD_WALL_SIDE = (80, 0, 96, 32)        # a plain stretch of the office's back wall, top to foot
SHIPYARD_POST_SIDE = (98, 121, 110, 146)    # the dividing wall's end, its striped face
SHIPYARD_RAIL_SIDE = (140, 194, 156, 208)   # the gallery's south rail: its handrail over balusters


def shipyard_1f():
    facet, own_shell, piece = _helpers()
    fl = SHIPYARD_FLOOR
    stools = [piece("stool_%d_%d" % (x0 // 16, y0 // 16), [(x0, y0, x0 + 12, y0 + 14)], 6, leave=fl)
              for (x0, y0) in ((82, 81), (18, 97), (66, 129), (2, 145), (18, 193))]
    desks = []
    for n, y in enumerate((72, 120, 168)):
        desks += [piece("screen_%d" % n, [(2, y, 16, y + 9)], 6, base=8, leave=fl),
                  piece("desk_%d" % n, [(0, y + 3, 32, y + 24)], 8, leave=fl)]
    # (the floor drawn between a girder's bars at its foot its own: the room's first floor, under it, is
    # the office's)
    girders = [piece("girder_%d" % n, [(x0, y0, x0 + 16, y1)], h, card=True, claim=[(x0, y1 - 1, x0 + 16, y1)])
               for n, (x0, y0, y1, h) in enumerate(((112, 64, 112, 45), (160, 144, 208, 61),
                                                     (224, 160, 208, 45)))]
    lumber = [piece("lumber_%d_%d" % (x0 // 16, y0 // 16), [(x0, y0, x1, y1)], h, leave=fl)
              for (x0, y0, x1, y1, h) in ((304, 64, 320, 112, 8), (320, 48, 336, 112, 16),
                                          (288, 176, 320, 224, 8), (320, 160, 336, 224, 16),
                                          (112, 176, 128, 240, 24), (128, 192, 144, 240, 16),
                                          (144, 208, 160, 240, 8))]
    return own_shell(stools + desks + girders + lumber + [
        piece("whiteboard", [(64, 97, 96, 127)], 30, leave=fl, card=True),
        piece("cabinet", [(0, 24, 32, 48)], 10, leave=fl + SHIPYARD_WALL),
        piece("pump", [(192, 120, 224, 161)], 24, leave=fl),
        piece("engine", [(176, 72, 240, 137)], 30, leave=fl + SHIPYARD_WALL),
        piece("machine", [(144, 72, 176, 120)], 24, leave=fl + SHIPYARD_WALL),
        piece("consoles", [(128, 16, 176, 39)], 17, back=32, leave=fl + SHIPYARD_WALL),
        piece("post", [(240, 57, 256, 97)], 28, leave=fl, side=SHIPYARD_POST_SIDE),
        piece("wall_inner", [(112, 60, 240, 96)], 27, fill=16, side=SHIPYARD_POST_SIDE),
        piece("wall_column_n", [(96, 0, 112, 146)], 30, fill=16, side=SHIPYARD_POST_SIDE),
        piece("wall_column_s", [(96, 169, 112, 240)], 30, fill=16, side=SHIPYARD_POST_SIDE),
        piece("wall_office", [(0, 0, 40, 32), (40, 0, 72, 12), (72, 0, 96, 32)], 32, fill=16, foot=32,
              side=SHIPYARD_WALL_SIDE),
        piece("wall_works", [(112, 0, 336, 32)], 32, fill=16, foot=32, side=SHIPYARD_WALL_SIDE),
        piece("side_w", [], 32, side=SHIPYARD_WALL_SIDE, walls=[((0, 240), (0, 32))]),
        piece("side_e", [], 32, side=SHIPYARD_WALL_SIDE, walls=[((336, 32), (336, 240))]),
    ])


# ── Stern's Shipyard, upper floor ──────────────────────────────────────────
#
# 17 x 15 cells, no model until the object audit of 2026-11-14: the gallery over the workshop. Its back
# wall with the stairs down cut out as below, three pairs of desks (8 rows) with a screen on one of each,
# fifteen stools (6 rows, walked on), two whiteboards on their legs (cards, 30 rows) and a bin (12 rows);
# the railing round the opening over the workshop (x 128-256, rows 80-208): its north and south sides
# 14 rows (a handrail on balusters over the floor's edge), its west and east sides drawn as strips
# from above, boxes 14 rows tall from where their strips begin to the south side. Through the opening
# the drawing shows the workshop below: the floor's, as drawn.

def shipyard_2f():
    facet, own_shell, piece = _helpers()
    fl = SHIPYARD_FLOOR
    stools = [piece("stool_%d_%d" % (x0 // 16, y0 // 16), [(x0, y0, x0 + 12, y0 + 14)], 6, leave=fl)
              for (x0, y0) in ((18, 97), (50, 97), (114, 97), (114, 113), (2, 145), (50, 145), (114, 145),
                               (18, 193), (50, 193), (178, 209), (194, 209), (178, 49), (242, 49),
                               (130, 65), (194, 65))]
    desks = []
    for n, (y, px) in enumerate(((72, 34), (120, 2), (168, 2))):
        desks += [piece("screen_%d" % n, [(px, y, px + 14, y + 9)], 6, base=8, leave=fl),
                  piece("desks_%d" % n, [(0, y + 3, 64, y + 24)], 8, leave=fl)]
    return own_shell(stools + desks + [
        piece("whiteboard_w", [(144, 49, 176, 79)], 30, leave=fl, card=True),
        piece("whiteboard_e", [(208, 49, 240, 79)], 30, leave=fl, card=True),
        piece("bin", [(114, 176, 126, 192)], 12, leave=fl),
        piece("rail_w", [(128, 89, 136, 208)], 14, side=SHIPYARD_RAIL_SIDE),
        piece("rail_e", [(248, 89, 256, 208)], 14, side=SHIPYARD_RAIL_SIDE),
        piece("rail_n", [(128, 80, 256, 96)], 14, side=SHIPYARD_RAIL_SIDE),
        piece("rail_s", [(128, 192, 256, 208)], 14, side=SHIPYARD_RAIL_SIDE),
        piece("wall", [(0, 0, 40, 32), (40, 0, 72, 12), (72, 0, 272, 32)], 32, fill=16, foot=32,
              side=SHIPYARD_WALL_SIDE),
        piece("side_w", [], 32, side=SHIPYARD_WALL_SIDE, walls=[((0, 240), (0, 32))]),
        piece("side_e", [], 32, side=SHIPYARD_WALL_SIDE, walls=[((272, 32), (272, 240))]),
    ])


# ── Lilycove's Contest Lobby ───────────────────────────────────────────────
#
# 31 x 12 cells, no model until the object audit of 2026-11-14. The back wall (rows 0-32: a red
# curtain wall, frames and posters on it), its two doorways to the halls (cells 9,1 and 21,1, walked
# into) cut out under their curtains (rows 0-14, the openings x 141-161 and 333-353); the reception
# counter (x 176-304): its arms from the wall (rows 26-64) and its glass front between them (x 208-
# 272, rows 46-64) 10 rows tall, the desk's top over the floor either side of the glass a slab on
# the arms' height; the four machines (x 368-384, 432-448: 12 rows); the sofas at both ends are
# walked on and stay as drawn. The terminal by the counter (x 304-320, rows 8-48) is the wall's: its
# foot is drawn over a cell the player stands on, so standing it there would put it before them.

CONTEST_FLOOR = ("f6b49c", "d5ac7b")
CONTEST_WALL_SIDE = (48, 0, 64, 32)


def contest_lobby():
    facet, own_shell, piece = _helpers()
    body = [(176, 26, 192, 64), (288, 26, 304, 64), (208, 46, 272, 64)]
    machines = [piece("machine_%d_%d" % (x0, z0), [(x0, z0, x0 + 16, z0 + 16)], 12, leave=CONTEST_FLOOR)
                for x0 in (368, 432) for z0 in (80, 144)]
    return own_shell(machines + [
        piece("counter", body, 10, claim=body),
        piece("desk_w", [(192, 46, 208, 54)], 2, base=8, claim=[(192, 46, 208, 54)]),
        piece("desk_e", [(272, 46, 288, 54)], 2, base=8, claim=[(272, 46, 288, 54)]),
        piece("wall_w", [(0, 0, 141, 32), (141, 0, 161, 14)], 32, fill=16, foot=32, side=CONTEST_WALL_SIDE),
        piece("wall_mid", [(161, 0, 333, 32), (333, 0, 353, 14)], 32, fill=16, foot=32,
              side=CONTEST_WALL_SIDE),
        piece("wall_e", [(353, 0, 496, 32)], 32, fill=16, side=CONTEST_WALL_SIDE),
        piece("side_w", [], 32, side=CONTEST_WALL_SIDE, walls=[((0, 192), (0, 32))]),
        piece("side_e", [], 32, side=CONTEST_WALL_SIDE, walls=[((496, 32), (496, 192))]),
    ])


# ── The Day Care (Route 117) ──────────────────────────────────────────────
#
# 12 x 9 cells, no model until the object audit of 2026-11-14: its counter, its displays, its
# table and the fence round its garden lay drawn on the floor. The back wall (rows 0-32), its
# doorway (cell 2,1) cut out under its lintel (rows 0-12, the opening x 32-48); the counter an L
# round the Day Care lady (its arm x 64-78 from the wall's foot, its bar x 0-78 rows 44-64: 8 rows,
# the arm's end over the wall's baseboard the wall's); two lattice displays on stands (x 80-112,
# rows 30-48: 8 rows); the table (x 160-192, rows 80-112: 8 rows of legs, the planks its top); the
# chairs walked on (6 rows); two plants (cards); the PC is the wall's (its foot over a cell the
# player stands on). The garden's fence (x 104-192, rows 128-144) stands its pickets as a card - the
# floor seen between them - and its gate post (x 96-104, rows 108-142) as tall as it is drawn.

DAYCARE_FLOOR = ("7b9cff", "6283e6", "4a6ad5", "4162a4", "bde673", "a4c562")   # the boards, the garden
DAYCARE_WALL_SIDE = (64, 0, 80, 32)


def day_care():
    facet, own_shell, piece = _helpers()
    counter = [(64, 32, 78, 56), (0, 44, 78, 64)]
    chairs = [piece("chair_%d_%d" % (x0, z0), [(x0, z0, x0 + 16, z0 + 16)], 6, leave=DAYCARE_FLOOR)
              for (x0, z0) in ((32, 64), (176, 64), (144, 96))]
    plants = [piece("plant_%d" % z0, [(0, z0, 16, z0 + 32)], 28, leave=DAYCARE_FLOOR, card=True)
              for z0 in (80, 112)]
    return own_shell(chairs + plants + [
        piece("counter", counter, 8, claim=counter),
        piece("display_w", [(80, 30, 96, 48)], 8, leave=DAYCARE_FLOOR),
        piece("display_e", [(96, 30, 112, 48)], 8, leave=DAYCARE_FLOOR),
        piece("table", [(160, 80, 192, 112)], 8, leave=DAYCARE_FLOOR),
        piece("post", [(96, 108, 104, 142)], 34, leave=DAYCARE_FLOOR),
        piece("fence", [(104, 128, 192, 144)], 16, leave=DAYCARE_FLOOR, card=True),
        piece("wall", [(0, 0, 32, 32), (32, 0, 48, 12), (48, 0, 192, 32)], 32, fill=16, foot=32,
              side=DAYCARE_WALL_SIDE),
        piece("side_w", [], 32, side=DAYCARE_WALL_SIDE, walls=[((0, 144), (0, 32))]),
        piece("side_e", [], 32, side=DAYCARE_WALL_SIDE, walls=[((192, 32), (192, 144))]),
    ])


# ── The Safari Zone's entrance (Route 121) ────────────────────────────────
#
# 18 x 14 cells, the store's Shop tiles, no model until the object audit of 2026-11-14. Its first
# room is the Cycling Road gate's, 16 pixels east: the back wall (rows 0-32), the counter before it
# (arms x 96-112 and 176-192 from the wall's foot, the bar between them rows 44-64: 10 rows), two
# produce stands (x 112-176, rows 72-96), the terminal the wall's (its foot over a cell the player
# stands on); three chairs (walked on: 6 rows). The corridor south of it (x 192-288): the
# attendants' green counter (x 256-288, rows 104-192, an open bracket round them: 8 rows), four
# plants (cards, one in the first room), a side wall down the black outside west of it (x 192).

def safari_entrance():
    facet, own_shell, piece = _helpers()
    counter = [(96, 32, 112, 64), (176, 32, 192, 64), (112, 44, 176, 64)]
    desk = [(256, 104, 272, 192), (272, 104, 288, 124), (272, 168, 288, 192)]
    chairs = [piece("chair_%d_%d" % (x0, z0), [(x0, z0, x0 + 16, z0 + 16)], 6, leave=STORE_ORANGE)
              for (x0, z0) in ((224, 48), (272, 48), (272, 64))]
    plants = [piece("plant_%d_%d" % (x0, z0), [(x0, z0, x0 + 16, z0 + 32)], 28, leave=STORE_ORANGE,
                    card=True) for (x0, z0) in ((0, 64), (192, 112), (192, 192), (272, 192))]
    return own_shell(chairs + plants + [
        piece("stand_w", [(112, 72, 144, 96)], 10, claim=[(112, 72, 144, 96)]),
        piece("stand_e", [(144, 72, 176, 96)], 10, claim=[(144, 72, 176, 96)]),
        piece("counter", counter, 10, claim=counter),
        piece("desk", desk, 8, claim=desk),
        piece("wall", [(0, 0, 288, 32)], 32, fill=16, side=STORE_WALL_SIDE),
        piece("side_w", [], 32, side=STORE_WALL_SIDE, walls=[((0, 96), (0, 32))]),
        piece("side_mid", [], 32, side=STORE_WALL_SIDE, walls=[((192, 224), (192, 96))]),
        piece("side_e", [], 32, side=STORE_WALL_SIDE, walls=[((288, 32), (288, 224))]),
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
        # its three plants (cells 2-4, 1) are the house kit's, reused (gen_voxel_buildings.reuse_pieces):
        # since the kit's plant takes its whole silhouette, cards of their own here kept only stray pixels
    ] + [
        piece("stove", [(112, 14, 128, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        piece("cabinet", [(128, 14, 144, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        piece("sink", [(144, 14, 176, 40)], 20, back=32, leave=HOUSE_WALL + SOOT_FLOOR),
        # behind the plants the doorway to the stairs down (x 45-67, rows 9-32: its yellow frame, the
        # dark well; the plants block it until the layout with the stairs unblocked): what they hide of
        # it runs on down from what shows above them, a doorway behind them and not loose yellow bits
        piece("wall", [(0, 0, 176, 32)], 24, fill=16, side=side, fill_down=[(45, 9, 68, 32)]),
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
            # each cell's own drawing under the walls: with the purple floor there, the black drawn
            # past the walls' ends showed a purple hairline at its decal's edge, off both back corners
            # into the dark (the object audit of 2026-11-14)
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_HALL_OF_FAME", "ground": [0x2F6],
                         "under": "own", "reuse_in": frozenset(), "pieces": hall_of_fame()},
        },
        {
            "name": "store_rooftop",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_ROOFTOP",
                         "ground": [0x24A], "reuse_in": frozenset(), "pieces": rooftop()},
        },
        {
            "name": "store_1f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_1F", "ground": [0x268],
                         "under": "own", "reuse_in": frozenset(), "pieces": store_1f()},
        },
        {
            "name": "store_2f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_2F", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": store_2f()},
        },
        {
            "name": "store_3f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_3F", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": store_3f()},
        },
        {
            "name": "store_4f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_4F", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": store_4f()},
        },
        {
            "name": "store_5f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_5F", "ground": [0x2F2, 0x2F4],
                         "under": "own", "reuse_in": frozenset(), "pieces": store_5f()},
        },
        {
            "name": "museum_1f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_LILYCOVE_MUSEUM_1F", "ground": [0x202, 0x203],
                         "under": "own", "reuse_in": frozenset(), "pieces": museum_1f()},
        },
        {
            "name": "museum_2f",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_LILYCOVE_MUSEUM_2F", "ground": [0x268, 0x269],
                         "under": "own", "reuse_in": frozenset(), "pieces": museum_2f()},
        },
        {
            "name": "harbor",
            # no shaded floor tiles named: their greys are the railings' (as the stations')
            "interior": {"layout": "LAYOUT_HARBOR", "ground": [0x202],
                         "under": "own", "reuse_in": frozenset(), "pieces": harbor()},
        },
        {
            "name": "cycling_gate",
            "interior": {"layout": "LAYOUT_ROUTE110_SEASIDE_CYCLING_ROAD_ENTRANCE", "ground": [0x268],
                         "under": "own", "reuse_in": frozenset(), "pieces": cycling_gate()},
        },
        {
            "name": "cable_car_station",
            # no shaded floor tiles named: the platform's greys are the railings' too, and a shade
            # tile's colours are the floor's wherever they run on from it (no rail claimed a pixel)
            "interior": {"layout": "LAYOUT_CABLE_CAR_STATION", "ground": [0x2F0],
                         "under": "own", "reuse_in": frozenset(), "pieces": cable_car_station()},
        },
        {
            "name": "tent_lobby",
            "interior": {"layout": "LAYOUT_BATTLE_TENT_LOBBY", "ground": [0x210, 0x211],
                         "under": "own", "reuse_in": frozenset(), "pieces": tent_lobby()},
        },
        {
            "name": "contest_lobby",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_CONTEST_LOBBY", "ground": [0x260],
                         "under": "own", "reuse_in": frozenset(), "pieces": contest_lobby()},
        },
        {
            "name": "day_care",
            "interior": {"layout": "LAYOUT_ROUTE117_POKEMON_DAY_CARE", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": day_care()},
        },
        {
            "name": "safari_entrance",
            "interior": {"layout": "LAYOUT_ROUTE121_SAFARI_ZONE_ENTRANCE", "ground": [0x268],
                         "under": "own", "reuse_in": frozenset(), "pieces": safari_entrance()},
        },
        {
            "name": "tent_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_TENT_CORRIDOR", "ground": [0x23A, 0x23B],
                         "under": "own", "reuse_in": frozenset(), "pieces": tent_corridor()},
        },
        {
            "name": "battle_lift",
            "interior": {"layout": "LAYOUT_BATTLE_ELEVATOR", "ground": [0x265],
                         "under": "own", "reuse_in": frozenset(), "pieces": lift(112)},
        },
        {
            "name": "store_lift",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_DEPARTMENT_STORE_ELEVATOR", "ground": [0x265],
                         "under": "own", "reuse_in": frozenset(), "pieces": lift(96)},
        },
        {
            "name": "tower_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_CORRIDOR", "ground": [0x200, 0x229],
                         "under": "own", "reuse_in": frozenset(),
                         "pieces": tower_corridor((96, 0, 112, 32), (0, 256))},
        },
        {
            "name": "tower_multi_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_MULTI_CORRIDOR",
                         "ground": [0x200, 0x229], "under": "own", "reuse_in": frozenset(),
                         "pieces": tower_corridor((64, 0, 80, 32), ())},
        },
        {
            "name": "oceanic_museum_1f",
            "interior": {"layout": "LAYOUT_SLATEPORT_CITY_OCEANIC_MUSEUM_1F", "ground": [0x201, 0x202],
                         "under": "own", "reuse_in": frozenset(), "pieces": oceanic_museum_1f()},
        },
        {
            "name": "oceanic_museum_2f",
            "interior": {"layout": "LAYOUT_SLATEPORT_CITY_OCEANIC_MUSEUM_2F", "ground": [0x201, 0x202],
                         "under": "own", "reuse_in": frozenset(), "pieces": oceanic_museum_2f()},
        },
        {
            "name": "tower_multi_partner_room",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_MULTI_PARTNER_ROOM", "ground": [0x200],
                         "under": "own", "reuse_in": frozenset(), "pieces": tower_multi_partner_room()},
        },
        {
            "name": "arena_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_ARENA_CORRIDOR", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": arena_corridor()},
        },
        {
            "name": "dome_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_DOME_CORRIDOR", "ground": [0x340, 0x341],
                         "under": "own", "reuse_in": frozenset(), "pieces": dome_corridor()},
        },
        {
            "name": "league_1f",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_POKEMON_LEAGUE_1F", "ground": [0x240],
                         "under": "own", "reuse_in": frozenset(), "pieces": league_1f()},
        },
        {
            "name": "dome_lobby",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_DOME_LOBBY", "ground": [0x340, 0x341],
                         "under": "own", "reuse_in": frozenset(), "pieces": dome_lobby()},
        },
        {
            "name": "palace_corridor",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_PALACE_CORRIDOR", "ground": [0x201],
                         "under": "own", "reuse_in": frozenset(), "pieces": palace_corridor()},
        },
        {
            "name": "slateport_fan_club",
            "interior": {"layout": "LAYOUT_SLATEPORT_CITY_POKEMON_FAN_CLUB", "ground": [0x201, 0x202, 0x231],
                         "shade": [0x204, 0x205, 0x228, 0x234], "under": "own", "reuse_in": frozenset(),
                         "pieces": slateport_fan_club()},
        },
        {
            "name": "trainer_fan_club",
            "interior": {"layout": "LAYOUT_LILYCOVE_CITY_POKEMON_TRAINER_FAN_CLUB", "ground": [0x223],
                         "under": "own", "reuse_in": frozenset(), "pieces": trainer_fan_club()},
        },
        {
            "name": "shipyard_1f",
            "interior": {"layout": "LAYOUT_SLATEPORT_CITY_STERNS_SHIPYARD_1F", "ground": [0x202, 0x229, 0x228],
                         "under": "own", "reuse_in": frozenset(), "pieces": shipyard_1f()},
        },
        {
            "name": "shipyard_2f",
            "interior": {"layout": "LAYOUT_SLATEPORT_CITY_STERNS_SHIPYARD_2F", "ground": [0x202],
                         "under": "own", "reuse_in": frozenset(), "pieces": shipyard_2f()},
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
