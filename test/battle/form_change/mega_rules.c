#include "global.h"
#include "test/battle.h"

// The rules around Mega Evolution: who may Mega Evolve and when, what stays and what reverts, turn order and damage, and
// the one-Mega-per-trainer rule in battles with more than two trainers. The per-species results are in mega_every_form.c.
//
// Note: the test runner refuses gimmick: GIMMICK_MEGA from a Pokemon that holds no Mega Stone (except Rayquaza), as the
// battle UI never offers it; such cases check that the engine has no Mega Evolution to offer (CanActivateGimmick).

SINGLE_BATTLE_TEST("Mega Evolution: a trainer can Mega Evolve only once per battle")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); }
        PLAYER(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
        TURN { SWITCH(player, 1); }
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        MESSAGE("Venusaur has Mega Evolved into Mega Venusaur!");
        NONE_OF {
            ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
            MESSAGE("Gardevoir has Mega Evolved into Mega Gardevoir!");
        }
        MESSAGE("Gardevoir used Celebrate!");
    } THEN {
        EXPECT_EQ(player->species, SPECIES_GARDEVOIR);
        EXPECT(!CanActivateGimmick(B_BATTLER_0, GIMMICK_MEGA));
    }
}

DOUBLE_BATTLE_TEST("Mega Evolution: a trainer with two Mega Stone holders on the field can Mega Evolve only one of them")
{
    // The battle UI and the AI don't offer the second Mega (HasTrainerUsedGimmick counts the partner's pending one); a
    // recorded or linked choice can still ask for both, and the engine checks again when it carries them out
    // (TryActivateGimmick in battle_main.c, EmeraldTogether).
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); }
        PLAYER(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); }
        OPPONENT(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WYNAUT);
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); MOVE(playerRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerLeft);
        NOT ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerRight);
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(playerRight->species, SPECIES_GARDEVOIR);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: a Pokemon can't Mega Evolve without the Mega Stone made for its species and form")
{
    enum Species species = SPECIES_NONE;
    enum Item item = ITEM_NONE;
    PARAMETRIZE { species = SPECIES_CHARIZARD;  item = ITEM_NONE; }        // no stone
    PARAMETRIZE { species = SPECIES_CHARIZARD;  item = ITEM_VENUSAURITE; } // another species' stone
    PARAMETRIZE { species = SPECIES_PIKACHU;    item = ITEM_RAICHUNITE_X; } // a species with no Mega form
    PARAMETRIZE { species = SPECIES_FLOETTE;    item = ITEM_FLOETTITE; }   // only Eternal Flower Floette uses it
    PARAMETRIZE { species = SPECIES_ZYGARDE_50; item = ITEM_ZYGARDITE; }   // only Complete Forme Zygarde uses it
    GIVEN {
        PLAYER(species) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: item == ITEM_NONE ? GIMMICK_NONE : GIMMICK_MEGA); }
    } SCENE {
        NOT ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
    } THEN {
        EXPECT_EQ(player->species, species);
        EXPECT_EQ(player->item, item);
        EXPECT(!CanActivateGimmick(B_BATTLER_0, GIMMICK_MEGA));
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: Rayquaza can't Mega Evolve if it doesn't know Dragon Ascent")
{
    GIVEN {
        PLAYER(SPECIES_RAYQUAZA) { Moves(MOVE_CELEBRATE, MOVE_DRAGON_CLAW); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        NONE_OF {
            MESSAGE("1's fervent wish has reached Rayquaza!");
            ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        }
        MESSAGE("Rayquaza used Celebrate!");
    } THEN {
        EXPECT_EQ(player->species, SPECIES_RAYQUAZA);
        EXPECT(!CanActivateGimmick(B_BATTLER_0, GIMMICK_MEGA));
    }
}

// The Gen 6 level-up learnset (P_LVL_UP_LEARNSETS is GEN_6) and the teachable learnset lack Dragon Ascent (in ORAS it came
// from a story tutor), and no script here teaches it, so players can't reach Mega Rayquaza.
TEST("Mega Evolution: Rayquaza can learn Dragon Ascent, the move its Mega Evolution needs")
{
    KNOWN_FAILING;
    const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(SPECIES_RAYQUAZA);
    bool32 learnable = CanLearnTeachableMove(SPECIES_RAYQUAZA, MOVE_DRAGON_ASCENT);
    for (u32 j = 0; learnset[j].move != LEVEL_UP_MOVE_END; j++)
    {
        if (learnset[j].move == MOVE_DRAGON_ASCENT)
            learnable = TRUE;
    }
    EXPECT(learnable);
}

SINGLE_BATTLE_TEST("Mega Evolution: Knock Off, Thief and Trick can't take a Mega Stone from a Pokemon that can use it")
{
    enum Move move = MOVE_NONE;
    enum Species holder = SPECIES_NONE;
    bool32 megaFirst = FALSE;
    PARAMETRIZE { move = MOVE_KNOCK_OFF; holder = SPECIES_VENUSAUR;  megaFirst = FALSE; }
    PARAMETRIZE { move = MOVE_THIEF;     holder = SPECIES_VENUSAUR;  megaFirst = FALSE; }
    PARAMETRIZE { move = MOVE_TRICK;     holder = SPECIES_VENUSAUR;  megaFirst = FALSE; }
    PARAMETRIZE { move = MOVE_KNOCK_OFF; holder = SPECIES_VENUSAUR;  megaFirst = TRUE; }
    PARAMETRIZE { move = MOVE_THIEF;     holder = SPECIES_VENUSAUR;  megaFirst = TRUE; }
    PARAMETRIZE { move = MOVE_TRICK;     holder = SPECIES_VENUSAUR;  megaFirst = TRUE; }
    // A Pokemon that can't use the stone loses it like any other item.
    PARAMETRIZE { move = MOVE_KNOCK_OFF; holder = SPECIES_WOBBUFFET; megaFirst = FALSE; }
    PARAMETRIZE { move = MOVE_THIEF;     holder = SPECIES_WOBBUFFET; megaFirst = FALSE; }
    PARAMETRIZE { move = MOVE_TRICK;     holder = SPECIES_WOBBUFFET; megaFirst = FALSE; }
    bool32 canUse = (holder == SPECIES_VENUSAUR);
    enum Item playerItem = (move == MOVE_TRICK) ? ITEM_LEFTOVERS : ITEM_NONE; // Trick needs something to give
    GIVEN {
        ASSUME(GetMoveEffect(MOVE_KNOCK_OFF) == EFFECT_KNOCK_OFF);
        ASSUME(GetMoveEffect(MOVE_THIEF) == EFFECT_STEAL_ITEM);
        ASSUME(GetMoveEffect(MOVE_TRICK) == EFFECT_TRICK);
        PLAYER(SPECIES_WOBBUFFET) { Item(playerItem); }
        OPPONENT(holder) { Item(ITEM_VENUSAURITE); }
    } WHEN {
        TURN { MOVE(opponent, MOVE_CELEBRATE, gimmick: megaFirst ? GIMMICK_MEGA : GIMMICK_NONE); MOVE(player, move); }
    } SCENE {
        if (megaFirst)
            ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponent);
        if (move == MOVE_TRICK && canUse) {
            MESSAGE("Wobbuffet used Trick!");
            MESSAGE("But it failed!");
        } else {
            ANIMATION(ANIM_TYPE_MOVE, move, player);
        }
    } THEN {
        EXPECT_EQ(opponent->species, megaFirst ? SPECIES_VENUSAUR_MEGA : holder);
        if (canUse) {
            EXPECT_EQ(opponent->item, ITEM_VENUSAURITE);
            EXPECT_EQ(player->item, playerItem);
        } else if (move == MOVE_KNOCK_OFF) {
            EXPECT_EQ(opponent->item, ITEM_NONE);
        } else { // Thief takes the stone, Trick swaps it for the Leftovers.
            EXPECT_EQ(player->item, ITEM_VENUSAURITE);
            EXPECT_EQ(opponent->item, playerItem);
        }
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: a Mega Evolved Pokemon is still Mega Evolved after switching out and back in")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); }
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
        TURN { SWITCH(player, 1); }
        TURN { SWITCH(player, 0); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        SWITCH_OUT_MESSAGE("Venusaur");
        SEND_IN_MESSAGE("Wobbuffet");
        SWITCH_OUT_MESSAGE("Wobbuffet");
        SEND_IN_MESSAGE("Venusaur");
    } THEN {
        EXPECT_EQ(player->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(player->types[0], GetSpeciesType(SPECIES_VENUSAUR_MEGA, 0));
        EXPECT_EQ(player->types[1], GetSpeciesType(SPECIES_VENUSAUR_MEGA, 1));
        EXPECT_EQ(player->ability, GetSpeciesAbility(SPECIES_VENUSAUR_MEGA, 0));
        EXPECT_EQ(GetActiveGimmick(B_BATTLER_0), GIMMICK_MEGA); // its healthbox keeps the Mega indicator
        // Only the end of the battle makes it Venusaur again.
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_VENUSAUR);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: a Mega that faints is its base form again when revived, and its trainer can't Mega Evolve again")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { HP(1); Item(ITEM_VENUSAURITE); }
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); MOVE(opponent, MOVE_SCRATCH); SEND_OUT(player, 1); }
        TURN { USE_ITEM(player, ITEM_REVIVE, 0); }
        TURN { SWITCH(player, 0); }
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        MESSAGE("Venusaur fainted!");
        NONE_OF {
            ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
            MESSAGE("Venusaur has Mega Evolved into Mega Venusaur!");
        }
        MESSAGE("Venusaur used Celebrate!");
    } THEN {
        // Fainting reverted its party data (FORM_CHANGE_FAINT), which is what it was sent back out with.
        EXPECT_EQ(player->species, SPECIES_VENUSAUR);
        EXPECT_EQ(player->types[0], GetSpeciesType(SPECIES_VENUSAUR, 0));
        EXPECT_EQ(player->types[1], GetSpeciesType(SPECIES_VENUSAUR, 1));
        EXPECT_EQ(player->ability, GetSpeciesAbility(SPECIES_VENUSAUR, 0));
        EXPECT_EQ(player->item, ITEM_VENUSAURITE);
        EXPECT(!CanActivateGimmick(B_BATTLER_0, GIMMICK_MEGA));
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: a revived Mega is no longer marked as Mega Evolved")
{
    // A fainting Mega's party slot loses its active gimmick with its form (EmeraldTogether, FaintClearSetData in
    // battle_main.c), so the healthbox Mega indicator (GetIndicatorPalTag/GetIndicatorSpriteSrc in battle_gimmick.c) and
    // the PC battle screen's Mega flag (ui_battle.c) don't show on the revived base form.
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { HP(1); Item(ITEM_VENUSAURITE); }
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); MOVE(opponent, MOVE_SCRATCH); SEND_OUT(player, 1); }
        TURN { USE_ITEM(player, ITEM_REVIVE, 0); }
        TURN { SWITCH(player, 0); }
    } THEN {
        EXPECT_EQ(player->species, SPECIES_VENUSAUR);
        EXPECT_EQ(GetActiveGimmick(B_BATTLER_0), GIMMICK_NONE);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: Zygarde that Power Construct made Complete can Mega Evolve on a later turn")
{
    GIVEN {
        PLAYER(SPECIES_ZYGARDE_50_POWER_CONSTRUCT) { Ability(ABILITY_POWER_CONSTRUCT); Item(ITEM_ZYGARDITE); HP(1); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { }
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        // Turn 1
        MESSAGE("You sense the presence of many!");
        ABILITY_POPUP(player, ABILITY_POWER_CONSTRUCT);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_POWER_CONSTRUCT, player);
        // Turn 2
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
    } THEN {
        EXPECT_EQ(player->species, SPECIES_ZYGARDE_MEGA);
        // Zygarde-Complete's table reverts to the form the battle started with.
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_ZYGARDE_50_POWER_CONSTRUCT);
    }
}

