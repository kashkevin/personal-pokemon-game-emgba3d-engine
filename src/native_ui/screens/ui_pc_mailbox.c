#ifdef ENABLE_NATIVE_UI
/*
 * 14 · PC storage: the player's PC's MAILBOX (player_pc.c) over the field, in
 * the PC's own pieces (ui_pc_items.c's teal, tab, card and rows; the field's
 * message and menu boxes): the PC's Mail in the list's order with each
 * sender's name and the Mail's own icon, then CANCEL; the chosen Mail on the
 * card (its icon, its name, who it is from, its first words); what to do with
 * it (READ, MOVE TO BAG, GIVE, CANCEL), the YES / NO before its words are lost
 * and the engine's lines. READ opens the letter (ui_mail.c), GIVE the party.
 * The engine keeps every rule and button: clicks and the wheel queue its own
 * presses. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "easy_chat.h"
#include "item.h"
#include "main.h"
#include "overworld.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/easy_chat.h"
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
};

#define ROW_H 22
#define VIS 10
#define LX 8          /* the Mail's card */
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
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Next[] = _("Next");
static const u8 sText_From[] = _("From ");

static struct NativeMailbox sM;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static u8 sFirstWords[NATIVE_MAILBOX_MAX][64];
static int sFirst;      /* the first row shown */

static struct UiRamp PcRamp(void)
{
    /* ui_storage.c's terminal teal, as the PC's other lists */
    return UiRampOf(UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.36), 0.38, 0.3, 0.62);
}

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativeMailbox m;

    return gMain.callback2 == CB2_Overworld && PlayerPC_NativeUiMailbox(&m);
}

static int RowCount(void)
{
    return sM.count + 1; /* the engine's CANCEL row */
}

static void Capture(void)
{
    u32 printed = 0;
    bool8 active = FALSE;
    int n;

    PlayerPC_NativeUiMailbox(&sM);
    for (int k = 0; k < sM.count; k++)
    {
        u8 *p = sFirstWords[k];
        int shown = 0;

        UiGfx_PrepareItemIcon(sM.items[k]);
        /* its first three words, as a line */
        *p = EOS;
        for (int w = 0; w < 9 && shown < 3; w++)
        {
            if (sM.words[k][w] == EC_EMPTY_WORD)
                continue;
            if (shown++ > 0)
                *p++ = CHAR_SPACE;
            p = CopyEasyChatWord(p, sM.words[k][w]);
        }
    }
    UiGfx_PrepareItemIcon(ITEM_LIST_END);
    sHasMsg = Window_NativeUiShown(0) && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &printed, &active);
    for (int k = 0; sHasMsg && k < (int)sizeof(sMsg) && sMsg[k] != EOS; k++)
        if (sMsg[k] == CHAR_NEWLINE)
            sMsg[k] = CHAR_SPACE;
    n = RowCount();
    if (sM.cursor < sFirst)
        sFirst = sM.cursor;
    if (sM.cursor >= sFirst + VIS)
        sFirst = sM.cursor - VIS + 1;
    if (sFirst > n - VIS)
        sFirst = n - VIS;
    if (sFirst < 0)
        sFirst = 0;
}

/* ---------------------------------------------------------------- drawing */

