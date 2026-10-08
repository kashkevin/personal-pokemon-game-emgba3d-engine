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
_Static_assert(offsetof(struct MultiplayerResetMailbox, receipt) == 84, "Personal receipt ABI");
const u32 gMultiplayerResetAbiVersion = MP_RESET_ABI;
EWRAM_DATA volatile struct MultiplayerResetMailbox gMultiplayerResetMailbox = {0};
static EWRAM_DATA u32 sSequence, sGeneration;
static EWRAM_DATA bool8 sWaiting;

void MultiplayerResetAdmission_Init(void)
{
    memset((void *)&gMultiplayerResetMailbox, 0, sizeof(gMultiplayerResetMailbox));
    gMultiplayerResetMailbox.magic = MP_RESET_MAGIC;
    gMultiplayerResetMailbox.abi = *(const volatile u32 *)&gMultiplayerResetAbiVersion;
    gMultiplayerResetMailbox.byteSize = sizeof(gMultiplayerResetMailbox);
    sSequence = sGeneration = 0;
    sWaiting = FALSE;
    MultiplayerStaticReset_InitCache();
}

bool32 MultiplayerResetAdmission_IsWaiting(void) { return sWaiting; }

static void PublishReceipt(u32 state, u32 attempt)
{
    gMultiplayerResetMailbox.localGeneration++;
    gMultiplayerResetMailbox.receipt.sequence = gMultiplayerResetMailbox.request.sequence;
    gMultiplayerResetMailbox.receipt.session = gMultiplayerResetMailbox.request.session;
    gMultiplayerResetMailbox.receipt.incarnation = gMultiplayerResetMailbox.request.incarnation;
    gMultiplayerResetMailbox.receipt.encounter = gMultiplayerResetMailbox.request.encounter;
    gMultiplayerResetMailbox.receipt.attempt = attempt;
    gMultiplayerResetMailbox.receipt.state = state;
    gMultiplayerResetMailbox.receipt.fieldReady = FALSE;
    gMultiplayerResetMailbox.localGeneration++;
}

void MultiplayerResetAdmission_Begin(u32 encounter)
{
    u32 session, incarnation;
    gSpecialVar_Result = FALSE;
    if (sWaiting || sSequence == 0xFFFFFFFFu || !Multiplayer_IsConnected()
     || !MultiplayerSave_GetIdleBinding(&session, &incarnation)
     || !MultiplayerLocalReset_CanBegin(encounter)) return;
    gMultiplayerResetMailbox.localGeneration++;
    memset((void *)&gMultiplayerResetMailbox.request, 0, sizeof(gMultiplayerResetMailbox.request));
    gMultiplayerResetMailbox.request.sequence = ++sSequence;
    gMultiplayerResetMailbox.request.session = session;
    gMultiplayerResetMailbox.request.incarnation = incarnation;
    gMultiplayerResetMailbox.request.encounter = encounter;
    gMultiplayerResetMailbox.request.observedCount = MultiplayerStaticReset_GetCount(encounter);
    gMultiplayerResetMailbox.localGeneration++;
    sWaiting = TRUE;
    PublishReceipt(MP_RESET_WAITING, 0);
    // The authored interaction resumes only after this exact durable ACK. No
    // timeout, disconnect, or malformed reply may silently generate a new roll.
    ScriptContext_Stop();
}

void MultiplayerResetAdmission_Tick(void)
{
    struct MultiplayerResetReply reply;
    u32 generation, session, incarnation;
    gMultiplayerResetMailbox.frame++;
    if (!sWaiting)
    {
        // A neutral response is not permission to export while its authored
        // script still holds controls. Observe the same safe point as saving.
        u32 state = gMultiplayerResetMailbox.receipt.state;
        u32 ready = (state == MP_RESET_DENIED || state == MP_RESET_CANCELLED)
            && MultiplayerSave_FieldReady();
        if (ready != gMultiplayerResetMailbox.receipt.fieldReady)
        {
            gMultiplayerResetMailbox.localGeneration++;
            gMultiplayerResetMailbox.receipt.fieldReady = ready;
            gMultiplayerResetMailbox.localGeneration++;
        }
        return;
    }
    generation = gMultiplayerResetMailbox.inboundGeneration;
    if ((generation & 1) || generation == sGeneration) return;
    memcpy(&reply, (const void *)&gMultiplayerResetMailbox.reply, sizeof(reply));
    if (generation != gMultiplayerResetMailbox.inboundGeneration) return;
    sGeneration = generation;
    if (reply.sequence != sSequence || reply.session != gMultiplayerResetMailbox.request.session
     || reply.incarnation != gMultiplayerResetMailbox.request.incarnation
     || reply.encounter != gMultiplayerResetMailbox.request.encounter || reply.status > 2) return;
    for (u32 i = 0; i < sizeof(reply.reserved); i++) if (reply.reserved[i]) return;
    if (!MultiplayerSave_GetIdleBinding(&session, &incarnation)
     || session != reply.session || incarnation != reply.incarnation) return;
    // Cancellation grants only neutral script cleanup to the exact retained
    // disconnected core. It cannot change a roll/count or reverse a prior ACK.
    if (reply.status == 2)
    {
        if (Multiplayer_IsConnected() || reply.attempt) return;
        gSpecialVar_Result = FALSE;
        sWaiting = FALSE;
        PublishReceipt(MP_RESET_CANCELLED, 0);
        ScriptContext_Enable();
        return;
    }
    if (!Multiplayer_IsConnected()) return;
    if (reply.status == 0)
    {
        if (reply.attempt <= gMultiplayerResetMailbox.request.observedCount) return;
        if (!MultiplayerStaticReset_SetAdmission(reply.encounter, reply.attempt)) return;
        gSpecialVar_Result = MultiplayerLocalReset_TryBegin(reply.encounter);
        MultiplayerStaticReset_SetAdmission(0, 0);
        PublishReceipt(MP_RESET_ADMITTED, reply.attempt);
    }
    else
    {
        if (reply.attempt) return;
        gSpecialVar_Result = FALSE;
        PublishReceipt(MP_RESET_DENIED, 0);
    }
    sWaiting = FALSE;
    ScriptContext_Enable();
}
