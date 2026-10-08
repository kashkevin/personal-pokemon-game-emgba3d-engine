#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_save.h"
#include "multiplayer_menu.h"
#include "multiplayer_ownership.h"
#include "multiplayer.h"
#include "main.h"
#include "link.h"
#include "multiplayer_battle_schema.h"
#include "battle.h"
#include "battle_main.h"
#include "battle_controllers.h"
#include "battle_message.h"
#include "battle_setup.h"
#include "overworld.h"
#include "load_save.h"
#include "pokemon.h"
#include "random.h"
#include "save.h"
#include "palette.h"
#include "script.h"
#include "sprite.h"
#include "sound.h"
#include "constants/songs.h"
#include "constants/hold_effects.h"
#include "constants/characters.h"
#include "constants/form_change_types.h"

_Static_assert(sizeof(struct MultiplayerBattleControl) == 240, "Battle control ABI");
_Static_assert(sizeof(struct MultiplayerBattleMailbox) == 512, "Battle mailbox ABI");
_Static_assert(offsetof(struct MultiplayerBattleMailbox, commandGeneration) == 24, "Battle command commit");
_Static_assert(offsetof(struct MultiplayerBattleMailbox, command) == 28, "Battle command slot");
_Static_assert(offsetof(struct MultiplayerBattleMailbox, responseGeneration) == 268, "Battle response commit");
_Static_assert(offsetof(struct MultiplayerBattleMailbox, response) == 272, "Battle response slot");
_Static_assert(offsetof(struct MultiplayerBattleControl, blockSeq) == 28, "Battle block sequence");
_Static_assert(offsetof(struct MultiplayerBattleControl, offset) == 32, "Battle block offset");
_Static_assert(offsetof(struct MultiplayerBattleControl, opcode) == 36, "Battle opcode");
_Static_assert(offsetof(struct MultiplayerBattleControl, length) == 42, "Battle payload length");
_Static_assert(offsetof(struct MultiplayerBattleControl, chunkCrc32) == 44, "Battle chunk CRC");
_Static_assert(offsetof(struct MultiplayerBattleControl, data) == 48, "Battle payload");
_Static_assert(sizeof(struct MultiplayerBattleBinding) == 116, "Battle binding layout");
_Static_assert(sizeof(struct MultiplayerBattleDiagnostic) == 40, "Typed diagnostic block");
_Static_assert(sizeof(gBlockRecvBuffer[0]) == MULTIPLAYER_BATTLE_BLOCK_SIZE, "Native block ceiling");
_Static_assert(sizeof(struct MultiplayerBattlePreparation) == 36, "Native preparation ABI");
_Static_assert(sizeof(struct LinkPlayer) == 28 && sizeof(struct LinkBattlerHeader) == 32, "Native startup ABI");
_Static_assert(sizeof(rng_value_t) == 16 && sizeof(struct Pokemon) == 100, "Native party/RNG ABI");

const u32 gMultiplayerBattleAbiVersion = MULTIPLAYER_BATTLE_ABI;
const u32 gMultiplayerBattleResumeAbiVersion = MULTIPLAYER_BATTLE_RESUME_ABI;
EWRAM_DATA volatile struct MultiplayerBattleMailbox gMultiplayerBattleMailbox = {0};
// Read-only proof observation; never an authority or command input.
EWRAM_DATA volatile u32 gMultiplayerBattleHoldFlags = 0;
static EWRAM_DATA struct MultiplayerBattleBinding sBinding = {0};
static EWRAM_DATA struct MultiplayerBattleControl sLastCommand = {0};
static EWRAM_DATA struct MultiplayerBattleControl sCachedResponse = {0};
static EWRAM_DATA u8 sBattleId[16] = {0};
static EWRAM_DATA u32 sFrame = 0;
static EWRAM_DATA u32 sSeenGeneration = 0;
static EWRAM_DATA u32 sState = MP_BATTLE_UNBOUND;
static EWRAM_DATA u32 sSession = 0;
static EWRAM_DATA u32 sIncarnation = 0;
static EWRAM_DATA u32 sLastSeq = 0;
static EWRAM_DATA u32 sDeadline = 0;
static EWRAM_DATA bool8 sResumeArmed = FALSE;
static EWRAM_DATA bool8 sReleasedResumable = FALSE;
static EWRAM_DATA bool8 sReleasedWaiting = FALSE;
static EWRAM_DATA bool8 sRetired = FALSE;
static EWRAM_DATA bool8 sRetiredWaiting = FALSE;
static EWRAM_DATA bool8 sReleasedSuspended = FALSE;
static EWRAM_DATA u8 sAdmitRuntime[16] = {0};
static EWRAM_DATA u32 sAdmitPartyCrc = 0;
static EWRAM_DATA u32 sAdmitSaveCounter = 0;
static EWRAM_DATA u8 sAdmitPartyCount = 0;
static EWRAM_DATA u32 sSuspendedState = 0;
static EWRAM_DATA u32 sResumeEpoch = 0;
static EWRAM_DATA u32 sSuspensionEpoch = 0;
static EWRAM_DATA u8 sResumeToken[16] = {0};
static EWRAM_DATA u32 sOutSeq = 0;
static EWRAM_DATA u32 sOutCrc = 0;
static EWRAM_DATA u32 sReceiveSeq[4] = {0};
static EWRAM_DATA u16 sOutSize = 0;
static EWRAM_DATA u16 sReadOffset = 0;
static EWRAM_DATA u8 sOutKind = 0;
static EWRAM_DATA u8 sReceiveFlags = 0;
static EWRAM_DATA bool8 sOutPending = FALSE;
static EWRAM_DATA bool8 sSavedResetDisabled = FALSE;
static EWRAM_DATA struct MultiplayerBattlePreparation sPreparation = {0};
static EWRAM_DATA rng_value_t sPersonalRng = {0};
static EWRAM_DATA rng_value_t sPersonalRng2 = {0};
static EWRAM_DATA MainCallback sSavedCallback = NULL;
static EWRAM_DATA u32 sSavedBattleFlags = 0;
static EWRAM_DATA u32 sOutcome = 0;
static EWRAM_DATA u16 sSavedLinkType = 0;
static EWRAM_DATA u16 sReceiveOffset[4] = {0};
static EWRAM_DATA u16 sReceiveSize[4] = {0};
static EWRAM_DATA u8 sReceiveKind[4] = {0};
static EWRAM_DATA u8 sNextReceiveKind[4] = {0};
static EWRAM_DATA u8 sNextSendKind = 0;
static EWRAM_DATA u8 sSavedWireless = 0;
static EWRAM_DATA u8 sOriginalCount = 0;
static EWRAM_DATA bool8 sPrepared = FALSE;
static EWRAM_DATA bool8 sStarted = FALSE;
static EWRAM_DATA bool8 sRestored = FALSE;
static EWRAM_DATA bool8 sSeedInstalled = FALSE;
static EWRAM_DATA u8 sSendOutAnimationOwner = 0;
static bool32 NativeBlockValid(const void *src, u16 size, u8 kind, u8 source);
static void NoteNativeController(const u8 *data, u8 source);

