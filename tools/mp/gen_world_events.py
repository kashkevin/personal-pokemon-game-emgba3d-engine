#!/usr/bin/env python3
"""Generate the World Event catalogue (species, Mega Stones, locations) for the
multiplayer host from this engine's own data.

Owner rules (docs/project-brief.txt "WORLD EVENTS", docs/project-update-gen6.txt):
  * categories: Rare, Mega, Legendary, Mythical and Mega Legendary Pokemon, any
    of which may be shiny (the shiny roll is made by the host);
  * the base roster is Gen 1-6; every official Mega Evolution of a Gen 1-6
    species is eligible, including the Z-A and Mega Dimension Megas;
  * a Mega event Pokemon holds its own Mega Stone, so catching it rewards the
    appropriate stone;
  * players travel to the event themselves: every location is a real, walkable
    tile on an outdoor route or dungeon floor, checked against the map layout,
    metatile behaviours, collision and the map's own objects/warps/triggers.

Everything numeric (species, items, map groups/numbers, coordinates) is read
from this engine, so the catalogue matches the ROM built from it.

Usage:
  python3 tools/mp/gen_world_events.py --output <app>/src/emerald_multiplayer/data/world_events.json
  python3 tools/mp/gen_world_events.py --ascii Route103   # show a map and its chosen spots
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_wild_pool as pool  # noqa: E402

ENGINE = pool.ENGINE
MAX_DEX = 721
NUM_METATILES_IN_PRIMARY = 512

# ------------------------------------------------------------------ locations
# (map folder, notice name, minimum progression level, theme types, anchor map,
#  allow sand floor). The anchor keeps the spot on the part of the map players
# reach from that neighbour before they can Surf/use special items.
LOCATIONS = [
    ("Route101", "ROUTE 101", 0, ("NORMAL", "GRASS", "BUG"), None, False),
    ("Route102", "ROUTE 102", 0, ("GRASS", "BUG", "POISON"), None, False),
    ("Route103", "ROUTE 103", 0, ("WATER", "NORMAL", "FLYING"), "MAP_OLDALE_TOWN", False),
    ("Route104", "ROUTE 104", 5, ("WATER", "FLYING", "GRASS"), "MAP_PETALBURG_CITY", False),
    ("PetalburgWoods", "PETALBURG WOODS", 5, ("BUG", "GRASS", "POISON"), None, False),
    ("Route116", "ROUTE 116", 10, ("NORMAL", "ROCK", "FAIRY"), "MAP_RUSTBORO_CITY", False),
    ("Route115", "ROUTE 115", 10, ("FLYING", "NORMAL", "FIGHTING"), "MAP_RUSTBORO_CITY", True),
    ("RusturfTunnel", "RUSTURF TUNNEL", 10, ("ROCK", "NORMAL", "GROUND"), None, False),
    ("GraniteCave_1F", "GRANITE CAVE", 13, ("ROCK", "STEEL", "DARK", "FIGHTING"), None, False),
    ("Route106", "ROUTE 106", 13, ("WATER", "ICE"), "MAP_DEWFORD_TOWN", True),
    ("Route109", "ROUTE 109", 16, ("WATER", "NORMAL", "FAIRY"), "MAP_SLATEPORT_CITY", True),
    ("Route110", "ROUTE 110", 16, ("ELECTRIC", "NORMAL", "STEEL"), "MAP_SLATEPORT_CITY", False),
    ("Route117", "ROUTE 117", 19, ("GRASS", "BUG", "FAIRY"), "MAP_MAUVILLE_CITY", False),
    ("Route111", "ROUTE 111", 19, ("GROUND", "ROCK", "FIGHTING"), "MAP_MAUVILLE_CITY", False),  # desert = wall
    ("Route112", "ROUTE 112", 19, ("FIRE", "GROUND", "FIGHTING"), "MAP_ROUTE111", False),
    ("FieryPath", "FIERY PATH", 21, ("FIRE", "POISON", "GROUND"), None, False),
    ("Route113", "ROUTE 113", 23, ("FIRE", "STEEL", "GROUND"), None, False),
    ("Route114", "ROUTE 114", 23, ("GRASS", "POISON", "GROUND", "DRAGON"), "MAP_FALLARBOR_TOWN", False),
    ("Route118", "ROUTE 118", 24, ("WATER", "ELECTRIC", "FLYING"), "MAP_MAUVILLE_CITY", False),
    ("MeteorFalls_1F_1R", "METEOR FALLS", 25, ("ROCK", "DRAGON", "PSYCHIC"), None, False),
    ("JaggedPass", "JAGGED PASS", 26, ("FIRE", "GROUND", "ROCK"), None, False),
    ("MtChimney", "MT. CHIMNEY", 26, ("FIRE", "GROUND", "ROCK", "DRAGON"), None, False),
    ("Route119", "ROUTE 119", 33, ("GRASS", "BUG", "WATER", "DRAGON"), "MAP_FORTREE_CITY", False),
    ("Route120", "ROUTE 120", 33, ("GRASS", "GHOST", "DARK"), "MAP_FORTREE_CITY", False),
    ("Route121", "ROUTE 121", 35, ("GHOST", "DARK", "NORMAL"), "MAP_LILYCOVE_CITY", False),
    ("MtPyre_Exterior", "MT. PYRE", 35, ("GHOST", "PSYCHIC", "FAIRY"), None, False),
    ("Route123", "ROUTE 123", 36, ("GRASS", "PSYCHIC", "NORMAL"), "MAP_ROUTE118", False),
    ("MtPyre_Summit", "MT. PYRE SUMMIT", 37, ("GHOST", "PSYCHIC", "DRAGON", "ICE"), None, False),
    ("VictoryRoad_1F", "VICTORY ROAD", 48, ("FIGHTING", "ROCK", "STEEL", "DRAGON"), None, False),
]
SPOTS_PER_MAP = 3
MIN_SPOT_SPACING = 8

FLOOR_BEHAVIORS = {"MB_NORMAL", "MB_CAVE", "MB_SHORT_GRASS", "MB_MOUNTAIN_TOP"}
SAND_BEHAVIORS = {"MB_SAND"}


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def enum_all(path: str, enum_name: str, prefix: str) -> dict[str, int]:
    """Values of an enum whose members may use '= <number|NAME|NAME + n>'."""
    text = (ENGINE / path).read_text()
    match = re.search(r"enum[^{;]*\b" + enum_name + r"\s*\{", text)
    body = text[match.end():]
    body = body[:body.index("};")]
    values, current = {}, -1
    for m in re.finditer(r"^\s*(" + prefix + r"\w+)\s*(?:=\s*([^,\n/]+))?\s*,", body, re.M):
        name, value = m.group(1), m.group(2)
        if value is None:
            current += 1
        else:
            value = value.strip()
            if re.fullmatch(r"\d+|0x[0-9a-fA-F]+", value):
                current = int(value, 0)
            else:
                expr = re.fullmatch(r"(\w+)\s*(?:([+-])\s*(\d+))?", value)
                base = values[expr.group(1)]
                delta = int(expr.group(3) or 0) * (-1 if expr.group(2) == "-" else 1)
                current = base + delta
        values[name] = current
    return values


def metatile_behaviors() -> dict[int, str]:
    text = (ENGINE / "include/constants/metatile_behaviors.h").read_text()
    body = text[text.index("enum {") + len("enum {"):]
    body = body[:body.index("};")]
    out, current = {}, -1
    for m in re.finditer(r"^\s*(MB_\w+)\s*(?:=\s*(\w+))?\s*,", body, re.M):
        current = int(m.group(2), 0) if m.group(2) else current + 1
        out[current] = m.group(1)
    return out


def evaluate_symbols(header: str, names: list[str]) -> dict[str, int]:
    """Evaluate enum/define values with the host compiler (exact C semantics)."""
    import subprocess
    import tempfile
    lines = [f'  printf("%ld\\n", (long)({n}));' for n in names]
    src = f'#include <stdio.h>\n#include "{header}"\nint main(void){{\n' + "\n".join(lines) + "\nreturn 0;}\n"
    with tempfile.TemporaryDirectory() as tmp:
        c, exe = Path(tmp) / "eval.c", Path(tmp) / "eval"
        c.write_text(src)
        subprocess.run(["gcc", "-w", "-I", str(ENGINE / "include"), "-I", str(ENGINE / "gflib"),
                        "-o", str(exe), str(c)], check=True, capture_output=True, text=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.split()
    return dict(zip(names, (int(v) for v in out)))


def map_constants() -> dict[str, tuple[int, int]]:
    text = (ENGINE / "include/constants/map_groups.h").read_text()
    out = {}
    for m in re.finditer(r"(MAP_\w+)\s*=\s*\((\d+)\s*\|\s*\((\d+)\s*<<\s*8\)\)", text):
        out[m.group(1)] = (int(m.group(3)), int(m.group(2)))  # (group, num)
    return out


def tileset_attributes() -> dict[str, list[int]]:
    headers = (ENGINE / "src/data/tilesets/headers.h").read_text()
    metatiles = (ENGINE / "src/data/tilesets/metatiles.h").read_text()
    incbins = {m.group(1): m.group(2) for m in re.finditer(
        r"(gMetatileAttributes_\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)", metatiles)}
    out = {}
    for m in re.finditer(r"const struct Tileset (gTileset_\w+)\s*=\s*\{(.*?)\};", headers, re.S):
        attr = re.search(r"\.metatileAttributes\s*=\s*(gMetatileAttributes_\w+)", m.group(2))
        if attr and attr.group(1) in incbins:
            data = (ENGINE / incbins[attr.group(1)]).read_bytes()
            out[m.group(1)] = [int.from_bytes(data[i:i + 2], "little") for i in range(0, len(data), 2)]
    return out


class MapGrid:
    def __init__(self, folder: str, layouts: dict, attrs: dict, behaviors: dict[int, str]):
        self.folder = folder
        self.map = read_json(ENGINE / "data/maps" / folder / "map.json")
        layout = layouts[self.map["layout"]]
        self.width, self.height = layout["width"], layout["height"]
        raw = (ENGINE / layout["blockdata_filepath"]).read_bytes()
        self.blocks = [int.from_bytes(raw[i:i + 2], "little") for i in range(0, len(raw), 2)]
        primary, secondary = attrs[layout["primary_tileset"]], attrs[layout["secondary_tileset"]]
        self.behavior_names = behaviors
        self._primary, self._secondary = primary, secondary

    def block(self, x: int, y: int) -> int:
        return self.blocks[y * self.width + x]

    def behavior(self, x: int, y: int) -> str:
        metatile = self.block(x, y) & 0x3FF
        if metatile < NUM_METATILES_IN_PRIMARY:
            table, index = self._primary, metatile
        else:
            table, index = self._secondary, metatile - NUM_METATILES_IN_PRIMARY
        value = table[index] & 0xFF if index < len(table) else 0xFF
        return self.behavior_names.get(value, f"MB_{value:02X}")

    def collision(self, x: int, y: int) -> int:
        return (self.block(x, y) >> 10) & 3

    def elevation(self, x: int, y: int) -> int:
        return self.block(x, y) >> 12


def blocked_tiles(grid: MapGrid) -> set[tuple[int, int]]:
    blocked = set()
    for obj in grid.map.get("object_events", []):
        if obj.get("type") == "clone":
            continue
        rx = int(obj.get("movement_range_x", 0) or 0)
        ry = int(obj.get("movement_range_y", 0) or 0)
        for dx in range(-rx - 2, rx + 3):
            for dy in range(-ry - 2, ry + 3):
                blocked.add((obj["x"] + dx, obj["y"] + dy))
        # Trainers watch along their sight line; keep event spots out of it.
        sight = obj.get("trainer_sight_or_berry_tree_id", "0")
        if obj.get("trainer_type", "TRAINER_TYPE_NONE") != "TRAINER_TYPE_NONE" and str(sight).isdigit():
            for step in range(1, int(sight) + 1):
                for d in ((step, 0), (-step, 0), (0, step), (0, -step)):
                    blocked.add((obj["x"] + d[0], obj["y"] + d[1]))
    for warp in grid.map.get("warp_events", []):
        for dx in (-2, -1, 0, 1, 2):
            for dy in (-2, -1, 0, 1, 2):
                blocked.add((warp["x"] + dx, warp["y"] + dy))
    for event in grid.map.get("coord_events", []) + grid.map.get("bg_events", []):
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                blocked.add((event["x"] + dx, event["y"] + dy))
    return blocked


SWIM_BEHAVIORS = {
    "MB_POND_WATER", "MB_INTERIOR_DEEP_WATER", "MB_DEEP_WATER", "MB_WATERFALL", "MB_SOOTOPOLIS_DEEP_WATER",
    "MB_OCEAN_WATER", "MB_UNUSED_SOOTOPOLIS_DEEP_WATER", "MB_NO_SURFACING", "MB_UNUSED_SOOTOPOLIS_DEEP_WATER_2",
    "MB_SEAWEED", "MB_SEAWEED_NO_SURFACING", "MB_FAST_WATER", "MB_EASTWARD_CURRENT", "MB_WESTWARD_CURRENT",
    "MB_NORTHWARD_CURRENT", "MB_SOUTHWARD_CURRENT", "MB_SEMI_DEEP_WATER",
}


def in_bounds(grid: MapGrid, x: int, y: int) -> bool:
    return 0 <= x < grid.width and 0 <= y < grid.height


# Areas that need an item first are treated as walls on these maps (the
# Route 111 desert needs the Go-Goggles).
ITEM_GATED = {"Route111": {"MB_DEEP_SAND", "MB_SAND"}}
# Spots stay inside this box (x0, y0, x1, y1): south Route 111, below the desert.
SPOT_BOX = {"Route111": (0, 100, 40, 140)}


def passable(grid: MapGrid, x: int, y: int) -> bool:
    """A tile a player can stand on without Surf (grass, ledges and bridges included)."""
    if not in_bounds(grid, x, y) or grid.collision(x, y) != 0:
        return False
    behavior = grid.behavior(x, y)
    return behavior not in SWIM_BEHAVIORS and behavior not in ITEM_GATED.get(grid.folder, ())


def can_step(grid: MapGrid, ax: int, ay: int, bx: int, by: int) -> bool:
    # Emerald's elevation rule: 0 and 15 connect to everything.
    ea, eb = grid.elevation(ax, ay), grid.elevation(bx, by)
    return ea == eb or ea in (0, 15) or eb in (0, 15)


def walkable(grid: MapGrid, x: int, y: int, allow_sand: bool) -> bool:
    """A plain floor tile good for an event Pokemon to stand on."""
    if not passable(grid, x, y) or grid.elevation(x, y) in (0, 15):
        return False
    behavior = grid.behavior(x, y)
    return behavior in FLOOR_BEHAVIORS or (allow_sand and behavior in SAND_BEHAVIORS)


def components(grid: MapGrid, allow_sand: bool) -> list[set[tuple[int, int]]]:
    seen, out = set(), []
    for y in range(grid.height):
        for x in range(grid.width):
            if (x, y) in seen or not passable(grid, x, y):
                continue
            comp, queue = set(), deque([(x, y)])
            seen.add((x, y))
            while queue:
                cx, cy = queue.popleft()
                comp.add((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if (nx, ny) not in seen and passable(grid, nx, ny) and can_step(grid, cx, cy, nx, ny):
                        seen.add((nx, ny))
                        queue.append((nx, ny))
            out.append(comp)
    return out


def anchor_edge(grid: MapGrid, anchor: str) -> set[tuple[int, int]]:
    edge = set()
    for connection in grid.map.get("connections") or []:
        if connection["map"] != anchor:
            continue
        direction = connection["direction"]
        for i in range(max(grid.width, grid.height)):
            for depth in range(3):
                if direction == "up":
                    edge.add((i, depth))
                elif direction == "down":
                    edge.add((i, grid.height - 1 - depth))
                elif direction == "left":
                    edge.add((depth, i))
                elif direction == "right":
                    edge.add((grid.width - 1 - depth, i))
    if not edge:
        raise SystemExit(f"{grid.folder}: no connection to anchor {anchor}")
    return edge


def choose_spots(grid: MapGrid, anchor: str | None, allow_sand: bool) -> list[tuple[int, int]]:
    comps = components(grid, allow_sand)
    if anchor is not None:
        edge = anchor_edge(grid, anchor)
        comps = [c for c in comps if c & edge]
    if not comps:
        raise SystemExit(f"{grid.folder}: no walkable area")
    main = max(comps, key=len)
    blocked = blocked_tiles(grid)

    def open_tile(x, y, radius):
        return all((x + dx, y + dy) in main and walkable(grid, x + dx, y + dy, allow_sand)
                   and grid.elevation(x + dx, y + dy) == grid.elevation(x, y)
                   for dx in range(-radius, radius + 1) for dy in range(-radius, radius + 1))

    x0, y0, x1, y1 = SPOT_BOX.get(grid.folder, (0, 0, grid.width, grid.height))
    candidates = [(x, y) for (x, y) in main
                  if (x, y) not in blocked and 2 <= x < grid.width - 2 and 2 <= y < grid.height - 2
                  and x0 <= x < x1 and y0 <= y < y1 and open_tile(x, y, 1)]
    if not candidates:
        raise SystemExit(f"{grid.folder}: no open event spot")
    cx = sum(x for x, _ in main) / len(main)
    cy = sum(y for _, y in main) / len(main)
    spots = [min(candidates, key=lambda p: ((p[0] - cx) ** 2 + (p[1] - cy) ** 2, p))]
    while len(spots) < SPOTS_PER_MAP:
        far = max(candidates, key=lambda p: (min(abs(p[0] - s[0]) + abs(p[1] - s[1]) for s in spots), p))
        if min(abs(far[0] - s[0]) + abs(far[1] - s[1]) for s in spots) < MIN_SPOT_SPACING:
            break
        spots.append(far)
    return spots


def ascii_map(grid: MapGrid, spots, anchor, allow_sand) -> str:
    comps = components(grid, allow_sand)
    if anchor is not None:
        edge = anchor_edge(grid, anchor)
        comps = [c for c in comps if c & edge]
    main = max(comps, key=len) if comps else set()
    blocked = blocked_tiles(grid)
    lines = []
    for y in range(grid.height):
        row = []
        for x in range(grid.width):
            if (x, y) in spots:
                row.append("@")
            elif (x, y) in main and walkable(grid, x, y, allow_sand):
                row.append("+" if (x, y) in blocked else ".")
            elif (x, y) in main:
                b = grid.behavior(x, y)
                row.append('"' if "GRASS" in b else ":")
            elif passable(grid, x, y):
                row.append(",")
            else:
                b = grid.behavior(x, y)
                row.append("~" if "WATER" in b or "OCEAN" in b or "POND" in b else
                           '"' if "GRASS" in b else "#")
        lines.append("".join(row))
    return "\n".join(lines)


# ------------------------------------------------------------------ species

def species_names(pre: str) -> dict[str, str]:
    start = pre.index("gSpeciesInfo[]")
    out = {}
    for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", pre[start:]):
        brace = start + m.end() - 1
        body = pre[brace + 1:pool.balanced(pre, brace) - 1]
        name = re.search(r"\.speciesName\s*=\s*_?\(\s*\"((?:[^\"\\]|\\.)*)\"\s*\)", body)
        if name and m.group(1) not in out:
            out[m.group(1)] = name.group(1)
    return out


def ascii_name(text: str) -> str:
    text = text.replace("♀", " F").replace("♂", " M").replace("é", "E").replace("É", "E")
    text = "".join(c for c in text.upper() if 32 <= ord(c) < 127)
    return re.sub(r"\s+", " ", text).strip()


def overworld_present(pre: str, name: str) -> bool:
    start = pre.index("gSpeciesInfo[]")
    m = re.search(r"\[" + name + r"\]\s*=\s*\{", pre[start:])
    brace = start + m.end() - 1
    body = pre[brace + 1:pool.balanced(pre, brace) - 1]
    return ".overworldData" in body


def form_changes(pre: str) -> list[tuple[str, str, str]]:
    out = []
    for m in re.finditer(r"\{\s*(FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM|FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE)"
                         r"\s*,\s*(SPECIES_\w+)\s*,\s*(\w+)", pre):
        out.append(m.groups())
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--ascii", help="print one location map with its spots")
    args = parser.parse_args()

    behaviors = metatile_behaviors()
    layouts = {l["id"]: l for l in read_json(ENGINE / "data/layouts/layouts.json")["layouts"] if "id" in l}
    attrs = tileset_attributes()
    maps = map_constants()

    if args.ascii:
        entry = next(l for l in LOCATIONS if l[0] == args.ascii)
        grid = MapGrid(entry[0], layouts, attrs, behaviors)
        spots = choose_spots(grid, entry[4], entry[5])
        print(ascii_map(grid, spots, entry[4], entry[5]))
        print(spots)
        return 0

    locations = []
    for folder, label, min_level, themes, anchor, sand in LOCATIONS:
        grid = MapGrid(folder, layouts, attrs, behaviors)
        group, num = maps[grid.map["id"]]
        for x, y in choose_spots(grid, anchor, sand):
            locations.append({"name": label, "map": grid.map["id"], "map_group": group, "map_number": num,
                              "x": x, "y": y, "min_level": min_level,
                              "themes": sorted(t.lower() for t in themes)})

    # Species data (the same parsing the wild pool generator uses).
    all_ids = enum_all("include/constants/species.h", "Species", "SPECIES_")
    pre = pool.preprocess()
    changes = [(method, target, param) for method, target, param in form_changes(pre)]
    mega_targets = {target for _, target, _ in changes}
    wanted = {n: v for n, v in all_ids.items() if 1 <= v <= MAX_DEX or n in mega_targets}
    species = pool.parse_species(pre, wanted)
    pool.species_ids = {n: wanted[n] for n in species}
    pool.evaluate_stats(species)
    names = species_names(pre)
    item_names = sorted({param for method, _, param in changes if method.endswith("_ITEM")})
    items = evaluate_symbols("constants/items.h", item_names + ["ITEMS_COUNT"])
    natdex = pool.enum_values("include/constants/pokedex.h", "NationalDexOrder")
    natdex_of = {}
    for name in species:
        start = pre.index("gSpeciesInfo[]")
        m = re.search(r"\[" + name + r"\]\s*=\s*\{", pre[start:])
        brace = start + m.end() - 1
        body = pre[brace + 1:pool.balanced(pre, brace) - 1]
        dex = re.search(r"\.natDexNum\s*=\s*(NATIONAL_DEX_\w+)", body)
        natdex_of[name] = natdex.get(dex.group(1), 0) if dex else 0

    def types(name):
        return sorted({t[len("TYPE_"):].lower() for t in species[name]["types"]})

    legendary, mythical = [], []
    for name, info in sorted(species.items(), key=lambda kv: wanted[kv[0]]):
        if not 1 <= wanted[name] <= MAX_DEX:
            continue
        entry = {"species": wanted[name], "name": ascii_name(names[name]), "types": types(name),
                 "symbol": name}
        if "isMythical" in info["flags"]:
            mythical.append(entry)
        elif info["flags"] & {"isRestrictedLegendary", "isSubLegendary"}:
            legendary.append(entry)

    mega, mega_legendary = [], []
    seen = set()
    for method, target, param in changes:
        if target in seen or target not in species:
            continue
        seen.add(target)
        if not 1 <= natdex_of.get(target, 0) <= MAX_DEX:
            continue  # a Gen 7+ base species (Drampa, Falinks, Tatsugiri, ...)
        flags = species[target]["flags"]
        base_name = ascii_name(names[target])
        suffix = re.search(r"_MEGA(?:_([XYZ]))?$", target)
        label = "MEGA " + base_name + (f" {suffix.group(1)}" if suffix and suffix.group(1) else "")
        held = items[param] if method.endswith("_ITEM") else 0
        entry = {"species": wanted[target], "name": label, "types": types(target), "mega": True,
                 "held_item": held, "symbol": target,
                 "stone": param if method.endswith("_ITEM") else None,
                 "overworld_sprite": overworld_present(pre, target)}
        if flags & {"isRestrictedLegendary", "isSubLegendary", "isMythical"}:
            mega_legendary.append(entry)
        else:
            mega.append(entry)

    rare = []
    for base, members in pool.build_families(species):
        if not 1 <= wanted[base] <= MAX_DEX:
            continue
        result = pool.classify(base, members, species)
        if result is None or result[0] not in (pool.TIER_RARE, pool.TIER_VERY_RARE):
            continue
        stages, listed = [], set()
        for n, lvl, _ in sorted(members, key=lambda m: (m[1], m[2], wanted[m[0]])):
            if n in listed or not 1 <= wanted[n] <= MAX_DEX:
                continue  # a species reached by two evolution methods keeps its lowest level
            listed.add(n)
            stages.append({"species": wanted[n], "name": ascii_name(names[n]), "min_level": min(lvl, 100)})
        rare.append({"stages": stages, "types": types(base), "symbol": base,
                     "tier": "very_rare" if result[0] == pool.TIER_VERY_RARE else "rare"})

    catalogue = {
        "format": 1,
        "generated_by": "tools/mp/gen_world_events.py",
        "engine_counts": {"species": len(all_ids), "items": len(items)},
        "rare": rare, "legendary": legendary, "mythical": mythical,
        "mega": mega, "mega_legendary": mega_legendary, "locations": locations,
    }
    text = json.dumps(catalogue, indent=1, sort_keys=True) + "\n"
    if args.output and args.output.suffix == ".py":
        import hashlib
        compact = json.dumps(catalogue, separators=(",", ":"), sort_keys=True)
        chunks = [compact[i:i + 96] for i in range(0, len(compact), 96)]
        body = "\n".join("    " + repr(chunk) for chunk in chunks)
        module = (
            '"""World Event catalogue generated by the engine\'s tools/mp/gen_world_events.py.\n\n'
            'Species, items, map numbers and coordinates are this ROM build\'s own values.\n'
            'Do not edit by hand; re-run the generator against the pinned engine.\n"""\n'
            "import json\n\n"
            f"CATALOGUE_SHA256 = {hashlib.sha256(compact.encode()).hexdigest()!r}\n"
            "CATALOGUE = json.loads(\n" + body + "\n)\n")
        args.output.write_text(module, encoding="utf-8")
    elif args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(f"rare families {len(rare)}, legendary {len(legendary)}, mythical {len(mythical)}, "
          f"mega {len(mega)}, mega legendary {len(mega_legendary)}, locations {len(locations)}"
          f" on {len(LOCATIONS)} maps")
    missing = [e["name"] for e in mega + mega_legendary if not e["overworld_sprite"]]
    if missing:
        print("Megas drawn with their base form's overworld sprite:", ", ".join(missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
