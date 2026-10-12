#!/usr/bin/env python3
"""Real roofs, recessed windows and framed doors for the modelled buildings
(project-owned).

The owner's choice of 2026-10-12 ("option B"): the world should look genuinely
dimensional - real slopes, ridges, recessed windows, framed doors and trim -
even where the normal camera then shows a building a little differently from
its drawing; its colours, its silhouette and its architecture stay Emerald's.

Roofs. Until then every building auto_buildings.py finds was a `Relief`: its
facade standing up and the rest of its drawing - the roof, as the GBA's 45
degrees see it - laid flat on top. Now its walls stay the Relief's (the facade
is still its drawing, pixel for pixel) and its roof is one of (build_roof,
BUILDERS; which building gets which is auto_buildings.ROOFS):

  pitched   a hipped roof with a ridge over the cells it stands on, tiled from
            its own drawn roof the way the 3DS port tiles the roofs it models
            by hand (voxel_building.HipRoof, Strip): the eave the drawing shows
            at the eave, then as many courses of its tiles as the slope takes,
            never stretched, and the drawing's ridge on a ridge. The roof is
            read row by row (`analyse`): the courses are the rows that repeat,
            every so many pixels down and across; above them is the ridge the
            artist drew, below them the eave. A drawn roof's edges are not its
            tiles (a verge, a hip, a tree leaning over), so the slopes are laid
            from a clean swatch of it (`material`) appended below the drawing.
  flat      the drawing laid flat as before, with a parapet round it
            (`Parapet`): still exactly the drawing from 45 degrees.
  dome, cone, pyramid4, pyramid8
            a shell over the plan (`Shell`) - a hut's thatch, a tower's roof,
            the Battle Pyramid - its drawn roof spread over it as the GBA's
            45 degrees see it, front half and back.
  tent      a Battle Tent: its dome down to the ground, its entrance standing
            out in front of it as the Relief's walls.
  split     two buildings drawn as one, each part its own roof.

Facades (`facade_details`, `detail_model`): on every building outdoors - the
hand-made models too - the windows (window_glow.py's panes grown to their
frame) and the doors (where the door's own opening animation draws) are set
back into the wall with reveals, the windows with a sill under them.

Nothing here is judged pixel for pixel: roofs are tagged "~behind" and the
details' rectangles are left out of the 45-degree proof (gen_voxel_buildings
verify); the facades round them still are.
"""

import math

from PIL import Image

import voxel_building as vb
import window_glow

# The slopes' pitch (degrees): steeper than the 3DS port's hand-made roofs,
# which had to keep within the rows the drawing gives a roof; not as steep as
# a real Japanese roof, so a house keeps its drawn proportions.
PITCH = 30.0
# How far the eaves stand out past the walls (pixels), and the eave board's
# rows at most.
OVERHANG = 2
FASCIA_ROWS = 2
EAVE_ROWS = 4
# A ridge box is at most this many rows tall and deep.
RIDGE_ROWS = 6


def _opaque(p):
    return p[3] >= 128


