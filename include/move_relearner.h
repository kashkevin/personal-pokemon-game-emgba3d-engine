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

// Where a move on one of the lists comes from: the row's tag ("Lv 12", "TM24", "HM03", "Egg", "Tutor")
enum MoveSwitchSourceKind
{
    MOVE_SOURCE_NONE,
    MOVE_SOURCE_LEVEL,      // number: the level it is learned at (0: on evolving)
    MOVE_SOURCE_EGG,
    MOVE_SOURCE_TM,         // number: the TM's
    MOVE_SOURCE_HM,         // number: the HM's
    MOVE_SOURCE_TUTOR,      // number: the one-time tutor (MoveRelearner_TutorPlace)
};
struct MoveSwitchSource
{
    u8 kind;                // enum MoveSwitchSourceKind
    u8 number;
    u16 species;            // MOVE_SOURCE_LEVEL / EGG: the form whose learnset or egg moves have it
};
// The moves a list offers this Pokemon now, as the relearner lists them (the list's own rules); how many
u32 MoveRelearner_GetListMoves(struct BoxPokemon *boxMon, enum MoveRelearnerStates list, u16 *moves);
bool32 MoveRelearner_GetSource(struct BoxPokemon *boxMon, enum MoveRelearnerStates list, enum Move move,
                               struct MoveSwitchSource *out);
// "Lv 12", "Evo", "TM24", "HM03", "Egg", "Tutor" (compact: "Lv12"); returns the end of dst
u8 *MoveRelearner_SourceText(const struct MoveSwitchSource *source, u8 *dst, bool32 compact);
// The town of a one-time tutor (MOVE_SOURCE_TUTOR's number), into dst; returns the end of dst
u8 *MoveRelearner_TutorPlace(u32 tutor, u8 *dst);
#endif

extern enum MoveRelearnerStates gMoveRelearnerState;
extern enum RelearnMode gRelearnMode;

#endif //GUARD_MOVE_RELEARNER_H
