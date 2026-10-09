#include "global.h"
#include <stddef.h>
#include "multiplayer.h"
#include "multiplayer_world.h"
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "script.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"
#include "constants/items.h"
#include "constants/map_types.h"

_Static_assert(sizeof(struct MultiplayerWorldState) == 16, "World state ABI");
_Static_assert(sizeof(struct MultiplayerWorldPeer) == 20, "World peer ABI");
_Static_assert(sizeof(struct MultiplayerWorldEvent) == 32, "World event ABI");
_Static_assert(sizeof(struct MultiplayerWorldAction) == 16, "World action ABI");
_Static_assert(sizeof(struct MultiplayerWorldNotice) == 52, "World notice ABI");
_Static_assert(sizeof(struct MultiplayerWorldMailbox) == MP_WORLD_MAILBOX_BYTES, "World mailbox ABI");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, local) == 12, "World local offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, action) == 28, "World action offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, inboundGeneration) == 44, "World inbound offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, flags) == 48, "World flags offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, peers) == 68, "World peers offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, event) == 128, "World event offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, notice) == 160, "World notice offset");
_Static_assert(MP_WORLD_LOCAL_ID_FOLLOWER_BASE == MULTIPLAYER_LOCAL_ID_BASE + MULTIPLAYER_MAX_PEERS, "Follower IDs follow avatars");
_Static_assert(MP_WORLD_LOCAL_ID_EVENT < LOCALID_OW_ENCOUNTER_END - OWE_SPAWNS_MAX + 1, "Event ID outside generated encounters");

// Morning of world day 1000: a large day origin keeps day-based timers in old
// saves (berry trees, daily events) moving forward instead of stalling.
#define WORLD_DAY_ORIGIN 1000

#define CLAIM_TIMEOUT_FRAMES 300
#define NOTICE_FRAMES 240
#define NOTICE_WINDOW_BASE_BLOCK 0x2A0

EWRAM_DATA volatile struct MultiplayerWorldMailbox gMultiplayerWorldMailbox = {0};
const u32 gMultiplayerWorldAbiVersion = MP_WORLD_ABI_VERSION;

struct WorldInbound
{
    u32 flags;
    u32 worldSeconds;
    u32 actionAck;
    u8 selfAppearance[MP_WORLD_APPEARANCE_BYTES];
    struct MultiplayerWorldPeer peers[MP_WORLD_MAX_PEERS];
    struct MultiplayerWorldEvent event;
    struct MultiplayerWorldNotice notice;
};

struct RemoteFollower
{
    u8 objectEventId;
    u16 graphicsId;
};

static EWRAM_DATA struct WorldInbound sInbound = {0};
static EWRAM_DATA struct RemoteFollower sFollowers[MP_WORLD_MAX_PEERS] = {0};
static EWRAM_DATA u32 sSeenInboundGeneration = 0;
static EWRAM_DATA bool8 sHaveInbound = FALSE;
static EWRAM_DATA bool8 sConnected = FALSE;
static EWRAM_DATA u8 sEventObjectId = 0; // OBJECT_EVENTS_COUNT after init
static EWRAM_DATA u32 sEventObjectEventId = 0;
static EWRAM_DATA u32 sShownNoticeId = 0;
static EWRAM_DATA u8 sNoticeTaskId = 0; // TASK_NONE after init
static EWRAM_DATA u32 sClaimEventId = 0;
static EWRAM_DATA u32 sBattlePersonality = 0;
static EWRAM_DATA u16 sBattleSpecies = 0;
static EWRAM_DATA struct MultiplayerWorldState sLocalMount = {0};

static void Task_WorldNotice(u8 taskId);

// ---------------------------------------------------------------------------
// Mailbox

