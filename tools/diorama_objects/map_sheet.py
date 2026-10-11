#!/usr/bin/env python3
"""Draws a layout (both layers) with each cell's metatile id over it, so the
objects seen in the game can be matched to their metatiles (graphics overhaul,
phase 2: the environmental object catalogue). A development tool: its pictures
are game art and are not committed.

    map_sheet.py LAYOUT_NAME OUT.png [--scale N] [--x X --y Y --width W --height H]
                 [--only ID,ID,...]

LAYOUT_NAME as in data/layouts/layouts.json without "_Layout" (LittlerootTown).
--only labels just those metatile ids (hex), to find where an object is used.
"""
import argparse
import json
import struct

from PIL import Image, ImageDraw

from metatile_sheet import Tileset, draw_metatile


def tileset_folder(symbol):
    """gTileset_Petalburg -> data/tilesets/secondary/petalburg (or primary)."""
    name = symbol[len("gTileset_"):]
    snake = "".join("_" + c.lower() if c.isupper() and i else c.lower() for i, c in enumerate(name))
    for kind in ("primary", "secondary"):
        for candidate in (name.lower(), snake):
            folder = f"data/tilesets/{kind}/{candidate}"
            try:
                open(folder + "/metatiles.bin", "rb").close()
                return folder
            except OSError:
                pass
    raise SystemExit(f"no tileset folder for {symbol}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("layout")
    parser.add_argument("out")
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--x", type=int, default=0)
    parser.add_argument("--y", type=int, default=0)
    parser.add_argument("--width", type=int, default=0)
    parser.add_argument("--height", type=int, default=0)
    parser.add_argument("--only", default="")
    args = parser.parse_args()

    layouts = json.load(open("data/layouts/layouts.json"))["layouts"]
    layout = next(l for l in layouts if l and l["name"] == args.layout + "_Layout")
    primary = Tileset(tileset_folder(layout["primary_tileset"]))
    secondary = Tileset(tileset_folder(layout["secondary_tileset"]))
    blocks = open(layout["blockdata_filepath"], "rb").read()
    width, height = layout["width"], layout["height"]
    w = args.width or width - args.x
    h = args.height or height - args.y
    only = {int(v, 16) for v in args.only.split(",") if v}
    cell = 16 * args.scale
    sheet = Image.new("RGB", (w * cell, h * cell), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    cache = {}
    for y in range(h):
        for x in range(w):
            mx, my = args.x + x, args.y + y
            if mx >= width or my >= height:
                continue
            value = struct.unpack_from("<H", blocks, (my * width + mx) * 2)[0]
            metatile = value & 0x3FF
            if metatile not in cache:
                cache[metatile] = draw_metatile(primary, secondary, metatile, (0, 1))
            art = cache[metatile]
            if art is None:
                continue
            background = Image.new("RGB", (16, 16), (0, 0, 0))
            background.paste(art, (0, 0), art)
            sheet.paste(background.resize((cell, cell), Image.NEAREST), (x * cell, y * cell))
            if not only or metatile in only:
                draw.rectangle((x * cell, y * cell, x * cell + 22, y * cell + 10), fill=(0, 0, 0))
                draw.text((x * cell + 1, y * cell), f"{metatile:03X}", fill=(255, 255, 120))
    sheet.save(args.out)


if __name__ == "__main__":
    main()
