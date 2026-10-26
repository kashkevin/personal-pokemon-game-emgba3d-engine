#ifdef ENABLE_NATIVE_UI
/*
 * The wall clock (wallclock.c): setting it at the start of a new game, or looking at the one on the bedroom wall, as a
 * clock drawn in the design's own pieces instead of the game's 240x160 picture of it: a round face with its ticks and
 * numbers, the hands where the game turns them (the minute hand sweeping as the game sweeps it), the game's AM / PM, the
 * time in figures below it, and "Is this the correct time?" with its YES / NO in the design's boxes. The boy's clock is
 * blue, the girl's red, as the game's are. The game keeps the clock: its time, how fast holding a direction turns it,
 * what it saves. Arrows beside the face turn the hands while held with the mouse (the game's left and right), a click
 * answers YES / NO, a right click is B. The owner's request of 2026-10-26: every screen in the current interface.
 * Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "strings.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define CX 320
#define CY 152
#define RIM 112                 /* the case's radius */
#define FACE 100                /* the face's */
#define TURN 6.283185307179586

enum
{
    HIT_ARROW = UI_HIT_SCREEN,  /* index 0 back, 1 on */
    HIT_FACE,
};

static const u8 sText_SetTime[] = _("Set the time");
static const u8 sText_Confirm[] = _("Confirm");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Am[] = _("AM");
static const u8 sText_Pm[] = _("PM");

static struct NativeWallClock sC;
static int sGoal = -1;          /* a clicked YES / NO, then A */
static int sGoalSteps;

static bool Detect(void)
{
    struct NativeWallClock c;

    return WallClock_NativeUiState(&c);
}

static void GoalStep(void)
{
    if (sGoal < 0)
        return;
    if (!sC.asking)
    {
        sGoal = -1;
        return;
    }
    if (NativeUi_PressesPending())
        return;
    if (++sGoalSteps > 6)
        sGoal = -1;
    else if (sC.yesNoCursor == sGoal)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
    }
    else
        NativeUi_QueuePress(sGoal > sC.yesNoCursor ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    WallClock_NativeUiState(&sC);
    GoalStep();
}

/* ---------------------------------------------------------------- drawing */

static struct UiRamp CaseRamp(void)
{
    return UiRampOf(sC.female ? UI_HEX(0xD85850) : UI_HEX(0x4C80D0), 0.38, 0.28, 0.62);
}

static void Wall(int t)
{
    struct UiRamp r = CaseRamp();
    UiRGBA paper = UiMix(UI_P_CREAM, r.base, 0.10), stripe = UiMix(UI_P_CREAM, r.base, 0.18);
    struct UiPattern v = UiPat_VStripe(24, 8, 0);
    int bx, by, bw, bh;

    (void)t;
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, paper);
    UiG_FillPattern(&v, bx, by, bw, bh, stripe, 0, 0);
    /* the skirting board */
    UiG_Rect(bx, by + bh - 40, bw, 40, UiMix(UI_HEX(0xA07048), UI_P_INK, 0.2));
    UiG_Rect(bx, by + bh - 40, bw, 3, UiMix(UI_HEX(0xA07048), UI_P_CREAM, 0.3));
}

/* a thick straight line from (x0, y0) along an angle (radians, clockwise from 12), stamped in squares */
static void Stroke(double x0, double y0, double a, int from, int to, int w, UiRGBA c)
{
    double dx = sin(a), dy = -cos(a);

    for (int d = from; d <= to; d++)
        UiG_Rect((int)floor(x0 + dx * d - w / 2.0 + 0.5), (int)floor(y0 + dy * d - w / 2.0 + 0.5), w, w, c);
}

