#ifdef ENABLE_NATIVE_UI
/*
 * The Battle Factory's rental and swap screens (battle_factory_screen.c; the
 * Slateport Battle Tent's too; not one of the 31 designed screens: the
 * design's pieces): the balls as cards on the Factory's belt - each one's
 * Pokémon as the game names it when the cursor is on it, the rentals' order
 * (1ST, 2ND, 3RD) - and the cursor's Pokémon in the details panel (its
 * picture, category, types, ability, held item and moves: what SUMMARY
 * shows), the game's instruction line, its menus (SUMMARY / RENT or DESELECT
 * / OTHERS; SUMMARY / SWAP / RECHOOSE), its YES / NO with the three chosen
 * up, the swap screen's buttons (PKMN FOR SWAP, CANCEL) and, on the beaten
 * trainer's side, the player's three with the one going out. The game keeps
 * every rule; clicks and the wheel are its own presses. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "international_string_util.h"
#include "pokemon.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_BALL = UI_HIT_SCREEN,
    HIT_BACKDROP,
    HIT_INFO,
};

#define CARD_Y 78
#define CARD_W 96
#define CARD_H 92
#define CARD_STEP 102
#define PANEL_Y 178
#define PANEL_H 152
#define MENU_W 112

static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Open[] = _("Open");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Quit[] = _("Quit");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Ability[] = _("Ability");
static const u8 sText_Item[] = _("Held item");
static const u8 sText_NoItem[] = _("None");
static const u8 sText_ForSwapHelp[] = _("Back to your own three Pokémon.");
static const u8 sText_CancelHelp[] = _("Keep your three Pokémon as they are.");
static const char *const sOrder[] = {"", "1ST", "2ND", "3RD"};

static struct NativeFactory sF;
static struct UiMon sMons[NATIVE_FACTORY_BALLS];
static struct UiMon sParty[3];
static u8 sCategory[40];
static u8 sInfo[UI_MESSAGE_COPY];
static bool sHasInfo;
static uint32_t sPicAt, sPicLast;
static int sPicFor = -1;

/* ---------------------------------------------------------------- state */

static bool Detect(void)
{
    struct NativeFactory f;

    return Factory_NativeUiState(&f);
}

static int Stops(void)
{
    return sF.count + sF.buttonCount;
}

static void Capture(void)
{
    u32 printed;
    bool8 active;
    uint32_t now = NativeUi_Frame();

    Factory_NativeUiState(&sF);
    for (int i = 0; i < sF.count; i++)
        UiMon_FromPokemon(&sMons[i], sF.mons[i]);
    for (int i = 0; i < 3 && sF.swap && sF.count > 0; i++)
        UiMon_FromPokemon(&sParty[i], sF.party[i]);
    sHasInfo = sF.count > 0 && TextPrinter_NativeUiWindowText(sF.infoWindow, sInfo, sizeof(sInfo), &printed, &active);
    sCategory[0] = EOS;
    if (sF.cursor < sF.count)
    {
        const struct UiMon *m = &sMons[sF.cursor];

        CopyMonCategoryText(m->species, sCategory);
        UiGfx_PrepareMonPic(m->species, m->personality, m->shiny, false);
        if (m->heldItem != ITEM_NONE)
            UiGfx_PrepareItemIcon(m->heldItem);
        /* the picture's own animation from the cursor's arrival */
        if (sPicFor != sF.cursor || now - sPicLast > 2)
            sPicAt = now;
        sPicFor = sF.cursor;
        sPicLast = now;
    }
    if (sF.showChosen)
        for (int i = 0; i < sF.count; i++)
            if (sF.order[i] != 0)
                UiGfx_PrepareMonPic(sMons[i].species, sMons[i].personality, sMons[i].shiny, false);
}

/* ---------------------------------------------------------------- drawing */

static int CardX(int i)
{
    return 16 + i * CARD_STEP;
}