static u32 Crc32(const u8 *data, u32 size)
{
    u32 crc = 0xFFFFFFFF, i;
    while (size--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static u32 Word(const u8 *data)
{
    return data[0] | data[1] << 8 | data[2] << 16 | (u32)data[3] << 24;
}

static void PutWord(u8 *data, u32 word)
{
    data[0] = word; data[1] = word >> 8; data[2] = word >> 16; data[3] = word >> 24;
}

static bool32 Zero(const u8 *data, u32 length)
{
    while (length--)
        if (*data++)
            return FALSE;
    return TRUE;
}

bool32 MultiplayerBattle_IsActive(void)
{
    return sState >= MP_BATTLE_CHANNEL && sState <= MP_BATTLE_ADMITTING;
}

static void LatchReleasedWait(void)
{
    if (!sReleasedSuspended)
    {
        sSuspensionEpoch = sResumeEpoch;
        sReleasedWaiting = sReleasedSuspended = TRUE;
    }
}

static void ObserveHoldFlags(void)
{
    gMultiplayerBattleHoldFlags = (MultiplayerMenu_IsBattleAdmissionHeld() ? 1 : 0)
        | (sState == MP_BATTLE_ADMITTING ? 2 : 0) | (sState == MP_BATTLE_SUSPENDED ? 4 : 0)
        | (sReleasedWaiting ? 8 : 0) | (sRetiredWaiting ? 16 : 0);
}

bool32 MultiplayerBattle_PauseClocks(void)
{
    // RELEASE may have succeeded locally while its acknowledgment was lost.
    // Freeze that exact terminal field on disconnect; reconnect alone cannot
    // thaw it before the original trainers complete a new quorum.
    if (sReleasedResumable && !Multiplayer_IsConnected()) LatchReleasedWait();
    ObserveHoldFlags();
    return MultiplayerMenu_IsBattleAdmissionHeld() || sReleasedWaiting || sRetiredWaiting || (MultiplayerBattle_IsActive() && sState != MP_BATTLE_RUNNING && sState != MP_BATTLE_RETURNING);
}

bool32 MultiplayerBattle_SkipCallbacks(void) { return MultiplayerBattle_PauseClocks(); }
bool32 MultiplayerBattle_IsFullMulti(void) { return MultiplayerBattle_IsActive() && sState != MP_BATTLE_ADMITTING && sBinding.mode == MP_BATTLE_PHASE_FULL_MULTI; }
bool32 MultiplayerBattle_IsNative(void) { return MultiplayerBattle_IsActive() && sState != MP_BATTLE_ADMITTING && (sBinding.mode == MP_BATTLE_PHASE_SINGLES || sBinding.mode == MP_BATTLE_PHASE_FULL_MULTI); }
// Four full parties keep more status sprites live than cable Multi. Delayed
// controller delivery can overlap a later ball with the earlier ball's entire
// particle burst. Keep every animation, but let its existing completion flags
// free those sprites before another native trainer starts their send-out.
bool32 MultiplayerBattle_TrySendOutAnimation(u8 battler)
{
    if (!MultiplayerBattle_IsFullMulti())
        return TRUE;
    if (battler >= MAX_BATTLERS_COUNT || gBattleSpritesDataPtr == NULL)
        return FALSE;
    if (sSendOutAnimationOwner != battler)
    {
        if ((sSendOutAnimationOwner < MAX_BATTLERS_COUNT
          && gBattleSpritesDataPtr->healthBoxesData[sSendOutAnimationOwner].ballAnimActive)
         || gBattleSpritesDataPtr->animationData->numBallParticles)
            return FALSE;
        sSendOutAnimationOwner = battler;
    }
    return TRUE;
}

bool32 MultiplayerBattle_CanReceiveControllers(void) { return MultiplayerBattle_IsNative() && sState == MP_BATTLE_RUNNING; }
void MultiplayerBattle_FailNative(void)
{
    if (MultiplayerBattle_IsNative()) sState = MP_BATTLE_RECOVERY_REQUIRED;
}

bool32 MultiplayerBattle_CheckBinding(u32 session, u32 incarnation)
{
    return session > sSession || (session == sSession && incarnation >= sIncarnation);
}

u8 MultiplayerBattle_GetPlayerId(void) { return sBinding.localSlot; }
u8 MultiplayerBattle_GetPlayerCount(void) { return sBinding.playerCount; }
bool8 MultiplayerBattle_IsSendFinished(void)
{
    return !sOutPending && (sState == MP_BATTLE_CHANNEL || sState == MP_BATTLE_PREPARED || sState == MP_BATTLE_RUNNING);
}
u8 MultiplayerBattle_GetReceivedFlags(void) { return sReceiveFlags; }

void MultiplayerBattle_ResetReceivedFlag(u8 slot)
{
    if (slot < sBinding.playerCount)
        sReceiveFlags &= ~(1 << slot);
}

void MultiplayerBattle_ResetReceivedFlags(void) { sReceiveFlags = 0; }

static bool32 DiagnosticValid(const struct MultiplayerBattleDiagnostic *block, u32 slot)
{
    return slot < sBinding.playerCount && block->magic == MULTIPLAYER_BATTLE_DIAGNOSTIC_MAGIC
        && block->version == 1 && block->sourceSlot == slot
        && !memcmp(block->generation, sBinding.generation, 16)
        && block->lease == sBinding.leases[slot] && block->incarnation == sBinding.incarnations[slot];
}

bool8 MultiplayerBattle_SendBlock(const void *src, u16 size)
{
    struct MultiplayerBattleDiagnostic block;
    u8 local = sBinding.localSlot;
    u8 kind = MP_BATTLE_KIND_DIAGNOSTIC;
    if (!src || sOutPending || sOutSeq == 0xFFFFFFFF || (sReceiveFlags & (1 << local)))
        return FALSE;
    if (MultiplayerBattle_IsNative())
    {
        if (sState != MP_BATTLE_PREPARED && sState != MP_BATTLE_RUNNING) return FALSE;
        kind = sNextSendKind;
        if (!NativeBlockValid(src, size, kind, local))
        {
            MultiplayerBattle_FailNative(); return FALSE;
        }
    }
    else
    {
        if (sState != MP_BATTLE_CHANNEL || size != sizeof(block)) return FALSE;
        memcpy(&block, src, sizeof(block));
        if (!DiagnosticValid(&block, local)) return FALSE;
    }
    memset(gBlockSendBuffer, 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    memcpy(gBlockSendBuffer, src, size);
    // Native block reception includes the sender. No controller consumes this
    // development frame; native battle entry and dispatch remain disabled.
    memset(gBlockRecvBuffer[local], 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    memcpy(gBlockRecvBuffer[local], src, size);
    sReceiveFlags |= 1 << local;
    sOutSeq++; sOutSize = size; sReadOffset = 0;
    sOutCrc = Crc32(src, size); sOutKind = kind; sOutPending = TRUE;
    if (MultiplayerBattle_IsNative())
    {
        if (kind == MP_BATTLE_KIND_CONTROLLER) NoteNativeController(src, local);
        else sNextSendKind++;
    }
    return TRUE;
}

static void QueryData(u8 *data)
{
    memset(data, 0, 64);
    PutWord(data, MultiplayerBattle_IsActive() | ((sState == MP_BATTLE_EXPIRED || sState == MP_BATTLE_RECOVERY_REQUIRED || sState == MP_BATTLE_SUSPENDED) << 1)
        | sOutPending << 2 | sPrepared << 3 | sStarted << 4 | sRestored << 5
        | (sState == MP_BATTLE_RECOVERY_REQUIRED || sState == MP_BATTLE_SUSPENDED) << 6
        | (sState == MP_BATTLE_ADMITTING) << 7);
    memcpy(data + 4, sBinding.generation, 16);
    data[20] = sBinding.playerCount; data[21] = sBinding.localSlot; data[22] = sBinding.mode;
    PutWord(data + 24, sLastSeq); PutWord(data + 28, sOutSeq);
    PutWord(data + 32, sOutSize ? sOutKind : MP_BATTLE_KIND_NONE);
    PutWord(data + 36, sOutSize); PutWord(data + 40, sOutCrc); PutWord(data + 44, sReadOffset);
    PutWord(data + 48, sReceiveFlags);
    PutWord(data + 52, MultiplayerBattle_IsActive() && sState != MP_BATTLE_EXPIRED && sState != MP_BATTLE_RECOVERY_REQUIRED && sState != MP_BATTLE_SUSPENDED && sState != MP_BATTLE_ADMITTING
        ? max(0, (s32)(sDeadline - sFrame)) : 0);
    PutWord(data + 56, sPreparation.checkpointRevision); PutWord(data + 60, sOutcome);
    if (sReleasedWaiting || sRetiredWaiting) PutWord(data, 1 | (sReleasedWaiting ? 256 : 512));
}

static void Respond(const struct MultiplayerBattleControl *command, u8 status, const u8 *data, u16 length, bool32 cache)
{
    struct MultiplayerBattleControl response = *command;
    u32 generation = (gMultiplayerBattleMailbox.responseGeneration + 1) | 1;
    response.status = status; response.length = length;
    memset(response.data, 0, sizeof(response.data));
    if (length)
        memcpy(response.data, data, length);
    response.chunkCrc32 = Crc32(response.data, length);
    if (cache)
    {
        sLastCommand = *command; sLastSeq = command->commandSeq;
        sCachedResponse = response;
    }
    gMultiplayerBattleMailbox.responseGeneration = generation;
    gMultiplayerBattleMailbox.response = response;
    gMultiplayerBattleMailbox.state = sState;
    gMultiplayerBattleMailbox.responseGeneration = generation + 1;
}

static void Replay(void)
{
    u32 generation = (gMultiplayerBattleMailbox.responseGeneration + 1) | 1;
    gMultiplayerBattleMailbox.responseGeneration = generation;
    gMultiplayerBattleMailbox.response = sCachedResponse;
    gMultiplayerBattleMailbox.state = sState;
    gMultiplayerBattleMailbox.responseGeneration = generation + 1;
}

static bool32 Empty(const struct MultiplayerBattleControl *c)
{
    return !c->blockSeq && !c->offset && !c->totalSize && !c->length && !c->kind;
}

static bool32 BindingValid(const struct MultiplayerBattleBinding *b, const struct MultiplayerBattleControl *c)
{
    u32 i, j;
    if ((b->playerCount != 2 && b->playerCount != 4) || b->localSlot >= b->playerCount
     || b->localSlot != c->sourceSlot || b->mode != c->phase || b->reserved > 1 || (b->reserved && b->mode == MP_BATTLE_PHASE_DIAGNOSTIC)
     || (b->mode == MP_BATTLE_PHASE_SINGLES && b->playerCount != 2)
     || (b->mode == MP_BATTLE_PHASE_FULL_MULTI && b->playerCount != 4)
     || Zero(b->generation, 16))
        return FALSE;
    if (b->reserved && *(const volatile u32 *)&gMultiplayerBattleResumeAbiVersion != MULTIPLAYER_BATTLE_RESUME_ABI)
        return FALSE;
    for (i = 0; i < 4; i++)
    {
        if (i >= b->playerCount)
        {
            if (!Zero(b->players[i], 16) || b->leases[i] || b->incarnations[i])
                return FALSE;
            continue;
        }
        if (Zero(b->players[i], 16) || !b->leases[i] || b->leases[i] > 0x7FFFFFFF || !b->incarnations[i])
            return FALSE;
        for (j = 0; j < i; j++)
            if (!memcmp(b->players[i], b->players[j], 16))
                return FALSE;
    }
    return b->leases[b->localSlot] == c->sessionEpoch && b->incarnations[b->localSlot] == c->incarnation
        && gSaveBlock3Ptr != NULL && !memcmp(b->players[b->localSlot], gSaveBlock3Ptr->ownership.playerId, 16);
}

static void ClearBlocks(void)
{
    u32 i;
    sReceiveFlags = 0; sOutPending = FALSE;
    sOutSeq = 0; sOutSize = 0; sReadOffset = 0; sOutCrc = 0; sOutKind = 0;
    memset(sReceiveSeq, 0, sizeof(sReceiveSeq));
    memset(sReceiveOffset, 0, sizeof(sReceiveOffset)); memset(sReceiveSize, 0, sizeof(sReceiveSize));
    memset(sReceiveKind, 0, sizeof(sReceiveKind));
    memset(sNextReceiveKind, MP_BATTLE_KIND_TRAINER, sizeof(sNextReceiveKind));
    sNextSendKind = MP_BATTLE_KIND_TRAINER;
    sSendOutAnimationOwner = MAX_BATTLERS_COUNT;
    sResumeArmed = FALSE; sRetired = FALSE; sRetiredWaiting = FALSE; sReleasedSuspended = FALSE; memset(sAdmitRuntime, 0, sizeof(sAdmitRuntime)); sReleasedResumable = FALSE; sReleasedWaiting = FALSE; sSuspendedState = 0; sResumeEpoch = 0; sSuspensionEpoch = 0;
    memset(sResumeToken, 0, sizeof(sResumeToken));
    memset(&sPreparation, 0, sizeof(sPreparation));
    sPrepared = sStarted = sRestored = sSeedInstalled = FALSE; sOutcome = 0;
    MultiplayerBattle_ResetControllerValidation();
    memset(gBlockSendBuffer, 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    for (i = 0; i < 4; i++)
        memset(gBlockRecvBuffer[i], 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
}

static u8 Bind(const struct MultiplayerBattleControl *c)
{
    struct MultiplayerBattleBinding binding;
    bool32 admitted = sState == MP_BATTLE_ADMITTING;
    bool32 samePair = c->sessionEpoch == sSession && c->incarnation == sIncarnation;
    if (!MultiplayerBattle_CheckBinding(c->sessionEpoch, c->incarnation)
     || !MultiplayerSave_CheckBinding(c->sessionEpoch, c->incarnation))
        return MP_BATTLE_SESSION;
    if ((!admitted && (MultiplayerBattle_IsActive() || sReleasedResumable || sRetiredWaiting)) || MultiplayerSave_IsHeld())
        return MP_BATTLE_BUSY;
    if ((!admitted && samePair && (sLastSeq == 0xFFFFFFFF || c->commandSeq != sLastSeq + 1 || !memcmp(c->battleId, sBattleId, 16)))
     || (!admitted && !samePair && c->commandSeq != 1)
     || (admitted && (sLastSeq == 0xFFFFFFFF || c->commandSeq != sLastSeq + 1)))
        return MP_BATTLE_ORDER;
    if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != sizeof(binding))
        return MP_BATTLE_MALFORMED;
    memcpy(&binding, c->data, sizeof(binding));
    if (!BindingValid(&binding, c))
        return MP_BATTLE_ROLE;
    if (admitted)
    {
        // The safe field has remained callback/clock held since ADMIT. No save,
        // party mutation, menu, or second native lifecycle can enter this hold.
        if (!binding.reserved || c->incarnation != sIncarnation || c->sessionEpoch < sSession
         || memcmp(c->battleId, sBattleId, 16) || memcmp(binding.generation, sBinding.generation, 16)
         || binding.localSlot != sBinding.localSlot || binding.mode != sBinding.mode
         || memcmp(binding.players[binding.localSlot], sBinding.players[sBinding.localSlot], 16)
         || gSaveCounter != sAdmitSaveCounter || gPartiesCount[B_TRAINER_PLAYER] != sAdmitPartyCount
         || Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600) != sAdmitPartyCrc)
            return MP_BATTLE_SESSION;
    }
    else if (!MultiplayerSave_FieldReady() || !MultiplayerOwnership_IsEnrolled() || !MultiplayerOwnership_CanSaveParty())
        return MP_BATTLE_UNSAFE;
    sSession = c->sessionEpoch; sIncarnation = c->incarnation;
    memcpy(sBattleId, c->battleId, 16); sBinding = binding; ClearBlocks();
    sResumeArmed = binding.reserved == 1;
    if (!admitted) sSavedResetDisabled = gSoftResetDisabled;
    gSoftResetDisabled = TRUE;
    sState = MP_BATTLE_CHANNEL; sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
    Multiplayer_LeaveField();
    return MP_BATTLE_OK;
}

static bool32 NameValid(const u8 *p, u32 size)
{
    while (size--)
    {
        if (*p == EOS) return TRUE;
        if (*p++ >= 0xFC) return FALSE;
    }
    return FALSE;
}

static bool32 PartyMonValid(const u8 *data)
{
    struct Pokemon mon;
    u32 species, serial, i;
    memcpy(&mon, data, sizeof(mon));
    species = GetMonData(&mon, MON_DATA_SPECIES);
    serial = GetMonData(&mon, MON_DATA_MULTIPLAYER_ID);
    if (GetMonData(&mon, MON_DATA_SANITY_IS_BAD_EGG) || species >= NUM_SPECIES) return FALSE;
    if (!species) return !serial;
    if (!serial || serial >= MULTIPLAYER_OWNERSHIP_SERIAL_LIMIT || !mon.level || mon.level > MAX_LEVEL
     || mon.hp > mon.maxHP || GetMonData(&mon, MON_DATA_HELD_ITEM) >= ITEMS_COUNT) return FALSE;
    for (i = 0; i < MAX_MON_MOVES; i++) if (GetMonData(&mon, MON_DATA_MOVE1 + i) >= MOVES_COUNT_ALL) return FALSE;
    return TRUE;
}

static bool32 NativeBlockValid(const void *src, u16 size, u8 kind, u8 source)
{
    const u8 *data = src;
    if (source >= sBinding.playerCount) return FALSE;
    switch (kind)
    {
    case MP_BATTLE_KIND_TRAINER:
    {
        struct LinkPlayer player;
        if (size != sizeof(player)) return FALSE;
        memcpy(&player, src, sizeof(player));
        return player.version == VERSION_EMERALD + 0x4000 && !player.lp_field_2
            && !player.progressFlags && !player.neverRead && !player.progressFlagsCopy
            && player.gender <= 1 && player.linkType == (MultiplayerBattle_IsFullMulti() ? LINKTYPE_MULTI_BATTLE : LINKTYPE_BATTLE) && player.id == source
            && player.language == gGameLanguage && NameValid(player.name, sizeof(player.name));
    }
    case MP_BATTLE_KIND_START_HEADER:
    {
        struct LinkBattlerHeader header;
        if (size != sizeof(header)) return FALSE;
        memcpy(&header, src, sizeof(header));
        return header.versionSignatureLo == 0 && header.versionSignatureHi == 3
            && !(header.vsScreenHealthFlagsHi & 0xF0)
            && header.battleEnigmaBerry.holdEffect < HOLD_EFFECT_COUNT
            && NameValid(header.battleEnigmaBerry.name, sizeof(header.battleEnigmaBerry.name)) && !data[31];
    }
    case MP_BATTLE_KIND_PARTY_0: case MP_BATTLE_KIND_PARTY_1: case MP_BATTLE_KIND_PARTY_2:
        return size == 200 && PartyMonValid(data) && PartyMonValid(data + 100);
    case MP_BATTLE_KIND_RNG:
        return size == 16 && !memcmp(src, sPreparation.rng, 16);
    case MP_BATTLE_KIND_CONTROLLER:
        return MultiplayerBattle_ValidateController(data, size, source);
    default: return FALSE;
    }
}

static void NoteNativeController(const u8 *data, u8 source)
{
    MultiplayerBattle_NoteController(data, source);
    if (data[0] == B_COMM_TO_CONTROLLER && data[8] == CONTROLLER_ENDLINKBATTLE)
        sOutcome = data[9]; // Master slot0's projection is the fixed-side result.
}

u16 MultiplayerBattle_PackController(u8 *dest, u8 battler, u8 bufferId, u16 size, const u8 *data)
{
    u16 padded = size - size % 4 + 4;
    if (!MultiplayerBattle_IsNative() || sState != MP_BATTLE_RUNNING) return 0;
    // Mirrored remote controllers still render and send DONE, but only the
    // trainer owning a battler may supply engine data/choices.
    if (bufferId == B_COMM_TO_ENGINE && battler != sBinding.localSlot) return 0;
    if (battler >= sBinding.playerCount || !data || !size || padded + 8 > MULTIPLAYER_BATTLE_BLOCK_SIZE)
    {
        MultiplayerBattle_FailNative(); return 0;
    }
    memset(dest, 0, padded + 8);
    dest[0] = bufferId; dest[1] = battler; dest[2] = gBattlerAttacker; dest[3] = gBattlerTarget;
    dest[4] = padded; dest[5] = padded >> 8; dest[6] = gAbsentBattlerFlags; dest[7] = gEffectBattler;
    // Replies/DONE never consume these auxiliary globals. Native loops may
    // leave a past-the-end effect index before an empty print or selection UI;
    // these audited handlers do not consume it either. Meaningful print and
    // animation metadata must still pass the strict incoming index bounds.
    if (bufferId == B_COMM_TO_ENGINE || bufferId == B_COMM_CONTROLLER_IS_DONE)
        dest[2] = dest[3] = dest[6] = dest[7] = 0;
    else if (dest[7] >= sBinding.playerCount && (data[0] == CONTROLLER_CHOOSEACTION || data[0] == CONTROLLER_CHOOSEMOVE
          || data[0] == CONTROLLER_CHOOSEPOKEMON || ((data[0] == CONTROLLER_PRINTSTRING
          || data[0] == CONTROLLER_PRINTSTRINGPLAYERONLY) && size >= 4
          && (data[2] | data[3] << 8) == STRINGID_EMPTYSTRING3)))
        dest[7] = 0;
    if (bufferId == B_COMM_CONTROLLER_IS_DONE)
    {
        if (size != 4 || *data != sBinding.localSlot)
        {
            MultiplayerBattle_FailNative(); return 0;
        }
        dest[8] = *data; // Native caller owns only one stack byte, not four.
    }
    else memcpy(dest + 8, data, size);
    if (bufferId != B_COMM_CONTROLLER_IS_DONE && (size < 2
     || MultiplayerBattle_ControllerRawSize(data[0], data[1]) != size))
    {
        MultiplayerBattle_FailNative(); return 0;
    }
    // DATA and DONE are queued together. Request/reply ordering is validated
    // later by SendBlock, after earlier queued replies have been accepted.
    // Native action prompts carry NONE on a new turn and reuse bufferB's old
    // reply bytes for the item argument, which is meaningful only for USE_ITEM.
    if (bufferId == B_COMM_TO_CONTROLLER && data[0] == CONTROLLER_CHOOSEACTION && data[1] != B_ACTION_USE_ITEM)
        dest[8 + 2] = dest[8 + 3] = 0;
    if (bufferId == B_COMM_TO_CONTROLLER && data[0] == CONTROLLER_SETMONDATA && data[1] == REQUEST_MOVES_PP_BATTLE)
        memset(dest + 8 + 16, 0, 3);
    if (bufferId == B_COMM_TO_CONTROLLER && data[0] == CONTROLLER_CHOOSEMOVE)
    {
        memset(dest + 8 + 25, 0, 3); dest[8 + 33] = 0;
        dest[8 + 42] = dest[8 + 43] = 0;
    }
    if (bufferId == B_COMM_TO_CONTROLLER && data[0] == CONTROLLER_DRAWPARTYSTATUSSUMMARY)
    {
        u32 i; for (i = 0; i < PARTY_SIZE; i++) memset(dest + 8 + 6 + i * 8, 0, 2);
    }
    if (bufferId == B_COMM_TO_ENGINE && data[0] == CONTROLLER_CHOSENMONRETURNVALUE && data[1] == PARTY_SIZE)
        memset(dest + 8 + 2, 0, 3);
    return padded + 8;
}

static u8 PrepareSingles(const struct MultiplayerBattleControl *c)
{
    struct MultiplayerBattlePreparation preparation;
    struct LinkPlayer player;
    u8 ownership[80];
    u32 i, usable = 0;
    if (sState != MP_BATTLE_CHANNEL || sPrepared || c->length != sizeof(preparation)
     || c->blockSeq || c->offset || c->totalSize || c->kind) return MP_BATTLE_ORDER;
    memcpy(&preparation, c->data, sizeof(preparation));
    if (!preparation.checkpointRevision || preparation.checkpointRevision > 0x7FFFFFFF || preparation.flags
     || Zero(preparation.rng, 16)) return MP_BATTLE_MALFORMED;
    if (preparation.nativeCounter != gSaveCounter || preparation.trainerId != READ_OTID_FROM_SAVE
     || preparation.partyCrc32 != Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600)) return MP_BATTLE_ORDER;
    if (MultiplayerOwnership_Query(ownership) != MP_SAVE_OK || Word(ownership + 4)
     || !gPartiesCount[B_TRAINER_PLAYER] || gPartiesCount[B_TRAINER_PLAYER] > PARTY_SIZE) return MP_BATTLE_UNSAFE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon mon = gParties[B_TRAINER_PLAYER][i];
        u32 species = GetMonData(&mon, MON_DATA_SPECIES);
        if (!PartyMonValid((const u8 *)&mon) || (!!species != (i < gPartiesCount[B_TRAINER_PLAYER]))) return MP_BATTLE_UNSAFE;
        if (species && !GetMonData(&mon, MON_DATA_IS_EGG) && mon.hp) usable++;
    }
    if (!usable || gSaveBlock2Ptr->playerGender > 1 || !NameValid(gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1)) return MP_BATTLE_UNSAFE;
    sPreparation = preparation; sOriginalCount = gPartiesCount[B_TRAINER_PLAYER];
    sPersonalRng = gRngValue; sPersonalRng2 = gRng2Value;
    sSavedCallback = gMain.savedCallback; sSavedBattleFlags = gBattleTypeFlags;
    sSavedWireless = gWirelessCommType; sSavedLinkType = gLinkType;
    SavePlayerParty(); sPrepared = TRUE; sState = MP_BATTLE_PREPARED;
    memset(&player, 0, sizeof(player));
    player.version = VERSION_EMERALD + 0x4000; player.trainerId = READ_OTID_FROM_SAVE;
    memcpy(player.name, gSaveBlock2Ptr->playerName, sizeof(player.name));
    player.gender = gSaveBlock2Ptr->playerGender; player.language = gGameLanguage;
    player.linkType = MultiplayerBattle_IsFullMulti() ? LINKTYPE_MULTI_BATTLE : LINKTYPE_BATTLE; player.id = sBinding.localSlot;
    gLinkPlayers[sBinding.localSlot] = player;
    if (!SendBlock(BitmaskAllOtherLinkPlayers(), &player, sizeof(player)))
    {
        MultiplayerBattle_FailNative(); return MP_BATTLE_INTERNAL;
    }
    return MP_BATTLE_OK;
}

static u8 StartSingles(void)
{
    if (sState != MP_BATTLE_PREPARED || !sPrepared || sStarted || sOutPending || sReceiveFlags != (1 << sBinding.playerCount) - 1) return MP_BATTLE_BUSY;
    if (Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600) != sPreparation.partyCrc32
     || gPartiesCount[B_TRAINER_PLAYER] != sOriginalCount || gSaveCounter != sPreparation.nativeCounter) return MP_BATTLE_ORDER;
    ResetBlockReceivedFlags(); sStarted = TRUE; sState = MP_BATTLE_RUNNING;
    gWirelessCommType = 0; gReceivedRemoteLinkPlayers = TRUE;
    gLinkType = MultiplayerBattle_IsFullMulti() ? LINKTYPE_MULTI_BATTLE : LINKTYPE_BATTLE;
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER;
    if (MultiplayerBattle_IsFullMulti())
        gBattleTypeFlags |= BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI;
    TRAINER_BATTLE_PARAM.opponentA = TRAINER_LINK_OPPONENT;
    CleanupOverworldWindowsAndTilemaps();
    gMain.savedCallback = CB2_ReturnToField;
    PlayMapChosenOrBattleBGM(MUS_VS_TRAINER);
    SetMainCallback2(CB2_InitBattle);
    return MP_BATTLE_OK;
}

bool8 MultiplayerBattle_SendPartyPair(u8 index)
{
    if (index != 0 && index != 2 && index != 4) return FALSE;
    return SendBlock(BitmaskAllOtherLinkPlayers(), GetSavedPlayerPartyMon(index), sizeof(struct Pokemon) * 2);
}

bool8 MultiplayerBattle_CopyFullMultiPartyPair(u8 index)
{
    u32 source;
    if (!MultiplayerBattle_IsFullMulti() || sState != MP_BATTLE_RUNNING
     || (index != 0 && index != 2 && index != 4) || sReceiveFlags != 15)
        return FALSE;
    for (source = 0; source < 4; source++)
        if (source != sBinding.localSlot && sReceiveKind[source] != MP_BATTLE_KIND_PARTY_0 + index / 2)
            return FALSE;
    // The three peers were checked by the typed receive path before their flag
    // was published. Map fixed participant IDs into native per-device parties.
    for (source = 0; source < 4; source++)
    {
        enum BattleTrainer trainer;
        if (source == sBinding.localSlot)
            continue; // Local temporary forms were already applied at entry.
        if ((source & 1) == (sBinding.localSlot & 1))
            trainer = B_TRAINER_PARTNER;
        else
            trainer = source < 2 ? B_TRAINER_OPPONENT_A : B_TRAINER_OPPONENT_B;
        memcpy(gParties[trainer] + index, gBlockRecvBuffer[source], sizeof(struct Pokemon) * 2);
    }
    if (index == 4)
    {
        for (enum BattleTrainer trainer = B_TRAINER_OPPONENT_A; trainer < MAX_BATTLE_TRAINERS; trainer++)
        {
            for (u32 slot = 0; slot < PARTY_SIZE; slot++)
            {
                TryFormChange(&gParties[trainer][slot], FORM_CHANGE_BEGIN_BATTLE, trainer);
            }
            gPartiesCount[trainer] = CalculatePartyCount(trainer);
        }
    }
    return TRUE;
}

void MultiplayerBattle_InstallNativeRng(void)
{
    if (MultiplayerBattle_IsNative() && sState == MP_BATTLE_RUNNING && !sSeedInstalled)
    {
        memcpy(&gRngValue, sPreparation.rng, 16); sSeedInstalled = TRUE;
    }
}

bool32 MultiplayerBattle_TryReturnNative(void)
{
    u32 outcome = gBattleOutcome;
    if (!MultiplayerBattle_IsNative() || sState != MP_BATTLE_RUNNING || !sOutcome || sOutPending
     || sReceiveFlags || gBattleControllerExecFlags || BattleControllers_HasPendingLinkBlocks()) return FALSE;
    if ((sBinding.localSlot & 1) && outcome != B_OUTCOME_DREW) outcome ^= B_OUTCOME_DREW;
    if (outcome != sOutcome)
    {
        MultiplayerBattle_FailNative(); return FALSE;
    }
    RestorePlayerPartyExact();
    if (gPartiesCount[B_TRAINER_PLAYER] != sOriginalCount
     || Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600) != sPreparation.partyCrc32)
    {
        MultiplayerBattle_FailNative(); return FALSE;
    }
    sState = MP_BATTLE_RETURNING;
    // Result-held queries have no block descriptor. There are no further native
    // sends before release, so retire the complete descriptor together.
    sOutSeq = sOutCrc = sReadOffset = sOutSize = sOutKind = 0;
    sOutPending = FALSE;
    if (MultiplayerBattle_IsFullMulti())
    {
        // The partner is a remote temporary roster, never locally owned state.
        memset(gParties[B_TRAINER_PARTNER], 0, sizeof(gParties[B_TRAINER_PARTNER]));
        gPartiesCount[B_TRAINER_PARTNER] = 0;
    }
    gReceivedRemoteLinkPlayers = FALSE; gWirelessCommType = sSavedWireless; gLinkType = sSavedLinkType;
    memset(gLinkPlayers, 0, sizeof(gLinkPlayers));
    BattleMain_ReturnNetworkBattleToField();
    return TRUE;
}

