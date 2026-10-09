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

#endif // GUARD_MULTIPLAYER_COOP_H
