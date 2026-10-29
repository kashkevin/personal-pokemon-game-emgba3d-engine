#ifndef GUARD_PLATFORM_MP_BREEDING_H
#define GUARD_PLATFORM_MP_BREEDING_H
// Update 30 (PC game): breeding rules of EmeraldTogether. See docs/update30-gameplay.md.
//   * the Day Care checks for an Egg every 64 steps (the original 256), with the original compatibility chances (20/50/70 %);
//   * an egg cycle is 32 steps instead of 128 (75 % fewer steps for every species, their own cycle counts kept); Flame Body and
//     Magma Armor still halve it (once, however many such Pokemon are in the party);
//   * up to five made Eggs wait at the Day Care (a queue saved in SaveBlock3), oldest first; since 2026-10-29 they are
//     handed over to the party or the PC without a word to the Day Care man, and wait only while both are full;
//   * an Egg's shiny status is rolled once when it is made, from the parents and the Shiny Charm of the player who owns the Day Care.
#ifdef PORTABLE
#include "global.h"

#define MP_EGG_CHECK_STEPS 64
#define MP_EGG_CYCLE_STEPS 32      // the Gen 8 value is 128
#define MP_EGG_CYCLE_STEPS_MIN 16  // the least a cycle may ever take, whatever is added to this file later

extern u8 gMPEggShinyRolls;        // pokemon.c: 0 outside egg generation, else the number of independent 1/4096 rolls

// Rolls of 1/4096 for an Egg: 1; 3 with a shiny parent (1 in 1365.7); 3 with the Shiny Charm; 6 with both (1 in 682.9).
// Two shiny parents give no more than one.
u32 MPBreeding_EggShinyRolls(bool32 parentShiny, bool32 charmOwned);

// The queue of Eggs waiting at the Day Care (SaveBlock3). A queue whose magic or CRC is wrong reads as empty.
u32 MPDaycare_QueueCount(void);
bool32 MPDaycare_QueuePush(const struct Pokemon *egg);        // FALSE when the queue is full
bool32 MPDaycare_QueuePop(struct Pokemon *egg);               // the oldest egg; FALSE when empty
bool32 MPDaycare_QueueDropOldest(void);                       // the egg the player turned down
void MPDaycare_QueueClear(void);
bool32 MPDaycare_QueueIsSane(void);                           // magic, version, count and CRC

// Eggs in the PC's boxes (the owner's request of 2026-10-27): at the end of each egg cycle of the player's own steps
// (TryToHatchEgg, the party's cadence: the same steps, the same skips), every Egg in every box spends `toSub` cycles as a
// party Egg does (never more), and one with none left hatches in its own slot with everything it was made with; the
// hatch is then said in a banner (MPBreeding_BoxEggNoticeTick, retried until the field is calm).
void MPBreeding_AdvanceBoxEggs(u32 toSub);
void MPBreeding_BoxEggNoticeTick(void);
const char *MPBreeding_LastBoxEggNotice(void);   // the last such banner shown ("" before any)

// The Day Care's Eggs come to the player without a word to the Day Care man (the owner's request of 2026-10-29). Hands
// the waiting Eggs over, oldest first: each to the party's first free slot as the Day Care man gives one, else to the PC
// as a gift Pokemon goes (CopyMonToPC, from the current box on); it stops at the first that fits nowhere, which waits with
// the Day Care man (he offers it as before). The pending flag clears with the last. Adds to *toParty and *toPc how many
// went where; *pcBox the box of the last one sent to the PC.
void MPDaycare_DeliverWaitingEggs(u32 *toParty, u32 *toPc, u32 *pcBox);
// Each frame: once the field is calm (no script, menu, fade or battle), outside a Frontier or Battle Tent challenge (a
// few chosen Pokemon or rentals may be the party), the Trainer Hill, the Union Room and link play, the waiting Eggs are
// handed over; a banner says where they went ("DAY CARE EGG!|An Egg joined your party."), or once that one waits for
// room. With the party and the PC full it looks again every second.
void MPDaycare_DeliveryTick(void);
const char *MPDaycare_LastDeliveryNotice(void);  // the last such banner shown ("" before any)
#endif
#endif // GUARD_PLATFORM_MP_BREEDING_H
