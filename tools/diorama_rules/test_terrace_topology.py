#!/usr/bin/env python3

import struct
import unittest
from pathlib import Path

from catalog import load_tilesets, load_world
from compile_rules import _load_cells, parse_behaviors
from terrace_topology import (CAVE_RISE_Q16, LEDGE_JUMPS, MOUNTAIN_LEDGE_Q16, SLOPE_RISE_Q16, TERRACE_ROLES,
                              cave_climbs, cave_levels, join_level_free_cells, join_water, lift_blocked_cells,
                              mountain_ledges, rock_tops, role_for_cell, seam_shifts, slope_tops, solve_layout)


ROOT = Path(__file__).resolve().parents[2]


def cell(metatile=113):
    return {"tileset": "gTileset_General", "metatile": metatile,
            "localMetatile": metatile, "behavior": "MB_NORMAL", "elevation": 0}


def ground():
    """Walkable ground of an elevation of its own (no terrace reaches it)."""
    return dict(cell(113), elevation=3)


def seam_layout(cells, width, height, mountains=()):
    members = {}
    solved = solve_layout(cells, width, height, members=members)
    return {"cells": cells, "width": width, "height": height, "members": members,
            "levels": {offset: entry["groundQ16"] for offset, entry in solved.items()},
            "mountains": frozenset(mountains)}


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

    def test_a_ledge_from_below_art_only_tops_lands_level(self):
        # A course whose top no one walks on (a wall over it), its low side's
        # ground and a ledge from that ground onto ground no terrace reaches: the
        # walkable ground lies on the top level, the course's top a level over it.
        def wall():
            return dict(cell(500), collision=1)

        def ledge():
            return dict(cell(500), behavior="MB_JUMP_SOUTH", collision=1)

        rows = [wall, cell, ledge, cell]
        cells = tuple(wall() for _ in range(3)) + tuple(cell(121) for _ in range(3)) + tuple(
            make() for make in rows[1:] for _ in range(3))
        solved = solve_layout(cells, 3, 5)
        self.assertEqual([(solved[offset]["groundQ16"], solved[offset]["topQ16"])
                          for offset in (3, 6)], [(0, 16), (0, 0)])
        self.assertTrue(all(offset not in solved for offset in range(12, 15)))
        # Without the ledge (a wall there) the course's top stays the top level.
        cells = cells[:9] + tuple(wall() for _ in range(3)) + cells[12:]
        solved = solve_layout(cells, 3, 5)
        self.assertEqual([(solved[offset]["groundQ16"], solved[offset]["topQ16"])
                          for offset in (3, 6)], [(-16, 0), (-16, -16)])

    def test_a_component_moves_by_its_shift_and_its_regions_are_named(self):
        # (the course of test_horizontal_course_raises_complete_upper_region, ground
        # no terrace reaches under a wall below it)
        cells = (tuple(cell(113) if y != 1 else cell(121) for y in range(3) for _ in range(3))
                 + tuple(dict(cell(500), collision=1) for _ in range(3)) + tuple(ground() for _ in range(3)))
        members = {}
        solved = solve_layout(cells, 3, 5, members=members)
        self.assertEqual(members, {**{offset: 0 for offset in (0, 1, 2, 6, 7, 8)},
                                   **{offset: -13 for offset in (12, 13, 14)}})
        self.assertTrue(all(offset not in solved for offset in (12, 13, 14)))
        moved = solve_layout(cells, 3, 5, shifts={0: 1, -13: 2})
        self.assertEqual([(moved[offset]["groundQ16"], moved[offset]["topQ16"]) for offset in (0, 4, 6, 12)],
                         [(16, 16), (0, 16), (0, 0), (32, 32)])

    def test_a_maps_ground_moves_to_meet_the_next_maps(self):
        # MS-162: a route a level under its top (cliffs' tops) meets a town's
        # ground: the route's component moves up a level.
        route = tuple(cell(113) if y != 1 else cell(121) for y in range(3) for _ in range(3))
        town = tuple(ground() for _ in range(3))
        layouts = {"route": seam_layout(route, 3, 3), "town": seam_layout(town, 3, 1),
                   "north": seam_layout(town, 3, 1)}
        south = [(("route", 6 + x), ("town", x)) for x in range(3)]
        self.assertEqual(seam_shifts(layouts, south), {"route": {0: 1}})
        # ground of the route at another seam at its level already: they disagree, nothing moves
        north = [(("route", x), ("north", x)) for x in range(3)]
        self.assertEqual(seam_shifts(layouts, south + north), {})

    def test_a_move_that_would_make_a_ledge_jump_up_is_refused(self):
        # ground no terrace reaches, a ledge from it onto the course's top (level
        # with it), the course, its foot at the seam a level under the town
        def ledge():
            return dict(cell(500), behavior="MB_JUMP_SOUTH", collision=1)

        rows = [ground, ledge, lambda: cell(113), lambda: cell(121), lambda: cell(113)]
        route = tuple(make() for make in rows for _ in range(3))
        town = tuple(ground() for _ in range(3))
        south = [(("route", 12 + x), ("town", x)) for x in range(3)]
        layouts = {"route": seam_layout(route, 3, 5), "town": seam_layout(town, 3, 1)}
        self.assertEqual(seam_shifts(layouts, south), {})
        walled = tuple(dict(cell(500), collision=1) if 3 <= offset < 6 else entry
                       for offset, entry in enumerate(route))
        layouts["route"] = seam_layout(walled, 3, 5)
        self.assertEqual(seam_shifts(layouts, south), {"route": {6: 1}})

    def test_the_ground_round_a_mountain_moves_together(self):
        # the course (columns 0-2), a mountain (column 3), ground no terrace
        # reaches beyond it (column 4): with the mountain's art the ground beyond
        # moves with the course
        route = tuple(dict(cell(500), collision=1) if x == 3 else ground() if x == 4
                      else cell(121) if y == 1 else cell(113)
                      for y in range(3) for x in range(5))
        town = tuple(ground() for _ in range(3))
        south = [(("route", 10 + x), ("town", x)) for x in range(3)]
        layouts = {"route": seam_layout(route, 5, 3), "town": seam_layout(town, 3, 1)}
        self.assertEqual(seam_shifts(layouts, south), {"route": {0: 1}})
        layouts["route"] = seam_layout(route, 5, 3, mountains=(3, 8, 13))
        self.assertEqual(seam_shifts(layouts, south), {"route": {0: 1, -5: 1}})

    def test_route123_and_route113_ledges_jump_down(self):
        # Route 123's tall grass under its mountain and Route 113's route under its
        # cliffs lay a level below the rest of the route (MS-154): every jump from
        # them onto it went up a level. Now no ledge on either lands higher than it
        # leaves, the grass and the route lie on the top level, Route 113's ends on
        # Fallarbor Town's and Route 111's level, the mountain's top a level over it.
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        rows = [row for row in layouts if row["id"] in ("LAYOUT_ROUTE123", "LAYOUT_ROUTE113")]
        cells, _, _ = _load_cells(ROOT, rows, tilesets, behavior_names)
        levels = {}
        for layout in rows:
            width, height, route = layout["width"], layout["height"], cells[layout["id"]]
            solved = levels[layout["id"]] = solve_layout(route, width, height)
            jumps = 0
            for offset, cell in enumerate(route):
                step = LEDGE_JUMPS.get(cell.get("behavior"))
                x, y = offset % width, offset // width
                if step is None or not (0 <= x - step[0] < width and 0 <= y - step[1] < height
                                        and 0 <= x + step[0] < width and 0 <= y + step[1] < height):
                    continue
                takeoff = (y - step[1]) * width + x - step[0]
                landing = (y + step[1]) * width + x + step[0]
                if route[takeoff].get("collision", 0) or route[landing].get("collision", 0):
                    continue
                jumps += 1
                self.assertLessEqual(solved.get(landing, {"groundQ16": 0})["groundQ16"],
                                     solved.get(takeoff, {"groundQ16": 0})["groundQ16"],
                                     (layout["id"], x, y))
            self.assertGreater(jumps, 20, layout["id"])
        width = next(row for row in rows if row["id"] == "LAYOUT_ROUTE123")["width"]
        self.assertEqual(levels["LAYOUT_ROUTE123"][8 * width + 52]["groundQ16"], 0)
        self.assertEqual((levels["LAYOUT_ROUTE123"][3 * width + 50]["groundQ16"],
                          levels["LAYOUT_ROUTE123"][3 * width + 50]["topQ16"]), (0, 16))
        width = next(row for row in rows if row["id"] == "LAYOUT_ROUTE113")["width"]
        for x, y in ((66, 5), (0, 8), (99, 8)):
            self.assertEqual(levels["LAYOUT_ROUTE113"][y * width + x]["groundQ16"], 0, (x, y))

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
        # Water solved with the land (the sea a level below the rocks in it)
        # offers its level too: a rock in it stands in it.
        cells = tuple(cell(121) if y == 1 else wall() if (y, x) == (3, 1)
                      else dict(walk(), behavior="MB_OCEAN_WATER") if (y, x) == (4, 1) else walk()
                      for y in range(5) for x in range(3))
        self.assertEqual(lifted(cells)[3 * 3 + 1]["groundQ16"], -16)

        # Nearest to unsolved water as much as to land (the sea at its own
        # height, at another elevation and by no terrace), it is left as it was.
        def sea():
            return dict(walk(), behavior="MB_OCEAN_WATER", elevation=1)

        cells = tuple(cell(121) if y == 1 else wall() if (y, x) == (3, 1)
                      else sea() if (y, x) == (4, 1) else walk()
                      for y in range(5) for x in range(3))
        self.assertNotIn(4 * 3 + 1, solve_layout(cells, 3, 5))
        self.assertEqual(lifted(cells), {})
        # Two rows of them between the land and that sea: each takes its
        # nearest ground, the row on the land lifted, the row on the shore left.
        cells = tuple(cell(121) if y == 1 else wall() if y in (3, 4)
                      else sea() if y == 5 else walk()
                      for y in range(6) for x in range(3))
        result = lift_blocked_cells(cells, 3, 6, solve_layout(cells, 3, 6))
        self.assertEqual({result[3 * 3 + x]["groundQ16"] for x in range(3)}, {-16})
        self.assertFalse(any(4 * 3 + x in result for x in range(3)))

    def test_shallows_join_the_solved_sea(self):
        # A course along row 1; below it the sea (same elevation: solved a level
        # down), and shallows at another elevation joining it on row 4: they
        # take the sea's level. Beyond a waterfall, nothing does.
        def water(behavior="MB_OCEAN_WATER", elevation=0):
            return dict(cell(), behavior=behavior, elevation=elevation)

        cells = tuple(cell(121) if y == 1 else water() if y in (2, 3)
                      else water("MB_SHALLOW_WATER", 3) if y == 4
                      else water("MB_WATERFALL", 3) if y == 5
                      else water("MB_SHALLOW_WATER", 3) if y == 6 else cell()
                      for y in range(7) for x in range(3))
        solved = solve_layout(cells, 3, 7)
        self.assertEqual(solved[2 * 3]["groundQ16"], -16)
        self.assertNotIn(4 * 3, solved)
        joined = join_water(cells, 3, 7, solved)
        self.assertEqual({joined[4 * 3 + x]["groundQ16"] for x in range(3)}, {-16})
        self.assertFalse(any(offset in joined for offset in range(5 * 3, 7 * 3)))
        # Touching the solved water at two levels: left as it was.
        self.assertEqual(join_water(cells, 3, 7, {**solved, 3 * 3 + 2: dict(solved[3 * 3 + 2],
                                                                        groundQ16=0)}),
                         {})

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


    def test_a_caves_floors_stand_at_their_elevation(self):
        # a floor of elevation 3 with a platform of 4 and a ledge of 5 on it,
        # a wall, a pool (1), a bridge (15) and stairs (0): the lowest floor
        # stays, the highest a tile up, the one between half way; the rest as
        # they are
        def floor(elevation, collision=0, behavior="MB_NORMAL"):
            return {"elevation": elevation, "collision": collision, "behavior": behavior,
                    "tileset": "gTileset_Cave", "metatile": 0}
        cells = ([floor(3)] * 6 + [floor(4)] * 3 + [floor(5)] + [floor(3, collision=1)]
                 + [floor(1, behavior="MB_POND_WATER"), floor(15), floor(0)])
        levels = cave_levels(tuple(cells), len(cells), 1)
        self.assertEqual(set(levels), {6, 7, 8, 9})
        self.assertEqual(levels[6]["groundQ16"], CAVE_RISE_Q16 // 2)
        self.assertEqual(levels[9]["groundQ16"], CAVE_RISE_Q16)
        self.assertEqual(levels[9]["topQ16"], levels[9]["groundQ16"])
        self.assertFalse(levels[6]["solid"])
        # one floor: nothing; a cell another solver placed: left to it
        self.assertEqual(cave_levels(tuple([floor(3)] * 4), 4, 1), {})
        self.assertNotIn(6, cave_levels(tuple(cells), len(cells), 1, {6: {"groundQ16": 0}}))

    def test_a_caves_bridge_stands_at_the_floor_it_joins(self):
        # 3 3 15 15 4 4: the bridge reaches the upper floor
        def floor(elevation):
            return {"elevation": elevation, "collision": 0, "behavior": "MB_NORMAL"}
        cells = tuple(floor(e) for e in (3, 3, 15, 15, 4, 4))
        levels = cave_levels(cells, 6, 1)
        self.assertEqual(levels[2]["groundQ16"], CAVE_RISE_Q16)
        self.assertEqual(levels[3]["groundQ16"], CAVE_RISE_Q16)
        self.assertTrue(levels[2]["deck"])          # a deck, not a block
        self.assertNotIn(0, levels)

    def test_stairs_raise_a_mountains_walkable_top_a_level(self):
        # Route 112: rock round a walkable top (MB_MOUNTAIN_TOP), General stairs
        # (175) climbing to it from the grass below: the top stands a level up,
        # the stairs rise between; the grass keeps its level (no entry).
        def rock():
            return {"tileset": "gTileset_Lavaridge", "metatile": 625, "localMetatile": 113,
                    "behavior": "MB_MOUNTAIN_TOP", "collision": 1, "elevation": 0}

        def top():
            return dict(rock(), collision=0, elevation=3)

        def grass():
            return dict(cell(1), collision=0, elevation=3)

        def stairs():
            return dict(cell(175), collision=0, elevation=3)

        rows = ["RRRRR", "RTTTR", "RTTTR", "RRSRR", "GGGGG"]
        make = {"R": rock, "T": top, "G": grass, "S": stairs}
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 5, 5, {})
        self.assertEqual({offset for offset, entry in raised.items()
                          if entry["profile"] == "none"}, {6, 7, 8, 11, 12, 13})
        self.assertEqual({raised[6]["groundQ16"], raised[13]["topQ16"]}, {MOUNTAIN_LEDGE_Q16})
        self.assertEqual((raised[17]["groundQ16"], raised[17]["topQ16"], raised[17]["rise"],
                          raised[17]["profile"]), (0, 16, 16, "transition-horizontal"))
        self.assertFalse(any(offset in raised for offset in range(20, 25)))
        # Stairs three rows long rise the one level over their rows.
        rows = ["RRRRR", "RTTTR", "RRSRR", "RRSRR", "RRSRR", "GGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 5, 6, {})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 17, 12)], [(0, 5), (5, 11), (11, 16)])
        # A second top up stairs from the first (Route 115's, Route 114's): a level a climb.
        rows = ["RRRRR", "RTTTR", "RRSRR", "RTTTR", "RRSRR", "GGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 5, 6, {})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 16, 12, 6)], [(0, 16), (16, 16), (16, 32), (32, 32)])
        # Stairs between ordinary ground: the terrace solver's.
        rows = ["GGGGG", "RRSRR", "GGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        self.assertEqual(mountain_ledges(cells, 5, 3, {}), {})
        # A top beside walkable ground of another elevation (the game lets no one cross)
        # stands a level over it at its north edge or its side (the game draws the top's
        # edge there); over it at its south edge, with no face drawn for it, the layout
        # keeps its levels.
        def other(ch):
            return dict(make[ch](), elevation=4)

        rows = ["RGGGR", "RTTTG", "RTTTR", "RRSRR", "GGGGG"]
        cells = tuple(other(ch) if ch == "G" and (y == 0 or (y, x) == (1, 4)) else make[ch]()
                      for y, row in enumerate(rows) for x, ch in enumerate(row))
        raised = mountain_ledges(cells, 5, 5, {})
        self.assertEqual(raised[6]["groundQ16"], MOUNTAIN_LEDGE_Q16)
        self.assertFalse({1, 9} & set(raised))
        rows = ["RRRRR", "RTTTR", "RTTTR", "RGSRR", "GGGGG"]
        cells = tuple(other(ch) if (y, x) == (3, 1) else make[ch]()
                      for y, row in enumerate(rows) for x, ch in enumerate(row))
        self.assertEqual(mountain_ledges(cells, 5, 5, {}), {})

    def test_route112_plateau_stands_a_level_over_the_ground(self):
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        wanted = {"LAYOUT_ROUTE112", "LAYOUT_ROUTE114", "LAYOUT_ROUTE115", "LAYOUT_ROUTE111",
                  "LAYOUT_ROUTE119"}
        rows = [row for row in layouts if row["id"] in wanted]
        cells, _, _ = _load_cells(ROOT, rows, tilesets, behavior_names)
        layout = next(row for row in rows if row["id"] == "LAYOUT_ROUTE112")
        width = layout["width"]
        route = cells[layout["id"]]
        raised = mountain_ledges(route, width, layout["height"],
                                 solve_layout(route, width, layout["height"]))
        # the plateau round the cable car station, the Fiery Path's mouth on it
        for x, y in ((24, 33), (12, 38), (11, 36)):
            self.assertEqual(raised[y * width + x]["groundQ16"], MOUNTAIN_LEDGE_Q16)
        # the stairs up to it (20-21, 41-43) over their three rows; the ground below stays
        self.assertEqual([raised[y * width + 20]["topQ16"] for y in (43, 42, 41)], [5, 11, 16])
        self.assertNotIn(45 * width + 20, raised)
        # Route 115's mountain climbs to three tops, a level a climb (MS-153): its plateau up
        # two rows of stairs, a lower top a row of stairs under it (on the grass's level), the
        # muddy slope up to the highest, rising over its rows; the pocket of that top in the
        # rock north of it on its level. Route 114's mountain climbs to five levels; Route 119's
        # top runs onto its bridge (elevation 15, which the game lets one walk across): it keeps
        # its levels.
        def entries(name):
            layout = next(row for row in rows if row["id"] == name)
            raised = mountain_ledges(cells[name], layout["width"], layout["height"],
                                     solve_layout(cells[name], layout["width"], layout["height"]))
            return lambda x, y: (None if y * layout["width"] + x not in raised
                                 else raised[y * layout["width"] + x])

        at = entries("LAYOUT_ROUTE115")
        for (x, y), levels in (((24, 10), (16, 16)), ((32, 12), (32, 32)), ((28, 9), (16, 24)),
                               ((28, 8), (24, 32)), ((18, 13), (0, 8)), ((18, 12), (8, 16)),
                               ((27, 12), (0, 16)), ((24, 42), (16, 32)), ((25, 40), (32, 32))):
            self.assertEqual((at(x, y)["groundQ16"], at(x, y)["topQ16"]), levels, (x, y))
        for x, y in ((24, 16), (17, 15)):
            self.assertIsNone(at(x, y), (x, y))
        self.assertEqual(at(33, 2)["groundQ16"], 32)
        at = entries("LAYOUT_ROUTE114")
        for (x, y), levels in (((12, 36), (16, 16)), ((6, 48), (32, 32)), ((13, 68), (48, 48)),
                               ((24, 66), (64, 64)), ((14, 49), (0, 16)), ((13, 67), (48, 64))):
            self.assertEqual((at(x, y)["groundQ16"], at(x, y)["topQ16"]), levels, (x, y))
        self.assertIsNone(at(12, 35))
        layout = next(row for row in rows if row["id"] == "LAYOUT_ROUTE119")
        self.assertEqual(mountain_ledges(cells["LAYOUT_ROUTE119"], layout["width"], layout["height"],
                                         solve_layout(cells["LAYOUT_ROUTE119"], layout["width"],
                                                      layout["height"])), {})

    def test_a_muddy_slope_climbs_to_a_mountains_top_as_stairs_do(self):
        # Route 111: the rock plateau north of the desert has no stairs; the Mach Bike's muddy
        # slopes (MB_MUDDY_SLOPE) are its ways up. Its walkable top stands a level up, the slope
        # rises over its rows, the ground below keeps its level.
        def rock():
            return {"tileset": "gTileset_Lavaridge", "metatile": 625, "localMetatile": 113,
                    "behavior": "MB_MOUNTAIN_TOP", "collision": 1, "elevation": 0}

        def top():
            return dict(rock(), collision=0, elevation=3)

        def ground():
            return dict(cell(1), collision=0, elevation=3)

        def slope():
            return dict(cell(1), behavior="MB_MUDDY_SLOPE", collision=0, elevation=3)

        rows = ["RRRRR", "RTTTR", "RTTTR", "RRMRR", "RRMRR", "GGGGG"]
        make = {"R": rock, "T": top, "G": ground, "M": slope}
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 5, 6, {})
        self.assertEqual({offset for offset, entry in raised.items()
                          if entry["profile"] == "none"}, {6, 7, 8, 11, 12, 13})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 17)], [(0, 8), (8, 16)])
        self.assertFalse(any(offset in raised for offset in range(25, 30)))
        # A pocket of the top no one walks into (blocked all round) lies on the raised level too; without a raised top
        # in the layout it keeps its level.
        rows = ["RRRRRRR", "RTTTRTR", "RTTTRRR", "RRMRRRR", "RRMRRRR", "GGGGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 7, 6, {})
        self.assertEqual(raised[12]["groundQ16"], MOUNTAIN_LEDGE_Q16)
        rows = ["RRRRRRR", "RTTTRTR", "RTTTRRR", "RRRRRRR", "GGGGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        self.assertEqual(mountain_ledges(cells, 7, 5, {}), {})

    def test_a_caves_muddy_slope_climbs_from_one_part_of_its_floor_to_another(self):
        # Granite Cave B1F: its halves, at one elevation, are joined only by a muddy slope. The
        # part at its top (north) stands a level up, the slope rising over its rows; a cave with
        # no such slope, or whose top would meet its foot where the game lets one walk across (a
        # bridge's elevation, 15), keeps its levels.
        def rock():
            return {"tileset": "gTileset_Cave", "metatile": 1, "behavior": "MB_NORMAL",
                    "collision": 1, "elevation": 0}

        def floor(elevation=3):
            return {"tileset": "gTileset_Cave", "metatile": 2, "behavior": "MB_NORMAL",
                    "collision": 0, "elevation": elevation}

        def slope():
            return dict(floor(), behavior="MB_MUDDY_SLOPE")

        make = {"#": rock, ".": floor, "M": slope, "B": lambda: floor(15)}
        rows = ["#####", "#...#", "##M##", "##M##", "#...#", "#####"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = cave_climbs(cells, 5, 6)
        self.assertEqual({offset for offset, entry in raised.items() if entry["profile"] == "none"},
                         {6, 7, 8})
        self.assertEqual({raised[6]["groundQ16"]}, {CAVE_RISE_Q16})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"]) for offset in (17, 12)],
                         [(0, 8), (8, 16)])
        self.assertFalse(any(offset in raised for offset in (21, 22, 23)))
        rows = ["#####", "#...#", "#####", "#...#", "#####"]
        self.assertEqual(cave_climbs(tuple(make[ch]() for row in rows for ch in row), 5, 5), {})
        rows = ["#####", "#...#", "##MB#", "##M.#", "#...#", "#####"]
        self.assertEqual(cave_climbs(tuple(make[ch]() for row in rows for ch in row), 5, 6), {})
        # the repository's: Granite Cave B1F's north half a level up, its slope (9,9-10) a ramp
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        layout = next(row for row in layouts if row["id"] == "LAYOUT_GRANITE_CAVE_B1F")
        cave = _load_cells(ROOT, [layout], tilesets, names)[0]["LAYOUT_GRANITE_CAVE_B1F"]
        width = layout["width"]
        raised = cave_climbs(cave, width, layout["height"], cave_levels(cave, width, layout["height"]))
        self.assertEqual(raised[8 * width + 9]["groundQ16"], CAVE_RISE_Q16)
        self.assertEqual([(raised[y * width + 9]["groundQ16"], raised[y * width + 9]["topQ16"])
                          for y in (10, 9)], [(0, 8), (8, 16)])
        self.assertNotIn(12 * width + 9, raised)

    def test_a_slopes_top_takes_the_level_of_the_ground_round_it(self):
        # Route 119: its muddy slope climbs to a row at the elevation of its foot, which the GBA
        # draws on the plateau, a row of elevation 0 joining it to the rest; the solver put the
        # row on the foot's level, a step under the plateau, the slope flat. The row takes the
        # plateau's level, the slope a ramp up to it, the mountain's rock beside the slope and
        # under the row on the foot; a top with nothing round it, or with ground round it at
        # another level, or one region with its foot, keeps its level.
        def rock():
            return {"tileset": "gTileset_Fortree", "metatile": 600, "behavior": "MB_NORMAL",
                    "collision": 1, "elevation": 0}

        def land(elevation=3):
            return {"tileset": "gTileset_Fortree", "metatile": 601, "behavior": "MB_NORMAL",
                    "collision": 0, "elevation": elevation}

        make = {"#": rock, "p": lambda: land(4), "z": lambda: land(0), "t": land, "f": land,
                "M": lambda: dict(land(), behavior="MB_MUDDY_SLOPE")}

        def climb(rows, low="tMf", mountain=frozenset()):
            cells = tuple(make[ch]() for row in rows for ch in row)
            # the solver's levels: the slope, its top and foot a level under the plateau
            solved = {offset: {"groundQ16": -SLOPE_RISE_Q16, "topQ16": -SLOPE_RISE_Q16,
                               "profile": "none", "solid": False}
                      for offset, ch in enumerate("".join(rows)) if ch in low}
            return slope_tops(cells, len(rows[0]), len(rows), solved, rock=mountain)

        rows = ["#####", "#ppp#", "#zzz#", "#ttt#", "##M##", "##M##", "#fff#", "#####"]
        raised = climb(rows, mountain=frozenset({20, 21, 23, 24, 26, 28}))
        self.assertEqual({offset for offset, entry in raised.items() if entry["profile"] == "none"
                          and not entry.get("lift")}, {16, 17, 18})
        self.assertEqual({raised[offset]["groundQ16"] for offset in (16, 17, 18)}, {0})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"]) for offset in (27, 22)],
                         [(-16, -8), (-8, 0)])
        # the face: the rock beside the slope and under the row, on the foot
        self.assertEqual({offset: (entry["groundQ16"], entry["lift"]) for offset, entry in raised.items()
                          if entry.get("lift")}, dict.fromkeys((21, 23, 26, 28), (-16, True)))
        self.assertFalse(any(offset in raised for offset in (31, 32, 33)))
        self.assertFalse(any(entry.get("lift") for entry in climb(rows).values()))
        # nothing round the top: the slope its only way up
        self.assertEqual(climb(["#####", "#####", "#ttt#", "##M##", "##M##", "#fff#", "#####"]), {})
        # ground round the top on the foot's level
        self.assertEqual(climb(rows, low="tMfz"), {})
        # its top and foot one region (a way round at their elevation)
        self.assertEqual(climb(["#####", "#ppp#", "#zzz#", "#ttt#", "#tM##", "#tM##", "#fff#", "#####"]), {})
        # the repository's: Route 119's row (5-8,53) on the plateau's level, its slope (6,54-55) a ramp
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        layout = next(row for row in layouts if row["id"] == "LAYOUT_ROUTE119")
        route = _load_cells(ROOT, [layout], tilesets, names)[0]["LAYOUT_ROUTE119"]
        width, height = layout["width"], layout["height"]
        solved = solve_layout(route, width, height)
        raised = slope_tops(route, width, height, {**solved, **join_water(route, width, height, solved)})
        self.assertEqual({(offset % width, offset // width, entry["groundQ16"])
                          for offset, entry in raised.items() if entry["profile"] == "none"},
                         {(x, 53, 0) for x in range(5, 9)})
        self.assertEqual([(raised[y * width + 6]["groundQ16"], raised[y * width + 6]["topQ16"])
                          for y in (55, 54)], [(-16, -8), (-8, 0)])

    def test_rock_takes_the_top_of_the_ground_nearest_it(self):
        # A column of rock between a top three levels up (north) and ground asking the rock a
        # level over it (south), three rows: a level a row down from the top (the game draws a
        # face a tile a level).
        self.assertEqual(rock_tops(frozenset({4, 7, 10}), {1: 48, 13: 16}, 3, 5, 16),
                         {4: 48, 7: 32, 10: 16})
        # A block of rock inside a top (its path all round): level with it, no pit in its middle.
        rock = frozenset(y * 5 + x for x in (1, 2, 3) for y in (1, 2, 3))
        ground = {offset: 32 for offset in range(25) if offset not in rock}
        self.assertEqual(set(rock_tops(rock, ground, 5, 5, 16).values()), {32})
        # Rock no ground reaches: the floor.
        self.assertEqual(rock_tops(frozenset({0}), {}, 1, 1, 16), {0: 16})

    def test_route111_plateau_stands_a_level_over_the_desert(self):
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        rows = [row for row in layouts if row["id"] in ("LAYOUT_ROUTE111", "LAYOUT_ROUTE111_NO_MIRAGE_TOWER")]
        cells, _, _ = _load_cells(ROOT, rows, tilesets, behavior_names)
        for layout in rows:
            width, route = layout["width"], cells[layout["id"]]
            raised = mountain_ledges(route, width, layout["height"],
                                     solve_layout(route, width, layout["height"]))
            # the plateau and its arm north to the second slope
            for x, y in ((16, 33), (24, 30), (34, 15), (30, 4)):
                self.assertEqual(raised[y * width + x]["groundQ16"], MOUNTAIN_LEDGE_Q16, (layout["id"], x, y))
            # its two muddy slopes over their two rows, the desert and the grass below stay
            for x, south, north in ((17, 37, 36), (29, 7, 6)):
                self.assertEqual((raised[south * width + x]["topQ16"], raised[north * width + x]["topQ16"]),
                                 (8, 16), (layout["id"], x))
            for x, y in ((17, 38), (29, 8), (24, 40)):
                self.assertNotIn(y * width + x, raised)
            # the pockets of the top no one walks into: south of the plateau and in the east cliff
            for x, y in ((20, 34), (20, 36), (38, 83)):
                self.assertEqual(raised[y * width + x]["groundQ16"], MOUNTAIN_LEDGE_Q16, (layout["id"], x, y))

    def test_a_cave_mouth_takes_the_level_it_is_entered_from(self):
        # Route 116's way into Rusturf Tunnel (elevation 0, a door) in the rock's
        # face over grass the terrace solver put a level down: on the grass's level.
        def mouth(elevation=0):
            return dict(cell(167), behavior="MB_NON_ANIMATED_DOOR", collision=0,
                        elevation=elevation)

        def rock():
            return dict(cell(113), behavior="MB_MOUNTAIN_TOP", collision=1)

        cells = (rock(), mouth(), rock(),
                 dict(cell(1), collision=0, elevation=3), dict(cell(1), collision=0, elevation=3),
                 dict(cell(1), collision=0, elevation=3))
        solved = {3: {"groundQ16": -16, "topQ16": -16, "profile": "none", "solid": False},
                  4: {"groundQ16": -16, "topQ16": -16, "profile": "none", "solid": False},
                  5: {"groundQ16": -16, "topQ16": -16, "profile": "none", "solid": False}}
        self.assertEqual(join_level_free_cells(cells, 3, 2, solved)[1]["groundQ16"], -16)
        self.assertEqual(join_level_free_cells(cells, 3, 2, solved,
                                               beside=frozenset((0, 2)))[1]["groundQ16"], -16)
        # not beside the given cells; on the top level (where it lies already); entered from
        # the water (Shoal Cave's); an ordinary cell of no elevation: no entry
        self.assertEqual(join_level_free_cells(cells, 3, 2, solved, beside=frozenset()), {})
        top = {offset: dict(entry, groundQ16=0, topQ16=0) for offset, entry in solved.items()}
        self.assertEqual(join_level_free_cells(cells, 3, 2, top), {})
        wet = cells[:4] + (dict(cells[4], behavior="MB_SHALLOW_WATER"),) + cells[5:]
        self.assertEqual(join_level_free_cells(wet, 3, 2, solved), {})
        plain = cells[:1] + (dict(cells[1], behavior="MB_NORMAL"),) + cells[2:]
        self.assertEqual(join_level_free_cells(plain, 3, 2, solved), {})

if __name__ == "__main__":
    unittest.main()
