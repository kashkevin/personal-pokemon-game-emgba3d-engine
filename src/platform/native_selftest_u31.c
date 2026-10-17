// Native PC build: Update 31 self-tests (multiplayer rival checkpoints, the streamlined introduction's save state).
// Project-owned. Runs the game's own rival module and save functions before the game starts, then exits.
//   --selftest-rival
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_rival.h"
#include "overworld.h"
#include "pokemon.h"
#include "sprite.h"
#include "event_object_movement.h"
#include "constants/battle.h"
#include "constants/event_object_movement.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/map_event_ids.h"
#include "constants/maps.h"
#include "constants/vars.h"

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
    MultiplayerRival_Init();
}

static void Record(u8 flags, u8 completed, u8 cleared, u8 here, u8 state)
{
    u8 record[MP_RIVAL_RECORD_BYTES] = {flags, completed, cleared, here, state, 0, 0, 0};
    MultiplayerRival_TestSetRecord(record);
}

static void SetMap(u32 group, u32 num)
{
    gSaveBlock1Ptr->location.mapGroup = group;
    gSaveBlock1Ptr->location.mapNum = num;
}

// rival_catalogue.gate_blocks (app) for the five checkpoints: only an armed trigger line blocks.
static bool32 ReferenceGateBlocks(u32 checkpoint, bool32 completed, bool32 cleared)
{
    bool32 triggerLine = checkpoint == MP_RIVAL_ROUTE110 || checkpoint == MP_RIVAL_ROUTE119;
    return triggerLine && !completed && !cleared;
}

// Each checkpoint's original prerequisite, made true (due) or the scene's effects already in the save (done).
static void SetSaveState(u32 checkpoint, bool32 due)
{
    switch (checkpoint)
    {
    case MP_RIVAL_ROUTE103:
        VarSet(VAR_BIRCH_LAB_STATE, due ? 3 : 4);
        due ? FlagClear(FLAG_DEFEATED_RIVAL_ROUTE103) : FlagSet(FLAG_DEFEATED_RIVAL_ROUTE103);
        break;
    case MP_RIVAL_RUSTBORO:
        VarSet(VAR_RUSTBORO_CITY_STATE, due ? 7 : 8);
        VarSet(VAR_ROUTE104_STATE, due ? 0 : 2);
        due ? FlagClear(FLAG_MET_RIVAL_RUSTBORO) : FlagSet(FLAG_MET_RIVAL_RUSTBORO);
        FlagClear(FLAG_REGISTER_RIVAL_POKENAV);
        break;
    case MP_RIVAL_ROUTE110:
        VarSet(VAR_ROUTE110_STATE, due ? 0 : 1);
        break;
    case MP_RIVAL_ROUTE119:
        VarSet(VAR_ROUTE119_STATE, due ? 0 : 1);
        break;
    case MP_RIVAL_LILYCOVE:
        due ? FlagClear(FLAG_MET_RIVAL_LILYCOVE) : FlagSet(FLAG_MET_RIVAL_LILYCOVE);
        FlagClear(FLAG_HIDE_LILYCOVE_CITY_RIVAL);
        break;
    }
}

