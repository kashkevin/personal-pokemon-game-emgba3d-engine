#!/usr/bin/env python3
"""The windows of the modelled buildings, for their glow at night.

Project-owned (graphics overhaul, building details); export_gl.py calls it.
A window is found on its building's page, not drawn by hand: glass-coloured
pixels (blue or cyan, never the wall's own colour) on a face that stands
upright, gathered with the glare painted across them, in a pane a darker frame
closes in; looked for over the page and in each building's own drawing, as
a colour one building's walls are drawn in can be another's glass
(find_page_windows). Each pane found is grown out to its frame and to the
other panes its frame closes in, and written as runs of page texels with a
brightness (one per window, so a street's windows are not all alike), which
the renderer keeps beside the page's palette slots (gl_building_renderer.c:
lit in the evening and at night, outdoors).

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


def _glass(at, rect):
    """The glassy pixels of `rect` and the glassy colours that are a wall's
    own there: a colour one piece of which covers more than a pane does."""
    rx0, ry0, rx1, ry1 = rect
    glass = set()
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
            if glassy(at(x, y)):
                glass.add((x, y))
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
    return glass, wallish


def find_windows(w, h, colour, wall, rect=None):
    """colour(x, y) -> (r, g, b) or None; wall: wall_mask(). Returns
    {"pixels": [(x, y)...], "shape": (x0, y0, w, h), "window": bool}
    for every piece of glass on the walls. With `rect` (x0, y0, x1, y1) only
    that part of the page is looked at: one model's drawing (model_rects), so
    a wall's own colour is the wall's of that building, not of any building
    on its page (V32)."""
    rx0, ry0, rx1, ry1 = rect if rect is not None else (0, 0, w, h)

    def at(x, y):
        return colour(x, y) if rx0 <= x < rx1 and ry0 <= y < ry1 and wall[y * w + x] else None

    glass, wallish = _glass(at, (rx0, ry0, rx1, ry1))
    glass = {p for p in glass if at(*p) not in wallish}
    # Glare painted across the glass.
    part = set(glass)
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
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
    pieces += dark_panes(w, h, at, {q for f in pieces for q in f["pixels"]}, (rx0, ry0, rx1, ry1))
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


def dark_panes(w, h, at, taken, rect=None):
    """The dark panes of a page (dark_glassy, framed by much lighter pixels):
    windows as they are, not grown. `taken`: pixels light glass claimed."""
    rx0, ry0, rx1, ry1 = rect if rect is not None else (0, 0, w, h)
    pieces = []
    done = set(taken)
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
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


def model_rects(blob, page):
    """The part of page `page` of buildings.bin each model's drawing takes
    (x0, y0, x1, y1): the extent of its triangles' texture coordinates, within
    the cells it stands on."""
    (pages, models, page_models, placements, height_bytes, masks, vertices,
     variants, _) = struct.unpack_from("<HHHHHHIHH", blob, 4)
    head = 4 + struct.calcsize("<HHHHHHIHH")
    page_table = [struct.unpack_from("<HHI", blob, head + 8 * i) for i in range(pages)]
    w, h = page_table[page][0], page_table[page][1]
    body = head + 8 * pages
    vertex_start = page_table[0][2] - 24 * vertices
    rects = []
    for i in range(page_models):
        model, model_page, ox, oy = struct.unpack_from("<HHhh", blob, body + 16 * models + 8 * i)
        if model_page != page:
            continue
        cells_w, cells_h = struct.unpack_from("<BB", blob, body + 16 * model)
        first, count = struct.unpack_from("<II", blob, body + 16 * model + 4)
        us, vs = [], []
        for k in range(first, first + count):
            u, v = struct.unpack_from("<2f", blob, vertex_start + 24 * k + 12)
            us.append(u + ox)
            vs.append(v + oy)
        if not us:
            continue
        # its drawing, not what its art has appended past it (a real roof's
        # swatch, the rows of the cells south of it)
        rect = (max(0, ox, int(math.floor(min(us)))), max(0, oy, int(math.floor(min(vs)))),
                min(w, ox + 16 * cells_w, int(math.ceil(max(us)))),
                min(h, oy + 16 * cells_h, int(math.ceil(max(vs)))))
        if rect[0] < rect[2] and rect[1] < rect[3] and rect not in rects:
            rects.append(rect)
    return rects


# A window's other panes (V32): past a glazing bar of the frame's colour, at
# most this many pixels, within this reach of the pane they are found from.
BAR_WIDTH = 2
PANE_REACH = 12


def lit_glass(c):
    """Glass a window's light shows through: light or dark glass, or a pane
    white with the sky's reflection."""
    return glassy(c) or near_white(c) or dark_glassy(c)


