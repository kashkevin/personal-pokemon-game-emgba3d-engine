#include "global.h"
#include "test/battle.h"

// EmeraldTogether free-for-all (BATTLE_TYPE_FFA): four trainers, one Pokemon each, every Pokemon on a side of its own.
// battler 0 = PLAYER (playerLeft), 1 = OPPONENT_A (opponentLeft), 2 = PARTNER (playerRight), 3 = OPPONENT_B (opponentRight).

FFA_BATTLE_TEST("FFA: every trainer is a foe to every other trainer")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT(!IsBattlerAlly(0, 1));
        EXPECT(!IsBattlerAlly(0, 2));
        EXPECT(!IsBattlerAlly(0, 3));
        EXPECT(!IsBattlerAlly(1, 2));
        EXPECT(!IsBattlerAlly(1, 3));
        EXPECT(!IsBattlerAlly(2, 3));
        EXPECT(IsBattlerAlly(2, 2));
    }
}

FFA_BATTLE_TEST("FFA: a trainer can hit the Pokemon beside them, not only the opposite column")
{
    GIVEN {
        ASSUME(GetMovePower(MOVE_TACKLE) > 0);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_TACKLE, target: playerRight); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_TACKLE, playerLeft);
        HP_BAR(playerRight);
    } THEN {
        EXPECT_LT(playerRight->hp, playerRight->maxHP);
        EXPECT_EQ(playerLeft->hp, playerLeft->maxHP);
        EXPECT_EQ(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
    }
}
