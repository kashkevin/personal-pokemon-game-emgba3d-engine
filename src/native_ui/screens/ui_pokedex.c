#ifdef ENABLE_NATIVE_UI
/*
 * 6 · Pokédex (the approved design's preview/js/screens/pokedex.js): the red
 * two-screen device, the entry pane on the left and the list on the right,
 * over the engine's Pokédex (pokedex.c), which keeps every rule: the Hoenn
 * and National lists, their order, what seen and caught Pokémon show, the
 * START menu, the search (SELECT) and the entry page's AREA, CRY and SIZE
 * pages (the game's own pictures). The design's generation tabs become the
 * game's two lists (HOENN, NATIONAL); its FIND box is not in the game (marked
 * PLANNED); its FORM and SHINY buttons give their places to the game's SIZE
 * and CANCEL (the game's entry page has AREA, CRY, SIZE, CANCEL); the forms
 * count stays, marked PENDING as in the design. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "pokedex.h"
#include "pokedex_common.h"
#include "pokemon.h"
#include "constants/characters.h"
#include "constants/pokedex.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_ENTRY,
    HIT_BUTTON,
    HIT_ORDER,
    HIT_RAIL,
    HIT_BACKDROP,
};

#define ROW_H 22
#define VIS 10
#define BUTTONS 4

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_No[] = _("{NO}");
static const u8 sText_Dashes[] = _("----------");
static const u8 sText_Pokemon[] = _(" Pokémon");
static const u8 sText_UnknownPokemon[] = _("??? Pokémon");
static const u8 sText_Unknown[] = _("???");
static const u8 sText_SeenNotCaught[] = _("Seen, not caught.");
static const u8 sText_NotSeen[] = _("Not seen yet.");
static const u8 sText_SeenLine1[] = _("The rest of this entry fills in");
static const u8 sText_SeenLine2[] = _("once it has been caught.");
static const u8 sText_UnseenLine1[] = _("There is no data for a Pokémon");
static const u8 sText_UnseenLine2[] = _("that hasn't been seen.");
static const u8 sText_NoEntry[] = _("No entry");
static const u8 sText_NothingToList[] = _("Nothing to list.");
static const u8 sText_NothingHere[] = _("No Pokémon to list here.");
static const u8 sText_AllTypes[] = _("All types");
static const u8 sText_Results[] = _("Results");
static const u8 sText_Numerical[] = _("Numerical");
static const u8 sText_AtoZ[] = _("A to Z");
static const u8 sText_Heaviest[] = _("Heaviest");
static const u8 sText_Lightest[] = _("Lightest");
static const u8 sText_Tallest[] = _("Tallest");
static const u8 sText_Smallest[] = _("Smallest");
static const u8 *const sOrderNames[] = {sText_Numerical, sText_AtoZ, sText_Heaviest, sText_Lightest, sText_Tallest, sText_Smallest};
static const u8 sText_FootAtoZ[] = _("Seen Pokémon from A to Z");
static const u8 sText_FootHeaviest[] = _("Caught Pokémon, heaviest first");
static const u8 sText_FootLightest[] = _("Caught Pokémon, lightest first");
static const u8 sText_FootTallest[] = _("Caught Pokémon, tallest first");
static const u8 sText_FootSmallest[] = _("Caught Pokémon, smallest first");
static const u8 *const sOrderFooters[] = {NULL, sText_FootAtoZ, sText_FootHeaviest, sText_FootLightest, sText_FootTallest, sText_FootSmallest};
static const u8 sText_SearchResults[] = _("Search results");
static const u8 sText_Area[] = _("Habitat map");
static const u8 sText_Cry[] = _("Play its cry");
static const u8 sText_Size[] = _("Its size beside yours");
static const u8 sText_SizeLocked[] = _("Catch it to compare sizes.");
static const u8 sText_Cancel[] = _("Back to the list");
static const u8 *const sButtonHelp[BUTTONS] = {sText_Area, sText_Cry, sText_Size, sText_Cancel};
static const char *const sButtonLabels[BUTTONS] = {"AREA", "CRY", "SIZE", "CANCEL"};
static const u8 sText_MenuBackToList[] = _("BACK TO LIST");
static const u8 sText_MenuListTop[] = _("LIST TOP");
static const u8 sText_MenuListBottom[] = _("LIST BOTTOM");
static const u8 sText_MenuBackToDex[] = _("BACK TO POKéDEX");
static const u8 sText_MenuClose[] = _("CLOSE POKéDEX");
static const u8 sText_Move[] = _("Move");
static const u8 sText_Page[] = _("Page");
static const u8 sText_Entry[] = _("Entry");
static const u8 sText_Search[] = _("Search");
static const u8 sText_Menu[] = _("Menu");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Button[] = _("Button");
static const u8 sText_PrevNext[] = _("Prev/next");
static const u8 sText_Press[] = _("Press");
static const u8 sText_List[] = _("List");
static const u8 sText_Metres[] = _(" m");
static const u8 sText_Kilograms[] = _(" kg");
static const u8 sText_Pounds[] = _(" lbs.");
static const u8 sText_Feet[] = _("’");
static const u8 sText_Inches[] = _("”");

/* ---------------------------------------------------------------- captured state */

struct Entry
{
    u16 species;
    u32 personality;
    bool8 seen;
    bool8 owned;
    u16 shownNum;
    const u8 *name;
    u8 category[48];
    s8 types[2];
    u16 height;          /* decimetres */
    u16 weight;          /* hectograms */
    const u8 *description;
    u16 forms;
};

static struct NativePokedex sP;
static struct NativePokedexRow sRows[VIS];
static int sRowCount;
static int sFirst;
static u16 sFirstDexNum;         /* the list's first national number (the rail's generation ticks) */
static u16 sFirstShown;          /* the numbers shown on the list's first and last rows */
static u16 sLastShown;
static struct Entry sE;
static u16 sEntryDexNum = 0xFFFF;
static int sEntryT;              /* frames since this entry came up (its front animation) */
static int sLed;                 /* the activity light: frames left */
static u16 sLastSelected = 0xFFFF;
static bool sSettled;            /* the device has been up since the last fade from elsewhere */
static bool sFresh = true;
/* mouse goals: a list position (the game's own presses until it gets there),
 * then maybe A; an entry-page button to press once the entry page reads buttons */
static int sGoal = -1;
static bool sGoalThenA;
static int sGoalFrames;
static int sButtonGoal = -1;
static int sButtonFrames;

