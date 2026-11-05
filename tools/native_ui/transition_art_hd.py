#!/usr/bin/env python3
"""MS-108, the offline step: the battle transitions' own GBA art (the Battle Frontier's logo and circles, the
legendaries', Regis' and the teams' patterns) as smooth high-resolution label maps, from the art itself.

    python3 tools/native_ui/transition_art_hd.py OUT_DIR [--scale 8] [--only NAME,...] [--root .]

For each picture (a BG0 tileset and tilemap, or the circles' sprite sheet) every palette slot's pixels (and the empty
background's) become a smooth membership field (nearest-upscaled, blurred: an approximate distance field); each output
pixel takes the slot with the most, with the runner-up across a one-pixel edge. The pixel stairs become the circles,
ellipses and straight strokes the art draws, with no seam between colours; the shapes, proportions and colours stay the
GBA's. Written per picture:

  NAME.slots.png    RGB: R = the winning slot (palette bank << 4 | colour index), G = the runner-up, B = the blend
                    (255: all R). A renderer colours R and G from the game's own palette of that frame
                    (gPlttBufferFaded: the patterns' palette cycling and the fades stay the game's) and mixes them by B.
  NAME.preview.png  the same coloured with the picture's own palette file, beside the GBA picture at the same size, for
                    a look by eye.

Only reads graphics/battle_transitions/. Not used by the game yet: the native UI's transition screen
(src/native_ui/screens/ui_transition.c) would draw these at the window's resolution with BG0's scroll and line offsets,
the GBA BG0 / circle sprites hidden as it already does for the big Poke Ball (docs of the app,
battle-fx-past-package-20261105.md, MS-108).
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# name: (tileset, tilemap or None, palette (None: the PNG's own; or a .pal and its first bank), map width in tiles,
#        crop in GBA pixels (x, y, w, h))
PICTURES = {
    "frontier_logo": ("frontier_logo.png", "frontier_logo.bin", None, 32, (0, 0, 240, 160)),
    "frontier_logo_center": ("frontier_logo_center.png", "frontier_logo_center.bin", None, 32, (0, 0, 240, 160)),
    "frontier_logo_circles": ("frontier_logo_circles.png", None, None, 0, (0, 0, 64, 192)),
    "kyogre": ("kyogre.png", "kyogre.bin", "kyogre_pt1.pal", 32, (0, 0, 240, 160)),
    "groudon": ("groudon.png", "groudon.bin", "groudon_pt1.pal", 32, (0, 0, 240, 160)),
    "rayquaza": ("rayquaza.png", "rayquaza.bin", "rayquaza.pal", 32, (0, 0, 256, 320)),
    "regice": ("regis.png", "regice.bin", "regice.pal", 32, (0, 0, 240, 160)),
    "registeel": ("regis.png", "registeel.bin", "registeel.pal", 32, (0, 0, 240, 160)),
    "regirock": ("regis.png", "regirock.bin", "regirock.pal", 32, (0, 0, 240, 160)),
    "team_aqua": ("team_aqua.png", "team_aqua.bin", "evil_team.pal", 32, (0, 0, 240, 160)),
    "team_magma": ("team_magma.png", "team_magma.bin", "evil_team.pal", 32, (0, 0, 240, 160)),
}


def jasc(path):
    v = open(path).read().split()
    n = int(v[2])
    vals = list(map(int, v[3:3 + 3 * n]))
    return [tuple(vals[i * 3:i * 3 + 3]) for i in range(n)]


def indices(img):
    a = np.array(img)
    if img.mode == "L":     # a 4-bit greyscale PNG: gbagfx reads the grey levels as the colour indices
        a = a // 17
    return a.astype(np.uint8)


def picture(gfx, tileset, tilemap, mapw):
    """(colour index, palette bank) of every GBA pixel of the picture."""
    img = Image.open(os.path.join(gfx, tileset))
    a = indices(img)
    if tilemap is None:
        return a, np.zeros_like(a), img
    th, tw = a.shape[0] // 8, a.shape[1] // 8
    tiles = [a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] for ty in range(th) for tx in range(tw)]
    data = open(os.path.join(gfx, tilemap), "rb").read()
    n = len(data) // 2
    maph = n // mapw
    idx = np.zeros((maph * 8, mapw * 8), np.uint8)
    bank = np.zeros_like(idx)
    for i in range(n):
        e = struct.unpack_from("<H", data, i * 2)[0]
        t = e & 0x3FF
        if t >= len(tiles):
            continue
        tile = tiles[t]
        if e & 0x400:
            tile = tile[:, ::-1]
        if e & 0x800:
            tile = tile[::-1, :]
        y, x = (i // mapw) * 8, (i % mapw) * 8
        idx[y:y + 8, x:x + 8] = tile
        bank[y:y + 8, x:x + 8] = e >> 12
    return idx, bank, img


def label_maps(idx, bank, scale, soft=0.55):
    """R, G, B planes of NAME.slots.png (see the module's text)."""
    h, w = idx.shape
    slot = np.where(idx == 0, 0, (bank.astype(np.int32) << 4) | idx).astype(np.int32)
    keys = sorted(set(slot.flatten().tolist()))
    fields = []
    for k in keys:
        m = (slot == k).astype(np.uint8) * 255
        big = Image.fromarray(m).resize((w * scale, h * scale), Image.NEAREST).filter(ImageFilter.GaussianBlur(scale * soft))
        fields.append(np.asarray(big, np.float32) / 255.0)
    F = np.stack(fields)
    order = np.argsort(-F, axis=0)
    a, b = order[0], order[1] if len(keys) > 1 else order[0]
    fa = np.take_along_axis(F, a[None], 0)[0]
    fb = np.take_along_axis(F, b[None], 0)[0]
    t = np.clip((fa - fb) * 4.0, 0, 1) * 0.5 + 0.5
    K = np.array(keys, np.int32)
    return K[a].astype(np.uint8), K[b].astype(np.uint8), (t * 255).astype(np.uint8)


def colours(pal_png, pal_file, keys):
    """slot -> RGB for the preview (the picture's own palette; the game's palette at run time)."""
    if pal_file is None:
        p = pal_png.getpalette() or [0] * 768
        return {k: tuple(p[(k & 15) * 3:(k & 15) * 3 + 3]) for k in keys}
    p = jasc(pal_file)
    out = {}
    for k in keys:
        i = (k >> 4) * 16 + (k & 15)
        out[k] = p[i] if i < len(p) else p[(k & 15) % len(p)]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--only", default="")
    ap.add_argument("--root", default=ROOT)
    o = ap.parse_args()
    gfx = os.path.join(o.root, "graphics", "battle_transitions")
    os.makedirs(o.out, exist_ok=True)
    only = set(x for x in o.only.split(",") if x)
    for name, (tileset, tilemap, pal, mapw, crop) in PICTURES.items():
        if only and name not in only:
            continue
        idx, bank, img = picture(gfx, tileset, tilemap, mapw)
        x0, y0, w, h = crop
        idx, bank = idx[y0:y0 + h, x0:x0 + w], bank[y0:y0 + h, x0:x0 + w]
        r, g, b = label_maps(idx, bank, o.scale)
        Image.fromarray(np.dstack([r, g, b])).save(os.path.join(o.out, name + ".slots.png"))
        keys = sorted(set(r.flatten().tolist()) | set(g.flatten().tolist()))
        col = colours(img, os.path.join(gfx, pal) if pal else None, keys)
        lut = np.zeros((256, 3), np.float32)
        for k, c in col.items():
            lut[k] = c
        lut[0] = (0, 0, 0)
        t = b.astype(np.float32)[..., None] / 255.0
        hd = (lut[r] * t + lut[g] * (1 - t)).astype(np.uint8)
        slot = np.where(idx == 0, 0, (bank.astype(np.int32) << 4) | idx)
        gba = Image.fromarray(lut[slot].astype(np.uint8)).resize((w * o.scale, h * o.scale), Image.NEAREST)
        prev = Image.new("RGB", (w * o.scale * 2 + 8, h * o.scale), (40, 40, 40))
        prev.paste(gba, (0, 0))
        prev.paste(Image.fromarray(hd), (w * o.scale + 8, 0))
        prev.save(os.path.join(o.out, name + ".preview.png"))
        print("%s: %d slots, %dx%d" % (name, len(keys), w * o.scale, h * o.scale))
    return 0


if __name__ == "__main__":
    sys.exit(main())
