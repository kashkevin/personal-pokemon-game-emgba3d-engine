#ifndef GUARD_NATIVE_UI_WIDGETS_H
#define GUARD_NATIVE_UI_WIDGETS_H

/*
 * The design's shared widgets (a port of artifacts/ui-art-direction/preview/
 * js/widgets.js): bars, badges, cursors, frames, key caps and hints, message
 * boxes, title tabs, DS buttons, menu boxes, YES/NO, scrollbars, page dots.
 * Engine text is engine bytes (ui_text.h); labels in the Route Caps face are
 * ASCII. Every widget that can be clicked registers a mouse region.
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_draw.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_text.h"

/* Mouse region ids (UiG_Hit); screens add their own from UI_HIT_SCREEN. */
enum UiHitId
{
    UI_HIT_NONE,
    UI_HIT_MENU,      /* index: the row */
    UI_HIT_YESNO,     /* index: 0 yes, 1 no */
    UI_HIT_HINT,      /* index: enum UiAction */
    UI_HIT_SCREEN = 100,
};

/* What a hint stands for: the game's own buttons (and two PC keys). */
enum UiAction
{
    UI_ACT_CONFIRM,  /* A */
    UI_ACT_CANCEL,   /* B */
    UI_ACT_START,
    UI_ACT_SELECT,
    UI_ACT_L,
    UI_ACT_R,
    UI_ACT_TABS,     /* L and R */
    UI_ACT_MOVE,     /* left / right */
    UI_ACT_SCROLL,   /* up / down */
    UI_ACT_PC_MENU,  /* F10 */
    UI_ACT_ESC,      /* the PC's Esc */
    UI_ACT_DETAILS,  /* the PC's Tab: a screen's own details view (the bag's item details) */
    UI_ACT_COUNT,
};

enum UiDevice
{
    UI_DEVICE_KEYBOARD,
    UI_DEVICE_PAD,
};
/* the device whose prompts are drawn (the last one used; the mouse keeps the keyboard's) */
enum UiDevice UiW_PromptDevice(void);
void UiW_SetPromptDevice(enum UiDevice device);
/* development: always this device's prompts (PKMN_NATIVE_UI_PROMPTS) */
void UiW_ForcePromptDevice(enum UiDevice device);

/* the standard list cursor phase: 1 px nudge every 16 frames */
int UiW_Nudge(int t);

int UiW_Gender(int x, int y, int gender); /* 0 male, 1 female, else none */
UiRGBA UiW_HpColour(double frac, int shade);
int UiW_HpBar(int x, int y, int w, double frac, bool label, UiRGBA ink);
int UiW_ExpBar(int x, int y, int w, double frac, bool label);
int UiW_TypeBadge(int x, int y, int type, int w);
int UiW_StatusBadge(int x, int y, const char *status, UiRGBA colour, int w);
int UiW_CategoryIcon(int x, int y, int category); /* 0 physical, 1 special, 2 status */
int UiW_Planned(int x, int y, const char *label);
void UiW_Cursor(int x, int y, int phase, UiRGBA colour);
void UiW_DownArrow(int x, int y, UiRGBA colour, bool up);
void UiW_SelectFrame(const struct UiMask *m, int x, int y, UiRGBA colour, UiRGBA hi, int width);
void UiW_CarryFrame(const struct UiMask *m, int x, int y);
int UiW_Keycap(int x, int y, const char *label);
int UiW_PadButton(int x, int y, const char *label);
/* glyphs for the action on the prompt device, then the label (engine text) */
int UiW_Hint(int x, int y, enum UiAction action, const uint8_t *label, bool dark);
int UiW_HintWidth(enum UiAction action, const uint8_t *label);
struct UiHintItem
{
    enum UiAction action;
    const uint8_t *label;
};
int UiW_Hints(int x, int y, const struct UiHintItem *items, int count, bool dark, int gap);
int UiW_HintsWidth(const struct UiHintItem *items, int count, int gap);

