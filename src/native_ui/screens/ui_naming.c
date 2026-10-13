#ifdef ENABLE_NATIVE_UI
/*
 * 29 · Naming (the approved design's preview/js/screens/naming.js): the name
 * tag with underscore slots and a block cursor, the icon of what is being named
 * and the keyboard's three pages (UPPER, lower, OTHERS) over the engine's naming
 * screen (naming_screen.c), which keeps every rule: its keyboards and their
 * order, the cursor and how it moves, the PAGE / BACK / OK keys, the limits
 * (a name 7, a nickname 12, a Box 8), the lower page after a first capital, and
 * what the name becomes. The keys are the engine's own grid (eight keys a row,
 * six on OTHERS, the blank keys type a space) with its PAGE, BACK and OK column
 * at the right, in the design's keycaps.
 *
 * Type on the PC keyboard: each character becomes the engine's own presses (the
 * page, the cursor's steps, A), then the cursor waits on OK, so Enter finishes;
 * Backspace deletes, Page Up / Down change the page. The controller and the
 * arrows keep the engine's grid; clicks on keys, tabs and PAGE / BACK / OK
 * press for you. The design's confirmation ("So it's ...?") belongs to whoever
 * opened the screen (Birch, the PC, the Name Rater), not to this one.
 * Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "naming_screen.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/pokemon.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_KEY = UI_HIT_SCREEN,   /* index: row * 8 + column */
    HIT_PAGE_KEY,
    HIT_BACK_KEY,
    HIT_OK_KEY,
    HIT_TAB,
};

#define BX 96
#define BY 118
#define BW 448
#define BH 178
#define KH 30
#define GAP 6
#define AW 76              /* the PAGE / BACK / OK column */
#define PITCH 16           /* the name tag's slots */
#define MAX_TYPED 32
#define MSG_FRAMES 150

static const char *const sPageLabels[NATIVE_NAMING_PAGES] = {"UPPER", "lower", "OTHERS"};
static const u8 sPageTypes[NATIVE_NAMING_PAGES] = {UI_TYPE_WATER, UI_TYPE_GRASS, UI_TYPE_PSYCHIC};
static const char *const sBoxIcon[13] = {
    "..####################..", ".#hhhhhhhhhhhhhhhhhhhh#.", "#hhbbbbbbbbbbbbbbbbbbbs#", "#bbbbbbbbbbbbbbbbbbbbss#",
    "########################", ".#bbbbbbbbbbbbbbbbbbbs#.", ".#bbbbb##########bbbbs#.", ".#bbbbb#wwwwwwww#bbbbs#.",
    ".#bbbbb#wllllllw#bbbbs#.", ".#bbbbb##########bbbbs#.", ".#bbbbbbbbbbbbbbbbbbbs#.", ".#ssssssssssssssssssss#.",
    "..####################..",
};

static const u8 sText_Help[] = _("Type on your keyboard, or pick letters with the cursor.");
static const u8 sText_HelpPad[] = _("Pick letters with the cursor. START jumps to OK.");
static const u8 sText_Full[] = _("That's as many characters as this name can have.");
static const u8 sText_NotHere[] = _("That character isn't on this keyboard.");
static const u8 sText_UpTo[] = _("Up to ");
static const u8 sText_Chars[] = _(" characters");
static const u8 sText_Enter[] = _("Enter");
static const u8 sText_Delete[] = _("Delete");
static const u8 sText_Page[] = _("Page");
static const u8 sText_ToOk[] = _("To OK");
static const u8 sText_Saving[] = _("Saving the name…");

/* ---------------------------------------------------------------- captured state */

static struct NativeNaming sN;
static u8 sTitle[64];
static bool sHasTitle;
static struct NativeFieldFrame sWalk[4];   /* the player walking down */
static bool sHasWalk;
static int sLength;
static uint32_t sLastCapture;
/* the key the engine just took (the design's 2-frame press) */
static int sPressR = -1, sPressC = -1;
static uint32_t sPressUntil;
/* typed characters (engine bytes) on their way in as the engine's presses */
static u8 sTyped[MAX_TYPED];
static int sTypedCount;
static bool sPark;               /* START once they are in: the cursor waits on OK */
/* a clicked key: walk the engine's cursor there, then A */
static int sGoalX = -1, sGoalY = -1;
static int sGoalSteps;
static const u8 *sMsg;
static uint32_t sMsgUntil;

static bool Detect(void)
{
    struct NativeNaming n;

    return NamingScreen_NativeUiState(&n);
}