static bool Detect(void)
{
    struct NativePokedex p;

    if (!Pokedex_NativeUiState(&p))
    {
        /* not in the Pokédex: the next time it opens, the list starts around its cursor */
        sSettled = false;
        sFresh = true;
        sGoal = sButtonGoal = -1;
        return false;
    }
    if (p.phase == NATIVE_DEX_NONE || p.phase == NATIVE_DEX_PAGE)
    {
        /* the game's own pages (AREA, CRY, SIZE, the search) */
        sSettled = false;
        return false;
    }
    return true;
}

static int FormCount(u16 species)
{
    const u16 *forms = species < NUM_SPECIES ? gSpeciesInfo[species].formSpeciesIdTable : NULL;
    int n = 0;

    if (forms == NULL)
        return 1;
    while (forms[n] != FORM_SPECIES_END && n < 64)
        n++;
    return n > 0 ? n : 1;
}

static void CaptureEntry(void)
{
    const struct NativePokedexRow *r = &sP.entry;

    if (r->dexNum == sEntryDexNum && sE.species == r->species && sE.seen == r->seen && sE.owned == r->owned)
    {
        sEntryT++;
        return;
    }
    memset(&sE, 0, sizeof(sE));
    sEntryDexNum = r->dexNum;
    sEntryT = 0;
    sE.types[0] = sE.types[1] = -1;
    if (r->dexNum == 0xFFFF || r->dexNum == 0 || r->species == SPECIES_NONE)
        return;
    sE.species = r->species;
    sE.personality = r->personality;
    sE.seen = r->seen;
    sE.owned = r->owned;
    sE.shownNum = r->shownNum;
    sE.name = GetSpeciesName(r->species);
    sE.category[0] = EOS;
    UiText_Append(sE.category, sizeof(sE.category), GetSpeciesCategory(r->species));
    UiText_Append(sE.category, sizeof(sE.category), sText_Pokemon);
    sE.types[0] = (s8)UiMon_Type(GetSpeciesType(r->species, 0));
    sE.types[1] = (s8)UiMon_Type(GetSpeciesType(r->species, 1));
    if (sE.types[1] == sE.types[0])
        sE.types[1] = -1;
    sE.height = GetSpeciesHeight(r->species);
    sE.weight = GetSpeciesWeight(r->species);
    sE.description = GetSpeciesPokedexDescription(r->species);
    sE.forms = FormCount(r->species);
}

static bool ListPhase(void)
{
    return sP.phase == NATIVE_DEX_LIST || sP.phase == NATIVE_DEX_SCROLLING;
}

static void GoalStep(void)
{
    if (sGoal >= 0)
    {
        bool waiting = sP.phase == NATIVE_DEX_LOADING || sP.phase == NATIVE_DEX_INFO_LOADING || sP.phase == NATIVE_DEX_INFO;

        if (++sGoalFrames > 900 || (!ListPhase() && !waiting) || sGoal >= sP.listCount)
        {
            sGoal = -1;
        }
        else if (sP.phase == NATIVE_DEX_LIST && sP.ready && !NativeUi_PressesPending())
        {
            int d = sGoal - (int)sP.selected;

            if (d == 0)
            {
                if (sGoalThenA)
                    NativeUi_QueuePress(A_BUTTON);
                sGoal = -1;
            }
            else if (abs(d) >= 7)
            {
                /* the game's page jump (seven rows); nearer rows are held (FilterKeys) */
                NativeUi_QueuePress(d > 0 ? DPAD_RIGHT : DPAD_LEFT);
            }
        }
    }
    if (sButtonGoal >= 0)
    {
        bool waiting = sP.phase == NATIVE_DEX_LIST || sP.phase == NATIVE_DEX_OPENING || sP.phase == NATIVE_DEX_INFO_LOADING;

        if (++sButtonFrames > 600 || (sP.phase != NATIVE_DEX_INFO && !waiting))
        {
            sButtonGoal = -1;
        }
        else if (sP.phase == NATIVE_DEX_INFO && sP.ready && !NativeUi_PressesPending())
        {
            int d = sButtonGoal - (int)sP.selectedScreen;

            if (d == 0)
            {
                NativeUi_QueuePress(A_BUTTON);
                sButtonGoal = -1;
            }
            else
            {
                NativeUi_QueuePresses(d > 0 ? DPAD_RIGHT : DPAD_LEFT, abs(d));
            }
        }
    }
}

static void Capture(void)
{
    Pokedex_NativeUiState(&sP);
    switch (sP.phase)
    {
    case NATIVE_DEX_LIST:
    case NATIVE_DEX_SCROLLING:
    case NATIVE_DEX_MENU:
    case NATIVE_DEX_INFO:
        sSettled = true;
        break;
    case NATIVE_DEX_LEAVING:
    case NATIVE_DEX_CLOSING:
        sSettled = false;
        break;
    default:
        break;
    }
    if (sP.listCount == 0 && sP.phase == NATIVE_DEX_LOADING)
        return;
    if (sP.selected != sLastSelected)
    {
        if (sLastSelected != 0xFFFF && !sFresh)
            sLed = 8;
        sLastSelected = sP.selected;
    }
    else if (sLed > 0)
    {
        sLed--;
    }
    if (sFresh)
    {
        sFirst = (int)sP.selected - 4;
        sFresh = false;
    }
    if ((int)sP.selected < sFirst)
        sFirst = sP.selected;
    if ((int)sP.selected >= sFirst + VIS)
        sFirst = (int)sP.selected - VIS + 1;
    if (sFirst > (int)sP.listCount - VIS)
        sFirst = (int)sP.listCount - VIS;
    if (sFirst < 0)
        sFirst = 0;
    sRowCount = (int)Pokedex_NativeUiRows((u32)sFirst, VIS, sRows);
    {
        struct NativePokedexRow first, last;

        sFirstDexNum = Pokedex_NativeUiRows(0, 1, &first) == 1 ? first.dexNum : 1;
        sFirstShown = sP.listCount > 0 ? first.shownNum : 0;
        sLastShown = sP.listCount > 0 && Pokedex_NativeUiRows(sP.listCount - 1, 1, &last) == 1 ? last.shownNum : 0;
    }
    CaptureEntry();
    if (sE.seen)
        UiGfx_PrepareMonPic(sE.species, sE.personality, false, false);
    GoalStep();
}

/* ---------------------------------------------------------------- the design's palette */

