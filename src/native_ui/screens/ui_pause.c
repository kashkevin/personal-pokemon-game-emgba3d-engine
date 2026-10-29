#ifdef ENABLE_NATIVE_UI
/*
 * 4 · Pause menu and 10 · Save (the approved design's preview/js/screens/
 * pause.js and save.js): the field menu with the Emerald location sign, the
 * link device and the entry column; and the save journal with the save's
 * summary over the field. The engine's start menu and save dialog
 * (start_menu.c) keep running under them and keep every rule: the entries
 * this game offers here (POKéNAV, the player submenu, Safari and Pyramid
 * menus...), the cursor, what A and B do, the save itself. The game is
 * reported as saved only once the engine says so and the save file is
 * written. Project-owned.
 * Rustboro's PokéNav tutorial draws a START menu of its own (script_menu.c, MULTI_FORCED_START_MENU): it is shown as
 * this menu too, its eight rows the game's (only POKéNAV goes on; the script asks again for any other row or B).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "main.h"
#include "menu.h"
#include "pokedex.h"
#include "multiplayer.h"
#include "overworld.h"
#include "pokemon.h"
#include "region_map.h"
#include "rtc.h"
#include "safari_zone.h"
#include "string_util.h"
#include "platform/mp_bridge.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/script_menu.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ENTRY = UI_HIT_SCREEN,
    HIT_LINK,
    HIT_MESSAGE,
};

/* the design's link-device colours (pause.js; its wood: UiW_LocationSign) */
#define LCD_LIGHT UI_HEX(0xC8E4A0)
#define LCD_MID UI_HEX(0x9CC47C)
#define LCD_DARK UI_HEX(0x2C4A2A)
#define LCD_SCAN UI_HEX(0xBCDA94)
/* the panel opens in 6 frames (art direction §9) */
static const double sSlide[6] = {0, 0.45, 0.75, 0.9, 0.97, 1};

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_Pokedex[] = _("Pokédex");
static const u8 sText_Pokemon[] = _("Pokémon");
static const u8 sText_Bag[] = _("Bag");
static const u8 sText_PokeNav[] = _("PokéNav");
static const u8 sText_Save[] = _("Save");
static const u8 sText_Options[] = _("Options");
static const u8 sText_Exit[] = _("Exit");
static const u8 sText_Retire[] = _("Retire");
static const u8 sText_Rest[] = _("Rest");
static const u8 sText_Debug[] = _("Debug");
static const u8 sText_DexNav[] = _("DexNav");
static const u8 sText_Card[] = _("Trainer Card");
static const u8 sText_Players[] = _("Players");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Ride[] = _("Ride");
static const u8 sText_GetOff[] = _("Get off");
static const u8 sText_Style[] = _("Style");
static const u8 sText_Tower[] = _("Tower");
static const u8 sText_SoftReset[] = _("Soft reset");
static const u8 sText_Open[] = _("Open");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Select[] = _("Select");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Wait[] = _("Please wait…");

static const u8 sHelp_Pokedex[] = _("Look up the Pokémon you have seen and caught.");
static const u8 sHelp_Pokemon[] = _("Check and organize the Pokémon in your party.");
static const u8 sHelp_Bag[] = _("Look at the items you are carrying.");
static const u8 sHelp_PokeNav[] = _("Open the PokéNav: map, condition and Match Call.");
static const u8 sHelp_Player[] = _("Your Trainer Card, the other players, the Tower, your style.");
static const u8 sHelp_Save[] = _("Save your progress on this PC.");
static const u8 sHelp_SaveLinked[] = _("Save your progress. Your friends keep playing.");
/* MS-61: with the launcher's host save page an in-game SAVE in a world is the host's save as well */
static const u8 sHelp_SaveHost[] = _("Save your progress here and on the host. Friends keep playing.");
static const u8 sHelp_Options[] = _("Text speed, battle scene, sound, buttons, frame.");
static const u8 sHelp_Exit[] = _("Close this menu.");
static const u8 sHelp_Retire[] = _("Leave the challenge you are taking part in.");
static const u8 sHelp_Rest[] = _("Save and stop the challenge for now.");
static const u8 sHelp_Debug[] = _("The development menu of this build.");
static const u8 sHelp_DexNav[] = _("Search for the Pokémon living in this area.");
static const u8 sHelp_Card[] = _("Open your Trainer Card: badges, records and play time.");
static const u8 sHelp_Players[] = _("See who is nearby and send a trade or battle request.");
static const u8 sHelp_Back[] = _("Back to the menu.");
static const u8 sHelp_Ride[] = _("Ride on your Pokémon.");
static const u8 sHelp_GetOff[] = _("Get off your Pokémon.");
static const u8 sHelp_Style[] = _("Change your colours: skin, hair, outfit, hat and bag.");
static const u8 sHelp_Tower[] = _("Battle Tower: Legends. 100 floors, alone or with a friend.");
static const u8 sHelp_SoftReset[] = _("Back to your last save (the key: Ctrl+R).");

