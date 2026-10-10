#ifdef ENABLE_DIORAMA

/*
 * The 3D battle (include/diorama/battle_scene.h, gl_battle.h): a port of
 * ZallaxDev/pokeemerald-3Ds-dualscreen's 3D battle - RenderBattleWorld and its
 * intro effects in 3ds_port/src/3ds_video.c, the battle camera and its glide
 * in 3ds_port/src/voxel/ctr_voxel.c, the stage in voxel_battle.c (MIT;
 * Copyright (c) 2026 ZallaxDev and Pokemon Emerald 3Ds Dual Screen
 * contributors; see src/diorama/README.md) - to the Diorama renderer on one
 * PC screen.
 *
 * The world is the overworld the field last drew in 3D, unchanged: the
 * compositor keeps its scene, atlases and models while a battle is up, and
 * this file only looks at it from elsewhere. The GBA picture of the battle is
 * the game's own, its scenery layer see-through (gba_easy_draw.c), composed
 * over the world with premultiplied alpha. On a window wider than 3:2 the
 * picture sits on the bottom edge and its background is carried out to the
 * sides from its edge columns (sprites left out), so the scenery, a curtain
 * or a fade reaches the whole window.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/battle_scene.h"
#include "diorama/battle_stage.h"
#include "diorama/buildings.h"
#include "diorama/gl_battle.h"
#include "diorama/gl_building_renderer.h"
#include "diorama/gl_compositor.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_target.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/gl_tree_sprites.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"
#include "diorama/terrain_mesh.h"
#include "diorama/tree_model.generated.h"
#include "diorama/tree_sprites.h"
#include "gba/defines.h"
#include "platform/display_settings.h"
#include "constants/map_types.h"

#define DEG (3.14159265358979323846f / 180.0f)
#define GBA_W 240.0f
#define GBA_H 160.0f

/*
 * The battle camera (the 3DS project's VOXEL_BATTLE_PITCH 30, _DISTANCE 7 with
 * its field of view of 35 degrees, the battle picture at CTR_BATTLE_ZOOM 1.4
 * and its point (120, 112) on the screen's (200, 192)): lower and closer than
 * the field's, so that the ground the battlers stand on fills the scene, but
 * never so low that the view reaches the horizon. Its distance is horizontal
 * there (voxel_camera.c Place), the eye 7 / cos 30 tiles from the target.
 */
#define BATTLE_PITCH (30.0f * DEG)
#define BATTLE_DISTANCE 8.0829f
#define BATTLE_FOCAL 271.8f
#define BATTLE_TARGET_ROW 60.57f

/* The glide in from the field (ctr_voxel.c BattleCamera). */
#define GLIDE_FRAMES 100u
#define GLIDE_TRAVEL 0.78f
#define GLIDE_CRANE (16.0f * DEG)
#define GLIDE_CRANE_MAX (75.0f * DEG)
#define GLIDE_VERTIGO 0.40f
#define GLIDE_PUNCH 0.16f
#define GLIDE_QUAKE 0.07f
#define BARS_IN 18.0f
/* An intro that never starts (a battle resumed without one): glide anyway. */
#define INTRO_WAIT 150u

/* The intro's effects (3ds_video.c), in the 3DS screen's 240 lines. */
#define INTRO_BAR (22.0f / 240.0f)
#define INTRO_BAR_EDGE (10.0f / 240.0f)
#define INTRO_STREAKS 12u

/* Contact shadows (3ds_video.c BattleWorldShadows). */
#define SHADOW_ALPHA 0.50f

/* The field's camera, as the compositor's terrain shader places it. */
#define FIELD_TARGET_VERTICAL DIORAMA_OVERWORLD_TARGET_VERTICAL
#define FIELD_CAMERA_HEIGHT DIORAMA_OVERWORLD_CAMERA_HEIGHT
#define FIELD_SCREEN_OFFSET DIORAMA_OVERWORLD_SCREEN_OFFSET

/* A view of the world in GBA picture terms: the target (Diorama world x, z,
 * ground height), the eye `distance` tiles away at `pitch`, `focal` GBA
 * pixels per tile at one tile, the target on picture row `row`. */
struct View
{
    float x, z, ground;
    float pitch, distance, focal, row;
};

struct IntroEffects
{
    float flight, speed, impact, bars, time;
};

struct Layout
{
    int outW, outH;
    float gameX, gameY, gameW, gameH, scale;
};

static struct
{
    bool ready;
    bool active;     /* this battle is drawn in 3D */
    bool refused;    /* this battle is drawn in 2D (no world for it) */
    bool suspended;  /* in 3D until the classic view was chosen (F4) */
    bool composedLastFrame, mosaicLogged; /* for the log */
    uint32_t battleFrames; /* game frames since the battle began, 3D or not */
    bool chosen;
    bool glided;
    uint32_t frames, glide;
    uint8_t sceneLayers; /* of the picture last drawn */
    struct DioramaBattleFrame frame;
    struct View field;   /* where the field's camera was */
    struct View stage;   /* the battle camera at rest */
    struct View view;    /* this frame's */
    struct IntroEffects intro;
    struct DioramaStageResult stageResult;
    uint8_t mapGroup, mapNum;
    uint16_t mapLayoutId;
    bool log;
    bool fieldCompose; /* this frame's picture is a transition over the 3D field */
    bool fieldOff;     /* PKMN_DIORAMA_TRANSITION_2D=1: transitions in 2D as before */
} sBattle;

static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLint sModeLocation;
static GLint sDepthLocation;
static GLint sMosaicLocation, sMosaicOriginLocation, sOutSizeLocation;
static GLuint sEdgeTexture;
static GLuint sWhiteTexture;
static uint32_t sEdgePixels[160 * 2];

/*
 * Where an indoor map ends - a room, a cave - the cells past its edge are its
 * border block, drawn as black ground. A curtain stands on that edge on the
 * sides the battle camera looks across (north, east and west), from under
 * the ground to well above the picture's top, showing the same scenery as
 * DrawBackdrop in the picture's own terms; the room's walls and models in
 * front of it hide it as they would anything. The camera is the terrain
 * shader's (gl_terrain_renderer.c), so the depths agree.
 */
#define CURTAIN_HEIGHT 12.0f
#define CURTAIN_MAX_QUADS 600
static GLuint sCurtainProgram, sCurtainVertexArray, sCurtainBuffer;
static GLint sCurtainCamera, sCurtainPitch, sCurtainFocal, sCurtainAspect;
static GLint sCurtainEyeHeight, sCurtainEyeDistance, sCurtainScreenOffset;
static GLint sCurtainOrigin, sCurtainScale, sCurtainOutSize;
static float sCurtainVertices[CURTAIN_MAX_QUADS * 6 * 3];
static unsigned sCurtainVertexCount;

/*
 * Where the world has nothing drawn - the black past a room, a cave or the
 * scene - the battle's own scenery shows, as the GBA would show it there
 * (gDioramaSceneArt): the picture's BG3 caught at rest in its own colours,
 * carried out to the window's edges, drawn just short of the far plane right
 * after the world (only where nothing was drawn) and kept with it; the
 * battle's fades then fade both. Before any is caught, the world's clear
 * colour.
 */
static GLuint sArtTexture;
static bool sArtValid;
static uint32_t sArtGeneration; /* the art changed: the kept world is stale */
static uint16_t sArtLast[160 * 240];
static uint32_t sArtPixels[160 * 240];
/* The 3D field under a battle transition (DioramaGLBattle_ComposeFieldBands). */
/*
 * A move's own background past the picture's edges (KNOWN_ISSUES V13): while
 * BG3 holds it, the world is not shown, and the GBA renderer draws how the
 * screen would go on if it were as large as the window (gDioramaSceneWide: the
 * background repeated within what the move filled, or mirrored at the
 * picture's edges where repeating would show a seam; its scroll, palette and
 * brightness as on screen; nothing stretched). That fills the window round the
 * picture; the text box's rows are still carried out from the picture's
 * edges, as with the world.
 */
static struct
{
    GLuint texture;
    int width, height;      /* the texture's: left + 240 + right, top + 160 */
    int left, right, top;   /* the margins it holds, GBA pixels */
    bool valid;             /* drawn this frame */
    bool moveBackground;    /* asked for a move's background (not BG3 off) */
    uint32_t pixels[DIORAMA_WIDE_MAX_W * DIORAMA_WIDE_MAX_H];
    uint32_t frames;        /* battle frames shown with a move's background, for the log */
} sWide;

static GLuint sFieldTexture;
static int sFieldWidth, sFieldHeight;

/*
 * The world as last drawn, kept while the battle camera stands still - all of
 * a battle but its glide and the scenery's shakes - so that a still camera
 * costs a copy instead of the whole world (the scene does not change under a
 * battle: the compositor leaves it alone until the battle is over).
 */
static struct
{
    GLuint texture;
    int width, height;
    bool valid;
    bool off; /* PKMN_DIORAMA_BATTLE_KEEP=0: the world drawn every frame */
    struct View view;
    struct Layout layout;
    uint32_t terrainGeneration, buildingGeneration, treeGeneration, artGeneration;
    uint32_t drawn, kept; /* frames drawn and frames reused, for the log */
} sWorldCache;

/*
 * The world's water and flowers: the field's tile animations
 * (DioramaBattle_AnimateFieldTiles) carried on in the battle's world, its
 * atlases updated (DioramaGL_SetBattleTiles) and the world redrawn when they
 * change - as often as the measured frame times allow. Every battle frame is
 * timed, start to start (sFrameTime). The glide redraws the world every frame,
 * so its frames are the first measure of what a redraw frame costs: within the
 * budget (a 60 Hz game frame, or the frames' own pace if that is slower, as on
 * a 50 Hz display) the tiles may make the world redraw at every change; up to
 * twice it, every 4 game frames; up to four times, every 16; beyond that the
 * water stands still, as on software graphics. After the glide each frame with
 * a redraw is measured again: when such frames run over by a quarter, redraws
 * are spaced twice as far apart (the changes in between gathered into one),
 * stopped past every TILE_STEP_MAX game frames; after TILE_GOOD_REDRAWS quick
 * ones, twice as close again, down to every change. PKMN_DIORAMA_BATTLE_TILES=0
 * keeps it still, =1 moves it at every change whatever the cost;
 * PKMN_DIORAMA_BATTLE_FRAME_BUDGET (ms) sets the budget (a test switch).
 */
#define TILE_FRAME_BUDGET_MS (1000.0f / 60.0f)
#define TILE_STEP_MAX 32u
#define TILE_GOOD_REDRAWS 8u

static struct
{
    int mode;              /* -1 by frame time, 0 off, 1 on */
    bool ready;            /* tiles taken from the battle's world */
    bool decided;          /* the glide measured, the first step chosen */
    unsigned step;         /* game frames between redraws: 1 at every change, 0 none */
    unsigned stepMin, stepMax; /* the range it took this battle, for the log */
    unsigned goodRedraws;  /* redraw frames in a row within the budget */
    uint32_t frames;       /* game frames animated */
    uint32_t sinceRedraw;  /* game frames since the tiles last made the world redraw */
    uint32_t updates;      /* redraws they made, for the log */
    uint8_t tiles[DIORAMA_TILE_GRAPHICS_SIZE];
} sTileAnim;

/* A battle frame's time, from its start to the next frame's (present to
 * present: the CPU's work, the GPU's, the wait for the display), by kind. */
struct FrameTimes
{
    uint32_t count;
    double sum;
    float max;
};

static struct
{
    float budget;          /* ms (TILE_FRAME_BUDGET_MS or the test switch) */
    Uint64 last;           /* the last battle frame's start */
    bool lastDrew;         /* it drew the world (not the kept copy) */
    bool lastGlide;        /* during the glide */
    bool drew;             /* this frame drew the world */
    float keptPace;        /* recent frames that kept the world, ms (moving average) */
    float redrawPace;      /* recent frames that redrew it after the glide, ms */
    struct FrameTimes kept, redraw, glide;
} sFrameTime;

