#ifdef ENABLE_NATIVE_UI
/*
 * Easy Chat (not one of the 31 designed screens; the design's own pieces): the
 * engine's word-by-word phrase screen (easy_chat.c) in every use - a Mail's
 * words, the profile and the battle words, an interview, the Bard's song, the
 * Quiz Lady's quiz - over the engine's own screen. The phrase in the design's
 * cream card under the screen's title (each word, or the game's dashes for a
 * blank), the footer's buttons (DEL. ALL, CANCEL, OK, and QUIZ or ANSWER), and
 * the lower window as the engine has it: the keyboard in the game's two modes
 * (the unlocked groups, or A-Z) with its SELECT MODE / DELETE / CANCEL
 * buttons, or a group's words with their scroll; the game's message and its
 * YES / NO in the design's boxes. The engine keeps every rule and button: a
 * click moves its cursor one press at a time (the engine reads none while a
 * window moves) and presses A there; a right click is B, the wheel scrolls a
 * list. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "easy_chat.h"
#include "main.h"
#include "constants/characters.h"
#include "constants/easy_chat.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_WORD = UI_HIT_SCREEN,  /* index: column + row * 8 */
    HIT_FOOTER,
    HIT_KEY,                   /* index: (column + 1) + row * 8; column -1 the buttons */
    HIT_LIST,                  /* index: column + row * 2, on screen */
    HIT_MESSAGE,
};

/* the engine's input states (easy_chat.c) */
enum
{
    EC_PHRASE,
    EC_FOOTER,
    EC_KEYBOARD,
    EC_WORDS,
    EC_EXIT_PROMPT,
    EC_DELETE_ALL,
    EC_CONFIRM,
    EC_QUIZ_QUESTION,
    EC_MESSAGE,
    EC_LYRICS,
    EC_LYRICS_YES_NO,
};

#define CARD_X 40
#define CARD_Y 32
#define CARD_W 560
#define LOWER_X 40
#define LOWER_Y 196
#define LOWER_W 560
#define LOWER_H 104
#define ROWS_SHOWN 4
#define BUTTONS_W 112
#define GOAL_STEPS 48

static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Quit[] = _("Quit");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Mode[] = _("Mode");
static const u8 sText_PageUp[] = _("Pg up");
static const u8 sText_PageDown[] = _("Pg down");
static const u8 sText_Answer[] = _("Answer");
static const u8 sText_Dashes[] = _("------------");
static const u8 sText_Empty[] = _("");

/* A-Z as the engine lays it out (sAlphabetGroupIdMap): its groups 1-26 the letters, 0 the others */
static const u8 sAlphabet[ROWS_SHOWN][7] = {
    {1, 2, 3, 4, 5, 6, 0},
    {7, 8, 9, 10, 11, 12, 0xFF},
    {13, 14, 15, 16, 17, 18, 19},
    {20, 21, 22, 23, 24, 25, 26},
};

static struct NativeEasyChat sEc;
static u8 sPhrase[NATIVE_EC_PHRASE_MAX][32];
static u8 sShown[NATIVE_EC_SHOWN_WORDS][32];
static u8 sMessage[160];

/* a click's target: the state it is for and the cursor it wants there (then A) */
static struct
{
    bool active;
    u8 state;
    s8 column, row;
    u8 steps;
} sGoal;

static const struct UiRamp sOrange = {UI_HEX(0xF8C890), UI_HEX(0xE89850), UI_HEX(0xC07030), UI_HEX(0x502810)};
static const struct UiRamp sGreen = {UI_HEX(0xB8E8B0), UI_HEX(0x78C070), UI_HEX(0x489848), UI_HEX(0x1C4020)};

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativeEasyChat e;

    return EasyChat_NativeUiState(&e);
}

/* the cursor the engine has for a state: the main cursor for a word and the footer, the keyboard's, the list's, the
 * YES / NO's */
