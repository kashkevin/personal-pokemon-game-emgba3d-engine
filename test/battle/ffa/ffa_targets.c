#include "global.h"
#include "test/battle.h"

// EmeraldTogether free-for-all: targets, sides and "allies" (there are none).
// battler 0 = PLAYER (playerLeft), 1 = OPPONENT_A (opponentLeft), 2 = PARTNER (playerRight), 3 = OPPONENT_B (opponentRight).

FFA_BATTLE_TEST("FFA: a move aimed at the foes hits every other trainer, never its user")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SWIFT) == TARGET_BOTH);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SWIFT); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SWIFT, playerLeft);
        HP_BAR(opponentLeft);
        HP_BAR(playerRight);
        HP_BAR(opponentRight);
    } THEN {
        EXPECT_EQ(playerLeft->hp, playerLeft->maxHP);
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_LT(playerRight->hp, playerRight->maxHP);
        EXPECT_LT(opponentRight->hp, opponentRight->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: a move that hits everything adjacent hits all three others")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SURF) == TARGET_FOES_AND_ALLY);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_SURF); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
        EXPECT_LT(playerLeft->hp, playerLeft->maxHP);
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_LT(playerRight->hp, playerRight->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: a single-target move can be aimed at any one of the three others")
{
    u32 target;
    PARAMETRIZE { target = 0; }
    PARAMETRIZE { target = 1; }
    PARAMETRIZE { target = 2; }
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        if (target == 0) {
            TURN { MOVE(opponentRight, MOVE_TACKLE, target: playerLeft); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
        } else if (target == 1) {
            TURN { MOVE(opponentRight, MOVE_TACKLE, target: opponentLeft); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
        } else {
            TURN { MOVE(opponentRight, MOVE_TACKLE, target: playerRight); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
        }
    } THEN {
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
        EXPECT_EQ(playerLeft->hp < playerLeft->maxHP, target == 0);
        EXPECT_EQ(opponentLeft->hp < opponentLeft->maxHP, target == 1);
        EXPECT_EQ(playerRight->hp < playerRight->maxHP, target == 2);
    }
}

FFA_BATTLE_TEST("FFA: Reflect protects only the trainer who set it")
{
    s16 damageUser, damageOther;
    GIVEN {
        ASSUME(GetMoveCategory(MOVE_SCRATCH) == DAMAGE_CATEGORY_PHYSICAL);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_REFLECT); MOVE(opponentLeft, MOVE_SCRATCH, target: playerLeft);
               MOVE(opponentRight, MOVE_SCRATCH, target: playerRight); MOVE(playerRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_REFLECT, playerLeft);
        HP_BAR(playerLeft, captureDamage: &damageUser);
        HP_BAR(playerRight, captureDamage: &damageOther);
    } THEN {
        // Reflect takes a third off in a double-layout battle (two thirds of the damage), half off in singles.
        EXPECT_MUL_EQ(damageUser, Q_4_12(1.5), damageOther);
    }
}

FFA_BATTLE_TEST("FFA: Intimidate lowers the Attack of all three other trainers")
{
    GIVEN {
        PLAYER(SPECIES_ARCANINE) { Ability(ABILITY_INTIMIDATE); }
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(playerLeft->statStages[STAT_ATK], DEFAULT_STAT_STAGE);
        EXPECT_EQ(opponentLeft->statStages[STAT_ATK], DEFAULT_STAT_STAGE - 1);
        EXPECT_EQ(playerRight->statStages[STAT_ATK], DEFAULT_STAT_STAGE - 1);
        EXPECT_EQ(opponentRight->statStages[STAT_ATK], DEFAULT_STAT_STAGE - 1);
    }
}

FFA_BATTLE_TEST("FFA: Spikes go on the side of the trainer they were aimed at, and only that one")
{
    GIVEN {
        ASSUME(GetMoveTarget(MOVE_SPIKES) == TARGET_OPPONENTS_FIELD);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WYNAUT);
        OPPONENT_B(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WYNAUT);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SPIKES, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
        TURN { SWITCH(opponentLeft, 1); SWITCH(opponentRight, 1); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SPIKES, playerLeft);
        MESSAGE("The opposing Wynaut was hurt by the spikes!");
    } THEN {
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_EQ(opponentRight->species, SPECIES_WYNAUT);
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: nobody has an ally, so Helping Hand has nobody to help")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_HELPING_HAND); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        MESSAGE("Wobbuffet used Helping Hand!");
        MESSAGE("But it failed!");
    }
}
