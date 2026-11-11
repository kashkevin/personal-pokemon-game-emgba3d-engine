#!/usr/bin/env python3
"""Tests for gen_voxel_buildings.wall_void_spans and voxel_building.Void: the room's outside
behind a back wall, black over the floor the wall hides at the GBA's 45 degrees.

A room's back wall stands on the floor at its foot as one panel of its drawn rows, and the cells
it is drawn over are laid with the room's floor. The GBA's 45 degrees see none of that floor; the
diorama camera's 52 degrees see over the wall's top, and the floor showed there as a strip over
the wall, where the 2D game draws black: Pacifidlog Town's houses, Mr. Briney's, Rustboro's
flats, Brendan's house, Dewford's hall (KNOWN_ISSUES MS-227)."""

import os
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402
import voxel_building as vb  # noqa: E402


def block(u0, u1, v0, v1):
    return {(u, v) for u in range(u0, u1) for v in range(v0, v1)}


def everywhere(u, first):
    return True


class WallVoidSpanTests(unittest.TestCase):
    def test_a_plain_back_wall_has_the_outside_behind_it_all_along(self):
        # Pacifidlog's houses: two cells tall, standing at row 32, nothing over it
        self.assertEqual(g.wall_void_spans(block(0, 160, 0, 32), 32, 32, everywhere),
                         [(0, 160, 0, 32)])

    def test_over_a_doorway_only_under_its_lintel(self):
        # Rustboro's flats: over the doorway only the lintel is the wall's; the recess behind
        # the opening (a cell deep, from row 16) keeps its floor and the flight drawn on it
        pixels = block(0, 32, 0, 32) | block(32, 48, 0, 13) | block(48, 96, 0, 32)
        self.assertEqual(g.wall_void_spans(pixels, 32, 32, everywhere),
                         [(0, 32, 0, 32), (32, 48, 0, 13), (48, 96, 0, 32)])

    def test_behind_a_drawn_top_only_from_its_back_edge(self):
        # 24 tall over 32 drawn rows: its top lies from row 24 to its foot, and the floor
        # behind the top's back edge is what shows over it
        self.assertEqual(g.wall_void_spans(block(0, 64, 0, 32), 32, 24, everywhere),
                         [(0, 64, 0, 24)])

    def test_a_wall_standing_free_in_the_room_keeps_the_floor_behind_it(self):
        # the drawing over its first row is the room's floor, not its black outside
        self.assertEqual(g.wall_void_spans(block(0, 44, 48, 80), 80, 28, lambda u, first: False),
                         [])

    def test_only_where_the_outside_is_over_it(self):
        over = lambda u, first: u < 16  # noqa: E731
        self.assertEqual(g.wall_void_spans(block(0, 32, 0, 32), 32, 32, over), [(0, 16, 0, 32)])

    def test_from_the_first_row_the_wall_is_drawn_from(self):
        # Rustboro's school: its wall drawn from 8 rows down, the black over it nothing
        self.assertEqual(g.wall_void_spans(block(0, 192, 8, 36), 36, 28, everywhere),
                         [(0, 192, 8, 36)])

    def test_without_a_foot_each_column_stands_at_the_end_of_its_last_run(self):
        # a gap in a column: the run that stands on the floor is the last one
        pixels = block(0, 16, 0, 32) | block(16, 32, 0, 4) | block(16, 32, 8, 40)
        self.assertEqual(g.wall_void_spans(pixels, None, 32, everywhere),
                         [(0, 16, 0, 32), (16, 32, 8, 40)])

    def test_columns_merge_only_where_they_are_alike(self):
        pixels = block(0, 16, 0, 32) | block(16, 32, 4, 32)
        self.assertEqual(g.wall_void_spans(pixels, 32, 32, everywhere),
                         [(0, 16, 0, 32), (16, 32, 4, 32)])

    def test_no_pixels_no_void(self):
        self.assertEqual(g.wall_void_spans(set(), 32, 32, everywhere), [])


