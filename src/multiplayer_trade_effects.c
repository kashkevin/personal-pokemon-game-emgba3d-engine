#include "global.h"
#include "multiplayer_trade_effects.h"
#include "pokemon.h"
#include "mail.h"
#include "battle.h"
#include "move.h"
#include "malloc.h"
#include "string_util.h"
#include "constants/moves.h"
#include "constants/game_stat.h"
#include "constants/flags.h"

STATIC_ASSERT(sizeof(struct MultiplayerTradeEffectPreview) == 36, TradeEffectPreviewSize);
STATIC_ASSERT(sizeof(struct Pokemon) == 100, TradeEffectPartyRecordSize);
STATIC_ASSERT(sizeof(struct MultiplayerTradeRecordPreparation) == 192, TradeEffectRecordPreparationSize);
STATIC_ASSERT(sizeof(struct MultiplayerTradeMoveInfo) == 12, TradeEffectMoveInfoSize);
STATIC_ASSERT(sizeof(struct MultiplayerTradeChoiceView) == 84, TradeEffectChoiceViewSize);
// Exact profile consumed by trade_effect_flash.py. A changed layout must fail
// compilation rather than silently write another save field.
STATIC_ASSERT(sizeof(struct SaveBlock1) == 15568, TradeEffectSaveBlock1Size);
STATIC_ASSERT(sizeof(struct SaveBlock2) == 3884, TradeEffectSaveBlock2Size);
STATIC_ASSERT(offsetof(struct SaveBlock1, playerParty) == 0x238, TradeEffectPartyOffset);
STATIC_ASSERT(offsetof(struct SaveBlock1, flags) == 0x1270, TradeEffectFlagsOffset);
STATIC_ASSERT(offsetof(struct SaveBlock1, gameStats) == 0x159C, TradeEffectStatsOffset);
STATIC_ASSERT(offsetof(struct SaveBlock1, giftRibbons) == 0x31A8, TradeEffectGiftRibbonsOffset);
STATIC_ASSERT(offsetof(struct SaveBlock1, dexSeen) == 0x3598, TradeEffectDexSeenOffset);
STATIC_ASSERT(offsetof(struct SaveBlock1, dexCaught) == 0x3619, TradeEffectDexCaughtOffset);
STATIC_ASSERT(offsetof(struct SaveBlock2, encryptionKey) == 0xAC, TradeEffectKeyOffset);
STATIC_ASSERT(offsetof(struct SaveBlock2, pokedex.unownPersonality) == 0x1C, TradeEffectUnownOffset);
STATIC_ASSERT(offsetof(struct SaveBlock2, pokedex.spindaPersonality) == 0x20, TradeEffectSpindaOffset);
STATIC_ASSERT(NUM_DEX_FLAG_BYTES == 129 && NUM_GIFT_RIBBONS == 7 && MAX_GIFT_RIBBON == 64, TradeEffectArrays);
STATIC_ASSERT(FLAG_SYS_RIBBON_GET == 0x89B && GAME_STAT_POKEMON_TRADES == 21 && GAME_STAT_EVOLVED_POKEMON == 14, TradeEffectBookkeepingIds);

// A headless host VM may invoke only this fixed, copy-only operation. There is
// no general memory RPC. The descriptor is read from the exact verified ROM.
#define MP_TRADE_REPLAY_MAGIC 0x57525445
#define MP_TRADE_REPLAY_INPUT_MAX 35116
#define MP_TRADE_REPLAY_OUTPUT_MAX 19452
static u32 MultiplayerTradeReplay_Run(const u8 *input, u32 length, u8 *out, u32 capacity);
struct MultiplayerTradeReplayDescriptor
{
    u32 magic;
    u32 version;
    u32 (*run)(const u8 *, u32, u8 *, u32);
    u8 *heap;
    u32 heapSize;
    u32 inputMax;
    u32 outputMax;
    u32 reserved;
};
const struct MultiplayerTradeReplayDescriptor gMultiplayerTradeReplayDescriptor =
{
    MP_TRADE_REPLAY_MAGIC, 1, MultiplayerTradeReplay_Run, gHeap, HEAP_SIZE,
    MP_TRADE_REPLAY_INPUT_MAX, MP_TRADE_REPLAY_OUTPUT_MAX, 0
};
STATIC_ASSERT(sizeof(struct MultiplayerTradeReplayDescriptor) == 32, TradeReplayDescriptorSize);

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

    // Retain the descriptor and its single function under linker GC. Volatile
    // prevents the compiler from folding this reference to a constant.
    if (((const volatile struct MultiplayerTradeReplayDescriptor *)&gMultiplayerTradeReplayDescriptor)->magic != MP_TRADE_REPLAY_MAGIC)
        return FALSE;
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

