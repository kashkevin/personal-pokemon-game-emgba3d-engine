#ifdef ENABLE_NATIVE_UI
/*
 * A contest painting (not one of the 31 designed screens; the design's
 * pieces): the painting stays the game's picture - its frame, the winner's
 * portrait as the artist painted it - and its caption (the Contest Hall's
 * "... RANK winner", a museum's line) is the design's museum label where the
 * game prints it, with the OK the game waits for. The artist's own painting
 * has no caption. A click closes it as A does. Project-owned.
 */
#include "global.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_PAINTING = UI_HIT_SCREEN,
};

static const u8 sText_OK[] = _("OK");

static u8 sCaption[UI_MESSAGE_COPY];
static bool sHasCaption;
static bool8 sReady;

static bool Detect(void)
{
    u8 window;
    bool8 ready;

    return ContestPainting_NativeUiState(&window, &ready);
}

static void Capture(void)
{
    u8 window = WINDOW_NONE;
    u32 printed;
    bool8 active;

    ContestPainting_NativeUiState(&window, &sReady);
    sHasCaption = window != WINDOW_NONE && TextPrinter_NativeUiWindowText(window, sCaption, sizeof(sCaption), &printed, &active);
}

/* an engine string's lines (the game's newlines), each centred on cx */
static void CentredLines(int cx, int y, const u8 *s, int lineH)
{
    u8 line[64];

    while (s != NULL && *s != EOS)
    {
        int n = 0;

        while (*s != EOS && *s != CHAR_NEWLINE && n < (int)sizeof(line) - 1)
            line[n++] = *s++;
        line[n] = EOS;
        UiText_Draw(UI_FONT_NORMAL, cx, y, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        if (*s == CHAR_NEWLINE)
            s++;
        y += lineH;
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh;

    (void)t;
    UiG_Bleed(&bx, &by, &bw, &bh);
    if (sHasCaption)
    {
        /* the label where the game prints its caption (2, 14; 26 x 4 tiles) at twice its size: paper in a dark frame */
        int x = 104, y = 238, w = 432, h = 76;

        UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_HEX(0x3A2A1A),
                  &(struct UiPaint){UI_HEX(0x1E140A), UI_HEX(0x6A4A2A), UI_HEX(0x24180C), 0, 0, NULL, UI_NONE});
        UiG_Paint(UiM_Rounded4(w - 12, h - 12, 2, 2, 2, 2), x + 6, y + 6, UI_P_CREAM,
                  &(struct UiPaint){UI_HEX(0x8A6A40), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
        CentredLines(x + w / 2, y + 20, sCaption, 18);
    }
    if (sReady)
    {
        struct UiHintItem ok = {UI_ACT_CONFIRM, sText_OK};

        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(&ok, 1, 10), by + bh - 28, &ok, 1, true, 10);
    }
    UiG_Hit(HIT_PAINTING, 0, bx, by, bw, bh);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sReady;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if ((button == 1 || button == 3) && sReady && !NativeUi_PressesPending())
        NativeUi_QueuePress(button == 1 ? A_BUTTON : B_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the painting stays: the label is drawn over the game's caption */
    return UI_PICTURE_KEEP;
}

const struct UiScreen gUiScreen_ContestPainting = {
    .name = "contest_painting",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
