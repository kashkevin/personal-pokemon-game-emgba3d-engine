#ifdef ENABLE_NATIVE_UI
/*
 * The FRONTIER PASS (frontier_pass.c: the START menu's pass once the player has it; not one of the 31 designed
 * screens: the design's pieces). The pass at the game's own layout, twice its size, so the game's free cursor stands
 * where it stands in the game: the symbols earned (the seven facilities' silver and gold), the battle record, the
 * battle points, the FRONTIER MAP and the TRAINER CARD, CANCEL - the area under the cursor lit, the cursor drawn, the
 * game's line for that area. The Battle Frontier's map: the facilities on a plan of the island, their list with the one
 * it points at, its lines, the player's head where the game shows it.
 * The game keeps the input: a click walks the cursor onto an area with the pad held toward it (two pixels a frame, as
 * the game moves it) and presses A where A does something there; the map's rows are its UP and DOWN. Project-owned.
 */
#include "global.h"
#include "string_util.h"
#include "strings.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_AREA = UI_HIT_SCREEN,  /* index: the area (1-13) */
    HIT_FACILITY,              /* index: the map's row */
};

/* the areas, as frontier_pass.c's CURSOR_AREA_* (index area - 1 in the state's rectangles) */
enum
{
    AREA_MAP = 1,
    AREA_CARD,
    AREA_RECORD,
    AREA_CANCEL,
    AREA_POINTS,
    AREA_SYMBOLS,
    AREA_SYMBOL_FIRST,         /* the Tower's; the Dome's, the Palace's... after it */
};

#define SCALE 2
#define PASS_W (240 * SCALE)
#define PASS_H (160 * SCALE)

static const u8 sText_Title[] = _("FRONTIER PASS");
static const u8 sText_Map[] = _("FRONTIER MAP");
static const u8 sText_Card[] = _("TRAINER CARD");
static const u8 sText_Cancel[] = _("CANCEL");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_PutAway[] = _("Put away");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Select[] = _("Select");
static const u8 sText_You[] = _("YOU");

/* the pass's colour by the trainer card's stars, as the game's palettes change with them */
static const UiRGBA sStarColors[] = {
    UI_HEX(0x4878C8), UI_HEX(0xC87838), UI_HEX(0x8890A8), UI_HEX(0xD8A830), UI_HEX(0x9058B8),
};

static struct NativeFrontierPass sP;
static int sPassX, sPassY;     /* the pass's top left, logical pixels */

static bool Detect(void)
{
    struct NativeFrontierPass p;

    return FrontierPass_NativeUiState(&p);
}

static void Capture(void)
{
    FrontierPass_NativeUiState(&sP);
}

static bool Ready(void)
{
    return sP.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

/* an area's rectangle on the drawn pass */
static void AreaRect(int area, int *x, int *y, int *w, int *h)
{
    const s16 *r = sP.rects[area - 1];

    *x = sPassX + r[0] * SCALE;
    *y = sPassY + r[1] * SCALE;
    *w = (r[2] - r[0] + 1) * SCALE;
    *h = (r[3] - r[1] + 1) * SCALE;
}

/* the game's text, a line at a time (its \n breaks) */
static void Lines(int x, int y, const u8 *s, int lineH, int maxLines, UiRGBA fg, UiRGBA sh)
{
    u8 line[96];

    for (int n = 0; s != NULL && *s != EOS && n < maxLines; n++, y += lineH)
    {
        int len = 0;

        while (s[len] != EOS && s[len] != CHAR_NEWLINE && s[len] != CHAR_PROMPT_SCROLL && s[len] != CHAR_PROMPT_CLEAR)
            len++;
        UiText_CopyN(line, sizeof(line), s, len);
        UiText_Draw(UI_FONT_NORMAL, x, y, line, fg, sh, UI_ALIGN_LEFT, 0);
        s += len;
        if (*s != EOS)
            s++;
    }
}

static void Plate(int x, int y, int w, int h, bool lit, bool dim)
{
    UiRGBA base = lit ? UI_HEX(0xFFF0B8) : dim ? UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.6) : UiMix(UI_P_CREAM, UI_HEX(0xFFFFFF), 0.35);

    UiG_Paint(UiM_Rounded4(w, h, 6, 6, 6, 6), x, y, base,
              &(struct UiPaint){lit ? UI_HEX(0xD89818) : UI_P_CREAM_LO, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, lit ? 2 : 0, NULL, UI_NONE});
}

