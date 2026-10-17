#ifdef ENABLE_DIORAMA
/*
 * The world detail objects pass (see diorama/gl_props.h). Project-owned
 * (graphics overhaul, phase 2).
 */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "fieldmap.h"
#include "diorama/buildings.h"
#include "diorama/gl_grade.h"
#include "diorama/gl_lamps.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_props.h"
#include "diorama/gl_quality.h"
#include "diorama/gl_shadow.h"
#include "diorama/metatile_atlas.h"
#include "diorama/props.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"

extern const struct MapLayout *const gMapLayouts[];

/* The art texture: ENTRY_COLUMNS x ENTRY_ROWS pieces of 16 x 16 pixels, each
 * with a one-pixel border repeating its edge (no bleeding between them). */
#define ENTRY_COLUMNS 16
#define ENTRY_ROWS 16
#define MAX_ENTRIES (ENTRY_COLUMNS * ENTRY_ROWS)
#define ENTRY_STRIDE 18
#define ART_WIDTH (ENTRY_COLUMNS * ENTRY_STRIDE)
#define ART_HEIGHT (ENTRY_ROWS * ENTRY_STRIDE)
#define ART_SIZE_GLSL "288.0"
#define NO_ENTRY 0xFF
/* A lamp's glass pixels (bright as drawn) carry this alpha: they glow at
 * night whatever the tint has made of their colour. */
#define GLASS_ALPHA 200
#define GLASS_BRIGHTNESS 0xA6
#define MAX_INSTANCES (DIORAMA_MAX_VISIBLE_CELLS * 2)
/* The shaders' spelling of the texture's size. */
typedef char ArtSizeCheck[(ART_WIDTH == 288 && ART_HEIGHT == 288) ? 1 : -1];

/* Face shades, as the voxel trees and the distant scenery have them: the art's
 * own face (towards the camera, or up on a block) as drawn, the sides a little
 * darker, those facing the camera least. A slab standing, then lying as a
 * block's top. */
#define SHADE_FRONT 1.0f
#define SHADE_BACK 0.72f
#define SHADE_EAST 0.86f
#define SHADE_WEST 0.78f
#define SHADE_TOP 1.0f
#define SHADE_BOTTOM 0.6f
#define SHADE_FLAT_TOP 1.0f
#define SHADE_FLAT_BOTTOM 0.5f
#define SHADE_FLAT_NORTH 0.72f
#define SHADE_FLAT_SOUTH 0.92f

struct MeshVertex
{
    float x, y, z;      /* across the art (0 to 1), up it (0 to 1), through it (-0.5 to 0.5) */
    float s, t;         /* the art's pixel: across (0 to 1), down (0 to 1) */
    float shade, shadeFlat; /* standing; lying (a block) */
};

struct GLInstance
{
    float x, y, z, yaw;
    float width, height, depth, lie; /* lie: a block, the art its top (1 + its north edge's rise) */
    float u, v, rowTop, rowBottom;   /* the art piece's corner in the texture; the rows shown (0 to 1) */
    float sway, phase, light, rustle;
};

/* 2 faces, then for each of the 17 column and 17 row boundaries the sides of
 * the pixels on either side of it: 2 + 32 + 32 quads. */
#define SLAB_QUADS (2 + 2 * 16 + 2 * 16)
#define CARD_QUADS 2

static bool sReady;
static GLuint sProgram;
static GLuint sSlabArray, sCardArray, sMoundArray;
static GLuint sSlabBuffer, sCardBuffer, sMoundBuffer, sInstanceBuffer;
static GLuint sArtTexture;
static GLint sCameraLocation, sCameraPitchLocation, sFocalLengthLocation, sAspectCorrectionLocation;
static GLint sCameraHeightLocation, sCameraDistanceLocation, sScreenOffsetLocation;
static GLint sArtLocation, sTimeLocation, sShadowPassLocation, sShadowEnabledLocation;
static bool sStill;
static GLint sLightMatrixLocation, sShadowMapLocation, sShadowAmbientLocation, sSaturationLocation;
static struct DioramaLightUniforms sLightUniforms;
static struct DioramaLampUniforms sLampUniforms;
static uint32_t sSlabVertexCount, sCardVertexCount;

static struct DioramaPropInstance sProps[MAX_INSTANCES];
static struct GLInstance sInstances[MAX_INSTANCES];
/* Slabs first, then mounds (both cast shadows), then cards. */
static unsigned sSlabCount, sMoundCount, sCardCount;

/* Mounds (props.h DIORAMA_PROP_MOUND): a mesh for each art piece, made from
 * its pixels - each opaque one a column, the taller the further it is from
 * the art's edge - the pieces' meshes one after the other in sMoundBuffer, and
 * the mound instances in runs of one piece each. */
#define MAX_MOUND_GROUPS 64
struct MoundGroup
{
    uint8_t entry;
    uint32_t firstInstance;
    uint32_t instanceCount;
};
static struct MoundGroup sMoundGroups[MAX_MOUND_GROUPS];
static unsigned sMoundGroupCount;
static uint32_t sMoundFirst[MAX_ENTRIES];
static uint32_t sMoundVertexCount[MAX_ENTRIES];
static uint64_t sMoundMaskHash[MAX_ENTRIES]; /* the pixels the mesh was made from; 0: none */
static struct MeshVertex *sMoundVertices;
static uint32_t sMoundCapacity;
static uint32_t sMoundGeneration;
static uint64_t sSequence;
static uint32_t sGeometryHash, sGeometryGeneration;
/* What the instance buffer holds (DioramaGLProps_Sync uploads only a change). */
static uint32_t sUploadedHash;
static unsigned sUploadedCount = 0xFFFFFFFFu;

/* The art pieces: which atlas slot each holds (the prop's processing comes
 * with the slot: a slot is always the same metatile of the same tileset), and
 * what they were composed from. */
static uint16_t sEntrySlot[MAX_ENTRIES];
static const struct DioramaProp *sEntryProp[MAX_ENTRIES];
static uint8_t sSlotEntry[DIORAMA_ATLAS_SLOT_COUNT];
static unsigned sEntryCount;
static bool sEntryFull;
static uint32_t sArtMapGeneration, sArtPaletteGeneration, sArtAnimationGeneration, sArtPageSignature;
static uint32_t sArtPixels[ART_WIDTH * ART_HEIGHT];

