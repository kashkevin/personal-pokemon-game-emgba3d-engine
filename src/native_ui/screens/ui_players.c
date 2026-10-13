#ifdef ENABLE_NATIVE_UI
/*
 * 12 · Players (the approved design's preview/js/screens/players.js, its LINK
 * kit): the link device over the game's host-driven Players menu
 * (multiplayer_menu.c, START > PLAYER > PLAYERS). The host decides every page
 * (the players list, a player's actions, trades, teams, challenges, records):
 * its title, status line and up to six rows, some only information. The UI
 * shows them on the design's sheet with the engine's cursor, the "Waiting for
 * server" state while a request is out, and B as the host's Back or Close.
 *
 * The device pane beside it shows what this game itself knows: you (your field
 * sprite with your saved look, your name, where you are), the session's
 * connected players from the bridge (their slots, where they are), and your
 * Trainer Card's public facts. The design's FRIENDS and ALL tabs, friendship
 * ranks and Lucky Friends are PLANNED (not built). Keys and clicks reach the
 * engine's own menu as its own buttons. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "event_data.h"
#include "multiplayer.h"
#include "overworld.h"
#include "region_map.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/trainers.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_TAB,
};

#define SHX 224
#define SHY 30
#define SHW 408
#define SHH 258
#define ROW_H 32
#define MAX_ROWS 6
#define POLL_FRAMES 120   /* the LIVE lamp: the host's status is read every 2 s */

static const uint32_t sSlotHex[4] = {0x58B048, 0x4C88E0, 0xE85848, 0xE0B028};
static const u8 sText_Waiting[] = _("Waiting for the host...");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Close[] = _("Close");
static const u8 sText_TradeHold[] = _("The trade is open: finish or cancel it here.");
static const u8 sText_BattleHold[] = _("Entering the battle...");
static const u8 sText_Alone[] = _("No one else is connected right now.");
static const u8 sText_HereWithYou[] = _("Here with you");
static const u8 sText_InAMenu[] = _("In this menu");

struct Peer
{
    u32 id;
    u8 gender;
    u8 activity;
    bool8 here;
    u8 where[24];
};

static struct NativeMpMenu sM;
static u8 sTitle[32], sStatus[40];
static u8 sRows[MAX_ROWS][32];
static u8 sName[PLAYER_NAME_LENGTH + 1];
static u8 sWhere[24];
static u16 sTrainerIdPublic;
static u8 sBadges;
static bool8 sIsMay;
static struct NativeFieldFrame sWalk[4];   /* you walking down: your character, or the trainer in your look */
static u16 sPic;
static u16 sLcdPalette[16];
static struct Peer sPeers[MULTIPLAYER_MAX_PEERS];
static int sPeerCount;
static bool sConnected;
static bool sReady;

static bool Detect(void)
{
    struct NativeMpMenu m;

    return MultiplayerMenu_NativeUiState(&m);
}

/* the host's ASCII as engine text ('$' is the currency sign) */
static void Ascii(u8 *dst, int size, const u8 *ascii)
{
    char tmp[40];
    int n = 0;

    for (; n < (int)sizeof(tmp) - 1 && ascii[n] != 0; n++)
        tmp[n] = (char)ascii[n];
    tmp[n] = '\0';
    UiText_FromAscii(dst, size, tmp);
}

