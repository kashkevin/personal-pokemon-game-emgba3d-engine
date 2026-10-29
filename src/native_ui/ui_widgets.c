#ifdef ENABLE_NATIVE_UI
/*
 * The design's shared widgets (see native_ui/ui_widgets.h): a line-by-line
 * port of artifacts/ui-art-direction/preview/js/widgets.js. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "native_ui/ui_gfx.h"
#include "native_ui/ui_widgets.h"
#include "platform/keybinds.h"

static enum UiDevice sPromptDevice = UI_DEVICE_KEYBOARD;
static bool sPromptDeviceForced;

enum UiDevice UiW_PromptDevice(void)
{
    return sPromptDevice;
}

void UiW_SetPromptDevice(enum UiDevice device)
{
    if (!sPromptDeviceForced)
        sPromptDevice = device;
}

void UiW_ForcePromptDevice(enum UiDevice device)
{
    sPromptDevice = device;
    sPromptDeviceForced = true;
}

int UiW_Nudge(int t)
{
    return (t / 16) % 2;
}

static int JsRound(double x)
{
    return (int)floor(x + 0.5);
}

static double Clamp01(double v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* ---------------------------------------------------------------- text */

int UiW_Gender(int x, int y, int gender)
{
    static const uint8_t male[] = {0xB5, 0xFF};
    static const uint8_t female[] = {0xB6, 0xFF};

    if (gender == 0)
        return UiText_Draw(UI_FONT_NORMAL, x, y, male, UI_P_MALE, UI_P_MALE_SH, UI_ALIGN_LEFT, 0);
    if (gender == 1)
        return UiText_Draw(UI_FONT_NORMAL, x, y, female, UI_P_FEMALE, UI_P_FEMALE_SH, UI_ALIGN_LEFT, 0);
    return x;
}

/* ---------------------------------------------------------------- bars */

UiRGBA UiW_HpColour(double frac, int shade)
{
    static const UiRGBA green[3] = {UI_P_HP_GREEN_0, UI_P_HP_GREEN_1, UI_P_HP_GREEN_2};
    static const UiRGBA yellow[3] = {UI_P_HP_YELLOW_0, UI_P_HP_YELLOW_1, UI_P_HP_YELLOW_2};
    static const UiRGBA red[3] = {UI_P_HP_RED_0, UI_P_HP_RED_1, UI_P_HP_RED_2};

    if (shade < 0 || shade > 2)
        shade = 1;
    return frac > 0.5 ? green[shade] : frac > 0.2 ? yellow[shade] : red[shade];
}

int UiW_HpBar(int x, int y, int w, double frac, bool label, UiRGBA ink)
{
    struct UiPaint o = {0};
    int fill;

    if (ink == UI_NONE)
        ink = UI_P_INK;
    o.ink = ink;
    if (label)
    {
        UiG_Paint(UiM_Rounded4(14, 7, 2, 0, 2, 0), x, y, UI_P_HP_TRACK_LO, &o);
        UiMicro_Draw(x + 3, y + 1, "HP", UI_P_HP_LABEL);
        x += 13;
    }
    UiG_Paint(UiM_Rounded4(w, 7, 0, 1, 0, 1), x, y, UI_P_HP_TRACK, &o);
    UiG_HLine(x + 1, y + 1, w - 2, UI_P_HP_TRACK_LO);
    fill = UiPyRound((w - 2) * Clamp01(frac));
    if (frac > 0 && fill == 0)
        fill = 1;
    if (fill)
    {
        UiG_Rect(x + 1, y + 1, fill, 5, UiW_HpColour(frac, 1));
        UiG_HLine(x + 1, y + 1, fill, UiW_HpColour(frac, 0));
        UiG_HLine(x + 1, y + 2, fill, UiW_HpColour(frac, 0));
        UiG_HLine(x + 1, y + 5, fill, UiW_HpColour(frac, 2));
    }
    return x + w;
}

int UiW_ExpBar(int x, int y, int w, double frac, bool label)
{
    struct UiPaint ink = {UI_P_INK};
    int fill;

    if (label)
    {
        UiG_Paint(UiM_Rounded4(17, 5, 1, 0, 1, 0), x, y - 1, UI_P_HP_TRACK_LO, NULL);
        UiMicro_Draw(x + 2, y - 1, "EXP", UI_P_EXP_0);
        x += 16;
    }
    UiG_Paint(UiM_Rounded4(w, 4, 0, 0, 0, 0), x, y, UI_P_EXP_TRACK, &ink);
    fill = UiPyRound((w - 2) * Clamp01(frac));
    if (fill)
    {
        UiG_HLine(x + 1, y + 1, fill, UI_P_EXP_0);
        UiG_HLine(x + 1, y + 2, fill, UI_P_EXP_1);
    }
    return x + w;
}

/* ---------------------------------------------------------------- badges */

