// Native PC build: Update 32 self-tests (the short introduction, the starter claimed in Birch's lab).
// Project-owned. Runs the game's own modules and save state before the game starts, then exits.
//   --selftest-update32
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_rival.h"
#include "overworld.h"
#include "pokemon.h"
#include "random.h"
#include "script.h"
#include "starter_choose.h"
#include "string_util.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/map_event_ids.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "constants/vars.h"

extern const u8 MpIntro_EventScript_LabBall[];
extern const u8 MpIntro_EventScript_LabBirch[];

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
    SetBagItemsPointers();
    ClearBag();
    ZeroPlayerPartyMons();
    MultiplayerRival_Init();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
}

static void SetMap(u32 group, u32 num)
{
    gSaveBlock1Ptr->location.mapGroup = group;
    gSaveBlock1Ptr->location.mapNum = num;
}

static bool32 Guard(s16 x, s16 y)
{
    struct MapPosition position = {.x = x + MAP_OFFSET, .y = y + MAP_OFFSET, .elevation = 3};
    bool8 started = MultiplayerIntro_TryStartGuardScript(&position);
    ScriptContext_Init();
    UnlockPlayerFieldControls();
    return started;
}

// What the 18-starter screen hands the claim, then the claim itself (the lab ball script's two specials).
static bool32 Claim(u16 choice)
{
    gSpecialVar_Result = choice;
    MultiplayerIntro_GiveLabStarter();
    return gSpecialVar_Result;
}

// A character back at its save of the arrival (what a soft reset reloads): no Pokemon, starter to claim.
static void BackToArrivalSave(void)
{
    ZeroPlayerPartyMons();
    FlagClear(FLAG_SYS_POKEMON_GET);
    VarSet(VAR_STARTER_MON, 0);
    VarSet(VAR_MULTIPLAYER_STARTER_CHOICE, MP_INTRO_STARTER_PENDING);
}

static void TestShortIntroState(void)
{
    u32 gender;

    for (gender = MALE; gender <= FEMALE; gender++)
    {
        char what[200];
        const char *g = gender == MALE ? "male" : "female";

        Setup();
        gSaveBlock2Ptr->playerGender = gender;
        MultiplayerIntro_TestApplyShortIntroState();
        snprintf(what, sizeof(what), "%s short introduction: no Pokemon, no starter flag, the claim is pending", g);
        Expect(what, CalculatePlayerPartyCount() == 0 && !FlagGet(FLAG_SYS_POKEMON_GET) && MultiplayerIntro_StarterPending()
               && VarGet(VAR_MULTIPLAYER_STARTER_CHOICE) == MP_INTRO_STARTER_PENDING);
        snprintf(what, sizeof(what), "%s short introduction: the story state of a character Birch sent to Route 103 (lab 3, Littleroot 2, Route 101 3)", g);
        Expect(what, VarGet(VAR_BIRCH_LAB_STATE) == 3 && VarGet(VAR_LITTLEROOT_TOWN_STATE) == 2 && VarGet(VAR_ROUTE101_STATE) == 3
               && VarGet(VAR_LITTLEROOT_INTRO_STATE) == 7 && FlagGet(FLAG_RESCUED_BIRCH) && !FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_BIRCH));
        snprintf(what, sizeof(what), "%s short introduction: Route 101's rescue scene never plays (Birch, Zigzagoon, bag of starters put away)", g);
        Expect(what, FlagGet(FLAG_HIDE_ROUTE_101_BIRCH_ZIGZAGOON_BATTLE) && FlagGet(FLAG_HIDE_ROUTE_101_ZIGZAGOON)
               && FlagGet(FLAG_HIDE_ROUTE_101_BIRCH_STARTERS_BAG));
        snprintf(what, sizeof(what), "%s short introduction: no Pokedex, Poke Balls, Running Shoes or Route 103 victory", g);
        Expect(what, !FlagGet(FLAG_SYS_POKEDEX_GET) && !FlagGet(FLAG_RECEIVED_RUNNING_SHOES) && !FlagGet(FLAG_DEFEATED_RIVAL_ROUTE103)
               && !CheckBagHasItem(ITEM_POKE_BALL, 1));
    }

    // Characters of the earlier introductions never see the lab's balls or the guard.
    Setup();
    gSaveBlock2Ptr->playerGender = MALE;
    MultiplayerIntro_TestApplyPostState();
    SavePlayerStarterChoice(5);
    Expect("an Update 31 character (its starter chosen on the 18-starter screen) has no starter to claim", !MultiplayerIntro_StarterPending());
    Setup();
    Expect("a character of the original introduction (no marker, before Route 101) has no starter to claim", !MultiplayerIntro_StarterPending());
    Setup();
    MultiplayerIntro_TestApplyShortIntroState();
    FlagSet(FLAG_SYS_POKEMON_GET);
    Expect("a short-introduction character whose claim is in the save (starter flag set) has nothing left to claim", !MultiplayerIntro_StarterPending());
}

