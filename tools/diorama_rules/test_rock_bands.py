#!/usr/bin/env python3

import unittest
from types import SimpleNamespace

from rock_bands import CAP_Q16, RIDGE_Q16, RIDGE_STEP_Q16, band_surfaces, rock_cells


def summary(palette, visible=256):
    source = SimpleNamespace(palette=palette)
    pixels = tuple(SimpleNamespace(visible=index < visible, source=source) for index in range(256))
    return {"resolved": True, "layers": ((), (), pixels)}


def layout(rows):
    """Cells from rows of characters: '.' walkable ground, '#' blocked rock, '~' water, 'T' a blocked tree."""
    cells = []
    for row in rows:
        for char in row:
            cells.append({"collision": 0 if char in ".~" else 1,
                          "behavior": "MB_POND_WATER" if char == "~" else "MB_NORMAL"})
    return tuple(cells), len(rows[0]), len(rows)


class RockBandTests(unittest.TestCase):
    def test_a_face_between_two_levels_meets_both(self):
        # a band one cell thick between a plateau (0) and a street a level lower (-16)
        cells, width, height = layout(["...", "###", "..."])
        levels = {0: 0, 1: 0, 2: 0, 6: -16, 7: -16, 8: -16}
        out = band_surfaces(cells, width, height, frozenset({3, 4, 5}), levels)
        for offset in (3, 4, 5):
            ground, top = out[offset]
            self.assertEqual(ground, -16)       # one mass from the lowest level it meets
            self.assertGreaterEqual(top, -16)
            self.assertLessEqual(top, 0)        # never over the plateau: no pillar
        # a thicker face: a cliff, its top level with the plateau to its foot on the street (no ramp)
        cells, width, height = layout(["..", "##", "##", "##", ".."])
        levels = {0: 0, 1: 0, 8: -16, 9: -16}
        out = band_surfaces(cells, width, height, frozenset(range(2, 8)), levels)
        self.assertEqual({out[offset] for offset in range(2, 8)}, {(-16, 0)})

    def test_a_ridge_on_one_level_rises_with_its_width(self):
        # a tile a cell into the band: walls a cell or two thick a tile, a three-cell ridge's middle half a tile more
        for thickness, top in ((1, RIDGE_Q16), (2, RIDGE_Q16), (3, RIDGE_Q16 + RIDGE_STEP_Q16)):
            rows = ["." * 5] + ["#" * 5] * thickness + ["." * 5]
            cells, width, height = layout(rows)
            rock = frozenset(offset for offset, cell in enumerate(cells) if cell["collision"])
            out = band_surfaces(cells, width, height, rock, {})
            middle = (1 + thickness // 2) * width + 2
            self.assertEqual(out[middle], (0, top))
        # a maze's junction no higher than its walls (one wall across another)
        rows = ["..#..", "..#..", "#####", "..#..", "..#.."]
        cells, width, height = layout(rows)
        rock = frozenset(offset for offset, cell in enumerate(cells) if cell["collision"])
        out = band_surfaces(cells, width, height, rock, {})
        self.assertEqual({top for _ground, top in out.values()}, {RIDGE_Q16})

    def test_a_wide_mountain_is_capped_into_a_mesa(self):
        rows = ["." * 12] + ["." + "#" * 10 + "."] * 10 + ["." * 12]
        cells, width, height = layout(rows)
        rock = frozenset(offset for offset, cell in enumerate(cells) if cell["collision"])
        out = band_surfaces(cells, width, height, rock, {})
        self.assertEqual(max(top for _ground, top in out.values()), CAP_Q16)
        # its rim, where it meets the ground, a tile; inside it its top
        self.assertEqual((out[1 * width + 1][1], out[1 * width + 6][1], out[6 * width + 6][1]),
                         (RIDGE_Q16, RIDGE_Q16, CAP_Q16))

    def test_water_and_other_blocked_cells(self):
        # rock in water stands from the water's level
        cells, width, height = layout(["~~~", "~#~", "~~~"])
        levels = {offset: -18 for offset, cell in enumerate(cells) if cell["behavior"] == "MB_POND_WATER"}
        out = band_surfaces(cells, width, height, frozenset({4}), levels)
        self.assertEqual(out[4][0], -18)
        self.assertEqual(out[4][1], -18 + RIDGE_Q16)
        # a tree beside it holds it at its ground's level: a strip between the sea and a forest is no fin
        cells, width, height = layout(["~#T", "~#T", "~#T"])
        levels = {0: -16, 3: -16, 6: -16, 2: 0, 5: 0, 8: 0}
        out = band_surfaces(cells, width, height, frozenset({1, 4, 7}), levels)
        self.assertTrue(all(-16 < out[offset][1] <= 0 for offset in (1, 4, 7)))
        # a band nothing meets (the map's edge all round) keeps no entry
        cells, width, height = layout(["#"])
        self.assertEqual(band_surfaces(cells, width, height, frozenset({0}), {}), {})

    def test_a_pit_beside_a_long_wall_takes_none_of_it_down(self):
        # Mt Pyre's summit: a wall of rock on the plateau (0), a pit two levels down (-32) beside its west end
        rows = ["." * 12, "#" * 12, "." * 12]
        cells, width, height = layout(rows)
        rock = frozenset(range(12, 24))
        levels = {offset: 0 for offset in range(36) if offset not in rock}
        levels[0] = levels[24] = -32
        out = band_surfaces(cells, width, height, rock, levels)
        self.assertTrue(all(ground == 0 for ground, _top in out.values()))      # on the plateau, not in the pit
        self.assertTrue(all(top == RIDGE_Q16 for _ground, top in out.values()))      # a ridge a tile high
        # lower ground along a real share of it is a face's foot: the band stands on it - a tile over the pit where
        # only the pit is near, level with the plateau elsewhere (a face up to it, or rock no higher than it)
        levels.update({offset: -32 for offset in list(range(0, 6)) + list(range(24, 30))})
        out = band_surfaces(cells, width, height, rock, levels)
        self.assertEqual([out[12 + x] for x in (0, 3, 4, 7, 8, 11)],
                         [(-32, -32 + RIDGE_Q16)] * 2 + [(-32, 0)] * 4)

    def test_walkable_mountain_art_is_not_rock(self):
        # MB_MOUNTAIN_TOP the player walks on keeps its height: never a band's cell
        cells, width, height = layout(["....", ".##.", "...."])
        cells = tuple(dict(cell, collision=0) if offset == 6 else cell for offset, cell in enumerate(cells))
        classes = ("ground",) * len(cells)
        sources = ("fallback:flat",) * len(cells)
        summaries = (summary(7),) * len(cells)
        rock = rock_cells(cells, classes, sources, summaries, width, height, mountain=frozenset({5, 6}))
        self.assertEqual(rock, frozenset({5}))

    def test_rock_cells_follow_the_rocks_palette_not_the_grounds(self):
        cells, width, height = layout(["....", ".##.", ".##.", "...."])
        classes = ["ground"] * len(cells)
        sources = ["fallback:flat"] * len(cells)
        summaries = [summary(2)] * len(cells)          # the grass's palette
        classes[5], sources[5] = "cliff", "rule:x"     # two seeds drawn in the rock's palette (7)
        classes[6], sources[6] = "cliff", "rule:x"
        summaries[5] = summaries[6] = summaries[9] = summary(7)
        # 9: a plain blocked cell in the rock's palette joins; 10: one in the grass's does not
        rock = rock_cells(cells, tuple(classes), tuple(sources), tuple(summaries), width, height)
        self.assertEqual(rock, frozenset({5, 6, 9}))
        # excluded cells (a prop) are never rock
        rock = rock_cells(cells, tuple(classes), tuple(sources), tuple(summaries), width, height,
                          exclude=frozenset({9}))
        self.assertEqual(rock, frozenset({5, 6}))

    def test_rock_art_is_rock_where_it_is_blocked(self):
        # Sootopolis: its white rock no class of its own stands, drawn in the palette of the streets it paves where
        # they are walked - the art's blocked cells are rock (5, 6), its walked one (9, the street) is not
        cells, width, height = layout(["....", ".##.", "....", "...."])
        classes = ("ground",) * len(cells)
        sources = ("fallback:flat",) * len(cells)
        summaries = (summary(2),) * len(cells)
        self.assertEqual(rock_cells(cells, classes, sources, summaries, width, height), frozenset())
        rock = rock_cells(cells, classes, sources, summaries, width, height, art=frozenset({5, 6, 9}))
        self.assertEqual(rock, frozenset({5, 6}))

    def test_a_craters_mouth_lies_on_its_foot_and_its_rim_rises_to_it(self):
        # a crater in the sea (-16): a ring of rock five cells thick round a mouth of two cells
        rows = ["~" * 14] + ["~" + "#" * 12 + "~"] * 12 + ["~" * 14]
        cells, width, height = layout(rows)
        rock = frozenset(offset for offset, cell in enumerate(cells) if cell["collision"])
        levels = {offset: -16 for offset, cell in enumerate(cells) if not cell["collision"]}
        mouth = frozenset({6 * width + 6, 6 * width + 7})
        out = band_surfaces(cells, width, height, rock, levels, pits=mouth)
        self.assertEqual({out[offset] for offset in mouth}, {(-16, -16)})     # on the rock's foot, no height
        # the rim beside it as high as the cells as far from the sea, not a tile as at an edge
        self.assertEqual(out[5 * width + 6][1], -16 + CAP_Q16)
        # a cap of the art's own: the rock rises on inward, four tiles over the sea at the most
        caps = {offset: 64 for offset in rock}
        out = band_surfaces(cells, width, height, rock, levels, pits=mouth, caps=caps)
        self.assertEqual(out[1 * width + 1][1], -16 + RIDGE_Q16)          # its edge a tile, as before
        self.assertEqual(out[5 * width + 6][1], -16 + RIDGE_Q16 + 4 * RIDGE_STEP_Q16)   # five cells in
        self.assertLessEqual(max(top for _ground, top in out.values()), -16 + 64)


if __name__ == "__main__":
    unittest.main()
