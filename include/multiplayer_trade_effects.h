#ifndef GUARD_MULTIPLAYER_TRADE_EFFECTS_H
#define GUARD_MULTIPLAYER_TRADE_EFFECTS_H

#include "pokemon.h"

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
struct SaveBlock1;
struct SaveBlock2;
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

// Internal candidate API, NOT a mailbox ABI or permission to publish effects.
// The owner must supply friendship resolved from authenticated retained history.
// Unknown handler history/geographic policy must never be replaced by a guess.
#define MP_TRADE_RECORD_VERSION 1
#define MP_TRADE_RECORD_MAX_CHOICES 16
#define MP_TRADE_RECORD_DECLINE 4

enum MultiplayerTradeRecordState
{
    MP_TRADE_RECORD_REFUSED,
    MP_TRADE_RECORD_CHOOSE_MOVE,
    MP_TRADE_RECORD_READY,
};

struct MultiplayerTradeMoveChoice
{
    u16 move;
    u8 slot; // 0..3 replaces that move; 4 explicitly declines learning.
    u8 reserved;
};

// Caller-owned scratch, never a pointer into a live party/save. Copy/export only
// through FinishRecord. Its state and candidate are internal, not client input.
struct MultiplayerTradeRecordPreparation
{
    u32 version;
    u32 receivedSerial;
    u32 sentSerial;
    u16 receivedSpecies;
    u16 evolvedSpecies; // NONE means no trade evolution applies.
    u16 itemBefore;
    u16 itemAfter;
    u16 pendingMove;
    u16 learnsetCursor;
    u8 state;
    u8 friendship;
    u8 choiceCount;
    u8 reserved;
    struct Pokemon candidate;
    struct MultiplayerTradeMoveChoice choices[MP_TRADE_RECORD_MAX_CHOICES];
};

// Operates on copies only. No field/party mutation, Dex/stat publication, link
// callbacks, native save, RNG draw or heap allocation. The current pinned native
// rules are reused; this does not certify the complete Gen6 configuration.
bool32 MultiplayerTradeEffects_BeginRecord(const struct Pokemon *received,
                                         const struct Pokemon *sent,
                                         u32 resolvedFriendship,
                                         struct MultiplayerTradeRecordPreparation *out);
// Exact move + zero-based choice sequence reject stale or duplicated decisions.
// Invalid input leaves the current candidate and pending choice unchanged.
bool32 MultiplayerTradeEffects_ChooseMove(struct MultiplayerTradeRecordPreparation *state,
                                        u32 sequence, u32 move, u32 slot);
bool32 MultiplayerTradeEffects_FinishRecord(const struct MultiplayerTradeRecordPreparation *state,
                                          struct Pokemon *out);
// Read-only UI facts. Four current slots plus the pending move, all read from
// private scratch and the pinned move table. Names remain presentation data.
struct MultiplayerTradeMoveInfo
{
    u16 move;
    u8 pp;
    u8 maxPp;
    u16 power;
    u8 accuracy;
    u8 type;
    u8 category;
    u8 flags; // bit0: native CannotForgetMove; other bits zero.
    u16 reserved;
};
struct MultiplayerTradeChoiceView
{
    u32 version;
    u32 receivedSerial;
    u32 sentSerial;
    u32 recordCrc; // Filled by the held mailbox over the exact 192-byte record.
    u16 pendingMove;
    u16 learnsetCursor;
    u8 state;
    u8 choiceCount;
    u8 replaceMask;
    u8 reserved;
    struct MultiplayerTradeMoveInfo moves[5];
};
bool32 MultiplayerTradeEffects_DescribeRecord(const struct MultiplayerTradeRecordPreparation *state,
                                             struct MultiplayerTradeChoiceView *out);
// Deterministic replay requires every decision exactly once, no trailing choices.
bool32 MultiplayerTradeEffects_ReplayRecord(const struct Pokemon *received,
                                          const struct Pokemon *sent,
                                          u32 resolvedFriendship,
                                          const struct MultiplayerTradeMoveChoice *choices,
                                          u32 count, struct Pokemon *out);

// Private saved-block preparation. Inputs are the exact frozen recipient and
// counterpart blocks; output buffers must be distinct caller-owned copies.
// Replays the record first, then received/evolved Dex, encrypted saturated
// counters and ribbon presentation. Failure leaves both outputs untouched.
// This does not write Flash, bind a transaction, increment the save counter,
// arm a save, choose missing handler policy or enable an effects capability.
bool32 MultiplayerTradeEffects_ReplaySaveBlocks(const struct SaveBlock1 *recipient1,
                                               const struct SaveBlock2 *recipient2,
                                               const struct SaveBlock1 *partner1,
                                               u32 outgoingSlot, u32 incomingSlot,
                                               u32 resolvedFriendship,
                                               const struct MultiplayerTradeMoveChoice *choices,
                                               u32 count, struct SaveBlock1 *out1,
                                               struct SaveBlock2 *out2);

// Overlay one original saved-block slice in private readback memory. The
// original bytes must match the frozen recipient block exactly. This reuses
// the READY record and bounded original partner ribbon descriptions; it does
// not need a second full SaveBlock allocation or modify the live source.
bool32 MultiplayerTradeEffects_OverlaySaveSlice(const struct SaveBlock1 *recipient1,
                                              const struct SaveBlock2 *recipient2,
                                              const struct Pokemon *incoming,
                                              const struct MultiplayerTradeRecordPreparation *prepared,
                                              const u8 *giftRibbons, u32 outgoingSlot,
                                              u32 block, u32 offset, u32 size, u8 *out);

#endif // GUARD_MULTIPLAYER_TRADE_EFFECTS_H
