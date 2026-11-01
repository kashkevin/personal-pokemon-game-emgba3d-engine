#ifndef GUARD_BATTLE_ARENA_H
#define GUARD_BATTLE_ARENA_H

#include "constants/battle_arena.h"

void CallBattleArenaFunction(void);
u8 BattleArena_ShowJudgmentWindow(u8 *state);
void BattleArena_InitPoints(void);
void BattleArena_AddMindPoints(enum BattlerId battler);
void BattleArena_AddSkillPoints(enum BattlerId battlerAtk);
void BattleArena_DeductSkillPoints(enum BattlerId battler, enum StringID stringId);
void DrawArenaRefereeTextBox(void);
void EraseArenaRefereeTextBox(void);
#ifdef ENABLE_NATIVE_UI
// The 3D UI's Battle Arena pieces (ui_battle.c): the referee's box and the judgment's panel, noted as the game shows them
#define NATIVE_ARENA_REFEREE_BOX 0
#define NATIVE_ARENA_JUDGMENT    1
void BattleArena_NativeUiNote(u32 piece, bool32 shown);
#endif

#endif //GUARD_BATTLE_ARENA_H
