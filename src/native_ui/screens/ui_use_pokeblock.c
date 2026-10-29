#ifdef ENABLE_NATIVE_UI
/*
 * The Pokéblock's condition menu (not one of the 31 designed screens; the
 * design's pieces): which Pokémon gets the block, then what it enhanced
 * (use_pokeblock.c) - the party on a list beside the chosen Pokémon's picture,
 * name, nature and sheen, and its five conditions in the PokéNav's condition
 * graph, which grows to its new conditions after the block, the conditions it
 * raised marked; "X gets a POKéBLOCK?" and its YES / NO, and the enhancements
 * one by one, in the message box. The engine keeps every rule: a click on a row
 * reaches it with UP and DOWN and presses A, on YES / NO the same, A or a click
 * goes on from a message, the wheel moves the cursor, a right click is B.
 * Project-owned.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_MESSAGE,
};

#define LIST_X 392
#define LIST_Y 48
#define ROW_H 32
#define LIST_W 232
#define GRAPH_CX 196
#define GRAPH_CY 214
#define GRAPH_R 70

static const u8 sText_Cancel[] = _("CANCEL");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Nature[] = _("NATURE");

static struct NativeUsePokeblock sU;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg, sMsgPrinting, sPicReady;
static u32 sMsgPrinted;
static double sShown[5];     /* the graph as drawn: it grows to the conditions as the game's does */
static int sShownFor = -1;   /* the row sShown belongs to */

static bool Detect(void)
{
    struct NativeUsePokeblock u;

    return UsePokeblock_NativeUiState(&u);
}

