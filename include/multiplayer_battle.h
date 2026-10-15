#ifndef GUARD_MULTIPLAYER_BATTLE_H
#define GUARD_MULTIPLAYER_BATTLE_H

// Opt-in development diagnostic/native singles/full-party Multi channel.
#define MULTIPLAYER_BATTLE_MAGIC 0x31424D45
#define MULTIPLAYER_BATTLE_ABI 3
#define MULTIPLAYER_BATTLE_RESUME_ABI 1
#define MULTIPLAYER_BATTLE_ADMISSION_CLEANUP_ABI 1
// Optional co-op Gym phase: two trainers on one side against a Gym Leader and
// its partner, both run by the master core's AI (Emerald's tower link-multi
// controller layout). Parties are restored exactly afterwards, like PvP.
#define MULTIPLAYER_BATTLE_COOP_ABI 1
#define MULTIPLAYER_BATTLE_FFA_ABI 1
#define MULTIPLAYER_BATTLE_SIZE 512
#define MULTIPLAYER_BATTLE_BLOCK_SIZE 256
#define MULTIPLAYER_BATTLE_CHUNK_SIZE 128
#define MULTIPLAYER_BATTLE_DIAGNOSTIC_MAGIC 0x44424D45
#define MULTIPLAYER_BATTLE_DEADLINE 600

enum MultiplayerBattleOpcode {
    MP_BATTLE_BIND = 1, MP_BATTLE_QUERY, MP_BATTLE_EMIT_DIAGNOSTIC,
    MP_BATTLE_READ_OUT, MP_BATTLE_ACK_OUT, MP_BATTLE_RECEIVE,
    MP_BATTLE_CLEAR_RX, MP_BATTLE_CLOSE, MP_BATTLE_KEEP,
    MP_BATTLE_PREPARE_SINGLES, MP_BATTLE_START_SINGLES,
    MP_BATTLE_ABORT_PRE_START, MP_BATTLE_RELEASE_SINGLES,
    MP_BATTLE_ARM_RESUME, MP_BATTLE_SUSPEND, MP_BATTLE_REAUTHORIZE, MP_BATTLE_RESUME, MP_BATTLE_RESUME_QUERY, MP_BATTLE_ADMIT, MP_BATTLE_RETIRE, MP_BATTLE_HANDOFF,
    MP_BATTLE_ADMISSION_CANCEL_HOLD, MP_BATTLE_ADMISSION_CANCEL_HANDOFF, MP_BATTLE_ADMISSION_CANCEL_READY,
};
enum MultiplayerBattleState {
    MP_BATTLE_UNBOUND, MP_BATTLE_IDLE, MP_BATTLE_CHANNEL, MP_BATTLE_EXPIRED,
    MP_BATTLE_PREPARED, MP_BATTLE_RUNNING, MP_BATTLE_RETURNING,
    MP_BATTLE_FIELD_READY_HELD, MP_BATTLE_RECOVERY_REQUIRED, MP_BATTLE_SUSPENDED, MP_BATTLE_ADMITTING,
};
enum MultiplayerBattlePhase {
    MP_BATTLE_PHASE_DIAGNOSTIC = 1, MP_BATTLE_PHASE_SINGLES, MP_BATTLE_PHASE_FULL_MULTI,
    MP_BATTLE_PHASE_COOP,
    MP_BATTLE_PHASE_FFA, // four independent trainers, one battle (BATTLE_TYPE_FFA); see include/battle_ffa.h
};
enum MultiplayerBattleKind {
    MP_BATTLE_KIND_NONE, MP_BATTLE_KIND_DIAGNOSTIC, MP_BATTLE_KIND_TRAINER,
    MP_BATTLE_KIND_START_HEADER, MP_BATTLE_KIND_PARTY_0, MP_BATTLE_KIND_PARTY_1,
    MP_BATTLE_KIND_PARTY_2, MP_BATTLE_KIND_RNG, MP_BATTLE_KIND_CONTROLLER,
};
enum MultiplayerBattleStatus {
    MP_BATTLE_OK, MP_BATTLE_BUSY, MP_BATTLE_UNSAFE, MP_BATTLE_MALFORMED,
    MP_BATTLE_SESSION, MP_BATTLE_ORDER, MP_BATTLE_CRC, MP_BATTLE_ROLE,
    MP_BATTLE_KIND, MP_BATTLE_TIMEOUT, MP_BATTLE_INTERNAL,
};