static bool Cursor(u8 state, s8 *column, s8 *row)
{
    switch (state)
    {
    case EC_PHRASE:
    case EC_FOOTER:
        if (sEc.inputState != EC_PHRASE && sEc.inputState != EC_FOOTER)
            return false;
        *column = sEc.cursorColumn, *row = sEc.cursorRow;
        return true;
    case EC_KEYBOARD:
        if (sEc.inputState != EC_KEYBOARD)
            return false;
        *column = sEc.keyboardColumn, *row = sEc.keyboardRow;
        return true;
    case EC_WORDS:
        if (sEc.inputState != EC_WORDS)
            return false;
        *column = sEc.wordColumn, *row = sEc.wordRow;
        return true;
    default:
        if (!sEc.yesNo || sEc.inputState != state)
            return false;
        *column = 0, *row = sEc.yesNoCursor;
        return true;
    }
}

/* one press toward a click's target each time the engine reads buttons */
static void StepGoal(void)
{
    s8 column, row;

    if (!sGoal.active || !sEc.ready || NativeUi_PressesPending())
        return;
    if (!Cursor(sGoal.state, &column, &row) || ++sGoal.steps > GOAL_STEPS)
    {
        sGoal.active = false;
        return;
    }
    if (column == sGoal.column && row == sGoal.row)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal.active = false;
    }
    else if (row != sGoal.row)
        NativeUi_QueuePress(row < sGoal.row ? DPAD_DOWN : DPAD_UP);
    else
        NativeUi_QueuePress(column < sGoal.column ? DPAD_RIGHT : DPAD_LEFT);
}

static void Capture(void)
{
    const u8 *m[2];
    u8 *p = sMessage;

    EasyChat_NativeUiState(&sEc);
    for (int i = 0; i < sEc.wordCount && i < NATIVE_EC_PHRASE_MAX; i++)
    {
        sPhrase[i][0] = EOS;
        if (sEc.words[i] != EC_EMPTY_WORD)
            CopyEasyChatWord(sPhrase[i], sEc.words[i]);
    }
    for (int i = 0; i < NATIVE_EC_SHOWN_WORDS; i++)
    {
        sShown[i][0] = EOS;
        if (sEc.shownWords[i] != EC_EMPTY_WORD)
            CopyEasyChatWord(sShown[i], sEc.shownWords[i]);
    }
    /* the message's two lines as one page */
    m[0] = sEc.message[0], m[1] = sEc.message[1];
    *p = EOS;
    for (int i = 0; i < 2; i++)
    {
        if (m[i] == NULL)
            continue;
        if (p != sMessage)
            *p++ = CHAR_NEWLINE;
        for (const u8 *s = m[i]; *s != EOS && p < sMessage + sizeof(sMessage) - 2; s++)
            *p++ = *s;
        *p = EOS;
    }
    StepGoal();
}

/* ---------------------------------------------------------------- drawing */

static bool LowerUp(void)
{
    return sEc.inputState == EC_KEYBOARD || sEc.inputState == EC_WORDS;
}