void MultiplayerWorld_Init(void)
{
    u32 i;
    memset((void *)&gMultiplayerWorldMailbox, 0, sizeof(gMultiplayerWorldMailbox));
    memset(&sInbound, 0, sizeof(sInbound));
    memset(&sLocalMount, 0, sizeof(sLocalMount));
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        sFollowers[i].objectEventId = OBJECT_EVENTS_COUNT;
    sSeenInboundGeneration = 0;
    sHaveInbound = FALSE;
    sConnected = FALSE;
    sEventObjectId = OBJECT_EVENTS_COUNT;
    sEventObjectEventId = 0;
    sShownNoticeId = 0;
    sNoticeTaskId = TASK_NONE;
    sClaimEventId = 0;
    gMultiplayerWorldMailbox.abiVersion = MP_WORLD_ABI_VERSION;
    gMultiplayerWorldMailbox.byteSize = sizeof(gMultiplayerWorldMailbox);
    if (*(const volatile u32 *)&gMultiplayerWorldAbiVersion == MP_WORLD_ABI_VERSION)
        gMultiplayerWorldMailbox.magic = MP_WORLD_MAGIC;
}

static void ReadInbound(void)
{
    struct WorldInbound copy;
    u32 generation = gMultiplayerWorldMailbox.inboundGeneration;
    u32 i;

    if ((generation & 1) || generation == sSeenInboundGeneration)
        return;
    copy.flags = gMultiplayerWorldMailbox.flags;
    copy.worldSeconds = gMultiplayerWorldMailbox.worldSeconds;
    copy.actionAck = gMultiplayerWorldMailbox.actionAck;
    for (i = 0; i < MP_WORLD_APPEARANCE_BYTES; i++)
        copy.selfAppearance[i] = gMultiplayerWorldMailbox.selfAppearance[i];
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        copy.peers[i] = *(const struct MultiplayerWorldPeer *)&gMultiplayerWorldMailbox.peers[i];
    copy.event = *(const struct MultiplayerWorldEvent *)&gMultiplayerWorldMailbox.event;
    copy.notice = *(const struct MultiplayerWorldNotice *)&gMultiplayerWorldMailbox.notice;
    if (generation != gMultiplayerWorldMailbox.inboundGeneration)
        return;
    sSeenInboundGeneration = generation;
    sInbound = copy;
    sHaveInbound = TRUE;
}

static void PublishLocalState(void)
{
    struct MultiplayerWorldState state = {0};
    struct ObjectEvent *follower = GetFollowerObject();
    u32 generation = (gMultiplayerWorldMailbox.localGeneration + 1) | 1;
    u32 i;

    if (follower != NULL && follower->active && IS_OW_MON_OBJ(follower))
    {
        state.followerSpecies = OW_SPECIES(follower);
        state.followerFlags = (OW_SHINY(follower) ? MP_WORLD_FOLLOWER_SHINY : 0)
                            | (OW_FEMALE(follower) ? MP_WORLD_FOLLOWER_FEMALE : 0)
                            | (!follower->invisible ? MP_WORLD_FOLLOWER_VISIBLE : 0);
    }
    state.mountState = sLocalMount.mountState;
    state.mountSpecies = sLocalMount.mountSpecies;
    state.mountFlags = sLocalMount.mountFlags;
    for (i = 0; i < MP_WORLD_APPEARANCE_BYTES; i++)
        state.appearance[i] = sInbound.selfAppearance[i];

    gMultiplayerWorldMailbox.localGeneration = generation;
    *(struct MultiplayerWorldState *)&gMultiplayerWorldMailbox.local = state;
    gMultiplayerWorldMailbox.localGeneration = generation + 1;
}

static bool32 ActionPending(void)
{
    return gMultiplayerWorldMailbox.action.seq != 0
        && gMultiplayerWorldMailbox.action.seq != sInbound.actionAck;
}

static void PostAction(u8 kind, u8 result, u32 eventId, u16 species)
{
    struct MultiplayerWorldAction action = {0};
    u32 generation = (gMultiplayerWorldMailbox.localGeneration + 1) | 1;

    action.seq = gMultiplayerWorldMailbox.action.seq + 1;
    if (action.seq == 0)
        action.seq = 1;
    action.kind = kind;
    action.result = result;
    action.eventId = eventId;
    action.species = species;
    gMultiplayerWorldMailbox.localGeneration = generation;
    *(struct MultiplayerWorldAction *)&gMultiplayerWorldMailbox.action = action;
    gMultiplayerWorldMailbox.localGeneration = generation + 1;
}

// ---------------------------------------------------------------------------
// Accessors used by other systems

