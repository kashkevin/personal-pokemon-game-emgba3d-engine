#ifndef GUARD_MULTIPLAYER_SOFT_RESET_H
#define GUARD_MULTIPLAYER_SOFT_RESET_H

// Update 32 (native PC game only): the soft reset. The GBA ROM build compiles none of this.
//
// Without a world (the game started on its own, single play on this PC) the game asks, then resets as the classic
// A+B+START+SELECT does: the title screen, and CONTINUE loads the last save. In a world the save that counts is the
// host's (in-game SAVE writes the local copy; the launcher's save, its autosave and an in-game SAVE in a world make
// the host's): the Players menu opens on its soft reset page, and on YES the launcher closes the game without saving
// and starts it again from the host's save (the app: player_menu.py, launcher_session.py). Either way the game boots
// afresh, so a Pokemon received after that save (a starter from Birch's lab) is rolled anew.
//
// MS-61 (the owner's request): the reset is a key only, no menu row. The key is Ctrl+R or A+B+START+SELECT held
// together (multiplayer_local_reset.c claims the chord, src/platform/sdl2.c sends it for Ctrl+R). The game takes it
// only where START would open the menu (the field, standing or at a tile's centre, nothing running) or while the
// START menu is open, never while a save, a trade or a battle has disabled the soft reset; anywhere else the press
// is dropped. In a battle the chord keeps its Local Soft Reset meaning.
//
// MS-61 too (the owner's request): in a world an in-game SAVE is the host's save as well. Once START > SAVE has
// written the game's own save, the field opens the Players menu on page 3 as soon as it is free; the companion closes
// it at once and the launcher saves on the host, as its Save now does, then a banner says how that went. So the
// reset key goes back to the last in-game SAVE (or a later autosave or Save now).

#ifdef PORTABLE
bool32 MultiplayerSoftReset_Begin(void);    // the question (no world) or PLAYERS on its reset page; FALSE: refused
void MultiplayerSoftReset_Do(void);         // special: the reset itself, after YES (data/scripts/mp_soft_reset.inc)
void MultiplayerSoftReset_KeyPressed(void); // the reset key was pressed this frame (the chord's first full frame)
void MultiplayerSoftReset_KeyFrame(void);   // each frame before the callbacks: drops a press the game cannot take
bool32 MultiplayerSoftReset_TakeKey(void);  // the field or the START menu takes a waiting press (TRUE once)
void MultiplayerHostSave_GameSaved(void);   // START > SAVE wrote the game's save: in a world the host saves next
bool32 MultiplayerHostSave_TryOpen(void);   // the free field: PLAYERS on its host save page (TRUE: it opened)
#endif

#endif // GUARD_MULTIPLAYER_SOFT_RESET_H
