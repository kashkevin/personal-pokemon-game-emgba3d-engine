#include "global.h"
#include "test/battle.h"

// EmeraldTogether free-for-all: field effects, redirection, switching and ally moves in a four-sided battle.
// battler 0 = PLAYER (playerLeft), 1 = OPPONENT_A (opponentLeft), 2 = PARTNER (playerRight), 3 = OPPONENT_B (opponentRight).

FFA_BATTLE_TEST("FFA: Safeguard protects only the trainer who used it")
{
    GIVEN {
        ASSUME(GetMoveEffect(MOVE_THUNDER_WAVE) == EFFECT_NON_VOLATILE_STATUS);
        PLAYER(SPECIES_WOBBUFFET) { Speed(40); }
        PARTNER(SPECIES_WOBBUFFET) { Speed(30); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Speed(20); }
        OPPONENT_B(SPECIES_WOBBUFFET) { Speed(10); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_SAFEGUARD); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
        TURN { MOVE(opponentRight, MOVE_THUNDER_WAVE, target: playerLeft); MOVE(opponentLeft, MOVE_THUNDER_WAVE, target: playerRight);
               MOVE(playerLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(playerLeft->status1, STATUS1_NONE);
        EXPECT_EQ(playerRight->status1, STATUS1_PARALYSIS);
    }
}

FFA_BATTLE_TEST("FFA: Follow Me redirects nothing: there are no allies to protect")
{
    GIVEN {
        ASSUME(GetMoveEffect(MOVE_FOLLOW_ME) == EFFECT_FOLLOW_ME);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_FOLLOW_ME); MOVE(playerLeft, MOVE_TACKLE, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_LT(opponentLeft->hp, opponentLeft->maxHP);
        EXPECT_EQ(opponentRight->hp, opponentRight->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: Lightning Rod draws an Electric move aimed at another trainer")
{
    GIVEN {
        ASSUME(GetMoveType(MOVE_THUNDER_SHOCK) == TYPE_ELECTRIC);
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_PIKACHU) { Ability(ABILITY_LIGHTNING_ROD); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_THUNDER_SHOCK, target: opponentLeft); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ABILITY_POPUP(opponentRight, ABILITY_LIGHTNING_ROD);
    } THEN {
        EXPECT_EQ(opponentLeft->hp, opponentLeft->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: Helping Hand, Ally Switch and Heal Pulse on an ally do nothing")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_ALLY_SWITCH); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        MESSAGE("But it failed!");
    } THEN {
        EXPECT_EQ(GetBattlerPosition(0), B_POSITION_PLAYER_LEFT);
        EXPECT_EQ(GetBattlerPosition(2), B_POSITION_PLAYER_RIGHT);
    }
}

FFA_BATTLE_TEST("FFA: Heal Bell cures only the user's own party")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { Status1(STATUS1_BURN); }
        PARTNER(SPECIES_WOBBUFFET) { Status1(STATUS1_BURN); }
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_HEAL_BELL); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT_EQ(playerLeft->status1 & STATUS1_BURN, 0);
        EXPECT(playerRight->status1 & STATUS1_BURN);
    }
}

FFA_BATTLE_TEST("FFA: Perish Song counts down for all four")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_PERISH_SONG); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } THEN {
        EXPECT(playerLeft->volatiles.perishSong);
        EXPECT(opponentLeft->volatiles.perishSong);
        EXPECT(playerRight->volatiles.perishSong);
        EXPECT(opponentRight->volatiles.perishSong);
    }
}

FFA_BATTLE_TEST("FFA: a switch brings in the switching trainer's own next Pokemon, and its own hazards hit it")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { Speed(40); }
        PLAYER(SPECIES_WYNAUT) { Speed(40); }
        PARTNER(SPECIES_WOBBUFFET) { Speed(30); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Speed(20); }
        OPPONENT_B(SPECIES_WOBBUFFET) { Speed(10); }
    } WHEN {
        TURN { MOVE(opponentRight, MOVE_STEALTH_ROCK, target: playerLeft); MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); }
        TURN { SWITCH(playerLeft, 1); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_STEALTH_ROCK, opponentRight);
        MESSAGE("Pointed stones dug into Wynaut!");
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_WYNAUT);
        EXPECT_LT(playerLeft->hp, playerLeft->maxHP);
    }
}

FFA_BATTLE_TEST("FFA: Imprison stops the user's moves for every other trainer")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { Moves(MOVE_IMPRISON, MOVE_CELEBRATE); }
        PARTNER(SPECIES_WOBBUFFET) { Moves(MOVE_CELEBRATE, MOVE_SPLASH); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Moves(MOVE_CELEBRATE, MOVE_SPLASH); }
        OPPONENT_B(SPECIES_WOBBUFFET) { Moves(MOVE_CELEBRATE, MOVE_SPLASH); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_IMPRISON); MOVE(opponentLeft, MOVE_CELEBRATE); MOVE(playerRight, MOVE_CELEBRATE); MOVE(opponentRight, MOVE_CELEBRATE); }
        TURN { MOVE(playerLeft, MOVE_CELEBRATE); MOVE(opponentLeft, MOVE_SPLASH); MOVE(playerRight, MOVE_SPLASH); MOVE(opponentRight, MOVE_SPLASH); }
    } SCENE {
        MESSAGE("Wobbuffet used Imprison!");
    }
}
