#include "global.h"
#include "config/battle.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "money.h"
#include "coins.h"
#include "battle.h"
#include "battle_interface.h"
#include "battle_message.h"
#include "battle_script_commands.h"
#include "main.h"
#include "event_data.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/vars.h"
#include "constants/battle.h"
#include "constants/battle_string_ids.h"

// Compared guards only. Never restore these as a character snapshot.
struct LocalResetGuard {
    struct Pokemon party[PARTY_SIZE];
    struct Bag bag;
    u32 money, storageHash, daycareHash, ownershipHash;
    u16 coins;
    u8 partyCount;
    u8 dexCaught[NUM_DEX_FLAG_BYTES];
};
static EWRAM_DATA struct LocalResetGuard sLocalResetGuard;
static EWRAM_DATA u32 sLocalResetSession, sLocalResetIncarnation, sLocalResetRosterEpoch;
static EWRAM_DATA u32 sLocalResetEncounterId;
static EWRAM_DATA bool8 sLocalResetActive, sLocalResetWindow, sLocalResetRequested, sLocalResetConfirming, sLocalResetAccepted, sLocalResetClosed, sLocalResetChordHeld;
static EWRAM_DATA u8 sLocalResetCursor;
static const u8 sLocalResetPrompt[] = _("Reset this encounter?");
#if MP_LOCAL_RESET_ENABLED
static const u8 sLocalResetActionPrompt[] = _("Choose an action\nSELECT: Reset");
#endif

static u32 Hash(const void *value, u32 size)
{
    const u8 *bytes = value;
    u32 result = 2166136261u;
    while (size--) result = (result ^ *bytes++) * 16777619u;
    return result;
}

static bool32 BindingReady(void)
{
    u32 session, incarnation;
    return Multiplayer_IsConnected()
        && MultiplayerSave_GetIdleBinding(&session, &incarnation)
        && session == sLocalResetSession && incarnation == sLocalResetIncarnation
        && gMultiplayerMailbox.rosterEpoch == sLocalResetRosterEpoch
        && !MultiplayerBattle_IsActive();
}

#if MP_LOCAL_RESET_ENABLED
static void CopyPocket(struct ItemSlot *target, const struct ItemSlot *source, u32 count)
{
    u16 key = gSaveBlock2Ptr->encryptionKey;
    while (count--)
    {
        *target = *source;
        target->quantity ^= key;
        target++; source++;
    }
}
#endif

static bool32 PocketMatches(const struct ItemSlot *guard, const struct ItemSlot *current, u32 count)
{
    u16 key = gSaveBlock2Ptr->encryptionKey;
    while (count--)
    {
        if (guard->itemId != current->itemId || guard->quantity != (u16)(current->quantity ^ key))
            return FALSE;
        guard++; current++;
    }
    return TRUE;
}

static bool32 BagMatches(void)
{
    const struct Bag *bag = &gSaveBlock1Ptr->bag;
    return PocketMatches(sLocalResetGuard.bag.items, bag->items, BAG_ITEMS_COUNT)
        && PocketMatches(sLocalResetGuard.bag.keyItems, bag->keyItems, BAG_KEYITEMS_COUNT)
        && PocketMatches(sLocalResetGuard.bag.pokeBalls, bag->pokeBalls, BAG_POKEBALLS_COUNT)
        && PocketMatches(sLocalResetGuard.bag.TMsHMs, bag->TMsHMs, BAG_TMHM_COUNT)
        && PocketMatches(sLocalResetGuard.bag.berries, bag->berries, BAG_BERRIES_COUNT);
}

static bool32 GuardMatches(void)
{
    return gSaveBlock1Ptr && gSaveBlock2Ptr && gSaveBlock3Ptr && gPokemonStoragePtr
        && gPartiesCount[B_TRAINER_PLAYER] == sLocalResetGuard.partyCount
        && !memcmp(gParties[B_TRAINER_PLAYER], sLocalResetGuard.party, sizeof(sLocalResetGuard.party))
        && BagMatches()
        && GetMoney(&gSaveBlock1Ptr->money) == sLocalResetGuard.money && GetCoins() == sLocalResetGuard.coins
        && !memcmp(gSaveBlock1Ptr->dexCaught, sLocalResetGuard.dexCaught, sizeof(sLocalResetGuard.dexCaught))
        && Hash(gPokemonStoragePtr, sizeof(*gPokemonStoragePtr)) == sLocalResetGuard.storageHash
        && Hash(&gSaveBlock1Ptr->daycare, sizeof(gSaveBlock1Ptr->daycare)) == sLocalResetGuard.daycareHash
        && Hash(&gSaveBlock3Ptr->ownership, sizeof(gSaveBlock3Ptr->ownership)) == sLocalResetGuard.ownershipHash
        && (sLocalResetEncounterId == MP_LOCAL_RESET_DEVELOPMENT_ID
            ? VarGet(MP_LOCAL_RESET_CAPTURED) == 0
            : MultiplayerStaticReset_GuardMatches(sLocalResetEncounterId));
}

