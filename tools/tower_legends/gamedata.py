"""The game's own data for the Battle Tower: Legends generator (tools/tower_legends/generate.py).

Numbers come from the native game's dump (`pokeemerald-mp --dump-tower-data FILE`, src/platform/native_selftest_tower.c);
their names come from the engine's constants headers (include/constants/*.h), read here as text. Nothing is invented: a
name that is not in a header, or a number that is not in the dump, is an error.

The dump's stats are in the order HP, Atk, Def, Spe, SpA, SpD (gSpeciesInfo's base stats as dumped); this module keeps
that order in `Species.stats` and offers `Species.stat(name)`.
"""
import json
import os
import re

STAT_NAMES = ("HP", "Atk", "Def", "Spe", "SpA", "SpD")          # the dump's order
SHOWDOWN_STATS = ("HP", "Atk", "Def", "SpA", "SpD", "Spe")      # the .party / JSON order


class DataError(Exception):
    pass


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_enum(text, enum_regex):
    """Values of a C enum whose header matches enum_regex (implicit values counted, `A = B` aliases resolved)."""
    m = re.search(r"enum[^{;]*?" + enum_regex + r"[^{;]*\{(.*?)\}\s*;", text, re.S)
    if not m:
        raise DataError("enum %s not found" % enum_regex)
    values, order, current = {}, [], -1
    body = _strip_comments(m.group(1))
    # Preprocessor lines and macro-generated entries (items.h's TM/HM aliases) name no new values.
    body = "\n".join(line for line in body.split("\n")
                     if not line.strip().startswith("#") and not line.strip().startswith("RECURSIVELY("))
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        if "=" in entry:
            key, val = [x.strip() for x in entry.split("=", 1)]
            if re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", val):
                current = int(val, 0)
            elif val in values:
                current = values[val]
            else:
                raise DataError("cannot evaluate enum entry %r" % entry)
        else:
            key = entry
            current += 1
        values[key] = current
        order.append(key)
    return values, order


def explicit_enum(text, prefix):
    """{value: canonical name} for `PREFIX_X = n,` entries (the first explicit name of a value is canonical)."""
    names = {}
    for m in re.finditer(r"^\s*(" + prefix + r"_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", text, re.M):
        names.setdefault(int(m.group(2)), m.group(1))
    return names


def canonical_names(values, order, prefix, skip=()):
    """{value: name} keeping the first name (in source order) per value that starts with prefix and is not skipped."""
    names = {}
    for key in order:
        if not key.startswith(prefix) or any(key.startswith(s) for s in skip):
            continue
        names.setdefault(values[key], key)
    return names