// Bounds protect this internal adapter from a future incompatible rules table.
// They are refusal capacities, not new gameplay limits or truncated learnsets.
#define MP_TRADE_MAX_EVOLUTIONS 64
#define MP_TRADE_MAX_CONDITIONS 16
#define MP_TRADE_MAX_LEARNSET 256

static bool32 Overlaps(const void *left, u32 leftSize, const void *right, u32 rightSize)
{
    uintptr_t a = (uintptr_t)left, b = (uintptr_t)right;
    return a < b + rightSize && b < a + leftSize;
}

static bool32 IsRecordOutput(const void *out, u32 size, const struct Pokemon *received,
                            const struct Pokemon *sent)
{
    return out != NULL && received != NULL && sent != NULL
        && !Overlaps(out, size, received, sizeof(*received))
        && !Overlaps(out, size, sent, sizeof(*sent));
}

static bool32 ReplayHeapRange(const void *pointer, u32 length)
{
    uintptr_t start = (uintptr_t)pointer, base = (uintptr_t)gHeap;
    return start >= base && length <= HEAP_SIZE - 4
        && start - base <= HEAP_SIZE - 4 - length && (start & 3) == 0;
}

static u32 MultiplayerTradeReplay_Run(const u8 *input, u32 length, u8 *out, u32 capacity)
{
    const u32 *header;
    const struct MultiplayerTradeMoveChoice *choices;
    u32 operation, friendship, count, outSlot, inSlot, base, i;

    // Valid exclusively in an isolated, fresh VM. Even a correctly bounded
    // request cannot commandeer the live game's allocator or save blocks.
    if (gSaveBlock1Ptr != NULL || gSaveBlock2Ptr != NULL || gSaveBlock3Ptr != NULL
     || length < 32 || length > MP_TRADE_REPLAY_INPUT_MAX
     || capacity != MP_TRADE_REPLAY_OUTPUT_MAX
     || !ReplayHeapRange(input, length) || !ReplayHeapRange(out, capacity)
     || Overlaps(input, length, out, capacity))
        return 0;
    header = (const u32 *)input;
    operation = header[1];
    friendship = header[2];
    count = header[3];
    outSlot = header[4];
    inSlot = header[5];
    if (header[0] != 1 || friendship > 255 || count > MP_TRADE_RECORD_MAX_CHOICES
     || outSlot >= PARTY_SIZE || inSlot >= PARTY_SIZE || header[6] || header[7])
        return 0;
    if (operation == 1)
    {
        if (outSlot || inSlot) return 0;
        base = 32 + 2 * sizeof(struct Pokemon);
    }
    else if (operation == 2)
        base = 32 + 2 * sizeof(struct SaveBlock1) + sizeof(struct SaveBlock2);
    else
        return 0;
    if (length != base + count * sizeof(*choices)) return 0;
    choices = (const struct MultiplayerTradeMoveChoice *)(input + base);
    for (i = 0; i < count; i++)
        if (choices[i].move == MOVE_NONE || choices[i].slot > MAX_MON_MOVES || choices[i].reserved)
            return 0;
    if (operation == 1)
        return MultiplayerTradeEffects_ReplayRecord((const struct Pokemon *)(input + 32),
            (const struct Pokemon *)(input + 32 + sizeof(struct Pokemon)), friendship,
            choices, count, (struct Pokemon *)out) ? sizeof(struct Pokemon) : 0;
    return MultiplayerTradeEffects_ReplaySaveBlocks((const struct SaveBlock1 *)(input + 32),
        (const struct SaveBlock2 *)(input + 32 + sizeof(struct SaveBlock1)),
        (const struct SaveBlock1 *)(input + 32 + sizeof(struct SaveBlock1) + sizeof(struct SaveBlock2)),
        outSlot, inSlot, friendship, choices, count, (struct SaveBlock1 *)out,
        (struct SaveBlock2 *)(out + sizeof(struct SaveBlock1))) ? MP_TRADE_REPLAY_OUTPUT_MAX : 0;
}

