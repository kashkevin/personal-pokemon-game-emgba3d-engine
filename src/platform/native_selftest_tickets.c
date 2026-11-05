// Native PC build: the Legendary Tickets' reward (reward word kind 8, src/platform/mp_tickets.c) and the save facts the
// host reads to decide them (the app's legendary_tickets.py and docs/legendary-tickets.md section 6.4).
// Project-owned. Runs through the game's own Bag, PC and flag code before the game starts, then exits.
//   --selftest-tickets
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_world.h"
#include "player_pc.h"
#include "constants/flags.h"
#include "constants/game_stat.h"
#include "constants/items.h"
#include "constants/vars.h"
#include "platform/mp_tickets.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void FreshSave(void)
{
    InitHeap(gHeap, HEAP_SIZE);    // the Bag allocates from the game heap
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    SetBagItemsPointers();
    ClearBag();
}

static u32 Apply(u32 payload)
{
    u8 result = 0xFF;
    u32 amount = MultiplayerWorld_ApplyRewardWord((s32)(MP_REWARD_FLAG | (MP_REWARD_TICKET << 24) | payload), &result);

    return result == MP_WORLD_WALLET_CREDIT ? amount : 0xDEAD;
}

static const struct { u32 index; u16 item, shipFlag, receivedFlag; const char *name; } sExpected[] =
{
    {MP_TICKET_EON,         ITEM_EON_TICKET,    FLAG_ENABLE_SHIP_SOUTHERN_ISLAND, 0,                           "Eon Ticket"},
    {MP_TICKET_MYSTIC,      ITEM_MYSTIC_TICKET, FLAG_ENABLE_SHIP_NAVEL_ROCK,      FLAG_RECEIVED_MYSTIC_TICKET, "Mystic Ticket"},
    {MP_TICKET_AURORA,      ITEM_AURORA_TICKET, FLAG_ENABLE_SHIP_BIRTH_ISLAND,    FLAG_RECEIVED_AURORA_TICKET, "Aurora Ticket"},
    {MP_TICKET_OLD_SEA_MAP, ITEM_OLD_SEA_MAP,   FLAG_ENABLE_SHIP_FARAWAY_ISLAND,  FLAG_RECEIVED_OLD_SEA_MAP,   "Old Sea Map"},
};

static bool32 IslandFlags(u32 i)
{
    return FlagGet(sExpected[i].shipFlag) && (sExpected[i].receivedFlag == 0 || FlagGet(sExpected[i].receivedFlag));
}

static bool32 NoIslandFlags(void)
{
    for (u32 i = 0; i < ARRAY_COUNT(sExpected); i++)
        if (FlagGet(sExpected[i].shipFlag) || (sExpected[i].receivedFlag != 0 && FlagGet(sExpected[i].receivedFlag)))
            return FALSE;
    return TRUE;
}

static u32 Owned(u16 item)
{
    return CountTotalItemQuantityInBag(item) + (CheckPCHasItem(item, 1) ? 1 : 0);
}

// The key pocket filled with key items that are not tickets; FALSE if it could not be filled.
static bool32 FillKeyPocket(void)
{
    u32 added = 0;

    for (u16 item = 1; item < ITEMS_COUNT && added < BAG_KEYITEMS_COUNT; item++)
    {
        if (GetItemPocket(item) != POCKET_KEY_ITEMS || MultiplayerTickets_Item(1) == item || MultiplayerTickets_Item(2) == item
         || MultiplayerTickets_Item(3) == item || MultiplayerTickets_Item(4) == item)
            continue;
        if (AddBagItem(item, 1))
            added++;
    }
    return added == BAG_KEYITEMS_COUNT;
}

// The PC's item storage filled with other items; FALSE if it could not be filled.
static bool32 FillPcItems(void)
{
    u32 added = 0;

    for (u16 item = 1; item < ITEMS_COUNT && added < PC_ITEMS_COUNT; item++)
    {
        if (GetItemPocket(item) == POCKET_KEY_ITEMS || item == ITEM_NONE)
            continue;
        if (AddPCItem(item, 1))
            added++;
    }
    return added == PC_ITEMS_COUNT;
}