static const char sVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 art;\n"
    "layout(location = 2) in vec2 vertexShade;\n"     /* standing, lying */
    "layout(location = 3) in vec4 instancePlace;\n"   /* x, y, z, yaw */
    "layout(location = 4) in vec4 instanceSize;\n"    /* width, height, depth, lying (1 + rise) */
    "layout(location = 5) in vec4 instanceArt;\n"     /* u, v, first row, end row */
    "layout(location = 6) in vec4 instanceMotion;\n"  /* sway, phase, light, rustle */
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "uniform float cameraHeight;\n"
    "uniform float cameraDistance;\n"
    "uniform float screenOffsetY;\n"
    "uniform int shadowPass;\n"
    "uniform float time;\n"
    "uniform mat4 lightViewProjection;\n"
    "out vec2 artUv;\n"
    "out float artRow;\n"
    "flat out vec2 rows;\n"
    "out float shade;\n"
    "out vec3 sceneWorldPosition;\n"
    "out float viewDepth;\n"
    "flat out float glow;\n"
    "void main() {\n"
    /* Standing, the last row shown on the ground; lying (a block), the art
     * on top, its first row to the north. */
    "  float drop = (1.0 - instanceArt.w) * instanceSize.y;\n"
    "  vec3 local = instanceSize.w > 0.5\n"
    "      ? vec3((position.x - 0.5) * instanceSize.x,\n"
    "             (0.5 - position.z) * instanceSize.y + position.y * (instanceSize.w - 1.0),\n"
    "             (position.y - 0.5) * instanceSize.z)\n"
    "      : vec3((position.x - 0.5) * instanceSize.x, position.y * instanceSize.y - drop,\n"
    "             position.z * instanceSize.z);\n"
    "  float s = sin(instancePlace.w);\n"
    "  float c = cos(instancePlace.w);\n"
    "  vec3 turned = vec3(local.x * c - local.z * s, local.y, local.x * s + local.z * c);\n"
    /* The top sways, the foot stays (not in the sun's depth map: it is kept). */
    "  float shown = max((instanceArt.w - instanceArt.z) * instanceSize.y, 0.01);\n"
    "  float up = clamp(local.y / shown, 0.0, 1.0);\n"
    "  if (shadowPass == 0 && instanceMotion.x > 0.0) {\n"
    "    float wind = sin(time * 1.9 + instanceMotion.y) + 0.35 * sin(time * 3.7 + instanceMotion.y * 2.0);\n"
    "    turned.x += instanceMotion.x * wind * up * up;\n"
    /* Walked through: a quick shiver, the blades pushed apart. */
    "    turned.x += instanceMotion.w * 0.06 * sin(time * 17.0 + instanceMotion.y * 3.0) * up;\n"
    "    turned.z += instanceMotion.w * 0.04 * sin(time * 13.0 + instanceMotion.y) * up;\n"
    "  }\n"
    "  vec3 worldPosition = instancePlace.xyz + turned;\n"
    /* The terrain's projection (gl_terrain_renderer.c), so the two meet. */
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = " DIORAMA_FAR_DEPTH_GLSL ";\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float relativeX = worldPosition.x - cameraPosition.x;\n"
    "  float relativeZ = worldPosition.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - worldPosition.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - worldPosition.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0 * aspectCorrection;\n"
    "  float clipY = screenOffsetY * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "  if (shadowPass == 1) gl_Position = lightViewProjection * vec4(worldPosition, 1.0);\n"
    "  viewDepth = depth;\n"
    "  sceneWorldPosition = worldPosition;\n"
    "  artUv = instanceArt.xy + art * vec2(16.0 / " ART_SIZE_GLSL ", 16.0 / " ART_SIZE_GLSL ");\n"
    "  artRow = art.y;\n"
    "  rows = instanceArt.zw;\n"
    "  shade = instanceSize.w > 0.5 ? vertexShade.y : vertexShade.x;\n"
    "  glow = instanceMotion.z;\n"
    "}\n";

static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 artUv;\n"
    "in float artRow;\n"
    "flat in vec2 rows;\n"
    "in float shade;\n"
    "in vec3 sceneWorldPosition;\n"
    "in float viewDepth;\n"
    "flat in float glow;\n"
    "out vec4 color;\n"
    "uniform sampler2D artTexture;\n"
    "uniform int shadowPass;\n"
    "uniform int shadowEnabled;\n"
    "uniform mat4 lightViewProjection;\n"
    "uniform sampler2DShadow shadowMap;\n"
    "uniform float shadowAmbient;\n"
    DIORAMA_GLSL_GRADE
    "float SunLit() {\n" /* 1 in the sun, 0 in a cast shadow */
    "  if (shadowEnabled == 0) return 1.0;\n"
    "  vec4 lightPosition = lightViewProjection * vec4(sceneWorldPosition, 1.0);\n"
    "  vec3 p = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;\n"
    "  if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
    "  return texture(shadowMap, vec3(p.xy, p.z - 0.0015));\n"
    "}\n"
    DIORAMA_GLSL_LIGHT
    DIORAMA_GLSL_LAMPS
    "void main() {\n"
    "  if (artRow < rows.x || artRow >= rows.y) discard;\n"
    "  vec4 texel = texture(artTexture, artUv);\n"
    "  if (texel.a < 0.5) discard;\n" /* no colour, no depth */
    "  if (shadowPass == 1) { color = vec4(1.0); return; }\n"
    "  color = vec4(Grade(texel.rgb) * Light(shade * mix(shadowAmbient, 1.0, SunLit()))\n"
    "             * Dapple(sceneWorldPosition), 1.0);\n"
    "  color.rgb += Grade(texel.rgb) * Lamps(sceneWorldPosition);\n"
    /* A lamp's glass glows at night: its light pixels warm and bright. */
    "  if (glow > 0.0) {\n"
    "    float lit = texel.a < 0.95 ? 1.0 : smoothstep(0.30, 0.65, max(texel.r, max(texel.g, texel.b)));\n"
    "    color.rgb = mix(color.rgb, vec3(1.0, 0.86, 0.55) * (0.75 + 0.35 * lit), glow * lit);\n"
    "  }\n"
    "  color.rgb = Haze(color.rgb, viewDepth, sceneWorldPosition);\n"
    "}\n";

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
        SDL_Log("Diorama prop shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sVertexShader);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sFragmentShader);
    GLint linked;
    bool lightLocated;

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
        SDL_Log("Diorama prop shader link failed: %s", log);
        return false;
    }
    sCameraLocation = dglGetUniformLocation(sProgram, "cameraPosition");
    sCameraPitchLocation = dglGetUniformLocation(sProgram, "cameraPitch");
    sFocalLengthLocation = dglGetUniformLocation(sProgram, "focalLength");
    sAspectCorrectionLocation = dglGetUniformLocation(sProgram, "aspectCorrection");
    sCameraHeightLocation = dglGetUniformLocation(sProgram, "cameraHeight");
    sCameraDistanceLocation = dglGetUniformLocation(sProgram, "cameraDistance");
    sScreenOffsetLocation = dglGetUniformLocation(sProgram, "screenOffsetY");
    sArtLocation = dglGetUniformLocation(sProgram, "artTexture");
    sTimeLocation = dglGetUniformLocation(sProgram, "time");
    sShadowPassLocation = dglGetUniformLocation(sProgram, "shadowPass");
    sShadowEnabledLocation = dglGetUniformLocation(sProgram, "shadowEnabled");
    sLightMatrixLocation = dglGetUniformLocation(sProgram, "lightViewProjection");
    sShadowMapLocation = dglGetUniformLocation(sProgram, "shadowMap");
    sShadowAmbientLocation = dglGetUniformLocation(sProgram, "shadowAmbient");
    sSaturationLocation = dglGetUniformLocation(sProgram, "saturation");
    lightLocated = DioramaGLLight_Locate(sProgram, &sLightUniforms);
    DioramaGLLamps_Locate(sProgram, &sLampUniforms);
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0 && sFocalLengthLocation >= 0
        && sAspectCorrectionLocation >= 0 && sArtLocation >= 0 && sTimeLocation >= 0
        && sShadowPassLocation >= 0 && sShadowEnabledLocation >= 0 && sLightMatrixLocation >= 0
        && sShadowMapLocation >= 0 && sShadowAmbientLocation >= 0 && sSaturationLocation >= 0
        && lightLocated;
}