void MultiplayerLocalReset_Init(void)
{
    sLocalResetActive = sLocalResetWindow = sLocalResetRequested = sLocalResetConfirming = sLocalResetAccepted = sLocalResetClosed = FALSE;
    sLocalResetSession = sLocalResetIncarnation = sLocalResetRosterEpoch = 0; sLocalResetChordHeld = FALSE;
    sLocalResetEncounterId = 0;
}

u32 MultiplayerLocalReset_Count(void)
{
    return VarGet(MP_LOCAL_RESET_COUNTER_LO) | (u32)VarGet(MP_LOCAL_RESET_COUNTER_HI) << 16;
}

#if MP_LOCAL_RESET_ENABLED
static bool32 SourceReady(u32 encounterId)
{
#if MP_LOCAL_RESET_DEVELOPMENT || TESTING
    if (encounterId == MP_LOCAL_RESET_DEVELOPMENT_ID)
        return gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_LITTLEROOT_TOWN)
            && gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_LITTLEROOT_TOWN)
            && gSpecialVar_LastTalked == 3 && VarGet(MP_LOCAL_RESET_CAPTURED) == 0;
#endif
    return MultiplayerStaticReset_EntryReady(encounterId);
}

static bool32 HasUsableParty(void)
{
    u32 slot;
    if (!gPartiesCount[B_TRAINER_PLAYER] || gPartiesCount[B_TRAINER_PLAYER] > PARTY_SIZE)
        return FALSE;
    for (slot = 0; slot < gPartiesCount[B_TRAINER_PLAYER]; slot++)
        if (GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_SANITY_HAS_SPECIES)
         && !GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_IS_EGG)
         && GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_HP))
            return TRUE;
    return FALSE;
}
#endif

bool32 MultiplayerLocalReset_TryBegin(u32 encounterId)
{
#if MP_LOCAL_RESET_ENABLED
    u32 count;
    if (sLocalResetActive || gMain.inBattle
     || !gSaveBlock1Ptr || !gSaveBlock2Ptr || !gSaveBlock3Ptr || !gPokemonStoragePtr
     || !SourceReady(encounterId) || !Multiplayer_IsConnected()
     || !MultiplayerSave_GetIdleBinding(&sLocalResetSession, &sLocalResetIncarnation)
     || MultiplayerBattle_IsActive() || !MultiplayerOwnership_IsEnrolled()
     || !HasUsableParty())
        return FALSE;
    if (encounterId != MP_LOCAL_RESET_DEVELOPMENT_ID)
    {
        if (!MultiplayerStaticReset_AdvanceAttempt(encounterId)) return FALSE;
    }
    else
    {
        if (VarGet(MP_LOCAL_RESET_COUNTER_MAGIC) != 0
         && VarGet(MP_LOCAL_RESET_COUNTER_MAGIC) != MP_LOCAL_RESET_MAGIC)
            return FALSE;
        if (!VarGet(MP_LOCAL_RESET_COUNTER_MAGIC)
         && (VarGet(MP_LOCAL_RESET_COUNTER_LO) || VarGet(MP_LOCAL_RESET_COUNTER_HI)))
            return FALSE;
        count = MultiplayerLocalReset_Count();
        if (count == 0xFFFFFFFFu) return FALSE;
        count++;
        VarSet(MP_LOCAL_RESET_COUNTER_MAGIC, MP_LOCAL_RESET_MAGIC);
        VarSet(MP_LOCAL_RESET_COUNTER_LO, count);
        VarSet(MP_LOCAL_RESET_COUNTER_HI, count >> 16);
    }
    sLocalResetEncounterId = encounterId;
    memcpy(sLocalResetGuard.party, gParties[B_TRAINER_PLAYER], sizeof(sLocalResetGuard.party));
    CopyPocket(sLocalResetGuard.bag.items, gSaveBlock1Ptr->bag.items, BAG_ITEMS_COUNT);
    CopyPocket(sLocalResetGuard.bag.keyItems, gSaveBlock1Ptr->bag.keyItems, BAG_KEYITEMS_COUNT);
    CopyPocket(sLocalResetGuard.bag.pokeBalls, gSaveBlock1Ptr->bag.pokeBalls, BAG_POKEBALLS_COUNT);
    CopyPocket(sLocalResetGuard.bag.TMsHMs, gSaveBlock1Ptr->bag.TMsHMs, BAG_TMHM_COUNT);
    CopyPocket(sLocalResetGuard.bag.berries, gSaveBlock1Ptr->bag.berries, BAG_BERRIES_COUNT);
    memcpy(sLocalResetGuard.dexCaught, gSaveBlock1Ptr->dexCaught, sizeof(sLocalResetGuard.dexCaught));
    sLocalResetGuard.partyCount = gPartiesCount[B_TRAINER_PLAYER];
    sLocalResetGuard.money = GetMoney(&gSaveBlock1Ptr->money); sLocalResetGuard.coins = GetCoins();
    sLocalResetGuard.storageHash = Hash(gPokemonStoragePtr, sizeof(*gPokemonStoragePtr));
    sLocalResetGuard.daycareHash = Hash(&gSaveBlock1Ptr->daycare, sizeof(gSaveBlock1Ptr->daycare));
    sLocalResetGuard.ownershipHash = Hash(&gSaveBlock3Ptr->ownership, sizeof(gSaveBlock3Ptr->ownership));
    sLocalResetRosterEpoch = gMultiplayerMailbox.rosterEpoch;
    sLocalResetActive = TRUE; sLocalResetWindow = sLocalResetRequested = sLocalResetConfirming = sLocalResetAccepted = sLocalResetClosed = FALSE;
    return TRUE;
#else
    return FALSE;
#endif
}

