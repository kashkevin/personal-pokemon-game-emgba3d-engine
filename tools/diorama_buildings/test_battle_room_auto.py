#!/usr/bin/env python3
"""Tests for battle_room_auto: its behaviour names (KNOWN_ISSUES MS-66) and its side walls (MS-232).

The reader counted a name that a comment in include/constants/metatile_behaviors.h mentions as a value of
its own (MB_INTERIOR_DEEP_WATER's comment names MB_DEEP_WATER), so every behaviour from MB_WATERFALL on read
as the one before it: ice as a Mart's sign, sand as ice, a Strength button as seaweed, a cracked floor's hole
as an arrow warp. Only the Sootopolis Gym 1F's reading would have changed with the names right (its floor is
ice); an ice floor is the room's floor, so it reads as it did."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_room_auto as bra  # noqa: E402
import voxel_building as vb  # noqa: E402
import voxel_cells  # noqa: E402


class BehaviourNamesTests(unittest.TestCase):
    def test_every_name_has_the_value_the_enum_gives_it(self):
        names = bra._behaviour_names()
        self.assertEqual({name: value for value, name in enumerate(names)},
                         {name: value for name, value in voxel_cells.MB.items() if name.startswith("MB_")})

    def test_the_values_after_the_comment(self):
        names = bra._behaviour_names()
        for name in ("MB_INTERIOR_DEEP_WATER", "MB_DEEP_WATER", "MB_WATERFALL", "MB_ICE", "MB_SAND",
                     "MB_STRENGTH_BUTTON", "MB_THIN_ICE", "MB_CRACKED_FLOOR_HOLE", "MB_SOUTH_ARROW_WARP"):
            self.assertEqual(names[voxel_cells.MB[name]], name)

    def test_ice_is_a_floor_and_water_is_not(self):
        for name in ("MB_ICE", "MB_THIN_ICE", "MB_CRACKED_ICE"):
            self.assertTrue(bra.LEVEL.match(name) and bra.ICE_FLOOR.match(name), name)
        for name in ("MB_POND_WATER", "MB_DEEP_WATER", "MB_SHALLOW_WATER", "MB_PUDDLE", "MB_WATERFALL",
                     "MB_CRACKED_FLOOR_HOLE"):
            self.assertTrue(bra.LEVEL.match(name), name)
            self.assertIsNone(bra.ICE_FLOOR.match(name), name)
        for name in ("MB_SAND", "MB_STRENGTH_BUTTON", "MB_POKEMART_SIGN", "MB_SPIN_RIGHT"):
            self.assertIsNone(bra.LEVEL.match(name), name)


class SootopolisGymTests(unittest.TestCase):
    def test_its_ice_is_the_floor(self):
        # the thin ice (0x20D) is the room's ground; the dark tiles beyond its walls are not
        ground, pieces, notes = bra.analyse("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        self.assertEqual(ground, [0x20D])
        self.assertTrue(pieces)


class SideWallTests(unittest.TestCase):
    """KNOWN_ISSUES MS-232: a room's side wall closes its floor at the map's edge. It ran from the
    shallowest back wall's foot through the outside and the faces of the walls in its column: in
    the Mauville Gym it stood in the black either side of the leader's alcove, its cells drawn
    with the floor's yellow; in the Mossdeep Gym down 19 rows of black; in the Battle Tower's
    battle room from inside the back wall, its far end showing over it."""

    ROOMS = ("LAYOUT_MAUVILLE_CITY_GYM", "LAYOUT_MOSSDEEP_CITY_GYM",
             "LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_BATTLE_ROOM", "LAYOUT_MT_PYRE_5F")

    @staticmethod
    def column(layout_id, x):
        """Each row of column x: 'void', 'floor' or 'blocked', as the reader reads them."""
        L = vb.LayoutArt(layout_id)
        cache, out = {}, []
        for y in range(L.h):
            m = L.blocks[y * L.w + x] & 0x3FF
            px = bra._cell_pixels(L, m, cache)
            dark = sum(1 for p in px if p[3] < 128
                       or (p[0] <= bra.DARK and p[1] <= bra.DARK and p[2] <= bra.DARK + 8))
            blocked = (L.blocks[y * L.w + x] >> 10) & 3 != 0
            out.append("void" if dark >= 0.9 * len(px) else "blocked" if blocked else "floor")
        return L, out

    def test_side_walls_close_floor_and_stand_on_no_outside(self):
        for layout_id in self.ROOMS:
            ground, pieces, notes = bra.analyse(layout_id)
            for pc in pieces:
                if pc["name"] not in ("side_w", "side_e"):
                    continue
                (xa, za), (xb, zb) = pc["walls"][0][:2]
                L, col = self.column(layout_id, 0 if xa == 0 else vb.LayoutArt(layout_id).w - 1)
                rows = range(min(za, zb) // 16, max(za, zb) // 16)
                with self.subTest(room=layout_id, wall=pc["name"], rows=(rows.start, rows.stop)):
                    self.assertNotIn("void", [col[y] for y in rows])
                    self.assertIn("floor", [col[y] for y in rows])
                    # not from inside a wall's face: the outside, or nothing but blocked cells
                    # under it up to the outside, above its first row
                    above = col[:rows.start]
                    while above and above[-1] == "blocked":
                        above.pop()
                    self.assertFalse(col[rows.start] == "blocked" and (not above or above[-1] == "void"))

    def test_the_mauville_gym(self):
        ground, pieces, notes = bra.analyse("LAYOUT_MAUVILLE_CITY_GYM")
        walls = {pc["name"]: pc["walls"][0][:2] for pc in pieces if pc["name"].startswith("side_")}
        # the floor reaches the edges only below the alcove's wings; from the foot of the wall in each edge
        # column (rows 9 and 8), over floor shaded at that foot, on its cells' own drawing (SideWallFromTheFootTests)
        self.assertEqual(walls, {"side_w": ((0, 240), (0, 144)), "side_e": ((160, 128), (160, 160))})
        own = {pc["name"]: pc["own_floor"] for pc in pieces if pc["name"].startswith("side_")}
        self.assertEqual(own, {"side_w": True, "side_e": True})


class FloorShadeTests(unittest.TestCase):
    """KNOWN_ISSUES MS-227: the floor cell north of a piece of furniture was taken as the piece's drawing
    going on wherever it differed from the floor - also where it is only the floor shaded in its own
    colours: the Fortree Gym's lone hedges stood 20 rows tall, the shaded floor laid on their tops, and
    four of the Sootopolis Gym's ice rocks stood as blocks topped with the ice's highlight beside two
    others standing 8 rows."""

    @staticmethod
    def things(layout_id):
        ground, pieces, notes = bra.analyse(layout_id)
        return {pc["name"]: pc for pc in pieces if pc["name"].startswith("thing_")}

    @staticmethod
    def cells(pc):
        return sorted({(x0 // 16, y0 // 16) for (x0, y0, x1, y1) in pc["shape"]})

    def test_the_fortree_gyms_lone_hedges(self):
        things = self.things("LAYOUT_FORTREE_CITY_GYM")
        for x, y in ((8, 13), (6, 17), (3, 18)):
            pc = things["thing_%d_%d" % (x, y)]
            with self.subTest(hedge=(x, y)):
                self.assertEqual(self.cells(pc), [(x, y)])
                self.assertEqual(pc["height"], 8)

    def test_the_sootopolis_gyms_rocks_stand_alike(self):
        # its ten ice rocks (0x203, and 0x216 on the ice's highlight) each stand alone, 8 rows: the
        # two at the foot of a ledge (ALONE) and the two behind a ledge, on its edge (11, 9 and 9, 19),
        # too - read with the ledge's column as one thing, those lay back as its top, bent over the
        # drop from the 3D cameras (the object audit's class 5; THING_FRONT); the ledge stands without
        # them
        things = self.things("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        L = vb.LayoutArt("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        rocks = [(x, y) for y in range(L.h) for x in range(L.w)
                 if L.blocks[y * L.w + x] & 0x3FF in (0x203, 0x216)]
        self.assertEqual(len(rocks), 10)
        owner = {cell: name for name, pc in things.items() for cell in self.cells(pc)}
        for x, y in ((11, 9), (9, 19)):
            ledge = things[owner[(x, y + 1)]]
            self.assertNotIn((x, y), self.cells(ledge))
            self.assertEqual(ledge["height"], 20)
        for x, y in rocks:
            pc = things["thing_%d_%d" % (x, y)]
            with self.subTest(rock=(x, y)):
                self.assertEqual(self.cells(pc), [(x, y)])
                self.assertEqual(pc["height"], 8)

    def test_the_ledges_behind_the_two_rocks_still_stand(self):
        # the ledge's column behind each rock at its foot stands on the ledge's own foot: with the
        # columns beside it (5, 5), or, the ledge's end by the stairs, alone (7, 15-16), not left
        # flat for the rock taking the floor in front of it
        things = self.things("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        owner = {cell: name for name, pc in things.items() for cell in self.cells(pc)}
        self.assertEqual(owner[(5, 5)], owner[(4, 5)])
        end = things[owner[(7, 16)]]
        self.assertEqual(self.cells(end), [(7, 15), (7, 16)])
        self.assertEqual(end["height"], 20)

    def test_a_drawing_running_on_is_still_the_pieces(self):
        # the Aqua Hideout 1F's machine at (2, 16): its top drawn on over the floor row behind it
        pc = self.things("LAYOUT_AQUA_HIDEOUT_1F")["thing_2_16"]
        self.assertIn((2, 15), self.cells(pc))
        self.assertIn((3, 15), self.cells(pc))
        self.assertEqual(pc["height"], 20)

    def test_a_stool_behind_a_table_stays_on_the_floor(self):
        # the object audit of 2026-11-14: the Space Center 1F's east plan tables took the two stools
        # behind them (10, 6 and 13, 6) as their drawing, and stood them on their tops (APART)
        things = self.things("LAYOUT_MOSSDEEP_CITY_SPACE_CENTER_1F")
        tables = things["thing_10_7"]
        self.assertEqual(self.cells(tables), [(x, y) for x in range(10, 14) for y in (7, 8)])
        self.assertEqual(self.cells(things["thing_2_5"]), [(x, y) for x in range(2, 6) for y in (5, 6)])
        self.assertEqual(tables["height"], things["thing_2_5"]["height"])
        stools = {(10, 6), (13, 6)}
        self.assertFalse(stools & {cell for pc in things.values() for cell in self.cells(pc)})
        # the machines on the desks north of the stools still take their drawing running on
        self.assertIn((10, 4), self.cells(things["thing_10_5"]))


class FactoryHallTests(unittest.TestCase):
    """KNOWN_ISSUES MS-227: the Battle Factory hall's side walls repeat the stretch of the back wall
    `FACTORY_SIDE` names. It was a whole pillar, whose red band bends up round the pillar's edges and
    whose edges are grey: repeated along a side wall, a striped wall with a sawtooth for a band."""

    def test_the_side_walls_stretch_repeats_without_a_seam(self):
        import battle_room_specs as brs
        L = vb.LayoutArt("LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_BATTLE_ROOM")
        x0, y0, x1, y1 = brs.FACTORY_SIDE

        def pixel(x, y):
            m = L.blocks[(y // 16) * L.w + x // 16] & 0x3FF
            return L.cell_image(m).convert("RGBA").getpixel((x % 16, y % 16))[:3]
        columns = {tuple(pixel(x, y) for y in range(y0, y1)) for x in range(x0, x1)}
        self.assertEqual(len(columns), 1)
        # and it carries the band: the dark red of the back wall's pillars, straight across
        self.assertIn((189, 41, 16), next(iter(columns)))

    def test_its_pieces_stand_on_the_cells_own_drawing(self):
        # KNOWN_ISSUES MS-227: under a piece's cells the game lays its placement's ground; the
        # hall's (0x2a2) holds part of the arena's orange ring, which showed in hairlines at the
        # edges of the pieces' floor decals - orange specks on the white tiles along the walls.
        # Each cell's own drawing (OWN_GROUND) is what the decals draw over it.
        import expansion_assets
        import gen_voxel_buildings as gen
        models = gen.build_models(only="frontier_factory_hall")
        self.assertTrue(models)
        layouts = expansion_assets.emerald_layouts(vb.ROOT)
        grounds = {p[3] for m in models for p in gen.find_placements(m, layouts)}
        self.assertEqual(grounds, {gen.OWN_GROUND})


class PalaceBattleRoomTests(unittest.TestCase):
    """The Battle Palace's battle room (the 3D check of 2026-11-11): its back wall has no wall piece,
    the room's own drawing lays it back; its outer pillars read as walls a row deeper, upright before
    it, and the door's posts a row shallower, upright behind it. Between those and the laid-back wall
    the outside showed: black wedges beside the pillars and the door, in the field and in battles."""

    LAYOUT = "LAYOUT_BATTLE_FRONTIER_BATTLE_PALACE_BATTLE_ROOM"

    def test_the_reader_still_finds_the_upright_pieces(self):
        # what the drop list leaves out: the pieces the reader finds without it (a change to the
        # reader that stops finding them would leave this list stale, not the room wrong)
        ground, pieces, notes = bra.analyse(self.LAYOUT)
        walls = {pc["name"]: pc["foot"] for pc in pieces if pc["name"].startswith("wall_")}
        # the outer pillars stand on their pedestals' row (48), the door's posts a row behind (32),
        # the middle pillars run down to the arena's foot (112)
        self.assertEqual(walls, {"wall_2_0": 48, "wall_11_0": 48, "wall_12_0": 32, "wall_14_0": 32,
                                 "wall_5_0": 112, "wall_8_0": 112})

    def test_nothing_stands_upright_on_its_back_wall(self):
        import battle_room_specs as brs
        ground, pieces, notes = bra.analyse(self.LAYOUT, drop=brs.AUTO_DROP[self.LAYOUT])
        self.assertEqual(sorted(pc["name"] for pc in pieces), ["side_e", "side_w"])



class DoorInWallTests(unittest.TestCase):
    """KNOWN_ISSUES MS-236: a lift's or a cabin's door in a wall. Its own cell is one the game's door table
    names (field_door.c), so it was no wall's: it lay back with the room's drawing between the upright wall
    on either side of it, the outside black over it (the Battle Tower's lobby, the S.S. Tidal's cabins, the
    Abandoned Ship). Where the wall on both sides makes the same run with the door counted, the door is
    that wall's, and the renderer opens it on the model (FindDoor)."""

    def walls(self, layout_id):
        ground, pieces, notes = bra.analyse(layout_id)
        return ({pc["name"]: pc["shape"][0] for pc in pieces if pc["name"].startswith("wall_")},
                [n for n in notes if n.startswith("door in the wall")])

    def test_the_tower_lobbys_lifts_stand_in_their_wall(self):
        walls, doors = self.walls("LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_LOBBY")
        # each lift (columns 6, 10, 14 and 18) is one wall with the posts either side of it
        for x in (5, 9, 13, 17):
            self.assertEqual(walls["wall_%d_0" % x], (x * 16, 0, (x + 3) * 16, 32))
        self.assertNotIn("wall_7_0", walls)
        self.assertEqual(len(doors), 4)

    def test_a_cabins_door_with_its_dark_top(self):
        # the cabin's doorway black over the door (read as the outside) is the wall's too
        walls, doors = self.walls("LAYOUT_SS_TIDAL_ROOMS")
        # one wall across the cabin: the door's columns and the walls over its two berths (BERTH)
        self.assertEqual(walls["wall_1_0"], (16, 0, 128, 32))
        self.assertEqual(len(doors), 4)

    def test_a_door_whose_frame_a_script_changes_stays_out(self):
        # the Battle Pike's curtain: its frames are set by its scripts (dynamic), so this rule makes no
        # wall of it (it stands by battle_room_auto.STAND instead: PikeCurtainTests)
        for layout_id in PikeCurtainTests.LAYOUTS:
            walls, doors = self.walls(layout_id)
            self.assertEqual(doors, [])


