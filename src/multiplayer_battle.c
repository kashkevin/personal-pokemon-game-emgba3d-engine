#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "multiplayer.h"
#include "main.h"
#include "link.h"

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

const u32 gMultiplayerBattleAbiVersion = MULTIPLAYER_BATTLE_ABI;
EWRAM_DATA volatile struct MultiplayerBattleMailbox gMultiplayerBattleMailbox = {0};
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
static EWRAM_DATA u32 sOutSeq = 0;
static EWRAM_DATA u32 sOutCrc = 0;
static EWRAM_DATA u32 sReceiveSeq[4] = {0};
static EWRAM_DATA u16 sOutSize = 0;
static EWRAM_DATA u16 sReadOffset = 0;
static EWRAM_DATA u8 sReceiveFlags = 0;
static EWRAM_DATA bool8 sOutPending = FALSE;
static EWRAM_DATA bool8 sSavedResetDisabled = FALSE;

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
    return sState == MP_BATTLE_CHANNEL || sState == MP_BATTLE_EXPIRED;
}

bool32 MultiplayerBattle_PauseClocks(void)
{
    return MultiplayerBattle_IsActive();
}

bool32 MultiplayerBattle_CheckBinding(u32 session, u32 incarnation)
{
    return session > sSession || (session == sSession && incarnation >= sIncarnation);
}

u8 MultiplayerBattle_GetPlayerId(void) { return sBinding.localSlot; }
u8 MultiplayerBattle_GetPlayerCount(void) { return sBinding.playerCount; }
bool8 MultiplayerBattle_IsSendFinished(void) { return !sOutPending && sState == MP_BATTLE_CHANNEL; }
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
    if (sState != MP_BATTLE_CHANNEL || sOutPending || sOutSeq == 0xFFFFFFFF
     || sReceiveFlags & (1 << local) || size != sizeof(block) || src == NULL)
        return FALSE;
    memcpy(&block, src, sizeof(block));
    if (!DiagnosticValid(&block, local))
        return FALSE;
    memset(gBlockSendBuffer, 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    memcpy(gBlockSendBuffer, &block, sizeof(block));
    // Native block reception includes the sender. No controller consumes this
    // development frame; native battle entry and dispatch remain disabled.
    memset(gBlockRecvBuffer[local], 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    memcpy(gBlockRecvBuffer[local], &block, sizeof(block));
    sReceiveFlags |= 1 << local;
    sOutSeq++; sOutSize = size; sReadOffset = 0;
    sOutCrc = Crc32((const u8 *)&block, size); sOutPending = TRUE;
    return TRUE;
}

static void QueryData(u8 *data)
{
    memset(data, 0, 64);
    PutWord(data, MultiplayerBattle_IsActive() | (sState == MP_BATTLE_EXPIRED) << 1 | sOutPending << 2);
    memcpy(data + 4, sBinding.generation, 16);
    data[20] = sBinding.playerCount; data[21] = sBinding.localSlot; data[22] = sBinding.mode;
    PutWord(data + 24, sLastSeq); PutWord(data + 28, sOutSeq);
    PutWord(data + 32, sOutSize ? MP_BATTLE_KIND_DIAGNOSTIC : MP_BATTLE_KIND_NONE);
    PutWord(data + 36, sOutSize); PutWord(data + 40, sOutCrc); PutWord(data + 44, sReadOffset);
    PutWord(data + 48, sReceiveFlags);
    PutWord(data + 52, sState == MP_BATTLE_CHANNEL ? max(0, (s32)(sDeadline - sFrame)) : 0);
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
     || b->localSlot != c->sourceSlot || b->mode != MP_BATTLE_PHASE_DIAGNOSTIC || b->reserved
     || Zero(b->generation, 16))
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
    sOutSeq = 0; sOutSize = 0; sReadOffset = 0; sOutCrc = 0;
    memset(sReceiveSeq, 0, sizeof(sReceiveSeq));
    memset(gBlockSendBuffer, 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
    for (i = 0; i < 4; i++)
        memset(gBlockRecvBuffer[i], 0, MULTIPLAYER_BATTLE_BLOCK_SIZE);
}

static u8 Bind(const struct MultiplayerBattleControl *c)
{
    struct MultiplayerBattleBinding binding;
    bool32 samePair = c->sessionEpoch == sSession && c->incarnation == sIncarnation;
    if (!MultiplayerBattle_CheckBinding(c->sessionEpoch, c->incarnation)
     || !MultiplayerSave_CheckBinding(c->sessionEpoch, c->incarnation))
        return MP_BATTLE_SESSION;
    if (MultiplayerBattle_IsActive() || MultiplayerSave_IsHeld())
        return MP_BATTLE_BUSY;
    if ((samePair && (sLastSeq == 0xFFFFFFFF || c->commandSeq != sLastSeq + 1 || !memcmp(c->battleId, sBattleId, 16)))
     || (!samePair && c->commandSeq != 1))
        return MP_BATTLE_ORDER;
    if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != sizeof(binding))
        return MP_BATTLE_MALFORMED;
    memcpy(&binding, c->data, sizeof(binding));
    if (!BindingValid(&binding, c))
        return MP_BATTLE_ROLE;
    if (!MultiplayerSave_FieldReady() || !MultiplayerOwnership_IsEnrolled() || !MultiplayerOwnership_CanSaveParty())
        return MP_BATTLE_UNSAFE;
    sSession = c->sessionEpoch; sIncarnation = c->incarnation;
    memcpy(sBattleId, c->battleId, 16); sBinding = binding; ClearBlocks();
    sSavedResetDisabled = gSoftResetDisabled; gSoftResetDisabled = TRUE;
    sState = MP_BATTLE_CHANNEL; sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
    Multiplayer_LeaveField();
    return MP_BATTLE_OK;
}

