#ifdef ENABLE_NATIVE_UI
/*
 * A battle transition on the field (battle_transition.c), drawn at the window's
 * resolution in the design's own pieces instead of the GBA's 240x160 picture of
 * it: the big Poke Ball weaving in and the circle closing on it, the Poke Balls
 * rolling over the field, the circles and the black wipes as smooth shapes, and
 * the trainer's last line in the design's message box while the game still
 * shows it. The GBA picture goes on underneath with the game's own versions left
 * out (BG0's ball and box, the balls' sprites, the windows' stepped masks). The
 * game keeps the transition - its steps, its timing, its end - and this draws
 * what it shows each frame. Transitions this does not draw (the field's waves
 * and slices, the mugshots, the legendaries' and the Frontier's patterns) keep
 * the game's picture of them. The owner's request of 2026-10-26: no old GBA
 * look when a battle starts. Project-owned.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle_transition.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "window.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_widgets.h"

#define BIG_BALL 128       /* the big Poke Ball's size on the GBA screen */
#define TRAIL_BALL 32      /* a rolling ball's */
#define FAR 4096.0         /* off every screen, in GBA pixels */
#define TURN 6.283185307179586

static struct NativeTransitionState sT;
static bool sBox;          /* the game still shows its message box (window 0) */
static u8 sMsg[UI_MESSAGE_COPY];
static UiRGBA sBoxVeil;    /* how far the game has turned the box's colours to another (its flash, its fade) */
static UiRGBA sBackdrop;   /* the GBA's backdrop colour: what a window's empty side shows */

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    return gMain.callback2 == CB2_OverworldBasic && BattleTransition_NativeUiPlaying() < B_TRANSITION_COUNT;
}

static UiRGBA FromGba(u16 c)
{
    return UI_RGB(((c & 0x1F) << 3) | ((c & 0x1F) >> 2), (((c >> 5) & 0x1F) << 3) | (((c >> 5) & 0x1F) >> 2),
                  (((c >> 10) & 0x1F) << 3) | (((c >> 10) & 0x1F) >> 2));
}

/* The blend the game has given a palette (BlendPalettes: the intro's flash to gray, the fade to black at the end; a
 * palette fade): two of its colours as they are and as they were give how far (k) and towards which colour. */
static UiRGBA PaletteVeil(u32 a, u32 b)
{
    int u1[3], u2[3], c1[3], c2[3], best = 0, t[3];
    double k;

    for (int i = 0; i < 3; i++)
    {
        u1[i] = (gPlttBufferUnfaded[a] >> (5 * i)) & 0x1F, u2[i] = (gPlttBufferUnfaded[b] >> (5 * i)) & 0x1F;
        c1[i] = (gPlttBufferFaded[a] >> (5 * i)) & 0x1F, c2[i] = (gPlttBufferFaded[b] >> (5 * i)) & 0x1F;
        if (abs(u1[i] - u2[i]) > abs(u1[best] - u2[best]))
            best = i;
    }
    if (abs(u1[best] - u2[best]) < 4)
        return UI_NONE;
    k = 1.0 - (double)(c1[best] - c2[best]) / (u1[best] - u2[best]);
    if (k < 0.03)
        return UI_NONE;
    if (k > 1.0)
        k = 1.0;
    for (int i = 0; i < 3; i++)
    {
        t[i] = (int)floor((c1[i] - u1[i] * (1.0 - k)) / k * 255.0 / 31.0 + 0.5);
        t[i] = t[i] < 0 ? 0 : t[i] > 255 ? 255 : t[i];
    }
    return UI_RGBA(t[0], t[1], t[2], (int)floor(k * 255.0 + 0.5));
}

static void Capture(void)
{
    u32 printed;
    bool8 active;

    BattleTransition_NativeUiState(&sT);
    sBox = Window_NativeUiOnScreen(0) && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &printed, &active);
    /* the message box's own colours: its paper (1) and its text (2) */
    sBoxVeil = sBox ? PaletteVeil(gWindows[0].window.paletteNum * 16 + 1, gWindows[0].window.paletteNum * 16 + 2)
                    : UI_NONE;
    sBackdrop = FromGba(((const u16 *)PLTT)[0]);
}

