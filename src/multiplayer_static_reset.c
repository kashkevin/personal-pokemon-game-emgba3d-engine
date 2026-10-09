#include "global.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_reset_admission.h"
#include "multiplayer.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "field_weather.h"
#include "metatile_behavior.h"
#include "pokemon.h"
#include "item.h"
#include "item_menu.h"
#include "main.h"
#include "sprite.h"
#include "constants/items.h"
#include "constants/event_objects.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/vars.h"
#include "constants/weather.h"

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
    SOURCE(MP_STATIC_RESET_SUDOWOODO, MAP_BATTLE_FRONTIER_OUTSIDE_EAST, 14, 54, 62, SPECIES_SUDOWOODO, 40, FLAG_HIDE_BATTLE_FRONTIER_SUDOWOODO, FLAG_DEFEATED_SUDOWOODO, 0, FALSE),
    // The weather legends have already approached three tiles before admission.
    SOURCE(MP_STATIC_RESET_KYOGRE, MAP_MARINE_CAVE_END, 1, 9, 25, SPECIES_KYOGRE, 70, FLAG_HIDE_MARINE_CAVE_KYOGRE, FLAG_DEFEATED_KYOGRE, FLAG_SYS_GAME_CLEAR, TRUE),
    SOURCE(MP_STATIC_RESET_GROUDON, MAP_TERRA_CAVE_END, 1, 17, 25, SPECIES_GROUDON, 70, FLAG_HIDE_TERRA_CAVE_GROUDON, FLAG_DEFEATED_GROUDON, FLAG_SYS_GAME_CLEAR, TRUE),
    // Kecleon uses its individual hide flag for completion; it has no separate
    // defeated flag. These are only the seven Devon Scope repeat interactions.
    SOURCE(MP_STATIC_RESET_ROUTE120_KECLEON_1, MAP_ROUTE120, 37, 20, 11, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_120_KECLEON_1, FLAG_HIDE_ROUTE_120_KECLEON_1, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE120_KECLEON_2, MAP_ROUTE120, 38, 27, 2, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_120_KECLEON_2, FLAG_HIDE_ROUTE_120_KECLEON_2, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE120_KECLEON_3, MAP_ROUTE120, 39, 4, 77, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_120_KECLEON_3, FLAG_HIDE_ROUTE_120_KECLEON_3, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE120_KECLEON_4, MAP_ROUTE120, 41, 19, 48, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_120_KECLEON_4, FLAG_HIDE_ROUTE_120_KECLEON_4, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE120_KECLEON_5, MAP_ROUTE120, 40, 7, 51, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_120_KECLEON_5, FLAG_HIDE_ROUTE_120_KECLEON_5, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE119_KECLEON_1, MAP_ROUTE119, 35, 31, 6, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_119_KECLEON_1, FLAG_HIDE_ROUTE_119_KECLEON_1, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_ROUTE119_KECLEON_2, MAP_ROUTE119, 36, 25, 15, SPECIES_KECLEON, 30, FLAG_HIDE_ROUTE_119_KECLEON_2, FLAG_HIDE_ROUTE_119_KECLEON_2, FLAG_RECEIVED_DEVON_SCOPE, FALSE),
    SOURCE(MP_STATIC_RESET_LUGIA, MAP_NAVEL_ROCK_BOTTOM, 1, 11, 13, SPECIES_LUGIA, 70, FLAG_HIDE_LUGIA, FLAG_DEFEATED_LUGIA, FLAG_ENABLE_SHIP_NAVEL_ROCK, TRUE),
    SOURCE(MP_STATIC_RESET_HO_OH, MAP_NAVEL_ROCK_TOP, 1, 12, 9, SPECIES_HO_OH, 70, FLAG_HIDE_HO_OH, FLAG_DEFEATED_HO_OH, FLAG_ENABLE_SHIP_NAVEL_ROCK, TRUE),
    // Mew's exact chased coordinates come only from CaptureMew, not this table.
    SOURCE(MP_STATIC_RESET_MEW, MAP_FARAWAY_ISLAND_INTERIOR, 1, 0, 0, SPECIES_MEW, 30, FLAG_HIDE_MEW, FLAG_DEFEATED_MEW, FLAG_ENABLE_SHIP_FARAWAY_ISLAND, TRUE),
    // These hidden map templates are explicitly spawned by the native script;
    // their hide flags intentionally remain set even while the object exists.
    SOURCE(MP_STATIC_RESET_DEOXYS, MAP_BIRTH_ISLAND_EXTERIOR, 2, 15, 10, SPECIES_DEOXYS_NORMAL, 30, FLAG_HIDE_DEOXYS, FLAG_DEFEATED_DEOXYS, FLAG_ENABLE_SHIP_BIRTH_ISLAND, TRUE),
    SOURCE(MP_STATIC_RESET_LATIOS, MAP_SOUTHERN_ISLAND_INTERIOR, 2, 13, 11, SPECIES_LATIOS, 50, FLAG_HIDE_SOUTHERN_ISLAND_UNCHOSEN_EON_DUO_MON, FLAG_DEFEATED_LATIAS_OR_LATIOS, FLAG_ENABLE_SHIP_SOUTHERN_ISLAND, TRUE),
    SOURCE(MP_STATIC_RESET_LATIAS, MAP_SOUTHERN_ISLAND_INTERIOR, 2, 13, 11, SPECIES_LATIAS, 50, FLAG_HIDE_SOUTHERN_ISLAND_UNCHOSEN_EON_DUO_MON, FLAG_DEFEATED_LATIAS_OR_LATIOS, FLAG_ENABLE_SHIP_SOUTHERN_ISLAND, TRUE),
};
_Static_assert(ARRAY_COUNT(sSources) == MP_STATIC_RESET_LAST - MP_STATIC_RESET_VOLTORB_1 + 1, "Static catalogue IDs");
#if MP_LOCAL_RESET_STATIC || TESTING
#define MP_STATIC_COMPILED_MASK ((1u << ARRAY_COUNT(sSources)) - 1)
#elif MP_LOCAL_RESET_NEW_MAUVILLE_PILOT
#define MP_STATIC_COMPILED_MASK 1u
#else
#define MP_STATIC_COMPILED_MASK 0u
#endif
#if MP_LOCAL_RESET_ENABLED
#define MP_STATIC_CONTROL_FLAGS 3u
#else
#define MP_STATIC_CONTROL_FLAGS 0u
#endif
#if MP_LOCAL_RESET_DEVELOPMENT
#define MP_STATIC_DEV_FLAG 1u
#else
#define MP_STATIC_DEV_FLAG 0u
#endif
#if TESTING
#define MP_STATIC_TEST_FLAG 2u
#else
#define MP_STATIC_TEST_FLAG 0u
#endif
const struct MultiplayerResetCatalogue gMultiplayerResetCatalogue = {
    1, MP_STATIC_RESET_VOLTORB_1, ARRAY_COUNT(sSources), MP_STATIC_COMPILED_MASK,
    MP_STATIC_CONTROL_FLAGS, MP_STATIC_DEV_FLAG | MP_STATIC_TEST_FLAG,
};
_Static_assert(sizeof(gMultiplayerResetCatalogue) == 24, "Reset catalogue layout");
_Static_assert(ARRAY_COUNT(sSources) < 32, "Reset catalogue bit budget");
static EWRAM_DATA u32 sObserved[ARRAY_COUNT(sSources)];
static EWRAM_DATA u32 sAttempt, sActiveId, sAdmittedCount, sAdmittedId;
static EWRAM_DATA u16 sMapState, sBlueState, sGreenState;
static EWRAM_DATA u16 sWeatherSteps, sWeatherEnd;
static EWRAM_DATA u16 sPuzzleSteps;
static EWRAM_DATA struct {
    bool8 valid, invisible, fixedPriority;
    u8 subspriteMode, subspriteTableNum, subpriority, priority;
    u8 elevation, previousElevation, facing;
    s16 x, y, previousX, previousY;
    u16 steps, temp1;
} sMewReturn;

