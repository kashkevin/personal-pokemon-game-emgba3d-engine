"""The floor rules of Battle Tower: Legends (docs/battle-tower-legends.md sections 2, 6 and 7 in the app repository and
the generator's brief). The builder (builder.py) follows them and the validator (validate.py) checks every one."""

FLOOR_COUNT = 100
BOSS_FLOORS = (10, 20, 30, 40, 60, 70, 80, 90)
SPECIAL_FLOORS = (25, 50, 75)
FINAL_FLOOR = 100
CHECKPOINTS = (10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90, 100)
KANTO_FLOORS = (10, 20, 25, 90, 100)          # the FRLG bosses; every other floor is Hoenn's

BOSS_TEAM_SIZE = {10: 3, 20: 4, 25: 5, 30: 5, 40: 5, 50: 6, 60: 6, 70: 6, 75: 6, 80: 6, 90: 6, 100: 6}

# Floors 30-49: at most 3 regular trainers Mega Evolve; 50-69: about a third of the 18 regular trainers do.
MEGA_REGULAR_30_49 = (34, 43, 47)
MEGA_REGULAR_50_69 = (52, 55, 58, 63, 66, 69)

# The engine's AI_FLAG_SMART_TRAINER preset without AI_FLAG_SMART_TERA: that flag keeps a trainer Pokemon's own Tera
# type, so the AI Terastallizes; the Tower's only battle gimmick is Mega Evolution (design section 6).
SMART = ("AI_FLAG_BASIC_TRAINER", "AI_FLAG_OMNISCIENT", "AI_FLAG_SMART_SWITCHING", "AI_FLAG_SMART_MON_CHOICES",
         "AI_FLAG_PP_STALL_PREVENTION", "AI_FLAG_RANDOMIZE_SWITCHIN")
NO_AI_FLAG = ("AI_FLAG_SMART_TERA",)          # never in a Tower trainer's flags (alone or in a preset)

AI_TIERS = (
    ("AI_FLAG_CHECK_BAD_MOVE",),                                         # floors 1-9
    ("AI_FLAG_BASIC_TRAINER",),                                          # 10-29
    ("AI_FLAG_BASIC_TRAINER", "AI_FLAG_SMART_SWITCHING"),                # 30-49
    SMART,                                                               # 50-69
    SMART + ("AI_FLAG_PREDICTION",),                                     # 70-100
    SMART + ("AI_FLAG_PREDICTION", "AI_FLAG_HP_AWARE"),                  # the strongest set (bosses above 70)
)


def kind(floor):
    if floor == FINAL_FLOOR:
        return "final"
    if floor in SPECIAL_FLOORS:
        return "special"
    if floor in BOSS_FLOORS:
        return "boss"
    return "regular"


def is_boss_floor(floor):
    return kind(floor) != "regular"


def solo_size(floor):
    if floor in BOSS_TEAM_SIZE:
        return BOSS_TEAM_SIZE[floor]
    if floor <= 4:
        return 1
    if floor <= 9:
        return 2
    if floor <= 24:
        return 3
    if floor <= 44:
        return 4
    if floor <= 49:
        return 5
    if floor <= 69:
        return 5
    return 6


def duo_size(floor):
    return min(3, solo_size(floor))


def region(floor):
    return "kanto" if floor in KANTO_FLOORS else "hoenn"


def difficulty(floor):
    base = 1 + (floor - 1) // 20                   # 1: 1-20, 2: 21-40, 3: 41-60, 4: 61-80, 5: 81-100
    k = kind(floor)
    if k == "boss":
        base += 1
    elif k in ("special", "final"):
        base += 2
    return min(5, base)


def band_tier(floor):
    if floor <= 9:
        return 0
    if floor <= 29:
        return 1
    if floor <= 49:
        return 2
    if floor <= 69:
        return 3
    return 4


def ai_flags(floor):
    """AI flags of the floor's trainers (solo, Duo A and Duo B alike)."""
    k = kind(floor)
    if k in ("special", "final"):
        return list(AI_TIERS[5]) + ["AI_FLAG_ACE_POKEMON"]
    if k == "boss":
        return list(AI_TIERS[min(5, band_tier(floor) + 1)])
    return list(AI_TIERS[band_tier(floor)])


def strong(floor):
    """Bosses and their partners take the top of the floor's IV/EV range; specials and the final the maximum."""
    return kind(floor) != "regular"


def optimized(floor):
    return kind(floor) in ("special", "final") or floor >= 70


