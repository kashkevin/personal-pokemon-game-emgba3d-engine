#ifdef ENABLE_NATIVE_UI
/*
 * 14 · PC storage: the player's PC's ITEM STORAGE lists (WITHDRAW ITEM and
 * TOSS ITEM, player_pc.c) over the field, in the design's pieces: the PC's teal
 * and its tab (as the PC's first menus, ui_storage_menus.c), the bag's rows and
 * quantity box (ui_bag.c), the field's message box (ui_field_text.c). The items
 * the PC keeps with their quantities, the chosen one's icon, how many the PC and
 * the bag have; the engine's lines in the message box (what an item does, "how
 * many", withdrew, threw away, no room, too important), how many to take or
 * throw away, the toss's YES / NO, and moving an item (SELECT). The engine keeps
 * every rule and button: its list's cursor moves with its own presses (clicks
 * and the wheel queue them). DEPOSIT ITEM is the bag's (screen 2), the mailbox
 * ui_pc_mailbox.c's, the decorations' menus ui_pc_decor.c's. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "constants/characters.h"
#include "main.h"
#include "overworld.h"
#include "strings.h"
#include "window.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,     /* index: the row (count: CANCEL) */
    HIT_MESSAGE,
    HIT_QTY_UP,
    HIT_QTY_DOWN,
};

#define ROW_H 22
#define VIS 10
#define LX 8          /* the item card */
#define LY 34
#define LW 228
#define LH 262
#define PX 244        /* the list */
#define PY 34
#define PW 388
#define PH 262

static const u8 sText_Withdraw[] = _("Withdraw");
static const u8 sText_Toss[] = _("Toss");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Back[] = _("Back");
static const u8 sText_PutHere[] = _("Put here");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_HowMany[] = _("How many");
static const u8 sText_InPc[] = _("In the PC");
static const u8 sText_InBag[] = _("In the Bag");
static const u8 sText_Empty[] = _("No items are stored in this PC.");

static struct NativePcItems sI;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static int sFirst;      /* the first row shown */

static struct UiRamp PcRamp(void)
{
    /* ui_storage.c's terminal teal */
    return UiRampOf(UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.36), 0.38, 0.3, 0.62);
}

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativePcItems i;

    return gMain.callback2 == CB2_Overworld && PlayerPC_NativeUiItems(&i);
}

static int RowCount(void)
{
    return sI.count + 1; /* the engine's CANCEL row */
}

static void Capture(void)
{
    u32 printed = 0;
    bool8 active = FALSE;
    int n;

    PlayerPC_NativeUiItems(&sI);
    /* the items' icons, decoded here (the game thread) */
    for (int k = 0; k < sI.count; k++)
        UiGfx_PrepareItemIcon(sI.items[k]);
    UiGfx_PrepareItemIcon(ITEM_LIST_END);
    sHasMsg = sI.messageWindow != WINDOW_NONE && Window_NativeUiShown(sI.messageWindow)
           && TextPrinter_NativeUiWindowText(sI.messageWindow, sMsg, sizeof(sMsg), &printed, &active);
    /* the lines broken for the GBA's narrow window run on in the wider box */
    for (int k = 0; sHasMsg && k < (int)sizeof(sMsg) && sMsg[k] != EOS; k++)
        if (sMsg[k] == CHAR_NEWLINE)
            sMsg[k] = CHAR_SPACE;
    /* the rows shown follow the cursor */
    n = RowCount();
    if (sI.cursor < sFirst)
        sFirst = sI.cursor;
    if (sI.cursor >= sFirst + VIS)
        sFirst = sI.cursor - VIS + 1;
    if (sFirst > n - VIS)
        sFirst = n - VIS;
    if (sFirst < 0)
        sFirst = 0;
}

/* ---------------------------------------------------------------- drawing */

