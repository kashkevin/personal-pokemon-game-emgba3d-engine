#!/usr/bin/env python3
"""Tests for a piece's `each_foot` (voxel_building_specs.piece, gen_voxel_buildings): each column of
a solid box stands on its own drawn foot, as a wall's, instead of all on the lowest.

The Elite Four's long cases are drawn with their corner by the arena cut on the slant. Stood all on
the lowest row, their front ran straight past that corner and the side by the arena stood out beyond
the drawing as a spike (the object audit of 2026-11-14). With each column on its own foot, the front
follows the slant and the side ends where the drawing does."""

import os
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voxel_building as vb  # noqa: E402
import voxel_building_specs as specs  # noqa: E402

CASE = (60, 70, 90, 255)


def slanted_case():
    """A case 12 columns wide, drawn from row 0: the left 6 columns down to row 40, the right 6
    cut on the slant, a row shorter each column (39 ... 34)."""
    art = Image.new("RGBA", (12, 48), (0, 0, 0, 0))
    px = art.load()
    for u in range(12):
        bottom = 40 if u < 6 else 40 - (u - 5)
        for v in range(0, bottom):
            px[u, v] = CASE
    return art


def front_ends(foot):
    """{column: the Z its front stands at} of the case stood as a solid box 12 rows high."""
    mesh = vb.Mesh()
    vb.Relief("case", slanted_case(), 12, vb.Tile(0, 20, 4, 32), foot=foot, solid=True).emit(mesh)
    ends = {}
    for (tri, _, tag) in mesh.tris:
        if tag.endswith(".front"):
            xs = [p[0] for p in tri]
            for u in range(int(round(min(xs))), int(round(max(xs)))):
                ends[u] = tri[0][2]
    return ends, mesh


class EachFootTests(unittest.TestCase):
    def test_the_spec_says_so(self):
        self.assertTrue(specs.piece("case", [(0, 0, 12, 40)], 12, each_foot=True)["each_foot"])
        self.assertFalse(specs.piece("case", [(0, 0, 12, 40)], 12)["each_foot"])

    def test_all_on_the_lowest_row_the_front_runs_straight_past_the_slant(self):
        ends, _ = front_ends(40)
        self.assertEqual({round(z) for z in ends.values()}, {40})

    def test_each_on_its_own_foot_the_front_follows_the_slant(self):
        ends, _ = front_ends(None)
        self.assertEqual(round(ends[0]), 40)
        self.assertEqual(round(ends[11]), 34)
        self.assertTrue(all(ends[u] >= ends[u + 1] for u in range(11)))

    def test_the_side_ends_where_the_drawing_does(self):
        # the side at the slanted end (x = 12) reaches no further than the last column's foot
        _, mesh = front_ends(None)
        side = [p[2] for (tri, _, tag) in mesh.tris if ".flank" in tag
                for p in tri if abs(p[0] - 12.0) < 1e-9]
        self.assertTrue(side)
        self.assertLessEqual(max(side), 34 + 1e-6)


if __name__ == "__main__":
    unittest.main()
