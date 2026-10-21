"""What the floor rules and the set builder know about moves (names checked against include/constants/moves.h).

The dump gives each move's effect, type, category, power, accuracy, priority, hit count and flags; secondary effects,
recharge turns and self-drops are not in it, so the few the builder needs are listed here by constant. Every name below
must exist in the engine (Movedex raises otherwise), so a typo cannot silently drop a rule.

Bans (section 7 of the design and the floor rules):
  * OHKO moves (EFFECT_OHKO), moves with the explosion flag, and the other moves that faint the user
    (SACRIFICE: Final Gambit, Memento, Healing Wish, Lunar Dance).
  * Status moves are allowed only when they appear in STATUS_ROLES below (a judgement call: a status move that does
    nothing useful for the AI in a single battle - Splash, Celebrate, Hold Hands, the doubles-only and the
    ally-only moves, Transform, Sketch - is never used). Evasion boosts (Double Team, Minimize) are not used either,
    and accuracy drops only on floors below 20.
  * JUDGEMENT_EXCLUDED: damaging moves that need conditions a Tower trainer never sets up or whose power the trainer
    data cannot fix (Return/Frustration need friendship, Hidden Power's type follows the IVs, Focus Punch, Belch,
    Spit Up, Natural Gift, Fling, Last Resort, Snore, Dream Eater, Synchronoise, Counter/Mirror Coat/Metal Burst/Bide,
    Present, Endeavor, Steel Beam/Mind Blown, Beat Up, Stored Power/Power Trip, Burn Up, Psywave, Sky Drop, Shell Trap,
    Rage; Rollout/Ice Ball/Flail/Reversal only below floor 15); Sonic Boom and Dragon Rage (fixed 20/40 HP) not
    below floor 20, where they would decide the battle.
"""

SACRIFICE = ("FINAL_GAMBIT", "MEMENTO", "HEALING_WISH", "LUNAR_DANCE")

JUDGEMENT_EXCLUDED = (
    "FOCUS_PUNCH", "BELCH", "SPIT_UP", "NATURAL_GIFT", "FLING", "LAST_RESORT", "SNORE", "DREAM_EATER", "SYNCHRONOISE",
    "SHELL_TRAP", "SKY_DROP", "PRESENT", "BIDE", "COUNTER", "MIRROR_COAT", "METAL_BURST", "COMEUPPANCE", "ENDEAVOR",
    "STEEL_BEAM", "MIND_BLOWN", "HIDDEN_POWER", "RETURN", "FRUSTRATION", "PIKA_PAPOW", "VEEVEE_VOLLEY", "STRUGGLE",
    "BEAT_UP", "STORED_POWER", "POWER_TRIP", "BURN_UP", "DOUBLE_SHOCK", "PSYWAVE", "RAGE", "ROLLOUT", "ICE_BALL",
    "FLAIL", "REVERSAL",
)
# Moves of JUDGEMENT_EXCLUDED that small Pokemon on the lowest floors may still use (they have little else): the floor
# below which each is allowed.
LOW_FLOOR_OK = {"ROLLOUT": 15, "ICE_BALL": 15, "FLAIL": 15, "REVERSAL": 15}
# Floors 1-9 (judgement): no damaging move above this power. The later generations' learnsets give some basic Pokemon
# strong moves at level 1 (Snubbull's three elemental Fangs, Chatot's Hyper Voice); the first floors stay with
# Tackle-like moves.
EARLY_FLOORS, EARLY_MAX_POWER = 10, 60