static bool32 FieldReturned(void)
{
    u32 id = gPlayerAvatar.objectEventId;
    return gMain.callback1 == CB1_Overworld && gMain.callback2 == CB2_Overworld && !gMain.inBattle
        && !gPaletteFade.active && !ScriptContext_IsEnabled() && !ArePlayerFieldControlsLocked()
        && id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active && gObjectEvents[id].isPlayer
        && gObjectEvents[id].spriteId < MAX_SPRITES && gSprites[gObjectEvents[id].spriteId].inUse;
}

static void FinishNativeReturn(void)
{
    if (sState == MP_BATTLE_RETURNING && FieldReturned())
    {
        if (gPartiesCount[B_TRAINER_PLAYER] != sOriginalCount
         || Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600) != sPreparation.partyCrc32)
        {
            MultiplayerBattle_FailNative(); return;
        }
        gRngValue = sPersonalRng; gRng2Value = sPersonalRng2;
        gBattleTypeFlags = sSavedBattleFlags; gMain.savedCallback = sSavedCallback;
        sRestored = TRUE;
        sState = MP_BATTLE_FIELD_READY_HELD;
    }
}

static u8 ReceiveNative(const struct MultiplayerBattleControl *c)
{
    u8 source = c->sourceSlot, kind = c->kind;
    if (source >= sBinding.playerCount || source == sBinding.localSlot) return MP_BATTLE_ROLE;
    if (kind < MP_BATTLE_KIND_TRAINER || kind > MP_BATTLE_KIND_CONTROLLER) return MP_BATTLE_KIND;
    if (!c->blockSeq || !c->totalSize || c->totalSize > 256 || c->offset >= c->totalSize
     || c->length != min(128, c->totalSize - c->offset) || (c->offset % 128)) return MP_BATTLE_MALFORMED;
    if (c->blockSeq == sReceiveSeq[source])
    {
        // Late exact retry stays consumed; it never recreates a native flag.
        return kind == sReceiveKind[source] && c->totalSize == sReceiveSize[source]
            && !memcmp((const u8 *)gBlockRecvBuffer[source] + c->offset, c->data, c->length) ? MP_BATTLE_OK : MP_BATTLE_ORDER;
    }
    if (kind != sNextReceiveKind[source] || (kind != MP_BATTLE_KIND_TRAINER && sState != MP_BATTLE_RUNNING)) return MP_BATTLE_ORDER;
    if (sReceiveSeq[source] == 0xFFFFFFFF || c->blockSeq != sReceiveSeq[source] + 1) return MP_BATTLE_ORDER;
    if (sReceiveFlags & (1 << source)) return MP_BATTLE_BUSY;
    if (c->offset != sReceiveOffset[source]) return MP_BATTLE_ORDER;
    if (!c->offset)
    {
        memset(gBlockRecvBuffer[source], 0, 256);
        sReceiveKind[source] = kind; sReceiveSize[source] = c->totalSize;
    }
    else if (kind != sReceiveKind[source] || c->totalSize != sReceiveSize[source]) return MP_BATTLE_ORDER;
    memcpy((u8 *)gBlockRecvBuffer[source] + c->offset, c->data, c->length);
    sReceiveOffset[source] += c->length;
    if (sReceiveOffset[source] == c->totalSize)
    {
        if (!NativeBlockValid(gBlockRecvBuffer[source], c->totalSize, kind, source))
        {
            sReceiveOffset[source] = 0; memset(gBlockRecvBuffer[source], 0, 256); return MP_BATTLE_KIND;
        }
        if (kind == MP_BATTLE_KIND_TRAINER) memcpy(&gLinkPlayers[source], gBlockRecvBuffer[source], sizeof(struct LinkPlayer));
        if (kind == MP_BATTLE_KIND_CONTROLLER) NoteNativeController((const u8 *)gBlockRecvBuffer[source], source);
        else sNextReceiveKind[source]++;
        sReceiveSeq[source] = c->blockSeq; sReceiveOffset[source] = 0; sReceiveFlags |= 1 << source;
    }
    return MP_BATTLE_OK;
}

