#ifdef ENABLE_NATIVE_UI
/*
 * The Pokéblock case (not one of the 31 designed screens; the design's pieces):
 * the case (pokeblock.c) from the Bag, the Safari Zone's battles and feeders
 * and the contest lady, as a screen of its own - the blocks on a list beside the
 * case, each in its colour with its level, the chosen block's colour, name,
 * level, which flavours it has (the game shows no amounts) and its feel; the
 * game's actions (USE / TOSS / CANCEL...) in the design's menu, a toss's
 * question and messages in the message box. The engine keeps every rule:
 * clicks, the wheel and a right click queue its presses (a row is reached
 * with UP and DOWN, then A; SELECT starts a move). Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_MESSAGE,
};

#define LIST_X 268
#define LIST_Y 44
#define ROW_H 28
#define LIST_W 356

static const u8 sText_Use[] = _("Choose");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Place[] = _("Place");
static const u8 sText_Feel[] = _("FEEL");
static const u8 sText_Lv[] = _("Lv.");

/* each block's colour (PBLOCK_CLR_*: none, red, blue, pink, green, yellow, purple, indigo, brown, light blue, olive,
 * gray, black, white, gold) and each flavour's (spicy red, dry blue, sweet pink, bitter green, sour yellow) */
static const UiRGBA sBlockColors[] = {
    UI_HEX(0x808080), UI_HEX(0xE04848), UI_HEX(0x4878E0), UI_HEX(0xF090C0), UI_HEX(0x50B850), UI_HEX(0xF0D040),
    UI_HEX(0x9858C8), UI_HEX(0x5058A8), UI_HEX(0x9C6838), UI_HEX(0x78C8F0), UI_HEX(0x98A048), UI_HEX(0x989898),
    UI_HEX(0x383838), UI_HEX(0xF8F8F8), UI_HEX(0xE8B830),
};
static const UiRGBA sFlavorColors[5] = {
    UI_HEX(0xE04848), UI_HEX(0x4878E0), UI_HEX(0xF090C0), UI_HEX(0x50B850), UI_HEX(0xF0D040),
};

static struct NativePokeblockCase sC;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg, sMsgPrinting;
static u32 sMsgPrinted;

static bool Detect(void)
{
    struct NativePokeblockCase c;

    return Pokeblock_NativeUiCase(&c);
}

