#ifndef GUARD_PLATFORM_MP_TRADE_EVOLUTION_H
#define GUARD_PLATFORM_MP_TRADE_EVOLUTION_H
// KNOWN_ISSUES MS-151 (PC game): a Pokemon received in a world trade evolves as one received in a link trade does
// (Kadabra, Machoke, Graveler, Haunter; Onix with a Metal Coat, Seadra with a Dragon Scale and the other items, which
// are used up; Karrablast and Shelmet traded for each other; an Everstone stops it). The world's trade itself stays the
// exact exchange of the two Pokemon (simple-owned-party-exchange-v1): the receiving game evolves its new Pokemon on its
// own once the trade's save is loaded and the field is calm, the original's trade evolution scene (it cannot be
// stopped), and its next save carries the evolved Pokemon as any evolution.
#ifdef PORTABLE
#include "global.h"

// A trade's save is about to be imported: the party as it was (multiplayer_save.c, the trade's first BEGIN_IMPORT).
void MPTradeEvolution_NoteBeforeImport(void);
// The trade's save is loaded and the field back: the one slot whose Pokemon changed holds the one received; if it
// evolves by trade (with the one given away as its partner), its evolution waits for the field to be ready.
void MPTradeEvolution_AfterTradeLoaded(void);
// Each frame: with the field ready (no script, menu, battle or held save) the waiting evolution starts.
void MPTradeEvolution_Tick(bool32 fieldReady);
// The last line said about a trade's received Pokemon ("" before any): the test builds' log.
const char *MPTradeEvolution_LastNote(void);
// The slot whose evolution waits (PARTY_SIZE: none), *target the species it evolves into (--selftest-trade-evolution).
u32 MPTradeEvolution_WaitingSlot(u32 *target);
#endif
#endif // GUARD_PLATFORM_MP_TRADE_EVOLUTION_H