static UiRGBA Rgb555(u16 c)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    return UI_RGB((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

static u16 To555(UiRGBA c)
{
    return (u16)((UI_R(c) >> 3) | ((UI_G(c) >> 3) << 5) | ((UI_B(c) >> 3) << 10));
}

/* the field sprite in the LCD's three tones by brightness (the design's owLcd) */
static void LcdPalette(const u16 *pal)
{
    static const uint32_t tones[3] = {0x2C4A2A, 0x9CC47C, 0xC8E4A0};

    sLcdPalette[0] = 0;
    for (int i = 1; i < 16; i++)
    {
        UiRGBA c = Rgb555(pal[i]);
        double lum = 0.299 * UI_R(c) + 0.587 * UI_G(c) + 0.114 * UI_B(c);

        sLcdPalette[i] = To555(UI_HEX(tones[lum < 100 ? 0 : lum < 190 ? 1 : 2]));
    }
}

static void Capture(void)
{
    u16 pal[16];

    MultiplayerMenu_NativeUiState(&sM);
    sReady = !sM.waiting && !sM.admissionHeld && sM.revision != 0;
    Ascii(sTitle, sizeof(sTitle), sM.title);
    Ascii(sStatus, sizeof(sStatus), sM.status);
    for (int i = 0; i < sM.count && i < MAX_ROWS; i++)
        Ascii(sRows[i], sizeof(sRows[i]), sM.rows[i]);
    StringCopyN(sName, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH);
    sName[PLAYER_NAME_LENGTH] = EOS;
    GetMapName(sWhere, gMapHeader.regionMapSectionId, 0);
    sTrainerIdPublic = (u16)(gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8));
    sBadges = 0;
    for (u32 f = FLAG_BADGE01_GET; f <= FLAG_BADGE08_GET; f++)
        if (FlagGet(f))
            sBadges++;
    sIsMay = gSaveBlock2Ptr->playerGender != MALE;
    sPic = MultiplayerAppearance_NativeUiFrontPic(MultiplayerAppearance_NativeUiLocalCharacter(), sIsMay);
    UiGfx_PrepareTrainerPic(sPic);
    for (int k = 0; k < 4; k++)
        MultiplayerAppearance_NativeUiPlayerFrame(0, NATIVE_GAIT_WALK, k, &sWalk[k]);
    memcpy(pal, sWalk[0].palette, sizeof(sWalk[0].palette));
    LcdPalette(pal);
    sConnected = Multiplayer_IsConnected();
    sPeerCount = 0;
    for (u32 i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        struct Peer *p = &sPeers[sPeerCount];
        u8 group, num;

        if (!Multiplayer_NativeUiPeer(i, &p->id, &group, &num, &p->gender, &p->activity))
            continue;
        p->here = p->activity != MULTIPLAYER_ACTIVITY_UNAVAILABLE && group == gSaveBlock1Ptr->location.mapGroup
               && num == gSaveBlock1Ptr->location.mapNum;
        GetMapName(p->where, Overworld_GetMapHeaderByGroupAndId(group, num)->regionMapSectionId, 0);
        sPeerCount++;
    }
}

/* ---------------------------------------------------------------- the LINK kit (players.js) */

static bool NetLine(int x, int y, const void *ctx)
{
    int a = x & 15, b = y & 15;

    (void)ctx;
    return (b == 8 && (a < 6 || a > 10) && (a & 1) == 0) || (a == 8 && (b < 6 || b > 10) && (b & 1) == 0);
}

static bool NetNode(int x, int y, const void *ctx)
{
    int a = x & 15, b = y & 15, dx = abs(a - 8), dy = abs(b - 8);

    (void)ctx;
    return dx <= 2 && dy <= 2 && (dx == 2 || dy == 2) && !(dx == 2 && dy == 2);
}

/* the network motif: dotted traces between ring nodes, drifting 1 px every 5 frames */
static void Background(int t, UiRGBA hue)
{
    UiRGBA base = UiMix(hue, UI_P_INK, 0.3);
    UiRGBA line = UiMix(base, UI_HEX(0xFFFFFF), 0.12), node = UiMix(base, UI_HEX(0xFFFFFF), 0.2);
    int bx, by, bw, bh, s = (t / 5) % 16;
    const struct UiMask *lines = UiM_Fn(16, 16, "mpnetl", NetLine, NULL);
    const struct UiMask *nodes = UiM_Fn(16, 16, "mpnetn", NetNode, NULL);

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, base);
    for (int y = by - 16 + ((s - by) % 16 + 16) % 16; y < by + bh; y += 16)
        for (int x = bx - 16 + ((s - bx) % 16 + 16) % 16; x < bx + bw; x += 16)
        {
            UiG_Mask(lines, x, y, line);
            UiG_Mask(nodes, x, y, node);
        }
}

