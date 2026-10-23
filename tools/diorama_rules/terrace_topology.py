#!/usr/bin/env python3
"""Compile General terrace artwork into deterministic relative terrain levels."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass


@dataclass(frozen=True)
class TerraceRole:
    profile: str
    quadrants: tuple[int, int, int, int]  # north-west, north-east, south-east, south-west
    solid: bool
    ports: tuple[int, int]


TERRACE_ROLES = {
    121: TerraceRole("horizontal", (1, 1, 0, 0), True, (1, 3)),
    136: TerraceRole("vertical", (0, 1, 1, 0), True, (0, 2)),
    137: TerraceRole("outer", (1, 1, 1, 0), True, (2, 3)),
    144: TerraceRole("inner", (0, 1, 0, 0), True, (0, 1)),
    135: TerraceRole("horizontal", (1, 1, 0, 0), False, (1, 3)),
    133: TerraceRole("vertical", (0, 1, 1, 0), False, (0, 2)),
    111: TerraceRole("outer", (1, 1, 1, 0), False, (2, 3)),
    141: TerraceRole("inner", (0, 1, 0, 0), False, (0, 1)),
    175: TerraceRole("transition-horizontal", (1, 1, 0, 0), False, (1, 3)),
    207: TerraceRole("transition-horizontal", (1, 1, 0, 0), False, (1, 3)),
}


# Water a blocked cell stands in or beside (a rock in the sea) keeps its own
# height: only blocked cells on dry land take the land's level.
WATER_BEHAVIORS = frozenset({
    "MB_POND_WATER", "MB_INTERIOR_DEEP_WATER", "MB_DEEP_WATER", "MB_SOOTOPOLIS_DEEP_WATER",
    "MB_OCEAN_WATER", "MB_SHALLOW_WATER", "MB_NO_SURFACING", "MB_SEAWEED", "MB_SEAWEED_NO_SURFACING",
    "MB_WATERFALL", "MB_EASTWARD_CURRENT", "MB_WESTWARD_CURRENT", "MB_NORTHWARD_CURRENT",
    "MB_SOUTHWARD_CURRENT", "MB_PUDDLE", "MB_HOT_SPRINGS",
})


def role_for_cell(cell: dict) -> TerraceRole | None:
    if cell.get("tileset") != "gTileset_General":
        return None
    return TERRACE_ROLES.get(cell.get("localMetatile", cell.get("metatile")))


def solve_layout(cells: tuple[dict, ...], width: int, height: int,
                 contradictions: list[tuple[int, ...]] | None = None,
                 conflict_edges: list[tuple[object, object, int, int]] | None = None
                 ) -> dict[int, dict]:
    """Return solved cells; contradictory connected topology components are omitted."""
    if len(cells) != width * height:
        raise ValueError("terrace topology layout dimensions do not match its cells")
    cardinal = ((0, -1), (1, 0), (0, 1), (-1, 0))
    all_candidates = {offset: role for offset, cell in enumerate(cells)
                      if (role := role_for_cell(cell)) is not None}
    candidates = {offset: role for offset, role in all_candidates.items() if role.solid}
    for offset, role in all_candidates.items():
        if not role.profile.startswith("transition-"):
            continue
        x, y = offset % width, offset // width
        if any((neighbor := (y + cardinal[d][1]) * width + x + cardinal[d][0])
               in candidates and (d + 2) % 4 in candidates[neighbor].ports
               for d in role.ports
               if 0 <= x + cardinal[d][0] < width
               and 0 <= y + cardinal[d][1] < height):
            candidates[offset] = role
    roles = {}
    for offset, role in candidates.items():
        x, y = offset % width, offset // width
        for direction in role.ports:
            dx, dy = cardinal[direction]
            nx, ny = x + dx, y + dy
            if not (0 <= nx < width and 0 <= ny < height):
                continue
            neighbor = ny * width + nx
            neighbor_role = candidates.get(neighbor)
            if neighbor_role is not None and (direction + 2) % 4 in neighbor_role.ports:
                roles[offset] = role
                break
    if not roles:
        return {}

    # Collapse ordinary terrain into regions. Terrace course cells remain explicit nodes.
    region_by_cell: dict[int, int] = {}
    region_cells: dict[int, list[int]] = {}
    next_node = len(cells)
    for start in range(len(cells)):
        if start in roles or start in region_by_cell:
            continue
        if cells[start].get("collision", 0) != 0:
            continue
        node = next_node
        next_node += 1
        pending = deque((start,))
        region_by_cell[start] = node
        region_cells[node] = []
        while pending:
            offset = pending.popleft()
            region_cells[node].append(offset)
            x, y = offset % width, offset // width
            for neighbor in (offset - width if y else -1,
                             offset + 1 if x + 1 < width else -1,
                             offset + width if y + 1 < height else -1,
                             offset - 1 if x else -1):
                if (neighbor >= 0 and neighbor not in roles and neighbor not in region_by_cell
                        and cells[neighbor].get("collision", 0) == 0
                        and cells[neighbor].get("elevation") == cells[offset].get("elevation")):
                    region_by_cell[neighbor] = node
                    pending.append(neighbor)

    def node_for(offset: int, quadrant: int):
        role = roles.get(offset)
        return ((offset, role.quadrants[quadrant]) if role is not None
                else region_by_cell.get(offset))

    # Each role has one low and one high surface node. Cell boundaries connect in
    # two halves so diagonal corners join straight courses without scalar side guesses.
    graph: dict[object, list[tuple[object, int]]] = defaultdict(list)
    for offset, role in sorted(roles.items()):
        low = (offset, 0)
        high = (offset, 1)
        graph[low].append((high, 1))
        graph[high].append((low, -1))
        x, y = offset % width, offset // width
        for direction, neighbor in enumerate((
                offset - width if y else -1,
                offset + 1 if x + 1 < width else -1,
                offset + width if y + 1 < height else -1,
                offset - 1 if x else -1)):
            if neighbor < 0:
                continue
            source_quadrants = ((0, 1), (1, 2), (3, 2), (0, 3))[direction]
            target_quadrants = ((3, 2), (0, 3), (0, 1), (1, 2))[direction]
            for source_quadrant, target_quadrant in zip(source_quadrants, target_quadrants):
                source_node = node_for(offset, source_quadrant)
                target_node = node_for(neighbor, target_quadrant)
                if source_node is None or target_node is None:
                    continue
                graph[source_node].append((target_node, 0))
                graph[target_node].append((source_node, 0))

    solved: dict[int, dict] = {}
    visited: set[object] = set()
    for seed in sorted(graph, key=str):
        if seed in visited:
            continue
        values = {seed: 0}
        component = []
        pending = deque((seed,))
        contradictory = False
        while pending:
            node = pending.popleft()
            if node in visited:
                continue
            visited.add(node)
            component.append(node)
            for neighbor, delta in graph[node]:
                expected = values[node] + delta
                if neighbor in values:
                    if values[neighbor] != expected:
                        contradictory = True
                        if conflict_edges is not None:
                            conflict_edges.append((node, neighbor, values[neighbor], expected))
                else:
                    values[neighbor] = expected
                    pending.append(neighbor)
        if contradictory:
            if contradictions is not None:
                contradictions.append(tuple(sorted(node[0] for node in component
                                                   if isinstance(node, tuple))))
            continue
        anchor = max(values[node] for node in component)
        for node in component:
            ground = values[node] - anchor
            if isinstance(node, tuple):
                offset, level = node
                if level != 0:
                    continue
                role = roles[offset]
                solved[offset] = {"groundQ16": ground * 16,
                                  "topQ16": (ground + (1 if role.solid else 0)) * 16,
                                  "profile": role.profile, "solid": role.solid}
            else:
                for offset in region_cells[node]:
                    solved[offset] = {"groundQ16": ground * 16,
                                      "topQ16": ground * 16,
                                      "profile": "none", "solid": False}
    return solved


def join_water(cells: tuple[dict, ...], width: int, height: int,
               solved: dict[int, dict]) -> dict[int, dict]:
    """Water the solver left unsolved (shallows at another elevation, water no
    terrace reaches) that joins solved water takes its level: a sea and its
    shallows lie level (graphics overhaul, 2026-10-12: where the sea was solved
    a terrace below its rocks, its shallows and sandbars stood a level above
    it). Each body of unsolved walkable water, joined cell to cell (not through
    a waterfall), that touches solved water at one level only takes that
    level. Returns the joined cells' entries."""
    def water(offset: int) -> bool:
        cell = cells[offset]
        return (cell.get("behavior") in WATER_BEHAVIORS and cell.get("behavior") != "MB_WATERFALL"
                and cell.get("collision", 0) == 0 and role_for_cell(cell) is None)

    def neighbours(offset: int):
        x, y = offset % width, offset // width
        for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
            if 0 <= nx < width and 0 <= ny < height:
                yield ny * width + nx

    seen: set[int] = set()
    joined: dict[int, dict] = {}
    for start in range(len(cells)):
        if start in seen or start in solved or not water(start):
            continue
        body = []
        levels = set()
        pending = deque((start,))
        seen.add(start)
        while pending:
            offset = pending.popleft()
            body.append(offset)
            for n in neighbours(offset):
                if n in solved:
                    if water(n):
                        levels.add(solved[n]["groundQ16"])
                elif n not in seen and water(n):
                    seen.add(n)
                    pending.append(n)
        if len(levels) == 1:
            level = next(iter(levels))
            for offset in body:
                joined[offset] = {"groundQ16": level, "topQ16": level,
                                  "profile": "none", "solid": False}
    return joined