// The World Event's Mega Zygarde is caught as Zygarde-Complete (MultiplayerWorld_GetBattleFormBase: the form that Mega
// Evolves into it), so a player's Zygarde can start a battle Complete. Complete's faint and end-of-battle form changes name
// no species: they go back to the form the battle started with, and this one started Complete.
SINGLE_BATTLE_TEST("Mega Evolution: a Zygarde that starts a battle Complete stays Complete when the battle ends")
{
    GIVEN {
        PLAYER(SPECIES_ZYGARDE_COMPLETE) { Moves(MOVE_TACKLE); }
        OPPONENT(SPECIES_WOBBUFFET) { HP(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_TACKLE); }
    } SCENE {
        MESSAGE("The opposing Wobbuffet fainted!");
    } THEN {
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_ZYGARDE_COMPLETE);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: a Zygarde that starts a battle Complete stays Complete when it faints")
{
    GIVEN {
        PLAYER(SPECIES_ZYGARDE_COMPLETE) { HP(1); }
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET) { Moves(MOVE_TACKLE); }
    } WHEN {
        TURN { MOVE(opponent, MOVE_TACKLE); SEND_OUT(player, 1); }
    } SCENE {
        MESSAGE("Zygarde fainted!");
    } THEN {
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_ZYGARDE_COMPLETE);
    }
}

