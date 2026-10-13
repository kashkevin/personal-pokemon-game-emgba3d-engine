#include "global.h"
#include <stddef.h>
#include "multiplayer.h"
#include "multiplayer_world.h"
#include "multiplayer_ride.h"
#include "multiplayer_appearance.h"
#include "multiplayer_coop.h"
#include "multiplayer_save.h"
#include "multiplayer_battle.h"
#include "multiplayer_menu.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "sprite.h"
#include "constants/event_objects.h"
#ifdef PORTABLE
#include "character_customization.h"
#endif
#line 20 // gba-line

#define HEARTBEAT_TIMEOUT_FRAMES 180
#define MAX_ANIMATED_CATCHUP_TILES 4

_Static_assert(sizeof(struct MultiplayerPose) == 20, "Multiplayer pose ABI");
_Static_assert(sizeof(struct MultiplayerPeer) == 24, "Multiplayer peer ABI");
_Static_assert(sizeof(struct MultiplayerMailbox) == MULTIPLAYER_MAILBOX_BYTES, "Multiplayer mailbox ABI");
_Static_assert(offsetof(struct MultiplayerMailbox, frame) == 8, "Multiplayer frame offset");
_Static_assert(offsetof(struct MultiplayerMailbox, localGeneration) == 12, "Multiplayer local generation offset");
_Static_assert(offsetof(struct MultiplayerMailbox, local) == 16, "Multiplayer local offset");
_Static_assert(offsetof(struct MultiplayerMailbox, inboundGeneration) == 36, "Multiplayer inbound offset");
_Static_assert(offsetof(struct MultiplayerMailbox, connectionFlags) == 40, "Multiplayer connection offset");
_Static_assert(offsetof(struct MultiplayerMailbox, heartbeat) == 44, "Multiplayer heartbeat offset");
_Static_assert(offsetof(struct MultiplayerMailbox, rosterEpoch) == 48, "Multiplayer roster offset");
_Static_assert(offsetof(struct MultiplayerMailbox, selfSlot) == 52, "Multiplayer slot offset");
_Static_assert(offsetof(struct MultiplayerMailbox, peerCount) == 53, "Multiplayer count offset");
_Static_assert(offsetof(struct MultiplayerMailbox, peers) == 56, "Multiplayer peers offset");
_Static_assert(offsetof(struct MultiplayerPose, x) == 8, "Multiplayer x offset");
_Static_assert(offsetof(struct MultiplayerPose, mapGroup) == 12, "Multiplayer map offset");
_Static_assert(offsetof(struct MultiplayerPose, activity) == 18, "Multiplayer activity offset");
_Static_assert(MULTIPLAYER_LOCAL_ID_BASE > OBJECT_EVENT_TEMPLATES_COUNT, "Remote IDs outside map templates");
_Static_assert(MULTIPLAYER_LOCAL_ID_BASE + MULTIPLAYER_MAX_PEERS <= LOCALID_OW_ENCOUNTER_END - OWE_SPAWNS_MAX + 1, "Remote IDs outside generated encounters");

EWRAM_DATA volatile struct MultiplayerMailbox gMultiplayerMailbox = {0};
const u32 gMultiplayerPresenceAnimationAbiVersion = MULTIPLAYER_PRESENCE_ANIMATION_ABI;

// Compiled settings identify this candidate independently of the retained
// baseline. Species IDs/save arrays and pre-existing records are not remapped.
const u32 gMultiplayerGenerationProfile[] =
{
    0x36474445, 1,
    GEN_LATEST, P_LVL_UP_LEARNSETS, P_UPDATED_TYPES, P_UPDATED_STATS,
    P_UPDATED_ABILITIES, P_UPDATED_FRIENDSHIP, P_EVOLUTION_LEVEL_1_LEARN,
    P_FRIENDSHIP_EVO_THRESHOLD, B_UPDATED_MOVE_DATA, B_UPDATED_ABILITY_DATA,
    B_UPDATED_TYPE_MATCHUPS, B_PHYSICAL_SPECIAL_SPLIT, B_CRIT_MULTIPLIER,
    B_PARALYSIS_SPEED, B_CONFUSION_SELF_DMG_CHANCE, B_BURN_DAMAGE,
    B_MEGA_EVO_TURN_ORDER, B_ATE_MULTIPLIER,
};
_Static_assert(sizeof(gMultiplayerGenerationProfile) == 80, "Generation profile ABI");

