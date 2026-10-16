#ifdef ENABLE_NATIVE_UI
/*
 * 1 · Party (the approved design's preview/js/screens/party.js): six slots with
 * the game's party icons, the info pane with the game's front picture, types,
 * ability, item and moves, the action menu, moving a Pokémon. The engine's
 * party menu (party_menu.c) keeps running under it and keeps every rule: what
 * the menu offers, items, field moves, switching, the battle's choices, the
 * messages. The UI follows its state, moves its cursor on the design's 2x3
 * grid and turns those moves and mouse clicks into the engine's own button
 * presses. The multi battles' layouts (a partner's Pokémon in the grid) keep
 * the game's own screen. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "event_object_movement.h"
#include "item.h"
#include "main.h"
#include "move.h"
#include "party_menu.h"
#include "pokemon.h"
#include "string_util.h"
#include "window.h"
#include "constants/characters.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/party_menu.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_SLOT = UI_HIT_SCREEN,
    HIT_BACK,
    HIT_CONFIRM,
    HIT_MESSAGE,
    HIT_BACKDROP,
    HIT_MOVE,
};

#define SLOT_W 200
#define SLOT_H 64
#define SEL_CONFIRM PARTY_SIZE
#define SEL_CANCEL (PARTY_SIZE + 1)
#define DPAD_ALL (DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT)

/* the design's slot colours (P.SLOT, P.SLOT_SEL, P.SLOT_FNT) */
struct SlotRamp
{
    UiRGBA hi;
    UiRGBA base;
    UiRGBA lo;
    UiRGBA ink;
    UiRGBA stripe;
};
static const struct SlotRamp sSlot = {UI_R_SLOT_HI, UI_R_SLOT_BASE, UI_R_SLOT_LO, UI_R_SLOT_INK, UI_R_SLOT_STRIPE};
static const struct SlotRamp sSlotSel = {UI_R_SLOT_SEL_HI, UI_R_SLOT_SEL_BASE, UI_R_SLOT_SEL_LO, UI_R_SLOT_SEL_INK,
                                         UI_R_SLOT_SEL_STRIPE};
static const struct SlotRamp sSlotFnt = {UI_R_SLOT_FNT_HI, UI_R_SLOT_FNT_BASE, UI_R_SLOT_FNT_LO, UI_R_SLOT_FNT_INK,
                                         UI_R_SLOT_FNT_STRIPE};

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_Back[] = _("Back");
static const u8 sText_Confirm[] = _("Confirm");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Move[] = _("Move");
static const u8 sText_PutHere[] = _("Put here");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Use[] = _("Use");
static const u8 sText_Give[] = _("Give");
static const u8 sText_Teach[] = _("Teach");
static const u8 sText_BackToBag[] = _("Back to Bag");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Next[] = _("Next");
static const u8 sText_No[] = _("No");
static const u8 sText_None[] = _("None");
static const u8 sText_Dash[] = _("-");
static const u8 sText_Wait[] = _("Please wait…");

/* ---------------------------------------------------------------- captured state */

static struct NativePartyMenu sMenu;
static struct UiMon sMons[PARTY_SIZE];
static int sMonCount;            /* the party's Pokémon (slots 0..sMonCount-1) */
static s8 sFollowing;            /* the slot of the Pokémon following the player (-1 none) */
static u8 sPrompt[256];          /* the prompt line as printed */
static bool sHasPrompt;
static u8 sMsg[256];             /* the message box as printed */
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sMsgWaiting;         /* the printer waits for a button (a page break) */
static u16 sItem;                /* the item in hand */
static const u8 *sItemName;
static int sNav;                 /* the grid's cursor: 0-5, SEL_CONFIRM, SEL_CANCEL */
static int sInfoSlot = -1;       /* the Pokémon in the info pane */
static int sInfoT;               /* frames since it came there (its front animation) */
static int sSwapT;
static int sHeldDir;             /* the direction held in the grid, for the repeat */
static int sHeldFrames;
/* SELECT on a Pokémon: A opens the menu, then its SWITCH row is chosen */
static bool sMoveIntent;
static int sMoveIntentT;

/* ---------------------------------------------------------------- the engine's cursor order */

/* The engine's party cursor goes up and down through the Pokémon, then
 * CONFIRM (when shown), then CANCEL, and wraps around (party_menu.c,
 * UpdatePartySelectionSingleLayout / DoubleLayout). */
static int EngineOrder(int *order)
{
    int n = 0;

    for (int i = 0; i < sMonCount; i++)
        order[n++] = i;
    if (sMenu.chooseHalf)
        order[n++] = SEL_CONFIRM;
    order[n++] = SEL_CANCEL;
    return n;
}

