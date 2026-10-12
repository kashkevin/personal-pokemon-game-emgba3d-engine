#ifdef ENABLE_NATIVE_UI
/*
 * 9 · Character customization (the approved design's preview/js/screens/
 * customize.js): the dressing room. The mirror shows the player's own field
 * sprite at 3x walking, running and turning, recoloured with the look being
 * edited (the engine's own palette recolour, MultiplayerAppearance_BuildPalette),
 * beside the player's front picture (portraits keep their base art); the hang
 * tags hold the design's categories; the option sheet shows the colour choices
 * this game has (skin tone, hair, outfit, hat and bag: the STYLE editor's four
 * rows, with the engine's names and colours); the sync tag shows the look's
 * eight bytes as the host receives them.
 *
 * The engine's STYLE editor (multiplayer_appearance.c, START > PLAYER > STYLE)
 * keeps running under it: its rows and cursor, what each button does, the live
 * recolour of the field sprite, and saving (the host stores the look). The
 * PRESET tag is the character selection (include/character_customization.h):
 * DEFAULT and the approved catalogue (32 presets, kit 1's builds and its three
 * class looks), each standing in the sheet; the mirror and the portrait show
 * the character chosen, and a character keeps its own colours (the colour tags
 * work for DEFAULT only). The design's other categories (body, styles of hair,
 * headwear, tops, bottoms, shoes, bags and accessories) need layered art the
 * game does not have yet: they are shown as PLANNED and do nothing.
 * Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "constants/characters.h"
#include "constants/trainers.h"
#include "string_util.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_TAG = UI_HIT_SCREEN,
    HIT_SWATCH,     /* index: row * 32 + choice */
    HIT_MIRROR,
    HIT_SAVE,
    HIT_CANCEL,
    HIT_CHARACTER,  /* index: the catalogue's id (0 = DEFAULT) */
};

enum
{
    TAB_PRESET,
    TAB_BODY,
    TAB_HAIR,
    TAB_HAT,
    TAB_TOP,
    TAB_BOTTOM,
    TAB_SHOES,
    TAB_BAG,
    TAB_EXTRA,
    TAB_COUNT,
};

enum
{
    ASK_NONE,
    ASK_SAVE,
    ASK_DISCARD,
};

enum
{
    MODE_AUTO,
    MODE_WALK,
    MODE_RUN,
    MODE_TURN,
    MODE_COUNT,
};

#define SX 232
#define SY 38
#define SW 400
#define SH 224
#define MX 8
#define MY 32
#define MW 216
#define MH 222
#define HELP_W 284           /* the help box; SAVE and CANCEL beside it */
#define BTN_X (SX + HELP_W + 6)
#define BTN_W (SW - HELP_W - 6)
#define SWATCH_COLUMNS 9
#define MAX_CHOICES 16
#define MAX_CHARACTERS 64    /* the catalogue with DEFAULT (character_customization.h: 38) */
#define CARD_COLUMNS 9
#define CARD_W 44
#define CARD_H 38

static const char *const sTabLabels[TAB_COUNT] = {"PRESET", "BODY", "HAIR", "HAT", "TOP", "BOTTOM", "SHOES", "BAG", "EXTRA"};
/* the editor's row behind each tag (-1: planned) and the tag of each row */
static const s8 sTabRow[TAB_COUNT] = {NATIVE_LOOK_CHARACTER, NATIVE_LOOK_SKIN, NATIVE_LOOK_HAIR, NATIVE_LOOK_HAT,
                                      NATIVE_LOOK_OUTFIT, -1, -1, -1, -1};
static const s8 sRowTab[NATIVE_LOOK_ROW_COUNT] = {TAB_BODY, TAB_HAIR, TAB_TOP, TAB_HAT, -1, -1, TAB_PRESET};
/* the tags in the order L and R walk them (the engine's rows) */
static const u8 sEnabledTabs[] = {TAB_PRESET, TAB_BODY, TAB_HAIR, TAB_HAT, TAB_TOP};

static const char *const sTick[] = {"......##", ".....##.", "#...##..", "##.##...", ".###....", "..#....."};
static const char *const sModeNames[MODE_COUNT] = {"AUTO", "WALK", "RUN", "TURN"};
static const char *const sDirNames[4] = {"DOWN", "LEFT", "UP", "RIGHT"};

/* the colour row of each value tag: its label and the planned style rows above it */
static const char *const sColourLabels[NATIVE_LOOK_SAVE] = {"SKIN TONE", "HAIR COLOUR", "OUTFIT COLOUR", "HAT & BAG COLOUR"};
static const char *const sStyleLabels[TAB_COUNT] = {"PRESETS", "BODY", "HAIR STYLE", "HEADWEAR", "TOP AND JACKET",
                                                    "BOTTOMS", "SHOES", "BACKPACK", "ACCESSORY"};

