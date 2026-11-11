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
        # the floor reaches the edges only below the alcove's wings (rows 9 on)
        self.assertEqual(walls, {"side_w": ((0, 240), (0, 176)), "side_e": ((160, 144), (160, 160))})


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
        # its ten ice rocks (0x203, and 0x216 on the ice's highlight): eight stand on the floor
        # alone, the two at the foot of a ledge (ALONE) too; the two behind a ledge (11, 9 and
        # 9, 19) lie on its top, where its 20 rows would hide a rock standing behind it
        things = self.things("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        L = vb.LayoutArt("LAYOUT_SOOTOPOLIS_CITY_GYM_1F")
        rocks = [(x, y) for y in range(L.h) for x in range(L.w)
                 if L.blocks[y * L.w + x] & 0x3FF in (0x203, 0x216)]
        self.assertEqual(len(rocks), 10)
        for x, y in rocks:
            if (x, y) in ((11, 9), (9, 19)):
                continue
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
        self.assertEqual(walls["wall_3_0"], (48, 0, 96, 32))
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


if __name__ == "__main__":
    unittest.main()
