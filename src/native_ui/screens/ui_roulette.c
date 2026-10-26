#ifdef ENABLE_NATIVE_UI
/*
 * 24 · The Game Corner's roulette tables at the PC's resolution, in the slot
 * machine screen's look (ui_slot_machine.c: casino carpet, gold frames,
 * chasing bulbs, reel digits): the owner's "make the HUD / screen match
 * resolution, graphics and current UI" (2026-10-25). The table is the
 * engine's own (src/roulette.c): its wheel, balls, board, bets, payouts and
 * messages run as before, and every key reaches it unchanged; this screen
 * only draws what Roulette_NativeUiState says it shows - the wheel turning
 * with its twelve slots and the balls on it, the board with the squares hit
 * this round, the cursor or the bet, the multiplier, the balls left, the
 * player's COINS (the table's credit and the Coin Case's reserve) and the
 * payout counting, the table's messages and its YES / NO.
 * Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "roulette.h"
#include "constants/items.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define SCALE 2           /* GBA pixels to the screen's */
#define WHEEL_X 16
#define WHEEL_Y 36
#define WHEEL_W 300
#define WHEEL_H 256
#define WHEEL_CX (WHEEL_X + WHEEL_W / 2)
#define WHEEL_CY (WHEEL_Y + WHEEL_H / 2)
/* the GBA wheel's pixels to the screen's: its balls start 68 out (Task_InitBallRoll), its icons sit 40 out
 * (CreateWheelIconSprite); both inside the drawn rim */
#define WHEEL_SCALE 1.6
#define ICON_RADIUS ((int)(40 * WHEEL_SCALE))
#define BOARD_X 328
#define BOARD_Y 36
#define BOARD_W 304
#define BOARD_H 256
#define CELL_W 52
#define CELL_H 44
#define GRID_X (BOARD_X + 22)
#define GRID_Y (BOARD_Y + 20)

static const u8 sText_Bet[] = _("Bet");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Quit[] = _("Quit");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Skip[] = _("Skip");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_No[] = _("No");

static const u16 sSpecies[4] = {SPECIES_WYNAUT, SPECIES_AZURILL, SPECIES_SKITTY, SPECIES_MAKUHITA};

static struct RouletteNativeState sS;
static bool sUp;

static bool Detect(void)
{
    struct RouletteNativeState probe;

    return Roulette_NativeUiState(&probe);
}

static void Capture(void)
{
    sUp = Roulette_NativeUiState(&sS);
}

/* ---------------------------------------------------------------- the room */

static struct UiRamp Red(void)
{
    return UiRampOf(UI_P_CMD_FIGHT, 0.42, 0.25, 0.62);
}

static struct UiRamp Gold(void)
{
    return UiRampOf(UI_P_POCKETS_KEY, 0.45, 0.25, 0.6);
}

static UiRGBA Felt(void)
{
    return UiMix(UI_HEX(0x2E8A4E), UI_P_INK, 0.35);
}

static UiRGBA RowColour(int row)
{
    static const u32 sRows[3] = {0xF08A2E, 0x4CB050, 0x9A5CCF}; /* orange, green, purple */

    return UI_HEX(sRows[row]);
}

struct CarpetCtx
{
    int x0;
    int y0;
};

static bool CarpetDiamonds(int x, int y, const void *ctx)
{
    const struct CarpetCtx *c = ctx;
    int cx = x + c->x0, cy = y + c->y0;

    return ((((cx + cy + 8192) / 16) + ((cx - cy + 8192) / 16)) & 1) == 0;
}

static void Carpet(void)
{
    UiRGBA carpet = UiMix(UI_P_CMD_FIGHT, UI_P_INK, 0.58), carpet2 = UiMix(UI_P_CMD_FIGHT, UI_P_INK, 0.46);
    struct CarpetCtx c;
    int x0, y0, w, h;
    char tag[48];

    UiG_Bleed(&x0, &y0, &w, &h);
    c.x0 = x0;
    c.y0 = y0;
    UiG_Rect(x0, y0, w, h, carpet);
    snprintf(tag, sizeof(tag), "rlcarpet%dx%d:%d,%d", w, h, x0, y0);
    UiG_Mask(UiM_Fn(w, h, tag, CarpetDiamonds, &c), x0, y0, carpet2);
}

