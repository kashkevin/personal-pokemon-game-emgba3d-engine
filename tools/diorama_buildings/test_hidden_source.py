#!/usr/bin/env python3
"""Tests for gen_voxel_buildings.hidden_source: where a wall's pixel hidden behind a piece standing
before it is drawn from.

A room's wall is filled where pieces before it hide it, so that from the diorama's cameras, which
see a little round each piece, the wall behind is a wall. Its own pixels a panel's width away served
for plain walls; behind the three plants of Sootopolis's Mystery Events house that copied the
doorway's yellow frame 16 columns on, and its bits showed between the pots (the object audit of
2026-11-14). A `fill_down` rectangle takes the column's own pixel above instead."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402

WALL, PLANT = 1, 0


def room(width, height, plant):
    """An owner grid: the wall (piece 1) everywhere but the plant's (piece 0) rectangle."""
    x0, y0, x1, y1 = plant
    return [[PLANT if x0 <= x < x1 and y0 <= y < y1 else WALL for x in range(width)]
            for y in range(height)]


class HiddenSourceTests(unittest.TestCase):
    def test_a_plain_wall_is_filled_a_panel_away_on_its_row(self):
        owner = room(40, 10, (16, 4, 24, 10))
        self.assertEqual(g.hidden_source(owner, {WALL}, 18, 6, 16), (2, 6))     # left first
        self.assertEqual(g.hidden_source(owner, {WALL}, 30, 6, 16), (14, 6))

    def test_the_other_side_when_the_first_is_hidden_too(self):
        owner = room(40, 10, (0, 4, 24, 10))
        self.assertEqual(g.hidden_source(owner, {WALL}, 18, 6, 16), (34, 6))

    def test_in_a_fill_down_rectangle_the_column_above(self):
        owner = room(40, 10, (16, 4, 24, 10))
        self.assertEqual(g.hidden_source(owner, {WALL}, 18, 6, 16, down=[(12, 1, 28, 10)]), (18, 3))
        self.assertEqual(g.hidden_source(owner, {WALL}, 23, 9, 16, down=[(12, 1, 28, 10)]), (23, 3))

    def test_the_column_above_only_inside_the_rectangle(self):
        # the rectangle starts under the plant's top: nothing of the wall above inside it, so the
        # pixel is filled as a plain wall's
        owner = room(40, 10, (16, 4, 24, 10))
        self.assertEqual(g.hidden_source(owner, {WALL}, 18, 6, 16, down=[(12, 5, 28, 10)]), (2, 6))

    def test_outside_the_rectangle_the_row_as_before(self):
        owner = room(40, 10, (16, 4, 24, 10))
        self.assertEqual(g.hidden_source(owner, {WALL}, 18, 6, 16, down=[(30, 1, 36, 10)]), (2, 6))

    def test_no_wall_to_draw_from(self):
        owner = room(16, 6, (0, 0, 16, 6))
        self.assertIsNone(g.hidden_source(owner, {WALL}, 5, 3, 16, down=[(0, 0, 16, 6)]))

    def test_a_lent_wall_when_the_room_shows_none_of_its_own(self):
        # Lavaridge's Center: west of its door the room's own wall hidden all along the row, east
        # of it the other Centers' wall, standing there as it is (place_reused)
        owner = room(64, 4, (0, 0, 40, 4))
        for y in range(4):
            for x in range(40, 64):
                owner[y][x] = g.LENT_WALL
        self.assertEqual(g.hidden_source(owner, {WALL}, 30, 2, 16), (46, 2))

    def test_its_own_wall_before_a_lent_one(self):
        owner = room(64, 4, (16, 0, 40, 4))
        for y in range(4):
            for x in range(40, 64):
                owner[y][x] = g.LENT_WALL
        self.assertEqual(g.hidden_source(owner, {WALL}, 30, 2, 16), (14, 2))     # not (46, 2)

    def test_reused_furniture_is_no_wall(self):
        owner = room(64, 4, (0, 0, 40, 4))
        for y in range(4):
            for x in range(40, 64):
                owner[y][x] = -1
        self.assertIsNone(g.hidden_source(owner, {WALL}, 30, 2, 16))


class PlacementGroundTests(unittest.TestCase):
    """The floor under a room's piece (gen_voxel_buildings.placement_ground): a card's cells are
    the room's floor, never their own drawing - which is the card's, and lay flat under it."""

    def test_a_room_s_own_drawing_under_its_pieces(self):
        self.assertEqual(g.placement_ground({"under": "own", "card": False}, 0x201), g.OWN_GROUND)

    def test_the_room_s_floor_under_a_card(self):
        self.assertEqual(g.placement_ground({"under": "own", "card": True}, 0x201), 0x201)

    def test_the_room_s_floor_without_own(self):
        self.assertEqual(g.placement_ground({"under": None, "card": False}, 0x201), 0x201)
        self.assertEqual(g.placement_ground({}, 0x23A), 0x23A)


if __name__ == "__main__":
    unittest.main()
