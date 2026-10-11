#!/usr/bin/env python3
"""Tests for window_glow.py (the windows of the modelled buildings)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import window_glow  # noqa: E402

WALL = (208, 208, 192)
FRAME = (64, 72, 104)
GLASS = (104, 184, 192)
GLARE = (224, 232, 240)
BLUE_WALL = (152, 176, 216)


class Page:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[WALL] * w for _ in range(h)]

    def rect(self, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.px[j][i] = c

    def window(self, x, y, w, h, glare=None):
        """A pane of w x h glass inside a one-pixel frame at (x, y)."""
        self.rect(x, y, w + 2, h + 2, FRAME)
        self.rect(x + 1, y + 1, w, h, GLASS)
        if glare is not None:
            self.px[y + 1 + glare[1]][x + 1 + glare[0]] = GLARE

    def colour(self, x, y):
        return self.px[y][x]


def upright(w, h, x0=0, y0=0, x1=None, y1=None):
    """A wall looking south over page pixels x0..x1, y0..y1 (two triangles)."""
    x1 = w if x1 is None else x1
    y1 = h if y1 is None else y1
    a = (x0, y0, 0.0, 1.0, 1.0)
    b = (x1, y0, 1.0, 1.0, 1.0)
    c = (x1, y1, 1.0, 0.0, 1.0)
    d = (x0, y1, 0.0, 0.0, 1.0)
    # wound so the normal looks south (+z)
    return [(a, d, c), (a, c, b)]


def lying(w, h):
    """A roof: the page lying flat (its normal straight up)."""
    a = (0, 0, 0.0, 1.0, 0.0)
    b = (w, 0, 1.0, 1.0, 0.0)
    c = (w, h, 1.0, 1.0, 1.0)
    d = (0, h, 0.0, 1.0, 1.0)
    return [(a, b, c), (a, c, d)]


def windows(page, tris):
    wall = window_glow.wall_mask(page.w, page.h, tris)
    return wall, [f for f in window_glow.find_windows(page.w, page.h, page.colour, wall) if f["window"]]


class WallMaskTests(unittest.TestCase):
    def test_upright_faces_count_and_lying_ones_do_not(self):
        wall = window_glow.wall_mask(8, 8, upright(8, 8))
        self.assertEqual(sum(wall), 64)
        self.assertEqual(sum(window_glow.wall_mask(8, 8, lying(8, 8))), 0)

    def test_part_of_the_page(self):
        wall = window_glow.wall_mask(8, 8, upright(8, 8, 2, 2, 6, 5))
        self.assertEqual(sum(wall), 4 * 3)
        self.assertEqual(wall[2 * 8 + 2], 1)
        self.assertEqual(wall[1 * 8 + 2], 0)


class FindWindowsTests(unittest.TestCase):
    def test_a_framed_pane_is_a_window(self):
        page = Page(24, 16)
        page.window(4, 4, 6, 4, glare=(1, 0))
        wall, found = windows(page, upright(24, 16))
        self.assertEqual(len(found), 1)
        pixels = set(found[0]["pixels"])
        self.assertEqual(pixels, {(x, y) for x in range(5, 11) for y in range(5, 9)})

    def test_the_walls_own_blue_is_not_glass(self):
        page = Page(40, 24)
        page.rect(2, 2, 30, 14, BLUE_WALL)
        page.window(6, 6, 4, 3)
        wall, found = windows(page, upright(40, 24))
        self.assertEqual(len(found), 1)
        self.assertTrue(all(page.colour(x, y) == GLASS for x, y in found[0]["pixels"]))

    def test_a_stripe_is_not_a_window(self):
        page = Page(24, 8)
        page.rect(2, 3, 12, 1, GLASS)
        page.rect(2, 2, 12, 1, FRAME)
        page.rect(2, 4, 12, 1, FRAME)
        wall, found = windows(page, upright(24, 8))
        self.assertEqual(found, [])

    def test_glass_on_a_roof_is_not_a_window(self):
        page = Page(24, 16)
        page.window(4, 4, 6, 4)
        wall, found = windows(page, lying(24, 16))
        self.assertEqual(found, [])

    def test_a_sliver_over_a_glazing_bar_joins_its_window(self):
        page = Page(24, 16)
        page.rect(4, 4, 8, 8, FRAME)
        page.rect(5, 5, 6, 1, GLASS)  # the sliver over the bar
        page.rect(5, 7, 6, 4, GLASS)  # the pane under it
        wall, found = windows(page, upright(24, 16))
        rows = {y for f in found for x, y in f["pixels"]}
        self.assertIn(5, rows)
        self.assertIn(9, rows)


class RunTests(unittest.TestCase):
    def test_runs_cover_the_pane_with_one_brightness(self):
        page = Page(24, 16)
        page.window(4, 4, 6, 4)
        wall, found = windows(page, upright(24, 16))
        runs = window_glow.runs(7, found)
        self.assertEqual(sorted((y, x, n) for y, x, n, level in runs),
                         [(y, 5, 6) for y in range(5, 9)])
        self.assertEqual(len({level for y, x, n, level in runs}), 1)
        self.assertTrue(all(1 <= level <= 255 for y, x, n, level in runs))

    def test_brightness_is_steady_and_in_range(self):
        for page in range(4):
            for x in range(0, 64, 7):
                b = window_glow.brightness(page, (x, 3, 4, 4))
                self.assertEqual(b, window_glow.brightness(page, (x, 3, 4, 4)))
                self.assertTrue(0.7 <= b <= 1.0)


if __name__ == "__main__":
    unittest.main()
