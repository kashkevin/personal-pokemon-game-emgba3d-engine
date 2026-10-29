#ifdef ENABLE_NATIVE_UI
/*
 * A contest's results (not one of the 31 designed screens; the design's
 * pieces): the design's board, a row a contestant as the game lists them -
 * its icon, its Pokémon and trainer, round one's stars and round two's hearts
 * as the game adds them, its bar filling as the game's does (the preliminary
 * part, then round two's added or a loss taken from it), its placing once the
 * game puts it up - the winner's row lit while the game's flashes; the
 * announcer's lines on the design's banner, sliding as the game's box does;
 * the winner's banner opening across the middle with its Pokémon's own
 * picture passing through. At the end A (a click) goes on. Project-owned.
 */
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/contest.h"
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
    HIT_BOARD = UI_HIT_SCREEN,
};

#define BOARD_X 80
#define BOARD_W 480
#define ROW_Y 70
#define ROW_H 54
#define ROW_STEP 58
#define BAR_X 286
#define BAR_W 132

static const u8 sText_Next[] = _("Next");

static const char *const sCategories[] = {"COOL", "BEAUTY", "CUTE", "SMART", "TOUGH"};
static const char *const sRanks[] = {"NORMAL RANK", "SUPER RANK", "HYPER RANK", "MASTER RANK"};
static const char *const sPlaces[] = {"", "1ST", "2ND", "3RD", "4TH"};