static void Face(void)
{
    struct UiRamp r = CaseRamp();
    char num[4];

    /* the case and its shadow, the face */
    UiG_Mask(UiM_Ellipse(RIM * 2 + 8, RIM * 2 + 8), CX - RIM - 2, CY - RIM + 4, UiMix(UI_P_INK, UI_P_CREAM, 0.35));
    UiG_Mask(UiM_Ellipse(RIM * 2 + 4, RIM * 2 + 4), CX - RIM - 2, CY - RIM - 2, r.ink);
    UiG_Mask(UiM_Ellipse(RIM * 2, RIM * 2), CX - RIM, CY - RIM, r.base);
    UiG_Mask(UiM_Ellipse(RIM * 2 - 10, RIM * 2 - 10), CX - RIM + 5, CY - RIM + 5, r.hi);
    UiG_Mask(UiM_Ellipse(FACE * 2 + 4, FACE * 2 + 4), CX - FACE - 2, CY - FACE - 2, r.ink);
    UiG_Mask(UiM_Ellipse(FACE * 2, FACE * 2), CX - FACE, CY - FACE, UI_P_PAPER);
    /* the minutes' and the hours' ticks */
    for (int m = 0; m < 60; m++)
    {
        double a = TURN * m / 60;

        if (m % 5 == 0)
            Stroke(CX, CY, a, FACE - 12, FACE - 4, 3, UI_P_INK);
        else
            Stroke(CX, CY, a, FACE - 6, FACE - 5, 2, UiMix(UI_P_INK, UI_P_PAPER, 0.45));
    }
    /* the numbers */
    for (int h = 1; h <= 12; h++)
    {
        double a = TURN * h / 12;
        int x = CX + (int)lround(sin(a) * (FACE - 26)), y = CY - (int)lround(cos(a) * (FACE - 26));
        u8 text[4];

        snprintf(num, sizeof(num), "%d", h);
        UiText_FromAscii(text, sizeof(text), num);
        UiText_Draw(UI_FONT_NORMAL, x, y - 7, text, UI_P_INK, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    /* the game's AM / PM sign, in the face */
    {
        const u8 *label = sC.pm ? sText_Pm : sText_Am;
        int w = UiText_Width(UI_FONT_NORMAL, label, 0) + 14;

        UiG_Paint(UiM_Rounded4(w, 16, 4, 4, 4, 4), CX - w / 2, CY + 30, r.base,
                  &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NORMAL, CX, CY + 31, label, UI_P_TEXT_W, r.ink, UI_ALIGN_CENTER, 0);
    }
}

static void Hands(void)
{
    struct UiRamp r = CaseRamp();
    double hour = TURN * sC.hourAngle / 360.0, minute = TURN * sC.minuteAngle / 360.0;

    /* shadows, then the hour hand, the minute hand and the cap */
    Stroke(CX + 2, CY + 3, hour, -8, 52, 7, UiMix(UI_P_INK, UI_P_PAPER, 0.75));
    Stroke(CX + 2, CY + 3, minute, -10, 80, 5, UiMix(UI_P_INK, UI_P_PAPER, 0.75));
    Stroke(CX, CY, hour, -8, 52, 7, UI_P_INK);
    Stroke(CX, CY, minute, -10, 80, 5, UI_P_INK);
    Stroke(CX, CY, minute, 60, 80, 3, r.base);
    UiG_Mask(UiM_Ellipse(14, 14), CX - 7, CY - 7, UI_P_INK);
    UiG_Mask(UiM_Ellipse(8, 8), CX - 4, CY - 4, r.base);
}

/* the time in figures under the clock */
static void Figures(void)
{
    struct UiRamp r = CaseRamp();
    char ascii[16];
    u8 text[16];
    int hour12 = sC.hours % 12 == 0 ? 12 : sC.hours % 12, w;

    snprintf(ascii, sizeof(ascii), "%d:%02d %s", hour12, sC.minutes, sC.pm ? "PM" : "AM");
    UiText_FromAscii(text, sizeof(text), ascii);
    w = UiText_Width(UI_FONT_NORMAL, text, 0) + 28;
    UiG_Paint(UiM_Rounded4(w, 22, 6, 6, 6, 6), CX - w / 2, CY + RIM + 12, UI_P_PAPER,
              &(struct UiPaint){r.ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, CX, CY + RIM + 15, text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
}

/* the arrows that turn the hands while held */
static void Arrows(int t)
{
    struct UiRamp r = CaseRamp();
    const struct UiHit *hover = NativeUi_MouseHover();

    if (!sC.adjusting)
        return;
    for (int i = 0; i < 2; i++)
    {
        int x = i == 0 ? CX - RIM - 64 : CX + RIM + 24, y = CY - 20;
        bool held = hover != NULL && hover->id == HIT_ARROW && hover->index == i && NativeUi_MouseHeld();
        bool over = hover != NULL && hover->id == HIT_ARROW && hover->index == i;
        UiRGBA body = held ? r.lo : over ? r.hi : r.base;
        int nudge = (t / 20) % 2 && !over ? (i == 0 ? -1 : 1) : 0;

        UiG_Paint(UiM_Rounded4(40, 40, 8, 8, 8, 8), x, y, body, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
        /* a chevron */
        for (int k = 0; k < 9; k++)
        {
            int dx = i == 0 ? 24 - k : 16 + k;

            UiG_Rect(x + dx + nudge, y + 11 + k, 3, 2, UI_P_TEXT_W);
            UiG_Rect(x + dx + nudge, y + 27 - k, 3, 2, UI_P_TEXT_W);
        }
        UiG_Hit(HIT_ARROW, i, x, y, 40, 40);
    }
}

static void Bottom(int t)
{
    if (sC.asking)
    {
        int b[4];

        UiW_DialogBox(8, 296, 624, 58, NULL, UI_NONE, b);
        UiW_MessagePageEx(gText_IsThisTheCorrectTime, -1, b[0] + 4, b[1], b[2] - 8, 2, 18);
        UiW_YesNo(632, 292, sC.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
        return;
    }
    if (sC.adjusting)
    {
        struct UiHintItem hints[2] = {{UI_ACT_MOVE, sText_SetTime}, {UI_ACT_CONFIRM, sText_Confirm}};

        UiW_Hints(16, 347, hints, 2, true, 10);
    }
    else if (sC.viewing)
    {
        struct UiHintItem hints[1] = {{UI_ACT_CANCEL, sText_Close}};

        UiW_Hints(16, 347, hints, 1, true, 10);
    }
}

static void Draw(int t)
{
    struct UiRamp r = CaseRamp();
    struct UiRamp tab = {r.hi, r.base, r.lo, r.ink};

    Wall(t);
    UiW_TitleTab(0, 8, "CLOCK", &tab, 0, NULL);
    Face();
    Hands();
    Figures();
    Arrows(t);
    UiG_Hit(HIT_FACE, 0, CX - RIM, CY - RIM, RIM * 2, RIM * 2);
    Bottom(t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sC.input;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    const struct UiHit *hover = NativeUi_MouseHover();

    (void)newKeys;
    /* an arrow held with the mouse is the game's left or right held */
    if (sC.adjusting && NativeUi_MouseHeld() && hover != NULL && hover->id == HIT_ARROW)
        keys |= hover->index == 0 ? DPAD_LEFT : DPAD_RIGHT;
    return keys;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        sGoal = -1;
        if (sC.asking || sC.viewing)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sC.asking && hit->id == UI_HIT_YESNO)
    {
        sGoal = hit->index;
        sGoalSteps = 0;
        GoalStep();
        return true;
    }
    if (hit->id == HIT_ARROW)
        return true; /* held: FilterKeys */
    if (hit->id == HIT_FACE && sC.input)
    {
        /* the clock itself: A (set: ask whether the time is right; on the wall: close it) */
        if (sC.adjusting || sC.viewing)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_WallClock = {
    .name = "wall_clock",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
