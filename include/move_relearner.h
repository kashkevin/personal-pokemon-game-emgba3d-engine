#ifndef GUARD_MOVE_RELEARNER_H
#define GUARD_MOVE_RELEARNER_H

#include "constants/move_relearner.h"

void TeachMoveRelearnerMove(void);
void MoveRelearnerShowHideHearts(s32 move);
void MoveRelearnerShowHideCategoryIcon(s32);
void CB2_InitLearnMove(void);
bool32 CanBoxMonRelearnMoves(struct BoxPokemon *boxMon, enum MoveRelearnerStates state);
bool32 HasMoveToRelearn(struct BoxPokemon *boxMon, enum MoveRelearnerStates state);
#ifdef PORTABLE
// Move switching (PC game): MOVES in the party menu and the summary screen's relearner (src/move_relearner.c)
bool32 MoveRelearner_IsMoveSwitching(void);
bool32 MoveRelearner_CanSwitchBackIn(struct BoxPokemon *boxMon, enum Move move);
bool32 MoveRelearner_CanForgetHiddenMove(struct BoxPokemon *boxMon, enum Move move);
const u8 *MoveRelearner_ForgetWarning(struct BoxPokemon *boxMon, u32 slot);
#endif

extern enum MoveRelearnerStates gMoveRelearnerState;
extern enum RelearnMode gRelearnMode;

#endif //GUARD_MOVE_RELEARNER_H
