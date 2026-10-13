// Shared visible wild Pokemon (EmeraldTogether Phase 3): see multiplayer_spawns.h.
// Native PC builds only; the GBA build compiles this file to nothing.
#include "global.h"
#ifdef PORTABLE
#include <stddef.h>
#include "multiplayer_spawns.h"
#include "multiplayer_world.h"
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_effect.h"
#include "field_weather.h"
#include "fieldmap.h"
#include "main.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "task.h"
#include "wild_encounter_ow.h"
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"
#include "constants/songs.h"
#include "constants/trainer_types.h"

_Static_assert(sizeof(struct MultiplayerSpawnAction) == 16, "Spawn action ABI");
_Static_assert(sizeof(struct MultiplayerSpawnShown) == 8, "Spawn shown ABI");
_Static_assert(sizeof(struct MultiplayerSpawnLocal) == 116, "Spawn local ABI");
_Static_assert(sizeof(struct MultiplayerSpawnSlot) == MP_SPAWN_SLOT_BYTES, "Spawn slot ABI");
_Static_assert(sizeof(struct MultiplayerSpawnMailbox) == MP_SPAWN_MAILBOX_BYTES, "Spawn mailbox ABI");
_Static_assert(offsetof(struct MultiplayerSpawnMailbox, local) == 12, "Spawn local offset");
_Static_assert(offsetof(struct MultiplayerSpawnMailbox, inboundGeneration) == 128, "Spawn inbound offset");
_Static_assert(offsetof(struct MultiplayerSpawnMailbox, flags) == 132, "Spawn flags offset");
_Static_assert(offsetof(struct MultiplayerSpawnMailbox, slots) == 144, "Spawn slots offset");
_Static_assert(MP_SPAWN_LOCAL_ID_BASE + MP_SPAWN_MAX_SLOTS - 1 < LOCALID_BERRY_BLENDER_PLAYER_END - 4,
               "Spawn IDs stay below the Berry Blender players");
_Static_assert(DIR_SOUTH == 1 && DIR_NORTH == 2 && DIR_WEST == 3 && DIR_EAST == 4, "Host facing values");

#define CLAIM_TIMEOUT_FRAMES 300
#define SPAWN_SPRITE_RESERVE 12
// Object events the shared Pokemon always leave free: the engine has 16 for
// everything on the field, and the map's own people (trainers included), the
// other players and their followers come first. Only as many shared Pokemon as
// fit are drawn, the nearest first; when people need room, the farthest go.
#define SPAWN_OBJECT_RESERVE 4

volatile struct MultiplayerSpawnMailbox gMultiplayerSpawnMailbox = {0};
const u32 gMultiplayerSpawnAbiVersion = MP_SPAWN_ABI_VERSION;

struct SpawnInbound
{
    u32 flags;
    u32 actionAck;
    u8 mapGroup;
    u8 mapNum;
    u8 count;
    struct MultiplayerSpawnSlot slots[MP_SPAWN_MAX_SLOTS];
};

// One drawn Pokemon per local object ID MP_SPAWN_LOCAL_ID_BASE + index.
struct SpawnObject
{
    u32 spawnId; // zero: unused
    u8 objectEventId;
    u8 moveSeq;
    u16 graphicsId;
};

static struct SpawnInbound sInbound;
static struct SpawnObject sObjects[MP_SPAWN_MAX_SLOTS];
static u32 sSeenGeneration;
static bool8 sHaveInbound;
static bool8 sConnected;
static bool8 sInteraction;      // the World Event script is talking to a spawn
static u32 sClaimSpawnId;       // asked for (or granted) by the current interaction
static u32 sBattleSpawnId;      // its battle
static u16 sBattleSpecies;
static bool8 sBattleActive;
static u8 sResult;              // MP_WORLD_RESULT_*
static u32 sPendingResultSpawnId;
static u8 sPendingResult;
static u16 sPendingResultSpecies;
static u32 sHiddenSpawnId;      // caught or defeated here: hidden until the host drops it

