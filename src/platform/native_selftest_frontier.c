// Native PC build: saving during a Battle Frontier or Battle Tent challenge in a world's save (the owner's request of
// 2026-10-27, every minigame playable; docs/gameplay-visual-updates-20261027.md section 3 in the app repo). Project-owned.
// A save enrolled with a world refused every save while a challenge's status or its paused flag was set: the lobby's
// opening save (the status is set just before it) failed, so no challenge could start, and every finished challenge
// leaves the paused flag set, which refused every save after. The rule now (MultiplayerOwnership_CanSaveParty): during a
// challenge a save is allowed exactly when the party in memory is the whole party the save block keeps, which is what
// every save of a challenge's own writes (they load it first); never while its chosen few or rentals are in play.
// Nothing here writes the save file: only the game's own decision is checked, run before the game starts; then it exits.
//   --selftest-frontier-saves
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle_pyramid.h"
#include "load_save.h"
#include "main.h"
#include "malloc.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "overworld.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "script_pokemon_util.h"
#include "constants/battle_frontier.h"
#include "constants/layouts.h"
#include "constants/species.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void PutWord(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

// The world's grant as the host sends it (test/multiplayer_ownership.c MakeGrant): a legacy save enrolls with flags 1.
static void MakeGrant(u8 *data, u32 start, u32 end, u32 flags)
{
    memset(data, 0, 60);
    data[0] = 1; data[16] = 2; data[32] = 3;
    PutWord(data + 48, start); PutWord(data + 52, end); PutWord(data + 56, flags);
}

static const u16 sParty[PARTY_SIZE] = {SPECIES_SCEPTILE, SPECIES_PIKACHU, SPECIES_GARDEVOIR, SPECIES_AGGRON,
                                       SPECIES_ALTARIA, SPECIES_MILOTIC};

static void Setup(void)
{
    u32 i;

    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    for (i = 0; i < PARTY_SIZE; i++)
        CreateMon(&gParties[B_TRAINER_PLAYER][i], sParty[i], 50, 0x1000 + i * 0x111, OTID_STRUCT_PLAYER_ID);
    CalculatePlayerPartyCount();
    gMain.inBattle = FALSE;
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_LOBBY;
    MultiplayerOwnership_Loaded();
}

static void ClearChallenge(void)
{
    gSaveBlock2Ptr->frontier.challengeStatus = 0;
    gSaveBlock2Ptr->frontier.challengePaused = FALSE;
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_TOWER_LOBBY;
}

// The challenge's three: the Battle Tower lobby's order (slots 2, 4, 1), as ChoosePartyForBattleFrontier leaves it.
static void ReduceToChosenThree(void)
{
    ClearSelectedPartyOrder();
    gSelectedOrderFromParty[0] = 2;
    gSelectedOrderFromParty[1] = 4;
    gSelectedOrderFromParty[2] = 1;
    ReducePlayerPartyToSelectedMons();
}

