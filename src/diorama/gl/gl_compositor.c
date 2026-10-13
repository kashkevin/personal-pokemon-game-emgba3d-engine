#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#ifdef NATIVE_LINUX
#endif

#include "diorama/battle_scene.h"
#include "diorama/buildings.h"
#include "diorama/camera.h"
#include "diorama/gl_air.h"
#include "diorama/gl_battle.h"
#include "diorama/gl_field_light.h"
#include "diorama/gl_building_renderer.h"
#include "diorama/gl_compositor.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_pc_overlay.h"
#include "diorama/gl_shadow.h"
#include "diorama/gl_object_renderer.h"
#include "diorama/gl_target.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/gl_tree_sprites.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/metatile_atlas.h"
#include "diorama/gl_lamps.h"
#include "diorama/gl_props.h"
#include "diorama/props.h"
#include "diorama/presentation_transition.h"
#include "diorama/scene_snapshot.h"
#include "diorama/terrain_mesh.h"
#include "diorama/weather.h"
#include "gba/defines.h"
#include "platform/display_settings.h"
#include "constants/global.h"
#include "constants/map_types.h"
#include "constants/rtc.h"
#include "constants/weather.h"

#define MAX_BORDER_BACKGROUNDS 15
#define SOURCE_FADE_SECONDS 0.12
#define CAMERA_TELEPORT_LIMIT 2.0f
#define UI_TRANSIENT_HOLD_FRAMES 3

struct DioramaTexture
{
    GLuint id;
    int width;
    int height;
};

static SDL_Window *sWindow;
static SDL_GLContext sContext;
/* The PC's own interface (settings menu, frame rate; src/platform/sdl2.c),
 * drawn last at the window's resolution before the frame is shown. */
static void (*sOverlay)(int outputWidth, int outputHeight);
/* The native UI (src/native_ui): which parts of the game's own picture it
 * replaces this frame (DIORAMA_PICTURE_*). */
static int (*sPictureFilter)(void);
static bool sLastFrame3D;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLint sOpacityUniform;
static struct DioramaTexture sFrameTexture;
static struct DioramaTexture sUiTexture;
static struct DioramaTexture sDebugTexture;
static struct DioramaTexture sWeatherTexture;
static struct DioramaTexture sAtlasTexture;
static struct DioramaTexture sBaseAtlasTexture;
static struct DioramaTexture sForegroundAtlasTexture;
static struct DioramaTexture sBackgroundTextures[MAX_BORDER_BACKGROUNDS];
static struct DioramaTexture sBorderTexture;
static u8 sBackgroundCount;
static struct DioramaSceneSnapshot sSceneSnapshot;
static struct DioramaSceneSnapshot sPreviousSceneSnapshot;
static struct DioramaSceneSnapshot sIncomingSceneSnapshot;
static struct DioramaSceneSnapshot sRenderedSceneSnapshot;
static struct DioramaSceneSnapshot sPreviousRenderedSceneSnapshot;
static struct DioramaResolvedCell sRenderedResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
static u32 sDebugPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sWeatherPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sUiPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
/* The text boxes made this game frame, uploaded only when they differ from
 * sUiPixels (what the texture holds; sUiUploaded false: nothing yet). A map's
 * name shown for two seconds is then one upload, not 120 (performance work
 * 2026-10-12). */
static u32 sUiScratch[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static bool sUiUploaded;
/*
 * The game's latest 240x160 picture until it is drawn (performance work
 * 2026-10-11): in the 3D view it is hidden nearly always, so it is uploaded
 * only when a frame draws it, not on each of the game's 60 frames a second.
 * VDraw's buffer (sdl2.c), rewritten only by the next VDraw on this thread.
 */
static const u32 *sPendingFrame;
static u32 sAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static u32 sBaseAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static u32 sForegroundAtlasPixels[DIORAMA_ATLAS_PIXEL_COUNT];
static struct DioramaResolvedCell sAtlasResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
/* Indexed by atlas slot (metatile_atlas.h): the map's metatiles, then the
 * neighbour pages'. */
static u16 sCutoutBaseMetatiles[DIORAMA_ATLAS_SLOT_COUNT];
static u16 sPreviousCutoutBaseMetatiles[DIORAMA_ATLAS_SLOT_COUNT];
static u8 sPresentMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sDirtyAtlasTiles[DIORAMA_ATLAS_DIRTY_TILE_BYTES];
static u8 sForcedMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sVisibleMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sUpdatedMetatiles[DIORAMA_ATLAS_PRESENT_BYTES];
static u8 sAtlasTileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
static u32 sAtlasMapGeneration;
static u32 sAtlasPaletteGeneration;
static u32 sAtlasAnimationGeneration;
static u32 sAtlasPageSignature;
/*
 * The last grid resolution's inputs (performance work 2026-10-11): the rules
 * resolve a grid from its cells, the map, the rules' generation and the cells
 * the game marked dirty; the same cells of the same map resolve the same, so
 * a snapshot that changes none of them (most of the game's frames) keeps the
 * resolved cells instead of resolving all 2401 again.
 */
struct ResolveKey
{
    s16 gridOriginX;
    s16 gridOriginY;
    u16 visibleCellCount;
    u16 mapLayoutId;
    u8 mapGroup;
    u8 mapNum;
    u8 pad[2];
    s32 mapCoordinateOffset;
    u32 mapGeneration;
    u32 rulesGeneration;
};
static struct ResolveKey sResolveKey;
static bool sResolveValid;
static struct DioramaCellSnapshot sResolveCells[DIORAMA_MAX_VISIBLE_CELLS];
static bool sHasAtlasTileGraphics;
static bool sHasSceneSnapshot;
static bool sHasPreviousSceneSnapshot;
static bool sHasRenderedSceneSnapshot;
static bool sHasPreviousRenderedSceneSnapshot;
static bool sCurrent3DReady;
static bool sTerrainAvailable;
static bool sObjectsAvailable;
static bool sTerrainDebug;
static int sSurveyView = -1;
static bool sSurveyCaptureRequested;
static bool sSurveySavedFullscreen;
static bool sSurveySavedTerrainDebug;
static int sSurveySavedWindowWidth;
static int sSurveySavedWindowHeight;
static int sSurveySavedWindowX;
static int sSurveySavedWindowY;
static unsigned sSurveyRun;
static enum DioramaRenderMode sSurveySavedRenderMode;
static enum DioramaRenderMode sRenderMode = DIORAMA_RENDER_AUTO;
static uint64_t sProcessedSequence;
static uint64_t sFadeStartCounter;
static float sTwoDOpacity = 1.0f;
static float sFadeStartOpacity = 1.0f;
static float sFadeTargetOpacity = 1.0f;
static bool sIntegerScale;
static bool sDrewField3D; /* the last frame presented drew the field in 3D */
/* The field as last seen with steady palettes (no fade running), whether or
 * not it was drawn in 3D: a 3D battle stands on it (gl_battle.c). */
static struct DioramaSceneSnapshot sLastFieldSnapshot;
static bool sHasLastFieldSnapshot;

static const char *SurveyFacingName(uint8_t direction)
{
    switch (direction)
    {
    case 1: return "south";
    case 2: return "north";
    case 3: return "west";
    case 4: return "east";
    default: return "unknown";
    }
}

static void SurveyImagePath(char *path, size_t size,
                            const struct DioramaSceneSnapshot *snapshot,
                            unsigned run, const char *view)
{
    snprintf(path, size, "pokeemerald-survey-%u-%u_%d_%d_%s_run-%u_%s.bmp",
             snapshot->mapGroup, snapshot->mapNum, snapshot->playerMapX,
             snapshot->playerMapY, SurveyFacingName(snapshot->playerFacingDirection),
             run, view);
}

static unsigned NextSurveyRun(const struct DioramaSceneSnapshot *snapshot)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};
    unsigned run = 1;

    for (;; run++)
    {
        int view;
        bool available = true;

        for (view = 0; view < 5; view++)
        {
            char path[256];
            FILE *file;

            SurveyImagePath(path, sizeof(path), snapshot, run, sNames[view]);
            file = fopen(path, "rb");
            if (file != NULL)
            {
                fclose(file);
                available = false;
                break;
            }
        }
        if (available)
            return run;
    }
}

