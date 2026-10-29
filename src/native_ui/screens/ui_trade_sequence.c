#ifdef ENABLE_NATIVE_UI
/*
 * A trade's sequence (not one of the 31 designed screens; the design's message
 * box): an in-game trade's (trade.c) or a link partner's. The sequence itself
 * stays the game's picture - the Pokémon sent off in its ball, the link, the
 * new Pokémon arriving - and its lines ("X will be sent to Y.", "Bye-bye, X!",
 * "Y sent over X.", "Take good care of X!") are the field's message box at the
 * bottom, fading with the game's own fades. An in-game trade's end waits for A
 * as the game does (the down arrow and Continue; a click is that A). The
 * evolution a trade can start is the evolution screen's. Project-owned.
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

static struct NativeTradeSequence sT;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;

static bool Detect(void)
{
    struct NativeTradeSequence t;

    return Trade_NativeUiSequence(&t);
}

static void Capture(void)
{
    bool8 active = FALSE;

    Trade_NativeUiSequence(&sT);
    sHasMsg = sT.message && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
}

static void Draw(int t)
{
    UiRGBA fade = NativeUi_ScreenFade();
    int bx, by, bw, bh;

    if (!sHasMsg)
        return;
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Save();
    /* the game's picture fades by itself; the box fades with it */
    UiG_Alpha(1.0 - UI_A(fade) / 255.0);
    if (sT.waitingA)
    {
        struct UiHintItem h = {UI_ACT_CONFIRM, sText_Continue};

        UiField_MessageBox(t, bx, bx + bw, by + bh, sMsg, -1, true, NULL, &h, 1, UI_P_FIELD_INK, HIT_BOX);
        UiG_Hit(HIT_BOX, 0, bx, by, bw, bh);
    }
    else
    {
        UiField_MessageBox(t, bx, bx + bw, by + bh, sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, false, NULL, NULL, 0,
                           UI_P_FIELD_INK, 0);
    }
    UiG_Restore();
}

static bool Ready(void)
{
    return sT.waitingA;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (button == 1 && sT.waitingA && !NativeUi_PressesPending())
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the sequence stays; the game's message window (BG0) gives way to the box */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_TradeSequence = {
    .name = "trade_sequence",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
