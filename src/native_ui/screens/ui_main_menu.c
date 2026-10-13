#ifdef ENABLE_NATIVE_UI
/*
 * 21 · Main menu (the approved design's main menu, preview/js/screens/title.js):
 * Emerald's stacked menu windows over the title remaster, CONTINUE with the
 * saved game's summary and party, the game's other rows, QUIT GAME, and how
 * this window was opened. The engine's main menu (main_menu.c) keeps running
 * under it and keeps every rule: its rows, its cursor, its save checks and
 * messages, what A and B do. QUIT GAME is the PC's own row: it asks, then
 * closes the window like its close button (nothing is saved). Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "main.h"
#include "constants/characters.h"
#include "platform/mp_bridge.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_title.h"
#include "native_ui/ui_title_bg.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_MESSAGE,
};

static const u8 sText_Continue[] = _("CONTINUE");
static const u8 sText_NewGame[] = _("NEW GAME");
static const u8 sText_Options[] = _("OPTIONS");
static const u8 sText_MysteryGift[] = _("MYSTERY GIFT");
static const u8 sText_MysteryEvents[] = _("MYSTERY EVENTS");
static const u8 sText_Quit[] = _("QUIT GAME");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Title[] = _("Title");
static const u8 sText_QuitAsk[] = _("Close the game? Closing doesn't save: anything since\nyour last save in the game is lost.");
static const u8 sText_Linked[] = _("Multiplayer link on.");
static const u8 sText_Offline[] = _("No multiplayer link.");
static const u8 sText_Linked1[] = _("This window was opened by the launcher");
static const u8 sText_Linked2[] = _("for a multiplayer session. Host Game and");
static const u8 sText_Linked3[] = _("Join Game are in the launcher.");
static const u8 sText_Offline1[] = _("This window was started on its own.");
static const u8 sText_Offline2[] = _("Host Game and Join Game are in the");
static const u8 sText_Offline3[] = _("EmeraldTogether launcher.");

static struct NativeMainMenu sMenu;
static struct NativeSaveSummary sSave;
static bool sHasSave;
static bool sLinked;
static bool sQuitRow;   /* the PC's QUIT GAME row is selected (the engine's cursor stays on its last row) */
static bool sAsk;       /* "Close the game?" is up */
static int sAskSel;     /* 0 yes, 1 no */
static int sPressT;     /* the chosen row shows pressed for 2 frames */
static u8 sPrevPhase;
static const u8 *sMessageStart;
static const u8 *sMessageAt;
static bool sMessagePrinting;

static bool Detect(void)
{
    struct NativeMainMenu m;

    return MainMenu_NativeUiState(&m);
}

static void Capture(void)
{
    sPrevPhase = sMenu.phase;
    MainMenu_NativeUiState(&sMenu);
    if (sMenu.phase != NATIVE_MAIN_MENU_INPUT)
    {
        sQuitRow = false;
        sAsk = false;
    }
    if (sMenu.phase == NATIVE_MAIN_MENU_CHOSEN && sPrevPhase != NATIVE_MAIN_MENU_CHOSEN)
        sPressT = 2;
    else if (sPressT > 0)
        sPressT--;
    sHasSave = sMenu.rowCount > 0 && sMenu.rows[0] == NATIVE_MAIN_MENU_CONTINUE;
    if (sHasSave)
        MainMenu_NativeUiSaveSummary(&sSave);
    sLinked = MpBridge_IsEnabled();
    sMessageStart = sMessageAt = NULL;
    sMessagePrinting = false;
    if (sMenu.phase == NATIVE_MAIN_MENU_MESSAGE)
    {
        const u8 *start, *at;
        bool8 active;

        if (TextPrinter_NativeUiWindow(7, &start, &at, &active) && start == sMenu.message)
        {
            sMessageStart = start;
            sMessageAt = active ? at : NULL;
            sMessagePrinting = active;
        }
        else
        {
            sMessageStart = sMenu.message;
        }
    }
}

static const u8 *RowLabel(int row)
{
    if (row >= sMenu.rowCount)
        return sText_Quit;
    switch (sMenu.rows[row])
    {
    case NATIVE_MAIN_MENU_CONTINUE: return sText_Continue;
    case NATIVE_MAIN_MENU_NEW_GAME: return sText_NewGame;
    case NATIVE_MAIN_MENU_OPTION: return sText_Options;
    case NATIVE_MAIN_MENU_MYSTERY_GIFT: return sText_MysteryGift;
    default: return sText_MysteryEvents;
    }
}

static int Selected(void)
{
    return sQuitRow ? sMenu.rowCount : sMenu.cursor;
}