class SpareBlackTexelTests(unittest.TestCase):
    """A wall whose drawing has no black gets one black texel where its page keeps texels
    already and nothing reads: a column more of page per wall doubled a page of the
    Battle Frontier's rooms when it was always painted past the art's edge."""

    def art(self, width, h, sample_w, sample_h, marked=()):
        art = Image.new("RGBA", (width, 2 * h * 16 + sample_h + h * 16), (0, 0, 0, 0))
        art.paste(Image.new("RGBA", (width, h * 16), (200, 100, 50, 255)), (0, 0))
        art.paste(Image.new("RGBA", (sample_w, sample_h), (90, 90, 90, 255)), (0, 2 * h * 16))
        for (cx, cy) in marked:
            art.putpixel((cx * 16 + 3, h * 16 + cy * 16 + 3), (10, 200, 10, 255))
        return art

    def test_beside_the_side_walls_sample(self):
        art = self.art(160, 2, 16, 32)
        out, spot = g.spare_black_texel(art, 10, 2, 16, 32, set())
        self.assertIs(out, art)
        self.assertEqual(spot, (16, 64))
        self.assertEqual(out.getpixel(spot), (0, 0, 0, 255))

    def test_a_wall_one_cell_wide_takes_a_cell_of_its_floor_copy_without_marks(self):
        art = self.art(16, 3, 16, 32, marked=[(0, 0)])
        before = art.getbbox()
        out, spot = g.spare_black_texel(art, 1, 3, 16, 32, {(0, 0)})
        self.assertEqual(spot, (0, 3 * 16 + 16))       # cell (0, 1) of the floor's copy
        self.assertEqual(out.getbbox(), before)         # no texel more of page
        self.assertEqual(out.getpixel((3, 3 * 16 + 3)), (10, 200, 10, 255))   # the mark kept

    def test_past_the_edge_only_when_its_page_has_no_room(self):
        # nothing under its drawing: no sample, no marks - the box ends with the drawing
        art = Image.new("RGBA", (16, 2 * 32 + 32), (0, 0, 0, 0))
        art.paste(Image.new("RGBA", (16, 32), (200, 100, 50, 255)), (0, 0))
        out, spot = g.spare_black_texel(art, 1, 2, 0, 0, set())
        self.assertEqual(out.size, (17, 96))
        self.assertEqual(spot, (16, 0))
        self.assertEqual(out.getpixel(spot), (0, 0, 0, 255))
        self.assertEqual(out.crop((0, 0, 16, 32)).tobytes(), art.crop((0, 0, 16, 32)).tobytes())


class VoidPartTests(unittest.TestCase):
    def make(self, spans, texel=(16, 64), lift=0.25):
        art = Image.new("RGBA", (32, 96), (0, 0, 0, 0))
        art.load()[texel] = (0, 0, 0, 255)
        return vb.Model("room_wall", art, [vb.Void("wall_void", spans, texel, lift)], (2, 2), 0)

    def test_every_corner_reads_the_one_black_texel(self):
        model = self.make([(0, 32, 0, 32)])
        self.assertEqual(model.triangle_count(), 2)
        for tri, _, tag in model.mesh.tris:
            for (_, _, _, u, v) in tri:
                self.assertTrue(16 <= u < 17 and 64 <= v < 65, (u, v))

    def test_it_lies_over_the_floor_and_never_in_front_of_the_walls_foot(self):
        # lifted along the GBA's line of sight as a floor decal is, its near edge stood a
        # quarter pixel in front of the wall's foot: a line of black along every back wall
        # from the game's own camera. It stops half a pixel behind the foot instead.
        model = self.make([(0, 32, 4, 32)], lift=0.25)
        zs = set()
        for tri, _, _ in model.mesh.tris:
            for (x, y, z, _, _) in tri:
                self.assertAlmostEqual(y, 0.25)
                zs.add(z)
        self.assertEqual(zs, {4.0, 31.5})
        # at 45 degrees (a point shows at row z - y) it lies under the wall's own rows
        self.assertTrue(all(4 - 1 < z - 0.25 < 32 for z in zs))

    def test_a_span_shallower_than_the_gap_is_left_out(self):
        self.assertEqual(self.make([(0, 32, 10, 10.25)]).triangle_count(), 0)

    def test_the_checks_leave_it_to_the_room(self):
        # the model's own check does not judge it (the wall is drawn over it at 45 degrees),
        # nor the texel density (one texel stretched), but the room's check does
        model = self.make([(0, 32, 0, 32)])
        tags = {tag for _, _, tag in model.mesh.tris}
        self.assertEqual(tags, {"wall_void.void~behind~clamp"})
        self.assertEqual(vb.density_check(model), [])
        self.assertEqual(vb.ortho_check(model, exact=[]), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