static bool32 TradeConditionsAreLocal(u16 species)
{
    const struct Evolution *rules = GetSpeciesEvolutions(species);
    u32 i, j;
    if (rules == NULL) return TRUE;
    for (i = 0; i < MP_TRADE_MAX_EVOLUTIONS; i++)
    {
        const struct EvolutionParam *params;
        if (rules[i].method == EVOLUTIONS_END) return TRUE;
        if (rules[i].method != EVO_TRADE) continue;
        if (rules[i].targetSpecies == SPECIES_NONE || rules[i].targetSpecies > SPECIES_VOLCANION)
            return FALSE;
        params = rules[i].params;
        if (params == NULL) continue;
        for (j = 0; j < MP_TRADE_MAX_CONDITIONS; j++)
        {
            if (params[j].condition == CONDITIONS_END) break;
            // In particular, never let DO_EVO reach IF_BAG_ITEM_COUNT and
            // remove a real bag item while preparing only a copied record.
            if (params[j].condition != IF_HOLD_ITEM
             && params[j].condition != IF_TRADE_PARTNER_SPECIES)
                return FALSE;
        }
        if (j == MP_TRADE_MAX_CONDITIONS) return FALSE;
    }
    return FALSE;
}

static bool32 AdvanceRecord(struct MultiplayerTradeRecordPreparation *state)
{
    const struct LevelUpMove *learnset;
    u32 level, result;
    if (state->evolvedSpecies == SPECIES_NONE)
    {
        state->state = MP_TRADE_RECORD_READY;
        return TRUE;
    }
    learnset = GetSpeciesLevelUpLearnset(state->evolvedSpecies);
    if (learnset == NULL) return FALSE;
    level = GetMonData(&state->candidate, MON_DATA_LEVEL);
    while (state->learnsetCursor < MP_TRADE_MAX_LEARNSET)
    {
        const struct LevelUpMove *entry = &learnset[state->learnsetCursor++];
        if (entry->move == LEVEL_UP_MOVE_END)
        {
            state->pendingMove = MOVE_NONE;
            state->state = MP_TRADE_RECORD_READY;
            return TRUE;
        }
        // Same ordering/condition as MonTryLearningNewMoveEvolution, without
        // its shared sLearningMoveTableID or gMoveToLearn cursor.
        if ((entry->level != 0 && entry->level != level)
         || (P_EVOLUTION_LEVEL_1_LEARN >= GEN_8 && entry->level == 1))
            continue;
        if (entry->move == MOVE_NONE || entry->move >= MOVES_COUNT_GEN6) return FALSE;
        result = GiveMoveToMon(&state->candidate, entry->move);
        if (result == MON_HAS_MAX_MOVES)
        {
            if (state->choiceCount == MP_TRADE_RECORD_MAX_CHOICES) return FALSE;
            state->pendingMove = entry->move;
            state->state = MP_TRADE_RECORD_CHOOSE_MOVE;
            return TRUE;
        }
        // Known moves and automatic empty-slot learning are native outcomes;
        // neither fabricates a recipient decision nor consumes a choice slot.
    }
    return FALSE;
}

