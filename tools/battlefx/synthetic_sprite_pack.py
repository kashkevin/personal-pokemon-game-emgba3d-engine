#!/usr/bin/env python3
"""A synthetic battle sprite pack ("EBSP" version 1, include/platform/battle_sprite_library.h) for tests
that need the pack's code paths without the private pack: for each library target id given, a front and a
back facing - four frames of a plain rectangle, 80 x 80, its feet at (40, 76), the front red and the back
blue, each with a yellow mark near its top and a shiny palette. Plain shapes, no art; the pack's ids are
the sprite library's target ids (src/platform/bsl_species_map.h: "kecleon", "zigzagoon", ...).

    python3 tools/battlefx/synthetic_sprite_pack.py OUT.ebsp kecleon zigzagoon
    PKMN_BATTLE_SPRITES_PACK=OUT.ebsp ./pokeemerald-mp-diorama-bfxtest ...

Used for KNOWN_ISSUES S7 (a move's own picture of a Pokemon drawn from the pack): with
tools/battlefx/gallery.sh and PKMN_BATTLE_SPRITES_TRACE, then move_copy_trace.py on the trace.
"""
import struct
import sys
import zlib

WIDTH, HEIGHT, FRAMES, STEP_MS = 80, 80, 4, 120
NORMAL = [(0, 0, 0, 0), (40, 40, 40, 255), (220, 60, 60, 255), (60, 160, 220, 255), (240, 220, 80, 255)]
SHINY = [(0, 0, 0, 0), (40, 40, 40, 255), (60, 200, 90, 255), (200, 120, 220, 255), (250, 250, 250, 255)]


def frames(back):
    """The facing's strip: FRAMES frames side by side, palette indices (0 see-through)."""
    stride = WIDTH * FRAMES
    out = bytearray(stride * HEIGHT)
    for f in range(FRAMES):
        top = 8 + 2 * f                         # it bobs as it animates
        for y in range(top, 76):
            for x in range(16, 64):
                edge = x in (16, 63) or y in (top, 75)
                colour = 1 if edge else (3 if back else 2)
                if not edge and abs(x - 40) < 8 and y < top + 20:
                    colour = 4
                out[y * stride + f * WIDTH + x] = colour
    return bytes(out)


def build(ids):
    strings = bytearray()

    def string(text):
        offset = len(strings)
        strings.extend(text.encode() + b"\0")
        return offset

    info = string("synthetic test pack (tools/battlefx/synthetic_sprite_pack.py): plain shapes, no art")
    u16, palette, blobs, facings, targets = [], [], [], [], []
    data = bytearray()
    for target in sorted(ids):
        facing_of = []
        for back in (0, 1):
            raw = frames(back)
            packed = zlib.compress(raw, 9)
            blobs.append((len(data), len(packed), len(raw), zlib.crc32(raw) & 0xFFFFFFFF))
            data.extend(packed)
            sequence = len(u16)
            u16 += list(range(FRAMES))
            durations = len(u16)
            u16 += [STEP_MS] * FRAMES
            normal = len(palette)
            palette += NORMAL
            shiny = len(palette)
            palette += SHINY
            source = string("synthetic/%s/%s" % (target, "back" if back else "front"))
            # w, h, unique frames, steps, anchor x, y, loop ms, sequence, durations, palettes, colours,
            # flags (animated, a real shiny), blob, source
            facings.append((WIDTH, HEIGHT, FRAMES, FRAMES, 40, 76, STEP_MS * FRAMES, sequence, durations, normal,
                            shiny, len(NORMAL), 1 | 2, len(blobs) - 1, source))
            facing_of.append(len(facings) - 1)
        targets.append((string(target), string("SPECIES_" + target.upper()), facing_of[0], facing_of[1], -1, -1))
    header = 64
    off_targets = header
    off_facings = off_targets + 16 * len(targets)
    off_blobs = off_facings + 48 * len(facings)
    off_strings = off_blobs + 16 * len(blobs)
    off_u16 = (off_strings + len(strings) + 1) & ~1
    off_palette = (off_u16 + 2 * len(u16) + 3) & ~3
    off_data = off_palette + 4 * len(palette)
    pack = bytearray(off_data + len(data))
    struct.pack_into("<4sHH14I", pack, 0, b"EBSP", 1, header, len(targets), len(facings), len(blobs),
                     off_targets, off_facings, off_blobs, off_strings, len(strings), off_u16, 2 * len(u16),
                     off_palette, 4 * len(palette), info, 0)
    for k, record in enumerate(targets):
        struct.pack_into("<IIhhhh", pack, off_targets + 16 * k, *record)
    for k, record in enumerate(facings):
        struct.pack_into("<HHHHhhIIIIIHHII4x", pack, off_facings + 48 * k, *record)
    for k, (offset, size, raw, crc) in enumerate(blobs):
        struct.pack_into("<IIII", pack, off_blobs + 16 * k, off_data + offset, size, raw, crc)
    pack[off_strings:off_strings + len(strings)] = strings
    struct.pack_into("<%dH" % len(u16), pack, off_u16, *u16)
    for k, rgba in enumerate(palette):
        struct.pack_into("<BBBB", pack, off_palette + 4 * k, *rgba)
    pack[off_data:] = data
    return bytes(pack)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    pack = build(sys.argv[2:])
    with open(sys.argv[1], "wb") as f:
        f.write(pack)
    print("%s: %d targets, %d bytes" % (sys.argv[1], len(sys.argv) - 2, len(pack)))


if __name__ == "__main__":
    main()
