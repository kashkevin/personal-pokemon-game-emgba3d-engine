#!/usr/bin/env python3
"""Draw the payout table of the PC game's classic 2D Slot Machine (SELECT on the machine; KNOWN_ISSUES MS-84).

The table is a picture: graphics/slot_machine/info_box.bin lays out tiles of graphics/slot_machine/menu.png, and the
GBA's payouts (300, 90, 12, 6, 2 and 4, Pikachu Power's 3) are drawn in its tiles. The PC game pays
include/platform/casino_rates.h's CASINO_SLOT_PAY_* instead, so it gets a table of its own: menu_pc.png is menu.png
with the new numbers' tiles put in tile slots nothing uses (blank in the original, named by neither tilemap nor the
code), and info_box_pc.bin is info_box.bin pointing its number cells at them. The GBA game keeps its files.

The numbers are drawn in the table's own letters: each glyph's ink is the original's (0, 1, 2, 3, 4, 6, 9, the comma,
P, W, R; 5, 7 and 8 drawn alike), with the table's drop shadow (the shadow colour right of, under and diagonally under
every ink pixel), right-aligned where the original's end, one pixel apart. Pikachu Power's row says "PWR,40": "POWER,"
and two digits do not fit beside its three bolts.

    python3 tools/pc_port/gen_slot_info_box.py            # writes graphics/slot_machine/menu_pc.png, info_box_pc.bin
    python3 tools/pc_port/gen_slot_info_box.py --check    # exits 1 when they are not what the rates give
    python3 tools/pc_port/gen_slot_info_box.py --verify   # draws the GBA's numbers and compares them with the original
"""
import argparse
import io
import os
import re
import struct
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MENU = os.path.join(ROOT, "graphics/slot_machine/menu.png")
INFO = os.path.join(ROOT, "graphics/slot_machine/info_box.bin")
MENU_PC = os.path.join(ROOT, "graphics/slot_machine/menu_pc.png")
INFO_PC = os.path.join(ROOT, "graphics/slot_machine/info_box_pc.bin")
RATES = os.path.join(ROOT, "include/platform/casino_rates.h")

COLUMNS, ROWS = 32, 20                  # the info box's tilemap
FIRST_COLUMN = 26                       # the numbers' four tile columns (26-29); column 29's last pixel is the border
RIGHT_EDGE = 27                         # the last ink column of every number, in pixels from column 26's left
BANK = 0x2000                           # the table's palette (BG palette 2), as its tilemap entries have it
INK, SHADOW, BACKGROUND, BORDER = 33, 15, 46, 41   # menu.png's own values in the table's tiles