struct DexPal
{
    struct UiRamp dev;
    UiRGBA bg, bg2, bezel, bezelHi, rowSel, rowSelHi, rowSelLo, stripe, rule, muted;
    struct UiRamp lens;
};

static struct DexPal Pal(void)
{
    struct DexPal c;

    c.dev = UiRampOf(UI_P_CMD_FIGHT, 0.38, 0.28, 0.62);
    c.bg = UiMix(c.dev.ink, UI_P_INK, 0.35);
    c.bg2 = UiMix(c.bg, c.dev.lo, 0.3);
    c.bezel = UiMix(UI_P_INK, c.dev.ink, 0.35);
    c.bezelHi = UiMix(UI_P_INK, c.dev.ink, 0.1);
    c.rowSel = UiMix(c.dev.hi, UI_P_PAPER, 0.55);
    c.rowSelHi = UiMix(c.dev.hi, UI_P_PAPER, 0.8);
    c.rowSelLo = UiMix(c.dev.hi, c.dev.base, 0.35);
    c.stripe = UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35);
    c.rule = UiMix(UI_P_CREAM_LO, c.dev.lo, 0.25);
    c.lens = UiRampOf(UI_P_TYPES_WATER, 0.45, 0.3, 0.62);
    c.muted = UI_HEX(0x909090);
    return c;
}

/* ---------------------------------------------------------------- small pieces */

/* the design's markers (original pixel art): caught = a check stamp, seen = an eye */
static void CaughtMark(int x, int y)
{
    static const char *const sRows[] = {".#######.", "#ggggggg#", "#gggggwg#", "#ggggwwg#", "#gwgwwgg#",
                                        "#gwwwggg#", "#ggwgggg#", "#ggggggg#", ".#######."};
    static const struct UiPixPal sPal[] = {{'#', UI_HEX(0x2A5A30)}, {'g', UI_HEX(0x58B048)}, {'w', UI_HEX(0xF8F8F8)}};

    UiW_Pix("dexcaught", sRows, 9, sPal, 3, x, y, false);
}

static void SeenMark(int x, int y)
{
    static const char *const sRows[] = {"...#####...", ".##wwwww##.", "#wwww#wwww#", "#www###www#", "#wwww#wwww#",
                                        ".##wwwww##.", "...#####..."};
    static const struct UiPixPal sPal[] = {{'#', UI_HEX(0x404858)}, {'w', UI_HEX(0xF8F8F8)}};

    UiW_Pix("dexseen", sRows, 7, sPal, 2, x, y, false);
}

static void QuestionMark(int x, int y)
{
    static const char *const sRows[] = {"..######..", ".########.", "###oooo###", "##o....###", ".......###",
                                        "......###.", ".....###..", "....###...", "....###...", "....###...",
                                        "..........", "....###...", "....###...", "....ooo..."};
    static const struct UiPixPal sPal[] = {{'#', UI_HEX(0xB8B8C8)}, {'o', UI_HEX(0x9898A8)}};

    UiW_Pix("dexq", sRows, 14, sPal, 2, x, y, false);
}

static bool GridAt(int x, int y, const void *ctx)
{
    (void)ctx;
    return x % 8 == 0 || y % 8 == 0;
}

