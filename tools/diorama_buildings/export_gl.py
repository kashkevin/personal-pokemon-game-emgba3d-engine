#!/usr/bin/env python3
"""Turn buildings.bin (VXB7, written by gen_voxel_buildings.py) into C for the
Diorama renderer.

    python3 export_gl.py buildings.bin src/data/diorama/buildings.generated.c

The 3DS port stores each texture page as RGBA5551 texels in the PICA200's
tiled order, coloured once from the tilesets' own palettes. The PC renderer
draws the world from the game's live palettes instead (so palette fades and
the day/night tint reach it), so every page texel is turned back into the BG
palette slot it was drawn from (palette * 16 + colour, 0..207; 255 is
transparent) and stored row by row, top row first. A texel's colour is the
slot's colour exactly (5 bits a channel either way); where several slots of
the layout's tileset pair hold that colour, the slot the layout's own
metatiles draw it with most often wins, then the first slot of the pair.

The rest of the file (header, models, placements, masks, vertices) is kept
byte for byte, up to the first page's texels, and read by
src/diorama/scene/buildings.c the way the 3DS port's voxel_building.c reads it.

The windows of the buildings of outdoor maps (window_glow.py) follow as runs
of page texels: page, row, first column, length << 8 | brightness.
"""

import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import expansion_assets  # noqa: E402
import voxel_building as vb  # noqa: E402
import window_glow  # noqa: E402

MAGIC = b"VXB7"
TRANSPARENT = 255
BG_SLOTS = 13 * 16