# A cave's highest floor stands this much over its lowest (Q16: a tile, the
# height the cave's walls are pinned to and its ledges drawn).
CAVE_RISE_Q16 = 16


def cave_levels(cells: tuple[dict, ...], width: int, height: int,
                solved: dict[int, dict] | None = None) -> dict[int, dict]:
    """A cave's floors at their own elevation (graphics overhaul, the owner's
    option B; V38): the walkable cells of each GBA elevation a level of their
    own, the lowest floor where it was and the highest a tile over it (a
    floor between them half way: Shoal Cave's and Meteor Falls's three). Its
    bridges (15) are decks at the highest floor they meet; its walls - cliffs,
    pinned a tile high - stand as they are, and so do its water (1), the
    cells of any elevation (0: stairs, slopes between floors) and the cells
    another solver already placed. A floor sunk deeper than the cave's walls
    would hide whoever walks in it from the camera. Returns nothing for a
    cave of one floor."""
    solved = solved or {}

    def walkable(offset):
        cell = cells[offset]
        return (not cell.get("collision") and cell.get("behavior") not in WATER_BEHAVIORS
                and offset not in solved)

    floors = [offset for offset in range(len(cells))
              if walkable(offset) and cells[offset].get("elevation") not in (0, 1, 15)]
    elevations = sorted({cells[offset]["elevation"] for offset in floors})
    if len(elevations) < 2:
        return {}
    step = {elevation: rank * CAVE_RISE_Q16 // (len(elevations) - 1)
            for rank, elevation in enumerate(elevations)}
    out = {}
    for offset in floors:
        level = step[cells[offset]["elevation"]]
        if level:
            out[offset] = {"groundQ16": level, "topQ16": level, "profile": "none",
                           "solid": False}
    # a bridge at the highest floor at either end of it
    seen = set()
    for start in range(len(cells)):
        if start in seen or not walkable(start) or cells[start].get("elevation") != 15:
            continue
        span, touching = [], []
        queue = deque([start])
        seen.add(start)
        while queue:
            offset = queue.popleft()
            span.append(offset)
            x, y = offset % width, offset // width
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < width and 0 <= ny < height):
                    continue
                neighbor = ny * width + nx
                if neighbor in seen or not walkable(neighbor):
                    continue
                elevation = cells[neighbor].get("elevation")
                if elevation == 15:
                    seen.add(neighbor)
                    queue.append(neighbor)
                elif elevation in step:
                    touching.append(step[elevation])
        level = max(touching, default=0)
        if level:
            for offset in span:
                # a deck over the floor under it, not a block of planks
                out[offset] = {"groundQ16": level, "topQ16": level, "profile": "none",
                               "solid": False, "deck": True}
    return out


