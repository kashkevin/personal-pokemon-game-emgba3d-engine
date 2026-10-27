#include "global.h"
#include "test/battle.h"

// The 2026-10-27 item audit: held items that no test of the expansion names (their own files are still "to do"
// upstream), checked against the Gen 6 rules this game is set to.

static const u16 sPlateMoveTable[][3] =
{
    { TYPE_FIGHTING, MOVE_KARATE_CHOP,     ITEM_FIST_PLATE },
    { TYPE_FLYING,   MOVE_WING_ATTACK,     ITEM_SKY_PLATE },
    { TYPE_POISON,   MOVE_POISON_STING,    ITEM_TOXIC_PLATE },
    { TYPE_GROUND,   MOVE_MUD_SHOT,        ITEM_EARTH_PLATE },
    { TYPE_ROCK,     MOVE_ROCK_THROW,      ITEM_STONE_PLATE },
    { TYPE_BUG,      MOVE_BUG_BITE,        ITEM_INSECT_PLATE },
    { TYPE_GHOST,    MOVE_SHADOW_PUNCH,    ITEM_SPOOKY_PLATE },
    { TYPE_STEEL,    MOVE_METAL_CLAW,      ITEM_IRON_PLATE },
    { TYPE_FIRE,     MOVE_EMBER,           ITEM_FLAME_PLATE },
    { TYPE_WATER,    MOVE_WATER_GUN,       ITEM_SPLASH_PLATE },
    { TYPE_GRASS,    MOVE_VINE_WHIP,       ITEM_MEADOW_PLATE },
    { TYPE_ELECTRIC, MOVE_THUNDER_SHOCK,   ITEM_ZAP_PLATE },
    { TYPE_PSYCHIC,  MOVE_CONFUSION,       ITEM_MIND_PLATE },
    { TYPE_ICE,      MOVE_AURORA_BEAM,     ITEM_ICICLE_PLATE },
    { TYPE_DRAGON,   MOVE_DRAGON_BREATH,   ITEM_DRACO_PLATE },
    { TYPE_DARK,     MOVE_BITE,            ITEM_DREAD_PLATE },
    { TYPE_FAIRY,    MOVE_DISARMING_VOICE, ITEM_PIXIE_PLATE },
};

SINGLE_BATTLE_TEST("Plates increase the power of their type's moves by 20%", s16 damage)
{
    enum Move move = MOVE_NONE;
    enum Item item = ITEM_NONE;
    enum Type type = TYPE_NONE;

    for (u32 j = 0; j < ARRAY_COUNT(sPlateMoveTable); j++) {
        PARAMETRIZE { type = sPlateMoveTable[j][0]; move = sPlateMoveTable[j][1]; item = ITEM_NONE; }
        PARAMETRIZE { type = sPlateMoveTable[j][0]; move = sPlateMoveTable[j][1]; item = sPlateMoveTable[j][2]; }
    }

    GIVEN {
        ASSUME(GetMovePower(move) > 0);
        ASSUME(GetMoveType(move) == type);
        if (item != ITEM_NONE) {
            ASSUME(GetItemHoldEffect(item) == HOLD_EFFECT_PLATE);
            ASSUME(GetItemSecondaryId(item) == type);
        }
        PLAYER(SPECIES_WOBBUFFET) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, move); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &results[i].damage);
    } FINALLY {
        for (u32 j = 0; j < ARRAY_COUNT(sPlateMoveTable); j++)
            EXPECT_MUL_EQ(results[j*2].damage, Q_4_12(1.2), results[(j*2)+1].damage);
    }
}

SINGLE_BATTLE_TEST("Light Clay makes Reflect last 8 turns")
{
    s16 damage[9];
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_LIGHT_CLAY) == HOLD_EFFECT_LIGHT_CLAY);
        ASSUME(GetMoveCategory(MOVE_SCRATCH) == DAMAGE_CATEGORY_PHYSICAL);
        PLAYER(SPECIES_WOBBUFFET) { Item(ITEM_LIGHT_CLAY); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_REFLECT); MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
        TURN { MOVE(opponent, MOVE_SCRATCH); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_REFLECT, player);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[0]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[1]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[2]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[3]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[4]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[5]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[6]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[7]);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, opponent);
        HP_BAR(player, captureDamage: &damage[8]);
    } THEN {
        for (u32 j = 1; j < 8; j++)
            EXPECT_MUL_EQ(damage[0], Q_4_12(1.0), damage[j]);
        EXPECT_LT(damage[0], damage[8]);
    }
}

