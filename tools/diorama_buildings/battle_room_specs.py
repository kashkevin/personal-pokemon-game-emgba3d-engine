#!/usr/bin/env python3
"""Rooms where battles happen, modelled from their own drawing (battle-environment overhaul).

The same kind of model as voxel_building_specs.py's rooms (gen_voxel_buildings.interior_specs:
a room cut into pieces, each read off the pixels it claims, reproducing the drawing exactly in
the GBA's projection), for the rooms the 3D battles take place in and that had none: their back
walls stand up, the rooms are closed at the sides, the furniture has height. They are ordinary
entries of the shared model library (voxel_building_specs.SPECS takes them at its end), drawn by
the same building renderer in the field and in a battle - no model is made for battles only.

Conventions, as in voxel_building_specs.py: shapes are pixels of the room's drawing; a piece's
`height` is the rows of its front, standing at its drawn foot; the rest of what it claims above
that is its top, laid back at 45 degrees; `fill` = P fills what pieces in front hide from the
piece's own pixels P columns away; `side` is the stretch of drawing that dresses the faces the
drawing never shows; side walls run ((x, z), (x, z)) facing into the room (west walls front to
back, east walls back to front). Front (south) walls are never stood up: the GBA's camera, the
field's and the battle's look at the room from the south.
"""


def _helpers():
    """voxel_building_specs' piece helpers, imported when the specs are made (that module takes
    this one's specs at its end: importing it at the top would be circular)."""
    import voxel_building_specs as v
    return v.facet, v.own_shell, v.piece


# ── The Elite Four's rooms ────────────────────────────────────────────────
#
# Sidney's, Phoebe's, Glacia's and Drake's rooms are one room in four colours (13 x 14 cells).
# From the top of the drawing:
#
#     rows   0-  8  the back wall's top (the light band)
#     rows   8- 48  its face: panels with red lamps, the door to the next room in the middle,
#                   the arena's spotlights along its foot
#     rows  48- 64  the floor's dark border along the wall
#     rows  66-148  at either side (columns 0-1 and 11-12, collision rows 4-8) a long low case,
#                   its contents seen from above: Sidney's tiered seats, Phoebe's mossy stones,
#                   Glacia's ice, Drake's spikes
#     rows 166-224  the front wall's top and face, the way in - never stood up
#
# The back wall stands 40 rows (two and a half cells) on row 48, its top band laid back over
# rows 0-8; the cases are boxes 12 rows high, their contents their tops, running back to where
# their collision starts; the room's sides, which the drawing never shows, are walls of the back
# wall's own panels from the back wall to the front.

E4_W, E4_H = 13 * 16, 14 * 16
E4_WALL_SIDE = (32, 8, 48, 48)     # a stretch of plain panel: lamp post, panels, a spotlight


def elite_four_room():
    facet, own_shell, piece = _helpers()
    case_l = piece("case_w", [(0, 64, 32, 152)], 12, back=64, solid=True,
                   side=(4, 136, 20, 148))
    case_r = piece("case_e", [(176, 64, 208, 152)], 12, back=64, solid=True,
                   side=(188, 136, 204, 148))
    return own_shell([
        case_l,
        case_r,
        piece("wall", [(0, 0, E4_W, 48)], 40, fill=32, foot=48, side=E4_WALL_SIDE),
        piece("side_w", [], 40, side=E4_WALL_SIDE, walls=[((0, 176), (0, 48))]),
        piece("side_e", [], 40, side=E4_WALL_SIDE, walls=[((E4_W, 48), (E4_W, 176))]),
    ])


# ── The Champion's room ───────────────────────────────────────────────────
#
# Wallace's hall (13 x 13 cells): an octagon. From the top of the drawing:
#
#     rows   0- 64  the back wall between the corners, x 48-160: glass panels on a grid, the
#                   door to the Hall of Fame in the middle; a light band is its top
#     rows  40- 80  under the door, a flight of three steps up to it (x 80-128)
#     corners       the wall turns 45 degrees at both back corners (x 0-48 and 160-208), its top
#                   drawn from (48, 0) down to (0, 48), its foot from (48, 64) to (0, 112),
#                   and runs on south along the room's sides - which the drawing shows only as
#                   the glass at its edges
#     rows  64-160  the raised octagonal floor the battle is fought on, its rim, the stairs up
#                   to it at the front (rows 160-208) - left as the floor
#
# The wall stands 60 rows (its face; the band over rows 0-4 is its top), the corners as
# chamfered walls of the same height, the sides on from them to the front; the steps are a
# block of 24 rows with their treads as its top.

