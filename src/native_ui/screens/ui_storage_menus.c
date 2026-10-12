#ifdef ENABLE_NATIVE_UI
/*
 * 14 · PC storage: the PC's first menus, over the field (the boxes are
 * ui_storage.c's terminal). Booting a PC up, which PC to use (the script's own
 * multichoice: whose storage it is, the player's PC, the Hall of Fame once the
 * game is clear, LOG OFF), the PC's lines ("Accessed ...", "POKéMON Storage
 * System opened."), the Pokémon Storage System's menu (WITHDRAW, DEPOSIT, MOVE
 * POKéMON, MOVE ITEMS, SEE YA!) with each option's description and the
 * engine's refusals (a full party, the last Pokémon), the player's PC (ITEM
 * STORAGE, MAILBOX, DECORATION, TURN OFF) and ITEM STORAGE's menu - in the
 * design's message box and menu box, the PC's teal and its tab, over the
 * field, as the pause menu is. The engine keeps every rule, line and button:
 * its menus' cursors move with its own presses (clicks and the wheel queue
 * them). The item lists, the mailbox, the decorations and the Hall of Fame keep
 * the game's picture. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "string_util.h"
#include "strings.h"
#include "constants/region_map_sections.h"
#include "constants/script_menu.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

/* the PC's lines (data/text/pc.inc, data/event_scripts.s) */
extern const u8 gText_PlayerHouseBootPC[];
extern const u8 gText_AccessedSomeonesPC[];
extern const u8 gText_AccessedLanettesPC[];
extern const u8 gText_StorageSystemOpened[];
extern const u8 gText_AccessedPlayersPC[];

enum
{
    HIT_MESSAGE = UI_HIT_SCREEN,
};

enum
{
    KIND_NONE,
    KIND_LINE,     /* one of the PC's lines in the field's message box (no menu yet) */
    KIND_CHOOSE,   /* which PC: the script's MULTI_PC */
    KIND_MENU,     /* the storage system's menu, the player's PC's, ITEM STORAGE's */
};

static const u8 sText_OK[] = _("OK");
static const u8 sText_Back[] = _("Back");
static const u8 sText_LogOff[] = _("Log off");
static const u8 sText_Next[] = _("Next");

static u8 sKind;
static struct NativePcMenu sM;
static u8 sPlayersPc[32];
static u8 sMsg[256];
static u32 sMsgPrinted;
static bool sHasMsg;
static bool sMsgPrinting;
static bool sWaiting;
static int sGrace;

static struct UiRamp PcRamp(void)
{
    /* ui_storage.c's terminal teal */
    return UiRampOf(UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.36), 0.38, 0.3, 0.62);
}

/* ---------------------------------------------------------------- detection */

static bool OnField(void)
{
    return gMain.callback2 == CB2_Overworld;
}

static bool IsPcLine(const u8 *text)
{
    static const u8 *const sLines[] = {
        gText_PlayerHouseBootPC, gText_WhichPCShouldBeAccessed, gText_AccessedSomeonesPC,
        gText_AccessedLanettesPC, gText_StorageSystemOpened, gText_AccessedPlayersPC,
    };
    u8 line[128];

    for (u32 i = 0; i < ARRAY_COUNT(sLines); i++)
    {
        StringExpandPlaceholders(line, sLines[i]);
        if (StringCompare(line, text) == 0)
            return true;
    }
    return false;
}

/* what of the PC is up now (m: its menu) */
static u8 Find(struct NativePcMenu *m)
{
    struct NativeScriptMenu sm;
    u8 text[256];
    u32 printed;
    bool8 active;

    memset(m, 0, sizeof(*m));
    if (!OnField())
        return KIND_NONE;
    if (Storage_NativeUiMainMenu(m) || PlayerPC_NativeUiState(m))
        return m->menu == NATIVE_PC_MENU_PLAYER_LINE ? KIND_LINE : KIND_MENU;
    if (ScriptMenu_NativeUiState(&sm) && sm.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sm.multichoiceId == MULTI_PC)
    {
        m->state = NATIVE_PC_MENU_INPUT;
        m->ready = sm.ready;
        m->count = ScriptMenu_NativeUiPcRows(m->labels, NATIVE_PC_MENU_MAX, sPlayersPc);
        m->cursor = sm.cursor < m->count ? sm.cursor : 0;
        return KIND_CHOOSE;
    }
    /* a secret base's PC boots with the same line, then its own menus (the game's picture) */
    if (gMapHeader.regionMapSectionId != MAPSEC_SECRET_BASE && Window_NativeUiShown(0)
     && TextPrinter_NativeUiWindowText(0, text, sizeof(text), &printed, &active) && IsPcLine(text))
        return KIND_LINE;
    return KIND_NONE;
}

