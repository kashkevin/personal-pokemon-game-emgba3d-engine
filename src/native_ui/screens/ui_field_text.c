#ifdef ENABLE_NATIVE_UI
/*
 * The field's messages (not one of the 31 designed screens; the design's own
 * pieces): the field's message box - a person's line, a sign's, the name box's
 * speaker - with the script's YES / NO and the multichoices whose rows the
 * engine lists, in the design's message box (W.dialogBox, the Emerald message
 * box; a sign's in the design's wood) and menu box over the field, where the
 * pause menu keeps its help box. The 3D view shows the game's own message box
 * only while a line types (docs U12); this keeps every line up until the
 * script closes it. The engine keeps every line, rule and button: clicks and
 * the wheel queue its presses. Anything else on the field's window layer that
 * this does not draw (a money box, a list, a PokéNav call, rows a script
 * builds itself) keeps the game's picture. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "field_name_box.h"
#include "main.h"
#include "match_call.h"
#include "menu.h"
#include "overworld.h"
#include "script.h"
#include "string_util.h"
#include "window.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_MESSAGE = UI_HIT_SCREEN,
};

static const u8 sText_Next[] = _("Next");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");
static const u8 sText_Cancel[] = _("Cancel");

/* the design's wood (pause.js's location sign) for a sign's lines */
static const struct UiRamp sWood = {UI_HEX(0xECC08A), UI_HEX(0xD29C62), UI_HEX(0xA8703C), UI_HEX(0x4A2A12)};

static struct NativeScriptMenu sMenu;
static u8 sMsg[256];
static u32 sMsgPrinted;
static bool sHasMsg;
static bool sMsgPrinting;
static bool sWaiting;
static bool sSign;
static u8 sSpeaker[40];

/* ---------------------------------------------------------------- shared with the PC's first menus */

void UiField_MessageBox(int t, int L, int R, int B, const u8 *msg, int printed, bool waiting,
                        const struct UiRamp *band, const struct UiHintItem *hints, int hintCount, UiRGBA panelInk,
                        int hitId)
{
    int b[4];

    UiW_DialogBox(L + 8, B - UI_FIELD_MSG_H - 8, UI_FIELD_MSG_W, UI_FIELD_MSG_H, band, UI_NONE, b);
    if (msg != NULL)
        UiW_MessagePage(msg, printed, b[0] + 2, b[1] - 1, b[2] - 4, 2);
    if (waiting)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 20 + ((t / 16) % 2), UI_P_CURSOR, false);
    if (hitId != 0)
        UiG_Hit(hitId, 0, L + 8, B - UI_FIELD_MSG_H - 8, UI_FIELD_MSG_W, UI_FIELD_MSG_H);
    if (hintCount > 0)
    {
        int y = B - UI_FIELD_MSG_H - 8;

        UiG_Paint(UiM_Rounded4(160, UI_FIELD_MSG_H, 3, 0, 3, 0), R - 160, y, UI_P_CREAM,
                  &(struct UiPaint){panelInk, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
        UiW_Hints(R - 152, y + 8, hints, hintCount > 2 ? 2 : hintCount, false, 8);
        if (hintCount > 2)
            UiW_Hints(R - 152, y + 26, hints + 2, hintCount - 2, false, 8);
    }
}

/* ---------------------------------------------------------------- detection */

/* the field's windows up besides the message box (the map's name, the name box apart) */
static int OtherWindows(void)
{
    u8 popup = GetMapNamePopUpWindowId();
    u32 namebox = GetNameboxWindowId();
    int n = 0;

    for (u32 i = 1; i < WINDOWS_MAX; i++)
        if (i != popup && i != namebox && Window_NativeUiShown(i))
            n++;
    return n;
}

static bool Detect(void)
{
    struct NativeScriptMenu m;

    if (gMain.callback2 != CB2_Overworld || !Window_NativeUiShown(0) || IsMatchCallTaskActive())
        return false;
    ScriptMenu_NativeUiState(&m);
    /* rows a script builds itself are not listed: the game's picture */
    if (m.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && m.count == 0)
        return false;
    /* the menu's own window, nothing else */
    return OtherWindows() <= (m.kind != NATIVE_SCRIPT_MENU_NONE ? 1 : 0);
}

static void Capture(void)
{
    bool8 active = FALSE;

    ScriptMenu_NativeUiState(&sMenu);
    sHasMsg = TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = sMenu.kind == NATIVE_SCRIPT_MENU_NONE && (Script_NativeUiWaitsForButton() || TextPrinter_NativeUiWindowWaiting(0));
    sSign = gMsgIsSignPost;
    sSpeaker[0] = EOS;
    if (GetNameboxWindowId() != WINDOW_NONE && gSpeakerName != NULL)
    {
        u8 name[64];

        StringExpandPlaceholders(name, gSpeakerName);
        StringCopyN(sSpeaker, name, sizeof(sSpeaker) - 1);
        sSpeaker[sizeof(sSpeaker) - 1] = EOS;
    }
}

/* ---------------------------------------------------------------- drawing */

static bool MenuUp(void)
{
    return sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO || (sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sMenu.count > 0);
}

static void Draw(int t)
{
    struct UiRamp field = UiW_FieldBand();
    const struct UiRamp *band = sSign ? &sWood : &field;
    struct UiHintItem h[2];
    int n = 0, bx, by, bw, bh, L, R, B;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    if (MenuUp() && sMenu.ready)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        else if (!sMenu.ignoreB)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
    }
    else if (sWaiting)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    if (sSpeaker[0] != EOS)
    {
        /* the name box: the speaker on a plate over the box */
        int w = UiText_Width(UI_FONT_NORMAL, sSpeaker, 0) + 16, y = B - UI_FIELD_MSG_H - 8 - 15;

        UiG_Paint(UiM_Rounded4(w, 18, 3, 3, 0, 0), L + 16, y, band->base,
                  &(struct UiPaint){band->ink, band->hi, band->lo, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NORMAL, L + 24, y + 1, sSpeaker, UI_P_TEXT_W, band->ink, UI_ALIGN_LEFT, 0);
    }
    UiField_MessageBox(t, L, R, B, sHasMsg ? sMsg : NULL, sMsgPrinting ? (int)sMsgPrinted : -1, sWaiting, band, h, n,
                       band->ink, HIT_MESSAGE);
    if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
    {
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sMenu.cursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (MenuUp())
    {
        struct UiMenuItem items[NATIVE_SCRIPT_MENU_MAX];
        struct UiMenuOpt o = {0};

        for (int i = 0; i < sMenu.count; i++)
            items[i] = (struct UiMenuItem){sMenu.labels[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 96;
        UiW_MenuBox(R - 8, B - UI_FIELD_MENU_GAP, items, sMenu.count, sMenu.cursor, &o, NULL);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sWaiting || (MenuUp() && sMenu.ready);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sWaiting || (MenuUp() && sMenu.ready && (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO || !sMenu.ignoreB)))
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (MenuUp() && sMenu.ready && (hit->id == UI_HIT_MENU || hit->id == UI_HIT_YESNO) && hit->index >= 0
     && hit->index < (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO ? 2 : sMenu.count))
    {
        int moves = hit->index - sMenu.cursor;

        NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (hit->id == HIT_MESSAGE && sWaiting)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !MenuUp() || !sMenu.ready)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_FieldText = {
    .name = "field_text",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
