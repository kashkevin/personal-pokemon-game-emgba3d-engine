#ifdef ENABLE_NATIVE_UI
/*
 * The title screen's clear-save question (clear_save_data_screen.c: UP,
 * SELECT and B held at the title; not one of the 31 designed screens: the
 * design's pieces): a warning card with the game's line and its YES / NO,
 * the cursor starting on NO as the game's does, then "Clearing data..." if
 * YES. The game keeps the rule and does the clearing. A click on YES only
 * moves the cursor there; a second click (or A) confirms, so one stray click
 * can't clear the save. Project-owned.
 */
#include "global.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define CARD_X 120
#define CARD_Y 84
#define CARD_W 400
#define CARD_H 150

static const u8 sText_OK[] = _("OK");
static const u8 sText_Cancel[] = _("Cancel");

static u8 sPhase, sCursor;
static u8 sLine[UI_MESSAGE_COPY];
static bool sHasLine;

static bool Detect(void)
{
    u8 phase, cursor;

    return ClearSaveData_NativeUiState(&phase, &cursor);
}

static void Capture(void)
{
    u32 printed;
    bool8 active;

    ClearSaveData_NativeUiState(&sPhase, &sCursor);
    sHasLine = TextPrinter_NativeUiWindowText(0, sLine, sizeof(sLine), &printed, &active);
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    struct UiRamp warn = UiRampDefault(UI_HEX(0xD84838));

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x201418));
    UiG_Paint(UiM_Rounded4(CARD_W, CARD_H, 8, 8, 8, 8), CARD_X, CARD_Y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x100808), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(CARD_W - 16, 30, 5, 5, 5, 5), CARD_X + 8, CARD_Y + 8, warn.base,
              &(struct UiPaint){warn.ink, warn.hi, warn.lo, 0, 0, NULL, UI_NONE});
    UiItems_BigCaps(CARD_X + CARD_W / 2, CARD_Y + 15, "SAVE DATA", 2, UI_P_TEXT_W, warn.ink, UI_ALIGN_CENTER);
    /* the warning sign */
    UiG_Mask(UiM_Ellipse(40, 40), CARD_X + 24, CARD_Y + 58, warn.base);
    UiItems_BigCaps(CARD_X + 44, CARD_Y + 66, "!", 3, UI_P_TEXT_W, warn.ink, UI_ALIGN_CENTER);
    if (sHasLine)
        UiW_MessagePage(sLine, -1, CARD_X + 80, CARD_Y + 60, CARD_W - 180, 2);
    if (sPhase == NATIVE_CLEAR_SAVE_ASKING)
    {
        struct UiHintItem h[2] = {{UI_ACT_CONFIRM, sText_OK}, {UI_ACT_CANCEL, sText_Cancel}};

        UiW_YesNo(CARD_X + CARD_W - 16, CARD_Y + CARD_H - 14, sCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, 2, 10), by + bh - 22, h, 2, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sPhase == NATIVE_CLEAR_SAVE_ASKING;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (sPhase != NATIVE_CLEAR_SAVE_ASKING || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || hit->id != UI_HIT_YESNO)
        return false;
    if (hit->index == 0 && sCursor != 0)
        NativeUi_QueuePress(DPAD_UP);       /* YES: the cursor goes there; confirming is a second click */
    else if (hit->index == 1 && sCursor != 1)
    {
        NativeUi_QueuePress(DPAD_DOWN);
        NativeUi_QueuePress(A_BUTTON);
    }
    else
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_ClearSave = {
    .name = "clear_save",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
