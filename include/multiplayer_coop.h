#ifndef GUARD_MULTIPLAYER_COOP_H
#define GUARD_MULTIPLAYER_COOP_H

#include "constants/multiplayer_coop.h"

// Co-op Gym battles. The Gym script offers a team challenge when the player's
// host-formed teammate stands in the same Gym; the host then issues an
// ordinary native challenge (Players menu consent and Enter) in the co-op
// phase. Parties are restored exactly after the battle, like every native
// battle; each trainer's own game then grants its own rewards: the badge and
// TM through the Gym's original victory script for a first win, and the
// Leader's prize money for everyone who won.

bool32 MultiplayerCoop_IsValidGym(u32 gym);
u16 MultiplayerCoop_GetLeader(u32 gym);
u16 MultiplayerCoop_GetTwin(u32 gym);
void MultiplayerCoop_Init(void);
void MultiplayerCoop_Tick(void);
void MultiplayerCoop_OnNativeResult(u32 gym, u32 outcome);

// Script specials (VAR_0x8004 = MP_COOP_GYM_*).
void MultiplayerCoop_CanOffer(void);
void MultiplayerCoop_RequestBattle(void);
void MultiplayerCoop_GivePrizeMoney(void);
void MultiplayerCoop_BufferPartnerName(void);

#ifdef PORTABLE
// Co-op experience (PC game): each player's own game awards its own party the
// experience of a co-op battle after the party is released (multiplayer_coop.c).
void MultiplayerCoop_ExpBegin(void);
void MultiplayerCoop_ExpStep(void);
void MultiplayerCoop_ExpChooseForget(void);
void MultiplayerCoop_ExpForgetResult(void);
void MultiplayerCoop_ExpSkipMove(void);
void MultiplayerCoop_ExpEvolve(void);
u32 MultiplayerCoop_ExpReward(struct Pokemon *mon, enum Species faintedSpecies, u32 faintedLevel, bool32 faced);
// A co-op battle's result waits for its script in the save's co-op record (include/global.h MPCoopResultRecord).
bool32 MultiplayerCoop_ResultSaved(void);       // the field is calm with a result waiting: TRUE when its script may run
void MultiplayerCoop_ResultScriptStarted(void); // a result script took the waiting result
bool32 MultiplayerCoop_ResultUnsaved(void);     // a result waits for its first host save (the soft reset key waits)
void MultiplayerCoop_WriteToSave(void);         // load_save.c: before a save is written
void MultiplayerCoop_LoadFromSave(void);        // load_save.c: after a save is loaded
// --selftest-coop-exp
void MultiplayerCoop_TestTrackFrame(void);
void MultiplayerCoop_TestCapture(u32 outcome);
bool32 MultiplayerCoop_TestForget(u32 moveSlot);
u32 MultiplayerCoop_TestKoCount(void);
void MultiplayerCoop_TestReset(void);
void MultiplayerCoop_TestNativeResult(u32 gym, u32 outcome);
u32 MultiplayerCoop_TestTakeResult(void);
u32 MultiplayerCoop_TestPendingGym(void);
void MultiplayerCoop_TestArmAward(void); // --test-coop-exp-award
#endif

#ifdef PORTABLE
u8 MultiplayerCoop_RemapBringSlots(u8 bits, u8 mask);
#endif

#endif // GUARD_MULTIPLAYER_COOP_H
