#ifdef ENABLE_NATIVE_UI
/*
 * 28 · Starter selection (the approved design's preview/js/screens/starters.js):
 * Professor Birch's open bag over the engine's starter choice
 * (starter_choose.c), which keeps every rule: its six pages of three (Hoenn,
 * Kanto, Johto, Sinnoh, Unova, Kalos; Hoenn first), Left / Right for the
 * Grass, Fire or Water ball, Up / Down or L / R for the page, A for the
 * Pokémon coming out and the engine's own question, YES to take it (the role -
 * Grass, Fire, Water - is what the story's rival follows), NO back to the same
 * ball. The research card shows the engine's data for the Pokémon under the
 * hand: its name, category, types, Pokédex entry and first ability. The first
 * battle, the Pokémon's level and the nickname question are the game's own
 * afterwards. Clicks on the balls, the region tag and YES / NO, the wheel and
 * a right click become the engine's presses. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "data.h"
#include "international_string_util.h"
#include "main.h"
#include "pokemon.h"
#include "strings.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_BALL = UI_HIT_SCREEN,
    HIT_PAGE_UP,
    HIT_PAGE_DOWN,
    HIT_DOT,
};

#define BX 32
#define BY 74
#define BW 352
#define BH 196
#define PAGES 6

static const int sCradles[3][2] = {{BX + 84, BY + 104}, {BX + 176, BY + 128}, {BX + 268, BY + 104}};
static const char *const sRoleLabels[3] = {"GRASS", "FIRE", "WATER"};
static const u8 sRoleTypes[3] = {UI_TYPE_GRASS, UI_TYPE_FIRE, UI_TYPE_WATER};
/* the pages in the engine's order: the generation each region's starters come from */
static const u8 sPageGens[PAGES] = {3, 1, 2, 4, 5, 6};
static const double sSlide[6] = {0, 0.45, 0.75, 0.9, 0.97, 1};
static const char *const sHandRows[14] = {
    "...##.......", "..#ww#......", "..#ww#......", "..#ww###....", "..#ww#ww##..", ".##ww#ww#w#.", "#ww#wwwww#w#",
    "#wwwwwwwwww#", "#wwwwwwwwww#", ".#wwwwwwwwl#", ".#wwwwwwwl#.", "..#wwwwwll#.", "..#llllll#..", "...######...",
};

static const u8 sText_OwnChoice1[] = _("Every player chooses their own.");
static const u8 sText_OwnChoice2[] = _("Friends may choose the same one.");
static const u8 sText_Rival1[] = _("Rival battles follow the role:");
static const u8 sText_RivalGrass[] = _("Grass, as in Emerald.");
static const u8 sText_RivalFire[] = _("Fire, as in Emerald.");
static const u8 sText_RivalWater[] = _("Water, as in Emerald.");
static const u8 sText_Gen[] = _(" · Gen ");
static const u8 sText_Page[] = _(" · Page ");
static const u8 sText_Of[] = _(" of ");
static const u8 sText_Ball[] = _("Grass · Fire · Water");
static const u8 sText_Region[] = _("Region");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");

/* ---------------------------------------------------------------- captured state */

static struct NativeStarters sS;
static u8 sMsg[160];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static u8 sRegion[PAGES][16];     /* the region names, from the engine's page lines */
static uint32_t sOpenAt;
static uint32_t sLastCapture;
static u8 sWasPhase;
static int sGoal = -1;            /* a clicked ball, then A */
static int sGoalSteps;

static bool Detect(void)
{
    struct NativeStarters s;

    return StarterChoose_NativeUiState(&s);
}

static int Page(void)
{
    return sS.pageSize > 0 ? sS.selection / sS.pageSize : 0;
}

static int Ball(void)
{
    return sS.pageSize > 0 ? sS.selection % sS.pageSize : 0;
}

static u16 Species(void)
{
    return sS.selection < NATIVE_STARTER_MAX ? sS.species[sS.selection] : SPECIES_NONE;
}

