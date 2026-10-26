#ifdef ENABLE_NATIVE_UI
/*
 * 23 · The Game Corner's slot machine at the PC's resolution, in the Game
 * Corner prize screen's look (ui_game_corner.c: casino carpet, gold frames,
 * chasing bulbs, reel digits): the owner's "make the HUD / screen match
 * resolution, graphics and current UI" (2026-10-25). The machine is the
 * engine's own (src/slot_machine.c): its reels, bets, payouts, Pika Power and
 * Reel Time run as before, and every key reaches it unchanged; this screen
 * only draws what SlotMachine_NativeUiState says it shows - the three reels
 * (the game's own symbol art, where its sprites are), the lines the bet
 * plays and those that won, CREDIT and PAYOUT, the bet lamps, the Pika Power
 * bolts, the machine's messages and its "Quit the game?" YES / NO, and the
 * payout table SELECT opens. Project-owned.
 */
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "slot_machine.h"
#include "platform/casino_rates.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define SCALE 2            /* GBA pixels to the screen's */
#define REEL_W 88
#define REEL_GAP 14
#define REEL_TOP 84        /* the reel windows' top: GBA y 40 */
#define REEL_GBA_TOP 40
#define REEL_GBA_BOTTOM 112
#define REEL_H ((REEL_GBA_BOTTOM - REEL_GBA_TOP) * SCALE)
#define CAB_X 16
#define CAB_Y 36
#define CAB_W 408
#define CAB_H 256

static const u8 sText_Bet[] = _("Bet");
static const u8 sText_Spin[] = _("Spin");
static const u8 sText_Stop[] = _("Stop");
static const u8 sText_Quit[] = _("Quit");
static const u8 sText_Info[] = _("Payouts");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Close[] = _("Close");
#if CASINO_SLOT_COINS_PER_BET != 5
#error "sText_BetLine names the bet: 5 COINS each, 15 for the three"
#endif
static const u8 sText_BetLine[] = _("Each bet is 5 COINS: DOWN adds up to 3, A spins.\nA with none in bets 3 (15 COINS).");
static const u8 sText_SpinLine[] = _("The reels are turning. A stops them,\none after another.");
static const u8 sText_LoseLine[] = _("No match this time.");
static const u8 sText_QuitLine[] = _("Quit the game?");
static const u8 sText_ReelTimeLine[] = _("REEL TIME! Take aim and nail the marks\nfor the awarded spins.");

static struct SlotMachineNativeState sS;
static bool sUp;

static bool Detect(void)
{
    struct SlotMachineNativeState probe;

    return SlotMachine_NativeUiState(&probe);
}

