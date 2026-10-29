#ifndef GUARD_NATIVE_UI_SCREEN_H
#define GUARD_NATIVE_UI_SCREEN_H

/*
 * One replaced screen of the native UI. All callbacks run on the main thread:
 * detect and capture at the frame boundary while the game is parked (so they
 * may read game memory), draw and click while presenting (they may only use
 * what capture copied).
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_draw.h"

/* How the GBA picture's windows apply while a screen is up: a screen that draws a
 * window's shape itself (a battle transition's circle) takes one region everywhere. */
enum UiWindows
{
    UI_WINDOWS_GAME,       /* as the game has them */
    UI_WINDOWS_INSIDE,     /* every pixel as inside WIN0 */
    UI_WINDOWS_OUTSIDE,    /* every pixel as outside the windows */
};

enum UiPicture
{
    UI_PICTURE_KEEP,       /* the GBA picture stays (overlay only) */
    UI_PICTURE_HIDE_BG0,   /* the GBA window layer is hidden (field menus) */
    UI_PICTURE_HIDE_ALL,   /* the whole GBA picture is hidden (full screens) */
    UI_PICTURE_HIDE_BG012, /* BG0 to BG2 hidden: the battle's level-up box and banner too */
    UI_PICTURE_HIDE_BG02,  /* BG0 and BG2 hidden: a contest's text and panels (BG1 keeps the moves' effects) */
};

struct UiScreen
{
    const char *name;      /* PKMN_NATIVE_UI_SCREENS name */
    /* the engine shows this screen now */
    bool (*detect)(void);
    /* copy what draw needs (only called after detect returned true) */
    void (*capture)(void);
    /* draw; t counts game frames since the screen came up */
    void (*draw)(int t);
    /* a click on one of the screen's regions; button 1 left, 3 right */
    bool (*click)(const struct UiHit *hit, int button);
    /* the mouse wheel over the screen (dy > 0: away from the user) */
    bool (*wheel)(int dy);
    /* the GBA buttons about to reach the game (NULL: unchanged) */
    uint16_t (*filterKeys)(uint16_t keys, uint16_t newKeys);
    /* the engine reads buttons now: queued presses wait for it (NULL: always) */
    bool (*ready)(void);
    /* how much of the GBA picture to hide while up */
    enum UiPicture (*picture)(void);
    /* a PC key that is not one of the game's buttons (enum UiPcKey); true: taken */
    bool (*pcKey)(int key);
    /* the GBA picture's OAM entries the screen draws itself (bit n of mask[n / 32]:
     * entry n), at the frame boundary after capture; NULL: none */
    void (*hideObjects)(uint32_t mask[4]);
    /* typing on the PC keyboard: while textMode() is true the keys that type
     * reach text() (UTF-8) instead of the game's buttons; Enter is A,
     * Backspace and Esc are B, Page Down is SELECT, Page Up SELECT twice;
     * the arrows and the controller stay the game's (NULL: no typing) */
    bool (*textMode)(void);
    void (*text)(const char *utf8);
    /* the GBA picture's windows while up (NULL: the game's) */
    enum UiWindows (*windows)(void);
};

enum UiPcKey
{
    UI_PCKEY_DETAILS,      /* Tab */
};

/* Mouse intents become engine button presses: queued, one edge a frame
 * (press, release), in order. */
void NativeUi_QueuePress(uint16_t key);
void NativeUi_QueuePresses(uint16_t key, int count);
bool NativeUi_PressesPending(void);
/* the left mouse button is held down over the game window (a drag), and the
 * screen region under the mouse (NULL: none) */
bool NativeUi_MouseHeld(void);
const struct UiHit *NativeUi_MouseHover(void);
/* the game frame counter of the UI (60 a second, stops while the game is paused) */
uint32_t NativeUi_Frame(void);
/* the time since the last 60 Hz step, 0..1 (smooth animation at any refresh rate) */
double NativeUi_FrameAlpha(void);
/* the engine's full-screen palette fade as a veil (colour and opacity; UI_NONE: none) */
UiRGBA NativeUi_ScreenFade(void);
bool NativeUi_LogEnabled(void);
/* a line in the game log ("Native UI: ..."), with PKMN_NATIVE_UI_LOG=1 only */
void NativeUi_Logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* the PC's own actions: close the game window (as its close button: nothing is
 * saved), open the PC settings menu (as F10) */
