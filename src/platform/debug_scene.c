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
#include "sprite.h"
#include "battle_setup.h"
#include "link.h"
#include "pokemon.h"
#include "string_util.h"
#include "constants/characters.h"
#include "rtc.h"
#include "m4a.h"
#include <math.h>
#include "field_screen_effect.h"
#include "field_player_avatar.h"
#include "script.h"
#include "platform/debug_scene.h"
#include <stdbool.h>
#include "platform/png_write.h"

#define MAX_KEYS 64
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

uint16_t DebugScene_FilterKeys(uint16_t keys)
{
    sKeyFrame++;
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
    return keys;
}

// PKMN_DEBUG_GLSHOTS=frame,frame: the window's own picture (the 3D view with the native interface over it) through the
// F12 screenshot path, into the screenshots folder; the program ends two presented frames after the last one.
static unsigned sGlShots[MAX_SHOTS];
static unsigned sGlShotCount, sGlFrame;

bool DebugScene_WantGlShot(void)
{
    const char *value = SDL_getenv("PKMN_DEBUG_GLSHOTS");

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
    sGlFrame++;
    if (sGlShotCount > 0 && sGlFrame == sGlShots[sGlShotCount - 1] + 2)
        exit(0);
    for (unsigned i = 0; i < sGlShotCount; i++)
        if (sGlShots[i] == sGlFrame)
            return true;
    return false;
}

void DebugScene_Frame(const uint16_t *pixels)
{
    static uint8_t rgba[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
    char path[480];

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
static void StartFfaScene(void)
{
    static const char *const names[4] = {"ALICE", "BOB", "CY", "DI"};
    u16 species[4] = {SPECIES_BLAZIKEN, SPECIES_GARDEVOIR, SPECIES_PIKACHU, SPECIES_RAYQUAZA};
    const char *value;
    int v[8], partySize = 2, i, j;
    bool weak = SDL_getenv("PKMN_DEBUG_FFA_WEAK") != NULL;

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

    CreateMonWithIVs(&gParties[B_TRAINER_PLAYER][0], species, 20, 0x1234u, id, 20);
    gPartiesCount[B_TRAINER_PLAYER] = 1;
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
    const char *scene = SDL_getenv("PKMN_DEBUG_SCENE");
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
        StartFieldScene();
    else if (strcmp(scene, "audio") == 0)
        StartAudioScene();
}

void DebugScene_Start(void)
{
    const char *value;

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
