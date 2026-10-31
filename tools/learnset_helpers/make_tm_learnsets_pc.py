#!/usr/bin/env python3
"""The PC game's TM learnsets: which species each TM can teach (the owner's rule of 2026-10-31).

"any pokemon that can actually learn the tm, from gen 1-6 ik they change in gens but if it was able to learn some from
gen 1-6 they can learn it". So TM n teaches its move to a species when, in any Gen 1-6 game, that species could learn
that move by TM (the porymoves files' TMMoves for the games in GEN_1_6_GAMES, united; the TM number the move had there
does not matter), plus the owner's custom extensions in CUSTOM_EXTENSIONS. Tutors are not covered: they keep their own
lists (teachable_learnsets_pc.h, make_teachables_pc.py). HM moves also appear in TMMoves; no HM move is on the TM list,
so they change nothing here (HM01-HM08 keep their own compatibility).

    python3 tools/learnset_helpers/make_tm_learnsets_pc.py [OUTPUT]      (default src/data/pokemon/tm_learnsets_pc.h)

Run from the engine root. Reads:
  tools/learnset_helpers/tm_moves_pc.json   the TM list, TM01 to TM168 in order ("tms": {"TM01": "MOVE_...", ...}).
      It is data: renumbering a TM is an edit there and a re-run. The generator stops if a TM number names a different
      move in include/constants/tms_hms.h (FOREACH_TM, or FOREACH_TM_GBA plus tms_hms_pc.h's FOREACH_TM_PC) than in the
      JSON, so the bits always mean the game's own TM numbers.
  tools/learnset_helpers/porymoves_files/<game>.json   per-game learnsets (keys such as BULBASAUR or ROTOM_HEAT)
  include/constants/species.h, include/constants/pokedex.h, src/data/pokemon/species_info.h and species_info/*.h
Writes gTmCompatPc: one bitset of TM_COMPAT_PC_BYTES bytes per species constant (bit n-1 = TM n), with designated
initializers, so a species that learns no TM has no line (all zero).

Which data each species constant of species.h takes:
  1. for the forms in POST_GEN_6_FORMS (first introduced after Gen 6): see "after Gen 6" below;
  2. the Gen 1-6 form's data for the forms in SWITCHED_FORMS (first introduced after Gen 6, but a Gen 1-6 Pokemon turns
     into them here with an item RARE GOODS sells: the Pokemon keeps its list, so the item does not take TMs away);
  3. the form's own porymoves entry when a Gen 1-6 game has one (Deoxys, Wormadam, Rotom, Shaymin, Kyurem, Meowstic,
     Hoopa and Eternal Floette forms, and every plain species);
  4. for species and forms first introduced after Gen 6 (National Dex numbers after Volcanion, and the species info's
     regional-form flags: Alolan, Galarian, Hisuian, Paldean, and Totem): see "after Gen 6" below;
  5. otherwise the entry of the teachable learnset the species info gives it (.teachableLearnset = sXxxTeachableLearnset
     -> XXX, as make_teachables.py names them): a form that existed in Gen 1-6 sharing its base species' list there
     (Unown letters, Arceus types, Giratina Origin, Vivillon patterns, Cosplay Pikachu, ...) or a battle-only form (Mega,
     Primal, Gigantamax, Castform, Zen Darmanitan, Blade Aegislash, ...), which takes the list of the form it returns to:
     TMs are taught outside battle.
A species constant with none of these (a species info entry missing, or a new form) learns no TM and is listed on stderr.

After Gen 6: no Gen 1-6 game had these Pokemon, so the owner's rule cannot speak for them. They keep the TM moves they
have today: TM n teaches one when its own games teach it the move by TM or tutor (its PC teachable list, src/data/
pokemon/teachable_learnsets_pc.h, the 2026-10-27 rule's). None of them can be caught here; Kleavor (Scyther with the
Black Augurite, sold at RARE GOODS) and Kingambit (Bisharp, after the Leader's Crest battles) can come from evolving.
"""
from __future__ import annotations