static void TestModes(void)
{
    char what[160];
    u32 cp, completed, cleared, due, facing;
    static const u8 sFacings[] = {DIR_NORTH, DIR_SOUTH, DIR_EAST, DIR_WEST};
    int mismatches = 0, blocks = 0;

    Setup();
    MultiplayerRival_TestSetRecord(NULL);
    for (cp = 0; cp < MP_RIVAL_CHECKPOINT_COUNT; cp++)
        if (MultiplayerRival_TestMode(cp, DIR_NORTH) != MP_RIVAL_MODE_ORIGINAL)
            mismatches++;
    Expect("offline (no rival record): every checkpoint plays its original scene", mismatches == 0);

    // Connected, the host's record not seen yet: the trigger lines stop a northbound player for now, nothing else does.
    Record(0, 0, 0, 0, 0);
    Expect("waiting for the record: Route 110 northbound waits", MultiplayerRival_TestMode(MP_RIVAL_ROUTE110, DIR_NORTH) == MP_RIVAL_MODE_WAIT);
    Expect("waiting for the record: Route 119 southbound passes", MultiplayerRival_TestMode(MP_RIVAL_ROUTE119, DIR_SOUTH) == MP_RIVAL_MODE_PASS);
    Expect("waiting for the record: Route 103 never plays the NPC scene", MultiplayerRival_TestMode(MP_RIVAL_ROUTE103, DIR_NORTH) == MP_RIVAL_MODE_PASS);

    // The full truth table against gate_blocks, with the direction rule and the reward guard.
    mismatches = 0;
    for (cp = 0; cp < MP_RIVAL_CHECKPOINT_COUNT; cp++)
        for (completed = 0; completed < 2; completed++)
            for (cleared = 0; cleared < 2; cleared++)
                for (due = 0; due < 2; due++)
                    for (facing = 0; facing < ARRAY_COUNT(sFacings); facing++)
                    {
                        u8 mode, expected;
                        Setup();
                        SetSaveState(cp, due);
                        Record(MP_RIVAL_FLAG_VALID, completed << cp, cleared << cp, 0, 0);
                        mode = MultiplayerRival_TestMode(cp, sFacings[facing]);
                        if (cleared)
                            expected = due ? MP_RIVAL_MODE_REWARD : MP_RIVAL_MODE_PASS;
                        else if (completed)
                            expected = due ? MP_RIVAL_MODE_CATCH_UP : MP_RIVAL_MODE_PASS;
                        else if (ReferenceGateBlocks(cp, completed, cleared || !due) && sFacings[facing] == DIR_NORTH)
                            expected = MP_RIVAL_MODE_BLOCK;
                        else
                            expected = MP_RIVAL_MODE_PASS;
                        if (mode == MP_RIVAL_MODE_BLOCK)
                            blocks++;
                        if (mode != expected || (mode == MP_RIVAL_MODE_BLOCK) != (ReferenceGateBlocks(cp, completed, cleared || !due) && sFacings[facing] == DIR_NORTH))
                        {
                            mismatches++;
                            printf("     checkpoint %u completed %u cleared %u due %u facing %u: mode %u, expected %u\n",
                                   cp, completed, cleared, due, sFacings[facing], mode, expected);
                        }
                    }
    // gate_blocks' personally_cleared includes a save that passed the scene itself (due == 0).
    snprintf(what, sizeof(what), "mode truth table: 160 cases agree with gate_blocks and the reward guard (%d blocking)", blocks);
    Expect(what, mismatches == 0 && blocks == 2);

    // Another checkpoint's bits never open this one.
    Setup();
    SetSaveState(MP_RIVAL_ROUTE119, TRUE);
    Record(MP_RIVAL_FLAG_VALID, 0x1F & ~(1 << MP_RIVAL_ROUTE119), 0x1F & ~(1 << MP_RIVAL_ROUTE119), 0, 0);
    Expect("Route 119 stays armed while only the other four are completed and cleared",
           MultiplayerRival_TestMode(MP_RIVAL_ROUTE119, DIR_NORTH) == MP_RIVAL_MODE_BLOCK);
}