def lift_blocked_cells(cells: tuple[dict, ...], width: int, height: int,
                       solved: dict[int, dict], liftable=None) -> dict[int, dict]:
    """Blocked cells that are no terrace course (a house, a tree, a rock) stand
    on the ground nearest them. solve_layout gives levels to walkable regions
    only, so on a level below the highest (Mossdeep's town under its cliffs'
    tops, Sootopolis's lower terraces, the sea a level below the rocks in it)
    houses, trees and rocks stayed at the top level, a tile up on plinths.
    Each blocked cell `liftable(offset)` allows (all, when None; the compiler
    leaves natural cliffs out) takes the level of the walkable ground nearest
    it, counted through such cells: when that is a solved level other than 0
    (outdoors one below the top: dry land, or the sea solved with it; in a cave
    a floor over its lowest), that level is its ground ("lift": its own height
    stands on it). Nearest to unsolved water (the sea at its
    own height), to two levels at once, or to unsolved ground, it is left as
    it was. Returns the lifted cells' entries (graphics overhaul phase 4,
    2026-10-12)."""
    def blocked(offset: int) -> bool:
        return (cells[offset].get("collision", 0) != 0 and offset not in solved
                and role_for_cell(cells[offset]) is None
                and (liftable is None or liftable(offset)))

    def neighbours(offset: int):
        x, y = offset % width, offset // width
        for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
            if 0 <= nx < width and 0 <= ny < height:
                yield ny * width + nx

    def label(offset: int):
        """What a walkable cell next to blocked ones offers them: its level
        (water too, solved with the land), or "water", or "unsolved"."""
        if offset in solved:
            return solved[offset]["groundQ16"]
        if cells[offset].get("behavior") in WATER_BEHAVIORS:
            return "water"
        return "unsolved"

    labels: dict[int, set] = {}
    frontier: list[int] = []
    for offset in range(len(cells)):
        if not blocked(offset):
            continue
        offered = {label(n) for n in neighbours(offset)
                   if cells[n].get("collision", 0) == 0 and role_for_cell(cells[n]) is None}
        if offered:
            labels[offset] = offered
            frontier.append(offset)
    # Further in, level by level: a cell takes what its nearest labelled
    # neighbours offer.
    while frontier:
        reached: dict[int, set] = {}
        for offset in frontier:
            for n in neighbours(offset):
                if blocked(n) and n not in labels:
                    reached.setdefault(n, set()).update(labels[offset])
        labels.update(reached)
        frontier = list(reached)
    lifted: dict[int, dict] = {}
    for offset, offered in labels.items():
        if len(offered) != 1:
            continue
        level = next(iter(offered))
        # outdoors the levels lie below the top (0); in a cave its floors over
        # the lowest (cave_levels)
        if isinstance(level, int) and level != 0:
            lifted[offset] = {"groundQ16": level, "topQ16": level,
                              "profile": "none", "solid": False, "lift": True}
    return lifted
