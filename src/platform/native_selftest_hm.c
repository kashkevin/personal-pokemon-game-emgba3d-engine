// Native PC build: self-test of the HMs used from the PC boxes (src/field_move.c; the owner's request of 2026-10-24).
// Project-owned. Runs the game's own code (the box search, the checkfieldmove script command, Surf's check, the party
// index of a boxed user in the messages and in the field move's cut-in) on save state built here, then exits.
//   --selftest-hm-box
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "field_effect.h"
#include "field_move.h"
#include "field_player_avatar.h"
#include "field_weather.h"
#include "load_save.h"
#include "malloc.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "script.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "constants/characters.h"
#include "constants/field_effects.h"
#include "constants/field_move.h"
#include "constants/flags.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/vars.h"

bool8 ScrCmd_checkfieldmove(struct ScriptContext *ctx);
bool8 ScrCmd_bufferpartymonnick(struct ScriptContext *ctx);
bool8 FldEff_FieldMoveShowMonInit(void);

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    ZeroPlayerPartyMons();
    gPlayerAvatar.flags = PLAYER_AVATAR_FLAG_ON_FOOT;
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gSpecialVar_Result = 0xFFFF;
    gSpecialVar_0x8004 = 0xFFFF;
}

// A Pokemon of `species` that knows `move` in the given move slot (0-3) and its level-up moves elsewhere
static void MakeMon(struct Pokemon *mon, u32 species, u32 personality, enum Move move, u32 moveSlot)
{
    CreateMon(mon, species, 30, personality, OTID_STRUCT_PLAYER_ID);
    GiveMonInitialMoveset(mon);
    if (move != MOVE_NONE)
        SetMonMoveSlot(mon, move, moveSlot);
}

static void PutInParty(u32 slot, u32 species, enum Move move)
{
    MakeMon(&gParties[B_TRAINER_PLAYER][slot], species, 0x1000 + slot, move, 0);
    CalculatePlayerPartyCount();
}

static void PutInBox(u32 box, u32 slot, u32 species, u32 personality, enum Move move, u32 moveSlot, bool32 egg)
{
    struct Pokemon mon;

    MakeMon(&mon, species, personality, move, moveSlot);
    if (egg)
    {
        bool32 isEgg = TRUE;

        SetMonData(&mon, MON_DATA_IS_EGG, &isEgg);
    }
    SetBoxMonAt(box, slot, &mon.box);
}

// checkfieldmove <fieldMove>, <checkUnlocked> as a script runs it
static u16 CheckFieldMove(enum FieldMove fieldMove, bool32 checkUnlocked)
{
    struct ScriptContext ctx = {0};
    u8 script[2] = {fieldMove, checkUnlocked};

    ctx.scriptPtr = script;
    ScrCmd_checkfieldmove(&ctx);
    return gSpecialVar_Result;
}

// bufferpartymonnick STR_VAR_1, VAR_RESULT as a script runs it
static const u8 *BufferNickOfResult(void)
{
    struct ScriptContext ctx = {0};
    u8 script[3] = {0, VAR_RESULT & 0xFF, VAR_RESULT >> 8};

    ctx.scriptPtr = script;
    gStringVar1[0] = EOS;
    ScrCmd_bufferpartymonnick(&ctx);
    return gStringVar1;
}

static bool32 SameNickname(const u8 *a, struct BoxPokemon *boxMon)
{
    u8 nickname[POKEMON_NAME_BUFFER_SIZE + 4];

    GetBoxMonData(boxMon, MON_DATA_NICKNAME, nickname);
    StringGet_Nickname(nickname);
    return StringCompare(a, nickname) == 0;
}