def analyse(art, height, pitched=True):
    """How the drawing of a building with a `height`-row facade draws its
    roof, or None when it has no roof to read.

    Returns a dict:
      body    (x0, x1): the columns standing full height (opaque on the
              facade's top row and on the bottom row);
      eave    the first facade row - the roof is drawn above it;
      top     the roof's first row (the median of the body's columns);
      across  the courses' period across (pixels), or 0 for none found;
      ridge   (a, b): rows of the drawn ridge, above the courses;
      course  (c, d): one course's rows;
      eaves   (e, f): rows of the eave, below the courses.
    """
    W, Ht = art.size
    px = art.load()
    eave = Ht - height
    if eave < 8:
        return None
    body = [u for u in range(W) if _opaque(px[u, eave]) and _opaque(px[u, Ht - 1])]
    if len(body) < 16:
        return None
    x0, x1 = body[0], body[-1] + 1
    tops = sorted(next((v for v in range(Ht) if _opaque(px[u, v])), Ht) for u in body)
    top = tops[len(tops) // 2]
    if eave - top < 8:
        return None
    # the middle of the roof: clear of its verges, and of the hips some
    # drawings draw at its ends
    margin = max(2, (x1 - x0) // 5)
    ix0, ix1 = x0 + margin, x1 - margin

    def same(a, b):
        return _opaque(a) and _opaque(b) and a[:3] == b[:3]

    # Across: the smallest shift that repeats the roof's middle (tiles are
    # drawn 8 pixels wide, but a corrugated roof repeats every 2 or 4).
    across, best_bad = 0, None
    for q in range(1, 33):
        n = bad = 0
        for v in range(top, eave):
            for u in range(ix0, ix1 - q):
                n += 1
                bad += not same(px[u, v], px[u + q, v])
        if not n:
            break
        if bad <= 0.06 * n:
            across = q
            break
        if best_bad is None or bad / float(n) < best_bad[0]:
            best_bad = (bad / float(n), q)
    if not across and best_bad is not None and best_bad[0] <= 0.2 and best_bad[1] in (4, 8, 16):
        across = best_bad[1]    # two roofs joined (a double house): nearly so

    # Down: the period whose repeating rows cover the most of the roof (a
    # longer period when it covers about as much: a course with its joint
    # line, not two rows of its stripes).
    cols = range(ix0, ix1)
    found = None
    for p in range(2, 17):
        start = None
        best_run = (0, None)
        for v in range(top, eave - p):
            ok = sum(1 for u in cols if same(px[u, v], px[u, v + p]))
            if ok >= 0.9 * len(cols):
                if start is None:
                    start = v
                if v - start + 1 > best_run[0]:
                    best_run = (v - start + 1, start)
            else:
                start = None
        run, start = best_run
        if run < p:
            continue
        cover = run + p
        if found is None or cover > found[0] + 2 or (cover >= found[0] - 2 and p > found[1]
                                                     and p % found[1] == 0):
            found = (cover, p, start)
    if found is None:
        if not pitched:
            return None
        # no course repeats: the middle of the roof, as auto_buildings'
        # roof_band takes it, is the course
        rows = eave - top
        c = top + int(rows * 0.3)
        d = max(c + 4, top + int(rows * 0.65))
        found = (d - c, d - c, c)
    cover, p, c = found
    end = c + cover
    # the eave: its last rows only - a roof drawn as seen from above (its
    # front slope a band of its own under a flat top) keeps its board, not
    # the band, whose ends are the hips'
    end = max(end, eave - EAVE_ROWS)
    return {
        "body": (x0, x1), "eave": eave, "top": top, "across": across,
        "ridge": (top, c), "course": (c, c + p), "eaves": (end, eave),
        "inner": (ix0, ix1),
    }


def material(drawing, art, info):
    """The art (the drawing, and anything appended to it already) with a
    clean swatch of the drawn roof appended below it: the ridge rows, one
    course and the eave rows, each `width` pixels wide - a whole number of
    the courses' period across - cut from the middle of the roof, every pixel
    opaque. Returns (image, rows) with rows the swatch's (ridge, course,
    eaves) row ranges in the image."""
    px = drawing.load()
    W, Ht = art.size
    ix0, ix1 = info["inner"]
    q = info["across"] or 8
    width = swatch_width(info)
    bands = [info["ridge"], info["course"], info["eaves"]]
    rows = sum(b - a for a, b in bands)
    out = Image.new("RGBA", (max(W, width), Ht + rows), (0, 0, 0, 0))
    out.paste(art, (0, 0))
    opx = out.load()
    at = Ht
    placed = []
    mid = (ix0 + ix1) // 2
    for (a, b) in bands:
        placed.append((at, at + (b - a)))
        for v in range(a, b):
            row = [px[u, v] for u in range(ix0, ix1)]
            for x in range(width):
                if info["across"]:
                    # the same place in the pattern, nearest the middle first
                    phase = x % q
                    cands = sorted((u for u in range(ix0, ix1) if (u - ix0) % q == phase),
                                   key=lambda u: abs(u - mid))
                else:
                    cands = [ix0 + x % max(1, ix1 - ix0)]
                pick = next((px[u, v] for u in cands if _opaque(px[u, v])), None)
                if pick is None:
                    pick = next((p for p in row if _opaque(p)), (128, 128, 128, 255))
                opx[x, at + v - a] = pick[:3] + (255,)
        at += b - a
    return out, placed


def swatch_width(info):
    q = info["across"] or 8
    return q * max(2, int(math.ceil(32.0 / q)))


def pitched_roof(name, info, rows, width, front, back, height, pitch=PITCH,
                 overhang=OVERHANG, hips=0.6):
    """A hipped roof with a ridge over the walls of a building whose drawing
    `analyse` read: the eaves `overhang` pixels past the walls (x over the
    body's columns, z from `back` to `front`), standing on the walls' top at
    `height`, laid from the swatch `material` appended (`rows`, `width`).
    `hips`: how far the hips come in, as a share of the front slope's run (1:
    the hips as steep as the slopes; less: steeper hips, a longer ridge)."""
    (r0, r1), (c0, c1), (e0, e1) = rows
    period = c1 - c0
    wrap = (0, width)
    # The eave board: the eave's last rows; the rest of the eave starts the
    # slope, then the courses carry on in phase with them.
    fascia_n = min(FASCIA_ROWS, e1 - e0) or 1
    if e1 - e0 >= 1:
        fascia = vb.Strip((e1 - fascia_n, e1), wrap=wrap)
        fixed = (e0, e1 - fascia_n)
    else:
        fascia = vb.Strip((c1 - 1, c1), wrap=wrap)
        fixed = (c0, c0)
    a, d = info["course"]
    e = info["eaves"][0]
    phase = (e - 1 - a) % period
    slope = vb.Strip(fixed, repeat=(c0, c1), start=c0 + phase, wrap=wrap)
    # The ridge: the drawing's own, split into its top (seen from above) and
    # its front, at most RIDGE_ROWS each; none when the drawing has none.
    n = r1 - r0
    if n >= 4:
        cap_n = min(RIDGE_ROWS, max(2, n // 2))
        teeth_n = min(RIDGE_ROWS, n - cap_n)
        cap = (r0, r0 + cap_n)
        teeth = (r1 - teeth_n, r1)
        ridge = True
    else:
        cap = (c0, c0)
        teeth = (c0, min(c1, c0 + 2))
        ridge = False
    x0, x1 = info["body"]
    zf, zb = front + overhang, back - overhang
    cd = cap[1] - cap[0]
    front_run = (zf - zb) / 2.0 - cd / 2.0
    run = max(0.0, min(front_run * hips, (x1 - x0 + 2 * overhang) / 2.0 - 4.0))
    return vb.HipRoof(
        name, x0 - overhang, x1 + overhang, zf=zf, zb=zb, y0=height,
        fascia=fascia, slope=slope, teeth=teeth, cap=cap, pitch=pitch, run=run,
        ridge_u=(0, width), end_tile=vb.Tile(0, teeth[0], max(1, min(cd, width)), teeth[1]),
        ridge=ridge, ridge_wrap=wrap)


def roofed(name, drawing, art, height, back, roof):
    """(art, roof part, info) for a Relief-modelled building given a pitched
    roof: the art with its swatch appended, the roof (tagged "~behind": the
    45-degree proof does not judge it) and what `analyse` read. None when its
    drawing has no roof to read."""
    info = analyse(drawing, height)
    if info is None:
        return None
    out, rows = material(drawing, art, info)
    part = pitched_roof(name + "~behind", info, rows, swatch_width(info), drawing.size[1],
                        back or 0, height, pitch=roof.get("pitch", PITCH),
                        hips=roof.get("hips", 0.6))
    return out, part, info


# ── Shells: domes, cones, pyramids ─────────────────────────────────────────

def shade_of(normal):
    """The shade of a face of any slope, as the models light their faces: a
    wall's (south 1, west 0.80, east 0.72, north 0.66) blended with a top's
    (1) by how far the face looks up; a face looking down is in the eave's
    shadow."""
    nx, ny, nz = normal
    n = math.sqrt(nx * nx + ny * ny + nz * nz)
    if n < 1e-12:
        return vb.SHADE_ART
    nx, ny, nz = nx / n, ny / n, nz / n
    if ny < -0.5:
        return 0.6
    h2 = nx * nx + nz * nz
    if h2 < 1e-12:
        return vb.SHADE_ART
    wall = (nx * nx * (vb.SHADE_WEST if nx < 0 else vb.SHADE_EAST)
            + nz * nz * (vb.SHADE_ART if nz > 0 else vb.SHADE_BACK)) / h2
    up = max(0.0, ny) ** 2
    return up * vb.SHADE_ART + (1.0 - up) * wall


def grown(art, rows):
    """The drawing's rows [rows) with every transparent pixel the colour of
    the nearest drawn one: a band a shell can sample anywhere without a
    hole. Returns the band (W x rows)."""
    W = art.size[0]
    r0, r1 = rows
    band = art.crop((0, r0, W, r1))
    px = band.load()
    w, h = band.size
    known = {(x, y): px[x, y] for y in range(h) for x in range(w) if _opaque(px[x, y])}
    edge = list(known)
    while edge and len(known) < w * h:
        nxt = []
        for (x, y) in edge:
            for (i, j) in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= i < w and 0 <= j < h and (i, j) not in known:
                    known[(i, j)] = known[(x, y)]
                    nxt.append((i, j))
        edge = nxt
    for (x, y), c in known.items():
        px[x, y] = c[:3] + (255,)
    return band


def _normal(a, b, c):
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    return (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0])


class Shell:
    """A roof of any shape over a plan - a dome, a cone, a pyramid - whose
    texture is its drawing as the GBA's 45 degrees see it, fitted to it.

    The drawing shows a building's roof shallower than the cells it stands
    on, so the shape seen from 45 degrees covers more rows than the drawing
    gives it: the rows are spread over it (`fit`, linear in the projection,
    u = X and v = Z - Y), its front half the drawing and its back half the
    drawing again, as if seen from behind. Every texel comes from a band of
    the drawing grown past its outline (`grown`), so the shape's edge never
    samples a hole. Faces are tagged "~behind~proj": no 1:1 density to keep,
    and the 45-degree proof does not judge them.

    `tris` are ((x, y, z) * 3) wound outwards; `centre` z is where the back
    half begins; `fit` maps (x, z - y) to (u, v) in the band.
    """

    def __init__(self, name, tris, centre_z, fit):
        self.name, self.tris, self.centre_z, self.fit = name, tris, centre_z, fit

    def emit(self, mesh):
        cz = self.centre_z
        for tri in self.tris:
            # split at the centre so no triangle mixes the two halves
            pieces = [tri]
            zs = [p[2] for p in tri]
            if min(zs) < cz - vb.EPS and max(zs) > cz + vb.EPS:
                poly = list(tri)
                pieces = []
                for keep in (True, False):
                    piece = vb.clip(poly, 2, cz, keep)
                    if len(piece) >= 3:
                        pieces += [(piece[0], piece[i], piece[i + 1])
                                   for i in range(1, len(piece) - 1)]
            for (a, b, c) in pieces:
                nrm = _normal(a, b, c)
                if sum(v * v for v in nrm) < 1e-12:
                    continue
                shade = shade_of(nrm)
                back = (a[2] + b[2] + c[2]) / 3.0 < cz
                out = []
                for (x, y, z) in (a, b, c):
                    zz = 2 * cz - z if back else z
                    u, v = self.fit(x, zz - y)
                    out.append((x, y, z, u, v))
                mesh.tri(out[0], out[1], out[2], shade, self.name + "~behind~proj")


def ellipse_ring(cx, cz, rx, rz, n, phase=0.0):
    return [(cx + rx * math.sin(phase + 2 * math.pi * i / n),
             cz + rz * math.cos(phase + 2 * math.pi * i / n)) for i in range(n)]


def dome_tris(cx, cz, rx, rz, y0, rise, n=24, rings=6, power=1.0):
    """An elliptical dome over (cx, cz) with radii rx, rz, its rim at y0 and
    its crown `rise` above it; `power` < 1 rounds its shoulders out (a tent),
    > 1 points it (an onion)."""
    tris = []
    prev = None
    for k in range(rings + 1):
        e = (math.pi / 2) * k / rings        # elevation
        s, y = math.cos(e), y0 + rise * (math.sin(e) ** power if k else 0.0)
        ring = [(x, y, z) for (x, z) in ellipse_ring(cx, cz, rx * s, rz * s, n)]
        if prev is not None:
            for i in range(n):
                a, b = prev[i], prev[(i + 1) % n]
                c, d = ring[(i + 1) % n], ring[i]
                if k == rings:
                    tris.append((a, b, c))
                else:
                    tris.append((a, b, c))
                    tris.append((a, c, d))
        prev = ring
    return [_outward(t, (cx, y0, cz)) for t in tris]


def pyramid_tris(plan, y0, apex):
    """A pyramid from the plan polygon [(x, z)] at y0 up to `apex` (x, y, z)."""
    tris = []
    n = len(plan)
    for i in range(n):
        (xa, za), (xb, zb) = plan[i], plan[(i + 1) % n]
        tris.append(((xa, y0, za), (xb, y0, zb), apex))
    cx = sum(p[0] for p in plan) / n
    cz = sum(p[1] for p in plan) / n
    return [_outward(t, (cx, y0, cz)) for t in tris]


def octagon(x0, x1, z0, z1, cut):
    """The plan of an octagon in the rectangle, its corners cut by `cut`."""
    return [(x0 + cut, z1), (x1 - cut, z1), (x1, z1 - cut), (x1, z0 + cut),
            (x1 - cut, z0), (x0 + cut, z0), (x0, z0 + cut), (x0, z1 - cut)]


def _outward(tri, inside):
    a, b, c = tri
    nrm = _normal(a, b, c)
    g = [(a[i] + b[i] + c[i]) / 3.0 - inside[i] for i in range(3)]
    if sum(nrm[i] * g[i] for i in range(3)) < 0:
        return (a, c, b)
    return tri


def projected_fit(tris, centre_z, rect, band_top):
    """The fit Shell takes: the shape's front half, seen from 45 degrees,
    spread over the drawing's roof `rect` (u0, v0, u1, v1), its rows in the
    band appended at `band_top` (the band's row 0 is the drawing's row v0)."""
    xs, vs = [], []
    for tri in tris:
        for (x, y, z) in tri:
            zz = z if z >= centre_z else 2 * centre_z - z
            xs.append(x)
            vs.append(zz - y)
    x_lo, x_hi = min(xs), max(xs)
    v_lo, v_hi = min(vs), max(vs)
    u0, v0, u1, v1 = rect
    su = (u1 - u0) / max(1e-6, x_hi - x_lo)
    sv = (v1 - v0) / max(1e-6, v_hi - v_lo)

    def fit(x, v):
        uu = u0 + (x - x_lo) * su
        vv = (v - v_lo) * sv
        return (min(u1 - 0.01, max(u0, uu)), band_top + min(v1 - v0 - 0.01, max(0.0, vv)))
    return fit


def _body(drawing, height):
    """(x0, x1, eave, top) of a drawing's body: the columns standing full
    height, the first facade row, the highest drawn row over them; None
    when there is none."""
    W, Ht = drawing.size
    px = drawing.load()
    eave = Ht - height
    if eave < 2:
        return None
    body = [u for u in range(W) if _opaque(px[u, eave]) and _opaque(px[u, Ht - 1])]
    if len(body) < 8:
        return None
    x0, x1 = body[0], body[-1] + 1
    top = min(next((v for v in range(Ht) if _opaque(px[u, v])), Ht) for u in body)
    return x0, x1, eave, top


def _shape(kind, roof, cx, cz, rx, rz, y0, rise):
    if kind == "dome":
        return dome_tris(cx, cz, rx, rz, y0, rise, power=roof.get("power", 1.0))
    if kind == "cone":
        return dome_tris(cx, cz, rx, rz, y0, rise, rings=1)
    if kind == "pyramid8":
        cut = roof.get("cut", 0.3) * min(rx, rz) * 2
        return pyramid_tris(octagon(cx - rx, cx + rx, cz - rz, cz + rz, cut), y0,
                            (cx, y0 + rise, cz))
    if kind == "pyramid4":
        plan = [(cx - rx, cz + rz), (cx + rx, cz + rz), (cx + rx, cz - rz), (cx - rx, cz - rz)]
        return pyramid_tris(plan, y0, (cx, y0 + rise, cz))
    return None


def _with_band(drawing, art, rows):
    """The art with the drawing's rows `rows`, grown past its outline,
    appended below it; returns (art, the band's first row)."""
    band = grown(drawing, rows)
    W, Ht = art.size
    out = Image.new("RGBA", (max(W, band.size[0]), Ht + band.size[1]), (0, 0, 0, 0))
    out.paste(art, (0, 0))
    out.paste(band, (0, Ht))
    return out, Ht


def shelled(name, drawing, art, height, back, roof):
    """(art, Shell, info) for a Relief-modelled building given a shell roof -
    roof["type"] "dome" (`power` rounding its shoulders out, or pointing it),
    "cone" (a hut's thatch), "pyramid8" (an octagonal tower's) or "pyramid4"
    - over its body, laid with the drawing's roof rows; None when there is
    no roof to read. `rise`: the crown over the walls' top, as a share of the
    plan's smaller half-width."""
    found = _body(drawing, height)
    if found is None:
        return None
    x0, x1, eave, top = found
    if eave - top < 4:
        return None
    Ht = drawing.size[1]
    z0, z1 = float(back or 0), float(Ht)
    over = roof.get("overhang", OVERHANG)
    cx, cz = (x0 + x1) / 2.0, (z0 + z1) / 2.0
    rx, rz = (x1 - x0) / 2.0 + over, (z1 - z0) / 2.0 + over
    rise = roof.get("rise", 0.8) * min(rx, rz)
    tris = _shape(roof["type"], roof, cx, cz, rx, rz, float(height), rise)
    if tris is None:
        return None
    out, band_top = _with_band(drawing, art, (top, eave))
    fit = projected_fit(tris, cz, (x0, top, x1, eave), band_top)
    info = {"body": (x0, x1), "eave": eave, "top": top}
    return out, Shell(name + "~behind", tris, cz, fit), info


def tented(name, drawing, art, height, back, roof):
    """(art, Shell, info) for a Battle Tent: its dome down to `rim` pixels
    over the ground all round, laid with the drawing's dome (its rows above
    `dome_end`), and its entrance - the drawing's columns `porch` - standing
    in front of it as the Relief's walls (info["relief"], the drawing the
    Relief keeps)."""
    original = drawing
    if "cols" in roof:
        # its own columns (a tree or the rock beside it drawn in its cells),
        # and none of the rows over it (a ledge behind it)
        drawing = masked(drawing, roof["cols"], roof.get("top", 0))
    W, Ht = drawing.size
    px = drawing.load()
    # the dome is as wide as it is drawn anywhere (its foot is the drawing's
    # narrowest part: the entrance)
    cols = [u for u in range(W) if any(_opaque(px[u, v]) for v in range(Ht))]
    if len(cols) < 8:
        return None
    x0, x1 = cols[0], cols[-1] + 1
    eave = Ht - height
    top = min(next((v for v in range(Ht) if _opaque(px[u, v])), Ht) for u in cols)
    z0, z1 = float(back or 0), float(Ht)
    over = roof.get("overhang", 0)
    cx, cz = (x0 + x1) / 2.0, (z0 + z1) / 2.0
    rx, rz = (x1 - x0) / 2.0 + over, (z1 - z0) / 2.0 + over
    rim = float(roof.get("rim", 6))
    rise = roof.get("rise", 0.9) * min(rx, rz)
    tris = dome_tris(cx, cz, rx, rz, rim, rise, n=32, rings=8, power=roof.get("power", 0.6))
    end = roof.get("dome_end", eave)
    out, band_top = _with_band(drawing, art, (top, end))
    fit = projected_fit(tris, cz, (x0, top, x1, end), band_top)
    # The Relief keeps the entrance - its drawn rows down from its top, at 45
    # degrees as deep as it stands out - and whatever is drawn beside the
    # tent in its cells (a tree, a rock), which nothing else would draw.
    porch = tuple(roof["porch"])
    back = Ht - roof.get("porch_depth", 24)
    porch_height = roof.get("porch_height", height)
    keep = original.copy()
    kpx = keep.load()
    for u in range(W):
        for v in range(Ht):
            if x0 <= u < x1 and not (porch[0] <= u < porch[1] and v >= back - porch_height):
                kpx[u, v] = (0, 0, 0, 0)
    info = {"body": (x0, x1), "eave": eave, "top": top, "relief": keep, "keep_lid": True,
            "relief_back": back, "relief_height": porch_height,
            "exact": [(porch[0], back - porch_height, porch[1], Ht)]}
    return out, Shell(name + "~behind", tris, cz, fit), info


# ── Flat roofs: a parapet round the edge ────────────────────────────────────

class Parapet:
    """A low wall round a flat roof's edge, `rise` pixels over the roof and
    `thick` deep, over the rectangle x0..x1, zb..zf of a roof at `y`.

    Textured as the GBA's 45 degrees see it (u = X, v = Z - Y), it is still
    the drawing from there: its front and its top show the very rows of the
    roof they stand in front of, a point (X, y + h, Z) lying on the ray of the
    roof's point (X, y, Z - h). Its faces the drawing cannot show - its inner
    sides, the back - take the roof's own rows, clamped to the drawn roof.
    Tagged "~behind": the proof judges the drawing under it."""

    def __init__(self, name, x0, x1, zb, zf, y, rise=3.0, thick=2.0, rows=None):
        self.name = name
        self.x0, self.x1, self.zb, self.zf, self.y = x0, x1, zb, zf, y
        self.rise, self.thick = rise, thick
        self.rows = rows          # (v0, v1): the drawn roof's rows, for clamping

    def _uv(self, x, y, z, clamp):
        v = z - y
        if clamp and self.rows is not None:
            v = min(self.rows[1] - 0.01, max(self.rows[0], v))
        return (x, y, z, x, v)

    def _quad(self, mesh, pts, shade, tag):
        clamp = tag.endswith("~clamp")
        mesh.poly([self._uv(x, y, z, clamp) for (x, y, z) in pts], shade, self.name + "~behind" + tag)

    def emit(self, mesh):
        x0, x1, zb, zf, y, h, t = self.x0, self.x1, self.zb, self.zf, self.y, self.rise, self.thick
        top = y + h
        # front: its face, its top, its inside
        self._quad(mesh, [(x0, y, zf), (x1, y, zf), (x1, top, zf), (x0, top, zf)], vb.SHADE_ART, ".front")
        self._quad(mesh, [(x0, top, zf), (x1, top, zf), (x1, top, zf - t), (x0, top, zf - t)],
                   vb.SHADE_ART, ".top")
        self._quad(mesh, [(x1, y, zf - t), (x0, y, zf - t), (x0, top, zf - t), (x1, top, zf - t)],
                   vb.SHADE_BACK, ".inside~clamp")
        # back
        self._quad(mesh, [(x1, y, zb), (x0, y, zb), (x0, top, zb), (x1, top, zb)], vb.SHADE_BACK,
                   ".back~clamp")
        self._quad(mesh, [(x0, top, zb + t), (x1, top, zb + t), (x1, top, zb), (x0, top, zb)],
                   vb.SHADE_ART, ".top~clamp")
        self._quad(mesh, [(x0, y, zb + t), (x1, y, zb + t), (x1, top, zb + t), (x0, top, zb + t)],
                   vb.SHADE_ART, ".inside~clamp")
        # the sides, between the front's and the back's
        for (xo, xi, shade_o, shade_i) in ((x0, x0 + t, vb.SHADE_WEST, vb.SHADE_EAST),
                                          (x1, x1 - t, vb.SHADE_EAST, vb.SHADE_WEST)):
            self._quad(mesh, [(xo, y, zb), (xo, y, zf), (xo, top, zf), (xo, top, zb)], shade_o,
                       ".side~clamp")
            self._quad(mesh, [(xo, top, zf - t), (xi, top, zf - t), (xi, top, zb + t), (xo, top, zb + t)],
                       vb.SHADE_ART, ".top~clamp")
            self._quad(mesh, [(xi, y, zb + t), (xi, y, zf - t), (xi, top, zf - t), (xi, top, zb + t)],
                       shade_i, ".inside~clamp")


def parapeted(name, drawing, art, height, back, roof):
    """(art, Parapet, info) for a flat-roofed Relief building: its drawing laid
    flat on top as before, a parapet round the edge of its body. None when
    there is no roof to read."""
    found = _body(drawing, height)
    if found is None:
        return None
    x0, x1, eave, top = found
    part = Parapet(name, float(x0), float(x1), float(back or 0), float(drawing.size[1]),
                   float(height), rise=roof.get("rise", 3.0), thick=roof.get("thick", 2.0),
                   rows=(top, eave))
    return art, part, {"body": (x0, x1), "eave": eave, "top": top, "keep_lid": True}


BUILDERS = {"pitched": roofed, "flat": parapeted, "tent": tented,
            "dome": shelled, "cone": shelled, "pyramid8": shelled, "pyramid4": shelled}


def masked(drawing, cols, top=0):
    """The drawing with every column outside [cols) and every row above
    `top` transparent."""
    out = drawing.copy()
    px = out.load()
    W, H = out.size
    for u in range(W):
        for v in range(H):
            if not (cols[0] <= u < cols[1]) or v < top:
                px[u, v] = (0, 0, 0, 0)
    return out


def build_roof(name, drawing, art, height, back, roof):
    """A Relief building's real roof (auto_buildings.ROOFS): returns (art,
    parts, lids, exact, (relief, back, height)) - the art with whatever the
    roof lays appended, the roof's parts, the column ranges whose drawn top
    the roof replaces (Relief `lid`), the art rectangles the proof still
    holds the model to and the drawing the Relief keeps with its back and
    height (a tent's: its entrance and whatever stands beside it; None for
    the whole drawing) - or None for no roof.
    roof["type"] "split" gives each of its `parts` - ((x0, x1), roof) - its
    own columns: two buildings drawn as one (Mauville's)."""
    W, Ht = drawing.size
    if roof["type"] == "split":
        jobs = [(tuple(cols), sub) for cols, sub in roof["parts"]]
    else:
        jobs = [(None, roof)]
    parts, lids, exact, relief, relief_back = [], [], [(0, 0, W, Ht)], None, back
    relief_height = height
    for k, (cols, sub) in enumerate(jobs):
        if isinstance(sub, str):
            sub = {"type": sub}
        make = BUILDERS.get(sub["type"])
        if make is None:
            continue
        d = drawing if cols is None else masked(drawing, cols)
        done = make(name + ("_%d" % k if cols else ""), d, art, height, back, sub)
        if done is None:
            continue
        art, part, info = done
        parts.append(part)
        if not info.get("keep_lid"):
            lids.append(info["body"])
            exact = [(0, info["eave"], W, Ht)]
        if "relief" in info:
            # (the entrance keeps its own top: the dome stands behind it)
            relief = info["relief"]
            relief_back = info["relief_back"]
            relief_height = info["relief_height"]
            exact = info["exact"]
    if not parts:
        return None
    return art, parts, lids, exact, (relief, relief_back, relief_height)


# ── Facades: recessed windows, framed doors ────────────────────────────────
#
# A facade is its drawing on an upright face looking south, textured as the
# GBA's 45 degrees see it (u = X + du, v = Z - Y + dv). A window's glass (the
# panes window_glow.py finds, which glow at night) and a door's leaf (where
# the door's own opening animation draws, field_door.c) are set back into the
# wall, their texels unchanged; the wall round them stays where it stood, so
# its frame - the drawing's - becomes a frame in depth, with reveals laid
# from the frame's own pixels and a sill under each window. The door's dark
# opening (gl_object_renderer.c, DrawDoorOpening) then opens inside its frame.

WINDOW_DEPTH = 1.5
DOOR_DEPTH = 3.0
SILL_OUT = 1.0
SILL_HIGH = 1.0
RECESS_SHADE = 0.88         # the glass and the leaf, set back from the light

_DOORS = None


def _door_table():
    """[(metatile, tileset symbol, size, png path)] of field_door.c."""
    global _DOORS
    if _DOORS is None:
        import os
        import re
        src = open(os.path.join(vb.ROOT, "src", "field_door.c"), encoding="utf-8").read()
        labels = {m.group(1): int(m.group(2), 16) for m in re.finditer(
            r"#define\s+(METATILE_\w+)\s+(0x[0-9A-Fa-f]+)",
            open(os.path.join(vb.ROOT, "include", "constants", "metatile_labels.h"),
                 encoding="utf-8").read())}
        tiles = dict(re.findall(r'static const u8 (sDoorAnimTiles_\w+)\[\] = INCGFX_U8\("([^"]+)"', src))
        body = src[src.index("sDoorAnimGraphicsTable[] ="):]
        body = body[:body.index("};")]
        _DOORS = []
        for m in re.finditer(r"\.metatileNum = (\w+),\s*\.tileset = &(\w+),.*?\.size = (\w+),"
                             r".*?\.tiles = (\w+),", body, re.S):
            if m.group(1) in labels and m.group(4) in tiles:
                _DOORS.append((labels[m.group(1)], m.group(2), m.group(3), tiles[m.group(4)]))
    return _DOORS


def doorway(layout, x, y):
    """The door at layout cell (x, y): (x0, y0, x1, y1), in pixels of the
    layout, of where its opening animation draws a fully open door
    differently from the closed one - its leaf and the dark behind it - or
    None when no animation is known for it."""
    import os
    m = layout.metatile(x, y)
    tileset = layout.primary if m < vb.NUM_PRIMARY else layout.secondary
    hit = next((d for d in _door_table() if d[0] == m and d[1] == tileset), None)
    if hit is None:
        return None
    _, _, size, png = hit
    img = Image.open(os.path.join(vb.ROOT, png)).convert("RGB")
    w = 32 if "2x2" in size else 16
    h = 16 if size == "DOOR_SIZE_1x1" else 32
    frames = img.size[1] // h
    if frames < 1:
        return None
    last = img.crop((0, (frames - 1) * h, w, frames * h)).load()
    left = x - (1 if size == "DOOR_SIZE_2x2_RIGHT" else 0)
    top = y - (h // 16 - 1)
    closed = Image.new("RGB", (w, h))
    for j in range(h // 16):
        for i in range(w // 16):
            if 0 <= left + i < layout.w and 0 <= top + j < layout.h:
                closed.paste(layout.cell_image(layout.metatile(left + i, top + j)).convert("RGB"),
                             (i * 16, j * 16))
    cpx = closed.load()
    diff = [(i, j) for j in range(h) for i in range(w)
            if sum(abs(p - q) for p, q in zip(cpx[i, j], last[i, j])) > 24]
    if len(diff) < 24:
        return None
    xs = [p[0] for p in diff]
    ys = [p[1] for p in diff]
    return (left * 16 + min(xs), top * 16 + min(ys), left * 16 + max(xs) + 1, top * 16 + max(ys) + 1)


def _facade_tris(mesh):
    """Indices of the triangles standing upright with the 45-degree mapping:
    {index: (z, du, dv)}."""
    out = {}
    for i, (tri, shade, tag) in enumerate(mesh.tris):
        z = tri[0][2]
        if any(abs(p[2] - z) > 1e-6 for p in tri):
            continue
        if "~behind" in tag or "~depth" in tag:
            continue
        du = tri[0][3] - tri[0][0]
        dv = tri[0][4] - (tri[0][2] - tri[0][1])
        if any(abs((p[3] - p[0]) - du) > 1e-4 or abs((p[4] - (p[2] - p[1])) - dv) > 1e-4
               for p in tri):
            continue
        xs = [p[0] for p in tri]
        ys = [p[1] for p in tri]
        if max(xs) - min(xs) < 1e-6 or max(ys) - min(ys) < 1e-6:
            continue
        out[i] = (z, du, dv)
    return out


def _area_xy(poly):
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
                   for i in range(len(poly)))) / 2.0


def _split(poly, xa, xb, ya, yb):
    """A planar polygon (tuples starting x, y) cut by the rectangle: (inside,
    [outside pieces])."""
    out = []
    left = vb.clip(poly, 0, xa, False)
    rest = vb.clip(poly, 0, xa, True)
    if len(left) >= 3:
        out.append(left)
    if len(rest) < 3:
        return None, out
    right = vb.clip(rest, 0, xb, True)
    mid = vb.clip(rest, 0, xb, False)
    if len(right) >= 3:
        out.append(right)
    if len(mid) < 3:
        return None, out
    below = vb.clip(mid, 1, ya, False)
    mid2 = vb.clip(mid, 1, ya, True)
    if len(below) >= 3:
        out.append(below)
    if len(mid2) < 3:
        return None, out
    above = vb.clip(mid2, 1, yb, True)
    inside = vb.clip(mid2, 1, yb, False)
    if len(above) >= 3:
        out.append(above)
    return (inside if len(inside) >= 3 else None), out


def recess(mesh, art_rect, depth, sill=False, name="recess"):
    """Sets the art rectangle (u0, v0, u1, v1) of a facade back `depth`
    pixels into its wall, with reveals and (with `sill`) a sill. Only where
    one upright face of the 45-degree mapping covers all of it - the same
    plane, the southernmost there; otherwise nothing changes. Returns True
    when it was set back."""
    u0, v0, u1, v1 = art_rect
    faces = _facade_tris(mesh)
    hits = {}
    for i, (z, du, dv) in faces.items():
        tri = mesh.tris[i][0]
        us = [p[3] for p in tri]
        vs = [p[4] for p in tri]
        if max(us) <= u0 + 1e-6 or min(us) >= u1 - 1e-6 or max(vs) <= v0 + 1e-6 or min(vs) >= v1 - 1e-6:
            continue
        hits[i] = (z, du, dv)
    if not hits:
        return False
    # the southernmost plane drawing the rectangle
    z = max(h[0] for h in hits.values())
    hits = {i: h for i, h in hits.items() if abs(h[0] - z) < 1e-6}
    planes = {(round(h[1], 4), round(h[2], 4)) for h in hits.values()}
    if len(planes) != 1:
        return False
    du, dv = next(iter(hits.values()))[1:]
    xa, xb = u0 - du, u1 - du
    ya, yb = z - (v1 - dv), z - (v0 - dv)        # v grows down: v1 is the bottom
    pieces, inside_area = [], 0.0
    for i in hits:
        tri, shade, tag = mesh.tris[i]
        inside, outside = _split(list(tri), xa, xb, ya, yb)
        pieces.append((i, inside, outside, shade, tag))
        if inside is not None:
            inside_area += _area_xy(inside)
    if inside_area < 0.98 * (xb - xa) * (yb - ya):
        return False                             # not one wall all over it
    drop = set()
    for (i, inside, outside, shade, tag) in pieces:
        drop.add(i)
        for poly in outside:
            mesh.poly(poly, shade, tag)
        if inside is not None:
            mesh.poly([(p[0], p[1], p[2] - depth, p[3], p[4]) for p in inside],
                      shade * RECESS_SHADE, tag + ".recess")
    mesh.tris = [t for k, t in enumerate(mesh.tris) if k not in drop]
    zb = z - depth
    # reveals, from the frame's pixels round the opening
    sill_v = v1 + 0.5                            # the frame's row under it
    head_v = v0 - 0.5                            # and over it
    mesh.poly([(xa, ya, zb, xa + du, sill_v), (xb, ya, zb, xb + du, sill_v),
               (xb, ya, z, xb + du, sill_v), (xa, ya, z, xa + du, sill_v)],
              vb.SHADE_ART, name + ".sill~clamp")
    mesh.poly([(xa, yb, z, xa + du, head_v), (xb, yb, z, xb + du, head_v),
               (xb, yb, zb, xb + du, head_v), (xa, yb, zb, xa + du, head_v)],
              0.6, name + ".head~clamp")
    mesh.poly([(xa, ya, z, u0 - 0.5, z - ya + dv), (xa, ya, zb, u0 - 0.5, z - ya + dv),
               (xa, yb, zb, u0 - 0.5, z - yb + dv), (xa, yb, z, u0 - 0.5, z - yb + dv)],
              vb.SHADE_EAST, name + ".jamb~clamp")
    mesh.poly([(xb, ya, zb, u1 + 0.5, z - ya + dv), (xb, ya, z, u1 + 0.5, z - ya + dv),
               (xb, yb, z, u1 + 0.5, z - yb + dv), (xb, yb, zb, u1 + 0.5, z - yb + dv)],
              vb.SHADE_WEST, name + ".jamb~clamp")
    if sill:
        # a ledge under the window, standing out of the wall
        sa, sb = xa - 1.0, xb + 1.0
        yt, yl = ya, ya - SILL_HIGH
        zf = z + SILL_OUT
        ua, ub = sa + du, sb + du
        mesh.poly([(sa, yt, z, ua, sill_v), (sb, yt, z, ub, sill_v),
                   (sb, yt, zf, ub, sill_v), (sa, yt, zf, ua, sill_v)], vb.SHADE_ART,
                  name + ".sill_top~clamp")
        mesh.poly([(sa, yl, zf, ua, sill_v), (sb, yl, zf, ub, sill_v),
                   (sb, yt, zf, ub, sill_v), (sa, yt, zf, ua, sill_v)], vb.SHADE_ART,
                  name + ".sill_front~clamp")
        for (x, uu, shade) in ((sa, ua, vb.SHADE_WEST), (sb, ub, vb.SHADE_EAST)):
            mesh.poly([(x, yl, z, uu, sill_v), (x, yl, zf, uu, sill_v),
                       (x, yt, zf, uu, sill_v), (x, yt, z, uu, sill_v)], shade,
                      name + ".sill_end~clamp")
    return True


def facade_details(model, doors=()):
    """Recess the windows and the doors (art rectangles, `doorway`) of a
    model's facades. Returns the art rectangles changed, which the proof
    leaves out."""
    art = model.art
    W, H = art.size
    px = art.load()
    faces = _facade_tris(model.mesh)
    tris = []
    for i in faces:
        tri = model.mesh.tris[i][0]
        tris.append(tuple((p[3], p[4], p[0] / 16.0, p[1] / 16.0, p[2] / 16.0) for p in tri))
    wall = window_glow.wall_mask(W, H, tris)

    def colour(x, y):
        p = px[x, y]
        return p[:3] if p[3] >= 128 else None

    changed = []
    for rect in doors:
        if recess(model.mesh, rect, DOOR_DEPTH, name=model.name + ".door"):
            changed.append(rect)
    windows = []
    for rect in window_rects(W, H, colour, wall):
        if rect[2] - rect[0] < 3 or rect[3] - rect[1] < 3:
            continue                    # a sliver of glass, not a window
        if any(r[0] < rect[2] and rect[0] < r[2] and r[1] < rect[3] and rect[1] < r[3]
               for r in changed):
            continue                    # a door's glass
        if recess(model.mesh, rect, WINDOW_DEPTH, sill=True, name=model.name + ".window"):
            changed.append(rect)
    return changed


def _lum(c):
    return sum(c) if c is not None else 0


def _flood(start, frame, colour, wall, W, H, box):
    """The pixels from `start` up to the frame's colour, or None when they
    run out of `box` or onto anything not drawn on the wall."""
    region = {start}
    todo = [start]
    while todo:
        x, y = todo.pop()
        for dx, dy in window_glow.STEPS:
            q = (x + dx, y + dy)
            if q in region:
                continue
            if not (box[0] <= q[0] <= box[2] and box[1] <= q[1] <= box[3]):
                return None
            if not (0 <= q[0] < W and 0 <= q[1] < H) or not wall[q[1] * W + q[0]]:
                return None
            c = colour(*q)
            if c is None:
                return None
            if c == frame:
                continue
            region.add(q)
            todo.append(q)
    return region


def _panes_past_bars(region, frame, colour, wall, W, H, reach):
    """The regions closed in by the frame's colour one or two pixels past a
    pane's sides - its frame's other panes - that are mostly glass or white."""
    xs = [q[0] for q in region]
    ys = [q[1] for q in region]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    box = (x0 - reach, y0 - reach, x1 + reach, y1 + reach)
    out = []
    for (x, y, dx, dy) in ((cx, y1, 0, 1), (cx, y0 - 1, 0, -1), (x1, cy, 1, 0), (x0 - 1, cy, -1, 0)):
        bar = 0
        while bar < 2 and 0 <= x < W and 0 <= y < H and colour(x, y) == frame:
            x, y, bar = x + dx, y + dy, bar + 1
        if bar == 0 or not (0 <= x < W and 0 <= y < H) or not wall[y * W + x]:
            continue
        c = colour(x, y)
        if c is None or c == frame:
            continue
        found = _flood((x, y), frame, colour, wall, W, H, box)
        if not found or not found.isdisjoint(region) or len(found) > 4 * len(region) + 16:
            continue
        lit = sum(1 for q in found if window_glow.glassy(colour(*q))
                  or window_glow.near_white(colour(*q)) or window_glow.dark_glassy(colour(*q)))
        if lit >= 0.5 * len(found):
            out.append(found)
    return out


def window_rects(W, H, colour, wall, reach=8):
    """The windows of a facade, as art rectangles: each pane window_glow.py
    finds grown over everything its frame closes in (its other panes - white,
    a reflection, a blind - inside the frame's dark colour), and the panes of
    one frame, apart only by its glazing bars, joined into one window. A pane
    whose frame does not close round it within `reach` pixels stays a pane."""
    regions = []
    for piece in window_glow.find_windows(W, H, colour, wall):
        if not piece["window"]:
            continue
        inside = set(piece["pixels"])
        lum = max(_lum(colour(*q)) for q in inside)
        ring = {}
        for (x, y) in inside:
            for dx, dy in window_glow.STEPS:
                q = (x + dx, y + dy)
                if q in inside or not (0 <= q[0] < W and 0 <= q[1] < H):
                    continue
                c = colour(*q)
                if c is not None and _lum(c) < lum - 150:
                    ring[c] = ring.get(c, 0) + 1
        xs = [q[0] for q in inside]
        ys = [q[1] for q in inside]
        box = (min(xs) - reach, min(ys) - reach, max(xs) + reach, max(ys) + reach)
        frame = max(ring, key=ring.get) if ring else None
        region = set(inside)
        if frame is not None:
            todo = list(inside)
            leak = False
            while todo and not leak:
                x, y = todo.pop()
                for dx, dy in window_glow.STEPS:
                    q = (x + dx, y + dy)
                    if q in region:
                        continue
                    if not (box[0] <= q[0] <= box[2] and box[1] <= q[1] <= box[3]):
                        leak = True
                        break
                    if not (0 <= q[0] < W and 0 <= q[1] < H) or not wall[q[1] * W + q[0]]:
                        leak = True
                        break
                    c = colour(*q)
                    if c is None:
                        leak = True
                        break
                    if c == frame:
                        continue
                    region.add(q)
                    todo.append(q)
            if leak:
                region = set(inside)
        rx = [q[0] for q in region]
        ry = [q[1] for q in region]
        regions.append([min(rx), min(ry), max(rx) + 1, max(ry) + 1, frame])
        # and the frame's other panes - white, a reflection, a blind - past
        # its glazing bars, closed in by the frame as this one is
        if frame is not None:
            for extra in _panes_past_bars(region, frame, colour, wall, W, H, reach):
                ex = [q[0] for q in extra]
                ey = [q[1] for q in extra]
                regions.append([min(ex), min(ey), max(ex) + 1, max(ey) + 1, frame])
    # the panes of one frame: apart by a bar of the frame's colour, two
    # pixels at most, and side by side along most of their length
    merged = True
    while merged:
        merged = False
        for i in range(len(regions)):
            for j in range(i + 1, len(regions)):
                a, b = regions[i], regions[j]
                if a[4] is None or a[4] != b[4]:
                    continue
                gx = max(a[0], b[0]) - min(a[2], b[2])     # gap across (> 0: apart)
                gy = max(a[1], b[1]) - min(a[3], b[3])
                ox = min(a[2], b[2]) - max(a[0], b[0])     # overlap across
                oy = min(a[3], b[3]) - max(a[1], b[1])
                side = (0 <= gx <= 2 and oy >= 0.5 * min(a[3] - a[1], b[3] - b[1]))
                stack = (0 <= gy <= 2 and ox >= 0.5 * min(a[2] - a[0], b[2] - b[0]))
                if side or stack:
                    regions[i] = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]),
                                  max(a[3], b[3]), a[4]]
                    del regions[j]
                    merged = True
                    break
            if merged:
                break
    out = []
    for r in regions:
        rect = tuple(r[:4])
        if not any(o[0] <= rect[0] and rect[2] <= o[2] and o[1] <= rect[1] and rect[3] <= o[3] for o in out):
            out = [o for o in out if not (rect[0] <= o[0] and o[2] <= rect[2] and rect[1] <= o[1] and o[3] <= rect[3])]
            out.append(rect)
    return out


def door_cells(layout, rect, owned):
    """The cells of a model's rectangle that are doors (MB_ANIMATED_DOOR or
    MB_NON_ANIMATED_DOOR in their tileset's attributes): [(i, j)]."""
    import auto_buildings
    names = auto_buildings.behaviours()
    attrs_p = vb.read_u16(layout.attrs[layout.primary])
    attrs_s = vb.read_u16(layout.attrs[layout.secondary])
    x, y, w, h = rect
    out = []
    for j in range(h):
        for i in range(w):
            if owned is not None and (i, j) not in owned:
                continue
            if not (0 <= x + i < layout.w and 0 <= y + j < layout.h):
                continue
            m = layout.metatile(x + i, y + j)
            a = attrs_p[m] if m < vb.NUM_PRIMARY else attrs_s[m - vb.NUM_PRIMARY]
            if names.get(a & 0xFF) in ("MB_ANIMATED_DOOR", "MB_NON_ANIMATED_DOOR"):
                out.append((i, j))
    return out


def detail_model(model):
    """facade_details for a model of an outdoor building: its doors found in
    the layout it was read from. Sets model.details (the art rectangles
    changed) and returns how many there are."""
    spec = model.spec
    x, y, w, h = spec["rect"]
    layout = model.layout
    doors = []
    for (i, j) in door_cells(layout, spec["rect"], model.owned):
        r = doorway(layout, x + i, y + j)
        if r is not None:
            doors.append((r[0] - x * 16, r[1] - y * 16, r[2] - x * 16, r[3] - y * 16))
    model.details = facade_details(model, doors)
    return len(model.details)
