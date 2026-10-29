#ifdef ENABLE_NATIVE_UI
/*
 * The POKéDEX diploma (not one of the 31 designed screens; the design's
 * pieces): the design's certificate - a gold-framed paper, the HOENN or the
 * NATIONAL POKéDEX's title, and the game's own text as it prints it (the
 * player's name in red, the lines centred, GAME FREAK's signature at the
 * right in red) with a seal. A, B or a click close it as the game does.
 * Project-owned.
 */
#include "global.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_DIPLOMA = UI_HIT_SCREEN,
};

#define PAPER_X 120
#define PAPER_Y 16
#define PAPER_W 400
#define PAPER_H 328

static const u8 sText_OK[] = _("OK");

static u8 sText[UI_MESSAGE_COPY];
static bool sHasText;
static bool8 sNational, sReady;

static bool Detect(void)
{
    bool8 national, ready;

    return Diploma_NativeUiState(&national, &ready);
}

static void Capture(void)
{
    u32 printed;
    bool8 active;

    Diploma_NativeUiState(&sNational, &sReady);
    sHasText = TextPrinter_NativeUiWindowText(0, sText, sizeof(sText), &printed, &active);
}

/* a line of the game's text: before a {CLEAR n} (the label's gap) the label, after it the value; a {CLEAR_TO x} at its
 * start ends it at the right (the signature); else it is centred */
static void TextLine(int cx, int left, int right, int y, const u8 *s, int len)
{
    u8 a[64], b[64];
    int na = 0, nb = 0, split = -1;
    bool rightAligned = false;

    for (int i = 0; i < len; i++)
    {
        if (s[i] == EXT_CTRL_CODE_BEGIN && i + 2 < len && (s[i + 1] == EXT_CTRL_CODE_CLEAR || s[i + 1] == EXT_CTRL_CODE_CLEAR_TO))
        {
            if (s[i + 1] == EXT_CTRL_CODE_CLEAR_TO && na == 0)
                rightAligned = true;
            else if (split < 0)
                split = na;
            i += 2;
            continue;
        }
        if (split < 0 && na < (int)sizeof(a) - 1)
            a[na++] = s[i];
        else if (split >= 0 && nb < (int)sizeof(b) - 1)
            b[nb++] = s[i];
    }
    a[na] = EOS;
    b[nb] = EOS;
    if (split >= 0)
    {
        int wa = UiText_Width(UI_FONT_NORMAL, a, 0), wb = UiText_Width(UI_FONT_NORMAL, b, 0), x = cx - (wa + 12 + wb) / 2;

        UiText_Draw(UI_FONT_NORMAL, x, y, a, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, x + wa + 12, y, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else if (rightAligned)
    {
        UiText_Draw(UI_FONT_NORMAL, right, y, a, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
    else
    {
        UiText_Draw(UI_FONT_NORMAL, cx, y, a, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    (void)left;
}

static void Draw(int t)
{
    struct UiRamp gold = UiRampDefault(UI_HEX(0xD8A838));
    int bx, by, bw, bh, cx = PAPER_X + PAPER_W / 2, y;

    (void)t;
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x283428));
    /* the paper in its gold frame, a thin rule inside */
    UiG_Paint(UiM_Rounded4(PAPER_W, PAPER_H, 6, 6, 6, 6), PAPER_X, PAPER_Y, gold.base,
              &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(PAPER_W - 24, PAPER_H - 24, 3, 3, 3, 3), PAPER_X + 12, PAPER_Y + 12, UI_HEX(0xFBF6E6),
              &(struct UiPaint){gold.lo, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Rect(PAPER_X + 24, PAPER_Y + 24, PAPER_W - 48, 1, gold.lo);
    UiG_Rect(PAPER_X + 24, PAPER_Y + PAPER_H - 25, PAPER_W - 48, 1, gold.lo);
    UiItems_BigCaps(cx, PAPER_Y + 36, "DIPLOMA", 3, gold.ink, UI_NONE, UI_ALIGN_CENTER);
    UiCaps_Draw(cx, PAPER_Y + 80, sNational ? "NATIONAL POK" UI_CAPS_E_ACUTE "DEX" : "HOENN POK" UI_CAPS_E_ACUTE "DEX",
                gold.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    /* the game's text, a line at a time */
    y = PAPER_Y + 104;
    if (sHasText)
    {
        const u8 *s = sText;

        while (*s != EOS)
        {
            int len = 0;

            while (s[len] != EOS && s[len] != CHAR_NEWLINE)
                len++;
            if (len > 0)
                TextLine(cx, PAPER_X + 40, PAPER_X + PAPER_W - 72, y, s, len);
            y += 20;
            s += len;
            if (*s == CHAR_NEWLINE)
                s++;
        }
    }
    /* the seal */
    UiG_Mask(UiM_Ellipse(44, 44), PAPER_X + PAPER_W - 64, PAPER_Y + PAPER_H - 68, UI_HEX(0xC83828));
    UiG_Mask(UiM_Ellipse(30, 30), PAPER_X + PAPER_W - 57, PAPER_Y + PAPER_H - 61, UI_HEX(0xE85848));
    UiG_Hit(HIT_DIPLOMA, 0, bx, by, bw, bh);
    if (sReady)
    {
        struct UiHintItem ok = {UI_ACT_CONFIRM, sText_OK};

        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(&ok, 1, 10), by + bh - 28, &ok, 1, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sReady;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (sReady && !NativeUi_PressesPending() && (button == 1 || button == 3))
        NativeUi_QueuePress(button == 1 ? A_BUTTON : B_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Diploma = {
    .name = "diploma",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
