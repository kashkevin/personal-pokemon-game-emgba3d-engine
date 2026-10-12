#ifdef ENABLE_NATIVE_UI
/*
 * 7 · Trainer Card (the approved design's preview/js/screens/trainer_card.js):
 * the collectible card over the engine's own Trainer Card (trainer_card.c).
 *
 * Front: the engine's trainer picture over a tinted stage with your walking
 * field sprite (your saved look), name, ID No., money, Pokédex (caught, with
 * seen beside it), the save's play time with the engine's blinking colon, the
 * four profile words, the eight badges in their sockets (the engine's own badge
 * art) and the engine's four stars, which also pick the card colour (green,
 * bronze, copper, silver, gold). Back: the records the engine prints (Hall of
 * Fame debut, link battles, trades, Pokéblocks and contests with friends,
 * Battle Points won), only those it has.
 *
 * The design's other back pages (link battle ledger, Championship, trades and
 * checkpoint, hunts) are the host's: the game reads them in PLAYERS > Your
 * Trainer Card (the Players screen), so their tabs here say where they are;
 * pair friendship is PLANNED. The game shows one card: other players' cards open
 * from PLAYERS > View Trainer Card. The left panes show who is in this world
 * (from the bridge) and the engine's four star rules with the ones you have met.
 *
 * Keys stay the engine's: A flips the front, B closes it; on the back A closes
 * and B flips back. A click on the card flips it, a right click closes it. The
 * flip squashes the card in whole pixels over nine frames, as the design's.
 * Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "multiplayer.h"
#include "overworld.h"
#include "region_map.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_CARD = UI_HIT_SCREEN,
    HIT_TAB,
};

#define CX 228
#define CY 32
#define CW 404
#define CH 252
#define FLIP_STEPS 10        /* the squash of each flip frame; the other face from step 5 */
#define MSG_FRAMES 300       /* a tab's note stays 5 s */
#define TAB_COUNT 6
#define RECORD_ROWS 6

static const double sFlip[FLIP_STEPS] = {1, 0.9, 0.75, 0.45, 0.08, 0.08, 0.45, 0.75, 0.9, 1};
static const uint32_t sTierHex[5] = {0x48A878, 0xC08850, 0xD07058, 0xA0A8C0, 0xE0B838};
static const uint32_t sTierCool[5] = {0, 0x3C1E28, 0x461828, 0, 0x602C1E};
static const char *const sTierNames[5] = {"GREEN", "BRONZE", "COPPER", "SILVER", "GOLD"};
static const uint32_t sSlotHex[4] = {0x58B048, 0x4C88E0, 0xE85848, 0xE0B028};
static const char *const sSlotHeads[4] = {"head_green", "head_blue", "head_red", "head_yellow"};
static const char *const sStarRows[11] = {".....#.....", "....###....", "....###....", "...#####...", "###########",
                                          ".#########.", "..#######..", "...#####...", "..###.###..", "..##...##..",
                                          ".##.....##."};
static const char *const sLockRows[7] = {".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####"};
/* the back's pages: the engine's records here, the host's in PLAYERS, friendship planned */
static const char *const sTabLabels[TAB_COUNT] = {"RECORDS", "LINK", "CHAMP", "TRADES", "HUNTS", "FRIENDS"};
/* walking frames of a direction's cycle (step 0..3); right is left mirrored */

static const u8 sText_IdNo[] = _("IDNo. ");
static const u8 sText_Seen[] = _("seen ");
static const u8 sText_Flip[] = _("Flip");
static const u8 sText_Close[] = _("Close");
static const u8 sText_HereWithYou[] = _("Here with you");
static const u8 sText_Records[] = _("'s records");
static const u8 sRule_Hof[] = _("Entered the Hall of Fame");
static const u8 sRule_Dex[] = _("Caught every Hoenn Pokémon");
static const u8 sRule_Paintings[] = _("All five contest paintings");
static const u8 sRule_Frontier[] = _("Every Battle Frontier symbol");
static const u8 sNote_Stars[] = _("Engine rule: stars pick the card colour.");
static const u8 sNote_LinkStars[] = _("'s card reports its star count only.");
static const u8 sHelp_Front[] = _("Your Trainer Card. Flip it for the records this save keeps: Hall of Fame, link battles, trades and contests.");
static const u8 sHelp_Back[] = _("The records this save keeps, as the game prints them. Your world's battles, trades and hunts are kept by the host.");
static const u8 sHelp_LinkFront[] = _("A Trainer Card from the link: its owner's details when it was sent.");
static const u8 sHelp_LinkBack[] = _("The records this card brought over the link.");
static const u8 sHelp_Host[] = _("Kept by the host, not this card: open PLAYERS, then Your Trainer Card.");
static const u8 sHelp_Friends[] = _("Pair friendship is planned: levels, thresholds and Lucky odds are not chosen yet.");
static const u8 sFoot_Cards1[] = _("Other players' cards and your");
static const u8 sFoot_Cards2[] = _("world records: PLAYERS menu.");
static const u8 sNothing1[] = _("Nothing recorded yet.");
static const u8 sNothing2[] = _("The Hall of Fame, link battles, trades,");
static const u8 sNothing3[] = _("contests and the Battle Frontier write here.");
static const u8 sHost1[] = _("Your world's link battles, Championship, trades");
static const u8 sHost2[] = _("and hunts: PLAYERS > Your Trainer Card.");
static const u8 sText_Bp[] = _(" BP");
static const u8 sCase_Offline1[] = _("Not connected to a host: the players");
static const u8 sCase_Offline2[] = _("of your world show here when you are.");
static const u8 sCase_Alone[] = _("No one else is connected right now.");

