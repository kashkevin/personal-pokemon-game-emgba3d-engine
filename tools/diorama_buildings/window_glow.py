#!/usr/bin/env python3
"""The windows of the modelled buildings, for their glow at night.

Project-owned (graphics overhaul, building details); export_gl.py calls it.
A window is found on its building's page, not drawn by hand: glass-coloured
pixels (blue or cyan, never the wall's own colour) on a face that stands
upright, gathered with the glare painted across them, in a pane a darker frame
closes in. Each pane found is grown out to its frame and written as runs of
page texels with a brightness (one per pane, so a street's windows are not all
alike), which the renderer keeps beside the page's palette slots
(gl_building_renderer.c: lit in the evening and at night, outdoors).

    python3 window_glow.py buildings.bin OUT_DIR PAGE...

draws the pages with their windows marked (yellow: a window; magenta: glass
that is not one), to look over what it finds. Those pictures are game art and
are not committed.
"""

import math
import struct
import sys
from collections import deque

# A pane: so many pixels at least and at most; not a stripe.
MIN_PIXELS = 3
MAX_PIXELS = 300
MIN_FILL = 0.5
MIN_FRAMED = 0.3
MAX_ASPECT = 6
# A glassy colour covering more of a wall than a pane does is the wall's own
# (a gym's blue).
WALL_PIXELS = 160
WALL_WIDTH = 24
WALL_HEIGHT = 20
# The frame: this much darker than the glass (the sum of the channels).
FRAME_DARKER = 120
GROW_BARRIER = 150
GROW_MARGIN = 2

STEPS = ((1, 0), (-1, 0), (0, 1), (0, -1))


def glassy(c):
    if c is None:
        return False
    r, g, b = c
    return b - r >= 40 and g - r >= 24 and g >= 112


# Dark glass (graphics overhaul phase 4): a window drawn as a dark blue pane
# in a light frame (Mossdeep's houses, Devon's slits in Rustboro), unlit by
# day, lit at night like the others. Stricter than light glass: a solid pane,
# framed nearly all round by something much lighter (the art's dark blue
# outlines and shadow bands are thin or unframed).
DARK_MIN_PIXELS = 12
DARK_MIN_FILL = 0.7
DARK_MIN_FRAMED = 0.6
FRAME_LIGHTER = 180


def dark_glassy(c):
    if c is None:
        return False
    r, g, b = c
    return b - r >= 30 and b - g >= 15 and 150 <= r + g + b <= 330


def near_white(c):
    return c is not None and min(c) >= 184


def wall_mask(w, h, triangles):
    """Page pixels of faces that stand upright (looking south, east or west).
    triangles: ((u, v, x, y, z) * 3) in page pixels and model tiles."""
    mask = bytearray(w * h)
    for a, b, c in triangles:
        ux, uy, uz = b[2] - a[2], b[3] - a[3], b[4] - a[4]
        vx, vy, vz = c[2] - a[2], c[3] - a[3], c[4] - a[4]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        n = math.sqrt(nx * nx + ny * ny + nz * nz)
        if n == 0.0 or (nz / n <= 0.9 and abs(nx / n) <= 0.9):
            continue
        us = (a[0], b[0], c[0])
        vs = (a[1], b[1], c[1])
        d = (vs[1] - vs[2]) * (us[0] - us[2]) + (us[2] - us[1]) * (vs[0] - vs[2])
        if abs(d) < 1e-9:
            continue
        x0 = max(0, int(math.floor(min(us))))
        x1 = min(w, int(math.ceil(max(us))))
        y0 = max(0, int(math.floor(min(vs))))
        y1 = min(h, int(math.ceil(max(vs))))
        for y in range(y0, y1):
            py = y + 0.5
            for x in range(x0, x1):
                px = x + 0.5
                l1 = ((vs[1] - vs[2]) * (px - us[2]) + (us[2] - us[1]) * (py - vs[2])) / d
                l2 = ((vs[2] - vs[0]) * (px - us[2]) + (us[0] - us[2]) * (py - vs[2])) / d
                if l1 >= -1e-6 and l2 >= -1e-6 and 1.0 - l1 - l2 >= -1e-6:
                    mask[y * w + x] = 1
    return mask


def _component(start, inside):
    out = [start]
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in STEPS:
            p = (x + dx, y + dy)
            if p not in seen and inside(p):
                seen.add(p)
                out.append(p)
                q.append(p)
    return out


