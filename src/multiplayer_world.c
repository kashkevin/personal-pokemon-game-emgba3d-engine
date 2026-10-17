#include "global.h"
#include <stddef.h>
#include "multiplayer.h"
#include "multiplayer_world.h"
#include "multiplayer_ride.h"
#include "multiplayer_appearance.h"
#ifdef PORTABLE
#include "multiplayer_spawns.h"
#include "multiplayer_extras.h"
#include "multiplayer_rival.h"
#endif
#line 7 // gba-line
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "map_name_popup.h"
#include "malloc.h"
#include "money.h"
#ifdef PORTABLE
#include "item.h"
#include "tv.h"
#include "constants/battle_frontier.h"
#endif
#line 18 // gba-line
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
_Static_assert(sizeof(struct MultiplayerWorldNotice) == 68, "World notice ABI");
_Static_assert(sizeof(struct MultiplayerWorldMailbox) == MP_WORLD_MAILBOX_BYTES, "World mailbox ABI");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, local) == 12, "World local offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, action) == 28, "World action offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, inboundGeneration) == 44, "World inbound offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, flags) == 48, "World flags offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, peers) == 68, "World peers offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, event) == 128, "World event offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, notice) == 160, "World notice offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, walletSeq) == 228, "World wallet offset");
_Static_assert(offsetof(struct MultiplayerWorldMailbox, teammateToken) == 236, "World teammate offset");
_Static_assert(MP_WORLD_LOCAL_ID_FOLLOWER_BASE == MULTIPLAYER_LOCAL_ID_BASE + MULTIPLAYER_MAX_PEERS, "Follower IDs follow avatars");
_Static_assert(MP_WORLD_LOCAL_ID_EVENT < LOCALID_OW_ENCOUNTER_END - OWE_SPAWNS_MAX + 1, "Event ID outside generated encounters");

// Morning of world day 1000: a large day origin keeps day-based timers in old
// saves (berry trees, daily events) moving forward instead of stalling.
#define WORLD_DAY_ORIGIN MP_WORLD_DAY_ORIGIN

#define CLAIM_TIMEOUT_FRAMES 300
#define MP_WORLD_SPRITE_RESERVE 12
#define NOTICE_FRAMES 360
#define NOTICE_WINDOW_BASE_BLOCK 0x220

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
    u32 walletSeq;
    s32 walletDelta;
    u32 teammateToken;
#ifdef PORTABLE
    u8 rival[MP_RIVAL_RECORD_BYTES]; // Update 31: the RivalRecord (reserved[8..15]), copied with the snapshot
#endif
#line 77 // gba-line
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
static EWRAM_DATA bool8 sEventBattleActive = FALSE;
static EWRAM_DATA u8 sEventResult = 0;
static EWRAM_DATA u32 sPendingResultEventId = 0;
static EWRAM_DATA u32 sHiddenEventId = 0; // caught here; hidden until the host drops it
static EWRAM_DATA bool8 sWalletReceiptSent = FALSE; // the save's last receipt, once per connection
static EWRAM_DATA bool8 sWalletNoticePending = FALSE;
static EWRAM_DATA char sWalletNotice[MP_WORLD_NOTICE_CHARS + 1] = {0};

static void Task_WorldNotice(u8 taskId);
static bool32 IsBattleOnlyForm(u16 species);

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
    sEventBattleActive = FALSE;
    sEventResult = 0;
    sPendingResultEventId = 0;
    sHiddenEventId = 0;
    sWalletReceiptSent = FALSE;
    sWalletNoticePending = FALSE;
    gMultiplayerWorldMailbox.abiVersion = MP_WORLD_ABI_VERSION;
    gMultiplayerWorldMailbox.byteSize = sizeof(gMultiplayerWorldMailbox);
    if (*(const volatile u32 *)&gMultiplayerWorldAbiVersion == MP_WORLD_ABI_VERSION)
        gMultiplayerWorldMailbox.magic = MP_WORLD_MAGIC;
#ifdef PORTABLE
    MultiplayerSpawns_Init(); // shared wild Pokemon (native PC only)
    MultiplayerExtras_Init(); // multiplayer Secret Bases (native PC only)
    MultiplayerRival_Init(); // Update 31: rival checkpoints (native PC only)
#endif
#line 138 // gba-line
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
    copy.walletSeq = gMultiplayerWorldMailbox.walletSeq;
    copy.walletDelta = gMultiplayerWorldMailbox.walletDelta;
    copy.teammateToken = gMultiplayerWorldMailbox.teammateToken;
#ifdef PORTABLE
    for (i = 0; i < MP_RIVAL_RECORD_BYTES; i++)
        copy.rival[i] = gMultiplayerWorldMailbox.reserved[MP_RIVAL_RECORD_OFFSET + i];
#endif
#line 160 // gba-line
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
    // The host derives this save's next wallet adjustment from it.
    if (gMain.callback2 == CB2_Overworld)
        state.walletSeqLow = VarGet(VAR_MP_WALLET_SEQ) & 0xFF;
    {
        // The look this player uses: the host's, or a newly saved one the
        // host has not confirmed yet (MP_WORLD_ACTION_APPEARANCE).
        const u8 *look = MultiplayerAppearance_GetPublished();
        for (i = 0; i < MP_WORLD_APPEARANCE_BYTES; i++)
            state.appearance[i] = look[i];
    }

    gMultiplayerWorldMailbox.localGeneration = generation;
    *(struct MultiplayerWorldState *)&gMultiplayerWorldMailbox.local = state;
    gMultiplayerWorldMailbox.localGeneration = generation + 1;
}