/* queues the engine's presses that take its cursor from one slot to another */
static void QueuePath(int from, int to)
{
    int order[PARTY_SIZE + 2];
    int n = EngineOrder(order);
    int a = -1, b = -1, fwd;

    for (int i = 0; i < n; i++)
    {
        if (order[i] == from)
            a = i;
        if (order[i] == to)
            b = i;
    }
    if (a < 0 || b < 0 || a == b)
        return;
    fwd = (b - a + n) % n;
    if (fwd <= n - fwd)
        NativeUi_QueuePresses(DPAD_DOWN, fwd);
    else
        NativeUi_QueuePresses(DPAD_UP, n - fwd);
}

/* the design's grid: two columns, three rows, then the buttons */
static int GridMove(int s, int dir)
{
    int n = sMonCount, last = n - 1;

    if (n <= 0)
        return s;
    if (s == SEL_CANCEL || s == SEL_CONFIRM)
    {
        if (dir == DPAD_UP)
            return last;
        if (dir == DPAD_DOWN)
            return 0;
        if (dir == DPAD_LEFT && s == SEL_CANCEL && sMenu.chooseHalf)
            return SEL_CONFIRM;
        if (dir == DPAD_RIGHT && s == SEL_CONFIRM)
            return SEL_CANCEL;
        return s;
    }
    switch (dir)
    {
    case DPAD_LEFT:
        return (s % 2 == 1) ? s - 1 : s;
    case DPAD_RIGHT:
        return (s % 2 == 0 && s + 1 < n) ? s + 1 : s;
    case DPAD_UP:
        return s >= 2 ? s - 2 : SEL_CANCEL;
    case DPAD_DOWN:
        if (s + 2 < n)
            return s + 2;
        if ((s / 2 + 1) * 2 < n)
            return last; /* the next row is shorter */
        return sMenu.chooseHalf ? SEL_CONFIRM : SEL_CANCEL;
    }
    return s;
}

/* ---------------------------------------------------------------- capture */

static bool Detect(void)
{
    struct NativePartyMenu m;

    if (!PartyMenu_NativeUiState(&m))
        return false;
    /* the multi battles' layouts show a partner's Pokémon: the game's own screen */
    if (m.layout != PARTY_LAYOUT_SINGLE && m.layout != PARTY_LAYOUT_DOUBLE)
        return false;
    return m.menuType != PARTY_MENU_TYPE_MULTI_SHOWCASE && m.menuType != PARTY_MENU_TYPE_MULTI_FULL_SHOWCASE;
}

static void CaptureFollowing(void)
{
    struct ObjectEvent *follower;
    struct Pokemon *mon;

    sFollowing = -1;
    if (sMenu.menuType != PARTY_MENU_TYPE_FIELD)
        return;
    follower = GetFollowerObject();
    mon = GetFirstLiveMon();
    if (follower != NULL && !follower->invisible && mon != NULL)
        sFollowing = (s8)(mon - gParties[B_TRAINER_PLAYER]);
}

