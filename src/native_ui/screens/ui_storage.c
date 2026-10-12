#ifdef ENABLE_NATIVE_UI
/*
 * 14 · PC storage (the approved design's preview/js/screens/pc.js): the teal
 * storage terminal on its dot-matrix desk, the monitor with the hovered
 * Pokémon and its tractor-feed printout, the Box (6×5) on pixel wallpapers,
 * the party column and the pixel hand, over the engine's Pokémon Storage
 * System (pokemon_storage_system.c), which keeps every rule: the four PC modes
 * (withdraw, deposit, move Pokémon, move items), where the cursor can go,
 * what A offers (the game's own menus), moving and placing, the last-Pokémon
 * and mail checks, releasing, markings, box options (jump, wallpaper, name),
 * the summary. The design's toolbar keeps its two buttons for the game's
 * own: PARTY POKéMON (the design's Box list is the game's JUMP, in the
 * plate's box options) and CLOSE BOX. Moving several Pokémon at once keeps
 * the game's own picture while it runs. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "item.h"
#include "main.h"
#include "pokeball.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "region_map.h"
#include "string_util.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/region_map_sections.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_storage.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_SLOT = UI_HIT_SCREEN,
    HIT_PARTY,
    HIT_PLATE,
    HIT_ARROW,
    HIT_PARTY_BUTTON,
    HIT_CLOSE_BUTTON,
    HIT_BACKDROP,
    HIT_MESSAGE,
    HIT_BOXROW,
    HIT_MARK,
};

/* the design's geometry */
#define WW UI_STORAGE_WALL_W
#define WH UI_STORAGE_WALL_H
#define BX 210
#define BY 34
#define GX 218
#define GY 42
#define CELL 40
#define PTX 474
#define PTY 34
#define LIST_ROW 17

static const double sSlideSteps[] = {0, 0.45, 0.75, 0.9, 0.97, 1};

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_Select[] = _("Select");
static const u8 sText_PutDown[] = _("Put down");
static const u8 sText_PutBack[] = _("Put back");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Quick[] = _("Quick move");
static const u8 sText_QuickOn[] = _("Quick move: on");
static const u8 sText_BoxTitle[] = _("Box title");
static const u8 sText_TurnBox[] = _("Turn box");
static const u8 sText_OK[] = _("OK");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Box[] = _("Box");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Mark[] = _("Mark");
static const u8 sText_Next[] = _("Next");
static const u8 sText_PartyPokemon[] = _("Party Pokémon");
static const u8 sText_CloseBox[] = _("Close Box");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_None[] = _("None");
static const u8 sText_Dashes[] = _("---");
static const u8 sText_Egg[] = _("Egg");
static const u8 sText_No[] = _("No.");
static const u8 sText_Traded[] = _("Received in a trade");
static const u8 sText_Fateful[] = _("Fateful encounter");
static const u8 sText_MetAt[] = _("Met at ");
static const u8 sText_PutHere[] = _("Put it here");
static const u8 sText_ToggleMarks[] = _("Toggle each mark.");
static const u8 sText_IsInParty[] = _(" is in your party.");
static const u8 sText_IsIn[] = _(" is in ");
static const u8 sText_Period[] = _(".");
static const u8 sText_EmptySpace[] = _("This space is empty.");
static const u8 sText_EmptyParty[] = _("This party space is empty.");
static const u8 sText_SpacesUsed[] = _(" of 30 spaces used.");
static const u8 sText_MoveWhere1[] = _("Move ");
static const u8 sText_MoveWhere2[] = _(" where?");
static const u8 sText_Swaps[] = _("Putting it down swaps it with ");
static const u8 sText_TitleHelp[] = _("Change the Box, or open the Box options.");
static const u8 sText_Wallpaper[] = _(" wallpaper");
static const u8 sText_PartyHelp[] = _("Bring out your party.");
static const u8 sText_CloseHelp[] = _("Close the Box and log off.");
static const u8 sText_BackHelp[] = _("Put the party away and go back to the Box.");
static const u8 sText_ItemInHand[] = _(" is in your hand.");
static const u8 sText_ItemWhere[] = _("Give it to a Pokémon, or put it away.");
static const u8 sText_Dot[] = _(" · ");

/* ---------------------------------------------------------------- captured state */

struct Display
{
    bool8 has;
    bool8 isEgg;
    struct UiMon mon;
    u16 dexNum;
    const u8 *nature;
    u8 ot[PLAYER_NAME_LENGTH + 1];
    u8 otGender;
    u16 otId;
    u8 markings;
    u16 ball;
    u8 met[48];
    u32 key;               /* species and personality: a new Pokémon restarts the picture's animation */
};

static struct NativeStorage sS;
static struct NativeStorageBox sBox;      /* the current box */
static struct NativeStorageBox sTarget;   /* the box the game is scrolling to */
static struct NativeStorageBox sFrom;     /* a slide's old box */
static bool sSliding;
static int sSlideTo = -1;
static int sSlideDir;
static int sSlideF;
static struct UiMon sParty[PARTY_SIZE];
static int sPartyCount;
static struct Display sD;
static int sHoverT;
static int sAllCount;
static u8 sListNames[TOTAL_BOXES_COUNT][BOX_NAME_LENGTH + 1];
static u8 sListWalls[TOTAL_BOXES_COUNT];
static u8 sListCounts[TOTAL_BOXES_COUNT];
static int sClosingT;
static u8 sHeldItemName[24];
static u8 sSwapName[POKEMON_NAME_BUFFER_SIZE];   /* holding: the Pokémon a put-down would swap with */
/* a mouse goal: the game's own presses until its cursor is there, then a button */
static struct
{
    bool on;
    u8 area;
    s8 pos;
    u16 then;            /* A_BUTTON, DPAD_LEFT/RIGHT (turning the box on its title), or 0 */
    int frames;
} sGoal;

static bool Detect(void)
{
    struct NativeStorage s;

    if (!Storage_NativeUiState(&s))
    {
        sGoal.on = false;
        sSliding = false;
        sSlideTo = -1;
        return false;
    }
    /* several Pokémon moving at once: the game's own picture while it runs */
    return s.phase != NATIVE_STORAGE_NONE && s.phase != NATIVE_STORAGE_MULTI;
}

/* the box the screen shows now (the scroll's target as soon as it starts) */
static const struct NativeStorageBox *ShownBox(void)
{
    return sS.scrollTo >= 0 ? &sTarget : &sBox;
}

static int ShownBoxId(void)
{
    return sS.scrollTo >= 0 ? sS.scrollTo : sS.box;
}