struct Peer
{
    bool8 here;
    u8 where[24];
};

struct Record
{
    const char *label;
    u8 value[24];
    bool wl;              /* a wins / losses bar beside the value */
};

static struct NativeTrainerCard sCard;
static uint32_t sLastCapture;
static uint32_t sFlipStart;
static bool sFlipping;
static bool sFlipToBack;
static struct NativeFieldSprite sSprite; /* the player's own field sprite (the chosen character's) */
static bool sHaveSprite;
static bool sConnected;
static struct Peer sPeers[MULTIPLAYER_MAX_PEERS];
static int sPeerCount;
static struct Record sRecords[RECORD_ROWS];
static int sRecordCount;
static int sMsgTab;             /* the back tab whose note shows (0: none) */
static uint32_t sMsgUntil;

static bool Detect(void)
{
    struct NativeTrainerCard c;

    return TrainerCard_NativeUiState(&c);
}

static void Number(u8 *dst, int size, long value, bool thousands)
{
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, value, 1, thousands);
}

/* the engine's back, in its order: only the records it prints */
static void BuildRecords(void)
{
    struct Record *r;

    sRecordCount = 0;
    if (sCard.hasHofResult)
    {
        char t[24];

        r = &sRecords[sRecordCount++];
        r->label = "HALL OF FAME DEBUT";
        snprintf(t, sizeof(t), "%u:%02u:%02u", (unsigned)sCard.hofHours, (unsigned)sCard.hofMinutes,
                 (unsigned)sCard.hofSeconds);
        UiText_FromAscii(r->value, sizeof(r->value), t);
        r->wl = false;
    }
    if (sCard.hasLinkResults)
    {
        char t[24];

        r = &sRecords[sRecordCount++];
        r->label = "LINK BATTLES";
        snprintf(t, sizeof(t), "%uW %uL", (unsigned)sCard.linkWins, (unsigned)sCard.linkLosses);
        UiText_FromAscii(r->value, sizeof(r->value), t);
        r->wl = true;
    }
    if (sCard.hasTrades)
    {
        r = &sRecords[sRecordCount++];
        r->label = "POK" UI_CAPS_E_ACUTE "MON TRADES";
        Number(r->value, sizeof(r->value), sCard.trades, true);
        r->wl = false;
    }
    if (sCard.pokeblocksWithFriends != 0)
    {
        r = &sRecords[sRecordCount++];
        r->label = "POK" UI_CAPS_E_ACUTE "BLOCKS WITH FRIENDS";
        Number(r->value, sizeof(r->value), sCard.pokeblocksWithFriends, true);
        r->wl = false;
    }
    if (sCard.contestsWithFriends != 0)
    {
        r = &sRecords[sRecordCount++];
        r->label = "CONTESTS WON WITH FRIENDS";
        Number(r->value, sizeof(r->value), sCard.contestsWithFriends, true);
        r->wl = false;
    }
    if (sCard.frontierBP != 0)
    {
        r = &sRecords[sRecordCount++];
        r->label = "BATTLE POINTS WON";
        Number(r->value, sizeof(r->value), sCard.frontierBP, true);
        UiText_Append(r->value, sizeof(r->value), sText_Bp);
        r->wl = false;
    }
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    bool fresh = now - sLastCapture != 1;
    u8 was = fresh ? NATIVE_CARD_LOADING : sCard.phase;

    sLastCapture = now;
    if (!TrainerCard_NativeUiState(&sCard))
        return;
    sCard.name[sizeof(sCard.name) - 1] = EOS;
    for (int i = 0; i < 4; i++)
        sCard.profile[i][sizeof(sCard.profile[i]) - 1] = EOS;
    if (fresh)
    {
        sFlipping = false;
        sMsgTab = 0;
    }
    /* the engine began a flip: the design's nine-frame squash from now */
    if ((sCard.phase == NATIVE_CARD_TO_BACK || sCard.phase == NATIVE_CARD_TO_FRONT) && sCard.phase != was)
    {
        sFlipping = true;
        sFlipStart = now;
        sFlipToBack = sCard.phase == NATIVE_CARD_TO_BACK;
        sMsgTab = 0;
    }
    UiGfx_PrepareTrainerPic(sCard.picId);
    sHaveSprite = !sCard.isLink;
    if (sHaveSprite)
        MultiplayerAppearance_NativeUiLocalSprite(&sSprite);
    BuildRecords();
    sConnected = Multiplayer_IsConnected();
    sPeerCount = 0;
    for (u32 i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        struct Peer *p = &sPeers[sPeerCount];
        u32 id;
        u8 group, num, gender, activity;

        if (!Multiplayer_NativeUiPeer(i, &id, &group, &num, &gender, &activity))
            continue;
        p->here = activity != MULTIPLAYER_ACTIVITY_UNAVAILABLE && group == gSaveBlock1Ptr->location.mapGroup
               && num == gSaveBlock1Ptr->location.mapNum;
        GetMapName(p->where, Overworld_GetMapHeaderByGroupAndId(group, num)->regionMapSectionId, 0);
        sPeerCount++;
    }
}