static void Capture(void)
{
    sUp = SlotMachine_NativeUiState(&sS);
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

static UiRGBA Plate(void)
{
    struct UiRamp r = Red();

    return UiMix(r.base, r.ink, 0.45);
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
    snprintf(tag, sizeof(tag), "smcarpet%dx%d:%d,%d", w, h, x0, y0);
    UiG_Mask(UiM_Fn(w, h, tag, CarpetDiamonds, &c), x0, y0, carpet2);
}

static void Bulb(int x, int y, bool on)
{
    static const char *const rows[5] = {".aaa.", "abbba", "abcba", "abbba", ".aaa."};
    struct UiRamp g = Gold();
    struct UiPixPal lit[3] = {{'a', g.ink}, {'b', UI_P_HP_YELLOW_0}, {'c', UI_P_PAPER}};
    struct UiPixPal off[3] = {{'a', g.ink}, {'b', UiMix(g.lo, UI_P_INK, 0.4)}, {'c', UiMix(g.lo, UI_P_INK, 0.25)}};

    UiW_Pix(on ? "sm_bulb_on" : "sm_bulb_off", rows, 5, on ? lit : off, 3, x, y, false);
}

/* bulbs around a rectangle, chasing; faster and all lit on a win */
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

/* reel digits: `digits` paper windows, unlit leading zeros */
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

/* ---------------------------------------------------------------- the machine */

static int ReelX(int reel)
{
    return CAB_X + (CAB_W - (3 * REEL_W + 2 * REEL_GAP)) / 2 + reel * (REEL_W + REEL_GAP);
}

/* a GBA row centre (52, 76, 100 at rest) on the screen */
static int RowY(int gbaY)
{
    return REEL_TOP + (gbaY - REEL_GBA_TOP) * SCALE;
}

static void Symbol(int symbol, int cx, int cy)
{
    const u8 *tiles = SlotMachine_NativeUiSymbolTiles(symbol);

    if (tiles != NULL)
        UiGfx_FieldSpriteSized(tiles, 1, SlotMachine_NativeUiSymbolPalette(), 0, 32, 32, cx - 16 * SCALE,
                               cy - 16 * SCALE, SCALE, false);
}

static void Reels(int t)
{
    struct UiRamp gold = Gold(), red = Red();

    for (int r = 0; r < SLOT_NATIVE_REELS; r++)
    {
        int x = ReelX(r);
        bool next = sS.phase == SLOT_NATIVE_SPIN && (sS.reelsTurning & (1 << r)) && sS.currentReel == r;

        /* the drum: paper, shaded top and bottom like a turning cylinder */
        UiG_Paint(UiM_Rounded4(REEL_W + 8, REEL_H + 8, 3, 3, 3, 3), x - 4, REEL_TOP - 4, gold.base,
                  &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 1, NULL, UI_NONE});
        UiG_Rect(x, REEL_TOP, REEL_W, REEL_H, UI_HEX(0xFFFFFF));
        for (int i = 0; i < 14; i++)
        {
            UiRGBA shade = UiMix(UI_HEX(0xFFFFFF), UI_P_PAPER_LO, (14 - i) / 14.0 * 0.85);

            UiG_HLine(x, REEL_TOP + i, REEL_W, shade);
            UiG_HLine(x, REEL_TOP + REEL_H - 1 - i, REEL_W, shade);
        }
        UiG_Save();
        UiG_Clip(x, REEL_TOP, REEL_W, REEL_H);
        for (int j = 0; j < SLOT_NATIVE_ROWS; j++)
        {
            int y = sS.symbolY[r][j];

            if (y + 16 >= REEL_GBA_TOP && y - 16 <= REEL_GBA_BOTTOM)
                Symbol(sS.symbol[r][j], x + REEL_W / 2, RowY(y));
        }
        UiG_Restore();
        /* the stop button under the reel: lit for the one A stops next */
        UiG_Paint(UiM_Rounded4(34, 14, 6, 6, 6, 6), x + REEL_W / 2 - 17, REEL_TOP + REEL_H + 10,
                  next && (t / 10) % 2 == 0 ? UI_P_HP_YELLOW_0 : (sS.reelsTurning & (1 << r)) ? red.base : UiMix(red.lo, UI_P_INK, 0.3),
                  &(struct UiPaint){red.ink, UI_HEX(0xFFFFFF), UI_NONE, 0, 0, NULL, UI_NONE});
    }
}