static void Shell(int x, int y, int w, int h)
{
    struct UiRamp f = UiW_FieldBand();
    const int screws[4][2] = {{x + 4, y + 4}, {x + w - 8, y + 4}, {x + 4, y + h - 9}, {x + w - 8, y + h - 9}};

    UiG_Paint(UiM_Rounded4(w, h, 5, 5, 5, 5), x, y, f.base, &(struct UiPaint){UI_P_FIELD_INK, f.hi, f.lo, 0, 2, NULL, UI_NONE});
    for (int i = 0; i < 4; i++)
    {
        UiG_Paint(UiM_Circle(4), screws[i][0], screws[i][1], f.lo, &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiG_Put(screws[i][0] + 1, screws[i][1] + 1, f.hi);
    }
}

static void Grille(int x, int y, int cols, int rows)
{
    struct UiRamp f = UiW_FieldBand();

    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++)
        {
            int px = x + i * 4 + (j & 1) * 2, py = y + j * 3;

            UiG_Put(px, py, f.lo);
            UiG_Put(px + 1, py + 1, UiMix(f.lo, UI_P_FIELD_INK, 0.4));
        }
}

#define LCD_LIGHT UI_HEX(0xC8E4A0)
#define LCD_MID UI_HEX(0x9CC47C)
#define LCD_DARK UI_HEX(0x2C4A2A)
#define LCD_SCAN UI_HEX(0xBCDA94)

static void Lcd(int x, int y, int w, int h)
{
    struct UiRamp f = UiW_FieldBand();
    struct UiPattern scan = UiPat_HStripe(2, 1, 0);

    UiG_Paint(UiM_Rounded4(w + 6, h + 6, 3, 3, 3, 3), x - 3, y - 3, UiMix(f.lo, UI_P_FIELD_INK, 0.3),
              &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, f.lo, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, LCD_LIGHT, &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_NONE, 0, 0, &scan, LCD_SCAN});
    UiG_HLine(x + 2, y + 1, w - 4, LCD_MID);
}

static void Led(int x, int y, UiRGBA colour, bool on)
{
    struct UiRamp f = UiW_FieldBand();
    UiRGBA c = on ? UiMix(colour, UI_HEX(0xFFFFFF), 0.18) : UiMix(colour, UI_P_INK, 0.62);

    UiG_Paint(UiM_Circle(9), x - 1, y - 1, UiMix(f.lo, UI_P_FIELD_INK, 0.55), &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Circle(7), x, y, c, &(struct UiPaint){on ? UiMix(colour, UI_P_INK, 0.5) : UI_P_FIELD_INK, UI_NONE, on ? colour : UI_NONE, 0, 0, NULL, UI_NONE});
    if (on)
    {
        UiG_Put(x + 2, y + 2, UiMix(c, UI_HEX(0xFFFFFF), 0.8));
        UiG_Put(x + 3, y + 2, UiMix(c, UI_HEX(0xFFFFFF), 0.45));
        UiG_Put(x + 2, y + 3, UiMix(c, UI_HEX(0xFFFFFF), 0.45));
    }
}

