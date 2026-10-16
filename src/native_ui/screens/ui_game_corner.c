#ifdef ENABLE_NATIVE_UI
/*
 * 22 · Game Corner prizes (the approved design's preview/js/screens/game_corner.js):
 * the Mauville Game Corner's counters on casino carpet - the Coin Case counter
 * with reel digits, the money window, the prize board with a gold marquee of
 * chasing bulbs - over the engine's own counters, which are its scripts
 * (data/maps/MauvilleCity_GameCorner/scripts.inc, data/scripts/mp_progression.inc):
 * four clerks show the coin box, a multichoice or a YES / NO, take the coins or
 * money, give the TM or the Shiny Charm, send the doll to the PC or fill the
 * Coin Case, and say so in the field's message box. The screen is up while a
 * clerk's coin box is, on that map. The counter (tab) is the clerk talked to;
 * its rows are the engine's multichoice rows (item, decoration and prices read
 * from their text); the PRIZE COUNTER also carries the Shiny Charm (the
 * owner's guaranteed prize, MP_SHINY_CHARM_COINS, refused when owned), which
 * is the next clerk's. Rows and tabs of another clerk only say so. The party
 * row under a TM is the game's own compatibility. Clicks walk the engine's
 * cursor and press A; the wheel moves it; a right click is B. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "coins.h"
#include "decoration.h"
#include "decoration_inventory.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "item.h"
#include "main.h"
#include "money.h"
#include "move.h"
#include "overworld.h"
#include "pokemon.h"
#include "string_util.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/coins.h"
#include "constants/decorations.h"
#include "constants/items.h"
#include "constants/vars.h"
#include "constants/map_groups.h"
#include "constants/multiplayer_balance.h"
#include "constants/script_menu.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN, /* index: the engine's row */
    HIT_TAB,
    HIT_BOARD,
};

enum
{
    COUNTER_NONE,
    COUNTER_TMS,
    COUNTER_PRIZES,
    COUNTER_COINS,
};

/* the clerks (their scripts) */
enum
{
    CLERK_NONE,
    CLERK_TMS,
    CLERK_DOLLS,
    CLERK_COINS,
    CLERK_CHARM,
};

extern const u8 MauvilleCity_GameCorner_EventScript_PrizeCornerTMs[];
extern const u8 MauvilleCity_GameCorner_EventScript_PrizeCornerDolls[];
extern const u8 MauvilleCity_GameCorner_EventScript_CoinsClerk[];
extern const u8 MauvilleCity_GameCorner_EventScript_ShinyCharmClerk[];

enum
{
    ROW_TEXT,   /* a row as the engine prints it */
    ROW_ITEM,   /* an item for coins */
    ROW_DECOR,  /* a decoration for coins (sent to the PC) */
    ROW_COINS,  /* coins for money */
    ROW_EXIT,
    ROW_CHARM,  /* the Shiny Charm (its clerk asks YES / NO, no list) */
};

#define ROW_H 24
#define CHARM_H 46
#define MAX_ROWS (NATIVE_SCRIPT_MENU_MAX + 1)
#define BX 224
#define BY 32
#define BW 408
#define BH 262

struct Row
{
    u8 kind;
    u8 clerk;     /* whose row it is */
    s8 engine;    /* its row in that clerk's multichoice (-1: none) */
    u16 id;       /* item or decoration */
    u32 price;    /* coins */
    u32 coins;    /* ROW_COINS: coins bought */
    u32 yen;      /* ROW_COINS: their price */
    const u8 *label;
};

static const char *const sTabLabels[3] = {"TM COUNTER", "PRIZE COUNTER", "COIN COUNTER"};

static const u8 sText_Pick[] = _("Pick");
static const u8 sText_Buy[] = _("Buy");
static const u8 sText_Leave[] = _("Leave");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_OtherClerk[] = _("That counter is another clerk's: talk to them.");
static const u8 sText_NextClerk[] = _("That prize is the next clerk's: talk to them.");
static const u8 sText_HoldsUpTo[] = _("Holds up to ");
static const u8 sText_Coins[] = _(" Coins");
static const u8 sText_CoinsInfo[] = _("Coins are a separate Game Corner currency. They aren't real money and can't be bought from outside the game.");
static const u8 sText_LeaveCounter[] = _("Leave the counter.");
static const u8 sText_ToPC[] = _("Sent to your PC.");
static const u8 sText_CharmLine[] = _("The major prize. Yours to keep.");
static const u8 sText_CharmOwnedLine[] = _("Already yours.");
static const u8 sText_CharmRule1[] = _("Guaranteed: never a random prize.");
static const u8 sText_CharmRule2[] = _("One per player: a second is refused.");
static const u8 sText_RingLine[] = _("Unlocks Mega Evolution. Yours to keep.");
static const u8 sText_RingRule1[] = _("Needed before any Mega Stone is sold.");
static const u8 sText_RingRule2[] = _("One per player: a second is refused.");
static const u8 sText_RoomFor[] = _("Room for ");
static const u8 sText_MoreCoins[] = _(" more Coins.");

/* ---------------------------------------------------------------- captured state */

static struct NativeScriptMenu sMenu;
static u8 sClerk;                 /* the clerk talked to */
static u8 sCounter;               /* the board's counter (tab) */
static struct Row sRows[MAX_ROWS];
static int sRowCount;
static int sSel;                  /* the selected row (the engine's cursor, kept after its list closes) */
static const u8 *sNote;
static int sFirst;
static u32 sCoins;
static u32 sMoney;
static u8 sMsg[256];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sWaiting;
static uint32_t sLastCapture;
static uint32_t sNoteUntil;
static int sGoal = -1;
static bool sGoalYesNo;
static int sGoalSteps;
static u32 sOwned[MAX_ROWS];      /* in the Bag (items) or the PC (decorations) */
static struct UiMon sParty[PARTY_SIZE];
static u8 sTeach[PARTY_SIZE];     /* 0 not able, 1 able, 2 learned */
static int sPartyCount;

