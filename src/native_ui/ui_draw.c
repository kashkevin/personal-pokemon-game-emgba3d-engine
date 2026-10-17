#ifdef ENABLE_NATIVE_UI
/*
 * The native UI's drawing context and its OpenGL backend (see
 * native_ui/ui_draw.h). Project-owned.
 *
 * One frame = a list of quads in logical canvas pixels, uploaded as one vertex
 * buffer and drawn with one program in as few draw calls as the clip changes
 * need. Quads are solid fills, pieces from the atlas (a 2D texture array of
 * RGBA layers holding every shape, label and picture rasterised once), glyphs
 * from the engine-font texture (indices: 1 ink, 2 shadow) and procedural
 * pattern fills evaluated per logical pixel in the fragment shader, so nothing
 * is uploaded per frame except the vertices.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_text.h"

/* ---------------------------------------------------------------- extra GL entry points */

typedef void (APIENTRY *PfnTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (APIENTRY *PfnTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (APIENTRY *PfnVertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void *);
static PfnTexImage3D sTexImage3D;
static PfnTexSubImage3D sTexSubImage3D;
static PfnVertexAttribIPointer sVertexAttribIPointer;

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif

/* ---------------------------------------------------------------- shaders */

static const char *const sVertexSource =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUv;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "layout(location = 3) in vec4 aColor2;\n"
    "layout(location = 4) in uvec4 aMode;\n"   /* mode, layer, pattern kind, dir/stagger */
    "layout(location = 5) in ivec4 aParam;\n"  /* pattern parameters */
    "layout(location = 6) in ivec2 aOrigin;\n" /* pattern origin (canvas) */
    "uniform vec2 uScreen;\n"
    "uniform vec2 uOrigin;\n"
    "uniform float uScale;\n"
    "out vec2 vUv;\n"
    "out vec4 vColor;\n"
    "out vec4 vColor2;\n"
    "out vec2 vPos;\n"
    "flat out uvec4 vMode;\n"
    "flat out ivec4 vParam;\n"
    "flat out ivec2 vOrigin;\n"
    "void main()\n"
    "{\n"
    "  vec2 px = uOrigin + aPos * uScale;\n"
    "  gl_Position = vec4(px.x * 2.0 / uScreen.x - 1.0, 1.0 - px.y * 2.0 / uScreen.y, 0.0, 1.0);\n"
    "  vUv = aUv; vColor = aColor; vColor2 = aColor2; vPos = aPos;\n"
    "  vMode = aMode; vParam = aParam; vOrigin = aOrigin;\n"
    "}\n";

static const char *const sFragmentSource =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "in vec4 vColor;\n"
    "in vec4 vColor2;\n"
    "in vec2 vPos;\n"
    "flat in uvec4 vMode;\n"
    "flat in ivec4 vParam;\n"
    "flat in ivec2 vOrigin;\n"
    "uniform sampler2DArray uAtlas;\n"
    "uniform usampler2D uFont;\n"
    "out vec4 fragColor;\n"
    "int imod(int a, int n) { int m = a % n; return m < 0 ? m + n : m; }\n"
    "int fdiv(int a, int n) { return (a - imod(a, n)) / n; }\n"
    "bool pattern(ivec2 p)\n"
    "{\n"
    "  uint kind = vMode.z;\n"
    "  int a = vParam.x, b = vParam.y, c = vParam.z, e = vParam.w;\n"
    "  if (kind == 1u) return imod(p.x + (int(vMode.w) - 128) * p.y + c, a) < b;\n"
    "  if (kind == 2u) return imod(p.y + c, a) < b;\n"
    "  if (kind == 3u) return imod(p.x + c, a) < b;\n"
    "  if (kind == 4u) {\n"
    "    int row = fdiv(p.y + e, a);\n"
    "    int sx = imod(p.x + c + imod(row, 2) * (vMode.w != 0u ? a / 2 : 0), a);\n"
    "    return sx < b && imod(p.y + e, a) < b;\n"
    "  }\n"
    "  if (kind == 5u) return imod(fdiv(p.x + b, a) + fdiv(p.y + b, a), 2) == 0;\n"
    "  if (kind == 6u) return imod(p.y, 16) > a;\n"
    "  return false;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "  uint mode = vMode.x;\n"
    "  if (mode == 0u) {\n"
    "    vec4 t = texelFetch(uAtlas, ivec3(ivec2(floor(vUv)), int(vMode.y)), 0);\n"
    "    if (t.a <= 0.0) discard;\n"
    "    fragColor = vec4(t.rgb, t.a * vColor.a);\n"
    "  } else if (mode == 1u) {\n"
    "    if (!pattern(ivec2(floor(vPos)) - vOrigin)) discard;\n"
    "    fragColor = vColor;\n"
    "  } else if (mode == 2u) {\n"
    "    uint idx = texelFetch(uFont, ivec2(floor(vUv)), 0).r;\n"
    "    if (idx == 1u) fragColor = vColor;\n"
    "    else if (idx == 2u && vColor2.a > 0.0) fragColor = vColor2;\n"
    "    else discard;\n"
    "  } else {\n"
    "    fragColor = vColor;\n"
    "  }\n"
    "}\n";

enum { MODE_PIECE, MODE_PATTERN, MODE_GLYPH, MODE_SOLID };

struct UiVertex
{
    int16_t x, y;
    uint16_t u, v;
    uint32_t color;
    uint32_t color2;
    uint8_t mode, layer, kind, dir;
    int16_t param[4];
    int16_t ox, oy;
};

#define MAX_QUADS 16384
#define ATLAS_SIZE 1024
#define ATLAS_LAYERS 8

/* ---------------------------------------------------------------- state */

static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLuint sIndexBuffer;
static GLuint sAtlas;
static GLuint sFontTexture;
static GLint sScreenLoc;
static GLint sOriginLoc;
static GLint sScaleLoc;
static GLint sAtlasLoc;
static GLint sFontLoc;

static struct UiVertex *sVertices;
static int sQuadCount;
static struct UiView sView;
static uint32_t sFrame;
static int sStatQuads;
static int sStatCalls;
static int sStatUploads;
static int sUploadsThisFrame;

/* draw segments: a clip each */
struct Segment
{
    int firstQuad;
    int clip[4]; /* canvas x, y, w, h; w < 0: none */
};
#define MAX_SEGMENTS 512
static struct Segment sSegments[MAX_SEGMENTS];
static int sSegmentCount;

/* the canvas-like state stack */
struct DrawState
{
    int tx;
    int ty;
    int clip[4];   /* canvas coordinates; w < 0: none */
    double alpha;
};
#define MAX_STATES 64
static struct DrawState sState;
static struct DrawState sStack[MAX_STATES];
static int sStackDepth;

/* mouse regions: the frame being drawn and the last finished one */
#define MAX_HITS 1024
static struct UiHit sHits[2][MAX_HITS];
static int sHitCount[2];
static int sHitWrite;

/* ---------------------------------------------------------------- atlas */

struct Shelf
{
    uint16_t y;
    uint16_t h;
    uint16_t x;
};

struct AtlasLayer
{
    struct Shelf shelves[128];
    int shelfCount;
    uint16_t nextY;
    uint32_t lastUsed;
    uint32_t generation;
};
static struct AtlasLayer sLayers[ATLAS_LAYERS];

struct PieceEntry
{
    char *key;
    uint32_t hash;
    uint32_t generation;  /* the layer generation it was placed in */
    struct UiPiece piece;
};
static struct PieceEntry *sPieces;
static uint32_t sPieceCapacity;
static uint32_t sPieceCount;

static void PieceTableClear(void)
{
    for (uint32_t i = 0; i < sPieceCapacity; i++)
        free(sPieces[i].key);
    free(sPieces);
    sPieces = NULL;
    sPieceCapacity = sPieceCount = 0;
}

static struct PieceEntry *PieceFind(const char *key, uint32_t hash)
{
    if (sPieceCapacity == 0)
        return NULL;
    for (uint32_t i = hash & (sPieceCapacity - 1);; i = (i + 1) & (sPieceCapacity - 1))
    {
        struct PieceEntry *e = &sPieces[i];

        if (e->key == NULL)
            return NULL;
        if (e->hash == hash && strcmp(e->key, key) == 0)
            return e;
    }
}

static struct PieceEntry *PieceSlot(uint32_t hash)
{
    for (uint32_t i = hash & (sPieceCapacity - 1);; i = (i + 1) & (sPieceCapacity - 1))
        if (sPieces[i].key == NULL)
            return &sPieces[i];
}

static void PieceGrow(void)
{
    struct PieceEntry *old = sPieces;
    uint32_t oldCapacity = sPieceCapacity;

    sPieceCapacity = sPieceCapacity == 0 ? 4096 : sPieceCapacity * 2;
    sPieces = calloc(sPieceCapacity, sizeof(*sPieces));
    for (uint32_t i = 0; i < oldCapacity; i++)
        if (old[i].key != NULL)
            *PieceSlot(old[i].hash) = old[i];
    free(old);
}

/* Entries of an evicted layer stay in the table but are stale (generation);
 * the table is rebuilt without them when it fills up. */
static void PieceCompact(void)
{
    struct PieceEntry *old = sPieces;
    uint32_t oldCapacity = sPieceCapacity;

    sPieces = calloc(sPieceCapacity, sizeof(*sPieces));
    sPieceCount = 0;
    for (uint32_t i = 0; i < oldCapacity; i++)
    {
        if (old[i].key == NULL)
            continue;
        if (old[i].generation != sLayers[old[i].piece.layer].generation)
        {
            free(old[i].key);
            continue;
        }
        *PieceSlot(old[i].hash) = old[i];
        sPieceCount++;
    }
    free(old);
}

static bool LayerPlace(int layer, int w, int h, uint16_t *outX, uint16_t *outY)
{
    struct AtlasLayer *L = &sLayers[layer];

    for (int i = 0; i < L->shelfCount; i++)
    {
        struct Shelf *s = &L->shelves[i];

        if (h <= s->h && h * 4 >= s->h * 3 - 8 && s->x + w <= ATLAS_SIZE)
        {
            *outX = s->x;
            *outY = s->y;
            s->x = (uint16_t)(s->x + w + 1);
            return true;
        }
    }
    if (L->shelfCount < 128 && L->nextY + h <= ATLAS_SIZE && w <= ATLAS_SIZE)
    {
        struct Shelf *s = &L->shelves[L->shelfCount++];

        s->y = L->nextY;
        s->h = (uint16_t)h;
        s->x = (uint16_t)(w + 1);
        L->nextY = (uint16_t)(L->nextY + h + 1);
        *outX = 0;
        *outY = s->y;
        return true;
    }
    return false;
}

static bool AtlasAllocate(int w, int h, struct UiPiece *piece)
{
    uint16_t x, y;
    int victim = -1;

    if (w > ATLAS_SIZE || h > ATLAS_SIZE)
        return false;
    for (int layer = 0; layer < ATLAS_LAYERS; layer++)
        if (LayerPlace(layer, w, h, &x, &y))
        {
            piece->layer = (uint16_t)layer;
            piece->x = x;
            piece->y = y;
            return true;
        }
    /* all full: empty the layer used longest ago (never one drawn this frame) */
    for (int layer = 0; layer < ATLAS_LAYERS; layer++)
        if (sLayers[layer].lastUsed != sFrame
         && (victim < 0 || sLayers[layer].lastUsed < sLayers[victim].lastUsed))
            victim = layer;
    if (victim < 0)
        return false;
    sLayers[victim].shelfCount = 0;
    sLayers[victim].nextY = 0;
    sLayers[victim].generation++;
    PieceCompact();
    if (!LayerPlace(victim, w, h, &x, &y))
        return false;
    piece->layer = (uint16_t)victim;
    piece->x = x;
    piece->y = y;
    return true;
}

/* ---------------------------------------------------------------- GL setup */

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint ok = 0;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];

        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("Native UI: shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool UiDraw_Init(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    uint16_t *indices;

    if (sReady)
        return true;
    sTexImage3D = (PfnTexImage3D)SDL_GL_GetProcAddress("glTexImage3D");
    sTexSubImage3D = (PfnTexSubImage3D)SDL_GL_GetProcAddress("glTexSubImage3D");
    sVertexAttribIPointer = (PfnVertexAttribIPointer)SDL_GL_GetProcAddress("glVertexAttribIPointer");
    if (sTexImage3D == NULL || sTexSubImage3D == NULL || sVertexAttribIPointer == NULL)
    {
        SDL_Log("Native UI: OpenGL 3.3 entry points missing");
        return false;
    }
    vs = Compile(GL_VERTEX_SHADER, sVertexSource);
    fs = Compile(GL_FRAGMENT_SHADER, sFragmentSource);
    if (vs == 0 || fs == 0)
        return false;
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vs);
    dglAttachShader(sProgram, fs);
    dglLinkProgram(sProgram);
    dglDeleteShader(vs);
    dglDeleteShader(fs);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];

        dglGetProgramInfoLog(sProgram, sizeof(log), NULL, log);
        SDL_Log("Native UI: program link failed: %s", log);
        return false;
    }
    sScreenLoc = dglGetUniformLocation(sProgram, "uScreen");
    sOriginLoc = dglGetUniformLocation(sProgram, "uOrigin");
    sScaleLoc = dglGetUniformLocation(sProgram, "uScale");
    sAtlasLoc = dglGetUniformLocation(sProgram, "uAtlas");
    sFontLoc = dglGetUniformLocation(sProgram, "uFont");

    dglGenVertexArrays(1, &sVertexArray);
    dglBindVertexArray(sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(struct UiVertex) * 4 * MAX_QUADS, NULL, GL_STREAM_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, x));
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, color));
    dglEnableVertexAttribArray(3);
    dglVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, color2));
    dglEnableVertexAttribArray(4);
    sVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, mode));
    dglEnableVertexAttribArray(5);
    sVertexAttribIPointer(5, 4, GL_SHORT, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, param));
    dglEnableVertexAttribArray(6);
    sVertexAttribIPointer(6, 2, GL_SHORT, sizeof(struct UiVertex), (void *)offsetof(struct UiVertex, ox));

    indices = malloc(sizeof(uint16_t) * 6 * MAX_QUADS);
    for (int q = 0; q < MAX_QUADS; q++)
    {
        uint16_t base = (uint16_t)(q * 4);

        indices[q * 6 + 0] = base;
        indices[q * 6 + 1] = (uint16_t)(base + 1);
        indices[q * 6 + 2] = (uint16_t)(base + 2);
        indices[q * 6 + 3] = (uint16_t)(base + 2);
        indices[q * 6 + 4] = (uint16_t)(base + 1);
        indices[q * 6 + 5] = (uint16_t)(base + 3);
    }
    dglGenBuffers(1, &sIndexBuffer);
    dglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIndexBuffer);
    dglBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * 6 * MAX_QUADS, indices, GL_STATIC_DRAW);
    free(indices);
    dglBindVertexArray(0);

    glGenTextures(1, &sAtlas);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sAtlas);
    sTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, ATLAS_LAYERS, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    sVertices = malloc(sizeof(struct UiVertex) * 4 * MAX_QUADS);
    memset(sLayers, 0, sizeof(sLayers));
    sReady = true;
    return true;
}

