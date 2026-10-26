#ifdef ENABLE_NATIVE_UI
/*
 * The location pop-up: the field's map name, which the game slides in at the
 * top left on a map change (map_name_popup.c: half a second's wait, in over
 * 20 frames, two seconds shown, out over 20), drawn as the design's wooden
 * location sign - the pause menu's (UiW_LocationSign) - sliding down from the
 * window's top edge and back up with the game's own pop-up, which is hidden
 * meanwhile (BG0). Only over the field (CB2_Overworld); a message or a menu
 * that takes the screen hides it as it hides the game's. The owner's request
 * of 2026-10-26: no old GBA boxes left. Project-owned.
 */
#include "global.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_widgets.h"

static u8 sName[40];
static u16 sOffset;
static u16 sOffscreen = 1;

static bool Detect(void)
{
    u16 offset, offscreen;
    u8 name[8], popup;

    if (gMain.callback2 != CB2_Overworld || !MapNamePopUp_NativeUiState(name, sizeof(name), &offset, &offscreen))
        return false;
    /* the window layer goes: nothing else of the game's may be on it (a call, a list) */
    popup = GetMapNamePopUpWindowId();
    for (u32 i = 0; i < WINDOWS_MAX; i++)
        if (i != popup && Window_NativeUiShown(i))
            return false;
    return true;
}

static void Capture(void)
{
    if (!MapNamePopUp_NativeUiState(sName, sizeof(sName), &sOffset, &sOffscreen) || sOffscreen == 0)
    {
        sName[0] = EOS;
        sOffscreen = 1;
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh, y;
    double k;

    (void)t;
    if (sName[0] == EOS)
        return;
    UiG_Bleed(&bx, &by, &bw, &bh);
    /* in place at the pause menu's sign, above the window's top edge when the game's pop-up is off screen */
    k = (double)sOffset / sOffscreen;
    y = by + 8 - (int)((UI_LOCATION_SIGN_H + 10) * (k > 1.0 ? 1.0 : k) + 0.5);
    UiW_LocationSign(bx + 8, y, sName, NULL);
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_BG0; /* the game's pop-up; the field stays */
}

const struct UiScreen gUiScreen_MapName = {
    .name = "map_name",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
