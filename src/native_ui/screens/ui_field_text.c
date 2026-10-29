#ifdef ENABLE_NATIVE_UI
/*
 * The field's messages (not one of the 31 designed screens; the design's own
 * pieces): the field's message box - a person's line, a sign's, the name box's
 * speaker - with the script's YES / NO and the multichoices whose rows the
 * engine lists, in the design's message box (W.dialogBox, the Emerald message
 * box; a sign's in the design's wood) and menu box over the field, where the
 * pause menu keeps its help box. The 3D view shows the game's own message box
 * only while a line types (docs U12); this keeps every line up until the
 * script closes it. The engine keeps every line, rule and button: clicks and
 * the wheel queue its presses. The money box and the coin box a script or a
 * game shows with its question (a vending machine, the Safari Zone's fee, a
 * roulette table's "Do you want to play?") are the design's money windows,
 * with the wallet as it is now. The picture box a script shows (showmonpic: a
 * Pokémon in a frame, Birch's Johto starters) is the design's box where the
 * script put it, with the Pokémon's own picture; the Lilycove elevator's "Now
 * on" window beside its floors; the records a script shows and waits on (the
 * Pokémon Center 2F cable box's battle results, the Contest Lobby's Berry
 * Blender record, the Battle Frontier's results and Ranking Hall boards) in
 * the design's box. Anything else on the field's
 * window layer that this does not draw (a list, a PokéNav call, rows a script
 * builds itself) keeps the game's picture. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "coins.h"
#include "field_name_box.h"
#include "main.h"
#include "match_call.h"
#include "menu.h"
#include "money.h"
#include "overworld.h"
#include "palette.h"
#include "roulette.h"
#include "script.h"
#include "string_util.h"
#include "window.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_MESSAGE = UI_HIT_SCREEN,
};

static const u8 sText_Next[] = _("Next");
static const u8 sText_OK[] = _("OK");
static const u8 sText_No[] = _("No");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Win[] = _("WIN");
static const u8 sText_Lose[] = _("LOSE");
static const u8 sText_Draw[] = _("DRAW");

/* the design's wood (pause.js's location sign) for a sign's lines */
static const struct UiRamp sWood = {UI_HEX(0xECC08A), UI_HEX(0xD29C62), UI_HEX(0xA8703C), UI_HEX(0x4A2A12)};

static struct NativeScriptMenu sMenu;
static u8 sMsg[UI_MESSAGE_COPY];
static u32 sMsgPrinted;
static bool sHasMsg;
static bool sMsgPrinting;
static bool sWaiting;
static bool sSign;
static u8 sSpeaker[40];
static bool sMoneyBox, sCoinBox;
static u32 sMoney, sCoins;
static bool sMsgUp;
static struct NativeScriptPic sPic;
static bool sPicUp;
static bool sElevatorUp;
static const u8 *sElevatorLines[2];
static bool sResultsUp, sBlenderUp, sBoardUp;
static u8 sBoardWindow, sBoardW, sBoardH;
static struct NativeLinkResults sResults;
static struct NativeBlenderRecords sBlender;
static uint32_t sPicAt, sPicLast;

/* the picture box's Pokémon as CreateMonSprite_PicBox makes it: not shiny, this personality */
#define PIC_PERSONALITY 0x8000

/* ---------------------------------------------------------------- shared with the PC's first menus */

