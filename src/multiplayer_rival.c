// Update 31: multiplayer rival checkpoints and the streamlined new-character
// introduction (include/multiplayer_rival.h). Native PC build only: the GBA
// ROM build compiles this file to nothing. Project-owned.
#ifdef PORTABLE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#endif
#include "global.h"
#ifdef PORTABLE
#include "multiplayer_rival.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "multiplayer_menu.h"
#include "multiplayer_world.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "main.h"
#include "overworld.h"
#include "pokemon.h"
#include "new_game.h"
#include "rtc.h"
#include "save.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "sound.h"
#include "starter_choose.h"
#include "task.h"
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/field_effects.h"
#include "constants/flags.h"
#include "constants/heal_locations.h"
#include "constants/items.h"
#include "constants/map_event_ids.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/vars.h"

const u32 gMultiplayerRivalAbiVersion = MP_RIVAL_ABI_VERSION;

// Scripts (data/scripts/mp_rival.inc) and the original rival objects' scripts.
extern const u8 MpRival_EventScript_RewardRoute103[];
extern const u8 MpRival_EventScript_RewardRustboro[];
extern const u8 MpRival_EventScript_RewardLilycove[];
extern const u8 MpIntro_EventScript_Arrive[];
extern const u8 LilycoveCity_EventScript_Rival[];

// Frames a connected game waits for the host's first rival record before it
// treats the host as one without rival checkpoints (the original scenes then play).
#define RECORD_WAIT_FRAMES   (60 * 10)
#define MENU_REQUEST_FRAMES  (60 * 5)
#define NOTICE_RETRY_FRAMES  (60 * 20)

struct CheckpointPlace
{
    u8 mapGroup, mapNum;
};

// Every map a checkpoint's scene happens on (rival_catalogue.CHECKPOINTS[i].triggers).
static const struct CheckpointPlace sPlaces[MP_RIVAL_CHECKPOINT_COUNT][2] = {
    [MP_RIVAL_ROUTE103] = {{MAP_GROUP(MAP_ROUTE103), MAP_NUM(MAP_ROUTE103)}, {0xFF, 0xFF}},
    [MP_RIVAL_RUSTBORO] = {{MAP_GROUP(MAP_RUSTBORO_CITY), MAP_NUM(MAP_RUSTBORO_CITY)}, {MAP_GROUP(MAP_ROUTE104), MAP_NUM(MAP_ROUTE104)}},
    [MP_RIVAL_ROUTE110] = {{MAP_GROUP(MAP_ROUTE110), MAP_NUM(MAP_ROUTE110)}, {0xFF, 0xFF}},
    [MP_RIVAL_ROUTE119] = {{MAP_GROUP(MAP_ROUTE119), MAP_NUM(MAP_ROUTE119)}, {0xFF, 0xFF}},
    [MP_RIVAL_LILYCOVE] = {{MAP_GROUP(MAP_LILYCOVE_CITY), MAP_NUM(MAP_LILYCOVE_CITY)}, {0xFF, 0xFF}},
};

// The one environment a rival battle uses on every core (rival_catalogue canonical_environment):
// the original NPC battle's background on the approach the story takes.
static const u8 sCanonicalEnvironment[MP_RIVAL_CHECKPOINT_COUNT] = {
    BATTLE_ENVIRONMENT_PLAIN, BATTLE_ENVIRONMENT_PLAIN, BATTLE_ENVIRONMENT_PLAIN,
    BATTLE_ENVIRONMENT_PLAIN, BATTLE_ENVIRONMENT_PLAIN,
};

static const char *const sTitles[MP_RIVAL_CHECKPOINT_COUNT] = {
    "ROUTE 103", "RUSTBORO CITY", "ROUTE 110", "ROUTE 119", "LILYCOVE CITY",
};