static bool OnGameCornerMap(void)
{
    return gSaveBlock1Ptr->location.mapGroup == (MAP_MAUVILLE_CITY_GAME_CORNER >> 8)
        && gSaveBlock1Ptr->location.mapNum == (MAP_MAUVILLE_CITY_GAME_CORNER & 0xFF);
}

static bool Detect(void)
{
    return gMain.callback2 == CB2_Overworld && OnGameCornerMap() && Coins_NativeUiBoxShown();
}

/* ---------------------------------------------------------------- the engine's rows */

static bool IsDigit(u8 c)
{
    return c >= CHAR_0 && c <= CHAR_9;
}

/* the numbers in a row's text after byte `from` (commas inside them skipped) */
static int Numbers(const u8 *s, int from, u32 *out, int max)
{
    int n = 0;

    for (int i = from; s[i] != EOS && n < max; i++)
    {
        if (!IsDigit(s[i]))
            continue;
        out[n] = 0;
        while (IsDigit(s[i]) || (s[i] == CHAR_COMMA && IsDigit(s[i + 1])))
        {
            if (s[i] != CHAR_COMMA)
                out[n] = out[n] * 10 + (s[i] - CHAR_0);
            i++;
        }
        n++;
        i--;
    }
    return n;
}

/* a row's leading name: up to a control code or a space before a number; returns its length */
static int NamePart(const u8 *s, u8 *name, int size)
{
    int i = 0, n = 0;

    while (s[i] == CHAR_SPACE)
        i++;
    for (; s[i] != EOS && s[i] != CHAR_NEWLINE && s[i] != EXT_CTRL_CODE_BEGIN && n < size - 1; i++)
    {
        if (s[i] == CHAR_SPACE && IsDigit(s[i + 1]))
            break;
        name[n++] = s[i];
    }
    while (n > 0 && name[n - 1] == CHAR_SPACE)
        n--;
    name[n] = EOS;
    return i;
}

static u16 FindTmItem(const u8 *name)
{
    if (name[0] == EOS)
        return ITEM_NONE;
    for (u32 i = 1; i < ITEMS_COUNT; i++)
        if (GetItemPocket(i) == POCKET_TM_HM && StringCompare(GetItemName(i), name) == 0)
            return i;
    return ITEM_NONE;
}

static u16 FindDecoration(const u8 *name)
{
    if (name[0] == EOS)
        return DECOR_NONE;
    for (u32 i = 1; i <= NUM_DECORATIONS; i++)
        if (StringCompare(gDecorations[i].name, name) == 0)
            return i;
    return DECOR_NONE;
}

static u32 DecorationsInPc(u16 decor)
{
    const struct DecorationInventory *inv = &gDecorationInventories[gDecorations[decor].category];
    u32 n = 0;

    for (u32 i = 0; i < inv->size; i++)
        if (inv->items[i] == decor)
            n++;
    return n;
}

/* one engine row: its item, decoration or coin price read from its text */
static void ParseRow(struct Row *r, u8 counter, const u8 *s)
{
    u8 name[32];
    u32 nums[2] = {0, 0};
    int end, n;

    r->kind = ROW_TEXT;
    r->label = s;
    if (s == NULL)
        return;
    if (s == gText_Exit)
    {
        r->kind = ROW_EXIT;
        return;
    }
    end = NamePart(s, name, sizeof(name));
    /* coins rows are all numbers ("  50 COINS    ¥1,000"); a prize's follow its name ("TM32", "TREECKO DOLL") */
    n = Numbers(s, counter == COUNTER_COINS ? 0 : end, nums, 2);
    if (counter == COUNTER_COINS && n == 2)
    {
        r->kind = ROW_COINS;
        r->coins = nums[0];
        r->yen = nums[1];
    }
    else if (counter == COUNTER_TMS && n >= 1 && (r->id = FindTmItem(name)) != ITEM_NONE)
    {
        r->kind = ROW_ITEM;
        r->price = nums[0];
    }
    else if (counter == COUNTER_PRIZES && n >= 1 && (r->id = FindDecoration(name)) != DECOR_NONE)
    {
        r->kind = ROW_DECOR;
        r->price = nums[0];
    }
}

static void AddList(u8 counter, u8 clerk, u32 multichoiceId)
{
    const u8 *labels[NATIVE_SCRIPT_MENU_MAX];
    u32 n = ScriptMenu_NativeUiList(multichoiceId, labels, NATIVE_SCRIPT_MENU_MAX);

    for (u32 i = 0; i < n && sRowCount < MAX_ROWS; i++)
    {
        struct Row *r = &sRows[sRowCount++];

        *r = (struct Row){ROW_TEXT, clerk, (s8)i, 0, 0, 0, 0, NULL};
        ParseRow(r, counter, labels[i]);
    }
}

/* the board of a counter: the engine's lists (the Shiny Charm first on the prize counter) */
static void BuildRows(u8 counter)
{
    sRowCount = 0;
    sFirst = 0;
    sSel = 0;
    switch (counter)
    {
    case COUNTER_TMS:
        AddList(counter, CLERK_TMS, MULTI_GAME_CORNER_TMS);
        break;
    case COUNTER_PRIZES:
        sRows[sRowCount++] = (struct Row){ROW_CHARM, CLERK_CHARM, -1, ITEM_MEGA_RING, MP_MEGA_RING_COINS, 0, 0, NULL};
        sRows[sRowCount++] = (struct Row){ROW_CHARM, CLERK_CHARM, -1, ITEM_SHINY_CHARM, MP_SHINY_CHARM_COINS, 0, 0, NULL};
        AddList(counter, CLERK_DOLLS, MULTI_GAME_CORNER_DOLLS);
        break;
    case COUNTER_COINS:
        AddList(counter, CLERK_COINS, MULTI_GAME_CORNER_COINS);
        break;
    }
    sCounter = counter;
}

