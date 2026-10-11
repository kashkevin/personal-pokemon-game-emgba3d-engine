#ifdef ENABLE_NATIVE_UI
/*
 * The native UI kit's test card (PKMN_NATIVE_UI_TESTCARD=1): one of every
 * shape, face and widget at fixed places, drawn exactly as the design's
 * preview draws the same calls (artifacts/ui-art-direction/tools/native_ui/
 * testcard.js), so the two can be compared pixel for pixel. Project-owned.
 */
#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

static const u8 sText_Line1[] = _("The quick brown fox jumps. {PKMN} é ♂♀ {LV}50 ¥1,234");
static const u8 sText_Small[] = _("Small font: 0123456789 ABCDEFG abcdefg");
static const u8 sText_Narrow[] = _("Narrow font: a slightly longer line of text");
static const u8 sText_Short[] = _("Short font: SHORT LINE 42");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Bag[] = _("BAG");
static const u8 sText_Pokemon[] = _("POKéMON");
static const u8 sText_Run[] = _("RUN");
static const u8 sText_Item1[] = _("POTION");
static const u8 sText_Item2[] = _("SUPER POTION");
static const u8 sText_Item3[] = _("FULL RESTORE");
static const u8 sText_White[] = _("White text on a button");

void UiTestCard_Draw(int t)
{
    int bx, by, bw, bh;
    struct UiPattern stripes = UiPat_Diag(8, 4, 0, 1);
    struct UiPattern dots = UiPat_Dots(6, 1, 0, 0, true);
    struct UiRamp field = {UI_R_FIELD_HI, UI_R_FIELD_BASE, UI_R_FIELD_LO, UI_R_FIELD_INK};
    struct UiRamp slot = {UI_R_SLOT_HI, UI_R_SLOT_BASE, UI_R_SLOT_LO, UI_R_SLOT_INK};
    struct UiPanelOpt panelOpt = {0};
    struct UiMenuItem items[3] = {{sText_Item1, false}, {sText_Item2, false}, {sText_Item3, true}};
    struct UiMenuOpt menuOpt = {0};
    struct UiHintItem hints[2] = {{UI_ACT_CONFIRM, sText_Select}, {UI_ACT_CANCEL, sText_Back}};
    struct UiDsButtonOpt ds = {0};
    const struct UiMask *m;
    int inner[4];
    int x;

    (void)t;
    t = 0; /* a still picture: the comparison is pixel for pixel */

    /* 1. background over the whole canvas: colour, then diagonal stripes */
    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_P_PARTY_BG);
    UiG_FillPattern(&stripes, bx, by, bw, bh, UI_P_PARTY_BG_STRIPE, 0, 0);

    /* 2. a panel with a dotted pattern */
    panelOpt.pattern = &dots;
    panelOpt.patternColor = UiMix(field.base, field.hi, 0.5);
    UiG_Panel(8, 8, 200, 120, &field, 3, &panelOpt);
    UiG_Panel(16, 16, 90, 40, &slot, 2, NULL);
    UiW_TitleTab(16, 64, "TEST CARD", &field, 0, NULL);
    UiW_TitleTab(16, 88, "WITH ICON", &slot, 0, "bag");

    /* 3. message box and the engine fonts */
    UiW_DialogBox(216, 8, 416, 48, NULL, UI_NONE, inner);
    UiG_Text(inner[0], inner[1], sText_Line1);
    UiText_Draw(UI_FONT_SMALL, 220, 60, sText_Small, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_NARROW, 220, 76, sText_Narrow, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SHORT, 220, 92, sText_Short, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);

    /* 4. label faces */
    UiG_Caps(220, 112, "OUTLINE CAPS 123", UI_P_TEXT_W, UI_P_INK, UI_CAPS_OUTLINE, UI_ALIGN_LEFT);
    UiCaps_Draw(340, 112, "SHADOW CAPS", UI_P_TEXT_W, UI_P_INK, UI_CAPS_SHADOW, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(440, 112, "PLAIN " UI_CAPS_RIGHT UI_CAPS_DOWN, UI_P_CURSOR, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiMicro_Draw(540, 113, "HP EXP LV", UI_P_HP_LABEL);

    /* 5. bars */
    UiW_HpBar(8, 140, 60, 1.0, true, UI_NONE);
    UiW_HpBar(90, 140, 60, 0.45, true, UI_NONE);
    UiW_HpBar(172, 140, 60, 0.15, true, UI_NONE);
    UiW_HpBar(254, 140, 60, 0.0, false, UI_NONE);
    UiW_ExpBar(330, 142, 80, 0.6, true);

    /* 6. badges */
    x = 8;
    x = UiW_TypeBadge(x, 160, UI_TYPE_FIRE, 0) + 4;
    x = UiW_TypeBadge(x, 160, UI_TYPE_WATER, 0) + 4;
    x = UiW_TypeBadge(x, 160, UI_TYPE_GRASS, 0) + 4;
    x = UiW_TypeBadge(x, 160, UI_TYPE_DRAGON, 0) + 4;
    x = UiW_TypeBadge(x, 160, UI_TYPE_FAIRY, 0) + 4;
    x = UiW_StatusBadge(x, 161, "PSN", UI_P_STATUS_PSN, 0) + 4;
    x = UiW_StatusBadge(x, 161, "PAR", UI_P_STATUS_PAR, 0) + 4;
    x = UiW_StatusBadge(x, 161, "BRN", UI_P_STATUS_BRN, 0) + 4;
    x = UiW_CategoryIcon(x, 160, 0) + 4;
    x = UiW_CategoryIcon(x, 160, 1) + 4;
    x = UiW_CategoryIcon(x, 160, 2) + 4;
    UiW_Planned(x, 161, NULL);

    /* 7. cursors and frames */
    UiW_Cursor(8, 184, UiW_Nudge(t), UI_NONE);
    UiW_DownArrow(24, 186, UI_NONE, false);
    UiW_DownArrow(36, 186, UI_NONE, true);
    m = UiM_Rounded4(60, 20, 2, 2, 2, 2);
    UiG_Paint(m, 60, 184, UI_P_PAPER, &(struct UiPaint){UI_P_INK, UI_NONE, UI_P_PAPER_LO});
    UiW_SelectFrame(m, 60, 184, UI_NONE, UI_NONE, 2);
    UiG_Paint(m, 140, 184, UI_P_PAPER, &(struct UiPaint){UI_P_INK, UI_NONE, UI_P_PAPER_LO});
    UiW_CarryFrame(m, 140, 184);

    /* 8. keys and hints */
    x = 220;
    x = UiW_Keycap(x, 186, "Z") + 4;
    x = UiW_Keycap(x, 186, "ENTER") + 4;
    x = UiW_Keycap(x, 186, "\\") + 4;
    x = UiW_PadButton(x, 186, "A") + 4;
    x = UiW_PadButton(x, 186, "X") + 4;
    x = UiW_PadButton(x, 186, "LB") + 8;
    UiW_Hints(x, 189, hints, 2, true, 10);

    /* 9. buttons, menu box, yes/no, scrollbar, page dots */
    ds.icon = "bag";
    ds.selected = true;
    UiW_DsButton(8, 214, 120, 30, UI_P_CMD_BAG, sText_Bag, &ds);
    ds = (struct UiDsButtonOpt){0};
    ds.center = true;
    UiW_DsButton(140, 214, 120, 30, UI_P_CMD_POKEMON, sText_Pokemon, &ds);
    ds.disabled = true;
    UiW_DsButton(272, 214, 80, 30, UI_P_CMD_RUN, sText_Run, &ds);
    ds = (struct UiDsButtonOpt){0};
    UiW_DsButton(364, 214, 268, 30, UI_P_CMD_FIGHT, sText_White, &ds);
    menuOpt.t = t;
    UiW_MenuBox(8, 252, items, 3, 1, &menuOpt, NULL);
    UiW_YesNo(260, 344, 0, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiW_Scrollbar(270, 252, 90, 3, 20, 6, NULL);
    UiW_PageDots(290, 256, 6, 2, UI_NONE, UI_NONE);

    /* 10. the design's menu icons */
    x = 290;
    UiGfx_Icon("dex", x, 272, 0);
    UiGfx_Icon("bag", x + 28, 272, 0);
    UiGfx_Icon("card", x + 56, 272, 0);
    UiGfx_Icon("save", x + 84, 272, 0);
    UiGfx_Icon("options", x + 112, 272, 0);
    UiGfx_Icon("players", x + 140, 272, 0);
    UiGfx_Icon("bag_sel", x + 168, 272, 0);

    /* 11. alpha and a clipped panel */
    UiG_Save();
    UiG_Alpha(0.5);
    UiG_Rect(470, 252, 60, 40, UI_P_SEL);
    UiG_Restore();
    UiG_Save();
    UiG_Clip(540, 252, 40, 40);
    UiG_Panel(540, 252, 90, 60, &slot, 4, NULL);
    UiG_Restore();
}
#endif // ENABLE_NATIVE_UI