/* a cell: a rounded chip, framed when the cursor is on it */
static void Chip(int x, int y, int w, int h, bool on, bool dim, const struct UiRamp *r, int t)
{
    const struct UiMask *m = UiM_Rounded4(w, h, 3, 3, 3, 3);
    UiRGBA body = on ? UI_HEX(0xFFFFF8) : dim ? UiMix(UI_P_CREAM, r->lo, 0.12) : UI_P_CREAM;

    if (on)
        UiW_SelectFrame(m, x, y, UI_NONE, UI_NONE, 2);
    UiG_Paint(m, x, y, body, &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (on)
        UiW_Cursor(x + 4, y + ((h - 9) >> 1), UiW_Nudge(t), r->ink);
}

static void Phrase(int t)
{
    int cols = sEc.numColumns > 0 ? sEc.numColumns : 1, rows = sEc.numRows;
    int rowH = rows > 3 ? 22 : 26, gap = 10, slotW = (CARD_W - 24 - (cols - 1) * gap) / cols;
    int cardH = rows * rowH + 18, y0 = CARD_Y + 10;
    bool mainUp = sEc.inputState == EC_PHRASE, editing = LowerUp();

    UiG_Paint(UiM_Rounded4(CARD_W, cardH, 4, 4, 4, 4), CARD_X, CARD_Y, UI_P_CREAM,
              &(struct UiPaint){sOrange.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++)
        {
            int i = row * cols + col, x = CARD_X + 12 + col * (slotW + gap), y = y0 + row * rowH;
            bool on = (mainUp || editing) && sEc.cursorColumn == col && sEc.cursorRow == row;

            if (i >= sEc.wordCount)
                continue;
            Chip(x, y, slotW, rowH - 4, on && mainUp, false, &sOrange, t);
            if (on && editing)
                UiG_Paint(UiM_Rounded4(slotW, rowH - 4, 3, 3, 3, 3), x, y, UiWithAlpha(sGreen.hi, 150),
                          &(struct UiPaint){sGreen.lo, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            if (sPhrase[i][0] != EOS)
                UiText_Draw(UI_FONT_NORMAL, x + 16, y + ((rowH - 4 - 14) >> 1), sPhrase[i], UI_P_TEXT, UI_P_TEXT_SH,
                            UI_ALIGN_LEFT, 0);
            else
                UiText_Draw(UI_FONT_NORMAL, x + 16, y + ((rowH - 4 - 14) >> 1), sText_Dashes, UI_P_TEXT_MUTED,
                            UI_NONE, UI_ALIGN_LEFT, 0);
            UiG_Hit(HIT_WORD, col + row * 8, x, y, slotW, rowH - 4);
        }

    /* the footer, under the card */
    if (sEc.footerCount > 0)
    {
        int fw = 112, fgap = 12, total = sEc.footerCount * fw + (sEc.footerCount - 1) * fgap;
        int fx = CARD_X + ((CARD_W - total) >> 1), fy = CARD_Y + cardH + 6;

        for (int i = 0; i < sEc.footerCount; i++)
        {
            int x = fx + i * (fw + fgap);
            bool on = sEc.inputState == EC_FOOTER && sEc.cursorColumn == i;

            Chip(x, fy, fw, 20, on, false, &sOrange, t);
            if (sEc.footer[i] != NULL)
                UiText_Draw(UI_FONT_NORMAL, x + (fw >> 1) + 4, fy + 3, sEc.footer[i], UI_P_TEXT, UI_P_TEXT_SH,
                            UI_ALIGN_CENTER, 0);
            UiG_Hit(HIT_FOOTER, i, x, fy, fw, 20);
        }
    }
}

/* the keyboard's own buttons (the game's button window): SELECT MODE, DELETE, B BUTTON CANCEL */
static void Buttons(int x, int y, int t)
{
    static const char *const sLabels[3] = {"MODE", "DELETE", "CANCEL"};
    static const enum UiAction sKeys[3] = {UI_ACT_SELECT, UI_ACT_COUNT, UI_ACT_CANCEL};

    for (int i = 0; i < 3; i++)
    {
        int by = y + i * 30;
        bool on = sEc.inputState == EC_KEYBOARD && sEc.keyboardColumn == -1 && sEc.keyboardRow == i;

        Chip(x, by, BUTTONS_W - 12, 24, on, false, &sGreen, t);
        UiCaps_Draw(x + 18, by + 9, sLabels[i], sGreen.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (sKeys[i] != UI_ACT_COUNT)
            UiW_Hint(x + BUTTONS_W - 34, by + 5, sKeys[i], sText_Empty, false);
        UiG_Hit(HIT_KEY, 0 + i * 8, x, by, BUTTONS_W - 12, 24);
    }
}

static void Keyboard(int t)
{
    int gx = LOWER_X + BUTTONS_W + 12, gy = LOWER_Y + 10, gw = LOWER_W - BUTTONS_W - 24;
    char mode[16];

    Buttons(LOWER_X + 12, gy, t);
    /* the mode the keyboard is in, as the game's tag says it */
    strcpy(mode, sEc.alphabet ? "A-Z MODE" : "GROUP MODE");
    UiCaps_Draw(LOWER_X + LOWER_W - 12, LOWER_Y - 10, mode, UI_P_TEXT_W, sGreen.ink, UI_CAPS_OUTLINE, UI_ALIGN_RIGHT,
                UI_NONE);
    if (!sEc.alphabet)
    {
        int cw = (gw - 8) / 2;

        for (int row = 0; row < ROWS_SHOWN; row++)
            for (int col = 0; col < 2; col++)
            {
                int i = (sEc.keyboardScroll + row) * 2 + col, x = gx + col * (cw + 8), y = gy + row * 22;
                bool on = sEc.keyboardColumn == col && sEc.keyboardRow == row;

                if (i >= sEc.groupCount || sEc.groupNames[i] == NULL)
                    continue;
                Chip(x, y, cw, 20, on, false, &sGreen, t);
                UiText_Draw(UI_FONT_NORMAL, x + 16, y + 3, sEc.groupNames[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
                UiG_Hit(HIT_KEY, (col + 1) + row * 8, x, y, cw, 20);
            }
        /* more groups above or below */
        if (sEc.keyboardScroll > 0)
            UiW_DownArrow(gx + gw - 10, LOWER_Y + 2, sGreen.ink, true);
        if ((sEc.keyboardScroll + ROWS_SHOWN) * 2 < sEc.groupCount)
            UiW_DownArrow(gx + gw - 10, LOWER_Y + LOWER_H - 8, sGreen.ink, false);
    }
    else
    {
        int cw = (gw - 6 * 6) / 7;

        for (int row = 0; row < ROWS_SHOWN; row++)
            for (int col = 0; col < 7; col++)
            {
                int x = gx + col * (cw + 6), y = gy + row * 22, w = cw;
                u8 g = sAlphabet[row][col], label[8];
                bool on = sEc.keyboardColumn == col && sEc.keyboardRow == row;

                if (g == 0xFF)
                    continue;
                if (g == 0)
                {
                    /* "others" takes the end of the first row */
                    UiText_FromAscii(label, sizeof(label), "others");
                }
                else
                {
                    label[0] = CHAR_A + g - 1;
                    label[1] = EOS;
                }
                Chip(x, y, w, 20, on, false, &sGreen, t);
                UiText_Draw(UI_FONT_NORMAL, x + (w >> 1) + 4, y + 3, label, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
                UiG_Hit(HIT_KEY, (col + 1) + row * 8, x, y, w, 20);
            }
    }
}

static void Words(int t)
{
    int gx = LOWER_X + 12, gy = LOWER_Y + 10, gw = LOWER_W - 24, cw = (gw - 30) / 2;

    for (int row = 0; row < ROWS_SHOWN; row++)
        for (int col = 0; col < 2; col++)
        {
            int i = row * 2 + col, x = gx + col * (cw + 10), y = gy + row * 22;
            bool on = sEc.wordColumn == col && sEc.wordRow == row;

            if (sShown[i][0] == EOS)
                continue;
            Chip(x, y, cw, 20, on, false, &sGreen, t);
            UiText_Draw(UI_FONT_NORMAL, x + 16, y + 3, sShown[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            UiG_Hit(HIT_LIST, i, x, y, cw, 20);
        }
    /* the game's START (a page up) and SELECT (a page down) */
    if (sEc.wordScroll > 0)
        UiW_DownArrow(gx + gw - 8, LOWER_Y + 2, sGreen.ink, true);
    if (sEc.wordScroll + ROWS_SHOWN <= sEc.wordLastRow)
        UiW_DownArrow(gx + gw - 8, LOWER_Y + LOWER_H - 8, sGreen.ink, false);
}

static int Hints(struct UiHintItem *h)
{
    int n = 0;

    if (!sEc.ready)
        return 0;
    switch (sEc.inputState)
    {
    case EC_PHRASE:
    case EC_FOOTER:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Quit};
        h[n++] = (struct UiHintItem){UI_ACT_START, sText_OK};
        break;
    case EC_KEYBOARD:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Mode};
        break;
    case EC_WORDS:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        h[n++] = (struct UiHintItem){UI_ACT_START, sText_PageUp};
        h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_PageDown};
        break;
    case EC_QUIZ_QUESTION:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Answer};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Quit};
        break;
    case EC_MESSAGE:
    case EC_LYRICS:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    default:
        if (sEc.yesNo)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        }
        break;
    }
    return n;
}

static void Draw(int t)
{
    struct UiPattern a = UiPat_Diag(24, 4, 0, 1), b = UiPat_Diag(24, 4, 0, -1);
    struct UiRamp tab = {sOrange.hi, sOrange.base, sOrange.lo, sOrange.ink};
    struct UiHintItem h[4];
    char title[40];
    int bx, by, bw, bh, n;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UiMix(sOrange.lo, sOrange.ink, 0.35));
    UiG_FillPattern(&a, bx, by, bw, bh, UiMix(sOrange.lo, sOrange.ink, 0.2), t / 5, 0);
    UiG_FillPattern(&b, bx, by, bw, bh, UiMix(sOrange.lo, sOrange.ink, 0.2), t / 5, 0);
    if (sEc.title != NULL)
        UiText_ToAscii(title, sizeof(title), sEc.title);
    else
        strcpy(title, "EASY CHAT");
    UiW_TitleTab(0, 8, title, &tab, 0, NULL);
    Phrase(t);
    if (LowerUp())
    {
        UiG_Paint(UiM_Rounded4(LOWER_W, LOWER_H, 4, 4, 4, 4), LOWER_X, LOWER_Y, UiMix(sGreen.base, sGreen.hi, 0.5),
                  &(struct UiPaint){sGreen.ink, sGreen.hi, sGreen.lo, 0, 0, NULL, UI_NONE});
        if (sEc.inputState == EC_KEYBOARD)
            Keyboard(t);
        else
            Words(t);
    }
    n = Hints(h);
    UiField_MessageBox(t, bx, bx + bw, by + bh, sMessage[0] != EOS ? sMessage : NULL, -1, false, NULL, h, n,
                       UI_P_FIELD_INK, HIT_MESSAGE);
    if (sEc.yesNo && sEc.ready)
        UiW_YesNo(bx + bw - 8, by + bh - UI_FIELD_MENU_GAP, sEc.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sEc.ready;
}

static void SetGoal(u8 state, s8 column, s8 row)
{
    sGoal.active = true;
    sGoal.state = state;
    sGoal.column = column;
    sGoal.row = row;
    sGoal.steps = 0;
    StepGoal();
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        if (sEc.ready && !NativeUi_PressesPending())
            NativeUi_QueuePress(B_BUTTON);
        sGoal.active = false;
        return true;
    }
    if (button != 1 || NativeUi_PressesPending() || sGoal.active)
        return true;
    switch (hit->id)
    {
    case HIT_WORD:
        if (sEc.inputState == EC_PHRASE || sEc.inputState == EC_FOOTER)
            SetGoal(EC_PHRASE, hit->index % 8, hit->index / 8);
        return true;
    case HIT_FOOTER:
        if (sEc.inputState == EC_PHRASE || sEc.inputState == EC_FOOTER)
            SetGoal(EC_FOOTER, hit->index, sEc.numRows);
        return true;
    case HIT_KEY:
        if (sEc.inputState == EC_KEYBOARD)
            SetGoal(EC_KEYBOARD, hit->index % 8 - 1, hit->index / 8);
        return true;
    case HIT_LIST:
        if (sEc.inputState == EC_WORDS)
            SetGoal(EC_WORDS, hit->index % 2, hit->index / 2);
        return true;
    case UI_HIT_YESNO:
        if (sEc.yesNo && hit->index >= 0 && hit->index < 2)
            SetGoal(sEc.inputState, 0, hit->index);
        return true;
    case HIT_MESSAGE:
        if (sEc.inputState == EC_MESSAGE || sEc.inputState == EC_LYRICS || sEc.inputState == EC_QUIZ_QUESTION)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sEc.ready || NativeUi_PressesPending())
        return false;
    if (sEc.inputState == EC_WORDS || sEc.inputState == EC_KEYBOARD)
    {
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_EasyChat = {
    .name = "easy_chat",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