import json
import pathlib
import re
import sys
import unicodedata

GEN_1_6_GAMES = ("rgb", "y", "gs", "c", "rse", "frlg", "xd", "dp", "pt", "hgss", "bw", "b2w2", "xy", "oras")
LAST_GEN_6_SPECIES = "NATIONAL_DEX_VOLCANION"
REGIONAL_OR_TOTEM_FLAGS = ("isAlolanForm", "isGalarianForm", "isHisuianForm", "isPaldeanForm", "isTotem")

# The owner's additions on top of the rule: (species, move, the owner's words).
CUSTOM_EXTENSIONS = (
    (("SPECIES_FERROSEED", "SPECIES_FERROTHORN"), "MOVE_STEALTH_ROCK",
     "\"For example, Ferrothorn should be able to learn Stealth Rock through the expanded TM system if we intentionally "
     "support its legitimate move access from Gen 6 breeding/tutoring. Treat this as a documented custom compatibility "
     "extension, not as an original official TM learnset.\" (the owner's TM request, 2026-10-31); Ferroseed, its "
     "pre-evolution, with it. No Gen 1-6 TM taught them Stealth Rock (egg move, B2W2/ORAS tutor)."),
)

# Forms of Gen 1-6 species first introduced after Gen 6, which no Gen 1-6 form turns into here, and which the species
# info flags do not mark (they mark regional forms and Totems): no TM moves.
POST_GEN_6_FORMS = {
    "SPECIES_PIKACHU_ORIGINAL": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_HOENN": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_SINNOH": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_UNOVA": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_KALOS": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_ALOLA": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_PARTNER": "cap Pikachu, Gen 7",
    "SPECIES_PIKACHU_WORLD": "cap Pikachu, Gen 8",
    "SPECIES_PIKACHU_STARTER": "partner Pikachu, Let's Go (Gen 7)",
    "SPECIES_EEVEE_STARTER": "partner Eevee, Let's Go (Gen 7)",
    "SPECIES_GRENINJA_BOND": "Battle Bond Greninja, Gen 7",
    "SPECIES_GRENINJA_ASH": "Gen 7; battle-only, returns to Battle Bond Greninja",
    "SPECIES_BASCULIN_WHITE_STRIPED": "Legends: Arceus (Gen 8)",
}

# Forms first introduced after Gen 6 that a Gen 1-6 Pokemon turns into here with an item RARE GOODS sells (or, in
# battle, from such a form): they take that Gen 1-6 form's list, as battle-only forms do, so that holding or using the
# item does not switch TMs off. Form -> (the Gen 1-6 form it comes from, how).
SWITCHED_FORMS = {
    "SPECIES_DIALGA_ORIGIN": ("SPECIES_DIALGA", "Legends: Arceus form; Dialga holding the Adamant Crystal"),
    "SPECIES_PALKIA_ORIGIN": ("SPECIES_PALKIA", "Legends: Arceus form; Palkia holding the Lustrous Globe"),
    "SPECIES_ZYGARDE_10_AURA_BREAK": ("SPECIES_ZYGARDE_50", "Gen 7 form; the Zygarde Cube"),
    "SPECIES_ZYGARDE_50_POWER_CONSTRUCT": ("SPECIES_ZYGARDE_50", "Gen 7 form; the Zygarde Cube"),
    "SPECIES_ZYGARDE_10_POWER_CONSTRUCT": ("SPECIES_ZYGARDE_50", "Gen 7 form; the Zygarde Cube"),
    "SPECIES_ZYGARDE_COMPLETE": ("SPECIES_ZYGARDE_50", "Gen 7, in battle, from a Power Construct Zygarde"),
    "SPECIES_ZYGARDE_MEGA": ("SPECIES_ZYGARDE_50", "Legends Z-A, in battle, from Complete Zygarde"),
}