struct MultiplayerBattleControl {
    u32 incarnation;
    u32 sessionEpoch;
    u8 battleId[16];
    u32 commandSeq;
    u32 blockSeq;
    u16 offset;
    u16 totalSize;
    u8 opcode;
    u8 status;
    u8 phase;
    u8 sourceSlot;
    u8 kind;
    u8 reserved;
    u16 length;
    u32 chunkCrc32;
    u8 data[192];
};
struct MultiplayerBattleMailbox {
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 frame;
    u32 state;
    u32 reserved[2];
    u32 commandGeneration;
    struct MultiplayerBattleControl command;
    u32 responseGeneration;
    struct MultiplayerBattleControl response;
};
struct MultiplayerBattleBinding {
    u8 generation[16];
    u8 players[4][16];
    u32 leases[4];
    u32 incarnations[4];
    u8 playerCount;
    u8 localSlot;
    u8 mode;
    u8 reserved;
};
struct MultiplayerBattleDiagnostic {
    u32 magic;
    u32 version;
    u8 generation[16];
    u32 sourceSlot;
    u32 token;
    u32 lease;
    u32 incarnation;
};
struct MultiplayerBattlePreparation {
    u32 checkpointRevision;
    u32 nativeCounter;
    u32 trainerId;
    u32 partyCrc32;
    u8 rng[16];
    u32 flags;
};

extern const u32 gMultiplayerBattleAbiVersion;
extern const u32 gMultiplayerBattleResumeAbiVersion;
extern const u32 gMultiplayerBattleAdmissionCleanupAbiVersion;
extern const u32 gMultiplayerBattleCoopAbiVersion;
extern const u32 gMultiplayerBattleFfaAbiVersion; // PC build and test ROM only: four-player free-for-all
extern volatile struct MultiplayerBattleMailbox gMultiplayerBattleMailbox;
extern volatile u32 gMultiplayerBattleHoldFlags;
void MultiplayerBattle_Init(void);
void MultiplayerBattle_Tick(void);
bool32 MultiplayerBattle_IsActive(void);
bool32 MultiplayerBattle_IsAdmissionCleanupHeld(void);
bool32 MultiplayerBattle_CanEnterAdmission(void);
bool32 MultiplayerBattle_PauseClocks(void);
bool32 MultiplayerBattle_SkipCallbacks(void);
bool32 MultiplayerBattle_IsNative(void);
bool32 MultiplayerBattle_IsFullMulti(void);
bool32 MultiplayerBattle_IsCoop(void);
bool32 MultiplayerBattle_IsFreeForAll(void);
bool32 MultiplayerBattle_IsNativeDouble(void);
u32 MultiplayerBattle_GetBattlerCount(void);
u32 MultiplayerBattle_GetBattlerOwner(u32 battler);
u32 MultiplayerBattle_GetCoopGym(void);
bool32 MultiplayerBattle_TrySendOutAnimation(u8 battler);
bool32 MultiplayerBattle_CanReceiveControllers(void);
bool32 MultiplayerBattle_ValidateController(const u8 *data, u16 size, u8 source);
u16 MultiplayerBattle_PackController(u8 *dest, u8 battler, u8 bufferId, u16 size, const u8 *data);
void MultiplayerBattle_FailNative(void);
bool8 MultiplayerBattle_SendPartyPair(u8 index);
bool8 MultiplayerBattle_CopyFullMultiPartyPair(u8 index);
void MultiplayerBattle_InstallNativeRng(void);
void MultiplayerBattle_CreateCoopOpponents(void);
void MultiplayerBattle_SetCoopLinkPlayers(void);
bool32 MultiplayerBattle_TryReturnNative(void);
bool32 MultiplayerBattle_CheckBinding(u32 session, u32 incarnation);
u8 MultiplayerBattle_GetPlayerId(void);
u8 MultiplayerBattle_GetPlayerCount(void);
bool8 MultiplayerBattle_SendBlock(const void *src, u16 size);
bool8 MultiplayerBattle_IsSendFinished(void);
u8 MultiplayerBattle_GetReceivedFlags(void);
void MultiplayerBattle_ResetReceivedFlag(u8 slot);
void MultiplayerBattle_ResetReceivedFlags(void);

#if TESTING
void MultiplayerBattle_TestResetAdmissionCleanup(void);
void MultiplayerBattle_TestSetCoreCount(u8 count);
#endif

#endif
