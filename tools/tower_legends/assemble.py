"""Builds the 100 floors (300 trainers) of Battle Tower: Legends: the hand-written bosses of bosses.py and the regular
floors generated from pools.py with one seeded random.Random."""
import random

import bosses
import pools
import rules
from builder import Trainer

SEED = 20261021
REGULAR_MUSIC = "MUS_VS_TRAINER"
RECENT_CLASSES = 3      # a regular floor's class (as the game names it) is not one of the 3 floors before
RECENT_ACES = 4         # nor its ace one of the 4 floors before (the previous floor's ace: never)
RECENT_LINES = 12       # nor its defeat lines one of the 12 lines before, where the class has others


def trainer_ids(floor):
    base = "TOWER_TRAINER_F%03d" % floor
    return base, base + "_DUO_A", base + "_DUO_B"


def class_intro_name(game, trainer_class, override=None):
    """The class as the field text writes it: the game's own class name in plain ASCII."""
    if override:
        return override
    name = game.classes[game.class_values[trainer_class]]["name"]
    if name.startswith("   "):
        name = "PKMN " + name.strip()
    name = name.replace("POK ", "POKE")
    if name in ("SWIMMERM", "SWIMMERF"):
        name = "SWIMMER"
    return name.strip()


def regular_intro(floor, class_name, name):
    one = "FLOOR %d!" % floor
    two = "%s %s wants to battle!" % (class_name, name)
    if len(two) <= 30:
        return one + "\\n" + two
    a, b = "FLOOR %d! %s" % (floor, class_name), "%s wants to battle!" % name
    if len(a) <= 30 and len(b) <= 30:
        return a + "\\n" + b
    return one + "\\n" + "%s %s" % (class_name, name) + "\\pwants to battle!"


