#include "global.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_reset_admission.h"
#include "multiplayer.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "pokemon.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/vars.h"

struct StaticSource {
    u16 id, species, hide, defeated, prerequisite;
    u8 mapGroup, mapNum, localId, x, y, level;
    bool8 legendary;
};
#define SOURCE(id, map, local, x, y, species, level, hide, defeated, prerequisite, legendary) \
    {id, species, hide, defeated, prerequisite, MAP_GROUP(map), MAP_NUM(map), local, x, y, level, legendary}
static const struct StaticSource sSources[] = {
    SOURCE(MP_STATIC_RESET_VOLTORB_1, MAP_NEW_MAUVILLE_INSIDE, 6, 25, 18, SPECIES_VOLTORB, 25, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1, FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE, 0, FALSE),
    SOURCE(MP_STATIC_RESET_VOLTORB_2, MAP_NEW_MAUVILLE_INSIDE, 7, 6, 11, SPECIES_VOLTORB, 25, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_2, FLAG_DEFEATED_VOLTORB_2_NEW_MAUVILLE, 0, FALSE),
    SOURCE(MP_STATIC_RESET_VOLTORB_3, MAP_NEW_MAUVILLE_INSIDE, 8, 13, 10, SPECIES_VOLTORB, 25, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_3, FLAG_DEFEATED_VOLTORB_3_NEW_MAUVILLE, 0, FALSE),
    SOURCE(MP_STATIC_RESET_ELECTRODE_1, MAP_AQUA_HIDEOUT_B1F, 6, 16, 9, SPECIES_ELECTRODE, 30, FLAG_HIDE_AQUA_HIDEOUT_B1F_ELECTRODE_1, FLAG_DEFEATED_ELECTRODE_1_AQUA_HIDEOUT, 0, FALSE),
    SOURCE(MP_STATIC_RESET_ELECTRODE_2, MAP_AQUA_HIDEOUT_B1F, 8, 16, 10, SPECIES_ELECTRODE, 30, FLAG_HIDE_AQUA_HIDEOUT_B1F_ELECTRODE_2, FLAG_DEFEATED_ELECTRODE_2_AQUA_HIDEOUT, 0, FALSE),
    SOURCE(MP_STATIC_RESET_REGISTEEL, MAP_ANCIENT_TOMB, 1, 8, 7, SPECIES_REGISTEEL, 40, FLAG_HIDE_REGISTEEL, FLAG_DEFEATED_REGISTEEL, FLAG_SYS_REGISTEEL_PUZZLE_COMPLETED, TRUE),
    SOURCE(MP_STATIC_RESET_REGIROCK, MAP_DESERT_RUINS, 1, 8, 7, SPECIES_REGIROCK, 40, FLAG_HIDE_REGIROCK, FLAG_DEFEATED_REGIROCK, FLAG_SYS_REGIROCK_PUZZLE_COMPLETED, TRUE),
    SOURCE(MP_STATIC_RESET_REGICE, MAP_ISLAND_CAVE, 1, 8, 7, SPECIES_REGICE, 40, FLAG_HIDE_REGICE, FLAG_DEFEATED_REGICE, FLAG_SYS_BRAILLE_REGICE_COMPLETED, TRUE),
    SOURCE(MP_STATIC_RESET_RAYQUAZA, MAP_SKY_PILLAR_TOP, 2, 14, 6, SPECIES_RAYQUAZA, 70, FLAG_HIDE_SKY_PILLAR_TOP_RAYQUAZA_STILL, FLAG_DEFEATED_RAYQUAZA, 0, TRUE),
};
_Static_assert(ARRAY_COUNT(sSources) == MP_STATIC_RESET_LAST - MP_STATIC_RESET_VOLTORB_1 + 1, "Static catalogue IDs");
static EWRAM_DATA u32 sObserved[ARRAY_COUNT(sSources)];
static EWRAM_DATA u32 sAttempt, sActiveId, sAdmittedCount, sAdmittedId;
static EWRAM_DATA u16 sMapState, sBlueState, sGreenState;

static const struct StaticSource *Source(u32 id)
{
    if (id < MP_STATIC_RESET_VOLTORB_1 || id > MP_STATIC_RESET_LAST) return NULL;
    return &sSources[id - MP_STATIC_RESET_VOLTORB_1];
}

void MultiplayerStaticReset_InitCache(void)
{
    memset(sObserved, 0, sizeof(sObserved));
    sAttempt = sActiveId = sAdmittedCount = sAdmittedId = 0;
}

u32 MultiplayerStaticReset_Count(void)
{
    // Preserve the first pilot's existing ordinary-save mirror exactly.
    return VarGet(VAR_MP_VOLTORB_1_RESET_COUNT_LO)
        | (u32)VarGet(VAR_MP_VOLTORB_1_RESET_COUNT_HI) << 16;
}

u32 MultiplayerStaticReset_GetCount(u32 id)
{
    if (id == MP_STATIC_RESET_VOLTORB_1) return MultiplayerStaticReset_Count();
    return Source(id) ? sObserved[id - MP_STATIC_RESET_VOLTORB_1] : 0;
}

bool32 MultiplayerStaticReset_IsLegendary(u32 id)
{
    const struct StaticSource *source = Source(id);
    return source && source->legendary;
}

static bool32 IsNewMauville(u32 id) { return id >= MP_STATIC_RESET_VOLTORB_1 && id <= MP_STATIC_RESET_VOLTORB_3; }
static u16 MapState(u32 id)
{
    if (IsNewMauville(id)) return VarGet(VAR_NEW_MAUVILLE_STATE);
    if (id == MP_STATIC_RESET_RAYQUAZA) return VarGet(VAR_SKY_PILLAR_STATE);
    return 0;
}

