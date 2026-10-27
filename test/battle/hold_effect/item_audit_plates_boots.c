#include "global.h"
#include "test/battle.h"

// The 2026-10-27 item audit, after the package: held items no test checked (the expansion's plate.c and
// heavy_duty_boots.c are to-dos). The Plates' boost to their own type is item_audit_untested.c's; here, that a Plate
// leaves another type alone, and Heavy-Duty Boots against the hazards as the holder switches in.

SINGLE_BATTLE_TEST("A Plate does not boost moves of another type", s16 damage)
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_NONE; }
    PARAMETRIZE { item = ITEM_FLAME_PLATE; }

    GIVEN {
        ASSUME(GetMoveType(MOVE_WATER_GUN) == TYPE_WATER);
        PLAYER(SPECIES_WOBBUFFET) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_WATER_GUN); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &results[i].damage);
    } FINALLY {
        EXPECT_EQ(results[0].damage, results[1].damage);
    }
}

SINGLE_BATTLE_TEST("Heavy-Duty Boots: the holder takes no damage from Stealth Rock as it switches in")
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_NONE; }
    PARAMETRIZE { item = ITEM_HEAVY_DUTY_BOOTS; }

    GIVEN {
        ASSUME(GetItemHoldEffect(ITEM_HEAVY_DUTY_BOOTS) == HOLD_EFFECT_HEAVY_DUTY_BOOTS);
        PLAYER(SPECIES_WOBBUFFET);
        PLAYER(SPECIES_WYNAUT) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_STEALTH_ROCK); }
        TURN { SWITCH(player, 1); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_STEALTH_ROCK, opponent);
        if (item == ITEM_NONE)
            MESSAGE("Pointed stones dug into Wynaut!");
        else
            NOT MESSAGE("Pointed stones dug into Wynaut!");
    } THEN {
        if (item == ITEM_NONE)
            EXPECT_LT(player->hp, player->maxHP);
        else
            EXPECT_EQ(player->hp, player->maxHP);
    }
}

SINGLE_BATTLE_TEST("Heavy-Duty Boots: the holder is not poisoned by Toxic Spikes as it switches in")
{
    enum Item item = ITEM_NONE;

    PARAMETRIZE { item = ITEM_NONE; }
    PARAMETRIZE { item = ITEM_HEAVY_DUTY_BOOTS; }

    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PLAYER(SPECIES_WYNAUT) { Item(item); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(opponent, MOVE_TOXIC_SPIKES); }
        TURN { SWITCH(player, 1); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_TOXIC_SPIKES, opponent);
        if (item == ITEM_NONE)
            MESSAGE("Wynaut was poisoned!");
        else
            NOT MESSAGE("Wynaut was poisoned!");
    } THEN {
        if (item == ITEM_NONE)
            EXPECT(player->status1 & STATUS1_POISON);
        else
            EXPECT_EQ(player->status1, STATUS1_NONE);
    }
}