def texel_offset(x, y, width):
    """8x8 Morton tiles, the PICA200 order (gen_voxel_buildings.py)."""
    morton = 0
    for bit in range(3):
        morton |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1)
    return ((y // 8) * (width // 8) + x // 8) * 64 + morton


def rgb5(rgb):
    return (rgb[0] >> 3, rgb[1] >> 3, rgb[2] >> 3)


class SlotFinder:
    """RGB555 -> BG palette slot, for one layout's tileset pair."""

    def __init__(self, layout_name):
        art = vb.LayoutArt(layout_name)
        self.ts = art.ts
        drawn = Counter()
        used = sorted(set(art.metatile(x, y) for y in range(art.h) for x in range(art.w)))
        for m in used:
            entries = art.entries(m)
            for entry in entries:
                tile, pal = entry & 0x3FF, (entry >> 12) & 0xF
                for rgb, idx in self.ts.subtile(tile, pal):
                    drawn[(rgb5(rgb), pal * 16 + idx)] += 1
        self.best = {}
        for (colour, slot), n in drawn.items():
            if colour not in self.best or n > drawn[(colour, self.best[colour])]:
                self.best[colour] = slot
        # every colour of the pair's thirteen BG palettes, first slot first
        self.any = {}
        for pal in range(13):
            palettes = self.ts.pp if pal < 6 else self.ts.sp
            if pal >= len(palettes):
                continue
            for idx, rgb in enumerate(palettes[pal]):
                self.any.setdefault(rgb5(rgb), pal * 16 + idx)

    def slot(self, colour):
        """(slot, how): how is "drawn", "pair" or "nearest". A model reused
        from another town's drawing can carry a colour this pair lacks (the
        gym statue): the nearest colour of the pair stands in for it."""
        if colour in self.best:
            return self.best[colour], "drawn"
        if colour in self.any:
            return self.any[colour], "pair"
        nearest = min(self.any, key=lambda c: sum((a - b) ** 2 for a, b in zip(c, colour)))
        return self.any[nearest], "nearest"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    blob = open(sys.argv[1], "rb").read()
    if blob[:4] != MAGIC:
        raise SystemExit("%s: not a VXB7 file" % sys.argv[1])
    (pages, models, page_models, placements, height_bytes, masks, vertices,
     variants, _) = struct.unpack_from("<HHHHHHIHH", blob, 4)
    head = 4 + struct.calcsize("<HHHHHHIHH")
    page_table = [struct.unpack_from("<HHI", blob, head + 8 * i) for i in range(pages)]
    body = head + 8 * pages
    # which layout each page belongs to: placements x 16 after models and page models
    place_at = body + 16 * models + 8 * page_models
    page_model_page = [struct.unpack_from("<HHhh", blob, body + 16 * models + 8 * i)[1]
                       for i in range(page_models)]
    page_layout = {}
    for i in range(placements):
        layout, page_model = struct.unpack_from("<HH", blob, place_at + 16 * i)
        page_layout.setdefault(page_model_page[page_model], layout)

    layouts = expansion_assets.emerald_layouts(vb.ROOT)
    texels_start = page_table[0][2] if pages else len(blob)
    out_pages = []
    window_runs = []
    misses = 0
    near = 0
    for page, (w, h, offset) in enumerate(page_table):
        layout = layouts[page_layout[page] - 1]
        finder = SlotFinder(layout["id"])
        texels = struct.unpack_from("<%dH" % (w * h), blob, offset)
        rows = bytearray(w * h)
        for y in range(h):
            for x in range(w):
                value = texels[texel_offset(x, y, w)]
                if not value & 1:
                    rows[y * w + x] = TRANSPARENT
                    continue
                colour = ((value >> 11) & 31, (value >> 6) & 31, (value >> 1) & 31)
                slot, how = finder.slot(colour)
                if how == "nearest":
                    misses += 1
                elif how == "pair":
                    near += 1
                rows[y * w + x] = slot
        out_pages.append((w, h, bytes(rows)))
        # Windows glow only outdoors (a town's or a route's buildings).
        if layout.get("primary_tileset") == "gTileset_General":
            def colour(x, y, texels=texels, w=w):
                value = texels[texel_offset(x, y, w)]
                if not value & 1:
                    return None
                return (((value >> 11) & 31) * 8, ((value >> 6) & 31) * 8, ((value >> 1) & 31) * 8)

            wall = window_glow.wall_mask(w, h, window_glow.page_triangles(blob, page))
            pieces = window_glow.find_windows(w, h, colour, wall)
            for (y, x, length, level) in window_glow.runs(page, pieces):
                window_runs.append((page, y, x, length << 8 | level))

    table = blob[:texels_start]
    offsets, data = [], bytearray()
    for (w, h, rows) in out_pages:
        offsets.append(len(data))
        data += rows

    def literal(name, raw, align):
        lines = []
        for i in range(0, len(raw), 64):
            lines.append('"' + "".join("\\%o" % b for b in raw[i:i + 64]) + '"')
        return ("const unsigned char %s[%d] __attribute__((aligned(%d))) =\n%s;\n"
                % (name, len(raw), align, "\n".join(lines) if lines else '""'))

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("// Generated by tools/diorama_buildings/export_gl.py from buildings.bin "
                "(gen_voxel_buildings.py). Do not edit.\n")
        f.write("#ifdef ENABLE_DIORAMA\n")
        f.write("const unsigned int gDioramaBuildingsTableSize = %d;\n" % len(table))
        f.write(literal("gDioramaBuildingsTable", table, 4))
        f.write("const unsigned int gDioramaBuildingsPageCount = %d;\n" % len(out_pages))
        f.write("const unsigned int gDioramaBuildingsPageOffsets[%d] = {%s};\n"
                % (max(1, len(offsets)), ", ".join(str(o) for o in offsets) or "0"))
        f.write(literal("gDioramaBuildingsPageSlots", bytes(data) or b"\0", 4))
        f.write("const unsigned int gDioramaBuildingsWindowRunCount = %d;\n" % len(window_runs))
        f.write("const unsigned short gDioramaBuildingsWindowRuns[%d][4] = {\n" % max(1, len(window_runs)))
        for run in window_runs:
            f.write("    {%d, %d, %d, %d},\n" % run)
        if not window_runs:
            f.write("    {0, 0, 0, 0},\n")
        f.write("};\n")
        f.write("#endif\n")
    print("diorama buildings: %d pages, %d texels, %d by the tileset pair's palettes, "
          "%d by the nearest colour, %d window runs -> %s"
          % (len(out_pages), len(data), near, misses, len(window_runs), sys.argv[2]))


if __name__ == "__main__":
    main()
