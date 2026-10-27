#include "global.h"
#include "test/battle.h"

// The 2026-10-27 item audit: what a Poke Ball does to the Pokemon it catches (battle_script_commands.c, after the third
// shake), checked against the Gen 6 rules this game is set to. The catch itself is made sure of as capture.c's tests do.

WILD_BATTLE_TEST("Heal Ball: the Pokemon caught in it has its HP, status and PP restored")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CATERPIE) { HP(1); Status1(STATUS1_PARALYSIS); MovesWithPP({MOVE_TACKLE, 3}); }
    } WHEN {
        TURN { USE_ITEM(player, ITEM_HEAL_BALL, WITH_RNG(RNG_BALLTHROW_SHAKE, 0)); }
    } SCENE {
        ONE_OF
        {
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_CRITICAL_CAPTURE_THROW);
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_BALL_THROW);
        }
    } THEN {
        struct Pokemon *caught = &gParties[B_TRAINER_PLAYER][1];

        EXPECT_EQ(GetMonData(caught, MON_DATA_SPECIES), SPECIES_CATERPIE);
        EXPECT_EQ(GetMonData(caught, MON_DATA_POKEBALL), BALL_HEAL);
        EXPECT_EQ(GetMonData(caught, MON_DATA_HP), GetMonData(caught, MON_DATA_MAX_HP));
        EXPECT_EQ(GetMonData(caught, MON_DATA_STATUS), STATUS1_NONE);
        EXPECT_EQ(GetMonData(caught, MON_DATA_PP1), GetMovePP(MOVE_TACKLE));
    }
}

WILD_BATTLE_TEST("A Poke Ball leaves the Pokemon it catches as it was: HP, status and PP")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CATERPIE) { HP(1); Status1(STATUS1_PARALYSIS); MovesWithPP({MOVE_TACKLE, 3}); }
    } WHEN {
        TURN { USE_ITEM(player, ITEM_POKE_BALL, WITH_RNG(RNG_BALLTHROW_SHAKE, 0)); }
    } SCENE {
        ONE_OF
        {
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_CRITICAL_CAPTURE_THROW);
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_BALL_THROW);
        }
    } THEN {
        struct Pokemon *caught = &gParties[B_TRAINER_PLAYER][1];

        EXPECT_EQ(GetMonData(caught, MON_DATA_POKEBALL), BALL_POKE);
        EXPECT_EQ(GetMonData(caught, MON_DATA_HP), 1);
        EXPECT_EQ(GetMonData(caught, MON_DATA_STATUS), STATUS1_PARALYSIS);
        EXPECT_EQ(GetMonData(caught, MON_DATA_PP1), 3);
    }
}

WILD_BATTLE_TEST("Friend Ball: the Pokemon caught in it starts with 200 friendship (Gen 6), a Poke Ball's with its own", u32 friendship)
{
    enum Item ball;

    PARAMETRIZE { ball = ITEM_FRIEND_BALL; }
    PARAMETRIZE { ball = ITEM_POKE_BALL; }
    GIVEN {
        ASSUME(gSpeciesInfo[SPECIES_CATERPIE].friendship != 200);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CATERPIE);
    } WHEN {
        TURN { USE_ITEM(player, ball, WITH_RNG(RNG_BALLTHROW_SHAKE, 0)); }
    } SCENE {
        ONE_OF
        {
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_CRITICAL_CAPTURE_THROW);
            ANIMATION(ANIM_TYPE_SPECIAL, B_ANIM_BALL_THROW);
        }
    } THEN {
        results[i].friendship = GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_FRIENDSHIP);
    } FINALLY {
        EXPECT_EQ(results[0].friendship, B_FRIEND_BALL_MODIFIER >= GEN_8 ? 150 : 200);
        EXPECT_EQ(results[1].friendship, gSpeciesInfo[SPECIES_CATERPIE].friendship);
    }
}