// B_MEGA_EVO_TURN_ORDER is GEN_LATEST (GEN_6): the turn order is not worked out again after Mega Evolution
// (TryDoGimmicksBeforeMoves in battle_main.c only calls TryChangeTurnOrder from Gen 7).
SINGLE_BATTLE_TEST("Mega Evolution: the turn a Pokemon Mega Evolves it moves at its old Speed, from the next turn at its Mega Speed")
{
    GIVEN {
        ASSUME(B_MEGA_EVO_TURN_ORDER < GEN_7);
        ASSUME(GetSpeciesBaseSpeed(SPECIES_BEEDRILL_MEGA) > GetSpeciesBaseSpeed(SPECIES_BEEDRILL));
        // No Speed(): the test runner makes Wobbuffet faster than Beedrill (TURN order), and Mega Evolving recalculates
        // Beedrill's real Speed, far above both.
        PLAYER(SPECIES_BEEDRILL) { Item(ITEM_BEEDRILLITE); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_CELEBRATE); MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
        TURN { }
    } SCENE {
        // Turn 1: Mega Evolution comes before every move, but Beedrill still moves last.
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        MESSAGE("The opposing Wobbuffet used Celebrate!");
        MESSAGE("Beedrill used Celebrate!");
        // Turn 2: Mega Beedrill's Speed decides.
        MESSAGE("Beedrill used Celebrate!");
        MESSAGE("The opposing Wobbuffet used Celebrate!");
    } THEN {
        EXPECT_GT(player->speed, opponent->speed);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: Mega Mawile's Huge Power doubles the damage of its physical moves", s16 damage)
{
    u32 gimmick = GIMMICK_NONE;
    PARAMETRIZE { gimmick = GIMMICK_NONE; }
    PARAMETRIZE { gimmick = GIMMICK_MEGA; }
    GIVEN {
        ASSUME(GetSpeciesAbility(SPECIES_MAWILE, 0) == ABILITY_HYPER_CUTTER);
        ASSUME(GetSpeciesAbility(SPECIES_MAWILE_MEGA, 0) == ABILITY_HUGE_POWER);
        ASSUME(GetMoveCategory(MOVE_SCRATCH) == DAMAGE_CATEGORY_PHYSICAL);
        ASSUME(GetMoveType(MOVE_SCRATCH) == TYPE_NORMAL);
        ASSUME(GetSpeciesType(SPECIES_MAWILE, 0) != TYPE_NORMAL && GetSpeciesType(SPECIES_MAWILE, 1) != TYPE_NORMAL);
        ASSUME(GetSpeciesType(SPECIES_MAWILE_MEGA, 0) != TYPE_NORMAL && GetSpeciesType(SPECIES_MAWILE_MEGA, 1) != TYPE_NORMAL);
        // Attack() fixes the Attack stat through Mega Evolution, so only the Ability differs.
        PLAYER(SPECIES_MAWILE) { Ability(ABILITY_HYPER_CUTTER); Item(ITEM_MAWILITE); Attack(100); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH, gimmick: gimmick); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_SCRATCH, player);
        HP_BAR(opponent, captureDamage: &results[i].damage);
    } FINALLY {
        EXPECT_MUL_EQ(results[0].damage, Q_4_12(2.0), results[1].damage);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: Mega Charizard X loses the Flying type, so Ground moves hit it")
{
    u32 gimmick = GIMMICK_NONE;
    PARAMETRIZE { gimmick = GIMMICK_NONE; }
    PARAMETRIZE { gimmick = GIMMICK_MEGA; }
    GIVEN {
        ASSUME(GetMoveType(MOVE_EARTHQUAKE) == TYPE_GROUND);
        ASSUME(GetSpeciesType(SPECIES_CHARIZARD, 1) == TYPE_FLYING);
        ASSUME(GetSpeciesType(SPECIES_CHARIZARD_MEGA_X, 0) == TYPE_FIRE);
        ASSUME(GetSpeciesType(SPECIES_CHARIZARD_MEGA_X, 1) == TYPE_DRAGON);
        PLAYER(SPECIES_CHARIZARD) { Item(ITEM_CHARIZARDITE_X); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_EARTHQUAKE); MOVE(player, MOVE_CELEBRATE, gimmick: gimmick); }
    } SCENE {
        if (gimmick == GIMMICK_MEGA) {
            ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
            ANIMATION(ANIM_TYPE_MOVE, MOVE_EARTHQUAKE, opponent);
            HP_BAR(player);
            MESSAGE("It's super effective!");
        } else {
            MESSAGE("The opposing Wobbuffet used Earthquake!");
            MESSAGE("It doesn't affect Charizard…");
            NOT HP_BAR(player);
        }
    }
}

// Battles with more than two trainers: every trainer has their own Mega Evolution (one each), Mega Evolutions happen in
// Speed order. The test runner's MULTI and free-for-all battles set BATTLE_TYPE_TWO_OPPONENTS, so the two opposing
// Pokemon belong to two trainers (BATTLE_TYPE_LINK, which the native four-player battles add, is not set here).

FFA_BATTLE_TEST("Mega Evolution: in a free-for-all each of the four trainers can Mega Evolve on the same turn")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); Speed(4); }
        PARTNER(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); Speed(3); }
        OPPONENT_A(SPECIES_BLASTOISE) { Item(ITEM_BLASTOISINITE); Speed(2); }
        OPPONENT_B(SPECIES_AMPHAROS) { Item(ITEM_AMPHAROSITE); Speed(1); }
    } WHEN {
        TURN {
            MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(playerRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
        }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerRight);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentRight);
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(playerRight->species, SPECIES_GARDEVOIR_MEGA);
        EXPECT_EQ(opponentLeft->species, SPECIES_BLASTOISE_MEGA);
        EXPECT_EQ(opponentRight->species, SPECIES_AMPHAROS_MEGA);
    }
}