static void TestBoxSearch(void)
{
    u32 box = 99, slot = 99;

    Setup();
    Expect("empty boxes: no Pokemon knows Cut", !FieldMove_FindBoxMonWithMove(MOVE_CUT, &box, &slot));
    Expect("empty boxes: MOVE_NONE is never 'known' (an empty move slot)", !FieldMove_FindBoxMonWithMove(MOVE_NONE, NULL, NULL));

    PutInBox(TOTAL_BOXES_COUNT - 1, IN_BOX_COUNT - 1, SPECIES_FARFETCHD, 0x2222, MOVE_CUT, 3, FALSE);
    Expect("the last slot of the last box, Cut in the fourth move slot: found there",
           FieldMove_FindBoxMonWithMove(MOVE_CUT, &box, &slot) && box == TOTAL_BOXES_COUNT - 1 && slot == IN_BOX_COUNT - 1);
    Expect("the same box: MOVE_NONE still not found", !FieldMove_FindBoxMonWithMove(MOVE_NONE, NULL, NULL));

    PutInBox(0, 0, SPECIES_ZIGZAGOON, 0x3333, MOVE_CUT, 0, TRUE);
    Expect("an Egg that knows Cut in box 1 slot 1 is skipped: still the last box's Pokemon",
           FieldMove_FindBoxMonWithMove(MOVE_CUT, &box, &slot) && box == TOTAL_BOXES_COUNT - 1 && slot == IN_BOX_COUNT - 1);

    PutInBox(2, 5, SPECIES_SCYTHER, 0x4444, MOVE_CUT, 1, FALSE);
    Expect("two boxed Pokemon know Cut: the first in box order (box 3 slot 6)",
           FieldMove_FindBoxMonWithMove(MOVE_CUT, &box, &slot) && box == 2 && slot == 5);

    PutInBox(1, 7, SPECIES_LOTAD, 0x5555, MOVE_WATERFALL, 0, FALSE);
    Expect("a Lotad that knows Waterfall in box 2 slot 8: found", FieldMove_FindBoxMonWithMove(MOVE_WATERFALL, &box, &slot)
           && box == 1 && slot == 7);
    SetBoxMonDataAt(1, 7, MON_DATA_SANITY_IS_BAD_EGG, &(u8){TRUE});
    Expect("the same Pokemon made a bad Egg: never counts", GetBoxMonDataAt(1, 7, MON_DATA_SANITY_IS_BAD_EGG)
           && !FieldMove_FindBoxMonWithMove(MOVE_WATERFALL, NULL, NULL));
    Expect("a move nobody knows: not found", !FieldMove_FindBoxMonWithMove(MOVE_SURF, NULL, NULL));
}

