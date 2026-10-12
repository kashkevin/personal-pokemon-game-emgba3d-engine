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
#include "pokemon_storage_system.h"

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

/* ---------------------------------------------------------------- party (party_menu.c) */

enum NativePartyPhase
{
    NATIVE_PARTY_NONE,
    NATIVE_PARTY_LOADING,
    NATIVE_PARTY_BROWSE,        /* choosing a Pokémon (or CONFIRM / CANCEL) */
    NATIVE_PARTY_MENU,          /* the selection menu is open (actions[]) */
    NATIVE_PARTY_MOVES,         /* choosing one of a Pokémon's moves (PP items) */
    NATIVE_PARTY_YES_NO,        /* a YES / NO question */
    NATIVE_PARTY_LEVEL_UP,      /* the level-up stats window (levelUpPage) */
    NATIVE_PARTY_SWAPPING,      /* two slots trade places */
    NATIVE_PARTY_MESSAGE,       /* a message (A continues once it is printed) */
    NATIVE_PARTY_CLOSING,
};
struct NativePartyMenu
{
    u8 phase;
    u8 menuType;                /* enum PartyMenuType */
    u8 layout;                  /* enum PartyMenuLayout */
    u8 action;                  /* PARTY_ACTION_* */
    u8 cursor;                  /* the slot the buttons move: 0-5 Pokémon, 6 CONFIRM, 7 CANCEL */
    u8 slotId;                  /* the chosen Pokémon */
    s8 carried;                 /* switching: the Pokémon being moved (-1 none) */
    s8 swapWith;                /* SWAPPING: the other slot */
    bool8 chooseHalf;           /* the screen has a CONFIRM button */
    u16 item;                   /* the item being used or given (ITEM_NONE: none) */
    u8 numActions;
    u8 actions[8];              /* MENU_* (party_menu.c) */
    const u8 *actionLabels[8];
    bool8 actionIsFieldMove[8];
    s8 switchRow;               /* MENU: the row of SWITCH (-1 none) */
    s8 summaryRow;              /* MENU: the row of SUMMARY (-1 none) */
    u8 menuCursor;              /* MENU, MOVES, YES_NO */
    s16 promptId;               /* the prompt line shown (PARTY_MSG_*), -1 none */
    u8 promptWindow;            /* its window (WINDOW_NONE: none) */
    u8 msgWindow;               /* the message box's window when shown (WINDOW_NONE: not shown) */
    u8 slotDesc[PARTY_SIZE];    /* PARTYBOX_DESC_* + 1 shown in a slot instead of its HP, 0 none */
    u8 levelUpPage;             /* 1: the gains, 2: the new stats */
    u16 levelUpOld[6];          /* HP, Attack, Defense, Sp. Atk, Sp. Def, Speed */
    u16 levelUpNew[6];
    bool8 ready;                /* the engine reads buttons now */
};
bool32 PartyMenu_NativeUiState(struct NativePartyMenu *out);

/* ---------------------------------------------------------------- summary (pokemon_summary_screen.c) */