bool32 MultiplayerTradeEffects_BeginRecord(const struct Pokemon *received,
                                         const struct Pokemon *sent,
                                         u32 resolvedFriendship,
                                         struct MultiplayerTradeRecordPreparation *out)
{
    struct MultiplayerTradeRecordPreparation prepared = {0};
    struct MultiplayerTradeEffectPreview preview;
    struct Pokemon partner;
    u16 target;
    u32 i, zero = 0;
    u8 oldLevelUpHP;
    u8 nickname[POKEMON_NAME_BUFFER_SIZE];
    if (!IsRecordOutput(out, sizeof(*out), received, sent)) return FALSE;
    memset(out, 0, sizeof(*out));
    if (resolvedFriendship > MAX_FRIENDSHIP
     || !MultiplayerTradeEffects_Preview(received, sent, &preview)
     || (preview.flags & (MP_TRADE_EFFECT_EGG | MP_TRADE_EFFECT_MAIL
                       | MP_TRADE_EFFECT_SPECIES_REVIEW | MP_TRADE_EFFECT_NATIVE_FORBIDDEN))
     || !TradeConditionsAreLocal(preview.receivedSpecies))
        return FALSE;
    prepared.version = MP_TRADE_RECORD_VERSION;
    prepared.receivedSerial = preview.receivedSerial;
    prepared.sentSerial = preview.sentSerial;
    prepared.receivedSpecies = preview.receivedSpecies;
    prepared.itemBefore = preview.heldItem;
    prepared.friendship = resolvedFriendship;
    prepared.candidate = *received;
    partner = *sent;
    for (i = 0; i < MAX_MON_MOVES; i++)
        if (GetMonData(&prepared.candidate, MON_DATA_MOVE1 + i) >= MOVES_COUNT_GEN6
         || GetMonData(&partner, MON_DATA_MOVE1 + i) >= MOVES_COUNT_GEN6)
            return FALSE;
    SetMonData(&prepared.candidate, MON_DATA_FRIENDSHIP, &prepared.friendship);
    target = GetEvolutionTargetSpecies(&prepared.candidate, EVO_MODE_TRADE,
                                       ITEM_NONE, &partner, NULL, CHECK_EVO);
    if (target != SPECIES_NONE)
    {
        if (target > SPECIES_VOLCANION || gSpeciesInfo[target].cannotBeTraded
         || gSpeciesInfo[target].growthRate != gSpeciesInfo[preview.receivedSpecies].growthRate
         || GetLevelFromMonExp(&prepared.candidate) != GetMonData(&prepared.candidate, MON_DATA_LEVEL)
         || GetEvolutionTargetSpecies(&prepared.candidate, EVO_MODE_TRADE,
                                      ITEM_NONE, &partner, NULL, DO_EVO) != target)
            return FALSE;
        prepared.evolvedSpecies = target;
        SetMonData(&prepared.candidate, MON_DATA_SPECIES, &target);
        SetMonData(&prepared.candidate, MON_DATA_EVOLUTION_TRACKER, &zero);
        // CalculateMonStats writes this one shared scratch byte. Restore it;
        // no battle renderer/controller receives this private preparation.
        oldLevelUpHP = gBattleScripting.levelUpHP;
        CalculateMonStats(&prepared.candidate);
        gBattleScripting.levelUpHP = oldLevelUpHP;
        // EvolutionRenameMon uses gStringVar1; perform its exact name/language
        // check locally instead of disturbing a retained native menu string.
        GetMonData(&prepared.candidate, MON_DATA_NICKNAME, nickname);
        if (GetMonData(&prepared.candidate, MON_DATA_LANGUAGE) == GAME_LANGUAGE
         && !StringCompare(GetSpeciesName(preview.receivedSpecies), nickname))
            SetMonData(&prepared.candidate, MON_DATA_NICKNAME, GetSpeciesName(target));
    }
    prepared.itemAfter = GetMonData(&prepared.candidate, MON_DATA_HELD_ITEM);
    if (!AdvanceRecord(&prepared)) return FALSE;
    *out = prepared;
    return TRUE;
}

bool32 MultiplayerTradeEffects_ChooseMove(struct MultiplayerTradeRecordPreparation *state,
                                        u32 sequence, u32 move, u32 slot)
{
    struct MultiplayerTradeRecordPreparation next;
    if (state == NULL || state->version != MP_TRADE_RECORD_VERSION
     || state->state != MP_TRADE_RECORD_CHOOSE_MOVE
     || sequence != state->choiceCount || sequence >= MP_TRADE_RECORD_MAX_CHOICES
     || move != state->pendingMove || move == MOVE_NONE || slot > MP_TRADE_RECORD_DECLINE)
        return FALSE;
    next = *state;
    if (slot < MAX_MON_MOVES)
    {
        if (CannotForgetMove(GetMonData(&next.candidate, MON_DATA_MOVE1 + slot))) return FALSE;
        RemoveMonPPBonus(&next.candidate, slot);
        SetMonMoveSlot(&next.candidate, move, slot);
    }
    next.choices[next.choiceCount++] = (struct MultiplayerTradeMoveChoice){move, slot, 0};
    next.pendingMove = MOVE_NONE;
    if (!AdvanceRecord(&next)) return FALSE;
    *state = next;
    return TRUE;
}