static struct
{
    u8 record[MP_RIVAL_RECORD_BYTES]; // the last valid record of this connection
    u8 completed, cleared;            // OR of every valid record of this connection (completion is permanent)
    bool8 connected, active, legacy;
    bool8 testRecord;                 // --selftest-rival / PKMN_DEBUG_RIVAL: a record without a host
    u16 waitFrames;
    // Players menu requested by a stopped player (opened when the field is free).
    bool8 menuRequested;
    u16 menuFrames;
    // One overworld banner at a time; retried while the map name pops up.
    char notice[MP_WORLD_NOTICE_CHARS + 1];
    bool8 noticePending;
    u16 noticeFrames;
    u8 noticeMapGroup, noticeMapNum; // the map whose arrival banner was shown
    u8 cueHere, cueState;            // the checkpoint state the battle cue last reacted to
    bool8 introArrive;               // the streamlined introduction's arrival script is due
    u32 testPreparation;             // PKMN_DEBUG_RIVAL_BATTLE: battles started in the test carry this word
} sRival;

// ---------------------------------------------------------------------------
// State

// Test trace (stderr, the game's log): only with a test record (PKMN_DEBUG_RIVAL, --selftest-rival).
static void TestLogV(const char *format, va_list args)
{
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    fflush(stderr);
}

static void TestLog(const char *format, ...)
{
    va_list args;
    if (!sRival.testRecord)
        return;
    va_start(args, format);
    TestLogV(format, args);
    va_end(args);
}

static void TestLogAlways(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    TestLogV(format, args);
    va_end(args);
}

static void ClearConnection(void)
{
    memset(sRival.record, 0, sizeof(sRival.record));
    sRival.completed = sRival.cleared = 0;
    sRival.active = sRival.legacy = FALSE;
    sRival.waitFrames = 0;
    sRival.menuRequested = FALSE;
    sRival.cueHere = sRival.cueState = 0;
    sRival.noticeMapGroup = sRival.noticeMapNum = 0xFF;
}

void MultiplayerRival_Init(void)
{
    bool8 introArrive = sRival.introArrive, testRecord = sRival.testRecord;
    u32 testPreparation = sRival.testPreparation;
    u8 record[MP_RIVAL_RECORD_BYTES];
    memcpy(record, sRival.record, sizeof(record));
    memset(&sRival, 0, sizeof(sRival));
    ClearConnection();
    sRival.introArrive = introArrive; // the arrival of a New Game started just before
    sRival.testPreparation = testPreparation;
    if (testRecord)
        MultiplayerRival_TestSetRecord(record);
}

static bool32 ValidRecord(const u8 *record)
{
    if (!(record[0] & MP_RIVAL_FLAG_VALID) || (record[0] & ~7) || (record[1] & ~0x1F) || (record[2] & ~0x1F)
     || record[3] > MP_RIVAL_CHECKPOINT_COUNT || record[4] > MP_RIVAL_STATE_COMPLETED
     || record[5] || record[6] || record[7])
        return FALSE;
    if ((record[3] == 0) != (record[4] == 0))
        return FALSE;
    if ((record[0] & (MP_RIVAL_FLAG_PRESENT | MP_RIVAL_FLAG_MY_MATCH)) && record[3] == 0)
        return FALSE;
    return TRUE;
}

static void AcceptRecord(const u8 *record)
{
    memcpy(sRival.record, record, MP_RIVAL_RECORD_BYTES);
    sRival.completed |= record[1];
    sRival.cleared |= record[2];
    sRival.active = TRUE;
    sRival.legacy = FALSE;
}

static bool32 InMultiplayer(void)
{
    return sRival.connected || sRival.testRecord;
}

static bool32 Completed(u32 checkpoint)
{
    return (sRival.completed >> checkpoint) & 1;
}

static bool32 Cleared(u32 checkpoint)
{
    return (sRival.cleared >> checkpoint) & 1;
}

static s32 CheckpointHere(void)
{
    u32 i, j;
    for (i = 0; i < MP_RIVAL_CHECKPOINT_COUNT; i++)
        for (j = 0; j < 2; j++)
            if (sPlaces[i][j].mapGroup == gSaveBlock1Ptr->location.mapGroup && sPlaces[i][j].mapNum == gSaveBlock1Ptr->location.mapNum)
                return i;
    return -1;
}

