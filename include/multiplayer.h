#ifndef GUARD_MULTIPLAYER_H
#define GUARD_MULTIPLAYER_H

// Phase 2 presence only. This volatile mailbox is not part of any save block.
#define MULTIPLAYER_MAGIC 0x31504D45u // EMP1, little endian
#define MULTIPLAYER_ABI_VERSION 1
#define MULTIPLAYER_MAILBOX_BYTES 128
#define MULTIPLAYER_MAX_PEERS 3
#define MULTIPLAYER_LOCAL_ID_BASE 241
#define MULTIPLAYER_CONNECTED (1u << 0)
#define MULTIPLAYER_ACTIVITY_UNAVAILABLE 0
#define MULTIPLAYER_ACTIVITY_OVERWORLD 1
#define MULTIPLAYER_PRESENCE_ANIMATION_ABI 1
#define MULTIPLAYER_MOVEMENT_RUN 5

struct MultiplayerPose
{
    u32 mapEpoch;
    u32 frame;
    s16 x; // Map tile coordinates without MAP_OFFSET.
    s16 y;
    u8 mapGroup;
    u8 mapNum;
    u8 direction;
    u8 elevation;
    u8 gender;
    u8 movementMode; // Avatar states 0..4; animation capability v1 additionally permits RUN=5.
    u8 activity;
    u8 reserved;
};

struct MultiplayerPeer
{
    u32 playerId; // Session identifier; zero means absent.
    struct MultiplayerPose pose;
};

struct MultiplayerMailbox
{
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 frame;
    u32 localGeneration;
    struct MultiplayerPose local;
    // Only the bridge writes the following fields, odd generation first and
    // the corresponding even generation last. ROM accepts a stable even copy.
    u32 inboundGeneration;
    u32 connectionFlags;
    u32 heartbeat;
    u32 rosterEpoch;
    u8 selfSlot;
    u8 peerCount;
    u16 reserved;
    struct MultiplayerPeer peers[MULTIPLAYER_MAX_PEERS];
};

extern volatile struct MultiplayerMailbox gMultiplayerMailbox;

void Multiplayer_Init(void);
bool32 Multiplayer_IsConnected(void);
void Multiplayer_Tick(void);
void Multiplayer_LeaveField(void);
void Multiplayer_BeginMapTransition(void);
void Multiplayer_PrepareSave(void);
bool32 Multiplayer_IsRemoteObject(const struct ObjectEvent *objectEvent);
u32 Multiplayer_GetRemotePlayerId(u32 i);
u8 Multiplayer_GetRemoteMovementMode(u32 i);
bool32 Multiplayer_IsPeerOnCurrentMap(u32 playerId);
#ifdef PORTABLE
// Local ids of the objects the multiplayer layer makes at run time: shared wild
// Pokemon (multiplayer_spawns.h), other players, their followers and the World
// Event Pokemon (multiplayer_world.h). They have no map template and are never
// part of a save.
bool32 Multiplayer_IsRuntimeLocalId(u32 localId);
// The pose of the i-th other player the bridge reports (any map). FALSE: nobody there.
bool32 Multiplayer_GetRemotePose(u32 i, u32 *playerId, struct MultiplayerPose *pose);
#endif

#endif
