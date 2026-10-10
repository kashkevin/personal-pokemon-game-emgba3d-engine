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
#include "diorama/gl_terrain_renderer.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"
#include "diorama/terrain_mesh.h"
#include "gba/defines.h"
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
/* The longest the field may take to fade back in after a battle. */
#define RETURN_FRAMES 300u

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
    uint32_t battleFrames; /* game frames since the battle began, 3D or not */
    bool returning;  /* a 3D battle just ended; the field not yet back in 3D */
    uint32_t returnFrames;
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
} sBattle;

static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLint sModeLocation;
static GLuint sEdgeTexture;
static GLuint sWhiteTexture;
static uint32_t sEdgePixels[160 * 2];

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
    uint32_t terrainGeneration, buildingGeneration;
    uint32_t drawn, kept; /* frames drawn and frames reused, for the log */
} sWorldCache;

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
    "out vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = vec4(position.x * 2.0 / viewport.x - 1.0, 1.0 - position.y * 2.0 / viewport.y, 0.0, 1.0);\n"
    "  uv = texCoord;\n"
    "  color = tint;\n"
    "}\n";

/* mode 0: texture times tint; 1: a soft disc (the contact shadow), times tint;
 * 2: the texture's colour, opaque (the kept world). */
static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in vec4 color;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D image;\n"
    "uniform int mode;\n"
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
    MakeTexture(&sWhiteTexture, 1, 1, &white);
    setting = SDL_getenv("PKMN_DIORAMA_BATTLE_KEEP");
    sWorldCache.off = setting != NULL && setting[0] == '0';
    sBattle.ready = true;
    return true;
}

void DioramaGLBattle_Shutdown(void)
{
    if (sWorldCache.texture != 0) glDeleteTextures(1, &sWorldCache.texture);
    memset(&sWorldCache, 0, sizeof(sWorldCache));
    if (sEdgeTexture != 0) glDeleteTextures(1, &sEdgeTexture);
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

static void QuadFlush(const struct Layout *layout, GLuint texture, int mode)
{
    if (sQuadCount == 0)
        return;
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sQuadCount * sizeof(*sQuads), sQuads, GL_STREAM_DRAW);
    dglUniform2f(sViewportLocation, (float)layout->outW, (float)layout->outH);
    dglUniform1i(sModeLocation, mode);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (mode == 2)
        glDisable(GL_BLEND);
    else
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDrawArrays(GL_TRIANGLES, 0, sQuadCount);
    sQuadCount = 0;
}

/* ── The battle's picture on the window ────────────────────────────────── */

/*
 * The GBA picture at the scale the compositor gives it (DioramaGL_Present),
 * on the bottom edge of the window: the text box at the bottom, the scene
 * carried out above and to both sides.
 */
static void GetLayout(SDL_Window *window, struct Layout *layout)
{
    bool integerScale = DioramaGL_GetIntegerScale();

    SDL_GL_GetDrawableSize(window, &layout->outW, &layout->outH);
    if (integerScale)
    {
        int scale = layout->outW / 240;

        if (layout->outH / 160 < scale)
            scale = layout->outH / 160;
        if (scale < 1)
            scale = 1;
        layout->gameW = 240.0f * scale;
        layout->gameH = 160.0f * scale;
    }
    else
    {
        layout->gameH = (float)(layout->outH * 8 / 9);
        layout->gameW = layout->gameH * 1.5f;
    }
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
};