SINGLE_BATTLE_TEST("Wide Lens raises the holder's accuracy by a tenth")
{
    PASSES_RANDOMLY(60, 100, RNG_ACCURACY); // Sing's 55 x 1.1, rounded down
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_WIDE_LENS) == HOLD_EFFECT_WIDE_LENS);
        ASSUME(GetMoveAccuracy(MOVE_SING) == 55);
        PLAYER(SPECIES_WOBBUFFET) { Item(ITEM_WIDE_LENS); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SING); }
    } SCENE {
        STATUS_ICON(opponent, sleep: TRUE);
    }
}

SINGLE_BATTLE_TEST("Lax Incense lowers the accuracy of moves against its holder by a tenth (Gen 4+)")
{
    PASSES_RANDOMLY(49, 100, RNG_ACCURACY); // Sing's 55 x 0.9, rounded down
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_LAX_INCENSE) == HOLD_EFFECT_EVASION_UP);
        ASSUME(GetMoveAccuracy(MOVE_SING) == 55);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { Item(ITEM_LAX_INCENSE); }
    } WHEN {
        TURN { MOVE(player, MOVE_SING); }
    } SCENE {
        STATUS_ICON(opponent, sleep: TRUE);
    }
}

SINGLE_BATTLE_TEST("Soul Dew raises the special moves of Latias and Latios by half, no other Pokemon's (Gen 6)", s16 damage)
{
    enum Species species = SPECIES_NONE;
    enum Item item = ITEM_NONE;

    PARAMETRIZE { species = SPECIES_LATIOS; item = ITEM_NONE; }
    PARAMETRIZE { species = SPECIES_LATIOS; item = ITEM_SOUL_DEW; }
    PARAMETRIZE { species = SPECIES_WOBBUFFET; item = ITEM_NONE; }
    PARAMETRIZE { species = SPECIES_WOBBUFFET; item = ITEM_SOUL_DEW; }

    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_SOUL_DEW) == HOLD_EFFECT_SOUL_DEW);
        ASSUME(B_SOUL_DEW_BOOST < GEN_7);
        ASSUME(GetMoveCategory(MOVE_SWIFT) == DAMAGE_CATEGORY_SPECIAL);
        PLAYER(species) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SWIFT); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &results[i].damage);
    } FINALLY {
        EXPECT_MUL_EQ(results[0].damage, Q_4_12(1.5), results[1].damage);
        EXPECT_EQ(results[2].damage, results[3].damage);
    }
}

SINGLE_BATTLE_TEST("Figy, Wiki, Mago, Aguav and Iapapa Berries restore 1/8 of max HP at half HP (Gen 6)")
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_FIGY_BERRY; }
    PARAMETRIZE { item = ITEM_WIKI_BERRY; }
    PARAMETRIZE { item = ITEM_MAGO_BERRY; }
    PARAMETRIZE { item = ITEM_AGUAV_BERRY; }
    PARAMETRIZE { item = ITEM_IAPAPA_BERRY; }

    GIVEN {
        ASSUME(GetItemHoldEffect(item) == HOLD_EFFECT_CONFUSE_FLAVOR);
        ASSUME(B_CONFUSE_BERRIES_HEAL < GEN_7);
        ASSUME(GetMoveEffect(MOVE_SUPER_FANG) == EFFECT_FIXED_PERCENT_DAMAGE);
        PLAYER(SPECIES_WOBBUFFET) { MaxHP(80); HP(41); Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_SUPER_FANG); }
    } SCENE {
        HP_BAR(player, damage: 20);
        HP_BAR(player, damage: -10);
    } THEN {
        EXPECT_EQ(player->hp, 31);
        EXPECT_EQ(player->item, ITEM_NONE);
    }
}

SINGLE_BATTLE_TEST("Figy Berry and the others are not eaten above half HP (Gen 6)")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FIGY_BERRY) == HOLD_EFFECT_CONFUSE_FLAVOR);
        ASSUME(B_CONFUSE_BERRIES_HEAL < GEN_7);
        PLAYER(SPECIES_WOBBUFFET) { MaxHP(81); HP(81); Item(ITEM_FIGY_BERRY); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_SUPER_FANG); }
    } SCENE {
        HP_BAR(player, damage: 40);
        NOT HP_BAR(player);
    } THEN {
        EXPECT_EQ(player->hp, 41); // above half of 81
        EXPECT_EQ(player->item, ITEM_FIGY_BERRY);
    }
}

