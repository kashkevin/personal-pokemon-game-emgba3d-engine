#ifdef ENABLE_NATIVE_UI
/*
 * 13 · PokéNav (step 13 of the owner's plan, 2026-10-29: the PokéNav in the design's look): the PokéNav's pages over
 * the game's own PokéNav (pokenav*.c), which keeps running under them and keeps every rule: its pages and what each
 * button does, its cursors and lists, calls, markings, flying from the map. The UI reads the page up at the frame
 * boundary (pokenav.c Pokenav_NativeUiState), draws it on the design's device and turns clicks into the game's own
 * button presses. Drawn here: the menus (the main menu, CONDITION's and its search's) and MATCH CALL (the trainers
 * registered, a trainer's menu and check page, a call), CONDITION (a Pokemon's conditions, sheen and markings, the
 * party's or a search's), a search's ranked results, and RIBBONS (the Pokemon with ribbons, a Pokemon's ribbon
 * case). The HOENN MAP is screens/ui_region_map.c's; between pages this screen's device stays up. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "decompress.h"
#include "main.h"
#include "pokemon_storage_system.h"
#include "pokenav.h"
#include "region_map.h"
#include "string_util.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_MENU_ITEM = UI_HIT_SCREEN,
    HIT_LIST_ROW,
    HIT_OPTION,
    HIT_MESSAGE,
    HIT_MON_ROW,
    HIT_MARK,
    HIT_RIBBON,
    HIT_MON_STEP,
};

/* the PokéNav's blue (the GBA's device and its screen's cyan) */
#define PNAV_BLUE UI_HEX(0x3A78C8)
#define PNAV_CYAN UI_HEX(0x58C8E0)

static const u8 sText_Open[] = _("OPEN");
static const u8 sText_Select[] = _("SELECT");
static const u8 sText_Back[] = _("BACK");
static const u8 sText_PutAway[] = _("PUT AWAY");
static const u8 sText_Menu[] = _("MENU");
static const u8 sText_Ok[] = _("OK");
static const u8 sText_Cancel[] = _("CANCEL");
static const u8 sText_Next[] = _("NEXT");
static const u8 sText_HangUp[] = _("HANG UP");
static const u8 sText_Scroll[] = _("TRAINER");
static const u8 sText_NoPlace[] = _("Somewhere in HOENN");
static const u8 sText_Condition[] = _("CONDITION");
static const u8 sText_Markings[] = _("MARKINGS");
static const u8 sText_Pokemon[] = _("POKéMON");
static const u8 sText_InParty[] = _("In your party");
static const u8 sText_ReturnCondition[] = _("Return to the CONDITION menu.");
static const u8 sText_Egg[] = _("EGG");
static const u8 sText_Ribbons[] = _("RIBBONS");
static const u8 sText_Ribbon[] = _("RIBBON");

static struct NativePokenav sP;
static int sPageSince;          /* frames since the page came up (its slide) */
static u8 sPrevPage;
static u8 sPrevMenuType;
static int sCursorSince;        /* frames since the cursor moved */
static u8 sPrevCursor;
static u16 sPrevListCursor;
static u8 sPlace[NATIVE_PNAV_ROWS][32];   /* the rows' places (GetMapName) */
static u8 sMsg[UI_MESSAGE_COPY];          /* a call's words, as the game prints them */
static u32 sPrinted;
static bool sMsgUp, sMsgPrinting, sMsgWaiting;
static bool sPicReady;
static bool sMonPicReady;
static u8 sWhere[32];                     /* the cursor's Pokemon's place: the party or its box */

/* a click's goal, reached a press at a time as the game takes them: it reads no buttons while a page animates, so
 * presses queued all at once would be lost */
enum
{
    GOAL_NONE,
    GOAL_MENU,                            /* a menu item (the menu wraps) */
    GOAL_LIST,                            /* a list's row: the trainers, a search's, the party's, the ribbon list's */
    GOAL_OPTION,                          /* a trainer's menu item */
    GOAL_MARK,                            /* a markings menu row (it wraps) */
    GOAL_RIBBON,                          /* a ribbon in the case */
};
static struct
{
    u8 kind;
    u8 page;
    bool thenA;                           /* A once there */
    int target;
    int presses;
} sGoal;

static void Title(const char *label);
static void Hints(const struct UiHintItem *items, int count);

static void StepGoal(void);

static bool Detect(void)
{
    struct NativePokenav p;

    /* every page, and between pages as they slide and fade: the game's own frames never show through (the HOENN
       MAP, once its map is set up, is screens/ui_region_map.c's: that screen is asked first) */
    return Pokenav_NativeUiState(&p);
}

static void Capture(void)
{
    Pokenav_NativeUiState(&sP);
    if (sP.page != sPrevPage || (sP.page == NATIVE_PNAV_MENU && sP.menuType != sPrevMenuType))
        sPageSince = 0;
    else if (sPageSince < 1000)
        sPageSince++;
    if (sP.menuCursor != sPrevCursor || sP.listCursor != sPrevListCursor)
        sCursorSince = 0;
    else if (sCursorSince < 1000)
        sCursorSince++;
    sPrevPage = sP.page;
    sPrevMenuType = sP.menuType;
    sPrevCursor = sP.menuCursor;
    sPrevListCursor = sP.listCursor;
    sMsgUp = sMsgPrinting = sMsgWaiting = false;
    sPicReady = false;
    sMonPicReady = false;
    if (sP.page == NATIVE_PNAV_CONDITION && !sP.condCancel && sP.cond.species != SPECIES_NONE)
    {
        sMonPicReady = UiGfx_PrepareMonPic(sP.cond.species, sP.cond.personality, sP.condShiny, sP.cond.egg);
        if (sP.condBox >= TOTAL_BOXES_COUNT)
            StringCopy(sWhere, sText_InParty);
        else
            StringCopyN(sWhere, GetBoxNamePtr(sP.condBox), sizeof(sWhere) - 1);
    }
    if ((sP.page == NATIVE_PNAV_SEARCH || sP.page == NATIVE_PNAV_RIBBON_LIST) && sP.listCursor >= sP.listTop
     && sP.listCursor - sP.listTop < sP.rowCount)
    {
        const struct NativePokenavRow *r = &sP.rows[sP.listCursor - sP.listTop];

        sMonPicReady = UiGfx_PrepareMonPic(r->species, r->personality, r->shiny, r->egg);
    }
    if (sP.page == NATIVE_PNAV_RIBBONS && sP.cond.species != SPECIES_NONE)
    {
        sMonPicReady = UiGfx_PrepareMonPic(sP.cond.species, sP.cond.personality, sP.condShiny, sP.cond.egg);
        if (sP.condBox >= TOTAL_BOXES_COUNT)
            StringCopy(sWhere, sText_InParty);
        else
            StringCopyN(sWhere, GetBoxNamePtr(sP.condBox), sizeof(sWhere) - 1);
    }
    if (sP.page == NATIVE_PNAV_MATCH_CALL)
    {
        for (int i = 0; i < sP.rowCount; i++)
        {
            if (sP.rows[i].mapSec >= MAPSEC_NONE || GetMapName(sPlace[i], sP.rows[i].mapSec, 0) == sPlace[i])
                StringCopy(sPlace[i], sText_NoPlace);
        }
        sPicReady = sP.mcPic != 0xFFFF && UiGfx_PrepareTrainerPic(sP.mcPic);
        if (sP.mcState == NATIVE_PNAV_MC_CALL && sP.mcMessageWindow != WINDOW_NONE)
        {
            bool8 active = FALSE;

            sMsgUp = TextPrinter_NativeUiWindowText(sP.mcMessageWindow, sMsg, sizeof(sMsg), &sPrinted, &active);
            sMsgWaiting = sMsgUp && active && TextPrinter_NativeUiWindowWaiting(sP.mcMessageWindow);
            sMsgPrinting = sMsgUp && active && !sMsgWaiting;
        }
    }
    StepGoal();
}

/* ---------------------------------------------------------------- the device */