# Pale glass (V32): a grey blue that walls are shaded in too (the band along
# a wall's foot, a door's lower panel, at the Battle Frontier); blue glass is
# bluer over its red than this.
PALE_BLUE = 96


def pale(c):
    return c[2] - c[0] < PALE_BLUE


# A frame closes round a window within this many pixels of its pane (as
# dimensional.window_rects has it for the windows it sets back).
FRAME_REACH = 8


def frame_region(pixels, at):
    """What a frame closes in round a pane: everything the pane reaches
    without crossing a colour much darker than its lightest pixel (the frame
    and its bars), within FRAME_REACH pixels of it and on the wall; None
    where that runs on (a wall's shading, glass no frame holds)."""
    inside = set(pixels)
    barrier = max(sum(at(*q)) for q in inside) - GROW_BARRIER
    xs = [q[0] for q in inside]
    ys = [q[1] for q in inside]
    box = (min(xs) - FRAME_REACH, min(ys) - FRAME_REACH, max(xs) + FRAME_REACH, max(ys) + FRAME_REACH)
    region = set(inside)
    todo = list(inside)
    while todo:
        x, y = todo.pop()
        for dx, dy in STEPS:
            q = (x + dx, y + dy)
            if q in region:
                continue
            c = at(*q)
            if c is not None and sum(c) < barrier:
                continue
            if c is None or not (box[0] <= q[0] <= box[2] and box[1] <= q[1] <= box[3]):
                return None
            region.add(q)
            todo.append(q)
    return region


def enclosed(pixels, at):
    """Whether a frame drawn in one colour closes a pane in, as
    dimensional.window_rects finds the windows it sets back: the commonest
    colour beside it much darker than its lightest pixel goes round all the
    pane reaches within FRAME_REACH pixels, on the wall. A wall's shading
    runs on past it, between pilasters and mouldings of other colours."""
    inside = set(pixels)
    frame = _frame_of(inside, at, max(sum(at(*q)) for q in inside))
    if frame is None:
        return False
    xs = [q[0] for q in inside]
    ys = [q[1] for q in inside]
    box = (min(xs) - FRAME_REACH, min(ys) - FRAME_REACH, max(xs) + FRAME_REACH, max(ys) + FRAME_REACH)
    region = set(inside)
    todo = list(inside)
    while todo:
        x, y = todo.pop()
        for dx, dy in STEPS:
            q = (x + dx, y + dy)
            if q in region:
                continue
            if not (box[0] <= q[0] <= box[2] and box[1] <= q[1] <= box[3]):
                return False
            c = at(*q)
            if c is None:
                return False
            if c != frame:
                region.add(q)
                todo.append(q)
    return True


def shop_window(piece, at):
    """The glass of a window its frame closes round though the piece found
    is no pane by its framing - a shop's window with a blind and a stripe
    across it (Mauville's Game Corner, Lilycove's stalls under their
    awnings): every pixel of glass, white or glass-coloured, inside the
    frame, if that is a window at least 3 x 3 and mostly glass. None if
    not."""
    x0, y0, bw, bh = piece["shape"]
    if not (MIN_PIXELS <= len(piece["pixels"]) <= MAX_PIXELS and bw >= 2 and bh >= 2
            and piece["fill"] >= MIN_FILL and bw <= MAX_ASPECT * bh and bh <= MAX_ASPECT * bw):
        return None
    region = frame_region(piece["pixels"], at)
    if region is None:
        return None
    xs = [q[0] for q in region]
    ys = [q[1] for q in region]
    if max(xs) - min(xs) < 2 or max(ys) - min(ys) < 2:
        return None
    glass = [q for q in region if lit_glass(at(*q))]
    return glass if len(glass) >= 0.5 * len(region) else None


def _frame_of(pixels, at, lum):
    """The colour the frame round a pane is drawn in: the commonest colour
    beside it much darker than the glass, or None."""
    inside = set(pixels)
    ring = {}
    for (x, y) in pixels:
        for dx, dy in STEPS:
            q = (x + dx, y + dy)
            if q in inside:
                continue
            c = at(*q)
            if c is not None and sum(c) < lum - GROW_BARRIER:
                ring[c] = ring.get(c, 0) + 1
    return max(ring, key=ring.get) if ring else None