// Player packages must prove the compiled developer controls are disabled.
const u32 gMultiplayerPlayerBuildGuard[] =
{
    0x31474250, 1, DEBUG_OVERWORLD_MENU, DEBUG_BATTLE_MENU,
    DEBUG_POKEMON_SPRITE_VISUALIZER, I_SHOW_NO_ID_TRAINER,
};
_Static_assert(sizeof(gMultiplayerPlayerBuildGuard) == 24, "Player build guard ABI");


struct RemoteState
{
    struct MultiplayerPeer peer;
    u8 objectEventId;
    bool8 moving;
};

static EWRAM_DATA struct RemoteState sRemotes[MULTIPLAYER_MAX_PEERS] = {0};
static EWRAM_DATA u32 sFrame = 0;
static EWRAM_DATA u32 sMapEpoch = 0;
static EWRAM_DATA u32 sSeenInboundGeneration = 0;
static EWRAM_DATA u32 sHeartbeat = 0;
static EWRAM_DATA u32 sRosterEpoch = 0;
static EWRAM_DATA u32 sLastHeartbeatFrame = 0;
static EWRAM_DATA u32 sSuppressThroughFrame = 0;
static EWRAM_DATA bool8 sConnected = FALSE;

bool32 Multiplayer_IsRemoteObject(const struct ObjectEvent *objectEvent)
{
    u32 i;
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        u32 id = sRemotes[i].objectEventId;
        if (id < OBJECT_EVENTS_COUNT && objectEvent == &gObjectEvents[id]
         && objectEvent->active && objectEvent->localId == MULTIPLAYER_LOCAL_ID_BASE + i)
            return TRUE;
    }
    // Other players' followers and mounts are equally non-blocking.
    return MultiplayerWorld_IsRemoteFollower(objectEvent);
}

u32 Multiplayer_GetRemotePlayerId(u32 i)
{
    return i < MULTIPLAYER_MAX_PEERS ? sRemotes[i].peer.playerId : 0;
}

bool32 Multiplayer_IsPeerOnCurrentMap(u32 playerId)
{
    u32 i;
    if (!sConnected || playerId == 0 || gSaveBlock1Ptr == NULL)
        return FALSE;
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        const struct MultiplayerPeer *peer = &sRemotes[i].peer;
        if (peer->playerId == playerId)
            return peer->pose.activity != MULTIPLAYER_ACTIVITY_UNAVAILABLE
                && peer->pose.mapGroup == gSaveBlock1Ptr->location.mapGroup
                && peer->pose.mapNum == gSaveBlock1Ptr->location.mapNum;
    }
    return FALSE;
}

u8 Multiplayer_GetRemoteMovementMode(u32 i)
{
    return i < MULTIPLAYER_MAX_PEERS ? sRemotes[i].peer.pose.movementMode : 0;
}

static void RemoveRemote(u32 i)
{
    u32 id = sRemotes[i].objectEventId;
    if (id < OBJECT_EVENTS_COUNT)
    {
        struct ObjectEvent *objectEvent = &gObjectEvents[id];
        // A map reset or ordinary off-screen removal may already have recycled
        // the slot. Never touch its replacement, or an unrelated sprite.
        if (objectEvent->localId == MULTIPLAYER_LOCAL_ID_BASE + i)
        {
            u32 spriteId = objectEvent->spriteId;
            if (objectEvent->active && spriteId < MAX_SPRITES
             && gSprites[spriteId].inUse && gSprites[spriteId].data[0] == id)
                RemoveObjectEvent(objectEvent);
            ClearObjectEvent(objectEvent);
        }
    }
    sRemotes[i].objectEventId = OBJECT_EVENTS_COUNT;
    sRemotes[i].moving = FALSE;
}

void Multiplayer_LeaveField(void)
{
    u32 i;
    MultiplayerWorld_LeaveField();
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
        RemoveRemote(i);
}

void Multiplayer_BeginMapTransition(void)
{
    Multiplayer_LeaveField();
    sMapEpoch++;
    if (sMapEpoch == 0)
        sMapEpoch = 1;
    sSuppressThroughFrame = sFrame + 1;
}

void Multiplayer_PrepareSave(void)
{
    // SaveObjectEvents copies all live object slots. Remove ephemeral objects
    // first and prevent the following tick from immediately recreating them.
    Multiplayer_LeaveField();
    sSuppressThroughFrame = sFrame + 1;
}

