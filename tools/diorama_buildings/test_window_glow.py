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


DARK_PANE = (64, 72, 104)
LIGHT_FRAME = (192, 192, 200)


class DarkPaneTests(unittest.TestCase):
    def test_a_dark_pane_in_a_light_frame_is_a_window(self):
        page = Page(32, 20)
        page.rect(4, 4, 12, 8, LIGHT_FRAME)
        page.rect(5, 5, 10, 6, DARK_PANE)
        wall, found = windows(page, upright(32, 20))
        self.assertEqual(len(found), 1)
        self.assertEqual(set(found[0]["pixels"]), {(x, y) for x in range(5, 15) for y in range(5, 11)})

    def test_a_dark_outline_or_band_is_not(self):
        page = Page(40, 20)
        page.rect(2, 2, 30, 1, DARK_PANE)    # an outline
        page.rect(2, 8, 30, 4, DARK_PANE)    # a shadow band across the wall
        wall, found = windows(page, upright(40, 20))
        self.assertEqual(found, [])

    def test_a_dark_pane_on_a_dark_wall_is_not(self):
        page = Page(32, 20)
        page.rect(0, 0, 32, 20, (90, 90, 110))
        page.rect(5, 5, 10, 6, DARK_PANE)
        wall, found = windows(page, upright(32, 20))
        self.assertEqual(found, [])


DOOR_BLUE = (96, 168, 232)
DOOR_SILL = (216, 224, 232)


def page_windows(page, rects):
    wall = window_glow.wall_mask(page.w, page.h, upright(page.w, page.h))
    return [f for f in window_glow.find_page_windows(page.w, page.h, page.colour, wall, rects)
            if f["window"]]


def pixels_of(found):
    return {q for f in found for q in f["pixels"]}


class PageWindowsTests(unittest.TestCase):
    """V32: the windows that glow are the ones each building's drawing has,
    every pane of them, and none the page had before is lost."""

    LEFT, RIGHT = (0, 0, 32, 24), (32, 0, 64, 24)

    def beside_a_blue_building(self, wall_colour=BLUE_WALL):
        # the building on the left is walled in the colour the right one's
        # glass is drawn in
        page = Page(64, 24)
        page.rect(2, 2, 28, 20, wall_colour)
        return page

    def test_a_house_beside_a_blue_walled_building_has_its_windows(self):
        page = self.beside_a_blue_building()
        page.rect(40, 6, 7, 5, FRAME)
        page.rect(41, 7, 5, 3, BLUE_WALL)
        wall, found = windows(page, upright(64, 24))
        self.assertEqual(found, [])          # the page's search: the left one's wall
        glass = {(x, y) for x in range(41, 46) for y in range(7, 10)}
        self.assertEqual(pixels_of(page_windows(page, [self.LEFT, self.RIGHT])), glass)

    def test_a_walls_shading_band_is_not_a_window(self):
        page = self.beside_a_blue_building()
        page.rect(34, 20, 25, 1, FRAME)      # the wall's foot
        for x in (36, 44, 52):               # columns
            page.rect(x, 4, 1, 16, FRAME)
        page.rect(37, 17, 7, 3, BLUE_WALL)   # the shading along the foot
        page.rect(45, 17, 7, 3, BLUE_WALL)
        wall = window_glow.wall_mask(64, 24, upright(64, 24))
        # the drawing's own search takes the band for panes ...
        self.assertTrue([f for f in window_glow.find_windows(64, 24, page.colour, wall, self.RIGHT)
                         if f["window"]])
        # ... but a frame does not close round it: the wall runs on above
        self.assertEqual(page_windows(page, [self.LEFT, self.RIGHT]), [])

    def test_a_glint_is_not_a_window(self):
        page = self.beside_a_blue_building()
        page.rect(40, 6, 4, 4, (80, 80, 88))   # grey: not itself a dark pane
        page.rect(41, 7, 2, 2, BLUE_WALL)
        self.assertEqual(page_windows(page, [self.LEFT, self.RIGHT]), [])

    def test_blue_glass_needs_no_frame_all_round(self):
        # a glass door: its frame at its sides and top, a sill under it
        page = self.beside_a_blue_building(DOOR_BLUE)
        page.rect(40, 7, 8, 9, FRAME)
        page.rect(41, 8, 6, 8, DOOR_BLUE)
        page.rect(41, 16, 6, 1, DOOR_SILL)
        glass = {(x, y) for x in range(41, 47) for y in range(8, 16)}
        self.assertTrue(glass <= pixels_of(page_windows(page, [self.LEFT, self.RIGHT])))

    def test_every_pane_of_a_window(self):
        # a pane white with the sky over a glazing bar: the page's search
        # finds the glass under it
        page = Page(24, 20)
        page.rect(4, 2, 8, 10, FRAME)
        page.rect(5, 3, 6, 3, GLARE)
        page.rect(5, 7, 6, 4, GLASS)
        lower = {(x, y) for x in range(5, 11) for y in range(7, 11)}
        upper = {(x, y) for x in range(5, 11) for y in range(3, 6)}
        wall, found = windows(page, upright(24, 20))
        self.assertEqual(pixels_of(found), lower)
        lit = pixels_of(page_windows(page, [(0, 0, 24, 20)]))
        self.assertEqual(lit, lower | upper)   # the bar stays dark

    def test_none_of_the_pages_windows_is_lost(self):
        page = Page(24, 16)
        page.window(4, 4, 6, 4)
        glass = {(x, y) for x in range(5, 11) for y in range(5, 9)}
        # at the side of its drawing, and outside any drawing
        self.assertTrue(glass <= pixels_of(page_windows(page, [(0, 0, 11, 16)])))
        self.assertTrue(glass <= pixels_of(page_windows(page, [])))

    def test_a_window_is_one_window_however_many_drawings_see_it(self):
        page = Page(24, 16)
        page.window(4, 4, 6, 4)
        found = page_windows(page, [(0, 0, 24, 16), (0, 0, 24, 16), (2, 2, 20, 14)])
        self.assertEqual(len(found), 1)
        runs = window_glow.runs(3, found)
        self.assertEqual(sum(n for y, x, n, level in runs), 24)


