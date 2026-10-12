#ifndef GUARD_NATIVE_UI_RESULTS_H
#define GUARD_NATIVE_UI_RESULTS_H

/*
 * 26 · Battle results, drawn by the battle HUD (ui_battle.c): the level-up box
 * while the engine shows its own, and the rewards ledger when a trainer battle
 * ends (ui_results.c).
 */
#include <stdbool.h>

/* the frame boundary, from the battle HUD's capture */
void UiResults_Capture(void);
/* the engine's level-up box (or its banner) runs: the GBA layers BG1 / BG2 are left out */
bool UiResults_LevelUpActive(void);
/* ... and waits for a button */
bool UiResults_LevelUpWaiting(void);
/* the rewards ledger is up (the HUD leaves the player's databoxes out) */
bool UiResults_LedgerShown(void);
/* draws the ledger and the defeated trainer's plate, then the level-up box;
 * L, R, T: the canvas's left, right and top in content coordinates */
void UiResults_Draw(int L, int R, int T, int t);

#endif // GUARD_NATIVE_UI_RESULTS_H
