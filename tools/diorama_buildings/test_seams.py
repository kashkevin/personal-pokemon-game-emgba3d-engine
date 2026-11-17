#!/usr/bin/env python3
"""The object audit's class 6 (2026-11-14): dotted lines of another colour along a model's edges, where the
floor laid under a model showed past a face lifted a hair over it, from a camera turned from the GBA's.

* A back wall's void (the room's outside behind it, black laid over the floor its cells hide): its end and
  the edge of the floor under the wall's cells were one line - the S.S. Tidal's carpet in dots between its
  cabins, the Ranking Hall's marble in its corner. It runs a pixel on over the outside's black now
  (reach_outside).
"""

import os
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402
import voxel_building as vb  # noqa: E402


def room(layout_id):
    import voxel_building_specs as specs
    spec = next(s for s in specs.SPECS if s.get("interior", {}).get("layout") == layout_id)
    return {ps["name"]: ps for ps in g.interior_specs(spec)}


def voids(piece):
    return [p for p in piece["parts"]() if isinstance(p, vb.Void)]


class VoidReachTests(unittest.TestCase):
    def test_a_void_runs_a_pixel_on_over_the_outside_beside_and_above_it(self):
        # a wall at x 32..63, y 16..47 of a black drawing; the column left of it black for its top rows only
        img = Image.new("RGBA", (96, 64), (0, 0, 0, 255))
        for y in range(16, 48):
            for x in range(32, 64):
                img.putpixel((x, y), (200, 100, 50, 255))
        for y in range(32, 48):
            img.putpixel((31, y), (10, 10, 10, 255))
        spans = g.reach_outside([(0, 32, 0, 32)], 32, 16, img.load(), 96, 64)
        # right: every row black - the span widened; left: rows 0..15 only - a strip; nothing over it
        self.assertEqual(spans, [(0, 33, 0, 32), (-1, 0, 0, 16)])

    def test_nothing_is_added_beside_the_rooms_floor(self):
        img = Image.new("RGBA", (64, 48), (90, 90, 90, 255))     # a floor all round
        spans = g.reach_outside([(0, 16, 0, 16)], 16, 16, img.load(), 64, 48)
        self.assertEqual(spans, [(0, 16, 0, 16)])

    def test_past_the_rooms_edge_counts_as_the_outside(self):
        img = Image.new("RGBA", (32, 32), (90, 90, 90, 255))
        self.assertEqual(g.reach_outside([(0, 32, 0, 16)], 0, 0, img.load(), 32, 32), [(-1, 33, 0, 16)])

    def test_the_ss_tidals_cabin_walls_reach_over_the_black_between_them(self):
        pieces = room("LAYOUT_SS_TIDAL_ROOMS")
        # the second cabin's back wall: the black column between the cabins on its left, the map's top over it
        (void,) = voids(pieces["auto_ss_tidal_rooms_wall_10_0"])
        self.assertEqual(void.spans[0][0], -1)
        # the first cabin's: the room's left wall beside it, not black - not carried over it
        (first,) = voids(pieces["auto_ss_tidal_rooms_wall_1_0"])
        self.assertEqual(first.spans[0][0], 0)

    def test_the_ranking_halls_walls_reach_over_its_black_corner(self):
        pieces = room("LAYOUT_BATTLE_FRONTIER_RANKING_HALL")
        # the middle wall: strips beside its top two rows where the corners either side are black (the
        # stepped walls stand beside its third)
        spans = [s for v in voids(pieces["frontier_ranking_hall_wall_5_2"]) for s in v.spans]
        self.assertEqual(spans, [(0, 112, 0, 48), (-1, 0, 0, 32), (112, 113, 0, 32)])
        # the stepped wall beside its corner: a strip over the rows where the corner is black
        spans = [s for v in voids(pieces["frontier_ranking_hall_wall_39_4"]) for s in v.spans]
        self.assertIn((-1, 0, 0, 32), spans)


if __name__ == "__main__":
    unittest.main()
