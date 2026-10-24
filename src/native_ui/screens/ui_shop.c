#ifdef ENABLE_NATIVE_UI
/*
 * 15 · Poké Mart (the approved design's preview/js/screens/shop.js): the mart's
 * own look - the blue-and-white awning, the pegboard, the counter with its sign
 * and the stock on two shelves, luggage-style price tags - over the engine's
 * mart (shop.c), which keeps every rule: what is for sale (and the stock rules
 * of this build), prices (Slateport's POKéNEWS sale halves them), sold-out key
 * items, how many you can afford, the clerk's lines, the Premier Ball bonus,
 * the money and the Bag.
 *
 * The field's BUY / SELL / QUIT menu shows the mart with its board FOR SALE and
 * the clerk's line from the field's message box; BUY is the engine's buy screen
 * (the list, how many, YES / NO, the clerk's lines); SELL is the Bag in its
 * selling mode (ui_bag.c). The decoration shops (Fortree, Lilycove, Slateport,
 * the Pretty Petal flower shop) are the same counter with the decorations' own
 * pictures (their icons, a doll's field sprite, the image the game composes
 * from the secret base's metatiles), names, prices and descriptions, and how
 * many of each your PC holds; one at a time, as the engine sells them. No clerk
 * is drawn: the clerk speaks in the message box. Keys stay the engine's; clicks
 * and the wheel become its presses. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "decoration.h"
#include "item.h"
#include "main.h"
#include "move.h"
#include "strings.h"
#include "window.h"
#include "constants/characters.h"
#include "constants/decorations.h"
#include "constants/items.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_QTY_UP,
    HIT_QTY_DOWN,
    HIT_QTY_OK,
    HIT_MESSAGE,
};

#define ROW_H 24
#define VIS 9
#define SHELF 9

static const u8 sText_Select[] = _("Select");
static const u8 sText_Quit[] = _("Quit");
static const u8 sText_Buy[] = _("Buy");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Plus1[] = _("+1");
static const u8 sText_Plus10[] = _("+10");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Wait[] = _("Please wait…");
static const u8 sText_ChooseItem[] = _("Choose what to buy, or CANCEL to go back.");
#if I_PREMIER_BALL_BONUS >= GEN_8
static const u8 sText_Premier[] = _("Buy Poké Balls of any kind, 10 at a time, and get a Premier Ball for every 10.");
#else
static const u8 sText_Premier[] = _("Buy 10 Poké Balls at once and get a Premier Ball, too: one for every 10.");
#endif
#if ITEM_SELL_FACTOR == 2
static const u8 sText_MenuHelp[] = _("BUY: what this shop sells. SELL: the items in your Bag, at half their price.");
#else
static const u8 sText_MenuHelp[] = _("BUY: what this shop sells. SELL: the items in your Bag, for part of their price.");
#endif
static const u8 sText_DecorHelp[] = _("BUY: decorations for your room and Secret Base, sent to your PC.");
static const u8 sText_None[] = _("");

/* the mart's ramps (the design's MART: the RUN command's blue) */
#define MART_BASE UI_P_CMD_RUN

static const char *const sRegister[13] = {
    "....aaaaaaaa......", "....abbbbbba......", "....abccccba......", "....abbbbbba......",
    "aaaaaaaaaaaaaaaaa.", "addddddddddddddda.", "adeedeedeedeedeea.", "addddddddddddddda.",
    "adeedeedeedfffdda.", "addddddddddfffdda.", "aaaaaaaaaaaaaaaaa.", "agggggggggggggggga",
    "aaaaaaaaaaaaaaaaaa",
};

/* ---------------------------------------------------------------- captured state */

static struct NativeShop sS;
static int sFirst;
static u8 sMsg[256];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sBonusTip;
/* a mouse goal: the engine's own presses until its cursor gets there */
static int sGoal = -1;
static bool sGoalThenA;
static int sGoalSteps;

