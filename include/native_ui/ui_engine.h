#ifndef GUARD_NATIVE_UI_ENGINE_H
#define GUARD_NATIVE_UI_ENGINE_H

/*
 * Read-only views of the engine's state for the native PC UI (src/native_ui).
 *
 * Each accessor lives next to the state it reads (most of it is file-static)
 * and is compiled only with ENABLE_NATIVE_UI (Makefile_pc DIORAMA=1). They are
 * called on the main thread at the frame boundary, while the game thread is
 * parked in VBlankIntrWait: they may read anything and change nothing. The UI
 * never drives a menu through them; it presses the engine's own buttons.
 */
#include "global.h"

/* ---------------------------------------------------------------- title screen (title_screen.c) */

enum NativeTitlePhase
{
    NATIVE_TITLE_NONE,          /* not the title (or one of its maintenance screens) */
    NATIVE_TITLE_LOADING,       /* CB2_InitTitleScreen: loading, fading in from white */
    NATIVE_TITLE_LOGO,          /* phase 1: the logo shines (any button skips) */
    NATIVE_TITLE_BANNER,        /* phase 2: the version banner (any button skips) */
    NATIVE_TITLE_PRESS_START,   /* phase 3: A or START opens the main menu */
    NATIVE_TITLE_TO_MAIN_MENU,  /* fading to white, then the main menu */
    NATIVE_TITLE_TO_INTRO,      /* the music ended: fading back to the intro */
};
u32 TitleScreen_NativeUiPhase(void);

/* ---------------------------------------------------------------- main menu (main_menu.c) */

enum NativeMainMenuPhase
{
    NATIVE_MAIN_MENU_NONE,      /* not the main menu (the new game speech is not part of it) */
    NATIVE_MAIN_MENU_LOADING,   /* fading in, checking the save */
    NATIVE_MAIN_MENU_MESSAGE,   /* a save or clock problem is reported; A continues */
    NATIVE_MAIN_MENU_INPUT,     /* the rows take input */
    NATIVE_MAIN_MENU_CHOSEN,    /* A was pressed: fading out to the chosen row */
    NATIVE_MAIN_MENU_BACK,      /* B was pressed: fading back to the title */
};
enum NativeMainMenuRow
{
    NATIVE_MAIN_MENU_CONTINUE,
    NATIVE_MAIN_MENU_NEW_GAME,
    NATIVE_MAIN_MENU_OPTION,
    NATIVE_MAIN_MENU_MYSTERY_GIFT,
    NATIVE_MAIN_MENU_MYSTERY_EVENTS,
};
struct NativeMainMenu
{
    u8 phase;
    u8 rowCount;
    u8 cursor;
    u8 rows[5];                 /* enum NativeMainMenuRow, in the engine's order */
    bool8 ready;                /* a button pressed now is read (the input task runs) */
    const u8 *message;          /* MESSAGE: the engine's text */
};
bool32 MainMenu_NativeUiState(struct NativeMainMenu *out);
/* The saved game the CONTINUE row loads (what the engine's continue window
 * shows, plus the party and where the save was made). */
struct NativeSaveSummary
{
    u8 playerName[PLAYER_NAME_LENGTH + 1];
    u8 gender;
    u16 hours;
    u8 minutes;
    bool8 hasPokedex;
    u16 dexCaught;
    u8 badges;
    u8 location[32];            /* the map's name */
    u8 partyCount;
    u16 species[PARTY_SIZE];
    u32 personality[PARTY_SIZE];
    bool8 isEgg[PARTY_SIZE];
    bool8 fainted[PARTY_SIZE];
};
void MainMenu_NativeUiSaveSummary(struct NativeSaveSummary *out);

/* ---------------------------------------------------------------- start menu (start_menu.c) */

