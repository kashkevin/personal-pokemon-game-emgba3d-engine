#ifdef ENABLE_NATIVE_UI
/*
 * The cable car's ride between Route 112 and Mt. Chimney (cable_car.c; step 15c, the design's pieces): the ride is
 * the game's own scene - the car on its cable, the mountain, the trees and the ash going by - with no line of text;
 * the design's wooden location sign (the pause menu's, the map name pop-up's: UiW_LocationSign) names where it goes,
 * sliding down at the top left half a second into the ride as the map name pop-up does. Project-owned.
 */
#include "global.h"
#include "main.h"
#include "region_map.h"
#include "constants/characters.h"
#include "constants/region_map_sections.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_widgets.h"

static u8 sName[40];

static bool Detect(void)
{
    bool8 goingDown;

    return CableCar_NativeUiState(&goingDown);
}

static void Capture(void)
{
    bool8 goingDown = FALSE;

    CableCar_NativeUiState(&goingDown);
    GetMapName(sName, goingDown ? MAPSEC_ROUTE_112 : MAPSEC_MT_CHIMNEY, 0);
}

static void Draw(int t)
{
    int bx, by, bw, bh, y;
    double k;

    UiG_Bleed(&bx, &by, &bw, &bh);
    /* in from above the window's top edge over 20 frames, half a second into the ride */
    k = t < 30 ? 0.0 : (t - 30) / 20.0;
    if (k > 1.0)
        k = 1.0;
    y = by + 8 - (int)((UI_LOCATION_SIGN_H + 10) * (1.0 - k) + 0.5);
    UiW_LocationSign(bx + 8, y, sName, NULL);
    UiG_Veil(NativeUi_ScreenFade());
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_KEEP; /* the ride is all the game's scene */
}

const struct UiScreen gUiScreen_CableCar = {
    .name = "cable_car",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
