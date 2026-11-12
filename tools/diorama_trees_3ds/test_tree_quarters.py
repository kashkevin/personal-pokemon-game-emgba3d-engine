#!/usr/bin/env python3
"""The 3DS trees' quarters (src/diorama/scene/tree_sprites.c, DioramaTreeSprites_Part) against the maps.

Each cell of a General tileset large tree draws its own quarter of the tree (its crown's card, its trunk and its
trunk's art on the ground: src/diorama/gl/gl_tree_sprites.c), so a metatile given the wrong quarter draws the other
side's, and an upper row left out of the list leaves its tree with half a crown (KNOWN_ISSUES V23). In every map of
the General tileset, a cell the list makes a lower quarter (2 left, 3 right) must stand under the matching upper
quarter (0, 1). Run from the repository root: python3 tools/diorama_trees_3ds/test_tree_quarters.py
"""
import collections
import json
import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def general_parts(source):
    """{metatile: quarter} for the General tileset's large trees, read from DioramaTreeSprites_Part's switch."""
    body = source[source.index("int DioramaTreeSprites_Part("):]
    body = body[body.index("switch (metatileId)"):body.index("default:")]
    parts, pending = {}, []
    for line in body.splitlines():
        pending += re.findall(r"case (0x[0-9A-Fa-f]+):", line)
        returned = re.search(r"return (\d);", line)
        if returned:
            for metatile in pending:
                parts[int(metatile, 16)] = int(returned.group(1))
            pending = []
    return parts


def misplaced(parts, layouts):
    """The lower quarters not under their upper quarter: Counter of (cell, cell above) and where they are."""
    bad, where, good = collections.Counter(), collections.defaultdict(set), 0
    for layout in layouts:
        if layout.get("primary_tileset") != "gTileset_General":
            continue
        with open(os.path.join(ROOT, layout["blockdata_filepath"]), "rb") as handle:
            data = handle.read()
        width, height = layout["width"], layout["height"]
        cells = [struct.unpack_from("<H", data, 2 * i)[0] & 0x3FF for i in range(width * height)]
        for y in range(1, height):
            for x in range(width):
                quarter = parts.get(cells[y * width + x])
                if quarter not in (2, 3):
                    continue
                if parts.get(cells[(y - 1) * width + x]) == quarter - 2:
                    good += 1
                    continue
                key = (cells[y * width + x], cells[(y - 1) * width + x])
                bad[key] += 1
                where[key].add(layout["name"])
    return good, bad, where


def main():
    with open(os.path.join(ROOT, "src", "diorama", "scene", "tree_sprites.c")) as handle:
        parts = general_parts(handle.read())
    with open(os.path.join(ROOT, "data", "layouts", "layouts.json")) as handle:
        layouts = json.load(handle)["layouts"]
    # the list's own pieces: both lower rows' sides and the upper rows they stand under
    for metatile, quarter in ((0x1D4, 0), (0x1D5, 1), (0x1DC, 2), (0x1DD, 3), (0x1F3, 2), (0x1F2, 3),
                              (0x1FC, 0), (0x1FD, 1)):
        assert parts.get(metatile) == quarter, (hex(metatile), parts.get(metatile), quarter)
    good, bad, where = misplaced(parts, layouts)
    if bad:
        for (cell, above), count in bad.most_common():
            print(f"0x{cell:03X} (quarter {parts[cell]}) under 0x{above:03X} (quarter {parts.get(above)}): "
                  f"{count} cells, {', '.join(sorted(where[(cell, above)])[:6])}")
        sys.exit(f"test_tree_quarters: {sum(bad.values())} lower quarters not under their upper quarter")
    # the check finds something to check (the maps read, the list parsed)
    assert good > 4000, good
    # a mutant list (1F2 and 1F3 given the other way round, as before V23's fix) fails it
    mutant = dict(parts)
    mutant.update({0x1F2: 2, 0x1F3: 3})
    assert sum(misplaced(mutant, layouts)[1].values()) > 150
    print(f"test_tree_quarters: {good} lower quarters, each under its upper quarter")


if __name__ == "__main__":
    main()