/* A quad from four corners (counter-clockwise or not: nothing is culled),
 * each with its art coordinates. */
static struct MeshVertex *Quad(struct MeshVertex *out, const float corners[4][5], float shade,
                               float shadeFlat)
{
    static const unsigned order[6] = {0, 1, 2, 0, 2, 3};
    unsigned i;

    for (i = 0; i < 6; i++)
    {
        const float *corner = corners[order[i]];

        out[i].x = corner[0];
        out[i].y = corner[1];
        out[i].z = corner[2];
        out[i].s = corner[3];
        out[i].t = corner[4];
        out[i].shade = shade;
        out[i].shadeFlat = shadeFlat;
    }
    return out + 6;
}

/* The faces: the art at the front (south, z -0.5) and back, as drawn (t 0 at
 * the top row, y 1). */
static struct MeshVertex *Faces(struct MeshVertex *out)
{
    const float front[4][5] = {
        {0, 0, -0.5f, 0, 1}, {1, 0, -0.5f, 1, 1}, {1, 1, -0.5f, 1, 0}, {0, 1, -0.5f, 0, 0},
    };
    const float back[4][5] = {
        {0, 0, 0.5f, 0, 1}, {1, 0, 0.5f, 1, 1}, {1, 1, 0.5f, 1, 0}, {0, 1, 0.5f, 0, 0},
    };

    out = Quad(out, front, SHADE_FRONT, SHADE_FLAT_TOP);
    return Quad(out, back, SHADE_BACK, SHADE_FLAT_BOTTOM);
}

static uint32_t BuildSlab(struct MeshVertex *vertices)
{
    struct MeshVertex *out = Faces(vertices);
    int i;

    /* Column boundary i: the west side of column i, the east side of column
     * i - 1, each cut where its own pixel is transparent. */
    for (i = 0; i <= 16; i++)
    {
        float x = i / 16.0f;

        if (i < 16)
        {
            float s = (i + 0.5f) / 16.0f;
            const float west[4][5] = {
                {x, 0, -0.5f, s, 1}, {x, 0, 0.5f, s, 1}, {x, 1, 0.5f, s, 0}, {x, 1, -0.5f, s, 0},
            };

            out = Quad(out, west, SHADE_WEST, SHADE_WEST);
        }
        if (i > 0)
        {
            float s = (i - 0.5f) / 16.0f;
            const float east[4][5] = {
                {x, 0, -0.5f, s, 1}, {x, 0, 0.5f, s, 1}, {x, 1, 0.5f, s, 0}, {x, 1, -0.5f, s, 0},
            };

            out = Quad(out, east, SHADE_EAST, SHADE_EAST);
        }
    }
    /* Row boundary j (from the top): the top of row j, the bottom of row j - 1. */
    for (i = 0; i <= 16; i++)
    {
        float y = 1.0f - i / 16.0f;

        if (i < 16)
        {
            float t = (i + 0.5f) / 16.0f;
            const float top[4][5] = {
                {0, y, -0.5f, 0, t}, {1, y, -0.5f, 1, t}, {1, y, 0.5f, 1, t}, {0, y, 0.5f, 0, t},
            };

            out = Quad(out, top, SHADE_TOP, SHADE_FLAT_NORTH);
        }
        if (i > 0)
        {
            float t = (i - 0.5f) / 16.0f;
            const float bottom[4][5] = {
                {0, y, -0.5f, 0, t}, {1, y, -0.5f, 1, t}, {1, y, 0.5f, 1, t}, {0, y, 0.5f, 0, t},
            };

            out = Quad(out, bottom, SHADE_BOTTOM, SHADE_FLAT_SOUTH);
        }
    }
    return (uint32_t)(out - vertices);
}

