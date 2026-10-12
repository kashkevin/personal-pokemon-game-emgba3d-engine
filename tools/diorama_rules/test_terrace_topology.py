#!/usr/bin/env python3

import struct
import unittest
from pathlib import Path

from catalog import load_tilesets, load_world
from compile_rules import _load_cells, parse_behaviors
from terrace_topology import TERRACE_ROLES, lift_blocked_cells, role_for_cell, solve_layout


ROOT = Path(__file__).resolve().parents[2]


def cell(metatile=113):
    return {"tileset": "gTileset_General", "metatile": metatile,
            "localMetatile": metatile, "behavior": "MB_NORMAL", "elevation": 0}


class TerraceTopologyTests(unittest.TestCase):
    def test_role_grammar(self):
        self.assertEqual(TERRACE_ROLES[121].quadrants, (1, 1, 0, 0))
        self.assertEqual(TERRACE_ROLES[136].quadrants, (0, 1, 1, 0))
        self.assertEqual(TERRACE_ROLES[137].profile, "outer")
        self.assertEqual(TERRACE_ROLES[144].profile, "inner")
        self.assertTrue(TERRACE_ROLES[121].solid)
        self.assertFalse(TERRACE_ROLES[111].solid)
        self.assertIsNone(role_for_cell({"tileset": "other", "metatile": 121}))

    def test_horizontal_course_raises_complete_upper_region(self):
        cells = tuple(cell(113) if y != 1 else cell(121)
                      for y in range(3) for _ in range(3))
        solved = solve_layout(cells, 3, 3)
        self.assertEqual({solved[x]["groundQ16"] for x in range(3)}, {0})
        self.assertEqual((solved[4]["groundQ16"], solved[4]["topQ16"]), (-16, 0))
        self.assertEqual({solved[6 + x]["groundQ16"] for x in range(3)}, {-16})

    def test_blocked_cells_stand_on_lower_land(self):
        # A course along row 1; below it a house (blocked, row 3, middle) in
        # the lower region stands on the lower level, its own height on it.
        def walk(metatile=113):
            return cell(metatile)

        def wall():
            return dict(cell(500), collision=1)

        def lifted(cells):
            solved = solve_layout(cells, 3, 5)
            return lift_blocked_cells(cells, 3, 5, solved)

        cells = tuple(cell(121) if y == 1 else wall() if (y, x) == (3, 1) else walk()
                      for y in range(5) for x in range(3))
        self.assertEqual(lifted(cells)[3 * 3 + 1]["groundQ16"], -16)
        self.assertTrue(lifted(cells)[3 * 3 + 1]["lift"])
        # Not when the compiler says no (a natural cliff).
        self.assertEqual(lift_blocked_cells(cells, 3, 5, solve_layout(cells, 3, 5),
                                            liftable=lambda offset: False), {})
        # A blocked cell on the top level keeps the default (no entry).
        cells = tuple(cell(121) if y == 1 else wall() if (y, x) == (0, 1) else walk()
                      for y in range(5) for x in range(3))
        self.assertEqual(lifted(cells), {})
        # Nearest to water as much as to land, it is left as it was (a rock in the sea).
        cells = tuple(cell(121) if y == 1 else wall() if (y, x) == (3, 1)
                      else dict(walk(), behavior="MB_OCEAN_WATER") if (y, x) == (4, 1) else walk()
                      for y in range(5) for x in range(3))
        self.assertEqual(lifted(cells), {})
        # Two rows of them between the land and the sea: each takes its
        # nearest ground, the row on the land lifted, the row on the shore left.
        cells = tuple(cell(121) if y == 1 else wall() if y in (3, 4)
                      else dict(walk(), behavior="MB_OCEAN_WATER") if y == 5 else walk()
                      for y in range(6) for x in range(3))
        result = lift_blocked_cells(cells, 3, 6, solve_layout(cells, 3, 6))
        self.assertEqual({result[3 * 3 + x]["groundQ16"] for x in range(3)}, {-16})
        self.assertFalse(any(4 * 3 + x in result for x in range(3)))

    def test_contradictory_component_falls_back_entirely(self):
        # A horizontal role demands north high while this vertical role demands east high;
        # their shared ordinary region closes an inconsistent loop.
        cells = (cell(121), cell(121),
                 cell(144), cell(121))
        self.assertEqual(solve_layout(cells, 2, 2), {})

    def test_route104_source_coordinate_is_not_map_offset_adjusted(self):
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        layout = next(row for row in layouts if row["id"] == "LAYOUT_ROUTE104")
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        cells, _, _ = _load_cells(ROOT, [layout], tilesets, behavior_names)
        route = cells[layout["id"]]
        target = 66 * layout["width"] + 25
        self.assertEqual(route[target]["metatile"], 121)
        solved = solve_layout(route, layout["width"], layout["height"])
        self.assertEqual((solved[target]["groundQ16"], solved[target]["topQ16"],
                          solved[target]["profile"]), (-16, 0, "horizontal"))
        self.assertEqual(solved[target - layout["width"]]["groundQ16"], 0)
        self.assertEqual(solved[target + layout["width"]]["groundQ16"], -16)


if __name__ == "__main__":
    unittest.main()
