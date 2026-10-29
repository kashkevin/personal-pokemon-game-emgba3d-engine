#ifdef ENABLE_NATIVE_UI
/*
 * The Berry Blender (not one of the 31 designed screens; the design's
 * pieces): the blender stays the game's picture - its bowl turning, the
 * players' arrows, the berries thrown, the hits' marks, the speed - and
 * round it the players' names are the design's plates where the game puts
 * them (the player's lit), the game's lines are the field's message box, the
 * ranking (each place's BEST, GOOD and MISS) and the results (each place's
 * berry, the top speed, the time) are the design's boards where the game puts
 * its window, and "blend another berry?" takes the design's YES / NO. While it
 * blends a click is A (the press at the arrow). Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "menu.h"
#include "sprite.h"
#include "string_util.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_SCREEN_ALL = UI_HIT_SCREEN,
};

/* the results window where the game puts it (5, 3; 21 x 14 tiles) at twice its size */
#define BOARD_X 156
#define BOARD_Y 62
#define BOARD_W 344
#define BOARD_H 236

static const u8 sText_Next[] = _("Next");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");

static struct NativeBerryBlender sB;
static u8 sMsg[UI_MESSAGE_COPY];
static u32 sMsgPrinted;
static bool sHasMsg, sMsgPrinting, sMsgWaiting;

/* ---------------------------------------------------------------- state */

static bool Detect(void)
{
    struct NativeBerryBlender b;

    return BerryBlender_NativeUiState(&b);
}

static void Capture(void)
{
    const struct NativeUiPrinted *p;
    bool8 active = FALSE;

    BerryBlender_NativeUiState(&sB);
    /* the message window while its own tiles are up (the results share its tiles) */
    sHasMsg = sB.board == NATIVE_BLENDER_BOARD_NONE && Window_NativeUiOnScreen(sB.msgWindow)
           && TextPrinter_NativeUiTranscript(sB.msgWindow, &p) > 0
           && TextPrinter_NativeUiWindowText(sB.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(sB.msgWindow);
}

/* ---------------------------------------------------------------- drawing */

/* a player's name where the game's window is (its tiles at twice their size) */
static void NamePlate(int i)
{
    static const s8 sPos[NATIVE_BLENDER_PLAYERS][2] = {{1, 6}, {22, 6}, {1, 12}, {22, 12}};
    struct UiRamp r = UiRampDefault(sB.isPlayer[i] ? UI_HEX(0xE84838) : UI_HEX(0x5878B8));
    int x = 80 + sPos[i][0] * 16, y = 20 + sPos[i][1] * 16, w = 112, h = 28;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 6, sB.names[i], UI_P_TEXT_W, r.ink, UI_ALIGN_CENTER, 0);
}