static const struct StaticSource *Source(u32 id)
{
    if (id < MP_STATIC_RESET_VOLTORB_1 || id > MP_STATIC_RESET_LAST) return NULL;
    return &sSources[id - MP_STATIC_RESET_VOLTORB_1];
}

void MultiplayerStaticReset_InitCache(void)
{
    // Retain the descriptor in every normal build, including disabled builds.
    // A volatile read prevents linker GC without changing runtime eligibility.
    (void)*(const volatile u32 *)&gMultiplayerResetCatalogue.version;
    memset(sObserved, 0, sizeof(sObserved));
    sAttempt = sActiveId = sAdmittedCount = sAdmittedId = 0;
    sMewReturn.valid = FALSE;
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
static bool32 IsWeatherSource(u32 id) { return id == MP_STATIC_RESET_KYOGRE || id == MP_STATIC_RESET_GROUDON; }
static bool32 IsKecleon(u32 id) { return id >= MP_STATIC_RESET_ROUTE120_KECLEON_1 && id <= MP_STATIC_RESET_ROUTE119_KECLEON_2; }
static bool32 IsLati(u32 id) { return id == MP_STATIC_RESET_LATIOS || id == MP_STATIC_RESET_LATIAS; }
static bool32 TicketReady(u32 id)
{
    if (id == MP_STATIC_RESET_LUGIA || id == MP_STATIC_RESET_HO_OH)
        return FlagGet(FLAG_ARRIVED_AT_NAVEL_ROCK) && CheckBagHasItem(ITEM_MYSTIC_TICKET, 1);
    if (id == MP_STATIC_RESET_MEW) return CheckBagHasItem(ITEM_OLD_SEA_MAP, 1);
    if (id == MP_STATIC_RESET_DEOXYS) return CheckBagHasItem(ITEM_AURORA_TICKET, 1);
    if (IsLati(id)) return CheckBagHasItem(ITEM_EON_TICKET, 1);
    return TRUE;
}
static u16 MapState(u32 id)
{
    if (IsNewMauville(id)) return VarGet(VAR_NEW_MAUVILLE_STATE);
    if (id == MP_STATIC_RESET_RAYQUAZA) return VarGet(VAR_SKY_PILLAR_STATE);
    if (IsWeatherSource(id)) return VarGet(VAR_ABNORMAL_WEATHER_LOCATION);
    if (IsKecleon(id)) return gSpecialVar_0x8009;
    if (id == MP_STATIC_RESET_HO_OH) return GetCurrentWeather();
    if (id == MP_STATIC_RESET_MEW) return VarGet(VAR_FARAWAY_ISLAND_STEP_COUNTER);
    if (id == MP_STATIC_RESET_DEOXYS) return VarGet(VAR_DEOXYS_ROCK_LEVEL);
    if (IsLati(id)) return VarGet(VAR_ROAMER_POKEMON);
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
     || ((id == MP_STATIC_RESET_DEOXYS || IsLati(id)) ? !FlagGet(source->hide) : FlagGet(source->hide))
     || FlagGet(source->defeated) || !TicketReady(id)
     || (source->prerequisite && !FlagGet(source->prerequisite))) return FALSE;
    if (IsNewMauville(id) && MapState(id) != 1 && MapState(id) != 2) return FALSE;
    if (id == MP_STATIC_RESET_RAYQUAZA && MapState(id) < 2) return FALSE;
    if (IsWeatherSource(id))
    {
        u16 start = id == MP_STATIC_RESET_KYOGRE ? MARINE_CAVE_LOCATIONS_START : TERRA_CAVE_LOCATIONS_START;
        u16 count = id == MP_STATIC_RESET_KYOGRE ? MARINE_CAVE_LOCATIONS : TERRA_CAVE_LOCATIONS;
        if (VarGet(VAR_TEMP_1) != 1 || MapState(id) < start || MapState(id) >= start + count) return FALSE;
    }
    if (IsKecleon(id) && (MapState(id) != id - MP_STATIC_RESET_ROUTE120_KECLEON_1 + 1
     || !CheckBagHasItem(ITEM_DEVON_SCOPE, 1))) return FALSE;
    if (id == MP_STATIC_RESET_LUGIA && (FlagGet(FLAG_CAUGHT_LUGIA)
     || !FlagGet(FLAG_ARRIVED_AT_NAVEL_ROCK) || !CheckBagHasItem(ITEM_MYSTIC_TICKET, 1))) return FALSE;
    if (id == MP_STATIC_RESET_HO_OH && (FlagGet(FLAG_CAUGHT_HO_OH)
     || VarGet(VAR_TEMP_1) != 1 || MapState(id) != WEATHER_NONE)) return FALSE;
    if (id == MP_STATIC_RESET_MEW && (!sMewReturn.valid || FlagGet(FLAG_CAUGHT_MEW)
     || VarGet(VAR_TEMP_1) != sMewReturn.temp1 || MapState(id) != 0xFFFF)) return FALSE;
    if (id == MP_STATIC_RESET_DEOXYS && (FlagGet(FLAG_BATTLED_DEOXYS)
     || !FlagGet(FLAG_DEOXYS_ROCK_COMPLETE) || !FlagGet(FLAG_HIDE_BIRTH_ISLAND_DEOXYS_TRIANGLE)
     || MapState(id) != 10)) return FALSE; // final level of the native eleven-position puzzle
    if (IsLati(id) && (FlagGet(FLAG_CAUGHT_LATIAS_OR_LATIOS) || !FlagGet(FLAG_TEMP_2)
     || !FlagGet(FLAG_ENCOUNTERED_LATIAS_OR_LATIOS) || VarGet(VAR_TEMP_4) != source->species
     || ((MapState(id) == 0) != (id == MP_STATIC_RESET_LATIOS)))) return FALSE;
    if (TryGetObjectEventIdByLocalIdAndMap(source->localId, source->mapNum, source->mapGroup, &objectId)) return FALSE;
    if ((IsKecleon(id) && (gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_KECLEON
                          || gObjectEvents[objectId].invisible))
     || (id == MP_STATIC_RESET_KYOGRE && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_KYOGRE_FRONT)
     || (id == MP_STATIC_RESET_GROUDON && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_GROUDON_FRONT)
     || (id == MP_STATIC_RESET_LUGIA && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_LUGIA)
     || (id == MP_STATIC_RESET_HO_OH && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_HOOH)
     || (id == MP_STATIC_RESET_MEW && (gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_MEW || gObjectEvents[objectId].invisible))
     || (id == MP_STATIC_RESET_DEOXYS && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_DEOXYS)
     || (id == MP_STATIC_RESET_LATIOS && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_LATIOS)
     || (id == MP_STATIC_RESET_LATIAS && gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_LATIAS)) return FALSE;
    // This descriptor belongs only to the existing Wailmer Pail interaction.
    // The script admits after watering; recheck its item and exact native object
    // while opening/confirming the first-action window. Nothing consumes the pail.
    if (id == MP_STATIC_RESET_SUDOWOODO
     && (gSpecialVar_ItemId != ITEM_WAILMER_PAIL || !CheckBagHasItem(ITEM_WAILMER_PAIL, 1)
      || gObjectEvents[objectId].graphicsId != OBJ_EVENT_GFX_SUDOWOODO)) return FALSE;
    return !gObjectEvents[objectId].isPlayer
        && !Multiplayer_IsRemoteObject(&gObjectEvents[objectId])
        && gObjectEvents[objectId].currentCoords.x == (id == MP_STATIC_RESET_MEW ? sMewReturn.x : source->x + MAP_OFFSET)
        && gObjectEvents[objectId].currentCoords.y == (id == MP_STATIC_RESET_MEW ? sMewReturn.y - 2 : source->y + MAP_OFFSET);
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
    sWeatherSteps = VarGet(VAR_ABNORMAL_WEATHER_STEP_COUNTER);
    sWeatherEnd = VarGet(VAR_SHOULD_END_ABNORMAL_WEATHER);
    sPuzzleSteps = VarGet(VAR_DEOXYS_ROCK_STEP_COUNT);
    return TRUE;
}

