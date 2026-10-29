#ifdef ENABLE_NATIVE_UI
/*
 * 13 · PokéNav (step 13 of the owner's plan, 2026-10-29: the PokéNav in the design's look): the PokéNav's pages over
 * the game's own PokéNav (pokenav*.c), which keeps running under them and keeps every rule: its pages and what each
 * button does, its cursors and lists, calls, markings, flying from the map. The UI reads the page up at the frame
 * boundary (pokenav.c Pokenav_NativeUiState), draws it on the design's device and turns clicks into the game's own
 * button presses. Drawn here: the menus (the main menu, CONDITION's and its search's) and MATCH CALL (the trainers
 * registered, a trainer's menu and check page, a call). A page not drawn here keeps the game's picture.
 * Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "pokenav.h"
#include "region_map.h"
#include "string_util.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_MENU_ITEM = UI_HIT_SCREEN,
    HIT_LIST_ROW,
    HIT_OPTION,
    HIT_MESSAGE,
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

static void Title(const char *label);
static void Hints(const struct UiHintItem *items, int count);

static bool PageDrawn(const struct NativePokenav *p)
{
    return p->page == NATIVE_PNAV_MENU || p->page == NATIVE_PNAV_MATCH_CALL;
}

static bool Detect(void)
{
    struct NativePokenav p;

    return Pokenav_NativeUiState(&p) && PageDrawn(&p);
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
            UiW_Cursor(rx + w - 18, ry + ((rowH - 6 - 9) >> 1), t / 8, b.ink);
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
        UiText_Draw(UI_FONT_SMALL, x + 40, ry + 1, row->sub, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, x + 40, ry + rowH - 19, row->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
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
            UiW_Cursor(x + 9, ry + 5, t / 8, b.ink);
        UiCaps_Draw(x + 24, ry + 6, OptionLabel(sP.mcOptions[i]), on ? b.ink : UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_OPTION, i, x + 5, ry, w - 10, rowH - 2);
    }
}

static void MatchCall(int t)
{
    int lx = 24, ly = 58, lw = 276, rowH = 34;
    int sx = 324, sy = 58, sw = 292, sh = 222;

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
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || NativeUi_PressesPending() || sP.busy)
        return button == 1;
    if (sP.page == NATIVE_PNAV_MENU && hit->id == HIT_MENU_ITEM && sP.menuInput)
    {
        int moves = hit->index - sP.menuCursor;

        /* the game's menu wraps: the shorter way round */
        if (moves > sP.menuCount / 2)
            moves -= sP.menuCount;
        else if (moves < -sP.menuCount / 2)
            moves += sP.menuCount;
        if (moves != 0)
            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (sP.page == NATIVE_PNAV_MATCH_CALL)
    {
        if (hit->id == HIT_LIST_ROW && sP.mcState == NATIVE_PNAV_MC_LIST)
        {
            int moves = hit->index - sP.listCursor;

            if (moves != 0)
                NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            else
                NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        if (hit->id == HIT_OPTION && sP.mcState == NATIVE_PNAV_MC_OPTIONS)
        {
            int moves = hit->index - sP.mcOptionCursor;

            if (moves != 0)
                NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        if (hit->id == HIT_MESSAGE && sP.mcState == NATIVE_PNAV_MC_CALL && (sMsgWaiting || (!sMsgPrinting && !sP.busy)))
        {
            NativeUi_QueuePress(A_BUTTON);
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
