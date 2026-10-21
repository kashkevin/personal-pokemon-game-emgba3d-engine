"""Checks every floor against the rules (README.md, "The rules"). validate() returns a Report: the number of times each
rule was checked and every violation; generate.py prints the summary and exits nonzero when anything fails."""
import collections
import re

import assemble
import bosses
import pools
import rules

TEXT_CHARS = re.compile(r"^[A-Za-z0-9 .,!?'\-:]*$")
NAME_RE = re.compile(r"^[A-Z][A-Z .'\-]*$")
RIVAL_NAME_CLASSES = ("TRAINER_CLASS_RIVAL_EARLY_FRLG", "TRAINER_CLASS_RIVAL_LATE_FRLG", "TRAINER_CLASS_CHAMPION_FRLG")
BRANDON_LEGENDS = ("SPECIES_REGIROCK", "SPECIES_REGICE", "SPECIES_REGISTEEL")
STATS = ("HP", "Atk", "Def", "SpA", "SpD", "Spe")


class Report:
    def __init__(self):
        self.counts = collections.OrderedDict()
        self.errors = []
        self.facts = collections.OrderedDict()

    def check(self, rule, ok, where=""):
        self.counts[rule] = self.counts.get(rule, 0) + 1
        if not ok:
            self.errors.append("%s: %s" % (rule, where))
        return ok


def text_ok(text):
    """The game's text: \\n and \\p escapes, plain ASCII, at most 30 characters a line, two lines a page."""
    if text is None:
        return False, "missing"
    pages = text.split("\\p")
    for page in pages:
        lines = page.split("\\n")
        if len(lines) > 2:
            return False, "more than two lines on a page: %r" % page
        for ln in lines:
            if len(ln) > 30:
                return False, "line over 30 characters: %r" % ln
            if not TEXT_CHARS.match(ln):
                return False, "character outside letters, digits and .,!?'-: in %r" % ln
            if not ln:
                return False, "empty line in %r" % text
    if "\\" in text.replace("\\n", "").replace("\\p", ""):
        return False, "stray backslash"
    return True, ""