static void CaptureDisplay(void)
{
    struct Pokemon mon;
    bool has = false;
    u32 key;

    if (sS.phase == NATIVE_STORAGE_LOADING)
    {
        has = false;
    }
    else if (sS.holdingMon)
    {
        mon = sS.heldMon;
        has = true;
    }
    else if (sS.cursorArea == NATIVE_STORAGE_IN_BOX && sS.scrollTo < 0)
    {
        has = Storage_NativeUiBoxMon(sS.box, sS.cursorPos, &mon);
    }
    else if (sS.cursorArea == NATIVE_STORAGE_IN_PARTY && sS.cursorPos >= 0 && sS.cursorPos < PARTY_SIZE)
    {
        mon = gParties[B_TRAINER_PLAYER][sS.cursorPos];
        has = GetMonData(&mon, MON_DATA_SPECIES) != SPECIES_NONE;
    }
    if (!has)
    {
        if (sD.has)
            sHoverT = 0;
        memset(&sD, 0, sizeof(sD));
        return;
    }
    key = GetMonData(&mon, MON_DATA_SPECIES) ^ GetMonData(&mon, MON_DATA_PERSONALITY);
    if (!sD.has || key != sD.key)
        sHoverT = 0;
    memset(&sD, 0, sizeof(sD));
    sD.has = true;
    sD.key = key;
    UiMon_FromPokemon(&sD.mon, &mon);
    sD.isEgg = sD.mon.isEgg;
    sD.dexNum = SpeciesToNationalPokedexNum(sD.mon.species);
    sD.nature = gNaturesInfo[GetNature(&mon)].name;
    GetMonData(&mon, MON_DATA_OT_NAME, sD.ot);
    sD.ot[PLAYER_NAME_LENGTH] = EOS;
    sD.otGender = GetMonData(&mon, MON_DATA_OT_GENDER);
    sD.otId = GetMonData(&mon, MON_DATA_OT_ID) & 0xFFFF;
    sD.markings = GetMonData(&mon, MON_DATA_MARKINGS);
    {
        u32 ball = GetMonData(&mon, MON_DATA_POKEBALL);

        sD.ball = ball < POKEBALL_COUNT ? gPokeBalls[ball].itemId : ITEM_NONE;
    }
    /* where it came from: met where (the save's own Pokémon), or received in a trade */
    {
        u16 playerId = gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8);
        u8 metLoc = GetMonData(&mon, MON_DATA_MET_LOCATION);

        sD.met[0] = EOS;
        if (sD.otId != playerId || sD.otGender != gSaveBlock2Ptr->playerGender
         || StringCompare(sD.ot, gSaveBlock2Ptr->playerName) != 0)
        {
            UiText_Append(sD.met, sizeof(sD.met), sText_Traded);
        }
        else if (metLoc == METLOC_FATEFUL_ENCOUNTER)
        {
            UiText_Append(sD.met, sizeof(sD.met), sText_Fateful);
        }
        else if (metLoc < MAPSEC_NONE)
        {
            u8 place[32];

            GetMapNameHandleAquaHideout(place, metLoc);
            UiText_Append(sD.met, sizeof(sD.met), sText_MetAt);
            UiText_Append(sD.met, sizeof(sD.met), place);
        }
    }
    if (sD.mon.heldItem != ITEM_NONE)
        UiGfx_PrepareItemIcon(sD.mon.heldItem);
    if (sD.ball != ITEM_NONE)
        UiGfx_PrepareItemIcon(sD.ball);
    UiGfx_PrepareMonPic(sD.mon.species, sD.mon.personality, sD.mon.shiny, sD.mon.isEgg);
}

/* ---------------------------------------------------------------- mouse goals */

static void SetGoal(u8 area, s8 pos, u16 then)
{
    sGoal.on = true;
    sGoal.area = area;
    sGoal.pos = pos;
    sGoal.then = then;
    sGoal.frames = 0;
}

/* one press toward the goal; the game moves its cursor (and slides the party) itself */
static void GoalStep(void)
{
    int r, c, tr, tc;
    u16 key = 0;

    if (!sGoal.on)
        return;
    if (++sGoal.frames > 600 || (sS.phase != NATIVE_STORAGE_MAIN && sS.phase != NATIVE_STORAGE_BUSY))
    {
        sGoal.on = false;
        return;
    }
    if (sS.phase != NATIVE_STORAGE_MAIN || !sS.ready || NativeUi_PressesPending())
        return;
    if (sS.cursorArea == sGoal.area && (sS.cursorPos == sGoal.pos || sGoal.area == NATIVE_STORAGE_BOX_TITLE))
    {
        if (sGoal.then != 0)
            NativeUi_QueuePress(sGoal.then);
        sGoal.on = false;
        return;
    }
    switch (sGoal.area)
    {
    case NATIVE_STORAGE_IN_BOX:
        tr = sGoal.pos / IN_BOX_COLUMNS;
        tc = sGoal.pos % IN_BOX_COLUMNS;
        if (sS.cursorArea == NATIVE_STORAGE_IN_BOX)
        {
            r = sS.cursorPos / IN_BOX_COLUMNS;
            c = sS.cursorPos % IN_BOX_COLUMNS;
            if (r != tr)
                key = tr > r ? DPAD_DOWN : DPAD_UP;
            else
                key = (tc - c + IN_BOX_COLUMNS) % IN_BOX_COLUMNS <= (c - tc + IN_BOX_COLUMNS) % IN_BOX_COLUMNS ? DPAD_RIGHT : DPAD_LEFT;
        }
        else if (sS.cursorArea == NATIVE_STORAGE_BOX_TITLE)
        {
            key = DPAD_DOWN;
        }
        else if (sS.cursorArea == NATIVE_STORAGE_BUTTONS)
        {
            key = DPAD_UP;
        }
        else
        {
            key = B_BUTTON;   /* the party goes away, the cursor back into the box */
        }
        break;
    case NATIVE_STORAGE_IN_PARTY:
        if (sS.cursorArea == NATIVE_STORAGE_IN_PARTY)
        {
            int down = (sGoal.pos - sS.cursorPos + PARTY_SIZE + 1) % (PARTY_SIZE + 1);

            key = down <= PARTY_SIZE + 1 - down ? DPAD_DOWN : DPAD_UP;
        }
        else if (sS.cursorArea == NATIVE_STORAGE_BUTTONS && sS.cursorPos == 0)
        {
            key = A_BUTTON;   /* PARTY POKéMON: the party comes out */
        }
        else if (sS.cursorArea == NATIVE_STORAGE_BUTTONS)
        {
            key = DPAD_LEFT;
        }
        else if (sS.cursorArea == NATIVE_STORAGE_BOX_TITLE)
        {
            key = DPAD_UP;
        }
        else
        {
            key = DPAD_DOWN;
        }
        break;
    case NATIVE_STORAGE_BOX_TITLE:
        if (sS.cursorArea == NATIVE_STORAGE_IN_BOX)
            key = START_BUTTON;
        else if (sS.cursorArea == NATIVE_STORAGE_BUTTONS)
            key = DPAD_DOWN;
        else
            key = B_BUTTON;
        break;
    case NATIVE_STORAGE_BUTTONS:
        if (sS.cursorArea == NATIVE_STORAGE_BUTTONS)
            key = DPAD_RIGHT;   /* two buttons: either way */
        else if (sS.cursorArea == NATIVE_STORAGE_BOX_TITLE)
            key = DPAD_UP;
        else if (sS.cursorArea == NATIVE_STORAGE_IN_BOX)
            key = DPAD_DOWN;
        else
            key = B_BUTTON;
        break;
    }
    if (key != 0)
        NativeUi_QueuePress(key);
}

/* ---------------------------------------------------------------- capture */