bool32 MultiplayerWorld_HasClock(void)
{
    return sConnected && sHaveInbound && (sInbound.flags & MP_WORLD_FLAG_CLOCK_VALID);
}

u32 MultiplayerWorld_GetSeconds(void)
{
    return sInbound.worldSeconds + WORLD_DAY_ORIGIN * 86400u;
}

const u8 *MultiplayerWorld_GetSelfAppearance(void)
{
    return sHaveInbound ? sInbound.selfAppearance : NULL;
}

static const struct MultiplayerWorldPeer *FindPeer(u32 playerId)
{
    u32 i;
    if (!sHaveInbound || playerId == 0)
        return NULL;
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
    {
        if (sInbound.peers[i].playerId == playerId)
            return &sInbound.peers[i];
    }
    return NULL;
}

const u8 *MultiplayerWorld_GetPeerAppearance(u32 playerId)
{
    const struct MultiplayerWorldPeer *peer = FindPeer(playerId);
    return peer != NULL ? peer->state.appearance : NULL;
}

void MultiplayerWorld_GetPeerMount(u32 playerId, u16 *species, u8 *flags, u8 *state)
{
    const struct MultiplayerWorldPeer *peer = FindPeer(playerId);
    *species = SPECIES_NONE;
    *flags = 0;
    *state = MP_WORLD_MOUNT_NONE;
    if (peer != NULL && peer->state.mountState == MP_WORLD_MOUNT_RIDING
     && peer->state.mountSpecies != SPECIES_NONE && peer->state.mountSpecies < NUM_SPECIES)
    {
        *species = peer->state.mountSpecies;
        *flags = peer->state.mountFlags;
        *state = MP_WORLD_MOUNT_RIDING;
    }
}

void MultiplayerWorld_SetLocalMount(u16 species, u8 flags, bool32 riding)
{
    sLocalMount.mountState = riding ? MP_WORLD_MOUNT_RIDING : MP_WORLD_MOUNT_NONE;
    sLocalMount.mountSpecies = riding ? species : SPECIES_NONE;
    sLocalMount.mountFlags = riding ? flags : 0;
}

bool32 MultiplayerWorld_IsRemoteFollower(const struct ObjectEvent *objectEvent)
{
    u32 i;
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
    {
        u32 id = sFollowers[i].objectEventId;
        if (id < OBJECT_EVENTS_COUNT && objectEvent == &gObjectEvents[id]
         && objectEvent->active && objectEvent->localId == MP_WORLD_LOCAL_ID_FOLLOWER_BASE + i)
            return TRUE;
    }
    return FALSE;
}

bool32 MultiplayerWorld_IsEventObject(const struct ObjectEvent *objectEvent)
{
    return sEventObjectId < OBJECT_EVENTS_COUNT && objectEvent == &gObjectEvents[sEventObjectId]
        && objectEvent->active && objectEvent->localId == MP_WORLD_LOCAL_ID_EVENT;
}

// ---------------------------------------------------------------------------
// Object helpers