static void Capture(void)
{
    u8 prevPhase = sMenu.phase;
    bool8 active;
    u32 printed;
    int info;

    PartyMenu_NativeUiState(&sMenu);
    sMonCount = 0;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        UiMon_FromPokemon(&sMons[i], &gParties[B_TRAINER_PLAYER][i]);
        if (sMons[i].species == SPECIES_NONE)
            continue;
        sMonCount = i + 1;
        UiGfx_PrepareMonPic(sMons[i].species, sMons[i].personality, sMons[i].shiny, sMons[i].isEgg);
        if (sMons[i].heldItem != ITEM_NONE)
            UiGfx_PrepareItemIcon(sMons[i].heldItem);
    }
    CaptureFollowing();
    sHasPrompt = sMenu.promptWindow != WINDOW_NONE
              && TextPrinter_NativeUiWindowText(sMenu.promptWindow, sPrompt, sizeof(sPrompt), &printed, &active);
    sHasMsg = sMenu.msgWindow != WINDOW_NONE
           && TextPrinter_NativeUiWindowText(sMenu.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sMsgPrinting && TextPrinter_NativeUiWindowWaiting(sMenu.msgWindow);
    sItem = sMenu.item;
    sItemName = NULL;
    if (sItem != ITEM_NONE)
    {
        UiGfx_PrepareItemIcon(sItem);
        /* a TM shows its move, as the design does */
        sItemName = GetItemPocket(sItem) == POCKET_TM_HM ? GetMoveName(ItemIdToBattleMoveId(sItem)) : GetItemName(sItem);
    }
    /* the grid's cursor runs ahead of the engine's while its presses land */
    if (!NativeUi_PressesPending() || prevPhase != NATIVE_PARTY_BROWSE)
        sNav = sMenu.cursor;
    info = sNav < PARTY_SIZE && sNav < sMonCount ? sNav : 0;
    if (info != sInfoSlot)
    {
        sInfoSlot = info;
        sInfoT = 0;
    }
    else
    {
        sInfoT++;
    }
    sSwapT = sMenu.phase == NATIVE_PARTY_SWAPPING ? sSwapT + 1 : 0;
    if (sMenu.phase != NATIVE_PARTY_BROWSE)
        sHeldDir = 0;
    /* SELECT's move: once the menu reads buttons, its SWITCH row */
    if (sMoveIntent)
    {
        if (sMenu.phase == NATIVE_PARTY_MENU && sMenu.ready && !NativeUi_PressesPending())
        {
            if (sMenu.switchRow >= 0)
            {
                int d = sMenu.switchRow - sMenu.menuCursor;

                NativeUi_QueuePresses(d > 0 ? DPAD_DOWN : DPAD_UP, abs(d));
                NativeUi_QueuePress(A_BUTTON);
            }
            sMoveIntent = false;
        }
        else if (++sMoveIntentT > 90)
        {
            sMoveIntent = false;
        }
    }
}

/* ---------------------------------------------------------------- drawing */

struct Arcs
{
    int cx;
    int cy;
};

static bool ArcAt(int x, int y, const void *ctx)
{
    const struct Arcs *a = ctx;
    double d = sqrt((double)(x - a->cx) * (x - a->cx) + (double)(y - a->cy) * (y - a->cy));

    return (d >= 150 && d < 168) || (d >= 196 && d < 206) || (d >= 232 && d < 238);
}

/* PK.drawPartyBackground */
static void Background(int t)
{
    int x0, y0, w, h;
    struct UiPattern diag = UiPat_Diag(24, 10, 0, 1);
    struct Arcs arcs;
    char tag[48];

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, UI_P_PARTY_BG);
    UiG_FillPattern(&diag, x0, y0, w, h, UI_P_PARTY_BG_STRIPE, t / 5, 0);
    arcs.cx = 640 + 30 - x0;
    arcs.cy = 360 + 60 - y0;
    snprintf(tag, sizeof(tag), "partyarcs%dx%d:%d,%d", w, h, arcs.cx, arcs.cy);
    UiG_Mask(UiM_Fn(w, h, tag, ArcAt, &arcs), x0, y0, UI_P_PARTY_BG_ARC);
}

static const struct UiMask *SlotMask(void)
{
    static int cuts[12 * 3];

    if (cuts[2] == 0)
    {
        for (int i = 0; i < 10; i++)
        {
            cuts[i * 3] = i;
            cuts[i * 3 + 1] = 1;
            cuts[i * 3 + 2] = 10 - i;
        }
        cuts[30] = -1, cuts[31] = 1, cuts[32] = 2;
        cuts[33] = -2, cuts[34] = 1, cuts[35] = 1;
    }
    return UiM_Carve(UiM_Capsule(SLOT_W, SLOT_H, true, false), cuts, 12, "partyslot");
}

