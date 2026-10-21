"""Builds the Tower's Pokemon and trainers from the game's data, the floor rules (rules.py) and the curated pools.

Everything a set needs is chosen here: the most evolved legal stage of a pool's line, its moves (legal at the floor,
scored by power, STAB, the battle form's attacking stat, accuracy and drawbacks, then picked for type coverage and one
utility or setup move where the role calls for it), its Ability, held item, nature, EVs and IVs. Hand-written fields
(bosses.py) are kept as given and only the missing ones are filled in. All randomness comes from the caller's
random.Random, so the output is the same on every run.
"""
import math

import rules

STATS_SD = ("HP", "Atk", "Def", "SpA", "SpD", "Spe")          # .party / JSON order
DUMP_INDEX = {"HP": 0, "Atk": 1, "Def": 2, "Spe": 3, "SpA": 4, "SpD": 5}
NATURE_STATS = ("Atk", "Def", "Spe", "SpA", "SpD")            # nature = 5 * raised + lowered

ABILITY_SCORES = {
    "HUGE_POWER": 12, "PURE_POWER": 12, "SPEED_BOOST": 7, "ADAPTABILITY": 6, "PROTEAN": 6, "MAGIC_GUARD": 5,
    "INTIMIDATE": 6, "LEVITATE": 5, "REGENERATOR": 5, "MULTISCALE": 5, "MOXIE": 5, "DROUGHT": 5, "DRIZZLE": 5,
    "SAND_STREAM": 5, "SNOW_WARNING": 4, "THICK_FAT": 4, "TOUGH_CLAWS": 5, "PRANKSTER": 4, "MAGIC_BOUNCE": 5,
    "TECHNICIAN": 3, "SKILL_LINK": 3, "SHEER_FORCE": 3, "IRON_FIST": 2, "STRONG_JAW": 2, "MEGA_LAUNCHER": 2,
    "RECKLESS": 2, "GUTS": 3, "POISON_HEAL": 3, "MARVEL_SCALE": 3, "STURDY": 3, "SERENE_GRACE": 3, "DOWNLOAD": 4,
    "NATURAL_CURE": 3, "ROUGH_SKIN": 3, "IRON_BARBS": 3, "FLASH_FIRE": 3, "WATER_ABSORB": 3, "VOLT_ABSORB": 3,
    "LIGHTNING_ROD": 3, "STORM_DRAIN": 3, "SAP_SIPPER": 3, "MOTOR_DRIVE": 3, "DRY_SKIN": 2, "CLEAR_BODY": 3,
    "MOLD_BREAKER": 3, "UNAWARE": 3, "FILTER": 3, "SOLID_ROCK": 3, "SHED_SKIN": 3, "STATIC": 3, "FLAME_BODY": 3,
    "POISON_POINT": 2, "EFFECT_SPORE": 3, "CUTE_CHARM": 1, "SYNCHRONIZE": 1, "INNER_FOCUS": 2, "TRACE": 3,
    "COMPETITIVE": 3, "DEFIANT": 3, "JUSTIFIED": 2, "SNIPER": 2, "SUPER_LUCK": 2, "HUSTLE": 1, "NO_GUARD": 3,
    "SCRAPPY": 3, "HYDRATION": 1, "RAIN_DISH": 2, "ICE_BODY": 2, "OVERCOAT": 2, "WEAK_ARMOR": 1, "ANALYTIC": 2,
    "SWIFT_SWIM": 1, "CHLOROPHYLL": 1, "SAND_RUSH": 1, "SAND_VEIL": 1, "SNOW_CLOAK": 1, "SAND_FORCE": 1,
    "SOLAR_POWER": 1, "LEAF_GUARD": 1, "TINTED_LENS": 4, "WONDER_SKIN": 3, "OWN_TEMPO": 1, "OBLIVIOUS": 1,
    "KEEN_EYE": 1, "BIG_PECKS": 1, "TANGLED_FEET": 1, "EARLY_BIRD": 1, "SHELL_ARMOR": 1, "BATTLE_ARMOR": 1,
    "ARENA_TRAP": 4, "SHADOW_TAG": 4, "MAGNET_PULL": 1, "UNBURDEN": 2, "GLUTTONY": 1, "AFTERMATH": 2,
    "INFILTRATOR": 2, "PRESSURE": 2, "CURSED_BODY": 2, "STENCH": 1, "SWARM": 1, "BLAZE": 1, "TORRENT": 1,
    "OVERGROW": 1, "QUICK_FEET": 2, "RIVALRY": 0, "RUN_AWAY": 0, "PICKUP": 0, "HONEY_GATHER": 0, "ILLUMINATE": 0,
    "BALL_FETCH": 0, "TRUANT": -10, "SLOW_START": -10, "DEFEATIST": -6, "KLUTZ": -6, "STALL": -4, "NORMALIZE": -4,
    "FOREWARN": 0, "ANTICIPATION": 0, "FRISK": 1, "PICKPOCKET": 1, "HARVEST": 1, "IMMUNITY": 2, "INSOMNIA": 2,
    "VITAL_SPIRIT": 2, "LIMBER": 1, "WATER_VEIL": 1, "MAGMA_ARMOR": 1, "STEADFAST": 2, "SUCTION_CUPS": 1,
    "STICKY_HOLD": 1, "DAMP": 0, "CLOUD_NINE": 1, "AIR_LOCK": 1, "COLOR_CHANGE": 1, "SIMPLE": 2, "CONTRARY": 2,
    "HEAVY_METAL": 0, "LIGHT_METAL": 0, "TELEPATHY": 0, "MUMMY": 1, "IRON_FIST_": 0, "PIXILATE": 6,
    "AERILATE": 6, "REFRIGERATE": 6, "GALE_WINGS": 4, "STANCE_CHANGE": 4, "GOOEY": 2, "STRONG_JAW_": 0,
    "BULLETPROOF": 3, "FUR_COAT": 5, "PARENTAL_BOND": 8, "MOODY": 0, "HEATPROOF": 2, "ROCK_HEAD": 1,
    "HYPER_CUTTER": 1, "WHITE_SMOKE": 1, "PLUS": 1, "MINUS": 1, "FLOWER_GIFT": 1, "FOREWARN_": 0,
}
NEGATIVE_ON_ITEMS = ("KLUTZ",)
ATE = {"PIXILATE": "TYPE_FAIRY", "AERILATE": "TYPE_FLYING", "REFRIGERATE": "TYPE_ICE", "GALVANIZE": "TYPE_ELECTRIC"}
WEATHER_SETTER = {"DROUGHT": "SUN", "DRIZZLE": "RAIN", "SAND_STREAM": "SAND", "SNOW_WARNING": "HAIL"}
WEATHER_USER = {"CHLOROPHYLL": "SUN", "SOLAR_POWER": "SUN", "SWIFT_SWIM": "RAIN", "RAIN_DISH": "RAIN",
                "DRY_SKIN": "RAIN", "HYDRATION": "RAIN", "SAND_RUSH": "SAND", "SAND_VEIL": "SAND", "SAND_FORCE": "SAND",
                "SNOW_CLOAK": "HAIL", "ICE_BODY": "HAIL"}