SINGLE_BATTLE_TEST("Macho Brace and the Power items halve the holder's Speed")
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_NONE; }
    PARAMETRIZE { item = ITEM_MACHO_BRACE; }
    PARAMETRIZE { item = ITEM_POWER_WEIGHT; }
    PARAMETRIZE { item = ITEM_POWER_BRACER; }
    PARAMETRIZE { item = ITEM_POWER_BELT; }
    PARAMETRIZE { item = ITEM_POWER_LENS; }
    PARAMETRIZE { item = ITEM_POWER_BAND; }
    PARAMETRIZE { item = ITEM_POWER_ANKLET; }

    GIVEN {
        if (item != ITEM_NONE)
            ASSUME(GetItemHoldEffect(item) == HOLD_EFFECT_MACHO_BRACE || GetItemHoldEffect(item) == HOLD_EFFECT_POWER_ITEM);
        PLAYER(SPECIES_WOBBUFFET) { Speed(100); Item(item); }
        OPPONENT(SPECIES_WOBBUFFET) { Speed(60); }
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); MOVE(opponent, MOVE_CELEBRATE); }
    } SCENE {
        if (item == ITEM_NONE) {
            ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, player);
            ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, opponent);
        } else {
            ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, opponent);
            ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, player);
        }
    }
}

SINGLE_BATTLE_TEST("Focus Sash lets a holder at full HP survive a hit that would knock it out, and is used up")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FOCUS_SASH) == HOLD_EFFECT_FOCUS_SASH);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { MaxHP(10); HP(10); Item(ITEM_FOCUS_SASH); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        HP_BAR(opponent);
        MESSAGE("The opposing Wobbuffet hung on using its Focus Sash!");
    } THEN {
        EXPECT_EQ(opponent->hp, 1);
        EXPECT_EQ(opponent->item, ITEM_NONE);
    }
}

SINGLE_BATTLE_TEST("Focus Sash does nothing for a holder below full HP")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FOCUS_SASH) == HOLD_EFFECT_FOCUS_SASH);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { MaxHP(10); HP(9); Item(ITEM_FOCUS_SASH); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); SEND_OUT(opponent, 1); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        NONE_OF { MESSAGE("The opposing Wobbuffet hung on using its Focus Sash!"); }
        MESSAGE("The opposing Wobbuffet fainted!");
    }
}

SINGLE_BATTLE_TEST("Focus Band lets its holder survive a hit that would knock it out, below full HP too, and is not used up")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FOCUS_BAND) == HOLD_EFFECT_FOCUS_BAND);
        ASSUME(GetItemHoldEffectParam(ITEM_FOCUS_BAND) == 10); // its chance: an untagged Random() % 100 below this (battle_util.c)
        RNGSeed(((rng_value_t){ .ctr = 1 })); // a seed whose roll succeeds, as in final_gambit.c's Focus Band test
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { MaxHP(10); HP(9); Item(ITEM_FOCUS_BAND); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        HP_BAR(opponent, hp: 1);
        MESSAGE("The opposing Wobbuffet hung on using its Focus Band!");
    } THEN {
        EXPECT_EQ(opponent->hp, 1);
        EXPECT_EQ(opponent->item, ITEM_FOCUS_BAND);
    }
}

SINGLE_BATTLE_TEST("On the same seed a Pokemon without a Focus Band faints")
{
    GIVEN {
        RNGSeed(((rng_value_t){ .ctr = 1 }));
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { MaxHP(10); HP(9); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); SEND_OUT(opponent, 1); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        NONE_OF { MESSAGE("The opposing Wobbuffet hung on using its Focus Band!"); }
        MESSAGE("The opposing Wobbuffet fainted!");
    }
}

SINGLE_BATTLE_TEST("Focus Band's holder faints when its roll fails")
{
    GIVEN {
        RNGSeed(((rng_value_t){ .a = 0x9E3779B9, .b = 0x7F4A7C15, .c = 0x85EBCA6B, .ctr = 1 })); // a mixed seed whose roll fails
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { MaxHP(10); HP(9); Item(ITEM_FOCUS_BAND); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); SEND_OUT(opponent, 1); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        NONE_OF { MESSAGE("The opposing Wobbuffet hung on using its Focus Band!"); }
        MESSAGE("The opposing Wobbuffet fainted!");
    }
}

SINGLE_BATTLE_TEST("Flame Orb burns its holder at the end of the turn and is not used up")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FLAME_ORB) == HOLD_EFFECT_FLAME_ORB);
        PLAYER(SPECIES_WOBBUFFET) { Item(ITEM_FLAME_ORB); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, player);
        STATUS_ICON(player, burn: TRUE);
    } THEN {
        EXPECT_NE(player->status1 & STATUS1_BURN, 0);
        EXPECT_EQ(player->item, ITEM_FLAME_ORB);
    }
}