// The original scene's prerequisite holds and its personal effects are not in the save yet.
static bool32 RewardDue(u32 checkpoint)
{
    switch (checkpoint)
    {
    case MP_RIVAL_ROUTE103:
        return VarGet(VAR_BIRCH_LAB_STATE) == 3 && !FlagGet(FLAG_DEFEATED_RIVAL_ROUTE103);
    case MP_RIVAL_RUSTBORO:
        return !FlagGet(FLAG_MET_RIVAL_RUSTBORO) && !FlagGet(FLAG_REGISTER_RIVAL_POKENAV)
            && (VarGet(VAR_RUSTBORO_CITY_STATE) == 7 || VarGet(VAR_ROUTE104_STATE) == 1);
    case MP_RIVAL_ROUTE110:
        return VarGet(VAR_ROUTE110_STATE) == 0;
    case MP_RIVAL_ROUTE119:
        return VarGet(VAR_ROUTE119_STATE) == 0;
    case MP_RIVAL_LILYCOVE:
        return !FlagGet(FLAG_MET_RIVAL_LILYCOVE) && !FlagGet(FLAG_HIDE_LILYCOVE_CITY_RIVAL);
    }
    return FALSE;
}

static bool32 IsTriggerLine(u32 checkpoint)
{
    return checkpoint == MP_RIVAL_ROUTE110 || checkpoint == MP_RIVAL_ROUTE119;
}

// gate_blocks() of rival_catalogue.py, plus the direction rule: the trigger lines
// of Route 110 and Route 119 cross the route northbound, so only a player
// stepping north onto them is stopped; going back is never blocked.
static u8 Mode(u32 checkpoint, u8 facing)
{
    if (checkpoint >= MP_RIVAL_CHECKPOINT_COUNT || !InMultiplayer() || sRival.legacy)
        return MP_RIVAL_MODE_ORIGINAL;
    if (!sRival.active)
        return IsTriggerLine(checkpoint) && facing == DIR_NORTH ? MP_RIVAL_MODE_WAIT : MP_RIVAL_MODE_PASS;
    if (Cleared(checkpoint))
        return RewardDue(checkpoint) ? MP_RIVAL_MODE_REWARD : MP_RIVAL_MODE_PASS;
    if (Completed(checkpoint))
        return RewardDue(checkpoint) ? MP_RIVAL_MODE_CATCH_UP : MP_RIVAL_MODE_PASS;
    // A save that passed the scene itself (before Update 31, or offline) is personally cleared.
    if (IsTriggerLine(checkpoint) && facing == DIR_NORTH && RewardDue(checkpoint))
        return MP_RIVAL_MODE_BLOCK;
    return MP_RIVAL_MODE_PASS;
}

// ---------------------------------------------------------------------------
// Banners and the battle cue

static void QueueNotice(const char *first, const char *second)
{
    u32 n = 0;
    while (*first && n < MP_WORLD_NOTICE_CHARS)
        sRival.notice[n++] = *first++;
    if (n < MP_WORLD_NOTICE_CHARS)
        sRival.notice[n++] = '|';
    while (*second && n < MP_WORLD_NOTICE_CHARS)
        sRival.notice[n++] = *second++;
    sRival.notice[n] = 0;
    sRival.noticePending = TRUE;
    sRival.noticeFrames = 0;
}

static void QueueCheckpointNotice(u32 checkpoint, const char *second)
{
    char first[MP_WORLD_NOTICE_CHARS + 1] = "RIVAL CHECKPOINT: ";
    u32 n = strlen(first);
    const char *title = sTitles[checkpoint];
    while (*title && n < sizeof(first) - 1)
        first[n++] = *title++;
    first[n] = 0;
    QueueNotice(first, second);
}

static void FlushNotice(void)
{
    if (!sRival.noticePending)
        return;
    if (MultiplayerWorld_ShowLocalNotice(sRival.notice) || ++sRival.noticeFrames >= NOTICE_RETRY_FRAMES)
        sRival.noticePending = FALSE;
}

