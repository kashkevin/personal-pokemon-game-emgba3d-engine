#!/usr/bin/env python3
"""The house kit's furniture as the rooms lend it (gen_voxel_buildings.reuse_pieces), from the 3D
object audit of 2026-11-14 and its survey of 2026-11-15.

The low tea table on the tatami stood only where Mr. Briney's own piece was lent - cut three columns
short of the drawing's right edge, which lay on the floor beside it as a strip, and 10 rows tall, its
front the top's colour over the apron (the Cove Lily Motel's "tall box with a thin strip on one end");
the same table lay flat in the rooms built before his, and the table without a cup everywhere. Now
house 3 stands the one with the cup and house 4 the one without, each the drawing's whole extent with
its apron and legs (6 rows) for its front, and every later room with their tiles stands them.

The second house's cupboard and the house with a bed's chest, lent to the Fan Clubs and many houses,
had a tile of their drawers' fronts run across their sides; their sides are the frame's own column."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voxel_building as vb  # noqa: E402
import voxel_building_specs as specs  # noqa: E402

OUTLINE = "414a6a"
CUP = (0x24E, 0x24F, 0x256, 0x257)
NO_CUP = (0x24E, 0x25F, 0x256, 0x257)


def tiles(layout, cx, cy):
    return (layout.metatile(cx, cy), layout.metatile(cx + 1, cy),
            layout.metatile(cx, cy + 1), layout.metatile(cx + 1, cy + 1))


def drawing(layout, x0, y0, x1, y1):
    """{(x, y): 'rrggbb'} of the layout's drawing in the rectangle."""
    out = {}
    for cy in range(y0 // 16, (y1 + 15) // 16):
        for cx in range(x0 // 16, (x1 + 15) // 16):
            px = layout.cell_image(layout.metatile(cx, cy)).convert("RGB").load()
            for j in range(16):
                for i in range(16):
                    x, y = cx * 16 + i, cy * 16 + j
                    if x0 <= x < x1 and y0 <= y < y1:
                        out[(x, y)] = "%02x%02x%02x" % px[i, j]
    return out


def own_pieces(layout_id):
    spec = next(s for s in specs.SPECS if s.get("interior", {}).get("layout") == layout_id)
    return {pc["name"]: pc for pc in spec["interior"]["pieces"]}


class TeaTableTests(unittest.TestCase):

    def test_each_table_is_its_whole_drawing(self):
        for layout_id, (cx, cy), want in (("LAYOUT_HOUSE3", (4, 3), CUP), ("LAYOUT_HOUSE4", (7, 4), NO_CUP)):
            with self.subTest(room=layout_id):
                L = vb.LayoutArt(layout_id)
                self.assertEqual(tiles(L, cx, cy), want)
                table = own_pieces(layout_id)["tea_table"]
                [(x0, y0, x1, y1)] = table["shape"]
                self.assertEqual(table["height"], 6)
                px = drawing(L, x0 - 1, y0, x1 + 1, y1)
                # down the top's rows its first and last columns are the outline; past them the
                # tatami (its lines are the outline's colour), none of the table's own colours
                table_colours = {"d5cd62", "bdac52", "947329"}
                for y in range(y0 + 1, y0 + 21):
                    self.assertEqual((px[(x0, y)], px[(x1 - 1, y)]), (OUTLINE, OUTLINE), y)
                    for x in (x0 - 1, x1):
                        self.assertIn(px[(x, y)], specs.TEA_TABLE_FLOOR + (OUTLINE,), (x, y))
                self.assertFalse(table_colours & {px[(x, y)] for x in (x0 - 1, x1) for y in range(y0, y1)})
                self.assertIn("d5cd62", {px[(x0 + 1, y)] for y in range(y0, y1)})
                self.assertIn("d5cd62", {px[(x1 - 2, y)] for y in range(y0, y1)})
                # its top row the outline across, its legs on its last row
                self.assertEqual({px[(x, y0)] for x in range(x0, x1)}, {OUTLINE})
                self.assertEqual((px[(x0 + 3, y1 - 1)], px[(x1 - 4, y1 - 1)]), (OUTLINE, OUTLINE))

    def test_the_rooms_after_them_have_the_same_tiles(self):
        # the rooms that stand them as house 3's and house 4's (reuse_pieces: the same tiles first)
        for layout_id, spots in (("LAYOUT_ROUTE104_MR_BRINEYS_HOUSE", {(7, 4): CUP}),
                                 ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_1F", {(2, 4): CUP, (8, 4): NO_CUP}),
                                 ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_2F",
                                  {(8, 3): CUP, (2, 6): CUP, (2, 3): NO_CUP, (8, 6): NO_CUP})):
            L = vb.LayoutArt(layout_id)
            for (cx, cy), want in spots.items():
                self.assertEqual(tiles(L, cx, cy), want, (layout_id, cx, cy))
        # Mr. Briney's own table is gone: house 3's stands in his house
        self.assertNotIn("table", own_pieces("LAYOUT_ROUTE104_MR_BRINEYS_HOUSE"))


class DresserSideTests(unittest.TestCase):

    def test_the_sides_are_the_frames_column(self):
        for layout_id, name, colours in (("LAYOUT_HOUSE2", "cupboard", {"bdac52", "ac8b39", OUTLINE}),
                                         ("LAYOUT_HOUSE_WITH_BED", "chest", {"83ac4a", "6a8b31", OUTLINE})):
            with self.subTest(room=layout_id):
                pc = own_pieces(layout_id)[name]
                x0, y0, x1, y1 = pc["side"]
                [(sx0, sy0, sx1, sy1)] = pc["shape"]
                # one column of its own front, from its foot up its height
                self.assertEqual((x1 - x0, y1, y1 - y0), (1, sy1, pc["height"]))
                self.assertTrue(sx0 < x0 < sx1 - 1)
                px = drawing(vb.LayoutArt(layout_id), x0, y0, x1, y1)
                self.assertLessEqual(set(px.values()), colours)



class EverGrandeHallTests(unittest.TestCase):
    """Ever Grande City's halls between the Elite Four's rooms had no model: their back wall with the doorway
    lay on the landing and their railings lay on the stairs' slope (the object audit, class 1). The terrain
    raises the landing in place and a model stands on the ground under its door, so the wall stands on the
    landing and each railing's row, a piece of its own, on its step."""

    HALLS = (("LAYOUT_EVER_GRANDE_CITY_SHORT_HALL", 13), ("LAYOUT_EVER_GRANDE_CITY_HALL4", 34))

    def test_the_doorway_is_cut_out_of_the_wall(self):
        for layout_id, rows in self.HALLS:
            pieces = own_pieces(layout_id)
            wall = pieces["wall"]
            # the opening x 77-99 under the lintel (rows 0-25); the warp's cell is the doorway's
            self.assertIn((77, 0, 99, 25), wall["shape"], layout_id)
            self.assertFalse([r for r in wall["shape"] if r[0] < 99 and 77 < r[2] and r[3] > 25], layout_id)
            self.assertEqual(list(wall["free"]), [(5, 2)], layout_id)
            self.assertEqual(pieces["stairwell_door"]["inside"], (77, 25, 99, 48), layout_id)
            px = drawing(vb.LayoutArt(layout_id), 77, 25, 99, 48)
            # the opening is the arch's dark, its jambs' light edges and its outline
            self.assertLessEqual(set(px.values()), {"414a6a", "316a7b", "94bdb4", "417ba4"}, layout_id)

    def test_a_railing_piece_a_row_on_each_side(self):
        for layout_id, rows in self.HALLS:
            pieces = own_pieces(layout_id)
            L = vb.LayoutArt(layout_id)
            for y in range(5, rows):
                for side, (x0, x1) in (("w", (51, 64)), ("e", (112, 125))):
                    pc = pieces["rail_%s_%d" % (side, y)]
                    self.assertEqual(pc["shape"], [(x0, y * 16, x0 + (x1 - x0), y * 16 + 16)])
                    # its edges the rail's outline, the stone or the landing past them
                    px = drawing(L, x0 - 1, y * 16 + 8, x1 + 1, y * 16 + 9)
                    self.assertEqual((px[(x0, y * 16 + 8)], px[(x1 - 1, y * 16 + 8)]), (OUTLINE, OUTLINE),
                                     (layout_id, side, y))
                    self.assertIn(px[(x0 - 1, y * 16 + 8)] if side == "w" else px[(x1, y * 16 + 8)],
                                  pc["leave"], (layout_id, side, y))
            self.assertFalse([n for n in pieces if n.startswith("rail_") and int(n.split("_")[2]) >= rows])


if __name__ == "__main__":
    unittest.main()