static const u8 sText_LinkOffline[] = _("Not linked");
static const u8 sText_LinkAlone[] = _("No one else");
static const u8 sText_LinkWaiting[] = _("Waiting for friends");
static const u8 sText_LinkSetup[] = _("Save, then close the game");
static const u8 sText_LinkStarting[] = _("Starting the world…");
static const u8 sText_LinkJoining[] = _("Joining the world…");
static const u8 sText_LinkSeeLauncher[] = _("See the launcher window");
static const u8 sText_Here[] = _("Here");
static const u8 sText_Elsewhere[] = _("Elsewhere");
static const u8 sText_Trainer[] = _("Trainer ");

static const u8 sText_Saving[] = _("Saving… Don't close the game window.");
static const u8 sText_FileFailed[] = _("The game was saved, but the save file could not be\nwritten. Your progress is NOT safe on this PC.");
static const u8 sText_JustNow[] = _("in-game save, just now");
static const u8 sText_NotYet[] = _("not saved since the game started");
static const u8 sText_Writing[] = _("writing…");
static const u8 sText_HostLauncher[] = _("checkpoints are the launcher's");
static const u8 sText_HostToo[] = _("this save goes there too");
static const u8 sText_NoSession[] = _("not in a multiplayer session");
static const u8 sText_AutoLauncher[] = _("set per character in the launcher");
static const u8 sText_Dash[] = _("-");
static const u8 sText_Launcher[] = _("launcher");

/* ---------------------------------------------------------------- captured state */

struct Entry
{
    u8 action;          /* enum NativeStartMenuAction */
    s8 engineIndex;     /* the engine's row, -1 for a row of the design only */
};

static struct NativeStartMenu sMenu;
static bool sForced;            /* Rustboro's PokéNav tutorial: its script's START menu (no wrap) */
static struct Entry sEntries[12];
static int sEntryCount;
static int sSelectedEntry;
static int sOpen;               /* frames since the menu came up (the panel's slide) */
static u8 sMapName[32];
static u8 sClock[12];
static bool sLinked;
static bool sConnected;
static u8 sLinkState;           /* enum MpLinkState */
static char sLinkRole;          /* MpBridge_Role: 'h', 'j' or 0 */
static int sPeerCount;
static int sPeersHere;
static u32 sPeerId[MULTIPLAYER_MAX_PEERS];
static u8 sPeerGender[MULTIPLAYER_MAX_PEERS];
static bool sPeerHere[MULTIPLAYER_MAX_PEERS];
static u16 sLeadSpecies;
static u32 sLeadPersonality;
static bool sLeadEgg;
static bool sSafari;
static u8 sSafariBalls;

/* the save dialog */
static u8 sMsgText[UI_MESSAGE_COPY];        /* the engine's message as printed */
static bool sHasMsg;
static u32 sMsgPrinted;         /* its bytes on screen so far */
static bool sMsgPrinting;
static bool sMsgWaiting;         /* the printer waits for a button (a page break) */
static u8 sYesNoCursor;
static int sSaveT;               /* frames in the saving step */
static u32 sWritesAtSave;        /* save file writes when the save began */
static u32 sFailsAtSave;
static int sSaveResult;          /* 0 not yet, 1 written, 2 engine error, 3 file not written */
static u8 sSavedClock[12];       /* the game clock when this run's last save was written */
static bool sSavedThisRun;
static int sStampT;
static u8 sPlayerName[PLAYER_NAME_LENGTH + 1];
static u16 sHours;
static u8 sMinutes;
static bool sHasDex;
static u16 sDexCaught;
static u8 sBadges[8];
static u8 sPrevStep;

static void ClockText(u8 *dst, int size, int hours, int minutes)
{
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, (hours % 12) == 0 ? 12 : hours % 12, 1, false);
    UiText_AppendAscii(dst, size, ":");
    UiText_AppendNumber(dst, size, minutes, 2, false);
    UiText_AppendAscii(dst, size, hours < 12 ? "AM" : "PM");
}

/* Rustboro's PokéNav tutorial: its script's START menu (script_menu.c CreateStartMenuForPokenavTutorial), as the field
 * menu: its rows the game's eight, its cursor and readiness the multichoice's */
static bool ForcedMenu(struct NativeStartMenu *m)
{
    static const u8 actions[] = {NATIVE_START_POKEDEX, NATIVE_START_POKEMON, NATIVE_START_BAG, NATIVE_START_POKENAV,
                                 NATIVE_START_PLAYER, NATIVE_START_SAVE, NATIVE_START_OPTION, NATIVE_START_EXIT};
    struct NativeScriptMenu s;

    if (gMain.callback2 != CB2_Overworld || !ScriptMenu_NativeUiState(&s) || s.kind != NATIVE_SCRIPT_MENU_MULTICHOICE
     || s.multichoiceId != MULTI_FORCED_START_MENU)
        return false;
    memset(m, 0, sizeof(*m));
    m->mode = NATIVE_START_MENU_BROWSE;
    m->count = ARRAY_COUNT(actions);
    for (int i = 0; i < m->count; i++)
    {
        m->actions[i] = actions[i];
        m->labels[i] = i < s.count ? s.labels[i] : NULL;
    }
    m->cursor = s.cursor;
    m->ready = s.ready;
    return true;
}

static bool Detect(void)
{
    struct NativeStartMenu m;

    if (!StartMenu_NativeUiState(&m))
        return ForcedMenu(&m);
    /* a Battle Pyramid retire and the like keep the game's own windows */
    return m.mode != NATIVE_START_MENU_NONE && m.mode != NATIVE_START_MENU_OTHER;
}