static void Card(int i, int t)
{
    const struct UiMon *m = &sMons[i];
    bool sel = sF.cursor == i, chosen = !sF.swap && sF.order[i] != 0;
    struct UiRamp r = UiRampDefault(chosen ? UI_HEX(0xE8B848) : UI_HEX(0x8CA8C0));
    int x = CardX(i), y = CARD_Y;

    if (sel)
        r = UiRampDefault(UiMix(r.base, UI_HEX(0xFFFFFF), 0.35));
    UiG_Paint(UiM_Rounded4(CARD_W, CARD_H, 6, 6, 6, 6), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(CARD_W - 12, 58, 4, 4, 4, 4), x + 6, y + 6, UI_P_CREAM,
              &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiGfx_MonIconScaled(m->species, m->personality, false, sel ? (t / 12) % 2 : 0, x + (CARD_W - 64) / 2, y + 2, 2);
    UiText_Draw(UiText_Width(UI_FONT_NORMAL, GetSpeciesName(m->species), 0) > CARD_W - 8 ? UI_FONT_NARROW : UI_FONT_NORMAL,
                x + CARD_W / 2, y + 68, GetSpeciesName(m->species), r.ink, UI_NONE, UI_ALIGN_CENTER, 0);
    if (chosen && sF.order[i] < ARRAY_COUNT(sOrder))
    {
        UiG_Paint(UiM_Rounded4(30, 14, 3, 3, 3, 3), x + CARD_W - 34, y + 4, UI_HEX(0xC83828),
                  &(struct UiPaint){UI_HEX(0x581010), UI_HEX(0xF06848), UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(x + CARD_W - 19, y + 8, sOrder[sF.order[i]], UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    }
    if (sel)
        UiW_DownArrow(x + CARD_W / 2 - 4, y - 8 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_BALL, i, x, y, CARD_W, CARD_H);
}

static void Button(int k, int t)
{
    int stop = sF.count + k, x = CardX(sF.count) + 6, y = CARD_Y + k * 48, w = 150, h = 40;
    bool sel = sF.cursor == stop, forSwap = sF.buttons[k] == NATIVE_FACTORY_BUTTON_FOR_SWAP;
    struct UiRamp r = UiRampDefault(forSwap ? UI_HEX(0x58A868) : UI_HEX(0xB85848));

    if (sel)
        r = UiRampDefault(UiMix(r.base, UI_HEX(0xFFFFFF), 0.3));
    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, UI_NONE});
    UiCaps_Draw(x + w / 2, y + 17, forSwap ? "POK" UI_CAPS_E_ACUTE "MON FOR SWAP" : "CANCEL", UI_P_TEXT_W, r.ink,
                UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
    if (sel)
        UiW_Cursor(x - 12, y + 14, UiW_Nudge(t), UI_NONE);
    UiG_Hit(HIT_BALL, stop, x, y, w, h);
}

/* the beaten trainer's side: the player's three, the one going out marked */
static void PartyStrip(void)
{
    int x = 486, y = CARD_Y, w = 138, h = 92;

    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, UI_HEX(0x3C4C5C),
              &(struct UiPaint){UI_HEX(0x10181E), UI_HEX(0x5C6C7C), UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + w / 2, y + 7, "YOUR POK" UI_CAPS_E_ACUTE "MON", UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    for (int i = 0; i < 3; i++)
    {
        bool out = i == sF.swapOut;
        int ix = x + 6 + i * 44;

        UiGfx_MonIcon(sParty[i].species, sParty[i].personality, false, 0, ix + 4, y + 22,
                      out ? &(struct UiFilter){0, 0.25f, 0, 0.75f} : NULL);
        if (out)
        {
            UiG_Paint(UiM_Rounded4(30, 13, 3, 3, 3, 3), ix + 5, y + 58, UI_HEX(0xC83828),
                      &(struct UiPaint){UI_HEX(0x581010), UI_HEX(0xF06848), UI_NONE, 0, 0, NULL, UI_NONE});
            UiCaps_Draw(ix + 20, y + 62, "OUT", UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        }
    }
}

static void Moves(const struct UiMon *m, int x, int y)
{
    for (int i = 0; i < m->moveCount && i < MAX_MON_MOVES; i++)
    {
        int yy = y + i * 24;

        UiW_TypeBadge(x, yy + 2, m->moveTypes[i] >= 0 ? m->moveTypes[i] : UI_TYPE_NORMAL, 0);
        if (m->moveNames[i] != NULL)
            UiG_Text(x + 54, yy, m->moveNames[i]);
    }
}

static void Details(int t)
{
    const struct UiMon *m = &sMons[sF.cursor];
    int x = 16, y = PANEL_Y, w = 608, h = PANEL_H, tx = x + 196;

    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x283440), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    if (sF.showChosen)
    {
        /* the three chosen, in the order they go out */
        for (int k = 1; k <= 3; k++)
            for (int i = 0; i < sF.count; i++)
                if (sF.order[i] == k)
                {
                    int cx = x + 100 + (k - 1) * 170;

                    UiG_Mask(UiM_Ellipse(120, 26), cx - 60, y + h - 34, UI_P_CREAM_LO);
                    UiGfx_MonPicEx(sMons[i].species, sMons[i].personality, sMons[i].shiny, false, 0, cx, y + h - 20, 2, 1.0,
                                   NULL);
                    UiCaps_Draw(cx, y + 10, sOrder[k], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
                }
        return;
    }
    if (sF.cursor >= sF.count)
    {
        bool forSwap = sF.buttons[sF.cursor - sF.count] == NATIVE_FACTORY_BUTTON_FOR_SWAP;

        UiG_Text(x + 24, y + 24, forSwap ? sText_ForSwapHelp : sText_CancelHelp);
        return;
    }
    /* the picture on its stand, the lines beside it, the moves on the right */
    UiG_Mask(UiM_Ellipse(140, 30), x + 26, y + h - 40, UI_P_CREAM_LO);
    UiGfx_MonPicEx(m->species, m->personality, m->shiny, false,
                   UiGfx_MonPicFrameAt(m->species, m->personality, m->shiny, false, (int)(NativeUi_Frame() - sPicAt)),
                   x + 96, y + h - 24, 2, 1.0, NULL);
    UiText_Draw(UI_FONT_NORMAL, tx, y + 12, GetSpeciesName(m->species), UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    if (m->shiny)
        UiMon_Star(tx + UiText_Width(UI_FONT_NORMAL, GetSpeciesName(m->species), 0) + 6, y + 15);
    UiText_Draw(UI_FONT_SMALL, tx, y + 30, sCategory, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    for (int k = 0; k < 2; k++)
        if (m->types[k] >= 0 && (k == 0 || m->types[1] != m->types[0]))
            UiW_TypeBadge(tx + k * 52, y + 48, m->types[k], 0);
    UiText_Draw(UI_FONT_SMALL, tx, y + 72, sText_Ability, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    if (m->abilityName != NULL)
        UiG_Text(tx, y + 84, m->abilityName);
    UiText_Draw(UI_FONT_SMALL, tx, y + 106, sText_Item, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    if (m->heldItem != ITEM_NONE)
    {
        UiGfx_ItemIcon(m->heldItem, tx - 2, y + 116, NULL);
        if (m->heldItemName != NULL)
            UiG_Text(tx + 24, y + 120, m->heldItemName);
    }
    else
    {
        UiG_Text(tx, y + 120, sText_NoItem);
    }
    UiG_Rect(x + 404, y + 12, 1, h - 24, UI_P_CREAM_LO);
    Moves(m, x + 418, y + 16);
    (void)t;
}

/* the menus drop from the cursor's card (or button) */
static int MenuX(void)
{
    int x = sF.cursor < sF.count ? CardX(sF.cursor) + (CARD_W - MENU_W) / 2 : CardX(sF.count) + 6;

    return x < 16 ? 16 : x > 624 - MENU_W ? 624 - MENU_W : x;
}

static void Hints(void)
{
    struct UiHintItem h[3];
    int n = 0;

    switch (sF.phase)
    {
    case NATIVE_FACTORY_CHOOSE:
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sF.cursor < sF.count ? sText_Open : sText_Select};
        if (sF.swap)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Quit};
        break;
    case NATIVE_FACTORY_MENU:
    case NATIVE_FACTORY_YES_NO:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_FACTORY_MESSAGE:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        break;
    default:
        break;
    }
    if (n > 0)
        UiW_Hints(16, 336, h, n, true, 12);
}

static void Draw(int t)
{
    int bx, by, bw, bh;
    const char *where;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x222C36));
    /* the Factory's belt under the balls */
    UiG_Rect(bx, CARD_Y + 30, bw, 34, UI_HEX(0x2E3A46));
    for (int xx = bx - (t / 2) % 24; xx < bx + bw; xx += 24)
        UiG_Rect(xx, CARD_Y + 30, 2, 34, UI_HEX(0x384856));
    UiG_Rect(bx, by, bw, 42, UI_HEX(0x34424E));
    UiItems_BigCaps(16, 12, sF.swap ? "POK" UI_CAPS_E_ACUTE "MON SWAP" : "RENTAL POK" UI_CAPS_E_ACUTE "MON", 2,
                    UI_HEX(0xF8D878), UI_HEX(0x101820), UI_ALIGN_LEFT);
    where = sF.lvlMode == FRONTIER_LVL_TENT ? "SLATEPORT BATTLE TENT"
          : sF.lvlMode == FRONTIER_LVL_50   ? "BATTLE FACTORY  -  LV. 50"
                                            : "BATTLE FACTORY  -  OPEN LEVEL";
    UiCaps_Draw(624, 18, where, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    if (sF.count == 0)
    {
        UiG_Veil(NativeUi_ScreenFade());
        return;
    }
    /* the game's line */
    UiG_Paint(UiM_Rounded4(608, 24, 4, 4, 4, 4), 16, 44, UI_P_PAPER,
              &(struct UiPaint){UI_P_INK, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    if (sHasInfo)
        UiW_MessagePage(sInfo, -1, 26, 48, 588, 1);
    UiG_Hit(HIT_INFO, 0, 16, 44, 608, 24);
    for (int i = 0; i < sF.count; i++)
        Card(i, t);
    for (int k = 0; k < sF.buttonCount; k++)
        Button(k, t);
    if (sF.swap && sF.enemyScreen)
        PartyStrip();
    Details(t);
    Hints();
    if (sF.phase == NATIVE_FACTORY_MENU || sF.phase == NATIVE_FACTORY_YES_NO)
        UiG_Hit(HIT_BACKDROP, 0, bx, by, bw, bh);
    if (sF.phase == NATIVE_FACTORY_MENU)
    {
        struct UiMenuItem items[3];

        for (int i = 0; i < sF.menuCount; i++)
            items[i] = (struct UiMenuItem){sF.menuLabels[i], false};
        UiW_MenuBox(MenuX(), CARD_Y + CARD_H + 2, items, sF.menuCount, sF.menuCursor,
                    &(struct UiMenuOpt){.anchor = UI_ANCHOR_TOPLEFT, .t = t, .minW = MENU_W}, NULL);
    }
    else if (sF.phase == NATIVE_FACTORY_YES_NO && sF.showChosen)
    {
        UiW_YesNo(624, 324, sF.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sF.phase == NATIVE_FACTORY_YES_NO)
    {
        UiW_YesNo(MenuX(), CARD_Y + CARD_H + 2, sF.menuCursor, t, UI_ANCHOR_TOPLEFT, NULL);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sF.ready;
}

/* the game's own way to a stop: LEFT / RIGHT round the balls (and buttons), the shorter way */
static void QueueTo(int stop)
{
    int n = Stops(), fwd;

    if (n <= 0 || stop < 0 || stop >= n || stop == sF.cursor)
        return;
    fwd = (stop - sF.cursor + n) % n;
    if (fwd <= n - fwd)
        NativeUi_QueuePresses(DPAD_RIGHT, fwd);
    else
        NativeUi_QueuePresses(DPAD_LEFT, n - fwd);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sF.ready || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sF.phase != NATIVE_FACTORY_CHOOSE || sF.swap)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    switch (sF.phase)
    {
    case NATIVE_FACTORY_CHOOSE:
        if (hit->id != HIT_BALL)
            return false;
        QueueTo(hit->index);
        NativeUi_QueuePress(A_BUTTON);
        return true;
    case NATIVE_FACTORY_MENU:
    case NATIVE_FACTORY_YES_NO:
        if (hit->id == (sF.phase == NATIVE_FACTORY_MENU ? UI_HIT_MENU : UI_HIT_YESNO))
        {
            NativeUi_QueuePresses(hit->index > sF.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sF.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_FACTORY_MESSAGE:
        NativeUi_QueuePress(A_BUTTON);
        return true;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (dy == 0 || !sF.ready || NativeUi_PressesPending())
        return true;
    if (sF.phase == NATIVE_FACTORY_CHOOSE)
        NativeUi_QueuePress(dy > 0 ? DPAD_LEFT : DPAD_RIGHT);
    else if (sF.phase == NATIVE_FACTORY_MENU || sF.phase == NATIVE_FACTORY_YES_NO)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Factory = {
    .name = "factory",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