/* the five lines: dim when the bet does not play them, gold when it does, flashing when they won */
static void Lines(int t, bool bars)
{
    static const u8 sRows[SLOT_NATIVE_LINES][2] = {{76, 76}, {52, 52}, {100, 100}, {52, 100}, {100, 52}};
    static const u8 sBet[SLOT_NATIVE_LINES] = {1, 2, 2, 3, 3};
    struct UiRamp gold = Gold();
    int x0 = ReelX(0) - 22, x1 = ReelX(2) + REEL_W + 22;

    for (int l = 0; l < SLOT_NATIVE_LINES; l++)
    {
        bool played = sS.bet >= sBet[l], won = (sS.linesLit & (1 << l)) != 0;
        UiRGBA c = won ? ((t / 6) % 2 ? UI_P_HP_YELLOW_0 : UI_HEX(0xFFFFFF)) : played ? gold.hi : UiMix(gold.lo, UI_P_INK, 0.5);
        int ya = RowY(sRows[l][0]), yb = RowY(sRows[l][1]);

        /* the lamps at both ends of the line, numbered as the machine's (1 middle, 2 top and bottom, 3 diagonals) */
        char n[2] = {(char)('0' + sBet[l]), 0};

        if (bars)
        {
            if (won)
            {
                /* the winning line across the reels: a 3-pixel bar, stepped along a diagonal */
                int steps = 64;

                UiG_Save();
                UiG_Alpha(0.85);
                for (int k = 0; k <= steps; k++)
                {
                    int x = x0 + 8 + (x1 - x0 - 16) * k / steps, y = ya + (yb - ya) * k / steps;

                    UiG_Rect(x, y - 1, (x1 - x0 - 16) / steps + 1, 3, c);
                }
                UiG_Restore();
            }
            continue;
        }
        UiG_Paint(UiM_Rounded4(16, 14, 4, 4, 4, 4), x0 - 8, ya - 7, c,
                  &(struct UiPaint){gold.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(x0, ya - 3, n, UI_P_INK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        UiG_Paint(UiM_Rounded4(16, 14, 4, 4, 4, 4), x1 - 8, yb - 7, c,
                  &(struct UiPaint){gold.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(x1, yb - 3, n, UI_P_INK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    }
}

static void PikaPower(int x, int y)
{
    struct UiRamp gold = Gold(), red = Red();

    UiCaps_Draw(x, y + 2, "PIKA POWER", gold.hi, red.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < 16; i++)
    {
        bool on = i < sS.pikaPowerBolts;

        UiG_Paint(UiM_Rounded4(10, 12, 2, 2, 2, 2), x + 76 + i * 13, y, on ? UI_P_HP_YELLOW_0 : UiMix(gold.lo, UI_P_INK, 0.45),
                  &(struct UiPaint){gold.ink, on ? UI_HEX(0xFFFFFF) : UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    }
}

static void BetLamps(int x, int y)
{
    struct UiRamp gold = Gold(), red = Red();

    UiCaps_Draw(x, y + 4, "BET", gold.hi, red.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < 3; i++)
    {
        char n[2] = {(char)('1' + i), 0};
        bool on = sS.bet > i;

        UiG_Paint(UiM_Rounded4(26, 18, 4, 4, 4, 4), x + 30 + i * 32, y, on ? UI_P_HP_YELLOW_0 : UiMix(red.lo, UI_P_INK, 0.35),
                  &(struct UiPaint){red.ink, on ? UI_HEX(0xFFFFFF) : UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(x + 43 + i * 32, y + 6, n, on ? UI_P_INK : UiMix(red.lo, UI_P_PAPER, 0.4), UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_CENTER, UI_NONE);
    }
}

static void Cabinet(int t)
{
    struct UiRamp gold = Gold(), red = Red();
    UiRGBA plate = Plate();
    bool win = sS.phase == SLOT_NATIVE_WIN;

    UiG_Paint(UiM_Rounded4(CAB_W, CAB_H, 6, 6, 6, 6), CAB_X, CAB_Y, gold.base,
              &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(CAB_W - 12, CAB_H - 12, 3, 3, 3, 3), CAB_X + 6, CAB_Y + 6, plate,
              &(struct UiPaint){red.ink, UiMix(plate, red.base, 0.5), UI_NONE, 0, 0, NULL, UI_NONE});
    BulbRing(CAB_X + 8, CAB_Y + 8, CAB_W - 16, CAB_H - 16, 10, t, win);
    PikaPower(CAB_X + 30, CAB_Y + 22);
    Lines(t, false);
    Reels(t);
    Lines(t, true);
    BetLamps(CAB_X + 30, CAB_Y + CAB_H - 40);
}

/* the right column: the counters and what the machine's own display says */
static void Display(int t)
{
    struct UiRamp gold = Gold(), red = Red();
    UiRGBA plate = Plate();
    int x = 436, y = 36, w = 196, h = 256;
    const char *big = NULL, *small = NULL;
    char buf[32];

    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 12, h - 12, 3, 3, 3, 3), x + 6, y + 6, plate,
              &(struct UiPaint){red.ink, UiMix(plate, red.base, 0.5), UI_NONE, 0, 0, NULL, UI_NONE});
    UiGfx_ItemIconScaled(ITEM_COIN_CASE, x + 14, y + 14, 1);
    Counter(x + 48, y + 16, "CREDIT", sS.coins, 6);
    Counter(x + 48, y + 66, "PAYOUT", sS.payout, 6);
    /* the digital display: dark glass with lit caps */
    UiG_Paint(UiM_Rounded4(w - 28, 96, 4, 4, 4, 4), x + 14, y + 120, UI_HEX(0x181410),
              &(struct UiPaint){gold.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    switch (sS.phase)
    {
    case SLOT_NATIVE_BET:
        big = sS.bet == 0 ? "INSERT" : "BET";
        snprintf(buf, sizeof(buf), "%d OF 3 IN", sS.bet);
        small = buf;
        break;
    case SLOT_NATIVE_SPIN:
        big = "STOP";
        snprintf(buf, sizeof(buf), "REEL %d", sS.currentReel + 1);
        small = buf;
        break;
    case SLOT_NATIVE_WIN:
        big = (t / 8) % 2 ? "WIN!" : "WIN";
        snprintf(buf, sizeof(buf), "+%d COINS", sS.payout);
        small = buf;
        break;
    case SLOT_NATIVE_LOSE:
        big = "LOSE";
        break;
    case SLOT_NATIVE_ASK_QUIT:
        big = "QUIT?";
        break;
    case SLOT_NATIVE_INFO:
        big = "PAYOUTS";
        small = "B TO CLOSE";
        break;
    case SLOT_NATIVE_REEL_TIME:
        big = "REEL TIME";
        snprintf(buf, sizeof(buf), "%d SPINS", sS.reelTimeSpinsLeft);
        small = sS.reelTimeSpinsLeft > 0 ? buf : NULL;
        break;
    default:
        break;
    }
    if (sS.reelTimeSpinsLeft > 0 && sS.phase != SLOT_NATIVE_REEL_TIME)
    {
        snprintf(buf, sizeof(buf), "REEL TIME x%d", sS.reelTimeSpinsLeft);
        small = buf;
    }
    if (big != NULL)
        UiItems_BigCaps(x + w / 2, y + 140, big, 3, UI_P_HP_YELLOW_0, UI_HEX(0x382008), UI_ALIGN_CENTER);
    if (small != NULL)
        UiCaps_Draw(x + w / 2, y + 190, small, UI_P_HP_YELLOW_1, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiCaps_Draw(x + w / 2, y + 228, "MACHINE", UiMix(gold.hi, UI_P_PAPER, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    snprintf(buf, sizeof(buf), "No. %d", sS.machineId + 1);
    UiCaps_Draw(x + w / 2, y + 238, buf, UiMix(gold.hi, UI_P_PAPER, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
}

/* SELECT: the payout table over the reels */
static void PayoutTable(void)
{
    /* each row: the symbols (-1 none), its payout's index in the machine's table (include/slot_machine.h) */
    static const struct { s8 sym[3]; u8 payout; const char *what; } sRows[] = {
        {{0, 0, 0}, 7, "RED 7s"},     {{1, 1, 1}, 8, "BLUE 7s"},   {{0, 1, 0}, 6, "MIXED 7s"},
        {{2, 2, 2}, 4, "AZURILL"},    {{3, 3, 3}, 3, "LOTAD"},     {{5, 5, 5}, 5, "POWER"},
        {{4, -1, -1}, 0, "CHERRY"},   {{6, 6, 6}, 2, "REPLAY"},
    };
    struct UiRamp gold = Gold(), red = Red();
    int x = CAB_X + 20, y = CAB_Y + 18, w = CAB_W - 40, h = CAB_H - 36;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UI_P_CREAM, &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + w / 2, y + 8, "PAYOUTS PER LINE", red.base, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    for (int i = 0; i < (int)ARRAY_COUNT(sRows); i++)
    {
        int col = i / 4, row = i % 4, rx = x + 12 + col * (w / 2), ry = y + 26 + row * 46;
        char buf[16];

        for (int k = 0; k < 3; k++)
        {
            const u8 *tiles = sRows[i].sym[k] >= 0 ? SlotMachine_NativeUiSymbolTiles(sRows[i].sym[k]) : NULL;

            if (tiles != NULL)
                UiGfx_FieldSpriteSized(tiles, 1, SlotMachine_NativeUiSymbolPalette(), 0, 32, 32, rx + k * 30, ry + 2, 1, false);
        }
        UiCaps_Draw(rx + 96, ry + 8, sRows[i].what, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (sS.payouts[sRows[i].payout] == 0)
            snprintf(buf, sizeof(buf), "FREE SPIN");
        else
            snprintf(buf, sizeof(buf), UI_CAPS_TIMES "%d", sS.payouts[sRows[i].payout]);
        UiCaps_Draw(rx + 96, ry + 20, buf, gold.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    UiCaps_Draw(x + w / 2, y + h - 14, "BET 1: MIDDLE LINE " UI_CAPS_DOT " 2: +TOP AND BOTTOM " UI_CAPS_DOT " 3: +DIAGONALS",
                UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
}

static void Bottom(int t)
{
    struct UiRamp red = Red();
    const u8 *line = NULL;
    int b[4];

    switch (sS.phase)
    {
    case SLOT_NATIVE_BET:       line = sText_BetLine; break;
    case SLOT_NATIVE_SPIN:      line = sText_SpinLine; break;
    case SLOT_NATIVE_LOSE:      line = sText_LoseLine; break;
    case SLOT_NATIVE_REEL_TIME: line = sText_ReelTimeLine; break;
    case SLOT_NATIVE_ASK_QUIT:  line = sText_QuitLine; break;
    case SLOT_NATIVE_MESSAGE:   line = sS.message; break;
    default: break;
    }
    UiW_DialogBox(8, 300, 464, 54, NULL, UI_NONE, b);
    if (sS.phase == SLOT_NATIVE_WIN)
    {
        u8 text[48];

        text[0] = EOS;
        UiText_AppendAscii(text, sizeof(text), "You won ");
        UiText_AppendNumber(text, sizeof(text), sS.payout, 1, true);
        UiText_AppendAscii(text, sizeof(text), " COINS!");
        UiW_MessagePage(text, -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
    }
    else if (line != NULL)
        UiW_MessagePage(line, -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
    if (sS.phase == SLOT_NATIVE_MESSAGE)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Paint(UiM_Rounded4(160, 54, 3, 0, 3, 0), 480, 300, UI_P_CREAM,
              &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (sS.phase == SLOT_NATIVE_ASK_QUIT)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Choose}}, c[1] = {{UI_ACT_CANCEL, sText_No}};

        UiW_Hints(490, 309, a, 1, false, 8);
        UiW_Hints(490, 329, c, 1, false, 8);
        UiW_YesNo(632, 296, sS.quitCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sS.phase == SLOT_NATIVE_BET)
    {
        struct UiHintItem a[2] = {{UI_ACT_CONFIRM, sText_Spin}, {UI_ACT_SCROLL, sText_Bet}};
        struct UiHintItem c[2] = {{UI_ACT_CANCEL, sText_Quit}, {UI_ACT_SELECT, sText_Info}};

        UiW_Hints(490, 309, a, 2, false, 8);
        UiW_Hints(490, 329, c, 2, false, 8);
    }
    else if (sS.phase == SLOT_NATIVE_SPIN)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Stop}};

        UiW_Hints(490, 309, a, 1, false, 8);
    }
    else if (sS.phase == SLOT_NATIVE_INFO)
    {
        struct UiHintItem c[1] = {{UI_ACT_CANCEL, sText_Close}};

        UiW_Hints(490, 309, c, 1, false, 8);
    }
    else if (sS.phase == SLOT_NATIVE_MESSAGE)
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
    UiW_TitleTab(0, 8, "SLOT MACHINE", &red, 0, NULL);
    Cabinet(t);
    if (sS.phase == SLOT_NATIVE_INFO)
        PayoutTable();
    Display(t);
    Bottom(t);
    UiG_Veil(NativeUi_ScreenFade());
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_SlotMachine = {
    .name = "slot_machine",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif /* ENABLE_NATIVE_UI */