CHAMP_W, CHAMP_H = 13 * 16, 13 * 16
CHAMP_SIDE = (64, 8, 80, 64)       # a stretch of the back wall's glass panels


def champion_room():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("steps", [(80, 40, 128, 80)], 24, solid=True, side=(84, 56, 100, 80)),
        facet("corner_w", (0, 48, 112), (48, 0, 64), 64,
              walls=[((0, CHAMP_H), (0, 112))], side=CHAMP_SIDE, cells=[(0, 12)]),
        facet("corner_e", (160, 0, 64), (208, 48, 112), 64,
              walls=[((CHAMP_W, 112), (CHAMP_W, CHAMP_H))], side=CHAMP_SIDE, cells=[(12, 12)]),
        piece("wall", [(48, 0, 160, 64)], 60, fill=16, foot=64, side=CHAMP_SIDE),
    ])



# ── The Battle Frontier's battle halls ────────────────────────────────────
#
# The Battle Dome's hall (20 x 10 cells) and the Battle Factory's (13 x 12) are drawn as one
# picture each and have no collision (the player never walks in them), so their cells say
# nothing about walls: modelled by hand, as the Champion's room.
#
# The Dome, from the top of the drawing:
#     rows  0- 40  the audience's seats, an aisle every 64 columns
#     rows 40- 48  the rail in front of them
#     rows 48- 64  the BATTLE FRONTIER banner
#     rows 64-160  the arena floor and the two trainers' stands - left as the floor
# The banner and the rail stand 24 rows on row 64; the seats are their top, laid back at 45
# degrees - the stand rising away from the arena; side walls of the banner close the hall.
#
# The Factory, from the top:
#     rows  0- 40  the back wall: white pillars, yellow windows, the dark door in the middle,
#                  the red and navy bands along its foot
#     corners      the wall turns 45 degrees at both back corners (x 0-16 and 192-208), its
#                  foot from (16, 40) down to (0, 56), its bands drawn along it
#     rows 40-192  the hexagon floor and the arena - left as the floor
# The wall stands its whole 40 rows, the corners as chamfered walls of the same height, the
# sides on from them to the front.

DOME_W, DOME_H = 20 * 16, 10 * 16
DOME_SIDE = (0, 40, 16, 64)        # the rail and the banner's end
FACTORY_W, FACTORY_H = 13 * 16, 12 * 16
FACTORY_SIDE = (16, 0, 32, 40)     # a pillar


# Verdanturf's Battle Tent hall (13 x 9 cells, no collision either): the tent's back wall
# across rows 0-32 - its canvas between red trims, the doorway at its east end - on the grass.
# It stands its 32 rows; side walls of its canvas close the hall.
VERD_W, VERD_H = 13 * 16, 9 * 16
VERD_SIDE = (16, 0, 32, 32)        # a stretch of the canvas


def verdanturf_tent_hall():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("wall", [(0, 0, VERD_W, 32)], 32, fill=16, foot=32, side=VERD_SIDE),
        piece("side_w", [], 32, side=VERD_SIDE, walls=[((0, VERD_H), (0, 32))]),
        piece("side_e", [], 32, side=VERD_SIDE, walls=[((VERD_W, 32), (VERD_W, VERD_H))]),
    ])


def dome_hall():
    facet, own_shell, piece = _helpers()
    return own_shell([
        piece("wall", [(0, 0, DOME_W, 64)], 24, fill=16, foot=64, side=DOME_SIDE),
        piece("side_w", [], 24, side=DOME_SIDE, walls=[((0, DOME_H), (0, 64))]),
        piece("side_e", [], 24, side=DOME_SIDE, walls=[((DOME_W, 64), (DOME_W, DOME_H))]),
    ])


