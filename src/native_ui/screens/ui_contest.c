#ifdef ENABLE_NATIVE_UI
/*
 * A contest's appeals (not one of the 31 designed screens; the design's
 * pieces): the game's stage stays its picture - the audience, the judge, the
 * Pokémon and its move's effects, the applause meter, the judge's bubbles -
 * and the panels beside it are the design's plates, one a contestant in the
 * turn order: its Pokémon and trainer, this round's hearts as the game adds
 * them (red, or black for a loss), its condition's stars, its status, its
 * score so far on a slider, the next turn's tag and the judge's eye. The
 * announcer's lines are the design's message box where the game puts its
 * window, and the player's moves are the design's list (each move's type, its
 * appeal and jam hearts, a combo lit and a repeat greyed as the game marks
 * them) with the move's description. The engine keeps every rule: clicks and
 * the wheel queue its presses. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "contest.h"
#include "contest_effect.h"
#include "main.h"
#include "move.h"
#include "palette.h"
#include "string_util.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_MOVE = UI_HIT_SCREEN,
    HIT_TEXT,
};

/* the plates' column right of the stage (the game's panels at twice their size are 192 wide; the column is the
 * canvas's rest) and the announcer's box where the game puts its window */
#define COL_X 360
#define COL_W 272
#define PLATE_H 76
#define BOX_X 88
#define BOX_Y 252
#define BOX_W 264
#define BOX_H 84

static const u8 sText_OK[] = _("OK");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Moves[] = _("Moves");
static const u8 sText_NextTurn[] = _("NEXT TURN");

static const char *const sTypeLabels[] = {"COOL", "BEAUTY", "CUTE", "SMART", "TOUGH"};
static const UiRGBA sTypeColours[] = {UI_HEX(0xE84838), UI_HEX(0x3878D8), UI_HEX(0xE890B8), UI_HEX(0x48A848),
                                      UI_HEX(0xE8B020)};

