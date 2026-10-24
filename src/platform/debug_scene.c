/*
 * Development aid: see include/platform/debug_scene.h. Project-owned.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "new_game.h"
#include "overworld.h"
#include "event_data.h"
#include "random.h"
#include "region_map.h"
#include "pokenav.h"
#include "load_save.h"
#include "save.h"
#include "malloc.h"
#include "pokemon.h"
#include "task.h"
#include "scanline_effect.h"
#include "palette.h"
#include "sprite.h"
#include "constants/flags.h"
#include "constants/species.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/battle.h"
#include "battle.h"
#include "battle_interface.h"
#include "battle_anim.h"
#include "sprite.h"
#include "battle_setup.h"
#include "link.h"
#include "pokemon.h"
#include "string_util.h"
#include "constants/characters.h"
#include "rtc.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_screen.h"
#endif
#include "script_pokemon_util.h"
#include "event_object_movement.h"
#include "m4a.h"
#include "constants/global.h"
#include "item.h"
#include "money.h"
#include "coins.h"
#include "constants/items.h"
#include <math.h>
#include "field_screen_effect.h"
#include "field_player_avatar.h"
#include "script.h"
#include "platform/debug_scene.h"
#include "platform/mp_bridge.h"
#include "item_menu.h"
#include "pokemon_storage_system.h"
#include <stdbool.h>
#include "platform/png_write.h"
#include "multiplayer_rival.h"

#define MAX_KEYS 1024 // long walks (map streaming checks) hold a direction for hundreds of frames
#define MAX_SHOTS 32
#define HOLD_FRAMES 4

static struct { unsigned frame; uint16_t keys; } sScript[MAX_KEYS];
static unsigned sScriptCount;
static unsigned sShots[MAX_SHOTS];
static unsigned sShotCount;
static unsigned sKeyFrame;
static unsigned sDrawFrame;
static char sStem[400];

static uint16_t KeyBit(const char *name, size_t length)
{
    static const struct { const char *name; uint16_t bit; } keys[] = {
        {"A", A_BUTTON}, {"B", B_BUTTON}, {"SELECT", SELECT_BUTTON}, {"START", START_BUTTON},
        {"RIGHT", DPAD_RIGHT}, {"LEFT", DPAD_LEFT}, {"UP", DPAD_UP}, {"DOWN", DPAD_DOWN},
        {"R", R_BUTTON}, {"L", L_BUTTON},
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (strlen(keys[i].name) == length && strncmp(keys[i].name, name, length) == 0)
            return keys[i].bit;
    return 0;
}

static void ParseKeys(const char *text)
{
    while (*text != '\0' && sScriptCount < MAX_KEYS)
    {
        char *end;
        unsigned frame = (unsigned)strtoul(text, &end, 10);
        uint16_t keys = 0;

        if (*end != ':')
            return;
        text = end + 1;
        while (*text != '\0' && *text != ',')
        {
            const char *start = text;

            while (*text != '\0' && *text != ',' && *text != '+')
                text++;
            keys |= KeyBit(start, (size_t)(text - start));
            if (*text == '+')
                text++;
        }
        sScript[sScriptCount].frame = frame;
        sScript[sScriptCount].keys = keys;
        sScriptCount++;
        if (*text == ',')
            text++;
    }
}

static void ParseShots(const char *text)
{
    while (*text != '\0' && sShotCount < MAX_SHOTS)
    {
        char *end;

        sShots[sShotCount++] = (unsigned)strtoul(text, &end, 10);
        text = (*end == ',') ? end + 1 : end;
        if (end == text && *end != '\0')
            break;
    }
}

static int Numbers(const char *text, int *out, int max)
{
    int n = 0;

    while (*text != '\0' && n < max)
    {
        char *end;

        out[n++] = (int)strtol(text, &end, 10);
        if (end == text)
            break;
        text = (*end == ',') ? end + 1 : end;
    }
    return n;
}

static struct RegionMapTestMarkers sTest;
static unsigned sPeersThenFrame;

static void ParsePeers(const char *value, struct RegionMapTestMarkers *test)
{
    int peer = 0;
    int v[8];

    memset(test->peers, 0, sizeof(test->peers));
    while (*value != '\0' && peer < 3)
    {
        char one[96];
        const char *end = strchr(value, ';');
        size_t length = end != NULL ? (size_t)(end - value) : strlen(value);

        SDL_strlcpy(one, value, length + 1 < sizeof(one) ? length + 1 : sizeof(one));
        if (Numbers(one, v, 6) >= 6)
        {
            test->peers[peer].present = 1;
            test->peers[peer].mapGroup = (u8)v[0];
            test->peers[peer].mapNum = (u8)v[1];
            test->peers[peer].x = (s16)v[2];
            test->peers[peer].y = (s16)v[3];
            test->peers[peer].gender = (u8)v[4];
            test->peers[peer].slot = (u8)v[5];
            peer++;
        }
        value += length + (end != NULL ? 1 : 0);
    }
}

static void TraceBattlers(void)
{
    for (int b = 0; b < 4; b++)
    {
        struct Sprite *h = &gSprites[gHealthboxSpriteIds[b]];
        struct Sprite *m = &gSprites[gBattlerSpriteIds[b]];

        SDL_Log("battler %d pos %d side %d | healthbox sprite %d x=%d y=%d x2=%d y2=%d inv=%d | mon sprite %d x=%d y=%d inv=%d", b,
                gBattlerPositions[b], GetBattlerSide(b), gHealthboxSpriteIds[b], h->x, h->y, h->x2, h->y2, h->invisible,
                gBattlerSpriteIds[b], m->x, m->y, m->invisible);
        SDL_Log("   slide=%d ball=%d cry=%d execflags=%08x ctrl=%p", gBattleSpritesDataPtr->healthBoxesData[b].healthboxSlideInStarted,
                gBattleSpritesDataPtr->healthBoxesData[b].ballAnimActive, gBattleSpritesDataPtr->healthBoxesData[b].waitForCry,
                gBattleControllerExecFlags, (void *)gBattlerControllerFuncs[b]);
        SDL_Log("   hbcb=%p dummy=%p tried=%d finished=%d", (void *)gSprites[gHealthboxSpriteIds[b]].callback, (void *)SpriteCallbackDummy,
                gBattleSpritesDataPtr->healthBoxesData[b].triedShinyMonAnim, gBattleSpritesDataPtr->healthBoxesData[b].finishedShinyMonAnim);
    }
}

// PKMN_DEBUG_BATTLE=wild:species:level | trainer:id  (with the field scene): a battle from the overworld of a fresh game, started
// when the field has settled and PKMN_DEBUG_BATTLE_AT game frames have passed; PKMN_DEBUG_BATTLE_AGAIN=n starts a second one n frames
// after the first has ended (the first-battle checks of the battle-to-overworld transition).
static struct { bool read, started; int kind, a, b; unsigned at, frames, again, endedFrame; int count; bool wasInBattle; } sBattleScript;

static void BattleScriptTick(void)
{
    const char *value;
    char kind[16] = {0};
    int a = 0, b = 0;

    if (!sBattleScript.read)
    {
        sBattleScript.read = true;
        if ((value = SDL_getenv("PKMN_DEBUG_BATTLE")) != NULL && sscanf(value, "%15[a-z]:%d:%d", kind, &a, &b) >= 2)
        {
            sBattleScript.kind = strcmp(kind, "trainer") == 0 ? 2 : strcmp(kind, "first") == 0 ? 3 : strcmp(kind, "bag") == 0 ? 4 : 1;
            sBattleScript.a = a;
            sBattleScript.b = b > 0 ? b : 5;
        }
        sBattleScript.at = (value = SDL_getenv("PKMN_DEBUG_BATTLE_AT")) != NULL ? (unsigned)strtoul(value, NULL, 10) : 120;
        sBattleScript.again = (value = SDL_getenv("PKMN_DEBUG_BATTLE_AGAIN")) != NULL ? (unsigned)strtoul(value, NULL, 10) : 0;
    }
    if (sBattleScript.kind == 0)
        return;
    if (gMain.inBattle)
        sBattleScript.wasInBattle = true;
    else if (sBattleScript.wasInBattle)
    {
        sBattleScript.wasInBattle = false;
        sBattleScript.endedFrame = sKeyFrame;
        SDL_Log("debug battle: battle %d over at frame %u (outcome %d)", sBattleScript.count, sKeyFrame, gBattleOutcome);
    }
    if (gMain.callback2 != CB2_Overworld || gMain.inBattle)
        return;
    if (ArePlayerFieldControlsLocked() || gPaletteFade.active || ScriptContext_IsEnabled())
    {
        sBattleScript.frames = 0;
        return;
    }
    if (sBattleScript.count == 0 ? ++sBattleScript.frames < sBattleScript.at
                                  : (sBattleScript.again == 0 || sKeyFrame < sBattleScript.endedFrame + sBattleScript.again || sBattleScript.count >= 2))
        return;
    sBattleScript.count++;
    SDL_Log("debug battle: starting battle %d (%s %d) at frame %u", sBattleScript.count, sBattleScript.kind == 2 ? "trainer" : "wild",
            sBattleScript.a, sKeyFrame);
    if (sBattleScript.kind == 4)
    {
        // Birch's bag exactly as the game's script runs it (fade, starter screen, the first battle, the script's end)
        extern const u8 Route101_EventScript_BirchsBag[];

        ScriptContext_SetupScript(Route101_EventScript_BirchsBag);
    }
    else if (sBattleScript.kind == 3)
    {
        // the story's first battle: Birch's bag, the starter screen, then CB2_GiveStarter -> CB2_StartFirstBattle (blur transition)
        gSpecialVar_Result = 0;
        CreateScriptedWildMon(SPECIES_ZIGZAGOON, 2, ITEM_NONE); // Birch's script: setwildbattle SPECIES_ZIGZAGOON, 2
        ChooseStarter();
    }
    else if (sBattleScript.kind == 1)
    {
        CreateScriptedWildMon((enum Species)sBattleScript.a, (u8)sBattleScript.b, ITEM_NONE);
        BattleSetup_StartScriptedWildBattle();
    }
    else
    {
        LockPlayerFieldControls();
        FreezeObjectEvents();
        gBattleTypeFlags = BATTLE_TYPE_TRAINER;
        TRAINER_BATTLE_PARAM.opponentA = (u16)sBattleScript.a;
        TRAINER_BATTLE_PARAM.opponentB = 0xFFFF;
        CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_A], GetTrainerStructFromId(sBattleScript.a));
        gBattleEnvironment = BattleSetup_GetEnvironmentId();
        CalculateEnemyPartyCount();
        BattleSetup_StartTrainerBattle_Debug();
    }
}


#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"
#include "native_ui/ui_widgets.h"
#endif

// PKMN_DEBUG_DIALOG=name[:frame]  (with the field scene): runs one of the game's own scripts (rescue = Birch's call on Route 101,
// boy, youngster, sign, bag) once the field has settled (or at game frame `frame`), and logs, every game frame in which
// anything changed, the line "dialog F: ..." with the script's position (offset into the map's script data and the mode), the
// window 0 text printer (state, offset into the string, delay), the keys of the frame (held/new), the Native UI's view of the
// window (printed offset, waiting, the page the 3D box would draw). Written for the Update 28 dialogue bug; see
// tools/pc_port/dialogue_check.py.
static struct { bool read, started; int which; unsigned at; } sDialog;

#ifdef ENABLE_NATIVE_UI
static void AsciiOf(const u8 *in, char *out, size_t size)
{
    size_t n = 0;

    for (; *in != 0xFF && n + 6 < size; in++)
    {
        u8 c = *in;

        if (c >= 0xBB && c <= 0xD4)
            out[n++] = 'A' + (c - 0xBB);
        else if (c >= 0xD5 && c <= 0xEE)
            out[n++] = 'a' + (c - 0xD5);
        else if (c >= 0xA1 && c <= 0xAA)
            out[n++] = '0' + (c - 0xA1);
        else if (c == 0x00)
            out[n++] = ' ';
        else if (c == 0xAB) out[n++] = '!';
        else if (c == 0xAC) out[n++] = '?';
        else if (c == 0xAD) out[n++] = '.';
        else if (c == 0xB8) out[n++] = ',';
        else if (c == 0xFE) { out[n++] = '\\'; out[n++] = 'n'; }
        else if (c == 0xFB) { out[n++] = '\\'; out[n++] = 'p'; }
        else if (c == 0xFA) { out[n++] = '\\'; out[n++] = 'l'; }
        else n += (size_t)SDL_snprintf(out + n, 6, "<%02X>", c);
    }
    out[n] = '\0';
}
#endif // ENABLE_NATIVE_UI

extern const u8 *ScriptContext_DebugPosition(int *mode, int *status);
extern bool32 TextPrinter_DebugState(u8 windowId, int *state, int *delay);

// PKMN_DEBUG_SETUP="coins=130000,money=1000000,case=1,ring=0,charm=0,stone=ITEM" (Update 29): the player's wallet and bag when the
// script starts. The line "econ F: coins C money M ring R charm H stones N" is logged whenever any of them changes.
static void EconSetup(void)
{
    const char *value = SDL_getenv("PKMN_DEBUG_SETUP");
    char copy[200], *token, *save = NULL;

    if (value == NULL)
        return;
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;      // the check presses A on a timer: fast text
    SDL_strlcpy(copy, value, sizeof(copy));
    for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save))
    {
        unsigned amount;

        if (sscanf(token, "coins=%u", &amount) == 1)
            SetCoins(amount);
        else if (sscanf(token, "money=%u", &amount) == 1)
            SetMoney(&gSaveBlock1Ptr->money, amount);
        else if (sscanf(token, "case=%u", &amount) == 1 && amount)
            AddBagItem(ITEM_COIN_CASE, 1);
        else if (sscanf(token, "ring=%u", &amount) == 1 && amount)
            AddBagItem(ITEM_MEGA_RING, 1);
        else if (sscanf(token, "charm=%u", &amount) == 1 && amount)
            AddBagItem(ITEM_SHINY_CHARM, 1);
    }
}

static void EconLog(void)
{
    static struct { u32 coins, money; int ring, charm, stones; bool init; } prev;
    u32 coins = GetCoins(), money = GetMoney(&gSaveBlock1Ptr->money);
    int ring = CheckBagHasItem(ITEM_MEGA_RING, 1), charm = CheckBagHasItem(ITEM_SHINY_CHARM, 1), stones = 0;

    {
        extern const u16 MauvilleCity_Mart_Pokemart_MegaStones[];

        for (int i = 0; MauvilleCity_Mart_Pokemart_MegaStones[i] != ITEM_NONE; i++)
            stones += CountTotalItemQuantityInBag(MauvilleCity_Mart_Pokemart_MegaStones[i]);
    }
    if (!prev.init || coins != prev.coins || money != prev.money || ring != prev.ring || charm != prev.charm || stones != prev.stones)
    {
        SDL_Log("econ %u: coins %u money %u ring %d charm %d stones %d", sKeyFrame, coins, money, ring, charm, stones);
        prev.coins = coins; prev.money = money; prev.ring = ring; prev.charm = charm; prev.stones = stones; prev.init = true;
    }
}

static void DialogTick(void)
{
    extern const u8 Route101_EventScript_StartBirchRescue[], Route101_EventScript_Boy[], Route101_EventScript_Youngster[],
        Route101_EventScript_RouteSign[], Route101_EventScript_BirchsBag[];
    extern const u8 MauvilleCity_GameCorner_EventScript_ShinyCharmClerk[], MauvilleCity_Mart_EventScript_MegaStoneDealer[],
        MauvilleCity_GameCorner_EventScript_CoinsClerk[], MauvilleCity_GameCorner_EventScript_Buy5000Coins[],
        MauvilleCity_GameCorner_EventScript_Buy500Coins[];
    static const struct { const char *name; const u8 *script; } scripts[] = {
        {"rescue", Route101_EventScript_StartBirchRescue}, {"boy", Route101_EventScript_Boy}, {"youngster", Route101_EventScript_Youngster},
        {"sign", Route101_EventScript_RouteSign}, {"bag", Route101_EventScript_BirchsBag},
        // Update 29 (tools/pc_port/economy_check.py): the Game Corner's prize counter, the Mega Stone dealer and the coin counter
        {"prizes", MauvilleCity_GameCorner_EventScript_ShinyCharmClerk}, {"dealer", MauvilleCity_Mart_EventScript_MegaStoneDealer},
        {"coinclerk", MauvilleCity_GameCorner_EventScript_CoinsClerk}, {"pack5000", MauvilleCity_GameCorner_EventScript_Buy5000Coins},
        {"pack500", MauvilleCity_GameCorner_EventScript_Buy500Coins},
    };
    static struct { const u8 *pos; int mode, status, state, delay; unsigned printed; bool waiting, printer; char page[200]; u16 held, newk; } prev;
    const char *value;
    int mode, status, state = -1, delay = -1;
    const u8 *pos;
    bool printer;
    char ascii[200] = "";
    unsigned printed = 0;
    int active = 0, waiting = 0;

    if (!sDialog.read)
    {
        char name[16] = {0};
        int at = 0;

        sDialog.read = true;
        sDialog.which = -1;
        if ((value = SDL_getenv("PKMN_DEBUG_DIALOG")) != NULL && sscanf(value, "%15[a-z0-9]:%d", name, &at) >= 1)
            for (unsigned i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++)
                if (strcmp(name, scripts[i].name) == 0)
                    sDialog.which = (int)i;
        sDialog.at = at > 0 ? (unsigned)at : 0;
    }
    if (sDialog.which < 0)
        return;
    if (!sDialog.started)
    {
        static unsigned settled;

        if (gMain.callback2 != CB2_Overworld || gMain.inBattle || ArePlayerFieldControlsLocked() || gPaletteFade.active || ScriptContext_IsEnabled())
        {
            settled = 0;
            return;
        }
        if (++settled < 90 || (sDialog.at != 0 && sKeyFrame < sDialog.at))
            return;
        sDialog.started = true;
        EconSetup();
        SDL_Log("dialog %u: START script %s", sKeyFrame, scripts[sDialog.which].name);
        ScriptContext_SetupScript(scripts[sDialog.which].script);
    }
    EconLog();
    pos = ScriptContext_DebugPosition(&mode, &status);
    printer = TextPrinter_DebugState(0, &state, &delay) != FALSE;
    waiting = printer && (state == 1 || state == 2 || state == 3);   // RENDER_STATE_WAIT, CLEAR, SCROLL_START
#ifdef ENABLE_NATIVE_UI
    {
        u8 text[256], page[256];
        u32 chars = 0;
        bool8 on = FALSE;

        if (TextPrinter_NativeUiWindowText(0, text, sizeof(text), &chars, &on))
        {
            // what the 3D message box draws: the same page selection (UiW_MessagePageFlip), flipped as the field box flips it
            UiW_MessagePageFlip(text, on ? (int)chars : -1, on && !waiting, page, sizeof(page));
            AsciiOf(page, ascii, sizeof(ascii));
        }
        printed = chars;
        active = on;
    }
#endif
    if (pos != prev.pos || mode != prev.mode || status != prev.status || printer != prev.printer || state != prev.state
        || printed != prev.printed || waiting != prev.waiting || strcmp(ascii, prev.page) != 0 || gMain.heldKeys != prev.held || gMain.newKeys != prev.newk)
    {
        SDL_Log("dialog %u: script %p mode %d status %d | printer %d state %d delay %d waiting %d | native printed %u active %d | keys held %04x new %04x | page \"%s\"",
                sKeyFrame, (const void *)pos, mode, status, printer, state, delay, waiting, printed, active, gMain.heldKeys, gMain.newKeys, ascii);
        prev.pos = pos; prev.mode = mode; prev.status = status; prev.printer = printer; prev.state = state;
        prev.printed = printed; prev.waiting = waiting; prev.held = gMain.heldKeys; prev.newk = gMain.newKeys;
        SDL_strlcpy(prev.page, ascii, sizeof(prev.page));
    }
}

// PKMN_DEBUG_TAPS=startMs:periodMs:durationMs:count  (Update 28, the dialogue bug): a thread presses the key bound to A (Z) as real SDL
// key events (SDL_PushEvent: the path of a keyboard), count times, each held durationMs and one every periodMs, and the game
// counts the presses it sees at its frame boundary (a rising edge of the A bit). The line "taps: pushed N seen M (lost L)"
// closes the run. tools/pc_port/input_tap_check.py.
static struct { int start, period, duration, count; unsigned seen, pushed; bool prevA; unsigned lastPushMs; } sTaps;

static int TapThread(void *unused)
{
    SDL_Event ev;

    (void)unused;
    SDL_Delay((Uint32)sTaps.start);
    for (int i = 0; i < sTaps.count; i++)
    {
        memset(&ev, 0, sizeof(ev));
        ev.type = SDL_KEYDOWN;
        ev.key.state = SDL_PRESSED;
        ev.key.keysym.sym = SDLK_z;
        ev.key.keysym.scancode = SDL_SCANCODE_Z;
        SDL_PushEvent(&ev);
        sTaps.pushed++;
        SDL_Delay((Uint32)sTaps.duration);
        ev.type = SDL_KEYUP;
        ev.key.state = SDL_RELEASED;
        SDL_PushEvent(&ev);
        SDL_Delay((Uint32)(sTaps.period > sTaps.duration ? sTaps.period - sTaps.duration : 1));
    }
    SDL_Delay(1500);
    SDL_Log("taps: pushed %u seen %u (lost %d)", sTaps.pushed, sTaps.seen, (int)sTaps.pushed - (int)sTaps.seen);
    sTaps.count = -1;
    return 0;
}

static void TapsStart(void)
{
    const char *value = SDL_getenv("PKMN_DEBUG_TAPS");

    if (value != NULL && sscanf(value, "%d:%d:%d:%d", &sTaps.start, &sTaps.period, &sTaps.duration, &sTaps.count) == 4)
        SDL_DetachThread(SDL_CreateThread(TapThread, "taps", NULL));
    else
        sTaps.count = -1;
}

static void TapsFrame(uint16_t keys)
{
    bool a = (keys & A_BUTTON) != 0;

    if (a && !sTaps.prevA)
        sTaps.seen++;
    sTaps.prevA = a;
}

// Update 31 (tools/pc_port/rival_check.py), with the field scene:
//   PKMN_DEBUG_RIVAL="flags,completed,cleared,here,state"  the host's RivalRecord as the companion would write it (as if connected)
//   PKMN_DEBUG_RIVAL_SET="v4084=3,f1e9=1,..."               save variables / flags (hex ids) before the map loads
//   PKMN_DEBUG_RIVAL_BATTLE=word                             a battle started here carries this preparation word (rival battle environment)
//   PKMN_DEBUG_RIVAL_WATCH="v4084,f1e9,..."                  the line "rival F: ..." whenever the place, the battle or a watched value changes
//   PKMN_DEBUG_RIVAL_OBJ=localId                             ... and whether that map object is on the field
static void RivalSetup(void)
{
    const char *value;
    char copy[400], *token, *save = NULL;

    if ((value = SDL_getenv("PKMN_DEBUG_RIVAL_SET")) != NULL)
    {
        SDL_strlcpy(copy, value, sizeof(copy));
        for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save))
        {
            unsigned id, amount;
            if (sscanf(token, "v%x=%u", &id, &amount) == 2)
                VarSet((u16)id, (u16)amount);
            else if (sscanf(token, "f%x=%u", &id, &amount) == 2)
                amount ? FlagSet((u16)id) : FlagClear((u16)id);
        }
    }
    if ((value = SDL_getenv("PKMN_DEBUG_RIVAL")) != NULL)
    {
        int v[5];
        u8 record[MP_RIVAL_RECORD_BYTES] = {0};
        if (Numbers(value, v, 5) == 5)
        {
            for (int i = 0; i < 5; i++)
                record[i] = (u8)v[i];
            MultiplayerRival_TestSetRecord(record);
        }
    }
    if ((value = SDL_getenv("PKMN_DEBUG_RIVAL_BATTLE")) != NULL)
        MultiplayerRival_TestSetPreparation((u32)strtoul(value, NULL, 0));
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST; // the checks press A on a timer
}

// PKMN_DEBUG_RIVAL_THEN="frame:flags,completed,cleared,here,state" or "frame:off": at that game frame the host's record
// changes (a pass recorded, a world completion) or the game loses its host ("off": as a disconnect).
static void RivalThen(void)
{
    static int sFrame = -1;
    static char sRest[64];
    const char *value;

    if (sFrame == -1)
    {
        sFrame = -2;
        if ((value = SDL_getenv("PKMN_DEBUG_RIVAL_THEN")) != NULL && sscanf(value, "%d:%63s", &sFrame, sRest) != 2)
            sFrame = -2;
    }
    if (sFrame < 0 || sKeyFrame != (unsigned)sFrame)
        return;
    if (strcmp(sRest, "off") == 0)
    {
        MultiplayerRival_TestSetRecord(NULL);
        SDL_Log("rival %u: host record gone (disconnect)", sKeyFrame);
    }
    else
    {
        int v[5];
        u8 record[MP_RIVAL_RECORD_BYTES] = {0};
        if (Numbers(sRest, v, 5) == 5)
        {
            for (int i = 0; i < 5; i++)
                record[i] = (u8)v[i];
            MultiplayerRival_TestSetRecord(record);
            SDL_Log("rival %u: host record now %s", sKeyFrame, sRest);
        }
    }
}

static void RivalLog(void)
{
    static char sPrev[600];
    char watch[300], obj[16], line[600], copy[300], *token, *save = NULL;
    int n;

    // Copies: on Windows each SDL_getenv call reuses one static buffer.
    if (SDL_getenv("PKMN_DEBUG_RIVAL_WATCH") == NULL || gSaveBlock1Ptr == NULL)
        return;
    SDL_strlcpy(watch, SDL_getenv("PKMN_DEBUG_RIVAL_WATCH"), sizeof(watch));
    SDL_strlcpy(obj, SDL_getenv("PKMN_DEBUG_RIVAL_OBJ") != NULL ? SDL_getenv("PKMN_DEBUG_RIVAL_OBJ") : "", sizeof(obj));
    n = SDL_snprintf(line, sizeof(line), "map %d.%d %d,%d dir %d | battle %d env %d trainer %d | dowsing %d hmfly %d balls %d party %d",
        gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y,
        gObjectEvents[gPlayerAvatar.objectEventId].facingDirection, gMain.inBattle, gBattleEnvironment,
        gMain.inBattle ? TRAINER_BATTLE_PARAM.opponentA : 0, CountTotalItemQuantityInBag(ITEM_DOWSING_MACHINE),
        CountTotalItemQuantityInBag(ITEM_HM_FLY), CountTotalItemQuantityInBag(ITEM_POKE_BALL), gPartiesCount[B_TRAINER_PLAYER]);
    SDL_strlcpy(copy, watch, sizeof(copy));
    for (token = strtok_r(copy, ",", &save); token != NULL && n < (int)sizeof(line) - 24; token = strtok_r(NULL, ",", &save))
    {
        unsigned id;
        if (sscanf(token, "v%x", &id) == 1)
            n += SDL_snprintf(line + n, sizeof(line) - n, " | v%x=%u", id, VarGet((u16)id));
        else if (sscanf(token, "f%x", &id) == 1)
            n += SDL_snprintf(line + n, sizeof(line) - n, " | f%x=%d", id, FlagGet((u16)id) ? 1 : 0);
    }
    if (obj[0] != '\0' && n < (int)sizeof(line) - 24)
        n += SDL_snprintf(line + n, sizeof(line) - n, " | obj %d %d", atoi(obj), GetObjectEventIdByLocalId((u8)atoi(obj)) < OBJECT_EVENTS_COUNT);
    if (gPartiesCount[B_TRAINER_PLAYER] && n < (int)sizeof(line) - 40)
        n += SDL_snprintf(line + n, sizeof(line) - n, " | mon %d lv %d met %d", GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES),
                          GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_LEVEL), GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION));
    if (strcmp(line, sPrev) != 0 || sKeyFrame % 300 == 0) // and a heartbeat, so a check knows how far the game is
    {
        SDL_Log("rival %u: %s", sKeyFrame, line);
        SDL_strlcpy(sPrev, line, sizeof(sPrev));
    }
}

// PKMN_DEBUG_BAG_LOG=1: the line "bag pocket F: n" each game frame the bag's pocket changes (the pocket-switch timing check).
static void BagLog(void)
{
    static int sLast = -1, sOn = -1;

    if (sOn < 0)
        sOn = SDL_getenv("PKMN_DEBUG_BAG_LOG") != NULL;
    if (sOn && gBagPosition.pocket != sLast)
    {
        sLast = gBagPosition.pocket;
        SDL_Log("bag pocket %u: %d", sKeyFrame, sLast);
    }
}

// PKMN_DEBUG_LINK_LOG=1: the line "link state F: s role r" each game frame the pause menu's link status changes
// (enum MpLinkState; the role from --link-role), and "clock F: hh:mm" at game frame 300 (the game's own time, as the
// pause menu shows it): the link-status and setup-clock checks.
static void LinkLog(void)
{
    static int sLast = -1, sOn = -1;

    if (sOn < 0)
        sOn = SDL_getenv("PKMN_DEBUG_LINK_LOG") != NULL;
    if (sOn && (int)MpBridge_LinkState() != sLast)
    {
        sLast = (int)MpBridge_LinkState();
        SDL_Log("link state %u: %d role %c", sKeyFrame, sLast, MpBridge_Role() != 0 ? MpBridge_Role() : '-');
    }
    if (sOn && sKeyFrame == 300 && gSaveBlock2Ptr != NULL)
    {
        RtcCalcLocalTime();
        SDL_Log("clock %u: %02d:%02d", sKeyFrame, gLocalTime.hours, gLocalTime.minutes);
    }
}

// PKMN_DEBUG_HURT=1: the party starts hurt (a quarter of its HP) and poisoned, and the line "party F: hp a/b status s"
// is logged each game frame the party's HP or status changes (the Pokemon Center healing check).
static void PartyLog(void)
{
    static int sOn = -1;
    static char sPrev[96];
    char line[96];
    int hp = 0, maxHp = 0, status = 0;

    if (sOn < 0)
        sOn = SDL_getenv("PKMN_DEBUG_HURT") != NULL;
    if (!sOn || gPartiesCount[B_TRAINER_PLAYER] == 0)
        return;
    for (int m = 0; m < gPartiesCount[B_TRAINER_PLAYER]; m++)
    {
        hp += GetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_HP);
        maxHp += GetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_MAX_HP);
        status |= GetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_STATUS);
    }
    SDL_snprintf(line, sizeof(line), "hp %d/%d status %d (%d Pokemon)", hp, maxHp, status, gPartiesCount[B_TRAINER_PLAYER]);
    if (strcmp(line, sPrev) != 0)
    {
        SDL_Log("party %u: %s", sKeyFrame, line);
        SDL_strlcpy(sPrev, line, sizeof(sPrev));
    }
}

unsigned DebugScene_GameFrame(void)
{
    return sKeyFrame;
}

uint16_t DebugScene_FilterKeys(uint16_t keys)
{
    sKeyFrame++;
    BagLog();
    LinkLog();
    PartyLog();
    BattleScriptTick();
    DialogTick();
    RivalThen();
    RivalLog();
    if (SDL_getenv("PKMN_DEBUG_FRAMES") != NULL)
    {
        static const void *sPrevCb2;
        static int sPrevFade = -1, sPrevIn = -1;
        static const char *sPrevScreen = (const char *)1;
#ifdef ENABLE_NATIVE_UI
        const char *screen = NativeUi_ScreenNow();
#else
        const char *screen = NULL;
#endif

        if (gMain.callback2 != sPrevCb2 || gPaletteFade.active != sPrevFade || gMain.inBattle != sPrevIn || screen != sPrevScreen)
        {
            // tools/pc_port/battle_end_check.py reads these lines ("battleCb": the battle's own callback runs)
            sPrevCb2 = (const void *)gMain.callback2;
            sPrevFade = gPaletteFade.active;
            sPrevIn = gMain.inBattle;
            sPrevScreen = screen;
            SDL_Log("game frame %u: callback2 %p fade %d inBattle %d battleCb %d native screen %s", sKeyFrame, (void *)gMain.callback2,
                    gPaletteFade.active, gMain.inBattle, gMain.callback2 == BattleMainCB2, screen != NULL ? screen : "-");
        }
    }
    if (SDL_getenv("PKMN_DEBUG_TRACE") != NULL && sKeyFrame % 100 == 0)
        SDL_Log("callbacks: main1 %p main2 %p state %d dispcnt %04x bldy %d fade %d bgvis %d", (void *)gMain.callback1, (void *)gMain.callback2,
                gMain.state, REG_DISPCNT, REG_BLDY, gPaletteFade.active, (int)gPaletteFade.bufferTransferDisabled);
    if (SDL_getenv("PKMN_DEBUG_TRACE") != NULL && sKeyFrame % 400 == 0 && gBattleSpritesDataPtr != NULL)
        TraceBattlers();
    if (sPeersThenFrame != 0 && sKeyFrame == sPeersThenFrame && SDL_getenv("PKMN_DEBUG_PEERS_THEN") != NULL)
    {
        // the other trainers move (or come, or leave) while the screen is open
        ParsePeers(SDL_getenv("PKMN_DEBUG_PEERS_THEN"), &sTest);
        RegionMap_PortableSetTest(&sTest);
    }
    for (unsigned i = 0; i < sScriptCount; i++)
        if (sKeyFrame >= sScript[i].frame && sKeyFrame < sScript[i].frame + HOLD_FRAMES)
            keys |= sScript[i].keys;
    if (sTaps.count != -1 || sTaps.pushed != 0)
        TapsFrame(keys);
    return keys;
}

// PKMN_DEBUG_GLSHOTS=frame,frame: the window's own picture (the 3D view with the native interface over it) through the
// F12 screenshot path, into the screenshots folder; the program ends two presented frames after the last one.
static unsigned sGlShots[MAX_SHOTS];
static unsigned sGlShotCount, sGlFrame;

// PKMN_DEBUG_GLSHOTS_BATTLE_END=every: a window screenshot every n presented frames from the moment the battle's data is freed
// (HandleEndTurn_FinishBattle) until the engine leaves BattleMainCB2 and then for 12 more presented frames (the battle-to-field handoff).
static bool sEndWatching;
static unsigned sEndCounter, sEndAfter;

static bool WantEndShot(void)
{
    const char *value = SDL_getenv("PKMN_DEBUG_GLSHOTS_BATTLE_END");
    unsigned every = value != NULL ? (unsigned)strtoul(value, NULL, 10) : 0;
    bool freed = gMain.callback2 == BattleMainCB2 && gBattleStruct == NULL && gMain.inBattle;

    if (every == 0)
        return false;
    if (freed)
        sEndWatching = true;
    if (!sEndWatching)
        return false;
    if (gMain.callback2 != BattleMainCB2 && ++sEndAfter > 60)
        exit(0);
    return (++sEndCounter % every) == 0;
}

// PKMN_DEBUG_GLANIM=every[:count]: while the player's Pokemon's move animation runs, a window screenshot every n presented
// frames (count of them, default 10), then the program ends (tools/pc_port/move_3d_check.py); the 3D view of what
// PKMN_DEBUG_ANIMSHOTS takes from the GBA frame.
static bool WantAnimShot(void)
{
    static int sEvery = -1, sMax = 10, sTaken, sCounter, sGuard;
    const char *value;

    if (sEvery < 0)
    {
        sEvery = (value = SDL_getenv("PKMN_DEBUG_GLANIM")) != NULL ? atoi(value) : 0;
        if (value != NULL && strchr(value, ':') != NULL)
            sMax = atoi(strchr(value, ':') + 1);
    }
    if (sEvery <= 0)
        return false;
    if (gMain.inBattle && ++sGuard > 6000)
        exit(2);
    // PKMN_DEBUG_GLANIM_ATTACKER=n: whose animations (default 0, the player's; -1: any battler's - the free-for-all's)
    {
        static int sWho = -2;

        if (sWho == -2)
            sWho = (value = SDL_getenv("PKMN_DEBUG_GLANIM_ATTACKER")) != NULL ? atoi(value) : 0;
        if (!(gAnimScriptActive && (sWho < 0 || gBattleAnimAttacker == sWho) && gMain.inBattle))
            return false;
    }
    if (sCounter++ % sEvery != 0)
        return false;
    if (++sTaken > sMax)
    {
        SDL_Log("Debug scene: %d window captures of move %d", sMax, (int)gAnimMoveIndex);
        exit(0);
    }
    return true;
}

bool DebugScene_WantGlShot(void)
{
    const char *value = SDL_getenv("PKMN_DEBUG_GLSHOTS");

    if (WantAnimShot())
        return true;
    if (WantEndShot())
        return true;
    if (value == NULL)
        return false;
    if (sGlShotCount == 0)
    {
        while (*value != '\0' && sGlShotCount < MAX_SHOTS)
        {
            char *end;

            sGlShots[sGlShotCount++] = (unsigned)strtoul(value, &end, 10);
            value = (*end == ',') ? end + 1 : end;
            if (end == value && *value != '\0')
                break;
        }
    }
    if (SDL_getenv("PKMN_DEBUG_GLSHOTS_GAME") != NULL)
    {
        // PKMN_DEBUG_GLSHOTS_GAME=1: the numbers are GAME frames (the frames PKMN_DEBUG_KEYS counts), not presented frames: a
        // check's pictures then fall at the same point of the script on a fast machine and a slow one (a software display
        // presents far fewer frames per game frame than a GPU). The first presented frame at or after each number is taken.
        static unsigned sTaken, sAfterLast;

        for (unsigned i = 0; i < sGlShotCount; i++)
            if (!(sTaken & (1u << i)) && sKeyFrame >= sGlShots[i])
            {
                sTaken |= 1u << i;
                return true;
            }
        // (all 32 taken: 1u << 32 is undefined in C and 0 on x86, which ended the program before the first picture)
        if (sGlShotCount > 0 && sTaken == (sGlShotCount >= 32 ? 0xFFFFFFFFu : (1u << sGlShotCount) - 1) && ++sAfterLast > 2)
            exit(0);
        return false;
    }
    sGlFrame++;
    if (sGlShotCount > 0 && sGlFrame == sGlShots[sGlShotCount - 1] + 2)
        exit(0);
    for (unsigned i = 0; i < sGlShotCount; i++)
        if (sGlShots[i] == sGlFrame)
            return true;
    return false;
}

// PKMN_DEBUG_STORAGE_AT=frame[,frame...]: the line "storage F: party ... | box1 ..." (species numbers) at those game frames.
static void StorageLog(void)
{
    static unsigned sAt[8], sCount;
    static bool sRead;
    const char *value;
    char line[256];
    int n;

    if (!sRead)
    {
        int v[8];
        sRead = true;
        if ((value = SDL_getenv("PKMN_DEBUG_STORAGE_AT")) != NULL)
        {
            sCount = (unsigned)Numbers(value, v, 8);
            for (unsigned i = 0; i < sCount; i++)
                sAt[i] = (unsigned)v[i];
        }
    }
    for (unsigned i = 0; i < sCount; i++)
    {
        if (sKeyFrame != sAt[i] || gPokemonStoragePtr == NULL)
            continue;
        n = snprintf(line, sizeof(line), "storage %u: party", sKeyFrame);
        for (int p = 0; p < gPartiesCount[B_TRAINER_PLAYER] && n < 200; p++)
            n += snprintf(line + n, sizeof(line) - n, " %u", (unsigned)GetMonData(&gParties[B_TRAINER_PLAYER][p], MON_DATA_SPECIES));
        n += snprintf(line + n, sizeof(line) - n, " | box1");
        for (int b = 0; b < IN_BOX_COUNT && n < 240; b++)
            if (GetBoxMonDataAt(0, b, MON_DATA_SPECIES) != SPECIES_NONE)
                n += snprintf(line + n, sizeof(line) - n, " %d:%u", b, (unsigned)GetBoxMonDataAt(0, b, MON_DATA_SPECIES));
        SDL_Log("%s", line);
    }
}

void DebugScene_Frame(const uint16_t *pixels)
{
    StorageLog();
    static uint8_t rgba[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
    char path[480];

    // PKMN_DEBUG_ANIMSHOTS=every[:count]: while the player's Pokemon's move animation runs, a capture every n drawn frames
    // (count of them, default 10), then the program ends (tools/pc_port/classic_move_check.py)
    {
        static int sEvery = -1, sMax = 10, sTaken, sCounter, sGuard;
        const char *value;

        if (sEvery < 0)
        {
            sEvery = (value = SDL_getenv("PKMN_DEBUG_ANIMSHOTS")) != NULL ? atoi(value) : 0;
            if (value != NULL && strchr(value, ':') != NULL)
                sMax = atoi(strchr(value, ':') + 1);
        }
        if (sEvery > 0 && sShotCount == 0)
        {
            // a guard against a move that never animates: end after 4000 drawn frames in a battle
            if (gMain.inBattle && ++sGuard > 4000)
                exit(2);
            if (gAnimScriptActive && gBattleAnimAttacker == 0 && gMain.inBattle)
            {
                if (sCounter++ % sEvery == 0)
                {
                    for (unsigned p = 0; p < DISPLAY_WIDTH * DISPLAY_HEIGHT; p++)
                    {
                        uint16_t c = pixels[p];

                        rgba[p * 4 + 0] = (uint8_t)((c & 31) * 255 / 31);
                        rgba[p * 4 + 1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
                        rgba[p * 4 + 2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
                        rgba[p * 4 + 3] = 255;
                    }
                    SDL_snprintf(path, sizeof(path), "%s-anim%02d.png", sStem[0] ? sStem : "debug-scene", sTaken);
                    PngWrite_RGBA(path, rgba, DISPLAY_WIDTH, DISPLAY_HEIGHT, false);
                    if (++sTaken >= sMax)
                    {
                        SDL_Log("Debug scene: %d animation captures of move %d", sTaken, (int)gAnimMoveIndex);
                        exit(0);
                    }
                }
            }
            return;
        }
    }
    if (sShotCount == 0)
        return;
    sDrawFrame++;
    for (unsigned i = 0; i < sShotCount; i++)
    {
        if (sShots[i] != sDrawFrame)
            continue;
        for (unsigned p = 0; p < DISPLAY_WIDTH * DISPLAY_HEIGHT; p++)
        {
            uint16_t c = pixels[p];

            rgba[p * 4 + 0] = (uint8_t)((c & 31) * 255 / 31);
            rgba[p * 4 + 1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
            rgba[p * 4 + 2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
            rgba[p * 4 + 3] = 255;
        }
        SDL_snprintf(path, sizeof(path), "%s-%04u.png", sStem[0] ? sStem : "debug-scene", sDrawFrame);
        PngWrite_RGBA(path, rgba, DISPLAY_WIDTH, DISPLAY_HEIGHT, false);
        SDL_Log("Debug scene: wrote %s", path);
        if (i + 1 == sShotCount)
            exit(0);
    }
}


// PKMN_DEBUG_SCENE=ffa: a free-for-all battle on this screen, you and three computer trainers, for looking at the layout
// (docs/four-player-battle-ui.md). PKMN_DEBUG_FFA="species,species,species,species" and PKMN_DEBUG_FFA_PARTY=n (Pokemon each).
// PKMN_DEBUG_FFA_TRAINERS=3 (Update 32): three trainers, the second player place empty, as a linked 1v1v1 draws it.
static void StartFfaScene(void)
{
    static const char *const names[4] = {"ALICE", "BOB", "CY", "DI"};
    u16 species[4] = {SPECIES_BLAZIKEN, SPECIES_GARDEVOIR, SPECIES_PIKACHU, SPECIES_RAYQUAZA};
    const char *value;
    int v[8], partySize = 2, i, j, trainers = 4;
    bool weak = SDL_getenv("PKMN_DEBUG_FFA_WEAK") != NULL;

    if ((value = SDL_getenv("PKMN_DEBUG_FFA_TRAINERS")) != NULL && Numbers(value, v, 1) >= 1 && v[0] == 3)
        trainers = 3;

    if ((value = SDL_getenv("PKMN_DEBUG_FFA")) != NULL && Numbers(value, v, 4) >= 4)
        for (i = 0; i < 4; i++)
            species[i] = (u16)v[i];
    if ((value = SDL_getenv("PKMN_DEBUG_FFA_PARTY")) != NULL && Numbers(value, v, 1) >= 1 && v[0] >= 1 && v[0] <= 6)
        partySize = v[0];
    memset(gParties, 0, sizeof(gParties));
    for (i = FLAG_BADGE01_GET; i <= FLAG_BADGE08_GET; i++)
        FlagSet((u16)i); // so the level 50 Pokemon obeys
    for (i = 0; i < 4; i++)
    {
        if (trainers == 3 && i == B_POSITION_PLAYER_RIGHT)
        {
            memset(&gLinkPlayers[i], 0, sizeof(gLinkPlayers[i]));
            gLinkPlayers[i].id = (u16)i;
            gLinkPlayers[i].name[0] = EOS;
            gPartiesCount[i] = 0;
            continue;
        }
        for (j = 0; j < partySize; j++)
        {
            struct OriginalTrainerId id = {OT_ID_PRESET, 0x1111u * (u32)(i + 1)};

            // PKMN_DEBUG_FFA_WEAK: the three other trainers' Pokemon are level 3, so one Swift knocks them all out
            // (to look at the elimination display)
            u8 level = weak && i > 0 ? 3 : 50 - 3 * j;

            CreateMonWithIVs(&gParties[i][j], j == 0 ? species[i] : SPECIES_ZIGZAGOON, level, 0x1000u * (u32)(i + 1) + (u32)j, id, 20);
        }
        for (j = 0; j < partySize; j++)
        {
            static const u16 moves[4] = {MOVE_TACKLE, MOVE_SWIFT, MOVE_SURF, MOVE_GROWL};

            u8 noGimmick = BLOCK_AI_DYNAMAX; // the computer trainers here would otherwise Dynamax (the debug party has no trainer data)

            for (int m = 0; m < 4; m++)
                SetMonMoveSlot(&gParties[i][j], moves[m], m);
            SetMonData(&gParties[i][j], MON_DATA_DYNAMAX_LEVEL, &noGimmick);
            {
                u8 noTera = TYPE_MYSTERY;

                SetMonData(&gParties[i][j], MON_DATA_TERA_TYPE, &noTera);
            }
        }
        gPartiesCount[i] = (u8)partySize;
        memset(&gLinkPlayers[i], 0, sizeof(gLinkPlayers[i]));
        gLinkPlayers[i].id = (u16)i;
        gLinkPlayers[i].gender = (u8)(i & 1);
        for (j = 0; names[i][j] != '\0'; j++)
            gLinkPlayers[i].name[j] = (u8)(CHAR_A + names[i][j] - 'A');
        gLinkPlayers[i].name[j] = EOS;
    }
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI | BATTLE_TYPE_INGAME_PARTNER | BATTLE_TYPE_FFA;
    if (SDL_getenv("PKMN_DEBUG_FFA_AS_2V2") != NULL)
    {
        // the same four Pokemon as a two against two (you and Steven against two trainers): for comparing the layouts
        gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_MULTI | BATTLE_TYPE_INGAME_PARTNER;
        TRAINER_BATTLE_PARAM.opponentB = TRAINER_BRAWLY_1;
        for (j = 0; j < partySize; j++)
            gParties[B_TRAINER_OPPONENT_B][j] = gParties[3][j];
        gPartiesCount[B_TRAINER_OPPONENT_B] = (u8)partySize;
    }
    gPartnerTrainerId = TRAINER_PARTNER(PARTNER_STEVEN);
    TRAINER_BATTLE_PARAM.opponentB = TRAINER_ROXANNE_1;
    TRAINER_BATTLE_PARAM.opponentA = TRAINER_ROXANNE_1;
    {
        extern void CB2_InitBattleForPreview(void);
        SetMainCallback2(CB2_InitBattleForPreview);
    }
}

// PKMN_DEBUG_SCENE=audio: every song and sound effect in turn through the game's own mixer (m4a), measuring what comes out of
// it (Platform_QueueAudio) frame by frame, one line per song: peak, RMS, how many frames made sound, clipped samples. Silent,
// clipped or stuck songs show up as numbers (docs/visual-audio-audit.md). PKMN_DEBUG_AUDIO=first,last,frames (frames per song).
static struct
{
    bool on;
    u16 song, first, last, frames, frame;
    double sumSquares;
    float peak;
    u32 samples, loudFrames, clipped;
    bool started;
} sAudio;

void DebugScene_AudioTap(const float *buffer, int count)
{
    float framePeak = 0.0f;

    if (!sAudio.on || !sAudio.started)
        return;
    for (int i = 0; i < count; i++)
    {
        float a = fabsf(buffer[i]);

        sAudio.sumSquares += (double)buffer[i] * buffer[i];
        if (a > framePeak)
            framePeak = a;
        if (a >= 0.999f)
            sAudio.clipped++;
    }
    sAudio.samples += (u32)count;
    if (framePeak > sAudio.peak)
        sAudio.peak = framePeak;
    if (framePeak > 0.002f)
        sAudio.loudFrames++;
}

static void CB2_AudioAudit(void)
{
    if (!sAudio.started || sAudio.frame >= sAudio.frames)
    {
        if (sAudio.started)
            SDL_Log("audio song %3u: peak %.3f rms %.4f loud-frames %u/%u clipped %u", sAudio.song, sAudio.peak,
                    sAudio.samples ? sqrt(sAudio.sumSquares / sAudio.samples) : 0.0, sAudio.loudFrames, sAudio.frames, sAudio.clipped);
        m4aMPlayAllStop();
        if (sAudio.started)
            sAudio.song++;
        if (sAudio.song > sAudio.last)
        {
            SDL_Log("audio audit done");
            exit(0);
        }
        sAudio.sumSquares = 0;
        sAudio.peak = 0;
        sAudio.samples = sAudio.loudFrames = sAudio.clipped = 0;
        sAudio.frame = 0;
        sAudio.started = true;
        m4aSongNumStart(sAudio.song);
        return;
    }
    sAudio.frame++;
}

static void StartAudioScene(void)
{
    const char *value;
    int v[4];

    sAudio.first = 1;
    sAudio.last = 558;
    sAudio.frames = 90;
    if ((value = SDL_getenv("PKMN_DEBUG_AUDIO")) != NULL && Numbers(value, v, 3) >= 3)
    {
        sAudio.first = (u16)v[0];
        sAudio.last = (u16)v[1];
        sAudio.frames = (u16)v[2];
    }
    sAudio.song = sAudio.first;
    sAudio.on = true;
    SetMainCallback2(CB2_AudioAudit);
}

// PKMN_DEBUG_SCENE=field: the overworld at PKMN_DEBUG_HERE=group,num,x,y[,gender] (a new game's player with one Pokemon, no
// save), for looking at maps in either renderer (the visual audit, docs/visual-audio-audit.md). PKMN_DEBUG_HOUR=h sets the
// clock (0-23), PKMN_DEBUG_SPECIES=n the Pokemon the player has (and PKMN_DEBUG_FOLLOWER=1 sets the follower flag).
static void StartFieldScene(void)
{
    const char *value;
    int v[4];
    u16 species = SPECIES_MUDKIP;

    if ((value = SDL_getenv("PKMN_DEBUG_SPECIES")) != NULL && Numbers(value, v, 1) >= 1)
        species = (u16)v[0];
    struct OriginalTrainerId id = {OT_ID_PRESET, 0x2222u};

    CreateMonWithIVs(&gParties[B_TRAINER_PLAYER][0], species, 30, 0x1234u, id, 20);
    SetMonMoveSlot(&gParties[B_TRAINER_PLAYER][0], MOVE_TACKLE, 0);
    SetMonMoveSlot(&gParties[B_TRAINER_PLAYER][0], MOVE_WATER_GUN, 1);
    // PKMN_DEBUG_MOVES=n,n,n,n: the Pokemon's moves by number (the Classic 2D move checks of Update 27)
    if ((value = SDL_getenv("PKMN_DEBUG_MOVES")) != NULL)
    {
        int mv[4] = {0};
        int count = Numbers(value, mv, 4);

        for (int m = 0; m < count && m < 4; m++)
            SetMonMoveSlot(&gParties[B_TRAINER_PLAYER][0], (u16)mv[m], (u8)m);
    }
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    // PKMN_DEBUG_PARTY=n (2-6): n Pokemon (the first, then Zigzagoon, Wurmple...) for the PC storage checks
    if ((value = SDL_getenv("PKMN_DEBUG_PARTY")) != NULL && Numbers(value, v, 1) >= 1 && v[0] >= 2 && v[0] <= PARTY_SIZE)
    {
        static const u16 sMore[] = {SPECIES_ZIGZAGOON, SPECIES_WURMPLE, SPECIES_POOCHYENA, SPECIES_LOTAD, SPECIES_SEEDOT};

        for (int m = 1; m < v[0]; m++)
            CreateMonWithIVs(&gParties[B_TRAINER_PLAYER][m], sMore[m - 1], 10, 0x1234u + m, id, 20);
        gPartiesCount[B_TRAINER_PLAYER] = (u8)v[0];
    }
    if (SDL_getenv("PKMN_DEBUG_HURT") != NULL)
    {
        for (int m = 0; m < gPartiesCount[B_TRAINER_PLAYER]; m++)
        {
            u16 hp = GetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_MAX_HP) / 4;
            u32 status = STATUS1_POISON;

            if (hp == 0)
                hp = 1;
            SetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_HP, &hp);
            SetMonData(&gParties[B_TRAINER_PLAYER][m], MON_DATA_STATUS, &status);
        }
    }
    for (int badge = FLAG_BADGE01_GET; badge <= FLAG_BADGE08_GET; badge++)
        FlagSet((u16)badge); // so the level 30 Pokemon obeys
    FlagSet(FLAG_SYS_POKEMON_GET); // the start menu lists POKéMON
    FlagSet(FLAG_SYS_POKEDEX_GET);
    // PKMN_DEBUG_RUN=1: the Running Shoes, so B held in PKMN_DEBUG_KEYS runs (the 3D view's frame-time walks)
    if ((value = SDL_getenv("PKMN_DEBUG_RUN")) != NULL && value[0] == '1')
        FlagSet(FLAG_SYS_B_DASH);
    // PKMN_DEBUG_SEED=n: the random numbers from seed n (the boot seeds them from the clock), so two runs - two builds'
    // pictures compared - have the same wild Pokemon in the same places
    if ((value = SDL_getenv("PKMN_DEBUG_SEED")) != NULL && Numbers(value, v, 1) >= 1)
        SeedRng((u32)v[0]);
    // PKMN_DEBUG_PARTY=0: no Pokemon at all (Update 32: a short-introduction character before its lab claim; the
    // starter flag goes too, PKMN_DEBUG_RIVAL_SET sets the rest of that state)
    if ((value = SDL_getenv("PKMN_DEBUG_PARTY")) != NULL && Numbers(value, v, 1) >= 1 && v[0] == 0)
    {
        ZeroPlayerPartyMons();
        gPartiesCount[B_TRAINER_PLAYER] = 0;
        FlagClear(FLAG_SYS_POKEMON_GET);
    }
    if ((value = SDL_getenv("PKMN_DEBUG_HOUR")) != NULL && Numbers(value, v, 1) >= 1)
        RtcCalcLocalTimeOffset(0, v[0], 0, 0);
    ResetInitialPlayerAvatarState();
    ScriptContext_Init();
    UnlockPlayerFieldControls();
    SetWarpDestination(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum, WARP_ID_NONE, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    WarpIntoMap();
    gFieldCallback = FieldCB_WarpExitFadeFromBlack;
    gFieldCallback2 = NULL;
    SetMainCallback2(CB2_LoadMap);
}

static void CB2_DebugSceneSetup(void)
{
    // A copy: on Windows SDL_getenv returns one static buffer that the next SDL_getenv call overwrites, so the scene name
    // read here would turn into PKMN_DEBUG_HERE's value below and no scene would ever start (seen under Wine).
    char sceneName[32] = "";
    const char *scene = SDL_getenv("PKMN_DEBUG_SCENE") != NULL ? sceneName : NULL;

    if (scene != NULL)
        SDL_strlcpy(sceneName, SDL_getenv("PKMN_DEBUG_SCENE"), sizeof(sceneName));
    const char *value;
    struct RegionMapTestMarkers test;
    int v[8];
    // what the game does before its first screen (CB2_InitCopyrightScreenAfterBootup)
    SetSaveBlocksPointers(GetSaveBlocksPointersBaseOffset());
    ResetMenuAndMonGlobals();
    Save_ResetSaveCounters();
    Sav2_ClearSetDefault();
    InitHeap(gHeap, HEAP_SIZE);
    ScanlineEffect_Clear(); // the field clears these when it starts; the screens expect it
    ResetPaletteFade();
    ResetSpriteData();
    FreeAllSpritePalettes();

    if (scene == NULL || scene[0] == '\0')
        return;

    NewGameInitData();
    FlagSet(FLAG_SYS_POKENAV_GET);
    gSaveBlock2Ptr->playerGender = MALE;
    {
        // a new game has no name: unterminated, so string copies of it run on
        for (int c = 0; c < 5; c++)
            gSaveBlock2Ptr->playerName[c] = (u8)(CHAR_A + c);
        gSaveBlock2Ptr->playerName[5] = EOS;
    }
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_ROUTE104);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_ROUTE104);
    gSaveBlock1Ptr->pos.x = 12;
    gSaveBlock1Ptr->pos.y = 30;
    if ((value = SDL_getenv("PKMN_DEBUG_HERE")) != NULL && Numbers(value, v, 5) >= 4)
    {
        gSaveBlock1Ptr->location.mapGroup = (u8)v[0];
        gSaveBlock1Ptr->location.mapNum = (u8)v[1];
        gSaveBlock1Ptr->pos.x = (s16)v[2];
        gSaveBlock1Ptr->pos.y = (s16)v[3];
        if (Numbers(value, v, 5) >= 5)
            gSaveBlock2Ptr->playerGender = (u8)v[4];
    }
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
    if (gSaveBlock1Ptr->pos.x < 0 && gMapHeader.mapLayout != NULL) // x < 0: the middle of the map (the visual audit's sweep)
    {
        gSaveBlock1Ptr->pos.x = (s16)(gMapHeader.mapLayout->width / 2);
        gSaveBlock1Ptr->pos.y = (s16)(gMapHeader.mapLayout->height / 2);
    }

    memset(&test, 0, sizeof(test));
    if ((value = SDL_getenv("PKMN_DEBUG_PEERS")) != NULL)
        ParsePeers(value, &test);
    if ((value = SDL_getenv("PKMN_DEBUG_EVENT")) != NULL && Numbers(value, v, 4) >= 4)
    {
        test.eventId = 1;
        test.eventSpecies = 1;
        test.eventMapGroup = (u8)v[0];
        test.eventMapNum = (u8)v[1];
        test.eventX = (s16)v[2];
        test.eventY = (s16)v[3];
    }
    if ((value = SDL_getenv("PKMN_DEBUG_OUTBREAK")) != NULL && Numbers(value, v, 2) >= 2)
    {
        test.outbreak[0] = 1;
        test.outbreak[1] = (u8)v[0];
        test.outbreak[2] = (u8)v[1];
    }
    sTest = test;
    RegionMap_PortableSetTest(&sTest);

    if (strcmp(scene, "regionmap") == 0)
        SetMainCallback2(CB2_OpenFlyMap);
    else if (strcmp(scene, "pokenav") == 0)
        SetMainCallback2(CB2_InitPokeNav);
    else if (strcmp(scene, "ffa") == 0)
        StartFfaScene();
    else if (strcmp(scene, "field") == 0)
    {
        StartFieldScene();
        RivalSetup();
    }
    else if (strcmp(scene, "newgame") == 0)
    {
        // Update 31: a New Game after Birch's speech (the name and look chosen above), through the streamlined introduction.
        gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;
        SetMainCallback2(CB2_NewGame);
    }
    else if (strcmp(scene, "audio") == 0)
        StartAudioScene();
}

void DebugScene_Start(void)
{
    static bool sBooted;
    const char *value;

    // A soft reset runs AgbMain again (sdl2.c SoftReset) and keeps this file's state (Makefile_pc PRESERVED_OBJS): the
    // scene and the scripts belong to the first boot, so the game then boots as it would, to the title screen
    // (tools/pc_port/soft_reset_check.py).
    if (sBooted)
        return;
    sBooted = true;
    TapsStart();

    if ((value = SDL_getenv("PKMN_DEBUG_KEYS")) != NULL)
        ParseKeys(value);
    if ((value = SDL_getenv("PKMN_DEBUG_SHOTS")) != NULL)
        ParseShots(value);
    if ((value = SDL_getenv("PKMN_DEBUG_STEM")) != NULL)
        SDL_strlcpy(sStem, value, sizeof(sStem));
    if ((value = SDL_getenv("PKMN_DEBUG_PEERS_AT")) != NULL)
        sPeersThenFrame = (unsigned)strtoul(value, NULL, 10);
    if ((value = SDL_getenv("PKMN_DEBUG_SCENE")) != NULL && value[0] != '\0')
        SetMainCallback2(CB2_DebugSceneSetup);
}