static void SuspendNative(void)
{
    if (sState != MP_BATTLE_SUSPENDED)
    {
        sSuspendedState = sState;
        sSuspensionEpoch = sResumeEpoch;
        sState = MP_BATTLE_SUSPENDED;
    }
}

static void ResumeQueryData(u8 *data)
{
    QueryData(data);
    PutWord(data + 64, sState | ((sResumeArmed || sReleasedResumable) << 8) | (sSuspendedState << 16));
    PutWord(data + 68, sResumeEpoch);
    memcpy(data + 72, sResumeToken, 16);
}

static u8 HandleResume(const struct MultiplayerBattleControl *c)
{
    struct MultiplayerBattleBinding binding;
    u32 epoch, last, i;
    if ((!MultiplayerBattle_IsNative() && !(sState == MP_BATTLE_IDLE && sReleasedResumable
          && sLastCommand.opcode == MP_BATTLE_RELEASE_SINGLES))
     || c->incarnation != sIncarnation || c->sessionEpoch < sSession
     || memcmp(c->battleId, sBattleId, 16) || c->phase != sBinding.mode || c->sourceSlot != sBinding.localSlot)
        return MP_BATTLE_SESSION;
    if (c->blockSeq || c->offset || c->totalSize || c->kind)
        return MP_BATTLE_MALFORMED;
    if (c->opcode == MP_BATTLE_RESUME_QUERY)
        return c->length ? MP_BATTLE_MALFORMED : MP_BATTLE_OK;
    if ((!sResumeArmed && !sReleasedResumable) || sState == MP_BATTLE_RECOVERY_REQUIRED || sState == MP_BATTLE_EXPIRED)
        return MP_BATTLE_UNSAFE;
    if (c->opcode == MP_BATTLE_SUSPEND)
    {
        if (c->length) return MP_BATTLE_MALFORMED;
        if (sState == MP_BATTLE_IDLE) LatchReleasedWait();
        else SuspendNative();
        return MP_BATTLE_OK;
    }
    if (c->opcode == MP_BATTLE_REAUTHORIZE)
    {
        if (c->length != 140 || (sState != MP_BATTLE_SUSPENDED && !sReleasedResumable))
            return MP_BATTLE_MALFORMED;
        memcpy(&binding, c->data, sizeof(binding));
        epoch = Word(c->data + 132); last = Word(c->data + 136);
        if (!BindingValid(&binding, c) || !epoch || epoch > 0x7FFFFFFF
         || Zero(c->data + 116, 16) || last != sLastSeq
         || memcmp(binding.generation, sBinding.generation, 16)
         || memcmp(binding.players, sBinding.players, sizeof(binding.players))
         || memcmp(binding.incarnations, sBinding.incarnations, sizeof(binding.incarnations))
         || binding.playerCount != sBinding.playerCount || binding.localSlot != sBinding.localSlot || binding.mode != sBinding.mode || binding.reserved != sBinding.reserved)
            return MP_BATTLE_SESSION;
        for (i = 0; i < binding.playerCount; i++)
            if (binding.leases[i] < sBinding.leases[i]) return MP_BATTLE_SESSION;
        if (epoch <= sSuspensionEpoch) return MP_BATTLE_ORDER;
        if (epoch == sResumeEpoch)
            return memcmp(&binding, &sBinding, sizeof(binding)) || memcmp(c->data + 116, sResumeToken, 16)
                ? MP_BATTLE_ORDER : MP_BATTLE_OK;
        if (epoch < sResumeEpoch) return MP_BATTLE_ORDER;
        sBinding = binding; sSession = c->sessionEpoch;
        sResumeEpoch = epoch; memcpy(sResumeToken, c->data + 116, 16);
        // The ordinary cached command is retained. Only the explicitly
        // approved lease envelope changes, so an uncertain successful command
        // can be acknowledged exactly once under the new authentication.
        sLastCommand.sessionEpoch = sSession; sCachedResponse.sessionEpoch = sSession;
        if (sLastCommand.opcode == MP_BATTLE_BIND)
        {
            memcpy(sLastCommand.data, &sBinding, sizeof(sBinding));
            sLastCommand.chunkCrc32 = Crc32(sLastCommand.data, sLastCommand.length);
        }
        return MP_BATTLE_OK;
    }
    if (c->opcode == MP_BATTLE_RESUME)
    {
        if (c->length != 20 || c->sessionEpoch != sSession || !sResumeEpoch
         || sResumeEpoch <= sSuspensionEpoch
         || Word(c->data + 16) != sResumeEpoch || memcmp(c->data, sResumeToken, 16))
            return MP_BATTLE_SESSION;
        // A released core stays field-held until durable global closure
        // authorizes its explicit idempotent RETIRE command.
        if (sState == MP_BATTLE_IDLE) sReleasedSuspended = FALSE;
        if (sState == MP_BATTLE_SUSPENDED)
        {
            sState = sSuspendedState;
            sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
        }
        return MP_BATTLE_OK;
    }
    return MP_BATTLE_MALFORMED;
}