def validate(g, b, md, floors, pic_genders):
    r = Report()
    T = g.type_values
    names_seen = {}
    total_trainers = 0
    prev_ace = None
    prev_class = None
    band_stats = collections.OrderedDict()
    mega_counts = collections.Counter()

    r.check("100 floors in order", [f["floor"] for f in floors] == list(range(1, 101)), "floor numbers")
    for f in floors:
        n = f["floor"]
        kind = rules.kind(n)
        where = "floor %d" % n
        r.check("floor kind", f["kind"] == kind, where)
        r.check("checkpoint floors", f["checkpoint"] == (n in rules.CHECKPOINTS), where)
        r.check("region", f["region"] == rules.region(n), where)
        r.check("difficulty 1..5", 1 <= f["difficulty"] <= 5, where)
        r.check("battle music exists", f["music"] in g.songs, where + " " + f["music"])
        if kind == "regular":
            r.check("regular floors play MUS_VS_TRAINER", f["music"] == "MUS_VS_TRAINER", where)
        else:
            r.check("boss battle music", f["music"] == bosses.BOSSES[n]["battle_music"], where)
        # texts
        for key in ("intro", "defeat", "defeatB"):
            ok, why = text_ok(f[key])
            r.check("text format", ok, "%s %s: %s" % (where, key, why))
        if kind == "regular":
            r.check("no victory text on regular floors", f["victory"] is None, where)
            r.check("regular intro names the floor and trainer",
                    ("FLOOR %d!" % n) in f["intro"] and f["solo"].name in f["intro"], where)
        else:
            ok, why = text_ok(f["victory"])
            r.check("text format", ok, "%s victory: %s" % (where, why))
        solo, a, bb = f["solo"], f["duoA"], f["duoB"]
        base = "TOWER_TRAINER_F%03d" % n
        r.check("trainer ids", (solo.id, a.id, bb.id) == (base, base + "_DUO_A", base + "_DUO_B"), where)
        total_trainers += 3
        r.check("solo team size", len(solo.mons) == rules.solo_size(n), "%s: %d" % (where, len(solo.mons)))
        r.check("Duo A team size", len(a.mons) == rules.duo_size(n), where)
        r.check("Duo B team size", len(bb.mons) == rules.duo_size(n), where)
        # Duo A: the solo trainer with its ace and its best
        same = (a.name, a.trainer_class, a.pic, a.gender, a.music, a.ai) == \
               (solo.name, solo.trainer_class, solo.pic, solo.gender, solo.music, solo.ai)
        r.check("Duo A is the solo trainer", same, where)
        r.check("Duo A brings the ace (last)", a.mons[-1] is solo.mons[-1], where)
        r.check("Duo A's Pokemon are the solo team's", all(any(m is s for s in solo.mons) for m in a.mons), where)
        if kind == "regular" and len(solo.mons) > 3:
            best = sorted(solo.mons[:-1], key=lambda m: -b.strength(m))[:2]
            r.check("Duo A = ace plus its best", all(any(m is x for x in a.mons) for m in best) or
                    sorted(b.strength(m) for m in a.mons[:-1]) == sorted(b.strength(m) for m in best), where)
        r.check("Duo B is a different trainer", bb.name != solo.name, where)
        # aces never repeat on consecutive floors
        ace = solo.mons[-1].species
        r.check("ace differs from the previous floor's", ace != prev_ace, "%s: %s" % (where, ace))
        prev_ace = ace
        # regular floors: never the class (as the game names it) of the regular floor before
        if kind == "regular":
            cls = pools.CLASS_BY_KEY[solo.class_key]
            family = assemble.class_intro_name(g, cls.trainer_class, cls.intro_name)
            r.check("class differs from the previous regular floor's", family != prev_class, "%s: %s" % (where, family))
            prev_class = family
        for role, t in (("solo", solo), ("duoA", a), ("duoB", bb)):
            tw = "%s %s %s" % (where, role, t.name)
            check_trainer(r, g, b, md, t, n, kind, role, tw, pic_genders, mega_counts)
            if role != "duoA":
                r.check("unique trainer names", t.name not in names_seen, "%s (also %s)" % (tw, names_seen.get(t.name)))
                names_seen.setdefault(t.name, tw)
        # difficulty statistics by band (regular solo trainers)
        if kind == "regular":
            band = (n - 1) // 10
            st = band_stats.setdefault(band, {"bst": [], "iv": [], "ev": [], "size": [], "tier": []})
            for m in solo.mons:
                st["bst"].append(b.strength(m))
                st["iv"].append(sum(m.ivs) / 6.0)
                st["ev"].append(sum(m.evs))
            st["size"].append(len(solo.mons))
            st["tier"].append(rules.band_tier(n))
        if kind != "regular":
            check_boss(r, g, f)

    r.check("300 trainers", total_trainers == 300, str(total_trainers))
    # Mega Evolution by band
    r.check("Megas 30-49: at most 3 regular trainers", mega_counts["regular30"] <= 3, str(mega_counts["regular30"]))
    r.check("Megas 50-69: about a third of the 18 regular trainers", 5 <= mega_counts["regular50"] <= 7,
            str(mega_counts["regular50"]))
    # difficulty rises band by band
    prev = None
    rows = []
    for band, st in band_stats.items():
        row = (band, sum(st["bst"]) / len(st["bst"]), sum(st["iv"]) / len(st["iv"]), sum(st["ev"]) / len(st["ev"]),
               sum(st["size"]) / len(st["size"]), max(st["tier"]))
        rows.append(row)
        if prev is not None:
            r.check("difficulty rises: team BST by band", row[1] > prev[1], "band %d: %.0f <= %.0f" % (band, row[1],
                                                                                                      prev[1]))
            r.check("difficulty rises: IVs by band", row[2] >= prev[2], "band %d" % band)
            r.check("difficulty rises: EVs by band", row[3] >= prev[3], "band %d" % band)
            r.check("difficulty rises: team size by band", row[4] >= prev[4], "band %d" % band)
            r.check("difficulty rises: AI tier by band", row[5] >= prev[5], "band %d" % band)
        prev = row
    r.facts["bands"] = rows
    r.facts["megas"] = dict(mega_counts)
    return r


