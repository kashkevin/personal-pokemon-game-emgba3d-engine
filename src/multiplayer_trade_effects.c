#include "global.h"
#include "multiplayer_trade_effects.h"
#include "pokemon.h"
#include "mail.h"
#include "constants/moves.h"

STATIC_ASSERT(sizeof(struct MultiplayerTradeEffectPreview) == 36, TradeEffectPreviewSize);

static bool32 OutputAliasesMon(const struct MultiplayerTradeEffectPreview *out,
                              const struct Pokemon *mon)
{
    uintptr_t output = (uintptr_t)out;
    uintptr_t input = (uintptr_t)mon;
    return output < input + sizeof(*mon) && input < output + sizeof(*out);
}

static bool32 ReadMon(struct Pokemon *mon, u32 *serial, u16 *species)
{
    // GetMonData verifies encrypted content and can mark a bad record. Only
    // the private copy may receive that flag; the offered input stays exact.
    *species = GetMonData(mon, MON_DATA_SPECIES);
    *serial = GetMonData(mon, MON_DATA_MULTIPLAYER_ID);
    return *species != SPECIES_NONE && *species < NUM_SPECIES && *serial != 0
        && !GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG)
        && GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES);
}

bool32 MultiplayerTradeEffects_Preview(const struct Pokemon *received,
                                      const struct Pokemon *sent,
                                      struct MultiplayerTradeEffectPreview *out)
{
    struct Pokemon incoming, outgoing;
    struct MultiplayerTradeEffectPreview preview = {0};
    u32 incomingEgg, outgoingEgg;
    u32 i;

    if (out == NULL || (received != NULL && OutputAliasesMon(out, received))
     || (sent != NULL && OutputAliasesMon(out, sent)))
        return FALSE;
    preview.version = MP_TRADE_EFFECT_PREVIEW_VERSION;
    preview.flags = MP_TRADE_EFFECT_POLICY_REQUIRED | MP_TRADE_EFFECT_INVALID_RECORD;
    // Initialize output even on malformed content; never leave a stale plan.
    *out = preview;
    if (received == NULL || sent == NULL)
        return FALSE;
    incoming = *received;
    outgoing = *sent;
    if (!ReadMon(&incoming, &preview.receivedSerial, &preview.receivedSpecies)
     || !ReadMon(&outgoing, &preview.sentSerial, &preview.sentSpecies)
     || preview.receivedSerial == preview.sentSerial)
        return FALSE;

    preview.flags = MP_TRADE_EFFECT_POLICY_REQUIRED;
    preview.heldItem = GetMonData(&incoming, MON_DATA_HELD_ITEM);
    preview.currentFriendship = GetMonData(&incoming, MON_DATA_FRIENDSHIP);
    preview.observedBaseFriendship = gSpeciesInfo[preview.receivedSpecies].friendship;
    preview.friendshipGeneration = P_UPDATED_FRIENDSHIP;
    preview.receivedPersonality = GetMonData(&incoming, MON_DATA_PERSONALITY);
    preview.receivedOtId = GetMonData(&incoming, MON_DATA_OT_ID);
    incomingEgg = GetMonData(&incoming, MON_DATA_IS_EGG)
        || GetMonData(&incoming, MON_DATA_SANITY_IS_EGG);
    outgoingEgg = GetMonData(&outgoing, MON_DATA_IS_EGG)
        || GetMonData(&outgoing, MON_DATA_SANITY_IS_EGG);
    if (incomingEgg || outgoingEgg)
        preview.flags |= MP_TRADE_EFFECT_EGG;
    if (GetMonData(&incoming, MON_DATA_MAIL) != MAIL_NONE
     || GetMonData(&outgoing, MON_DATA_MAIL) != MAIL_NONE
     || ItemIsMail(preview.heldItem)
     || ItemIsMail(GetMonData(&outgoing, MON_DATA_HELD_ITEM)))
        preview.flags |= MP_TRADE_EFFECT_MAIL;
    if (preview.receivedSpecies > SPECIES_VOLCANION || preview.sentSpecies > SPECIES_VOLCANION)
        preview.flags |= MP_TRADE_EFFECT_SPECIES_REVIEW;
    if (gSpeciesInfo[preview.receivedSpecies].cannotBeTraded
     || gSpeciesInfo[preview.sentSpecies].cannotBeTraded)
        preview.flags |= MP_TRADE_EFFECT_NATIVE_FORBIDDEN;
    if (!incomingEgg)
    {
        preview.flags |= MP_TRADE_EFFECT_HANDLER_FRIENDSHIP;
        if (P_UPDATED_FRIENDSHIP != GEN_6)
            preview.flags |= MP_TRADE_EFFECT_FRIENDSHIP_PROFILE;
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            u32 move = GetMonData(&incoming, MON_DATA_MOVE1 + i);
            if (move == MOVE_RETURN || move == MOVE_FRUSTRATION)
                preview.flags |= MP_TRADE_EFFECT_RETURN_FRUSTRATION;
        }
        preview.evolutionTarget = GetEvolutionTargetSpecies(&incoming, EVO_MODE_TRADE,
                                      ITEM_NONE, &outgoing, NULL, CHECK_EVO);
        if (preview.evolutionTarget != SPECIES_NONE)
            preview.flags |= MP_TRADE_EFFECT_EVOLUTION;
    }
    *out = preview;
    return TRUE;
}