// ---------------------------------------------------------------------------
// Mailbox

void MultiplayerSpawns_Init(void)
{
    u32 i;

    memset((void *)&gMultiplayerSpawnMailbox, 0, sizeof(gMultiplayerSpawnMailbox));
    gMultiplayerSpawnMailbox.magic = MP_SPAWN_MAGIC;
    gMultiplayerSpawnMailbox.abiVersion = MP_SPAWN_ABI_VERSION;
    gMultiplayerSpawnMailbox.byteSize = MP_SPAWN_MAILBOX_BYTES;
    memset(&sInbound, 0, sizeof(sInbound));
    for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
    {
        sObjects[i].spawnId = 0;
        sObjects[i].objectEventId = OBJECT_EVENTS_COUNT;
    }
    sSeenGeneration = 0;
    sHaveInbound = sConnected = sInteraction = sBattleActive = FALSE;
    sClaimSpawnId = sBattleSpawnId = sPendingResultSpawnId = sHiddenSpawnId = 0;
}

static void ReadInbound(void)
{
    struct SpawnInbound copy;
    u32 generation = gMultiplayerSpawnMailbox.inboundGeneration;
    u32 i;

    if ((generation & 1) || generation == sSeenGeneration)
        return;
    copy.flags = gMultiplayerSpawnMailbox.flags;
    copy.actionAck = gMultiplayerSpawnMailbox.actionAck;
    copy.mapGroup = gMultiplayerSpawnMailbox.mapGroup;
    copy.mapNum = gMultiplayerSpawnMailbox.mapNum;
    copy.count = gMultiplayerSpawnMailbox.count;
    for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
        copy.slots[i] = *(const struct MultiplayerSpawnSlot *)&gMultiplayerSpawnMailbox.slots[i];
    if (generation != gMultiplayerSpawnMailbox.inboundGeneration)
        return;
    if (copy.count > MP_SPAWN_MAX_SLOTS)
        copy.count = 0;
    sSeenGeneration = generation;
    sInbound = copy;
    sHaveInbound = TRUE;
}

// The save is loaded (the title screen and intro run before it is).
static bool32 SaveLoaded(void)
{
    return gSaveBlock1Ptr != NULL && gMapHeader.mapLayout != NULL;
}

static bool32 ActionPending(void)
{
    return gMultiplayerSpawnMailbox.local.action.seq != 0
        && gMultiplayerSpawnMailbox.local.action.seq != sInbound.actionAck;
}

static void PostAction(u8 kind, u8 result, u32 spawnId, u16 species)
{
    struct MultiplayerSpawnAction action = {0};
    u32 generation = (gMultiplayerSpawnMailbox.localGeneration + 1) | 1;

    action.seq = gMultiplayerSpawnMailbox.local.action.seq + 1;
    if (action.seq == 0)
        action.seq = 1;
    action.kind = kind;
    action.result = result;
    action.spawnId = spawnId;
    action.species = species;
    if (SaveLoaded())
    {
        action.mapGroup = gSaveBlock1Ptr->location.mapGroup;
        action.mapNum = gSaveBlock1Ptr->location.mapNum;
    }
    gMultiplayerSpawnMailbox.localGeneration = generation;
    *(struct MultiplayerSpawnAction *)&gMultiplayerSpawnMailbox.local.action = action;
    gMultiplayerSpawnMailbox.localGeneration = generation + 1;
}