def factory_hall():
    facet, own_shell, piece = _helpers()
    return own_shell([
        facet("corner_w", (0, 16, 56), (16, 0, 40), 40,
              walls=[((0, FACTORY_H), (0, 56))], side=FACTORY_SIDE, cells=[(0, 11)]),
        facet("corner_e", (192, 0, 40), (208, 16, 56), 40,
              walls=[((FACTORY_W, 56), (FACTORY_W, FACTORY_H))], side=FACTORY_SIDE, cells=[(12, 11)]),
        piece("wall", [(16, 0, 192, 40)], 40, fill=16, foot=40, side=FACTORY_SIDE),
    ])


# ── The other rooms battles take place in, read off their cells ───────────
#
# battle_room_auto.py's first reading (walls, edges, furniture), for every room a battle can
# take place in that no spec models piece by piece - the Gyms of Mauville, Lavaridge,
# Fortree, Mossdeep and Sootopolis, the Battle Frontier's and the Battle Tents' rooms, Team
# Aqua's hideout, Mt. Pyre, Sky Pillar, Mirage Tower, the Trick House, the ships, the Space
# Center and the Weather Institute, the link rooms. Only the rooms whose models were proved
# against their drawing (gen_voxel_buildings.py --verify: every model exact, the room composed
# with every model standing in it the drawing again) are listed; the generated floors (Trainer
# Hill, the Battle Pyramid), the secret bases (their decorations change them), the places that
# are outdoors in all but name (Southern Island, Faraway Island, Birth Island: the field's trees
# and props are theirs), Team Magma's hideout (a volcano's rock, not a room's walls) and the
# rooms that are not proved (Lavaridge Gym B1F, Trick House 8: cells no piece claims differ)
# are not. tools/battle_env/README.md says how
# the list is checked (tools/battle_env/auto_rooms.py).

AUTO_ROOMS = [
    "LAYOUT_AQUA_HIDEOUT_1F",
    "LAYOUT_AQUA_HIDEOUT_B1F",
    "LAYOUT_AQUA_HIDEOUT_B2F",
    "LAYOUT_BATTLE_COLOSSEUM_2P",
    "LAYOUT_BATTLE_COLOSSEUM_4P",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_ARENA_BATTLE_ROOM",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_DOME_PRE_BATTLE_ROOM",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_PRE_BATTLE_ROOM",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PALACE_BATTLE_ROOM",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_FINAL",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_BATTLE_ROOM",
    "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_LOBBY",
    "LAYOUT_BATTLE_TENT_BATTLE_ROOM",
    "LAYOUT_FORTREE_CITY_GYM",
    "LAYOUT_LAVARIDGE_TOWN_GYM_1F",
    "LAYOUT_MAUVILLE_CITY_GYM",
    "LAYOUT_MIRAGE_TOWER_1F",
    "LAYOUT_MIRAGE_TOWER_2F",
    "LAYOUT_MIRAGE_TOWER_3F",
    "LAYOUT_MIRAGE_TOWER_4F",
    "LAYOUT_MOSSDEEP_CITY_GYM",
    "LAYOUT_MOSSDEEP_CITY_SPACE_CENTER_1F",
    "LAYOUT_MT_PYRE_5F",
    "LAYOUT_MT_PYRE_6F",
    "LAYOUT_ROUTE109_SEASHORE_HOUSE",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE2",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE3",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE4",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE6",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE7",
    "LAYOUT_ROUTE119_WEATHER_INSTITUTE_1F",
    "LAYOUT_ROUTE119_WEATHER_INSTITUTE_2F",
    "LAYOUT_SKY_PILLAR_1F",
    "LAYOUT_SKY_PILLAR_3F",
    "LAYOUT_SKY_PILLAR_5F",
    "LAYOUT_SLATEPORT_CITY_OCEANIC_MUSEUM_2F",
    "LAYOUT_SOOTOPOLIS_CITY_GYM_1F",
    "LAYOUT_SOOTOPOLIS_CITY_GYM_B1F",
    "LAYOUT_SOOTOPOLIS_CITY_MYSTERY_EVENTS_HOUSE_B1F",
    "LAYOUT_SS_TIDAL_LOWER_DECK",
    "LAYOUT_SS_TIDAL_ROOMS",
    "LAYOUT_UNION_ROOM",
    # proved after the side walls learnt to stop short of another floor and with the pieces
    # AUTO_DROP leaves out; after the rooms above, whose pieces they reuse (Mt. Pyre's lower
    # floors stand the graves modelled on 5F): the order is the proof's
    "LAYOUT_MT_PYRE_1F",
    "LAYOUT_MT_PYRE_2F",
    "LAYOUT_MT_PYRE_3F",
    "LAYOUT_MT_PYRE_4F",
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE1",
    "LAYOUT_NEW_MAUVILLE_ENTRANCE",
    "LAYOUT_NEW_MAUVILLE_INSIDE",
    "LAYOUT_ABANDONED_SHIP_CORRIDORS_1F",
    "LAYOUT_ABANDONED_SHIP_ROOMS_1F",
    "LAYOUT_ABANDONED_SHIP_CORRIDORS_B1F",
    "LAYOUT_ABANDONED_SHIP_ROOMS_B1F",
    "LAYOUT_ABANDONED_SHIP_ROOMS2_1F",
    "LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_CORRIDORS",
    "LAYOUT_MOSSDEEP_CITY_SPACE_CENTER_2F",
]


