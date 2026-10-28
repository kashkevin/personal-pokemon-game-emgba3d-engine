#!/usr/bin/env python3
"""Compile General terrace artwork into deterministic relative terrain levels."""

from __future__ import annotations

from collections import Counter, defaultdict, deque
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
# A mountain's walkable top stands this much over the ground or top its stairs
# climb from (mountain_ledges): one level a climb.
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


def _climb_regions(cells: tuple[dict, ...], width: int, height: int, solved: dict[int, dict],
                   is_climb) -> tuple[set[int], dict[int, int], list[list[int]],
                                      list[tuple[int, int, list[int]]]]:
    """The regions of mountain_ledges and cave_climbs: walkable cells side by
    side at one elevation, a cell at elevation 0 (the game's "any level": a
    doorway, a cave mouth) joining any; a ledge (MB_JUMP_*), a cell another
    solver placed and the climbs (`is_climb`: stairs, slopes) bound them. Returns
    the climb cells, each region's cells by offset, the regions, and the runs of
    climb cells with one region north of them and another south: (the region
    at the top, the one at the foot, the run)."""
    def walkable(offset: int) -> bool:
        cell = cells[offset]
        return (cell.get("collision", 0) == 0 and offset not in solved
                and not str(cell.get("behavior") or "").startswith("MB_JUMP_"))

    stairs = {offset for offset in range(len(cells))
              if walkable(offset) and is_climb(cells[offset])}
    region_of: dict[int, int] = {}
    regions: list[list[int]] = []
    if not stairs:
        return stairs, region_of, regions, []
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
    runs: list[tuple[int, int, list[int]]] = []
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
        if len(north) == 1 and len(south) == 1 and north != south:
            runs.append((next(iter(north)), next(iter(south)), run))
    return stairs, region_of, regions, runs


def _climb_levels(climbs: list[tuple[int, int, list[int]]], ground: set[int]) -> dict[int, int] | None:
    """Each region's level, `ground` on level 0 and up a level a climb; None
    where the climbs give one region two levels, raise nothing, or put a region
    under the ground."""
    steps: dict[int, list[tuple[int, int]]] = defaultdict(list)
    for upper, lower, _ in climbs:
        steps[lower].append((upper, 1))
        steps[upper].append((lower, -1))
    level = dict.fromkeys(sorted(ground), 0)
    pending = deque(level)
    while pending:
        index = pending.popleft()
        for other, step in steps[index]:
            if other not in level:
                level[other] = level[index] + step
                pending.append(other)
            elif level[other] != level[index] + step:
                return None  # two levels for one region
    if not any(level.values()) or min(level.values()) < 0:
        return None  # nothing raised, or a region under the ground
    return level


def _steps_hold(cells: tuple[dict, ...], width: int, region_of: dict[int, int],
                level: dict[int, int]) -> bool:
    """Whether walkable cells of two regions side by side at two levels
    (`level`, others 0) are a step the game draws: two elevations, which the game
    lets no one cross, the south one the higher (it draws that one's top's edge
    there), or side by side. A step where the game lets one walk across (elevation
    15, a bridge's) or onto a top's south edge from lower walkable ground (no face
    drawn for it) is not."""
    for offset, index in region_of.items():
        for neighbor in ((offset + 1) if (offset + 1) % width else None,
                         (offset + width) if offset + width < len(cells) else None):
            if neighbor is None or region_of.get(neighbor, index) == index:
                continue
            mine, theirs = level.get(index, 0), level.get(region_of[neighbor], 0)
            if mine == theirs:
                continue
            if 15 in (cells[offset].get("elevation"), cells[neighbor].get("elevation")):
                return False  # a step where the game lets one walk across (a bridge's elevation)
            if neighbor == offset + width and mine > theirs:
                return False  # a top's south edge on lower walkable ground
    return True