static void TestCheckFieldMove(void)
{
    struct Pokemon partyBefore[PARTY_SIZE];
    struct BoxPokemon boxBefore;
    struct BoxPokemon *user;
    u16 result;

    // The party knows Cut (slot 2) and a box too: the party Pokemon is the user.
    Setup();
    FlagSet(FLAG_BADGE01_GET);
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    PutInParty(1, SPECIES_ZIGZAGOON, MOVE_CUT);
    PutInBox(4, 3, SPECIES_FARFETCHD, 0x6666, MOVE_CUT, 2, FALSE);
    result = CheckFieldMove(FIELD_MOVE_CUT, TRUE);
    Expect("party and box know Cut: the party Pokemon (slot 2) uses it", result == 1 && gSpecialVar_0x8004 == SPECIES_ZIGZAGOON);

    // Only the box knows Cut: the boxed Pokemon, as FIELD_MOVE_BOX_USER, its species in VAR_0x8004.
    Setup();
    FlagSet(FLAG_BADGE01_GET);
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    PutInParty(1, SPECIES_ZIGZAGOON, MOVE_NONE);
    PutInBox(4, 3, SPECIES_FARFETCHD, 0x6666, MOVE_CUT, 2, FALSE);
    {
        bool32 shiny = TRUE;

        SetBoxMonDataAt(4, 3, MON_DATA_IS_SHINY, &shiny);
    }
    memcpy(partyBefore, gParties[B_TRAINER_PLAYER], sizeof(partyBefore));
    boxBefore = gPokemonStoragePtr->boxes[4][3];
    result = CheckFieldMove(FIELD_MOVE_CUT, TRUE);
    Expect("only a boxed Pokemon knows Cut (with the badge): it uses it, as FIELD_MOVE_BOX_USER",
           result == FIELD_MOVE_BOX_USER && gSpecialVar_0x8004 == SPECIES_FARFETCHD);
    user = FieldMove_UserBoxMon(FIELD_MOVE_BOX_USER);
    Expect("the boxed user's copy: its species, personality, shininess and Cut",
           GetBoxMonData(user, MON_DATA_SPECIES) == SPECIES_FARFETCHD && GetBoxMonData(user, MON_DATA_PERSONALITY) == 0x6666
           && GetBoxMonData(user, MON_DATA_IS_SHINY) && GetBoxMonData(user, MON_DATA_MOVE3) == MOVE_CUT);
    Expect("bufferpartymonnick of FIELD_MOVE_BOX_USER: the boxed Pokemon's name",
           SameNickname(BufferNickOfResult(), &gPokemonStoragePtr->boxes[4][3]));
    gSpecialVar_Result = 1;
    Expect("bufferpartymonnick of a party slot: still that party Pokemon's name",
           SameNickname(BufferNickOfResult(), &gParties[B_TRAINER_PLAYER][1].box));
    Expect("the check moved nothing: the party and the box are as they were",
           memcmp(partyBefore, gParties[B_TRAINER_PLAYER], sizeof(partyBefore)) == 0
           && memcmp(&boxBefore, &gPokemonStoragePtr->boxes[4][3], sizeof(boxBefore)) == 0 && CalculatePlayerPartyCount() == 2);

    // The cut-in's Init reads the boxed copy for FIELD_MOVE_BOX_USER, never a party slot (then the cut-in itself starts:
    // its task and the Pokemon's picture, as on the field).
    ResetPreservedPalettesInWeather();
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    FieldEffectActiveListClear();
    gFieldEffectArguments[0] = FIELD_MOVE_BOX_USER | (1u << 31);
    FldEff_FieldMoveShowMonInit();
    Expect("the cut-in for FIELD_MOVE_BOX_USER shows the boxed Pokemon (species, shiny, personality; Surf's no-ducking bit kept)",
           (gFieldEffectArguments[0] & 0xFFFF) == SPECIES_FARFETCHD && (gFieldEffectArguments[0] & (1u << 31))
           && gFieldEffectArguments[1] == TRUE && gFieldEffectArguments[2] == 0x6666);
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    FieldEffectActiveListClear();
    gFieldEffectArguments[0] = 1;
    FldEff_FieldMoveShowMonInit();
    Expect("the cut-in for party slot 2 still shows that party Pokemon",
           gFieldEffectArguments[0] == SPECIES_ZIGZAGOON && gFieldEffectArguments[2] == 0x1001);
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    FieldEffectActiveListClear();

    // A box changed after the check: the cut-in shows the Pokemon that was chosen (the copy), not what is there now.
    result = CheckFieldMove(FIELD_MOVE_CUT, TRUE);
    ZeroBoxMonAt(4, 3);
    Expect("the boxed user's copy outlives a change to its box", result == FIELD_MOVE_BOX_USER
           && GetBoxMonData(FieldMove_UserBoxMon(FIELD_MOVE_BOX_USER), MON_DATA_SPECIES) == SPECIES_FARFETCHD);
    Expect("its box emptied: Cut is no longer known anywhere", CheckFieldMove(FIELD_MOVE_CUT, TRUE) == PARTY_SIZE);

    // Neither the party nor the boxes know it: refused as before.
    Setup();
    FlagSet(FLAG_BADGE01_GET);
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    PutInBox(0, 0, SPECIES_ZIGZAGOON, 0x7777, MOVE_TACKLE, 0, FALSE);
    Expect("nobody knows Cut: PARTY_SIZE (the game's refusal)", CheckFieldMove(FIELD_MOVE_CUT, TRUE) == PARTY_SIZE);

    // The badge rules stay: a boxed Pokemon does not get round them.
    Setup();
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    PutInBox(0, 0, SPECIES_FARFETCHD, 0x6666, MOVE_CUT, 0, FALSE);
    Expect("Cut known in a box, no Stone Badge: refused (PARTY_SIZE)", CheckFieldMove(FIELD_MOVE_CUT, TRUE) == PARTY_SIZE);
    FlagSet(FLAG_BADGE01_GET);
    Expect("... and with the Stone Badge: the boxed Pokemon", CheckFieldMove(FIELD_MOVE_CUT, TRUE) == FIELD_MOVE_BOX_USER);
    Expect("Rock Smash known nowhere with all badges: refused", CheckFieldMove(FIELD_MOVE_ROCK_SMASH, TRUE) == PARTY_SIZE);

    // Eggs never count, in the party or the boxes.
    Setup();
    FlagSet(FLAG_BADGE03_GET);
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    MakeMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_MAKUHITA, 0x8888, MOVE_ROCK_SMASH, 0);
    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_IS_EGG, &(bool32){TRUE});
    CalculatePlayerPartyCount();
    PutInBox(3, 3, SPECIES_GEODUDE, 0x9999, MOVE_ROCK_SMASH, 0, TRUE);
    Expect("Rock Smash known by an Egg in the party and one in a box: refused", CheckFieldMove(FIELD_MOVE_ROCK_SMASH, TRUE) == PARTY_SIZE);
    PutInBox(3, 4, SPECIES_GEODUDE, 0x9998, MOVE_ROCK_SMASH, 0, FALSE);
    Expect("... a hatched Geodude beside the Egg: it uses Rock Smash", CheckFieldMove(FIELD_MOVE_ROCK_SMASH, TRUE) == FIELD_MOVE_BOX_USER
           && GetBoxMonData(FieldMove_UserBoxMon(FIELD_MOVE_BOX_USER), MON_DATA_PERSONALITY) == 0x9998);

    // Every HM; other field moves (TMs and level-up moves) still need the party.
    {
        static const struct { enum FieldMove fieldMove; u32 badgeFlag; const char *name; } sHms[] = {
            {FIELD_MOVE_CUT, FLAG_BADGE01_GET, "Cut"}, {FIELD_MOVE_FLASH, FLAG_BADGE02_GET, "Flash"},
            {FIELD_MOVE_ROCK_SMASH, FLAG_BADGE03_GET, "Rock Smash"}, {FIELD_MOVE_STRENGTH, FLAG_BADGE04_GET, "Strength"},
            {FIELD_MOVE_SURF, FLAG_BADGE05_GET, "Surf"}, {FIELD_MOVE_FLY, FLAG_BADGE06_GET, "Fly"},
            {FIELD_MOVE_DIVE, FLAG_BADGE07_GET, "Dive"}, {FIELD_MOVE_WATERFALL, FLAG_BADGE08_GET, "Waterfall"},
        };
        char what[160];

        for (u32 i = 0; i < ARRAY_COUNT(sHms); i++)
        {
            Setup();
            PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
            PutInBox(7, 11, SPECIES_SMEARGLE, 0xAAAA, FieldMove_GetMoveId(sHms[i].fieldMove), 1, FALSE);
            snprintf(what, sizeof(what), "%s known in a box: refused without its badge", sHms[i].name);
            Expect(what, CheckFieldMove(sHms[i].fieldMove, TRUE) == PARTY_SIZE);
            FlagSet(sHms[i].badgeFlag);
            snprintf(what, sizeof(what), "%s known in a box: the boxed Pokemon with its badge", sHms[i].name);
            Expect(what, CheckFieldMove(sHms[i].fieldMove, TRUE) == FIELD_MOVE_BOX_USER);
            snprintf(what, sizeof(what), "%s known in a box: the boxed Pokemon where the script checks no badge", sHms[i].name);
            Expect(what, CheckFieldMove(sHms[i].fieldMove, FALSE) == FIELD_MOVE_BOX_USER);
        }
        Setup();
        PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
        PutInBox(0, 0, SPECIES_SMEARGLE, 0xBBBB, MOVE_SECRET_POWER, 0, FALSE);
        Expect("Secret Power (a TM) known only in a box: still the party's (PARTY_SIZE)", CheckFieldMove(FIELD_MOVE_SECRET_POWER, FALSE) == PARTY_SIZE);
        PutInBox(0, 1, SPECIES_SMEARGLE, 0xBBBC, MOVE_SWEET_SCENT, 0, FALSE);
        Expect("Sweet Scent known only in a box: still the party's", CheckFieldMove(FIELD_MOVE_SWEET_SCENT, FALSE) == PARTY_SIZE);
    }
}