static bool BigBallShown(void)
{
    return sT.pokeball && (sT.kind == NATIVE_TRANSITION_WEAVE || sT.kind == NATIVE_TRANSITION_CIRCLE);
}

static enum UiPicture Picture(void)
{
    /* the game's message box and its big Poke Ball (both BG0) are drawn here */
    return sBox || BigBallShown() ? UI_PICTURE_HIDE_BG0 : UI_PICTURE_KEEP;
}

static enum UiWindows Windows(void)
{
    /* the shapes drawn here instead: the region that shows the field everywhere */
    switch (sT.kind)
    {
    case NATIVE_TRANSITION_CIRCLE:
    case NATIVE_TRANSITION_ANGLED:
        return UI_WINDOWS_INSIDE;
    case NATIVE_TRANSITION_BLACKHOLE:
    case NATIVE_TRANSITION_CLOCKWISE:
        return UI_WINDOWS_OUTSIDE;
    }
    return UI_WINDOWS_GAME;
}

/* the rolling balls' sprites, by their tiles */
static void HideObjects(uint32_t mask[4])
{
    const struct OamData *oam = (const struct OamData *)OAM;

    if (sT.kind != NATIVE_TRANSITION_TRAIL)
        return;
    for (int i = 0; i < 128; i++)
        for (int b = 0; b < sT.ballCount; b++)
            if (oam[i].tileNum == sT.balls[b].tileNum && oam[i].affineMode != ST_OAM_AFFINE_ERASE)
                mask[i >> 5] |= 1u << (i & 31);
}

/* ---------------------------------------------------------------- the GBA screen on the canvas */

/* GBA pixel (x, y) is at (gx + x * k, gy + y * k) in current coordinates; the shapes cover [ax, ax + aw) x [ay, ay + ah):
 * the whole canvas over the 3D field, else the picture's own rectangle */
struct Frame
{
    double gx, gy, k;
    int ax, ay, aw, ah;
};

static void Place(struct Frame *f)
{
    const struct UiView *v = UiDraw_View();
    int px, py, pw, ph, bx, by, bw, bh;
    bool8 fills;

    Platform_NativeUiPictureRect(v->outputWidth, v->outputHeight, &px, &py, &pw, &ph, &fills);
    UiG_Bleed(&bx, &by, &bw, &bh);
    f->k = (double)pw / DISPLAY_WIDTH / v->scale;
    f->gx = bx + (double)(px - v->originX) / v->scale;
    f->gy = by + (double)(py - v->originY) / v->scale;
    if (fills)
    {
        f->ax = bx, f->ay = by, f->aw = bw, f->ah = bh;
        return;
    }
    f->ax = (int)floor(f->gx + 0.5);
    f->ay = (int)floor(f->gy + 0.5);
    f->aw = (int)floor(DISPLAY_WIDTH * f->k + 0.5);
    f->ah = (int)floor(DISPLAY_HEIGHT * f->k + 0.5);
}

/* a span of a canvas row, [x0, x1) in GBA pixels */
static void RowSpan(const struct Frame *f, int row, double x0, double x1, UiRGBA c)
{
    int a = (int)floor(f->gx + x0 * f->k + 0.5), b = (int)floor(f->gx + x1 * f->k + 0.5);

    if (x0 <= -FAR)
        a = f->ax;
    if (x1 >= FAR)
        b = f->ax + f->aw;
    if (a < f->ax)
        a = f->ax;
    if (b > f->ax + f->aw)
        b = f->ax + f->aw;
    if (b > a)
        UiG_Rect(a, row, b - a, 1, c);
}

/* the GBA row a canvas row's middle is on */
static double GbaRow(const struct Frame *f, int row)
{
    return (row + 0.5 - f->gy) / f->k;
}

/* ---------------------------------------------------------------- shapes */