SNAKIFY = re.compile(r"(?!^)([A-Z]+)")
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(?:\([^)]*\))?((?:[^\n]*\\\n)*[^\n]*)", re.M)
ENTRY = re.compile(r"^\s*\[(SPECIES_\w+)\]\s*=", re.M)
LEARNSET = re.compile(r"\.teachableLearnset\s*=\s*s(\w+?)TeachableLearnset\b")
NATDEX = re.compile(r"\.natDexNum\s*=\s*(NATIONAL_DEX_\w+)")
FLAG = re.compile(r"\.(is\w+)\s*=\s*TRUE\b")


def fail(message: str) -> None:
    sys.exit(f"make_tm_learnsets_pc.py: {message}")


def learnset_key(name: str) -> str:
    """make_teachables.py's own conversion: sRaichuAlolaTeachableLearnset -> RAICHU_ALOLA."""
    return SNAKIFY.sub(r"_\1", name).upper()


def ascii_key(name: str) -> str:
    """FLABÉBÉ -> FLABEBE, as make_teachables_pc.py spells the porymoves keys."""
    return unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()


def enum_body(text: str, header: str) -> str:
    start = text.index(header)
    return text[start:text.index("};", start)]


def read_tm_list(path: pathlib.Path) -> list[str]:
    """tm_moves_pc.json's "tms": {"TM01": "MOVE_...", ...} -> the moves, TM01 first."""
    by_number: dict[int, str] = {}
    for key, move in json.loads(path.read_text())["tms"].items():
        match = re.fullmatch(r"TM(\d+)", key)
        if match is None or int(match.group(1)) in by_number:
            fail(f"{path}: bad or repeated TM key {key!r}")
        by_number[int(match.group(1))] = move
    if sorted(by_number) != list(range(1, len(by_number) + 1)):
        fail(f"{path}: TM numbers must run TM01, TM02, ... without gaps")
    moves = [by_number[number] for number in sorted(by_number)]
    if len(set(moves)) != len(moves):
        fail(f"{path}: a move has two TMs")
    return moves


def macro_moves(text: str, name: str) -> list[str] | None:
    match = re.search(r"^#define\s+" + name + r"\(F\)((?:[^\n]*\\\n)*[^\n]*)", text, re.M)
    return None if match is None else ["MOVE_" + move for move in re.findall(r"\bF\((\w+)\)", match.group(1))]


def check_against_items(tms: list[str]) -> int:
    """The TM numbers the game's items use (tms_hms.h) must name the same moves as the JSON."""
    text = pathlib.Path("include/constants/tms_hms.h").read_text()
    game = macro_moves(text, "FOREACH_TM_GBA")
    if game is None:
        game = macro_moves(text, "FOREACH_TM") or []
    else:
        pc_header = pathlib.Path("include/constants/tms_hms_pc.h")
        if pc_header.exists():
            game += macro_moves(pc_header.read_text(), "FOREACH_TM_PC") or []
    for number, move in enumerate(game, 1):
        if number > len(tms) or tms[number - 1] != move:
            fail(f"TM{number:02d} is {move} in include/constants/tms_hms*.h but "
                 f"{tms[number - 1] if number <= len(tms) else 'missing'} in tm_moves_pc.json")
    return len(game)


def read_moves() -> set[str]:
    return set(re.findall(r"^\s*(MOVE_\w+)\b", pathlib.Path("include/constants/moves.h").read_text(), re.M))


def read_species() -> tuple[dict[str, int], dict[str, str]]:
    """species.h's constants with a number of their own (in order), and its aliases."""
    body = enum_body(pathlib.Path("include/constants/species.h").read_text(), "enum __attribute__((packed)) Species")
    numbered, aliases = {}, {}
    for line in body.split("\n"):
        line = re.sub(r"//.*", "", line).strip().rstrip(",").strip()
        if match := re.fullmatch(r"(SPECIES_\w+)\s*=\s*(\d+)", line):
            numbered[match.group(1)] = int(match.group(2))
        elif match := re.fullmatch(r"(SPECIES_\w+)\s*=\s*(SPECIES_\w+)", line):
            aliases[match.group(1)] = match.group(2)
    return numbered, aliases