static void TestRecords(void)
{
    static const u8 sBad[][MP_RIVAL_RECORD_BYTES] = {
        {0, 0, 0, 0, 0, 0, 0, 0},                       // not valid
        {MP_RIVAL_FLAG_VALID | 8, 0, 0, 0, 0, 0, 0, 0}, // unknown flag
        {MP_RIVAL_FLAG_VALID, 0x20, 0, 0, 0, 0, 0, 0},  // a sixth checkpoint
        {MP_RIVAL_FLAG_VALID, 0, 0x20, 0, 0, 0, 0, 0},
        {MP_RIVAL_FLAG_VALID, 0, 0, 6, 1, 0, 0, 0},     // here out of range
        {MP_RIVAL_FLAG_VALID, 0, 0, 3, 5, 0, 0, 0},     // state out of range
        {MP_RIVAL_FLAG_VALID, 0, 0, 3, 0, 0, 0, 0},     // here without a state
        {MP_RIVAL_FLAG_VALID, 0, 0, 0, 1, 0, 0, 0},     // a state without here
        {MP_RIVAL_FLAG_VALID | MP_RIVAL_FLAG_PRESENT, 0, 0, 0, 0, 0, 0, 0},
        {MP_RIVAL_FLAG_VALID, 0, 0, 0, 0, 1, 0, 0},     // reserved bytes
    };
    u32 i, accepted = 0;

    for (i = 0; i < ARRAY_COUNT(sBad); i++)
    {
        Setup();
        VarSet(VAR_ROUTE110_STATE, 0);
        MultiplayerRival_TestSetRecord(sBad[i]);
        // An unaccepted record leaves the game waiting (never BLOCK, never a reward).
        if (MultiplayerRival_TestMode(MP_RIVAL_ROUTE110, DIR_NORTH) != MP_RIVAL_MODE_WAIT)
            accepted++;
    }
    Expect("ten malformed rival records are refused (the game keeps waiting, nothing opens)", accepted == 0);

    Setup();
    VarSet(VAR_ROUTE110_STATE, 0);
    Record(MP_RIVAL_FLAG_VALID | MP_RIVAL_FLAG_PRESENT | MP_RIVAL_FLAG_MY_MATCH, 0, 0, 3, MP_RIVAL_STATE_BATTLING);
    Expect("a valid record with a battle in progress keeps the Route 110 line armed",
           MultiplayerRival_TestMode(MP_RIVAL_ROUTE110, DIR_NORTH) == MP_RIVAL_MODE_BLOCK);
}

static void TestPreparation(void)
{
    u32 cp, good = 0, env;
    u8 out;

    for (cp = 1; cp <= MP_RIVAL_CHECKPOINT_COUNT; cp++)
        good += MultiplayerRival_IsPreparationWord(MP_RIVAL_PREP_WORD(cp, BATTLE_ENVIRONMENT_PLAIN));
    Expect("the five checkpoints' preparation words (canonical PLAIN) are accepted", good == 5);
    good = 0;
    for (env = 0; env < 256; env++)
        if (env != BATTLE_ENVIRONMENT_PLAIN)
            good += MultiplayerRival_IsPreparationWord(MP_RIVAL_PREP_WORD(3, env));
    Expect("any other environment for a checkpoint is refused", good == 0);
    Expect("checkpoint 0 and 6 are refused", !MultiplayerRival_IsPreparationWord(MP_RIVAL_PREP_WORD(0, BATTLE_ENVIRONMENT_PLAIN))
                                            && !MultiplayerRival_IsPreparationWord(MP_RIVAL_PREP_WORD(6, BATTLE_ENVIRONMENT_PLAIN)));
    good = 0;
    for (env = 1; env <= 8; env++)
        good += MultiplayerRival_IsPreparationWord(env);
    Expect("co-op Gym codes 1-8, a ladder code and zero are not rival words", good == 0
           && !MultiplayerRival_IsPreparationWord(0x80000000u | (1u << 20) | (2u << 10) | 50)
           && !MultiplayerRival_IsPreparationWord(0));

    Setup();
    MultiplayerRival_TestSetPreparation(0);
    Expect("no battle environment without a rival preparation", !MultiplayerRival_BattleEnvironment(&out));
    MultiplayerRival_TestSetPreparation(MP_RIVAL_PREP_WORD(4, BATTLE_ENVIRONMENT_PLAIN));
    Expect("a rival preparation gives its environment to the battle", MultiplayerRival_BattleEnvironment(&out) && out == BATTLE_ENVIRONMENT_PLAIN);
    MultiplayerRival_TestSetPreparation(MP_RIVAL_PREP_WORD(4, BATTLE_ENVIRONMENT_GRASS));
    Expect("a preparation with a wrong environment gives none", !MultiplayerRival_BattleEnvironment(&out));
    MultiplayerRival_TestSetPreparation(0);
}