static void TestSurfAndPartyMenu(void)
{
    Setup();
    PutInParty(0, SPECIES_TORCHIC, MOVE_NONE);
    Expect("Surf known nowhere: PartyHasMonWithSurf is FALSE", !PartyHasMonWithSurf());
    PutInBox(9, 20, SPECIES_WAILMER, 0xCCCC, MOVE_SURF, 0, TRUE);
    Expect("Surf known only by a boxed Egg: still FALSE", !PartyHasMonWithSurf());
    PutInBox(9, 21, SPECIES_WAILMER, 0xCCCD, MOVE_SURF, 3, FALSE);
    Expect("Surf known by a boxed Pokemon: PartyHasMonWithSurf is TRUE (the water's prompt is offered)", PartyHasMonWithSurf());
    gPlayerAvatar.flags = PLAYER_AVATAR_FLAG_SURFING;
    Expect("... but not while already surfing", !PartyHasMonWithSurf());
    gPlayerAvatar.flags = PLAYER_AVATAR_FLAG_ON_FOOT;
    Expect("FieldMove_PartyMonKnowsMove: the party does not know Surf", !FieldMove_PartyMonKnowsMove(MOVE_SURF));
    PutInParty(1, SPECIES_MARILL, MOVE_SURF);
    Expect("FieldMove_PartyMonKnowsMove: a party Marill knows Surf", FieldMove_PartyMonKnowsMove(MOVE_SURF));

    // The party menu's user: the chosen party slot, or the boxed Pokemon once a move from the boxes was chosen there.
    FieldMove_SetPartyMenuUserBoxed(FALSE);
    Expect("party menu: a party Pokemon's own field move is that slot's", FieldMove_PartyMenuUser(3) == 3);
    FieldMove_SetPartyMenuUserBoxed(TRUE);
    Expect("party menu: Fly or Flash offered from the boxes is FIELD_MOVE_BOX_USER's", FieldMove_PartyMenuUser(3) == FIELD_MOVE_BOX_USER);
    FieldMove_SetPartyMenuUserBoxed(FALSE);
}

int NativeSelfTest_HmBox(void)
{
    printf("selftest-hm-box: HMs used from the PC boxes\n");
    TestBoxSearch();
    TestCheckFieldMove();
    TestSurfAndPartyMenu();
    printf("selftest-hm-box: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
