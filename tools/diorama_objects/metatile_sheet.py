#!/usr/bin/env python3
"""Draws a tileset's metatiles as a sheet with their ids (graphics overhaul,
phase 2: the environmental object catalogue is made by looking at these).

    metatile_sheet.py PRIMARY_DIR SECONDARY_DIR OUT.png [--secondary] [--scale N]
                      [--layer both|upper] [--first ID --count N]

PRIMARY_DIR/SECONDARY_DIR are data/tilesets/primary/<name> and
data/tilesets/secondary/<name> (tiles.png, palettes/NN.pal, metatiles.bin).
Without --secondary the primary's 512 metatiles are drawn, with it the
secondary's (ids from 0x200). --layer upper draws only the upper layer over a
grey checker, which shows the objects standing on the ground (signs, lamps,
fences, flowers). A development tool: its pictures are not committed (they are
game art).
"""
import argparse
import struct

from PIL import Image, ImageDraw


def load_palette(path):
    lines = open(path, encoding="ascii", errors="replace").read().split("\n")
    colours = []
    for line in lines[3:19]:
        parts = line.split()
        if len(parts) == 3:
            colours.append(tuple(int(v) for v in parts))
    while len(colours) < 16:
        colours.append((0, 0, 0))
    return colours


class Tileset:
    def __init__(self, folder):
        self.tiles = Image.open(folder + "/tiles.png")
        self.palettes = [load_palette(f"{folder}/palettes/{i:02d}.pal") for i in range(16)]
        self.metatiles = open(folder + "/metatiles.bin", "rb").read()
        self.width = self.tiles.size[0] // 8
        self.pixels = self.tiles.load()

    def tile(self, index):
        x, y = (index % self.width) * 8, (index // self.width) * 8
        if y >= self.tiles.size[1]:
            return None
        return [[self.pixels[x + i, y + j] & 15 for i in range(8)] for j in range(8)]


def draw_metatile(primary, secondary, metatile, layers):
    source = primary if metatile < 512 else secondary
    offset = metatile if metatile < 512 else metatile - 512
    if (offset + 1) * 16 > len(source.metatiles):
        return None
    entries = struct.unpack_from("<8H", source.metatiles, offset * 16)
    image = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    out = image.load()
    for layer in layers:
        for quarter in range(4):
            entry = entries[layer * 4 + quarter]
            tile_id, hflip, vflip, palette = entry & 0x3FF, entry >> 10 & 1, entry >> 11 & 1, entry >> 12
            owner = primary if tile_id < 512 else secondary
            pixels = owner.tile(tile_id % 512)
            if pixels is None:
                continue
            colours = (primary if palette < 6 else secondary).palettes[palette]
            for y in range(8):
                for x in range(8):
                    sx = 7 - x if hflip else x
                    sy = 7 - y if vflip else y
                    index = pixels[sy][sx]
                    if index:
                        out[(quarter & 1) * 8 + x, (quarter >> 1) * 8 + y] = colours[index] + (255,)
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("primary")
    parser.add_argument("secondary")
    parser.add_argument("out")
    parser.add_argument("--secondary-sheet", dest="secondary_sheet", action="store_true")
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--layer", choices=("both", "upper"), default="both")
    parser.add_argument("--first", type=lambda v: int(v, 0), default=None)
    parser.add_argument("--count", type=int, default=512)
    parser.add_argument("--columns", type=int, default=16)
    args = parser.parse_args()

    primary = Tileset(args.primary)
    secondary = Tileset(args.secondary)
    first = args.first if args.first is not None else (512 if args.secondary_sheet else 0)
    layers = (0, 1) if args.layer == "both" else (1,)
    cell = 16 * args.scale
    label = 12
    columns = args.columns
    rows = (args.count + columns - 1) // columns
    sheet = Image.new("RGB", (columns * (cell + 4), rows * (cell + label + 4)), (30, 30, 34))
    draw = ImageDraw.Draw(sheet)
    for n in range(args.count):
        metatile = first + n
        image = draw_metatile(primary, secondary, metatile, layers)
        if image is None:
            continue
        x = (n % columns) * (cell + 4)
        y = (n // columns) * (cell + label + 4)
        background = Image.new("RGB", (16, 16), (120, 120, 120))
        if args.layer == "upper":
            checker = background.load()
            for j in range(16):
                for i in range(16):
                    if (i // 4 + j // 4) % 2:
                        checker[i, j] = (150, 150, 150)
        background.paste(image, (0, 0), image)
        sheet.paste(background.resize((cell, cell), Image.NEAREST), (x, y + label))
        draw.text((x + 1, y), f"{metatile:03X}", fill=(230, 230, 120))
    sheet.save(args.out)


if __name__ == "__main__":
    main()
