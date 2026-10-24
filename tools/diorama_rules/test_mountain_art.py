#!/usr/bin/env python3

import unittest
from types import SimpleNamespace

from mountain_art import (MountainArtError, detect_layout, enclosed_tops, mass_extension,
                          mountain_art_mask, mountain_heightmap, outline_cells,
                          validate_layout)


def pixel(color=None, visible=True):
    return SimpleNamespace(rgba=color or (0, 0, 0, 255), visible=visible)


class MountainArtTests(unittest.TestCase):
    def test_partial_foreground_is_authoritative(self):
        foreground = tuple(pixel(visible=3 <= index % 16 <= 12)
                           for index in range(256))
        full = tuple(pixel() for _ in range(256))
        rows, mode = mountain_art_mask(
            {"resolved": True, "layers": (full, foreground, full)})
        self.assertEqual(mode, "foreground")
        self.assertEqual(set(rows), {0x1FF8})

    def test_opaque_tile_uses_corner_connected_background(self):
        full = tuple(pixel((1, 1, 1, 255) if index % 16 in (0, 15)
                           or index // 16 in (0, 15) else (2, 2, 2, 255))
                     for index in range(256))
        empty = tuple(pixel(visible=False) for _ in range(256))
        rows, mode = mountain_art_mask(
            {"resolved": True, "layers": (full, empty, full)})
        self.assertEqual(mode, "silhouette")
        self.assertEqual(rows[0], 0)
        self.assertEqual(rows[1], 0x7FFE)

    def test_square_and_unresolved_art_are_rejected(self):
        full = tuple(pixel((index, 0, 0, 255)) for index in range(256))
        empty = tuple(pixel(visible=False) for _ in range(256))
        with self.assertRaisesRegex(MountainArtError, "non-rectangular"):
            mountain_art_mask({"resolved": True, "layers": (full, empty, full)})
        with self.assertRaisesRegex(MountainArtError, "unresolved"):
            validate_layout(({"behavior": "MB_MOUNTAIN_TOP", "collision": 1},),
                            ({"resolved": False},), 1, 1)

    def test_heightmap_curves_from_low_edge_to_one_cell_peak(self):
        rows = (0, 0) + (0x3FFC,) * 12 + (0, 0)
        heights = mountain_heightmap(rows)
        occupied = {height for height in heights if height != 0}
        self.assertEqual((min(occupied), max(occupied)), (4, 16))
        self.assertGreater(len(occupied), 2)
        self.assertEqual(heights[8 * 16 + 8], 16)
        self.assertEqual(heights[2 * 16 + 2], 4)

    def test_normal_art_requires_family_tokens_and_seed_connection(self):
        def source_pixel(tile, palette=10):
            return SimpleNamespace(rgba=(2, 2, 2, 255), visible=True,
                                   source=SimpleNamespace(tile=tile, palette=palette))

        full_seed = tuple(source_pixel(65 + index % 2) for index in range(256))
        full_edge = tuple(source_pixel(66) for _ in range(256))
        foreground = tuple(pixel(visible=3 <= index % 16 <= 12)
                           for index in range(256))
        summaries = tuple({"resolved": True, "layers": (full, foreground, composed)}
                          for full, composed in ((full_seed, full_seed),
                                                 (full_edge, full_edge),
                                                 (full_edge, full_edge)))
        cells = ({"behavior": "MB_MOUNTAIN_TOP", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 1}, {"behavior": "MB_NORMAL", "collision": 1})
        self.assertEqual(detect_layout(cells, summaries, 3, 1), frozenset((0, 1, 2)))

        # The game tags its mountain paths MB_MOUNTAIN_TOP too (the battle background):
        # a cell the player walks on is no rock, and joins none to it.
        walkable_seed = ({"behavior": "MB_MOUNTAIN_TOP", "collision": 0},) + cells[1:]
        self.assertEqual(detect_layout(walkable_seed, summaries, 3, 1), frozenset())
        walkable_edge = cells[:2] + ({"behavior": "MB_NORMAL", "collision": 0},)
        self.assertEqual(detect_layout(walkable_edge, summaries, 3, 1), frozenset((0, 1)))

        disconnected = cells + ({"behavior": "MB_NORMAL", "collision": 1},)
        unrelated = tuple(source_pixel(200) for _ in range(256))
        summaries += ({"resolved": True,
                       "layers": (unrelated, foreground, unrelated)},)
        self.assertEqual(detect_layout(disconnected, summaries, 4, 1),
                         frozenset((0, 1, 2)))

    def test_mass_takes_the_blocked_rock_round_a_mountain_in_its_palette(self):
        # Route 106: the mountain's MB_MOUNTAIN_TOP rock, then its face and edges
        # (MB_NORMAL, the same palette, other tiles) - one mass; a cell in another
        # palette (a tree), a walkable one, one an object stands on (`exclude`)
        # and one past them are not.
        def drawn(palette):
            full = tuple(SimpleNamespace(rgba=(2, 2, 2, 255), visible=True,
                                         source=SimpleNamespace(tile=palette * 16, palette=palette))
                         for _ in range(256))
            return {"resolved": True, "layers": (full, tuple(pixel(visible=False)
                                                             for _ in range(256)), full)}

        cells = ({"behavior": "MB_MOUNTAIN_TOP", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 1},
                 {"behavior": "MB_NORMAL", "collision": 0},
                 {"behavior": "MB_NORMAL", "collision": 1})
        summaries = (drawn(3), drawn(3), drawn(3), drawn(2), drawn(3), drawn(3), drawn(3))
        self.assertEqual(mass_extension(cells, summaries, 7, 1, frozenset((0,))),
                         frozenset((1, 2)))
        self.assertEqual(mass_extension(cells, summaries, 7, 1, frozenset((0,)),
                                        exclude=frozenset((2,))), frozenset((1,)))
        # no MB_MOUNTAIN_TOP rock, no mountain to extend
        self.assertEqual(mass_extension(({"behavior": "MB_NORMAL", "collision": 1},) * 2,
                                        (drawn(3),) * 2, 2, 1, frozenset((0,))), frozenset())


    def test_a_reef_is_one_mass_with_its_closed_in_top(self):
        # Route 106's reefs: a walkable MB_MOUNTAIN_TOP middle no walk reaches,
        # closed in by blocked water drawn with the rock; one with an event on
        # it, or open to the sea, is left walkable.
        reef = ({"behavior": "MB_OCEAN_WATER", "collision": 1},
                {"behavior": "MB_MOUNTAIN_TOP", "collision": 1},
                {"behavior": "MB_OCEAN_WATER", "collision": 1},
                {"behavior": "MB_NORMAL", "collision": 1},
                {"behavior": "MB_MOUNTAIN_TOP", "collision": 0},
                {"behavior": "MB_NORMAL", "collision": 1},
                {"behavior": "MB_OCEAN_WATER", "collision": 1},
                {"behavior": "MB_NORMAL", "collision": 1},
                {"behavior": "MB_OCEAN_WATER", "collision": 1})
        self.assertEqual(enclosed_tops(reef, 3, 3), frozenset((4,)))
        self.assertEqual(enclosed_tops(reef, 3, 3, events=frozenset((4,))), frozenset())
        opened = reef[:7] + ({"behavior": "MB_OCEAN_WATER", "collision": 0},) + reef[8:]
        self.assertEqual(enclosed_tops(opened, 3, 3), frozenset())

        def drawn(upper):
            # the sea on the lower layer, the rock (palette 3) on the upper
            lower = tuple(SimpleNamespace(rgba=(1, 1, 1, 255), visible=True,
                                          source=SimpleNamespace(tile=1, palette=4))
                          for _ in range(256))
            top = tuple(SimpleNamespace(rgba=(2, 2, 2, 255), visible=index < upper,
                                        source=SimpleNamespace(tile=2, palette=3))
                        for index in range(256))
            composed = tuple(t if t.visible else b for t, b in zip(top, lower))
            return {"resolved": True, "layers": (lower, top, composed)}

        summaries = tuple(drawn(120 if index % 2 == 0 else 250) for index in range(9))
        self.assertEqual(mass_extension(reef, summaries, 3, 3, frozenset((1, 4))),
                         frozenset((0, 2, 3, 5, 6, 7, 8)))
        # the edges draw the rock over the sea: their outline is the rock
        self.assertEqual(outline_cells(summaries, frozenset((0, 1))), frozenset((0, 1)))


if __name__ == "__main__":
    unittest.main()