enum NativeStartMenuMode
{
    NATIVE_START_MENU_NONE,
    NATIVE_START_MENU_OPENING,  /* the window is being built */
    NATIVE_START_MENU_BROWSE,   /* the rows take input */
    NATIVE_START_MENU_CHOSEN,   /* a row was chosen: its screen is opening (fade) */
    NATIVE_START_MENU_SAVE,     /* the save dialog (see saveStep) */
    NATIVE_START_MENU_OTHER,    /* Battle Pyramid retire or another dialog of the menu */
};
enum NativeStartMenuAction
{
    NATIVE_START_POKEDEX,
    NATIVE_START_POKEMON,
    NATIVE_START_BAG,
    NATIVE_START_POKENAV,
    NATIVE_START_PLAYER,        /* the trainer card (or Frontier Pass) */
    NATIVE_START_SAVE,
    NATIVE_START_OPTION,
    NATIVE_START_EXIT,
    NATIVE_START_RETIRE_SAFARI,
    NATIVE_START_PLAYER_LINK,
    NATIVE_START_REST_FRONTIER,
    NATIVE_START_RETIRE_FRONTIER,
    NATIVE_START_PYRAMID_BAG,
    NATIVE_START_DEBUG,
    NATIVE_START_DEXNAV,
    NATIVE_START_PLAYER_OPTIONS, /* the player's name: opens the player submenu */
    NATIVE_START_TRAINER_CARD,
    NATIVE_START_PLAYERS,
    NATIVE_START_PLAYER_BACK,
    NATIVE_START_RIDE,
    NATIVE_START_GET_OFF,
    NATIVE_START_STYLE,
    NATIVE_START_UNKNOWN,
};
enum NativeSaveStep
{
    NATIVE_SAVE_NONE,
    NATIVE_SAVE_ASK,            /* "Would you like to save the game?" printing */
    NATIVE_SAVE_ASK_YES_NO,     /* ... YES / NO */
    NATIVE_SAVE_OVERWRITE,      /* "There is already a saved file..." printing */
    NATIVE_SAVE_OVERWRITE_YES_NO,
    NATIVE_SAVE_SAVING,         /* "SAVING... DON'T TURN OFF THE POWER." (the write happens) */
    NATIVE_SAVE_SAVED,          /* the engine reports the save succeeded */
    NATIVE_SAVE_FAILED,         /* the engine reports the save failed */
};
struct NativeStartMenu
{
    u8 mode;
    u8 count;
    u8 cursor;
    u8 actions[9];              /* enum NativeStartMenuAction */
    const u8 *labels[9];        /* the engine's own labels ({PLAYER} for the name rows) */
    bool8 playerSubmenu;
    bool8 ready;                /* a button pressed now is read */
    u8 saveStep;                /* enum NativeSaveStep */
    bool8 saveDefaultNo;        /* the YES / NO starts on NO */
    bool8 safariOrPyramid;      /* a Safari / Pyramid extra window is up (kept as GBA) */
};
bool32 StartMenu_NativeUiState(struct NativeStartMenu *out);
/* field_screen_effect.c: the start menu reopens after a screen (fading in) */
bool32 FieldScreenEffect_NativeUiStartMenuFadingIn(void);

/* ---------------------------------------------------------------- options (option_menu.c) */

enum NativeOptionPhase
{
    NATIVE_OPTION_NONE,
    NATIVE_OPTION_LOADING,
    NATIVE_OPTION_INPUT,
    NATIVE_OPTION_LEAVING,      /* saved; fading out */
};
enum NativeOptionRow
{
    NATIVE_OPTION_TEXT_SPEED,
    NATIVE_OPTION_BATTLE_SCENE,
    NATIVE_OPTION_BATTLE_STYLE,
    NATIVE_OPTION_SOUND,
    NATIVE_OPTION_BUTTON_MODE,
    NATIVE_OPTION_FRAME,
    NATIVE_OPTION_CANCEL,
    NATIVE_OPTION_ROW_COUNT,
};
struct NativeOptionMenu
{
    u8 phase;
    u8 cursor;                  /* enum NativeOptionRow */
    u8 values[NATIVE_OPTION_CANCEL];
    u8 textSpeedCount;          /* the text speeds this build offers */
    u8 frameCount;              /* the window frames this build offers */
    bool8 ready;
};
bool32 OptionMenu_NativeUiState(struct NativeOptionMenu *out);

/* ---------------------------------------------------------------- multiplayer (multiplayer.c) */

/* the session's other players (i < MULTIPLAYER_MAX_PEERS); FALSE: none there or offline */
bool32 Multiplayer_NativeUiPeer(u32 i, u32 *playerId, u8 *mapGroup, u8 *mapNum, u8 *gender, u8 *activity);

/* ---------------------------------------------------------------- game art */

/* pokemon_icon.c: a party / box icon's 4bpp tiles (two 32x32 frames) and palette */
void PokemonIcon_NativeUiArt(u16 species, u32 personality, bool32 isEgg, const u8 **tiles, const u16 **palette);

/* ---------------------------------------------------------------- text printers (text.c) */

/* The printer on a window: the start of its string, how far it has printed,
 * whether it is still printing. FALSE when the window has no printer. */
bool32 TextPrinter_NativeUiWindow(u8 windowId, const u8 **start, const u8 **current, bool8 *active);

/* ---------------------------------------------------------------- platform (src/platform/sdl2.c) */

/* the PC's F10 settings menu is open (the game gets no buttons meanwhile) */
bool8 Platform_NativeUiPcMenuOpen(void);
/* save file writes since start-up: written, failed; *pending: an image waits for the next write */
void Platform_NativeUiSaveWrites(u32 *written, u32 *failed, bool8 *pending);

#endif // GUARD_NATIVE_UI_ENGINE_H
