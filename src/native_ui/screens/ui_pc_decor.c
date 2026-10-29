#ifdef ENABLE_NATIVE_UI
/*
 * 14 · PC storage: the decoration PC's menus (decoration.c: the player's room's
 * PC, a secret base's PC, the Fortree trader's choice) over the field, in the
 * PC's own pieces (ui_pc_items.c's teal, tab, card and rows; the field's
 * message box): what to do (DECORATE, PUT AWAY, TOSS, CANCEL) with the engine's
 * description, the eight categories with how many of each are owned and which
 * the player's room takes, a category's decorations with those in use (in the
 * secret base, in the room) and the chosen one's picture as the shops show it,
 * the toss's YES / NO and the engine's lines. Placing a decoration and putting
 * one away on the field keep the game's picture. The engine keeps every rule
 * and button: clicks and the wheel queue its own presses. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "decoration.h"
#include "main.h"
#include "overworld.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/items.h"
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
    HIT_ROW = UI_HIT_SCREEN,
    HIT_MESSAGE,
};

#define ROW_H 22
#define VIS 10
#define LX 8          /* the card */
#define LY 34
#define LW 228
#define LH 262
#define PX 244        /* the list */
#define PY 34
#define PW 388
#define PH 262

static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Back[] = _("Back");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_InBase[] = _("In the secret base");
static const u8 sText_InRoom[] = _("In your room");
static const u8 sText_InPc[] = _("In the PC");
static const u8 sText_RoomTakes[] = _("Your room takes dolls and cushions.");

static struct NativeDecorMenu sD;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static int sFirst;
static u8 sListedPhase;

static struct UiRamp PcRamp(void)
{
    /* ui_storage.c's terminal teal, as the PC's other lists */
    return UiRampOf(UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.36), 0.38, 0.3, 0.62);
}

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativeDecorMenu d;

    return gMain.callback2 == CB2_Overworld && Decoration_NativeUiMenu(&d);
}

/* the list shown: the actions, the categories, or a category's decorations (a line and the YES / NO keep the list
 * they came from) */
static u8 ListOf(void)
{
    switch (sD.phase)
    {
    case NATIVE_DECOR_MENU_ACTIONS:
    case NATIVE_DECOR_MENU_CATEGORIES:
    case NATIVE_DECOR_MENU_ITEMS:
        return sD.phase;
    case NATIVE_DECOR_MENU_OPENING:
    case NATIVE_DECOR_MENU_CONFIRM:
        return NATIVE_DECOR_MENU_ITEMS;
    default:
        return sListedPhase != NATIVE_DECOR_MENU_NONE ? sListedPhase : NATIVE_DECOR_MENU_ACTIONS;
    }
}

static int RowCount(void)
{
    switch (ListOf())
    {
    case NATIVE_DECOR_MENU_ACTIONS:
        return 4;
    case NATIVE_DECOR_MENU_CATEGORIES:
        return NATIVE_DECOR_CATEGORIES + 1;
    default:
        return sD.count + 1;
    }
}

static void Capture(void)
{
    u32 printed = 0;
    bool8 active = FALSE;
    int n;

    Decoration_NativeUiMenu(&sD);
    if (sD.phase == NATIVE_DECOR_MENU_ACTIONS || sD.phase == NATIVE_DECOR_MENU_CATEGORIES || sD.phase == NATIVE_DECOR_MENU_ITEMS)
        sListedPhase = sD.phase;
    for (int k = 0; k < sD.count; k++)
        UiGfx_PrepareDecor(sD.items[k]);
    UiGfx_PrepareItemIcon(ITEM_LIST_END);
    sHasMsg = Window_NativeUiShown(0) && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &printed, &active);
    for (int k = 0; sHasMsg && k < (int)sizeof(sMsg) && sMsg[k] != EOS; k++)
        if (sMsg[k] == CHAR_NEWLINE)
            sMsg[k] = CHAR_SPACE;
    n = RowCount();
    if (ListOf() != NATIVE_DECOR_MENU_ITEMS)
        sFirst = 0;
    if (sD.cursor < sFirst)
        sFirst = sD.cursor;
    if (sD.cursor >= sFirst + VIS)
        sFirst = sD.cursor - VIS + 1;
    if (sFirst > n - VIS)
        sFirst = n - VIS;
    if (sFirst < 0)
        sFirst = 0;
}