# Status moves the builder may use, with a role and a base value.
#   setup_phys / setup_spec / setup_both / setup_speed / setup_def: stat boosts of the user
#   recover, status, hazard, screen, protect, weather, phaze, utility, debuff (opponent's stats), accuracy
STATUS_ROLES = {
    # setup
    "SWORDS_DANCE": ("setup_phys", 9), "DRAGON_DANCE": ("setup_phys", 10), "BULK_UP": ("setup_phys", 8),
    "HONE_CLAWS": ("setup_phys", 6), "COIL": ("setup_phys", 7), "HOWL": ("setup_phys", 5), "CURSE": ("setup_phys", 6),
    "SHIFT_GEAR": ("setup_phys", 9), "MEDITATE": ("setup_phys", 4), "SHARPEN": ("setup_phys", 3),
    "NASTY_PLOT": ("setup_spec", 9), "CALM_MIND": ("setup_spec", 8), "QUIVER_DANCE": ("setup_spec", 10),
    "TAIL_GLOW": ("setup_spec", 9), "GROWTH": ("setup_spec", 5), "WORK_UP": ("setup_both", 5),
    "SHELL_SMASH": ("setup_both", 10), "AGILITY": ("setup_speed", 6), "ROCK_POLISH": ("setup_speed", 6),
    "AUTOTOMIZE": ("setup_speed", 5), "IRON_DEFENSE": ("setup_def", 5), "ACID_ARMOR": ("setup_def", 5),
    "BARRIER": ("setup_def", 5), "AMNESIA": ("setup_def", 5), "COSMIC_POWER": ("setup_def", 6),
    "COTTON_GUARD": ("setup_def", 5), "DEFEND_ORDER": ("setup_def", 6), "HARDEN": ("setup_def", 1),
    "WITHDRAW": ("setup_def", 1), "DEFENSE_CURL": ("setup_def", 1), "FOCUS_ENERGY": ("setup_def", 1),
    "CHARGE": ("setup_def", 2),
    # recovery
    "RECOVER": ("recover", 9), "ROOST": ("recover", 9), "SOFT_BOILED": ("recover", 9), "MILK_DRINK": ("recover", 9),
    "SLACK_OFF": ("recover", 9), "SYNTHESIS": ("recover", 7), "MORNING_SUN": ("recover", 7), "MOONLIGHT": ("recover", 7),
    "HEAL_ORDER": ("recover", 9), "SHORE_UP": ("recover", 8), "STRENGTH_SAP": ("recover", 9), "WISH": ("recover", 6),
    "REST": ("recover", 3), "INGRAIN": ("recover", 3), "AQUA_RING": ("recover", 3),
    # status conditions
    "THUNDER_WAVE": ("status", 8), "WILL_O_WISP": ("status", 8), "TOXIC": ("status", 8), "SPORE": ("status", 10),
    "SLEEP_POWDER": ("status", 8), "HYPNOSIS": ("status", 5), "SING": ("status", 4), "LOVELY_KISS": ("status", 6),
    "GRASS_WHISTLE": ("status", 4), "YAWN": ("status", 6), "GLARE": ("status", 8), "STUN_SPORE": ("status", 6),
    "POISON_POWDER": ("status", 4), "POISON_GAS": ("status", 4), "CONFUSE_RAY": ("status", 5), "SUPERSONIC": ("status", 2),
    "SWAGGER": ("status", 3), "SWEET_KISS": ("status", 3), "TEETER_DANCE": ("status", 3), "LEECH_SEED": ("status", 7),
    # hazards, screens, protection, weather
    "STEALTH_ROCK": ("hazard", 8), "SPIKES": ("hazard", 7), "TOXIC_SPIKES": ("hazard", 6), "STICKY_WEB": ("hazard", 6),
    "DEFOG": ("utility", 3), "REFLECT": ("screen", 6), "LIGHT_SCREEN": ("screen", 6),
    "PROTECT": ("protect", 5), "DETECT": ("protect", 5), "KINGS_SHIELD": ("protect", 6), "SPIKY_SHIELD": ("protect", 6),
    "BANEFUL_BUNKER": ("protect", 6), "ENDURE": ("protect", 1),
    "RAIN_DANCE": ("weather", 4), "SUNNY_DAY": ("weather", 4), "SANDSTORM": ("weather", 3), "HAIL": ("weather", 3),
    # phazing and other utility
    "ROAR": ("phaze", 4), "WHIRLWIND": ("phaze", 4), "HAZE": ("phaze", 4),
    "TAUNT": ("utility", 6), "ENCORE": ("utility", 6), "SUBSTITUTE": ("utility", 5), "PAIN_SPLIT": ("utility", 5),
    "HEAL_BELL": ("utility", 4), "AROMATHERAPY": ("utility", 4), "TAILWIND": ("utility", 5), "DESTINY_BOND": ("utility", 4),
    "PERISH_SONG": ("utility", 2), "BATON_PASS": ("utility", 2), "PARTING_SHOT": ("utility", 5), "MEAN_LOOK": ("utility", 1),
    "BLOCK": ("utility", 1), "SPIDER_WEB": ("utility", 1), "TORMENT": ("utility", 2), "DISABLE": ("utility", 3),
    "SAFEGUARD": ("utility", 3), "MIST": ("utility", 1), "MAGIC_COAT": ("utility", 2), "REFRESH": ("utility", 2),
    "SLEEP_TALK": ("utility", 1), "TRICK": ("utility", 3), "SWITCHEROO": ("utility", 3), "TRICK_ROOM": ("utility", 2),
    # opponent stat drops (filler on low floors)
    "GROWL": ("debuff", 2), "LEER": ("debuff", 2), "TAIL_WHIP": ("debuff", 2), "STRING_SHOT": ("debuff", 2),
    "SCARY_FACE": ("debuff", 3), "SCREECH": ("debuff", 3), "CHARM": ("debuff", 3), "FEATHER_DANCE": ("debuff", 3),
    "FAKE_TEARS": ("debuff", 3), "METAL_SOUND": ("debuff", 3), "TICKLE": ("debuff", 3), "COTTON_SPORE": ("debuff", 3),
    "EERIE_IMPULSE": ("debuff", 3), "CONFIDE": ("debuff", 2), "NOBLE_ROAR": ("debuff", 3), "PLAY_NICE": ("debuff", 2),
    "BABY_DOLL_EYES": ("debuff", 2), "TEARFUL_LOOK": ("debuff", 2), "VENOM_DRENCH": ("debuff", 2),
    # accuracy drops: floors 1-19 only
    "SAND_ATTACK": ("accuracy", 2), "SMOKESCREEN": ("accuracy", 2), "FLASH": ("accuracy", 2), "KINESIS": ("accuracy", 2),
}

