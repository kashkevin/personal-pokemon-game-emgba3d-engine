#!/usr/bin/env python3
"""Lists and draws the metatiles of a tileset that look like standing objects
(graphics overhaul, phase 2: the environmental object catalogue,
src/diorama/scene/props.c): an upper layer that covers part of the cell over a
lower layer, or a single layer with transparent edges, used on the maps with
collision. A development tool: a list to look through, not a decision; its
pictures are game art and are not committed.

    prop_candidates.py TILESET OUT.png [--min-uses N]

TILESET is a secondary tileset symbol without gTileset_ (Slateport) or
General. The sheet shows each candidate's upper layer over a checker, with its
id, how many cells use it and how many of those block the player.
"""
import argparse
import json
import struct
from collections import defaultdict

from PIL import Image, ImageDraw

from map_sheet import tileset_folder
from metatile_sheet import Tileset, draw_metatile


def coverage(tileset_pair, metatile, layer):
    image = draw_metatile(*tileset_pair, metatile, (layer,))
    if image is None:
        return 0.0
    alpha = image.getchannel("A")
    return sum(1 for value in alpha.getdata() if value) / 256.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tileset")
    parser.add_argument("out")
    parser.add_argument("--min-uses", type=int, default=2)
    args = parser.parse_args()

    symbol = "gTileset_" + args.tileset
    layouts = [l for l in json.load(open("data/layouts/layouts.json"))["layouts"] if l]
    users = [l for l in layouts if symbol in (l["primary_tileset"], l["secondary_tileset"])]
    outdoor_secondaries = {l["secondary_tileset"] for l in users}
    primary = Tileset(tileset_folder("gTileset_General"))
    secondary_symbol = symbol if symbol != "gTileset_General" else sorted(outdoor_secondaries)[0]
    secondary = Tileset(tileset_folder(secondary_symbol))
    uses = defaultdict(int)
    blocked = defaultdict(int)
    for layout in users:
        blocks = open(layout["blockdata_filepath"], "rb").read()
        for i in range(len(blocks) // 2):
            value = struct.unpack_from("<H", blocks, i * 2)[0]
            metatile = value & 0x3FF
            if (symbol == "gTileset_General") != (metatile < 0x200):
                continue
            uses[metatile] += 1
            blocked[metatile] += (value >> 10) & 3 != 0
    candidates = []
    for metatile, count in sorted(uses.items()):
        if count < args.min_uses:
            continue
        upper = coverage((primary, secondary), metatile, 1)
        lower = coverage((primary, secondary), metatile, 0)
        if 0.04 <= upper <= 0.9 or (upper == 0 and 0.2 <= lower <= 0.9):
            candidates.append((metatile, count, blocked[metatile], upper, lower))
    for metatile, count, block, upper, lower in candidates:
        print(f"{metatile:03X} uses {count:5d} blocked {block:5d} upper {upper:4.2f} lower {lower:4.2f}")
    columns = 16
    scale = 3
    cell = 16 * scale
    label = 24
    rows = (len(candidates) + columns - 1) // columns or 1
    sheet = Image.new("RGB", (columns * (cell + 4), rows * (cell + label + 4)), (30, 30, 34))
    draw = ImageDraw.Draw(sheet)
    for n, (metatile, count, block, upper, lower) in enumerate(candidates):
        x = (n % columns) * (cell + 4)
        y = (n // columns) * (cell + label + 4)
        background = Image.new("RGB", (16, 16), (120, 120, 120))
        checker = background.load()
        for j in range(16):
            for i in range(16):
                if (i // 4 + j // 4) % 2:
                    checker[i, j] = (150, 150, 150)
        art = draw_metatile(primary, secondary, metatile, (1,) if upper else (0,))
        if art is not None:
            background.paste(art, (0, 0), art)
        sheet.paste(background.resize((cell, cell), Image.NEAREST), (x, y + label))
        draw.text((x + 1, y), f"{metatile:03X}", fill=(230, 230, 120))
        draw.text((x + 1, y + 11), f"{count}/{block}", fill=(160, 220, 160))
    sheet.save(args.out)


if __name__ == "__main__":
    main()
