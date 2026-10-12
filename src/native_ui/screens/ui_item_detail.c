#ifdef ENABLE_NATIVE_UI
/*
 * 16 · Item details (the approved design's preview/js/screens/item_detail.js):
 * the display case (the item's own icon at 2x on a cushioned plinth under a
 * spotlight, a brass plaque) and the index card (price tags, the game's
 * description, what the item does, who holds it, TM move data with the
 * party's real compatibility, the Pokémon a Mega Stone belongs to), opened
 * from the bag. The game has no such screen: this is the UI's own view over
 * the bag's list, and nothing in the game changes while it is up. What the
 * item does comes from its field, battle and hold data, never guessed from
 * its name. The design's favourites are not in the game (left out); the
 * Shiny Charm's multiplayer rules are not stated by the game (left out);
 * registering stays the bag's own REGISTER. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "item_use.h"
#include "move.h"
#include "pokemon.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/form_change_types.h"
#include "constants/hold_effects.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_item_detail.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ARROW = UI_HIT_SCREEN + 40,
    HIT_STRIP,
    HIT_BACKDROP,
};

#define STRIP_VISIBLE 12

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_NoneInBag[] = _("None in Bag");
static const u8 sText_NotSold[] = _("not sold in marts");
static const u8 sText_NotForSale[] = _("not for sale");
static const u8 sText_ExpensiveByDesign[] = _("expensive by design");
static const u8 sText_UseField[] = _("Use it from the Bag outside battle.");
static const u8 sText_UseBattle[] = _("Use it from the Bag during battle.");
static const u8 sText_UseBall[] = _("Throw it at a wild Pokémon to catch it.");
static const u8 sText_BallNote[] = _("It can't be used on a Trainer's Pokémon.");
static const u8 sText_UseHold[] = _("A Pokémon can hold it for its effect.");
static const u8 sText_UseBerry[] = _("A Pokémon holding it eats it in battle.");
static const u8 sText_KeyNote[] = _("Key items can't be sold or tossed.");
static const u8 sText_NoUse[] = _("It has no use of its own.");
static const u8 sText_Registered[] = _("Registered: ");
static const u8 sText_NothingRegistered[] = _("Nothing is registered.");
static const u8 sText_RegisterHow[] = _("Register a key item from the Bag's own menu.");
static const u8 sText_Mega[] = _("Mega ");
static const u8 sText_IsHolding[] = _(" is holding it.");
static const u8 sText_CanHold[] = _(" in your party can hold it.");
static const u8 sText_NoOneOf1[] = _("No ");
static const u8 sText_NoOneOf2[] = _(" in your party.");
static const u8 sText_HaveRing[] = _("You have the Mega Ring.");
static const u8 sText_NeedRing[] = _("Needs the Mega Ring.");
static const u8 sText_OnePerBattle[] = _("One Mega Evolution per trainer per battle.");
static const u8 sText_Browse[] = _("Browse");
static const u8 sText_Back[] = _("Back");

/* ---------------------------------------------------------------- state */

struct Use
{
    const char *chip;
    const u8 *text;
};

static bool sOpen;
static int sIndex;      /* the shown item (set at capture) */
static int sWant;       /* the item asked for (browsing between captures) */
static int sOpenT;
static int sPressT;
static int sPress;
static u16 sItems[NATIVE_BAG_MAX_ITEMS];
static u16 sQty[NATIVE_BAG_MAX_ITEMS];
static int sCount;
static u8 sPocket;
static u16 sRegistered;
/* the shown item, read once when it comes up */
static u16 sItem;
static u32 sBuy;
static u32 sSell;
static bool sImportant;
static u16 sMove;
static u16 sMegaBase;
static u16 sMegaForm;
static struct Use sUses[5];
static int sUseCount;
static bool sHpMode;
static bool sHasRing;
static struct UiMon sParty[PARTY_SIZE];
static s8 sTeach[PARTY_SIZE];       /* TMs: 0 not able, 1 able, 2 knows it */
static int sPartyCount;

void UiItemDetail_Open(const struct NativeBag *bag, int index)
{
    if (bag == NULL || index < 0 || index >= bag->count)
        return;
    sOpen = true;
    sIndex = sWant = index;
    sOpenT = 0;
    sPressT = 0;
    sItem = ITEM_NONE;
}

void UiItemDetail_Close(void)
{
    sOpen = false;
}

bool UiItemDetail_IsOpen(void)
{
    return sOpen;
}

int UiItemDetail_Index(void)
{
    return sIndex;
}

static void Browse(int d)
{
    if (sCount > 1)
    {
        sWant = (sWant + d + sCount) % sCount;
        sPress = d;
        sPressT = 6;
    }
}

/* the species a Mega Stone belongs to, and its Mega form (the game's form changes) */
static void FindMega(u16 item)
{
    sMegaBase = sMegaForm = SPECIES_NONE;
    if (GetItemHoldEffect(item) != HOLD_EFFECT_MEGA_STONE)
        return;
    for (u32 s = 1; s < NUM_SPECIES && sMegaBase == SPECIES_NONE; s++)
    {
        const struct FormChange *fc = GetSpeciesFormChanges(s);

        if (!IsSpeciesEnabled(s))
            continue;
        for (int i = 0; fc != NULL && fc[i].method != FORM_CHANGE_TERMINATOR; i++)
            if (fc[i].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM && fc[i].param1 == item)
            {
                sMegaBase = s;
                sMegaForm = fc[i].targetSpecies;
                break;
            }
    }
}