static void TestObjects(void)
{
    Setup();
    MultiplayerRival_TestSetRecord(NULL);
    Expect("offline: the Route 110 rival object stays",
           !MultiplayerRival_HidesObject(LOCALID_ROUTE110_RIVAL, MAP_NUM(MAP_ROUTE110), MAP_GROUP(MAP_ROUTE110)));
    Record(MP_RIVAL_FLAG_VALID, 0, 0, 0, 0);
    Expect("multiplayer: the rival objects of Route 103, Rustboro, Route 110 and Route 119 stay away",
           MultiplayerRival_HidesObject(LOCALID_ROUTE103_RIVAL, MAP_NUM(MAP_ROUTE103), MAP_GROUP(MAP_ROUTE103))
        && MultiplayerRival_HidesObject(LOCALID_RUSTBORO_RIVAL, MAP_NUM(MAP_RUSTBORO_CITY), MAP_GROUP(MAP_RUSTBORO_CITY))
        && MultiplayerRival_HidesObject(LOCALID_ROUTE110_RIVAL, MAP_NUM(MAP_ROUTE110), MAP_GROUP(MAP_ROUTE110))
        && MultiplayerRival_HidesObject(LOCALID_ROUTE110_RIVAL_ON_BIKE, MAP_NUM(MAP_ROUTE110), MAP_GROUP(MAP_ROUTE110))
        && MultiplayerRival_HidesObject(LOCALID_ROUTE119_RIVAL, MAP_NUM(MAP_ROUTE119), MAP_GROUP(MAP_ROUTE119)));
    Expect("multiplayer: other objects stay (Scott on Route 119, the Route 103 rival's ID on Route 110)",
           !MultiplayerRival_HidesObject(LOCALID_ROUTE119_SCOTT, MAP_NUM(MAP_ROUTE119), MAP_GROUP(MAP_ROUTE119))
        && !MultiplayerRival_HidesObject(LOCALID_ROUTE103_RIVAL, MAP_NUM(MAP_ROUTE110), MAP_GROUP(MAP_ROUTE110))
        && !MultiplayerRival_HidesObject(LOCALID_ROUTE110_RIVAL, MAP_NUM(MAP_ROUTE111), MAP_GROUP(MAP_ROUTE111)));
    MultiplayerRival_TestSetRecord(NULL);
}

