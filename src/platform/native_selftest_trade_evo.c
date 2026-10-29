// Native PC build: a Pokemon received in a world trade evolves by trade (KNOWN_ISSUES MS-151; mp_trade_evolution.c).
// Project-owned. The party before a trade's save and after it, through the real evolution rules, then exits.
//   --selftest-trade-evolution
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "constants/items.h"
#include "constants/species.h"
#include "platform/mp_trade_evolution.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    SetBagItemsPointers();
    ClearBag();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
}

static void Make(struct Pokemon *mon, enum Species species, u32 personality, u32 otId, enum Item item)
{
    CreateMon(mon, species, 30, personality, OTID_STRUCT_PRESET(otId));
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
}

// A trade as the game sees it: a party of Zigzagoon and `sent` in slot 2; the save comes in with `received` there.
// Returns the waiting slot (PARTY_SIZE: none) and its target.
static u32 Trade(enum Species sent, enum Item sentItem, enum Species received, enum Item receivedItem, u32 *target)
{
    ZeroPlayerPartyMons();
    for (u32 i = 0; i < 4; i++)
        Make(&gParties[B_TRAINER_PLAYER][i], SPECIES_ZIGZAGOON, 0x100 + i, 1, ITEM_NONE);
    Make(&gParties[B_TRAINER_PLAYER][2], sent, 0x2000, 1, sentItem);
    CalculatePlayerPartyCount();
    MPTradeEvolution_NoteBeforeImport();
    Make(&gParties[B_TRAINER_PLAYER][2], received, 0x3000, 2, receivedItem);   // another trainer's Pokemon
    MPTradeEvolution_AfterTradeLoaded();
    return MPTradeEvolution_WaitingSlot(target);
}

int NativeSelfTest_TradeEvolution(void)
{
    struct Pokemon onix;
    u32 target = 0, slot;
    enum Item item;

    Setup();
    printf("selftest-trade-evolution: KNOWN_ISSUES MS-151\n");
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_KADABRA, ITEM_NONE, &target);
    Expect("a Kadabra received evolves into Alakazam (its slot waits for the field)", slot == 2 && target == SPECIES_ALAKAZAM);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_MACHOKE, ITEM_EVERSTONE, &target);
    Expect("a Machoke holding an Everstone does not evolve", slot == PARTY_SIZE);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_MACHOKE, ITEM_NONE, &target);
    Expect("... without one it evolves into Machamp", slot == 2 && target == SPECIES_MACHAMP);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_KADABRA, ITEM_EVERSTONE, &target);
    Expect("a Kadabra evolves even holding an Everstone (the rule since Gen 4)", slot == 2 && target == SPECIES_ALAKAZAM);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_ONIX, ITEM_METAL_COAT, &target);
    Expect("an Onix holding a Metal Coat evolves into Steelix", slot == 2 && target == SPECIES_STEELIX);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_ONIX, ITEM_NONE, &target);
    Expect("... an Onix without one does not", slot == PARTY_SIZE);
    slot = Trade(SPECIES_SHELMET, ITEM_NONE, SPECIES_KARRABLAST, ITEM_NONE, &target);
    Expect("a Karrablast traded for a Shelmet evolves into Escavalier", slot == 2 && target == SPECIES_ESCAVALIER);
    slot = Trade(SPECIES_KARRABLAST, ITEM_NONE, SPECIES_SHELMET, ITEM_NONE, &target);
    Expect("... and the Shelmet on the other side into Accelgor", slot == 2 && target == SPECIES_ACCELGOR);
    slot = Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_KARRABLAST, ITEM_NONE, &target);
    Expect("a Karrablast traded for anything else does not", slot == PARTY_SIZE);
    slot = Trade(SPECIES_KADABRA, ITEM_NONE, SPECIES_PIKACHU, ITEM_NONE, &target);
    Expect("a Pikachu received (the Kadabra given away) does not", slot == PARTY_SIZE);

    // not one exchange: two slots changed, or none noted before the save came in
    Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_KADABRA, ITEM_NONE, &target);
    MPTradeEvolution_NoteBeforeImport();
    Make(&gParties[B_TRAINER_PLAYER][0], SPECIES_KADABRA, 0x4000, 2, ITEM_NONE);
    Make(&gParties[B_TRAINER_PLAYER][1], SPECIES_KADABRA, 0x4001, 2, ITEM_NONE);
    MPTradeEvolution_AfterTradeLoaded();
    Expect("two slots changed (not one exchange): nothing evolves", MPTradeEvolution_WaitingSlot(&target) == PARTY_SIZE);
    Trade(SPECIES_PIKACHU, ITEM_NONE, SPECIES_PIKACHU, ITEM_NONE, &target);       // a trade noted and loaded: done
    Make(&gParties[B_TRAINER_PLAYER][2], SPECIES_KADABRA, 0x4002, 2, ITEM_NONE);
    MPTradeEvolution_AfterTradeLoaded();
    Expect("a save loaded without a trade noted before it: nothing evolves", MPTradeEvolution_WaitingSlot(&target) == PARTY_SIZE);

    // the evolution's own rule uses up the item it needed (the tick's DO_EVO), as a link trade does
    Make(&onix, SPECIES_ONIX, 0x5000, 2, ITEM_METAL_COAT);
    target = GetEvolutionTargetSpecies(&onix, EVO_MODE_TRADE, ITEM_NONE, &gParties[B_TRAINER_PLAYER][3], NULL, DO_EVO);
    item = GetMonData(&onix, MON_DATA_HELD_ITEM);
    Expect("starting it uses up the Metal Coat", target == SPECIES_STEELIX && item == ITEM_NONE);

    printf("selftest-trade-evolution: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
