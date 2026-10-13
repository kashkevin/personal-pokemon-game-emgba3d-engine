#!/usr/bin/env python3
"""Generate the shared wild spawn catalogue for the multiplayer host.

EmeraldTogether Phase 3 (docs/shared-spawn-architecture.md): while a game is
connected to a multiplayer world, the host chooses every visible land wild
Pokemon (species, level, personality, shiny, tile) and moves it around; the
games only draw it. The host therefore needs this engine's own data:

  * every map that has land encounters: its map group/number, region map
    section, map type, default weather and the vanilla land encounter slots
    (rates and level ranges; the level still follows the map, as offline);
  * the habitat tiles a wild Pokemon may stand on: the tiles on which the game
    itself starts land encounters (TILE_FLAG_HAS_ENCOUNTERS and not surfable,
    as MetatileBehavior_IsLandWildEncounter), without collision, not on the
    transition/multi-level elevations the overworld spawner refuses, away from
    warps, triggers and the map's own objects, and in an area a player can walk
    into (the same tiles the offline overworld spawner may use, so a map with
    no such tiles has no visible land Pokemon offline either);
  * the randomized Gen 1-6 pool (src/data/mp_wild_pool.h) with numeric species
    IDs, the area type boosts of src/mp_wild_random.c (GetAreaTypeBoost) and the
    weather enum, so the host weights exactly as MPWild_RandomizeSpecies does.

Everything numeric comes from this engine, so the catalogue matches the game
built from it. The output is a Python module for the app repository.

Usage:
  python3 tools/mp/gen_shared_spawns.py --output <app>/src/emerald_multiplayer/shared_spawn_catalogue.py
  python3 tools/mp/gen_shared_spawns.py --ascii Route102     # show a map's habitat tiles
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_world_events as gw  # noqa: E402

ENGINE = gw.ENGINE
FORMAT = 1

# Tiles a wild Pokemon must keep clear of: doors/warps and scripted triggers
# (a spawn must never block an entrance or a story trigger), and the map's own
# objects within their movement range plus one tile.
WARP_CLEARANCE = 1
OBJECT_CLEARANCE = 1


def tile_flags() -> dict[str, int]:
    """sTileBitAttributes of src/metatile_behavior.c: behaviour -> flag bits."""
    text = (ENGINE / "src/metatile_behavior.c").read_text()
    flags = {name: 1 << int(bit) for name, bit in re.findall(r"#define (TILE_FLAG_\w+)\s+\(1 << (\d+)\)", text)}
    body = text[text.index("sTileBitAttributes"):]
    body = body[body.index("{") + 1:body.index("};")]
    out = {}
    for behavior, expr in re.findall(r"\[(MB_\w+)\]\s*=\s*([^,\n]+),", body):
        value = 0
        for name in re.findall(r"TILE_FLAG_\w+", expr):
            value |= flags[name]
        out[behavior] = value
    return out


def land_encounter_behaviors() -> set[str]:
    flags = tile_flags()
    return {name for name, value in flags.items() if value & 1 and not value & 2}


def area_type_boosts() -> tuple[dict[str, list[str]], list[str]]:
    """GetAreaTypeBoost (src/mp_wild_random.c): mapsec -> boosted types, and
    the types every underground map gets."""
    text = (ENGINE / "src/mp_wild_random.c").read_text()
    start = text.index("static u32 GetAreaTypeBoost(void)")
    body = text[start:text.index("\n}\n", start)]
    switch = body[body.index("switch"):body.index("    }\n")]
    boosts, pending = {}, []
    for line in switch.splitlines():
        line = line.strip()
        case = re.match(r"case (MAPSEC_\w+):", line)
        if case:
            pending.append(case.group(1))
            continue
        ret = re.match(r"return (.*);", line)
        if ret and pending:
            types = re.findall(r"TYPE_BIT\((TYPE_\w+)\)", ret.group(1))
            for mapsec in pending:
                boosts[mapsec] = types
            pending = []
    tail = body[body.index("MAP_TYPE_UNDERGROUND"):]
    underground = re.findall(r"TYPE_BIT\((TYPE_\w+)\)", tail[:tail.index(";")])
    if not boosts or not underground:
        raise SystemExit("could not read GetAreaTypeBoost from src/mp_wild_random.c")
    return boosts, underground


def weather_enum() -> dict[str, int]:
    text = (ENGINE / "include/constants/weather.h").read_text()
    body = text[text.index("enum"):]
    body = body[body.index("{") + 1:body.index("};")]
    names = re.findall(r"^\s*(WEATHER_\w+)\s*,", body, re.M)
    return {name: index for index, name in enumerate(names) if name != "WEATHER_COUNT"}


def wild_pool() -> tuple[list[tuple[str, int, int]], list[tuple]]:
    text = (ENGINE / "src/data/mp_wild_pool.h").read_text()
    members_body = text[text.index("sMPWildMembers[]"):]
    members_body = members_body[members_body.index("{") + 1:members_body.index("};")]
    members = [(name, int(level), int(depth)) for name, level, depth in
               re.findall(r"\{(SPECIES_\w+),\s*(\d+),\s*(\d+)\}", members_body)]
    families_body = text[text.index("sMPWildFamilies["):]
    families_body = families_body[families_body.index("{") + 1:families_body.index("};")]
    families = [(int(first), int(count), tier, int(habitat), int(time), type1, type2) for
                first, count, tier, habitat, time, type1, type2 in
                re.findall(r"\{(\d+),\s*(\d+),\s*MP_WILD_TIER_(\w+),\s*(\d+),\s*(\d+),\s*(TYPE_\w+),\s*(TYPE_\w+)\}",
                           families_body)]
    count = int(re.search(r"#define MP_WILD_FAMILY_COUNT (\d+)", text).group(1))
    if len(families) != count or not members:
        raise SystemExit("src/data/mp_wild_pool.h does not match its family count")
    return members, families


def map_folders() -> dict[str, str]:
    out = {}
    for path in sorted((ENGINE / "data/maps").glob("*/map.json")):
        data = gw.read_json(path)
        if "id" in data:
            out[data["id"]] = path.parent.name
    return out


def land_encounters() -> dict[str, dict]:
    """The first gWildMonHeaders entry with land_mons per map constant."""
    data = gw.read_json(ENGINE / "src/data/wild_encounters.json")
    group = next(g for g in data["wild_encounter_groups"] if g["label"] == "gWildMonHeaders")
    rates = next(f["encounter_rates"] for f in group["fields"] if f["type"] == "land_mons")
    out = {}
    for entry in group["encounters"]:
        if "land_mons" in entry and entry["map"] not in out:
            mons = entry["land_mons"]["mons"]
            out[entry["map"]] = {"rates": list(rates),
                                 "levels": [[mon["min_level"], mon["max_level"]] for mon in mons]}
    return out


def habitat_tiles(grid: gw.MapGrid, land: set[str]) -> list[tuple[int, int, int]]:
    """Land encounter tiles a wild Pokemon may stand on (see the module notes)."""
    clear = set()
    for warp in grid.map.get("warp_events", []):
        for dx in range(-WARP_CLEARANCE, WARP_CLEARANCE + 1):
            for dy in range(-WARP_CLEARANCE, WARP_CLEARANCE + 1):
                clear.add((warp["x"] + dx, warp["y"] + dy))
    for event in grid.map.get("coord_events", []) + grid.map.get("bg_events", []):
        clear.add((event["x"], event["y"]))
    for obj in grid.map.get("object_events", []):
        if obj.get("type") == "clone":
            continue
        rx = int(obj.get("movement_range_x", 0) or 0) + OBJECT_CLEARANCE
        ry = int(obj.get("movement_range_y", 0) or 0) + OBJECT_CLEARANCE
        for dx in range(-rx, rx + 1):
            for dy in range(-ry, ry + 1):
                clear.add((obj["x"] + dx, obj["y"] + dy))

    def habitat(x: int, y: int) -> bool:
        if not gw.in_bounds(grid, x, y) or grid.collision(x, y) != 0 or (x, y) in clear:
            return False
        behavior = grid.behavior(x, y)
        return behavior in land and grid.elevation(x, y) not in (0, 15)

    # Keep only areas a player can walk into: the passable components holding
    # a warp or touching a map edge that has a connection.
    entrances = set()
    for warp in grid.map.get("warp_events", []):
        entrances.add((warp["x"], warp["y"]))
    for connection in grid.map.get("connections") or []:
        direction = connection["direction"]
        for i in range(max(grid.width, grid.height)):
            if direction == "up":
                entrances.add((i, 0))
            elif direction == "down":
                entrances.add((i, grid.height - 1))
            elif direction == "left":
                entrances.add((0, i))
            elif direction == "right":
                entrances.add((grid.width - 1, i))
    reachable: set[tuple[int, int]] = set()
    seen: set[tuple[int, int]] = set()
    for y in range(grid.height):
        for x in range(grid.width):
            if (x, y) in seen or not gw.passable(grid, x, y):
                continue
            component, queue = set(), deque([(x, y)])
            seen.add((x, y))
            while queue:
                cx, cy = queue.popleft()
                component.add((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if (nx, ny) not in seen and gw.passable(grid, nx, ny) and gw.can_step(grid, cx, cy, nx, ny):
                        seen.add((nx, ny))
                        queue.append((nx, ny))
            if component & entrances:
                reachable |= component
    tiles = [(x, y, grid.elevation(x, y)) for y in range(grid.height) for x in range(grid.width)
             if habitat(x, y) and (x, y) in reachable]
    if not tiles:
        # Maps entered only by scripted means (no warp or connection reaches
        # the encounter area in the data): keep every habitat tile.
        tiles = [(x, y, grid.elevation(x, y)) for y in range(grid.height) for x in range(grid.width) if habitat(x, y)]
    return tiles


def encode_runs(tiles: list[tuple[int, int, int]]) -> list[list[int]]:
    """Row runs [y, x0, length, elevation] of consecutive tiles."""
    runs: list[list[int]] = []
    for x, y, elevation in sorted(tiles, key=lambda t: (t[1], t[0])):
        if runs and runs[-1][0] == y and runs[-1][1] + runs[-1][2] == x and runs[-1][3] == elevation:
            runs[-1][2] += 1
        else:
            runs.append([y, x, 1, elevation])
    return runs


def display_name(mapsec: str) -> str:
    name = mapsec.removeprefix("MAPSEC_")
    name = re.sub(r"(?<=[A-Z])\d$", "", name)  # FIERY_PATH2 -> FIERY_PATH
    return name.replace("_", " ")[:24]


def build() -> dict:
    behaviors = gw.metatile_behaviors()
    layouts = {l["id"]: l for l in gw.read_json(ENGINE / "data/layouts/layouts.json")["layouts"] if "id" in l}
    attrs = gw.tileset_attributes()
    constants = gw.map_constants()
    folders = map_folders()
    land = land_encounter_behaviors()
    boosts, underground_types = area_type_boosts()
    members, families = wild_pool()
    species_names = sorted({name for name, _, _ in members})
    species_ids = gw.evaluate_symbols("constants/species.h", species_names)
    type_names = sorted({f[5] for f in families} | {f[6] for f in families}
                        | {t for types in boosts.values() for t in types} | set(underground_types))
    maps = {}
    for constant, encounter in sorted(land_encounters().items()):
        if constant not in constants or constant not in folders:
            continue  # data for maps this Emerald build does not have
        grid = gw.MapGrid(folders[constant], layouts, attrs, behaviors)
        tiles = habitat_tiles(grid, land)
        if not tiles:
            continue
        group, number = constants[constant]
        mapsec = grid.map.get("region_map_section", "MAPSEC_NONE")
        map_type = grid.map.get("map_type", "MAP_TYPE_NONE")
        boost = boosts.get(mapsec)
        if boost is None and map_type == "MAP_TYPE_UNDERGROUND":
            boost = underground_types
        maps[f"{group}.{number}"] = {
            "constant": constant, "folder": grid.folder, "name": display_name(mapsec), "mapsec": mapsec,
            "map_type": map_type, "underground": map_type == "MAP_TYPE_UNDERGROUND",
            "weather": grid.map.get("weather", "WEATHER_NONE"),
            "width": grid.width, "height": grid.height, "boost": boost or [],
            "rates": encounter["rates"], "levels": encounter["levels"],
            "tiles": encode_runs(tiles), "tile_count": len(tiles),
        }
    return {
        "format": FORMAT,
        "generated_by": "tools/mp/gen_shared_spawns.py",
        "tier_weights": {"COMMON": 30, "UNCOMMON": 15, "RARE": 6, "VERY_RARE": 3},
        "lower_stage_chance": 35,
        "habitat_land": 1,
        "types": type_names,
        "weather": weather_enum(),
        "members": [[species_ids[name], level, depth] for name, level, depth in members],
        "member_names": [name.removeprefix("SPECIES_") for name, _, _ in members],
        "families": [[first, count, tier, habitat, time, type1, type2]
                     for first, count, tier, habitat, time, type1, type2 in families],
        "maps": maps,
    }


def ascii_habitat(folder: str) -> str:
    behaviors = gw.metatile_behaviors()
    layouts = {l["id"]: l for l in gw.read_json(ENGINE / "data/layouts/layouts.json")["layouts"] if "id" in l}
    grid = gw.MapGrid(folder, layouts, gw.tileset_attributes(), behaviors)
    tiles = {(x, y) for x, y, _ in habitat_tiles(grid, land_encounter_behaviors())}
    rows = []
    for y in range(grid.height):
        row = []
        for x in range(grid.width):
            if (x, y) in tiles:
                row.append("*")
            elif not gw.passable(grid, x, y):
                row.append("#")
            else:
                row.append(".")
        rows.append("".join(row))
    return "\n".join(rows)


def write_module(catalogue: dict, output: Path) -> None:
    text = json.dumps(catalogue, sort_keys=True, separators=(",", ":"))
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    chunks = [text[i:i + 96] for i in range(0, len(text), 96)]
    body = "\n".join("    " + repr(chunk) for chunk in chunks)
    output.write_text(
        '"""Shared wild spawn catalogue generated by the engine\'s tools/mp/gen_shared_spawns.py.\n\n'
        "Map numbers, habitat tiles, encounter levels, species IDs and type boosts are this\n"
        "ROM build's own values. Do not edit by hand; re-run the generator against the\n"
        'pinned engine.\n"""\nimport json\n\n'
        f"CATALOGUE_SHA256 = {digest!r}\nCATALOGUE = json.loads(\n{body}\n)\n",
        encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--ascii", metavar="MAP_FOLDER")
    args = parser.parse_args()
    if args.ascii:
        print(ascii_habitat(args.ascii))
        return 0
    catalogue = build()
    if args.output is None:
        total = sum(entry["tile_count"] for entry in catalogue["maps"].values())
        print(f"{len(catalogue['maps'])} maps, {total} habitat tiles, {len(catalogue['families'])} families")
        return 0
    write_module(catalogue, args.output)
    print(f"wrote {args.output} ({len(catalogue['maps'])} maps)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