static bool32 CanAllocateObject(u16 graphicsId)
{
    u32 i;
    const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(graphicsId);
    if (info == NULL || GetFirstInactiveObjectEventId() == OBJECT_EVENTS_COUNT)
        return FALSE;
    // Never ask the palette allocator to evict a live actor's palette.
    if (IndexOfSpritePaletteTag(info->paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return FALSE;
    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse)
            return TRUE;
    return FALSE;
}

static void RemoveOwnedObject(u8 *objectEventId, u32 localId)
{
    u32 id = *objectEventId;
    if (id < OBJECT_EVENTS_COUNT)
    {
        struct ObjectEvent *objectEvent = &gObjectEvents[id];
        if (objectEvent->localId == localId)
        {
            u32 spriteId = objectEvent->spriteId;
            if (objectEvent->active && spriteId < MAX_SPRITES
             && gSprites[spriteId].inUse && gSprites[spriteId].data[0] == id)
                RemoveObjectEvent(objectEvent);
            ClearObjectEvent(objectEvent);
        }
    }
    *objectEventId = OBJECT_EVENTS_COUNT;
}

static u8 SpawnOwnedObject(u32 localId, u16 graphicsId, s16 mapX, s16 mapY, u8 elevation, u8 movementType, const u8 *script)
{
    struct ObjectEventTemplate template = {0};
    u32 id;

    if (!CanAllocateObject(graphicsId) || GetObjectEventIdByLocalId(localId) < OBJECT_EVENTS_COUNT)
        return OBJECT_EVENTS_COUNT;
    template.localId = localId;
    template.graphicsId = graphicsId;
    template.kind = OBJ_KIND_NORMAL;
    template.x = mapX - MAP_OFFSET;
    template.y = mapY - MAP_OFFSET;
    template.elevation = elevation;
    template.movementType = movementType;
    template.script = script;
    id = SpawnSpecialObjectEvent(&template);
    if (id >= OBJECT_EVENTS_COUNT)
        return OBJECT_EVENTS_COUNT;
    gObjectEvents[id].hideReflection = TRUE;
    MoveObjectEventToMapCoords(&gObjectEvents[id], mapX, mapY);
    return id;
}

static bool32 OnScreen(s32 mapX, s32 mapY)
{
    return mapX >= gSaveBlock1Ptr->pos.x - 2 + MAP_OFFSET && mapX <= gSaveBlock1Ptr->pos.x + 17 + MAP_OFFSET
        && mapY >= gSaveBlock1Ptr->pos.y + MAP_OFFSET && mapY <= gSaveBlock1Ptr->pos.y + 16 + MAP_OFFSET;
}

// ---------------------------------------------------------------------------
// Remote followers (and mounts drawn with the same object)

static void RemoveFollower(u32 i)
{
    RemoveOwnedObject(&sFollowers[i].objectEventId, MP_WORLD_LOCAL_ID_FOLLOWER_BASE + i);
}

static void StepToward(struct ObjectEvent *objectEvent, s16 x, s16 y, u8 movementMode)
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
    if (movementMode == PLAYER_AVATAR_STATE_MACH_BIKE || movementMode == PLAYER_AVATAR_STATE_ACRO_BIKE)
        ObjectEventSetHeldMovement(objectEvent, GetWalkFasterMovementAction(direction));
    else if (movementMode == MULTIPLAYER_MOVEMENT_RUN || abs(dx) + abs(dy) > 1)
        ObjectEventSetHeldMovement(objectEvent, GetWalkFastMovementAction(direction));
    else
        ObjectEventSetHeldMovement(objectEvent, GetWalkNormalMovementAction(direction));
}

