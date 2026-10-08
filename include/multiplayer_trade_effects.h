#ifndef GUARD_MULTIPLAYER_TRADE_EFFECTS_H
#define GUARD_MULTIPLAYER_TRADE_EFFECTS_H

#define MP_TRADE_EFFECT_PREVIEW_VERSION 1

// This observation never authorizes an offer, mutation, export or commit.
// The pinned profile and recipient handler history still require validation.
enum MultiplayerTradeEffectFlags
{
    MP_TRADE_EFFECT_POLICY_REQUIRED = (1 << 0),
    MP_TRADE_EFFECT_INVALID_RECORD = (1 << 1),
    MP_TRADE_EFFECT_EGG = (1 << 2),
    MP_TRADE_EFFECT_MAIL = (1 << 3),
    MP_TRADE_EFFECT_SPECIES_REVIEW = (1 << 4),
    MP_TRADE_EFFECT_NATIVE_FORBIDDEN = (1 << 5),
    MP_TRADE_EFFECT_EVOLUTION = (1 << 6),
    MP_TRADE_EFFECT_HANDLER_FRIENDSHIP = (1 << 7),
    MP_TRADE_EFFECT_FRIENDSHIP_PROFILE = (1 << 8),
    MP_TRADE_EFFECT_RETURN_FRUSTRATION = (1 << 9),
};

struct Pokemon;
struct MultiplayerTradeEffectPreview
{
    u32 version;
    u32 flags;
    u32 receivedSerial;
    u32 sentSerial;
    u16 receivedSpecies;
    u16 sentSpecies;
    u16 evolutionTarget;
    u16 heldItem;
    u8 currentFriendship;
    u8 observedBaseFriendship;
    u8 friendshipGeneration;
    u8 reserved;
    u32 receivedPersonality;
    u32 receivedOtId;
};

// CHECK_EVO operates only on copies. No link callback, RNG draw, item
// consumption, friendship write, Pokédex update or native save occurs.
// Call only inside initialized native game context; the existing evaluator
// reads weather/item context. Inputs must be valid addressable party records.
// Return FALSE for malformed/unassigned/duplicate records or aliased output.
bool32 MultiplayerTradeEffects_Preview(const struct Pokemon *received,
                                      const struct Pokemon *sent,
                                      struct MultiplayerTradeEffectPreview *out);

#endif // GUARD_MULTIPLAYER_TRADE_EFFECTS_H