static const char *const sHeart[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
static const char *const sStar[] = {"...#...", "..###..", "#######", ".#####.", "..#.#..", ".#...#."};

static struct NativeContestResults sR;
static uint32_t sBannerAt, sBannerLast;

/* ---------------------------------------------------------------- state */

static bool Detect(void)
{
    struct NativeContestResults r;

    return ContestUtil_NativeUiResults(&r);
}

static void Capture(void)
{
    ContestUtil_NativeUiResults(&sR);
    if (sR.winnerOpen > 0 && sR.winner < NATIVE_CONTESTANTS)
    {
        const struct NativeContestResult *w = &sR.mons[sR.winner];
        uint32_t now = NativeUi_Frame();

        /* the winner's picture: its own animation from the banner's opening */
        if (now - sBannerLast > 2)
            sBannerAt = now;
        sBannerLast = now;
        UiGfx_PrepareMonPic(w->species, w->personality, w->shiny, false);
    }
}

/* ---------------------------------------------------------------- drawing */

static void Pips(int x, int y, const char *const *rows, int count, int scale, const char *name, UiRGBA colour)
{
    for (int i = 0; i < count; i++)
        UiW_PixScaled(name, rows, 6, (const struct UiPixPal[]){{'#', colour}}, 1, x + i * 8 * scale, y, scale);
}

static void Row(int i, int t)
{
    const struct NativeContestResult *m = &sR.mons[i];
    bool player = i == sR.player, lit = sR.highlight && i == sR.winner && (t / 8) % 2 == 0;
    struct UiRamp r = UiRampDefault(player ? UI_HEX(0x88D070) : UI_HEX(0xF0D058));
    int x = BOARD_X + 8, y = ROW_Y + i * ROW_STEP, w = BOARD_W - 16;
    int fill, pre;

    if (lit)
        r = UiRampDefault(UiMix(r.base, UI_HEX(0xFFFFFF), 0.45));
    UiG_Paint(UiM_Rounded4(w, ROW_H, 4, 4, 4, 4), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(40, 40, 3, 3, 3, 3), x + 7, y + 7, UI_P_CREAM,
              &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiGfx_MonIcon(m->species, m->personality, false, (t / 16) % 2, x + 11, y + 6, NULL);
    UiText_Draw(UI_FONT_NORMAL, x + 56, y + 8, m->nickname, r.ink, UI_NONE, UI_ALIGN_LEFT, 0);
    {
        u8 trainer[24];

        trainer[0] = CHAR_SLASH;
        StringCopyN(trainer + 1, m->trainerName, sizeof(trainer) - 2);
        trainer[sizeof(trainer) - 1] = EOS;
        UiText_Draw(UI_FONT_NORMAL, x + 56, y + 28, trainer, UiMix(r.ink, r.base, 0.25), UI_NONE, UI_ALIGN_LEFT, 0);
    }
    /* round one's stars over round two's hearts, as the game adds them */
    Pips(x + 160, y + 9, sStar, m->stars > 8 ? 8 : m->stars, 1, "cr_star", UI_HEX(0xE8B020));
    Pips(x + 160, y + 29, sHeart, m->hearts > 8 ? 8 : m->hearts, 1, m->lostPoints ? "cr_heartk" : "cr_heart",
         m->lostPoints ? UI_P_INK : UI_HEX(0xE84838));
    /* the bar: the preliminary part, then round two's (a loss shown taken from it) */
    fill = sR.barMax > 0 ? m->bar * BAR_W / sR.barMax : 0;
    pre = sR.barMax > 0 ? m->barPreliminary * BAR_W / sR.barMax : 0;
    if (fill < 0)
        fill = 0;
    if (fill > BAR_W)
        fill = BAR_W;
    if (pre > fill)
        pre = fill;
    UiG_Paint(UiM_Rounded4(BAR_W + 4, 14, 3, 3, 3, 3), x + BAR_X - 2, y + 19, UiMix(r.lo, r.ink, 0.35), &(struct UiPaint){UI_NONE});
    if (pre > 0)
        UiG_Rect(x + BAR_X, y + 21, pre, 10, UI_HEX(0xE8B020));
    if (fill > pre)
        UiG_Rect(x + BAR_X + pre, y + 21, fill - pre, 10, m->lostPoints ? UI_HEX(0x707070) : UI_HEX(0xE84838));
    if (m->place > 0 && m->place < ARRAY_COUNT(sPlaces))
        UiItems_BigCaps(x + w - 30, y + 14, sPlaces[m->place], 2, m->place == 1 ? UI_HEX(0xF8E080) : UI_P_TEXT_W, r.ink,
                        UI_ALIGN_CENTER);
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    char sub[40];

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x242c48));
    UiG_Rect(bx, by, bw, 62, UI_HEX(0x2c3658));
    UiItems_BigCaps(320, 12, "CONTEST RESULTS", 2, UI_HEX(0xF8E080), UI_HEX(0x101828), UI_ALIGN_CENTER);
    snprintf(sub, sizeof(sub), "%s  -  %s", sR.category < ARRAY_COUNT(sCategories) ? sCategories[sR.category] : "",
             sR.rank < ARRAY_COUNT(sRanks) ? sRanks[sR.rank] : "");
    UiCaps_Draw(320, 46, sub, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    for (int i = 0; i < NATIVE_CONTESTANTS; i++)
        Row(i, t);
    UiG_Hit(HIT_BOARD, 0, bx, by, bw, bh);
    /* the announcer's banner where the game's box slides (its middle, GBA pixels: twice that from the picture's left) */
    if (sR.banner != NULL)
    {
        int tw = UiText_Width(UI_FONT_NORMAL, sR.banner, 0) + 48, cx = 80 + sR.bannerX * 2, y = by + bh - 42;

        UiG_Paint(UiM_Rounded4(tw, 28, 6, 6, 6, 6), cx - tw / 2, y, UI_P_PAPER,
                  &(struct UiPaint){UI_P_INK, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NORMAL, cx, y + 6, sR.banner, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    /* the winner's banner across the middle as the game's opens, its Pokémon passing through */
    if (sR.winnerOpen > 0)
    {
        int open = sR.winnerOpen > 64 ? 64 : sR.winnerOpen, cy = by + bh / 2, top = cy - open, h = open * 2;
        const struct NativeContestResult *w = &sR.mons[sR.winner < NATIVE_CONTESTANTS ? sR.winner : 0];

        UiG_Rect(bx, top, bw, h, UiMix(UI_HEX(0xF8E080), UI_HEX(0xE8B020), 0.5));
        UiG_Rect(bx, top, bw, 2, UI_HEX(0xFFF8D0));
        UiG_Rect(bx, top + h - 2, bw, 2, UI_HEX(0xA87818));
        if (sR.winnerShown)
        {
            uint32_t f = NativeUi_Frame() - sBannerAt;

            UiG_Save();
            UiG_Clip(bx, top, bw, h);
            UiGfx_MonPicEx(w->species, w->personality, w->shiny, false,
                           UiGfx_MonPicFrameAt(w->species, w->personality, w->shiny, false, (int)f), 80 + sR.winnerX * 2,
                           cy + 56, 2, 1.0, NULL);
            UiG_Restore();
        }
    }
    if (sR.waiting)
    {
        struct UiHintItem next = {UI_ACT_CONFIRM, sText_Next};

        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(&next, 1, 10), by + bh - 28, &next, 1, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sR.waiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (button == 1 && sR.waiting && !NativeUi_PressesPending())
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_ContestResults = {
    .name = "contest_results",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
