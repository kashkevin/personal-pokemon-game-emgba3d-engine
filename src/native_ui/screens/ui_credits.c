#ifdef ENABLE_NATIVE_UI
/*
 * The credits (not one of the 31 designed screens; the design's own pieces):
 * the pages of names (credits.c) printed over the game's own ride - the bike
 * along the coast and the Pokémon interludes stay the game's picture - in the
 * game's font twice its size, the titles in the game's red-shadowed grey and
 * the names in white, on a soft band so they read over the sky, fading in and
 * out with the game's own fade of each page. THE END keeps the game's picture.
 * Nothing to press. Project-owned.
 */
#include <string.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"

/* the GBA's screen as the canvas shows it under a screen that keeps its picture: twice its size, in the middle */
#define SCALE 2
#define GBA_LEFT 80
#define GBA_TOP 20
#define GBA_TO_Y(y) (GBA_TOP + (y) * SCALE)

static struct NativeCredits sC;

static bool Detect(void)
{
    struct NativeCredits c;

    return Credits_NativeUiState(&c);
}

static void Capture(void)
{
    Credits_NativeUiState(&sC);
}

static void Draw(int t)
{
    double a = sC.shown / 16.0;
    int top, bottom;

    (void)t;
    if (sC.count == 0 || sC.shown == 0)
        return;
    top = GBA_TO_Y(sC.y[0]) - 10;
    bottom = GBA_TO_Y(sC.y[sC.count - 1]) + 16 * SCALE + 6;
    UiG_Save();
    UiG_Alpha(a);
    /* a soft band behind the page, so it reads over the sky */
    UiG_Rect(GBA_LEFT, top, DISPLAY_WIDTH * SCALE, bottom - top, UiWithAlpha(UI_HEX(0x081018), 64));
    for (int i = 0; i < sC.count; i++)
    {
        const u8 *s = sC.lines[i];
        int w, y = GBA_TO_Y(sC.y[i]);

        if (s == NULL)
            continue;
        w = UiText_Width(UI_FONT_NORMAL, s, 0) * SCALE;
        if (sC.titles[i])
            UiText_DrawScaled(UI_FONT_NORMAL, 320 - w / 2, y, s, UI_HEX(0xE0E0E0), UI_HEX(0xD04848), SCALE);
        else
            UiText_DrawScaled(UI_FONT_NORMAL, 320 - w / 2, y, s, UI_HEX(0xFFFFFF), UI_HEX(0x505058), SCALE);
    }
    UiG_Restore();
}

static enum UiPicture Picture(void)
{
    /* the ride stays; the game's page (BG0) gives way to this */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_Credits = {
    .name = "credits",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