static u8 ClerkCounter(u8 clerk)
{
    switch (clerk)
    {
    case CLERK_TMS: return COUNTER_TMS;
    case CLERK_DOLLS: return COUNTER_PRIZES;
    case CLERK_CHARM: return COUNTER_PRIZES;
    case CLERK_COINS: return COUNTER_COINS;
    }
    return COUNTER_NONE;
}

static u8 ListClerk(u8 multichoiceId)
{
    switch (multichoiceId)
    {
    case MULTI_GAME_CORNER_TMS: return CLERK_TMS;
    case MULTI_GAME_CORNER_DOLLS: return CLERK_DOLLS;
    case MULTI_GAME_CORNER_COINS: return CLERK_COINS;
    }
    return CLERK_NONE;
}

/* the clerk talked to: the selected object's script (else the list up) */
static u8 TalkedClerk(void)
{
    const u8 *script = NULL;

    if (gSelectedObjectEvent < OBJECT_EVENTS_COUNT && gObjectEvents[gSelectedObjectEvent].active)
        script = GetObjectEventScriptPointerByObjectEventId(gSelectedObjectEvent);
    if (script == MauvilleCity_GameCorner_EventScript_PrizeCornerTMs)
        return CLERK_TMS;
    if (script == MauvilleCity_GameCorner_EventScript_PrizeCornerDolls)
        return CLERK_DOLLS;
    if (script == MauvilleCity_GameCorner_EventScript_CoinsClerk)
        return CLERK_COINS;
    if (script == MauvilleCity_GameCorner_EventScript_ShinyCharmClerk)
        return CLERK_CHARM;
    if (sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE)
        return ListClerk(sMenu.multichoiceId);
    return sClerk;
}

/* the clerk's own list is up */
static bool ListUp(void)
{
    return sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sClerk != CLERK_NONE && ListClerk(sMenu.multichoiceId) == sClerk;
}

/* the row of the clerk's engine row */
static int RowOf(int engine)
{
    for (int i = 0; i < sRowCount; i++)
        if (sRows[i].clerk == sClerk && sRows[i].engine == engine)
            return i;
    return -1;
}

static const struct Row *Cur(void)
{
    return sSel >= 0 && sSel < sRowCount ? &sRows[sSel] : NULL;
}

static int Visible(void)
{
    return sCounter == COUNTER_TMS ? 5 : 6;
}

static void Clamp(void)
{
    if (sSel < sFirst)
        sFirst = sSel;
    if (sSel >= sFirst + Visible())
        sFirst = sSel - Visible() + 1;
    if (sFirst < 0)
        sFirst = 0;
}

static void GoalStep(void)
{
    if (sGoal < 0)
        return;
    if (sGoalYesNo ? sMenu.kind != NATIVE_SCRIPT_MENU_YES_NO : !ListUp())
    {
        sGoal = -1;
        return;
    }
    if (!sMenu.ready || NativeUi_PressesPending())
        return;
    if (++sGoalSteps > 16)
        sGoal = -1;
    else if (sMenu.cursor == sGoal)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
    }
    else
        NativeUi_QueuePress(sGoal > sMenu.cursor ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    const struct Row *cur;
    u16 move = MOVE_NONE;
    bool8 active;

    if (!Detect())
        return;
    if (now - sLastCapture != 1)
    {
        sGoal = -1;
        sNoteUntil = 0;
        sCounter = COUNTER_NONE;
    }
    sLastCapture = now;
    ScriptMenu_NativeUiState(&sMenu);
    sClerk = TalkedClerk();
    if (ClerkCounter(sClerk) != sCounter)
        BuildRows(ClerkCounter(sClerk));
    if (ListUp() && RowOf(sMenu.cursor) >= 0)
        sSel = RowOf(sMenu.cursor);
    else if (sClerk == CLERK_CHARM)
    {
        /* the prize the clerk is offering now (the script keeps its item in VAR_TEMP_2); else the first */
        u16 offered = VarGet(VAR_TEMP_2);

        sSel = 0;
        for (int i = 0; i < sRowCount; i++)
            if (sRows[i].kind == ROW_CHARM && sRows[i].id == offered)
                sSel = i;
    }
    Clamp();
    sCoins = GetCoins();
    sMoney = GetMoney(&gSaveBlock1Ptr->money);
    for (int i = 0; i < sRowCount; i++)
    {
        sOwned[i] = 0;
        if (sRows[i].kind == ROW_ITEM || sRows[i].kind == ROW_CHARM)
        {
            UiGfx_PrepareItemIcon(sRows[i].id);
            sOwned[i] = CountTotalItemQuantityInBag(sRows[i].id);
        }
        else if (sRows[i].kind == ROW_DECOR)
            sOwned[i] = DecorationsInPc(sRows[i].id);
    }
    UiGfx_PrepareItemIcon(ITEM_COIN_CASE);
    /* the party under a TM: the game's own compatibility */
    cur = Cur();
    if (cur != NULL && cur->kind == ROW_ITEM)
        move = GetItemTMHMMoveId(cur->id);
    sPartyCount = 0;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        UiMon_FromPokemon(&sParty[i], &gParties[B_TRAINER_PLAYER][i]);
        sTeach[i] = 0;
        if (sParty[i].species == SPECIES_NONE)
            continue;
        sPartyCount = i + 1;
        if (move != MOVE_NONE && !sParty[i].isEgg)
        {
            for (int m = 0; m < sParty[i].moveCount; m++)
                if (sParty[i].moves[m] == move)
                    sTeach[i] = 2;
            if (sTeach[i] == 0 && CanLearnTeachableMove(sParty[i].species, move))
                sTeach[i] = 1;
        }
    }
    sHasMsg = TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sWaiting = sMenu.kind == NATIVE_SCRIPT_MENU_NONE && (Script_NativeUiWaitsForButton() || TextPrinter_NativeUiWindowWaiting(0));
    GoalStep();
}