WEATHER_MOVE = {"MOVE_SUNNY_DAY": "SUN", "MOVE_RAIN_DANCE": "RAIN", "MOVE_SANDSTORM": "SAND", "MOVE_HAIL": "HAIL"}
WEATHER_ROCK = {"SUN": "ITEM_HEAT_ROCK", "RAIN": "ITEM_DAMP_ROCK", "SAND": "ITEM_SMOOTH_ROCK", "HAIL": "ITEM_ICY_ROCK"}
SPECIES_ITEM = {"SPECIES_PIKACHU": "ITEM_LIGHT_BALL", "SPECIES_MAROWAK": "ITEM_THICK_CLUB",
                "SPECIES_FARFETCHD": "ITEM_LEEK", "SPECIES_CHANSEY": "ITEM_LUCKY_PUNCH"}
ATTACKS_ONLY = ("ITEM_CHOICE_BAND", "ITEM_CHOICE_SPECS", "ITEM_CHOICE_SCARF", "ITEM_ASSAULT_VEST")
SIMPLE_BERRIES = ("ITEM_CHERI_BERRY", "ITEM_CHESTO_BERRY", "ITEM_PECHA_BERRY", "ITEM_RAWST_BERRY", "ITEM_ASPEAR_BERRY",
                  "ITEM_PERSIM_BERRY")


class Mon:
    def __init__(self, species, species_id, level):
        self.species = species
        self.species_id = species_id
        self.level = level
        self.item = None
        self.ability = None
        self.nature = None
        self.ivs = [0] * 6
        self.evs = [0] * 6
        self.moves = []
        self.mega = None
        self.notes = []

    def __repr__(self):
        return "%s@%s" % (self.species, self.item)


class Trainer:
    def __init__(self, tid, name, trainer_class, pic, gender, music, ai, mons, class_key=None):
        self.id = tid
        self.name = name
        self.trainer_class = trainer_class
        self.pic = pic
        self.gender = gender
        self.music = music
        self.ai = ai
        self.mons = mons
        self.class_key = class_key


