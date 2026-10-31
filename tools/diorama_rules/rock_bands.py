#!/usr/bin/env python3
"""Every band of rock one surface over the ground round it (step 16, the world geometry overhaul: the app's
docs/3d-world-geometry-20261029.md).

The GBA draws rock - a mountain, a ridge, the face between two levels of ground - with pieces of its tilesets the
player cannot walk on. The rules stood some of them up cell by cell, each a tile over its own ground (Mossdeep's
cliff: its corner pieces a tile over the plateau whose face they are, pillars two tiles over the street), laid others
flat on one level or the other (the Safari Zone's mountain, its fill and faces painted on the ground), and read the
rest as plain ground (Route 120's cliff). Here a layout's rock - the blocked cells of the standing classes (cliff,
mound, wall) and of its mountains' art, and the blocked plain cells beside them drawn mostly in a palette their rock
is drawn in - is cut into bands (4-connected), and each band becomes one surface over the walkable ground and the
water round it: on that ground's levels where it meets it; where the band parts two levels, a face whose top meets
the higher of the levels within NEAR_CELLS of the cell (the GBA's cliff, its face down to the lower: laid smoothly
between them, a face more than a cell thick was a ramp, and Ever Grande's mountain round Victory Road's mouth a slope
down to it; risen over the higher level inward, Route 114's mountains stood taller than the game draws them), and
RIDGE_Q16 over the one level near a cell where only one is but no higher than the higher level the band parts (the
band's rock far from its face: Route 112's north, a tile over the grass, not as high as the mountain's top it meets
far off; risen over the higher level, the rock round a face stood in steps); where the band parts none, a ridge or a
mountain, RIDGE_Q16 a cell into the band and RIDGE_STEP_Q16 more each cell farther in; never higher than CAP_Q16 over
the highest level it meets. Each band's cells share one ground, the lowest of the levels it meets along a real share
of its edge (LEVEL_SHARE: a face's two levels), as the renderer shapes their pixels as one mass from their neighbours
(gl_terrain_renderer.c ShapeRockMasses); a level it meets only here and there - a pit two levels down beside one wall
of the maze of rock on Mt Pyre's summit - neither takes the band down to it nor makes it a face: the whole maze stood
on the pits' level, flat (and the water round the summit with it, whose banks its walkable rock was), and the
renderer hangs the side down to such a pit as to any lower ground. (Each cell on the levels near it alone was tried:
rock on two grounds side by side left the sky between them, and the ridge's rising inside a wide mass between levels
stood it as columns.) A terrace's solved course (the terrace solver's General pieces) is ground of its top's level
here, not rock: it keeps its own profile; so is a mountain's walkable art (MB_MOUNTAIN_TOP): what the player walks on
keeps its height; and the mountains _mountain_masses stands (`tops`) keep theirs.

Heights are in sixteenths of a tile (the rules' Q16).
"""

from __future__ import annotations

from collections import Counter, deque

from mountain_art import WATER, _main_palette

ROCK_CLASSES = frozenset({"cliff", "mound", "wall"})
# a band that parts no levels rises this much a cell into it: a ridge a cell or two thick a tile, the maze of walls
# on Mt Pyre's summit as the GBA draws it, its junctions too (risen as a sum over the band's cells - each a share of
# its neighbours' heights - they and its thick parts stood up to twice as high as its walls, in blocks)
RIDGE_Q16 = 16
# ... and this much more each cell farther in (a whole tile a cell stood a three-cell ridge's middle row as a spine)
RIDGE_STEP_Q16 = 8
# ... and no higher than this over the highest ground it meets
CAP_Q16 = 32
# a band no cell of which is farther in from its edge than this, parting no levels, is a cliff painted on level ground
# (band_surfaces' `painted`): Lilycove's, three cells thick, two in
PAINTED_DEPTH = 2
# a plain blocked cell joins the rock when at least this share of its pixels are in one of the rock's palettes
PALETTE_SHARE = 0.5
# a palette is the rock's when it is the main palette of at least this share of the seeds (and of two of them)
FAMILY_SHARE = 0.1
# a face's cell: the levels within this many cells of it, through the band, part or not (a face three cells thick
# parts its levels all through)
NEAR_CELLS = 3
# a level counts for a band's ground and shape when it meets at least this share of the cells round the band (Mt
# Pyre's summit: its pits under 5%; a face's two levels 15% and more)
LEVEL_SHARE = 0.1
RELAXATION = 1.7        # successive over-relaxation
MAX_SWEEPS = 600
TOLERANCE_Q16 = 0.05