/* black outside (or inside) the circle of the GBA's masks, centred on the screen */
static void Circle(const struct Frame *f, double radius, bool inside, UiRGBA c)
{
    for (int row = f->ay; row < f->ay + f->ah; row++)
    {
        double dy = GbaRow(f, row) - DISPLAY_HEIGHT / 2, hw;

        if (fabs(dy) >= radius)
        {
            if (!inside)
                RowSpan(f, row, -FAR, FAR, c);
            continue;
        }
        hw = sqrt(radius * radius - dy * dy);
        if (inside)
            RowSpan(f, row, DISPLAY_WIDTH / 2 - hw, DISPLAY_WIDTH / 2 + hw, c);
        else
        {
            RowSpan(f, row, -FAR, DISPLAY_WIDTH / 2 - hw, c);
            RowSpan(f, row, DISPLAY_WIDTH / 2 + hw, FAR, c);
        }
    }
}

static int CompareDoubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return x < y ? -1 : x > y;
}

/* a polygon (n points, GBA pixels) filled even-odd, row by row */
static void Polygon(const struct Frame *f, const double *pts, int n, UiRGBA c)
{
    double xs[24];

    for (int row = f->ay; row < f->ay + f->ah; row++)
    {
        double y = GbaRow(f, row);
        int count = 0;

        for (int i = 0; i < n && count < 24; i++)
        {
            double x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * ((i + 1) % n)], y1 = pts[2 * ((i + 1) % n) + 1];

            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y))
                xs[count++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        }
        qsort(xs, count, sizeof(xs[0]), CompareDoubles);
        for (int i = 0; i + 1 < count; i += 2)
            RowSpan(f, row, xs[i], xs[i + 1], c);
    }
}

/* the clockwise wipe: black from twelve o'clock round to the line to its end */
static void Clockwise(const struct Frame *f, UiRGBA c)
{
    const double cx = DISPLAY_WIDTH / 2, cy = DISPLAY_HEIGHT / 2, far = FAR / 2;
    double theta = atan2(sT.wipeEndX - cx, -(sT.wipeEndY - cy)), pts[2 * 12];
    int n = 0;

    if (sT.wipeFull)
    {
        for (int row = f->ay; row < f->ay + f->ah; row++)
            RowSpan(f, row, -FAR, FAR, c);
        return;
    }
    if (theta < 0)
        theta += TURN;
    if (theta <= 0)
        return;
    pts[n++] = cx, pts[n++] = cy;
    /* the sweep's edge far off screen, every eighth of a turn, then the line itself */
    for (double a = 0; a < theta && n < 2 * 11; a += TURN / 8)
        pts[n++] = cx + far * sin(a), pts[n++] = cy - far * cos(a);
    pts[n++] = cx + far * sin(theta), pts[n++] = cy - far * cos(theta);
    Polygon(f, pts, n / 2, c);
}

/* the angled wipes: each a black side of its line, those done on every row they cross, the moving one up to the row it
 * has reached; past the picture's top and bottom the rows go on as its edge rows */
static void Angled(const struct Frame *f, UiRGBA c)
{
    s16 lines[8][5];
    int count = 0;

    while (count < 8 && BattleTransition_NativeUiAngledWipe((u8)count, lines[count]))
        count++;
    for (int row = f->ay; row < f->ay + f->ah; row++)
    {
        double y = GbaRow(f, row), left = -FAR, right = FAR;
        int done = sT.wipeId < count ? sT.wipeId : count;

        if (y < 0)
            y = 0;
        if (y > DISPLAY_HEIGHT - 0.5)
            y = DISPLAY_HEIGHT - 0.5;
        for (int i = 0; i <= done && i < count; i++)
        {
            const s16 *L = lines[i];
            double x, lo = min(L[1], L[3]), hi = max(L[1], L[3]);

            if (i == done)
            {
                /* the moving wipe: only the rows from its start to the one it has reached */
                if (!sT.wipeActive)
                    break;
                lo = min(L[1], sT.wipeCurrY), hi = max(L[1], sT.wipeCurrY);
                if (y < lo || y >= hi)
                    continue;
            }
            else if (y < lo || y > hi + 1)
                continue;
            x = L[0] + (double)(L[2] - L[0]) * (y - L[1]) / (L[3] - L[1]);
            if (L[4] == 0)
                left = max(left, x);
            else
                right = min(right, x);
        }
        if (left >= right)
            RowSpan(f, row, -FAR, FAR, c);
        else
        {
            if (left > -FAR)
                RowSpan(f, row, -FAR, left, c);
            if (right < FAR)
                RowSpan(f, row, right, FAR, c);
        }
    }
}