static void PublishLocal(void)
{
    u32 generation = (gMultiplayerSpawnMailbox.localGeneration + 1) | 1;
    u32 i, shown = 0;

    gMultiplayerSpawnMailbox.localGeneration = generation;
    if (SaveLoaded() && gMain.callback2 == CB2_Overworld)
    {
        gMultiplayerSpawnMailbox.local.mapGroup = gSaveBlock1Ptr->location.mapGroup;
        gMultiplayerSpawnMailbox.local.mapNum = gSaveBlock1Ptr->location.mapNum;
        gMultiplayerSpawnMailbox.local.weather = GetCurrentWeather();
    }
    for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
    {
        struct ObjectEvent *objectEvent;
        if (sObjects[i].spawnId == 0 || sObjects[i].objectEventId >= OBJECT_EVENTS_COUNT)
            continue;
        objectEvent = &gObjectEvents[sObjects[i].objectEventId];
        gMultiplayerSpawnMailbox.local.shown[shown].spawnId = sObjects[i].spawnId;
        gMultiplayerSpawnMailbox.local.shown[shown].x = objectEvent->currentCoords.x - MAP_OFFSET;
        gMultiplayerSpawnMailbox.local.shown[shown].y = objectEvent->currentCoords.y - MAP_OFFSET;
        shown++;
    }
    gMultiplayerSpawnMailbox.local.shownCount = shown;
    for (i = shown; i < MP_SPAWN_MAX_SLOTS; i++)
    {
        gMultiplayerSpawnMailbox.local.shown[i].spawnId = 0;
        gMultiplayerSpawnMailbox.local.shown[i].x = 0;
        gMultiplayerSpawnMailbox.local.shown[i].y = 0;
    }
    gMultiplayerSpawnMailbox.localGeneration = generation + 1;
}

bool32 MultiplayerSpawns_IsActive(void)
{
    return gMultiplayerSpawnMailbox.magic == MP_SPAWN_MAGIC && sConnected && sHaveInbound
        && (sInbound.flags & MP_SPAWN_FLAG_ENABLED);
}

static const struct MultiplayerSpawnSlot *FindSlot(u32 spawnId)
{
    u32 i;

    if (spawnId == 0 || !sHaveInbound)
        return NULL;
    for (i = 0; i < sInbound.count; i++)
    {
        if (sInbound.slots[i].spawnId == spawnId)
            return &sInbound.slots[i];
    }
    return NULL;
}

static bool32 InboundIsForThisMap(void)
{
    return sHaveInbound && SaveLoaded() && sInbound.mapGroup == gSaveBlock1Ptr->location.mapGroup
        && sInbound.mapNum == gSaveBlock1Ptr->location.mapNum;
}

// ---------------------------------------------------------------------------
// Objects

static u16 SlotSpecies(const struct MultiplayerSpawnSlot *slot)
{
    if (slot->species == SPECIES_UNOWN)
        return GetUnownSpeciesId(slot->personality);
    return slot->species;
}

static u16 SlotGraphicsId(const struct MultiplayerSpawnSlot *slot)
{
    u16 species = SlotSpecies(slot);
    bool32 female = GetGenderFromSpeciesAndPersonality(species, slot->personality) == MON_FEMALE;
    return GetGraphicsIdForMon(species, (slot->flags & MP_SPAWN_SLOT_SHINY) ? TRUE : FALSE, female);
}

static bool32 ValidSlot(const struct MultiplayerSpawnSlot *slot)
{
    return slot->spawnId != 0 && SaveLoaded() && slot->species != SPECIES_NONE && slot->species < NUM_SPECIES
        && slot->x >= 0 && slot->y >= 0
        && slot->x < gMapHeader.mapLayout->width && slot->y < gMapHeader.mapLayout->height
        && slot->facing >= DIR_SOUTH && slot->facing <= DIR_EAST;
}

// The engine's own object window (TrySpawnObjectEvents, RemoveObjectEventsOutsideView)
// in map coordinates with MAP_OFFSET; gSaveBlock1Ptr->pos is the camera's corner
// there (the player stands at pos + MAP_OFFSET).
static bool32 OnScreen(s32 mapX, s32 mapY)
{
    return mapX >= gSaveBlock1Ptr->pos.x - 2 && mapX <= gSaveBlock1Ptr->pos.x + MAP_OFFSET_W + 2
        && mapY >= gSaveBlock1Ptr->pos.y && mapY <= gSaveBlock1Ptr->pos.y + MAP_OFFSET_H + 2;
}

