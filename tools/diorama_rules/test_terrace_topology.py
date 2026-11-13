#!/usr/bin/env python3

import struct
import unittest
from pathlib import Path

from catalog import load_tilesets, load_world
from compile_rules import CAVE_HAND_LEVELS, _load_cells, parse_behaviors
from terrace_topology import (CAVE_RISE_Q16, LEDGE_JUMPS, MOUNTAIN_LEDGE_Q16, SLOPE_RISE_Q16, TERRACE_ROLES,
                              contradiction_ramps, course_ridges, cave_climbs, cave_hand_levels, cave_levels,
                              cave_stairs,
                              join_level_free_cells, join_water, lift_blocked_cells,
                              hall_stairs, hand_banks, hand_decks, hand_floors, hand_ridges, hand_slopes,
                              indoor_levels,
                              climb_levels, mountain_by_hand, stair_runs_between, stair_terraces,
                              mountain_ledges, pass_levels, rock_tops, role_for_cell, seam_shifts, slope_tops,
                              solve_layout)


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

    def test_ground_no_terrace_reaches_lies_flush_with_the_ground_it_is_walked_onto_from(self):
        # MS-162: the Battle Frontier west half's paths of elevation 3 run through its
        # lower ground of elevation 0 (under its plaza's cliffs), walked onto from it
        # with nothing between them; they stood on the top level, a level over it.
        # Ground no terrace reaches beside a component's ground on one level other
        # than the top takes that level and the component.
        def wall():
            return dict(cell(500), collision=1)

        course = tuple(cell(113) if y != 1 else cell(121) for y in range(3) for _ in range(3))
        members = {}
        solved = solve_layout(course + tuple(ground() for _ in range(3)), 3, 4, members=members)
        self.assertEqual({solved[9 + x]["groundQ16"] for x in range(3)}, {-16})
        self.assertEqual({members[9 + x] for x in range(3)}, {0})
        self.assertFalse(any("doorway" in solved[9 + x] for x in range(3)))
        # Beside the top level it lies there as before, ground no terrace reaches.
        members = {}
        solved = solve_layout(tuple(ground() for _ in range(3)) + course, 3, 4, members=members)
        self.assertTrue(all(x not in solved for x in range(3)))
        self.assertEqual({members[x] for x in range(3)}, {-1})
        # Beside the component's ground on two levels (through ground beside it of
        # another elevation): it keeps its place.
        high = dict(ground(), elevation=4)
        rows = [(cell(113), cell(113), cell(113), ground(), ground()),
                (cell(121), cell(121), cell(121), wall(), high),
                (cell(113), cell(113), cell(113), high, high)]
        solved = solve_layout(tuple(entry for row in rows for entry in row), 5, 3)
        self.assertTrue(all(offset not in solved for offset in (3, 4, 9, 13, 14)))
        # Beside water no component levels (the open sea's): it keeps its place.
        water = dict(cell(113), behavior="MB_OCEAN_WATER", elevation=1)
        solved = solve_layout(course + (ground(), ground(), water), 3, 4)
        self.assertTrue(all(offset not in solved for offset in (9, 10, 11)))
        # A doorway joined through it lies on that level too, marked so that it
        # lifts no rock round it (a cave mouth's way).
        door = dict(cell(113), behavior="MB_NON_ANIMATED_DOOR")
        solved = solve_layout(course + tuple(ground() for _ in range(3)) + (wall(), door, wall()), 3, 5)
        self.assertEqual((solved[13]["groundQ16"], solved[13].get("doorway")), (-16, True))
        self.assertEqual(solved[10]["groundQ16"], -16)

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

    def test_a_blocked_cell_between_two_levels_stands_on_the_one_asked(self):
        # A rock between ground a level down and ground two levels down: left as it was, unless `between` names
        # the lower (the Safari Zone's north-east) or the higher (its north, the rock edge its raised sand's rim).
        def walk():
            return dict(cell(113), collision=0)

        rock = dict(cell(500), collision=1)
        cells = (walk(), rock, walk())
        solved = {0: {"groundQ16": -16}, 2: {"groundQ16": -32}}
        self.assertEqual(lift_blocked_cells(cells, 3, 1, solved), {})
        self.assertEqual(lift_blocked_cells(cells, 3, 1, solved, between="lowest")[1]["groundQ16"], -32)
        self.assertEqual(lift_blocked_cells(cells, 3, 1, solved, between="highest")[1]["groundQ16"], -16)
        # A jump ledge drawn with a terrace course's piece (General 135) the solver left unplaced lifts only with
        # `ledges` (the Safari Zone's north: its ring of ledges stood a level over the sand round it).
        ledge = dict(cell(135), collision=1, behavior="MB_JUMP_SOUTH")
        cells = (walk(), ledge, walk())
        solved = {0: {"groundQ16": -16}, 2: {"groundQ16": -16}}
        self.assertEqual(lift_blocked_cells(cells, 3, 1, solved), {})
        self.assertEqual(lift_blocked_cells(cells, 3, 1, solved, ledges=True)[1]["groundQ16"], -16)

    def test_the_safari_zones_north_climbs_its_bumpy_slope(self):
        # The Safari Zone's north: its bumpy slope at (22, 23), of elevation 0 between its raised sand and the grass
        # south of it, a ramp up from the grass to the sand (compile_rules' RAMP_HINTS), nothing dropped.
        from compile_rules import RAMP_HINTS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_SAFARI_ZONE_NORTH"
        layout = next(row for row in layouts if row["id"] == lid)
        width, height = layout["width"], layout["height"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        ramps = {y * width + x: d for (x, y), d in RAMP_HINTS[lid].items()}
        contradictions = []
        solved = solve_layout(cells, width, height, contradictions=contradictions, ramps=ramps)
        self.assertEqual(contradictions, [])
        slope = solved[23 * width + 22]
        self.assertEqual((slope["profile"], slope["groundQ16"]), ("transition-horizontal", -32))
        self.assertEqual(solved[22 * width + 22]["groundQ16"], -16)     # the sand it climbs to
        self.assertEqual(solved[24 * width + 22]["groundQ16"], -32)     # the grass it climbs from

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
        # Stairs three rows long rise the one level over their rows; a level a row with `per_row` (the game's
        # heights: the owner's decision of 2026-10-29 for Route 112's plateau and Route 111's).
        rows = ["RRRRR", "RTTTR", "RRSRR", "RRSRR", "RRSRR", "GGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 5, 6, {})
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 17, 12)], [(0, 5), (5, 11), (11, 16)])
        raised = mountain_ledges(cells, 5, 6, {}, per_row=True)
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 17, 12)], [(0, 16), (16, 32), (32, 48)])
        self.assertEqual({raised[offset]["groundQ16"] for offset in (6, 7, 8)}, {3 * MOUNTAIN_LEDGE_Q16})
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

    def test_route112_plateau_stands_three_levels_over_the_ground(self):
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
                                 solve_layout(route, width, layout["height"]), per_row=True)
        # the plateau round the cable car station, the Fiery Path's mouth on it: three levels up, a level
        # a row of its stairs (the owner's decision of 2026-10-29: "Raise to the GBA heights")
        for x, y in ((24, 33), (12, 38), (11, 36)):
            self.assertEqual(raised[y * width + x]["groundQ16"], 3 * MOUNTAIN_LEDGE_Q16)
        # the stairs up to it (20-21, 41-43), a level each of their three rows; the ground below stays
        self.assertEqual([raised[y * width + 20]["topQ16"] for y in (43, 42, 41)], [16, 32, 48])
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
        # slopes (MB_MUDDY_SLOPE) are its ways up. Its walkable top stands a level up for each row
        # of the slope, the slope rises over its rows, the ground below keeps its level.
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
        # a level a row with `per_row` (Route 111's, the game's heights)
        raised = mountain_ledges(cells, 5, 6, {}, per_row=True)
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"])
                          for offset in (22, 17)], [(0, 16), (16, 32)])
        self.assertEqual(raised[6]["groundQ16"], 2 * MOUNTAIN_LEDGE_Q16)
        # A pocket of the top no one walks into (blocked all round) lies on the raised level too; without a raised top
        # in the layout it keeps its level.
        rows = ["RRRRRRR", "RTTTRTR", "RTTTRRR", "RRMRRRR", "RRMRRRR", "GGGGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        raised = mountain_ledges(cells, 7, 6, {})
        self.assertEqual(raised[12]["groundQ16"], MOUNTAIN_LEDGE_Q16)
        self.assertEqual(mountain_ledges(cells, 7, 6, {}, per_row=True)[12]["groundQ16"], 2 * MOUNTAIN_LEDGE_Q16)
        rows = ["RRRRRRR", "RTTTRTR", "RTTTRRR", "RRRRRRR", "GGGGGGG"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        self.assertEqual(mountain_ledges(cells, 7, 5, {}), {})

    def test_stairs_between_ordinary_ground_level_the_terraces_they_join(self):
        # climb_levels (compile_rules CLIMB_LEVELS; KNOWN_ISSUES MS-247): terraces of ordinary ground (no
        # MB_MOUNTAIN_TOP, no terrace course) whose stairs are the GBA's only sign of their levels - Mt. Pyre's
        # exterior. mountain_ledges leaves them to the terrace solver (nothing); climb_levels raises the ground at a
        # climb's top a level over the ground at its foot, the stairs rising between.
        def rock():
            return dict(cell(1), collision=1)

        def grass():
            return dict(cell(1), collision=0, elevation=3)

        def stairs():
            return dict(cell(175), collision=0, elevation=3)

        make = {"R": rock, "G": grass, "S": stairs}

        def layout(rows):
            return tuple(make[ch]() for row in rows for ch in row), len(rows[0]), len(rows)

        cells, width, height = layout(["GGGGG", "RRSRR", "GGGGG"])
        self.assertEqual(mountain_ledges(cells, width, height, {}), {})
        raised = climb_levels(cells, width, height, {})
        self.assertEqual({offset for offset, entry in raised.items() if entry["profile"] == "none"}, set(range(5)))
        self.assertEqual({raised[offset]["groundQ16"] for offset in range(5)}, {MOUNTAIN_LEDGE_Q16})
        self.assertEqual((raised[7]["groundQ16"], raised[7]["topQ16"], raised[7]["profile"]),
                         (0, MOUNTAIN_LEDGE_Q16, "transition-horizontal"))
        self.assertFalse(set(range(10, 15)) & set(raised))           # the foot keeps its level
        # A climb from a climb's top: a level a climb; a climb of two rows rises its one level over them (with
        # `per_row`, a level a row: the GBA's heights).
        cells, width, height = layout(["GGGGG", "RRSRR", "GGGGG", "RRSRR", "RRSRR", "GGGGG"])
        raised = climb_levels(cells, width, height, {})
        self.assertEqual((raised[0]["groundQ16"], raised[10]["groundQ16"]),
                         (2 * MOUNTAIN_LEDGE_Q16, MOUNTAIN_LEDGE_Q16))
        self.assertEqual([(raised[offset]["groundQ16"], raised[offset]["topQ16"]) for offset in (22, 17)],
                         [(0, 8), (8, 16)])
        raised = climb_levels(cells, width, height, {}, per_row=True)
        self.assertEqual((raised[0]["groundQ16"], raised[10]["groundQ16"]),
                         (3 * MOUNTAIN_LEDGE_Q16, 2 * MOUNTAIN_LEDGE_Q16))
        # Climbs that give one terrace two levels (from two feet on the ground, one climb and two) raise nothing,
        # as no climb at all.
        cells, width, height = layout(["GGGGGGG", "RSRRRSR", "GGGRGGG", "RRRRRSR", "RRRRGGG"])
        self.assertEqual(climb_levels(cells, width, height, {}), {})
        cells, width, height = layout(["GGGGG", "RRRRR", "GGGGG"])
        self.assertEqual(climb_levels(cells, width, height, {}), {})
        # A terrace whose south edge meets lower walkable ground (another elevation: no face drawn there) raises
        # nothing.
        cells, width, height = layout(["GGGGG", "GRSRR", "GGGGG"])
        cells = tuple(dict(entry, elevation=4) if offset == 10 else entry for offset, entry in enumerate(cells))
        self.assertEqual(climb_levels(cells, width, height, {}), {})

    def test_mt_pyre_exterior_climbs_four_terraces_to_its_summit(self):
        # The owner's picture of Route 119 (2026-11-13, "the third floor doesn't look 3d"); Mt. Pyre's exterior lay
        # on one level the same way, its five staircases flat. Its terraces now climb a level a staircase: the two
        # grounds at the foot of its first stairs on level 0, the summit's graves four levels up.
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        rows = [row for row in layouts if row["id"] == "LAYOUT_MT_PYRE_EXTERIOR"]
        cells, _, _ = _load_cells(ROOT, rows, tilesets, behavior_names)
        width, height = rows[0]["width"], rows[0]["height"]
        pyre = cells["LAYOUT_MT_PYRE_EXTERIOR"]
        solved = solve_layout(pyre, width, height)
        self.assertEqual(solved, {})                                   # neither solver gave it levels
        self.assertEqual(mountain_ledges(pyre, width, height, solved), {})
        raised = climb_levels(pyre, width, height, solved)

        def at(x, y):
            return raised.get(y * width + x)

        for (x, y), level in (((20, 12), 4), ((27, 15), 4), ((12, 18), 3), ((20, 19), 3), ((15, 27), 2),
                              ((25, 24), 2), ((15, 36), 1), ((28, 37), 1)):
            self.assertEqual((at(x, y)["groundQ16"], at(x, y)["profile"]), (MOUNTAIN_LEDGE_Q16 * level, "none"),
                             (x, y))
        for x, y in ((20, 44), (12, 39), (18, 40)):                    # the two grounds at the foot
            self.assertIsNone(at(x, y), (x, y))
        # the summit's stairs rise from the third level to the fourth over their two rows; the long west stairs
        # from the first to the second over their five
        self.assertEqual([(at(20, y)["groundQ16"], at(20, y)["topQ16"]) for y in (16, 15)], [(48, 56), (56, 64)])
        self.assertEqual((at(9, 33)["groundQ16"], at(9, 29)["topQ16"]), (16, 32))

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

    def test_a_caves_stairs_climb_from_one_floor_to_the_other(self):
        # Victory Road: stairs (elevation 0, the game's any level) through the face of a raised
        # floor (4), rock either side, the lower floor (3) at their foot. cave_levels left them on
        # the lowest floor, a step a tile high at their top; they now climb, a row its share, from
        # the lower floor's level to the higher's - north, or south where the higher floor is the
        # south one. A gap with an open side, both ends on one level, an end at no floor or at a
        # cell of no level, or not a rectangle, keeps its level.
        def rock():
            return {"tileset": "gTileset_Cave", "metatile": 1, "behavior": "MB_NORMAL",
                    "collision": 1, "elevation": 0}

        def floor(elevation):
            return {"tileset": "gTileset_Cave", "metatile": 2, "behavior": "MB_NORMAL",
                    "collision": 0, "elevation": elevation}

        make = {"#": rock, "4": lambda: floor(4), "3": lambda: floor(3), "0": lambda: floor(0),
                "B": lambda: floor(15)}

        def stairs(rows):
            cells = tuple(make[ch]() for row in rows for ch in row)
            width, height = len(rows[0]), len(rows)
            return cave_stairs(cells, width, height, cave_levels(cells, width, height))

        climb = stairs(["44444", "##0##", "##0##", "33333", "33333"])
        self.assertEqual(set(climb), {7, 12})
        self.assertEqual([(climb[offset]["groundQ16"], climb[offset]["topQ16"], climb[offset]["rise"])
                          for offset in (12, 7)], [(0, 8, 8), (8, 16, 8)])
        self.assertEqual({climb[offset]["profile"] for offset in climb}, {"transition-horizontal"})
        # two wide, one row: each cell the whole climb
        climb = stairs(["44444", "#00##", "33333"])
        self.assertEqual({(climb[offset]["groundQ16"], climb[offset]["topQ16"]) for offset in (6, 7)},
                         {(0, CAVE_RISE_Q16)})
        # the higher floor south: climbing south, the north row first
        climb = stairs(["33333", "##0##", "##0##", "44444"])
        self.assertEqual([(climb[offset]["groundQ16"], climb[offset]["topQ16"]) for offset in (7, 12)],
                         [(0, 8), (8, 16)])
        self.assertEqual({climb[offset]["profile"] for offset in climb}, {"transition-south"})
        # stairs down into water a player surfs: from its rim (the lowest floor's level); into rock (a
        # dead end): as they were
        make["~"] = lambda: dict(floor(3), behavior="MB_DEEP_WATER")
        climb = stairs(["44444", "##0##", "~~~~~", "33333"])
        self.assertEqual((climb[7]["groundQ16"], climb[7]["topQ16"]), (0, CAVE_RISE_Q16))
        self.assertEqual(stairs(["44444", "##0##", "#####", "33333"]), {})
        # an open side; both ends one level; a bridge (15) at an end; not a rectangle: as they were
        self.assertEqual(stairs(["44444", "#30##", "##0##", "33333"]), {})
        self.assertEqual(stairs(["44444", "##0##", "44444", "33333"]), {})
        self.assertEqual(stairs(["44B44", "##0##", "33333", "44444"]), {})
        self.assertEqual(stairs(["44444", "#00##", "##0##", "33333"]), {})
        # the repository's: Victory Road 1F's stairs (13, 27-28) climb north from its lower floor to
        # its raised one
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        layout = next(row for row in layouts if row["id"] == "LAYOUT_VICTORY_ROAD_1F")
        cave = _load_cells(ROOT, [layout], tilesets, names)[0]["LAYOUT_VICTORY_ROAD_1F"]
        width, height = layout["width"], layout["height"]
        levels = cave_levels(cave, width, height)
        self.assertEqual(levels[26 * width + 13]["groundQ16"], CAVE_RISE_Q16)
        climb = cave_stairs(cave, width, height, levels)
        self.assertEqual([(climb[y * width + 13]["groundQ16"], climb[y * width + 13]["topQ16"])
                          for y in (28, 27)], [(0, 8), (8, 16)])
        self.assertNotIn(29 * width + 13, climb)

    def test_a_cave_room_laid_out_by_hand(self):
        # compile_rules.CAVE_HAND_LEVELS (the owner's decision #52): a floor's region (the walkable cells of its
        # seed's elevation joined to it, not another of that elevation apart from it), a bridge's deck and single
        # rocks on levels given by hand, and the stairs between such floors climbing from one to the other
        def rock():
            return {"tileset": "gTileset_Cave", "metatile": 1, "behavior": "MB_NORMAL",
                    "collision": 1, "elevation": 0}

        def floor(elevation):
            return {"tileset": "gTileset_Cave", "metatile": 2, "behavior": "MB_NORMAL",
                    "collision": 0, "elevation": elevation}

        make = {"#": rock, "5": lambda: floor(5), "4": lambda: floor(4), "3": lambda: floor(3),
                "0": lambda: floor(0), "B": lambda: floor(15)}
        rows = ["4BB4#55", "5555#55", "##0####", "33333##"]
        cells = tuple(make[ch]() for row in rows for ch in row)
        width, height = 7, 4
        hand = cave_hand_levels(cells, width, height, {"floors": (((0, 1), -8), ((0, 3), -16)),
                                                       "decks": (((1, 0), 8),), "rocks": (((4, 1), -24),)})
        # the wet sand west of the rock on its level; the 5s east of it, apart from it, left alone
        self.assertEqual({hand[offset]["groundQ16"] for offset in range(7, 11)}, {-8})
        self.assertTrue(all(offset not in hand for offset in (5, 6, 12, 13)))
        self.assertEqual({hand[offset]["groundQ16"] for offset in range(21, 26)}, {-16})
        self.assertEqual([(hand[offset]["groundQ16"], hand[offset].get("deck")) for offset in (1, 2)],
                         [(8, True), (8, True)])
        self.assertEqual((hand[11]["groundQ16"], hand[11]["topQ16"], hand[11]["lift"]), (-24, -24, True))
        self.assertTrue(all(not entry["solid"] and entry["profile"] == "none" for entry in hand.values()))
        # the stairs between the two hand floors climb from the lower to the higher
        climb = cave_stairs(cells, width, height, hand)
        self.assertEqual((climb[16]["groundQ16"], climb[16]["topQ16"], climb[16]["profile"]),
                         (-16, -8, "transition-horizontal"))
        # a seed that is no floor, a deck's that is no bridge, a rock that is no rock: refused
        for spec in ({"floors": (((4, 0), 0),)}, {"decks": (((0, 0), 8),)}, {"rocks": (((0, 1), 0),)}):
            with self.assertRaises(ValueError):
                cave_hand_levels(cells, width, height, spec)
        # the repository's: Shoal Cave's low-tide inner room - both decks on the walkways' level, a tile or more
        # over the ground under them (the wet sand under the high one, the channel under the low one); the
        # stairs from the channel up to the wet sand; the rock east of the cave mouth on the mouth's floor
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        name = "LAYOUT_SHOAL_CAVE_LOW_TIDE_INNER_ROOM"
        layout = next(row for row in layouts if row["id"] == name)
        cave = _load_cells(ROOT, [layout], tilesets, names)[0][name]
        width, height = layout["width"], layout["height"]
        levels = cave_levels(cave, width, height)
        levels = {**levels, **cave_hand_levels(cave, width, height, CAVE_HAND_LEVELS[name])}
        at = lambda x, y: levels[y * width + x]["groundQ16"]
        walkway = at(5, 5)
        self.assertEqual((at(9, 5), at(10, 5), at(33, 6), at(34, 6)), (walkway, walkway, walkway, walkway))
        self.assertEqual((at(8, 21), at(9, 21), at(16, 22), at(17, 22)), (walkway, walkway, walkway, walkway))
        self.assertGreaterEqual(walkway - at(20, 7), CAVE_RISE_Q16)        # the wet sand under the high deck
        self.assertGreaterEqual(walkway - at(12, 20), CAVE_RISE_Q16)       # the channel under the low deck
        self.assertGreaterEqual(walkway - at(12, 23), CAVE_RISE_Q16)
        self.assertGreater(at(11, 17), at(11, 19))                         # the wet sand over the channel
        stairs = cave_stairs(cave, width, height, levels)[18 * width + 11]
        self.assertEqual((stairs["groundQ16"], stairs["topQ16"]), (at(11, 19), at(11, 17)))
        self.assertEqual(at(16, 18), at(15, 19))                           # the rock beside the cave mouth
        self.assertEqual(at(15, 19), at(15, 20))

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
        # A row of rock between ground asking a level (west) and a path three levels up (east): no
        # cell two levels under the rock beside it (Route 112's mountain, its middle a wall where
        # its rock nearest its plateau met its rock nearest the grass north of it).
        self.assertEqual(rock_tops(frozenset(range(1, 6)), {0: 16, 6: 48}, 7, 1, 16),
                         {1: 16, 2: 16, 3: 32, 4: 48, 5: 48})

    def test_route111_plateau_stands_two_levels_over_the_desert(self):
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        behavior_ids = parse_behaviors(ROOT / "include/constants/metatile_behaviors.h")
        behavior_names = {value: name for name, value in behavior_ids.items()}
        rows = [row for row in layouts if row["id"] in ("LAYOUT_ROUTE111", "LAYOUT_ROUTE111_NO_MIRAGE_TOWER")]
        cells, _, _ = _load_cells(ROOT, rows, tilesets, behavior_names)
        for layout in rows:
            width, route = layout["width"], cells[layout["id"]]
            raised = mountain_ledges(route, width, layout["height"],
                                     solve_layout(route, width, layout["height"]), per_row=True)
            # the plateau and its arm north to the second slope, two levels up (its slopes' two rows)
            for x, y in ((16, 33), (24, 30), (34, 15), (30, 4)):
                self.assertEqual(raised[y * width + x]["groundQ16"], 2 * MOUNTAIN_LEDGE_Q16, (layout["id"], x, y))
            # its two muddy slopes, a level each of their two rows; the desert and the grass below stay
            for x, south, north in ((17, 37, 36), (29, 7, 6)):
                self.assertEqual((raised[south * width + x]["topQ16"], raised[north * width + x]["topQ16"]),
                                 (16, 32), (layout["id"], x))
            for x, y in ((17, 38), (29, 8), (24, 40)):
                self.assertNotIn(y * width + x, raised)
            # the pockets of the top no one walks into: south of the plateau on its level, and in the
            # east cliff on the level of the rock round it (a level over the desert)
            for x, y, level in ((20, 34, 32), (20, 36, 32), (38, 83, 16)):
                self.assertEqual(raised[y * width + x]["groundQ16"], level, (layout["id"], x, y))

    def test_stairs_cut_through_rock_are_a_step_where_they_let_a_terrace_solve(self):
        # A terrace's course (General 121) parts the upper ground H from the lower L; stairs (General 175) cut
        # through rock beside no course join them. As ground of their region the stairs made H and L one region,
        # the course contradicted and the component was dropped (its course a block on level ground); counted as a
        # step up to the north they let it solve, H a level over L.
        def rock():
            return dict(cell(1), collision=1)

        rows = ["#########",
                "#.......#",
                "#TTTT#S##",
                "#.......#",
                "#########"]
        make = {"#": rock, ".": ground, "T": lambda: dict(cell(121), collision=1),
                "S": lambda: dict(cell(175), elevation=3)}
        cells = tuple(make[ch]() for row in rows for ch in row)
        contradictions = []
        solve_layout(cells, 9, 5, contradictions=contradictions)
        self.assertTrue(contradictions)
        ramps = contradiction_ramps(cells, 9, 5)
        self.assertEqual(ramps, {2 * 9 + 6: "n"})
        contradictions = []
        solved = solve_layout(cells, 9, 5, contradictions=contradictions, ramps=ramps)
        self.assertEqual(contradictions, [])
        self.assertEqual({solved[9 + x]["groundQ16"] for x in range(1, 8)}, {0})
        self.assertEqual({solved[27 + x]["groundQ16"] for x in range(1, 8)}, {-16})
        self.assertEqual(solved[2 * 9 + 6]["profile"], "transition-horizontal")
        # nothing to solve, nothing laid
        rows[2] = "#TTTT####"
        cells = tuple(make[ch]() for row in rows for ch in row)
        self.assertEqual(contradiction_ramps(cells, 9, 5), {})

    def test_the_ground_beside_a_courses_open_end_meets_its_foot(self):
        # A terrace course (General 121) with rock behind it (its top no one walks on) whose run ends in open
        # grass at its east (Safari Zone South's wall at (18-25, 2)): the grass beside its end met both its halves,
        # a level apart, and the component was dropped (its course a block). With open_ends it meets the course's
        # foot, the ground before it: one level for the ground, the course standing a level over it.
        def rock():
            return dict(cell(1), collision=1)

        rows = ["#######",
                "#######",
                "#TTT..#",
                "#.....#",
                "#######"]
        make = {"#": rock, ".": ground, "T": lambda: dict(cell(121), collision=1)}
        cells = tuple(make[ch]() for row in rows for ch in row)
        contradictions = []
        solve_layout(cells, 7, 5, contradictions=contradictions)
        self.assertTrue(contradictions)
        contradictions = []
        solved = solve_layout(cells, 7, 5, contradictions=contradictions, open_ends=True)
        self.assertEqual(contradictions, [])
        grass = (18, 19, 22, 23, 24, 25, 26)
        self.assertEqual(len({solved[o]["groundQ16"] for o in grass}), 1)
        self.assertEqual(solved[15]["groundQ16"], solved[22]["groundQ16"])
        self.assertEqual(solved[15]["topQ16"] - solved[15]["groundQ16"], 16)
        # a run whose grass goes round its end, from its top to its foot, still contradicts: that is a ramp's
        rows[1] = "#.....#"
        cells = tuple(make[ch]() for row in rows for ch in row)
        contradictions = []
        solve_layout(cells, 7, 5, contradictions=contradictions, open_ends=True)
        self.assertTrue(contradictions)

    def test_a_course_the_solver_cannot_place_is_a_face_on_the_ground_before_it(self):
        # course_ridges: a dropped component's courses (or scenery's) stand on the lowest ground round them, a level
        # up to their top edge in their own profile, not as blocks with no entry
        rows = ["#####", "#...#", "#TTT#", "#...#", "#####"]
        make = {"#": lambda: dict(cell(1), collision=1), ".": ground, "T": lambda: dict(cell(121), collision=1)}
        cells = tuple(make[ch]() for row in rows for ch in row)
        solved = {o: {"groundQ16": -16, "topQ16": -16, "profile": "none", "solid": False} for o in (16, 17, 18)}
        out = course_ridges(cells, 5, 5, [(11, 12, 13)], solved)
        self.assertEqual({o: (e["groundQ16"], e["topQ16"], e["profile"]) for o, e in out.items()},
                         dict.fromkeys((11, 12, 13), (-16, 0, "horizontal")))

    def test_the_safari_zones_entrance_stands_its_enclosure_a_level_up(self):
        # The owner's report of 2026-11-01 ("some hills are blocks, some hills are 2d"): with compile_rules'
        # hints for the Safari Zone's entrance (its two walls scenery, the ground before its stairs where the next
        # maps meet it) the enclosure stands a level over the ground before its stairs, nothing dropped. Its white
        # rails from its opening (17, 7) to the east plateau, of its elevation, stand on its level, and the plateau
        # with them, over the valley the rails cross (KNOWN_ISSUES MS-222, the owner's report of 2026-11-09: a ramp
        # laid at the opening stood white stairs there and put the rails and the plateau on the valley).
        from compile_rules import RAMP_HINTS, SCENERY_COURSES
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_SAFARI_ZONE_SOUTH"
        layout = next(row for row in layouts if row["id"] == lid)
        width, height = layout["width"], layout["height"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        scenery = {y * width + x for x, y in SCENERY_COURSES[lid]}
        cells = tuple(dict(c, metatile=0, localMetatile=0) if o in scenery else c for o, c in enumerate(cells))
        self.assertNotIn(lid, RAMP_HINTS)
        contradictions = []
        solved = solve_layout(cells, width, height, contradictions=contradictions, open_ends=True)
        self.assertEqual(contradictions, [])
        enclosure, valley = solved[7 * width + 13]["groundQ16"], solved[10 * width + 13]["groundQ16"]
        self.assertEqual(enclosure - valley, 16)
        self.assertEqual(solved[9 * width + 13]["profile"], "transition-horizontal")   # its stairs climb
        for x, y in ((17, 7), (20, 6), (22, 4), (25, 3), (26, 4), (30, 3), (35, 4), (26, 7)):   # rails, plateau
            self.assertEqual((solved[y * width + x]["groundQ16"], solved[y * width + x]["profile"]),
                             (enclosure, "none"), (x, y))
        for x, y in ((21, 7), (23, 5), (24, 8)):                                          # the valley by them
            self.assertEqual(solved[y * width + x]["groundQ16"], valley, (x, y))

    def test_the_safari_zones_north_east_steps_down_to_ramps_at_its_south_edge(self):
        # The owner's choice of 2026-11-01 ("Real terraces + slope at edge"): with compile_rules' hints for the
        # Safari Zone's north-east (the ground beside a course's end meets its foot, its stairs' halves climb with
        # them, three rows of ramp at its south edge) nothing is dropped, its terraces step down three levels to the
        # south, and the ramps climb those three levels back to the top, where the south-east meets it.
        from compile_rules import OPEN_END_LAYOUTS, RAMP_HINTS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_SAFARI_ZONE_NORTHEAST"
        self.assertIn(lid, OPEN_END_LAYOUTS)
        layout = next(row for row in layouts if row["id"] == lid)
        width, height = layout["width"], layout["height"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        ramps = {y * width + x: d for (x, y), d in RAMP_HINTS[lid].items()}
        contradictions = []
        solved = solve_layout(cells, width, height, contradictions=contradictions, ramps=ramps, open_ends=True)
        self.assertEqual(contradictions, [])
        self.assertEqual(solved[35 * width + 7]["groundQ16"], -48)      # the lowest terrace, three levels down
        for x in (7, 30, 34):
            for row, ground in ((37, -48), (38, -32), (39, -16)):
                entry = solved[row * width + x]
                self.assertEqual((entry["profile"], entry["groundQ16"]), ("transition-south", ground))
        self.assertEqual(solved[22 * width + 21]["profile"], "transition-horizontal")  # its stairs climb north

    def test_a_passs_paths_stand_a_level_a_ledge_apart(self):
        # A pass whose paths are all at one elevation (Jagged Pass): the top path H jumps south into an island I,
        # which jumps east into a way W that runs down from H beside it, and both jump south into the bottom L.
        # H > I > W asks a ramp in the way (its narrow row (5,2)); the pocket P behind the rock lies with the rock.
        def rock():
            return dict(cell(1), collision=1, elevation=0)

        def path():
            return dict(cell(1), collision=0, elevation=3)

        def ledge(behavior):
            return dict(cell(1), behavior=behavior, collision=0, elevation=3)

        rows = ["#########",
                "#HHHHH###",
                "#vvv#W###",
                "#III>W#P#",
                "#vvv#v###",
                "#LLLLL###",
                "#########"]
        make = {"#": rock, "H": path, "I": path, "W": path, "L": path, "P": path,
                "v": lambda: ledge("MB_JUMP_SOUTH"), ">": lambda: ledge("MB_JUMP_EAST")}
        cells = tuple(make[ch]() for row in rows for ch in row)
        out = pass_levels(cells, 9, 7)

        def at(x, y):
            return out.get(y * 9 + x, {"groundQ16": 0, "topQ16": 0, "profile": "none"})

        self.assertEqual({at(x, 1)["groundQ16"] for x in range(1, 6)}, {48})       # H
        self.assertEqual({at(x, 3)["groundQ16"] for x in range(1, 4)}, {32})       # I, a ledge under H
        self.assertEqual(at(5, 3)["groundQ16"], 16)                                # W's foot, a ledge under I
        self.assertEqual({at(x, 5)["groundQ16"] for x in range(1, 6)}, {0})        # L
        self.assertEqual((at(5, 2)["profile"], at(5, 2)["groundQ16"], at(5, 2)["topQ16"]),
                         ("transition-horizontal", 16, 48))                       # the ramp from W up to H
        self.assertEqual(at(7, 3)["groundQ16"], 32)                                # the pocket with the rock round it
        # every straight ledge drops from where it is jumped to where it lands
        for offset, ch in enumerate("".join(rows)):
            if ch in "v>":
                step = 9 if ch == "v" else 1
                self.assertGreater(at((offset - step) % 9, (offset - step) // 9)["groundQ16"],
                                   at((offset + step) % 9, (offset + step) // 9)["groundQ16"])
        # the same pass upside down (jumped north): the way would rise to the south, which no ramp does, so the
        # island's asks still contradict and nothing changes
        make["^"] = lambda: ledge("MB_JUMP_NORTH")
        flipped = [row.replace("v", "^") for row in reversed(rows)]
        cells = tuple(make[ch]() for row in flipped for ch in row)
        self.assertEqual(pass_levels(cells, 9, 7), {})
        # the repository's Jagged Pass: nine levels from the Mt. Chimney end down, every ledge a drop
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        layout = next(row for row in layouts if row["id"] == "LAYOUT_JAGGED_PASS")
        jagged = _load_cells(ROOT, [layout], tilesets, names)[0]["LAYOUT_JAGGED_PASS"]
        width = layout["width"]
        out = pass_levels(jagged, width, layout["height"], solve_layout(jagged, width, layout["height"]))

        def level(x, y):
            return out.get(y * width + x, {"groundQ16": 0})["groundQ16"]

        self.assertEqual((level(14, 7), level(15, 22), level(14, 24), level(20, 24), level(14, 39)),
                         (144, 80, 64, 48, 0))
        self.assertEqual({out[y * width + 21]["profile"] for y in (21, 22, 23)}, {"transition-horizontal"})
        for offset, entry in enumerate(jagged):
            step = {"MB_JUMP_SOUTH": width, "MB_JUMP_EAST": 1, "MB_JUMP_WEST": -1}.get(entry.get("behavior"))
            takeoff, landing = (offset - step, offset + step) if step else (None, None)
            if step and jagged[takeoff].get("collision", 0) == 0 and jagged[landing].get("collision", 0) == 0:
                self.assertGreater(level(takeoff % width, takeoff // width), level(landing % width, landing // width))

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

    def test_ever_grandes_halls_climb_to_their_landings(self):
        # KNOWN_ISSUES MS-164: the halls between the Elite Four's rooms lay flat, their staircases
        # pictures of steps. A short hall's eight rows of steps rise two levels to its landing, Hall 4's
        # 29 seven; the landing, the door and the wall behind them on that top level; the railings on
        # the steps' ground row by row; the dark floor either side where it was.
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        wanted = ("LAYOUT_EVER_GRANDE_CITY_SHORT_HALL", "LAYOUT_EVER_GRANDE_CITY_HALL4",
                  "LAYOUT_EVER_GRANDE_CITY_CHAMPIONS_ROOM")
        rows = {row["id"]: row for row in layouts if row["id"] in wanted}
        loaded = _load_cells(ROOT, list(rows.values()), tilesets, names)[0]
        for layout_id, foot, rise in (("LAYOUT_EVER_GRANDE_CITY_SHORT_HALL", 12, 32),
                                      ("LAYOUT_EVER_GRANDE_CITY_HALL4", 33, 112)):
            layout, cells = rows[layout_id], loaded[layout_id]
            width = layout["width"]
            raised = hall_stairs(cells, width, layout["height"])
            steps = [raised[y * width + 5] for y in range(foot, 4, -1)]
            self.assertEqual((steps[0]["groundQ16"], steps[-1]["topQ16"]), (0, rise))
            self.assertTrue(all(entry["profile"] == "transition-horizontal"
                                and entry["topQ16"] == entry["groundQ16"] + entry["rise"] for entry in steps))
            self.assertTrue(all(lower["topQ16"] == upper["groundQ16"] for lower, upper in zip(steps, steps[1:])))
            # the landing (4), the door (2) and the wall (0) on the top level, the blocked ones standing on it
            self.assertEqual({raised[y * width + 5]["groundQ16"] for y in (0, 2, 4)}, {rise})
            self.assertTrue(raised[0]["lift"] and "lift" not in raised[4 * width])
            # a railing on its row's ground; the railings at the foot and the dark floor stay
            self.assertEqual(raised[6 * width + 3]["groundQ16"], steps[foot - 6]["groundQ16"])
            self.assertTrue(raised[6 * width + 3]["lift"])
            self.assertNotIn(foot * width + 3, raised)
            self.assertFalse(any(y * width + x in raised for y in range(6, foot + 1) for x in (0, 1, 2, 8, 9, 10)))
        # the Champion's room's two rows of steps up to its door: no landing, nothing raised
        champion = rows["LAYOUT_EVER_GRANDE_CITY_CHAMPIONS_ROOM"]
        self.assertEqual(hall_stairs(loaded["LAYOUT_EVER_GRANDE_CITY_CHAMPIONS_ROOM"], champion["width"],
                                     champion["height"]), {})

    def test_a_rooms_raised_floor_given_by_hand(self):
        # KNOWN_ISSUES MS-164: a lobby's upper floor the GBA draws at the floor's elevation (the Battle
        # Pyramid's): a raised rectangle on the given level, a blocked cell on it standing its own height,
        # steps climbing north to it a share a row, a rail beside them on their ground row by row.
        floor = lambda: dict(cell(1), collision=0, elevation=4)
        wall = lambda: dict(cell(1), collision=1, elevation=4)
        rows = ["####", "....", "#..#", "#..#", "...."]
        make = {".": floor, "#": wall}
        cells = tuple(make[ch]() for row in rows for ch in row)
        spec = {"rise": 32, "raised": ((0, 0, 3, 1),), "ramps": ((1, 2, 1, 3),), "rails": ((0, 2, 0, 3),)}
        out = indoor_levels(cells, 4, 5, spec)
        self.assertEqual({out[offset]["groundQ16"] for offset in range(8)}, {32})
        self.assertTrue(out[0]["lift"] and "lift" not in out[4])
        self.assertEqual([(out[o]["groundQ16"], out[o]["topQ16"], out[o]["rise"]) for o in (13, 9)],
                         [(0, 16, 16), (16, 32, 16)])
        self.assertEqual(out[8]["groundQ16"], 16)        # the rail beside the steps' upper row
        self.assertNotIn(12, out)                        # the rail at the steps' foot stays on the floor
        self.assertFalse(any(o in out for o in (10, 11, 14, 15, 16, 17, 18, 19)))
        # a cell another solver placed keeps its entry
        self.assertNotIn(5, indoor_levels(cells, 4, 5, spec, {5: {"groundQ16": 0}}))

    def test_a_rooms_several_levels_given_by_hand(self):
        # The Battle Pyramid's top: rows on levels in order, the walkable cells of a slope's rows
        # climbing north from its low level to its high one, its blocked cells keeping their step.
        floor = lambda: dict(cell(1), collision=0, elevation=4)
        wall = lambda: dict(cell(1), collision=1, elevation=4)
        rows = ["....", "#.##", "....", "#.##", "...."]
        make = {".": floor, "#": wall}
        cells = tuple(make[ch]() for row in rows for ch in row)
        spec = {"steps": ((32, (0, 0, 3, 1)), (16, (0, 2, 3, 3))),
                "slopes": (((0, 1, 3, 1), 16, 32), ((0, 3, 3, 3), 0, 16))}
        out = indoor_levels(cells, 4, 5, spec)
        self.assertEqual({out[offset]["groundQ16"] for offset in range(4)}, {32})
        self.assertEqual((out[5]["groundQ16"], out[5]["topQ16"], out[5]["rise"]), (16, 32, 16))
        self.assertEqual((out[4]["groundQ16"], out[4]["lift"]), (32, True))
        self.assertEqual({out[offset]["groundQ16"] for offset in range(8, 12)}, {16})
        self.assertEqual((out[13]["groundQ16"], out[13]["topQ16"]), (0, 16))
        self.assertEqual((out[12]["groundQ16"], out[12]["lift"]), (16, True))
        self.assertFalse(any(offset in out for offset in range(16, 20)))

    def test_a_seam_ramp_given_by_hand(self):
        # A rectangle's rows climb from its low level at the edge it starts from to its high one, a share a row;
        # the Safari Zone's south-west's pocket of grass (only the north-west walks into it) climbs south from the
        # north-west's level at the seam to the south-west's, the ledges at its south side jumped into the
        # south-west (compile_rules SEAM_RAMPS).
        from compile_rules import SEAM_RAMPS
        out = hand_slopes(5, (((1, 0, 3, 1), "s", -32, 0), ((0, 2, 0, 3), "n", 0, 16)))
        self.assertEqual({(out[x]["groundQ16"], out[x]["topQ16"], out[x]["rise"], out[x]["profile"], out[x]["solid"])
                          for x in (1, 2, 3)}, {(-32, -16, 16, "transition-south", False)})
        self.assertEqual((out[5 + 2]["groundQ16"], out[5 + 2]["topQ16"]), (-16, 0))
        self.assertEqual((out[15]["groundQ16"], out[15]["topQ16"], out[15]["profile"]), (0, 8, "transition-horizontal"))
        self.assertEqual((out[10]["groundQ16"], out[10]["topQ16"]), (8, 16))
        self.assertEqual(sorted(out), [1, 2, 3, 6, 7, 8, 10, 15])

        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        self.assertEqual(set(SEAM_RAMPS), {"LAYOUT_SAFARI_ZONE_SOUTHWEST", "LAYOUT_SAFARI_ZONE_SOUTH",
                                           "LAYOUT_SAFARI_ZONE_NORTHWEST",
                                           "LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST", "LAYOUT_ROUTE111",
                                           "LAYOUT_ROUTE111_NO_MIRAGE_TOWER"})
        for lid, ramp in SEAM_RAMPS.items():
            if lid not in ("LAYOUT_SAFARI_ZONE_SOUTHWEST", "LAYOUT_SAFARI_ZONE_SOUTH"):
                continue                                  # (the north-west's plateau: the test below)
            layout = next(row for row in layouts if row["id"] == lid)
            width = layout["width"]
            cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
            # the seam's pocket: the ramp down from the map's north edge (the south-west's muddy slope and the
            # plateau's rows over it are the test below's)
            ((x0, sy0, x1, y1), direction, low, high), = [slope for slope in ramp["slopes"] if slope[1] == "s"]
            floors = tuple((rect, level) for rect, level in ramp.get("floors", ())
                           if rect[0] <= x1 and rect[2] >= x0)
            y0 = min([sy0] + [rect[1] for rect, _ in floors])
            # from the map's north edge to its own ground (the south's: to its east plateau, a level over its valley,
            # KNOWN_ISSUES MS-222)
            self.assertEqual((y0, direction, high), (0, "s", 16 if lid == "LAYOUT_SAFARI_ZONE_SOUTH" else 0))
            self.assertLess(low, high)
            # a floor before the slope lies on its low level, flat, its walkable cells only
            self.assertTrue(all(level == low and rect[3] < sy0 for rect, level in floors))
            flat = hand_floors(cells, width, floors)
            self.assertTrue(all(entry["profile"] == "none" and entry["groundQ16"] == low for entry in flat.values()))
            self.assertEqual(len(flat), sum((r[2] - r[0] + 1) * (r[3] - r[1] + 1) for r, _ in floors))
            for x in range(x0, x1 + 1):
                self.assertTrue(all(cells[y * width + x]["collision"] == 0 for y in range(y0, y1 + 1)))
            # the trees beside it: their two rows on one level, from the ramp's low end; the ramp's cells left alone
            # (the south's last row runs into its plateau, the trees beside it the plateau's)
            seam_banks = tuple((rect, lvl) for rect, lvl in ramp["banks"] if rect[0] <= x1 and rect[2] >= x0)
            ((bx0, by0, bx1, by1), level), = seam_banks
            self.assertEqual((by0, by1), (y0, y1 - 1 if lid == "LAYOUT_SAFARI_ZONE_SOUTH" else y1))
            self.assertTrue(low <= level < high)
            banks = hand_banks(cells, width, seam_banks)
            self.assertTrue(banks)
            self.assertEqual({(entry["groundQ16"], entry["lift"]) for entry in banks.values()}, {(level, True)})
            self.assertFalse(any(cells[offset]["collision"] == 0 for offset in banks))
            self.assertFalse(any(x0 <= offset % width <= x1 for offset in banks))
            if lid == "LAYOUT_SAFARI_ZONE_SOUTHWEST":
                # the pocket's south side: the ledges down into the south-west
                for x in range(x0, x1 + 1):
                    self.assertEqual(cells[(y1 + 1) * width + x]["behavior"], "MB_JUMP_SOUTH")
            else:
                # the south's path runs on south from the ramp
                self.assertTrue(all(cells[(y1 + 1) * width + x]["collision"] == 0 for x in range(x0, x1 + 1)))

    def test_courses_and_walkways_given_by_hand(self):
        # compile_rules HAND_LEVELS: a rectangle's terrace courses are faces from the level given a level up, its
        # other cells left alone (hand_ridges); a rectangle's walkable cells are decks on the level solved for them,
        # its blocked cells left alone, and a walkable cell with no flat level solved for it is an error (hand_decks).
        cells = (dict(cell(121), collision=1), ground(), dict(cell(1), collision=1),
                 ground(), ground(), dict(cell(1), collision=1))
        self.assertEqual(hand_ridges(cells, 3, (((0, 0, 2, 0), 0),)),
                         {0: {"groundQ16": 0, "topQ16": 16, "profile": TERRACE_ROLES[121].profile, "solid": True}})
        self.assertEqual(hand_ridges(cells, 3, (((0, 0, 2, 0), -16),))[0]["topQ16"], 0)
        solved = {3: {"groundQ16": 16, "topQ16": 16, "profile": "none", "solid": False},
                  4: {"groundQ16": 16, "topQ16": 16, "profile": "none", "solid": False}}
        decks = hand_decks(cells, 3, ((0, 1, 2, 1),), solved)
        self.assertEqual(decks, {3: {**solved[3], "deck": True}, 4: {**solved[4], "deck": True}})
        self.assertNotIn("deck", solved[3])                        # the solved entries themselves are left alone
        with self.assertRaises(ValueError):
            hand_decks(cells, 3, ((0, 0, 2, 0),), solved)          # (1, 0): walkable, no level solved for it
        with self.assertRaises(ValueError):
            hand_decks(cells, 3, ((0, 1, 0, 1),), {3: {"groundQ16": 0, "topQ16": 16, "solid": False,
                                                      "profile": "transition-south"}})  # a ramp is no deck

    def test_the_safari_zones_south_walkway_given_by_hand(self):
        # KNOWN_ISSUES MS-222: Safari Zone South's white rails on posts are decks over its valley - every rail cell
        # and nothing else; the valley's ground they close round (no one walks there, its elevation the valley's)
        # lies on the valley's level; the wall along (18-25, 2) behind them is a face from that level a level up.
        from compile_rules import HAND_LEVELS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_SAFARI_ZONE_SOUTH"
        self.assertEqual(set(HAND_LEVELS), {lid, "LAYOUT_FORTREE_CITY", "LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST"})
        layout = next(row for row in layouts if row["id"] == lid)
        width = layout["width"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        levels = HAND_LEVELS[lid]
        flat = {offset: {"groundQ16": 16, "topQ16": 16, "profile": "none", "solid": False}
                for offset, c in enumerate(cells) if c["collision"] == 0}
        decks = hand_decks(cells, width, levels["decks"], flat)
        rails = {offset for offset, c in enumerate(cells) if str(c["behavior"]).endswith("_RAIL")}
        self.assertEqual(set(decks), rails)
        self.assertEqual(len(rails), 13)
        self.assertEqual({cells[offset]["elevation"] for offset in rails}, {5})
        floors = hand_floors(cells, width, levels["floors"])
        self.assertEqual(len(floors), 12)
        self.assertEqual({entry["groundQ16"] for entry in floors.values()}, {0})
        self.assertEqual({cells[offset]["elevation"] for offset in floors}, {3})
        for offset in floors:                       # closed round by the rails, the enclosure's cliff and the wall
            for neighbor in (offset - width, offset + 1, offset + width, offset - 1):
                self.assertTrue(neighbor in floors or neighbor in rails or cells[neighbor]["collision"], neighbor)
        ridges = hand_ridges(cells, width, levels["ridges"])
        self.assertEqual(sorted(ridges), [2 * width + x for x in range(18, 26)])
        self.assertEqual({(entry["groundQ16"], entry["topQ16"]) for entry in ridges.values()}, {(0, 16)})

    def test_the_battle_frontiers_east_strip_given_by_hand(self):
        # The Battle Frontier's east half (KNOWN_ISSUES MS-187's last walk-steps): the strip (12-22, 34-38) is two
        # lanes of elevation 0 (12-14, 20-22) and the flower bed between them, between two plazas of elevation 3,
        # the north one walked into from the lanes and the south one through row 39; the 2D game draws no step
        # between them. It climbs south from a level down (the north plaza's) to the south plaza's level, a fifth of
        # a level a row: no step of a third of a level or more along it. The fences' columns beside it are left.
        from compile_rules import HAND_LEVELS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST"
        layout = next(row for row in layouts if row["id"] == lid)
        width = layout["width"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        levels = HAND_LEVELS[lid]
        self.assertEqual(set(levels), {"late_slopes"})       # laid after the blocked cells are lifted
        slopes = hand_slopes(width, levels["late_slopes"])
        self.assertEqual(sorted(slopes), [y * width + x for y in range(34, 39) for x in range(12, 23)])
        self.assertTrue(all(cells[y * width + x]["collision"] for y in range(34, 39) for x in (11, 23)))
        lanes = {offset for offset in slopes if cells[offset]["collision"] == 0}
        self.assertEqual(lanes, {y * width + x for y in range(34, 39) for x in (12, 13, 14, 20, 21, 22)})
        self.assertEqual({cells[offset]["elevation"] for offset in lanes}, {0})
        for x in (12, 13, 14, 20, 21, 22):          # the plaza north of each lane, the way south out of it
            self.assertEqual((cells[33 * width + x]["collision"], cells[33 * width + x]["elevation"]), (0, 3))
            self.assertEqual((cells[39 * width + x]["collision"], cells[39 * width + x]["elevation"]), (0, 0))
            self.assertEqual((cells[40 * width + x]["collision"], cells[40 * width + x]["elevation"]), (0, 3))
            column = [slopes[y * width + x] for y in range(34, 39)]
            self.assertEqual((column[0]["groundQ16"], column[-1]["topQ16"]), (-16, 0))
            self.assertTrue(all(entry["topQ16"] - entry["groundQ16"] <= 4 for entry in column))
            self.assertTrue(all(a["topQ16"] == b["groundQ16"] for a, b in zip(column, column[1:])))

    def test_fortree_citys_tree_houses_given_by_hand(self):
        # Fortree City's raised walkways (the 3D sweep of 2026-11-09): every walkable cell of the walkways'
        # elevation (4) - porches and bridges - and the bridges' crossings over the path (15) lie a level up, and
        # nothing else walkable but the houses' doors; the decks are its plank bridges (MB_FORTREE_BRIDGE), every one;
        # the slopes are its ladders, the walkable cells of elevation 0 that forbid running, every one, each climbing
        # north from the floor to its porch; the house over each door stands a level up; its forest is left where it
        # is (HAND_LEVELS_ONLY).
        from compile_rules import HAND_LEVELS, HAND_LEVELS_ONLY
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_FORTREE_CITY"
        self.assertEqual(HAND_LEVELS_ONLY, {lid})
        layout = next(row for row in layouts if row["id"] == lid)
        width = layout["width"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        levels = HAND_LEVELS[lid]
        walkable = {offset for offset, c in enumerate(cells) if c["collision"] == 0}
        doors = {offset for offset in walkable if str(cells[offset]["behavior"]).endswith("_DOOR")}
        floors = hand_floors(cells, width, levels["floors"])
        self.assertEqual({entry["groundQ16"] for entry in floors.values()}, {16})
        walkways = {offset for offset in walkable if cells[offset]["elevation"] == 4}
        bridges = {offset for offset in walkable if cells[offset]["behavior"] == "MB_FORTREE_BRIDGE"}
        self.assertEqual(len(walkways), 52)
        self.assertEqual(len(bridges), 27)
        self.assertEqual(len(doors), 6)
        self.assertEqual(set(floors), walkways | bridges | doors)
        self.assertEqual({cells[offset]["elevation"] for offset in bridges - walkways}, {15})
        decks = hand_decks(cells, width, levels["decks"], floors)
        self.assertEqual(set(decks), bridges)
        slopes = hand_slopes(width, levels["slopes"])
        ladders = {offset for offset in walkable
                   if cells[offset]["elevation"] == 0 and cells[offset]["behavior"] == "MB_NO_RUNNING"}
        self.assertEqual(len(ladders), 10)
        self.assertEqual(set(slopes), ladders)
        for offset in ladders:                       # the floor south of each ladder, its porch north of it
            if offset - width in ladders:
                self.assertIn(offset + width, walkable)
                self.assertEqual(cells[offset + width]["elevation"], 3)
                self.assertIn(offset - 2 * width, floors)
        banks = hand_banks(cells, width, levels["banks"])
        self.assertTrue(all(cells[offset]["collision"] for offset in banks))
        self.assertTrue(all(door - width in banks for door in doors))   # the house over each door

    def test_the_safari_zones_plateau_given_by_hand(self):
        # The south-west's muddy slope (8, 2-3) climbs through the rock of its face to the north-west's plateau (its
        # elevation 7), which lay on the slope's foot's level (KNOWN_ISSUES MS-153): the plateau, the slope's top
        # rows and the trees on them stand a level up, the slope climbs north over its two rows, the plateau's
        # stairs down into the north-west's lower ground climb the two levels over their row and the row over them,
        # and where the two maps meet (7-10) both lie on the plateau's level.
        from compile_rules import SEAM_RAMPS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        loaded = {}
        for lid in ("LAYOUT_SAFARI_ZONE_NORTHWEST", "LAYOUT_SAFARI_ZONE_SOUTHWEST"):
            layout = next(row for row in layouts if row["id"] == lid)
            loaded[lid] = (layout["width"], _load_cells(ROOT, [layout], tilesets, names)[0][lid])
        nw_width, nw = loaded["LAYOUT_SAFARI_ZONE_NORTHWEST"]
        sw_width, sw = loaded["LAYOUT_SAFARI_ZONE_SOUTHWEST"]
        north, south = SEAM_RAMPS["LAYOUT_SAFARI_ZONE_NORTHWEST"], SEAM_RAMPS["LAYOUT_SAFARI_ZONE_SOUTHWEST"]
        # the plateau's floors: its own walkable cells (elevation 7), every one of them, on one level
        plateau = hand_floors(nw, nw_width, north["floors"])
        self.assertEqual({entry["groundQ16"] for entry in plateau.values()}, {16})
        self.assertEqual({nw[offset]["elevation"] for offset in plateau}, {7})
        joined = {offset for offset in range(len(nw)) if nw[offset]["collision"] == 0 and nw[offset]["elevation"] == 7
                  and offset // nw_width >= 27}
        self.assertEqual(set(plateau) - {30 * nw_width + 13, 30 * nw_width + 14}, joined - {30 * nw_width + 13,
                                                                                          30 * nw_width + 14})
        # its stairs: elevation 0 at the plateau's edge (31), climbing north from the lower ground to the plateau
        stairs = hand_slopes(nw_width, north["slopes"])
        self.assertEqual({nw[31 * nw_width + x]["elevation"] for x in (13, 14)}, {0})
        self.assertEqual([(stairs[y * nw_width + 13]["groundQ16"], stairs[y * nw_width + 13]["topQ16"]) for y in (31, 30)],
                         [(-16, 0), (0, 16)])
        self.assertTrue(all(nw[32 * nw_width + x]["collision"] == 0 for x in (13, 14)))
        # the south-west: the slope climbs north from its foot's level to the plateau's over its two rows
        slope = hand_slopes(sw_width, [s for s in south["slopes"] if s[1] == "n"])
        self.assertEqual({sw[y * sw_width + 8]["behavior"] for y in (2, 3)}, {"MB_MUDDY_SLOPE"})
        self.assertEqual([(slope[y * sw_width + 8]["groundQ16"], slope[y * sw_width + 8]["topQ16"]) for y in (3, 2)],
                         [(0, 8), (8, 16)])
        top = hand_floors(sw, sw_width, south["floors"])
        self.assertTrue(all(top[x]["groundQ16"] == 16 for x in (7, 8, 9, 10)))
        # where the maps meet the plateau's way, both on its level (north-west row 39, south-west row 0)
        self.assertTrue(all(plateau[39 * nw_width + x]["groundQ16"] == top[x]["groundQ16"] == 16 for x in (7, 8, 9, 10)))
        # the trees on the plateau and the face's top row stand on it, blocked cells only
        banks = hand_banks(sw, sw_width, [(rect, level) for rect, level in south["banks"] if level == 16])
        self.assertTrue(banks and all(sw[offset]["collision"] != 0 for offset in banks))
        self.assertTrue(all(sw[2 * sw_width + x]["collision"] != 0 and 2 * sw_width + x in banks
                            for x in range(0, 13) if x != 8))

    def test_ramps_given_by_hand_climb_east_and_west(self):
        # An east ramp's columns climb from its west edge, a west one's from its east edge, a share a column; a
        # direction other than n, s, e or w is refused.
        out = hand_slopes(4, (((0, 0, 1, 0), "e", -16, 0), ((2, 1, 3, 1), "w", 0, 16)))
        self.assertEqual([(out[x]["groundQ16"], out[x]["topQ16"], out[x]["rise"], out[x]["profile"]) for x in (0, 1)],
                         [(-16, -8, 8, "transition-east"), (-8, 0, 8, "transition-east")])
        self.assertEqual([(out[4 + x]["groundQ16"], out[4 + x]["topQ16"], out[4 + x]["profile"]) for x in (3, 2)],
                         [(0, 8, "transition-west"), (8, 16, "transition-west")])
        self.assertEqual(sorted(out), [0, 1, 6, 7])
        with self.assertRaises(ValueError):
            hand_slopes(4, (((0, 0, 0, 0), "up", 0, 16),))

    def test_the_battle_frontiers_seam_given_by_hand(self):
        # The west half's south plaza lies a level under the east half's (MS-162): the west half's last column lies
        # flat on the east half's level, the column before it climbs east to it where it is walked, and the
        # building and the Mart beside it stand on the plaza's level, whole.
        from compile_rules import SEAM_RAMPS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST"
        ramp = SEAM_RAMPS[lid]
        layout = next(row for row in layouts if row["id"] == lid)
        width = layout["width"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        walk = lambda x, y: cells[y * width + x]["collision"] == 0
        ((fx0, fy0, fx1, fy1), floor_level), = ramp["floors"]
        self.assertEqual((fx0, fx1, floor_level), (width - 1, width - 1, 0))       # the seam column, the east level
        flat = hand_floors(cells, width, ramp["floors"])
        self.assertEqual(len(flat), fy1 - fy0 + 1)                                  # walked end to end
        slopes = hand_slopes(width, ramp["slopes"])
        rows = set()
        for (x0, y0, x1, y1), direction, low, high in ramp["slopes"]:
            self.assertEqual((x0, x1, direction, low, high), (width - 2, width - 2, "e", -16, 0))
            rows |= set(range(y0, y1 + 1))
        # every walkable cell of the column before the seam's, and only those, climbs
        self.assertEqual(rows, {y for y in range(fy0, fy1 + 1) if walk(width - 2, y)})
        self.assertTrue(all(entry["groundQ16"] == -16 and entry["topQ16"] == 0 for entry in slopes.values()))
        banks = hand_banks(cells, width, ramp["banks"])
        self.assertEqual({(entry["groundQ16"], entry["lift"]) for entry in banks.values()}, {(-16, True)})
        self.assertFalse(set(banks) & set(slopes))
        self.assertFalse(set(banks) & set(flat))
        # the column's blocked cells (the building's and the Mart's corners) are banked
        self.assertTrue(all((y * width + width - 2) in banks for y in range(fy0, fy1 + 1) if not walk(width - 2, y)))

    def test_mauvilles_garden_given_by_hand(self):
        # Route 111's part of Mauville City's garden (the rows at the seam, to its fence and rocks) lies on the town's
        # level, flat, its fence and rocks with it: a pocket walled off from the rest of Route 111, whose own ground
        # meets Mauville on that level at the road.
        from compile_rules import SEAM_RAMPS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_ROUTE111"
        ramp = SEAM_RAMPS[lid]
        self.assertNotIn("slopes", ramp)
        # the route's layout after the Mirage Tower falls: the same corner, the same entry
        self.assertEqual(SEAM_RAMPS["LAYOUT_ROUTE111_NO_MIRAGE_TOWER"], ramp)
        layout = next(row for row in layouts if row["id"] == lid)
        width, height = layout["width"], layout["height"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        fallen = next(row for row in layouts if row["id"] == "LAYOUT_ROUTE111_NO_MIRAGE_TOWER")
        fallen_cells = _load_cells(ROOT, [fallen], tilesets, names)[0][fallen["id"]]
        self.assertEqual([(cell["metatile"], cell["collision"]) for cell in cells[130 * width:]],
                         [(cell["metatile"], cell["collision"]) for cell in fallen_cells[130 * width:]])
        walk = lambda offset: cells[offset]["collision"] == 0
        flat = hand_floors(cells, width, ramp["floors"])
        banks = hand_banks(cells, width, ramp["banks"])
        self.assertEqual({entry["groundQ16"] for entry in flat.values()}, {0})
        self.assertEqual({(entry["groundQ16"], entry["lift"]) for entry in banks.values()}, {(0, True)})
        ((x0, y0, x1, y1), _), = ramp["floors"]
        self.assertEqual(y1, height - 1)                                      # it reaches the seam
        # the floors' cells are one pocket the rest of the route does not reach, its last row the seam's
        pocket, queue = set(flat), list(flat)
        while queue:
            offset = queue.pop()
            x, y = offset % width, offset // width
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < width and 0 <= ny < height and walk(ny * width + nx):
                    self.assertIn(ny * width + nx, pocket)                    # nothing walkable beyond it
        self.assertTrue(all((height - 1) * width + x in flat for x in range(x0, x1 + 1)))
        # the fence and rocks round it on its level, none of the route's walkable ground
        self.assertFalse(any(walk(offset) for offset in banks))
        self.assertTrue(all(offset // width >= y0 for offset in banks))

    def test_a_town_of_stair_terraces(self):
        # Streets a flight of stairs climbs between: the street by the water on level 0, the one at the flight's top
        # a level up; the flight climbs a share a row; the rails at the upper street's edge stand on it, the rock
        # and the house on the street south of them; a stair tile joining no two streets (a doorstep) lies flat.
        #   y0  U U U U U U
        #   y1  R R T T R H      U, L streets; T the flight; R rails; B rock; H a house; D a doorstep
        #   y2  B B T T B H
        #   y3  L L L L L L
        #   y4  L L D L L L
        #   y5  W W W W W W
        def tile(kind):
            base = {"tileset": "gTileset_Sootopolis", "localMetatile": 1, "metatile": 513,
                    "behavior": "MB_NORMAL", "collision": 0, "elevation": 3}
            if kind in "TD":
                base["localMetatile"] = 68
            elif kind == "R":
                base.update(localMetatile=0x41, collision=1)
            elif kind in "BH":
                base.update(localMetatile=2, collision=1)
            elif kind == "W":
                base.update(behavior="MB_DEEP_WATER", elevation=1)
            return base
        rows = ("UUUUUU", "RRTTRH", "BBTTBH", "LLLLLL", "LLDLLL", "WWWWWW")
        width, height = 6, len(rows)
        cells = tuple(tile(kind) for row in rows for kind in row)
        stairs = {("gTileset_Sootopolis", 68)}
        out = stair_terraces(cells, width, height, stairs, {("gTileset_Sootopolis", 0x41)})
        at = lambda x, y: out[y * width + x]
        self.assertTrue(all((at(x, 0)["groundQ16"], at(x, 0)["topQ16"], at(x, 0)["profile"]) == (16, 16, "none")
                            for x in range(width)))
        self.assertTrue(all(at(x, 3)["groundQ16"] == 0 for x in range(width)))
        for x in (2, 3):                                     # the flight, from the lower street up to the upper
            self.assertEqual([(at(x, y)["groundQ16"], at(x, y)["topQ16"], at(x, y)["profile"]) for y in (2, 1)],
                             [(0, 8, "transition-horizontal"), (8, 16, "transition-horizontal")])
        for x in (0, 1, 4):                                  # the rails on the upper street, the rock on the lower
            self.assertEqual((at(x, 1)["groundQ16"], at(x, 1)["lift"]), (16, True))
            self.assertEqual((at(x, 2)["groundQ16"], at(x, 2)["lift"]), (0, True))
        for y in (1, 2):                                     # the house faces the lower street
            self.assertEqual((at(5, y)["groundQ16"], at(5, y)["lift"]), (0, True))
        self.assertEqual((at(2, 4)["groundQ16"], at(2, 4)["topQ16"], at(2, 4)["profile"]), (0, 0, "none"))
        self.assertFalse(any(y * width + x in out for y in (5,) for x in range(width)))     # the water left alone

        # a flight whose top and foot are one street (the street round its wall) gives it two levels: nothing
        rows = ("UUUU", "UBTB", "UBTB", "UUUU", "WWWW")
        cells = tuple(tile(kind) for row in rows for kind in row)
        self.assertEqual(stair_terraces(cells, 4, len(rows), stairs), {})
        # no street by the water: nothing to start the levels from
        rows = ("UUUU", "BBTB", "LLLL")
        cells = tuple(tile(kind) for row in rows for kind in row)
        self.assertEqual(stair_terraces(cells, 4, len(rows), stairs), {})

    def test_route119s_mountain_and_bridge_given_by_hand(self):
        # Route 119's two tops and the bridge between them stand a level up; the bridge's cells (elevation 15, over
        # two paths and the river) and the walkway over the forest are decks; the stairs climb to the tops.
        from compile_rules import MOUNTAIN_LEVELS
        tilesets = load_tilesets(ROOT)
        _, layouts = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        lid = "LAYOUT_ROUTE119"
        layout = next(row for row in layouts if row["id"] == lid)
        width, height = layout["width"], layout["height"]
        cells = _load_cells(ROOT, [layout], tilesets, names)[0][lid]
        ground = mountain_by_hand(cells, width, height, MOUNTAIN_LEVELS[lid])
        decks = mountain_by_hand(cells, width, height, MOUNTAIN_LEVELS[lid], decks=True)
        self.assertFalse(set(ground) & set(decks))
        self.assertTrue(all(entry["deck"] for entry in decks.values()))
        self.assertFalse(any(entry.get("deck") for entry in ground.values()))
        out = {**ground, **decks}
        at = lambda x, y: out.get(y * width + x)
        for x, y in ((5, 83), (36, 85), (34, 92), (35, 89)):            # the tops, the east tiers' stairs flat
            self.assertEqual((at(x, y)["groundQ16"], at(x, y)["topQ16"], at(x, y).get("deck")), (16, 16, None))
        for x in (7, 13, 18, 21, 22, 23, 25, 26, 27, 33):                 # the bridge, end to end
            for y in (84, 85):
                self.assertEqual((at(x, y)["groundQ16"], at(x, y)["deck"]), (16, True))
        for x, y in ((13, 83), (13, 86), (26, 86), (21, 83), (1, 84)):    # the paths and the river under it, the strip
            self.assertIsNone(at(x, y))
        # the west stairs climb from the grass north of them over two rows, the east ones from the ground
        self.assertEqual([(at(5, y)["groundQ16"], at(5, y)["topQ16"], at(5, y)["profile"]) for y in (79, 80)],
                         [(-16, 0, "transition-south"), (0, 16, "transition-south")])
        self.assertEqual((at(30, 97)["groundQ16"], at(30, 97)["topQ16"], at(30, 97)["profile"]),
                         (0, 16, "transition-horizontal"))
        # the grass bank along the river's west side a level over the rock plateau its stairs climb from (the owner's
        # picture of 2026-11-13): the whole bank, its stairs from the plateau's level; the plateau, the grass south of
        # it and the river left to the solver
        for x, y in ((16, 46), (12, 30), (8, 40), (20, 47), (14, 50)):
            self.assertEqual((at(x, y)["groundQ16"], at(x, y)["topQ16"], at(x, y).get("deck")), (16, 16, None))
        for x in (21, 22):
            self.assertEqual((at(x, 48)["groundQ16"], at(x, 48)["topQ16"], at(x, 48)["profile"]),
                             (0, 16, "transition-horizontal"))
        for x, y in ((20, 51), (25, 49), (18, 55), (25, 45)):
            self.assertIsNone(at(x, y))
        self.assertFalse(any(cells[offset]["collision"] for offset in out))

    def test_stairs_whose_ends_do_not_meet_the_ground_lie_between_its_levels(self):
        # A column: ground, General's terrace stairs (175 over 207), ground. Mossdeep City's: the stairs' cells left
        # where no pass put them (the top level 0) between ground on one level - they lie flat on it; between two
        # levels - they climb from the lower to the higher a share a row; already meeting both ends - left alone.
        def column(kinds):
            return tuple({"collision": 0, "tileset": "gTileset_General", "elevation": 3,
                          "localMetatile": {"g": 1, "t": 175, "b": 207}[kind]} for kind in kinds)
        cells = column("gtbg")
        flat = {0: {"groundQ16": -16, "topQ16": -16, "profile": "none"},
                3: {"groundQ16": -16, "topQ16": -16, "profile": "none"}}
        out = stair_runs_between(cells, 1, 4, flat)
        self.assertEqual({o: (e["groundQ16"], e["topQ16"]) for o, e in out.items()}, {1: (-16, -16), 2: (-16, -16)})
        two = {0: {"groundQ16": 0, "topQ16": 0, "profile": "none"}, 2: {"groundQ16": -16, "topQ16": -16, "profile": "none"},
               3: {"groundQ16": -16, "topQ16": -16, "profile": "none"}}
        out = stair_runs_between(cells, 1, 4, two)
        self.assertEqual({o: (e["groundQ16"], e["topQ16"], e["profile"]) for o, e in out.items()},
                         {1: (-8, 0, "transition-horizontal"), 2: (-16, -8, "transition-horizontal")})
        # the Battle Frontier's: the top row on the upper level, the foot a ramp (its rise in its entry) - they meet
        met = {0: {"groundQ16": 0, "topQ16": 0, "profile": "none"},
               2: {"groundQ16": -16, "topQ16": -16, "profile": "transition-horizontal", "rise": 16},
               3: {"groundQ16": -16, "topQ16": -16, "profile": "none"}}
        self.assertEqual(stair_runs_between(cells, 1, 4, met), {})
        # Route 119's west stairs climb south from a ramp of grass: they meet it
        cells = column("ggtg")
        south = {1: {"groundQ16": -16, "topQ16": -16, "profile": "transition-south", "rise": 16},
                 2: {"groundQ16": 0, "topQ16": 0, "profile": "transition-south", "rise": 16},
                 3: {"groundQ16": 16, "topQ16": 16, "profile": "none"}}
        self.assertEqual(stair_runs_between(cells, 1, 4, south), {})

if __name__ == "__main__":
    unittest.main()