static void SetupArray(GLuint array, GLuint meshBuffer)
{
    dglBindVertexArray(array);
    dglBindBuffer(GL_ARRAY_BUFFER, meshBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct MeshVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct MeshVertex),
                           (const void *)offsetof(struct MeshVertex, s));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(struct MeshVertex),
                           (const void *)offsetof(struct MeshVertex, shade));
    dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
    dglEnableVertexAttribArray(3);
    dglVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)offsetof(struct GLInstance, x));
    dglVertexAttribDivisor(3, 1);
    dglEnableVertexAttribArray(4);
    dglVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)offsetof(struct GLInstance, width));
    dglVertexAttribDivisor(4, 1);
    dglEnableVertexAttribArray(5);
    dglVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)offsetof(struct GLInstance, u));
    dglVertexAttribDivisor(5, 1);
    dglEnableVertexAttribArray(6);
    dglVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)offsetof(struct GLInstance, sway));
    dglVertexAttribDivisor(6, 1);
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* A borrowed art's tile entries (props.h artMetatile): the game's own
 * tilesets, constant data, so the renderer may read them. */
static bool MetatileEntries(uint16_t layoutId, uint16_t metatileId,
                            uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT])
{
    const struct MapLayout *layout;
    const struct Tileset *tileset;

    /* Only layouts the rules know (the ids of maps the 3D view can show). */
    if (!DioramaProps_KnownLayout(layoutId) || metatileId >= NUM_METATILES_TOTAL)
        return false;
    layout = gMapLayouts[layoutId - 1];
    if (layout == NULL)
        return false;
    tileset = metatileId < NUM_METATILES_IN_PRIMARY ? layout->primaryTileset : layout->secondaryTileset;
    if (tileset == NULL || tileset->metatiles == NULL)
        return false;
    memcpy(entries, tileset->metatiles + (metatileId % NUM_METATILES_IN_PRIMARY) * NUM_TILES_PER_METATILE,
           DIORAMA_METATILE_ENTRY_COUNT * sizeof(uint16_t));
    return true;
}

/* A modelled building's cell shows the model (its own hedge or rock). */
static bool CellTaken(uint16_t layout, int x, int y)
{
    return DioramaBuildings_CellAt(layout, x, y, NULL, NULL, NULL);
}

