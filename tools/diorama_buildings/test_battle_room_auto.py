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


if __name__ == "__main__":
    unittest.main()