/* one of the seven symbols: a silver or gold medal, else its empty socket */
static void Symbol(int area, u8 level, int t)
{
    int x, y, w, h;
    int d = 26;

    AreaRect(area, &x, &y, &w, &h);
    x += (w - d) / 2;
    y += (h - d) / 2;
    if (level == 0)
    {
        const struct UiMask *m = UiM_Ellipse(d, d);

        UiG_Mask(m, x, y, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.7));
        UiG_Mask(UiM_Sub(m, UiM_Erode4(m)), x, y, UI_P_CREAM_LO);
        return;
    }
    {
        UiRGBA rim = level >= 2 ? UI_HEX(0xB07818) : UI_HEX(0x7880A0);
        UiRGBA face = level >= 2 ? UI_HEX(0xF8D050) : UI_HEX(0xD8E0EC);
        UiRGBA shine = level >= 2 ? UI_HEX(0xFFF4C0) : UI_HEX(0xFFFFFF);
        const struct UiMask *m = UiM_Ellipse(d, d);

        UiG_Mask(m, x, y, rim);
        UiG_Mask(UiM_Erode4(UiM_Erode4(m)), x, y, face);
        UiG_Mask(UiM_Ellipse(8, 6), x + 7, y + 6, shine);
        if (sP.area == area)
            UiG_Mask(UiM_Sub(UiM_Ellipse(d + 8, d + 8), UiM_Ellipse(d + 4, d + 4)), x - 4, y - 4, UI_HEX(0xD89818));
        UiG_Hit(HIT_AREA, area, x, y, d, d);
    }
}

/* a stylized map of the island and a card, on the two big buttons */
static void MapGlyph(int cx, int cy)
{
    static const int sIsland[] = {6, 10, 22, 2, 44, 4, 58, 14, 56, 30, 40, 38, 18, 36, 2, 26};

    UiG_Paint(UiM_Poly(60, 40, sIsland, 8), cx - 30, cy - 20, UI_HEX(0x88C060),
              &(struct UiPaint){UI_HEX(0x406828), UI_HEX(0xB8E090), UI_NONE, 0, 1, NULL, UI_NONE});
    UiG_Mask(UiM_Rounded4(8, 8, 2, 2, 2, 2), cx - 14, cy - 8, UI_HEX(0xE84838));
    UiG_Mask(UiM_Rounded4(8, 8, 2, 2, 2, 2), cx + 6, cy - 12, UI_HEX(0xE84838));
    UiG_Mask(UiM_Rounded4(8, 8, 2, 2, 2, 2), cx + 10, cy + 4, UI_HEX(0xE84838));
}

static void CardGlyph(int cx, int cy)
{
    UiG_Paint(UiM_Rounded4(56, 36, 4, 4, 4, 4), cx - 28, cy - 18, UI_HEX(0xF8F8F0),
              &(struct UiPaint){UI_HEX(0x405070), UI_NONE, UI_NONE, 0, 1, NULL, UI_NONE});
    UiG_Mask(UiM_Ellipse(12, 12), cx - 20, cy - 12, UI_HEX(0x5878B0));
    UiG_Mask(UiM_Rounded4(16, 10, 5, 5, 0, 0), cx - 22, cy + 2, UI_HEX(0x5878B0));
    UiG_Mask(UiM_Rounded4(22, 4, 2, 2, 2, 2), cx - 2, cy - 8, UI_HEX(0xA0A8B8));
    UiG_Mask(UiM_Rounded4(18, 4, 2, 2, 2, 2), cx - 2, cy, UI_HEX(0xA0A8B8));
}