void UiDraw_SetFontTexture(const uint8_t *indices, int width, int height)
{
    if (!sReady)
        return;
    if (sFontTexture == 0)
        glGenTextures(1, &sFontTexture);
    glBindTexture(GL_TEXTURE_2D, sFontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, indices);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UiDraw_Shutdown(void)
{
    if (!sReady)
        return;
    if (sAtlas != 0)
        glDeleteTextures(1, &sAtlas);
    if (sFontTexture != 0)
        glDeleteTextures(1, &sFontTexture);
    dglDeleteBuffers(1, &sVertexBuffer);
    dglDeleteBuffers(1, &sIndexBuffer);
    dglDeleteVertexArrays(1, &sVertexArray);
    dglDeleteProgram(sProgram);
    free(sVertices);
    sVertices = NULL;
    PieceTableClear();
    sAtlas = sFontTexture = sVertexBuffer = sIndexBuffer = sVertexArray = sProgram = 0;
    sReady = false;
}

/* ---------------------------------------------------------------- view and frame */

void UiView_Compute(struct UiView *view, int outputWidth, int outputHeight, enum UiSize size)
{
    int sx = outputWidth / UI_LOGICAL_WIDTH;
    int sy = outputHeight / UI_LOGICAL_HEIGHT;
    int scale = sx < sy ? sx : sy;

    if (size == UI_SIZE_SMALL && scale > 2)
        scale--;
    if (scale < 1)
        scale = 1;
    view->outputWidth = outputWidth;
    view->outputHeight = outputHeight;
    view->scale = scale;
    view->canvasWidth = outputWidth / scale;
    view->canvasHeight = outputHeight / scale;
    view->originX = (outputWidth - view->canvasWidth * scale) / 2;
    view->originY = (outputHeight - view->canvasHeight * scale) / 2;
    /* floor halves, as the design's preview (a window smaller than 640x360 at x1) */
    view->contentX = (int)floor((view->canvasWidth - UI_LOGICAL_WIDTH) / 2.0);
    view->contentY = (int)floor((view->canvasHeight - UI_LOGICAL_HEIGHT) / 2.0);
}

static void NewSegment(void)
{
    struct Segment *s;

    if (sSegmentCount > 0 && sSegments[sSegmentCount - 1].firstQuad == sQuadCount)
        s = &sSegments[sSegmentCount - 1];
    else if (sSegmentCount < MAX_SEGMENTS)
        s = &sSegments[sSegmentCount++];
    else
        return;
    s->firstQuad = sQuadCount;
    memcpy(s->clip, sState.clip, sizeof(s->clip));
}

void UiDraw_Begin(int outputWidth, int outputHeight, enum UiSize size)
{
    sFrame++;
    UiView_Compute(&sView, outputWidth, outputHeight, size);
    sQuadCount = 0;
    sSegmentCount = 0;
    sStackDepth = 0;
    sUploadsThisFrame = 0;
    sState.tx = sView.contentX;
    sState.ty = sView.contentY;
    sState.clip[0] = sState.clip[1] = 0;
    sState.clip[2] = -1;
    sState.clip[3] = -1;
    sState.alpha = 1.0;
    sHitWrite = 0;
    sHitCount[0] = 0;
    NewSegment();
}

const struct UiView *UiDraw_View(void)
{
    return &sView;
}

void UiDraw_Stats(int *quads, int *drawCalls, int *uploads)
{
    if (quads != NULL)
        *quads = sStatQuads;
    if (drawCalls != NULL)
        *drawCalls = sStatCalls;
    if (uploads != NULL)
        *uploads = sStatUploads;
}

void UiDraw_End(void)
{
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLint blendSrcRgb, blendDstRgb, blendSrcA, blendDstA;
    GLint program = 0;
    GLint activeTexture = 0;
    int calls = 0;

    /* the frame's mouse regions become the current ones */
    memcpy(sHits[1], sHits[0], sizeof(struct UiHit) * (size_t)sHitCount[0]);
    sHitCount[1] = sHitCount[0];

    sStatQuads = sQuadCount;
    sStatUploads = sUploadsThisFrame;
    if (!sReady || sQuadCount == 0)
    {
        sStatCalls = 0;
        UiKit_EndFrame();
        return;
    }
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);

    glViewport(0, 0, sView.outputWidth, sView.outputHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    dglUseProgram(sProgram);
    dglUniform2f(sScreenLoc, (float)sView.outputWidth, (float)sView.outputHeight);
    dglUniform2f(sOriginLoc, (float)sView.originX, (float)sView.originY);
    dglUniform1f(sScaleLoc, (float)sView.scale);
    dglUniform1i(sAtlasLoc, 0);
    dglUniform1i(sFontLoc, 1);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sAtlas);
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sFontTexture);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(struct UiVertex) * 4 * MAX_QUADS, NULL, GL_STREAM_DRAW);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(struct UiVertex) * 4 * (size_t)sQuadCount, sVertices, GL_STREAM_DRAW);

    for (int i = 0; i < sSegmentCount; i++)
    {
        const struct Segment *s = &sSegments[i];
        int end = i + 1 < sSegmentCount ? sSegments[i + 1].firstQuad : sQuadCount;
        int count = end - s->firstQuad;

        if (count <= 0)
            continue;
        if (s->clip[2] >= 0)
        {
            int x = sView.originX + s->clip[0] * sView.scale;
            int y = sView.originY + s->clip[1] * sView.scale;
            int w = s->clip[2] * sView.scale;
            int h = s->clip[3] * sView.scale;

            if (w <= 0 || h <= 0)
                continue;
            glEnable(GL_SCISSOR_TEST);
            glScissor(x, sView.outputHeight - (y + h), w, h);
        }
        else
            glDisable(GL_SCISSOR_TEST);
        glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_SHORT, (void *)(sizeof(uint16_t) * 6 * (size_t)s->firstQuad));
        calls++;
    }
    sStatCalls = calls;

    dglBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    dglActiveTexture((GLenum)activeTexture);
    dglUseProgram((GLuint)program);
    glBlendFunc((GLenum)blendSrcRgb, (GLenum)blendDstRgb);
    (void)blendSrcA;
    (void)blendDstA;
    if (!blend)
        glDisable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
    if (stencil)
        glEnable(GL_STENCIL_TEST);
    UiKit_EndFrame();
}