MINT = (144, 216, 200)
MINT_SHADE = (120, 176, 168)
BLIND = (248, 248, 248)
STRIPE = (240, 192, 96)
BAR = (128, 128, 136)


class ShopWindowTests(unittest.TestCase):
    def test_a_shop_window_with_a_blind_and_a_stripe_glows(self):
        # Mauville's Game Corner: its frame holds the glass at the sides; a
        # blind over it and a stripe across it, a bar under it; the goods
        # behind the glass in its shade
        page = Page(40, 32)
        page.rect(4, 2, 26, 24, FRAME)
        page.rect(5, 3, 24, 1, BAR)
        page.rect(5, 4, 24, 2, BLIND)
        page.rect(5, 6, 24, 9, MINT)
        page.rect(5, 15, 24, 2, STRIPE)
        page.rect(5, 17, 24, 5, MINT)
        page.rect(5, 22, 24, 1, BAR)
        for y in list(range(10, 14)) + list(range(17, 22)):
            for x in (2, 3, 4, 5, 10, 11, 12, 13, 18, 19, 20, 21):
                page.px[y][5 + x] = MINT_SHADE
        page.rect(5, 14, 24, 1, MINT_SHADE)
        wall, found = windows(page, upright(40, 32))
        self.assertEqual(found, [])          # no pane by its framing
        lit = pixels_of(page_windows(page, [(0, 0, 40, 32)]))
        glass = {(x, y) for x in range(5, 29) for y in list(range(6, 15)) + list(range(17, 22))}
        self.assertTrue(glass <= lit)
        self.assertNotIn((10, 15), lit)      # the stripe stays dark
        self.assertNotIn((10, 22), lit)


STONE = (120, 128, 120)
STONE_DARK = (80, 88, 80)
CRATER_WALL = (216, 216, 176)
OUTLINE = (64, 72, 104)


class StoneWindowTests(unittest.TestCase):
    """Sootopolis's houses: windows, emblems and doors in the same greys."""

    def page(self):
        page = Page(48, 32)
        page.rect(0, 0, 48, 32, CRATER_WALL)
        return page

    def arch(self, page, x, y, glints=True):
        # inside: an arch 5 rows high (2 to 10 wide) over 6 rows 12 wide
        page.rect(x + 5, y - 1, 2, 1, OUTLINE)
        for r in range(5):
            half = r + 1
            page.rect(x + 6 - half - 1, y + r, 2 * half + 2, 1, OUTLINE)
            page.rect(x + 6 - half, y + r, 2 * half, 1, STONE)
        page.rect(x - 1, y + 5, 14, 6, OUTLINE)
        page.rect(x, y + 5, 12, 6, STONE)
        page.rect(x, y + 6, 12, 1, STONE_DARK)
        if glints:
            page.px[y + 7][x + 3] = page.px[y + 7][x + 8] = BLIND
        page.rect(x - 1, y + 11, 14, 1, BLIND)      # the sill

    def test_an_arched_window_glows(self):
        page = self.page()
        self.arch(page, 8, 6)
        lit = pixels_of(page_windows(page, [(0, 0, 48, 32)]))
        self.assertIn((11, 13), lit)          # a glint
        self.assertIn((14, 6), lit)           # the arch's top
        self.assertNotIn((7, 13), lit)        # the outline
        self.assertNotIn((10, 17), lit)       # the sill
        self.assertEqual(len(lit), 2 + 4 + 6 + 8 + 10 + 12 * 6)

    def test_an_emblem_or_a_door_does_not(self):
        page = self.page()
        self.arch(page, 8, 6, glints=False)   # no glint: an emblem's greys
        page.rect(27, 4, 14, 22, OUTLINE)     # a door: square, its knob
        page.rect(28, 5, 12, 20, STONE)
        page.px[15][29] = BLIND
        self.assertEqual(page_windows(page, [(0, 0, 48, 32)]), [])


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
