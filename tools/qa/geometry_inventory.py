#!/usr/bin/env python3
"""Every map the Emerald game shows, with the facts its 3D shape is built from (step 16: the world geometry overhaul).

From the engine checkout: the maps of tools/qa/inventory.py that belong to the Emerald game (reachable from Littleroot
Town, or entered by the C code: secret bases, Battle Pyramid floors, the high-tide Shoal Cave, the truck), each with
its layout's size and tilesets, its kind (outdoor, cave, underwater, indoor, secret base) and what its block data
holds: the height levels its cells stand on, the steps between neighbouring levels (where a cliff, a wall or a slope
has to stand in 3D), its water, waterfalls, ledges, stairs, bridges and doors. FireRed/LeafGreen and unused maps are
listed as out of scope, so the manifest still accounts for every map of data/maps/map_groups.json.

    tools/qa/geometry_inventory.py --out build/qa/geometry_inventory.json [--markdown build/qa/geometry_inventory.md]

The block data: a u16 per cell, metatile id bits 0-9, collision bits 10-11, elevation bits 12-15 (0 a transition
cell, 1 the surfing level, 3-14 land levels, 15 a bridge usable from any level); a metatile's behaviour is the low byte
of its tileset's metatile_attributes.bin entry (ids from 512 are the secondary tileset's).
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory as game_inventory  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NUM_METATILES_IN_PRIMARY = 512
KIND_OF_TYPE = {
    "MAP_TYPE_TOWN": "outdoor", "MAP_TYPE_CITY": "outdoor", "MAP_TYPE_ROUTE": "outdoor",
    "MAP_TYPE_OCEAN_ROUTE": "outdoor", "MAP_TYPE_UNDERGROUND": "cave", "MAP_TYPE_UNDERWATER": "underwater",
    "MAP_TYPE_INDOOR": "indoor", "MAP_TYPE_SECRET_BASE": "secret_base",
}
# behaviour families the 3D shape has to show (names of include/constants/metatile_behaviors.h)
FAMILIES = [
    ("water", re.compile(r"^MB_(POND_WATER|INTERIOR_DEEP_WATER|DEEP_WATER|SOOTOPOLIS_DEEP_WATER|OCEAN_WATER|"
                         r"SHALLOW_WATER|NO_SURFACING|SEAWEED|SEAWEED_NO_SURFACING|FAST_WATER|CYCLING_ROAD_WATER|"
                         r"(EAST|WEST|NORTH|SOUTH)WARD_CURRENT|UNUSED_SOOTOPOLIS_DEEP_WATER(_2)?)$")),
    ("waterfall", re.compile(r"^MB_WATERFALL$")),
    ("puddle", re.compile(r"^MB_PUDDLE$")),
    ("ledge", re.compile(r"^MB_JUMP_")),
    ("stairs", re.compile(r"^MB_(SIDEWAYS_STAIRS_|ROCK_STAIRS|STAIRS_OUTSIDE_ABANDONED_SHIP|LADDER|"
                          r"(UP|DOWN)_(LEFT|RIGHT)_STAIR_WARP)")),
    ("bridge", re.compile(r"^MB_(BRIDGE_|FORTREE_BRIDGE|BIKE_BRIDGE_OVER_BARRIER|UNUSED_BRIDGE)")),
    ("door", re.compile(r"^MB_(ANIMATED_DOOR|NON_ANIMATED_DOOR|WATER_DOOR|PETALBURG_GYM_DOOR|"
                        r"CLOSED_SOOTOPOLIS_DOOR|SKY_PILLAR_CLOSED_DOOR|TRICK_HOUSE_PUZZLE_DOOR)$")),
    ("arrow_warp", re.compile(r"^MB_((EAST|WEST|NORTH|SOUTH)_ARROW_WARP|WATER_SOUTH_ARROW_WARP|DEEP_SOUTH_WARP)$")),
    ("grass", re.compile(r"^MB_(TALL_GRASS|LONG_GRASS|LONG_GRASS_SOUTH_EDGE|ASHGRASS|SHORT_GRASS)$")),
    ("sand", re.compile(r"^MB_(SAND|DEEP_SAND)$")),
    ("ice", re.compile(r"^MB_(ICE|THIN_FLOOR|CRACKED_FLOOR|CRACKED_ICE|CRACKED_FLOOR_HOLE)$")),
    ("counter", re.compile(r"^MB_COUNTER$")),
    ("shelf", re.compile(r"^MB_(BOOKSHELF|POKEMON_CENTER_BOOKSHELF|SHOP_SHELF|PICTURE_BOOK_SHELF|CABINET)$")),
]


def behaviour_names(root):
    """The behaviours in their enum order (include/constants/metatile_behaviors.h)."""
    text = open(os.path.join(root, "include", "constants", "metatile_behaviors.h")).read()
    body = text[text.index("enum {") + 6:text.index("};")]
    names, value = {}, 0
    for line in body.split("\n"):
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            line, number = (part.strip() for part in line.split("="))
            value = int(number, 0)
        names[value] = line
        value += 1
    return names


def tileset_roots(root):
    """{tileset symbol: its metatile_attributes.bin}: gTileset_X's metatileAttributes symbol (src/data/tilesets/
    headers.h) and that symbol's INCBIN (metatiles.h)."""
    out = {}
    headers = open(os.path.join(root, "src", "data", "tilesets", "headers.h")).read()
    attrs = open(os.path.join(root, "src", "data", "tilesets", "metatiles.h")).read()
    files = dict(re.findall(r"const u16 (gMetatileAttributes_\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)", attrs))
    for symbol, block in re.findall(r"const struct Tileset (gTileset_\w+)\s*=\s*\{(.*?)\};", headers, flags=re.S):
        match = re.search(r"\.metatileAttributes\s*=\s*(gMetatileAttributes_\w+)", block)
        if match and match.group(1) in files:
            out[symbol] = os.path.join(root, files[match.group(1)])
    return out