static void CaptureSurveyFrame(int width, int height,
                               const struct DioramaSceneSnapshot *snapshot,
                               bool stateMatchesPixels)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};
    const char *facing;
    char imagePath[256];
    char metadataPath[272];
    uint8_t *pixels;
    uint8_t *flipped;
    SDL_Surface *surface;
    FILE *metadata;
    size_t stride;
    int y;

    sSurveyCaptureRequested = false;
    if (sSurveyView < 0 || !sHasSceneSnapshot || !stateMatchesPixels
     || snapshot->sceneKind != DIORAMA_SCENE_OVERWORLD_FREE
     || snapshot->fallbackReasons != 0 || width != 960 || height != 640)
    {
        SDL_Log("Diorama survey capture rejected: state or framebuffer is not stable");
        return;
    }
    facing = SurveyFacingName(snapshot->playerFacingDirection);
    SurveyImagePath(imagePath, sizeof(imagePath), snapshot, sSurveyRun, sNames[sSurveyView]);
    snprintf(metadataPath, sizeof(metadataPath), "%s.json", imagePath);
    stride = (size_t)width * 4;
    pixels = malloc(stride * height);
    flipped = malloc(stride * height);
    if (pixels == NULL || flipped == NULL)
    {
        SDL_Log("Diorama survey capture: out of memory");
        free(pixels);
        free(flipped);
        return;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (y = 0; y < height; y++)
        memcpy(flipped + (size_t)y * stride,
               pixels + (size_t)(height - 1 - y) * stride, stride);
    surface = SDL_CreateRGBSurfaceFrom(flipped, width, height, 32, (int)stride,
                                       0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (surface == NULL || SDL_SaveBMP(surface, imagePath) != 0)
        SDL_Log("Diorama survey capture failed: %s", SDL_GetError());
    else
    {
        metadata = fopen(metadataPath, "w");
        if (metadata != NULL)
        {
            fprintf(metadata,
                    "{\"schemaVersion\":1,\"mapGroup\":%u,\"mapNum\":%u,"
                    "\"layoutId\":%u,\"x\":%d,\"y\":%d,\"facing\":\"%s\","
                    "\"view\":\"%s\",\"width\":%d,\"height\":%d,"
                    "\"captureRun\":%u,\"sceneKind\":%u,\"fallbackReasons\":%u,"
                    "\"snapshotSequence\":%llu,\"mapGeneration\":%u,"
                    "\"mapEditGeneration\":%u,\"paletteGeneration\":%u,"
                    "\"objPaletteGeneration\":%u,\"animationGeneration\":%u}\n",
                    snapshot->mapGroup, snapshot->mapNum, snapshot->mapLayoutId,
                    snapshot->playerMapX, snapshot->playerMapY, facing, sNames[sSurveyView],
                    width, height, sSurveyRun, snapshot->sceneKind, snapshot->fallbackReasons,
                    (unsigned long long)snapshot->sequence,
                    snapshot->mapGeneration, snapshot->mapEditGeneration,
                    snapshot->paletteGeneration, snapshot->objPaletteGeneration,
                    snapshot->tilesetAnimationGeneration);
            fclose(metadata);
            SDL_Log("Diorama survey capture: %s", imagePath);
        }
        else
            SDL_Log("Diorama survey metadata could not be written");
    }
    if (surface != NULL)
        SDL_FreeSurface(surface);
    free(pixels);
    free(flipped);
}
static u8 sUiTransientHoldFrames;

#undef RGB // windows.h (via the GL headers) defines its own
#define RGB(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define RGBA(r, g, b, a) (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static void DioramaFillRect(int x, int y, int width, int height, u32 color)
{
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width > DISPLAY_WIDTH ? DISPLAY_WIDTH : x + width;
    int bottom = y + height > DISPLAY_HEIGHT ? DISPLAY_HEIGHT : y + height;
    int px;
    int py;

    for (py = top; py < bottom; py++)
        for (px = left; px < right; px++)
            sDebugPixels[py * DISPLAY_WIDTH + px] = color;
}

static u16 GetGlyph(char character)
{
    if (character >= 'a' && character <= 'z')
        character -= 'a' - 'A';

#define GLYPH(a, b, c, d, e) ((a) << 12 | (b) << 9 | (c) << 6 | (d) << 3 | (e))
    switch (character)
    {
    case '0': return GLYPH(7, 5, 5, 5, 7);
    case '1': return GLYPH(2, 6, 2, 2, 7);
    case '2': return GLYPH(7, 1, 7, 4, 7);
    case '3': return GLYPH(7, 1, 7, 1, 7);
    case '4': return GLYPH(5, 5, 7, 1, 1);
    case '5': return GLYPH(7, 4, 7, 1, 7);
    case '6': return GLYPH(7, 4, 7, 5, 7);
    case '7': return GLYPH(7, 1, 2, 2, 2);
    case '8': return GLYPH(7, 5, 7, 5, 7);
    case '9': return GLYPH(7, 5, 7, 1, 7);
    case 'A': return GLYPH(2, 5, 7, 5, 5);
    case 'B': return GLYPH(6, 5, 6, 5, 6);
    case 'C': return GLYPH(3, 4, 4, 4, 3);
    case 'D': return GLYPH(6, 5, 5, 5, 6);
    case 'E': return GLYPH(7, 4, 6, 4, 7);
    case 'F': return GLYPH(7, 4, 6, 4, 4);
    case 'G': return GLYPH(3, 4, 5, 5, 3);
    case 'H': return GLYPH(5, 5, 7, 5, 5);
    case 'I': return GLYPH(7, 2, 2, 2, 7);
    case 'J': return GLYPH(1, 1, 1, 5, 2);
    case 'K': return GLYPH(5, 5, 6, 5, 5);
    case 'L': return GLYPH(4, 4, 4, 4, 7);
    case 'M': return GLYPH(5, 7, 7, 5, 5);
    case 'N': return GLYPH(5, 7, 7, 7, 5);
    case 'O': return GLYPH(2, 5, 5, 5, 2);
    case 'P': return GLYPH(6, 5, 6, 4, 4);
    case 'Q': return GLYPH(2, 5, 5, 7, 3);
    case 'R': return GLYPH(6, 5, 6, 5, 5);
    case 'S': return GLYPH(3, 4, 2, 1, 6);
    case 'T': return GLYPH(7, 2, 2, 2, 2);
    case 'U': return GLYPH(5, 5, 5, 5, 7);
    case 'V': return GLYPH(5, 5, 5, 5, 2);
    case '-': return GLYPH(0, 0, 7, 0, 0);
    case ':': return GLYPH(0, 2, 0, 2, 0);
    case ',': return GLYPH(0, 0, 0, 2, 4);
    default: return 0;
    }
#undef GLYPH
}

static void DioramaDrawText(int x, int y, const char *text, u32 color)
{
    for (; *text != '\0'; text++, x += 4)
    {
        u16 glyph = GetGlyph(*text);
        int row;
        int column;

        for (row = 0; row < 5; row++)
            for (column = 0; column < 3; column++)
                if (glyph & (1 << ((4 - row) * 3 + (2 - column))))
                    DioramaFillRect(x + column, y + row, 1, 1, color);
    }
}

static void DrawValue(int x, int y, const char *label, int value)
{
    char text[24];

    SDL_snprintf(text, sizeof(text), "%s%d", label, value);
    DioramaDrawText(x, y, text, RGB(220, 230, 235));
}

static void BuildDebugImage(const struct DioramaSceneSnapshot *snapshot)
{
    const struct DioramaTerrainMetrics *metrics = DioramaGLTerrain_GetMetrics();
    const struct DioramaObjectMetrics *objectMetrics = DioramaGLObjects_GetMetrics();
    const struct DioramaResolvedCell *targetCell = NULL;
    struct DioramaMapProfile profile;
    char text[32];
    int targetX = snapshot->playerMapX;
    int targetY = snapshot->playerMapY;
    uint16_t i;

    memset(sDebugPixels, 0, sizeof(sDebugPixels));
    DioramaFillRect(2, 2, 122, 156, RGBA(10, 14, 18, 220));
    DrawValue(5, 5, "GEN:", snapshot->mapGeneration);
    DrawValue(5, 14, "CH:", metrics->activeChunks);
    DrawValue(5, 23, "VIS:", metrics->visibleChunks);
    DrawValue(5, 32, "CUL:", metrics->culledChunks);
    DrawValue(5, 41, "REB:", metrics->rebuiltChunks);
    DrawValue(5, 50, "TRI:", metrics->triangles);
    DrawValue(5, 59, "DRA:", metrics->drawCalls);
    DrawValue(5, 68, "OBJ:", objectMetrics->visibleObjects);
    if (DioramaRules_GetMapProfile(snapshot->mapGroup, snapshot->mapNum,
                                   snapshot->mapLayoutId, &profile))
        DioramaDrawText(5, 77, profile.cameraProfile == DIORAMA_CAMERA_INTERIOR
                            ? "CAM:I" : "CAM:E", RGB(220, 230, 235));
    else
        DioramaDrawText(5, 77, "CAM:-", RGB(220, 230, 235));
    SDL_snprintf(text, sizeof(text), "MAP:%u,%u", snapshot->mapGroup, snapshot->mapNum);
    DioramaDrawText(5, 86, text, RGB(220, 230, 235));
    SDL_snprintf(text, sizeof(text), "POS:%d,%d", snapshot->playerMapX,
                  snapshot->playerMapY);
    DioramaDrawText(5, 95, text, RGB(220, 230, 235));
    switch (snapshot->playerFacingDirection)
    {
    case 1: targetY++; break;
    case 2: targetY--; break;
    case 3: targetX--; break;
    case 4: targetX++; break;
    }
    SDL_snprintf(text, sizeof(text), "TGT:%d,%d", targetX, targetY);
    DioramaDrawText(5, 104, text, RGB(220, 230, 235));
    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
        if (snapshot->cells[i].sourceMapGroup == snapshot->mapGroup
         && snapshot->cells[i].sourceMapNum == snapshot->mapNum
         && snapshot->cells[i].sourceMapX == targetX
         && snapshot->cells[i].sourceMapY == targetY)
        {
            targetCell = &sRenderedResolvedCells[i];
            break;
        }
    if (targetCell != NULL)
    {
        const char *className = DioramaRules_ClassName(targetCell->classifierClass);
        const char *sourceName = DioramaRules_ClassifierSourceName(targetCell->classifierSource);
        const struct DioramaGeneratedStructureV2 *structure =
            DioramaRules_GetStructure(targetCell->structureId);
        const char *ownerKind = DioramaRules_StructureKindName(targetCell->structureOwnerKind);
        const char *structureSource = structure != NULL ? structure->source : "none";
        size_t sourceLength;
        size_t structureSourceLength;

        SDL_snprintf(text, sizeof(text), "CLS:%s", className != NULL ? className : "ground");
        DioramaDrawText(5, 113, text, RGB(220, 230, 235));
        if (sourceName == NULL)
            sourceName = "fallback";
        sourceLength = strcspn(sourceName, ":");
        structureSourceLength = strcspn(structureSource, ":");
        SDL_snprintf(text, sizeof(text), "SRC:%.*s/%.*s", (int)sourceLength, sourceName,
                     (int)structureSourceLength, structureSource);
        DioramaDrawText(5, 122, text, RGB(220, 230, 235));
        SDL_snprintf(text, sizeof(text), "OWN:%s", ownerKind != NULL ? ownerKind : "none");
        DioramaDrawText(5, 131, text, RGB(220, 230, 235));
        SDL_snprintf(text, sizeof(text), "CLM:%u RGN:%u", targetCell->claimOwner,
                     targetCell->regionId);
        DioramaDrawText(5, 140, text, RGB(220, 230, 235));
        if (targetCell->pixelObjectId != 0)
        {
            SDL_snprintf(text, sizeof(text), "PX:%u G:%u", targetCell->pixelObjectId,
                         targetCell->baseMetatileId);
            DioramaDrawText(5, 149, text, RGB(220, 230, 235));
        }
        else if (targetCell->measuredBandCount != 0)
        {
            SDL_snprintf(text, sizeof(text), "VOL H:%u P:%u %c F:%u",
                         targetCell->measuredBandCount,
                         targetCell->measuredPeriodBands,
                         targetCell->measuredAxis == DIORAMA_PLANE_AXIS_Z ? 'Z' : 'X',
                         targetCell->measuredFlags);
            DioramaDrawText(5, 149, text, RGB(220, 230, 235));
        }
        else
            DrawValue(5, 149, "EVI:", targetCell->evidenceFlags);
    }

    glBindTexture(GL_TEXTURE_2D, sDebugTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sDebugPixels);
}

/* What the weather layer was last made from (BuildWeatherImageFor): its picture
 * moves on 60 times a second, so at higher frame rates most frames would make
 * and upload the same picture again (performance directive, 2026-10-12). */
struct WeatherImageKey
{
    uint32_t phase;
    uint16_t fogScrollOffset;
    uint16_t weatherBlendEVA;
    uint8_t effect;
    uint8_t weather;
    uint8_t rainVisibleCount;
    uint8_t airDrawsFog;
};
static struct WeatherImageKey sWeatherKey;
static bool sWeatherKeyValid;
static bool sWeatherImageDrawn;

/* The weather's picture for `weather` (constants/weather.h): the field's own
 * (BuildWeatherImage), or a battle's over its world (DioramaGL_DrawBattleWeather). */
static bool BuildWeatherImageFor(uint8_t weather, const struct DioramaSceneSnapshot *snapshot)
{
    enum DioramaWeatherEffect effect = DioramaWeather_Classify(weather);
    uint32_t phase = (uint32_t)(SDL_GetPerformanceCounter() * 60
                             / SDL_GetPerformanceFrequency());
    struct WeatherImageKey key;
    int i;

    memset(&key, 0, sizeof(key));
    key.phase = phase;
    key.fogScrollOffset = snapshot->fogScrollOffset;
    key.weatherBlendEVA = snapshot->weatherBlendEVA;
    key.effect = (uint8_t)effect;
    key.weather = weather;
    key.rainVisibleCount = snapshot->rainVisibleCount;
    key.airDrawsFog = effect == DIORAMA_WEATHER_FOG && DioramaGLAir_DrawsFog();
    if (sWeatherKeyValid && memcmp(&key, &sWeatherKey, sizeof(key)) == 0)
        return sWeatherImageDrawn; /* the picture in the texture is this one */
    sWeatherKey = key;
    sWeatherKeyValid = true;
    sWeatherImageDrawn = false;

    memset(sWeatherPixels, 0, sizeof(sWeatherPixels));
    if (effect == DIORAMA_WEATHER_RAIN)
    {
        int dropCount = DioramaWeather_RainDropCount(snapshot->rainVisibleCount);
        /* A downpour's drops are longer and faster (the GBA's heavy rain). */
        int dropLength = weather == WEATHER_DOWNPOUR ? 11 : 7;
        int fall = weather == WEATHER_DOWNPOUR ? 10 : 7;

        for (i = 0; i < dropCount; i++)
        {
            int cycle = DISPLAY_WIDTH + 16;
            int x = (i * 73 - (int)(phase * 3 % cycle)) % cycle;
            int y = (i * 47 + phase * fall) % (DISPLAY_HEIGHT + 24) - 12;
            int length;

            if (x < 0)
                x += cycle;
            x -= 8;
            for (length = 0; length < dropLength; length++)
            {
                int px = x - length / 2;
                int py = y + length;

                if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                    sWeatherPixels[py * DISPLAY_WIDTH + px] = RGBA(185, 215, 235, 150);
            }
        }
    }
    else if (effect == DIORAMA_WEATHER_ASH)
    {
        for (i = 0; i < 24; i++)
        {
            int cycleX = DISPLAY_WIDTH + 12;
            int x = (i * 67 + phase * 2) % cycleX - 6;
            int y = (i * 41 + phase * 2) % (DISPLAY_HEIGHT + 12) - 6;
            int size = (i % 3 == 0) ? 2 : 1;
            int px;
            int py;

            for (py = y; py < y + size; py++)
                for (px = x; px < x + size; px++)
                    if (px >= 0 && px < DISPLAY_WIDTH
                     && py >= 0 && py < DISPLAY_HEIGHT)
                        sWeatherPixels[py * DISPLAY_WIDTH + px]
                            = RGBA(205, 198, 190, 190);
        }
    }
    else if (effect == DIORAMA_WEATHER_SNOW)
    {
        /* Flakes drifting down and swaying, nearer ones bigger and faster. */
        for (i = 0; i < 72; i++)
        {
            int nearFlake = i % 4 == 0; /* not "near": a macro on Windows */
            int cycleY = DISPLAY_HEIGHT + 8;
            int y = (int)((i * 53 + phase * (nearFlake ? 2 : 1) / 2) % cycleY) - 4;
            float sway = sinf((phase + i * 37) * 0.035f) * (nearFlake ? 5.0f : 3.0f);
            int x = (int)((i * 97 + (int)(phase / 6)) % (DISPLAY_WIDTH + 8)) - 4 + (int)sway;
            int size = nearFlake ? 2 : 1;
            int px;
            int py;

            for (py = y; py < y + size; py++)
                for (px = x; px < x + size; px++)
                    if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                        sWeatherPixels[py * DISPLAY_WIDTH + px] = RGBA(244, 248, 255, nearFlake ? 235 : 190);
        }
    }
    else if (effect == DIORAMA_WEATHER_SAND)
    {
        /* The air full of sand (the GBA's sandstorm layer) with streaks
         * driven across it. As thick as the GBA's own, which hides much of
         * the scene: thinner, the 3D view's sand haze and the battles' showed
         * hardly any (the battle environments' work). */
        int x;
        int y;

        for (y = 0; y < DISPLAY_HEIGHT; y++)
        {
            int swirl = ((y + phase / 2) % 32);
            int alpha = 66 + (swirl < 16 ? swirl : 32 - swirl);

            for (x = 0; x < DISPLAY_WIDTH; x++)
                sWeatherPixels[y * DISPLAY_WIDTH + x] = RGBA(214, 178, 112, alpha);
        }
        for (i = 0; i < 130; i++)
        {
            int cycleX = DISPLAY_WIDTH + 24;
            int px = (int)((i * 61 + phase * (5 + i % 3)) % cycleX) - 12;
            int py = (int)((i * 37 + phase / 3) % (DISPLAY_HEIGHT + 4)) - 2;
            int length = 4 + i % 5;
            int k;

            for (k = 0; k < length; k++)
                if (px + k >= 0 && px + k < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                    sWeatherPixels[py * DISPLAY_WIDTH + px + k] = RGBA(238, 214, 162, 200);
        }
    }
    else if (effect == DIORAMA_WEATHER_BUBBLES)
    {
        /* Bubbles rising and wobbling under water: small rings. */
        for (i = 0; i < 14; i++)
        {
            int cycleY = DISPLAY_HEIGHT + 16;
            int y = DISPLAY_HEIGHT + 8 - (int)((i * 59 + phase * (1 + i % 2)) % cycleY);
            int x = (i * 83) % DISPLAY_WIDTH + (int)(sinf((phase + i * 29) * 0.06f) * 3.0f);
            int big = i % 3 == 0;
            static const int8_t sSmall[][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
            static const int8_t sBig[][2] = {{-1, -2}, {0, -2}, {1, -2}, {-2, -1}, {2, -1}, {-2, 0},
                                             {2, 0}, {-2, 1}, {2, 1}, {-1, 2}, {0, 2}, {1, 2}};
            int count = big ? 12 : 4;
            int k;

            for (k = 0; k < count; k++)
            {
                int px = x + (big ? sBig[k][0] : sSmall[k][0]);
                int py = y + (big ? sBig[k][1] : sSmall[k][1]);

                if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                    sWeatherPixels[py * DISPLAY_WIDTH + px] = RGBA(200, 236, 255, 200);
            }
            if (big && x - 1 >= 0 && y - 1 >= 0 && x - 1 < DISPLAY_WIDTH && y - 1 < DISPLAY_HEIGHT)
                sWeatherPixels[(y - 1) * DISPLAY_WIDTH + x - 1] = RGBA(255, 255, 255, 230);
        }
    }
    /* The fog's banks are in the scene already (gl_air.h): no flat pane. */
    else if (effect == DIORAMA_WEATHER_FOG && !DioramaGLAir_DrawsFog())
    {
        int baseAlpha = DioramaWeather_FogAlpha(snapshot->weatherBlendEVA);
        int y;
        int x;

        for (y = 0; y < DISPLAY_HEIGHT; y++)
        {
            int band = (y + snapshot->fogScrollOffset / 8) % 48;
            int modulation = band < 24 ? band : 48 - band;
            int alpha = baseAlpha + modulation * baseAlpha / 96;

            for (x = 0; x < DISPLAY_WIDTH; x++)
                sWeatherPixels[y * DISPLAY_WIDTH + x] = RGBA(210, 220, 222, alpha);
        }
    }
    else
    {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, sWeatherTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sWeatherPixels);
    sWeatherImageDrawn = true;
    return true;
}

static bool BuildWeatherImage(const struct DioramaSceneSnapshot *snapshot)
{
    return BuildWeatherImageFor(snapshot->weather, snapshot);
}

static void DrawTexture(const struct DioramaTexture *texture,
                        int outputWidth, int outputHeight,
                        float x, float y, float width, float height,
                        float sourceX, float sourceY, float sourceWidth, float sourceHeight,
                        bool blend, float opacity);

/* Rain, snow and the like over the whole window at one whole-number scale
 * (not stretched to the window's shape), centred, the overhang cut off. */
static void DrawWeatherImage(int outputWidth, int outputHeight, float opacity)
{
    int coverScale = (outputWidth + DISPLAY_WIDTH - 1) / DISPLAY_WIDTH;
    int coverWidth;
    int coverHeight;

    if ((outputHeight + DISPLAY_HEIGHT - 1) / DISPLAY_HEIGHT > coverScale)
        coverScale = (outputHeight + DISPLAY_HEIGHT - 1) / DISPLAY_HEIGHT;
    coverWidth = DISPLAY_WIDTH * coverScale;
    coverHeight = DISPLAY_HEIGHT * coverScale;
    DrawTexture(&sWeatherTexture, outputWidth, outputHeight,
                (outputWidth - coverWidth) / 2, (outputHeight - coverHeight) / 2,
                coverWidth, coverHeight,
                0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, opacity);
}

bool DioramaGL_DrawBattleWeather(uint8_t weather, int outputWidth, int outputHeight, float opacity)
{
    if (!sHasRenderedSceneSnapshot || opacity <= 0.0f || sWeatherTexture.id == 0
     || !BuildWeatherImageFor(weather, &sRenderedSceneSnapshot))
        return false;
    glViewport(0, 0, outputWidth, outputHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglActiveTexture(GL_TEXTURE0);
    DrawWeatherImage(outputWidth, outputHeight, opacity > 1.0f ? 1.0f : opacity);
    return true;
}

static const char sVertexShaderSource[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "out vec2 uv;\n"
    "void main() { gl_Position = vec4(position.xy * position.z, 0.0, position.z); uv = texCoord; }\n";

static const char sFragmentShaderSource[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform float opacity;\n"
    "void main() { vec4 sampleColor = texture(image, uv); color = vec4(sampleColor.rgb, sampleColor.a * opacity); }\n";

static GLuint CompileShader(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint compiled;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[1024];
        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("Diorama shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sVertexShaderSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sFragmentShaderSource);
    GLint linked;

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
            dglDeleteShader(vertexShader);
        if (fragmentShader != 0)
            dglDeleteShader(fragmentShader);
        return false;
    }

    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vertexShader);
    dglAttachShader(sProgram, fragmentShader);
    dglLinkProgram(sProgram);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[1024];
        dglGetProgramInfoLog(sProgram, sizeof(log), NULL, log);
        SDL_Log("Diorama shader link failed: %s", log);
        return false;
    }
    return true;
}

static void ConfigureTexture(GLuint texture)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static struct DioramaTexture LoadTexture(const char *path)
{
    struct DioramaTexture texture = {0};
    // Optional border artwork; BMP on every platform (this build has no SDL_image).
    SDL_Surface *loaded = SDL_LoadBMP(path);
    SDL_Surface *rgba;

    if (loaded == NULL)
    {
        SDL_Log("Diorama artwork could not be loaded (%s): %s", path, SDL_GetError());
        return texture;
    }
    rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(loaded);
    if (rgba == NULL)
    {
        SDL_Log("Diorama artwork conversion failed (%s): %s", path, SDL_GetError());
        return texture;
    }

    texture.width = rgba->w;
    texture.height = rgba->h;
    glGenTextures(1, &texture.id);
    ConfigureTexture(texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    SDL_FreeSurface(rgba);
    return texture;
}

static void DrawTexture(const struct DioramaTexture *texture,
                        int outputWidth, int outputHeight,
                        float x, float y, float width, float height,
                        float sourceX, float sourceY, float sourceWidth, float sourceHeight,
                         bool blend, float opacity)
{
    float left = x * 2.0f / outputWidth - 1.0f;
    float right = (x + width) * 2.0f / outputWidth - 1.0f;
    float top = 1.0f - y * 2.0f / outputHeight;
    float bottom = 1.0f - (y + height) * 2.0f / outputHeight;
    float u0 = sourceX / texture->width;
    float v0 = sourceY / texture->height;
    float u1 = (sourceX + sourceWidth) / texture->width;
    float v1 = (sourceY + sourceHeight) / texture->height;
    const float vertices[] = {
        left,  top,    1.0f, u0, v0,
        right, top,    1.0f, u1, v0,
        right, bottom, 1.0f, u1, v1,
        left,  top,    1.0f, u0, v0,
        right, bottom, 1.0f, u1, v1,
        left,  bottom, 1.0f, u0, v1,
    };

    if (texture->id == 0 || outputWidth <= 0 || outputHeight <= 0)
        return;
    if (blend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glBindTexture(GL_TEXTURE_2D, texture->id);
    dglUniform1f(sOpacityUniform, opacity);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void UploadAtlasSlots(const struct DioramaTexture *texture, const u32 *pixels,
                             const u8 *updatedMetatiles)
{
    int row;

    glBindTexture(GL_TEXTURE_2D, texture->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, DIORAMA_ATLAS_WIDTH);
    for (row = 0; row < DIORAMA_ATLAS_ROWS; row++)
    {
        int column = 0;

        while (column < DIORAMA_ATLAS_COLUMNS)
        {
            int first;
            int count;
            int x;
            int y;

            while (column < DIORAMA_ATLAS_COLUMNS
                && !(updatedMetatiles[(row * DIORAMA_ATLAS_COLUMNS + column) / 8]
                   & (1 << ((row * DIORAMA_ATLAS_COLUMNS + column) % 8))))
                column++;
            first = column;
            while (column < DIORAMA_ATLAS_COLUMNS
                && (updatedMetatiles[(row * DIORAMA_ATLAS_COLUMNS + column) / 8]
                  & (1 << ((row * DIORAMA_ATLAS_COLUMNS + column) % 8))))
                column++;
            count = column - first;
            if (count == 0)
                continue;
            x = first * DIORAMA_ATLAS_STRIDE;
            y = row * DIORAMA_ATLAS_STRIDE;
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y,
                            count * DIORAMA_ATLAS_STRIDE, DIORAMA_ATLAS_STRIDE,
                            GL_BGRA, GL_UNSIGNED_BYTE,
                            pixels + y * DIORAMA_ATLAS_WIDTH + x);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/* PKMN_DIORAMA_LOG=1: log the camera's map crossings and the neighbour pages. */
static bool LogEnabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

        sEnabled = value != NULL && value[0] == '1';
    }
    return sEnabled;
}

/* Returns true when the atlases changed (rebuilt, or slots updated). */
static bool EnsureAtlas(const struct DioramaSceneSnapshot *snapshot)
{
    /* The neighbouring maps' pages first (metatile_atlas.h): another tileset
     * on one redraws the atlas, as another map does. */
    u32 pageSignature = DioramaAtlas_SetPages(snapshot);
    bool reset = snapshot->mapGeneration != sAtlasMapGeneration
              || snapshot->paletteGeneration != sAtlasPaletteGeneration
              || pageSignature != sAtlasPageSignature
              || !sHasAtlasTileGraphics;
    bool animationChanged = snapshot->tilesetAnimationGeneration != sAtlasAnimationGeneration;
    bool changed;
    unsigned cellCount = snapshot->visibleCellCount;
    unsigned i;

    if (reset)
    {
        DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
        memset(sBaseAtlasPixels, 0, sizeof(sBaseAtlasPixels));
        memset(sForegroundAtlasPixels, 0, sizeof(sForegroundAtlasPixels));
        sAtlasMapGeneration = snapshot->mapGeneration;
        sAtlasPaletteGeneration = snapshot->paletteGeneration;
        sAtlasAnimationGeneration = snapshot->tilesetAnimationGeneration;
        if (pageSignature != sAtlasPageSignature && LogEnabled())
            SDL_Log("Diorama: neighbour pages changed (the highest in use: %u); atlas redrawn",
                    DioramaAtlas_HighestPage());
        sAtlasPageSignature = pageSignature;
    }
    memset(sDirtyAtlasTiles, 0, sizeof(sDirtyAtlasTiles));
    memset(sForcedMetatiles, 0, sizeof(sForcedMetatiles));
    memset(sVisibleMetatiles, 0, sizeof(sVisibleMetatiles));
    memset(sUpdatedMetatiles, 0, sizeof(sUpdatedMetatiles));
    if (!reset && animationChanged)
    {
        for (i = 0; i < DIORAMA_TILE_COUNT; i++)
            if (memcmp(snapshot->tileGraphics + i * DIORAMA_TILE_BYTES,
                       sAtlasTileGraphics + i * DIORAMA_TILE_BYTES,
                       DIORAMA_TILE_BYTES) != 0)
                sDirtyAtlasTiles[i / 8] |= 1 << (i % 8);
    }
    if (cellCount > DIORAMA_MAX_VISIBLE_CELLS)
        cellCount = DIORAMA_MAX_VISIBLE_CELLS;
    {
        struct ResolveKey key;

        memset(&key, 0, sizeof(key));
        key.gridOriginX = snapshot->gridOriginX;
        key.gridOriginY = snapshot->gridOriginY;
        key.visibleCellCount = (u16)cellCount;
        key.mapLayoutId = snapshot->mapLayoutId;
        key.mapGroup = snapshot->mapGroup;
        key.mapNum = snapshot->mapNum;
        key.mapCoordinateOffset = snapshot->mapCoordinateOffset;
        key.mapGeneration = snapshot->mapGeneration;
        key.rulesGeneration = DioramaRules_GetGeneration();
        if (!sResolveValid || snapshot->dirtyCellCount != 0 || snapshot->dirtyOverflow
         || memcmp(&key, &sResolveKey, sizeof(key)) != 0
         || memcmp(sResolveCells, snapshot->cells, cellCount * sizeof(sResolveCells[0])) != 0)
        {
            DioramaRules_ResolveGrid(snapshot, sAtlasResolvedCells);
            DioramaBuildings_FlattenResolvedCells(snapshot, sAtlasResolvedCells, cellCount);
            /* Kept for the next snapshot only when no dirty cell shaped it. */
            sResolveValid = snapshot->dirtyCellCount == 0 && !snapshot->dirtyOverflow;
            sResolveKey = key;
            memcpy(sResolveCells, snapshot->cells, cellCount * sizeof(sResolveCells[0]));
        }
    }
    for (i = 0; i < DIORAMA_ATLAS_SLOT_COUNT; i++)
    {
        sCutoutBaseMetatiles[i] = 0xFFFF;
        if (reset)
            sPreviousCutoutBaseMetatiles[i] = 0xFFFF;
    }
    for (i = 0; i < cellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        uint16_t slot = DioramaAtlas_CellSlot(cell);
        /* A cell's base is a metatile of its own map's tilesets. */
        uint16_t baseSlot = DioramaAtlas_Slot(cell->atlasPage, sAtlasResolvedCells[i].baseMetatileId);

        if (slot >= DIORAMA_ATLAS_SLOT_COUNT)
            continue;
        sVisibleMetatiles[slot / 8] |= 1 << (slot % 8);
        if (baseSlot >= DIORAMA_ATLAS_SLOT_COUNT)
            continue;
        if (sCutoutBaseMetatiles[slot] == 0xFFFF)
            sCutoutBaseMetatiles[slot] = baseSlot;
        else if (sCutoutBaseMetatiles[slot] != baseSlot)
            sCutoutBaseMetatiles[slot] = 0xFFFE;
    }
    for (i = 0; i < DIORAMA_ATLAS_SLOT_COUNT; i++)
        if ((sVisibleMetatiles[i / 8] & (1 << (i % 8)))
         && sCutoutBaseMetatiles[i] != sPreviousCutoutBaseMetatiles[i])
            sForcedMetatiles[i / 8] |= 1 << (i % 8);
    changed = DioramaAtlas_UpdateDirty(snapshot, sCutoutBaseMetatiles,
                                       reset ? NULL : sDirtyAtlasTiles,
                                       reset ? NULL : sForcedMetatiles,
                                       sAtlasPixels, sBaseAtlasPixels,
                                       sForegroundAtlasPixels, sPresentMetatiles,
                                       sUpdatedMetatiles);
    if (reset)
    {
        /* The map's rows and those of the pages in use: the rest is unused. */
        int height = (DIORAMA_ATLAS_MAP_ROWS + DioramaAtlas_HighestPage() * DIORAMA_ATLAS_PAGE_ROWS)
                   * DIORAMA_ATLAS_STRIDE;

        glBindTexture(GL_TEXTURE_2D, sAtlasTexture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, sAtlasPixels);
        glBindTexture(GL_TEXTURE_2D, sBaseAtlasTexture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, sBaseAtlasPixels);
        glBindTexture(GL_TEXTURE_2D, sForegroundAtlasTexture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DIORAMA_ATLAS_WIDTH, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, sForegroundAtlasPixels);
    }
    else if (changed)
    {
        UploadAtlasSlots(&sAtlasTexture, sAtlasPixels, sUpdatedMetatiles);
        UploadAtlasSlots(&sBaseAtlasTexture, sBaseAtlasPixels, sUpdatedMetatiles);
        UploadAtlasSlots(&sForegroundAtlasTexture, sForegroundAtlasPixels,
                         sUpdatedMetatiles);
    }
    memcpy(sAtlasTileGraphics, snapshot->tileGraphics, sizeof(sAtlasTileGraphics));
    for (i = 0; i < DIORAMA_ATLAS_SLOT_COUNT; i++)
        if (sVisibleMetatiles[i / 8] & (1 << (i % 8)))
            sPreviousCutoutBaseMetatiles[i] = sCutoutBaseMetatiles[i];
    sHasAtlasTileGraphics = true;
    sAtlasAnimationGeneration = snapshot->tilesetAnimationGeneration;
    return reset || changed;
}

/*
 * How strong the sun's shadows (gl_shadow.h) are: none indoors - the 3DS
 * port's rule, "indoors nothing casts" - or under rain, fog, ash, sand or an
 * overcast sky (the 3DS port's weather light leaves its shadows all but
 * gone there too); half by night (the game's own clock, OW_ENABLE_DNS: the
 * moon's are fainter, but without any the night tint leaves the scene
 * flat); full otherwise.
 */
static float SunStrength(const struct DioramaSceneSnapshot *snapshot)
{
    switch (snapshot->mapType)
    {
    case MAP_TYPE_TOWN:
    case MAP_TYPE_CITY:
    case MAP_TYPE_ROUTE:
    case MAP_TYPE_OCEAN_ROUTE:
        break;
    default:
        return 0.0f;
    }
    switch (snapshot->weather)
    {
    case WEATHER_NONE:
    case WEATHER_SUNNY_CLOUDS:
    case WEATHER_SUNNY:
    case WEATHER_DROUGHT:
        break;
    default:
        return 0.0f;
    }
    return snapshot->timeOfDay == TIME_NIGHT ? 0.5f : 1.0f;
}

static void GetCameraPosition(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaSceneSnapshot *previousSnapshot,
                              bool hasPreviousSnapshot, float frameAlpha,
                              float *cameraX, float *cameraZ)
{
    float currentX = snapshot->cameraMapX
                   + (snapshot->cameraSubpixelX + snapshot->cameraPanX) / 16.0f;
    float currentZ = -(snapshot->cameraMapY
                     + (snapshot->cameraSubpixelY + snapshot->cameraPanY) / 16.0f);

    *cameraX = currentX;
    *cameraZ = currentZ;
    if (hasPreviousSnapshot
     && snapshot->sequence == previousSnapshot->sequence + 1
     && snapshot->mapGeneration == previousSnapshot->mapGeneration
     && (snapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_FREE
      || snapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_SCRIPTED)
     && (previousSnapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_FREE
      || previousSnapshot->sceneKind == DIORAMA_SCENE_OVERWORLD_SCRIPTED))
    {
        float previousX = previousSnapshot->cameraMapX
                        + (previousSnapshot->cameraSubpixelX
                        + previousSnapshot->cameraPanX) / 16.0f;
        float previousZ = -(previousSnapshot->cameraMapY
                          + (previousSnapshot->cameraSubpixelY
                          + previousSnapshot->cameraPanY) / 16.0f);
        float deltaX = currentX - previousX;
        float deltaZ = currentZ - previousZ;

        if (deltaX * deltaX + deltaZ * deltaZ
         <= CAMERA_TELEPORT_LIMIT * CAMERA_TELEPORT_LIMIT)
        {
            *cameraX = previousX + deltaX * frameAlpha;
            *cameraZ = previousZ + deltaZ * frameAlpha;
        }
    }
}

/*
 * The camera (diorama/camera.h): the graphics overhaul's Adventure, Diorama
 * and Photo cameras, eased in real time from the game camera's focus; the eye
 * each field frame is drawn from goes to the shaders through terrain_mesh.h's
 * DioramaCameraRig and is put back to the overworld's default afterwards, so a
 * frame drawn by anyone else (a 3D battle's camera) is unchanged. The survey
 * views (F5, a development aid) keep their fixed pitches.
 */
static struct DioramaCameraView sCameraView;
static Uint64 sCameraClock;
static Uint64 sCameraLastDrawn;
static bool sCameraKnown;
static uint8_t sCameraMapGroup;
static uint8_t sCameraMapNum;

static enum DioramaCameraPlace CameraPlace(uint8_t mapType)
{
    switch (mapType)
    {
    case MAP_TYPE_INDOOR:
    case MAP_TYPE_SECRET_BASE:
        return DIORAMA_CAMERA_INDOORS;
    case MAP_TYPE_UNDERGROUND:
        return DIORAMA_CAMERA_CAVE;
    default:
        return DIORAMA_CAMERA_OUTDOORS;
    }
}

/* A map crossing: the cells the new map shows of the one just left say how
 * far its coordinates moved (as the dapples' anchor in gl_light.c). A warp
 * has none: the view jumps. */
static bool CameraCrossing(const struct DioramaSceneSnapshot *snapshot, float *shiftX, float *shiftZ)
{
    unsigned i;

    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        if ((cell->flags & DIORAMA_CELL_CONNECTED)
         && cell->sourceMapGroup == sCameraMapGroup
         && cell->sourceMapNum == sCameraMapNum)
        {
            /* The old map's (sourceMapX + MAP_OFFSET) is the new map's mapX. */
            *shiftX = (float)(cell->mapX - (cell->sourceMapX + snapshot->mapCoordinateOffset));
            *shiftZ = -(float)(cell->mapY - (cell->sourceMapY + snapshot->mapCoordinateOffset));
            return true;
        }
    }
    return false;
}

/*
 * The colour past the scene's cells and of the fog at their edge (graphics
 * overhaul, phase 1): outdoors a clear sky's pale haze, turned towards the
 * weather's haze when it has one, through the palette's fade and time-of-day
 * tint (gl_light.h's air light), so a warp's fade and the night reach it as
 * they reach the map. Indoors and in caves the dark stays (false).
 */
static bool SkyColour(const struct DioramaSceneSnapshot *snapshot, float rgb[3])
{
    static const float sClearSky[3] = {0.66f, 0.80f, 0.88f};
    struct DioramaAirLight air;
    float haze[3];
    float hazeColour[3];

    if (snapshot->mapType == MAP_TYPE_INDOOR || snapshot->mapType == MAP_TYPE_SECRET_BASE
     || snapshot->mapType == MAP_TYPE_UNDERGROUND || snapshot->mapType == MAP_TYPE_UNDERWATER)
        return false;
    DioramaGLLight_GetAir(&air);
    DioramaGLLight_GetHaze(0.7f, haze, hazeColour);
    for (int i = 0; i < 3; i++)
    {
        float value = sClearSky[i] * air.fadeScale[i] + air.fadeOffset[i];

        if (haze[2] > 0.0f)
            value += (hazeColour[i] - value) * (haze[2] < 0.85f ? haze[2] : 0.85f);
        rgb[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
    return true;
}

static void UpdateCamera(const struct DioramaSceneSnapshot *snapshot, float rawX, float rawZ)
{
    static const float sSurveyPitches[] = {
        0.0f, 0.26179939f, 0.61086524f, 0.87266463f, 1.30899694f,
    };
    struct DioramaCameraInput input;
    Uint64 now = SDL_GetPerformanceCounter();
    double frequency = (double)SDL_GetPerformanceFrequency();

    if (sSurveyView > 0)
    {
        DioramaCamera_Frame(sSurveyPitches[sSurveyView], 33.0f, 34.0f, 0.0f, &sCameraView);
        sCameraView.x = rawX;
        sCameraView.z = rawZ;
        sCameraClock = now;
        return;
    }
    memset(&input, 0, sizeof(input));
    input.targetX = rawX;
    input.targetZ = rawZ;
    input.groundY = DioramaGLObjects_GetPlayerGround();
    input.place = CameraPlace(snapshot->mapType);
    input.seconds = sCameraClock != 0 ? (float)((double)(now - sCameraClock) / frequency) : 0.0f;
    /* Back to the field after a while away (a battle, a menu drawn flat). */
    input.snap = sCameraLastDrawn == 0 || (double)(now - sCameraLastDrawn) / frequency > 0.5;
    if (sCameraKnown
     && (snapshot->mapGroup != sCameraMapGroup || snapshot->mapNum != sCameraMapNum))
    {
        input.shift = CameraCrossing(snapshot, &input.shiftX, &input.shiftZ);
        if (!input.shift)
            input.snap = true;
        if (LogEnabled())
            SDL_Log("Diorama: camera %s from map %u.%u to %u.%u (shift %.0f, %.0f)",
                    input.shift ? "carried across" : "cut", sCameraMapGroup, sCameraMapNum,
                    snapshot->mapGroup, snapshot->mapNum, input.shiftX, input.shiftZ);
    }
    switch (snapshot->playerFacingDirection)
    {
    case DIR_SOUTH: input.facingZ = -1.0f; break;
    case DIR_NORTH: input.facingZ = 1.0f; break;
    case DIR_WEST: input.facingX = -1.0f; break;
    case DIR_EAST: input.facingX = 1.0f; break;
    default: break;
    }
    DioramaCamera_Update(&input, &sCameraView);
    sCameraMapGroup = snapshot->mapGroup;
    sCameraMapNum = snapshot->mapNum;
    sCameraKnown = true;
    sCameraClock = now;
    sCameraLastDrawn = now;
}

/* The field frame's eye for the shaders (terrain_mesh.h). */
static void ApplyCameraRig(float aspectCorrection)
{
    gDioramaCameraRig.active = true;
    gDioramaCameraRig.height = sCameraView.height;
    gDioramaCameraRig.distance = sCameraView.distance;
    gDioramaCameraRig.screenOffsetY = DIORAMA_OVERWORLD_SCREEN_OFFSET;
    gDioramaCameraRig.aspectCorrection = aspectCorrection;
}

static void SnapOpacity(float opacity)
{
    sTwoDOpacity = opacity;
    sFadeStartOpacity = opacity;
    sFadeTargetOpacity = opacity;
    sFadeStartCounter = SDL_GetPerformanceCounter();
}

static void SetOpacityTarget(float opacity)
{
    if (opacity == sFadeTargetOpacity)
        return;
    sFadeStartOpacity = sTwoDOpacity;
    sFadeTargetOpacity = opacity;
    sFadeStartCounter = SDL_GetPerformanceCounter();
}

static void UpdateOpacity(void)
{
    double elapsed;
    float progress;

    if (sTwoDOpacity == sFadeTargetOpacity)
        return;
    elapsed = (double)(SDL_GetPerformanceCounter() - sFadeStartCounter)
            / SDL_GetPerformanceFrequency();
    progress = elapsed >= SOURCE_FADE_SECONDS ? 1.0f : elapsed / SOURCE_FADE_SECONDS;
    sTwoDOpacity = sFadeStartOpacity
                 + (sFadeTargetOpacity - sFadeStartOpacity) * progress;
}

/* PKMN_DIORAMA_WEATHER=<number> (constants/weather.h): draw the 3D view as
 * if that weather were up, to see its rain, snow, sand, bubbles, fog and
 * light on any map. The game's own weather (and its palettes) is unchanged. */
static void PreviewWeather(struct DioramaSceneSnapshot *snapshot)
{
    static int sPreview = -2;

    if (sPreview == -2)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_WEATHER");

        sPreview = value != NULL && value[0] >= '0' && value[0] <= '9' ? SDL_atoi(value) : -1;
        if (sPreview >= WEATHER_COUNT)
            sPreview = -1;
        if (sPreview >= 0)
            SDL_Log("3D view: previewing weather %d (the game's own weather is unchanged)", sPreview);
    }
    if (sPreview < 0)
        return;
    snapshot->weather = snapshot->nextWeather = (uint8_t)sPreview;
    if (DioramaWeather_Classify(sPreview) == DIORAMA_WEATHER_RAIN && snapshot->rainVisibleCount == 0)
        snapshot->rainVisibleCount = sPreview == WEATHER_DOWNPOUR ? 24 : 16;
    if (DioramaWeather_Classify(sPreview) == DIORAMA_WEATHER_FOG)
        snapshot->weatherBlendEVA = 12;
    if (DioramaWeather_Classify(sPreview) == DIORAMA_WEATHER_BUBBLES)
        snapshot->weatherBlendEVA = 4;
}

/*
 * A battle transition playing on the field: the scene is a TRANSITION (the
 * field runs CB2_OverworldBasic) with nothing else against drawing it, and
 * gl_battle.c can compose the transition over the 3D field.
 */
static bool IsComposableTransition(const struct DioramaSceneSnapshot *snapshot)
{
    return snapshot->battleTransition
        && snapshot->sceneKind == DIORAMA_SCENE_TRANSITION
        && snapshot->fallbackReasons == DIORAMA_FALLBACK_UNSUPPORTED_SCENE
        && snapshot->valid && snapshot->tilesetResourcesValid
        && snapshot->visibleCellCount == DIORAMA_MAX_VISIBLE_CELLS
        && snapshot->objectCount <= DIORAMA_MAX_OBJECTS
        && DioramaGLBattle_CanComposeField();
}

static void AdoptLatestSnapshot(void)
{
    if (!DioramaSnapshotExchange_CopyLatest(&sIncomingSceneSnapshot))
        return;
    PreviewWeather(&sIncomingSceneSnapshot);
    if (!sHasSceneSnapshot)
    {
        sSceneSnapshot = sIncomingSceneSnapshot;
        sPreviousSceneSnapshot = sIncomingSceneSnapshot;
        sHasSceneSnapshot = true;
        sCurrent3DReady = false;
    }
    else if (sIncomingSceneSnapshot.sequence != sSceneSnapshot.sequence)
    {
        sPreviousSceneSnapshot = sSceneSnapshot;
        sSceneSnapshot = sIncomingSceneSnapshot;
        sHasPreviousSceneSnapshot = true;
        sCurrent3DReady = false;
    }
    if ((sSceneSnapshot.sceneKind == DIORAMA_SCENE_OVERWORLD_FREE
      || sSceneSnapshot.sceneKind == DIORAMA_SCENE_OVERWORLD_SCRIPTED
      || sSceneSnapshot.sceneKind == DIORAMA_SCENE_DIALOGUE
      || sSceneSnapshot.sceneKind == DIORAMA_SCENE_MENU)
     && sSceneSnapshot.valid && sSceneSnapshot.tilesetResourcesValid
     && sSceneSnapshot.fallbackReasons == DIORAMA_FALLBACK_NONE
     && sSceneSnapshot.visibleCellCount == DIORAMA_MAX_VISIBLE_CELLS
     && sSceneSnapshot.objectCount <= DIORAMA_MAX_OBJECTS
     && !sSceneSnapshot.paletteFading
     && (!sHasLastFieldSnapshot || sLastFieldSnapshot.sequence != sSceneSnapshot.sequence))
    {
        sLastFieldSnapshot = sSceneSnapshot;
        sHasLastFieldSnapshot = true;
    }
}

bool DioramaGL_Init(SDL_Window *window, u8 backgroundCount)
{
    sWindow = window;
    sBackgroundCount = backgroundCount;
    sContext = SDL_GL_CreateContext(window);
    if (sContext == NULL)
    {
        SDL_Log("OpenGL context could not be created: %s", SDL_GetError());
        return false;
    }
    if (!DioramaGL_LoadFunctions() || !CreateProgram())
        return false;
    DioramaProfile_Init();
    sTerrainAvailable = DioramaGLTerrain_Init();
    if (!sTerrainAvailable)
        SDL_Log("Diorama terrain renderer unavailable; retaining the 2D OpenGL fallback");
    sObjectsAvailable = DioramaGLObjects_Init();
    if (!sObjectsAvailable)
        SDL_Log("Diorama object renderer unavailable; retaining the 2D OpenGL fallback");
    // Modelled buildings (3DS port); without them houses keep the Diorama's
    // own extrusion.
    if (!DioramaGLBuildings_Init())
        SDL_Log("Diorama building models unavailable; houses stay extruded");
    if (!DioramaGLShadow_Init())
        SDL_Log("Diorama sun shadows unavailable");
    /* The 3DS project's trees, whenever the tree style asks for them (F8,
     * tree_sprites.h). */
    if (!DioramaGLTreeSprites_Init())
        SDL_Log("Diorama 3DS trees unavailable; the voxel trees stay");
    /* World detail objects (props.h); without their pass they stay flat. */
    if (!DioramaGLProps_Init())
    {
        SDL_Log("Diorama detail objects unavailable; they stay flat");
        DioramaProps_SetEnabled(false);
    }
    // The 3D battle (gl_battle.c); without it battles stay 2D.
    if (!DioramaGLBattle_Init())
        SDL_Log("Diorama 3D battles unavailable; battles stay 2D");

    dglGenVertexArrays(1, &sVertexArray);
    dglBindVertexArray(sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    dglUseProgram(sProgram);
    dglActiveTexture(GL_TEXTURE0);
    sOpacityUniform = dglGetUniformLocation(sProgram, "opacity");
    if (sOpacityUniform < 0)
        return false;

    glGenTextures(1, &sFrameTexture.id);
    sFrameTexture.width = DISPLAY_WIDTH;
    sFrameTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sFrameTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sDebugTexture.id);
    sDebugTexture.width = DISPLAY_WIDTH;
    sDebugTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sDebugTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                   GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sUiTexture.id);
    sUiTexture.width = DISPLAY_WIDTH;
    sUiTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sUiTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    sUiUploaded = false;
    sWeatherKeyValid = false;
    glGenTextures(1, &sWeatherTexture.id);
    sWeatherTexture.width = DISPLAY_WIDTH;
    sWeatherTexture.height = DISPLAY_HEIGHT;
    ConfigureTexture(sWeatherTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sAtlasTexture.id);
    sAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sBaseAtlasTexture.id);
    sBaseAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sBaseAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sBaseAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glGenTextures(1, &sForegroundAtlasTexture.id);
    sForegroundAtlasTexture.width = DIORAMA_ATLAS_WIDTH;
    sForegroundAtlasTexture.height = DIORAMA_ATLAS_HEIGHT;
    ConfigureTexture(sForegroundAtlasTexture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_ATLAS_WIDTH, DIORAMA_ATLAS_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    DioramaAtlas_Clear(sAtlasPixels, sPresentMetatiles);
    memset(sBaseAtlasPixels, 0, sizeof(sBaseAtlasPixels));
    memset(sForegroundAtlasPixels, 0, sizeof(sForegroundAtlasPixels));
    sAtlasMapGeneration = 0;
    sAtlasPaletteGeneration = 0;
    sAtlasAnimationGeneration = 0;
    sHasAtlasTileGraphics = false;
    sHasSceneSnapshot = false;
    sHasPreviousSceneSnapshot = false;
    sHasRenderedSceneSnapshot = false;
    sHasPreviousRenderedSceneSnapshot = false;
    sCurrent3DReady = false;
    sProcessedSequence = 0;
    sUiTransientHoldFrames = 0;
    SnapOpacity(1.0f);
    sTerrainDebug = false;
    DioramaCamera_Reset();
    sCameraKnown = false;
    sCameraClock = sCameraLastDrawn = 0;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

void DioramaGL_LoadArtwork(void)
{
    for (int i = 0; i < sBackgroundCount; i++)
    {
        char filename[16];
        SDL_snprintf(filename, sizeof(filename), i == 0 ? "BG.bmp" : "BG%d.bmp", i);
        sBackgroundTextures[i] = LoadTexture(filename);
    }
    sBorderTexture = LoadTexture("Border.bmp");
}

static void EnsureFrameTexture(void)
{
    if (sPendingFrame == NULL)
        return;
    glBindTexture(GL_TEXTURE_2D, sFrameTexture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sPendingFrame);
    sPendingFrame = NULL;
}

void DioramaGL_UploadFrame(const u32 *argb8888)
{
    AdoptLatestSnapshot();
    sPendingFrame = argb8888;
    /* The text boxes over the 3D view are drawn only while the game shows
     * some (DioramaGL_Present); without any there is nothing to make. */
    if (!sHasSceneSnapshot || sSceneSnapshot.uiRectCount == 0)
        return;
    DioramaUI_RenderBgOverlay(
        DISPLAY_WIDTH, DISPLAY_HEIGHT,
        sHasSceneSnapshot ? sSceneSnapshot.uiRects : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.uiRectCount : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgControl : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgHOffset : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgVOffset : 0,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgTileGraphics : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.uiBgTilemap : NULL,
        sHasSceneSnapshot ? sSceneSnapshot.fadedPalette : NULL,
        sUiScratch);
    if (sUiUploaded && memcmp(sUiScratch, sUiPixels, sizeof(sUiPixels)) == 0)
        return;
    memcpy(sUiPixels, sUiScratch, sizeof(sUiPixels));
    glBindTexture(GL_TEXTURE_2D, sUiTexture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    GL_BGRA, GL_UNSIGNED_BYTE, sUiPixels);
    sUiUploaded = true;
}

void DioramaGL_Present(u8 background, bool border, bool integerScale, float frameAlpha)
{
    int outputWidth;
    int outputHeight;
    int gameWidth;
    int gameHeight;
    int gameX;
    int gameY;
    const struct DioramaTexture *gameTexture = &sFrameTexture;
    bool drawTerrain = false;
    bool current3D = false;
    bool softFallback = false;
    bool newSequence = sHasSceneSnapshot && sSceneSnapshot.sequence != sProcessedSequence;
    enum DioramaPresentationDecision presentationDecision;
    float cameraX;
    float cameraZ;
    int pictureFlags = 0;

    if (sSurveyView >= 0)
        frameAlpha = 1.0f;
    sIntegerScale = integerScale;
    // A battle in 3D draws its whole frame; the field's scene, atlases and
    // models stay as they were for it (gl_battle.c).
    if (sSurveyView < 0 && DioramaGLBattle_Present(sWindow, frameAlpha))
    {
        sDrewField3D = false;
        sLastFrame3D = true;
        // The PC layer (the F10 menu, the frame rate) over the battle too: the
        // menu takes the keys while it is open, so it must be seen.
        if (sOverlay != NULL)
        {
            int overlayWidth;
            int overlayHeight;

            SDL_GL_GetDrawableSize(sWindow, &overlayWidth, &overlayHeight);
            if (overlayWidth > 0 && overlayHeight > 0)
            {
                glViewport(0, 0, overlayWidth, overlayHeight);
                sOverlay(overlayWidth, overlayHeight);
            }
        }
        SDL_GL_SwapWindow(sWindow);
        return;
    }

    DioramaProfile_Begin(DIORAMA_PROF_FRAME);
    if (sRenderMode == DIORAMA_RENDER_AUTO
     && sTerrainAvailable
     && sObjectsAvailable
     && sHasSceneSnapshot
     && DioramaRules_IsMapSupported(sSceneSnapshot.mapGroup, sSceneSnapshot.mapNum,
                                    sSceneSnapshot.mapLayoutId)
     && (DioramaSnapshot_CanRenderFlatMap(&sSceneSnapshot) || IsComposableTransition(&sSceneSnapshot)))
    {
        if (!sCurrent3DReady)
        {
            DioramaProfile_Begin(DIORAMA_PROF_SYNC);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_ATLAS);
            EnsureAtlas(&sSceneSnapshot);
            DioramaProfile_End(DIORAMA_PROF_SYNC_ATLAS);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_TERRAIN);
            sCurrent3DReady = DioramaGLTerrain_Sync(&sSceneSnapshot, sAtlasResolvedCells);
            DioramaProfile_End(DIORAMA_PROF_SYNC_TERRAIN);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_OBJECTS);
            sCurrent3DReady = sCurrent3DReady
                           && DioramaGLObjects_Sync(&sSceneSnapshot, sAtlasResolvedCells);
            DioramaProfile_End(DIORAMA_PROF_SYNC_OBJECTS);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_BUILDINGS);
            DioramaGLBuildings_Sync(&sSceneSnapshot, sAtlasResolvedCells);
            DioramaProfile_End(DIORAMA_PROF_SYNC_BUILDINGS);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_TREES);
            DioramaGLTreeSprites_Sync(&sSceneSnapshot, sAtlasResolvedCells);
            DioramaProfile_End(DIORAMA_PROF_SYNC_TREES);
            DioramaProfile_Begin(DIORAMA_PROF_SYNC_PROPS);
            DioramaGLProps_Sync(&sSceneSnapshot, sAtlasResolvedCells);
            DioramaProfile_End(DIORAMA_PROF_SYNC_PROPS);
            DioramaProfile_End(DIORAMA_PROF_SYNC);
        }
        current3D = sCurrent3DReady;
    }
    else if (sRenderMode == DIORAMA_RENDER_DEBUG
          && sHasSceneSnapshot
          && DioramaSnapshot_CanRenderGrid(&sSceneSnapshot))
        gameTexture = &sDebugTexture;

    presentationDecision = DioramaTransition_Classify(
        sHasSceneSnapshot ? &sSceneSnapshot : NULL, current3D,
        &sRenderedSceneSnapshot, sHasRenderedSceneSnapshot);
    if (presentationDecision == DIORAMA_PRESENT_3D)
    {
        sUiTransientHoldFrames = 0;
        bool mapDiscontinuity = !sHasRenderedSceneSnapshot
                             || !DioramaTransition_IsSameMap(&sSceneSnapshot,
                                                             &sRenderedSceneSnapshot);

        if (!sHasRenderedSceneSnapshot
         || sRenderedSceneSnapshot.sequence != sSceneSnapshot.sequence)
        {
            if (sHasRenderedSceneSnapshot)
            {
                sPreviousRenderedSceneSnapshot = sRenderedSceneSnapshot;
                sHasPreviousRenderedSceneSnapshot = true;
            }
            sRenderedSceneSnapshot = sSceneSnapshot;
            memcpy(sRenderedResolvedCells, sAtlasResolvedCells,
                   sizeof(sRenderedResolvedCells));
            sHasRenderedSceneSnapshot = true;
        }
        if (newSequence)
        {
            if (mapDiscontinuity)
                SnapOpacity(1.0f);
            SetOpacityTarget(0.0f);
        }
        drawTerrain = true;
    }
    else if (presentationDecision == DIORAMA_PRESENT_HOLD_3D
          && sHasRenderedSceneSnapshot)
    {
        if (newSequence && sUiTransientHoldFrames < 0xFF)
            sUiTransientHoldFrames++;
        if (sUiTransientHoldFrames <= UI_TRANSIENT_HOLD_FRAMES)
        {
            SetOpacityTarget(0.0f);
            drawTerrain = true;
        }
        else
        {
            SnapOpacity(1.0f);
        }
    }
    else if (presentationDecision == DIORAMA_PRESENT_SOFT_2D)
    {
        sUiTransientHoldFrames = 0;
        softFallback = true;
        if (newSequence)
            SetOpacityTarget(1.0f);
        drawTerrain = sTwoDOpacity < 1.0f;
    }
    else if (newSequence || !sHasSceneSnapshot)
    {
        sUiTransientHoldFrames = 0;
        SnapOpacity(1.0f);
    }
    if (newSequence)
        sProcessedSequence = sSceneSnapshot.sequence;
    if (sSurveyView >= 0)
        SnapOpacity(sSurveyView == 0 ? 1.0f : 0.0f);
    UpdateOpacity();
    if (softFallback && sTwoDOpacity < 1.0f)
        drawTerrain = true;
    // A battle transition composed over the 3D field (gl_battle.c) made the
    // field's layers see-through in this frame's picture: it needs a field
    // under it, the last one drawn if this frame's is refused.
    if (!drawTerrain && sSurveyView < 0 && sHasRenderedSceneSnapshot && DioramaGLBattle_WantsField())
        drawTerrain = true;

    SDL_GL_GetDrawableSize(sWindow, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;
    if (sPictureFilter != NULL && sSurveyView < 0)
        pictureFlags = sPictureFilter();
    /* A native screen over the whole window (ENABLE_NATIVE_UI: the dressing
     * room, Players, the mart, the Game Corner over the field) leaves nothing
     * of the 3D scene to see: it is not drawn while one is up. */
    if (pictureFlags & DIORAMA_PICTURE_HIDE_ALL)
        drawTerrain = false;
    glViewport(0, 0, outputWidth, outputHeight);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);

    if (sSurveyView < 0 && background < sBackgroundCount
     && sBackgroundTextures[background].id != 0
     && !(pictureFlags & DIORAMA_PICTURE_HIDE_ALL))
        DrawTexture(&sBackgroundTextures[background], outputWidth, outputHeight,
                    0, 0, outputWidth, outputHeight,
                     0, 0, sBackgroundTextures[background].width,
                     sBackgroundTextures[background].height, true, 1.0f);

    {
        /* The 240x160 picture (text boxes, menus, 2D scenes) in whole-number
         * steps at the window's own resolution, so pixel art stays sharp at
         * every size: 'integerScale' as large as fits, otherwise within 8/9 of
         * the height (6x at 1080p, 8x at 1440p, 12x at 2160p, as before). */
        int scale = DisplaySettings_PixelScale(outputWidth, outputHeight, integerScale);

        gameWidth = DISPLAY_WIDTH * scale;
        gameHeight = DISPLAY_HEIGHT * scale;
    }
    if (sSurveyView >= 0)
    {
        gameWidth = outputWidth;
        gameHeight = outputHeight;
        gameX = 0;
        gameY = 0;
    }
    else
    {
        gameX = (outputWidth - gameWidth) / 2;
        gameY = (outputHeight - gameHeight) / 2;
    }
    if (drawTerrain)
    {
        float cameraPitch;
        float cameraFocalLength;
        float aspectCorrection;
        int sceneWidth;
        int sceneHeight;
        bool composing;

        /* The scene at the chosen resolution: the window itself, or an
         * offscreen target scaled into it afterwards (gl_target.h). */
        DioramaGLTarget_Begin(outputWidth, outputHeight, &sceneWidth, &sceneHeight);
        aspectCorrection = (float)DISPLAY_WIDTH * sceneHeight / ((float)DISPLAY_HEIGHT * sceneWidth);
        GetCameraPosition(&sRenderedSceneSnapshot, &sPreviousRenderedSceneSnapshot,
                          sHasPreviousRenderedSceneSnapshot,
                          current3D ? frameAlpha : 1.0f, &cameraX, &cameraZ);
        UpdateCamera(&sRenderedSceneSnapshot, cameraX, cameraZ);
        cameraX = sCameraView.x;
        cameraZ = sCameraView.z;
        cameraPitch = sCameraView.pitch;
        cameraFocalLength = sCameraView.focalLength;
        ApplyCameraRig(aspectCorrection);
        // This frame's weather light (gl_light.h), the sky and the fog at the
        // scene's edge, the sun's depth map (gl_shadow.h), then the lit scene.
        DioramaGLLight_Update(&sRenderedSceneSnapshot, !sTerrainDebug);
        /* The street lamps' light round the camera (gl_lamps.h), this pass only. */
        DioramaGLLamps_SetFocus(cameraX, cameraZ);
        DioramaGLLamps_SetActive(!sTerrainDebug);
        DioramaGLLamps_SetNight(DioramaGLLamps_NightFor(sRenderedSceneSnapshot.mapType,
                                                        sRenderedSceneSnapshot.timeOfDay));
        {
            float sky[3] = {0.035f, 0.055f, 0.07f}; /* indoors and caves: the dark */
            bool outdoors = SkyColour(&sRenderedSceneSnapshot, sky);

            /* Outdoors the cells fade into the sky's haze before they end, and
             * the picture past them is that colour: no board's edge. */
            /* With distant scenery (far_field.h) the haze closes in at its
             * end instead of the scene's. */
            float fogEnd = DioramaGLTerrain_FarShown() ? DIORAMA_FAR_RADIUS - 1.0f
                                                       : DIORAMA_GRID_WIDTH / 2 - 1.0f;

            DioramaGLLight_SetEdgeFog(outdoors && sSurveyView < 0,
                                      /* cell (x, y)'s centre is (x, -y) in the scene */
                                      (float)(sRenderedSceneSnapshot.gridOriginX + DIORAMA_GRID_WIDTH / 2),
                                      -(float)(sRenderedSceneSnapshot.gridOriginY + DIORAMA_GRID_HEIGHT / 2),
                                      fogEnd - (DioramaGLTerrain_FarShown() ? 17.0f : 4.5f), fogEnd, sky);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, sceneWidth, sceneHeight);
            glClearColor(sky[0], sky[1], sky[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glViewport(0, 0, sceneWidth, sceneHeight);
        }
        DioramaProfile_Begin(DIORAMA_PROF_SHADOW);
        if (DioramaGLShadow_Begin(cameraX, cameraZ,
                                  sTerrainDebug ? 0.0f : SunStrength(&sRenderedSceneSnapshot),
                                  ((uint64_t)DioramaGLBuildings_GetGeometryGeneration() << 32)
                                  | (DioramaGLTerrain_GetGeometryGeneration()
                                   ^ DioramaGLTreeSprites_GetGeometryGeneration() * 2654435761u
                                   ^ DioramaGLProps_GetGeometryGeneration() * 40503u)))
        {
            DioramaGLTerrain_DrawShadow(sAtlasTexture.id, sBaseAtlasTexture.id,
                                        sForegroundAtlasTexture.id);
            DioramaGLBuildings_DrawShadow();
            DioramaGLTreeSprites_DrawShadow();
            DioramaGLProps_DrawShadow();
            DioramaGLShadow_End(sceneWidth, sceneHeight);
        }
        DioramaProfile_End(DIORAMA_PROF_SHADOW);
        DioramaProfile_Begin(DIORAMA_PROF_TERRAIN);
        DioramaGLTerrain_Draw(sAtlasTexture.id, sBaseAtlasTexture.id,
                               sForegroundAtlasTexture.id, cameraX, cameraZ,
                               cameraPitch, cameraFocalLength,
                               aspectCorrection, sTerrainDebug);
        DioramaProfile_End(DIORAMA_PROF_TERRAIN);
        DioramaProfile_Begin(DIORAMA_PROF_TREES);
        DioramaGLTreeSprites_Draw(cameraX, cameraZ, cameraPitch, cameraFocalLength,
                                  aspectCorrection);
        DioramaProfile_End(DIORAMA_PROF_TREES);
        DioramaProfile_Begin(DIORAMA_PROF_PROPS);
        DioramaGLProps_Draw(cameraX, cameraZ, cameraPitch, cameraFocalLength, aspectCorrection);
        DioramaProfile_End(DIORAMA_PROF_PROPS);
        DioramaProfile_Begin(DIORAMA_PROF_BUILDINGS);
        DioramaGLBuildings_Draw(cameraX, cameraZ, cameraPitch, cameraFocalLength,
                                aspectCorrection);
        DioramaProfile_End(DIORAMA_PROF_BUILDINGS);
        /* Players hidden by a house or a tree are seen through it, except
         * in photo mode's pictures. */
        DioramaGLObjects_SetSilhouettes(DioramaCamera_GetMode() != DIORAMA_CAMERA_PHOTO);
        // A battle transition composed over the 3D field (gl_battle.c): the
        // ground and models laid again band by band as the transition moves
        // the GBA's map layers; the people, the weather and a dark cave's
        // light - sprites and windows on the GBA - stay where they are.
        // It works in the scene's framebuffer, at the scene's size (the
        // window's, or the offscreen target's: gl_target.h), so the GBA
        // picture's place is given in the scene's pixels.
        composing = sSurveyView < 0 && !sTerrainDebug
                 && DioramaGLBattle_ComposeFieldBands(sceneWidth, sceneHeight,
                                                      (float)gameX * sceneWidth / outputWidth,
                                                      (float)gameY * sceneHeight / outputHeight,
                                                      (float)gameWidth * sceneWidth / outputWidth,
                                                      (float)gameHeight * sceneHeight / outputHeight);
        DioramaProfile_Begin(DIORAMA_PROF_OBJECTS);
        DioramaGLObjects_Draw(current3D ? frameAlpha : 1.0f,
                               cameraX, cameraZ, cameraPitch,
                               cameraFocalLength, aspectCorrection);
        DioramaProfile_End(DIORAMA_PROF_OBJECTS);
        // The light in the air over it all (gl_air.h): dust, rays, bloom.
        DioramaProfile_Begin(DIORAMA_PROF_AIR);
        DioramaGLAir_Draw(cameraX, cameraZ, cameraPitch, cameraFocalLength,
                          aspectCorrection, sceneWidth, sceneHeight);
        DioramaProfile_End(DIORAMA_PROF_AIR);
        // A dark cave's circle of light round the player (gl_field_light.h):
        // everything but the game's text is dark outside it, as on the GBA.
        DioramaProfile_Begin(DIORAMA_PROF_FIELD_LIGHT);
        DioramaGLFieldLight_Draw(&sRenderedSceneSnapshot, current3D ? frameAlpha : 1.0f,
                                 cameraX, cameraZ, cameraPitch, cameraFocalLength,
                                 aspectCorrection, sceneWidth, sceneHeight);
        DioramaProfile_End(DIORAMA_PROF_FIELD_LIGHT);
        glDisable(GL_SCISSOR_TEST);
        gDioramaCameraRig.active = false;
        DioramaGLLight_SetEdgeFog(false, 0.0f, 0.0f, 0.0f, 0.0f, NULL);
        DioramaGLLamps_SetActive(false);
        DioramaProfile_Begin(DIORAMA_PROF_RESOLVE);
        DioramaGLTarget_End(outputWidth, outputHeight);
        DioramaProfile_End(DIORAMA_PROF_RESOLVE);
        glViewport(0, 0, outputWidth, outputHeight);
        DioramaProfile_Begin(DIORAMA_PROF_2D);
        dglUseProgram(sProgram);
        dglBindVertexArray(sVertexArray);
        if (sSurveyView < 0 && BuildWeatherImage(&sRenderedSceneSnapshot))
            DrawWeatherImage(outputWidth, outputHeight, 1.0f);
        if (sTerrainDebug)
        {
            BuildDebugImage(&sRenderedSceneSnapshot);
            DrawTexture(&sDebugTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, 1.0f);
        }
        // A battle transition composed over the 3D field (gl_battle.c): its
        // picture is the frame's whole 2D picture.
        if (composing)
        {
            DioramaGLBattle_ComposeFieldPicture(outputWidth, outputHeight, gameX, gameY,
                                                gameWidth, gameHeight);
            dglUseProgram(sProgram);
            dglBindVertexArray(sVertexArray);
        }
        /* Photo mode shows the world alone (no text boxes, no map name). */
        else if (sTwoDOpacity > 0.0f && DioramaCamera_GetMode() != DIORAMA_CAMERA_PHOTO
              && !(pictureFlags & DIORAMA_PICTURE_HIDE_ALL))
        {
            EnsureFrameTexture();
            DrawTexture(&sFrameTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, sTwoDOpacity);
        }
        if (current3D && sSceneSnapshot.uiRectCount > 0
         && DioramaCamera_GetMode() != DIORAMA_CAMERA_PHOTO
         && !(pictureFlags & (DIORAMA_PICTURE_HIDE_ALL | DIORAMA_PICTURE_HIDE_UI)))
            DrawTexture(&sUiTexture, outputWidth, outputHeight,
                        gameX, gameY, gameWidth, gameHeight,
                        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, 1.0f);
    }
    else if (!(pictureFlags & DIORAMA_PICTURE_HIDE_ALL))
    {
        if (gameTexture == &sFrameTexture)
            EnsureFrameTexture();
        DrawTexture(gameTexture, outputWidth, outputHeight,
                    gameX, gameY, gameWidth, gameHeight,
                    0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, 1.0f);
    }

    sDrewField3D = drawTerrain && sSurveyView < 0;
    if (sSurveyView < 0 && !drawTerrain && border && sBorderTexture.id != 0
     && !(pictureFlags & DIORAMA_PICTURE_HIDE_ALL))
    {
        int innerWidth = gameWidth - 2;
        int innerHeight = gameHeight - 2;
        DrawTexture(&sBorderTexture, outputWidth, outputHeight,
                    gameX + 1 - innerWidth * 19 / 961,
                    gameY + 1 - innerHeight * 20 / 643,
                     innerWidth * 1000 / 961,
                     innerHeight * 683 / 643,
                     141, 18, 1000, 683, true, 1.0f);
    }
    if (sSurveyCaptureRequested)
    {
        const struct DioramaSceneSnapshot *captureSnapshot = sSurveyView == 0
                                                           ? &sSceneSnapshot
                                                           : &sRenderedSceneSnapshot;
        bool stateMatchesPixels = sSurveyView == 0
                               || (presentationDecision == DIORAMA_PRESENT_3D
                                && sHasRenderedSceneSnapshot
                                && sRenderedSceneSnapshot.sequence == sSceneSnapshot.sequence);

        CaptureSurveyFrame(outputWidth, outputHeight, captureSnapshot, stateMatchesPixels);
    }
    sLastFrame3D = drawTerrain;
    DioramaProfile_Begin(DIORAMA_PROF_2D); /* when no 3D was drawn: all of it is 2D */
    if (sOverlay != NULL && sSurveyView < 0)
    {
        glViewport(0, 0, outputWidth, outputHeight);
        sOverlay(outputWidth, outputHeight);
    }
    DioramaProfile_End(DIORAMA_PROF_2D);
    DioramaProfile_End(DIORAMA_PROF_FRAME);
    DioramaProfile_Begin(DIORAMA_PROF_SWAP);
    SDL_GL_SwapWindow(sWindow);
    DioramaProfile_End(DIORAMA_PROF_SWAP);
    DioramaProfile_FrameDone();
}

void DioramaGL_SetOverlay(void (*overlay)(int outputWidth, int outputHeight))
{
    sOverlay = overlay;
}

void DioramaGL_SetPictureFilter(int (*filter)(void))
{
    sPictureFilter = filter;
}

bool DioramaGL_LastFrameWas3D(void)
{
    return sLastFrame3D;
}

unsigned DioramaGL_GetFrameTexture(void)
{
    EnsureFrameTexture();
    return sFrameTexture.id;
}

void DioramaGL_DescribeRenderer(char *buffer, size_t size)
{
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);

    SDL_snprintf(buffer, size, "%s, %s (OpenGL %s)", renderer != NULL ? renderer : "?",
                 vendor != NULL ? vendor : "?", version != NULL ? version : "?");
}

SDL_Window *DioramaGL_GetWindow(void)
{
    return sWindow;
}

bool DioramaGL_GetIntegerScale(void)
{
    return sIntegerScale;
}

bool DioramaGL_DrewField3D(void)
{
    return sDrewField3D;
}

bool DioramaGL_IsAutoMode(void)
{
    return sRenderMode == DIORAMA_RENDER_AUTO && sSurveyView < 0;
}

static bool IsSnapshotOfMap(const struct DioramaSceneSnapshot *snapshot,
                            u8 mapGroup, u8 mapNum, u16 mapLayoutId)
{
    return snapshot->mapGroup == mapGroup && snapshot->mapNum == mapNum
        && snapshot->mapLayoutId == mapLayoutId;
}

bool DioramaGL_GetBattleWorld(u8 mapGroup, u8 mapNum, u16 mapLayoutId,
                              const struct DioramaSceneSnapshot **snapshot,
                              const struct DioramaResolvedCell **resolved)
{
    if (sRenderMode != DIORAMA_RENDER_AUTO || sSurveyView >= 0
     || !sTerrainAvailable || !sObjectsAvailable)
        return false;
    // The field as last seen with its palettes steady: as the 3D view drew it
    // if that was the frame, otherwise built now, behind the battle's first
    // black frames (the 3D view also draws fades and scripted scenes, so its
    // last frame may be part way through a fade).
    if (!sHasRenderedSceneSnapshot
     || !sHasLastFieldSnapshot
     || sRenderedSceneSnapshot.sequence != sLastFieldSnapshot.sequence
     || !IsSnapshotOfMap(&sRenderedSceneSnapshot, mapGroup, mapNum, mapLayoutId))
    {
        if (!sHasLastFieldSnapshot
         || !IsSnapshotOfMap(&sLastFieldSnapshot, mapGroup, mapNum, mapLayoutId)
         || !DioramaRules_IsMapSupported(mapGroup, mapNum, mapLayoutId))
            return false;
        EnsureAtlas(&sLastFieldSnapshot);
        if (!DioramaGLTerrain_Sync(&sLastFieldSnapshot, sAtlasResolvedCells)
         || !DioramaGLObjects_Sync(&sLastFieldSnapshot, sAtlasResolvedCells))
            return false;
        DioramaGLBuildings_Sync(&sLastFieldSnapshot, sAtlasResolvedCells);
        DioramaGLTreeSprites_Sync(&sLastFieldSnapshot, sAtlasResolvedCells);
        // The detail objects too: their art is composed with the palette of the
        // frame they were last synced on, and the field's last frames before a
        // battle are its fade (black or white objects otherwise).
        DioramaGLProps_Sync(&sLastFieldSnapshot, sAtlasResolvedCells);
        if (sHasRenderedSceneSnapshot)
        {
            sPreviousRenderedSceneSnapshot = sRenderedSceneSnapshot;
            sHasPreviousRenderedSceneSnapshot = true;
        }
        sRenderedSceneSnapshot = sLastFieldSnapshot;
        memcpy(sRenderedResolvedCells, sAtlasResolvedCells, sizeof(sRenderedResolvedCells));
        sHasRenderedSceneSnapshot = true;
        sCurrent3DReady = false;
    }
    *snapshot = &sRenderedSceneSnapshot;
    *resolved = sRenderedResolvedCells;
    return true;
}

void DioramaGL_GetFieldCamera(float *cameraX, float *cameraZ, float *pitch, float *focalLength,
                              float *eyeHeight, float *eyeDistance)
{
    struct DioramaCameraView view = sCameraView;

    if (!sCameraKnown || view.pitch <= 0.0f)
    {
        float x, z;

        GetCameraPosition(&sRenderedSceneSnapshot, &sPreviousRenderedSceneSnapshot,
                          false, 1.0f, &x, &z);
        /* The Adventure camera's framing (camera.h): 42 degrees down, a 33
         * degree lens, 11.5 tiles at the target. */
        DioramaCamera_Frame(0.733038f, 33.0f, 11.5f, 0.0f, &view);
        view.x = x;
        view.z = z;
    }
    *cameraX = view.x;
    *cameraZ = view.z;
    *pitch = view.pitch;
    *focalLength = view.focalLength;
    *eyeHeight = view.height;
    *eyeDistance = view.distance;
}

void DioramaGL_DrawBattleWorld(float cameraX, float cameraZ, float pitch, float focalLength,
                               float aspectCorrection, int outputWidth, int outputHeight)
{
    float sky[3] = {0.035f, 0.055f, 0.07f}; /* indoors and caves: the dark */
    bool outdoors;

    // The field's weather light (gl_light.h) first: the sky's colour follows
    // it. Then the sun's depth map round the stage (gl_shadow.h), then the lit
    // scene. A room's fog (the Lavaridge Gym's steam) is left out: the battle's
    // low camera sees the whole room through it, a white veil over every wall,
    // where the field's high one sees the floor at the player's feet; the GBA's
    // battles show no fog there (battle_capture.c BattleWorldWeather).
    if (sRenderedSceneSnapshot.mapType == MAP_TYPE_INDOOR
     && DioramaWeather_Classify(sRenderedSceneSnapshot.weather) == DIORAMA_WEATHER_FOG)
    {
        uint8_t weather = sRenderedSceneSnapshot.weather;

        sRenderedSceneSnapshot.weather = WEATHER_NONE;
        DioramaGLLight_Update(&sRenderedSceneSnapshot, true);
        sRenderedSceneSnapshot.weather = weather;
    }
    else
        DioramaGLLight_Update(&sRenderedSceneSnapshot, true);
    /* Outdoors the battle's world ends as the field's does (DioramaGL_Present):
     * its cells fade into the sky's haze before they end, and what is past them
     * - the horizon a battle's low camera looks out to - is that haze, through
     * the palette's fade and the time of day, not the GBA's battle background. */
    outdoors = SkyColour(&sRenderedSceneSnapshot, sky);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, outputWidth, outputHeight);
    glClearColor(sky[0], sky[1], sky[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, outputWidth, outputHeight);
    if (outdoors)
    {
        float fogEnd = DioramaGLTerrain_FarShown() ? DIORAMA_FAR_RADIUS - 1.0f
                                                   : DIORAMA_GRID_WIDTH / 2 - 1.0f;

        DioramaGLLight_SetEdgeFog(true,
                                  (float)(sRenderedSceneSnapshot.gridOriginX + DIORAMA_GRID_WIDTH / 2),
                                  -(float)(sRenderedSceneSnapshot.gridOriginY + DIORAMA_GRID_HEIGHT / 2),
                                  fogEnd - (DioramaGLTerrain_FarShown() ? 17.0f : 4.5f), fogEnd, sky);
    }
    // The street lamps and lit windows (gl_lamps.h) round the stage, as the
    // field lights them (this pass only).
    DioramaGLLamps_SetFocus(cameraX, cameraZ);
    DioramaGLLamps_SetActive(true);
    DioramaGLLamps_SetNight(DioramaGLLamps_NightFor(sRenderedSceneSnapshot.mapType,
                                                    sRenderedSceneSnapshot.timeOfDay));
    if (DioramaGLShadow_Begin(cameraX, cameraZ, SunStrength(&sRenderedSceneSnapshot),
                              ((uint64_t)DioramaGLBuildings_GetGeometryGeneration() << 32)
                              | (DioramaGLTerrain_GetGeometryGeneration()
                               ^ DioramaGLTreeSprites_GetGeometryGeneration() * 2654435761u
                               ^ DioramaGLProps_GetGeometryGeneration() * 40503u)))
    {
        DioramaGLTerrain_DrawShadow(sAtlasTexture.id, sBaseAtlasTexture.id,
                                    sForegroundAtlasTexture.id);
        DioramaGLBuildings_DrawShadow();
        DioramaGLTreeSprites_DrawShadow();
        DioramaGLProps_DrawShadow();
        DioramaGLShadow_End(outputWidth, outputHeight);
    }
    DioramaGLTerrain_Draw(sAtlasTexture.id, sBaseAtlasTexture.id, sForegroundAtlasTexture.id,
                          cameraX, cameraZ, pitch, focalLength, aspectCorrection, false);
    // The 3DS project's trees when the tree style asks for them (F8).
    DioramaGLTreeSprites_Draw(cameraX, cameraZ, pitch, focalLength, aspectCorrection);
    // The world's detail objects (props.h: grass, flowers, signs, fences, lamps,
    // rocks), the field's own last sync: the terrain leaves their cells' flat
    // picture out, so without this pass a battle would show bare ground there.
    DioramaGLProps_Draw(cameraX, cameraZ, pitch, focalLength, aspectCorrection);
    DioramaGLBuildings_Draw(cameraX, cameraZ, pitch, focalLength, aspectCorrection);
    // The light in the air over it (gl_air.h: sunlit dust, rays, bloom, the
    // fog's banks), as over the field: the battle's world in the same air.
    DioramaGLAir_Draw(cameraX, cameraZ, pitch, focalLength, aspectCorrection,
                      outputWidth, outputHeight);
    DioramaGLLight_SetEdgeFog(false, 0.0f, 0.0f, 0.0f, 0.0f, NULL);
    DioramaGLLamps_SetActive(false);
    glDisable(GL_SCISSOR_TEST);
}

bool DioramaGL_SetBattleTiles(const uint8_t *tileGraphics)
{
    static uint32_t sBattleTileGeneration;

    if (!sHasRenderedSceneSnapshot
     || memcmp(sRenderedSceneSnapshot.tileGraphics, tileGraphics, DIORAMA_TILE_GRAPHICS_SIZE) == 0)
        return false;
    memcpy(sRenderedSceneSnapshot.tileGraphics, tileGraphics, DIORAMA_TILE_GRAPHICS_SIZE);
    // A generation of its own (high bit set) that no field snapshot has, so
    // the field's next one is told apart and brings its own tiles back.
    sRenderedSceneSnapshot.tilesetAnimationGeneration = 0x80000000u | ++sBattleTileGeneration;
    return EnsureAtlas(&sRenderedSceneSnapshot);
}

bool DioramaGL_GetFieldScene(const struct DioramaSceneSnapshot **snapshot,
                             const struct DioramaResolvedCell **resolved)
{
    if (!sHasRenderedSceneSnapshot || !sDrewField3D)
        return false;
    *snapshot = &sRenderedSceneSnapshot;
    *resolved = sRenderedResolvedCells;
    return true;
}

void DioramaGL_SetVSync(bool enabled)
{
    if (SDL_GL_SetSwapInterval(enabled ? 1 : 0) < 0)
        SDL_Log("OpenGL VSync could not be changed: %s", SDL_GetError());
}

void DioramaGL_ToggleTerrainDebug(void)
{
    sTerrainDebug = !sTerrainDebug;
}

void DioramaGL_ToggleEnabled(void)
{
    if (sSurveyView >= 0)
    {
        DioramaGL_EndSurvey();
        return;
    }
    sRenderMode = sRenderMode == DIORAMA_RENDER_CLASSIC_2D
                ? DIORAMA_RENDER_AUTO
                : DIORAMA_RENDER_CLASSIC_2D;
}

void DioramaGL_CycleSurveyView(void)
{
    static const char *const sNames[] = {"flat", "v15", "v35", "v50", "v75"};

    if (sSurveyView < 0)
    {
        Uint32 flags = SDL_GetWindowFlags(sWindow);

        sSurveySavedRenderMode = sRenderMode;
        sSurveySavedTerrainDebug = sTerrainDebug;
        sSurveySavedFullscreen = (flags & (SDL_WINDOW_FULLSCREEN
                                         | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
        if (sSurveySavedFullscreen)
            SDL_SetWindowFullscreen(sWindow, 0);
        SDL_GetWindowSize(sWindow, &sSurveySavedWindowWidth, &sSurveySavedWindowHeight);
        SDL_GetWindowPosition(sWindow, &sSurveySavedWindowX, &sSurveySavedWindowY);
        sSurveyRun = sHasSceneSnapshot ? NextSurveyRun(&sSceneSnapshot) : 1;
    }
    sSurveyView = (sSurveyView + 1) % 5;
    sRenderMode = sSurveyView == 0 ? DIORAMA_RENDER_CLASSIC_2D : DIORAMA_RENDER_AUTO;
    sTerrainDebug = false;
    SDL_SetWindowFullscreen(sWindow, 0);
    SDL_SetWindowSize(sWindow, 960, 640);
    SDL_Log("Diorama survey view: %s", sNames[sSurveyView]);
}

void DioramaGL_EndSurvey(void)
{
    if (sSurveyView < 0)
        return;
    sSurveyView = -1;
    sSurveyCaptureRequested = false;
    sRenderMode = sSurveySavedRenderMode;
    sTerrainDebug = sSurveySavedTerrainDebug;
    SDL_SetWindowSize(sWindow, sSurveySavedWindowWidth, sSurveySavedWindowHeight);
    SDL_SetWindowPosition(sWindow, sSurveySavedWindowX, sSurveySavedWindowY);
    if (sSurveySavedFullscreen)
        SDL_SetWindowFullscreen(sWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void DioramaGL_RequestSurveyCapture(void)
{
    if (sSurveyView >= 0)
        sSurveyCaptureRequested = true;
}

void DioramaGL_AdjustCameraZoom(int steps)
{
    if (sSurveyView >= 0)
        return;
    DioramaCamera_Zoom(steps);
}

void DioramaGL_AdjustCameraPitch(int steps)
{
    if (sSurveyView >= 0)
        DioramaGL_EndSurvey();
    DioramaCamera_Tilt(steps);
}

void DioramaGL_SetCameraMode(int mode)
{
    DioramaCamera_SetMode((enum DioramaCameraMode)mode);
}

int DioramaGL_GetCameraMode(void)
{
    return (int)DioramaCamera_GetMode();
}

void DioramaGL_GetCameraSteps(int mode, int *zoomSteps, int *tiltSteps)
{
    DioramaCamera_GetSteps((enum DioramaCameraMode)mode, zoomSteps, tiltSteps);
}

void DioramaGL_SetCameraSteps(int mode, int zoomSteps, int tiltSteps)
{
    DioramaCamera_SetSteps((enum DioramaCameraMode)mode, zoomSteps, tiltSteps);
}

void DioramaGL_PhotoPan(int dx, int dz)
{
    DioramaCamera_PhotoPan(dx, dz);
}

void DioramaGL_Shutdown(void)
{
    DioramaPCOverlay_Shutdown();
    DioramaGLTarget_Shutdown();
    DioramaGLBattle_Shutdown();
    DioramaGLShadow_Shutdown();
    DioramaGLAir_Shutdown();
    DioramaGLFieldLight_Shutdown();
    DioramaGLTreeSprites_Shutdown();
    DioramaGLProps_Shutdown();
    DioramaGLLight_Shutdown();
    DioramaGLBuildings_Shutdown();
    DioramaGLObjects_Shutdown();
    DioramaGLTerrain_Shutdown();
    for (int i = 0; i < sBackgroundCount; i++)
        if (sBackgroundTextures[i].id != 0)
            glDeleteTextures(1, &sBackgroundTextures[i].id);
    if (sBorderTexture.id != 0)
        glDeleteTextures(1, &sBorderTexture.id);
    if (sFrameTexture.id != 0)
        glDeleteTextures(1, &sFrameTexture.id);
    if (sDebugTexture.id != 0)
        glDeleteTextures(1, &sDebugTexture.id);
    if (sUiTexture.id != 0)
        glDeleteTextures(1, &sUiTexture.id);
    sUiUploaded = false;
    sWeatherKeyValid = false;
    if (sWeatherTexture.id != 0)
        glDeleteTextures(1, &sWeatherTexture.id);
    if (sAtlasTexture.id != 0)
        glDeleteTextures(1, &sAtlasTexture.id);
    if (sBaseAtlasTexture.id != 0)
        glDeleteTextures(1, &sBaseAtlasTexture.id);
    if (sForegroundAtlasTexture.id != 0)
        glDeleteTextures(1, &sForegroundAtlasTexture.id);
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    if (sContext != NULL)
        SDL_GL_DeleteContext(sContext);
    sContext = NULL;
}

#endif
