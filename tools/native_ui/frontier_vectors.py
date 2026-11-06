#!/usr/bin/env python3
"""MS-108: checks the Battle Frontier's vector pieces (src/native_ui/ui_transition_vectors.c) against the game's art.

    python3 tools/native_ui/frontier_vectors.py [--preview OUT_DIR [--scale 6]]

Reads the C tables (each piece: shapes painted in order, each a colour where its circles' conditions hold - inside,
r > 0, or outside, r < 0 - with a line of the shape's line colour along an edge where the circle gives a width), samples
each piece at the art's pixel middles as the game's UiArt_MatchPiece does, and compares the result with the art itself
(graphics/battle_transitions/: frontier_logo.png on frontier_logo.bin, frontier_logo_center.png on its .bin,
frontier_logo_circles.png's three 64 x 64 frames). Prints, per piece, how many coloured pixels differ, of which kinds,
and how far the differing pixels lie from the nearest edge of the piece (all of them should lie within a pixel: the
art's own anti-aliasing and ties, not a wrong shape). The game accepts a piece for its art when at most 2 % differ.
--preview writes each piece drawn smooth at SCALE (16 samples a pixel) beside the art, coloured with the art's palette.
Exit status 1 if a piece differs from its art by more than 2 % or a differing pixel lies a pixel or more from every
edge.
"""
import argparse
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GFX = os.path.join(ROOT, "graphics", "battle_transitions")
SRC = os.path.join(ROOT, "src", "native_ui", "ui_transition_vectors.c")

# table: (art, frame of the circles' sheet or None)
PIECES = {
    "sLogo": ("frontier_logo", None),
    "sCentre": ("frontier_logo_center", None),
    "sCircleTop": ("frontier_logo_circles", 0),
    "sCircleLeft": ("frontier_logo_circles", 1),
    "sCircleRight": ("frontier_logo_circles", 2),
}


def tables():
    text = open(SRC).read()
    text = text.replace("NONE", "{0, 0, 0, 0}")
    out = {}
    for name in PIECES:
        body = re.search(r"static const struct UiArtShape %s\[\] = \{(.*?)\n\};" % name, text, re.S).group(1)
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        shapes = []
        for m in re.finditer(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*\{(.*?)\}\s*\}\s*,", body, re.S):
            circles = [tuple(float(v.rstrip("f")) for v in c.split(","))
                       for c in re.findall(r"\{([^{}]*)\}", m.group(3))]
            shapes.append((int(m.group(1)), int(m.group(2)), circles))
        out[name] = shapes
    return out


def indices(img):
    a = np.array(img)
    if img.mode == "L":
        a = a // 17
    return a.astype(np.uint8)