static void UpdateFollower(u32 i)
{
    const struct MultiplayerWorldPeer *peer;
    struct ObjectEvent *avatar, *follower;
    u32 avatarId, playerId = Multiplayer_GetRemotePlayerId(i);
    u8 movementMode = Multiplayer_GetRemoteMovementMode(i);
    u16 species, graphicsId;
    bool32 mounted;
    s16 targetX, targetY;

    avatarId = GetObjectEventIdByLocalId(MULTIPLAYER_LOCAL_ID_BASE + i);
    peer = FindPeer(playerId);
    if (peer == NULL || avatarId >= OBJECT_EVENTS_COUNT || !gObjectEvents[avatarId].active)
    {
        RemoveFollower(i);
        return;
    }
    avatar = &gObjectEvents[avatarId];
    mounted = peer->state.mountState == MP_WORLD_MOUNT_RIDING && peer->state.mountSpecies != SPECIES_NONE;
    species = mounted ? peer->state.mountSpecies : peer->state.followerSpecies;
    if (species == SPECIES_NONE || species >= NUM_SPECIES
     || (!mounted && !(peer->state.followerFlags & MP_WORLD_FOLLOWER_VISIBLE))
     || (!mounted && (movementMode == PLAYER_AVATAR_STATE_SURFING || movementMode == PLAYER_AVATAR_STATE_UNDERWATER)))
    {
        RemoveFollower(i);
        return;
    }
    if (mounted)
        graphicsId = GetGraphicsIdForMon(species, peer->state.mountFlags & MP_WORLD_FOLLOWER_SHINY,
                                         peer->state.mountFlags & MP_WORLD_FOLLOWER_FEMALE);
    else
        graphicsId = GetGraphicsIdForMon(species, peer->state.followerFlags & MP_WORLD_FOLLOWER_SHINY,
                                         peer->state.followerFlags & MP_WORLD_FOLLOWER_FEMALE);
    // Large Pokemon wait outside, as for the local follower.
    if (!mounted && gMapHeader.mapType == MAP_TYPE_INDOOR
     && GetObjectEventGraphicsInfo(graphicsId)->oam->size > ST_OAM_SIZE_2)
    {
        RemoveFollower(i);
        return;
    }

    // A mount stands on the rider's tile; a follower walks one tile behind.
    if (mounted)
    {
        targetX = avatar->currentCoords.x;
        targetY = avatar->currentCoords.y;
    }
    else if (avatar->previousCoords.x != avatar->currentCoords.x || avatar->previousCoords.y != avatar->currentCoords.y)
    {
        targetX = avatar->previousCoords.x;
        targetY = avatar->previousCoords.y;
    }
    else
    {
        targetX = 0x7FFF; // idle: stay where it is
        targetY = 0x7FFF;
    }

    follower = sFollowers[i].objectEventId < OBJECT_EVENTS_COUNT ? &gObjectEvents[sFollowers[i].objectEventId] : NULL;
    if (follower == NULL || !MultiplayerWorld_IsRemoteFollower(follower))
    {
        s16 spawnX = avatar->currentCoords.x, spawnY = avatar->currentCoords.y;
        sFollowers[i].objectEventId = OBJECT_EVENTS_COUNT;
        if (!mounted)
        {
            switch (avatar->facingDirection)
            {
            case DIR_NORTH: spawnY++; break;
            case DIR_SOUTH: spawnY--; break;
            case DIR_WEST:  spawnX++; break;
            default:        spawnX--; break;
            }
        }
        sFollowers[i].objectEventId = SpawnOwnedObject(MP_WORLD_LOCAL_ID_FOLLOWER_BASE + i, graphicsId,
            spawnX, spawnY, avatar->currentElevation, MOVEMENT_TYPE_NONE, NULL);
        sFollowers[i].graphicsId = graphicsId;
        if (sFollowers[i].objectEventId >= OBJECT_EVENTS_COUNT)
            return;
        follower = &gObjectEvents[sFollowers[i].objectEventId];
        ObjectEventTurn(follower, avatar->facingDirection);
    }
    if (follower->graphicsId != graphicsId)
    {
        u16 paletteTag = GetObjectEventGraphicsInfo(graphicsId)->paletteTag;
        if (IndexOfSpritePaletteTag(paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        {
            RemoveFollower(i);
            return;
        }
        ObjectEventClearHeldMovementIfActive(follower);
        ObjectEventSetGraphicsId(follower, graphicsId);
        sFollowers[i].graphicsId = graphicsId;
    }
    follower->currentElevation = avatar->currentElevation;
    follower->previousElevation = avatar->currentElevation;
    // A mount is drawn beneath its rider, who sits raised on its back.
    if (mounted)
    {
        const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(graphicsId);
        follower->fixedPriority = TRUE;
        gSprites[follower->spriteId].subpriority = gSprites[avatar->spriteId].subpriority + 1;
        gSprites[avatar->spriteId].y2 = -(info->height >= 64 ? 14 : info->height >= 32 ? 7 : 4);
    }
    else
    {
        follower->fixedPriority = FALSE;
        gSprites[avatar->spriteId].y2 = 0;
    }

    if (ObjectEventClearHeldMovementIfFinished(follower) == TRUE)
        MoveObjectEventToMapCoords(follower, follower->currentCoords.x, follower->currentCoords.y);
    if (ObjectEventIsHeldMovementActive(follower))
        return;
    if (targetX != 0x7FFF)
    {
        if (abs(avatar->currentCoords.x - follower->currentCoords.x) + abs(avatar->currentCoords.y - follower->currentCoords.y) > 3)
            MoveObjectEventToMapCoords(follower, targetX, targetY);
        else
            StepToward(follower, targetX, targetY, movementMode);
    }
    if (!ObjectEventIsHeldMovementActive(follower))
    {
        enum Direction facing = mounted ? avatar->facingDirection : follower->facingDirection;
        if (!mounted)
        {
            s16 dx = avatar->currentCoords.x - follower->currentCoords.x;
            s16 dy = avatar->currentCoords.y - follower->currentCoords.y;
            if (dx || dy)
                facing = dx > 0 ? DIR_EAST : dx < 0 ? DIR_WEST : dy > 0 ? DIR_SOUTH : DIR_NORTH;
        }
        if (follower->facingDirection != facing)
            ObjectEventTurn(follower, facing);
    }
}

// ---------------------------------------------------------------------------
// World event object

static bool32 EventVisibleHere(const struct MultiplayerWorldEvent *event)
{
    return event->eventId != 0 && (event->flags & MP_WORLD_EVENT_VISIBLE)
        && event->species != SPECIES_NONE && event->species < NUM_SPECIES
        && event->mapGroup == gSaveBlock1Ptr->location.mapGroup
        && event->mapNum == gSaveBlock1Ptr->location.mapNum
        && event->x >= 0 && event->y >= 0
        && event->x < gMapHeader.mapLayout->width && event->y < gMapHeader.mapLayout->height
        && OnScreen(event->x + MAP_OFFSET, event->y + MAP_OFFSET);
}

static void UpdateEventObject(void)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    u16 graphicsId;

    if (!sHaveInbound || !EventVisibleHere(event))
    {
        // Keep the object while the local player is battling it (it was
        // already removed by the battle transition) or show nothing.
        RemoveOwnedObject(&sEventObjectId, MP_WORLD_LOCAL_ID_EVENT);
        sEventObjectEventId = 0;
        return;
    }
    graphicsId = GetGraphicsIdForMon(event->species, event->flags & MP_WORLD_EVENT_SHINY, FALSE);
    if (sEventObjectId < OBJECT_EVENTS_COUNT && MultiplayerWorld_IsEventObject(&gObjectEvents[sEventObjectId])
     && sEventObjectEventId == event->eventId && gObjectEvents[sEventObjectId].graphicsId == graphicsId)
        return;
    RemoveOwnedObject(&sEventObjectId, MP_WORLD_LOCAL_ID_EVENT);
    sEventObjectId = SpawnOwnedObject(MP_WORLD_LOCAL_ID_EVENT, graphicsId, event->x + MAP_OFFSET, event->y + MAP_OFFSET,
        MapGridGetElevationAt(event->x + MAP_OFFSET, event->y + MAP_OFFSET), MOVEMENT_TYPE_LOOK_AROUND,
        EventScript_MultiplayerWorldEvent);
    sEventObjectEventId = sEventObjectId < OBJECT_EVENTS_COUNT ? event->eventId : 0;
}

#define tTimer data[0]

static void Task_WaitEventClaim(u8 taskId)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    bool32 done = FALSE;

    if (!sConnected || event->eventId != sClaimEventId || !(event->flags & MP_WORLD_EVENT_VISIBLE))
    {
        gSpecialVar_Result = 0;
        done = TRUE;
    }
    else if (event->flags & MP_WORLD_EVENT_MINE)
    {
        gSpecialVar_Result = 1;
        done = TRUE;
    }
    else if ((event->flags & MP_WORLD_EVENT_BUSY) || ++gTasks[taskId].tTimer > CLAIM_TIMEOUT_FRAMES)
    {
        gSpecialVar_Result = 0;
        done = TRUE;
    }
    if (done)
    {
        DestroyTask(taskId);
        ScriptContext_Enable();
    }
}

