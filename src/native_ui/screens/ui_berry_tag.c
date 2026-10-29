#ifdef ENABLE_NATIVE_UI
/*
 * A berry's tag (not one of the 31 designed screens; the design's pieces):
 * the Bag's CHECK TAG as the design's card - the berry's own picture, its
 * number and name, its size and firmness, its two lines of description as the
 * game prints them, and its five flavours as bars in the flavours' colours
 * (the game's circles show which a berry has; the bars show how much). UP and
 * DOWN (the wheel) go to the Bag's next berry as the game does; A, B or a
 * click go back. Project-owned.
 */
#include "global.h"
#include "window.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_CARD = UI_HIT_SCREEN,
};

#define CARD_X 112
#define CARD_Y 40
#define CARD_W 416
#define CARD_H 270

static const u8 sText_OK[] = _("OK");
static const u8 sText_Back[] = _("Back");

static const char *const sFlavors[] = {"SPICY", "DRY", "SWEET", "BITTER", "SOUR"};
static const UiRGBA sFlavorColours[] = {UI_HEX(0xE84838), UI_HEX(0x3878D8), UI_HEX(0xE890B8), UI_HEX(0x48A848),
                                        UI_HEX(0xE8B020)};

static struct NativeBerryTag sT;

static bool Detect(void)
{
    struct NativeBerryTag t;

    return BerryTag_NativeUiState(&t);
}

static void Capture(void)
{
    BerryTag_NativeUiState(&sT);
    UiGfx_PrepareItemIcon(sT.item);
}

/* the line the game printed on a window at a row (its y, GBA pixels) from a column on (x at least minX), else NULL */
static const u8 *Printed(u8 window, int y, int minX)
{
    const struct NativeUiPrinted *p;
    u32 n = TextPrinter_NativeUiTranscript(window, &p);

    for (u32 i = 0; i < n; i++)
        if (p[i].y == y && p[i].x >= minX)
            return p[i].text;
    return NULL;
}

static void Line(int x, int y, const u8 *s, UiRGBA fg)
{
    if (s != NULL)
        UiText_Draw(UI_FONT_NORMAL, x, y, s, fg, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

static void Draw(int t)
{
    struct UiRamp tag = UiRampDefault(UI_HEX(0xE8B868));
    int bx, by, bw, bh, x = CARD_X, y = CARD_Y;
    const struct NativeUiPrinted *p;
    struct UiHintItem h[2] = {{UI_ACT_CONFIRM, sText_OK}, {UI_ACT_CANCEL, sText_Back}};

    (void)t;
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x584838));
    /* the tag: a card with its hole, the berry on its left in a circle, its lines on the right */
    UiG_Paint(UiM_Rounded4(CARD_W, CARD_H, 10, 10, 10, 10), x, y, tag.base,
              &(struct UiPaint){tag.ink, tag.hi, tag.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(CARD_W - 20, CARD_H - 20, 6, 6, 6, 6), x + 10, y + 10, UI_P_CREAM,
              &(struct UiPaint){tag.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Mask(UiM_Ellipse(14, 14), x + 22, y + 22, tag.lo);
    UiItems_BigCaps(x + CARD_W / 2, y + 22, "BERRY TAG", 2, tag.ink, UI_NONE, UI_ALIGN_CENTER);
    UiG_Mask(UiM_Ellipse(116, 116), x + 26, y + 62, UiMix(tag.hi, UI_HEX(0xFFFFFF), 0.5));
    UiGfx_ItemIconScaled(sT.item, x + 36, y + 72, 4);
    /* the game's lines: the number and name, the size and firmness (label, then its value), the description */
    if (TextPrinter_NativeUiTranscript(sT.nameWindow, &p) > 0)
        UiText_Draw(UI_FONT_NORMAL, x + 164, y + 66, p[0].text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    Line(x + 164, y + 96, Printed(sT.sizeWindow, 1, 0), UI_P_TEXT_MUTED);
    Line(x + 236, y + 96, Printed(sT.sizeWindow, 1, 0x28), UI_P_TEXT);
    Line(x + 164, y + 118, Printed(sT.sizeWindow, 17, 0), UI_P_TEXT_MUTED);
    Line(x + 236, y + 118, Printed(sT.sizeWindow, 17, 0x28), UI_P_TEXT);
    UiG_HLine(x + 26, y + 190, CARD_W - 52, UI_P_CREAM_LO);
    Line(x + 30, y + 198, Printed(sT.descWindow, 1, 0), UI_P_TEXT);
    Line(x + 30, y + 218, Printed(sT.descWindow, 17, 0), UI_P_TEXT);
    /* the five flavours: a bar each, as long as the berry's flavour (the game's values up to 40) */
    for (int i = 0; i < 5; i++)
    {
        int fy = y + 146 + (i / 3) * 18, fx = x + 164 + (i % 3) * 78, len = sT.flavors[i] > 40 ? 40 : sT.flavors[i];

        UiCaps_Draw(fx, fy, sFlavors[i], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Rect(fx + 36, fy, 34, 6, UI_P_CREAM_LO);
        if (len > 0)
            UiG_Rect(fx + 36, fy, len * 34 / 40 > 2 ? len * 34 / 40 : 2, 6, sFlavorColours[i]);
    }
    UiG_Hit(HIT_CARD, 0, bx, by, bw, bh);
    if (sT.ready)
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(h, 2, 10), by + bh - 28, h, 2, true, 10);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sT.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (sT.ready && !NativeUi_PressesPending() && (button == 1 || button == 3))
        NativeUi_QueuePress(button == 1 ? A_BUTTON : B_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sT.ready || NativeUi_PressesPending())
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_BerryTag = {
    .name = "berry_tag",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