static bool Detect(void)
{
    struct NativeShop s;

    return Shop_NativeUiState(&s) && s.phase != NATIVE_SHOP_NONE;
}

/* an item's or (a decoration shop) a decoration's picture, name and description */
static void PrepareArt(u16 item)
{
    if (sS.decorations)
        UiGfx_PrepareDecor(item);
    else
        UiGfx_PrepareItemIcon(item);
}

static void Art(u16 item, int x, int y, int scale)
{
    if (sS.decorations)
        UiGfx_Decor(item, x, y, 24 * scale, scale);
    else if (scale > 1)
        UiGfx_ItemIconScaled(item, x, y, scale);
    else
        UiGfx_ItemIcon(item, x, y, NULL);
}

static void Name(u8 *dst, int size, u16 item)
{
    if (sS.decorations)
    {
        dst[0] = EOS;
        if (item <= NUM_DECORATIONS)
            UiText_Append(dst, size, gDecorations[item].name);
    }
    else
    {
        UiItems_ItemName(dst, size, item);
    }
}

static const u8 *Description(u16 item)
{
    if (sS.decorations)
        return item <= NUM_DECORATIONS ? gDecorations[item].description : sText_None;
    return GetItemDescription(item);
}

static bool OnField(void)
{
    return sS.phase == NATIVE_SHOP_MENU || sS.phase == NATIVE_SHOP_FIELD;
}

/* the list's rows: the stock, then CANCEL (the field's board: the stock only) */
static int RowCount(void)
{
    return sS.count + (OnField() ? 0 : 1);
}

static int GoalCursor(void)
{
    if (sS.phase == NATIVE_SHOP_LIST)
        return sS.cursor;
    return sS.menuCursor;
}

static void GoalStep(void)
{
    int cursor;

    if (sGoal < 0 || NativeUi_PressesPending() || !sS.ready)
        return;
    cursor = GoalCursor();
    if (cursor == sGoal)
    {
        if (sGoalThenA)
            NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
        return;
    }
    if (++sGoalSteps > 200)
    {
        sGoal = -1;
        return;
    }
    NativeUi_QueuePress(sGoal > cursor ? DPAD_DOWN : DPAD_UP);
}

static void SetGoal(int goal, bool thenA)
{
    sGoal = goal;
    sGoalThenA = thenA;
    sGoalSteps = 0;
    GoalStep();
}

static void Capture(void)
{
    u8 was = sS.phase;
    bool8 active;

    Shop_NativeUiState(&sS);
    if (sS.phase != was)
        sGoal = -1;
    if (sS.cursor < sFirst)
        sFirst = sS.cursor;
    if (sS.cursor >= sFirst + VIS)
        sFirst = sS.cursor - VIS + 1;
    if (OnField())
        sFirst = 0;
    if (sFirst > RowCount() - VIS)
        sFirst = RowCount() - VIS;
    if (sFirst < 0)
        sFirst = 0;
    for (int i = sFirst; i < sFirst + VIS && i < sS.count; i++)
        PrepareArt(sS.items[i]);
    for (int i = 0; i < 2 * SHELF && i < sS.count; i++)
        PrepareArt(sS.items[i]);
    if (sS.chosen != ITEM_NONE)
        PrepareArt(sS.chosen);
    sBonusTip = false;
    for (int i = 0; i < sS.count && !sS.decorations; i++)
    {
#if I_PREMIER_BALL_BONUS >= GEN_8
        if (GetItemPocket(sS.items[i]) == POCKET_POKE_BALLS)
#else
        if (sS.items[i] == ITEM_POKE_BALL)
#endif
            sBonusTip = true;
    }
    if (sBonusTip)
        UiGfx_PrepareItemIcon(ITEM_PREMIER_BALL);
    sHasMsg = sS.msgWindow != WINDOW_NONE && Window_NativeUiShown(sS.msgWindow)
           && TextPrinter_NativeUiWindowText(sS.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    GoalStep();
}

/* ---------------------------------------------------------------- the mart's pieces */

static struct UiRamp Mart(void)
{
    return UiRampOf(MART_BASE, 0.42, 0.25, 0.62);
}

/* an amount of the shop's currency: yen, or Game Corner coins in the Mega Stone dealer's shop (Update 29) */
static void Cash(u8 *dst, int size, u32 amount)
{
    if (!sS.coins)
    {
        UiItems_Money(dst, size, amount);
        return;
    }
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, (long)amount, 1, true);
    UiText_AppendAscii(dst, size, " COINS");
}