def find_windows(w, h, colour, wall):
    """colour(x, y) -> (r, g, b) or None; wall: wall_mask(). Returns
    {"pixels": [(x, y)...], "shape": (x0, y0, w, h), "window": bool}
    for every piece of glass on the walls."""
    def at(x, y):
        return colour(x, y) if 0 <= x < w and 0 <= y < h and wall[y * w + x] else None

    glass = set()
    for y in range(h):
        for x in range(w):
            if glassy(at(x, y)):
                glass.add((x, y))
    # The wall's own colours.
    wallish = set()
    done = set()
    for p in sorted(glass, key=lambda q: (q[1], q[0])):
        if p in done:
            continue
        c = at(*p)
        piece = _component(p, lambda q, c=c: q in glass and at(*q) == c)
        done.update(piece)
        xs = [q[0] for q in piece]
        ys = [q[1] for q in piece]
        if len(piece) > WALL_PIXELS or max(xs) - min(xs) >= WALL_WIDTH or max(ys) - min(ys) >= WALL_HEIGHT:
            wallish.add(c)
    glass = {p for p in glass if at(*p) not in wallish}
    # Glare painted across the glass.
    part = set(glass)
    for y in range(h):
        for x in range(w):
            if (x, y) not in glass and near_white(at(x, y)):
                if sum((x + dx, y + dy) in glass for dx, dy in STEPS) >= 2:
                    part.add((x, y))

    pieces = []
    done = set()
    for p in sorted(part, key=lambda q: (q[1], q[0])):
        if p in done:
            continue
        piece = _component(p, lambda q: q in part)
        done.update(piece)
        inside = set(piece)
        lit = [sum(at(*q)) for q in piece if q in glass]
        lum = sum(lit) / max(1, len(lit))
        ring = set()
        for x, y in piece:
            for dx, dy in STEPS:
                if (x + dx, y + dy) not in inside:
                    ring.add((x + dx, y + dy))
        framed = sum(1 for q in ring if at(*q) is not None and sum(at(*q)) < lum - FRAME_DARKER)
        xs = [q[0] for q in piece]
        ys = [q[1] for q in piece]
        bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        pieces.append({
            "pixels": piece, "lum": lum,
            "shape": (min(xs), min(ys), bw, bh),
            "fill": len(piece) / float(bw * bh),
            "framed": framed / float(max(1, len(ring))),
        })

    def pane(f):
        x0, y0, bw, bh = f["shape"]
        return (MIN_PIXELS <= len(f["pixels"]) <= MAX_PIXELS and bw >= 2 and bh >= 2
                and f["fill"] >= MIN_FILL and f["framed"] >= MIN_FRAMED
                and bw <= MAX_ASPECT * bh and bh <= MAX_ASPECT * bw)

    def near(f, g):
        ax, ay, aw, ah = f["shape"]
        bx, by, bw, bh = g["shape"]
        return (ax - 2 <= bx + bw and bx - 2 <= ax + aw and ay - 2 <= by + bh and by - 2 <= ay + ah)

    for f in pieces:
        f["window"] = pane(f)
    pieces += dark_panes(w, h, at, {q for f in pieces for q in f["pixels"]})
    # A sliver of glass (the top row of a pane over a glazing bar) beside a
    # window is part of it.
    for f in pieces:
        if not f["window"] and len(f["pixels"]) >= 2 and f["framed"] >= MIN_FRAMED:
            x0, y0, bw, bh = f["shape"]
            if (bw == 1 or bh == 1) and max(bw, bh) <= 8 and any(g["window"] and near(f, g) for g in pieces if g is not f and pane(g)):
                f["window"] = True
    # Out to the frame: the pane's shaded edge and the bars' light sides,
    # unless that runs out over the wall.
    taken = set()
    for f in pieces:
        if not f["window"]:
            continue
        x0, y0, bw, bh = f["shape"]
        bx0, by0 = x0 - GROW_MARGIN, y0 - GROW_MARGIN
        bx1, by1 = x0 + bw - 1 + GROW_MARGIN, y0 + bh - 1 + GROW_MARGIN
        barrier = f["lum"] - GROW_BARRIER
        region = set(f["pixels"])
        q = deque(f["pixels"])
        leak = False
        while q and not leak:
            x, y = q.popleft()
            for dx, dy in STEPS:
                p = (x + dx, y + dy)
                if p in region:
                    continue
                c = at(*p)
                if c is None or sum(c) < barrier:
                    continue
                if not (bx0 < p[0] < bx1 and by0 < p[1] < by1):
                    leak = True
                    break
                region.add(p)
                q.append(p)
        if not leak:
            f["pixels"] = sorted(region - taken)
        taken.update(f["pixels"])
    return pieces