static void Capture(void)
{
    bool8 active = FALSE;

    UsePokeblock_NativeUiState(&sU);
    sHasMsg = (sU.phase == NATIVE_UPB_ASK || sU.phase == NATIVE_UPB_MESSAGE || sU.phase == NATIVE_UPB_ENHANCED)
           && TextPrinter_NativeUiWindowText(sU.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sPicReady = false;
    if (sU.cursor < sU.count)
    {
        const struct NativeUsePokeblockMon *m = &sU.mons[sU.cursor];

        sPicReady = UiGfx_PrepareMonPic(m->species, m->personality, m->shiny, m->egg);
    }
    /* another Pokémon: its graph as it is; the same one: toward its conditions, a few points a frame */
    if (sShownFor != sU.cursor)
    {
        for (int i = 0; i < 5; i++)
            sShown[i] = sU.values[i];
        sShownFor = sU.cursor;
    }
    for (int i = 0; i < 5; i++)
    {
        double d = sU.values[i] - sShown[i];

        sShown[i] += fabs(d) <= 3 ? d : (d > 0 ? 3 : -3);
    }
}

/* the chosen Pokémon: its picture in a frame, name, nature and sheen's sparkles, over its condition graph */
static void Chosen(void)
{
    int x = 16, y = 44, w = 360, h = 276;
    u8 values[5];

    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    if (sU.cursor >= sU.count)
    {
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 128, sText_Cancel, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        return;
    }
    {
        const struct NativeUsePokeblockMon *m = &sU.mons[sU.cursor];
        static const UiRGBA sSheen[3] = {UI_HEX(0x9CC8F0), UI_HEX(0xE0F0FF), UI_HEX(0xFFFFFF)};

        UiG_Paint(UiM_Rounded4(76, 76, 3, 3, 3, 3), x + 12, y + 10, UI_HEX(0xE8F4F8),
                  &(struct UiPaint){UI_HEX(0x7090A8), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (sPicReady)
        {
            UiG_Save();
            UiG_Clip(x + 13, y + 11, 74, 74);
            UiGfx_MonPic(m->species, m->personality, m->shiny, m->egg, 0, x + 50, y + 82, NULL);
            UiG_Restore();
        }
        UiText_Draw(UI_FONT_NORMAL, x + 100, y + 14, m->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        if (sU.nature != NULL)
        {
            UiText_Draw(UI_FONT_SMALL, x + 100, y + 36, sText_Nature, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
            UiText_Draw(UI_FONT_NORMAL, x + 100, y + 48, sU.nature, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        UiCaps_Draw(x + w - 16, y + 14, "SHEEN", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        for (int i = 0; i < 10; i++)
        {
            int kx = x + w - 20 - (i % 5) * 10, ky = y + 32 + (i / 5) * 10;

            if (i < sU.sparkles)
                UiMon_Sparkle(kx, ky, false, sSheen);
            else
                UiG_Put(kx, ky, UI_HEX(0xA0B0C0));
        }
    }
    for (int i = 0; i < 5; i++)
        values[i] = (u8)(sShown[i] + 0.5);
    UiMon_ConditionGraph(GRAPH_CX, GRAPH_CY, GRAPH_R, values);
    /* the conditions the block raised: a mark by each one's name */
    for (int i = 0; i < 5; i++)
    {
        double a = (-90 + 72 * i) * 3.14159265358979 / 180.0;
        int lx = (int)(GRAPH_CX + cos(a) * GRAPH_R * 1.28), ly = (int)(GRAPH_CY + sin(a) * GRAPH_R * 1.24);
        int tri[] = {0, 6, 4, 0, 8, 6};

        if (sU.enhanced[i])
            UiG_Paint(UiM_Poly(9, 7, tri, 3), lx + 26, ly - 3, UI_HEX(0x48B048),
                      &(struct UiPaint){UI_HEX(0x206820), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    }
}

/* the party, the chosen row lit, CANCEL last */
static void List(void)
{
    int rows = sU.count + 1;

    UiW_DialogBox(LIST_X - 8, LIST_Y - 4, LIST_W + 16, rows * ROW_H + 16, NULL, UI_NONE, NULL);
    for (int i = 0; i < rows; i++)
    {
        int y = LIST_Y + 4 + i * ROW_H;

        if (i == sU.cursor)
            UiG_Paint(UiM_Rounded4(LIST_W, ROW_H - 4, 3, 3, 3, 3), LIST_X, y, UiWithAlpha(UI_P_CURSOR, 60),
                      &(struct UiPaint){UI_P_CURSOR, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        if (i < sU.count)
        {
            const struct NativeUsePokeblockMon *m = &sU.mons[i];

            UiGfx_MonIconScaled(m->species, m->personality, m->egg, 0, LIST_X + 4, y - 6, 1);
            UiText_Draw(UI_FONT_NORMAL, LIST_X + 42, y + 6, m->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        else
        {
            UiText_Draw(UI_FONT_NORMAL, LIST_X + 42, y + 6, sText_Cancel, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        UiG_Hit(HIT_ROW, i, LIST_X, y, LIST_W, ROW_H - 4);
    }
}

static void Draw(int t)
{
    struct UiRamp tab = {UI_HEX(0x90D8F0), UI_HEX(0x58A8D0), UI_HEX(0x3878A0), UI_HEX(0x183850)};
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x203040));
    UiW_TitleTab(0, 8, "CONDITION", &tab, 0, NULL);
    Chosen();
    List();
    if (sHasMsg)
    {
        bool waiting = sU.ready && (sU.phase == NATIVE_UPB_MESSAGE || sU.phase == NATIVE_UPB_ENHANCED);

        UiField_MessageBox(t, bx, bx + bw, by + bh, sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, waiting, NULL, NULL, 0,
                           UI_P_FIELD_INK, HIT_MESSAGE);
        if (sU.phase == NATIVE_UPB_ASK && sU.ready)
            UiW_YesNo(bx + bw - 8, by + bh - UI_FIELD_MENU_GAP, sU.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sU.ready)
    {
        struct UiHintItem h[2];
        int n = 0;

        if (sU.phase == NATIVE_UPB_CHOOSE)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        }
        else
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        }
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, n, 10), by + bh - 26, h, n, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sU.ready;
}

static void Goto(int from, int to)
{
    if (to != from)
        NativeUi_QueuePresses(to > from ? DPAD_DOWN : DPAD_UP, abs(to - from));
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sU.ready || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    switch (sU.phase)
    {
    case NATIVE_UPB_CHOOSE:
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index <= sU.count)
        {
            Goto(sU.cursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case NATIVE_UPB_ASK:
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            Goto(sU.yesNoCursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case NATIVE_UPB_MESSAGE:
    case NATIVE_UPB_RESULTS:
    case NATIVE_UPB_ENHANCED:
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sU.ready || NativeUi_PressesPending() || sU.phase != NATIVE_UPB_CHOOSE)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_UsePokeblock = {
    .name = "use_pokeblock",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
