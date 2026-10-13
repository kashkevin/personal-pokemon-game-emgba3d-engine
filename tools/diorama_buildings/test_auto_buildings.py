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

    def test_sootopolis_houses_are_found_by_their_roofs(self):
        # The door search leaves the crater's rock alone; its houses are found
        # by their roofs (CRATER_HOUSES): towers and boxes, 3 cells wide, each
        # once, the tower with its roof's tip a cell over its middle.
        specs = specs_of("SootopolisCity")
        self.assertEqual(len(specs), 22)
        self.assertTrue(all("_tower_" in n or "_box_" in n for n in specs))
        tower = specs["auto_sootopoliscity_tower_8_3"]   # house 1, its door at (9, 6)
        self.assertEqual(tower["rect"], (8, 2, 3, 5))
        self.assertIn((1, 0), tower["owned"])
        box = specs["auto_sootopoliscity_box_44_3"]      # house 2, its door at (45, 6)
        self.assertEqual(box["rect"], (44, 3, 3, 4))
        cells = [(s["rect"][0] + i, s["rect"][1] + j) for s in specs.values() for (i, j) in s["owned"]]
        self.assertEqual(len(cells), len(set(cells)))

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


class LayoutVariantTests(unittest.TestCase):
    """A map whose scripts switch its layout (setmaplayoutindex) keeps its
    buildings in the other layout where their cells are the same."""

    def test_the_variants_come_from_the_maps_scripts(self):
        import gen_voxel_buildings as gen
        variants = gen.layout_variants()
        self.assertEqual(variants["LAYOUT_ROUTE111"], {"LAYOUT_ROUTE111_NO_MIRAGE_TOWER"})
        self.assertEqual(variants["LAYOUT_SOOTOPOLIS_CITY"],
                         {"LAYOUT_SOOTOPOLIS_CITY_LEGENDS_BATTLE"})

    def test_trainer_hill_stands_without_the_mirage_tower_and_the_tower_does_not(self):
        import expansion_assets
        import gen_voxel_buildings as gen
        import voxel_building as vb
        models = {m.name: m for m in gen.build_models(only="auto_route111")}
        layouts = expansion_assets.emerald_layouts(vb.ROOT)
        where = {name: {p[4] for p in gen.find_placements(m, layouts)}
                 for name, m in models.items()}
        self.assertEqual(where["auto_route111_landmark_29_106"],
                         {"LAYOUT_ROUTE111", "LAYOUT_ROUTE111_NO_MIRAGE_TOWER"})
        # the tower's cells are sand in the layout the game shows when it is hidden
        self.assertEqual(where["auto_route111_19_58"], {"LAYOUT_ROUTE111"})


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


class HeightTests(unittest.TestCase):
    """V29: buildings whose eave the search cannot find stand as their
    drawing says - a tower up to its roof's outline, a second storey with
    the first - and a building is its own cells, not the trees over it."""

    def test_a_sootopolis_tower_stands_up_to_its_roof(self):
        specs = specs_of("SootopolisCity")
        towers = [s for n, s in specs.items() if "_tower_" in n]
        self.assertEqual(len(towers), 11)
        # the search stops at the door's lintel (20); the roof's outline is
        # 8 rows higher
        self.assertTrue(all(s["relief"]["height"] == 28 for s in towers))
        boxes = [s for n, s in specs.items() if "_box_" in n]
        self.assertEqual(len(boxes), 11)
        self.assertTrue(all(s["relief"]["height"] == 34 for s in boxes))

    def test_a_second_storey_is_wall_not_roof(self):
        self.assertEqual(specs_of("LilycoveCity")["auto_lilycovecity_37_24"]["relief"]["height"], 64)
        factory = specs_of("BattleFrontier_OutsideWest")["auto_battlefrontier_outsidewest_19_17"]
        self.assertEqual(factory["relief"]["height"], 56)

    def test_the_glass_workshop_is_its_own_cells(self):
        shop = specs_of("Route113")["auto_route113_33_5"]
        x, y, w, h = shop["rect"]
        cells = {(x + i, y + j) for (i, j) in shop["owned"]}
        # its four rows, not the two rows of trees over them; its window's
        # cell (walkable on the map) is no hole in its front
        self.assertEqual((x, y, w, h), (32, 2, 4, 4))
        self.assertIn((34, 4), cells)
        self.assertEqual(len(cells), 16)
        self.assertEqual(shop["relief"]["height"], 28)


if __name__ == "__main__":
    unittest.main()
