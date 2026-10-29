#ifdef ENABLE_NATIVE_UI
/*
 * The Battle Dome's info cards (battle_dome.c: a trainer's or a match's from
 * the tourney tree, the next opponent's before a round; not one of the 31
 * designed screens: the design's pieces): a trainer's card - the picture,
 * the class and name, the three and the game's three lines on them (their
 * chances, their style, what they emphasize) - or a match's - its round and
 * number, both sides with their pictures and three (the loser greyed once
 * decided) and how it went. The arrows the game shows go on to the next card
 * as the pad does; A or B goes back. Project-owned.
 */
#include "global.h"
#include "constants/battle_dome.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ARROW = UI_HIT_SCREEN,   /* index: 0 up, 1 down, 2 left, 3 right */
};

#define CARD_X 48
#define CARD_Y 28
#define CARD_W 544
#define CARD_H 304

static const u8 sText_Back[] = _("Back");
static const u8 sText_Next[] = _("Next card");
static const u16 sArrowKeys[] = {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT};
static const struct UiFilter sLost = {1.0f, 0, 0, 0.6f};   /* the game greys the loser */

static struct NativeDomeCard sC;

static bool Detect(void)
{
    struct NativeDomeCard c;

    return DomeCard_NativeUiState(&c);
}

static void Capture(void)
{
    DomeCard_NativeUiState(&sC);
    for (int i = 0; i < (sC.match ? 2 : 1); i++)
        UiGfx_PrepareTrainerPic(sC.sides[i].picId);
}

/* the game's line on a window (its first printed text), else NULL */
static const u8 *Line(u8 window, int index)
{
    const struct NativeUiPrinted *p;
    u32 n = TextPrinter_NativeUiTranscript(window, &p);

    return (u32)index < n ? p[index].text : NULL;
}

static void Text(int x, int y, const u8 *s, UiRGBA fg, int align)
{
    if (s != NULL)
        UiText_Draw(UI_FONT_NORMAL, x, y, s, fg, fg == UI_P_TEXT ? UI_P_TEXT_SH : UI_NONE, align, 0);
}