static void AddUse(const char *chip, const u8 *text)
{
    if (sUseCount < (int)(sizeof(sUses) / sizeof(sUses[0])))
        sUses[sUseCount++] = (struct Use){chip, text};
}

static void ReadItem(u16 item)
{
    ItemUseFunc field = GetItemFieldFunc(item);
    enum EffectItem battle = GetItemBattleUsage(item);
    enum HoldEffect hold = GetItemHoldEffect(item);

    sItem = item;
    sBuy = GetItemPrice(item);
    sSell = GetItemSellPrice(item);
    sImportant = GetItemImportance(item) != 0;
    sMove = GetItemPocket(item) == POCKET_TM_HM ? GetItemTMHMMoveId(item) : MOVE_NONE;
    FindMega(item);
    sUseCount = 0;
    sHpMode = battle == EFFECT_ITEM_RESTORE_HP || battle == EFFECT_ITEM_CURE_STATUS || battle == EFFECT_ITEM_HEAL_AND_CURE_STATUS
           || battle == EFFECT_ITEM_REVIVE || battle == EFFECT_ITEM_RESTORE_PP;
    if (battle == EFFECT_ITEM_THROW_BALL)
    {
        AddUse("BATTLE", sText_UseBall);
        AddUse("NOTE", sText_BallNote);
    }
    else
    {
        if (field != NULL && field != ItemUseOutOfBattle_CannotUse)
            AddUse("FIELD", sText_UseField);
        if (battle != 0)
            AddUse("BATTLE", sText_UseBattle);
        if (hold != HOLD_EFFECT_NONE && hold != HOLD_EFFECT_MEGA_STONE)
            AddUse("HOLD", GetItemPocket(item) == POCKET_BERRIES ? sText_UseBerry : sText_UseHold);
        if (sImportant)
            AddUse("NOTE", sText_KeyNote);
        if (sUseCount == 0)
            AddUse("NOTE", sText_NoUse);
    }
    UiGfx_PrepareItemIcon(item);
    if (sMegaForm != SPECIES_NONE)
        UiGfx_PrepareMonPic(sMegaForm, 0, false, false);
}

void UiItemDetail_Capture(const struct NativeBag *bag)
{
    if (!sOpen)
        return;
    sCount = bag->count;
    sPocket = bag->pocket;
    sRegistered = bag->registeredItem;
    for (int i = 0; i < sCount; i++)
    {
        sItems[i] = bag->items[i];
        sQty[i] = bag->quantities[i];
    }
    if (sCount == 0)
    {
        sOpen = false;
        return;
    }
    if (sWant >= sCount)
        sWant = sCount - 1;
    if (sWant < 0)
        sWant = 0;
    sIndex = sWant;
    sOpenT++;
    if (sPressT > 0)
        sPressT--;
    if (sItems[sIndex] != sItem)
        ReadItem(sItems[sIndex]);
    for (int j = 0; j < STRIP_VISIBLE + 2 && j < sCount; j++)
    {
        int first = sIndex - 5 < 0 ? 0 : sIndex - 5;

        if (first > sCount - STRIP_VISIBLE)
            first = sCount - STRIP_VISIBLE < 0 ? 0 : sCount - STRIP_VISIBLE;
        if (first + j < sCount)
            UiGfx_PrepareItemIcon(sItems[first + j]);
    }
    sPartyCount = 0;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        UiMon_FromPokemon(&sParty[i], &gParties[B_TRAINER_PLAYER][i]);
        if (sParty[i].species == SPECIES_NONE)
            continue;
        sPartyCount = i + 1;
        if (sParty[i].heldItem != ITEM_NONE)
            UiGfx_PrepareItemIcon(sParty[i].heldItem);
        sTeach[i] = 0;
        if (sMove != MOVE_NONE && !sParty[i].isEgg)
        {
            for (int m = 0; m < sParty[i].moveCount; m++)
                if (sParty[i].moves[m] == sMove)
                    sTeach[i] = 2;
            if (sTeach[i] == 0 && CanLearnTeachableMove(sParty[i].species, sMove))
                sTeach[i] = 1;
        }
    }
    sHasRing = CheckBagHasItem(ITEM_MEGA_RING, 1);
    if (sRegistered != ITEM_NONE)
        UiGfx_PrepareItemIcon(sRegistered);
}

/* ---------------------------------------------------------------- pieces */

/* the item's name as the bag lists it: a TM or HM with its move */
static const u8 *Title(u8 *dst, int size)
{
    dst[0] = EOS;
    UiText_Append(dst, size, GetItemName(sItem));
    if (sMove != MOVE_NONE)
    {
        UiText_AppendAscii(dst, size, " ");
        UiText_Append(dst, size, GetMoveName(sMove));
    }
    return dst;
}

static UiRGBA PocketColour(int pocket)
{
    switch (pocket)
    {
    case POCKET_POKE_BALLS: return UI_P_POCKETS_BALLS;
    case POCKET_TM_HM: return UI_P_POCKETS_TMS;
    case POCKET_BERRIES: return UI_P_POCKETS_BERRIES;
    case POCKET_KEY_ITEMS: return UI_P_POCKETS_KEY;
    default: return UI_P_POCKETS_ITEMS;
    }
}