static int TextLength(const u8 *s, int max)
{
    int n = 0;

    while (n < max && s[n] != EOS)
        n++;
    return n;
}

/* the key holding a character on a page (the first such key; -1: none) */
static bool FindKey(int page, u8 ch, int *x, int *y)
{
    for (int r = 0; r < NATIVE_NAMING_ROWS; r++)
        for (int c = 0; c < sN.columns[page]; c++)
            if (sN.keys[page][r][c] == ch)
            {
                *x = c;
                *y = r;
                return true;
            }
    return false;
}

static bool OnAnyPage(u8 ch)
{
    int x, y;

    for (int p = 0; p < NATIVE_NAMING_PAGES; p++)
        if (FindKey(p, ch, &x, &y))
            return true;
    return false;
}

static void Note(const u8 *msg)
{
    sMsg = msg;
    sMsgUntil = NativeUi_Frame() + MSG_FRAMES;
}

/* one press of the engine's d-pad towards a key (never through the PAGE /
 * BACK / OK column, where the rows change); true: the cursor is there */
static bool Walk(int x, int y)
{
    int cols = sN.columns[sN.page], down;

    if (sN.cursorX == x && sN.cursorY == y)
        return true;
    if (sN.cursorX >= cols)
    {
        NativeUi_QueuePress(x >= cols / 2 ? DPAD_LEFT : DPAD_RIGHT);
        return false;
    }
    if (sN.cursorY != y)
    {
        down = (y - sN.cursorY + NATIVE_NAMING_ROWS) % NATIVE_NAMING_ROWS;
        NativeUi_QueuePress(down <= NATIVE_NAMING_ROWS / 2 ? DPAD_DOWN : DPAD_UP);
        return false;
    }
    NativeUi_QueuePress(x > sN.cursorX ? DPAD_RIGHT : DPAD_LEFT);
    return false;
}

/* typing and clicked keys: one engine press at a time, while it reads buttons */
static void Step(void)
{
    int x, y;

    if (!sN.ready || NativeUi_PressesPending())
        return;
    if (sGoalX >= 0)
    {
        if (++sGoalSteps > 40 || sGoalX >= sN.columns[sN.page])
            sGoalX = -1;
        else if (Walk(sGoalX, sGoalY))
        {
            NativeUi_QueuePress(A_BUTTON);
            sGoalX = -1;
        }
        return;
    }
    if (sTypedCount > 0)
    {
        if (sLength >= sN.maxChars)
        {
            sTypedCount = 0;
            Note(sText_Full);
            return;
        }
        if (!FindKey(sN.page, sTyped[0], &x, &y))
        {
            NativeUi_QueuePress(SELECT_BUTTON); /* the page that has it comes round */
            return;
        }
        if (Walk(x, y))
        {
            NativeUi_QueuePress(A_BUTTON);
            memmove(sTyped, sTyped + 1, (size_t)(sTypedCount - 1));
            sTypedCount--;
            if (sTypedCount == 0)
                sPark = true;
        }
        return;
    }
    if (sPark)
    {
        sPark = false;
        if (sLength < sN.maxChars) /* full: the engine has gone to OK itself */
            NativeUi_QueuePress(START_BUTTON);
    }
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    bool fresh = now - sLastCapture != 1;
    int was = sLength;
    bool8 active;
    u32 printed;

    sLastCapture = now;
    if (!NamingScreen_NativeUiState(&sN))
        return;
    if (fresh)
    {
        sTypedCount = 0;
        sPark = false;
        sGoalX = -1;
        sMsg = NULL;
        was = TextLength(sN.text, sN.maxChars);
    }
    sLength = TextLength(sN.text, sN.maxChars);
    if (sLength > was && sN.cursorX >= 0 && sN.cursorX < sN.columns[sN.page])
    {
        sPressR = sN.cursorY;
        sPressC = sN.cursorX;
        sPressUntil = now + 2;
    }
    sHasTitle = TextPrinter_NativeUiWindowText(sN.titleWindow, sTitle, sizeof(sTitle), &printed, &active);
    sHasWalk = sN.kind == NAMING_SCREEN_PLAYER;
    if (sHasWalk)
        for (int k = 0; k < 4; k++)
            MultiplayerAppearance_NativeUiPlayerFrame(0, NATIVE_GAIT_WALK, k, &sWalk[k]);
    if (sN.phase == NATIVE_NAMING_DONE)
    {
        sTypedCount = 0;
        sGoalX = -1;
    }
    Step();
}