class Builder:
    def __init__(self, game, movedex, typechart):
        self.g = game
        self.md = movedex
        self.tc = typechart
        self.natures = {v: k for k, v in game.natures.items()}            # id -> NATURE_X
        self.nature_ids = dict(game.natures)
        self.mega_hold = game.hold_values["HOLD_EFFECT_MEGA_STONE"]
        self.type_power = game.hold_values["HOLD_EFFECT_TYPE_POWER"]
        self._eligible_cache = {}
        self._score_cache = {}
        T = game.type_values
        self.T = T
        self.type_booster = {}
        for iid, it in sorted(game.items.items()):
            const = game.item_const[iid]
            if it["holdEffect"] == self.type_power and const in rules.allowed_items(20):
                self.type_booster.setdefault(it["secondaryId"], const)

    # ----- species -----------------------------------------------------------------------------------------------
    def species_problem(self, sid, floor, legendary_ok=False):
        """Why the species may not be used on the floor (None when it may)."""
        g = self.g
        if sid not in g.species:
            return "not enabled in this build"
        s = g.species[sid]
        if s.dex > 721:
            return "not Gen 1-6"
        if s.id != s.dex:
            return "a form, not the base species"
        if not s.all_pics:
            return "placeholder picture"
        if s.legendary and not legendary_ok:
            return "legendary/mythical/Ultra Beast/paradox"
        if g.battle_form_changes(sid):
            return "changes form in battle"
        if g.min_level(sid) > floor:
            return "not legal below level %d" % g.min_level(sid)
        if not any(self.md.is_damaging(m) for m in self.eligible_moves(sid, floor)):
            return "no damaging move at this level"
        return None

    def best_stage(self, entry_sid, floor, excluded=()):
        """The most evolved stage of the entry's line that may be used on the floor (None if none)."""
        for sid in reversed(self.g.chain(entry_sid)):
            if self.g.species[sid].const in excluded:
                continue
            if self.species_problem(sid, floor) is None:
                return sid
        return None

    def mega_options(self, sid):
        """[(mega species id, stone item const)] usable by this species: the form exists with real pictures and the
        stone is a Mega Stone of this build."""
        out = []
        for mega_sid, stone in self.g.mega_forms(sid):
            mega = self.g.species.get(mega_sid)
            item = self.g.items.get(stone)
            if mega is None or not mega.all_pics or not mega.flags["mega"]:
                continue
            if item is None or item["holdEffect"] != self.mega_hold:
                continue
            out.append((mega_sid, self.g.item_const[stone]))
        return out

    def battle_form(self, sid, item_const):
        for mega_sid, stone in self.mega_options(sid):
            if stone == item_const:
                return mega_sid
        return sid

    # ----- moves -------------------------------------------------------------------------------------------------
    def eligible_moves(self, sid, floor):
        key = (sid, floor)
        if key not in self._eligible_cache:
            moves = sorted(m for m in self.g.learnable(sid, floor) if self.md.ban_reason(m, floor) is None)
            self._eligible_cache[key] = moves
        return self._eligible_cache[key]

    def move_type(self, mid, ability):
        t = self.g.moves[mid]["type"]
        ab = self.g.ability_const.get(ability, "")[8:] if isinstance(ability, int) else (ability or "")[8:]
        if ab in ATE and t == self.T["TYPE_NORMAL"] and self.md.is_damaging(mid):
            return self.T[ATE[ab]]
        return t

    def is_stab(self, mid, types, ability):
        return self.md.is_damaging(mid) and self.move_type(mid, ability) in types

    def form_info(self, form_sid, ability_const):
        s = self.g.species[form_sid]
        st = {k: s.stats[i] for k, i in DUMP_INDEX.items()}
        ab = (ability_const or "")[8:]
        atk = st["Atk"] * (2 if ab in ("HUGE_POWER", "PURE_POWER") else 1)
        return {"sid": form_sid, "types": s.types, "stats": st, "atk": atk, "spa": st["SpA"], "ability": ability_const,
                "ab": ab, "side": None}

    def preferred_side(self, sid, floor, fi):
        """'P' or 'S': the attacking side of this battle form - its stats when they differ by 10%, else the side of
        its best available attacks."""
        if fi["atk"] * 10 >= fi["spa"] * 11:
            return "P"
        if fi["spa"] * 10 >= fi["atk"] * 11:
            return "S"
        best = {1: [0.0], 2: [0.0]}
        probe = dict(fi, side=None)
        for m in self.eligible_moves(sid, floor):
            if self.md.is_damaging(m):
                cat = self.g.moves[m]["category"]
                if cat in best:
                    best[cat].append(self.damage_score(m, probe, floor))
        top = {c: sum(sorted(v, reverse=True)[:2]) for c, v in best.items()}
        return "P" if top[1] >= top[2] else "S"

    def damage_score(self, mid, fi, level, item=None):
        """Expected damage-like value of a damaging move for this battle form (no type matchups)."""
        g, md = self.g, self.md
        m = g.moves[mid]
        ab = fi["ab"]
        p = md.base_power(mid, level, fi["stats"]["Spe"], skill_link=(ab == "SKILL_LINK"))
        p *= md.multiplier(mid, has_power_herb=(item == "ITEM_POWER_HERB"), rock_head=(ab in ("ROCK_HEAD",)))
        mtype = self.move_type(mid, fi["ability"])
        fixed = m["effect"] in (md.e_fixed_hp, md.e_level)
        top = max(fi["atk"], fi["spa"], 1)
        if m["effect"] == md.e_level:
            p = 85.0 * 100.0 / top
        if not fixed:
            if mtype in fi["types"]:
                p *= 2.0 if ab == "ADAPTABILITY" else 1.5
            elif mtype == self.T["TYPE_NORMAL"]:
                p *= 0.8                                   # Normal coverage hits nothing super effectively
            if ab in ATE and m["type"] == self.T["TYPE_NORMAL"]:
                p *= 1.2
            if ab == "TECHNICIAN" and m["power"] <= 60:
                p *= 1.5
            if ab == "TOUGH_CLAWS" and m["contact"]:
                p *= 1.3
            if ab == "IRON_FIST" and mid in md.punch:
                p *= 1.2
            if ab == "STRONG_JAW" and mid in md.bite:
                p *= 1.5
            if ab == "MEGA_LAUNCHER" and mid in md.pulse:
                p *= 1.5
            if ab == "RECKLESS" and m["effect"] in (md.e_recoil, md.e_recoil_miss):
                p *= 1.2
            if ab == "SHEER_FORCE" and mid in md.secondary:
                p *= 1.3
            if ab == "PARENTAL_BOND":
                p *= 1.25
            if m["effect"] == md.e_facade and (item in ("ITEM_FLAME_ORB", "ITEM_TOXIC_ORB") or ab == "GUTS"):
                p *= 2.0 if item in ("ITEM_FLAME_ORB", "ITEM_TOXIC_ORB") else 1.2
            if m["effect"] == md.e_body_press:
                stat = fi["stats"]["Def"]
            elif m["effect"] == md.e_foul_play:
                stat = 100
            elif m["category"] == 1:
                stat = fi["atk"]
            else:
                stat = fi["spa"]
            p *= (stat / top) ** 1.3
            side = fi.get("side")
            if side and m["effect"] not in (md.e_body_press, md.e_foul_play) and \
                    (m["category"] == 1) != (side == "P"):
                p *= 0.7                                  # off the set's attacking side
        bonus = md.secondary.get(mid, 0)
        if m["priority"] > 0 and m["effect"] != md.e_first_turn:
            bonus += 6
        return p + bonus

    def coverage(self, picks):
        """How well the damaging moves (mid, type, score) hit every single type (best move per defending type)."""
        total = 0.0
        for t in self.tc.battle_types:
            best = 0.0
            for _, mtype, sc in picks:
                if isinstance(mtype, tuple):              # fixed damage: type matchups only decide immunity
                    v = sc if self.tc.eff(mtype[1], [t]) > 0 else 0.0
                else:
                    v = sc * self.tc.eff(mtype, [t])
                if v > best:
                    best = v
            total += 60.0 * math.log1p(best / 60.0)
        return total / len(self.tc.battle_types)

    def status_value(self, mid, fi, role, floor, chosen, item):
        """How much the status move helps this Pokemon (0: never)."""
        g = self.g
        r, v = self.md.status_roles[mid]
        const = g.move_const[mid]
        st = fi["stats"]
        physical = (fi["side"] == "P") if fi.get("side") else fi["atk"] >= fi["spa"]
        bulky = st["HP"] + (st["Def"] + st["SpD"]) / 2 >= 170
        if r == "setup_phys":
            if not physical:
                return 0
            if const == "MOVE_CURSE" and self.T["TYPE_GHOST"] in fi["types"]:
                return 0
            return v * (1.2 if role in ("setup", "sweeper") else 0.8)
        if r == "setup_spec":
            if physical:
                return 0
            return v * (1.2 if role in ("setup", "sweeper") else 0.8)
        if r == "setup_both":
            return v * (1.1 if role in ("setup", "sweeper") else 0.7)
        if r == "setup_speed":
            return v * (1.0 if st["Spe"] < 100 and role in ("setup", "sweeper") else 0.5)
        if r == "setup_def":
            return v * (1.0 if bulky else 0.5)
        if r == "recover":
            if const == "MOVE_REST":
                return v
            return v * (1.2 if bulky or role == "wall" else 0.7)
        if r == "status":
            if const == "MOVE_WILL_O_WISP" and self.T["TYPE_FIRE"] in fi["types"]:
                return v * 1.0
            return v * (1.1 if role in ("wall", "support") else 0.8)
        if r == "hazard":
            return v * (1.2 if role in ("wall", "support") else 0.6)
        if r == "screen":
            return v * (1.0 if role == "support" else 0.5)
        if r == "protect":
            return v * (1.2 if fi["ab"] in ("SPEED_BOOST", "POISON_HEAL") else 0.6)
        if r == "weather":
            w = WEATHER_MOVE.get(const)
            if WEATHER_USER.get(fi["ab"]) == w:
                return v * 1.6
            if w == "RAIN" and self.T["TYPE_WATER"] in fi["types"]:
                return v * 0.8
            if w == "SUN" and self.T["TYPE_FIRE"] in fi["types"]:
                return v * 0.8
            return v * 0.3
        if r == "phaze":
            return v * (1.0 if role == "wall" else 0.4)
        if r == "utility":
            if const in ("MOVE_TRICK", "MOVE_SWITCHEROO"):
                return v if item in ("ITEM_CHOICE_SCARF", "ITEM_CHOICE_SPECS", "ITEM_CHOICE_BAND") else 0
            if const == "MOVE_SLEEP_TALK":
                return 6 if self.g.move("MOVE_REST") in chosen else 0
            return v * (0.9 if role in ("support", "wall") else 0.6)
        if r in ("debuff", "accuracy"):
            return v * (1.0 if floor < 20 else 0.4)
        return 0

    def select_moves(self, sid, floor, fi, rng, role, util_slots, item=None, forced=()):
        """Up to four moves: STAB first, then type coverage, then the role's utility, then the best of the rest."""
        g = self.g
        eligible = self.eligible_moves(sid, floor)
        dmg = [m for m in eligible if self.md.is_damaging(m)]
        status = [m for m in eligible if not self.md.is_damaging(m)]
        scored = {m: self.damage_score(m, fi, floor, item) for m in dmg}
        chosen = [g.move(c) for c in forced]
        if item in ATTACKS_ONLY and len(dmg) >= 4:
            status = []
            eligible = dmg
            util_slots = 0
        if len(eligible) <= 4:
            rest = sorted([m for m in dmg if m not in chosen], key=lambda m: (-scored[m], m))
            rest += sorted([m for m in status if m not in chosen],
                           key=lambda m: (-self.status_value(m, fi, role, floor, chosen, item), m))
            return (chosen + rest)[:4]

        def pick_tuple(m):
            fixed = g.moves[m]["effect"] in (self.md.e_fixed_hp, self.md.e_level)
            return (m, ("fixed", self.move_type(m, fi["ability"])) if fixed else self.move_type(m, fi["ability"]),
                    scored[m])

        attacks = [m for m in chosen if m in scored]
        if not attacks:
            stab = [m for m in dmg if self.is_stab(m, fi["types"], fi["ability"])]
            pool = stab or dmg
            first = max(pool, key=lambda m: (scored[m], -m))
            chosen.append(first)
            attacks.append(first)
        n_attacks = max(1, 4 - util_slots)
        while len([m for m in chosen if m in scored]) < n_attacks:
            current = [pick_tuple(x) for x in chosen if x in scored]
            base = self.coverage(current)
            best, best_gain = None, 0.0
            for m in dmg:
                if m in chosen or self._clashes(m, chosen):
                    continue
                gain = self.coverage(current + [pick_tuple(m)]) - base + 0.04 * scored[m]
                mt = pick_tuple(m)[1]
                if any(t == mt for _, t, _ in current):
                    gain *= 0.3                           # a second move of a type the set already has
                if best is None or gain > best_gain or (gain == best_gain and m < best):
                    best, best_gain = m, gain
            if best is None:
                break
            # a fourth attack that adds next to nothing gives way to a good status move
            if (floor >= 20 and len(chosen) >= 3 and best_gain < 4 and
                    self._best_status(status, fi, role, floor, chosen, item) >= 5):
                break
            chosen.append(best)
        # utility and setup moves: one per role, worth at least `threshold` while attacks remain
        threshold = 4 if floor >= 20 else 10 ** 9
        roles_used = {self.md.status_roles[x][0] for x in chosen if x in self.md.status_roles}
        while len(chosen) < 4:
            cands = [m for m in status if m not in chosen and self.md.status_roles[m][0] not in roles_used]
            if not cands:
                break
            vals = {m: self.status_value(m, fi, role, floor, chosen, item) for m in cands}
            m = max(cands, key=lambda x: (vals[x], -x))
            if vals[m] <= 0:
                break
            if [x for x in dmg if x not in chosen] and vals[m] < threshold:
                break
            chosen.append(m)
            roles_used.add(self.md.status_roles[m][0])
        # fill with the best remaining attacks, then the most useful status moves
        if len(chosen) < 4:
            rest = sorted([m for m in dmg if m not in chosen], key=lambda m: (self._clashes(m, chosen), -scored[m], m))
            chosen += rest[:4 - len(chosen)]
        if len(chosen) < 4:
            rest = [m for m in eligible if m not in chosen and not self.md.is_damaging(m)]
            rest.sort(key=lambda m: (-self.status_value(m, fi, role, floor, chosen, item), m))
            chosen += rest[:4 - len(chosen)]
        return chosen[:4]

    def _clashes(self, mid, chosen):
        """A second rampage, recharge or charge-turn move (one of each kind is enough)."""
        md = self.md
        kinds = []
        for x in chosen:
            kinds.append(self._lock_kind(x))
        k = self._lock_kind(mid)
        return k is not None and k in kinds

    def _lock_kind(self, mid):
        md = self.md
        m = self.g.moves[mid]
        if mid in md.rampage:
            return "rampage"
        if mid in md.recharge:
            return "recharge"
        if m["effect"] in md.e_two_turn or m["effect"] == md.e_semi:
            return "charge"
        if mid in md.self_drop:
            return "drop"
        return None

    def _best_status(self, status, fi, role, floor, chosen, item):
        vals = [self.status_value(m, fi, role, floor, chosen, item) for m in status if m not in chosen]
        return max(vals) if vals else 0

    # ----- roles, natures, EVs, IVs ------------------------------------------------------------------------------
    def role_of(self, fi):
        st = fi["stats"]
        off = max(fi["atk"], fi["spa"])
        bulk = st["HP"] + (st["Def"] + st["SpD"]) / 2
        if bulk >= 185 and off < 100:
            return "wall"
        if st["Spe"] >= 90 and off >= 85:
            return "sweeper"
        if off >= 88:
            return "attacker"
        if bulk >= 165:
            return "wall"
        return "support" if off < 75 else "attacker"

    def attack_side(self, mon, fi):
        """'P' or 'S': the attacking stat the chosen moves lean on (each move weighted by its power times the stat
        it uses)."""
        phys = spec = 0.0
        for c in mon.moves:
            mid = self.g.move(c)
            m = self.g.moves[mid]
            if not self.md.is_damaging(mid) or m["effect"] in (self.md.e_fixed_hp, self.md.e_level):
                continue
            w = self.md.base_power(mid, mon.level, fi["stats"]["Spe"])
            if m["category"] == 1 and m["effect"] not in (self.md.e_body_press, self.md.e_foul_play):
                phys += w * fi["atk"]
            elif m["category"] == 2:
                spec += w * fi["spa"]
        if phys == spec:
            if fi.get("side"):
                return fi["side"]
            return "P" if fi["atk"] >= fi["spa"] else "S"
        return "P" if phys > spec else "S"

    def mixed(self, mon):
        """Whether the set attacks with both Attack and Sp. Attack (fixed-damage moves, Body Press and Foul Play use
        neither)."""
        cats = set()
        for c in mon.moves:
            mid = self.g.move(c)
            m = self.g.moves[mid]
            if not self.md.is_damaging(mid) or m["effect"] in (self.md.e_fixed_hp, self.md.e_level,
                                                                self.md.e_body_press, self.md.e_foul_play):
                continue
            cats.add(m["category"])
        return len(cats) == 2

    def lowered_stat(self, mon, fi, side, speedy, keep=()):
        """The stat the nature lowers: the attacking stat the set does not use; for a set that attacks with both, its
        Speed (a slow Pokemon) or its weaker defense (one not in keep)."""
        unused = "SpA" if side == "P" else "Atk"
        if not self.mixed(mon):
            return unused
        if not speedy:
            return "Spe"
        st = fi["stats"]
        defs = [d for d in ("Def", "SpD") if d not in keep]
        return min(defs, key=lambda d: (st[d], d)) if defs else unused

    def nature_for(self, raised, lowered):
        return self.natures[5 * NATURE_STATS.index(raised) + NATURE_STATS.index(lowered)]

    def nature_effect(self, nature_const):
        n = self.nature_ids[nature_const]
        up, down = NATURE_STATS[n // 5], NATURE_STATS[n % 5]
        return (None, None) if up == down else (up, down)

    def spread(self, mon, fi, side, floor, rng):
        """EVs (Showdown order) and nature by the floor's rules and the Pokemon's role."""
        st = fi["stats"]
        atk_stat = "Atk" if side == "P" else "SpA"
        moves = [self.g.move(c) for c in mon.moves]
        statuses = [m for m in moves if not self.md.is_damaging(m)]
        wall = self.role_of(fi) == "wall" and len(statuses) >= 2
        fast = st["Spe"] >= 70
        mixed = self.mixed(mon)
        evs = {k: 0 for k in STATS_SD}
        per, total, minimum = rules.ev_limits(floor)
        if floor <= 19:
            nature = self.natures[rng.randrange(25)]
            return [0] * 6, nature
        if floor >= 40 or minimum:
            if wall:
                hp_def = "Def" if st["Def"] <= st["SpD"] else "SpD"
                if fi["ab"] in ("INTIMIDATE",) or side == "S":
                    hp_def = "SpD" if st["SpD"] < st["Def"] else "Def"
                evs["HP"], evs[hp_def] = 252, 252
                other = "SpD" if hp_def == "Def" else "Def"
                evs[other] = 4
                nature = self.nature_for(hp_def, self.lowered_stat(mon, fi, side, fast, keep=(hp_def,)))
            elif fast or st["Spe"] >= 60 and st["HP"] < 80:
                evs[atk_stat], evs["Spe"], evs["HP"] = 252, 252, 4
                low = self.lowered_stat(mon, fi, side, True)
                nature = self.nature_for("Spe", low) if (st["Spe"] >= 85 or not rules.optimized(floor)
                                                        and rng.random() < 0.5) else self.nature_for(atk_stat, low)
            else:
                evs["HP"], evs[atk_stat] = 252, 252
                evs["SpD" if st["SpD"] <= st["Def"] else "Def"] = 4
                nature = self.nature_for(atk_stat, self.lowered_stat(mon, fi, side, False))
            return [evs[k] for k in STATS_SD], nature
        # floors 20-39: light EVs (bosses: their best stat maxed)
        strong = rules.strong(floor)
        main = 252 if strong else min(128, 32 + (floor - 20) * 6 // 4 * 4)
        second = 128 if strong else min(128, max(0, (floor - 20) * 4 // 4 * 4))
        evs[atk_stat] = main - main % 4
        evs["Spe" if fast else "HP"] = second - second % 4
        # a nature that never lowers the attacking stat (either one for a set that uses both; Speed for a fast Pokemon)
        options = []
        for n in range(25):
            up, down = NATURE_STATS[n // 5], NATURE_STATS[n % 5]
            if down == atk_stat or (fast and down == "Spe") or (mixed and down in ("Atk", "SpA")):
                continue
            options.append(n)
        if strong:
            nature = self.nature_for(atk_stat, self.lowered_stat(mon, fi, side, fast))
        else:
            nature = self.natures[options[rng.randrange(len(options))]]
        return [evs[k] for k in STATS_SD], nature

    def ivs_for(self, floor, rng):
        lo, hi = rules.iv_range(floor)
        return [lo if lo == hi else rng.randint(lo, hi) for _ in STATS_SD]

    # ----- abilities and items -----------------------------------------------------------------------------------
    def ability_options(self, sid, floor):
        a = self.g.species[sid].abilities
        opts = [x for x in (a[0], a[1]) if x]
        if floor >= 50 and a[2] and a[2] not in opts:
            opts.append(a[2])
        out = []
        for x in opts:
            if x not in out:
                out.append(x)
        return [self.g.ability_const[x] for x in out]

    def ability_score(self, const, mon_moves, item):
        ab = const[8:]
        s = ABILITY_SCORES.get(ab, 2)
        mids = [self.g.move(c) for c in mon_moves]
        dm = [m for m in mids if self.md.is_damaging(m)]
        if ab == "TECHNICIAN":
            s += 2 * sum(1 for m in dm if self.g.moves[m]["power"] <= 60)
        if ab == "SKILL_LINK":
            s += 4 * sum(1 for m in dm if self.g.moves[m]["multiHit"])
        if ab == "SHEER_FORCE":
            s += 1.5 * sum(1 for m in dm if m in self.md.secondary)
        if ab == "IRON_FIST":
            s += 2 * sum(1 for m in dm if m in self.md.punch)
        if ab == "STRONG_JAW":
            s += 2 * sum(1 for m in dm if m in self.md.bite)
        if ab == "RECKLESS":
            s += 2 * sum(1 for m in dm if self.g.moves[m]["effect"] in (self.md.e_recoil, self.md.e_recoil_miss))
        if ab == "ROCK_HEAD":
            s += 2 * sum(1 for m in dm if self.g.moves[m]["effect"] == self.md.e_recoil)
        if ab == "GUTS" and item in ("ITEM_FLAME_ORB", "ITEM_TOXIC_ORB"):
            s += 6
        if ab == "POISON_HEAL" and item == "ITEM_TOXIC_ORB":
            s += 8
        if ab in WEATHER_USER:
            s += 0
        if ab in NEGATIVE_ON_ITEMS and item:
            s -= 4
        return s

    def item_candidates(self, mon, fi, side, floor, role, archetype):
        """Held items for the Pokemon, best first (the floor's rules filter them afterwards)."""
        g = self.g
        types = fi["types"]
        moves = [g.move(c) for c in mon.moves]
        dmg = [m for m in moves if self.md.is_damaging(m)]
        status = [m for m in moves if not self.md.is_damaging(m)]
        st = fi["stats"]
        best_move = max(dmg, key=lambda m: self.damage_score(m, fi, floor)) if dmg else None
        booster = self.type_booster.get(self.move_type(best_move, fi["ability"])) if best_move else None
        poison = self.T["TYPE_POISON"] in self.g.species[mon.species_id].types
        frail = st["HP"] + (st["Def"] + st["SpD"]) / 2 < 140
        out = []
        sp_item = SPECIES_ITEM.get(mon.species)
        if sp_item:
            out.append(sp_item)
        if floor >= 50:
            ab = fi["ab"]
            if ab == "POISON_HEAL":
                out.append("ITEM_TOXIC_ORB")
            if ab == "GUTS" and any(g.moves[m]["category"] == 1 for m in dmg):
                out.append("ITEM_FLAME_ORB")
            if g.can_evolve(mon.species_id):
                out.append("ITEM_EVIOLITE")
            if archetype == "choice":
                fast_enough = 70 <= st["Spe"] <= 105
                if side == "P":
                    out += ["ITEM_CHOICE_SCARF", "ITEM_CHOICE_BAND"] if fast_enough else ["ITEM_CHOICE_BAND",
                                                                                         "ITEM_CHOICE_SCARF"]
                else:
                    out += ["ITEM_CHOICE_SCARF", "ITEM_CHOICE_SPECS"] if fast_enough else ["ITEM_CHOICE_SPECS",
                                                                                          "ITEM_CHOICE_SCARF"]
            if archetype == "wall" or role == "wall":
                if poison:
                    out.append("ITEM_BLACK_SLUDGE")
                out.append("ITEM_LEFTOVERS")
                if any(g.move_const[m] in ("MOVE_REFLECT", "MOVE_LIGHT_SCREEN") for m in status):
                    out.append("ITEM_LIGHT_CLAY")
                out += ["ITEM_ROCKY_HELMET", "ITEM_SITRUS_BERRY"]
            if archetype == "setup":
                if any(g.move_const[m] == "MOVE_SHELL_SMASH" for m in status):
                    out.append("ITEM_WHITE_HERB")
                out += (["ITEM_WEAKNESS_POLICY", "ITEM_LUM_BERRY", "ITEM_LIFE_ORB"] if not frail
                        else ["ITEM_LIFE_ORB", "ITEM_LUM_BERRY", "ITEM_FOCUS_SASH"])
            if not status and len(dmg) == 4 and st["SpD"] >= 70:
                out.append("ITEM_ASSAULT_VEST")
            if frail and st["Spe"] >= 90:
                out.append("ITEM_FOCUS_SASH")
            out += ["ITEM_LIFE_ORB", "ITEM_EXPERT_BELT"]
            out.append("ITEM_MUSCLE_BAND" if side == "P" else "ITEM_WISE_GLASSES")
            if booster:
                out.append(booster)
            if poison:
                out.append("ITEM_BLACK_SLUDGE")
            out += ["ITEM_LEFTOVERS", "ITEM_SITRUS_BERRY", "ITEM_LUM_BERRY", "ITEM_SHELL_BELL", "ITEM_SCOPE_LENS",
                    "ITEM_ROCKY_HELMET", "ITEM_QUICK_CLAW", "ITEM_KINGS_ROCK", "ITEM_FOCUS_BAND", "ITEM_ZOOM_LENS",
                    "ITEM_WIDE_LENS"]
            weak = self._weakest_to(fi)
            if weak is not None:
                out.append(weak)
            out += sorted(set(self.type_booster.values()))
            return out
        if floor >= 30:
            if role == "wall" or archetype == "wall":
                out += ["ITEM_LEFTOVERS", "ITEM_SITRUS_BERRY", "ITEM_LUM_BERRY", "ITEM_SHELL_BELL"]
            if st["Spe"] < 60 and role != "wall":
                out.append("ITEM_QUICK_CLAW")
            if any(g.moves[m]["multiHit"] or g.moves[m]["strikes"] > 1 for m in dmg):
                out.append("ITEM_KINGS_ROCK")
            if booster:
                out.append(booster)
            out += ["ITEM_SHELL_BELL", "ITEM_SCOPE_LENS", "ITEM_LEFTOVERS", "ITEM_LUM_BERRY", "ITEM_SITRUS_BERRY",
                    "ITEM_KINGS_ROCK", "ITEM_QUICK_CLAW", "ITEM_FOCUS_BAND"]
            out += sorted(set(self.type_booster.values()))
            out += list(SIMPLE_BERRIES) + ["ITEM_ORAN_BERRY"]
            return out
        if floor >= 20:
            if booster:
                out.append(booster)
            out += ["ITEM_SITRUS_BERRY"]
            out += sorted(set(self.type_booster.values()))
            out += ["ITEM_ORAN_BERRY"] + list(SIMPLE_BERRIES)
            return out
        return ["ITEM_ORAN_BERRY"] + list(SIMPLE_BERRIES)

    def _weakest_to(self, fi):
        """A resist berry for the type that hits the battle form hardest."""
        worst, worst_eff = None, 1.0
        for t in self.tc.battle_types:
            e = self.tc.eff(t, fi["types"])
            if e > worst_eff:
                worst, worst_eff = t, e
        if worst is None:
            return None
        for iid, it in sorted(self.g.items.items()):
            if it["holdEffect"] == self.g.hold_values["HOLD_EFFECT_RESIST_BERRY"] and it["holdParam"] == worst:
                const = self.g.item_const[iid]
                if const in rules.allowed_items(50):
                    return const
        return None

    def choose_item(self, mon, fi, side, floor, role, archetype, used, rng):
        allowed = rules.allowed_items(floor)
        for c in self.item_candidates(mon, fi, side, floor, role, archetype):
            if c not in allowed or (floor >= 20 and c in used):
                continue
            if c == "ITEM_EVIOLITE" and not self.g.can_evolve(mon.species_id):
                continue
            if c == "ITEM_BLACK_SLUDGE" and self.T["TYPE_POISON"] not in self.g.species[mon.species_id].types:
                continue
            if c in ("ITEM_CHOICE_BAND", "ITEM_CHOICE_SPECS", "ITEM_CHOICE_SCARF", "ITEM_ASSAULT_VEST"):
                if any(not self.md.is_damaging(self.g.move(x)) for x in mon.moves):
                    continue
            if c in SPECIES_ITEM.values() and SPECIES_ITEM.get(mon.species) != c:
                continue
            if c == "ITEM_LIGHT_CLAY" and not any(x in ("MOVE_REFLECT", "MOVE_LIGHT_SCREEN") for x in mon.moves):
                continue
            if c in ("ITEM_FLAME_ORB", "ITEM_TOXIC_ORB") and fi["ab"] not in ("GUTS", "POISON_HEAL", "MARVEL_SCALE"):
                continue
            if c == "ITEM_WHITE_HERB" and "MOVE_SHELL_SMASH" not in mon.moves:
                continue
            if c == "ITEM_WEAKNESS_POLICY" and fi["stats"]["HP"] + fi["stats"]["Def"] < 160:
                continue
            return c
        return None

    # ----- one Pokemon -------------------------------------------------------------------------------------------
    def build_mon(self, spec, floor, rng, used_items, holds_item=True, archetype=None):
        """A Pokemon for the floor: spec gives at least 'species'; 'item', 'ability', 'nature', 'evs' (dict),
        'ivs' (dict), 'moves' and 'mega' (True: its Mega Stone) are kept when given."""
        g = self.g
        sp = g.sp(spec["species"])
        sid = sp.id
        mon = Mon(sp.const, sid, floor)
        item = spec.get("item")
        if spec.get("mega"):
            opts = self.mega_options(sid)
            want = spec.get("stone")
            choice = [o for o in opts if want is None or o[1] == want]
            if not choice:
                raise ValueError("%s has no Mega Stone %s" % (sp.const, want))
            item = choice[0][1]
        form = self.battle_form(sid, item)
        if form != sid:
            mon.mega = g.species[form].const
        abilities = [spec["ability"]] if spec.get("ability") else self.ability_options(sid, floor)
        mega_ability = g.ability_const[g.species[form].abilities[0]] if form != sid else None
        # choose moves per candidate Ability, keep the best combination
        best = None
        for ab in abilities:
            fi = self.form_info(form, mega_ability or ab)
            fi["side"] = self.preferred_side(sid, floor, fi)
            role = self.role_of(fi)
            arch = archetype or ("choice" if item in ATTACKS_ONLY else self._archetype(sid, floor, fi, role, rng, item))
            util = self._util_slots(floor, role, arch, rng)
            if spec.get("moves"):
                moves = [g.move(c) for c in spec["moves"]]
            else:
                plan = item or self._planned_item(fi, floor)
                if floor >= 30 and arch != "choice":
                    top = self._best_status([m for m in self.eligible_moves(sid, floor) if not self.md.is_damaging(m)],
                                            fi, role if arch != "setup" else "setup", floor, [], plan)
                    if top >= 8:
                        util = max(util, 1)
                moves = self.select_moves(sid, floor, fi, rng, role if arch != "setup" else "setup", util, plan,
                                          forced=spec.get("forced_moves", ()))
            consts = [g.move_const[m] for m in moves]
            val = self.ability_score(ab, consts, item) + sum(
                self.damage_score(m, fi, floor) for m in moves if self.md.is_damaging(m)) / 40.0
            if best is None or val > best[0]:
                best = (val, ab, consts, fi, role, arch)
        _, ab, consts, fi, role, arch = best
        mon.ability = ab
        mon.moves = consts
        side = self.attack_side(mon, fi)
        if item is None and holds_item and floor >= 5 and not spec.get("no_item"):
            item = self.choose_item(mon, fi, side, floor, role, arch, used_items, rng)
        mon.item = item
        if item:
            used_items.add(item)
        evs, nature = self.spread(mon, fi, side, floor, rng)
        if spec.get("evs") is not None:
            evs = [spec["evs"].get(k, 0) for k in STATS_SD]
        if spec.get("nature"):
            nature = spec["nature"]
        mon.evs = evs
        mon.nature = nature
        ivs = self.ivs_for(floor, rng)
        if spec.get("ivs") is not None:
            ivs = [spec["ivs"].get(k, ivs[i]) for i, k in enumerate(STATS_SD)]
        mon.ivs = ivs
        return mon

    def _planned_item(self, fi, floor):
        """An item the set is built around (the moves see it before the item is picked)."""
        if floor >= 50 and fi["ab"] == "POISON_HEAL":
            return "ITEM_TOXIC_ORB"
        if floor >= 50 and fi["ab"] == "GUTS" and fi["atk"] >= fi["spa"]:
            return "ITEM_FLAME_ORB"
        return None

    def _archetype(self, sid, floor, fi, role, rng, item):
        if floor < 50:
            return "wall" if role == "wall" else "attacker"
        if role == "wall":
            return "wall"
        eligible = self.eligible_moves(sid, floor)
        physical = fi["side"] == "P" if fi.get("side") else fi["atk"] >= fi["spa"]
        setups = [m for m in eligible if m in self.md.status_roles and self.md.status_roles[m][0] in
                  (("setup_phys", "setup_both") if physical else ("setup_spec", "setup_both"))
                  and self.md.status_roles[m][1] >= 7]
        if setups and rng.random() < 0.55:
            return "setup"
        if item is None and max(fi["atk"], fi["spa"]) >= 100 and rng.random() < 0.3:
            return "choice"
        return "attacker"

    def _util_slots(self, floor, role, arch, rng):
        if floor < 20:
            return 0
        if floor < 30:
            return 1 if rng.random() < 0.4 else 0
        if floor < 50:
            return 2 if role == "wall" else (1 if rng.random() < 0.6 else 0)
        if arch == "choice":
            return 0
        if arch == "wall":
            return 2
        if arch == "setup":
            return 1
        return 1 if rng.random() < 0.5 else 0

    # ----- strength ----------------------------------------------------------------------------------------------
    def strength(self, mon):
        form = self.g.species_by_const[mon.mega] if mon.mega else self.g.species[mon.species_id]
        return form.bst

    # The team strength the floors aim at (a species' BST, or its Mega's): basic Pokemon low, final forms high.
    TARGET_BST = ((1, 270), (10, 320), (20, 380), (30, 430), (40, 465), (50, 490), (60, 510), (70, 525), (80, 535),
                  (90, 545), (100, 560))

    def target_bst(self, floor):
        pts = self.TARGET_BST
        for (f0, b0), (f1, b1) in zip(pts, pts[1:]):
            if f0 <= floor <= f1:
                return b0 + (b1 - b0) * (floor - f0) / float(f1 - f0)
        return pts[-1][1]

    # ----- a generated trainer's team ----------------------------------------------------------------------------
    def candidates(self, pool, floor, excluded):
        out = []
        for entry in pool:
            sid = self.best_stage(self.g.sp(entry).id, floor, excluded)
            if sid is not None and sid not in out:
                out.append(sid)
        return out

    def weighted_pick(self, cands, floor, rng, k, lift=0):
        """k distinct species, those near the floor's target strength (plus lift) most likely."""
        target = self.target_bst(floor) + lift
        pool = list(cands)
        out = []
        while pool and len(out) < k:
            weights = [math.exp(-((self.g.species[s].bst - target) / 55.0) ** 2) + 0.002 for s in pool]
            total = sum(weights)
            r = rng.random() * total
            acc = 0.0
            pick = pool[-1]
            for s, w in zip(pool, weights):
                acc += w
                if r <= acc:
                    pick = s
                    break
            out.append(pick)
            pool.remove(pick)
        return out

    def _ace_pool(self, cands, recent_aces, need_mega):
        """The ace's candidates: never the previous floor's ace (recent_aces[-1]), and none of the aces of the floors
        before it either while the pool allows (the oldest is given up first)."""
        avoid = list(recent_aces)
        while True:
            pool = [s for s in cands if s not in avoid and (not need_mega or self.mega_options(s))]
            if pool or len(avoid) <= 1:
                return pool
            avoid = avoid[1:]

    def generate_team(self, pool, floor, size, rng, mode, recent_aces=(), avoid_species=(), excluded=()):
        """Species (ace last) and the ace's Mega Stone (or None) for a generated trainer."""
        cands = [s for s in self.candidates(pool, floor, excluded) if s not in avoid_species]
        if len(cands) < size:
            cands = self.candidates(pool, floor, excluded)
        if len(cands) < size:
            raise ValueError("pool too small for floor %d" % floor)
        stone = None
        ace = None
        if mode in ("force", "prefer"):
            megas = self._ace_pool(cands, recent_aces, True)
            if megas:
                ace = self.weighted_pick(megas, floor, rng, 1, lift=40)[0]
                opts = self.mega_options(ace)
                stone = opts[rng.randrange(len(opts))][1] if len(opts) > 1 else opts[0][1]
                stone = self._best_stone(ace, opts) or stone
            elif mode == "force":
                raise ValueError("no Mega-capable species for floor %d" % floor)
        if ace is None:
            ace = self.weighted_pick(self._ace_pool(cands, recent_aces, False), floor, rng, 1, lift=40)[0]
        rest = [s for s in cands if s != ace]
        if stone is None:
            # the strongest member becomes the ace below: keep the recent aces out of the team where the pool allows
            fresh = [s for s in rest if s not in recent_aces]
            if len(fresh) >= size - 1:
                rest = fresh
        others = self.weighted_pick(rest, floor, rng, size - 1)
        if stone is None:
            # the ace is the strongest of the team
            team = others + [ace]
            team.sort(key=lambda s: self.g.species[s].bst)
            ace = team[-1]
            others = team[:-1]
            rng.shuffle(others)
        return others + [ace], stone

    def _best_stone(self, sid, opts):
        """Of several Megas (Charizard X/Y and the like), the one whose form fits the species' attacking side."""
        if len(opts) == 1:
            return opts[0][1]
        base = self.g.species[sid]
        physical = base.stat("Atk") >= base.stat("SpA")
        best = None
        for mega_sid, stone in opts:
            m = self.g.species[mega_sid]
            key = (m.stat("Atk") if physical else m.stat("SpA"), m.bst, -mega_sid)
            if best is None or key > best[0]:
                best = (key, stone)
        return best[1]