enum NativeSummaryPhase
{
    NATIVE_SUMMARY_NONE,
    NATIVE_SUMMARY_LOADING,
    NATIVE_SUMMARY_BROWSE,      /* up/down: Pokémon, left/right: pages */
    NATIVE_SUMMARY_TURNING,     /* a page or a Pokémon is changing */
    NATIVE_SUMMARY_MOVE_SELECT, /* a move page's cursor (moveCursor; 4: CANCEL) */
    NATIVE_SUMMARY_MOVE_SWITCH, /* moving moveCursor to moveCursor2 */
    NATIVE_SUMMARY_REPLACE,     /* learning newMove: the move to forget (4: the new move itself) */
    NATIVE_SUMMARY_CANT_FORGET, /* "HM moves can't be forgotten now." */
    NATIVE_SUMMARY_CLOSING,
};
#define NATIVE_SUMMARY_LIST_MAX 30
struct NativeSummaryEntry
{
    u8 index;                   /* in the engine's list */
    u16 species;
    u32 personality;
    bool8 isEgg;
    bool8 fainted;
};
struct NativeSummary
{
    u8 phase;
    u8 mode;                    /* SUMMARY_MODE_* */
    u8 page;                    /* PSS_PAGE_* */
    u8 minPage;
    u8 maxPage;
    bool8 isBoxMon;
    bool8 isEgg;
    u8 monIndex;
    u8 moveCursor;
    u8 moveCursor2;
    u16 newMove;
    bool8 lockMons;
    bool8 lockMoves;
    bool8 canRename;            /* A on the info page renames (the game's rule) */
    bool8 canRelearn;           /* START on a move page opens the move relearner */
    bool8 ivEvShown;            /* the game shows IVs and EVs (its setting) */
    bool8 isLucky;              /* a Lucky Pokémon (multiplayer trades) */
    bool8 hideOT;               /* rental Pokémon: no Trainer shown */
    struct Pokemon mon;         /* the Pokémon shown (a box Pokémon with its stats) */
    u8 otName[PLAYER_NAME_LENGTH + 1];
    u8 otGender;
    u32 otId;
    u8 nature;                  /* the memo's nature */
    u8 statNature;              /* the nature its stats follow (a mint's) */
    u8 ribbonCount;
    u8 markings;
    u16 ballItem;
    u8 memo[200];               /* the Trainer Memo as the game writes it (colour codes kept) */
    const u8 *eggState;
    u8 listCount;
    u8 listPos;                 /* the shown Pokémon's place in list[] */
    struct NativeSummaryEntry list[NATIVE_SUMMARY_LIST_MAX];
    bool8 ready;
};
bool32 Summary_NativeUiState(struct NativeSummary *out);

/* ---------------------------------------------------------------- bag (item_menu.c) */

enum NativeBagPhase
{
    NATIVE_BAG_NONE,
    NATIVE_BAG_LOADING,
    NATIVE_BAG_BROWSE,          /* the list: up/down items, left/right pockets */
    NATIVE_BAG_SWITCHING,       /* a pocket is sliding in */
    NATIVE_BAG_SWAPPING,        /* moving an item (swapFrom) to the cursor */
    NATIVE_BAG_MENU,            /* the item's (or the sort) menu: menuLabels, a 2-column grid when menuGrid */
    NATIVE_BAG_QUANTITY,        /* how many (toss, sell, deposit) */
    NATIVE_BAG_YES_NO,
    NATIVE_BAG_MESSAGE,
    NATIVE_BAG_TUTORIAL,        /* Wally's catching tutorial drives the bag */
    NATIVE_BAG_CLOSING,
};
#define NATIVE_BAG_MAX_ITEMS 100
struct NativeBag
{
    u8 phase;
    u8 location;                /* ITEMMENULOCATION_* (field, battle, shop, berry tree...) */
    u8 pocket;                  /* POCKET_* */
    bool8 pocketSwitchDisabled;
    bool8 victoryCatch;         /* the Poké Balls pocket is skipped */
    bool8 closeRow;             /* the list ends with the game's CLOSE BAG row */
    u16 count;                  /* items in the pocket */
    u16 cursor;                 /* the list position (count: CLOSE BAG) */
    u16 swapFrom;               /* SWAPPING: the item being moved (0xFFFF: none) */
    u16 items[NATIVE_BAG_MAX_ITEMS];
    u16 quantities[NATIVE_BAG_MAX_ITEMS];
    u16 registeredItem;
    u8 menuCount;
    bool8 menuGrid;
    u8 menuActions[6];
    const u8 *menuLabels[6];    /* NULL: an empty grid cell */
    u8 menuCursor;              /* MENU (grid position), YES_NO */
    u16 quantity;
    u16 maxQuantity;
    bool8 selling;
    u8 msgWindow;               /* the message box's window when shown (WINDOW_NONE: none) */
    u8 descWindow;              /* the description line's window */
    bool8 ready;
};
bool32 Bag_NativeUiState(struct NativeBag *out);
/* menu_helpers.c: the shared YES/NO, and the shared message (its window) */
bool32 MenuHelpers_NativeUiYesNo(void);
bool32 MenuHelpers_NativeUiMessage(u8 *windowId);

/* ---------------------------------------------------------------- Pokédex (pokedex.c, the classic one) */

