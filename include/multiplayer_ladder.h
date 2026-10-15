#ifndef GUARD_MULTIPLAYER_LADDER_H
#define GUARD_MULTIPLAYER_LADDER_H

// Co-op ladder battles (expansion Phases 8 and 10, native PC game only).
//
// The co-op Gym battle (multiplayer_coop.c) is two linked trainers against two in-game trainers whose parties every core
// builds from the shared seed. A ladder battle is the same battle against any two trainers the host names, optionally at
// a level the host chose, so the host can run the Battle Frontier's survival ladder and the Pokemon League's rounds on the
// existing native battle, its synchronisation and its recovery. The 32-bit "gym" field of the battle preparation carries
// the choice (a Gym is still 1..8, bit 31 clear):
//
//   bit 31      ladder battle
//   bit 30      this battle's experience is awarded (the host decides; the Frontier's never is, as in the Battle Frontier)
//   bits 20-29  first opposing trainer (TRAINER_*)     bits 10-19  second opposing trainer
//   bits 7-9    zero                                    bits 0-6    opposing level 1..100
//
// Rewards are not the game's business: the host pays money through the wallet adjustments (exactly once per save) and keeps
// the progress; the game only shows the result and, when allowed, awards experience.

#ifdef PORTABLE

#define MP_LADDER_FLAG     0x80000000u
#define MP_LADDER_EXP      0x40000000u
#define MP_LADDER_MIN_LEVEL 1
#define MP_LADDER_MAX_LEVEL 100
#define MP_LADDER_TEAM_SIZE 3 // Pokemon each of the two opposing trainers brings

u32 MultiplayerLadder_Encode(u16 trainerA, u16 trainerB, u32 level, bool32 awardExp);
bool32 MultiplayerLadder_IsLadder(u32 code);
bool32 MultiplayerLadder_IsValid(u32 code);
u16 MultiplayerLadder_TrainerA(u32 code);
u16 MultiplayerLadder_TrainerB(u32 code);
u32 MultiplayerLadder_Level(u32 code);
bool32 MultiplayerLadder_ExpAllowed(u32 code);

// Called by the battle with the opponents' parties built: every opposing Pokemon becomes the chosen level.
void MultiplayerLadder_ScaleOpponents(u32 code);
// Called when the battle ends (before the party is restored) and every frame by the co-op tick.
void MultiplayerLadder_OnResult(u32 code, u32 outcome);
void MultiplayerLadder_Tick(void);

#endif // PORTABLE
#endif // GUARD_MULTIPLAYER_LADDER_H
