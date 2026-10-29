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
    NATIVE_START_SOFT_RESET,    /* MS-61: no menu has it now (the reset is a key); kept named all the same */
    NATIVE_START_TOWER,         /* Battle Tower: Legends, in a world (START > PLAYER > TOWER) */
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
    u8 actions[9];              /* MENU_* (party_menu.c): SUMMARY, four field moves, MOVES, SWITCH, ITEM, CANCEL */
    const u8 *actionLabels[9];
    bool8 actionIsFieldMove[9];
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
    u8 hmCanGo;                 /* bit per move slot: an HM's move that move switching may take out (the HM in the Bag) */
    bool8 ivEvShown;            /* the game shows IVs and EVs: the player's own Pokémon (ShouldShowIvEvPrompt) */
    u8 skillsMode;              /* SUMMARY_SKILLS_MODE_*: A on the skills page turns the IVs and EVs on (IVS) and off */
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
/* the summary is up and goes on to that main callback when it closes (the move relearner's move to forget) */
bool32 Summary_NativeUiExitsTo(void (*callback)(void));

/* ---------------------------------------------------------------- move relearner (move_relearner.c) */

/* The move relearner as the engine shows it - MOVES in the party menu (move switching), the summary's relearner (START
 * on a move page), the Move Reminder's: its lists, the list shown and its cursor, the move being taught and the step of
 * teaching it (chooseboxmon.c LearnMove) with its message and YES / NO. The move to forget is chosen in the summary's
 * move list (phase FORGET: Summary_NativeUiState, mode SUMMARY_MODE_SELECT_MOVE). */
enum NativeRelearnerPhase
{
    NATIVE_RELEARNER_NONE,
    NATIVE_RELEARNER_LOADING,   /* the screen is being built and fades in (returning: back from the move to forget) */
    NATIVE_RELEARNER_BROWSE,    /* the list takes input */
    NATIVE_RELEARNER_TEACHING,  /* teaching the move: step's message, or its YES / NO */
    NATIVE_RELEARNER_GIVE_UP,   /* B on the list: "Stop trying to learn new moves for ...?", then YES / NO */
    NATIVE_RELEARNER_FORGET,    /* the summary's move list chooses the move to forget (Summary_NativeUiState) */
    NATIVE_RELEARNER_CLOSING,   /* fading out to where it came from */
};
enum NativeLearnStep
{
    NATIVE_LEARN_NONE,
    NATIVE_LEARN_TEACH_ASK,     /* "Teach X?" */
    NATIVE_LEARN_REPLACE_ASK,   /* "... wants to learn X. However, ... already knows four moves. Should a move be deleted?" */
    NATIVE_LEARN_STOP_ASK,      /* "Stop trying to teach X?" */
    NATIVE_LEARN_WHICH,         /* "Which move should be forgotten?" (then the summary's move list) */
    NATIVE_LEARN_WARN,          /* move switching: "Y can't be switched back in later." / "Y's PP Ups will be lost." ... */
    NATIVE_LEARN_FORGOT,        /* "1, 2, and... ... Poof! ... forgot Y. And..." */
    NATIVE_LEARN_LEARNED,       /* "... learned X!" (into an empty slot) */
    NATIVE_LEARN_NOT_LEARNED,   /* "... did not learn the move X." */
    NATIVE_LEARN_DONE,          /* its last message, then the list again */
};
/* chooseboxmon.c: the step of LearnMove's state, whether its YES / NO is up and whether it started on NO */
u32 LearnMove_NativeUiStep(s32 learnState, bool8 *yesNo, bool8 *noFirst);
#define NATIVE_RELEARNER_MAX_MOVES 60   /* MAX_RELEARNER_MOVES */
struct NativeRelearnerMove
{
    u16 move;
    u8 source;                  /* enum MoveSwitchSourceKind (move_relearner.h) */
    u8 number;                  /* the level (0: on evolving), the TM's or HM's number, the tutor */
    u16 species;                /* the form whose learnset or egg moves have it */
};
struct NativeMoveRelearner
{
    u8 phase;
    u8 mode;                    /* enum RelearnMode */
    u8 step;                    /* TEACHING: enum NativeLearnStep */
    u8 list;                    /* enum MoveRelearnerStates: the list shown */
    bool8 switching;            /* move switching (MOVES, the summary's relearner): the lists change, warnings */
    bool8 listOn[4];            /* the game has this list here */
    u8 listCount[4];            /* the moves it has now (SELECT, L and R skip an empty one) */
    bool8 contest;              /* the details show the contest data (LEFT / RIGHT) */
    bool8 contestData;          /* the game shows contest data (!C_HIDE_CONTEST_DATA) */
    bool8 returning;            /* LOADING: back from the summary's move list (the same screen to the player) */
    u16 count;                  /* moves on the list (CANCEL after them) */
    u16 cursor;                 /* the list's row (count: CANCEL) */
    u16 scroll;                 /* the first row the game shows */
    struct NativeRelearnerMove moves[NATIVE_RELEARNER_MAX_MOVES];
    u16 teaching;               /* the move being taught (MOVE_NONE: none) */
    u8 forgetSlot;              /* WARN, FORGOT: the slot of the move being forgotten (MAX_MON_MOVES: none) */
    bool8 yesNo;                /* a YES / NO is up */
    u8 yesNoCursor;             /* 0: YES */
    bool8 noFirst;              /* it started on NO (the warnings) */
    u8 msgWindow;               /* the message box's window */
    bool8 msgStale;             /* its text is the relearner's own prompt printed again on its restart (back from the
                                   move to forget, before the next step's message) */
    struct Pokemon mon;         /* the Pokémon (a box Pokémon with its stats) */
    bool8 isBoxMon;
    bool8 canComeBack[MAX_MON_MOVES]; /* a known move a list would offer again once it is switched out */
    bool8 ppUps[MAX_MON_MOVES];       /* that slot has PP Ups */
    bool8 hmLocked[MAX_MON_MOVES];    /* an HM's move that can't be forgotten here */
    bool8 ready;                /* the engine reads buttons now */
};
bool32 MoveRelearner_NativeUiState(struct NativeMoveRelearner *out);

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
/* whose they are: whether a task runs one of the two helpers now, the
 * message's next step and the YES/NO's YES (NULL: not up) */
bool32 MenuHelpers_NativeUiRunsHelper(u8 taskId);
void (*MenuHelpers_NativeUiMessageNext(void))(u8);
void (*MenuHelpers_NativeUiYesFunc(void))(u8);
/* the list menus' move line's sprite sheet tag */
u16 MenuHelpers_NativeUiSwapLineTag(void);

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
    NATIVE_DEX_PAGE,            /* the search (Pokedex_NativeUiSearch) */
    NATIVE_DEX_CLOSING,         /* fading out (closing, or leaving search results) */
    NATIVE_DEX_SUBPAGE,         /* the entry's AREA, CRY or SIZE page (subPage): loading, up, or leaving */
};
/* the entry's pages: subPage (pokedex_common.h's AREA_SCREEN, CRY_SCREEN, SIZE_SCREEN) */
#define NATIVE_DEX_NO_SUBPAGE 0xFF
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
    u8 subPage;                 /* NATIVE_DEX_SUBPAGE: AREA_SCREEN, CRY_SCREEN, SIZE_SCREEN; the page a
                                   LEAVING entry page goes to; else NATIVE_DEX_NO_SUBPAGE */
    bool8 subPageLeaving;       /* NATIVE_DEX_SUBPAGE: fading out (to subPageNext) */
    u8 subPageNext;             /* ... to this page (NATIVE_DEX_NO_SUBPAGE: the entry page) */
    struct NativePokedexRow entry;   /* the selected one */
    bool8 ready;                /* the list, START menu, entry page or sub page reads buttons */
};
bool32 Pokedex_NativeUiState(struct NativePokedex *out);
/* the list's rows from first; returns how many there are */
u32 Pokedex_NativeUiRows(u32 first, u32 count, struct NativePokedexRow *rows);

/* the AREA page (pokedex_area_screen.c): where the species lives on the region map */
#define NATIVE_DEX_AREA_MAX 64
#define NATIVE_DEX_AREA_MARKERS 32
struct NativeDexArea
{
    bool8 ready;                /* the page reads buttons */
    bool8 leaving;              /* fading out, to next: 1 the entry page, 2 CRY, 3 this page again */
    u8 next;
    bool8 found;                /* the areas have been worked out */
    u16 species;
    u8 regionMapType;           /* gRegionMapInfos' index */
    u16 overworldCount;
    u16 overworldSections[NATIVE_DEX_AREA_MAX];   /* map sections that glow */
    u16 specialCount;
    u16 specialSections[NATIVE_DEX_AREA_MARKERS]; /* caves and special places: markers */
    s16 markerX[NATIVE_DEX_AREA_MARKERS];          /* the markers' centres (screen pixels) */
    s16 markerY[NATIVE_DEX_AREA_MARKERS];
    bool8 markersShown;         /* the markers are in their visible half of the blink */
    bool8 showingMarkers;       /* the glow rests while markers blink */
    u8 glowEva;                 /* the glow's blend weights (0..16) */
    u8 glowEvb;
    const u16 *glowTilemap;     /* 32 x 20 BG entries of the glow tiles (NULL: none yet) */
    const u32 *glowGfx;         /* the glow tiles (compressed 4bpp) and palette */
    const u16 *glowPalette;
    const u8 *markerGfx;        /* the marker (16x16 4bpp) and palette */
    const u16 *markerPalette;
    bool8 playerShown;          /* the player's icon on the map, at these screen pixels (centre) */
    s16 playerX;
    s16 playerY;
    bool8 female;
    s16 mapScrollY;             /* the map's vertical offset on screen */
};
bool32 PokedexArea_NativeUiState(struct NativeDexArea *out);
/* the region map's player icon (16x16 4bpp) and its palette (region_map.c) */
const u8 *RegionMap_NativeUiPlayerIcon(bool32 female, const u16 **palette);

