#ifdef ENABLE_NATIVE_UI
/*
 * The three wireless minigames with computer partners (MS-141): Berry Crush, Pokemon Jump and Dodrio Berry Picking
 * (step 15c; not among the 31 designed screens: the design's pieces). Each stays the game's picture - the crusher,
 * the vine, the Dodrio and their Berries, the timers and counters the game draws as sprites - and its windows on BG0
 * are the design's where the game puts them: a window showing one player's name alone is that player's plate (the
 * player's lit), a framed window (the game's frame round it) the design's box with the game's lines as it printed
 * them, in its colours (TextPrinter_NativeUiTranscript) and places, an unframed one its lines alone, and the game's
 * sprites over a window (Dodrio Berry Picking's Berries over their columns) are drawn again over the design's box;
 * Berry Crush's message box is the field's, and "play again?" takes the design's YES / NO. In play a click is A (a
 * press of the crusher, a jump). Project-owned.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "bg.h"
#include "menu.h"
#include "palette.h"
#include "string_util.h"
#include "text.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_SCREEN_ALL = UI_HIT_SCREEN,
};

/* the GBA picture's place on the screen: its tiles at twice their size */
#define PIC_X 80
#define PIC_Y 20
#define TILE 16

static const u8 sText_Next[] = _("Next");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");

static struct NativeMinigame sM;
static u8 sMsg[UI_MESSAGE_COPY];
static u32 sMsgPrinted;
static bool sHasMsg, sMsgPrinting, sMsgWaiting;

/* ---------------------------------------------------------------- state */

static bool State(struct NativeMinigame *m)
{
    return BerryCrush_NativeUiState(m) || PokemonJump_NativeUiState(m) || DodrioBerryPicking_NativeUiState(m);
}

static bool Detect(void)
{
    struct NativeMinigame m;

    return State(&m);
}