/* a rounded panel with the design's 8 px grid inside its outline */
static void GridPanel(int x, int y, int w, int h, UiRGBA body, UiRGBA ink, UiRGBA grid)
{
    const struct UiMask *m = UiM_Rounded4(w, h, 2, 2, 2, 2);

    UiG_Paint(m, x, y, body, &(struct UiPaint){ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    UiG_Mask(UiM_And(UiM_Erode4(m), UiM_Fn(w, h, "dexgrid8", GridAt, NULL)), x, y, grid);
}

/* "{NO}025": the number as the game shows it */
static void DexNumber(u8 *dst, int size, int num, bool prefix)
{
    dst[0] = EOS;
    if (prefix)
        UiText_Append(dst, size, sText_No);
    UiText_AppendNumber(dst, size, num, sP.digits > 0 ? sP.digits : 3, false);
}

static void DexNumberAscii(char *dst, int size, int num)
{
    snprintf(dst, (size_t)size, sP.digits == 4 ? "%04d" : "%03d", num % 10000);
}

/* Gen 3's conversions (pokedex.c ConvertMonHeightToImperialString, ...WeightTo...) */
static void HeightText(u8 *dst, int size, u32 dm, bool imperial)
{
    dst[0] = EOS;
    if (imperial)
    {
        u32 inches = (dm * 10000) / 254, feet;

        if (inches % 10 >= 5)
            inches += 10;
        feet = inches / 120;
        inches = (inches - feet * 120) / 10;
        UiText_AppendNumber(dst, size, feet, 1, false);
        UiText_Append(dst, size, sText_Feet);
        UiText_AppendNumber(dst, size, inches, 2, false);
        UiText_Append(dst, size, sText_Inches);
    }
    else
    {
        UiText_AppendNumber(dst, size, dm / 10, 1, false);
        UiText_AppendAscii(dst, size, ".");
        UiText_AppendNumber(dst, size, dm % 10, 1, false);
        UiText_Append(dst, size, sText_Metres);
    }
}

static void WeightText(u8 *dst, int size, u32 hg, bool imperial)
{
    dst[0] = EOS;
    if (imperial)
    {
        u32 lbs = (u32)(((u64)hg * 10000000) / 453592);

        if (lbs % 10 >= 5)
            lbs += 10;
        UiText_AppendNumber(dst, size, lbs / 100, 1, false);
        UiText_AppendAscii(dst, size, ".");
        UiText_AppendNumber(dst, size, (lbs % 100) / 10, 1, false);
        UiText_Append(dst, size, sText_Pounds);
    }
    else
    {
        UiText_AppendNumber(dst, size, hg / 10, 1, false);
        UiText_AppendAscii(dst, size, ".");
        UiText_AppendNumber(dst, size, hg % 10, 1, false);
        UiText_Append(dst, size, sText_Kilograms);
    }
}

/* ---------------------------------------------------------------- the device frame */

static void DrawDevice(const struct DexPal *c, int t)
{
    const struct UiRamp *dv = &c->dev;
    const UiRGBA leds[3] = {UI_P_CURSOR, UI_P_TYPES_ELECTRIC, UI_P_TYPES_GRASS};
    const bool on[3] = {true, (t / 32) % 2 == 0, sLed > 0};
    struct UiPattern stripes = UiPat_HStripe(2, 1, 0);
    bool hoenn = sP.dexMode == DEX_MODE_HOENN;
    int lx = 370;
    char num[16];
    u8 b[16];

    UiG_Paint(UiM_Rounded4(624, 344, 6, 6, 6, 6), 8, 8, dv->base, &(struct UiPaint){dv->ink, dv->hi, dv->lo, 2, 2, NULL, 0});
    /* the lens, three lights, the engraved title */
    UiG_Paint(UiM_Circle(24), 14, 11, UI_P_PAPER, &(struct UiPaint){dv->ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiG_Paint(UiM_Circle(18), 17, 14, c->lens.base, &(struct UiPaint){c->lens.ink, c->lens.hi, c->lens.lo, 2, 0, NULL, 0});
    UiG_Rect(21, 17, 3, 2, UI_HEX(0xF8F8F8));
    UiG_Put(21, 19, c->lens.hi);
    for (int i = 0; i < 3; i++)
    {
        UiRGBA col = on[i] ? leds[i] : UiMix(leds[i], dv->ink, 0.55);

        UiG_Paint(UiM_Circle(7), 44 + i * 10, 14, col,
                  &(struct UiPaint){dv->ink, on[i] ? UiMix(col, UI_HEX(0xFFFFFF), 0.5) : UI_NONE, UI_NONE, 0, 0, NULL, 0});
    }
    UiCaps_Draw(46, 25, "POK" UI_CAPS_E_ACUTE "DEX", UI_P_TEXT_W, dv->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(98, 25, sP.searchResults ? "SEARCH RESULTS" : hoenn ? "HOENN MODE" : "NATIONAL MODE", dv->hi, UI_NONE,
                UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    /* the totals the game counts (the list's mode) */
    UiG_Paint(UiM_Rounded4(190, 26, 3, 3, 3, 3), lx - 2, 10, c->bezel, &(struct UiPaint){dv->ink, UI_NONE, c->bezelHi, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(186, 22, 2, 2, 2, 2), lx, 12, UI_P_CREAM,
              &(struct UiPaint){UI_NONE, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, &stripes, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.25)});
    UiCaps_Draw(lx + 6, 20, hoenn ? "HOENN" : "NATL", dv->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    SeenMark(lx + 44, 19);
    UiCaps_Draw(lx + 58, 20, "SEEN", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(num, sizeof(num), "%u", (unsigned)(hoenn ? sP.regionalSeen : sP.seenCount));
    UiText_FromAscii(b, sizeof(b), num);
    UiText_Draw(UI_FONT_NORMAL, lx + 112, 15, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    CaughtMark(lx + 120, 18);
    UiCaps_Draw(lx + 133, 20, "OWN", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(num, sizeof(num), "%u", (unsigned)(hoenn ? sP.regionalOwn : sP.ownCount));
    UiText_FromAscii(b, sizeof(b), num);
    UiText_Draw(UI_FONT_NORMAL, lx + 180, 15, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    /* the speaker grill */
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 6; k++)
        {
            UiG_Rect(572 + k * 7 + (r % 2) * 3, 18 + r * 5, 3, 2, dv->ink);
            UiG_Put(572 + k * 7 + (r % 2) * 3, 18 + r * 5, UiMix(dv->ink, dv->lo, 0.5));
        }
    /* the screens' bezels and the hinge between them */
    UiG_Paint(UiM_Rounded4(288, 292, 3, 3, 3, 3), 14, 39, c->bezel, &(struct UiPaint){dv->ink, c->bezelHi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(308, 292, 3, 3, 3, 3), 318, 39, c->bezel, &(struct UiPaint){dv->ink, c->bezelHi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(12, 292, 2, 2, 2, 2), 303, 39, dv->lo, &(struct UiPaint){dv->ink, dv->base, UI_NONE, 0, 0, NULL, 0});
    for (int y = 48; y < 324; y += 6)
    {
        UiG_HLine(305, y, 8, dv->ink);
        UiG_HLine(305, y + 1, 8, UiMix(dv->lo, dv->base, 0.5));
    }
    /* the screws */
    for (int k = 0; k < 2; k++)
    {
        int sx = k == 0 ? 12 : 621, sy = 335;

        UiG_Paint(UiM_Circle(6), sx, sy, dv->lo, &(struct UiPaint){dv->ink, dv->hi, UI_NONE, 0, 0, NULL, 0});
        UiG_HLine(sx + 1, sy + 3, 4, dv->ink);
    }
}

/* ---------------------------------------------------------------- the entry pane (left screen) */

static void EntryButtons(const struct DexPal *c, int x, int y, int h, int t)
{
    const struct UiRamp *dv = &c->dev;
    bool focus = sP.phase == NATIVE_DEX_INFO || sP.phase == NATIVE_DEX_INFO_LOADING || sP.phase == NATIVE_DEX_OPENING;
    int by = y + h - 50, bw = 64;
    const u8 *msg = NULL;

    (void)t;
    for (int i = 0; i < BUTTONS; i++)
    {
        int bx = x + 8 + i * (bw + 4);
        bool avail = sE.seen && (i != SIZE_SCREEN || sE.owned);
        bool sel = focus && sP.selectedScreen == i;
        UiRGBA col = !avail ? UI_HEX(0xA8A0A0) : i == CANCEL_SCREEN ? UiMix(dv->base, UI_P_INK, 0.3) : dv->base;
        struct UiRamp r = UiRampOf(col, 0.42, 0.25, 0.62);
        const struct UiMask *m = UiM_Rounded4(bw, 22, 3, 3, 3, 3);

        if (sel)
            UiW_SelectFrame(m, bx, by, UI_NONE, UI_NONE, 0);
        UiG_Paint(m, bx, by, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
        UiCaps_Draw(bx + (bw >> 1), by + 7, sButtonLabels[i], UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
        if (sE.seen)
            UiG_Hit(HIT_BUTTON, i, bx, by, bw, 22);
    }
    if (focus && sE.seen && sP.selectedScreen < BUTTONS)
        msg = sP.selectedScreen == SIZE_SCREEN && !sE.owned ? sText_SizeLocked : sButtonHelp[sP.selectedScreen];
    if (msg != NULL)
    {
        UiW_Cursor(x + 9, by - 17, 0, dv->base);
        UiText_Draw(UI_FONT_SMALL, x + 18, by - 19, msg, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void DrawEntry(const struct DexPal *c, int t)
{
    const struct UiRamp *dv = &c->dev;
    int x = 18, y = 43, w = 280, h = 284;
    int sx = x + 6, sy = y + 25, sw = 128, sh = 112, gy = sy + sh - 14, ix0 = sx + sw + 8, iy = sy, dy = sy + sh + 6;
    struct UiRamp grey = {UI_HEX(0xD8D8E0), UI_HEX(0xB8B8C8), UI_HEX(0x9090A0), UI_P_INK};
    const struct UiRamp *tr;
    UiRGBA stageBody;
    u8 b[64];

    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, UI_P_PAPER, &(struct UiPaint){UI_NONE, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiG_Hit(HIT_ENTRY, 0, x, y, w, h);
    UiG_Paint(UiM_Rounded(w - 4, 19, UiCornerR(1), UiCornerChamfer(6), UiCornerR(0), UiCornerR(0)), x + 2, y + 2, dv->base,
              &(struct UiPaint){UI_NONE, dv->hi, dv->lo, 0, 0, NULL, 0});
    if (sE.species == SPECIES_NONE)
    {
        /* nothing listed: the entry's layout with every field empty */
        UiText_Draw(UI_FONT_NORMAL, x + 8, y + 3, sText_NoEntry, UI_P_TEXT_W, dv->ink, UI_ALIGN_LEFT, 0);
        GridPanel(x + 6, y + 25, 128, 112, UI_HEX(0xF0F0F4), UI_HEX(0xA8A8B8), UI_HEX(0xE0E0E8));
        QuestionMark(x + 64, y + 59);
        UiG_Text(x + 142, y + 24, sText_NothingToList);
        EntryButtons(c, x, y, h, t);
        return;
    }
    /* the header: number, name, caught or seen */
    {
        int hx;

        DexNumber(b, sizeof(b), sE.shownNum, true);
        UiText_Draw(UI_FONT_NORMAL, x + 8, y + 3, b, UI_P_TEXT_W, dv->ink, UI_ALIGN_LEFT, 0);
        hx = x + 16 + UiText_Width(UI_FONT_NORMAL, b, 0);
        UiText_Draw(UI_FONT_NORMAL, hx, y + 3, sE.seen ? sE.name : sText_Dashes, UI_P_TEXT_W, dv->ink, UI_ALIGN_LEFT, 0);
        if (sE.owned)
        {
            CaughtMark(x + 214, y + 7);
            UiCaps_Draw(x + 226, y + 8, "CAUGHT", UI_P_TEXT_W, dv->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        }
        else if (sE.seen)
        {
            SeenMark(x + 220, y + 8);
            UiCaps_Draw(x + 234, y + 8, "SEEN", UI_P_TEXT_W, dv->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        }
    }
    /* the picture stage: caught in colour, seen as a silhouette, else a question mark */
    tr = sE.owned && sE.types[0] >= 0 ? &gUiTypeRamps[sE.types[0]] : &grey;
    stageBody = UiMix(tr->hi, UI_HEX(0xFFFFFF), 0.6);
    GridPanel(sx, sy, sw, sh, stageBody, UiMix(tr->lo, UI_P_INK, 0.35), UiMix(tr->hi, UI_HEX(0xFFFFFF), 0.3));
    UiG_Mask(UiM_Ellipse(76, 12), sx + (sw >> 1) - 38, gy - 5, UiMix(stageBody, tr->lo, 0.45));
    UiG_Save();
    UiG_Clip(sx + 1, sy + 1, sw - 2, sh - 2);
    if (sE.owned)
    {
        int frame = UiGfx_MonPicFrameAt(sE.species, sE.personality, false, false, sEntryT);

        if (!UiGfx_MonPic(sE.species, sE.personality, false, false, frame, sx + (sw >> 1), gy + 1, NULL))
            UiW_Missing(sx + (sw >> 1) - 24, gy - 48, 48, 48);
    }
    else if (sE.seen)
    {
        struct UiFilter sil = {0};

        sil.silhouette = UiMix(UI_P_INK, dv->ink, 0.35);
        if (!UiGfx_MonPic(sE.species, sE.personality, false, false, 0, sx + (sw >> 1), gy + 1, &sil))
            UiW_Missing(sx + (sw >> 1) - 24, gy - 48, 48, 48);
    }
    else
    {
        QuestionMark(sx + (sw >> 1) - 6, sy + 34);
    }
    UiG_Restore();
    /* the facts beside the stage */
    if (sE.owned)
    {
        int bx = ix0;
        u8 v[24];

        UiText_Draw(UiText_Width(UI_FONT_NORMAL, sE.category, 0) > w - (ix0 - x) - 8 ? UI_FONT_NARROW : UI_FONT_NORMAL,
                    ix0, iy - 1, sE.category, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        for (int k = 0; k < 2; k++)
            if (sE.types[k] >= 0)
                bx = UiW_TypeBadge(bx, iy + 18, sE.types[k], 0) + 3;
        /* the game's units first (its UNITS setting), the other ones beside */
        UiCaps_Draw(ix0, iy + 41, "HT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        HeightText(v, sizeof(v), sE.height, UNITS == UNITS_IMPERIAL);
        UiG_Text(ix0 + 16, iy + 36, v);
        HeightText(v, sizeof(v), sE.height, UNITS != UNITS_IMPERIAL);
        UiText_Draw(UI_FONT_SMALL, ix0 + 72, iy + 38, v, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiCaps_Draw(ix0, iy + 59, "WT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        WeightText(v, sizeof(v), sE.weight, UNITS == UNITS_IMPERIAL);
        UiG_Text(ix0 + 16, iy + 54, v);
        WeightText(v, sizeof(v), sE.weight, UNITS != UNITS_IMPERIAL);
        UiText_Draw(UI_FONT_SMALL, ix0 + 72, iy + 56, v, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        /* the forms count (the in-game form catalogue is pending, as in the design) */
        UiG_HLine(ix0, iy + 76, w - (ix0 - x) - 8, UI_P_PAPER_LO);
        UiCaps_Draw(ix0, iy + 82, "FORMS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (sE.forms > 1)
            UiW_Planned(ix0 + 34, iy + 80, "PENDING");
        {
            char f[16];

            snprintf(f, sizeof(f), "1/%u", (unsigned)sE.forms);
            UiCaps_Draw(x + w - 10, iy + 82, f, sE.forms > 1 ? dv->base : UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_RIGHT, UI_NONE);
        }
        UiText_Draw(UI_FONT_NORMAL, ix0, iy + 90, sE.name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else
    {
        UiG_Text(ix0, iy - 1, sE.seen ? sText_UnknownPokemon : sText_Dashes);
        for (int k = 0; k < 2; k++)
        {
            UiG_Paint(UiM_Rounded4(40, 13, 1, 1, 1, 1), ix0 + k * 43, iy + 18, UI_HEX(0xB8B8C4),
                      &(struct UiPaint){UI_HEX(0x686878), UI_HEX(0xE0E0E8), UI_HEX(0x9898A8), 0, 0, NULL, 0});
            UiCaps_Draw(ix0 + k * 43 + 20, iy + 21, "???", UI_P_TEXT_W, UI_HEX(0x686878), UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
        }
        UiCaps_Draw(ix0, iy + 41, "HT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Text(ix0 + 16, iy + 36, sText_Unknown);
        UiCaps_Draw(ix0, iy + 59, "WT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Text(ix0 + 16, iy + 54, sText_Unknown);
        UiG_HLine(ix0, iy + 76, w - (ix0 - x) - 8, UI_P_PAPER_LO);
        UiText_Draw(UI_FONT_SMALL, ix0, iy + 82, sE.seen ? sText_SeenNotCaught : sText_NotSeen, UI_P_TEXT_MUTED,
                    UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    /* the entry's text */
    UiG_HLine(x + 6, dy - 2, w - 12, UiMix(dv->hi, UI_P_PAPER, 0.5));
    if (sE.owned && sE.description != NULL)
    {
        const u8 *starts[6];
        int lengths[6];
        int n = UiText_Wrap(sE.description, w - 20, UI_FONT_NORMAL, starts, lengths, 6);
        int pitch = n > 4 ? 14 : 16;

        for (int i = 0; i < n && i < 5; i++)
        {
            UiText_CopyN(b, sizeof(b), starts[i], lengths[i]);
            UiG_Text(x + 10, dy + i * pitch, b);
        }
    }
    else
    {
        UiText_Draw(UI_FONT_NORMAL, x + 10, dy, sE.seen ? sText_SeenLine1 : sText_UnseenLine1, UI_HEX(0xA0A0A0), UI_NONE, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, x + 10, dy + 16, sE.seen ? sText_SeenLine2 : sText_UnseenLine2, UI_HEX(0xA0A0A0), UI_NONE, UI_ALIGN_LEFT, 0);
    }
    EntryButtons(c, x, y, h, t);
}

/* ---------------------------------------------------------------- the list pane (right screen) */

static void DrawList(const struct DexPal *c, int t)
{
    const struct UiRamp *dv = &c->dev;
    int x = 322, y = 43, w = 300, h = 284;
    int lbx = x + 3, lbw = w - 17, lx = lbx + 7, lw = lbw - 9, ly = y + 48, cy = y + 24;
    int n = sP.listCount, rx = x + w - 11, rh = VIS * ROW_H + 4;
    bool cursor = sP.phase == NATIVE_DEX_LIST || sP.phase == NATIVE_DEX_SCROLLING || sP.phase == NATIVE_DEX_MENU;
    const char *tabs[2] = {"HOENN", "NATIONAL"};
    int tx = x + 3;
    u8 b[64];

    (void)h;
    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, UI_P_CREAM, &(struct UiPaint){UI_NONE, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    /* the game's two lists (its mode; SELECT's search changes it) */
    for (int i = 0; i < (sP.nationalEnabled ? 2 : 1); i++)
    {
        bool on = (i == 0) == (sP.dexMode == DEX_MODE_HOENN);
        int tw = UiCaps_Width(tabs[i]) + 18, th = on ? 17 : 15, ty = on ? y + 2 : y + 4;

        UiG_Paint(UiM_Rounded4(tw, th, 2, 2, 0, 0), tx, ty, on ? dv->base : UiMix(UI_P_CREAM_LO, dv->hi, 0.25),
                  &(struct UiPaint){on ? dv->ink : UiMix(UI_P_CREAM_LO, dv->lo, 0.5), on ? dv->hi : UI_P_CREAM_HI, UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(tx + (tw >> 1), ty + 5, tabs[i], on ? UI_P_TEXT_W : UiMix(dv->lo, UI_P_TEXT_MUTED, 0.4), on ? dv->ink : UI_NONE,
                    on ? UI_CAPS_OUTLINE : UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        tx += tw + 3;
    }
    UiG_Rect(x + 2, y + 19, w - 4, 2, dv->base);
    UiG_HLine(x + 2, y + 21, w - 4, dv->lo);
    /* FIND (the design's name search, not in the game), ORDER and TYPE (the game's search) */
    UiG_Paint(UiM_Rounded4(46, 19, 2, 0, 2, 0), x + 4, cy, dv->lo, &(struct UiPaint){dv->ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    UiGfx_Icon("search", x + 7, cy + 3, 0);
    UiCaps_Draw(x + 22, cy + 6, "FIND", UI_P_TEXT_W, dv->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(96, 19, 0, 2, 0, 2), x + 49, cy, UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.5),
              &(struct UiPaint){dv->ink, UI_P_PAPER_LO, UI_NONE, 0, 0, NULL, 0});
    for (int k = 0; k < 8; k++)
        UiG_HLine(x + 55 + k * 11, cy + 15, 9, UI_HEX(0xC0C0C0));
    UiW_Planned(x + 66, cy + 4, NULL);
    {
        int ox = x + 150, tx2 = ox + 80;

        UiG_Paint(UiM_Rounded4(76, 19, 2, 2, 2, 2), ox, cy, UiMix(dv->lo, dv->ink, 0.25), &(struct UiPaint){dv->ink, dv->lo, UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(ox + 5, cy + 6, UI_CAPS_UP UI_CAPS_DOWN, dv->hi, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiText_Draw(UI_FONT_SMALL, ox + 22, cy + 3, sOrderNames[sP.dexOrder < 6 ? sP.dexOrder : 0], UI_P_TEXT_W, dv->ink, UI_ALIGN_LEFT, 0);
        UiG_Hit(HIT_ORDER, 0, ox, cy, 76, 19);
        UiG_Paint(UiM_Rounded4(66, 19, 2, 2, 2, 2), tx2, cy, UiMix(dv->lo, dv->ink, 0.25), &(struct UiPaint){dv->ink, dv->lo, UI_NONE, 0, 0, NULL, 0});
        UiText_Draw(UI_FONT_SMALL, tx2 + 33, cy + 3, sP.searchResults ? sText_Results : sText_AllTypes, UI_P_TEXT_W, dv->ink,
                    UI_ALIGN_CENTER, 0);
        UiG_Hit(HIT_ORDER, 1, tx2, cy, 66, 19);
    }
    /* the rows on one paper sheet; the selected row is a red-chipped bar */
    UiG_Paint(UiM_Rounded4(lbw, VIS * ROW_H + 4, 1, 1, 1, 1), lbx, ly - 2, UI_P_PAPER,
              &(struct UiPaint){UiMix(UI_P_CREAM_LO, dv->lo, 0.45), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
    if (n == 0)
        UiG_Text(lx + 8, ly + 10, sText_NothingHere);
    for (int r = 0; r < sRowCount; r++)
    {
        const struct NativePokedexRow *row = &sRows[r];
        int idx = sFirst + r, ry = ly + r * ROW_H;
        bool sel = idx == sP.selected;
        char num[16];

        if (sel)
        {
            UiG_Paint(UiM_Rounded(lw, ROW_H, UiCornerR(2), UiCornerChamfer(6), UiCornerR(2), UiCornerR(2)), lx, ry, c->rowSel,
                      &(struct UiPaint){dv->lo, c->rowSelHi, c->rowSelLo, 0, 0, NULL, 0});
            UiG_Paint(UiM_Rounded4(29, ROW_H - 4, 1, 1, 1, 1), lx + 2, ry + 2, dv->base, &(struct UiPaint){UI_NONE, dv->hi, dv->lo, 0, 0, NULL, 0});
        }
        else if (r % 2 == 1)
        {
            UiG_Rect(lbx + 1, ry + 1, lbw - 2, ROW_H - 1, c->stripe);
        }
        if (!sel && r < VIS - 1 && idx + 1 != sP.selected)
            for (int xx = lx + 2; xx < lx + lw - 2; xx += 3)
                UiG_Put(xx, ry + ROW_H, c->rule);
        DexNumberAscii(num, sizeof(num), row->shownNum);
        UiCaps_Draw(lx + 17, ry + 8, num, sel ? UI_P_TEXT_W : row->seen ? UI_P_TEXT_MUTED : c->muted, sel ? dv->ink : UI_NONE,
                    sel ? UI_CAPS_OUTLINE : UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        if (row->seen)
        {
            int fr = sel ? (t / 10) % 2 : 0, hop = sel && fr == 1 ? 2 : 0;
            struct UiFilter sil = {0};

            sil.silhouette = UiMix(UI_P_INK, dv->ink, 0.3);
            UiG_Save();
            UiG_Clip(lx + 33, ry + 1, 32, ROW_H - 1);
            UiGfx_MonIcon(row->species, row->personality, false, fr, lx + 33, ry - 8 - hop, row->owned ? NULL : &sil);
            UiG_Restore();
            UiG_Text(lx + 68, ry + 3, GetSpeciesName(row->species));
            if (row->owned)
            {
                if (sP.dexOrder == ORDER_NUMERICAL || sP.dexOrder == ORDER_ALPHABETICAL)
                {
                    int t0 = UiMon_Type(GetSpeciesType(row->species, 0)), t1 = UiMon_Type(GetSpeciesType(row->species, 1));

                    if (t0 >= 0)
                        UiW_TypeBadge(lx + lw - 100, ry + 4, t0, 0);
                    if (t1 >= 0 && t1 != t0)
                        UiW_TypeBadge(lx + lw - 58, ry + 4, t1, 0);
                }
                else
                {
                    bool weight = sP.dexOrder == ORDER_HEAVIEST || sP.dexOrder == ORDER_LIGHTEST;

                    if (weight)
                        WeightText(b, sizeof(b), GetSpeciesWeight(row->species), UNITS == UNITS_IMPERIAL);
                    else
                        HeightText(b, sizeof(b), GetSpeciesHeight(row->species), UNITS == UNITS_IMPERIAL);
                    UiG_TextEx(lx + lw - 20, ry + 3, b, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
                }
                CaughtMark(lx + lw - 14, ry + 6);
            }
            else
            {
                SeenMark(lx + lw - 15, ry + 7);
            }
        }
        else
        {
            UiText_Draw(UI_FONT_NORMAL, lx + 68, ry + 3, sText_Dashes, c->muted, UI_NONE, UI_ALIGN_LEFT, 0);
        }
        if (sel && cursor)
            UiW_Cursor(lbx + 1, ry + 6, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ROW, idx, lbx, ry, lbw, ROW_H);
    }
    /* the scroll rail, with the generations' ticks on the National list */
    UiG_Paint(UiM_Rounded4(7, rh, 2, 2, 2, 2), rx, ly - 2, UiMix(dv->lo, dv->ink, 0.3), &(struct UiPaint){dv->ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    if (sP.dexOrder == ORDER_NUMERICAL && sP.dexMode != DEX_MODE_HOENN && !sP.searchResults && n > 0)
    {
        static const u16 sGenEnds[] = {151, 251, 386, 493, 649, 721, 809, 905};

        for (size_t k = 0; k < sizeof(sGenEnds) / sizeof(sGenEnds[0]); k++)
        {
            int pos = (int)sGenEnds[k] - (int)sFirstDexNum + 1;

            if (pos > 0 && pos < n)
                UiG_HLine(rx + 1, ly - 2 + rh * pos / n, 5, dv->hi);
        }
    }
    if (n > VIS)
    {
        int th = rh * VIS / n, tyy;

        if (th < 12)
            th = 12;
        tyy = ly - 2 + (rh - th) * sFirst / (n - VIS);
        UiG_Paint(UiM_Rounded4(7, th, 2, 2, 2, 2), rx, tyy, UI_P_CREAM, &(struct UiPaint){dv->ink, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, 0});
        for (int k = 0; k < rh; k += 4)
            UiG_Hit(HIT_RAIL, k * (n - 1) / (rh - 1), rx - 2, ly - 2 + k, 11, 4);
    }
    else
    {
        UiG_Paint(UiM_Rounded4(7, rh, 2, 2, 2, 2), rx, ly - 2, UI_P_CREAM, &(struct UiPaint){dv->ink, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, 0});
    }
    /* the footer: what the list holds, and where the cursor is */
    {
        int fy = y + 268;
        char pos[24];

        b[0] = EOS;
        if (sP.dexOrder == ORDER_NUMERICAL || sP.dexOrder >= 6)
        {
            /* the numbers the list spans (it ends at the last Pokémon seen) */
            if (n > 0)
            {
                u8 d[12];

                UiText_AppendAscii(b, sizeof(b), "No.");
                DexNumber(d, sizeof(d), sFirstShown, false);
                UiText_Append(b, sizeof(b), d);
                UiText_AppendAscii(b, sizeof(b), "-");
                DexNumber(d, sizeof(d), sLastShown, false);
                UiText_Append(b, sizeof(b), d);
            }
        }
        else
        {
            UiText_Append(b, sizeof(b), sOrderFooters[sP.dexOrder]);
        }
        if (sP.searchResults)
        {
            UiText_AppendAscii(b, sizeof(b), " - ");
            UiText_Append(b, sizeof(b), sText_SearchResults);
        }
        UiText_Draw(UI_FONT_SMALL, x + 6, fy, b, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        snprintf(pos, sizeof(pos), "%d/%d", n > 0 ? (int)sP.selected + 1 : 0, n);
        UiCaps_Draw(x + w - 8, fy + 4, pos, dv->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
}

static void DrawHints(void)
{
    struct UiHintItem h[6];
    int n = 0;

    switch (sP.phase)
    {
    case NATIVE_DEX_LIST:
    case NATIVE_DEX_SCROLLING:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Move};
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Page};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Entry};
        h[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Search};
        h[n++] = (struct UiHintItem){UI_ACT_START, sText_Menu};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sP.searchResults ? sText_Back : sText_Close};
        break;
    case NATIVE_DEX_MENU:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_DEX_INFO:
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Button};
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_PrevNext};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Press};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_List};
        break;
    default:
        break;
    }
    if (n > 0)
        UiW_Hints(20, 337, h, n, true, 8);
}

static void DrawMenu(int t)
{
    struct UiMenuItem items[5];
    struct UiMenuOpt o = {0};
    int n = 0, bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Hit(HIT_BACKDROP, 0, bx, by, bw, bh);
    items[n++] = (struct UiMenuItem){sText_MenuBackToList, false};
    items[n++] = (struct UiMenuItem){sText_MenuListTop, false};
    items[n++] = (struct UiMenuItem){sText_MenuListBottom, false};
    if (sP.searchResults)
        items[n++] = (struct UiMenuItem){sText_MenuBackToDex, false};
    items[n++] = (struct UiMenuItem){sText_MenuClose, false};
    o.anchor = UI_ANCHOR_BOTTOMRIGHT;
    o.t = t;
    o.minW = 120;
    UiW_MenuBox(616, 322, items, n, sP.menuCursor, &o, NULL);
}

static void Draw(int t)
{
    struct DexPal c = Pal();
    int x0, y0, w, h;
    struct UiPattern dots = UiPat_Dots(8, 2, 0, 0, true);
    bool veil;

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, c.bg);
    UiG_FillPattern(&dots, x0, y0, w, h, c.bg2, t / 5, t / 5);
    DrawDevice(&c, t);
    if (!(sP.listCount == 0 && sP.phase == NATIVE_DEX_LOADING))
    {
        DrawEntry(&c, t);
        DrawList(&c, t);
        DrawHints();
        if (sP.phase == NATIVE_DEX_MENU)
            DrawMenu(t);
    }
    /* the game's fades: from and to its own pages and the field; within the
     * device (list <-> entry page) the device stays up */
    veil = sP.phase == NATIVE_DEX_LEAVING || sP.phase == NATIVE_DEX_CLOSING
        || (!sSettled && (sP.phase == NATIVE_DEX_LOADING || sP.phase == NATIVE_DEX_OPENING || sP.phase == NATIVE_DEX_INFO_LOADING));
    if (veil)
        UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static void SetGoal(int position, bool thenA)
{
    sGoal = position;
    sGoalThenA = thenA;
    sGoalFrames = 0;
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
    if (NativeUi_PressesPending() || sGoal >= 0 || sButtonGoal >= 0)
        return true;
    switch (sP.phase)
    {
    case NATIVE_DEX_LIST:
    case NATIVE_DEX_SCROLLING:
        switch (hit->id)
        {
        case HIT_ROW:
        case HIT_RAIL:
            /* the row, then its entry page (A: the game opens seen ones) */
            SetGoal(hit->index, hit->id == HIT_ROW);
            return true;
        case HIT_ENTRY:
            if (sE.seen)
                NativeUi_QueuePress(A_BUTTON);
            return true;
        case HIT_BUTTON:
            if (sE.seen && hit->index != CANCEL_SCREEN)
            {
                NativeUi_QueuePress(A_BUTTON);
                sButtonGoal = hit->index;
                sButtonFrames = 0;
            }
            return true;
        case HIT_ORDER:
            NativeUi_QueuePress(SELECT_BUTTON);
            return true;
        default:
            return false;
        }
    case NATIVE_DEX_MENU:
        if (hit->id == UI_HIT_MENU)
        {
            /* the menu's rows as the game numbers them (no BACK TO POKéDEX outside search results) */
            int row = hit->index;

            NativeUi_QueuePresses(row > sP.menuCursor ? DPAD_DOWN : DPAD_UP, abs(row - sP.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_DEX_INFO:
        switch (hit->id)
        {
        case HIT_BUTTON:
            sButtonGoal = hit->index;
            sButtonFrames = 0;
            return true;
        case HIT_ROW:
            if (hit->index != sP.selected)
            {
                /* back to the list, to that row, and its entry page */
                NativeUi_QueuePress(B_BUTTON);
                SetGoal(hit->index, true);
            }
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (NativeUi_PressesPending() || sGoal >= 0)
        return true;
    if (ListPhase() || sP.phase == NATIVE_DEX_MENU || sP.phase == NATIVE_DEX_INFO)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    /* the player's own buttons take over from a mouse goal */
    if (newKeys != 0)
    {
        sGoal = -1;
        sButtonGoal = -1;
    }
    /* nearby rows: hold the direction (the game scrolls while it is held) until the list gets there */
    if (sGoal >= 0 && ListPhase() && !NativeUi_PressesPending())
    {
        int d = sGoal - (int)sP.selected;

        if (d != 0 && abs(d) < 7)
            keys |= d > 0 ? DPAD_DOWN : DPAD_UP;
    }
    return keys;
}

static bool Ready(void)
{
    return sP.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Pokedex = {
    .name = "pokedex",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
