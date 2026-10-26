#ifdef ENABLE_NATIVE_UI
/*
 * A field move's cut-in (field_effect.c, FLDEFF_FIELD_MOVE_SHOW_MON: Cut,
 * Strength, Rock Smash, Surf, Fly... show the Pokemon that uses the move on a
 * banner across the field) in the design's own pieces: a band in the Pokemon's
 * type colours with speed lines running through it, opening and closing as the
 * game's banner does, and the Pokemon's picture sliding in, crying and sliding
 * off with the game's sprite. The 3D view never showed the game's own (none of
 * the GBA picture is drawn over the 3D field there); this shows in both views,
 * with the game's banner (BG0) and sprite left out. The game keeps the effect,
 * its timing and its end. The owner's request of 2026-10-26: every animation in
 * the current interface. Project-owned.
 */
#include <math.h>

#include "global.h"
#include "pokemon.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"

#define MON_HALF 32         /* half the Pokemon's picture */

static struct NativeFieldMoveState sS;
static int sType;           /* the Pokemon's first type (UI_TYPE_*): the band's colours */
static bool sPicReady;

static bool Detect(void)
{
    struct NativeFieldMoveState s;

    return FieldEffect_NativeUiShowMon(&s);
}

static void Capture(void)
{
    FieldEffect_NativeUiShowMon(&sS);
    sType = UiMon_Type(GetSpeciesType(sS.species, 0));
    sPicReady = sS.species != SPECIES_NONE && UiGfx_PrepareMonPic(sS.species, sS.personality, sS.shiny, false);
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_BG0; /* the game's banner; the field stays */
}

static void HideObjects(uint32_t mask[4])
{
    const struct OamData *oam = (const struct OamData *)OAM;

    if (!sS.monShown)
        return;
    for (int i = 0; i < 128; i++)
        if (oam[i].tileNum == sS.tileNum && oam[i].affineMode != ST_OAM_AFFINE_ERASE)
            mask[i >> 5] |= 1u << (i & 31);
}

/* the speed lines: a few dashes a row band, running left with the game's scroll, each band at its own pace */
static void SpeedLines(int x0, int y0, int w, int h, int scroll, UiRGBA c)
{
    static const uint8_t rows[] = {9, 23, 31, 47, 58, 66, 79, 88};
    static const uint8_t lens[] = {38, 22, 54, 30, 46, 26, 60, 34};

    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++)
    {
        int y = y0 + rows[i] * h / 96, period = 160 + 37 * i, len = lens[i];
        int shift = (int)(((long)-scroll * (3 + (i & 3)) / 4 + i * 71) % period);

        if (shift < 0)
            shift += period;
        if (y < y0 + 2 || y > y0 + h - 4)
            continue;
        for (int x = x0 - period + shift; x < x0 + w; x += period)
        {
            int a = x < x0 ? x0 : x, b = x + len > x0 + w ? x0 + w : x + len;

            if (b > a)
                UiG_Rect(a, y, b - a, 2, c);
        }
    }
}

static void Draw(int t)
{
    const struct UiView *v = UiDraw_View();
    const struct UiRamp *r = &gUiTypeRamps[sType >= 0 && sType < UI_TYPE_COUNT ? sType : UI_TYPE_NORMAL];
    int px, py, pw, ph, bx, by, bw, bh, x0, x1, y0, y1;
    double gx, gy, k;
    bool8 fills;

    (void)t;
    Platform_NativeUiPictureRect(v->outputWidth, v->outputHeight, &px, &py, &pw, &ph, &fills);
    UiG_Bleed(&bx, &by, &bw, &bh);
    k = (double)pw / DISPLAY_WIDTH / v->scale;
    gx = bx + (double)(px - v->originX) / v->scale;
    gy = by + (double)(py - v->originY) / v->scale;
    UiG_Save();
    if (!fills)
        UiG_Clip((int)floor(gx + 0.5), (int)floor(gy + 0.5), (int)floor(DISPLAY_WIDTH * k + 0.5),
                 (int)floor(DISPLAY_HEIGHT * k + 0.5));
    if (sS.right > sS.left && sS.bottom > sS.top)
    {
        /* the banner: across the whole window over the 3D field (the game's screen's width is the window's) */
        x0 = (int)floor((fills ? bx + bw * sS.left / (double)DISPLAY_WIDTH : gx + sS.left * k) + 0.5);
        x1 = (int)floor((fills ? bx + bw * sS.right / (double)DISPLAY_WIDTH : gx + sS.right * k) + 0.5);
        y0 = (int)floor(gy + sS.top * k + 0.5);
        y1 = (int)floor(gy + sS.bottom * k + 0.5);
        if (x1 > x0 && y1 > y0)
        {
            UiG_Rect(x0, y0, x1 - x0, y1 - y0, r->base);
            SpeedLines(x0, y0, x1 - x0, y1 - y0, sS.scroll, r->hi);
            /* its rims: ink outside, a light line inside */
            if (y1 - y0 > 6)
            {
                UiG_Rect(x0, y0, x1 - x0, 2, r->ink);
                UiG_Rect(x0, y0 + 2, x1 - x0, 1, r->hi);
                UiG_Rect(x0, y1 - 2, x1 - x0, 2, r->ink);
                UiG_Rect(x0, y1 - 3, x1 - x0, 1, r->lo);
            }
            else
                UiG_Rect(x0, y0, x1 - x0, y1 - y0, r->ink);
        }
    }
    if (sS.monShown && sPicReady)
    {
        /* over the 3D field it comes in at the window's right edge and goes at its left, as it does at the game
         * screen's (its middle at 120, just off at 120 + 152 either way) */
        double reach = fills ? (bw / 2.0 + MON_HALF * k) / (DISPLAY_WIDTH / 2 + MON_HALF) : k;
        double mx = fills ? bx + bw / 2.0 + (sS.monX - DISPLAY_WIDTH / 2) * reach : gx + sS.monX * k;

        UiGfx_MonPicAffine(sS.species, sS.personality, sS.shiny, 0, (int)floor(mx + 0.5),
                           (int)floor(gy + sS.monY * k + 0.5), 256, (int)floor(k + 0.5), NULL);
    }
    UiG_Restore();
}

const struct UiScreen gUiScreen_FieldMove = {
    .name = "field_move",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
    .hideObjects = HideObjects,
};
#endif // ENABLE_NATIVE_UI