/* ---------------------------------------------------------------- pieces */

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* the card colour of a star count (PK.player.tier) */
static struct UiRamp Tier(int stars)
{
    int k = Clamp(stars, 0, 4);

    return UiRampOf2(UI_HEX(sTierHex[k]), 0.42, 0.28, 0.62, UI_NONE, sTierCool[k] != 0 ? UI_HEX(sTierCool[k]) : UI_NONE);
}

static void Rule(int x, int y, int w, UiRGBA c)
{
    for (int i = 0; i < w; i += 3)
        UiG_Put(x + i, y, c);
}

static void Label(int x, int y, const char *s, UiRGBA c)
{
    UiCaps_Draw(x, y, s, c, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

/* 11x11 star: gold when earned, an empty socket in the surface ramp otherwise */
static void Star(int x, int y, bool on, const struct UiRamp *r)
{
    const struct UiMask *m = UiM_Rows(sStarRows, 11, NULL);

    if (on)
        UiG_Paint(m, x, y, UI_HEX(0xF8D040),
                  &(struct UiPaint){UI_HEX(0x6A4410), UI_HEX(0xFFF4B0), UI_HEX(0xD09820), 0, 0, NULL, UI_NONE});
    else
        UiG_Paint(m, x, y, UiMix(r->lo, r->ink, 0.3),
                  &(struct UiPaint){r->ink, UI_NONE, UiMix(r->lo, r->base, 0.4), 0, 0, NULL, UI_NONE});
}

static void Lock(int x, int y, UiRGBA c)
{
    UiG_Mask(UiM_Rows(sLockRows, 7, NULL), x, y, c);
}

/* the badges' own art, as the engine decompressed it: a 128x16 strip */
static void BuildBadges(struct UiBitmap *bm, const void *ctx)
{
    const struct NativeTrainerCard *c = ctx;

    UiGfx_Decode4bpp(bm->d, 128, c->badgeTiles, c->badgePalette, 128, 16);
}

static void BadgeRow(int x, int y, const struct UiRamp *r)
{
    const struct UiPiece *art = UiG_Cached(sCard.isHoenn ? "tcbadges:hoenn" : "tcbadges:kanto", 128, 16, BuildBadges, &sCard);

    for (int i = 0; i < 8; i++)
    {
        int bx = x + i * 40;

        UiG_Paint(UiM_Circle(24), bx, y, UiMix(r->lo, r->ink, 0.28),
                  &(struct UiPaint){r->ink, UiMix(r->lo, r->ink, 0.5), UiMix(r->base, r->hi, 0.5), 0, 0, NULL, UI_NONE});
        if (sCard.badges[i] && art != NULL)
            UiG_BlitPart(art, i * 16, 0, 16, 16, bx + 4, y + 4, 1, false, 1.0);
        else
            UiG_Put(bx + 11, y + 11, UiMix(r->lo, r->ink, 0.6));
    }
}

static void WlBar(int x, int y, int w, int wins, int losses)
{
    int total = wins + losses, inner = w - 2, ww;

    UiG_Paint(UiM_Rounded4(w, 7, 1, 1, 1, 1), x, y, UI_P_HP_TRACK, &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    if (total <= 0)
        return;
    ww = (int)floor(inner * (double)wins / total + 0.5);
    if (ww > 0)
    {
        UiG_Rect(x + 1, y + 1, ww, 5, UI_P_HP_GREEN_1);
        UiG_HLine(x + 1, y + 1, ww, UI_P_HP_GREEN_0);
        UiG_HLine(x + 1, y + 5, ww, UI_P_HP_GREEN_2);
    }
    if (inner - ww > 0)
    {
        UiG_Rect(x + 1 + ww, y + 1, inner - ww, 5, UI_P_HP_RED_1);
        UiG_HLine(x + 1 + ww, y + 1, inner - ww, UI_P_HP_RED_0);
        UiG_HLine(x + 1 + ww, y + 5, inner - ww, UI_P_HP_RED_2);
    }
}

/* the foil frame; the holo glint sweeps the border every 4 s (the paper covers the rest) */
static void Frame(int x, int y, const struct UiRamp *r, int t, bool shine)
{
    struct UiPattern diag = UiPat_Diag(14, 3, 0, -1);

    UiG_Paint(UiM_Rounded4(CW, CH, 5, 5, 5, 5), x, y, r->base,
              &(struct UiPaint){r->ink, r->hi, r->lo, 2, 2, &diag, UiMix(r->base, r->hi, 0.35)});
    if (shine && t % 240 < 110)
    {
        int gx = (t % 240) * 5 - 60;
        UiRGBA gc = UiMix(r->hi, UI_HEX(0xFFFFFF), 0.45);

        for (int yy = 3; yy < CH - 3; yy++)
        {
            int x0 = gx - (yy >> 1), x1 = x0 + 8;

            if (x0 < 3)
                x0 = 3;
            if (x1 > CW - 3)
                x1 = CW - 3;
            if (x1 > x0)
                UiG_Rect(x + x0, y + yy, x1 - x0, 1, gc);
        }
    }
}

/* a front line's caps label and dotted rule (its value is drawn right-aligned beside) */
static int Line(int tx, int ry, int ruleW, const char *label, UiRGBA lab)
{
    Label(tx, ry + 5, label, lab);
    Rule(tx, ry + 18, ruleW, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3));
    return ry + 22;
}

/* ---------------------------------------------------------------- the front */

static void Front(int x, int y, int t, bool live, const struct UiRamp *r)
{
    UiRGBA white = UI_HEX(0xFFFFFF), lab = UiMix(r->lo, r->ink, 0.25);
    int px = x + 6, py = y + 28, pw = CW - 12, ph = 184;
    int wx = px + 8, wy = py + 8, ww = 84, wh = 108, fy = wy + wh - 30;
    int tx = px + 104, right = px + pw - 10, ruleW = pw - 112, ry, nx, by, earned = 0;
    char caps[24];
    u8 buf[32];

    Frame(x, y, r, t, live);
    /* header band: the title, the colour's name and the engine's stars */
    UiG_Paint(UiM_Rounded4(CW - 12, 20, 3, 3, 0, 0), x + 6, y + 6, r->lo,
              &(struct UiPaint){UI_NONE, UiMix(r->lo, r->base, 0.6), UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 14, y + 12, "TRAINER CARD", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%s CARD", sTierNames[Clamp(sCard.stars, 0, 4)]);
    UiCaps_Draw(x + CW - 76 - UiCaps_Width(caps), y + 12, caps, UiMix(r->hi, white, 0.4), UI_NONE, UI_CAPS_PLAIN,
                UI_ALIGN_LEFT, UI_NONE);
    for (int s = 0; s < 4; s++)
        Star(x + CW - 64 + s * 13, y + 10, s < sCard.stars, r);

    /* paper */
    UiG_Paint(UiM_Rounded4(pw, ph, 0, 0, 3, 3), px, py, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});

    /* portrait window: the engine's picture over a tinted stage, you walking on a floor strip */
    {
        struct UiPattern diag = UiPat_Diag(12, 5, 0, 1);

        UiG_Paint(UiM_Rounded4(ww, wh, 2, 2, 2, 2), wx, wy, UiMix(r->hi, white, 0.55),
                  &(struct UiPaint){r->ink, UI_NONE, UI_NONE, 0, 0, &diag, UiMix(r->hi, white, 0.25)});
    }
    UiGfx_TrainerPic(sCard.picId, wx + 10, wy + 6, NULL);
    UiG_Rect(wx + 1, fy, ww - 2, 29, UiMix(r->lo, white, 0.35));
    for (int k = 0; k < ww - 2; k += 8)
        UiG_Rect(wx + 1 + k, fy + 1 + ((k / 8) % 2) * 14, ww - 2 - k < 8 ? ww - 2 - k : 8, 14, UiMix(r->lo, white, 0.5));
    UiG_HLine(wx + 1, fy, ww - 2, r->lo);
    if (sHaveSprite)
    {
        /* a step every 8 frames, a turn every 96: down, left, up, right */
        int step = (t / 8) % 4, di = (t / 96) % 4;

        UiGfx_NativeFieldSprite(&sSprite, 0, di, step, wx + (ww >> 1) - 8, fy - 5, 1);
    }

    /* name row */
    nx = UiG_Text(tx, py + 6, sCard.name);
    UiW_Gender(nx + 1, py + 6, sCard.gender == MALE ? 0 : 1);
    {
        char id[8];

        snprintf(id, sizeof(id), "%05u", (unsigned)sCard.trainerId);
        memcpy(buf, sText_IdNo, sizeof(sText_IdNo));
        UiText_AppendAscii(buf, sizeof(buf), id);
        UiG_TextEx(right, py + 6, buf, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
    }
    Rule(tx, py + 25, ruleW, UI_P_CREAM_LO);
    ry = py + 30;

    /* money, Pokédex, time: what the engine prints */
    UiText_FromAscii(buf, sizeof(buf), "$");
    UiText_AppendNumber(buf, sizeof(buf), (long)sCard.money, 1, true);
    UiG_TextEx(right, ry, buf, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
    ry = Line(tx, ry, ruleW, "MONEY", lab);
    if (sCard.hasPokedex)
    {
        int vx = right;

        if (!sCard.isLink)
        {
            memcpy(buf, sText_Seen, sizeof(sText_Seen));
            UiText_AppendNumber(buf, sizeof(buf), sCard.seen, 1, false);
            UiText_Draw(UI_FONT_SMALL, vx, ry + 2, buf, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            vx -= UiText_Width(UI_FONT_SMALL, buf, 0) + 8;
        }
        Number(buf, sizeof(buf), sCard.caught, false);
        UiG_TextEx(vx, ry, buf, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        ry = Line(tx, ry, ruleW, "POK" UI_CAPS_E_ACUTE "DEX", lab);
    }
    {
        /* h:mm, the colon blinking with the engine's (own card) */
        u8 hours[8], minutes[8], colon[2] = {CHAR_COLON, EOS};
        int mw, cw;

        Number(hours, sizeof(hours), sCard.playHours, false);
        minutes[0] = EOS;
        UiText_AppendNumber(minutes, sizeof(minutes), sCard.playMinutes, 2, false);
        mw = UiText_Width(UI_FONT_NORMAL, minutes, 0);
        cw = UiText_Width(UI_FONT_NORMAL, colon, 0);
        UiG_TextEx(right, ry, minutes, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        if (!sCard.colonHidden)
            UiG_TextEx(right - mw, ry, colon, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        UiG_TextEx(right - mw - cw, ry, hours, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        ry = Line(tx, ry, ruleW, "TIME", lab);
    }
    /* the four profile words (the engine's easy chat profile) */
    Label(tx, ry + 5, "PROFILE", lab);
    for (int l = 0; l < 2; l++)
    {
        int wx2 = UiG_Text(tx, ry + 13 + l * 16, sCard.profile[l * 2]);

        UiG_Text(wx2 + 6, ry + 13 + l * 16, sCard.profile[l * 2 + 1]);
    }

    /* badge case: the engine's badge art in sockets (a link card shows none, as the engine) */
    by = y + CH - 34;
    for (int i = 0; i < 8; i++)
        earned += sCard.badges[i] ? 1 : 0;
    UiCaps_Draw(x + 14, by + 8, "BADGES", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    if (sCard.isLink)
    {
        UiCaps_Draw(x + 70, by + 11, "NOT ON A LINK CARD", UiMix(r->hi, white, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT,
                    UI_NONE);
    }
    else
    {
        snprintf(caps, sizeof(caps), "%d/8", earned);
        UiCaps_Draw(x + 14, by + 18, caps, UiMix(r->hi, white, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        BadgeRow(x + 70, by + 3, r);
    }
    /* a tiny twinkle on the gold card's stars */
    if (sCard.stars >= 4 && live && (t / 20) % 3 == 0)
        UiG_Put(x + CW - 59, y + 12, white);
}

/* ---------------------------------------------------------------- the back */

static void Back(int x, int y, int t, bool live, const struct UiRamp *r)
{
    UiRGBA white = UI_HEX(0xFFFFFF), lab = UiMix(r->lo, r->ink, 0.25);
    int px = x + 6, py = y + 27, pw = CW - 12, ph = 190;
    int cx = px + 10, cy = py + 8, cw = pw - 20, tx = x + 8, firstW = 0;
    u8 buf[48];

    Frame(x, y, r, t, live);
    /* page tabs along the top edge: the engine's records joins the paper */
    for (int i = 0; i < TAB_COUNT; i++)
    {
        bool on = i == 0, host = i >= 1 && i <= 4;
        int w = UiCaps_Width(sTabLabels[i]) + 16 + (host ? 8 : 0);

        if (on)
        {
            UiG_Paint(UiM_Rounded4(w, 22, 3, 3, 0, 0), tx, y + 6, UI_P_CREAM,
                      &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_NONE, 0, 0, NULL, UI_NONE});
            UiCaps_Draw(tx + 8, y + 13, sTabLabels[i], r->ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            firstW = w;
        }
        else
        {
            UiRGBA dk = UiMix(r->lo, r->base, 0.3);

            UiG_Paint(UiM_Rounded4(w, 18, 3, 3, 0, 0), tx, y + 10, dk,
                      &(struct UiPaint){r->ink, UiMix(r->base, r->hi, 0.5), UI_NONE, 0, 0, NULL, UI_NONE});
            UiCaps_Draw(tx + 8, y + 15, sTabLabels[i], UiMix(UI_P_TEXT_W, dk, 0.3), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT,
                        UI_NONE);
            if (host)
                Lock(tx + 8 + UiCaps_Width(sTabLabels[i]) + 3, y + 14, UiMix(UI_P_TEXT_W, dk, 0.3));
            if (live)
                UiG_Hit(HIT_TAB, i, tx, y + 6, w, 22);
        }
        tx += w + 3;
    }
    UiG_Paint(UiM_Rounded4(pw, ph, 0, 3, 3, 3), px, py, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Rect(x + 9, py, firstW - 2, 1, UI_P_CREAM);

    /* whose records */
    memcpy(buf, sCard.name, sizeof(sCard.name));
    buf[sizeof(sCard.name)] = EOS;
    UiText_Append(buf, sizeof(buf), sText_Records);
    UiG_Text(cx, cy - 2, buf);
    UiCaps_Draw(cx + cw, cy + 3, "AS THE GAME PRINTS THEM", lab, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    Rule(cx, cy + 16, cw, UI_P_CREAM_LO);

    if (sRecordCount == 0)
    {
        UiG_Paint(UiM_Rounded4(cw, 58, 2, 2, 2, 2), cx, cy + 26, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.5),
                  &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiG_Text(cx + 10, cy + 31, sNothing1);
        UiText_Draw(UI_FONT_SMALL, cx + 10, cy + 49, sNothing2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_SMALL, cx + 10, cy + 62, sNothing3, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    for (int i = 0; i < sRecordCount; i++)
    {
        int yy = cy + 22 + i * 20;

        if (i % 2 == 1)
            UiG_Rect(cx - 4, yy - 1, cw + 8, 18, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        Rule(cx - 2, yy + 17, cw + 4, UiMix(UI_P_CREAM_LO, r->lo, 0.3));
        Label(cx + 4, yy + 4, sRecords[i].label, lab);
        UiG_TextEx(cx + cw - 4, yy - 1, sRecords[i].value, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        if (sRecords[i].wl)
            WlBar(cx + 132, yy + 4, cw - 136 - UiText_Width(UI_FONT_NORMAL, sRecords[i].value, 0) - 14, sCard.linkWins,
                  sCard.linkLosses);
    }

    /* the host's records: where they are */
    Lock(cx, py + ph - 30, UI_P_TEXT_MUTED);
    UiText_Draw(UI_FONT_SMALL, cx + 10, py + ph - 33, sHost1, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, cx + 10, py + ph - 20, sHost2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);

    /* footer on the foil */
    UiCaps_Draw(x + 14, y + CH - 16, sCard.isLink ? "FROM THE LINK" : "SAVED WITH YOUR GAME", UI_P_TEXT_W, r->ink,
                UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(x + CW - 14, y + CH - 16, sCard.isHoenn ? "GAME CARD - HOENN" : "GAME CARD - KANTO", UiMix(r->hi, white, 0.4),
                UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
}

/* ---------------------------------------------------------------- the card, flipping */

/* the face shown when not squashing: the engine's, or where its flip is going */
static bool FaceBack(void)
{
    if (sCard.phase == NATIVE_CARD_TO_BACK)
        return true;
    if (sCard.phase == NATIVE_CARD_TO_FRONT)
        return false;
    return sCard.onBack;
}

static void Card(int t, const struct UiRamp *r)
{
    uint32_t e = NativeUi_Frame() - sFlipStart;

    if (sFlipping && e < FLIP_STEPS - 1)
    {
        int step = (int)e + 1, mark = UiG_Mark(), half = (int)floor(CW / 2 * sFlip[step] + 0.5);
        bool back = step >= 5 ? sFlipToBack : !sFlipToBack;

        if (half < 1)
            half = 1;
        if (back)
            Back(CX, CY, t, false, r);
        else
            Front(CX, CY, t, false, r);
        UiG_SquashX(mark, CX + CW / 2, sFlip[step]);
        UiG_VLine(CX + CW / 2 - half, CY + 4, CH - 8, r->ink);
        UiG_VLine(CX + CW / 2 + half - 1, CY + 4, CH - 8, r->ink);
        return;
    }
    if (FaceBack())
        Back(CX, CY, t, true, r);
    else
        Front(CX, CY, t, true, r);
    UiG_Hit(HIT_CARD, 0, CX, CY + 28, CW, CH - 28);
}

/* ---------------------------------------------------------------- left panes */

static void CasePane(const struct UiRamp *r)
{
    UiRGBA white = UI_HEX(0xFFFFFF);
    int x = 8, y = 32, w = 212, h = 112, rows = sCard.isLink ? 1 : 1 + sPeerCount;
    char caps[24];

    UiG_Paint(UiM_Rounded(w, h, UiCornerR(4), UiCornerChamfer(8), UiCornerR(4), UiCornerR(4)), x, y, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded(w - 6, 16, UiCornerR(2), UiCornerChamfer(6), UiCornerR(0), UiCornerR(0)), x + 3, y + 3, r->lo,
              &(struct UiPaint){UI_NONE, r->base, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 9, y + 8, "CARDS", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    if (sCard.isLink)
        snprintf(caps, sizeof(caps), "LINK");
    else if (sConnected)
        snprintf(caps, sizeof(caps), "%d/%d IN THIS WORLD", sPeerCount + 1, MULTIPLAYER_MAX_PEERS + 1);
    else
        snprintf(caps, sizeof(caps), "OFFLINE");
    UiCaps_Draw(x + w - 14, y + 8, caps, UiMix(r->hi, white, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    for (int i = 0; i < rows && i < 4; i++)
    {
        int ry = y + 22 + i * 22;

        if (i == 0)
            UiG_Paint(UiM_Rounded4(w - 10, 20, 2, 2, 2, 2), x + 5, ry, UiMix(r->hi, white, 0.55),
                      &(struct UiPaint){UiMix(r->lo, r->ink, 0.2), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        else if (i % 2)
            UiG_Rect(x + 5, ry + 1, w - 10, 18, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.35));
        UiGfx_Icon(sSlotHeads[i], x + 14, ry + 4, 0);
        if (i == 0)
        {
            UiG_Text(x + 31, ry + 1, sCard.name);
            UiCaps_Draw(x + w - 12, ry + 6, sCard.isLink ? "THIS CARD" : "YOU", sCard.isLink ? UI_P_TEXT_MUTED : UI_HEX(0x48B070),
                        UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        }
        else
        {
            const struct Peer *p = &sPeers[i - 1];

            UiG_Text(x + 31, ry + 1, p->here ? sText_HereWithYou : p->where);
            UiCaps_Draw(x + w - 12, ry + 6, "ONLINE", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        }
    }
    if (sCard.isLink)
        return;
    if (!sConnected)
    {
        UiText_Draw(UI_FONT_SMALL, x + 10, y + 50, sCase_Offline1, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_SMALL, x + 10, y + 63, sCase_Offline2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else if (sPeerCount == 0)
    {
        UiText_Draw(UI_FONT_SMALL, x + 10, y + 50, sCase_Alone, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void StarPane(const struct UiRamp *r)
{
    static const u8 *const rules[4] = {sRule_Hof, sRule_Dex, sRule_Paintings, sRule_Frontier};
    UiRGBA white = UI_HEX(0xFFFFFF);
    bool own = !sCard.isLink;
    bool facts[4] = {sCard.hasHofResult, sCard.caughtAllRegional, sCard.hasAllPaintings, sCard.hasAllFrontierSymbols};
    int x = 8, y = 152, w = 212, h = 132;
    char caps[32];
    u8 note[64];

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 6, 16, 2, 2, 0, 0), x + 3, y + 3, r->lo, &(struct UiPaint){UI_NONE, r->base, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 9, y + 8, "STARS", UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%d/4 - %s CARD", Clamp(sCard.stars, 0, 4), sTierNames[Clamp(sCard.stars, 0, 4)]);
    UiCaps_Draw(x + w - 12, y + 8, caps, UiMix(r->hi, white, 0.4), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    for (int i = 0; i < 4; i++)
    {
        int ry = y + 24 + i * 19;
        bool got = own && facts[i];

        if (got)
            Star(x + 8, ry + 3, true, r);
        else
            UiG_Paint(UiM_Rows(sStarRows, 11, NULL), x + 8, ry + 3, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3),
                      &(struct UiPaint){UiMix(UI_P_CREAM_LO, UI_P_TEXT_MUTED, 0.5), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NARROW, x + 24, ry, rules[i], own && !got ? UI_P_TEXT_MUTED : UI_P_TEXT, UI_P_TEXT_SH,
                    UI_ALIGN_LEFT, 0);
    }
    if (own)
    {
        UiText_Draw(UI_FONT_SMALL, x + 8, y + h - 18, sNote_Stars, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else
    {
        memcpy(note, sCard.name, sizeof(sCard.name));
        note[sizeof(sCard.name)] = EOS;
        UiText_Append(note, sizeof(note), sNote_LinkStars);
        UiText_Draw(UI_FONT_SMALL, x + 8, y + h - 18, note, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

/* ---------------------------------------------------------------- help and prompts */

static void Help(bool back)
{
    const u8 *text, *starts[4];
    int lengths[4], b[4], n;

    if (sMsgTab != 0 && NativeUi_Frame() < sMsgUntil && back)
        text = sMsgTab == TAB_COUNT - 1 ? sHelp_Friends : sHelp_Host;
    else if (sCard.isLink)
        text = back ? sHelp_LinkBack : sHelp_LinkFront;
    else
        text = back ? sHelp_Back : sHelp_Front;
    UiW_DialogBox(8, 292, 392, 60, NULL, UI_NONE, b);
    n = UiText_Wrap(text, b[2] - 4, UI_FONT_NORMAL, starts, lengths, 4);
    for (int i = 0; i < n && i < 3; i++)
    {
        u8 line[128];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiG_Text(b[0] + 2, b[1] - 1 + i * 15, line);
    }
}

static void Prompts(bool back, const struct UiRamp *r)
{
    int hx;

    UiG_Paint(UiM_Rounded4(224, 60, 3, 0, 3, 0), 416, 292, UI_P_CREAM,
              &(struct UiPaint){r->ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    /* the engine's own buttons: on the back A closes and B flips back (B closes while linked) */
    hx = UiW_Hint(424, 302, UI_ACT_CONFIRM, back ? sText_Close : sText_Flip, false) + 10;
    UiW_Hint(hx, 302, UI_ACT_CANCEL, back && !sCard.linked ? sText_Flip : sText_Close, false);
    UiText_Draw(UI_FONT_SMALL, 424, 318, sFoot_Cards1, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, 424, 331, sFoot_Cards2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

/* ---------------------------------------------------------------- the screen */

static void Draw(int t)
{
    struct UiRamp r = Tier(sCard.stars);
    struct UiRamp tab = {r.hi, r.lo, UiMix(r.lo, r.ink, 0.5), r.ink};
    bool back = FaceBack();
    int bx, by, bw, bh;

    /* the card case lining takes the card's colour, with a slow diamond weave */
    {
        struct UiPattern a = UiPat_Diag(24, 4, 0, 1), b = UiPat_Diag(24, 4, 0, -1);
        UiRGBA bg2 = UiMix(r.lo, r.ink, 0.3);

        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Rect(bx, by, bw, bh, UiMix(r.lo, r.ink, 0.45));
        UiG_FillPattern(&a, bx, by, bw, bh, bg2, t / 5, 0);
        UiG_FillPattern(&b, bx, by, bw, bh, bg2, t / 5, 0);
    }
    UiW_TitleTab(0, 8, "TRAINER CARD", &tab, 0, NULL);
    Card(t, &r);
    CasePane(&r);
    StarPane(&r);
    Help(back);
    Prompts(back, &r);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sCard.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    bool back = sCard.phase == NATIVE_CARD_BACK;

    if (hit->id == HIT_TAB && button == 1)
    {
        /* the host's pages and the planned one: say where they are */
        sMsgTab = hit->index;
        sMsgUntil = NativeUi_Frame() + MSG_FRAMES;
        return true;
    }
    if (!sCard.ready || NativeUi_PressesPending())
        return button == 3 || hit->id >= UI_HIT_SCREEN;
    if (button == 3)
    {
        /* close: B on the front, A on the back */
        NativeUi_QueuePress(back ? A_BUTTON : B_BUTTON);
        return true;
    }
    if (hit->id == HIT_CARD && button == 1)
    {
        /* flip: A on the front, B on the back (which closes instead while linked) */
        if (!back)
            NativeUi_QueuePress(A_BUTTON);
        else if (!sCard.linked)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_TrainerCard = {
    .name = "trainer_card",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