/* ---------------------------------------------------------------- state */

void UiG_Save(void)
{
    if (sStackDepth < MAX_STATES)
        sStack[sStackDepth++] = sState;
}

void UiG_Restore(void)
{
    bool clipChanged;

    if (sStackDepth == 0)
        return;
    clipChanged = memcmp(sState.clip, sStack[sStackDepth - 1].clip, sizeof(sState.clip)) != 0;
    sState = sStack[--sStackDepth];
    if (clipChanged)
        NewSegment();
}

void UiG_Translate(int x, int y)
{
    sState.tx += x;
    sState.ty += y;
}

void UiG_Clip(int x, int y, int w, int h)
{
    int x0 = x + sState.tx;
    int y0 = y + sState.ty;
    int x1 = x0 + w;
    int y1 = y0 + h;

    if (sState.clip[2] >= 0)
    {
        int cx1 = sState.clip[0] + sState.clip[2];
        int cy1 = sState.clip[1] + sState.clip[3];

        if (x0 < sState.clip[0]) x0 = sState.clip[0];
        if (y0 < sState.clip[1]) y0 = sState.clip[1];
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }
    sState.clip[0] = x0;
    sState.clip[1] = y0;
    sState.clip[2] = x1 > x0 ? x1 - x0 : 0;
    sState.clip[3] = y1 > y0 ? y1 - y0 : 0;
    NewSegment();
}