/* ---------------------------------------------------------------- the name tag */

static struct UiRamp PageRamp(int page)
{
    return gUiTypeRamps[sPageTypes[page]];
}

static void Icon(int cx, int cy, int t)
{
    if (sN.kind == NAMING_SCREEN_PLAYER)
    {
        const struct NativeFieldFrame *f = &sWalk[(t / 10) % 4];

        if (sHasWalk && f->tiles != NULL)
            UiGfx_FieldSpriteSized(f->tiles, 1, f->palette, 0, f->width, f->height, cx - f->width / 2, cy + 12 - f->height, 1,
                                   f->flip);
    }
    else if (sN.kind == NAMING_SCREEN_CAUGHT_MON || sN.kind == NAMING_SCREEN_NICKNAME)
    {
        int fr = (t / 10) % 2;

        UiGfx_MonIcon(sN.species, sN.personality, false, fr, cx - 16, cy - 20 - fr, NULL);
    }
    else if (sN.kind == NAMING_SCREEN_BOX)
    {
        struct UiRamp br = UiRampOf(UI_P_BAG_BG, 0.4, 0.28, 0.66);
        struct UiPixPal pal[6] = {
            {'#', br.ink}, {'h', br.hi}, {'b', br.base}, {'s', br.lo}, {'w', UI_P_PAPER}, {'l', UI_P_PAPER_LO},
        };

        UiW_Pix("namebox", sBoxIcon, 13, pal, 6, cx - 12, cy - 7, false);
    }
}

