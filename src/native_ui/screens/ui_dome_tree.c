#ifdef ENABLE_NATIVE_UI
/*
 * The Battle Dome's tourney tree (battle_dome.c: the tree shown before each
 * round, the lobby's record of the last tourney, a round's results between
 * battles; not one of the 31 designed screens: the design's pieces): the
 * sixteen as the game seeds them, eight a side, the bracket's lines lit along
 * each one's wins as the game shows them, those out greyed, the player in
 * green, the champion crowned; the cursor on a trainer, a match or the close
 * button where the game has it, and a card for it (a trainer's three, a
 * match's two and how it went). A on a trainer or a match opens the game's
 * own info card; clicks walk the game's own cursor there first. Project-owned.
 */
#include <string.h>

#include "global.h"
#include "data.h"
#include "constants/battle_dome.h"
#include "constants/characters.h"
#include "constants/trainers.h"
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
    HIT_STOP = UI_HIT_SCREEN,   /* index: the game's cursor stop (0-15 a trainer, 16-30 a match, 31 close) */
    HIT_BOARD,
};

#define ROW_Y 62
#define ROW_STEP 32
#define NAME_W 116
#define NAME_H 20
#define LEFT_X 14

static const u8 sText_Move[] = _("Move");
static const u8 sText_Info[] = _("Info");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Next[] = _("Next");
static const u8 sText_NotYet[] = _("Not decided yet");
static const char *const sRounds[] = {"ROUND 1", "ROUND 2", "SEMIFINAL", "FINAL"};

static struct NativeDomeTree sD;

/* ---------------------------------------------------------------- state */

static bool Detect(void)
{
    struct NativeDomeTree d;

    return DomeTree_NativeUiState(&d);
}

static void Capture(void)
{
    DomeTree_NativeUiState(&sD);
}

/* ---------------------------------------------------------------- geometry */

/* the bracket's columns from the names in (left side; the right is mirrored): round 1's, round 2's, the semifinal's */
static const int sColX[] = {LEFT_X + NAME_W, 158, 190, 222};

static int RowY(int row)
{
    return ROW_Y + row * ROW_STEP + NAME_H / 2;
}

/* a point of entrant p's way at level l (0 its name, 1-3 the matches it reaches, 4 the final) */
static void WayPoint(int p, int l, int *x, int *y)
{
    int side = p / 8, row = p % 8, span = 1 << l;

    if (l >= 4)
    {
        *x = 320;
        *y = RowY(0) + (RowY(7) - RowY(0)) / 2;
        return;
    }
    *y = (RowY(row / span * span) + RowY(row / span * span + span - 1)) / 2;
    *x = side == 0 ? sColX[l] : 640 - sColX[l];
}

/* match m's point: its round and where it sits */
static int MatchRound(int m)
{
    return m < 8 ? 0 : m < 12 ? 1 : m < 14 ? 2 : 3;
}

static int MatchFirst(int m)
{
    static const int sFirst[] = {0, 2, 4, 6, 8, 10, 12, 14, 0, 4, 8, 12, 0, 8, 0};

    return sFirst[m];
}

static void MatchPoint(int m, int *x, int *y)
{
    WayPoint(MatchFirst(m), MatchRound(m) + 1, x, y);
}

static void NamePos(int p, int *x, int *y)
{
    *x = p < 8 ? LEFT_X : 640 - LEFT_X - NAME_W;
    *y = ROW_Y + (p % 8) * ROW_STEP;
}

/* ---------------------------------------------------------------- drawing */

static void Segment(int x0, int y0, int x1, int y1, UiRGBA c, int w)
{
    if (y0 == y1)
        UiG_Rect(x0 < x1 ? x0 : x1, y0 - w / 2, (x0 < x1 ? x1 - x0 : x0 - x1) + w / 2, w, c);
    else
        UiG_Rect(x0 - w / 2, y0 < y1 ? y0 : y1, w, (y0 < y1 ? y1 - y0 : y0 - y1) + w / 2, c);
}

/* entrant p's way from level l to l + 1: across, then up or down to the match */
static void Way(int p, int l, UiRGBA c, int w)
{
    int x0, y0, x1, y1;

    WayPoint(p, l, &x0, &y0);
    WayPoint(p, l + 1, &x1, &y1);
    Segment(x0, y0, x1, y0, c, w);
    Segment(x1, y0, x1, y1, c, w);
}

static UiRGBA WayColour(int p)
{
    return sD.entrants[p].trainerId == TRAINER_PLAYER ? UI_HEX(0x58C060) : UI_HEX(0xF0C040);
}

