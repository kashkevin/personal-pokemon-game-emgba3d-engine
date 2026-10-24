// Native PC build: the summary screen's IVs and EVs (owner request 2026-10-24: see your own Pokémon's IVs and EVs).
// Project-owned. Runs the game's own summary set-up (pokemon_summary_screen.c, SummaryTest_IvEvOffered) for each list
// a caller opens it on, and checks the IVs and EVs are offered for the player's own Pokémon only, then exits.
//   --selftest-summary-ivev
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "event_data.h"
#include "load_save.h"
#include "main.h"
#include "malloc.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "constants/battle.h"
#include "constants/layouts.h"
#include "constants/party_menu.h"
#include "constants/region_map_sections.h"
#include "constants/species.h"

bool32 SummaryTest_IvEvOffered(u8 mode, void *mons, u8 monIndex, u8 maxMonIndex); // pokemon_summary_screen.c
bool32 AreMultiPartiesFullTeams(void);                                            // battle_util.c

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void MakeMon(struct Pokemon *mon, u16 species, bool32 egg)
{
    CreateMonWithIVs(mon, species, 30, 0x13579BDF, OTID_STRUCT_PLAYER_ID, 17);
    if (egg)
    {
        bool32 isEgg = TRUE;

        SetMonData(mon, MON_DATA_IS_EGG, &isEgg);
    }
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    ResetPokemonStorageSystem();
    for (u32 trainer = 0; trainer < MAX_BATTLE_TRAINERS; trainer++)
        for (u32 i = 0; i < PARTY_SIZE; i++)
            ZeroMonData(&gParties[trainer][i]);
    for (u32 i = 0; i < PARTY_SIZE; i++)
    {
        MakeMon(&gParties[B_TRAINER_PLAYER][i], SPECIES_MUDKIP + i, i == 5);
        MakeMon(&gParties[B_TRAINER_PARTNER][i], SPECIES_ZIGZAGOON, FALSE);
        MakeMon(&gParties[B_TRAINER_OPPONENT_A][i], SPECIES_POOCHYENA, FALSE);
    }
    gPartiesCount[B_TRAINER_PLAYER] = PARTY_SIZE;
    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;
    gMapHeader.mapLayoutId = LAYOUT_LITTLEROOT_TOWN;
    gMapHeader.regionMapSectionId = MAPSEC_LITTLEROOT_TOWN;
}

// Where the game opens the summary on the player's own Pokémon: shown (an Egg never).
static void TestOwn(void)
{
    struct Pokemon held;
    struct BoxPokemon *box = GetBoxedMonPtr(2, 0);

    Expect("party menu (field): the player's first Pokemon", SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, gParties[B_TRAINER_PLAYER], 0, 5));
    Expect("party menu (field): the player's fourth Pokemon", SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, gParties[B_TRAINER_PLAYER], 3, 5));
    Expect("party menu (field): an Egg in the party has none", !SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, gParties[B_TRAINER_PLAYER], 5, 5));
    Expect("trade menu: the player's own side", SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], 1, 5));
    CreateBoxMon(box, SPECIES_TREECKO, 20, 0x2468ACE0, OTID_STRUCT_PLAYER_ID);
    Expect("PC: a Pokemon in one of the player's boxes", SummaryTest_IvEvOffered(SUMMARY_MODE_BOX, box, 0, IN_BOX_COUNT - 1));
    gSpecialVar_MonBoxId = 2;
    gSpecialVar_MonBoxPos = 0;
    Expect("PC: a box Pokemon chosen by a script (PC_MON_CHOSEN)", SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, NULL, PC_MON_CHOSEN, 0));
    held = gParties[B_TRAINER_PLAYER][2];
    Expect("PC: the Pokemon held while moving it (SUMMARY_MODE_BOX_CURSOR)", SummaryTest_IvEvOffered(SUMMARY_MODE_BOX_CURSOR, &held, 0, 0));
    gMain.inBattle = TRUE;
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    Expect("battle: the player's party (a single battle)", SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], 0, 5));
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE;
    Expect("battle: the player's party (a double battle)", SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], 1, 5));
    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;
}

// Where it opens on someone else's: not shown.
static void TestNotOwn(void)
{
    static struct Pokemon sFactoryChoices[PARTY_SIZE];
    bool32 fullTeams;

    Expect("trade menu: the trade partner's offer (gParties[B_TRAINER_OPPONENT_A])", !SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_OPPONENT_A], 0, 5));
    Expect("multi battle: the partner's own party (PARTY_LAYOUT_MULTI_FULL_PARTNER)", !SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PARTNER], 0, 5));

    // a multi battle's party menu: with half teams (three each) the partner's three are at 1, 4 and 5 of the player's
    // list (party_menu.c GetMultiPartyForSummaryScreen); with full teams the partner's are a list of their own (above)
    for (u32 half = 0; half < 2; half++)
    {
        gMain.inBattle = TRUE;
        gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI | BATTLE_TYPE_INGAME_PARTNER;
        if (half)
            gBattleTypeFlags |= BATTLE_TYPE_TOWER_LINK_MULTI; // AreMultiPartiesFullTeams: three each
        fullTeams = AreMultiPartiesFullTeams();
        Expect(half ? "multi battle (Tower link multi): half teams" : "multi battle (in-game partner): full teams",
               fullTeams == !half);
        for (u32 slot = 0; slot < 5; slot++)
        {
            char what[120];
            bool32 partner = !fullTeams && (slot == 1 || slot == 4);

            snprintf(what, sizeof(what), "multi battle, %s teams: summary slot %u is %s", fullTeams ? "full" : "half", slot,
                     partner ? "the partner's: none" : "the player's: shown");
            Expect(what, SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], slot, 5) == !partner);
        }
    }
    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;

    // rentals
    for (u32 i = 0; i < PARTY_SIZE; i++)
        MakeMon(&sFactoryChoices[i], SPECIES_NINCADA, FALSE);
    Expect("Battle Factory: the rental choices (battle_factory_screen.c sFactorySelectMons)", !SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, sFactoryChoices, 0, 5));
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_PRE_BATTLE_ROOM;
    Expect("Battle Factory: the rentals in the player's party (swap screen)", !SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, gParties[B_TRAINER_PLAYER], 0, 2));
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_BATTLE_ROOM;
    gMain.inBattle = TRUE;
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_FRONTIER;
    Expect("Battle Factory: the rentals in a battle's party menu", !SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], 0, 2));
    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;
    gMapHeader.regionMapSectionId = MAPSEC_SLATEPORT_CITY;
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_TENT_BATTLE_ROOM;
    Expect("Slateport Battle Tent: the rentals", !SummaryTest_IvEvOffered(SUMMARY_MODE_LOCK_MOVES, gParties[B_TRAINER_PLAYER], 0, 2));
    gMapHeader.mapLayoutId = LAYOUT_LITTLEROOT_TOWN;
    gMapHeader.regionMapSectionId = MAPSEC_LITTLEROOT_TOWN;
    Expect("back in the field: the player's own again", SummaryTest_IvEvOffered(SUMMARY_MODE_NORMAL, gParties[B_TRAINER_PLAYER], 0, 5));
}

int NativeSelfTest_SummaryIvEv(void)
{
    printf("selftest-summary-ivev: IVs and EVs on the summary screen, the player's own Pokemon only\n");
    Setup();
    TestOwn();
    TestNotOwn();
    printf("selftest-summary-ivev: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