static void Capture(void)
{
    bool8 active = FALSE;

    Pokeblock_NativeUiCase(&sC);
    sHasMsg = (sC.phase == NATIVE_PBLOCK_MESSAGE || sC.phase == NATIVE_PBLOCK_YES_NO)
           && TextPrinter_NativeUiWindowText(sC.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
}

static UiRGBA BlockColor(u8 color)
{
    return color < ARRAY_COUNT(sBlockColors) ? sBlockColors[color] : sBlockColors[0];
}

/* a Pokéblock: a rounded block in its colour with a lit top */
static void Block(int x, int y, int size, u8 color)
{
    UiRGBA c = BlockColor(color);

    UiG_Paint(UiM_Rounded4(size, size, 3, 3, 3, 3), x, y, c,
              &(struct UiPaint){UiMix(c, UI_HEX(0x000000), 0.45), UiMix(c, UI_HEX(0xFFFFFF), 0.45),
                                UiMix(c, UI_HEX(0x000000), 0.25), 0, 2, NULL, UI_NONE});
}

static void Level(u8 *out, u32 size, u8 level)
{
    out[0] = EOS;
    UiText_Append(out, size, sText_Lv);
    UiText_AppendNumber(out, size, level, 1, false);
}

/* the case and the chosen block: its colour, name, level, flavours and feel */
static void Detail(void)
{
    int x = 16, y = 44, w = 236, h = 252;
    u8 lv[12];

    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    if (sC.cursor >= sC.count)
    {
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 112, sC.stow, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        return;
    }
    {
        const struct NativePokeblock *b = &sC.blocks[sC.cursor];
        u8 feel[8];

        Block(x + (w >> 1) - 28, y + 16, 56, b->color);
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 80, b->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        Level(lv, sizeof(lv), b->level);
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 98, lv, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        UiG_HLine(x + 16, y + 120, w - 32, UI_P_TEXT_MUTED);
        for (int i = 0; i < 5; i++)
        {
            int ry = y + 128 + i * 20;
            bool has = b->flavors[i] > 0;

            UiG_Paint(UiM_Rounded4(12, 12, 2, 2, 2, 2), x + 24, ry + 2, has ? sFlavorColors[i] : UI_HEX(0xD8D8D0),
                      &(struct UiPaint){has ? UiMix(sFlavorColors[i], UI_HEX(0x000000), 0.4) : UI_HEX(0xB0B0A8),
                                        UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            UiText_Draw(UI_FONT_NORMAL, x + 44, ry, sC.flavorNames[i], has ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_P_TEXT_SH,
                        UI_ALIGN_LEFT, 0);
        }
        UiG_HLine(x + 16, y + 230, w - 32, UI_P_TEXT_MUTED);
        UiText_Draw(UI_FONT_NORMAL, x + 24, y + 234, sText_Feel, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        feel[0] = EOS;
        UiText_AppendNumber(feel, sizeof(feel), b->feel, 1, false);
        UiText_Draw(UI_FONT_NORMAL, x + w - 24, y + 234, feel, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
}

/* the list: the rows shown from the first, the cursor's row lit, a block being moved in red */
static void List(int t)
{
    int rows = sC.shown, total = sC.count + 1;

    UiW_DialogBox(LIST_X - 8, LIST_Y, LIST_W + 16, rows * ROW_H + 16, NULL, UI_NONE, NULL);
    for (int r = 0; r < rows; r++)
    {
        int i = sC.scroll + r, y = LIST_Y + 8 + r * ROW_H;

        if (i >= total)
            break;
        if (i == sC.cursor)
            UiG_Paint(UiM_Rounded4(LIST_W - 8, ROW_H - 2, 3, 3, 3, 3), LIST_X, y, UiWithAlpha(UI_P_CURSOR, 60),
                      &(struct UiPaint){UI_P_CURSOR, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (sC.phase == NATIVE_PBLOCK_SWAP && i == sC.swapFrom)
            UiG_Paint(UiM_Rounded4(LIST_W - 8, ROW_H - 2, 3, 3, 3, 3), LIST_X, y, UiWithAlpha(UI_HEX(0xE04848), 60),
                      &(struct UiPaint){UI_HEX(0xC03030), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (i < sC.count)
        {
            u8 lv[12];

            Block(LIST_X + 8, y + 5, 16, sC.blocks[i].color);
            UiText_Draw(UI_FONT_NORMAL, LIST_X + 32, y + 5, sC.blocks[i].name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            Level(lv, sizeof(lv), sC.blocks[i].level);
            UiText_Draw(UI_FONT_NORMAL, LIST_X + LIST_W - 24, y + 5, lv, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        else
        {
            UiText_Draw(UI_FONT_NORMAL, LIST_X + 32, y + 5, sC.stow, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        UiG_Hit(HIT_ROW, i, LIST_X, y, LIST_W - 8, ROW_H - 2);
    }
    if (sC.scroll > 0)
        UiW_DownArrow(LIST_X + (LIST_W >> 1), LIST_Y - 6 - ((t / 16) % 2), UI_P_CURSOR, true);
    if (sC.scroll + rows < total)
        UiW_DownArrow(LIST_X + (LIST_W >> 1), LIST_Y + rows * ROW_H + 14 + ((t / 16) % 2), UI_P_CURSOR, false);
}

static void Draw(int t)
{
    struct UiRamp tab = {UI_HEX(0xF0A8C8), UI_HEX(0xD87098), UI_HEX(0xA84870), UI_HEX(0x501830)};
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x283048));
    UiW_TitleTab(0, 8, "POK" UI_CAPS_E_ACUTE "BLOCK CASE", &tab, 0, NULL);
    Detail();
    List(t);
    if (sC.phase == NATIVE_PBLOCK_ACTIONS)
    {
        struct UiMenuItem items[4];
        struct UiMenuOpt o = {0};

        for (int i = 0; i < sC.actionCount; i++)
            items[i] = (struct UiMenuItem){sC.actions[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 96;
        UiW_MenuBox(bx + bw - 8, by + bh - 40, items, sC.actionCount, sC.actionCursor, &o, NULL);
    }
    if (sC.phase == NATIVE_PBLOCK_MESSAGE || sC.phase == NATIVE_PBLOCK_YES_NO)
    {
        UiField_MessageBox(t, bx, bx + bw, by + bh, sHasMsg ? sMsg : NULL, sMsgPrinting ? (int)sMsgPrinted : -1,
                           sC.phase == NATIVE_PBLOCK_MESSAGE && sC.ready, NULL, NULL, 0, UI_P_FIELD_INK, HIT_MESSAGE);
        if (sC.phase == NATIVE_PBLOCK_YES_NO)
            UiW_YesNo(bx + bw - 8, by + bh - UI_FIELD_MENU_GAP, sC.actionCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sC.ready && (sC.phase == NATIVE_PBLOCK_LIST || sC.phase == NATIVE_PBLOCK_SWAP))
    {
        struct UiHintItem h[3];
        int n = 0;

        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sC.phase == NATIVE_PBLOCK_SWAP ? sText_Place : sText_Use};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Close};
        if (sC.phase == NATIVE_PBLOCK_LIST)
            h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Move};
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, n, 10), by + bh - 26, h, n, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sC.ready;
}

static void Goto(int from, int to)
{
    if (to != from)
        NativeUi_QueuePresses(to > from ? DPAD_DOWN : DPAD_UP, abs(to - from));
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sC.ready || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    switch (sC.phase)
    {
    case NATIVE_PBLOCK_LIST:
    case NATIVE_PBLOCK_SWAP:
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index <= sC.count)
        {
            Goto(sC.cursor, hit->index);
            NativeUi_QueuePress(sC.phase == NATIVE_PBLOCK_SWAP ? SELECT_BUTTON : A_BUTTON);
        }
        return true;
    case NATIVE_PBLOCK_ACTIONS:
        if (hit->id == UI_HIT_MENU && hit->index >= 0 && hit->index < sC.actionCount)
        {
            Goto(sC.actionCursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case NATIVE_PBLOCK_YES_NO:
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            Goto(sC.actionCursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case NATIVE_PBLOCK_MESSAGE:
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sC.ready || NativeUi_PressesPending()
     || (sC.phase != NATIVE_PBLOCK_LIST && sC.phase != NATIVE_PBLOCK_SWAP && sC.phase != NATIVE_PBLOCK_ACTIONS))
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_PokeblockCase = {
    .name = "pokeblock_case",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