static const char *const sHeart[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
static const char *const sStar[] = {"...#...", "..###..", "#######", ".#####.", "..#.#..", ".#...#."};
static const char *const sEye[] = {"..#####..", ".#.....#.", "#..###..#", "#..###..#", ".#.....#.", "..#####.."};

static struct NativeContest sC;
static u8 sMsg[UI_MESSAGE_COPY];
static u32 sMsgPrinted;
static bool sHasMsg, sMsgPrinting, sMsgWaiting;

/* ---------------------------------------------------------------- state */

static bool Detect(void)
{
    struct NativeContest c;

    return Contest_NativeUiState(&c);
}

static void Capture(void)
{
    const struct NativeUiPrinted *p;
    bool8 active = FALSE;

    Contest_NativeUiState(&sC);
    /* the window as the game has it: cleared (nothing printed since), else its last line */
    sHasMsg = TextPrinter_NativeUiTranscript(sC.textWindow, &p) > 0
           && TextPrinter_NativeUiWindowText(sC.textWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(sC.textWindow);
}

/* ---------------------------------------------------------------- drawing */

static void Hearts(int x, int y, int count, int scale, const char *name, UiRGBA colour)
{
    for (int i = 0; i < count; i++)
        UiW_PixScaled(name, sHeart, 6, (const struct UiPixPal[]){{'#', colour}}, 1, x + i * 8 * scale, y, scale);
}

/* an engine name in its room: the normal font, else the narrow one */
static void Name(int x, int y, const u8 *name, int room, UiRGBA fg, UiRGBA sh, enum UiAlign align)
{
    int font = UiText_Width(UI_FONT_NORMAL, name, 0) <= room ? UI_FONT_NORMAL : UI_FONT_NARROW;

    UiText_Draw(font, x, y, name, fg, sh, align, 0);
}

/* the status the game marks with a symbol, as a word on a pill: returns the pill's width (0: none) */
static int StatusPill(int x, int y, u8 status)
{
    static const char *const sWords[] = {
        [NATIVE_CONTEST_STATUS_RESISTANT] = "GUARDED",
        [NATIVE_CONTEST_STATUS_NERVOUS] = "NERVOUS",
        [NATIVE_CONTEST_STATUS_SKIPPED] = "NO TURN",
        [NATIVE_CONTEST_STATUS_UNNERVED] = "UNNERVED",
    };
    static const UiRGBA sColours[] = {
        [NATIVE_CONTEST_STATUS_RESISTANT] = UI_HEX(0x3878D8),
        [NATIVE_CONTEST_STATUS_NERVOUS] = UI_HEX(0xE07828),
        [NATIVE_CONTEST_STATUS_SKIPPED] = UI_HEX(0x707070),
        [NATIVE_CONTEST_STATUS_UNNERVED] = UI_HEX(0x9050C0),
    };
    int w;

    if (status == NATIVE_CONTEST_STATUS_NONE || status >= ARRAY_COUNT(sWords))
        return 0;
    w = UiCaps_Width(sWords[status]) + 10;
    UiG_Paint(UiM_Rounded4(w, 12, 2, 2, 2, 2), x, y, sColours[status],
              &(struct UiPaint){UiMix(sColours[status], UI_P_INK, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 5, y + 3, sWords[status], UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    return w;
}

/* a contestant's plate: the game's panel colours (the player's green, the others' gold) in the design's plate */
static void Plate(int x, int y, const struct NativeContestant *c, bool player, int t)
{
    struct UiRamp r = UiRampDefault(player ? UI_HEX(0x88D070) : UI_HEX(0xF0D058));
    int w = COL_W, hx = x + 30, ly = y + PLATE_H - 16, px = x + 156;

    UiG_Paint(UiM_Rounded4(w, PLATE_H, 4, 4, 4, 4), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 8, 36, 3, 3, 3, 3), x + 4, y + 20, UI_P_CREAM,
              &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    /* the Pokémon and its trainer on the plate's band, in the band's ink (the game's dark letters on its panels) */
    Name(x + 8, y + 3, c->nickname, 120, r.ink, UI_NONE, UI_ALIGN_LEFT);
    {
        u8 trainer[24];

        trainer[0] = CHAR_SLASH;
        StringCopyN(trainer + 1, c->trainerName, sizeof(trainer) - 2);
        trainer[sizeof(trainer) - 1] = EOS;
        Name(x + w - 8, y + 3, trainer, 120, r.ink, UI_NONE, UI_ALIGN_RIGHT);
    }
    /* the condition's stars down the left */
    for (int i = 0; i < c->stars; i++)
        UiW_Pix("ct_star", sStar, 6, (const struct UiPixPal[]){{'#', UI_HEX(0xE8B020)}}, 1, x + 12, y + 24 + i * 10, false);
    /* this round's hearts, eight a line */
    if (c->hearts > 0)
    {
        Hearts(hx, y + 25, c->hearts > 8 ? 8 : c->hearts, 2, "ct_heart2", UI_HEX(0xE84838));
        if (c->hearts > 8)
            Hearts(hx, y + 40, c->hearts - 8, 2, "ct_heart2", UI_HEX(0xE84838));
    }
    else if (c->hearts < 0)
    {
        Hearts(hx, y + 25, -c->hearts > 8 ? 8 : -c->hearts, 2, "ct_heart2k", UI_P_INK);
        if (-c->hearts > 8)
            Hearts(hx, y + 40, -c->hearts - 8, 2, "ct_heart2k", UI_P_INK);
    }
    /* the band's foot: the score so far (the slider's heart along its track, the game's 56 steps), the judge's eye,
     * the status, the next turn */
    UiG_Paint(UiM_Rounded4(124, 6, 2, 2, 2, 2), x + 10, ly + 4, UiMix(r.lo, r.ink, 0.25), &(struct UiPaint){UI_NONE});
    if (c->sliderShown)
        UiW_Pix("ct_heart1w", sHeart, 6, (const struct UiPixPal[]){{'#', UI_HEX(0xE84838)}}, 1, x + 10 + c->slider * 117 / 56,
                ly + 4, false);
    if (c->attention)
        UiW_Pix("ct_eye", sEye, 6, (const struct UiPixPal[]){{'#', (t / 16) % 2 ? r.ink : UiMix(r.ink, r.base, 0.4)}}, 1,
                x + 140, ly + 4, false);
    StatusPill(px, ly + 1, c->status);
    if (c->nextTurn != 0)
    {
        u8 tag[16];
        int tw;

        StringCopy(tag, sText_NextTurn);
        tw = UiText_Width(UI_FONT_SMALL, tag, 0);
        UiG_Paint(UiM_Rounded4(tw + 26, 12, 2, 2, 2, 2), x + w - tw - 34, ly + 1, UI_P_CURSOR,
                  &(struct UiPaint){UiMix(UI_P_CURSOR, UI_P_INK, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_SMALL, x + w - tw - 28, ly, tag, UI_P_TEXT_W, UI_NONE, UI_ALIGN_LEFT, 0);
        tag[0] = c->nextTurn > 4 ? CHAR_QUESTION_MARK : CHAR_0 + c->nextTurn;
        tag[1] = EOS;
        UiText_Draw(UI_FONT_SMALL, x + w - 16, ly, tag, UI_P_TEXT_W, UI_NONE, UI_ALIGN_LEFT, 0);
    }
}

/* the player's moves where the panels are: each move's type, name (a combo lit, a repeat greyed), appeal and jam */
static void MoveList(int t)
{
    int rowH = 40, h = MAX_MON_MOVES * rowH + 16, x = COL_X, y = 20;

    UiW_DialogBox(x, y, COL_W, h, NULL, UI_NONE, NULL);
    for (int i = 0; i < sC.moveCount && i < MAX_MON_MOVES; i++)
    {
        u16 move = sC.moves[i];
        int ry = y + 8 + i * rowH, c = GetMoveContestCategory(move), effect = GetMoveContestEffect(move);
        UiRGBA fg = sC.moveStyle[i] == NATIVE_CONTEST_MOVE_REPEATED ? UI_P_TEXT_MUTED : UI_P_TEXT;

        if (c < 0 || c >= (int)ARRAY_COUNT(sTypeLabels))
            c = 0;
        if (sC.moveStyle[i] == NATIVE_CONTEST_MOVE_COMBO)
            UiG_Paint(UiM_Rounded4(COL_W - 20, rowH - 4, 3, 3, 3, 3), x + 10, ry, UiMix(UI_HEX(0xF8E080), UI_P_PAPER, 0.4),
                      &(struct UiPaint){UI_NONE});
        UiMon_Tag(x + 26, ry + 4, sTypeLabels[c], sTypeColours[c]);
        Name(x + 82, ry + 1, GetMoveName(move), COL_W - 96, fg, UI_P_TEXT_SH, UI_ALIGN_LEFT);
        UiCaps_Draw(x + 26, ry + 24, "APPEAL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        Hearts(x + 62, ry + 23, gContestEffects[effect].appeal / 10, 1, "ct_heart1", UI_HEX(0xE84838));
        UiCaps_Draw(x + 150, ry + 24, "JAM", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        Hearts(x + 172, ry + 23, gContestEffects[effect].jam / 10, 1, "ct_heart1k", UI_P_INK);
        if (i == sC.moveCursor)
            UiW_Cursor(x + 12, ry + 4, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_MOVE, i, x + 4, ry, COL_W - 8, rowH);
    }
}

static void Draw(int t)
{
    struct UiHintItem h[2];
    int n = 0, b[4];
    const u8 *text = sHasMsg ? sMsg : NULL;
    int printed = sMsgPrinting ? (int)sMsgPrinted : -1;

    for (int row = 0; row < NATIVE_CONTESTANTS; row++)
    {
        u8 i = sC.order[row];

        Plate(COL_X, 20 + row * (PLATE_H + 4), &sC.mons[i], i == sC.player, t);
    }
    if (sC.choosing)
    {
        /* the move on the cursor: what it does, where the announcer speaks */
        u16 move = sC.moveCursor < MAX_MON_MOVES ? sC.moves[sC.moveCursor] : MOVE_NONE;

        text = move != MOVE_NONE ? gContestEffects[GetMoveContestEffect(move)].description : NULL;
        printed = -1;
        MoveList(t);
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
    }
    else
    {
        if (sC.askMove)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Moves};
        else if (sMsgWaiting)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    UiW_DialogBox(BOX_X, BOX_Y, BOX_W, BOX_H, NULL, UI_NONE, b);
    if (text != NULL)
        UiW_MessagePageFlipDraw(text, printed, printed >= 0 && !sMsgWaiting, b[0] + 4, b[1] + 2, b[2] - 8, 2, 18);
    if (sMsgWaiting && !sC.choosing)
        UiW_DownArrow(b[0] + b[2] - 12, b[1] + b[3] - 12 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_TEXT, 0, BOX_X, BOX_Y, BOX_W, BOX_H);
    if (n > 0)
        UiW_Hints(BOX_X + BOX_W - 10 - UiW_HintsWidth(h, n, 10), BOX_Y + BOX_H - 24, h, n, false, 10);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sC.choosing || sC.askMove || sMsgWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sC.choosing || sC.askMove)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sC.choosing)
    {
        /* a move: the cursor there, then A */
        if (hit->id == HIT_MOVE && hit->index >= 0 && hit->index < sC.moveCount)
        {
            int moves = hit->index - sC.moveCursor;

            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    }
    /* the question before the list, a line waiting: A */
    if (sC.askMove || sMsgWaiting)
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !sC.choosing)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the stage stays (BG3, the moves' effects on BG1, the sprites); the text (BG0) and the panels (BG2) give way */
    return UI_PICTURE_HIDE_BG02;
}

/* the panels' own sprites (the score hearts, the next turn tags, the boxes' blinks) go with the panels */
static void HideObjects(uint32_t mask[4])
{
    const struct OamData *oam = (const struct OamData *)OAM;

    for (int k = 0; k < (int)ARRAY_COUNT(sC.hideTiles); k++)
    {
        u16 start = sC.hideTiles[k][0], count = sC.hideTiles[k][1];

        if (start == 0xFFFF)
            continue;
        for (int i = 0; i < 128; i++)
            if (oam[i].tileNum >= start && oam[i].tileNum < start + count)
                mask[i >> 5] |= 1u << (i & 31);
    }
}

const struct UiScreen gUiScreen_Contest = {
    .name = "contest",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
    .hideObjects = HideObjects,
};
#endif // ENABLE_NATIVE_UI