static const char *PocketCaps(int pocket)
{
    switch (pocket)
    {
    case POCKET_POKE_BALLS: return "POK" UI_CAPS_E_ACUTE " BALLS";
    case POCKET_TM_HM: return "TMS & HMS";
    case POCKET_BERRIES: return "BERRIES";
    case POCKET_KEY_ITEMS: return "KEY ITEMS";
    default: return "ITEMS";
    }
}

static UiRGBA ChipColour(const char *label)
{
    if (strcmp(label, "FIELD") == 0)
        return UI_P_CMD_POKEMON;
    if (strcmp(label, "BATTLE") == 0)
        return UI_P_CMD_FIGHT;
    if (strcmp(label, "HOLD") == 0)
        return UI_HEX(0x6870D8);
    if (strcmp(label, "SELL") == 0)
        return UI_P_POCKETS_KEY;
    return UiMix(UI_P_TEXT_MUTED, UI_P_PAPER_LO, 0.3);
}

static int Chip(int x, int y, const char *label)
{
    struct UiRamp r = UiRampOf(ChipColour(label), 0.4, 0.25, 0.6);
    int w = 46;

    UiG_Paint(UiM_Capsule(w, 11, true, true), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, 0});
    UiCaps_Draw(x + (w >> 1), y + 2, label, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    return x + w;
}

struct BigCapsCtx
{
    const struct UiMask *m;
    UiRGBA fg;
    UiRGBA ink;
};

static void BuildBigCaps(struct UiBitmap *bm, const void *ctx)
{
    const struct BigCapsCtx *c = ctx;
    const struct UiMask *big = UiM_Pad(c->m, 1, 1, 1, 1);

    UiBitmap_FillMask(bm, UiM_Sub(UiM_Dilate8(big), big), 0, 0, c->ink);
    UiBitmap_FillMask(bm, c->m, 1, 1, c->fg);
}

/* I.bigCaps: an outlined Route Caps label at 2x */
static void BigCaps(int x, int y, const char *text, UiRGBA fg, UiRGBA ink)
{
    struct BigCapsCtx c = {UiCaps_Mask(text, false), fg, ink};
    char key[64];
    const struct UiPiece *p;

    snprintf(key, sizeof(key), "bigcaps:%s:%08x:%08x", text, (unsigned)fg, (unsigned)ink);
    p = UiG_Cached(key, c.m->w + 3, c.m->h + 3, BuildBigCaps, &c);
    UiG_BlitPart(p, 0, 0, c.m->w + 3, c.m->h + 3, x, y, 2, false, 1.0);
}