static void GoalStep(void)
{
    if (sGoal < 0 || !sS.ready || NativeUi_PressesPending())
        return;
    if (sS.phase == NATIVE_STARTER_BROWSE)
    {
        if (++sGoalSteps > 12)
            sGoal = -1;
        else if (Ball() == sGoal)
        {
            NativeUi_QueuePress(A_BUTTON);
            sGoal = -1;
        }
        else
            NativeUi_QueuePress(sGoal > Ball() ? DPAD_RIGHT : DPAD_LEFT);
    }
    else if (sS.phase == NATIVE_STARTER_CONFIRM)
    {
        if (++sGoalSteps > 6)
            sGoal = -1;
        else if (sS.yesNoCursor == sGoal)
        {
            NativeUi_QueuePress(A_BUTTON);
            sGoal = -1;
        }
        else
            NativeUi_QueuePress(sGoal > sS.yesNoCursor ? DPAD_DOWN : DPAD_UP);
    }
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    bool8 active;

    if (!StarterChoose_NativeUiState(&sS))
        return;
    if (now - sLastCapture != 1)
    {
        sGoal = -1;
        sWasPhase = NATIVE_STARTER_MOVING;
    }
    sLastCapture = now;
    if (sS.phase == NATIVE_STARTER_OPENING && sWasPhase != NATIVE_STARTER_OPENING && sWasPhase != NATIVE_STARTER_CONFIRM)
        sOpenAt = now;
    sWasPhase = sS.phase;
    if (Species() != SPECIES_NONE)
        UiGfx_PrepareMonPic(Species(), 0, false, false);
    UiGfx_PrepareItemIcon(ITEM_POKE_BALL);
    sHasMsg = TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    /* the region: the engine's page line up to its colon ("Hoenn: choose ...") */
    if (sHasMsg && sS.phase != NATIVE_STARTER_OPENING && sS.phase != NATIVE_STARTER_CONFIRM && Page() < PAGES)
    {
        int n = 0;

        while (n < (int)sizeof(sRegion[0]) - 1 && sMsg[n] != EOS && sMsg[n] != CHAR_COLON && sMsg[n] != CHAR_NEWLINE)
            n++;
        if (sMsg[n] == CHAR_COLON)
            UiText_CopyN(sRegion[Page()], sizeof(sRegion[0]), sMsg, n);
    }
    GoalStep();
}

/* ---------------------------------------------------------------- the bag */

/* each region's lining (the design's) */
static struct UiRamp Lining(int page)
{
    UiRGBA base;

    switch (page)
    {
    case 0: base = UiW_FieldBand().base; break;
    case 1: base = UI_P_CMD_FIGHT; break;
    case 2: base = UI_P_POCKETS_KEY; break;
    case 3: base = gUiTypeRamps[UI_TYPE_WATER].base; break;
    case 4: base = gUiTypeRamps[UI_TYPE_STEEL].base; break;
    default: base = gUiTypeRamps[UI_TYPE_FAIRY].base; break;
    }
    return UiRampOf(base, 0.45, 0.25, 0.62);
}

struct StitchCtx
{
    int step;
    int dash;
};

static bool StitchAt(int x, int y, const void *ctx)
{
    const struct StitchCtx *c = ctx;

    return (x + y) % c->step < c->dash;
}

/* W.stitches: a dashed stitch line inset px inside a shape's edge */
static void Stitches(const struct UiMask *m, int x, int y, int inset, UiRGBA colour)
{
    const struct UiMask *inner = m;
    struct StitchCtx c = {4, 2};

    for (int i = 0; i < inset; i++)
        inner = UiM_Erode4(inner);
    UiG_Mask(UiM_And(UiM_Outline(inner), UiM_Fn(m->w, m->h, "st4,2", StitchAt, &c)), x, y, colour);
}

static void Hand(int x, int y)
{
    static const char *rows[14];
    struct UiPixPal pal[3] = {{'#', UI_P_INK}, {'w', UI_HEX(0xF8F8F8)}, {'l', UI_HEX(0xC8C8D0)}};

    for (int i = 0; i < 14; i++)
        rows[i] = sHandRows[13 - i]; /* pointing down */
    UiW_Pix("handd", rows, 14, pal, 3, x, y, false);
}