bool DioramaGLProps_Init(void)
{
    static struct MeshVertex sSlab[SLAB_QUADS * 6];
    struct MeshVertex card[CARD_QUADS * 6];
    const char *value = SDL_getenv("PKMN_DIORAMA_PROPS");

    if (sReady)
        return true;
    if (value != NULL && value[0] == '0')
        DioramaProps_SetEnabled(false);
    if (!CreateProgram())
        return false;
    DioramaProps_SetMetatileSource(MetatileEntries);
    DioramaProps_SetCellTaken(CellTaken);
    sSlabVertexCount = BuildSlab(sSlab);
    sCardVertexCount = (uint32_t)(Faces(card) - card);
    dglGenBuffers(1, &sSlabBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sSlabBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sSlabVertexCount * sizeof(struct MeshVertex), sSlab, GL_STATIC_DRAW);
    dglGenBuffers(1, &sCardBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sCardBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sCardVertexCount * sizeof(struct MeshVertex), card, GL_STATIC_DRAW);
    dglGenBuffers(1, &sInstanceBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(sInstances), NULL, GL_DYNAMIC_DRAW);
    dglGenBuffers(1, &sMoundBuffer);
    dglGenVertexArrays(1, &sSlabArray);
    dglGenVertexArrays(1, &sCardArray);
    dglGenVertexArrays(1, &sMoundArray);
    SetupArray(sSlabArray, sSlabBuffer);
    SetupArray(sCardArray, sCardBuffer);
    SetupArray(sMoundArray, sMoundBuffer);
    glGenTextures(1, &sArtTexture);
    glBindTexture(GL_TEXTURE_2D, sArtTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    memset(sArtPixels, 0, sizeof(sArtPixels));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ART_WIDTH, ART_HEIGHT, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                 sArtPixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    memset(sSlotEntry, NO_ENTRY, sizeof(sSlotEntry));
    memset(sMoundMaskHash, 0, sizeof(sMoundMaskHash));
    sEntryCount = 0;
    sSlabCount = sMoundCount = sCardCount = 0;
    sMoundGroupCount = 0;
    sSequence = 0;
    sReady = true;
    return true;
}

/* A piece's 18 x 18 pixels: the art, its edge repeated round it. */
/* Stores a piece's art (with its border); true when it differs from what the
 * texture holds. */
static bool StorePiece(unsigned entry, const uint32_t art[16 * 16])
{
    unsigned originX = (entry % ENTRY_COLUMNS) * ENTRY_STRIDE;
    unsigned originY = (entry / ENTRY_COLUMNS) * ENTRY_STRIDE;
    bool changed = false;
    int y;
    int x;

    for (y = -1; y <= 16; y++)
        for (x = -1; x <= 16; x++)
        {
            int sx = x < 0 ? 0 : x > 15 ? 15 : x;
            int sy = y < 0 ? 0 : y > 15 ? 15 : y;
            uint32_t *texel = &sArtPixels[(originY + 1 + y) * ART_WIDTH + originX + 1 + x];

            if (*texel != art[sy * 16 + sx])
            {
                *texel = art[sy * 16 + sx];
                changed = true;
            }
        }
    return changed;
}

static void UploadPiece(unsigned entry)
{
    unsigned originX = (entry % ENTRY_COLUMNS) * ENTRY_STRIDE;
    unsigned originY = (entry / ENTRY_COLUMNS) * ENTRY_STRIDE;

    glPixelStorei(GL_UNPACK_ROW_LENGTH, ART_WIDTH);
    glTexSubImage2D(GL_TEXTURE_2D, 0, originX, originY, ENTRY_STRIDE, ENTRY_STRIDE, GL_BGRA,
                    GL_UNSIGNED_BYTE, &sArtPixels[originY * ART_WIDTH + originX]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/* Composes a piece from a cell using it; true when its art changed. */
static bool ComposePiece(const struct DioramaSceneSnapshot *snapshot, unsigned entry,
                         const struct DioramaPropInstance *prop)
{
    const struct DioramaCellSnapshot *cell = &snapshot->cells[prop->cellIndex];
    struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, cell);
    uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT];
    uint32_t pixels[16 * 16];
    struct DioramaProp whole = *prop->prop;

    /* The piece holds every row the prop has; an instance shows some. */
    DioramaProps_ArtEntries(cell, prop->prop, entries);
    DioramaProps_ComposeArt(&whole, art.tileGraphics, art.fadedPalette, entries, pixels);
    if ((prop->prop->flags & DIORAMA_PROP_LIGHT) && art.unfadedPalette != NULL)
    {
        /* A lamp's glass: its light pixels as drawn, before the night tint
         * darkens them (which left the glass too dark to glow), marked in the
         * alpha the shader reads (GLASS_ALPHA). */
        uint32_t unfaded[16 * 16];
        unsigned i;

        DioramaProps_ComposeArt(&whole, art.tileGraphics, art.unfadedPalette, entries, unfaded);
        for (i = 0; i < 16 * 16; i++)
        {
            uint32_t c = unfaded[i];
            uint32_t brightest = (c >> 16) & 0xFF;

            if (((c >> 8) & 0xFF) > brightest)
                brightest = (c >> 8) & 0xFF;
            if ((c & 0xFF) > brightest)
                brightest = c & 0xFF;
            if ((pixels[i] >> 24) != 0 && brightest >= GLASS_BRIGHTNESS)
                pixels[i] = (pixels[i] & 0x00FFFFFFu) | ((uint32_t)GLASS_ALPHA << 24);
        }
    }
    sEntryProp[entry] = prop->prop;
    return StorePiece(entry, pixels);
}

static void SyncArt(const struct DioramaSceneSnapshot *snapshot, uint32_t pageSignature)
{
    bool recompose = snapshot->paletteGeneration != sArtPaletteGeneration
                  || snapshot->tilesetAnimationGeneration != sArtAnimationGeneration
                  || pageSignature != sArtPageSignature;
    unsigned i;

    if (snapshot->mapGeneration != sArtMapGeneration)
    {
        /* Another map: its slots hold other art. */
        memset(sSlotEntry, NO_ENTRY, sizeof(sSlotEntry));
        sEntryCount = 0;
        sEntryFull = false;
        sArtMapGeneration = snapshot->mapGeneration;
        recompose = true;
    }
    glBindTexture(GL_TEXTURE_2D, sArtTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (recompose)
    {
        /* Every piece again from the cells using it now; a piece no cell
         * uses any more keeps its old art until the map changes. Only the
         * pieces whose art changed are uploaded (a palette's night tint, an
         * animated tile, touch some of them or none: performance work
         * 2026-10-12), the whole texture when most did. */
        static bool sDone[MAX_ENTRIES];
        static uint8_t sChanged[MAX_ENTRIES];
        unsigned changedCount = 0;
        unsigned c;

        memset(sDone, 0, sizeof(sDone));
        for (i = 0; i < sSlabCount + sMoundCount + sCardCount; i++)
        {
            uint8_t entry = sSlotEntry[sProps[i].artSlot];

            if (entry != NO_ENTRY && !sDone[entry])
            {
                if (ComposePiece(snapshot, entry, &sProps[i]))
                    sChanged[changedCount++] = entry;
                sDone[entry] = true;
            }
        }
        sArtPaletteGeneration = snapshot->paletteGeneration;
        sArtAnimationGeneration = snapshot->tilesetAnimationGeneration;
        sArtPageSignature = pageSignature;
        if (changedCount > MAX_ENTRIES / 4)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ART_WIDTH, ART_HEIGHT, GL_BGRA, GL_UNSIGNED_BYTE,
                            sArtPixels);
        else
            for (c = 0; c < changedCount; c++)
                UploadPiece(sChanged[c]);
    }
    /* New slots get a piece. */
    for (i = 0; i < sSlabCount + sMoundCount + sCardCount; i++)
    {
        uint16_t slot = sProps[i].artSlot;

        if (sSlotEntry[slot] != NO_ENTRY)
            continue;
        if (sEntryCount >= MAX_ENTRIES)
        {
            if (!sEntryFull)
                SDL_Log("Diorama: the prop art texture is full (%u pieces); more props stay flat",
                        MAX_ENTRIES);
            sEntryFull = true;
            continue;
        }
        sSlotEntry[slot] = (uint8_t)sEntryCount;
        sEntrySlot[sEntryCount] = slot;
        ComposePiece(snapshot, sEntryCount, &sProps[i]);
        UploadPiece(sEntryCount);
        sEntryCount++;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

static uint32_t HashInstances(const struct GLInstance *instances, unsigned count)
{
    const uint8_t *bytes = (const uint8_t *)instances;
    size_t length = count * sizeof(*instances);
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < length; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash ^ count;
}

/* A mound's column heights (sixteenths, 0 where transparent) from an art
 * piece: each opaque pixel's distance to the nearest transparent one (or the
 * art's edge), rounded off towards the middle. */
static void MoundHeights(unsigned entry, uint8_t heights[16 * 16])
{
    unsigned originX = (entry % ENTRY_COLUMNS) * ENTRY_STRIDE + 1;
    unsigned originY = (entry / ENTRY_COLUMNS) * ENTRY_STRIDE + 1;
    uint8_t distance[16 * 16];
    uint8_t deepest = 0;
    bool changed = true;
    bool joined = sEntryProp[entry] != NULL && (sEntryProp[entry]->flags & DIORAMA_PROP_JOINED);
    int pass;
    int i;

    for (i = 0; i < 16 * 16; i++)
        distance[i] = (sArtPixels[(originY + i / 16) * ART_WIDTH + originX + i % 16] >> 24) != 0 ? 0xFF : 0;
    /* Distances by repeated relaxation (16 x 16: a few passes). */
    for (pass = 0; changed && pass < 16; pass++)
    {
        changed = false;
        for (i = 0; i < 16 * 16; i++)
        {
            int x = i % 16;
            int y = i / 16;
            uint8_t nearest;

            if (distance[i] == 0)
                continue;
            nearest = !joined && (x == 0 || y == 0 || x == 15 || y == 15) ? 0 : 0xFF;
            if (x > 0 && distance[i - 1] < nearest) nearest = distance[i - 1];
            if (x < 15 && distance[i + 1] < nearest) nearest = distance[i + 1];
            if (y > 0 && distance[i - 16] < nearest) nearest = distance[i - 16];
            if (y < 15 && distance[i + 16] < nearest) nearest = distance[i + 16];
            if (nearest + 1 < distance[i])
            {
                distance[i] = (uint8_t)(nearest + 1);
                changed = true;
            }
        }
    }
    for (i = 0; i < 16 * 16; i++)
        if (distance[i] != 0xFF && distance[i] > deepest)
            deepest = distance[i];
    for (i = 0; i < 16 * 16; i++)
    {
        float f;
        int h;

        if (distance[i] == 0 || deepest == 0)
        {
            heights[i] = 0;
            continue;
        }
        f = powf((float)distance[i] / deepest, 0.6f);
        h = (int)(f * 16.0f + 0.5f);
        heights[i] = (uint8_t)(h < 3 ? 3 : h > 16 ? 16 : h);
    }
}

/* A mound's mesh, in the lying slab's terms (x across the art, y up it - its
 * first row north -, z 0.5 at the ground, -0.5 at the full height). */
static uint32_t BuildMound(unsigned entry, struct MeshVertex *out)
{
    static const int8_t sSides[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}}; /* north, south, east, west */
    /* Lying (a mound) the rows run north-south; standing (a relief) the
     * row above faces up and the row below down. */
    static const float sShades[4] = {SHADE_FLAT_NORTH, SHADE_FLAT_SOUTH, SHADE_EAST, SHADE_WEST};
    static const float sStandingShades[4] = {SHADE_TOP, SHADE_BOTTOM, SHADE_EAST, SHADE_WEST};
    uint8_t heights[16 * 16];
    struct MeshVertex *start = out;
    int i;

    MoundHeights(entry, heights);
    for (i = 0; i < 16 * 16; i++)
    {
        int x = i % 16;
        int y = i / 16;
        float h = heights[i] / 16.0f;
        float x0 = x / 16.0f;
        float x1 = (x + 1) / 16.0f;
        float north = 1.0f - y / 16.0f;
        float south = 1.0f - (y + 1) / 16.0f;
        int side;

        if (heights[i] == 0)
            continue;
        {
            const float top[4][5] = {
                {x0, north, 0.5f - h, x0, y / 16.0f}, {x1, north, 0.5f - h, x1, y / 16.0f},
                {x1, south, 0.5f - h, x1, (y + 1) / 16.0f}, {x0, south, 0.5f - h, x0, (y + 1) / 16.0f},
            };

            out = Quad(out, top, SHADE_FRONT, SHADE_FLAT_TOP);
        }
        for (side = 0; side < 4; side++)
        {
            int nx = x + sSides[side][0];
            int ny = y + sSides[side][1];
            /* A wall takes the colour a pixel further in: the art's dark
             * outline stays on the top's edge, the sides show the body. */
            int ix = x - sSides[side][0];
            int iy = y - sSides[side][1];
            bool inner = ix >= 0 && iy >= 0 && ix <= 15 && iy <= 15 && heights[iy * 16 + ix] != 0;
            float s = ((inner ? ix : x) + 0.5f) / 16.0f;
            float t = ((inner ? iy : y) + 0.5f) / 16.0f;
            float below = nx < 0 || ny < 0 || nx > 15 || ny > 15 ? 0.0f : heights[ny * 16 + nx] / 16.0f;
            float a[2];
            float b[2];

            if (below >= h)
                continue;
            if (side == 0)      { a[0] = x0; a[1] = north; b[0] = x1; b[1] = north; }
            else if (side == 1) { a[0] = x0; a[1] = south; b[0] = x1; b[1] = south; }
            else if (side == 2) { a[0] = x1; a[1] = north; b[0] = x1; b[1] = south; }
            else                { a[0] = x0; a[1] = north; b[0] = x0; b[1] = south; }
            {
                const float wall[4][5] = {
                    {a[0], a[1], 0.5f - below, s, t}, {b[0], b[1], 0.5f - below, s, t},
                    {b[0], b[1], 0.5f - h, s, t}, {a[0], a[1], 0.5f - h, s, t},
                };

                out = Quad(out, wall, sStandingShades[side], sShades[side]);
            }
        }
    }
    return (uint32_t)(out - start);
}

static uint64_t PieceMask(unsigned entry)
{
    unsigned originX = (entry % ENTRY_COLUMNS) * ENTRY_STRIDE + 1;
    unsigned originY = (entry / ENTRY_COLUMNS) * ENTRY_STRIDE + 1;
    uint64_t hash = 1469598103934665603ull;
    int i;

    for (i = 0; i < 16 * 16; i++)
        hash = (hash ^ ((sArtPixels[(originY + i / 16) * ART_WIDTH + originX + i % 16] >> 24) != 0))
             * 1099511628211ull;
    hash ^= (uint64_t)(uintptr_t)sEntryProp[entry];
    return hash | 1;
}

/* The meshes of the pieces the mounds use, again when one's pixels changed. */
static void SyncMounds(void)
{
    bool rebuild = false;
    uint32_t total = 0;
    unsigned g;

    for (g = 0; g < sMoundGroupCount; g++)
        if (PieceMask(sMoundGroups[g].entry) != sMoundMaskHash[sMoundGroups[g].entry])
            rebuild = true;
    if (!rebuild)
        return;
    /* Every piece a mound uses: at most 16 x 16 columns, each a top and four
     * sides. */
    if (sMoundCapacity < sMoundGroupCount * 16 * 16 * 5 * 6)
    {
        struct MeshVertex *grown = SDL_realloc(sMoundVertices,
                                               sMoundGroupCount * 16 * 16 * 5 * 6 * sizeof(*grown));

        if (grown == NULL)
        {
            sMoundGroupCount = 0;
            return;
        }
        sMoundVertices = grown;
        sMoundCapacity = sMoundGroupCount * 16 * 16 * 5 * 6;
    }
    memset(sMoundMaskHash, 0, sizeof(sMoundMaskHash));
    for (g = 0; g < sMoundGroupCount; g++)
    {
        unsigned entry = sMoundGroups[g].entry;

        if (sMoundMaskHash[entry] != 0)
            continue;
        sMoundFirst[entry] = total;
        sMoundVertexCount[entry] = BuildMound(entry, sMoundVertices + total);
        total += sMoundVertexCount[entry];
        sMoundMaskHash[entry] = PieceMask(entry);
    }
    dglBindBuffer(GL_ARRAY_BUFFER, sMoundBuffer);
    dglBufferData(GL_ARRAY_BUFFER, total * sizeof(struct MeshVertex), total != 0 ? sMoundVertices : NULL,
                  GL_DYNAMIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    sMoundGeneration++;
}

static int CompareMounds(const void *a, const void *b)
{
    const struct DioramaPropInstance *first = a;
    const struct DioramaPropInstance *second = b;

    if (sSlotEntry[first->artSlot] != sSlotEntry[second->artSlot])
        return (int)sSlotEntry[first->artSlot] - (int)sSlotEntry[second->artSlot];
    return (int)first->cellIndex - (int)second->cellIndex;
}

void DioramaGLProps_Sync(const struct DioramaSceneSnapshot *snapshot,
                         const struct DioramaResolvedCell *resolvedCells)
{
    static struct DioramaPropInstance sBuilt[MAX_INSTANCES];
    unsigned count;
    unsigned i;
    unsigned out = 0;
    bool complete;
    uint32_t hash;
    float lampStrength;

    if (!sReady || snapshot == NULL || snapshot->sequence == sSequence)
        return;
    sSequence = snapshot->sequence;
    count = DioramaProps_Build(snapshot, resolvedCells, sBuilt, MAX_INSTANCES, &complete);
    /* Slabs first, then mounds, then cards. */
    for (i = 0; i < count; i++)
        if (sBuilt[i].prop->form != DIORAMA_PROP_CARDS && sBuilt[i].prop->form != DIORAMA_PROP_MOUND
         && sBuilt[i].prop->form != DIORAMA_PROP_RELIEF)
            sProps[out++] = sBuilt[i];
    sSlabCount = out;
    for (i = 0; i < count; i++)
        if (sBuilt[i].prop->form == DIORAMA_PROP_MOUND || sBuilt[i].prop->form == DIORAMA_PROP_RELIEF)
            sProps[out++] = sBuilt[i];
    sMoundCount = out - sSlabCount;
    for (i = 0; i < count; i++)
        if (sBuilt[i].prop->form == DIORAMA_PROP_CARDS)
            sProps[out++] = sBuilt[i];
    sCardCount = out - sSlabCount - sMoundCount;
    /* The atlas's pages for this snapshot are set (gl_compositor.c EnsureAtlas
     * ran first); a change of them changes the pieces' art. */
    SyncArt(snapshot, DioramaAtlas_SetPages(snapshot));
    /* The mounds in runs of one piece (one draw each, its own mesh). */
    qsort(&sProps[sSlabCount], sMoundCount, sizeof(sProps[0]), CompareMounds);
    sMoundGroupCount = 0;
    for (i = sSlabCount; i < sSlabCount + sMoundCount; i++)
    {
        uint8_t entry = sSlotEntry[sProps[i].artSlot];

        if (entry == NO_ENTRY)
            continue;
        if (sMoundGroupCount != 0 && sMoundGroups[sMoundGroupCount - 1].entry == entry)
            sMoundGroups[sMoundGroupCount - 1].instanceCount++;
        else if (sMoundGroupCount < MAX_MOUND_GROUPS)
        {
            sMoundGroups[sMoundGroupCount].entry = entry;
            sMoundGroups[sMoundGroupCount].firstInstance = i;
            sMoundGroups[sMoundGroupCount].instanceCount = 1;
            sMoundGroupCount++;
        }
    }
    SyncMounds();
    /* The lamps: lit in the evening, fully at night; each a light a little
     * under the top of its head, reaching a few tiles. */
    {
        static float sLampPlaces[128][4];
        unsigned lamps = 0;

        lampStrength = DioramaGLQuality_Lighting()
                     ? DioramaGLLamps_NightFor(snapshot->mapType, snapshot->timeOfDay) : 0.0f;
        for (i = 0; i < out && lamps < 128; i++)
            if (sProps[i].flags & DIORAMA_PROP_LIGHT)
            {
                sLampPlaces[lamps][0] = sProps[i].x;
                sLampPlaces[lamps][1] = sProps[i].y + sProps[i].height * 0.55f;
                sLampPlaces[lamps][2] = sProps[i].z;
                sLampPlaces[lamps][3] = 3.0f;
                lamps++;
            }
        DioramaGLLamps_Set((const float (*)[4])sLampPlaces, lamps, lampStrength);
    }
    for (i = 0; i < out; i++)
    {
        const struct DioramaPropInstance *prop = &sProps[i];
        struct GLInstance *instance = &sInstances[i];
        uint8_t entry = sSlotEntry[prop->artSlot];

        instance->x = prop->x;
        instance->y = prop->y;
        instance->z = prop->z;
        instance->yaw = prop->yaw;
        instance->width = prop->width;
        instance->height = prop->height;
        instance->depth = prop->depth;
        /* Lying: 1 and the rise of its north edge (a canopy's slope). */
        instance->lie = prop->prop->form == DIORAMA_PROP_BLOCK || prop->prop->form == DIORAMA_PROP_MOUND
                      ? 1.0f + prop->rise : 0.0f;
        instance->light = (prop->flags & DIORAMA_PROP_LIGHT) ? lampStrength : 0.0f;
        instance->rustle = prop->rustle;
        if (entry == NO_ENTRY)
        {
            /* No art for it: nothing shown (rows empty). */
            instance->u = instance->v = 0.0f;
            instance->rowTop = instance->rowBottom = 0.0f;
        }
        else
        {
            instance->u = ((entry % ENTRY_COLUMNS) * ENTRY_STRIDE + 1) / (float)ART_WIDTH;
            instance->v = ((entry / ENTRY_COLUMNS) * ENTRY_STRIDE + 1) / (float)ART_HEIGHT;
            instance->rowTop = prop->artTop / 16.0f;
            instance->rowBottom = prop->artBottom / 16.0f;
        }
        instance->sway = prop->sway;
        instance->phase = prop->phase;
    }
    hash = HashInstances(sInstances, sSlabCount + sMoundCount) ^ sMoundGeneration * 2654435761u;
    if (hash != sGeometryHash)
    {
        sGeometryHash = hash;
        sGeometryGeneration++;
    }
    /* Uploaded again only when an instance changed (most game frames none
     * does: performance work 2026-10-12). */
    {
        uint32_t allHash = HashInstances(sInstances, out);

        if (allHash != sUploadedHash || out != sUploadedCount)
        {
            dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
            dglBufferData(GL_ARRAY_BUFFER, out * sizeof(struct GLInstance), out != 0 ? sInstances : NULL,
                          GL_DYNAMIC_DRAW);
            dglBindBuffer(GL_ARRAY_BUFFER, 0);
            sUploadedHash = allHash;
            sUploadedCount = out;
        }
    }
    {
        static int sLogEnabled = -1;
        static unsigned sLogged = 0xFFFFFFFFu;

        if (sLogEnabled < 0)
        {
            const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

            sLogEnabled = value != NULL && value[0] == '1';
        }
        if (sLogEnabled && sLogged != (sSlabCount << 20 ^ sMoundCount << 10 ^ sCardCount))
        {
            SDL_Log("Diorama: props: %u slabs, %u mounds (%u kinds), %u cards, %u art pieces%s", sSlabCount,
                    sMoundCount, sMoundGroupCount, sCardCount, sEntryCount,
                    complete ? "" : " (more did not fit)");
            sLogged = sSlabCount << 20 ^ sMoundCount << 10 ^ sCardCount;
        }
    }
}

/* Points the instance attributes of a vertex array at instance `first`. */
static void PointInstances(GLuint array, uint32_t first)
{
    size_t offset = first * sizeof(struct GLInstance);

    dglBindVertexArray(array);
    dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
    dglVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)(offset + offsetof(struct GLInstance, x)));
    dglVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)(offset + offsetof(struct GLInstance, width)));
    dglVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)(offset + offsetof(struct GLInstance, u)));
    dglVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(struct GLInstance),
                           (const void *)(offset + offsetof(struct GLInstance, sway)));
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* The slabs and the mounds: what casts shadows. */
static void DrawSolids(void)
{
    unsigned g;

    if (sSlabCount != 0)
    {
        PointInstances(sSlabArray, 0);
        dglDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)sSlabVertexCount, (GLsizei)sSlabCount);
    }
    for (g = 0; g < sMoundGroupCount; g++)
    {
        const struct MoundGroup *group = &sMoundGroups[g];

        if (sMoundVertexCount[group->entry] == 0)
            continue;
        PointInstances(sMoundArray, group->firstInstance);
        dglDrawArraysInstanced(GL_TRIANGLES, (GLint)sMoundFirst[group->entry],
                               (GLsizei)sMoundVertexCount[group->entry], (GLsizei)group->instanceCount);
    }
}