int NativeSelfTest_FrontierSaves(void)
{
    u8 grant[60];
    u32 i;

    printf("selftest-frontier-saves: saving during a Battle Frontier or Battle Tent challenge (multiplayer_ownership.c)\n");

    // An offline (not enrolled) save: unchanged, every save allowed as in the original game
    Setup();
    Expect("an offline save is not enrolled", !MultiplayerOwnership_IsEnrolled());
    gSaveBlock2Ptr->frontier.challengeStatus = CHALLENGE_STATUS_SAVING;
    Expect("offline: a save during a challenge is allowed (the original game's rule)", MultiplayerOwnership_CanSaveParty());
    ClearChallenge();

    // The same save enrolled with a world
    MakeGrant(grant, 1, 33, 1);
    Expect("the world's grant enrolls the save", MultiplayerOwnership_Grant(grant, 1, 1) == MP_SAVE_OK);
    Expect("the save is enrolled", MultiplayerOwnership_IsEnrolled());
    Expect("enrolled, no challenge: a save is allowed", MultiplayerOwnership_CanSaveParty());
    Expect("no challenge is on", !MultiplayerOwnership_InFrontierChallenge());

    // The Battle Tower lobby: the attendant keeps the party (SavePlayerParty), the three are chosen, then tower_init sets
    // the status to SAVING, LoadPlayerParty brings the whole party back and the lobby saves (Common_EventScript_SaveGame)
    SavePlayerParty();
    ReduceToChosenThree();
    gSaveBlock2Ptr->frontier.challengeStatus = CHALLENGE_STATUS_SAVING;
    gSaveBlock2Ptr->frontier.challengePaused = FALSE;
    LoadPlayerParty();
    Expect("a challenge is on (status SAVING)", MultiplayerOwnership_InFrontierChallenge());
    Expect("the lobby's opening save (the whole party loaded) is allowed - it was refused, so no challenge could start",
           MultiplayerOwnership_CanSaveParty());

    // In the battle rooms: the party is the chosen three (frontier_setpartyorder)
    ReduceToChosenThree();
    Expect("the chosen three are in play: a save is refused (it would write three Pokemon over the six)",
           !MultiplayerOwnership_CanSaveParty());
    gMain.inBattle = TRUE;
    Expect("in a battle: refused", !MultiplayerOwnership_CanSaveParty());
    gMain.inBattle = FALSE;

    // A rest between battles: SaveTowerChallenge sets PAUSED and paused, then SaveGameFrontier loads the whole party,
    // saves, and puts the three back
    gSaveBlock2Ptr->frontier.challengeStatus = CHALLENGE_STATUS_PAUSED;
    gSaveBlock2Ptr->frontier.challengePaused = TRUE;
    {
        struct Pokemon chosen[PARTY_SIZE];
        u8 count = gPartiesCount[B_TRAINER_PLAYER];

        memcpy(chosen, gParties[B_TRAINER_PLAYER], sizeof(chosen));
        LoadPlayerParty();
        Expect("SaveGameFrontier's save (the whole party loaded) is allowed", MultiplayerOwnership_CanSaveParty());
        memcpy(gParties[B_TRAINER_PLAYER], chosen, sizeof(chosen));
        gPartiesCount[B_TRAINER_PLAYER] = count;
    }
    Expect("after it, the three again: refused", !MultiplayerOwnership_CanSaveParty());

    // The Battle Factory and the Slateport Tent: rentals in the party (other Pokemon entirely)
    for (i = 0; i < 3; i++)
        CreateMon(&gParties[B_TRAINER_PLAYER][i], SPECIES_MACHAMP + i, 50, 0xBEEF00 + i, OTID_STRUCT_PLAYER_ID);
    gSaveBlock2Ptr->frontier.challengeStatus = CHALLENGE_STATUS_SAVING;
    Expect("rentals in play: a save is refused", !MultiplayerOwnership_CanSaveParty());

    // The challenge ends: the lobby loads the whole party and saves with status 0 (tower_save 0), which sets paused
    LoadPlayerParty();
    gSaveBlock2Ptr->frontier.challengeStatus = 0;
    gSaveBlock2Ptr->frontier.challengePaused = TRUE;
    Expect("the end of a challenge: its last save is allowed", MultiplayerOwnership_CanSaveParty());
    Expect("status 0 with the paused flag left set: no challenge is on", !MultiplayerOwnership_InFrontierChallenge());
    // later the party changes (a Pokemon caught, the order changed) and the flag is still set from the challenge
    CreateMon(&gParties[B_TRAINER_PLAYER][5], SPECIES_ABSOL, 30, 0x777, OTID_STRUCT_PLAYER_ID);
    Expect("a later save with a changed party is allowed - the flag left over refused every save after a challenge",
           MultiplayerOwnership_CanSaveParty());

    // The Battle Pyramid plays its floors with the status cleared and the chosen three in the party
    SavePlayerParty();
    ReduceToChosenThree();
    gSaveBlock2Ptr->frontier.challengeStatus = 0;
    gSaveBlock2Ptr->frontier.challengePaused = FALSE;
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR;
    Expect("inside the Pyramid (status 0) a challenge is on", MultiplayerOwnership_InFrontierChallenge());
    Expect("inside the Pyramid with the three in play: refused (allowed before, it would have written three over six)",
           !MultiplayerOwnership_CanSaveParty());
    // its rest: PausePyramidChallenge sets PAUSED and loads the whole party before the start menu's save
    gSaveBlock2Ptr->frontier.challengeStatus = CHALLENGE_STATUS_PAUSED;
    LoadPlayerParty();
    Expect("the Pyramid's rest (the whole party loaded) is allowed", MultiplayerOwnership_CanSaveParty());
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_TOP;
    ReduceToChosenThree();
    Expect("at the Pyramid's top with the three in play: refused", !MultiplayerOwnership_CanSaveParty());
    ClearChallenge();
    LoadPlayerParty();
    Expect("back outside, no challenge: allowed", MultiplayerOwnership_CanSaveParty());

    printf("selftest-frontier-saves: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
