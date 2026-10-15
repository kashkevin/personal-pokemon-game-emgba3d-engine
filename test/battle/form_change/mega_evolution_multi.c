#include "global.h"
#include "test/battle.h"

// Multiplayer 2v2 (link multi battle): each of the four trainers has their own Mega Ring, so both
// trainers of a side can Mega Evolve (the single-battle rules are in mega_evolution.c).

MULTI_BATTLE_TEST("Multi battle: both trainers of a side can Mega Evolve in the same turn")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); }
        PARTNER(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); }
        OPPONENT_A(SPECIES_WOBBUFFET);
        OPPONENT_B(SPECIES_WYNAUT);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
               MOVE(playerRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        MESSAGE("Venusaur has Mega Evolved into Mega Venusaur!");
        MESSAGE("Gardevoir has Mega Evolved into Mega Gardevoir!");
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(playerRight->species, SPECIES_GARDEVOIR_MEGA);
    }
}

MULTI_BATTLE_TEST("Multi battle: an opposing trainer can Mega Evolve while the player's side does too")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); }
        PARTNER(SPECIES_WOBBUFFET);
        OPPONENT_A(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); }
        OPPONENT_B(SPECIES_WYNAUT);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
               MOVE(opponentLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(opponentLeft->species, SPECIES_GARDEVOIR_MEGA);
    }
}