int UiW_TypeBadge(int x, int y, int type, int w)
{
    const struct UiRamp *r;
    struct UiPaint o;

    if (w <= 0)
        w = 40;
    if (type < 0 || type >= UI_TYPE_COUNT)
        type = UI_TYPE_NORMAL;
    r = &gUiTypeRamps[type];
    o = (struct UiPaint){r->ink, r->hi, r->lo, 0, 0, NULL, 0};
    UiG_Paint(UiM_Rounded4(w, 13, 1, 1, 1, 1), x, y, r->base, &o);
    UiG_Put(x + 2, y + 6, r->hi);
    UiG_Put(x + w - 3, y + 6, r->lo);
    UiCaps_Draw(x + (w >> 1), y + 3, gUiTypeLabels[type], UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
    return x + w;
}

int UiW_StatusBadge(int x, int y, const char *status, UiRGBA colour, int w)
{
    struct UiRamp r = UiRampOf(colour, 0.4, 0.25, 0.6);
    struct UiPaint o = {r.ink, r.hi, r.lo, 0, 0, NULL, 0};

    if (w <= 0)
        w = UiCaps_Width(status) + 10;
    UiG_Paint(UiM_Capsule(w, 11, true, true), x, y, r.base, &o);
    UiCaps_Draw(x + (w >> 1), y + 2, status, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    return x + w;
}

int UiW_CategoryIcon(int x, int y, int category)
{
    static const char *const burst[] = {"....#....", ".#..#..#.", "..#####..", ".#######.", "#########",
                                        ".#######.", "..#####..", ".#..#..#.", "....#...."};
    UiRGBA base = category == 0 ? UI_HEX(0xE86830) : category == 1 ? UI_HEX(0x6870D8) : UI_HEX(0xA0A0A0);
    struct UiRamp r = UiRampOf(base, 0.4, 0.25, 0.62);
    struct UiPaint o = {r.ink, r.hi, r.lo, 0, 0, NULL, 0};
    int cx = x + 12;
    int cy = y + 6;

    UiG_Paint(UiM_Rounded4(24, 13, 1, 1, 1, 1), x, y, r.base, &o);
    if (category == 0)
    {
        UiG_Mask(UiM_Rows(burst, 9, NULL), cx - 4, cy - 4, UI_HEX(0xFFF0A0));
        UiG_Put(cx, cy, UI_HEX(0xF8F8F8));
    }
    else if (category == 1)
    {
        UiG_Mask(UiM_Outline(UiM_Ellipse(9, 9)), cx - 4, cy - 4, UI_HEX(0xE0E8FF));
        UiG_Mask(UiM_Outline(UiM_Ellipse(5, 5)), cx - 2, cy - 2, UI_HEX(0xF8F8FF));
    }
    else
    {
        UiG_Mask(UiM_Outline(UiM_Ellipse(7, 7)), cx - 3, cy - 3, UI_HEX(0xF8F8F8));
        UiG_Put(cx, cy, UI_HEX(0xF8F8F8));
        UiG_Put(cx - 1, cy, UI_HEX(0xF8F8F8));
    }
    return x + 24;
}

int UiW_Planned(int x, int y, const char *label)
{
    struct UiRamp r;
    struct UiPattern pat = UiPat_Diag(4, 1, 0, 1);
    struct UiPaint o;
    int w;

    if (label == NULL)
        label = "PLANNED";
    w = UiCaps_Width(label) + 8;
    r = UiRampOf(UI_HEX(0x8C84A8), 0.4, 0.25, 0.6);
    o = (struct UiPaint){r.ink, r.hi, r.lo, 0, 0, &pat, UiMix(r.base, r.lo, 0.5)};
    UiG_Paint(UiM_Rounded4(w, 11, 1, 1, 1, 1), x, y, r.base, &o);
    UiCaps_Draw(x + 4, y + 2, label, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_LEFT, UI_NONE);
    return x + w;
}

/* ---------------------------------------------------------------- cursors */

struct CursorBuild
{
    UiRGBA colour;
    UiRGBA hi;
    bool up;
};

static void BuildCursor(struct UiBitmap *bm, const void *ctx)
{
    static const char *const arrow[] = {"#.....", "##....", "#O#...", "#OO#..", "#OOO#.", "#OOOO#",
                                        "#OOO#.", "#OO#..", "#O#...", "##....", "#....."};
    const struct CursorBuild *b = ctx;

    for (int j = 0; j < 11; j++)
        for (int i = 0; i < 6; i++)
        {
            char c = arrow[j][i];

            if (c == '#')
                UiBitmap_Put(bm, i, j, UI_P_INK);
            else if (c == 'O')
                UiBitmap_Put(bm, i, j, (j < 5 && i == 1) ? b->hi : b->colour);
        }
}

void UiW_Cursor(int x, int y, int phase, UiRGBA colour)
{
    struct CursorBuild b;
    char key[32];

    if (colour == UI_NONE)
        colour = UI_P_CURSOR;
    b.colour = colour;
    b.hi = UiMix(colour, UI_HEX(0xFFFFFF), 0.45);
    snprintf(key, sizeof(key), "cur%08x", colour);
    UiG_Blit(UiG_Cached(key, 6, 11, BuildCursor, &b), x + phase, y);
}

static void BuildDown(struct UiBitmap *bm, const void *ctx)
{
    static const char *const down[] = {"#######", "#OOOOO#", ".#OOO#.", "..#O#..", "...#..."};
    const struct CursorBuild *b = ctx;

    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 7; i++)
        {
            char c = down[b->up ? 4 - j : j][i];

            if (c == '#')
                UiBitmap_Put(bm, i, j, UI_P_INK);
            else if (c == 'O')
                UiBitmap_Put(bm, i, j, b->colour);
        }
}

void UiW_DownArrow(int x, int y, UiRGBA colour, bool up)
{
    struct CursorBuild b;
    char key[32];

    if (colour == UI_NONE)
        colour = UI_P_CURSOR;
    b.colour = colour;
    b.up = up;
    snprintf(key, sizeof(key), "dn%c%08x", up ? 'u' : 'd', colour);
    UiG_Blit(UiG_Cached(key, 7, 5, BuildDown, &b), x, y);
}

struct FrameBuild
{
    const struct UiMask *m;
    UiRGBA colour;
    UiRGBA hi;
    int width;
};

static void BuildSelectFrame(struct UiBitmap *bm, const void *ctx)
{
    const struct FrameBuild *b = ctx;
    const struct UiMask *pm = UiM_Pad(b->m, 3, 3, 3, 3);
    const struct UiMask *ring1 = UiM_Sub(UiM_Dilate4(pm), pm);
    const struct UiMask *inner = UiM_Or(pm, ring1);
    const struct UiMask *ring2 = UiM_Sub(UiM_Dilate8(inner), inner);
    const struct UiMask *top;

    UiBitmap_FillMask(bm, ring1, 0, 0, b->colour);
    if (b->width >= 2)
    {
        const struct UiMask *all = UiM_Or(inner, ring2);

        UiBitmap_FillMask(bm, ring2, 0, 0, b->colour);
        UiBitmap_FillMask(bm, UiM_Sub(UiM_Dilate4(all), all), 0, 0, UI_P_SEL_LO);
    }
    top = UiM_Sub(ring1, UiM_Shift(ring1, 0, 1));
    UiBitmap_FillMask(bm, UiM_And(top, UiM_Shift(pm, 0, -1)), 0, 0, b->hi);
}

void UiW_SelectFrame(const struct UiMask *m, int x, int y, UiRGBA colour, UiRGBA hi, int width)
{
    struct FrameBuild b;
    char key[2048];

    b.m = m;
    b.colour = colour != UI_NONE ? colour : UI_P_SEL;
    b.hi = hi != UI_NONE ? hi : UI_P_SEL_HI;
    b.width = width > 0 ? width : 2;
    snprintf(key, sizeof(key), "sf%s%08x%08x%d", m->key, b.colour, b.hi, b.width);
    UiG_Blit(UiG_Cached(key, m->w + 6, m->h + 6, BuildSelectFrame, &b), x - 3, y - 3);
}

static void BuildCarryFrame(struct UiBitmap *bm, const void *ctx)
{
    const struct FrameBuild *b = ctx;
    const struct UiMask *pm = UiM_Pad(b->m, 4, 4, 4, 4);
    const struct UiMask *r1 = UiM_Sub(UiM_Dilate4(pm), pm);
    const struct UiMask *a1 = UiM_Or(pm, r1);
    const struct UiMask *r2 = UiM_Sub(UiM_Dilate4(a1), a1);
    const struct UiMask *a2 = UiM_Or(a1, r2);
    const struct UiMask *r3 = UiM_Sub(UiM_Dilate4(a2), a2);

    UiBitmap_FillMask(bm, r1, 0, 0, UI_P_PAPER);
    UiBitmap_FillMask(bm, r2, 0, 0, UI_P_INK);
    UiBitmap_FillMask(bm, r3, 0, 0, UI_P_PAPER);
}

void UiW_CarryFrame(const struct UiMask *m, int x, int y)
{
    struct FrameBuild b = {m, 0, 0, 0};
    char key[2048];

    snprintf(key, sizeof(key), "cf%s", m->key);
    UiG_Blit(UiG_Cached(key, m->w + 8, m->h + 8, BuildCarryFrame, &b), x - 4, y - 4);
}

/* ---------------------------------------------------------------- prompts */

int UiW_Keycap(int x, int y, const char *label)
{
    int w = UiCaps_Width(label) + 6;
    struct UiPaint o = {UI_R_KEY_INK, UI_R_KEY_HI, UI_R_KEY_LO, 0, 2, NULL, 0};

    if (w < 11)
        w = 11;
    UiG_Paint(UiM_Rounded4(w, 11, 1, 1, 1, 1), x, y, UI_R_KEY_BASE, &o);
    UiCaps_Draw(x + (w >> 1), y + 2, label, UI_R_KEY_INK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    return x + w;
}

static int KeycapWidth(const char *label)
{
    int w = UiCaps_Width(label) + 6;

    return w < 11 ? 11 : w;
}

static UiRGBA PadColour(const char *label)
{
    if (strcmp(label, "A") == 0) return UI_P_PAD_A;
    if (strcmp(label, "B") == 0) return UI_P_PAD_B;
    if (strcmp(label, "X") == 0) return UI_P_PAD_X;
    if (strcmp(label, "Y") == 0) return UI_P_PAD_Y;
    return UI_NONE;
}

int UiW_PadButton(int x, int y, const char *label)
{
    UiRGBA pad = PadColour(label);

    if (pad != UI_NONE)
    {
        struct UiRamp r = UiRampOf(pad, 0.4, 0.25, 0.6);
        struct UiPaint o = {r.ink, r.hi, r.lo, 0, 0, NULL, 0};

        UiG_Paint(UiM_Ellipse(11, 11), x, y, r.base, &o);
        UiCaps_Draw(x + 6, y + 2, label, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        return x + 11;
    }
    else
    {
        struct UiRamp r = UiRampOf(UI_HEX(0x586070), 0.4, 0.25, 0.6);
        struct UiPaint o = {r.ink, r.hi, r.lo, 0, 0, NULL, 0};
        int w = UiCaps_Width(label) + 10;

        UiG_Paint(UiM_Capsule(w, 11, true, true), x, y, r.base, &o);
        UiCaps_Draw(x + (w >> 1), y + 2, label, UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        return x + w;
    }
}

static int PadButtonWidth(const char *label)
{
    return PadColour(label) != UI_NONE ? 11 : UiCaps_Width(label) + 10;
}

/* The game's keys (src/platform/sdl2.c) and its XInput mapping (A = A, the
 * pad's X = B, Back = Select, LB / RB = L / R); Tab / the pad's Y (not game
 * buttons) open a screen's details view. */
/* The keyboard key bound to a game button (the Controls page of the F10 menu), else the default. */
static const char *KeyGlyph(int bind, const char *fallback, int slot)
{
    static char sLabels[2][24];

    Platform_NativeUiKeyLabel(bind, sLabels[slot], sizeof(sLabels[slot]));
    return sLabels[slot][0] != '\0' ? sLabels[slot] : fallback;
}

static int Glyphs(enum UiAction action, const char *out[2])
{
    bool pad = sPromptDevice == UI_DEVICE_PAD;

    switch (action)
    {
    case UI_ACT_CONFIRM: out[0] = pad ? "A" : KeyGlyph(KEYBIND_A, "Z", 0); return 1;
    case UI_ACT_CANCEL: out[0] = pad ? "X" : KeyGlyph(KEYBIND_B, "X", 0); return 1;
    case UI_ACT_START: out[0] = pad ? "START" : KeyGlyph(KEYBIND_START, "ENTER", 0); return 1;
    case UI_ACT_SELECT: out[0] = pad ? "BACK" : KeyGlyph(KEYBIND_SELECT, "\\", 0); return 1;
    case UI_ACT_L: out[0] = pad ? "LB" : KeyGlyph(KEYBIND_L, "A", 0); return 1;
    case UI_ACT_R: out[0] = pad ? "RB" : KeyGlyph(KEYBIND_R, "S", 0); return 1;
    case UI_ACT_TABS:
        out[0] = pad ? "LB" : KeyGlyph(KEYBIND_L, "A", 0);
        out[1] = pad ? "RB" : KeyGlyph(KEYBIND_R, "S", 1);
        return 2;
    case UI_ACT_MOVE: out[0] = UI_CAPS_LEFT; out[1] = UI_CAPS_RIGHT; return 2;
    case UI_ACT_SCROLL: out[0] = UI_CAPS_UP; out[1] = UI_CAPS_DOWN; return 2;
    case UI_ACT_PC_MENU: out[0] = "F10"; return 1;
    case UI_ACT_ESC: out[0] = "ESC"; return 1;
    case UI_ACT_DETAILS: out[0] = pad ? "Y" : "TAB"; return 1;
    default: out[0] = "?"; return 1;
    }
}

int UiW_HintWidth(enum UiAction action, const uint8_t *label)
{
    const char *glyphs[2];
    int n = Glyphs(action, glyphs);
    int w = 0;

    for (int i = 0; i < n; i++)
        w += (sPromptDevice == UI_DEVICE_PAD ? PadButtonWidth(glyphs[i]) : KeycapWidth(glyphs[i])) + 2;
    return w + 1 + UiText_Width(UI_FONT_NORMAL, label, 0);
}

int UiW_Hint(int x, int y, enum UiAction action, const uint8_t *label, bool dark)
{
    const char *glyphs[2];
    int n = Glyphs(action, glyphs);
    int x2;

    for (int i = 0; i < n; i++)
        x = (sPromptDevice == UI_DEVICE_PAD ? UiW_PadButton(x, y, glyphs[i]) : UiW_Keycap(x, y, glyphs[i])) + 2;
    x += 1;
    x2 = dark ? UiG_TextW(x, y - 3, label) : UiG_Text(x, y - 3, label);
    UiG_Hit(UI_HIT_HINT, action, x - 14, y - 2, x2 - x + 16, 15);
    return x2;
}

int UiW_Hints(int x, int y, const struct UiHintItem *items, int count, bool dark, int gap)
{
    if (gap == 0)
        gap = 10;
    for (int i = 0; i < count; i++)
        x = UiW_Hint(x, y, items[i].action, items[i].label, dark) + gap;
    return x;
}

int UiW_HintsWidth(const struct UiHintItem *items, int count, int gap)
{
    int w = 0;

    if (gap == 0)
        gap = 10;
    for (int i = 0; i < count; i++)
        w += UiW_HintWidth(items[i].action, items[i].label) + gap;
    return w > 0 ? w - gap : 0;
}

/* ---------------------------------------------------------------- frames */

struct UiRamp UiW_FieldBand(void)
{
    struct UiRamp r = {UI_HEX(0x98E0A8), UI_HEX(0x48B070), UI_HEX(0x2F8A58), UI_HEX(0x1C4430)};
    return r;
}

void UiW_DialogBox(int x, int y, int w, int h, const struct UiRamp *band, UiRGBA body, int inner[4])
{
    struct UiRamp field = UiW_FieldBand();
    struct UiPaint outer;
    struct UiPaint core;
    int mx;

    if (band == NULL)
        band = &field;
    if (body == UI_NONE)
        body = UI_P_PAPER;
    outer = (struct UiPaint){band->ink, band->hi, band->lo, 0, 0, NULL, 0};
    core = (struct UiPaint){band->ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0};
    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, band->base, &outer);
    UiG_Paint(UiM_Rounded4(w - 8, h - 8, 2, 2, 2, 2), x + 4, y + 4, body, &core);
    mx = x + (w >> 1);
    UiG_Rect(mx - 6, y + 1, 2, 2, band->hi);
    UiG_Rect(mx + 4, y + 1, 2, 2, band->hi);
    if (inner != NULL)
    {
        inner[0] = x + 8;
        inner[1] = y + 6;
        inner[2] = w - 16;
        inner[3] = h - 12;
    }
}

void UiW_LocationSign(int x, int y, const uint8_t *name, const char *clock)
{
    /* the design's wood (pause.js) */
    static const struct UiRamp wood = {UI_HEX(0xECC08A), UI_HEX(0xD29C62), UI_HEX(0xA8703C), UI_HEX(0x4A2A12)};
    int w = UI_LOCATION_SIGN_W, h = UI_LOCATION_SIGN_H;
    const int nails[4][2] = {{x + 4, y + 4}, {x + w - 6, y + 4}, {x + 4, y + h - 6}, {x + w - 6, y + h - 6}};

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, wood.base,
              &(struct UiPaint){wood.ink, wood.hi, wood.lo, 0, 2, NULL, UI_NONE});
    for (int i = 0; i < 4; i++)
    {
        UiG_Rect(nails[i][0], nails[i][1], 2, 2, wood.ink);
        UiG_Put(nails[i][0], nails[i][1], wood.lo);
    }
    UiText_Draw(UI_FONT_NORMAL, x + 12, y + 8, name, UI_HEX(0x402410), UI_HEX(0xECC08A), UI_ALIGN_LEFT, 0);
    if (clock != NULL)
    {
        UiGfx_Icon("sun", x + w - 64, y + 10, 0);
        UiCaps_Draw(x + w - 10, y + 13, clock, UI_HEX(0x402410), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
}

/* PokeBall: red top, white bottom, the ink band and the button, lit from the top left; turned as the GBA's affine
 * sprite turns it (a screen offset maps to the ball by the rotation's inverse) */
struct PokeBallSpec
{
    int d;
    int turn;
};

static void BuildPokeBall(struct UiBitmap *bm, const void *ctx)
{
    const struct PokeBallSpec *b = ctx;
    const double r = b->d / 2.0, a = b->turn * (6.283185307179586 / 64.0), ca = cos(a), sa = sin(a);
    const double ink = b->d >= 96 ? b->d / 64.0 : 1.0, band = b->d >= 64 ? b->d / 64.0 : 1.0;
    const double button = b->d * 0.19, inner = button * 0.52;
    const UiRGBA redHi = UI_HEX(0xF89880), red = UI_P_CMD_FIGHT, redLo = UI_P_HP_RED_2, white = UI_P_PAPER,
                 whiteLo = UI_P_PAPER_LO, inkC = UI_P_INK;

    for (int py = 0; py < b->d; py++)
        for (int px = 0; px < b->d; px++)
        {
            double x = px + 0.5 - r, y = py + 0.5 - r, dist = sqrt(x * x + y * y);
            double u = ca * x + sa * y, v = -sa * x + ca * y; /* the ball's own axes: v < 0 its red half */
            double nx = x / r, ny = y / r, sx = nx + 0.14, sy = ny + 0.18;
            bool shade = sx * sx + sy * sy > 0.92 * 0.92; /* the rim away from the light: a crescent */
            UiRGBA c;

            if (dist > r)
                continue;
            if (dist > r - ink)
                c = inkC;
            else if (u * u + v * v < button * button)
            {
                double bd = sqrt(u * u + v * v);

                if (bd > button - ink || (bd < inner && bd > inner - (ink > 1.5 ? ink / 2 : 1.0)))
                    c = inkC;
                else
                    c = bd > inner && (u + v) / button > 0.55 ? whiteLo : white;
            }
            else if (fabs(v) < band)
                c = inkC;
            else if (v < 0)
            {
                double hx = nx + 0.42, hy = ny + 0.5;

                c = shade ? redLo : (hx * hx / 0.03 + hy * hy / 0.012 < 1.0 ? redHi : red);
            }
            else
                c = shade ? whiteLo : white;
            UiBitmap_Put(bm, px, py, c);
        }
}

const struct UiPiece *UiW_PokeBallPiece(int d, int turn)
{
    struct PokeBallSpec spec = {d < 4 ? 4 : d, ((turn % 64) + 64) % 64};
    char key[48];

    snprintf(key, sizeof(key), "pokeball:%d:%d", spec.d, spec.turn);
    return UiG_Cached(key, spec.d, spec.d, BuildPokeBall, &spec);
}

void UiW_PokeBall(int cx, int cy, int d, int turn)
{
    UiG_Blit(UiW_PokeBallPiece(d, turn), cx - d / 2, cy - d / 2);
}

int UiW_TitleTab(int x, int y, const char *label, const struct UiRamp *r, int w, const char *icon)
{
    static const int8_t chamfer[] = {6, 5, 4, 3, 2, 1};
    struct UiPaint o = {r->ink, r->hi, r->lo, 0, 0, NULL, 0};
    int h = 17;
    int tx;

    if (w <= 0)
        w = UiCaps_Width(label) + 22 + (icon != NULL ? 18 : 0);
    UiG_Paint(UiM_Rounded(w, h, UiCornerR(1), UiCornerP(6, chamfer), UiCornerR(1), UiCornerP(6, chamfer)),
              x, y, r->base, &o);
    tx = x + 7;
    if (icon != NULL)
    {
        UiGfx_Icon(icon, x + 4, y + ((h - 16) >> 1), 16);
        tx += 16;
    }
    UiCaps_Draw(tx, y + 5, label, UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    return x + w;
}

struct DsBuild
{
    const struct UiMask *m;
    struct UiRamp r;
    int lip;
    int w;
    int h;
};

static void BuildDsButton(struct UiBitmap *bm, const void *ctx)
{
    const struct DsBuild *b = ctx;
    struct UiPaint ink = {b->r.ink};
    const struct UiMask *inner = UiM_Erode4(b->m);

    UiBitmap_Paint(bm, b->m, 0, 0, b->r.base, &ink);
    UiBitmap_FillMask(bm, UiM_Window(inner, 0, 0, b->w, 3), 0, 0, b->r.hi);
    UiBitmap_FillMask(bm, UiM_Window(inner, 0, b->h - 1 - b->lip, b->w, b->h), 0, 0, b->r.lo);
    UiBitmap_Rect(bm, 2, b->h - 2 - b->lip, b->w - 4, 1, UiMix(b->r.base, b->r.lo, 0.5));
}

const struct UiMask *UiW_DsButton(int x, int y, int w, int h, UiRGBA colour, const uint8_t *label,
                                  const struct UiDsButtonOpt *o)
{
    static const struct UiDsButtonOpt sNone = {0};
    struct DsBuild b;
    char key[160];
    int lip;
    int tx;
    int ty;
    UiRGBA fg;

    if (o == NULL)
        o = &sNone;
    lip = o->lipSet ? o->lip : 3;
    b.r = UiRampOf(o->disabled ? UI_HEX(0xA0A0A8) : colour, 0.42, 0.25, 0.62);
    b.m = UiM_Rounded4(w, h, 4, 4, 4, 4);
    b.lip = lip;
    b.w = w;
    b.h = h;
    if (o->pressed)
    {
        UiRGBA t = b.r.hi;

        y += 1;
        b.r.hi = b.r.lo;
        b.r.lo = t;
    }
    if (o->selected)
        UiW_SelectFrame(b.m, x, y, UI_NONE, UI_NONE, 2);
    snprintf(key, sizeof(key), "ds%dx%d%08x%08x%d", w, h, b.r.base, b.r.hi, lip);
    UiG_Blit(UiG_Cached(key, w, h, BuildDsButton, &b), x, y);
    tx = x + 10;
    if (o->icon != NULL)
    {
        int cy = y + ((h - lip) >> 1);
        int iw = 24, ih = 24;

        UiGfx_IconSize(o->icon, &iw, &ih);
        if (!o->noChip)
        {
            int d = h - lip - 4 < 26 ? h - lip - 4 : 26;
            struct UiPaint chip = {b.r.ink, UI_NONE, UiMix(b.r.base, UI_HEX(0xFFFFFF), 0.2), 0, 0, NULL, 0};

            UiG_Paint(UiM_Ellipse(d, d), x + 5, cy - (d >> 1), UiMix(b.r.base, UI_HEX(0xFFFFFF), 0.45), &chip);
            UiGfx_Icon(o->icon, x + 5 + (d >> 1) - (iw >> 1), cy - (ih >> 1), 0);
            tx = x + 5 + d + 6;
        }
        else
        {
            UiGfx_Icon(o->icon, x + 6, cy - (ih >> 1), 0);
            tx = x + 6 + iw + 4;
        }
    }
    ty = y + ((h - lip) >> 1) - 9;
    fg = o->disabled ? UI_HEX(0xE8E8E8) : UI_P_TEXT_W;
    if (label != NULL)
    {
        if (o->center)
            UiText_Draw(o->font, (tx + x + w) >> 1, ty, label, fg, b.r.ink, UI_ALIGN_CENTER, 0);
        else
            UiText_Draw(o->font, tx, ty, label, fg, b.r.ink, UI_ALIGN_LEFT, 0);
    }
    return b.m;
}

void UiW_MenuBox(int x, int y, const struct UiMenuItem *items, int count, int sel,
                 const struct UiMenuOpt *o, int out[4])
{
    static const struct UiMenuOpt sNone = {0};
    int rowH;
    int w;
    int h;
    int hitId;

    if (o == NULL)
        o = &sNone;
    rowH = o->rowH > 0 ? o->rowH : 16;
    w = o->w;
    hitId = o->hitId > 0 ? o->hitId : UI_HIT_MENU;
    for (int i = 0; i < count; i++)
    {
        int lw = UiText_Width(UI_FONT_NORMAL, items[i].label, 0) + 32;

        if (lw > w)
            w = lw;
    }
    if (w < o->minW)
        w = o->minW;
    h = count * rowH + 14;
    if (o->anchor == UI_ANCHOR_RIGHT || o->anchor == UI_ANCHOR_BOTTOMRIGHT)
        x -= w;
    if (o->anchor == UI_ANCHOR_BOTTOM || o->anchor == UI_ANCHOR_BOTTOMRIGHT)
        y -= h;
    UiW_DialogBox(x, y, w, h, o->band, UI_NONE, NULL);
    for (int i = 0; i < count; i++)
    {
        int ry = y + 7 + i * rowH;

        if (items[i].disabled)
            UiText_Draw(UI_FONT_NORMAL, x + 18, ry, items[i].label, UI_HEX(0x909090), UI_NONE, UI_ALIGN_LEFT, 0);
        else
            UiG_Text(x + 18, ry, items[i].label);
        if (i == sel)
            UiW_Cursor(x + 9, ry + 2, UiW_Nudge(o->t), UI_NONE);
        UiG_Hit(hitId, i, x + 4, ry, w - 8, rowH);
    }
    if (out != NULL)
    {
        out[0] = x;
        out[1] = y;
        out[2] = w;
        out[3] = h;
    }
}

void UiW_YesNo(int x, int y, int sel, int t, enum UiMenuAnchor anchor, int out[4])
{
    static const uint8_t yes[] = {0xD3, 0xBF, 0xCD, 0xFF}; /* YES */
    static const uint8_t no[] = {0xC8, 0xC9, 0xFF};        /* NO */
    struct UiMenuItem items[2] = {{yes, false}, {no, false}};
    struct UiMenuOpt o = {0};

    o.anchor = anchor;
    o.t = t;
    o.hitId = UI_HIT_YESNO;
    o.minW = 60;
    UiW_MenuBox(x, y, items, 2, sel, &o, out);
}

void UiW_Scrollbar(int x, int y, int h, int first, int total, int visible, const struct UiRamp *r)
{
    struct UiRamp d = {UI_HEX(0xF0E8D8), UI_HEX(0xC8C0B0), UI_HEX(0xA09888), UI_P_INK};
    struct UiPaint track;
    struct UiPaint thumb;
    int th;
    int ty;

    if (r == NULL)
        r = &d;
    track = (struct UiPaint){r->ink};
    UiG_Paint(UiM_Rounded4(6, h, 2, 2, 2, 2), x, y, UiMix(r->base, r->lo, 0.5), &track);
    if (total <= visible)
        return;
    th = JsRound((double)(h - 2) * visible / total);
    if (th < 8)
        th = 8;
    ty = y + 1 + JsRound((double)(h - 2 - th) * first / (total - visible > 1 ? total - visible : 1));
    thumb = (struct UiPaint){UI_NONE, r->hi, r->lo};
    UiG_Paint(UiM_Rounded4(4, th, 1, 1, 1, 1), x + 1, ty, r->base, &thumb);
}

void UiW_PageDots(int x, int y, int n, int cur, UiRGBA on, UiRGBA off)
{
    for (int i = 0; i < n; i++)
    {
        UiRGBA c = i == cur ? (on != UI_NONE ? on : UI_P_SEL) : (off != UI_NONE ? off : UI_HEX(0xB8B0A0));
        struct UiPaint o = {UI_P_INK, UiMix(c, UI_HEX(0xFFFFFF), 0.4)};

        UiG_Paint(UiM_Ellipse(7, 7), x + i * 10, y, c, &o);
    }
}

/* ---------------------------------------------------------------- pixel icons */

struct PixBuild
{
    const char *const *rows;
    int rowCount;
    const struct UiPixPal *pal;
    int palCount;
};

static void BuildPix(struct UiBitmap *bm, const void *ctx)
{
    const struct PixBuild *b = ctx;

    for (int j = 0; j < b->rowCount; j++)
        for (int i = 0; b->rows[j][i] != '\0'; i++)
            for (int k = 0; k < b->palCount; k++)
                if (b->pal[k].letter == b->rows[j][i])
                {
                    if (b->pal[k].colour != UI_NONE)
                        UiBitmap_Put(bm, i, j, b->pal[k].colour);
                    break;
                }
}

static int PixDraw(const char *name, const char *const *rows, int rowCount, const struct UiPixPal *pal,
                   int palCount, int x, int y, int scale, bool flip)
{
    struct PixBuild b = {rows, rowCount, pal, palCount};
    const struct UiPiece *piece;
    char key[256];
    int w = 0;
    int n;

    for (int j = 0; j < rowCount; j++)
    {
        int len = (int)strlen(rows[j]);

        if (len > w)
            w = len;
    }
    n = snprintf(key, sizeof(key), "px%s", name);
    for (int k = 0; k < palCount && n < (int)sizeof(key) - 12; k++)
        n += snprintf(key + n, sizeof(key) - (size_t)n, "%c%08x", pal[k].letter, pal[k].colour);
    piece = UiG_Cached(key, w, rowCount, BuildPix, &b);
    if (piece != NULL)
        UiG_BlitPart(piece, 0, 0, piece->w, piece->h, x, y, scale, flip, 1.0);
    return x + w * scale;
}

int UiW_Pix(const char *name, const char *const *rows, int rowCount, const struct UiPixPal *pal,
            int palCount, int x, int y, bool flip)
{
    return PixDraw(name, rows, rowCount, pal, palCount, x, y, 1, flip);
}

int UiW_PixScaled(const char *name, const char *const *rows, int rowCount, const struct UiPixPal *pal,
                  int palCount, int x, int y, int scale)
{
    return PixDraw(name, rows, rowCount, pal, palCount, x, y, scale < 1 ? 1 : scale, false);
}

void UiW_Missing(int x, int y, int w, int h)
{
    UiRGBA c = UI_HEXA(0xA8A8B0, 160);

    UiG_Rect(x, y, w, 1, c);
    UiG_Rect(x, y + h - 1, w, 1, c);
    UiG_Rect(x, y, 1, h, c);
    UiG_Rect(x + w - 1, y, 1, h, c);
    UiMicro_Draw(x + (w >> 1) - 1, y + (h >> 1) - 2, "P", c);
}
void UiW_MessagePage(const uint8_t *text, int printed, int x, int y, int width, int maxLines)
{
    UiW_MessagePageEx(text, printed, x, y, width, maxLines, 16);
}

void UiW_MessagePageFlipDraw(const uint8_t *text, int printed, bool flip, int x, int y, int width, int maxLines, int lineH)
{
    const uint8_t *starts[8];
    int lengths[8];
    uint8_t page[256];
    int n;

    UiW_MessagePageFlip(text, printed, flip, page, (int)sizeof(page));
    if (maxLines > 8)
        maxLines = 8;
    n = UiText_Wrap(page, width, UI_FONT_NORMAL, starts, lengths, 8);
    for (int i = n > maxLines ? n - maxLines : 0, row = 0; i < n; i++, row++)
    {
        uint8_t line[128];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiG_Text(x, y + row * lineH, line);
    }
}

void UiW_MessagePageEx(const uint8_t *text, int printed, int x, int y, int width, int maxLines, int lineH)
{
    UiW_MessagePageFlipDraw(text, printed, false, x, y, width, maxLines, lineH);
}
#endif // ENABLE_NATIVE_UI