/* The stage survey, a test tool (PKMN_DIORAMA_STAGE_SURVEY=1, SurveyStages). */
static struct
{
    bool on;
    bool done;
    uint32_t mapGeneration;
} sSurvey;

/* ── 2D drawing: premultiplied quads ───────────────────────────────────── */

struct QuadVertex
{
    float x, y;       /* window pixels, from the top left */
    float u, v;
    float r, g, b, a; /* premultiplied */
};

static const char sVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in vec4 tint;\n"
    "uniform vec2 viewport;\n"
    "uniform float depth;\n"
    "out vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = vec4(position.x * 2.0 / viewport.x - 1.0, 1.0 - position.y * 2.0 / viewport.y, depth, 1.0);\n"
    "  uv = texCoord;\n"
    "  color = tint;\n"
    "}\n";

/* mode 0: texture times tint; 1: a soft disc (the contact shadow), times tint;
 * 2: the texture's colour, opaque; 5: as 2, in the GBA's mosaic blocks
 * (`mosaic` window pixels a block, from `mosaicOrigin`; the texture spans the
 * window, its rows bottom up). */
static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in vec4 color;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D image;\n"
    "uniform int mode;\n"
    "uniform vec2 mosaic;\n"
    "uniform vec2 mosaicOrigin;\n"
    "uniform vec2 outSize;\n"
    "void main() {\n"
    "  if (mode == 1) {\n"
    "    vec2 c = uv * 2.0 - 1.0;\n"
    "    float d = 1.0 - dot(c, c);\n"
    "    float a = d > 0.0 ? d * sqrt(d) : 0.0;\n"
    "    outColor = color * a;\n"
    "    return;\n"
    "  }\n"
    "  if (mode == 2) {\n"
    "    outColor = vec4(texture(image, uv).rgb, 1.0);\n"
    "    return;\n"
    "  }\n"
    "  if (mode == 5) {\n"
    "    vec2 p = vec2(gl_FragCoord.x, outSize.y - gl_FragCoord.y);\n"
    "    vec2 q = mosaicOrigin + floor((p - mosaicOrigin) / mosaic) * mosaic + 0.5;\n"
    "    vec2 at = uv + vec2(q.x - p.x, p.y - q.y) / outSize;\n"
    "    outColor = vec4(texture(image, at).rgb, 1.0);\n"
    "    return;\n"
    "  }\n"
    "  outColor = texture(image, uv) * color;\n"
    "}\n";

static GLint sViewportLocation;

static GLuint Compile(GLenum type, const char *source)
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
        SDL_Log("Diorama battle shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void MakeTexture(GLuint *texture, int width, int height, const void *pixels)
{
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
}

static float EnvFloat(const char *name, float fallback)
{
    const char *value = SDL_getenv(name);

    return value != NULL && value[0] != '\0' ? (float)atof(value) : fallback;
}

/* ── The world's water and flowers ──────────────────────────────────── */

/* Around the battle's first redraws of the world: their time to completion. */
static void AddFrameTime(struct FrameTimes *times, float ms)
{
    ++times->count;
    times->sum += ms;
    if (ms > times->max)
        times->max = ms;
}

static float AverageFrameTime(const struct FrameTimes *times)
{
    return times->count != 0 ? (float)(times->sum / times->count) : 0.0f;
}

/* What a frame may take: the budget, or the frames' own pace if that is slower
 * (a display refreshing below 60 Hz paces every frame, redraw or not). */
static float FrameAllowance(void)
{
    return sFrameTime.keptPace > sFrameTime.budget ? sFrameTime.keptPace : sFrameTime.budget;
}

static void SetTileStep(unsigned step)
{
    sTileAnim.step = step;
    if (step != 0 && (sTileAnim.stepMin == 0 || step < sTileAnim.stepMin))
        sTileAnim.stepMin = step;
    if (step > sTileAnim.stepMax)
        sTileAnim.stepMax = step;
}

static const char *TileStepWords(unsigned step, char *buffer, size_t size)
{
    if (step == 0)
        return "stand still (frames too slow to redraw the world)";
    if (step == 1)
        return "move at every change";
    SDL_snprintf(buffer, size, "move every %u game frames", step);
    return buffer;
}

/* The glide is over: its frames, each a redraw of the world, choose the first
 * step. */
static void DecideTileStep(void)
{
    float glide = AverageFrameTime(&sFrameTime.glide);
    float allowance = FrameAllowance();
    char words[48];

    sTileAnim.decided = true;
    if (sTileAnim.mode == 1)
        SetTileStep(1);
    else if (sTileAnim.mode == 0)
        SetTileStep(0);
    else if (sFrameTime.glide.count == 0)
        SetTileStep(16);   /* no glide (3D chosen mid-battle): try now and then */
    else if (glide <= allowance * 1.1f)
        SetTileStep(1);
    else if (glide <= allowance * 2.0f)
        SetTileStep(4);
    else if (glide <= allowance * 4.0f)
        SetTileStep(16);
    else
        SetTileStep(0);
    if (sBattle.log)
        SDL_Log("Diorama battle: the glide's frames took %.2f ms on average (%u frames, at most "
                "%.2f; the budget %.2f ms): water and flowers %s%s", glide, sFrameTime.glide.count,
                sFrameTime.glide.max, allowance, TileStepWords(sTileAnim.step, words, sizeof(words)),
                sTileAnim.mode == 1 ? " (PKMN_DIORAMA_BATTLE_TILES=1)"
                : sTileAnim.mode == 0 ? " (PKMN_DIORAMA_BATTLE_TILES=0)" : "");
}

/* A frame that redrew the world for the tiles took `ms`: further apart when
 * such frames run over, closer when they keep within. */
static void AdjustTileStep(float ms)
{
    float allowance;
    char words[48];
    unsigned before = sTileAnim.step;

    sFrameTime.redrawPace = sFrameTime.redrawPace > 0.0f ? sFrameTime.redrawPace * 0.7f + ms * 0.3f : ms;
    if (sTileAnim.mode != -1 || sTileAnim.step == 0)
        return;
    allowance = FrameAllowance();
    if (sFrameTime.redrawPace > allowance * 1.25f)
    {
        SetTileStep(sTileAnim.step * 2 > TILE_STEP_MAX ? 0 : sTileAnim.step * 2);
        sTileAnim.goodRedraws = 0;
    }
    else if (sFrameTime.redrawPace <= allowance * 1.05f)
    {
        if (++sTileAnim.goodRedraws >= TILE_GOOD_REDRAWS && sTileAnim.step > 1)
        {
            SetTileStep(sTileAnim.step / 2);
            sTileAnim.goodRedraws = 0;
        }
    }
    else
        sTileAnim.goodRedraws = 0;
    if (sBattle.log && sTileAnim.step != before)
        SDL_Log("Diorama battle: frames redrawing the world take %.2f ms (the budget %.2f): "
                "water and flowers %s", sFrameTime.redrawPace, allowance,
                TileStepWords(sTileAnim.step, words, sizeof(words)));
}

/* At the start of each battle frame: the last one's time, by kind. */
static void MeasureFrame(void)
{
    Uint64 now = SDL_GetPerformanceCounter();

    if (sFrameTime.last != 0)
    {
        float ms = (float)((double)(now - sFrameTime.last) * 1000.0 / (double)SDL_GetPerformanceFrequency());

        /* Not a pause (the window dragged, the game stopped). */
        if (ms < 1000.0f)
        {
            if (sFrameTime.lastGlide)
                AddFrameTime(&sFrameTime.glide, ms);
            else if (sFrameTime.lastDrew)
            {
                AddFrameTime(&sFrameTime.redraw, ms);
                if (sTileAnim.decided)
                    AdjustTileStep(ms);
            }
            else
            {
                AddFrameTime(&sFrameTime.kept, ms);
                sFrameTime.keptPace = sFrameTime.keptPace > 0.0f ? sFrameTime.keptPace * 0.9f + ms * 0.1f : ms;
            }
        }
    }
    sFrameTime.last = now;
    sFrameTime.drew = false;
}

/* The camera is flying from the field to the stage (it redraws the world
 * every frame). */
static bool Gliding(void)
{
    return sBattle.chosen && sBattle.glided && sBattle.glide > 0 && sBattle.glide < GLIDE_FRAMES;
}

/* At the end of each battle frame. */
static void EndFrameMeasure(void)
{
    sFrameTime.lastDrew = sFrameTime.drew;
    sFrameTime.lastGlide = Gliding();
}

static void ResetFrameTimes(void)
{
    float budget = sFrameTime.budget;

    memset(&sFrameTime, 0, sizeof(sFrameTime));
    sFrameTime.budget = budget;
}

/* Once a game frame while the battle shows its world. */
static void AnimateTiles(void)
{
    if (!sTileAnim.ready)
        return;
    if (!sTileAnim.decided && sBattle.glided && sBattle.glide >= GLIDE_FRAMES)
        DecideTileStep();
    if (sTileAnim.mode == 0)
        return;
    DioramaBattle_AnimateFieldTiles(sTileAnim.tiles, sizeof(sTileAnim.tiles), ++sTileAnim.frames);
    ++sTileAnim.sinceRedraw;
    if (!sTileAnim.decided)
    {
        /* The glide redraws the world every frame anyway; before it, the
         * world waits as it is. */
        if (Gliding() && DioramaGL_SetBattleTiles(sTileAnim.tiles))
            sWorldCache.valid = false;
        return;
    }
    if (sTileAnim.step == 0 || sTileAnim.sinceRedraw < sTileAnim.step)
        return;
    if (DioramaGL_SetBattleTiles(sTileAnim.tiles))
    {
        sWorldCache.valid = false;
        sTileAnim.sinceRedraw = 0;
        ++sTileAnim.updates;
    }
}

/* ── Where an indoor map ends ───────────────────────────────────────── */

static const char sCurtainVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "uniform float cameraHeight;\n"
    "uniform float cameraDistance;\n"
    "uniform float screenOffsetY;\n"
    "void main() {\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = 80.0;\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float relativeX = position.x - cameraPosition.x;\n"
    "  float relativeZ = position.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - position.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - position.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0 * aspectCorrection;\n"
    "  float clipY = screenOffsetY * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "}\n";

static const char sCurtainFragmentShader[] =
    "#version 330 core\n"
    "out vec4 outColor;\n"
    "uniform sampler2D image;\n"
    "uniform vec2 origin;\n"
    "uniform float scale;\n"
    "uniform vec2 outSize;\n"
    "void main() {\n"
    "  vec2 p = vec2(gl_FragCoord.x, outSize.y - gl_FragCoord.y);\n"
    "  outColor = vec4(texture(image, (p - origin) / scale / vec2(240.0, 160.0)).rgb, 1.0);\n"
    "}\n";

static GLuint Compile(GLenum type, const char *source);