void UiG_Alpha(double alpha)
{
    sState.alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
}

double UiG_GetAlpha(void)
{
    return sState.alpha;
}

void UiG_Bleed(int *x0, int *y0, int *w, int *h)
{
    *x0 = -sState.tx;
    *y0 = -sState.ty;
    *w = sView.canvasWidth;
    *h = sView.canvasHeight;
}

/* ---------------------------------------------------------------- quads */

static uint32_t WithGlobalAlpha(UiRGBA c)
{
    int a = UI_A(c);

    if (sState.alpha < 1.0)
        a = (int)(a * sState.alpha + 0.5);
    return UiWithAlpha(c, a);
}

static struct UiVertex *NewQuad(void)
{
    struct UiVertex *v;

    if (sVertices == NULL || sQuadCount >= MAX_QUADS)
        return NULL;
    v = &sVertices[sQuadCount * 4];
    sQuadCount++;
    memset(v, 0, sizeof(*v) * 4);
    return v;
}

/* positions: canvas rectangle; uvs: texel rectangle (u0, v0)-(u1, v1) */
static void SetQuad(struct UiVertex *v, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1)
{
    v[0].x = (int16_t)x0; v[0].y = (int16_t)y0; v[0].u = (uint16_t)u0; v[0].v = (uint16_t)v0;
    v[1].x = (int16_t)x1; v[1].y = (int16_t)y0; v[1].u = (uint16_t)u1; v[1].v = (uint16_t)v0;
    v[2].x = (int16_t)x0; v[2].y = (int16_t)y1; v[2].u = (uint16_t)u0; v[2].v = (uint16_t)v1;
    v[3].x = (int16_t)x1; v[3].y = (int16_t)y1; v[3].u = (uint16_t)u1; v[3].v = (uint16_t)v1;
}