static void TestGuard(void)
{
    Setup();
    MultiplayerIntro_TestApplyShortIntroState();
    SetMap(MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN));
    Expect("no Pokemon yet: both tiles of the path to Route 101 turn the character back", Guard(10, 1) && Guard(11, 1));
    Expect("no Pokemon yet: the rest of Littleroot is free (the lab door, the houses, the row below the path)",
           !Guard(10, 2) && !Guard(11, 2) && !Guard(7, 17) && !Guard(5, 9) && !Guard(12, 1) && !Guard(9, 1));
    SetMap(MAP_GROUP(MAP_ROUTE101), MAP_NUM(MAP_ROUTE101));
    Expect("the guard is Littleroot's only (the same coordinates elsewhere do nothing)", !Guard(10, 1) && !Guard(11, 1));
    SetMap(MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN));
    gSpecialVar_Result = 0;
    MultiplayerIntro_GiveLabStarter();
    Expect("after the claim the path is open", !Guard(10, 1) && !Guard(11, 1));
    Setup();
    gSaveBlock2Ptr->playerGender = FEMALE;
    MultiplayerIntro_TestApplyPostState();
    SavePlayerStarterChoice(2);
    SetMap(MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN));
    Expect("an Update 31 character walks out freely", !Guard(10, 1) && !Guard(11, 1));
}

static struct ObjectEvent LabObject(u8 localId, u32 mapGroup, u32 mapNum)
{
    struct ObjectEvent objectEvent;
    memset(&objectEvent, 0, sizeof(objectEvent));
    objectEvent.localId = localId;
    objectEvent.mapGroup = mapGroup;
    objectEvent.mapNum = mapNum;
    return objectEvent;
}

static void LoadMap(u32 group, u32 num)
{
    SetMap(group, num);
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(group, num);
    gSaveBlock1Ptr->mapLayoutId = gMapHeader.mapLayoutId;
    gMapHeader.mapLayout = GetMapLayout(gMapHeader.mapLayoutId);
    LoadObjEventTemplatesFromHeader();
}

static const struct ObjectEventTemplate *Template(u8 localId)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENT_TEMPLATES_COUNT; i++)
    {
        if (gSaveBlock1Ptr->objectEventTemplates[i].localId == localId)
            return &gSaveBlock1Ptr->objectEventTemplates[i];
    }
    return NULL;
}

