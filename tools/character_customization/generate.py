#!/usr/bin/env python3
"""Character selection data for the native PC build (PORTABLE only).

Reads the approved character-customization library (the `charcustom` workspace,
checkpoint a05e549 "integration-final") and writes the engine's data for the
selectable roster: 32 presets, kit 1 (builds A and B) and its three class looks.

Nothing is drawn or redrawn here. Every frame is the library's own pixels:
  * presets: the frames the library's state candidates name (the first permitted
    candidate of each state, as the integration guide says);
  * kit 1 and the class looks: composed by the library's own C compositor
    (codecs/c/trainer_layered.c, bit-exact with its Python reference) from the
    exported layer atlases, exactly as INTEGRATION_GUIDE.md section 7 asks.
What changes is only the container the GBA picture needs:
  * placement on the engine's sprite canvas by the library's anchors (ground
    pivot on the tile's ground line, surfing frames by their seat on the surf
    blob's saddle: the corrected ground anchors, 2026-10-12);
  * 4bpp tiles with one 16-colour palette per character (colours converted to
    the GBA's 15-bit colour; where a character uses more than 15 colours the
    least visible ones are merged into their nearest neighbour - measured and
    listed in the manifest);
  * battle pictures on the 64x64 trainer-picture canvas (a bigger front is cut,
    never scaled: the measured loss is in the manifest; the DS throws' 80x80
    backs keep their whole picture in six OAM pieces, and a front up to 72x80
    is shown whole on the Trainer Card).

Outputs (all generated, all committed):
  include/constants/character_customization.h       counts, ids, trainer pic ids
  src/data/character_customization/overworld.h     frames, palettes, anims, roster
  src/data/character_customization/trainer_pics.h  battle / Trainer Card pictures
  tools/character_customization/manifest.json      sources, hashes, every decision

Usage:
  python3 tools/character_customization/generate.py --library ../charcustom
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path

import numpy as np
from PIL import Image

ENGINE = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

OUT_CONSTANTS = ENGINE / "include/constants/character_customization.h"
OUT_OVERWORLD = ENGINE / "src/data/character_customization/overworld.h"
OUT_PICS = ENGINE / "src/data/character_customization/trainer_pics.h"
OUT_MANIFEST = HERE / "manifest.json"
IDS_FILE = HERE / "ids.json"
ANIMS_FILE = ENGINE / "src/data/object_events/object_event_anims.h"

DIRS = ("south", "north", "west", "east")

# ---------------------------------------------------------------------------
# Engine groups: one object-event graphics set per player avatar state.
# `table` is the vanilla anim table whose structure (anim indices, durations,
# loops, ends) is kept; only the frames it shows are the character's.
GROUPS = OrderedDict([
    ("normal",     {"table": "sAnimTable_BrendanMayNormal", "sizes": [(16, 32), (32, 32)], "tracks": "TRACKS_FOOT"}),
    ("mach",       {"table": "sAnimTable_Standard", "sizes": [(32, 32)], "tracks": "TRACKS_BIKE_TIRE"}),
    ("acro",       {"table": "sAnimTable_AcroBike", "sizes": [(32, 32)], "tracks": "TRACKS_BIKE_TIRE"}),
    ("surf",       {"table": "sAnimTable_Surfing", "sizes": [(32, 32)], "tracks": "TRACKS_FOOT"}),
    ("field",      {"table": "sAnimTable_FieldMove", "sizes": [(32, 32)], "tracks": "TRACKS_FOOT"}),
    ("fish",       {"table": "sAnimTable_Fishing", "sizes": [(32, 32)], "tracks": "TRACKS_FOOT"}),
    ("watering",   {"table": "sAnimTable_Standard", "sizes": [(32, 32)], "tracks": "TRACKS_FOOT"}),
    ("decorating", {"table": "sAnimTable_Inanimate", "sizes": [(16, 32), (32, 32)], "tracks": "TRACKS_FOOT"}),
])
# The sprite canvases are the ones the engine's own avatar graphics use (16 x 32
# walking, 32 x 32 everything else; walking frames too wide for 16 px take 32 x 32):
# field effects, shadows and the 3D view then place the character exactly like the
# vanilla trainer.
GROUP_ENUM = {g: "CC_GROUP_" + g.upper() for g in GROUPS}

# Compact labels for the in-game selection rows (the full names stay in the
# library; these only have to fit the GBA picture's small font).
SHORT = {
    "frlg-leaf": ("LEAF", "FireRed/LeafGreen"),
    "tah-green": ("LEAF", "Green - kwenio"),
    "frlg-red": ("RED", "FireRed/LeafGreen"),
    "tah-rgby-red": ("RED", "RGBY outfit - hyo"),
    "hgss-ethan": ("ETHAN", "HeartGold/SoulSilver"),
    "tah-gold": ("GOLD", "Gen 2 outfit - hyo"),
    "hgss-lyra": ("LYRA", "HeartGold/SoulSilver"),
    "e-brendan": ("BRENDAN", "Emerald"),
    "tah-oras-brendan": ("BRENDAN", "ORAS outfit - Kasen"),
    "tah-frlg-brendan": ("BRENDAN", "FRLG style - Pawkkie"),
    "tah-hyo-brendan": ("BRENDAN", "hyo remaster"),
    "tah-hyo-rs-brendan": ("BRENDAN", "R/S outfit - hyo"),
    "tah-oras-brendan-v2": ("BRENDAN", "ORAS v2 - Kasen"),
    "tah-poffin-brendan": ("BRENDAN", "FRLG style - Poffin"),
    "tah-hyo-dpp-oras-brendan": ("BRENDAN", "ORAS DPP style - hyo"),
    "tah-poffin-oras-brendan": ("BRENDAN", "ORAS FRLG - Poffin"),
    "tah-poffin-rs-brendan": ("BRENDAN", "R/S FRLG - Poffin"),
    "e-may": ("MAY", "Emerald"),
    "tah-oras-may": ("MAY", "ORAS outfit - Kasen"),
    "tah-frlg-may": ("MAY", "ORAS FRLG - Pawkkie"),
    "tah-hyo-may": ("MAY", "hyo remaster"),
    "tah-poffin-may": ("MAY", "FRLG style - Poffin"),
    "tah-poffin-oras-may": ("MAY", "ORAS FRLG - Poffin"),
    "tah-poffin-rs-may": ("MAY", "R/S FRLG - Poffin"),
    "e-noland": ("NOLAND", "Emerald"),
    "dp-dawn": ("DAWN", "Diamond/Pearl"),
    "pt-dawn": ("DAWN", "Platinum"),
    "tah-dawn": ("DAWN", "Gen 3 - spilledpizza"),
    "dp-lucas": ("LUCAS", "Diamond/Pearl"),
    "pt-lucas": ("LUCAS", "Platinum"),
    "tah-lucas": ("LUCAS", "Gen 3 - spilledpizza"),
    "tah-hilbert": ("HILBERT", "Gen 3 - kwenio"),
    "kit1-a": ("KIT 1 - A", "Layered kit, build A"),
    "kit1-b": ("KIT 1 - B", "Layered kit, build B"),
    "look-backpacker-m": ("BACKPACKER", "Kit 1 look, male"),
    "look-backpacker-f": ("BACKPACKER", "Kit 1 look, female"),
    "look-veteran-f": ("VETERAN", "Kit 1 look, female"),
}

# Library portraits that are the engine's own trainer pictures (pixel for pixel:
# the library cut them from this engine): use the engine's picture id.
ENGINE_PICS = {
    "exp-brendan": "TRAINER_PIC_BRENDAN",
    "exp-may": "TRAINER_PIC_MAY",
    "exp-red": "TRAINER_PIC_RED",
    "exp-leaf": "TRAINER_PIC_LEAF",
    "exp-rs-brendan": "TRAINER_PIC_RS_BRENDAN",
    "exp-rs-may": "TRAINER_PIC_RS_MAY",
}

# The two Emerald protagonists are the engine's own avatar graphics: selecting
# them uses the vanilla object-event graphics unchanged (every state native).
VANILLA_AVATAR = {"e-brendan": "MALE", "e-may": "FEMALE"}


def die(msg):
    raise SystemExit("generate.py: " + msg)


def sha256_file(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


# ---------------------------------------------------------------------------
# Colour

def to555(rgb):
    r, g, b = rgb
    return (round(r * 31 / 255), round(g * 31 / 255), round(b * 31 / 255))


def from555(c):
    return tuple((v << 3) | (v >> 2) for v in c)


def hexrgb(s):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def _lab(c555):
    def lin(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in from555(c555))
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883

    def f(t):
        return t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116
    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


def delta_e(a, b):
    la, lb = _lab(a), _lab(b)
    return math.sqrt(sum((p - q) ** 2 for p, q in zip(la, lb)))


def reduce_colours(counts: dict, limit: int = 15):
    """counts: {c555: pixels}. Fewer than `limit` + 1 colours: unchanged.
    Otherwise groups the colours with Ward's method in CIELAB (the merge that
    adds the least squared error first) and gives every group the member colour
    with the smallest pixel-weighted error to the others (a weighted medoid), so
    only colours the art already has survive.
    Returns (mapping c -> kept c, merges [(from, into, deltaE, pixels)])."""
    if len(counts) <= limit:
        return {c: c for c in counts}, []
    lab = {c: _lab(c) for c in counts}
    clusters = [{"m": [c], "n": counts[c], "s": [v * counts[c] for v in lab[c]]} for c in counts]

    def mean(k):
        return [v / k["n"] for v in k["s"]]

    while len(clusters) > limit:
        best = None
        for i in range(len(clusters)):
            mi = mean(clusters[i])
            for j in range(i + 1, len(clusters)):
                mj = mean(clusters[j])
                ni, nj = clusters[i]["n"], clusters[j]["n"]
                cost = ni * nj / (ni + nj) * sum((a - b) ** 2 for a, b in zip(mi, mj))
                if best is None or cost < best[0]:
                    best = (cost, i, j)
        _, i, j = best
        a, b = clusters[i], clusters[j]
        merged = {"m": a["m"] + b["m"], "n": a["n"] + b["n"], "s": [x + y for x, y in zip(a["s"], b["s"])]}
        clusters = [k for t, k in enumerate(clusters) if t not in (i, j)] + [merged]
    mapping, merges = {}, []
    for k in clusters:
        rep = min(k["m"], key=lambda c: (sum(counts[o] * delta_e(c, o) ** 2 for o in k["m"]), c))
        for c in k["m"]:
            mapping[c] = rep
            if c != rep:
                merges.append((c, rep, round(delta_e(c, rep), 2), counts[c]))
    merges.sort(key=lambda m: -m[3])
    return mapping, merges


# ---------------------------------------------------------------------------
# Images: an image is (rgb HxWx3 uint8 array of 555 colours, mask HxW bool)

class Img:
    __slots__ = ("c", "m")

    def __init__(self, c, m):
        self.c = c
        self.m = m

    @property
    def h(self):
        return self.m.shape[0]

    @property
    def w(self):
        return self.m.shape[1]

    def flip(self):
        return Img(self.c[:, ::-1].copy(), self.m[:, ::-1].copy())

    def key(self):
        c = np.where(self.m[..., None], self.c, 0)
        return hashlib.sha1(c.tobytes() + self.m.tobytes() + bytes([self.w, self.h])).hexdigest()


def blank(w, h):
    return Img(np.zeros((h, w, 3), np.uint8), np.zeros((h, w), bool))


def place(src: Img, ax, ay, bx, by, w, h):
    """Put src so that its anchor (ax, ay) lands on (bx, by) of a w x h canvas.
    Returns (img, dx, dy, clipped opaque pixels)."""
    dx = math.floor(bx - ax + 0.5)
    dy = math.floor(by - ay + 0.5)
    out = blank(w, h)
    ys, xs = np.nonzero(src.m)
    nx, ny = xs + dx, ys + dy
    ok = (nx >= 0) & (nx < w) & (ny >= 0) & (ny < h)
    out.m[ny[ok], nx[ok]] = True
    out.c[ny[ok], nx[ok]] = src.c[ys[ok], xs[ok]]
    return out, dx, dy, int((~ok).sum())


# ---------------------------------------------------------------------------
# Library access

class Library:
    def __init__(self, root: Path):
        self.root = root
        self.lib = root / "library"
        self.cat = json.loads((root / "integration/roster/selection_catalogue.json").read_text())
        self._presets = {}
        self._pages = {}
        self._portraits = {}

    def preset(self, pid):
        if pid not in self._presets:
            self._presets[pid] = json.loads((self.lib / "presets" / pid / "preset.json").read_text())
        return self._presets[pid]

    def page(self, pid, i):
        key = (pid, i)
        if key not in self._pages:
            p = self.preset(pid)
            pg = p["pages"][i]
            im = Image.open(self.lib / "presets" / pid / pg["file"])
            if im.mode != "P":
                die(f"{pid} page {i} is not indexed")
            idx = np.array(im, dtype=np.uint8)
            pal = np.array([to555(hexrgb(c)) for c in pg["palette"]] + [(0, 0, 0)] * (256 - len(pg["palette"])),
                           dtype=np.uint8)
            self._pages[key] = (idx, pal)
        return self._pages[key]

    def frame(self, pid, fid) -> Img:
        p = self.preset(pid)
        f = next(fr for fr in p["frames"] if fr["id"] == fid)
        idx, pal = self.page(pid, f["page"])
        cell = idx[f["y"]:f["y"] + f["h"], f["x"]:f["x"] + f["w"]]
        return Img(pal[cell], cell != 0)

    def portrait(self, pid):
        if pid not in self._portraits:
            self._portraits[pid] = json.loads((self.lib / "portraits" / pid / "portrait.json").read_text())
        return self._portraits[pid]

    def portrait_frames(self, pid, part):
        pj = self.portrait(pid)
        p = pj["parts"][part]
        im = Image.open(self.lib / "portraits" / pid / p["file"])
        if im.mode != "P":
            die(f"portrait {pid}/{part} is not indexed")
        idx = np.array(im, dtype=np.uint8)
        pal = np.array([to555(hexrgb(c)) for c in p["palette"]] + [(0, 0, 0)] * (256 - len(p["palette"])),
                       dtype=np.uint8)
        out = []
        for f in p["frames"]:
            cell = idx[f["y"]:f["y"] + f["h"], f["x"]:f["x"] + f["w"]]
            out.append(Img(pal[cell], cell != 0))
        return p, out


def first_candidate(preset, state):
    for c in preset["states"].get(state, []):
        if c.get("allowed_by_default"):
            return c
    return None


# ---------------------------------------------------------------------------
# Vanilla anim tables (structure kept, frames remapped)

CMD_RE = re.compile(r"ANIMCMD_(FRAME|LOOP|JUMP|END)\s*(?:\(([^)]*)\))?")


def parse_anims(path: Path):
    text = path.read_text()
    cmds = {}
    for m in re.finditer(r"static const union AnimCmd (\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        name, body = m.group(1), m.group(2)
        out = []
        for c in CMD_RE.finditer(body):
            kind, args = c.group(1), c.group(2)
            if kind == "FRAME":
                parts = [a.strip() for a in args.split(",")]
                img, dur = int(parts[0]), int(parts[1])
                hf = any(re.match(r"\.hFlip\s*=\s*TRUE", a) for a in parts[2:])
                vf = any(re.match(r"\.vFlip\s*=\s*TRUE", a) for a in parts[2:])
                out.append(("frame", img, dur, hf, vf))
            elif kind == "LOOP":
                out.append(("loop", int(args)))
            elif kind == "JUMP":
                out.append(("jump", int(args)))
            else:
                out.append(("end",))
        cmds[name] = out
    tables = {}
    for m in re.finditer(r"static const union AnimCmd \*const (sAnimTable_\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        name, body = m.group(1), m.group(2)
        entries = []
        for e in re.finditer(r"\[(\w+)\]\s*=\s*(\([^)]*\)|\w+)", body):
            idx, val = e.group(1), e.group(2)
            if val.startswith("("):  # (IS_FRLG ? a : b): this is the Emerald build
                val = re.match(r"\(\s*IS_FRLG\s*\?\s*\w+\s*:\s*(\w+)\s*\)", val).group(1)
            entries.append((idx, val))
        tables[name] = entries
    return cmds, tables


# ---------------------------------------------------------------------------
# A group under construction

class Group:
    def __init__(self, name):
        self.name = name
        self.spec = GROUPS[name]
        self.vanilla = {}        # (vanilla frame, hflip) -> (Img, anchor (ax, ay), target(bx_rel, by_rel), note)
        self.sources = {}        # what each vanilla frame came from (for the manifest)
        self.fallback = None     # None = native frames, else a text
        self.size = None
        self.images = []         # unique Img on the chosen canvas
        self.map = {}            # (vanilla frame, hflip) -> (image index, hflip)
        self.clipped = 0
        self.placement = {}

    def put(self, vframe, img, anchor, target, east=False, hflip=False, src=None):
        """target: (x offset from the canvas centre line, y offset from the canvas bottom)."""
        self.vanilla[(vframe, east)] = (img, anchor, target, hflip)
        if src is not None:
            self.sources[f"{vframe}{'E' if east else ''}"] = src

    def build(self, canvas_sizes=None):
        sizes = canvas_sizes or self.spec["sizes"]
        best = None
        for (w, h) in sizes:
            placed = {}
            clipped = 0
            for key, (img, (ax, ay), (tx, ty), hflip) in self.vanilla.items():
                im, dx, dy, cl = place(img, ax, ay, w / 2 + tx, h + ty, w, h)
                placed[key] = (im, hflip, dx, dy)
                clipped += cl
            if best is None or clipped < best[2]:
                best = ((w, h), placed, clipped)
            if clipped == 0:
                break
        (w, h), placed, clipped = best
        self.size, self.clipped = (w, h), clipped
        keys = {}
        for key, (im, hflip, dx, dy) in sorted(placed.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            k = im.key()
            if k not in keys:
                keys[k] = len(self.images)
                self.images.append(im)
            self.map[key] = (keys[k], hflip)
            self.placement[f"{key[0]}{'E' if key[1] else ''}"] = [dx, dy]

    def lookup(self, vframe, vflip):
        """The image and flip for a vanilla anim command showing frame vframe (flipped or not)."""
        if vflip and (vframe, True) in self.map:
            img, hflip = self.map[(vframe, True)]
            return img, hflip
        img, hflip = self.map[(vframe, False)]
        return img, (hflip != vflip)


def seq(cand, d):
    return cand["dirs"][d]


def is_mirror(cand):
    e, w = cand["dirs"]["east"], cand["dirs"]["west"]
    return e["frames"] == w["frames"] and all(e["hflip"]) and not any(w["hflip"])


def walk_steps(frames):
    """[a, n, b, n] -> (a, n, b); [n, a, n, b] -> (a, n, b); a single frame -> (x, x, x)."""
    if len(frames) == 1:
        return frames[0], frames[0], frames[0]
    if len(frames) == 4 and frames[1] == frames[3]:
        return frames[0], frames[1], frames[2]
    if len(frames) == 4 and frames[0] == frames[2]:
        return frames[1], frames[0], frames[3]
    if len(frames) == 4:
        # four different frames (the DS bicycle's pedalling): the engine's cycle
        # shows step, rest, step, rest - the fourth frame is left out
        return frames[0], frames[1], frames[2]
    die(f"unexpected 4-step sequence {frames}")


# ---------------------------------------------------------------------------
# Presets

def preset_groups(L: Library, pid: str, target_seat: dict, report: dict):
    p = L.preset(pid)
    groups = OrderedDict()
    fr = lambda fid: L.frame(pid, fid)  # noqa: E731

    def cand(state):
        return first_candidate(p, state)

    idle, walk, run = cand("idle"), cand("walk"), cand("run")
    if not (idle and walk and run):
        die(f"{pid}: no idle/walk/run")
    piv = lambda c: tuple(c["anchors"]["pivot"])  # noqa: E731
    ground = (0, 0)

    def ground_dirs(g, c_idle, c_move, idle_frames, step_frames, label):
        """idle_frames / step_frames: per direction (S, N, W) the vanilla frame numbers."""
        for di, d in enumerate(("south", "north", "west")):
            f_idle = seq(c_idle, d)["frames"][0]
            g.put(idle_frames[di], fr(f_idle), piv(c_idle), ground, src=f"{label}:{d}:{f_idle}")
            a, n, b = walk_steps(seq(c_move, d)["frames"])
            g.put(step_frames[di][0], fr(a), piv(c_move), ground, src=f"{label}:{d}:{a}")
            g.put(step_frames[di][1], fr(b), piv(c_move), ground, src=f"{label}:{d}:{b}")
        if not is_mirror(c_idle) or not is_mirror(c_move):
            e_idle = seq(c_idle, "east")
            g.put(idle_frames[2], fr(e_idle["frames"][0]), piv(c_idle), ground, east=True,
                  hflip=e_idle["hflip"][0], src=f"{label}:east:{e_idle['frames'][0]}")
            em = seq(c_move, "east")
            a, n, b = walk_steps(em["frames"])
            hf = em["hflip"][0]
            g.put(step_frames[2][0], fr(a), piv(c_move), ground, east=True, hflip=hf, src=f"{label}:east:{a}")
            g.put(step_frames[2][1], fr(b), piv(c_move), ground, east=True, hflip=hf, src=f"{label}:east:{b}")

    # normal: walking 0-8, running 9-17 (Emerald layout)
    g = Group("normal")
    ground_dirs(g, idle, walk, (0, 1, 2), ((3, 4), (5, 6), (7, 8)), "walk")
    for di, d in enumerate(("south", "north", "west")):
        a, n, b = walk_steps(seq(run, d)["frames"])
        g.put(9 + di, fr(n), piv(run), ground, src=f"run:{d}:{n}")
        g.put(12 + 2 * di, fr(a), piv(run), ground, src=f"run:{d}:{a}")
        g.put(13 + 2 * di, fr(b), piv(run), ground, src=f"run:{d}:{b}")
    if not is_mirror(run):
        er = seq(run, "east")
        a, n, b = walk_steps(er["frames"])
        hf = er["hflip"][0]
        g.put(11, fr(n), piv(run), ground, east=True, hflip=hf, src=f"run:east:{n}")
        g.put(16, fr(a), piv(run), ground, east=True, hflip=hf, src=f"run:east:{a}")
        g.put(17, fr(b), piv(run), ground, east=True, hflip=hf, src=f"run:east:{b}")
    groups["normal"] = g

    # bikes: Mach (0-8) and the Acro Bike's wheelie poses (no art in the
    # library: the Mach Bike frames of the same direction stand in)
    bi, bm = cand("bike_idle"), cand("bike_move")
    g = Group("mach")
    if bi and bm:
        ground_dirs(g, bi, bm, (0, 1, 2), ((3, 4), (5, 6), (7, 8)), "bike")
    else:
        ground_dirs(g, idle, walk, (0, 1, 2), ((3, 4), (5, 6), (7, 8)), "walk")
        g.fallback = "no bicycle frames in the library: the walking frames ride the bike"
    groups["mach"] = g
    groups["acro"] = acro_from(g)

    # surfing: sitting (0-2), moving (3-8), getting on/off (9-11), by the seat
    si, sm, smt = cand("surf_idle"), cand("surf_move"), cand("surf_mount")
    if not si:
        die(f"{pid}: no surf_idle")
    g = Group("surf")
    seat = si["anchors"]["seat"]
    for di, d in enumerate(("south", "north", "west", "east")):
        east = d == "east"
        if east and is_mirror(si):
            continue
        s = seq(si, d)
        f_sit = s["frames"][0]
        anc = tuple(seat[d])
        tgt = target_seat[d]
        vd = 2 if east else di
        hf = s["hflip"][0] if east else False
        g.put(vd, fr(f_sit), anc, tgt, east=east, hflip=hf, src=f"surf_idle:{d}:{f_sit}")
        if sm:
            a, n, b = walk_steps(seq(sm, d)["frames"])
        else:
            a = b = f_sit
        g.put((7 if east else 3 + 2 * di), fr(a), anc, tgt, east=east, hflip=hf, src=f"surf_move:{d}:{a}")
        g.put((8 if east else 4 + 2 * di), fr(b), anc, tgt, east=east, hflip=hf, src=f"surf_move:{d}:{b}")
        if smt:
            m = seq(smt, d)["frames"][0]
            g.put((11 if east else 9 + di), fr(m), anc, tgt, east=east, hflip=hf, src=f"surf_mount:{d}:{m}")
        else:
            g.put((11 if east else 9 + di), fr(f_sit), anc, tgt, east=east, hflip=hf, src=f"surf_idle:{d}:{f_sit}")
    if not smt:
        g.fallback = "no hop-on frames: getting on and off the surf mount shows the seated frame"
    groups["surf"] = g

    # field move (HM use): 5 frames facing south
    fm = cand("field_move")
    g = Group("field")
    if fm:
        fs = seq(fm, "south")["frames"]
        for i in range(5):
            g.put(i, fr(fs[min(i, len(fs) - 1)]), piv(fm), ground, src=f"field_move:south:{fs[min(i, len(fs) - 1)]}")
    else:
        f0 = seq(idle, "south")["frames"][0]
        for i in range(5):
            g.put(i, fr(f0), piv(idle), ground, src=f"idle:south:{f0}")
        g.fallback = "no field-move pose: the standing frame facing south"
    groups["field"] = g

    # fishing: West 0-3, North 4-7, South 8-11 in casting order
    fc = cand("fish_cast")
    if not fc:
        die(f"{pid}: no fish_cast")
    g = Group("fish")
    for d, base in (("west", 0), ("north", 4), ("south", 8)):
        fs = seq(fc, d)["frames"]
        if len(fs) != 4:
            die(f"{pid}: fish_cast {d} has {len(fs)} frames")
        for i, f in enumerate(fs):
            g.put(base + i, fr(f), piv(fc), ground, src=f"fish_cast:{d}:{f}")
    if not is_mirror(fc):
        es = seq(fc, "east")
        for i, f in enumerate(es["frames"]):
            g.put(i, fr(f), piv(fc), ground, east=True, hflip=es["hflip"][i], src=f"fish_cast:east:{f}")
    groups["fish"] = g

    groups["watering"] = standing_set(groups["normal"], "watering")
    groups["decorating"] = decorating_from(groups["normal"])
    return groups


def standing_set(normal: Group, name: str) -> Group:
    """Standard-table frames 0-8 that only stand (watering: no can art)."""
    g = Group(name)
    for vf in range(9):
        d = (0, 1, 2)[0 if vf in (0, 3, 4) else 1 if vf in (1, 5, 6) else 2]
        img, anc, tgt, hf = normal.vanilla[(d, False)]
        g.put(vf, img, anc, tgt)
        if (d, True) in normal.vanilla and d == 2:
            img, anc, tgt, hf = normal.vanilla[(2, True)]
            g.put(vf, img, anc, tgt, east=True, hflip=hf)
    g.fallback = "no watering frames: the standing frames"
    return g


def decorating_from(normal: Group) -> Group:
    g = Group("decorating")
    img, anc, tgt, hf = normal.vanilla[(0, False)]
    g.put(0, img, anc, tgt)
    g.fallback = "the standing frame facing south (decorating pose)"
    return g


def acro_from(mach: Group) -> Group:
    """Acro Bike: frames 0-8 as the Mach Bike; wheelie poses 9-26 show the Mach
    Bike frame of the same direction (idle for standing poses, the steps for the
    moving wheelie)."""
    g = Group("acro")
    for key, v in mach.vanilla.items():
        g.vanilla[key] = v
    rows = {0: (9, 10, 11, 12), 1: (13, 14, 15, 16), 2: (17, 18, 19, 20)}
    moving = {0: (21, 22), 1: (23, 24), 2: (25, 26)}
    steps = {0: (3, 4), 1: (5, 6), 2: (7, 8)}
    for d in (0, 1, 2):
        for vf in rows[d]:
            for east in (False, True):
                if (d, east) in mach.vanilla:
                    g.vanilla[(vf, east)] = mach.vanilla[(d, east)]
        for vf, sf in zip(moving[d], steps[d]):
            for east in (False, True):
                if (sf, east) in mach.vanilla:
                    g.vanilla[(vf, east)] = mach.vanilla[(sf, east)]
    g.fallback = ("no Acro Bike frames in the library: the Mach Bike frames of the same direction "
                  "stand in for the wheelie poses") + ("" if not mach.fallback else "; " + mach.fallback)
    return g


# ---------------------------------------------------------------------------
# Kit 1 (layered, original art): composed by the library's C compositor

class Kit1:
    def __init__(self, L: Library, tmp: Path):
        self.L = L
        sys.path.insert(0, str(L.root / "pipeline"))
        sys.path.insert(0, str(L.root / "codecs" / "c"))
        import test_layered as TL  # the library's own harness (C compositor + atlases)
        from ccpipe import appearance as A
        from ccpipe.components import compose as C
        self.TL, self.A, self.C = TL, A, C
        self.comp = json.loads((L.lib / "components" / "components.json").read_text())
        self.reg = A.Registry.load(L.lib / "registry.json")
        self.lib = TL.build_lib(tmp)
        self.atl = TL.Atlases(self.comp)
        self.p2atl = TL.Atlases(self.comp, "portrait2_files")
        self.poses = self.comp["poses"]

    def palette(self, rec):
        import ctypes
        n = self.comp["palette"]["size"]
        pal = (ctypes.c_uint8 * (n * 3))()
        rc = self.lib.tlt_palette(rec, len(rec), pal)
        if rc != 0:
            die(f"tl_palette failed {rc}")
        return [tuple(pal[i * 3:i * 3 + 3]) for i in range(n)]

    def frame(self, rec, facing, pose):
        import ctypes
        buf = (ctypes.c_uint8 * (32 * 32))()
        fi = ("S", "N", "W", "E").index(facing)
        rc = self.lib.tlt_compose(rec, len(rec), fi, self.poses.index(pose), self.atl.cfn, None, buf)
        if rc != 0:
            die(f"tl_compose failed {rc}")
        return np.frombuffer(bytes(buf), dtype=np.uint8).reshape(32, 32)

    def portrait2(self, rec, view):
        import ctypes
        buf = (ctypes.c_uint8 * (80 * 80))()
        vi = self.comp["portrait2"]["views"].index(view)
        rc = self.lib.tlt_compose_portrait2(rec, len(rec), vi, self.p2atl.cfn, None, buf)
        if rc != 0:
            die(f"tl_compose_portrait2 failed {rc}")
        return np.frombuffer(bytes(buf), dtype=np.uint8).reshape(80, 80)

    def img(self, idx, pal):
        c = np.array([to555(pal[i]) if i < len(pal) else (0, 0, 0) for i in range(256)], np.uint8)
        return Img(c[idx], idx != 0)

    def groups(self, rec, body, target_seat):
        pal = self.palette(rec)
        comp = self.comp
        st = comp["states"]
        anchors = comp["anchors"][body]
        ground = (0, 0)
        pivot = tuple(comp["frame"]["pivot"])
        cache = {}

        def fr(facing, pose):
            k = (facing, pose)
            if k not in cache:
                cache[k] = self.img(self.frame(rec, facing, pose), pal)
            return cache[k]

        def facing_of(state, d):
            return st[state]["dirs"][d]["facing"], st[state]["dirs"][d]["poses"]

        groups = OrderedDict()
        g = Group("normal")
        for di, d in enumerate(("south", "north", "west")):
            fa, poses = facing_of("idle", d)
            g.put(di, fr(fa, poses[0]), pivot, ground, src=f"idle:{d}:{poses[0]}")
            fa, poses = facing_of("walk", d)
            a, n, b = walk_steps(poses)
            g.put(3 + 2 * di, fr(fa, a), pivot, ground, src=f"walk:{d}:{a}")
            g.put(4 + 2 * di, fr(fa, b), pivot, ground, src=f"walk:{d}:{b}")
            fa, poses = facing_of("run", d)
            a, n, b = walk_steps(poses)
            g.put(9 + di, fr(fa, n), pivot, ground, src=f"run:{d}:{n}")
            g.put(12 + 2 * di, fr(fa, a), pivot, ground, src=f"run:{d}:{a}")
            g.put(13 + 2 * di, fr(fa, b), pivot, ground, src=f"run:{d}:{b}")
        groups["normal"] = g
        g = Group("mach")
        for key, v in groups["normal"].vanilla.items():
            if key[0] <= 8:
                g.vanilla[key] = v
        g.fallback = "kit 1 has no bicycle frames: the walking frames ride the bike"
        groups["mach"] = g
        groups["acro"] = acro_from(g)
        g = Group("surf")
        for di, d in enumerate(("south", "north", "west")):
            fa, poses = facing_of("surf_idle", d)
            seat = tuple(anchors[fa][poses[0]]["seat"])
            im = fr(fa, poses[0])
            for vf in (di, 3 + 2 * di, 4 + 2 * di, 9 + di):
                g.put(vf, im, seat, target_seat[d], src=f"surf_idle:{d}:{poses[0]}")
        g.fallback = "kit 1 has no hop-on frames: getting on and off the surf mount shows the seated frame"
        groups["surf"] = g
        g = Group("field")
        fa, poses = facing_of("idle", "south")
        for i in range(5):
            g.put(i, fr(fa, poses[0]), pivot, ground, src="idle:south:stand")
        g.fallback = "kit 1 has no field-move pose: the standing frame facing south"
        groups["field"] = g
        g = Group("fish")
        for d, base in (("west", 0), ("north", 4), ("south", 8)):
            fa, poses = facing_of("fish_cast", d)
            for i, pose in enumerate(poses):
                g.put(base + i, fr(fa, pose), pivot, ground, src=f"fish_cast:{d}:{pose}")
        groups["fish"] = g
        groups["watering"] = standing_set(groups["normal"], "watering")
        groups["decorating"] = decorating_from(groups["normal"])
        return groups, pal


# ---------------------------------------------------------------------------
# 4bpp

def tiles_4bpp(idx: np.ndarray) -> bytes:
    h, w = idx.shape
    out = bytearray()
    for ty in range(h // 8):
        for tx in range(w // 8):
            for y in range(8):
                row = idx[ty * 8 + y, tx * 8:tx * 8 + 8]
                for x in range(0, 8, 2):
                    out.append((int(row[x]) & 15) | ((int(row[x + 1]) & 15) << 4))
    return bytes(out)


def lz77_literal(data: bytes) -> bytes:
    """GBA BIOS LZ77 (type 0x10) stream made only of literal blocks."""
    out = bytearray(((len(data) << 8) | 0x10).to_bytes(4, "little"))
    for i in range(0, len(data), 8):
        out.append(0)
        out += data[i:i + 8]
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def u32_words(b: bytes):
    if len(b) % 4:
        b = b + bytes(4 - len(b) % 4)
    return [int.from_bytes(b[i:i + 4], "little") for i in range(0, len(b), 4)]


def c_words(name, words, static=True, per_line=8):
    lines = [f"{'static ' if static else ''}const u32 {name}[] = {{"]
    for i in range(0, len(words), per_line):
        lines.append("    " + ", ".join(f"0x{w:08X}" for w in words[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_pal(name, pal16):
    vals = [(c[2] << 10) | (c[1] << 5) | c[0] for c in pal16]
    return f"static const u16 {name}[16] = {{" + ", ".join(f"0x{v:04X}" for v in vals) + "};"


def gba_text(s: str) -> str:
    bad = set(s) - set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 /-.:'(),")
    if bad:
        die(f"unsupported characters {bad} in {s!r}")
    return s


# ---------------------------------------------------------------------------
# Palettes

def build_palette(groups_list, report_key, report):
    counts = {}
    for groups in groups_list:
        for g in groups.values():
            for im in g.images:
                cs = im.c[im.m]
                for c, n in zip(*np.unique(cs.reshape(-1, 3), axis=0, return_counts=True)):
                    counts[tuple(int(v) for v in c)] = counts.get(tuple(int(v) for v in c), 0) + int(n)
    mapping, merges = reduce_colours(counts, 15)
    kept = sorted(set(mapping.values()), key=lambda c: (-counts_of(c, counts, mapping), c))
    index = {c: i + 1 for i, c in enumerate(kept)}
    report[report_key] = {
        "colours": len(counts),
        "palette": len(kept),
        "merged": [{"from": "#%02x%02x%02x" % from555(a), "into": "#%02x%02x%02x" % from555(b), "delta_e": d,
                    "pixels": n} for a, b, d, n in merges],
    }
    pal16 = [(0, 0, 0)] + kept + [(0, 0, 0)] * (15 - len(kept))
    return pal16, {c: index[mapping[c]] for c in mapping}


def counts_of(c, counts, mapping):
    return sum(n for k, n in counts.items() if mapping[k] == c)


def indexed(im: Img, lut) -> np.ndarray:
    out = np.zeros((im.h, im.w), np.uint8)
    ys, xs = np.nonzero(im.m)
    for y, x in zip(ys, xs):
        out[y, x] = lut[tuple(int(v) for v in im.c[y, x])]
    return out


# ---------------------------------------------------------------------------
# Trainer pictures

def fit64(frames, pivot_feet, keep="top", report=None, label=""):
    """Cut frames (same size) to 64x64: one window for every frame. Width: the
    window that keeps the most opaque pixels; height: from the top of the
    figure (the GBA battle box hides below; a Trainer Card shows the head)."""
    h, w = frames[0].h, frames[0].w
    if w <= 64 and h <= 64:
        out = []
        for f in frames:
            im = blank(64, 64)
            ox = (64 - w) // 2 if w < 64 else 0
            if pivot_feet is not None and w < 64:
                ox = int(math.floor(32 - pivot_feet[0] + 0.5))
                ox = max(0, min(64 - w, ox))
            oy = 64 - h
            im.c[oy:oy + h, ox:ox + w] = f.c
            im.m[oy:oy + h, ox:ox + w] = f.m
            out.append(im)
        if report is not None:
            report[label] = {"cell": [w, h], "window": None, "lost_pixels": 0}
        return out
    total = [int(f.m.sum()) for f in frames]
    union = np.zeros((h, w), bool)
    for f in frames:
        union |= f.m
    ys, xs = np.nonzero(union)
    top = int(ys.min())
    oy = min(top, max(0, h - 64)) if keep == "top" else max(0, h - 64)
    best = None
    for ox in range(0, max(1, w - 64 + 1)):
        kept = sum(int(f.m[oy:oy + 64, ox:ox + 64].sum()) for f in frames)
        if best is None or kept > best[0]:
            best = (kept, ox)
    kept, ox = best
    out = []
    for f in frames:
        im = blank(64, 64)
        cw, ch = min(64, w - ox), min(64, h - oy)
        im.c[0:ch, 0:cw] = f.c[oy:oy + ch, ox:ox + cw]
        im.m[0:ch, 0:cw] = f.m[oy:oy + ch, ox:ox + cw]
        out.append(im)
    if report is not None:
        lost = [t - int(o.m.sum()) for t, o in zip(total, out)]
        report[label] = {"cell": [w, h], "window": [ox, oy, 64, 64], "lost_pixels": sum(lost),
                         "lost_percent_worst_frame": round(100 * max(l / t for l, t in zip(lost, total) if t), 1)}
    return out


CARD_W, CARD_H = 72, 80

# 80 x 80 battle backs: one 64 x 64 OAM piece and five smaller ones (100 tiles,
# frame data stored piece by piece). (x, y, w, h) inside the 80 x 80 picture.
BACK80_PIECES = [(0, 0, 64, 64), (64, 0, 16, 32), (64, 32, 16, 32), (0, 64, 32, 16), (32, 64, 32, 16), (64, 64, 16, 16)]


def tiles_pieces(idx: np.ndarray, pieces) -> bytes:
    return b"".join(tiles_4bpp(idx[y:y + h, x:x + w]) for (x, y, w, h) in pieces)


def fit80(frames, pivot_feet, report=None, label=""):
    """DS battle backs (80 x 80, 8 frames) keep their whole picture."""
    w, h = frames[0].w, frames[0].h
    if w > 80 or h > 80:
        die(f"{label}: {w}x{h} is larger than 80x80")
    out = []
    for f in frames:
        im = blank(80, 80)
        ox, oy = (80 - w) // 2, 80 - h
        im.c[oy:oy + h, ox:ox + w] = f.c
        im.m[oy:oy + h, ox:ox + w] = f.m
        out.append(im)
    if report is not None:
        report[label] = {"cell": [w, h], "window": None, "lost_pixels": 0, "canvas": [80, 80]}
    return out


def fit_card(im, pivot_feet, report=None, label=""):
    """A front for the Trainer Card's 72 x 80 picture window."""
    w, h = im.w, im.h
    if h > CARD_H:
        die(f"{label}: taller than {CARD_H}")
    out = blank(CARD_W, CARD_H)
    if w <= CARD_W:
        ox = (CARD_W - w) // 2
        if pivot_feet is not None:
            ox = max(0, min(CARD_W - w, int(math.floor(CARD_W / 2 - pivot_feet[0] + 0.5))))
        out.c[CARD_H - h:, ox:ox + w] = im.c
        out.m[CARD_H - h:, ox:ox + w] = im.m
        lost = 0
    else:
        best = max(range(0, w - CARD_W + 1), key=lambda x: (int(im.m[:, x:x + CARD_W].sum()), -abs(x - (w - CARD_W) // 2)))
        out.c[CARD_H - h:, :] = im.c[:, best:best + CARD_W]
        out.m[CARD_H - h:, :] = im.m[:, best:best + CARD_W]
        lost = int(im.m.sum()) - int(out.m.sum())
    if report is not None:
        report[label] = {"cell": [w, h], "canvas": [CARD_W, CARD_H], "lost_pixels": lost}
    return out


def pic_palette(frames, report, label):
    counts = {}
    for im in frames:
        for c, n in zip(*np.unique(im.c[im.m].reshape(-1, 3), axis=0, return_counts=True)):
            k = tuple(int(v) for v in c)
            counts[k] = counts.get(k, 0) + int(n)
    mapping, merges = reduce_colours(counts, 15)
    kept = sorted(set(mapping.values()), key=lambda c: (-counts_of(c, counts, mapping), c))
    index = {c: i + 1 for i, c in enumerate(kept)}
    report.setdefault(label, {}).update({
        "colours": len(counts), "palette": len(kept),
        "merged": [{"from": "#%02x%02x%02x" % from555(a), "into": "#%02x%02x%02x" % from555(b),
                    "delta_e": d, "pixels": n} for a, b, d, n in merges]})
    pal16 = [(0, 0, 0)] + kept + [(0, 0, 0)] * (15 - len(kept))
    return pal16, {c: index[mapping[c]] for c in mapping}


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--library", default=os.environ.get("CHARCUSTOM_LIBRARY"), required=False)
    ap.add_argument("--check", action="store_true", help="fail if the outputs would change")
    args = ap.parse_args()
    if not args.library:
        die("pass --library <charcustom workspace> (or set CHARCUSTOM_LIBRARY)")
    root = Path(args.library).resolve()
    L = Library(root)
    cat = L.cat
    report = OrderedDict()
    lib_head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()

    # Target seat on the surf blob: the Emerald protagonist's own surfing frames
    # (the blob is drawn for them). Relative to the canvas bottom-centre.
    eb = L.preset("e-brendan")
    si = first_candidate(eb, "surf_idle")
    fw, fh = si["anchors"]["frame"]
    target_seat = {d: (si["anchors"]["seat"][d][0] - fw / 2, si["anchors"]["seat"][d][1] - fh) for d in DIRS}

    # stable engine ids (append-only)
    ids = json.loads(IDS_FILE.read_text()) if IDS_FILE.exists() else {"next": 1, "ids": {}}
    entries = []
    for e in cat["presets"]:
        entries.append(("preset", e["id"], e))
    for body in ("a", "b"):
        entries.append(("kit1", f"kit1-{body}", cat["custom"]["kit1"]["bodies"][body]))
    for lk in cat["custom"]["looks"]:
        entries.append(("look", lk["id"], lk))
    for kind, lid, e in entries:
        if lid not in ids["ids"]:
            ids["ids"][lid] = ids["next"]
            ids["next"] += 1
    if ids["next"] > 255:
        die("more than 255 engine character ids")

    cmds, tables = parse_anims(ANIMS_FILE)
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="ccgen-"))
    kit = Kit1(L, tmp)

    characters = []
    pic_keys = []             # custom trainer pictures, in id order

    def custom_pic(key):
        if key not in pic_keys:
            pic_keys.append(key)
        return key

    for kind, lid, e in entries:
        rep = report.setdefault(lid, OrderedDict())
        ch = {"id": ids["ids"][lid], "lib": lid, "kind": kind}
        if kind == "preset":
            ch.update(gender=e["gender"], registry=e["num"], region=e["region"], art=e["art"],
                      code=e["code"], name=SHORT[lid][0], variant=SHORT[lid][1], full=e["name"])
            if lid in VANILLA_AVATAR:
                ch["vanilla"] = VANILLA_AVATAR[lid]
                rep["overworld"] = "the engine's own Emerald avatar graphics (every state native)"
            else:
                groups = preset_groups(L, lid, target_seat, rep)
                for g in groups.values():
                    g.build()
                pal, lut = build_palette([groups], "palette", rep)
                ch.update(groups=groups, pal=pal, lut=lut)
            ch["front_portrait"] = e["front"]["portrait"]
            ch["back_portrait"] = e["battle_throw"]["portrait"]
            ch["throw"] = e["battle_throw"]
        else:
            if kind == "kit1":
                body = lid[-1]
                rec = kit.A.encode({"mode": "layered", **dict(kit.C.DEFAULT, body=body)}, kit.reg)
                gender = "male" if body == "a" else "female"
                ch.update(full=f"Kit 1, build {body.upper()} (default look)")
            else:
                rec = kit.A.from_text(e["code"])
                gender = e["gender"]
                ch.update(full=f"{e['name']} ({e['gender']}, kit 1 look)", class_portrait=e.get("class_portrait"))
            dec = kit.A.decode(rec, kit.reg)
            body = dec["body"]
            if dec.get("portrait"):
                die(f"{lid}: portrait overrides are not handled")
            ch.update(name=SHORT[lid][0], variant=SHORT[lid][1], gender=gender, registry=0, region="",
                      art="original (kit 1)", code=kit.A.to_text(rec), throw=e["battle_throw"],
                      front_portrait=None, back_portrait=e["battle_throw"]["portrait"], kit_record=rec, kit_body=body)
            groups, kpal = kit.groups(rec, body, target_seat)
            for g in groups.values():
                g.build()
            pal, lut = build_palette([groups], "palette", rep)
            ch.update(groups=groups, pal=pal, lut=lut)
        characters.append(ch)

        # battle back (intro + throw): the engine's own picture or a generated one
        bp = ch["back_portrait"]
        ch["back_pic"] = ENGINE_PICS[bp] if bp in ENGINE_PICS else custom_pic(("back", bp))
        # Trainer Card and opponent-side front
        fp = ch["front_portrait"]
        if fp is None:
            ch["front_pic"] = custom_pic(("kitfront", lid))
        else:
            ch["front_pic"] = ENGINE_PICS[fp] if fp in ENGINE_PICS else custom_pic(("front", fp))
        rep["back_pic"] = ch["back_pic"] if isinstance(ch["back_pic"], str) else f"generated from portrait {bp}"
        rep["front_pic"] = (ch["front_pic"] if isinstance(ch["front_pic"], str)
                            else f"generated from portrait {fp}" if fp else "generated: kit 1 v2 front view of this look")
        if "groups" in ch:
            rep["groups"] = OrderedDict()
            for gname, g in ch["groups"].items():
                rep["groups"][gname] = {"canvas": list(g.size), "images": len(g.images), "clipped_pixels": g.clipped,
                                        "fallback": g.fallback, "true_east": any(k[1] for k in g.vanilla),
                                        "sources": g.sources, "placement_dx_dy": g.placement}

    # custom trainer pictures
    pic_report = OrderedDict()
    pic_data = []
    fronts = {}               # key -> the uncropped front Img (for the Trainer Card)
    for key in pic_keys:
        kind, ref = key
        if kind == "back":
            p, frames = L.portrait_frames(ref, "back")
            if frames[0].w <= 64 and frames[0].h <= 64:
                fitted = fit64(frames, p.get("pivot_feet"), keep="top", report=pic_report, label=f"back:{ref}")
                size = (64, 64)
            else:
                fitted = fit80(frames, p.get("pivot_feet"), report=pic_report, label=f"back:{ref}")
                size = (80, 80)
            anim = p["anim"]
            pal, lut = pic_palette(fitted, pic_report, f"back:{ref}")
            if size == (64, 64):
                data = b"".join(tiles_4bpp(indexed(f, lut)) for f in fitted)
            else:
                data = b"".join(tiles_pieces(indexed(f, lut), BACK80_PIECES) for f in fitted)
            pic_data.append({"key": key, "kind": "back", "ref": ref, "frames": len(fitted), "pal": pal, "data": data,
                             "size": size, "sequence": anim["sequence"], "ticks": anim["ticks"]})
        elif kind == "front":
            p, frames = L.portrait_frames(ref, "front")
            fronts[key] = (frames[0], p.get("pivot_feet"))
            fitted = fit64(frames[:1], p.get("pivot_feet"), keep="top", report=pic_report, label=f"front:{ref}")
            pal, lut = pic_palette(fitted, pic_report, f"front:{ref}")
            pic_data.append({"key": key, "kind": "front", "ref": ref, "pal": pal,
                             "data": tiles_4bpp(indexed(fitted[0], lut))})
        else:  # kit 1 look: its own v2 battle art, front view, composed with the look's palette
            ch = next(c for c in characters if c["lib"] == ref)
            im = kit.img(kit.portrait2(ch["kit_record"], "front"), kit.palette(ch["kit_record"]))
            fronts[key] = (im, None)
            fitted = fit64([im], None, keep="top", report=pic_report, label=f"front:kit:{ref}")
            pal, lut = pic_palette(fitted, pic_report, f"front:kit:{ref}")
            pic_data.append({"key": key, "kind": "front", "ref": f"kit 1 v2 front view of {ref}", "pal": pal,
                             "data": tiles_4bpp(indexed(fitted[0], lut))})

    # Trainer Card pictures: the card's picture window is 72 x 80, so a front
    # bigger than 64 x 64 is shown whole there (bottom-aligned, centred)
    card_data = OrderedDict()
    for key, (im, pivot) in fronts.items():
        if im.w <= 64 and im.h <= 64:
            continue
        label = f"card:{key[1]}"
        fitted = fit_card(im, pivot, report=pic_report, label=label)
        pal, lut = pic_palette([fitted], pic_report, label)
        card_data[key] = {"pal": pal, "data": tiles_4bpp(indexed(fitted, lut)), "w": CARD_W, "h": CARD_H}
    for ch in characters:
        fk = ("kitfront", ch["lib"]) if ch["front_portrait"] is None else ("front", ch["front_portrait"])
        ch["card"] = fk if fk in card_data else None

    emit(characters, pic_data, card_data, cmds, tables, report, pic_report, root, lib_head, args.check)
    IDS_FILE.write_text(json.dumps(ids, indent=1) + "\n")


# ---------------------------------------------------------------------------
# Emission

def anim_tables_for(group: Group, cmds, tables, pool):
    """Transformed copy of the group's vanilla anim table (structure kept)."""
    vt = tables[group.spec["table"]]
    entries = []
    for idx, anim in vt:
        out = []
        for c in cmds[anim]:
            if c[0] == "frame":
                img, hf = group.lookup(c[1], c[3])
                out.append(("frame", img, c[2], hf, c[4]))
            else:
                out.append(c)
        key = repr(out)
        if key not in pool["cmds"]:
            pool["cmds"][key] = (f"sCCAnim_{len(pool['cmds'])}", out)
        entries.append((idx, pool["cmds"][key][0]))
    tkey = repr(entries)
    if tkey not in pool["tables"]:
        pool["tables"][tkey] = (f"sCCAnimTable_{len(pool['tables'])}", entries)
    return pool["tables"][tkey][0]


def cmd_text(c):
    if c[0] == "frame":
        extra = (", .hFlip = TRUE" if c[3] else "") + (", .vFlip = TRUE" if c[4] else "")
        return f"ANIMCMD_FRAME({c[1]}, {c[2]}{extra})"
    if c[0] == "loop":
        return f"ANIMCMD_LOOP({c[1]})"
    if c[0] == "jump":
        return f"ANIMCMD_JUMP({c[1]})"
    return "ANIMCMD_END"


def emit(characters, pic_data, card_data, cmds, tables, report, pic_report, root, lib_head, check):
    hdr = ("// Generated by tools/character_customization/generate.py from the character-customization\n"
           "// library (do not edit). Native PC build only (PORTABLE).\n")
    pool = {"cmds": OrderedDict(), "tables": OrderedDict()}
    ow = [hdr]
    custom_pic_names = {pd["key"]: f"TRAINER_PIC_CC_{i}" for i, pd in enumerate(pic_data)}
    for ch in characters:
        for k in ("back_pic", "front_pic"):
            if not isinstance(ch[k], str):
                ch[k] = custom_pic_names[ch[k]]

    # characters' frames
    roster = []
    for ch in characters:
        n = ch["id"]
        if "groups" in ch:
            ow.append(c_pal(f"sCCPal_{n}", ch["pal"]))
            gfx = {}
            # every frame of the character once (groups share identical frames)
            blobs, offsets = [], {}
            per_group = {}
            for gname, g in ch["groups"].items():
                refs = []
                for im in g.images:
                    b = tiles_4bpp(indexed(im, ch["lut"]))
                    if b not in offsets:
                        offsets[b] = sum(len(x) for x in blobs)
                        blobs.append(b)
                    refs.append((offsets[b], len(b)))
                per_group[gname] = refs
            arr = f"sCCGfx_{n}"
            ow.append(c_words(arr, u32_words(b"".join(blobs))))
            for gname, g in ch["groups"].items():
                w, h = g.size
                frame_bytes = w * h // 2
                imgs = f"sCCImages_{n}_{gname}"
                ow.append(f"static const struct SpriteFrameImage {imgs}[] = {{")
                for off, size in per_group[gname]:
                    ow.append(f"    {{.data = (const u8 *){arr} + {off}, .size = {size}}},")
                ow.append("};")
                table = anim_tables_for(g, cmds, tables, pool)
                gfx[gname] = (imgs, table, frame_bytes, w, h, GROUPS[gname]["tracks"])
            ch["gfx"] = gfx
    # anim pools (after frames so names exist; tables reference command arrays)
    anim_lines = []
    for name, out in pool["cmds"].values():
        anim_lines.append(f"static const union AnimCmd {name}[] = {{ " + ", ".join(cmd_text(c) for c in out) + " };")
    for name, entries in pool["tables"].values():
        anim_lines.append(f"static const union AnimCmd *const {name}[] = {{")
        for idx, a in entries:
            anim_lines.append(f"    [{idx}] = {a},")
        anim_lines.append("};")
    anim_lines.append("// Every anim table above (GetStepAnimTable recognises them: steps alternate as in vanilla)")
    anim_lines.append("static const union AnimCmd *const *const sCCAnimTables[] = {")
    for name, _ in pool["tables"].values():
        anim_lines.append(f"    {name},")
    anim_lines.append("};")

    # Trainer Card pictures (fronts bigger than 64 x 64, shown whole in the card's window)
    card_names = {}
    for k, (key, cd) in enumerate(card_data.items()):
        card_names[key] = (f"sCCCard_{k}", f"sCCCardPal_{k}", cd["w"], cd["h"])
        ow.append(c_words(f"sCCCard_{k}", u32_words(cd["data"])))
        ow.append(c_pal(f"sCCCardPal_{k}", cd["pal"]))
    ow.append("// Generated battle backs drawn from 80 x 80 pictures (several OAM pieces)")
    ow.append("static const bool8 sCCLargeBackPic[CHARCUSTOM_TRAINER_PIC_COUNT] = {" +
              ", ".join("TRUE" if pd["kind"] == "back" and pd["size"] == (80, 80) else "FALSE" for pd in pic_data) + "};")

    # roster table
    rl = []
    rl.append("static const struct CharCustomCharacter sCharacters[CC_CHARACTER_COUNT] = {")
    for ch in characters:
        n = ch["id"]
        rl.append(f"    [{n}] = {{")
        rl.append(f"        .name = COMPOUND_STRING(\"{gba_text(ch['name'])}\"),")
        rl.append(f"        .variant = COMPOUND_STRING(\"{gba_text(ch['variant'])}\"),")
        rl.append(f"        .libraryId = \"{ch['lib']}\",")
        rl.append(f"        .registryNum = {ch['registry']},")
        rl.append(f"        .gender = {'MALE' if ch['gender'] == 'male' else 'FEMALE'},")
        rl.append(f"        .kind = CC_KIND_{ch['kind'].upper()},")
        rl.append(f"        .frontPic = {ch['front_pic']},")
        rl.append(f"        .backPic = {ch['back_pic']},")
        if ch.get("card"):
            cn, cp, cw, chh = card_names[ch["card"]]
            rl.append(f"        .cardPic = {cn},")
            rl.append(f"        .cardPal = {cp},")
            rl.append(f"        .cardWidth = {cw},")
            rl.append(f"        .cardHeight = {chh},")
        if "vanilla" in ch:
            rl.append(f"        .vanillaAvatar = TRUE,")
        else:
            rl.append(f"        .palette = sCCPal_{n},")
            rl.append("        .gfx = {")
            for gname, (imgs, table, fb, w, h, tracks) in ch["gfx"].items():
                rl.append(f"            [{GROUP_ENUM[gname]}] = {{{imgs}, {table}, {fb}, {w}, {h}, {tracks}}},")
            rl.append("        },")
        rl.append("    },")
    rl.append("};")

    consts = [hdr, "#ifndef GUARD_CONSTANTS_CHARACTER_CUSTOMIZATION_H", "#define GUARD_CONSTANTS_CHARACTER_CUSTOMIZATION_H", ""]
    consts.append(f"#define CC_CHARACTER_COUNT {max(c['id'] for c in characters) + 1} // engine ids 1..{max(c['id'] for c in characters)}; 0 = the default trainer")
    consts.append(f"#define CHARCUSTOM_TRAINER_PIC_COUNT {len(pic_data)}")
    consts.append("")
    consts.append("// Engine character ids (stable: tools/character_customization/ids.json, append-only)")
    for ch in characters:
        consts.append(f"#define CC_CHAR_{ch['lib'].upper().replace('-', '_')} {ch['id']}")
    consts.append("")
    consts.append("// Generated trainer pictures follow the engine's own (TRAINER_PIC_COUNT + i)")
    for i, pd in enumerate(pic_data):
        consts.append(f"#define TRAINER_PIC_CC_{i} (TRAINER_PIC_COUNT + {i}) // {pd['kind']}: {pd['ref']}")
    consts += ["", "#endif // GUARD_CONSTANTS_CHARACTER_CUSTOMIZATION_H", ""]

    pl = [hdr]
    for i, pd in enumerate(pic_data):
        pal = pd["pal"]
        if pd["kind"] == "front":
            pl.append(c_words(f"sCCTrainerFrontPic_{i}", u32_words(lz77_literal(pd["data"]))))
            pl.append(c_pal(f"sCCTrainerFrontPal_{i}", pal))
        else:
            pl.append(c_words(f"sCCTrainerBackPic_{i}", u32_words(pd["data"])))
            pl.append(c_pal(f"sCCTrainerBackPal_{i}", pal))
            seq_, ticks = pd["sequence"], pd["ticks"]
            rest = seq_[-1]
            pl.append(f"static const union AnimCmd sCCBackRest_{i}[] = {{ ANIMCMD_FRAME({rest}, 0), ANIMCMD_END }};")
            pl.append(f"static const union AnimCmd sCCBackThrow_{i}[] = {{ " +
                      ", ".join(f"ANIMCMD_FRAME({f}, {max(1, t)})" for f, t in zip(seq_, ticks)) + ", ANIMCMD_END };")
            pl.append(f"static const union AnimCmd *const sCCBackAnims_{i}[] = {{ sCCBackRest_{i}, sCCBackThrow_{i}, sCCBackRest_{i} }};")
    pl.append("")
    pl.append("// Appended to gTrainerPicInfo (src/data/graphics/trainers.h) in the native build.")
    pl.append("#define CHARCUSTOM_TRAINER_PIC_INFOS \\")
    for i, pd in enumerate(pic_data):
        if pd["kind"] == "front":
            pl.append(f"    [TRAINER_PIC_CC_{i}] = {{ .frontPic = TRAINER_FRONT_PIC(sCCTrainerFrontPic_{i}, sCCTrainerFrontPal_{i}),"
                      f" .backPic = TRAINER_BACK_PIC(1, gTrainerBackPic_None, gTrainerPalette_None, sBackAnims_None) }}, \\")
        elif pd["size"] == (64, 64):
            pl.append(f"    [TRAINER_PIC_CC_{i}] = {{ .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_None, gTrainerPalette_None),"
                      f" .backPic = TRAINER_BACK_PIC({pd['frames']}, (const u8 *)sCCTrainerBackPic_{i}, sCCTrainerBackPal_{i}, sCCBackAnims_{i}) }}, \\")
        else:  # 80 x 80: 100 tiles a frame, laid out piece by piece (see character_customization.c)
            pl.append(f"    [TRAINER_PIC_CC_{i}] = {{ .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_None, gTrainerPalette_None),"
                      f" .backPic = (const struct TrainerBackPicInfo[]) {{{{ .coordinates = {{.size = 8, .y_offset = {pd['frames']}}},"
                      f" .image = {{.data = (const u8 *)sCCTrainerBackPic_{i}, .size = 80 * 80 / 2, .relativeFrames = TRUE}},"
                      f" .paletteData = sCCTrainerBackPal_{i}, .animation = sCCBackAnims_{i} }}}} }}, \\")
    pl.append("")

    files = {
        OUT_CONSTANTS: "\n".join(consts),
        OUT_OVERWORLD: "\n".join(ow + anim_lines + rl) + "\n",
        OUT_PICS: "\n".join(pl) + "\n",
    }
    manifest = OrderedDict()
    manifest["about"] = ("Engine data for the approved character roster, generated from the character-customization "
                         "library by tools/character_customization/generate.py. Lists every decision per character.")
    manifest["library"] = {"path_name": root.name, "commit": lib_head,
                           "selection_catalogue_sha256": sha256_file(root / "integration/roster/selection_catalogue.json"),
                           "registry_sha256": sha256_file(root / "library/registry.json"),
                           "components_sha256": sha256_file(root / "library/components/components.json")}
    manifest["outputs"] = {str(p.relative_to(ENGINE)): hashlib.sha256(t.encode()).hexdigest() for p, t in files.items()}
    manifest["characters"] = [OrderedDict([("engine_id", c["id"]), ("library_id", c["lib"]), ("kind", c["kind"]),
                                           ("name", c["full"]), ("menu", [c["name"], c["variant"]]),
                                           ("gender", c["gender"]), ("code", c["code"]),
                                           ("front_pic", c["front_pic"]), ("back_pic", c["back_pic"]),
                                           ("details", report[c["lib"]])]) for c in characters]
    manifest["trainer_pics"] = pic_report
    text_manifest = json.dumps(manifest, indent=1, default=str) + "\n"
    if check:
        stale = [str(p) for p, t in files.items() if not p.exists() or p.read_text() != t]
        if stale:
            die("stale outputs: " + ", ".join(stale))
        print("outputs are current")
        return
    for p, t in files.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != t:   # unchanged files keep their time (no rebuild)
            p.write_text(t)
    if not OUT_MANIFEST.exists() or OUT_MANIFEST.read_text() != text_manifest:
        OUT_MANIFEST.write_text(text_manifest)
    print(f"{len(characters)} characters, {len(pic_data)} trainer pictures, "
          f"{len(pool['cmds'])} anim command lists, {len(pool['tables'])} anim tables")


if __name__ == "__main__":
    main()