RECHARGE = ("HYPER_BEAM", "GIGA_IMPACT", "BLAST_BURN", "HYDRO_CANNON", "FRENZY_PLANT", "ROCK_WRECKER", "ROAR_OF_TIME",
            "PRISMATIC_LASER", "ETERNABEAM", "METEOR_ASSAULT")
SELF_DROP = ("OVERHEAT", "DRACO_METEOR", "LEAF_STORM", "PSYCHO_BOOST", "FLEUR_CANNON", "CLOSE_COMBAT", "SUPERPOWER",
             "V_CREATE", "HAMMER_ARM", "ICE_HAMMER", "DRAGON_ASCENT", "HEADLONG_RUSH", "ARMOR_CANNON", "MAKE_IT_RAIN",
             "CLANGING_SCALES")
RAMPAGE = ("THRASH", "OUTRAGE", "PETAL_DANCE", "RAGING_FURY")
PUNCH = ("MEGA_PUNCH", "FIRE_PUNCH", "ICE_PUNCH", "THUNDER_PUNCH", "COMET_PUNCH", "DIZZY_PUNCH", "DYNAMIC_PUNCH",
         "MACH_PUNCH", "SKY_UPPERCUT", "METEOR_MASH", "SHADOW_PUNCH", "DRAIN_PUNCH", "BULLET_PUNCH", "HAMMER_ARM",
         "ICE_HAMMER", "POWER_UP_PUNCH")