bool32 MultiplayerTradeEffects_DescribeRecord(const struct MultiplayerTradeRecordPreparation *state,
                                             struct MultiplayerTradeChoiceView *out)
{
    struct MultiplayerTradeChoiceView view = {0};
    struct Pokemon candidate;
    u32 i, serial;
    u16 species;
    if (state == NULL || out == NULL || Overlaps(out, sizeof(*out), state, sizeof(*state))
     || state->version != MP_TRADE_RECORD_VERSION || state->choiceCount > MP_TRADE_RECORD_MAX_CHOICES
     || (state->state != MP_TRADE_RECORD_CHOOSE_MOVE && state->state != MP_TRADE_RECORD_READY)
     || (state->state == MP_TRADE_RECORD_CHOOSE_MOVE && (state->pendingMove == MOVE_NONE || state->pendingMove > MOVE_HYPERSPACE_FURY
          || state->choiceCount >= MP_TRADE_RECORD_MAX_CHOICES))
     || (state->state == MP_TRADE_RECORD_READY && state->pendingMove != MOVE_NONE))
        return FALSE;
    candidate = state->candidate;
    if (!ReadMon(&candidate, &serial, &species) || serial != state->receivedSerial
     || species != (state->evolvedSpecies ? state->evolvedSpecies : state->receivedSpecies))
        return FALSE;
    view.version = 1;
    view.receivedSerial = state->receivedSerial;
    view.sentSerial = state->sentSerial;
    view.pendingMove = state->pendingMove;
    view.learnsetCursor = state->learnsetCursor;
    view.state = state->state;
    view.choiceCount = state->choiceCount;
    for (i = 0; i < ARRAY_COUNT(view.moves); i++)
    {
        struct MultiplayerTradeMoveInfo *info = &view.moves[i];
        u32 move = i < MAX_MON_MOVES ? GetMonData(&candidate, MON_DATA_MOVE1 + i) : state->pendingMove;
        if (move > MOVE_HYPERSPACE_FURY) return FALSE;
        if (move == MOVE_NONE) continue;
        info->move = move;
        info->pp = i < MAX_MON_MOVES ? GetMonData(&candidate, MON_DATA_PP1 + i) : GetMovePP(move);
        info->maxPp = i < MAX_MON_MOVES ? CalculatePPWithBonus(move, GetMonData(&candidate, MON_DATA_PP_BONUSES), i) : GetMovePP(move);
        info->power = GetMovePower(move);
        info->accuracy = GetMoveAccuracy(move);
        info->type = GetMoveType(move);
        info->category = GetMoveCategory(move);
        if (i < MAX_MON_MOVES)
        {
            info->flags = CannotForgetMove(move) ? 1 : 0;
            if (!info->flags && state->state == MP_TRADE_RECORD_CHOOSE_MOVE)
                view.replaceMask |= 1 << i;
        }
    }
    *out = view;
    return TRUE;
}

bool32 MultiplayerTradeEffects_FinishRecord(const struct MultiplayerTradeRecordPreparation *state,
                                          struct Pokemon *out)
{
    struct Pokemon candidate;
    u32 serial;
    u16 species;
    if (state == NULL || out == NULL || Overlaps(out, sizeof(*out), state, sizeof(*state))
     || state->version != MP_TRADE_RECORD_VERSION || state->state != MP_TRADE_RECORD_READY
     || state->pendingMove != MOVE_NONE || state->choiceCount > MP_TRADE_RECORD_MAX_CHOICES)
        return FALSE;
    candidate = state->candidate;
    if (!ReadMon(&candidate, &serial, &species) || serial != state->receivedSerial
     || species != (state->evolvedSpecies ? state->evolvedSpecies : state->receivedSpecies))
        return FALSE;
    *out = candidate;
    return TRUE;
}