/* ---------------------------------------------------------------- drawing */

static void Card(const struct UiRamp *r)
{
    const struct UiMask *m = UiM_Rounded4(LW, LH, 4, 4, 4, 4);
    struct UiPattern dots = UiPat_Dots(8, 2, 0, 0, true);
    int cx = LX + (LW >> 1), iy = LY + 34;
    u8 line[48];

    UiG_Paint(m, LX, LY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    UiCaps_Draw(cx, LY + 9, sD.playerRoom ? "YOUR ROOM" : "SECRET BASE", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE,
                UI_ALIGN_CENTER, UI_NONE);
    UiG_Paint(UiM_Rounded4(LW - 16, LH - 34, 3, 3, 3, 3), LX + 8, LY + 26, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(LW - 32, 104, 3, 3, 3, 3), LX + 16, iy, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.55),
              &(struct UiPaint){UiMix(r->lo, r->ink, 0.3), UI_NONE, UI_NONE, 0, 0, &dots, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.25)});
    switch (ListOf())
    {
    case NATIVE_DECOR_MENU_ITEMS:
        if (sD.cursor < sD.count)
        {
            u16 decor = sD.items[sD.cursor];
            const u8 *where = sD.inUse[sD.cursor] == 1 ? sText_InBase : sD.inUse[sD.cursor] == 2 ? sText_InRoom : sText_InPc;

            /* the decoration as the shops show it, three times its size, on the stage */
            UiGfx_Decor(decor, cx - 48, iy + 4, 96, 3);
            UiText_Draw(UI_FONT_NORMAL, cx, iy + 112, gDecorations[decor].name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
            UiG_HLine(LX + 18, iy + 132, LW - 36, r->base);
            UiG_HLine(LX + 18, iy + 133, LW - 36, r->hi);
            UiText_Draw(UI_FONT_NORMAL, LX + 20, iy + 138, where, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            break;
        }
        UiGfx_ItemIconScaled(ITEM_LIST_END, cx - 36, iy + 16, 3);
        UiText_Draw(UI_FONT_NORMAL, cx, iy + 112, gText_Cancel2, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        break;
    case NATIVE_DECOR_MENU_CATEGORIES:
        if (sD.cursor < NATIVE_DECOR_CATEGORIES)
        {
            line[0] = EOS;
            UiText_AppendNumber(line, sizeof(line), sD.owned[sD.cursor], 1, false);
            UiText_AppendAscii(line, sizeof(line), " / ");
            UiText_AppendNumber(line, sizeof(line), sD.capacity[sD.cursor], 1, false);
            UiText_Draw(UI_FONT_NORMAL, cx, iy + 34, sD.categoryNames[sD.cursor], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
            UiText_Draw(UI_FONT_NORMAL, cx, iy + 56, line, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        }
        if (sD.playerRoom && !sD.trade && sD.categoryDisabled[0])
            UiW_MessagePage(sText_RoomTakes, -1, LX + 18, iy + 116, LW - 36, 3);
        break;
    default:
        /* what is owned, category by category */
        for (int i = 0; i < NATIVE_DECOR_CATEGORIES; i++)
        {
            int y = iy + 4 + (i % 4) * 24, x = LX + 24 + (i / 4) * 96;

            line[0] = EOS;
            UiText_AppendNumber(line, sizeof(line), sD.owned[i], 1, false);
            UiText_Draw(UI_FONT_SMALL, x, y + 2, sD.categoryNames[i], UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            UiText_Draw(UI_FONT_NORMAL, x + 80, y, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        break;
    }
}

static void Row(int idx, int sx, int ry, const struct UiRamp *r)
{
    u8 count[16];

    switch (ListOf())
    {
    case NATIVE_DECOR_MENU_ACTIONS:
        UiG_Text(sx + 16, ry + 2, sD.actions[idx]);
        break;
    case NATIVE_DECOR_MENU_CATEGORIES:
        if (idx < NATIVE_DECOR_CATEGORIES)
        {
            bool dim = sD.categoryDisabled[idx];

            UiText_Draw(UI_FONT_NORMAL, sx + 16, ry + 2, sD.categoryNames[idx], dim ? UI_P_TEXT_MUTED : UI_P_TEXT,
                        UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            count[0] = EOS;
            UiText_AppendNumber(count, sizeof(count), sD.owned[idx], 1, false);
            UiText_AppendAscii(count, sizeof(count), "/");
            UiText_AppendNumber(count, sizeof(count), sD.capacity[idx], 1, false);
            UiText_Draw(UI_FONT_NORMAL, sx + PW - 44, ry + 2, count, dim ? UI_P_TEXT_MUTED : UI_P_TEXT, UI_P_TEXT_SH,
                        UI_ALIGN_RIGHT, 0);
        }
        else
        {
            UiGfx_Icon("return_blue", sx + 14, ry + 5, 0);
            UiG_Text(sx + 40, ry + 2, sD.categoryCancel);
        }
        break;
    default:
        if (idx < sD.count)
        {
            bool dim = sD.categoryDisabled[sD.category < NATIVE_DECOR_CATEGORIES ? sD.category : 0];

            UiText_Draw(UI_FONT_NORMAL, sx + 16, ry + 2, gDecorations[sD.items[idx]].name, dim ? UI_P_TEXT_MUTED : UI_P_TEXT,
                        UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            /* the game's in-use marks: red in the secret base, blue in the room */
            if (sD.inUse[idx] != 0)
                UiG_Paint(UiM_Circle(8), sx + PW - 56, ry + 7, sD.inUse[idx] == 1 ? UI_HEX(0xE83030) : UI_HEX(0x3070E8),
                          &(struct UiPaint){r->ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        }
        else
        {
            UiGfx_Icon("return_blue", sx + 14, ry + 5, 0);
            UiG_Text(sx + 40, ry + 2, gText_Cancel2);
        }
        break;
    }
}

static void List(int t, const struct UiRamp *r)
{
    int sx = PX + 8, sy = PY + 26, n = RowCount();
    UiRGBA pc = UiMix(UI_P_TYPES_ICE, UI_HEX(0xFFFFFF), 0.2);
    bool live = sD.phase == NATIVE_DECOR_MENU_ACTIONS || sD.phase == NATIVE_DECOR_MENU_CATEGORIES
             || sD.phase == NATIVE_DECOR_MENU_ITEMS;
    char title[24];

    UiG_Paint(UiM_Rounded4(PW, PH, 4, 4, 4, 4), PX, PY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    if (ListOf() == NATIVE_DECOR_MENU_ITEMS && sD.category < NATIVE_DECOR_CATEGORIES)
        UiText_ToAscii(title, sizeof(title), sD.categoryNames[sD.category]);
    else
        strcpy(title, ListOf() == NATIVE_DECOR_MENU_CATEGORIES ? "CATEGORIES" : "DECORATION");
    UiCaps_Draw(PX + 12, PY + 9, title, UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(PW - 30, PH - 34, 2, 2, 2, 2), sx, sy, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int k = 0; k < VIS; k++)
    {
        int idx = sFirst + k, ry = sy + 4 + k * (ROW_H + 1);
        bool sel = idx == sD.cursor;

        if (idx >= n)
            break;
        if (sel)
        {
            const struct UiMask *hm = UiM_Rounded(PW - 46, ROW_H, UiCornerR(2), UiCornerChamfer(5), UiCornerR(2), UiCornerR(2));

            UiG_Paint(hm, sx + 8, ry, UiMix(pc, UI_HEX(0xFFFFFF), 0.55),
                      &(struct UiPaint){r->ink, UiMix(pc, UI_HEX(0xFFFFFF), 0.8), UiMix(pc, UI_HEX(0xFFFFFF), 0.3), 0, 0, NULL, UI_NONE});
        }
        else if (k % 2 == 1)
        {
            UiG_Rect(sx + 4, ry + 1, PW - 38, ROW_H - 1, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        }
        Row(idx, sx, ry, r);
        if (sel && live)
            UiW_Cursor(sx - 1, ry + 5, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ROW, idx, sx + 4, ry, PW - 38, ROW_H);
    }
    if (ListOf() == NATIVE_DECOR_MENU_ITEMS)
        UiW_Scrollbar(PX + PW - 18, sy + 2, PH - 38, sFirst, n, VIS, r);
}

static int Hints(struct UiHintItem *h)
{
    int n = 0;

    if (!sD.ready)
        return 0;
    switch (sD.phase)
    {
    case NATIVE_DECOR_MENU_ACTIONS:
    case NATIVE_DECOR_MENU_CATEGORIES:
    case NATIVE_DECOR_MENU_ITEMS:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_DECOR_MENU_CONFIRM:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        break;
    case NATIVE_DECOR_MENU_MESSAGE:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    }
    return n;
}

static void Draw(int t)
{
    struct UiRamp r = PcRamp();
    struct UiHintItem h[4];
    const u8 *msg = sHasMsg ? sMsg : NULL;
    int bx, by, bw, bh, L, R, B, n;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    UiW_TitleTab(L, by + 8, "DECORATION", &r, 0, NULL);
    Card(&r);
    List(t, &r);
    /* a decoration's own words, as the game's description window shows them */
    if (sD.phase == NATIVE_DECOR_MENU_ITEMS)
        msg = sD.cursor < sD.count ? gDecorations[sD.items[sD.cursor]].description : gText_GoBackPrevMenu;
    n = Hints(h);
    UiField_MessageBox(t, L, R, B, msg, -1, sD.phase == NATIVE_DECOR_MENU_MESSAGE && sD.ready, NULL, h, n, r.ink, HIT_MESSAGE);
    if (sD.phase == NATIVE_DECOR_MENU_CONFIRM)
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sD.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input: the engine's own buttons */

static bool Ready(void)
{
    return sD.ready;
}

/* a row: the cursor walked there a press at a time, then A */
static void Choose(int idx, int cursor)
{
    int moves = idx - cursor;

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
        if (sD.ready)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || !sD.ready)
        return false;
    switch (sD.phase)
    {
    case NATIVE_DECOR_MENU_ACTIONS:
    case NATIVE_DECOR_MENU_CATEGORIES:
    case NATIVE_DECOR_MENU_ITEMS:
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index < RowCount())
        {
            Choose(hit->index, sD.cursor);
            return true;
        }
        break;
    case NATIVE_DECOR_MENU_CONFIRM:
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            Choose(hit->index, sD.yesNoCursor);
            return true;
        }
        break;
    case NATIVE_DECOR_MENU_MESSAGE:
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
    if (dy == 0 || NativeUi_PressesPending() || !sD.ready)
        return false;
    if (sD.phase != NATIVE_DECOR_MENU_ACTIONS && sD.phase != NATIVE_DECOR_MENU_CATEGORIES && sD.phase != NATIVE_DECOR_MENU_ITEMS)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

/* the list's scroll arrows stay out of the picture (the PC's lists' sheets: their tag is the same) */
static void HideObjects(uint32_t mask[4])
{
    u32 tiles[32];
    const struct OamData *oam = (const struct OamData *)OAM;

    PlayerPC_NativeUiItemTiles(tiles);
    for (int i = 0; i < 128; i++)
    {
        u32 tile = oam[i].tileNum;

        if ((tiles[tile >> 5] >> (tile & 31)) & 1)
            mask[i >> 5] |= 1u << (i & 31);
    }
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_PcDecor = {
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
