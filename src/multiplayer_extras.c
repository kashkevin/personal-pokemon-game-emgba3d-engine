#include "global.h"
#ifdef PORTABLE
#include <stddef.h>
// Shared-world extras: multiplayer Secret Bases (include/multiplayer_extras.h).
#include "main.h"
#include "multiplayer_extras.h"
#include "overworld.h"

STATIC_ASSERT(sizeof(struct MultiplayerExtrasMailbox) == MP_EXTRAS_MAILBOX_BYTES, ExtrasMailboxIs384Bytes);
STATIC_ASSERT(offsetof(struct MultiplayerExtrasMailbox, local) == 12, ExtrasLocalOffset);
STATIC_ASSERT(offsetof(struct MultiplayerExtrasMailbox, inboundGeneration) == 180, ExtrasInboundOffset);
STATIC_ASSERT(offsetof(struct MultiplayerExtrasMailbox, base) == 200, ExtrasInboundBaseOffset);

volatile struct MultiplayerExtrasMailbox gMultiplayerExtrasMailbox = {0};
const u32 gMultiplayerExtrasAbiVersion = MP_EXTRAS_ABI_VERSION;

static u8 sPublished[MP_EXTRAS_BASE_BYTES];
static bool8 sPublishedHas;
static bool8 sHavePublished;
static u32 sSeenGeneration;
static u32 sAppliedRecordSeq;
static bool8 sWasConnected;

void MultiplayerExtras_Init(void)
{
    memset((void *)&gMultiplayerExtrasMailbox, 0, sizeof(gMultiplayerExtrasMailbox));
    gMultiplayerExtrasMailbox.magic = MP_EXTRAS_MAGIC;
    gMultiplayerExtrasMailbox.abiVersion = MP_EXTRAS_ABI_VERSION;
    gMultiplayerExtrasMailbox.byteSize = MP_EXTRAS_MAILBOX_BYTES;
    memset(sPublished, 0, sizeof(sPublished));
    sPublishedHas = sHavePublished = sWasConnected = FALSE;
    sSeenGeneration = 0;
    sAppliedRecordSeq = 0;
}

static bool32 SaveLoaded(void)
{
    return gSaveBlock1Ptr != NULL && gMapHeader.mapLayout != NULL;
}

// Publish the player's own base when it differs from the last one published.
static void PublishOwnBase(void)
{
    u8 current[MP_EXTRAS_BASE_BYTES];
    bool32 has = MultiplayerSecretBase_GetOwn(current);
    u32 generation, seq;

    if (sHavePublished && has == sPublishedHas && memcmp(current, sPublished, sizeof(current)) == 0)
        return;
    seq = gMultiplayerExtrasMailbox.local.baseSeq + 1;
    // The first publication after a (soft) reset starts at a number of its own: a companion that
    // saw the previous game's last number must not take this one for it and skip it.
    if (gMultiplayerExtrasMailbox.local.baseSeq == 0)
        seq = (gMain.vblankCounter1 * 2654435761u) ^ 0x5EED0002u;
    if (seq == 0)
        seq = 1;
    generation = (gMultiplayerExtrasMailbox.localGeneration + 1) | 1;
    gMultiplayerExtrasMailbox.localGeneration = generation;
    gMultiplayerExtrasMailbox.local.baseSeq = seq;
    gMultiplayerExtrasMailbox.local.hasBase = has;
    memcpy((void *)gMultiplayerExtrasMailbox.local.base, current, sizeof(current));
    gMultiplayerExtrasMailbox.localGeneration = generation + 1;
    memcpy(sPublished, current, sizeof(current));
    sPublishedHas = has;
    sHavePublished = TRUE;
}

// Apply the base the host handed over, once per record number.
static void ApplyInbound(void)
{
    u8 base[MP_EXTRAS_BASE_BYTES];
    u32 generation = gMultiplayerExtrasMailbox.inboundGeneration;
    u32 flags, recordSeq;

    if ((generation & 1) || generation == sSeenGeneration)
        return;
    flags = gMultiplayerExtrasMailbox.flags;
    recordSeq = gMultiplayerExtrasMailbox.recordSeq;
    memcpy(base, (const void *)gMultiplayerExtrasMailbox.base, sizeof(base));
    if (generation != gMultiplayerExtrasMailbox.inboundGeneration)
        return;
    sSeenGeneration = generation;
    if (!(flags & MP_EXTRAS_FLAG_ENABLED) || recordSeq == 0 || recordSeq == sAppliedRecordSeq)
        return;
    sAppliedRecordSeq = recordSeq;
    MultiplayerSecretBase_Receive(base);
}

void MultiplayerExtras_Tick(bool32 fieldReady, bool32 connected)
{
    (void)fieldReady;
    if (!SaveLoaded())
        return;
    if (!connected)
    {
        // Disconnected: nothing is applied; the next connection publishes the base afresh.
        sWasConnected = FALSE;
        return;
    }
    if (!sWasConnected)
    {
        sWasConnected = TRUE;
        sHavePublished = FALSE; // publish the base at every (re)connection
    }
    PublishOwnBase();
    ApplyInbound();
}
#endif // PORTABLE