static void SetAll(struct UiVertex *v, uint8_t mode, uint8_t layer, uint32_t color, uint32_t color2)
{
    for (int i = 0; i < 4; i++)
    {
        v[i].mode = mode;
        v[i].layer = layer;
        v[i].color = color;
        v[i].color2 = color2;
    }
}

void UiG_Rect(int x, int y, int w, int h, UiRGBA c)
{
    struct UiVertex *v;

    if (w <= 0 || h <= 0 || UI_A(c) == 0)
        return;
    v = NewQuad();
    if (v == NULL)
        return;
    x += sState.tx;
    y += sState.ty;
    SetQuad(v, x, y, x + w, y + h, 0, 0, 0, 0);
    SetAll(v, MODE_SOLID, 0, WithGlobalAlpha(c), 0);
}

int UiG_Mark(void)
{
    return sQuadCount;
}

void UiG_SquashX(int mark, int cx, double k)
{
    if (sVertices == NULL || mark < 0)
        return;
    cx += sState.tx;
    for (int q = mark; q < sQuadCount; q++)
        for (int i = 0; i < 4; i++)
        {
            struct UiVertex *v = &sVertices[q * 4 + i];

            v->x = (int16_t)(cx + (int)floor((v->x - cx) * k + 0.5));
        }
}