static void Exclaim(u8 localId)
{
    gFieldEffectArguments[0] = localId;
    gFieldEffectArguments[1] = gSaveBlock1Ptr->location.mapNum;
    gFieldEffectArguments[2] = gSaveBlock1Ptr->location.mapGroup;
    FieldEffectStart(FLDEFF_EXCLAMATION_MARK_ICON);
}

// The rival battle's opening: the players at the checkpoint (their real avatars,
// drawn by the presence layer with their own looks) all get the trainer-spotted
// mark, as the original rival did before the battle. Nothing is locked or moved.
static void BattleCue(void)
{
    u32 i;
    PlaySE(SE_PIN);
    Exclaim(LOCALID_PLAYER);
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
        if (GetObjectEventIdByLocalId(MULTIPLAYER_LOCAL_ID_BASE + i) < OBJECT_EVENTS_COUNT
         && Multiplayer_IsPeerOnCurrentMap(Multiplayer_GetRemotePlayerId(i)))
            Exclaim(MULTIPLAYER_LOCAL_ID_BASE + i);
}

static void UpdateCue(bool32 fieldReady)
{
    u8 here = sRival.record[3], state = sRival.record[4];
    s32 checkpoint = CheckpointHere();

    if (here == 0 || checkpoint < 0 || here != checkpoint + 1 || !(sRival.record[0] & MP_RIVAL_FLAG_PRESENT))
    {
        sRival.cueHere = sRival.cueState = 0;
        return;
    }
    if (here == sRival.cueHere && state == sRival.cueState)
        return;
    if (!fieldReady || ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled())
        return; // react when the field is free again
    if (state == MP_RIVAL_STATE_COUNTDOWN && sRival.cueState != MP_RIVAL_STATE_COUNTDOWN)
    {
        BattleCue();
        QueueNotice("RIVAL BATTLE!", "Your rivals are here. The battle starts soon.");
    }
    else if (state == MP_RIVAL_STATE_BATTLING && (sRival.record[0] & MP_RIVAL_FLAG_MY_MATCH))
    {
        QueueNotice("YOUR RIVAL BATTLE IS READY", "Players > Rival checkpoint > Battle");
    }
    else if (state == MP_RIVAL_STATE_COMPLETED && sRival.cueState && sRival.cueState != MP_RIVAL_STATE_COMPLETED)
    {
        QueueCheckpointNotice(checkpoint, Cleared(checkpoint) ? "Complete! The story goes on." : "Completed by your rivals.");
    }
    sRival.cueHere = here;
    sRival.cueState = state;
}

// A banner once per arrival on a checkpoint's map while it still matters to this save.
static void UpdateArrival(bool32 fieldReady)
{
    s32 checkpoint = CheckpointHere();
    u8 group = gSaveBlock1Ptr->location.mapGroup, num = gSaveBlock1Ptr->location.mapNum;

    if (!fieldReady || (group == sRival.noticeMapGroup && num == sRival.noticeMapNum))
        return;
    sRival.noticeMapGroup = group;
    sRival.noticeMapNum = num;
    if (checkpoint < 0 || !RewardDue(checkpoint) || Cleared(checkpoint))
        return;
    if (Completed(checkpoint))
        QueueCheckpointNotice(checkpoint, "Players > Rival checkpoint: Continue the story");
    else if (checkpoint == MP_RIVAL_ROUTE103 || IsTriggerLine(checkpoint))
        QueueCheckpointNotice(checkpoint, "Battle your rivals: Players > Rival checkpoint");
    else
        QueueCheckpointNotice(checkpoint, "Optional battle: Players > Rival checkpoint");
}

// An original rival already on screen when the game learns it is in a rival world
// leaves without its save flag changing (offline it is there again).
static void RemoveHiddenObjects(void)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *object = &gObjectEvents[i];
        if (object->active && !object->isPlayer && object->mapNum == gSaveBlock1Ptr->location.mapNum
         && object->mapGroup == gSaveBlock1Ptr->location.mapGroup
         && MultiplayerRival_HidesObject(object->localId, object->mapNum, object->mapGroup))
            RemoveObjectEvent(object);
    }
}

