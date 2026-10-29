#ifdef ENABLE_NATIVE_UI
/*
 * A Pokémon eating a Pokéblock (not one of the 31 designed screens; the
 * design's message box): the scene (pokeblock_feed.c) stays the game's picture
 * - the case thrown, the block caught, the Pokémon's own animation - and its
 * line ("X happily ate the POKéBLOCK." and the others) is the field's message
 * box at the bottom, with the down arrow while the game waits for A (a click is
 * that A). Project-owned.
 */
#include <string.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_BOX = UI_HIT_SCREEN,
};

static const u8 sText_Continue[] = _("Continue");

static bool8 sMessage;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg, sMsgPrinting, sWaiting;
static u32 sMsgPrinted;

static bool Detect(void)
{
    bool8 message;

    return PokeblockFeed_NativeUiState(&message);
}

static void Capture(void)
{
    bool8 active = FALSE;

    PokeblockFeed_NativeUiState(&sMessage);
    sHasMsg = sMessage && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(0);
}

static void Draw(int t)
{
    UiRGBA fade = NativeUi_ScreenFade();
    struct UiHintItem h = {UI_ACT_CONFIRM, sText_Continue};
    int bx, by, bw, bh;

    if (!sHasMsg)
        return;
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Save();
    /* the game's picture fades by itself; the box fades with it */
    UiG_Alpha(1.0 - UI_A(fade) / 255.0);
    UiField_MessageBox(t, bx, bx + bw, by + bh, sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, sWaiting, NULL,
                       sWaiting ? &h : NULL, sWaiting ? 1 : 0, UI_P_FIELD_INK, HIT_BOX);
    if (sWaiting)
        UiG_Hit(HIT_BOX, 0, bx, by, bw, bh);
    UiG_Restore();
}

static bool Ready(void)
{
    return sWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (button == 1 && sWaiting && !NativeUi_PressesPending())
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the scene stays; the game's message window (BG0) gives way to the box */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_PokeblockFeed = {
    .name = "pokeblock_feed",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