static const u8 sNote_Style[] = _("Styles need layered art for every pose; this build recolours the body's own sprite.");
static const u8 sNote_OwnColours[] = _("A character keeps its own colours. Choose DEFAULT on the PRESET tag to recolour BRENDAN or MAY.");
static const u8 sNote_Bottom[] = _("No palette slot of its own on this body yet: the look keeps a byte for it.");
static const u8 sNote_Shoes[] = _("Shoes take the OUTFIT COLOUR (the TOP tag) on this body.");
static const u8 sNote_Bag[] = _("The bag takes the HAT & BAG COLOUR (the HAT tag) on this body.");
static const u8 sNote_Extra[] = _("Accessories need layered art for every pose.");
static const u8 sNote_Unlocks[] = _("Cosmetic only: clothing never changes battle stats.");
static const u8 sHelp_Recolours[] = _(": recolours palette slots ");
static const u8 sHelp_Live[] = _(" of this body, live in every pose.");
static const u8 sHelp_Base[] = _("Base colours");
static const u8 sHelp_Save[] = _("Save this look: the host keeps it and the other players see it.");
static const u8 sHelp_SaveOffline[] = _("Save this look. Offline it is kept until you quit and goes to the host when you connect.");
static const u8 sHelp_Cancel[] = _("Leave the dressing room without saving.");
static const u8 sHelp_Planned[] = _("Planned: nothing to change here yet. PRESET picks a character; BODY, HAIR, HAT and TOP recolour DEFAULT.");
static const u8 sHelp_OwnColours[] = _("This character keeps its own colours: DEFAULT (on PRESET) can be recoloured.");
static const u8 sHelp_Character[] = _(": LEFT and RIGHT, or a click, pick another.");
static const u8 sHelp_Sep[] = _(" - ");
static const u8 sAsk_Ok[] = _("Is this OK?");
static const u8 sAsk_OkNote[] = _("Other players see it once the host has it.");
static const u8 sAsk_Discard[] = _("Discard your changes?");
static const u8 sAsk_DiscardNote[] = _("You keep the look you started with.");
static const u8 sText_Save[] = _("SAVE");
static const u8 sText_Cancel[] = _("CANCEL");
static const u8 sHint_Category[] = _("Category");
static const u8 sHint_Colour[] = _("Colour");
static const u8 sHint_Character[] = _("Character");
static const u8 sHint_Save[] = _("Save");
static const u8 sHint_Pose[] = _("Pose");
static const u8 sHint_Back[] = _("Back");
static const u8 sFoot_Portraits[] = _("Each character has its own portrait.");
static const u8 sFoot_Planned[] = _("Styles and unlocks: planned.");

/* one colour row of the editor, from the engine's tables */
struct LookRow
{
    u8 count;
    u8 byte;                       /* the look byte it edits */
    u8 slotCount;
    u8 slots[4];
    const u8 *names[MAX_CHOICES];
    u8 colourCount[MAX_CHOICES];
    u16 colours[MAX_CHOICES][4];   /* light to dark, BGR555 */
};

static struct NativeAppearance sLook;
static struct LookRow sRows[NATIVE_LOOK_SAVE];
static struct NativeCharacterCard sCards[MAX_CHARACTERS];
static int sCardCount;
static u8 sName[PLAYER_NAME_LENGTH + 1];
static bool sReady;
static bool sFresh = true;
static int sLastTab = TAB_BODY;
static int sViewTab = -1;          /* a planned tag picked with the mouse (until the cursor moves) */
static int sViewCursor;
static int sAsk;
static int sAskSel;
static int sMode;
static int sSwing;                 /* the active tag's drop */
static int sPrevTab = -1;

static bool Detect(void)
{
    struct NativeAppearance a;

    if (!MultiplayerAppearance_NativeUiState(&a))
    {
        sFresh = true;
        return false;
    }
    return true;
}

static void CaptureRows(void)
{
    for (int r = 0; r < NATIVE_LOOK_SAVE; r++)
    {
        struct LookRow *row = &sRows[r];
        u32 n = MultiplayerAppearance_NativeUiOptionCount(r);

        row->count = (u8)(n > MAX_CHOICES ? MAX_CHOICES : n);
        row->byte = (u8)MultiplayerAppearance_NativeUiLookByte(r);
        row->slotCount = (u8)MultiplayerAppearance_NativeUiSlots(r, sLook.isMay, row->slots);
        for (int i = 0; i < row->count; i++)
        {
            row->names[i] = MultiplayerAppearance_NativeUiOptionName(r, i);
            row->colourCount[i] = (u8)MultiplayerAppearance_NativeUiSwatch(r, i, sLook.isMay, row->colours[i]);
        }
    }
}

/* the tag of an editor row (-1 for SAVE and CANCEL) */
static int RowTab(int row)
{
    return row >= 0 && row < NATIVE_LOOK_ROW_COUNT ? sRowTab[row] : -1;
}

/* the catalogue (DEFAULT and the characters), standing in the PRESET sheet */
static void CaptureCards(void)
{
    sCardCount = 0;
    while (sCardCount < MAX_CHARACTERS && MultiplayerAppearance_NativeUiCharacter(sCardCount, &sCards[sCardCount]))
        sCardCount++;
}

static int ShownTab(void)
{
    if (sViewTab >= 0)
        return sViewTab;
    if (RowTab(sLook.cursor) >= 0)
        return RowTab(sLook.cursor);
    return sLastTab;
}

static void Capture(void)
{
    int tab;

    MultiplayerAppearance_NativeUiState(&sLook);
    sReady = true;
    if (sFresh)
    {
        sFresh = false;
        sAsk = ASK_NONE;
        sViewTab = -1;
        sMode = MODE_AUTO;
        sLastTab = TAB_PRESET;
        sPrevTab = -1;
        CaptureRows();
        StringCopyN(sName, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH);
        sName[PLAYER_NAME_LENGTH] = EOS;
    }
    CaptureCards(); /* DEFAULT's card follows the colours chosen */
    UiGfx_PrepareTrainerPic(sLook.portraitPic);
    if (sViewTab >= 0 && sLook.cursor != sViewCursor)
        sViewTab = -1;
    if (RowTab(sLook.cursor) >= 0)
        sLastTab = RowTab(sLook.cursor);
    tab = ShownTab();
    if (tab != sPrevTab)
    {
        sSwing = 3;
        sPrevTab = tab;
    }
    else if (sSwing > 0)
    {
        sSwing--;
    }
}

