#ifdef ENABLE_NATIVE_UI
/*
 * Placing a decoration and putting one away on the field (decoration.c; not one of the 31 designed screens: the
 * design's pieces). The 3D view draws the decoration where the cursor is (laid on the floor; a doll or a cushion
 * standing) and the decorating player by it (diorama/scene/scene_snapshot.c); this plate says what is going on: the
 * decoration's picture and name and whether it can stand where the cursor is - the game's own check, the answer A
 * would get - or, putting away, what to do; and the keys. The game keeps every rule and key; its lines and YES / NO are
 * the field's message box (ui_field_text.c). The prompts' clicks press A and B. Project-owned.
 */
#include "global.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

static const u8 sText_Placing[] = _("DECORATE");
static const u8 sText_PutAway[] = _("PUT AWAY");
static const u8 sText_Fits[] = _("It can go here.");
static const u8 sText_DoesNotFit[] = _("It can't go here.");
static const u8 sText_PutAwayLine[] = _("Choose a decoration to return to the PC.");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Place[] = _("Place");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Done[] = _("Done");

static struct NativeDecorPlacing sD;

static bool Detect(void)
{
    struct NativeDecorPlacing d;

    return Decoration_NativeUiPlacing(&d);
}

static void Capture(void)
{
    Decoration_NativeUiPlacing(&sD);
    if (!sD.putAway && sD.decoration != 0)
        UiGfx_PrepareDecor(sD.decoration);
}

static bool Ready(void)
{
    return sD.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_KEEP;
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    int w = 336, h = 64, x, y;
    struct UiRamp band = UiRampDefault(sD.putAway ? UI_HEX(0x7860B0) : UI_HEX(0x48A070));
    struct UiHintItem hints[3];

    UiG_Bleed(&bx, &by, &bw, &bh);
    x = bx + (bw - w) / 2;
    y = by + 10;
    UiG_Paint(UiM_Rounded4(w, h, 8, 8, 8, 8), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x101420), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(96, 18, 4, 4, 4, 4), x + 10, y - 8, band.base,
              &(struct UiPaint){band.ink, band.hi, band.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + 58, y - 7, sD.putAway ? sText_PutAway : sText_Placing, UI_P_TEXT_W, band.ink,
                UI_ALIGN_CENTER, 0);
    if (sD.putAway)
    {
        UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 24, sText_PutAwayLine, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER, 0);
    }
    else
    {
        /* the decoration, as the shops show it; its name; whether it can stand where the cursor is */
        UiG_Paint(UiM_Rounded4(48, 48, 6, 6, 6, 6), x + 10, y + 8, UI_HEX(0xFFFFFF),
                  &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_NONE, 0, 1, NULL, UI_NONE});
        if (sD.decoration != 0)
        {
            int dw = 16, dh = 16, scale;

            UiGfx_DecorSize(sD.decoration, &dw, &dh);
            scale = 40 / (dw > dh ? dw : dh);
            UiGfx_Decor(sD.decoration, x + 14, y + 12, 40, scale < 1 ? 1 : scale > 3 ? 3 : scale);
        }
        if (sD.name != NULL)
            UiText_Draw(UI_FONT_NORMAL, x + 70, y + 12, sD.name, UI_P_TEXT, UI_NONE, UI_ALIGN_LEFT, 0);
        UiG_Mask(UiM_Ellipse(10, 10), x + 70, y + 39, sD.canPlace ? UI_HEX(0x38B060) : UI_HEX(0xE04838));
        UiText_Draw(UI_FONT_NORMAL, x + 86, y + 34, sD.canPlace ? sText_Fits : sText_DoesNotFit,
                    sD.canPlace ? UI_HEX(0x287848) : UI_HEX(0xB03020), UI_NONE, UI_ALIGN_LEFT, 0);
    }
    hints[0] = (struct UiHintItem){UI_ACT_SCROLL, sText_Move};
    hints[1] = (struct UiHintItem){UI_ACT_CONFIRM, sD.putAway ? sText_Choose : sText_Place};
    hints[2] = (struct UiHintItem){UI_ACT_CANCEL, sD.putAway ? sText_Done : sText_Cancel};
    UiW_Hints(bx + bw - 8 - UiW_HintsWidth(hints, 3, 10), by + bh - 22, hints, 3, true, 10);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sD.ready || NativeUi_PressesPending())
        return true;
    if (button == 3 || (button == 1 && hit->id == UI_HIT_HINT && hit->index == UI_ACT_CANCEL))
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button == 1 && hit->id == UI_HIT_HINT && hit->index == UI_ACT_CONFIRM)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

const struct UiScreen gUiScreen_DecorPlace = {
    .name = "decor_place",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