bool32 MultiplayerStaticReset_GuardMatches(u32 id)
{
    const struct StaticSource *source = Source(id);
    struct Pokemon enemy;
    if (!MultiplayerStaticReset_EntryReady(id) || !TicketReady(id) || sActiveId != id
     || MultiplayerStaticReset_GetCount(id) != sAttempt || MapState(id) != sMapState
     || (id == MP_STATIC_RESET_VOLTORB_1 && VarGet(VAR_MP_VOLTORB_1_RESET_VERSION) != MP_STATIC_RESET_VERSION_1)
     || (IsWeatherSource(id) && (VarGet(VAR_TEMP_1) != sBlueState
         || VarGet(VAR_ABNORMAL_WEATHER_STEP_COUNTER) != sWeatherSteps
         || VarGet(VAR_SHOULD_END_ABNORMAL_WEATHER) != sWeatherEnd))
     || (IsNewMauville(id) && (VarGet(VAR_TEMP_1) != sBlueState || VarGet(VAR_TEMP_2) != sGreenState))) return FALSE;
    if (id == MP_STATIC_RESET_DEOXYS && VarGet(VAR_DEOXYS_ROCK_STEP_COUNT) != sPuzzleSteps) return FALSE;
    // Clone reads: native checksum failure handling may mark a bad egg.
    enemy = gParties[B_TRAINER_OPPONENT_A][0];
    if (GetMonData(&enemy, MON_DATA_SPECIES) != source->species
     || !GetMonData(&enemy, MON_DATA_SANITY_HAS_SPECIES)
     || GetMonData(&enemy, MON_DATA_SANITY_IS_BAD_EGG)
     || GetMonData(&enemy, MON_DATA_IS_EGG) || enemy.box.isEgg
     || GetMonData(&enemy, MON_DATA_LEVEL) != source->level) return FALSE;
    if (id >= MP_STATIC_RESET_LUGIA && !GetMonData(&enemy, MON_DATA_MODERN_FATEFUL_ENCOUNTER)) return FALSE;
    if (IsLati(id) && GetMonData(&enemy, MON_DATA_HELD_ITEM) != ITEM_SOUL_DEW) return FALSE;
    for (u32 slot = 1; slot < PARTY_SIZE; slot++)
    {
        enemy = gParties[B_TRAINER_OPPONENT_A][slot];
        if (GetMonData(&enemy, MON_DATA_SPECIES)) return FALSE;
    }
    return TRUE;
}