// Provisional admission is independent of ordinary command sequencing. A
// client persists its exact admission before sending it; losing presence may
// not prevent that same original idle save binding from completing the hold.
static u8 Admit(const struct MultiplayerBattleControl *c)
{
    u32 saveSession, saveIncarnation, base;
    bool32 menuOwned = MultiplayerMenu_CanAdmitBattle(c->sessionEpoch, c->incarnation);
    if (c->phase == MP_BATTLE_PHASE_DIAGNOSTIC || c->sourceSlot >= (c->phase == MP_BATTLE_PHASE_SINGLES ? 2 : 4)
     || c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 48
     || Zero(c->data, 16) || Zero(c->data + 16, 16) || Zero(c->data + 32, 16))
        return MP_BATTLE_MALFORMED;
    if (sState == MP_BATTLE_ADMITTING)
        return c->incarnation == sIncarnation && c->sessionEpoch >= sSession
            && !memcmp(c->battleId, sBattleId, 16) && !memcmp(c->data, sBinding.generation, 16)
            && !memcmp(c->data + 16, sBinding.players[sBinding.localSlot], 16)
            && !memcmp(c->data + 32, sAdmitRuntime, 16)
            && c->sourceSlot == sBinding.localSlot && c->phase == sBinding.mode ? MP_BATTLE_OK : MP_BATTLE_SESSION;
    if (sReleasedResumable || sRetiredWaiting || !MultiplayerSave_GetIdleBinding(&saveSession, &saveIncarnation)
     || saveSession != c->sessionEpoch || saveIncarnation != c->incarnation
     || !MultiplayerBattle_CheckBinding(c->sessionEpoch, c->incarnation))
        return MP_BATTLE_SESSION;
    if (!(menuOwned ? MultiplayerSave_FieldReadyForBattleMenu(c->sessionEpoch, c->incarnation) : MultiplayerSave_FieldReady())
     || !MultiplayerOwnership_IsEnrolled() || !MultiplayerOwnership_CanSaveParty()
     || gSaveBlock3Ptr == NULL || memcmp(c->data + 16, gSaveBlock3Ptr->ownership.playerId, 16))
        return MP_BATTLE_UNSAFE;
    base = sSession == c->sessionEpoch && sIncarnation == c->incarnation ? sLastSeq : 0;
    if (base == 0xFFFFFFFF) return MP_BATTLE_ORDER;
    ClearBlocks(); memset(&sBinding, 0, sizeof(sBinding));
    memcpy(sBinding.generation, c->data, 16); memcpy(sBinding.players[c->sourceSlot], c->data + 16, 16);
    memcpy(sAdmitRuntime, c->data + 32, 16); memcpy(sBattleId, c->battleId, 16);
    sBinding.playerCount = c->phase == MP_BATTLE_PHASE_SINGLES ? 2 : 4;
    sBinding.localSlot = c->sourceSlot; sBinding.mode = c->phase; sBinding.reserved = 1;
    sBinding.leases[c->sourceSlot] = sSession = c->sessionEpoch;
    sBinding.incarnations[c->sourceSlot] = sIncarnation = c->incarnation;
    sLastSeq = base; memset(&sLastCommand, 0, sizeof(sLastCommand)); memset(&sCachedResponse, 0, sizeof(sCachedResponse));
    sAdmitPartyCrc = Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600);
    sAdmitSaveCounter = gSaveCounter; sAdmitPartyCount = gPartiesCount[B_TRAINER_PLAYER];
    sSavedResetDisabled = menuOwned ? MultiplayerMenu_AdmissionResetWasDisabled() : gSoftResetDisabled;
    gSoftResetDisabled = TRUE;
    sState = MP_BATTLE_ADMITTING; Multiplayer_LeaveField();
    if (menuOwned) MultiplayerMenu_TransferBattleAdmission(c->sessionEpoch, c->incarnation);
    return MP_BATTLE_OK;
}