static void SetUniforms(float cameraX, float cameraZ, float cameraPitch, float focalLength,
                        float aspectCorrection)
{
    float eyeHeight;
    float eyeDistance;
    float screenOffsetY;

    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1f(sAspectCorrectionLocation, aspectCorrection);
    DioramaCamera_GetRig(cameraPitch, &eyeHeight, &eyeDistance, &screenOffsetY);
    dglUniform1f(sCameraHeightLocation, eyeHeight);
    dglUniform1f(sCameraDistanceLocation, eyeDistance);
    dglUniform1f(sScreenOffsetLocation, screenOffsetY);
    /* Still (the battle's world, kept as a picture between redraws): one pose, so no redraw jumps. */
    dglUniform1f(sTimeLocation, sStill ? 0.0f : (float)fmod(SDL_GetTicks() / 1000.0, 3600.0));
}

void DioramaGLProps_SetStill(bool still)
{
    sStill = still;
}

void DioramaGLProps_Draw(float cameraX, float cameraZ, float cameraPitch,
                         float focalLength, float aspectCorrection)
{
    if (!sReady || sSlabCount + sMoundCount + sCardCount == 0)
        return;
    dglUseProgram(sProgram);
    SetUniforms(cameraX, cameraZ, cameraPitch, focalLength, aspectCorrection);
    dglUniform1i(sArtLocation, 0);
    dglUniform1i(sShadowPassLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniform1i(sShadowEnabledLocation, DioramaGLShadow_BindForLighting());
    dglUniform1f(sShadowAmbientLocation, DioramaGLShadow_Ambient());
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglUniform1f(sSaturationLocation, DioramaGLGrade_Saturation());
    DioramaGLLight_Apply(&sLightUniforms, cameraPitch);
    DioramaGLLamps_Apply(&sLampUniforms);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sArtTexture);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    DrawSolids();
    if (sCardCount != 0)
    {
        /* The cards' instances follow the slabs' and the mounds'. */
        PointInstances(sCardArray, sSlabCount + sMoundCount);
        dglDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)sCardVertexCount, (GLsizei)sCardCount);
    }
    dglBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_DEPTH_TEST);
}