// ---------------------------------------------------------------------------
// Hooks

void MultiplayerRival_Tick(bool32 fieldReady, bool32 connected, const u8 *record)
{
    if (sRival.testRecord)
        connected = TRUE;
    if (!connected)
    {
        if (sRival.connected)
            ClearConnection();
        sRival.connected = FALSE;
        return;
    }
    sRival.connected = TRUE;
    if (!sRival.testRecord && record != NULL && ValidRecord(record))
        AcceptRecord(record);
    else if (!sRival.active && !sRival.legacy && fieldReady && ++sRival.waitFrames >= RECORD_WAIT_FRAMES)
        sRival.legacy = TRUE; // a host without rival checkpoints: the original scenes play
    if (gMain.callback2 != CB2_Overworld)
        return;
    if (sRival.active)
    {
        if (fieldReady && !ScriptContext_IsEnabled())
            RemoveHiddenObjects();
        UpdateCue(fieldReady);
        UpdateArrival(fieldReady);
    }
    if (sRival.menuRequested && ++sRival.menuFrames >= MENU_REQUEST_FRAMES)
        sRival.menuRequested = FALSE;
    if (fieldReady)
        FlushNotice();
}

bool8 MultiplayerRival_TryStartFieldScript(void)
{
    s32 checkpoint;

    if (sRival.introArrive)
    {
        sRival.introArrive = FALSE;
        ScriptContext_SetupScript(MpIntro_EventScript_Arrive);
        return TRUE;
    }
    if (sRival.menuRequested)
    {
        if (MultiplayerMenu_Open())
        {
            sRival.menuRequested = FALSE;
            return TRUE;
        }
        if (sRival.testRecord)
        {
            TestLog("rival: Players menu requested (no host in this test)");
            sRival.menuRequested = FALSE;
        }
    }
    if (!sRival.active)
        return FALSE;
    // A pass recorded while the player stands at a talk or optional checkpoint
    // brings the original scene's effects at once; the trigger lines of Route
    // 110 and 119 give theirs on the line itself (the player walks on).
    checkpoint = CheckpointHere();
    if (checkpoint < 0 || IsTriggerLine(checkpoint) || Mode(checkpoint, DIR_NONE) != MP_RIVAL_MODE_REWARD)
        return FALSE;
    switch (checkpoint)
    {
    case MP_RIVAL_ROUTE103:
        ScriptContext_SetupScript(MpRival_EventScript_RewardRoute103);
        return TRUE;
    case MP_RIVAL_RUSTBORO:
        ScriptContext_SetupScript(MpRival_EventScript_RewardRustboro);
        return TRUE;
    case MP_RIVAL_LILYCOVE:
        ScriptContext_SetupScript(MpRival_EventScript_RewardLilycove);
        return TRUE;
    }
    return FALSE;
}

bool32 MultiplayerRival_HidesObject(u8 localId, u8 mapNum, u8 mapGroup)
{
    if (!InMultiplayer() || sRival.legacy)
        return FALSE;
#define AT(map) (mapGroup == MAP_GROUP(map) && mapNum == MAP_NUM(map))
    if (AT(MAP_ROUTE103))
        return localId == LOCALID_ROUTE103_RIVAL;
    if (AT(MAP_RUSTBORO_CITY))
        return localId == LOCALID_RUSTBORO_RIVAL;
    if (AT(MAP_ROUTE104))
        return localId == LOCALID_ROUTE104_RIVAL && (VarGet(VAR_ROUTE104_STATE) <= 1 || !FlagGet(FLAG_MET_RIVAL_RUSTBORO));
    if (AT(MAP_ROUTE110))
        return localId == LOCALID_ROUTE110_RIVAL || localId == LOCALID_ROUTE110_RIVAL_ON_BIKE;
    if (AT(MAP_ROUTE119))
        return localId == LOCALID_ROUTE119_RIVAL || localId == LOCALID_ROUTE119_RIVAL_ON_BIKE;
    if (AT(MAP_LILYCOVE_CITY))
    {
        // The Lilycove rival has no local ID constant; it is the object that runs her script.
        u32 i;
        for (i = 0; i < OBJECT_EVENT_TEMPLATES_COUNT && i < gMapHeader.events->objectEventCount; i++)
            if (gSaveBlock1Ptr->objectEventTemplates[i].localId == localId)
                return gSaveBlock1Ptr->objectEventTemplates[i].script == LilycoveCity_EventScript_Rival;
    }
#undef AT
    return FALSE;
}