static u8 Retire(const struct MultiplayerBattleControl *c)
{
    if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 28)
        return MP_BATTLE_MALFORMED;
    if (sState != MP_BATTLE_IDLE || (!sReleasedResumable && !sRetired)
     || sLastCommand.opcode != MP_BATTLE_RELEASE_SINGLES || sCachedResponse.status != MP_BATTLE_OK
     || c->incarnation != sIncarnation || c->sessionEpoch < sSession
     || !MultiplayerSave_CheckBinding(c->sessionEpoch, c->incarnation)
     || memcmp(c->battleId, sBattleId, 16) || c->sourceSlot != sBinding.localSlot || c->phase != sBinding.mode
     || memcmp(c->data, sBinding.generation, 16) || Word(c->data + 16) != sLastSeq
     || memcmp(c->data + 20, sLastCommand.data, 8))
        return MP_BATTLE_SESSION;
    if (c->opcode == MP_BATTLE_HANDOFF && (!sRetired || sReleasedResumable)) return MP_BATTLE_ORDER;
    sSession = c->sessionEpoch; sBinding.leases[sBinding.localSlot] = sSession;
    sLastCommand.sessionEpoch = sCachedResponse.sessionEpoch = sSession;
    if (c->opcode == MP_BATTLE_HANDOFF)
    {
        sRetiredWaiting = FALSE; gSoftResetDisabled = sSavedResetDisabled;
    }
    else
    {
        if (!sRetired) sRetiredWaiting = TRUE;
        sReleasedResumable = sReleasedWaiting = FALSE; sRetired = TRUE;
        if (sRetiredWaiting) gSoftResetDisabled = TRUE;
    }
    return MP_BATTLE_OK;
}

