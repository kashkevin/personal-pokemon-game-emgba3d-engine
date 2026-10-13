#ifdef ENABLE_NATIVE_UI
/*
 * 27 · Connection notices (the approved design's preview/js/screens/
 * connection.js, its toast banner): the game's multiplayer notice banner
 * (multiplayer_world.c: the host's notices, money from wagers, this ROM's own
 * hints) drawn as the design's toast over the field, sliding in from the right
 * and out again, for exactly as long as the game shows its banner. Only while
 * the field is calm (as the game itself waits for); when a script or menu takes
 * the screen, the game's own banner shows. The design's recovery dialogs (host
 * lost, saved here, rejoin, mismatches) belong to the launcher, not the game.
 * Project-owned.
 */
#include <string.h>

#include "global.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define TOAST_W 300
#define TOAST_SLIDE 6

static const double sSlide[TOAST_SLIDE] = {0, 0.45, 0.75, 0.9, 0.97, 1};

static u8 sText[160];
static u16 sShown;
static u16 sTotal;

static bool Detect(void)
{
    u8 window;
    u16 shown, total;

    return gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked()
        && MultiplayerWorld_NativeUiNotice(&window, &shown, &total);
}

static void Capture(void)
{
    u8 window;
    u32 printed;
    bool8 active;

    if (!MultiplayerWorld_NativeUiNotice(&window, &sShown, &sTotal)
     || !TextPrinter_NativeUiWindowText(window, sText, sizeof(sText), &printed, &active))
        sText[0] = EOS;
}

static void Draw(int t)
{
    struct UiRamp f = UiW_FieldBand();
    int lines = 1, x, y = 8, h, in, out, frame;
    double k;
    const u8 *starts[3];
    int lengths[3];

    (void)t;
    if (sText[0] == EOS)
        return;
    lines = UiText_Wrap(sText, TOAST_W - 40, UI_FONT_SMALL, starts, lengths, 3);
    if (lines > 2)
        lines = 2;
    h = 10 + lines * 13;
    /* in over the first frames, out over the last ones */
    in = sShown;
    out = sTotal > sShown ? sTotal - sShown : 0;
    frame = in < out ? in : out;
    k = sSlide[frame >= TOAST_SLIDE ? TOAST_SLIDE - 1 : frame];
    x = 632 - TOAST_W + (int)((TOAST_W + 12) * (1.0 - k) + 0.5);
    UiG_Paint(UiM_Rounded4(TOAST_W, h, 3, 0, 3, 0), x, y, UI_P_CREAM, &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Rect(x + 1, y + 1, 4, h - 2, f.base);
    UiG_VLine(x + 5, y + 1, h - 2, UiMix(f.base, UI_P_INK, 0.4));
    /* the link lamp */
    UiG_Paint(UiM_Circle(9), x + 11, y + (h >> 1) - 4, UiMix(f.base, UI_HEX(0xFFFFFF), 0.2),
              &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, f.lo, 0, 0, NULL, UI_NONE});
    UiG_Put(x + 13, y + (h >> 1) - 2, UI_HEX(0xFFFFFF));
    for (int i = 0; i < lines; i++)
    {
        u8 line[96];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, x + 26, y + 4 + i * 13, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_BG0; /* the game's banner; the field stays */
}

const struct UiScreen gUiScreen_Notice = {
    .name = "notice",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
