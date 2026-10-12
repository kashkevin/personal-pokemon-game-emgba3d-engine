#ifdef ENABLE_NATIVE_UI
/*
 * 21 · Title (the approved design's title screen, preview/js/screens/title.js):
 * the Emerald title remaster, version C (the cinematic intro, then B's loop),
 * the faceted EMERALD wordmark with its gem and MULTIPLAYER ribbon, a blinking
 * PRESS START. The engine's title screen (title_screen.c) keeps running under
 * it: its music, its timing, its buttons (A / START open the main menu, the
 * maintenance combos, SELECT's quick start in development builds). Only the
 * picture is replaced. Project-owned.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include "global.h"
#include "main.h"
#include "platform/mp_bridge.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_title.h"
#include "native_ui/ui_title_bg.h"
#include "native_ui/ui_widgets.h"

/* ---------------------------------------------------------------- the wordmark */

static const char *const sGlyphE[] = {"######", "######", "##....", "#####.", "#####.", "##....", "######", "######"};
static const char *const sGlyphM[] = {"##....##", "###..###", "########", "##.##.##", "##....##", "##....##", "##....##", "##....##"};
static const char *const sGlyphR[] = {"#####.", "######", "##..##", "######", "#####.", "##.##.", "##..##", "##..##"};
static const char *const sGlyphA[] = {"..##..", ".####.", "##..##", "##..##", "######", "######", "##..##", "##..##"};
static const char *const sGlyphL[] = {"##....", "##....", "##....", "##....", "##....", "##....", "######", "######"};
static const char *const sGlyphD[] = {"#####.", "######", "##..##", "##..##", "##..##", "##..##", "######", "#####."};
#define CELL 4
static const char sWord[] = "EMERALD";

static const char *const *GlyphRows(char ch)
{
    switch (ch)
    {
    case 'E': return sGlyphE;
    case 'M': return sGlyphM;
    case 'R': return sGlyphR;
    case 'A': return sGlyphA;
    case 'L': return sGlyphL;
    default: return sGlyphD;
    }
}

/* the gem colours: PK.ramp('28A868', 0.45, 0.3, 0.68, warm default, cool [8, 40, 40]) */
static struct UiRamp Gem(void)
{
    return UiRampOf2(UI_HEX(0x28A868), 0.45, 0.3, 0.68, UI_NONE, UI_RGB(8, 40, 40));
}

/* a glyph scaled by CELL with chamfered convex corners (the cut-gem look; glyphMask) */
static const struct UiMask *GlyphMask(char ch)
{
    const char *const *rows = GlyphRows(ch);
    int gw = (int)strlen(rows[0]), gh = 8, w = gw * CELL, h = gh * CELL, e = CELL - 1;
    static char buf[8 * CELL][8 * CELL + 1];
    const char *ptrs[8 * CELL];

#define ON(cx, cy) ((cy) >= 0 && (cy) < gh && (cx) >= 0 && (cx) < gw && rows[cy][cx] == '#')
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int cx = x / CELL, cy = y / CELL, sx = x % CELL, sy = y % CELL;
            bool on = ON(cx, cy);

            if (on && !ON(cx, cy - 1) && !ON(cx - 1, cy) && sx + sy < 2)
                on = false;
            if (on && !ON(cx, cy - 1) && !ON(cx + 1, cy) && (e - sx) + sy < 2)
                on = false;
            if (on && !ON(cx, cy + 1) && !ON(cx - 1, cy) && sx + (e - sy) < 2)
                on = false;
            if (on && !ON(cx, cy + 1) && !ON(cx + 1, cy) && (e - sx) + (e - sy) < 2)
                on = false;
            buf[y][x] = on ? '#' : '.';
        }
        buf[y][w] = '\0';
        ptrs[y] = buf[y];
    }
#undef ON
    return UiM_Rows(ptrs, h, "#");
}

static int WordWidth(void)
{
    int w = 0;

    for (int i = 0; sWord[i] != '\0'; i++)
        w += (int)strlen(GlyphRows(sWord[i])[0]) * CELL + 4;
    return w - 4;
}

static double JsRound(double x)
{
    return floor(x + 0.5);
}

static void Line(int x0, int y0, int x1, int y1, UiRGBA c)
{
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);

    for (int i = 0; i <= n; i++)
        UiG_Put((int)JsRound(x0 + (x1 - x0) * (double)i / n), (int)JsRound(y0 + (y1 - y0) * (double)i / n), c);
}