/* ---------------------------------------------------------------- the room */

static struct UiRamp Red(void)
{
    return UiRampOf(UI_P_CMD_FIGHT, 0.42, 0.25, 0.62);
}

static struct UiRamp Gold(void)
{
    return UiRampOf(UI_P_POCKETS_KEY, 0.45, 0.25, 0.6);
}

static UiRGBA Plate(void)
{
    struct UiRamp r = Red();

    return UiMix(r.base, r.ink, 0.45);
}

struct CarpetCtx
{
    int x0;
    int y0;
};

static bool CarpetDiamonds(int x, int y, const void *ctx)
{
    const struct CarpetCtx *c = ctx;
    int cx = x + c->x0, cy = y + c->y0;

    return ((((cx + cy + 8192) / 16) + ((cx - cy + 8192) / 16)) & 1) == 0;
}

static bool CarpetDots(int x, int y, const void *ctx)
{
    const struct CarpetCtx *c = ctx;
    int cx = x + c->x0 + 4096, cy = y + c->y0 + 4096;

    return ((cx + cy) % 32 < 2) && ((cx - cy + 8192) % 32 < 2);
}

static void Carpet(void)
{
    UiRGBA carpet = UiMix(UI_P_CMD_FIGHT, UI_P_INK, 0.58), carpet2 = UiMix(UI_P_CMD_FIGHT, UI_P_INK, 0.46);
    struct UiRamp gold = Gold();
    struct CarpetCtx c;
    int x0, y0, w, h;
    char tag[48];

    UiG_Bleed(&x0, &y0, &w, &h);
    c.x0 = x0;
    c.y0 = y0;
    UiG_Rect(x0, y0, w, h, carpet);
    snprintf(tag, sizeof(tag), "gccarpet%dx%d:%d,%d", w, h, x0, y0);
    UiG_Mask(UiM_Fn(w, h, tag, CarpetDiamonds, &c), x0, y0, carpet2);
    snprintf(tag, sizeof(tag), "gccarpet%dx%d:%d,%dd", w, h, x0, y0);
    UiG_Mask(UiM_Fn(w, h, tag, CarpetDots, &c), x0, y0, UiMix(gold.lo, carpet, 0.35));
}

/* one marquee bulb (5x5); lit ones have a white core */
static void Bulb(int x, int y, bool on)
{
    static const char *const rows[5] = {".aaa.", "abbba", "abcba", "abbba", ".aaa."};
    struct UiRamp g = Gold();
    struct UiPixPal lit[3] = {{'a', g.ink}, {'b', UI_P_HP_YELLOW_0}, {'c', UI_P_PAPER}};
    struct UiPixPal off[3] = {{'a', g.ink}, {'b', UiMix(g.lo, UI_P_INK, 0.4)}, {'c', UiMix(g.lo, UI_P_INK, 0.25)}};

    UiW_Pix(on ? "gc_bulb_on" : "gc_bulb_off", rows, 5, on ? lit : off, 3, x, y, false);
}

/* bulbs around a rectangle every `step` px, chasing: 2 of 3 lit, a step every 8 frames */
static void BulbRing(int x, int y, int w, int h, int step, int t)
{
    int i = 0, ph = t / 8;

    for (int bx = x; bx <= x + w - 5; bx += step, i++)
        Bulb(bx, y, (i + ph) % 3 != 0);
    for (int by = y + step; by <= y + h - 5 - step; by += step, i++)
        Bulb(x + w - 5, by, (i + ph) % 3 != 0);
    for (int bx = x + w - 5; bx >= x; bx -= step, i++)
        Bulb(bx, y + h - 5, (i + ph) % 3 != 0);
    for (int by = y + h - 5 - step; by >= y + step; by -= step, i++)
        Bulb(x, by, (i + ph) % 3 != 0);
}

static void Num(u8 *dst, int size, u32 n)
{
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, (long)n, 1, true);
}