static void Capture(void)
{
    const struct NativeUiPrinted *p;
    bool8 active = FALSE;

    State(&sM);
    sHasMsg = sM.msgWindow != NATIVE_MINIGAME_NO_WINDOW && Window_NativeUiOnScreen(sM.msgWindow)
           && TextPrinter_NativeUiTranscript(sM.msgWindow, &p) > 0
           && TextPrinter_NativeUiWindowText(sM.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(sM.msgWindow);
}

/* ---------------------------------------------------------------- drawing */

/* a colour of a window's palette (BGR555) as the design's */
static UiRGBA PaletteColor(u8 paletteNum, u8 index)
{
    u16 c = gPlttBufferUnfaded[BG_PLTT_ID(paletteNum & 15) + (index & 15)];

    return UI_RGB((c & 0x1F) * 255 / 31, ((c >> 5) & 0x1F) * 255 / 31, ((c >> 10) & 0x1F) * 255 / 31);
}

static int UiFont(u8 fontId)
{
    switch (fontId)
    {
    case FONT_SMALL:
        return UI_FONT_SMALL;
    case FONT_SHORT:
        return UI_FONT_SHORT;
    case FONT_NARROW:
        return UI_FONT_NARROW;
    default:
        return UI_FONT_NORMAL;
    }
}

/* the player whose name a window shows alone (its plate; the game may print it again over itself, as Berry Crush
 * does after its boards), or -1 */
static int PlateOf(u8 windowId)
{
    const struct NativeUiPrinted *p;
    u32 n = TextPrinter_NativeUiTranscript(windowId, &p);

    if (n == 0)
        return -1;
    for (u32 k = 1; k < n; k++)
        if (StringCompare(p[k].text, p[0].text) != 0)
            return -1;
    for (int i = 0; i < sM.numPlayers; i++)
        if (sM.names[i] != NULL && StringCompare(p[0].text, sM.names[i]) == 0)
            return i;
    return -1;
}

/* whether the game drew its frame round a window (BG0's tile just outside its top left corner) */
static bool Framed(const struct WindowTemplate *w)
{
    const u16 *map = (const u16 *)GetBgTilemapBuffer(0);

    if (map == NULL || w->tilemapLeft == 0 || w->tilemapTop == 0)
        return false;
    return (map[(w->tilemapTop - 1) * 32 + (w->tilemapLeft - 1)] & 0x3FF) != 0;
}

/* a window's lines as the game printed them, where it printed them, in its colours; a line break starts a line, and
 * the game's own place codes keep their places ({CLEAR_TO} and SKIP: from where the printer started; SHIFT_RIGHT: on
 * from the text so far), as Dodrio Berry Picking's points over its Berries */
static void Lines(u8 windowId, const struct WindowTemplate *w)
{
    const struct NativeUiPrinted *p;
    u32 n = TextPrinter_NativeUiTranscript(windowId, &p);

    for (u32 k = 0; k < n; k++)
    {
        u8 seg[sizeof(p[k].text)];
        UiRGBA fg = PaletteColor(w->paletteNum, p[k].fgColor), sh = PaletteColor(w->paletteNum, p[k].shadowColor);
        int font = UiFont(p[k].fontId);
        int x0 = PIC_X + (w->tilemapLeft * 8 + p[k].x) * 2, x = x0, y = PIC_Y + (w->tilemapTop * 8 + p[k].y) * 2;
        u32 i = 0, len = 0;

        /* the menus' selector arrows are the design's (the YES / NO), never a board's line */
        if (p[k].text[0] == CHAR_RIGHT_ARROW || p[k].text[0] == CHAR_BLACK_TRIANGLE)
            continue;
        for (;;)
        {
            u8 c = p[k].text[i];

            if (c == EOS || c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR)
            {
                seg[len] = EOS;
                if (len > 0)
                    UiText_Draw(font, x, y, seg, fg, sh, UI_ALIGN_LEFT, 0);
                if (c == EOS)
                    break;
                i++;
                x = x0;
                y += 32;
                len = 0;
                continue;
            }
            if (c == EXT_CTRL_CODE_BEGIN && p[k].text[i + 1] != EOS)
            {
                u8 code = p[k].text[i + 1], arg = p[k].text[i + 2];
                int size = 1 + (GetExtCtrlCodeLength(code) > 0 ? GetExtCtrlCodeLength(code) : 1);

                if ((code == EXT_CTRL_CODE_CLEAR_TO || code == EXT_CTRL_CODE_SKIP || code == EXT_CTRL_CODE_SHIFT_RIGHT)
                 && arg != EOS)
                {
                    int width;

                    seg[len] = EOS;
                    width = len > 0 ? UiText_Width(font, seg, 0) : 0;
                    if (len > 0)
                        UiText_Draw(font, x, y, seg, fg, sh, UI_ALIGN_LEFT, 0);
                    x = code == EXT_CTRL_CODE_SHIFT_RIGHT ? x + width + arg * 2 : x0 + arg * 2;
                    len = 0;
                    i += 3;
                    continue;
                }
                /* any other code goes with the text (the drawer reads its colours and skips the rest) */
                for (int b = 0; b < size && p[k].text[i] != EOS; b++)
                {
                    if (len < sizeof(seg) - 1)
                        seg[len++] = p[k].text[i];
                    i++;
                }
                continue;
            }
            if (len < sizeof(seg) - 1)
                seg[len++] = c;
            i++;
        }
    }
}

/* a player's name plate where the game's window is */
static void Plate(const struct WindowTemplate *w, int player)
{
    struct UiRamp r = UiRampDefault(player == sM.ownId ? UI_HEX(0xE84838) : UI_HEX(0x5878B8));
    int x = PIC_X + w->tilemapLeft * TILE, y = PIC_Y + w->tilemapTop * TILE, wd = w->width * TILE, h = w->height * TILE;

    UiG_Paint(UiM_Rounded4(wd - 4, h - 4, 4, 4, 4, 4), x + 2, y + 2, r.base,
              &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + wd / 2, y + (h - 4) / 2 - 8, sM.names[player], UI_P_TEXT_W, r.ink, UI_ALIGN_CENTER, 0);
}

static void Windows(void)
{
    for (u32 id = 0; id < WINDOWS_MAX; id++)
    {
        const struct WindowTemplate *w = &gWindows[id].window;
        int player;

        if (gWindows[id].tileData == NULL || w->bg != 0 || !Window_NativeUiOnScreen(id)
         || id == sM.msgWindow || (sM.yesNo && id == sM.yesNoWindow))
            continue;
        player = PlateOf(id);
        if (player >= 0)
        {
            Plate(w, player);
            continue;
        }
        if (Framed(w))
        {
            int inner[4];

            UiW_DialogBox(PIC_X + w->tilemapLeft * TILE - 12, PIC_Y + w->tilemapTop * TILE - 12,
                          w->width * TILE + 24, w->height * TILE + 24, NULL, UI_NONE, inner);
        }
        Lines(id, w);
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh, R, B;
    struct UiHintItem h[2];
    int n = 0;

    UiG_Bleed(&bx, &by, &bw, &bh);
    R = bx + bw;
    B = by + bh;
    Windows();
    /* the game's sprites over its windows (Dodrio Berry Picking's Berries over their columns), over the design's box */
    for (int i = 0; i < sM.iconCount; i++)
        UiGfx_FieldSpriteSized((const u8 *)(uintptr_t)(OBJ_VRAM0 + sM.icons[i].tileNum * TILE_SIZE_4BPP), 1,
                               &gPlttBufferUnfaded[OBJ_PLTT_ID(sM.icons[i].paletteNum & 15)], 0, 16, 16,
                               PIC_X + sM.icons[i].x * 2, PIC_Y + sM.icons[i].y * 2, 2, false);
    if (sM.yesNo)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
    }
    else if (sMsgWaiting)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    if (sHasMsg)
        UiField_MessageBox(t, bx, R, B, sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, sMsgWaiting, NULL, h, n, UI_P_FIELD_INK,
                           HIT_SCREEN_ALL);
    else if (n > 0)
        UiW_Hints(R - 8 - UiW_HintsWidth(h, n, 10), B - 28, h, n, true, 10);
    if (sM.yesNo)
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sM.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Hit(HIT_SCREEN_ALL, 0, bx, by, bw, bh);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sM.playing || sM.yesNo || sMsgWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending() && !sM.playing)
        return true;
    if (button == 3)
    {
        if (sM.yesNo)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sM.yesNo)
    {
        if (hit->id == UI_HIT_YESNO && hit->index >= 0 && hit->index < 2)
        {
            int moves = hit->index - sM.yesNoCursor;

            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    }
    /* a press of the crusher, a jump; else a line waiting */
    if ((sM.playing && sM.kind != NATIVE_MINIGAME_DODRIO) || sMsgWaiting)
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !sM.yesNo)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the game's scene stays (BG1 to BG3, the sprites); its windows (BG0) give way */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_Minigame = {
    .name = "minigame",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