/* the emerald-cut gem emblem (gemEmblem) */
static void GemEmblem(int x, int y, int t, int glint)
{
    static const int outer[] = {9, 0, 27, 0, 36, 9, 36, 35, 27, 44, 9, 44, 0, 35, 0, 9};
    static const int inner[] = {13, 9, 23, 9, 27, 13, 27, 31, 23, 35, 13, 35, 9, 31, 9, 13};
    struct UiRamp gem = Gem();
    const struct UiMask *om = UiM_Poly(37, 45, outer, 8);
    const struct UiMask *rim = UiM_Dilate8(UiM_Pad(om, 1, 1, 1, 1));
    int ph;

    UiG_Mask(UiM_Shift(rim, 1, 1), x - 1, y - 1, UiMix(gem.ink, UI_HEX(0x000000), 0.3));
    UiG_Mask(rim, x - 1, y - 1, gem.ink);
    UiG_Paint(om, x, y, gem.base, &(struct UiPaint){gem.ink, gem.hi, gem.lo, 2, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Poly(37, 45, inner, 8), x, y, UiMix(gem.base, gem.hi, 0.45),
              &(struct UiPaint){UI_NONE, UiMix(gem.hi, UI_HEX(0xFFFFFF), 0.4), gem.base, 0, 0, NULL, UI_NONE});
    for (int i = 0; i < 8; i++)
        Line(x + outer[i * 2], y + outer[i * 2 + 1], x + inner[i * 2], y + inner[i * 2 + 1], gem.lo);
    /* a stepped glint that crosses the table */
    ph = glint == UI_TITLE_NO_GLINT_GIVEN ? t % 180 : glint;
    if (ph >= 0 && ph < 12)
        for (int k = 0; k < 6; k++)
            UiG_Put(x + 12 + ph + k - 6, y + 12 + k, UI_HEX(0xFFFFFF));
    UiG_Rect(x + 13, y + 11, 3, 1, UI_HEX(0xF8FFF8));
    UiG_Put(x + 13, y + 12, UI_HEX(0xF8FFF8));
}

void UiTitle_Wordmark(int x, int y, int t, int glint)
{
    struct UiRamp gem = Gem();
    struct UiPattern below = UiPat_Below(-1);
    struct UiPattern diag = UiPat_Diag(14, 3, 0, -1);
    int wx = x + 46;
    static const char label[] = "MULTIPLAYER";
    int lw = (int)(sizeof(label) - 1) * 8 - 2, rw = lw + 28;
    int rx = x + 46 + ((WordWidth() - rw) >> 1), ry = y + 38;
    int rib[12];
    const struct UiMask *ribbon;

    GemEmblem(x, y - 4, t, glint);
    diag.and_ = &below;
    for (int i = 0; sWord[i] != '\0'; i++)
    {
        const struct UiMask *m = GlyphMask(sWord[i]);
        const struct UiMask *r = UiM_Dilate8(UiM_Pad(m, 1, 1, 1, 1));

        UiG_Mask(UiM_Shift(r, 1, 1), wx - 1, y - 1, UiMix(gem.ink, UI_HEX(0x000000), 0.3));
        UiG_Mask(r, wx - 1, y - 1, gem.ink);
        UiG_Paint(m, wx, y, gem.base,
                  &(struct UiPaint){gem.ink, gem.hi, gem.lo, 2, 2, &diag, UiMix(gem.base, gem.hi, 0.35)});
        wx += m->w + 4;
    }
    /* the ribbon */
    rib[0] = 0;      rib[1] = 0;
    rib[2] = rw - 1; rib[3] = 0;
    rib[4] = rw - 7; rib[5] = 7;
    rib[6] = rw - 1; rib[7] = 14;
    rib[8] = 0;      rib[9] = 14;
    rib[10] = 6;     rib[11] = 7;
    ribbon = UiM_Poly(rw, 15, rib, 6);
    UiG_Mask(UiM_Shift(UiM_Dilate8(UiM_Pad(ribbon, 1, 1, 1, 1)), 1, 1), rx - 1, ry - 1,
             UiMix(gem.ink, UI_HEX(0x000000), 0.3));
    UiG_Paint(ribbon, rx, ry, UI_P_CREAM, &(struct UiPaint){gem.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int k = 0; label[k] != '\0'; k++)
    {
        char one[2] = {label[k], '\0'};

        UiCaps_Draw(rx + 14 + k * 8, ry + 4, one, gem.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
}

UiRGBA UiTitle_GemInk(void)
{
    return Gem().ink;
}

/* ---------------------------------------------------------------- the title's clock */

/* frames since the title opened: the remaster's timeline (the main menu keeps it) */
static uint32_t sClockStart;
static bool sClockRunning;

double UiTitle_Clock(void)
{
    double f;

    if (!sClockRunning)
    {
        sClockStart = NativeUi_Frame() - UI_TITLE_INTRO_END;
        sClockRunning = true;
    }
    f = (double)(NativeUi_Frame() - sClockStart);
    return f + NativeUi_FrameAlpha();
}

static void ClockRestart(void)
{
    sClockStart = NativeUi_Frame();
    sClockRunning = true;
}

static void ClockSkipIntro(void)
{
    if (NativeUi_Frame() - sClockStart < UI_TITLE_INTRO_END)
        sClockStart = NativeUi_Frame() - UI_TITLE_INTRO_END;
}

void UiTitle_DrawBackground(void)
{
    int bx, by, bw, bh;

    if (!UiTitleBg_Draw('c', UiTitle_Clock()))
    {
        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Rect(bx, by, bw, bh, UI_HEX(0x1C6A60));
    }
}

/* ---------------------------------------------------------------- the screen */

static const u8 sText_Start[] = _("Start");

static u32 sPhase;
static u32 sPrevPhase;
static bool sLinked;

static bool Detect(void)
{
    return TitleScreen_NativeUiPhase() != NATIVE_TITLE_NONE;
}

static void Capture(void)
{
    sPrevPhase = sPhase;
    sPhase = TitleScreen_NativeUiPhase();
    if (sPhase == NATIVE_TITLE_LOADING && sPrevPhase != NATIVE_TITLE_LOADING)
        ClockRestart();
    /* the engine reached PRESS START (a button skipped its logo, or its timing
     * ran ahead): the remaster's intro ends there too */
    if (sPhase >= NATIVE_TITLE_PRESS_START)
        ClockSkipIntro();
    sLinked = MpBridge_IsEnabled();
}

static int Frame(void)
{
    return (int)(NativeUi_Frame() - sClockStart);
}

static void Draw(int t)
{
    struct UiTitleUi u;
    struct UiRamp gem = Gem();
    int f = Frame();
    int bx, by, bw, bh;
    char corner[64];

    (void)t;
    UiTitle_DrawBackground();
    UiTitleBg_Ui('c', f, &u);
    if (u.logo)
        UiTitle_Wordmark(22, 26, f, u.glint);
    UiG_Bleed(&bx, &by, &bw, &bh);
    if (u.press > 0)
    {
        /* PRESS START sits under Rayquaza's chin: it follows the art, which
         * stands on the bottom edge of the canvas (taller canvases too) */
        int bottom = by + bh;
        int psY = bottom - 42, cy = bottom - 16;
        static const char ps[] = "PRESS START";
        int px0 = 320 - ((int)(sizeof(ps) - 1) * 8 - 2) / 2;

        UiG_Save();
        UiG_Alpha(u.press);
        if (u.blinkOn)
        {
            for (int q = 0; ps[q] != '\0'; q++)
            {
                char one[2] = {ps[q], '\0'};

                UiCaps_Draw(px0 + q * 8, psY, one, UI_HEX(0xFFFFFF), gem.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT,
                            UiMix(gem.ink, UI_HEX(0x000000), 0.3));
            }
        }
        UiW_Hint(296, psY + 18, UI_ACT_CONFIRM, sText_Start, true);
        snprintf(corner, sizeof(corner), "PRIVATE BUILD %s", __DATE__);
        for (char *p = corner; *p != '\0'; p++)
            *p = (char)toupper((unsigned char)*p);
        UiCaps_Draw(10, cy, corner, UI_HEX(0xE8F8EC), gem.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(630, cy, sLinked ? "OPENED BY THE LAUNCHER " UI_CAPS_DOT " MULTIPLAYER LINK ON"
                                     : "NO MULTIPLAYER LINK " UI_CAPS_DOT " HOST AND JOIN ARE IN THE LAUNCHER",
                    UI_HEX(0xE8F8EC), gem.ink, UI_CAPS_OUTLINE, UI_ALIGN_RIGHT, UI_NONE);
        UiG_Restore();
    }
    UiG_Hit(UI_HIT_SCREEN, 0, bx, by, bw, bh);
    if (u.white > 0)
        UiG_Veil(UiWithAlpha(UI_HEX(0xFFFFFF), (int)JsRound(u.white * 255)));
    /* the engine's own fade out (to the main menu, or back to the intro) */
    if (sPhase == NATIVE_TITLE_TO_MAIN_MENU || sPhase == NATIVE_TITLE_TO_INTRO)
        UiG_Veil(NativeUi_ScreenFade());
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (button != 1)
        return false;
    /* a click is START: it skips the intro, then opens the main menu */
    NativeUi_QueuePress(START_BUTTON);
    return true;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    bool press = (newKeys & (A_BUTTON | START_BUTTON)) != 0;

    if (sPhase == NATIVE_TITLE_LOGO || sPhase == NATIVE_TITLE_BANNER)
    {
        /* any of A, B, START, SELECT skips the engine's logo (and the
         * remaster's intro with it). When PRESS START is already shown, A or
         * START also opens the main menu, as the picture promises. */
        if (press && Frame() >= UI_TITLE_INTRO_END - 24)
            NativeUi_QueuePress(START_BUTTON);
        if (newKeys & (A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON))
            ClockSkipIntro();
    }
    return keys;
}

static bool Ready(void)
{
    /* a queued START opens the main menu: only once PRESS START reads it */
    return sPhase == NATIVE_TITLE_PRESS_START;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Title = {
    .name = "title",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
