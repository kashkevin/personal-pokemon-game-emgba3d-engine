#!/usr/bin/env python3
"""The house kit's furniture as the rooms lend it (gen_voxel_buildings.reuse_pieces), from the 3D
object audit of 2026-11-14 and its survey of 2026-11-15.

The low tea table on the tatami stood only where Mr. Briney's own piece was lent - cut three columns
short of the drawing's right edge, which lay on the floor beside it as a strip, and 10 rows tall, its
front the top's colour over the apron (the Cove Lily Motel's "tall box with a thin strip on one end");
the same table lay flat in the rooms built before his, and the table without a cup everywhere. Now
house 3 stands the one with the cup and house 4 the one without, each the drawing's whole extent with
its apron and legs (6 rows) for its front, and every later room with their tiles stands them.

The second house's cupboard and the house with a bed's chest, lent to the Fan Clubs and many houses,
had a tile of their drawers' fronts run across their sides; their sides are the frame's own column."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voxel_building as vb  # noqa: E402
import voxel_building_specs as specs  # noqa: E402

OUTLINE = "414a6a"
CUP = (0x24E, 0x24F, 0x256, 0x257)
NO_CUP = (0x24E, 0x25F, 0x256, 0x257)


def tiles(layout, cx, cy):
    return (layout.metatile(cx, cy), layout.metatile(cx + 1, cy),
            layout.metatile(cx, cy + 1), layout.metatile(cx + 1, cy + 1))


def drawing(layout, x0, y0, x1, y1):
    """{(x, y): 'rrggbb'} of the layout's drawing in the rectangle."""
    out = {}
    for cy in range(y0 // 16, (y1 + 15) // 16):
        for cx in range(x0 // 16, (x1 + 15) // 16):
            px = layout.cell_image(layout.metatile(cx, cy)).convert("RGB").load()
            for j in range(16):
                for i in range(16):
                    x, y = cx * 16 + i, cy * 16 + j
                    if x0 <= x < x1 and y0 <= y < y1:
                        out[(x, y)] = "%02x%02x%02x" % px[i, j]
    return out


def own_pieces(layout_id):
    spec = next(s for s in specs.SPECS if s.get("interior", {}).get("layout") == layout_id)
    return {pc["name"]: pc for pc in spec["interior"]["pieces"]}


class TeaTableTests(unittest.TestCase):

    def test_each_table_is_its_whole_drawing(self):
        for layout_id, (cx, cy), want in (("LAYOUT_HOUSE3", (4, 3), CUP), ("LAYOUT_HOUSE4", (7, 4), NO_CUP)):
            with self.subTest(room=layout_id):
                L = vb.LayoutArt(layout_id)
                self.assertEqual(tiles(L, cx, cy), want)
                table = own_pieces(layout_id)["tea_table"]
                [(x0, y0, x1, y1)] = table["shape"]
                self.assertEqual(table["height"], 6)
                px = drawing(L, x0 - 1, y0, x1 + 1, y1)
                # down the top's rows its first and last columns are the outline; past them the
                # tatami (its lines are the outline's colour), none of the table's own colours
                table_colours = {"d5cd62", "bdac52", "947329"}
                for y in range(y0 + 1, y0 + 21):
                    self.assertEqual((px[(x0, y)], px[(x1 - 1, y)]), (OUTLINE, OUTLINE), y)
                    for x in (x0 - 1, x1):
                        self.assertIn(px[(x, y)], specs.TEA_TABLE_FLOOR + (OUTLINE,), (x, y))
                self.assertFalse(table_colours & {px[(x, y)] for x in (x0 - 1, x1) for y in range(y0, y1)})
                self.assertIn("d5cd62", {px[(x0 + 1, y)] for y in range(y0, y1)})
                self.assertIn("d5cd62", {px[(x1 - 2, y)] for y in range(y0, y1)})
                # its top row the outline across, its legs on its last row
                self.assertEqual({px[(x, y0)] for x in range(x0, x1)}, {OUTLINE})
                self.assertEqual((px[(x0 + 3, y1 - 1)], px[(x1 - 4, y1 - 1)]), (OUTLINE, OUTLINE))

    def test_the_rooms_after_them_have_the_same_tiles(self):
        # the rooms that stand them as house 3's and house 4's (reuse_pieces: the same tiles first)
        for layout_id, spots in (("LAYOUT_ROUTE104_MR_BRINEYS_HOUSE", {(7, 4): CUP}),
                                 ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_1F", {(2, 4): CUP, (8, 4): NO_CUP}),
                                 ("LAYOUT_LILYCOVE_CITY_COVE_LILY_MOTEL_2F",
                                  {(8, 3): CUP, (2, 6): CUP, (2, 3): NO_CUP, (8, 6): NO_CUP})):
            L = vb.LayoutArt(layout_id)
            for (cx, cy), want in spots.items():
                self.assertEqual(tiles(L, cx, cy), want, (layout_id, cx, cy))
        # Mr. Briney's own table is gone: house 3's stands in his house
        self.assertNotIn("table", own_pieces("LAYOUT_ROUTE104_MR_BRINEYS_HOUSE"))


class DresserSideTests(unittest.TestCase):

    def test_the_sides_are_the_frames_column(self):
        for layout_id, name, colours in (("LAYOUT_HOUSE2", "cupboard", {"bdac52", "ac8b39", OUTLINE}),
                                         ("LAYOUT_HOUSE_WITH_BED", "chest", {"83ac4a", "6a8b31", OUTLINE})):
            with self.subTest(room=layout_id):
                pc = own_pieces(layout_id)[name]
                x0, y0, x1, y1 = pc["side"]
                [(sx0, sy0, sx1, sy1)] = pc["shape"]
                # one column of its own front, from its foot up its height
                self.assertEqual((x1 - x0, y1, y1 - y0), (1, sy1, pc["height"]))
                self.assertTrue(sx0 < x0 < sx1 - 1)
                px = drawing(vb.LayoutArt(layout_id), x0, y0, x1, y1)
                self.assertLessEqual(set(px.values()), colours)



class EverGrandeHallTests(unittest.TestCase):
    """Ever Grande City's halls between the Elite Four's rooms had no model: their back wall with the doorway
    lay on the landing and their railings lay on the stairs' slope (the object audit, class 1). The terrain
    raises the landing in place and a model stands on the ground under its door, so the wall stands on the
    landing and each railing's row, a piece of its own, on its step."""

    HALLS = (("LAYOUT_EVER_GRANDE_CITY_SHORT_HALL", 13), ("LAYOUT_EVER_GRANDE_CITY_HALL4", 34))

    def test_the_doorway_is_cut_out_of_the_wall(self):
        for layout_id, rows in self.HALLS:
            pieces = own_pieces(layout_id)
            wall = pieces["wall"]
            # the opening x 77-99 under the lintel (rows 0-25); the warp's cell is the doorway's
            self.assertIn((77, 0, 99, 25), wall["shape"], layout_id)
            self.assertFalse([r for r in wall["shape"] if r[0] < 99 and 77 < r[2] and r[3] > 25], layout_id)
            self.assertEqual(list(wall["free"]), [(5, 2)], layout_id)
            self.assertEqual(pieces["stairwell_door"]["inside"], (77, 25, 99, 48), layout_id)
            px = drawing(vb.LayoutArt(layout_id), 77, 25, 99, 48)
            # the opening is the arch's dark, its jambs' light edges and its outline
            self.assertLessEqual(set(px.values()), {"414a6a", "316a7b", "94bdb4", "417ba4"}, layout_id)

    def test_a_railing_piece_a_row_on_each_side(self):
        for layout_id, rows in self.HALLS:
            pieces = own_pieces(layout_id)
            L = vb.LayoutArt(layout_id)
            for y in range(5, rows):
                for side, (x0, x1) in (("w", (51, 64)), ("e", (112, 125))):
                    pc = pieces["rail_%s_%d" % (side, y)]
                    self.assertEqual(pc["shape"], [(x0, y * 16, x0 + (x1 - x0), y * 16 + 16)])
                    # its edges the rail's outline, the stone or the landing past them
                    px = drawing(L, x0 - 1, y * 16 + 8, x1 + 1, y * 16 + 9)
                    self.assertEqual((px[(x0, y * 16 + 8)], px[(x1 - 1, y * 16 + 8)]), (OUTLINE, OUTLINE),
                                     (layout_id, side, y))
                    self.assertIn(px[(x0 - 1, y * 16 + 8)] if side == "w" else px[(x1, y * 16 + 8)],
                                  pc["leave"], (layout_id, side, y))
            self.assertFalse([n for n in pieces if n.startswith("rail_") and int(n.split("_")[2]) >= rows])



class ArenaLobbyTests(unittest.TestCase):
    """The Battle Arena's lobby had no model: its shoji back wall lay on the dojo's raised stage (the object
    audit, class 1). The wall stands its drawn rows on its foot, and the game stands it on the stage."""

    def test_the_back_wall_is_its_drawn_rows_across_the_room(self):
        L = vb.LayoutArt("LAYOUT_BATTLE_FRONTIER_BATTLE_ARENA_LOBBY")
        wall = own_pieces("LAYOUT_BATTLE_FRONTIER_BATTLE_ARENA_LOBBY")["wall"]
        self.assertEqual(wall["shape"], [(0, 0, L.w * 16, 48)])
        self.assertEqual((wall["height"], wall["foot"]), (48, 48))
        # its foot row the wall's, the row under it the stage's (the shoji's frame, then the boards)
        px = drawing(L, 0, 46, 32, 49)
        self.assertIn("ac5231", {px[(x, 47)] for x in range(32)})
        self.assertEqual({px[(x, 48)] for x in range(32)}, {"eee6a4"})



class TrickHousePuzzleWallTests(unittest.TestCase):
    """Trick House puzzles 5 and 8 stood worse read off their cells than flat (the object audit's survey of
    2026-11-15); their back wall, which lay on the floor, stands by hand as the other puzzles' read wall."""

    def test_the_wall_is_the_other_puzzles(self):
        import battle_room_auto as bra
        ground, pieces, notes = bra.analyse("LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE4")
        read = {pc["name"]: pc for pc in pieces}["wall_0_0"]
        for n in (5, 8):
            layout_id = "LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE%d" % n
            wall = own_pieces(layout_id)["wall"]
            self.assertEqual((wall["shape"], wall["height"], wall["foot"], wall["side"]),
                             (read["shape"], read["height"], read["foot"], read["side"]), layout_id)
            # the same back wall drawn: its two rows as puzzle 4's
            mine, theirs = vb.LayoutArt(layout_id), vb.LayoutArt("LAYOUT_ROUTE110_TRICK_HOUSE_PUZZLE4")
            self.assertEqual([mine.metatile(x, y) for y in range(2) for x in range(15)],
                             [theirs.metatile(x, y) for y in range(2) for x in range(15)], layout_id)
            self.assertEqual(list(own_pieces(layout_id)), ["wall"], layout_id)


class PyramidLobbyTests(unittest.TestCase):
    """The Battle Pyramid's lobby (the object audit's class 1): the back wall, the pillars, the statues on their
    pedestals, the braziers and the little pyramids on its upper floor lay drawn on that floor. Cut by hand: the
    statues, braziers and pyramids cards of their own pixels, the wall running on behind the statues."""

    LAYOUT = "LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_LOBBY"

    def test_each_card_is_its_own_pixels(self):
        import furnished_room_specs as frs
        L = vb.LayoutArt(self.LAYOUT)
        pieces = own_pieces(self.LAYOUT)
        cards = [pc for pc in pieces.values() if pc["card"]]
        self.assertEqual(sorted(pc["name"] for pc in cards),
                         sorted(["statue_3", "statue_5", "statue_9", "statue_11", "brazier_4", "brazier_10",
                                 "pyramid_6", "pyramid_8"]))
        for pc in cards:
            with self.subTest(piece=pc["name"]):
                self.assertTrue(pc["shape"])
                for (x0, y0, x1, y1) in pc["shape"]:
                    self.assertEqual(y1 - y0, 1)
                    cy = y0 // 16
                    a = L.cell_image(L.metatile(x0 // 16, cy)).convert("RGB").load()
                    b = L.cell_image(frs.PYRAMID_PLAIN[cy]).convert("RGB").load()
                    for x in range(x0, x1):
                        self.assertNotEqual(a[x % 16, y0 % 16], b[x % 16, y0 % 16])
        # a statue from the ears over the wall's second row to its pedestal's foot on the floor's row
        statue = pieces["statue_3"]
        self.assertEqual(min(y0 for (_, y0, _, _) in statue["shape"]), 24)
        self.assertEqual(max(y1 for (_, _, _, y1) in statue["shape"]), 80)

    def test_the_wall_stands_and_runs_on_behind_the_statues(self):
        pieces = own_pieces(self.LAYOUT)
        wall = pieces["wall"]
        self.assertEqual((wall["shape"], wall["height"], wall["foot"]), ([(48, 0, 192, 64)], 64, 64))
        self.assertEqual(wall["fill_down"], [(cx * 16, 16, cx * 16 + 16, 64) for cx in (3, 5, 9, 11)])
        for name, x0 in (("block_pillar_w", 32), ("block_pillar_e", 192)):
            pillar = pieces[name]
            self.assertEqual((pillar["shape"], pillar["height"]), ([(x0, 0, x0 + 16, 96)], 64))
        # nothing of the lobby's own floor below (the stairs down are rows 8-12) is the model's
        for pc in pieces.values():
            for (x0, y0, x1, y1) in pc["shape"]:
                self.assertLessEqual(y1, 96, pc["name"])


class DevonLobbyStripTests(unittest.TestCase):
    """Devon's lobby (the object audit, class 3): its alcoves' side walls, drawn as their tops (orange strips 4
    pixels wide), stand from the alcove's back wall to the lobby's; the room's carpet laid under their cells
    showed at the edge of their decal as a dotted pink line down the black outside between the alcoves. Their
    cells keep their own drawing under them - the black of the outside."""

    def test_the_strips_stand_on_their_cells_own_drawing(self):
        pieces = own_pieces("LAYOUT_RUSTBORO_CITY_DEVON_CORP_1F")
        strips = [pc for name, pc in pieces.items() if name.startswith("edge_")]
        self.assertEqual(sorted(pc["shape"][0][0] for pc in strips), [44, 144, 204, 256])
        for pc in strips:
            self.assertTrue(pc["own_floor"], pc["name"])
        # and only they: the counter and the jars stand on the carpet
        for name in ("counter", "jar_w", "jar_e"):
            self.assertFalse(pieces[name]["own_floor"], name)


class TrainerHillFloorTests(unittest.TestCase):
    """Trainer Hill's floors 1F-4F (the object audit's class 1): their first five rows - the alcoves' back wall
    with the stairs' doorways, the block between them and the panel with the floor's number - are the layout's
    own on every challenge and had no model; cut by hand. The rows under them are the challenge's maze."""

    FLOORS = ["LAYOUT_TRAINER_HILL_%dF" % n for n in (1, 2, 3, 4)]

    def test_the_five_rows_are_every_floors_but_for_the_number(self):
        arts = [vb.LayoutArt(lid) for lid in self.FLOORS]
        for cy in range(5):
            for cx in range(16):
                ids = {L.metatile(cx, cy) for L in arts}
                if (cx, cy) == (7, 4):
                    self.assertEqual(len(ids), 4)
                else:
                    self.assertEqual(len(ids), 1, (cx, cy))

    def test_the_walls_doorways_block_and_panel(self):
        for lid in self.FLOORS:
            with self.subTest(floor=lid):
                pieces = own_pieces(lid)
                self.assertEqual(sorted(pieces), ["block_mid", "block_panel", "stairwell_e", "stairwell_w",
                                                  "wall_e", "wall_w"])
                for side, x0, door in (("w", 31, (2, 1)), ("e", 191, (12, 1))):
                    wall = pieces["wall_" + side]
                    self.assertEqual((wall["height"], wall["foot"], wall["free"]), (32, 32, [door]))
                    # the doorway's dark, rows 16-32, is the recess's, not the wall's
                    for (a, b, c, d) in wall["shape"]:
                        self.assertFalse(a < x0 + 18 and c > x0 and d > 16, (side, (a, b, c, d)))
                    # its recess back to where the arch's corners end
                    self.assertEqual(pieces["stairwell_" + side]["inside"], (x0, 16, x0 + 18, 32))
                mid = pieces["block_mid"]
                self.assertEqual((mid["shape"], mid["height"], mid["foot"]), ([(64, 0, 176, 80)], 32, 80))
                self.assertTrue(mid["solid"])
                self.assertEqual(mid["side"], (0, 0, 16, 32))
                panel = pieces["block_panel"]
                self.assertEqual((panel["shape"], panel["height"], panel["foot"]), ([(96, 48, 144, 96)], 32, 96))
                self.assertEqual(panel["free"], [(6, 5), (7, 5), (8, 5)])
                # nothing of the maze, rows 5 on, but the panel's foot
                for pc in pieces.values():
                    for (a, b, c, d) in pc["shape"]:
                        self.assertLessEqual(d, 96 if pc is panel else 80, pc["name"])

    def test_the_maze_draws_the_panels_foot_its_own_way(self):
        """The challenges' mazes draw the panel's foot row with a block's white top over its last 3 pixel
        rows where a block stands before it: the panel does not answer for that row."""
        import glob
        import os
        L = vb.LayoutArt(self.FLOORS[0])
        own = [L.metatile(cx, 5) for cx in (6, 7, 8)]
        self.assertEqual(own, [0x213, 0x214, 0x215])
        maps = sorted(glob.glob(os.path.join(vb.ROOT, "graphics", "trainer_hill", "maps_*", "floor_*",
                                             "metatiles.bin")))
        self.assertEqual(len(maps), 18)
        seen = set()
        for path in maps:
            with open(path, "rb") as f:
                row = f.read(16)
            for k, cx in enumerate((6, 7, 8)):
                m = 0x200 + row[cx]
                seen.add(m)
                self.assertIn(m, (own[k], own[k] + 8), path)
        self.assertEqual(seen, {0x213, 0x214, 0x215, 0x21B, 0x21C, 0x21D})
        for m in own:
            a = L.cell_image(m).convert("RGB").load()
            b = L.cell_image(m + 8).convert("RGB").load()
            rows = {j for j in range(16) for i in range(16) if a[i, j] != b[i, j]}
            self.assertEqual(rows, {13, 14, 15})


class TrainerHillEntranceTests(unittest.TestCase):
    """Trainer Hill's entrance (the object audit's class 1): its counters, machines and stools lay drawn on the floor;
    cut by hand. Its machines' feet are drawn on their cells' lower layer, where the layer above draws the counter
    or nothing at all - a card of the cell's upper layer alone stood in the wall or lay half flat."""

    LAYOUT = "LAYOUT_TRAINER_HILL_ENTRANCE"

    def test_the_machines_feet_are_on_the_lower_layer(self):
        import furnished_room_specs as frs
        L = vb.LayoutArt(self.LAYOUT)
        for (cx, cy) in ((2, 9), (3, 9), (6, 9)):
            m = L.metatile(cx, cy)
            a = L.cell_image(m).convert("RGB").load()
            b = L.cell_image(m, layers=(0,)).convert("RGB").load()
            # the machine's foot on the lower layer, the counter's top over it from row 12 on
            self.assertEqual({j for j in range(16) for i in range(16) if a[i, j] != b[i, j]}, {12, 13, 14, 15})
        # the terminal's body: nothing on its upper layer at all
        m = L.metatile(0, 10)
        self.assertEqual(m, 0x004)
        a = L.cell_image(m).convert("RGB").load()
        b = L.cell_image(m, layers=(0,)).convert("RGB").load()
        self.assertTrue(all(a[i, j] == b[i, j] for j in range(16) for i in range(16)))
        # read off the clerks' floor, the scanner's foot is every pixel of its cells' rows 144-155
        runs = frs.upper_apart(self.LAYOUT, [(2, 9), (3, 9)], (32, 144, 64, 156), floor=frs.TH_CLERKS_FLOOR)
        self.assertEqual(sum(x1 - x0 for (x0, _, x1, _) in runs), 32 * 12)

    def test_the_pieces(self):
        pieces = own_pieces(self.LAYOUT)
        self.assertEqual(sorted(pieces), sorted(
            ["stool_%d" % cx for cx in (1, 2, 3, 15, 16, 17)]
            + ["pc", "terminal", "scanner", "cabinet", "post", "counter_w", "counter_partition", "counter_e",
               "counter_corridor_w", "counter_corridor_e", "counter_arm_w", "counter_arm_e", "stairwell_stairs",
               "wall_corridor", "block_side_w", "block_side_e", "wall_panel_w", "wall_w", "wall_e", "side_w",
               "side_e"]))
        # the side walls from the back wall's foot - the west panel's, a pilaster 6 rows before it - to the front
        self.assertEqual(pieces["side_w"]["walls"], [((0, 272), (0, 150))])
        self.assertEqual(pieces["side_e"]["walls"], [((304, 144), (304, 272))])
        self.assertEqual(pieces["wall_panel_w"]["foot"], 150)
        self.assertEqual(pieces["wall_e"]["foot"], 144)
        for name in ("pc", "terminal", "scanner", "cabinet", "post") + tuple("stool_%d" % cx for cx in
                                                                            (1, 2, 3, 15, 16, 17)):
            self.assertTrue(pieces[name]["card"], name)

        def bottom(pc):
            return max(d for (_, _, _, d) in pc["shape"])
        # the scanner and the cabinet stand on the clerks' floor before the wall (its foot row 144), behind
        # the counter's top (from row 156): never in the wall's plane, where they flickered with it
        for name in ("scanner", "cabinet"):
            self.assertEqual(bottom(pieces[name]), 156, name)
            self.assertEqual(pieces[name]["not_floor"], [(pieces[name]["shape"][0][0] // 16 * 16, 144,
                                                          pieces[name]["shape"][0][0] // 16 * 16 + (32 if name ==
                                                                                                    "scanner" else 16),
                                                          156)], name)
        # the terminal stands on its foot (row 184); the cell in front of it is the lobby's other floor,
        # laid as its own marks
        self.assertEqual(bottom(pieces["terminal"]), 184)
        self.assertEqual(pieces["terminal"]["not_floor"], [(0, 182, 16, 192)])
        # the post stands on the east counter's top, behind its own foot drawn there
        post = pieces["post"]
        self.assertEqual(post["foot"], 168)
        self.assertTrue(164 <= post["foot"] < 175)
        # the partition: the counter's height, run back to the wall's face (its top drawn from row 136: 144 - 8),
        # its top filled under the post, and before the counter, whose back rows it takes
        part = pieces["counter_partition"]
        self.assertEqual((part["shape"], part["height"], part["foot"], part["fill"]),
                         ([(240, 136, 256, 160)], 8, 175, 3))
        names = list(pieces)
        self.assertLess(names.index("counter_partition"), names.index("counter_e"))
        self.assertLess(names.index("post"), names.index("counter_partition"))
        for name in ("counter_w", "counter_e", "counter_partition"):
            self.assertEqual(pieces[name]["height"], 8, name)
            self.assertTrue(pieces[name]["solid"], name)

    def test_the_counters_stand_on_blocked_cells(self):
        """Play is the GBA game's: no counter's foot runs over a cell the player walks on."""
        L = vb.LayoutArt(self.LAYOUT)
        pieces = own_pieces(self.LAYOUT)
        for name, pc in pieces.items():
            if not name.startswith("counter"):
                continue
            foot = pc["foot"] if pc["foot"] is not None else max(d for (_, _, _, d) in pc["shape"])
            cy = (foot - 1) // 16
            for (x0, _, x1, _) in pc["shape"]:
                for cx in range(x0 // 16, (x1 - 1) // 16 + 1):
                    self.assertNotEqual((L.blocks[cy * L.w + cx] >> 10) & 3, 0, (name, cx, cy))


if __name__ == "__main__":
    unittest.main()