#undef tTimer

void MultiplayerWorld_TryClaimEvent(void)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;

    gSpecialVar_Result = 0;
    sClaimEventId = 0;
    if (!sConnected || !sHaveInbound || event->eventId == 0 || !(event->flags & MP_WORLD_EVENT_VISIBLE)
     || (event->flags & MP_WORLD_EVENT_BUSY))
        return;
    if (event->flags & MP_WORLD_EVENT_MINE)
    {
        sClaimEventId = event->eventId;
        gSpecialVar_Result = 1;
        return;
    }
    if (ActionPending())
        return;
    sClaimEventId = event->eventId;
    PostAction(MP_WORLD_ACTION_CLAIM, 0, event->eventId, event->species);
    CreateTask(Task_WaitEventClaim, 80);
    ScriptContext_Stop();
}

static bool32 IsLegendaryEventSpecies(u16 species)
{
    return gSpeciesInfo[species].isRestrictedLegendary || gSpeciesInfo[species].isSubLegendary
        || gSpeciesInfo[species].isMythical || gSpeciesInfo[species].isMegaEvolution;
}

void MultiplayerWorld_StartEventBattle(void)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];
    bool32 shiny = (event->flags & MP_WORLD_EVENT_SHINY) ? TRUE : FALSE;
    u32 level = event->level, i, perfect, personality;
    u16 heldItem = event->heldItem;
    u8 ivStat[NUM_STATS];

    if (event->eventId == 0 || event->eventId != sClaimEventId || event->species == SPECIES_NONE)
    {
        ScriptContext_Enable();
        return;
    }
    if (level < MIN_LEVEL)
        level = MIN_LEVEL;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    ZeroEnemyPartyMons();
    personality = GetMonPersonality(event->species, MON_GENDER_RANDOM, NATURE_RANDOM, RANDOM_UNOWN_LETTER);
    CreateMonWithIVs(mon, event->species, level, personality, OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
    // Event Pokemon get guaranteed perfect IVs like their main-series counterparts.
    for (i = 0; i < NUM_STATS; i++)
        ivStat[i] = i;
    for (i = 0; i < NUM_STATS; i++)
    {
        u32 j = Random() % NUM_STATS, t = ivStat[i];
        ivStat[i] = ivStat[j];
        ivStat[j] = t;
    }
    perfect = event->perfectIvs > NUM_STATS ? NUM_STATS : event->perfectIvs;
    for (i = 0; i < perfect; i++)
    {
        u32 iv = MAX_PER_STAT_IVS;
        SetMonData(mon, MON_DATA_HP_IV + ivStat[i], &iv);
    }
    SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
    if (heldItem != ITEM_NONE && heldItem < ITEMS_COUNT)
        SetMonData(mon, MON_DATA_HELD_ITEM, &heldItem);
    CalculateMonStats(mon);
    GiveMonInitialMoveset(mon);
    sBattlePersonality = GetMonData(mon, MON_DATA_PERSONALITY);
    sBattleSpecies = event->species;
    if (IsLegendaryEventSpecies(event->species))
        BattleSetup_StartLegendaryBattle();
    else
        BattleSetup_StartScriptedWildBattle();
}