/* the Coin Case counter: the engine's icon, reel digits, its capacity */
static void CoinCase(int x, int y, int w, int h, int t)
{
    struct UiRamp gold = Gold(), red = Red();
    UiRGBA plate = Plate();
    char digits[12];
    u8 line[40];
    int lead;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 12, h - 12, 2, 2, 2, 2), x + 6, y + 6, plate,
              &(struct UiPaint){red.ink, UiMix(plate, red.base, 0.5), UI_NONE, 0, 0, NULL, UI_NONE});
    BulbRing(x + 8, y + 8, w - 16, h - 16, 8, t);
    UiGfx_ItemIconScaled(ITEM_COIN_CASE, x + 16, y + 26, 2);
    UiCaps_Draw(x + 72, y + 20, "COINS", gold.hi, red.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    /* the Coin Case holds up to 999,999 coins (the PC game's wallet): six reels */
    snprintf(digits, sizeof(digits), "%06u", (unsigned)(sCoins > MAX_COINS_WALLET ? MAX_COINS_WALLET : sCoins));
    lead = 6 - (int)snprintf(NULL, 0, "%u", (unsigned)sCoins);
    for (int i = 0; i < 6; i++)
    {
        int cx = x + 72 + i * 21, cy = y + 32;
        char d[2] = {digits[i], 0};

        UiG_Paint(UiM_Rounded4(19, 32, 2, 2, 2, 2), cx, cy, UI_P_PAPER, &(struct UiPaint){red.ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiG_HLine(cx + 2, cy + 15, 15, UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.6));
        UiItems_BigCaps(cx + 10, cy + 6, d, 2, i < lead ? UI_P_PAPER_LO : UI_P_TEXT, UI_NONE, UI_ALIGN_CENTER);
    }
    line[0] = EOS;
    UiText_Append(line, sizeof(line), sText_HoldsUpTo);
    UiText_AppendNumber(line, sizeof(line), MAX_COINS_WALLET, 1, true);
    UiText_AppendAscii(line, sizeof(line), ".");
    UiText_Draw(UI_FONT_SMALL, x + 72, y + 68, line, UiMix(gold.hi, UI_P_PAPER, 0.4), UI_NONE, UI_ALIGN_LEFT, 0);
}

/* ---------------------------------------------------------------- prizes */

/* a decoration's own picture (a doll is its field sprite) */
static bool DecorArt(u16 decor, int x, int y, int scale)
{
    const struct ObjectEventGraphicsInfo *info;
    const u16 *pal;

    if (decor == DECOR_NONE || gDecorations[decor].permission != DECORPERM_SPRITE || gDecorations[decor].tiles == NULL)
        return false;
    info = GetObjectEventGraphicsInfo(gDecorations[decor].tiles[0]);
    if (info == NULL || info->compressed || info->images == NULL)
        return false;
    pal = ObjectEvent_NativeUiPalette(info->paletteTag);
    return UiGfx_FieldSpriteSized(info->images[0].data, 1, pal, 0, info->width, info->height, x, y, scale, false);
}

static void RowName(const struct Row *r, u8 *dst, int size)
{
    dst[0] = EOS;
    switch (r->kind)
    {
    case ROW_ITEM:
    case ROW_CHARM:
        UiItems_ItemName(dst, size, r->id);
        break;
    case ROW_DECOR:
        UiText_Append(dst, size, gDecorations[r->id].name);
        break;
    case ROW_COINS:
        Num(dst, size, r->coins);
        UiText_Append(dst, size, sText_Coins);
        break;
    default:
        if (r->label != NULL)
            UiText_Append(dst, size, r->label);
        break;
    }
}

static void Paragraph(const u8 *text, int x, int y, int w, int lineH, int maxLines, int font)
{
    const u8 *starts[8];
    int lengths[8];
    int n = UiText_Wrap(text, w, font, starts, lengths, 8);

    for (int i = 0; i < n && i < maxLines; i++)
    {
        u8 line[96];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(font, x, y + i * lineH, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

/* the green OWNED capsule */
static void OwnedTag(int x, int y)
{
    struct UiRamp r = UiRampOf(UI_P_CMD_POKEMON, 0.4, 0.25, 0.6);

    UiG_Paint(UiM_Capsule(50, 11, true, true), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 25, y + 2, "OWNED", UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
}

static void InfoCard(int x, int y, int w, int h)
{
    const struct Row *r = Cur();
    struct UiRamp red = Red();
    u8 buf[64];

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_CREAM, &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    if (r == NULL)
        return;
    switch (r->kind)
    {
    case ROW_COINS:
        UiItems_Coin(x + 10, y + 10);
        RowName(r, buf, sizeof(buf));
        UiG_Text(x + 24, y + 4, buf);
        UiItems_Money(buf, sizeof(buf), r->yen);
        UiItems_PriceTag(x + w - 92, y + 5, 84, buf, red.ink, false);
        Paragraph(sText_CoinsInfo, x + 10, y + 28, w - 20, 12, 5, UI_FONT_SMALL);
        UiCaps_Draw(x + 10, y + h - 30, "RATES: EMERALD DEFAULT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiCaps_Draw(x + 10, y + h - 20, "20 YEN A COIN, ALL PACKS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        return;
    case ROW_CHARM:
    {
        int yy = y + 38;

        UiGfx_ItemIcon(r->id, x + 6, y + 4, NULL);
        RowName(r, buf, sizeof(buf));
        UiG_Text(x + 34, y + 2, buf);
        UiItems_Coin(x + 34, y + 21);
        Num(buf, sizeof(buf), r->price);
        UiText_Append(buf, sizeof(buf), sText_Coins);
        UiG_TextEx(x + 46, y + 16, buf, UI_FONT_NORMAL, sCoins >= r->price ? UI_P_TEXT : UI_P_TEXT_RED,
                   sCoins >= r->price ? UI_P_TEXT_SH : UI_P_TEXT_RED_SH, UI_ALIGN_LEFT);
        if (CheckBagHasItem(r->id, 1))
            OwnedTag(x + w - 58, y + 19);
        Paragraph(GetItemDescription(r->id), x + 10, yy, w - 20, 12, 3, UI_FONT_SMALL);
        yy += 40;
        UiG_Rect(x + 10, yy + 5, 3, 3, Gold().base);
        UiText_Draw(UI_FONT_SMALL, x + 17, yy, r->id == ITEM_MEGA_RING ? sText_RingRule1 : sText_CharmRule1, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiG_Rect(x + 10, yy + 17, 3, 3, Gold().base);
        UiText_Draw(UI_FONT_SMALL, x + 17, yy + 12, r->id == ITEM_MEGA_RING ? sText_RingRule2 : sText_CharmRule2, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        return;
    }
    case ROW_EXIT:
    case ROW_TEXT:
        if (r->label != NULL)
            UiG_Text(x + 10, y + 4, r->label);
        if (r->kind == ROW_EXIT)
            UiText_Draw(UI_FONT_SMALL, x + 10, y + 24, sText_LeaveCounter, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        return;
    default:
        break;
    }
    /* an item or a decoration for coins */
    if (r->kind == ROW_ITEM)
        UiGfx_ItemIcon(r->id, x + 6, y + 4, NULL);
    else
        DecorArt(r->id, x + 10, y + 8, 1);
    RowName(r, buf, sizeof(buf));
    UiG_Text(x + 34, y + 2, buf);
    UiItems_Coin(x + 34, y + 21);
    Num(buf, sizeof(buf), r->price);
    UiText_Append(buf, sizeof(buf), sText_Coins);
    UiG_TextEx(x + 46, y + 16, buf, UI_FONT_NORMAL, sCoins >= r->price ? UI_P_TEXT : UI_P_TEXT_RED,
               sCoins >= r->price ? UI_P_TEXT_SH : UI_P_TEXT_RED_SH, UI_ALIGN_LEFT);
    if (r->kind == ROW_ITEM)
    {
        u16 move = GetItemTMHMMoveId(r->id);
        int dy = y + 38;
        char stats[40], pw[8], ac[8];

        if (move != MOVE_NONE)
        {
            int type = UiMon_Type(GetMoveType(move));

            if (type >= 0)
                UiW_TypeBadge(x + 10, dy, type, 0);
            UiW_CategoryIcon(x + 54, dy, GetMoveCategory(move));
            snprintf(pw, sizeof(pw), "%u", (unsigned)GetMovePower(move));
            snprintf(ac, sizeof(ac), "%u", (unsigned)GetMoveAccuracy(move));
            snprintf(stats, sizeof(stats), "POW %s  ACC %s", GetMovePower(move) > 1 ? pw : "---", GetMoveAccuracy(move) ? ac : "---");
            UiCaps_Draw(x + 86, dy + 3, stats, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            dy += 18;
        }
        Paragraph(GetItemDescription(r->id), x + 10, dy, w - 20, 15, 3, UI_FONT_NORMAL);
        snprintf(stats, sizeof(stats), "IN BAG " UI_CAPS_TIMES "%u", (unsigned)sOwned[sSel]);
        UiCaps_Draw(x + w - 10, y + h - 14, stats, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
    else
    {
        char stats[40];

        Paragraph(gDecorations[r->id].description, x + 10, y + 38, w - 20, 15, 3, UI_FONT_NORMAL);
        UiText_Draw(UI_FONT_SMALL, x + 10, y + h - 30, sText_ToPC, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
        snprintf(stats, sizeof(stats), "IN PC " UI_CAPS_TIMES "%u", (unsigned)sOwned[sSel]);
        UiCaps_Draw(x + w - 10, y + h - 14, stats, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
}

/* the party under a TM: who can learn it (the game's own compatibility) */
static void PartyRow(int x, int y, int w, int t)
{
    static const struct UiFilter sGray = {0.6f, 0, 0, 0, 0};

    UiG_Rect(x, y - 4, w, 1, UI_P_CREAM_LO);
    UiG_Rect(x, y - 3, w, 1, UI_P_CREAM_HI);
    UiCaps_Draw(x + 4, y + 2, "YOUR PARTY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    for (int i = 0; i < sPartyCount; i++)
    {
        const struct UiMon *m = &sParty[i];
        int cx = x + 4 + i * 62, cy = y + 14;
        const char *tag = sTeach[i] == 2 ? "LEARNED" : sTeach[i] == 1 ? "ABLE" : "NOT ABLE";
        struct UiRamp r = UiRampOf(sTeach[i] == 2 ? UI_HEX(0x5890C8) : sTeach[i] == 1 ? UI_HEX(0x48A848) : UI_HEX(0xA0A0A8), 0.4, 0.25, 0.6);

        if (m->species == SPECIES_NONE)
            continue;
        UiGfx_MonIcon(m->species, m->personality, m->isEgg, sTeach[i] == 1 ? ((t + i * 7) / 20) % 2 : 0, cx - 4, cy - 6,
                      sTeach[i] ? NULL : &sGray);
        UiG_Paint(UiM_Capsule(54, 11, true, true), cx - 2, cy + 26, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(cx + 25, cy + 28, tag, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    }
}

/* the coin counter: the Coin Case's room left */
static void CaseGauge(int x, int y, int w)
{
    struct UiRamp gold = Gold();
    int bw = w - 8, fill = (int)((bw - 2) * (double)(sCoins > MAX_COINS_WALLET ? MAX_COINS_WALLET : sCoins) / MAX_COINS_WALLET + 0.5);
    char caps[32];
    u8 line[48];

    UiG_Rect(x, y - 4, w, 1, UI_P_CREAM_LO);
    UiG_Rect(x, y - 3, w, 1, UI_P_CREAM_HI);
    UiCaps_Draw(x + 4, y + 2, "COIN CASE", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "%u,%03u / 999,999", (unsigned)(sCoins / 1000), (unsigned)(sCoins % 1000));
    if (sCoins < 1000)
        snprintf(caps, sizeof(caps), "%u / 999,999", (unsigned)sCoins);
    UiCaps_Draw(x + w - 4, y + 2, caps, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_Paint(UiM_Rounded4(bw, 10, 2, 2, 2, 2), x + 4, y + 13, UiMix(gold.lo, UI_P_INK, 0.5),
              &(struct UiPaint){gold.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
    if (fill > 0)
    {
        UiG_Rect(x + 5, y + 14, fill, 8, gold.base);
        UiG_Rect(x + 5, y + 14, fill, 2, gold.hi);
        UiG_Rect(x + 5, y + 21, fill, 1, gold.lo);
    }
    line[0] = EOS;
    UiText_Append(line, sizeof(line), sText_RoomFor);
    UiText_AppendNumber(line, sizeof(line), (long)(sCoins >= MAX_COINS_WALLET ? 0 : MAX_COINS_WALLET - sCoins), 1, true);
    UiText_Append(line, sizeof(line), sText_MoreCoins);
    UiG_Text(x + 4, y + 26, line);
}

static void Board(int t)
{
    struct UiRamp red = Red(), gold = Gold();
    UiRGBA plate = Plate();
    int tx = BX + 8, sx = BX + 8, sy = BY + 64, sw = BW - 16, sh = BH - 72, ry;

    UiG_Paint(UiM_Rounded4(BW, BH, 4, 4, 4, 4), BX, BY, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(BW - 12, 34, 2, 2, 2, 2), BX + 6, BY + 6, plate,
              &(struct UiPaint){red.ink, UiMix(plate, red.base, 0.5), UI_NONE, 0, 0, NULL, UI_NONE});
    BulbRing(BX + 8, BY + 8, BW - 16, 30, 8, t);
    UiItems_BigCaps(BX + (BW >> 1), BY + 13, "PRIZE CORNER", 2, gold.hi, red.ink, UI_ALIGN_CENTER);
    /* the counters: the clerk's own is the tab in front */
    for (int i = 0; i < 3; i++)
    {
        bool on = sCounter == COUNTER_TMS + i;
        int tw = UiCaps_Width(sTabLabels[i]) + 20;
        struct UiRamp r = on ? red : UiRampOf(UiMix(red.base, UI_P_INK, 0.35), 0.3, 0.25, 0.62);

        UiG_Paint(UiM_Rounded4(tw, on ? 20 : 17, 3, 3, 0, 0), tx, BY + 44 + (on ? 0 : 3), r.base,
                  &(struct UiPaint){red.ink, r.hi, UI_NONE, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(tx + 10, BY + 50 + (on ? 0 : 2), sTabLabels[i], on ? UI_P_TEXT_W : UiMix(r.hi, UI_P_PAPER, 0.3), r.ink,
                    UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiG_Hit(HIT_TAB, i, tx, BY + 44, tw, 20);
        tx += tw + 4;
    }
    UiG_Paint(UiM_Rounded4(sw, sh, 2, 2, 2, 2), sx, sy, UI_P_CREAM, &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiG_Rect(sx + 1, sy + 1, sw - 2, 2, red.base);
    ry = sy + 6;
    for (int k = 0; k < Visible() && sFirst + k < sRowCount; k++)
    {
        int idx = sFirst + k, rh = sRows[idx].kind == ROW_CHARM ? CHARM_H : ROW_H;
        const struct Row *r = &sRows[idx];
        bool sel = idx == sSel;
        u8 name[64];

        if (sel)
            UiG_Paint(UiM_Rounded(sw - 20, rh - 2, UiCornerR(2), UiCornerChamfer(4), UiCornerR(2), UiCornerR(2)), sx + 10, ry,
                      UiMix(gold.hi, UI_P_PAPER, 0.55), &(struct UiPaint){gold.lo, UiMix(gold.hi, UI_P_PAPER, 0.85), UI_NONE, 0, 0, NULL, UI_NONE});
        RowName(r, name, sizeof(name));
        switch (r->kind)
        {
        case ROW_CHARM:
        {
            u8 price[16];

            if (!sel)
                UiG_Paint(UiM_Rounded4(sw - 20, rh - 2, 2, 2, 2, 2), sx + 10, ry, UiMix(gold.hi, UI_P_CREAM, 0.7),
                          &(struct UiPaint){gold.lo, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            BulbRing(sx + 12, ry + 2, sw - 24, rh - 6, 10, t);
            UiGfx_ItemIcon(r->id, sx + 24, ry + 10, NULL);
            UiG_Text(sx + 52, ry + 7, name);
            UiText_Draw(UI_FONT_SMALL, sx + 52, ry + 23, CheckBagHasItem(r->id, 1) ? sText_CharmOwnedLine : (r->id == ITEM_MEGA_RING ? sText_RingLine : sText_CharmLine), UI_P_TEXT_MUTED,
                        UI_NONE, UI_ALIGN_LEFT, 0);
            if (CheckBagHasItem(r->id, 1))
                OwnedTag(sx + sw - 148, ry + 17);
            Num(price, sizeof(price), r->price);
            UiG_TextEx(sx + sw - 30, ry + 14, price, UI_FONT_NORMAL, r->price > sCoins ? UI_HEX(0x909090) : UI_P_TEXT,
                       r->price > sCoins ? UI_NONE : UI_P_TEXT_SH, UI_ALIGN_RIGHT);
            UiItems_Coin(sx + sw - 26, ry + 18);
            break;
        }
        case ROW_COINS:
        {
            u8 price[24];

            UiItems_Coin(sx + 18, ry + 7);
            UiG_Text(sx + 34, ry + 4, name);
            UiItems_Money(price, sizeof(price), r->yen);
            UiItems_PriceTag(sx + sw - 104, ry + 5, 84, price, red.lo, r->yen > sMoney);
            break;
        }
        case ROW_ITEM:
        case ROW_DECOR:
        {
            u8 price[16];
            bool dear = r->price > sCoins;

            if (r->kind == ROW_ITEM)
            {
                u16 move = GetItemTMHMMoveId(r->id);

                UiGfx_ItemIcon(r->id, sx + 14, ry, NULL);
                if (move != MOVE_NONE && UiMon_Type(GetMoveType(move)) >= 0)
                    UiW_TypeBadge(sx + 252, ry + 6, UiMon_Type(GetMoveType(move)), 0);
            }
            else
                DecorArt(r->id, sx + 18, ry + 4, 1);
            UiG_Text(sx + 42, ry + 4, name);
            Num(price, sizeof(price), r->price);
            UiG_TextEx(sx + sw - 30, ry + 4, price, UI_FONT_NORMAL, dear ? UI_HEX(0x909090) : UI_P_TEXT, dear ? UI_NONE : UI_P_TEXT_SH,
                       UI_ALIGN_RIGHT);
            UiItems_Coin(sx + sw - 26, ry + 8);
            break;
        }
        default:
            UiG_Text(sx + 42, ry + 4, name);
            break;
        }
        if (sel)
            UiW_Cursor(sx + 1, ry + (rh >> 1) - 6, UiW_Nudge(t), UI_NONE);
        if (!sel && k < Visible() - 1 && idx < sRowCount - 1)
            UiG_HLine(sx + 14, ry + rh - 1, sw - 30, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.4));
        UiG_Hit(HIT_ROW, idx, sx + 4, ry, sw - 8, rh);
        ry += rh;
    }
    if (sCounter == COUNTER_COINS)
        CaseGauge(sx + 10, sy + sh - 62, sw - 20);
    if (sCounter == COUNTER_TMS && Cur() != NULL && Cur()->kind == ROW_ITEM)
        PartyRow(sx + 10, sy + sh - 60, sw - 20, t);
    if (NativeUi_Frame() < sNoteUntil && sNote != NULL)
    {
        int w = UiText_Width(UI_FONT_SMALL, sNote, 0) + 16;

        UiG_Paint(UiM_Rounded4(w, 18, 3, 3, 3, 3), BX + BW - 8 - w, BY + 66, UI_P_PAPER,
                  &(struct UiPaint){red.ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
        UiText_Draw(UI_FONT_SMALL, BX + BW - 8 - w + 8, BY + 68, sNote, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

/* ---------------------------------------------------------------- message, prompts */

static void Bottom(int t)
{
    struct UiRamp red = Red();
    int b[4];

    UiW_DialogBox(8, 300, 464, 54, NULL, UI_NONE, b);
    if (sHasMsg)
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] - 1, b[2] - 4, 2);
    if (sWaiting)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Paint(UiM_Rounded4(160, 54, 3, 0, 3, 0), 480, 300, UI_P_CREAM, &(struct UiPaint){red.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Choose}}, c[1] = {{UI_ACT_CANCEL, sText_No}};

        UiW_Hints(490, 309, a, 1, false, 8);
        UiW_Hints(490, 329, c, 1, false, 8);
        UiW_YesNo(632, 296, sMenu.cursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    }
    else if (ListUp())
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sCounter == COUNTER_COINS ? sText_Buy : sText_Pick}};
        struct UiHintItem c[1] = {{UI_ACT_CANCEL, sText_Leave}};

        UiW_Hints(490, 309, a, 1, false, 8);
        if (!sMenu.ignoreB)
            UiW_Hints(490, 329, c, 1, false, 8);
    }
    else if (sWaiting)
    {
        struct UiHintItem a[1] = {{UI_ACT_CONFIRM, sText_Next}};

        UiW_Hints(490, 309, a, 1, false, 8);
    }
}

static void Draw(int t)
{
    struct UiRamp red = Red();
    u8 money[24];
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Hit(HIT_BOARD, 0, bx, by, bw, bh);
    Carpet();
    UiW_TitleTab(0, 8, "GAME CORNER", &red, 0, NULL);
    CoinCase(8, 32, 208, 104, t);
    UiItems_Money(money, sizeof(money), sMoney);
    UiItems_MoneyBox(8, 140, 208, "MONEY", money, red.ink);
    InfoCard(8, 166, 208, 128);
    Board(t);
    Bottom(t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sMenu.ready || sWaiting;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        sGoal = -1;
        if ((sMenu.kind == NATIVE_SCRIPT_MENU_MULTICHOICE && sMenu.ready && !sMenu.ignoreB)
         || (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO && sMenu.ready) || sWaiting)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (hit->id == HIT_TAB)
    {
        if (hit->index != sCounter - COUNTER_TMS)
        {
            sNote = sText_OtherClerk;
            sNoteUntil = NativeUi_Frame() + 180;
        }
        return true;
    }
    if (hit->id == HIT_ROW && hit->index >= 0 && hit->index < sRowCount && sRows[hit->index].clerk != sClerk)
    {
        sNote = sText_NextClerk;
        sNoteUntil = NativeUi_Frame() + 180;
        return true;
    }
    if (sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
    {
        if (hit->id == UI_HIT_YESNO)
        {
            sGoal = hit->index;
            sGoalYesNo = true;
            sGoalSteps = 0;
            GoalStep();
        }
        return true;
    }
    if (ListUp())
    {
        if (hit->id == HIT_ROW && hit->index >= 0 && hit->index < sRowCount && sRows[hit->index].engine >= 0
         && sRows[hit->index].engine < sMenu.count)
        {
            sGoal = sRows[hit->index].engine;
            sGoalYesNo = false;
            sGoalSteps = 0;
            GoalStep();
        }
        return true;
    }
    if (sWaiting)
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || !sMenu.ready)
        return false;
    if (ListUp() || sMenu.kind == NATIVE_SCRIPT_MENU_YES_NO)
    {
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
        return true;
    }
    return false;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_GameCorner = {
    .name = "game_corner",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