def check_trainer(r, g, b, md, t, n, kind, role, tw, pic_genders, mega_counts):
    # class, picture, music, gender, AI
    r.check("trainer class exists", t.trainer_class in g.class_values, tw)
    cname = g.classes[g.class_values[t.trainer_class]]["name"] if t.trainer_class in g.class_values else ""
    r.check("class never prints the rival's name", t.trainer_class not in RIVAL_NAME_CLASSES, tw)
    r.check("class prints a real name", bool(cname.strip()) and cname.strip() != "TRAINER" or
            t.trainer_class in ("TRAINER_CLASS_RIVAL",), "%s: %r" % (tw, cname))
    pid = g.pic_values.get(t.pic)
    r.check("trainer picture exists and is drawn", pid is not None and g.pics[pid]["front"] == 1 and
            g.pics[pid]["palette"] == 1, tw + " " + t.pic)
    want = pic_genders.get(t.pic)
    r.check("trainer gender fits the picture", want is None or want == t.gender, "%s %s %s" % (tw, t.pic, t.gender))
    r.check("encounter music exists", t.music in g.encounter_music, tw)
    r.check("AI flags of the floor", t.ai == rules.ai_flags(n), "%s %s" % (tw, t.ai))
    r.check("AI flags exist", all(a in g.ai_flags for a in t.ai), tw)
    banned = 0
    for flag in rules.NO_AI_FLAG:
        banned |= g.ai_flags.get(flag, 0)
    r.check("no AI flag that Terastallizes (AI_FLAG_SMART_TERA, alone or in a preset)",
            all(not (g.ai_flags.get(a, 0) & banned) for a in t.ai), "%s %s" % (tw, t.ai))
    r.check("trainer name: uppercase, at most 10 characters", bool(NAME_RE.match(t.name)) and len(t.name) <= 10, tw)
    stones = [m for m in t.mons if m.mega]
    r.check("at most one Mega Stone per trainer", len(stones) <= 1, tw)
    regular = kind == "regular"
    if stones:
        if regular and role == "solo" and 30 <= n <= 49:
            mega_counts["regular30"] += 1
        if regular and role == "solo" and 50 <= n <= 69:
            mega_counts["regular50"] += 1
    r.check("no Mega Evolution below floor 30", n >= 30 or not stones, tw)
    if 30 <= n <= 49 and kind == "regular" and role == "duoB":
        r.check("no Mega for 30-49 Duo partners", not stones, tw)
    if 70 <= n <= 89 and kind == "regular" and role != "duoA":
        ace = t.mons[-1]
        r.check("Megas 70-89: the ace holds its stone where it has a Mega",
                (not b.mega_options(ace.species_id)) or ace.mega is not None, tw + " " + ace.species)
    if n >= 90:
        r.check("Megas 90-100: every trainer has a Mega", len(stones) == 1, tw)
    if n >= 30:
        items = [m.item for m in t.mons if m.item]
        r.check("no item twice in a team from floor 30", len(items) == len(set(items)), tw)
    for i, m in enumerate(t.mons):
        check_mon(r, g, b, md, m, n, kind, role, "%s #%d %s" % (tw, i + 1, m.species), t)