def dark_panes(w, h, at, taken):
    """The dark panes of a page (dark_glassy, framed by much lighter pixels):
    windows as they are, not grown. `taken`: pixels light glass claimed."""
    pieces = []
    done = set(taken)
    for y in range(h):
        for x in range(w):
            if (x, y) in done or not dark_glassy(at(x, y)):
                continue
            c = at(x, y)
            piece = _component((x, y), lambda q, c=c: q not in done and at(*q) == c)
            done.update(piece)
            inside = set(piece)
            xs = [q[0] for q in piece]
            ys = [q[1] for q in piece]
            bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
            if (len(piece) > WALL_PIXELS or bw >= WALL_WIDTH or bh >= WALL_HEIGHT
                    or len(piece) < DARK_MIN_PIXELS or bw < 3 or bh < 3
                    or bw > MAX_ASPECT * bh or bh > MAX_ASPECT * bw):
                continue
            fill = len(piece) / float(bw * bh)
            ring = set()
            for px, py in piece:
                for dx, dy in STEPS:
                    if (px + dx, py + dy) not in inside:
                        ring.add((px + dx, py + dy))
            lum = sum(c)
            framed = sum(1 for q in ring if at(*q) is not None and sum(at(*q)) >= lum + FRAME_LIGHTER)
            framed /= float(max(1, len(ring)))
            if fill < DARK_MIN_FILL or framed < DARK_MIN_FRAMED:
                continue
            pieces.append({"pixels": sorted(piece), "lum": lum, "shape": (min(xs), min(ys), bw, bh),
                           "fill": fill, "framed": framed, "window": True, "dark": True})
    return pieces


def brightness(page, shape):
    """How bright a window burns: 0.7 to 1, the same every run."""
    x, y = shape[0], shape[1]
    hsh = (page * 2654435761 ^ x * 73856093 ^ y * 19349663) & 0xFFFFFFFF
    hsh ^= hsh >> 13
    hsh = (hsh * 1274126177) & 0xFFFFFFFF
    return 0.7 + 0.3 * ((hsh >> 8) & 0xFF) / 255.0


def runs(page, pieces):
    """(y, x, length, level 1..255) for each row of each window."""
    out = []
    for f in pieces:
        if not f["window"]:
            continue
        level = max(1, min(255, int(round(255 * brightness(page, f["shape"])))))
        rows = {}
        for x, y in f["pixels"]:
            rows.setdefault(y, []).append(x)
        for y in sorted(rows):
            xs = sorted(rows[y])
            start = prev = xs[0]
            for x in xs[1:] + [None]:
                if x is not None and x == prev + 1:
                    prev = x
                    continue
                out.append((y, start, prev - start + 1, level))
                if x is not None:
                    start = prev = x
    return out


def page_triangles(blob, page):
    """Every triangle of every model drawn from page `page` of buildings.bin,
    as ((u, v, x, y, z) * 3) in page pixels."""
    (pages, models, page_models, placements, height_bytes, masks, vertices,
     variants, _) = struct.unpack_from("<HHHHHHIHH", blob, 4)
    head = 4 + struct.calcsize("<HHHHHHIHH")
    page_table = [struct.unpack_from("<HHI", blob, head + 8 * i) for i in range(pages)]
    body = head + 8 * pages
    vertex_start = page_table[0][2] - 24 * vertices
    tris = []
    for i in range(page_models):
        model, model_page, ox, oy = struct.unpack_from("<HHhh", blob, body + 16 * models + 8 * i)
        if model_page != page:
            continue
        first, count = struct.unpack_from("<II", blob, body + 16 * model + 4)
        for k in range(first, first + count - 2, 3):
            tri = []
            for j in range(3):
                x, y, z, u, v, shade = struct.unpack_from("<6f", blob, vertex_start + 24 * (k + j))
                tri.append((u + ox, v + oy, x, y, z))
            tris.append(tuple(tri))
    return tris


def main():
    from PIL import Image

    import export_gl

    blob = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    pages = struct.unpack_from("<H", blob, 4)[0]
    head = 4 + struct.calcsize("<HHHHHHIHH")
    for page in map(int, sys.argv[3:]):
        if page >= pages:
            continue
        w, h, offset = struct.unpack_from("<HHI", blob, head + 8 * page)
        texels = struct.unpack_from("<%dH" % (w * h), blob, offset)

        def colour(x, y):
            v = texels[export_gl.texel_offset(x, y, w)]
            if not v & 1:
                return None
            return (((v >> 11) & 31) * 8, ((v >> 6) & 31) * 8, ((v >> 1) & 31) * 8)

        wall = wall_mask(w, h, page_triangles(blob, page))
        pieces = find_windows(w, h, colour, wall)
        img = Image.new("RGB", (w, h), (40, 40, 48))
        px = img.load()
        for y in range(h):
            for x in range(w):
                c = colour(x, y)
                if c is not None:
                    px[x, y] = c if wall[y * w + x] else tuple(v // 2 for v in c)
        for f in pieces:
            for x, y in f["pixels"]:
                px[x, y] = (255, 200, 60) if f["window"] else (255, 0, 255)
        img.resize((w * 3, h * 3), Image.NEAREST).save("%s/windows-%03d.png" % (out, page))
        print(page, sum(f["window"] for f in pieces), "windows,", len(runs(page, pieces)), "runs")


if __name__ == "__main__":
    main()