class PikeCurtainTests(unittest.TestCase):
    """KNOWN_ISSUES MS-236's last four doors: the Battle Pike's rooms' curtain in the middle of the back wall.
    Its frames are set by setmetatile when the Pike Queen comes out of it, so none of its cells was solid: it
    lay back with the room's drawing between the wall standing on either side of it, the outside black over
    it. It stands with the wall (battle_room_auto.STAND); the renderer leaves its pieces out while the Queen's
    curtain moves (buildings.c DioramaBuildings_Validate)."""

    LAYOUTS = ("LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL", "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS")

    def test_the_back_wall_stands_across_the_room(self):
        for layout_id in self.LAYOUTS:
            ground, pieces, notes = bra.analyse(layout_id)
            walls = {pc["name"]: pc["shape"][0] for pc in pieces if pc["name"].startswith("wall_")}
            columns = set()
            for x0, y0, x1, y1 in walls.values():
                self.assertEqual(y0, 0, layout_id)
                columns.update(range(x0 // 16, x1 // 16))
            self.assertEqual(columns, set(range(9)), layout_id)
            # the curtain's sides stand to their foot on row 4; its opening, whose foot (4, 3) is passable,
            # a row shallower
            self.assertEqual(walls["wall_3_0"], (48, 0, 64, 64), layout_id)
            self.assertEqual(walls["wall_4_0"], (64, 0, 80, 48), layout_id)
            self.assertEqual(walls["wall_5_0"], (80, 0, 96, 64), layout_id)
            self.assertFalse([n for n in notes if n.startswith("left flat") and n.endswith(", 0) (no front on the floor)")],
                             layout_id)

    def test_only_the_curtain_is_stood_up(self):
        # every cell STAND names is one the game changes, in the curtain's 3 x 3 (its top row the middle cell)
        dynamic = bra.dynamic_metatiles()
        self.assertEqual(set(bra.STAND), set(self.LAYOUTS))
        for layout_id, cells in bra.STAND.items():
            layout = vb.LayoutArt(layout_id)
            for x, y in cells:
                m = layout.blocks[y * layout.w + x] & 0x3FF
                self.assertIn(((layout.primary if m < 512 else layout.secondary), m), dynamic, (layout_id, x, y))
                self.assertTrue(3 <= x <= 5 and 1 <= y <= 3 and (y > 1 or x == 4), (layout_id, x, y))


class LevelWallTests(unittest.TestCase):
    """KNOWN_ISSUES MS-237: the Sootopolis Gym 1F's back wall is drawn 32 rows tall, its waterfalls 48 a row in
    front of it and its side walls as runs down to row 14. Each standing to its own foot at up to MAX_WALL, the
    waterfalls and the side walls stood 48 tall in front of the 32-row wall: from the 3D cameras their tops stood
    over the wall's, the outside black between (a notch over columns 3 and 13, a strip beside each side wall).
    Standing no taller than the back wall (battle_room_auto.LEVEL_WALL), every piece's top meets the wall's: a
    piece standing h rows on its foot lays the rest of its drawing back, so its top's far edge lies at the depth
    of its first drawn row plus h, at height h."""

    LAYOUT = "LAYOUT_SOOTOPOLIS_CITY_GYM_1F"

    def test_the_back_walls_pieces_meet_at_one_height(self):
        ground, pieces, notes = bra.analyse(self.LAYOUT)
        walls = {pc["name"]: pc for pc in pieces if pc["name"].startswith("wall_")}
        # the back wall, its two waterfalls and the side walls, each from the drawing's first row
        for name in ("wall_3_0", "wall_6_0", "wall_13_0", "wall_4_0", "wall_11_0", "wall_1_0", "wall_14_0"):
            pc = walls[name]
            x0, y0, x1, y1 = pc["shape"][0]
            self.assertEqual(y0, 0, name)
            self.assertEqual(pc["height"], 32, name)
            # its top's far edge: depth y0 + height, at that height - the back wall's own (32, 32)
            self.assertEqual((y0 + pc["height"], pc["height"]), (32, 32), name)
        # the waterfalls still stand on row 3 and the side walls on row 14: only their height changed
        self.assertEqual(walls["wall_4_0"]["foot"], 48)
        self.assertEqual(walls["wall_11_0"]["foot"], 48)
        self.assertEqual(walls["wall_1_0"]["foot"], 224)
        self.assertEqual(walls["wall_14_0"]["foot"], 224)

    def test_only_that_room_is_levelled(self):
        self.assertEqual(bra.LEVEL_WALL, {self.LAYOUT: 32})


class StairwellTests(unittest.TestCase):
    """KNOWN_ISSUES MS-237, the owner's decision #56: the Facility tileset's stairwell (Mt. Pyre 1F-5F, Team
    Aqua's hideout, the Space Center) read as walls stood its lintel a row behind its posts and 16 rows tall:
    from the 3D cameras the outside black over it, down the posts' inner edges and, beside a taller side wall,
    in a wedge. By hand: the frame on one foot, its doorway's recess, the taller walls' cheeks - nothing rising
    above the row the walls' tops are drawn on, so at 45 degrees nothing changes; the walls keep their heights."""

    ROOMS = {
        "LAYOUT_MT_PYRE_1F": [(10, 0)], "LAYOUT_MT_PYRE_2F": [(9, 0)], "LAYOUT_MT_PYRE_3F": [(1, 0)],
        "LAYOUT_MT_PYRE_4F": [(9, 0)], "LAYOUT_MT_PYRE_5F": [(1, 0)],
        "LAYOUT_AQUA_HIDEOUT_B1F": [(28, 0)], "LAYOUT_AQUA_HIDEOUT_B2F": [(11, 0), (17, 0), (2, 2)],
        "LAYOUT_MOSSDEEP_CITY_SPACE_CENTER_1F": [(12, 0)],
    }

    @classmethod
    def setUpClass(cls):
        cls.read = {lid: bra.analyse(lid) for lid in cls.ROOMS}

    def test_the_auto_rooms_with_the_stairwell(self):
        import battle_room_specs
        top, low = bra.STAIRWELL["gTileset_Facility"]
        rooms = {}
        for lid in battle_room_specs.AUTO_ROOMS:
            L = vb.LayoutArt(lid)
            if L.secondary != "gTileset_Facility":
                continue
            spots = [(x, y) for y in range(L.h - 1) for x in range(L.w - 2)
                     if all(L.metatile(x + i, y) == top[i] and L.metatile(x + i, y + 1) == low[i] for i in range(3))]
            if spots:
                rooms[lid] = spots
        self.assertEqual(rooms, self.ROOMS)

    def test_each_stands_as_its_frame_and_recess(self):
        for lid, spots in self.ROOMS.items():
            ground, pieces, notes = self.read[lid]
            names = {pc["name"]: pc for pc in pieces}
            for (sx, sy) in spots:
                X, Y = sx * 16, sy * 16
                frame = names["stairwell_frame_%d_%d" % (sx, sy)]
                self.assertEqual(frame["shape"], [(X, Y, X + 48, Y + 12), (X, Y + 12, X + 8, Y + 32),
                                                  (X + 40, Y + 12, X + 48, Y + 32)], lid)
                # the lintel on the posts' foot, not a row back
                self.assertEqual((frame["foot"], frame["height"]), (Y + 32, 32), lid)
                self.assertTrue(frame.get("alone"), lid)
                recess = names["stairwell_%d_%d" % (sx, sy)]
                self.assertEqual(recess["inside"], (X + 8, Y + 12, X + 40, Y + 32), lid)
                # the doorway's drawing is never the room's floor (on 1F two pixels of the flight's bottom
                # row have the floor's colour at the floor's place: the terrain drew them in its other shade)
                self.assertEqual(recess["not_floor"], [(X + 8, Y + 12, X + 40, Y + 32)], lid)
                self.assertTrue(recess.get("alone"), lid)
                # no wall of the reader's stands in the stairwell's columns
                for pc in pieces:
                    if not pc["name"].startswith("wall_"):
                        continue
                    x0, y0, x1, y1 = pc["shape"][0]
                    self.assertFalse(x0 < X + 48 and X < x1 and y0 < Y + 32 and Y < y1, (lid, pc["name"]))

    def test_nothing_rises_above_the_walls_top_row(self):
        for lid, spots in self.ROOMS.items():
            ground, pieces, notes = self.read[lid]
            names = {pc["name"]: pc for pc in pieces}
            for (sx, sy) in spots:
                Y = sy * 16
                for (a, b, h) in names["stairwell_%d_%d" % (sx, sy)]["walls"]:
                    # at 45 degrees a wall's top at depth z, height h lies on row z - h
                    self.assertLessEqual(h, min(a[1], b[1]) - Y, (lid, a, b))
            for pc in pieces:
                for (x, z0, z1, crest, u, facing) in pc.get("cheeks", ()):
                    # beside a stairwell's frame: its left edge for a wall west of it, its right edge east
                    self.assertIn((x // 16 if facing > 0 else x // 16 - 3, crest // 16), spots, (lid, pc["name"]))
                    # from the frame's face to where the wall's top starts, which is on that row too
                    self.assertEqual(z0, crest + 32, (lid, pc["name"]))
                    x0, y0, x1, y1 = pc["shape"][0]
                    self.assertEqual(y0, crest, (lid, pc["name"]))
                    self.assertEqual(z1 - crest, pc["height"], (lid, pc["name"]))
                    self.assertEqual(x, x1 if facing > 0 else x0, (lid, pc["name"]))

    def test_mt_pyre_5f_keeps_its_walls_and_closes_both_wedges(self):
        ground, pieces, notes = self.read["LAYOUT_MT_PYRE_5F"]
        names = {pc["name"]: pc for pc in pieces}
        for name, cheek in (("wall_0_0", (16, 32, 48, 0, 15.5, 1)), ("wall_4_0", (64, 32, 48, 0, 64.5, -1))):
            pc = names[name]
            self.assertEqual((pc["height"], pc["foot"]), (48, 128), name)
            self.assertEqual(pc["cheeks"], [cheek], name)
        # Team Aqua's hideout B1F: the walls beside its stairwell stand on its foot - no cheek
        ground, pieces, notes = self.read["LAYOUT_AQUA_HIDEOUT_B1F"]
        self.assertFalse([pc["name"] for pc in pieces if pc.get("cheeks")])

    def test_a_cheek_lies_on_the_row_and_carries_the_side_on(self):
        for facing in (1, -1):
            mesh = vb.Mesh()
            vb.Cheek("c", 16, 32, 48, 0, 15.5, facing).emit(mesh)
            self.assertEqual(len(mesh.tris), 2)
            for (tri, shade, tag) in mesh.tris:
                self.assertTrue(tag.endswith("~proj"))
                self.assertEqual(shade, vb.SHADE_EAST if facing > 0 else vb.SHADE_WEST)
                for (x, y, z, u, v) in tri:
                    self.assertEqual((x, u, v), (16.0, 15.5, z - y))
                    self.assertTrue(32.0 <= z <= 48.0)
                    self.assertIn(y, (-1.0, z))


if __name__ == "__main__":
    unittest.main()


class BerthTests(unittest.TestCase):
    """The object audit of 2026-11-14 (class 2): the ship cabins' berths - the InsideShip tileset's bed in its
    alcove, two cells by two under the back wall - were read with the back wall's columns as one run four rows
    deep, 48 rows of it standing: the bed, drawn from above, painted upright on the wall's face. By hand (BERTH):
    the wall over each stands to the row its foot is drawn on, as the wall beside it; the berth is a bed 8 rows
    tall in front of it."""

    ROOMS = {
        # (15, 11): the berth with a blue blanket
        "LAYOUT_SS_TIDAL_ROOMS": {(1, 2), (6, 2), (10, 2), (15, 2), (19, 2), (33, 2), (1, 11), (6, 11), (15, 11),
                                  (24, 11), (28, 11)},
        "LAYOUT_ABANDONED_SHIP_ROOMS_1F": {(10, 2), (1, 11), (15, 11)},
        "LAYOUT_ABANDONED_SHIP_ROOMS_B1F": {(1, 2), (6, 2), (10, 2), (19, 2), (24, 2)},
        "LAYOUT_ABANDONED_SHIP_ROOMS2_1F": {(1, 2)},
        "LAYOUT_ABANDONED_SHIP_ROOMS2_B1F": {(1, 2), (10, 2), (15, 2)},
    }

    @classmethod
    def setUpClass(cls):
        cls.read = {lid: bra.analyse(lid) for lid in cls.ROOMS}

    def test_every_berth_in_the_auto_rooms_is_found(self):
        import battle_room_specs
        found = {}
        for lid in battle_room_specs.AUTO_ROOMS:
            L = vb.LayoutArt(lid)
            spots = {(x, y) for tiles in bra.BERTH.get(L.secondary, ()) for y in range(L.h - 1) for x in range(L.w - 1)
                     if (L.metatile(x, y), L.metatile(x + 1, y), L.metatile(x, y + 1), L.metatile(x + 1, y + 1)) == tiles}
            if spots:
                found[lid] = spots
        self.assertEqual(found, self.ROOMS)

    def test_each_is_a_bed_before_the_wall(self):
        for lid, spots in self.ROOMS.items():
            ground, pieces, notes = self.read[lid]
            names = {pc["name"]: pc for pc in pieces}
            for (bx, by) in spots:
                X, Y = bx * 16, by * 16
                with self.subTest(room=lid, berth=(bx, by)):
                    bed = names["berth_%d_%d" % (bx, by)]
                    self.assertEqual(bed["shape"], [(X, Y - bra.BERTH_TALL, X + 32, Y + 32)])
                    self.assertEqual(bed["height"], bra.BERTH_TALL)
                    # its top's far edge, at its height, is the wall's foot: z = row + height
                    self.assertEqual(Y - bra.BERTH_TALL + bed["height"], Y)
                    # the wall over its columns stands on the berth's top row, no taller than the room's
                    walls = [pc for pc in pieces if pc["name"].startswith("wall_")
                             and pc["shape"][0][0] <= X and X + 32 <= pc["shape"][0][2]
                             and pc["shape"][0][1] < Y <= pc["shape"][0][3]]
                    self.assertEqual(len(walls), 1)
                    self.assertEqual(walls[0]["foot"], Y)
                    self.assertLessEqual(walls[0]["height"], 32)
                    # and no other piece takes the berth's cells
                    for pc in pieces:
                        if pc is bed:
                            continue
                        for (x0, y0, x1, y1) in pc["shape"]:
                            self.assertFalse(x0 < X + 32 and X < x1 and y0 < Y + 32 and Y < y1, pc["name"])

    def test_the_bin_at_a_berths_foot_stands_alone(self):
        # the S.S. Tidal's last berth (28, 11) has a bin on the cell at its foot (28, 13), in the same run
        ground, pieces, notes = self.read["LAYOUT_SS_TIDAL_ROOMS"]
        names = {pc["name"]: pc for pc in pieces}
        bin_ = names["thing_28_13"]
        self.assertEqual(bin_["shape"], [(448, 208, 464, 224)])
        self.assertEqual(bin_["height"], 8)

    def test_a_bin_on_the_shaded_carpet_leaves_it_to_the_floor(self):
        # THING_FRONT: the S.S. Tidal's three bins on the carpet in shade stand as bins, not red boxes - the
        # carpet and its shade round them are the floor's
        ground, pieces, notes = self.read["LAYOUT_SS_TIDAL_ROOMS"]
        names = {pc["name"]: pc for pc in pieces}
        for name in ("thing_10_5", "thing_16_13", "thing_28_13"):
            self.assertEqual(tuple(names[name]["leave"]), bra.SHIP_CARPET, name)



class SideWallFromTheFootTests(unittest.TestCase):
    """The object audit's survey of 2026-11-15: a side wall started past any floor of another drawing in its
    column, so where the floor at a back wall's foot is shaded (or a curtain's hem drawn over it: the Battle
    Pike's rooms) it stood a row or more in front of the back wall, and the outside showed black in the gap
    from every camera - in eighteen rooms. Rows from the foot of the wall in its column that are all floor, none
    a way out (EXIT), it runs on through, on its cells' own drawing (`own_floor`), which loses none of it."""

    def side(self, lid, name):
        ground, pieces, notes = bra.analyse(lid)
        return {pc["name"]: pc for pc in pieces}[name]

    def test_the_pike_rooms_wall_runs_on_from_the_curtains(self):
        pc = self.side("LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL", "side_e")
        # from the curtained back wall's foot in its column (row 4; it started at row 5) to the room's front
        self.assertEqual(pc["walls"], [((144, 64), (144, 128))])
        self.assertTrue(pc["own_floor"])

    def test_one_that_starts_at_the_foot_is_as_it_was(self):
        for name, seg in (("side_w", ((0, 80), (0, 48))), ("side_e", ((80, 48), (80, 80)))):
            pc = self.side("LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_FINAL", name)
            self.assertEqual(pc["walls"], [seg], name)
            self.assertFalse(pc["own_floor"], name)

    def test_not_over_a_way_out(self):
        # the Abandoned Ship's hidden floor's corridors: a way out among the rows before its east wall's stretch
        pc = self.side("LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_CORRIDORS", "side_e")
        self.assertEqual(pc["walls"], [((208, 48), (208, 176))])
        self.assertFalse(pc["own_floor"])

    def test_its_cells_keep_their_own_drawing(self):
        import gen_voxel_buildings as gen
        spec = bra.auto_room_spec("auto_battle_frontier_battle_pike_room_normal",
                                  "LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL")
        specs = {ps["name"]: ps for ps in gen.interior_specs(spec)}
        self.assertEqual(specs["auto_battle_frontier_battle_pike_room_normal_side_e"]["under"], "own")
        self.assertIsNone(specs["auto_battle_frontier_battle_pike_room_normal_wall_7_0"]["under"])


class TrickHouseWallTests(unittest.TestCase):
    """The object audit of 2026-11-14 (class 5): the Trick House's puzzle rooms draw their back wall on two rows;
    the maze's partitions that meet it were read as one run with it and stood 48 rows tall on the partition's foot,
    up to ten cells into the room. WALL_ROWS: the wall stands its two rows, the partition's cells are a thing's, as
    the maze's other partitions are."""

    LAYOUTS = ["LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE%d" % n for n in (1, 2, 3, 4, 6, 7)]

    @classmethod
    def setUpClass(cls):
        cls.read = {lid: bra.analyse(lid) for lid in cls.LAYOUTS}

    def test_the_auto_rooms_with_the_tileset(self):
        import battle_room_specs
        rooms = [lid for lid in battle_room_specs.AUTO_ROOMS
                 if vb.LayoutArt(lid).secondary in bra.WALL_ROWS]
        self.assertEqual(sorted(rooms), sorted(self.LAYOUTS))

    def test_each_back_wall_stands_its_two_rows(self):
        for lid in self.LAYOUTS:
            ground, pieces, notes = self.read[lid]
            walls = [pc for pc in pieces if pc["name"].startswith("wall_")]
            with self.subTest(room=lid):
                self.assertTrue(walls)
                for pc in walls:
                    x0, y0, x1, y1 = pc["shape"][0]
                    self.assertEqual((y0, y1, pc["height"], pc["foot"]), (0, 32, 32, 32), pc["name"])
                # nothing in the room stands taller than its wall
                self.assertLessEqual(max(pc["height"] for pc in pieces), 32)

    def test_a_partition_at_the_wall_stands_as_the_maze_does(self):
        # puzzle 6's column 12: a partition from the wall down to row 9, before a 48-row slab
        ground, pieces, notes = self.read["LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE6"]
        pc = {p["name"]: p for p in pieces}["thing_12_2"]
        cells = sorted({(x0 // 16, y0 // 16) for (x0, y0, x1, y1) in pc["shape"]})
        self.assertEqual(cells, [(12, y) for y in range(2, 10)])
        self.assertEqual(pc["height"], bra.THING)

    def test_puzzle_1_stands_on_its_cells_own_floor(self):
        # its tatami shaded along a partition's foot has the partitions' outline for its top row: the
        # outline's corners in a partition's cell are that floor's, so under the partition it is laid
        # (UNDER_OWN), not the plain tatami
        spec = bra.auto_room_spec("auto_route110_trick_house_puzzle1", "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE1")
        self.assertEqual(spec["interior"]["under"], "own")
        spec = bra.auto_room_spec("auto_route110_trick_house_puzzle2", "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE2")
        self.assertIsNone(spec["interior"]["under"])


class ShipCabinTests(unittest.TestCase):
    """The object audit's survey of 2026-11-15, the ship cabins (the InsideShip tileset). A table stood 20 rows
    tall with its top's lower half run down its front - a table stacked on a table; a bin with the shaded carpet
    north of it for a top stood 20 rows, beside the outside 24; a bin under one half of a table cut the table in
    two, the half over the bin flat; the tables and the bins on a cabin's last row lay flat. The dark holes in
    the Abandoned Ship's floor stood as dark blocks topped with carpet. The walls between two cabins with a
    passage in front of them stood as one wall 48 rows tall, the outside's black its face."""

    ROOMS = ["LAYOUT_SS_TIDAL_ROOMS", "LAYOUT_ABANDONED_SHIP_ROOMS_1F", "LAYOUT_ABANDONED_SHIP_ROOMS_B1F",
             "LAYOUT_ABANDONED_SHIP_ROOMS2_1F", "LAYOUT_ABANDONED_SHIP_ROOMS2_B1F",
             "LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_ROOMS"]

    @classmethod
    def setUpClass(cls):
        cls.read = {lid: bra.analyse(lid) for lid in cls.ROOMS}
        cls.art = {lid: vb.LayoutArt(lid) for lid in cls.ROOMS}

    def cells(self, pc):
        return sorted((x0 // 16, y0 // 16) for (x0, y0, x1, y1) in pc["shape"])

    def test_each_table_and_bin_stands_its_drawn_front_on_its_own_cells(self):
        fronts = bra.THING_FRONT["gTileset_InsideShip"]
        for lid in self.ROOMS:
            L = self.art[lid]
            for pc in self.read[lid][1]:
                kinds = {next((i for i, (tiles, _, _) in enumerate(fronts) if L.metatile(x, y) in tiles), None)
                         for (x, y) in self.cells(pc)}
                if not pc["name"].startswith("thing_") or kinds == {None}:
                    continue
                with self.subTest(room=lid, piece=pc["name"]):
                    # one kind, and every cell a whole cell of it
                    self.assertEqual(len(kinds), 1)
                    tiles, rows, colours = fronts[kinds.pop()]
                    self.assertEqual(pc["height"], rows)
                    self.assertEqual(tuple(pc["leave"]), colours)
                    for (x0, y0, x1, y1) in pc["shape"]:
                        self.assertEqual((x1 - x0, y1 - y0), (16, 16))

    def test_the_tidal_cabins_tables_and_bins(self):
        # every table whole - the one with a bin under its east half (22-23, 5-6) included - and every bin its
        # own cell, those on a cabin's last row (row 7) standing too
        ground, pieces, notes = self.read["LAYOUT_SS_TIDAL_ROOMS"]
        names = {pc["name"]: self.cells(pc) for pc in pieces if pc["name"].startswith("thing_")}
        for (x, y, w) in ((5, 6, 2), (13, 5, 2), (22, 5, 2), (33, 6, 2), (11, 12, 2), (31, 12, 4)):
            self.assertEqual(names["thing_%d_%d" % (x, y)],
                             sorted((x + i, y + j) for i in range(w) for j in range(2)))
        for (x, y) in ((7, 7), (10, 5), (16, 13), (23, 7), (28, 13)):
            self.assertEqual(names["thing_%d_%d" % (x, y)], [(x, y)])

    def test_the_holes_in_the_floor_lie_as_drawn(self):
        for lid in self.ROOMS:
            L = self.art[lid]
            holes = {(x, y) for y in range(L.h) for x in range(L.w)
                     if L.metatile(x, y) in bra.HOLES["gTileset_InsideShip"]}
            for pc in self.read[lid][1]:
                for (x0, y0, x1, y1) in pc["shape"]:
                    for (x, y) in holes:
                        self.assertFalse(x0 < x * 16 + 16 and x * 16 < x1 and y0 < y * 16 + 16 and y * 16 < y1,
                                         (lid, pc["name"], (x, y)))

    def test_the_walls_between_two_cabins_stand_apart(self):
        # the Abandoned Ship 1F's lower cabins (rows 9-12 over the passage on row 13) and the second B1F's (rows
        # 0-3 over row 4): two thin walls, their faces 28 rows, the black between them no wall's
        for lid, ya, yb in (("LAYOUT_ABANDONED_SHIP_ROOMS_1F", 9, 13), ("LAYOUT_ABANDONED_SHIP_ROOMS2_B1F", 0, 4)):
            ground, pieces, notes = self.read[lid]
            names = {pc["name"]: pc for pc in pieces}
            with self.subTest(room=lid):
                self.assertNotIn("wall_8_%d" % ya, names)
                west, east = names["divider_8_%d" % ya], names["divider_9_%d" % ya]
                self.assertEqual(west["shape"], [(128, ya * 16, 134, yb * 16)])
                self.assertEqual(east["shape"], [(154, ya * 16, 160, yb * 16)])
                for pc in (west, east):
                    self.assertEqual(pc["height"], 28)
                    self.assertTrue(pc["own_floor"])
                # nothing else stands over the columns
                for pc in pieces:
                    if pc is west or pc is east:
                        continue
                    for (x0, y0, x1, y1) in pc["shape"]:
                        self.assertFalse(x0 < 160 and 128 < x1 and y0 < yb * 16 and ya * 16 < y1, pc["name"])
                # the room's tallest piece is its wall
                self.assertLessEqual(max(pc["height"] for pc in pieces), 32)

    def test_the_second_b1f_stands_on_its_cells_own_floor(self):
        # its carpet shaded along the walls' and the passage's rows is a floor of its own (UNDER_OWN), whose
        # pixels on the berths' edges are the berths': the plain carpet laid under them showed there
        spec = bra.auto_room_spec("auto_abandoned_ship_rooms2_b1f", "LAYOUT_ABANDONED_SHIP_ROOMS2_B1F")
        self.assertEqual(spec["interior"]["under"], "own")


class SeatTests(unittest.TestCase):
    """The object audit's class 2: the ship cabins' chairs (the InsideShip tileset's two) are walkable, and
    the 2D game draws them over the player who steps on to one (their top layer covered); they lay flat.
    Each stands as a card of its cell's own pixels, as the desks' chairs, each room's own."""

    def test_the_chairs_are_walkable_and_drawn_over_the_player(self):
        import struct
        path = os.path.join(vb.ROOT, "data", "tilesets", "secondary", "inside_ship", "metatile_attributes.bin")
        with open(path, "rb") as f:
            data = f.read()
        attrs = struct.unpack("<%dH" % (len(data) // 2), data)
        for m in bra.SEATS["gTileset_InsideShip"]:
            self.assertEqual((attrs[m - 0x200] >> 12) & 0xF, 1, hex(m))     # covered
        import battle_room_specs
        for lid in battle_room_specs.AUTO_ROOMS:
            L = vb.LayoutArt(lid)
            for y in range(L.h):
                for x in range(L.w):
                    if L.metatile(x, y) in bra.SEATS.get(L.secondary, ()):
                        self.assertEqual((L.blocks[y * L.w + x] >> 10) & 3, 0, (lid, x, y))

    def test_every_chair_in_the_auto_rooms_is_a_card_of_its_cell(self):
        import battle_room_specs
        rooms = 0
        for lid in battle_room_specs.AUTO_ROOMS:
            L = vb.LayoutArt(lid)
            want = {(x, y) for y in range(L.h) for x in range(L.w)
                    if L.metatile(x, y) in bra.SEATS.get(L.secondary, ())}
            if not want:
                continue
            rooms += 1
            with self.subTest(room=lid):
                ground, pieces, notes = bra.analyse(lid)
                seats = [pc for pc in pieces if pc["name"].startswith("seat_")]
                self.assertEqual({(pc["shape"][0][0] // 16, pc["shape"][0][1] // 16) for pc in seats}, want)
                for pc in seats:
                    [(x0, y0, x1, y1)] = pc["shape"]
                    self.assertEqual((x1 - x0, y1 - y0), (16, 16))
                    self.assertTrue(pc["card"] and pc["alone"], pc["name"])
        # the S.S. Tidal's cabins and the Abandoned Ship's six rooms of them
        self.assertEqual(rooms, 7)

    def test_the_abandoned_ships_b1f_cabin_is_read_off_its_cells(self):
        import battle_room_specs
        self.assertIn("LAYOUT_ABANDONED_SHIP_ROOM_B1F", battle_room_specs.AUTO_ROOMS)
        ground, pieces, notes = bra.analyse("LAYOUT_ABANDONED_SHIP_ROOM_B1F")
        names = {pc["name"]: pc for pc in pieces}
        self.assertEqual(names["wall_1_0"]["shape"], [(16, 0, 128, 32)])
        self.assertEqual(sorted(n for n in names if n.startswith("seat_")), ["seat_2_4", "seat_3_3", "seat_6_5"])


class DeskTests(unittest.TestCase):
    """The object audit's survey of 2026-11-15: the ship cabins' desks against the back wall (the InsideShip
    tileset's, a chair pushed in) were read with the wall's columns as one run four rows deep and stood 48 rows
    of it, the desk and the chair painted upright on the wall's face - a slab over the S.S. Tidal's cabins; the
    tables and bins before the hidden floor's back walls the same. By hand (DESK): the wall over a desk stands
    to its top row, the desk its front's rows with a notch where the chair is, the chair a card before it."""

    ROOMS = {
        "LAYOUT_SS_TIDAL_ROOMS": {(28, 2), (19, 11)},
        "LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_ROOMS": {(22, 2), (40, 2), (40, 10)},
    }

    @classmethod
    def setUpClass(cls):
        cls.read = {lid: bra.analyse(lid) for lid in cls.ROOMS}

    def test_every_desk_in_the_auto_rooms_is_found(self):
        import battle_room_specs
        found = {lid: set(bra.desks(vb.LayoutArt(lid))) for lid in battle_room_specs.AUTO_ROOMS}
        self.assertEqual({lid: spots for lid, spots in found.items() if spots}, self.ROOMS)

    def test_each_is_a_desk_and_a_chair_before_the_wall(self):
        cx0, cy0, cx1, cy1 = bra.DESK_CHAIR
        for lid, spots in self.ROOMS.items():
            ground, pieces, notes = self.read[lid]
            names = {pc["name"]: pc for pc in pieces}
            for (dx, dy) in spots:
                X, Y = dx * 16, dy * 16
                with self.subTest(room=lid, desk=(dx, dy)):
                    desk, chair = names["desk_%d_%d" % (dx, dy)], names["chair_%d_%d" % (dx, dy)]
                    self.assertEqual(desk["height"], bra.DESK_FRONT)
                    self.assertEqual(max(y1 for (_, _, _, y1) in desk["shape"]), Y + 14 + bra.DESK_FRONT)
                    self.assertEqual(desk["back"], Y)
                    self.assertTrue(chair["card"])
                    self.assertEqual(chair["shape"], [(X + cx0, Y + cy0, X + cx1, Y + cy1)])
                    # the desk's notch is the chair's
                    for (x0, y0, x1, y1) in desk["shape"]:
                        self.assertFalse(x0 < X + cx1 and X + cx0 < x1 and y0 < Y + cy1 and Y + cy0 < y1)
                    # the wall over its columns stands on its top row, no taller than the room's
                    walls = [pc for pc in pieces if pc["name"].startswith("wall_")
                             and pc["shape"][0][0] <= X and X + 32 <= pc["shape"][0][2]
                             and pc["shape"][0][1] < Y <= pc["shape"][0][3]]
                    self.assertEqual(len(walls), 1)
                    self.assertEqual(walls[0]["foot"], Y)
                    self.assertLessEqual(walls[0]["height"], 32)

    def test_nothing_stands_taller_than_the_walls(self):
        for lid in self.ROOMS:
            ground, pieces, notes = self.read[lid]
            with self.subTest(room=lid):
                self.assertLessEqual(max(pc["height"] for pc in pieces), 32)

    def test_a_wall_stands_over_a_lent_table_or_bin(self):
        # the S.S. Tidal's tables and bins are lent to the hidden floor rooms: the cells they stand in are
        # `taken`, and the wall over them stands to their row as over the room's own (FOOT_ON_LENT); before,
        # the run's foot was no floor and the wall lay flat behind them
        taken = {(1, 2), (18, 2), (19, 2), (18, 3), (19, 3), (20, 2), (7, 10)}
        ground, pieces, notes = bra.analyse("LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_ROOMS", taken=taken)
        walls = [pc["shape"][0] for pc in pieces if pc["name"].startswith("wall_")]
        for (cx, cy) in ((1, 2), (18, 2), (19, 2), (20, 2), (7, 10)):
            self.assertTrue(any(x0 <= cx * 16 < x1 and y1 == cy * 16 for (x0, y0, x1, y1) in walls), (cx, cy))
        self.assertEqual(bra.FOOT_ON_LENT, {"gTileset_InsideShip"})

    def test_the_tables_and_bins_before_a_back_wall_stand_on_their_own(self):
        # the hidden floor's bins in the wall's shade (1, 2), (20, 2), (7, 10) and its table under the wall
        # (18-19, 2-3); the S.S. Tidal's bin under its desk (28, 4)
        ground, pieces, notes = self.read["LAYOUT_ABANDONED_SHIP_HIDDEN_FLOOR_ROOMS"]
        names = {pc["name"]: sorted((x0 // 16, y0 // 16) for (x0, y0, x1, y1) in pc["shape"]) for pc in pieces}
        for (x, y) in ((1, 2), (20, 2), (7, 10)):
            self.assertEqual(names["thing_%d_%d" % (x, y)], [(x, y)])
        self.assertEqual(names["thing_18_2"], [(18, 2), (18, 3), (19, 2), (19, 3)])
        ground, pieces, notes = self.read["LAYOUT_SS_TIDAL_ROOMS"]
        names = {pc["name"]: sorted((x0 // 16, y0 // 16) for (x0, y0, x1, y1) in pc["shape"]) for pc in pieces}
        self.assertEqual(names["thing_28_4"], [(28, 4)])