def read_u16(path):
    data = open(path, "rb").read()
    return list(struct.unpack(f"<{len(data) // 2}H", data))


def layout_facts(root, layout, attributes, names):
    """What a layout's cells hold: levels, steps between levels, behaviour families, blocked cells."""
    width, height = layout["width"], layout["height"]
    blocks = read_u16(os.path.join(root, layout["blockdata_filepath"]))[:width * height]
    primary = attributes.get(layout["primary_tileset"], [])
    secondary = attributes.get(layout["secondary_tileset"], [])

    def behaviour(block):
        metatile = block & 0x03FF
        table, index = (primary, metatile) if metatile < NUM_METATILES_IN_PRIMARY else \
            (secondary, metatile - NUM_METATILES_IN_PRIMARY)
        return names.get(table[index] & 0xFF, "?") if index < len(table) else "?"

    levels = collections.Counter(block >> 12 for block in blocks)
    blocked = sum(1 for block in blocks if block & 0x0C00)
    families = collections.Counter()
    unknown = 0
    kinds = []
    for block in blocks:
        name = behaviour(block)
        unknown += name == "?"
        family = next((key for key, pattern in FAMILIES if pattern.match(name)), None)
        kinds.append(family)
        if family:
            families[family] += 1
    # steps: neighbouring cells on two different land levels (3-14); where a cliff face, wall or slope must stand
    steps, step_sizes, walkable_steps = 0, collections.Counter(), 0
    for y in range(height):
        for x in range(width):
            a = blocks[y * width + x]
            for dx, dy in ((1, 0), (0, 1)):
                nx, ny = x + dx, y + dy
                if nx >= width or ny >= height:
                    continue
                b = blocks[ny * width + nx]
                la, lb = a >> 12, b >> 12
                if 3 <= la <= 14 and 3 <= lb <= 14 and la != lb:
                    steps += 1
                    step_sizes[abs(la - lb)] += 1
                    if not (a & 0x0C00) and not (b & 0x0C00):
                        walkable_steps += 1     # both passable: the game walks it only through a transition
    land = sorted(level for level in levels if 3 <= level <= 14)
    return dict(width=width, height=height, cells=width * height, blocked=blocked,
                levels={str(level): count for level, count in sorted(levels.items())},
                land_levels=land, level_span=(land[-1] - land[0]) if land else 0,
                steps=steps, step_sizes={str(k): v for k, v in sorted(step_sizes.items())},
                walkable_steps=walkable_steps, families=dict(families), unknown_behaviours=unknown)


