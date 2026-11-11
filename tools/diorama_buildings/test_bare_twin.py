#!/usr/bin/env python3
"""Tests for gen_voxel_buildings.bare_parts: a piece standing bare on another room's floor
carries none of its own room's floor.

A piece found by its own pixels in another room (reuse_pieces) stands there as its bare twin; the
other room's paint is laid round it and its ground under it (place_reused). The twin left out the
marks round it (`_floor`) but kept the floor its drawing hides (`_under~behind`): the Pokemon
Center's PC in the Pokemon League's lobby laid the Center's wall top over the lobby's own wall,
black over the wall's trim beside and above the PC (KNOWN_ISSUES MS-227)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402
import voxel_building as vb  # noqa: E402


class Body:
    """A part that is not a decal (a Relief, a Card, a wall): the piece itself."""

    def __init__(self, name):
        self.name = name


class BareTwinTests(unittest.TestCase):
    def test_the_bare_twin_keeps_the_piece_and_leaves_out_both_floor_decals(self):
        body = Body("pc")
        marks = vb.Decal("pc_floor", [(0, 2)], 48)
        hidden = vb.Decal("pc_under~behind", [(0, 0), (1, 0), (1, 2)], 96, lift=0.25)
        self.assertEqual(g.bare_parts([body, marks, hidden]), [body])

    def test_a_part_that_is_no_decal_stays_whatever_its_name(self):
        # a side wall of a stairwell is named "..._side0~behind": not a floor decal
        parts = [Body("stairwell_side0~behind"), Body("rug_floor"), Body("pc_under~behind")]
        self.assertEqual(g.bare_parts(parts), parts)

    def test_the_piece_as_it_stands_in_its_own_room_is_unchanged(self):
        # make_model passes the original parts to the piece's own model: the decals stay there
        marks = vb.Decal("pc_floor", [(0, 2)], 48)
        hidden = vb.Decal("pc_under~behind", [(0, 0)], 96, lift=0.25)
        parts = [Body("pc"), marks, hidden]
        g.bare_parts(parts)
        self.assertEqual(parts, [parts[0], marks, hidden])


if __name__ == "__main__":
    unittest.main()