enum NativePokedexPhase
{
    NATIVE_DEX_NONE,
    NATIVE_DEX_LOADING,         /* opening, or a list page loading and fading in */
    NATIVE_DEX_LIST,            /* the list reads buttons */
    NATIVE_DEX_SCROLLING,       /* the list moves a row */
    NATIVE_DEX_MENU,            /* the START menu: menuCursor of menuCount */
    NATIVE_DEX_OPENING,         /* the picture moves over to the entry page */
    NATIVE_DEX_INFO,            /* the entry page reads buttons: selectedScreen */
    NATIVE_DEX_INFO_LOADING,    /* the entry page loads, moves to another Pokémon, or closes */
    NATIVE_DEX_LEAVING,         /* fading out to one of the game's own pages */
    NATIVE_DEX_PAGE,            /* AREA, CRY, SIZE, the search: the game's own pages */
    NATIVE_DEX_CLOSING,         /* fading out (closing, or leaving search results) */
};
struct NativePokedexRow
{
    u16 dexNum;                 /* the national number (0xFFFF: an empty row) */
    u16 shownNum;               /* the number the game shows (the regional one in the Hoenn mode) */
    u16 species;
    u32 personality;            /* the picture's (the save's Unown and Spinda; else none) */
    bool8 seen;
    bool8 owned;
};
struct NativePokedex
{
    u8 phase;
    u8 dexMode;                 /* DEX_MODE_* */
    u8 dexOrder;                /* ORDER_* (pokedex_common.h) */
    bool8 nationalEnabled;
    bool8 searchResults;        /* the list is a search's results */
    u8 digits;                  /* the numbers' digits (3; 4 in a National mode over 999) */
    u16 seenCount;              /* the game's totals: the National Dex's once it is enabled, else Hoenn's */
    u16 ownCount;
    u16 regionalSeen;           /* Hoenn's (shown beside the National ones once enabled) */
    u16 regionalOwn;
    u16 listCount;
    u16 selected;               /* the list position */
    u8 menuCursor;
    u8 menuCount;               /* the START menu's rows (4; 5 in search results) */
    u8 selectedScreen;          /* the entry page's AREA, CRY, SIZE, CANCEL (pokedex_common.h) */
    struct NativePokedexRow entry;   /* the selected one */
    bool8 ready;
};
bool32 Pokedex_NativeUiState(struct NativePokedex *out);
/* the list's rows from first; returns how many there are */
u32 Pokedex_NativeUiRows(u32 first, u32 count, struct NativePokedexRow *rows);

/* ---------------------------------------------------------------- PC storage (pokemon_storage_system.c) */