/* a capsule label (status chips) */
static int Chip(int x, int y, const char *label, UiRGBA colour, bool right)
{
    struct UiRamp r = UiRampOf(colour, 0.4, 0.25, 0.6);
    int w = UiCaps_Width(label) + 10;

    if (right)
        x -= w;
    UiG_Paint(UiM_Capsule(w, 11, true, true), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + (w >> 1), y + 2, label, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    return right ? x : x + w;
}

/* ---------------------------------------------------------------- the device pane: what this game knows */

static void Device(int t)
{
    struct UiRamp f = UiW_FieldBand();
    int dx = 8, dy = 32, dw = 208, dh = 306, lx = 18, ly = 42, lw = 188, lh = 82;
    int cx = 16, cy = 148, cw = 192, ch = 182, tr = cx + cw - 8;
    bool lit = (t % POLL_FRAMES) < 8;
    char caps[40];

    Shell(dx, dy, dw, dh);
    Lcd(lx, ly, lw, lh);
    UiCaps_Draw(lx + 6, ly + 5, "LINK", LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(lx + lw - 6, ly + 5, "THIS IS YOU", LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_HLine(lx + 4, ly + 14, lw - 8, LCD_MID);
    /* you, in the LCD's tones, walking */
    {
        const struct NativeFieldFrame *f = &sWalk[(t / 10) % 4];

        if (f->tiles != NULL)
            UiGfx_FieldSpriteSized(f->tiles, 1, sLcdPalette, 0, f->width, f->height, lx + 17 - f->width / 2,
                                   ly + 51 - f->height, 1, f->flip);
    }
    UiText_Draw(UI_FONT_NORMAL, lx + 32, ly + 16, sName, LCD_DARK, LCD_MID, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, lx + 32, ly + 33, sWhere, LCD_DARK, UI_NONE, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, lx + 32, ly + 45, sText_InAMenu, LCD_DARK, UI_NONE, UI_ALIGN_LEFT, 0);
    UiG_HLine(lx + 4, ly + lh - 17, lw - 8, LCD_MID);
    UiG_Paint(UiM_Circle(5), lx + 6, ly + lh - 12, lit ? LCD_DARK : LCD_LIGHT, &(struct UiPaint){LCD_DARK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(lx + 14, ly + lh - 12, sConnected ? "LIVE" : "OFFLINE", LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%d/%d IN THIS WORLD", sPeerCount + 1, MULTIPLAYER_MAX_PEERS + 1);
    UiCaps_Draw(lx + lw - 6, ly + lh - 12, caps, LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);

    /* the session: one LED a slot, lit while that player is connected */
    UiCaps_Draw(dx + 12, 133, "SESSION", UI_P_TEXT_W, UI_P_FIELD_INK, UI_CAPS_SHADOW, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i <= MULTIPLAYER_MAX_PEERS && i < 4; i++)
        Led(dx + 60 + i * 12, 132, UI_HEX(sSlotHex[i]), i == 0 ? sConnected : i <= sPeerCount);
    Grille(dx + dw - 50, 133, 10, 3);

    /* the card: your picture and your Trainer Card's public facts */
    UiG_Paint(UiM_Rounded4(cw, ch, 3, 3, 3, 3), cx, cy, UI_P_CREAM, &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    {
        struct UiPattern diag = UiPat_Diag(8, 3, 0, 1);
        UiRGBA tint = UiMix(UI_HEX(sSlotHex[0]), UI_HEX(0xFFFFFF), 0.7);

        UiG_Paint(UiM_Rounded4(70, 70, 2, 2, 2, 2), cx + 5, cy + 5, tint,
                  &(struct UiPaint){UiMix(UI_HEX(sSlotHex[0]), UI_P_INK, 0.45), UI_NONE, UI_NONE, 0, 0, &diag,
                                    UiMix(UI_HEX(sSlotHex[0]), UI_HEX(0xFFFFFF), 0.56)});
        UiGfx_TrainerPic(sPic, cx + 8, cy + 8, NULL);
    }
    UiG_Text(cx + 82, cy + 3, sName);
    UiCaps_Draw(cx + 82, cy + 25, "ID NO.", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%05u", (unsigned)sTrainerIdPublic);
    UiCaps_Draw(tr, cy + 25, caps, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiCaps_Draw(cx + 82, cy + 40, "BADGES", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%u/8", (unsigned)sBadges);
    UiCaps_Draw(tr, cy + 40, caps, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiCaps_Draw(cx + 82, cy + 55, "LINK", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    Chip(tr, cy + 53, sConnected ? "ONLINE" : "OFFLINE", UI_HEX(sConnected ? 0x58B048 : 0xA0A0A8), true);
    UiG_HLine(cx + 6, cy + 81, cw - 12, UI_P_CREAM_LO);

    /* the others in this world, from the bridge: where each one is */
    UiCaps_Draw(cx + 8, cy + 87, "IN THIS WORLD", UiMix(f.lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sPeerCount == 0)
    {
        const u8 *starts[3];
        int lengths[3];
        int n = UiText_Wrap(sText_Alone, cw - 20, UI_FONT_SMALL, starts, lengths, 3);

        for (int i = 0; i < n && i < 2; i++)
        {
            u8 line[64];

            UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
            UiText_Draw(UI_FONT_SMALL, cx + 8, cy + 99 + i * 12, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
    }
    for (int i = 0; i < sPeerCount && i < 3; i++)
    {
        int py = cy + 98 + i * 14;

        UiG_Rect(cx + 8, py + 2, 4, 9, UI_HEX(sSlotHex[i + 1]));
        UiText_Draw(UI_FONT_SMALL, cx + 16, py, sPeers[i].here ? sText_HereWithYou : sPeers[i].where, UI_P_TEXT,
                    UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    UiG_HLine(cx + 6, cy + 145, cw - 12, UI_P_CREAM_LO);
    UiCaps_Draw(cx + 8, cy + 151, "FRIENDSHIP", UiMix(f.lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiW_Planned(tr - (UiCaps_Width("PLANNED") + 8), cy + 149, NULL);
    UiCaps_Draw(cx + 8, cy + 166, "RANKS AND LUCKY FRIENDS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

/* ---------------------------------------------------------------- the host's page */

static void Tabs(void)
{
    struct UiRamp f = UiW_FieldBand();
    static const char *const planned[2] = {"FRIENDS", "ALL"};
    char title[32];
    int n = 0;

    for (int i = 0; sM.title[i] != 0 && n < (int)sizeof(title) - 1; i++)
        title[n++] = (char)(sM.title[i] >= 'a' && sM.title[i] <= 'z' ? sM.title[i] - 32 : sM.title[i]);
    title[n] = '\0';
    if (n == 0)
        snprintf(title, sizeof(title), "PLAYERS");
    UiG_Paint(UiM_Rounded4(150, 26, 4, 4, 0, 0), SHX + 4, 6, f.base, &(struct UiPaint){UI_P_FIELD_INK, f.hi, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(SHX + 4 + 75, 14, title, UI_P_TEXT_W, UI_P_FIELD_INK, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
    for (int i = 0; i < 2; i++)
    {
        UiRGBA dk = UiMix(f.lo, UI_P_INK, 0.25);
        int tx = SHX + 158 + i * 104;

        UiG_Paint(UiM_Rounded4(100, 20, 4, 4, 0, 0), tx, 12, dk, &(struct UiPaint){UI_P_FIELD_INK, UiMix(dk, f.lo, 0.6), UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(tx + 8, 18, planned[i], UiMix(UI_P_TEXT_W, dk, 0.35), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiW_Planned(tx + 12 + UiCaps_Width(planned[i]), 16, NULL);
        UiG_Hit(HIT_TAB, i + 1, tx, 12, 100, 20);
    }
}

static void Sheet(int t)
{
    struct UiRamp f = UiW_FieldBand();
    int ix = SHX + 6, iy = SHY + 7, iw = SHW - 12, ih = SHH - 14;
    bool dim = sM.waiting || sM.admissionHeld;

    UiG_Paint(UiM_Rounded4(SHW, SHH, 2, 3, 3, 3), SHX, SHY, f.base, &(struct UiPaint){UI_P_FIELD_INK, f.hi, f.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(iw, ih, 2, 2, 2, 2), ix, iy, UI_P_CREAM, &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    /* the host's status line as the sheet's header */
    UiText_Draw(UI_FONT_SMALL, ix + 10, iy + 4, sStatus, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiG_HLine(ix + 4, iy + 18, iw - 8, UI_P_CREAM_LO);
    for (int r = 0; r < MAX_ROWS; r++)
    {
        int ry = iy + 22 + r * ROW_H;
        bool there = r < sM.count, sel = there && r == sM.cursor && !dim, on = there && sM.enabled[r];

        if (!sel && r % 2 == 1)
            UiG_Rect(ix + 3, ry + 1, iw - 6, ROW_H - 1, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        if (!there)
            continue;
        if (sel)
            UiG_Paint(UiM_Rounded(iw - 14, ROW_H - 1, UiCornerR(2), UiCornerChamfer(6), UiCornerR(2), UiCornerR(2)), ix + 10, ry + 1,
                      UiMix(f.hi, UI_HEX(0xFFFFFF), 0.45), &(struct UiPaint){f.ink, UI_HEX(0xFFFFFF), f.hi, 0, 0, NULL, UI_NONE});
        if (on)
        {
            UiG_Paint(UiM_Circle(10), ix + 18, ry + 11, UiMix(f.base, UI_HEX(0xFFFFFF), sel ? 0.2 : 0.45),
                      &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            UiText_Draw(UI_FONT_NORMAL, ix + 36, ry + 7, sRows[r], dim ? UI_P_TEXT_MUTED : UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            if (sM.admission[r])
                Chip(ix + iw - 52, ry + 10, "BATTLE", UI_HEX(0xE85848), true);
            UiCaps_Draw(ix + iw - 12, ry + 12, "OPEN " UI_CAPS_RIGHT, sel ? f.ink : UiMix(UI_P_TEXT_MUTED, UI_P_CREAM, 0.3),
                        UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        }
        else
        {
            /* a row without an action: the host's information */
            UiG_Rect(ix + 20, ry + 14, 6, 2, UI_P_CREAM_LO);
            UiText_Draw(UI_FONT_NORMAL, ix + 36, ry + 7, sRows[r], UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        if (sel)
            UiW_Cursor(ix + 2, ry + 11, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ROW, r, ix + 3, ry, iw - 6, ROW_H);
    }
    /* the sheet's footer */
    {
        int fty = iy + 22 + MAX_ROWS * ROW_H + 4;
        bool lit = (t % POLL_FRAMES) < 8;

        UiG_HLine(ix + 4, fty, iw - 8, UI_P_CREAM_LO);
        UiG_Paint(UiM_Circle(7), ix + 8, fty + 5, lit ? UI_HEX(0x70E870) : UiMix(UI_HEX(0x308830), UI_P_INK, 0.5),
                  &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(ix + 18, fty + 5, "FROM THE HOST", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(ix + iw - 10, fty + 5, sM.back ? "B: BACK" : "B: CLOSE", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_RIGHT, UI_NONE);
    }
}

static void Message(int t)
{
    int b[4];
    const u8 *text = NULL;

    UiW_DialogBox(SHX, 292, SHW, 48, NULL, UI_NONE, b);
    if (sM.admissionHeld)
        text = sText_BattleHold;
    else if (sM.waiting)
        text = sText_Waiting;
    else if (sM.tradeMenu)
        text = sText_TradeHold;
    if (text != NULL)
    {
        UiG_Text(b[0] + 2, b[1] - 1, text);
        if (sM.waiting && (t / 16) % 2 == 0)
            UiW_DownArrow(b[0] + b[2] - 8, b[1] + 22, UI_P_CURSOR, false);
        return;
    }
    if (sM.cursor < sM.count)
    {
        UiG_Text(b[0] + 2, b[1] - 1, sRows[sM.cursor]);
        UiText_Draw(UI_FONT_SMALL, b[0] + 2, b[1] + 17, sStatus, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void Draw(int t)
{
    struct UiRamp f = UiW_FieldBand();
    struct UiRamp tab = {f.hi, f.lo, UI_P_FIELD_INK, UI_P_FIELD_INK};
    struct UiHintItem hints[2] = {{UI_ACT_CONFIRM, sText_Choose}, {UI_ACT_CANCEL, sM.back ? sText_Back : sText_Close}};

    Background(t, sM.tradeMenu ? UI_HEX(0x4C88E0) : f.lo);
    UiW_TitleTab(0, 8, "PLAYERS", &tab, 0, NULL);
    Device(t);
    Tabs();
    Sheet(t);
    Message(t);
    UiW_Hints(8, 347, hints, sM.tradeMenu && !sM.back ? 1 : 2, true, 10);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input: the engine's own buttons */

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        if (!NativeUi_PressesPending())
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || NativeUi_PressesPending())
        return button == 1;
    if (hit->id == HIT_ROW && hit->index < sM.count && !sM.waiting)
    {
        int n = sM.count, moves = hit->index - sM.cursor;

        if (moves > n / 2)
            moves -= n;
        else if (moves < -(n / 2))
            moves += n;
        if (moves != 0)
            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        if (sM.enabled[hit->index])
            NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return hit->id == HIT_TAB;
}

static bool Wheel(int dy)
{
    if (!sM.waiting && sM.count > 0 && !NativeUi_PressesPending())
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static bool Ready(void)
{
    return sReady;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Players = {
    .name = "players",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