void MultiplayerLocalReset_BeginDevelopmentHunt(void)
{
    gSpecialVar_Result = MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID);
}

bool32 MultiplayerLocalReset_PersonalGeneration(void)
{
    // Only the explicit authored source, during native generation. Owned mons,
    // wild encounters, gifts, presets and future event sources do not opt in.
    return sLocalResetActive && !gMain.inBattle && !sLocalResetClosed
        && sLocalResetEncounterId == MP_STATIC_RESET_VOLTORB_1
        && BindingReady() && MultiplayerStaticReset_EntryReady(sLocalResetEncounterId);
}

static bool32 OpenWindow(void)
{
    if (!sLocalResetActive || sLocalResetClosed || sLocalResetAccepted || !gMain.inBattle || !gBattleStruct
     || gBattleResults.battleTurnCounter || (gBattleTypeFlags & ~BATTLE_TYPE_IS_MASTER) || !BindingReady() || !GuardMatches())
        return FALSE;
    sLocalResetWindow = TRUE;
    return TRUE;
}

#if MP_LOCAL_RESET_ENABLED
static bool32 Request(void)
{
    if (!sLocalResetWindow || !gMain.inBattle || !gBattleStruct || gBattleResults.battleTurnCounter
     || (gBattleTypeFlags & ~BATTLE_TYPE_IS_MASTER) || !BindingReady() || !GuardMatches()) return FALSE;
    sLocalResetRequested = TRUE;
    return TRUE;
}
#endif

const u8 *MultiplayerLocalReset_GetActionPrompt(enum BattlerId battler)
{
#if MP_LOCAL_RESET_ENABLED
    // A menu render is also the beginning of the first-action window. Do not
    // advertise the shortcut after any action, on other controllers, or while
    // the native confirmation already owns input.
    if (GetBattlerPosition(battler) == B_POSITION_PLAYER_LEFT
     && !sLocalResetRequested && !sLocalResetConfirming && OpenWindow())
        return sLocalResetActionPrompt;
#endif
    return NULL;
}

static bool32 RequestFromSelect(enum BattlerId battler)
{
#if MP_LOCAL_RESET_ENABLED
    // Leave the full reset chord and ordinary A/B/Start input to their existing
    // handlers. Only an eligible Select request takes priority over the debug
    // menu; an ineligible request leaves its input untouched.
    if (GetBattlerPosition(battler) == B_POSITION_PLAYER_LEFT
     && !sLocalResetRequested && !sLocalResetConfirming
     && JOY_NEW(SELECT_BUTTON) && !JOY_HELD_RAW(A_BUTTON | B_BUTTON | START_BUTTON)
     && Request())
        return TRUE;
#endif
    return FALSE;
}

static bool32 Confirm(bool32 yes)
{
    if (!sLocalResetRequested && !sLocalResetConfirming) return FALSE;
    if (!yes)
    {
        sLocalResetRequested = sLocalResetConfirming = FALSE;
        return FALSE;
    }
    if (!sLocalResetWindow || !BindingReady() || !GuardMatches()
     || !gMain.inBattle || !gBattleStruct || gBattleResults.battleTurnCounter || (gBattleTypeFlags & ~BATTLE_TYPE_IS_MASTER))
    {
        sLocalResetWindow = sLocalResetRequested = sLocalResetConfirming = FALSE;
        return FALSE;
    }
    sLocalResetAccepted = TRUE; sLocalResetWindow = sLocalResetRequested = sLocalResetConfirming = FALSE;
    return TRUE;
}

