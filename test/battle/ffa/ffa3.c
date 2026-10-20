#include "global.h"
#include "test/battle.h"
#include "battle_ffa.h"

// EmeraldTogether Update 32: the three-player free-for-all (1v1v1) as three linked games play it (battle phase 6): the
// free-for-all's four places with the second player place empty. Every trainer is on a side of their own.
// battler 0 = PLAYER (playerLeft), 1 = OPPONENT_A (opponentLeft), 2 = the empty place (playerRight),
// 3 = OPPONENT_B (opponentRight).

FFA3_BATTLE_TEST("1v1v1: three trainers, each a foe of the two others; the empty place holds nobody")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT(!IsBattlerAlly(0, 1));
        EXPECT(!IsBattlerAlly(0, 3));
        EXPECT(!IsBattlerAlly(1, 3));
        EXPECT(FFA_IsEmptyPlace(2));
        EXPECT(!FFA_IsEmptyPlace(0) && !FFA_IsEmptyPlace(1) && !FFA_IsEmptyPlace(3));
        EXPECT(gAbsentBattlerFlags & (1u << 2));
        EXPECT(!IsBattlerAlive(2));
    }
}

FFA3_BATTLE_TEST("1v1v1: a move aimed at the foes hits the two other trainers and nothing in the empty place")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SWIFT); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SWIFT, playerLeft);
        HP_BAR(opponentLeft);
        HP_BAR(opponentRight);
        NONE_OF { HP_BAR(playerRight); }
    } THEN {
        EXPECT_EQ(playerLeft->hp, playerLeft->maxHP);
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_LT(opponentRight->hp, opponentRight->maxHP);
    }
}

FFA3_BATTLE_TEST("1v1v1: a move that hits everything adjacent hits both other trainers")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SURF) == TARGET_FOES_AND_ALLY);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_SURF); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
        EXPECT_LT(playerLeft->hp, playerLeft->maxHP);
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
    }
}

FFA3_BATTLE_TEST("1v1v1: a single-target move can be aimed at either of the two others")
{
    u32 target;
    PARAMETRIZE { target = 0; }
    PARAMETRIZE { target = 1; }
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        if (target == 0) {
            TURN { MOVE(opponentRight, MOVE_TACKLE, target: playerLeft); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); }
        } else {
            TURN { MOVE(opponentRight, MOVE_TACKLE, target: opponentLeft); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); }
        }
    } THEN {
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
        EXPECT_EQ(playerLeft->hp < playerLeft->maxHP, target == 0);
        EXPECT_EQ(opponentLeft->hp < opponentLeft->maxHP, target == 1);
    }
}

FFA3_BATTLE_TEST("1v1v1: a move with a random target picks one of the two real trainers, never the empty place")
{
    u32 pick;
    PARAMETRIZE { pick = 0; }
    PARAMETRIZE { pick = 1; }
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_OUTRAGE) == TARGET_RANDOM);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_OUTRAGE, WITH_RNG(RNG_RANDOM_TARGET, pick)); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_OUTRAGE, playerLeft);
        NONE_OF { HP_BAR(playerRight); }
    } THEN {
        EXPECT_EQ((opponentLeft->hp < opponentLeft->maxHP) + (opponentRight->hp < opponentRight->maxHP), 1);
        EXPECT_EQ(playerLeft->hp, playerLeft->maxHP);
    }
}

FFA3_BATTLE_TEST("1v1v1: Intimidate lowers the Attack of the two other trainers")
{
    GIVEN {
        PLAYER(SPECIES_ARCANINE) { Ability(ABILITY_INTIMIDATE); }
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(playerLeft->statStages[STAT_ATK], DEFAULT_STAT_STAGE);
        EXPECT_EQ(opponentLeft->statStages[STAT_ATK], DEFAULT_STAT_STAGE - 1);
        EXPECT_EQ(opponentRight->statStages[STAT_ATK], DEFAULT_STAT_STAGE - 1);
    }
}

FFA3_BATTLE_TEST("1v1v1: the trainers move in order of Speed")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { Speed(10); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Speed(30); }
        OPPONENT_B(SPECIES_WOBBUFFET) { Speed(20); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SPLASH); MOVE(opponentLeft, MOVE_SPLASH); MOVE(opponentRight, MOVE_SPLASH); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SPLASH, opponentLeft);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SPLASH, opponentRight);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SPLASH, playerLeft);
    }
}

FFA3_BATTLE_TEST("1v1v1: a trainer who switches keeps their place; the others hit the new Pokemon")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WYNAUT);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { SWITCH(opponentLeft, 1); MOVE(playerLeft, MOVE_TACKLE, target: opponentLeft); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(opponentLeft->species, SPECIES_WYNAUT);
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
    }
}

FFA3_BATTLE_TEST("1v1v1: a trainer with no Pokemon left is out, the two others fight on")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_TACKLE, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_TACKLE, target: playerLeft); }
    } SCENE {
        MESSAGE("2's Wobbuffet fainted!"); // a free-for-all names the trainer (Update 21)
        ANIMATION(ANIM_TYPE_MOVE, MOVE_TACKLE, opponentRight);
        HP_BAR(playerLeft);
    } THEN {
        EXPECT(gAbsentBattlerFlags & (1u << 1));
        EXPECT(gAbsentBattlerFlags & (1u << 2));
    }
}

FFA3_BATTLE_TEST("1v1v1: the last trainer standing wins")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET) { HP(1); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SWIFT); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(gBattleOutcome, B_OUTCOME_WON);
    }
}

FFA3_BATTLE_TEST("1v1v1: a trainer whose last Pokemon faints while another trainer stands has lost")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_SWIFT); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(gBattleOutcome, B_OUTCOME_LOST);
    }
}