/* the game's cursor: a pointer whose tip is the point that picks the area */
static void Pointer(int x, int y)
{
    static const int sArrow[] = {0, 0, 0, 17, 4, 13, 8, 21, 12, 19, 8, 11, 14, 11};

    UiG_Paint(UiM_Poly(15, 22, sArrow, 7), x, y, UI_HEX(0xFFFFFF),
              &(struct UiPaint){UI_HEX(0x101420), UI_NONE, UI_NONE, 0, 1, NULL, UI_NONE});
}

static void DrawPass(int t)
{
    struct UiRamp band = UiRampDefault(sStarColors[sP.stars < ARRAY_COUNT(sStarColors) ? sP.stars : 0]);
    int x, y, w, h;
    u8 points[8];

    UiG_Paint(UiM_Rounded4(PASS_W, PASS_H, 10, 10, 10, 10), sPassX, sPassY, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x101420), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    /* the title band, left of CANCEL; the stars after the title */
    UiG_Paint(UiM_Rounded4(286, 26, 5, 5, 5, 5), sPassX + 8, sPassY + 6, band.base,
              &(struct UiPaint){band.ink, band.hi, band.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, sPassX + 20, sPassY + 10, sText_Title, UI_P_TEXT_W, band.ink, UI_ALIGN_LEFT, 0);
    for (int s = 0; s < sP.stars && s < 4; s++)
    {
        static const int sStar[] = {7, 0, 9, 5, 14, 5, 10, 8, 12, 14, 7, 10, 2, 14, 4, 8, 0, 5, 5, 5};

        UiG_Mask(UiM_Poly(15, 15, sStar, 10), sPassX + 200 + s * 20, sPassY + 11, UI_HEX(0xF8D850));
    }
    /* CANCEL */
    AreaRect(AREA_CANCEL, &x, &y, &w, &h);
    Plate(x + 6, y + 6, w - 14, h - 8, sP.area == AREA_CANCEL, false);
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 12, sText_Cancel, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_AREA, AREA_CANCEL, x, y, w, h);
    /* the symbols earned: the title over the seven */
    AreaRect(AREA_SYMBOLS, &x, &y, &w, &h);
    Plate(x, y, w, h - 4, sP.area == AREA_SYMBOLS, false);
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 12, gText_SymbolsEarned, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_AREA, AREA_SYMBOLS, x, y, w, h);
    for (int f = 0; f < NATIVE_PASS_FACILITIES; f++)
        Symbol(AREA_SYMBOL_FIRST + f, sP.symbols[f], t);
    /* the battle record */
    AreaRect(AREA_RECORD, &x, &y, &w, &h);
    Plate(x, y + 4, w, h - 4, sP.area == AREA_RECORD, !sP.hasRecord);
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 16, gText_BattleRecord, sP.hasRecord ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_NONE,
                UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_AREA, AREA_RECORD, x, y, w, h);
    /* the battle points */
    AreaRect(AREA_POINTS, &x, &y, &w, &h);
    Plate(x, y, w, h, sP.area == AREA_POINTS, false);
    UiText_Draw(UI_FONT_NORMAL, x + 12, y + 8, gText_BattlePoints, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    ConvertIntToDecimalStringN(points, sP.battlePoints, STR_CONV_MODE_LEFT_ALIGN, 5);
    UiText_DrawScaled(UI_FONT_NORMAL, x + w - 14 - UiText_Width(UI_FONT_NORMAL, points, 0) * 2, y + 22, points, UI_P_TEXT,
                      UI_NONE, 2);
    UiG_Hit(HIT_AREA, AREA_POINTS, x, y, w, h);
    /* the FRONTIER MAP and the TRAINER CARD */
    AreaRect(AREA_MAP, &x, &y, &w, &h);
    Plate(x, y, w, h, sP.area == AREA_MAP, false);
    MapGlyph(x + w / 2, y + h / 2 - 8);
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + h - 26, sText_Map, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_AREA, AREA_MAP, x, y, w, h);
    AreaRect(AREA_CARD, &x, &y, &w, &h);
    Plate(x, y, w, h, sP.area == AREA_CARD, false);
    CardGlyph(x + w / 2, y + h / 2 - 8);
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + h - 26, sText_Card, UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_AREA, AREA_CARD, x, y, w, h);
    /* the game's line for the area under the cursor */
    UiG_Paint(UiM_Rounded4(PASS_W - 32, 34, 4, 4, 4, 4), sPassX + 16, sPassY + PASS_H - 44, UI_P_PAPER,
              &(struct UiPaint){UI_P_CREAM_LO, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    if (sP.line != NULL)
        Lines(sPassX + 28, sPassY + PASS_H - 36, sP.line, 18, 1, UI_P_TEXT, UI_NONE);
    Pointer(sPassX + sP.cursorX * SCALE, sPassY + sP.cursorY * SCALE);
}

static void DrawMap(int t)
{
    int w = 560, h = 312, x = sPassX + PASS_W / 2 - w / 2, y = sPassY + 4;
    int planX = x + 16, planY = y + 40;
    struct UiRamp band = UiRampDefault(UI_HEX(0x4878C8));

    UiG_Paint(UiM_Rounded4(w, h, 10, 10, 10, 10), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_HEX(0x101420), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 16, 26, 5, 5, 5, 5), x + 8, y + 6, band.base,
              &(struct UiPaint){band.ink, band.hi, band.lo, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + w / 2, y + 10, sText_Map, UI_P_TEXT_W, band.ink, UI_ALIGN_CENTER, 0);
    /* the island: the sea, the land the facilities stand on (the game's map, GBA x 8-150, y 24-104, twice its size) */
    UiG_Paint(UiM_Rounded4(300, 176, 8, 8, 8, 8), planX, planY, UI_HEX(0x58A8E0),
              &(struct UiPaint){UI_HEX(0x2860A0), UI_HEX(0x88C8F0), UI_NONE, 0, 1, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(268, 140, 30, 30, 30, 30), planX + 16, planY + 18, UI_HEX(0x98C870),
              &(struct UiPaint){UI_HEX(0x507838), UI_HEX(0xC0E098), UI_NONE, 0, 1, NULL, UI_NONE});
    for (int f = 0; f < NATIVE_PASS_FACILITIES; f++)
    {
        int mx = planX + (sP.marks[f][0] - 8) * SCALE, my = planY + (sP.marks[f][1] - 24) * SCALE;
        bool on = f == sP.mapCursor;
        int d = on ? 18 + ((t / 8) % 2) * 2 : 12;

        UiG_Paint(UiM_Rounded4(d, d, 3, 3, 3, 3), mx - d / 2, my - d / 2, on ? UI_HEX(0xE84838) : UI_HEX(0xF8F0E0),
                  &(struct UiPaint){UI_HEX(0x502018), UI_NONE, UI_NONE, 0, 1, NULL, UI_NONE});
        UiG_Hit(HIT_FACILITY, f, mx - 12, my - 12, 24, 24);
    }
    if (sP.head)
    {
        int hx = planX + (sP.headX - 8) * SCALE, hy = planY + (sP.headY - 24) * SCALE;

        UiG_Paint(UiM_Ellipse(16, 16), hx - 8, hy - 8, sP.gender == MALE ? UI_HEX(0x4870E0) : UI_HEX(0xE05870),
                  &(struct UiPaint){UI_HEX(0xFFFFFF), UI_NONE, UI_NONE, 0, 2, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NORMAL, hx, hy + 9, sText_You, UI_P_TEXT_W, UI_HEX(0x101420), UI_ALIGN_CENTER, 0);
    }
    /* the list, the one it points at lit */
    for (int f = 0; f < NATIVE_PASS_FACILITIES; f++)
    {
        int ry = planY + f * 25;

        Plate(x + 332, ry, w - 348, 23, f == sP.mapCursor, false);
        if (sP.names[f] != NULL)
            UiText_Draw(UI_FONT_NORMAL, x + 346, ry + 4, sP.names[f], UI_P_TEXT, UI_NONE, UI_ALIGN_LEFT, 0);
        UiG_Hit(HIT_FACILITY, f, x + 332, ry, w - 348, 23);
    }
    /* its lines */
    UiG_Paint(UiM_Rounded4(w - 32, 70, 4, 4, 4, 4), x + 16, y + h - 82, UI_P_PAPER,
              &(struct UiPaint){UI_P_CREAM_LO, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    Lines(x + 30, y + h - 74, sP.description, 18, 3, UI_P_TEXT, UI_NONE);
}

static void Draw(int t)
{
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UI_HEX(0x1C2232));
    sPassX = (UI_LOGICAL_WIDTH - PASS_W) / 2;
    sPassY = 8;
    if (sP.page == NATIVE_PASS_MAP)
        DrawMap(t);
    else
        DrawPass(t);
    if (sP.ready)
    {
        struct UiHintItem hints[2];

        if (sP.page == NATIVE_PASS_MAP)
        {
            hints[0] = (struct UiHintItem){UI_ACT_SCROLL, sText_Select};
            hints[1] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        }
        else
        {
            hints[0] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Choose};
            hints[1] = (struct UiHintItem){UI_ACT_CANCEL, sText_PutAway};
        }
        UiW_Hints(bx + bw - 8 - UiW_HintsWidth(hints, 2, 10), by + bh - 22, hints, 2, true, 10);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* A click walks the game's cursor onto an area: the pad held toward the area's middle (two pixels a frame, as the game
 * moves it; both ways at once, as the game reads them) until the game's cursor is over it, then A where A does something.
 * The player's own keys take over; a walk gives up after four seconds. */
static int sGoal;              /* the area (0: none) */
static bool sGoalThenA;
static int sGoalFrames;

static void WalkTo(int area)
{
    sGoal = area;
    sGoalThenA = area == AREA_MAP || area == AREA_CARD || area == AREA_CANCEL || (area == AREA_RECORD && sP.hasRecord);
    sGoalFrames = 0;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    const s16 *r;
    int tx, ty;

    (void)newKeys;
    if (sGoal == 0)
        return keys;
    if ((keys & (DPAD_ANY | A_BUTTON | B_BUTTON)) != 0 || sP.page != NATIVE_PASS_CARD || ++sGoalFrames > 240)
    {
        sGoal = 0;
        return keys;
    }
    if (!sP.ready)
        return keys;
    if (sP.area == sGoal)
    {
        if (sGoalThenA)
            NativeUi_QueuePress(A_BUTTON);
        sGoal = 0;
        return keys;
    }
    r = sP.rects[sGoal - 1];
    tx = (r[0] + r[2]) / 2;
    ty = (r[1] + r[3]) / 2;
    if (sP.cursorX < tx - 1)
        keys |= DPAD_RIGHT;
    else if (sP.cursorX > tx + 1)
        keys |= DPAD_LEFT;
    if (sP.cursorY < ty - 1)
        keys |= DPAD_DOWN;
    else if (sP.cursorY > ty + 1)
        keys |= DPAD_UP;
    return keys;
}

static void MapTo(int row)
{
    int down = (row - sP.mapCursor + NATIVE_PASS_FACILITIES) % NATIVE_PASS_FACILITIES;

    if (down == 0)
        return;
    if (down <= NATIVE_PASS_FACILITIES / 2)
        NativeUi_QueuePresses(DPAD_DOWN, down);
    else
        NativeUi_QueuePresses(DPAD_UP, NATIVE_PASS_FACILITIES - down);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (!sP.ready || NativeUi_PressesPending() || sGoal != 0)
        return true;
    if (button == 3 || (button == 1 && hit->id == UI_HIT_HINT && hit->index == UI_ACT_CANCEL))
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sP.page == NATIVE_PASS_MAP && hit->id == HIT_FACILITY)
        MapTo(hit->index);
    else if (sP.page == NATIVE_PASS_CARD && hit->id == HIT_AREA && hit->index >= 1 && hit->index <= NATIVE_PASS_AREAS)
        WalkTo(hit->index);
    else if (hit->id == UI_HIT_HINT && hit->index == UI_ACT_CONFIRM)
        NativeUi_QueuePress(A_BUTTON);
    else
        return false;
    return true;
}

static bool Wheel(int dy)
{
    if (!sP.ready || NativeUi_PressesPending() || sP.page != NATIVE_PASS_MAP)
        return true;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

const struct UiScreen gUiScreen_FrontierPass = {
    .name = "frontier_pass",
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