static bool Dirty(void)
{
    return memcmp(sLook.look, sLook.start, NATIVE_LOOK_BYTES) != 0;
}

static UiRGBA Rgb555(u16 c)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    return UI_RGB((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

/* a choice's swatch ramp: four tones (skin) or a colour and its shade */
static struct UiRamp SwatchRamp(const struct LookRow *row, int i)
{
    struct UiRamp r;
    const u16 *c = row->colours[i];

    if (row->colourCount[i] >= 4)
    {
        r.hi = Rgb555(c[0]);
        r.base = Rgb555(c[1]);
        r.lo = Rgb555(c[2]);
        r.ink = Rgb555(c[3]);
    }
    else
    {
        r.base = Rgb555(c[0]);
        r.lo = row->colourCount[i] > 1 ? Rgb555(c[1]) : UiMix(r.base, UI_HEX(0x141838), 0.28);
        r.hi = UiMix(r.base, UI_HEX(0xFFF4C8), 0.38);
        r.ink = UiMix(r.lo, UI_HEX(0x141838), 0.5);
    }
    return r;
}

static bool ArgyleDiamond(int x, int y, const void *ctx)
{
    const int *o = ctx;
    int X = x + o[0] + 1000, Y = y + o[1] + 1000;
    int row = Y / 24, cx = ((X + (row % 2) * 16) % 32) - 16, cy = (Y % 24) - 12;

    return abs(cx) * 3 + abs(cy) * 4 < 40;
}

static bool ArgyleLine(int x, int y, const void *ctx)
{
    const int *o = ctx;
    int X = x + o[0] + 1000, Y = y + o[1] + 1000;

    return ((X * 3 + Y * 4) % 96 == 0) || (((X * 3 - Y * 4 + 9600) % 96 + 96) % 96 == 0);
}

static void Background(const struct UiRamp *rose)
{
    int bx, by, bw, bh, o[2];
    char tag[64];

    UiG_Bleed(&bx, &by, &bw, &bh);
    o[0] = bx;
    o[1] = by;
    UiG_Rect(bx, by, bw, bh, UiMix(rose->lo, rose->base, 0.35));
    snprintf(tag, sizeof(tag), "argyle%dx%d:%d,%d", bw, bh, bx, by);
    UiG_Mask(UiM_Fn(bw, bh, tag, ArgyleDiamond, o), bx, by, UiMix(rose->lo, rose->base, 0.6));
    snprintf(tag, sizeof(tag), "argyleln%dx%d:%d,%d", bw, bh, bx, by);
    UiG_Mask(UiM_Fn(bw, bh, tag, ArgyleLine, o), bx, by, UiMix(rose->base, rose->hi, 0.35));
}

static void Mirror(int t, const struct UiRamp *rose, const struct UiRamp *brass)
{
    const UiRGBA glassHi = UI_HEX(0xF4F4FA), glassBase = UI_HEX(0xDCE2F0), glassLo = UI_HEX(0xB8C2DC);
    int gw = MW - 14, gh = MH - 16, gx = MX + 7, gy = MY + 7, fy = gy + gh - 40;
    struct UiPattern diag = UiPat_Diag(48, 3, 0, -1), below = UiPat_Below(-1);
    int dir, step, stepEvery = 8, pw, nw, phx = MX + 12, phy = MY + 92;
    bool run = false, moving = true;
    char plate[40];

    diag.and_ = &below;
    UiG_Paint(UiM_Rounded4(MW, MH, 40, 40, 4, 4), MX, MY, brass->base,
              &(struct UiPaint){brass->ink, brass->hi, brass->lo, 2, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(gw, gh, 34, 34, 2, 2), gx, gy, glassBase,
              &(struct UiPaint){brass->ink, glassHi, glassLo, 0, 0, &diag, glassHi});
    /* the floor tiles */
    UiG_Save();
    UiG_Clip(gx + 1, fy, gw - 2, 39);
    for (int fx = 0; fx < gw; fx += 12)
        for (int fr = 0; fr < 40; fr += 10)
            UiG_Rect(gx + fx, fy + fr, 12, 10, ((fx / 12 + fr / 10) % 2) ? UiMix(rose->hi, UI_HEX(0xFFFFFF), 0.55)
                                                                         : UiMix(rose->hi, rose->base, 0.2));
    UiG_Restore();
    UiG_HLine(gx + 1, fy, gw - 2, UiMix(rose->base, rose->lo, 0.5));

    /* the Trainer at 3x: the character shown, or the edited look on the body's own field frames */
    switch (sMode)
    {
    case MODE_AUTO:
    {
        int ph = t % 448, q = ph % 112;

        dir = ph / 112;
        if (q < 64)
            run = false;
        else if (q < 96)
        {
            run = true;
            stepEvery = 4;
        }
        else
            moving = false;
        break;
    }
    case MODE_WALK:
        dir = (t / 64) % 4;
        break;
    case MODE_RUN:
        dir = (t / 64) % 4;
        run = true;
        stepEvery = 4;
        break;
    default:
        dir = (t / 30) % 4;
        moving = false;
        break;
    }
    step = moving ? (t / stepEvery) % 4 : 0;
    UiG_Save();
    UiG_Clip(gx + 1, gy + 1, gw - 2, gh - 2);
    UiG_Mask(UiM_Ellipse(30, 8), gx + 132 - 15, fy + 30 - 5, UiWithAlpha(UI_HEX(0x502038), 60));
    UiGfx_NativeFieldSprite(&sLook.sprite, run ? 1 : 0, dir, step, gx + 132 - 24, fy + 30 - 96, 3);
    UiG_Restore();

    /* the pinned photo: the character's front picture (DEFAULT's is not recoloured) */
    UiG_Paint(UiM_Rounded4(72, 86, 1, 1, 1, 1), phx, phy, UI_HEX(0xF8F8F0),
              &(struct UiPaint){UiMix(brass->ink, glassLo, 0.3), UI_NONE, UI_HEX(0xD8D8D0), 0, 0, NULL, UI_NONE});
    UiG_Rect(phx + 4, phy + 4, 64, 64, UiMix(glassBase, rose->hi, 0.25));
    UiGfx_TrainerPic(sLook.portraitPic, phx + 4, phy + 4, NULL);
    UiCaps_Draw(phx + 36, phy + 73, "PORTRAIT", UI_HEX(0x8890A0), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiG_Rect(phx + 33, phy - 3, 6, 6, rose->base);
    UiG_Put(phx + 34, phy - 2, rose->hi);

    /* the pose plate */
    if (sMode == MODE_AUTO)
        snprintf(plate, sizeof(plate), "AUTO %s %s", moving ? (run ? "RUN" : "WALK") : "STAND", sDirNames[dir]);
    else
        snprintf(plate, sizeof(plate), "%s %s", moving ? (run ? "RUN" : "WALK") : "TURN", sDirNames[dir]);
    pw = UiCaps_Width(plate) + 14;
    UiG_Paint(UiM_Rounded4(pw, 13, 2, 2, 2, 2), MX + (MW >> 1) - (pw >> 1), MY + MH - 9, brass->lo,
              &(struct UiPaint){brass->ink, brass->base, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(MX + (MW >> 1), MY + MH - 6, plate, UI_HEX(0xFFF0C8), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiG_Hit(HIT_MIRROR, 0, MX, MY, MW, MH);

    /* the name card on the mirror's crown: the player's name */
    nw = UiText_Width(UI_FONT_NORMAL, sName, 0) + 18;
    UiG_Paint(UiM_Rounded4(nw, 18, 2, 2, 2, 2), MX + (MW >> 1) - (nw >> 1), MY + 12, UI_HEX(0xFFFFFF),
              &(struct UiPaint){brass->ink, UI_NONE, glassLo, 0, 0, NULL, UI_NONE});
    UiG_TextEx(MX + (MW >> 1), MY + 12, sName, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER);
}

/* the look's eight bytes as the host receives them, the shown tag's byte marked */
static void SyncTag(int tab, const struct UiRamp *rose)
{
    int tx = 8, ty = 262, tw = 216, th = 54, hl = -1, hl2 = -1;
    const char *status;
    char hex[4];

    if (tab == TAB_PRESET)
    {
        hl = 4; /* the character's number: bytes 4 and 6 */
        hl2 = 6;
    }
    else if (sTabRow[tab] >= 0 && sTabRow[tab] < NATIVE_LOOK_SAVE)
    {
        hl = sRows[sTabRow[tab]].byte;
    }
    UiG_Paint(UiM_Rounded(tw, th, UiCornerChamfer(8), UiCornerR(3), UiCornerChamfer(8), UiCornerR(3)), tx, ty,
              UI_HEX(0xF8F0E0), &(struct UiPaint){rose->ink, UI_HEX(0xFFFFFF), UI_HEX(0xE0D0B8), 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Circle(7), tx + 6, ty + 23, UiMix(rose->lo, rose->base, 0.4),
              &(struct UiPaint){rose->ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(tx + 18, ty + 7, "SYNC TAG  " UI_CAPS_DOT "  8 BYTES", rose->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT,
                UI_NONE);
    for (int i = 0; i < NATIVE_LOOK_BYTES; i++)
    {
        int bx = tx + 18 + i * 21, by = ty + 20;
        bool on = i == hl || i == hl2;

        if (on)
            UiG_Paint(UiM_Rounded4(17, 11, 1, 1, 1, 1), bx - 3, by - 2, UiMix(rose->hi, UI_HEX(0xFFFFFF), 0.3),
                      &(struct UiPaint){rose->lo, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        snprintf(hex, sizeof(hex), "%02X", sLook.look[i]);
        UiCaps_Draw(bx, by, hex, on ? rose->ink : UI_HEX(0x6A6058), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    /* what the host has: the look saved before this visit */
    if (sLook.pendingSave)
        status = sLook.connected ? "SAVED LOOK: WAITING FOR THE HOST" : "SAVED HERE: SENT WHEN YOU CONNECT";
    else if (sLook.connected)
        status = sLook.hostHasLook ? "THE HOST KEEPS YOUR SAVED LOOK" : "HOST LINK ON";
    else
        status = "OFFLINE: THE HOST STORES LOOKS";
    UiCaps_Draw(tx + 18, ty + 38, status, UI_HEX(0x9A8C80), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

static void HangTags(int tab, const struct UiRamp *rose, const struct UiRamp *brass)
{
    int hx = SX + 2;

    UiG_Rect(SX - 2, 11, SW + 4, 2, brass->lo);
    UiG_HLine(SX - 2, 11, SW + 4, brass->hi);
    for (int i = 0; i < TAB_COUNT; i++)
    {
        int w = UiCaps_Width(sTabLabels[i]) + 12, drop = i == tab ? (sSwing > 1 ? 4 : 3) : 0;
        bool on = i == tab, planned = sTabRow[i] < 0;
        const struct UiMask *tm = UiM_Rounded(w, 16, UiCornerR(1), UiCornerR(1), UiCornerChamfer(3), UiCornerChamfer(3));
        UiRGBA body = planned ? UiMix(rose->base, UI_HEX(0xA8A0A0), 0.55) : rose->base;

        UiG_VLine(hx + (w >> 1), 12, 4 + drop, brass->ink);
        if (on)
            UiG_Paint(tm, hx, 15 + drop, UI_HEX(0xFFF8EC), &(struct UiPaint){rose->ink, UI_HEX(0xFFFFFF), UI_HEX(0xE8D8C8), 0, 0, NULL, UI_NONE});
        else
            UiG_Paint(tm, hx, 15, body, &(struct UiPaint){rose->ink, UiMix(body, UI_HEX(0xFFFFFF), 0.35), UiMix(body, rose->ink, 0.3), 0, 0, NULL, UI_NONE});
        UiG_Put(hx + (w >> 1), 17 + drop, on ? rose->base : rose->ink);
        UiCaps_Draw(hx + (w >> 1), 20 + drop, sTabLabels[i], on ? (planned ? UI_HEX(0x9A9090) : rose->ink) : UI_P_TEXT_W,
                    on ? UI_NONE : rose->ink, on ? UI_CAPS_PLAIN : UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
        UiG_Hit(HIT_TAG, i, hx, 12, w, 22);
        hx += w + 4;
    }
}

static void NoteLines(int x, int y, int width, const u8 *text, UiRGBA fg)
{
    const u8 *starts[4];
    int lengths[4];
    int n = UiText_Wrap(text, width, UI_FONT_SMALL, starts, lengths, 4);

    for (int i = 0; i < n && i < 3; i++)
    {
        u8 line[96];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, x, y + i * 13, line, fg, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

/* a planned style row: its label, the mark, and the one style this body has */
static int StyleRow(int y, const char *label, const struct UiRamp *rose)
{
    const struct UiMask *tm = UiM_Rounded4(60, 22, 2, 2, 2, 2);
    struct UiPattern hatch = UiPat_Diag(6, 1, 0, 1);

    UiCaps_Draw(SX + 10, y + 1, label, UI_HEX(0xA8A098), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiW_Planned(SX + 14 + UiCaps_Width(label), y - 1, "PLANNED: NEEDS LAYERED ART");
    y += 11;
    UiG_Paint(tm, SX + 10, y, UiMix(UI_P_CREAM, UI_P_PAPER, 0.5),
              &(struct UiPaint){UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.2), UI_NONE, UI_NONE, 0, 0, &hatch,
                                UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.5)});
    UiCaps_Draw(SX + 40, y + 8, sLook.isMay ? "MAY" : "BRENDAN", UI_HEX(0x8C8478), UI_NONE, UI_CAPS_PLAIN,
                UI_ALIGN_CENTER, UI_NONE);
    UiW_Pix("cz_tick", sTick, 6, (const struct UiPixPal[]){{'#', rose->lo}}, 1, SX + 60, y + 15, false);
    NoteLines(SX + 78, y - 2, SW - 88, sNote_Style, UI_P_TEXT_MUTED);
    return y + 30;
}

static void ColourRow(int y, int row, const struct UiRamp *rose)
{
    const struct LookRow *r = &sRows[row];
    int cur = sLook.look[r->byte], started = sLook.start[r->byte];
    bool focused = sLook.cursor == row;
    char slots[40];
    int len;

    UiCaps_Draw(SX + 10, y + 1, sColourLabels[row], rose->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    len = snprintf(slots, sizeof(slots), "PALETTE SLOTS");
    for (int i = 0; i < r->slotCount && len < (int)sizeof(slots) - 4; i++)
        len += snprintf(slots + len, sizeof(slots) - len, " %d", r->slots[i]);
    UiCaps_Draw(SX + SW - 10, y + 1, slots, UI_HEX(0xA09080), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    y += 12;
    for (int i = 0; i < r->count; i++)
    {
        int x = SX + 10 + (i % SWATCH_COLUMNS) * 40, sy = y + (i / SWATCH_COLUMNS) * 40;
        const struct UiMask *sm = UiM_Rounded4(28, 24, 3, 3, 3, 3);
        struct UiRamp rr = SwatchRamp(r, i);

        if (focused && i == cur)
            UiW_SelectFrame(sm, x, sy, UI_NONE, UI_NONE, 2);
        else if (i == cur)
            UiW_SelectFrame(sm, x, sy, UiMix(rose->base, UI_HEX(0xFFFFFF), 0.3), UiMix(rose->hi, UI_HEX(0xFFFFFF), 0.5), 1);
        UiG_Paint(sm, x, sy, rr.base, &(struct UiPaint){rr.ink, rr.hi, rr.lo, 3, 3, NULL, UI_NONE});
        if (i == 0)
            UiCaps_Draw(x + 14, sy + 27, "BASE", rose->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        if (i == started)
            UiW_Pix("cz_tickw", sTick, 6, (const struct UiPixPal[]){{'#', UI_HEX(0xFFFFFF)}}, 1, x + 10, sy + 9, false);
        UiG_Hit(HIT_SWATCH, row * 32 + i, x - 2, sy - 2, 32, 28);
    }
}

static void PlannedSheet(int tab, const struct UiRamp *rose)
{
    const u8 *note = tab == TAB_BOTTOM ? sNote_Bottom : tab == TAB_SHOES ? sNote_Shoes : tab == TAB_BAG ? sNote_Bag
                   : sNote_Extra;
    int y = SY + 6;

    UiCaps_Draw(SX + 10, y + 1, sStyleLabels[tab], UI_HEX(0xA8A098), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiW_Planned(SX + 14 + UiCaps_Width(sStyleLabels[tab]), y - 1, "PLANNED: NEEDS LAYERED ART");
    NoteLines(SX + 10, y + 16, SW - 20, note, UI_P_TEXT_MUTED);
    (void)rose;
}

/* the PRESET sheet: DEFAULT and every character of the catalogue standing, the
 * one shown framed (pressed in while the editor's cursor is on CHARACTER), the
 * one the editor started from ticked */
static void PresetSheet(const struct UiRamp *rose)
{
    int y = SY + 6, gy = SY + 40;
    bool focused = sLook.cursor == NATIVE_LOOK_CHARACTER;
    const struct NativeCharacterCard *shown = sLook.character < sCardCount ? &sCards[sLook.character] : NULL;
    char caps[24];

    UiCaps_Draw(SX + 10, y + 1, "CHARACTER", rose->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sLook.character != 0)
        snprintf(caps, sizeof(caps), "%d / %d", sLook.character, sCardCount - 1);
    else
        snprintf(caps, sizeof(caps), "%d CHARACTERS", sCardCount - 1);
    UiCaps_Draw(SX + SW - 10, y + 1, caps, UI_HEX(0xA09080), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    if (shown != NULL)
    {
        int nw = UiText_Width(UI_FONT_NORMAL, shown->name, 0);

        UiText_Draw(UI_FONT_NORMAL, SX + 10, y + 11, shown->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_SMALL, SX + 18 + nw, y + 14, shown->variant, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    for (int i = 0; i < sCardCount; i++)
    {
        int x = SX + 2 + (i % CARD_COLUMNS) * CARD_W, cy = gy + (i / CARD_COLUMNS) * CARD_H;
        const struct UiMask *cm = UiM_Rounded4(CARD_W - 4, CARD_H - 3, 3, 3, 3, 3);
        bool on = i == sLook.character;

        if (on && focused)
            UiW_SelectFrame(cm, x + 2, cy + 1, UI_NONE, UI_NONE, 2);
        else if (on)
            UiW_SelectFrame(cm, x + 2, cy + 1, UiMix(rose->base, UI_HEX(0xFFFFFF), 0.3),
                            UiMix(rose->hi, UI_HEX(0xFFFFFF), 0.5), 1);
        UiG_Paint(cm, x + 2, cy + 1, on ? UiMix(rose->hi, UI_HEX(0xFFFFFF), 0.55) : UiMix(UI_P_CREAM, UI_P_PAPER, 0.5),
                  &(struct UiPaint){UiMix(UI_P_CREAM_LO, rose->lo, on ? 0.6 : 0.2), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiGfx_NativeFieldSprite(&sCards[i].sprite, 0, 0, 0, x + 14, cy + 2, 1);
        if (i == sLook.startCharacter)
            UiW_Pix("cz_tickc", sTick, 6, (const struct UiPixPal[]){{'#', rose->lo}}, 1, x + CARD_W - 12, cy + 4, false);
        UiG_Hit(HIT_CHARACTER, i, x, cy, CARD_W, CARD_H);
    }
}

static void Unlocks(const struct UiRamp *rose)
{
    static const char *const sources[] = {"GYMS", "CHAMPIONSHIP", "PVP", "EVENTS", "EXPLORING", "STORES", "QUESTS"};
    int uy = SY + SH - 46, ux = SX + 10;

    for (int i = 0; i < SW - 16; i += 3)
        UiG_Put(SX + 8 + i, uy - 4, UI_P_CREAM_LO);
    UiCaps_Draw(SX + 10, uy + 2, "UNLOCKS", rose->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiW_Planned(SX + 56, uy, NULL);
    UiText_Draw(UI_FONT_SMALL, SX + 116, uy + 1, sNote_Unlocks, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++)
    {
        int uw = UiCaps_Width(sources[i]) + 10;

        UiG_Paint(UiM_Rounded4(uw, 13, 2, 2, 2, 2), ux, uy + 17, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.4),
                  &(struct UiPaint){UiMix(UI_P_CREAM_LO, rose->lo, 0.35), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(ux + 5, uy + 20, sources[i], UI_HEX(0x8C7C70), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        ux += uw + 4;
    }
}

static void Sheet(int tab, const struct UiRamp *rose)
{
    UiG_Paint(UiM_Rounded4(SW, SH, 3, 3, 3, 3), SX, SY, UI_P_CREAM,
              &(struct UiPaint){rose->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (tab == TAB_PRESET)
    {
        PresetSheet(rose); /* the whole sheet: no unlocks strip */
        return;
    }
    if (sTabRow[tab] < 0)
    {
        PlannedSheet(tab, rose);
    }
    else
    {
        int y = StyleRow(SY + 6, sStyleLabels[tab], rose);

        if (sLook.coloursEnabled)
            ColourRow(y, sTabRow[tab], rose);
        else
            NoteLines(SX + 10, y, SW - 20, sNote_OwnColours, UI_P_TEXT_MUTED);
    }
    Unlocks(rose);
}

/* the help line: what the focused choice does, or the row's action */
static void Help(int tab)
{
    int b[4];
    u8 text[160];
    const u8 *starts[4];
    int lengths[4], n;

    UiW_DialogBox(SX, 268, HELP_W, 48, NULL, UI_NONE, b);
    text[0] = EOS;
    if (sTabRow[tab] < 0 && sViewTab >= 0)
        UiText_Append(text, sizeof(text), sHelp_Planned); /* the tag's own note is on the sheet */
    else if (sViewTab >= 0 && tab != TAB_PRESET && !sLook.coloursEnabled)
        UiText_Append(text, sizeof(text), sHelp_OwnColours);
    else if (sLook.cursor == NATIVE_LOOK_CHARACTER && sLook.character < sCardCount)
    {
        UiText_Append(text, sizeof(text), sCards[sLook.character].name);
        UiText_Append(text, sizeof(text), sHelp_Sep);
        UiText_Append(text, sizeof(text), sCards[sLook.character].variant);
        UiText_Append(text, sizeof(text), sHelp_Character);
    }
    else if (sLook.cursor == NATIVE_LOOK_SAVE)
        UiText_Append(text, sizeof(text), sLook.connected ? sHelp_Save : sHelp_SaveOffline);
    else if (sLook.cursor == NATIVE_LOOK_CANCEL)
        UiText_Append(text, sizeof(text), sHelp_Cancel);
    else if (sLook.cursor < NATIVE_LOOK_SAVE)
    {
        const struct LookRow *r = &sRows[sLook.cursor];
        int cur = sLook.look[r->byte];

        UiText_Append(text, sizeof(text), cur == 0 ? sHelp_Base : r->names[cur < r->count ? cur : 0]);
        UiText_Append(text, sizeof(text), sHelp_Recolours);
        for (int i = 0; i < r->slotCount; i++)
        {
            if (i > 0)
                UiText_AppendAscii(text, sizeof(text), i == r->slotCount - 1 ? " and " : ", ");
            UiText_AppendNumber(text, sizeof(text), r->slots[i], 1, false);
        }
        UiText_Append(text, sizeof(text), sHelp_Live);
    }
    n = UiText_Wrap(text, b[2] - 4, UI_FONT_NORMAL, starts, lengths, 4);
    for (int i = 0; i < n && i < 2; i++)
    {
        u8 line[96];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiG_Text(b[0] + 2, b[1] - 1 + i * 16, line);
    }
}

static void Buttons(int t)
{
    struct UiDsButtonOpt o = {0};

    o.selected = sLook.cursor == NATIVE_LOOK_SAVE;
    o.center = true;
    UiW_DsButton(BTN_X, 268, BTN_W, 22, UI_HEX(0xC86088), sText_Save, &o);
    UiG_Hit(HIT_SAVE, 0, BTN_X, 268, BTN_W, 22);
    o.selected = sLook.cursor == NATIVE_LOOK_CANCEL;
    UiW_DsButton(BTN_X, 294, BTN_W, 22, UI_HEX(0x8890A0), sText_Cancel, &o);
    UiG_Hit(HIT_CANCEL, 0, BTN_X, 294, BTN_W, 22);
    if (sLook.cursor == NATIVE_LOOK_SAVE || sLook.cursor == NATIVE_LOOK_CANCEL)
        UiW_Cursor(BTN_X + 6, (sLook.cursor == NATIVE_LOOK_SAVE ? 268 : 294) + 7, UiW_Nudge(t), UI_NONE);
}

static void Question(int t)
{
    int b[4];

    if (sAsk == ASK_NONE)
        return;
    UiW_DialogBox(SX, 268, HELP_W, 48, NULL, UI_NONE, b);
    UiG_Text(b[0] + 2, b[1] - 1, sAsk == ASK_SAVE ? sAsk_Ok : sAsk_Discard);
    UiText_Draw(UI_FONT_SMALL, b[0] + 2, b[1] + 17, sAsk == ASK_SAVE ? sAsk_OkNote : sAsk_DiscardNote, UI_P_TEXT_MUTED,
                UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiW_YesNo(632, 262, sAskSel, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
}

static void Draw(int t)
{
    struct UiRamp rose = UiRampOf(UI_HEX(0xC86088), 0.42, 0.28, 0.62);
    struct UiRamp brass = UiRampOf2(UI_HEX(0xD0A050), 0.42, 0.28, 0.62, UI_NONE, UI_RGB(90, 50, 30));
    struct UiRamp titleRamp = {rose.hi, rose.lo, UiMix(rose.lo, rose.ink, 0.5), rose.ink};
    int tab = ShownTab();

    Background(&rose);
    UiW_TitleTab(0, 8, "TRAINER STYLE", &titleRamp, 0, NULL);
    Mirror(t, &rose, &brass);
    SyncTag(tab, &rose);
    HangTags(tab, &rose, &brass);
    Sheet(tab, &rose);
    Help(tab);
    Buttons(t);
    {
        struct UiHintItem hints[5] = {{UI_ACT_TABS, sHint_Category}, {UI_ACT_MOVE, tab == TAB_PRESET ? sHint_Character : sHint_Colour},
                                      {UI_ACT_START, sHint_Save}, {UI_ACT_DETAILS, sHint_Pose},
                                      {UI_ACT_CANCEL, sHint_Back}};

        UiW_Hints(SX + 4, 330, hints, 5, true, 8);
    }
    UiText_Draw(UI_FONT_SMALL, 12, 327, sFoot_Portraits, UiMix(rose.hi, UI_HEX(0xFFFFFF), 0.5), rose.ink, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, 12, 340, sFoot_Planned, UiMix(rose.hi, UI_HEX(0xFFFFFF), 0.5), rose.ink, UI_ALIGN_LEFT, 0);
    Question(t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input: the engine's own buttons */

/* the editor's cursor to a row (the editor skips the colour rows while a
 * character is shown: FALSE then) */
static bool MoveTo(int row)
{
    if (!MultiplayerAppearance_NativeUiSetCursor(row))
        return false;
    sLook.cursor = row; /* the presses queued next act on it */
    return true;
}

/* presses that set a colour row to a choice (the choices wrap: the shorter way) */
static void Choose(int row, int choice)
{
    const struct LookRow *r = &sRows[row];
    int n = r->count, d;

    if (n <= 0 || choice >= n || !MoveTo(row))
        return;
    d = (choice - sLook.look[r->byte] + n) % n;
    if (d <= n / 2)
        NativeUi_QueuePresses(DPAD_RIGHT, d);
    else
        NativeUi_QueuePresses(DPAD_LEFT, n - d);
}

/* L and R: the next tag the editor takes (PRESET, and the colour tags for DEFAULT), in the tags' order */
static void StepTab(int dir)
{
    int count = (int)sizeof(sEnabledTabs), at = 0, tab = sViewTab >= 0 ? sViewTab : ShownTab();

    for (int i = 0; i < count; i++)
        if (sEnabledTabs[i] == tab)
            at = i;
    if (sTabRow[tab] < 0)
        at = dir > 0 ? count - 1 : 0; /* from a planned tag: the first or the last */
    sViewTab = -1;
    for (int tries = 0; tries < count; tries++)
    {
        at = (at + dir + count) % count;
        if (MoveTo(sTabRow[sEnabledTabs[at]]))
            break;
    }
}

static void Ask(int kind)
{
    sAsk = kind;
    sAskSel = kind == ASK_SAVE ? 0 : 1;
}

static void Answer(bool yes)
{
    int kind = sAsk;

    sAsk = ASK_NONE;
    if (!yes)
        return;
    if (kind == ASK_SAVE)
    {
        MoveTo(NATIVE_LOOK_SAVE);
        NativeUi_QueuePress(A_BUTTON);
    }
    else
    {
        NativeUi_QueuePress(B_BUTTON);
    }
}

static void Back(void)
{
    if (Dirty())
        Ask(ASK_DISCARD);
    else
        NativeUi_QueuePress(B_BUTTON);
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    if (sAsk != ASK_NONE)
    {
        if (newKeys & (DPAD_UP | DPAD_DOWN))
            sAskSel ^= 1;
        if (newKeys & A_BUTTON)
            Answer(sAskSel == 0);
        else if (newKeys & B_BUTTON)
            sAsk = ASK_NONE;
        return 0; /* the editor waits meanwhile */
    }
    if (NativeUi_PressesPending())
        return keys & ~(START_BUTTON | L_BUTTON | R_BUTTON);
    if (newKeys & START_BUTTON)
    {
        Ask(ASK_SAVE);
        return keys & ~START_BUTTON;
    }
    if ((newKeys & B_BUTTON) && Dirty())
    {
        Ask(ASK_DISCARD);
        return keys & ~B_BUTTON;
    }
    if (newKeys & (L_BUTTON | R_BUTTON))
    {
        StepTab((newKeys & R_BUTTON) ? 1 : -1);
        return keys & ~(L_BUTTON | R_BUTTON);
    }
    if (newKeys & (DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT | A_BUTTON))
        sViewTab = -1;
    return keys;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (sAsk != ASK_NONE)
    {
        if (button == 1 && hit->id == UI_HIT_YESNO)
            Answer(hit->index == 0);
        else if (button == 3)
            sAsk = ASK_NONE;
        return true;
    }
    if (button == 3)
    {
        if (!NativeUi_PressesPending())
            Back();
        return true;
    }
    if (button != 1)
        return false;
    if (hit->id == HIT_MIRROR)
    {
        sMode = (sMode + 1) % MODE_COUNT;
        return true;
    }
    if (NativeUi_PressesPending())
        return true;
    switch (hit->id)
    {
    case HIT_TAG:
        if (hit->index >= 0 && hit->index < TAB_COUNT)
        {
            if (sTabRow[hit->index] >= 0 && MoveTo(sTabRow[hit->index]))
            {
                sViewTab = -1;
            }
            else
            {
                /* a planned tag, or a colour tag while a character keeps its own
                 * colours: its note, until the cursor moves */
                sViewTab = hit->index;
                sViewCursor = sLook.cursor;
            }
        }
        return true;
    case HIT_CHARACTER:
        sViewTab = -1;
        if (MultiplayerAppearance_NativeUiSelectCharacter(hit->index))
            sLook.cursor = NATIVE_LOOK_CHARACTER;
        return true;
    case HIT_SWATCH:
        sViewTab = -1;
        Choose(hit->index / 32, hit->index % 32);
        return true;
    case HIT_SAVE:
        Ask(ASK_SAVE);
        return true;
    case HIT_CANCEL:
        Back();
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (sAsk == ASK_NONE && !NativeUi_PressesPending())
        StepTab(dy > 0 ? -1 : 1);
    return true;
}

/* Tab, the pad's Y: the mirror's pose (walk, run, turn) */
static bool PcKey(int key)
{
    if (key != UI_PCKEY_DETAILS)
        return false;
    sMode = (sMode + 1) % MODE_COUNT;
    return true;
}

static bool Ready(void)
{
    return sReady && sAsk == ASK_NONE;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Customize = {
    .name = "customize",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
    .pcKey = PcKey,
};
#endif // ENABLE_NATIVE_UI