bool32 MultiplayerTradeEffects_ReplayRecord(const struct Pokemon *received,
                                          const struct Pokemon *sent,
                                          u32 resolvedFriendship,
                                          const struct MultiplayerTradeMoveChoice *choices,
                                          u32 count, struct Pokemon *out)
{
    struct MultiplayerTradeRecordPreparation prepared;
    struct Pokemon candidate;
    u32 i;
    if (!IsRecordOutput(out, sizeof(*out), received, sent)
     || count > MP_TRADE_RECORD_MAX_CHOICES || (count && choices == NULL)
     || (count && Overlaps(out, sizeof(*out), choices, count * sizeof(*choices))))
        return FALSE;
    if (!MultiplayerTradeEffects_BeginRecord(received, sent, resolvedFriendship, &prepared))
        return FALSE;
    for (i = 0; i < count; i++)
        if (choices[i].reserved || !MultiplayerTradeEffects_ChooseMove(&prepared, i, choices[i].move, choices[i].slot))
            return FALSE;
    if (!MultiplayerTradeEffects_FinishRecord(&prepared, &candidate)) return FALSE;
    *out = candidate;
    return TRUE;
}

static bool32 SaveOutputIsPrivate(const void *out, u32 size,
                                 const struct SaveBlock1 *recipient1,
                                 const struct SaveBlock2 *recipient2,
                                 const struct SaveBlock1 *partner1,
                                 const struct MultiplayerTradeMoveChoice *choices, u32 count)
{
    return out != NULL
        && !Overlaps(out, size, recipient1, sizeof(*recipient1))
        && !Overlaps(out, size, recipient2, sizeof(*recipient2))
        && !Overlaps(out, size, partner1, sizeof(*partner1))
        && !(count && Overlaps(out, size, choices, count * sizeof(*choices)))
        && !(gSaveBlock1Ptr && Overlaps(out, size, gSaveBlock1Ptr, sizeof(*gSaveBlock1Ptr)))
        && !(gSaveBlock2Ptr && Overlaps(out, size, gSaveBlock2Ptr, sizeof(*gSaveBlock2Ptr)))
        && !(gSaveBlock3Ptr && Overlaps(out, size, gSaveBlock3Ptr, sizeof(*gSaveBlock3Ptr)))
        && !Overlaps(out, size, gParties, sizeof(gParties));
}

static void IncrementPrivateStat(struct SaveBlock1 *block, u32 key, u32 index)
{
    u32 value = block->gameStats[index] ^ key;
    block->gameStats[index] = (value < 0xFFFFFF ? value + 1 : 0xFFFFFF) ^ key;
}

static void OverlayBytes(u8 *out, u32 offset, u32 size, u32 target, const void *data, u32 length)
{
    u32 start = max(offset, target), end = min(offset + size, target + length);
    if (start < end) memcpy(out + start - offset, (const u8 *)data + start - target, end - start);
}

static void OverlayBit(u8 *out, u32 offset, u32 size, u32 target, u32 bit)
{
    if (target >= offset && target - offset < size) out[target - offset] |= bit;
}