/* the price an item row shows: the engine's price, or SOLD OUT */
static const u8 *PriceText(int i, u8 *buf, int size)
{
    if (sS.soldOut[i])
        return gText_SoldOut;
    Cash(buf, size, sS.prices[i]);
    return buf;
}

struct AwningCtx
{
    int top;
    int x0;
};

static bool AwningShape(int x, int y, const void *ctx)
{
    const struct AwningCtx *c = ctx;
    int cy = y - c->top, lx;
    double dx, dy;

    if (cy < 22)
        return true;
    lx = ((x + c->x0) % 16 + 16) % 16;
    dx = lx - 7.5;
    dy = cy - 22 + 0.5;
    return dx * dx + dy * dy <= 60;
}

static bool AwningBlue(int x, int y, const void *ctx)
{
    const struct AwningCtx *c = ctx;

    (void)y;
    return (((x + c->x0 + 1600) >> 4) & 1) == 0; /* + 1600: whole 32 px periods, for the left bleed */
}

static void Background(const struct UiRamp *m)
{
    UiRGBA bg = UiMix(m->lo, m->ink, 0.25), peg = UiMix(bg, m->base, 0.4);
    struct UiPattern dots = UiPat_Dots(8, 1, 0, 0, true);
    struct AwningCtx c;
    const struct UiMask *shape, *blue;
    int x0, y0, w, h, bandH;
    char tag[48];

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, bg);
    UiG_FillPattern(&dots, x0, y0 + 2, w, h, peg, 0, 0);
    /* the scalloped awning across the top, the canvas's bleed included */
    c.top = -y0;
    c.x0 = x0;
    bandH = c.top + 30;
    snprintf(tag, sizeof(tag), "awn%dx%d:%ds", w, bandH, x0);
    shape = UiM_Fn(w, bandH, tag, AwningShape, &c);
    snprintf(tag, sizeof(tag), "awn%dx%d:%db", w, bandH, x0);
    blue = UiM_Fn(w, bandH, tag, AwningBlue, &c);
    UiG_Mask(UiM_Sub(UiM_Dilate4(shape), shape), x0, y0, m->ink);
    UiG_Mask(shape, x0, y0, UI_P_PAPER);
    UiG_Mask(UiM_And(shape, blue), x0, y0, m->base);
    UiG_Rect(x0, y0, w, 2, m->ink);
}

/* the counter scene: pegboard wall, two stocked shelves, the counter and its
 * sign, the item in question on the counter with its tag on a string */