BITE = ("BITE", "CRUNCH", "FIRE_FANG", "ICE_FANG", "THUNDER_FANG", "POISON_FANG", "HYPER_FANG", "PSYCHIC_FANGS")
PULSE = ("WATER_PULSE", "DARK_PULSE", "DRAGON_PULSE", "AURA_SPHERE", "ORIGIN_PULSE")
# Damaging moves whose secondary effect is worth something (value added to the builder's score; Sheer Force boosts them).
SECONDARY = {
    "SCALD": 14, "NUZZLE": 18, "BODY_SLAM": 6, "DISCHARGE": 6, "LAVA_PLUME": 6, "THUNDERBOLT": 4, "THUNDER": 4,
    "FLAMETHROWER": 4, "FIRE_BLAST": 4, "ICE_BEAM": 3, "BLIZZARD": 3, "PSYCHIC": 3, "SHADOW_BALL": 3, "SLUDGE_BOMB": 6,
    "SLUDGE_WAVE": 4, "POISON_JAB": 5, "EARTH_POWER": 3, "FLASH_CANNON": 2, "ENERGY_BALL": 2, "BUG_BUZZ": 2,
    "FOCUS_BLAST": 2, "DARK_PULSE": 4, "AIR_SLASH": 7, "IRON_HEAD": 5, "ROCK_SLIDE": 5, "ZEN_HEADBUTT": 3, "CRUNCH": 3,
    "WATERFALL": 3, "HEADBUTT": 4, "STOMP": 4, "EXTRASENSORY": 3, "ICY_WIND": 6, "MUD_SHOT": 5, "ROCK_TOMB": 5,
    "SNARL": 4, "BULLDOZE": 4, "ELECTROWEB": 5, "BUBBLE_BEAM": 2, "AURORA_BEAM": 2, "PSYBEAM": 2, "TWISTER": 1,
    "DRAGON_BREATH": 4, "SLUDGE": 3, "SMOG": 2, "ACID": 1, "MUD_BOMB": 2, "OCTAZOOKA": 2, "MIST_BALL": 2,
    "LUSTER_PURGE": 2, "METEOR_MASH": 3, "CROSS_POISON": 3, "BLAZE_KICK": 3, "HEAT_WAVE": 3, "HURRICANE": 4,
    "MOONBLAST": 3, "PLAY_ROUGH": 3, "LIQUIDATION": 3, "SPARK": 3, "FLAME_WHEEL": 3, "POWER_UP_PUNCH": 5,
    "IRON_TAIL": 2, "STEEL_WING": 2, "CHARGE_BEAM": 3, "ANCIENT_POWER": 3, "SILVER_WIND": 3, "OMINOUS_WIND": 3,
    "MYSTICAL_FIRE": 4, "FIRE_PUNCH": 3, "ICE_PUNCH": 3, "THUNDER_PUNCH": 3, "FIRE_FANG": 3, "ICE_FANG": 3,
    "THUNDER_FANG": 3, "POISON_FANG": 4, "BITE": 3, "FAKE_OUT": 4, "KNOCK_OFF": 16, "U_TURN": 10, "VOLT_SWITCH": 10,
    "RAPID_SPIN": 4, "DRAIN_PUNCH": 9, "GIGA_DRAIN": 9, "HORN_LEECH": 7, "DRAINING_KISS": 7, "LEECH_LIFE": 4,
    "MEGA_DRAIN": 3, "ABSORB": 2, "OBLIVION_WING": 7, "PARABOLIC_CHARGE": 5, "STEAMROLLER": 3, "ZING_ZAP": 4,
    "POISON_STING": 2, "EMBER": 2, "THUNDER_SHOCK": 2, "POWDER_SNOW": 2, "CONFUSION": 2, "WATER_PULSE": 2,
    "DYNAMIC_PUNCH": 4, "INFERNO": 4, "ZAP_CANNON": 4, "SCORCHING_SANDS": 8, "LUNGE": 5, "TROP_KICK": 4,
    "FIRE_LASH": 4, "STRUGGLE_BUG": 3, "SKITTER_SMACK": 3, "FLAME_CHARGE": 5, "LOW_SWEEP": 3, "ROCK_SMASH": 2,
    "CRUSH_CLAW": 2, "SHADOW_BONE": 3, "POLLEN_PUFF": 0, "FREEZE_DRY": 6, "FIERY_DANCE": 4, "SACRED_FIRE": 6,
}

# Variable-power moves: the builder's estimate of their power (the dump says 1).
VARIABLE_POWER = {"LOW_KICK": 60, "GRASS_KNOT": 60, "HEAVY_SLAM": 70, "HEAT_CRASH": 70, "SEISMIC_TOSS": 55,
                  "NIGHT_SHADE": 55, "SUPER_FANG": 45, "MAGNITUDE": 70, "WRING_OUT": 60, "CRUSH_GRIP": 60}


