#include "global.h"
#include "test/battle.h"

// EmeraldTogether free-for-all: fainting, replacement, elimination and the winner.
// battler 0 = PLAYER (playerLeft), 1 = OPPONENT_A (opponentLeft), 2 = PARTNER (playerRight), 3 = OPPONENT_B (opponentRight).

FFA_BATTLE_TEST("FFA: a trainer with no Pokemon left is out, the others fight on")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_TACKLE, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_TACKLE, target: playerLeft); }
    } SCENE {
        MESSAGE("The opposing Wobbuffet fainted!");
        ANIMATION(ANIM_TYPE_MOVE, MOVE_TACKLE, opponentRight);
        HP_BAR(playerLeft);
    } THEN {
        EXPECT(gAbsentBattlerFlags & (1u << 1));
    }
}

FFA_BATTLE_TEST("FFA: a trainer with another Pokemon sends it in, and stays in")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_A(SPECIES_WYNAUT);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_TACKLE, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE);
               SEND_OUT(opponentLeft, 1); }
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(opponentLeft->species, SPECIES_WYNAUT);
        EXPECT(!(gAbsentBattlerFlags & (1u << 1)));
    }
}

FFA_BATTLE_TEST("FFA: the last trainer standing wins")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET) { HP(1); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SWIFT); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(gBattleOutcome, B_OUTCOME_WON);
    }
}

FFA_BATTLE_TEST("FFA: a trainer who loses their last Pokemon to another trainer's win has lost")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET) { HP(1); }
        PARTNER(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_A(SPECIES_WOBBUFFET) { HP(1); }
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_SWIFT); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(gBattleOutcome, B_OUTCOME_LOST);
    }
}