bool32 MultiplayerTradeEffects_OverlaySaveSlice(const struct SaveBlock1 *recipient1,
                                              const struct SaveBlock2 *recipient2,
                                              const struct Pokemon *incoming,
                                              const struct MultiplayerTradeRecordPreparation *prepared,
                                              const u8 *giftRibbons, u32 outgoingSlot,
                                              u32 block, u32 offset, u32 size, u8 *out)
{
    struct Pokemon received, sent, candidate;
    const u8 *source;
    u32 blockSize, dex, finalDex, species, finalSpecies, personality, index, mask, value, ribbons = 0;
    if (recipient1 == NULL || recipient2 == NULL || incoming == NULL || prepared == NULL
     || giftRibbons == NULL || out == NULL || block < 1 || block > 2 || !size || size > 3968
     || recipient1->playerPartyCount > PARTY_SIZE || outgoingSlot >= recipient1->playerPartyCount)
        return FALSE;
    blockSize = block == 1 ? sizeof(*recipient1) : sizeof(*recipient2);
    source = block == 1 ? (const u8 *)recipient1 : (const u8 *)recipient2;
    if (offset > blockSize || size > blockSize - offset
     || !SaveOutputIsPrivate(out, size, recipient1, recipient2, recipient1, NULL, 0)
     || Overlaps(out, size, incoming, sizeof(*incoming))
     || Overlaps(out, size, prepared, sizeof(*prepared))
     || Overlaps(out, size, giftRibbons, NUM_GIFT_RIBBONS)
     || memcmp(out, source + offset, size)
     || !MultiplayerTradeEffects_FinishRecord(prepared, &candidate))
        return FALSE;
    received = *incoming;
    sent = recipient1->playerParty[outgoingSlot];
    species = GetMonData(&received, MON_DATA_SPECIES);
    finalSpecies = GetMonData(&candidate, MON_DATA_SPECIES);
    dex = SpeciesToNationalPokedexNum(species);
    finalDex = SpeciesToNationalPokedexNum(finalSpecies);
    if (!dex || !finalDex || dex != species || finalDex != finalSpecies
     || dex > SPECIES_VOLCANION || finalDex > SPECIES_VOLCANION
     || GetMonData(&received, MON_DATA_MULTIPLAYER_ID) != prepared->receivedSerial
     || GetMonData(&sent, MON_DATA_MULTIPLAYER_ID) != prepared->sentSerial)
        return FALSE;
    personality = GetMonData(&received, MON_DATA_PERSONALITY);
    for (index = MON_DATA_CHAMPION_RIBBON; index <= MON_DATA_WORLD_RIBBON; index++)
        ribbons += GetMonData(&received, index);
    // All failure checks precede writes. The recipient source remains immutable.
    if (block == 1)
    {
        OverlayBytes(out, offset, size, offsetof(struct SaveBlock1, playerParty)
                     + outgoingSlot * sizeof(candidate), &candidate, sizeof(candidate));
        index = (dex - 1) / 8; mask = 1 << ((dex - 1) % 8);
        OverlayBit(out, offset, size, offsetof(struct SaveBlock1, dexSeen) + index, mask);
        OverlayBit(out, offset, size, offsetof(struct SaveBlock1, dexCaught) + index, mask);
        if (species != finalSpecies)
        {
            index = (finalDex - 1) / 8; mask = 1 << ((finalDex - 1) % 8);
            OverlayBit(out, offset, size, offsetof(struct SaveBlock1, dexSeen) + index, mask);
            OverlayBit(out, offset, size, offsetof(struct SaveBlock1, dexCaught) + index, mask);
            value = recipient1->gameStats[GAME_STAT_EVOLVED_POKEMON] ^ recipient2->encryptionKey;
            value = (value < 0xFFFFFF ? value + 1 : 0xFFFFFF) ^ recipient2->encryptionKey;
            OverlayBytes(out, offset, size, offsetof(struct SaveBlock1, gameStats)
                         + GAME_STAT_EVOLVED_POKEMON * 4, &value, 4);
        }
        value = recipient1->gameStats[GAME_STAT_POKEMON_TRADES] ^ recipient2->encryptionKey;
        value = (value < 0xFFFFFF ? value + 1 : 0xFFFFFF) ^ recipient2->encryptionKey;
        OverlayBytes(out, offset, size, offsetof(struct SaveBlock1, gameStats)
                     + GAME_STAT_POKEMON_TRADES * 4, &value, 4);
        if (ribbons) OverlayBit(out, offset, size, offsetof(struct SaveBlock1, flags)
                               + FLAG_SYS_RIBBON_GET / 8, 1 << (FLAG_SYS_RIBBON_GET % 8));
        for (index = 0; index < NUM_GIFT_RIBBONS; index++)
            if (!recipient1->giftRibbons[index] && giftRibbons[index] && giftRibbons[index] < MAX_GIFT_RIBBON)
                OverlayBytes(out, offset, size, offsetof(struct SaveBlock1, giftRibbons) + index, giftRibbons + index, 1);
    }
    else
    {
        index = (dex - 1) / 8; mask = 1 << ((dex - 1) % 8);
        if (!(recipient1->dexCaught[index] & mask))
        {
            if (species == SPECIES_UNOWN)
                OverlayBytes(out, offset, size, offsetof(struct SaveBlock2, pokedex.unownPersonality), &personality, 4);
            if (species == SPECIES_SPINDA)
                OverlayBytes(out, offset, size, offsetof(struct SaveBlock2, pokedex.spindaPersonality), &personality, 4);
        }
    }
    return TRUE;
}