void UiG_Put(int x, int y, UiRGBA c) { UiG_Rect(x, y, 1, 1, c); }
void UiG_HLine(int x, int y, int w, UiRGBA c) { UiG_Rect(x, y, w, 1, c); }
void UiG_VLine(int x, int y, int h, UiRGBA c) { UiG_Rect(x, y, 1, h, c); }

void UiG_Veil(UiRGBA c)
{
    int x0, y0, w, h;

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, c);
}

const struct UiPiece *UiG_Cached(const char *key, int w, int h, UiBuildFn fn, const void *ctx)
{
    uint32_t hash = UiHash(key);
    struct PieceEntry *e;
    struct UiBitmap bm;
    struct UiPiece piece;

    if (!sReady || w <= 0 || h <= 0)
        return NULL;
    e = PieceFind(key, hash);
    if (e != NULL && e->generation == sLayers[e->piece.layer].generation)
    {
        sLayers[e->piece.layer].lastUsed = sFrame;
        return &e->piece;
    }
    if (!AtlasAllocate(w, h, &piece))
        return NULL;
    piece.w = (uint16_t)w;
    piece.h = (uint16_t)h;
    UiBitmap_Init(&bm, w, h);
    fn(&bm, ctx);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sAtlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    sTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, piece.x, piece.y, piece.layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, bm.d);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    UiBitmap_Free(&bm);
    sUploadsThisFrame++;
    sLayers[piece.layer].lastUsed = sFrame;

    e = PieceFind(key, hash); /* a stale entry under the same key is reused */
    if (e == NULL)
    {
        if ((sPieceCount + 1) * 2 > sPieceCapacity)
            PieceGrow();
        e = PieceSlot(hash);
        e->key = strdup(key);
        e->hash = hash;
        sPieceCount++;
    }
    e->generation = sLayers[piece.layer].generation;
    e->piece = piece;
    return &e->piece;
}

void UiG_BlitPart(const struct UiPiece *piece, int sx, int sy, int sw, int sh, int dx, int dy,
                  int scale, bool flip, double alpha)
{
    struct UiVertex *v;
    int u0, u1;

    if (piece == NULL || sw <= 0 || sh <= 0)
        return;
    v = NewQuad();
    if (v == NULL)
        return;
    if (scale < 1)
        scale = 1;
    dx += sState.tx;
    dy += sState.ty;
    u0 = piece->x + sx;
    u1 = piece->x + sx + sw;
    if (flip)
    {
        int t = u0;

        u0 = u1;
        u1 = t;
    }
    SetQuad(v, dx, dy, dx + sw * scale, dy + sh * scale, u0, piece->y + sy, u1, piece->y + sy + sh);
    SetAll(v, MODE_PIECE, (uint8_t)piece->layer, WithGlobalAlpha(UI_RGBA(255, 255, 255, (int)(255 * alpha + 0.5))), 0);
}