static void Tag(int t)
{
    UiRGBA ink = UiMix(UI_P_BAG_INK, UI_P_INK, 0.4);
    int w = 92 + sN.maxChars * PITCH + 56, h = 92, x, y = 10, sx, sy, cx, cy;
    struct UiPattern lines = UiPat_HStripe(16, 1, 4), below = UiPat_Below(30);
    char caps[16];
    u8 note[48];

    if (w < 300)
        w = 300;
    x = 320 - (w >> 1);
    lines.and_ = &below;
    UiG_Paint(UiM_Rounded(w, h, UiCornerChamfer(14), UiCornerR(3), UiCornerChamfer(14), UiCornerR(3)), x, y, UI_P_CREAM,
              &(struct UiPaint){ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, &lines, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.5)});
    /* the eyelet ring, in the page's colour */
    UiG_Paint(UiM_Circle(12), x + 7, y + 40, UiMix(UI_P_POCKETS_KEY, UI_P_CREAM, 0.2), &(struct UiPaint){ink, UI_P_CREAM_HI, UI_NONE, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Circle(6), x + 10, y + 43, UiMix(PageRamp(sN.page).base, UI_P_INK, 0.55), &(struct UiPaint){UI_NONE, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    /* the icon disc */
    cx = x + 28 + 24;
    cy = y + 22 + 24;
    UiG_Paint(UiM_Circle(48), x + 28, y + 22, UiMix(UI_P_CREAM_LO, UI_P_PAPER, 0.3), &(struct UiPaint){ink, UI_P_CREAM_LO, UI_P_CREAM_HI, 0, 0, NULL, UI_NONE});
    Icon(cx, cy, t);
    /* the title as the engine printed it, the count */
    if (sHasTitle)
        UiG_Text(x + 92, y + 8, sTitle);
    snprintf(caps, sizeof(caps), "%d/%d", sLength, sN.maxChars);
    UiCaps_Draw(x + w - 12, y + 13, caps, sLength >= sN.maxChars ? UI_P_CMD_FIGHT : UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                UI_ALIGN_RIGHT, UI_NONE);
    /* the slots: each character centred over its underscore, the block cursor at the next one */
    sx = x + 92;
    sy = y + 42;
    UiG_Paint(UiM_Rounded4(sN.maxChars * PITCH + 12, 28, 2, 2, 2, 2), sx - 6, sy - 5, UI_P_PAPER,
              &(struct UiPaint){UI_P_CREAM_LO, UI_P_PAPER_LO, UI_NONE, 0, 0, NULL, UI_NONE});
    for (int k = 0; k < sN.maxChars; k++)
    {
        if (k < sLength && sN.text[k] != CHAR_SPACE)
        {
            u8 one[2] = {sN.text[k], EOS};

            UiText_Draw(UI_FONT_NORMAL, sx + k * PITCH + ((PITCH - 2) >> 1), sy, one, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        }
        UiG_HLine(sx + k * PITCH, sy + 15, PITCH - 2, UI_HEX(0x909098));
    }
    if (sN.phase != NATIVE_NAMING_DONE && sLength < sN.maxChars && (t / 16) % 2 == 0)
        UiG_Rect(sx + sLength * PITCH, sy + 4, PITCH - 2, 11, UI_P_TEXT);
    if (sN.genderIcon && sN.gender != MON_GENDERLESS)
        UiW_Gender(sx + sN.maxChars * PITCH + 8, sy, sN.gender == MON_FEMALE ? 1 : 0);
    memcpy(note, sText_UpTo, sizeof(sText_UpTo));
    UiText_AppendNumber(note, sizeof(note), sN.maxChars, 1, false);
    UiText_Append(note, sizeof(note), sText_Chars);
    UiText_Draw(UI_FONT_SMALL, x + 92, y + 72, note, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

/* ---------------------------------------------------------------- the keyboard */

/* a keycap: light by default, coloured for the action keys; thick lip; the
 * selection frame outside */
static void Keycap(int x, int y, int w, int h, UiRGBA colour, bool on, bool pressed)
{
    struct UiRamp r = colour != UI_NONE ? UiRampOf(colour, 0.42, 0.25, 0.62)
                                        : (struct UiRamp){UI_R_KEY_HI, UI_R_KEY_BASE, UI_R_KEY_LO, UI_R_KEY_INK};
    const struct UiMask *m = UiM_Rounded4(w, h, 3, 3, 3, 3);

    if (pressed)
        y += 1;
    if (on)
        UiW_SelectFrame(m, x, y, UI_NONE, UI_NONE, 0);
    UiG_Paint(m, x, y, r.base, &(struct UiPaint){r.ink, pressed ? r.lo : r.hi, pressed ? r.hi : r.lo, 1, pressed ? 1 : 3, NULL, UI_NONE});
}

static void Board(int t)
{
    int page = sN.page, cols = sN.columns[page], kw, gx, gy, ax;
    struct UiRamp pr = PageRamp(page);
    UiRGBA light = UiMix(pr.hi, UI_HEX(0xFFFFFF), 0.35);
    bool editing = sN.phase == NATIVE_NAMING_INPUT && sN.cursorShown;
    struct UiPattern diag = UiPat_Diag(16, 4, 0, 1);

    (void)t;
    /* the page tabs */
    for (int i = 0; i < NATIVE_NAMING_PAGES; i++)
    {
        struct UiRamp r = PageRamp(i);
        bool on = i == page;
        int tw = 84, tx = BX + 12 + i * (tw + 4), th = on ? 20 : 16, ty = BY - th + 2;
        u8 label[12];

        UiG_Paint(UiM_Rounded4(tw, th, 3, 3, 0, 0), tx, ty, on ? light : UiMix(r.base, UI_P_INK, 0.25),
                  &(struct UiPaint){r.ink, on ? UI_HEX(0xFFFFFF) : r.base, UI_NONE, 0, 0, NULL, UI_NONE});
        UiText_FromAscii(label, sizeof(label), sPageLabels[i]);
        UiText_Draw(UI_FONT_NORMAL, tx + (tw >> 1), ty + (on ? 2 : 0), label, on ? r.ink : UI_P_TEXT_W,
                    on ? UiMix(light, r.lo, 0.3) : r.ink, UI_ALIGN_CENTER, 0);
        UiG_Hit(HIT_TAB, i, tx, BY - 20, tw, 20);
    }
    UiG_Paint(UiM_Rounded4(BW, BH, 3, 4, 4, 4), BX, BY, light,
              &(struct UiPaint){pr.ink, UI_HEX(0xFFFFFF), UiMix(light, pr.base, 0.4), 0, 2, &diag, UiMix(light, pr.hi, 0.35)});
    /* the engine's grid: its keys, then its PAGE / BACK / OK column */
    kw = (BW - 24 - AW - GAP - (cols - 1) * GAP) / cols;
    gx = BX + ((BW - (cols * kw + (cols - 1) * GAP + GAP + AW)) >> 1);
    gy = BY + ((BH - (NATIVE_NAMING_ROWS * KH + (NATIVE_NAMING_ROWS - 1) * GAP)) >> 1);
    for (int r = 0; r < NATIVE_NAMING_ROWS; r++)
        for (int c = 0; c < cols; c++)
        {
            u8 ch = sN.keys[page][r][c];
            int kx = gx + c * (kw + GAP), ky = gy + r * (KH + GAP);
            bool on = editing && sN.cursorX == c && sN.cursorY == r;
            bool pressed = sPressR == r && sPressC == c && NativeUi_Frame() < sPressUntil;

            Keycap(kx, ky, kw, KH, UI_NONE, on, pressed);
            if (ch == CHAR_SPACE)
            {
                UiCaps_Draw(kx + (kw >> 1), ky + 10 + (pressed ? 1 : 0), "SPACE", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                            UI_ALIGN_CENTER, UI_NONE);
                UiG_HLine(kx + (kw >> 1) - 12, ky + 20 + (pressed ? 1 : 0), 24, UI_R_KEY_LO);
            }
            else
            {
                u8 one[2] = {ch, EOS};

                UiText_Draw(UI_FONT_NORMAL, kx + (kw >> 1), ky + 6 + (pressed ? 1 : 0), one, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
            }
            UiG_Hit(HIT_KEY, r * NATIVE_NAMING_COLS + c, kx, ky, kw, KH);
        }
    ax = gx + cols * kw + (cols - 1) * GAP + GAP;
    {
        static const int ids[3] = {HIT_PAGE_KEY, HIT_BACK_KEY, HIT_OK_KEY};
        int ah = (NATIVE_NAMING_ROWS * KH + (NATIVE_NAMING_ROWS - 1) * GAP - 2 * GAP) / 3;
        int next = (page + 1) % NATIVE_NAMING_PAGES;

        for (int i = 0; i < 3; i++)
        {
            int ay = gy + i * (ah + GAP), ty = ay + (ah >> 1) - 6;
            UiRGBA col = i == 0 ? PageRamp(next).base : i == 1 ? UI_P_CMD_FIGHT : UI_P_CMD_POKEMON;
            struct UiRamp cr = UiRampOf(col, 0.42, 0.25, 0.62);
            bool on = editing && sN.cursorX >= cols && sN.cursorY == i;

            Keycap(ax, ay, AW, ah, col, on, false);
            if (i == 0)
            {
                u8 label[12];

                UiCaps_Draw(ax + 8, ty + 4, UI_CAPS_LEFT, UI_P_TEXT_W, cr.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
                UiText_FromAscii(label, sizeof(label), sPageLabels[next]);
                UiText_Draw(UI_FONT_NORMAL, ax + (AW >> 1), ty, label, UI_P_TEXT_W, cr.ink, UI_ALIGN_CENTER, 0);
                UiCaps_Draw(ax + AW - 13, ty + 4, UI_CAPS_RIGHT, UI_P_TEXT_W, cr.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
            }
            else
            {
                UiCaps_Draw(ax + (AW >> 1), ty + 4, i == 1 ? "BACK" : "OK", UI_P_TEXT_W, cr.ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
            }
            UiG_Hit(ids[i], 0, ax, ay, AW, ah);
        }
    }
}

/* ---------------------------------------------------------------- the foot */

static int CapsHint(int x, int y, const char *const *caps, int n, const u8 *label)
{
    for (int i = 0; i < n; i++)
        x = UiW_Keycap(x, y, caps[i]) + 2;
    x = UiG_TextW(x + 1, y - 3, label);
    return x + 10;
}

static void Foot(void)
{
    UiRGBA ink = UiMix(UI_P_BAG_INK, UI_P_INK, 0.4);
    bool pad = UiW_PromptDevice() == UI_DEVICE_PAD;
    int hx = 100, hy = 342;

    UiG_Paint(UiM_Rounded4(448, 22, 2, 2, 2, 2), 96, 304, UI_P_CREAM, &(struct UiPaint){ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (sN.phase == NATIVE_NAMING_DONE)
        UiG_Text(106, 306, sText_Saving);
    else if (sMsg != NULL && NativeUi_Frame() < sMsgUntil)
    {
        UiW_Cursor(104, 309, 0, UI_P_CMD_FIGHT);
        UiText_Draw(UI_FONT_NORMAL, 114, 306, sMsg, UI_P_TEXT_RED, UI_P_TEXT_RED_SH, UI_ALIGN_LEFT, 0);
    }
    else
        UiG_Text(106, 306, pad ? sText_HelpPad : sText_Help);
    if (sN.phase == NATIVE_NAMING_DONE)
        return;
    if (pad)
    {
        struct UiHintItem hints[4] = {{UI_ACT_CONFIRM, sText_Enter}, {UI_ACT_CANCEL, sText_Delete},
                                      {UI_ACT_SELECT, sText_Page}, {UI_ACT_START, sText_ToOk}};

        UiW_Hints(hx, hy, hints, 4, true, 10);
        return;
    }
    {
        static const char *const enter[1] = {"ENTER"}, *const bksp[1] = {"BKSP"}, *const pages[2] = {"PGUP", "PGDN"};

        hx = CapsHint(hx, hy, enter, 1, sText_Enter);
        hx = CapsHint(hx, hy, bksp, 1, sText_Delete);
        CapsHint(hx, hy, pages, 2, sText_Page);
    }
}

static void Draw(int t)
{
    struct UiRamp pr = PageRamp(sN.page);
    int bx, by, bw, bh;
    struct UiPattern dots = UiPat_Dots(12, 2, 0, 0, true);

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UiMix(pr.base, UI_P_INK, 0.55));
    UiG_FillPattern(&dots, bx, by, bw, bh, UiMix(pr.base, UI_P_INK, 0.4), t / 5, t / 5);
    Tag(t);
    Board(t);
    Foot();
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sN.ready;
}

static bool TextMode(void)
{
    return sN.phase == NATIVE_NAMING_INPUT || sN.phase == NATIVE_NAMING_SWAP || sN.phase == NATIVE_NAMING_LOADING;
}

/* one UTF-8 character as the engine's byte (0xFF: not on its keyboards) */
static u8 EngineChar(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned cp;

    if (s[0] < 0x80)
    {
        char ascii[2] = {(char)s[0], 0};
        u8 out[4];

        *p += 1;
        if (s[0] == ' ')
            return CHAR_SPACE;
        if (s[0] == '"')
            return 0xB1; /* “ */
        if (s[0] == '\'')
            return 0xB4; /* ’ (the keyboard's apostrophe) */
        UiText_FromAscii(out, sizeof(out), ascii);
        return out[0] == EOS ? 0xFF : out[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1] != 0)
    {
        cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        *p += 2;
    }
    else if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0)
    {
        cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        *p += 3;
    }
    else
    {
        *p += 1;
        while ((**p & 0xC0) == 0x80)
            *p += 1;
        return 0xFF;
    }
    switch (cp)
    {
    case 0x2642: return 0xB5; /* ♂ */
    case 0x2640: return 0xB6; /* ♀ */
    case 0x2026: return 0xB0; /* … */
    case 0x201C: return 0xB1; /* “ */
    case 0x201D: return 0xB2; /* ” */
    case 0x2018: return 0xB3; /* ‘ */
    case 0x2019: return 0xB4; /* ’ */
    default: return 0xFF;
    }
}

static void Text(const char *utf8)
{
    const char *p = utf8;

    while (*p != 0)
    {
        u8 ch = EngineChar(&p);

        if (ch == 0xFF || !OnAnyPage(ch))
        {
            Note(sText_NotHere);
            continue;
        }
        if (sLength + sTypedCount >= sN.maxChars)
        {
            Note(sText_Full);
            continue;
        }
        if (sTypedCount < MAX_TYPED)
            sTyped[sTypedCount++] = ch;
    }
    sPark = false;
    sGoalX = -1;
    Step();
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        sTypedCount = 0;
        sGoalX = -1;
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || sN.phase == NATIVE_NAMING_DONE)
        return false;
    switch (hit->id)
    {
    case HIT_KEY:
        sTypedCount = 0;
        sPark = false;
        sGoalX = hit->index % NATIVE_NAMING_COLS;
        sGoalY = hit->index / NATIVE_NAMING_COLS;
        sGoalSteps = 0;
        Step();
        return true;
    case HIT_PAGE_KEY:
        NativeUi_QueuePress(SELECT_BUTTON);
        return true;
    case HIT_BACK_KEY:
        NativeUi_QueuePress(B_BUTTON);
        return true;
    case HIT_OK_KEY:
        sTypedCount = 0;
        sGoalX = -1;
        NativeUi_QueuePress(START_BUTTON);
        NativeUi_QueuePress(A_BUTTON);
        return true;
    case HIT_TAB:
        if (hit->index != sN.page)
            NativeUi_QueuePresses(SELECT_BUTTON, (hit->index - sN.page + NATIVE_NAMING_PAGES) % NATIVE_NAMING_PAGES);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || sN.phase == NATIVE_NAMING_DONE || NativeUi_PressesPending())
        return false;
    NativeUi_QueuePresses(SELECT_BUTTON, dy > 0 ? 2 : 1);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Naming = {
    .name = "naming",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
    .textMode = TextMode,
    .text = Text,
};
#endif // ENABLE_NATIVE_UI
