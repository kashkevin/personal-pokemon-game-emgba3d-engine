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


# General's terrace stairs (the transitions above).
GENERAL_STAIRS = frozenset({175, 207})
# A mountain's walkable top stands this much over the ground its stairs climb
# from (mountain_ledges): one level, the height of the mountain's rock.
MOUNTAIN_LEDGE_Q16 = 16


def _general_stairs(cell: dict) -> bool:
    return (cell.get("tileset") == "gTileset_General"
            and cell.get("localMetatile", cell.get("metatile")) in GENERAL_STAIRS)


def _climb(cell: dict) -> bool:
    """A way up onto a mountain's top for mountain_ledges: General's terrace
    stairs, or a muddy slope (MB_MUDDY_SLOPE, the Mach Bike's: Route 111's rock
    plateau north of the desert has two and no stairs)."""
    return _general_stairs(cell) or cell.get("behavior") == "MB_MUDDY_SLOPE"


def _neighbours(offset: int, width: int, height: int):
    x, y = offset % width, offset // width
    for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
        if 0 <= nx < width and 0 <= ny < height:
            yield ny * width + nx


def mountain_ledges(cells: tuple[dict, ...], width: int, height: int,
                    solved: dict[int, dict]) -> dict[int, dict]:
    """A mountain's walkable top on the mountain: Route 112's plateau (the cable
    car station on it, drawn with the rock's own top, MB_MOUNTAIN_TOP) lay on the
    level of the ground round the mountain, the rock a cell high all round it,
    so it and every path through the rock were trenches between walls (the
    owner's report, 2026-10-24: "a maze of walls"). The game draws stairs up to
    such a top (General's terrace stairs, 175 and 207) or, for the Mach Bike, a
    muddy slope (Route 111's rock plateau north of the desert: it has two and no
    stairs, so it lay on the desert's level inside its rock, a pit); where no
    terrace solves them (solve_layout), the walkable region at the stairs' top that is mostly
    MB_MOUNTAIN_TOP stands MOUNTAIN_LEDGE_Q16 over the region at their foot (its
    own level, 0) and the stairs rise between the two over their rows. The
    mountain's rock, one mass a level high (compile_rules._mountain_masses),
    then lies level with that top. A region is walkable cells side by side at
    one elevation, a cell at elevation 0 (the game's "any level": a doorway, a
    cave mouth) joining any; a ledge (MB_JUMP_*) bounds them. Only a mountain of
    one walkable level: where stairs climb from one such top to another (Route
    115's second plateau) the layout keeps its levels (the rock is one mass a
    level high, so a second level would stand over it), and so does a raised
    top that meets other walkable ground at its side without stairs. Returns
    the raised cells' entries, the stairs' with their rise."""
    def walkable(offset: int) -> bool:
        cell = cells[offset]
        return (cell.get("collision", 0) == 0 and offset not in solved
                and not str(cell.get("behavior") or "").startswith("MB_JUMP_"))

    stairs = {offset for offset in range(len(cells))
              if walkable(offset) and _climb(cells[offset])}
    if not stairs:
        return {}
    region_of: dict[int, int] = {}
    regions: list[list[int]] = []
    for start in range(len(cells)):
        if start in region_of or start in stairs or not walkable(start):
            continue
        index = len(regions)
        region, pending = [], deque((start,))
        region_of[start] = index
        while pending:
            offset = pending.popleft()
            region.append(offset)
            elevation = cells[offset].get("elevation")
            for neighbor in _neighbours(offset, width, height):
                other = cells[neighbor].get("elevation")
                if (neighbor not in region_of and neighbor not in stairs and walkable(neighbor)
                        and (other == elevation or other == 0 or elevation == 0)):
                    region_of[neighbor] = index
                    pending.append(neighbor)
        regions.append(region)
    mountain_top = [2 * sum(cells[offset].get("behavior") == "MB_MOUNTAIN_TOP" for offset in region)
                    >= len(region) for region in regions]

    raised: dict[int, list[list[int]]] = defaultdict(list)
    seen: set[int] = set()
    for start in sorted(stairs):
        if start in seen:
            continue
        run, pending = [], [start]
        seen.add(start)
        while pending:
            offset = pending.pop()
            run.append(offset)
            for neighbor in _neighbours(offset, width, height):
                if neighbor in stairs and neighbor not in seen:
                    seen.add(neighbor)
                    pending.append(neighbor)
        north = {region_of[offset - width] for offset in run
                 if offset >= width and offset - width not in stairs and offset - width in region_of}
        south = {region_of[offset + width] for offset in run
                 if offset + width < len(cells) and offset + width not in stairs
                 and offset + width in region_of}
        if len(north) != 1 or len(south) != 1 or north == south:
            continue
        upper, lower = next(iter(north)), next(iter(south))
        if not mountain_top[upper]:
            continue  # stairs between ordinary ground: the terrace solver's
        if mountain_top[lower]:
            return {}  # a second level on the mountain
        raised[upper].append(run)
    if not raised:
        return {}
    for index in raised:
        for offset in regions[index]:
            for neighbor in _neighbours(offset, width, height):
                if (neighbor in region_of and region_of[neighbor] not in raised
                        and neighbor not in stairs):
                    return {}  # a step between walkable cells with no stairs
    # A pocket of the top that no one walks into (walkable cells, mostly MB_MOUNTAIN_TOP, blocked on every side: three
    # cells south of Route 111's plateau) lies on the raised level too; on the ground's it was a pit in the rock.
    pockets = [index for index, region in enumerate(regions)
               if index not in raised and mountain_top[index]
               and all(region_of.get(neighbor) == index or cells[neighbor].get("collision", 0) != 0
                       for offset in region for neighbor in _neighbours(offset, width, height))]
    out: dict[int, dict] = {}
    for index in list(raised) + pockets:
        for offset in regions[index]:
            out[offset] = {"groundQ16": MOUNTAIN_LEDGE_Q16, "topQ16": MOUNTAIN_LEDGE_Q16,
                           "profile": "none", "solid": False}
    for index, runs in raised.items():
        for run in runs:
            rows = sorted({offset // width for offset in run}, reverse=True)  # south first
            for offset in run:
                step = rows.index(offset // width)
                ground = round(MOUNTAIN_LEDGE_Q16 * step / len(rows))
                top = round(MOUNTAIN_LEDGE_Q16 * (step + 1) / len(rows))
                out[offset] = {"groundQ16": ground, "topQ16": top,
                               "profile": "transition-horizontal", "solid": False,
                               "rise": top - ground}
    return out


def join_level_free_cells(cells: tuple[dict, ...], width: int, height: int,
                          solved: dict[int, dict],
                          beside: frozenset[int] | None = None) -> dict[int, dict]:
    """A walkable cell at elevation 0 (the game's "any level": a cave mouth, a
    doorway) is no region of the terrace solver's, which joins only cells of one
    elevation, so it stayed on the top level: Route 116's way into Rusturf Tunnel
    a level over the grass it is entered from, a shelf on the rock's face. A
    doorway or warp (the behaviour's name says DOOR or WARP) whose walkable
    neighbours, all dry land, offer one solved whole level takes it (through
    others like it; a mouth entered from the water keeps its place);
    with `beside`, only one next to one of those cells (a mountain's: a mouth in
    its face) and those joined through it. Returns the joined cells' entries."""
    def free(offset: int) -> bool:
        cell = cells[offset]
        behavior = str(cell.get("behavior") or "")
        return (cell.get("collision", 0) == 0 and cell.get("elevation") == 0
                and offset not in solved and role_for_cell(cell) is None
                and ("DOOR" in behavior or "WARP" in behavior))

    if beside is not None:
        # the free cells side by side with one next to `beside`
        reach = deque(offset for offset in range(len(cells))
                      if free(offset) and any(neighbor in beside
                                              for neighbor in _neighbours(offset, width, height)))
        allowed = set(reach)
        while reach:
            offset = reach.popleft()
            for neighbor in _neighbours(offset, width, height):
                if neighbor not in allowed and free(neighbor):
                    allowed.add(neighbor)
                    reach.append(neighbor)
        base_free = free
        free = lambda offset: offset in allowed and base_free(offset)  # noqa: E731

    levels = dict(solved)
    out: dict[int, dict] = {}
    pending = [offset for offset in range(len(cells)) if free(offset)]
    changed = True
    while changed and pending:
        changed = False
        rest = []
        for offset in pending:
            around = [neighbor for neighbor in _neighbours(offset, width, height)
                      if cells[neighbor].get("collision", 0) == 0]
            offered = {levels[neighbor]["groundQ16"] for neighbor in around
                       if neighbor in levels and levels[neighbor].get("profile", "none") == "none"}
            if (len(offered) == 1 and next(iter(offered)) % 16 == 0
                    and not any(cells[neighbor].get("behavior") in WATER_BEHAVIORS
                                for neighbor in around)):
                level = next(iter(offered))
                levels[offset] = {"groundQ16": level, "topQ16": level,
                                  "profile": "none", "solid": False}
                if level != 0:  # (on the top level it lies where it was)
                    out[offset] = levels[offset]
                changed = True
            else:
                rest.append(offset)
        pending = rest
    return out


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