void NativeUi_QuitGame(void);
void NativeUi_OpenPcMenu(void);
/* frame boundary: the screen the UI shows now - or, with the UI off, the one it
 * would show (its detection only) - by name; NULL: the game's own picture */
const char *NativeUi_ScreenNow(void);
/* Screen files use the engine's _("...") strings: they must not include SDL's
 * headers (its '\\' key literal confuses tools/preproc's string scanner). */
/* screens */
extern const struct UiScreen gUiScreen_Title;
extern const struct UiScreen gUiScreen_Pokenav;
extern const struct UiScreen gUiScreen_MainMenu;
extern const struct UiScreen gUiScreen_Pause;
extern const struct UiScreen gUiScreen_Options;
extern const struct UiScreen gUiScreen_Party;
extern const struct UiScreen gUiScreen_Summary;
extern const struct UiScreen gUiScreen_MoveSwitch;
extern const struct UiScreen gUiScreen_Bag;
extern const struct UiScreen gUiScreen_Pokedex;
extern const struct UiScreen gUiScreen_DexNewEntry;
extern const struct UiScreen gUiScreen_Storage;
extern const struct UiScreen gUiScreen_StorageMenus;
extern const struct UiScreen gUiScreen_PcItems;
extern const struct UiScreen gUiScreen_Battle;
extern const struct UiScreen gUiScreen_Customize;
extern const struct UiScreen gUiScreen_Players;
extern const struct UiScreen gUiScreen_TrainerCard;
extern const struct UiScreen gUiScreen_Mail;
extern const struct UiScreen gUiScreen_EasyChat;
extern const struct UiScreen gUiScreen_PcMailbox;
extern const struct UiScreen gUiScreen_PcDecor;
extern const struct UiScreen gUiScreen_HallOfFame;
extern const struct UiScreen gUiScreen_Credits;
extern const struct UiScreen gUiScreen_TradeSequence;
extern const struct UiScreen gUiScreen_HillBoard;
extern const struct UiScreen gUiScreen_PokeblockCase;
extern const struct UiScreen gUiScreen_UsePokeblock;
extern const struct UiScreen gUiScreen_PokeblockFeed;
extern const struct UiScreen gUiScreen_Contest;
extern const struct UiScreen gUiScreen_ContestResults;
extern const struct UiScreen gUiScreen_BerryBlender;
extern const struct UiScreen gUiScreen_ContestPainting;
extern const struct UiScreen gUiScreen_BerryTag;
extern const struct UiScreen gUiScreen_Diploma;
extern const struct UiScreen gUiScreen_Factory;
extern const struct UiScreen gUiScreen_DomeTree;
extern const struct UiScreen gUiScreen_Shop;
extern const struct UiScreen gUiScreen_Naming;
extern const struct UiScreen gUiScreen_Starters;
extern const struct UiScreen gUiScreen_Evolution;
extern const struct UiScreen gUiScreen_GameCorner;
extern const struct UiScreen gUiScreen_SlotMachine;
extern const struct UiScreen gUiScreen_Roulette;
extern const struct UiScreen gUiScreen_FieldText;
extern const struct UiScreen gUiScreen_MapName;
extern const struct UiScreen gUiScreen_Transition;
extern const struct UiScreen gUiScreen_FieldMove;
extern const struct UiScreen gUiScreen_MatchCall;
extern const struct UiScreen gUiScreen_RegionMap;
extern const struct UiScreen gUiScreen_EggHatch;
extern const struct UiScreen gUiScreen_WallClock;
extern const struct UiScreen gUiScreen_Notice;

#endif // GUARD_NATIVE_UI_SCREEN_H