static void Bracket(int t)
{
    /* every way grey, then each one's wins lit (the round just decided pulsing) */
    for (int p = 0; p < NATIVE_DOME_ENTRANTS; p++)
        for (int l = 0; l < 4; l++)
            Way(p, l, UI_HEX(0x4A5468), 2);
    for (int p = 0; p < NATIVE_DOME_ENTRANTS; p++)
        for (int l = 0; l < sD.entrants[p].wins && l < 4; l++)
        {
            UiRGBA c = WayColour(p);

            if (l == sD.justDecided && (t / 10) % 2 == 0)
                c = UiMix(c, UI_HEX(0xFFFFFF), 0.5);
            Way(p, l, c, 4);
        }
    /* the matches' points */
    for (int m = 0; m < 15; m++)
    {
        int x, y, stop = 16 + m;
        bool sel = sD.phase == NATIVE_DOME_BROWSE && sD.cursor == stop;

        MatchPoint(m, &x, &y);
        if (sel)
            UiG_Mask(UiM_Ellipse(20, 20), x - 10, y - 10, UI_P_SEL);
        UiG_Mask(UiM_Ellipse(12, 12), x - 6, y - 6, UI_HEX(0x202838));
        UiG_Mask(UiM_Ellipse(8, 8), x - 4, y - 4, m == 14 ? UI_HEX(0xF0C040) : UI_HEX(0xC8D0E0));
        UiG_Hit(HIT_STOP, stop, x - 10, y - 10, 20, 20);
    }
}

static void Name(int p, int t)
{
    const struct NativeDomeEntrant *e = &sD.entrants[p];
    bool player = e->trainerId == TRAINER_PLAYER, champ = e->wins >= 4;
    bool sel = sD.phase == NATIVE_DOME_BROWSE && sD.cursor == p;
    struct UiRamp r = UiRampDefault(player ? UI_HEX(0x88D070) : champ ? UI_HEX(0xF0C858) : UI_HEX(0xD8DEE8));
    int x, y;

    NamePos(p, &x, &y);
    if (e->out)
        r = UiRampDefault(UI_HEX(0x707888));
    if (sel)
        r = UiRampDefault(UiMix(r.base, UI_HEX(0xFFFFFF), 0.4));
    UiG_Paint(UiM_Rounded4(NAME_W, NAME_H, 4, 4, 4, 4), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, p < 8 ? x + 8 : x + NAME_W - 8, y + 2, e->name, e->out ? UI_HEX(0x3A3F48) : r.ink, UI_NONE,
                p < 8 ? UI_ALIGN_LEFT : UI_ALIGN_RIGHT, 0);
    if (sel)
        UiW_Cursor(p < 8 ? x - 10 : x + NAME_W + 2, y + 5, UiW_Nudge(t), UI_NONE);
    UiG_Hit(HIT_STOP, p, x, y, NAME_W, NAME_H);
}

