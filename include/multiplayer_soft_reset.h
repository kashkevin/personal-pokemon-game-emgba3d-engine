#ifndef GUARD_MULTIPLAYER_SOFT_RESET_H
#define GUARD_MULTIPLAYER_SOFT_RESET_H

// Update 32 (native PC game only): START > PLAYER > RESET. The GBA ROM build compiles none of this.
//
// Without a world (the game started on its own, single play on this PC) the game asks, then resets as the classic
// A+B+START+SELECT does: the title screen, and CONTINUE loads the last save. In a world the save that counts is the
// host's (in-game SAVE writes the local copy; the launcher's save, its autosave and an in-game SAVE in a world make
// the host's): the Players menu opens on its soft reset page, and on YES the launcher closes the game without saving
// and starts it again from the host's save (the app: player_menu.py, launcher_session.py). Either way the game boots
// afresh, so a Pokemon received after that save (a starter from Birch's lab) is rolled anew.

#ifdef PORTABLE
void MultiplayerSoftReset_Begin(void); // START > PLAYER > RESET
void MultiplayerSoftReset_Do(void);    // special: the reset itself, after YES (data/scripts/mp_soft_reset.inc)
#endif

#endif // GUARD_MULTIPLAYER_SOFT_RESET_H