def iv_range(floor):
    """(lowest, highest) IV a regular trainer of the floor uses in each stat."""
    if kind(floor) in ("special", "final") or floor >= 70:
        return 31, 31
    if floor <= 19:
        v = 3 + floor // 2
        return v, v
    if floor <= 39:
        lo = 15 + (floor - 20) * 6 // 19
        return (24, 24) if strong(floor) else (lo, lo + 3)
    lo = 24 + (floor - 40) * 4 // 29
    return (31, 31) if strong(floor) else (lo, min(31, lo + 3))


def iv_band(floor):
    """The rule's own bounds (for the validator)."""
    if kind(floor) in ("special", "final") or floor >= 70:
        return 31, 31
    if floor <= 19:
        v = 3 + floor // 2
        return v, v
    if floor <= 39:
        return 15, 24
    return 24, 31


def ev_limits(floor):
    """(per-stat limit, total limit, minimum total) of a trainer's EVs on the floor (bosses: best stat up to 252)."""
    if floor <= 19:
        return 0, 0, 0
    if kind(floor) in ("special", "final") or floor >= 40:
        return 252, 510, 500
    if strong(floor):
        return 252, 380, 0
    return 128, 256, 0


# Held items: the floor from which each group may be held.
ITEM_GROUPS = {
    5: ("ORAN_BERRY", "CHERI_BERRY", "CHESTO_BERRY", "PECHA_BERRY", "RAWST_BERRY", "ASPEAR_BERRY", "PERSIM_BERRY"),
    20: ("SITRUS_BERRY", "SILK_SCARF", "CHARCOAL", "MYSTIC_WATER", "MAGNET", "MIRACLE_SEED", "NEVER_MELT_ICE",
         "BLACK_BELT", "POISON_BARB", "SOFT_SAND", "SHARP_BEAK", "TWISTED_SPOON", "SILVER_POWDER", "HARD_STONE",
         "SPELL_TAG", "DRAGON_FANG", "BLACK_GLASSES", "METAL_COAT", "FAIRY_FEATHER"),
    30: ("LEFTOVERS", "QUICK_CLAW", "SCOPE_LENS", "SHELL_BELL", "FOCUS_BAND", "KINGS_ROCK", "LUM_BERRY"),
    50: ("LIFE_ORB", "CHOICE_BAND", "CHOICE_SPECS", "CHOICE_SCARF", "FOCUS_SASH", "ASSAULT_VEST", "EXPERT_BELT",
         "ROCKY_HELMET", "BLACK_SLUDGE", "EVIOLITE", "WEAKNESS_POLICY", "MUSCLE_BAND", "WISE_GLASSES", "LIGHT_CLAY",
         "HEAT_ROCK", "DAMP_ROCK", "SMOOTH_ROCK", "ICY_ROCK", "AIR_BALLOON", "WHITE_HERB", "MENTAL_HERB",
         "POWER_HERB", "TOXIC_ORB", "FLAME_ORB", "LIGHT_BALL", "THICK_CLUB", "LEEK", "LUCKY_PUNCH", "RAZOR_CLAW",
         "RAZOR_FANG", "WIDE_LENS", "ZOOM_LENS", "LIECHI_BERRY", "PETAYA_BERRY", "SALAC_BERRY", "OCCA_BERRY",
         "PASSHO_BERRY", "WACAN_BERRY", "RINDO_BERRY", "YACHE_BERRY", "CHOPLE_BERRY", "KEBIA_BERRY", "SHUCA_BERRY",
         "COBA_BERRY", "PAYAPA_BERRY", "TANGA_BERRY", "CHARTI_BERRY", "KASIB_BERRY", "HABAN_BERRY", "COLBUR_BERRY",
         "BABIRI_BERRY", "CHILAN_BERRY", "ROSELI_BERRY"),
}


def allowed_items(floor):
    out = set()
    for first, names in ITEM_GROUPS.items():
        if floor >= first:
            out.update("ITEM_" + n for n in names)
    return out


def mega_allowed(floor):
    return floor >= 30


def mega_mode(floor, partner=False):
    """How a generated trainer of a regular floor uses Mega Evolution: none, force (its ace holds its stone),
    prefer (its ace holds a stone when its species has a Mega; the ace is chosen among such species when possible)."""
    if floor < 30:
        return "none"
    if floor >= 90:
        return "force"
    if floor >= 70:
        return "prefer"
    if partner:
        return "none"
    if floor in MEGA_REGULAR_30_49 or floor in MEGA_REGULAR_50_69:
        return "force"
    return "none"


def moves_level_rule(floor):
    if floor < 30:
        return "level-up moves at or below the level"
    if floor < 50:
        return "level-up, TM and tutor moves"
    return "level-up, TM, tutor and egg moves"