void Multiplayer_Init(void)
{
    u32 i;
    memset((void *)&gMultiplayerMailbox, 0, sizeof(gMultiplayerMailbox));
    memset(sRemotes, 0, sizeof(sRemotes));
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
        sRemotes[i].objectEventId = OBJECT_EVENTS_COUNT;
    sFrame = 0;
    sMapEpoch = 1;
    sSeenInboundGeneration = 0;
    sHeartbeat = 0;
    sRosterEpoch = 0;
    sLastHeartbeatFrame = 0;
    sSuppressThroughFrame = 0;
    sConnected = FALSE;
    gMultiplayerMailbox.abiVersion = MULTIPLAYER_ABI_VERSION;
    gMultiplayerMailbox.byteSize = sizeof(gMultiplayerMailbox);
    gMultiplayerMailbox.local.mapEpoch = sMapEpoch;
    gMultiplayerMailbox.local.direction = DIR_SOUTH;
    // Keep the descriptor under linker garbage collection, and publish only
    // the declared profile. The host verifies every compiled word from ROM.
    if (((const volatile u32 *)gMultiplayerGenerationProfile)[0] == 0x36474445
     && ((const volatile u32 *)gMultiplayerPlayerBuildGuard)[0] == 0x31474250)
        gMultiplayerMailbox.magic = MULTIPLAYER_MAGIC;
}

static bool32 FieldReady(void)
{
    u32 id = gPlayerAvatar.objectEventId;
    return !MultiplayerMenu_IsActive() && !MultiplayerSave_IsHeld() && !MultiplayerBattle_IsActive() && gMain.callback2 == CB2_Overworld && gMain.callback1 == CB1_Overworld
        && gSaveBlock1Ptr != NULL && gMapHeader.mapLayout != NULL
        && id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active && gObjectEvents[id].isPlayer
        && gObjectEvents[id].spriteId < MAX_SPRITES && gSprites[gObjectEvents[id].spriteId].inUse
        && !gPaletteFade.active && !ArePlayerFieldControlsLocked()
        && (s32)(sFrame - sSuppressThroughFrame) > 0;
}

static u8 MovementMode(void)
{
    if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_UNDERWATER)
        return PLAYER_AVATAR_STATE_UNDERWATER;
    if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_SURFING)
        return PLAYER_AVATAR_STATE_SURFING;
    if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_ACRO_BIKE)
        return PLAYER_AVATAR_STATE_ACRO_BIKE;
    if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_MACH_BIKE)
        return PLAYER_AVATAR_STATE_MACH_BIKE;
    // The actual native run action owns this flag. Do not infer running from
    // network distance or invent a faster local movement rule.
    if ((gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_DASH)
     && *(const volatile u32 *)&gMultiplayerPresenceAnimationAbiVersion == MULTIPLAYER_PRESENCE_ANIMATION_ABI)
        return MULTIPLAYER_MOVEMENT_RUN;
    return PLAYER_AVATAR_STATE_NORMAL;
}

static void PublishLocalPose(bool32 ready)
{
    // Keep a canonical, valid pose even while unavailable. Consumers use the
    // activity flag, rather than interpreting menus as a move to map 0,0.
    struct MultiplayerPose pose = gMultiplayerMailbox.local;
    u32 generation = (gMultiplayerMailbox.localGeneration + 1) | 1;
    if (gSaveBlock1Ptr != NULL)
    {
        u8 mapGroup = gSaveBlock1Ptr->location.mapGroup;
        u8 mapNum = gSaveBlock1Ptr->location.mapNum;
        if (pose.mapGroup != mapGroup || pose.mapNum != mapNum)
        {
            sMapEpoch++;
            if (sMapEpoch == 0)
                sMapEpoch = 1;
        }
        pose.mapGroup = mapGroup;
        pose.mapNum = mapNum;
    }
    pose.mapEpoch = sMapEpoch;
    pose.frame = sFrame;
    pose.activity = MULTIPLAYER_ACTIVITY_UNAVAILABLE;
    if (ready)
    {
        const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
        pose.x = player->currentCoords.x - MAP_OFFSET;
        pose.y = player->currentCoords.y - MAP_OFFSET;
        pose.mapGroup = gSaveBlock1Ptr->location.mapGroup;
        pose.mapNum = gSaveBlock1Ptr->location.mapNum;
        pose.direction = player->facingDirection;
        pose.elevation = player->currentElevation;
        pose.gender = gPlayerAvatar.gender;
        pose.movementMode = MovementMode();
        pose.activity = MULTIPLAYER_ACTIVITY_OVERWORLD;
    }
    gMultiplayerMailbox.localGeneration = generation;
    gMultiplayerMailbox.local = pose;
    gMultiplayerMailbox.localGeneration = generation + 1;
}