# Ink of each glyph, five rows high; "," hangs two rows lower (its own rows 4 to 6).
GLYPHS = {
    "0": [".##.", "#..#", "#..#", "#..#", ".##."],
    "1": ["##", ".#", ".#", ".#", ".#"],
    "2": ["###.", "...#", ".##.", "#...", "####"],
    "3": [".##.", "#..#", "..##", "#..#", ".##."],
    "4": ["..##", ".#.#", "#..#", "####", "...#"],
    "5": ["####", "#...", "###.", "...#", "###."],
    "6": [".##.", "#...", "####", "#..#", "###."],
    "7": ["####", "...#", "..#.", ".#..", ".#.."],
    "8": [".##.", "#..#", ".##.", "#..#", ".##."],
    "9": [".###", "#..#", "####", "...#", ".##."],
    "P": ["###", "#.#", "#.#", "###", "#.."],
    "O": ["###", "#.#", "#.#", "#.#", "###"],
    "W": ["#..#", "#..#", "#..#", "####", ".##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "R": ["##.", "#.#", "#.#", "##.", "#.#"],
    ",": ["..", "..", "..", "..", "##", ".#", "#."],
}
GBA_ROWS = {5: "300", 6: "300", 8: "90", 9: "90", 11: "12", 12: "6", 13: "2,4", 15: "POWER,3"}


def rates():
    text = open(RATES, encoding="utf-8").read()
    value = lambda name: int(re.search(r"#define %s\s+(\d+)" % name, text).group(1))
    return value


def pc_rows():
    value = rates()
    number = lambda n: f"{n:,}"
    rows = {5: number(value("CASINO_SLOT_PAY_RED_7")), 6: number(value("CASINO_SLOT_PAY_BLUE_7")),
            8: number(value("CASINO_SLOT_PAY_MIXED_7")), 9: number(value("CASINO_SLOT_PAY_MIXED_7")),
            11: number(value("CASINO_SLOT_PAY_AZURILL")), 12: number(value("CASINO_SLOT_PAY_LOTAD")),
            13: f'{value("CASINO_SLOT_PAY_CHERRY")},{value("CASINO_SLOT_PAY_CHERRY_EDGE")}',
            15: f'PWR,{value("CASINO_SLOT_PAY_POWER")}'}
    for row, text in rows.items():
        width = text_width(text)
        assert width <= RIGHT_EDGE + 1 - 3, f"row {row}: {text!r} is {width} pixels wide"
    return rows


def text_width(text):
    return sum(len(GLYPHS[c][0]) for c in text) + len(text) - 1


class Sheet:
    def __init__(self):
        self.image = Image.open(MENU)
        self.image.load()
        self.width_tiles = self.image.size[0] // 8
        self.count = self.width_tiles * (self.image.size[1] // 8)
        self.pixels = self.image.load()
        self.tilemap = list(struct.unpack("<%dH" % (COLUMNS * ROWS), open(INFO, "rb").read()))

    def tile(self, index):
        tx, ty = index % self.width_tiles, index // self.width_tiles
        return tuple(self.pixels[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8))

    def put_tile(self, index, data):
        tx, ty = index % self.width_tiles, index // self.width_tiles
        for i, value in enumerate(data):
            self.pixels[tx * 8 + i % 8, ty * 8 + i // 8] = value

    def cell(self, row, column):
        """The cell's 8x8 pixels as the table shows them (flips applied)."""
        entry = self.tilemap[row * COLUMNS + column]
        data = self.tile(entry & 0x3FF)
        out = []
        for y in range(8):
            for x in range(8):
                sx = 7 - x if entry & 0x400 else x
                sy = 7 - y if entry & 0x800 else y
                out.append(data[sy * 8 + sx])
        return out


def code_tiles():
    """Tiles of menu.png the slot machine's code names itself (src/slot_machine.c)."""
    source = open(os.path.join(ROOT, "src/slot_machine.c"), encoding="utf-8").read()
    tiles = set()
    table = re.search(r"sPikaPowerTileTable\[\]\[2\] =\s*\{(.*?)\};", source, re.S).group(1)
    tiles.update(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]+)", table))
    for value in re.findall(r"sReelOverlay_Tilemap\[\d\] = 0x([0-9a-fA-F]+);", source):
        tiles.add(int(value, 16) & 0x3FF)
    for call in re.findall(r"SetReelButtonTilemap\([^,]+,\s*(0x\w+),\s*(0x\w+),\s*(0x\w+),\s*(0x\w+)\)", source):
        tiles.update(int(v, 16) for v in call)
    assert len(tiles) >= 18, sorted(tiles)
    return tiles


def free_tiles(sheet):
    menu = struct.unpack("<%dH" % (COLUMNS * ROWS), open(os.path.join(ROOT, "graphics/slot_machine/menu.bin"), "rb").read())
    used = {e & 0x3FF for e in menu} | {e & 0x3FF for e in sheet.tilemap} | code_tiles()
    blank = tuple([0] * 64)
    # from the sheet's end back: its last rows' padding
    return [t for t in range(sheet.count - 1, -1, -1) if t not in used and sheet.tile(t) == blank]


def draw_row(strip, text):
    """Draws `text` into an 8x32 strip (row-major), right-aligned at RIGHT_EDGE."""
    for y in range(8):
        for x in range(31):
            strip[y * 32 + x] = BACKGROUND
    ink = set()
    x = RIGHT_EDGE + 1 - text_width(text)
    for c in text:
        glyph = GLYPHS[c]
        for gy, line in enumerate(glyph):
            for gx, mark in enumerate(line):
                if mark == "#":
                    ink.add((x + gx, 1 + gy))
        x += len(glyph[0]) + 1
    for (ix, iy) in ink:
        for dx, dy in ((1, 0), (0, 1), (1, 1)):
            sx, sy = ix + dx, iy + dy
            if (sx, sy) not in ink and sy < 8 and sx < 31:
                strip[sy * 32 + sx] = SHADOW
    for (ix, iy) in ink:
        strip[iy * 32 + ix] = INK


def strip_of(sheet, row):
    strip = [0] * 256
    for c in range(4):
        cell = sheet.cell(row, FIRST_COLUMN + c)
        for y in range(8):
            for x in range(8):
                strip[y * 32 + c * 8 + x] = cell[y * 8 + x]
    return strip


def render():
    sheet = Sheet()
    free = free_tiles(sheet)
    known = {sheet.tile(t): t for t in range(sheet.count) if t not in free}
    made = {}
    tilemap = list(sheet.tilemap)
    for row, text in pc_rows().items():
        strip = strip_of(sheet, row)
        assert all(strip[y * 32 + 31] == BORDER for y in range(8)), f"row {row}: the border is not where it was"
        draw_row(strip, text)
        for c in range(4):
            data = tuple(strip[y * 32 + c * 8 + x] for y in range(8) for x in range(8))
            if data in known:
                index = known[data]
            elif data in made:
                index = made[data]
            else:
                index = made[data] = free.pop(0)
                sheet.put_tile(index, data)
            tilemap[row * COLUMNS + FIRST_COLUMN + c] = BANK | index
    png = io.BytesIO()
    sheet.image.save(png, format="PNG", optimize=False)
    return png.getvalue(), struct.pack("<%dH" % len(tilemap), *tilemap), made


def verify():
    """Draws the GBA's numbers the same way and counts the pixels that differ from the original's table."""
    sheet = Sheet()
    total = 0
    for row, text in GBA_ROWS.items():
        original = strip_of(sheet, row)
        drawn = list(original)
        draw_row(drawn, text)
        differ = [(i % 32, i // 32) for i in range(256) if drawn[i] != original[i]]
        total += len(differ)
        print(f"row {row:2d} {text!r:10}: {len(differ)} pixels differ {differ[:8]}")
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.verify:
        print("pixels that differ in all:", verify())
        return 0
    png, tilemap, made = render()
    if args.check:
        ok = os.path.exists(INFO_PC) and open(INFO_PC, "rb").read() == tilemap and os.path.exists(MENU_PC)
        if ok:
            a, b = Image.open(MENU_PC), Image.open(io.BytesIO(png))
            ok = a.size == b.size and a.mode == b.mode and a.getpalette() == b.getpalette() and a.tobytes() == b.tobytes()
        print("slot info box: " + ("up to date" if ok else "OUT OF DATE (run tools/pc_port/gen_slot_info_box.py)"))
        return 0 if ok else 1
    open(MENU_PC, "wb").write(png)
    open(INFO_PC, "wb").write(tilemap)
    print(f"wrote {os.path.relpath(MENU_PC, ROOT)}, {os.path.relpath(INFO_PC, ROOT)}: {len(made)} new tiles "
          f"({', '.join(str(t) for t in sorted(made.values()))}); rows {pc_rows()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