static void Capture(void)
{
    int shown;

    Storage_NativeUiState(&sS);
    Storage_NativeUiBox(sS.box, &sBox);
    if (sS.scrollTo >= 0)
    {
        Storage_NativeUiBox(sS.scrollTo, &sTarget);
        if (sSlideTo != sS.scrollTo)
        {
            /* the design's slide toward the new box (six frames) */
            sFrom = sBox;
            sSliding = true;
            sSlideTo = sS.scrollTo;
            sSlideDir = sS.scrollDir;
            sSlideF = 0;
        }
        else if (sSliding && ++sSlideF >= 5)
        {
            sSliding = false;
        }
    }
    else
    {
        sSliding = false;
        sSlideTo = -1;
    }
    shown = ShownBoxId();
    (void)shown;
    sPartyCount = 0;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        UiMon_FromPokemon(&sParty[i], &gParties[B_TRAINER_PLAYER][i]);
        if (sParty[i].species != SPECIES_NONE)
            sPartyCount++;
    }
    sAllCount = 0;
    for (int b = 0; b < TOTAL_BOXES_COUNT; b++)
    {
        Storage_NativeUiBoxInfo(b, sListNames[b], &sListWalls[b], &sListCounts[b]);
        sAllCount += sListCounts[b];
    }
    CaptureDisplay();
    sHoverT++;
    /* holding: who would be swapped out under the hand */
    sSwapName[0] = EOS;
    if (sS.holdingMon)
    {
        struct Pokemon under;
        bool has = false;

        if (sS.cursorArea == NATIVE_STORAGE_IN_BOX && sS.scrollTo < 0)
            has = Storage_NativeUiBoxMon(sS.box, sS.cursorPos, &under);
        else if (sS.cursorArea == NATIVE_STORAGE_IN_PARTY && sS.cursorPos < PARTY_SIZE)
            has = (under = gParties[B_TRAINER_PLAYER][sS.cursorPos], GetMonData(&under, MON_DATA_SPECIES) != SPECIES_NONE);
        if (has)
        {
            GetMonData(&under, MON_DATA_NICKNAME, sSwapName);
            StringGet_Nickname(sSwapName);
        }
    }
    if (sS.heldItem != ITEM_NONE)
    {
        UiGfx_PrepareItemIcon(sS.heldItem);
        sHeldItemName[0] = EOS;
        UiText_Append(sHeldItemName, sizeof(sHeldItemName), GetItemName(sS.heldItem));
    }
    sClosingT = sS.phase == NATIVE_STORAGE_CLOSING ? sClosingT + 1 : 0;
    GoalStep();
}

/* ---------------------------------------------------------------- pieces */

static struct UiRamp PcRamp(void)
{
    return UiRampOf(UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.36), 0.38, 0.3, 0.62);
}

/* the design's hand (original pixel art); the game's quick mode turns its cuff green */
static void Hand(int x, int y, bool grab)
{
    static const char *const sPoint[] = {"...aaaaaaaaa...", "..accccccccca..", "..addddddddda..", "..aaaaaaaaaaa..", ".abbbbbbbbbbba.",
                                         "abhbbbbbbbbbbsa", "abbbbbbbbbbbbsa", "abbbbbbbbbbbbsa", "abbbbbbbbbbbbsa", ".abbsbbsbbbbsa.",
                                         ".abbsbbsabbbsa.", "..aa.aa.abbbsa.", "........abbbsa.", "........abbbsa.", "........abbbsa.",
                                         "........abbbsa.", ".........aaaa.."};
    static const char *const sGrab[] = {"...aaaaaaaaa...", "..accccccccca..", "..addddddddda..", "..aaaaaaaaaaa..", ".abbbbbbbbbbba.",
                                        "abhbbbbbbbbbbsa", "abbbbbbbbbbbbsa", "abbbbbbbbbbbbsa", "abbsbbsbbsbbbsa", "abbsbbsbbsbbbsa",
                                        ".aa.aa.aa.aaaa."};
    struct UiRamp r = sS.quickMode ? UiRampDefault(UI_P_CMD_POKEMON) : PcRamp();
    struct UiPixPal pal[6] = {{'a', UI_P_INK}, {'b', UI_P_PAPER}, {'s', UiMix(UI_P_PAPER_LO, UI_P_TYPES_STEEL, 0.4)},
                              {'h', UI_HEX(0xFFFFFF)}, {'c', r.hi}, {'d', r.lo}};
    char name[24];

    snprintf(name, sizeof(name), "%s%s", grab ? "pchandg" : "pchandp", sS.quickMode ? "q" : "");
    UiW_Pix(name, grab ? sGrab : sPoint, grab ? 11 : 17, pal, 6, x, y, false);
}

static void NumberText(u8 *dst, int size, long n, int digits)
{
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, n, digits, false);
}

/* ---------------------------------------------------------------- the monitor and the printout */

