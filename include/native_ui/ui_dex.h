#ifndef GUARD_NATIVE_UI_DEX_H
#define GUARD_NATIVE_UI_DEX_H

/*
 * 6 · Pokédex: the pieces the device (ui_pokedex.c) shares with its entry's
 * AREA, CRY and SIZE pages and the search form (ui_dex_pages.c).
 */
#include <stdbool.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_widgets.h"

/* the design's device palette (preview/js/screens/pokedex.js pal()) */
struct DexPal
{
    struct UiRamp dev;
    UiRGBA bg, bg2, bezel, bezelHi, rowSel, rowSelHi, rowSelLo, stripe, rule, muted;
    struct UiRamp lens;
};
struct DexPal UiDex_Pal(void);

/* the selected Pokémon, as the entry pane shows it */
struct UiDexEntry
{
    u16 species;
    u32 personality;
    bool8 seen;
    bool8 owned;
    u16 shownNum;
    const u8 *name;
    u8 category[48];
    s8 types[2];
    u16 height;          /* decimetres */
    u16 weight;          /* hectograms */
    const u8 *description;
    u16 forms;
};

/* the entry pane's frame: x, y, w, h of the left screen */
#define DEX_PANE_X 18
#define DEX_PANE_Y 43
#define DEX_PANE_W 280
#define DEX_PANE_H 284
#define DEX_LIST_X 322
#define DEX_LIST_W 300

/* mouse regions shared by both files */
enum
{
    DEX_HIT_ROW = UI_HIT_SCREEN,
    DEX_HIT_ENTRY,
    DEX_HIT_BUTTON,
    DEX_HIT_ORDER,
    DEX_HIT_RAIL,
    DEX_HIT_BACKDROP,
    DEX_HIT_PLAY,         /* the CRY page: play it again */
    DEX_HIT_SEARCH_TAB,   /* the search form: SEARCH, SHIFT, CANCEL */
    DEX_HIT_SEARCH_ROW,   /* ... its rows (pokedex_common.h's SEARCH_*) */
    DEX_HIT_SEARCH_OPTION,/* ... an option of the open list (its index) */
    DEX_HIT_SEARCH_ARROW, /* ... the option list's scroll arrows (0 up, 1 down) */
};

void UiDex_GridPanel(int x, int y, int w, int h, UiRGBA body, UiRGBA ink, UiRGBA grid);
void UiDex_CaughtMark(int x, int y);
void UiDex_SeenMark(int x, int y);
void UiDex_HeightText(u8 *dst, int size, u32 dm, bool imperial);
void UiDex_WeightText(u8 *dst, int size, u32 hg, bool imperial);
/* the stage colours for a Pokémon (its first type when caught, grey otherwise) */
void UiDex_StageColours(const struct UiDexEntry *e, UiRGBA *body, UiRGBA *ink, UiRGBA *grid, UiRGBA *shadow);

/* ---------------------------------------------------------------- ui_dex_pages.c */

/* the frame boundary: what the pages draw (the area map, the cry, the search) */
void UiDexPages_Capture(const struct NativePokedex *p, const struct UiDexEntry *e);
/* one of the entry's pages (AREA_SCREEN, CRY_SCREEN, SIZE_SCREEN) in the left
 * screen's content area (below the header, above the buttons' help line) */
void UiDexPages_DrawPage(const struct DexPal *c, u8 page, const struct UiDexEntry *e, int x, int y, int w, int t);
/* the help line over the buttons on a page (NULL: none) */
const u8 *UiDexPages_PageHelp(u8 page, const struct UiDexEntry *e);
/* the search form is up (Pokedex_NativeUiSearch) */
bool UiDexPages_SearchUp(void);
const struct NativeDexSearch *UiDexPages_Search(void);
/* draws the search form over both screens */
void UiDexPages_DrawSearch(const struct DexPal *c, int t);
/* a click on the form; true when handled */
bool UiDexPages_SearchClick(const struct UiHit *hit, int button);
bool UiDexPages_SearchWheel(int dy);
/* the form's mouse goals step with the game's own presses (once a frame) */
void UiDexPages_SearchStep(void);
void UiDexPages_SearchCancelGoals(void);

#endif // GUARD_NATIVE_UI_DEX_H