static struct UiRamp BlueRamp(void)
{
    return UiRampOf(PNAV_BLUE, 0.42, 0.28, 0.62);
}

static void Background(void)
{
    struct UiRamp b = BlueRamp();
    struct UiPattern dots = UiPat_Dots(8, 1, 0, 0, true);
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UiMix(b.lo, b.base, 0.35));
    UiG_FillPattern(&dots, bx, by, bw, bh, UiMix(b.base, b.hi, 0.35), 0, 0);
}

/* the device's screen: the game's cyan LCD with its scan lines, rounded in the device's frame */
static void Screen(int x, int y, int w, int h)
{
    struct UiRamp b = BlueRamp();
    struct UiPattern scan = UiPat_HStripe(2, 1, 0);

    UiG_Paint(UiM_Rounded4(w + 8, h + 8, 5, 5, 5, 5), x - 4, y - 4, b.base,
              &(struct UiPaint){b.ink, b.hi, b.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.55),
              &(struct UiPaint){b.ink, UI_NONE, UI_NONE, 0, 0, &scan, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.4)});
}

/* ---------------------------------------------------------------- icons */

/* CONDITION: the condition graph's pentagon, its five points joined to the middle */
static void IconCondition(int x, int y, UiRGBA ink, UiRGBA fill)
{
    static const int pts[5][2] = {{11, 1}, {21, 9}, {17, 21}, {5, 21}, {1, 9}};

    UiG_Paint(UiM_Circle(18), x + 3, y + 3, fill, &(struct UiPaint){UI_NONE, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    for (int i = 0; i < 5; i++)
    {
        int ax = pts[i][0], ay = pts[i][1], bx = pts[(i + 1) % 5][0], by = pts[(i + 1) % 5][1];
        int n = abs(bx - ax) > abs(by - ay) ? abs(bx - ax) : abs(by - ay);

        for (int s = 0; s <= n; s++)
        {
            UiG_Put(x + ax + (bx - ax) * s / n, y + ay + (by - ay) * s / n, ink);
            if (s % 2 == 0)
                UiG_Put(x + 11 + (ax - 11) * s / n, y + 12 + (ay - 12) * s / n, UiMix(ink, fill, 0.45));
        }
    }
}

/* RIBBONS: a rosette with its two tails */
static void IconRibbon(int x, int y, UiRGBA colour)
{
    struct UiRamp r = UiRampOf(colour, 0.42, 0.28, 0.62);

    UiG_Paint(UiM_Rounded4(5, 10, 0, 0, 1, 1), x + 6, y + 12, r.lo, &(struct UiPaint){r.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(5, 10, 0, 0, 1, 1), x + 13, y + 12, r.lo, &(struct UiPaint){r.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Circle(16), x + 4, y + 1, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 1, NULL, UI_NONE});
    UiG_Paint(UiM_Circle(8), x + 8, y + 5, UI_HEX(0xF8D850), &(struct UiPaint){r.ink, UI_HEX(0xFFF0A0), UI_NONE, 0, 0, NULL, UI_NONE});
}

/* a contest condition's gem */
static void IconGem(int x, int y, UiRGBA colour)
{
    struct UiRamp r = UiRampOf(colour, 0.42, 0.28, 0.62);

    UiG_Paint(UiM_Circle(18), x + 3, y + 3, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, UI_NONE});
    UiG_Rect(x + 8, y + 7, 3, 2, UiMix(r.hi, UI_HEX(0xFFFFFF), 0.6));
}

static const u32 sContestHex[5] = {0xE05040, 0x4878E0, 0xF078B0, 0x48B058, 0xE8B030};

static void ItemIcon(u8 item, int x, int y, bool on)
{
    switch (item)
    {
    case POKENAV_MENUITEM_MAP:
        UiGfx_Icon(on ? "map_sel" : "map", x, y, 0);
        break;
    case POKENAV_MENUITEM_CONDITION:
        IconCondition(x, y, UI_HEX(0x285898), on ? UI_HEX(0xF8E8F0) : UI_HEX(0xE8E8F8));
        break;
    case POKENAV_MENUITEM_MATCH_CALL:
        UiGfx_Icon(on ? "players_sel" : "players", x, y, 0);
        break;
    case POKENAV_MENUITEM_RIBBONS:
        IconRibbon(x, y, UI_HEX(0xD84858));
        break;
    case POKENAV_MENUITEM_CONDITION_PARTY:
        UiW_PokeBall(x + 12, y + 12, 18, 0);
        break;
    case POKENAV_MENUITEM_CONDITION_SEARCH:
        UiGfx_Icon("search", x + 5, y + 5, 0);
        break;
    case POKENAV_MENUITEM_CONDITION_SEARCH_COOL:
    case POKENAV_MENUITEM_CONDITION_SEARCH_BEAUTY:
    case POKENAV_MENUITEM_CONDITION_SEARCH_CUTE:
    case POKENAV_MENUITEM_CONDITION_SEARCH_SMART:
    case POKENAV_MENUITEM_CONDITION_SEARCH_TOUGH:
        IconGem(x, y, UI_HEX(sContestHex[item - POKENAV_MENUITEM_CONDITION_SEARCH_COOL]));
        break;
    default:   /* SWITCH OFF and the cancels */
        UiGfx_Icon(on ? "return_blue" : "return", x + 4, y + 5, 0);
        break;
    }
}

static const char *ItemLabel(u8 item)
{
    switch (item)
    {
    case POKENAV_MENUITEM_MAP: return "HOENN MAP";
    case POKENAV_MENUITEM_CONDITION: return "CONDITION";
    case POKENAV_MENUITEM_MATCH_CALL: return "MATCH CALL";
    case POKENAV_MENUITEM_RIBBONS: return "RIBBONS";
    case POKENAV_MENUITEM_SWITCH_OFF: return "SWITCH OFF";
    case POKENAV_MENUITEM_CONDITION_PARTY: return "PARTY";
    case POKENAV_MENUITEM_CONDITION_SEARCH: return "SEARCH";
    case POKENAV_MENUITEM_CONDITION_SEARCH_COOL: return "COOL";
    case POKENAV_MENUITEM_CONDITION_SEARCH_BEAUTY: return "BEAUTY";
    case POKENAV_MENUITEM_CONDITION_SEARCH_CUTE: return "CUTE";
    case POKENAV_MENUITEM_CONDITION_SEARCH_SMART: return "SMART";
    case POKENAV_MENUITEM_CONDITION_SEARCH_TOUGH: return "TOUGH";
    default: return "CANCEL";
    }
}

static const char *MenuTitle(void)
{
    switch (sP.menuType)
    {
    case POKENAV_MENU_TYPE_CONDITION: return "CONDITION";
    case POKENAV_MENU_TYPE_CONDITION_SEARCH: return "CONDITION SEARCH";
    default: return "POK" UI_CAPS_E_ACUTE "NAV";
    }
}

/* ---------------------------------------------------------------- the menus */

static void Menu(int t)
{
    struct UiRamp b = BlueRamp();
    struct UiRamp tab = {b.hi, b.lo, UiMix(b.lo, b.ink, 0.5), b.ink};
    int n = sP.menuCount > MAX_POKENAV_MENUITEMS ? MAX_POKENAV_MENUITEMS : sP.menuCount;
    int rowH = n > 5 ? 40 : 46, w = 268, x0 = 24, y0 = 58;
    int slide = sPageSince < 6 ? (6 - sPageSince) * 10 : 0;
    int sx = 324, sy = 58, sw = 292, sh = 222;
    int hint[4];
    u8 item = sP.menuItems[sP.menuCursor < MAX_POKENAV_MENUITEMS ? sP.menuCursor : 0];

    (void)tab;
    Title(MenuTitle());
    /* the items, as the game lists them; the cursor's lifts and glows */
    for (int i = 0; i < n; i++)
    {
        u8 it = sP.menuItems[i];
        bool on = i == sP.menuCursor;
        int ry = y0 + i * rowH, rx = x0 - slide * (i + 1) / 2 + (on ? 6 : 0);
        const struct UiMask *m = UiM_Rounded4(w, rowH - 6, 4, 4, 4, 4);
        struct UiRamp r = on ? UiRampOf(UI_HEX(0xF8F8F0), 0.42, 0.28, 0.62) : UiRampOf(UiMix(b.base, b.hi, 0.25), 0.42, 0.28, 0.62);

        if (on)
            UiW_SelectFrame(m, rx, ry, UI_NONE, UI_NONE, 2);
        UiG_Paint(m, rx, ry, r.base, &(struct UiPaint){b.ink, r.hi, r.lo, 0, 2, NULL, UI_NONE});
        ItemIcon(it, rx + 10, ry + ((rowH - 6 - 24) >> 1) - (on && (t / 16) % 2 ? 1 : 0), on);
        UiCaps_Draw(rx + 46, ry + ((rowH - 6 - 7) >> 1), ItemLabel(it), on ? b.ink : UI_P_TEXT_W, on ? UI_NONE : b.ink,
                    on ? UI_CAPS_PLAIN : UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        if (on)
            UiW_Cursor(rx + w - 18, ry + ((rowH - 6 - 9) >> 1), UiW_Nudge(t), b.ink);
        UiG_Hit(HIT_MENU_ITEM, i, rx, ry, w, rowH - 6);
    }

    /* the device's screen: the cursor's item, big, and what it does (the game's words) */
    Screen(sx, sy, sw, sh);
    UiG_Save();
    UiG_Translate(sx + (sw >> 1) - 24, sy + 34);
    UiG_Paint(UiM_Circle(48), 0, 0, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.8),
              &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    ItemIcon(item, 12, 12, true);
    UiG_Restore();
    UiCaps_Draw(sx + (sw >> 1), sy + 92, ItemLabel(item), b.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    if (sP.menuDesc != NULL)
    {
        const uint8_t *starts[3];
        int lengths[3];
        int lines = UiText_Wrap(sP.menuDesc, sw - 32, UI_FONT_NORMAL, starts, lengths, 3);

        for (int i = 0; i < lines; i++)
        {
            u8 line[96];

            UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
            UiText_Draw(UI_FONT_NORMAL, sx + (sw >> 1), sy + 112 + i * 16, line, b.ink, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3),
                        UI_ALIGN_CENTER, 0);
        }
    }
    if (sP.menuNoRibbons)
        UiCaps_Draw(sx + (sw >> 1), sy + sh - 22, "NO RIBBONS YET", UiMix(b.ink, UI_HEX(0xE04040), 0.6), UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);

    /* what the buttons do */
    {
        struct UiHintItem items[2] = {
            {UI_ACT_CONFIRM, item == POKENAV_MENUITEM_SWITCH_OFF || item == POKENAV_MENUITEM_CONDITION_CANCEL
                          || item == POKENAV_MENUITEM_CONDITION_SEARCH_CANCEL ? sText_Select : sText_Open},
            {UI_ACT_CANCEL, sP.menuType == POKENAV_MENU_TYPE_CONDITION || sP.menuType == POKENAV_MENU_TYPE_CONDITION_SEARCH
                          ? sText_Back : sText_PutAway},
        };

        (void)hint;
        Hints(items, 2);
    }
}

/* ---------------------------------------------------------------- shared */

static void Title(const char *label)
{
    struct UiRamp b = BlueRamp();
    struct UiRamp tab = {b.hi, b.lo, UiMix(b.lo, b.ink, 0.5), b.ink};

    UiW_TitleTab(0, 8, label, &tab, 0, NULL);
}

static void Hints(const struct UiHintItem *items, int count)
{
    struct UiRamp b = BlueRamp();
    int hint[4];

    UiW_DialogBox(24, 300, 592, 40, &b, UI_NONE, hint);
    UiW_Hints(hint[0] + 4, hint[1] + 2, items, count, false, 12);
}

/* ---------------------------------------------------------------- MATCH CALL */

static const char *OptionLabel(u8 option)
{
    switch (option)
    {
    case MATCH_CALL_OPTION_CALL: return "CALL";
    case MATCH_CALL_OPTION_CHECK: return "CHECK";
    default: return "CANCEL";
    }
}

/* the trainers registered: the game's window of them, the cursor's lit */
static void TrainerList(int t, int x, int y, int w, int rowH)
{
    struct UiRamp b = BlueRamp();
    int shown = sP.listShown > 0 ? sP.listShown : 6;

    UiG_Paint(UiM_Rounded4(w, rowH * shown + 12, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int i = 0; i < sP.rowCount && i < shown; i++)
    {
        const struct NativePokenavRow *row = &sP.rows[i];
        bool on = sP.listTop + i == sP.listCursor;
        int ry = y + 6 + i * rowH;

        if (on)
        {
            const struct UiMask *rm = UiM_Rounded4(w - 12, rowH - 2, 2, 2, 2, 2);

            UiW_SelectFrame(rm, x + 6, ry, UI_NONE, UI_NONE, 2);
            UiG_Paint(rm, x + 6, ry, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.7),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UiMix(PNAV_CYAN, b.lo, 0.3), 0, 0, NULL, UI_NONE});
        }
        UiGfx_Icon(on ? "players_sel" : "players", x + 10, ry + ((rowH - 2 - 24) >> 1), 0);
        /* the name, the trainer's class at the row's end (before a rematch's Poke Ball) */
        UiText_Draw(UI_FONT_NORMAL, x + 40, ry + (rowH >> 1) - 9, row->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_SMALL, x + w - 34, ry + (rowH >> 1) - 7, row->sub, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_RIGHT,
                    0);
        if (row->rematch)
            UiW_PokeBall(x + w - 22, ry + (rowH >> 1) - 1, 12, (t / 4) % 16 < 2 ? 1 : 0);
        UiG_Hit(HIT_LIST_ROW, sP.listTop + i, x + 6, ry, w - 12, rowH - 2);
    }
    if (sP.listCount > shown)
        UiW_Scrollbar(x + w + 4, y + 4, rowH * shown + 4, sP.listTop, sP.listCount, shown, &b);
}

static void Trainer(int x, int y, int w, int h)
{
    struct UiRamp b = BlueRamp();
    int i = sP.listCursor - sP.listTop;
    const struct NativePokenavRow *row = i >= 0 && i < sP.rowCount ? &sP.rows[i] : NULL;

    Screen(x, y, w, h);
    if (row == NULL)
        return;
    UiG_Paint(UiM_Rounded4(76, 76, 3, 3, 3, 3), x + 10, y + 10, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.85),
              &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    if (sPicReady)
        UiGfx_TrainerPic(sP.mcPic, x + 16, y + 16, NULL);
    else
        UiGfx_Icon("players", x + 36, y + 36, 0);
    UiText_Draw(UI_FONT_SMALL, x + 96, y + 12, row->sub, b.ink, UI_NONE, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + 96, y + 26, row->name, b.ink, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3), UI_ALIGN_LEFT, 0);
    UiCaps_Draw(x + 96, y + 52, "PLACE", UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiText_Draw(UI_FONT_SMALL, x + 96, y + 62, sPlace[i], b.ink, UI_NONE, UI_ALIGN_LEFT, 0);
    if (row->rematch)
        UiCaps_Draw(x + 96, y + 78, "WANTS A REMATCH", UI_HEX(0xC03838), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sP.mcState == NATIVE_PNAV_MC_CHECK)
    {
        static const char *const heads[4] = {"STRATEGY", "POK" UI_CAPS_E_ACUTE "MON", "ABOUT", ""};
        int ty = y + 100;

        for (int k = 0; k < 4; k++)
        {
            const uint8_t *starts[3];
            int lengths[3], lines;

            if (sP.mcCheck[k] == NULL)
                continue;
            if (heads[k][0] != '\0')
            {
                UiCaps_Draw(x + 12, ty, heads[k], UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT,
                            UI_NONE);
                ty += 10;
            }
            lines = UiText_Wrap(sP.mcCheck[k], w - 24, UI_FONT_SMALL, starts, lengths, 3);
            for (int l = 0; l < lines; l++)
            {
                u8 line[96];

                UiText_CopyN(line, sizeof(line), starts[l], lengths[l]);
                UiText_Draw(UI_FONT_SMALL, x + 12, ty, line, b.ink, UI_NONE, UI_ALIGN_LEFT, 0);
                ty += 12;
            }
            ty += 4;
        }
    }
    else if (sP.mcCheck[0] != NULL)
    {
        UiCaps_Draw(x + 12, y + 100, "CHECK FOR MORE", UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_LEFT, UI_NONE);
    }
}

/* a trainer's menu, by its row */
static void Options(int t, int x, int y)
{
    struct UiRamp b = BlueRamp();
    int rowH = 20, w = 96, h = sP.mcOptionCount * rowH + 10;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    for (int i = 0; i < sP.mcOptionCount; i++)
    {
        bool on = i == sP.mcOptionCursor;
        int ry = y + 5 + i * rowH;

        if (on)
            UiG_Paint(UiM_Rounded4(w - 10, rowH - 2, 2, 2, 2, 2), x + 5, ry, UI_HEX(0xD8F0F8),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UI_HEX(0xA8D8E8), 0, 0, NULL, UI_NONE});
        if (on)
            UiW_Cursor(x + 9, ry + 5, UiW_Nudge(t), b.ink);
        UiCaps_Draw(x + 24, ry + 6, OptionLabel(sP.mcOptions[i]), on ? b.ink : UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_OPTION, i, x + 5, ry, w - 10, rowH - 2);
    }
}

static void MatchCall(int t)
{
    int lx = 24, ly = 58, lw = 276, rowH = 28;   /* the game's eight rows above the hints */
    int sx = 324, sy = 58, sw = 292, sh = NATIVE_PNAV_ROWS * 28 + 12;

    Title("MATCH CALL");
    TrainerList(t, lx, ly, lw, rowH);
    Trainer(sx, sy, sw, sh);
    if (sP.mcState == NATIVE_PNAV_MC_OPTIONS)
    {
        int row = sP.listCursor - sP.listTop, h = sP.mcOptionCount * 20 + 10;
        int oy = ly + 6 + (row < 0 ? 0 : row) * rowH + rowH - 4;

        /* under its row, or over it when that would run past the list */
        if (oy + h > 292)
            oy = ly + 6 + (row < 0 ? 0 : row) * rowH - h + 4;
        Options(t, lx + lw - 110, oy);
    }
    if (sP.mcState == NATIVE_PNAV_MC_CALL)
    {
        struct UiRamp b = BlueRamp();
        int inner[4];
        /* the dots wait for A before the trainer speaks (the game's page break); A or B hangs up once they have */
        struct UiHintItem hang = {UI_ACT_CONFIRM, sMsgWaiting ? sText_Next : sText_HangUp};

        UiW_DialogBox(24, 252, 592, 88, &b, UI_NONE, inner);
        if (sMsgUp)
            UiW_MessagePage(sMsg, sMsgPrinting || sMsgWaiting ? (int)sPrinted : -1, inner[0] + 2, inner[1], inner[2] - 4, 3);
        if (sMsgWaiting || (!sMsgPrinting && !sP.busy))
            UiW_Hints(inner[0] + inner[2] - UiW_HintsWidth(&hang, 1, 0) - 2, inner[1] + inner[3] - 14, &hang, 1, false, 0);
        UiG_Hit(HIT_MESSAGE, 0, 24, 252, 592, 88);
        return;
    }
    {
        struct UiHintItem list[] = {{UI_ACT_CONFIRM, sText_Menu}, {UI_ACT_CANCEL, sText_Back}};
        struct UiHintItem menu[] = {{UI_ACT_CONFIRM, sText_Ok}, {UI_ACT_CANCEL, sText_Cancel}};
        struct UiHintItem check[] = {{UI_ACT_SCROLL, sText_Scroll}, {UI_ACT_CANCEL, sText_Back}};

        if (sP.mcState == NATIVE_PNAV_MC_OPTIONS)
            Hints(menu, 2);
        else if (sP.mcState == NATIVE_PNAV_MC_CHECK)
            Hints(check, 2);
        else
            Hints(list, 2);
    }
}

/* ---------------------------------------------------------------- CONDITION and a search's results */

static const char *const sCondLabels[5] = {"COOL", "BEAUTY", "CUTE", "SMART", "TOUGH"};
static const UiRGBA sSheen[3] = {UI_HEX(0xC08018), UI_HEX(0xF8C840), UI_HEX(0xFFFFF0)};   /* the sheen's sparkles */

/* a ribbon's rosette, small (the ribbon list's counts) */
static void Rosette(int x, int y)
{
    static const char *const rows[] = {"..aaa..", ".abbba.", "abbcbba", "abcccba", "abbcbba", ".abbba.", "..aaa..",
                                       ".dd.dd.", ".dd.dd.", "dd...dd"};
    static const struct UiPixPal pal[4] = {{'a', UI_HEX(0xB07010)}, {'b', UI_HEX(0xF0C030)}, {'c', UI_HEX(0xFFF0A0)},
                                           {'d', UI_HEX(0xD04848)}};

    UiW_Pix("pnrosette", rows, 10, pal, 4, x, y, false);
}

/* a Pokemon's row: its icon, name, level and gender (a search's rank before them, a ribbon count after) */
static void MonRow(int t, const struct NativePokenavRow *r, int x, int y, int w, int h, bool on, int rank, int ribbons)
{
    struct UiRamp b = BlueRamp();
    int nx = x + 40;

    if (on)
    {
        const struct UiMask *rm = UiM_Rounded4(w, h - 2, 2, 2, 2, 2);

        UiW_SelectFrame(rm, x, y, UI_NONE, UI_NONE, 2);
        UiG_Paint(rm, x, y, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.7),
                  &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UiMix(PNAV_CYAN, b.lo, 0.3), 0, 0, NULL, UI_NONE});
    }
    if (rank > 0)
    {
        char num[8];

        snprintf(num, sizeof(num), "%d", rank);
        UiCaps_Draw(x + 16, y + (h >> 1) - 4, num, b.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        x += 18;
        nx += 18;
    }
    if (r->species != SPECIES_NONE)
    {
        UiG_Save();
        UiG_Clip(x + 2, y, 34, h - 2);
        UiGfx_MonIcon(r->species, r->personality, r->egg, on ? (t / 10) % 2 : 0, x + 2, y + (h >> 1) - 20 - (on ? 1 : 0), NULL);
        UiG_Restore();
    }
    UiText_Draw(UI_FONT_NORMAL, nx, y + (h >> 1) - 8, r->egg ? sText_Egg : r->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    if (!r->egg)
    {
        u8 lv[16];

        UiMon_LevelText(lv, sizeof(lv), r->level);
        UiText_Draw(UI_FONT_SMALL, x + w - 34, y + (h >> 1) - 6, lv, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        UiW_Gender(x + w - 46, y + (h >> 1) - 5, r->gender == MON_MALE ? 0 : r->gender == MON_FEMALE ? 1 : 2);
    }
    if (ribbons > 0)
    {
        char num[8];

        snprintf(num, sizeof(num), "%d", ribbons);
        Rosette(x + w - 84, y + (h >> 1) - 5);
        UiCaps_Draw(x + w - 74, y + (h >> 1) - 3, num, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
}

/* the condition graph: the five conditions' pentagon, the Pokemon's own inside it */
static void Graph(int cx, int cy, int radius, const u8 *values)
{
    struct UiRamp b = BlueRamp();
    double px[5], py[5], vx[5], vy[5];
    int minY = cy, maxY = cy;

    for (int i = 0; i < 5; i++)
    {
        double a = (-90 + 72 * i) * 3.14159265358979 / 180.0;
        double v = values[i] / 255.0;

        px[i] = cx + cos(a) * radius;
        py[i] = cy + sin(a) * radius;
        vx[i] = cx + cos(a) * radius * (0.08 + 0.92 * v);
        vy[i] = cy + sin(a) * radius * (0.08 + 0.92 * v);
        if (vy[i] < minY)
            minY = (int)floor(vy[i]);
        if (vy[i] > maxY)
            maxY = (int)ceil(vy[i]);
    }
    /* the rings and the axes */
    for (int ring = 1; ring <= 4; ring++)
        for (int i = 0; i < 5; i++)
        {
            double ax = cx + (px[i] - cx) * ring / 4, ay = cy + (py[i] - cy) * ring / 4;
            double bx = cx + (px[(i + 1) % 5] - cx) * ring / 4, by = cy + (py[(i + 1) % 5] - cy) * ring / 4;
            int n = (int)ceil(fmax(fabs(bx - ax), fabs(by - ay)));

            for (int s = 0; s <= n; s += ring == 4 ? 1 : 2)
                UiG_Put((int)floor(ax + (bx - ax) * s / (n ? n : 1) + 0.5), (int)floor(ay + (by - ay) * s / (n ? n : 1) + 0.5),
                        ring == 4 ? b.ink : UiMix(PNAV_CYAN, b.ink, 0.35));
        }
    for (int i = 0; i < 5; i++)
    {
        int n = (int)ceil(fmax(fabs(px[i] - cx), fabs(py[i] - cy)));

        for (int s = 0; s <= n; s += 2)
            UiG_Put((int)floor(cx + (px[i] - cx) * s / n + 0.5), (int)floor(cy + (py[i] - cy) * s / n + 0.5),
                    UiMix(PNAV_CYAN, b.ink, 0.35));
    }
    /* the Pokemon's: filled row by row */
    for (int y = minY; y <= maxY; y++)
    {
        double xs[5];
        int n = 0;

        for (int i = 0; i < 5; i++)
        {
            double y0 = vy[i], y1 = vy[(i + 1) % 5];

            if ((y + 0.5 >= fmin(y0, y1)) && (y + 0.5 < fmax(y0, y1)))
                xs[n++] = vx[i] + (y + 0.5 - y0) * (vx[(i + 1) % 5] - vx[i]) / (y1 - y0);
        }
        if (n >= 2)
        {
            double l = fmin(xs[0], xs[1]), r = fmax(xs[0], xs[1]);

            UiG_HLine((int)floor(l + 0.5), y, (int)floor(r + 0.5) - (int)floor(l + 0.5) + 1, UiWithAlpha(UI_HEX(0xF07090), 178));
        }
    }
    for (int i = 0; i < 5; i++)
    {
        double bx = vx[(i + 1) % 5], by = vy[(i + 1) % 5];
        int n = (int)ceil(fmax(fabs(bx - vx[i]), fabs(by - vy[i])));

        for (int s = 0; s <= n; s++)
            UiG_Put((int)floor(vx[i] + (bx - vx[i]) * s / (n ? n : 1) + 0.5),
                    (int)floor(vy[i] + (by - vy[i]) * s / (n ? n : 1) + 0.5), UI_HEX(0xC03868));
    }
    for (int i = 0; i < 5; i++)
    {
        double lx = cx + (px[i] - cx) * 1.28, ly = cy + (py[i] - cy) * 1.24;

        UiCaps_Draw((int)lx, (int)ly - 3, sCondLabels[i], UiMix(UI_HEX(sContestHex[i]), b.ink, 0.25), UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    }
}

/* the markings menu over the device: the four marks, OK and CANCEL, the game's cursor */
static void MarkingsMenu(int t, int x, int y)
{
    struct UiRamp b = BlueRamp();
    int w = 112, h = 4 * 18 + 2 * 18 + 12;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    for (int i = 0; i < 6; i++)
    {
        int ry = y + 6 + i * 18;
        bool on = i == sP.condMarkCursor;

        if (on)
        {
            UiG_Paint(UiM_Rounded4(w - 10, 16, 2, 2, 2, 2), x + 5, ry, UI_HEX(0xD8F0F8),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UI_HEX(0xA8D8E8), 0, 0, NULL, UI_NONE});
            UiW_Cursor(x + 8, ry + 4, UiW_Nudge(t), b.ink);
        }
        if (i < 4)
        {
            /* the game's marks: circle, square, triangle, heart (bit i) */
            bool set = (sP.condMarkEdit >> i) & 1;

            UiMon_MarkGlyph(i, x + 24, ry + 5, set ? b.ink : UiMix(UI_P_CREAM_LO, b.ink, 0.25));
            UiCaps_Draw(x + 40, ry + 5, set ? "ON" : "OFF", set ? b.ink : UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_LEFT, UI_NONE);
        }
        else
        {
            UiCaps_Draw(x + 24, ry + 5, i == 4 ? "OK" : "CANCEL", on ? b.ink : UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_LEFT, UI_NONE);
        }
        UiG_Hit(HIT_MARK, i, x + 5, ry, w - 10, 16);
    }
}

static void MonList(int t, int x, int y, int w, int rowH, bool ranks)
{
    struct UiRamp b = BlueRamp();
    bool list = sP.page == NATIVE_PNAV_SEARCH || sP.page == NATIVE_PNAV_RIBBON_LIST;   /* the game's list */
    int shown = list ? (sP.listShown > 0 ? sP.listShown : 7) : NATIVE_PNAV_ROWS;
    int rows = sP.rowCount < shown ? sP.rowCount : shown;
    bool cancelRow = sP.page == NATIVE_PNAV_CONDITION && !sP.condSearch;
    int panelRows = NATIVE_PNAV_ROWS;   /* the game's eight; the list stands as tall as the screen beside it */

    UiG_Paint(UiM_Rounded4(w, rowH * panelRows + 12, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int i = 0; i < rows && i < panelRows; i++)
    {
        int index = sP.listTop + i;

        MonRow(t, &sP.rows[i], x + 6, y + 6 + i * rowH, w - 12, rowH, index == sP.listCursor,
               ranks ? sP.rows[i].value : 0, sP.page == NATIVE_PNAV_RIBBON_LIST ? sP.rows[i].value : 0);
        UiG_Hit(HIT_MON_ROW, index, x + 6, y + 6 + i * rowH, w - 12, rowH - 2);
    }
    if (cancelRow && rows < panelRows)
    {
        int ry = y + 6 + rows * rowH;
        bool on = sP.condCancel;

        if (on)
        {
            const struct UiMask *rm = UiM_Rounded4(w - 12, rowH - 2, 2, 2, 2, 2);

            UiW_SelectFrame(rm, x + 6, ry, UI_NONE, UI_NONE, 2);
            UiG_Paint(rm, x + 6, ry, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.7),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UiMix(PNAV_CYAN, b.lo, 0.3), 0, 0, NULL, UI_NONE});
        }
        UiGfx_Icon(on ? "return_blue" : "return", x + 14, ry + ((rowH - 2 - 14) >> 1), 0);
        UiText_Draw(UI_FONT_NORMAL, x + 46, ry + (rowH >> 1) - 8, sText_Cancel, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiG_Hit(HIT_MON_ROW, sP.listCount - 1, x + 6, ry, w - 12, rowH - 2);
    }
    if (list && sP.listCount > shown)
        UiW_Scrollbar(x + w + 4, y + 4, rowH * panelRows + 4, sP.listTop, sP.listCount, shown, &b);
    else if (sP.page == NATIVE_PNAV_CONDITION && sP.condSearch && sP.listCount > panelRows)
        UiW_Scrollbar(x + w + 4, y + 4, rowH * panelRows + 4, sP.listTop, sP.listCount, panelRows, &b);
}

static void Condition(int t)
{
    struct UiRamp b = BlueRamp();
    int lx = 24, ly = 58, lw = 276, rowH = 26;
    int sx = 324, sy = 58, sw = 292, sh = NATIVE_PNAV_ROWS * 26 + 12;

    Title("CONDITION");
    MonList(t, lx, ly, lw, rowH, sP.condSearch);
    Screen(sx, sy, sw, sh);
    if (sP.condCancel || sP.cond.species == SPECIES_NONE)
    {
        UiG_Save();
        UiG_Translate(sx + (sw >> 1) - 24, sy + 60);
        UiG_Paint(UiM_Circle(48), 0, 0, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.8),
                  &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiGfx_Icon("return_blue", 16, 17, 0);
        UiG_Restore();
        UiText_Draw(UI_FONT_NORMAL, sx + (sw >> 1), sy + 124, sText_ReturnCondition, b.ink,
                    UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3), UI_ALIGN_CENTER, 0);
    }
    else
    {
        u8 lv[16];
        int nx = sx + 92;

        /* the Pokemon: its picture, name, level, where it is, its markings and sheen */
        UiG_Paint(UiM_Rounded4(76, 76, 3, 3, 3, 3), sx + 8, sy + 8, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.85),
                  &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (sMonPicReady)
        {
            UiG_Save();
            UiG_Clip(sx + 9, sy + 9, 74, 74);
            UiGfx_MonPic(sP.cond.species, sP.cond.personality, sP.condShiny, sP.cond.egg, 0, sx + 46, sy + 80, NULL);
            UiG_Restore();
        }
        UiText_Draw(UI_FONT_NORMAL, nx, sy + 10, sP.cond.egg ? sText_Egg : sP.cond.name, b.ink,
                    UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3), UI_ALIGN_LEFT, 0);
        if (!sP.cond.egg)
        {
            UiMon_LevelText(lv, sizeof(lv), sP.cond.level);
            UiText_Draw(UI_FONT_SMALL, nx, sy + 28, lv, b.ink, UI_NONE, UI_ALIGN_LEFT, 0);
            UiW_Gender(nx + 34, sy + 29, sP.cond.gender == MON_MALE ? 0 : sP.cond.gender == MON_FEMALE ? 1 : 2);
        }
        if (sP.condShiny)
            UiMon_Star(nx + 50, sy + 29);
        UiText_Draw(UI_FONT_SMALL, nx, sy + 42, sWhere, UiMix(b.ink, PNAV_CYAN, 0.25), UI_NONE, UI_ALIGN_LEFT, 0);
        UiMon_Markings(nx, sy + 58, sP.condMarks, b.ink, UiMix(PNAV_CYAN, b.ink, 0.25));
        UiCaps_Draw(sx + sw - 12, sy + 10, "SHEEN", UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT,
                    UI_NONE);
        for (int i = 0; i < 10; i++)
        {
            int kx = sx + sw - 16 - (i % 5) * 10, ky = sy + 26 + (i / 5) * 10;

            if (i < sP.condSparkles)
                UiMon_Sparkle(kx, ky, false, sSheen);
            else
                UiG_Put(kx, ky, UiMix(PNAV_CYAN, b.ink, 0.3));
        }
        Graph(sx + (sw >> 1), sy + 152, 52, sP.condValues);
    }
    if (sP.condMarking)
        MarkingsMenu(t, sx + sw - 124, sy + 92);
    {
        struct UiHintItem party[] = {{UI_ACT_SCROLL, sText_Pokemon}, {UI_ACT_CANCEL, sText_Back}};
        struct UiHintItem partyCancel[] = {{UI_ACT_CONFIRM, sText_Back}, {UI_ACT_SCROLL, sText_Pokemon}};
        struct UiHintItem search[] = {{UI_ACT_CONFIRM, sText_Markings}, {UI_ACT_SCROLL, sText_Pokemon},
                                      {UI_ACT_CANCEL, sText_Back}};
        struct UiHintItem marking[] = {{UI_ACT_CONFIRM, sText_Select}, {UI_ACT_CANCEL, sText_Cancel}};

        if (sP.condMarking)
            Hints(marking, 2);
        else if (sP.condSearch)
            Hints(search, 3);
        else if (sP.condCancel)
            Hints(partyCancel, 2);
        else
            Hints(party, 2);
    }
}

static void Search(int t)
{
    struct UiRamp b = BlueRamp();
    char title[32];
    int lx = 24, ly = 58, lw = 276, rowH = 26;
    int sx = 324, sy = 58, sw = 292, sh = NATIVE_PNAV_ROWS * 26 + 12;
    int i = sP.listCursor - sP.listTop;

    snprintf(title, sizeof(title), "%s SEARCH", sCondLabels[sP.searchId < 5 ? sP.searchId : 0]);
    Title(title);
    MonList(t, lx, ly, lw, rowH, true);
    Screen(sx, sy, sw, sh);
    if (i >= 0 && i < sP.rowCount)
    {
        const struct NativePokenavRow *r = &sP.rows[i];
        char rank[24];

        UiG_Paint(UiM_Circle(120), sx + (sw >> 1) - 60, sy + 20, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.8),
                  &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (sMonPicReady)
            UiGfx_MonPic(r->species, r->personality, r->shiny, r->egg, 0, sx + (sw >> 1), sy + 128, NULL);
        snprintf(rank, sizeof(rank), "RANK %d OF %d", r->value, sP.listCount);
        UiCaps_Draw(sx + (sw >> 1), sy + 150, rank, UiMix(UI_HEX(sContestHex[sP.searchId < 5 ? sP.searchId : 0]), b.ink, 0.3),
                    UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        UiText_Draw(UI_FONT_NORMAL, sx + (sw >> 1), sy + 164, r->egg ? sText_Egg : r->name, b.ink,
                    UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3), UI_ALIGN_CENTER, 0);
    }
    {
        struct UiHintItem items[] = {{UI_ACT_CONFIRM, sText_Condition}, {UI_ACT_SCROLL, sText_Pokemon},
                                     {UI_ACT_CANCEL, sText_Back}};

        Hints(items, 3);
    }
}

/* ---------------------------------------------------------------- RIBBONS */

static u8 *sRibbonArt;                    /* the game's big ribbon icons, decompressed on first use */

struct RibbonBuild
{
    const u8 *tiles;
    const u16 *palette;
};

static void BuildRibbon(struct UiBitmap *bm, const void *ctx)
{
    const struct RibbonBuild *r = ctx;

    UiGfx_Decode4bpp(bm->d, bm->w, r->tiles, r->palette, 32, 32);
}

/* a ribbon: the game's own 32x32 icon (scale 2: the chosen one, on the screen) */
static void RibbonIcon(u32 ribbonId, int x, int y, int scale)
{
    struct RibbonBuild r;
    const u32 *art;
    u32 index;
    char key[24];

    art = PokenavRibbons_NativeUiIconArt(ribbonId, &index, &r.palette);
    if (art == NULL || index >= 12)
        return;
    if (sRibbonArt == NULL)
    {
        u32 size = GetDecompressedDataSize(art);

        if (size < 12 * 512 || (sRibbonArt = calloc(size + 64, 1)) == NULL)
            return;
        DecompressDataWithHeaderWram(art, sRibbonArt);
    }
    r.tiles = sRibbonArt + index * 512;
    snprintf(key, sizeof(key), "pnrib:%u", (unsigned)ribbonId);
    UiG_BlitPart(UiG_Cached(key, 32, 32, BuildRibbon, &r), 0, 0, 32, 32, x, y, scale, false, 1.0);
}

/* the case's ribbon at a position (0: none there) */
static int RibbonAt(int pos, bool *gift)
{
    *gift = pos >= sP.ribbonGiftStart;
    if (!*gift)
        return pos < sP.ribbonNormal ? sP.ribbonIds[pos] + 1 : 0;
    return pos - sP.ribbonGiftStart < sP.ribbonGift ? sP.giftIds[pos - sP.ribbonGiftStart] + 1 : 0;
}

static void RibbonList(int t)
{
    struct UiRamp b = BlueRamp();
    int lx = 24, ly = 58, lw = 276, rowH = 26;
    int sx = 324, sy = 58, sw = 292, sh = NATIVE_PNAV_ROWS * 26 + 12;
    int i = sP.listCursor - sP.listTop;

    Title("RIBBONS");
    MonList(t, lx, ly, lw, rowH, false);
    Screen(sx, sy, sw, sh);
    if (i >= 0 && i < sP.rowCount)
    {
        const struct NativePokenavRow *r = &sP.rows[i];
        char line[24];

        snprintf(line, sizeof(line), "%d / %d", sP.listCursor + 1, sP.listCount);
        UiCaps_Draw(sx + sw - 12, sy + 10, line, UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT,
                    UI_NONE);
        UiG_Paint(UiM_Circle(120), sx + (sw >> 1) - 60, sy + 20, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.8),
                  &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (sMonPicReady)
            UiGfx_MonPic(r->species, r->personality, r->shiny, r->egg, 0, sx + (sw >> 1), sy + 128, NULL);
        UiText_Draw(UI_FONT_NORMAL, sx + (sw >> 1), sy + 148, r->name, b.ink, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3),
                    UI_ALIGN_CENTER, 0);
        snprintf(line, sizeof(line), "RIBBONS %d", r->value);
        Rosette(sx + (sw >> 1) - 36, sy + 170);
        UiCaps_Draw(sx + (sw >> 1) - 24, sy + 172, line, b.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    {
        struct UiHintItem items[] = {{UI_ACT_CONFIRM, sText_Ribbons}, {UI_ACT_SCROLL, sText_Pokemon},
                                     {UI_ACT_CANCEL, sText_Back}};

        Hints(items, 3);
    }
}

/* a Pokemon's ribbons: its card, the chosen ribbon on the screen, the case (the game's rows, its gift row apart) */
static void Ribbons(int t)
{
    struct UiRamp b = BlueRamp();
    int cx = 24, cy = 58, cw = 276, ch = 84;
    int sx = 324, sy = 58, sw = 292, sh = 84;
    int kx = 24, ky = 150, kw = 592, kh = 144;
    int per = sP.ribbonPerRow > 0 ? sP.ribbonPerRow : 9, pitch = 64, x0 = kx + ((kw - per * pitch) >> 1);
    int giftRow = sP.ribbonGiftStart / per;
    char line[32];
    bool gift;
    int chosen = RibbonAt(sP.ribbonPos, &gift);

    Title("RIBBONS");
    /* the Pokemon: its picture, name, level, where it is, its ribbons; which of the list it is */
    UiG_Paint(UiM_Rounded4(cw, ch, 4, 4, 4, 4), cx, cy, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(72, 72, 3, 3, 3, 3), cx + 6, cy + 6, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.85),
              &(struct UiPaint){UiMix(b.lo, PNAV_CYAN, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    if (sMonPicReady)
    {
        UiG_Save();
        UiG_Clip(cx + 7, cy + 7, 70, 70);
        UiGfx_MonPic(sP.cond.species, sP.cond.personality, sP.condShiny, sP.cond.egg, 0, cx + 42, cy + 76, NULL);
        UiG_Restore();
    }
    UiText_Draw(UI_FONT_NORMAL, cx + 88, cy + 8, sP.cond.name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    {
        u8 lv[16];

        UiMon_LevelText(lv, sizeof(lv), sP.cond.level);
        UiText_Draw(UI_FONT_SMALL, cx + 88, cy + 27, lv, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        UiW_Gender(cx + 122, cy + 28, sP.cond.gender == MON_MALE ? 0 : sP.cond.gender == MON_FEMALE ? 1 : 2);
        if (sP.condShiny)
            UiMon_Star(cx + 138, cy + 28);
    }
    UiText_Draw(UI_FONT_SMALL, cx + 88, cy + 41, sWhere, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    snprintf(line, sizeof(line), "RIBBONS %d", sP.ribbonTotal);
    Rosette(cx + 88, cy + 60);
    UiCaps_Draw(cx + 100, cy + 62, line, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(line, sizeof(line), "%d / %d", sP.listCursor + 1, sP.listCount);
    UiCaps_Draw(cx + cw - 12, cy + 10, line, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    if (sP.listCount > 1 && !sP.ribbonExpanded)
    {
        /* the game's UP and DOWN: the list's Pokemon before and after */
        if (sP.listCursor > 0)
        {
            UiW_DownArrow(cx + cw - 22, cy + 30 - ((t / 16) % 2), UI_P_TEXT, true);
            UiG_Hit(HIT_MON_STEP, 0, cx + cw - 30, cy + 24, 24, 18);
        }
        if (sP.listCursor + 1 < sP.listCount)
        {
            UiW_DownArrow(cx + cw - 22, cy + 62 + ((t / 16) % 2), UI_P_TEXT, false);
            UiG_Hit(HIT_MON_STEP, 1, cx + cw - 30, cy + 56, 24, 18);
        }
    }
    /* the chosen ribbon, larger, and its words (the case's count until one is chosen) */
    Screen(sx, sy, sw, sh);
    if (sP.ribbonExpanded && chosen > 0)
    {
        RibbonIcon(chosen - 1, sx + 10, sy + 10, 2);
        for (int k = 0; k < 2; k++)
            if (sP.ribbonDesc[k] != NULL)
                UiText_Draw(UI_FONT_NORMAL, sx + 84, sy + 20 + k * 20, sP.ribbonDesc[k], b.ink,
                            UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.3), UI_ALIGN_LEFT, 0);
    }
    else
    {
        snprintf(line, sizeof(line), "RIBBONS %d", sP.ribbonTotal);
        UiCaps_Draw(sx + (sw >> 1), sy + 26, line, b.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        UiCaps_Draw(sx + (sw >> 1), sy + 48, "CHOOSE ONE TO READ ABOUT IT", UiMix(b.ink, PNAV_CYAN, 0.35), UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    }
    /* the case: its slots, the ribbons in them, the cursor while one is chosen */
    UiG_Paint(UiM_Rounded4(kw, kh, 4, 4, 4, 4), kx, ky, UI_P_CREAM,
              &(struct UiPaint){b.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_HLine(kx + 10, ky + 6 + giftRow * 34 - 2, kw - 20, UiMix(UI_P_CREAM_LO, b.ink, 0.2));
    for (int pos = 0; pos < sP.ribbonGiftStart + NUM_GIFT_RIBBONS; pos++)
    {
        int row = pos / per, col = pos % per;
        int px = x0 + col * pitch + ((pitch - 40) >> 1), py = ky + 6 + row * 34;
        int ribbon;
        bool on = sP.ribbonExpanded && pos == sP.ribbonPos;
        const struct UiMask *slot = UiM_Rounded4(40, 32, 3, 3, 3, 3);

        if (pos >= FIRST_GIFT_RIBBON && pos < sP.ribbonGiftStart)
            continue;   /* the last normal row's end: the game has no slots there */
        ribbon = RibbonAt(pos, &gift);
        if (on)
        {
            UiW_SelectFrame(slot, px, py, UI_NONE, UI_NONE, 2);
            UiG_Paint(slot, px, py, UiMix(PNAV_CYAN, UI_HEX(0xFFFFFF), 0.7),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFFF), UiMix(PNAV_CYAN, b.lo, 0.3), 0, 0, NULL, UI_NONE});
        }
        else
        {
            UiG_Mask(slot, px, py, UiMix(UI_P_CREAM, UI_P_CREAM_LO, ribbon > 0 ? 0.35 : 0.6));
        }
        if (ribbon > 0)
        {
            RibbonIcon(ribbon - 1, px + 4, py + (on && (t / 16) % 2 ? -1 : 0), 1);
            UiG_Hit(HIT_RIBBON, pos, px, py, 40, 32);
        }
    }
    {
        struct UiHintItem browse[] = {{UI_ACT_CONFIRM, sText_Ribbon}, {UI_ACT_SCROLL, sText_Pokemon},
                                      {UI_ACT_CANCEL, sText_Back}};
        struct UiHintItem chosenHints[] = {{UI_ACT_MOVE, sText_Ribbon}, {UI_ACT_CANCEL, sText_Back}};

        if (sP.ribbonExpanded)
            Hints(chosenHints, 2);
        else if (sP.listCount > 1)
            Hints(browse, 3);
        else
        {
            browse[1] = browse[2];
            Hints(browse, 2);
        }
    }
}

/* ---------------------------------------------------------------- the screen */

static void Draw(int t)
{
    Background();
    switch (sP.page)
    {
    case NATIVE_PNAV_MENU:
        Menu(t);
        break;
    case NATIVE_PNAV_MATCH_CALL:
        MatchCall(t);
        break;
    case NATIVE_PNAV_CONDITION:
        Condition(t);
        break;
    case NATIVE_PNAV_SEARCH:
        Search(t);
        break;
    case NATIVE_PNAV_RIBBON_LIST:
        RibbonList(t);
        break;
    case NATIVE_PNAV_RIBBONS:
        Ribbons(t);
        break;
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* the game's ribbon cursor (pokenav_ribbons_summary.c TrySelectRibbon*): where a press takes it */
static int RibbonMove(int pos, uint16_t key)
{
    int per = sP.ribbonPerRow > 0 ? sP.ribbonPerRow : 9, normal = sP.ribbonNormal, gifts = sP.ribbonGift;
    int lastRowStart = normal > 0 ? ((normal - 1) / per) * per : 0, col = pos % per;

    switch (key)
    {
    case DPAD_UP:
        if (pos < FIRST_GIFT_RIBBON)
            return pos < per ? pos : pos - per;
        if (normal == 0)
            return pos;
        pos = pos - sP.ribbonGiftStart + lastRowStart;
        return pos >= normal ? normal - 1 : pos;
    case DPAD_DOWN:
        if (pos >= FIRST_GIFT_RIBBON)
            return pos;
        if (pos < lastRowStart)
            return pos + per >= normal ? normal - 1 : pos + per;
        if (gifts == 0)
            return pos;
        return (pos - lastRowStart >= gifts ? gifts - 1 : pos - lastRowStart) + sP.ribbonGiftStart;
    case DPAD_LEFT:
        return col != 0 ? pos - 1 : pos;
    case DPAD_RIGHT:
        if (col >= per - 1)
            return pos;
        if (pos < sP.ribbonGiftStart)
            return pos < normal - 1 ? pos + 1 : pos;
        return col < gifts - 1 ? pos + 1 : pos;
    }
    return pos;
}

/* the press that takes the ribbon cursor toward a position: rows first, then along the row (0: there, or stuck) */
static uint16_t RibbonKey(int from, int to)
{
    int per = sP.ribbonPerRow > 0 ? sP.ribbonPerRow : 9;
    int fromRow = from >= FIRST_GIFT_RIBBON ? 100 : from / per, toRow = to >= FIRST_GIFT_RIBBON ? 100 : to / per;
    uint16_t key;

    if (toRow != fromRow)
        key = toRow > fromRow ? DPAD_DOWN : DPAD_UP;
    else if (to % per != from % per)
        key = to % per > from % per ? DPAD_RIGHT : DPAD_LEFT;
    else
        return 0;
    return RibbonMove(from, key) != from ? key : 0;
}

/* the goal's next press, once the game reads buttons again */
static void StepGoal(void)
{
    int cursor = 0, count = 0, moves;
    uint16_t key = 0;

    if (sGoal.kind == GOAL_NONE)
        return;
    if (sP.page != sGoal.page || sGoal.presses > 48
     || (sGoal.kind == GOAL_LIST && sP.page == NATIVE_PNAV_MATCH_CALL && sP.mcState != NATIVE_PNAV_MC_LIST)
     || (sGoal.kind == GOAL_OPTION && sP.mcState != NATIVE_PNAV_MC_OPTIONS)
     || (sGoal.kind == GOAL_MARK && !sP.condMarking))
    {
        sGoal.kind = GOAL_NONE;
        return;
    }
    if (sP.busy || NativeUi_PressesPending() || (sGoal.kind == GOAL_MENU && !sP.menuInput))
        return;
    switch (sGoal.kind)
    {
    case GOAL_MENU:
        cursor = sP.menuCursor;
        count = sP.menuCount;
        break;
    case GOAL_LIST:
        cursor = sP.listCursor;
        break;
    case GOAL_OPTION:
        cursor = sP.mcOptionCursor;
        break;
    case GOAL_MARK:
        cursor = sP.condMarkCursor;
        count = 6;
        break;
    case GOAL_RIBBON:
        /* A chooses a ribbon first; then the cursor moves over the case */
        if (!sP.ribbonExpanded)
            key = A_BUTTON;
        else if ((key = RibbonKey(sP.ribbonPos, sGoal.target)) == 0)
        {
            sGoal.kind = GOAL_NONE;
            return;
        }
        NativeUi_QueuePress(key);
        sGoal.presses++;
        return;
    }
    moves = sGoal.target - cursor;
    /* a menu that wraps: the shorter way round */
    if (count > 0 && moves > count / 2)
        moves -= count;
    else if (count > 0 && moves < -count / 2)
        moves += count;
    if (moves == 0)
    {
        if (sGoal.thenA)
            NativeUi_QueuePress(A_BUTTON);
        sGoal.kind = GOAL_NONE;
        return;
    }
    NativeUi_QueuePress(moves > 0 ? DPAD_DOWN : DPAD_UP);
    sGoal.presses++;
}

static void SetGoal(int kind, int target, bool thenA)
{
    sGoal.kind = kind;
    sGoal.page = sP.page;
    sGoal.target = target;
    sGoal.thenA = thenA;
    sGoal.presses = 0;
    StepGoal();
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        sGoal.kind = GOAL_NONE;
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || NativeUi_PressesPending() || sP.busy || sGoal.kind != GOAL_NONE)
        return button == 1;
    if (sP.page == NATIVE_PNAV_MENU && hit->id == HIT_MENU_ITEM && sP.menuInput)
    {
        SetGoal(GOAL_MENU, hit->index, true);
        return true;
    }
    if (sP.page == NATIVE_PNAV_MATCH_CALL)
    {
        if (hit->id == HIT_LIST_ROW && sP.mcState == NATIVE_PNAV_MC_LIST)
        {
            SetGoal(GOAL_LIST, hit->index, hit->index == sP.listCursor);
            return true;
        }
        if (hit->id == HIT_OPTION && sP.mcState == NATIVE_PNAV_MC_OPTIONS)
        {
            SetGoal(GOAL_OPTION, hit->index, true);
            return true;
        }
        if (hit->id == HIT_MESSAGE && sP.mcState == NATIVE_PNAV_MC_CALL && (sMsgWaiting || (!sMsgPrinting && !sP.busy)))
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
    }
    if (sP.page == NATIVE_PNAV_CONDITION && sP.condMarking && hit->id == HIT_MARK)
    {
        SetGoal(GOAL_MARK, hit->index, true);
        return true;
    }
    if (((sP.page == NATIVE_PNAV_CONDITION && !sP.condMarking) || sP.page == NATIVE_PNAV_SEARCH
         || sP.page == NATIVE_PNAV_RIBBON_LIST) && hit->id == HIT_MON_ROW)
    {
        /* the cursor to the row; on it, what A does there (the party's condition page reads A only on CANCEL) */
        SetGoal(GOAL_LIST, hit->index,
                hit->index == sP.listCursor && (sP.page != NATIVE_PNAV_CONDITION || sP.condSearch || sP.condCancel));
        return true;
    }
    if (sP.page == NATIVE_PNAV_RIBBONS)
    {
        if (hit->id == HIT_RIBBON)
        {
            SetGoal(GOAL_RIBBON, hit->index, false);
            return true;
        }
        if (hit->id == HIT_MON_STEP && !sP.ribbonExpanded)
        {
            NativeUi_QueuePress(hit->index == 0 ? DPAD_UP : DPAD_DOWN);
            return true;
        }
    }
    return false;
}

static bool Ready(void)
{
    if (sP.page == NATIVE_PNAV_MATCH_CALL && sP.mcState == NATIVE_PNAV_MC_CALL && sMsgWaiting)
        return true;    /* the calling dots' page break reads A itself */
    return !sP.busy && (sP.page != NATIVE_PNAV_MENU || sP.menuInput);
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Pokenav = {
    .name = "pokenav",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
