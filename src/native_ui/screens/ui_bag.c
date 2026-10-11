#ifdef ENABLE_NATIVE_UI
/*
 * 2 · Bag (the approved design's preview/js/screens/bag.js): the leather bag
 * with the stitched information pocket, the five pocket tabs and the item list,
 * over the engine's bag (item_menu.c), which keeps every rule: what each item
 * offers (use, give, toss, register, check tag...), the quantities, moving an
 * item (SELECT), sorting (START), selling and depositing, the battle bag. The
 * design's FIND box and favourites are not in the game: FIND is marked
 * PLANNED, favourites are left out. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "item_menu.h"
#include "main.h"
#include "move.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/item.h"
#include "constants/items.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_TAB = UI_HIT_SCREEN,
    HIT_ROW,
    HIT_ARROW_L,
    HIT_ARROW_R,
    HIT_SORT,
    HIT_MESSAGE,
    HIT_BACKDROP,
    HIT_MENU,
};

#define ROW_H 22
#define VIS 11
#define LEATHER_BG UI_HEX(0x8E5230)
#define LEATHER_BG2 UI_HEX(0x985A36)
static const struct UiRamp sTan = {UI_HEX(0xF0C890), UI_HEX(0xD8A066), UI_HEX(0xB07840), UI_HEX(0x4A2810)};
static const UiRGBA sPocketColours[POCKETS_COUNT] = {
    [POCKET_ITEMS] = UI_P_POCKETS_ITEMS, [POCKET_POKE_BALLS] = UI_P_POCKETS_BALLS, [POCKET_TM_HM] = UI_P_POCKETS_TMS,
    [POCKET_BERRIES] = UI_P_POCKETS_BERRIES, [POCKET_KEY_ITEMS] = UI_P_POCKETS_KEY,
};

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_Items[] = _("Items");
static const u8 sText_Balls[] = _("Poké Balls");
static const u8 sText_TMs[] = _("TMs & HMs");
static const u8 sText_Berries[] = _("Berries");
static const u8 sText_Key[] = _("Key Items");
static const u8 *const sPocketNames[POCKETS_COUNT] = {sText_Items, sText_Balls, sText_TMs, sText_Berries, sText_Key};
static const u8 sText_Empty[] = _("This pocket is empty.");
static const u8 sText_CloseBagHelp[] = _("Close the Bag.");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Pocket[] = _("Pocket");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Sort[] = _("Sort");
static const u8 sText_Back[] = _("Back");
static const u8 sText_PutHere[] = _("Put here");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Wait[] = _("Please wait…");
static const u8 sText_HowMany[] = _("How many?");
static const u8 sText_Power[] = _("Power");
static const u8 sText_Accuracy[] = _("Accuracy");
static const u8 sText_PP[] = _("PP");

/* ---------------------------------------------------------------- captured state */

static struct NativeBag sB;
static int sFirst;               /* the first row shown */
static u8 sMsg[256];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sMsgWaiting;
static u8 sDesc[256];            /* the description line as the bag printed it (menus: "X is selected.") */
static bool sHasDesc;
/* a mouse goal on the list: the engine's own presses until its cursor gets there */
static int sGoal = -1;
static bool sGoalThenA;
static int sGoalSteps;

static bool Detect(void)
{
    struct NativeBag b;

    if (!Bag_NativeUiState(&b))
        return false;
    /* Wally's catching tutorial drives the bag itself: the game's own picture */
    return b.phase != NATIVE_BAG_NONE && b.phase != NATIVE_BAG_TUTORIAL;
}

static int RowCount(void)
{
    return sB.count + (sB.closeRow ? 1 : 0);
}

