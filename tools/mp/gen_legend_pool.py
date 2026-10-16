#!/usr/bin/env python3
"""Generate the Legendary / Mythical wild encounter data (Update 29).

Reads the game's own species classification (the same engine data tools/mp/gen_wild_pool.py reads: isRestrictedLegendary,
isSubLegendary, isMythical, Gen 1-6 national dex <= 721; Ultra Beasts and Paradox species are not in the Gen 1-6 range) and
writes

  src/platform/mp_legend_data.h          for the game (src/platform/mp_legend.c)
  include/platform/mp_legend_tags.h      its constants
  <app>/src/emerald_multiplayer/legend_pool.py   for the multiplayer host (--app-output), the same tables

The habitat design lives here, in one place: each species lists the habitat tags it may appear in outside the Safari Zone;
each map section lists the tags of its land and its water; weather adds tags; two species are night only. Inside the Safari
Zone every species is eligible. The tables are written to both outputs, so the game and the host cannot disagree
(scripts/multiplayer/check_engine_legend_pool.py in the app compares them through the game's own dump).

    python3 tools/mp/gen_legend_pool.py [--check] [--app-output PATH]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_wild_pool as gw  # noqa: E402

ENGINE = gw.ENGINE
OUTPUT_C = ENGINE / "src/platform/mp_legend_data.h"
OUTPUT_TAGS = ENGINE / "include/platform/mp_legend_tags.h"

# Habitat tags (bits). The order is part of both outputs.
TAGS = ["FIELD", "FOREST", "MOUNTAIN", "CAVE", "VOLCANIC", "ICY", "STORM", "DESERT", "SKY", "OCEAN", "FRESH", "MYSTIC"]
BIT = {name: 1 << i for i, name in enumerate(TAGS)}

# --- Species habitats (outside the Safari Zone). A species is eligible in an area when its tags and the area's tags
# share at least one tag. Examples from the owner's brief: Kyogre ocean, Groudon land and caves, Rayquaza outdoors and the
# sky, Mewtwo caves and unusual places, Articuno cold, Zapdos storms and power, Moltres volcanic and hot.
SPECIES_HABITATS = {
    "ARTICUNO": "ICY MOUNTAIN", "ZAPDOS": "STORM SKY", "MOLTRES": "VOLCANIC DESERT",
    "MEWTWO": "CAVE MYSTIC", "MEW": "FOREST MYSTIC FRESH",
    "RAIKOU": "STORM FIELD", "ENTEI": "VOLCANIC DESERT FIELD", "SUICUNE": "FRESH OCEAN",
    "LUGIA": "OCEAN STORM", "HO_OH": "SKY MYSTIC MOUNTAIN", "CELEBI": "FOREST MYSTIC",
    "REGIROCK": "DESERT CAVE MOUNTAIN", "REGICE": "ICY CAVE OCEAN", "REGISTEEL": "CAVE MOUNTAIN MYSTIC",
    "LATIAS": "FIELD OCEAN SKY", "LATIOS": "FIELD OCEAN SKY",
    "KYOGRE": "OCEAN STORM", "GROUDON": "MOUNTAIN VOLCANIC CAVE DESERT", "RAYQUAZA": "SKY FIELD MOUNTAIN",
    "JIRACHI": "FIELD MYSTIC SKY", "DEOXYS_NORMAL": "SKY CAVE MYSTIC",
    "UXIE": "CAVE FRESH MYSTIC", "MESPRIT": "FRESH FOREST MYSTIC", "AZELF": "CAVE MOUNTAIN MYSTIC",
    "DIALGA": "MOUNTAIN CAVE MYSTIC", "PALKIA": "OCEAN SKY MYSTIC", "HEATRAN": "VOLCANIC CAVE",
    "REGIGIGAS": "ICY MOUNTAIN CAVE", "GIRATINA_ALTERED": "MYSTIC CAVE", "CRESSELIA": "FIELD FRESH MYSTIC",
    "PHIONE": "OCEAN FRESH", "MANAPHY": "OCEAN FRESH", "DARKRAI": "FOREST CAVE MYSTIC",
    "SHAYMIN_LAND": "FIELD FOREST", "ARCEUS_NORMAL": "MYSTIC SKY FIELD MOUNTAIN", "VICTINI": "FIELD MYSTIC",
    "COBALION": "MOUNTAIN CAVE", "TERRAKION": "MOUNTAIN CAVE DESERT", "VIRIZION": "FOREST FIELD",
    "TORNADUS_INCARNATE": "SKY STORM FIELD", "THUNDURUS_INCARNATE": "STORM SKY MOUNTAIN",
    "LANDORUS_INCARNATE": "FIELD DESERT MOUNTAIN", "RESHIRAM": "VOLCANIC DESERT SKY",
    "ZEKROM": "STORM CAVE MOUNTAIN", "KYUREM": "ICY MOUNTAIN CAVE", "KELDEO_ORDINARY": "FRESH FOREST",
    "MELOETTA_ARIA": "FIELD FOREST MYSTIC", "GENESECT": "CAVE MOUNTAIN MYSTIC",
    "XERNEAS_NEUTRAL": "FOREST FIELD MYSTIC", "YVELTAL": "MYSTIC MOUNTAIN CAVE SKY",
    "ZYGARDE_50": "CAVE FOREST MYSTIC MOUNTAIN", "DIANCIE": "CAVE MOUNTAIN MYSTIC",
    "HOOPA_CONFINED": "DESERT MYSTIC FIELD", "VOLCANION": "VOLCANIC MOUNTAIN CAVE",
}
NIGHT_ONLY = {"DARKRAI", "CRESSELIA"}

# --- Area tags. Land and Rock Smash: LAND_DEFAULT; underground maps: CAVE_DEFAULT (instead). Surfing and fishing:
# WATER_DEFAULT. A map section listed below ADDS its tags to the default (underground maps lose FIELD first).
LAND_DEFAULT = "FIELD"
CAVE_DEFAULT = "CAVE"
WATER_DEFAULT = "OCEAN"
MAPSEC_LAND = {
    "MAPSEC_PETALBURG_WOODS": "FOREST", "MAPSEC_ROUTE_117": "FOREST", "MAPSEC_ROUTE_119": "FOREST", "MAPSEC_ROUTE_120": "FOREST",
    "MAPSEC_ROUTE_110": "STORM", "MAPSEC_ROUTE_111": "DESERT MOUNTAIN", "MAPSEC_ROUTE_112": "MOUNTAIN",
    "MAPSEC_ROUTE_113": "VOLCANIC", "MAPSEC_ROUTE_114": "MOUNTAIN", "MAPSEC_ROUTE_115": "MOUNTAIN",
    "MAPSEC_MT_CHIMNEY": "MOUNTAIN VOLCANIC", "MAPSEC_FIERY_PATH": "VOLCANIC MOUNTAIN", "MAPSEC_FIERY_PATH2": "VOLCANIC MOUNTAIN",
    "MAPSEC_JAGGED_PASS": "MOUNTAIN VOLCANIC", "MAPSEC_JAGGED_PASS2": "MOUNTAIN VOLCANIC",
    "MAPSEC_METEOR_FALLS": "MOUNTAIN MYSTIC SKY", "MAPSEC_METEOR_FALLS2": "MOUNTAIN MYSTIC SKY",
    "MAPSEC_VICTORY_ROAD": "MOUNTAIN", "MAPSEC_SKY_PILLAR": "SKY MYSTIC MOUNTAIN", "MAPSEC_SHOAL_CAVE": "ICY",
    "MAPSEC_NEW_MAUVILLE": "STORM", "MAPSEC_MT_PYRE": "MYSTIC MOUNTAIN", "MAPSEC_ABANDONED_SHIP": "MYSTIC OCEAN",
    "MAPSEC_SEAFLOOR_CAVERN": "OCEAN", "MAPSEC_CAVE_OF_ORIGIN": "MYSTIC", "MAPSEC_SEALED_CHAMBER": "MYSTIC",
    "MAPSEC_SCORCHED_SLAB": "VOLCANIC MYSTIC", "MAPSEC_ISLAND_CAVE": "MYSTIC ICY", "MAPSEC_DESERT_RUINS": "DESERT MYSTIC",
    "MAPSEC_ANCIENT_TOMB": "MYSTIC", "MAPSEC_MIRAGE_ISLAND": "DESERT MYSTIC", "MAPSEC_SOUTHERN_ISLAND": "FOREST MYSTIC",
    "MAPSEC_VIRIDIAN_FOREST": "FOREST", "MAPSEC_MT_MOON": "MOUNTAIN MYSTIC", "MAPSEC_KANTO_VICTORY_ROAD": "MOUNTAIN",
    "MAPSEC_SEAFOAM_ISLANDS": "ICY OCEAN", "MAPSEC_POKEMON_MANSION": "MYSTIC",
}
MAPSEC_WATER = {
    "MAPSEC_ROUTE_102": "FRESH", "MAPSEC_ROUTE_111": "FRESH", "MAPSEC_ROUTE_114": "FRESH", "MAPSEC_ROUTE_117": "FRESH",
    "MAPSEC_ROUTE_118": "FRESH", "MAPSEC_ROUTE_119": "FRESH", "MAPSEC_ROUTE_120": "FRESH", "MAPSEC_PETALBURG_CITY": "FRESH",
    "MAPSEC_METEOR_FALLS": "FRESH MYSTIC", "MAPSEC_METEOR_FALLS2": "FRESH MYSTIC", "MAPSEC_SHOAL_CAVE": "ICY",
    "MAPSEC_ABANDONED_SHIP": "MYSTIC", "MAPSEC_SOOTOPOLIS_CITY": "MYSTIC", "MAPSEC_SKY_PILLAR": "SKY",
    "MAPSEC_SEAFLOOR_CAVERN": "MYSTIC",
}
# Weather adds a tag (not underground): rain and storms -> STORM, snow -> ICY, sandstorm -> DESERT, drought (sun) ->
# VOLCANIC (hot), fog -> MYSTIC. Names as in include/constants/weather.h.
WEATHER_TAGS = {
    "WEATHER_RAIN": "STORM", "WEATHER_DOWNPOUR": "STORM", "WEATHER_RAIN_THUNDERSTORM": "STORM", "WEATHER_SNOW": "ICY",
    "WEATHER_SANDSTORM": "DESERT", "WEATHER_DROUGHT": "VOLCANIC", "WEATHER_FOG": "MYSTIC", "WEATHER_FOG_HORIZONTAL": "MYSTIC",
    "WEATHER_FOG_DIAGONAL": "MYSTIC",
}
ONE_IN_REGULAR = 6000
ONE_IN_SAFARI = 4000


def mask(text: str) -> int:
    value = 0
    for name in text.split():
        value |= BIT[name]
    return value


def legend_species():
    all_ids = {n: v for n, v in gw.species_enum().items() if 1 <= v <= gw.MAX_DEX}
    species = gw.parse_species(gw.preprocess(), all_ids)
    flagged = {"isRestrictedLegendary", "isSubLegendary", "isMythical"}
    found = sorted(((all_ids[n], n[len("SPECIES_"):]) for n, i in species.items() if i["flags"] & flagged))
    names = {name for _, name in found}
    if names != set(SPECIES_HABITATS):
        raise SystemExit("habitat table and the game's Legendary/Mythical classification differ: "
                         f"missing habitats {sorted(names - set(SPECIES_HABITATS))}, unknown {sorted(set(SPECIES_HABITATS) - names)}")
    unknown = [name for name in NIGHT_ONLY if name not in names]
    if unknown:
        raise SystemExit(f"night-only species not in the pool: {unknown}")
    return found


def weather_names():
    text = (ENGINE / "include/constants/weather.h").read_text()
    names = set(__import__("re").findall(r"^\s*(WEATHER_\w+)\s*[,=]", text, __import__("re").M))
    missing = [w for w in WEATHER_TAGS if w not in names]
    if missing:
        raise SystemExit(f"unknown weather names {missing}")


def render_tags(found) -> str:
    lines = [
        "#ifndef GUARD_PLATFORM_MP_LEGEND_TAGS_H",
        "#define GUARD_PLATFORM_MP_LEGEND_TAGS_H",
        "",
        "// GENERATED by tools/mp/gen_legend_pool.py: the constants of the Legendary / Mythical encounter data.",
        "",
        f"#define MP_LEGEND_ONE_IN_REGULAR {ONE_IN_REGULAR}",
        f"#define MP_LEGEND_ONE_IN_SAFARI {ONE_IN_SAFARI}",
        f"#define MP_LEGEND_SPECIES_COUNT {len(found)}",
        "",
    ]
    for name in TAGS:
        lines.append(f"#define MP_LEGEND_{name:<9} 0x{BIT[name]:04X}")
    lines += ["#define MP_LEGEND_ALL_TAGS 0x%04X" % ((1 << len(TAGS)) - 1), "", "#define MP_LEGEND_NIGHT_ONLY 1", "",
              f"#define MP_LEGEND_LAND_DEFAULT MP_LEGEND_{LAND_DEFAULT}",
              f"#define MP_LEGEND_CAVE_DEFAULT MP_LEGEND_{CAVE_DEFAULT}",
              f"#define MP_LEGEND_WATER_DEFAULT MP_LEGEND_{WATER_DEFAULT}", "", "#endif // GUARD_PLATFORM_MP_LEGEND_TAGS_H", ""]
    return "\n".join(lines)


def render_c(found) -> str:
    lines = [
        "// GENERATED by tools/mp/gen_legend_pool.py from the engine's own species classification.",
        "// Do not edit by hand: change the design tables in the generator and re-run it.",
        f"// Legendary and Mythical species of the Gen 1-6 Pokedex (isRestrictedLegendary, isSubLegendary, isMythical): {len(found)}.",
        "",
        "static const struct MPLegendSpecies sMPLegendSpecies[MP_LEGEND_SPECIES_COUNT] =",
        "{",
    ]
    for value, name in found:
        tags = " | ".join(f"MP_LEGEND_{t}" for t in SPECIES_HABITATS[name].split())
        lines.append(f"    {{SPECIES_{name}, {tags}, {'MP_LEGEND_NIGHT_ONLY' if name in NIGHT_ONLY else '0'}}},")
    lines += ["};", ""]
    for table, data in (("Land", MAPSEC_LAND), ("Water", MAPSEC_WATER)):
        lines += [f"static const struct MPLegendMapsecTags sMPLegend{table}Tags[] =", "{"]
        for sec, tags in data.items():
            lines.append(f"    {{{sec}, " + " | ".join(f"MP_LEGEND_{t}" for t in tags.split()) + "},")
        lines += ["};", ""]
    lines += ["static const struct MPLegendWeatherTag sMPLegendWeatherTags[] =", "{"]
    for weather, tag in WEATHER_TAGS.items():
        lines.append(f"    {{{weather}, MP_LEGEND_{tag}}},")
    lines += ["};", ""]
    return "\n".join(lines)


def render_py(found) -> str:
    def pairs(data):
        return "{\n" + "".join(f'    "{k}": {mask(v)},\n' for k, v in data.items()) + "}"
    out = [
        '"""GENERATED by tools/mp/gen_legend_pool.py in the engine repository: the Legendary / Mythical wild encounter data',
        'the game uses (src/platform/mp_legend_data.h), for the multiplayer host\'s shared wild spawns. Do not edit by hand."""',
        "",
        f"ONE_IN_REGULAR = {ONE_IN_REGULAR}",
        f"ONE_IN_SAFARI = {ONE_IN_SAFARI}",
        f"TAGS = {TAGS!r}",
        "TAG_BITS = {name: 1 << index for index, name in enumerate(TAGS)}",
        f"LAND_DEFAULT = {mask(LAND_DEFAULT)}",
        f"CAVE_DEFAULT = {mask(CAVE_DEFAULT)}",
        f"WATER_DEFAULT = {mask(WATER_DEFAULT)}",
        "",
        "# (species id, name, habitat tags, night only)",
        "SPECIES = (",
    ]
    for value, name in found:
        out.append(f'    ({value}, "{name}", {mask(SPECIES_HABITATS[name])}, {name in NIGHT_ONLY}),')
    out += [")", "", "LAND_TAGS = " + pairs(MAPSEC_LAND), "", "WATER_TAGS = " + pairs(MAPSEC_WATER), "",
            "WEATHER_TAGS = {\n" + "".join(f'    "{k}": {mask(v)},\n' for k, v in WEATHER_TAGS.items()) + "}", ""]
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--app-output", type=Path)
    args = parser.parse_args()
    weather_names()
    found = legend_species()
    c_text, py_text, tags_text = render_c(found), render_py(found), render_tags(found)
    if args.check:
        ok = OUTPUT_C.exists() and OUTPUT_C.read_text() == c_text and OUTPUT_TAGS.exists() and OUTPUT_TAGS.read_text() == tags_text
        if args.app_output is not None:
            ok = ok and args.app_output.exists() and args.app_output.read_text() == py_text
        return 0 if ok else 1
    OUTPUT_C.write_text(c_text)
    OUTPUT_TAGS.write_text(tags_text)
    print(f"wrote {OUTPUT_C.relative_to(ENGINE)}: {len(found)} species")
    if args.app_output is not None:
        args.app_output.write_text(py_text)
        print(f"wrote {args.app_output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