static void Handle(const struct MultiplayerBattleControl *c)
{
    u8 data[128], status = MP_BATTLE_OK;
    u16 length = 0;
    bool32 cache = TRUE;
    if (c->status || c->reserved || c->length > 192 || !c->incarnation || !c->sessionEpoch
     || c->sessionEpoch > 0x7FFFFFFF || !c->commandSeq || Zero(c->battleId, 16)
     || (c->phase < MP_BATTLE_PHASE_DIAGNOSTIC || c->phase > MP_BATTLE_PHASE_FULL_MULTI) || c->sourceSlot >= 4
     || c->totalSize > MULTIPLAYER_BATTLE_BLOCK_SIZE || c->offset > c->totalSize
     || !Zero(c->data + c->length, 192 - c->length)
     || c->opcode < MP_BATTLE_BIND || c->opcode > MP_BATTLE_HANDOFF)
    {
        Respond(c, MP_BATTLE_MALFORMED, NULL, 0, FALSE); return;
    }
    if (c->chunkCrc32 != Crc32(c->data, c->length))
    {
        Respond(c, MP_BATTLE_CRC, NULL, 0, FALSE); return;
    }
    if (c->opcode == MP_BATTLE_ADMIT || c->opcode == MP_BATTLE_RETIRE || c->opcode == MP_BATTLE_HANDOFF)
    {
        status = c->opcode == MP_BATTLE_ADMIT ? Admit(c) : Retire(c);
        if (status == MP_BATTLE_OK)
        {
            if (c->opcode == MP_BATTLE_ADMIT) { QueryData(data); length = 64; }
            else { memcpy(data, c->data, 28); length = 28; }
        }
        Respond(c, status, data, length, FALSE); return;
    }
    if (c->opcode >= MP_BATTLE_SUSPEND)
    {
        status = HandleResume(c);
        if (status == MP_BATTLE_OK) { ResumeQueryData(data); length = 88; }
        // Out-of-band suspension and quorum renewal never consume or replace
        // the last ordinary command/response, including an uncertain delivery.
        Respond(c, status, data, length, FALSE); return;
    }
    if (c->opcode == MP_BATTLE_QUERY)
    {
        // Observation cannot advance sequence/cache or refresh the watchdog.
        // A newer authenticated lease can inspect an old held incarnation.
        if (!Empty(c))
            status = MP_BATTLE_MALFORMED;
        else if (sState != MP_BATTLE_UNBOUND && (c->incarnation != sIncarnation || c->sessionEpoch < sSession
             || memcmp(c->battleId, sBattleId, 16) || c->sourceSlot != sBinding.localSlot || c->phase != sBinding.mode))
            status = MP_BATTLE_SESSION;
        else
        {
            QueryData(data); length = 64;
        }
        Respond(c, status, data, length, FALSE); return;
    }
    // A byte-identical last control is the only replay. Native rejected
    // controls and read-only QUERY preserve a lost successful acknowledgement.
    // Phase 1 retains its accepted diagnostic failure-sequence behavior.
    if (c->commandSeq == sLastSeq && sLastSeq && !memcmp(c, &sLastCommand, sizeof(*c)))
    {
        Replay(); return;
    }
    if (c->opcode == MP_BATTLE_BIND)
    {
        status = Bind(c);
        if (status == MP_BATTLE_OK)
        {
            sLastSeq = c->commandSeq;
            QueryData(data); length = 64;
        }
        Respond(c, status, data, length, status == MP_BATTLE_OK); return;
    }
    if (sState == MP_BATTLE_UNBOUND || c->incarnation != sIncarnation || c->sessionEpoch != sSession
     || memcmp(c->battleId, sBattleId, 16) || c->phase != sBinding.mode)
    {
        Respond(c, MP_BATTLE_SESSION, NULL, 0, FALSE); return;
    }
    if (sLastSeq == 0xFFFFFFFF || c->commandSeq != sLastSeq + 1)
    {
        Respond(c, MP_BATTLE_ORDER, NULL, 0, FALSE); return;
    }
    if (c->opcode != MP_BATTLE_RECEIVE && c->sourceSlot != sBinding.localSlot)
        status = MP_BATTLE_ROLE;
    else if (!MultiplayerBattle_IsActive())
        status = MP_BATTLE_ORDER;
    else if (sState == MP_BATTLE_RECOVERY_REQUIRED || sState == MP_BATTLE_SUSPENDED || sState == MP_BATTLE_ADMITTING
          || (sState == MP_BATTLE_EXPIRED && c->opcode != (MultiplayerBattle_IsNative() ? MP_BATTLE_ABORT_PRE_START : MP_BATTLE_CLOSE)))
        status = MP_BATTLE_TIMEOUT;
    else switch (c->opcode)
    {
    case MP_BATTLE_EMIT_DIAGNOSTIC:
    {
        struct MultiplayerBattleDiagnostic block;
        if (MultiplayerBattle_IsNative()) status = MP_BATTLE_KIND;
        else if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 4)
            status = MP_BATTLE_MALFORMED;
        else
        {
            block.magic = MULTIPLAYER_BATTLE_DIAGNOSTIC_MAGIC; block.version = 1;
            memcpy(block.generation, sBinding.generation, 16);
            block.sourceSlot = sBinding.localSlot; block.token = Word(c->data);
            block.lease = sSession; block.incarnation = sIncarnation;
            if (!SendBlock(BitmaskAllOtherLinkPlayers(), &block, sizeof(block)))
                status = MP_BATTLE_BUSY;
            else
            {
                sLastSeq = c->commandSeq; QueryData(data); length = 64;
            }
        }
        break;
    }
    case MP_BATTLE_READ_OUT:
        if (c->kind != sOutKind)
            status = MP_BATTLE_KIND;
        else if (c->length || !sOutPending || c->blockSeq != sOutSeq || c->totalSize != sOutSize
              || c->offset != sReadOffset || c->offset >= sOutSize)
            status = MP_BATTLE_ORDER;
        else
        {
            length = min(MULTIPLAYER_BATTLE_CHUNK_SIZE, sOutSize - sReadOffset);
            memcpy(data, (const u8 *)gBlockSendBuffer + sReadOffset, length);
            sReadOffset += length;
        }
        break;
    case MP_BATTLE_ACK_OUT:
        if (c->kind != sOutKind)
            status = MP_BATTLE_KIND;
        else if (c->length != 8 || !sOutPending || c->blockSeq != sOutSeq || c->totalSize != sOutSize
              || c->offset != sOutSize || sReadOffset != sOutSize)
            status = MP_BATTLE_ORDER;
        else if (Word(c->data) != sOutCrc)
            status = MP_BATTLE_CRC;
        else if (Word(c->data + 4) != (((1 << sBinding.playerCount) - 1) ^ (1 << sBinding.localSlot)))
            status = MP_BATTLE_ROLE;
        else
            sOutPending = FALSE;
        break;
    case MP_BATTLE_RECEIVE:
    {
        struct MultiplayerBattleDiagnostic block;
        u32 source = c->sourceSlot;
        if (MultiplayerBattle_IsNative()) status = ReceiveNative(c);
        else if (source >= sBinding.playerCount || source == sBinding.localSlot)
            status = MP_BATTLE_ROLE;
        else if (c->kind != MP_BATTLE_KIND_DIAGNOSTIC)
            status = MP_BATTLE_KIND;
        else if (!c->blockSeq || c->offset || c->totalSize != sizeof(block) || c->length != sizeof(block))
            status = MP_BATTLE_MALFORMED;
        else
        {
            memcpy(&block, c->data, sizeof(block));
            if (!DiagnosticValid(&block, source))
                status = MP_BATTLE_ROLE;
            else if (c->blockSeq == sReceiveSeq[source])
            {
                if (memcmp(gBlockRecvBuffer[source], &block, sizeof(block)))
                    status = MP_BATTLE_ORDER;
                // A consumed exact block stays consumed on a late retry.
            }
            else if (sReceiveSeq[source] == 0xFFFFFFFF || c->blockSeq != sReceiveSeq[source] + 1)
                status = MP_BATTLE_ORDER;
            else if (sReceiveFlags & (1 << source))
                status = MP_BATTLE_BUSY;
            else
            {
                memset(gBlockRecvBuffer[source], 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
                memcpy(gBlockRecvBuffer[source], &block, sizeof(block));
                sReceiveSeq[source] = c->blockSeq; sReceiveFlags |= 1 << source;
            }
        }
        break;
    }
    case MP_BATTLE_CLEAR_RX:
        if (MultiplayerBattle_IsNative()) status = MP_BATTLE_KIND;
        else if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 4 || !Word(c->data))
            status = MP_BATTLE_MALFORMED;
        else if (Word(c->data) & ~((1 << sBinding.playerCount) - 1))
            status = MP_BATTLE_ROLE;
        else
        {
            u32 i;
            for (i = 0; i < sBinding.playerCount; i++)
                if (Word(c->data) & (1 << i))
                    ResetBlockReceivedFlag(i);
        }
        break;
    case MP_BATTLE_CLOSE:
        if (MultiplayerBattle_IsNative()) status = MP_BATTLE_UNSAFE;
        else if (!Empty(c))
            status = MP_BATTLE_MALFORMED;
        else
        {
            ClearBlocks(); sState = MP_BATTLE_IDLE;
            gSoftResetDisabled = sSavedResetDisabled;
        }
        break;
    case MP_BATTLE_ARM_RESUME:
        if (!Empty(c) || !MultiplayerBattle_IsNative() || sState != MP_BATTLE_CHANNEL || sPrepared || sResumeArmed)
            status = MP_BATTLE_UNSAFE;
        else
        {
            sResumeArmed = TRUE; sLastSeq = c->commandSeq; QueryData(data); length = 64;
        }
        break;
    case MP_BATTLE_KEEP:
        if (!Empty(c))
            status = MP_BATTLE_MALFORMED;
        else
        {
            sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
            sLastSeq = c->commandSeq; QueryData(data); length = 64;
        }
        break;
    case MP_BATTLE_PREPARE_SINGLES:
        if (!MultiplayerBattle_IsNative()) status = MP_BATTLE_KIND;
        else status = PrepareSingles(c);
        if (status == MP_BATTLE_OK) { sLastSeq = c->commandSeq; QueryData(data); length = 64; }
        break;
    case MP_BATTLE_START_SINGLES:
        if (!MultiplayerBattle_IsNative()) status = MP_BATTLE_KIND;
        else if (!Empty(c)) status = MP_BATTLE_MALFORMED;
        else status = StartSingles();
        if (status == MP_BATTLE_OK) { sLastSeq = c->commandSeq; QueryData(data); length = 64; }
        break;
    case MP_BATTLE_ABORT_PRE_START:
        if (!MultiplayerBattle_IsNative() || sStarted) status = MP_BATTLE_UNSAFE;
        else if (!Empty(c)) status = MP_BATTLE_MALFORMED;
        else
        {
            if (sPrepared)
            {
                RestorePlayerPartyExact(); gRngValue = sPersonalRng; gRng2Value = sPersonalRng2;
                gMain.savedCallback = sSavedCallback;
            }
            ClearBlocks(); sState = MP_BATTLE_IDLE; gSoftResetDisabled = sSavedResetDisabled;
        }
        break;
    case MP_BATTLE_RELEASE_SINGLES:
        if (!MultiplayerBattle_IsNative() || sState != MP_BATTLE_FIELD_READY_HELD || !sRestored) status = MP_BATTLE_UNSAFE;
        else if (c->length != 8 || c->blockSeq || c->offset || c->totalSize || c->kind) status = MP_BATTLE_MALFORMED;
        else if (Word(c->data) != sPreparation.partyCrc32 || Word(c->data + 4) != sOutcome) status = MP_BATTLE_ORDER;
        else if (gPartiesCount[B_TRAINER_PLAYER] != sOriginalCount
              || Crc32((const u8 *)gParties[B_TRAINER_PLAYER], 600) != sPreparation.partyCrc32) status = MP_BATTLE_INTERNAL;
        else
        {
            bool8 resumable = sResumeArmed;
            ClearBlocks(); sState = MP_BATTLE_IDLE; gSoftResetDisabled = sSavedResetDisabled;
            sReleasedResumable = sReleasedWaiting = resumable;
            if (resumable) gSoftResetDisabled = TRUE;
        }
        break;
    default:
        status = MP_BATTLE_MALFORMED; cache = FALSE;
    }
    if (status == MP_BATTLE_OK && MultiplayerBattle_IsActive() && sState != MP_BATTLE_EXPIRED && sState != MP_BATTLE_RECOVERY_REQUIRED && sState != MP_BATTLE_SUSPENDED)
        sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
    if (c->phase != MP_BATTLE_PHASE_DIAGNOSTIC && status != MP_BATTLE_OK)
        cache = FALSE;
    Respond(c, status, data, length, cache);
}