SINGLE_BATTLE_TEST("Flame Orb does not burn a Fire-type holder")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_FLAME_ORB) == HOLD_EFFECT_FLAME_ORB);
        ASSUME(GetSpeciesType(SPECIES_CHARMANDER, 0) == TYPE_FIRE);
        PLAYER(SPECIES_CHARMANDER) { Item(ITEM_FLAME_ORB); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } SCENE {
        NONE_OF { STATUS_ICON(player, burn: TRUE); }
    } THEN {
        EXPECT_EQ(player->status1 & STATUS1_BURN, 0);
    }
}

SINGLE_BATTLE_TEST("Toxic Orb badly poisons its holder at the end of the turn and is not used up")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_TOXIC_ORB) == HOLD_EFFECT_TOXIC_ORB);
        PLAYER(SPECIES_WOBBUFFET) { Item(ITEM_TOXIC_ORB); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, player);
        STATUS_ICON(player, badPoison: TRUE);
    } THEN {
        EXPECT_NE(player->status1 & STATUS1_TOXIC_POISON, 0);
        EXPECT_EQ(player->item, ITEM_TOXIC_ORB);
    }
}

SINGLE_BATTLE_TEST("Toxic Orb does not poison a Steel- or Poison-type holder")
{
    enum Species species = SPECIES_NONE;

    PARAMETRIZE { species = SPECIES_MAGNEMITE; }
    PARAMETRIZE { species = SPECIES_EKANS; }

    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_TOXIC_ORB) == HOLD_EFFECT_TOXIC_ORB);
        ASSUME(GetSpeciesType(SPECIES_MAGNEMITE, 1) == TYPE_STEEL);
        ASSUME(GetSpeciesType(SPECIES_EKANS, 0) == TYPE_POISON);
        PLAYER(species) { Item(ITEM_TOXIC_ORB); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } SCENE {
        NONE_OF { STATUS_ICON(player, badPoison: TRUE); }
    } THEN {
        EXPECT_EQ(player->status1 & STATUS1_PSN_ANY, 0);
    }
}

SINGLE_BATTLE_TEST("Bright Powder lowers the accuracy of moves against its holder by a tenth")
{
    PASSES_RANDOMLY(49, 100, RNG_ACCURACY); // Sing's 55 x 0.9, rounded down
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_BRIGHT_POWDER) == HOLD_EFFECT_EVASION_UP);
        ASSUME(GetMoveAccuracy(MOVE_SING) == 55);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { Item(ITEM_BRIGHT_POWDER); }
    } WHEN {
        TURN { MOVE(player, MOVE_SING); }
    } SCENE {
        STATUS_ICON(opponent, sleep: TRUE);
    }
}

SINGLE_BATTLE_TEST("Amulet Coin and Luck Incense double a trainer battle's prize money when the player's Pokemon holds one")
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_NONE; }
    PARAMETRIZE { item = ITEM_AMULET_COIN; }
    PARAMETRIZE { item = ITEM_LUCK_INCENSE; }

    GIVEN {
        if (item != ITEM_NONE)
            ASSUME(GetItemHoldEffect(item) == HOLD_EFFECT_DOUBLE_PRIZE);
        PLAYER(SPECIES_WOBBUFFET) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ((u32)gBattleStruct->moneyMultiplier, item == ITEM_NONE ? 1u : 2u);
    }
}

DOUBLE_BATTLE_TEST("Amulet Coin doubles the prize money once, however many hold one, and not for the opponent's")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_AMULET_COIN) == HOLD_EFFECT_DOUBLE_PRIZE);
        PLAYER(SPECIES_WOBBUFFET) { Item(ITEM_AMULET_COIN); }
        PLAYER(SPECIES_WYNAUT) { Item(ITEM_AMULET_COIN); }
        OPPONENT(SPECIES_WOBBUFFET) { Item(ITEM_AMULET_COIN); }
        OPPONENT(SPECIES_WYNAUT);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ((u32)gBattleStruct->moneyMultiplier, 2u);
    }
}

SINGLE_BATTLE_TEST("An opponent's Amulet Coin does not double the player's prize money")
{
    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_AMULET_COIN) == HOLD_EFFECT_DOUBLE_PRIZE);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { Item(ITEM_AMULET_COIN); }
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ((u32)gBattleStruct->moneyMultiplier, 1u);
    }
}