static void RevertCaughtMega(void)
{
    u32 i, box, pos;
    u16 base = GetFormSpeciesId(sBattleSpecies, 0);

    if (!gSpeciesInfo[sBattleSpecies].isMegaEvolution || base == SPECIES_NONE || base == sBattleSpecies)
        return;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        if (GetMonData(mon, MON_DATA_SPECIES) == sBattleSpecies && GetMonData(mon, MON_DATA_PERSONALITY) == sBattlePersonality)
        {
            SetMonData(mon, MON_DATA_SPECIES, &base);
            CalculateMonStats(mon);
            return;
        }
    }
    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
    {
        for (pos = 0; pos < IN_BOX_COUNT; pos++)
        {
            struct BoxPokemon *mon = GetBoxedMonPtr(box, pos);
            if (GetBoxMonData(mon, MON_DATA_SPECIES) == sBattleSpecies && GetBoxMonData(mon, MON_DATA_PERSONALITY) == sBattlePersonality)
            {
                SetBoxMonData(mon, MON_DATA_SPECIES, &base);
                return;
            }
        }
    }
}

void MultiplayerWorld_ReportEventResult(void)
{
    u8 result;

    switch (gBattleOutcome)
    {
    case B_OUTCOME_CAUGHT:
        result = MP_WORLD_RESULT_CAUGHT;
        RevertCaughtMega();
        break;
    case B_OUTCOME_WON:
        result = MP_WORLD_RESULT_DEFEATED;
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
    if (sClaimEventId != 0)
        PostAction(MP_WORLD_ACTION_RESULT, result, sClaimEventId, sBattleSpecies);
    gSpecialVar_Result = result;
    sClaimEventId = 0;
}

// ---------------------------------------------------------------------------
// Notices

static const u8 sAsciiToGame[96] =
{
    // ' ' ... '/'
    0x00, 0xAB, 0xB2, 0x00, 0x00, 0x5B, 0x2D, 0xB4, 0x5C, 0x5D, 0x00, 0x2E, 0xB8, 0xAE, 0xAD, 0xBA,
    // '0' ... '?'
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xF0, 0x00, 0x00, 0x35, 0x00, 0xAC,
    // '@' 'A' ... 'O'
    0x00, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    // 'P' ... '_'
    0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0x00, 0x00, 0x00, 0x00, 0xAE,
    // '`' 'a' ... 'o'
    0x00, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
    // 'p' ... '~'
    0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void ConvertNotice(u8 *dest, const u8 *ascii)
{
    u32 i;
    for (i = 0; i < MP_WORLD_NOTICE_CHARS && ascii[i] != 0; i++)
    {
        u8 c = ascii[i];
        dest[i] = (c >= 32 && c < 128) ? sAsciiToGame[c - 32] : 0x00;
    }
    dest[i] = EOS;
}

#define tTimer   data[0]
#define tWindow  data[1]

static void CloseNotice(u8 taskId)
{
    u8 windowId = gTasks[taskId].tWindow;
    if (windowId != WINDOW_NONE)
    {
        ClearWindowTilemap(windowId);
        CopyWindowToVram(windowId, COPYWIN_MAP);
        RemoveWindow(windowId);
    }
    DestroyTask(taskId);
    sNoticeTaskId = TASK_NONE;
}

static void Task_WorldNotice(u8 taskId)
{
    if (++gTasks[taskId].tTimer > NOTICE_FRAMES || gMain.callback2 != CB2_Overworld)
        CloseNotice(taskId);
}

static void ShowNotice(const u8 *ascii)
{
    u8 text[MP_WORLD_NOTICE_CHARS + 1];
    u8 windowId;

    if (sNoticeTaskId != TASK_NONE && FuncIsActiveTask(Task_WorldNotice))
        CloseNotice(sNoticeTaskId);
    {
        struct WindowTemplate template = {
            .bg = 0, .tilemapLeft = 0, .tilemapTop = 0, .width = 30, .height = 2,
            .paletteNum = 15, .baseBlock = NOTICE_WINDOW_BASE_BLOCK,
        };
        windowId = AddWindow(&template);
    }
    if (windowId == WINDOW_NONE)
        return;
    ConvertNotice(text, ascii);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    AddTextPrinterParameterized(windowId, FONT_SMALL, text, 4, 1, TEXT_SKIP_DRAW, NULL);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
    sNoticeTaskId = CreateTask(Task_WorldNotice, 90);
    gTasks[sNoticeTaskId].tTimer = 0;
    gTasks[sNoticeTaskId].tWindow = windowId;
}

#undef tTimer
#undef tWindow

static void UpdateNotice(bool32 fieldReady)
{
    u8 text[MP_WORLD_NOTICE_CHARS + 1];
    if (!sHaveInbound || sInbound.notice.noticeId == 0 || sInbound.notice.noticeId == sShownNoticeId)
        return;
    if (!fieldReady)
        return;
    memcpy(text, sInbound.notice.text, MP_WORLD_NOTICE_CHARS);
    text[MP_WORLD_NOTICE_CHARS] = 0;
    sShownNoticeId = sInbound.notice.noticeId;
    ShowNotice(text);
}

// ---------------------------------------------------------------------------

void MultiplayerWorld_LeaveField(void)
{
    u32 i;
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        RemoveFollower(i);
    RemoveOwnedObject(&sEventObjectId, MP_WORLD_LOCAL_ID_EVENT);
    sEventObjectEventId = 0;
}

void MultiplayerWorld_Tick(bool32 fieldReady, bool32 connected)
{
    u32 i;

    if (gMultiplayerWorldMailbox.magic != MP_WORLD_MAGIC)
        return;
    sConnected = connected;
    ReadInbound();
    if (!connected)
    {
        sHaveInbound = FALSE;
        memset(&sInbound, 0, sizeof(sInbound));
    }
    PublishLocalState();
    if (!fieldReady || !connected)
    {
        MultiplayerWorld_LeaveField();
        return;
    }
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        UpdateFollower(i);
    UpdateEventObject();
    UpdateNotice(fieldReady);
}