static void Number(int xRight, int y, u32 value)
{
    u8 text[12];

    text[0] = EOS;
    UiText_AppendNumber(text, sizeof(text), (long)value, 1, false);
    UiText_Draw(UI_FONT_NORMAL, xRight, y, text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
}

static void Place(int x, int y, int i)
{
    u8 text[32];

    text[0] = CHAR_1 + i;
    text[1] = CHAR_PERIOD;
    text[2] = CHAR_SPACE;
    StringCopyN(text + 3, sB.placeNames[i], sizeof(text) - 4);
    text[sizeof(text) - 1] = EOS;
    /* the player's own place in red, as the game's names show the player */
    UiText_Draw(UI_FONT_NORMAL, x, y, text, i == sB.ownPlace ? UI_HEX(0xC83828) : UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

static void Ranking(void)
{
    static const char *const sCols[] = {"BEST", "GOOD", "MISS"};
    int b[4], cx = BOARD_X + BOARD_W / 2;

    UiW_DialogBox(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, NULL, UI_NONE, b);
    UiItems_BigCaps(cx, BOARD_Y + 14, "RANKING", 2, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER);
    for (int c = 0; c < 3; c++)
        UiCaps_Draw(BOARD_X + 196 + c * 52, BOARD_Y + 56, sCols[c], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT,
                    UI_NONE);
    UiG_HLine(BOARD_X + 16, BOARD_Y + 68, BOARD_W - 32, UI_P_TEXT_MUTED);
    for (int i = 0; i < sB.numPlayers; i++)
    {
        int y = BOARD_Y + 78 + i * 32;

        Place(BOARD_X + 20, y, i);
        for (int c = 0; c < 3; c++)
            Number(BOARD_X + 196 + c * 52, y, sB.hits[i][c]);
    }
}

static void Results(void)
{
    int b[4], cx = BOARD_X + BOARD_W / 2, y = BOARD_Y + 60;
    u8 text[32];

    UiW_DialogBox(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, NULL, UI_NONE, b);
    UiItems_BigCaps(cx, BOARD_Y + 14, "RESULTS", 2, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER);
    for (int i = 0; i < sB.numPlayers; i++, y += 26)
    {
        Place(BOARD_X + 20, y, i);
        UiText_Draw(UI_FONT_NORMAL, BOARD_X + BOARD_W - 20, y, sB.berries[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
    y = BOARD_Y + BOARD_H - 62;
    UiG_HLine(BOARD_X + 16, y - 8, BOARD_W - 32, UI_P_TEXT_MUTED);
    UiCaps_Draw(BOARD_X + 20, y + 4, "MAXIMUM SPEED", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf((char *)text, sizeof(text), "%u.%02u RPM", sB.maxRpm / 100, sB.maxRpm % 100);
    {
        u8 line[32];

        UiText_FromAscii(line, sizeof(line), (const char *)text);
        UiText_Draw(UI_FONT_NORMAL, BOARD_X + BOARD_W - 20, y, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        UiCaps_Draw(BOARD_X + 20, y + 28, "TIME", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        snprintf((char *)text, sizeof(text), "%02u MIN. %02u SEC.", sB.frames / 3600, (sB.frames / 60) % 60);
        UiText_FromAscii(line, sizeof(line), (const char *)text);
        UiText_Draw(UI_FONT_NORMAL, BOARD_X + BOARD_W - 20, y + 24, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh, L, R, B;
    struct UiHintItem h[2];
    int n = 0;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    if (sB.namesShown)
    {
        for (int i = 0; i < NATIVE_BLENDER_PLAYERS; i++)
            if (sB.names[i] != NULL)
                NamePlate(i);
    }
    if (sB.board == NATIVE_BLENDER_BOARD_RANKING)
        Ranking();
    else if (sB.board == NATIVE_BLENDER_BOARD_RESULTS)
        Results();
    if (sB.yesNo)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
    }
    else if (sMsgWaiting || sB.boardWaits)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    if (sHasMsg)
        UiField_MessageBox(t, L, R, B, sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, sMsgWaiting, NULL, h, n, UI_P_FIELD_INK,
                           HIT_SCREEN_ALL);
    else if (n > 0)
        UiW_Hints(R - 8 - UiW_HintsWidth(h, n, 10), B - 28, h, n, true, 10);
    if (sB.yesNo)
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sB.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Hit(HIT_SCREEN_ALL, 0, bx, by, bw, bh);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sB.playing || sB.yesNo || sB.boardWaits || sMsgWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending() && !sB.playing)
        return true;
    if (button == 3)
    {
        if (sB.yesNo)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sB.yesNo)
    {
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            int moves = hit->index - sB.yesNoCursor;

            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    }
    /* the press at the arrow while it blends; else a board or a line waiting */
    if (sB.playing || sB.boardWaits || sMsgWaiting)
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !sB.yesNo)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the blender stays (BG1, BG2, the sprites); the game's windows (BG0) give way */
    return UI_PICTURE_HIDE_BG0;
}

/* the ranking's score symbols (the game's sprites over its window) go with the window */
static void HideObjects(uint32_t mask[4])
{
    const struct OamData *oam = (const struct OamData *)OAM;

    for (int k = 0; k < 3; k++)
    {
        u8 id = sB.scoreIcons[k];
        u16 tile;

        if (id >= MAX_SPRITES || !gSprites[id].inUse)
            continue;
        tile = gSprites[id].oam.tileNum;
        for (int i = 0; i < 128; i++)
            if (oam[i].tileNum == tile)
                mask[i >> 5] |= 1u << (i & 31);
    }
}

const struct UiScreen gUiScreen_BerryBlender = {
    .name = "berry_blender",
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
