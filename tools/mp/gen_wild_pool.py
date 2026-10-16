#!/usr/bin/env python3
"""Generate src/data/mp_wild_pool.h for the randomized Gen 1-6 wild encounter pool.

Project rules (docs/master-spec.md, "Wild encounters and shinies"):
  * ordinary wild encounters pick from eligible Gen 1-6 evolutionary families;
  * Legendary/Mythical Pokemon never appear as ordinary wild encounters;
  * early encounters are mostly basic/first-stage Pokemon; evolved stages only
    appear once the map's own (vanilla) level reaches a sensible threshold;
  * families have rarity weighting (common/uncommon/rare/very rare);
  * starters may appear; time of day can weight (never forbid) species.

The species data is read from the engine itself: src/pokemon.c is run through
the normal cross preprocessor so every config switch (Gen6 profile) is honoured.
Numeric stat expressions are evaluated by the host C compiler, so no C
expression is re-implemented here.

Usage: python3 tools/mp/gen_wild_pool.py [--check]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
OUTPUT = ENGINE / "src/data/mp_wild_pool.h"
OUTPUT_SAFARI = ENGINE / "src/platform/mp_wild_safari.h"   # native PC game only (the GBA ROM keeps its pinned pool data)
MAX_DEX = 721

CPPFLAGS = ["-iquote", "include", "-iquote", "gflib", "-iquote", ".", "-Wno-trigraphs",
            "-DMODERN=1", "-DTESTING=0", "-DEMERALD", "-std=gnu17",
            "-DMP_LOCAL_RESET_DEVELOPMENT=0", "-DMP_LOCAL_RESET_STATIC=1",
            "-DMP_LOCAL_RESET_NEW_MAUVILLE_PILOT=0"]

# --- Hand-curated design lists (balance values approved as "recommended defaults") ---
TIER_COMMON, TIER_UNCOMMON, TIER_RARE, TIER_VERY_RARE = range(4)
# Per-species weights: a single common family is 10x as likely as a single very rare one.
TIER_WEIGHT = {TIER_COMMON: 10, TIER_UNCOMMON: 5, TIER_RARE: 2, TIER_VERY_RARE: 1}
TIER_NAME = {TIER_COMMON: "COMMON", TIER_UNCOMMON: "UNCOMMON", TIER_RARE: "RARE", TIER_VERY_RARE: "VERY_RARE"}

PSEUDO_AND_FOSSIL = {  # very rare
    "DRATINI", "LARVITAR", "BAGON", "BELDUM", "GIBLE", "DEINO", "GOOMY",
    "OMANYTE", "KABUTO", "AERODACTYL", "LILEEP", "ANORITH", "CRANIDOS", "SHIELDON",
    "TIRTOUGA", "ARCHEN", "TYRUNT", "AMAURA",
}
STARTERS = {
    "BULBASAUR", "CHARMANDER", "SQUIRTLE", "CHIKORITA", "CYNDAQUIL", "TOTODILE",
    "TREECKO", "TORCHIC", "MUDKIP", "TURTWIG", "CHIMCHAR", "PIPLUP",
    "SNIVY", "TEPIG", "OSHAWOTT", "CHESPIN", "FENNEKIN", "FROAKIE",
}
FORCE_RARE = STARTERS | {
    "EEVEE", "RIOLU", "AXEW", "HONEDGE", "LAPRAS", "MUNCHLAX", "SNORLAX", "ABSOL",
    "SCYTHER", "PINSIR", "HERACROSS", "HAPPINY", "CHANSEY", "DITTO", "PORYGON",
    "FEEBAS", "TAUROS", "KANGASKHAN", "SPIRITOMB", "ROTOM", "LITWICK", "ZORUA",
    "SKARMORY", "DRUDDIGON", "HAWLUCHA", "NOIBAT", "TYROGUE", "UNOWN", "SABLEYE",
    "MAWILE", "TROPIUS", "RELICANTH", "CASTFORM", "KECLEON", "CHATOT",
}
FORCE_COMMON = {
    "MAGIKARP", "ZUBAT", "RATTATA", "PIDGEY", "SPEAROW", "CATERPIE", "WEEDLE",
    "ZIGZAGOON", "POOCHYENA", "WURMPLE", "TAILLOW", "WINGULL", "BIDOOF", "STARLY",
    "KRICKETOT", "PATRAT", "LILLIPUP", "PURRLOIN", "PIDOVE", "SEWADDLE", "VENIPEDE",
    "FLETCHLING", "BUNNELBY", "SCATTERBUG", "TENTACOOL", "GOLDEEN", "GEODUDE",
    "ODDISH", "BELLSPROUT", "HOOTHOOT", "SENTRET", "LEDYBA", "SPINARAK", "NINCADA",
    "WHISMUR", "MAKUHITA", "SHROOMISH", "SEEDOT", "LOTAD", "SURSKIT", "BURMY",
    "COMBEE", "WOOBAT", "ROGGENROLA", "TYMPOLE", "MARILL", "AZURILL", "PSYDUCK",
}
# Families that may never be generated as ordinary wild Pokemon besides the
# Legendary/Mythical/Ultra Beast/Paradox flags (special mechanics or event-only).
EXCLUDE_FAMILY = {"SHEDINJA"}
# Babies whose own evolution is a significant step (Riolu -> Lucario) keep the
# baby as the only first-stage find.
BABY_KEEPS_STAGE = {"RIOLU", "HAPPINY", "MUNCHLAX", "TYROGUE", "MANTYKE", "BONSLY",
                    "MIME_JR", "BUDEW", "CHINGLING"}
BABIES = {
    "PICHU", "CLEFFA", "IGGLYBUFF", "TOGEPI", "TYROGUE", "SMOOCHUM", "ELEKID", "MAGBY",
    "AZURILL", "WYNAUT", "BUDEW", "CHINGLING", "BONSLY", "MIME_JR", "HAPPINY",
    "MUNCHLAX", "RIOLU", "MANTYKE",
}
NOCTURNAL = {
    "ZUBAT", "ODDISH", "VENONAT", "GASTLY", "HOOTHOOT", "SPINARAK", "MURKROW",
    "MISDREAVUS", "SNEASEL", "HOUNDOUR", "POOCHYENA", "SABLEYE", "DUSKULL", "SHUPPET",
    "VOLBEAT", "ILLUMISE", "STUNKY", "GLAMEOW", "DRIFLOON", "PURRLOIN", "WOOBAT",
    "LITWICK", "YAMASK", "SANDILE", "ZORUA", "PAWNIARD", "NOIBAT", "PHANTUMP",
    "PUMPKABOO", "GLIGAR", "CROAGUNK", "KRICKETOT", "SEVIPER", "MAWILE", "SPIRITOMB",
    "CHINCHOU", "SHELLDER", "STARYU", "LUVDISC", "ABSOL", "DEINO", "INKAY",
}
DIURNAL = {
    "PIDGEY", "SPEAROW", "CATERPIE", "WEEDLE", "LEDYBA", "SUNKERN", "HOPPIP",
    "YANMA", "TAILLOW", "WINGULL", "SWABLU", "STARLY", "COMBEE", "CHERUBI",
    "PACHIRISU", "BUNEARY", "PIDOVE", "SEWADDLE", "COTTONEE", "PETILIL", "DEERLING",
    "FLETCHLING", "SCATTERBUG", "FLABEBE", "SKIDDO", "BELLSPROUT", "PARAS", "BUTTERFREE",
    "SURSKIT", "BEAUTIFLY", "PLUSLE", "MINUN", "SEEDOT", "ROSELIA", "BUDEW", "TROPIUS",
}
# Pokemon that are clearly aquatic (cannot walk on land) even if their egg groups
# alone do not say so. Land spawns exclude these families.
AQUATIC_EXTRA = {
    "MAGIKARP", "FEEBAS", "GOLDEEN", "HORSEA", "TENTACOOL", "REMORAID", "MANTYKE",
    "CHINCHOU", "QWILFISH", "STARYU", "SHELLDER", "LUVDISC", "CORSOLA", "WAILMER",
    "CARVANHA", "BARBOACH", "FINNEON", "ALOMOMOLA", "FRILLISH", "BASCULIN",
    "RELICANTH", "CLAMPERL", "SKRELP", "CLAUNCHER", "LAPRAS", "SEEL", "DEWGONG",
    "PSYDUCK", "POLIWAG", "SLOWPOKE", "KRABBY", "TYMPOLE", "DUCKLETT", "WOOPER",
    "MARILL", "AZURILL", "LOTAD", "WINGULL", "SPHEAL", "BUIZEL", "SHELLOS", "PIPLUP",
    "PANPOUR", "TIRTOUGA", "OSHAWOTT", "TOTODILE", "MUDKIP", "SQUIRTLE", "FROAKIE",
    "SURSKIT", "CORPHISH", "BINACLE", "OMANYTE", "KABUTO", "SLOWPOKE",
}
LAND_ALLOWED_DESPITE_WATER = {  # amphibians etc. that also walk on land
    "PSYDUCK", "POLIWAG", "SLOWPOKE", "KRABBY", "TYMPOLE", "WOOPER", "MARILL", "AZURILL",
    "LOTAD", "SPHEAL", "BUIZEL", "SHELLOS", "PIPLUP", "PANPOUR", "TIRTOUGA", "OSHAWOTT",
    "TOTODILE", "MUDKIP", "SQUIRTLE", "FROAKIE", "SURSKIT", "CORPHISH", "BINACLE",
    "OMANYTE", "KABUTO", "SEEL", "DUCKLETT", "WINGULL", "PALPITOAD",
}
FISH = {
    "MAGIKARP", "FEEBAS", "GOLDEEN", "HORSEA", "TENTACOOL", "REMORAID", "CHINCHOU",
    "QWILFISH", "LUVDISC", "CORSOLA", "WAILMER", "CARVANHA", "BARBOACH", "FINNEON",
    "ALOMOMOLA", "BASCULIN", "RELICANTH", "CLAMPERL", "SKRELP", "CLAUNCHER", "SHELLDER",
    "STARYU", "KRABBY", "POLIWAG", "PSYDUCK", "SLOWPOKE", "CORPHISH", "TYMPOLE", "FRILLISH",
}

# Update 29: ordinary families that appear ONLY in the Safari Zone (a reason to go there besides Legendaries and shinies; all are
# classic Safari Zone Pokemon, all reachable through the intended Safari Zone progression, none is a story or event Pokemon).
# The Safari Zone also weights every Rare and Very Rare family MP_WILD_SAFARI_RARE_BOOST times higher (mp_wild_random.c).
SAFARI_EXCLUSIVE = ("KANGASKHAN", "TAUROS", "HAPPINY", "SCYTHER", "PINSIR", "HERACROSS", "MIME_JR", "FARFETCHD")

# Non-level evolutions (stones, trade, friendship, held items...) appear in the
# wild only from these levels on: first evolution / second evolution.
NONLEVEL_MIN = {1: 30, 2: 45}


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def preprocess() -> str:
    text = run(["arm-none-eabi-gcc", "-E", "-P", *CPPFLAGS, "src/pokemon.c"], cwd=ENGINE)
    # -P drops line markers; also drop any stray directives so fields parse cleanly.
    return "\n".join(line for line in text.split("\n") if not line.lstrip().startswith("#"))


def species_enum() -> dict[str, int]:
    text = (ENGINE / "include/constants/species.h").read_text()
    body = text[re.search(r"enum[^{]*Species\s*\{", text).start():]
    values, current = {}, -1
    for match in re.finditer(r"^\s*(SPECIES_\w+)\s*(?:=\s*([^,\n]+))?,", body, re.M):
        name, value = match.group(1), match.group(2)
        if value is None:
            current += 1
        else:
            value = value.strip()
            current = int(value, 0) if re.fullmatch(r"\d+|0x[0-9a-fA-F]+", value) else values.get(value, current + 1)
        values[name] = current
        if name == "SPECIES_VOLCANION":
            break
    return values


def balanced(text: str, start: int) -> int:
    """Return index just after the brace block beginning at text[start] == '{'."""
    depth = 0
    for i in range(start, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("unbalanced braces")


def split_top(text: str) -> list[str]:
    parts, depth, cur = [], 0, []
    for c in text:
        if c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if "".join(cur).strip():
        parts.append("".join(cur).strip())
    return parts


def parse_species(pre: str, ids: dict[str, int]) -> dict[str, dict]:
    """Parse gSpeciesInfo designated initializers for the given species values.

    Returns {canonical_name: info} with one entry per species value; aliases
    (e.g. SPECIES_HOOPA / SPECIES_HOOPA_CONFINED) resolve to whichever name the
    table actually uses.
    """
    start = pre.index("gSpeciesInfo[]")
    out, seen_values = {}, set()
    for match in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", pre[start:]):
        name = match.group(1)
        value = ids.get(name)
        if value is None or value in seen_values:
            continue
        seen_values.add(value)
        brace = start + match.end() - 1
        body = pre[brace + 1:balanced(pre, brace) - 1]
        fields = {}
        for part in split_top(body):
            m = re.match(r"\.(\w+)\s*=\s*(.*)$", part.strip(), re.S)
            if m:
                fields[m.group(1)] = m.group(2).strip()
        info = {"flags": set()}
        for field in ("baseHP", "baseAttack", "baseDefense", "baseSpeed", "baseSpAttack", "baseSpDefense"):
            info[field] = fields[field]
        for field, key in (("types", "types"), ("eggGroups", "eggs")):
            exprs = split_top(fields[field].strip()[1:-1])
            info[key + "_expr"] = (exprs[0], exprs[-1])
        for flag in ("isRestrictedLegendary", "isSubLegendary", "isMythical", "isUltraBeast",
                     "isParadox", "isMegaEvolution", "isPrimalReversion", "isGigantamax",
                     "isTotem", "isAlolanForm", "isGalarianForm", "isHisuianForm", "isPaldeanForm"):
            if fields.get(flag, "0") in ("1", "TRUE"):
                info["flags"].add(flag)
        evolutions = []
        evo = fields.get("evolutions")
        if evo and evo.startswith("(const struct Evolution[])"):
            block = evo[evo.index("{") + 1:evo.rindex("}")]
            for entry in split_top(block):
                if not entry.startswith("{") or "0xFFFF" in entry:
                    continue
                parts = split_top(entry[1:-1])
                if len(parts) >= 3:
                    evolutions.append((parts[0], parts[1], parts[2], len(parts) > 3))
        info["evolutions"] = evolutions
        out[name] = info
    return out


def enum_values(path: str, enum_name: str) -> dict[str, int]:
    text = (ENGINE / path).read_text()
    body = text[re.search(r"enum[^{]*\b" + enum_name + r"\s*\{", text).end():]
    body = body[:body.index("}")]
    values, current = {}, -1
    for match in re.finditer(r"^\s*(\w+)\s*(?:=\s*(\d+))?\s*,?\s*$", body, re.M):
        current = int(match.group(2)) if match.group(2) else current + 1
        values[match.group(1)] = current
    return values


TYPES = {}
EGGS = {}


def evaluate_stats(species: dict[str, dict]) -> None:
    TYPES.update(enum_values("include/constants/pokemon.h", "Type"))
    EGGS.update(enum_values("include/constants/pokemon.h", "EggGroup"))
    defines = [f"#define {k} {v}" for k, v in {**TYPES, **EGGS}.items()]
    lines, order = [], []
    for name, info in species.items():
        for field in ("baseHP", "baseAttack", "baseDefense", "baseSpeed", "baseSpAttack", "baseSpDefense"):
            lines.append(f'  printf("%d\\n", (int)({info[field]}));')
            order.append((name, field))
        for key in ("types_expr", "eggs_expr"):
            for index, expr in enumerate(info[key]):
                lines.append(f'  printf("%d\\n", (int)({expr}));')
                order.append((name, (key, index)))
    src = "#include <stdio.h>\n" + "\n".join(defines) + "\nint main(void){\n" + "\n".join(lines) + "\nreturn 0;}\n"
    with tempfile.TemporaryDirectory() as tmp:
        c, exe = Path(tmp) / "eval.c", Path(tmp) / "eval"
        c.write_text(src)
        run(["gcc", "-w", "-O0", "-o", str(exe), str(c)])
        values = run([str(exe)]).split()
    type_names = {v: k for k, v in TYPES.items()}
    egg_names = {v: k for k, v in EGGS.items()}
    for (name, field), value in zip(order, values):
        if isinstance(field, tuple):
            key, index = field
            out = species[name].setdefault(key[:-5], [None, None])
            out[index] = (type_names if key == "types_expr" else egg_names)[int(value)]
        else:
            species[name][field] = int(value)
    for info in species.values():
        info["types"] = tuple(info["types"])
        info["eggs"] = tuple(info["eggs"])
    for info in species.values():
        info["bst"] = sum(info[f] for f in ("baseHP", "baseAttack", "baseDefense", "baseSpeed", "baseSpAttack", "baseSpDefense"))


def build_families(species: dict[str, dict]):
    std = set(species)
    edges: dict[str, list[tuple[str, int]]] = {n: [] for n in std}
    incoming = {n: 0 for n in std}
    for name, info in species.items():
        for method, param, target, conditional in info["evolutions"]:
            if target not in std:
                continue  # later-generation or regional evolution: not part of Gen1-6 wild pool
            level = 0
            if method == "EVO_LEVEL" and not conditional and re.fullmatch(r"\d+", param.strip()):
                level = int(param)
            elif method == "EVO_LEVEL" and re.fullmatch(r"\d+", param.strip()) and int(param) > 0:
                level = int(param)  # level evolutions with side conditions (gender/stat/time) keep their level
            edges[name].append((target, level))
            incoming[target] += 1
    families = []
    for base in sorted(std, key=lambda n: species_ids[n]):
        if incoming[base]:
            continue
        members = []  # (species, min_level, depth)

        def walk(name, depth, min_level):
            members.append((name, min_level, depth))
            for target, level in edges[name]:
                if level > 0:
                    child_min = max(level, min_level)
                else:
                    child_min = max(min_level + 15, NONLEVEL_MIN.get(depth + 1, 55))
                walk(target, depth + 1, child_min)

        short = base[len("SPECIES_"):]
        if short in BABIES and short not in BABY_KEEPS_STAGE:
            # A wild baby and its first evolution are both first-stage finds
            # (Pichu and Pikachu at any level; Raichu from level 30).
            members.append((base, 0, 0))
            for target, _ in edges[base]:
                walk(target, 0, 0)
        else:
            walk(base, 0, 0)
        families.append((base, members))
    return families


def classify(base: str, members, species) -> tuple[int, int, int]:
    short = base[len("SPECIES_"):]
    flags = set().union(*(species[m[0]]["flags"] for m in members))
    if flags & {"isRestrictedLegendary", "isSubLegendary", "isMythical", "isUltraBeast", "isParadox"} or short in EXCLUDE_FAMILY:
        return None
    max_bst = max(species[m[0]]["bst"] for m in members)
    if short in PSEUDO_AND_FOSSIL:
        tier = TIER_VERY_RARE
    elif short in FORCE_RARE:
        tier = TIER_RARE
    elif short in FORCE_COMMON:
        tier = TIER_COMMON
    elif max_bst >= 525:
        tier = TIER_RARE
    elif max_bst >= 470:
        tier = TIER_UNCOMMON
    elif max_bst >= 400 and len(members) == 1:
        tier = TIER_UNCOMMON
    else:
        tier = TIER_COMMON
    base_info = species[base]
    types = set(base_info["types"])
    eggs = set(base_info["eggs"])
    habitat = 0
    aquatic = short in AQUATIC_EXTRA or bool(eggs & {"EGG_GROUP_WATER_2", "EGG_GROUP_WATER_3"})
    if not aquatic or short in LAND_ALLOWED_DESPITE_WATER:
        habitat |= 1  # land
    if "TYPE_WATER" in types or aquatic or short in {"WINGULL"}:
        habitat |= 2  # surf
    if short in FISH or (aquatic and "TYPE_WATER" in types):
        habitat |= 4  # fishing
    member_types = set().union(*(species[m[0]]["types"] for m in members))
    if member_types & {"TYPE_ROCK", "TYPE_GROUND", "TYPE_STEEL"} and habitat & 1:
        habitat |= 8  # rock smash
    time = 1 if short in NOCTURNAL else 2 if short in DIURNAL else 0
    return tier, habitat, time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the output would change")
    args = parser.parse_args()
    global species_ids
    all_ids = {n: v for n, v in species_enum().items() if 1 <= v <= MAX_DEX}
    species = parse_species(preprocess(), all_ids)
    species_ids = {n: all_ids[n] for n in species}
    missing = set(range(1, MAX_DEX + 1)) - set(species_ids.values())
    if missing:
        raise SystemExit(f"species data not found for values {sorted(missing)[:5]}")
    evaluate_stats(species)
    families = []
    for base, members in build_families(species):
        result = classify(base, members, species)
        if result is None:
            continue
        families.append((base, members, *result))
    lines = [
        "// GENERATED by tools/mp/gen_wild_pool.py from the engine's own species data.",
        "// Do not edit by hand: change the generator's design lists and re-run it.",
        "// Randomized Gen 1-6 wild encounter families (no Legendary/Mythical/UB/Paradox).",
        "",
        f"#define MP_WILD_FAMILY_COUNT {len(families)}",
        "",
        "static const struct MPWildMember sMPWildMembers[] =",
        "{",
    ]
    family_lines = []
    cursor = 0
    counts = {t: 0 for t in TIER_NAME}
    for base, members, tier, habitat, time in families:
        counts[tier] += 1
        start = cursor
        for name, min_level, depth in sorted(members, key=lambda m: (m[2], m[1], species_ids[m[0]])):
            lines.append(f"    {{{name}, {min(min_level, 100)}, {depth}}},")
            cursor += 1
        t1, t2 = species[base]["types"]
        family_lines.append(
            f"    {{{start}, {cursor - start}, MP_WILD_TIER_{TIER_NAME[tier]}, {habitat}, {time}, {t1}, {t2}}}, // {base}")
    lines += ["};", "", "static const struct MPWildFamily sMPWildFamilies[MP_WILD_FAMILY_COUNT] =", "{"]
    lines += family_lines
    lines += ["};", ""]
    lines.insert(5, "// Families per tier: " + ", ".join(f"{TIER_NAME[t]}={counts[t]}" for t in TIER_NAME))
    text = "\n".join(lines)
    bases = [base for base, *_ in families]
    exclusive = [name for name in SAFARI_EXCLUSIVE if f"SPECIES_{name}" not in bases]
    if exclusive:
        raise SystemExit(f"Safari exclusive families missing from the pool: {exclusive}")
    safari = ["// GENERATED by tools/mp/gen_wild_pool.py: ordinary families that appear only in the Safari Zone (native PC game).",
              "// One flag per family of src/data/mp_wild_pool.h, in the same order.", "",
              "#define MP_WILD_SAFARI_EXCLUSIVE 1", "#define MP_WILD_SAFARI_RARE_BOOST 3", "",
              "static const u8 sMPWildSafariFlags[MP_WILD_FAMILY_COUNT] =", "{"]
    for base in bases:
        safari.append(f"    {'MP_WILD_SAFARI_EXCLUSIVE' if base[len('SPECIES_'):] in SAFARI_EXCLUSIVE else '0'}, // {base}")
    safari += ["};", ""]
    safari_text = "\n".join(safari)
    if args.check:
        return 0 if OUTPUT.exists() and OUTPUT.read_text() == text and OUTPUT_SAFARI.exists() and OUTPUT_SAFARI.read_text() == safari_text else 1
    OUTPUT_SAFARI.write_text(safari_text)
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(ENGINE)}: {len(families)} families, tiers {counts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