/* I.priceTag: a luggage tag with the price (crossed: none) */
static void PriceTag(int x, int y, int w, u32 price, UiRGBA ink, bool dim)
{
    int h = 15;
    UiRGBA body = dim ? UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.7) : UI_P_PAPER;
    const struct UiMask *m = UiM_Rounded(w, h, UiCornerChamfer((h - 1) >> 1), UiCornerR(2), UiCornerChamfer((h - 1) >> 1), UiCornerR(2));

    UiG_Paint(m, x, y, body, &(struct UiPaint){ink, dim ? UI_NONE : UI_HEX(0xFFFFFF), UiMix(body, UI_P_PAPER_LO, 0.8), 0, 0, NULL, 0});
    UiG_Rect(x + 8, y + 6, 3, 3, UiMix(ink, body, 0.35));
    UiG_Put(x + 9, y + 7, UiMix(ink, body, 0.65));
    UiG_HLine(x + 8, y + 9, 3, UI_HEX(0xFFFFFF));
    if (price == 0)
    {
        UiCaps_Draw(x + 13 + ((w - 17) >> 1), y + 4, "---", UI_HEX(0x909090), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    }
    else
    {
        u8 v[24];

        v[0] = EOS;
        UiText_AppendAscii(v, sizeof(v), "$");
        UiText_AppendNumber(v, sizeof(v), price, 1, true);
        UiText_Draw(UI_FONT_NORMAL, x + w - 5, y - 2, v, dim ? UI_HEX(0x909090) : UI_P_TEXT, dim ? UI_NONE : UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
}

/* ---------------------------------------------------------------- the display case */

static void Display(int t, const struct UiRamp *pr)
{
    int cx = 126, lift = (t / 32) % 2;
    UiRGBA lin = UiMix(pr->lo, pr->ink, 0.45);
    struct UiRamp mb = {UI_P_CREAM_HI, UI_P_CREAM, UI_P_CREAM_LO, UiMix(UI_P_CREAM_LO, UI_P_INK, 0.62)};
    struct UiRamp br = UiRampOf(UI_P_POCKETS_KEY, 0.45, 0.25, 0.62);
    static const int sOuter[] = {66, 0, 154, 0, 212, 230, 8, 230}, sInner[] = {86, 0, 134, 0, 184, 230, 36, 230};
    int px = 18, py = 268, pw = 216, ph = 44;
    char no[24];
    u8 title[48];

    /* the spotlight: two flat bands */
    UiG_Mask(UiM_Poly(220, 232, sOuter, 4), 16, 30, UiMix(lin, pr->hi, 0.16));
    UiG_Mask(UiM_Poly(220, 232, sInner, 4), 16, 30, UiMix(lin, pr->hi, 0.3));
    UiG_Rect(8, 256, 236, 3, UiMix(lin, pr->ink, 0.5));
    UiG_Rect(8, 259, 236, 1, UiMix(lin, pr->hi, 0.2));
    /* the plinth: steps, fluted column, capital */
    UiG_Paint(UiM_Rounded4(116, 9, 1, 1, 0, 0), cx - 58, 248, mb.base, &(struct UiPaint){mb.ink, mb.hi, mb.lo, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(100, 8, 1, 1, 0, 0), cx - 50, 241, mb.base, &(struct UiPaint){mb.ink, mb.hi, mb.lo, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rect(76, 50), cx - 38, 192, mb.base, &(struct UiPaint){mb.ink, mb.hi, mb.lo, 0, 0, NULL, 0});
    for (int fx = cx - 30; fx <= cx + 28; fx += 8)
    {
        UiG_VLine(fx, 194, 46, mb.lo);
        UiG_VLine(fx + 1, 194, 46, mb.hi);
    }
    UiG_Paint(UiM_Rounded4(100, 9, 1, 1, 1, 1), cx - 50, 185, mb.base, &(struct UiPaint){mb.ink, mb.hi, mb.lo, 0, 0, NULL, 0});
    /* the cushion in the pocket's colour, with the dent the item rests in */
    UiG_Paint(UiM_Ellipse(96, 24), cx - 48, 166, pr->base, &(struct UiPaint){pr->ink, pr->hi, pr->lo, 0, 0, NULL, 0});
    UiG_Mask(UiM_Ellipse(60, 10), cx - 30, 172, UiMix(pr->base, pr->lo, 0.6));
    UiG_Put(cx - 40, 177, pr->ink);
    UiG_Put(cx + 39, 177, pr->ink);
    UiG_Put(cx, 168, pr->ink);
    UiG_Put(cx, 186, pr->ink);
    UiGfx_ItemIconScaled(sItem, cx - 24, 130 - lift, 2);
    /* browse arrows */
    if (sCount > 1)
    {
        int nd = UiW_Nudge(t), pl = sPressT > 0 && sPress < 0, prr = sPressT > 0 && sPress > 0;

        BigCaps(22 - nd + pl, 140 + pl, UI_CAPS_LEFT, UI_P_TEXT_W, pr->ink);
        BigCaps(214 + nd + prr, 140 + prr, UI_CAPS_RIGHT, UI_P_TEXT_W, pr->ink);
        UiG_Hit(HIT_ARROW, -1, 12, 126, 40, 50);
        UiG_Hit(HIT_ARROW, 1, 202, 126, 40, 50);
    }
    /* the brass plaque */
    UiG_Paint(UiM_Rounded4(pw, ph, 2, 2, 2, 2), px, py, br.base, &(struct UiPaint){br.ink, br.hi, br.lo, 0, 2, NULL, 0});
    UiG_Paint(UiM_Rect(pw - 12, ph - 12), px + 6, py + 6, UiMix(br.base, br.hi, 0.35), &(struct UiPaint){UI_NONE, UI_NONE, br.lo, 0, 0, NULL, 0});
    UiG_Rect(px + 3, py + 3, 2, 2, br.ink);
    UiG_Rect(px + pw - 5, py + 3, 2, 2, br.ink);
    UiG_Rect(px + 3, py + ph - 5, 2, 2, br.ink);
    UiG_Rect(px + pw - 5, py + ph - 5, 2, 2, br.ink);
    Title(title, sizeof(title));
    UiText_Draw(UiText_Width(UI_FONT_NORMAL, title, 0) > pw - 16 ? UI_FONT_NARROW : UI_FONT_NORMAL, px + (pw >> 1), py + 6,
                title, br.ink, UiMix(br.hi, UI_HEX(0xFFFFFF), 0.4), UI_ALIGN_CENTER, 0);
    snprintf(no, sizeof(no), "NO. %d / %d", sIndex + 1, sCount);
    UiCaps_Draw(px + (pw >> 1), py + 27, no, UiMix(br.ink, br.lo, 0.3), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
}

/* ---------------------------------------------------------------- the index card */

static void Strip(int x, int y, int w, const struct UiRamp *pr)
{
    int n = sCount, first = sIndex - 5, sy = y + 40;
    char pos[16];
    static const struct UiFilter sDim = {0, 0, 0.85f, 0.96f, 0};

    if (first > n - STRIP_VISIBLE)
        first = n - STRIP_VISIBLE;
    if (first < 0)
        first = 0;
    UiG_Rect(x + 6, y, w - 12, 1, UI_P_CREAM_LO);
    UiG_Rect(x + 6, y + 1, w - 12, 1, UI_P_CREAM_HI);
    UiCaps_Draw(x + 12, y + 3, "IN THIS POCKET", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(pos, sizeof(pos), "%d / %d", sIndex + 1, n);
    UiCaps_Draw(x + w - 12, y + 3, pos, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_Paint(UiM_Rect(w - 20, 4), x + 10, sy + 3, UiMix(pr->base, UI_P_CREAM, 0.35),
              &(struct UiPaint){UiMix(pr->lo, UI_P_INK, 0.3), UiMix(pr->hi, UI_P_CREAM, 0.4), UI_NONE, 0, 0, NULL, 0});
    for (int j = 0; j < STRIP_VISIBLE && first + j < n; j++)
    {
        int idx = first + j, ix = x + 16 + j * 29, iy = sy - 25;
        bool on = idx == sIndex;

        if (on)
        {
            const struct UiMask *m = UiM_Rounded4(28, 26, 2, 2, 2, 2);

            UiW_SelectFrame(m, ix - 2, iy, UI_NONE, UI_NONE, 0);
            UiG_Paint(m, ix - 2, iy, UI_P_PAPER, &(struct UiPaint){UI_NONE, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        }
        UiGfx_ItemIcon(sItems[idx], ix, iy, on ? NULL : &sDim);
        UiG_Hit(HIT_STRIP, idx, ix - 2, iy, 28, 26);
    }
    if (first > 0)
        UiCaps_Draw(x + 4, sy - 16, UI_CAPS_LEFT, UiMix(pr->lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (first + STRIP_VISIBLE < n)
        UiCaps_Draw(x + w - 9, sy - 16, UI_CAPS_RIGHT, UiMix(pr->lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

/* the party with each member's held item; members holding this one are marked */
static void Holders(int t, int x, int y, int w)
{
    int holders = 0;
    char head[24];

    for (int i = 0; i < sPartyCount; i++)
        if (sParty[i].species != SPECIES_NONE && sParty[i].heldItem == sItem)
            holders++;
    UiCaps_Draw(x + 12, y + 2, "YOUR PARTY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sHpMode)
    {
        UiCaps_Draw(x + w - 12, y + 2, "HP AND HELD ITEMS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
    else
    {
        snprintf(head, sizeof(head), holders ? "HELD BY %d" : "NO ONE HOLDS ONE", holders);
        UiCaps_Draw(x + w - 12, y + 2, head, holders ? UiMix(UI_P_POCKETS_KEY, UI_P_INK, 0.35) : UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_RIGHT, UI_NONE);
    }
    for (int i = 0; i < sPartyCount; i++)
    {
        const struct UiMon *m = &sParty[i];
        int cx = x + 12 + i * 60, cy = y + 12;
        bool mine = m->species != SPECIES_NONE && m->heldItem == sItem;
        static const struct UiFilter sGray = {0.65f, 0, 0, 0, 0};

        if (m->species == SPECIES_NONE)
            continue;
        UiG_Paint(UiM_Rounded4(56, 48, 3, 3, 3, 3), cx, cy, mine ? UI_P_PAPER : UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.4),
                  &(struct UiPaint){mine ? UiMix(UI_P_POCKETS_KEY, UI_P_INK, 0.3) : UI_P_CREAM_LO, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiGfx_MonIcon(m->species, m->personality, m->isEgg, mine ? ((t + i * 7) / 20) % 2 : 0, cx - 1, cy - 2,
                      (!m->isEgg && m->hp == 0) ? &sGray : NULL);
        if (m->heldItem != ITEM_NONE)
            UiGfx_ItemIcon(m->heldItem, cx + 30, cy + 6, NULL);
        else
            UiCaps_Draw(cx + 42, cy + 15, "---", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        if (mine)
        {
            struct UiRamp r = UiRampOf(UI_P_POCKETS_KEY, 0.4, 0.25, 0.6);

            UiG_Paint(UiM_Capsule(52, 11, true, true), cx + 2, cy + 34, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, 0});
            UiCaps_Draw(cx + 28, cy + 36, "HOLDS IT", UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
        }
        else if (sHpMode && !m->isEgg)
        {
            UiRGBA sc;
            const char *status = UiMon_StatusLabel(m, &sc);
            double frac = m->maxHP > 0 ? (double)m->hp / m->maxHP : 0;

            if (status != NULL && m->hp == 0)
            {
                UiW_StatusBadge(cx + 4, cy + 34, status, sc, 48);
            }
            else if (status != NULL)
            {
                UiW_StatusBadge(cx + 3, cy + 34, status, sc, 24);
                UiW_HpBar(cx + 29, cy + 36, 24, frac, false, UI_NONE);
            }
            else
            {
                UiW_HpBar(cx + 4, cy + 36, 48, frac, false, UI_NONE);
            }
        }
        else
        {
            UiText_Draw(UI_FONT_SMALL, cx + 28, cy + 32, m->nickname, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        }
    }
}

/* a TM's move and the party's real compatibility (the game's teachable learnsets) */
static void TmPanel(int t, int x, int y, int w)
{
    int ty = UiMon_Type(GetMoveType(sMove)), cx = x + 92, yy = y + 15, gy = y + 44;
    const char *stats[3][2];
    char power[8], acc[8], pp[8];
    const u8 *starts[3];
    int lengths[3], n;
    u8 b[64];

    UiW_TypeBadge(x + 12, y, ty >= 0 ? ty : UI_TYPE_NORMAL, 0);
    UiW_CategoryIcon(x + 56, y, GetMoveCategory(sMove) == DAMAGE_CATEGORY_PHYSICAL ? 0 : GetMoveCategory(sMove) == DAMAGE_CATEGORY_SPECIAL ? 1 : 2);
    snprintf(power, sizeof(power), GetMovePower(sMove) > 1 ? "%u" : "---", (unsigned)GetMovePower(sMove));
    snprintf(acc, sizeof(acc), GetMoveAccuracy(sMove) > 0 ? "%u" : "---", (unsigned)GetMoveAccuracy(sMove));
    snprintf(pp, sizeof(pp), "%u", (unsigned)GetMovePP(sMove));
    stats[0][0] = "POWER"; stats[0][1] = power;
    stats[1][0] = "ACC"; stats[1][1] = acc;
    stats[2][0] = "PP"; stats[2][1] = pp;
    for (int i = 0; i < 3; i++)
    {
        UiCaps_Draw(cx, y + 3, stats[i][0], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        cx += UiCaps_Width(stats[i][0]) + 5;
        UiCaps_Draw(cx, y + 3, stats[i][1], UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        cx += UiCaps_Width(stats[i][1]) + 12;
    }
    n = UiText_Wrap(GetMoveDescription(sMove), w - 24, UI_FONT_SMALL, starts, lengths, 3);
    for (int i = 0; i < n && i < 2; i++)
    {
        UiText_CopyN(b, sizeof(b), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, x + 12, yy, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        yy += 12;
    }
    UiCaps_Draw(x + 12, gy + 2, "YOUR PARTY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < sPartyCount; i++)
    {
        const struct UiMon *m = &sParty[i];
        int c = x + 12 + i * 60, cy = gy + 14, tw = 52;
        static const struct UiFilter sGray = {0.6f, 0, 0, 0, 0};
        const char *tag = sTeach[i] == 2 ? "LEARNED" : sTeach[i] == 1 ? "ABLE" : "NOT ABLE";
        UiRGBA tc = sTeach[i] == 2 ? UI_HEX(0x5890C8) : sTeach[i] == 1 ? UI_HEX(0x48A848) : UI_HEX(0xA0A0A8);
        struct UiRamp r = UiRampOf(tc, 0.4, 0.25, 0.6);

        if (m->species == SPECIES_NONE)
            continue;
        UiG_Paint(UiM_Rounded4(56, 48, 3, 3, 3, 3), c, cy, sTeach[i] == 1 ? UI_P_PAPER : UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.4),
                  &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiGfx_MonIcon(m->species, m->personality, m->isEgg, sTeach[i] == 1 ? ((t + i * 7) / 20) % 2 : 0, c + 12, cy - 1,
                      sTeach[i] ? NULL : &sGray);
        UiG_Paint(UiM_Capsule(tw, 11, true, true), c + 2, cy + 34, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, 0});
        UiCaps_Draw(c + 2 + (tw >> 1), cy + 36, tag, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    }
}

/* a Mega Stone: its Pokémon's Mega form from the game's form changes */
static void MegaPanel(int t, int x, int y, int w)
{
    int stx = x + 12, sty = y + 1, sw = 100, sh = 84, rx = stx + sw + 10, nx, ly = y + 62, ny = y + 90;
    int t0 = UiMon_Type(GetSpeciesType(sMegaForm, 0)), t1 = UiMon_Type(GetSpeciesType(sMegaForm, 1));
    const struct UiRamp *tr = &gUiTypeRamps[t0 >= 0 ? t0 : UI_TYPE_NORMAL];
    struct UiPattern diag = UiPat_Diag(12, 4, 0, 1);
    struct UiRamp stage = UiRampDefault(UiMix(tr->base, UI_HEX(0x808080), 0.35));
    static const char *const sLabels[6] = {"HP", "ATK", "DEF", "SPA", "SPD", "SPE"};
    const struct SpeciesInfo *a = &gSpeciesInfo[sMegaBase], *b = &gSpeciesInfo[sMegaForm];
    const u8 base[6] = {a->baseHP, a->baseAttack, a->baseDefense, a->baseSpAttack, a->baseSpDefense, a->baseSpeed};
    const u8 mega[6] = {b->baseHP, b->baseAttack, b->baseDefense, b->baseSpAttack, b->baseSpDefense, b->baseSpeed};
    const struct UiMon *owner = NULL;
    u8 line[64];
    int col = 0, row = 0;

    (void)w;
    UiG_Paint(UiM_Rounded4(sw, sh, 3, 3, 3, 3), stx, sty, UiMix(tr->hi, UI_HEX(0xFFFFFF), 0.55),
              &(struct UiPaint){UiMix(tr->lo, UI_P_INK, 0.3), UI_NONE, UI_NONE, 0, 0, &diag, UiMix(tr->hi, UI_HEX(0xFFFFFF), 0.3)});
    UiG_Paint(UiM_Ellipse(70, 14), stx + 15, sty + sh - 18, stage.base, &(struct UiPaint){stage.ink, stage.hi, stage.lo, 0, 0, NULL, 0});
    UiG_Save();
    UiG_Clip(stx + 1, sty + 1, sw - 2, sh - 2);
    if (!UiGfx_MonPic(sMegaForm, 0, false, false, UiGfx_MonPicFrameAt(sMegaForm, 0, false, false, t), stx + (sw >> 1), sty + sh - 10, NULL))
        UiW_Missing(stx + (sw >> 1) - 24, sty + sh - 58, 48, 48);
    UiG_Restore();
    line[0] = EOS;
    UiText_Append(line, sizeof(line), sText_Mega);
    UiText_Append(line, sizeof(line), GetSpeciesName(sMegaBase));
    nx = UiG_Text(rx, y - 1, line);
    if (t0 >= 0)
        nx = UiW_TypeBadge(nx + 6, y + 1, t0, 0) - 3;
    if (t1 >= 0 && t1 != t0)
        UiW_TypeBadge(nx + 6, y + 1, t1, 0);
    UiCaps_Draw(rx, y + 21, "ABILITY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_Text(rx + 44, y + 16, gAbilitiesInfo[b->abilities[0]].name);
    /* the base stats a Mega Evolution changes */
    for (int i = 1; i < 6; i++)
    {
        int cx = rx + col * 84, cy = y + 37 + row * 11;
        char va[8], vb[8];

        snprintf(va, sizeof(va), "%u", (unsigned)base[i]);
        snprintf(vb, sizeof(vb), "%u", (unsigned)mega[i]);
        UiCaps_Draw(cx, cy, sLabels[i], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(cx + 22, cy, va, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(cx + 22 + UiCaps_Width(va) + 3, cy, UI_CAPS_RIGHT, mega[i] > base[i] ? UI_P_CMD_POKEMON : UI_P_TEXT_MUTED, UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(cx + 22 + UiCaps_Width(va) + 11, cy, vb, mega[i] > base[i] ? UiMix(UI_P_CMD_POKEMON, UI_P_INK, 0.3) : UI_P_TEXT,
                    UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (++col > 2)
        {
            col = 0;
            row++;
        }
    }
    /* who can use it, and the rule */
    for (int i = 0; i < sPartyCount && owner == NULL; i++)
        if (sParty[i].species == sMegaBase && !sParty[i].isEgg)
            owner = &sParty[i];
    Chip(rx, ly + 4, "HOLD");
    line[0] = EOS;
    if (owner != NULL)
    {
        UiText_Append(line, sizeof(line), owner->nickname);
        UiText_Append(line, sizeof(line), owner->heldItem == sItem ? sText_IsHolding : sText_CanHold);
    }
    else
    {
        UiText_Append(line, sizeof(line), sText_NoOneOf1);
        UiText_Append(line, sizeof(line), GetSpeciesName(sMegaBase));
        UiText_Append(line, sizeof(line), sText_NoOneOf2);
    }
    UiG_Text(rx + 52, ly, line);
    UiGfx_Icon(sHasRing ? "gem" : "gem_off", x + 14, ny + 6, 0);
    UiG_Text(x + 24, ny, sHasRing ? sText_HaveRing : sText_NeedRing);
    UiText_Draw(UI_FONT_SMALL, x + 168, ny + 2, sText_OnePerBattle, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
}

/* a key item: the field shortcut the game keeps (SELECT) */
static void QuickUse(int x, int y, int w, const struct UiRamp *pr)
{
    u8 line[48];

    UiG_Paint(UiM_Rounded4(w - 24, 52, 3, 3, 3, 3), x + 12, y, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35),
              &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiCaps_Draw(x + 20, y + 6, "QUICK USE", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(30, 30, 3, 3, 3, 3), x + 20, y + 16, UI_P_PAPER, &(struct UiPaint){UiMix(pr->lo, UI_P_INK, 0.3), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
    if (sRegistered != ITEM_NONE)
        UiGfx_ItemIcon(sRegistered, x + 23, y + 19, NULL);
    else
        UiCaps_Draw(x + 35, y + 28, "---", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    line[0] = EOS;
    if (sRegistered != ITEM_NONE)
    {
        UiText_Append(line, sizeof(line), sText_Registered);
        UiText_Append(line, sizeof(line), GetItemName(sRegistered));
    }
    else
    {
        UiText_Append(line, sizeof(line), sText_NothingRegistered);
    }
    UiG_Text(x + 58, y + 14, line);
    UiText_Draw(UI_FONT_SMALL, x + 58, y + 32, sText_RegisterHow, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
}

static void Card(int t, const struct UiRamp *pr)
{
    int x = 252, y = 40, w = 380, h = 288, dy = y + 52, sy, tw;
    UiRGBA ink = UiMix(pr->ink, UI_P_INK, 0.4), rule = UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.55);
    const char *tl = PocketCaps(sPocket);
    const u8 *starts[4];
    int lengths[4], n, nx;
    u8 b[64];

    tw = UiCaps_Width(tl) + 16;
    UiG_Paint(UiM_Rounded4(tw, 14, 3, 3, 0, 0), x + 12, y - 12, pr->base, &(struct UiPaint){pr->ink, pr->hi, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(x + 20, y - 8, tl, UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, UI_P_CREAM, &(struct UiPaint){ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, 0});
    /* the header: name, the game's registered mark, quantity */
    nx = UiG_Text(x + 12, y + 4, Title(b, sizeof(b)));
    if (sRegistered == sItem && sItem != ITEM_NONE)
    {
        struct UiRamp rr = UiRampOf(UI_P_POCKETS_KEY, 0.4, 0.25, 0.62);

        UiG_Paint(UiM_Capsule(30, 11, true, true), nx + 4, y + 7, rr.base, &(struct UiPaint){rr.ink, rr.hi, rr.lo, 0, 0, NULL, 0});
        UiCaps_Draw(nx + 19, y + 9, "REG", UI_P_TEXT_W, rr.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    }
    if (sPocket != POCKET_KEY_ITEMS)
    {
        u16 q = sQty[sIndex];

        b[0] = EOS;
        if (q)
        {
            UiText_AppendAscii(b, sizeof(b), "x");
            UiText_AppendNumber(b, sizeof(b), q, 1, false);
        }
        UiText_Draw(UI_FONT_NORMAL, x + w - 12, y + 4, q ? b : sText_NoneInBag, q ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
    else
    {
        UiCaps_Draw(x + w - 12, y + 9, "KEY ITEM", UiMix(pr->lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
    UiG_Rect(x + 6, y + 22, w - 12, 2, pr->base);
    UiG_Rect(x + 6, y + 22, w - 12, 1, pr->hi);
    /* the price tags: the game's buy and sell prices */
    {
        int py = y + 30;

        UiCaps_Draw(x + 12, py + 4, "BUY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (sBuy == 0 && sMegaForm != SPECIES_NONE)
        {
            /* the design's Mega Stone price is still to be set */
            PriceTag(x + 36, py, 90, 0, ink, true);
            UiW_Planned(x + 132, py + 2, "PRICE PENDING");
            UiText_Draw(UI_FONT_SMALL, x + 240, py + 1, sText_ExpensiveByDesign, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        }
        else
        {
            PriceTag(x + 36, py, 90, sBuy, ink, sBuy == 0);
            UiCaps_Draw(x + 146, py + 4, "SELL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            PriceTag(x + 174, py, 90, sImportant ? 0 : sSell, ink, sImportant || sSell == 0);
            if (sBuy == 0)
                UiText_Draw(UI_FONT_SMALL, x + 276, py + 1, sImportant ? sText_NotForSale : sText_NotSold, UI_P_TEXT_MUTED, UI_NONE,
                            UI_ALIGN_LEFT, 0);
        }
    }
    /* the game's description on ruled lines */
    for (int r = 0; r < 3; r++)
        UiG_HLine(x + 10, dy + 15 + r * 16, w - 20, rule);
    UiG_VLine(x + 26, dy, 48, UiMix(pr->hi, UI_P_CREAM, 0.5));
    n = UiText_Wrap(GetItemDescription(sItem), w - 44, UI_FONT_NORMAL, starts, lengths, 4);
    for (int i = 0; i < n && i < 3; i++)
    {
        UiText_CopyN(b, sizeof(b), starts[i], lengths[i]);
        UiG_Text(x + 32, dy + i * 16, b);
    }
    sy = dy + 54;
    UiG_Rect(x + 6, sy, w - 12, 1, UI_P_CREAM_LO);
    UiG_Rect(x + 6, sy + 1, w - 12, 1, UI_P_CREAM_HI);
    sy += 6;
    Strip(x, y + 238, w, pr);
    if (sMove != MOVE_NONE)
    {
        TmPanel(t, x, sy, w);
        return;
    }
    if (sMegaForm != SPECIES_NONE)
    {
        MegaPanel(t, x, sy, w);
        return;
    }
    for (int i = 0; i < sUseCount; i++)
    {
        Chip(x + 12, sy + 3, sUses[i].chip);
        UiG_Text(x + 64, sy - 1, sUses[i].text);
        sy += 18;
    }
    if (sPocket != POCKET_KEY_ITEMS)
        Holders(t, x, sy + 2 > y + 170 ? sy + 2 : y + 170, w);
    else
        QuickUse(x, sy + 6 > y + 160 ? sy + 6 : y + 160, w, pr);
}

bool UiItemDetail_Draw(int t)
{
    struct UiRamp pr = UiRampOf(PocketColour(sPocket), 0.4, 0.25, 0.62);
    UiRGBA lin = UiMix(pr.lo, pr.ink, 0.45);
    struct UiPattern stripes = UiPat_VStripe(8, 1, 0);
    struct UiRamp tan = {UI_HEX(0xF0C890), UI_HEX(0xB07840), UI_HEX(0x8A5A2A), UI_HEX(0x4A2810)};
    struct UiHintItem h[2] = {{UI_ACT_TABS, sText_Browse}, {UI_ACT_CANCEL, sText_Back}};
    int x0, y0, w, hh;

    if (!sOpen || sItem == ITEM_NONE)
        return false;
    t = sOpenT;
    UiG_Bleed(&x0, &y0, &w, &hh);
    UiG_Rect(x0, y0, w, hh, lin);
    UiG_FillPattern(&stripes, x0, y0, w, hh, UiMix(lin, pr.lo, 0.3), 0, 0);
    UiG_Hit(HIT_BACKDROP, 0, x0, y0, w, hh);
    UiW_TitleTab(0, 8, "BAG", &tan, 0, NULL);
    Display(t, &pr);
    Card(t, &pr);
    UiW_Hints(252, 340, h, 2, true, 10);
    return true;
}

/* ---------------------------------------------------------------- input */

bool UiItemDetail_Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        sOpen = false;
        return true;
    }
    if (button != 1)
        return false;
    switch (hit->id)
    {
    case HIT_ARROW:
        Browse(hit->index);
        return true;
    case HIT_STRIP:
        sWant = hit->index;
        return true;
    case UI_HIT_HINT:
        if (hit->index == UI_ACT_CANCEL)
            sOpen = false;
        else if (hit->index == UI_ACT_TABS)
            Browse(1);
        return true;
    default:
        return true;
    }
}

bool UiItemDetail_Wheel(int dy)
{
    Browse(dy > 0 ? -1 : 1);
    return true;
}

uint16_t UiItemDetail_FilterKeys(uint16_t keys, uint16_t newKeys)
{
    if (newKeys & (A_BUTTON | B_BUTTON))
        sOpen = false;
    else if (newKeys & (DPAD_LEFT | DPAD_UP | L_BUTTON))
        Browse(-1);
    else if (newKeys & (DPAD_RIGHT | DPAD_DOWN | R_BUTTON))
        Browse(1);
    (void)keys;
    return 0;   /* the game gets none of them while the case is open */
}
#endif // ENABLE_NATIVE_UI