static void GoalStep(void)
{
    if (sGoal < 0 || NativeUi_PressesPending() || !sB.ready)
        return;
    if (sB.cursor == sGoal)
    {
        if (sGoalThenA)
            NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
        return;
    }
    if (++sGoalSteps > 120)
    {
        sGoal = -1;
        return;
    }
    NativeUi_QueuePress(sGoal > sB.cursor ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    u8 prevPocket = sB.pocket;
    bool8 active;
    u32 printed;

    Bag_NativeUiState(&sB);
    if (sB.phase == NATIVE_BAG_LOADING)
        return;
    if (sB.pocket != prevPocket)
        sFirst = 0;
    if (sB.cursor < sFirst)
        sFirst = sB.cursor;
    if (sB.cursor >= sFirst + VIS)
        sFirst = sB.cursor - VIS + 1;
    if (sFirst > RowCount() - VIS)
        sFirst = RowCount() - VIS;
    if (sFirst < 0)
        sFirst = 0;
    for (int i = sFirst; i < sFirst + VIS && i < sB.count; i++)
        UiGfx_PrepareItemIcon(sB.items[i]);
    UiGfx_PrepareItemIcon(ITEM_POTION);
    UiGfx_PrepareItemIcon(ITEM_POKE_BALL);
    UiGfx_PrepareItemIcon(ITEM_ORAN_BERRY);
    sHasMsg = sB.msgWindow != WINDOW_NONE
           && TextPrinter_NativeUiWindowText(sB.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sMsgPrinting && TextPrinter_NativeUiWindowWaiting(sB.msgWindow);
    sHasDesc = TextPrinter_NativeUiWindowText(sB.descWindow, sDesc, sizeof(sDesc), &printed, &active);
    GoalStep();
}

/* ---------------------------------------------------------------- drawing pieces */

struct StitchCtx
{
    int step;
    int dash;
};

static bool StitchAt(int x, int y, const void *ctx)
{
    const struct StitchCtx *c = ctx;

    return (x + y) % c->step < c->dash;
}

/* W.stitches: a dashed stitch line inset px inside a shape's edge */
static void Stitches(const struct UiMask *m, int x, int y, int inset, UiRGBA colour)
{
    const struct UiMask *inner = m;
    struct StitchCtx c = {4, 2};
    char tag[16];

    for (int i = 0; i < inset; i++)
        inner = UiM_Erode4(inner);
    snprintf(tag, sizeof(tag), "st%d,%d", c.step, c.dash);
    UiG_Mask(UiM_And(UiM_Outline(inner), UiM_Fn(m->w, m->h, tag, StitchAt, &c)), x, y, colour);
}

/* the design's satchel; the front pocket takes the pocket's colour */
static void BagArt(int x, int y, int w, int h, UiRGBA pc)
{
    const struct UiRamp lr = {UI_HEX(0xF0B070), UI_HEX(0xD88A48), UI_HEX(0xA86030), UI_HEX(0x4A2410)};
    const struct UiRamp st = {UI_HEX(0xA86A3A), UI_HEX(0x8A4A20), UI_HEX(0x6A3416), UI_HEX(0x3A1C0C)};
    const struct UiRamp fr = {UI_HEX(0xF8C890), UI_HEX(0xE89C58), UI_HEX(0xC07838), UI_HEX(0x4A2410)};
    struct UiRamp pr = UiRampOf(pc, 0.4, 0.25, 0.62);
    int sw = (int)(w * 0.62), sh = (int)(h * 0.36), by = y + (int)(h * 0.26), fh = (int)(h * 0.36);
    int pw = (int)(w * 0.44), ph = (int)(h * 0.24), px = x + ((w - pw) >> 1), py = y + h - ph - 7, bx = x + (w >> 1) - 6;
    const struct UiMask *arc = UiM_Sub(UiM_Rounded4(sw, sh, 12, 12, 0, 0),
                                       UiM_Place(UiM_Rect(sw, sh), UiM_Rounded4(sw - 12, sh - 6, 9, 9, 0, 0), 6, 6));
    const struct UiMask *flap = UiM_Rounded4(w - 12, fh, 8, 8, 10, 10);
    const struct UiMask *patch = UiM_Rounded4(pw, ph, 4, 4, 4, 4);

    UiG_Paint(arc, x + ((w - sw) >> 1), y, st.base, &(struct UiPaint){st.ink, st.hi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(w - 12, h - (by - y), 8, 8, 7, 7), x + 6, by, lr.base,
              &(struct UiPaint){lr.ink, lr.hi, lr.lo, 0, 2, NULL, 0});
    for (int k = 0; k < 2; k++)
    {
        int sx = k == 0 ? x : x + w - 14, sy = y + (int)(h * 0.48);
        const struct UiMask *sp = UiM_Rounded4(14, (int)(h * 0.42), 4, 4, 4, 4);

        UiG_Paint(sp, sx, sy, lr.lo, &(struct UiPaint){lr.ink, lr.base, UI_NONE, 0, 0, NULL, 0});
        Stitches(sp, sx, sy, 2, lr.hi);
    }
    UiG_Paint(flap, x + 6, by - 2, fr.base, &(struct UiPaint){fr.ink, fr.hi, fr.lo, 0, 0, NULL, 0});
    Stitches(flap, x + 6, by - 2, 3, fr.hi);
    UiG_Paint(patch, px, py, pr.base, &(struct UiPaint){pr.ink, pr.hi, pr.lo, 0, 0, NULL, 0});
    Stitches(patch, px, py, 2, pr.hi);
    UiG_Paint(UiM_Rounded4(12, 11, 3, 3, 3, 3), bx, by - 2 + fh - 7, UI_HEX(0xF8D040),
              &(struct UiPaint){UI_HEX(0x5A3A08), UI_HEX(0xFFF0A0), UI_HEX(0xC89818), 0, 0, NULL, 0});
    UiG_Rect(bx + 5, by - 2 + fh - 4, 2, 4, UI_HEX(0x5A3A08));
}

static void PocketIcon(int pocket, int x, int y, const struct UiFilter *f)
{
    switch (pocket)
    {
    case POCKET_ITEMS: UiGfx_ItemIcon(ITEM_POTION, x, y, f); break;
    case POCKET_POKE_BALLS: UiGfx_ItemIcon(ITEM_POKE_BALL, x, y, f); break;
    case POCKET_TM_HM: UiGfx_Icon("disc", x, y, 0); break;
    case POCKET_BERRIES: UiGfx_ItemIcon(ITEM_ORAN_BERRY, x, y, f); break;
    default: UiGfx_Icon("key", x, y, 0); break;
    }
}

static void PocketTab(int x, int y, int pocket, bool active, bool available)
{
    struct UiRamp r = UiRampOf(available ? sPocketColours[pocket] : UI_HEX(0xA0A0A8), 0.4, 0.25, 0.62);
    static const struct UiFilter sDim = {0, 0.18f, 0, 0.82f};

    if (active)
    {
        UiG_Paint(UiM_Rounded4(70, 36, 5, 5, 0, 0), x, y, r.base, &(struct UiPaint){r.ink, r.hi, UI_NONE, 0, 0, NULL, 0});
        PocketIcon(pocket, x + 23, y + 5, NULL);
    }
    else
    {
        UiRGBA dk = UI_HEX(0x3A2010);

        UiG_Paint(UiM_Rounded4(70, 28, 5, 5, 0, 0), x, y + 8, UiMix(r.base, dk, 0.35),
                  &(struct UiPaint){r.ink, UiMix(r.hi, dk, 0.35), UiMix(r.lo, dk, 0.35), 0, 0, NULL, 0});
        PocketIcon(pocket, x + 23, y + 9, &sDim);
    }
}

/* an item's name as the bag lists it: a TM or HM with its move */
static void ItemName(u8 *dst, int size, u16 item)
{
    dst[0] = EOS;
    UiText_Append(dst, size, GetItemName(item));
    if (GetItemPocket(item) == POCKET_TM_HM)
    {
        UiText_AppendAscii(dst, size, " ");
        UiText_Append(dst, size, GetMoveName(GetItemTMHMMoveId(item)));
    }
}

static void InfoPane(int t, const struct UiRamp *pr, UiRGBA pc)
{
    int ix = 8, iy = 34, iw = 216, ih = 304;
    const struct UiMask *m = UiM_Rounded4(iw, ih, 5, 5, 5, 5);
    struct UiPattern dots = UiPat_Dots(8, 2, 0, 0, true);
    bool onItem = sB.cursor < sB.count;

    (void)t;
    UiG_Paint(m, ix, iy, sTan.base, &(struct UiPaint){sTan.ink, sTan.hi, sTan.lo, 0, 2, NULL, 0});
    Stitches(m, ix, iy, 3, sTan.hi);
    UiG_Paint(UiM_Rounded4(iw - 16, ih - 16, 3, 3, 3, 3), ix + 8, iy + 8, UI_P_CREAM,
              &(struct UiPaint){sTan.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(iw - 24, 104, 2, 2, 2, 2), ix + 12, iy + 12, UiMix(pr->hi, UI_HEX(0xFFFFFF), 0.45),
              &(struct UiPaint){UiMix(pr->lo, sTan.ink, 0.3), UI_NONE, UI_NONE, 0, 0, &dots, UiMix(pr->hi, pc, 0.3)});
    BagArt(ix + (iw >> 1) - 52, iy + 18, 104, 92, pc);
    UiG_Paint(UiM_Rounded4(iw - 24, 20, 2, 2, 2, 2), ix + 12, iy + 120, pr->base,
              &(struct UiPaint){pr->ink, pr->hi, pr->lo, 0, 0, NULL, 0});
    UiText_Draw(UI_FONT_NORMAL, ix + (iw >> 1), iy + 122, sPocketNames[sB.pocket], UI_P_TEXT_W, pr->ink, UI_ALIGN_CENTER, 0);
    if (!sB.pocketSwitchDisabled)
    {
        UiCaps_Draw(ix + 18, iy + 127, UI_CAPS_LEFT, UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(ix + iw - 22, iy + 127, UI_CAPS_RIGHT, UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_ARROW_L, 0, ix + 12, iy + 120, 24, 20);
        UiG_Hit(HIT_ARROW_R, 0, ix + iw - 36, iy + 120, 24, 20);
    }
    if (onItem)
    {
        u16 item = sB.items[sB.cursor];
        u8 name[64];
        int yy = iy + 192;

        UiG_Paint(UiM_Rounded4(32, 32, 3, 3, 3, 3), ix + 14, iy + 148, UI_P_PAPER,
                  &(struct UiPaint){sTan.ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiGfx_ItemIcon(item, ix + 18, iy + 152, NULL);
        ItemName(name, sizeof(name), item);
        UiText_Draw(UiText_Width(UI_FONT_NORMAL, name, 0) > iw - 60 ? UI_FONT_NARROW : UI_FONT_NORMAL, ix + 52, iy + 147,
                    name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        if (sB.pocket != POCKET_KEY_ITEMS && sB.pocket != POCKET_TM_HM)
        {
            u8 q[16];

            q[0] = EOS;
            UiText_AppendAscii(q, sizeof(q), "x");
            UiText_AppendNumber(q, sizeof(q), sB.quantities[sB.cursor], 1, false);
            UiText_Draw(UI_FONT_NORMAL, ix + 52, iy + 162, q, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        UiG_HLine(ix + 14, iy + 186, iw - 28, pr->base);
        UiG_HLine(ix + 14, iy + 187, iw - 28, pr->hi);
        if (sB.pocket == POCKET_TM_HM)
        {
            /* the TM's move, from the game's move data */
            u16 move = GetItemTMHMMoveId(item);
            char v[16];
            u8 b[16];
            int ty = UiMon_Type(GetMoveType(move));

            UiW_TypeBadge(ix + 52, iy + 164, ty >= 0 ? ty : UI_TYPE_NORMAL, 0);
            UiText_Draw(UI_FONT_SMALL, ix + 16, yy, sText_Power, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            snprintf(v, sizeof(v), "%s", GetMovePower(move) > 1 ? "" : "---");
            if (GetMovePower(move) > 1)
                snprintf(v, sizeof(v), "%u", (unsigned)GetMovePower(move));
            UiText_FromAscii(b, sizeof(b), v);
            UiText_Draw(UI_FONT_SMALL, ix + 66, yy, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            UiText_Draw(UI_FONT_SMALL, ix + 76, yy, sText_Accuracy, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            if (GetMoveAccuracy(move) > 0)
                snprintf(v, sizeof(v), "%u", (unsigned)GetMoveAccuracy(move));
            else
                snprintf(v, sizeof(v), "---");
            UiText_FromAscii(b, sizeof(b), v);
            UiText_Draw(UI_FONT_SMALL, ix + 150, yy, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            UiText_Draw(UI_FONT_SMALL, ix + 160, yy, sText_PP, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            snprintf(v, sizeof(v), "%u", (unsigned)GetMovePP(move));
            UiText_FromAscii(b, sizeof(b), v);
            UiText_Draw(UI_FONT_SMALL, ix + 196, yy, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            yy += 14;
            UiW_MessagePage(GetMoveDescription(move), -1, ix + 16, yy, iw - 32, 4);
        }
        else
        {
            UiW_MessagePage(GetItemDescription(item), -1, ix + 16, yy, iw - 32, 4);
        }
        {
            struct UiHintItem h[1] = {{UI_ACT_CONFIRM, sText_Select}};

            UiW_Hints(ix + 16, iy + 268, h, 1, false, 12);
        }
    }
    else if (sB.count == 0)
    {
        UiG_Text(ix + 16, iy + 150, sText_Empty);
    }
    else
    {
        UiGfx_Icon("return", ix + 18, iy + 152, 0);
        UiG_Text(ix + 52, iy + 147, gText_CloseBag);
        UiG_HLine(ix + 14, iy + 186, iw - 28, pr->base);
        UiG_HLine(ix + 14, iy + 187, iw - 28, pr->hi);
        UiG_Text(ix + 16, iy + 192, sText_CloseBagHelp);
    }
}

static void ListPanel(int t, const struct UiRamp *pr, UiRGBA pc)
{
    int px0 = 236, sx = px0 + 8, sy = 74, railH = 255, n = RowCount();

    for (int i = 0; i < POCKETS_COUNT; i++)
    {
        bool available = !(sB.victoryCatch && i == POCKET_POKE_BALLS) && (!sB.pocketSwitchDisabled || i == sB.pocket);

        PocketTab(px0 + 4 + i * 76, 6, i, i == sB.pocket, available);
        if (available)
            UiG_Hit(HIT_TAB, i, px0 + 4 + i * 76, 6, 70, 36);
    }
    UiG_Paint(UiM_Rounded4(396, 298, 3, 4, 4, 4), px0, 40, pr->base, &(struct UiPaint){pr->ink, pr->hi, pr->lo, 0, 2, NULL, 0});
    /* FIND: the design's search box, not in the game yet */
    UiG_Paint(UiM_Rounded4(64, 20, 2, 0, 2, 0), px0 + 8, 47, pr->lo, &(struct UiPaint){pr->ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    UiGfx_Icon("search", px0 + 12, 50, 0);
    UiCaps_Draw(px0 + 28, 54, "FIND", UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(196, 20, 0, 2, 0, 2), px0 + 71, 47, UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.5),
              &(struct UiPaint){pr->ink, UI_P_PAPER_LO, UI_NONE, 0, 0, NULL, 0});
    for (int k = 0; k < 12; k++)
        UiG_HLine(px0 + 80 + k * 14, 62, 9, UI_HEX(0xC0C0C0));
    UiW_Planned(px0 + 80 + 6 * 14 - 24, 51, NULL);
    /* SORT: the game's own (START) */
    UiG_Paint(UiM_Rounded4(104, 20, 2, 2, 2, 2), px0 + 278, 47, UiMix(pr->lo, pr->ink, 0.25),
              &(struct UiPaint){pr->ink, pr->lo, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(px0 + 286, 54, "SORT", pr->hi, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(px0 + 312, 54, UI_CAPS_UP UI_CAPS_DOWN, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiW_Hint(px0 + 330, 49, UI_ACT_START, NULL, true);
    UiG_Hit(HIT_SORT, 0, px0 + 278, 47, 104, 20);
    /* the list */
    UiG_Paint(UiM_Rounded4(368, 259, 2, 2, 2, 2), sx, sy, UI_P_CREAM, &(struct UiPaint){pr->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    for (int r = 0; r < VIS; r++)
    {
        int idx = sFirst + r, ry = sy + 4 + r * (ROW_H + 1);
        bool sel = idx == sB.cursor, carried = sB.phase == NATIVE_BAG_SWAPPING && idx == sB.swapFrom;

        if (idx >= n)
            break;
        if (sel)
        {
            const struct UiMask *hm = UiM_Rounded(352, ROW_H, UiCornerR(2), UiCornerChamfer(5), UiCornerR(2), UiCornerR(2));

            if (carried)
                UiW_CarryFrame(hm, sx + 10, ry);
            UiG_Paint(hm, sx + 10, ry, UiMix(pc, UI_HEX(0xFFFFFF), 0.55),
                      &(struct UiPaint){pr->ink, UiMix(pc, UI_HEX(0xFFFFFF), 0.8), UiMix(pc, UI_HEX(0xFFFFFF), 0.3), 0, 0, NULL, 0});
        }
        else if (carried)
        {
            UiG_Rect(sx + 4, ry + 1, 360, ROW_H - 1, UiMix(pc, UI_HEX(0xFFFFFF), 0.75));
        }
        else if (r % 2 == 1)
        {
            UiG_Rect(sx + 4, ry + 1, 360, ROW_H - 1, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        }
        if (r < VIS - 1 && !sel && idx + 1 != sB.cursor)
            for (int xx = sx + 12; xx < sx + 360; xx += 3)
                UiG_Put(xx, ry + ROW_H, UiMix(UI_P_CREAM_LO, sTan.lo, 0.3));
        if (idx < sB.count)
        {
            u16 item = sB.items[idx];
            u8 name[64];

            UiGfx_ItemIcon(item, sx + 14, ry - 1, NULL);
            ItemName(name, sizeof(name), item);
            UiG_Text(sx + 42, ry + 2, name);
            if (item == sB.registeredItem && item != ITEM_NONE)
                UiCaps_Draw(sx + 300, ry + 7, "REG", pr->base, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
            if (sB.pocket == POCKET_TM_HM)
            {
                int ty = UiMon_Type(GetMoveType(GetItemTMHMMoveId(item)));

                UiW_TypeBadge(sx + 312, ry + 4, ty >= 0 ? ty : UI_TYPE_NORMAL, 0);
            }
            else if (sB.pocket != POCKET_KEY_ITEMS)
            {
                u8 q[16];

                q[0] = EOS;
                UiText_AppendAscii(q, sizeof(q), "x");
                UiText_AppendNumber(q, sizeof(q), sB.quantities[idx], 1, false);
                UiText_Draw(UI_FONT_NORMAL, sx + 352, ry + 2, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            }
        }
        else
        {
            /* the game's own last row */
            UiGfx_Icon("return", sx + 18, ry + 3, 0);
            UiG_Text(sx + 42, ry + 2, gText_CloseBag);
        }
        if (sel)
            UiW_Cursor(sx + 1, ry + 5, UiW_Nudge(t), UI_NONE);
        if (sB.phase == NATIVE_BAG_SWAPPING && sel)
            UiG_Rect(sx + 12, ry - 1, 340, 2, UI_P_SEL);   /* where the moved item goes */
        UiG_Hit(HIT_ROW, idx, sx + 4, ry, 360, ROW_H);
    }
    UiG_Paint(UiM_Rounded4(8, railH, 2, 2, 2, 2), px0 + 380, 76, UiMix(pr->lo, pr->ink, 0.35), &(struct UiPaint){pr->ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    if (n > VIS)
    {
        int th = railH * VIS / n;

        UiG_Paint(UiM_Rounded4(8, th, 2, 2, 2, 2), px0 + 380, 76 + railH * sFirst / n, UI_P_CREAM,
                  &(struct UiPaint){pr->ink, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, 0});
    }
    else
    {
        UiG_Paint(UiM_Rounded4(8, railH, 2, 2, 2, 2), px0 + 380, 76, UI_P_CREAM, &(struct UiPaint){pr->ink, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, 0});
    }
}

static void Hints(void)
{
    struct UiHintItem h[5];
    int n = 0;

    switch (sB.phase)
    {
    case NATIVE_BAG_BROWSE:
        if (!sB.pocketSwitchDisabled)
            h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Pocket};
        if (sB.count > 1)
            h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Move};
        if (sB.count > 1)
            h[n++] = (struct UiHintItem){UI_ACT_START, sText_Sort};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_BAG_SWAPPING:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_PutHere};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_BAG_MENU:
    case NATIVE_BAG_YES_NO:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_BAG_QUANTITY:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_HowMany};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_BAG_MESSAGE:
        if (sMsgPrinting && !sMsgWaiting)
        {
            UiText_Draw(UI_FONT_SMALL, 244, 345, sText_Wait, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);
            return;
        }
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    default:
        break;
    }
    if (n > 0)
        UiW_Hints(242, 344, h, n, true, 0);
}

/* the item's menu as the game lays it out: a list, or two columns */
static void ContextMenu(int t)
{
    int cols = sB.menuGrid ? 2 : 1, rows = (sB.menuCount + cols - 1) / cols, colW = 0, w, h, x, y, rowH = 16;

    for (int i = 0; i < sB.menuCount; i++)
        if (sB.menuLabels[i] != NULL)
        {
            int lw = UiText_Width(UI_FONT_NORMAL, sB.menuLabels[i], 0) + 24;

            colW = lw > colW ? lw : colW;
        }
    if (colW < 56)
        colW = 56;
    w = cols * colW + 12;
    if (w < 104)
        w = 104;
    h = rows * rowH + 14;
    x = 624 - w;
    y = 330 - h;
    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    for (int i = 0; i < sB.menuCount; i++)
    {
        int cx = x + 8 + (i % cols) * colW, cy = y + 7 + (i / cols) * rowH;

        if (sB.menuLabels[i] == NULL)
            continue;
        UiG_Text(cx + 10, cy, sB.menuLabels[i]);
        if (i == sB.menuCursor)
            UiW_Cursor(cx + 1, cy + 2, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_MENU, i, cx, cy, colW, rowH);
    }
}

static void QuantityBox(void)
{
    int w = 104, h = 30, x = 624 - w, y = 330 - h;
    u8 q[16];
    int b[4];

    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, b);
    q[0] = EOS;
    UiText_AppendAscii(q, sizeof(q), "x");
    UiText_AppendNumber(q, sizeof(q), sB.quantity, 2, false);
    UiText_Draw(UI_FONT_NORMAL, x + w - 14, y + 7, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiW_DownArrow(x + 12, y + 4, UI_P_CURSOR, true);
    UiW_DownArrow(x + 12, y + 20, UI_P_CURSOR, false);
    if (sB.selling)
    {
        u8 price[24];

        price[0] = EOS;
        UiText_AppendNumber(price, sizeof(price), (long)GetItemSellPrice(sB.items[sB.cursor < sB.count ? sB.cursor : 0]) * sB.quantity, 1, true);
        UiText_AppendAscii(price, sizeof(price), "$");
        UiText_Draw(UI_FONT_SMALL, x + 26, y + 9, price, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void Draw(int t)
{
    int x0, y0, w, h;
    UiRGBA pc = sPocketColours[sB.pocket < POCKETS_COUNT ? sB.pocket : 0];
    struct UiRamp pr = UiRampOf(pc, 0.4, 0.25, 0.62);
    struct UiRamp tab = {sTan.hi, sTan.lo, UI_HEX(0x8A5A2A), sTan.ink};
    struct UiPattern diag = UiPat_Diag(32, 16, 0, 1);
    bool menuUp = sB.phase == NATIVE_BAG_MENU || sB.phase == NATIVE_BAG_YES_NO || sB.phase == NATIVE_BAG_QUANTITY;

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, LEATHER_BG);
    UiG_FillPattern(&diag, x0, y0, w, h, LEATHER_BG2, 0, 0);
    for (int xx = x0; xx < x0 + w; xx += 6)
    {
        UiG_HLine(xx, y0 + 3, 3, UI_HEX(0xC8905A));
        UiG_HLine(xx, y0 + h - 5, 3, UI_HEX(0xC8905A));
    }
    if (sB.phase == NATIVE_BAG_LOADING)
    {
        UiG_Veil(NativeUi_ScreenFade());
        return;
    }
    UiW_TitleTab(0, 10, "BAG", &tab, 0, NULL);
    InfoPane(t, &pr, pc);
    ListPanel(t, &pr, pc);
    Hints();
    /* the game's messages and questions in the message box */
    if (sHasMsg || ((sB.phase == NATIVE_BAG_MENU || sB.phase == NATIVE_BAG_YES_NO || sB.phase == NATIVE_BAG_QUANTITY) && sHasDesc))
    {
        int b[4];

        UiW_DialogBox(8, 304, 456, 50, NULL, UI_NONE, b);
        if (sHasMsg)
            UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] + 1, b[2] - 4, 2);
        else
            UiW_MessagePage(sDesc, -1, b[0] + 2, b[1] + 1, b[2] - 4, 2);
        if (sHasMsg && (sMsgWaiting || (sB.phase == NATIVE_BAG_MESSAGE && !sMsgPrinting)))
            UiW_DownArrow(b[0] + b[2] - 8, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
        UiG_Hit(HIT_MESSAGE, 0, 8, 304, 456, 50);
    }
    if (menuUp)
    {
        int bx, by, bw, bh;

        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Hit(HIT_BACKDROP, 0, bx, by, bw, bh);
    }
    if (sB.phase == NATIVE_BAG_MENU)
        ContextMenu(t);
    else if (sB.phase == NATIVE_BAG_YES_NO)
        UiW_YesNo(624, 296, sB.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    else if (sB.phase == NATIVE_BAG_QUANTITY)
        QuantityBox();
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

/* the engine's context-menu grid: the shortest way through its valid cells */
static void QueueGridPath(int from, int to)
{
    int prev[6], queue[6], head = 0, tail = 0, path[6], n = 0;

    for (int i = 0; i < 6; i++)
        prev[i] = -2;
    prev[from] = -1;
    queue[tail++] = from;
    while (head < tail)
    {
        int c = queue[head++];
        const int next[4] = {c - 2, c + 2, (c & 1) ? c - 1 : -1, (c & 1) ? -1 : c + 1};

        for (int k = 0; k < 4; k++)
        {
            int nx = next[k];

            if (nx < 0 || nx >= sB.menuCount || sB.menuLabels[nx] == NULL || prev[nx] != -2)
                continue;
            prev[nx] = c;
            queue[tail++] = nx;
        }
    }
    if (prev[to] == -2)
        return;
    for (int c = to; c != from; c = prev[c])
        path[n++] = c;
    for (int i = n - 1, c = from; i >= 0; c = path[i], i--)
    {
        int d = path[i] - c;

        NativeUi_QueuePress(d == -2 ? DPAD_UP : d == 2 ? DPAD_DOWN : d == -1 ? DPAD_LEFT : DPAD_RIGHT);
    }
}

static void QueuePocket(int to)
{
    int pockets[POCKETS_COUNT], n = 0, a = -1, b = -1, fwd;

    for (int i = 0; i < POCKETS_COUNT; i++)
        if (!(sB.victoryCatch && i == POCKET_POKE_BALLS))
            pockets[n++] = i;
    for (int i = 0; i < n; i++)
    {
        if (pockets[i] == sB.pocket)
            a = i;
        if (pockets[i] == to)
            b = i;
    }
    if (a < 0 || b < 0 || a == b)
        return;
    fwd = (b - a + n) % n;
    if (fwd <= n - fwd)
        NativeUi_QueuePresses(DPAD_RIGHT, fwd);
    else
        NativeUi_QueuePresses(DPAD_LEFT, n - fwd);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (NativeUi_PressesPending() || sGoal >= 0)
        return true;
    switch (sB.phase)
    {
    case NATIVE_BAG_BROWSE:
        if (hit->id == HIT_TAB)
            QueuePocket(hit->index);
        else if (hit->id == HIT_ARROW_L || hit->id == HIT_ARROW_R)
            NativeUi_QueuePress(hit->id == HIT_ARROW_L ? DPAD_LEFT : DPAD_RIGHT);
        else if (hit->id == HIT_SORT)
            NativeUi_QueuePress(START_BUTTON);
        else if (hit->id == HIT_ROW)
        {
            sGoal = hit->index;
            sGoalThenA = true;
            sGoalSteps = 0;
        }
        else
            return false;
        return true;
    case NATIVE_BAG_SWAPPING:
        if (hit->id == HIT_ROW)
        {
            sGoal = hit->index;
            sGoalThenA = true;
            sGoalSteps = 0;
            return true;
        }
        return false;
    case NATIVE_BAG_MENU:
        if (hit->id == HIT_MENU)
        {
            if (sB.menuGrid)
                QueueGridPath(sB.menuCursor, hit->index);
            else
                NativeUi_QueuePresses(hit->index > sB.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sB.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_BAG_YES_NO:
        if (hit->id == UI_HIT_YESNO)
        {
            NativeUi_QueuePresses(hit->index > sB.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sB.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_BAG_QUANTITY:
        NativeUi_QueuePress(A_BUTTON);
        return true;
    case NATIVE_BAG_MESSAGE:
        if (hit->id == HIT_MESSAGE)
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (NativeUi_PressesPending())
        return true;
    if (sB.phase == NATIVE_BAG_BROWSE || sB.phase == NATIVE_BAG_SWAPPING)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    else if (sB.phase == NATIVE_BAG_QUANTITY)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static bool Ready(void)
{
    return sB.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Bag = {
    .name = "bag",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