def check_mon(r, g, b, md, m, n, kind, role, mw, t):
    T = g.type_values
    s = g.species_by_const.get(m.species)
    r.check("species enabled", s is not None, mw)
    if s is None:
        return
    r.check("level is exactly the floor", m.level == n, mw)
    legendary_ok = (n == 75 and role in ("solo", "duoA") and m.species in BRANDON_LEGENDS)
    problem = b.species_problem(s.id, n, legendary_ok=legendary_ok)
    r.check("species legal on the floor (Gen 1-6, base form, pictures, no legends, level chain)", problem is None,
            "%s: %s" % (mw, problem))
    if s.legendary:
        r.check("legendaries only where the design says (Brandon's Regis)", legendary_ok, mw)
    # Mega Stone
    stone_ids = {c for _, c in b.mega_options(s.id)}
    is_stone = m.item and g.items[g.item(m.item)]["holdEffect"] == g.hold_values["HOLD_EFFECT_MEGA_STONE"]
    if is_stone:
        r.check("a Mega Stone only on the species it Mega Evolves", m.item in stone_ids, mw + " " + m.item)
        r.check("Mega form recorded", m.mega == g.species[b.battle_form(s.id, m.item)].const, mw)
        mega = g.species_by_const.get(m.mega)
        r.check("Mega form enabled with real pictures", mega is not None and mega.all_pics and mega.flags["mega"], mw)
    else:
        r.check("no Mega form without its stone", m.mega is None, mw)
    # moves
    learnable = g.learnable(s.id, n)
    mids = []
    for c in m.moves:
        if c not in g.move_by_const:
            r.check("move exists", False, mw + " " + c)
            continue
        mids.append(g.move(c))
    r.check("1 to 4 moves", 1 <= len(mids) <= 4, mw)
    r.check("distinct moves", len(set(mids)) == len(mids), mw)
    for mid in mids:
        r.check("move legal at the floor (%s)" % rules.moves_level_rule(n).split(" at ")[0], mid in learnable,
                "%s %s (%s)" % (mw, g.move_const[mid], rules.moves_level_rule(n)))
        reason = md.ban_reason(mid, n)
        r.check("no banned move (OHKO, explosion, useless status, judgement)", reason is None,
                "%s %s: %s" % (mw, g.move_const[mid], reason))
    eligible = b.eligible_moves(s.id, n)
    r.check("four moves unless fewer are legal", len(mids) == min(4, len(eligible)),
            "%s: %d of %d" % (mw, len(mids), len(eligible)))
    r.check("at least one damaging move", any(md.is_damaging(x) for x in mids), mw)
    if n >= 30:
        form = g.species_by_const[m.mega] if m.mega else s
        battle_ability = g.ability_const[form.abilities[0]] if m.mega else m.ability
        types = set(s.types) | set(form.types)
        can_stab = any(b.is_stab(x, s.types, m.ability) for x in eligible)
        has_stab = any(b.is_stab(x, types, battle_ability) or b.is_stab(x, s.types, m.ability) for x in mids)
        r.check("a STAB move from floor 30 (when learnable)", has_stab or not can_stab, mw)
    # ability
    ab = g.ability_by_const.get(m.ability)
    r.check("ability exists", ab is not None, mw)
    a = s.abilities
    r.check("one of the species' own abilities", ab in [x for x in a if x], "%s %s" % (mw, m.ability))
    if ab == a[2] and ab not in (a[0], a[1]):
        r.check("hidden ability only from floor 50", n >= 50, mw)
    # item
    if n < 5:
        r.check("no held item on floors 1-4", m.item is None, mw)
    if m.item:
        item = g.items.get(g.item_by_const.get(m.item))
        r.check("held item exists with a hold effect", item is not None and item["holdEffect"] != 0, mw + " " + m.item)
        if not is_stone:
            r.check("held item allowed on the floor", m.item in rules.allowed_items(n), "%s %s" % (mw, m.item))
        else:
            r.check("Mega Stones from floor 30", n >= 30, mw)
        if m.item == "ITEM_EVIOLITE":
            r.check("Eviolite only on a Pokemon that can evolve", g.can_evolve(s.id), mw)
        if m.item == "ITEM_BLACK_SLUDGE":
            r.check("Black Sludge only on Poison types", T["TYPE_POISON"] in s.types, mw)
        if m.item in ("ITEM_CHOICE_BAND", "ITEM_CHOICE_SPECS", "ITEM_CHOICE_SCARF", "ITEM_ASSAULT_VEST"):
            r.check("Choice items and Assault Vest with attacks only", all(md.is_damaging(x) for x in mids), mw)
        if m.item == "ITEM_LIGHT_BALL":
            r.check("Light Ball only on Pikachu", m.species == "SPECIES_PIKACHU", mw)
    # IVs, EVs, nature
    lo, hi = rules.iv_band(n)
    r.check("IVs of the floor's band", len(m.ivs) == 6 and all(lo <= v <= hi for v in m.ivs),
            "%s %s not in %d-%d" % (mw, m.ivs, lo, hi))
    if n <= 19:
        r.check("IVs 3 + floor/2 on floors 1-19", all(v == 3 + n // 2 for v in m.ivs), mw)
    if kind in ("special", "final") or n >= 70:
        r.check("IVs 31 on special floors and from floor 70", all(v == 31 for v in m.ivs), mw)
    per, total, minimum = rules.ev_limits(n)
    r.check("EVs at most 252 each and 510 in all", all(0 <= v <= 252 for v in m.evs) and sum(m.evs) <= 510, mw)
    r.check("EVs of the floor (none to 19, light to 39, full spreads from 40)",
            all(v <= per for v in m.evs) and sum(m.evs) <= total and sum(m.evs) >= minimum,
            "%s %s (per %d, total %d, min %d)" % (mw, m.evs, per, total, minimum))
    r.check("nature exists", m.nature in g.natures, mw)
    if n >= 20 and m.nature in g.natures:
        fi = b.form_info(b.battle_form(s.id, m.item), m.ability)
        fi["side"] = b.preferred_side(s.id, n, fi)
        side = b.attack_side(m, fi)
        prim = "Atk" if side == "P" else "SpA"
        up, down = b.nature_effect(m.nature)
        r.check("nature never lowers the attacking stat (from floor 20)", down != prim,
                "%s %s %s" % (mw, m.nature, prim))
        if b.mixed(m):
            r.check("a set attacking with both stats lowers neither (from floor 20)", down not in ("Atk", "SpA"),
                    "%s %s" % (mw, m.nature))
        if n >= 40:
            evs = dict(zip(STATS, m.evs))
            good = up is not None and (up in (prim, "Spe") or evs.get(up, 0) >= 252)
            r.check("a beneficial nature from floor 40", good, "%s %s" % (mw, m.nature))


def check_boss(r, g, f):
    """The design's boss table: the core species, the ace, the Mega, the pictures and classes."""
    n = f["floor"]
    spec = bosses.BOSSES[n]
    solo, partner = f["solo"], f["duoB"]
    species = [m.species for m in solo.mons]
    mega = [m.species for m in solo.mons if m.mega]
    want = {
        10: (["GEODUDE", "KABUTO", "ONIX"], "ONIX", None),
        20: (["PSYDUCK", "GOLDEEN", "STARYU", "STARMIE"], "STARMIE", None),
        25: (["SANDSLASH", "GLIGAR", "RHYHORN", "NIDOQUEEN", "NIDOKING"], "NIDOKING", None),
        30: (["TORKOAL", "NINETALES", "MAGMAR", "ARCANINE", "HOUNDOOM"], None, "HOUNDOOM"),
        40: (["LINOONE", "SWELLOW", "URSARING", "KANGASKHAN", "SLAKING"], "SLAKING", "KANGASKHAN"),
        50: (["WAILORD", "TENTACRUEL", "LUDICOLO", "WHISCASH", "GYARADOS", "MILOTIC"], "MILOTIC", "GYARADOS"),
        60: (["SWELLOW", "PELIPPER", "TROPIUS", "SKARMORY", "STARAPTOR", "ALTARIA"], None, "ALTARIA"),
        70: (["SKARMORY", "CLAYDOL", "AGGRON", "CRADILY", "ARMALDO", "METAGROSS"], None, "METAGROSS"),
        75: (["REGIROCK", "REGICE", "REGISTEEL"], None, None),
        80: (["ALTARIA", "KINGDRA", "FLYGON", "HAXORUS", "DRAGONITE", "SALAMENCE"], None, "SALAMENCE"),
        90: (["PIDGEOT", "ALAKAZAM", "RHYPERIOR", "EXEGGUTOR", "GYARADOS", "ARCANINE"], None, None),
        100: (["PIKACHU", "LAPRAS", "SNORLAX", "VENUSAUR", "BLASTOISE", "CHARIZARD"], "CHARIZARD", "CHARIZARD"),
    }[n]
    core, ace, mega_species = want
    r.check("boss team has the design's species", all("SPECIES_" + c in species for c in core),
            "floor %d: %s" % (n, species))
    if ace:
        r.check("boss ace written last", solo.mons[-1].species == "SPECIES_" + ace, "floor %d" % n)
    if mega_species:
        r.check("boss Mega as designed", mega == ["SPECIES_" + mega_species], "floor %d: %s" % (n, mega))
    if n == 75:
        r.check("Brandon has one Mega", len(mega) == 1, "floor 75")
    if n == 90:
        r.check("Blue's Mega is Pidgeot or Alakazam", mega in (["SPECIES_PIDGEOT"], ["SPECIES_ALAKAZAM"]), str(mega))
    if n == 100:
        pika = [m for m in solo.mons if m.species == "SPECIES_PIKACHU"]
        r.check("Red's Pikachu holds the Light Ball", bool(pika) and pika[0].item == "ITEM_LIGHT_BALL", "floor 100")
        r.check("Red's Duo A: Pikachu, Snorlax and Charizard",
                sorted(m.species for m in f["duoA"].mons) == ["SPECIES_CHARIZARD", "SPECIES_PIKACHU",
                                                               "SPECIES_SNORLAX"], "floor 100")
        r.check("Leaf's Duo: Venusaur, Blastoise and Lapras",
                sorted(m.species for m in partner.mons) == ["SPECIES_BLASTOISE", "SPECIES_LAPRAS",
                                                             "SPECIES_VENUSAUR"], "floor 100")
    pics = {10: "LEADER_BROCK_FRLG", 20: "LEADER_MISTY_FRLG", 25: "LEADER_GIOVANNI_FRLG", 30: "LEADER_FLANNERY",
            40: "LEADER_NORMAN", 50: "CHAMPION_WALLACE", 60: "LEADER_WINONA", 70: "STEVEN",
            75: "PYRAMID_KING_BRANDON", 80: "ELITE_FOUR_DRAKE", 90: "CHAMPION_RIVAL_FRLG", 100: "RED"}
    r.check("boss picture as designed", solo.pic == "TRAINER_PIC_" + pics[n], "floor %d" % n)
    partner_pics = {10: "HIKER_FRLG", 20: "SWIMMER_F_FRLG", 25: "ROCKET_GRUNT_M_FRLG", 30: "KINDLER",
                    40: "WALLY", 50: "LEADER_JUAN", 60: "BIRD_KEEPER", 70: "EXPERT_M", 75: "PIKE_QUEEN_LUCY",
                    80: "DRAGON_TAMER", 90: "COOLTRAINER_F_FRLG", 100: "LEAF"}
    r.check("Duo partner as designed", partner.pic == "TRAINER_PIC_" + partner_pics[n], "floor %d" % n)