static void Bag(int t)
{
    struct UiRamp lea = UiRampOf(UI_P_BAG_BG, 0.36, 0.3, 0.66);
    struct UiRamp dark = UiRampOf(UiMix(UI_P_BAG_BG, UI_P_BAG_INK, 0.35), 0.3, 0.3, 0.7);
    struct UiRamp gold = UiRampOf(UI_P_POCKETS_KEY, 0.45, 0.25, 0.66);
    struct UiRamp ln = Lining(Page());
    UiRGBA tb = UiMix(ln.hi, UI_HEX(0xFFFFFF), 0.45);
    bool asking = sS.phase == NATIVE_STARTER_OPENING || sS.phase == NATIVE_STARTER_CONFIRM;
    const struct UiMask *flap = UiM_Rounded4(BW - 96, 50, 10, 10, 0, 0);
    const struct UiMask *body = UiM_Rounded4(BW, BH, 8, 8, 8, 8);
    struct UiPattern dots = UiPat_Dots(8, 1, 0, 0, true);

    /* the shoulder strap leaving the bag */
    for (int k = 0; k < 2; k++)
    {
        int sx = k == 0 ? BX - 10 : BX + BW - 8;

        UiG_Paint(UiM_Rounded4(18, 46, 3, 3, 3, 3), sx, BY + 96, dark.base, &(struct UiPaint){dark.ink, dark.hi, dark.lo, 0, 0, NULL, UI_NONE});
        UiG_Paint(UiM_Rounded4(18, 12, 2, 2, 2, 2), sx, BY + 112, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 0, NULL, UI_NONE});
        UiG_Rect(sx + 5, BY + 115, 8, 6, UiMix(dark.base, dark.lo, 0.5));
        UiG_VLine(sx + 8, BY + 114, 8, gold.lo);
    }
    /* the open flap folded back behind the body, the body, the tray in the region's colour */
    UiG_Paint(flap, BX + 48, BY - 42, dark.base, &(struct UiPaint){dark.ink, dark.hi, dark.lo, 0, 0, NULL, UI_NONE});
    Stitches(flap, BX + 48, BY - 42, 3, dark.hi);
    UiG_Paint(body, BX, BY, lea.base, &(struct UiPaint){lea.ink, lea.hi, lea.lo, 2, 2, NULL, UI_NONE});
    Stitches(body, BX, BY, 4, lea.hi);
    UiG_Paint(UiM_Rounded4(BW - 36, BH - 36, 4, 4, 4, 4), BX + 18, BY + 18, tb,
              &(struct UiPaint){UiMix(ln.ink, lea.ink, 0.4), UI_NONE, UiMix(tb, ln.base, 0.3), 0, 0, &dots, UiMix(tb, ln.base, 0.35)});
    /* the region tag on the flap (under the sprite window while a choice is open) */
    if (!asking)
    {
        int tw = 136, tx = BX + (BW >> 1) - (tw >> 1), ty = BY - 36;

        UiG_Paint(UiM_Rounded4(tw, 30, 2, 2, 2, 2), tx, ty, UI_P_CREAM, &(struct UiPaint){dark.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
        UiG_Rect(tx + 4, ty + 3, 3, 24, ln.base);
        UiG_Rect(tx + tw - 7, ty + 3, 3, 24, ln.base);
        if (Page() < PAGES && sRegion[Page()][0] != 0)
            UiG_TextEx(tx + (tw >> 1), ty, sRegion[Page()], UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER);
        UiW_PageDots(tx + (tw >> 1) - 28, ty + 19, sS.pageCount, Page(), ln.base, UI_NONE);
        for (int d = 0; d < sS.pageCount; d++)
            UiG_Hit(HIT_DOT, d, tx + (tw >> 1) - 29 + d * 10, ty + 17, 10, 11);
        UiCaps_Draw(tx + 14, ty + 4, UI_CAPS_UP, ln.base, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(tx + tw - 20, ty + 4, UI_CAPS_DOWN, ln.base, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_PAGE_UP, 0, tx, ty, 30, 30);
        UiG_Hit(HIT_PAGE_DOWN, 0, tx + tw - 30, ty, 30, 30);
    }
    /* the cradles, the role tags and the balls */
    for (int i = 0; i < 3; i++)
    {
        int cx = sCradles[i][0], cy = sCradles[i][1], rw;
        bool sel = i == Ball();
        const struct UiRamp *rr = &gUiTypeRamps[sRoleTypes[i]];

        UiG_Paint(UiM_Ellipse(56, 34), cx - 28, cy - 14, UiMix(ln.lo, lea.lo, 0.3),
                  &(struct UiPaint){UiMix(ln.ink, lea.ink, 0.5), UiMix(ln.lo, ln.ink, 0.3), UiMix(ln.base, tb, 0.4), 0, 0, NULL, UI_NONE});
        rw = UiCaps_Width(sRoleLabels[i]) + 10;
        UiG_Paint(UiM_Capsule(rw, 11, true, true), cx - (rw >> 1), cy + 24, rr->base, &(struct UiPaint){rr->ink, rr->hi, rr->lo, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(cx, cy + 26, sRoleLabels[i], UI_P_TEXT_W, rr->ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
        if (!(asking && sel))
        {
            int hop = sel && !asking && (t / 16) % 2 == 1 ? 2 : 0;

            UiGfx_ItemIcon(ITEM_POKE_BALL, cx - 12, cy - 16 - hop, NULL);
        }
        UiG_Hit(HIT_BALL, i, cx - 30, cy - 34, 60, 72);
    }
    if (!asking)
    {
        /* the hand and the label window above the selected ball */
        int c0x = sCradles[Ball()][0], c0y = sCradles[Ball()][1], bob = (t / 16) % 2, lw, lx, ly, b[4];
        u8 cat[40];
        const u8 *name = GetSpeciesName(Species());

        Hand(c0x - 6, c0y - 42 + bob);
        CopyMonCategoryText(Species(), cat); /* "Chick POKéMON" */
        lw = UiText_Width(UI_FONT_NARROW, cat, 0);
        if (UiText_Width(UI_FONT_NORMAL, name, 0) > lw)
            lw = UiText_Width(UI_FONT_NORMAL, name, 0);
        lw += 20;
        lx = c0x - (lw >> 1);
        if (lx > BX + BW - 6 - lw)
            lx = BX + BW - 6 - lw;
        if (lx < BX + 6)
            lx = BX + 6;
        ly = c0y - 82;
        UiW_DialogBox(lx, ly, lw, 40, &ln, UI_NONE, b);
        UiText_Draw(UI_FONT_NARROW, lx + (lw >> 1), ly + 5, cat, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        UiText_Draw(UI_FONT_NORMAL, lx + (lw >> 1), ly + 18, name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    else
    {
        /* the round window with the Pokémon, sliding up as it opens */
        uint32_t f = NativeUi_Frame() - sOpenAt;
        double k = sSlide[f > 5 ? 5 : f];
        int d0 = 100, wx = BX + (BW >> 1) - (d0 >> 1), wy = BY - 38 + (int)(40 * (1 - k) + 0.5);
        u16 sp = Species();

        UiG_Paint(UiM_Circle(d0 + 8), wx - 4, wy - 4, ln.base, &(struct UiPaint){ln.ink, ln.hi, ln.lo, 0, 0, NULL, UI_NONE});
        UiG_Paint(UiM_Circle(d0), wx, wy, UI_P_PAPER, &(struct UiPaint){ln.ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiG_Mask(UiM_Ellipse(60, 10), wx + (d0 >> 1) - 30, wy + d0 - 28, UiMix(UI_P_PAPER, ln.base, 0.3));
        UiG_Save();
        UiG_Clip(wx + 6, wy + 4, d0 - 12, d0 - 10);
        UiGfx_MonPic(sp, 0, false, false, UiGfx_MonPicFrameAt(sp, 0, false, false, (int)(NativeUi_Frame() - sOpenAt)),
                     wx + (d0 >> 1), wy + d0 - 22, NULL);
        UiG_Restore();
    }
}

/* ---------------------------------------------------------------- the research card */

static void Card(void)
{
    u16 sp = Species();
    int x = 396, y = 30, w = 236, h = 252, bx, ny, n;
    int t0 = UiMon_Type(GetSpeciesType(sp, 0));
    const struct UiRamp *tr = &gUiTypeRamps[t0 >= 0 ? t0 : UI_TYPE_NORMAL];
    struct UiPattern lines = UiPat_HStripe(14, 1, 3), below = UiPat_Below(64);
    char caps[24];
    u8 buf[64];
    const u8 *starts[6];
    int lengths[6];

    lines.and_ = &below;
    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, &lines, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.55)});
    UiG_Paint(UiM_Rounded4(w - 6, 20, 2, 2, 0, 0), x + 3, y + 3, tr->base, &(struct UiPaint){UI_NONE, tr->hi, tr->lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + 10, y + 4, GetSpeciesName(sp), UI_P_TEXT_W, tr->ink, UI_ALIGN_LEFT, 0);
    CopyMonCategoryText(sp, buf);
    UiG_Text(x + 10, y + 26, buf);
    bx = x + 10;
    for (int i = 0; i < 2; i++)
    {
        enum Type type = GetSpeciesType(sp, i);

        if (i == 1 && type == GetSpeciesType(sp, 0))
            break;
        if (UiMon_Type(type) >= 0)
            bx = UiW_TypeBadge(bx, y + 46, UiMon_Type(type), 0) + 3;
    }
    snprintf(caps, sizeof(caps), "%s ROLE", sRoleLabels[Ball()]);
    UiCaps_Draw(x + w - 10, y + 49, caps, gUiTypeRamps[sRoleTypes[Ball()]].lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_HLine(x + 6, y + 64, w - 12, UiMix(tr->base, UI_P_CREAM, 0.4));
    /* the Pokédex entry, as one paragraph */
    {
        u8 entry[256];
        const u8 *src = GetSpeciesPokedexDescription(sp);
        int k = 0;

        for (; k < (int)sizeof(entry) - 1 && src[k] != EOS; k++)
            entry[k] = src[k] == CHAR_NEWLINE ? CHAR_SPACE : src[k];
        entry[k] = EOS;
        n = UiText_Wrap(entry, w - 20, UI_FONT_SMALL, starts, lengths, 6);
        for (int i = 0; i < n && i < 5; i++)
        {
            u8 line[96];

            UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
            UiText_Draw(UI_FONT_SMALL, x + 10, y + 68 + i * 14, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
    }
    UiCaps_Draw(x + 10, y + 139, "ABILITY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiText_Draw(UI_FONT_SMALL, x + 56, y + 135, gAbilitiesInfo[GetSpeciesAbility(sp, 0)].name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    /* what this build says about the choice */
    ny = y + 158;
    UiG_HLine(x + 6, ny - 4, w - 12, UI_P_CREAM_LO);
    UiGfx_Icon("head_green", x + 9, ny + 1, 0);
    UiText_Draw(UI_FONT_SMALL, x + 26, ny, sText_OwnChoice1, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, x + 26, ny + 13, sText_OwnChoice2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiGfx_Icon("head_red", x + 9, ny + 31, 0);
    UiText_Draw(UI_FONT_SMALL, x + 26, ny + 30, sText_Rival1, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, x + 26, ny + 43, Ball() == 0 ? sText_RivalGrass : Ball() == 1 ? sText_RivalFire : sText_RivalWater,
                UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    buf[0] = EOS;
    if (Page() < PAGES)
    {
        UiText_Append(buf, sizeof(buf), sRegion[Page()]);
        UiText_Append(buf, sizeof(buf), sText_Gen);
        UiText_AppendNumber(buf, sizeof(buf), sPageGens[Page()], 1, false);
    }
    UiText_Append(buf, sizeof(buf), sText_Page);
    UiText_AppendNumber(buf, sizeof(buf), Page() + 1, 1, false);
    UiText_Append(buf, sizeof(buf), sText_Of);
    UiText_AppendNumber(buf, sizeof(buf), sS.pageCount, 1, false);
    UiText_Draw(UI_FONT_SMALL, x + 10, ny + 64, buf, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

/* ---------------------------------------------------------------- the message and the prompts */

static void Message(int t)
{
    int b[4], hx = 12, hy = 343;

    UiW_DialogBox(8, 290, 624, 44, NULL, UI_NONE, b);
    if (sHasMsg)
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 4, b[1] + 1, b[2] - 8, 2);
    if (sS.phase == NATIVE_STARTER_CONFIRM)
    {
        UiW_YesNo(632, 286, sS.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
        {
            struct UiHintItem hints[3] = {{UI_ACT_SCROLL, sText_Choose}, {UI_ACT_CONFIRM, sText_OK}, {UI_ACT_CANCEL, sText_No}};

            UiW_Hints(hx, hy, hints, 3, true, 10);
        }
    }
    else if (sS.phase != NATIVE_STARTER_OPENING)
    {
        struct UiHintItem a[1] = {{UI_ACT_MOVE, sText_Ball}};
        struct UiHintItem b2[2] = {{UI_ACT_TABS, sText_Region}, {UI_ACT_CONFIRM, sText_Select}};

        hx = UiW_Hints(hx, hy, a, 1, true, 10);
        hx = UiW_Hint(hx, hy, UI_ACT_SCROLL, (const u8 *)"\xFF", true) + 2;
        UiW_Hints(hx, hy, b2, 2, true, 10);
    }
}

static void Draw(int t)
{
    struct UiRamp f = UiW_FieldBand();
    struct UiRamp tab = {f.hi, f.base, f.lo, UI_P_FIELD_INK};
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, f.lo);
    {
        /* tufts of grass, two to a 16 px tile */
        static const char *const tuft[16] = {
            "................", "................", "................", "..#.#.#.........", "...#.#..........",
            "................", "................", "................", "................", "................",
            "................", "..........#.#.#.", "...........#.#..", "................", "................",
            "................",
        };
        const struct UiMask *m = UiM_Rows(tuft, 16, NULL);

        for (int y = by - ((by % 16) + 16) % 16; y < by + bh; y += 16)
            for (int x = bx - ((bx % 16) + 16) % 16; x < bx + bw; x += 16)
                UiG_Mask(m, x, y, UiMix(f.lo, f.base, 0.55));
    }
    UiW_TitleTab(0, 8, "CHOOSE A POK" UI_CAPS_E_ACUTE "MON", &tab, 0, NULL);
    Bag(t);
    Card();
    Message(t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sS.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sS.ready || NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        sGoal = -1;
        if (sS.phase == NATIVE_STARTER_CONFIRM)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sS.phase == NATIVE_STARTER_CONFIRM && hit->id == UI_HIT_YESNO)
    {
        sGoal = hit->index;
        sGoalSteps = 0;
        GoalStep();
        return true;
    }
    if (sS.phase != NATIVE_STARTER_BROWSE)
        return false;
    switch (hit->id)
    {
    case HIT_BALL:
        sGoal = hit->index;
        sGoalSteps = 0;
        GoalStep();
        return true;
    case HIT_PAGE_UP:
        NativeUi_QueuePress(DPAD_UP);
        return true;
    case HIT_PAGE_DOWN:
        NativeUi_QueuePress(DPAD_DOWN);
        return true;
    case HIT_DOT:
        if (hit->index != Page())
        {
            int down = (hit->index - Page() + sS.pageCount) % sS.pageCount;

            if (down <= sS.pageCount / 2)
                NativeUi_QueuePresses(DPAD_DOWN, down);
            else
                NativeUi_QueuePresses(DPAD_UP, sS.pageCount - down);
        }
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (!sS.ready || dy == 0 || NativeUi_PressesPending())
        return false;
    if (sS.phase == NATIVE_STARTER_BROWSE)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Starters = {
    .name = "starters",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