static void Printout(int t)
{
    int x = 12, y = 174, w = 186, h = 162, lx = x + 16, vx = x + 66;
    const struct NativeStorageBox *b = ShownBox();
    u8 v[48];

    (void)t;
    UiG_Rect(x + 2, y + h, w - 2, 2, UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.75));
    UiG_Paint(UiM_Rect(w, h), x, y, UI_P_PAPER, &(struct UiPaint){UiMix(UI_P_PAPER_LO, UI_P_INK, 0.45), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
    /* tractor-feed margins with sprocket holes */
    for (int k = 0; k < 2; k++)
    {
        int mx = k == 0 ? x + 1 : x + w - 11;

        UiG_Rect(mx, y + 1, 10, h - 2, UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.5));
        for (int yy = y + 6; yy < y + h - 4; yy += 12)
            UiG_Paint(UiM_Circle(5), mx + 3, yy, UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.64),
                      &(struct UiPaint){UiMix(UI_P_PAPER_LO, UI_P_INK, 0.4), UI_NONE, UI_NONE, 0, 0, NULL, 0});
    }
    UiG_VLine(x + 11, y + 1, h - 2, UI_P_PAPER_LO);
    UiG_VLine(x + w - 12, y + 1, h - 2, UI_P_PAPER_LO);
    for (int k = 1; k <= 5; k += 2)
        UiG_Rect(x + 12, y + 18 + k * 16, w - 24, 16, UiMix(UI_P_PAPER, UI_P_TYPES_ICE, 0.12));
    if (!sD.has)
    {
        int count = b->count;

        UiCaps_Draw(lx, y + 8, "BOX", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Text(vx, y + 3, b->name);
        UiCaps_Draw(lx, y + 24, "WALL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiG_Text(vx, y + 19, UiStorage_WallName(b->wallpaper));
        UiCaps_Draw(lx, y + 40, "SPACES", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        NumberText(v, sizeof(v), count, 1);
        UiText_AppendAscii(v, sizeof(v), "/30");
        UiG_Text(vx, y + 35, v);
        UiCaps_Draw(lx, y + 56, "ALL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        NumberText(v, sizeof(v), sAllCount, 1);
        UiText_AppendAscii(v, sizeof(v), "/");
        UiText_AppendNumber(v, sizeof(v), TOTAL_BOXES_COUNT * IN_BOX_COUNT, 1, false);
        UiG_Text(vx, y + 51, v);
        UiCaps_Draw(lx, y + 72, "PARTY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        NumberText(v, sizeof(v), sPartyCount, 1);
        UiText_AppendAscii(v, sizeof(v), "/6");
        UiG_Text(vx, y + 67, v);
        return;
    }
    /* the number and types (an Egg tells nothing) */
    v[0] = EOS;
    UiText_Append(v, sizeof(v), sText_No);
    if (sD.isEgg)
        UiText_Append(v, sizeof(v), sText_Dashes);
    else
        UiText_AppendNumber(v, sizeof(v), sD.dexNum, NATIONAL_DEX_COUNT > 999 ? 4 : 3, false);
    UiG_Text(lx, y + 3, v);
    if (!sD.isEgg)
    {
        int tx = x + w - 14;

        for (int i = 1; i >= 0; i--)
        {
            if (sD.mon.types[i] < 0)
                continue;
            tx -= 40;
            UiW_TypeBadge(tx, y + 5, sD.mon.types[i], 0);
            tx -= 2;
        }
    }
    {
        static const char *const sRows[5] = {"SPECIES", "NATURE", "ABILITY", "ITEM", "OT"};

        for (int k = 0; k < 5; k++)
        {
            int yy = y + 19 + k * 16;
            const u8 *val = sText_Dashes;
            UiRGBA fg = UI_P_TEXT, sh = UI_P_TEXT_SH;

            UiCaps_Draw(lx, yy + 5, sRows[k], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            if (sD.isEgg)
            {
                val = k == 0 ? sText_Egg : sText_Dashes;
            }
            else if (k == 0)
            {
                val = GetSpeciesName(sD.mon.species);
            }
            else if (k == 1)
            {
                val = sD.nature;
            }
            else if (k == 2)
            {
                val = sD.mon.abilityName;
            }
            else if (k == 3)
            {
                if (sD.mon.heldItem != ITEM_NONE)
                {
                    UiGfx_ItemIcon(sD.mon.heldItem, vx - 4, yy - 5, NULL);
                    if (UiText_Width(UI_FONT_NORMAL, sD.mon.heldItemName, 0) > x + w - 36 - vx)
                        UiText_Draw(UI_FONT_SMALL, vx + 20, yy + 2, sD.mon.heldItemName, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
                    else
                        UiG_Text(vx + 20, yy, sD.mon.heldItemName);
                    continue;
                }
                val = sText_None;
            }
            else
            {
                char id[8];

                fg = sD.otGender == FEMALE ? UI_P_FEMALE : UI_P_MALE;
                sh = sD.otGender == FEMALE ? UI_P_FEMALE_SH : UI_P_MALE_SH;
                v[0] = EOS;
                UiText_Append(v, sizeof(v), sD.ot);
                snprintf(id, sizeof(id), "  %05u", (unsigned)sD.otId);
                UiText_AppendAscii(v, sizeof(v), id);
                val = v;
            }
            if (UiText_Width(UI_FONT_NORMAL, val, 0) > x + w - 16 - vx)
                UiText_Draw(UI_FONT_SMALL, vx, yy + 2, val, fg, sh, UI_ALIGN_LEFT, 0);
            else
                UiText_Draw(UI_FONT_NORMAL, vx, yy, val, fg, sh, UI_ALIGN_LEFT, 0);
        }
    }
    {
        int my2 = y + 19 + 5 * 16, sy2 = my2 + 18;

        UiCaps_Draw(lx, my2 + 5, "MARKS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiMon_Markings(vx, my2 + 4, sD.markings, UI_P_INK, UI_P_CREAM_LO);
        if (!sD.isEgg)
        {
            UiRGBA sc;
            const char *status = UiMon_StatusLabel(&sD.mon, &sc);
            char hp[16];
            u8 hb[16];

            UiW_HpBar(lx, sy2 + 3, 92, sD.mon.maxHP > 0 ? (double)sD.mon.hp / sD.mon.maxHP : 0, true, UI_NONE);
            snprintf(hp, sizeof(hp), "%u/%u", (unsigned)sD.mon.hp, (unsigned)sD.mon.maxHP);
            UiText_FromAscii(hb, sizeof(hb), hp);
            UiText_Draw(UI_FONT_SMALL, x + w - 15, sy2, hb, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
            if (status != NULL)
                UiW_StatusBadge(x + w - 48, sy2 + 15, status, sc, 30);
        }
        if (sD.ball != ITEM_NONE)
            UiGfx_ItemIcon(sD.ball, lx - 4, sy2 + 12, NULL);
        UiText_Draw(UI_FONT_SMALL, lx + 20, sy2 + 16, sD.met, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void Monitor(int t)
{
    struct UiRamp r = PcRamp();
    int mx = 8, my = 30, mw = 194, mh = 140, sx = mx + 8, sy = my + 8, sw = mw - 16, sh = 104, ny = my + mh - 26;
    const struct NativeStorageBox *b = ShownBox();

    UiG_Paint(UiM_Rounded4(mw, mh, 5, 5, 5, 5), mx, my, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
    UiG_Paint(UiM_Circle(4), mx + 4, my + 4, r.lo, &(struct UiPaint){r.ink, r.hi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Circle(4), mx + mw - 7, my + 4, r.lo, &(struct UiPaint){r.ink, r.hi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(sw, sh, 2, 2, 2, 2), sx, sy, UI_P_INK, &(struct UiPaint){r.ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    if (sD.has)
    {
        int feet, cx, frame;

        UiMon_Stage(sx + 2, sy + 2, sw - 4, sh - 4, sD.isEgg ? UI_TYPE_NORMAL : sD.mon.types[0], sD.isEgg ? -1 : sD.mon.types[1], 26, 60,
                    &feet, &cx);
        UiG_Save();
        UiG_Clip(sx + 2, sy + 2, sw - 4, sh - 4);
        frame = UiGfx_MonPicFrameAt(sD.mon.species, sD.mon.personality, sD.mon.shiny, sD.mon.isEgg, sHoverT);
        if (!UiGfx_MonPic(sD.mon.species, sD.mon.personality, sD.mon.shiny, sD.mon.isEgg, frame, cx, feet, NULL))
            UiW_Missing(cx - 24, feet - 48, 48, 48);
        UiG_Restore();
        if (sD.mon.shiny)
            UiMon_Star(sx + 6, sy + 6);
    }
    else
    {
        /* nothing under the hand: this box's wallpaper with a caption plate */
        bool space = sS.cursorArea == NATIVE_STORAGE_IN_BOX || (sS.cursorArea == NATIVE_STORAGE_IN_PARTY && sS.cursorPos < PARTY_SIZE);
        int cw = space ? UiCaps_Width("EMPTY SPACE") + 12 : UiText_Width(UI_FONT_NORMAL, b->name, 0) + 12;

        UiG_BlitPart(UiStorage_Wall(b->wallpaper, sS.waldaColors), 34, 50, sw - 4, sh - 4, sx + 2, sy + 2, 1, false, 1.0);
        UiG_Paint(UiM_Rounded4(cw, space ? 13 : 16, 2, 2, 2, 2), sx + ((sw - cw) >> 1), sy + 44, UI_P_PAPER,
                  &(struct UiPaint){r.ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        if (space)
            UiCaps_Draw(sx + (sw >> 1), sy + 47, "EMPTY SPACE", UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        else
            UiText_Draw(UI_FONT_NORMAL, sx + (sw >> 1), sy + 44, b->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    }
    UiG_HLine(sx + 2, sy + 2, sw - 4, UiMix(UI_HEX(0xFFFFFF), r.hi, 0.5));
    /* the bezel strip: name and level */
    UiG_Paint(UiM_Rounded4(8, 8, 4, 4, 4, 4), mx + mw - 18, ny + 6, sD.has ? UI_P_CMD_POKEMON : UiMix(r.lo, UI_P_INK, 0.3),
              &(struct UiPaint){r.ink, UiMix(UI_P_CMD_POKEMON, UI_HEX(0xFFFFFF), 0.5), UI_NONE, 0, 0, NULL, 0});
    if (sD.has)
    {
        int nx = UiText_Draw(UI_FONT_NORMAL, mx + 10, ny + 2, sD.mon.nickname, UI_P_TEXT_W, r.ink, UI_ALIGN_LEFT, 0);

        if (!sD.isEgg)
        {
            u8 lv[16];

            UiW_Gender(nx + 1, ny + 2, sD.mon.gender);
            UiMon_LevelText(lv, sizeof(lv), sD.mon.level);
            UiText_Draw(UI_FONT_NORMAL, mx + mw - 24, ny + 2, lv, UI_P_TEXT_W, r.ink, UI_ALIGN_RIGHT, 0);
        }
    }
    else
    {
        UiText_Draw(UI_FONT_NORMAL, mx + 10, ny + 2, b->name, UI_P_TEXT_W, r.ink, UI_ALIGN_LEFT, 0);
    }
    Printout(t);
}

/* ---------------------------------------------------------------- the box */

static void BoxContents(const struct NativeStorageBox *b, int ox, bool current, int t)
{
    UiG_Blit(UiStorage_Wall(b->wallpaper, sS.waldaColors), BX + 4 + ox, BY + 4);
    for (int i = 0; i < IN_BOX_COUNT; i++)
    {
        int cx = GX + (i % IN_BOX_COLUMNS) * CELL + ox, cy = GY + (i / IN_BOX_COLUMNS) * CELL;
        bool on = current && !sSliding && sS.cursorArea == NATIVE_STORAGE_IN_BOX && sS.cursorPos == i;
        int fr = on ? (t / 10) % 2 : 0;

        if (b->species[i] == SPECIES_NONE)
            continue;
        UiGfx_MonIcon(b->species[i], b->personality[i], b->isEgg[i], fr, cx + 4, cy + 3 - (on && fr ? 1 : 0), NULL);
        if (b->heldItem[i] != ITEM_NONE)
            UiGfx_Icon("held_item", cx + 28, cy + 28, 0);
    }
}

static void Box(int t)
{
    const struct NativeStorageBox *b = ShownBox();
    struct UiRamp wr = UiStorage_WallRamp(b->wallpaper, sS.waldaColors);
    const struct UiMask *plate = UiM_Rounded4(256, 24, 4, 4, 4, 4);
    bool canBox = sS.mode != NATIVE_STORAGE_DEPOSIT;

    if (sS.cursorArea == NATIVE_STORAGE_BOX_TITLE && !sS.holdingMon && sS.phase != NATIVE_STORAGE_LOADING)
        UiW_SelectFrame(plate, BX, 6, UI_NONE, UI_NONE, 0);
    UiG_Paint(plate, BX, 6, wr.base, &(struct UiPaint){wr.ink, wr.hi, wr.lo, 0, 2, NULL, 0});
    for (int k = 0; k < 2; k++)
    {
        int ax = k == 0 ? BX + 3 : BX + 256 - 27;

        UiG_Paint(UiM_Rounded4(24, 17, 3, 3, 3, 3), ax, 9, UiMix(wr.lo, wr.ink, 0.25), &(struct UiPaint){wr.ink, wr.base, UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(ax + 12, 14, k == 0 ? UI_CAPS_LEFT : UI_CAPS_RIGHT, UI_P_TEXT_W, wr.ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
        if (canBox)
            UiG_Hit(HIT_ARROW, k, ax, 6, 24, 24);
    }
    UiText_Draw(UI_FONT_NORMAL, BX + 128, 8, b->name, UI_P_TEXT_W, wr.ink, UI_ALIGN_CENTER, 0);
    if (canBox)
        UiG_Hit(HIT_PLATE, 0, BX + 28, 6, 200, 24);
    UiG_Paint(UiM_Rounded4(256, 216, 3, 3, 3, 3), BX, BY, wr.base, &(struct UiPaint){wr.ink, wr.hi, wr.lo, 0, 2, NULL, 0});
    UiG_Save();
    UiG_Clip(BX + 4, BY + 4, WW, WH);
    if (sSliding)
    {
        int off = (int)(WW * sSlideSteps[sSlideF + 1 < 6 ? sSlideF + 1 : 5] + 0.5) * sSlideDir;

        BoxContents(&sFrom, -off, false, t);
        BoxContents(&sTarget, sSlideDir * WW - off, false, t);
    }
    else
    {
        BoxContents(b, 0, true, t);
    }
    UiG_Restore();
    UiG_Mask(UiM_Outline(UiM_Rect(WW + 2, WH + 2)), BX + 3, BY + 3, wr.ink);
    if (canBox)
        for (int i = 0; i < IN_BOX_COUNT; i++)
            UiG_Hit(HIT_SLOT, i, GX + (i % IN_BOX_COLUMNS) * CELL, GY + (i / IN_BOX_COLUMNS) * CELL, CELL, CELL);
    if (sS.cursorArea == NATIVE_STORAGE_IN_BOX && !sSliding && !sS.holdingMon && sS.heldItem == ITEM_NONE && sS.phase != NATIVE_STORAGE_LOADING)
    {
        int cx = GX + (sS.cursorPos % IN_BOX_COLUMNS) * CELL, cy = GY + (sS.cursorPos / IN_BOX_COLUMNS) * CELL;

        UiW_SelectFrame(UiM_Rounded4(36, 36, 3, 3, 3, 3), cx + 2, cy + 2, UI_NONE, UI_NONE, 0);
    }
}

/* ---------------------------------------------------------------- the party column */

static const struct UiMask *SlotMask(void)
{
    static const int sCuts[] = {0, 1, 4, 1, 1, 3, 2, 1, 2, 3, 1, 1};

    return UiM_Carve(UiM_Capsule(150, 29, true, false), sCuts, 4, "pcslot");
}

static void Party(int t)
{
    struct UiRamp r = PcRamp();
    struct UiPattern stripes = UiPat_HStripe(2, 1, 0);
    bool canParty = sS.mode != NATIVE_STORAGE_WITHDRAW;
    char head[16];

    UiG_Paint(UiM_Rounded4(158, 216, 3, 3, 3, 3), PTX, PTY, r.base,
              &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, &stripes, UiMix(r.base, r.lo, 0.3)});
    UiG_Paint(UiM_Rounded(78, 14, UiCornerChamfer(5), UiCornerR(2), UiCornerR(0), UiCornerR(2)), PTX + 158 - 82, PTY + 4, r.lo,
              &(struct UiPaint){r.ink, r.base, UI_NONE, 0, 0, NULL, 0});
    snprintf(head, sizeof(head), "PARTY %d/6", sPartyCount);
    UiCaps_Draw(PTX + 158 - 10, PTY + 7, head, UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_RIGHT, UI_NONE);
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        const struct UiMon *m = &sParty[i];
        int x = PTX + 4, y = PTY + 22 + i * 32;
        bool on = sS.cursorArea == NATIVE_STORAGE_IN_PARTY && sS.cursorPos == i && sS.phase != NATIVE_STORAGE_LOADING;
        bool hold = sS.holdingMon || sS.heldItem != ITEM_NONE;
        const struct UiMask *sm = SlotMask();

        if (m->species == SPECIES_NONE)
        {
            UiG_Mask(UiM_Outline(sm), x, y, UiMix(r.hi, r.base, 0.4));
            if (on && !hold)
                UiW_SelectFrame(sm, x, y, UI_NONE, UI_NONE, 0);
            if (on && hold)
                UiText_Draw(UI_FONT_SMALL, x + 40, y + 8, sText_PutHere, UI_P_TEXT_W, r.ink, UI_ALIGN_LEFT, 0);
        }
        else
        {
            bool fnt = !m->isEgg && m->hp == 0;
            struct UiRamp rr = on ? (struct UiRamp){UI_R_SLOT_SEL_HI, UI_R_SLOT_SEL_BASE, UI_R_SLOT_SEL_LO, UI_R_SLOT_SEL_INK}
                             : fnt ? (struct UiRamp){UI_R_SLOT_FNT_HI, UI_R_SLOT_FNT_BASE, UI_R_SLOT_FNT_LO, UI_R_SLOT_FNT_INK}
                             : (struct UiRamp){UI_HEX(0xFFFFFF), UI_P_CREAM, UI_P_CREAM_LO, r.ink};
            static const struct UiFilter sGray = {0.65f, 0, 0, 0, 0};
            int fr = fnt ? 0 : (on ? (t / 10) % 2 : ((t + i * 7) / 20) % 2), nx;

            if (on && !hold)
                UiW_SelectFrame(sm, x, y, UI_NONE, UI_NONE, 0);
            UiG_Paint(sm, x, y, rr.base, &(struct UiPaint){rr.ink, rr.hi, rr.lo, 0, 0, NULL, 0});
            UiG_Save();
            UiG_Clip(x, y - 6, 40, 36);
            UiGfx_MonIcon(m->species, m->personality, m->isEgg, fr, x + 1, y - 5 - (on && fr ? 1 : 0), fnt ? &sGray : NULL);
            UiG_Restore();
            if (m->heldItem != ITEM_NONE)
                UiGfx_Icon("held_item", x + 27, y + 20, 0);
            if (UiText_Width(UI_FONT_NORMAL, m->nickname, 0) > 70)
                nx = UiText_Draw(UI_FONT_SMALL, x + 36, y + 1, m->nickname, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
            else
                nx = UiG_Text(x + 36, y - 1, m->nickname);
            if (!m->isEgg)
            {
                u8 lv[16];
                UiRGBA sc;
                const char *status = UiMon_StatusLabel(m, &sc);

                UiW_Gender(nx + 1, y - 1, m->gender);
                UiMon_LevelText(lv, sizeof(lv), m->level);
                UiText_Draw(UI_FONT_SMALL, x + 146, y + 1, lv, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
                UiW_HpBar(x + 36, y + 17, 64, m->maxHP > 0 ? (double)m->hp / m->maxHP : 0, false, UI_NONE);
                if (status != NULL)
                    UiW_StatusBadge(x + 108, y + 15, status, sc, 30);
                else if (i == 0)
                    UiGfx_Icon("footprints", x + 112, y + 17, 0);
            }
        }
        if (canParty)
            UiG_Hit(HIT_PARTY, i, x, y, 150, 29);
    }
}

static void Toolbar(void)
{
    struct UiRamp r = PcRamp();
    bool hold = sS.holdingMon || sS.heldItem != ITEM_NONE;
    bool partyBack = sS.partyShown && sS.mode != NATIVE_STORAGE_DEPOSIT;
    bool closeSel = (sS.cursorArea == NATIVE_STORAGE_BUTTONS && sS.cursorPos == 1)
                 || (sS.cursorArea == NATIVE_STORAGE_IN_PARTY && sS.cursorPos == PARTY_SIZE);
    struct UiDsButtonOpt left = {0}, right = {0};
    u8 v[24];
    int cx = BX + 118;

    /* the game's PARTY POKéMON (its party is already out in deposit mode) */
    left.noChip = true;
    left.selected = sS.cursorArea == NATIVE_STORAGE_BUTTONS && sS.cursorPos == 0 && !hold;
    left.disabled = sS.mode == NATIVE_STORAGE_DEPOSIT || sS.partyShown;
    UiW_DsButton(BX, 256, 112, 30, UiMix(UI_P_CMD_RUN, r.base, 0.35), sText_PartyPokemon, &left);
    if (!left.disabled)
        UiG_Hit(HIT_PARTY_BUTTON, 0, BX, 256, 112, 30);
    UiG_Paint(UiM_Rounded4(138, 27, 3, 3, 3, 3), cx, 256, UI_P_CREAM, &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    UiCaps_Draw(cx + 8, 261, "IN BOX", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    NumberText(v, sizeof(v), ShownBox()->count, 1);
    UiText_AppendAscii(v, sizeof(v), "/30");
    UiText_Draw(UI_FONT_SMALL, cx + 130, 258, v, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiCaps_Draw(cx + 8, 273, "ALL BOXES", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    NumberText(v, sizeof(v), sAllCount, 1);
    UiText_AppendAscii(v, sizeof(v), "/");
    UiText_AppendNumber(v, sizeof(v), TOTAL_BOXES_COUNT * IN_BOX_COUNT, 1, false);
    UiText_Draw(UI_FONT_SMALL, cx + 130, 270, v, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    /* the game's CLOSE BOX, or its party panel's button (back to the box when moving) */
    right.icon = "return_white";
    right.noChip = true;
    right.selected = closeSel && !hold;
    UiW_DsButton(PTX, 256, 158, 30, UI_P_CMD_FIGHT, partyBack ? sText_Cancel : sText_CloseBox, &right);
    UiG_Hit(HIT_CLOSE_BUTTON, 0, PTX, 256, 158, 30);
}

/* the hand over the cursor's place (closed, with what it carries, when moving) */
static void HandOver(int t)
{
    int x, y, bob = UiW_Nudge(t);

    if (sSliding || sS.phase == NATIVE_STORAGE_LOADING)
        return;
    switch (sS.cursorArea)
    {
    case NATIVE_STORAGE_IN_BOX:
        x = GX + (sS.cursorPos % IN_BOX_COLUMNS) * CELL;
        y = GY + (sS.cursorPos / IN_BOX_COLUMNS) * CELL;
        break;
    case NATIVE_STORAGE_IN_PARTY:
        if (sS.cursorPos < PARTY_SIZE)
        {
            x = PTX + 4;
            y = PTY + 22 + sS.cursorPos * 32 - 6;
        }
        else
        {
            x = PTX + 70;
            y = 248;
        }
        break;
    case NATIVE_STORAGE_BOX_TITLE:
        x = BX + 40;
        y = 14;
        break;
    default:
        x = sS.cursorPos == 0 ? BX + 44 : PTX + 70;
        y = 248;
        break;
    }
    if (sS.holdingMon)
    {
        int ix = x + 4, iy = y - 10 - bob;
        struct UiMon held;

        UiMon_FromPokemon(&held, &sS.heldMon);
        UiW_CarryFrame(UiM_Rounded4(30, 28, 4, 4, 4, 4), ix + 1, iy + 3);
        UiGfx_MonIcon(held.species, held.personality, held.isEgg, (t / 10) % 2, ix, iy, NULL);
        Hand(ix + 9, iy - 9, true);
    }
    else if (sS.heldItem != ITEM_NONE)
    {
        int ix = x + 6, iy = y - 8 - bob;

        UiW_CarryFrame(UiM_Rounded4(28, 28, 4, 4, 4, 4), ix - 2, iy - 2);
        UiGfx_ItemIcon(sS.heldItem, ix, iy, NULL);
        Hand(ix + 7, iy - 11, true);
    }
    else
    {
        Hand(x + 13, y - 11 - bob, false);
    }
}

/* ---------------------------------------------------------------- the message box and the windows */

static void StatusLines(u8 *l1, int s1, u8 *l2, int s2)
{
    const struct NativeStorageBox *b = ShownBox();

    l1[0] = l2[0] = EOS;
    if (sS.holdingMon)
    {
        struct UiMon held;

        UiMon_FromPokemon(&held, &sS.heldMon);
        UiText_Append(l1, s1, sText_MoveWhere1);
        UiText_Append(l1, s1, held.nickname);
        UiText_Append(l1, s1, sText_MoveWhere2);
        if (sSwapName[0] != EOS)
        {
            UiText_Append(l2, s2, sText_Swaps);
            UiText_Append(l2, s2, sSwapName);
            UiText_Append(l2, s2, sText_Period);
        }
        return;
    }
    if (sS.heldItem != ITEM_NONE)
    {
        UiText_Append(l1, s1, sHeldItemName);
        UiText_Append(l1, s1, sText_ItemInHand);
        UiText_Append(l2, s2, sText_ItemWhere);
        return;
    }
    switch (sS.cursorArea)
    {
    case NATIVE_STORAGE_BOX_TITLE:
        UiText_Append(l1, s1, sText_TitleHelp);
        UiText_Append(l2, s2, b->name);
        UiText_Append(l2, s2, sText_Dot);
        UiText_Append(l2, s2, UiStorage_WallName(b->wallpaper));
        UiText_Append(l2, s2, sText_Wallpaper);
        return;
    case NATIVE_STORAGE_BUTTONS:
        UiText_Append(l1, s1, sS.cursorPos == 0 ? sText_PartyHelp : sText_CloseHelp);
        return;
    case NATIVE_STORAGE_IN_PARTY:
        if (sS.cursorPos >= PARTY_SIZE)
        {
            UiText_Append(l1, s1, sS.mode == NATIVE_STORAGE_DEPOSIT ? sText_CloseHelp : sText_BackHelp);
            return;
        }
        if (!sD.has)
        {
            UiText_Append(l1, s1, sText_EmptyParty);
            return;
        }
        UiText_Append(l1, s1, sD.mon.nickname);
        UiText_Append(l1, s1, sText_IsInParty);
        return;
    default:
        if (!sD.has)
        {
            u8 n[8];

            UiText_Append(l1, s1, sText_EmptySpace);
            UiText_Append(l2, s2, b->name);
            UiText_AppendAscii(l2, s2, ": ");
            NumberText(n, sizeof(n), b->count, 1);
            UiText_Append(l2, s2, n);
            UiText_Append(l2, s2, sText_SpacesUsed);
            return;
        }
        UiText_Append(l1, s1, sD.mon.nickname);
        UiText_Append(l1, s1, sText_IsIn);
        UiText_Append(l1, s1, b->name);
        UiText_Append(l1, s1, sText_Period);
        return;
    }
}

static void MessageBox(int t)
{
    int mb[4];

    UiW_DialogBox(BX, 292, 422, 46, NULL, UI_NONE, mb);
    if (sS.messageShown && sS.message[0] != EOS)
    {
        UiW_MessagePage(sS.message, -1, mb[0] + 2, mb[1] - 1, mb[2] - 4, 2);
        if (sS.phase == NATIVE_STORAGE_MESSAGE)
            UiW_DownArrow(mb[0] + mb[2] - 8, mb[1] + 20 + ((t / 16) % 2), UI_P_CURSOR, false);
    }
    else if (sS.phase == NATIVE_STORAGE_MAIN || sS.phase == NATIVE_STORAGE_BUSY)
    {
        u8 l1[96], l2[96];

        StatusLines(l1, sizeof(l1), l2, sizeof(l2));
        UiG_Text(mb[0] + 2, mb[1] - 1, l1);
        if (l2[0] != EOS)
            UiText_Draw(UI_FONT_SMALL, mb[0] + 2, mb[1] + 17, l2, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    UiG_Hit(HIT_MESSAGE, 0, BX, 292, 422, 46);
}

static void Hints(void)
{
    struct UiHintItem h[6];
    int n = 0;

    switch (sS.phase)
    {
    case NATIVE_STORAGE_MAIN:
        if (sS.holdingMon)
        {
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_PutDown};
            if (sS.lrBoxes)
                h[n++] = (struct UiHintItem){UI_ACT_TABS, sText_TurnBox};
            h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sS.bPutsBack ? sText_PutBack : sText_Close};
            break;
        }
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Select};
        h[n++] = (struct UiHintItem){UI_ACT_SELECT, sS.quickMode ? sText_QuickOn : sText_Quick};
        if (sS.mode != NATIVE_STORAGE_DEPOSIT)
            h[n++] = (struct UiHintItem){UI_ACT_START, sText_BoxTitle};
        if (sS.lrBoxes && sS.mode != NATIVE_STORAGE_DEPOSIT)
            h[n++] = (struct UiHintItem){UI_ACT_TABS, sText_TurnBox};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Close};
        break;
    case NATIVE_STORAGE_MENU:
    case NATIVE_STORAGE_YES_NO:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_STORAGE_CHOOSE_BOX:
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Box};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_OK};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_STORAGE_MARKS:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Mark};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    case NATIVE_STORAGE_MESSAGE:
    case NATIVE_STORAGE_ITEM_INFO:
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        break;
    default:
        break;
    }
    if (n > 0)
        UiW_Hints(BX, 344, h, n, true, 9);
}

/* the game's choose-box window as the design's box list */
static void BoxList(int t)
{
    struct UiRamp r = PcRamp();
    int x = BX - 4, y = 30, w = 264, h = TOTAL_BOXES_COUNT * LIST_ROW + 16;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
    UiG_Paint(UiM_Rounded4(w - 10, h - 10, 2, 2, 2, 2), x + 5, y + 5, UI_P_CREAM, &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    for (int i = 0; i < TOTAL_BOXES_COUNT; i++)
    {
        int ry = y + 8 + i * LIST_ROW;
        bool on = sS.chooseBox == i;
        struct UiRamp wr = UiStorage_WallRamp(sListWalls[i], sS.waldaColors);
        u8 n[16];

        if (on)
            UiG_Paint(UiM_Rounded(w - 22, LIST_ROW, UiCornerR(2), UiCornerChamfer(5), UiCornerR(2), UiCornerR(2)), x + 14, ry,
                      UiMix(wr.base, UI_HEX(0xFFFFFF), 0.6), &(struct UiPaint){wr.ink, UiMix(wr.base, UI_HEX(0xFFFFFF), 0.85), UI_NONE, 0, 0, NULL, 0});
        UiG_BlitPart(UiStorage_Wall(sListWalls[i], sS.waldaColors), 96, 120, 30, 13, x + 19, ry + 2, 1, false, 1.0);
        UiG_Mask(UiM_Outline(UiM_Rect(32, 15)), x + 18, ry + 1, wr.ink);
        UiG_Text(x + 56, ry + 1, sListNames[i]);
        NumberText(n, sizeof(n), sListCounts[i], 1);
        UiText_AppendAscii(n, sizeof(n), "/30");
        UiText_Draw(UI_FONT_SMALL, x + w - 14, ry + 3, n, sListCounts[i] == IN_BOX_COUNT ? UI_P_TEXT_RED : UI_P_TEXT,
                    sListCounts[i] == IN_BOX_COUNT ? UI_P_TEXT_RED_SH : UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        if (i == sS.box)
            UiCaps_Draw(x + w - 52, ry + 6, "HERE", wr.base, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        if (on)
            UiW_Cursor(x + 8, ry + 4, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_BOXROW, i, x + 8, ry, w - 16, LIST_ROW);
    }
}

static void MarkWindow(int t)
{
    int w = 196, h = 64, x = 632 - w, y = 288 - h;
    static const u8 *const sLabels[2] = {sText_OK, sText_Cancel};

    UiW_DialogBox(x, y, w, h, NULL, UI_NONE, NULL);
    for (int i = 0; i < 4; i++)
    {
        int cx = x + 14 + i * 26;
        const struct UiMask *m = UiM_Rounded4(20, 20, 2, 2, 2, 2);

        if (sS.markCursor == i)
            UiW_SelectFrame(m, cx, y + 10, UI_NONE, UI_NONE, 0);
        UiG_Paint(m, cx, y + 10, sS.marks[i] ? UiMix(UI_P_CMD_RUN, UI_HEX(0xFFFFFF), 0.75) : UI_P_PAPER,
                  &(struct UiPaint){UI_P_INK, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiMon_MarkGlyph(i, cx + 7, y + 17, sS.marks[i] ? UI_P_INK : UI_P_CREAM_LO);
        UiG_Hit(HIT_MARK, i, cx, y + 10, 20, 20);
    }
    for (int k = 0; k < 2; k++)
    {
        int bx = x + 120, by = y + 8 + k * 24;

        if (sS.markCursor == 4 + k)
            UiW_Cursor(bx - 9, by + 2, UiW_Nudge(t), UI_NONE);
        UiG_Text(bx, by, sLabels[k]);
        UiG_Hit(HIT_MARK, 4 + k, bx - 10, by, 70, 18);
    }
    UiText_Draw(UI_FONT_SMALL, x + 14, y + 38, sText_ToggleMarks, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

/* MOVE ITEMS: INFO shows the item's description */
static void ItemInfo(void)
{
    u16 item = sS.heldItem != ITEM_NONE ? sS.heldItem : (sD.has ? sD.mon.heldItem : ITEM_NONE);
    int inner[4];

    if (item == ITEM_NONE)
        return;
    UiW_DialogBox(8, 174, 194, 112, NULL, UI_NONE, inner);
    UiGfx_ItemIcon(item, inner[0], inner[1], NULL);
    UiG_Text(inner[0] + 28, inner[1] + 2, GetItemName(item));
    UiW_MessagePage(GetItemDescription(item), -1, inner[0], inner[1] + 28, inner[2], 4);
}

static void Draw(int t)
{
    struct UiRamp r = PcRamp();
    struct UiPattern dots = UiPat_Dots(8, 1, 0, 0, false), stripe = UiPat_HStripe(64, 1, 0);
    int x0, y0, w, h;

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.64));
    UiG_FillPattern(&dots, x0, y0, w, h, UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.5), 0, 0);
    UiG_FillPattern(&stripe, x0, y0, w, h, UiMix(UI_P_TYPES_ICE, UI_P_INK, 0.57), 0, 0);
    UiW_TitleTab(0, 8, "POK" UI_CAPS_E_ACUTE "MON STORAGE", &r, 0, NULL);
    Monitor(t);
    Box(t);
    Party(t);
    Toolbar();
    HandOver(t);
    MessageBox(t);
    Hints();
    if (sS.phase == NATIVE_STORAGE_MENU || sS.phase == NATIVE_STORAGE_YES_NO || sS.phase == NATIVE_STORAGE_CHOOSE_BOX
     || sS.phase == NATIVE_STORAGE_MARKS)
    {
        int bx, by, bw, bh;

        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Hit(HIT_BACKDROP, 0, bx, by, bw, bh);
    }
    switch (sS.phase)
    {
    case NATIVE_STORAGE_MENU:
    {
        struct UiMenuItem items[7];
        struct UiMenuOpt o = {0};
        int box[4];

        for (int i = 0; i < sS.menuCount; i++)
            items[i] = (struct UiMenuItem){sS.menuLabels[i], false};
        o.anchor = UI_ANCHOR_BOTTOMRIGHT;
        o.t = t;
        o.minW = 104;
        UiW_MenuBox(632, 288, items, sS.menuCount, sS.menuCursor, &o, box);
        /* a wallpaper menu: the highlighted wallpaper's swatch beside the list */
        if (sS.menuCursor < sS.menuCount && sS.menuWallpaper[sS.menuCursor] >= 0)
        {
            int k = sS.menuWallpaper[sS.menuCursor], sx = box[0] - 92, sy = box[1] + box[3] - 70;
            struct UiRamp wr = UiStorage_WallRamp(k, sS.waldaColors);

            UiG_Paint(UiM_Rounded4(88, 70, 3, 3, 3, 3), sx, sy, wr.base, &(struct UiPaint){wr.ink, wr.hi, wr.lo, 0, 0, NULL, 0});
            UiG_BlitPart(UiStorage_Wall(k, sS.waldaColors), 80, 70, 80, 62, sx + 4, sy + 4, 1, false, 1.0);
        }
        break;
    }
    case NATIVE_STORAGE_YES_NO:
        UiW_YesNo(632, 288, sS.menuCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
        break;
    case NATIVE_STORAGE_CHOOSE_BOX:
        BoxList(t);
        break;
    case NATIVE_STORAGE_MARKS:
        MarkWindow(t);
        break;
    case NATIVE_STORAGE_ITEM_INFO:
        ItemInfo();
        break;
    default:
        break;
    }
    /* fades: the game's palette fades (to and from the summary, naming, the bag),
     * the opening (the game's own effect is a growing window) and the closing */
    if (t < 10)
        UiG_Veil(UI_RGBA(0, 0, 0, 255 * (10 - t) / 10));
    if (sS.phase == NATIVE_STORAGE_CLOSING && sClosingT > 0)
        UiG_Veil(UI_RGBA(0, 0, 0, sClosingT >= 12 ? 255 : 255 * sClosingT / 12));
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (NativeUi_PressesPending() || sGoal.on)
        return true;
    switch (sS.phase)
    {
    case NATIVE_STORAGE_MAIN:
    case NATIVE_STORAGE_BUSY:
        switch (hit->id)
        {
        case HIT_SLOT:
            SetGoal(NATIVE_STORAGE_IN_BOX, hit->index, A_BUTTON);
            return true;
        case HIT_PARTY:
            if (sS.mode == NATIVE_STORAGE_WITHDRAW)
                return true;
            SetGoal(NATIVE_STORAGE_IN_PARTY, hit->index, A_BUTTON);
            return true;
        case HIT_PLATE:
            SetGoal(NATIVE_STORAGE_BOX_TITLE, 0, A_BUTTON);
            return true;
        case HIT_ARROW:
            SetGoal(NATIVE_STORAGE_BOX_TITLE, 0, hit->index == 0 ? DPAD_LEFT : DPAD_RIGHT);
            return true;
        case HIT_PARTY_BUTTON:
            SetGoal(NATIVE_STORAGE_BUTTONS, 0, A_BUTTON);
            return true;
        case HIT_CLOSE_BUTTON:
            if (sS.partyShown)
                SetGoal(NATIVE_STORAGE_IN_PARTY, PARTY_SIZE, A_BUTTON);
            else
                SetGoal(NATIVE_STORAGE_BUTTONS, 1, A_BUTTON);
            return true;
        default:
            return false;
        }
    case NATIVE_STORAGE_MENU:
        if (hit->id == UI_HIT_MENU)
        {
            NativeUi_QueuePresses(hit->index > sS.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sS.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_STORAGE_YES_NO:
        if (hit->id == UI_HIT_YESNO)
        {
            NativeUi_QueuePresses(hit->index > sS.menuCursor ? DPAD_DOWN : DPAD_UP, abs(hit->index - sS.menuCursor));
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_STORAGE_CHOOSE_BOX:
        if (hit->id == HIT_BOXROW)
        {
            int right = (hit->index - sS.chooseBox + TOTAL_BOXES_COUNT) % TOTAL_BOXES_COUNT;

            if (right <= TOTAL_BOXES_COUNT - right)
                NativeUi_QueuePresses(DPAD_RIGHT, right);
            else
                NativeUi_QueuePresses(DPAD_LEFT, TOTAL_BOXES_COUNT - right);
            NativeUi_QueuePress(A_BUTTON);
        }
        else
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_STORAGE_MARKS:
        if (hit->id == HIT_MARK)
        {
            int down = (hit->index - sS.markCursor + 6) % 6;

            if (down <= 6 - down)
                NativeUi_QueuePresses(DPAD_DOWN, down);
            else
                NativeUi_QueuePresses(DPAD_UP, 6 - down);
            NativeUi_QueuePress(A_BUTTON);
        }
        else if (hit->id == HIT_BACKDROP)
        {
            NativeUi_QueuePress(B_BUTTON);
        }
        return true;
    case NATIVE_STORAGE_MESSAGE:
    case NATIVE_STORAGE_ITEM_INFO:
        NativeUi_QueuePress(A_BUTTON);
        return true;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (NativeUi_PressesPending() || sGoal.on)
        return true;
    if (sS.phase == NATIVE_STORAGE_MENU || sS.phase == NATIVE_STORAGE_YES_NO || sS.phase == NATIVE_STORAGE_MARKS)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    else if (sS.phase == NATIVE_STORAGE_CHOOSE_BOX)
        NativeUi_QueuePress(dy > 0 ? DPAD_LEFT : DPAD_RIGHT);
    return true;
}

static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    if (newKeys != 0)
        sGoal.on = false;   /* the player's own buttons take over */
    return keys;
}

static bool Ready(void)
{
    return sS.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Storage = {
    .name = "storage",
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