static void Tag(int x, int y, const char *label, UiRGBA colour)
{
    struct UiRamp tr = UiRampOf(colour, 0.4, 0.25, 0.6);
    int tw = UiCaps_Width(label) + 10;

    UiG_Paint(UiM_Capsule(tw, 11, true, true), x, y, tr.base, &(struct UiPaint){tr.ink, tr.hi, tr.lo, 0, 0, NULL, 0});
    UiCaps_Draw(x + (tw >> 1), y + 2, label, UI_P_TEXT_W, tr.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
}

/* what the engine shows in a slot instead of its HP (TM compatibility...) */
static const char *DescTag(int desc, UiRGBA *colour)
{
    switch (desc - 1)
    {
    case PARTYBOX_DESC_ABLE: case PARTYBOX_DESC_ABLE_2: case PARTYBOX_DESC_ABLE_3:
        *colour = UI_HEX(0x48A848); return "ABLE";
    case PARTYBOX_DESC_NOT_ABLE: case PARTYBOX_DESC_NOT_ABLE_2:
        *colour = UI_HEX(0xA0A0A8); return "NOT ABLE";
    case PARTYBOX_DESC_LEARNED: *colour = UI_HEX(0x5890C8); return "LEARNED";
    case PARTYBOX_DESC_NO_USE: *colour = UI_HEX(0xA0A0A8); return "NO USE";
    case PARTYBOX_DESC_FIRST: *colour = UI_P_SEL; return "FIRST";
    case PARTYBOX_DESC_SECOND: *colour = UI_P_SEL; return "SECOND";
    case PARTYBOX_DESC_THIRD: *colour = UI_P_SEL; return "THIRD";
    case PARTYBOX_DESC_FOURTH: *colour = UI_P_SEL; return "FOURTH";
    case PARTYBOX_DESC_HAVE: *colour = UI_HEX(0x5890C8); return "HAVE";
    case PARTYBOX_DESC_DONT_HAVE: *colour = UI_HEX(0xA0A0A8); return "DON'T HAVE";
    default: return NULL;
    }
}

/* PK.drawPartySlot */
static void Slot(int x, int y, int i, bool selected, bool carried, int frame, int hop)
{
    const struct UiMon *mon = &sMons[i];
    bool fainted = !mon->isEgg && mon->hp == 0;
    const struct SlotRamp *r = selected ? &sSlotSel : fainted ? &sSlotFnt : &sSlot;
    const struct UiMask *m = SlotMask();
    struct UiPattern below = UiPat_Below(26), stripes = UiPat_HStripe(2, 1, 0);
    const struct UiMask *band;
    UiRGBA dbase = UiMix(r->lo, r->ink, 0.22);
    static const struct UiFilter sGray = {0.65f, 0, 0, 0};
    const char *tag;
    UiRGBA tagColour = 0;
    int nx;

    stripes.and_ = &below;
    if (carried)
        UiW_CarryFrame(m, x, y);
    else if (selected)
        UiW_SelectFrame(m, x, y, UI_NONE, UI_NONE, 2);
    UiG_Paint(m, x, y, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 0, &stripes, r->stripe});
    band = UiM_Window(UiM_Erode4(m), 52, 4, SLOT_W, 22);
    UiG_Mask(band, x, y, r->lo);
    UiG_Mask(UiM_Sub(band, UiM_Shift(band, 0, 1)), x, y, UiMix(r->lo, r->ink, 0.25));
    UiG_Paint(UiM_Circle(40), x + 10, y + 14, dbase,
              &(struct UiPaint){r->ink, UiMix(dbase, r->ink, 0.35), UiMix(dbase, r->hi, 0.3), 0, 0, NULL, 0});
    UiGfx_MonIcon(mon->species, mon->personality, mon->isEgg, frame, x + 14, y + 11 - hop, fainted ? &sGray : NULL);
    if (mon->heldItem != ITEM_NONE)
        UiGfx_Icon("held_item", x + 38, y + 40, 0);
    nx = UiText_Draw(UI_FONT_NORMAL, x + 56, y + 5, mon->nickname, UI_P_TEXT_W, r->ink, UI_ALIGN_LEFT, 0);
    /* an Egg shows its name only, as the game does */
    if (mon->isEgg)
        return;
    UiW_Gender(nx + 1, y + 5, mon->gender);
    {
        u8 lv[16];

        UiMon_LevelText(lv, sizeof(lv), mon->level);
        UiText_Draw(UI_FONT_NORMAL, x + 186, y + 5, lv, UI_P_TEXT_W, r->ink, UI_ALIGN_RIGHT, 0);
    }
    UiW_HpBar(x + 58, y + 29, 115, mon->maxHP > 0 ? (double)mon->hp / mon->maxHP : 0, true, r->ink);
    {
        char hp[16];

        snprintf(hp, sizeof(hp), "%u/%u", (unsigned)mon->hp, (unsigned)mon->maxHP);
        UiCaps_Draw(x + 186, y + 44, hp, UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_RIGHT, UI_NONE);
    }
    tag = DescTag(sMenu.slotDesc[i], &tagColour);
    if (tag != NULL)
    {
        Tag(x + 58, y + 42, tag, tagColour);
    }
    else
    {
        UiRGBA sc;
        const char *status = UiMon_StatusLabel(mon, &sc);

        if (status != NULL)
        {
            UiW_StatusBadge(x + 58, y + 42, status, sc, 0);
        }
        else if (i == sFollowing)
        {
            UiGfx_Icon("footprints", x + 60, y + 44, 0);
            UiCaps_Draw(x + 72, y + 44, "FOLLOWING", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE4, UI_ALIGN_LEFT, UI_NONE);
        }
    }
}