static void Bulb(int x, int y, bool on)
{
    static const char *const rows[5] = {".aaa.", "abbba", "abcba", "abbba", ".aaa."};
    struct UiRamp g = Gold();
    struct UiPixPal lit[3] = {{'a', g.ink}, {'b', UI_P_HP_YELLOW_0}, {'c', UI_P_PAPER}};
    struct UiPixPal off[3] = {{'a', g.ink}, {'b', UiMix(g.lo, UI_P_INK, 0.4)}, {'c', UiMix(g.lo, UI_P_INK, 0.25)}};

    UiW_Pix(on ? "rl_bulb_on" : "rl_bulb_off", rows, 5, on ? lit : off, 3, x, y, false);
}

/* bulbs around a rectangle, chasing; faster and alternating on a win */
static void BulbRing(int x, int y, int w, int h, int step, int t, bool win)
{
    int i = 0, ph = t / (win ? 3 : 8);

    for (int bx = x; bx <= x + w - 5; bx += step, i++)
        Bulb(bx, y, win ? (ph & 1) == (i & 1) : (i + ph) % 3 != 0);
    for (int by = y + step; by <= y + h - 5 - step; by += step, i++)
        Bulb(x + w - 5, by, win ? (ph & 1) == (i & 1) : (i + ph) % 3 != 0);
    for (int bx = x + w - 5; bx >= x; bx -= step, i++)
        Bulb(bx, y + h - 5, win ? (ph & 1) == (i & 1) : (i + ph) % 3 != 0);
    for (int by = y + h - 5 - step; by >= y + step; by -= step, i++)
        Bulb(x, by, win ? (ph & 1) == (i & 1) : (i + ph) % 3 != 0);
}

/* a gold-framed panel on the carpet */
static void Frame(int x, int y, int w, int h, UiRGBA inside)
{
    struct UiRamp gold = Gold(), red = Red();

    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 12, h - 12, 3, 3, 3, 3), x + 6, y + 6, inside,
              &(struct UiPaint){red.ink, UiMix(inside, UI_HEX(0xFFFFFF), 0.15), UI_NONE, 0, 0, NULL, UI_NONE});
}