FFA3_BATTLE_TEST("Mega Evolution: in a 1v1v1 each of the three trainers can Mega Evolve on the same turn")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); Speed(3); }
        OPPONENT_A(SPECIES_BLASTOISE) { Item(ITEM_BLASTOISINITE); Speed(2); }
        OPPONENT_B(SPECIES_AMPHAROS) { Item(ITEM_AMPHAROSITE); Speed(1); }
    } WHEN {
        TURN {
            MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
        }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentRight);
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(opponentLeft->species, SPECIES_BLASTOISE_MEGA);
        EXPECT_EQ(opponentRight->species, SPECIES_AMPHAROS_MEGA);
    }
}

MULTI_BATTLE_TEST("Mega Evolution: in a multi battle each of the four trainers can Mega Evolve on the same turn")
{
    GIVEN {
        PLAYER(SPECIES_VENUSAUR) { Item(ITEM_VENUSAURITE); Speed(4); }
        PARTNER(SPECIES_GARDEVOIR) { Item(ITEM_GARDEVOIRITE); Speed(3); }
        OPPONENT_A(SPECIES_BLASTOISE) { Item(ITEM_BLASTOISINITE); Speed(2); }
        OPPONENT_B(SPECIES_AMPHAROS) { Item(ITEM_AMPHAROSITE); Speed(1); }
    } WHEN {
        TURN {
            MOVE(playerLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(playerRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentLeft, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
            MOVE(opponentRight, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA);
        }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, playerRight);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentLeft);
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, opponentRight);
    } THEN {
        EXPECT_EQ(playerLeft->species, SPECIES_VENUSAUR_MEGA);
        EXPECT_EQ(playerRight->species, SPECIES_GARDEVOIR_MEGA);
        EXPECT_EQ(opponentLeft->species, SPECIES_BLASTOISE_MEGA);
        EXPECT_EQ(opponentRight->species, SPECIES_AMPHAROS_MEGA);
    }
}