struct UiRamp UiW_FieldBand(void);
/* the Emerald message box; fills inner[4] = text area x, y, w, h */
void UiW_DialogBox(int x, int y, int w, int h, const struct UiRamp *band, UiRGBA body, int inner[4]);
int UiW_TitleTab(int x, int y, const char *label, const struct UiRamp *r, int w, const char *icon);
/* the Emerald wooden location sign (PK.locationSign: the pause menu's, the map name pop-up's), 184 x 34: the place's
 * name (a game string), with the time of day (clock, ASCII) at its right unless NULL */
#define UI_LOCATION_SIGN_W 184
#define UI_LOCATION_SIGN_H 34
void UiW_LocationSign(int x, int y, const uint8_t *name, const char *clock);

struct UiDsButtonOpt
{
    const char *icon;      /* pkui icon in a chip */
    bool noChip;
    bool selected;
    bool pressed;
    bool disabled;
    bool center;
    int lip;               /* 0: 3 */
    bool lipSet;
    int font;              /* enum UiFont */
};
const struct UiMask *UiW_DsButton(int x, int y, int w, int h, UiRGBA colour, const uint8_t *label,
                                  const struct UiDsButtonOpt *o);

struct UiMenuItem
{
    const uint8_t *label;
    bool disabled;
};
enum UiMenuAnchor
{
    UI_ANCHOR_TOPLEFT,
    UI_ANCHOR_RIGHT,
    UI_ANCHOR_BOTTOM,
    UI_ANCHOR_BOTTOMRIGHT,
};
struct UiMenuOpt
{
    int rowH;      /* 0: 16 */
    int w;         /* minimum width from the items; 0: auto */
    int minW;
    enum UiMenuAnchor anchor;
    int t;         /* frame counter for the cursor nudge */
    int hitId;     /* 0: UI_HIT_MENU */
    const struct UiRamp *band;
};
/* returns the box rectangle in out[4] */
void UiW_MenuBox(int x, int y, const struct UiMenuItem *items, int count, int sel,
                 const struct UiMenuOpt *o, int out[4]);
void UiW_YesNo(int x, int y, int sel, int t, enum UiMenuAnchor anchor, int out[4]);
void UiW_Scrollbar(int x, int y, int h, int first, int total, int visible, const struct UiRamp *r);
void UiW_PageDots(int x, int y, int n, int cur, UiRGBA on, UiRGBA off);
/* rows of palette letters: pal maps a letter to a colour (UI_NONE: clear) */
struct UiPixPal
{
    char letter;
    UiRGBA colour;
};
int UiW_Pix(const char *name, const char *const *rows, int rowCount, const struct UiPixPal *pal,
            int palCount, int x, int y, bool flip);
/* ... each pixel a scale x scale square (W.pix's scale option) */
int UiW_PixScaled(const char *name, const char *const *rows, int rowCount, const struct UiPixPal *pal,
                  int palCount, int x, int y, int scale);
void UiW_Missing(int x, int y, int w, int h);
/* An engine message as its window shows it: the page the printer is on, up to
 * the printed byte count (< 0: all), word-wrapped, the last maxLines lines;
 * a page break just reached keeps the previous page up (the game waits for a
 * button there). */
void UiW_MessagePage(const uint8_t *text, int printed, int x, int y, int width, int maxLines);
/* ... lineH pixels from one line to the next (16 above) */
void UiW_MessagePageEx(const uint8_t *text, int printed, int x, int y, int width, int maxLines, int lineH);
int UiW_MessagePageText(const uint8_t *text, int printed, uint8_t *page, int size);
int UiW_MessagePageFlip(const uint8_t *text, int printed, bool flip, uint8_t *page, int size);
void UiW_MessagePageFlipDraw(const uint8_t *text, int printed, bool flip, int x, int y, int width, int maxLines, int lineH);

/* src/platform/sdl2.c: the key bound to a game button for the prompts (see ui_engine.h) */
void Platform_NativeUiKeyLabel(int action, char *out, int size);

#endif // GUARD_NATIVE_UI_WIDGETS_H
