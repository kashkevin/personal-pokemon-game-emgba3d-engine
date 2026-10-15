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
#include "platform/debug_scene.h"
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

uint16_t DebugScene_FilterKeys(uint16_t keys)
{
    sKeyFrame++;
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