static bool StageCellAt(void *context, int x, int y, struct DioramaStageCell *cell)
{
    const struct StageContext *c = context;
    const struct DioramaSceneSnapshot *snapshot = c->snapshot;
    int gx = x - snapshot->gridOriginX, gy = y - snapshot->gridOriginY;
    const struct DioramaCellSnapshot *source;
    const struct DioramaResolvedCell *resolved;
    uint16_t ground;
    uint8_t quarters;
    float top = 0.0f, modelTop;
    int index;

    if (gx < 0 || gy < 0 || gx >= DIORAMA_GRID_WIDTH || gy >= DIORAMA_GRID_HEIGHT)
        return false;
    index = gy * DIORAMA_GRID_WIDTH + gx;
    if (index >= snapshot->visibleCellCount)
        return false;
    source = &snapshot->cells[index];
    if (source->mapX != x || source->mapY != y || !(source->flags & DIORAMA_CELL_SOURCE_VALID))
        return false;
    resolved = &c->resolved[index];
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
    if (resolved->archetype == DIORAMA_ARCHETYPE_TREE && top < 2.4f)
        top = 2.4f;
    switch (resolved->shape)
    {
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

static void ChooseStage(const struct Layout *layout, const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolved)
{
    struct StageContext context = {snapshot, resolved};
    struct DioramaStageRequest request;
    struct DioramaStageResult *result = &sBattle.stageResult;
    Uint64 start = SDL_GetPerformanceCounter();

    memset(&request, 0, sizeof(request));
    request.view.pitch = EnvFloat("PKMN_BATTLE3D_PITCH", BATTLE_PITCH / DEG) * DEG;
    request.view.distance = EnvFloat("PKMN_BATTLE3D_DISTANCE", BATTLE_DISTANCE);
    request.view.focal = EnvFloat("PKMN_BATTLE3D_FOCAL", BATTLE_FOCAL);
    request.view.targetRow = EnvFloat("PKMN_BATTLE3D_ROW", BATTLE_TARGET_ROW);
    VisibleRect(layout, &request.visibleLeft, &request.visibleRight, &request.visibleTop,
                &request.visibleBottom);
    /* The search works in the world's cells: the snapshot's map grid. */
    request.playerX = sBattle.frame.playerGridX;
    request.playerY = sBattle.frame.playerGridY;
    request.facing = sBattle.frame.playerFacing;
    request.surfing = sBattle.frame.surfing;
    request.wild = sBattle.frame.wild;
    request.indoor = IsIndoor(snapshot->mapType);
    request.cellAt = StageCellAt;
    request.context = &context;
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
                "spot) ground %.2f score %.1f%s, %.2f ms",
                result->want[0], result->want[1], result->targetX - offsetX, result->targetY - offsetY,
                result->offsetX, result->offsetY, result->ground, result->score,
                request.indoor ? " indoor" : "",
                (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency());
        /* The ground round the player as the search read it: . plain, g grass,
         * s sand, ~ water, space nothing, # something tall, ? not in the
         * scene, P the player. */
        for (int dy = -5; dy <= 2; ++dy)
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
    GetLayout(window, &layout);
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
        SDL_Log("Diorama battle: 3D on map %u.%u (%s%s), player %d,%d facing %u",
                sBattle.frame.mapGroup, sBattle.frame.mapNum,
                sBattle.frame.wild ? "wild" : "trainer", sBattle.frame.doubles ? ", double" : "",
                sBattle.frame.playerMapX, sBattle.frame.playerMapY, sBattle.frame.playerFacing);
}

static void End(void)
{
    if (sBattle.active)
    {
        sBattle.returning = true;
        sBattle.returnFrames = 0;
        if (sBattle.log)
            SDL_Log("Diorama battle over after %u frames: the field's camera back "
                    "(world drawn %u times, kept %u)", sBattle.frames, sWorldCache.drawn, sWorldCache.kept);
    }
    sWorldCache.valid = false;
    sBattle.active = false;
    sBattle.refused = false;
    sBattle.suspended = false;
    sBattle.chosen = false;
    sBattle.battleFrames = 0;
    gDioramaCameraRig.active = false;
}

uint8_t DioramaGLBattle_BeginFrame(void)
{
    uint8_t layers = 0;

    if (!sBattle.ready)
        return 0;
    DioramaBattle_Capture(&sBattle.frame);
    if (sBattle.frame.inBattle)
    {
        sBattle.returning = false;
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
    /* The return lasts the field's fade in; past a few seconds, or in the
     * classic view, whatever comes next is not part of it. */
    else if (sBattle.returning && (++sBattle.returnFrames > RETURN_FRAMES || !DioramaGL_IsAutoMode()))
        sBattle.returning = false;

    if (sBattle.active)
    {
        StepCamera();
        /* The world is BG3 when BG3 holds the scenery; while the intro slides
         * it in, also BG1 and BG2, its entry picture (not in a link battle,
         * whose intro shows its VS frame there). */
        if (sBattle.frame.sceneryShown && sBattle.frame.bg3Enabled && !sBattle.frame.forcedBlank)
        {
            layers = 1u << 3;
            if (sBattle.frame.introSliding && !sBattle.frame.link)
                layers |= (1u << 1) | (1u << 2);
        }
    }
    sBattle.sceneLayers = layers;
    return layers;
}

void DioramaGLBattle_UploadFrame(const uint32_t *rgba)
{
    (void)rgba;
    if (!sBattle.ready || sBattle.sceneLayers == 0)
        return;
    for (int y = 0; y < 160; ++y)
        for (int side = 0; side < 2; ++side)
        {
            const struct DioramaSceneEdge *edge = &gDioramaSceneEdges[y][side];
            uint32_t r = (edge->color & 0x1F) * 255 / 31;
            uint32_t g = ((edge->color >> 5) & 0x1F) * 255 / 31;
            uint32_t b = ((edge->color >> 10) & 0x1F) * 255 / 31;
            uint32_t a = (16u - edge->cover) * 255u / 16u;

            sEdgePixels[y * 2 + side] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    glBindTexture(GL_TEXTURE_2D, sEdgeTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 160, GL_BGRA, GL_UNSIGNED_BYTE, sEdgePixels);
}

bool DioramaGLBattle_IsActive(void)
{
    return sBattle.active;
}

bool DioramaGLBattle_IsReturning(uint8_t mapGroup, uint8_t mapNum, uint16_t mapLayoutId)
{
    return sBattle.returning && mapGroup == sBattle.mapGroup && mapNum == sBattle.mapNum
        && mapLayoutId == sBattle.mapLayoutId;
}

void DioramaGLBattle_FieldDrawn(void)
{
    if (sBattle.returning && sBattle.log)
        SDL_Log("Diorama battle: the field back in 3D after %u frames", sBattle.returnFrames);
    sBattle.returning = false;
}

/*
 * The fade that best explains the shown colours from the steady ones, shown =
 * keep * steady + (1 - keep) * colour, by least squares over all of them (the
 * 3DS port's FadeFit, as battle_capture.c uses it for the battle's scenery).
 */
static void FitFade(const uint16_t *shown, const uint16_t *steady, unsigned count,
                    float *amount, float rgb[3])
{
    float n = (float)count, cov = 0.0f, var = 0.0f, keep;
    float su[3] = {0}, sf[3] = {0}, suu[3] = {0}, suf[3] = {0};

    for (unsigned i = 0; i < count; ++i)
        for (unsigned c = 0; c < 3; ++c)
        {
            float u = (float)((steady[i] >> (c * 5)) & 31), f = (float)((shown[i] >> (c * 5)) & 31);

            su[c] += u;
            sf[c] += f;
            suu[c] += u * u;
            suf[c] += u * f;
        }
    for (unsigned c = 0; c < 3; ++c)
    {
        cov += n * suf[c] - su[c] * sf[c];
        var += n * suu[c] - su[c] * su[c];
    }
    keep = var > 1.0f ? Clamp(cov / var, 0.0f, 1.0f) : 0.0f;
    *amount = 1.0f - keep;
    for (unsigned c = 0; c < 3; ++c)
    {
        float offset = (sf[c] - keep * su[c]) / n;

        rgb[c] = *amount > 0.01f ? Clamp(offset / *amount, 0.0f, 31.0f) / 31.0f : 0.0f;
    }
}

void DioramaGLBattle_DrawFieldFade(const uint16_t *faded, const uint16_t *unfaded, unsigned count,
                                   int outW, int outH)
{
    struct Layout layout = {outW, outH, 0.0f, 0.0f, (float)outW, (float)outH, 1.0f};
    float amount, rgb[3], c[4];

    if (!sBattle.ready || outW <= 0 || outH <= 0 || memcmp(faded, unfaded, count * sizeof(*faded)) == 0)
        return;
    FitFade(faded, unfaded, count, &amount, rgb);
    /* Below one step of the game's own 16 it is rounding, not a fade. */
    if (amount < 1.0f / 32.0f)
        return;
    c[0] = rgb[0] * amount;
    c[1] = rgb[1] * amount;
    c[2] = rgb[2] * amount;
    c[3] = amount;
    glViewport(0, 0, outW, outH);
    QuadBegin();
    PushRect(0.0f, 0.0f, (float)outW, (float)outH, c);
    QuadFlush(&layout, sWhiteTexture, 0);
    glDisable(GL_BLEND);
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
 * colour and cover to the sides, its first line's above. */
static void DrawCarriedOut(const struct Layout *layout)
{
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float x0 = layout->gameX, x1 = layout->gameX + layout->gameW;
    float y0 = layout->gameY, y1 = layout->gameY + layout->gameH;
    float w = (float)layout->outW;

    QuadBegin();
    if (x0 > 0.0f)
        PushQuad(0.0f, y0, x0, y1, 0.25f, 0.0f, 0.25f, 1.0f, white, white, white, white);
    if (x1 < w)
        PushQuad(x1, y0, w, y1, 0.75f, 0.0f, 0.75f, 1.0f, white, white, white, white);
    if (y0 > 0.0f)
    {
        float v = 0.5f / 160.0f;

        PushQuad(0.0f, 0.0f, x0 + layout->gameW * 0.5f, y0, 0.25f, v, 0.25f, v, white, white, white, white);
        PushQuad(x0 + layout->gameW * 0.5f, 0.0f, w, y0, 0.75f, v, 0.75f, v, white, white, white, white);
    }
    QuadFlush(layout, sEdgeTexture, 0);
}

static bool IsWorldKept(const struct View *view, const struct Layout *layout)
{
    return sWorldCache.valid && !sWorldCache.off
        && memcmp(&sWorldCache.view, view, sizeof(*view)) == 0
        && memcmp(&sWorldCache.layout, layout, sizeof(*layout)) == 0
        && sWorldCache.terrainGeneration == DioramaGLTerrain_GetGeometryGeneration()
        && sWorldCache.buildingGeneration == DioramaGLBuildings_GetGeometryGeneration();
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

/* The world from this frame's view: drawn, or the copy of it if the camera
 * has not moved since. */
static void DrawWorld(const struct Layout *layout)
{
    float focal, aspect;

    if (IsWorldKept(&sBattle.view, layout))
    {
        DrawKeptWorld(layout);
        ++sWorldCache.kept;
        return;
    }
    ApplyView(&sBattle.view, layout, &focal, &aspect);
    DioramaGL_DrawBattleWorld(sBattle.view.x, sBattle.view.z, sBattle.view.pitch, focal, aspect,
                              layout->outW, layout->outH);
    gDioramaCameraRig.active = false;
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

bool DioramaGLBattle_Present(SDL_Window *window)
{
    struct Layout layout;
    bool world;

    if (!sBattle.ready || !sBattle.active)
        return false;
    GetLayout(window, &layout);
    if (layout.outW <= 0 || layout.outH <= 0)
        return false;
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
    DrawPicture(&layout);
    if (world)
        DrawIntroBars(&layout);
    glDisable(GL_BLEND);
    return true;
}

#endif