bool32 MultiplayerSpawns_IsSpawnObject(const struct ObjectEvent *objectEvent)
{
    u32 i;

    if (objectEvent == NULL || !objectEvent->active)
        return FALSE;
    if (objectEvent->localId < MP_SPAWN_LOCAL_ID_BASE || objectEvent->localId >= MP_SPAWN_LOCAL_ID_BASE + MP_SPAWN_MAX_SLOTS)
        return FALSE;
    i = objectEvent->localId - MP_SPAWN_LOCAL_ID_BASE;
    return sObjects[i].spawnId != 0 && sObjects[i].objectEventId < OBJECT_EVENTS_COUNT
        && objectEvent == &gObjectEvents[sObjects[i].objectEventId];
}

static bool32 ObjectStillOurs(u32 index)
{
    u32 id = sObjects[index].objectEventId;
    return id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active
        && gObjectEvents[id].localId == MP_SPAWN_LOCAL_ID_BASE + index;
}

static void RemoveSpawnObject(u32 index)
{
    u32 id = sObjects[index].objectEventId;

    if (id < OBJECT_EVENTS_COUNT && gObjectEvents[id].localId == MP_SPAWN_LOCAL_ID_BASE + index)
    {
        struct ObjectEvent *objectEvent = &gObjectEvents[id];
        u32 spriteId = objectEvent->spriteId;
        if (objectEvent->active && spriteId < MAX_SPRITES
         && gSprites[spriteId].inUse && gSprites[spriteId].data[0] == id)
            RemoveObjectEvent(objectEvent);
        ClearObjectEvent(objectEvent);
    }
    sObjects[index].spawnId = 0;
    sObjects[index].objectEventId = OBJECT_EVENTS_COUNT;
}

void MultiplayerSpawns_LeaveField(void)
{
    u32 i;
    for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
    {
        if (sObjects[i].spawnId != 0)
            RemoveSpawnObject(i);
    }
}

static u32 FreeObjectEvents(void)
{
    u32 i, free = 0;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (!gObjectEvents[i].active)
            free++;
    return free;
}

