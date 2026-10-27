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
//  * notices: short host messages shown in an overworld pop-up;
//  * wallet: PvP wager money moves through numbered per-save adjustments.

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
// The world clock's day 0 is the game's RTC day 1000 (multiplayer_world.c), so
// day-based values a connected game wrote from it are 1000 or more.
#define MP_WORLD_DAY_ORIGIN             1000

#define MP_WORLD_FOLLOWER_SHINY     (1 << 0)
#define MP_WORLD_FOLLOWER_FEMALE    (1 << 1)
#define MP_WORLD_FOLLOWER_VISIBLE   (1 << 2)

#define MP_WORLD_MOUNT_NONE         0
#define MP_WORLD_MOUNT_RIDING       1
#ifdef PORTABLE
// Surfing on the Pokemon that used Surf (field_carrier.c): its species and its shiny / female flags, as a mount's
#define MP_WORLD_MOUNT_CARRIER      2
#endif

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
#define MP_WORLD_ACTION_APPEARANCE  4 // the local state carries a newly saved look
#define MP_WORLD_ACTION_WALLET      5 // eventId: wallet sequence applied, amount: money moved
#define MP_WORLD_ACTION_COOP_GYM    6 // eventId: MP_COOP_GYM_* the player asked to challenge with the teammate

#define MP_WORLD_WALLET_RECEIPT     0 // re-sent receipt of the save's last adjustment
#define MP_WORLD_WALLET_CREDIT      1
#define MP_WORLD_WALLET_DEBIT       2
#ifdef PORTABLE
// Another player surfing on their carrier (MP_WORLD_MOUNT_CARRIER): its species and flags. FALSE: not surfing on one.
bool32 MultiplayerWorld_GetPeerCarrier(u32 playerId, u16 *species, u8 *flags);
// A wallet delta above MAX_MONEY with this flag is a Frontier reward (src/multiplayer_world.c).
#define MP_REWARD_FLAG              0x40000000
#define MP_REWARD_BP                1
#define MP_REWARD_ITEM              2
#define MP_REWARD_BP_DEBIT          3
#define MP_REWARD_CATCH_KEEP        4 // payload = event number (23 bits): the host confirms this save's World Event catch
#define MP_REWARD_CATCH_RELEASE     5 // ... or says another trainer caught it first: the duplicate is released
// Battle Tower: Legends (tower_legends.h): reward kinds 6 (the record) and 7 (a milestone Pokemon), and world action 7
// (a Tower battle's result: eventId the ticket, species the floor).
#define MP_WORLD_ACTION_TOWER       7
bool32 MultiplayerWorld_PostTowerAction(u8 result, u32 ticket, u16 floor);
bool32 MultiplayerWorld_IsRewardWord(s32 delta);
u32 MultiplayerWorld_ApplyRewardWord(s32 delta, u8 *result);
// --selftest-catch-reconcile
void MultiplayerWorld_TestRecordCatch(u32 eventId, u16 species, u32 personality);
bool32 MultiplayerWorld_TestHasCatchRecord(void);
#endif

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
    u8 walletSeqLow; // local record only: low byte of VAR_MP_WALLET_SEQ (zero for peers)
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
    u32 amount; // MP_WORLD_ACTION_WALLET only
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
    // Wager wallet: the next adjustment for this save (sequence zero = none).
    // Applied once when walletSeq == VAR_MP_WALLET_SEQ + 1.
    u32 walletSeq;
    s32 walletDelta;
    u32 teammateToken; // peer playerId of this player's host-formed teammate (0 = none)
    u8 reserved[16];
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
bool32 MultiplayerWorld_GetRemoteRider(const struct ObjectEvent *avatar, u8 *lift, struct ObjectEvent **mount);
#ifdef PORTABLE
// Another player's seat on their Pokemon, 0 to MP_RIDE_RAMP_FRAMES: rising after their hop on (once the Pokemon
// stands under them), coming down over their hop off; and that Pokemon. 0 for anybody else.
u32 MultiplayerWorld_GetRemoteSeat(const struct ObjectEvent *avatar, struct ObjectEvent **mount);
bool32 MultiplayerWorld_IsRemoteSeated(u32 remoteIndex);   // that player sits on their Pokemon now (their look)
#endif

// Riding (field_player_avatar.c owns the local mount behaviour).
void MultiplayerWorld_SetLocalMount(u16 species, u8 flags, bool32 riding);

// Co-op Gym support (multiplayer_coop.c).
u32 MultiplayerWorld_GetTeammateToken(void);
bool32 MultiplayerWorld_IsActionPending(void);
bool32 MultiplayerWorld_PostCoopRequest(u32 gym);

// Appearance editor support (multiplayer_appearance.c).
bool32 MultiplayerWorld_PostAppearanceSaved(void);
bool32 MultiplayerWorld_ShowLocalNotice(const char *ascii);

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