/* the big Poke Ball, each GBA line moved as BG0 moves it (the weave), faded in as the game blends it */
static void BigBall(const struct Frame *f, bool weave)
{
    int d = (int)floor(BIG_BALL * f->k + 0.5), top = (int)floor(f->gy + (DISPLAY_HEIGHT - BIG_BALL) / 2 * f->k + 0.5);
    double left = f->gx + (DISPLAY_WIDTH - BIG_BALL) / 2 * f->k;
    /* the GBA adds the ball (eva) to the field (evb) and then takes the field away: here a fade in, half way while the
     * field is still whole, the whole ball once it has gone */
    double alpha = sT.eva / 16.0 * (1.0 - sT.evb / 32.0);
    const struct UiPiece *ball = UiW_PokeBallPiece(d, 0);

    if (ball == NULL || alpha <= 0.0)
        return;
    for (int row = 0; row < d; row++)
    {
        int line = (int)floor(GbaRow(f, top + row)), x = (int)floor(left + 0.5);

        if (weave && line >= 0 && line < DISPLAY_HEIGHT)
            x = (int)floor(left - sT.lineOffset[line] * f->k + 0.5);
        UiG_BlitPart(ball, 0, row, d, 1, x, top + row, 1, false, alpha > 1.0 ? 1.0 : alpha);
    }
}

/* the rolling balls, turned as their sprites are */
static void TrailBalls(const struct Frame *f)
{
    int d = (int)floor(TRAIL_BALL * f->k + 0.5);

    for (int i = 0; i < sT.ballCount; i++)
    {
        const struct NativeTransitionBall *b = &sT.balls[i];
        int turn = (int)floor(atan2(b->matrix[1], b->matrix[0]) * 64.0 / TURN + 0.5);

        UiW_PokeBall((int)floor(f->gx + b->x * f->k + 0.5), (int)floor(f->gy + b->y * f->k + 0.5), d, turn);
    }
}

/* ---------------------------------------------------------------- drawing */

static void Draw(int t)
{
    struct Frame f;
    int bx, by, bw, bh;

    Place(&f);
    UiG_Bleed(&bx, &by, &bw, &bh);
    if (sBox)
    {
        struct UiRamp band = UiW_FieldBand();

        UiField_MessageBox(t, bx, bx + bw, by + bh, sMsg, -1, false, &band, NULL, 0, band.ink, 0);
        /* the game's flashes and fades take every colour it has, the box's too */
        UiG_Rect(bx + 8, by + bh - UI_FIELD_MSG_H - 8, UI_FIELD_MSG_W, UI_FIELD_MSG_H, sBoxVeil);
    }
    /* nothing of the transition outside the game's own picture (the whole window over the 3D field) */
    UiG_Save();
    UiG_Clip(f.ax, f.ay, f.aw, f.ah);
    switch (sT.kind)
    {
    case NATIVE_TRANSITION_WEAVE:
        if (sT.pokeball)
            BigBall(&f, true);
        break;
    case NATIVE_TRANSITION_CIRCLE:
        if (sT.pokeball)
            BigBall(&f, false);
        Circle(&f, sT.radius, false, sBackdrop);
        break;
    case NATIVE_TRANSITION_BLACKHOLE:
        Circle(&f, sT.radius, true, sBackdrop);
        break;
    case NATIVE_TRANSITION_CLOCKWISE:
        Clockwise(&f, sBackdrop);
        break;
    case NATIVE_TRANSITION_ANGLED:
        Angled(&f, sBackdrop);
        break;
    case NATIVE_TRANSITION_TRAIL:
        TrailBalls(&f);
        break;
    }
    UiG_Restore();
    /* finished: the game has turned every colour black until the battle comes up */
    if (sT.done)
        UiG_Veil(UI_RGB(0, 0, 0));
}

const struct UiScreen gUiScreen_Transition = {
    .name = "transition",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
    .hideObjects = HideObjects,
    .windows = Windows,
};
#endif // ENABLE_NATIVE_UI