class Movedex:
    def __init__(self, game):
        self.g = game

        def ids(names):
            out = set()
            for n in names:
                out.add(game.move("MOVE_" + n))
            return out

        self.sacrifice = ids(SACRIFICE)
        self.excluded = ids(JUDGEMENT_EXCLUDED)
        self.low_floor_ok = {game.move("MOVE_" + n): lvl for n, lvl in LOW_FLOOR_OK.items()}
        self.status_roles = {game.move("MOVE_" + n): v for n, v in STATUS_ROLES.items()}
        self.recharge = ids(RECHARGE)
        self.self_drop = ids(SELF_DROP)
        self.rampage = ids(RAMPAGE)
        self.punch = ids(PUNCH)
        self.bite = ids(BITE)
        self.pulse = ids(PULSE)
        self.secondary = {game.move("MOVE_" + n): v for n, v in SECONDARY.items()}
        self.variable = {game.move("MOVE_" + n): v for n, v in VARIABLE_POWER.items()}
        e = game.effect_values
        self.e_ohko = e["EFFECT_OHKO"]
        self.e_two_turn = {e["EFFECT_TWO_TURNS_ATTACK"], e["EFFECT_SOLAR_BEAM"]}
        self.e_semi = e["EFFECT_SEMI_INVULNERABLE"]
        self.e_recoil = e["EFFECT_RECOIL"]
        self.e_recoil_miss = e["EFFECT_RECOIL_IF_MISS"]
        self.e_first_turn = e["EFFECT_FIRST_TURN_ONLY"]
        self.e_fixed_hp = e["EFFECT_FIXED_HP_DAMAGE"]
        self.e_level = e["EFFECT_LEVEL_DAMAGE"]
        self.e_triple = e["EFFECT_TRIPLE_KICK"]
        self.e_gyro = e["EFFECT_GYRO_BALL"]
        self.e_electro = e["EFFECT_ELECTRO_BALL"]
        self.e_knock = e["EFFECT_KNOCK_OFF"]
        self.e_acrobatics = e["EFFECT_ACROBATICS"]
        self.e_facade = e["EFFECT_FACADE"]
        self.e_psyshock = e["EFFECT_PSYSHOCK"]
        self.e_body_press = e["EFFECT_BODY_PRESS"]
        self.e_foul_play = e["EFFECT_FOUL_PLAY"]
        self.e_power_hp = e["EFFECT_POWER_BASED_ON_USER_HP"]
        self.e_rapid_spin = e["EFFECT_RAPID_SPIN"]
        self.e_delayed = e["EFFECT_FUTURE_SIGHT"]
        self.status_cat = 3

    # ----- rules -------------------------------------------------------------------------------------------------
    def ban_reason(self, mid, floor):
        """Why the floor rules (or the judgement rules above) forbid the move; None when it is allowed."""
        m = self.g.moves[mid]
        if m["effect"] == self.e_ohko:
            return "OHKO"
        if m["explosion"]:
            return "explosion"
        if mid in self.sacrifice:
            return "faints the user"
        if m["category"] == self.status_cat:
            role = self.status_roles.get(mid)
            if role is None:
                return "status move with no use for the AI"
            if role[0] == "accuracy" and floor >= 20:
                return "accuracy drop (floors 1-19 only)"
            return None
        if mid in self.excluded and not (mid in self.low_floor_ok and floor < self.low_floor_ok[mid]):
            return "condition-dependent (judgement)"
        if m["effect"] == self.e_fixed_hp and floor < 20:
            return "fixed damage that would decide a low-level battle (judgement, floors 1-19)"
        if m["power"] == 0:
            return "no power"
        if floor < EARLY_FLOORS and m["power"] > EARLY_MAX_POWER:
            return "over %d power on floors 1-%d (judgement)" % (EARLY_MAX_POWER, EARLY_FLOORS - 1)
        return None

    def is_damaging(self, mid):
        m = self.g.moves[mid]
        return m["category"] != self.status_cat and m["power"] > 0

    # ----- the builder's estimates -------------------------------------------------------------------------------
    def hits(self, mid, skill_link=False):
        m = self.g.moves[mid]
        if m["effect"] == self.e_triple:
            return 3.0                                    # Triple Kick 10+20+30, Triple Axel 20+40+60 (power x6)
        if m["strikes"] and m["strikes"] > 1:
            return float(m["strikes"])
        if m["multiHit"]:
            return 5.0 if skill_link else 3.0
        return 1.0

    def base_power(self, mid, level, speed_base, skill_link=False):
        """A move's expected power before STAB and abilities (variable, multi-hit and fixed-damage moves estimated)."""
        m = self.g.moves[mid]
        p = m["power"]
        if mid in self.variable:
            p = self.variable[mid]
        elif m["effect"] == self.e_fixed_hp:
            p = 60 if level < 20 else (35 if level < 35 else 10)        # Dragon Rage / Sonic Boom: big early, nothing late
        elif m["effect"] == self.e_level:
            p = 55
        elif m["effect"] == self.e_gyro:
            p = 100 if speed_base <= 50 else 50
        elif m["effect"] == self.e_electro:
            p = 90 if speed_base >= 100 else 55
        elif m["effect"] == self.e_knock:
            p = p * 1.5
        elif m["effect"] == self.e_power_hp:
            p = p * 0.8
        elif p <= 1:
            p = 40
        if m["effect"] == self.e_triple:
            return m["power"] * 6 * 0.85
        return p * self.hits(mid, skill_link)

    def multiplier(self, mid, has_power_herb=False, rock_head=False):
        """Accuracy and drawbacks."""
        m = self.g.moves[mid]
        acc = 1.05 if m["accuracy"] == 0 else m["accuracy"] / 100.0
        f = acc
        if mid in self.recharge:
            f *= 0.4
        if m["effect"] in self.e_two_turn and not has_power_herb:
            f *= 0.5
        if m["effect"] == self.e_semi:
            f *= 0.7
        if mid in self.self_drop:
            f *= 0.85
        if mid in self.rampage:
            f *= 0.8
        if m["effect"] == self.e_recoil and not rock_head:
            f *= 0.9
        if m["effect"] == self.e_recoil_miss:
            f *= 0.85
        if m["effect"] == self.e_first_turn:
            f *= 0.45
        if m["effect"] == self.e_delayed:
            f *= 0.7
        return f