def art(name, frame):
    """(colour indices, the PNG's palette) of the picture as the game shows it: a map's pixels, or a sprite frame's"""
    img = Image.open(os.path.join(GFX, name + ".png"))
    a = indices(img)
    pal = img.getpalette() or [0] * 768
    if frame is not None:
        return a[frame * 64:(frame + 1) * 64, :], pal
    th, tw = a.shape[0] // 8, a.shape[1] // 8
    tiles = [a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] for ty in range(th) for tx in range(tw)]
    data = open(os.path.join(GFX, name + ".bin"), "rb").read()
    out = np.zeros((256, 256), np.uint8)
    for i in range(min(len(data) // 2, 1024)):
        e = struct.unpack_from("<H", data, i * 2)[0]
        if (e & 0x3FF) >= len(tiles):
            continue
        tile = tiles[e & 0x3FF]
        if e & 0x400:
            tile = tile[:, ::-1]
        if e & 0x800:
            tile = tile[::-1, :]
        out[(i // 32) * 8:(i // 32) * 8 + 8, (i % 32) * 8:(i % 32) * 8 + 8] = tile
    return out, pal


def paint(shapes, X, Y, cover=False, k=1.0):
    """the colour at each point (the game's ColourAt); with cover, each shape's (colour, line colour, core, halo)
    coverage over one output pixel (k output pixels an art pixel), as the shader paints them"""
    out = np.zeros(X.shape, np.uint8)
    layers = []
    for colour, line, circles in shapes:
        core = np.ones(X.shape, bool if not cover else float)
        halo = np.ones(X.shape, bool if not cover else float)
        for cx, cy, r, w in circles:
            if r == 0:
                continue
            half = w / 2 if line else 0.0
            d = np.hypot(X - cx, Y - cy)
            if not cover:
                core &= (d < r - half) if r > 0 else (d >= -r + half)
                halo &= (d < r + half) if r > 0 else (d >= -r - half)
            else:
                if r > 0:
                    core *= np.clip(0.5 - (d - (r - half)) * k, 0, 1)
                    halo *= np.clip(0.5 - (d - (r + half)) * k, 0, 1)
                else:
                    core *= np.clip(0.5 + (d - (-r + half)) * k, 0, 1)
                    halo *= np.clip(0.5 + (d - (-r - half)) * k, 0, 1)
        if cover:
            layers.append((colour, line, core, halo if line else core))
        else:
            out[halo & ~core] = line
            out[core] = colour
    return layers if cover else out


def edge_distance(shapes, X, Y):
    """each point's distance from the nearest edge of the piece (each circle, and a line's two sides)"""
    best = np.full(X.shape, 1e9)
    for colour, line, circles in shapes:
        for cx, cy, r, w in circles:
            if r == 0:
                continue
            d = np.hypot(X - cx, Y - cy)
            for off in ((-w / 2, w / 2) if line and w else (0.0,)):
                best = np.minimum(best, np.abs(d - (abs(r) + off)))
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--preview", metavar="OUT_DIR")
    ap.add_argument("--scale", type=int, default=6)
    args = ap.parse_args()
    ok = True
    for name, shapes in tables().items():
        picture, pal = art(*PIECES[name])
        h, w = picture.shape
        ys, xs = np.mgrid[0:h, 0:w]
        X, Y = xs + 0.5, ys + 0.5
        got = paint(shapes, X, Y)
        coloured = int(((got != 0) | (picture != 0)).sum())
        bad = got != picture
        far = float(edge_distance(shapes, X[bad], Y[bad]).max()) if bad.any() else 0.0
        kinds = {}
        for a, b in zip(picture[bad].tolist(), got[bad].tolist()):
            kinds[(a, b)] = kinds.get((a, b), 0) + 1
        print("%-13s %d shapes: %d of %d coloured pixels differ (%.2f %%), the farthest %.2f px from an edge; "
              "art -> piece: %s" % (name, len(shapes), bad.sum(), coloured, 100.0 * bad.sum() / coloured, far,
                                    ", ".join("%d->%d %d" % (a, b, n) for (a, b), n in
                                              sorted(kinds.items(), key=lambda t: -t[1]))))
        if bad.sum() * 50 > coloured or far >= 1.0:
            ok = False
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            s = args.scale
            rgb = np.array(pal[:48], np.float64).reshape(16, 3)
            ys2, xs2 = np.mgrid[0:h * s, 0:w * s]
            Xs, Ys = (xs2 + 0.5) / s, (ys2 + 0.5) / s
            col = np.zeros((h * s, w * s, 3))
            alpha = np.zeros((h * s, w * s))
            for colour, line, core, halo in paint(shapes, Xs, Ys, cover=True, k=s):
                col = col * (1 - halo)[..., None] + rgb[line][None, None] * (halo - core)[..., None] \
                    + rgb[colour][None, None] * core[..., None]
                alpha = alpha * (1 - halo) + halo
            back = np.array([40, 40, 48], np.float64)
            smooth = col + back * (1 - alpha)[..., None]
            gba = rgb[picture]
            gba[picture == 0] = back
            gba = np.repeat(np.repeat(gba, s, 0), s, 1)
            both = np.concatenate([gba, np.full((h * s, 8, 3), 255.0), smooth], 1)
            Image.fromarray(np.clip(both, 0, 255).astype(np.uint8)).save(os.path.join(args.preview, name + ".png"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