static void TestIntroPostState(void)
{
    static const u16 sMustNot[] = {FLAG_SYS_POKEDEX_GET, FLAG_RECEIVED_POKEDEX_FROM_BIRCH, FLAG_ADVENTURE_STARTED,
                                   FLAG_RECEIVED_RUNNING_SHOES, FLAG_SYS_B_DASH, FLAG_DEFEATED_RIVAL_ROUTE103};
    u32 gender, i, wrong;

    for (gender = MALE; gender <= FEMALE; gender++)
    {
        char what[160];
        Setup();
        gSaveBlock2Ptr->playerGender = gender;
        MultiplayerIntro_TestApplyPostState();
        wrong = 0;
        for (i = 0; i < ARRAY_COUNT(sMustNot); i++)
            wrong += FlagGet(sMustNot[i]) ? 1 : 0;
        wrong += CheckBagHasItem(ITEM_POKE_BALL, 1) ? 1 : 0; // the Pokedex is a flag (above), not an item
        snprintf(what, sizeof(what), "%s introduction: no Pokedex, Poke Balls, Running Shoes or Route 103 victory", gender == MALE ? "male" : "female");
        Expect(what, wrong == 0);
        snprintf(what, sizeof(what), "%s introduction: VAR_BIRCH_LAB_STATE 3 (sent to Route 103), Littleroot states, starter flag", gender == MALE ? "male" : "female");
        Expect(what, VarGet(VAR_BIRCH_LAB_STATE) == 3 && VarGet(VAR_LITTLEROOT_INTRO_STATE) == 7 && VarGet(VAR_LITTLEROOT_TOWN_STATE) == 2
               && VarGet(VAR_LITTLEROOT_RIVAL_STATE) == 3 && VarGet(VAR_ROUTE101_STATE) == 3 && FlagGet(FLAG_SYS_POKEMON_GET)
               && FlagGet(FLAG_RESCUED_BIRCH) && FlagGet(FLAG_SET_WALL_CLOCK) && !FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_BIRCH));
        snprintf(what, sizeof(what), "%s introduction: the player's own house and the rival's house are set for that gender", gender == MALE ? "male" : "female");
        Expect(what, gender == MALE
            ? FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MAY) && !FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_BRENDAN)
              && VarGet(VAR_LITTLEROOT_HOUSES_STATE_BRENDAN) == 1 && VarGet(VAR_LITTLEROOT_HOUSES_STATE_MAY) == 2
            : FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_BRENDAN) && !FlagGet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MAY)
              && VarGet(VAR_LITTLEROOT_HOUSES_STATE_BRENDAN) == 2 && VarGet(VAR_LITTLEROOT_HOUSES_STATE_MAY) == 1);
    }
}

// A reference for the record checks of multiplayer_rival.c ValidRecord (the app's world_mailbox.RivalRecord rules).
static bool32 ReferenceValid(const u8 *r)
{
    if (!(r[0] & MP_RIVAL_FLAG_VALID) || (r[0] & ~7) || (r[1] & ~0x1F) || (r[2] & ~0x1F) || r[3] > 5 || r[4] > 4 || r[5] || r[6] || r[7])
        return FALSE;
    if ((r[3] == 0) != (r[4] == 0))
        return FALSE;
    return !((r[0] & (MP_RIVAL_FLAG_PRESENT | MP_RIVAL_FLAG_MY_MATCH)) && r[3] == 0);
}

static u32 sFuzzState = 0x31A7E5u;

static u32 FuzzNext(void)
{
    sFuzzState ^= sFuzzState << 13;
    sFuzzState ^= sFuzzState >> 17;
    sFuzzState ^= sFuzzState << 5;
    return sFuzzState;
}

