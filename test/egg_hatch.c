#include "global.h"
#include "daycare.h"
#include "event_data.h"
#include "ow_abilities.h"
#include "pokemon.h"
#include "test/overworld_script.h"
#include "test/test.h"

// Egg cycles on the overworld (this game runs Gen 6 rules: 257 steps a cycle, level 1 hatchlings).

static void GiveEgg(u32 slot, enum Species species, u32 cycles)
{
    CreateEgg(&gParties[B_TRAINER_PLAYER][slot], species, FALSE);
    SetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_FRIENDSHIP, &cycles);
    gPartiesCount[B_TRAINER_PLAYER] = slot + 1;
}

static u32 Steps(u32 count)
{
    u32 i, hatched = 0;

    for (i = 0; i < count; i++)
        hatched |= ShouldEggHatch();
    return hatched;
}

TEST("(EggHatch) a Gen 6 egg loses one cycle every 257 steps")
{
    ASSUME(P_EGG_CYCLE_LENGTH == GEN_6);
    ZeroPlayerPartyMons();
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    GiveEgg(0, SPECIES_PICHU, 5);

    EXPECT(!Steps(256));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_FRIENDSHIP), 5);
    EXPECT(!Steps(1));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_FRIENDSHIP), 4);
    EXPECT(!Steps(257 * 3));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_FRIENDSHIP), 1);
}

TEST("(EggHatch) an egg with no cycles left hatches on the next cycle, and says which one")
{
    ASSUME(P_EGG_CYCLE_LENGTH == GEN_6);
    ZeroPlayerPartyMons();
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    GiveEgg(0, SPECIES_PICHU, 1);
    GiveEgg(1, SPECIES_RIOLU, 0);   // the second slot is the one that is due
    gSpecialVar_0x8004 = 0xFF;

    EXPECT(!Steps(256));
    EXPECT(Steps(1));
    EXPECT_EQ(gSpecialVar_0x8004, 1);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_FRIENDSHIP), 0);
}

TEST("(EggHatch) a party of grown Pokémon never hatches anything")
{
    ASSUME(P_EGG_CYCLE_LENGTH == GEN_6);
    ZeroPlayerPartyMons();
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    RUN_OVERWORLD_SCRIPT(givemon SPECIES_PIKACHU, 20;);

    EXPECT(!Steps(257 * 3));
}

TEST("(EggHatch) a Flame Body or Magma Armor Pokémon in the party takes two cycles at a time")
{
    ASSUME(P_EGG_CYCLE_LENGTH == GEN_6);
    ZeroPlayerPartyMons();
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    GiveEgg(0, SPECIES_PICHU, 6);
    RUN_OVERWORLD_SCRIPT(givemon SPECIES_MAGCARGO, 30, abilityNum=0;);
    ASSUME(DoesPartyHaveIncubatorMon());

    EXPECT(!Steps(257));
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_FRIENDSHIP), 4);
}
