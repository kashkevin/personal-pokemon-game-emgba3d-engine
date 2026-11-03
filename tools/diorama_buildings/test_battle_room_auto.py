#!/usr/bin/env python3
"""Tests for battle_room_auto's behaviour names (KNOWN_ISSUES MS-66).

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


if __name__ == "__main__":
    unittest.main()