/* wallclock.c: the wall clock (setting it at the start, CB2_StartWallClock, or looking at one, CB2_ViewWallClock) */
struct NativeWallClock
{
    bool8 setting;              /* the player sets it (else looks at it) */
    bool8 viewing;
    u8 hours, minutes;          /* the time it shows (0-23, 0-59) */
    u16 minuteAngle, hourAngle; /* its hands as the game turns them (degrees clockwise from 12) */
    bool8 pm;                   /* its AM / PM sign */
    bool8 female;               /* the girl's clock (else the boy's) */
    bool8 adjusting;            /* left and right turn the hands, A asks */
    bool8 asking;               /* "Is this the correct time?" and its YES / NO */
    u8 yesNoCursor;
    bool8 input;                /* the game reads buttons */
};
bool8 WallClock_NativeUiState(struct NativeWallClock *out);

/* egg_hatch.c: an Egg hatching (CB2_EggHatch), as the game plays it now (screens/ui_egg_hatch.c) */
#define NATIVE_EGG_SHARDS 20
struct NativeEggHatch
{
    u8 state;                   /* the scene's step (egg_hatch.c CB2_EggHatch) */
    bool8 ready;                /* the game reads buttons (the nickname's YES / NO) */
    u16 species;
    u32 personality;
    bool8 shiny;
    u8 windowId;                /* the message's window */
    bool8 yesNo;
    u8 yesNoCursor;
    bool8 monAnim;              /* the Pokemon's front animation plays */
    bool8 eggShown;
    s16 eggX, eggY;             /* the egg's centre, shaking, in GBA pixels */
    u8 eggTiles[16 * 32];       /* its picture now (32x32, 4bpp sprite tiles: whole or cracked) */
    u16 eggPalette[16];
    bool8 monShown;
    s16 monX, monY;             /* the Pokemon's centre as it rises */
    u16 monAffine;              /* its OAM matrix scale (256 its size; more: smaller, as it comes out) */
    u8 shardCount;
    struct { s16 x, y; u8 frame; } shards[NATIVE_EGG_SHARDS];
};
bool8 EggHatch_NativeUiState(struct NativeEggHatch *out);

/* The Fly map (region_map.c) and the wall map (field_region_map.c) as the game has them now (screens/ui_region_map.c).
 * Cells are the cursor's coordinates: cell (x, y) is the map art's 8x8 tile x, y. */
#define NATIVE_REGION_FLY_MAX 24
enum NativeRegionMapKind
{
    NATIVE_REGION_MAP_FLY = 1,
    NATIVE_REGION_MAP_TOWN,
    NATIVE_REGION_MAP_POKENAV,  /* the PokéNav's HOENN MAP: it zooms (A), R flies with the Poke Rider */
};
struct NativeRegionFlySpot
{
    u16 mapSec;
    u8 x, y, w, h;              /* its cells */
    bool8 visited;              /* the game's bright icon (else its dim one) */
    bool8 outline;              /* the Battle Frontier's red outline */
};
struct NativeRegionMapState
{
    u8 kind;                    /* enum NativeRegionMapKind */
    u8 regionMapType;
    bool8 input;                /* the map takes input (not fading in or out) */
    bool8 canFly;               /* A flies to the cursor's place (the Fly map); R does (the wall map, the Poke Rider) */
    bool8 female;
    bool8 playerShown;
    u16 cursorX, cursorY;       /* the cursor's cell */
    s16 cursorPixX, cursorPixY; /* its sprite's centre as it glides from cell to cell, in GBA pixels */
    u16 playerX, playerY;       /* the player's cell */
    u16 mapSec;
    u8 mapSecType;
    u8 name[24];                /* the cursor's place */
    u8 subName[24];             /* a place with two names' second (Ever Grande City's), as the Fly map prints it */
    u8 flyCount;
    struct NativeRegionFlySpot fly[NATIVE_REGION_FLY_MAX];
    /* the PokéNav's */
    bool8 zoomed;               /* zoomed in: the map scrolls under a still cursor (cursorX, cursorY its cell) */
    bool8 offMap;               /* the player off the map (the event islands): the map dimmed, no cursor or zoom */
    s16 scrollX, scrollY;       /* zoomed in: the map's scroll, the art's pixel under the cursor's cell less 0x34, 0x44 */
    u8 posWithinMapSec;         /* which of the place's landmarks and town maps (a place over several cells) */
};
bool8 RegionMap_NativeUiState(struct NativeRegionMapState *out);
/* a clicked cell: the map's own input puts the cursor there next (as SELECT's jump to a trainer does) */
void RegionMap_NativeUiMoveCursor(u16 x, u16 y);
struct RegionMap;
/* field_region_map.c: the wall map is open (its map set up) */
bool8 FieldRegionMap_NativeUiOpen(bool8 *input, bool8 *canFly, struct RegionMap **map);
/* pokenav_region_map.c: the PokéNav's HOENN MAP is up (its map set up); *offMap: the player is off the map */
bool8 PokenavRegionMap_NativeUiOpen(bool8 *input, bool8 *canFly, bool8 *offMap, struct RegionMap **map);
/* ... a town's map as the HOENN MAP shows it zoomed in: its 10x10 tilemap (compressed) over the town map tiles
 * (compressed, four bits a pixel) and their palette; NULL: no town map there */
const u32 *PokenavRegionMap_NativeUiTownMap(u16 mapSec, u8 posWithinMapSec, const u32 **tiles, const u16 **palette);
/* pokenav.c: the PokéNav's page while it runs (POKENAV_MAIN_MENU ...), else 0 */
u32 Pokenav_NativeUiMenuId(void);

/* the CRY page (pokedex_cry_screen.c): the waveform and the meter */
struct NativeDexCry
{
    bool8 active;               /* the cry screen is set up */
    bool8 playing;              /* the waveform is drawing a cry this frame */
    s8 amplitude[2];            /* the two samples drawn this frame (0 while flat) */
    s8 needle;                  /* the meter needle's rotation (32 quiet .. -32 loud; 256 a turn) */
};
bool32 PokedexCry_NativeUiState(struct NativeDexCry *out);

/* the search form (pokedex.c): SELECT on the list */
enum
{
    NATIVE_DEX_SEARCH_LOADING,
    NATIVE_DEX_SEARCH_TOPBAR,   /* SEARCH, SHIFT, CANCEL */
    NATIVE_DEX_SEARCH_MENU,     /* the form's rows */
    NATIVE_DEX_SEARCH_OPTIONS,  /* a row's option list */
    NATIVE_DEX_SEARCH_SEARCHING,
    NATIVE_DEX_SEARCH_DONE,     /* "Search completed." / no match: A goes on */
    NATIVE_DEX_SEARCH_EXITING,
};
#define NATIVE_DEX_SEARCH_ROWS 7     /* pokedex_common.h's SEARCH_NAME .. SEARCH_OK */
#define NATIVE_DEX_SEARCH_OPTIONS_MAX 24
struct NativeDexSearch
{
    u8 state;
    bool8 ready;                /* reads buttons */
    bool8 national;             /* the MODE row is there */
    u8 topBar;                  /* SEARCH_TOPBAR_* */
    u8 menuItem;                /* SEARCH_* */
    const u8 *values[NATIVE_DEX_SEARCH_ROWS];   /* each row's chosen option (NULL for OK) */
    u8 chosen[NATIVE_DEX_SEARCH_ROWS];          /* ... its index in the row's options */
    u8 optionCount;             /* NATIVE_DEX_SEARCH_OPTIONS: the open row's options */
    u8 optionCursor;            /* the cursor on screen (0..5) */
    u8 optionScroll;            /* the first option shown */
    const u8 *options[NATIVE_DEX_SEARCH_OPTIONS_MAX];
    const u8 *message;          /* what the game's text box says */
    u16 results;                /* NATIVE_DEX_SEARCH_DONE: the Pokémon found */
};
bool32 Pokedex_NativeUiSearch(struct NativeDexSearch *out);