static bool InitCurtain(void)
{
    GLuint vertexShader = Compile(GL_VERTEX_SHADER, sCurtainVertexShader);
    GLuint fragmentShader = Compile(GL_FRAGMENT_SHADER, sCurtainFragmentShader);
    GLint linked = 0;

    if (vertexShader == 0 || fragmentShader == 0)
        return false;
    sCurtainProgram = dglCreateProgram();
    dglAttachShader(sCurtainProgram, vertexShader);
    dglAttachShader(sCurtainProgram, fragmentShader);
    dglLinkProgram(sCurtainProgram);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(sCurtainProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        dglDeleteProgram(sCurtainProgram);
        sCurtainProgram = 0;
        return false;
    }
    sCurtainCamera = dglGetUniformLocation(sCurtainProgram, "cameraPosition");
    sCurtainPitch = dglGetUniformLocation(sCurtainProgram, "cameraPitch");
    sCurtainFocal = dglGetUniformLocation(sCurtainProgram, "focalLength");
    sCurtainAspect = dglGetUniformLocation(sCurtainProgram, "aspectCorrection");
    sCurtainEyeHeight = dglGetUniformLocation(sCurtainProgram, "cameraHeight");
    sCurtainEyeDistance = dglGetUniformLocation(sCurtainProgram, "cameraDistance");
    sCurtainScreenOffset = dglGetUniformLocation(sCurtainProgram, "screenOffsetY");
    sCurtainOrigin = dglGetUniformLocation(sCurtainProgram, "origin");
    sCurtainScale = dglGetUniformLocation(sCurtainProgram, "scale");
    sCurtainOutSize = dglGetUniformLocation(sCurtainProgram, "outSize");
    dglGenVertexArrays(1, &sCurtainVertexArray);
    dglBindVertexArray(sCurtainVertexArray);
    dglGenBuffers(1, &sCurtainBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sCurtainBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
    return true;
}

/* A quad from (x0, z0) to (x1, z1) on the ground plane, standing from y0 up
 * to y1 (world: x east, y up, z north). */
static void CurtainQuad(float x0, float z0, float x1, float z1, float y0, float y1)
{
    const float corners[6][3] = {
        {x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1},
        {x0, y0, z0}, {x1, y1, z1}, {x0, y1, z0},
    };

    if (sCurtainVertexCount + 6 > CURTAIN_MAX_QUADS * 6)
        return;
    memcpy(&sCurtainVertices[sCurtainVertexCount * 3], corners, sizeof(corners));
    sCurtainVertexCount += 6;
}

static bool IsIndoor(uint8_t mapType);

/* The edges of an indoor map in the battle's world: an in-map cell whose
 * neighbour to the north, east or west is the border block. A cell is centred
 * on its map coordinates, z = -y (gl_tree_sprites.c). */
static void BuildCurtain(const struct DioramaSceneSnapshot *snapshot, const struct DioramaResolvedCell *resolved)
{
    static const int sides[3][2] = {{0, -1}, {1, 0}, {-1, 0}};
    int gx, gy, side;

    sCurtainVertexCount = 0;
    if (sCurtainProgram == 0 || !IsIndoor(snapshot->mapType)
     || snapshot->visibleCellCount < DIORAMA_GRID_WIDTH * DIORAMA_GRID_HEIGHT)
        return;
    for (gy = 0; gy < DIORAMA_GRID_HEIGHT; ++gy)
        for (gx = 0; gx < DIORAMA_GRID_WIDTH; ++gx)
        {
            const struct DioramaCellSnapshot *cell = &snapshot->cells[gy * DIORAMA_GRID_WIDTH + gx];
            float ground = resolved[gy * DIORAMA_GRID_WIDTH + gx].groundHeight;
            float x = (float)cell->mapX, y = (float)cell->mapY;

            if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID))
                continue;
            for (side = 0; side < 3; ++side)
            {
                int nx = gx + sides[side][0], ny = gy + sides[side][1];
                const struct DioramaCellSnapshot *next;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                next = &snapshot->cells[ny * DIORAMA_GRID_WIDTH + nx];
                if (!(next->flags & DIORAMA_CELL_BORDER) || (next->flags & DIORAMA_CELL_SOURCE_VALID))
                    continue;
                if (side == 0)
                    CurtainQuad(x - 0.5f, -(y - 0.5f), x + 0.5f, -(y - 0.5f), ground - 1.0f, ground + CURTAIN_HEIGHT);
                else
                {
                    float edge = x + 0.5f * (float)sides[side][0];

                    CurtainQuad(edge, -(y + 0.5f), edge, -(y - 0.5f), ground - 1.0f, ground + CURTAIN_HEIGHT);
                }
            }
        }
    if (sCurtainVertexCount == 0)
        return;
    dglBindVertexArray(sCurtainVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sCurtainBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sCurtainVertexCount * 3 * sizeof(float), sCurtainVertices, GL_STATIC_DRAW);
}

/* Right after the world, with its camera still set (gDioramaCameraRig). */
static void DrawCurtain(const struct Layout *layout, float focal, float aspect)
{
    float eyeHeight, eyeDistance, screenOffsetY;

    if (!sArtValid || sCurtainVertexCount == 0)
        return;
    DioramaCamera_GetRig(sBattle.view.pitch, &eyeHeight, &eyeDistance, &screenOffsetY);
    dglUseProgram(sCurtainProgram);
    dglUniform2f(sCurtainCamera, sBattle.view.x, sBattle.view.z);
    dglUniform1f(sCurtainPitch, sBattle.view.pitch);
    dglUniform1f(sCurtainFocal, focal);
    dglUniform1f(sCurtainAspect, aspect);
    dglUniform1f(sCurtainEyeHeight, eyeHeight);
    dglUniform1f(sCurtainEyeDistance, eyeDistance);
    dglUniform1f(sCurtainScreenOffset, screenOffsetY);
    dglUniform2f(sCurtainOrigin, layout->gameX, layout->gameY);
    dglUniform1f(sCurtainScale, layout->scale);
    dglUniform2f(sCurtainOutSize, (float)layout->outW, (float)layout->outH);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sArtTexture);
    dglBindVertexArray(sCurtainVertexArray);
    glViewport(0, 0, layout->outW, layout->outH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sCurtainVertexCount);
    glDisable(GL_DEPTH_TEST);
}

bool DioramaGLBattle_Init(void)
{
    static const uint32_t white = 0xFFFFFFFFu;
    GLuint vertexShader, fragmentShader;
    GLint linked;
    const char *setting = SDL_getenv("PKMN_DIORAMA_BATTLE");

    memset(&sBattle, 0, sizeof(sBattle));
    sBattle.log = SDL_getenv("PKMN_DIORAMA_LOG") != NULL;
    /* PKMN_DIORAMA_BATTLE=0 keeps every battle 2D, as before the 3D battle. */
    if (setting != NULL && setting[0] == '0')
    {
        SDL_Log("Diorama 3D battles off (PKMN_DIORAMA_BATTLE=0)");
        return true;
    }
    vertexShader = Compile(GL_VERTEX_SHADER, sVertexShader);
    fragmentShader = Compile(GL_FRAGMENT_SHADER, sFragmentShader);
    if (vertexShader == 0 || fragmentShader == 0)
        return false;
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vertexShader);
    dglAttachShader(sProgram, fragmentShader);
    dglLinkProgram(sProgram);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        SDL_Log("Diorama battle shader link failed");
        return false;
    }
    sModeLocation = dglGetUniformLocation(sProgram, "mode");
    sDepthLocation = dglGetUniformLocation(sProgram, "depth");
    sMosaicLocation = dglGetUniformLocation(sProgram, "mosaic");
    sMosaicOriginLocation = dglGetUniformLocation(sProgram, "mosaicOrigin");
    sOutSizeLocation = dglGetUniformLocation(sProgram, "outSize");
    sViewportLocation = dglGetUniformLocation(sProgram, "viewport");
    dglGenVertexArrays(1, &sVertexArray);
    dglBindVertexArray(sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct QuadVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct QuadVertex),
                           (void *)offsetof(struct QuadVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(struct QuadVertex),
                           (void *)offsetof(struct QuadVertex, r));
    MakeTexture(&sEdgeTexture, 2, 160, NULL);
    MakeTexture(&sArtTexture, 240, 160, NULL);
    if (!InitCurtain())
        SDL_Log("Diorama battle: no curtain where indoor maps end (its shader failed)");
    MakeTexture(&sWhiteTexture, 1, 1, &white);
    setting = SDL_getenv("PKMN_DIORAMA_BATTLE_KEEP");
    sWorldCache.off = setting != NULL && setting[0] == '0';
    setting = SDL_getenv("PKMN_DIORAMA_TRANSITION_2D");
    sBattle.fieldOff = setting != NULL && setting[0] == '1';
    setting = SDL_getenv("PKMN_DIORAMA_STAGE_SURVEY");
    sSurvey.on = setting != NULL && setting[0] == '1';
    setting = SDL_getenv("PKMN_DIORAMA_BATTLE_TILES");
    sTileAnim.mode = setting == NULL ? -1 : setting[0] == '0' ? 0 : 1;
    sFrameTime.budget = EnvFloat("PKMN_DIORAMA_BATTLE_FRAME_BUDGET", TILE_FRAME_BUDGET_MS);
    if (sFrameTime.budget <= 0.0f)
        sFrameTime.budget = TILE_FRAME_BUDGET_MS;
    sBattle.ready = true;
    return true;
}

void DioramaGLBattle_Shutdown(void)
{
    ResetFrameTimes();
    if (sFieldTexture != 0) glDeleteTextures(1, &sFieldTexture);
    sFieldTexture = 0;
    sFieldWidth = sFieldHeight = 0;
    if (sWorldCache.texture != 0) glDeleteTextures(1, &sWorldCache.texture);
    memset(&sWorldCache, 0, sizeof(sWorldCache));
    if (sEdgeTexture != 0) glDeleteTextures(1, &sEdgeTexture);
    if (sArtTexture != 0) glDeleteTextures(1, &sArtTexture);
    sArtTexture = 0;
    sArtValid = false;
    if (sWide.texture != 0) glDeleteTextures(1, &sWide.texture);
    sWide.texture = 0;
    sWide.width = sWide.height = 0;
    sWide.valid = false;
    if (sCurtainBuffer != 0) dglDeleteBuffers(1, &sCurtainBuffer);
    if (sCurtainVertexArray != 0) dglDeleteVertexArrays(1, &sCurtainVertexArray);
    if (sCurtainProgram != 0) dglDeleteProgram(sCurtainProgram);
    sCurtainBuffer = sCurtainVertexArray = sCurtainProgram = 0;
    sCurtainVertexCount = 0;
    if (sWhiteTexture != 0) glDeleteTextures(1, &sWhiteTexture);
    if (sVertexBuffer != 0) dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0) dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0) dglDeleteProgram(sProgram);
    sEdgeTexture = sWhiteTexture = sVertexBuffer = sVertexArray = sProgram = 0;
    sBattle.ready = false;
}

static struct QuadVertex *sQuads;
static unsigned sQuadCount, sQuadCapacity;

static void QuadBegin(void)
{
    sQuadCount = 0;
}

static void PushVertex(float x, float y, float u, float v, const float c[4])
{
    struct QuadVertex *q;

    if (sQuadCount == sQuadCapacity)
    {
        unsigned capacity = sQuadCapacity ? sQuadCapacity * 2 : 96;
        struct QuadVertex *grown = realloc(sQuads, capacity * sizeof(*sQuads));

        if (grown == NULL)
            return;
        sQuads = grown;
        sQuadCapacity = capacity;
    }
    q = &sQuads[sQuadCount++];
    q->x = x; q->y = y; q->u = u; q->v = v;
    q->r = c[0]; q->g = c[1]; q->b = c[2]; q->a = c[3];
}

/* A quad with a colour at each corner: top left, top right, bottom left,
 * bottom right (premultiplied). */
static void PushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                     const float tl[4], const float tr[4], const float bl[4], const float br[4])
{
    PushVertex(x0, y0, u0, v0, tl);
    PushVertex(x1, y0, u1, v0, tr);
    PushVertex(x1, y1, u1, v1, br);
    PushVertex(x0, y0, u0, v0, tl);
    PushVertex(x1, y1, u1, v1, br);
    PushVertex(x0, y1, u0, v1, bl);
}

static void PushRect(float x0, float y0, float x1, float y1, const float c[4])
{
    PushQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, c, c, c, c);
}

enum QuadBlend
{
    QUAD_OVER, /* premultiplied over what is there */
    QUAD_COPY, /* blending off */
    QUAD_FAR,  /* blending off, at the far plane: only where nothing is drawn */
};

static void QuadFlushAs(const struct Layout *layout, GLuint texture, int mode, enum QuadBlend blend)
{
    if (sQuadCount == 0)
        return;
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sQuadCount * sizeof(*sQuads), sQuads, GL_STREAM_DRAW);
    dglUniform2f(sViewportLocation, (float)layout->outW, (float)layout->outH);
    dglUniform1i(sModeLocation, mode);
    /* Just short of the far plane: in front of the cleared depth only. */
    dglUniform1f(sDepthLocation, blend == QUAD_FAR ? 0.9999f : 0.0f);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_CULL_FACE);
    switch (blend)
    {
    case QUAD_FAR:
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        break;
    case QUAD_COPY:
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        break;
    default:
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
    glDrawArrays(GL_TRIANGLES, 0, sQuadCount);
    if (blend == QUAD_FAR)
    {
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }
    sQuadCount = 0;
}