def read_national_dex() -> dict[str, int]:
    body = enum_body(pathlib.Path("include/constants/pokedex.h").read_text(), "enum NationalDexOrder")
    numbers, number = {}, -1
    for line in body.split("\n"):
        line = re.sub(r"//.*", "", line).strip().rstrip(",").strip()
        if match := re.fullmatch(r"(NATIONAL_DEX_\w+)(?:\s*=\s*(\d+))?", line):
            number = int(match.group(2)) if match.group(2) else number + 1
            numbers[match.group(1)] = number
    return numbers


def read_species_info(aliases: dict[str, str]) -> dict[str, dict]:
    """Each gSpeciesInfo entry's teachable learnset, National Dex constant and TRUE flags (through its macros)."""
    files = sorted(pathlib.Path("src/data/pokemon/species_info").glob("gen_*_families.h"))
    files.append(pathlib.Path("src/data/pokemon/species_info.h"))
    texts = [path.read_text() for path in files]
    macros = {match.group(1): match.group(2) for text in texts for match in DEFINE.finditer(text)}
    info: dict[str, dict] = {}
    for path, text in zip(files, texts):
        text = DEFINE.sub("", text)
        starts = [(match.start(), match.group(1)) for match in ENTRY.finditer(text)]
        for index, (start, species) in enumerate(starts):
            chunk = text[start:starts[index + 1][0] if index + 1 < len(starts) else len(text)]
            expanded, pending, seen = [chunk], [chunk], set()
            while pending:
                for word in set(re.findall(r"\b[A-Z][A-Z0-9_]+\b", pending.pop())):
                    if word in macros and word not in seen:
                        seen.add(word)
                        expanded.append(macros[word])
                        pending.append(macros[word])
            expanded = "\n".join(expanded)
            learnsets = set(LEARNSET.findall(expanded))
            if len(learnsets) > 1:
                fail(f"{path}: {species} names more than one teachable learnset: {sorted(learnsets)}")
            natdex = NATDEX.search(expanded)
            entry = {"learnset": learnsets.pop() if learnsets else None,
                     "natdex": natdex.group(1) if natdex else None,
                     "flags": set(FLAG.findall(expanded))}
            species = aliases.get(species, species)
            if species in info and info[species]["learnset"] != entry["learnset"]:
                fail(f"{path}: {species} has two species info entries with different learnsets")
            info.setdefault(species, entry)
    return info


def read_pc_teachables() -> dict[str, set[str]]:
    """teachable_learnsets_pc.h: sXxxTeachableLearnset -> its moves (the after-Gen-6 Pokemon's TM moves come from it)."""
    text = pathlib.Path("src/data/pokemon/teachable_learnsets_pc.h").read_text()
    found = re.findall(r"static const u16 s(\w+)TeachableLearnset\[\] = \{(.*?)\};", text, re.S)
    if not found:
        fail("src/data/pokemon/teachable_learnsets_pc.h: no teachable learnset found")
    return {name: set(re.findall(r"\bMOVE_\w+", body)) - {"MOVE_UNAVAILABLE"} for name, body in found}


def read_gen_1_6_tms(folder: pathlib.Path) -> dict[str, set[str]]:
    """Porymoves key -> every move a Gen 1-6 game teaches that species by TM (HMs included, as the files list them)."""
    taught: dict[str, set[str]] = {}
    for game in GEN_1_6_GAMES:
        with open(folder / f"{game}.json", "r", encoding="utf-8") as fp:
            for species, by_method in json.load(fp).items():
                taught.setdefault(ascii_key(species), set()).update(by_method.get("TMMoves", []))
    return taught