/* PK.drawPartyInfo: the DS top screen's job */
static void InfoPane(int x, int y, int w, int h, const struct UiMon *mon)
{
    struct UiPaint edge = {UI_R_SLOT_INK, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, 0, NULL, 0};
    int sx = x + 6, sy = y + 26, sw = w - 12, sh = 112;
    const struct UiRamp *t0 = &gUiTypeRamps[mon->types[0] >= 0 ? mon->types[0] : UI_TYPE_NORMAL];
    UiRGBA light = UiMix(t0->hi, UI_HEX(0xFFFFFF), 0.55);
    struct UiPattern diag = UiPat_Diag(16, 6, 0, 1);
    struct UiRamp pr = UiRampDefault(UiMix(t0->base, UI_HEX(0x808080), 0.35));
    int nx, frame, ry, my, tx;

    UiG_Paint(UiM_Rounded(w, h, UiCornerR(3), UiCornerChamfer(10), UiCornerR(3), UiCornerR(3)), x, y, UI_P_CREAM, &edge);
    UiG_Paint(UiM_Rounded(w - 6, 20, UiCornerR(2), UiCornerChamfer(7), UiCornerR(0), UiCornerR(0)), x + 3, y + 3,
              UI_R_SLOT_LO, &(struct UiPaint){UI_NONE, UI_R_SLOT_BASE, UI_NONE, 0, 0, NULL, 0});
    nx = UiText_Draw(UI_FONT_NORMAL, x + 10, y + 5, mon->nickname, UI_P_TEXT_W, UI_R_SLOT_INK, UI_ALIGN_LEFT, 0);
    if (!mon->isEgg)
    {
        u8 lv[16];

        UiW_Gender(nx + 1, y + 5, mon->gender);
        UiMon_LevelText(lv, sizeof(lv), mon->level);
        UiText_Draw(UI_FONT_NORMAL, x + w - 16, y + 5, lv, UI_P_TEXT_W, UI_R_SLOT_INK, UI_ALIGN_RIGHT, 0);
    }
    UiG_Paint(UiM_Rounded4(sw, sh, 2, 2, 2, 2), sx, sy, light,
              &(struct UiPaint){UiMix(t0->lo, UI_P_INK, 0.3), UI_NONE, UI_NONE, 0, 0, &diag,
                                UiMix(t0->hi, UI_HEX(0xFFFFFF), 0.25)});
    UiG_Paint(UiM_Ellipse(84, 20), sx + (sw >> 1) - 42, sy + sh - 26, pr.base,
              &(struct UiPaint){pr.ink, pr.hi, pr.lo, 0, 0, NULL, 0});
    /* the game's own front picture, with its own animation when it comes up */
    frame = UiGfx_MonPicFrameAt(mon->species, mon->personality, mon->shiny, mon->isEgg, sInfoT);
    UiGfx_MonPic(mon->species, mon->personality, mon->shiny, mon->isEgg, frame, sx + (sw >> 1), sy + sh - 15, NULL);
    if (mon->shiny)
        UiCaps_Draw(sx + 5, sy + 5, UI_CAPS_STAR, UI_P_SEL, UI_P_INK, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    if (mon->isEgg)
        return;
    tx = x + 8;
    for (int i = 0; i < 2; i++)
        if (mon->types[i] >= 0)
            tx = UiW_TypeBadge(tx, y + 144, mon->types[i], 0) + 3;
    ry = y + 162;
    UiCaps_Draw(x + 10, ry + 5, "ABILITY", UI_R_SLOT_LO, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_Text(x + 64, ry, mon->abilityName != NULL ? mon->abilityName : sText_Dash);
    ry += 18;
    UiCaps_Draw(x + 10, ry + 5, "ITEM", UI_R_SLOT_LO, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (mon->heldItem != ITEM_NONE)
    {
        UiGfx_ItemIcon(mon->heldItem, x + 58, ry - 4, NULL);
        UiG_Text(x + 82, ry, mon->heldItemName);
    }
    else
    {
        UiG_Text(x + 64, ry, sText_None);
    }
    my = y + 202;
    UiG_HLine(x + 6, my - 4, w - 12, UI_P_CREAM_LO);
    for (int i = 0; i < mon->moveCount; i++)
    {
        const struct UiRamp *tr = &gUiTypeRamps[mon->moveTypes[i] >= 0 ? mon->moveTypes[i] : UI_TYPE_NORMAL];
        char pp[16];

        UiG_Paint(UiM_Rounded4(6, 13, 1, 1, 1, 1), x + 9, my + 2, tr->base,
                  &(struct UiPaint){tr->ink, tr->hi, UI_NONE, 0, 0, NULL, 0});
        UiG_Text(x + 19, my, mon->moveNames[i]);
        UiMicro_Draw(x + w - 62, my + 6, "PP", UI_R_SLOT_LO);
        snprintf(pp, sizeof(pp), "%u/%u", (unsigned)mon->pp[i], (unsigned)mon->maxPP[i]);
        UiCaps_Draw(x + w - 12, my + 5, pp, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        my += 17;
    }
    UiG_HLine(x + 6, my + 1, w - 12, UI_P_CREAM_LO);
    UiW_ExpBar(x + 10, my + 9, w - 40, mon->expFrac, true);
}

static const u8 *ActionVerb(void)
{
    switch (sMenu.action)
    {
    case PARTY_ACTION_USE_ITEM: return sText_Use;
    case PARTY_ACTION_GIVE_ITEM: case PARTY_ACTION_GIVE_PC_ITEM: case PARTY_ACTION_GIVE_MAILBOX_MAIL: return sText_Give;
    case PARTY_ACTION_MOVE_TUTOR: return sText_Teach;
    default: return sText_Select;
    }
}

static void Hints(void)
{
    struct UiHintItem h[3];
    int n = 0;

    switch (sMenu.phase)
    {
    case NATIVE_PARTY_BROWSE:
        if (sMenu.action == PARTY_ACTION_SWITCH)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_PutHere};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        }
        else if (sItem != ITEM_NONE && sMenu.menuType == PARTY_MENU_TYPE_FIELD)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, ActionVerb()};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_BackToBag};
        }
        else
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, ActionVerb()};
            if (sMenu.action == PARTY_ACTION_CHOOSE_MON && sMenu.menuType == PARTY_MENU_TYPE_FIELD)
                h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Move};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        }
        break;
    case NATIVE_PARTY_MENU:
    case NATIVE_PARTY_MOVES:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_PARTY_YES_NO:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Select};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        break;
    case NATIVE_PARTY_LEVEL_UP:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    case NATIVE_PARTY_MESSAGE:
        if (sMsgPrinting && !sMsgWaiting)
            UiText_Draw(UI_FONT_SMALL, 226, 313, sText_Wait, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);
        else if (sHasMsg)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    default:
        break;
    }
    if (n > 0)
        UiW_Hints(224, 312, h, n, true, 0);
}