static void MailCard(const struct UiRamp *r)
{
    const struct UiMask *m = UiM_Rounded4(LW, LH, 4, 4, 4, 4);
    struct UiPattern dots = UiPat_Dots(8, 2, 0, 0, true);
    int cx = LX + (LW >> 1), iy = LY + 34;

    UiG_Paint(m, LX, LY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    UiCaps_Draw(cx, LY + 9, "MAILBOX", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
    UiG_Paint(UiM_Rounded4(LW - 16, LH - 34, 3, 3, 3, 3), LX + 8, LY + 26, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(LW - 32, 104, 3, 3, 3, 3), LX + 16, iy, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.55),
              &(struct UiPaint){UiMix(r->lo, r->ink, 0.3), UI_NONE, UI_NONE, 0, 0, &dots, UiMix(r->hi, UI_HEX(0xFFFFFF), 0.25)});
    if (sM.cursor < sM.count)
    {
        u8 name[64], from[32];
        int ry = iy + 136;
        const u8 *starts[3];
        int lengths[3], lines;

        UiGfx_ItemIconScaled(sM.items[sM.cursor], cx - 36, iy + 16, 3);
        UiItems_ItemName(name, sizeof(name), sM.items[sM.cursor]);
        UiText_Draw(UI_FONT_NORMAL, cx, iy + 112, name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        UiG_HLine(LX + 18, ry - 4, LW - 36, r->base);
        UiG_HLine(LX + 18, ry - 3, LW - 36, r->hi);
        from[0] = EOS;
        UiText_Append(from, sizeof(from), sText_From);
        UiText_Append(from, sizeof(from), sM.names[sM.cursor]);
        UiText_Draw(UI_FONT_NORMAL, LX + 20, ry + 2, from, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        lines = UiText_Wrap(sFirstWords[sM.cursor], LW - 40, UI_FONT_NORMAL, starts, lengths, 3);
        for (int i = 0; i < lines; i++)
            UiText_DrawN(UI_FONT_NORMAL, LX + 20, ry + 24 + i * 16, starts[i], lengths[i], UI_P_TEXT_MUTED, UI_P_TEXT_SH,
                         UI_ALIGN_LEFT, 0);
    }
    else
    {
        UiGfx_ItemIconScaled(ITEM_LIST_END, cx - 36, iy + 16, 3);
        UiText_Draw(UI_FONT_NORMAL, cx, iy + 112, gText_Cancel2, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
}

static void List(int t, const struct UiRamp *r)
{
    int sx = PX + 8, sy = PY + 26, n = RowCount();
    UiRGBA pc = UiMix(UI_P_TYPES_ICE, UI_HEX(0xFFFFFF), 0.2);
    bool listUp = sM.phase == NATIVE_MAILBOX_LIST || sM.phase == NATIVE_MAILBOX_OPENING;
    u8 count[24];

    UiG_Paint(UiM_Rounded4(PW, PH, 4, 4, 4, 4), PX, PY, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 2, NULL, UI_NONE});
    UiCaps_Draw(PX + 12, PY + 9, "MAIL IN THE PC", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    count[0] = EOS;
    UiText_AppendNumber(count, sizeof(count), sM.count, 1, false);
    UiText_AppendAscii(count, sizeof(count), " / ");
    UiText_AppendNumber(count, sizeof(count), NATIVE_MAILBOX_MAX, 1, false);
    UiText_Draw(UI_FONT_SMALL, PX + PW - 22, PY + 6, count, UI_P_TEXT_W, r->ink, UI_ALIGN_RIGHT, 0);
    UiG_Paint(UiM_Rounded4(PW - 30, PH - 34, 2, 2, 2, 2), sx, sy, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int k = 0; k < VIS; k++)
    {
        int idx = sFirst + k, ry = sy + 4 + k * (ROW_H + 1);
        bool sel = idx == sM.cursor;

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
        if (idx < sM.count)
        {
            u8 name[64];

            UiGfx_ItemIcon(sM.items[idx], sx + 12, ry - 1, NULL);
            UiG_Text(sx + 40, ry + 2, sM.names[idx]);
            UiItems_ItemName(name, sizeof(name), sM.items[idx]);
            UiText_Draw(UI_FONT_SMALL, sx + PW - 44, ry + 5, name, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        else
        {
            UiGfx_Icon("return_blue", sx + 14, ry + 5, 0);
            UiG_Text(sx + 40, ry + 2, gText_Cancel2);
        }
        if (sel && listUp)
            UiW_Cursor(sx - 1, ry + 5, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ROW, idx, sx + 4, ry, PW - 38, ROW_H);
    }
    UiW_Scrollbar(PX + PW - 18, sy + 2, PH - 38, sFirst, n, VIS, r);
}

static int Hints(struct UiHintItem *h)
{
    int n = 0;

    if (!sM.ready)
        return 0;
    switch (sM.phase)
    {
    case NATIVE_MAILBOX_LIST:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_MAILBOX_OPTIONS:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_MAILBOX_CONFIRM:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        break;
    case NATIVE_MAILBOX_MESSAGE:
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
    UiW_TitleTab(L, by + 8, "MAILBOX", &r, 0, NULL);
    MailCard(&r);
    List(t, &r);
    n = Hints(h);
    UiField_MessageBox(t, L, R, B, sHasMsg ? sMsg : NULL, -1, sM.phase == NATIVE_MAILBOX_MESSAGE && sM.ready, NULL, h, n,
                       r.ink, HIT_MESSAGE);
    if (sM.phase == NATIVE_MAILBOX_OPTIONS)
    {
        struct UiMenuItem items[NATIVE_MAILBOX_OPTIONS_MAX];
        struct UiMenuOpt o = {0};

        for (int i = 0; i < sM.optionCount; i++)
            items[i] = (struct UiMenuItem){sM.options[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 120;
        UiW_MenuBox(R - 8, B - UI_FIELD_MENU_GAP, items, sM.optionCount, sM.menuCursor, &o, NULL);
    }
    else if (sM.phase == NATIVE_MAILBOX_CONFIRM)
    {
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sM.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input: the engine's own buttons */

static bool Ready(void)
{
    return sM.ready;
}

/* a row of a list or a menu: its cursor walked there a press at a time (none of them wraps), then A */
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
        if (sM.ready)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || !sM.ready)
        return false;
    switch (sM.phase)
    {
    case NATIVE_MAILBOX_LIST:
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index < RowCount())
        {
            Choose(hit->index, sM.cursor);
            return true;
        }
        break;
    case NATIVE_MAILBOX_OPTIONS:
        if (hit->id == UI_HIT_MENU && hit->index >= 0 && hit->index < sM.optionCount)
        {
            Choose(hit->index, sM.menuCursor);
            return true;
        }
        break;
    case NATIVE_MAILBOX_CONFIRM:
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            Choose(hit->index, sM.menuCursor);
            return true;
        }
        break;
    case NATIVE_MAILBOX_MESSAGE:
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
    if (dy == 0 || NativeUi_PressesPending() || !sM.ready)
        return false;
    if (sM.phase != NATIVE_MAILBOX_LIST && sM.phase != NATIVE_MAILBOX_OPTIONS)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

/* the list's scroll arrows stay out of the picture (the item lists' sheets: their tag is the same) */
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

const struct UiScreen gUiScreen_PcMailbox = {
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