def resolve(species: str, infos: dict[str, dict], dex: dict[str, int],
            taught: dict[str, set[str]]) -> tuple[str | None, str]:
    """(porymoves key or None, how it was chosen) for one species constant."""
    if species in POST_GEN_6_FORMS:
        return None, f"form introduced after Gen 6 ({POST_GEN_6_FORMS[species]})"
    if species in SWITCHED_FORMS:
        source, note = SWITCHED_FORMS[species]
        key, _how = resolve(source, infos, dex, taught)
        if key is None:
            fail(f"SWITCHED_FORMS: {source} ({species}'s Gen 1-6 form) has no Gen 1-6 data")
        return key, f"turns from {source} ({note})"
    own = ascii_key(species[len("SPECIES_"):])
    if own in taught:
        return own, "own entry"
    info = infos.get(species)
    if info is None:
        return None, "no species info entry"
    if info["natdex"] in dex and dex[info["natdex"]] > dex[LAST_GEN_6_SPECIES]:
        return None, "species introduced after Gen 6"
    flags = sorted(info["flags"].intersection(REGIONAL_OR_TOTEM_FLAGS))
    if flags:
        return None, "form introduced after Gen 6 (" + ", ".join(flags) + ")"
    if info["learnset"] and learnset_key(info["learnset"]) in taught:
        return learnset_key(info["learnset"]), "shares the learnset"
    return None, "no Gen 1-6 data"


def build() -> dict:
    """Everything the header (and a report) needs, read from the engine tree in the current directory."""
    tms = read_tm_list(pathlib.Path(__file__).with_name("tm_moves_pc.json"))
    known_moves = read_moves()
    unknown = [move for move in tms if move not in known_moves]
    if unknown:
        fail(f"tm_moves_pc.json: not in include/constants/moves.h: {unknown}")
    checked = check_against_items(tms)
    numbered, aliases = read_species()
    info = read_species_info(aliases)
    dex = read_national_dex()
    taught = read_gen_1_6_tms(pathlib.Path("tools/learnset_helpers/porymoves_files"))
    tm_index = {move: index for index, move in enumerate(tms)}
    pc_teachables = read_pc_teachables()

    species_moves: dict[str, set[str]] = {}
    sources: dict[str, tuple[str | None, str]] = {}
    after_gen_6: set[str] = set()
    for species in numbered:
        if species == "SPECIES_NONE":
            continue
        key, how = resolve(species, info, dex, taught)
        sources[species] = (key, how)
        species_moves[species] = {move for move in taught.get(key, ()) if move in tm_index}
        if key is None and "after Gen 6" in how:
            # After Gen 6 (the docstring): its own games' TM and tutor moves, as its PC teachable list has them today
            learnset = (info.get(species) or {}).get("learnset")
            if learnset is not None and learnset not in pc_teachables:
                fail(f"{species}: s{learnset}TeachableLearnset is not in teachable_learnsets_pc.h")
            species_moves[species] = {move for move in pc_teachables.get(learnset, ()) if move in tm_index}
            after_gen_6.add(species)

    extended: dict[str, list[str]] = {}
    for targets, move, _quote in CUSTOM_EXTENSIONS:
        if move not in tm_index:
            fail(f"custom extension {move} is not on the TM list")
        for species in targets:
            if species not in species_moves:
                fail(f"custom extension: {species} is not a species constant")
            if move not in species_moves[species]:
                species_moves[species].add(move)
                extended.setdefault(species, []).append(move)

    return {"tms": tms, "checked": checked, "numbered": numbered, "info": info, "taught": taught,
            "sources": sources, "species_moves": species_moves, "extended": extended, "tm_index": tm_index,
            "after_gen_6": after_gen_6}