static bool32 ValidPose(const struct MultiplayerPose *pose)
{
    if (pose->reserved != 0 || pose->activity > MULTIPLAYER_ACTIVITY_OVERWORLD)
        return FALSE;
    return pose->x >= 0 && pose->y >= 0
        && pose->direction >= DIR_SOUTH && pose->direction <= DIR_EAST
        && pose->elevation <= 15 && pose->gender <= FEMALE
        && pose->movementMode <= MULTIPLAYER_MOVEMENT_RUN;
}

static void ReadInbound(void)
{
    struct MultiplayerPeer peers[MULTIPLAYER_MAX_PEERS];
    u32 generation = gMultiplayerMailbox.inboundGeneration;
    u32 flags, heartbeat, rosterEpoch;
    u32 i, j;
    u8 count, selfSlot;
    u16 reserved;
    if ((generation & 1) || generation == sSeenInboundGeneration)
        return;
    flags = gMultiplayerMailbox.connectionFlags;
    heartbeat = gMultiplayerMailbox.heartbeat;
    rosterEpoch = gMultiplayerMailbox.rosterEpoch;
    selfSlot = gMultiplayerMailbox.selfSlot;
    count = gMultiplayerMailbox.peerCount;
    reserved = gMultiplayerMailbox.reserved;
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
        peers[i] = gMultiplayerMailbox.peers[i];
    if (generation != gMultiplayerMailbox.inboundGeneration)
        return;
    sSeenInboundGeneration = generation;
    if (flags & ~MULTIPLAYER_CONNECTED || count > MULTIPLAYER_MAX_PEERS || selfSlot > 3 || reserved != 0)
        return;
    if (!(flags & MULTIPLAYER_CONNECTED))
    {
        sConnected = FALSE;
        Multiplayer_LeaveField();
        for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
            sRemotes[i].peer.playerId = 0;
        return;
    }
    for (i = 0; i < count; i++)
    {
        if (peers[i].playerId == 0 || !ValidPose(&peers[i].pose))
            return;
        for (j = 0; j < i; j++)
            if (peers[i].playerId == peers[j].playerId)
                return;
    }
    if (!sConnected || heartbeat != sHeartbeat)
        sLastHeartbeatFrame = sFrame;
    if (!sConnected || rosterEpoch != sRosterEpoch)
    {
        Multiplayer_LeaveField();
        for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
            sRemotes[i].peer.playerId = 0;
    }
    sConnected = TRUE;
    sHeartbeat = heartbeat;
    sRosterEpoch = rosterEpoch;
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        if (i >= count)
        {
            RemoveRemote(i);
            sRemotes[i].peer.playerId = 0;
            continue;
        }
        if (sRemotes[i].peer.playerId == peers[i].playerId
         && (s32)(peers[i].pose.frame - sRemotes[i].peer.pose.frame) < 0)
            continue;
        if (sRemotes[i].peer.playerId != peers[i].playerId
         || sRemotes[i].peer.pose.mapEpoch != peers[i].pose.mapEpoch)
            RemoveRemote(i);
        sRemotes[i].peer = peers[i];
    }
}

static bool32 PeerVisible(const struct MultiplayerPose *pose)
{
    s32 x = pose->x + MAP_OFFSET;
    s32 y = pose->y + MAP_OFFSET;
    return pose->activity == MULTIPLAYER_ACTIVITY_OVERWORLD
        && pose->mapGroup == gSaveBlock1Ptr->location.mapGroup
        && pose->mapNum == gSaveBlock1Ptr->location.mapNum
        && pose->x < gMapHeader.mapLayout->width && pose->y < gMapHeader.mapLayout->height
        && x >= gSaveBlock1Ptr->pos.x - 2 && x <= gSaveBlock1Ptr->pos.x + 17
        && y >= gSaveBlock1Ptr->pos.y && y <= gSaveBlock1Ptr->pos.y + 16;
}