// ---------------------------------------------------------------------------
// Script specials

void MultiplayerRival_ScriptMode(void)
{
    gSpecialVar_0x8005 = GetPlayerFacingDirection(); // MpRival_EventScript_StepOn walks on this way
    gSpecialVar_Result = Mode(gSpecialVar_0x8004, gSpecialVar_0x8005);
    TestLog("rival: checkpoint %d mode %d (facing %d)", gSpecialVar_0x8004, gSpecialVar_Result, GetPlayerFacingDirection());
}

void MultiplayerRival_RequestMenu(void)
{
    sRival.menuRequested = TRUE;
    sRival.menuFrames = 0;
}

void MultiplayerRival_CatchUpNotice(void)
{
    if (gSpecialVar_0x8004 < MP_RIVAL_CHECKPOINT_COUNT)
        QueueCheckpointNotice(gSpecialVar_0x8004, "Done by your rivals: Players > Continue the story");
}

// ---------------------------------------------------------------------------
// The battle

bool32 MultiplayerRival_IsPreparationWord(u32 flags)
{
    u32 checkpoint = (flags >> 8) & 0xFF;
    return (flags & 0xFFFF0000u) == MP_RIVAL_PREP_TAG && checkpoint >= 1 && checkpoint <= MP_RIVAL_CHECKPOINT_COUNT
        && (flags & 0xFF) == sCanonicalEnvironment[checkpoint - 1];
}

// The environment of the battle being set up: the one its native preparation names, on every core.
bool32 MultiplayerRival_BattleEnvironment(u8 *environment)
{
    u32 flags = sRival.testPreparation ? sRival.testPreparation
              : MultiplayerBattle_IsNative() ? MultiplayerBattle_GetRivalPreparation() : 0;
    if (!MultiplayerRival_IsPreparationWord(flags))
        return FALSE;
    *environment = flags & 0xFF;
    if (sRival.testPreparation)
    {
        static u8 sLogged = 0xFF;
        if (sLogged != *environment)
            TestLogAlways("rival: battle environment %d from the preparation", *environment);
        sLogged = *environment;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// The streamlined new-character introduction (docs/update31/intro-post-state.json of the app)

bool32 MultiplayerIntro_IsEnabled(void)
{
    // PKMN_ORIGINAL_INTRO=1: the original truck, house and Route 101 rescue (development and comparison only).
    return !IS_FRLG && getenv("PKMN_ORIGINAL_INTRO") == NULL;
}

static void ApplyIntroPostState(void)
{
    bool32 male = gSaveBlock2Ptr->playerGender == MALE;

    FlagSet(FLAG_SYS_POKEMON_GET);
    FlagSet(FLAG_RESCUED_BIRCH);
    FlagSet(FLAG_HIDE_ROUTE_101_BIRCH_ZIGZAGOON_BATTLE);
    FlagSet(FLAG_HIDE_ROUTE_101_BIRCH_STARTERS_BAG);
    FlagSet(FLAG_SET_WALL_CLOCK);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_PLAYERS_HOUSE_VIGOROTH_1);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_PLAYERS_HOUSE_VIGOROTH_2);
    FlagSet(FLAG_SYS_TV_HOME);
    FlagSet(FLAG_MET_RIVAL_MOM);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MOM_OUTSIDE);
    if (male)
    {
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_SIBLING);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F_POKE_BALL);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MAY);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_2F_POKE_BALL);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM);
    }
    else
    {
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_SIBLING);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_2F_POKE_BALL);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_BRENDAN);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F_POKE_BALL);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_BEDROOM);
    }
    FlagClear(FLAG_HIDE_MAP_NAME_POPUP);
    FlagClear(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_BIRCH);
    FlagClear(FLAG_HIDE_LITTLEROOT_TOWN_FAT_MAN);
    FlagClear(FLAG_HIDE_ROUTE_101_BOY);
    VarSet(VAR_LITTLEROOT_INTRO_STATE, 7);
    VarSet(VAR_LITTLEROOT_RIVAL_STATE, 3);
    VarSet(VAR_LITTLEROOT_TOWN_STATE, 2);
    VarSet(VAR_ROUTE101_STATE, 3);
    VarSet(VAR_BIRCH_LAB_STATE, 3);
    VarSet(VAR_LITTLEROOT_HOUSES_STATE_BRENDAN, male ? 1 : 2);
    VarSet(VAR_LITTLEROOT_HOUSES_STATE_MAY, male ? 2 : 1);
    // The wall clock as the player would accept it upstairs: the time shown is the time kept.
    RtcCalcLocalTime();
    RtcInitLocalTimeOffset(gLocalTime.hours, gLocalTime.minutes);
    SetLastHealLocationWarp(male ? HEAL_LOCATION_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F : HEAL_LOCATION_LITTLEROOT_TOWN_MAYS_HOUSE_2F);
}

