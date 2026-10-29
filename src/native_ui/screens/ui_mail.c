#ifdef ENABLE_NATIVE_UI
/*
 * Mail (not one of the 31 designed screens; the design's own pieces): a Mail's
 * letter (mail.c), read from the Bag (CHECK), a Pokémon's held Mail (READ) or
 * the PC's MAILBOX, over the engine's own screen. The Mail's own stationery as
 * the engine built it (its tiles, tilemap and palettes, the player's two
 * colours in it) twice the game's size, its pixels kept square, on the design's
 * lining in the stationery's colour with the Mail's name on the title tab; each
 * line of words and the signature where the engine prints them, in the Mail's
 * own ink and the game's font at the same size; a Bead or Dream Mail's Pokémon
 * icon where the engine puts it. A or B closes it, as in the game (a click or
 * a right click too). Project-owned.
 */
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "main.h"
#include "constants/characters.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_LETTER = UI_HIT_SCREEN,
};

/* the letter: the GBA's screen, twice its size, under the title tab */
#define SCALE 2
#define LETTER_X 80
#define LETTER_Y 28
#define TILES 512

static const u8 sText_Close[] = _("Close");

static struct NativeMail sMail;
static u16 sTilemap[32 * 32];
static u8 sTiles[TILES * 32];
static u16 sPalettes[16 * 16];
static UiRGBA sMean;
static int sPaper[4]; /* the stationery's painted part: left, top, right, bottom (GBA pixels) */
static int sMeanType = -1;
static u16 sMeanColours[2];