void UiG_Blit(const struct UiPiece *piece, int x, int y)
{
    if (piece != NULL)
        UiG_BlitPart(piece, 0, 0, piece->w, piece->h, x, y, 1, false, 1.0);
}

struct MaskBuild
{
    const struct UiMask *m;
    UiRGBA c;
    UiRGBA body;
    const struct UiPaint *o;
};

static void BuildMask(struct UiBitmap *bm, const void *ctx)
{
    const struct MaskBuild *b = ctx;

    UiBitmap_FillMask(bm, b->m, 0, 0, b->c);
}

static void BuildPaint(struct UiBitmap *bm, const void *ctx)
{
    const struct MaskBuild *b = ctx;

    UiBitmap_Paint(bm, b->m, 0, 0, b->body, b->o);
}

/* A piece larger than the atlas (ATLAS_SIZE) in tiles that fit: built whole, each tile cached under its own key and
 * drawn at its place. At a window too low for x2 (under 720 rows) the interface is drawn at x1 and its canvas is the
 * window's own width, so a shape across the window - the battle's message plate - was wider than the atlas: it was
 * never drawn (the battle text stood on the 3D scene with no plate). */
struct TileBuild
{
    int w, h, sx, sy;
    UiBuildFn fn;
    const void *ctx;
};

static void BuildTile(struct UiBitmap *bm, const void *ctx)
{
    const struct TileBuild *t = ctx;
    struct UiBitmap full;

    UiBitmap_Init(&full, t->w, t->h);
    t->fn(&full, t->ctx);
    for (int y = 0; y < bm->h; y++)
        memcpy(&bm->d[(size_t)y * bm->w * 4], &full.d[((size_t)(t->sy + y) * t->w + t->sx) * 4], (size_t)bm->w * 4);
    UiBitmap_Free(&full);
}

static void BlitCached(const char *key, int w, int h, UiBuildFn fn, const void *ctx, int x, int y)
{
    char tileKey[2100];

    if (w <= ATLAS_SIZE && h <= ATLAS_SIZE)
    {
        UiG_Blit(UiG_Cached(key, w, h, fn, ctx), x, y);
        return;
    }
    for (int ty = 0; ty < h; ty += ATLAS_SIZE)
        for (int tx = 0; tx < w; tx += ATLAS_SIZE)
        {
            struct TileBuild t = {w, h, tx, ty, fn, ctx};
            int tw = w - tx < ATLAS_SIZE ? w - tx : ATLAS_SIZE;
            int th = h - ty < ATLAS_SIZE ? h - ty : ATLAS_SIZE;

            snprintf(tileKey, sizeof(tileKey), "%s#%d,%d", key, tx, ty);
            UiG_Blit(UiG_Cached(tileKey, tw, th, BuildTile, &t), x + tx, y + ty);
        }
}

void UiG_Mask(const struct UiMask *m, int x, int y, UiRGBA c)
{
    char key[2048];
    struct MaskBuild b = {m, c, 0, NULL};

    if (m == NULL || UI_A(c) == 0)
        return;
    snprintf(key, sizeof(key), "m%s|%08x", m->key, c);
    BlitCached(key, m->w, m->h, BuildMask, &b, x, y);
}

void UiG_Paint(const struct UiMask *m, int x, int y, UiRGBA body, const struct UiPaint *o)
{
    char key[2048];
    struct MaskBuild b = {m, 0, body, o};

    if (m == NULL)
        return;
    key[0] = 'p';
    UiPaintKey(key + 1, sizeof(key) - 1, m, body, o);
    BlitCached(key, m->w, m->h, BuildPaint, &b, x, y);
}

const struct UiMask *UiG_Panel4(int x, int y, int w, int h, const struct UiRamp *r,
                                struct UiCorner tl, struct UiCorner tr, struct UiCorner bl,
                                struct UiCorner br, const struct UiPanelOpt *o)
{
    static const struct UiPanelOpt sNone = {0};
    const struct UiMask *m;
    struct UiPaint paint = {0};

    if (o == NULL)
        o = &sNone;
    m = o->shape != NULL ? o->shape : UiM_Rounded(w, h, tl, tr, bl, br);
    paint.ink = o->noInk ? UI_NONE : r->ink;
    paint.hi = o->noHi ? UI_NONE : r->hi;
    paint.lo = o->noLo ? UI_NONE : r->lo;
    paint.hiW = o->hiW;
    paint.loW = o->loW;
    paint.pattern = o->pattern;
    paint.patternColor = o->patternColor;
    UiG_Paint(m, x, y, r->base, &paint);
    return m;
}