static void CounterScene(int x, int y, int w, int h, const struct UiRamp *m, u16 cur, const u8 *tag, bool dim)
{
    struct UiRamp wood = {UI_P_CREAM_HI, UI_P_CREAM, UI_P_CREAM_LO, m->ink};
    struct UiPattern dots = UiPat_Dots(8, 1, 3, 3, true);
    int cy = y + 112, cw = w - 8, fh, sw = 132, sx, sy2;

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, m->base, &(struct UiPaint){m->ink, m->hi, m->lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 8, h - 8, 2, 2, 2, 2), x + 4, y + 4, UiMix(m->hi, UI_P_PAPER, 0.62),
              &(struct UiPaint){m->ink, UI_NONE, UI_NONE, 0, 0, &dots, UiMix(m->hi, UI_P_PAPER, 0.25)});
    for (int si = 0; si < 2; si++)
    {
        int sy = y + 30 + si * 30;
        /* decorations: eight a shelf, room for the two-wide posters */
        int per = sS.decorations ? SHELF - 1 : SHELF, step = sS.decorations ? 34 : 30;

        for (int i = 0; i < per && si * per + i < sS.count; i++)
            Art(sS.items[si * per + i], x + 12 + i * step + (sS.decorations ? 4 : 0), sy - 25, 1);
        UiG_Paint(UiM_Rect(w - 16, 5), x + 8, sy, wood.base, &(struct UiPaint){wood.ink, wood.hi, wood.lo, 0, 0, NULL, UI_NONE});
        for (int k = 0; k < 2; k++)
        {
            int bx = k == 0 ? x + 20 : x + w - 24;

            UiG_Rect(bx, sy + 5, 4, 3, m->lo);
            UiG_Rect(bx, sy + 8, 2, 1, m->lo);
        }
    }
    /* counter: top slab, the front panel with the blue sign */
    UiG_Paint(UiM_Rounded4(cw, 9, 2, 2, 0, 0), x + 4, cy, wood.base, &(struct UiPaint){wood.ink, wood.hi, wood.lo, 0, 0, NULL, UI_NONE});
    fh = y + h - 4 - (cy + 8);
    UiG_Paint(UiM_Rounded4(cw, fh, 0, 0, 2, 2), x + 4, cy + 8, m->base, &(struct UiPaint){m->ink, m->hi, m->lo, 0, 0, NULL, UI_NONE});
    for (int px = x + 12; px < x + cw; px += 24)
        UiG_VLine(px, cy + 10, fh - 4, UiMix(m->base, m->lo, 0.6));
    sx = x + 4 + ((cw - sw) >> 1);
    sy2 = cy + 13;
    UiG_Paint(UiM_Rounded4(sw, 26, 2, 2, 2, 2), sx, sy2, m->lo,
              &(struct UiPaint){m->ink, UiMix(m->lo, m->hi, 0.5), UiMix(m->lo, m->ink, 0.4), 0, 0, NULL, UI_NONE});
    UiItems_BigCaps(sx + (sw >> 1), sy2 + 5, sS.decorations ? "DECOR SHOP" : "POK" UI_CAPS_E_ACUTE " MART", 2, UI_P_TEXT_W,
                    m->ink, UI_ALIGN_CENTER);
    {
        struct UiPixPal pal[7] = {
            {'a', m->ink}, {'b', UI_P_PAPER}, {'c', UiMix(UI_P_HP_GREEN_0, UI_P_PAPER, 0.3)}, {'d', m->hi},
            {'e', m->lo}, {'f', UI_P_HP_YELLOW_1}, {'g', m->lo},
        };

        UiW_Pix("mart_register", sRegister, 13, pal, 7, x + w - 38, cy - 12, false);
    }
    if (cur != ITEM_NONE)
    {
        int ix = x + 28, iy = cy - 46, aw = 24, ah = 24, wide;

        /* a decoration wider than the icon's place (a long poster) starts there */
        if (sS.decorations)
            UiGfx_DecorSize(cur, &aw, &ah);
        wide = aw * 2 > 48 ? aw * 2 : 48;
        UiG_Mask(UiM_Ellipse(wide - 4, 6), ix + 2, cy - 2, UiMix(wood.lo, wood.ink, 0.25));
        Art(cur, ix + (wide - 48) / 2, iy, 2);
        if (tag != NULL)
        {
            int tx = ix + wide + 14, ty = cy - 32;

            UiItems_TagString(ix + wide - 8, iy + 14, tx + 9, ty + 7, m->ink);
            UiItems_PriceTag(tx, ty, 78, tag, m->ink, dim);
        }
    }
}