static void BuildEntries(void)
{
    sEntryCount = 0;
    for (int i = 0; i < sMenu.count && sEntryCount < (int)ARRAY_COUNT(sEntries) - 1; i++)
    {
        sEntries[sEntryCount].action = sMenu.actions[i];
        sEntries[sEntryCount].engineIndex = (s8)i;
        sEntryCount++;
    }
    sSelectedEntry = -1;
    for (int i = 0; i < sEntryCount; i++)
        if (sEntries[i].engineIndex == sMenu.cursor)
            sSelectedEntry = i;
}

static void CaptureSave(void)
{
    u32 written, failed;
    bool8 pending;
    bool8 active;

    Platform_NativeUiSaveWrites(&written, &failed, &pending);
    if (sMenu.saveStep == NATIVE_SAVE_SAVING && sPrevStep != NATIVE_SAVE_SAVING)
    {
        sSaveT = 0;
        sWritesAtSave = written;
        sFailsAtSave = failed;
        sSaveResult = 0;
    }
    if (sMenu.saveStep == NATIVE_SAVE_SAVING || (sMenu.saveStep == NATIVE_SAVE_SAVED && sSaveResult == 0))
        sSaveT++;
    if (sMenu.saveStep == NATIVE_SAVE_SAVED && sSaveResult == 0 && !pending)
    {
        /* the engine says saved: the file must be written too */
        if (failed != sFailsAtSave)
        {
            sSaveResult = 3;
        }
        else if (written != sWritesAtSave)
        {
            sSaveResult = 1;
            sStampT = 1;
            sSavedThisRun = true;
            ClockText(sSavedClock, sizeof(sSavedClock), gLocalTime.hours, gLocalTime.minutes);
        }
    }
    if (sMenu.saveStep == NATIVE_SAVE_FAILED)
        sSaveResult = 2;
    if (sStampT > 0 && sStampT < 9)
        sStampT++;
    sHasMsg = TextPrinter_NativeUiWindowText(0, sMsgText, sizeof(sMsgText), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sMsgPrinting && TextPrinter_NativeUiWindowWaiting(0);
    sYesNoCursor = Menu_GetCursorPos();
    sHours = gSaveBlock2Ptr->playTimeHours;
    sMinutes = gSaveBlock2Ptr->playTimeMinutes;
    sHasDex = FlagGet(FLAG_SYS_POKEDEX_GET) == TRUE;
    if (sHasDex)
        sDexCaught = IsNationalPokedexEnabled() ? GetNationalPokedexCount(FLAG_GET_CAUGHT)
                                                : GetRegionalPokedexCount(FLAG_GET_CAUGHT);
    for (int i = 0; i < 8; i++)
        sBadges[i] = FlagGet(FLAG_BADGE01_GET + i) == TRUE;
}

static void Capture(void)
{
    u8 prevMode = sMenu.mode;

    sPrevStep = sMenu.saveStep;
    sForced = !StartMenu_NativeUiState(&sMenu) && ForcedMenu(&sMenu);
    if (prevMode == NATIVE_START_MENU_NONE || prevMode == NATIVE_START_MENU_OTHER)
        sOpen = 0;
    else if (sOpen < 5)
        sOpen++;
    BuildEntries();
    StringCopy_PlayerName(sPlayerName, gSaveBlock2Ptr->playerName);
    GetMapName(sMapName, gMapHeader.regionMapSectionId, 0);
    ClockText(sClock, sizeof(sClock), gLocalTime.hours, gLocalTime.minutes);
    sLinked = MpBridge_IsEnabled();
    sConnected = Multiplayer_IsConnected();
    sLinkState = MpBridge_LinkState();
    sLinkRole = MpBridge_Role();
    sPeerCount = sPeersHere = 0;
    for (u32 i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        u8 group, num, gender, activity;
        u32 id;

        if (!Multiplayer_NativeUiPeer(i, &id, &group, &num, &gender, &activity))
            continue;
        sPeerId[sPeerCount] = id;
        sPeerGender[sPeerCount] = gender;
        sPeerHere[sPeerCount] = activity != MULTIPLAYER_ACTIVITY_UNAVAILABLE
                             && group == gSaveBlock1Ptr->location.mapGroup && num == gSaveBlock1Ptr->location.mapNum;
        if (sPeerHere[sPeerCount])
            sPeersHere++;
        sPeerCount++;
    }
    sLeadSpecies = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES);
    sLeadPersonality = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_PERSONALITY);
    sLeadEgg = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG);
    sSafari = GetSafariZoneFlag();
    sSafariBalls = gNumSafariBalls;
    if (sMenu.mode == NATIVE_START_MENU_SAVE)
        CaptureSave();
    else
        sStampT = 0;
}

/* ---------------------------------------------------------------- the pause menu */