static void LevelUpBox(int t)
{
    static const char *const sLabels[6] = {"MAX HP", "ATTACK", "DEFENSE", "SP. ATK", "SP. DEF", "SPEED"};
    int w = 150, h = 6 * 16 + 14, x = 632 - w, y = 246 - h;

    (void)t;
    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    for (int i = 0; i < 6; i++)
    {
        int ry = y + 7 + i * 16;
        u8 v[16];

        UiCaps_Draw(x + 12, ry + 5, sLabels[i], UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        v[0] = EOS;
        if (sMenu.levelUpPage == 1)
        {
            UiText_AppendAscii(v, sizeof(v), "+");
            UiText_AppendNumber(v, sizeof(v), (long)sMenu.levelUpNew[i] - (long)sMenu.levelUpOld[i], 1, false);
        }
        else
        {
            UiText_AppendNumber(v, sizeof(v), sMenu.levelUpNew[i], 1, false);
        }
        UiG_TextEx(x + w - 12, ry, v, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
    }
    UiG_Hit(HIT_MESSAGE, 0, x, y, w, h);
}

static void MovesBox(int t)
{
    const struct UiMon *mon = &sMons[sMenu.slotId < PARTY_SIZE ? sMenu.slotId : 0];
    struct UiMenuItem items[MAX_MON_MOVES];
    int box[4];

    for (int i = 0; i < mon->moveCount; i++)
        items[i] = (struct UiMenuItem){mon->moveNames[i], false};
    UiW_MenuBox(632, 246, items, mon->moveCount, sMenu.menuCursor,
                &(struct UiMenuOpt){.anchor = UI_ANCHOR_BOTTOMRIGHT, .t = t, .minW = 176, .hitId = HIT_MOVE}, box);
    for (int i = 0; i < mon->moveCount; i++)
    {
        char pp[16];
        int ry = box[1] + 7 + i * 16;

        UiMicro_Draw(box[0] + box[2] - 58, ry + 6, "PP", UI_P_TEXT_MUTED);
        snprintf(pp, sizeof(pp), "%u/%u", (unsigned)mon->pp[i], (unsigned)mon->maxPP[i]);
        UiCaps_Draw(box[0] + box[2] - 10, ry + 5, pp, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
}

static void Draw(int t)
{
    struct UiRamp tab = {UI_R_SLOT_HI, UI_R_SLOT_LO, UI_HEX(0x2E6098), UI_R_SLOT_INK};
    int box[4];
    int boxW = sMenu.chooseHalf ? 252 : 330;
    bool menuUp = sMenu.phase == NATIVE_PARTY_MENU || sMenu.phase == NATIVE_PARTY_MOVES
               || sMenu.phase == NATIVE_PARTY_YES_NO || sMenu.phase == NATIVE_PARTY_LEVEL_UP;

    Background(t);
    UiW_TitleTab(0, 8, "POK" UI_CAPS_E_ACUTE "MON", &tab, 0, NULL);
    if (sMonCount > 0)
        InfoPane(8, 32, 208, 294, &sMons[sInfoSlot >= 0 ? sInfoSlot : 0]);
    for (int i = 0; i < sMonCount; i++)
    {
        int px = 224 + (i % 2) * (SLOT_W + 8), py = 34 + (i / 2) * (SLOT_H + 6);
        bool carried = i == sMenu.carried || (sMenu.phase == NATIVE_PARTY_SWAPPING && i == sMenu.swapWith);
        bool selected = i == sNav && !carried && sMenu.phase != NATIVE_PARTY_SWAPPING;
        int frame, hop = 0;

        if (selected)
        {
            frame = (t / 10) % 2;
            hop = frame == 1 ? 2 : 0;
        }
        else
        {
            frame = sMons[i].isEgg || sMons[i].hp > 0 ? ((t + i * 7) / 20) % 2 : 0;
        }
        Slot(px, py - (carried ? 2 : 0), i, selected, carried, frame, hop);
        UiG_Hit(HIT_SLOT, i, px, py, SLOT_W, SLOT_H);
    }
    if (sItemName != NULL)
    {
        int fw;

        UiG_Paint(UiM_Rounded4(116, 30, 2, 2, 2, 2), 100, 6, UI_P_CREAM,
                  &(struct UiPaint){UI_R_SLOT_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
        UiGfx_ItemIcon(sItem, 103, 9, NULL);
        fw = UiText_Width(UI_FONT_NORMAL, sItemName, 0);
        UiText_Draw(fw > 84 ? UI_FONT_NARROW : UI_FONT_NORMAL, 128, 13, sItemName, UI_P_TEXT, UI_P_TEXT_SH,
                    UI_ALIGN_LEFT, 0);
    }
    UiW_DialogBox(224, 252, boxW, 46, NULL, UI_NONE, box);
    if (sHasMsg)
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, box[0] + 2, box[1] + 1, box[2] - 4, 2);
    else if (sHasPrompt)
        UiW_MessagePage(sPrompt, -1, box[0] + 2, box[1] + 1, box[2] - 4, 2);
    if (sHasMsg && (sMsgWaiting || (sMenu.phase == NATIVE_PARTY_MESSAGE && !sMsgPrinting)))
        UiW_DownArrow(box[0] + box[2] - 8, box[1] + 22 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_MESSAGE, 0, 224, 252, boxW, 46);
    if (sMenu.chooseHalf)
    {
        UiW_DsButton(484, 252, 70, 46, UI_P_CMD_POKEMON, sText_Confirm,
                     &(struct UiDsButtonOpt){.noChip = true, .selected = sNav == SEL_CONFIRM});
        UiG_Hit(HIT_CONFIRM, 0, 484, 252, 70, 46);
    }
    UiW_DsButton(562, 252, 70, 46, UI_P_CMD_FIGHT, sText_Back,
                 &(struct UiDsButtonOpt){.icon = "return_white", .noChip = true, .selected = sNav == SEL_CANCEL});
    UiG_Hit(HIT_BACK, 0, 562, 252, 70, 46);
    Hints();
    if (menuUp)
    {
        int bx, by, bw, bh;

        /* a click outside the menu backs out of it, as the design's does */
        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Hit(HIT_BACKDROP, 0, bx, by, bw, bh);
    }
    if (sMenu.phase == NATIVE_PARTY_MENU && sMenu.numActions > 0)
    {
        struct UiMenuItem items[8];

        for (int i = 0; i < sMenu.numActions; i++)
            items[i] = (struct UiMenuItem){sMenu.actionLabels[i], false};
        UiW_MenuBox(632, 246, items, sMenu.numActions, sMenu.menuCursor,
                    &(struct UiMenuOpt){.anchor = UI_ANCHOR_BOTTOMRIGHT, .t = t, .minW = 112}, NULL);
    }
    else if (sMenu.phase == NATIVE_PARTY_MOVES)
    {
        MovesBox(t);
    }
    else if (sMenu.phase == NATIVE_PARTY_YES_NO)
    {
        if (!sMsgPrinting)
            UiW_YesNo(632, 246, sMenu.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (sMenu.phase == NATIVE_PARTY_LEVEL_UP)
    {
        LevelUpBox(t);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    int dir = 0;

    if (sMenu.phase != NATIVE_PARTY_BROWSE || sMonCount == 0)
    {
        sHeldDir = 0;
        return keys;
    }
    /* the grid's directions, with the engine's own key repeat */
    if (newKeys & DPAD_ALL)
    {
        dir = (newKeys & DPAD_UP) ? DPAD_UP : (newKeys & DPAD_DOWN) ? DPAD_DOWN
            : (newKeys & DPAD_LEFT) ? DPAD_LEFT : DPAD_RIGHT;
        sHeldDir = dir;
        sHeldFrames = 0;
    }
    else if (sHeldDir != 0 && (keys & sHeldDir))
    {
        int start = gKeyRepeatStartDelay > 0 ? gKeyRepeatStartDelay : 40;
        int every = gKeyRepeatContinueDelay > 0 ? gKeyRepeatContinueDelay : 5;

        sHeldFrames++;
        if (sHeldFrames >= start && (sHeldFrames - start) % every == 0)
            dir = sHeldDir;
    }
    else
    {
        sHeldDir = 0;
    }
    if (dir != 0 && sMenu.ready)
    {
        int to = GridMove(sNav, dir);

        if (to != sNav)
        {
            QueuePath(sNav, to);
            sNav = to;
        }
    }
    keys &= ~DPAD_ALL;
    /* SELECT on a Pokémon moves it: A, then the menu's SWITCH row */
    if ((newKeys & SELECT_BUTTON) && sMenu.action == PARTY_ACTION_CHOOSE_MON
     && sMenu.menuType == PARTY_MENU_TYPE_FIELD && sNav < sMonCount && !sMoveIntent)
    {
        NativeUi_QueuePress(A_BUTTON);
        sMoveIntent = true;
        sMoveIntentT = 0;
    }
    keys &= ~SELECT_BUTTON;
    /* buttons pressed while the cursor's presses land wait their turn */
    if (NativeUi_PressesPending())
    {
        if (newKeys & A_BUTTON)
            NativeUi_QueuePress(A_BUTTON);
        else if (newKeys & B_BUTTON)
            NativeUi_QueuePress(B_BUTTON);
        keys &= ~(A_BUTTON | B_BUTTON | START_BUTTON);
    }
    return keys;
}

static void QueueRow(int from, int to)
{
    int d = to - from;

    NativeUi_QueuePresses(d > 0 ? DPAD_DOWN : DPAD_UP, abs(d));
    NativeUi_QueuePress(A_BUTTON);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (NativeUi_PressesPending())
        return true;
    switch (sMenu.phase)
    {
    case NATIVE_PARTY_BROWSE:
        if (hit->id == HIT_SLOT && hit->index < sMonCount)
        {
            QueuePath(sNav, hit->index);
            sNav = hit->index;
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        if (hit->id == HIT_CONFIRM && sMenu.chooseHalf)
        {
            QueuePath(sNav, SEL_CONFIRM);
            sNav = SEL_CONFIRM;
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        if (hit->id == HIT_BACK)
        {
            NativeUi_QueuePress(B_BUTTON);
            return true;
        }
        return false;
    case NATIVE_PARTY_MENU:
        if (hit->id == UI_HIT_MENU)
            QueueRow(sMenu.menuCursor, hit->index);
        else
            NativeUi_QueuePress(B_BUTTON);
        return true;
    case NATIVE_PARTY_MOVES:
        if (hit->id == HIT_MOVE)
            QueueRow(sMenu.menuCursor, hit->index);
        else
            NativeUi_QueuePress(B_BUTTON);
        return true;
    case NATIVE_PARTY_YES_NO:
        if (hit->id == UI_HIT_YESNO)
            QueueRow(sMenu.menuCursor, hit->index);
        else
            NativeUi_QueuePress(B_BUTTON);
        return true;
    case NATIVE_PARTY_LEVEL_UP:
    case NATIVE_PARTY_MESSAGE:
        if (hit->id == HIT_MESSAGE || hit->id == HIT_BACKDROP)
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool Ready(void)
{
    return sMenu.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Party = {
    .name = "party",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
