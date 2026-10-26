#ifdef ENABLE_NATIVE_UI
/*
 * A PokeNav call on the field (match_call.c: a registered trainer, Mr. Stone,
 * May / Brendan calling as the player walks) in the design's message box with a
 * PokeNav tab - its little device ringing - instead of the game's own call box,
 * sliding up from the window's bottom edge and back down with it (BG0's offset).
 * The game keeps the call: its lines, their speed, A or B to hang up (a click
 * hangs up too once the line is printed). The owner's request of 2026-10-26:
 * every dialogue box in the current interface. Project-owned.
 */
#include <math.h>

#include "global.h"
#include "field_name_box.h"
#include "main.h"
#include "overworld.h"
#include "string_util.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define TAB_H 18
#define GBA_BOX_DROP 32     /* the game's box is this far below its place when its slide begins */

enum
{
    HIT_CALL = UI_HIT_SCREEN,
};

static const u8 sText_HangUp[] = _("Hang up");
static const u8 sText_PokeNav[] = _("POKéNAV");

static u8 sMsg[256];
static u32 sPrinted;
static bool sTalking, sPrinting, sWaiting;
static s16 sOffsetY;
static u8 sSpeaker[40];

static bool Detect(void)
{
    u8 windowId;
    s16 offsetY;
    bool8 talking, waiting;

    return gMain.callback2 == CB2_Overworld && MatchCall_NativeUiState(&windowId, &offsetY, &talking, &waiting);
}

static void Capture(void)
{
    u8 windowId = WINDOW_NONE;
    bool8 talking = FALSE, waiting = FALSE, active = FALSE;

    sOffsetY = 0;
    MatchCall_NativeUiState(&windowId, &sOffsetY, &talking, &waiting);
    sTalking = talking && TextPrinter_NativeUiWindowText(windowId, sMsg, sizeof(sMsg), &sPrinted, &active);
    sPrinting = sTalking && active;
    sWaiting = waiting && !sPrinting;
    sSpeaker[0] = EOS;
    if (GetNameboxWindowId() != WINDOW_NONE && gSpeakerName != NULL)
    {
        u8 name[64];

        StringExpandPlaceholders(name, gSpeakerName);
        StringCopyN(sSpeaker, name, sizeof(sSpeaker) - 1);
        sSpeaker[sizeof(sSpeaker) - 1] = EOS;
    }
}

/* the PokeNav, ringing: a small red device with its screen, waves beside it every other half second */
static void Device(int x, int y, int t, const struct UiRamp *band)
{
    UiG_Paint(UiM_Rounded4(10, 13, 2, 2, 2, 2), x, y, UI_P_CMD_FIGHT,
              &(struct UiPaint){band->ink, UI_HEX(0xF89880), UI_P_HP_RED_2, 0, 0, NULL, UI_NONE});
    UiG_Rect(x + 2, y + 2, 6, 5, UI_HEX(0x9CE4F0));
    UiG_Rect(x + 4, y + 9, 2, 2, band->ink);
    if ((t / 15) % 2 == 0 && !sTalking)
    {
        UiG_Rect(x + 12, y + 4, 1, 5, band->ink);
        UiG_Rect(x + 14, y + 2, 1, 9, band->ink);
    }
}

static void Draw(int t)
{
    struct UiRamp band = UiRampOf(UI_HEX(0xF07838), 0.38, 0.28, 0.62);
    struct UiHintItem hint = {UI_ACT_CONFIRM, sText_HangUp};
    int bx, by, bw, bh, L, B, tabW, tabY, drop;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    B = by + bh;
    /* below its place as the game's box is: its 32 pixels' drop hides the whole box and tab */
    drop = sOffsetY < 0 ? (int)floor(-sOffsetY * (UI_FIELD_MSG_H + TAB_H + 16) / (double)GBA_BOX_DROP + 0.5) : 0;
    UiG_Save();
    UiG_Translate(0, drop);
    /* the tab: the device and POKéNAV, with the caller's name when the game shows it */
    tabW = UiText_Width(UI_FONT_NORMAL, sText_PokeNav, 0) + 38;
    if (sSpeaker[0] != EOS)
        tabW += UiText_Width(UI_FONT_NORMAL, sSpeaker, 0) + 14;
    tabY = B - UI_FIELD_MSG_H - 8 - TAB_H + 3;
    UiG_Paint(UiM_Rounded4(tabW, TAB_H, 3, 3, 0, 0), L + 16, tabY, band.base,
              &(struct UiPaint){band.ink, band.hi, band.lo, 0, 0, NULL, UI_NONE});
    Device(L + 22, tabY + 3, t, &band);
    UiText_Draw(UI_FONT_NORMAL, L + 40, tabY + 1, sText_PokeNav, UI_P_TEXT_W, band.ink, UI_ALIGN_LEFT, 0);
    if (sSpeaker[0] != EOS)
        UiText_Draw(UI_FONT_NORMAL, L + 40 + UiText_Width(UI_FONT_NORMAL, sText_PokeNav, 0) + 12, tabY + 1, sSpeaker,
                    UI_P_TEXT_W, band.ink, UI_ALIGN_LEFT, 0);
    UiField_MessageBox(t, L, bx + bw, B, sTalking ? sMsg : NULL, sPrinting ? (int)sPrinted : -1, sWaiting, &band,
                       &hint, sWaiting ? 1 : 0, band.ink, HIT_CALL);
    UiG_Restore();
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if ((button == 1 && hit->id == HIT_CALL) || button == 3)
    {
        if (sWaiting)
            NativeUi_QueuePress(button == 3 ? B_BUTTON : A_BUTTON);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_BG0; /* the game's call box and name box; the field stays */
}

const struct UiScreen gUiScreen_MatchCall = {
    .name = "match_call",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
