#!/usr/bin/env python3
"""Deterministic artwork masks for one-cell Emerald mountain terrain."""

from __future__ import annotations

from collections import Counter, defaultdict, deque


class MountainArtError(ValueError):
    pass


GENERAL_TERRACE_METATILES = frozenset((111, 113, 121, 133, 135, 136, 137, 141, 144))


def mountain_art_mask(summary: dict) -> tuple[tuple[int, ...], str]:
    layers = summary.get("layers")
    if not summary.get("resolved") or not layers:
        raise MountainArtError("mountain metatile artwork is unresolved")

    foreground = layers[1]
    visible = sum(pixel.visible for pixel in foreground)
    if 0 < visible < 256:
        pixels = foreground
        mode = "foreground"
        outside: set[int] = set()
    else:
        pixels = layers[2]
        corners = [pixels[index].rgba for index in (0, 15, 240, 255)]
        background, count = min(Counter(corners).items(),
                                key=lambda item: (-item[1], item[0]))
        outside = set()
        if count >= 2:
            pending = deque(index for index in range(256)
                            if (index % 16 in (0, 15) or index // 16 in (0, 15))
                            and pixels[index].rgba == background)
            outside.update(pending)
            while pending:
                index = pending.popleft()
                x, y = index % 16, index // 16
                for neighbor in ((index - 1 if x else -1),
                                 (index + 1 if x < 15 else -1),
                                 (index - 16 if y else -1),
                                 (index + 16 if y < 15 else -1)):
                    if neighbor >= 0 and neighbor not in outside \
                            and pixels[neighbor].rgba == background:
                        outside.add(neighbor)
                        pending.append(neighbor)
        mode = "silhouette"

    rows = tuple(sum(1 << x for x in range(16)
                     if pixels[y * 16 + x].visible
                     and y * 16 + x not in outside)
                 for y in range(16))
    occupied = sum(row.bit_count() for row in rows)
    if occupied == 0 or occupied == 256:
        raise MountainArtError(
            f"mountain {mode} mask must be non-empty and non-rectangular, got {occupied} pixels")
    return rows, mode


def mountain_heightmap(rows: tuple[int, ...]) -> tuple[int, ...]:
    remaining = list(rows)
    depths = [0] * 256
    depth = 0
    while any(remaining):
        depth += 1
        eroded = [0] * 16
        for y, bits in enumerate(remaining):
            for x in range(16):
                if bits & (1 << x):
                    depths[y * 16 + x] = depth
            if 0 < y < 15:
                eroded[y] = (bits & (bits << 1) & (bits >> 1)
                              & remaining[y - 1] & remaining[y + 1]) & 0xFFFF
        remaining = eroded
    if depth == 0:
        return tuple(depths)
    return tuple(0 if value == 0 else 16 if depth == 1
                 else 4 + (value - 1) * 12 // (depth - 1)
                 for value in depths)


def detect_layout(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                  width: int, height: int) -> frozenset[int]:
    # Only rock the player cannot walk on stands up: the game tags its mountain
    # paths MB_MOUNTAIN_TOP too (the battle background's mountain).
    seeds = {offset for offset, cell in enumerate(cells)
             if cell.get("behavior") == "MB_MOUNTAIN_TOP" and cell.get("collision")}
    palette_rows: dict[int, set[int]] = defaultdict(set)
    for offset in seeds:
        for pixel in summaries[offset].get("layers", ((), (), ()))[2]:
            source = pixel.source
            if pixel.visible and source is not None and source.palette >= 6:
                palette_rows[source.palette].add(source.tile // 16)

    eligible = set()
    for offset, (cell, summary) in enumerate(zip(cells, summaries)):
        if cell.get("behavior") != "MB_NORMAL" or not cell.get("collision"):
            continue
        mountain_pixels = 0
        outside_family = False
        for pixel in summary.get("layers", ((), (), ()))[2]:
            source = pixel.source
            if not pixel.visible or source is None or source.palette < 6:
                continue
            rows = palette_rows.get(source.palette, set())
            if source.tile // 16 not in rows:
                outside_family = True
                break
            mountain_pixels += 1
        if outside_family or mountain_pixels < 16:
            continue
        try:
            mountain_art_mask(summary)
        except MountainArtError:
            continue
        eligible.add(offset)

    pending = deque(sorted(seeds))
    reached = set(seeds)
    while pending:
        offset = pending.popleft()
        x, y = offset % width, offset // width
        for neighbor in ((offset - 1 if x else -1),
                         (offset + 1 if x + 1 < width else -1),
                         (offset - width if y else -1),
                         (offset + width if y + 1 < height else -1)):
            if neighbor in eligible and neighbor not in reached:
                reached.add(neighbor)
                pending.append(neighbor)
    return frozenset(reached)


# A seed palette is the mountain's own when it is the main colour of at least
# this share of its seeds (Route 112: the rock's, not its trees').
MASS_PALETTE_SHARE = 0.25


def _main_palette(summary: dict) -> tuple[int | None, float]:
    counts = Counter(pixel.source.palette
                     for pixel in summary.get("layers", ((), (), ()))[2]
                     if pixel.visible and pixel.source is not None)
    if not counts:
        return None, 0.0
    palette, count = min(counts.items(), key=lambda item: (-item[1], item[0]))
    return palette, count / sum(counts.values())


def _layer_palette(summary: dict, layer: int) -> tuple[int | None, int]:
    counts = Counter(pixel.source.palette
                     for pixel in summary.get("layers", ((), (), ()))[layer]
                     if pixel.visible and pixel.source is not None)
    if not counts:
        return None, 0
    palette, _count = min(counts.items(), key=lambda item: (-item[1], item[0]))
    return palette, sum(counts.values())


def outline_cells(summaries: tuple[dict, ...], offsets: frozenset[int]) -> frozenset[int]:
    """The mountain cells whose upper layer draws the rock's edge over ground
    in another palette (Route 106's rim, a reef's edge in the sea): the rock is
    the upper layer's outline, the rest ground (gl_terrain_renderer.c shapes
    the mass to it). Where both layers are the rock's (Route 112's crags drawn
    on rock) the upper layer is relief on the rock instead."""
    out = set()
    for offset in offsets:
        upper, drawn = _layer_palette(summaries[offset], 1)
        lower, _ = _layer_palette(summaries[offset], 0)
        if upper is not None and 16 <= drawn < 256 and lower != upper:
            out.add(offset)
    return frozenset(out)


def enclosed_tops(cells: tuple[dict, ...], width: int, height: int,
                  events: frozenset[int] = frozenset()) -> frozenset[int]:
    """Walkable MB_MOUNTAIN_TOP cells no walk reaches: a region of them every
    neighbour of which is blocked (the top of a reef in the sea, drawn with the
    mountain's rock and closed in by its blocked edge). They are the rock's
    own, not ground: flat inside the reef's rock they read as a pit. A region
    with an event on it (`events`: a trainer, an item a script puts there) is
    left as it was."""
    def top(offset: int) -> bool:
        cell = cells[offset]
        return cell.get("behavior") == "MB_MOUNTAIN_TOP" and not cell.get("collision")

    out: set[int] = set()
    seen: set[int] = set()
    for start in range(len(cells)):
        if start in seen or not top(start):
            continue
        region, pending, open_ = [], deque((start,)), False
        seen.add(start)
        while pending:
            offset = pending.popleft()
            region.append(offset)
            x, y = offset % width, offset // width
            for neighbor in ((offset - 1 if x else -1),
                             (offset + 1 if x + 1 < width else -1),
                             (offset - width if y else -1),
                             (offset + width if y + 1 < height else -1)):
                if neighbor < 0:
                    open_ = True
                elif top(neighbor):
                    if neighbor not in seen:
                        seen.add(neighbor)
                        pending.append(neighbor)
                elif not cells[neighbor].get("collision"):
                    open_ = True
        if not open_ and not events.intersection(region):
            out.update(region)
    return frozenset(out)


def mass_extension(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                   width: int, height: int, mountain: frozenset[int],
                   exclude: frozenset[int] = frozenset()) -> frozenset[int]:
    """The rest of a mountain's rock: the blocked cells (MB_NORMAL) joined to
    its cells that are drawn mostly in its own palette - the faces and edges
    the GBA draws round its MB_MOUNTAIN_TOP rock (Route 106's mountain: its
    face, its edges and the cave mouth's top), the rock of a reef round a
    MB_MOUNTAIN_TOP cell in the sea. Left as plain blocked ground they lay flat
    beside rock a cell high (the owner's report, 2026-10-24: a field of cubes at
    uneven heights). `exclude`: cells something else draws (the sea's rocks, a
    terrace's course). Returns the cells added."""
    seeds = [offset for offset, cell in enumerate(cells)
             if cell.get("behavior") == "MB_MOUNTAIN_TOP" and cell.get("collision")]
    if not seeds:
        return frozenset()
    counts = Counter(_main_palette(summaries[offset])[0] for offset in seeds)
    family = {palette for palette, count in counts.items()
              if palette is not None and count >= MASS_PALETTE_SHARE * len(seeds)}

    def joins(offset: int) -> bool:
        cell = cells[offset]
        # (a reef's edge in the sea is blocked water drawn with the rock)
        if (offset in exclude or not cell.get("collision")
                or (cell.get("behavior") != "MB_NORMAL"
                    and cell.get("behavior") not in ("MB_OCEAN_WATER", "MB_POND_WATER",
                                                     "MB_DEEP_WATER", "MB_SHALLOW_WATER"))
                or not summaries[offset].get("resolved")):
            return False
        palette, share = _main_palette(summaries[offset])
        if palette in family and share >= 0.5:
            return True
        # the rock drawn on the upper layer over the sea or the ground (a
        # reef's edge): the upper layer's palette is the rock's
        upper, drawn = _layer_palette(summaries[offset], 1)
        return upper in family and drawn >= 64

    added: set[int] = set()
    pending = deque(sorted(mountain))
    while pending:
        offset = pending.popleft()
        x, y = offset % width, offset // width
        for neighbor in ((offset - 1 if x else -1),
                         (offset + 1 if x + 1 < width else -1),
                         (offset - width if y else -1),
                         (offset + width if y + 1 < height else -1)):
            if (neighbor >= 0 and neighbor not in mountain and neighbor not in added
                    and joins(neighbor)):
                added.add(neighbor)
                pending.append(neighbor)
    return frozenset(added)


# The water a face may stand in (Sootopolis's crater): its level is the sea's,
# no ground's (faces_between_levels).
WATER = frozenset({
    "MB_POND_WATER", "MB_INTERIOR_DEEP_WATER", "MB_DEEP_WATER", "MB_SOOTOPOLIS_DEEP_WATER",
    "MB_OCEAN_WATER", "MB_SHALLOW_WATER", "MB_NO_SURFACING", "MB_SEAWEED",
    "MB_SEAWEED_NO_SURFACING", "MB_WATERFALL", "MB_EASTWARD_CURRENT", "MB_WESTWARD_CURRENT",
    "MB_NORTHWARD_CURRENT", "MB_SOUTHWARD_CURRENT", "MB_PUDDLE", "MB_HOT_SPRINGS"})


def faces_between_levels(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                         width: int, height: int, mountain: frozenset[int],
                         levels: dict[int, int],
                         exclude: frozenset[int] = frozenset()) -> frozenset[int]:
    """The rock a mountain's walkable top stands on, drawn beside it: a blocked
    cell (MB_NORMAL) drawn in the mountain's palette, beside the mountain's
    walkable top (its MB_MOUNTAIN_TOP cells joined to one beside its rock),
    whose walkable neighbours lie on two levels or more (`levels`: a walkable cell's level, absent 0). Route
    116's ledge path (a level over the grass) has such a face on its west side
    (General 112): no rock beside it joined it to the mountain (mass_extension),
    so it lay flat as ground on the path's level and its side, a level high
    over the grass, was drawn with the grass under the rock's edge (the owner's
    report, 2026-10-24: "the light blue emptiness"). They are the mountain's
    rock (compile_rules._mountain_masses then stands them from the lower level
    to the upper). Returns those cells."""
    seeds = [offset for offset, cell in enumerate(cells)
             if cell.get("behavior") == "MB_MOUNTAIN_TOP" and cell.get("collision")]
    if not seeds:
        return frozenset()
    counts = Counter(_main_palette(summaries[offset])[0] for offset in seeds)
    family = {palette for palette, count in counts.items()
              if palette is not None and count >= MASS_PALETTE_SHARE * len(seeds)}

    def neighbours(offset: int):
        x, y = offset % width, offset // width
        for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
            if 0 <= nx < width and 0 <= ny < height:
                yield ny * width + nx

    def walkable(offset: int) -> bool:
        return not cells[offset].get("collision")

    def walkable_top(offset: int) -> bool:
        return walkable(offset) and cells[offset].get("behavior") == "MB_MOUNTAIN_TOP"

    # the mountain's walkable top: its MB_MOUNTAIN_TOP paths side by side with one
    # beside its rock
    tops = set()
    pending = deque(offset for offset in range(len(cells))
                    if walkable_top(offset)
                    and any(neighbor in mountain for neighbor in neighbours(offset)))
    tops.update(pending)
    while pending:
        offset = pending.popleft()
        for neighbor in neighbours(offset):
            if neighbor not in tops and walkable_top(neighbor):
                tops.add(neighbor)
                pending.append(neighbor)

    out = set()
    for offset, cell in enumerate(cells):
        if (offset in mountain or offset in exclude or not cell.get("collision")
                or cell.get("behavior") != "MB_NORMAL" or not summaries[offset].get("resolved")):
            continue
        palette, share = _main_palette(summaries[offset])
        upper, drawn = _layer_palette(summaries[offset], 1)
        if not ((palette in family and share >= 0.5) or (upper in family and drawn >= 64)):
            continue
        around = [neighbor for neighbor in neighbours(offset) if walkable(neighbor)
                  and cells[neighbor].get("behavior") not in WATER]
        if (len({levels.get(neighbor, 0) for neighbor in around}) >= 2
                and any(neighbor in tops for neighbor in around)):
            out.add(offset)
    return frozenset(out)


def validate_layout(cells: tuple[dict, ...], summaries: tuple[dict, ...],
                    width: int, height: int,
                    exclude: frozenset[int] = frozenset(),
                    events: frozenset[int] = frozenset()) -> frozenset[int]:
    offsets = detect_layout(cells, summaries, width, height)
    dense_forest = {offset for offset, cell in enumerate(cells)
                    if cell.get("tileset") == "gTileset_General"
                    and cell.get("metatile") in (198, 199)}
    terraces = {offset for offset, cell in enumerate(cells)
                if cell.get("tileset") == "gTileset_General"
                and cell.get("metatile") in GENERAL_TERRACE_METATILES}
    for offset in sorted(offsets | dense_forest | terraces):
        summary = summaries[offset]
        try:
            rows, _ = mountain_art_mask(summary)
            heights = mountain_heightmap(rows)
            if max(heights) != 16:
                raise MountainArtError("mountain heightmap does not reach one cell")
        except MountainArtError as error:
            raise MountainArtError(f"mountain cell {offset}: {error}") from error
    # the extension's cells are plain rock where the drawing is whole: their
    # relief, if any, is read at run time (gl_terrain_renderer.c)
    seeds = frozenset(offset for offset, cell in enumerate(cells)
                      if cell.get("behavior") == "MB_MOUNTAIN_TOP" and cell.get("collision"))
    enclosed = enclosed_tops(cells, width, height, events) - exclude
    return offsets | enclosed | mass_extension(cells, summaries, width, height,
                                               offsets | seeds | enclosed, exclude)