static const u8 *EntryLabel(const struct Entry *e)
{
    switch (e->action)
    {
    case NATIVE_START_POKEDEX: return sText_Pokedex;
    case NATIVE_START_POKEMON: return sText_Pokemon;
    case NATIVE_START_BAG: case NATIVE_START_PYRAMID_BAG: return sText_Bag;
    case NATIVE_START_POKENAV: return sText_PokeNav;
    case NATIVE_START_PLAYER: case NATIVE_START_PLAYER_LINK: case NATIVE_START_PLAYER_OPTIONS: return sPlayerName;
    case NATIVE_START_SAVE: return sText_Save;
    case NATIVE_START_OPTION: return sText_Options;
    case NATIVE_START_EXIT: return sText_Exit;
    case NATIVE_START_RETIRE_SAFARI: case NATIVE_START_RETIRE_FRONTIER: return sText_Retire;
    case NATIVE_START_REST_FRONTIER: return sText_Rest;
    case NATIVE_START_DEBUG: return sText_Debug;
    case NATIVE_START_DEXNAV: return sText_DexNav;
    case NATIVE_START_TRAINER_CARD: return sText_Card;
    case NATIVE_START_PLAYERS: return sText_Players;
    case NATIVE_START_PLAYER_BACK: return sText_Back;
    case NATIVE_START_RIDE: return sText_Ride;
    case NATIVE_START_GET_OFF: return sText_GetOff;
    case NATIVE_START_STYLE: return sText_Style;
    case NATIVE_START_TOWER: return sText_Tower;
    case NATIVE_START_SOFT_RESET: return sText_SoftReset;
    default: return sText_Exit;
    }
}

static const u8 *EntryHelp(const struct Entry *e)
{
    switch (e->action)
    {
    case NATIVE_START_POKEDEX: return sHelp_Pokedex;
    case NATIVE_START_POKEMON: return sHelp_Pokemon;
    case NATIVE_START_BAG: case NATIVE_START_PYRAMID_BAG: return sHelp_Bag;
    case NATIVE_START_POKENAV: return sHelp_PokeNav;
    case NATIVE_START_PLAYER_OPTIONS: return sHelp_Player;
    case NATIVE_START_PLAYER: case NATIVE_START_PLAYER_LINK: case NATIVE_START_TRAINER_CARD: return sHelp_Card;
    case NATIVE_START_SAVE: return !sLinked ? sHelp_Save : MpBridge_HostSavePageEnabled() ? sHelp_SaveHost
                                                                                      : sHelp_SaveLinked;
    case NATIVE_START_OPTION: return sHelp_Options;
    case NATIVE_START_RETIRE_SAFARI: case NATIVE_START_RETIRE_FRONTIER: return sHelp_Retire;
    case NATIVE_START_REST_FRONTIER: return sHelp_Rest;
    case NATIVE_START_DEBUG: return sHelp_Debug;
    case NATIVE_START_DEXNAV: return sHelp_DexNav;
    case NATIVE_START_PLAYERS: return sHelp_Players;
    case NATIVE_START_PLAYER_BACK: return sHelp_Back;
    case NATIVE_START_RIDE: return sHelp_Ride;
    case NATIVE_START_GET_OFF: return sHelp_GetOff;
    case NATIVE_START_STYLE: return sHelp_Style;
    case NATIVE_START_TOWER: return sHelp_Tower;
    case NATIVE_START_SOFT_RESET: return sHelp_SoftReset;
    default: return sHelp_Exit;
    }
}

static const char *EntryIcon(const struct Entry *e, bool *hasSel)
{
    *hasSel = true;
    switch (e->action)
    {
    case NATIVE_START_POKEDEX: return "dex";
    case NATIVE_START_BAG: case NATIVE_START_PYRAMID_BAG: return "bag";
    case NATIVE_START_POKENAV: return "map";
    case NATIVE_START_PLAYER: case NATIVE_START_PLAYER_LINK: case NATIVE_START_PLAYER_OPTIONS:
    case NATIVE_START_TRAINER_CARD: return "card";
    case NATIVE_START_SAVE: case NATIVE_START_REST_FRONTIER: return "save";
    case NATIVE_START_OPTION: return "options";
    case NATIVE_START_DEBUG: return "key";
    case NATIVE_START_PLAYERS: return "players";
    case NATIVE_START_RIDE: case NATIVE_START_GET_OFF: return "shoe";
    case NATIVE_START_RETIRE_SAFARI: case NATIVE_START_RETIRE_FRONTIER: return "flag";   // giving up, as the battle's forfeit
    case NATIVE_START_DEXNAV: *hasSel = false; return "search";
    case NATIVE_START_STYLE: *hasSel = false; return "head_green";
    case NATIVE_START_TOWER: return "burst";
    case NATIVE_START_SOFT_RESET: *hasSel = false; return "disc_red";
    default: *hasSel = false; return "return";
    }
}

// --selftest-start-menu (MS-61): the 3D menu names this row with its own words (the fallback is "Exit")
bool32 NativeUiPause_TestNamed(u8 action)
{
    struct Entry e = {action, 0};
    bool hasSel;

    return action == NATIVE_START_EXIT
        || (EntryLabel(&e) != sText_Exit && EntryHelp(&e) != sHelp_Exit && strcmp(EntryIcon(&e, &hasSel), "return") != 0)
        || action == NATIVE_START_PLAYER_BACK     // BACK: its own words, the return arrow
        // POKeMON: its own words, and the lead Pokemon's icon drawn in place of one (the row is shown once there is one)
        || (action == NATIVE_START_POKEMON && EntryLabel(&e) == sText_Pokemon && EntryHelp(&e) == sHelp_Pokemon);
}