def battle_room_specs():
    """The specs, made when voxel_building_specs asks for them (its SPECS end with them)."""
    return [
        {
            "name": "e4_sidney",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_SIDNEYS_ROOM", "ground": [0x22C],
                         "pieces": elite_four_room()},
        },
        {
            "name": "e4_phoebe",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_PHOEBES_ROOM", "ground": [0x25C],
                         "pieces": elite_four_room()},
        },
        {
            "name": "e4_glacia",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_GLACIAS_ROOM", "ground": [0x28C],
                         "pieces": elite_four_room()},
        },
        {
            "name": "e4_drake",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_DRAKES_ROOM", "ground": [0x2BC],
                         "pieces": elite_four_room()},
        },
        {
            "name": "champion",
            "interior": {"layout": "LAYOUT_EVER_GRANDE_CITY_CHAMPIONS_ROOM", "ground": [0x313],
                         "pieces": champion_room()},
        },
        {
            "name": "frontier_dome_hall",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_DOME_BATTLE_ROOM",
                         "ground": [0x309], "pieces": dome_hall()},
        },
        {
            "name": "tent_verdanturf_hall",
            "interior": {"layout": "LAYOUT_VERDANTURF_TOWN_BATTLE_TENT_BATTLE_ROOM",
                         "ground": [0x1], "pieces": verdanturf_tent_hall()},
        },
        {
            "name": "frontier_factory_hall",
            "interior": {"layout": "LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_BATTLE_ROOM",
                         "ground": [0x2a2], "pieces": factory_hall()},
        },
    ] + [spec for spec in (_auto("auto_" + layout[len("LAYOUT_"):].lower(), layout)
                           for layout in AUTO_ROOMS) if spec is not None]


# Pieces of an auto room left out because the proof found they do not give back their
# drawing (a few pixels of another floor under them, an edge whose top the room's black cuts):
# the rest of the room stands. tools/battle_env/README.md says how they are found.
AUTO_DROP = {
    "LAYOUT_ABANDONED_SHIP_CORRIDORS_1F": ["side_w", "thing_0_8", "thing_17_9", "side_e", "thing_0_4"],
    "LAYOUT_ABANDONED_SHIP_CORRIDORS_B1F": ["thing_12_6", "side_w"],
    "LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_CORRIDORS": ["thing_0_3", "thing_0_5", "thing_12_4", "side_e"],
    "LAYOUT_MOSSDEEP_CITY_SPACE_CENTER_2F": ["thing_1_4", "side_w"],
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE1": [
        "thing_10_10", "thing_11_13", "thing_11_17", "thing_11_9", "thing_12_14", "thing_12_18",
        "thing_12_6", "thing_14_18", "thing_14_6", "thing_1_6", "thing_1_8", "thing_2_10",
        "thing_2_13", "thing_2_5", "thing_4_14", "thing_4_6", "thing_4_9", "thing_5_10",
        "thing_6_14", "thing_6_5", "thing_7_9", "thing_8_11", "thing_8_5", "thing_9_18"],
}


def _auto(name, layout):
    import battle_room_auto
    return battle_room_auto.auto_room_spec(name, layout, AUTO_DROP.get(layout, ()),
                                           reuse_in=AUTO_ROOMS)