static UiRGBA FromBgr555(u16 c)
{
    int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;

    return UI_RGB((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

/* the colour BG1 shows at x, y (0: the GBA's colour 0, clear) */
static u16 ArtPixel(int x, int y, bool *clear)
{
    u16 e = sTilemap[(y >> 3) * 32 + (x >> 3)];
    int sx = e & 0x400 ? 7 - (x & 7) : x & 7, sy = e & 0x800 ? 7 - (y & 7) : y & 7;
    u8 b = sTiles[((e & 0x3FF) % TILES) * 32 + sy * 4 + (sx >> 1)];
    u8 ci = sx & 1 ? b >> 4 : b & 0xF;

    *clear = ci == 0;
    return sPalettes[(e >> 12) * 16 + ci];
}

/* the stationery as BG1 shows it (30 x 20 tiles) */
static void BuildArt(struct UiBitmap *bm, const void *ctx)
{
    (void)ctx;
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            bool clear;
            UiRGBA c = FromBgr555(ArtPixel(x, y, &clear));
            u8 *d = &bm->d[(y * bm->w + x) * 4];

            d[0] = c & 0xFF;
            d[1] = (c >> 8) & 0xFF;
            d[2] = (c >> 16) & 0xFF;
            d[3] = clear ? 0 : 0xFF;
        }
}

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativeMail m;

    return Mail_NativeUiState(&m);
}

static void Capture(void)
{
    Mail_NativeUiState(&sMail);
    memcpy(sTilemap, sMail.tilemap, sizeof(sTilemap));
    memcpy(sTiles, sMail.tiles, sizeof(sTiles));
    memcpy(sPalettes, sMail.palettes, sizeof(sPalettes));
    /* the stationery's mean colour, for the lining (once a Mail and the player's two colours, BG palette 0's 10 and 11) */
    if (sMail.mailType != sMeanType || sPalettes[10] != sMeanColours[0] || sPalettes[11] != sMeanColours[1])
    {
        long sum[3] = {0, 0, 0}, n = 0;

        sPaper[0] = DISPLAY_WIDTH, sPaper[1] = DISPLAY_HEIGHT, sPaper[2] = sPaper[3] = 0;
        for (int y = 0; y < DISPLAY_HEIGHT; y++)
            for (int x = 0; x < DISPLAY_WIDTH; x++)
            {
                bool clear;
                UiRGBA c = FromBgr555(ArtPixel(x, y, &clear));

                if (clear)
                    continue;
                if (x < sPaper[0])
                    sPaper[0] = x;
                if (y < sPaper[1])
                    sPaper[1] = y;
                if (x + 1 > sPaper[2])
                    sPaper[2] = x + 1;
                if (y + 1 > sPaper[3])
                    sPaper[3] = y + 1;
                if ((x | y) & 1)
                    continue;
                sum[0] += c & 0xFF;
                sum[1] += (c >> 8) & 0xFF;
                sum[2] += (c >> 16) & 0xFF;
                n++;
            }
        sMean = n > 0 ? UI_RGB(sum[0] / n, sum[1] / n, sum[2] / n) : UI_HEX(0x808080);
        sMeanType = sMail.mailType;
        sMeanColours[0] = sPalettes[10];
        sMeanColours[1] = sPalettes[11];
    }
}

/* ---------------------------------------------------------------- drawing */

static void Draw(int t)
{
    struct UiRamp r = {UiMix(sMean, UI_HEX(0xFFFFFF), 0.35), sMean, UiMix(sMean, UI_HEX(0x000000), 0.3),
                       UiMix(sMean, UI_HEX(0x000000), 0.75)};
    struct UiRamp tab = {r.hi, r.base, r.lo, r.ink};
    struct UiPattern a = UiPat_Diag(24, 4, 0, 1), b = UiPat_Diag(24, 4, 0, -1);
    struct UiHintItem hint = {UI_ACT_CANCEL, sText_Close};
    UiRGBA ink = FromBgr555(sMail.textColor), shadow = FromBgr555(sMail.textShadow);
    char name[32];
    char key[32];
    int bx, by, bw, bh;

    /* the lining in the stationery's colour, with the slow diamond weave */
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UiMix(r.lo, r.ink, 0.45));
    UiG_FillPattern(&a, bx, by, bw, bh, UiMix(r.lo, r.ink, 0.3), t / 5, 0);
    UiG_FillPattern(&b, bx, by, bw, bh, UiMix(r.lo, r.ink, 0.3), t / 5, 0);
    UiText_ToAscii(name, sizeof(name), GetItemName(sMail.itemId));
    UiW_TitleTab(0, 8, name, &tab, 0, NULL);

    /* the letter, its shadow under the painted part */
    if (sPaper[2] > sPaper[0] && sPaper[3] > sPaper[1])
        UiG_Mask(UiM_Rounded4((sPaper[2] - sPaper[0]) * SCALE, (sPaper[3] - sPaper[1]) * SCALE, 8, 8, 8, 8),
                 LETTER_X + sPaper[0] * SCALE + 4, LETTER_Y + sPaper[1] * SCALE + 6, UiWithAlpha(r.ink, 110));
    snprintf(key, sizeof(key), "mail:%d:%04x%04x", sMail.mailType, sPalettes[10], sPalettes[11]);
    UiG_BlitPart(UiG_Cached(key, DISPLAY_WIDTH, DISPLAY_HEIGHT, BuildArt, NULL), 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 LETTER_X, LETTER_Y, SCALE, false, 1.0);
    for (int i = 0; i < sMail.lineCount; i++)
        UiText_DrawScaled(UI_FONT_NORMAL, LETTER_X + sMail.lineX[i] * SCALE, LETTER_Y + sMail.lineY[i] * SCALE,
                          sMail.lines[i], ink, shadow, SCALE);
    if (sMail.signature[0] != EOS)
        UiText_DrawScaled(UI_FONT_NORMAL, LETTER_X + sMail.signatureX * SCALE, LETTER_Y + sMail.signatureY * SCALE,
                          sMail.signature, ink, shadow, SCALE);
    if (sMail.iconSpecies != SPECIES_NONE)
        UiGfx_MonIconScaled(sMail.iconSpecies, 0, false, 0, LETTER_X + (sMail.iconX - 16) * SCALE,
                            LETTER_Y + (sMail.iconY - 16) * SCALE, SCALE);
    UiG_Hit(HIT_LETTER, 0, LETTER_X, LETTER_Y, DISPLAY_WIDTH * SCALE, DISPLAY_HEIGHT * SCALE);
    if (sMail.ready)
        UiW_Hints(16, 347, &hint, 1, true, 10);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sMail.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending() || !sMail.ready)
        return true;
    if (button == 3 || (button == 1 && hit->id == HIT_LETTER))
    {
        NativeUi_QueuePress(button == 3 ? B_BUTTON : A_BUTTON);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    /* the Bead and Dream Mails' icon too */
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Mail = {
    .name = "mail",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