static void Handle(const struct MultiplayerBattleControl *c)
{
    u8 data[128], status = MP_BATTLE_OK;
    u16 length = 0;
    bool32 cache = TRUE;
    if (c->status || c->reserved || c->length > 192 || !c->incarnation || !c->sessionEpoch
     || c->sessionEpoch > 0x7FFFFFFF || !c->commandSeq || Zero(c->battleId, 16)
     || c->phase != MP_BATTLE_PHASE_DIAGNOSTIC || c->sourceSlot >= 4
     || c->totalSize > MULTIPLAYER_BATTLE_BLOCK_SIZE || c->offset > c->totalSize
     || !Zero(c->data + c->length, 192 - c->length)
     || c->opcode < MP_BATTLE_BIND || c->opcode > MP_BATTLE_KEEP)
    {
        Respond(c, MP_BATTLE_MALFORMED, NULL, 0, FALSE); return;
    }
    if (c->chunkCrc32 != Crc32(c->data, c->length))
    {
        Respond(c, MP_BATTLE_CRC, NULL, 0, FALSE); return;
    }
    if (c->opcode == MP_BATTLE_QUERY)
    {
        // Observation cannot advance sequence/cache or refresh the watchdog.
        // A newer authenticated lease can inspect an old held incarnation.
        if (!Empty(c))
            status = MP_BATTLE_MALFORMED;
        else if (sState != MP_BATTLE_UNBOUND && (c->incarnation != sIncarnation || c->sessionEpoch < sSession
             || memcmp(c->battleId, sBattleId, 16) || c->sourceSlot != sBinding.localSlot))
            status = MP_BATTLE_SESSION;
        else
        {
            QueryData(data); length = 64;
        }
        Respond(c, status, data, length, FALSE); return;
    }
    // A byte-identical last control is the only replay. Rejected controls and
    // read-only QUERY do not destroy a lost successful acknowledgement.
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
     || memcmp(c->battleId, sBattleId, 16))
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
    else if (sState == MP_BATTLE_EXPIRED && c->opcode != MP_BATTLE_CLOSE)
        status = MP_BATTLE_TIMEOUT;
    else switch (c->opcode)
    {
    case MP_BATTLE_EMIT_DIAGNOSTIC:
    {
        struct MultiplayerBattleDiagnostic block;
        if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 4)
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
        if (c->kind != MP_BATTLE_KIND_DIAGNOSTIC)
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
        if (c->kind != MP_BATTLE_KIND_DIAGNOSTIC)
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
        if (source >= sBinding.playerCount || source == sBinding.localSlot)
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
        if (c->blockSeq || c->offset || c->totalSize || c->kind || c->length != 4 || !Word(c->data))
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
        if (!Empty(c))
            status = MP_BATTLE_MALFORMED;
        else
        {
            ClearBlocks(); sState = MP_BATTLE_IDLE;
            gSoftResetDisabled = sSavedResetDisabled;
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
    default:
        status = MP_BATTLE_MALFORMED; cache = FALSE;
    }
    if (status == MP_BATTLE_OK && sState == MP_BATTLE_CHANNEL)
        sDeadline = sFrame + MULTIPLAYER_BATTLE_DEADLINE;
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
    gMultiplayerBattleMailbox.abiVersion = *(const volatile u32 *)&gMultiplayerBattleAbiVersion;
    gMultiplayerBattleMailbox.byteSize = MULTIPLAYER_BATTLE_SIZE;
    gMultiplayerBattleMailbox.magic = MULTIPLAYER_BATTLE_MAGIC;
}

void MultiplayerBattle_Tick(void)
{
    u32 generation;
    struct MultiplayerBattleControl command;
    sFrame++; gMultiplayerBattleMailbox.frame = sFrame;
    if (sState == MP_BATTLE_CHANNEL && (s32)(sFrame - sDeadline) >= 0)
        sState = MP_BATTLE_EXPIRED;
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
}