static void ItemCard(const struct UiRamp *r)
{
    const struct UiMask *m = UiM_Rounded4(LW, LH, 4, 4, 4, 4);
    struct UiPattern dots = UiPat_Dots(8, 2, 0, 0, true);
    int cx = LX + (LW >> 1), iy = LY + 34;

    UiG_Paint(m, LX, LY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    /* the engine's title of the list */
    UiText_Draw(UI_FONT_NORMAL, cx, LY + 7, sI.title, UI_P_TEXT_W, r->ink, UI_ALIGN_CENTER, 0);
    UiG_Paint(UiM_Rounded4(LW - 16, LH - 34, 3, 3, 3, 3), LX + 8, LY + 26, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    /* the stage the item stands on */
    UiG_Paint(UiM_Rounded4(LW - 32, 104, 3, 3, 3, 3), LX + 16, iy, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.55),
              &(struct UiPaint){UiMix(r->lo, r->ink, 0.3), UI_NONE, UI_NONE, 0, 0, &dots, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.25)});
    if (sI.count == 0)
    {
        UiW_MessagePage(sText_Empty, -1, LX + 18, iy + 116, LW - 36, 3);
        return;
    }
    if (sI.cursor < sI.count)
    {
        u16 item = sI.items[sI.cursor];
        u8 name[64], q[16];
        int ry = iy + 140;

        /* the item at three times its size */
        UiGfx_ItemIconScaled(item, cx - 36, iy + 16, 3);
        UiItems_ItemName(name, sizeof(name), item);
        UiText_Draw(UiText_Width(UI_FONT_NORMAL, name, 0) > LW - 36 ? UI_FONT_NARROW : UI_FONT_NORMAL, cx, iy + 112, name,
                    UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        UiG_HLine(LX + 18, ry - 4, LW - 36, r->base);
        UiG_HLine(LX + 18, ry - 3, LW - 36, r->hi);
        UiText_Draw(UI_FONT_NORMAL, LX + 20, ry + 2, sText_InPc, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        q[0] = CHAR_MULT_SIGN;
        q[1] = EOS;
        UiText_AppendNumber(q, sizeof(q), sI.quantities[sI.cursor], 1, false);
        UiText_Draw(UI_FONT_NORMAL, LX + LW - 20, ry + 2, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        UiText_Draw(UI_FONT_NORMAL, LX + 20, ry + 22, sText_InBag, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        q[0] = CHAR_MULT_SIGN;
        q[1] = EOS;
        UiText_AppendNumber(q, sizeof(q), sI.inBag, 1, false);
        UiText_Draw(UI_FONT_NORMAL, LX + LW - 20, ry + 22, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
    else
    {
        /* the game's own return arrow, as its item storage shows for CANCEL */
        UiGfx_ItemIconScaled(ITEM_LIST_END, cx - 36, iy + 16, 3);
        UiText_Draw(UI_FONT_NORMAL, cx, iy + 112, gText_Cancel2, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
}

static void List(int t, const struct UiRamp *r)
{
    int sx = PX + 8, sy = PY + 26, n = RowCount();
    UiRGBA pc = UiMix(UI_P_TYPES_ICE, UI_HEX(0xFFFFFF), 0.2);
    u8 count[24];

    UiG_Paint(UiM_Rounded4(PW, PH, 4, 4, 4, 4), PX, PY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    UiCaps_Draw(PX + 12, PY + 9, "ITEMS IN THE PC", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    count[0] = EOS;
    UiText_AppendNumber(count, sizeof(count), sI.count, 1, false);
    UiText_AppendAscii(count, sizeof(count), " / ");
    UiText_AppendNumber(count, sizeof(count), NATIVE_PC_ITEMS_MAX, 1, false);
    UiText_Draw(UI_FONT_SMALL, PX + PW - 22, PY + 6, count, UI_P_TEXT_W, r->ink, UI_ALIGN_RIGHT, 0);
    UiG_Paint(UiM_Rounded4(PW - 30, PH - 34, 2, 2, 2, 2), sx, sy, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int k = 0; k < VIS; k++)
    {
        int idx = sFirst + k, ry = sy + 4 + k * (ROW_H + 1);
        bool sel = idx == sI.cursor;
        bool carried = sI.phase == NATIVE_PC_ITEMS_SWAP && idx == sI.swapFrom;

        if (idx >= n)
            break;
        if (sel)
        {
            const struct UiMask *hm = UiM_Rounded(PW - 46, ROW_H, UiCornerR(2), UiCornerChamfer(5), UiCornerR(2), UiCornerR(2));

            UiG_Paint(hm, sx + 8, ry, UiMix(pc, UI_HEX(0xFFFFFF), 0.55),
                      &(struct UiPaint){r->ink, UiMix(pc, UI_HEX(0xFFFFFF), 0.8), UiMix(pc, UI_HEX(0xFFFFFF), 0.3), 0, 0, NULL, UI_NONE});
        }
        else if (carried)
        {
            UiW_CarryFrame(UiM_Rounded4(PW - 46, ROW_H, 2, 2, 2, 2), sx + 8, ry);
        }
        else if (k % 2 == 1)
        {
            UiG_Rect(sx + 4, ry + 1, PW - 38, ROW_H - 1, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        }
        if (idx < sI.count)
        {
            u8 name[64], q[16];

            UiGfx_ItemIcon(sI.items[idx], sx + 12, ry - 1, NULL);
            UiItems_ItemName(name, sizeof(name), sI.items[idx]);
            UiG_Text(sx + 40, ry + 2, name);
            q[0] = CHAR_MULT_SIGN;
            q[1] = EOS;
            UiText_AppendNumber(q, sizeof(q), sI.quantities[idx], 1, false);
            UiText_Draw(UI_FONT_NORMAL, sx + PW - 44, ry + 2, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        else
        {
            /* the engine's own last row */
            UiGfx_Icon("return_blue", sx + 14, ry + 5, 0);
            UiG_Text(sx + 40, ry + 2, gText_Cancel2);
        }
        if (sel)
            UiW_Cursor(sx - 1, ry + 5, UiW_Nudge(t), UI_NONE);
        if (sI.phase == NATIVE_PC_ITEMS_SWAP && sel)
            UiG_Rect(sx + 10, ry - 1, PW - 50, 2, UI_P_SEL);   /* where the moved item goes */
        UiG_Hit(HIT_ROW, idx, sx + 4, ry, PW - 38, ROW_H);
    }
    UiW_Scrollbar(PX + PW - 18, sy + 2, PH - 38, sFirst, n, VIS, r);
}

static void QuantityBox(int R, int B)
{
    int w = 120, h = 34, x = R - 8 - w, y = B - UI_FIELD_MSG_H - 8 - h - 6;
    u8 q[16];
    int b[4];

    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, b);
    q[0] = CHAR_MULT_SIGN;
    q[1] = EOS;
    UiText_AppendNumber(q, sizeof(q), sI.quantity, 2, false);
    UiText_Draw(UI_FONT_NORMAL, x + 58, y + 9, q, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    q[0] = EOS;
    UiText_AppendAscii(q, sizeof(q), "/ ");
    UiText_AppendNumber(q, sizeof(q), sI.quantityMax, 1, false);
    UiText_Draw(UI_FONT_SMALL, x + 66, y + 13, q, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiW_DownArrow(x + 12, y + 5, UI_P_CURSOR, true);
    UiW_DownArrow(x + 12, y + 21, UI_P_CURSOR, false);
    /* the box's upper half adds one, its lower half takes one away */
    UiG_Hit(HIT_QTY_UP, 0, x, y, w, h >> 1);
    UiG_Hit(HIT_QTY_DOWN, 0, x, y + (h >> 1), w, h - (h >> 1));
}

static int Hints(struct UiHintItem *h)
{
    int n = 0;

    if (!sI.ready)
        return 0;
    switch (sI.phase)
    {
    case NATIVE_PC_ITEMS_LIST:
        if (sI.cursor < sI.count)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sI.toss ? sText_Toss : sText_Withdraw};
            h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Move};
        }
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_PC_ITEMS_SWAP:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_PutHere};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_PC_ITEMS_QUANTITY:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_HowMany};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_PC_ITEMS_CONFIRM:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        break;
    case NATIVE_PC_ITEMS_MESSAGE:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    }
    return n;
}

static void Draw(int t)
{
    struct UiRamp r = PcRamp();
    struct UiHintItem h[4];
    int bx, by, bw, bh, L, R, B, n;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    UiW_TitleTab(L, by + 8, "ITEM STORAGE", &r, 0, NULL);
    ItemCard(&r);
    List(t, &r);
    n = Hints(h);
    UiField_MessageBox(t, L, R, B, sHasMsg ? sMsg : NULL, -1, sI.phase == NATIVE_PC_ITEMS_MESSAGE && sI.ready, NULL, h, n,
                       r.ink, HIT_MESSAGE);
    if (sI.phase == NATIVE_PC_ITEMS_QUANTITY)
        QuantityBox(R, B);
    else if (sI.phase == NATIVE_PC_ITEMS_CONFIRM)
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sI.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input: the engine's own buttons */

static bool Ready(void)
{
    return sI.ready;
}

/* a row: the list's cursor walked there a press at a time (it does not wrap), then A */
static void ChooseRow(int idx)
{
    int moves = idx - sI.cursor;

    if (moves != 0)
        NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
    NativeUi_QueuePress(A_BUTTON);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sI.ready)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || !sI.ready)
        return false;
    switch (sI.phase)
    {
    case NATIVE_PC_ITEMS_LIST:
    case NATIVE_PC_ITEMS_SWAP:
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index < RowCount())
        {
            ChooseRow(hit->index);
            return true;
        }
        break;
    case NATIVE_PC_ITEMS_QUANTITY:
        if (hit->id == HIT_QTY_UP || hit->id == HIT_QTY_DOWN)
        {
            NativeUi_QueuePress(hit->id == HIT_QTY_UP ? DPAD_UP : DPAD_DOWN);
            return true;
        }
        if (hit->id == HIT_MESSAGE)
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        break;
    case NATIVE_PC_ITEMS_CONFIRM:
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            int moves = hit->index - sI.yesNoCursor;

            if (moves != 0)
                NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        break;
    case NATIVE_PC_ITEMS_MESSAGE:
        if (hit->id == HIT_MESSAGE)
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
    if (dy == 0 || NativeUi_PressesPending() || !sI.ready)
        return false;
    if (sI.phase != NATIVE_PC_ITEMS_LIST && sI.phase != NATIVE_PC_ITEMS_SWAP && sI.phase != NATIVE_PC_ITEMS_QUANTITY)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

/* the lists' own sprites (the item's icon, the scroll arrows, the move line) stay
 * out of the picture */
static void HideObjects(uint32_t mask[4])
{
    u32 tiles[32];
    const struct OamData *oam = (const struct OamData *)OAM;

    PlayerPC_NativeUiItemTiles(tiles);
    for (int i = 0; i < 128; i++)
    {
        u32 t = oam[i].tileNum;

        if ((tiles[t >> 5] >> (t & 31)) & 1)
            mask[i >> 5] |= 1u << (i & 31);
    }
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_PcItems = {
    .name = "storage",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
    .hideObjects = HideObjects,
};
#endif // ENABLE_NATIVE_UI