void DioramaGLProps_DrawShadow(void)
{
    if (!sReady || sSlabCount + sMoundCount == 0)
        return;
    dglUseProgram(sProgram);
    dglUniform1i(sArtLocation, 0);
    dglUniform1i(sShadowPassLocation, 1);
    dglUniform1i(sShadowEnabledLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sArtTexture);
    glDisable(GL_CULL_FACE);
    DrawSolids();
    dglBindVertexArray(0);
    dglUniform1i(sShadowPassLocation, 0);
}

uint32_t DioramaGLProps_GetGeometryGeneration(void)
{
    return sGeometryGeneration;
}

unsigned DioramaGLProps_InstanceCount(void)
{
    return sSlabCount + sMoundCount + sCardCount;
}

void DioramaGLProps_Shutdown(void)
{
    if (sInstanceBuffer != 0)
        dglDeleteBuffers(1, &sInstanceBuffer);
    if (sSlabBuffer != 0)
        dglDeleteBuffers(1, &sSlabBuffer);
    if (sCardBuffer != 0)
        dglDeleteBuffers(1, &sCardBuffer);
    if (sMoundBuffer != 0)
        dglDeleteBuffers(1, &sMoundBuffer);
    if (sMoundArray != 0)
        dglDeleteVertexArrays(1, &sMoundArray);
    SDL_free(sMoundVertices);
    sMoundVertices = NULL;
    sMoundCapacity = 0;
    memset(sMoundMaskHash, 0, sizeof(sMoundMaskHash));
    if (sSlabArray != 0)
        dglDeleteVertexArrays(1, &sSlabArray);
    if (sCardArray != 0)
        dglDeleteVertexArrays(1, &sCardArray);
    if (sArtTexture != 0)
        glDeleteTextures(1, &sArtTexture);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sInstanceBuffer = sSlabBuffer = sCardBuffer = sMoundBuffer = 0;
    sSlabArray = sCardArray = sMoundArray = sArtTexture = sProgram = 0;
    sReady = false;
    sSlabCount = sMoundCount = sCardCount = 0;
    sMoundGroupCount = 0;
    sSequence = 0;
    sEntryCount = 0;
    sUploadedCount = 0xFFFFFFFFu;
}

#endif // ENABLE_DIORAMA