bool32 MultiplayerTradeEffects_ReplaySaveBlocks(const struct SaveBlock1 *recipient1,
                                               const struct SaveBlock2 *recipient2,
                                               const struct SaveBlock1 *partner1,
                                               u32 outgoingSlot, u32 incomingSlot,
                                               u32 resolvedFriendship,
                                               const struct MultiplayerTradeMoveChoice *choices,
                                               u32 count, struct SaveBlock1 *out1,
                                               struct SaveBlock2 *out2)
{
    struct Pokemon incoming, candidate;
    u32 receivedSpecies, finalSpecies, dex, finalDex, personality, index, mask, ribbons = 0;
    if (recipient1 == NULL || recipient2 == NULL || partner1 == NULL || recipient1 == partner1
     || recipient1->playerPartyCount > PARTY_SIZE || partner1->playerPartyCount > PARTY_SIZE
     || outgoingSlot >= recipient1->playerPartyCount || incomingSlot >= partner1->playerPartyCount
     || count > MP_TRADE_RECORD_MAX_CHOICES || (count && choices == NULL)
     || !SaveOutputIsPrivate(out1, sizeof(*out1), recipient1, recipient2, partner1, choices, count)
     || !SaveOutputIsPrivate(out2, sizeof(*out2), recipient1, recipient2, partner1, choices, count)
     || Overlaps(out1, sizeof(*out1), out2, sizeof(*out2)))
        return FALSE;
    if (!MultiplayerTradeEffects_ReplayRecord(&partner1->playerParty[incomingSlot],
        &recipient1->playerParty[outgoingSlot], resolvedFriendship, choices, count, &candidate))
        return FALSE;
    incoming = partner1->playerParty[incomingSlot];
    receivedSpecies = GetMonData(&incoming, MON_DATA_SPECIES);
    finalSpecies = GetMonData(&candidate, MON_DATA_SPECIES);
    dex = SpeciesToNationalPokedexNum(receivedSpecies);
    finalDex = SpeciesToNationalPokedexNum(finalSpecies);
    // This save adapter is for the standard 1..721 profile, not arbitrary forms.
    if (dex != receivedSpecies || finalDex != finalSpecies || !dex || !finalDex
     || dex > SPECIES_VOLCANION || finalDex > SPECIES_VOLCANION)
        return FALSE;
    personality = GetMonData(&incoming, MON_DATA_PERSONALITY);
    // Preserve the exact native trade's presentation range; contest ranks are
    // outside MON_DATA_CHAMPION_RIBBON..WORLD_RIBBON in this engine.
    for (index = MON_DATA_CHAMPION_RIBBON; index <= MON_DATA_WORLD_RIBBON; index++)
        ribbons += GetMonData(&incoming, index);

    // No failing operations follow these writes. Both complete outputs remain
    // private; the owner must validate/publish the pair in one transaction.
    memcpy(out1, recipient1, sizeof(*out1));
    memcpy(out2, recipient2, sizeof(*out2));
    out1->playerParty[outgoingSlot] = candidate;
    index = (dex - 1) / 8;
    mask = 1 << ((dex - 1) % 8);
    out1->dexSeen[index] |= mask;
    if (!(out1->dexCaught[index] & mask))
    {
        out1->dexCaught[index] |= mask;
        if (receivedSpecies == SPECIES_UNOWN) out2->pokedex.unownPersonality = personality;
        if (receivedSpecies == SPECIES_SPINDA) out2->pokedex.spindaPersonality = personality;
    }
    if (finalSpecies != receivedSpecies)
    {
        index = (finalDex - 1) / 8;
        mask = 1 << ((finalDex - 1) % 8);
        out1->dexSeen[index] |= mask;
        out1->dexCaught[index] |= mask;
        IncrementPrivateStat(out1, out2->encryptionKey, GAME_STAT_EVOLVED_POKEMON);
    }
    IncrementPrivateStat(out1, out2->encryptionKey, GAME_STAT_POKEMON_TRADES);
    if (ribbons) out1->flags[FLAG_SYS_RIBBON_GET / 8] |= 1 << (FLAG_SYS_RIBBON_GET % 8);
    for (index = 0; index < NUM_GIFT_RIBBONS; index++)
        if (!out1->giftRibbons[index] && partner1->giftRibbons[index]
         && partner1->giftRibbons[index] < MAX_GIFT_RIBBON)
            out1->giftRibbons[index] = partner1->giftRibbons[index];
    return TRUE;
}