static bool Detect(void)
{
    struct NativePcMenu m;

    if (Find(&m) != KIND_NONE)
    {
        sGrace = 3;
        return true;
    }
    /* between two of the PC's steps (a menu closed, the script's next line not
     * printed yet) the field's script still runs: stay up for those frames */
    if (sGrace > 0 && OnField() && (ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked()))
    {
        sGrace--;
        return true;
    }
    sGrace = 0;
    return false;
}

static void Capture(void)
{
    struct NativePcMenu m;
    u8 kind = Find(&m);
    bool8 active = FALSE;

    if (kind != KIND_NONE)
    {
        sKind = kind;
        sM = m;
    }
    else
    {
        /* the grace frames: what was up stays drawn, nothing reads buttons */
        sM.ready = FALSE;
    }
    sHasMsg = Window_NativeUiShown(0) && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = kind == KIND_LINE && sHasMsg && (Script_NativeUiWaitsForButton() || TextPrinter_NativeUiWindowWaiting(0));
}

/* ---------------------------------------------------------------- drawing */

static const char *TabLabel(void)
{
    if (sKind == KIND_MENU && sM.menu == NATIVE_PC_MENU_STORAGE)
        return "POK" UI_CAPS_E_ACUTE "MON STORAGE";
    if (sKind == KIND_MENU && sM.menu == NATIVE_PC_MENU_ITEMS)
        return "ITEM STORAGE";
    return "PC";
}

static bool MenuUp(void)
{
    return (sKind == KIND_MENU || sKind == KIND_CHOOSE) && sM.count > 0;
}

static void Bottom(int t, int L, int R, int B)
{
    struct UiRamp r = PcRamp();
    int b[4], hx;

    UiW_DialogBox(L + 8, B - 54, 456, 46, NULL, UI_NONE, b);
    if (sKind == KIND_MENU && sM.message != NULL)
    {
        /* the option's description, or why it was refused (the engine's lines) */
        UiW_MessagePage(sM.message, -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
        if (sM.state == NATIVE_PC_MENU_REFUSED)
            UiW_DownArrow(b[0] + b[2] - 10, b[1] + 20 + ((t / 16) % 2), UI_P_CURSOR, false);
    }
    else if (sHasMsg)
    {
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
        if (sWaiting)
            UiW_DownArrow(b[0] + b[2] - 10, b[1] + 20 + ((t / 16) % 2), UI_P_CURSOR, false);
    }
    UiG_Hit(HIT_MESSAGE, 0, L + 8, B - 54, 456, 46);
    UiG_Paint(UiM_Rounded4(160, 46, 3, 0, 3, 0), R - 160, B - 54, UI_P_CREAM,
              &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (MenuUp() && sM.state == NATIVE_PC_MENU_INPUT)
    {
        hx = UiW_Hint(R - 152, B - 46, UI_ACT_CONFIRM, sText_OK, false) + 8;
        UiW_Hint(hx, B - 46, UI_ACT_CANCEL, sKind == KIND_CHOOSE ? sText_LogOff : sText_Back, false);
    }
    else if ((sKind == KIND_MENU && sM.state == NATIVE_PC_MENU_REFUSED) || sWaiting)
    {
        UiW_Hint(R - 152, B - 46, UI_ACT_CONFIRM, sText_Next, false);
    }
}

static void Draw(int t)
{
    struct UiRamp r = PcRamp();
    int bx, by, bw, bh, L, R, B;

    UiG_Bleed(&bx, &by, &bw, &bh);
    L = bx;
    R = bx + bw;
    B = by + bh;
    UiW_TitleTab(L, by + 8, TabLabel(), &r, 0, NULL);
    if (MenuUp())
    {
        struct UiMenuItem items[NATIVE_PC_MENU_MAX];
        struct UiMenuOpt o = {0};

        for (int i = 0; i < sM.count; i++)
            items[i] = (struct UiMenuItem){sM.labels[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 112;
        o.band = &r;
        UiW_MenuBox(R - 8, B - 62, items, sM.count, sM.cursor, &o, NULL);
    }
    Bottom(t, L, R, B);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return (MenuUp() && sM.ready) || sWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if ((MenuUp() && sM.ready) || sWaiting)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (MenuUp() && sM.ready && sM.state == NATIVE_PC_MENU_INPUT && hit->id == UI_HIT_MENU
     && hit->index >= 0 && hit->index < sM.count)
    {
        int moves = hit->index - sM.cursor;

        NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (hit->id == HIT_MESSAGE && (sWaiting || (sKind == KIND_MENU && sM.state == NATIVE_PC_MENU_REFUSED && sM.ready)))
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    return false;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !MenuUp() || !sM.ready || sM.state != NATIVE_PC_MENU_INPUT)
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_StorageMenus = {
    .name = "storage",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