const struct UiMask *UiG_Panel(int x, int y, int w, int h, const struct UiRamp *r, int corner,
                               const struct UiPanelOpt *o)
{
    struct UiCorner c = UiCornerR(corner);

    return UiG_Panel4(x, y, w, h, r, c, c, c, c, o);
}

void UiG_FillPattern(const struct UiPattern *p, int x, int y, int w, int h, UiRGBA c, int px, int py)
{
    struct UiVertex *v;

    if (w <= 0 || h <= 0 || UI_A(c) == 0 || p == NULL)
        return;
    v = NewQuad();
    if (v == NULL)
        return;
    SetQuad(v, x + sState.tx, y + sState.ty, x + sState.tx + w, y + sState.ty + h, 0, 0, 0, 0);
    SetAll(v, MODE_PATTERN, 0, WithGlobalAlpha(c), 0);
    for (int i = 0; i < 4; i++)
    {
        v[i].kind = p->kind;
        v[i].ox = (int16_t)(sState.tx + px);
        v[i].oy = (int16_t)(sState.ty + py);
        switch (p->kind)
        {
        case UI_PAT_DIAG:
            v[i].param[0] = p->a; v[i].param[1] = p->b; v[i].param[2] = p->c;
            v[i].dir = (uint8_t)(p->d + 128);
            break;
        case UI_PAT_HSTRIPE:
        case UI_PAT_VSTRIPE:
            v[i].param[0] = p->a; v[i].param[1] = p->b; v[i].param[2] = p->c;
            break;
        case UI_PAT_DOTS:
            v[i].param[0] = p->a; v[i].param[1] = p->b; v[i].param[2] = p->c; v[i].param[3] = p->d;
            v[i].dir = (uint8_t)(p->e != 0);
            break;
        case UI_PAT_CHECKER:
            v[i].param[0] = p->a; v[i].param[1] = p->b;
            break;
        case UI_PAT_BELOW:
            v[i].param[0] = p->a;
            break;
        }
    }
}

void UiG_GlyphQuad(int x, int y, int font, int code, int w, int h, UiRGBA fg, UiRGBA sh)
{
    struct UiVertex *v;
    int u0;
    int v0;

    if (w <= 0 || h <= 0)
        return;
    v = NewQuad();
    if (v == NULL)
        return;
    UiText_GlyphOrigin(font, code, &u0, &v0);
    x += sState.tx;
    y += sState.ty;
    SetQuad(v, x, y, x + w, y + h, u0, v0, u0 + w, v0 + h);
    SetAll(v, MODE_GLYPH, 0, WithGlobalAlpha(fg), UI_A(sh) > 0 ? WithGlobalAlpha(sh) : 0);
}

/* ---------------------------------------------------------------- mouse regions */

void UiG_Hit(int id, int index, int x, int y, int w, int h)
{
    struct UiHit *hit;

    if (sHitCount[0] >= MAX_HITS)
        return;
    hit = &sHits[0][sHitCount[0]++];
    hit->id = id;
    hit->index = index;
    hit->x = x + sState.tx;
    hit->y = y + sState.ty;
    hit->w = w;
    hit->h = h;
    /* clipped regions only count where they are visible */
    if (sState.clip[2] >= 0)
    {
        int x1 = hit->x + hit->w, y1 = hit->y + hit->h;
        int cx1 = sState.clip[0] + sState.clip[2], cy1 = sState.clip[1] + sState.clip[3];

        if (hit->x < sState.clip[0]) hit->x = sState.clip[0];
        if (hit->y < sState.clip[1]) hit->y = sState.clip[1];
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        hit->w = x1 - hit->x;
        hit->h = y1 - hit->y;
        if (hit->w <= 0 || hit->h <= 0)
            sHitCount[0]--;
    }
}

const struct UiHit *UiDraw_HitAt(int canvasX, int canvasY)
{
    for (int i = sHitCount[1] - 1; i >= 0; i--)
    {
        const struct UiHit *h = &sHits[1][i];

        if (canvasX >= h->x && canvasY >= h->y && canvasX < h->x + h->w && canvasY < h->y + h->h)
            return h;
    }
    return NULL;
}

void UiDraw_WindowToCanvas(int px, int py, int *cx, int *cy)
{
    int s = sView.scale > 0 ? sView.scale : 1;
    int dx = px - sView.originX;
    int dy = py - sView.originY;

    *cx = dx >= 0 ? dx / s : -((-dx + s - 1) / s);
    *cy = dy >= 0 ? dy / s : -((-dy + s - 1) / s);
}

void UiDraw_CanvasToContent(int cx, int cy, int *x, int *y)
{
    *x = cx - sView.contentX;
    *y = cy - sView.contentY;
}
#endif // ENABLE_NATIVE_UI
