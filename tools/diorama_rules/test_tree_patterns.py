#!/usr/bin/env python3
"""The rules' tree patterns (data/diorama/defaults.json, the grouped-hull exact patterns) against the maps.

With the Diorama's voxel trees (F8) a General tileset large tree stands as the 3D tree only where a grouped-hull exact
pattern matches its cells; a tree whose cells form a combination no pattern lists lies as plain ground, its art painted
flat (KNOWN_ISSUES V23). In every map of the General tileset, every two-by-two block of a large tree's body row
(0x1D4/0x1D6 left, 0x1D5/0x1D7 right) over its lower row must match a two-row pattern, and every such block under a
crown tip row (0x1CE/0x1CF over grass, 0x1C6/0x1C7 over tall grass) a three-row pattern with the tips. Run from the
repository root: python3 tools/diorama_rules/test_tree_patterns.py
"""
import json
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

BODY_LEFT, BODY_RIGHT = {0x1D4, 0x1D6}, {0x1D5, 0x1D7}
LOWER_LEFT, LOWER_RIGHT = {0x1DC, 0x1E4, 0x1E6, 0x1F3}, {0x1DD, 0x1E5, 0x1E7, 0x1F2}
TIPS = {(0x1CE, 0x1CF), (0x1C6, 0x1C7)}


def tree_patterns(defaults):
    """The General tileset's grouped-hull patterns as cell tuples: (two-row set, three-row set)."""
    two, three = set(), set()
    for pattern in defaults["exactPatterns"]:
        if (pattern["action"].get("archetype") != "grouped-hull"
                or pattern["tilesets"].get("primary") != "gTileset_General"
                or "secondary" in pattern["tilesets"]
                or pattern["dimensions"]["width"] != 2):
            continue
        cells = tuple(tuple(row) for row in pattern["cells"])
        if pattern["dimensions"]["height"] == 2:
            two.add(cells)
        elif pattern["dimensions"]["height"] == 3:
            three.add(cells)
    return two, three


def unmatched(two, three, layouts):
    """(blocks seen, those under a tip row, [unmatched two-row blocks], [tipped blocks without a three-row pattern])."""
    blocks = tipped = 0
    missing_two, missing_three = [], []
    for layout in layouts:
        if not layout or layout.get("primary_tileset") != "gTileset_General":
            continue
        with open(os.path.join(ROOT, layout["blockdata_filepath"]), "rb") as handle:
            data = handle.read()
        width, height = layout["width"], layout["height"]
        cells = [struct.unpack_from("<H", data, 2 * i)[0] & 0x3FF for i in range(width * height)]
        for y in range(height - 1):
            for x in range(width - 1):
                upper = (cells[y * width + x], cells[y * width + x + 1])
                lower = (cells[(y + 1) * width + x], cells[(y + 1) * width + x + 1])
                if (upper[0] not in BODY_LEFT or upper[1] not in BODY_RIGHT
                        or lower[0] not in LOWER_LEFT or lower[1] not in LOWER_RIGHT):
                    continue
                blocks += 1
                if (upper, lower) not in two:
                    missing_two.append((layout["name"], x, y, upper, lower))
                tip = (cells[(y - 1) * width + x], cells[(y - 1) * width + x + 1]) if y > 0 else None
                if tip in TIPS:
                    tipped += 1
                    if (tip, upper, lower) not in three:
                        missing_three.append((layout["name"], x, y - 1, tip, upper, lower))
    return blocks, tipped, missing_two, missing_three


def main():
    with open(os.path.join(ROOT, "data", "diorama", "defaults.json")) as handle:
        defaults = json.load(handle)
    with open(os.path.join(ROOT, "data", "layouts", "layouts.json")) as handle:
        layouts = json.load(handle)["layouts"]
    two, three = tree_patterns(defaults)
    blocks, tipped, missing_two, missing_three = unmatched(two, three, layouts)
    hexes = lambda rows: "/".join(",".join("%X" % metatile for metatile in row) for row in rows)
    for name, x, y, *rows in (missing_two + missing_three)[:20]:
        print(f"{name} ({x}, {y}): {hexes(rows)} matches no pattern")
    if missing_two or missing_three:
        sys.exit(f"test_tree_patterns: {len(missing_two)} two-by-two tree blocks without a two-row pattern, "
                 f"{len(missing_three)} under a tip row without a three-row pattern")
    # the check finds something to check (the maps read, the patterns parsed)
    assert blocks > 2000 and tipped > 300, (blocks, tipped)
    # without the lower row half a tree to the side's patterns (V23's band in Petalburg Woods) it fails
    mutant = {cells for cells in two if 0x1F3 not in cells[1] and 0x1F2 not in cells[1]}
    assert len(unmatched(mutant, three, layouts)[2]) > 50
    print(f"test_tree_patterns: {blocks} two-by-two tree blocks, each matching a pattern ({tipped} under a tip row, "
          f"each matching a three-row pattern)")


if __name__ == "__main__":
    main()