def render(data: dict) -> str:
    tms, tm_index = data["tms"], data["tm_index"]
    size = (len(tms) + 7) // 8
    rule = [
        "//",
        "// DO NOT MODIFY THIS FILE! It is auto-generated by tools/learnset_helpers/make_tm_learnsets_pc.py",
        "// The PC game's TM learnsets (the owner's rule of 2026-10-31): \"any pokemon that can actually learn the tm, from",
        "// gen 1-6 ik they change in gens but if it was able to learn some from gen 1-6 they can learn it\". TM n teaches its",
        "// move to a species when a Gen 1-6 game (" + ", ".join(GEN_1_6_GAMES) + ")",
        "// taught that species the move by TM (porymoves TMMoves, under any TM number), plus the custom extensions below.",
        "// Tutors are not covered: they keep teachable_learnsets_pc.h.",
        "// Pokemon first introduced after Gen 6 (no Gen 1-6 game had them; none can be caught here, Kleavor and Kingambit",
        "// can come from evolving) keep the TM moves they have today: their own games' TM and tutor moves, as their",
        "// teachable_learnsets_pc.h list has them (\"after Gen 6\" below).",
        "//",
        "// TM n can teach a species when species < ARRAY_COUNT(gTmCompatPc) and",
        "// (gTmCompatPc[species][(n - 1) / 8] >> ((n - 1) % 8)) & 1. A species with no line learns no TM.",
        "//",
        "// Custom extensions:",
    ]
    for targets, move, quote in CUSTOM_EXTENSIONS:
        rule.append(f"// - {move}: " + ", ".join(targets))
        words, line = quote.split(), "//   "
        for word in words:
            if len(line) + len(word) + 1 > 120:
                rule.append(line.rstrip())
                line = "//   "
            line += word + " "
        rule.append(line.rstrip())
    rule.append("//")
    rule.append("// TMs (tools/learnset_helpers/tm_moves_pc.json):")
    cells = [f"TM{n:02d} {move[len('MOVE_'):]}" for n, move in enumerate(tms, 1)]
    for start in range(0, len(cells), 4):
        rule.append("//   " + "".join(f"{cell:<28}" for cell in cells[start:start + 4]).rstrip())
    rule.append("//")

    lines = rule + [
        "",
        "#ifndef GUARD_DATA_POKEMON_TM_LEARNSETS_PC_H",
        "#define GUARD_DATA_POKEMON_TM_LEARNSETS_PC_H",
        "",
        f"#define TM_COMPAT_PC_COUNT {len(tms)}",
        f"#define TM_COMPAT_PC_BYTES {size}",
        "",
        "const u8 gTmCompatPc[][TM_COMPAT_PC_BYTES] =",
        "{",
    ]
    order = sorted(data["species_moves"], key=lambda species: data["numbered"][species])
    for species in order:
        moves = data["species_moves"][species]
        if not moves:
            continue
        bits = [0] * size
        for move in moves:
            index = tm_index[move]
            bits[index // 8] |= 1 << (index % 8)
        key, _how = data["sources"][species]
        note = f"{len(moves)} TM{'s' if len(moves) != 1 else ''}"
        if key and key != species[len("SPECIES_"):]:
            note += f", data of {key}"
        if species in data["after_gen_6"]:
            note += ", after Gen 6: its own games' list"
        if species in data["extended"]:
            note += ", + " + ", ".join(move[len("MOVE_"):] for move in data["extended"][species]) + " (custom)"
        lines.append(f"    [{species}] = {{" + ", ".join(f"0x{byte:02X}" for byte in bits) + f"}}, // {note}")
    lines += ["};", "", "#endif // GUARD_DATA_POKEMON_TM_LEARNSETS_PC_H", ""]
    return "\n".join(lines)


def main() -> int:
    output = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src/data/pokemon/tm_learnsets_pc.h")
    data = build()
    output.write_text(render(data))
    rows = sum(1 for moves in data["species_moves"].values() if moves)
    pairs = sum(len(moves) for moves in data["species_moves"].values())
    unexpected = [species for species, (key, how) in data["sources"].items()
                  if key is None and how in ("no Gen 1-6 data", "no species info entry")]
    late = sum(len(data["species_moves"][species]) for species in data["after_gen_6"])
    print(f"{output}: {len(data['tms'])} TMs ({data['checked']} checked against tms_hms.h), {rows} species with a TM, "
          f"{pairs} species-TM pairs, {sum(len(m) for m in data['extended'].values())} from custom extensions, "
          f"{late} of Pokemon first introduced after Gen 6 ({len(data['after_gen_6'])} species constants)")
    if unexpected:
        print("no Gen 1-6 data, no TM: " + ", ".join(unexpected), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
