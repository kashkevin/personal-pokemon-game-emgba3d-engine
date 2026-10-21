"""The hand-written bosses of Battle Tower: Legends and their Duo partners (design: the Bosses table).

Each boss floor names the solo trainer (the boss), which of its Pokemon it brings to a Duo battle (`duo_a`: indices,
its ace plus its best, in team order), the Duo partner, and the texts. A Pokemon gives its species, held item, Ability
and moves (and the nature/EVs where the set needs a particular one); the rest follows the floor's rules
(builder.Builder.build_mon), and validate.py checks everything against the rules like any other trainer.

Text conventions (Emerald field and battle text): `\\n` a new line, `\\p` a new page, at most 30 characters per line and
two lines per page, plain ASCII. intro: shown on the field before the solo battle; defeat: the boss's line when beaten
(also the Duo A line); defeat_b: the partner's line; victory: when the player loses.
"""

M, F = "Male", "Female"


def mon(species, item=None, ability=None, moves=None, nature=None, evs=None, mega=False, stone=None):
    out = {"species": "SPECIES_" + species}
    if item:
        out["item"] = "ITEM_" + item
    if ability:
        out["ability"] = "ABILITY_" + ability
    if moves:
        out["moves"] = ["MOVE_" + m for m in moves]
    if nature:
        out["nature"] = "NATURE_" + nature
    if evs:
        out["evs"] = evs
    if mega:
        out["mega"] = True
        if stone:
            out["stone"] = "ITEM_" + stone
    return out


def trainer(name, tclass, pic, gender, music, mons):
    return {"name": name, "class": "TRAINER_CLASS_" + tclass, "pic": "TRAINER_PIC_" + pic, "gender": gender,
            "music": "TRAINER_ENCOUNTER_MUSIC_" + music, "mons": mons}


