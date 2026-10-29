#ifdef ENABLE_NATIVE_UI
/*
 * The main menu's MYSTERY GIFT on its menus (mystery_gift_menu.c; not one of
 * the 31 designed screens: the design's pieces): the game's title on a gift
 * card's ribbon, its list as the design's buttons (WONDER CARDS / WONDER NEWS
 * / EXIT; WIRELESS COMMUNICATION / FRIEND / CANCEL) and its lines in the
 * message box as the game prints them. The PC has no Wireless Adapter, so a
 * search says so and comes back (union_room.c). A WONDER CARD or NEWS, never
 * received on PC, stays the game's picture. Project-owned.
 */
#include <stdlib.h>

#include "global.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ITEM = UI_HIT_SCREEN,
    HIT_MESSAGE,
};

#define BUTTON_W 320
#define BUTTON_H 34

static const u8 sText_Choose[] = _("Choose");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Next[] = _("Next");

static struct NativeMysteryGift sM;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg, sMsgPrinting;
static u32 sMsgPrinted;

static bool Detect(void)
{
    struct NativeMysteryGift m;

    return MysteryGift_NativeUiState(&m);
}

static void Capture(void)
{
    bool8 active;

    MysteryGift_NativeUiState(&sM);
    sHasMsg = sM.msgWindow != WINDOW_NONE && TextPrinter_NativeUiWindowText(sM.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
}

static const u8 *Title(void)
{
    const struct NativeUiPrinted *p;

    return TextPrinter_NativeUiTranscript(sM.headerWindow, &p) > 0 ? p[0].text : NULL;
}

static void Backdrop(int bx, int by, int bw, int bh, int t)
{
    struct UiPattern dots = UiPat_Dots(16, 3, 0, 0, true);

    UiG_Rect(bx, by, bw, bh, UI_HEX(0x6C78D8));
    UiG_FillPattern(&dots, bx, by, bw, bh, UI_HEX(0x7C88E4), (t / 4) % 16, 0);
    /* the gift's ribbon across the screen */
    UiG_Rect(bx, by + 52, bw, 12, UI_HEX(0xE84868));
    UiG_Rect(bx, by + 52, bw, 2, UI_HEX(0xF888A0));
}

static void Menu(int t)
{
    int top = 168 - (sM.menuCount * (BUTTON_H + 10)) / 2;

    for (int i = 0; i < sM.menuCount; i++)
    {
        bool sel = i == sM.menuCursor;
        int x = 320 - BUTTON_W / 2, y = top + i * (BUTTON_H + 10);
        struct UiRamp r = UiRampDefault(sel ? UI_HEX(0xF8D870) : UI_HEX(0xF0F0F8));

        UiG_Paint(UiM_Rounded4(BUTTON_W, BUTTON_H, 6, 6, 6, 6), x, y, r.base,
                  &(struct UiPaint){UI_HEX(0x202848), r.hi, r.lo, 0, 2, NULL, UI_NONE});
        if (sM.menuLabels[i] != NULL)
            UiText_Draw(UI_FONT_NORMAL, 320, y + 9, sM.menuLabels[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        if (sel)
            UiW_Cursor(x + 14, y + 13, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ITEM, i, x, y, BUTTON_W, BUTTON_H);
    }
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    const u8 *title = Title();
    struct UiHintItem h[3];
    int n = 0;

    UiG_Bleed(&bx, &by, &bw, &bh);
    Backdrop(bx, by, bw, bh, t);
    /* the title on the ribbon's bow */
    UiG_Paint(UiM_Rounded4(260, 40, 8, 8, 8, 8), 190, 38, UI_HEX(0xF85878),
              &(struct UiPaint){UI_HEX(0x581020), UI_HEX(0xFF98B0), UI_HEX(0xC03050), 0, 2, NULL, UI_NONE});
    if (title != NULL)
        UiText_Draw(UI_FONT_NORMAL, 320, 50, title, UI_P_TEXT_W, UI_HEX(0x581020), UI_ALIGN_CENTER, 0);
    if (sM.menuUp)
        Menu(t);
    if (sHasMsg)
    {
        int b[4];

        UiW_DialogBox(16, 290, 608, 56, NULL, UI_NONE, b);
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 4, b[1] + 2, b[2] - 8, 2);
        if (!sM.menuUp && !sMsgPrinting)
            UiW_DownArrow(b[0] + b[2] - 10, b[1] + 28 + ((t / 16) % 2), UI_P_CURSOR, false);
        UiG_Hit(HIT_MESSAGE, 0, 16, 290, 608, 56);
    }
    if (sM.ready)
    {
        if (sM.menuUp)
        {
            h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        }
        else if (sHasMsg && !sMsgPrinting)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        }
        if (n > 0)
            UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, n, 10), sHasMsg ? 270 : by + bh - 22, h, n, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sM.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sM.ready || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sM.menuUp && hit->id == HIT_ITEM)
    {
        NativeUi_QueuePresses(hit->index > sM.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sM.menuCursor));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (!sM.menuUp && sHasMsg)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy != 0 && sM.ready && sM.menuUp && !NativeUi_PressesPending())
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_MysteryGift = {
    .name = "mystery_gift",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