/* the card for the cursor's stop: a trainer's three, or a match's two and its winner */
static void Card(void)
{
    int x = 236, y = 252, w = 168, h = 76;
    int stop = sD.cursor;

    UiG_Paint(UiM_Rounded4(w, h, 5, 5, 5, 5), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x202838), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    if (stop < NATIVE_DOME_ENTRANTS)
    {
        const struct NativeDomeEntrant *e = &sD.entrants[stop];

        UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 6, e->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        for (int k = 0; k < 3; k++)
            UiGfx_MonIcon(e->species[k], 0, false, 0, x + 18 + k * 46, y + 30, NULL);
    }
    else if (stop < NATIVE_DOME_CLOSE)
    {
        int m = stop - 16, round = MatchRound(m), first = MatchFirst(m), span = 2 << round, a = -1, b = -1, winner = -1;

        UiCaps_Draw(x + w / 2, y + 8, sRounds[round], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        /* who reached it from each half, and who won it */
        for (int p = first; p < first + span; p++)
        {
            if (sD.entrants[p].wins < round)
                continue;
            if (p < first + span / 2)
                a = p;
            else
                b = p;
            if (sD.entrants[p].wins > round)
                winner = p;
        }
        for (int k = 0; k < 2; k++)
        {
            int p = k == 0 ? a : b, yy = y + 22 + k * 18;

            if (p < 0)
            {
                UiCaps_Draw(x + w / 2, yy + 5, "---", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
                continue;
            }
            UiText_Draw(UI_FONT_NORMAL, x + w / 2, yy, sD.entrants[p].name, p == winner ? UI_P_TEXT : UI_P_TEXT_MUTED,
                        UI_NONE, UI_ALIGN_CENTER, 0);
            if (p == winner)
                UiCaps_Draw(x + w - 10, yy + 5, "WIN", UI_HEX(0xC83828), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        }
        if (winner < 0)
            UiText_Draw(UI_FONT_SMALL, x + w / 2, y + h - 14, sText_NotYet, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_CENTER, 0);
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh, fx, fy;
    const char *where;
    struct UiHintItem h[3];
    int n = 0;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x1C2232));
    UiG_Rect(bx, by, bw, 44, UI_HEX(0x2A3248));
    UiItems_BigCaps(320, 10, "BATTLE TOURNEY", 2, UI_HEX(0xF0C040), UI_HEX(0x101420), UI_ALIGN_CENTER);
    where = sD.previous ? "THE LAST TOURNEY" : sD.lvlMode == FRONTIER_LVL_50 ? "BATTLE DOME  -  LV. 50" : "BATTLE DOME  -  OPEN LEVEL";
    UiCaps_Draw(16, 32, where, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sD.shown < 4)
        UiCaps_Draw(320, 32, sRounds[sD.justDecided != 0xFF ? sD.justDecided : sD.shown], UI_HEX(0xC8D0E0), UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiG_Hit(HIT_BOARD, 0, bx, by, bw, bh);
    Bracket(t);
    for (int p = 0; p < NATIVE_DOME_ENTRANTS; p++)
        Name(p, t);
    /* the champion's crown over the final */
    WayPoint(0, 4, &fx, &fy);
    for (int p = 0; p < NATIVE_DOME_ENTRANTS; p++)
        if (sD.entrants[p].wins >= 4)
        {
            UiItems_BigCaps(fx, fy - 40, "CHAMPION", 2, UI_HEX(0xF0C040), UI_HEX(0x101420), UI_ALIGN_CENTER);
            UiText_Draw(UI_FONT_NORMAL, fx, fy - 24, sD.entrants[p].name, UI_HEX(0xF8E8B0), UI_NONE, UI_ALIGN_CENTER, 0);
        }
    if (sD.phase == NATIVE_DOME_BROWSE)
    {
        /* the game's close button (CANCEL, or EXIT on the last tourney's record) */
        bool sel = sD.cursor == NATIVE_DOME_CLOSE;
        struct UiRamp r = UiRampDefault(sel ? UI_HEX(0xE87868) : UI_HEX(0xB85848));

        UiG_Paint(UiM_Rounded4(84, 22, 5, 5, 5, 5), 540, 8, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, UI_NONE});
        UiCaps_Draw(582, 16, sD.previous ? "EXIT" : "CANCEL", UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
        if (sel)
            UiW_Cursor(528, 13, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_STOP, NATIVE_DOME_CLOSE, 540, 8, 84, 22);
        Card();
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Move};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sD.cursor == NATIVE_DOME_CLOSE ? sText_Close : sText_Info};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Close};
    }
    else if (sD.phase == NATIVE_DOME_RESULTS && sD.ready)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    if (n > 0)
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, n, 10), by + bh - 22, h, n, true, 10);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sD.ready;
}

/* the game's own cursor moves to a stop: the shortest way through its moves */
static bool QueueTo(int to)
{
    static const u16 sKeys[] = {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT};
    int prev[32], how[32], queue[32], head = 0, tail = 0, path[32], n = 0;

    for (int i = 0; i < 32; i++)
        prev[i] = -2;
    prev[sD.cursor] = -1;
    queue[tail++] = sD.cursor;
    while (head < tail)
    {
        int c = queue[head++];

        for (int d = 0; d < 4; d++)
        {
            int nx = DomeTree_NativeUiNext(c, d);

            if (nx == 0xFF || nx >= 32 || prev[nx] != -2)
                continue;
            prev[nx] = c;
            how[nx] = d;
            queue[tail++] = nx;
        }
    }
    if (to < 0 || to >= 32 || prev[to] == -2)
        return false;
    for (int c = to; c != sD.cursor; c = prev[c])
        path[n++] = how[c];
    while (n > 0)
        NativeUi_QueuePress(sKeys[path[--n]]);
    return true;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sD.ready || NativeUi_PressesPending())
        return true;
    if (sD.phase == NATIVE_DOME_RESULTS)
    {
        if (button == 1 || button == 3)
            NativeUi_QueuePress(button == 1 ? A_BUTTON : B_BUTTON);
        return true;
    }
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || hit->id != HIT_STOP)
        return false;
    if (QueueTo(hit->index))
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_DomeTree = {
    .name = "dome_tree",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