static bool32 ActionPending(void)
{
    return gMultiplayerWorldMailbox.action.seq != 0
        && gMultiplayerWorldMailbox.action.seq != sInbound.actionAck;
}

static void PostActionWithAmount(u8 kind, u8 result, u32 eventId, u16 species, u32 amount)
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
    action.amount = amount;
    gMultiplayerWorldMailbox.localGeneration = generation;
    *(struct MultiplayerWorldAction *)&gMultiplayerWorldMailbox.action = action;
    gMultiplayerWorldMailbox.localGeneration = generation + 1;
}

static void PostAction(u8 kind, u8 result, u32 eventId, u16 species)
{
    PostActionWithAmount(kind, result, eventId, species, 0);
}

u32 MultiplayerWorld_GetTeammateToken(void)
{
    return sConnected && sHaveInbound ? sInbound.teammateToken : 0;
}

bool32 MultiplayerWorld_IsActionPending(void)
{
    return ActionPending();
}

bool32 MultiplayerWorld_PostCoopRequest(u32 gym)
{
    if (!sConnected || !sHaveInbound || ActionPending() || gym == 0)
        return FALSE;
    PostAction(MP_WORLD_ACTION_COOP_GYM, 0, gym, 0);
    return TRUE;
}

bool32 MultiplayerWorld_PostAppearanceSaved(void)
{
    if (!sConnected || !sHaveInbound || ActionPending())
        return FALSE;
    PostAction(MP_WORLD_ACTION_APPEARANCE, 0, 0, 0);
    return TRUE;
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

// A remote avatar that is riding: its lift and the mount object drawn under it.
bool32 MultiplayerWorld_GetRemoteRider(const struct ObjectEvent *avatar, u8 *lift, struct ObjectEvent **mount)
{
    const struct MultiplayerWorldPeer *peer;
    u32 i, id;

    if (avatar->localId < MULTIPLAYER_LOCAL_ID_BASE || avatar->localId >= MULTIPLAYER_LOCAL_ID_BASE + MP_WORLD_MAX_PEERS
     || !Multiplayer_IsRemoteObject(avatar))
        return FALSE;
    i = avatar->localId - MULTIPLAYER_LOCAL_ID_BASE;
    peer = FindPeer(Multiplayer_GetRemotePlayerId(i));
    id = sFollowers[i].objectEventId;
    if (peer == NULL || peer->state.mountState != MP_WORLD_MOUNT_RIDING || id >= OBJECT_EVENTS_COUNT
     || !MultiplayerWorld_IsRemoteFollower(&gObjectEvents[id]))
        return FALSE;
    *mount = &gObjectEvents[id];
    *lift = MultiplayerRide_GetLiftForMount(gObjectEvents[id].graphicsId);
    return TRUE;
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
    u32 i, free;
    const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(graphicsId);
    if (info == NULL || GetFirstInactiveObjectEventId() == OBJECT_EVENTS_COUNT)
        return FALSE;
    // Never ask the palette allocator to evict a live actor's palette.
    if (IndexOfSpritePaletteTag(info->paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return FALSE;
    // Keep a reserve for weather, field effects and wild Pokemon: the engine's
    // CreateSprite treats an exhausted sprite table as a fatal error.
    for (i = 0, free = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free >= MP_WORLD_SPRITE_RESERVE)
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
#ifdef PORTABLE
    // The engine's object window (TrySpawnObjectEvents): gSaveBlock1Ptr->pos
    // already is the camera's corner in MAP_OFFSET coordinates. The GBA build
    // keeps its window, shifted by MAP_OFFSET, so its ROM stays byte-identical.
    return mapX >= gSaveBlock1Ptr->pos.x - 2 && mapX <= gSaveBlock1Ptr->pos.x + 17
        && mapY >= gSaveBlock1Ptr->pos.y && mapY <= gSaveBlock1Ptr->pos.y + 16;
#else
#line 416 // gba-line
    return mapX >= gSaveBlock1Ptr->pos.x - 2 + MAP_OFFSET && mapX <= gSaveBlock1Ptr->pos.x + 17 + MAP_OFFSET
        && mapY >= gSaveBlock1Ptr->pos.y + MAP_OFFSET && mapY <= gSaveBlock1Ptr->pos.y + 16 + MAP_OFFSET;
#endif
#line 418 // gba-line
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
    // Pokemon objects use their "faster" animation slots for Poke Ball
    // effects, so bikes are followed at the fast pace (teleport when far).
    if (movementMode == PLAYER_AVATAR_STATE_MACH_BIKE || movementMode == PLAYER_AVATAR_STATE_ACRO_BIKE
     || movementMode == MULTIPLAYER_MOVEMENT_RUN || abs(dx) + abs(dy) > 1)
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
    // A mount is drawn beneath its rider, who sits raised on its back; the
    // lift and draw order are applied by the ride hooks every frame.
    if (mounted)
    {
        follower->fixedPriority = TRUE;
        // Move in lockstep: start the rider's current step on the mount in the
        // same frame, so neither sprite trails the other.
        if (avatar->heldMovementActive && !avatar->heldMovementFinished)
        {
            if (!(follower->heldMovementActive && !follower->heldMovementFinished)
             || follower->movementActionId != avatar->movementActionId)
            {
                ObjectEventForceSetHeldMovement(follower, avatar->movementActionId);
            }
            return;
        }
        ObjectEventClearHeldMovementIfFinished(follower);
        if (!(follower->heldMovementActive && !follower->heldMovementFinished))
        {
            if (follower->currentCoords.x != avatar->currentCoords.x || follower->currentCoords.y != avatar->currentCoords.y)
                MoveObjectEventToMapCoords(follower, avatar->currentCoords.x, avatar->currentCoords.y);
            if (follower->facingDirection != avatar->facingDirection)
                ObjectEventTurn(follower, avatar->facingDirection);
        }
        return;
    }
    follower->fixedPriority = FALSE;

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
    return event->eventId != 0 && event->eventId != sHiddenEventId && (event->flags & MP_WORLD_EVENT_VISIBLE)
        && event->species != SPECIES_NONE && event->species < NUM_SPECIES
        && event->mapGroup == gSaveBlock1Ptr->location.mapGroup
        && event->mapNum == gSaveBlock1Ptr->location.mapNum
        && event->x >= 0 && event->y >= 0
        && event->x < gMapHeader.mapLayout->width && event->y < gMapHeader.mapLayout->height
        && OnScreen(event->x + MAP_OFFSET, event->y + MAP_OFFSET);
}

// Megas added after Gen 6 have no overworld sprite yet: show the base form
// (the banner, battle and capture still use the Mega).
static u16 EventGraphicsSpecies(u16 species)
{
#if OW_POKEMON_OBJECT_EVENTS
    if (IsBattleOnlyForm(species) && gSpeciesInfo[species].overworldData.tileTag == 0)
        return MultiplayerWorld_GetBattleFormBase(species);
#endif
    return species;
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
    graphicsId = GetGraphicsIdForMon(EventGraphicsSpecies(event->species), event->flags & MP_WORLD_EVENT_SHINY, FALSE);
    if (sEventObjectId < OBJECT_EVENTS_COUNT && MultiplayerWorld_IsEventObject(&gObjectEvents[sEventObjectId])
     && sEventObjectEventId == event->eventId && gObjectEvents[sEventObjectId].graphicsId == graphicsId)
        return;
    RemoveOwnedObject(&sEventObjectId, MP_WORLD_LOCAL_ID_EVENT);
    sEventObjectId = SpawnOwnedObject(MP_WORLD_LOCAL_ID_EVENT, graphicsId, event->x + MAP_OFFSET, event->y + MAP_OFFSET,
        MapGridGetElevationAt(event->x + MAP_OFFSET, event->y + MAP_OFFSET), MOVEMENT_TYPE_LOOK_AROUND,
        EventScript_MultiplayerWorldEvent);
    sEventObjectEventId = sEventObjectId < OBJECT_EVENTS_COUNT ? event->eventId : 0;
}

#define tTimer   data[0]
#define tPosted  data[1]

// Every special that is followed by `waitstate` in the event script must
// re-enable the script from a task, never synchronously (waitstate stops the
// script after the special returns).
static void Task_WaitEventClaim(u8 taskId)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    bool32 done = FALSE;

    // VAR_RESULT: 1 granted, 0 another trainer is battling it, 2 unavailable.
    if (!sConnected || !sHaveInbound || sClaimEventId == 0 || event->eventId != sClaimEventId
     || !(event->flags & MP_WORLD_EVENT_VISIBLE))
    {
        gSpecialVar_Result = 2;
        done = TRUE;
    }
    else if (event->flags & MP_WORLD_EVENT_MINE)
    {
        gSpecialVar_Result = 1;
        done = TRUE;
    }
    else if (event->flags & MP_WORLD_EVENT_BUSY)
    {
        gSpecialVar_Result = 0;
        done = TRUE;
    }
    else if (!gTasks[taskId].tPosted)
    {
        // The previous action must be acknowledged before a new one is posted.
        if (!ActionPending())
        {
            PostAction(MP_WORLD_ACTION_CLAIM, 0, event->eventId, event->species);
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
            sClaimEventId = 0;
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

void MultiplayerWorld_TryClaimEvent(void)
{
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    u8 taskId;

#ifdef PORTABLE
    // A shared wild Pokemon uses the same script and messages.
    if (MultiplayerSpawns_TryClaimTalked())
        return;
#endif
#line 719 // gba-line
    gSpecialVar_Result = 2;
    sClaimEventId = 0;
    if (sConnected && sHaveInbound && event->eventId != 0 && event->eventId != sHiddenEventId
     && (event->flags & MP_WORLD_EVENT_VISIBLE) && !(event->flags & MP_WORLD_EVENT_BUSY))
        sClaimEventId = event->eventId;
    taskId = CreateTask(Task_WaitEventClaim, 80);
    if (taskId == TASK_NONE)
    {
        sClaimEventId = 0;
        CreateTask(Task_EnableScript, 80);
    }
    else
    {
        gTasks[taskId].data[0] = 0;
        // Already granted (e.g. after a cancelled message): no new action.
        gTasks[taskId].data[1] = (event->flags & MP_WORLD_EVENT_MINE) ? TRUE : FALSE;
    }
}

static bool32 IsLegendaryEventSpecies(u16 species)
{
    return gSpeciesInfo[species].isRestrictedLegendary || gSpeciesInfo[species].isSubLegendary
        || gSpeciesInfo[species].isMythical || gSpeciesInfo[species].isMegaEvolution
        || gSpeciesInfo[species].isPrimalReversion;
}

static bool32 IsBattleOnlyForm(u16 species)
{
    return species != SPECIES_NONE && species < NUM_SPECIES
        && (gSpeciesInfo[species].isMegaEvolution || gSpeciesInfo[species].isPrimalReversion);
}

// The species whose form-change table turns into this Mega/Primal form
// (Floette-Eternal for Mega Floette, Meowstic-F for Mega Meowstic-F, ...).
u16 MultiplayerWorld_GetBattleFormBase(u16 species)
{
    const u16 *forms;
    u32 i, j;

    if (!IsBattleOnlyForm(species))
        return species;
    forms = gSpeciesInfo[species].formSpeciesIdTable;
    for (i = 0; forms != NULL && forms[i] != FORM_SPECIES_END; i++)
    {
        const struct FormChange *changes;
        if (forms[i] == species || forms[i] >= NUM_SPECIES)
            continue;
        changes = GetSpeciesFormChanges(forms[i]);
        for (j = 0; changes != NULL && changes[j].method != FORM_CHANGE_TERMINATOR; j++)
        {
            if ((changes[j].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM
              || changes[j].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE
              || changes[j].method == FORM_CHANGE_BATTLE_PRIMAL_REVERSION)
             && changes[j].targetSpecies == species)
                return forms[i];
        }
    }
    return GetFormSpeciesId(species, 0);
}

void MultiplayerWorld_StartEventBattle(void)
{
#ifdef PORTABLE
    if (MultiplayerSpawns_InInteraction())
    {
        MultiplayerSpawns_StartBattle();
        return;
    }
#endif
#line 781 // gba-line
    const struct MultiplayerWorldEvent *event = &sInbound.event;
    struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];
    bool32 shiny = (event->flags & MP_WORLD_EVENT_SHINY) ? TRUE : FALSE;
    u32 level = event->level, i, perfect, personality;
    u16 heldItem = event->heldItem;
    u8 ivStat[NUM_STATS];

    sEventBattleActive = FALSE;
    if (!sConnected || event->eventId == 0 || event->eventId != sClaimEventId
     || event->species == SPECIES_NONE || event->species >= NUM_SPECIES)
    {
        sClaimEventId = 0;
        gSpecialVar_Result = 0;
        CreateTask(Task_EnableScript, 80);
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
    sEventResult = 0;
    sEventBattleActive = TRUE;
    gSpecialVar_Result = 1;
    if (IsLegendaryEventSpecies(event->species))
        BattleSetup_StartLegendaryBattle();
    else
        BattleSetup_StartScriptedWildBattle();
}

// Called at the end of BattleStartClearSetData: a World Event Mega/Primal
// battler did not evolve during this battle, so record its original form
// exactly as the engine does for a Mega Evolution. Catching or fainting it
// then reverts it through the engine's own END_BATTLE/FAINT form changes.
void MultiplayerWorld_PrepareEventBattle(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];
    u16 base;

    if (!sEventBattleActive || !IsBattleOnlyForm(sBattleSpecies)
     || GetMonData(mon, MON_DATA_SPECIES) != sBattleSpecies
     || GetMonData(mon, MON_DATA_PERSONALITY) != sBattlePersonality)
        return;
    base = MultiplayerWorld_GetBattleFormBase(sBattleSpecies);
    if (base != SPECIES_NONE && base != sBattleSpecies)
        gBattleStruct->partyState[B_TRAINER_OPPONENT_A][0].changedSpecies = base;
}

// Safety net: a caught battle-only form is stored as its base form.
static void RevertCaughtBattleForm(void)
{
    u32 i, box, pos;
    u16 base = MultiplayerWorld_GetBattleFormBase(sBattleSpecies);

    if (!IsBattleOnlyForm(sBattleSpecies) || base == SPECIES_NONE || base == sBattleSpecies)
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

// CB2_EndScriptedWildBattle runs for every scripted wild battle, including a
// whiteout that never returns to the event script.
void MultiplayerWorld_OnScriptedBattleEnd(void)
{
    u8 result;

#ifdef PORTABLE
    if (MultiplayerSpawns_OnScriptedBattleEnd())
    {
        sEventResult = MultiplayerSpawns_TakeResult();
        return;
    }
#endif
#line 891 // gba-line
    if (!sEventBattleActive)
        return;
    sEventBattleActive = FALSE;
    switch (gBattleOutcome)
    {
    case B_OUTCOME_CAUGHT:
        result = MP_WORLD_RESULT_CAUGHT;
        RevertCaughtBattleForm();
        sHiddenEventId = sClaimEventId;
#ifdef PORTABLE
        {
            // Remember this catch in the save until the host rules on it (Update 17).
            u16 base = MultiplayerWorld_GetBattleFormBase(sBattleSpecies);
            u16 species = (base != SPECIES_NONE && IsBattleOnlyForm(sBattleSpecies)) ? base : sBattleSpecies;

            if (sClaimEventId != 0)
                MultiplayerWorld_TestRecordCatch(sClaimEventId, species, sBattlePersonality);
        }
#endif
#line 900 // gba-line
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
    sEventResult = result;
    if (sClaimEventId != 0)
        sPendingResultEventId = sClaimEventId;
    sClaimEventId = 0;
}

void MultiplayerWorld_ReportEventResult(void)
{
    gSpecialVar_Result = sEventResult;
}

// Results are posted from the field tick so a pending, unacknowledged claim
// never loses the result (the mailbox has one action slot).
static void FlushPendingResult(void)
{
    if (sPendingResultEventId == 0 || ActionPending())
        return;
    PostAction(MP_WORLD_ACTION_RESULT, sEventResult, sPendingResultEventId, sBattleSpecies);
    sPendingResultEventId = 0;
}

// ---------------------------------------------------------------------------
// Notices

static const u8 sAsciiToGame[96] =
{
    // ' ' ... '/' ('$' is the money sign)
    0x00, 0xAB, 0xB2, 0x00, CHAR_CURRENCY, 0x5B, 0x2D, 0xB4, 0x5C, 0x5D, 0x00, 0x2E, 0xB8, 0xAE, 0xAD, 0xBA,
    // '0' ... '?'
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xF0, 0x00, 0x00, 0x35, 0x00, 0xAC,
    // '@' 'A' ... 'O'
    0x00, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    // 'P' ... '_'
    0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0x00, 0x00, 0x00, 0x00, 0xAE,
    // '`' 'a' ... 'o'
    0x00, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
    // 'p' ... '~' ('|' is the line break)
    0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0x00, CHAR_NEWLINE, 0x00, 0x00, 0x00,
};

static void ConvertNotice(u8 *dest, const u8 *ascii)
{
    u32 i, lines = 1;
    for (i = 0; i < MP_WORLD_NOTICE_CHARS && ascii[i] != 0; i++)
    {
        u8 c = ascii[i];
        dest[i] = (c >= 32 && c < 128) ? sAsciiToGame[c - 32] : 0x00;
        if (dest[i] == CHAR_NEWLINE && ++lines > 2)
            dest[i] = 0x00; // at most two lines fit the banner
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
    // The map-name pop-up slides BG0; never share the screen top with it.
    if (++gTasks[taskId].tTimer > NOTICE_FRAMES || gMain.callback2 != CB2_Overworld || IsMapNamePopupShowing())
        CloseNotice(taskId);
}

static bool32 ShowNotice(const u8 *ascii)
{
    u8 text[MP_WORLD_NOTICE_CHARS + 1];
    u8 windowId;

    if (sNoticeTaskId != TASK_NONE && FuncIsActiveTask(Task_WorldNotice))
        CloseNotice(sNoticeTaskId);
    {
        // BG0 tiles 0x220-0x297: above the dialogue frame tiles (0x200-0x21C)
        // and below the end of BG0's character area (0x300).
        struct WindowTemplate template = {
            .bg = 0, .tilemapLeft = 0, .tilemapTop = 0, .width = 30, .height = 4,
            .paletteNum = 15, .baseBlock = NOTICE_WINDOW_BASE_BLOCK,
        };
        windowId = AddWindow(&template);
    }
    if (windowId == WINDOW_NONE)
        return FALSE;
    ConvertNotice(text, ascii);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    AddTextPrinterParameterized(windowId, FONT_SMALL, text, 4, 1, TEXT_SKIP_DRAW, NULL);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
    sNoticeTaskId = CreateTask(Task_WorldNotice, 90);
    if (sNoticeTaskId == TASK_NONE)
    {
        ClearWindowTilemap(windowId);
        CopyWindowToVram(windowId, COPYWIN_MAP);
        RemoveWindow(windowId);
        return FALSE;
    }
    gTasks[sNoticeTaskId].tTimer = 0;
    gTasks[sNoticeTaskId].tWindow = windowId;
    return TRUE;
}

#undef tTimer
#undef tWindow

// A banner from this ROM itself (hints), shown like a host notice.
bool32 MultiplayerWorld_ShowLocalNotice(const char *ascii)
{
    if (gMain.callback2 != CB2_Overworld || IsMapNamePopupShowing() || ArePlayerFieldControlsLocked()
     || (sNoticeTaskId != TASK_NONE && FuncIsActiveTask(Task_WorldNotice)))
        return FALSE;
    return ShowNotice((const u8 *)ascii);
}

static void UpdateNotice(bool32 fieldReady)
{
    u8 text[MP_WORLD_NOTICE_CHARS + 1];
    if (!sHaveInbound || sInbound.notice.noticeId == 0 || sInbound.notice.noticeId == sShownNoticeId)
        return;
    // Wait for a calm overworld: no script, no menu and no map-name pop-up.
    if (!fieldReady || IsMapNamePopupShowing() || ArePlayerFieldControlsLocked())
        return;
    memcpy(text, sInbound.notice.text, MP_WORLD_NOTICE_CHARS);
    text[MP_WORLD_NOTICE_CHARS] = 0;
    if (ShowNotice(text))
        sShownNoticeId = sInbound.notice.noticeId;
}

// ---------------------------------------------------------------------------
// Wager wallet. The host numbers every money movement for each player; the
// save remembers the last one it applied (VAR_MP_WALLET_SEQ), so each moves
// money exactly once per save, and an older reloaded save applies it again
// together with its older money. A debit collects at most the money on hand
// and the receipt tells the host exactly what moved: winners are credited
// only what losers actually paid.

static u32 SavedWalletAmount(void)
{
    return VarGet(VAR_MP_WALLET_AMOUNT_LO) | ((u32)VarGet(VAR_MP_WALLET_AMOUNT_HI) << 16);
}

static char *AppendAscii(char *dest, const char *src)
{
    while (*src != 0)
        *dest++ = *src++;
    return dest;
}

static char *AppendMoneyAscii(char *dest, u32 amount)
{
    char digits[12];
    u32 count = 0, i;

    *dest++ = '$';
    do
    {
        digits[count++] = '0' + amount % 10;
        amount /= 10;
    } while (amount != 0 && count < 9);
    for (i = count; i > 0; i--)
    {
        *dest++ = digits[i - 1];
        if (i > 1 && (i - 1) % 3 == 0)
            *dest++ = ',';
    }
    return dest;
}

#ifdef PORTABLE
// Battle Frontier rewards ride the wager wallet stream (one numbered, exactly-once sequence per save): a delta above
// MAX_MONEY with the reward flag is not money. Bits 24-27 name the kind, the low 24 bits are its payload:
//   1 Battle Points added (payload = points, the save holds at most MAX_BATTLE_FRONTIER_POINTS)
//   2 an item (payload = item id << 8 | quantity 1-99): into the bag, else the PC's item storage
//   3 Battle Points taken (a shop purchase; payload = points asked, at most the points on hand are taken)
// The receipt carries what really moved, so the host never believes a reward that did not land.
bool32 MultiplayerWorld_IsRewardWord(s32 delta)
{
    return delta > MAX_MONEY && ((u32)delta & 0x70000000) == MP_REWARD_FLAG;
}

// ---------------------------------------------------------------------------
// World Event catch reconciliation (Update 17). A catch is written into the save (a few unused game variables) when the
// battle ends, before anything is reported. The game tells the host again and again until the host's verdict arrives on
// the wallet stream: KEEP (this save's catch is the recorded one) or RELEASE (another trainer's report came first, so the
// copy in this save is a duplicate and is released). A record is never guessed at: the Pokemon released is the one with the
// recorded species, personality and this trainer as OT, nothing else.

static u32 CatchRecordEvent(void)
{
    return VarGet(VAR_MP_CATCH_EVENT_LO) | ((u32)VarGet(VAR_MP_CATCH_EVENT_HI) << 16);
}

static u32 CatchRecordPersonality(void)
{
    return VarGet(VAR_MP_CATCH_PID_LO) | ((u32)VarGet(VAR_MP_CATCH_PID_HI) << 16);
}

// The 23-bit number the host's verdict carries for an event (never zero).
static u32 CatchEventNumber(u32 eventId)
{
    return ((eventId - 1) & 0x7FFFFF) + 1;
}

static void ClearCatchRecord(void)
{
    VarSet(VAR_MP_CATCH_EVENT_LO, 0);
    VarSet(VAR_MP_CATCH_EVENT_HI, 0);
    VarSet(VAR_MP_CATCH_SPECIES, 0);
    VarSet(VAR_MP_CATCH_PID_LO, 0);
    VarSet(VAR_MP_CATCH_PID_HI, 0);
}

void MultiplayerWorld_TestRecordCatch(u32 eventId, u16 species, u32 personality)
{
    VarSet(VAR_MP_CATCH_EVENT_LO, eventId & 0xFFFF);
    VarSet(VAR_MP_CATCH_EVENT_HI, eventId >> 16);
    VarSet(VAR_MP_CATCH_SPECIES, species);
    VarSet(VAR_MP_CATCH_PID_LO, personality & 0xFFFF);
    VarSet(VAR_MP_CATCH_PID_HI, personality >> 16);
}

bool32 MultiplayerWorld_TestHasCatchRecord(void)
{
    return CatchRecordEvent() != 0;
}

// Release the recorded Pokemon. TRUE when exactly it was found and released. A party keeps at least one Pokemon.
static bool32 ReleaseRecordedCatch(void)
{
    u32 i, box, pos, personality = CatchRecordPersonality(), otId = GetPlayerIDAsU32();
    u16 species = VarGet(VAR_MP_CATCH_SPECIES);

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) == species && GetMonData(mon, MON_DATA_PERSONALITY) == personality
         && GetMonData(mon, MON_DATA_OT_ID) == otId)
        {
            if (gPartiesCount[B_TRAINER_PLAYER] <= 1)
                return FALSE; // never empties the party
            ZeroMonData(mon);
            CompactPartySlots();
            CalculatePlayerPartyCount();
            return TRUE;
        }
    }
    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
    {
        for (pos = 0; pos < IN_BOX_COUNT; pos++)
        {
            struct BoxPokemon *mon = GetBoxedMonPtr(box, pos);

            if (GetBoxMonData(mon, MON_DATA_SPECIES) == species && GetBoxMonData(mon, MON_DATA_PERSONALITY) == personality
             && GetBoxMonData(mon, MON_DATA_OT_ID) == otId)
            {
                ZeroBoxMonAt(box, pos);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static u32 ApplyCatchVerdict(u32 kind, u32 payload)
{
    u32 eventId = CatchRecordEvent(), amount = 0;

    if (eventId == 0 || payload != CatchEventNumber(eventId))
        return 0; // nothing recorded for that event in this save (an older save, or already settled)
    if (kind == MP_REWARD_CATCH_RELEASE && ReleaseRecordedCatch())
        amount = 1;
    else if (kind == MP_REWARD_CATCH_KEEP)
        amount = 1;
    ClearCatchRecord();
    return amount;
}

static u16 sCatchRepostTimer = 0;

// Until the verdict is applied the record is reported again now and then (a lost report, a reset, a host restart).
static void RepostCatchRecord(void)
{
    u32 eventId = CatchRecordEvent();

    if (eventId == 0 || sPendingResultEventId != 0 || ActionPending())
        return;
    if (sCatchRepostTimer != 0)
    {
        sCatchRepostTimer--;
        return;
    }
    sCatchRepostTimer = 60 * 20;
    PostAction(MP_WORLD_ACTION_RESULT, MP_WORLD_RESULT_CAUGHT, eventId, VarGet(VAR_MP_CATCH_SPECIES));
}

u32 MultiplayerWorld_ApplyRewardWord(s32 delta, u8 *result)
{
    u32 kind = ((u32)delta >> 24) & 0xF, payload = (u32)delta & 0xFFFFFF, amount = 0;
    u16 *points = &gSaveBlock2Ptr->frontier.battlePoints;

    *result = MP_WORLD_WALLET_CREDIT;
    if (kind == MP_REWARD_BP && payload >= 1 && payload <= MAX_BATTLE_FRONTIER_POINTS)
    {
        amount = payload;
        if (*points > MAX_BATTLE_FRONTIER_POINTS)
            *points = MAX_BATTLE_FRONTIER_POINTS;
        if (amount > MAX_BATTLE_FRONTIER_POINTS - *points)
            amount = MAX_BATTLE_FRONTIER_POINTS - *points;
        *points += amount;
    }
    else if (kind == MP_REWARD_BP_DEBIT && payload >= 1 && payload <= MAX_BATTLE_FRONTIER_POINTS)
    {
        amount = payload < *points ? payload : *points;
        *points -= amount;
        *result = MP_WORLD_WALLET_DEBIT;
    }
    else if (kind == MP_REWARD_CATCH_KEEP || kind == MP_REWARD_CATCH_RELEASE)
    {
        amount = ApplyCatchVerdict(kind, payload);
    }
    else if (kind == MP_REWARD_ITEM)
    {
        u32 item = payload >> 8, quantity = payload & 0xFF;

        if (item != ITEM_NONE && item < ITEMS_COUNT && quantity >= 1 && quantity <= 99)
        {
            if (AddBagItem(item, quantity) || AddPCItem(item, quantity))
                amount = quantity;
        }
    }
    return amount;
}
#endif

#line 1091 // gba-line
static void QueueWalletNotice(u8 result, u32 amount)
{
    char *end = sWalletNotice;
    if (amount == 0)
        return;
    end = AppendAscii(end, result == MP_WORLD_WALLET_CREDIT ? "WAGER WINNINGS!|" : "WAGER PAID|");
    end = AppendMoneyAscii(end, amount);
    end = AppendAscii(end, result == MP_WORLD_WALLET_CREDIT ? " added to your money" : " left your money");
    *end = 0;
    sWalletNoticePending = TRUE;
}

static void UpdateWallet(bool32 fieldReady)
{
    u32 applied, seq, money, amount;
    s32 delta;
    u8 result;

    // Only a loaded save in a calm field moves money or reports receipts.
    if (!sHaveInbound || !fieldReady || gMain.callback2 != CB2_Overworld || ActionPending())
        return;
    applied = VarGet(VAR_MP_WALLET_SEQ);
    if (!sWalletReceiptSent)
    {
        // Once per connection the host hears this save's last receipt again,
        // so one lost with a crash or a disconnect still pays the winner.
        sWalletReceiptSent = TRUE;
        if (applied != 0)
        {
            PostActionWithAmount(MP_WORLD_ACTION_WALLET, MP_WORLD_WALLET_RECEIPT, applied, 0, SavedWalletAmount());
            return;
        }
    }
    seq = sInbound.walletSeq;
    delta = sInbound.walletDelta;
#ifdef PORTABLE
    if (seq == 0 || seq > 0xFFFF || seq != ((applied + 1) & 0xFFFF)
     || delta == 0 || (delta > MAX_MONEY && !MultiplayerWorld_IsRewardWord(delta)) || delta < -MAX_MONEY)
        return;
#else
#line 1126 // gba-line
    if (seq == 0 || seq > 0xFFFF || seq != ((applied + 1) & 0xFFFF)
     || delta == 0 || delta > MAX_MONEY || delta < -MAX_MONEY)
        return;
#endif
#line 1129 // gba-line
    money = GetMoney(&gSaveBlock1Ptr->money);
#ifdef PORTABLE
    if (MultiplayerWorld_IsRewardWord(delta))
    {
        amount = MultiplayerWorld_ApplyRewardWord(delta, &result);
        VarSet(VAR_MP_WALLET_SEQ, seq);
        VarSet(VAR_MP_WALLET_AMOUNT_LO, amount & 0xFFFF);
        VarSet(VAR_MP_WALLET_AMOUNT_HI, amount >> 16);
        PostActionWithAmount(MP_WORLD_ACTION_WALLET, result, seq, 0, amount);
        return; // the host's own notice names a Frontier prize
    }
#endif
#line 1130 // gba-line
    if (delta > 0)
    {
        amount = delta;
        if (money >= MAX_MONEY)
            amount = 0;
        else if (amount > MAX_MONEY - money)
            amount = MAX_MONEY - money;
        AddMoney(&gSaveBlock1Ptr->money, amount);
        result = MP_WORLD_WALLET_CREDIT;
    }
    else
    {
        amount = -delta;
        if (amount > money)
            amount = money;
        RemoveMoney(&gSaveBlock1Ptr->money, amount);
        result = MP_WORLD_WALLET_DEBIT;
    }
    VarSet(VAR_MP_WALLET_SEQ, seq);
    VarSet(VAR_MP_WALLET_AMOUNT_LO, amount & 0xFFFF);
    VarSet(VAR_MP_WALLET_AMOUNT_HI, amount >> 16);
    PostActionWithAmount(MP_WORLD_ACTION_WALLET, result, seq, 0, amount);
    QueueWalletNotice(result, amount);
}

// ---------------------------------------------------------------------------

void MultiplayerWorld_LeaveField(void)
{
    u32 i;
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        RemoveFollower(i);
    RemoveOwnedObject(&sEventObjectId, MP_WORLD_LOCAL_ID_EVENT);
    sEventObjectEventId = 0;
#ifdef PORTABLE
    // Shared wild Pokemon are not removed here: this runs whenever a script
    // locks the field, including the one talking to a spawn. Their module
    // removes them itself when the game leaves the overworld or disconnects,
    // and a save never holds them (load_save.c, KNOWN_ISSUES M15).
#endif
#line 1164 // gba-line
}

void MultiplayerWorld_Tick(bool32 fieldReady, bool32 connected)
{
    u32 i;

    if (gMultiplayerWorldMailbox.magic != MP_WORLD_MAGIC)
        return;
#ifdef PORTABLE
    MultiplayerSpawns_Tick(fieldReady, connected);
    MultiplayerExtras_Tick(fieldReady, connected);
#endif
#line 1172 // gba-line
    sConnected = connected;
    ReadInbound();
    if (!connected)
    {
        sHaveInbound = FALSE;
        sWalletReceiptSent = FALSE;
        memset(&sInbound, 0, sizeof(sInbound));
    }
    PublishLocalState();
#ifdef PORTABLE
    MultiplayerRival_Tick(fieldReady, connected, sHaveInbound ? sInbound.rival : NULL); // Update 31
#endif
#line 1181 // gba-line
    if (connected)
    {
#line 1182 // gba-line
        FlushPendingResult();
#ifdef PORTABLE
        RepostCatchRecord();
#endif
    }
#line 1183 // gba-line
    if (sHiddenEventId != 0 && sHaveInbound && sInbound.event.eventId != sHiddenEventId)
        sHiddenEventId = 0;
    if (!connected || gMain.callback2 != CB2_Overworld)
    {
        MultiplayerWorld_LeaveField();
        return;
    }
    if (!fieldReady)
    {
        // A script or menu runs on the overworld: remote avatars are hidden by
        // the presence layer, so their Pokemon go too, but the World Event
        // Pokemon the player may be talking to stays where it is.
        for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
            RemoveFollower(i);
        return;
    }
    for (i = 0; i < MP_WORLD_MAX_PEERS; i++)
        UpdateFollower(i);
    UpdateEventObject();
    UpdateNotice(fieldReady);
    UpdateWallet(fieldReady);
    if (sWalletNoticePending && MultiplayerWorld_ShowLocalNotice(sWalletNotice))
        sWalletNoticePending = FALSE;
}

#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI's banner (src/native_ui/screens/ui_notice.c) draws the
// notice the game shows now, from its window's text; the notice's timing and
// its window stay here.
bool32 MultiplayerWorld_NativeUiNotice(u8 *windowId, u16 *shownFrames, u16 *totalFrames)
{
    if (sNoticeTaskId == TASK_NONE || !FuncIsActiveTask(Task_WorldNotice))
        return FALSE;
    *windowId = gTasks[sNoticeTaskId].data[1];
    *shownFrames = gTasks[sNoticeTaskId].data[0];
    *totalFrames = NOTICE_FRAMES;
    return TRUE;
}
#endif // ENABLE_NATIVE_UI