def defines(text, prefix):
    out = {}
    for m in re.finditer(r"^#define\s+(" + prefix + r"[A-Z0-9_]*)\s+(\d+)\b", text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


class Species:
    def __init__(self, raw, const):
        self.raw = raw
        self.id = raw["id"]
        self.const = const
        self.name = raw["name"]
        self.dex = raw["dex"]
        self.stats = tuple(raw["stats"])            # HP, Atk, Def, Spe, SpA, SpD
        self.types = tuple(t for i, t in enumerate(raw["types"]) if i == 0 or t != raw["types"][0])
        self.abilities = tuple(raw["abilities"])
        self.flags = raw["flags"]
        self.pics = raw["pics"]
        self.level_up = [tuple(x) for x in raw["levelUp"]]
        self.teachable = list(raw["teachable"])
        self.egg = list(raw["egg"])
        self.evolutions = raw["evolutions"]
        self.forms = [tuple(f) for f in raw["forms"]]
        self.gender = raw["gender"]

    def stat(self, name):
        return self.stats[STAT_NAMES.index(name)]

    @property
    def bst(self):
        return sum(self.stats)

    @property
    def all_pics(self):
        return all(self.pics[k] == 1 for k in ("front", "back", "shiny", "icon"))

    @property
    def legendary(self):
        f = self.flags
        return bool(f["restricted"] or f["sub"] or f["mythical"] or f["ultraBeast"] or f["paradox"])

    def __repr__(self):
        return self.const


class Game:
    """The dump plus the constants headers of one engine checkout."""

    def __init__(self, dump_path, engine_root):
        with open(dump_path, encoding="utf-8") as f:
            self.dump = json.load(f)
        if self.dump.get("version") != 1:
            raise DataError("unknown dump version %r" % self.dump.get("version"))
        self.root = engine_root
        inc = "include/constants/"
        species_h = _read(engine_root, inc + "species.h")
        moves_h = _read(engine_root, inc + "moves.h")
        items_h = _read(engine_root, inc + "items.h")
        abilities_h = _read(engine_root, inc + "abilities.h")
        trainers_h = _read(engine_root, inc + "trainers.h")
        songs_h = _read(engine_root, inc + "songs.h")
        ai_h = _read(engine_root, inc + "battle_ai.h")
        forms_h = _read(engine_root, inc + "form_change_types.h")
        pokemon_h = _read(engine_root, inc + "pokemon.h")
        effects_h = _read(engine_root, inc + "battle_move_effects.h")
        hold_h = _read(engine_root, inc + "hold_effects.h")

        self.species_const = explicit_enum(species_h, "SPECIES")
        vals, order = parse_enum(moves_h, r"Move\b")            # some entries are `MOVE_X = MOVES_COUNT_GENn`
        self.move_const = canonical_names(vals, order, "MOVE_", skip=("MOVES_",))
        vals, order = parse_enum(items_h, r"Item\b")            # some entries are `ITEM_X = FIRST_..._INDEX`
        self.item_const = canonical_names(vals, order, "ITEM_", skip=("ITEMS_",))
        vals, order = parse_enum(abilities_h, r"Ability\b")
        self.ability_const = canonical_names(vals, order, "ABILITY_", skip=("ABILITIES_",))
        vals, order = parse_enum(trainers_h, r"TrainerPicID")
        self.pic_values = {k: v for k, v in vals.items() if k.startswith("TRAINER_PIC_") and k != "TRAINER_PIC_COUNT"}
        self.pic_const = {v: k for k, v in self.pic_values.items()}
        vals, order = parse_enum(trainers_h, r"TrainerClassID")
        self.class_values = {k: v for k, v in vals.items() if k != "TRAINER_CLASS_COUNT"}
        self.class_const = {v: k for k, v in self.class_values.items()}
        self.encounter_music = defines(trainers_h, "TRAINER_ENCOUNTER_MUSIC_")
        self.songs = defines(songs_h, "MUS_")
        self.ai_flags = {}
        for m in re.finditer(r"^#define\s+(AI_FLAG_[A-Z0-9_]+)\s+AI_FLAG\((\d+)\)", ai_h, re.M):
            self.ai_flags[m.group(1)] = 1 << int(m.group(2))
        for m in re.finditer(r"^#define\s+(AI_FLAG_[A-Z0-9_]+)\s+\(([A-Z0-9_| ]+)\)", ai_h, re.M):
            value = 0
            for part in m.group(2).split("|"):
                value |= self.ai_flags[part.strip()]
            self.ai_flags[m.group(1)] = value
        vals, _ = parse_enum(forms_h, r"FormChanges")
        self.form_methods = vals
        vals, _ = parse_enum(pokemon_h, r"EvolutionMethods")
        self.evo_methods = vals
        vals, _ = parse_enum(pokemon_h, r"EvolutionConditions")
        self.evo_conditions = vals
        vals, _ = parse_enum(pokemon_h, r"Type\b")
        self.type_const = {v: k for k, v in vals.items() if k.startswith("TYPE_")}
        self.type_values = {k: v for k, v in vals.items() if k.startswith("TYPE_")}
        self.natures = {k: v for k, v in defines(pokemon_h, "NATURE_").items() if v < 25}
        vals, _ = parse_enum(effects_h, r"BattleMoveEffects")
        self.effect_values = vals
        self.effect_const = {v: k for k, v in vals.items()}
        vals, _ = parse_enum(hold_h, r"HoldEffect\b")
        self.hold_values = vals
        self.hold_const = {v: k for k, v in vals.items()}

        self.species = {}
        for raw in self.dump["species"]:
            const = self.species_const.get(raw["id"])
            if const is None:
                raise DataError("species %d of the dump has no SPECIES_ constant" % raw["id"])
            self.species[raw["id"]] = Species(raw, const)
        self.species_by_const = {s.const: s for s in self.species.values()}
        self.moves = {m["id"]: m for m in self.dump["moves"]}
        self.items = {i["id"]: i for i in self.dump["items"]}
        self.abilities = {a["id"]: a for a in self.dump["abilities"]}
        self.classes = {c["id"]: c for c in self.dump["trainerClasses"]}
        self.pics = {p["id"]: p for p in self.dump["trainerPics"]}
        for mid in self.moves:
            if mid not in self.move_const:
                raise DataError("move %d has no MOVE_ constant" % mid)
        for iid in self.items:
            if iid not in self.item_const:
                raise DataError("item %d has no ITEM_ constant" % iid)
        self.move_by_const = {self.move_const[m]: m for m in self.moves}
        self.item_by_const = {self.item_const[i]: i for i in self.items}
        self.ability_by_const = {c: v for v, c in self.ability_const.items()}

        self._build_evolution_graph()
        self._min_level_cache = {}

    # ----- names -------------------------------------------------------------------------------------------------
    def move(self, const):
        if const not in self.move_by_const:
            raise DataError("unknown move %s" % const)
        return self.move_by_const[const]

    def item(self, const):
        if const not in self.item_by_const:
            raise DataError("unknown item %s" % const)
        return self.item_by_const[const]

    def ability(self, const):
        if const not in self.ability_by_const:
            raise DataError("unknown ability %s" % const)
        return self.ability_by_const[const]

    def sp(self, const):
        if const not in self.species_by_const:
            raise DataError("unknown or disabled species %s" % const)
        return self.species_by_const[const]

    def type_of(self, const):
        return self.type_values[const]

    # ----- evolutions --------------------------------------------------------------------------------------------
    def _build_evolution_graph(self):
        self.pre = {}        # target id -> [(source id, evolution dict)]
        for s in self.species.values():
            for evo in s.evolutions:
                if evo["target"] in self.species:
                    self.pre.setdefault(evo["target"], []).append((s.id, evo))

    def is_trade_stand_in(self, source, evo):
        """An item evolution that stands in for a trade (the Linking Cord, or the item of the same pair's trade)."""
        if evo["method"] != self.evo_methods["EVO_ITEM"]:
            return False
        if evo["param"] == self.item_by_const.get("ITEM_LINKING_CORD"):
            return True
        src = self.species[source]
        return any(e["method"] == self.evo_methods["EVO_TRADE"] and e["target"] == evo["target"] for e in src.evolutions)

    def edge_min_level(self, source, evo):
        """The lowest level one evolution step allows (section 7 of the design), before chaining."""
        method = evo["method"]
        friendship = any(c[0] == self.evo_conditions["IF_MIN_FRIENDSHIP"] for c in evo["conditions"])
        if method == self.evo_methods["EVO_LEVEL"] and evo["param"] > 0:
            return evo["param"], "level %d" % evo["param"]
        if method == self.evo_methods["EVO_LEVEL"] and friendship:
            return 18, "friendship"
        if method == self.evo_methods["EVO_TRADE"] or self.is_trade_stand_in(source, evo):
            return 30, "trade"
        if method == self.evo_methods["EVO_ITEM"]:
            return 20, "item %s" % self.item_const.get(evo["param"], evo["param"])
        return 25, "other (method %d)" % method

    def min_level(self, species_id, _stack=()):
        """The lowest level the species can legally have: every earlier stage's requirement chained (1 for a base form)."""
        if species_id in self._min_level_cache:
            return self._min_level_cache[species_id]
        sources = [(src, evo) for src, evo in self.pre.get(species_id, []) if src not in _stack and src != species_id]
        if not sources:
            result = 1
        else:
            result = 101
            for src, evo in sources:
                edge, _ = self.edge_min_level(src, evo)
                result = min(result, max(edge, self.min_level(src, _stack + (species_id,))))
        self._min_level_cache[species_id] = result
        return result

    def chain(self, species_id):
        """The species and its pre-evolutions (the cheapest path), root first."""
        out = [species_id]
        seen = {species_id}
        while True:
            sources = [src for src, _ in self.pre.get(out[0], []) if src not in seen]
            if not sources:
                break
            best = min(sources, key=lambda s: (self.min_level(s), s))
            out.insert(0, best)
            seen.add(best)
        return out

    def can_evolve(self, species_id):
        return any(e["target"] in self.species for e in self.species[species_id].evolutions)

    # ----- forms -------------------------------------------------------------------------------------------------
    def mega_forms(self, species_id):
        """[(mega species id, stone item id)] of the species' own form-change table (item Mega Evolution only)."""
        method = self.form_methods["FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM"]
        out = []
        for f in self.species[species_id].forms:
            if f[0] == method and f[1] != species_id and f[1] in self.species:
                out.append((f[1], f[2]))
        return out

    BATTLE_FORM_METHODS = (
        "FORM_CHANGE_BEGIN_BATTLE", "FORM_CHANGE_BATTLE_SWITCH_OUT", "FORM_CHANGE_BATTLE_SWITCH_IN",
        "FORM_CHANGE_BATTLE_HP_PERCENT_TURN_END", "FORM_CHANGE_BATTLE_HP_PERCENT_SEND_OUT",
        "FORM_CHANGE_BATTLE_HP_PERCENT_DURING_MOVE", "FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE",
        "FORM_CHANGE_BATTLE_PRIMAL_REVERSION", "FORM_CHANGE_BATTLE_WEATHER", "FORM_CHANGE_BATTLE_TURN_END",
        "FORM_CHANGE_BATTLE_ULTRA_BURST", "FORM_CHANGE_BATTLE_HIT_BY_MOVE_CATEGORY",
        "FORM_CHANGE_BATTLE_HIT_BY_CONFUSION_SELF_DMG", "FORM_CHANGE_BATTLE_BEFORE_MOVE",
        "FORM_CHANGE_BATTLE_AFTER_MOVE", "FORM_CHANGE_BATTLE_BOND", "FORM_CHANGE_BATTLE_BEFORE_MOVE_CATEGORY",
        "FORM_CHANGE_ITEM_HOLD",
    )

    def battle_form_changes(self, species_id):
        """Form changes other than a held-stone Mega Evolution that can happen to this species in or for a battle."""
        methods = {self.form_methods[m] for m in self.BATTLE_FORM_METHODS}
        return [f for f in self.species[species_id].forms if f[0] in methods and f[1] != species_id]

    # ----- moves -------------------------------------------------------------------------------------------------
    def level_up_moves(self, species_id, level):
        s = self.species[species_id]
        return {move for lvl, move in s.level_up if lvl <= level and move in self.moves}

    def egg_moves(self, species_id):
        out = set()
        for sid in self.chain(species_id):
            out.update(m for m in self.species[sid].egg if m in self.moves)
        return out

    def learnable(self, species_id, level):
        """Every move the floor's rules allow the species at this level (before the move bans)."""
        moves = set(self.level_up_moves(species_id, level))
        if level >= 30:
            moves.update(m for m in self.species[species_id].teachable if m in self.moves)
        if level >= 50:
            moves.update(self.egg_moves(species_id))
        return moves
