#include "global.h"
#include "test/battle.h"

// The 2026-10-27 item audit: key items that work from the Bag in battle, checked against the Gen 6 rules this game is set
// to. The Exp. Charm (battle_script_commands.c ApplyExperienceMultipliers) as the expansion's Lucky Egg test checks its
// held boost; the co-op battles' rewards take the same path (--selftest-coop-exp compares them with and without it).

WILD_BATTLE_TEST("Exp. Charm in the Bag: the experience gained is half again as much", s32 exp)
{
    bool32 charm = FALSE;

    PARAMETRIZE { charm = TRUE; }
    PARAMETRIZE { charm = FALSE; }

    GIVEN {
        if (charm)
            GIVE_PLAYER_ITEM(ITEM_EXP_CHARM, 1);
        PLAYER(SPECIES_WOBBUFFET) { Level(20); }
        OPPONENT(SPECIES_CATERPIE) { Level(10); HP(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        MESSAGE("Wobbuffet used Scratch!");
        MESSAGE("The wild Caterpie fainted!");
        EXPERIENCE_BAR(player, captureGainedExp: &results[i].exp);
    } FINALLY {
        EXPECT_MUL_EQ(results[1].exp, Q_4_12(1.5), results[0].exp);
    }
}

WILD_BATTLE_TEST("Exp. Charm and a held Lucky Egg: both boosts apply", s32 exp)
{
    bool32 charm = FALSE;

    PARAMETRIZE { charm = TRUE; }
    PARAMETRIZE { charm = FALSE; }

    GIVEN {
        if (charm)
            GIVE_PLAYER_ITEM(ITEM_EXP_CHARM, 1);
        PLAYER(SPECIES_WOBBUFFET) { Level(20); Item(ITEM_LUCKY_EGG); }
        OPPONENT(SPECIES_CATERPIE) { Level(10); HP(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        MESSAGE("Wobbuffet used Scratch!");
        MESSAGE("The wild Caterpie fainted!");
        EXPERIENCE_BAR(player, captureGainedExp: &results[i].exp);
    } FINALLY {
        EXPECT_MUL_EQ(results[1].exp, Q_4_12(1.5), results[0].exp);
    }
}