static void CB2_IntroGiveStarter(void)
{
    u8 metLocation = MAPSEC_ROUTE_101;
    bool32 male;

    // The existing 18-starter screen chose; the starter is given as Birch's bag gives it (level 5, no item).
    SavePlayerStarterChoice(gSpecialVar_Result);
    ScriptGiveMon(GetStarterPokemon(gSpecialVar_Result), 5, ITEM_NONE);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLocation);
    ApplyIntroPostState();
    male = gSaveBlock2Ptr->playerGender == MALE;
    ResetTasks();
    ResetInitialPlayerAvatarState();
    ScriptContext_Init();
    UnlockPlayerFieldControls();
    // In front of the player's own house door (Littleroot (5,8) / (14,8)), facing down.
    SetWarpDestination(MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN), WARP_ID_NONE, male ? 5 : 14, 9);
    WarpIntoMap();
    gFieldCallback = FieldCB_WarpExitFadeFromBlack;
    gFieldCallback2 = NULL;
    sRival.introArrive = TRUE;
    SetMainCallback2(CB2_LoadMap);
}

void MultiplayerIntro_Begin(void)
{
    // After the existing gender and name choice (Birch's speech) and NewGameInitData: the existing 18-starter screen.
    gMain.savedCallback = CB2_IntroGiveStarter;
    SetMainCallback2(CB2_ChooseStarter);
}

void MultiplayerIntro_Save(void)
{
    u8 status;

    IncrementGameStat(GAME_STAT_SAVED_GAME);
    status = TrySavingData(gDifferentSaveFile ? SAVE_OVERWRITE_DIFFERENT_FILE : SAVE_NORMAL);
    gDifferentSaveFile = FALSE;
    gSpecialVar_Result = status == SAVE_STATUS_OK;
    TestLogAlways("intro: saved (status %d)", status);
}

// ---------------------------------------------------------------------------
// Tests

void MultiplayerRival_TestSetRecord(const u8 *record)
{
    if (record == NULL)
    {
        sRival.testRecord = FALSE;
        sRival.connected = FALSE;
        ClearConnection();
        return;
    }
    sRival.testRecord = TRUE;
    sRival.connected = TRUE;
    ClearConnection();
    if (ValidRecord(record))
        AcceptRecord(record);
}

u8 MultiplayerRival_TestMode(u32 checkpoint, u8 facing)
{
    return Mode(checkpoint, facing);
}

void MultiplayerRival_TestSetPreparation(u32 flags)
{
    sRival.testPreparation = flags;
}

void MultiplayerIntro_TestApplyPostState(void)
{
    ApplyIntroPostState();
}
#endif // PORTABLE