def whole_window(piece, at):
    """The pixels of a window's panes: the pane found and every pane its
    frame closes in beside it, past glazing bars of the frame's colour (a
    sash window's paler upper panes, Slateport's), the bars left dark."""
    pixels = set(piece["pixels"])
    frame = _frame_of(pixels, at, piece["lum"])
    if frame is None:
        return sorted(pixels)
    xs = [q[0] for q in pixels]
    ys = [q[1] for q in pixels]
    box = (min(xs) - PANE_REACH, min(ys) - PANE_REACH, max(xs) + PANE_REACH, max(ys) + PANE_REACH)
    todo = [set(pixels)]
    while todo:
        pane = todo.pop()
        px = [q[0] for q in pane]
        py = [q[1] for q in pane]
        x0, x1, y0, y1 = min(px), max(px) + 1, min(py), max(py) + 1
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        for (x, y, dx, dy) in ((cx, y1, 0, 1), (cx, y0 - 1, 0, -1), (x1, cy, 1, 0), (x0 - 1, cy, -1, 0)):
            bar = 0
            while bar < BAR_WIDTH and at(x, y) == frame:
                x, y, bar = x + dx, y + dy, bar + 1
            c = at(x, y)
            if bar == 0 or c is None or c == frame or (x, y) in pixels:
                continue
            region, ok, flood = {(x, y)}, True, [(x, y)]
            while flood and ok:
                fx, fy = flood.pop()
                for sx, sy in STEPS:
                    q = (fx + sx, fy + sy)
                    if q in region:
                        continue
                    qc = at(*q)
                    if qc == frame:
                        continue
                    if qc is None or not (box[0] <= q[0] <= box[2] and box[1] <= q[1] <= box[3]):
                        ok = False
                        break
                    region.add(q)
                    flood.append(q)
            if not ok or len(region) > 4 * len(pane) + 16 or not region.isdisjoint(pixels):
                continue
            if sum(1 for q in region if lit_glass(at(*q))) < 0.5 * len(region):
                continue
            pixels |= region
            todo.append(region)
    return sorted(pixels)


# Windows drawn in stone (V32: Sootopolis's houses): panes of grey in a dark
# blue outline, an arch over straight sides with a white sill under it, and
# glints of white in the panes. The same greys draw the town's emblems (a
# diamond, no glint in it) and its doors (square, on the wall's own colour).
STONE_REACH = 12
STONE_MIN_PIXELS = 30


def stone_grey(c):
    return c is not None and max(c) - min(c) <= 24 and sum(c) <= 480


def _stone_region(start, at):
    """The grey pixels from `start` to the outline (dark glass's blue) and
    the white of the glints and the sill, or None when they reach anything
    else (the wall) or run past STONE_REACH."""
    region = {start}
    todo = [start]
    while todo:
        x, y = todo.pop()
        for dx, dy in STEPS:
            q = (x + dx, y + dy)
            if q in region:
                continue
            if abs(q[0] - start[0]) > STONE_REACH or abs(q[1] - start[1]) > STONE_REACH:
                return None
            c = at(*q)
            if c is None:
                return None
            if dark_glassy(c) or near_white(c):
                continue
            if not stone_grey(c):
                return None
            region.add(q)
            todo.append(q)
    return region


def stone_panes(at, rect, taken):
    """The windows drawn in stone in `rect` (x0, y0, x1, y1), as window
    pieces: every grey pixel inside the outline and the glints."""
    rx0, ry0, rx1, ry1 = rect
    pieces = []
    done = set(taken)
    for y in range(ry0, ry1):
        for x in range(rx0, rx1):
            if (x, y) in done or not near_white(at(x, y)):
                continue
            greys = [(x + dx, y + dy) for dx, dy in STEPS if stone_grey(at(x + dx, y + dy))]
            if len(greys) < 2 or any(q in done for q in greys):
                continue
            region = _stone_region(greys[0], at)
            if region is None or len(region) < STONE_MIN_PIXELS:
                continue
            glints = {(gx + dx, gy + dy) for (gx, gy) in region for dx, dy in STEPS}
            glints = {q for q in glints - region if near_white(at(*q))
                      and sum((q[0] + dx, q[1] + dy) in region for dx, dy in STEPS) >= 2}
            done |= region | glints
            rows = {}
            for (px, py) in region:
                lo, hi = rows.get(py, (px, px))
                rows[py] = (min(lo, px), max(hi, px))
            widths = [rows[r][1] - rows[r][0] + 1 for r in sorted(rows)]
            # an arch over straight sides: narrow at the top, its widest four
            # rows or more at the bottom
            if (len(widths) < 6 or widths[0] > max(widths) - 4
                    or any(v != max(widths) for v in widths[-4:])):
                continue
            pixels = sorted(region | glints)
            xs = [q[0] for q in pixels]
            ys = [q[1] for q in pixels]
            pieces.append({"pixels": pixels, "lum": sum(sum(at(*q)) for q in pixels) / len(pixels),
                           "shape": (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1),
                           "fill": 1.0, "framed": 1.0, "window": True, "stone": True})
    return pieces