static bool32 CanAllocateObject(u16 graphicsId)
{
    u32 i, free;
    const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(graphicsId);

    if (info == NULL || FreeObjectEvents() <= SPAWN_OBJECT_RESERVE)
        return FALSE;
    // Never ask the palette allocator to evict a live actor's palette.
    if (IndexOfSpritePaletteTag(info->paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return FALSE;
    // Keep a reserve for weather, field effects and the other players: the
    // engine's CreateSprite treats an exhausted sprite table as a fatal error.
    for (i = 0, free = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free >= SPAWN_SPRITE_RESERVE)
            return TRUE;
    return FALSE;
}

static void PlayAppearAnimation(struct ObjectEvent *objectEvent, const struct MultiplayerSpawnSlot *slot)
{
    enum SpawnDespawnTypeOWE type;
    u32 behavior;

    if (slot->flags & MP_SPAWN_SLOT_SHINY)
    {
        PlaySE(SE_SHINY);
        MovementAction_OverworldEncounterSpawn(OWE_SPAWN_ANIM_SHINY, objectEvent);
        return;
    }
    if (!(slot->flags & MP_SPAWN_SLOT_FRESH))
        return;
    behavior = MapGridGetMetatileBehaviorAt(objectEvent->currentCoords.x, objectEvent->currentCoords.y);
    if (MetatileBehavior_IsPokeGrass(behavior) || MetatileBehavior_IsAshGrass(behavior))
        type = OWE_SPAWN_ANIM_GRASS;
    else if (MetatileBehavior_IsLongGrass(behavior))
        type = OWE_SPAWN_ANIM_LONG_GRASS;
    else
        type = OWE_SPAWN_ANIM_CAVE;
    MovementAction_OverworldEncounterSpawn(type, objectEvent);
}

static void SpawnSlotObject(u32 index, const struct MultiplayerSpawnSlot *slot)
{
    struct ObjectEventTemplate template = {0};
    s16 mapX = slot->x + MAP_OFFSET, mapY = slot->y + MAP_OFFSET;
    u16 graphicsId = SlotGraphicsId(slot);
    u32 id;

    if (!CanAllocateObject(graphicsId) || GetObjectEventIdByLocalId(MP_SPAWN_LOCAL_ID_BASE + index) < OBJECT_EVENTS_COUNT)
        return;
    template.localId = MP_SPAWN_LOCAL_ID_BASE + index;
    template.graphicsId = graphicsId;
    template.kind = OBJ_KIND_NORMAL;
    template.x = slot->x;
    template.y = slot->y;
    template.elevation = MapGridGetElevationAt(mapX, mapY);
    template.movementType = MOVEMENT_TYPE_NONE;
    template.trainerType = TRAINER_TYPE_NONE;
    template.script = EventScript_MultiplayerWorldEvent;
    id = SpawnSpecialObjectEvent(&template);
    if (id >= OBJECT_EVENTS_COUNT)
        return;
    gObjectEvents[id].hideReflection = TRUE;
    gObjectEvents[id].disableCoveringGroundEffects = TRUE;
    MoveObjectEventToMapCoords(&gObjectEvents[id], mapX, mapY);
    ObjectEventTurn(&gObjectEvents[id], slot->facing);
    sObjects[index].spawnId = slot->spawnId;
    sObjects[index].objectEventId = id;
    sObjects[index].moveSeq = slot->moveSeq;
    sObjects[index].graphicsId = graphicsId;
    PlayAppearAnimation(&gObjectEvents[id], slot);
}

static void StepToward(struct ObjectEvent *objectEvent, s16 x, s16 y)
{
    s16 dx = x - objectEvent->currentCoords.x;
    s16 dy = y - objectEvent->currentCoords.y;
    enum Direction direction;

    if (dx == 0 && dy == 0)
        return;
    if (abs(dx) + abs(dy) > 2)
    {
        MoveObjectEventToMapCoords(objectEvent, x, y);
        return;
    }
    direction = dx > 0 ? DIR_EAST : dx < 0 ? DIR_WEST : dy > 0 ? DIR_SOUTH : DIR_NORTH;
    ObjectEventSetHeldMovement(objectEvent, GetWalkNormalMovementAction(direction));
}

static void UpdateSlotObject(u32 index, const struct MultiplayerSpawnSlot *slot)
{
    struct ObjectEvent *objectEvent = &gObjectEvents[sObjects[index].objectEventId];
    s16 mapX = slot->x + MAP_OFFSET, mapY = slot->y + MAP_OFFSET;

    if (ObjectEventClearHeldMovementIfFinished(objectEvent) == TRUE)
        MoveObjectEventToMapCoords(objectEvent, objectEvent->currentCoords.x, objectEvent->currentCoords.y);
    if (ObjectEventIsHeldMovementActive(objectEvent))
        return;
    if (objectEvent->currentCoords.x != mapX || objectEvent->currentCoords.y != mapY)
    {
        StepToward(objectEvent, mapX, mapY);
        return;
    }
    if (objectEvent->facingDirection != slot->facing)
        ObjectEventTurn(objectEvent, slot->facing);
    sObjects[index].moveSeq = slot->moveSeq;
}

static u32 DistanceToPlayer(s32 mapX, s32 mapY)
{
    s32 dx = mapX - (gSaveBlock1Ptr->pos.x + MAP_OFFSET);
    s32 dy = mapY - (gSaveBlock1Ptr->pos.y + MAP_OFFSET);
    return (u32)(abs(dx) + abs(dy));
}

// Keeps SPAWN_OBJECT_RESERVE object events free: removes the farthest drawn
// Pokemon (never the one being talked to or battled) while too few are free.
static void KeepObjectReserve(void)
{
    while (FreeObjectEvents() < SPAWN_OBJECT_RESERVE)
    {
        u32 i, farthest = MP_SPAWN_MAX_SLOTS, distance = 0;

        for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
        {
            struct ObjectEvent *objectEvent;
            u32 d;

            if (sObjects[i].spawnId == 0 || sObjects[i].spawnId == sClaimSpawnId || !ObjectStillOurs(i))
                continue;
            objectEvent = &gObjectEvents[sObjects[i].objectEventId];
            d = DistanceToPlayer(objectEvent->currentCoords.x, objectEvent->currentCoords.y);
            if (farthest == MP_SPAWN_MAX_SLOTS || d > distance)
            {
                farthest = i;
                distance = d;
            }
        }
        if (farthest == MP_SPAWN_MAX_SLOTS)
            return;
        RemoveSpawnObject(farthest);
    }
}

static void UpdateObjects(void)
{
    u32 i, j;
    u8 order[MP_SPAWN_MAX_SLOTS];
    u32 count;

    if (!InboundIsForThisMap())
    {
        MultiplayerSpawns_LeaveField();
        return;
    }
    // Drop objects the host no longer lists, the engine removed off screen, or
    // that wandered out of the drawn area.
    for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
    {
        const struct MultiplayerSpawnSlot *slot;
        if (sObjects[i].spawnId == 0)
            continue;
        slot = FindSlot(sObjects[i].spawnId);
        if (slot == NULL || !ObjectStillOurs(i) || slot->spawnId == sHiddenSpawnId
         || !OnScreen(slot->x + MAP_OFFSET, slot->y + MAP_OFFSET))
        {
            RemoveSpawnObject(i);
            continue;
        }
        UpdateSlotObject(i, slot);
    }
    KeepObjectReserve();
    // Draw the listed Pokemon that are on screen and not drawn yet, the
    // nearest first (as many as the object reserve allows).
    count = sInbound.count < MP_SPAWN_MAX_SLOTS ? sInbound.count : MP_SPAWN_MAX_SLOTS;
    for (j = 0; j < count; j++)
        order[j] = j;
    for (j = 1; j < count; j++)
    {
        u8 index = order[j];
        u32 d = DistanceToPlayer(sInbound.slots[index].x + MAP_OFFSET, sInbound.slots[index].y + MAP_OFFSET);
        i = j;
        while (i > 0 && DistanceToPlayer(sInbound.slots[order[i - 1]].x + MAP_OFFSET,
                                         sInbound.slots[order[i - 1]].y + MAP_OFFSET) > d)
        {
            order[i] = order[i - 1];
            i--;
        }
        order[i] = index;
    }
    for (j = 0; j < count; j++)
    {
        const struct MultiplayerSpawnSlot *slot = &sInbound.slots[order[j]];
        u32 free = MP_SPAWN_MAX_SLOTS;
        bool32 drawn = FALSE;

        if (!ValidSlot(slot) || slot->spawnId == sHiddenSpawnId
         || !OnScreen(slot->x + MAP_OFFSET, slot->y + MAP_OFFSET))
            continue;
        for (i = 0; i < MP_SPAWN_MAX_SLOTS; i++)
        {
            if (sObjects[i].spawnId == slot->spawnId)
                drawn = TRUE;
            else if (sObjects[i].spawnId == 0 && free == MP_SPAWN_MAX_SLOTS)
                free = i;
        }
        if (!drawn && free < MP_SPAWN_MAX_SLOTS)
            SpawnSlotObject(free, slot);
    }
}

// ---------------------------------------------------------------------------
// Claims, battles and results

static void FlushPending(void)
{
    u32 i;

    if (ActionPending())
        return;
    if (sPendingResultSpawnId != 0)
    {
        PostAction(MP_SPAWN_ACTION_RESULT, sPendingResult, sPendingResultSpawnId, sPendingResultSpecies);
        sPendingResultSpawnId = 0;
        return;
    }
    // A Pokemon the host still holds for this player although no battle uses
    // it (a cancelled message, a soft reset, a restarted game): let it go.
    if (sInteraction || sBattleActive || !InboundIsForThisMap())
        return;
    for (i = 0; i < sInbound.count; i++)
    {
        const struct MultiplayerSpawnSlot *slot = &sInbound.slots[i];
        if ((slot->flags & MP_SPAWN_SLOT_MINE) && slot->spawnId != sClaimSpawnId && slot->spawnId != sBattleSpawnId)
        {
            PostAction(MP_SPAWN_ACTION_RELEASE, 0, slot->spawnId, slot->species);
            return;
        }
    }
}

static u32 TalkedSlotIndex(void)
{
    u32 localId = gSpecialVar_LastTalked;

    if (localId < MP_SPAWN_LOCAL_ID_BASE || localId >= MP_SPAWN_LOCAL_ID_BASE + MP_SPAWN_MAX_SLOTS)
        return MP_SPAWN_MAX_SLOTS;
    if (sObjects[localId - MP_SPAWN_LOCAL_ID_BASE].spawnId == 0)
        return MP_SPAWN_MAX_SLOTS;
    return localId - MP_SPAWN_LOCAL_ID_BASE;
}

#define tTimer   data[0]
#define tPosted  data[1]

// VAR_RESULT: 1 granted, 0 another trainer is battling it, 2 unavailable.
static void Task_WaitSpawnClaim(u8 taskId)
{
    const struct MultiplayerSpawnSlot *slot = FindSlot(sClaimSpawnId);
    bool32 done = FALSE;

    if (!MultiplayerSpawns_IsActive() || sClaimSpawnId == 0 || slot == NULL)
    {
        gSpecialVar_Result = 2;
        done = TRUE;
    }
    else if (slot->flags & MP_SPAWN_SLOT_MINE)
    {
        gSpecialVar_Result = 1;
        done = TRUE;
    }
    else if (slot->flags & MP_SPAWN_SLOT_BUSY)
    {
        gSpecialVar_Result = 0;
        done = TRUE;
    }
    else if (!gTasks[taskId].tPosted)
    {
        // The previous action must be answered before a new one is posted.
        if (!ActionPending())
        {
            PostAction(MP_SPAWN_ACTION_CLAIM, 0, slot->spawnId, slot->species);
            gTasks[taskId].tPosted = TRUE;
        }
        else if (++gTasks[taskId].tTimer > CLAIM_TIMEOUT_FRAMES)
        {
            gSpecialVar_Result = 2;
            done = TRUE;
        }
    }
    else if (++gTasks[taskId].tTimer > CLAIM_TIMEOUT_FRAMES)
    {
        gSpecialVar_Result = 2;
        done = TRUE;
    }
    if (done)
    {
        if (gSpecialVar_Result != 1)
        {
            sClaimSpawnId = 0;
            sInteraction = FALSE; // the script shows its message and ends here
        }
        DestroyTask(taskId);
        ScriptContext_Enable();
    }
}

static void Task_EnableScript(u8 taskId)
{
    DestroyTask(taskId);
    ScriptContext_Enable();
}

#undef tTimer
#undef tPosted

bool32 MultiplayerSpawns_TryClaimTalked(void)
{
    u32 index = TalkedSlotIndex();
    const struct MultiplayerSpawnSlot *slot;
    u8 taskId;

    sInteraction = FALSE;
    if (index >= MP_SPAWN_MAX_SLOTS)
        return FALSE;
    sInteraction = TRUE;
    gSpecialVar_Result = 2;
    sClaimSpawnId = 0;
    slot = FindSlot(sObjects[index].spawnId);
    if (MultiplayerSpawns_IsActive() && slot != NULL && !(slot->flags & MP_SPAWN_SLOT_BUSY))
        sClaimSpawnId = slot->spawnId;
    taskId = CreateTask(Task_WaitSpawnClaim, 80);
    if (taskId == TASK_NONE)
    {
        sClaimSpawnId = 0;
        CreateTask(Task_EnableScript, 80);
    }
    else
    {
        gTasks[taskId].data[0] = 0;
        // Already granted (a reconnect, or a cancelled message): no new action.
        gTasks[taskId].data[1] = (slot != NULL && (slot->flags & MP_SPAWN_SLOT_MINE)) ? TRUE : FALSE;
    }
    return TRUE;
}

bool32 MultiplayerSpawns_InInteraction(void)
{
    return sInteraction;
}

void MultiplayerSpawns_StartBattle(void)
{
    const struct MultiplayerSpawnSlot *slot = FindSlot(sClaimSpawnId);
    struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];
    bool32 shiny;
    u32 level;
    u16 species;

    sBattleActive = FALSE;
    if (!MultiplayerSpawns_IsActive() || slot == NULL || !(slot->flags & MP_SPAWN_SLOT_MINE) || !ValidSlot(slot))
    {
        sClaimSpawnId = 0;
        sInteraction = FALSE;
        gSpecialVar_Result = 0;
        CreateTask(Task_EnableScript, 80);
        return;
    }
    level = slot->level;
    if (level < MIN_LEVEL)
        level = MIN_LEVEL;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    species = SlotSpecies(slot);
    shiny = (slot->flags & MP_SPAWN_SLOT_SHINY) ? TRUE : FALSE;
    // The host's personality: every game derives the same gender, nature,
    // ability and Unown letter; the host alone decides shininess.
    ZeroEnemyPartyMons();
    CreateMonWithIVs(mon, species, level, slot->personality, OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
    GiveMonInitialMoveset(mon);
    SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
    sBattleSpawnId = slot->spawnId;
    sBattleSpecies = slot->species;
    sResult = 0;
    sBattleActive = TRUE;
    gSpecialVar_Result = 1;
    BattleSetup_StartScriptedWildBattle();
}

// CB2_EndScriptedWildBattle runs for every scripted wild battle, including a
// whiteout that never returns to the script.
bool32 MultiplayerSpawns_OnScriptedBattleEnd(void)
{
    u8 result;

    if (!sBattleActive)
        return FALSE;
    sBattleActive = FALSE;
    switch (gBattleOutcome)
    {
    case B_OUTCOME_CAUGHT:
        result = MP_WORLD_RESULT_CAUGHT;
        sHiddenSpawnId = sBattleSpawnId;
        break;
    case B_OUTCOME_WON:
        result = MP_WORLD_RESULT_DEFEATED;
        sHiddenSpawnId = sBattleSpawnId;
        break;
    case B_OUTCOME_LOST:
    case B_OUTCOME_DREW:
    case B_OUTCOME_FORFEITED:
        result = MP_WORLD_RESULT_LOST;
        break;
    default:
        result = MP_WORLD_RESULT_RAN;
        break;
    }
    sResult = result;
    // Reported from the field tick so an unanswered action never loses it
    // (the mailbox has one action slot); a lost connection keeps it pending.
    sPendingResultSpawnId = sBattleSpawnId;
    sPendingResult = result;
    sPendingResultSpecies = sBattleSpecies;
    sBattleSpawnId = 0;
    sClaimSpawnId = 0;
    sInteraction = FALSE; // a whiteout never returns to the script
    return TRUE;
}

u8 MultiplayerSpawns_TakeResult(void)
{
    return sResult;
}

// ---------------------------------------------------------------------------

void MultiplayerSpawns_Tick(bool32 fieldReady, bool32 connected)
{
    if (gMultiplayerSpawnMailbox.magic != MP_SPAWN_MAGIC)
        return;
    sConnected = connected;
    ReadInbound();
    if (!connected)
    {
        sHaveInbound = FALSE;
        memset(&sInbound, 0, sizeof(sInbound));
    }
    else if (SaveLoaded())
    {
        FlushPending();
    }
    PublishLocal();
    if (sHiddenSpawnId != 0 && sHaveInbound && FindSlot(sHiddenSpawnId) == NULL)
        sHiddenSpawnId = 0;
    if (!MultiplayerSpawns_IsActive() || gMain.callback2 != CB2_Overworld)
    {
        MultiplayerSpawns_LeaveField();
        return;
    }
    // While a script or menu runs, the Pokemon stay where they are (the one
    // the player talks to among them).
    if (!fieldReady)
        return;
    UpdateObjects();
}

#endif // PORTABLE