/* the new entry's page after a catch (pokedex.c DisplayCaughtMonDexPage, in battle) */
enum
{
    NATIVE_DEX_NEW_LOADING,
    NATIVE_DEX_NEW_UP,          /* reads A or B */
    NATIVE_DEX_NEW_LEAVING,     /* the page fades, the Pokémon slides back */
};
struct NativeDexNewEntry
{
    u8 state;
    u16 species;
    u32 personality;
    u16 dexNum;                 /* national */
    u16 shownNum;               /* the number the page shows (regional without the National Dex) */
    bool8 national;             /* the National Dex is on */
    u16 seen;                   /* the totals the device shows */
    u16 own;
    u16 regionalSeen;
    u16 regionalOwn;
};
bool32 Pokedex_NativeUiNewEntry(struct NativeDexNewEntry *out);
/* a search row's options, as the game lists them; returns how many */
u32 Pokedex_NativeUiSearchOptions(u32 row, const u8 **titles, u32 max);

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
    NATIVE_STORAGE_CLOSING,     /* the screen closes, or another screen opens */
};
/* several Pokémon at once (MOVE POKéMON, the quick hand: A held over a Pokémon) */
enum NativeStorageMulti
{
    NATIVE_STORAGE_MULTI_NONE,
    NATIVE_STORAGE_MULTI_SELECTING, /* A held: multiFrom to multiTo (box positions) */
    NATIVE_STORAGE_MULTI_MOVING,    /* the hand carries the group (Storage_NativeUiGroup) */
    NATIVE_STORAGE_MULTI_PLACING,   /* the group is put down (already in the box) */
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
    bool8 lrBoxes;              /* L and R turn the boxes (the LR button mode) */
    bool8 bPutsBack;            /* B puts a held Pokémon back (OW_PC_PRESS_B) */
    bool8 holdingMon;
    struct Pokemon heldMon;
    u16 heldItem;               /* MOVE ITEMS: the item in the hand (ITEM_NONE: none) */
    u8 menuCount;
    u8 menuCursor;
    const u8 *menuLabels[7];
    s8 menuWallpaper[7];        /* a wallpaper menu's rows: their wallpaper (enum NativeWallpaper; -1: other rows) */
    u8 chooseBox;
    u8 markCursor;
    bool8 marks[4];
    bool8 messageShown;
    u8 message[64];
    u16 waldaColors[2];         /* Walda's wallpaper's background and foreground (BGR555) */
    u8 multi;                   /* enum NativeStorageMulti */
    u8 multiFrom;               /* SELECTING: where it began */
    u8 multiTo;                 /* ... and the other corner (the cursor) */
    u8 groupPos;                /* MOVING, PLACING: the group's top left */
    u8 groupCols;
    u8 groupRows;
    s8 groupLift;               /* GBA pixels the group is lifted (0-8) */
    s8 groupSlideX;             /* MOVING: sixths of a cell the group is still to go */
    s8 groupSlideY;
    bool8 canPlace;             /* MOVING: nothing is in the way of putting it down here */
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
/* a box's name, wallpaper and how many Pokémon it holds (cheap: no Pokémon decoded) */
void Storage_NativeUiBoxInfo(u32 box, u8 *name, u8 *wallpaper, u8 *count);
/* the Pokémon at a box position as a party Pokémon (FALSE: empty) */
bool32 Storage_NativeUiBoxMon(u32 box, u32 pos, struct Pokemon *out);
/* the group the hand carries, row by row (groupCols a row; empty places too) */
void Storage_NativeUiGroup(struct NativeStorageBox *out);
bool32 Storage_NativeUiGroupMon(u32 index, struct Pokemon *out);

/* ---------------------------------------------------------------- the PC's first menus (field) */

#define NATIVE_PC_MENU_MAX 5
enum NativePcMenuKind
{
    NATIVE_PC_MENU_NONE,
    NATIVE_PC_MENU_STORAGE,      /* the Pokémon Storage System's (pokemon_storage_system.c) */
    NATIVE_PC_MENU_PLAYER,       /* the player's PC: ITEM STORAGE, MAILBOX... (player_pc.c) */
    NATIVE_PC_MENU_ITEMS,        /* its ITEM STORAGE: WITHDRAW ITEM... */
    NATIVE_PC_MENU_PLAYER_LINE,  /* one of its lines (the menu comes back after it) */
};
enum NativePcMenuState
{
    NATIVE_PC_MENU_OPENING,      /* the menu is put up (or the field fades in) */
    NATIVE_PC_MENU_INPUT,        /* it reads buttons */
    NATIVE_PC_MENU_REFUSED,      /* a choice was refused: message waits for a button */
    NATIVE_PC_MENU_ENTERING,     /* a choice was taken: the screen fades to it */
};
struct NativePcMenu
{
    u8 menu;                     /* enum NativePcMenuKind */
    u8 state;                    /* enum NativePcMenuState */
    bool8 ready;
    u8 count;
    u8 cursor;
    const u8 *labels[NATIVE_PC_MENU_MAX];
    const u8 *message;           /* the line under it: the option's description, why it was refused
                                  * (NULL: the field's message box has its own line) */
};
bool32 Storage_NativeUiMainMenu(struct NativePcMenu *out);
bool32 PlayerPC_NativeUiState(struct NativePcMenu *out);
/* the player's PC's ITEM STORAGE lists: WITHDRAW ITEM and TOSS ITEM (player_pc.c) */
enum NativePcItemsPhase
{
    NATIVE_PC_ITEMS_NONE,
    NATIVE_PC_ITEMS_OPENING,     /* the list is put up */
    NATIVE_PC_ITEMS_LIST,        /* the list reads buttons */
    NATIVE_PC_ITEMS_SWAP,        /* SELECT: an item being moved */
    NATIVE_PC_ITEMS_QUANTITY,    /* how many */
    NATIVE_PC_ITEMS_CONFIRM,     /* the toss's YES / NO */
    NATIVE_PC_ITEMS_MESSAGE,     /* a line waits for A or B (withdrew, threw away, no room, too important) */
};
#define NATIVE_PC_ITEMS_MAX 50   /* PC_ITEMS_COUNT */
struct NativePcItems
{
    u8 phase;                    /* enum NativePcItemsPhase */
    bool8 toss;                  /* TOSS ITEM (else WITHDRAW ITEM) */
    bool8 ready;
    u8 count;                    /* the items; the list's last row (count) is CANCEL */
    u8 cursor;
    u8 swapFrom;                 /* SWAP: the row being moved (0xFF: none) */
    u8 yesNoCursor;              /* CONFIRM */
    u8 messageWindow;            /* the window with the description or the line */
    u16 quantity;                /* QUANTITY: how many */
    u16 quantityMax;             /* how many of the item the PC has */
    u16 inBag;                   /* how many of it the bag has */
    const u8 *title;             /* the engine's WITHDRAW ITEM / TOSS ITEM */
    u16 items[NATIVE_PC_ITEMS_MAX];
    u16 quantities[NATIVE_PC_ITEMS_MAX];
};
bool32 PlayerPC_NativeUiItems(struct NativePcItems *out);
/* the sheets of the lists' own sprites (the item's icon, the scroll arrows, the move line) */
void PlayerPC_NativeUiItemTiles(u32 mask[32]);
/* the player's PC's MAILBOX (player_pc.c): the PC's Mail as its list shows it, what to do with one, the YES / NO
 * before its words are lost and the lines in between */
enum NativeMailboxPhase
{
    NATIVE_MAILBOX_NONE,
    NATIVE_MAILBOX_OPENING,      /* the list is put up (again), or the chosen Mail's line is about to print */
    NATIVE_MAILBOX_LIST,         /* the list reads buttons */
    NATIVE_MAILBOX_MESSAGE,      /* a line: what to do with it, its words will be lost, moved to the Bag, the Bag full */
    NATIVE_MAILBOX_OPTIONS,      /* READ, MOVE TO BAG, GIVE, CANCEL */
    NATIVE_MAILBOX_CONFIRM,      /* the YES / NO */
};
#define NATIVE_MAILBOX_MAX 10    /* MAIL_COUNT - PARTY_SIZE */
#define NATIVE_MAILBOX_OPTIONS_MAX 4
struct NativeMailbox
{
    u8 phase;                    /* enum NativeMailboxPhase */
    bool8 ready;
    u8 count;                    /* the Mail; the list's last row (count) is CANCEL */
    u8 cursor;
    u8 menuCursor;               /* OPTIONS, CONFIRM */
    u8 optionCount;
    const u8 *options[NATIVE_MAILBOX_OPTIONS_MAX];
    u8 names[NATIVE_MAILBOX_MAX][12];
    u16 items[NATIVE_MAILBOX_MAX];
    u16 words[NATIVE_MAILBOX_MAX][9];
};
bool32 PlayerPC_NativeUiMailbox(struct NativeMailbox *out);
/* script_menu.c: the PC's own multichoice (MULTI_PC) as the engine prints it;
 * {PLAYER}'s PC is expanded into playersPc (32 bytes); returns how many rows */
u32 ScriptMenu_NativeUiPcRows(const u8 **labels, u32 max, u8 *playersPc);

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

/* ---------------------------------------------------------------- the starter choice (starter_choose.c) */

#define NATIVE_STARTER_MAX 18
enum
{
    NATIVE_STARTER_MOVING,    /* between two balls or pages (the label is redrawn) */
    NATIVE_STARTER_BROWSE,    /* Left / Right: the ball, Up / Down, L / R: the page, A: this one */
    NATIVE_STARTER_OPENING,   /* the Pokémon comes out of its ball */
    NATIVE_STARTER_CONFIRM,   /* "Do you choose this POKéMON?" YES / NO */
};
struct NativeStarters
{
    u8 phase;
    bool8 ready;
    u8 selection;             /* page * pageSize + ball */
    u8 pageSize;              /* balls a page (Grass, Fire, Water) */
    u8 pageCount;
    u8 yesNoCursor;           /* 0: YES */
    u16 species[NATIVE_STARTER_MAX];
};
bool32 StarterChoose_NativeUiState(struct NativeStarters *out);

/* ---------------------------------------------------------------- evolution (evolution_scene.c) */

enum
{
    NATIVE_EVO_LOADING,      /* the screen before fades to black (BeginEvolutionScene) */
    NATIVE_EVO_INTRO,        /* "What? X is evolving!": the Pokémon as it is */
    NATIVE_EVO_CHARGING,     /* the music starts, sparkles spiral up and arc down */
    NATIVE_EVO_CYCLING,      /* the old form and the new one alternate (B held stops it, when allowed) */
    NATIVE_EVO_FLASH,        /* the new form, white: sparkles circle in, spray, and its colours return */
    NATIVE_EVO_EVOLVED,      /* the new form: "Congratulations!", then any new move (messages, YES / NO) */
    NATIVE_EVO_STOPPED,      /* the old form: "Huh? X stopped evolving!" */
    NATIVE_EVO_ENDING,       /* fading out to what comes next */
};
struct NativeEvolution
{
    u8 phase;
    bool8 ready;             /* the engine reads buttons now: a waiting message, YES / NO, stopping */
    bool8 canStop;           /* B held stops it (not for an evolution stone, say) */
    bool8 cycleActive;       /* the alternation runs (stopping is possible now) */
    bool8 showNew;           /* the new form is the one in front (the alternation: the larger one) */
    bool8 monAnim;           /* the Pokémon in front plays its animation and cry now */
    bool8 msgValid;          /* window 0 holds this scene's message (not one from before) */
    bool8 learning;          /* a new move's messages have begun (wants to learn, forget one, learned) */
    bool8 yesNo;             /* the engine's YES / NO is up */
    u8 yesNoCursor;          /* 0: YES */
    u8 white;                /* how white the Pokémon in front is (0..16: the flash, the stop) */
    bool8 trade;             /* an evolution inside a trade (trade.c): no stopping, the trade's window */
    bool8 shiny;
    u8 partyId;
    u16 preSpecies;
    u16 postSpecies;
    u32 personality;
};
bool32 Evolution_NativeUiState(struct NativeEvolution *out);

/* ---------------------------------------------------------------- script menus (script_menu.c), coins (coins.c) */

#define NATIVE_SCRIPT_MENU_MAX 12
enum
{
    NATIVE_SCRIPT_MENU_NONE,
    NATIVE_SCRIPT_MENU_MULTICHOICE,  /* a script's multichoice (MULTI_*) */
    NATIVE_SCRIPT_MENU_YES_NO,       /* a script's YES / NO (yesnobox, MSGBOX_YESNO) */
};
struct NativeScriptMenu
{
    u8 kind;
    bool8 ready;                     /* it reads buttons now */
    u8 multichoiceId;
    u8 count;
    u8 cursor;                       /* YES / NO: 0 YES */
    bool8 ignoreB;
    const u8 *labels[NATIVE_SCRIPT_MENU_MAX]; /* the rows as the engine prints them */
};
bool32 ScriptMenu_NativeUiState(struct NativeScriptMenu *out);
/* the rows of any multichoice list as the engine prints them; returns how many */
u32 ScriptMenu_NativeUiList(u32 multichoiceId, const u8 **labels, u32 max);
/* the picture box a script shows (showmonpic) until hidemonpic closes it: the Pokémon, its window (the frame's inside,
 * 8 x 8 tiles from left, top) and the first tile of the picture's sprite (0xFFFF: none) */
struct NativeScriptPic
{
    u16 species;
    u8 windowId;
    u8 left, top;
    u16 tileNum;
};
bool32 ScriptMenu_NativeUiPokemonPic(struct NativeScriptPic *out);
/* the Lilycove elevator's "Now on" window beside its floors' menu (field_specials.c): its window and its two lines */
bool32 FieldSpecials_NativeUiElevatorFloor(u8 *windowId, const u8 **nowOn, const u8 **floor);
/* a Mail's letter (mail.c) while its screen is up: its stationery as the screen built it (BG1's tiles at char base 0,
 * its 32 x 32 tilemap, the background palettes before the fade), each line of words and the signature where
 * PrintMailText puts them (GBA pixels, the text's top left) in the Mail's ink, a Bead or Dream Mail's Pokémon icon */
#define NATIVE_MAIL_LINES 8
struct NativeMail
{
    bool8 ready;                     /* faded in: A or B closes it */
    u8 mailType;                     /* ITEM_TO_MAIL */
    u16 itemId;
    const u16 *tilemap;
    const u8 *tiles;
    const u16 *palettes;             /* 256 colours, BGR555 */
    u16 textColor, textShadow;
    u8 lineCount;
    u8 lines[NATIVE_MAIL_LINES][64];
    s16 lineX[NATIVE_MAIL_LINES], lineY[NATIVE_MAIL_LINES];
    u8 signature[32];                /* empty: a Mail with no words (CHECK in the Bag) */
    s16 signatureX, signatureY;
    u16 iconSpecies;                 /* SPECIES_NONE: no icon */
    s16 iconX, iconY;                /* the icon's centre */
};
bool32 Mail_NativeUiState(struct NativeMail *out);
/* the Hall of Fame after the Champion (hall_of_fame.c): the team stepping up a Pokémon at a time (each where its picture
 * is on the GBA's screen, its sprite's centre), its line, the welcome and the confetti, the player with their record */
enum NativeHofPhase
{
    NATIVE_HOF_NONE,
    NATIVE_HOF_OPENING,          /* fading in; saving the team */
    NATIVE_HOF_MON,              /* a Pokémon coming up, then its line */
    NATIVE_HOF_WELCOME,          /* "Welcome to the HALL OF FAME!" and the confetti */
    NATIVE_HOF_PLAYER,           /* the player stepping up, then their record and LEAGUE CHAMPION (A goes on) */
    NATIVE_HOF_LEAVING,
    NATIVE_HOF_PC_TEAM,          /* the Hall of Fame PC: a team, UP and DOWN pick one, A the one before, B leaves */
    NATIVE_HOF_PC_CORRUPTED,     /* its records cannot be read (A leaves) */
};
struct NativeHofMon
{
    u16 species;                 /* SPECIES_NONE: no Pokémon in that place */
    u32 personality;
    bool8 shiny;
    bool8 shown;                 /* its picture is up */
    bool8 dim;                   /* tinted while another is shown, as the game does */
    s16 x, y;                    /* its picture's centre on the GBA's screen */
    u16 affine;                  /* its OAM matrix's scale (256: none) */
};
struct NativeHof
{
    u8 phase;
    bool8 ready;                 /* PLAYER: A ends it */
    bool8 saving;
    u8 current;                  /* MON: the Pokémon coming up */
    bool8 lineShown;             /* MON: its line is printed */
    u16 frames;                  /* WELCOME: the confetti's frames left (they fall for the last 110) */
    struct NativeHofMon mons[6];
    u8 number[12], nickname[16], species[24], level[12], id[16];  /* MON: the line, as HallOfFame_PrintMonInfo puts it */
    bool8 playerShown, playerRecord;
    u16 playerPic;               /* the player's front picture (a trainer picture id) */
    s16 playerX, playerY;
    u8 time[8];                  /* the record's play time, as the game prints it */
    bool8 pc;                    /* the Hall of Fame PC's records (else the induction) */
    u16 pageNo;                  /* PC: the team's HALL OF FAME No. */
    bool8 moreTeams;             /* PC: A shows the team before (else A leaves) */
    u8 teamCount;                /* PC: the Pokémon of the team shown */
};
bool32 HallOfFame_NativeUiState(struct NativeHof *out);
/* the credits (credits.c): the page Task_UpdatePage printed over the game's ride (each line's text, whether it is a
 * title, its top on the GBA's screen) and how far its fade has come (0 gone .. 16 whole) */
#define NATIVE_CREDITS_LINES 5
struct NativeCredits
{
    u8 count;
    u8 shown;
    const u8 *lines[NATIVE_CREDITS_LINES];
    bool8 titles[NATIVE_CREDITS_LINES];
    u8 y[NATIVE_CREDITS_LINES];
};
bool32 Credits_NativeUiState(struct NativeCredits *out);
/* the trade's sequence (trade.c: an in-game trade's, a link partner's): whether its message window (window 0) shows
 * its last line now (the line is the window's printer's), and whether an in-game trade's end waits for A */
struct NativeTradeSequence
{
    bool8 message;
    bool8 waitingA;
};
bool32 Trade_NativeUiSequence(struct NativeTradeSequence *out);
/* Easy Chat (easy_chat.c) while its screen is up: the words being edited and every part of the screen as the engine
 * has it. inputState is the screen's own (0 a word, 1 the footer, 2 the keyboard, 3 a group's words, 4 quit?,
 * 5 delete all?, 6 confirm?, 7 a quiz's question, 8 a message, 9 and 10 the Bard's lyrics) */
#define NATIVE_EC_PHRASE_MAX 9
#define NATIVE_EC_GROUPS 22
#define NATIVE_EC_SHOWN_WORDS 8
struct NativeEasyChat
{
    bool8 ready;                     /* the engine reads buttons now (no window moving, no fade) */
    u8 inputState;
    u8 type;                         /* EASY_CHAT_TYPE_* */
    u8 frameId;
    u8 numColumns, numRows;
    s8 cursorColumn, cursorRow;      /* the main cursor; cursorRow == numRows: the footer */
    u8 footerCount;
    const u8 *footer[4];             /* DEL. ALL, CANCEL, OK and a quiz's QUIZ or ANSWER */
    u8 wordCount;
    u16 words[NATIVE_EC_PHRASE_MAX]; /* EC_EMPTY_WORD: a blank */
    const u8 *title;
    const u8 *message[2];
    bool8 yesNo;                     /* a question's YES / NO is up */
    u8 yesNoCursor;
    /* the lower window: the keyboard (the unlocked groups, or A-Z) or a group's words */
    bool8 alphabet;
    s8 keyboardColumn, keyboardRow;  /* column -1: the buttons (0 the mode, 1 DEL., 2 CANCEL); the row on screen */
    u8 keyboardScroll;               /* the group names' first row shown */
    u8 groupCount;
    u8 groups[NATIVE_EC_GROUPS];
    const u8 *groupNames[NATIVE_EC_GROUPS];
    u16 groupWordCount;
    u8 wordScroll, wordLastRow;      /* a group's words: the first row shown, the last row */
    s8 wordColumn, wordRow;          /* the cursor, its row on screen */
    u16 shownWords[NATIVE_EC_SHOWN_WORDS]; /* the four rows shown, two a row (EC_EMPTY_WORD: none) */
};
bool32 EasyChat_NativeUiState(struct NativeEasyChat *out);
/* the coin box (showcoinsbox) is up */
bool32 Coins_NativeUiBoxShown(void);
/* the coin box's window while it is up (WINDOW_NONE: not up) */
u8 Coins_NativeUiBoxWindow(void);
/* money.c: the money box's window (showmoneybox) while it is up (WINDOW_NONE: not up), and its label sprite's tiles */
u8 Money_NativeUiBoxWindow(void);
bool32 Money_NativeUiLabelTiles(u16 *start, u16 *count);
/* script.c: the native function the field's script runs now (NULL: none) */
u8 (*ScriptContext_NativeUiWaitFunc(void))(void);
/* script.c: the script the field started last (ScriptContext_SetupScript; NULL: none yet) */
const u8 *ScriptContext_NativeUiStartedScript(void);
/* scrcmd.c: the field's script waits for A or B (a finished msgbox) */
bool32 Script_NativeUiWaitsForButton(void);
/* event_object_movement.c: a field sprite palette by its tag (NULL: none) */
const u16 *ObjectEvent_NativeUiPalette(u16 tag);

/* ---------------------------------------------------------------- the naming screen (naming_screen.c) */

enum
{
    NATIVE_NAMING_LOADING,
    NATIVE_NAMING_INPUT,     /* reading buttons (or about to: the cursor flies to OK) */
    NATIVE_NAMING_SWAP,      /* the keyboard page is changing */
    NATIVE_NAMING_DONE,      /* OK pressed: saving the name, fading out */
};
/* keyboard pages in the order SELECT cycles them: UPPER, lower, OTHERS */
#define NATIVE_NAMING_PAGES 3
#define NATIVE_NAMING_ROWS 4
#define NATIVE_NAMING_COLS 8
struct NativeNaming
{
    u8 phase;
    bool8 ready;
    u8 kind;                 /* NAMING_SCREEN_* */
    u8 maxChars;
    u8 page;                 /* the page in front (during a swap: the one coming) */
    u8 columns[NATIVE_NAMING_PAGES]; /* keys a row on each page; x == columns: PAGE, BACK, OK */
    s8 cursorX;
    s8 cursorY;
    bool8 cursorShown;
    bool8 genderIcon;        /* the template shows the Pokémon's gender */
    u8 gender;               /* MON_MALE, MON_FEMALE, MON_GENDERLESS */
    u8 playerGender;         /* the player template: whose sprite */
    u16 species;
    u32 personality;
    u8 titleWindow;          /* the title as the engine printed it */
    u8 text[17];             /* the name so far */
    u8 keys[NATIVE_NAMING_PAGES][NATIVE_NAMING_ROWS][NATIVE_NAMING_COLS];
};
bool32 NamingScreen_NativeUiState(struct NativeNaming *out);

/* ---------------------------------------------------------------- the Poké Mart (shop.c) */

#define NATIVE_SHOP_MAX_ITEMS 128   /* the longest coin shop lists (the Game Corner's RARE GOODS, the Mega Stone dealer's) fit whole: tools/mp/gen_rare_goods.py --check */
enum
{
    NATIVE_SHOP_NONE,
    NATIVE_SHOP_MENU,        /* the field's BUY / SELL / QUIT (a decoration shop: BUY / QUIT) */
    NATIVE_SHOP_FIELD,       /* the field around it: "anything else?", or fading to BUY or SELL */
    NATIVE_SHOP_LIST,        /* the buy screen's list */
    NATIVE_SHOP_QUANTITY,    /* how many, and its price */
    NATIVE_SHOP_MESSAGE,     /* a clerk's line on the buy screen (printing, or waiting for A or B) */
    NATIVE_SHOP_CONFIRM,     /* YES / NO to the price */
    NATIVE_SHOP_LEAVING,     /* fading back to the field */
};
struct NativeShop
{
    u8 phase;
    bool8 ready;              /* the engine reads buttons now */
    bool8 decorations;        /* a decoration shop: its list holds decorations */
    bool8 sale;               /* Slateport's POKéNEWS sale halves the prices */
    bool8 coins;              /* the prices and the money are Game Corner coins (the Mega Stone dealer; Update 29) */
    u8 menuCount;             /* the field menu's rows */
    u8 menuCursor;            /* the field menu's row; the YES / NO's (0: YES) */
    u8 msgWindow;             /* the clerk's line (WINDOW_NONE: none) */
    const u8 *menuLabels[3];
    u16 count;                /* items for sale (the list adds CANCEL after them) */
    u16 cursor;               /* the list's row (count: CANCEL) */
    u16 scroll;               /* the first row shown */
    u16 chosen;               /* the item being bought (ITEM_NONE: none) */
    u16 quantity;
    u16 maxQuantity;
    u16 inBag;                /* of the chosen item, else of the row's (a decoration: how many the PC has) */
    u32 totalCost;
    u32 money;
    u16 items[NATIVE_SHOP_MAX_ITEMS];
    u32 prices[NATIVE_SHOP_MAX_ITEMS];
    bool8 soldOut[NATIVE_SHOP_MAX_ITEMS];  /* a key item already owned */
};
bool32 Shop_NativeUiState(struct NativeShop *out);

/* decoration.c: a decoration's picture as the game's shops and menus show it */
enum
{
    NATIVE_DECOR_PIC_NONE,
    NATIVE_DECOR_PIC_ICON,      /* its icon: 24x24 4bpp, compressed with its header */
    NATIVE_DECOR_PIC_OBJECT,    /* a doll or a cushion: its object event's first frame */
    NATIVE_DECOR_PIC_TILES,     /* composed from the secret base's metatiles: width x height, OBJ order */
};
struct NativeDecorPicture
{
    u8 kind;
    const u32 *iconPic;
    const u16 *iconPal;
    u16 graphicsId;
    u8 width;
    u8 height;
    u8 image[0x800];
    u16 palette[16];
};
bool32 Decoration_NativeUiPicture(u16 decor, struct NativeDecorPicture *out);
/* the decoration PC's menus (decoration.c: the player's room's PC, a secret base's PC, the Fortree trader): what to
 * do, the categories with what is owned of each, a category's decorations with those in use, the toss's YES / NO and
 * the lines in between. Placing and putting away on the field stay the game's. */
enum NativeDecorMenuPhase
{
    NATIVE_DECOR_MENU_NONE,
    NATIVE_DECOR_MENU_OPENING,   /* a list is put up */
    NATIVE_DECOR_MENU_ACTIONS,   /* DECORATE, PUT AWAY, TOSS, CANCEL */
    NATIVE_DECOR_MENU_CATEGORIES,
    NATIVE_DECOR_MENU_ITEMS,
    NATIVE_DECOR_MENU_MESSAGE,   /* a line (none owned, none in use, in use, thrown away, will be thrown away) */
    NATIVE_DECOR_MENU_CONFIRM,   /* the toss's YES / NO */
};
#define NATIVE_DECOR_CATEGORIES 8
#define NATIVE_DECOR_ITEMS_MAX 40
struct NativeDecorMenu
{
    u8 phase;                    /* enum NativeDecorMenuPhase */
    bool8 ready;
    bool8 playerRoom;            /* the player's room (else a secret base) */
    bool8 trade;                 /* the Fortree trader's choice */
    u8 cursor;                   /* the list up now: actions, categories (NATIVE_DECOR_CATEGORIES: CANCEL), items (count: CANCEL) */
    const u8 *actions[4];
    const u8 *actionDescription; /* the cursor's action */
    const u8 *categoryNames[NATIVE_DECOR_CATEGORIES];
    u8 owned[NATIVE_DECOR_CATEGORIES], capacity[NATIVE_DECOR_CATEGORIES];
    bool8 categoryDisabled[NATIVE_DECOR_CATEGORIES]; /* the player's room takes only dolls and cushions */
    const u8 *categoryCancel;    /* CANCEL, or the trader's EXIT */
    u8 category;                 /* ITEMS: the category shown */
    u8 count;
    u16 items[NATIVE_DECOR_ITEMS_MAX];
    u8 inUse[NATIVE_DECOR_ITEMS_MAX]; /* 0 no, 1 in the secret base, 2 in the player's room */
    u8 yesNoCursor;
};
bool32 Decoration_NativeUiMenu(struct NativeDecorMenu *out);

/* ---------------------------------------------------------------- the Trainer Card (trainer_card.c) */

/* The card as the engine shows it: its face, what A and B do now, and the facts
 * it prints. The own card's play time is the save's live clock (as the engine
 * prints it); a card from a link is its owner's snapshot. */
enum
{
    NATIVE_CARD_LOADING,
    NATIVE_CARD_FRONT,      /* A flips, B closes */
    NATIVE_CARD_BACK,       /* A closes, B flips back (closes while linked) */
    NATIVE_CARD_TO_BACK,    /* the engine's flip */
    NATIVE_CARD_TO_FRONT,
    NATIVE_CARD_CLOSING,
};
struct NativeTrainerCard
{
    u8 phase;
    bool8 ready;          /* reading A and B */
    bool8 onBack;
    bool8 isLink;         /* another player's card in a link room */
    bool8 linked;         /* a link is up: B on the back closes */
    bool8 isHoenn;
    u8 cardType;
    bool8 hasPokedex;
    bool8 hasHofResult;
    bool8 hasLinkResults;
    bool8 hasTrades;
    bool8 colonHidden;    /* the play time's blinking colon */
    /* the engine's four star rules (the own card only; a link card has its count) */
    bool8 caughtAllRegional;
    bool8 hasAllPaintings;
    bool8 hasAllFrontierSymbols;
    u8 gender;
    u8 stars;
    u16 picId;            /* the trainer picture the engine shows */
    u16 caught;
    u16 seen;             /* the own card only (the engine prints caught) */
    u16 trainerId;
    u16 playHours, playMinutes;
    u32 money;
    u16 hofHours, hofMinutes, hofSeconds;
    u16 linkWins, linkLosses;
    u16 trades;
    u16 pokeblocksWithFriends;
    u16 contestsWithFriends;
    u16 frontierBP;
    u8 name[8];
    u8 profile[4][13];    /* the four easy chat words */
    bool8 badges[8];
    u8 badgeTiles[8 * 128]; /* the badges' own art: a 128x16 strip, 16x16 each */
    const u16 *badgePalette;
};
bool32 TrainerCard_NativeUiState(struct NativeTrainerCard *out);

/* ---------------------------------------------------------------- the Players menu (multiplayer_menu.c) */

/* The host-driven Players menu as the game holds it: the host's title, status
 * and up to six rows (printable ASCII, NUL-padded); rows without an action are
 * information. waiting: a request is out (the game shows "Waiting for server"). */
struct NativeMpMenu
{
    bool8 waiting;
    bool8 tradeMenu;       /* reopened by the host for a trade (B cannot close it) */
    bool8 admissionHeld;   /* a battle entry was chosen: the menu waits for the battle */
    bool8 back;            /* B chooses the host's Back (else B closes the menu) */
    u8 cursor;
    u8 count;
    u32 revision;          /* the host's screen revision (0: none received yet) */
    u8 title[25];
    u8 status[33];
    u8 rows[6][27];
    bool8 enabled[6];
    bool8 admission[6];    /* the battle-entry row */
    u8 towerStyle;         /* Battle Tower: Legends: the page's Tower screen (tower_legends.h TOWER_STYLE_*), 0: none */
    u8 towerArg;           /* its argument (a floor) */
};
bool32 MultiplayerMenu_NativeUiState(struct NativeMpMenu *out);
/* multiplayer_world.c: the notice banner up now (host notices, wallet, hints): its
 * window (its text through TextPrinter_NativeUiWindowText), frames shown, frames in all */
bool32 MultiplayerWorld_NativeUiNotice(u8 *windowId, u16 *shownFrames, u16 *totalFrames);

/* ---------------------------------------------------------------- the STYLE editor (multiplayer_appearance.c) */

/* The editor's rows (its cursor): four colour rows, then SAVE and CANCEL. */
enum NativeAppearanceRow
{
    NATIVE_LOOK_SKIN,
    NATIVE_LOOK_HAIR,
    NATIVE_LOOK_OUTFIT,
    NATIVE_LOOK_HAT,     /* the hat (or bandana) and the bag */
    NATIVE_LOOK_SAVE,
    NATIVE_LOOK_CANCEL,
    NATIVE_LOOK_CHARACTER, /* the character selection's first row (claude/character-customization) */
    NATIVE_LOOK_ROW_COUNT,
};
#define NATIVE_LOOK_BYTES 8
struct NativeAppearance
{
    u8 look[NATIVE_LOOK_BYTES];    /* the look being edited (the field sprite shows it live) */
    u8 start[NATIVE_LOOK_BYTES];   /* the player's look the editor started from */
    u8 cursor;                     /* enum NativeAppearanceRow */
    bool8 isMay;
    bool8 pendingSave;             /* a saved look not yet confirmed by the host */
    bool8 connected;               /* the multiplayer host is there */
    bool8 hostHasLook;             /* the host keeps the player's current look */
    /* the character selection, when this build has it (claude/character-customization):
     * its editor puts a CHARACTER row first; the colour rows recolour the trainer only */
    bool8 characters;
    u8 character;                  /* the character shown (0: the trainer, with the colour rows) */
    u8 startCharacter;             /* the one the editor started from */
    u8 characterCount;             /* the roster, the trainer included */
    bool8 coloursEnabled;          /* the colour rows apply (the trainer) */
};
bool32 MultiplayerAppearance_NativeUiState(struct NativeAppearance *out);
/* a character of the roster (FALSE: none; 0 is the trainer) */
struct NativeCharacter
{
    const u8 *name;
    const u8 *variant;             /* the game or the artist */
    u8 gender;
    u8 kind;                       /* 0 a preset, 1 kit 1, 2 a class look */
    u16 frontPic;                  /* TRAINER_PIC_* */
};
bool32 MultiplayerAppearance_NativeUiCharacter(u32 character, struct NativeCharacter *out);
/* picks a character in the open editor as its LEFT / RIGHT on CHARACTER would (FALSE: no selection here) */
bool32 MultiplayerAppearance_NativeUiSelectCharacter(u32 character);
/* one field frame of a character (0: the trainer recoloured by look; NULL look: its own colours) */
enum
{
    NATIVE_GAIT_STAND,
    NATIVE_GAIT_WALK,
    NATIVE_GAIT_RUN,
};
struct NativeFieldFrame
{
    const void *tiles;             /* 4bpp, OAM tile order */
    u8 width;
    u8 height;
    bool8 flip;
    u16 palette[16];
};
/* dir: 0 down, 1 left, 2 up, 3 right; step: 0-3 of a walk or run */
bool32 MultiplayerAppearance_NativeUiFieldFrame(u32 character, bool32 isMay, const u8 *look, u32 dir, u32 gait, u32 step,
                                                struct NativeFieldFrame *out);
/* the player's own field frame as the others see it (the saved character or look) */
bool32 MultiplayerAppearance_NativeUiPlayerFrame(u32 dir, u32 gait, u32 step, struct NativeFieldFrame *out);
/* the local player's character (the editor's while it is open, else the saved one; 0: none) */
u32 MultiplayerAppearance_NativeUiLocalCharacter(void);
/* the player's front picture (TRAINER_PIC_*: the character's, or Brendan's / May's) */
u16 MultiplayerAppearance_NativeUiFrontPic(u32 character, bool32 isMay);
/* the player's own Trainer Card picture when their character has a bigger one than
 * the front picture (4bpp tiles, w x h, and its palette; FALSE: none) */
bool32 MultiplayerAppearance_NativeUiCardPic(const void **tiles, const u16 **palette, u32 *w, u32 *h);
/* a colour row's choices; DEFAULT (0) keeps the body's own colours */
u32 MultiplayerAppearance_NativeUiOptionCount(u32 row);
const u8 *MultiplayerAppearance_NativeUiOptionName(u32 row, u32 index);
/* the look byte a row edits (1 skin, 2 hair, 3 outfit, 5 hat and bag) */
u32 MultiplayerAppearance_NativeUiLookByte(u32 row);
/* the body's palette slots a row recolours (up to 4); returns how many */
u32 MultiplayerAppearance_NativeUiSlots(u32 row, bool32 isMay, u8 *slots);
/* a choice's colours, light to dark (up to 4; DEFAULT: the body's own); returns how many */
u32 MultiplayerAppearance_NativeUiSwatch(u32 row, u32 index, bool32 isMay, u16 *colors);

/* ---------------------------------------------------------------- game art */

/* pokemon_icon.c: a party / box icon's 4bpp tiles (two 32x32 frames) and palette */
void PokemonIcon_NativeUiArt(u16 species, u32 personality, bool32 isEgg, const u8 **tiles, const u16 **palette);

/* ---------------------------------------------------------------- battle (battle_interface.c, battle_controller_player.c) */

/* One battler's healthbox as the game shows it (its values noted as the game
 * draws them, so a bar that is still moving shows where it is). */
struct NativeBattleBox
{
    bool8 exists;       /* its healthbox sprites exist */
    bool8 visible;      /* shown (the game hides them for a move's animation) */
    s16 x2, y2;         /* the healthbox's offset from its place: sliding, bouncing */
    bool8 hpShown;      /* its HP bar was drawn */
    s32 hp;             /* the HP the bar (and the player's HP text) shows */
    s32 hpNum, hpDen;   /* the bar's fill: hpNum / hpDen */
    bool8 expShown;
    s32 expNum, expDen; /* the EXP bar's fill within the level */
    u8 level;           /* the level it shows (0: not drawn yet) */
    bool8 statusShown;
    u32 status;         /* the status it shows (STATUS1_*) */
};
bool32 Battle_NativeUiBox(u32 battler, struct NativeBattleBox *out);
/* The OBJ tiles of the game's battle interface sprites (healthboxes, bars, the
 * party trays, the gimmick trigger and indicators, the last used Ball window,
 * the move info window, the category icon): bit n of mask[n / 32]. */
void Battle_NativeUiHudTiles(u32 mask[32]);
/* the party tray's 8x8 ball tile (4bpp; gBattleInterface_BallDisplayPal):
 * 0 full, 1 empty, 2 fainted, 3 statused, 4 caught */
const u8 *Battle_NativeUiBallTile(u32 kind);

enum NativeBattleInput
{
    NATIVE_BATTLE_INPUT_NONE,
    NATIVE_BATTLE_INPUT_ACTION_WAIT,    /* the action menu is being put up */
    NATIVE_BATTLE_INPUT_ACTION,         /* FIGHT / BAG / POKéMON / RUN */
    NATIVE_BATTLE_INPUT_MOVE_WAIT,
    NATIVE_BATTLE_INPUT_MOVE,           /* the four moves (START: the gimmick, L: the description) */
    NATIVE_BATTLE_INPUT_MOVE_SWITCH,    /* SELECT: reordering the moves */
    NATIVE_BATTLE_INPUT_TARGET,         /* one target (doubles) */
    NATIVE_BATTLE_INPUT_TARGETS_SHOWN,  /* a spread move's targets: A confirms */
    NATIVE_BATTLE_INPUT_YES_NO,         /* the controller's YES / NO (switch after a faint) */
    NATIVE_BATTLE_INPUT_PARTY,          /* the party menu opens */
    NATIVE_BATTLE_INPUT_BAG,            /* the bag opens */
    NATIVE_BATTLE_INPUT_PREVIEW_HANDED = 254, /* breadcrumbs only: the move preview's goal handed to the engine's menu */
    NATIVE_BATTLE_INPUT_SENT_SHOWN = 255, /* breadcrumbs only (ui_battle.c): a link battle's "choice sent" note is up */
};
/* battle_script_commands.c: the level-up box (Cmd_drawlvlupbox) */
struct NativeLevelUpBox
{
    bool8 shown;          /* the box itself is up (else the banner of a Pokémon not in battle) */
    u8 page;              /* 1 the gains, 2 the totals */
    bool8 waiting;        /* it waits for a button */
    u8 partyId;
    u8 level;
    u16 before[NUM_STATS]; /* STAT_* order */
    u16 after[NUM_STATS];
};
bool32 Battle_NativeUiLevelUpBox(struct NativeLevelUpBox *out);
/* the prize money a won trainer battle pays (0 outside trainer battles) */
u32 Battle_NativeUiPrizeMoney(void);
/* battle_main.c: the battle's intro runs (money and Exp. as before the battle) */
bool32 Battle_NativeUiInIntro(void);
/* battle_message.c: the name of the trainer a battler belongs to, as the battle's
 * messages name them; NULL for a wild Pokémon (text: 32 bytes at least) */
const u8 *Battle_NativeUiTrainerName(u32 battler, u8 *text);
/* battle_controller_player.c: what the player controller waits for from this battler */
u32 Battle_NativeUiPlayerInput(u32 battler);
/* battle_controller_safari.c: the same for the Safari Zone's controller (its action
 * menu BALL, POKéBLOCK, GO NEAR, RUN on the same 2x2; the Pokéblock case as BAG) */
u32 Battle_NativeUiSafariInput(u32 battler);
/* battle_controller_wally.c: what Wally's catching tutorial shows of its own
 * choices (it makes them itself: nothing waits for the player's buttons);
 * actionCursor: where its action cursor is (it goes to BAG before the Ball) */
u32 Battle_NativeUiWallyInput(u32 battler, u8 *actionCursor);
/* sprite.c: the OBJ tiles a sheet tag holds */
bool32 Sprite_NativeUiTagTiles(u16 tag, u16 *start, u16 *count);

/* ---------------------------------------------------------------- text printers (text.c) */

/* The last string printed on a window: the engine's pointer to it (to tell
 * which string it was; its buffer may have changed since), how far the printer
 * is, whether it is still printing. FALSE when nothing was printed there. */
bool32 TextPrinter_NativeUiWindow(u8 windowId, const u8 **start, const u8 **current, bool8 *active);
/* ... a copy of that string as it was printed (up to UI_MESSAGE_COPY - 1 bytes) and how many
 * of its bytes are on screen so far */
bool32 TextPrinter_NativeUiWindowText(u8 windowId, u8 *dst, u32 size, u32 *printed, bool8 *active);
/* The copy's size, and every screen's buffer for a message it shows (KNOWN_ISSUES MS-167): the game's message buffer,
 * gStringVar4, holds 1,000 bytes. At 256 a long dialogue (451 of the game's texts are longer) stopped mid-word on its
 * last page and showed no more pages after it: Devon's rock display "From that humble start" twice. */
#define UI_MESSAGE_COPY 1024
/* the window's printer waits for a button (a page break or a pause) */
bool32 TextPrinter_NativeUiWindowWaiting(u8 windowId);
/* window.c: a window removed forgets its last string (WINDOWS_MAX: every
 * window, as InitWindows sets them up anew) */
void TextPrinter_NativeUiForgetWindow(u32 windowId);

/* window.c: the window's tiles are on its background now (shown, not cleared) */
bool32 Window_NativeUiShown(u8 windowId);
/* ...and its tiles are on the screen itself (VRAM), not only in the background's buffer */
bool32 Window_NativeUiOnScreen(u8 windowId);

/* match_call.c: a PokeNav call on the field: its text window, its box's vertical offset (BG0's: below its place while it
 * slides in and out, 0 in place), whether its lines have begun, whether it is on its last step (A or B hangs up once
 * the line is printed) */
bool8 MatchCall_NativeUiState(u8 *windowId, s16 *offsetY, bool8 *talking, bool8 *waiting);

/* The PokéNav (pokenav*.c) as the game has it now (screens/ui_pokenav.c, step 13 of the owner's plan): the page up,
 * its cursor and what it lists. The engine keeps every rule; the UI draws the pages and presses the game's buttons. */
#define NATIVE_PNAV_ROWS 8              /* a list's rows copied: from the game's top row (it shows 6 to 8) */
#define NATIVE_PNAV_NAME 24
enum NativePokenavPage
{
    NATIVE_PNAV_NONE,
    NATIVE_PNAV_MENU,                   /* the main menu, CONDITION's or its search's (menuType says which) */
    NATIVE_PNAV_MAP,                    /* the region map */
    NATIVE_PNAV_CONDITION,              /* a Pokemon's condition (the party's, or a search's result) */
    NATIVE_PNAV_SEARCH,                 /* a condition search's results */
    NATIVE_PNAV_MATCH_CALL,             /* the trainers registered: the list, its menu, a check page, a call */
    NATIVE_PNAV_RIBBON_LIST,            /* the Pokemon with ribbons */
    NATIVE_PNAV_RIBBONS,                /* a Pokemon's ribbons */
};
struct NativePokenavRow
{
    u8 name[NATIVE_PNAV_NAME];          /* a Pokemon's nickname or a trainer's name */
    u8 sub[NATIVE_PNAV_NAME];           /* a trainer's place, as the list prints it */
    u16 species;                        /* the Pokemon's (0: a trainer) */
    u32 personality;
    u8 level;
    u8 gender;                          /* MON_MALE, MON_FEMALE, MON_GENDERLESS */
    u16 value;                          /* a search's: the condition searched for; a ribbon list's: the ribbon count */
    bool8 egg;
    bool8 shiny;
    bool8 rematch;                      /* a trainer who wants a rematch (the game's Poke Ball icon) */
    u16 mapSec;                         /* a trainer's place */
};
enum NativePokenavCallState
{
    NATIVE_PNAV_MC_LIST,                /* the trainers' list */
    NATIVE_PNAV_MC_OPTIONS,             /* a trainer's menu: CALL, CHECK, CANCEL */
    NATIVE_PNAV_MC_CHECK,               /* a trainer's check page */
    NATIVE_PNAV_MC_CALL,                /* a call (the trainer's words, or that they are close by) */
    NATIVE_PNAV_MC_EXIT,                /* going back to the main menu */
};
struct NativePokenav
{
    u8 page;                            /* enum NativePokenavPage */
    u8 mode;                            /* POKENAV_MODE_* (the Rustboro tutorial's two) */
    bool8 busy;                         /* a page sliding in or out, or the screen fading: input waits */
    u32 menuId;                         /* POKENAV_MAIN_MENU... */
    /* NATIVE_PNAV_MENU */
    u8 menuType;                        /* POKENAV_MENU_TYPE_* */
    u8 menuCursor;
    u8 menuCount;
    u8 menuItems[6];                    /* POKENAV_MENUITEM_* */
    bool8 menuInput;                    /* the menu reads buttons */
    bool8 menuNoRibbons;                /* "There are no RIBBON winners." is up */
    const u8 *menuDesc;                 /* the cursor's item's description */
    /* lists (NATIVE_PNAV_SEARCH, NATIVE_PNAV_MATCH_CALL, NATIVE_PNAV_RIBBON_LIST) */
    u16 listCount;
    u16 listTop;                        /* the index of the game's top row */
    u16 listCursor;                     /* the cursor's index */
    u8 listShown;                       /* rows the game shows at once */
    u8 rowCount;                        /* rows copied, from listTop */
    struct NativePokenavRow rows[NATIVE_PNAV_ROWS];
    u8 searchId;                        /* a search's condition: 0 cool ... 4 tough */
    /* NATIVE_PNAV_MATCH_CALL */
    u8 mcState;                         /* enum NativePokenavCallState */
    u8 mcOptionCursor;
    u8 mcOptionCount;
    u8 mcOptions[3];                    /* MATCH_CALL_OPTION_* */
    u16 mcPic;                          /* the cursor's trainer's picture (TRAINER_PIC_*; 0xFFFF: none, no check page) */
    const u8 *mcCheck[4];               /* its check page: strategy, Pokemon, two lines about them (NULL: none) */
    u8 mcMessageWindow;                 /* the call's text window (its text printer read by the UI); WINDOW_NONE */
    /* NATIVE_PNAV_CONDITION: the cursor's Pokemon (the party's rows from 0, a search's from listTop) */
    bool8 condSearch;                   /* a search's result (else the party's, its last row CANCEL) */
    bool8 condCancel;                   /* the cursor on CANCEL */
    bool8 condMarking;                  /* the markings menu open */
    s8 condMarkCursor;                  /* its cursor: 0-3 the marks, 4 OK, 5 CANCEL */
    u8 condMarkEdit;                    /* the marks as the menu has them */
    u8 condValues[5];                   /* cool, beauty, cute, smart, tough */
    u8 condSparkles;                    /* the sheen's sparkles, as the game counts them */
    u8 condMarks;                       /* the Pokemon's markings */
    bool8 condShiny;
    u8 condBox;                         /* TOTAL_BOXES_COUNT: the party */
    struct NativePokenavRow cond;       /* the cursor's Pokemon (NATIVE_PNAV_RIBBONS' too) */
    /* NATIVE_PNAV_RIBBONS: the cursor's Pokemon's ribbon case (listCursor of listCount Pokemon with ribbons) */
    u8 ribbonNormal;                    /* its ribbons, packed from the case's first position */
    u8 ribbonGift;                      /* its gift ribbons, on a row of their own */
    u8 ribbonIds[FIRST_GIFT_RIBBON];    /* the normal ribbons in the case's order (CHAMPION_RIBBON ...) */
    u8 giftIds[NUM_GIFT_RIBBONS];       /* the gift ribbons (MARINE_RIBBON ...) */
    u8 ribbonPerRow;                    /* the case's rows: RIBBONS_PER_ROW (the game's cursor moves by them) */
    u8 ribbonGiftStart;                 /* the gift row's first position */
    u8 ribbonPos;                       /* the cursor's position in the case */
    bool8 ribbonExpanded;               /* a ribbon chosen (A): the cursor moves over the case */
    u8 ribbonTotal;                     /* the ribbons it holds, as the game counts them */
    const u8 *ribbonDesc[2];            /* the cursor's ribbon's name and words (NULL: an empty gift slot) */
};
bool8 Pokenav_NativeUiState(struct NativePokenav *out);
/* the parts' own (each in its file) */
void PokenavMenu_NativeUiFill(struct NativePokenav *out);
const u8 *PokenavMenuGfx_NativeUiDescription(u32 menuItem, bool8 *noRibbons);
void PokenavMatchCall_NativeUiFill(struct NativePokenav *out);
/* pokenav_match_call_gfx.c: the call's text window, WINDOW_NONE when the page is not up; *busy: its tasks run */
u8 PokenavMatchCallGfx_NativeUiWindow(bool8 *busy);
void PokenavCondition_NativeUiFill(struct NativePokenav *out);
void PokenavSearch_NativeUiFill(struct NativePokenav *out);
void PokenavRibbonsList_NativeUiFill(struct NativePokenav *out);
void PokenavRibbons_NativeUiFill(struct NativePokenav *out);
/* pokenav_ribbons_summary.c: a ribbon's big icon - the game's compressed sheet of twelve 32x32 icons (four bits a
 * pixel), the icon's index in it and its palette (NULL: no such ribbon) */
const u32 *PokenavRibbons_NativeUiIconArt(u32 ribbonId, u32 *index, const u16 **palette);
/* pokenav.c: a row for a box's or the party's Pokemon (boxId TOTAL_BOXES_COUNT: the party) */
void Pokenav_NativeUiMonRow(struct NativePokenavRow *row, u32 boxId, u32 monId);
/* mon_markings.c: the markings menu open, its cursor (0-3 the marks, 4 OK, 5 CANCEL) and its marks */
bool8 MonMarkings_NativeUiState(s8 *cursor, u8 *markings);
/* pokenav_list.c: the list's window (FALSE with no list) */
bool32 PokenavList_NativeUiWindow(u16 *top, u16 *count, u16 *selected, u8 *shown);

/* battle_transition.c: the battle transition playing (B_TRANSITION_*), B_TRANSITION_COUNT with none */
u8 BattleTransition_NativeUiPlaying(void);

/* battle_transition.c: what a battle transition on the field shows now, for the native UI to draw instead of the
 * GBA's picture of it (screens/ui_transition.c). Everything in the GBA screen's pixels (240 x 160), as the game
 * computed it this frame (the picture shows it the next). */
enum NativeTransitionKind
{
    NATIVE_TRANSITION_NONE,      /* nothing of its own the UI draws (the intro's flash, a wave of the field, ...) */
    NATIVE_TRANSITION_WEAVE,     /* the big Poke Ball weaving in: lineOffset per line, the blend eva / evb */
    NATIVE_TRANSITION_CIRCLE,    /* a pattern in a closing circle (radius); the big Poke Ball's own if pokeball */
    NATIVE_TRANSITION_BLACKHOLE, /* a black circle growing or pulsing (radius) */
    NATIVE_TRANSITION_CLOCKWISE, /* the black sweep from twelve o'clock to the line to wipeEndX, wipeEndY */
    NATIVE_TRANSITION_ANGLED,    /* the angled black wipes: those before wipeId done, wipeId up to its row wipeCurrY */
    NATIVE_TRANSITION_TRAIL,     /* Poke Balls rolling over the field (balls) */
};
#define NATIVE_TRANSITION_BALLS 5
struct NativeTransitionBall
{
    s16 x;          /* centre */
    s16 y;
    s16 matrix[4];  /* its OAM matrix (a, b, c, d; 8.8 fixed point, 256 = 1) */
    u16 tileNum;    /* its OAM tiles (the game's sprite, hidden while the UI draws the ball) */
};
struct NativeTransitionState
{
    u8 id;               /* B_TRANSITION_* */
    u8 kind;             /* enum NativeTransitionKind */
    bool8 done;          /* finished: the screen is black until the battle */
    bool8 pokeball;      /* WEAVE, CIRCLE: the pattern is the big Poke Ball */
    u8 eva;              /* WEAVE, CIRCLE: the pattern's blend over the field (0..16 each) */
    u8 evb;
    s16 radius;          /* CIRCLE, BLACKHOLE */
    s16 wipeEndX;        /* CLOCKWISE */
    s16 wipeEndY;
    bool8 wipeFull;      /* CLOCKWISE: the sweep has gone round */
    u8 wipeId;           /* ANGLED */
    bool8 wipeActive;    /* ANGLED: wipe wipeId is moving (else it is done) */
    s16 wipeCurrY;       /* ANGLED: the row wipe wipeId has reached */
    u8 ballCount;        /* TRAIL */
    struct NativeTransitionBall balls[NATIVE_TRANSITION_BALLS];
    s16 lineOffset[160]; /* WEAVE: BG0's horizontal offset by line */
};
bool8 BattleTransition_NativeUiState(struct NativeTransitionState *state);
/* field_effect.c: the field move cut-in (FLDEFF_FIELD_MOVE_SHOW_MON: a field move's Pokemon on its banner) as the game
 * has it now, for the native UI to draw (the 3D view shows nothing of the GBA's picture of it); GBA screen pixels */
struct NativeFieldMoveState
{
    bool8 outdoors;     /* the outdoor banner (a band opening from the middle) or the indoor one (put on column by column) */
    s16 left;           /* the banner: columns [left, right), rows [top, bottom); none when empty */
    s16 right;
    s16 top;
    s16 bottom;
    s16 scroll;         /* its streaks' horizontal scroll */
    bool8 monShown;     /* the Pokemon's picture, centred at monX, monY */
    s16 monX;
    s16 monY;
    u16 tileNum;        /* its sprite's OAM tiles (hidden while the UI draws it) */
    u16 species;
    bool8 shiny;
    u32 personality;
};
bool8 FieldEffect_NativeUiShowMon(struct NativeFieldMoveState *state);

/* battle_transition.c: the angled wipes' lines in order: start x and y, end x and y, side (0: black to the left of the
 * line, 1: to the right); FALSE past the last */
bool8 BattleTransition_NativeUiAngledWipe(u8 index, s16 line[5]);

/* map_name_popup.c: the location pop-up sliding in, shown or sliding out: its name and how far above its place it is
 * (0 in place, *offscreen gone) */
bool8 MapNamePopUp_NativeUiState(u8 *name, u32 size, u16 *offset, u16 *offscreen);

/* ---------------------------------------------------------------- platform (src/platform/sdl2.c) */

/* the PC's F10 settings menu is open (the game gets no buttons meanwhile) */
bool8 Platform_NativeUiPcMenuOpen(void);
/* the keyboard key now bound to a game button, as a prompt shows it ("Z", "ENTER", "\\"...);
 * action is a KeybindAction (include/platform/keybinds.h); empty when unknown */
void Platform_NativeUiKeyLabel(int action, char *out, int size);
/* save file writes since start-up: written, failed; *pending: an image waits for the next write */
void Platform_NativeUiSaveWrites(u32 *written, u32 *failed, bool8 *pending);
/* where the 240x160 GBA picture is in a window of outputWidth x outputHeight pixels; *fieldFillsWindow: the last
 * frame drew the field in 3D, filling the whole window (else only the picture's rectangle is the game's) */
void Platform_NativeUiPictureRect(int outputWidth, int outputHeight, int *x, int *y, int *w, int *h,
                                  bool8 *fieldFillsWindow);

#endif // GUARD_NATIVE_UI_ENGINE_H