static bool32 CanAllocateObject(u16 graphicsId)
{
    u32 i, free;
    u16 paletteTag = GetObjectEventGraphicsInfo(graphicsId)->paletteTag;
    if (GetFirstInactiveObjectEventId() == OBJECT_EVENTS_COUNT)
        return FALSE;
    // The upstream palette allocator can evict generated wild objects. Never
    // request a new palette without a free slot in this presence adapter.
    if (IndexOfSpritePaletteTag(paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return FALSE;
    // Leave sprites for weather and field effects: CreateSprite treats an
    // exhausted table as fatal. Avatars keep priority over world extras.
    for (i = 0, free = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free >= 8)
            return TRUE;
    return FALSE;
}

static void UpdateRemote(u32 i)
{
    const struct MultiplayerPose *pose = &sRemotes[i].peer.pose;
    struct ObjectEvent *objectEvent;
    u32 id = sRemotes[i].objectEventId;
    u16 graphicsId;
    s16 x, y, dx, dy;
    bool32 completed, mounted = FALSE;
    enum Direction direction;
    if (sRemotes[i].peer.playerId == 0 || !PeerVisible(pose))
    {
        RemoveRemote(i);
        return;
    }
    // Normal avatar graphics already include dedicated running frames for both
    // trainers. Reuse their native animation table without another sprite,
    // palette, frame cache, decompression, or save field.
    {
        u16 mountSpecies;
        u8 mountFlags, mountState;
        MultiplayerWorld_GetPeerMount(sRemotes[i].peer.playerId, &mountSpecies, &mountFlags, &mountState);
        // A rider sits like a surfer on the mount drawn beneath (multiplayer_world.c).
        mounted = mountState == MP_WORLD_MOUNT_RIDING && pose->movementMode != PLAYER_AVATAR_STATE_SURFING
               && pose->movementMode != PLAYER_AVATAR_STATE_UNDERWATER;
        if (mounted)
            graphicsId = GetPlayerAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_SURFING, pose->gender);
        else
            graphicsId = GetPlayerAvatarGraphicsIdByStateIdAndGender(
                pose->movementMode == MULTIPLAYER_MOVEMENT_RUN ? PLAYER_AVATAR_STATE_NORMAL : pose->movementMode,
                pose->gender);
    }
#ifdef PORTABLE
    // Every other player shows their own character (character_customization.c).
    graphicsId = CharCustom_RemoteGraphicsId(MultiplayerWorld_GetPeerAppearance(sRemotes[i].peer.playerId), graphicsId);
#endif
#line 406 // gba-line
    if (id >= OBJECT_EVENTS_COUNT || !Multiplayer_IsRemoteObject(&gObjectEvents[id]))
    {
        struct ObjectEventTemplate template = {0};
        sRemotes[i].objectEventId = OBJECT_EVENTS_COUNT;
        sRemotes[i].moving = FALSE;
        if (!CanAllocateObject(graphicsId) || GetObjectEventIdByLocalId(MULTIPLAYER_LOCAL_ID_BASE + i) < OBJECT_EVENTS_COUNT)
            return;
        template.localId = MULTIPLAYER_LOCAL_ID_BASE + i;
        template.graphicsId = graphicsId;
        template.kind = OBJ_KIND_NORMAL;
        template.x = pose->x;
        template.y = pose->y;
        template.elevation = pose->elevation;
        template.movementType = MOVEMENT_TYPE_NONE;
        id = SpawnSpecialObjectEvent(&template);
        if (id >= OBJECT_EVENTS_COUNT)
            return;
        sRemotes[i].objectEventId = id;
        gObjectEvents[id].hideReflection = TRUE;
        // Special-object spawn normally runs during camera refresh. Network
        // spawn may run mid-step; this API also accounts for partial camera
        // pixels, keeping the actor anchored to its actual map tile.
        MoveObjectEventToMapCoords(&gObjectEvents[id], pose->x + MAP_OFFSET, pose->y + MAP_OFFSET);
        ObjectEventTurn(&gObjectEvents[id], pose->direction);
    }
    objectEvent = &gObjectEvents[id];
    x = pose->x + MAP_OFFSET;
    y = pose->y + MAP_OFFSET;
    if (objectEvent->graphicsId != graphicsId)
    {
        u16 paletteTag = GetObjectEventGraphicsInfo(graphicsId)->paletteTag;
        if (IndexOfSpritePaletteTag(paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
            return;
        ObjectEventClearHeldMovementIfActive(objectEvent);
        sRemotes[i].moving = FALSE;
        ObjectEventSetGraphicsId(objectEvent, graphicsId);
        MoveObjectEventToMapCoords(objectEvent, x, y);
    }
    completed = ObjectEventClearHeldMovementIfFinished(objectEvent) == TRUE;
    if (completed)
    {
        MoveObjectEventToMapCoords(objectEvent, objectEvent->currentCoords.x, objectEvent->currentCoords.y);
        completed = sRemotes[i].moving;
        sRemotes[i].moving = FALSE;
    }
    if (ObjectEventIsHeldMovementActive(objectEvent))
        return;
    dx = x - objectEvent->currentCoords.x;
    dy = y - objectEvent->currentCoords.y;
    objectEvent->currentElevation = pose->elevation;
    objectEvent->previousElevation = pose->elevation;
    if (abs(dx) + abs(dy) > MAX_ANIMATED_CATCHUP_TILES)
    {
        MoveObjectEventToMapCoords(objectEvent, x, y);
        ObjectEventTurn(objectEvent, pose->direction);
    }
    else if (dx != 0 || dy != 0)
    {
        direction = dx > 0 ? DIR_EAST : dx < 0 ? DIR_WEST : dy > 0 ? DIR_SOUTH : DIR_NORTH;
        // Riders always move at the standard riding pace; the surfing-pose
        // graphics have no running frames.
        sRemotes[i].moving = !ObjectEventSetHeldMovement(objectEvent, mounted ? GetWalkFastMovementAction(direction)
            : pose->movementMode == MULTIPLAYER_MOVEMENT_RUN ? GetPlayerRunMovementAction(direction)
            : abs(dx) + abs(dy) > 1 ? GetWalkFastMovementAction(direction) : GetWalkNormalMovementAction(direction));
    }
    else if (objectEvent->facingDirection != pose->direction || completed)
    {
        ObjectEventSetHeldMovement(objectEvent, GetFaceDirectionMovementAction(pose->direction));
    }
}

bool32 Multiplayer_IsConnected(void)
{
    return sConnected;
}

void Multiplayer_Tick(void)
{
    u32 i;
    bool32 ready;
    gMultiplayerMailbox.frame = ++sFrame;
    ready = FieldReady();
    MultiplayerRide_Tick(ready);
    MultiplayerAppearance_Tick(sConnected);
    MultiplayerCoop_Tick();
    PublishLocalPose(ready);
    ReadInbound();
    if (sConnected && sFrame - sLastHeartbeatFrame >= HEARTBEAT_TIMEOUT_FRAMES)
        sConnected = FALSE;
    if (!ready || !sConnected)
    {
        Multiplayer_LeaveField();
        MultiplayerWorld_Tick(FALSE, sConnected);
        return;
    }
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
        UpdateRemote(i);
    MultiplayerWorld_Tick(TRUE, sConnected);
}

#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI's link panel (src/native_ui/screens/ui_pause.c): the other
// players of the session as the bridge reports them. FALSE: no player there.
bool32 Multiplayer_NativeUiPeer(u32 i, u32 *playerId, u8 *mapGroup, u8 *mapNum, u8 *gender, u8 *activity)
{
    const struct MultiplayerPeer *peer;

    if (!sConnected || i >= MULTIPLAYER_MAX_PEERS || sRemotes[i].peer.playerId == 0)
        return FALSE;
    peer = &sRemotes[i].peer;
    *playerId = peer->playerId;
    *mapGroup = peer->pose.mapGroup;
    *mapNum = peer->pose.mapNum;
    *gender = peer->pose.gender;
    *activity = peer->pose.activity;
    return TRUE;
}
#endif // ENABLE_NATIVE_UI

#ifdef PORTABLE
#include "multiplayer_spawns.h"

bool32 Multiplayer_IsRuntimeLocalId(u32 localId)
{
    if (localId >= MP_SPAWN_LOCAL_ID_BASE && localId < MP_SPAWN_LOCAL_ID_BASE + MP_SPAWN_MAX_SLOTS)
        return TRUE; // shared wild Pokemon 224-235
    return localId >= MULTIPLAYER_LOCAL_ID_BASE && localId <= MP_WORLD_LOCAL_ID_EVENT; // 241-247
}
#endif // PORTABLE
