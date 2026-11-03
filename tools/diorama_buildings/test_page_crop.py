#!/usr/bin/env python3
"""Tests for gen_voxel_buildings.page_crop: a model's page keeps every texel its triangles read.

The pages set their images edge to edge. A floor decal over a cell its marks only half fill read past
what is opaque into the next image on the page (MS-164: under the bike shop's beds, a flat copy of the
other bed's top on the floor in front of them)."""

import os
import sys
import types
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402


def model(art, uvs):
    """A model of one triangle per three (u, v) reads, over `art`."""
    tris = [([(0, 0, 0, u, v) for (u, v) in uvs[i:i + 3]], 0, "piece_under~behind.decal")
            for i in range(0, len(uvs), 3)]
    return types.SimpleNamespace(art=art, mesh=types.SimpleNamespace(tris=tris))


def drawing(w, h, box):
    """A transparent w x h art, opaque in `box`."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x0, y0, x1, y1 = box
    for y in range(y0, y1):
        for x in range(x0, x1):
            img.putpixel((x, y), (200, 100, 50, 255))
    return img


class PageCropTests(unittest.TestCase):
    def test_a_decal_past_what_is_opaque_keeps_its_texels(self):
        # the bike shop's bed: 32 x 96, opaque from row 3 to row 87; its under decal's last cell
        # reads rows 80 to 96
        art = drawing(32, 96, (0, 3, 32, 87))
        m = model(art, [(0, 80), (32, 80), (32, 96), (0, 80), (32, 96), (0, 96)])
        self.assertEqual(g.page_crop(m), (0, 3, 32, 96))

    def test_reads_inside_what_is_opaque_keep_the_opaque_box(self):
        art = drawing(32, 32, (4, 4, 28, 28))
        self.assertEqual(g.page_crop(model(art, [(8, 8), (20, 8), (20, 20)])), (4, 4, 28, 28))

    def test_never_past_the_art(self):
        art = drawing(16, 16, (2, 2, 14, 14))
        self.assertEqual(g.page_crop(model(art, [(-3.5, -1), (20, 0), (20, 30.2)])), (0, 0, 16, 16))

    def test_a_fractional_read_keeps_its_whole_texel(self):
        art = drawing(16, 16, (4, 4, 8, 8))
        self.assertEqual(g.page_crop(model(art, [(2.5, 3.2), (9.1, 3.2), (9.1, 10.7)])), (2, 3, 10, 11))

    def test_no_triangles(self):
        self.assertEqual(g.page_crop(model(drawing(16, 16, (4, 4, 8, 8)), [])), (4, 4, 8, 8))
        self.assertEqual(g.page_crop(model(Image.new("RGBA", (8, 8)), [])), (0, 0, 1, 1))


if __name__ == "__main__":
    unittest.main()