enum NativeStoragePhase
{
    NATIVE_STORAGE_NONE,
    NATIVE_STORAGE_LOADING,     /* opening, or coming back from the summary, naming or the bag */
    NATIVE_STORAGE_MAIN,        /* the cursor reads buttons */
    NATIVE_STORAGE_BUSY,        /* the cursor or a Pokémon moves, the party slides, a box scrolls... */
    NATIVE_STORAGE_MENU,        /* menuLabels (menuCount), menuCursor */
    NATIVE_STORAGE_YES_NO,      /* menuCursor */
    NATIVE_STORAGE_CHOOSE_BOX,  /* which box (deposit, jump): chooseBox */
    NATIVE_STORAGE_MARKS,       /* markCursor (0-3 the marks, 4 OK, 5 CANCEL), marks */
    NATIVE_STORAGE_MESSAGE,     /* a message waits for a button */
    NATIVE_STORAGE_ITEM_INFO,   /* MOVE ITEMS: an item's description waits for a button */
    NATIVE_STORAGE_MULTI,       /* several Pokémon selected or moved at once (the game's own picture) */
    NATIVE_STORAGE_CLOSING,     /* the screen closes, or another screen opens */
};
enum NativeStorageMode
{
    NATIVE_STORAGE_WITHDRAW,
    NATIVE_STORAGE_DEPOSIT,
    NATIVE_STORAGE_MOVE_MONS,
    NATIVE_STORAGE_MOVE_ITEMS,
    NATIVE_STORAGE_SELECT_MON,  /* a script asks for a Pokémon */
};
/* the cursor's places, as the game numbers them (CURSOR_AREA_*) */
enum NativeStorageArea
{
    NATIVE_STORAGE_IN_BOX,      /* 0-29 */
    NATIVE_STORAGE_IN_PARTY,    /* 0-5, 6 the party panel's button */
    NATIVE_STORAGE_BOX_TITLE,
    NATIVE_STORAGE_BUTTONS,     /* 0 PARTY POKéMON, 1 CLOSE BOX */
};
/* the game's wallpapers (data/wallpapers.h order) */
enum NativeWallpaper
{
    NATIVE_WALLPAPER_FOREST,
    NATIVE_WALLPAPER_CITY,
    NATIVE_WALLPAPER_DESERT,
    NATIVE_WALLPAPER_SAVANNA,
    NATIVE_WALLPAPER_CRAG,
    NATIVE_WALLPAPER_VOLCANO,
    NATIVE_WALLPAPER_SNOW,
    NATIVE_WALLPAPER_CAVE,
    NATIVE_WALLPAPER_BEACH,
    NATIVE_WALLPAPER_SEAFLOOR,
    NATIVE_WALLPAPER_RIVER,
    NATIVE_WALLPAPER_SKY,
    NATIVE_WALLPAPER_POLKADOT,
    NATIVE_WALLPAPER_POKECENTER,
    NATIVE_WALLPAPER_MACHINE,
    NATIVE_WALLPAPER_PLAIN,
    NATIVE_WALLPAPER_FRIENDS,   /* Walda's: her two colours */
    NATIVE_WALLPAPER_COUNT,
};
struct NativeStorage
{
    u8 phase;
    u8 mode;                    /* enum NativeStorageMode */
    u8 box;                     /* the current box */
    u8 boxCount;
    s8 scrollTo;                /* the box the game is scrolling to (-1: none) */
    s8 scrollDir;               /* 1: to the right */
    u8 cursorArea;              /* enum NativeStorageArea */
    s8 cursorPos;
    bool8 partyShown;           /* the game's party panel is out (its cursor can go there) */
    bool8 quickMode;            /* SELECT's auto action: A acts without the menu */
    bool8 holdingMon;
    struct Pokemon heldMon;
    u16 heldItem;               /* MOVE ITEMS: the item in the hand (ITEM_NONE: none) */
    u8 menuCount;
    u8 menuCursor;
    const u8 *menuLabels[7];
    u8 chooseBox;
    u8 markCursor;
    bool8 marks[4];
    bool8 messageShown;
    u8 message[64];
    u16 waldaColors[2];         /* Walda's wallpaper's background and foreground (BGR555) */
    bool8 ready;
};
bool32 Storage_NativeUiState(struct NativeStorage *out);
struct NativeStorageBox
{
    u8 name[BOX_NAME_LENGTH + 1];
    u8 wallpaper;               /* enum NativeWallpaper */
    u8 count;
    u16 species[IN_BOX_COUNT];  /* SPECIES_NONE: empty */
    u32 personality[IN_BOX_COUNT];
    bool8 isEgg[IN_BOX_COUNT];
    u16 heldItem[IN_BOX_COUNT];
};
void Storage_NativeUiBox(u32 box, struct NativeStorageBox *out);
/* the Pokémon at a box position as a party Pokémon (FALSE: empty) */
bool32 Storage_NativeUiBoxMon(u32 box, u32 pos, struct Pokemon *out);

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

/* The last string printed on a window: the engine's pointer to it (to tell
 * which string it was; its buffer may have changed since), how far the printer
 * is, whether it is still printing. FALSE when nothing was printed there. */
bool32 TextPrinter_NativeUiWindow(u8 windowId, const u8 **start, const u8 **current, bool8 *active);
/* ... a copy of that string as it was printed (up to 255 bytes) and how many
 * of its bytes are on screen so far */
bool32 TextPrinter_NativeUiWindowText(u8 windowId, u8 *dst, u32 size, u32 *printed, bool8 *active);
/* the window's printer waits for a button (a page break or a pause) */
bool32 TextPrinter_NativeUiWindowWaiting(u8 windowId);

/* window.c: the window's tiles are on its background now (shown, not cleared) */
bool32 Window_NativeUiShown(u8 windowId);

/* ---------------------------------------------------------------- platform (src/platform/sdl2.c) */

/* the PC's F10 settings menu is open (the game gets no buttons meanwhile) */
bool8 Platform_NativeUiPcMenuOpen(void);
/* save file writes since start-up: written, failed; *pending: an image waits for the next write */
void Platform_NativeUiSaveWrites(u32 *written, u32 *failed, bool8 *pending);

#endif // GUARD_NATIVE_UI_ENGINE_H
