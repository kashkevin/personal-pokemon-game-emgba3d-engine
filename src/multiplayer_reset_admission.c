#include "global.h"
#include <stddef.h>
#include "multiplayer_reset_admission.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_save.h"
#include "multiplayer.h"
#include "script.h"
#include "event_data.h"

_Static_assert(sizeof(struct MultiplayerResetMailbox) == 128, "Personal reset ABI");
_Static_assert(offsetof(struct MultiplayerResetMailbox, request) == 16, "Personal request ABI");
_Static_assert(offsetof(struct MultiplayerResetMailbox, reply) == 52, "Personal reply ABI");
const u32 gMultiplayerResetAbiVersion = MP_RESET_ABI;
EWRAM_DATA volatile struct MultiplayerResetMailbox gMultiplayerResetMailbox = {0};
static EWRAM_DATA u32 sSequence, sGeneration;
static EWRAM_DATA bool8 sWaiting;

void MultiplayerResetAdmission_Init(void)
{
    memset((void *)&gMultiplayerResetMailbox, 0, sizeof(gMultiplayerResetMailbox));
    gMultiplayerResetMailbox.magic = MP_RESET_MAGIC;
    gMultiplayerResetMailbox.abi = MP_RESET_ABI;
    gMultiplayerResetMailbox.byteSize = sizeof(gMultiplayerResetMailbox);
    sSequence = sGeneration = 0;
    sWaiting = FALSE;
}

bool32 MultiplayerResetAdmission_IsWaiting(void) { return sWaiting; }

void MultiplayerResetAdmission_Begin(u32 encounter)
{
    u32 session, incarnation;
    gSpecialVar_Result = FALSE;
    if (sWaiting || sSequence == 0xFFFFFFFFu || !Multiplayer_IsConnected()
     || !MultiplayerSave_GetIdleBinding(&session, &incarnation)
     || !MultiplayerStaticReset_EntryReady(encounter)) return;
    gMultiplayerResetMailbox.localGeneration++;
    memset((void *)&gMultiplayerResetMailbox.request, 0, sizeof(gMultiplayerResetMailbox.request));
    gMultiplayerResetMailbox.request.sequence = ++sSequence;
    gMultiplayerResetMailbox.request.session = session;
    gMultiplayerResetMailbox.request.incarnation = incarnation;
    gMultiplayerResetMailbox.request.encounter = encounter;
    gMultiplayerResetMailbox.request.observedCount = MultiplayerStaticReset_Count();
    gMultiplayerResetMailbox.localGeneration++;
    sWaiting = TRUE;
    // The authored interaction resumes only after this exact durable ACK. No
    // timeout, disconnect, or malformed reply may silently generate a new roll.
    ScriptContext_Stop();
}

void MultiplayerResetAdmission_Tick(void)
{
    struct MultiplayerResetReply reply;
    u32 generation, session, incarnation;
    gMultiplayerResetMailbox.frame++;
    if (!sWaiting) return;
    generation = gMultiplayerResetMailbox.inboundGeneration;
    if ((generation & 1) || generation == sGeneration) return;
    memcpy(&reply, (const void *)&gMultiplayerResetMailbox.reply, sizeof(reply));
    if (generation != gMultiplayerResetMailbox.inboundGeneration) return;
    sGeneration = generation;
    if (reply.sequence != sSequence || reply.session != gMultiplayerResetMailbox.request.session
     || reply.incarnation != gMultiplayerResetMailbox.request.incarnation
     || reply.encounter != gMultiplayerResetMailbox.request.encounter || reply.status > 1) return;
    for (u32 i = 0; i < sizeof(reply.reserved); i++) if (reply.reserved[i]) return;
    if (!Multiplayer_IsConnected() || !MultiplayerSave_GetIdleBinding(&session, &incarnation)
     || session != reply.session || incarnation != reply.incarnation) return;
    if (reply.status == 0)
    {
        if (reply.attempt <= gMultiplayerResetMailbox.request.observedCount) return;
        if (!MultiplayerStaticReset_SetAdmission(reply.encounter, reply.attempt)) return;
        gSpecialVar_Result = MultiplayerLocalReset_TryBegin(reply.encounter);
        MultiplayerStaticReset_SetAdmission(0, 0);
    }
    else
    {
        if (reply.attempt) return;
        gSpecialVar_Result = FALSE;
    }
    sWaiting = FALSE;
    ScriptContext_Enable();
}
