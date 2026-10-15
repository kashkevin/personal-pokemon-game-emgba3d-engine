#ifndef GUARD_BATTLE_FFA_H
#define GUARD_BATTLE_FFA_H

// EmeraldTogether four-player free-for-all (1v1v1v1). See docs/free-for-all.md in the app repository.
//
// A free-for-all is the four-trainer link Multi battle (BATTLE_TYPE_MULTI, four battlers, one party each) with
// BATTLE_TYPE_FFA added: every battler is a side of its own (GetBattlerSide() returns the battler number), nobody has
// an ally, and every other battler is a foe. Only the master core runs these rules; the others mirror its commands.
#include "constants/battle.h"

#if MP_FFA_ENABLED

// TRUE when this battler has an ally on the field's other flank. Never in a free-for-all.
bool32 BattlerHasAlly(enum BattlerId battler);

// Alive battlers other than `battler`, in battler order. In every other battle type: the opposing side's battlers.
u32 CountAliveFoes(enum BattlerId battler);

// The n-th (0-based) other battler, alive or not. Used to walk "every other battler".
enum BattlerId GetOtherBattler(enum BattlerId battler, u32 n);

// A random alive foe, or `battler` itself when none is alive.
enum BattlerId PickRandomAliveFoe(enum BattlerId battler);

// A trainer is eliminated once none of their Pokemon can battle (the Pokemon on the field included).
bool32 FFA_IsEliminated(enum BattlerId battler);

// How many trainers can still battle, and (when exactly one can) which battler is theirs; gBattlersCount otherwise.
u32 FFA_CountTrainersLeft(void);
enum BattlerId FFA_GetSoleSurvivor(void);

// The battler this core's player controls (the one with a player controller), and how the battle ended for it.
enum BattlerId FFA_GetLocalBattler(void);
u32 FFA_GetOutcomeForWinner(u32 winnerByte);

// The trainer a Pokemon belongs to, as the battle's link players name them (the game's text), or NULL: never for a Pokemon
// that is not in a free-for-all. Everything that tells the players whose Pokemon is whose asks this.
const u8 *FFA_GetOwnerName(enum BattlerId battler);
// TRUE for the Pokemon this core's player controls.
bool32 FFA_IsLocalBattler(enum BattlerId battler);

// The winner as the byte the end-of-battle command carries: a battler number, or 0xFF for a draw.
u8 FFA_RecordWinner(enum BattlerId winner);
u8 FFA_GetWinnerByte(void);

#endif // MP_FFA_ENABLED
#endif // GUARD_BATTLE_FFA_H