static void QuadFlush(const struct Layout *layout, GLuint texture, int mode)
{
    QuadFlushAs(layout, texture, mode, mode == 2 || mode == 5 ? QUAD_COPY : QUAD_OVER);
}

/* ── The battle's picture on the window ────────────────────────────────── */

/*
 * The GBA picture at the scale the compositor gives it (DioramaGL_Present),
 * on the bottom edge of the window: the text box at the bottom, the scene
 * carried out above and to both sides.
 */
static void BattleLayout(SDL_Window *window, struct Layout *layout)
{
    /* The picture at the field's whole-number scale (the compositor's rule,
     * display_settings.h), so pixel art stays sharp and the same size as on
     * the field; on the window's bottom edge, the world above and beside it. */
    int scale;

    SDL_GL_GetDrawableSize(window, &layout->outW, &layout->outH);
    scale = DisplaySettings_PixelScale(layout->outW, layout->outH, DioramaGL_GetIntegerScale());
    layout->gameW = 240.0f * scale;
    layout->gameH = 160.0f * scale;
    layout->scale = layout->gameH / GBA_H;
    layout->gameX = floorf((layout->outW - layout->gameW) * 0.5f);
    layout->gameY = (float)layout->outH - layout->gameH;
}

/* The picture's columns and rows that the window shows above its text box. */
static void VisibleRect(const struct Layout *layout, float *left, float *right, float *top, float *bottom)
{
    *left = -layout->gameX / layout->scale;
    *right = (layout->outW - layout->gameX) / layout->scale;
    *top = -layout->gameY / layout->scale;
    *bottom = 112.0f;
}

/* ── The views and the camera ──────────────────────────────────────────── */

/* The rig and shader values for a view (terrain_mesh.h DioramaCameraRig). */
static void ApplyView(const struct View *view, const struct Layout *layout,
                      float *focalLength, float *aspectCorrection)
{
    float k = layout->scale * GBA_H / layout->outH;

    *focalLength = view->focal * k;
    *aspectCorrection = (GBA_W * layout->outH) / (GBA_H * layout->outW);
    gDioramaCameraRig.active = true;
    gDioramaCameraRig.height = view->ground + view->distance * sinf(view->pitch);
    gDioramaCameraRig.distance = view->distance * cosf(view->pitch);
    gDioramaCameraRig.screenOffsetY = 1.0f - 2.0f * (layout->gameY + view->row * layout->scale) / layout->outH;
    gDioramaCameraRig.aspectCorrection = *aspectCorrection;
}

/*
 * The field's camera as a view: its eye 16 tiles up at `pitch`, the ground
 * point in the middle of its picture as the target (the compositor's terrain
 * shader, gl_terrain_renderer.c), its focal length converted from the
 * window's 160 rows to the battle picture's.
 */
static void FieldView(struct View *view, const struct Layout *layout, float cameraX, float cameraZ,
                      float pitch, float focalLength)
{
    float k = layout->scale * GBA_H / layout->outH;
    float windowRow = layout->outH * 0.5f * (1.0f - FIELD_SCREEN_OFFSET);

    view->x = cameraX;
    view->z = cameraZ - FIELD_TARGET_VERTICAL / sinf(pitch);
    view->ground = 0.0f;
    view->pitch = pitch;
    view->distance = FIELD_CAMERA_HEIGHT / sinf(pitch);
    view->focal = focalLength / k;
    view->row = (windowRow - layout->gameY) / layout->scale;
}

static float Clamp(float t, float lo, float hi)
{
    return t < lo ? lo : t > hi ? hi : t;
}