bool32 MultiplayerStaticReset_EntryReady(u32 id)
{
#if MP_LOCAL_RESET_STATIC || MP_LOCAL_RESET_NEW_MAUVILLE_PILOT || TESTING
    const struct StaticSource *source = Source(id);
    u8 objectId;
    if (!(MP_LOCAL_RESET_STATIC || TESTING) && id != MP_STATIC_RESET_VOLTORB_1) return FALSE;
    if (!source || !gSaveBlock1Ptr
     || gSaveBlock1Ptr->location.mapGroup != source->mapGroup
     || gSaveBlock1Ptr->location.mapNum != source->mapNum
     || gSpecialVar_LastTalked != source->localId
     || FlagGet(source->hide) || FlagGet(source->defeated)
     || (source->prerequisite && !FlagGet(source->prerequisite))) return FALSE;
    if (IsNewMauville(id) && MapState(id) != 1 && MapState(id) != 2) return FALSE;
    if (id == MP_STATIC_RESET_RAYQUAZA && MapState(id) < 2) return FALSE;
    if (TryGetObjectEventIdByLocalIdAndMap(source->localId, source->mapNum, source->mapGroup, &objectId)) return FALSE;
    return !gObjectEvents[objectId].isPlayer
        && !Multiplayer_IsRemoteObject(&gObjectEvents[objectId])
        && gObjectEvents[objectId].currentCoords.x == source->x + MAP_OFFSET
        && gObjectEvents[objectId].currentCoords.y == source->y + MAP_OFFSET;
#else
    return FALSE;
#endif
}

bool32 MultiplayerStaticReset_AdvanceAttempt(u32 id)
{
    u32 count;
    if (!MultiplayerStaticReset_EntryReady(id)) return FALSE;
    count = MultiplayerStaticReset_GetCount(id);
    if (count == 0xFFFFFFFFu || (sAdmittedCount && sAdmittedId != id)) return FALSE;
    if (id == MP_STATIC_RESET_VOLTORB_1)
    {
        u16 version = VarGet(VAR_MP_VOLTORB_1_RESET_VERSION);
        // Unknown legacy data is never cleared or reinterpreted.
        if ((version && version != MP_STATIC_RESET_VERSION_1) || (!version && count)) return FALSE;
    }
#if !TESTING
    if (!sAdmittedCount) return FALSE;
#endif
    sAttempt = sAdmittedCount ? sAdmittedCount : count + 1;
    sActiveId = id;
    sObserved[id - MP_STATIC_RESET_VOLTORB_1] = sAttempt;
    if (id == MP_STATIC_RESET_VOLTORB_1)
    {
        VarSet(VAR_MP_VOLTORB_1_RESET_VERSION, MP_STATIC_RESET_VERSION_1);
        VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, sAttempt);
        VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_HI, sAttempt >> 16);
    }
    sMapState = MapState(id);
    sBlueState = VarGet(VAR_TEMP_1);
    sGreenState = VarGet(VAR_TEMP_2);
    return TRUE;
}

bool32 MultiplayerStaticReset_GuardMatches(u32 id)
{
    const struct StaticSource *source = Source(id);
    struct Pokemon enemy;
    if (!MultiplayerStaticReset_EntryReady(id) || sActiveId != id
     || MultiplayerStaticReset_GetCount(id) != sAttempt || MapState(id) != sMapState
     || (id == MP_STATIC_RESET_VOLTORB_1 && VarGet(VAR_MP_VOLTORB_1_RESET_VERSION) != MP_STATIC_RESET_VERSION_1)
     || (IsNewMauville(id) && (VarGet(VAR_TEMP_1) != sBlueState || VarGet(VAR_TEMP_2) != sGreenState))) return FALSE;
    // Clone reads: native checksum failure handling may mark a bad egg.
    enemy = gParties[B_TRAINER_OPPONENT_A][0];
    if (GetMonData(&enemy, MON_DATA_SPECIES) != source->species
     || !GetMonData(&enemy, MON_DATA_SANITY_HAS_SPECIES)
     || GetMonData(&enemy, MON_DATA_SANITY_IS_BAD_EGG)
     || GetMonData(&enemy, MON_DATA_IS_EGG) || enemy.box.isEgg
     || GetMonData(&enemy, MON_DATA_LEVEL) != source->level) return FALSE;
    for (u32 slot = 1; slot < PARTY_SIZE; slot++)
    {
        enemy = gParties[B_TRAINER_OPPONENT_A][slot];
        if (GetMonData(&enemy, MON_DATA_SPECIES)) return FALSE;
    }
    return TRUE;
}

static void Begin(u32 id)
{
#if TESTING
    gSpecialVar_Result = MultiplayerLocalReset_TryBegin(id);
#else
    MultiplayerResetAdmission_Begin(id);
#endif
}

void MultiplayerStaticReset_BeginVoltorb1(void) { Begin(MP_STATIC_RESET_VOLTORB_1); }

void MultiplayerStaticReset_BeginCurrent(void)
{
    gSpecialVar_Result = FALSE;
    for (u32 i = 0; i < ARRAY_COUNT(sSources); i++)
        if (MultiplayerStaticReset_EntryReady(sSources[i].id))
        {
            Begin(sSources[i].id);
            return;
        }
}

bool32 MultiplayerStaticReset_SetAdmission(u32 id, u32 count)
{
    if (!id && !count) { sAdmittedCount = sAdmittedId = 0; return TRUE; }
    if (!Source(id) || count <= MultiplayerStaticReset_GetCount(id)
     || !MultiplayerStaticReset_EntryReady(id)) return FALSE;
    sAdmittedCount = count; sAdmittedId = id;
    return TRUE;
}
