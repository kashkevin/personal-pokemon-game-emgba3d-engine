#!/usr/bin/env python3
"""compile_vox.py's coarser models for distant trees (KNOWN_ISSUES MS-244): the blocks, their colours, their quads,
where they stand."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compile_vox  # noqa: E402


def cube(n, color=1, at=(0, 0, 0)):
    return {(at[0] + x, at[1] + y, at[2] + z): color for x in range(n) for y in range(n) for z in range(n)}


class DownsampleTests(unittest.TestCase):
    def test_a_full_block_is_solid_and_a_lone_voxel_is_not(self):
        voxels = cube(2)
        voxels[(5, 5, 5)] = 1
        box = ((0, 0, 0), (8, 8, 8))
        self.assertEqual(compile_vox.downsample(voxels, 2, (0, 0, 0), box), {(0, 0, 0): 1})

    def test_half_a_block_is_solid(self):
        voxels = {(x, 0, z): 1 for x in range(2) for z in range(2)}  # 4 of 8, a voxel thick
        self.assertEqual(compile_vox.downsample(voxels, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {(0, 0, 0): 1})
        voxels = {(0, 0, 0): 1, (1, 0, 0): 1, (1, 1, 0): 1}  # 3 of 8, flat: half its face either way
        self.assertEqual(compile_vox.downsample(voxels, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {})

    def test_a_voxel_thick_trunk_is_kept_where_it_covers_the_block_seen_from_the_front(self):
        # 3 of a block's 8 voxels, a voxel thick, covering 3 of its face's 4 seen along y: kept
        trunk = {(0, 0, 0): 1, (1, 0, 0): 1, (1, 0, 1): 1}
        self.assertEqual(compile_vox.downsample(trunk, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {(0, 0, 0): 1})
        # ... along x the same
        trunk = {(0, 0, 0): 1, (0, 1, 0): 1, (0, 1, 1): 1}
        self.assertEqual(compile_vox.downsample(trunk, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {(0, 0, 0): 1})
        # 2 of its face's 4 is not enough
        twig = {(0, 0, 0): 1, (1, 0, 0): 1}
        self.assertEqual(compile_vox.downsample(twig, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {})

    def test_a_block_is_measured_as_far_as_the_tree_s_box_reaches(self):
        # the tree's top two layers in the top block of four: half its volume, all of the clipped block's
        voxels = {(x, y, z): 1 for x in range(4) for y in range(4) for z in range(2)}
        self.assertEqual(compile_vox.downsample(voxels, 4, (0, 0, 0), ((0, 0, 0), (4, 4, 2))), {(0, 0, 0): 1})
        voxels = {(x, 0, z): 1 for x in range(4) for z in range(2)}  # a voxel thick
        self.assertEqual(compile_vox.downsample(voxels, 4, (0, 0, 0), ((0, 0, 0), (4, 1, 2))), {(0, 0, 0): 1})

    def test_a_block_takes_its_commonest_colour_the_lower_on_a_tie(self):
        voxels = cube(2, 7)
        voxels[(0, 0, 0)] = 3
        self.assertEqual(compile_vox.downsample(voxels, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {(0, 0, 0): 7})
        tie = {(x, y, z): (3 if x == 0 else 9) for x in range(2) for y in range(2) for z in range(2)}
        self.assertEqual(compile_vox.downsample(tie, 2, (0, 0, 0), ((0, 0, 0), (2, 2, 2))), {(0, 0, 0): 3})

    def test_the_grid_s_shift(self):
        # shifted by one, block 0 covers -1 and 0, block 1 covers 1 and 2
        voxels = cube(2, 1, at=(1, 1, 1))
        self.assertEqual(compile_vox.downsample(voxels, 2, (1, 1, 1), ((1, 1, 1), (3, 3, 3))), {(1, 1, 1): 1})


class BlockFacesTests(unittest.TestCase):
    def test_a_cube_is_six_quads_in_the_tree_s_own_units(self):
        voxels = cube(4, 2, at=(4, 0, 0))
        box = compile_vox.bounds(voxels)
        for block in compile_vox.LOD_BLOCKS:
            model = compile_vox.block_faces(voxels, block, (0, 0, 0), box)
            self.assertEqual(len(model), 6)
            self.assertTrue(all(face[-1] == 2 for face in model))
            planes = sorted((axis, plane) for axis, sign, plane, *_ in model)
            self.assertEqual(planes, [(0, 4), (0, 8), (1, 0), (1, 4), (2, 0), (2, 4)])

    def test_its_edges_are_clipped_to_the_tree_s_box(self):
        # 3 voxels tall, in blocks of 4 standing on its lowest voxel: one block, drawn 3 tall from z = 1
        voxels = {(x, y, z): 5 for x in range(4) for y in range(4) for z in range(1, 4)}
        box = compile_vox.bounds(voxels)
        model = compile_vox.block_faces(voxels, 4, (0, 0, -1 % 4), box)
        self.assertEqual(len(model), 6)
        for axis, sign, plane, u0, u1, v0, v1, color in model:
            corners = {axis: (plane, plane), (axis + 1) % 3: (u0, u1), (axis + 2) % 3: (v0, v1)}
            self.assertEqual((min(corners[2]), max(corners[2])), (1, 4) if axis != 2 else (plane, plane))
            if axis == 2:
                self.assertIn(plane, (1, 4))


class TheTreeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        _size, cls.voxels, _palette = compile_vox.read_vox(root / "tree_model" / "emerald_tree.vox")
        cls.box = compile_vox.bounds(cls.voxels)
        cls.whole = compile_vox.greedy_faces(_size, cls.voxels)
        cls.models = compile_vox.lod_faces(cls.voxels)

    def test_each_level_a_few_times_fewer_quads(self):
        self.assertEqual(len(self.models), len(compile_vox.LOD_BLOCKS))
        previous = len(self.whole)
        for model in self.models:
            self.assertLess(len(model) * 3, previous)
            previous = len(model)

    def test_each_stands_where_the_tree_does_as_tall_and_no_wider(self):
        low, high = self.box
        for model in self.models:
            reach = [[None, None] for _ in range(3)]
            for axis, sign, plane, u0, u1, v0, v1, color in model:
                for a, values in ((axis, (plane,)), ((axis + 1) % 3, (u0, u1)), ((axis + 2) % 3, (v0, v1))):
                    for value in values:
                        reach[a][0] = value if reach[a][0] is None else min(reach[a][0], value)
                        reach[a][1] = value if reach[a][1] is None else max(reach[a][1], value)
            self.assertEqual(reach[2], [low[2], high[2]])  # on the ground, to the tree's top
            for axis in (0, 1):
                self.assertGreaterEqual(reach[axis][0], low[axis])
                self.assertLessEqual(reach[axis][1], high[axis])

    def test_its_silhouettes_match_the_tree_s(self):
        expected = {2: 0.93, 4: 0.86}
        for block in compile_vox.LOD_BLOCKS:
            shift = compile_vox.lod_shift(self.voxels, block)
            self.assertEqual(shift[2], -self.box[0][2] % block)  # standing on the lowest voxel
            cells = compile_vox.filled(self.voxels, block, shift, self.box)
            self.assertGreater(compile_vox.silhouettes_match(self.voxels, cells), expected[block])
            # about the tree's own volume: within a fifth of it
            self.assertLess(abs(len(cells) - len(self.voxels)), len(self.voxels) / 5)

    def test_the_trunk_reaches_the_ground(self):
        low = self.box[0]
        ground = {(x, y) for (x, y, z) in self.voxels if z == low[2]}
        for block in compile_vox.LOD_BLOCKS:
            shift = compile_vox.lod_shift(self.voxels, block)
            cells = compile_vox.filled(self.voxels, block, shift, self.box)
            base = {(x, y) for (x, y, z) in cells if z == low[2]}
            self.assertTrue(ground & base, f"blocks of {block}: nothing on the ground under the trunk")


if __name__ == "__main__":
    unittest.main()