def geometry_inventory(root):
    inv = game_inventory.inventory(root)
    names = behaviour_names(root)
    attributes = {symbol: read_u16(path) for symbol, path in tileset_roots(root).items() if os.path.exists(path)}
    layouts = {l["id"]: l for l in json.load(open(os.path.join(root, "data", "layouts", "layouts.json")))["layouts"]}
    facts_of_layout, maps = {}, []
    for m in inv["maps"]:
        data = json.load(open(os.path.join(root, "data", "maps", m["name"], "map.json")))
        layout = layouts.get(data.get("layout"))
        entry = m.get("entry", "")
        in_game = (m["reachable"] and not m["frlg"]) or entry.split(":")[0] in (
            "secret base", "battle pyramid", "high tide", "truck")
        row = dict(key=m["key"], id=m["id"], name=m["name"], map_type=m["map_type"],
                   kind=KIND_OF_TYPE.get(m["map_type"], "other"), in_game=in_game,
                   entry=entry or ("scripted: " + m["scripted_entry"] if m.get("scripted_entry") else "walked"),
                   layout=data.get("layout"), connections=[c["map"] for c in data.get("connections") or []],
                   warps=len(m["warps"]), objects=len(m["objects"]))
        if not in_game:
            row["out_of_scope"] = "FireRed/LeafGreen" if m["frlg"] else entry or "not entered"
        if layout is not None:
            row.update(primary_tileset=layout["primary_tileset"], secondary_tileset=layout["secondary_tileset"])
            if in_game:
                if layout["id"] not in facts_of_layout:
                    facts_of_layout[layout["id"]] = layout_facts(root, layout, attributes, names)
                row.update(facts_of_layout[layout["id"]])
        maps.append(row)
    return maps


def summary(maps):
    game = [m for m in maps if m["in_game"]]
    by_kind = collections.Counter(m["kind"] for m in game)
    return dict(maps=len(maps), in_game=len(game), out_of_scope=len(maps) - len(game),
                out_of_scope_why=dict(collections.Counter(m["out_of_scope"].split(":")[0]
                                                          for m in maps if not m["in_game"])),
                by_kind=dict(by_kind),
                layouts=len({m["layout"] for m in game}),
                cells=sum(m.get("cells", 0) for m in game),
                with_steps=sum(1 for m in game if m.get("steps")),
                with_water=sum(1 for m in game if m.get("families", {}).get("water")),
                with_waterfall=sum(1 for m in game if m.get("families", {}).get("waterfall")),
                with_ledges=sum(1 for m in game if m.get("families", {}).get("ledge")),
                with_stairs=sum(1 for m in game if m.get("families", {}).get("stairs")),
                with_bridges=sum(1 for m in game if m.get("families", {}).get("bridge")),
                multi_level=sum(1 for m in game if len(m.get("land_levels", [])) > 1))


def markdown(maps, summ):
    lines = ["# Geometry inventory: every map the Emerald game shows", "",
             "Generated by `tools/qa/geometry_inventory.py` from the game's own data. Levels are the block data's "
             "land levels (3-14); steps are neighbouring cells on two levels (where a cliff, wall or slope stands in "
             "3D); families count the cells of each behaviour family.", "",
             "```", json.dumps(summ, indent=1), "```", ""]
    for kind in ("outdoor", "cave", "underwater", "indoor", "secret_base", "other"):
        rows = [m for m in maps if m["in_game"] and m["kind"] == kind]
        if not rows:
            continue
        lines += [f"## {kind} ({len(rows)})", "",
                  "| Map | Size | Levels | Steps (walkable) | Water | Falls | Ledges | Stairs | Bridges | Doors "
                  "| Entry |",
                  "|---|---|---|---|---|---|---|---|---|---|---|"]
        for m in rows:
            f = m.get("families", {})
            lines.append(f"| {m['name']} | {m.get('width', '?')}x{m.get('height', '?')} | "
                         f"{','.join(map(str, m.get('land_levels', [])))} | {m.get('steps', 0)} "
                         f"({m.get('walkable_steps', 0)}) | {f.get('water', 0)} | {f.get('waterfall', 0)} | "
                         f"{f.get('ledge', 0)} | {f.get('stairs', 0)} | {f.get('bridge', 0)} | {f.get('door', 0)} | "
                         f"{m['entry'].split(':')[0]} |")
        lines.append("")
    rest = collections.Counter(m["out_of_scope"].split(":")[0] for m in maps if not m["in_game"])
    lines += ["## Out of scope", ""] + [f"* {why}: {count} maps" for why, count in sorted(rest.items())] + [""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT)
    parser.add_argument("--out", required=True)
    parser.add_argument("--markdown")
    args = parser.parse_args()
    maps = geometry_inventory(args.root)
    summ = summary(maps)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as out:
        json.dump(dict(summary=summ, maps=maps), out, indent=1)
    if args.markdown:
        with open(args.markdown, "w") as out:
            out.write(markdown(maps, summ))
    print(json.dumps(summ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
