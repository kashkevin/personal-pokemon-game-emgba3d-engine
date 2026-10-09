#ifndef GUARD_MULTIPLAYER_WORLD_H
#define GUARD_MULTIPLAYER_WORLD_H

// Shared-world presence extension (ABI EMW1 v1). A separate volatile mailbox
// next to the original EMP1 presence mailbox; never part of any save block.
//
//  * followers: every player's visible follower Pokemon is published and drawn
//    behind that player's avatar on every other client;
//  * mounts: a player riding a Pokemon is drawn mounted on every client;
//  * appearance: customized trainer palette IDs come from the host profile;
//  * world clock: the host's persistent accelerated clock replaces the local
//    RTC for every time-of-day system while connected;
//  * world events: the host's current event Pokemon, its claim state and the
//    player's claim/result actions;
//  * notices: short host messages shown in an overworld pop-up.

#define MP_WORLD_MAGIC              0x31574D45u // EMW1
#define MP_WORLD_ABI_VERSION        1
#define MP_WORLD_MAILBOX_BYTES      256
#define MP_WORLD_MAX_PEERS          3
#define MP_WORLD_APPEARANCE_BYTES   8
#define MP_WORLD_NOTICE_CHARS       64 // two lines split by '|'

// Local object IDs. 241-243 are remote avatars (multiplayer.c), 249-252 are
// generated wild encounters, 254 the local follower and 255 the player.
#define MP_WORLD_LOCAL_ID_FOLLOWER_BASE 244 // 244-246
#define MP_WORLD_LOCAL_ID_EVENT         247

#define MP_WORLD_FOLLOWER_SHINY     (1 << 0)
#define MP_WORLD_FOLLOWER_FEMALE    (1 << 1)
#define MP_WORLD_FOLLOWER_VISIBLE   (1 << 2)

#define MP_WORLD_MOUNT_NONE         0
#define MP_WORLD_MOUNT_RIDING       1

#define MP_WORLD_FLAG_CLOCK_VALID   (1 << 0)
#define MP_WORLD_FLAG_EVENTS        (1 << 1)

#define MP_WORLD_EVENT_SHINY        (1 << 0)
#define MP_WORLD_EVENT_VISIBLE      (1 << 1)
#define MP_WORLD_EVENT_MINE         (1 << 2) // the host granted this player's claim
#define MP_WORLD_EVENT_BUSY         (1 << 3) // another player is battling it
#define MP_WORLD_EVENT_MEGA         (1 << 4)

#define MP_WORLD_ACTION_NONE        0
#define MP_WORLD_ACTION_CLAIM       1
#define MP_WORLD_ACTION_RESULT      2
#define MP_WORLD_ACTION_RELEASE     3

#define MP_WORLD_RESULT_CAUGHT      1
#define MP_WORLD_RESULT_DEFEATED    2
#define MP_WORLD_RESULT_RAN         3
#define MP_WORLD_RESULT_LOST        4
#define MP_WORLD_RESULT_CANCELLED   5

struct MultiplayerWorldState
{
    u16 followerSpecies;
    u8 followerFlags;
    u8 mountState;
    u16 mountSpecies;
    u8 mountFlags;
    u8 reserved;
    u8 appearance[MP_WORLD_APPEARANCE_BYTES];
};

struct MultiplayerWorldPeer
{
    u32 playerId; // equals the EMP1 peer playerId of the same player
    struct MultiplayerWorldState state;
};

struct MultiplayerWorldEvent
{
    u32 eventId; // zero means no event
    u16 species;
    u8 level;
    u8 flags;
    u8 mapGroup;
    u8 mapNum;
    s16 x; // map tile coordinates without MAP_OFFSET
    s16 y;
    u16 heldItem;
    u32 secondsLeft;
    u8 perfectIvs;
    u8 reserved[11];
};

struct MultiplayerWorldAction
{
    u32 seq; // increments for every new action; acknowledged inbound
    u8 kind;
    u8 result;
    u16 species;
    u32 eventId;
    u32 reserved;
};

struct MultiplayerWorldNotice
{
    u32 noticeId; // a changed id shows the text once
    u8 text[MP_WORLD_NOTICE_CHARS]; // printable ASCII, NUL padded; '|' breaks the line
};

struct MultiplayerWorldMailbox
{
    // ROM-owned: odd localGeneration while writing.
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 localGeneration;
    struct MultiplayerWorldState local;
    struct MultiplayerWorldAction action;
    // Bridge-owned from here on: odd inboundGeneration while writing.
    u32 inboundGeneration;
    u32 flags;
    u32 worldSeconds; // seconds since world day 0, 00:00:00
    u32 actionAck;    // last action seq the host has received
    u8 selfAppearance[MP_WORLD_APPEARANCE_BYTES];
    struct MultiplayerWorldPeer peers[MP_WORLD_MAX_PEERS];
    struct MultiplayerWorldEvent event;
    struct MultiplayerWorldNotice notice;
    u8 reserved[28];
};

extern volatile struct MultiplayerWorldMailbox gMultiplayerWorldMailbox;
extern const u32 gMultiplayerWorldAbiVersion;

void MultiplayerWorld_Init(void);
void MultiplayerWorld_Tick(bool32 fieldReady, bool32 connected);
void MultiplayerWorld_LeaveField(void);
bool32 MultiplayerWorld_IsRemoteFollower(const struct ObjectEvent *objectEvent);
bool32 MultiplayerWorld_HasClock(void);
u32 MultiplayerWorld_GetSeconds(void);
const u8 *MultiplayerWorld_GetSelfAppearance(void);
const u8 *MultiplayerWorld_GetPeerAppearance(u32 playerId);
void MultiplayerWorld_GetPeerMount(u32 playerId, u16 *species, u8 *flags, u8 *state);
bool32 MultiplayerWorld_IsEventObject(const struct ObjectEvent *objectEvent);

// Riding (field_player_avatar.c owns the local mount behaviour).
void MultiplayerWorld_SetLocalMount(u16 species, u8 flags, bool32 riding);

// Script specials for the world event object.
void MultiplayerWorld_TryClaimEvent(void);
void MultiplayerWorld_StartEventBattle(void);
void MultiplayerWorld_ReportEventResult(void);

// Battle hooks: a World Event Mega/Primal starts in its battle form and must
// revert like a Mega Evolved battler; the result is reported from the one
// exit every scripted wild battle shares (also on a whiteout).
void MultiplayerWorld_PrepareEventBattle(void);
void MultiplayerWorld_OnScriptedBattleEnd(void);
u16 MultiplayerWorld_GetBattleFormBase(u16 species);

#endif // GUARD_MULTIPLAYER_WORLD_H