def _climb_entries(climbs: list[tuple[int, int, list[int]]], level: dict[int, int], width: int,
                   rise_q16: int) -> dict[int, dict]:
    """The climbs' cells, each run rising `rise_q16` from the level of the region
    at its foot over its rows, south first (General's terrace stairs, a muddy
    slope: a ramp)."""
    out: dict[int, dict] = {}
    for _, lower, run in climbs:
        if lower not in level:
            continue
        base = rise_q16 * level[lower]
        rows = sorted({offset // width for offset in run}, reverse=True)  # south first
        for offset in run:
            step = rows.index(offset // width)
            ground = base + round(rise_q16 * step / len(rows))
            top = base + round(rise_q16 * (step + 1) / len(rows))
            out[offset] = {"groundQ16": ground, "topQ16": top,
                           "profile": "transition-horizontal", "solid": False,
                           "rise": top - ground}
    return out


def rock_tops(rock: frozenset[int], ground: dict[int, int], width: int, height: int,
              floor: int) -> dict[int, int]:
    """The top of each cell of `rock` (blocked cells side by side: a mountain's),
    in Q16, from the walkable `ground` beside it, each cell the top it asks the
    rock beside it to have (a path on the mountain's top its own level, other
    ground a level over it): the lowest asked by the walkable cells nearest it
    through the rock (a mountain's top lies level with the paths it meets, as
    the game draws it), or, where a higher top is near, that top less a level a
    cell away from it (the game draws a cliff's face a tile a level: three rows
    of rock under a plateau three levels up descend a level a row to the
    ground), whichever is higher, at least `floor`. Cells the ground does not
    reach through the rock get `floor`."""
    def neighbours(offset: int):
        x, y = offset % width, offset // width
        for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
            if 0 <= nx < width and 0 <= ny < height:
                yield ny * width + nx

    # distance through the rock to the nearest walkable cell of each level
    distance: dict[int, dict[int, int]] = {}
    for level in sorted(set(ground.values())):
        seen = {}
        frontier = sorted({neighbor for offset, value in ground.items() if value == level
                           for neighbor in neighbours(offset) if neighbor in rock})
        for offset in frontier:
            seen[offset] = 1
        step = 1
        while frontier:
            step += 1
            ring = []
            for offset in frontier:
                for neighbor in neighbours(offset):
                    if neighbor in rock and neighbor not in seen:
                        seen[neighbor] = step
                        ring.append(neighbor)
            frontier = ring
        distance[level] = seen
    tops: dict[int, int] = {}
    for offset in rock:
        reach = {level: seen[offset] for level, seen in distance.items() if offset in seen}
        if not reach:
            tops[offset] = floor
            continue
        nearest = min(reach.values())
        flat = min(level for level, steps in reach.items() if steps == nearest)
        face = max(level - MOUNTAIN_LEDGE_Q16 * (steps - 1) for level, steps in reach.items())
        tops[offset] = max(floor, flat, face)
    return tops


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
    MB_MOUNTAIN_TOP stands MOUNTAIN_LEDGE_Q16 over the region at their foot and
    the stairs rise between the two over their rows. A region is walkable cells
    side by side at one elevation, a cell at elevation 0 (the game's "any
    level": a doorway, a cave mouth) joining any; a ledge (MB_JUMP_*) bounds
    them. Ground that is not mostly the mountain's top lies on its own level
    (0), and a mountain of several tops climbs from it a level a climb: Route
    115's stairs up to its plateau, a row of stairs down from that to a lower
    top, a muddy slope up to a third (Route 114's five levels the same). The
    mountain's rock (compile_rules._mountain_masses) then rises to each top.
    Two regions side by side at two elevations (the game lets no one cross)
    can lie at two levels where the south one is the higher: the game draws its
    top's edge there. Where the climbs give one region two levels, or put a top
    under the ground, or a top's south edge on lower walkable ground (no face
    for it), or a step where the game lets one walk across (elevation 15, a
    bridge's: Route 119's top runs onto its bridge over the forest), the layout
    keeps its levels. (The game draws a face a tile a level, its stairs a row a
    tile: Route 112's climb three rows, Route 115's two and one. A level a
    climb keeps Route 111's and Route 112's plateaus a level up, as they were.)
    Returns the raised cells' entries, the stairs' with their rise."""
    stairs, region_of, regions, runs = _climb_regions(cells, width, height, solved, _climb)
    mountain_top = [2 * sum(cells[offset].get("behavior") == "MB_MOUNTAIN_TOP" for offset in region)
                    >= len(region) for region in regions]
    # (stairs between ordinary ground are the terrace solver's)
    climbs = [(upper, lower, run) for upper, lower, run in runs if mountain_top[upper]]
    # Each region's level, from the ground up a level a climb (a mountain's tops
    # that no climb joins to the ground keep theirs)
    level = _climb_levels(climbs, {index for upper, lower, _ in climbs for index in (upper, lower)
                                   if not mountain_top[index]})
    if level is None or not _steps_hold(cells, width, region_of, level):
        return {}
    # A pocket of the top that no one walks into (walkable cells, mostly MB_MOUNTAIN_TOP, blocked on every side: three
    # cells south of Route 111's plateau) lies where the rock round it does (rock_tops, through the rock and other
    # pockets); on the ground's level it was a pit in the rock.
    pockets = {index for index, region in enumerate(regions)
               if index not in level and mountain_top[index]
               and all(region_of.get(neighbor) == index or cells[neighbor].get("collision", 0) != 0
                       for offset in region for neighbor in _neighbours(offset, width, height))}
    raised = {index: value for index, value in level.items() if value}
    if pockets:
        rock = frozenset(offset for offset in range(len(cells))
                         if cells[offset].get("collision", 0) != 0
                         or region_of.get(offset) in pockets)
        tops = rock_tops(rock, {offset: MOUNTAIN_LEDGE_Q16 * level.get(index, 0)
                                + (0 if cells[offset].get("behavior") == "MB_MOUNTAIN_TOP"
                                   else MOUNTAIN_LEDGE_Q16)
                                for offset, index in region_of.items() if index not in pockets},
                         width, height, MOUNTAIN_LEDGE_Q16)
        for index in sorted(pockets):
            raised[index] = max(tops.get(offset, MOUNTAIN_LEDGE_Q16) for offset in regions[index]) // MOUNTAIN_LEDGE_Q16
    out: dict[int, dict] = {}
    for index, value in raised.items():
        for offset in regions[index]:
            out[offset] = {"groundQ16": MOUNTAIN_LEDGE_Q16 * value, "topQ16": MOUNTAIN_LEDGE_Q16 * value,
                           "profile": "none", "solid": False}
    out.update(_climb_entries(climbs, level, width, MOUNTAIN_LEDGE_Q16))
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


# A jump ledge's take-off and landing cells: (x, y) steps from the ledge's own cell.
LEDGE_JUMPS = {
    "MB_JUMP_SOUTH": (0, 1), "MB_JUMP_NORTH": (0, -1), "MB_JUMP_EAST": (1, 0), "MB_JUMP_WEST": (-1, 0),
    "MB_JUMP_SOUTHEAST": (1, 1), "MB_JUMP_SOUTHWEST": (-1, 1), "MB_JUMP_NORTHEAST": (1, -1),
    "MB_JUMP_NORTHWEST": (-1, -1),
}


def solve_layout(cells: tuple[dict, ...], width: int, height: int,
                 contradictions: list[tuple[int, ...]] | None = None,
                 conflict_edges: list[tuple[object, object, int, int]] | None = None,
                 shifts: dict[int, int] | None = None,
                 members: dict[int, int] | None = None
                 ) -> dict[int, dict]:
    """Return solved cells; contradictory connected topology components are omitted.

    A component's key is the lowest cell offset in it. shifts: levels a component
    moves by, by key (seam_shifts). members: filled with each solved region cell's
    component key."""
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

    # The regions a jump ledge leaves for ground no terrace reaches (a region with
    # no node in the graph: the top level).
    onto_free: set[int] = set()
    for offset, cell in enumerate(cells):
        step = LEDGE_JUMPS.get(cell.get("behavior"))
        if step is None:
            continue
        x, y = offset % width, offset // width
        tx, ty, lx, ly = x - step[0], y - step[1], x + step[0], y + step[1]
        if 0 <= tx < width and 0 <= ty < height and 0 <= lx < width and 0 <= ly < height:
            takeoff = region_by_cell.get(ty * width + tx)
            landing = region_by_cell.get(ly * width + lx)
            if takeoff is not None and landing is not None and landing not in graph:
                onto_free.add(takeoff)

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
        if any(values[node] < anchor for node in component if node in onto_free):
            # A ledge is jumped down, never up (KNOWN_ISSUES MS-154). The highest
            # node is the top level, and where that is only terrace art (a course's
            # top no one walks on: Route 123's mountain over its tall grass, Route
            # 113's cliffs) the walkable regions all lay below it while ground no
            # terrace reaches, the rest of the route, lay on it: every jump from the
            # grass onto the route went up a level. The component's highest
            # walkable ground lies on the top level then, the art's top over it.
            anchor = max(values[node] for node in component if not isinstance(node, tuple))
        key = min(node[0] if isinstance(node, tuple) else min(region_cells[node]) for node in component)
        if shifts:
            anchor -= shifts.get(key, 0)
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
                    if members is not None:
                        members[offset] = key
    # Ground no terrace reaches lies on level 0. Its key: minus one less its
    # lowest offset (a component's is its lowest offset). A doorway of elevation 0
    # (the game's any level: a cave mouth) has none: it takes its level from the
    # ground it is entered from (join_level_free_cells).
    for node, offsets in region_cells.items():
        if node in graph or (cells[offsets[0]].get("elevation") == 0 and any(
                "DOOR" in str(cells[offset].get("behavior") or "")
                or "WARP" in str(cells[offset].get("behavior") or "") for offset in offsets)):
            continue
        key = -min(offsets) - 1
        if members is not None:
            for offset in offsets:
                members[offset] = key
        if shifts and shifts.get(key):
            for offset in offsets:
                solved[offset] = {"groundQ16": shifts[key] * 16, "topQ16": shifts[key] * 16,
                                  "profile": "none", "solid": False}
    return solved


def seam_shifts(layouts: dict[str, dict], seams: list[tuple[tuple[str, int], tuple[str, int]]],
                rounds: int = 8) -> dict[str, dict[int, int]]:
    """The levels each map's ground moves by to meet the next maps' ground at its
    level (KNOWN_ISSUES MS-162). Each layout is solved alone, its top level 0, and
    where two maps meet their ground could lie a level or two apart: Route 118's
    route a level under Mauville City, Route 119 and Route 123 round it (its top
    level is its cliffs' tops), Route 111's south end two under Mauville.

    The ground moves in groups that keep their levels to each other: a terrace
    component's regions; the components a walkable cell pair or a jump ledge joins
    in a layout; the ground round one mountain (its art cells: a mountain stands
    between the levels it divides, Route 116's between its grass and its east
    plateau, Route 118's cliffs under its lake's plateau); water with the ground
    round it (a lake lies level with its banks), but for the sea the next maps
    share. A group with a component in it that meets another map moves when every
    land cell pair it has with other ground (across the seams, and in its layout
    across an elevation change) says the same, and no jump ledge between it and
    other ground then goes up. Ground no terrace reaches and nothing joins to a
    component (a town, Route 118's corner by Mauville behind its trees) is where
    the others move to, and what a mountain or water joins to it stays. The open sea is the renderer's (buildings.c lays it at one
    level); ground another step gave a level (a cave mouth, a lifted cell) has no
    say.

    layouts: layout id -> {"cells", "width", "height", "members" (solve_layout's:
    every region cell's key), "levels" (groundQ16 of each cell with a terrace
    entry), "mountains" (the mountain art cells, optional)}. seams: the cell pairs
    side by side across the maps' connections. Returns layout id -> {key: levels}
    for solve_layout's shifts."""
    free = ("free",)

    def land(lid: str, offset: int) -> bool:
        cell = layouts[lid]["cells"][offset]
        behavior = cell.get("behavior")
        return (cell.get("collision", 0) == 0 and behavior not in WATER_BEHAVIORS
                and behavior not in LEDGE_JUMPS)

    def unit(lid: str, offset: int):
        """A region's (lid, key); free for ground of a layout no terrace reaches at
        all (level 0); None for ground another step gave a level."""
        key = layouts[lid]["members"].get(offset)
        if key is not None:
            return (lid, key)
        return free if offset not in layouts[lid]["levels"] else None

    def level(lid: str, offset: int) -> int:
        return round(layouts[lid]["levels"].get(offset, 0) / 16)

    parent: dict[tuple, tuple] = {}

    def find(u: tuple) -> tuple:
        parent.setdefault(u, u)
        while parent[u] != u:
            parent[u] = parent[parent[u]]
            u = parent[u]
        return u

    def join(a: tuple, b: tuple) -> None:
        parent[find(a)] = find(b)

    seam_cells: dict[str, set[int]] = defaultdict(set)
    for (lid_a, offset_a), (lid_b, offset_b) in seams:
        seam_cells[lid_a].add(offset_a)
        seam_cells[lid_b].add(offset_b)
    pairs: list[tuple[tuple[str, int], tuple[str, int]]] = []
    ledges: list[tuple[tuple[str, int], tuple[str, int]]] = []
    # (ground a mountain or water joins to ground no terrace levels stays; ground
    # another step gave a level follows what it was given it from)
    pinned: set[tuple] = set()
    for lid, info in sorted(layouts.items()):
        cells, width, height = info["cells"], info["width"], info["height"]
        members = info["members"]
        for u in {(lid, key) for key in members.values()}:
            find(u)
        for offset, cell in enumerate(cells):
            u = unit(lid, offset) if land(lid, offset) else None
            if u is not None and u is not free:
                for n in _neighbours(offset, width, height):
                    v = unit(lid, n) if land(lid, n) else None
                    if v is None or v == u or (v is not free and v < u):
                        continue
                    if v is not free and u[1] >= 0 and v[1] >= 0:
                        join(u, v)
                    else:
                        pairs.append(((lid, offset), (lid, n)))
            step = LEDGE_JUMPS.get(cell.get("behavior"))
            if step is None:
                continue
            x, y = offset % width, offset // width
            tx, ty, lx, ly = x - step[0], y - step[1], x + step[0], y + step[1]
            if not (0 <= tx < width and 0 <= ty < height and 0 <= lx < width and 0 <= ly < height):
                continue
            takeoff, landing = ty * width + tx, ly * width + lx
            if not (land(lid, takeoff) and land(lid, landing)):
                continue
            ut, ul = unit(lid, takeoff), unit(lid, landing)
            if ut is None or ul is None or ut == ul or ut is free and ul is free:
                continue
            if ut is not free and ul is not free and ut[1] >= 0 and ul[1] >= 0:
                join(ut, ul)
            else:
                ledges.append(((lid, takeoff), (lid, landing)))
        # the ground round each mountain together
        art = frozenset(info.get("mountains", ()))
        seen: set[int] = set()
        for start in sorted(art):
            if start in seen:
                continue
            seen.add(start)
            pending, around, fixed = deque((start,)), set(), False
            while pending:
                offset = pending.popleft()
                for n in _neighbours(offset, width, height):
                    if n in art:
                        if n not in seen:
                            seen.add(n)
                            pending.append(n)
                    elif land(lid, n):
                        if unit(lid, n) is free:
                            fixed = True
                        elif unit(lid, n) is not None:
                            around.add(unit(lid, n))
            around = sorted(around)
            for u in around[1:]:
                join(around[0], u)
            if fixed:
                pinned.update(around)
        # water with the ground round it (a lake lies level with its banks), but for
        # the sea the next maps share (the renderer's)
        waters = defaultdict(list)
        for offset, key in members.items():
            if cells[offset].get("behavior") in WATER_BEHAVIORS:
                waters[key].append(offset)
        for key, water in sorted(waters.items()):
            if any(offset in seam_cells[lid] for offset in water):
                continue
            banks = {unit(lid, n) for offset in water for n in _neighbours(offset, width, height)
                     if land(lid, n)}
            for u in sorted(banks - {None, free}):
                join((lid, key), u)
            if free in banks:
                pinned.update(banks - {None, free} | {(lid, key)})
    at_seam: set[tuple] = set()
    for a, b in seams:
        if land(*a) and land(*b) and unit(*a) is not None and unit(*b) is not None \
                and not (unit(*a) is free and unit(*b) is free):
            pairs.append((a, b))
            at_seam.update(u for u in (unit(*a), unit(*b)) if u is not free)

    shift: dict[tuple, int] = defaultdict(int)

    def group(cell: tuple[str, int]):
        u = unit(*cell)
        return None if u is free else find(u)

    def now(cell: tuple[str, int], g) -> int:
        return level(*cell) + (shift[g] if g is not None else 0)

    pairs_of: dict[tuple, list] = defaultdict(list)
    for a, b in pairs:
        ga, gb = group(a), group(b)
        if ga != gb:
            for g in {ga, gb} - {None}:
                pairs_of[g].append((a, ga, b, gb))
    ledges_of: dict[tuple, list] = defaultdict(list)
    for takeoff, landing in ledges:
        gt, gl = group(takeoff), group(landing)
        if gt != gl:
            for g in {gt, gl} - {None}:
                ledges_of[g].append((takeoff, gt, landing, gl))

    def votes(g: tuple) -> Counter:
        out = Counter()
        for a, ga, b, gb in pairs_of[g]:
            mine, other = ((a, ga), (b, gb)) if ga == g else ((b, gb), (a, ga))
            out[now(*other) - now(*mine)] += 1
        return out

    def ledges_hold(g: tuple, move: int) -> bool:
        for takeoff, gt, landing, gl in ledges_of[g]:
            drop = now(takeoff, gt) - now(landing, gl)
            after = drop + (move if gt == g else 0) - (move if gl == g else 0)
            if drop >= 0 > after:
                return False
        return True

    terraced = {find(u) for u in parent if u[1] >= 0}
    groups = sorted({find(u) for u in at_seam} & terraced - {find(u) for u in pinned},
                    key=lambda g: (-len(pairs_of[g]), str(g)))
    for _ in range(rounds):
        moved = False
        for g in groups:
            tally = votes(g)
            if len(tally) != 1:
                continue
            move = next(iter(tally))
            if move != 0 and ledges_hold(g, move):
                shift[g] += move
                moved = True
        if not moved:
            break
    out: dict[str, dict[int, int]] = defaultdict(dict)
    for u in list(parent):
        if shift[find(u)]:
            out[u[0]][u[1]] = shift[find(u)]
    return dict(out)


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


def cave_climbs(cells: tuple[dict, ...], width: int, height: int,
                solved: dict[int, dict] | None = None) -> dict[int, dict]:
    """A cave's muddy slope (MB_MUDDY_SLOPE, the Mach Bike's) between two parts
    of one floor: Granite Cave B1F's (9,9-10), its halves at one elevation joined
    only by it, lay flat, both ends on one level (KNOWN_ISSUES MS-153). The part
    at its top (to its north: the GBA draws the face it climbs on the south of
    the higher part) stands CAVE_RISE_Q16 over the part at its foot and the
    slope rises between them over its rows, as a mountain's (mountain_ledges);
    the cave's rock round the raised part stands on it
    (compile_rules: lift_blocked_cells). Where the climbs give a part two levels,
    or would make a step the game draws no face for or lets one walk across
    (_steps_hold), the cave keeps its levels. Returns the raised cells' entries,
    the slope's with its rise."""
    solved = solved or {}
    _, region_of, regions, runs = _climb_regions(
        cells, width, height, solved, lambda cell: cell.get("behavior") == "MB_MUDDY_SLOPE")
    tops = {upper for upper, _, _ in runs}
    level = _climb_levels(runs, {index for upper, lower, _ in runs for index in (upper, lower)
                                 if index not in tops})
    if level is None or not _steps_hold(cells, width, region_of, level):
        return {}
    out = {offset: {"groundQ16": CAVE_RISE_Q16 * value, "topQ16": CAVE_RISE_Q16 * value,
                    "profile": "none", "solid": False}
           for index, value in level.items() if value for offset in regions[index]}
    out.update(_climb_entries(runs, level, width, CAVE_RISE_Q16))
    return out


# A slope's cells: the Mach Bike's muddy slopes and the Acro Bike's bumpy ones.
SLOPES = frozenset({"MB_MUDDY_SLOPE", "MB_BUMPY_SLOPE"})
# A slope's top stands this much over its foot (slope_tops): a level.
SLOPE_RISE_Q16 = 16


def slope_tops(cells: tuple[dict, ...], width: int, height: int,
               solved: dict[int, dict], rock: frozenset[int] = frozenset()) -> dict[int, dict]:
    """A slope's top on the level of the ground round it: Route 119's muddy
    slope (6,54-55) climbs to a row at the elevation of its foot (the game lets
    one ride off the slope onto it) that the GBA draws on the plateau over the
    slope's rock, a row of elevation 0 (the game's "any level") joining it to
    the rest of the plateau. The solver, joining cells of one elevation, put the
    row on the foot's level, a step under the plateau beside it, and the slope
    lay flat (KNOWN_ISSUES MS-153). The region at a slope's top (walkable cells
    side by side at one elevation, slopes and terrace courses bounding them),
    apart from the region at its foot and on its level, whose walkable cells
    round it (slopes aside) all lie SLOPE_RISE_Q16 over it, takes their level,
    the slope a ramp up to it over its rows, and the mountain's rock (`rock`)
    beside the slope and under the top on the foot: the face it climbs, a tile
    high. A top with nothing round it (the slope its only way, Route 115's, or a
    map's edge, the Safari Zone's), or with ground round it at another level,
    keeps its level. Returns the raised cells' entries, the slope's with its
    rise, and the face's."""
    def walkable(offset: int) -> bool:
        cell = cells[offset]
        return (cell.get("collision", 0) == 0 and cell.get("behavior") not in SLOPES
                and role_for_cell(cell) is None)

    def level(offset: int) -> int | None:
        """A walkable cell's level (ground no terrace reaches: 0); None for a
        course's cell or water no terrace reaches (the sea at its own height)."""
        entry = solved.get(offset)
        if entry is None:
            return None if cells[offset].get("behavior") in WATER_BEHAVIORS else 0
        return entry["groundQ16"] if entry.get("profile", "none") == "none" else None

    slope = {offset for offset, cell in enumerate(cells)
             if cell.get("collision", 0) == 0 and cell.get("behavior") in SLOPES}
    if not slope:
        return {}
    region_of: dict[int, int] = {}
    regions: list[list[int]] = []
    for start in range(len(cells)):
        if start in region_of or not walkable(start):
            continue
        index = len(regions)
        region, pending = [], deque((start,))
        region_of[start] = index
        while pending:
            offset = pending.popleft()
            region.append(offset)
            for neighbor in _neighbours(offset, width, height):
                if (neighbor not in region_of and walkable(neighbor)
                        and cells[neighbor].get("elevation") == cells[offset].get("elevation")):
                    region_of[neighbor] = index
                    pending.append(neighbor)
        regions.append(region)
    # each run of slope cells down a column, by the region at its top
    climbs: dict[int, list[tuple[int, int | None, list[int]]]] = defaultdict(list)
    for start in sorted(slope):
        if start - width in slope or start < width:
            continue
        run, offset = [], start
        while offset in slope:
            run.append(offset)
            offset += width
        if start - width in region_of:
            climbs[region_of[start - width]].append(
                (region_of[start - width], region_of.get(offset), run))
    out: dict[int, dict] = {}
    for upper, runs in sorted(climbs.items()):
        lowers = {lower for _, lower, _ in runs}
        if len(lowers) != 1 or None in lowers or upper in lowers:
            continue  # a foot at no region or two, or the top's own region
        lower = next(iter(lowers))
        tops = {level(offset) for offset in regions[upper]}
        feet = {level(offset) for offset in regions[lower]}
        if len(tops) != 1 or tops != feet or None in tops or next(iter(tops)) % 16:
            continue  # not flat on one whole level
        foot = next(iter(feet))
        inside = set(regions[upper])
        around = {level(neighbor) for offset in inside
                  for neighbor in _neighbours(offset, width, height)
                  if neighbor not in inside and neighbor not in slope
                  and cells[neighbor].get("collision", 0) == 0}
        if around != {foot + SLOPE_RISE_Q16}:
            continue
        for offset in inside:
            out[offset] = {"groundQ16": foot + SLOPE_RISE_Q16, "topQ16": foot + SLOPE_RISE_Q16,
                           "profile": "none", "solid": False}
        out.update(_climb_entries(runs, {lower: foot // SLOPE_RISE_Q16,
                                         upper: foot // SLOPE_RISE_Q16 + 1},
                                  width, SLOPE_RISE_Q16))
        # the face the slope climbs (the rock beside it, and under the top's
        # south edge) stands on the foot, as it did: beside the raised top,
        # lift_blocked_cells left it on the top level, a tile over the top
        face = {side for _, _, run in runs for offset in run
                for side in (offset - 1 if offset % width else None,
                             offset + 1 if (offset + 1) % width else None)
                if side is not None}
        face |= {offset + width for offset in inside if offset + width < len(cells)}
        for offset in sorted((face & rock) - slope - set(solved)):
            out[offset] = {"groundQ16": foot, "topQ16": foot, "profile": "none",
                           "solid": False, "lift": True}
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
