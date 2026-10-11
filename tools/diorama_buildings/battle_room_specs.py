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



# ── The other rooms battles take place in, read off their cells ───────────
#
# battle_room_auto.py's first reading (walls, edges, furniture), for every room a battle can
# take place in that no spec models piece by piece - the Gyms of Mauville, Lavaridge,
# Fortree, Mossdeep and Sootopolis, the Battle Frontier's and the Battle Tents' rooms, Team
# Aqua's hideout, Mt. Pyre, Sky Pillar, Mirage Tower, the Trick House, the ships, the Space
# Center and the Weather Institute, the link rooms. Only the rooms whose models were proved
# against their drawing (gen_voxel_buildings.py --verify: every model exact, the room composed
# with every model standing in it the drawing again) are listed; the generated floors (Trainer
# Hill, the Battle Pyramid), the secret bases (their decorations change them) and the rooms
# that are not proved yet (Lavaridge Gym B1F, Space Center 2F, Mt. Pyre 1F-4F, Trick House 1)
# are not. tools/battle_env/README.md says how the list is checked.

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
    "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE8",
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
    ] + [spec for spec in (_auto("auto_" + layout[len("LAYOUT_"):].lower(), layout)
                           for layout in AUTO_ROOMS) if spec is not None]


def _auto(name, layout):
    import battle_room_auto
    return battle_room_auto.auto_room_spec(name, layout)

