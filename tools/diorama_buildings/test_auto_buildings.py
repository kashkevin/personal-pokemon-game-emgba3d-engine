#!/usr/bin/env python3
"""Tests for auto_buildings.py (the buildings no spec models), on the game's
own maps."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import auto_buildings  # noqa: E402


def specs_of(map_name):
    return {s["name"]: s for s in auto_buildings.auto_specs({}, only_map=map_name)}


class LilycoveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.specs = specs_of("LilycoveCity")

    def test_houses_and_the_department_store_are_found(self):
        self.assertIn("auto_lilycovecity_36_6", self.specs)
        store = self.specs["auto_lilycovecity_27_6"]
        self.assertEqual(store["rect"], (23, 0, 9, 7))
        # nearly all facade: the override, not the eave search
        self.assertEqual(store["relief"]["height"], 106)

    def test_a_house_stands_on_its_cells_with_its_facade(self):
        house = self.specs["auto_lilycovecity_36_6"]
        x, y, w, h = house["rect"]
        self.assertEqual((w, h), (4, 4))
        # the door is the front row's, and the front is the rect's bottom row
        self.assertIn((36 - x, 6 - y), house["owned"])
        self.assertEqual(y + h - 1, 6)
        self.assertTrue(16 <= house["relief"]["height"] <= 40)
        # the top runs back to the north edge of the cells it stands on
        self.assertEqual(house["relief"]["back"] % 16, 0)

    def test_buildings_do_not_overlap(self):
        seen = set()
        for spec in self.specs.values():
            x, y, w, h = spec["rect"]
            cells = {(x + i, y + j) for (i, j) in spec["owned"]}
            self.assertFalse(cells & seen, spec["name"])
            seen |= cells


class SkipTests(unittest.TestCase):
    def test_a_woods_way_in_is_no_building(self):
        # Route 104's gaps into Petalburg Woods are doors in a row of trees
        self.assertFalse([n for n in specs_of("Route104") if n in
                          ("auto_route104_10_38", "auto_route104_32_42")])

    def test_sootopolis_is_left_to_its_rock(self):
        self.assertEqual(specs_of("SootopolisCity"), {})

    def test_houses_side_by_side_are_two_buildings(self):
        specs = specs_of("VerdanturfTown")
        a, b = specs["auto_verdanturftown_1_14"], specs["auto_verdanturftown_10_14"]
        ax = {a["rect"][0] + i for (i, j) in a["owned"]}
        bx = {b["rect"][0] + i for (i, j) in b["owned"]}
        self.assertFalse(ax & bx)


class RoofBandTests(unittest.TestCase):
    def test_a_shallow_roof_has_no_middle(self):
        self.assertIsNone(auto_buildings.roof_band(8))

    def test_the_band_is_the_middle_of_the_roof(self):
        self.assertEqual(auto_buildings.roof_band(56), (16, 36))
        a, b = auto_buildings.roof_band(20)
        self.assertTrue(0 < a < b <= 20)

    def test_the_hidden_depth_repeats_the_band_not_the_eave(self):
        from PIL import Image
        import voxel_building as vb

        # 32 rows of roof over a 16-row front, standing on 3 cells: the top
        # drawn from Z = 16, laid back to Z = 0 with what the depth repeats
        art = Image.new("RGBA", (16, 48), (200, 80, 80, 255))

        def depth_rows(fill):
            mesh = vb.Mesh()
            vb.Relief("r", art, 16, vb.Tile(0, 32, 8, 48), back=0, fill=fill).emit(mesh)
            rows = [p[4] for (tri, _, tag) in mesh.tris if tag.endswith(".top~depth")
                    for p in tri]
            return [v for v in rows if v > 1.0]   # the back edge's outline row aside

        self.assertGreater(max(depth_rows(None)), 30)   # the course ends at the eave
        rows = depth_rows((10, 20))
        self.assertTrue(rows and min(rows) >= 10 and max(rows) <= 20)


class FacadeTests(unittest.TestCase):
    def test_the_eave_is_the_first_long_edge_above_the_door(self):
        from PIL import Image

        img = Image.new("RGBA", (48, 64), (0, 0, 0, 0))
        px = img.load()
        for v in range(64):
            for u in range(48):
                # roof above row 36, wall below: one long edge
                px[u, v] = (200, 60, 60, 255) if v < 36 else (220, 220, 200, 255)
        self.assertEqual(auto_buildings.facade_height(img, 16), 64 - 36)

    def test_no_edge_leaves_the_door_and_its_lintel(self):
        from PIL import Image

        img = Image.new("RGBA", (32, 48), (180, 180, 180, 255))
        self.assertEqual(auto_buildings.facade_height(img, 8), 20)


if __name__ == "__main__":
    unittest.main()