static float Smooth(float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Smootherstep: slower off the mark and into the stop than Smooth. */
static float Smoother(float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/*
 * The battle camera this game frame (ctr_voxel.c BattleCamera): on its way
 * from the field's to the stage until the glide is over - craning up as it
 * leaves the field, a dolly zoom flattening the world into a telephoto swoop
 * on the way, a push and a short quake on landing - then still, moved only by
 * the scenery's shake.
 */
static void StepCamera(void)
{
    const struct View *from = &sBattle.field, *to = &sBattle.stage;
    struct View *v = &sBattle.view;
    struct IntroEffects *intro = &sBattle.intro;
    float u, p, q, t, arc, punch, quake, fovFrom, fov, tilesPerPixel;

    if (sBattle.frame.introSliding || sBattle.frames >= INTRO_WAIT)
        sBattle.glided = true;
    ++sBattle.frames;
    if (sBattle.chosen && sBattle.glided && sBattle.glide < GLIDE_FRAMES)
        ++sBattle.glide;
    u = (float)sBattle.glide / (float)GLIDE_FRAMES;
    /* p: the flight, 0..1; q: the landing after it, 0..1. */
    p = Clamp(u / GLIDE_TRAVEL, 0.0f, 1.0f);
    q = Clamp((u - GLIDE_TRAVEL) / (1.0f - GLIDE_TRAVEL), 0.0f, 1.0f);
    t = Smoother(p);
    arc = p > 0.0f && p < 1.0f ? sinf(p * 3.14159265f) : 0.0f;
    v->x = Lerp(from->x, to->x, t);
    v->z = Lerp(from->z, to->z, t);
    v->ground = Lerp(from->ground, to->ground, t);
    v->pitch = Lerp(from->pitch, to->pitch, t);
    v->distance = Lerp(from->distance, to->distance, t);
    v->focal = Lerp(from->focal, to->focal, t);
    v->row = Lerp(from->row, to->row, t);
    /* The crane: up over the world mid-flight, short of looking straight down. */
    v->pitch += GLIDE_CRANE * arc;
    if (v->pitch > GLIDE_CRANE_MAX && v->pitch > from->pitch)
        v->pitch = from->pitch > GLIDE_CRANE_MAX ? from->pitch : GLIDE_CRANE_MAX;
    /* The dolly zoom: narrower and further back, the target framed the same. */
    fovFrom = 2.0f * atanf(80.0f / v->focal);
    fov = fovFrom * (1.0f - GLIDE_VERTIGO * arc * arc);
    if (arc > 0.0f)
    {
        float m = tanf(fovFrom * 0.5f) / tanf(fov * 0.5f);

        v->distance *= m;
        v->focal *= m;
    }
    /* The landing: a push in that springs back, and a quake dying away. */
    punch = q > 0.0f && q < 1.0f ? expf(-6.0f * q) * sinf(3.0f * 3.14159265f * q) : 0.0f;
    quake = q > 0.0f && q < 1.0f ? GLIDE_QUAKE * expf(-7.0f * q) : 0.0f;
    v->distance *= 1.0f - GLIDE_PUNCH * punch;
    v->x += quake * sinf((float)sBattle.glide * 2.7f);
    v->z += quake * cosf((float)sBattle.glide * 3.9f);

    /* What the effects over the world need of it. */
    intro->flight = arc;
    /* Smootherstep's slope, 30 p^2 (1 - p)^2, over its peak of 1.875. */
    intro->speed = p > 0.0f && p < 1.0f ? 16.0f * p * p * (1.0f - p) * (1.0f - p) : 0.0f;
    intro->impact = q > 0.0f && q < 1.0f ? expf(-5.0f * q) * (1.0f - q) : 0.0f;
    intro->bars = Smooth((float)sBattle.frames / BARS_IN) * (1.0f - Smooth((q - 0.35f) / 0.65f));
    intro->time = (float)sBattle.frames;

    /* BG3's scroll moves the scenery: a GBA pixel at the target is this many
     * tiles - across, and along the ground foreshortened by the pitch. */
    tilesPerPixel = v->distance / v->focal;
    v->x += sBattle.frame.sceneryShiftX * tilesPerPixel;
    v->z -= sBattle.frame.sceneryShiftY * tilesPerPixel / sinf(v->pitch);
}

/* ── Where the battle stands ───────────────────────────────────────────── */

struct StageContext
{
    const struct DioramaSceneSnapshot *snapshot;
    const struct DioramaResolvedCell *resolved;
    bool legacy; /* the cells as the stage search read them before (for comparisons) */
};

/*
 * The Diorama's trees (terrain_mesh.c AppendVoxelTreeInstance): a two-wide
 * grouped hull is drawn as the voxel tree model, about two tiles tall, on
 * its body's rows; a one-row hull (the trunk row, the crown flat map art
 * above it) on the trunk row and the row above. The resolved cells give
 * only the body's rows, one or two tiles high.
 */
#define VOXEL_TREE_HEIGHT ((DIORAMA_TREE_MODEL_MAX_Y - DIORAMA_TREE_MODEL_MIN_Y) \
                           / (float)DIORAMA_VOXELS_PER_CELL)

/*
 * The 3DS project's trees (F8; gl_tree_sprites.c LargeCrown, SmallCrown):
 * the tree's cells are published as ground with the crown, a cut-out
 * leaning back 50 degrees, standing on them. A large tree's crown tops out
 * 1.62 tiles up over its upper row and comes down to the ground across its
 * lower row; a small tree's tops out 1.43 up over its one cell.
 */
#define TREE_3DS_LARGE_TOP (-0.10f + 1.7236f)
#define TREE_3DS_LARGE_MIDDLE (-0.10f + 0.5f * 1.7236f)
#define TREE_3DS_SMALL_TOP (-0.10f + 1.532089f)
/* How far north of its cell the crown's top edge leans: the large crown's
 * upper quarters 1.446 back from 1.35, the small crown 1.286 back from 0.825
 * (Dewford's 0.975). */
#define TREE_3DS_LARGE_NORTH (1.446272f - 1.35f)
#define TREE_3DS_SMALL_NORTH (1.285575f - 0.825f)
#define TREE_3DS_DEWFORD_NORTH (1.285575f - 0.975f)

static float Tree3dsNorth(uint8_t treeSprite)
{
    switch (treeSprite)
    {
    case DIORAMA_TREE_SPRITE_QUARTER + 0:
    case DIORAMA_TREE_SPRITE_QUARTER + 1:
        return TREE_3DS_LARGE_NORTH;
    case DIORAMA_TREE_SPRITE_SMALL:
        return TREE_3DS_SMALL_NORTH;
    case DIORAMA_TREE_SPRITE_SMALL_DEWFORD:
        return TREE_3DS_DEWFORD_NORTH;
    default:
        return 0.0f;
    }
}

static float Tree3dsTop(uint8_t treeSprite)
{
    switch (treeSprite)
    {
    case DIORAMA_TREE_SPRITE_QUARTER + 0:
    case DIORAMA_TREE_SPRITE_QUARTER + 1:
        return TREE_3DS_LARGE_TOP;
    case DIORAMA_TREE_SPRITE_QUARTER + 2:
    case DIORAMA_TREE_SPRITE_QUARTER + 3:
        return TREE_3DS_LARGE_MIDDLE;
    case DIORAMA_TREE_SPRITE_SMALL:
    case DIORAMA_TREE_SPRITE_SMALL_DEWFORD:
        return TREE_3DS_SMALL_TOP;
    default:
        return 0.0f;
    }
}

static bool IsVoxelTree(const struct DioramaResolvedCell *resolved)
{
    return resolved->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL && resolved->structureWidth == 2;
}

static const struct DioramaResolvedCell *StageResolvedAt(const struct StageContext *c, int x, int y,
                                                         const struct DioramaCellSnapshot **cell)
{
    const struct DioramaSceneSnapshot *snapshot = c->snapshot;
    int gx = x - snapshot->gridOriginX, gy = y - snapshot->gridOriginY;
    const struct DioramaCellSnapshot *source;
    int index;

    if (gx < 0 || gy < 0 || gx >= DIORAMA_GRID_WIDTH || gy >= DIORAMA_GRID_HEIGHT)
        return NULL;
    index = gy * DIORAMA_GRID_WIDTH + gx;
    if (index >= snapshot->visibleCellCount)
        return NULL;
    source = &snapshot->cells[index];
    if (source->mapX != x || source->mapY != y || !(source->flags & DIORAMA_CELL_SOURCE_VALID))
        return NULL;
    if (cell != NULL)
        *cell = source;
    return &c->resolved[index];
}

static bool StageCellAt(void *context, int x, int y, struct DioramaStageCell *cell)
{
    const struct StageContext *c = context;
    const struct DioramaCellSnapshot *source;
    const struct DioramaResolvedCell *resolved = StageResolvedAt(c, x, y, &source);
    const struct DioramaResolvedCell *south;
    uint16_t ground;
    uint8_t quarters;
    float top = 0.0f, modelTop;

    if (resolved == NULL)
        return false;
    memset(cell, 0, sizeof(*cell));
    cell->kind = DioramaBattle_GroundKind(source->behavior);
    if (resolved->shape == DIORAMA_SHAPE_HIDDEN)
        cell->kind = DIORAMA_GROUND_VOID;
    cell->blocked = source->collision != 0;
    cell->lift = resolved->groundHeight;
    cell->slope = resolved->shape == DIORAMA_SHAPE_CLIFF || resolved->shape == DIORAMA_SHAPE_LEDGE
               || resolved->shape == DIORAMA_SHAPE_STAIRS || resolved->cliffEdgeMask != 0;
    /* What stands on it: a modelled building, a tree, anything the rules raise. */
    if (DioramaBuildings_CellAt(source->sourceLayoutId, source->sourceMapX, source->sourceMapY,
                                &ground, &quarters, &modelTop) && modelTop > top)
        top = modelTop;
    south = StageResolvedAt(c, x, y + 1, NULL);
    if ((IsVoxelTree(resolved)
      || (south != NULL && IsVoxelTree(south) && south->structureHeight == 1))
     && top < VOXEL_TREE_HEIGHT)
        top = VOXEL_TREE_HEIGHT;
    if (Tree3dsTop(source->treeSprite) > top)
    {
        top = Tree3dsTop(source->treeSprite);
        if (!c->legacy)
            cell->north = Tree3dsNorth(source->treeSprite);
    }
    switch (resolved->shape)
    {
    case DIORAMA_SHAPE_CLIFF:
        /* A cliff, a mound, a room's wall: a block from the ground up. */
        if (c->legacy)
            break;
        /* fall through */
    case DIORAMA_SHAPE_EXTRUDED:
    case DIORAMA_SHAPE_BUILDING_PART:
    case DIORAMA_SHAPE_ROOF:
    case DIORAMA_SHAPE_CUTOUT:
    case DIORAMA_SHAPE_BILLBOARD:
        if (resolved->topHeight - resolved->groundHeight > top)
            top = resolved->topHeight - resolved->groundHeight;
        break;
    default:
        break;
    }
    cell->top = top;
    return true;
}

static bool IsIndoor(uint8_t mapType)
{
    return mapType == MAP_TYPE_INDOOR || mapType == MAP_TYPE_SECRET_BASE
        || mapType == MAP_TYPE_UNDERGROUND || mapType == MAP_TYPE_UNDERWATER;
}

/* The stage search's request for the battle camera on this layout. */
static void StageRequest(struct DioramaStageRequest *request, const struct Layout *layout,
                         const struct DioramaSceneSnapshot *snapshot, struct StageContext *context)
{
    memset(request, 0, sizeof(*request));
    request->view.pitch = EnvFloat("PKMN_BATTLE3D_PITCH", BATTLE_PITCH / DEG) * DEG;
    request->view.distance = EnvFloat("PKMN_BATTLE3D_DISTANCE", BATTLE_DISTANCE);
    request->view.focal = EnvFloat("PKMN_BATTLE3D_FOCAL", BATTLE_FOCAL);
    request->view.targetRow = EnvFloat("PKMN_BATTLE3D_ROW", BATTLE_TARGET_ROW);
    VisibleRect(layout, &request->visibleLeft, &request->visibleRight, &request->visibleTop,
                &request->visibleBottom);
    request->indoor = IsIndoor(snapshot->mapType);
    request->cellAt = StageCellAt;
    request->context = context;
}

static void ChooseStage(const struct Layout *layout, const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolved)
{
    struct StageContext context = {snapshot, resolved, false};
    struct DioramaStageRequest request;
    struct DioramaStageResult *result = &sBattle.stageResult;
    Uint64 start = SDL_GetPerformanceCounter();

    StageRequest(&request, layout, snapshot, &context);
    /* The search works in the world's cells: the snapshot's map grid. */
    request.playerX = sBattle.frame.playerGridX;
    request.playerY = sBattle.frame.playerGridY;
    request.facing = sBattle.frame.playerFacing;
    request.surfing = sBattle.frame.surfing;
    request.wild = sBattle.frame.wild;
    request.doubles = sBattle.frame.doubles;
    DioramaStage_Choose(&request, result);

    /* The search's tiles span x..x+1 with y growing south; the Diorama's cell
     * centres are whole numbers with z growing north. */
    sBattle.stage.x = result->targetX - 0.5f;
    sBattle.stage.z = 0.5f - result->targetY;
    sBattle.stage.ground = result->ground;
    sBattle.stage.pitch = request.view.pitch;
    sBattle.stage.distance = request.view.distance;
    sBattle.stage.focal = request.view.focal;
    sBattle.stage.row = request.view.targetRow;
    sBattle.chosen = true;
    if (sBattle.log)
    {
        int offsetX = sBattle.frame.playerGridX - sBattle.frame.playerMapX;
        int offsetY = sBattle.frame.playerGridY - sBattle.frame.playerMapY;

        SDL_Log("Diorama battle stage: wants %u/%u, target %.1f,%.1f on the map (%+.0f,%+.0f from the player's "
                "spot) ground %.2f score %.1f%s, %.2f ms; seen: black %.0f%%, a battler covered %.0f%%, "
                "pressed %.2f",
                result->want[0], result->want[1], result->targetX - offsetX, result->targetY - offsetY,
                result->offsetX, result->offsetY, result->ground, result->score,
                request.indoor ? " indoor" : "",
                (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency(),
                100.0f * result->seen.voidFraction, 100.0f * result->seen.overlap, result->seen.cramped);
        /* The ground round the player as the search read it: . plain, g grass,
         * s sand, ~ water, space nothing, # something tall, ? not in the
         * scene, P the player. */
        for (int dy = -5; dy <= 5; ++dy)
        {
            char row[16];

            for (int dx = -7; dx <= 7; ++dx)
            {
                struct DioramaStageCell cell;
                char c = '?';

                if (dx == 0 && dy == 0)
                    c = 'P';
                else if (StageCellAt(&context, request.playerX + dx, request.playerY + dy, &cell))
                    c = cell.top > 0.4f ? '#' : ".gs~ "[cell.kind < 5 ? cell.kind : 4];
                row[dx + 7] = c;
            }
            row[15] = '\0';
            SDL_Log("Diorama battle stage:   %+d |%s|", dy, row);
        }
    }
}

/* ── The stage survey (a test tool: PKMN_DIORAMA_STAGE_SURVEY=1) ─────── */

/*
 * For each map the field shows in 3D: the stage search run from every other
 * walkable cell round the player, singles and doubles, as the game runs it
 * and as the 3DS project's scoring alone would (the cells read as before),
 * and what each stage chosen lets the battle camera see
 * (DioramaStage_Measure), summed up in the log. It changes nothing.
 */
#define SURVEY_MAP_OFFSET 7 /* fieldmap.h MAP_OFFSET: the snapshot's player is without it */
#define SURVEY_REACH 10

struct SurveyTally
{
    unsigned spots, covered, black, pressed;
    float blackSum, coverSum, worst;
    int worstX, worstY;
    bool worstDoubles;
};

static void Tally(struct SurveyTally *t, const struct DioramaStageView *v, int x, int y, bool doubles)
{
    float bad = v->overlap + v->voidFraction;

    ++t->spots;
    t->covered += v->overlap > 0.02f;
    t->black += v->voidFraction > 0.02f;
    t->pressed += v->cramped > 0.5f;
    t->blackSum += v->voidFraction;
    t->coverSum += v->overlap;
    if (bad > t->worst)
    {
        t->worst = bad;
        t->worstX = x;
        t->worstY = y;
        t->worstDoubles = doubles;
    }
}

/* With PKMN_DIORAMA_STAGE_DUMP=<folder> as well: the cells as the stage search
 * reads them, to <folder>/cells-<group>.<num>.txt, for trying the search
 * outside the game. */
static void DumpStageCells(const struct DioramaSceneSnapshot *snapshot, const struct StageContext *now,
                           const struct StageContext *before)
{
    const char *folder = SDL_getenv("PKMN_DIORAMA_STAGE_DUMP");
    char path[512];
    FILE *file;
    int x, y;

    if (folder == NULL || folder[0] == '\0')
        return;
    snprintf(path, sizeof(path), "%s/cells-%u.%u.txt", folder, snapshot->mapGroup, snapshot->mapNum);
    file = fopen(path, "w");
    if (file == NULL)
        return;
    fprintf(file, "map %u %u type %u indoor %d player %d %d origin %d %d size %d %d\n", snapshot->mapGroup,
            snapshot->mapNum, snapshot->mapType, IsIndoor(snapshot->mapType),
            snapshot->playerMapX + SURVEY_MAP_OFFSET, snapshot->playerMapY + SURVEY_MAP_OFFSET,
            snapshot->gridOriginX, snapshot->gridOriginY, DIORAMA_GRID_WIDTH, DIORAMA_GRID_HEIGHT);
    for (y = snapshot->gridOriginY; y < snapshot->gridOriginY + DIORAMA_GRID_HEIGHT; ++y)
        for (x = snapshot->gridOriginX; x < snapshot->gridOriginX + DIORAMA_GRID_WIDTH; ++x)
        {
            struct DioramaStageCell cell, old;
            const struct DioramaResolvedCell *r = StageResolvedAt(now, x, y, NULL);

            if (!StageCellAt((void *)now, x, y, &cell) || !StageCellAt((void *)before, x, y, &old))
                continue;
            fprintf(file, "%d %d %u %d %d %.3f %.3f %.3f %.3f  shape %u arch %u ground %.2f top %.2f feature %.2f "
                    "body %.2f roof %.2f facade %u x %.2f pixobj %u struct %u %ux%u\n", x, y, cell.kind,
                    cell.blocked, cell.slope, cell.top, old.top, cell.lift, cell.north, r->shape, r->archetype,
                    r->groundHeight,
                    r->topHeight, r->featureHeight, r->structureBodyHeight, r->structureRoofHeight,
                    r->structureSouthFacadeRows, r->structureSouthFacadeUnitHeight, r->pixelObjectId, r->structureId,
                    r->structureWidth, r->structureHeight);
        }
    fclose(file);
}

static void SurveyStages(const struct DioramaSceneSnapshot *snapshot, const struct DioramaResolvedCell *resolved)
{
    struct StageContext now = {snapshot, resolved, false}, before = {snapshot, resolved, true};
    struct SurveyTally tally[2];
    struct Layout layout;
    SDL_Window *window = DioramaGL_GetWindow();
    Uint64 start = SDL_GetPerformanceCounter();
    int px = snapshot->playerMapX + SURVEY_MAP_OFFSET, py = snapshot->playerMapY + SURVEY_MAP_OFFSET;
    int dx, dy, d;

    if (window == NULL)
        return;
    DumpStageCells(snapshot, &now, &before);
    BattleLayout(window, &layout);
    memset(tally, 0, sizeof(tally));
    for (dy = -SURVEY_REACH; dy <= SURVEY_REACH; dy += 2)
        for (dx = -SURVEY_REACH; dx <= SURVEY_REACH; dx += 2)
        {
            struct DioramaStageCell cell;
            int x = px + dx, y = py + dy;

            if (!StageCellAt(&now, x, y, &cell) || cell.blocked || cell.kind == DIORAMA_GROUND_VOID
             || cell.top > 0.4f)
                continue;
            for (d = 0; d < 2; ++d)
            {
                struct DioramaStageRequest request;
                struct DioramaStageResult result;
                struct DioramaStageView seen;

                StageRequest(&request, &layout, snapshot, &now);
                request.playerX = x;
                request.playerY = y;
                request.facing = 2; /* north */
                request.wild = true;
                request.doubles = d != 0;
                DioramaStage_Choose(&request, &result);
                Tally(&tally[1], &result.seen, x, y, d != 0);
                /* As before: the 3DS scoring on the cells as they were read;
                 * measured as the camera sees it now. */
                request.context = &before;
                request.legacy = true;
                DioramaStage_Choose(&request, &result);
                request.context = &now;
                request.legacy = false;
                DioramaStage_Measure(&request, result.targetX, result.targetY, &seen);
                Tally(&tally[0], &seen, x, y, d != 0);
            }
        }
    if (tally[1].spots == 0)
        return;
    SDL_Log("Diorama stage survey: map %u.%u%s, %u stages (singles and doubles) round %d,%d, %.0f ms",
            snapshot->mapGroup, snapshot->mapNum, IsIndoor(snapshot->mapType) ? " indoor" : "",
            tally[1].spots, px - SURVEY_MAP_OFFSET, py - SURVEY_MAP_OFFSET,
            (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency());
    for (d = 0; d < 2; ++d)
    {
        const struct SurveyTally *t = &tally[d];

        SDL_Log("Diorama stage survey:   %s: a battler covered %u, black over 2%% %u (mean %.1f%%), "
                "pressed %u; worst at %d,%d%s (covered + black %.0f%%)",
                d == 0 ? "3DS scoring" : "now        ", t->covered, t->black, 100.0f * t->blackSum / t->spots,
                t->pressed, t->worstX - SURVEY_MAP_OFFSET, t->worstY - SURVEY_MAP_OFFSET,
                t->worstDoubles ? " doubles" : "", 100.0f * t->worst);
    }
}

/* ── Starting, stepping and ending ─────────────────────────────────────── */

static void Begin(void)
{
    const struct DioramaSceneSnapshot *snapshot;
    const struct DioramaResolvedCell *resolved;
    struct Layout layout;
    float cameraX, cameraZ, pitch, focal;
    SDL_Window *window = DioramaGL_GetWindow();

    if (!sBattle.ready || window == NULL
     || !DioramaGL_GetBattleWorld(sBattle.frame.mapGroup, sBattle.frame.mapNum, sBattle.frame.mapLayoutId,
                                  &snapshot, &resolved))
    {
        sBattle.refused = true;
        if (sBattle.log)
            SDL_Log("Diorama battle: drawn in 2D (no 3D field of map %u.%u to stand in)",
                    sBattle.frame.mapGroup, sBattle.frame.mapNum);
        return;
    }
    BattleLayout(window, &layout);
    sBattle.active = true;
    sBattle.chosen = false;
    sBattle.glided = false;
    sBattle.frames = 0;
    sBattle.glide = 0;
    sWorldCache.valid = false;
    sWorldCache.drawn = sWorldCache.kept = 0;
    sBattle.mapGroup = snapshot->mapGroup;
    sBattle.mapNum = snapshot->mapNum;
    sBattle.mapLayoutId = snapshot->mapLayoutId;
    BuildCurtain(snapshot, resolved);
    sArtValid = false;
    memcpy(sTileAnim.tiles, snapshot->tileGraphics, sizeof(sTileAnim.tiles));
    sTileAnim.ready = true;
    sTileAnim.decided = false;
    sTileAnim.step = 0;
    sTileAnim.stepMin = sTileAnim.stepMax = 0;
    sTileAnim.goodRedraws = 0;
    sTileAnim.frames = 0;
    sTileAnim.sinceRedraw = 0;
    sTileAnim.updates = 0;
    ResetFrameTimes();
    DioramaGL_GetFieldCamera(&cameraX, &cameraZ, &pitch, &focal);
    FieldView(&sBattle.field, &layout, cameraX, cameraZ, pitch, focal);
    sBattle.view = sBattle.field;
    ChooseStage(&layout, snapshot, resolved);
    /* Into 3D from the classic view in the middle of a battle: no flight. */
    if (sBattle.battleFrames > 30)
    {
        sBattle.glided = true;
        sBattle.glide = GLIDE_FRAMES;
        sBattle.frames = INTRO_WAIT;
    }
    if (sBattle.log)
    {
        /* Once a run: what draws it, for frame times sent from another computer. */
        static bool sDescribed;

        if (!sDescribed)
        {
            char renderer[256];
            char antialiasing[32];
            int windowW = 0, windowH = 0, sceneW = 0, sceneH = 0, samples = 0;

            sDescribed = true;
            DioramaGL_DescribeRenderer(renderer, sizeof(renderer));
            SDL_GL_GetDrawableSize(window, &windowW, &windowH);
            DioramaGLTarget_GetLastSize(&sceneW, &sceneH, &samples);
            if (samples > 1)
                SDL_snprintf(antialiasing, sizeof(antialiasing), "%dx MSAA", samples);
            else
                SDL_strlcpy(antialiasing, "no MSAA", sizeof(antialiasing));
            SDL_Log("Diorama battle: drawn by %s; the window %dx%d, the 3D view %dx%d, %s",
                    renderer, windowW, windowH, sceneW, sceneH, antialiasing);
        }
        SDL_Log("Diorama battle: 3D on map %u.%u (%s%s), player %d,%d facing %u",
                sBattle.frame.mapGroup, sBattle.frame.mapNum,
                sBattle.frame.wild ? "wild" : "trainer", sBattle.frame.doubles ? ", double" : "",
                sBattle.frame.playerMapX, sBattle.frame.playerMapY, sBattle.frame.playerFacing);
    }
}

static void End(void)
{
    if (sBattle.active)
    {
        if (sBattle.log)
        {
            SDL_Log("Diorama battle over after %u frames: the field's camera back "
                    "(world drawn %u times, kept %u; animated tiles redrew it %u times%s, "
                    "every %u to %u game frames)", sBattle.frames, sWorldCache.drawn,
                    sWorldCache.kept, sTileAnim.updates,
                    sTileAnim.mode == 0 ? ", off" : sTileAnim.mode == 1 ? ", forced on" : "",
                    sTileAnim.stepMin, sTileAnim.stepMax);
            SDL_Log("Diorama battle frame times: kept world %.2f ms on average (at most %.2f, %u frames); "
                    "world redrawn %.2f ms (at most %.2f, %u frames); glide %.2f ms (at most %.2f, %u frames)",
                    AverageFrameTime(&sFrameTime.kept), sFrameTime.kept.max, sFrameTime.kept.count,
                    AverageFrameTime(&sFrameTime.redraw), sFrameTime.redraw.max, sFrameTime.redraw.count,
                    AverageFrameTime(&sFrameTime.glide), sFrameTime.glide.max, sFrameTime.glide.count);
        }
    }
    sTileAnim.ready = false;
    sWorldCache.valid = false;
    sWide.valid = false;
    sWide.frames = 0;
    sBattle.active = false;
    sBattle.refused = false;
    sBattle.suspended = false;
    sBattle.chosen = false;
    sBattle.battleFrames = 0;
    gDioramaCameraRig.active = false;
}

/* While no world is shown (a move's own background on BG3, or BG3 off): ask
 * the GBA renderer for the screen past the picture, as wide as the window
 * shows round it (gDioramaSceneWide). */
static void RequestWide(bool moveBackground)
{
    struct Layout layout;
    SDL_Window *window = DioramaGL_GetWindow();
    int left, right, top;

    if (window == NULL)
        return;
    BattleLayout(window, &layout);
    if (layout.outW <= 0 || layout.outH <= 0 || layout.scale <= 0.0f)
        return;
    left = (int)ceilf(layout.gameX / layout.scale);
    right = (int)ceilf((layout.outW - layout.gameX - layout.gameW) / layout.scale);
    top = (int)ceilf(layout.gameY / layout.scale);
    if (left <= 0 && right <= 0 && top <= 0)
        return;
    gDioramaSceneWideLeft = (uint16_t)(left < 0 ? 0 : left > DIORAMA_WIDE_MAX_SIDE ? DIORAMA_WIDE_MAX_SIDE : left);
    gDioramaSceneWideRight = (uint16_t)(right < 0 ? 0 : right > DIORAMA_WIDE_MAX_SIDE ? DIORAMA_WIDE_MAX_SIDE : right);
    gDioramaSceneWideTop = (uint16_t)(top < 0 ? 0 : top > DIORAMA_WIDE_MAX_TOP ? DIORAMA_WIDE_MAX_TOP : top);
    gDioramaSceneWideWanted = true;
    sWide.moveBackground = moveBackground;
}

uint8_t DioramaGLBattle_BeginFrame(void)
{
    uint8_t layers = 0;

    if (!sBattle.ready)
        return 0;
    DioramaBattle_Capture(&sBattle.frame);
    if (sBattle.frame.inBattle)
    {
        if (!DioramaGL_IsAutoMode())
        {
            /* The classic view (F4): the battle as ever, until 3D is back. */
            if (sBattle.active)
            {
                sBattle.active = false;
                sBattle.suspended = true;
            }
        }
        else if (sBattle.suspended)
        {
            /* Back to 3D: straight onto the stage, no flight. */
            sBattle.active = true;
            sBattle.suspended = false;
            sWorldCache.valid = false;
            sBattle.glided = true;
            sBattle.glide = GLIDE_FRAMES;
            if (sBattle.frames < INTRO_WAIT)
                sBattle.frames = INTRO_WAIT;
        }
        else if (!sBattle.active && !sBattle.refused)
            Begin();
        ++sBattle.battleFrames;
    }
    else if (sBattle.active || sBattle.refused || sBattle.suspended)
        End();
    else if (sSurvey.on && !sBattle.frame.transition)
    {
        const struct DioramaSceneSnapshot *snapshot;
        const struct DioramaResolvedCell *resolved;

        if (DioramaGL_GetFieldScene(&snapshot, &resolved)
         && (!sSurvey.done || snapshot->mapGeneration != sSurvey.mapGeneration))
        {
            sSurvey.done = true;
            sSurvey.mapGeneration = snapshot->mapGeneration;
            SurveyStages(snapshot, resolved);
        }
    }

    sBattle.fieldCompose = false;
    memset(gDioramaSceneSkipObj, 0, sizeof(gDioramaSceneSkipObj));
    gDioramaSceneArtWanted = false;
    gDioramaSceneWideWanted = false;
    if (sBattle.active)
    {
        StepCamera();
        AnimateTiles();
        /* The world is BG3 when BG3 holds the scenery; while the intro slides
         * it in, also BG1 and BG2, its entry picture (not in a link battle,
         * whose intro shows its VS frame there). */
        if (sBattle.frame.sceneryShown && sBattle.frame.bg3Enabled && !sBattle.frame.forcedBlank)
        {
            layers = 1u << 3;
            if (sBattle.frame.introSliding && !sBattle.frame.link)
                layers |= (1u << 1) | (1u << 2);
            /* The scenery at rest, for DrawBackdrop (not while the intro
             * slides it in or a move shakes it). */
            gDioramaSceneArtWanted = !sBattle.frame.introSliding && sBattle.frame.sceneryShiftX == 0.0f
                                  && sBattle.frame.sceneryShiftY == 0.0f;
        }
        else if (!sBattle.frame.forcedBlank)
            RequestWide(sBattle.frame.bg3Enabled && !sBattle.frame.sceneryShown);
    }
    else if (!sBattle.frame.inBattle && sBattle.frame.transition && !sBattle.fieldOff
          && DioramaGL_IsAutoMode() && DioramaGL_DrewField3D())
    {
        /* A battle transition on the field, the field drawn in 3D: its three
         * map layers see-through and its map sprites left out, the 3D field
         * standing in for them (DioramaGLBattle_ComposeField), as the 3DS
         * project's RenderTransition composes it over its voxel world. */
        layers = (1u << 1) | (1u << 2) | (1u << 3);
        memcpy(gDioramaSceneSkipObj, gDioramaMapOam, sizeof(gDioramaSceneSkipObj));
        sBattle.fieldCompose = true;
        if (sBattle.log && !sBattle.composedLastFrame)
            SDL_Log("Diorama battle transition: composed over the 3D field");
        if (sBattle.log && (sBattle.frame.fieldMosaicH != 0 || sBattle.frame.fieldMosaicV != 0)
         && !sBattle.mosaicLogged)
        {
            SDL_Log("Diorama battle transition: the field in mosaic blocks of %u x %u",
                    sBattle.frame.fieldMosaicH + 1, sBattle.frame.fieldMosaicV + 1);
            sBattle.mosaicLogged = true;
        }
    }
    if (!sBattle.fieldCompose)
        sBattle.mosaicLogged = false;
    sBattle.composedLastFrame = sBattle.fieldCompose;
    sBattle.sceneLayers = layers;
    return layers;
}

/* The picture's edge colours to carry out (DrawCarriedOut); with a move's
 * background shown (moveBackground), only the text box's rows - the rest is
 * the wide picture's - and fully opaque, there being no world. */
static void UploadEdges(bool moveBackground)
{
    for (int y = 0; y < 160; ++y)
        for (int side = 0; side < 2; ++side)
        {
            const struct DioramaSceneEdge *edge = &gDioramaSceneEdges[y][side];
            uint32_t r = (edge->color & 0x1F) * 255 / 31;
            uint32_t g = ((edge->color >> 5) & 0x1F) * 255 / 31;
            uint32_t b = ((edge->color >> 10) & 0x1F) * 255 / 31;
            uint32_t a = (16u - edge->cover) * 255u / 16u;

            if (moveBackground)
                sEdgePixels[y * 2 + side] = edge->layer == 0 ? 0xFF000000u | (r << 16) | (g << 8) | b : 0;
            else
                sEdgePixels[y * 2 + side] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    glBindTexture(GL_TEXTURE_2D, sEdgeTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 160, GL_BGRA, GL_UNSIGNED_BYTE, sEdgePixels);
}

/* The wide picture the GBA renderer drew this frame into its texture. */
static void UploadWide(void)
{
    int width = gDioramaSceneWideLeft + 240 + gDioramaSceneWideRight;
    int height = gDioramaSceneWideTop + 160;

    for (int i = 0; i < width * height; ++i)
    {
        uint16_t c = gDioramaSceneWide[i];
        uint32_t r = (c & 0x1F) * 255 / 31, g = ((c >> 5) & 0x1F) * 255 / 31, b = ((c >> 10) & 0x1F) * 255 / 31;

        sWide.pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    if (sWide.texture == 0)
        MakeTexture(&sWide.texture, width, height, sWide.pixels);
    else if (sWide.width != width || sWide.height != height)
    {
        glBindTexture(GL_TEXTURE_2D, sWide.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, sWide.pixels);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, sWide.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, sWide.pixels);
    }
    sWide.width = width;
    sWide.height = height;
    sWide.left = gDioramaSceneWideLeft;
    sWide.right = gDioramaSceneWideRight;
    sWide.top = gDioramaSceneWideTop;
    sWide.valid = true;
}

void DioramaGLBattle_UploadFrame(const uint32_t *rgba)
{
    (void)rgba;
    if (!sBattle.ready)
        return;
    sWide.valid = false;
    if (gDioramaSceneWideWanted)
    {
        UploadWide();
        UploadEdges(true);
        if (sBattle.log && sWide.moveBackground && sWide.frames++ == 0)
            SDL_Log("Diorama battle: a move's own background, carried past the picture "
                    "(%d, %d and %d pixels left, right and above)", sWide.left, sWide.right, sWide.top);
        return;
    }
    if (sBattle.sceneLayers == 0)
        return;
    UploadEdges(false);
    if (gDioramaSceneArtWanted && (!sArtValid || memcmp(sArtLast, gDioramaSceneArt, sizeof(sArtLast)) != 0))
    {
        memcpy(sArtLast, gDioramaSceneArt, sizeof(sArtLast));
        for (int i = 0; i < 160 * 240; ++i)
        {
            uint16_t c = gDioramaSceneArt[i];
            uint32_t r = (c & 0x1F) * 255 / 31, g = ((c >> 5) & 0x1F) * 255 / 31, b = ((c >> 10) & 0x1F) * 255 / 31;

            sArtPixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        glBindTexture(GL_TEXTURE_2D, sArtTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_BGRA, GL_UNSIGNED_BYTE, sArtPixels);
        if (sBattle.log && !sArtValid)
            SDL_Log("Diorama battle: the scenery caught for where the world has nothing (top left %08x)",
                    (unsigned)sArtPixels[0]);
        sArtValid = true;
        ++sArtGeneration;
    }
}

bool DioramaGLBattle_IsActive(void)
{
    return sBattle.active;
}

/* ── Drawing ───────────────────────────────────────────────────────────── */

static void DrawShadows(const struct Layout *layout)
{
    static const float shade[4] = {8.0f / 255.0f * SHADOW_ALPHA, 20.0f / 255.0f * SHADOW_ALPHA,
                                   40.0f / 255.0f * SHADOW_ALPHA, SHADOW_ALPHA};

    QuadBegin();
    for (unsigned i = 0; i < sBattle.frame.shadowCount; ++i)
    {
        const struct DioramaBattleShadow *s = &sBattle.frame.shadows[i];
        float x = layout->gameX + s->x * layout->scale, y = layout->gameY + s->y * layout->scale;
        float rx = s->rx * layout->scale, ry = s->ry * layout->scale;

        PushRect(x - rx, y - ry, x + rx, y + ry, shade);
    }
    QuadFlush(layout, sWhiteTexture, 1);
}

/* The world's fade: towards the colour the scenery's palettes fade to. */
static void DrawFade(const struct Layout *layout)
{
    float a = sBattle.frame.fadeAmount;
    float c[4] = {sBattle.frame.fadeRgb[0] * a, sBattle.frame.fadeRgb[1] * a, sBattle.frame.fadeRgb[2] * a, a};

    if (a <= 0.0f)
        return;
    QuadBegin();
    PushRect(0.0f, 0.0f, (float)layout->outW, (float)layout->outH, c);
    QuadFlush(layout, sWhiteTexture, 0);
}

/* Over the world, under the picture: the speed lines and the vignette while
 * the camera flies in, and the landing's flash (3ds_video.c). */
static void DrawIntroAtmosphere(const struct Layout *layout)
{
    const struct IntroEffects *intro = &sBattle.intro;
    float w = (float)layout->outW, bottom = layout->gameY + 112.0f * layout->scale;
    float unit = layout->outH / 240.0f; /* the 3DS screen's pixel */
    float shade = fmaxf(0.60f * intro->flight, 0.30f * intro->bars);

    QuadBegin();
    if (intro->speed > 0.04f)
    {
        /* Streaks racing right to left, none across the middle band where
         * the battlers stand; each its own height, length and pace. */
        for (unsigned i = 0; i < INTRO_STREAKS; ++i)
        {
            uint32_t h = (i + 1) * 2654435761u;
            float y = (6.0f + (float)((h >> 8) % 160u)) * unit;
            float length = (70.0f + (float)((h >> 16) % 110u)) * unit;
            float pace = (26.0f + (float)((h >> 20) % 18u)) * unit;
            float x = w - fmodf(intro->time * pace + (float)(h % 400u) * unit, w + length);
            float alpha = intro->speed * (0.35f + 0.35f * (float)((h >> 4) & 3) / 3.0f);
            float head[4] = {225.0f / 255.0f * alpha, 236.0f / 255.0f * alpha, alpha, alpha};
            static const float tail[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float thick = ((h & 16) ? 2.0f : 1.0f) * unit;

            if (fabsf(y - bottom * 0.55f) < 24.0f * unit)
                y = y < bottom * 0.55f ? y - 24.0f * unit : y + 24.0f * unit;
            PushQuad(x, y, x + length, y + thick, 0, 0, 1, 1, head, tail, head, tail);
        }
    }
    if (shade > 0.01f)
    {
        float a = fminf(shade, 1.0f) * 230.0f / 255.0f;
        float dark[4] = {8.0f / 255.0f * a, 6.0f / 255.0f * a, 34.0f / 255.0f * a, a};
        static const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float band = 64.0f * unit, side = 96.0f * unit;

        PushQuad(0, 0, w, band, 0, 0, 1, 1, dark, dark, none, none);
        PushQuad(0, bottom - band, w, bottom, 0, 0, 1, 1, none, none, dark, dark);
        PushQuad(0, 0, side, bottom, 0, 0, 1, 1, dark, none, dark, none);
        PushQuad(w - side, 0, w, bottom, 0, 0, 1, 1, none, dark, none, dark);
    }
    if (intro->impact > 0.01f)
    {
        float a = fminf(intro->impact, 1.0f) * 170.0f / 255.0f;
        float flash[4] = {a, 250.0f / 255.0f * a, 235.0f / 255.0f * a, a};

        PushRect(0, 0, w, (float)layout->outH, flash);
    }
    QuadFlush(layout, sWhiteTexture, 0);
}

/* The cinema bars, over everything - the battlers and the text box too. */
static void DrawIntroBars(const struct Layout *layout)
{
    const struct IntroEffects *intro = &sBattle.intro;
    float w = (float)layout->outW, h = (float)layout->outH;
    float bar = INTRO_BAR * h * intro->bars, edge = INTRO_BAR_EDGE * h * intro->bars;
    static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f}, none[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (intro->bars <= 0.01f || sBattle.frame.forcedBlank)
        return;
    QuadBegin();
    PushRect(0, 0, w, bar, black);
    PushQuad(0, bar, w, bar + edge, 0, 0, 1, 1, black, black, none, none);
    PushRect(0, h - bar, w, h, black);
    PushQuad(0, h - bar - edge, w, h - bar, 0, 0, 1, 1, none, none, black, black);
    QuadFlush(layout, sWhiteTexture, 0);
}

/* The picture's background carried out past its edges: each line's edge
 * colour and cover to the sides, its first line's above, its last below. */
/* The wide picture round the battle's own (a move's background, V13): GBA
 * pixel for GBA pixel at the picture's scale, so nothing is stretched. */
static void DrawWide(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float x0 = layout->gameX - sWide.left * layout->scale;
    float y0 = layout->gameY - sWide.top * layout->scale;
    float x1 = layout->gameX + (240 + sWide.right) * layout->scale;
    float y1 = layout->gameY + 160.0f * layout->scale;

    QuadBegin();
    PushQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, white, white, white, white);
    QuadFlush(layout, sWide.texture, 2);
}

static void DrawCarriedOut(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float x0 = layout->gameX, x1 = layout->gameX + layout->gameW;
    float y0 = layout->gameY, y1 = layout->gameY + layout->gameH;
    float w = (float)layout->outW, h = (float)layout->outH, middle = x0 + layout->gameW * 0.5f;

    QuadBegin();
    if (x0 > 0.0f)
        PushQuad(0.0f, y0, x0, y1, 0.25f, 0.0f, 0.25f, 1.0f, white, white, white, white);
    if (x1 < w)
        PushQuad(x1, y0, w, y1, 0.75f, 0.0f, 0.75f, 1.0f, white, white, white, white);
    if (y0 > 0.0f)
    {
        float v = 0.5f / 160.0f;

        PushQuad(0.0f, 0.0f, middle, y0, 0.25f, v, 0.25f, v, white, white, white, white);
        PushQuad(middle, 0.0f, w, y0, 0.75f, v, 0.75f, v, white, white, white, white);
    }
    if (y1 < h)
    {
        float v = 159.5f / 160.0f;

        PushQuad(0.0f, y1, middle, h, 0.25f, v, 0.25f, v, white, white, white, white);
        PushQuad(middle, y1, w, h, 0.75f, v, 0.75f, v, white, white, white, white);
    }
    QuadFlush(layout, sEdgeTexture, 0);
}

/* Right after the world is drawn, before it is kept: where it has nothing
 * (the depth still the far plane's), the scenery as the GBA would show it
 * (sArtTexture, its edges carried out to the window's). */
static void DrawBackdrop(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float u0, u1, v0, v1;

    if (!sArtValid)
        return;
    u0 = -layout->gameX / layout->scale / 240.0f;
    u1 = ((float)layout->outW - layout->gameX) / layout->scale / 240.0f;
    v0 = -layout->gameY / layout->scale / 160.0f;
    v1 = ((float)layout->outH - layout->gameY) / layout->scale / 160.0f;
    QuadBegin();
    PushQuad(0.0f, 0.0f, (float)layout->outW, (float)layout->outH, u0, v0, u1, v1, white, white, white, white);
    QuadFlushAs(layout, sArtTexture, 0, QUAD_FAR);
}

static bool IsWorldKept(const struct View *view, const struct Layout *layout)
{
    return sWorldCache.valid && !sWorldCache.off
        && memcmp(&sWorldCache.view, view, sizeof(*view)) == 0
        && memcmp(&sWorldCache.layout, layout, sizeof(*layout)) == 0
        && sWorldCache.terrainGeneration == DioramaGLTerrain_GetGeometryGeneration()
        && sWorldCache.buildingGeneration == DioramaGLBuildings_GetGeometryGeneration()
        && sWorldCache.treeGeneration == DioramaGLTreeSprites_GetGeometryGeneration()
        && sWorldCache.artGeneration == sArtGeneration;
}

/* Right after the world is drawn, before anything over it: copy it out. */
static void KeepWorld(const struct View *view, const struct Layout *layout)
{
    if (sWorldCache.texture == 0 || sWorldCache.width != layout->outW
     || sWorldCache.height != layout->outH)
    {
        if (sWorldCache.texture == 0)
            MakeTexture(&sWorldCache.texture, layout->outW, layout->outH, NULL);
        else
        {
            glBindTexture(GL_TEXTURE_2D, sWorldCache.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, layout->outW, layout->outH, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        }
        sWorldCache.width = layout->outW;
        sWorldCache.height = layout->outH;
    }
    glBindTexture(GL_TEXTURE_2D, sWorldCache.texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, layout->outW, layout->outH);
    sWorldCache.view = *view;
    sWorldCache.layout = *layout;
    sWorldCache.terrainGeneration = DioramaGLTerrain_GetGeometryGeneration();
    sWorldCache.buildingGeneration = DioramaGLBuildings_GetGeometryGeneration();
    sWorldCache.treeGeneration = DioramaGLTreeSprites_GetGeometryGeneration();
    sWorldCache.artGeneration = sArtGeneration;
    sWorldCache.valid = true;
}

static void DrawKeptWorld(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /* The copy's rows run bottom up, as GL's window does. */
    QuadBegin();
    PushQuad(0.0f, 0.0f, (float)layout->outW, (float)layout->outH, 0.0f, 1.0f, 1.0f, 0.0f,
             white, white, white, white);
    QuadFlush(layout, sWorldCache.texture, 2);
}

/* The layout in the scene's pixels when the world is drawn at the scene's own
 * size (gl_target.h): the same picture, scaled with the window's shape. */
static void SceneLayout(const struct Layout *layout, int sceneW, int sceneH, struct Layout *scene)
{
    float kx = (float)sceneW / (float)layout->outW, ky = (float)sceneH / (float)layout->outH;

    *scene = *layout;
    scene->outW = sceneW;
    scene->outH = sceneH;
    scene->gameX = layout->gameX * kx;
    scene->gameY = layout->gameY * ky;
    scene->gameW = layout->gameW * kx;
    scene->gameH = layout->gameH * ky;
    scene->scale = layout->scale * ky;
}

/* The world from this frame's view: drawn, or the copy of it if the camera
 * has not moved since. It is drawn as the field's scene is, at the chosen
 * render size and with the chosen antialiasing (gl_target.h: offscreen and
 * then scaled into the window, or straight into it), so a battle looks like
 * the field it stands on and costs what the field costs; the battle's own
 * picture and everything 2D over it stay at the window's resolution. */
static void DrawWorld(const struct Layout *layout)
{
    struct Layout scene;
    int sceneW, sceneH;
    float focal, aspect;

    if (IsWorldKept(&sBattle.view, layout))
    {
        DrawKeptWorld(layout);
        ++sWorldCache.kept;
        return;
    }
    DioramaGLTarget_Begin(layout->outW, layout->outH, &sceneW, &sceneH);
    SceneLayout(layout, sceneW, sceneH, &scene);
    ApplyView(&sBattle.view, &scene, &focal, &aspect);
    sFrameTime.drew = true;
    DioramaGL_DrawBattleWorld(sBattle.view.x, sBattle.view.z, sBattle.view.pitch, focal, aspect,
                              scene.outW, scene.outH);
    DrawCurtain(&scene, focal, aspect);
    gDioramaCameraRig.active = false;
    glViewport(0, 0, scene.outW, scene.outH);
    DrawBackdrop(&scene);
    DioramaGLTarget_End(layout->outW, layout->outH);
    glViewport(0, 0, layout->outW, layout->outH);
    if (!sWorldCache.off)
        KeepWorld(&sBattle.view, layout);
    ++sWorldCache.drawn;
}

/* The battle's own picture: premultiplied over the world (a picture drawn
 * without scene layers is opaque, alpha 255 everywhere). */
static void DrawPicture(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    QuadBegin();
    PushQuad(layout->gameX, layout->gameY, layout->gameX + layout->gameW, layout->gameY + layout->gameH,
             0.0f, 0.0f, 1.0f, 1.0f, white, white, white, white);
    QuadFlush(layout, DioramaGL_GetFrameTexture(), 0);
}

/* ── A battle transition over the 3D field ──────────────────────────── */

/*
 * The 3DS project's RenderTransition/TransitionField on one PC screen: the 3D
 * field the compositor has just drawn is copied out and laid back band by
 * band of the picture's lines, each moved as the transition moves the
 * field's layers on those lines (gDioramaSceneLineShift; what moves past the
 * copy's edge leaves black, the backdrop; the Blur transition's mosaic in its
 * blocks); the compositor draws what the GBA draws as sprites or windows -
 * the people, the weather, a dark cave's light - over that, where they stand,
 * and the transition's own picture - the field's layers see-through, its
 * windows, BG0, its sprites and the blending, brightness included - goes over
 * it all, carried out to the window's edges.
 */
bool DioramaGLBattle_CanComposeField(void)
{
    return sBattle.ready && !sBattle.fieldOff && DioramaGL_IsAutoMode();
}

static void ComposeLayout(struct Layout *layout, int outW, int outH, float gameX, float gameY, float gameW, float gameH)
{
    layout->outW = outW;
    layout->outH = outH;
    layout->gameX = gameX;
    layout->gameY = gameY;
    layout->gameW = gameW;
    layout->gameH = gameH;
    layout->scale = gameH / GBA_H;
}

bool DioramaGLBattle_WantsField(void)
{
    return sBattle.ready && sBattle.fieldCompose;
}

bool DioramaGLBattle_ComposeFieldBands(int outW, int outH, float gameX, float gameY, float gameW, float gameH)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct Layout layout;
    float scale;
    int y0, y1;
    GLint drawFramebuffer = 0;

    if (!sBattle.ready || !sBattle.fieldCompose || outW <= 0 || outH <= 0 || gameH <= 0.0f)
        return false;
    ComposeLayout(&layout, outW, outH, gameX, gameY, gameW, gameH);
    scale = layout.scale;

    if (sFieldTexture == 0 || sFieldWidth != outW || sFieldHeight != outH)
    {
        if (sFieldTexture == 0)
            MakeTexture(&sFieldTexture, outW, outH, NULL);
        else
        {
            glBindTexture(GL_TEXTURE_2D, sFieldTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        }
        sFieldWidth = outW;
        sFieldHeight = outH;
    }
    glViewport(0, 0, outW, outH);
    glBindTexture(GL_TEXTURE_2D, sFieldTexture);
    /* Read from the scene's framebuffer, a resolved copy of it if it is
     * multisampled (a multisampled one cannot be copied from). */
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, DioramaGLTarget_ReadableFramebuffer((unsigned)drawFramebuffer));
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, outW, outH);
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)drawFramebuffer);
    /* The colour only: the people drawn next are hidden by the field's
     * ground and models where they were, as ever. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* The field, band by band: rows of equal shift, the first band reaching
     * up to the window's top and the last down to its bottom. */
    QuadBegin();
    for (y0 = 0; y0 < 160; y0 = y1)
    {
        float dx = gDioramaSceneLineShift[y0][0] * scale, dy = gDioramaSceneLineShift[y0][1] * scale;
        float top, bottom, left, right;

        for (y1 = y0 + 1; y1 < 160; ++y1)
            if (gDioramaSceneLineShift[y1][0] != gDioramaSceneLineShift[y0][0]
             || gDioramaSceneLineShift[y1][1] != gDioramaSceneLineShift[y0][1])
                break;
        top = y0 == 0 ? 0.0f : layout.gameY + y0 * scale;
        bottom = y1 == 160 ? (float)outH : layout.gameY + y1 * scale;
        /* Only what the copy holds. */
        left = dx < 0.0f ? -dx : 0.0f;
        right = dx > 0.0f ? outW - dx : (float)outW;
        if (top + dy < 0.0f)
            top = -dy;
        if (bottom + dy > outH)
            bottom = outH - dy;
        if (left >= right || top >= bottom)
            continue;
        /* The copy's rows run bottom up. */
        PushQuad(left, top, right, bottom,
                 (left + dx) / outW, 1.0f - (top + dy) / outH, (right + dx) / outW, 1.0f - (bottom + dy) / outH,
                 white, white, white, white);
    }
    /* The Blur transition's mosaic on the field's layers: the field in its
     * blocks, from the GBA picture's corner. */
    if (sBattle.frame.fieldMosaicH != 0 || sBattle.frame.fieldMosaicV != 0)
    {
        dglUseProgram(sProgram);
        dglUniform2f(sMosaicLocation, (sBattle.frame.fieldMosaicH + 1) * scale,
                     (sBattle.frame.fieldMosaicV + 1) * scale);
        dglUniform2f(sMosaicOriginLocation, layout.gameX, layout.gameY);
        dglUniform2f(sOutSizeLocation, (float)outW, (float)outH);
        QuadFlush(&layout, sFieldTexture, 5);
    }
    else
        QuadFlush(&layout, sFieldTexture, 2);
    return true;
}

void DioramaGLBattle_ComposeFieldPicture(int outW, int outH, int gameX, int gameY, int gameW, int gameH)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct Layout layout;

    if (!sBattle.ready || !sBattle.fieldCompose || outW <= 0 || outH <= 0 || gameH <= 0)
        return;
    ComposeLayout(&layout, outW, outH, gameX, gameY, gameW, gameH);
    glViewport(0, 0, outW, outH);
    glDisable(GL_SCISSOR_TEST);
    /* The transition's own picture over it all. */
    DrawCarriedOut(&layout);
    QuadBegin();
    PushQuad(layout.gameX, layout.gameY, layout.gameX + layout.gameW, layout.gameY + layout.gameH,
             0.0f, 0.0f, 1.0f, 1.0f, white, white, white, white);
    QuadFlush(&layout, DioramaGL_GetFrameTexture(), 0);
    glDisable(GL_BLEND);
}

bool DioramaGLBattle_Present(SDL_Window *window)
{
    struct Layout layout;
    bool world;

    if (!sBattle.ready || !sBattle.active)
        return false;
    BattleLayout(window, &layout);
    if (layout.outW <= 0 || layout.outH <= 0)
        return false;
    MeasureFrame();
    world = sBattle.sceneLayers != 0;
    glViewport(0, 0, layout.outW, layout.outH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (world)
    {
        DrawWorld(&layout);
        DrawFade(&layout);
        DrawShadows(&layout);
        DrawIntroAtmosphere(&layout);
        DrawCarriedOut(&layout);
    }
    else if (sWide.valid)
    {
        DrawWide(&layout);
        DrawCarriedOut(&layout);
    }
    DrawPicture(&layout);
    if (world)
        DrawIntroBars(&layout);
    glDisable(GL_BLEND);
    EndFrameMeasure();
    return true;
}

#endif