void MultiplayerBattle_Init(void)
{
    if (MultiplayerBattle_IsActive())
        gSoftResetDisabled = sSavedResetDisabled;
    sState = MP_BATTLE_UNBOUND; sFrame = 0; sSeenGeneration = 0;
    sSession = 0; sIncarnation = 0; sLastSeq = 0; sDeadline = 0;
    memset(&sBinding, 0, sizeof(sBinding)); memset(sBattleId, 0, sizeof(sBattleId));
    memset(&sLastCommand, 0, sizeof(sLastCommand)); memset(&sCachedResponse, 0, sizeof(sCachedResponse));
    memset((void *)&gMultiplayerBattleMailbox, 0, sizeof(gMultiplayerBattleMailbox));
    ClearBlocks();
    ObserveHoldFlags();
    gMultiplayerBattleMailbox.abiVersion = *(const volatile u32 *)&gMultiplayerBattleAbiVersion;
    gMultiplayerBattleMailbox.byteSize = MULTIPLAYER_BATTLE_SIZE;
    gMultiplayerBattleMailbox.magic = MULTIPLAYER_BATTLE_MAGIC;
}

void MultiplayerBattle_Tick(void)
{
    u32 generation;
    struct MultiplayerBattleControl command;
    sFrame++; gMultiplayerBattleMailbox.frame = sFrame;
    FinishNativeReturn();
    if (MultiplayerBattle_IsActive() && sState != MP_BATTLE_EXPIRED && sState != MP_BATTLE_RECOVERY_REQUIRED && sState != MP_BATTLE_SUSPENDED && sState != MP_BATTLE_ADMITTING
     && (s32)(sFrame - sDeadline) >= 0)
    {
        if (sResumeArmed) SuspendNative();
        else sState = sStarted ? MP_BATTLE_RECOVERY_REQUIRED : MP_BATTLE_EXPIRED;
    }
    generation = gMultiplayerBattleMailbox.commandGeneration;
    if (generation != sSeenGeneration && !(generation & 1))
    {
        command = gMultiplayerBattleMailbox.command;
        if (generation == gMultiplayerBattleMailbox.commandGeneration)
        {
            sSeenGeneration = generation;
            Handle(&command);
        }
    }
    gMultiplayerBattleMailbox.state = sState;
    ObserveHoldFlags();
}