static void TestLab(void)
{
    const u32 labGroup = MAP_GROUP(MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB), labNum = MAP_NUM(MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB);
    static const u8 sBalls[] = {LOCALID_BIRCHS_LAB_CHIKORITA_BALL, LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL, LOCALID_BIRCHS_LAB_TOTODILE_BALL};
    struct ObjectEvent birch = LabObject(LOCALID_BIRCHS_LAB_BIRCH, labGroup, labNum);
    struct ObjectEvent aide = LabObject(LOCALID_BIRCHS_LAB_AIDE, labGroup, labNum);
    struct ObjectEvent ballElsewhere = LabObject(LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL, MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN));
    const struct ObjectEventTemplate *t[3];
    bool32 shows = TRUE, scripts = TRUE, flagsKept = TRUE;
    u32 i;

    Setup();
    MultiplayerIntro_TestApplyShortIntroState();
    // New game: the Johto ball flags are set (data/scripts/new_game.inc); the balls show by the lab rule alone.
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CYNDAQUIL);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_TOTODILE);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CHIKORITA);
    LoadMap(labGroup, labNum);
    MultiplayerIntro_OnTransition();
    for (i = 0; i < 3; i++)
    {
        struct ObjectEvent ball = LabObject(sBalls[i], labGroup, labNum);
        t[i] = Template(sBalls[i]);
        shows &= MultiplayerIntro_ShowsObject(sBalls[i], labNum, labGroup);
        scripts &= MultiplayerIntro_ObjectScript(&ball) == MpIntro_EventScript_LabBall;
    }
    Expect("to claim: the lab has the table layout of the Johto starters", gSaveBlock1Ptr->mapLayoutId == LAYOUT_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB_WITH_TABLE);
    Expect("to claim: the three balls stand on the table (8,4) Grass, (9,4) Fire, (10,4) Water",
           t[0] && t[1] && t[2] && t[0]->x == 8 && t[0]->y == 4 && t[1]->x == 9 && t[1]->y == 4 && t[2]->x == 10 && t[2]->y == 4);
    Expect("to claim: the three balls show although their flags are set", shows);
    Expect("to claim: each ball runs the claim script, Birch his own lines, the aide hers",
           scripts && MultiplayerIntro_ObjectScript(&birch) == MpIntro_EventScript_LabBirch && MultiplayerIntro_ObjectScript(&aide) == NULL);
    Expect("to claim: Birch and the aide are not made to show, nor a ball of another map",
           !MultiplayerIntro_ShowsObject(LOCALID_BIRCHS_LAB_BIRCH, labNum, labGroup) && !MultiplayerIntro_ShowsObject(LOCALID_BIRCHS_LAB_AIDE, labNum, labGroup)
           && !MultiplayerIntro_ShowsObject(LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL, MAP_NUM(MAP_LITTLEROOT_TOWN), MAP_GROUP(MAP_LITTLEROOT_TOWN))
           && MultiplayerIntro_ObjectScript(&ballElsewhere) == NULL);

    Expect("claim: the starter of the chosen ball joins the party", Claim(3) && CalculatePlayerPartyCount() == 1
           && GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES) == SPECIES_BULBASAUR);
    Expect("claim: level 5, met in Littleroot, the starter flag and the chosen starter saved (0x6100 | 3, Grass for old scripts)",
           GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_LEVEL) == 5
           && GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION) == MAPSEC_LITTLEROOT_TOWN
           && FlagGet(FLAG_SYS_POKEMON_GET) && VarGet(VAR_MULTIPLAYER_STARTER_CHOICE) == 0x6103 && VarGet(VAR_STARTER_MON) == 0
           && GetPlayerStarterPokemon() == SPECIES_BULBASAUR);
    Expect("claim: the message names it", StringCompare(gStringVar1, GetSpeciesName(SPECIES_BULBASAUR)) == 0);
    Expect("claim: nothing more to claim; a second ball gives nothing", !MultiplayerIntro_StarterPending() && !Claim(5)
           && CalculatePlayerPartyCount() == 1);
    shows = FALSE;
    for (i = 0; i < 3; i++)
    {
        struct ObjectEvent ball = LabObject(sBalls[i], labGroup, labNum);
        shows |= MultiplayerIntro_ShowsObject(sBalls[i], labNum, labGroup);
        scripts = MultiplayerIntro_ObjectScript(&ball) != NULL;
    }
    flagsKept = FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CYNDAQUIL) && FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_TOTODILE)
             && FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CHIKORITA);
    Expect("after the claim: the balls are gone for good and their objects run their original scripts again", !shows && !scripts);
    Expect("after the claim: the Johto ball flags are untouched (the post-game Johto starters still work)", flagsKept);
    LoadMap(labGroup, labNum);
    MultiplayerIntro_OnTransition();
    Expect("after the claim: the lab loads with its own layout and the balls at their own places",
           gSaveBlock1Ptr->mapLayoutId == LAYOUT_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB && Template(LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL)->x == 6
           && Template(LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL)->y == 8);

    Setup();
    MultiplayerIntro_TestApplyShortIntroState();
    Expect("backing out of the screen (B) gives nothing and the claim stays", !Claim(MP_INTRO_NO_STARTER) && CalculatePlayerPartyCount() == 0
           && MultiplayerIntro_StarterPending());
    ZeroPlayerPartyMons();
    CreateRandomMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_ZIGZAGOON, 5); // a defensive case: a Pokemon without the flag
    Expect("a party that is not empty never gets a lab starter (no second Pokemon)", !Claim(1) && GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES) == SPECIES_ZIGZAGOON
           && MultiplayerIntro_StarterPending());
    {
        bool32 all = TRUE;
        for (i = 0; i < 18; i++)
        {
            Setup();
            MultiplayerIntro_TestApplyShortIntroState();
            all &= Claim(i) && GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES) == GetStarterPokemon(i)
                && VarGet(VAR_STARTER_MON) == i % 3 && VarGet(VAR_MULTIPLAYER_STARTER_CHOICE) == (0x6100 | i)
                && GetPlayerStarterPokemon() == GetStarterPokemon(i);
        }
        Expect("each of the 18 starters can be claimed and is the one the rival scenes read", all);
    }
    Setup();
    gSaveBlock2Ptr->playerGender = MALE;
    MultiplayerIntro_TestApplyPostState();
    SavePlayerStarterChoice(7);
    CreateRandomMon(&gParties[B_TRAINER_PLAYER][0], GetStarterPokemon(7), 5);
    LoadMap(labGroup, labNum);
    MultiplayerIntro_OnTransition();
    Expect("an Update 31 character's lab: no table, no balls, no claim", gSaveBlock1Ptr->mapLayoutId == LAYOUT_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB
           && !MultiplayerIntro_ShowsObject(LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL, labNum, labGroup) && MultiplayerIntro_ObjectScript(&birch) == NULL
           && !Claim(0) && CalculatePlayerPartyCount() == 1);
}

