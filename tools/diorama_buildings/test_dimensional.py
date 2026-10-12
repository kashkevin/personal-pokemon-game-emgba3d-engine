#!/usr/bin/env python3
"""Tests for dimensional.py: real roofs, recessed windows and framed doors
(the owner's option B), on drawings made here and on the game's own maps."""

import math
import os
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import auto_buildings  # noqa: E402
import dimensional as dm  # noqa: E402
import voxel_building as vb  # noqa: E402

WALL = (200, 180, 120)
FRAME = (60, 60, 90)
GLASS = (130, 200, 220)
WHITE = (230, 230, 230)
RIDGE = (220, 210, 230)
TILE_A = (110, 90, 160)
TILE_B = (90, 70, 140)
EAVE = (50, 50, 70)


def house(width=64, facade=24, roof=40, ridge_rows=8, course=8, eave_rows=2):
    """A drawing of a house: a ridge band, courses of tiles 8 wide with a
    joint every `course` rows, an eave line, then a plain facade."""
    img = Image.new("RGBA", (width, roof + facade), (0, 0, 0, 0))
    px = img.load()
    for v in range(roof + facade):
        for u in range(width):
            if v < ridge_rows:
                c = RIDGE
            elif v < roof - eave_rows:
                row = v - ridge_rows
                c = TILE_B if row % course == course - 1 else (TILE_A if (u // 8) % 2 == 0 else TILE_B)
            elif v < roof:
                c = EAVE
            else:
                c = WALL
            px[u, v] = c + (255,)
    return img


def window(img, x0, y0, panes=(2, 2), pane=(4, 3)):
    """A framed window, glazing bars between its panes; the top row of panes
    glass, the rest white (a reflection)."""
    px = img.load()
    pw, ph = pane
    nx, ny = panes
    w, h = nx * (pw + 1) + 1, ny * (ph + 1) + 1
    for j in range(h):
        for i in range(w):
            px[x0 + i, y0 + j] = FRAME + (255,)
    for b in range(ny):
        for a in range(nx):
            for j in range(ph):
                for i in range(pw):
                    c = GLASS if b == 0 else WHITE
                    px[x0 + 1 + a * (pw + 1) + i, y0 + 1 + b * (ph + 1) + j] = c + (255,)
    return (x0 + 1, y0 + 1, x0 + w - 1, y0 + h - 1)


def facade_mesh(width, height, z):
    """A facade standing at z: two triangles, mapped as the GBA sees them."""
    mesh = vb.Mesh()
    quad = [(0.0, 0.0, z), (float(width), 0.0, z), (float(width), float(height), z),
            (0.0, float(height), z)]
    mesh.poly([(x, y, zz, x, zz - y) for (x, y, zz) in quad], vb.SHADE_ART, "front")
    return mesh


def plan_area(tris):
    """Total area of the triangles' projection on the facade's plane (x, y)."""
    total = 0.0
    for ((a, b, c), shade, tag) in tris:
        total += abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) / 2.0
    return total


class RoofAnalysisTests(unittest.TestCase):
    def test_courses_ridge_and_eave_are_read(self):
        info = dm.analyse(house(), 24)
        self.assertEqual(info["eave"], 40)
        self.assertEqual(info["top"], 0)
        self.assertEqual(info["across"], 16)          # two tiles' colours repeat
        c0, c1 = info["course"]
        self.assertEqual(c1 - c0, 8)                   # one course, joint included
        self.assertEqual(info["ridge"], (0, c0))       # the band above the courses
        self.assertTrue(info["ridge"][1] >= 8)
        self.assertEqual(info["eaves"][1], 40)
        self.assertTrue(info["eaves"][1] - info["eaves"][0] <= dm.EAVE_ROWS)

    def test_no_roof_to_read(self):
        self.assertIsNone(dm.analyse(house(roof=4), 24))
        self.assertIsNone(dm.analyse(Image.new("RGBA", (64, 64), (0, 0, 0, 0)), 24))

    def test_swatch_is_opaque_and_whole_tiles_wide(self):
        art = house()
        info = dm.analyse(art, 24)
        out, rows = dm.material(art, art, info)
        width = dm.swatch_width(info)
        self.assertEqual(width % (info["across"] or 8), 0)
        self.assertEqual(out.size[1], art.size[1] + sum(b - a for a, b in rows))
        px = out.load()
        for (a, b) in rows:
            for v in range(a, b):
                for u in range(width):
                    self.assertEqual(px[u, v][3], 255)
        # the drawing itself is untouched
        self.assertEqual(out.crop((0, 0) + art.size).tobytes(), art.tobytes())


class PitchedRoofTests(unittest.TestCase):
    def setUp(self):
        self.art = house()
        out, part, info = dm.roofed("house", self.art, self.art, 24, 16, {"type": "pitched"})
        self.out, self.info = out, info
        self.mesh = vb.Mesh()
        part.emit(self.mesh)

    def test_a_roof_over_the_walls_rising_to_a_ridge(self):
        pts = [p for (tri, s, t) in self.mesh.tris for p in tri]
        ys = [p[1] for p in pts]
        self.assertAlmostEqual(min(ys), 24.0)                  # on the walls' top
        rise = max(ys) - 24.0
        # 30 degrees over half the depth (64 - 16, the eaves 2 out) and a ridge
        half = (64 - 16 + 2 * dm.OVERHANG) / 2.0
        self.assertGreater(rise, math.tan(math.radians(20)) * half)
        xs = [p[0] for p in pts]
        zs = [p[2] for p in pts]
        x0, x1 = self.info["body"]
        self.assertAlmostEqual(min(xs), x0 - dm.OVERHANG)
        self.assertAlmostEqual(max(xs), x1 + dm.OVERHANG)
        self.assertAlmostEqual(max(zs), 64 + dm.OVERHANG)       # the front eave
        self.assertAlmostEqual(min(zs), 16 - dm.OVERHANG)       # the back eave

    def test_every_texel_it_samples_is_opaque_and_never_stretched(self):
        px = self.out.load()
        for (tri, shade, tag) in self.mesh.tris:
            self.assertIn("~behind", tag)
            cu = sum(p[3] for p in tri) / 3.0
            cv = sum(p[4] for p in tri) / 3.0
            self.assertGreaterEqual(cv, self.art.size[1])        # the swatch, not the drawing
            self.assertEqual(px[int(cu), int(cv)][3], 255)
        model = vb.Model("house", self.out, [], (4, 4), 1)
        model.mesh = self.mesh
        self.assertEqual(vb.density_check(model), [])


class ShellAndParapetTests(unittest.TestCase):
    def test_a_pyramid_rises_over_its_plan(self):
        art = house()
        out, shell, info = dm.shelled("tower", art, art, 24, 16,
                                      {"type": "pyramid8", "rise": 1.0})
        mesh = vb.Mesh()
        shell.emit(mesh)
        # an octagon's faces (those across the middle cut in two, halves
        # textured apart)
        self.assertGreaterEqual(len(mesh.tris), 8)
        apex = max(p[1] for (tri, s, t) in mesh.tris for p in tri)
        self.assertGreater(apex, 24 + 20)
        px = out.load()
        for (tri, s, t) in mesh.tris:
            for p in tri:
                self.assertEqual(px[min(out.size[0] - 1, int(p[3])), min(out.size[1] - 1, int(p[4]))][3], 255)

    def test_a_parapet_keeps_the_drawing_seen_at_45_degrees(self):
        # a flat-roofed box: its Relief alone reproduces the drawing; with the
        # parapet round its roof it still does, whatever the parapet hides
        art = house(roof=24)
        relief = vb.Relief("box", art, 24, vb.Tile(0, 24, 8, 48), back=0)
        _, parapet, info = dm.parapeted("box", art, art, 24, 0, {"type": "flat", "rise": 6})
        mesh = vb.Mesh()
        relief.emit(mesh)
        bare = len(mesh.tris)
        top = vb.Mesh()
        parapet.emit(top)
        for (tri, s, t) in top.tris:
            # the parapet's front and top textured as projected: exact
            if t.endswith(".front") or t.endswith(".top"):
                for (x, y, z, u, v) in tri:
                    self.assertAlmostEqual(v, z - y)
        mesh.tris += [(tri, s, t.replace("~behind", "")) for (tri, s, t) in top.tris
                      if t.endswith(".front") or t.endswith(".top")]
        self.assertGreater(len(mesh.tris), bare)
        model = vb.Model("box", art, [], (4, 3), 1)
        model.mesh = mesh
        wrong, missing, extra = vb.ortho_check(model, exact=[(0, 0, 64, 48)])
        self.assertEqual((wrong, missing), (0, 0))


class RecessTests(unittest.TestCase):
    def test_a_window_is_set_back_with_reveals_and_a_sill(self):
        mesh = facade_mesh(32, 24, 64.0)
        before = plan_area(mesh.tris)
        # the art rows 44..52 at z 64 are y 12..20
        self.assertTrue(dm.recess(mesh, (8, 44, 16, 52), 1.5, sill=True, name="w"))
        fronts = [t for t in mesh.tris if all(abs(p[2] - 64.0) < 1e-6 for p in t[0])
                  and len({round(p[2], 6) for p in t[0]}) == 1
                  and not any(k in t[2] for k in ("sill", "jamb", "head"))]
        backs = [t for t in mesh.tris if all(abs(p[2] - 62.5) < 1e-6 for p in t[0])
                 and "recess" in t[2]]
        self.assertAlmostEqual(plan_area(fronts) + plan_area(backs), before)
        self.assertAlmostEqual(plan_area(backs), 8 * 8)
        # its texels are where they were: the glass seen straight on
        for (tri, s, t) in backs:
            for (x, y, z, u, v) in tri:
                self.assertAlmostEqual(u, x)
                self.assertAlmostEqual(v, 64.0 - y)
        tags = [t[2] for t in mesh.tris]
        self.assertEqual(sum("jamb" in t for t in tags), 4)      # two jambs, two triangles
        self.assertTrue(any("sill_top" in t for t in tags))
        self.assertTrue(any("head" in t for t in tags))

    def test_nothing_changes_across_two_walls(self):
        mesh = facade_mesh(32, 24, 64.0)
        other = facade_mesh(32, 24, 60.0)
        for (tri, s, t) in other.tris:
            mesh.tri(*[(x + 32, y, z, x + 32, z - y + 4) for (x, y, z, u, v) in tri],
                     shade=s, tag=t)
        count = len(mesh.tris)
        # straddles the two facades: no single plane draws it
        self.assertFalse(dm.recess(mesh, (28, 44, 36, 52), 1.5))
        self.assertEqual(len(mesh.tris), count)

    def test_a_framed_window_is_found_whole(self):
        img = Image.new("RGBA", (48, 32), WALL + (255,))
        rect = window(img, 10, 8)
        px = img.load()
        W, H = img.size
        wall = bytearray([1]) * (W * H)

        def colour(x, y):
            return px[x, y][:3]
        found = dm.window_rects(W, H, colour, wall)
        self.assertIn(rect, found)


class DoorTests(unittest.TestCase):
    def test_a_doorway_is_where_the_door_opens(self):
        # Lilycove's houses: the door's leaf in its cell and the four pixels
        # over it, as the door's own opening animation draws it
        layout = vb.LayoutArt("LAYOUT_LILYCOVE_CITY")
        x0, y0, x1, y1 = dm.doorway(layout, 39, 14)
        self.assertTrue(39 * 16 <= x0 < x1 <= 40 * 16)
        self.assertTrue(13 * 16 + 8 <= y0 < 14 * 16 < y1 <= 15 * 16)
        self.assertGreaterEqual(x1 - x0, 10)
        self.assertGreaterEqual(y1 - y0, 14)

    def test_no_doorway_without_its_animation(self):
        layout = vb.LayoutArt("LAYOUT_LILYCOVE_CITY")
        self.assertIsNone(dm.doorway(layout, 0, 0))


class ConfigurationTests(unittest.TestCase):
    def test_every_roof_named_is_one_dimensional_builds(self):
        for name, table in auto_buildings.ROOFS.items():
            for door, kind in table.items():
                roof = auto_buildings.roof_of(name, door if door != "default" else (-1, -1))
                if roof is None:
                    continue
                parts = roof["parts"] if roof["type"] == "split" else [(None, roof)]
                for cols, sub in parts:
                    sub = {"type": sub} if isinstance(sub, str) else sub
                    self.assertIn(sub["type"], dm.BUILDERS, (name, door))

    def test_a_split_roof_gives_each_part_its_columns(self):
        art = house(width=128)
        done = dm.build_roof("pair", art, art, 24, 16,
                             {"type": "split", "parts": [((0, 64), "pitched"), ((64, 128), "flat")]})
        out, parts, lids, exact, relief = done
        self.assertEqual(len(parts), 2)
        self.assertEqual(len(lids), 1)                           # the flat part keeps its top
        self.assertTrue(lids[0][1] <= 64)


if __name__ == "__main__":
    unittest.main()