// 200,000 records from a host or a damaged mailbox (half fully random, half near-valid), any checkpoint, facing and save
// state: the mode is always the reference's, a malformed record never opens, blocks or rewards anything, a reward needs
// the player's own pass and a due save, and only a northbound player on a trigger line is ever stopped.
static void TestFuzz(void)
{
    static const u8 sFacings[] = {DIR_NORTH, DIR_SOUTH, DIR_EAST, DIR_WEST};
    u32 i, mismatches = 0, illegal = 0, accepted = 0, rewards = 0, blocks = 0;
    char what[200];

    for (i = 0; i < 200000; i++)
    {
        u8 r[MP_RIVAL_RECORD_BYTES], mode, expected;
        u32 cp = FuzzNext() % MP_RIVAL_CHECKPOINT_COUNT, facing = sFacings[FuzzNext() % 4], due = FuzzNext() & 1, b;
        bool32 valid, triggerLine = cp == MP_RIVAL_ROUTE110 || cp == MP_RIVAL_ROUTE119;

        for (b = 0; b < MP_RIVAL_RECORD_BYTES; b++)
            r[b] = FuzzNext();
        if (i & 1)
        {
            // near-valid: the shapes a real companion writes, with an occasional broken byte
            r[0] = MP_RIVAL_FLAG_VALID | (FuzzNext() & 6);
            r[1] &= 0x1F; r[2] &= 0x1F;
            r[3] = FuzzNext() % 6;
            r[4] = r[3] ? 1 + FuzzNext() % 4 : 0;
            r[5] = r[6] = r[7] = 0;
            if ((FuzzNext() & 15) == 0)
                r[FuzzNext() % MP_RIVAL_RECORD_BYTES] ^= 1u << (FuzzNext() % 8);
        }
        Setup();
        SetSaveState(cp, due);
        MultiplayerRival_TestSetRecord(r);
        mode = MultiplayerRival_TestMode(cp, facing);
        valid = ReferenceValid(r);
        accepted += valid;
        if (!valid)
            expected = triggerLine && facing == DIR_NORTH ? MP_RIVAL_MODE_WAIT : MP_RIVAL_MODE_PASS;
        else if ((r[2] >> cp) & 1)
            expected = due ? MP_RIVAL_MODE_REWARD : MP_RIVAL_MODE_PASS;
        else if ((r[1] >> cp) & 1)
            expected = due ? MP_RIVAL_MODE_CATCH_UP : MP_RIVAL_MODE_PASS;
        else
            expected = triggerLine && facing == DIR_NORTH && due ? MP_RIVAL_MODE_BLOCK : MP_RIVAL_MODE_PASS;
        if (mode != expected)
            mismatches++;
        if ((!valid && mode != MP_RIVAL_MODE_WAIT && mode != MP_RIVAL_MODE_PASS)
         || (mode == MP_RIVAL_MODE_REWARD && !(((r[2] >> cp) & 1) && due))
         || (mode == MP_RIVAL_MODE_BLOCK && !(triggerLine && facing == DIR_NORTH)) || mode > MP_RIVAL_MODE_PASS)
            illegal++;
        rewards += mode == MP_RIVAL_MODE_REWARD;
        blocks += mode == MP_RIVAL_MODE_BLOCK;
    }
    MultiplayerRival_TestSetRecord(NULL);
    snprintf(what, sizeof(what), "fuzz: 200000 records (%u valid): %u mismatches, %u illegal modes (%u rewards, %u blocks)",
             accepted, mismatches, illegal, rewards, blocks);
    Expect(what, mismatches == 0 && illegal == 0 && accepted > 50000 && rewards > 0 && blocks > 0);
}

extern const union AnimCmd *const sAnimTable_Following[];
void SetStepAnimHandleAlternation(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 animNum);

static u32 AnimEnd(const union AnimCmd *anim)
{
    u32 i = 0;
    while (anim[i].type != -2 && anim[i].type != -1)  // ANIMCMD_JUMP, ANIMCMD_END
        i++;
    return i;
}

// A following Pokemon's step from a longer animation into a shorter one (a script pushing the player back into the follower):
// the position carried over must stay inside the new animation (AddressSanitizer read past sAnim_GoNorth2F before the fix).
static void TestFollowerAnimationPosition(void)
{
    struct ObjectEvent object;
    struct Sprite sprite;
    u32 anim, carried, outside = 0, cases = 0;

    for (anim = ANIM_STD_GO_SOUTH; anim <= ANIM_STD_GO_EAST; anim++)
        for (carried = 0; carried < 16; carried++)
        {
            memset(&object, 0, sizeof(object));
            memset(&sprite, 0, sizeof(sprite));
            sprite.anims = sAnimTable_Following;
            sprite.usingSheet = TRUE;
            sprite.animCmdIndex = carried;
            SetStepAnimHandleAlternation(&object, &sprite, anim);
            cases++;
            if (sprite.animCmdIndex > AnimEnd(sAnimTable_Following[anim]))
                outside++;
        }
    Expect("a following Pokemon's carried-over animation position stays inside the new (shorter) animation", outside == 0 && cases == 64);
}

int NativeSelfTest_Rival(void)
{
    printf("selftest-rival: Update 31 rival checkpoints\n");
    TestModes();
    TestRecords();
    TestPreparation();
    TestObjects();
    TestIntroPostState();
    TestFuzz();
    TestFollowerAnimationPosition();
    MultiplayerRival_TestSetRecord(NULL);
    printf("selftest-rival: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