bool32 MultiplayerLocalReset_HandleChord(void)
{
#if MP_LOCAL_RESET_ENABLED
    // Suppress a BIOS reboot for an enrolled game, including unsafe scenes.
    u32 session, incarnation;
    const u16 chord = A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON;
    if (JOY_HELD_RAW(chord) != chord)
    {
        sLocalResetChordHeld = FALSE;
        return FALSE;
    }
    if (!MultiplayerSave_HasBinding()) return FALSE;
    if (!sLocalResetChordHeld && MultiplayerSave_GetIdleBinding(&session, &incarnation)) Request();
    sLocalResetChordHeld = TRUE;
    return TRUE;
#else
    return FALSE;
#endif
}

u32 MultiplayerLocalReset_HandlePlayerInput(enum BattlerId battler)
{
    if (!sLocalResetActive || GetBattlerPosition(battler) != B_POSITION_PLAYER_LEFT) return MP_LOCAL_RESET_INPUT_PASS;
    if (!sLocalResetWindow && !sLocalResetClosed && !sLocalResetAccepted) OpenWindow();
    RequestFromSelect(battler);
    if (sLocalResetRequested && !sLocalResetConfirming)
    {
        sLocalResetRequested = FALSE; sLocalResetConfirming = TRUE; sLocalResetCursor = 1;
        // The action menu scrolls BG0 to its second page; native message and
        // Yes/No windows live on the first page.
        gBattle_BG0_X = gBattle_BG0_Y = 0;
        HandleBattleWindow(YESNOBOX_X_Y, 0);
        BattlePutTextOnWindow(sLocalResetPrompt, B_WIN_MSG);
        BattlePutTextOnWindow(gText_BattleYesNoChoice, B_WIN_YESNO);
        BattleCreateYesNoCursorAt(sLocalResetCursor);
        return MP_LOCAL_RESET_INPUT_HANDLED;
    }
    if (sLocalResetConfirming)
    {
        if (JOY_NEW(DPAD_UP) && sLocalResetCursor)
        {
            BattleDestroyYesNoCursorAt(sLocalResetCursor); sLocalResetCursor = 0; BattleCreateYesNoCursorAt(sLocalResetCursor);
        }
        else if (JOY_NEW(DPAD_DOWN) && !sLocalResetCursor)
        {
            BattleDestroyYesNoCursorAt(sLocalResetCursor); sLocalResetCursor = 1; BattleCreateYesNoCursorAt(sLocalResetCursor);
        }
        if (JOY_NEW(A_BUTTON) || JOY_NEW(B_BUTTON) || !BindingReady())
        {
            bool32 accepted = Confirm(JOY_NEW(A_BUTTON) && sLocalResetCursor == 0);
            BattleDestroyYesNoCursorAt(sLocalResetCursor);
            HandleBattleWindow(YESNOBOX_X_Y, WINDOW_CLEAR);
            if (accepted)
            {
                BattleMain_QuitPersonalEncounter();
                return MP_LOCAL_RESET_INPUT_HANDLED;
            }
            return MP_LOCAL_RESET_INPUT_CANCELED;
        }
        return MP_LOCAL_RESET_INPUT_HANDLED;
    }
    if (JOY_NEW(A_BUTTON) || JOY_NEW(B_LAST_USED_BALL_BUTTON)) MultiplayerLocalReset_CloseAction();
    return MP_LOCAL_RESET_INPUT_PASS;
}

void MultiplayerLocalReset_CloseAction(void)
{
    sLocalResetWindow = sLocalResetRequested = sLocalResetConfirming = FALSE;
    // Never reopen after an action/submenu was selected, even when canceled.
    if (sLocalResetActive) sLocalResetClosed = TRUE;
}

void MultiplayerLocalReset_CloseCapture(void)
{
    MultiplayerLocalReset_CloseAction();
}

bool32 MultiplayerLocalReset_WasAccepted(void)
{
    return sLocalResetActive && sLocalResetAccepted && !sLocalResetClosed;
}

void MultiplayerLocalReset_Finish(void)
{
    MultiplayerLocalReset_Init();
}

void MultiplayerLocalReset_ConsumeResult(void)
{
    gSpecialVar_Result = MultiplayerLocalReset_WasAccepted();
    MultiplayerLocalReset_Finish();
}

#if TESTING
bool32 MultiplayerLocalReset_TestOpenWindow(void) { return OpenWindow(); }
bool32 MultiplayerLocalReset_TestRequest(void) { return Request(); }
bool32 MultiplayerLocalReset_TestConfirm(bool32 yes) { return Confirm(yes); }
bool32 MultiplayerLocalReset_TestSelect(enum BattlerId battler) { return RequestFromSelect(battler); }
#endif