// Shiny hunting: back to the save of the arrival, claim, look, reset, claim again. Every claim rolls a new
// Pokemon from the game's random state of that moment through the game's own CreateMon (1/4096 without the
// Shiny Charm, src/pokemon.c ComputePlayerShinyOdds; the reset's fresh random state is the soft reset's own test).
static void TestRerolls(void)
{
    const u32 n = 400000;
    const double p = 16.0 / 65536.0, expected = n * p, sigma = __builtin_sqrt(expected * (1.0 - p));
    u32 i, shiny = 0, repeats = 0, wrongCount = 0, lastPersonality = 0;
    double diff;
    char what[200];

    Setup();
    MultiplayerIntro_TestApplyShortIntroState();
    SeedRng(0x320C1A1Du);
    for (i = 0; i < n; i++)
    {
        u32 personality;

        BackToArrivalSave();
        if (!Claim(1) || CalculatePlayerPartyCount() != 1)
            wrongCount++;
        personality = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_PERSONALITY);
        if (i > 0 && personality == lastPersonality)
            repeats++;
        lastPersonality = personality;
        if (GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_SHINY))
            shiny++;
    }
    diff = (double)shiny - expected;
    printf("lab starter rerolls: %u shiny of %u claims (expected %.0f +- %.0f), 1 in %.1f; %u repeated personalities\n",
           shiny, n, expected, sigma, shiny ? (double)n / shiny : 0.0, repeats);
    Expect("every claim of the reroll loop gives exactly one Pokemon", wrongCount == 0);
    Expect("every claim rolls a new Pokemon (no claim repeats the one before)", repeats == 0);
    snprintf(what, sizeof(what), "the claimed starter is shiny 1 in 4096 without the Shiny Charm (within 4.5 sigma)");
    Expect(what, diff > -4.5 * sigma && diff < 4.5 * sigma);
}

int NativeSelfTest_Update32(void)
{
    printf("selftest-update32: the short introduction and the starter claim in Birch's lab\n");
    TestShortIntroState();
    TestGuard();
    TestLab();
    TestRerolls();
    printf("selftest-update32: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