def _neighbours(offset: int, width: int, height: int):
    x, y = offset % width, offset // width
    if y > 0:
        yield offset - width
    if x + 1 < width:
        yield offset + 1
    if y + 1 < height:
        yield offset + width
    if x > 0:
        yield offset - 1


def rock_cells(cells: tuple[dict, ...], class_names: tuple[str, ...], sources: tuple[str, ...],
               summaries: tuple[dict, ...], width: int, height: int,
               mountain: frozenset[int] = frozenset(), exclude: frozenset[int] = frozenset(),
               art: frozenset[int] = frozenset()) -> frozenset[int]:
    """A layout's rock: the blocked cells of the standing classes, of the mountains' art (`mountain`) and drawn with
    a tileset's rock art (`art`: the tileset files' rockArt - Sootopolis's crater, its white rock no class of its own
    stands, the same art the city's streets are paved with where they are walked), and the blocked plain cells (the
    classifier's flat fallback, MB_NORMAL or MB_MOUNTAIN_TOP) joined to them and drawn mostly in a palette the rock is
    drawn in - not the main palette of most of the walkable ground (grass, sand, a town's paving: a blocked cell drawn
    with it is an invisible wall, not rock). `exclude`: cells something else stands (props, the sea's rocks, a
    terrace's courses)."""
    seeds = {offset for offset, cell in enumerate(cells)
             if offset not in exclude and cell.get("collision")
             and (class_names[offset] in ROCK_CLASSES or offset in mountain or offset in art)}
    if not seeds:
        return frozenset()
    counts = Counter(_main_palette(summaries[offset])[0] for offset in seeds)
    ground = Counter(_main_palette(summaries[offset])[0] for offset, cell in enumerate(cells)
                     if not cell.get("collision") and cell.get("behavior") not in WATER)
    walkable = sum(ground.values())
    family = {palette for palette, count in counts.items()
              if palette is not None and count >= max(2, FAMILY_SHARE * len(seeds))
              and not (walkable and ground[palette] >= 0.5 * walkable)}
    rock = set(seeds)
    pending = deque(sorted(seeds))
    while pending:
        offset = pending.popleft()
        for neighbor in _neighbours(offset, width, height):
            if neighbor in rock or neighbor in exclude:
                continue
            cell = cells[neighbor]
            if (not cell.get("collision") or not sources[neighbor].startswith("fallback")
                    or cell.get("behavior") not in ("MB_NORMAL", "MB_MOUNTAIN_TOP")):
                continue
            palette, share = _main_palette(summaries[neighbor])
            if palette in family and share >= PALETTE_SHARE:
                rock.add(neighbor)
                pending.append(neighbor)
    return frozenset(rock)


