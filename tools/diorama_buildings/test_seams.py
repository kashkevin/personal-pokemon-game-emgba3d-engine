#!/usr/bin/env python3
"""The object audit's class 6 (2026-11-14): dotted lines of another colour along a model's edges, where the
floor laid under a model showed past a face lifted a hair over it, from a camera turned from the GBA's.

* A back wall's void (the room's outside behind it, black laid over the floor its cells hide): its end and
  the edge of the floor under the wall's cells were one line - the S.S. Tidal's carpet in dots between its
  cabins, the Ranking Hall's marble in its corner. It runs a pixel on over the outside's black now
  (reach_outside).
* Wanda's rug dashes: house 1's chair stands in her room whole, its rug round it its own floor decal, lifted
  half a pixel along the GBA's line of sight; from a camera turned from the GBA's the decal's edge drew off the
  edge of its cells and the planks laid under the chair's cell showed, beside the table's marks too. A floor
  decal hangs a skirt down to the floor at the sides where the floor under it is another colour now (vb.Decal
  `edges`, floor_decal_edges), edge on to the GBA's camera; and the rug the chair hides - a quarter pixel lower
  than the rug round it - runs a texel under that rug, so the two no longer part over a few planks specks
  (ring_under_marks).
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


class DecalReachTests(unittest.TestCase):
    """A floor decal hangs a skirt down to the floor from the sides of its cells it is told to (its edge's
    texels stretched, `~clamp`), along the GBA's line of sight, so its camera sees each edge on; its cells'
    own quads stay a texel to a pixel. The sides: where the floor laid under the cell is another colour at
    its edge - not between two of its own cells, nor over the room's outside (floor_decal_edges)."""

    class Mesh:
        def __init__(self):
            self.polys = []

        def poly(self, verts, shade, tag):
            self.polys.append((verts, tag))

    def emit(self, decal):
        mesh = self.Mesh()
        decal.emit(mesh)
        return ([v for (v, tag) in mesh.polys if tag.endswith(".decal")],
                [v for (v, tag) in mesh.polys if tag.endswith(".skirt~clamp")])

    def test_a_decal_hangs_skirts_from_the_sides_named(self):
        edges = {(0, 0, "w"), (0, 0, "n"), (0, 0, "s"), (1, 0, "e"), (1, 0, "n"), (1, 0, "s")}
        cells, skirts = self.emit(vb.Decal("rug_floor", [(0, 0), (1, 0)], 32, edges=edges))
        self.assertEqual(len(cells), 2)
        for verts in cells:                                   # a texel to a pixel, lifted 0.5 south
            for (x, y, z, u, v) in verts:
                self.assertEqual((u, v), (x, z - 0.5 + 32))
        self.assertEqual(len(skirts), 6)
        for verts in skirts:
            # from the floor (y 0) up to the decal (y 0.5), each corner moved along the line of sight
            # (0, 1, 1): seen along it, the skirt has no area
            self.assertEqual(sorted({v[1] for v in verts}), [0.0, 0.5])
            seen = {(round(x, 6), round(z - y, 6)) for (x, y, z, u, v) in verts}
            self.assertEqual(len(seen), 2)
            for (x, y, z, u, v) in verts:
                self.assertTrue(0.0 < u < 32.0 and 32.0 < v < 48.0)
        # nothing past its cells: x 0..32, the floor's z 0..16
        xs = [v[0] for verts in skirts for v in verts]
        self.assertEqual((min(xs), max(xs)), (0.0, 32.0))
        # no sides named (the decal under a piece): no skirts
        _, skirts = self.emit(vb.Decal("chair_under~behind", [(0, 0)], 32, lift=0.25))
        self.assertEqual(skirts, [])

    @staticmethod
    def art(marks):
        """A one-cell piece's art (16 x 48): its drawing, the floor's marks under it (`marks`: {(u, v):
        rgb}) 16 rows down, what it hides 32 rows down."""
        img = Image.new("RGBA", (16, 48), (0, 0, 0, 0))
        for (u, v), c in marks.items():
            img.putpixel((u, 16 + v), c + (255,))
        return img

    def test_the_sides_whose_floor_under_is_another_colour(self):
        planks = Image.new("RGBA", (16, 16), (200, 180, 40, 255)).load()
        rug = (240, 200, 120)
        # a rug all over the cell: every side but the room's outside and its own next cell
        apx = self.art({(u, v): rug for v in range(16) for u in range(16)}).load()
        edges = g.floor_decal_edges([(0, 0), (0, -1)], (5, 5), apx, 16, lambda cx, cy: planks,
                                    lambda cx, cy: (cx, cy) == (5, 6))
        self.assertEqual({e for e in edges if e[:2] == (0, 0)}, {(0, 0, "w"), (0, 0, "e")})
        # planks at its west edge, as the floor under it: not that side
        marks = {(u, v): rug for v in range(16) for u in range(1, 16)}
        marks.update({(0, v): (200, 180, 40) for v in range(16)})
        edges = g.floor_decal_edges([(0, 0)], (5, 5), self.art(marks).load(), 16,
                                    lambda cx, cy: planks, lambda cx, cy: False)
        self.assertEqual(edges, {(0, 0, "e"), (0, 0, "n"), (0, 0, "s")})
        # nothing drawn at an edge (the piece's own pixels there): not that side either
        marks = {(u, v): rug for v in range(1, 16) for u in range(16)}
        edges = g.floor_decal_edges([(0, 0)], (5, 5), self.art(marks).load(), 16,
                                    lambda cx, cy: planks, lambda cx, cy: False)
        self.assertNotIn((0, 0, "n"), edges)

    def test_the_hidden_marks_run_a_texel_under_the_marks_round_them(self):
        rug, under = (240, 200, 120), (230, 190, 110)
        marks = {(u, v): rug for v in range(16) for u in range(16) if not (4 <= u < 8 and 4 <= v < 8)}
        img = self.art(marks)
        for v in range(4, 8):
            for u in range(4, 8):
                img.putpixel((u, 32 + v), under + (255,))
        apx = img.load()
        self.assertEqual(g.ring_under_marks(apx, 16, 16, 16, 32), 6 * 6 - 4 * 4)
        for v in range(16):
            for u in range(16):
                inside = 4 <= u < 8 and 4 <= v < 8
                ring = 3 <= u < 9 and 3 <= v < 9 and not inside
                self.assertEqual(apx[u, 32 + v][3] >= 128, inside or ring, (u, v))
                if ring:
                    self.assertEqual(apx[u, 32 + v][:3], rug)
                    self.assertEqual(apx[u, 16 + v][:3], rug)       # the marks themselves unchanged

    def test_house_1s_chair_by_its_table(self):
        pieces = room("LAYOUT_HOUSE1")
        decals = {p.name: p for ps in pieces.values() for p in ps["parts"]() if isinstance(p, vb.Decal)}
        chair = decals["chair_n_back_floor"]
        # the rug round it over the house's planks: east of it (the room's floor), and west, where the
        # table hides the floor beside it
        self.assertIn((0, 0, "e"), chair.edges)
        self.assertIn((0, 0, "w"), chair.edges)


if __name__ == "__main__":
    unittest.main()