static void Begin(u32 id)
{
    MultiplayerResetAdmission_Begin(id);
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

void MultiplayerStaticReset_ReturnWeatherSource(void)
{
#if MP_LOCAL_RESET_STATIC || TESTING
    // Called only from the authored neutral return, after an explicit refusal
    // or consumed first-action reset. Never clear a hide/defeat/weather flag,
    // respawn a missing object, or move the player to manufacture eligibility.
    if (gMain.inBattle) return;
    for (u32 id = MP_STATIC_RESET_KYOGRE; id <= MP_STATIC_RESET_GROUDON; id++)
    {
        const struct StaticSource *source = Source(id);
        u8 objectId;
        if (!MultiplayerStaticReset_EntryReady(id)
         || TryGetObjectEventIdByLocalIdAndMap(source->localId, source->mapNum, source->mapGroup, &objectId)) continue;
        struct ObjectEvent *object = &gObjectEvents[objectId];
        if (object->spriteId >= MAX_SPRITES || !gSprites[object->spriteId].inUse) return;
        struct Sprite *sprite = &gSprites[object->spriteId];
        // Admission can be refused before any battle has rebuilt field sprites.
        // Free only a still-owned affine matrix; after battle it is already off.
        if (sprite->oam.affineMode != ST_OAM_AFFINE_OFF)
        {
            FreeOamMatrix(sprite->oam.matrixNum);
            sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
        }
        const struct ObjectEventGraphicsInfo *graphics = GetObjectEventGraphicsInfo(object->graphicsId);
        if (graphics->subspriteTables != NULL) SetSubspriteTables(sprite, graphics->subspriteTables);
        else sprite->subspriteMode = SUBSPRITES_OFF;
        sprite->x2 = sprite->y2 = 0;
        TryMoveObjectEventToMapCoords(source->localId, source->mapNum, source->mapGroup, source->x, source->y - 3);
        ObjectEventTurn(object, DIR_SOUTH);
        // VAR_TEMP_1 remains 1. A new player step onto the original coordinate
        // trigger runs the same approach and asks for a new durable admission.
        return;
    }
#endif
}

void MultiplayerStaticReset_CaptureMew(void)
{
    sMewReturn.valid = FALSE;
    gSpecialVar_Result = FALSE;
#if MP_LOCAL_RESET_STATIC || TESTING
    const struct StaticSource *source = Source(MP_STATIC_RESET_MEW);
    u8 objectId;
    if (gMain.inBattle || !gSaveBlock1Ptr
     || gSaveBlock1Ptr->location.mapGroup != source->mapGroup
     || gSaveBlock1Ptr->location.mapNum != source->mapNum
     || gSpecialVar_LastTalked != source->localId
     || FlagGet(source->hide) || FlagGet(source->defeated) || FlagGet(FLAG_CAUGHT_MEW)
     || !FlagGet(source->prerequisite) || !TicketReady(source->id)
     || VarGet(VAR_TEMP_1) != 1 || VarGet(VAR_FARAWAY_ISLAND_STEP_COUNTER) >= 9999
     || TryGetObjectEventIdByLocalIdAndMap(source->localId, source->mapNum, source->mapGroup, &objectId)) return;
    struct ObjectEvent *object = &gObjectEvents[objectId];
    if (object->isPlayer || Multiplayer_IsRemoteObject(object) || object->graphicsId != OBJ_EVENT_GFX_MEW
     || object->spriteId >= MAX_SPRITES || !gSprites[object->spriteId].inUse
     || object->currentCoords.y < MAP_OFFSET + 2
     || !MetatileBehavior_IsPokeGrass(MapGridGetMetatileBehaviorAt(object->currentCoords.x, object->currentCoords.y))) return;
    const struct Sprite *sprite = &gSprites[object->spriteId];
    // Only the authored interaction supplies this ephemeral presentation anchor.
    // Never copy a save, player position, RNG state, puzzle or progression flag.
    sMewReturn.x = object->currentCoords.x;
    sMewReturn.y = object->currentCoords.y;
    sMewReturn.previousX = object->previousCoords.x;
    sMewReturn.previousY = object->previousCoords.y;
    sMewReturn.elevation = object->currentElevation;
    sMewReturn.previousElevation = object->previousElevation;
    sMewReturn.facing = object->facingDirection;
    sMewReturn.invisible = object->invisible;
    sMewReturn.fixedPriority = object->fixedPriority;
    sMewReturn.subspriteMode = sprite->subspriteMode;
    sMewReturn.subspriteTableNum = sprite->subspriteTableNum;
    sMewReturn.subpriority = sprite->subpriority;
    sMewReturn.priority = sprite->oam.priority;
    sMewReturn.steps = VarGet(VAR_FARAWAY_ISLAND_STEP_COUNTER);
    sMewReturn.temp1 = VarGet(VAR_TEMP_1);
    sMewReturn.valid = TRUE;
    gSpecialVar_Result = TRUE;
#endif
}

void MultiplayerStaticReset_ForgetMew(void)
{
    sMewReturn.valid = FALSE;
}

void MultiplayerStaticReset_ReturnTicketSource(void)
{
    gSpecialVar_Result = FALSE;
#if MP_LOCAL_RESET_STATIC || TESTING
    if (gMain.inBattle) return;
    for (u32 id = MP_STATIC_RESET_HO_OH; id <= MP_STATIC_RESET_LATIAS; id++)
    {
        const struct StaticSource *source = Source(id);
        u8 objectId;
        if (!MultiplayerStaticReset_EntryReady(id)
         || TryGetObjectEventIdByLocalIdAndMap(source->localId, source->mapNum, source->mapGroup, &objectId)) continue;
        struct ObjectEvent *object = &gObjectEvents[objectId];
        if (object->spriteId >= MAX_SPRITES || !gSprites[object->spriteId].inUse) return;
        if (id == MP_STATIC_RESET_HO_OH)
        {
            TryMoveObjectEventToMapCoords(source->localId, source->mapNum, source->mapGroup, 12, 4);
            ObjectEventTurn(object, DIR_SOUTH);
            VarSet(VAR_TEMP_1, 0); // only the native approach trigger; no caught/defeated flag
        }
        else if (id == MP_STATIC_RESET_MEW)
        {
            struct Sprite *sprite = &gSprites[object->spriteId];
            TryMoveObjectEventToMapCoords(source->localId, source->mapNum, source->mapGroup,
                                         sMewReturn.x - MAP_OFFSET, sMewReturn.y - MAP_OFFSET);
            object->previousCoords.x = sMewReturn.previousX;
            object->previousCoords.y = sMewReturn.previousY;
            object->currentElevation = sMewReturn.elevation;
            object->previousElevation = sMewReturn.previousElevation;
            object->invisible = sMewReturn.invisible;
            object->fixedPriority = sMewReturn.fixedPriority;
            ObjectEventTurn(object, sMewReturn.facing);
            sprite->subspriteMode = sMewReturn.subspriteMode;
            sprite->subspriteTableNum = sMewReturn.subspriteTableNum;
            sprite->subpriority = sMewReturn.subpriority;
            sprite->oam.priority = sMewReturn.priority;
            VarSet(VAR_FARAWAY_ISLAND_STEP_COUNTER, sMewReturn.steps);
            sMewReturn.valid = FALSE;
            // The native script already destroyed its grass-effect sprite
            // before admission. Its old sprite index must never be reused here.
        }
        else if (IsLati(id))
        {
            // This temporary object was explicitly added from a hidden template.
            // Remove only the local instance; do not write its hide flag, permanent
            // encounter history, roaming selection, ticket or species variable.
            RemoveObjectEvent(object);
            FlagClear(FLAG_TEMP_2);
        }
        // Deoxys stays revealed at its post-approach position. Its new guarded
        // object interaction retries generation without undoing the solved puzzle.
        gSpecialVar_Result = TRUE;
        return;
    }
#endif
}

bool32 MultiplayerStaticReset_SetAdmission(u32 id, u32 count)
{
    if (!id && !count) { sAdmittedCount = sAdmittedId = 0; return TRUE; }
    if (!Source(id) || count <= MultiplayerStaticReset_GetCount(id)
     || !MultiplayerStaticReset_EntryReady(id)) return FALSE;
    sAdmittedCount = count; sAdmittedId = id;
    return TRUE;
}
