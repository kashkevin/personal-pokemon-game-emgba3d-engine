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
    u8 movementMode; // PLAYER_AVATAR_STATE_NORMAL through UNDERWATER (0..4).
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
void Multiplayer_Tick(void);
void Multiplayer_LeaveField(void);
void Multiplayer_BeginMapTransition(void);
void Multiplayer_PrepareSave(void);
bool32 Multiplayer_IsRemoteObject(const struct ObjectEvent *objectEvent);

#endif