/* the Emerald wooden location sign (PK.locationSign), with the time */
static void LocationSign(int x, int y)
{
    char clock[16];

    UiText_ToAscii(clock, sizeof(clock), sClock);
    UiW_LocationSign(x, y, sMapName, clock);
}

/* the link device: the session's other players as the game knows them */
static void LinkPanel(int x, int y)
{
    int w = 184, h = 82, yy = y + 21;
    struct UiPattern scan = UiPat_HStripe(2, 1, 0);
    char count[24];
    const u8 *note = sText_LinkOffline;

    UiG_Paint(UiM_Rounded4(w, h, 5, 5, 5, 5), x, y, UI_R_FIELD_BASE,
              &(struct UiPaint){UI_P_FIELD_INK, UI_R_FIELD_HI, UI_R_FIELD_LO, 0, 2, NULL, UI_NONE});
    for (int i = 0; i < 3; i++)
    {
        UiG_Put(x + w - 8, y + 30 + i * 4, UI_R_FIELD_LO);
        UiG_Put(x + w - 6, y + 32 + i * 4, UI_R_FIELD_LO);
    }
    UiG_Paint(UiM_Rounded4(w - 20, h - 12, 2, 2, 2, 2), x + 5, y + 5, LCD_LIGHT,
              &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_NONE, 0, 0, &scan, LCD_SCAN});
    UiCaps_Draw(x + 10, y + 9, "LINK", LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    /* what the launcher made of this game (MpBridge_LinkState): a host's own game says HOSTING once its world is up */
    switch (sLinkState)
    {
    case MP_LINK_SETUP:
        snprintf(count, sizeof(count), "SETUP");
        note = sText_LinkSetup;
        break;
    case MP_LINK_CONNECTING:
        snprintf(count, sizeof(count), "CONNECTING");
        note = sLinkRole == 'h' ? sText_LinkStarting : sText_LinkJoining;
        break;
    case MP_LINK_NOT_CONNECTED:
        snprintf(count, sizeof(count), "NOT CONNECTED");
        note = sText_LinkSeeLauncher;
        break;
    case MP_LINK_LOST:
        snprintf(count, sizeof(count), "LINK LOST");
        note = sText_LinkSeeLauncher;
        break;
    case MP_LINK_CONNECTED:
        if (sPeerCount > 0)
            snprintf(count, sizeof(count), "%s%d NEARBY", sLinkRole == 'h' ? "HOST " : "", sPeersHere);
        else
            snprintf(count, sizeof(count), "%s", sLinkRole == 'h' ? "HOSTING" : "CONNECTED");
        note = sLinkRole == 'h' ? sText_LinkWaiting : sText_LinkAlone;
        break;
    default:
        snprintf(count, sizeof(count), "OFFLINE");
        break;
    }
    UiCaps_Draw(x + w - 20, y + 9, count, LCD_DARK, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_HLine(x + 9, y + 18, w - 28, LCD_MID);
    if (!sConnected || sPeerCount == 0)
        UiText_Draw(UI_FONT_NARROW, x + 12, yy - 2, note, LCD_DARK, LCD_MID, UI_ALIGN_LEFT, 0);
    for (int i = 0; i < sPeerCount && i < 3; i++)
    {
        u8 name[24];

        name[0] = EOS;
        UiText_Append(name, sizeof(name), sText_Trainer);
        UiText_AppendNumber(name, sizeof(name), (long)(sPeerId[i] % 10000), 4, false);
        UiGfx_Icon(sPeerGender[i] == FEMALE ? "lcd_head_red" : "lcd_head_blue", x + 9, yy + 1, 0);
        UiText_Draw(UI_FONT_NARROW, x + 26, yy - 2, name, LCD_DARK, LCD_MID, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_SMALL, x + 112, yy, sPeerHere[i] ? sText_Here : sText_Elsewhere, LCD_DARK, UI_NONE,
                    UI_ALIGN_LEFT, 0);
        yy += 17;
    }
    UiG_Hit(HIT_LINK, 0, x, y, w, h);
}

static void PauseMenu(int t, int L, int T, int R, int B)
{
    int rowH = 26, w = 168, h = sEntryCount * rowH + 14;
    int x = R - w + (int)floor(w * (1 - sSlide[sOpen < 5 ? sOpen : 5]) + 0.5), y = T + 8;
    int hb[4];

    LocationSign(L + 8, T + 8);
    LinkPanel(L + 8, T + 48);
    if (sSafari)
    {
        char balls[24];

        snprintf(balls, sizeof(balls), "SAFARI BALLS %d", sSafariBalls);
        UiW_DialogBox(L + 8, T + 136, 184, 24, NULL, UI_NONE, NULL);
        UiCaps_Draw(L + 18, T + 145, balls, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    UiG_Paint(UiM_Rounded4(w, h, 4, 0, 4, 0), x, y, UI_R_FIELD_BASE,
              &(struct UiPaint){UI_P_FIELD_INK, UI_R_FIELD_HI, UI_R_FIELD_LO, 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 6, h - 10, 3, 0, 3, 0), x + 5, y + 5, UI_P_CREAM,
              &(struct UiPaint){UI_P_FIELD_INK, UI_NONE, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int i = 0; i < sEntryCount; i++)
    {
        const struct Entry *e = &sEntries[i];
        int ry = y + 7 + i * rowH;
        bool on = i == sSelectedEntry, planned = e->engineIndex < 0;
        int hop = on ? 1 : 0;
        int lx;

        if (on)
        {
            const struct UiMask *rm = UiM_Rounded4(w - 12, rowH - 2, 2, 0, 2, 0);

            UiW_SelectFrame(rm, x + 8, ry, UI_NONE, UI_NONE, 2);
            UiG_Paint(rm, x + 8, ry, UI_HEX(0xD8F0C8),
                      &(struct UiPaint){UI_NONE, UI_HEX(0xF0FFE8), UI_HEX(0xB8DCA8), 0, 0, NULL, UI_NONE});
        }
        if (e->action == NATIVE_START_POKEMON && sLeadSpecies != 0)
        {
            UiG_Save();
            UiG_Clip(x + 10, ry, 28, 24);
            UiGfx_MonIcon(sLeadSpecies, sLeadPersonality, sLeadEgg, on ? (t / 10) % 2 : 0, x + 8, ry - 6 - hop, NULL);
            UiG_Restore();
        }
        else
        {
            bool hasSel;
            const char *icon = EntryIcon(e, &hasSel);
            char name[32];

            snprintf(name, sizeof(name), "%s%s", icon, on && hasSel ? "_sel" : "");
            if (planned)
            {
                UiG_Save();
                UiG_Alpha(0.5);
            }
            UiGfx_Icon(name, x + 12, ry + 1 - hop, 0);
            if (planned)
                UiG_Restore();
        }
        if (planned)
            lx = UiText_Draw(UI_FONT_NORMAL, x + 42, ry + 4, EntryLabel(e), UI_P_TEXT_MUTED, UI_P_TEXT_SH,
                             UI_ALIGN_LEFT, 0);
        else
            lx = UiG_Text(x + 42, ry + 4, EntryLabel(e));
        if (planned)
            UiW_Planned(lx + 4, ry + 7, NULL);
        else
            UiG_Hit(HIT_ENTRY, e->engineIndex, x + 8, ry, w - 12, rowH - 2);
    }
    UiW_DialogBox(L + 8, B - 44, 456, 38, NULL, UI_NONE, hb);
    if (sSelectedEntry >= 0)
        UiG_Text(hb[0] + 2, hb[1] - 1, EntryHelp(&sEntries[sSelectedEntry]));
    UiG_Paint(UiM_Rounded4(160, 38, 3, 0, 3, 0), R - 160, B - 44, UI_P_CREAM,
              &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    {
        int hx = UiW_Hint(R - 152, B - 31, UI_ACT_CONFIRM, sText_Open, false) + 8;

        UiW_Hint(hx, B - 31, UI_ACT_CANCEL, sText_Close, false);
    }
}

/* ---------------------------------------------------------------- the save journal */

#define COVER_BASE UI_HEX(0x3C78C8)
static const char *const sRing[] = {".ooo.", "o...o", "o...o", "o...o", ".ooo."};
static const char *const sClockIcon[] = {"..ooo..", ".o...o.", "o..o..o", "o..oo.o", "o.....o", ".o...o.", "..ooo.."};

static int JournalRow(int i, int rx, int vx, int y0, const char *label, const u8 *value, UiRGBA lc)
{
    int yy = y0 + i * 18;

    UiCaps_Draw(rx, yy + 7, label, lc, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (value != NULL)
        UiG_TextEx(vx, yy, value, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
    return yy;
}

static void BadgeRow(int x, int y, const struct UiRamp *r)
{
    for (int i = 0; i < 8; i++)
    {
        int bx = x + i * 18;
        const struct UiMask *sock = UiM_Circle(16);

        UiG_Paint(sock, bx, y, UiMix(r->lo, r->ink, 0.28),
                  &(struct UiPaint){r->ink, UiMix(r->lo, r->ink, 0.5), UiMix(r->base, r->hi, 0.5), 0, 0, NULL, UI_NONE});
        if (sBadges[i])
            UiG_Paint(UiM_Circle(10), bx + 3, y + 3, UI_HEX(0xF8D040),
                      &(struct UiPaint){UI_HEX(0x6A4410), UI_HEX(0xFFF4B0), UI_HEX(0xD09820), 0, 0, NULL, UI_NONE});
        else
            UiG_Put(bx + 7, y + 7, UiMix(r->lo, r->ink, 0.6));
    }
}

static void Journal(int jx, int jy)
{
    struct UiRamp cover = UiRampOf(COVER_BASE, 0.42, 0.28, 0.62);
    int jw = 272, jh = 188;
    int pgx = jx + 16, pgy = jy + 6, pgw = jw - 22, pgh = jh - 12;
    int rx = pgx + 32, vx = pgx + pgw - 8, y0 = pgy + 28;
    UiRGBA line = UI_HEX(0xC8D8EC), lc = UiMix(cover.lo, UI_P_TEXT_MUTED, 0.3);
    struct UiPixPal ringPal[1] = {{'o', UI_RGB(176, 180, 192)}};
    struct UiPixPal clockPal[1] = {{'o', cover.lo}};
    struct UiRamp cream = {UI_P_CREAM_HI, UI_P_CREAM, UI_P_CREAM_LO, UiMix(UI_P_CREAM_LO, cover.ink, 0.45)};
    bool writing = sMenu.saveStep == NATIVE_SAVE_SAVING || (sMenu.saveStep == NATIVE_SAVE_SAVED && sSaveResult == 0);
    u8 buf[24];
    int yy;

    UiG_Paint(UiM_Rounded4(jw, jh, 4, 4, 4, 4), jx, jy, cover.base,
              &(struct UiPaint){cover.ink, cover.hi, cover.lo, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(pgw, pgh, 1, 3, 1, 3), pgx, pgy, UI_P_CREAM,
              &(struct UiPaint){cover.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    for (int ly = pgy + 44; ly < pgy + pgh - 4; ly += 18)
        UiG_HLine(pgx + 2, ly, pgw - 4, line);
    UiG_HLine(pgx + 2, pgy + 27, pgw - 4, UiMix(line, cover.base, 0.4));
    UiG_HLine(pgx + 2, pgy + 29, pgw - 4, line);
    UiG_VLine(pgx + 26, pgy + 1, pgh - 2, UI_HEX(0xE8A0A0));
    for (int ry = jy + 12; ry < jy + jh - 10; ry += 14)
        UiW_Pix("sv_ring", sRing, 5, ringPal, 1, jx + 6, ry, false);
    UiGfx_Icon("save", pgx + 2, pgy + 3, 0);
    UiCaps_Draw(pgx + 32, pgy + 9, "SAVE", cover.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(pgx + pgw - 8, pgy + 9, sLinked ? "MULTIPLAYER" : "THIS PC", UiMix(cover.lo, UI_P_CREAM_LO, 0.4),
                UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    JournalRow(0, rx, vx, y0, "PLAYER", sPlayerName, lc);
    yy = JournalRow(1, rx, vx, y0, "BADGES", NULL, lc);
    BadgeRow(vx - 7 * 18 - 16, yy + 1, &cream);
    buf[0] = EOS;
    if (sHasDex)
        UiText_AppendNumber(buf, sizeof(buf), sDexCaught, 1, false);
    JournalRow(2, rx, vx, y0, "POK" UI_CAPS_E_ACUTE "DEX", sHasDex ? buf : sText_Dash, lc);
    buf[0] = EOS;
    UiText_AppendNumber(buf, sizeof(buf), sHours, 1, false);
    UiText_AppendAscii(buf, sizeof(buf), ":");
    UiText_AppendNumber(buf, sizeof(buf), sMinutes, 2, false);
    JournalRow(3, rx, vx, y0, "TIME", buf, lc);
    /* the two saves, kept apart: this PC's file, and the host's copy (the launcher's) */
    yy = JournalRow(4, rx, vx, y0, "THIS PC", NULL, lc);
    if (writing)
    {
        UiText_Draw(UI_FONT_SMALL, rx + 52, yy + 2, sText_Writing, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else
    {
        UiG_TextEx(vx, yy, sSavedThisRun ? sSavedClock : sText_Dash, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH,
                   UI_ALIGN_RIGHT);
        UiText_Draw(UI_FONT_SMALL, rx + 52, yy + 2, sSavedThisRun ? sText_JustNow : sText_NotYet, UI_P_TEXT_MUTED,
                    UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    yy = JournalRow(5, rx, vx, y0, "HOST", NULL, lc);
    UiG_TextEx(vx, yy, sLinked ? sText_Launcher : sText_Dash, UI_FONT_NORMAL, UI_P_TEXT_MUTED, UI_P_TEXT_SH,
               UI_ALIGN_RIGHT);
    UiText_Draw(UI_FONT_SMALL, rx + 52, yy + 2, !sLinked ? sText_NoSession
                                                : MpBridge_HostSavePageEnabled() ? sText_HostToo : sText_HostLauncher,
                UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    yy = JournalRow(6, rx, vx, y0, "AUTOSAVE", NULL, lc);
    UiW_Pix("sv_clock", sClockIcon, 7, clockPal, 1, pgx + 9, yy + 4, false);
    UiG_TextEx(vx, yy, sLinked ? sText_Launcher : sText_Dash, UI_FONT_NORMAL, UI_P_TEXT_MUTED, UI_P_TEXT_SH,
               UI_ALIGN_RIGHT);
    if (sLinked)
        UiText_Draw(UI_FONT_SMALL, rx + 52, yy + 2, sText_AutoLauncher, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    /* the stamp once the file is written; a chip when it is not */
    if (sSaveResult == 1 && sStampT > 0)
    {
        int press = sStampT <= 2 ? 1 : 0, sw = 74, sx = pgx + pgw - sw - 10, sty = pgy + pgh - 23 + press;
        const struct UiMask *r = UiM_Rounded4(sw, 20, 2, 2, 2, 2);
        char stamp[24];

        UiG_Mask(UiM_Sub(r, UiM_Erode4(UiM_Erode4(r))), sx, sty, UI_HEX(0xD04838));
        UiText_ToAscii(stamp, sizeof(stamp), sSavedClock);
        {
            char text[32];

            snprintf(text, sizeof(text), "SAVED %s", stamp);
            UiCaps_Draw(sx + (sw >> 1), sty + 7, text, UI_HEX(0xD04838), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER,
                        UI_NONE);
        }
    }
    else if (sSaveResult == 2 || sSaveResult == 3)
    {
        struct UiRamp chip = UiRampOf(UI_HEX(0xD04838), 0.42, 0.28, 0.62);
        const char *label = sSaveResult == 2 ? "NOT SAVED" : "FILE NOT WRITTEN";
        int cw = UiCaps_Width(label) + 14;

        UiG_Paint(UiM_Capsule(cw, 13, true, true), pgx + pgw - cw - 8, pgy + pgh - 19, chip.base,
                  &(struct UiPaint){chip.ink, chip.hi, chip.lo, 0, 0, NULL, UI_NONE});
        UiCaps_Draw(pgx + pgw - 8 - (cw >> 1), pgy + pgh - 16, label, UI_P_TEXT_W, chip.ink, UI_CAPS_SHADOW,
                    UI_ALIGN_CENTER, UI_NONE);
    }
}

static void SaveDialog(int t, int L, int T, int R, int B)
{
    struct UiRamp cover = UiRampOf(COVER_BASE, 0.42, 0.28, 0.62);
    int b[4];
    bool yesNo = sMenu.saveStep == NATIVE_SAVE_ASK_YES_NO || sMenu.saveStep == NATIVE_SAVE_OVERWRITE_YES_NO;
    bool writing = sMenu.saveStep == NATIVE_SAVE_SAVING || (sMenu.saveStep == NATIVE_SAVE_SAVED && sSaveResult == 0);
    bool waiting;

    LocationSign(L + 8, T + 8);
    Journal(L + 8, T + 48);
    UiW_DialogBox(L + 8, B - 70, 456, 62, NULL, UI_NONE, b);
    if (writing)
    {
        UiG_Text(b[0] + 2, b[1] - 1, sText_Saving);
        for (int k = 0; k < 12; k++)
            UiG_Rect(b[0] + 4 + k * 10, b[1] + 26, 8, 4, k < sSaveT / 8 ? cover.base : UI_P_PAPER_LO);
    }
    else if (sSaveResult == 3)
    {
        UiW_MessagePage(sText_FileFailed, -1, b[0] + 2, b[1] - 1, b[2] - 4, 3);
    }
    else if (sHasMsg)
    {
        UiW_MessagePage(sMsgText, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] - 1, b[2] - 4, 3);
    }
    waiting = !writing && !yesNo && (sMsgWaiting || (!sMsgPrinting
           && (sMenu.saveStep == NATIVE_SAVE_SAVED || sMenu.saveStep == NATIVE_SAVE_FAILED)));
    if (waiting)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 42 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_MESSAGE, 0, L + 8, B - 70, 456, 62);
    if (yesNo && !sMsgPrinting)
        UiW_YesNo(R - 8, B - 50, sYesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
    UiG_Paint(UiM_Rounded4(160, 38, 3, 0, 3, 0), R - 160, B - 44, UI_P_CREAM,
              &(struct UiPaint){cover.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    if (yesNo)
    {
        int hx = UiW_Hint(R - 152, B - 31, UI_ACT_CONFIRM, sText_Select, false) + 8;

        UiW_Hint(hx, B - 31, UI_ACT_CANCEL, sText_Back, false);
    }
    else if (writing || sMsgPrinting)
    {
        UiText_Draw(UI_FONT_SMALL, R - 150, B - 32, sText_Wait, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else
    {
        UiW_Hint(R - 152, B - 31, UI_ACT_CONFIRM, sMenu.saveStep == NATIVE_SAVE_SAVED ? sText_Next : sText_Close, false);
    }
}

/* ---------------------------------------------------------------- the screen */

static void Draw(int t)
{
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    if (sMenu.mode == NATIVE_START_MENU_SAVE)
        SaveDialog(t, bx, by, bx + bw, by + bh);
    else
        PauseMenu(t, bx, by, bx + bw, by + bh);
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || NativeUi_PressesPending())
        return button == 1;
    if (sMenu.mode == NATIVE_START_MENU_BROWSE && hit->id == HIT_ENTRY)
    {
        int moves = hit->index - sMenu.cursor;

        /* the engine's own menu wraps: the shorter way round (the tutorial's does not) */
        if (!sForced && moves > sMenu.count / 2)
            moves -= sMenu.count;
        else if (!sForced && moves < -sMenu.count / 2)
            moves += sMenu.count;
        NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (sMenu.mode == NATIVE_START_MENU_SAVE)
    {
        if (hit->id == UI_HIT_YESNO)
        {
            int moves = hit->index - sYesNoCursor;

            NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
        if (hit->id == HIT_MESSAGE)
        {
            NativeUi_QueuePress(A_BUTTON);
            return true;
        }
    }
    return false;
}

static bool Ready(void)
{
    return sMenu.ready;
}

static enum UiPicture Picture(void)
{
    /* the field stays; the game's windows (BG0) give way to these */
    return UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_Pause = {
    .name = "pause",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