void UiField_MessageBox(int t, int L, int R, int B, const u8 *msg, int printed, bool waiting,
                        const struct UiRamp *band, const struct UiHintItem *hints, int hintCount, UiRGBA panelInk,
                        int hitId)
{
    int b[4];

    UiW_DialogBox(L + 8, B - UI_FIELD_MSG_H - 8, UI_FIELD_MSG_W, UI_FIELD_MSG_H, band, UI_NONE, b);
    if (msg != NULL)
        UiW_MessagePageFlipDraw(msg, printed, printed >= 0 && !waiting, b[0] + 2, b[1] - 1, b[2] - 4, 2, 16);
    if (waiting)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 20 + ((t / 16) % 2), UI_P_CURSOR, false);
    if (hitId != 0)
        UiG_Hit(hitId, 0, L + 8, B - UI_FIELD_MSG_H - 8, UI_FIELD_MSG_W, UI_FIELD_MSG_H);
    if (hintCount > 0)
    {
        int y = B - UI_FIELD_MSG_H - 8;

        UiG_Paint(UiM_Rounded4(160, UI_FIELD_MSG_H, 3, 0, 3, 0), R - 160, y, UI_P_CREAM,
                  &(struct UiPaint){panelInk, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
        UiW_Hints(R - 152, y + 8, hints, hintCount > 2 ? 2 : hintCount, false, 8);
        if (hintCount > 2)
            UiW_Hints(R - 152, y + 26, hints + 2, hintCount - 2, false, 8);
    }
}

/* ---------------------------------------------------------------- detection */

/* the records window a script shows (the cable box's results, the Berry Blender's record), else WINDOW_NONE */
static u8 RecordsWindow(void)
{
    struct NativeLinkResults r;
    struct NativeBlenderRecords b;

    if (BattleRecords_NativeUiLinkResults(&r))
        return r.windowId;
    u8 id, w, h;

    if (BerryBlender_NativeUiRecords(&b))
        return b.windowId;
    if (FrontierUtil_NativeUiRecordsWindow(&id, &w, &h))
        return id;
    return WINDOW_NONE;
}

/* the field's windows up besides the message box (the map's name and the name box apart, and the money and coin boxes,
 * the picture box, the elevator's floor and the records, which this draws) */
static int OtherWindows(u8 picture)
{
    u8 popup = GetMapNamePopUpWindowId();
    u32 namebox = GetNameboxWindowId();
    u8 money = Money_NativeUiBoxWindow(), coins = Coins_NativeUiBoxWindow(), elevator = WINDOW_NONE;
    u8 records = RecordsWindow();
    const u8 *lines[2];
    int n = 0;

    FieldSpecials_NativeUiElevatorFloor(&elevator, &lines[0], &lines[1]);
    for (u32 i = 1; i < WINDOWS_MAX; i++)
        if (i != popup && i != namebox && i != money && i != coins && i != picture && i != elevator && i != records
         && Window_NativeUiShown(i))
            n++;
    return n;
}

/* the script's menu up now; else the shared YES / NO a game's own question uses (DoYesNoFuncWithChoice: a roulette
 * table's "Do you want to play?", an item's "Use it?"), which takes A, B and the cursor as a script's does */
static void MenuState(struct NativeScriptMenu *m)
{
    if (ScriptMenu_NativeUiState(m) || !MenuHelpers_NativeUiYesNo())
        return;
    m->kind = NATIVE_SCRIPT_MENU_YES_NO;
    m->ready = !gPaletteFade.active;
    m->cursor = Menu_GetCursorPos();
    m->count = 2;
}

static bool Detect(void)
{
    struct NativeScriptMenu m;
    struct NativeScriptPic pic;
    bool picUp;

    if (gMain.callback2 != CB2_Overworld || IsMatchCallTaskActive())
        return false;
    /* a message, or a picture box alone (before its question, after it), or the records a script waits on */
    picUp = ScriptMenu_NativeUiPokemonPic(&pic);
    if (!Window_NativeUiShown(0) && !picUp && RecordsWindow() == WINDOW_NONE)
        return false;
    MenuState(&m);
    /* rows a script builds itself are not listed: the game's picture */
    if (m.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && m.count == 0)
        return false;
    /* the menu's own window, nothing else */
    return OtherWindows(picUp ? pic.windowId : WINDOW_NONE) <= (m.kind != NATIVE_SCRIPT_MENU_NONE ? 1 : 0);
}

static void Capture(void)
{
    bool8 active = FALSE;

    MenuState(&sMenu);
    sResultsUp = BattleRecords_NativeUiLinkResults(&sResults);
    sBlenderUp = !sResultsUp && BerryBlender_NativeUiRecords(&sBlender);
    sBoardUp = !sResultsUp && !sBlenderUp && FrontierUtil_NativeUiRecordsWindow(&sBoardWindow, &sBoardW, &sBoardH);
    /* a records window over the message window's corner leaves its own tiles there: then the message window is up
     * only while its own tiles are */
    sMsgUp = RecordsWindow() != WINDOW_NONE ? Window_NativeUiOnScreen(0) : Window_NativeUiShown(0);
    sHasMsg = sMsgUp && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = sMenu.kind == NATIVE_SCRIPT_MENU_NONE
            && (Script_NativeUiWaitsForButton() || TextPrinter_NativeUiWindowWaiting(0) || Roulette_NativeUiEntryWaits());
    sSign = gMsgIsSignPost;
    sSpeaker[0] = EOS;
    if (GetNameboxWindowId() != WINDOW_NONE && gSpeakerName != NULL)
    {
        u8 name[64];

        StringExpandPlaceholders(name, gSpeakerName);
        StringCopyN(sSpeaker, name, sizeof(sSpeaker) - 1);
        sSpeaker[sizeof(sSpeaker) - 1] = EOS;
    }
    /* the wallet as it is now (the game's boxes show it as last printed) */
    sMoneyBox = Money_NativeUiBoxWindow() != WINDOW_NONE;
    sCoinBox = Coins_NativeUiBoxWindow() != WINDOW_NONE;
    sMoney = GetMoney(&gSaveBlock1Ptr->money);
    sCoins = GetCoins();
    {
        u8 window;

        sElevatorUp = FieldSpecials_NativeUiElevatorFloor(&window, &sElevatorLines[0], &sElevatorLines[1]);
    }
    sPicUp = ScriptMenu_NativeUiPokemonPic(&sPic);
    if (sPicUp)
    {
        uint32_t now = NativeUi_Frame();

        /* a box just shown: the Pokémon's own animation from its start */
        if (now - sPicLast > 2)
            sPicAt = now;
        sPicLast = now;
        UiGfx_PrepareMonPic(sPic.species, PIC_PERSONALITY, false, false);
    }
}

/* ---------------------------------------------------------------- drawing */

static bool MenuUp(void)
{
    return sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO || (sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sMenu.count > 0);
}

/* the picture box: the design's box where the script put the game's (its middle and top on the GBA's screen; 8 x 8
 * tiles in a frame a tile wide), above the message box, rising as it opens; the Pokémon twice the game's size */
static void PictureBox(int bx, int by, int bw, int bh)
{
    static const u8 sRise[] = {16, 9, 4, 1};
    int w = 156, h = 156, f = (int)(NativeUi_Frame() - sPicAt);
    int x = bx + (sPic.left * 8 + 32) * bw / DISPLAY_WIDTH - (w >> 1), y = by + (sPic.top * 8 - 8) * bh / DISPLAY_HEIGHT;
    int top = by + 8, bottom = by + bh - UI_FIELD_MSG_H - 16 - h;
    u16 sp = sPic.species;

    if (x > bx + bw - 8 - w)
        x = bx + bw - 8 - w;
    if (x < bx + 8)
        x = bx + 8;
    if (y > bottom)
        y = bottom;
    if (y < top)
        y = top;
    if (f >= 0 && f < (int)ARRAY_COUNT(sRise))
        y += sRise[f];
    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    UiG_Mask(UiM_Ellipse(76, 12), x + (w >> 1) - 38, y + h - 26, UI_P_PAPER_LO);
    UiG_Save();
    UiG_Clip(x + 4, y + 4, w - 8, h - 8);
    UiGfx_MonPicEx(sp, PIC_PERSONALITY, false, false, UiGfx_MonPicFrameAt(sp, PIC_PERSONALITY, false, false, f),
                   x + (w >> 1), y + h - 20, 2, 1.0, NULL);
    UiG_Restore();
}

/* an engine string's lines (the game's newlines), each centred on cx */
static void CentredLines(int cx, int y, const u8 *s, int lineH)
{
    while (s != NULL && *s != EOS)
    {
        UiText_Draw(UI_FONT_NORMAL, cx, y, s, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        while (*s != EOS && *s != CHAR_NEWLINE)
            s++;
        if (*s == CHAR_NEWLINE)
            s++;
        y += lineH;
    }
}

static int Number(int xRight, int y, u32 value)
{
    u8 text[8];

    text[0] = EOS;
    UiText_AppendNumber(text, sizeof(text), (long)value, 1, false);
    return UiText_Draw(UI_FONT_NORMAL, xRight, y, text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
}

/* the cable box's battle results where the game puts its window (near the whole screen): the title, the totals, and
 * a row for each of the last five players, the game's dashes for a row no one is on */
static void LinkResults(int L, int R, int T, const struct UiRamp *band)
{
    int w = 448, h = 244, x = ((L + R) >> 1) - (w >> 1), y = T + 12, b[4];
    int colW = x + 236, colL = x + 320, colD = x + 404;

    UiW_DialogBox(x, y, w, h, band, UI_NONE, b);
    UiG_Hit(HIT_MESSAGE, 0, x, y, w, h);
    CentredLines(x + (w >> 1), y + 8, sResults.title, 18);
    CentredLines(x + (w >> 1), y + 30, sResults.total, 18);
    UiText_Draw(UI_FONT_NORMAL, colW, y + 62, sText_Win, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiText_Draw(UI_FONT_NORMAL, colL, y + 62, sText_Lose, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiText_Draw(UI_FONT_NORMAL, colD, y + 62, sText_Draw, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiG_HLine(x + 16, y + 80, w - 32, UI_P_TEXT_MUTED);
    for (int i = 0; i < NATIVE_LINK_RESULTS; i++)
    {
        int ry = y + 88 + i * 28;

        if (!sResults.used[i])
        {
            UiText_Draw(UI_FONT_NORMAL, x + 24, ry, sResults.dashName, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            UiText_Draw(UI_FONT_NORMAL, colW, ry, sResults.dashScore, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            UiText_Draw(UI_FONT_NORMAL, colL, ry, sResults.dashScore, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            UiText_Draw(UI_FONT_NORMAL, colD, ry, sResults.dashScore, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            continue;
        }
        UiText_Draw(UI_FONT_NORMAL, x + 24, ry, sResults.names[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        Number(colW, ry, sResults.wins[i]);
        Number(colL, ry, sResults.losses[i]);
        Number(colD, ry, sResults.draws[i]);
    }
}

/* the Berry Blender's record where the game puts its window: the title, and each count of players with its speed */
static void BlenderRecords(int L, int R, int T, const struct UiRamp *band)
{
    int w = 320, h = 156, x = ((L + R) >> 1) - (w >> 1), y = T + 56, b[4];
    const u8 *p = sBlender.players;

    UiW_DialogBox(x, y, w, h, band, UI_NONE, b);
    UiG_Hit(HIT_MESSAGE, 0, x, y, w, h);
    CentredLines(x + (w >> 1), y + 8, sBlender.title, 18);
    UiG_HLine(x + 16, y + 50, w - 32, UI_P_TEXT_MUTED);
    for (int i = 0; i < NATIVE_BLENDER_RECORDS; i++)
    {
        int ry = y + 60 + i * 28;

        if (p != NULL && *p != EOS)
        {
            UiText_Draw(UI_FONT_NORMAL, x + 24, ry, p, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            while (*p != EOS && *p != CHAR_NEWLINE)
                p++;
            if (*p == CHAR_NEWLINE)
                p++;
        }
        UiText_Draw(UI_FONT_NORMAL, x + w - 24, ry, sBlender.speeds[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    }
}

/* a line of the game's own hyphens: a rule */
static bool IsHyphens(const u8 *s)
{
    if (*s != CHAR_HYPHEN)
        return false;
    while (*s == CHAR_HYPHEN)
        s++;
    return *s == EOS;
}

/* a Battle Frontier board (a facility's results, the link contests', the Ranking Hall's): the design's box where the
 * game puts its window, twice its size, and each line the game printed on it where the game laid it out, twice as far
 * in; a line the game centres in the window stays centred, one it ends at the right stays ending there, a row of
 * hyphens is a rule */
static void FrontierBoard(int L, int R, int T, const struct UiRamp *band)
{
    const struct NativeUiPrinted *p;
    u32 n = TextPrinter_NativeUiTranscript(sBoardWindow, &p);
    int gw = sBoardW * 8, gh = sBoardH * 8, w = gw * 2 + 16, h = gh * 2 + 16;
    int x = ((L + R) >> 1) - (w >> 1), y = T + 12, b[4], ix = x + 8, iy = y + 8;

    UiW_DialogBox(x, y, w, h, band, UI_NONE, b);
    UiG_Hit(HIT_MESSAGE, 0, x, y, w, h);
    for (u32 i = 0; i < n; i++)
    {
        int font = p[i].fontId == FONT_SMALL ? UI_FONT_SMALL : p[i].fontId == FONT_NARROW ? UI_FONT_NARROW : UI_FONT_NORMAL;
        int sw = UiText_Width(font, p[i].text, 0), ty = iy + p[i].y * 2;

        if (IsHyphens(p[i].text))
            UiG_HLine(ix + p[i].x * 2, ty + 8, w - 16 - p[i].x * 4, UI_P_TEXT_MUTED);
        else if (abs(p[i].x * 2 + sw - gw) <= 4 || abs(p[i].x * 2 + sw - (DISPLAY_WIDTH - 16)) <= 4) /* PrintAligned's */
            UiText_Draw(font, x + (w >> 1), ty, p[i].text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        else if (p[i].x > gw / 2 && p[i].x + sw >= gw - 12)
            UiText_Draw(font, ix + (p[i].x + sw) * 2, ty, p[i].text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        else
            UiText_Draw(font, ix + p[i].x * 2, ty, p[i].text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void Draw(int t)
{
    struct UiRamp field = UiW_FieldBand();
    const struct UiRamp *band = sSign ? &sWood : &field;
    struct UiHintItem h[2];
    int n = 0, bx, by, bw, bh, L, R, B;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    if (MenuUp() && sMenu.ready)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_No};
        else if (!sMenu.ignoreB)
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
    }
    else if (sWaiting)
    {
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
    }
    if (sPicUp)
        PictureBox(bx, by, bw, bh);
    if (sMsgUp && sSpeaker[0] != EOS)
    {
        /* the name box: the speaker on a plate over the box */
        int w = UiText_Width(UI_FONT_NORMAL, sSpeaker, 0) + 16, y = B - UI_FIELD_MSG_H - 8 - 15;

        UiG_Paint(UiM_Rounded4(w, 18, 3, 3, 0, 0), L + 16, y, band->base,
                  &(struct UiPaint){band->ink, band->hi, band->lo, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_NORMAL, L + 24, y + 1, sSpeaker, UI_P_TEXT_W, band->ink, UI_ALIGN_LEFT, 0);
    }
    if (sMsgUp)
        UiField_MessageBox(t, L, R, B, sHasMsg ? sMsg : NULL, sMsgPrinting ? (int)sMsgPrinted : -1, sWaiting, band, h, n,
                           band->ink, HIT_MESSAGE);
    if (sMoneyBox || sCoinBox)
    {
        /* the money window, then the coins under it (top left, where the game puts its boxes) */
        u8 value[24];
        int y = by + 8;

        if (sMoneyBox)
        {
            UiItems_Money(value, sizeof(value), sMoney);
            UiItems_MoneyBox(L + 8, y, 148, "MONEY", value, band->ink);
            y += 26;
        }
        if (sCoinBox)
        {
            value[0] = EOS;
            UiText_AppendNumber(value, sizeof(value), (long)sCoins, 1, true);
            UiItems_MoneyBox(L + 8, y, 148, "COINS", value, band->ink);
        }
    }
    if (sResultsUp || sBlenderUp || sBoardUp)
    {
        if (sResultsUp)
            LinkResults(L, R, by, band);
        else if (sBlenderUp)
            BlenderRecords(L, R, by, band);
        else
            FrontierBoard(L, R, by, band);
        if (!sMsgUp && sWaiting)
        {
            struct UiHintItem ok = {UI_ACT_CONFIRM, sText_OK};

            UiW_Hints(R - 8 - UiW_HintsWidth(&ok, 1, 10), B - 28, &ok, 1, true, 10);
        }
    }
    if (sElevatorUp)
    {
        /* the elevator's "Now on" window where the game puts its own (top right): the floor the car is on */
        int w = 132, x = R - 8 - w, y = by + 8, b[4];

        UiW_DialogBox(x, y, w, 50, band, UI_NONE, b);
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 8, sElevatorLines[0], UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 26, sElevatorLines[1], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
    {
        UiW_YesNo(R - 8, B - UI_FIELD_MENU_GAP, sMenu.cursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (MenuUp())
    {
        struct UiMenuItem items[NATIVE_SCRIPT_MENU_MAX];
        struct UiMenuOpt o = {0};

        for (int i = 0; i < sMenu.count; i++)
            items[i] = (struct UiMenuItem){sMenu.labels[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 96;
        UiW_MenuBox(R - 8, B - UI_FIELD_MENU_GAP, items, sMenu.count, sMenu.cursor, &o, NULL);
    }
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sWaiting || (MenuUp() && sMenu.ready);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sWaiting || (MenuUp() && sMenu.ready && (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO || !sMenu.ignoreB)))
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (MenuUp() && sMenu.ready && (hit->id == UI_HIT_MENU || hit->id == UI_HIT_YESNO) && hit->index >= 0
     && hit->index < (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO ? 2 : sMenu.count))
    {
        int moves = hit->index - sMenu.cursor;

        NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (hit->id == HIT_MESSAGE && sWaiting)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !MenuUp() || !sMenu.ready)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

static void HideTiles(uint32_t mask[4], u16 start, u16 count)
{
    const struct OamData *oam = (const struct OamData *)OAM;

    for (int i = 0; i < 128; i++)
        if (oam[i].tileNum >= start && oam[i].tileNum < start + count)
            mask[i >> 5] |= 1u << (i & 31);
}

/* the money box's label sprite (the game's sign beside its box) goes with the box; the picture box's Pokémon (a 64 x 64
 * sprite) is drawn here */
static void HideObjects(uint32_t mask[4])
{
    u16 start, count;

    if (sMoneyBox && Money_NativeUiLabelTiles(&start, &count))
        HideTiles(mask, start, count);
    if (sPicUp && sPic.tileNum != 0xFFFF)
        HideTiles(mask, sPic.tileNum, 64);
}

const struct UiScreen gUiScreen_FieldText = {
    .name = "field_text",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
    .hideObjects = HideObjects,
};
#endif // ENABLE_NATIVE_UI
