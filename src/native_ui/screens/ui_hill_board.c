#ifdef ENABLE_NATIVE_UI
/*
 * Trainer Hill's time board (not one of the 31 designed screens; the design's
 * pieces): the board at the Trainer Hill entrance (battle_records.c,
 * ShowTrainerHillRecords) as a board of its own under an evening sky - TIME
 * BOARD on its tab, each of the four tag matches on a row with its best time
 * as the game prints it (trainer_hill.c), and A or B to close it as the game
 * waits for (a click is A, a right click B). The Trainer Tower's board (FRLG)
 * keeps the game's picture. Project-owned.
 */
#include <string.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_BOARD = UI_HIT_SCREEN,
};

static const u8 sText_Close[] = _("Close");

static struct NativeHillBoard sH;

static bool Detect(void)
{
    struct NativeHillBoard h;

    return BattleRecords_NativeUiHillBoard(&h);
}

static void Capture(void)
{
    BattleRecords_NativeUiHillBoard(&sH);
}

/* the sky over the hill: dusk blue to violet, in bands */
static void Sky(int x, int y, int w, int h)
{
    const int bands = 18;

    for (int i = 0; i < bands; i++)
    {
        int y0 = y + h * i / bands, y1 = y + h * (i + 1) / bands;

        UiG_Rect(x, y0, w, y1 - y0, UiMix(UI_HEX(0x1C2A4A), UI_HEX(0x5A3C68), (double)i / (bands - 1)));
    }
}

static void Draw(int t)
{
    struct UiRamp tab = {UI_HEX(0xF0C870), UI_HEX(0xD8A040), UI_HEX(0xA87020), UI_HEX(0x503010)};
    int bx, by, bw, bh, b[4];
    int x = 92, y = 52, w = 456, rowH = 62;

    (void)t;
    UiG_Bleed(&bx, &by, &bw, &bh);
    Sky(bx, by, bw, bh);
    UiW_TitleTab(0, 8, "TIME BOARD", &tab, 0, NULL);
    UiW_DialogBox(x, y, w, rowH * NATIVE_HILL_MODES + 20, NULL, UI_NONE, b);
    for (int i = 0; i < NATIVE_HILL_MODES; i++)
    {
        int ry = b[1] + 4 + i * rowH;

        if (i > 0)
            UiG_HLine(b[0] + 8, ry - 6, b[2] - 16, UI_P_TEXT_MUTED);
        if (sH.modes[i] != NULL)
            UiText_Draw(UI_FONT_NORMAL, b[0] + 12, ry + 4, sH.modes[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, b[0] + b[2] - 12, ry + 26, sH.times[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
    if (sH.ready)
    {
        struct UiHintItem h[1] = {{UI_ACT_CONFIRM, sText_Close}};

        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, 1, 10), by + bh - 26, h, 1, true, 10);
    }
    UiG_Hit(HIT_BOARD, 0, bx, by, bw, bh);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sH.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (!sH.ready || NativeUi_PressesPending())
        return true;
    if (button == 1)
        NativeUi_QueuePress(A_BUTTON);
    else if (button == 3)
        NativeUi_QueuePress(B_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_HillBoard = {
    .name = "hill_board",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