/* the item's description (Gen 3: the icon and the engine's text), or a line of help */
static void DescBox(int x, int y, int w, int h, const struct UiRamp *m, u16 item, const u8 *text)
{
    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_PAPER, &(struct UiPaint){m->ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    if (item == ITEM_NONE)
    {
        UiW_MessagePage(text, -1, x + 12, y + 6, w - 24, 3);
        return;
    }
    UiG_Paint(UiM_Rounded4(30, 30, 2, 2, 2, 2), x + 6, y + 6, UI_P_CREAM, &(struct UiPaint){m->ink, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    Art(item, x + 9, y + 9, 1);
    UiW_MessagePage(text != NULL ? text : Description(item), -1, x + 44, y + 4, w - 52, 3);
}

/* ---------------------------------------------------------------- the price board */

static void Board(int t, const struct UiRamp *m)
{
    int bx = 308, by = 34, bw = 324, bh = 260, sx = bx + 8, sy = by + 32, sw = bw - 16, sh = 222, n = RowCount();
    bool field = OnField(), list = sS.phase == NATIVE_SHOP_LIST;

    UiG_Paint(UiM_Rounded4(bw, bh, 3, 3, 3, 3), bx, by, m->base, &(struct UiPaint){m->ink, m->hi, m->lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(bw - 8, 24, 2, 2, 0, 0), bx + 4, by + 4, m->lo, &(struct UiPaint){UI_NONE, UiMix(m->lo, m->hi, 0.4), UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(bx + 12, by + 13, field ? "FOR SALE" : "BUY", UI_P_TEXT_W, m->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(bx + bw - 22, by + 13, sS.sale ? "SALE PRICE" : "PRICE", UiMix(m->hi, UI_P_PAPER, 0.5), UI_NONE, UI_CAPS_PLAIN,
                UI_ALIGN_RIGHT, UI_NONE);
    UiG_Paint(UiM_Rounded4(sw, sh, 2, 2, 2, 2), sx, sy, UI_P_CREAM, &(struct UiPaint){m->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int r = 0; r < VIS; r++)
    {
        int idx = sFirst + r, ry = sy + 3 + r * ROW_H;
        bool sel = !field && idx == sS.cursor;

        if (idx >= n)
            break;
        if (sel)
            UiG_Paint(UiM_Rounded(sw - 20, ROW_H, UiCornerR(2), UiCornerChamfer(4), UiCornerR(2), UiCornerR(2)), sx + 8, ry,
                      UiMix(m->hi, UI_P_PAPER, 0.45), &(struct UiPaint){m->lo, UiMix(m->hi, UI_P_PAPER, 0.8), UI_NONE, 0, 0, NULL, UI_NONE});
        else if (r < VIS - 1 && idx < n - 1)
            UiG_HLine(sx + 12, ry + ROW_H, sw - 34, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.4));
        if (idx == sS.count)
        {
            UiGfx_Icon("return_blue", sx + (sS.decorations ? 18 : 14), ry + 5, 0);
            UiG_Text(sx + (sS.decorations ? 46 : 38), ry + 4, gText_Cancel2);
        }
        else
        {
            u8 name[48], price[24];
            u16 item = sS.items[idx];
            const u8 *pt = PriceText(idx, price, sizeof(price));

            /* a decoration's picture may be two tiles wide (a long poster) */
            Art(item, sx + (sS.decorations ? 14 : 10), ry, 1);
            Name(name, sizeof(name), item);
            UiG_Text(sx + (sS.decorations ? 46 : 38), ry + 4, name);
            /* wide enough for its price: a coin shop's run to "500,000 COINS" (the Game Corner's RARE GOODS) */
            int tagW = max(74, UiText_Width(UI_FONT_NORMAL, pt, 0) + 18);

            UiItems_PriceTag(sx + sw - 16 - tagW, ry + 5, tagW, pt, m->lo, sS.soldOut[idx] || sS.prices[idx] > sS.money);
        }
        if (sel && list)
            UiW_Cursor(sx + 1, ry + 7, UiW_Nudge(t), UI_NONE);
        if (!field)
            UiG_Hit(HIT_ROW, idx, sx + 4, ry, sw - 16, ROW_H);
    }
    {
        struct UiRamp sr = {UiMix(m->hi, UI_P_PAPER, 0.6), m->hi, m->lo, m->ink};

        UiW_Scrollbar(sx + sw - 11, sy + 5, sh - 10, sFirst, n, VIS, &sr);
    }
}

/* ---------------------------------------------------------------- message, prompts, windows */

static bool Waiting(void)
{
    return sS.phase == NATIVE_SHOP_MESSAGE && sS.ready;
}

static void MessageBox(int t)
{
    int b[4];

    UiW_DialogBox(8, 300, 464, 54, NULL, UI_NONE, b);
    if (sHasMsg)
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] + 1, b[2] - 8, 2);
    else if (sS.phase == NATIVE_SHOP_LIST)
        UiW_MessagePage(sText_ChooseItem, -1, b[0] + 2, b[1] + 1, b[2] - 8, 2);
    if (Waiting())
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
    if (sS.phase == NATIVE_SHOP_MESSAGE)
        UiG_Hit(HIT_MESSAGE, 0, 8, 300, 464, 54);
}

static void Prompts(const struct UiRamp *m)
{
    struct UiHintItem rows[2][2];
    int counts[2] = {0, 0};

    UiG_Paint(UiM_Rounded4(160, 54, 3, 0, 3, 0), 480, 300, UI_P_CREAM, &(struct UiPaint){m->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    switch (sS.phase)
    {
    case NATIVE_SHOP_MESSAGE:
        if (sS.ready)
            rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        else
            UiText_Draw(UI_FONT_SMALL, 490, 306, sText_Wait, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        break;
    case NATIVE_SHOP_MENU:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Select};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Quit};
        break;
    case NATIVE_SHOP_QUANTITY:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Plus1};
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_MOVE, sText_Plus10};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_SHOP_CONFIRM:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        break;
    case NATIVE_SHOP_LIST:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Buy};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    default:
        UiText_Draw(UI_FONT_SMALL, 490, 306, sText_Wait, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        break;
    }
    for (int i = 0; i < 2; i++)
        if (counts[i] > 0)
            UiW_Hints(490, 309 + i * 20, rows[i], counts[i], false, 8);
}

static void Windows(int t, const struct UiRamp *m)
{
    if (sS.phase == NATIVE_SHOP_MENU)
    {
        struct UiMenuItem items[3];
        struct UiMenuOpt o = {0};
        int n = 0;

        for (int i = 0; i < sS.menuCount && i < 3; i++)
            if (sS.menuLabels[i] != NULL)
                items[n++] = (struct UiMenuItem){sS.menuLabels[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 92;
        UiW_MenuBox(632, 296, items, n, sS.menuCursor, &o, NULL);
    }
    if (sS.phase == NATIVE_SHOP_QUANTITY)
    {
        int qx = 480, qy = 252, qw = 152, qh = 44, bob = (t / 16) % 2;
        u8 q[16], total[24];

        UiG_Paint(UiM_Rounded4(qw, qh, 3, 3, 3, 3), qx, qy, UI_P_PAPER, &(struct UiPaint){m->ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiW_DownArrow(qx + 15, qy + 5 - bob, m->base, true);
        UiW_DownArrow(qx + 15, qy + 34 + bob, m->base, false);
        q[0] = CHAR_MULT_SIGN;
        q[1] = EOS;
        UiText_AppendNumber(q, sizeof(q), sS.quantity, 2, false);
        UiG_Text(qx + 9, qy + 12, q);
        Cash(total, sizeof(total), sS.totalCost);
        UiItems_PriceTag(qx + qw - 92, qy + 15, 84, total, m->lo, false);
        UiG_Hit(HIT_QTY_UP, 0, qx + 4, qy + 1, 30, 14);
        UiG_Hit(HIT_QTY_DOWN, 0, qx + 4, qy + 30, 30, 14);
        UiG_Hit(HIT_QTY_OK, 0, qx + 40, qy, qw - 40, qh);
    }
    if (sS.phase == NATIVE_SHOP_CONFIRM)
        UiW_YesNo(632, 296, sS.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
}

/* ---------------------------------------------------------------- the screen */

static void Draw(int t)
{
    struct UiRamp m = Mart();
    bool field = OnField();
    u16 item = ITEM_NONE;
    u8 money[24], inBag[16], tag[24];
    const u8 *tagText = NULL;
    bool dim = false;

    if (!field)
        item = sS.chosen != ITEM_NONE ? sS.chosen : sS.cursor < sS.count ? sS.items[sS.cursor] : ITEM_NONE;
    Background(&m);
    UiW_TitleTab(0, 8, sS.decorations ? "DECORATIONS" : "POK" UI_CAPS_E_ACUTE " MART", &m, 0, NULL);
    /* left column: money, how many in the Bag, the counter, the description */
    Cash(money, sizeof(money), sS.money);
    UiItems_MoneyBox(8, 34, 148, sS.coins ? "COINS" : "MONEY", money, m.ink);
    if (item != ITEM_NONE)
    {
        inBag[0] = CHAR_MULT_SIGN;
        inBag[1] = EOS;
        UiText_AppendNumber(inBag, sizeof(inBag), sS.inBag, 1, false);
    }
    else
    {
        UiText_FromAscii(inBag, sizeof(inBag), "---");
    }
    UiItems_MoneyBox(160, 34, 140, sS.decorations ? "IN YOUR PC" : "IN BAG", inBag, m.ink);
    if (item != ITEM_NONE)
    {
        for (int i = 0; i < sS.count; i++)
            if (sS.items[i] == item)
            {
                tagText = PriceText(i, tag, sizeof(tag));
                dim = sS.soldOut[i];
                break;
            }
    }
    CounterScene(8, 62, 292, 164, &m, item, tagText, dim);
    if (field)
        DescBox(8, 230, 292, 62, &m, sBonusTip ? ITEM_PREMIER_BALL : ITEM_NONE,
                sBonusTip ? sText_Premier : sS.decorations ? sText_DecorHelp : sText_MenuHelp);
    else if (item != ITEM_NONE)
        DescBox(8, 230, 292, 62, &m, item, NULL);
    else
        DescBox(8, 230, 292, 62, &m, ITEM_NONE, gText_QuitShopping);
    Board(t, &m);
    MessageBox(t);
    Prompts(&m);
    Windows(t, &m);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sS.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sS.ready || NativeUi_PressesPending())
        return button == 3 || hit->id >= UI_HIT_SCREEN || hit->id == UI_HIT_MENU || hit->id == UI_HIT_YESNO;
    if (button == 3)
    {
        sGoal = -1;
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    switch (sS.phase)
    {
    case NATIVE_SHOP_MENU:
        if (hit->id == UI_HIT_MENU)
        {
            SetGoal(hit->index, true);
            return true;
        }
        break;
    case NATIVE_SHOP_LIST:
        if (hit->id == HIT_ROW)
        {
            SetGoal(hit->index, true);
            return true;
        }
        break;
    case NATIVE_SHOP_QUANTITY:
        if (hit->id == HIT_QTY_UP || hit->id == HIT_QTY_DOWN || hit->id == HIT_QTY_OK)
        {
            NativeUi_QueuePress(hit->id == HIT_QTY_UP ? DPAD_UP : hit->id == HIT_QTY_DOWN ? DPAD_DOWN : A_BUTTON);
            return true;
        }
        break;
    case NATIVE_SHOP_CONFIRM:
        if (hit->id == UI_HIT_YESNO)
        {
            SetGoal(hit->index, true);
            return true;
        }
        break;
    case NATIVE_SHOP_MESSAGE:
        if (hit->id == HIT_MESSAGE || hit->id >= UI_HIT_SCREEN)
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        break;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (!sS.ready || dy == 0)
        return false;
    switch (sS.phase)
    {
    case NATIVE_SHOP_MENU:
    case NATIVE_SHOP_LIST:
    case NATIVE_SHOP_QUANTITY:
        if (!NativeUi_PressesPending())
            NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Shop = {
    .name = "shop",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