def band_surfaces(cells: tuple[dict, ...], width: int, height: int, rock: frozenset[int],
                  levels: dict[int, int], tops: dict[int, int] | None = None,
                  ridge: int = RIDGE_Q16, cap: int = CAP_Q16, caps: dict[int, int] | None = None,
                  pits: frozenset[int] = frozenset(),
                  painted: bool = False) -> dict[int, tuple[int, int]]:
    """{offset: (groundQ16, topQ16)} for the rock's cells. `levels`: the walkable ground's and the water's levels
    (absent: 0, the level the terrace solver gives ground of no level of its own) - every cell beside the rock that
    is not rock: walkable ground, water, and what stands on the ground (a tree, a house, a fence, at its ground's
    level); `tops`: such cells that meet it at a height of their own (a terrace's course: its top). Only the map's
    edge says nothing. A band that meets nothing keeps no entry. `caps`: cells whose rock may stand higher than
    `cap` over its ground (a band takes the largest of its cells'); `pits`: rock cells that shape their band (its
    cells are as far from the ground as they are from the band's edge, not from them) but lie on its ground,
    (ground, ground) - a crater's mouth, its rim rising to it. `painted`: a thin band (no cell more than
    PAINTED_DEPTH cells in from its edge) that parts no levels, on land more than in water - a cliff the GBA paints on
    level ground - keeps no entry, its pieces drawn as their own shapes as before step 16: risen a tile as a ridge,
    Lilycove's, Fallarbor's, Route 120's and the Safari Zone's cliffs stood as walls on the ground both sides of them
    (the owner's report of 2026-10-31). A wide mass (a mountain) still rises."""
    tops = tops or {}
    caps = caps or {}
    out: dict[int, tuple[int, int]] = {}
    seen: set[int] = set()
    for start in sorted(rock):
        if start in seen:
            continue
        band, pending = [], [start]
        seen.add(start)
        while pending:
            offset = pending.pop()
            band.append(offset)
            for neighbor in _neighbours(offset, width, height):
                if neighbor in rock and neighbor not in seen:
                    seen.add(neighbor)
                    pending.append(neighbor)
        boundary: dict[int, int] = {}
        for offset in band:
            for neighbor in _neighbours(offset, width, height):
                if neighbor in rock or neighbor in boundary:
                    continue
                if neighbor in tops:
                    boundary[neighbor] = tops[neighbor]
                else:
                    # walkable ground, water, and what stands on the ground beside it (a forest, a house, a
                    # fence: its ground's level) - a strip of rock between the sea and Route 115's forest, held by
                    # the sea alone, stood as a fin
                    boundary[neighbor] = levels.get(neighbor, 0)
        if not boundary:
            continue
        shares = Counter(boundary.values())
        common = shares.most_common(1)[0][0]
        counted = [level for level, count in shares.items()
                   if count >= LEVEL_SHARE * len(boundary) or level == common]
        low, high = min(counted), max(boundary.values())
        members = set(band)
        band_cap = max([cap] + [caps[offset] for offset in band if offset in caps])
        for offset in band:
            if offset in pits:
                out[offset] = (low, low)
        if max(counted) - low < 16:
            # a ridge on one level: as high as the cell is far from the ground round it
            depth = _depths(band, members, width, height)
            if (painted and max(depth.values()) <= PAINTED_DEPTH
                    and 2 * sum(cells[neighbor].get("behavior") in WATER for neighbor in boundary) < len(boundary)):
                continue    # a cliff painted on level ground: its pieces as they are drawn, not a wall (a rock in
                            # the sea, water most of the way round it, still rises as a mound)
            for offset in band:
                if offset in pits:
                    continue
                beside = [boundary[neighbor] for neighbor in _neighbours(offset, width, height)
                          if neighbor in boundary]
                top = max([low + min(band_cap, ridge + RIDGE_STEP_Q16 * (depth[offset] - 1))] + beside)
                out[offset] = (low, min(top, high + band_cap))
            continue
        height_of = {offset: (low if offset in pits else (low + high) / 2.0) for offset in band}
        order = [offset for offset in sorted(band) if offset not in pits]     # a pit stays on the ground
        for _sweep in range(MAX_SWEEPS):
            largest = 0.0
            for offset in order:
                total, count = 0.0, 0
                for neighbor in _neighbours(offset, width, height):
                    if neighbor in members:
                        total += height_of[neighbor]
                        count += 1
                    elif neighbor in boundary:
                        total += boundary[neighbor]
                        count += 1
                if not count:
                    continue
                target = total / count
                value = height_of[offset] + RELAXATION * (target - height_of[offset])
                value = min(max(value, low), high + band_cap)   # within its bounds as it settles (a mesa's top)
                largest = max(largest, abs(value - height_of[offset]))
                height_of[offset] = value
            if largest < TOLERANCE_Q16:
                break
        near = _levels_near(band, members, boundary, width, height)
        upper = max(counted)
        for offset in band:
            if offset in pits:
                continue
            # at least as high as the highest ground beside it (a top a little under it left a sliver of sky
            # between them) and as the higher of the levels near it, or a tile over the one level near it; and at
            # least a sixteenth over its ground (rock of no height the renderer stands a whole tile up:
            # gl_terrain_renderer.c ShapeRockMasses)
            beside = [boundary[neighbor] for neighbor in _neighbours(offset, width, height) if neighbor in boundary]
            levels_near = near[offset]
            if max(levels_near) - min(levels_near) >= 16:
                top = max([int(round(height_of[offset])), low + 1, max(levels_near)] + beside)
            else:
                top = max([min(upper, min(levels_near) + ridge), low + 1] + beside)
            out[offset] = (low, min(top, high + band_cap))
    return out