static void Pair(int lx, int rx, int ry, const char *label, const u8 *value, UiRGBA lab)
{
    UiCaps_Draw(lx, ry + 5, label, lab, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_TextEx(rx, ry, value, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
}

static void ContinueCard(int x, int yy, int w, bool on, int t)
{
    struct UiRamp gem = UiRampOf2(UI_HEX(0x28A868), 0.45, 0.3, 0.68, UI_NONE, UI_RGB(8, 40, 40));
    UiRGBA lab = UiMix(gem.lo, UI_P_TEXT_MUTED, 0.3);
    struct UiFilter gray = {0.65f, 0, 0, 0};
    u8 buf[24];
    int ry;

    for (int k = 0; k < sSave.partyCount; k++)
    {
        bool alive = !sSave.fainted[k];

        UiG_Save();
        UiG_Clip(x + 10 + k * 45, yy + 20, 42, 28);
        UiGfx_MonIcon(sSave.species[k], sSave.personality[k], sSave.isEgg[k],
                      alive ? ((t + k * 7) / (on ? 10 : 20)) % 2 : 0, x + 15 + k * 45, yy + 18, alive ? NULL : &gray);
        UiG_Restore();
    }
    UiG_HLine(x + 10, yy + 49, w - 20, UI_P_PAPER_LO);
    ry = yy + 51;
    Pair(x + 12, x + 136, ry, "PLAYER", sSave.playerName, lab);
    buf[0] = EOS;
    UiText_AppendNumber(buf, sizeof(buf), sSave.hours, 1, false);
    UiText_AppendAscii(buf, sizeof(buf), ":");
    UiText_AppendNumber(buf, sizeof(buf), sSave.minutes, 2, false);
    Pair(x + 152, x + 276, ry, "TIME", buf, lab);
    ry += 17;
    if (sSave.hasPokedex)
    {
        buf[0] = EOS;
        UiText_AppendNumber(buf, sizeof(buf), sSave.dexCaught, 1, false);
        Pair(x + 12, x + 136, ry, "POK" UI_CAPS_E_ACUTE "DEX", buf, lab);
    }
    buf[0] = EOS;
    UiText_AppendNumber(buf, sizeof(buf), sSave.badges, 1, false);
    Pair(x + 152, x + 276, ry, "BADGES", buf, lab);
    ry += 17;
    if (sSave.location[0] != EOS)
        Pair(x + 12, x + 276, ry, "LOCATION", sSave.location, lab);
}

static void MultiplayerPanel(void)
{
    struct UiRamp gem = UiRampOf2(UI_HEX(0x28A868), 0.45, 0.3, 0.68, UI_NONE, UI_RGB(8, 40, 40));
    int px = 368, py = 262, pw = 264, ph = 90;

    UiG_Paint(UiM_Rounded4(pw, ph, 3, 3, 3, 3), px, py, UI_P_CREAM,
              &(struct UiPaint){gem.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(pw - 6, 16, 2, 2, 0, 0), px + 3, py + 3, gem.lo,
              &(struct UiPaint){UI_NONE, gem.base, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(px + 9, py + 8, "MULTIPLAYER", UI_P_TEXT_W, gem.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(px + pw - 10, py + 8, sLinked ? "LAUNCHER" : "OFFLINE", UI_HEX(0xD8F8E0), UI_NONE, UI_CAPS_PLAIN,
                UI_ALIGN_RIGHT, UI_NONE);
    UiG_Text(px + 9, py + 21, sLinked ? sText_Linked : sText_Offline);
    UiText_Draw(UI_FONT_SMALL, px + 9, py + 39, sLinked ? sText_Linked1 : sText_Offline1, UI_P_TEXT, UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px + 9, py + 52, sLinked ? sText_Linked2 : sText_Offline2, UI_P_TEXT, UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px + 9, py + 65, sLinked ? sText_Linked3 : sText_Offline3, UI_P_TEXT, UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
}

/* the engine's message page being printed (after the last paragraph break before
 * the printer), up to the printer */
static void MessageBox(const u8 *text, const u8 *upTo, bool printing, bool yesNo, int t)
{
    int b[4];
    const u8 *starts[8];
    int lengths[8];
    int n;
    u8 page[256];
    int len = 0;
    const u8 *p = text, *pageStart = text;

    UiW_DialogBox(8, 300, 624, 52, NULL, UI_NONE, b);
    for (; *p != EOS && (upTo == NULL || p < upTo); p++)
        if (*p == 0xFA || *p == 0xFB) /* CHAR_PROMPT_SCROLL, CHAR_PROMPT_CLEAR: a new page */
            pageStart = p + 1;
    for (p = pageStart; *p != EOS && (upTo == NULL || p < upTo) && *p != 0xFA && *p != 0xFB && len < 254; p++)
        page[len++] = *p;
    page[len] = EOS;
    n = UiText_Wrap(page, b[2] - 20, UI_FONT_NORMAL, starts, lengths, 8);
    for (int i = n > 2 ? n - 2 : 0, row = 0; i < n; i++, row++)
    {
        u8 line[128];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiG_Text(b[0] + 2, b[1] - 1 + row * 16, line);
    }
    if (yesNo)
        UiW_YesNo(632, 296, sAskSel, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    else if (!printing)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 30 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_MESSAGE, 0, 8, 300, 624, 52);
}

static void Draw(int t)
{
    struct UiTitleUi u;
    struct UiRamp gem = UiRampOf2(UI_HEX(0x28A868), 0.45, 0.3, 0.68, UI_NONE, UI_RGB(8, 40, 40));
    struct UiRamp band = UiW_FieldBand();
    int f = (int)UiTitle_Clock();
    int x = 16, y = 80, w = 288;
    int rows = sMenu.rowCount > 0 ? sMenu.rowCount + 1 : 0; /* + QUIT GAME */
    char corner[64];

    UiTitle_DrawBackground();
    UiTitleBg_Ui('c', f, &u);
    UiTitle_Wordmark(22, 26, f, u.glint);
    for (int i = 0; i < rows; i++)
    {
        bool big = i < sMenu.rowCount && sMenu.rows[i] == NATIVE_MAIN_MENU_CONTINUE && sHasSave;
        int h = big ? 116 : 22;
        bool on = i == Selected() && sMenu.phase != NATIVE_MAIN_MENU_LOADING;
        int yy = y + (on && sPressT > 0 ? 1 : 0);
        const struct UiMask *m = UiM_Rounded4(w, h, 3, 3, 3, 3);

        if (on)
            UiW_SelectFrame(m, x, yy, UI_NONE, UI_NONE, 2);
        UiW_DialogBox(x, yy, w, h, &band, UI_NONE, NULL);
        UiG_Text(x + 12, yy + (big ? 4 : 3), RowLabel(i));
        if (big)
            ContinueCard(x, yy, w, on, t);
        UiG_Hit(HIT_ROW, i, x, y, w, h);
        y += h + 4;
    }
    MultiplayerPanel();
    snprintf(corner, sizeof(corner), "PRIVATE BUILD %s", __DATE__);
    for (char *p = corner; *p != '\0'; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    UiCaps_Draw(10, 346, corner, UI_HEX(0xE8F8EC), gem.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    if (sAsk)
    {
        MessageBox(sText_QuitAsk, NULL, false, true, t);
    }
    else if (sMenu.phase == NATIVE_MAIN_MENU_MESSAGE && sMessageStart != NULL)
    {
        MessageBox(sMessageStart, sMessageAt, sMessagePrinting, false, t);
    }
    else if (sMenu.phase == NATIVE_MAIN_MENU_INPUT)
    {
        struct UiHintItem hints[2] = {{UI_ACT_CONFIRM, sText_Choose}, {UI_ACT_CANCEL, sText_Title}};

        UiW_Hints(372, 246, hints, 2, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static void Quit(void)
{
    NativeUi_QuitGame();
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        /* right click: back (the engine's B, or closing the question) */
        if (sAsk)
            sAsk = false;
        else
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sAsk)
    {
        if (hit->id == UI_HIT_YESNO)
        {
            if (hit->index == 0)
                Quit();
            else
                sAsk = false;
        }
        return true;
    }
    if (hit->id == HIT_MESSAGE && sMenu.phase == NATIVE_MAIN_MENU_MESSAGE)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (hit->id == HIT_ROW && sMenu.phase == NATIVE_MAIN_MENU_INPUT && !NativeUi_PressesPending())
    {
        if (hit->index >= sMenu.rowCount)
        {
            sQuitRow = true;
            sAsk = true;
            sAskSel = 1;
        }
        else
        {
            int moves = hit->index - sMenu.cursor;

            sQuitRow = false;
            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    }
    return false;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    if (sAsk)
    {
        if (newKeys & (DPAD_UP | DPAD_DOWN))
            sAskSel ^= 1;
        if (newKeys & A_BUTTON)
        {
            if (sAskSel == 0)
                Quit();
            else
                sAsk = false;
        }
        else if (newKeys & B_BUTTON)
        {
            sAsk = false;
        }
        return 0; /* the engine's menu waits meanwhile */
    }
    if (sMenu.phase != NATIVE_MAIN_MENU_INPUT)
        return keys;
    if (sQuitRow)
    {
        if (newKeys & DPAD_UP)
            sQuitRow = false;
        if (newKeys & A_BUTTON)
        {
            sAsk = true;
            sAskSel = 1;
        }
        if (newKeys & B_BUTTON)
        {
            sQuitRow = false;
            return keys; /* B: back to the title, as from any row */
        }
        return keys & ~(DPAD_UP | DPAD_DOWN | A_BUTTON | START_BUTTON);
    }
    if ((newKeys & DPAD_DOWN) && sMenu.ready && sMenu.cursor == sMenu.rowCount - 1)
    {
        sQuitRow = true;
        return keys & ~DPAD_DOWN;
    }
    return keys;
}

static bool Ready(void)
{
    return sMenu.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_MainMenu = {
    .name = "main_menu",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
