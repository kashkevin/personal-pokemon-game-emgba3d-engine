#ifdef ENABLE_NATIVE_UI
/*
 * The new game's introduction (KNOWN_ISSUES MS-250): Professor Birch's speech at a new game - its message box, the
 * BOY / GIRL menu and the name's YES / NO - drawn as the field's message box and menus (ui_field_text.c's pieces) over
 * the game's picture, the game's own windows (BG0) hidden; Birch, his Pokemon and the player stay the game's. The
 * whole-game walk's audit found the speech the one screen of a new game still in the game's own windows. Every
 * button stays the game's: a click presses A, or moves the menu's cursor and presses A, as the game reads them.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "menu.h"
#include "palette.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
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

static struct NativeScriptMenu sMenu;
static u8 sMsg[UI_MESSAGE_COPY];
static u32 sMsgPrinted;
static bool sHasMsg, sMsgPrinting, sWaiting;

static bool MenuUp(void)
{
    return sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO || (sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sMenu.count > 0);
}

/* the speech with its message box up (window 0: the box the game prints Birch's lines in) */
static bool Detect(void)
{
    struct NativeScriptMenu m;

    return MainMenu_NativeUiIntroState(&m) && Window_NativeUiShown(0);
}

static void Capture(void)
{
    bool8 active = FALSE;

    MainMenu_NativeUiIntroState(&sMenu);
    sHasMsg = TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = !MenuUp() && TextPrinter_NativeUiWindowWaiting(0);
}

static void Draw(int t)
{
    struct UiRamp band = UiW_FieldBand();
    struct UiHintItem h[2];
    int n = 0, bx, by, bw, bh, R, B;

    UiG_Bleed(&bx, &by, &bw, &bh);
    R = bx + bw;
    B = by + bh;
    if (MenuUp() && sMenu.ready)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
    }
    else if (sWaiting)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    UiField_MessageBox(t, bx, R, B, sHasMsg ? sMsg : NULL, sMsgPrinting ? (int)sMsgPrinted : -1, sWaiting, &band, h, n,
                       band.ink, HIT_MESSAGE);
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
        /* B: the YES / NO's NO (the BOY / GIRL menu takes none) */
        if (MenuUp() && sMenu.ready && sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
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

static enum UiPicture Picture(void)
{
    /* Birch, his Pokemon and the player stay; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_Intro = {
    .name = "intro",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