def find_page_windows(w, h, colour, wall, rects):
    """The windows that glow at night: the page's (find_windows over the
    whole page, as before, so none is lost), and the ones a building's own
    drawing has where its glass is a colour another building on the page has
    for its walls (V32: Slateport's houses beside its Pokemon Center), looked
    for model by model (model_rects); each with all its panes (whole_window).

    A window one drawing alone has is glass of such a colour, a pane at least
    3 x 2 (not a glint on a roof) and not running out to the drawing's side
    (a band along a roof's edge); pale glass only where a frame closes round
    it (enclosed: not a wall's shading). Glass a drawing has beside a window
    of the page's is part of that window. And the drawings' shop windows
    (shop_window) and windows drawn in stone (stone_panes)."""
    def page_at(x, y):
        return colour(x, y) if 0 <= x < w and 0 <= y < h and wall[y * w + x] else None

    def drawing_at(rect):
        def at(x, y):
            return (colour(x, y) if rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
                    and wall[y * w + x] else None)
        return at

    _, page_walls = _glass(page_at, (0, 0, w, h))
    pieces = find_windows(w, h, colour, wall)
    windows = [f for f in pieces if f["window"]]
    for f in windows:
        f["pixels"] = list(f["pixels"])
    owner = {q: i for i, f in enumerate(windows) for q in f["pixels"]}
    drawn = [None] * len(windows)   # the drawing each window is in, once known
    for rect in rects:
        at = drawing_at(rect)
        for f in find_windows(w, h, colour, wall, rect):
            if f.get("dark"):
                continue
            if not f["window"]:
                shop = None if any(q in owner for q in f["pixels"]) else shop_window(f, at)
                if shop is None or any(q in owner for q in shop):
                    continue
                f["window"] = True
                f["pixels"] = []
                windows.append(f)
                pieces.append(f)
                drawn.append(at)
                for q in shop:
                    owner[q] = len(windows) - 1
                    f["pixels"].append(q)
                continue
            glass = [q for q in f["pixels"] if q not in owner]
            hits = [owner[q] for q in f["pixels"] if q in owner]
            if hits:
                i = max(sorted(set(hits)), key=hits.count)
            else:
                x0, y0, bw, bh = f["shape"]
                if (bw < 3 or bh < 2 or x0 <= rect[0] or x0 + bw >= rect[2]
                        or not any(page_at(*q) in page_walls for q in f["pixels"])):
                    continue
                blue = any(glassy(at(*q)) and not pale(at(*q)) for q in f["pixels"])
                if not blue and not enclosed(f["pixels"], at):
                    continue
                f["pixels"] = []
                windows.append(f)
                pieces.append(f)
                drawn.append(None)
                i = len(windows) - 1
            drawn[i] = drawn[i] or at
            for q in glass:
                owner[q] = i
                windows[i]["pixels"].append(q)
    for rect in rects:
        at = drawing_at(rect)
        for f in stone_panes(at, rect, owner):
            windows.append(f)
            pieces.append(f)
            drawn.append(at)
            for q in f["pixels"]:
                owner[q] = len(windows) - 1
    # every pane of each window, in its own drawing where it is known
    for i, f in enumerate(windows):
        if f.get("dark") or f.get("stone"):
            continue
        for q in whole_window(f, drawn[i] or page_at):
            if q not in owner:
                owner[q] = i
                f["pixels"].append(q)
    for f in windows:
        f["pixels"] = sorted(f["pixels"])
    return pieces


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
        pieces = find_page_windows(w, h, colour, wall, model_rects(blob, page))
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