BOSSES = {
    10: {
        "battle_music": "MUS_RG_VS_GYM_LEADER",
        "pic_source": "FRLG",
        "solo": trainer("BROCK", "LEADER_FRLG", "LEADER_BROCK_FRLG", M, "MALE", [
            mon("GEODUDE", "ORAN_BERRY", "STURDY", ["ROLLOUT", "TACKLE", "DEFENSE_CURL", "ROCK_POLISH"]),
            mon("KABUTO", "CHERI_BERRY", "BATTLE_ARMOR", ["SCRATCH", "ABSORB", "HARDEN"]),
            mon("ONIX", "ORAN_BERRY", "STURDY", ["ROCK_TOMB", "TACKLE", "BIND", "CURSE"]),
        ]),
        "duo_a": [0, 1, 2],
        "partner": trainer("CLIFFORD", "HIKER_FRLG", "HIKER_FRLG", M, "HIKER", [
            mon("SANDSHREW", "PECHA_BERRY", "SAND_VEIL"),
            mon("MACHOP", "RAWST_BERRY", "GUTS"),
            mon("RHYHORN", "ORAN_BERRY", "ROCK_HEAD"),
        ]),
        "intro": r"I'm BROCK, the TOWER's first\nwall! My rock-hard willpower\pwill stop your climb!",
        "defeat": r"I took you for granted.\nYou've earned your next floor!",
        "defeat_b": r"My boulders crumbled!\nSorry, BROCK!",
        "victory": r"Your POKEMON need more\ntraining. Come back stronger!",
    },
    20: {
        "battle_music": "MUS_RG_VS_GYM_LEADER",
        "pic_source": "FRLG",
        "solo": trainer("MISTY", "LEADER_FRLG", "LEADER_MISTY_FRLG", F, "FEMALE", [
            mon("PSYDUCK", "TWISTED_SPOON", "CLOUD_NINE", ["WATER_PULSE", "CONFUSION", "FURY_SWIPES", "TAIL_WHIP"]),
            mon("GOLDEEN", "SILK_SCARF", "WATER_VEIL", ["HORN_ATTACK", "WATER_PULSE", "PECK", "SUPERSONIC"]),
            mon("STARYU", "SITRUS_BERRY", "NATURAL_CURE", ["BUBBLE_BEAM", "SWIFT", "RAPID_SPIN", "RECOVER"]),
            mon("STARMIE", "MYSTIC_WATER", "NATURAL_CURE", ["HYDRO_PUMP", "SWIFT", "RAPID_SPIN", "RECOVER"]),
        ]),
        "duo_a": [0, 2, 3],
        "partner": trainer("CORAL", "SWIMMER_F_FRLG", "SWIMMER_F_FRLG", F, "FEMALE", [
            mon("HORSEA", "MYSTIC_WATER", "SWIFT_SWIM"),
            mon("SEEL", "SITRUS_BERRY", "THICK_FAT"),
            mon("SHELLDER", "NEVER_MELT_ICE", "SKILL_LINK"),
        ]),
        "intro": r"I'm MISTY! My water POKEMON\nwill wash away your climb!",
        "defeat": r"Wow! You're too much!\nAll right, you can pass.",
        "defeat_b": r"Glub! MISTY, I'm sorry!\nThey're too strong!",
        "victory": r"Hah! That was a splash!\nTrain up and try again!",
    },
    25: {
        "battle_music": "MUS_RG_VS_GYM_LEADER",
        "pic_source": "FRLG",
        "solo": trainer("GIOVANNI", "BOSS_FRLG", "LEADER_GIOVANNI_FRLG", M, "AQUA", [
            mon("SANDSLASH", "SOFT_SAND", "SAND_VEIL", ["MAGNITUDE", "CRUSH_CLAW", "FURY_CUTTER", "RAPID_SPIN"],
                "ADAMANT"),
            mon("GLIGAR", "SITRUS_BERRY", "HYPER_CUTTER", ["ACROBATICS", "KNOCK_OFF", "FURY_CUTTER", "QUICK_ATTACK"],
                "JOLLY"),
            mon("RHYHORN", "HARD_STONE", "ROCK_HEAD", ["BULLDOZE", "SMACK_DOWN", "CHIP_AWAY", "SCARY_FACE"],
                "ADAMANT"),
            mon("NIDOQUEEN", "BLACK_BELT", "POISON_POINT", ["SUPERPOWER", "CHIP_AWAY", "POISON_STING", "TAIL_WHIP"],
                "ADAMANT"),
            mon("NIDOKING", "SILVER_POWDER", "POISON_POINT", ["MEGAHORN", "CHIP_AWAY", "DOUBLE_KICK", "POISON_STING"],
                "JOLLY"),
        ]),
        "duo_a": [0, 3, 4],
        "partner": trainer("GRUNT", "TEAM_ROCKET_FRLG", "ROCKET_GRUNT_M_FRLG", M, "AQUA", [
            mon("RATICATE", "SILK_SCARF", "GUTS"),
            mon("GOLBAT", "SHARP_BEAK", "INNER_FOCUS"),
            mon("ARBOK", "BLACK_GLASSES", "INTIMIDATE"),
        ]),
        "intro": r"So, you've climbed this far.\nI am GIOVANNI. Let me teach\pyou what true power means!",
        "defeat": r"Ha! That was a truly intense\nfight. You have won.",
        "defeat_b": r"The BOSS won't be pleased\nwith me...",
        "victory": r"Hmph. Know your place, child.\nCome back when you're ready.",
    },
    30: {
        "battle_music": "MUS_VS_GYM_LEADER",
        "pic_source": "Emerald",
        "solo": trainer("FLANNERY", "LEADER", "LEADER_FLANNERY", F, "FEMALE", [
            mon("TORKOAL", "LEFTOVERS", "WHITE_SMOKE", ["LAVA_PLUME", "EARTHQUAKE", "ROCK_SLIDE", "TOXIC"]),
            mon("NINETALES", "SHELL_BELL", "FLASH_FIRE", ["FLAMETHROWER", "SHADOW_BALL", "NASTY_PLOT", "CONFUSE_RAY"]),
            mon("MAGMAR", "CHARCOAL", "FLAME_BODY", ["FLAMETHROWER", "THUNDER_PUNCH", "PSYCHIC", "CONFUSE_RAY"]),
            mon("ARCANINE", "LUM_BERRY", "INTIMIDATE", ["FIRE_FANG", "THUNDER_FANG", "BODY_SLAM", "ROAR"]),
            mon("HOUNDOOM", None, "FLASH_FIRE", ["FLAMETHROWER", "SHADOW_BALL", "SLUDGE_BOMB", "NASTY_PLOT"],
                mega=True, stone="HOUNDOOMINITE"),
        ]),
        "duo_a": [1, 3, 4],
        "partner": trainer("ASHTON", "KINDLER", "KINDLER", M, "HIKER", [
            mon("COMBUSKEN", "SCOPE_LENS", "BLAZE"),
            mon("QUILAVA", "SHELL_BELL", "BLAZE"),
            mon("FLAREON", "LEFTOVERS", "FLASH_FIRE"),
        ]),
        "intro": r"Welcome to floor 30! I'm\nFLANNERY, and I'll show you\pa scorching battle!",
        "defeat": r"Oh... I guess I was trying\ntoo hard. You win!",
        "defeat_b": r"Not even my flames could\nkeep up with you!",
        "victory": r"Whew! That was hot! Cool\noff and come back again!",
    },
    40: {
        "battle_music": "MUS_VS_GYM_LEADER",
        "pic_source": "Emerald",
        "solo": trainer("NORMAN", "LEADER", "LEADER_NORMAN", M, "MALE", [
            mon("LINOONE", "SILK_SCARF", "GLUTTONY", ["DOUBLE_EDGE", "PLAY_ROUGH", "DIG", "THUNDER_WAVE"]),
            mon("SWELLOW", "SHARP_BEAK", "GUTS", ["BRAVE_BIRD", "FACADE", "STEEL_WING", "QUICK_ATTACK"]),
            mon("URSARING", "QUICK_CLAW", "GUTS", ["BODY_SLAM", "EARTHQUAKE", "HAMMER_ARM", "SWORDS_DANCE"]),
            mon("KANGASKHAN", None, "SCRAPPY", ["BODY_SLAM", "CRUNCH", "EARTHQUAKE", "ICE_PUNCH"],
                mega=True, stone="KANGASKHANITE"),
            mon("SLAKING", "SHELL_BELL", "TRUANT", ["DOUBLE_EDGE", "EARTHQUAKE", "FIRE_PUNCH", "SLACK_OFF"]),
        ]),
        "duo_a": [2, 3, 4],
        "partner": trainer("WALLY", "RIVAL", "WALLY", M, "MALE", [
            mon("ALTARIA", "LEFTOVERS", "NATURAL_CURE", ["DRAGON_CLAW", "EARTHQUAKE", "DRAGON_DANCE", "AERIAL_ACE"]),
            mon("MAGNETON", "MAGNET", "STURDY", ["THUNDERBOLT", "FLASH_CANNON", "TRI_ATTACK", "THUNDER_WAVE"]),
            mon("GARDEVOIR", "TWISTED_SPOON", "TRACE", ["PSYCHIC", "MOONBLAST", "THUNDERBOLT", "CALM_MIND"]),
        ]),
        "intro": r"I'm NORMAN, PETALBURG's\nLEADER. I won't hold back,\peven against you!",
        "defeat": r"I... I can't believe it.\nYou've grown so strong.",
        "defeat_b": r"I'm not done yet! I'll\nkeep getting stronger!",
        "victory": r"A father's strength is not\nso easily overcome!",
    },
    50: {
        "battle_music": "MUS_VS_CHAMPION",
        "pic_source": "Emerald",
        "solo": trainer("WALLACE", "CHAMPION", "CHAMPION_WALLACE", M, "MALE", [
            mon("WAILORD", "CHOICE_SPECS", "WATER_VEIL", ["WATER_SPOUT", "SURF", "ICE_BEAM", "EARTHQUAKE"],
                "QUIET", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("TENTACRUEL", "BLACK_SLUDGE", "CLEAR_BODY", ["SLUDGE_BOMB", "SURF", "KNOCK_OFF", "TOXIC_SPIKES"],
                "HASTY", {"HP": 4, "SpA": 252, "Spe": 252}),
            mon("LUDICOLO", "DAMP_ROCK", "SWIFT_SWIM", ["RAIN_DANCE", "SURF", "GIGA_DRAIN", "ICE_BEAM"],
                "MODEST", {"HP": 4, "SpA": 252, "Spe": 252}),
            mon("WHISCASH", "LUM_BERRY", "HYDRATION", ["DRAGON_DANCE", "EARTHQUAKE", "WATERFALL", "ZEN_HEADBUTT"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("GYARADOS", None, "INTIMIDATE", ["DRAGON_DANCE", "WATERFALL", "CRUNCH", "EARTHQUAKE"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="GYARADOSITE"),
            mon("MILOTIC", "LEFTOVERS", "MARVEL_SCALE", ["SURF", "ICE_BEAM", "RECOVER", "TOXIC"],
                "BOLD", {"HP": 252, "Def": 252, "SpA": 4}),
        ]),
        "duo_a": [1, 4, 5],
        "partner": trainer("JUAN", "LEADER", "LEADER_JUAN", M, "MALE", [
            mon("WALREIN", "LEFTOVERS", "THICK_FAT", ["ICE_BEAM", "SURF", "TOXIC", "PROTECT"],
                "BOLD", {"HP": 252, "Def": 252, "SpA": 4}),
            mon("CRAWDAUNT", "LIFE_ORB", "ADAPTABILITY", ["CRABHAMMER", "KNOCK_OFF", "SUPERPOWER", "SWORDS_DANCE"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("KINGDRA", "CHOICE_SPECS", "SNIPER", ["HYDRO_PUMP", "DRAGON_PULSE", "ICE_BEAM", "SIGNAL_BEAM"],
                "MODEST", {"HP": 4, "SpA": 252, "Spe": 252}),
        ]),
        "intro": r"Welcome. I am WALLACE, the\nCHAMPION. Let us perform\pa battle worthy of this TOWER!",
        "defeat": r"I, the CHAMPION, fall in\ndefeat... Splendid!",
        "defeat_b": r"Ah, such elegance! Your\nvictory is a true work of art.",
        "victory": r"Your elegance needs more\npolish. Return when ready.",
    },
    60: {
        "battle_music": "MUS_VS_GYM_LEADER",
        "pic_source": "Emerald",
        "solo": trainer("WINONA", "LEADER", "LEADER_WINONA", F, "FEMALE", [
            mon("SWELLOW", "FLAME_ORB", "GUTS", ["FACADE", "BRAVE_BIRD", "QUICK_ATTACK", "ROOST"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("PELIPPER", "LEFTOVERS", "RAIN_DISH", ["HURRICANE", "SURF", "ROOST", "TAILWIND"],
                "BOLD", {"HP": 252, "Def": 252, "SpA": 4}),
            mon("TROPIUS", "SITRUS_BERRY", "HARVEST", ["GIGA_DRAIN", "AIR_SLASH", "LEECH_SEED", "SYNTHESIS"],
                "CALM", {"HP": 252, "SpD": 252, "Def": 4}),
            mon("SKARMORY", "ROCKY_HELMET", "STURDY", ["BRAVE_BIRD", "SPIKES", "STEALTH_ROCK", "WHIRLWIND"],
                "IMPISH", {"HP": 252, "Def": 252, "SpD": 4}),
            mon("STARAPTOR", "CHOICE_BAND", "RECKLESS", ["BRAVE_BIRD", "CLOSE_COMBAT", "DOUBLE_EDGE", "QUICK_ATTACK"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("ALTARIA", None, "NATURAL_CURE", ["DRAGON_DANCE", "DOUBLE_EDGE", "EARTHQUAKE", "ROOST"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="ALTARIANITE"),
        ]),
        "duo_a": [3, 4, 5],
        "partner": trainer("SKYLER", "BIRD_KEEPER", "BIRD_KEEPER", M, "COOL", [
            mon("TALONFLAME", "LIFE_ORB", "GALE_WINGS"),
            mon("HONCHKROW", "SCOPE_LENS", "SUPER_LUCK"),
            mon("BRAVIARY", "CHOICE_SCARF", "SHEER_FORCE"),
        ]),
        "intro": r"I am WINONA. I have become\none with bird POKEMON.\pCan you weather my storm?",
        "defeat": r"Never before have I seen a\ntrainer soar so gracefully.",
        "defeat_b": r"My birds were grounded!\nWINONA, forgive me!",
        "victory": r"Your wings need more\nstrength. Try again!",
    },
    70: {
        "battle_music": "MUS_VS_CHAMPION",
        "pic_source": "Emerald",
        "solo": trainer("STEVEN", "CHAMPION", "STEVEN", M, "MALE", [
            mon("SKARMORY", "LEFTOVERS", "STURDY", ["DRILL_PECK", "STEEL_WING", "SPIKES", "WHIRLWIND"],
                "IMPISH", {"HP": 252, "Def": 252, "SpD": 4}),
            mon("CLAYDOL", "LIGHT_CLAY", "LEVITATE", ["EARTH_POWER", "PSYCHIC", "REFLECT", "LIGHT_SCREEN"],
                "CALM", {"HP": 252, "SpD": 252, "Def": 4}),
            mon("AGGRON", "CHOICE_BAND", "ROCK_HEAD", ["HEAD_SMASH", "IRON_HEAD", "EARTHQUAKE", "SUPERPOWER"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("CRADILY", "SITRUS_BERRY", "STORM_DRAIN", ["ROCK_SLIDE", "GIGA_DRAIN", "RECOVER", "STEALTH_ROCK"],
                "SASSY", {"HP": 252, "SpD": 252, "Def": 4}),
            mon("ARMALDO", "LIFE_ORB", "BATTLE_ARMOR", ["X_SCISSOR", "ROCK_SLIDE", "EARTHQUAKE", "SWORDS_DANCE"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("METAGROSS", None, "CLEAR_BODY", ["METEOR_MASH", "ZEN_HEADBUTT", "EARTHQUAKE", "ICE_PUNCH"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="METAGROSSITE"),
        ]),
        "duo_a": [1, 2, 5],
        "partner": trainer("RENZO", "EXPERT", "EXPERT_M", M, "INTENSE", [
            mon("HARIYAMA", "FLAME_ORB", "GUTS"),
            mon("CONKELDURR", "LIFE_ORB", "IRON_FIST"),
            mon("LUCARIO", None, "INNER_FOCUS", mega=True, stone="LUCARIONITE"),
        ]),
        "intro": r"I'm STEVEN. I've heard of\nyour climb. Let me see how\pstrong you've become!",
        "defeat": r"I, STEVEN, have been beaten\nby you... Impressive.",
        "defeat_b": r"You fight like a seasoned\nmaster! Well done!",
        "victory": r"Steel bends, but it doesn't\nbreak. Train and return!",
    },
    75: {
        "battle_music": "MUS_VS_FRONTIER_BRAIN",
        "pic_source": "Emerald",
        "solo": trainer("BRANDON", "PYRAMID_KING", "PYRAMID_KING_BRANDON", M, "MALE", [
            mon("REGIROCK", "LEFTOVERS", "STURDY", ["STONE_EDGE", "EARTHQUAKE", "SUPERPOWER", "THUNDER_WAVE"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("REGICE", "SITRUS_BERRY", "CLEAR_BODY", ["ICE_BEAM", "THUNDERBOLT", "THUNDER_WAVE", "AMNESIA"],
                "MODEST", {"HP": 252, "SpA": 252, "Def": 4}),
            mon("REGISTEEL", "ROCKY_HELMET", "CLEAR_BODY", ["IRON_HEAD", "SEISMIC_TOSS", "TOXIC", "CURSE"],
                "IMPISH", {"HP": 252, "Def": 252, "SpD": 4}),
            mon("GARCHOMP", "LIFE_ORB", "ROUGH_SKIN", ["SWORDS_DANCE", "EARTHQUAKE", "OUTRAGE", "FIRE_FANG"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("EXCADRILL", "EXPERT_BELT", "SAND_RUSH", ["EARTHQUAKE", "ROCK_SLIDE", "METAL_CLAW", "SWORDS_DANCE"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("TYRANITAR", None, "SAND_STREAM", ["DRAGON_DANCE", "STONE_EDGE", "CRUNCH", "EARTHQUAKE"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="TYRANITARITE"),
        ]),
        "duo_a": [0, 3, 5],
        "partner": trainer("LUCY", "PIKE_QUEEN", "PIKE_QUEEN_LUCY", F, "MALE", [
            mon("SEVIPER", "BLACK_SLUDGE", "INFILTRATOR", ["SLUDGE_BOMB", "FLAMETHROWER", "GIGA_DRAIN", "GLARE"],
                "MODEST", {"HP": 4, "SpA": 252, "Spe": 252}),
            mon("SHUCKLE", "MENTAL_HERB", "STURDY", ["STICKY_WEB", "TOXIC", "ENCORE", "ROCK_SLIDE"],
                "CAREFUL", {"HP": 252, "SpD": 252, "Def": 4}),
            mon("MILOTIC", "LEFTOVERS", "COMPETITIVE", ["SURF", "ICE_BEAM", "DRAGON_PULSE", "RECOVER"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
        ]),
        "intro": r"Hahahah! I am BRANDON, the\nPYRAMID KING! Behold the\ppower of the legendary REGIS!",
        "defeat": r"Hahahah! Superb! You have\nmy respect, challenger!",
        "defeat_b": r"...You win. Don't expect\nme to say it twice.",
        "victory": r"Hahahah! Your courage is\nfine, but your skill lacks!",
    },
    80: {
        "battle_music": "MUS_VS_ELITE_FOUR",
        "pic_source": "Emerald",
        "solo": trainer("DRAKE", "ELITE_FOUR", "ELITE_FOUR_DRAKE", M, "ELITE_FOUR", [
            mon("ALTARIA", "LEFTOVERS", "NATURAL_CURE", ["DRAGON_PULSE", "MOONBLAST", "ROOST", "TOXIC"],
                "CALM", {"HP": 252, "SpD": 252, "Def": 4}),
            mon("KINGDRA", "LUM_BERRY", "SNIPER", ["DRAGON_DANCE", "WATERFALL", "OUTRAGE", "BODY_SLAM"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("FLYGON", "CHOICE_SCARF", "LEVITATE", ["EARTHQUAKE", "DRAGON_CLAW", "ROCK_SLIDE", "FIRE_PUNCH"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("HAXORUS", "LIFE_ORB", "MOLD_BREAKER", ["DRAGON_DANCE", "OUTRAGE", "EARTHQUAKE", "BRICK_BREAK"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("DRAGONITE", "WEAKNESS_POLICY", "MULTISCALE", ["DRAGON_DANCE", "OUTRAGE", "EXTREME_SPEED",
                                                               "EARTHQUAKE"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("SALAMENCE", None, "INTIMIDATE", ["DRAGON_DANCE", "DOUBLE_EDGE", "EARTHQUAKE", "DRAGON_CLAW"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="SALAMENCITE"),
        ]),
        "duo_a": [3, 4, 5],
        "partner": trainer("RYUJI", "DRAGON_TAMER", "DRAGON_TAMER", M, "INTENSE", [
            mon("HYDREIGON", "CHOICE_SPECS", "LEVITATE"),
            mon("GOODRA", "ASSAULT_VEST", "SAP_SIPPER"),
            mon("GARCHOMP", None, "ROUGH_SKIN", mega=True, stone="GARCHOMPITE"),
        ]),
        "intro": r"I am DRAKE of the ELITE\nFOUR! Do you understand\pwhat a dragon truly needs?",
        "defeat": r"Superb, it should be said.\nYou have earned this floor!",
        "defeat_b": r"My dragons were no match\nfor your spirit!",
        "victory": r"Without a heart of steel,\nno dragon can be tamed!",
    },
    90: {
        "battle_music": "MUS_RG_VS_CHAMPION",
        "pic_source": "FRLG",
        "solo": trainer("BLUE", "CHAMPION", "CHAMPION_RIVAL_FRLG", M, "MALE", [
            mon("PIDGEOT", "LIFE_ORB", "BIG_PECKS", ["BRAVE_BIRD", "DOUBLE_EDGE", "QUICK_ATTACK", "ROOST"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("RHYPERIOR", "ASSAULT_VEST", "SOLID_ROCK", ["EARTHQUAKE", "STONE_EDGE", "MEGAHORN", "ICE_PUNCH"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("EXEGGUTOR", "SITRUS_BERRY", "HARVEST", ["LEAF_STORM", "PSYCHIC", "SLUDGE_BOMB", "SYNTHESIS"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("GYARADOS", "LEFTOVERS", "INTIMIDATE", ["DRAGON_DANCE", "WATERFALL", "EARTHQUAKE", "TAUNT"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("ARCANINE", "CHOICE_BAND", "INTIMIDATE", ["FLARE_BLITZ", "EXTREME_SPEED", "CLOSE_COMBAT",
                                                           "THUNDER_FANG"],
                "ADAMANT", {"HP": 4, "Atk": 252, "Spe": 252}),
            mon("ALAKAZAM", None, "MAGIC_GUARD", ["PSYCHIC", "SHADOW_BALL", "CALM_MIND", "RECOVER"],
                "TIMID", {"HP": 4, "SpA": 252, "Spe": 252}, mega=True, stone="ALAKAZITE"),
        ]),
        "duo_a": [3, 4, 5],
        "partner": trainer("TRISTA", "COOLTRAINER_FRLG", "COOLTRAINER_F_FRLG", F, "COOL", [
            mon("STARMIE", "LIFE_ORB", "NATURAL_CURE"),
            mon("JOLTEON", "CHOICE_SPECS", "VOLT_ABSORB"),
            mon("GENGAR", None, "LEVITATE", mega=True, stone="GENGARITE"),
        ]),
        "intro": r"So you finally made it!\nI'm BLUE! I'll show you\pwho the real champion is!",
        "defeat": r"What? Unbelievable!\nI picked the wrong POKEMON!",
        "defeat_b": r"BLUE won't let me hear the\nend of this one!",
        "victory": r"Smell ya later! Come back\nwhen you're actually good!",
    },
    100: {
        "battle_music": "MUS_RG_VS_CHAMPION",
        "pic_source": "FRLG",
        "solo": trainer("RED", "CHAMPION", "RED", M, "MALE", [
            mon("PIKACHU", "LIGHT_BALL", "LIGHTNING_ROD", ["THUNDERBOLT", "SURF", "ENCORE", "THUNDER_WAVE"],
                "TIMID", {"HP": 4, "SpA": 252, "Spe": 252}),
            mon("LAPRAS", "ASSAULT_VEST", "WATER_ABSORB", ["SURF", "FREEZE_DRY", "THUNDERBOLT", "PSYCHIC"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("SNORLAX", "LEFTOVERS", "THICK_FAT", ["BODY_SLAM", "CRUNCH", "EARTHQUAKE", "CURSE"],
                "ADAMANT", {"HP": 252, "Atk": 252, "SpD": 4}),
            mon("VENUSAUR", "BLACK_SLUDGE", "CHLOROPHYLL", ["GIGA_DRAIN", "SLUDGE_BOMB", "SLEEP_POWDER", "SYNTHESIS"],
                "BOLD", {"HP": 252, "Def": 252, "SpA": 4}),
            mon("BLASTOISE", "CHOICE_SPECS", "RAIN_DISH", ["HYDRO_PUMP", "ICE_BEAM", "AURA_SPHERE", "DRAGON_PULSE"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("CHARIZARD", None, "BLAZE", ["DRAGON_DANCE", "FLARE_BLITZ", "DRAGON_CLAW", "EARTHQUAKE"],
                "JOLLY", {"HP": 4, "Atk": 252, "Spe": 252}, mega=True, stone="CHARIZARDITE_X"),
        ]),
        "duo_a": [0, 2, 5],
        "partner": trainer("LEAF", "RIVAL", "LEAF", F, "FEMALE", [
            mon("LAPRAS", "LEFTOVERS", "WATER_ABSORB", ["HYDRO_PUMP", "FREEZE_DRY", "THUNDERBOLT", "CONFUSE_RAY"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("VENUSAUR", "BLACK_SLUDGE", "CHLOROPHYLL", ["LEAF_STORM", "SLUDGE_BOMB", "SLEEP_POWDER",
                                                              "LEECH_SEED"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}),
            mon("BLASTOISE", None, "RAIN_DISH", ["WATER_PULSE", "AURA_SPHERE", "DRAGON_PULSE", "ICE_BEAM"],
                "MODEST", {"HP": 252, "SpA": 252, "SpD": 4}, mega=True, stone="BLASTOISINITE"),
        ]),
        "intro": r"......\p......!",
        "defeat": r"......!",
        "defeat_b": r"Wow, you beat us both!\nYou're the real champion!",
        "victory": r"......",
    },
}

# What the design table asked for and what was built instead, with the reason (FLOORS.md lists them).
DEVIATIONS = [
    "Floor 90 Blue: class CHAMPION (Emerald's), not CHAMPION_FRLG: battle_message.c prints the rival's name for "
    "RIVAL_EARLY_FRLG, RIVAL_LATE_FRLG and CHAMPION_FRLG alike, so only a non-FRLG class keeps the name BLUE.",
    "Floor 90 Blue: the Mega is Alakazam (Alakazite, the design allows Pidgeot or Alakazam); Pidgeot leads without "
    "a stone (one Mega per trainer).",
    "Floor 100 Red: Charizard holds Charizardite X (the Drought of Mega Y would weaken Red's own Blastoise and Lapras "
    "and Leaf's Duo Blastoise).",
    "Floor 100 Leaf: class RIVAL (prints 'PKMN TRAINER LEAF', the house data's own class for LEAF); her Mega is "
    "Blastoise (Blastoisinite).",
    "Floor 70 Steven: class CHAMPION as in the design table (Emerald's own trainer uses RIVAL, 'PKMN TRAINER').",
    "Floor 10 Brock: Kabuto knows only three moves at level 10 (Scratch, Absorb, Harden are all its level-up moves).",
    "Floor 75 Brandon: the three strong members are Garchomp, Excadrill and Mega Tyranitar, a sandstorm team around "
    "the Regis (Tyranitar's Sand Stream boosts Regirock's Sp. Def and Excadrill's Sand Rush).",
    "Floor 75 Lucy and Brandon use the house encounter music of src/data/trainers.party (Male) for the Frontier "
    "Brains.",
]