static void Disc(int cx, int cy, int d, UiRGBA body, UiRGBA ink)
{
    UiG_Paint(UiM_Circle(d), cx - d / 2, cy - d / 2, body, &(struct UiPaint){ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
}

/* reel digits: `digits` paper windows, unlit leading zeros (the slot screen's) */
static void Counter(int x, int y, const char *label, long value, int digits)
{
    struct UiRamp gold = Gold(), red = Red();
    char text[12];
    int lead, len;

    if (value < 0)
        value = 0;
    snprintf(text, sizeof(text), "%0*ld", digits, value);
    len = (int)snprintf(NULL, 0, "%ld", value);
    lead = digits - len;
    UiCaps_Draw(x, y, label, gold.hi, red.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < digits; i++)
    {
        int cx = x + i * 21, cy = y + 12;
        char d[2] = {text[i], 0};

        UiG_Paint(UiM_Rounded4(19, 32, 2, 2, 2, 2), cx, cy, UI_P_PAPER,
                  &(struct UiPaint){red.ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiG_HLine(cx + 2, cy + 15, 15, UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.6));
        UiItems_BigCaps(cx + 10, cy + 6, d, 2, i < lead ? UI_P_PAPER_LO : UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER);
    }
}

/* ---------------------------------------------------------------- the wheel */

static void Wheel(int t)
{
    struct UiRamp gold = Gold();
    bool landed = sS.phase == ROULETTE_NATIVE_RESULT;

    Frame(WHEEL_X, WHEEL_Y, WHEEL_W, WHEEL_H, Felt());
    BulbRing(WHEEL_X + 8, WHEEL_Y + 8, WHEEL_W - 16, WHEEL_H - 16, 12, t, landed && sS.won);
    /* the bowl: a gold rim, a dark track the balls roll on, the slots' ring, the hub */
    Disc(WHEEL_CX, WHEEL_CY, 238, gold.base, gold.ink);
    Disc(WHEEL_CX, WHEEL_CY, 226, UiMix(UI_HEX(0x3A2414), UI_P_INK, 0.2), gold.ink);
    Disc(WHEEL_CX, WHEEL_CY, 176, UiMix(gold.lo, UI_P_INK, 0.35), gold.ink);
    for (int i = 0; i < ROULETTE_NATIVE_SQUARES; i++)
    {
        int row = i / 4, col = i % 4;
        int selection = (row + 1) * 5 + col + 1;
        double a = (sS.wheelAngle + sS.squareAngle[i]) * M_PI / 180.0;
        int x = WHEEL_CX + (int)lround(sin(a) * ICON_RADIUS), y = WHEEL_CY - (int)lround(cos(a) * ICON_RADIUS);
        bool hit = (sS.hitFlags & (1u << selection)) != 0;
        bool winning = landed && sS.winningSquare == selection;
        UiRGBA slot = RowColour(row);

        if (hit && !winning)
            slot = UiMix(slot, UI_P_INK, 0.55);
        if (winning && (t / 6) % 2 == 0)
            slot = UiMix(slot, UI_HEX(0xFFFFFF), 0.6);
        Disc(x, y, 40, slot, gold.ink);
        UiGfx_MonIcon(sSpecies[col], 0, false, (t / 16) % 2, x - 16, y - 20, NULL);
    }
    Disc(WHEEL_CX, WHEEL_CY, 60, gold.base, gold.ink);
    Disc(WHEEL_CX, WHEEL_CY, 44, UiMix(gold.hi, UI_HEX(0xFFFFFF), 0.3), gold.ink);
    UiItems_BigCaps(WHEEL_CX, WHEEL_CY - 8, sS.tableId == 0 ? "L" : "R", 2, UiMix(gold.ink, UI_P_INK, 0.3), UI_NONE,
                    UI_ALIGN_CENTER);
    for (int b = 0; b < ROULETTE_NATIVE_BALLS; b++)
    {
        if (!(sS.ballShown & (1 << b)))
            continue;
        int x = WHEEL_CX + (int)lround(sS.ballX[b] * WHEEL_SCALE), y = WHEEL_CY + (int)lround(sS.ballY[b] * WHEEL_SCALE);

        Disc(x, y, 12, UI_HEX(0xFFFFFF), UI_P_INK);
        UiG_Rect(x - 3, y - 3, 2, 2, UI_P_PAPER_LO); /* a glint */
    }
    /* the round's six balls: lit while still to throw */
    UiCaps_Draw(WHEEL_X + 22, WHEEL_Y + 22, "BALLS", gold.hi, Red().ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < 6; i++)
        Disc(WHEEL_X + 30, WHEEL_Y + 44 + i * 18, 12, i < sS.ballsLeft ? UI_HEX(0xFFFFFF) : UiMix(Felt(), UI_P_INK, 0.4),
             UI_P_INK);
}

/* ---------------------------------------------------------------- the board */

/* whether the selection covers grid square (row 1-3, column 1-4) */
static bool Covers(int selection, int row, int col)
{
    int r = selection / 5, c = selection % 5;

    if (selection == 0)
        return false;
    return (r == 0 && c == col) || (c == 0 && r == row) || (r == row && c == col);
}

static void Board(int t)
{
    struct UiRamp gold = Gold(), red = Red();
    UiRGBA felt = Felt();
    int shown = sS.phase == ROULETTE_NATIVE_BET ? sS.cursor : sS.bet;
    bool betting = sS.phase == ROULETTE_NATIVE_BET;

    Frame(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, felt);
    /* the column heads: the four Pokemon; the row heads: the three colours */
    for (int col = 1; col <= 4; col++)
    {
        int x = GRID_X + col * CELL_W;
        bool sel = shown == col;

        UiG_Paint(UiM_Rounded4(CELL_W - 4, CELL_H - 4, 4, 4, 4, 4), x + 2, GRID_Y + 2,
                  sel ? UiMix(gold.hi, UI_HEX(0xFFFFFF), 0.3) : UiMix(felt, UI_HEX(0xFFFFFF), 0.12),
                  &(struct UiPaint){gold.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiGfx_MonIcon(sSpecies[col - 1], 0, false, sel ? (t / 12) % 2 : 0, x + CELL_W / 2 - 16, GRID_Y + 4, NULL);
    }
    for (int row = 1; row <= 3; row++)
    {
        int y = GRID_Y + row * CELL_H;
        bool sel = shown == row * 5;

        UiG_Paint(UiM_Rounded4(CELL_W - 4, CELL_H - 4, 4, 4, 4, 4), GRID_X + 2, y + 2, RowColour(row - 1),
                  &(struct UiPaint){sel ? UI_HEX(0xFFFFFF) : gold.ink, UiMix(RowColour(row - 1), UI_HEX(0xFFFFFF), 0.3), UI_NONE,
                                    0, 0, NULL, UI_NONE});
        for (int col = 1; col <= 4; col++)
        {
            int x = GRID_X + col * CELL_W;
            int selection = row * 5 + col;
            bool hit = (sS.hitFlags & (1u << selection)) != 0;
            bool covered = Covers(shown, row, col);
            bool winning = sS.phase == ROULETTE_NATIVE_RESULT && sS.winningSquare == selection;
            UiRGBA body = UiMix(RowColour(row - 1), felt, 0.55);

            if (covered)
                body = betting ? UiMix(UI_P_HP_YELLOW_0, body, (t / 10) % 2 ? 0.35 : 0.15) : UiMix(gold.hi, body, 0.4);
            if (winning && (t / 6) % 2 == 0)
                body = UI_HEX(0xFFFFFF);
            UiG_Paint(UiM_Rounded4(CELL_W - 4, CELL_H - 4, 3, 3, 3, 3), x + 2, y + 2, body,
                      &(struct UiPaint){covered ? UI_P_INK : gold.ink, UiMix(body, UI_HEX(0xFFFFFF), 0.25), UI_NONE, 0, 0,
                                        NULL, UI_NONE});
            UiGfx_MonIcon(sSpecies[col - 1], 0, false, 0, x + CELL_W / 2 - 16, y + 2,
                          hit ? &(struct UiFilter){.grayscale = 0.85f, .brightness = 0.7f, .silhouette = UI_NONE} : NULL);
            if (hit)
                Disc(x + CELL_W - 12, y + CELL_H - 12, 12, UI_HEX(0xFFFFFF), UI_P_INK); /* the ball that landed there */
        }
    }
    /* under the grid: what the cursor's bet pays (BET) or the bet placed, and the credit */
    {
        int y = GRID_Y + 4 * CELL_H + 6;
        char buf[24];

        if (sS.payout > 0)
            Counter(GRID_X + 4, y, "PAYOUT", sS.payout, 5);
        else
        {
            UiCaps_Draw(GRID_X + 4, y, betting ? "PAYS" : "BET PAYS", gold.hi, red.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
            if (shown != 0 && sS.multiplier != 0)
                snprintf(buf, sizeof(buf), "x%d", sS.multiplier);
            else
                snprintf(buf, sizeof(buf), "-");
            UiItems_BigCaps(GRID_X + 4, y + 14, buf, 3, UI_P_HP_YELLOW_0, UI_HEX(0x382008), UI_ALIGN_LEFT);
        }
        /* the player's coins: the table's credit and what a big Coin Case keeps in reserve while it plays a stake (the
         * credit alone read as lost coins: 30,000 coins showed 5,000) */
        Counter(GRID_X + 5 * CELL_W - 6 * 21, y, "COINS", (long)sS.wallet, 6);
    }
}

/* ---------------------------------------------------------------- the bottom */

static void Bottom(int t)
{
    struct UiRamp red = Red();
    int b[4];

    UiW_DialogBox(8, 300, 464, 54, NULL, UI_NONE, b);
    if (sS.message != NULL)
        UiW_MessagePage(sS.message, -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
    if (sS.waitingForKey && sS.phase != ROULETTE_NATIVE_ASK)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Paint(UiM_Rounded4(160, 54, 3, 0, 3, 0), 480, 300, UI_P_CREAM,
              &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (sS.phase == ROULETTE_NATIVE_ASK)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Choose}}, c[1] = {{UI_ACT_CANCEL, sText_No}};

        UiW_Hints(490, 309, a, 1, false, 8);
        UiW_Hints(490, 329, c, 1, false, 8);
        /* in the panel: over the board it covered the coins */
        UiW_YesNo(634, 352, sS.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sS.phase == ROULETTE_NATIVE_BET && !sS.waitingForKey)
    {
        struct UiHintItem a[2] = {{UI_ACT_CONFIRM, sText_Bet}, {UI_ACT_MOVE, sText_Move}};
        struct UiHintItem c[1] = {{UI_ACT_CANCEL, sText_Quit}};

        UiW_Hints(490, 309, a, 2, false, 8);
        UiW_Hints(490, 329, c, 1, false, 8);
    }
    else if (sS.payout > 0)
    {
        /* A adds the rest of the payout at once (roulette.c Task_GivePayout) */
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Skip}};

        UiW_Hints(490, 309, a, 1, false, 8);
    }
    else if (sS.waitingForKey)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Next}};

        UiW_Hints(490, 309, a, 1, false, 8);
    }
}

static void Draw(int t)
{
    struct UiRamp red = Red();

    if (!sUp)
        return;
    Carpet();
    {
        char title[48];

        snprintf(title, sizeof(title), "ROULETTE  %d COINS A BALL%s", sS.minBet, sS.specialRate ? "  SPECIAL RATE" : "");
        UiW_TitleTab(0, 8, title, &red, 0, NULL);
    }
    Wheel(t);
    Board(t);
    Bottom(t);
    UiG_Veil(NativeUi_ScreenFade());
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Roulette = {
    .name = "roulette",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif /* ENABLE_NATIVE_UI */
