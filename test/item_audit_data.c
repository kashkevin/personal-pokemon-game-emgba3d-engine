#include "global.h"
#include "item.h"
#include "test/test.h"
#include "constants/hold_effects.h"
#include "constants/items.h"

// The 2026-10-27 item audit: the Plates' and Memories' own data, all seventeen of each, as the battle code reads it
// (the holder's type for Judgment and Multi-Attack, a Plate's power boost): their hold effect and the type each names.
// The moves' type change has the expansion's own tests (move_effect/change_type_on_item.c, four of each).

static const u16 sTypeItems[][3] =
{
    { TYPE_FIGHTING, ITEM_FIST_PLATE,   ITEM_FIGHTING_MEMORY },
    { TYPE_FLYING,   ITEM_SKY_PLATE,    ITEM_FLYING_MEMORY },
    { TYPE_POISON,   ITEM_TOXIC_PLATE,  ITEM_POISON_MEMORY },
    { TYPE_GROUND,   ITEM_EARTH_PLATE,  ITEM_GROUND_MEMORY },
    { TYPE_ROCK,     ITEM_STONE_PLATE,  ITEM_ROCK_MEMORY },
    { TYPE_BUG,      ITEM_INSECT_PLATE, ITEM_BUG_MEMORY },
    { TYPE_GHOST,    ITEM_SPOOKY_PLATE, ITEM_GHOST_MEMORY },
    { TYPE_STEEL,    ITEM_IRON_PLATE,   ITEM_STEEL_MEMORY },
    { TYPE_FIRE,     ITEM_FLAME_PLATE,  ITEM_FIRE_MEMORY },
    { TYPE_WATER,    ITEM_SPLASH_PLATE, ITEM_WATER_MEMORY },
    { TYPE_GRASS,    ITEM_MEADOW_PLATE, ITEM_GRASS_MEMORY },
    { TYPE_ELECTRIC, ITEM_ZAP_PLATE,    ITEM_ELECTRIC_MEMORY },
    { TYPE_PSYCHIC,  ITEM_MIND_PLATE,   ITEM_PSYCHIC_MEMORY },
    { TYPE_ICE,      ITEM_ICICLE_PLATE, ITEM_ICE_MEMORY },
    { TYPE_DRAGON,   ITEM_DRACO_PLATE,  ITEM_DRAGON_MEMORY },
    { TYPE_DARK,     ITEM_DREAD_PLATE,  ITEM_DARK_MEMORY },
    { TYPE_FAIRY,    ITEM_PIXIE_PLATE,  ITEM_FAIRY_MEMORY },
};

TEST("Each Plate is a Plate of its own type, with a 20% boost")
{
    u32 j;

    for (j = 0; j < ARRAY_COUNT(sTypeItems); j++)
    {
        EXPECT_EQ(GetItemHoldEffect(sTypeItems[j][1]), HOLD_EFFECT_PLATE);
        EXPECT_EQ(GetItemSecondaryId(sTypeItems[j][1]), sTypeItems[j][0]);
        EXPECT_EQ(GetItemHoldEffectParam(sTypeItems[j][1]), 20);
    }
}

TEST("Each Memory is a Memory of its own type")
{
    u32 j;

    for (j = 0; j < ARRAY_COUNT(sTypeItems); j++)
    {
        EXPECT_EQ(GetItemHoldEffect(sTypeItems[j][2]), HOLD_EFFECT_MEMORY);
        EXPECT_EQ(GetItemSecondaryId(sTypeItems[j][2]), sTypeItems[j][0]);
    }
}