class Assembler:
    def __init__(self, game, builder, seed=SEED):
        self.g = game
        self.b = builder
        self.rng = random.Random(seed)
        self.excluded = ["SPECIES_" + s for s in pools.EXCLUDED_SPECIES]
        self.used_names = set()
        self.floors = []
        self.notes = []             # substitutions and choices made on the way (FLOORS.md)
        for b in bosses.BOSSES.values():
            self.used_names.add(b["solo"]["name"])
            self.used_names.add(b["partner"]["name"])

    # ----- names, lines ------------------------------------------------------------------------------------------
    def new_name(self, gender):
        bank = pools.MALE_NAMES if gender == "M" else pools.FEMALE_NAMES
        free = [n for n in bank if n not in self.used_names]
        name = free[self.rng.randrange(len(free))]
        self.used_names.add(name)
        return name

    def line(self, cls, avoid=()):
        """One of the class's defeat lines in the game's text form (a literal \\n between the two lines)."""
        lines = [x.replace("\n", "\\n") for x in (cls.lines or pools.GENERIC_LINES)]
        fresh = [x for x in lines if x not in avoid] or lines
        return fresh[self.rng.randrange(len(fresh))]

    # ----- regular floors ----------------------------------------------------------------------------------------
    def class_ok(self, cls, floor, size, mode):
        if not cls.floors[0] <= floor <= cls.floors[1]:
            return False
        cands = self.b.candidates(cls.pool, floor, self.excluded)
        if len(cands) < size:
            return False
        if mode == "force" and not any(self.b.mega_options(s) for s in cands):
            return False
        return True

    def family(self, cls):
        """The class as the game names it: TUBER for both Tubers, TRIATHLETE for all six Triathletes."""
        return class_intro_name(self.g, cls.trainer_class, cls.intro_name)

    def schedule(self):
        """A trainer class for every regular floor: each class (as the game names it) of the floor's band in turn,
        never the class of one of the three floors before, and a class with a Mega-capable species where the floor
        needs a Mega."""
        usage = {c.key: 0 for c in pools.CLASSES}
        family_usage = {self.family(c): 0 for c in pools.CLASSES}
        recent = []
        plan = {}
        for floor in range(1, rules.FLOOR_COUNT + 1):
            if rules.is_boss_floor(floor):
                continue
            size = rules.solo_size(floor)
            mode = rules.mega_mode(floor)
            cands = [c for c in pools.CLASSES if self.class_ok(c, floor, size, mode)]
            fresh = [c for c in cands if self.family(c) not in recent[-RECENT_CLASSES:]]
            cands = fresh or cands

            def use(c):
                return family_usage[self.family(c)], usage[c.key]
            low = min(use(c) for c in cands)
            least = [c for c in cands if use(c) == low]
            pick = least[self.rng.randrange(len(least))]
            plan[floor] = pick
            usage[pick.key] += 1
            family_usage[self.family(pick)] += 1
            recent.append(self.family(pick))
        return plan

    def partner_class(self, cls, floor, size, previous):
        mode = rules.mega_mode(floor, partner=True)
        options = [pools.CLASS_BY_KEY[k] for k in cls.partners]
        ok = [p for p in options if p.key != cls.key and self.class_ok(p, floor, size, mode)]
        fresh = [p for p in ok if p.key != previous]
        if fresh or ok:
            return (fresh or ok)[0]
        # no listed partner fits this floor: any class of the floor with the same encounter music, else any class
        same = [p for p in pools.CLASSES if p.key != cls.key and p.music == cls.music and
                self.class_ok(p, floor, size, mode)]
        anyc = [p for p in pools.CLASSES if p.key != cls.key and self.class_ok(p, floor, size, mode)]
        pick = (same or anyc)[0]
        self.notes.append("Floor %d: no listed Duo partner of %s fits the floor; %s partners instead."
                          % (floor, cls.key, pick.key))
        return pick

    def holders(self, floor, size):
        """Which team slots hold an item on a regular floor (the ace is the last slot)."""
        if floor < 5:
            return set()
        if floor < 20:
            out = {size - 1}
            others = list(range(size - 1))
            if floor >= 11 and others:
                out.add(others[self.rng.randrange(len(others))])
            return out
        if floor < 25:
            out = {size - 1}
            others = list(range(size - 1))
            self.rng.shuffle(others)
            out.update(others[:1])
            return out
        return set(range(size))

    def generated_trainer(self, cls, floor, size, mode, recent_aces=(), avoid_species=()):
        species, stone = self.b.generate_team(cls.pool, floor, size, self.rng, mode, recent_aces=recent_aces,
                                              avoid_species=avoid_species, excluded=self.excluded)
        pic, gender = cls.pics[self.rng.randrange(len(cls.pics))]
        name = self.new_name(gender)
        hold = self.holders(floor, size)
        used = set()
        mons = []
        order = list(range(size - 1, -1, -1))           # the ace's set first, so it gets the first pick of items
        built = {}
        for i in order:
            spec = {"species": self.g.species[species[i]].const}
            if i == size - 1 and stone:
                spec["mega"] = True
                spec["stone"] = stone
            if floor < 20 and i in hold:
                spec["item"] = "ITEM_ORAN_BERRY" if (i == size - 1 or self.rng.random() < 0.5) else \
                    ["ITEM_CHERI_BERRY", "ITEM_PECHA_BERRY", "ITEM_RAWST_BERRY", "ITEM_PERSIM_BERRY"][
                        self.rng.randrange(4)]
            built[i] = self.b.build_mon(spec, floor, self.rng, used, holds_item=(i in hold))
        mons = [built[i] for i in range(size)]
        return Trainer(None, name, cls.trainer_class, pic, "Male" if gender == "M" else "Female", cls.music,
                       rules.ai_flags(floor), mons, class_key=cls.key)

    # ----- Duo A -------------------------------------------------------------------------------------------------
    def duo_a(self, solo, indices=None):
        n = len(solo.mons)
        if indices is None:
            if n <= 3:
                indices = list(range(n))
            else:
                ace = n - 1
                rest = sorted(range(n - 1), key=lambda i: (-self.b.strength(solo.mons[i]), i))[:2]
                indices = sorted(rest) + [ace]
        return Trainer(None, solo.name, solo.trainer_class, solo.pic, solo.gender, solo.music, list(solo.ai),
                       [solo.mons[i] for i in indices], class_key=solo.class_key)

    # ----- bosses ------------------------------------------------------------------------------------------------
    def boss_trainer(self, spec, floor):
        used = set()
        mons = []
        for m in spec["mons"]:
            mons.append(self.b.build_mon(dict(m), floor, self.rng, used, holds_item=True))
        return Trainer(None, spec["name"], spec["class"], spec["pic"], spec["gender"], spec["music"],
                       rules.ai_flags(floor), mons)

    # ----- all floors --------------------------------------------------------------------------------------------
    def build(self):
        plan = self.schedule()
        recent_aces = []
        prev_partner = None
        recent_lines = []
        for floor in range(1, rules.FLOOR_COUNT + 1):
            solo_id, a_id, b_id = trainer_ids(floor)
            kind = rules.kind(floor)
            if kind == "regular":
                cls = plan[floor]
                size = rules.solo_size(floor)
                solo = self.generated_trainer(cls, floor, size, rules.mega_mode(floor),
                                              recent_aces=recent_aces[-RECENT_ACES:])
                pcls = self.partner_class(cls, floor, rules.duo_size(floor), prev_partner)
                prev_partner = pcls.key
                partner = self.generated_trainer(pcls, floor, rules.duo_size(floor),
                                                 rules.mega_mode(floor, partner=True),
                                                 avoid_species=[m.species_id for m in solo.mons])
                duo_a = self.duo_a(solo)
                intro = regular_intro(floor, self.family(cls), solo.name)
                defeat = self.line(cls, avoid=recent_lines[-RECENT_LINES:])
                defeat_b = self.line(pcls, avoid=recent_lines[-RECENT_LINES:] + [defeat])
                recent_lines += [defeat, defeat_b]
                victory = None
                music = REGULAR_MUSIC
                pic_source = None
            else:
                spec = bosses.BOSSES[floor]
                solo = self.boss_trainer(spec["solo"], floor)
                partner = self.boss_trainer(spec["partner"], floor)
                duo_a = self.duo_a(solo, spec["duo_a"])
                intro, defeat, defeat_b, victory = spec["intro"], spec["defeat"], spec["defeat_b"], spec["victory"]
                music = spec["battle_music"]
                pic_source = spec["pic_source"]
            solo.id, duo_a.id, partner.id = solo_id, a_id, b_id
            recent_aces.append(solo.mons[-1].species_id)
            self.floors.append({
                "floor": floor, "kind": kind, "checkpoint": floor in rules.CHECKPOINTS, "region": rules.region(floor),
                "difficulty": rules.difficulty(floor), "music": music, "intro": intro, "defeat": defeat,
                "defeatB": defeat_b, "victory": victory, "solo": solo, "duoA": duo_a, "duoB": partner,
                "pic_source": pic_source,
            })
        return self.floors