def _depths(band: list[int], members: set[int], width: int, height: int) -> dict[int, int]:
    """Each of a band's cells: how many cells it is from the ground round the band, diagonals counted (1 beside it:
    a maze's junction, its corners on the ground, as its walls)."""
    def around(offset: int):
        x, y = offset % width, offset // width
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if (dx or dy) and 0 <= x + dx < width and 0 <= y + dy < height:
                    yield (y + dy) * width + x + dx

    depth = {offset: 1 for offset in band if any(neighbor not in members for neighbor in around(offset))}
    pending = deque(sorted(depth))
    while pending:
        offset = pending.popleft()
        for neighbor in around(offset):
            if neighbor in members and neighbor not in depth:
                depth[neighbor] = depth[offset] + 1
                pending.append(neighbor)
    for offset in band:
        depth.setdefault(offset, 1)     # (a band the map's edge alone bounds: not reached)
    return depth


def _levels_near(band: list[int], members: set[int], boundary: dict[int, int], width: int,
                 height: int) -> dict[int, list[int]]:
    """Each of a band's cells: the levels of the ground it meets within NEAR_CELLS cells through the band, or, a cell
    farther in, those its nearest such cells have."""
    near: dict[int, list[int]] = {offset: [] for offset in band}
    for where, level in boundary.items():
        frontier = [neighbor for neighbor in _neighbours(where, width, height) if neighbor in members]
        reached = set(frontier)
        for _depth in range(NEAR_CELLS):
            for offset in frontier:
                near[offset].append(level)
            following = []
            for offset in frontier:
                for neighbor in _neighbours(offset, width, height):
                    if neighbor in members and neighbor not in reached:
                        reached.add(neighbor)
                        following.append(neighbor)
            frontier = following
    pending = deque(offset for offset in sorted(band) if near[offset])
    while pending:
        offset = pending.popleft()
        for neighbor in _neighbours(offset, width, height):
            if neighbor in members and not near[neighbor]:
                near[neighbor] = near[offset]
                pending.append(neighbor)
    return near


# a tree the runtime stands as a solid stands on the walkable ground within this many cells of it (tree_grounds)
TREE_REACH = 2


def tree_grounds(cells: tuple[dict, ...], width: int, height: int, trees: dict[int, int | None],
                 solved: dict[int, dict]) -> dict[int, dict]:
    """Where each tree the runtime stands as a solid (`trees`: {offset: a large tree's quarter 0-3, row * 2 +
    column, or None for a small tree}: the tileset files' solidTrees, tree_sprites.c's DioramaTreeSprites_
    LavaridgePart) whose cells joined the rock round them (`solved`'s "mass" entries: a band's, a mountain's) stands:
    on the walkable ground it grows from - the level most of the walkable cells within TREE_REACH of it meet, the
    lower of two met as often (a tree at a cliff's foot) - else, a tree deep in the rock, on the rock's top. Drawn as
    rock (Route 112's terraces, Jagged Pass's ledges), each was a column painted with the tree from the rock's foot to
    its top, and a solid on the foot a pit a level or three under the terrace beside it (step 16). A tree on its
    ground already (the solver's) keeps it; a large tree's quarters stand on one ground. Returns {offset: entry},
    "lift": the voxel trees' style keeps the cell's own height on that ground."""
    groups: dict[int, list[int]] = {}
    for offset, quarter in trees.items():
        anchor = offset if quarter is None else offset - (quarter >> 1) * width - (quarter & 1)
        groups.setdefault(anchor, []).append(offset)
    out: dict[int, dict] = {}
    for _anchor, members in sorted(groups.items()):
        rock = [solved[offset]["topQ16"] for offset in members if solved.get(offset, {}).get("mass")]
        if not rock:
            continue
        seen = set(members)
        frontier = list(members)
        met: Counter = Counter()
        for _step in range(TREE_REACH):
            following = []
            for offset in frontier:
                for neighbor in _neighbours(offset, width, height):
                    if neighbor in seen:
                        continue
                    seen.add(neighbor)
                    if cells[neighbor].get("collision"):
                        following.append(neighbor)
                    elif neighbor in solved:
                        entry = solved[neighbor]
                        met[entry.get("topQ16", entry["groundQ16"])] += 1
            if met:
                break
            frontier = following
        if met:
            most = max(met.values())
            level = min(level for level, count in met.items() if count == most)
        else:
            level = min(rock)
        for offset in members:
            out[offset] = {"groundQ16": level, "topQ16": level, "profile": "none", "solid": False, "lift": True}
    return out