int NativeSelfTest_Tickets(void)
{
    u32 i, receipt;

    printf("selftest-tickets: the Legendary Tickets' reward (kind 8) and the save facts the host reads\n");

    // The contract with the app (legendary_tickets.py, rewards.py): indexes, items, flags, the marker.
    Expect("reward kind 8 and the marker 1", MP_REWARD_TICKET == 8 && gMultiplayerTicketAbiVersion == 1);
    Expect("the items: Eon 728, Mystic 729, Aurora 730, Old Sea Map 731",
           ITEM_EON_TICKET == 728 && ITEM_MYSTIC_TICKET == 729 && ITEM_AURORA_TICKET == 730 && ITEM_OLD_SEA_MAP == 731
           && MultiplayerTickets_Item(MP_TICKET_EON) == ITEM_EON_TICKET && MultiplayerTickets_Item(MP_TICKET_MYSTIC) == ITEM_MYSTIC_TICKET
           && MultiplayerTickets_Item(MP_TICKET_AURORA) == ITEM_AURORA_TICKET && MultiplayerTickets_Item(MP_TICKET_OLD_SEA_MAP) == ITEM_OLD_SEA_MAP
           && MultiplayerTickets_Item(0) == ITEM_NONE && MultiplayerTickets_Item(5) == ITEM_NONE);
    Expect("the ship flags 0x8B3, 0x8E0, 0x8D5, 0x8D6 and the received flags 0x13A-0x13C",
           FLAG_ENABLE_SHIP_SOUTHERN_ISLAND == 0x8B3 && FLAG_ENABLE_SHIP_NAVEL_ROCK == 0x8E0
           && FLAG_ENABLE_SHIP_BIRTH_ISLAND == 0x8D5 && FLAG_ENABLE_SHIP_FARAWAY_ISLAND == 0x8D6
           && FLAG_RECEIVED_AURORA_TICKET == 0x13A && FLAG_RECEIVED_MYSTIC_TICKET == 0x13B && FLAG_RECEIVED_OLD_SEA_MAP == 0x13C);
    // The save facts the host reads for the sources (docs/legendary-tickets.md 6.4).
    Expect("SaveBlock1: flags at 0x1270, vars at 0x139C, game stats at 0x159C",
           offsetof(struct SaveBlock1, flags) == 0x1270 && offsetof(struct SaveBlock1, vars) == 0x139C
           && offsetof(struct SaveBlock1, gameStats) == 0x159C);
    Expect("SaveBlock2: the encryption key at 0xAC, the Frontier's card Battle Points at 0xEBA",
           offsetof(struct SaveBlock2, encryptionKey) == 0xAC
           && offsetof(struct SaveBlock2, frontier) + offsetof(struct BattleFrontier, cardBattlePoints) == 0xEBA);
    Expect("GAME_STAT_ENTERED_HOF 10, VAR_TRICK_HOUSE_LEVEL 0x4044",
           GAME_STAT_ENTERED_HOF == 10 && VAR_TRICK_HOUSE_LEVEL == 0x4044);
    Expect("the six legends' defeated flags 0x1BB-0x1C0 (Regirock, Regice, Registeel, Kyogre, Groudon, Rayquaza)",
           FLAG_DEFEATED_REGIROCK == 0x1BB && FLAG_DEFEATED_REGICE == 0x1BC && FLAG_DEFEATED_REGISTEEL == 0x1BD
           && FLAG_DEFEATED_KYOGRE == 0x1BE && FLAG_DEFEATED_GROUDON == 0x1BF && FLAG_DEFEATED_RAYQUAZA == 0x1C0);
    Expect("the seven symbols' flags from 0x8C4, each Gold right after its Silver",
           FLAG_SYS_TOWER_SILVER == 0x8C4 && FLAG_SYS_TOWER_GOLD == FLAG_SYS_TOWER_SILVER + 1
           && FLAG_SYS_DOME_SILVER == 0x8C6 && FLAG_SYS_DOME_GOLD == FLAG_SYS_DOME_SILVER + 1
           && FLAG_SYS_PALACE_SILVER == 0x8C8 && FLAG_SYS_PALACE_GOLD == FLAG_SYS_PALACE_SILVER + 1
           && FLAG_SYS_ARENA_SILVER == 0x8CA && FLAG_SYS_ARENA_GOLD == FLAG_SYS_ARENA_SILVER + 1
           && FLAG_SYS_FACTORY_SILVER == 0x8CC && FLAG_SYS_FACTORY_GOLD == FLAG_SYS_FACTORY_SILVER + 1
           && FLAG_SYS_PIKE_SILVER == 0x8CE && FLAG_SYS_PIKE_GOLD == FLAG_SYS_PIKE_SILVER + 1
           && FLAG_SYS_PYRAMID_SILVER == 0x8D0 && FLAG_SYS_PYRAMID_GOLD == FLAG_SYS_PYRAMID_SILVER + 1);

    // Each ticket on a fresh save: given once, its flags set; again: owned, still one.
    for (i = 0; i < ARRAY_COUNT(sExpected); i++)
    {
        char what[160];

        FreshSave();
        receipt = Apply(sExpected[i].index);
        snprintf(what, sizeof(what), "%s on a fresh save: receipt 1, one in the Bag, its island's flags set", sExpected[i].name);
        Expect(what, receipt == MP_TICKET_RECEIPT_GIVEN && CountTotalItemQuantityInBag(sExpected[i].item) == 1
               && !CheckPCHasItem(sExpected[i].item, 1) && IslandFlags(i));
        receipt = Apply(sExpected[i].index);
        snprintf(what, sizeof(what), "%s again: receipt 2 (already owned), still exactly one", sExpected[i].name);
        Expect(what, receipt == MP_TICKET_RECEIPT_OWNED && Owned(sExpected[i].item) == 1 && IslandFlags(i));
    }

    // Owned but without its flags (an earlier Mystery Gift save, a flag cleared): the flags are made sure, nothing added.
    FreshSave();
    AddBagItem(ITEM_AURORA_TICKET, 1);
    receipt = Apply(MP_TICKET_AURORA);
    Expect("a ticket already in the Bag without its flags: receipt 2, the flags set, still one", receipt == MP_TICKET_RECEIPT_OWNED
           && Owned(ITEM_AURORA_TICKET) == 1 && FlagGet(FLAG_ENABLE_SHIP_BIRTH_ISLAND) && FlagGet(FLAG_RECEIVED_AURORA_TICKET));
    FreshSave();
    AddPCItem(ITEM_OLD_SEA_MAP, 1);
    receipt = Apply(MP_TICKET_OLD_SEA_MAP);
    Expect("a ticket already in the PC: receipt 2, none added to the Bag", receipt == MP_TICKET_RECEIPT_OWNED
           && CountTotalItemQuantityInBag(ITEM_OLD_SEA_MAP) == 0 && CheckPCHasItem(ITEM_OLD_SEA_MAP, 1)
           && FlagGet(FLAG_ENABLE_SHIP_FARAWAY_ISLAND));

    // A full key pocket: the PC; full pocket and PC: nothing, no flag.
    FreshSave();
    Expect("(setup) the key pocket filled with 30 other key items", FillKeyPocket());
    receipt = Apply(MP_TICKET_MYSTIC);
    Expect("a full key pocket: the ticket goes to the PC, receipt 1, its flags set", receipt == MP_TICKET_RECEIPT_GIVEN
           && CountTotalItemQuantityInBag(ITEM_MYSTIC_TICKET) == 0 && CheckPCHasItem(ITEM_MYSTIC_TICKET, 1)
           && FlagGet(FLAG_ENABLE_SHIP_NAVEL_ROCK) && FlagGet(FLAG_RECEIVED_MYSTIC_TICKET));
    FreshSave();
    Expect("(setup) the key pocket and the PC's items full", FillKeyPocket() && FillPcItems());
    receipt = Apply(MP_TICKET_EON);
    Expect("a full key pocket and PC: receipt 0, nothing added, no flag set (all or nothing)",
           receipt == MP_TICKET_RECEIPT_NOTHING && Owned(ITEM_EON_TICKET) == 0 && NoIslandFlags());

    // Payloads this game does not know change nothing.
    FreshSave();
    {
        static const u32 bad[] = {0, 5, 0xFF, 0x100 | MP_TICKET_EON, 0x800000 | MP_TICKET_AURORA, 0x010000 | MP_TICKET_MYSTIC};
        bool32 none = TRUE;

        for (i = 0; i < ARRAY_COUNT(bad); i++)
            none = none && Apply(bad[i]) == MP_TICKET_RECEIPT_NOTHING;
        Expect("payloads 0, 5, 255 and any with bits 8-23 set: receipt 0, nothing given, no flag", none
               && Owned(ITEM_EON_TICKET) == 0 && Owned(ITEM_MYSTIC_TICKET) == 0 && Owned(ITEM_AURORA_TICKET) == 0
               && Owned(ITEM_OLD_SEA_MAP) == 0 && NoIslandFlags());
    }
    // Kind 8 is a reward word; its neighbours stay what they were.
    Expect("a ticket word is a reward word (flagged, above the money cap)",
           MultiplayerWorld_IsRewardWord((s32)(MP_REWARD_FLAG | (MP_REWARD_TICKET << 24) | MP_TICKET_EON)));

    printf("selftest-tickets: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