static void TrainerCard(int t)
{
    const struct NativeDomeCardSide *s = &sC.sides[0];
    const struct NativeUiPrinted *p;
    u32 n;

    UiG_Mask(UiM_Ellipse(150, 30), CARD_X + 34, CARD_Y + 186, UI_P_CREAM_LO);
    UiGfx_TrainerPicAffine(s->picId, CARD_X + 110, CARD_Y + 136, 256, 2, NULL);
    /* the three, with the names the game prints */
    for (int k = 0; k < 3; k++)
    {
        int y = CARD_Y + 60 + k * 50;

        UiG_Paint(UiM_Rounded4(300, 44, 4, 4, 4, 4), CARD_X + 220, y, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.4),
                  &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiGfx_MonIcon(s->species[k], 0, false, (t / 16) % 2, CARD_X + 230, y + 4, NULL);
        Text(CARD_X + 276, y + 12, Line(sC.monWindows[k], 0), UI_P_TEXT, UI_ALIGN_LEFT);
    }
    /* the game's three lines on them */
    UiG_Paint(UiM_Rounded4(CARD_W - 32, 82, 4, 4, 4, 4), CARD_X + 16, CARD_Y + 214, UI_P_PAPER,
              &(struct UiPaint){UI_P_CREAM_LO, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    n = TextPrinter_NativeUiTranscript(sC.flavorWindow, &p);
    for (u32 i = 0; i < n && i < 4; i++)
        Text(CARD_X + 30, CARD_Y + 222 + (p[i].y / 16) * 22, p[i].text, UI_P_TEXT, UI_ALIGN_LEFT);
}

static void MatchSide(int k, int t)
{
    const struct NativeDomeCardSide *s = &sC.sides[k];
    int cx = k == 0 ? CARD_X + 136 : CARD_X + CARD_W - 136;
    const struct UiFilter *f = s->lost ? &sLost : NULL;

    Text(cx, CARD_Y + 58, Line(k == 0 ? sC.leftWindow : sC.rightWindow, 0), s->lost ? UI_P_TEXT_MUTED : UI_P_TEXT,
         UI_ALIGN_CENTER);
    UiG_Mask(UiM_Ellipse(150, 28), cx - 75, CARD_Y + 186, UI_P_CREAM_LO);
    UiGfx_TrainerPicAffine(s->picId, cx, CARD_Y + 142, 256, 2, f);
    for (int m = 0; m < 3; m++)
        UiGfx_MonIcon(s->species[m], 0, false, s->lost ? 0 : (t / 16) % 2, cx - 64 + m * 44, CARD_Y + 212, f);
}

static void MatchCard(int t)
{
    MatchSide(0, t);
    MatchSide(1, t);
    UiItems_BigCaps(CARD_X + CARD_W / 2, CARD_Y + 120, "VS", 3, UI_HEX(0xE84838), UI_HEX(0x581010), UI_ALIGN_CENTER);
    UiG_Paint(UiM_Rounded4(CARD_W - 32, 36, 4, 4, 4, 4), CARD_X + 16, CARD_Y + 256, UI_P_PAPER,
              &(struct UiPaint){UI_P_CREAM_LO, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    Text(CARD_X + CARD_W / 2, CARD_Y + 266, Line(sC.winWindow, 0), UI_P_TEXT, UI_ALIGN_CENTER);
}

static void Arrows(int t)
{
    int bob = (t / 16) % 2;

    if (sC.phase != NATIVE_DOME_BROWSE)
        return;
    if (sC.arrows[0])
    {
        UiW_DownArrow(CARD_X + CARD_W / 2 - 4, CARD_Y - 14 - bob, UI_P_CURSOR, true);
        UiG_Hit(HIT_ARROW, 0, CARD_X + CARD_W / 2 - 20, CARD_Y - 22, 40, 22);
    }
    if (sC.arrows[1])
    {
        UiW_DownArrow(CARD_X + CARD_W / 2 - 4, CARD_Y + CARD_H + 6 + bob, UI_P_CURSOR, false);
        UiG_Hit(HIT_ARROW, 1, CARD_X + CARD_W / 2 - 20, CARD_Y + CARD_H, 40, 22);
    }
    if (sC.arrows[2])
    {
        UiCaps_Draw(CARD_X - 20 - bob, CARD_Y + CARD_H / 2 - 4, UI_CAPS_LEFT, UI_P_TEXT_W, UI_P_INK, UI_CAPS_OUTLINE,
                    UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_ARROW, 2, CARD_X - 30, CARD_Y + CARD_H / 2 - 20, 30, 40);
    }
    if (sC.arrows[3])
    {
        UiCaps_Draw(CARD_X + CARD_W + 12 + bob, CARD_Y + CARD_H / 2 - 4, UI_CAPS_RIGHT, UI_P_TEXT_W, UI_P_INK,
                    UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_ARROW, 3, CARD_X + CARD_W, CARD_Y + CARD_H / 2 - 20, 30, 40);
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    struct UiRamp band = UiRampDefault(sC.match ? UI_HEX(0xD85838) : UI_HEX(0x4878C8));
    const u8 *title = sC.match ? Line(sC.numberWindow, 0) : Line(sC.nameWindow, 0);

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x1C2232));
    if (!sC.ready && title == NULL)
    {
        UiG_Veil(NativeUi_ScreenFade());
        return;
    }
    UiG_Paint(UiM_Rounded4(CARD_W, CARD_H, 8, 8, 8, 8), CARD_X, CARD_Y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x101420), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(CARD_W - 16, 26, 5, 5, 5, 5), CARD_X + 8, CARD_Y + 8, band.base,
              &(struct UiPaint){band.ink, band.hi, band.lo, 0, 0, NULL, UI_NONE});
    if (title != NULL)
        UiText_Draw(UI_FONT_NORMAL, CARD_X + CARD_W / 2, CARD_Y + 12, title, UI_P_TEXT_W, band.ink, UI_ALIGN_CENTER, 0);
    if (sC.match)
        MatchCard(t);
    else
        TrainerCard(t);
    Arrows(t);
    if (sC.ready)
    {
        struct UiHintItem h[2];
        int n = 0;

        if (sC.arrows[0] || sC.arrows[1] || sC.arrows[2] || sC.arrows[3])
            h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Next};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, n, 10), by + bh - 22, h, n, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sC.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sC.ready || NativeUi_PressesPending())
        return true;
    if (button == 3 || (button == 1 && hit->id == UI_HIT_HINT && hit->index == UI_ACT_CANCEL))
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button == 1 && hit->id == HIT_ARROW && hit->index >= 0 && hit->index < 4)
    {
        NativeUi_QueuePress(sArrowKeys[hit->index]);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sC.ready || NativeUi_PressesPending())
        return true;
    if (sC.arrows[dy > 0 ? 0 : 1])
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_DomeCard = {
    .name = "dome_card",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
