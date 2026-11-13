#!/usr/bin/env python3

import copy
import hashlib
import json
import struct
import tempfile
import unittest
from collections import Counter
from pathlib import Path
from types import SimpleNamespace
from compile_rules import (ARCHETYPES, PASS_LAYOUTS, RuleError, _action, _ground_policy, _grounded_forests, _mask,
                              _walked_rock_pins,
                              _mountain_masses,
                              _pattern_placements, _patterns, _profiles, _selector,
                              _validate_logical_ids, _validate_pattern_claim_overlaps,
                              _validate_pixel_object_catalog,
                             _load_cells, compile_data, load_json, parse_behaviors, parse_camera,
                             render_c, render_header)
from catalog import load_tilesets, load_world
from profiles import shape_from_action
from migrate_v1_to_v2 import MigrationError, migrate_document


ROOT = Path(__file__).resolve().parents[2]


class DioramaRuleCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = compile_data(ROOT)

    def test_repository_ir_is_complete_and_deterministic(self):
        second = compile_data(ROOT)
        self.assertEqual(self.data, second)
        self.assertEqual(render_c(self.data), render_c(second))
        self.assertEqual(len(self.data["tilesets"]), 75)
        self.assertEqual([row["id"] for row in self.data["tilesets"]], list(range(1, 76)))
        self.assertEqual(len(self.data["layouts"]), 441)
        self.assertEqual(sum(row["width"] * row["height"] for row in self.data["layouts"]),
                         sum(record["mapGroup"] == 0xFF for row in self.data["layouts"]
                             for record in row["terrainRecords"]))
        self.assertEqual([row["id"] for row in self.data["layouts"]], list(range(1, 442)))
        self.assertEqual(len(self.data["maps"]), 518)
        self.assertEqual(len(self.data["tilesets"]), 75)
        self.assertEqual({row["terrainClass"] for row in self.data["tilesets"]},
                         {"ground"})
        self.assertEqual(sum(row["supported"] for row in self.data["maps"]), 518)
        references = {"MAP_FORTREE_CITY", "MAP_SOOTOPOLIS_CITY", "MAP_MT_CHIMNEY",
                      "MAP_JAGGED_PASS", "MAP_GRANITE_CAVE_B1F", "MAP_MT_PYRE_2F"}
        self.assertTrue(references <= {row["symbol"] for row in self.data["maps"]
                                      if row["supported"]})
        indoor = next(row for row in self.data["maps"]
                      if row["mapType"] == "MAP_TYPE_INDOOR"
                      and row["symbol"] not in {"MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F",
                                                "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F",
                                                "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_1F",
                                                "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_2F",
                                                "MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB"})
        self.assertEqual(indoor["camera"]["profile"], "interior")
        self.assertTrue(all(row["unsupportedReason"] for row in self.data["maps"]
                            if not row["supported"]))

    def test_repository_layouts_have_compiled_classifier_terrain(self):
        for layout in self.data["layouts"]:
            self.assertGreater(layout["width"], 0)
            self.assertGreater(layout["height"], 0)
        self.assertGreater(sum(row["terrainRecordCount"] for row in self.data["layouts"]), 0)
        self.assertTrue(self.data["ambiguities"])
        self.assertEqual(render_c(self.data), render_c(compile_data(ROOT)))

    def test_repository_has_authoritative_pin_and_r3_r4_patterns(self):
        self.assertEqual(self.data["profiles"], [])
        self.assertEqual([(row["behavior"], row["action"]["archetype"],
                           row["action"]["height"])
                          for row in self.data["behaviorRules"]],
                         [("MB_MOUNTAIN_TOP", "cliff", 1.0)])
        self.assertEqual([row["id"] for row in self.data["contextualRules"]],
                         ["general-rock-terrace-cell", "general-dense-forest-cell",
                          "underwater-stolen-submarine"])
        placements = {row["id"]: row["placementCount"]
                      for row in self.data["exactPatterns"]}
        self.assertTrue(all(placements[name] > 0 for name in (
            "general-large-tree-canonical", "general-tree-body-canonical",
            "general-small-tree-canonical")))
        self.assertEqual({name: placements[name] for name in (
            "petalburg-lab-shell", "petalburg-house-left-shell",
            "petalburg-house-right-shell", "pokemon-center-service-counter",
            "generic-building-potted-plant", "brendan-tv-support", "may-tv-support",
            "brendan-tv-cutout", "may-tv-cutout", "brendan-tv-base", "may-tv-base",
            "generic-building-left-stool", "generic-building-right-stool",
            "generic-building-table-relief", "petalburg-isolated-signpost")}, {
                "petalburg-lab-shell": 1, "petalburg-house-left-shell": 1,
                "petalburg-house-right-shell": 1, "pokemon-center-service-counter": 3,
                "generic-building-potted-plant": 5, "brendan-tv-support": 1,
                "may-tv-support": 1, "brendan-tv-cutout": 1, "may-tv-cutout": 1,
                "brendan-tv-base": 1, "may-tv-base": 1,
                "generic-building-left-stool": 6, "generic-building-right-stool": 10,
                "generic-building-table-relief": 1, "petalburg-isolated-signpost": 2,
            })
        # The battle environments' pins (tools/battle_env/cave_pins.py: the caves' and the sea
        # floor's rock; the secret bases' walls) stand rock and walls up as cliffs one tile high;
        # every other pin is the authoritative one below.
        battle_env = [pin for pin in self.data["tilesetPins"]
                      if "battle-environment overhaul" in pin["reason"]]
        self.assertTrue(battle_env)
        self.assertEqual({(pin["action"]["archetype"], pin["action"]["pool"], pin["action"]["height"])
                          for pin in battle_env}, {("cliff", "terrain", 1.0)})
        self.assertEqual({pin["tileset"] for pin in battle_env}, {
            "gTileset_Cave", "gTileset_MeteorFalls", "gTileset_RusturfTunnel", "gTileset_NavelRock",
            "gTileset_Underwater", "gTileset_SecretBaseRedCave", "gTileset_SecretBaseBrownCave",
            "gTileset_SecretBaseBlueCave", "gTileset_SecretBaseYellowCave", "gTileset_SecretBaseTree",
            "gTileset_SecretBaseShrub"})
        # the stolen submarine at the Seafloor Cavern's way in under the sea is no rock: its top
        # row stood as a rock block over the rest of it (cave_pins.NOT_ROCK, KNOWN_ISSUES MS-227)
        self.assertFalse([pin for pin in battle_env if pin["tileset"] == "gTileset_Underwater"
                          and pin["metatile"] in (0xE4, 0xE5, 0xE6, 0xE7, 0x2E4, 0x2E5, 0x2E6, 0x2E7)])
        self.assertTrue([pin for pin in battle_env if pin["tileset"] == "gTileset_Underwater"])
        # its other two rows are deep water, drawn in the 2D game as the sea floor under it: they lay as
        # flat sea floor, not water with its surface over the submarine (an edge of it across the hull);
        # the hollow's own deep water round it keeps its surface (defaults.json, KNOWN_ISSUES MS-227)
        submarine = next(row for row in self.data["contextualRules"]
                         if row["id"] == "underwater-stolen-submarine")
        self.assertEqual(submarine["placementCount"], 8)
        hollow = next(row for row in self.data["layouts"]
                      if row["symbol"] == "LAYOUT_UNDERWATER_SEAFLOOR_CAVERN")
        cells = {(row["cellOffset"] % hollow["width"], row["cellOffset"] // hollow["width"]): row
                 for row in hollow["terrainRecords"]}
        self.assertEqual({(cells[(x, y)]["shape"], cells[(x, y)]["class"], cells[(x, y)]["source"])
                          for x in range(5, 9) for y in (4, 5)},
                         {("flat", "ground", "context:underwater-stolen-submarine")})
        self.assertEqual({cells[cell]["shape"] for cell in [(4, 5), (9, 5)] + [(x, 6) for x in range(3, 10)]},
                         {"water"})
        pins = [pin for pin in self.data["tilesetPins"] if pin not in battle_env]
        self.assertEqual(len(pins), 1)
        pin = pins[0]
        self.assertEqual((pin["tileset"], pin["metatile"], pin["placementCount"]),
                         ("gTileset_General", 4, 615))
        self.assertEqual((pin["action"]["archetype"], pin["action"]["pool"]),
                         ("flower", "vegetation"))
        self.assertIn("General_Flower", pin["reason"])
        self.assertTrue(all(row["contextualRules"] == [] for row in self.data["maps"]))
        self.assertTrue(all(row["exactPatterns"] == [] for row in self.data["maps"]))

    def test_repository_mountains_are_one_block_art_masked_cliffs(self):
        mountains = [record for layout in self.data["layouts"]
                     for record in layout["terrainRecords"]
                     if record["source"] == "behavior:MB_MOUNTAIN_TOP"]
        # (the blocked ones: about 16,600 records; the walkable ones are ground, below)
        self.assertGreater(len(mountains), 15000)
        self.assertEqual({record["class"] for record in mountains}, {"cliff"})
        # one block, but the rock of a mountain of several tops (Route 114's, Route 115's:
        # _mountain_masses), from its lowest ground to the top it rises to, and of the two that
        # stand at the game's heights since 2026-10-29 (Route 112's plateau three levels up,
        # Route 111's two: compile_rules.GBA_HEIGHT_LAYOUTS) - whole levels; and since step 16 every
        # other band of rock one surface (rock_bands.py): a face from the lower level it parts to the
        # higher and over it inward, a ridge a tile and more inward, never more than two tiles over
        # the highest ground it meets
        self.assertTrue({16, 32, 48, 64} <= {record["heightQ16"] for record in mountains})
        self.assertTrue(all(1 <= record["heightQ16"] <= 80 for layout in self.data["layouts"]
                            if layout["symbol"] not in PASS_LAYOUTS for record in layout["terrainRecords"]
                            if record["source"] == "behavior:MB_MOUNTAIN_TOP"))
        # a mountain pass's rock (PASS_LAYOUTS: Jagged Pass) is one band round all its levels, so it stands from
        # the pass's foot; it never stands more than two tiles over the pass's highest ground
        for layout in self.data["layouts"]:
            if layout["symbol"] in PASS_LAYOUTS:
                rock = [record for record in layout["terrainRecords"]
                        if record["source"] == "behavior:MB_MOUNTAIN_TOP"]
                highest = max(record["groundQ16"] for record in layout["terrainRecords"]
                              if record["class"] == "ground")
                self.assertTrue(rock)
                self.assertTrue(all(record["heightQ16"] >= 1 and record["topQ16"] <= highest + 32
                                    for record in rock))
        self.assertEqual({record["measuredFlags"] & 32 for record in mountains}, {32})
        # Mossdeep's cliff (the owner's "tall, narrow, rectangular cliffs"): its corner pieces stood
        # a tile over the plateau whose face they are, two over the street; they are that face now
        mossdeep = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_MOSSDEEP_CITY")
        corners = [(record["groundQ16"], record["topQ16"]) for record in mossdeep["terrainRecords"]
                   if record["mapGroup"] == 0xFF and record["expectedMetatile"] == 0x89]
        self.assertGreaterEqual(corners.count((-16, 0)), 30)
        self.assertTrue(all(top - ground <= 32 for ground, top in corners))
        # (a reef in the sea stands on the sea's level: a terrace entry, flag 128)
        self.assertEqual({record["measuredFlags"] & ~(32 | 128) for record in mountains}, {0})
        # "cutout": the cell's upper layer draws the rock's edge over ground (mountain_art.outline_cells)
        self.assertEqual({record["artMode"] for record in mountains}, {"top", "cutout"})
        # The game tags its mountain paths MB_MOUNTAIN_TOP too (the battle background): a cell the
        # player walks on is ground, never mountain art (Route 112's paths stood as little pyramids).
        walkable = [record for layout in self.data["layouts"]
                    for record in layout["terrainRecords"]
                    if record["source"] == "behavior:MB_MOUNTAIN_TOP:walkable"]
        self.assertGreater(len(walkable), 2000)
        self.assertEqual({record["class"] for layout in self.data["layouts"] if layout["symbol"] not in PASS_LAYOUTS
                          for record in layout["terrainRecords"]
                          if record["source"] == "behavior:MB_MOUNTAIN_TOP:walkable"}, {"ground"})
        # (on a pass, also the ramp its solver lays between an island's levels: terrace_topology._pass_ramp)
        self.assertTrue({record["class"] for record in walkable} <= {"ground", "stairs"})
        self.assertEqual({record["measuredFlags"] & 32 for record in walkable}, {0})

    def test_repository_rock_masses_reefs_and_terrace_stairs(self):
        # The owner's report of 2026-10-24. Route 106's mountain: its face and edges (blocked
        # MB_NORMAL in the rock's palette) are rock of the same mass, not flat ground beside it.
        layouts = {row["symbol"]: row for row in self.data["layouts"]}
        route106 = layouts["LAYOUT_ROUTE106"]
        records = {record["cellOffset"]: record for record in route106["terrainRecords"]
                   if record["mapGroup"] == 0xFF}
        width = route106["width"]
        faces = [record for record in records.values() if record["expectedMetatile"] in (116, 124, 125)]
        self.assertTrue(faces)
        self.assertTrue(all(record["class"] == "cliff" and record["measuredFlags"] & 32
                            for record in faces))
        # A reef in the sea (9..11, 2..4): its walkable top no walk reaches and its blocked edge are
        # one mass standing on the sea's level (-2), its edges the rock's outline.
        reef = [records[y * width + x] for y in range(2, 5) for x in range(9, 12)]
        self.assertTrue(all(record["class"] == "cliff" and record["measuredFlags"] & 32
                            for record in reef))
        self.assertEqual({record["groundQ16"] for record in reef}, {-2})
        self.assertEqual(records[2 * width + 9]["artMode"], "cutout")
        # General's stairs between two terrace levels are steps up from the lower level.
        route104 = layouts["LAYOUT_ROUTE104"]
        stairs = {record["cellOffset"]: record for record in route104["terrainRecords"]
                  if record["mapGroup"] == 0xFF}
        for x in (16, 17):
            record = stairs[54 * route104["width"] + x]
            self.assertEqual((record["shape"], record["archetype"], record["groundQ16"],
                              record["topQ16"]), ("stairs", "stairs-n", -16, 0))
        # A cave's rock is a mass as a mountain's is.
        granite = layouts["LAYOUT_GRANITE_CAVE_1F"]
        rock = [record for record in granite["terrainRecords"]
                if record["source"] == "context:cave-rock" and record["class"] == "cliff"]
        self.assertTrue(rock)
        self.assertTrue(all(record["measuredFlags"] & 32 for record in rock))

    def test_a_mountain_whose_top_is_its_paths_is_one_mass(self):
        # rock (M) round a boulder (B), grass a level down (L) and the mountain's own path (P,
        # walkable MB_MOUNTAIN_TOP) on the top level: one mass from the grass to the path, the
        # boulder on its top.
        def make(ch):
            return {"M": {"behavior": "MB_MOUNTAIN_TOP", "collision": 1},
                    "B": {"behavior": "MB_NORMAL", "collision": 1},
                    "L": {"behavior": "MB_NORMAL", "collision": 0},
                    "P": {"behavior": "MB_MOUNTAIN_TOP", "collision": 0},
                    "W": {"behavior": "MB_OCEAN_WATER", "collision": 0}}[ch]

        def build(rows):
            return tuple(make(ch) for row in rows for ch in row)

        def level(value):
            return {"groundQ16": value, "topQ16": value, "profile": "none", "solid": False}

        rows = ["MMMMM", "MMBMM", "MMMMM", "LLLPP"]
        cells = build(rows)
        mountain = frozenset(index for index, ch in enumerate("".join(rows)) if ch == "M")
        solved = {15: level(-16), 16: level(-16), 17: level(-16), 18: level(0), 19: level(0)}
        masses = _mountain_masses(cells, 5, 4, mountain, solved)
        self.assertEqual({(masses[offset]["groundQ16"], masses[offset]["topQ16"])
                          for offset in mountain}, {(-16, 0)})
        self.assertTrue(all(masses[offset]["mass"] for offset in mountain))
        self.assertEqual((masses[7]["groundQ16"], masses[7]["lift"]), (0, True))
        # the upper ground is no path of the mountain's (a desert beside it), the sea beside it,
        # ground on three levels: it keeps its cells' entries
        plain = cells[:18] + (make("L"), make("L"))
        self.assertEqual(_mountain_masses(plain, 5, 4, mountain, solved), {})
        wet = cells[:15] + (make("W"),) + cells[16:]
        self.assertEqual(_mountain_masses(wet, 5, 4, mountain, solved), {})
        self.assertEqual(_mountain_masses(cells, 5, 4, mountain, {**solved, 17: level(-32)}), {})
        # one level all round, the top level: the rock already where its records put it, the
        # boulder in it on its top (it sank into the rock, standing on the ground)
        self.assertEqual(_mountain_masses(cells, 5, 4, mountain,
                                          {offset: level(0) for offset in solved}),
                         {7: {"groundQ16": 16, "topQ16": 16, "profile": "none", "solid": False,
                              "lift": True}})
        # A mountain whose stairs climb to a top two levels up and more (`climbed`:
        # mountain_ledges, Route 115's) is one mass on its lowest ground, each cell as high as
        # the ground nearest it asks (a level over the grass, level with the path) or on a face
        # down from the higher top, a level a row; with no climb (two levels the terrace
        # solver's) it keeps its cells' entries.
        rows = ["PPPPP", "MMMMM", "MMMMM", "MMMMM", "LLLLL"]
        cells = build(rows)
        mountain = frozenset(index for index, ch in enumerate("".join(rows)) if ch == "M")
        solved = {offset: level(48) for offset in range(5)}
        masses = _mountain_masses(cells, 5, 5, mountain, solved, climbed=frozenset(range(5)))
        self.assertEqual([(masses[offset]["groundQ16"], masses[offset]["topQ16"])
                          for offset in (7, 12)], [(0, 48), (0, 32)])
        self.assertNotIn(17, masses)  # (0, 16): where its record puts it
        self.assertEqual(_mountain_masses(cells, 5, 5, mountain, solved), {})

    def test_a_forest_beside_a_raised_top_stays_on_its_ground(self):
        # Route 112's forest west of its plateau (three levels up since 2026-10-29): trees (F) between a raised top
        # mountain_ledges climbs to (T) and the grass under it (G) would each take the level of the walkable cell
        # nearest it, the trees by the top three levels up and a wall of their sides between; the forest stays on
        # its ground (_grounded_forests). Not a forest: blocked cells that are not trees, the mountain's own cells,
        # jump ledges, or a group with no lower ground beside it.
        def make(ch, behavior=None):
            cell = {"T": {"behavior": "MB_MOUNTAIN_TOP", "collision": 0},
                    "F": {"behavior": "MB_NORMAL", "collision": 1},
                    "G": {"behavior": "MB_NORMAL", "collision": 0}}[ch]
            return dict(cell, behavior=behavior) if behavior else cell

        def level(value):
            return {"groundQ16": value, "topQ16": value, "profile": "none", "solid": False}

        rows = ["TTTT", "FFFF", "GGGG"]
        cells = tuple(make(ch) for row in rows for ch in row)
        solved = {offset: level(48) for offset in range(4)}
        climbed = frozenset(range(4))
        trees = tuple(SimpleNamespace(class_name="round-hull") for _ in cells)
        grounds = tuple(SimpleNamespace(class_name="ground") for _ in cells)
        anything = lambda offset: True
        self.assertEqual(_grounded_forests(cells, 4, 3, solved, climbed, anything, trees),
                         frozenset({4, 5, 6, 7}))
        self.assertEqual(_grounded_forests(cells, 4, 3, solved, climbed, anything, grounds), frozenset())
        self.assertEqual(_grounded_forests(cells, 4, 3, solved, climbed, anything, trees,
                                           mountain=frozenset({4, 5, 6, 7})), frozenset())
        ledges = tuple(make(ch, "MB_JUMP_SOUTH") if ch == "F" else make(ch) for row in rows for ch in row)
        self.assertEqual(_grounded_forests(ledges, 4, 3, solved, climbed, anything, trees), frozenset())
        self.assertEqual(_grounded_forests(cells, 4, 3, {**solved, **{offset: level(48) for offset in range(8, 12)}},
                                           climbed, anything, trees), frozenset())

    def test_rock_by_a_forest_on_its_ground_climbs_from_it(self):
        # Rock (M) beside a top three levels up that stairs climb to (P, `climbed`), the grass below (L) and trees on
        # the grass (F): level with the top beyond the trees, it rose three levels in its one row by them (Route
        # 112's forest, the crowns the game draws over the rock drawn down that face in stripes). By a forest that
        # stays on its ground (`forest`) the rock stands a level over the trees and climbs from them a level a cell.
        def make(ch):
            return {"M": {"behavior": "MB_MOUNTAIN_TOP", "collision": 1},
                    "P": {"behavior": "MB_MOUNTAIN_TOP", "collision": 0},
                    "L": {"behavior": "MB_NORMAL", "collision": 0},
                    "F": {"behavior": "MB_NORMAL", "collision": 1}}[ch]

        def level(value):
            return {"groundQ16": value, "topQ16": value, "profile": "none", "solid": False}

        rows = ["MMMPP", "MMMPP", "MMMPP", "FFLLL"]
        flat = "".join(rows)
        cells = tuple(make(ch) for ch in flat)
        mountain = frozenset(index for index, ch in enumerate(flat) if ch == "M")
        tops = frozenset(index for index, ch in enumerate(flat) if ch == "P")
        solved = {offset: level(48) for offset in tops}

        def heights(masses):
            return [[masses[y * 5 + x]["topQ16"] if y * 5 + x in masses else 16 for x in range(3)]
                    for y in range(3)]

        self.assertEqual(heights(_mountain_masses(cells, 5, 4, mountain, solved, climbed=tops)),
                         [[48, 48, 48], [48, 48, 48], [32, 32, 48]])
        masses = _mountain_masses(cells, 5, 4, mountain, solved, climbed=tops, forest=frozenset({15, 16}))
        self.assertEqual(heights(masses), [[48, 48, 48], [32, 32, 48], [16, 16, 32]])
        self.assertFalse({10, 11, 15, 16} & set(masses))  # a level over the trees: where their records put them

    def test_repository_mountains_stand_on_one_level(self):
        # The owner's report of 2026-10-24 ("a maze of terraced walls", "the light blue
        # emptiness"): Route 116's mountain is one mass from the grass below it to the paths on
        # it - its face no longer at both levels cell by cell; its tunnel's mouth on the grass's
        # level; the face its ledge path stands on rock; its boulders on its top.
        layouts = {row["symbol"]: row for row in self.data["layouts"]}

        def wildcard(name):
            layout = layouts[name]
            return layout["width"], {record["cellOffset"]: record
                                     for record in layout["terrainRecords"]
                                     if record["mapGroup"] == 0xFF}

        # (Route 116 a level up since MS-162: its grass meets Rustboro City's ground)
        width, records = wildcard("LAYOUT_ROUTE116")
        front = [records[8 * width + x] for x in range(43, 53) if x != 47]
        self.assertEqual({(record["class"], record["groundQ16"], record["topQ16"])
                          for record in front}, {("cliff", 0, 16)})
        self.assertEqual(records[8 * width + 47]["groundQ16"], 0)
        for y in (12, 13, 14):
            record = records[y * width + 54]
            self.assertEqual((record["class"], record["groundQ16"], record["topQ16"]),
                             ("cliff", 0, 16))
        for x, y in ((42, 4), (43, 6)):
            self.assertEqual(records[y * width + x]["groundQ16"], 16)
        # Route 112: its plateau three levels over the ground round the mountain, the stairs rising
        # to it a level a row (the owner's decision of 2026-10-29: "Raise to the GBA heights"); the
        # mountain level with it, down its face a level a row ((15, 42) a row under its edge), a
        # level over the paved ground by the cable car station ((30, 20)), and a level over the grass
        # far north of it ((5, 10)); the forest west of the plateau on the ground ((3, 38)), the rock
        # by it a level over it and climbing from it a level a cell, and the tree in that rock (a small
        # tree's trunk, (3, 36)) on the ground it grows from, the rock's top beside it, not at the rock's
        # foot (rock_bands.tree_grounds: the renderer stands it as a solid).
        width, records = wildcard("LAYOUT_ROUTE112")
        self.assertEqual(records[33 * width + 24]["groundQ16"], 48)
        self.assertEqual(records[45 * width + 20]["groundQ16"], 0)
        self.assertEqual([(records[y * width + 20]["shape"], records[y * width + 20]["groundQ16"],
                           records[y * width + 20]["topQ16"]) for y in (43, 42, 41)],
                         [("stairs", 0, 16), ("stairs", 16, 32), ("stairs", 32, 48)])
        for (x, y), top in (((5, 10), 16), ((15, 42), 32), ((30, 20), 64), ((3, 37), 16),
                            ((8, 37), 16), ((12, 41), 16), ((13, 41), 32)):
            record = records[y * width + x]
            self.assertEqual((record["class"], record["groundQ16"], record["topQ16"]),
                             ("cliff", 0, top), (x, y))
        self.assertEqual(records[36 * width + 3]["groundQ16"], 32)
        self.assertEqual((records[38 * width + 3]["shape"], records[38 * width + 3]["groundQ16"]), ("flat", 0))
        # Route 111's rock runs the length of the map, beside the desert and its plateau two levels
        # up (its slopes' two rows): one mass from the lowest ground it meets, a level over the
        # desert, level with the plateau's paths and down its faces a level a cell ((35, 30)).
        width, records = wildcard("LAYOUT_ROUTE111")
        self.assertEqual((records[30 * width + 35]["groundQ16"], records[30 * width + 35]["topQ16"]),
                         (-16, 32))

    def test_repository_mountains_of_several_tops(self):
        # KNOWN_ISSUES MS-153: Route 115's muddy slope lay flat, its mountain on one level (its
        # stairs climb from one top to another, and the mountain's step kept the layout's
        # levels). Its tops stand a level a climb, the slope a ramp between them, the rock level
        # with each top and down its faces; Route 114's mountain of five levels the same (Route
        # 119's tops, joined by the bridge the game lets one walk across, stand a level up as
        # given by hand: compile_rules MOUNTAIN_LEVELS). No rock stands over the rock beside it
        # with nothing under it (the sky between: the mesh draws rock from a voxel under its
        # ground).
        layouts = {row["symbol"]: row for row in self.data["layouts"]}

        def wildcard(name):
            layout = layouts[name]
            return layout["width"], {record["cellOffset"]: record
                                     for record in layout["terrainRecords"]
                                     if record["mapGroup"] == 0xFF}

        width, records = wildcard("LAYOUT_ROUTE115")

        def at(x, y):
            record = records[y * width + x]
            return record["shape"], record["groundQ16"], record["topQ16"]

        self.assertEqual([at(24, 10), at(32, 12), at(24, 16)],
                         [("flat", 16, 16), ("flat", 32, 32), ("flat", 0, 0)])
        self.assertEqual([at(28, 9), at(28, 8)], [("stairs", 16, 24), ("stairs", 24, 32)])
        self.assertEqual([at(18, 13), at(18, 12), at(27, 12)],
                         [("stairs", 0, 8), ("stairs", 8, 16), ("stairs", 0, 16)])
        self.assertEqual([at(29, 8), at(29, 9), at(31, 13)],
                         [("cliff", 0, 32), ("cliff", 0, 16), ("cliff", 0, 32)])
        width, records = wildcard("LAYOUT_ROUTE114")
        self.assertEqual([at(12, 36), at(6, 48), at(13, 68), at(24, 66), at(13, 67)],
                         [("flat", 16, 16), ("flat", 32, 32), ("flat", 48, 48), ("flat", 64, 64),
                          ("stairs", 48, 64)])
        width, records = wildcard("LAYOUT_ROUTE119")
        self.assertEqual([at(33, 92)[1], at(36, 85)[1]], [16, 16])
        for name in ("LAYOUT_ROUTE114", "LAYOUT_ROUTE115"):
            width, records = wildcard(name)

            def column(record):
                solved = record["measuredFlags"] & 128
                return ((record["groundQ16"] if solved else 0),
                        (record["topQ16"] if solved else record["heightQ16"]))

            rock = {offset: column(record) for offset, record in records.items()
                    if record["measuredFlags"] & 32 and record["terraceProfile"] == "none"}
            for offset, (ground, top) in rock.items():
                for neighbor in (offset + 1 if (offset + 1) % width else None, offset + width):
                    if neighbor in rock:
                        other_ground, other_top = rock[neighbor]
                        self.assertLessEqual(ground - 1, other_top + 1, (name, offset, neighbor))
                        self.assertLessEqual(other_ground - 1, top + 1, (name, offset, neighbor))

    def test_repository_cave_slope_climbs(self):
        # KNOWN_ISSUES MS-153: Granite Cave B1F's halves, at one elevation, are joined only by a
        # muddy slope (9,9-10), which lay flat. Its north half stands a level up
        # (terrace_topology.cave_climbs), the slope a ramp, the cave's rock round that half
        # standing on it; no rock over the rock beside it with nothing under it.
        layout = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_GRANITE_CAVE_B1F")
        width = layout["width"]
        records = {record["cellOffset"]: record for record in layout["terrainRecords"]
                   if record["mapGroup"] == 0xFF}

        def at(x, y):
            record = records[y * width + x]
            return record["shape"], record["groundQ16"], record["topQ16"]

        self.assertEqual([at(9, 8), at(9, 9), at(9, 10), at(9, 12)],
                         [("flat", 16, 16), ("stairs", 8, 16), ("stairs", 0, 8), ("flat", 0, 0)])
        self.assertEqual([at(0, 5), at(5, 3)], [("cliff", 16, 32), ("cliff", 16, 32)])

        def column(record):
            solved = record["measuredFlags"] & 128
            return ((record["groundQ16"] if solved else 0),
                    (record["topQ16"] if solved else record["heightQ16"]))

        rock = {offset: column(record) for offset, record in records.items()
                if record["measuredFlags"] & 32 and record["terraceProfile"] == "none"}
        for offset, (ground, top) in rock.items():
            for neighbor in (offset + 1 if (offset + 1) % width else None, offset + width):
                if neighbor in rock:
                    self.assertLessEqual(ground - 1, rock[neighbor][1] + 1, (offset, neighbor))
                    self.assertLessEqual(rock[neighbor][0] - 1, top + 1, (offset, neighbor))

    def test_repository_slope_top_on_the_plateau(self):
        # KNOWN_ISSUES MS-153: Route 119's muddy slope (6,54-55) climbs to a row (5-8,53) the GBA
        # draws on the plateau, which lay a level under it, a step between them, the slope flat.
        # The row stands on the plateau's level (terrace_topology.slope_tops), the slope a ramp,
        # the rock beside it on its foot as before (not a tile over the plateau).
        layout = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_ROUTE119")
        width = layout["width"]
        records = {record["cellOffset"]: record for record in layout["terrainRecords"]
                   if record["mapGroup"] == 0xFF}

        def at(x, y):
            record = records[y * width + x]
            return record["shape"], record["groundQ16"], record["topQ16"]

        self.assertEqual([at(6, 52), at(6, 53), at(6, 54), at(6, 55), at(6, 56)],
                         [("flat", 0, 0), ("flat", 0, 0), ("stairs", -8, 0), ("stairs", -16, -8),
                          ("flat", -16, -16)])
        self.assertEqual({at(x, 53) for x in range(5, 9)}, {("flat", 0, 0)})
        self.assertEqual([at(7, 54), at(8, 54), at(7, 55)], [("cliff", -16, 0)] * 3)

    def test_repository_maps_meet_at_one_level(self):
        # KNOWN_ISSUES MS-162: each map's ground is solved alone, its top level 0, and Route 118 lay a level
        # under Mauville City, Route 119 and Route 123 round it, Route 116 a level under Rustboro City
        # (terrace_topology.seam_shifts). Their walkable ground meets at one level, and no ledge of Route 118's
        # jumps up.
        layouts = {row["symbol"]: row for row in self.data["layouts"]}
        grounds = {}

        def ground(name, x, y):
            if name not in grounds:
                grounds[name] = {record["cellOffset"]: record["groundQ16"]
                                 for record in layouts[name]["terrainRecords"] if record["mapGroup"] == 0xFF}
            return grounds[name][y * layouts[name]["width"] + x]

        seams = ([("LAYOUT_ROUTE118", 0, y, "LAYOUT_MAUVILLE_CITY", 39, y) for y in (7, 8, 9, 10)]
                 + [("LAYOUT_ROUTE118", 79, y, "LAYOUT_ROUTE123", 0, y) for y in (10, 11, 12)]
                 + [("LAYOUT_ROUTE118", x, 0, "LAYOUT_ROUTE119", x - 40, 139) for x in (57, 58, 59)]
                 + [("LAYOUT_ROUTE116", 0, y, "LAYOUT_RUSTBORO_CITY", 39, y) for y in (8, 9, 10, 11)])
        for a, ax, ay, b, bx, by in seams:
            self.assertEqual(ground(a, ax, ay), ground(b, bx, by), (a, ax, ay, b, bx, by))
        tilesets = load_tilesets(ROOT)
        _, rows = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        route = next(row for row in rows if row["id"] == "LAYOUT_ROUTE118")
        cells = _load_cells(ROOT, [route], tilesets, names)[0]["LAYOUT_ROUTE118"]
        width, height, jumps = route["width"], route["height"], 0
        steps = {"MB_JUMP_SOUTH": (0, 1), "MB_JUMP_NORTH": (0, -1), "MB_JUMP_EAST": (1, 0),
                 "MB_JUMP_WEST": (-1, 0)}
        for offset, cell in enumerate(cells):
            step = steps.get(cell.get("behavior"))
            x, y = offset % width, offset // width
            if step is None or not (0 <= x - step[0] < width and 0 <= y - step[1] < height
                                    and 0 <= x + step[0] < width and 0 <= y + step[1] < height):
                continue
            takeoff, landing = (x - step[0], y - step[1]), (x + step[0], y + step[1])
            if any(cells[py * width + px].get("collision", 0) for px, py in (takeoff, landing)):
                continue
            jumps += 1
            self.assertLessEqual(ground("LAYOUT_ROUTE118", *landing), ground("LAYOUT_ROUTE118", *takeoff),
                                 (x, y))
        self.assertGreater(jumps, 30)

    def test_route118_river_head_lies_with_the_river(self):
        # KNOWN_ISSUES MS-154: Route 118's river north of the rocks in it (21-28, 0-4: water between two sand banks,
        # nowhere the player walks) rose with the route when the route rose a level to meet the next maps (MS-162):
        # a plateau a level over the rest of the river and over the forest east of it, so the two ledges from that
        # forest onto its east bank, (29, 0-1), landed a level higher than they leave (blocked behind: never jumped).
        # Its water and banks lie level with the rest of the river and the route (compile_rules.SEAM_KEEP), the forest
        # west of it a level up as before: no ledge of Route 118's lands higher than the cell behind it, blocked or not.
        route = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_ROUTE118")
        width, height = route["width"], route["height"]
        records = {record["cellOffset"]: record["groundQ16"] for record in route["terrainRecords"]
                   if record["mapGroup"] == 0xFF}

        def ground(x, y):
            return records.get(y * width + x, 0)

        self.assertEqual([ground(x, 0) for x in range(21, 29)], [ground(24, 8)] * 8)   # the head and the lake
        self.assertEqual(ground(30, 0), ground(28, 0))                                 # the east forest and bank
        self.assertEqual(ground(19, 0), ground(21, 0) + 16)                            # the west forest a level up
        self.assertEqual(ground(18, 3), ground(18, 5))                                 # and the rock's top beside it
        tilesets = load_tilesets(ROOT)
        _, rows = load_world(ROOT)
        names = {value: name for name, value
                 in parse_behaviors(ROOT / "include/constants/metatile_behaviors.h").items()}
        layout = next(row for row in rows if row["id"] == "LAYOUT_ROUTE118")
        cells = _load_cells(ROOT, [layout], tilesets, names)[0]["LAYOUT_ROUTE118"]
        steps = {"MB_JUMP_SOUTH": (0, 1), "MB_JUMP_NORTH": (0, -1), "MB_JUMP_EAST": (1, 0), "MB_JUMP_WEST": (-1, 0),
                 "MB_JUMP_SOUTHEAST": (1, 1), "MB_JUMP_SOUTHWEST": (-1, 1), "MB_JUMP_NORTHEAST": (1, -1),
                 "MB_JUMP_NORTHWEST": (-1, -1)}
        ledges = 0
        for offset, cell in enumerate(cells):
            step = steps.get(cell.get("behavior"))
            x, y = offset % width, offset // width
            if step is None or not (0 <= x - step[0] < width and 0 <= y - step[1] < height
                                    and 0 <= x + step[0] < width and 0 <= y + step[1] < height):
                continue
            ledges += 1
            self.assertLessEqual(ground(x + step[0], y + step[1]), ground(x - step[0], y - step[1]), (x, y))
        self.assertGreater(ledges, 90)

    def test_repository_safari_zone_south_walkway_and_plateau(self):
        # KNOWN_ISSUES MS-222 (the owner's report of 2026-11-09): Safari Zone South's white rails (17-25, 3-7) run
        # from its west enclosure to its east plateau over its valley, the three of elevation 5 over the valley's 3.
        # They lay on the valley like benches, white stairs at their start (17, 7), the plateau on the valley's level
        # with its cliffs walls on flat ground. The enclosure, the rails and the plateau stand a level over the
        # valley, the rails decks (the bridge's shape) over it; the valley's ground the rails close round, the pit in
        # the plateau and the wall's foot lie on the valley's level; the path north climbs from the north's level at
        # the seam to the plateau.
        layout = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_SAFARI_ZONE_SOUTH")
        width = layout["width"]
        records = {record["cellOffset"]: record for record in layout["terrainRecords"] if record["mapGroup"] == 0xFF}

        def at(x, y):
            return records[y * width + x]

        valley = at(22, 7)["groundQ16"]
        self.assertEqual(valley, 0)
        for x, y in ((13, 6), (16, 7), (26, 4), (30, 3), (35, 4), (26, 7)):   # the enclosure and the plateau
            self.assertEqual((at(x, y)["shape"], at(x, y)["groundQ16"]), ("flat", valley + 16), (x, y))
        for x, y in ((17, 7), (18, 7), (19, 7), (20, 7), (20, 6), (20, 5), (21, 5), (22, 5), (22, 4),
                     (22, 3), (23, 3), (24, 3), (25, 3)):                      # the rails, end to end
            self.assertEqual((at(x, y)["shape"], at(x, y)["groundQ16"]), ("bridge", valley + 16), (x, y))
        for x, y in ((18, 3), (21, 4), (19, 6), (23, 5), (21, 7), (28, 7), (32, 7)):
            self.assertEqual((at(x, y)["shape"], at(x, y)["groundQ16"]), ("flat", valley), (x, y))
        for x in range(18, 26):                                                 # the wall
            self.assertEqual((at(x, 2)["groundQ16"], at(x, 2)["topQ16"]), (valley, valley + 16), x)
        self.assertEqual([(at(29, y)["groundQ16"], at(29, y)["topQ16"]) for y in (0, 1, 2)],
                         [(-16, -16), (-16, 0), (0, 16)])

    def test_repository_terraces_their_stairs_climb_to_stand_over_each_other(self):
        # KNOWN_ISSUES MS-247 (the owner's picture of Route 119, 2026-11-13: "the third floor doesn't look 3d"):
        # terraces of ordinary ground whose stairs are the GBA's only sign of their levels (compile_rules
        # CLIMB_LEVELS): Mt. Pyre's exterior climbs a level a staircase from its foot to the summit four levels up,
        # Birth Island's top stands a level over its landing, Mossdeep City's Space Center two flights over the street
        # at its stairs' foot; each staircase rises from the level at its foot to the one at its top.
        def layout(symbol):
            row = next(row for row in self.data["layouts"] if row["symbol"] == symbol)
            records = {record["cellOffset"]: record for record in row["terrainRecords"] if record["mapGroup"] == 0xFF}

            def at(x, y):
                record = records[y * row["width"] + x]
                return record["shape"], record["groundQ16"], record["topQ16"]
            return at

        at = layout("LAYOUT_MT_PYRE_EXTERIOR")
        self.assertEqual([at(20, 44), at(12, 39), at(15, 36), at(15, 27), at(12, 18), at(20, 12)],
                         [("flat", 0, 0), ("flat", 0, 0), ("flat", 16, 16), ("flat", 32, 32), ("flat", 48, 48),
                          ("flat", 64, 64)])
        self.assertEqual([at(20, 16), at(20, 15), at(9, 33)[1], at(9, 29)[2]],
                         [("stairs", 48, 56), ("stairs", 56, 64), 16, 32])
        at = layout("LAYOUT_BIRTH_ISLAND_EXTERIOR")
        self.assertEqual([at(15, 22), at(15, 10), at(15, 6)], [("flat", 0, 0), ("flat", 16, 16), ("flat", 16, 16)])
        self.assertEqual([at(15, y) for y in (19, 18, 17)],
                         [("stairs", 0, 5), ("stairs", 5, 11), ("stairs", 11, 16)])
        at = layout("LAYOUT_MOSSDEEP_CITY")
        street = at(64, 23)[1]
        self.assertEqual([at(64, 20), at(62, 16), at(60, 7)],
                         [("flat", street + 16, street + 16), ("flat", street + 32, street + 32),
                          ("flat", street + 32, street + 32)])
        self.assertEqual([at(64, y)[1:] for y in (22, 21, 19, 18)],
                         [(street, street + 8), (street + 8, street + 16), (street + 16, street + 24),
                          (street + 24, street + 32)])

    def test_repository_navel_rock_sand_stands_over_its_pier(self):
        # KNOWN_ISSUES MS-249: Navel Rock's pier stairs climb through the island's rim to its sand, which stands a level
        # over the pier, the rim's faces over the sand (compile_rules HAND_LEVELS); the five course pieces in the west
        # side of the rock in the middle are scenery faces (SCENERY_COURSES) whose tops lie on the sand's level. The
        # courtyard before the cave lay a level under the pier: the player stepped a level down off the stairs.
        row = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_NAVEL_ROCK_EXTERIOR")
        records = {record["cellOffset"]: record for record in row["terrainRecords"] if record["mapGroup"] == 0xFF}

        def at(x, y):
            record = records[y * row["width"] + x]
            return record["shape"], record["groundQ16"], record["topQ16"]

        self.assertEqual([at(10, 15), at(10, 14)], [("flat", 0, 0), ("flat", 0, 0)])        # the pier
        self.assertEqual([at(x, 13) for x in (9, 10, 11)], [("stairs", 0, 16)] * 3)
        for x, y in ((8, 12), (10, 12), (12, 11), (10, 10), (8, 4), (6, 7), (14, 9)):     # the sand, the cave's door
            self.assertEqual(at(x, y), ("flat", 16, 16), (x, y))
        for x, y in ((7, 7), (7, 8), (7, 9), (8, 9), (8, 10)):                             # the scenery faces
            self.assertEqual(at(x, y), ("cliff", 0, 16), (x, y))
        for x, y in ((5, 7), (10, 3), (15, 8)):                                            # the rim over the sand
            self.assertEqual(at(x, y), ("cliff", 16, 32), (x, y))

    def test_repository_fortree_city_tree_houses_stand_a_level_up(self):
        # Fortree City (the 3D sweep of 2026-11-09, MS-222's kind): its six tree houses, their porches and the plank
        # bridges between them stand a level over the forest floor in the GBA's drawing (the walkways' elevation 4
        # over the town's 3; the path at 30-33 runs under the south bridge). They lay on the floor: the planks on the
        # ground, the ladders flat before the porches, the player on a bridge at the height of the people on the path
        # under it. The houses, their doors, porches and walkways stand a level up, the bridges decks, the ladders
        # climb from the floor to the porches; the forest round them stays on the floor (none of it lifted).
        layout = next(row for row in self.data["layouts"] if row["symbol"] == "LAYOUT_FORTREE_CITY")
        width = layout["width"]
        records = {record["cellOffset"]: record for record in layout["terrainRecords"] if record["mapGroup"] == 0xFF}

        def at(x, y):
            return records[y * width + x]

        floor = at(20, 13)["groundQ16"]
        self.assertEqual(floor, 0)
        for x, y in ((9, 4), (12, 4), (17, 4), (25, 4), (32, 4), (32, 3), (12, 14), (36, 14),   # porches
                     (10, 3), (17, 3), (25, 3), (32, 2), (12, 13), (37, 13)):                    # doors
            self.assertEqual((at(x, y)["shape"], at(x, y)["groundQ16"]), ("flat", floor + 16), (x, y))
        for x, y in ((10, 2), (17, 2), (25, 2), (32, 1), (12, 12), (37, 12)):                    # the houses
            self.assertEqual(at(x, y)["groundQ16"], floor + 16, (x, y))
        for x, y in ((13, 4), (14, 4), (20, 4), (22, 4), (28, 4), (29, 4), (15, 14), (25, 14),
                     (30, 14), (31, 14), (33, 14), (34, 14)):                                    # the bridges
            self.assertEqual((at(x, y)["shape"], at(x, y)["groundQ16"]), ("bridge", floor + 16), (x, y))
        for x in (10, 25, 32):                                                                   # the ladders
            self.assertEqual([(at(x, y)["groundQ16"], at(x, y)["topQ16"]) for y in (6, 5)], [(0, 8), (8, 16)], x)
        for x in (12, 37):
            self.assertEqual([(at(x, y)["groundQ16"], at(x, y)["topQ16"]) for y in (16, 15)], [(0, 8), (8, 16)], x)
        for x, y in ((31, 13), (31, 15), (10, 7), (12, 17), (25, 7), (37, 17),                   # the floor, the path
                     (15, 13), (20, 16), (13, 3), (12, 5), (20, 5), (13, 15), (20, 15), (5, 3)):  # the forest
            self.assertEqual(at(x, y)["groundQ16"], floor, (x, y))

    def test_repository_paths_lie_flush_with_the_ground_they_run_through(self):
        # KNOWN_ISSUES MS-162: the Battle Frontier west half's paths of elevation 3 (the walk north from
        # the harbour, the band along its lower ground, the cells by the Battle Pike) run through its
        # lower ground of elevation 0, a level under its plaza's cliffs, walked onto with nothing between;
        # they stood a level over it, every step on or off them a level up or down. Safari Zone
        # Southeast's cells of elevation 0 in its meadow stood as pillars. They lie on the level of the
        # ground round them (terrace_topology._flush_free_regions); the plaza and Victory Road's mouth
        # in Ever Grande City's mountain (the rock round it) as before.
        layouts = {row["symbol"]: row for row in self.data["layouts"]}

        def ground(name):
            records = {record["cellOffset"]: record["groundQ16"]
                       for record in layouts[name]["terrainRecords"] if record["mapGroup"] == 0xFF}
            return lambda x, y: records[y * layouts[name]["width"] + x]

        west = ground("LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST")
        self.assertEqual({west(x, y) for x, y in ((26, 52), (26, 58), (30, 50), (12, 50), (47, 50),
                                                  (37, 25), (38, 28), (39, 31), (26, 61))}, {-16})
        self.assertEqual({west(x, y) for x, y in ((26, 44), (31, 49), (10, 26))}, {-16})
        self.assertEqual({west(x, y) for x, y in ((20, 20), (20, 23))}, {0})
        safari = ground("LAYOUT_SAFARI_ZONE_SOUTHEAST")
        self.assertEqual({safari(x, y) for x, y in ((16, 32), (22, 33), (31, 33), (19, 36), (21, 37))}, {-16})
        ever = ground("LAYOUT_EVER_GRANDE_CITY")
        self.assertEqual((ever(18, 27), ever(18, 28)), (-16, -16))
        # the rock round Victory Road's mouth: the mountain's one band (rock_bands.py), from the
        # mouth's level to the plateau's and over it, the mouth in its face
        ever_records = {record["cellOffset"]: record
                        for record in layouts["LAYOUT_EVER_GRANDE_CITY"]["terrainRecords"]
                        if record["mapGroup"] == 0xFF}
        width = layouts["LAYOUT_EVER_GRANDE_CITY"]["width"]
        for x, y in ((17, 26), (14, 25), (13, 27)):
            record = ever_records[y * width + x]
            self.assertEqual(record["groundQ16"], -16)
            self.assertGreaterEqual(record["topQ16"], 0)

    def test_repository_cave_rock_stands_and_its_exits_stay_open(self):
        layouts = {row["symbol"]: row for row in self.data["layouts"]}
        granite = layouts["LAYOUT_GRANITE_CAVE_1F"]
        records = {record["cellOffset"]: record for record in granite["terrainRecords"]
                   if record["mapGroup"] == 0xFF}
        # Granite Cave tops its rock with the sand it also uses for floor (529): blocked in a rock
        # mass it stands as rock cell by cell; the same sand the player walks on stays ground.
        sand = [record for record in records.values() if record["expectedMetatile"] == 529]
        self.assertTrue(any(record["source"] == "context:cave-rock" and record["class"] == "cliff"
                            for record in sand))
        self.assertTrue(any(record["class"] == "ground" for record in sand))
        # The way out beyond Granite Cave 1F's south arrow warp (37, 12) is no rock.
        self.assertNotEqual(records[13 * granite["width"] + 37]["class"], "cliff")
        # A ledge the player jumps (east from the sand at (12, 10)) stays a ledge; the blocked one
        # above it, jumped from no cell the player stands on, stands with the rock.
        self.assertEqual(records[10 * granite["width"] + 13]["class"], "ledge")
        self.assertEqual(records[9 * granite["width"] + 14]["source"], "context:cave-rock")
        # Rusturf Tunnel draws its rock in tiers with blocked ledges between them: rock too, so no
        # trench runs through the rock (a straight ledge with no take-off cell, a diagonal one).
        rusturf = layouts["LAYOUT_RUSTURF_TUNNEL"]
        tunnel = {record["cellOffset"]: record for record in rusturf["terrainRecords"]
                  if record["mapGroup"] == 0xFF}
        for x, y in ((6, 14), (5, 14), (8, 14)):
            self.assertEqual((tunnel[y * rusturf["width"] + x]["class"],
                              tunnel[y * rusturf["width"] + x]["source"]),
                             ("cliff", "context:cave-rock"))
        # Rusturf Tunnel's rock tops carry the cave's own behaviour (MB_CAVE): pinned as wall.
        pins = {(pin["tileset"], pin["metatile"]) for pin in self.data["tilesetPins"]}
        self.assertTrue({("gTileset_RusturfTunnel", 9), ("gTileset_RusturfTunnel", 17)} <= pins)
        self.assertNotIn(("gTileset_RusturfTunnel", 81), pins)

    def test_repository_walked_cave_rock_is_floor(self):
        # The Cave of Origin's entrance draws its corridor's middle (8-10, 6-20) with the cave rock's plain fill
        # (metatile 819, pinned as rock): walkable there, it is floor, at the corridor's level, in the layout's
        # records and in its map's where it has its own; the same fill past the corridor's walls, blocked, stands as
        # rock (KNOWN_ISSUES MS-228).
        layouts = {row["symbol"]: row for row in self.data["layouts"]}
        origin = layouts["LAYOUT_CAVE_OF_ORIGIN_ENTRANCE"]
        width = origin["width"]
        records = {record["cellOffset"]: record for record in origin["terrainRecords"]
                   if record["mapGroup"] == 0xFF}
        corridor = {y * width + x for y in range(6, 21) for x in (8, 9, 10)
                    if records[y * width + x]["expectedMetatile"] == 819}
        self.assertEqual(len(corridor), 44)
        for offset in corridor:
            self.assertEqual(records[offset]["class"], "ground", offset)
            self.assertFalse(records[offset]["source"].startswith("pin:"), offset)
        self.assertEqual({records[offset]["groundQ16"] for offset in corridor}, {records[13 * width + 7]["groundQ16"]})
        self.assertEqual((records[13 * width + 5]["expectedMetatile"], records[13 * width + 5]["class"]),
                         (819, "cliff"))
        for record in origin["terrainRecords"]:
            if (record["mapGroup"], record["mapNumber"]) == (24, 37) and record["cellOffset"] in corridor:
                self.assertNotEqual(record["class"], "cliff", record["cellOffset"])
        # The Seafloor Cavern's first room left the fill round its walls walkable where no walk reaches: rock.
        room = layouts["LAYOUT_SEAFLOOR_CAVERN_ROOM1"]
        records = {record["cellOffset"]: record for record in room["terrainRecords"] if record["mapGroup"] == 0xFF}
        for x, y in ((0, 0), (5, 0), (1, 10), (19, 8)):
            self.assertEqual((records[y * room["width"] + x]["class"],
                              records[y * room["width"] + x]["source"][:4]), ("cliff", "pin:"), (x, y))

    def test_sha256_is_canonical_and_covers_every_ir_section(self):
        canonical_keys = ("schemaVersion", "tilesets", "layouts", "pools", "profiles", "default",
                           "behaviorRules", "tilesetPins", "contextualRules", "exactPatterns",
                           "maps", "structureCandidates", "pixelObjects",
                           "evidenceDetails", "ambiguities",
                           "classifierSources")
        canonical = {key: self.data[key] for key in canonical_keys}
        encoded = json.dumps(canonical, sort_keys=True, separators=(",", ":"),
                             ensure_ascii=True).encode("ascii")
        self.assertEqual(self.data["sha256"], hashlib.sha256(encoded).hexdigest())
        self.assertEqual(len(self.data["sha256"]), 64)
        changed = copy.deepcopy(canonical)
        changed["tilesets"][0]["terrainClass"] = "rock"
        changed_hash = hashlib.sha256(json.dumps(changed, sort_keys=True, separators=(",", ":"),
                                                  ensure_ascii=True).encode("ascii")).hexdigest()
        self.assertNotEqual(changed_hash, self.data["sha256"])

    def test_all_g3_archetypes_are_closed(self):
        self.assertEqual(len(ARCHETYPES), len(set(ARCHETYPES)))
        self.assertIn("ground", ARCHETYPES)
        self.assertIn("stairs-down-w", ARCHETYPES)
        self.assertIn("animated-cutout", ARCHETYPES)
        with self.assertRaisesRegex(RuleError, "unknown archetype"):
            _action({"archetype": "sphere", "pool": "terrain"}, "test", {"terrain"}, {})

    def test_action_heights_align_to_voxel_grid(self):
        action = _action({"archetype": "ground", "pool": "terrain",
                          "groundOffset": -0.125, "height": 0.375},
                         "test", {"terrain"}, {})
        self.assertEqual(action["height"], 0.375)
        with self.assertRaisesRegex(RuleError, "1/16-cell"):
            _action({"archetype": "ground", "pool": "terrain", "height": 0.1},
                    "test", {"terrain"}, {})

    def test_action_terrain_class_is_closed(self):
        action = _action({"archetype": "ground", "pool": "terrain",
                          "terrainClass": "pavement"}, "test", {"terrain"}, {})
        self.assertEqual(action["terrainClass"], "pavement")
        with self.assertRaisesRegex(RuleError, "unknown terrain class"):
            _action({"archetype": "ground", "pool": "terrain",
                     "terrainClass": "generic-stuff"}, "test", {"terrain"}, {})

    def test_action_shape_can_override_archetype_geometry(self):
        action = _action({"shape": "cliff", "archetype": "rock", "pool": "prop",
                          "terrainClass": "rock", "height": 1},
                         "test", {"prop"}, {})
        self.assertEqual(action["shape"], "cliff")
        self.assertEqual(action["archetype"], "rock")
        with self.assertRaisesRegex(RuleError, "unknown shape"):
            _action({"shape": "sphere", "archetype": "rock", "pool": "prop"},
                    "test", {"prop"}, {})

    def test_action_face_materials_are_closed(self):
        action = _action({"archetype": "cliff", "pool": "terrain",
                          "faces": {"top": {"metatile": "self", "layer": "base",
                                             "rotation": 90, "flipX": True},
                                    "south": {"metatile": 23, "layer": "full"},
                                    "north": "none"}},
                         "test", {"terrain"}, {})
        self.assertEqual(action["faces"]["south"]["metatile"], 23)
        self.assertEqual(action["faces"]["north"], "none")
        self.assertEqual(action["faces"]["top"]["rotation"], 90)
        self.assertTrue(action["faces"]["top"]["flipX"])
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _action({"archetype": "ground", "pool": "terrain",
                     "faces": {"bottom": "none"}}, "test", {"terrain"}, {})

    def test_rejects_v1_and_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rules.json"
            path.write_text('{"version":1}', encoding="utf-8")
            with self.assertRaisesRegex(RuleError, "schemaVersion must be 2"):
                load_json(path)
            path.write_text('{"schemaVersion":2,"kind":"global","kind":"map"}', encoding="utf-8")
            with self.assertRaisesRegex(RuleError, "duplicate key"):
                load_json(path)

    def test_profile_masks_samples_ranges_and_references(self):
        tilesets = {"gTileset_Test": SimpleNamespace(metatile_count=2)}
        values = [{"id": "tree-profile", "archetype": "tree",
                   "masks": {"occupancy": {"width": 3, "height": 1, "rows": ["7"]}},
                   "samples": [{"tileset": "gTileset_Test", "metatile": 1, "layer": "full"}]}]
        profiles, by_id = _profiles(values, "profiles", tilesets)
        self.assertEqual(profiles[0]["masks"]["occupancy"]["rows"], ["7"])
        self.assertIn("tree-profile", by_id)
        bad = copy.deepcopy(values)
        bad[0]["samples"][0]["metatile"] = 2
        with self.assertRaisesRegex(RuleError, "0 through 1"):
            _profiles(bad, "profiles", tilesets)
        with self.assertRaisesRegex(RuleError, "outside mask width"):
            _mask({"width": 3, "height": 1, "rows": ["f"]}, "mask")

    def test_context_selector_accepts_every_context_dimension(self):
        tilesets = {"gTileset_Test": object()}
        behaviors = {"MB_NORMAL": 0}
        selector = _selector({
            "tilesets": ["gTileset_Test"], "metatiles": [4], "behaviors": ["MB_NORMAL"],
            "layerTypes": ["split"], "elevations": [3], "mapTypes": ["MAP_TYPE_ROUTE"],
            "neighbors": {"n": {"behavior": "MB_NORMAL", "layerType": "normal",
                                  "elevation": 2, "metatile": 7}},
            "event": {"kind": "object", "class": "berry-tree"},
        }, "selector", tilesets, behaviors)
        self.assertEqual(set(selector["neighbors"]), {"n"})
        self.assertEqual(selector["event"]["class"], "berry-tree")
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _selector({"collision": [1]}, "selector", tilesets, behaviors)

    def test_exact_pattern_dimensions_claim_and_placement_scan(self):
        tilesets = {"P": SimpleNamespace(role="primary", metatile_count=5),
                    "S": SimpleNamespace(role="secondary", metatile_count=4)}
        raw = [{"id": "door-pattern", "dimensions": {"width": 2, "height": 2},
                "tilesets": {"primary": "P", "secondary": "S"},
                "cells": [[1, 2], [3, 4]], "priority": 10, "claimMask": ["11", "01"],
                "action": {"archetype": "building", "pool": "structure"}}]
        pattern = _patterns(raw, "patterns", tilesets, {"structure"}, {})[0]
        layout = {"id": "L", "primary_tileset": "P", "secondary_tileset": "S",
                  "width": 3, "height": 2}
        cells = {"L": tuple({"metatile": value} for value in (1, 2, 9, 3, 4, 9))}
        self.assertEqual(_pattern_placements(pattern, [layout], cells), 1)
        bad = copy.deepcopy(raw)
        bad[0]["claimMask"] = ["00", "00"]
        with self.assertRaisesRegex(RuleError, "claim at least one"):
            _patterns(bad, "patterns", tilesets, {"structure"}, {})

    def test_exact_pattern_rejects_cells_outside_declared_tilesets(self):
        tilesets = {"P": SimpleNamespace(role="primary", metatile_count=2),
                    "S": SimpleNamespace(role="secondary", metatile_count=1)}
        base = {"id": "range-pattern", "dimensions": {"width": 1, "height": 1},
                "tilesets": {"primary": "P", "secondary": "S"}, "priority": 1,
                "claimMask": ["1"],
                "action": {"archetype": "building", "pool": "structure"}}
        for metatile in (2, 0x201):
            raw = [{**base, "cells": [[metatile]]}]
            with self.assertRaisesRegex(RuleError, "outside the declared"):
                _patterns(raw, "patterns", tilesets, {"structure"}, {})

    def test_equal_priority_concrete_pattern_claim_overlap_is_rejected(self):
        action = {"archetype": "building", "pool": "structure"}
        patterns = [
            {"id": name, "dimensions": {"width": 1, "height": 1},
             "tilesets": {"primary": "P", "secondary": "S"}, "cells": [[1]],
             "priority": priority, "claimMask": ["1"], "action": action}
            for name, priority in (("first", 4), ("second", 4))
        ]
        layout = {"id": "L", "primary_tileset": "P", "secondary_tileset": "S",
                  "width": 1, "height": 1}
        maps = [{"symbol": "MAP_TEST", "layout": "L"}]
        cells = {"L": ({"metatile": 1},)}
        with self.assertRaisesRegex(RuleError, "equal-priority pattern claim overlap"):
            _validate_pattern_claim_overlaps(patterns, maps, {"L": layout}, cells, {})
        patterns[1]["priority"] = 3
        _validate_pattern_claim_overlaps(patterns, maps, {"L": layout}, cells, {})

    def test_duplicate_logical_ids_are_global_across_sections_and_scopes(self):
        with self.assertRaisesRegex(RuleError, "duplicate logical ID 'shared'"):
            _validate_logical_ids([("shared", "profiles"),
                                   ("shared", "MAP_TEST.contextualRules")])

    def test_ground_policy_is_closed(self):
        self.assertEqual(_ground_policy({"mode": "automatic"}, "ground"), {"mode": "automatic"})
        self.assertEqual(_ground_policy({"mode": "manual", "metatile": 17}, "ground")["metatile"], 17)
        with self.assertRaisesRegex(RuleError, "unknown fields"):
            _ground_policy({"mode": "automatic", "metatile": 1}, "ground")

    def test_pixel_object_abi_limits_are_rejected_at_compile_time(self):
        with self.assertRaisesRegex(RuleError, "dimensions exceed"):
            _validate_pixel_object_catalog([{
                "id": 1, "layoutId": 1, "mapGroup": 255, "mapNumber": 255,
                "width": 256, "height": 1, "supportOffsetQ16": 0,
                "maskRows": [1], "pixels": [],
            }])

    def test_camera_ranges(self):
        profile, pitch, focal = parse_camera(
            {"profile": "interior", "pitch": 55, "focalLength": 150}, "test")
        self.assertEqual(profile, "interior")
        self.assertAlmostEqual(pitch, 0.959931, places=5)
        self.assertEqual(focal, 150)
        with self.assertRaises(RuleError):
            parse_camera({"profile": "interior", "pitch": 90}, "test")

    def test_explicit_migration_never_revives_retired_prototypes(self):
        migrated = migrate_document({"version": 1, "map": "MAP_X", "layout": "LAYOUT_X",
                                     "supported": True, "overrides": [], "buildings": []})
        self.assertEqual(migrated["schemaVersion"], 2)
        self.assertEqual(migrated["contextualRules"], [])
        with self.assertRaisesRegex(MigrationError, "retired"):
            migrate_document({"version": 1, "map": "MAP_X", "layout": "LAYOUT_X",
                              "supported": True, "overrides": [{"x": 1}], "buildings": []})
        with self.assertRaisesRegex(MigrationError, "retired"):
            migrate_document({"version": 1, "templates": {"house": {}}})

    def test_c_contract_contains_full_catalog_base_tables(self):
        header, source = render_header(), render_c(self.data)
        self.assertIn("struct DioramaGeneratedTilesetV2", header)
        self.assertIn("struct DioramaGeneratedLayoutV2", header)
        self.assertIn("struct DioramaGeneratedTerrainV2", header)
        self.assertIn("struct DioramaGeneratedStructureV2", header)
        self.assertIn("struct DioramaGeneratedPixelObjectV2", header)
        self.assertIn("struct DioramaGeneratedPixelV2", header)
        self.assertIn("role, terrainClass", header)
        self.assertIn("struct DioramaGeneratedMapV2", header)
        self.assertTrue(all(f'"{row["symbol"]}"' in source for row in self.data["tilesets"]))
        self.assertIn("gDioramaTerrainV2Count", source)
        self.assertIn("gDioramaAmbiguitiesV2", source)
        self.assertIn("gDioramaStructuresV2", source)
        self.assertIn("gDioramaPixelObjectsV2", source)
        self.assertIn("gDioramaPixelMaskRowsV2", source)
        self.assertGreater(len(self.data["pixelObjects"]), 0)
        self.assertTrue(all(pixel["sourceColor"] != 0
                            for obj in self.data["pixelObjects"] for pixel in obj["pixels"]))
        supported = [obj for obj in self.data["pixelObjects"] if obj["supportStructureId"]]
        self.assertEqual(len(supported), 2)
        self.assertTrue(all(obj["class"] == "console" and obj["supportOffsetQ16"] == 8
                            and obj["groundMode"] == "replacement"
                            and obj["groundMetatile"] == 513 for obj in supported))
        structures = {row["id"]: row for row in self.data["structureCandidates"]}
        mask_goldens = Counter()
        for obj in self.data["pixelObjects"]:
            mask_hash = hashlib.sha256(b"".join(
                struct.pack("<Q", row) for row in obj["maskRows"])).hexdigest()
            mask_goldens[(structures[obj["structureId"]]["owner"], obj["groundMode"],
                          obj["groundMetatile"], bool(obj["supportStructureId"]),
                          mask_hash)] += 1
        self.assertEqual(mask_goldens, Counter({
            ("petalburg-isolated-signpost", "replacement", 1, False,
             "d48f1e20b24a2549f329db82dfe0399a852945cc3585be861290e5d5e4bfbc3f"): 2,
            ("event:background-sign", "replacement", 1, False,
             "d48f1e20b24a2549f329db82dfe0399a852945cc3585be861290e5d5e4bfbc3f"): 3,
            ("brendan-tv-cutout", "replacement", 513, True,
             "f19997c0626bbf8e6ae5b3bbc525ffb28e6ded5f3fcea5d4da6b04fec43c9996"): 1,
            ("may-tv-cutout", "replacement", 513, True,
             "f19997c0626bbf8e6ae5b3bbc525ffb28e6ded5f3fcea5d4da6b04fec43c9996"): 1,
            ("brendan-tv-support", "source-base", 0xFFFF, False,
             "077af72744ba6ac7056fd27e228770687db42b56fa17bbeb0a1bc399d3925a1b"): 1,
            ("may-tv-support", "source-base", 0xFFFF, False,
             "077af72744ba6ac7056fd27e228770687db42b56fa17bbeb0a1bc399d3925a1b"): 1,
            ("brendan-tv-base", "replacement", 558, False,
             "5ada150e0358d4385fd8b34c576ca78aca64d94aa5dc105cd4c89aff290bb248"): 1,
            ("may-tv-base", "replacement", 689, False,
             "5ada150e0358d4385fd8b34c576ca78aca64d94aa5dc105cd4c89aff290bb248"): 1,
            ("generic-building-potted-plant", "source-base", 0xFFFF, False,
             "d444d1b17085ac9d0f7adfb5f55e82a229f32e59e3147ab1b098c1739a64e0e9"): 5,
            ("generic-building-left-stool", "source-base", 0xFFFF, False,
             "e5760e791385a5e2cdac53907b27ace89dcdcb58207dc558702a30943fa76962"): 6,
            ("generic-building-right-stool", "source-base", 0xFFFF, False,
             "56b7e3207c6d8a42fcbeeea10bcbf7960018769b89881ced169fac99493f473e"): 10,
            ("generic-building-table-relief", "source-base", 0xFFFF, False,
             "0a219127e3a3ae24590df9a9737fa4e3cc702caa48d947ec7d4d726e1634f88e"): 1,
        }))
        self.assertIn(self.data["sha256"], source)

    def test_c_contract_excludes_compiler_only_precedence_tables(self):
        packed = copy.deepcopy(self.data)
        packed["profiles"] = [{"id": "flat-cell", "archetype": "ground",
                               "masks": {"claim": {"width": 16, "height": 1,
                                                     "rows": ["ffff"]}},
                               "samples": []}]
        action = {"archetype": "ground", "pool": "terrain", "profile": "flat-cell",
                   "axis": "cross", "groundOffset": 0.25, "height": 1.0,
                  "faces": {"top": {"metatile": "self", "layer": "base"},
                            "south": {"metatile": 7, "layer": "full"},
                            "north": "none"},
                   "groundPolicy": {"mode": "manual", "metatile": 1}}
        selector = {"tilesets": [packed["tilesets"][0]["symbol"]], "metatiles": [1],
                    "behaviors": ["MB_NORMAL"], "behaviorIds": [0],
                    "layerTypes": ["normal"], "elevations": [2],
                    "mapTypes": ["MAP_TYPE_TOWN"],
                    "neighbors": {"nw": {"metatile": 1, "behavior": "MB_NORMAL",
                                             "behaviorId": 0, "layerType": "normal",
                                             "elevation": 2}},
                    "event": {"kind": "object", "class": "berry-tree"}}
        rule = {"id": "packed-rule", "selector": selector, "action": action,
                "priority": 7, "placementCount": 1, "allowedUnused": None}
        primary = next(row for row in packed["tilesets"] if row["role"] == "primary")
        secondary = next(row for row in packed["tilesets"] if row["role"] == "secondary")
        pattern = {"id": "packed-pattern", "dimensions": {"width": 1, "height": 1},
                   "tilesets": {"primary": primary["symbol"], "secondary": secondary["symbol"]},
                   "cells": [[0]], "priority": 8, "claimMask": ["1"], "action": action,
                   "placementCount": 0, "allowedNoPlacements": {"reason": "fixture"}}
        packed["contextualRules"] = [rule]
        packed["exactPatterns"] = [pattern]
        packed["maps"][0]["contextualRules"] = [{**rule, "id": "local-rule"}]
        packed["maps"][0]["exactPatterns"] = [{**pattern, "id": "local-pattern"}]
        header, source = render_header(), render_c(packed)
        for name in ("Pools", "Profiles", "Masks", "MaskRows", "Samples"):
            self.assertIn(f"gDiorama{name}V2", header)
            self.assertIn(f"gDiorama{name}V2", source)
        for name in ("TilesetPins", "Selectors", "Neighbors", "ContextualRules",
                     "PatternCells", "PatternClaims", "ExactPatterns"):
            self.assertNotIn(f"gDiorama{name}V2", header)
            self.assertNotIn(f"gDiorama{name}V2", source)
        for content in ("packed-rule", "local-rule", "packed-pattern", "local-pattern",
                        "berry-tree"):
            self.assertNotIn(content, source)
        self.assertNotIn("EventPreset", header)
        self.assertNotIn("EventPreset", source)
        self.assertIn("UINT64_C(0xFFFF)", source)


class ScriptLayoutTests(unittest.TestCase):
    """The layouts a map's own scripts switch it to are that map's (no
    compile needed)."""

    def test_switched_layouts_belong_to_their_map(self):
        from compile_rules import _script_layouts
        switched = _script_layouts(ROOT)
        self.assertEqual(switched["LAYOUT_SHOAL_CAVE_HIGH_TIDE_INNER_ROOM"],
                         "LAYOUT_SHOAL_CAVE_LOW_TIDE_INNER_ROOM")
        self.assertEqual(switched["LAYOUT_ROUTE111_NO_MIRAGE_TOWER"], "LAYOUT_ROUTE111")
        # switching back to its own layout is not another one
        self.assertNotIn("LAYOUT_SHOAL_CAVE_LOW_TIDE_INNER_ROOM", switched)


class WalkedRockPinTests(unittest.TestCase):
    """A pin that stands a cave's rock up does not stand up the cells of it the player walks on
    (compile_rules._walked_rock_pins, KNOWN_ISSUES MS-228; no compile needed)."""

    ROCK = shape_from_action({"archetype": "cliff", "pool": "terrain", "height": 1},
                             source="pin:gTileset_Cave:307", authored=True, confidence=1.0)
    PAINTED = shape_from_action({"archetype": "cliff", "pool": "terrain", "height": 1},
                                source="heuristic:mountain-art-family", authored=False, confidence=0.9)
    FLOOR = shape_from_action({"archetype": "ground", "pool": "terrain"}, source="behavior:MB_CAVE",
                              authored=False, confidence=0.95)

    def walked(self, *rows, elevations=None):
        """rows: '#' a blocked cell, '.' floor, 'P' a pinned rock cell the game leaves walkable, 'H' one of
        rock the pins do not stand up, 'v' a ledge jumped south; `elevations` {(x, y): elevation}, else 3."""
        width, cells, shapes = len(rows[0]), [], []
        for y, row in enumerate(rows):
            for x, kind in enumerate(row):
                cells.append({"collision": int(kind in "#v"), "elevation": (elevations or {}).get((x, y), 3),
                              "behavior": "MB_JUMP_SOUTH" if kind == "v" else "MB_CAVE"})
                shapes.append({"P": self.ROCK, "H": self.PAINTED}.get(kind, self.FLOOR))
        offsets = _walked_rock_pins(tuple(cells), width, len(rows), tuple(shapes))
        return sorted((offset % width, offset // width) for offset in offsets)

    def test_rock_beside_the_floor_is_walked(self):
        # the Cave of Origin's corridor: its middle the rock's plain fill, its edges floor
        self.assertEqual(self.walked("#.PP.#",
                                     "#.PP.#"), [(2, 0), (2, 1), (3, 0), (3, 1)])

    def test_rock_closed_in_stays_rock(self):
        # blocked cells all round, or the map's edge: the fill past a cave's walls
        self.assertEqual(self.walked("###",
                                     "#P#",
                                     "###"), [])
        self.assertEqual(self.walked("PP#.",
                                     "P##."), [])

    def test_floor_on_another_level_reaches_no_rock(self):
        # a step the game does not let the player take: elevation 4 beside 3
        self.assertEqual(self.walked("#.P#", elevations={(2, 0): 4}), [])
        # elevation 0 (and 15) are any level's
        self.assertEqual(self.walked("#.P#", elevations={(1, 0): 0, (2, 0): 4}), [(2, 0)])
        self.assertEqual(self.walked("#.P#", elevations={(2, 0): 15}), [(2, 0)])

    def test_a_ledge_landing_in_the_rock_walks_it(self):
        self.assertEqual(self.walked("#.#",
                                     "#v#",
                                     "#P#"), [(1, 2)])

    def test_rock_the_pins_do_not_stand_up_is_left_alone(self):
        # a mountain's enclosed tops are the mountain art's own call (_with_mountain_art)
        self.assertEqual(self.walked("#.H#"), [])


if __name__ == "__main__":
    unittest.main()
