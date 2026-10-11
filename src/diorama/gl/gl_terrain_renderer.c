#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "diorama/buildings.h"
#include "diorama/far_field.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_grade.h"
#include "diorama/gl_lamps.h"
#include "diorama/gl_light.h"
#include "diorama/gl_quality.h"
#include "diorama/gl_shadow.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/metatile_atlas.h"
#include "diorama/props.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"
#include "metatile_behavior.h"

#define TERRAIN_GRID_CHUNKS 100 /* the 49 x 49 cells span up to 8 x 8 chunks */
/*
 * The chunk cache (performance work 2026-10-11): the chunks in view, and
 * those of places seen before - other maps included - kept until the slots
 * are needed again (the least recently seen first). A chunk comes back from
 * the cache when its input's signature is the same, so walking back over a
 * map's edge or out of a house builds nothing it built before. Its size is
 * the memory budget's (DioramaGLTerrain_SetCacheChunks): at least the grid's
 * 100, at most TERRAIN_CHUNK_POOL_MAX.
 */
#define TERRAIN_CHUNK_POOL_MAX 1024
#define TERRAIN_CHUNK_POOL_DEFAULT 160
#define TERRAIN_DEBUG_MAX_VERTICES ((TERRAIN_GRID_CHUNKS + DIORAMA_MAX_VISIBLE_CELLS) * 8)
#define TERRAIN_MAX_VISIBLE_TREE_INSTANCES \
    (TERRAIN_GRID_CHUNKS * DIORAMA_TERRAIN_MAX_TREE_INSTANCES)

struct GLTerrainChunk
{
    bool occupied;
    bool active;
    int16_t chunkX;
    int16_t chunkY;
    uint32_t mapKey;           /* the map whose coordinates chunkX and chunkY are in */
    bool stale;                /* its build is waiting (sPending): the old mesh stands meanwhile */
    uint32_t mapGeneration;
    uint32_t rulesGeneration;
    uint64_t signature;
    uint64_t lastSeenSequence;
    /* Whether its input reads tile pixels (trees, mountain art, silhouettes,
     * ledges, cliffs), and whether pixel objects (coloured from cells
     * anywhere) fall in it: only such chunks can change when the game's
     * animated tiles do. Set when its input is built. */
    bool readsPixelCells;
    bool hasPixelObjects;
    GLuint vertexArray;
    GLuint vertexBuffer;
    struct DioramaTerrainMesh mesh;
};

static struct GLTerrainChunk sChunks[TERRAIN_CHUNK_POOL_MAX];
static unsigned sChunkLimit = TERRAIN_CHUNK_POOL_DEFAULT;
static uint32_t sSyncMapKey; /* the map of the chunks being synced */
static uint32_t sCacheHits;  /* chunks a map change found in the cache (PKMN_DIORAMA_LOG) */
static bool sMapJustChanged;
static uint32_t sHitsAtChange;

/*
 * What every chunk's BuildInput shares, worked out once per sync (it was per
 * chunk: up to 64 times a snapshot) - performance work 2026-10-11.
 */
static uint16_t sSyncGroundSlot;          /* the commonest flat ground on screen */
static int16_t sSyncAnchor[256];          /* per pixel object: its anchor cell, -1 none */
static bool sSyncSupportActive[256];      /* ... and whether its support stands in view */
/*
 * The last sync's inputs: a snapshot that changes none of them (the player
 * standing, the camera turning, the game's other frames) leaves every chunk
 * as it is without building any chunk's input again. A change of any kind -
 * a cell, a resolved cell, the palettes (a fade, the night tint), animated
 * tiles, a neighbour page, the rules, the detail objects' switch - runs the
 * chunk loop as before, whose signatures then pick the chunks to rebuild.
 */
struct TerrainSyncKey
{
    int16_t gridOriginX;
    int16_t gridOriginY;
    uint16_t visibleCellCount;
    uint8_t propsEnabled;
    uint8_t complete;
    uint32_t mapGeneration;
    uint32_t mapEditGeneration;
    uint32_t paletteGeneration;
    uint32_t tilesetAnimationGeneration;
    uint32_t rulesGeneration;
    uint32_t pageSignature;
};
static struct TerrainSyncKey sSyncKey;
static bool sSyncKeyValid;
static struct DioramaCellSnapshot sSyncCells[DIORAMA_MAX_VISIBLE_CELLS];
static struct DioramaResolvedCell sSyncResolved[DIORAMA_MAX_VISIBLE_CELLS];
static uint8_t sSyncTiles[DIORAMA_TILE_GRAPHICS_SIZE]; /* the tiles the chunks were built from */
static uint8_t sSyncDirtyTiles[DIORAMA_TILE_COUNT / 8];
static unsigned sCandidatePixels; /* the pixel object pixels BuildInput's chunk owns, any colour */
static uint32_t sSyncAnimated;  /* syncs where only animated tiles changed */
static uint32_t sSyncSkipped;   /* syncs that changed nothing (PKMN_DIORAMA_LOG) */
static uint32_t sSyncRun;
static bool sDebugDrawn;        /* the last Draw was the terrain debug view */
/*
 * A chunk the player cannot see may wait a few syncs for its rebuild
 * (performance work 2026-10-11): walking moves the grid a cell a step, and the
 * chunks at its far edges - off screen in the Adventure camera, in the haze in
 * the Diorama camera - change with it. Those near the player, on screen or
 * touched by a cell the game changed are built at once; the rest are built
 * TERRAIN_OFFSCREEN_BUILDS a sync, nearest first, so no frame pays for a row.
 */
#define TERRAIN_NEAR_CELLS 13
#define TERRAIN_OFFSCREEN_BUILDS 2
#define TERRAIN_MAX_PENDING 128
static int16_t sPending[TERRAIN_MAX_PENDING][2];
static unsigned sPendingCount;
static uint16_t sPrevGroundSlot;
static bool sCameraKnownForSync;
static float sSyncCameraX;
static float sSyncCameraZ;
static float sSyncCameraPitch;
static float sSyncFocalLength;
static uint32_t sInteriorSkips;
static uint32_t sDeferredBuilds;
static struct DioramaTerrainVertex sScratchVertices[DIORAMA_TERRAIN_MAX_VERTICES];
static struct DioramaTerrainVertex sDebugVertices[TERRAIN_DEBUG_MAX_VERTICES];
static struct DioramaTerrainTreeInstance sVisibleTreeInstances[TERRAIN_MAX_VISIBLE_TREE_INSTANCES];
static struct DioramaTerrainMetrics sMetrics;
static GLuint sProgram;
static GLuint sDebugVertexArray;
static GLuint sDebugVertexBuffer;
static GLuint sTreeVertexArray;
static GLuint sTreeVertexBuffer;
static GLuint sTreeInstanceBuffer;
static uint32_t sTreeModelVertexCount;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
static GLint sCameraHeightLocation;
static GLint sCameraDistanceLocation;
static GLint sScreenOffsetLocation;
static GLint sImageLocation;
static GLint sBaseImageLocation;
static GLint sForegroundImageLocation;
static GLint sDebugColorLocation;
static GLint sRenderPassLocation;
static GLint sShadowPassLocation;
static GLint sShadowEnabledLocation;
static GLint sLightMatrixLocation;
static GLint sShadowMapLocation;
static GLint sShadowAmbientLocation;
static GLint sPixelScaleLocation;
static GLint sPixelOffsetLocation;
static GLint sSaturationLocation;
static struct DioramaLightUniforms sLightUniforms;
static struct DioramaLampUniforms sLampUniforms;
/* The tree model's colour line this frame (TreeTint): faded = scale * baked + offset. */
static float sTreeScale[3] = {1.0f, 1.0f, 1.0f};
static float sTreeOffset[3];
static uint32_t sMapGeneration;
static uint32_t sMapEditGeneration;
static uint32_t sRulesGeneration;
static uint64_t sLastSyncSequence;
/* Bumped when what DrawShadow draws changes: a chunk rebuilt, the
 * chunks on screen changed, a reset. The sun's depth map is kept until
 * then (gl_shadow.h). */
static uint32_t sGeometryGeneration;
static int sLastChunkRange[4];
static uint32_t sClaimDebugVertexCount;
/* Distant scenery (diorama/far_field.h): one buffer, rebuilt when the far
 * blocks or the scene's grid move. */
static struct DioramaTerrainVertex sFarVertices[DIORAMA_FAR_MAX_VERTICES];
static struct DioramaFarSector sFarSectors[DIORAMA_FAR_SECTORS];
static GLuint sFarVertexArray;
static GLuint sFarVertexBuffer;
static uint32_t sFarVertexCount;
static uint32_t sFarGeneration;
static int16_t sFarGridX;
static int16_t sFarGridY;
static bool sFarBuilt;
static bool sFarShown;
static bool sFarLogged;
static bool FarBuildingHeight(uint16_t layoutId, int x, int y, float *top);

static const char sTerrainVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in float vertexShade;\n"
    "layout(location = 3) in float vertexTextureLayer;\n"
    "layout(location = 4) in float vertexReflectionMask;\n"
    "layout(location = 5) in vec4 vertexColor;\n"
    "layout(location = 6) in vec3 instanceOffset;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    /* The eye (terrain_mesh.h, DioramaCameraRig). */
    "uniform float cameraHeight;\n"
    "uniform float cameraDistance;\n"
    "uniform float screenOffsetY;\n"
    "uniform int shadowPass;\n"
    "uniform mat4 lightViewProjection;\n"
    "out vec2 uv;\n"
    "out float shade;\n"
    "flat out int textureLayer;\n"
    "out float reflectionMask;\n"
    "out vec4 pixelColor;\n"
    "out vec3 sceneWorldPosition;\n"
    "out float viewDepth;\n"
    "void main() {\n"
    "  vec3 worldPosition = position + instanceOffset;\n"
    "  sceneWorldPosition = worldPosition;\n"
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
    "  viewDepth = depth;\n"
    "  if (shadowPass == 1) gl_Position = lightViewProjection * vec4(worldPosition, 1.0);\n"
    "  uv = texCoord;\n"
    "  shade = vertexShade;\n"
    "  textureLayer = int(vertexTextureLayer);\n"
    "  reflectionMask = vertexReflectionMask;\n"
    "  pixelColor = vertexColor;\n"
    "}\n";

static const char sTerrainFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in float shade;\n"
    "flat in int textureLayer;\n"
    "in float reflectionMask;\n"
    "in vec4 pixelColor;\n"
    "in vec3 sceneWorldPosition;\n"
    "in float viewDepth;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform sampler2D baseImage;\n"
    "uniform sampler2D foregroundImage;\n"
    "uniform vec4 debugColor;\n"
    "uniform int renderPass;\n"
    "uniform int shadowPass;\n"
    "uniform int shadowEnabled;\n"
    "uniform mat4 lightViewProjection;\n"
    "uniform sampler2DShadow shadowMap;\n"
    "uniform float shadowAmbient;\n"
    /* The voxel tree model's own colours are baked: they follow the
     * palette's fade and night tint through this per-channel line. */
    "uniform vec3 pixelScale;\n"
    "uniform vec3 pixelOffset;\n"
    DIORAMA_GLSL_GRADE
    /* gl_shadow.h: how much sun a point gets, 1 lit, 0 in a cast shadow;
     * main() darkens a shadow to its ambient (the 3DS port's 0.70 by day)
     * and tints it cool (gl_light.h). */
    "float SunLit() {\n"
    "  if (shadowEnabled == 0) return 1.0;\n"
    "  vec4 lightPosition = lightViewProjection * vec4(sceneWorldPosition, 1.0);\n"
    "  vec3 p = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;\n"
    "  if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
    "  return texture(shadowMap, vec3(p.xy, p.z - 0.0015));\n"
    "}\n"
    DIORAMA_GLSL_LIGHT
    DIORAMA_GLSL_LAMPS
    "void main() {\n"
    "  if (shadowPass == 1) {\n"
    "    vec4 caster = textureLayer == 1 ? texture(baseImage, uv)\n"
    "                : (textureLayer == 2 ? texture(foregroundImage, uv)\n"
    "                : (textureLayer == 3 ? pixelColor : texture(image, uv)));\n"
    "    if (caster.a < 0.5) discard;\n"
    "    color = vec4(1.0); return;\n"
    "  }\n"
    "  if (renderPass == 1) {\n"
    "    if (reflectionMask < 0.5 || texture(foregroundImage, uv).a >= 0.5) discard;\n"
    "    color = vec4(1.0); return;\n"
    "  }\n"
    "  if (renderPass == 2) {\n"
    "    if (textureLayer != 3) discard;\n"
    "    color = vec4(1.0); return;\n"
    "  }\n"
    "  if (debugColor.a >= 0.0) { color = debugColor; return; }\n"
    "  vec4 texel = textureLayer == 1 ? texture(baseImage, uv)\n"
    "             : (textureLayer == 2 ? texture(foregroundImage, uv)\n"
    "             : (textureLayer == 3 ? pixelColor : texture(image, uv)));\n"
    "  if (texel.a < 0.5) discard;\n"
    "  if (textureLayer == 3) texel.rgb = clamp(texel.rgb * pixelScale + pixelOffset, 0.0, 1.0);\n"
    "  color = vec4(Grade(texel.rgb) * Light(shade * mix(shadowAmbient, 1.0, SunLit()))\n"
    "             * Dapple(sceneWorldPosition), texel.a);\n"
    "  color.rgb += Grade(texel.rgb) * Lamps(sceneWorldPosition);\n"
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
        SDL_Log("Diorama terrain shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sTerrainVertexShader);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sTerrainFragmentShader);
    GLint linked;
    bool lightLocated;

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0) dglDeleteShader(vertexShader);
        if (fragmentShader != 0) dglDeleteShader(fragmentShader);
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
        SDL_Log("Diorama terrain shader link failed: %s", log);
        return false;
    }
    sCameraLocation = dglGetUniformLocation(sProgram, "cameraPosition");
    sCameraPitchLocation = dglGetUniformLocation(sProgram, "cameraPitch");
    sFocalLengthLocation = dglGetUniformLocation(sProgram, "focalLength");
    sAspectCorrectionLocation = dglGetUniformLocation(sProgram, "aspectCorrection");
    sCameraHeightLocation = dglGetUniformLocation(sProgram, "cameraHeight");
    sCameraDistanceLocation = dglGetUniformLocation(sProgram, "cameraDistance");
    sScreenOffsetLocation = dglGetUniformLocation(sProgram, "screenOffsetY");
    sImageLocation = dglGetUniformLocation(sProgram, "image");
    sBaseImageLocation = dglGetUniformLocation(sProgram, "baseImage");
    sForegroundImageLocation = dglGetUniformLocation(sProgram, "foregroundImage");
    sDebugColorLocation = dglGetUniformLocation(sProgram, "debugColor");
    sRenderPassLocation = dglGetUniformLocation(sProgram, "renderPass");
    sShadowPassLocation = dglGetUniformLocation(sProgram, "shadowPass");
    sShadowEnabledLocation = dglGetUniformLocation(sProgram, "shadowEnabled");
    sLightMatrixLocation = dglGetUniformLocation(sProgram, "lightViewProjection");
    sShadowMapLocation = dglGetUniformLocation(sProgram, "shadowMap");
    sShadowAmbientLocation = dglGetUniformLocation(sProgram, "shadowAmbient");
    sPixelScaleLocation = dglGetUniformLocation(sProgram, "pixelScale");
    sPixelOffsetLocation = dglGetUniformLocation(sProgram, "pixelOffset");
    sSaturationLocation = dglGetUniformLocation(sProgram, "saturation");
    lightLocated = DioramaGLLight_Locate(sProgram, &sLightUniforms);
    DioramaGLLamps_Locate(sProgram, &sLampUniforms);
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0
        && sFocalLengthLocation >= 0 && sAspectCorrectionLocation >= 0
        && sImageLocation >= 0
        && sBaseImageLocation >= 0 && sForegroundImageLocation >= 0
        && sDebugColorLocation >= 0 && sRenderPassLocation >= 0
        && sShadowPassLocation >= 0 && sShadowEnabledLocation >= 0
        && sLightMatrixLocation >= 0 && sShadowMapLocation >= 0
        && sShadowAmbientLocation >= 0
        && sPixelScaleLocation >= 0 && sPixelOffsetLocation >= 0
        && sSaturationLocation >= 0 && lightLocated;
}

static void ConfigureVertexArray(GLuint vertexArray, GLuint vertexBuffer)
{
    dglBindVertexArray(vertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, shade));
    dglEnableVertexAttribArray(3);
    dglVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, textureLayer));
    dglEnableVertexAttribArray(4);
    dglVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainVertex),
                           (void *)offsetof(struct DioramaTerrainVertex, reflectionMask));
    dglEnableVertexAttribArray(5);
    dglVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                           sizeof(struct DioramaTerrainVertex),
                            (void *)offsetof(struct DioramaTerrainVertex, color));
}

static bool CreateTreeBuffers(void)
{
    struct DioramaTerrainBounds bounds;

    if (!DioramaTerrain_BuildTreeModel(sScratchVertices,
            DIORAMA_TERRAIN_MAX_VERTICES, &sTreeModelVertexCount, &bounds)
     || sTreeModelVertexCount == 0)
        return false;
    dglGenVertexArrays(1, &sTreeVertexArray);
    dglGenBuffers(1, &sTreeVertexBuffer);
    dglGenBuffers(1, &sTreeInstanceBuffer);
    ConfigureVertexArray(sTreeVertexArray, sTreeVertexBuffer);
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER,
                  sTreeModelVertexCount * sizeof(struct DioramaTerrainVertex),
                  sScratchVertices, GL_STATIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
    dglEnableVertexAttribArray(6);
    dglVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainTreeInstance), NULL);
    dglVertexAttribDivisor(6, 1);
    return true;
}

static uint32_t MapKey(const struct DioramaSceneSnapshot *snapshot)
{
    return ((uint32_t)snapshot->mapGroup << 24) | ((uint32_t)snapshot->mapNum << 16) | snapshot->mapLayoutId;
}

static struct GLTerrainChunk *FindChunk(int chunkX, int chunkY)
{
    unsigned i;

    for (i = 0; i < sChunkLimit; i++)
        if (sChunks[i].occupied && sChunks[i].mapKey == sSyncMapKey
         && sChunks[i].chunkX == chunkX && sChunks[i].chunkY == chunkY)
            return &sChunks[i];
    return NULL;
}

static struct GLTerrainChunk *AllocateChunk(void)
{
    struct GLTerrainChunk *oldest = NULL;
    unsigned i;

    for (i = 0; i < sChunkLimit; i++)
    {
        if (!sChunks[i].occupied)
            return &sChunks[i];
        if (!sChunks[i].active
         && (oldest == NULL || sChunks[i].lastSeenSequence < oldest->lastSeenSequence))
            oldest = &sChunks[i];
    }
    return oldest;
}

static const struct DioramaCellSnapshot *FindSnapshotCell(
    const struct DioramaSceneSnapshot *snapshot, int mapX, int mapY)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return NULL;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount
     || snapshot->cells[index].mapX != mapX || snapshot->cells[index].mapY != mapY)
        return NULL;
    return &snapshot->cells[index];
}

static struct DioramaTerrainCell *FindTerrainInputCell(
    struct DioramaTerrainChunkInput *input, int mapX, int mapY)
{
    int x = mapX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = mapY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    struct DioramaTerrainCell *cell;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE
     || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
    if (!cell->present || cell->mapX != mapX || cell->mapY != mapY)
        return NULL;
    return cell;
}

/* The page (scene_snapshot.h, atlasPage) of the cell whose materials are
 * being set: their metatiles are its map's, drawn in that page's slots. */
static uint8_t sMaterialPage;

/* A material drawn from an atlas slot (metatile_atlas.h); its metatileId
 * holds the slot, so a chunk's signature changes with the page. */
static void SetTerrainMaterialSlot(struct DioramaTerrainMaterial *material,
                                   uint16_t slot, uint8_t layer)
{
    struct DioramaAtlasUv uv = DioramaAtlas_GetUv(slot);

    material->metatileId = slot;
    material->layer = layer;
    material->rotation = 0;
    material->flags = 0;
    material->u0 = uv.u0;
    material->v0 = uv.v0;
    material->u1 = uv.u1;
    material->v1 = uv.v1;
}

static void SetTerrainMaterial(struct DioramaTerrainMaterial *material,
                               uint16_t metatileId, uint8_t layer)
{
    SetTerrainMaterialSlot(material, DioramaAtlas_Slot(sMaterialPage, metatileId), layer);
}

static void SetTerrainMaterialTransformed(struct DioramaTerrainMaterial *material,
                                          uint16_t metatileId, uint8_t layer,
                                          uint8_t rotation, uint8_t flags)
{
    SetTerrainMaterial(material, metatileId, layer);
    material->rotation = rotation;
    material->flags = flags;
}

static uint32_t PixelColor(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaCellSnapshot *sourceCell,
                           const struct DioramaGeneratedPixelV2 *pixel)
{
    struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, sourceCell);
    uint8_t colorIndex = DioramaMetatile_DecodePixel(
        art.tileGraphics, pixel->expectedTileEntry,
        pixel->sourceX & 7, pixel->sourceY & 7);
    uint8_t palette = pixel->expectedTileEntry >> 12;
    uint32_t argb;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (colorIndex == 0 || palette >= 16)
        return 0;
    argb = DioramaMetatile_ConvertColor(
        art.fadedPalette[palette * 16 + colorIndex], false);
    red = (argb >> 16) & 0xFF;
    green = (argb >> 8) & 0xFF;
    blue = argb & 0xFF;
    return red | ((uint32_t)green << 8) | ((uint32_t)blue << 16) | UINT32_C(0xFF000000);
}

static uint8_t TreeShade(const struct DioramaSceneSnapshot *snapshot,
                         const struct DioramaCellSnapshot *cell, int x, int y)
{
    static const uint16_t acceptedColors = (1 << 1) | (1 << 2) | (1 << 3)
                                         | (1 << 4) | (1 << 6) | (1 << 8);
    int subtile = (y / 8) * 2 + x / 8;
    struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, cell);
    uint16_t entry = cell->tileEntries[4 + subtile];
    uint8_t color = DioramaMetatile_DecodePixel(art.tileGraphics, entry,
                                                x & 7, y & 7);
    uint8_t palette = entry >> 12;
    uint32_t argb;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t darkest;

    if (color == 0)
    {
        entry = cell->tileEntries[subtile];
        color = DioramaMetatile_DecodePixel(art.tileGraphics, entry,
                                            x & 7, y & 7);
        palette = entry >> 12;
    }
    if (color == 0 || palette != 2 || !(acceptedColors & (1 << color)))
        return DIORAMA_TREE_SHADE_OFF;
    argb = DioramaMetatile_ConvertColor(art.unfadedPalette[palette * 16 + color], false);
    red = (argb >> 16) & 0xFF;
    green = (argb >> 8) & 0xFF;
    blue = argb & 0xFF;
    darkest = red < green ? red : green;
    if (blue < darkest)
        darkest = blue;
    if (darkest <= 64)
        return DIORAMA_TREE_SHADE_BLACK;
    if (darkest <= 140)
        return DIORAMA_TREE_SHADE_DARK;
    if (darkest <= 217)
        return DIORAMA_TREE_SHADE_LIGHT;
    return DIORAMA_TREE_SHADE_WHITE;
}

/*
 * The voxel tree model (tree_model.generated.c) has its colours baked, so the
 * palette's fade and the night tint passed it by: green trees on a black
 * screen, bright trees at night (KNOWN_ISSUES V5). Both act on every colour
 * of a palette alike (a blend towards a colour, a scale), so the trees' own
 * palette (BG palette 2, TreeShade's) gives the line each channel's faded
 * value follows from its unfaded one; the tree's colours go the same way.
 */
static void TreeTint(const struct DioramaSceneSnapshot *snapshot)
{
    for (int channel = 0; channel < 3; channel++)
    {
        float unfaded[15];
        float faded[15];
        float meanU = 0.0f;
        float meanF = 0.0f;
        float covariance = 0.0f;
        float variance = 0.0f;
        int shift = 16 - channel * 8; /* ConvertColor's 0xAARRGGBB: red, green, blue */

        for (int i = 0; i < 15; i++)
        {
            uint32_t u = DioramaMetatile_ConvertColor(snapshot->unfadedPalette[2 * 16 + 1 + i], false);
            uint32_t f = DioramaMetatile_ConvertColor(snapshot->fadedPalette[2 * 16 + 1 + i], false);

            unfaded[i] = ((u >> shift) & 0xFF) / 255.0f;
            faded[i] = ((f >> shift) & 0xFF) / 255.0f;
            meanU += unfaded[i] / 15.0f;
            meanF += faded[i] / 15.0f;
        }
        for (int i = 0; i < 15; i++)
        {
            covariance += (unfaded[i] - meanU) * (faded[i] - meanF);
            variance += (unfaded[i] - meanU) * (unfaded[i] - meanU);
        }
        sTreeScale[channel] = variance > 1e-4f ? covariance / variance : 1.0f;
        if (sTreeScale[channel] < 0.0f)
            sTreeScale[channel] = 0.0f;
        sTreeOffset[channel] = meanF - sTreeScale[channel] * meanU;
    }
    {
        static int sLogEnabled = -1;
        static float sLogged[6] = {-1.0f};

        if (sLogEnabled < 0)
        {
            const char *value = SDL_getenv("PKMN_DIORAMA_LOG");
            sLogEnabled = value != NULL && value[0] == '1';
        }
        if (sLogEnabled && (fabsf(sLogged[0] - sTreeScale[0]) > 0.02f || fabsf(sLogged[1] - sTreeScale[1]) > 0.02f
                         || fabsf(sLogged[2] - sTreeScale[2]) > 0.02f || fabsf(sLogged[3] - sTreeOffset[0]) > 0.02f
                         || fabsf(sLogged[4] - sTreeOffset[1]) > 0.02f || fabsf(sLogged[5] - sTreeOffset[2]) > 0.02f))
        {
            SDL_Log("Diorama: tree colours x(%.2f %.2f %.2f) + (%.2f %.2f %.2f)", sTreeScale[0], sTreeScale[1],
                    sTreeScale[2], sTreeOffset[0], sTreeOffset[1], sTreeOffset[2]);
            sLogged[0] = sTreeScale[0]; sLogged[1] = sTreeScale[1]; sLogged[2] = sTreeScale[2];
            sLogged[3] = sTreeOffset[0]; sLogged[4] = sTreeOffset[1]; sLogged[5] = sTreeOffset[2];
        }
    }
}

void DioramaGLTerrain_GetPaletteLine(float scale[3], float offset[3])
{
    int i;

    for (i = 0; i < 3; i++)
    {
        scale[i] = sTreeScale[i];
        offset[i] = sTreeOffset[i];
    }
}

static bool AppendPixelObjects(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaResolvedCell *resolvedCells,
                               int chunkX, int chunkY,
                               struct DioramaTerrainChunkInput *input)
{
    size_t objectIndex;

    for (objectIndex = 0; objectIndex < gDioramaPixelObjectV2Count; objectIndex++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[objectIndex];
        const struct DioramaGeneratedPixelV2 *pixels;
        const struct DioramaCellSnapshot *anchor = NULL;
        const struct DioramaResolvedCell *anchorResolved = NULL;
        bool supportActive;
        size_t pixelCount;
        size_t pixelIndex;
        int index;
        int translationX;
        int translationY;

        if (objectIndex < sizeof(sSyncAnchor) / sizeof(sSyncAnchor[0]))
        {
            /* Worked out once for the sync (PrepareSync). */
            index = sSyncAnchor[objectIndex];
            if (index >= 0)
            {
                anchor = &snapshot->cells[index];
                anchorResolved = &resolvedCells[index];
            }
        }
        else
        {
            for (index = 0; index < snapshot->visibleCellCount; index++)
                if (resolvedCells[index].pixelObjectId == object->id)
                {
                    anchor = &snapshot->cells[index];
                    anchorResolved = &resolvedCells[index];
                    break;
                }
        }
        if (anchor == NULL || !(anchor->flags & DIORAMA_CELL_SOURCE_VALID))
            continue;
        if (object->layoutId != anchor->sourceLayoutId
         || !((object->mapGroup == UINT8_MAX && object->mapNumber == UINT8_MAX)
           || (object->mapGroup == anchor->sourceMapGroup
            && object->mapNumber == anchor->sourceMapNum)))
            continue;
        if (objectIndex < sizeof(sSyncSupportActive) / sizeof(sSyncSupportActive[0]))
            supportActive = sSyncSupportActive[objectIndex];
        else
        {
            supportActive = object->supportStructureId == 0;
            for (index = 0; !supportActive && index < snapshot->visibleCellCount; index++)
                supportActive = resolvedCells[index].structureId == object->supportStructureId;
        }
        if (!supportActive)
            continue;
        translationX = anchor->mapX - anchor->sourceMapX;
        translationY = anchor->mapY - anchor->sourceMapY;
        pixels = DioramaRules_GetPixelObjectPixels(object, &pixelCount);
        if (pixels == NULL)
            return false;
        for (pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++)
        {
            const struct DioramaGeneratedPixelV2 *sourcePixel = &pixels[pixelIndex];
            int sourceX = sourcePixel->sourceCellOffset
                        % gDioramaLayoutsV2[object->layoutId - 1].width;
            int sourceY = sourcePixel->sourceCellOffset
                        / gDioramaLayoutsV2[object->layoutId - 1].width;
            const struct DioramaCellSnapshot *sourceCell = FindSnapshotCell(
                snapshot, sourceX + translationX, sourceY + translationY);
            int32_t xQ32 = sourcePixel->xQ32 + translationX * 32;
            int32_t zQ32 = sourcePixel->zQ32 + translationY * 32;
            int ownerChunkX = DioramaTerrain_FloorDiv(
                xQ32 + sourcePixel->sizeXQ32 / 2, DIORAMA_TERRAIN_CHUNK_SIZE * 32);
            int ownerChunkY = DioramaTerrain_FloorDiv(
                zQ32 + sourcePixel->sizeZQ32 / 2, DIORAMA_TERRAIN_CHUNK_SIZE * 32);
            struct DioramaTerrainPixelPrimitive *target;
            uint32_t color;

            if (ownerChunkX != chunkX || ownerChunkY != chunkY)
                continue;
            sCandidatePixels++;
            if (sourceCell == NULL || sourceCell->sourceLayoutId != object->layoutId
             || sourceCell->metatileId != sourcePixel->expectedMetatile)
                continue;
            color = PixelColor(snapshot, sourceCell, sourcePixel);
            if (color == 0)
                continue;
            if (input->pixelCount >= DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES)
                return false;
            target = &input->pixels[input->pixelCount++];
            target->xQ32 = xQ32;
            target->yQ32 = sourcePixel->yQ32 + object->supportOffsetQ16 * 2
                         + (int32_t)(anchorResolved->groundHeight * 32.0f);
            target->zQ32 = zQ32;
            target->rgba = color;
            target->sourceCellOffset = sourcePixel->sourceCellOffset;
            target->objectId = object->id;
            target->structureId = object->structureId;
            target->expectedMetatile = sourcePixel->expectedMetatile;
            target->expectedTileEntry = sourcePixel->expectedTileEntry;
            target->sizeXQ32 = sourcePixel->sizeXQ32;
            target->sizeYQ32 = sourcePixel->sizeYQ32;
            target->sizeZQ32 = sourcePixel->sizeZQ32;
            target->sourceLayer = sourcePixel->sourceLayer;
            target->sourceX = sourcePixel->sourceX;
            target->sourceY = sourcePixel->sourceY;
            target->kind = object->kind;
        }
    }
    return true;
}

static void BuildMountainArtMask(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaCellSnapshot *source,
                                 uint8_t terraceProfile,
                                 uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                 uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                               * DIORAMA_VOXELS_PER_CELL])
{
    uint32_t pixels[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint16_t remaining[DIORAMA_VOXELS_PER_CELL];
    uint16_t eroded[DIORAMA_VOXELS_PER_CELL];
    unsigned visible = 0;
    unsigned depth = 0;
    unsigned maxDepth = 0;
    int row;
    struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, source);

    DioramaMetatile_ComposeLayer(art.tileGraphics, source->tileEntries,
                                 art.unfadedPalette, 1, pixels);
    DioramaMetatile_BuildAlphaMask(pixels, rows);
    for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        for (uint16_t bits = rows[row]; bits != 0; bits >>= 1)
            visible += bits & 1;
    if (terraceProfile != DIORAMA_TERRACE_NONE)
    {
        DioramaTerrain_BuildTerraceHeightmap(terraceProfile, rows,
            visible != 0 && visible != DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL,
            rows, heights);
        return;
    }

    if (visible == 0 || visible == DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
    {
        DioramaMetatile_Compose(art.tileGraphics, source->tileEntries,
                                art.unfadedPalette, pixels);
        DioramaMetatile_BuildSilhouetteMask(pixels, rows);
    }

    memcpy(remaining, rows, sizeof(remaining));
    memset(heights, 0, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL);
    while (true)
    {
        bool any = false;

        depth++;
        memset(eroded, 0, sizeof(eroded));
        for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
        {
            uint16_t bits = remaining[row];

            if (bits == 0)
                continue;
            any = true;
            for (int x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
                if (bits & (1u << x))
                    heights[row * DIORAMA_VOXELS_PER_CELL + x] = depth;
            if (row != 0 && row + 1 < DIORAMA_VOXELS_PER_CELL)
                eroded[row] = bits & (uint16_t)(bits << 1) & (bits >> 1)
                            & remaining[row - 1] & remaining[row + 1];
        }
        if (!any)
            break;
        maxDepth = depth;
        memcpy(remaining, eroded, sizeof(remaining));
    }
    if (maxDepth == 0)
        return;
    for (int pixel = 0;
         pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
    {
        unsigned pixelDepth = heights[pixel];

        if (pixelDepth == 0)
            continue;
        heights[pixel] = maxDepth == 1 ? DIORAMA_VOXELS_PER_CELL
            : 4 + (pixelDepth - 1) * (DIORAMA_VOXELS_PER_CELL - 4)
                / (maxDepth - 1);
    }
}

static bool BuildInput(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolvedCells,
                       int chunkX, int chunkY,
                       struct DioramaTerrainChunkInput *input)
{
    int originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE;
    int originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE;
    int y;
    int x;
    int face;
    bool hasInterior = false;

    /* The header and the cells; the pixel primitives are read only up to
     * pixelCount (zero here), each written whole when appended. */
    memset(input, 0, offsetof(struct DioramaTerrainChunkInput, pixels));
    sMaterialPage = 0;
    input->chunkX = chunkX;
    input->chunkY = chunkY;
    input->mapGeneration = snapshot->mapGeneration;
    input->rulesGeneration = DioramaRules_GetGeneration();
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
    {
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            int mapX = originX + x - DIORAMA_TERRAIN_HALO;
            int mapY = originY + y - DIORAMA_TERRAIN_HALO;
            int gridX = mapX - snapshot->gridOriginX;
            int gridY = mapY - snapshot->gridOriginY;
            int gridIndex;
            const struct DioramaCellSnapshot *source;
            struct DioramaTerrainCell *cell;

            cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
            cell->mapX = mapX;
            cell->mapY = mapY;
            if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
                continue;
            gridIndex = gridY * DIORAMA_GRID_WIDTH + gridX;
            if (gridIndex >= snapshot->visibleCellCount)
                continue;
            source = &snapshot->cells[gridIndex];
            if (source->mapX != mapX || source->mapY != mapY || source->metatileId >= DIORAMA_TILE_COUNT)
                continue;
            cell->present = true;
            sMaterialPage = source->atlasPage;
            cell->mapX = source->mapX;
            cell->mapY = source->mapY;
            cell->metatileId = source->metatileId;
            cell->behavior = source->behavior;
            cell->layerType = source->layerType;
            cell->collision = source->collision;
            cell->rawElevation = source->elevation;
            cell->reflective = MetatileBehavior_IsReflective(source->behavior);
            cell->shape = resolvedCells[gridIndex].shape;
            cell->archetype = resolvedCells[gridIndex].archetype;
            cell->terrainClass = resolvedCells[gridIndex].terrainClass;
            cell->semanticProfile = resolvedCells[gridIndex].semanticProfile;
            cell->profile = resolvedCells[gridIndex].profile;
            cell->planeAxis = resolvedCells[gridIndex].planeAxis;
            cell->effectiveElevation = resolvedCells[gridIndex].effectiveElevation;
            cell->surfaceCount = resolvedCells[gridIndex].surfaceCount;
            cell->claimOwner = resolvedCells[gridIndex].claimOwner;
            cell->regionId = resolvedCells[gridIndex].regionId;
            cell->structureId = resolvedCells[gridIndex].structureId;
            cell->rulePriority = resolvedCells[gridIndex].rulePriority;
            cell->structureTemplateId = resolvedCells[gridIndex].structureTemplateId;
            cell->structureX = DioramaTerrain_VisibleStructureOrigin(
                source->mapX, resolvedCells[gridIndex].structureLocalX);
            cell->structureY = DioramaTerrain_VisibleStructureOrigin(
                source->mapY, resolvedCells[gridIndex].structureLocalY);
            cell->structureWidth = resolvedCells[gridIndex].structureWidth;
            cell->structureHeight = resolvedCells[gridIndex].structureHeight;
            cell->structureRoofRows = resolvedCells[gridIndex].structureRoofRows;
            cell->structureLocalX = resolvedCells[gridIndex].structureLocalX;
            cell->structureLocalY = resolvedCells[gridIndex].structureLocalY;
            cell->structureOwnerKind = resolvedCells[gridIndex].structureOwnerKind;
            cell->doorFold = resolvedCells[gridIndex].doorFold;
            cell->voidKind = resolvedCells[gridIndex].voidKind;
            cell->cliffEdgeMask = resolvedCells[gridIndex].cliffEdgeMask;
            cell->cliffBaseMask = resolvedCells[gridIndex].cliffBaseMask;
            cell->cliffTransitionMask = resolvedCells[gridIndex].cliffTransitionMask;
            cell->cliffCornerMask = resolvedCells[gridIndex].cliffCornerMask;
            cell->terraceProfile = resolvedCells[gridIndex].terraceProfile;
            cell->groundHeight = resolvedCells[gridIndex].groundHeight;
            cell->visualHeight = resolvedCells[gridIndex].topHeight;
            cell->featureHeight = resolvedCells[gridIndex].featureHeight;
            cell->structureBodyHeight = resolvedCells[gridIndex].structureBodyHeight;
            cell->structureRoofHeight = resolvedCells[gridIndex].structureRoofHeight;
            cell->southFacadeUnitHeight = resolvedCells[gridIndex].structureSouthFacadeUnitHeight;
            cell->measuredAxis = resolvedCells[gridIndex].measuredAxis;
            cell->measuredExtentBands = resolvedCells[gridIndex].measuredExtentBands;
            cell->measuredPeriodBands = resolvedCells[gridIndex].measuredPeriodBands;
            cell->measuredRoofBands = resolvedCells[gridIndex].measuredRoofBands;
            cell->measuredRunLocal = resolvedCells[gridIndex].measuredRunLocal;
            cell->measuredRunLength = resolvedCells[gridIndex].measuredRunLength;
            cell->measuredFlags = resolvedCells[gridIndex].measuredFlags;
            cell->measuredBandCount = resolvedCells[gridIndex].measuredBandCount;
            cell->measuredConfidence = resolvedCells[gridIndex].measuredConfidence;
            for (face = 0; face < cell->surfaceCount; face++)
            {
                cell->surfaces[face].bottomHeight = resolvedCells[gridIndex].surfaces[face].bottomHeight;
                cell->surfaces[face].topHeight = resolvedCells[gridIndex].surfaces[face].topHeight;
                cell->surfaces[face].gameplayElevation = resolvedCells[gridIndex].surfaces[face].gameplayElevation;
            }
            for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
            {
                uint16_t materialId = resolvedCells[gridIndex].materials[face].metatileId;
                uint8_t materialLayer = resolvedCells[gridIndex].materials[face].layer;

                if (resolvedCells[gridIndex].pixelObjectId != 0
                 && resolvedCells[gridIndex].groundMode == DIORAMA_PIXEL_GROUND_SOURCE_BASE)
                {
                    materialId = source->metatileId;
                    materialLayer = DIORAMA_MATERIAL_BASE;
                }
                else if (resolvedCells[gridIndex].pixelObjectId != 0
                      && resolvedCells[gridIndex].baseMetatileId != DIORAMA_MATERIAL_METATILE_SELF)
                {
                    materialId = resolvedCells[gridIndex].baseMetatileId;
                    materialLayer = DIORAMA_MATERIAL_FULL;
                }
                if (materialId == DIORAMA_MATERIAL_METATILE_SELF)
                    materialId = source->metatileId;
                SetTerrainMaterialTransformed(
                    &cell->materials[face], materialId,
                    materialLayer,
                    resolvedCells[gridIndex].materials[face].rotation,
                    resolvedCells[gridIndex].materials[face].flags);
                SetTerrainMaterial(&cell->underlayMaterials[face], source->metatileId,
                                   DIORAMA_MATERIAL_BASE);
            }
            {
                /* A detail object stands here (props.h, gl_props.c): the
                 * ground without the flat picture of it. */
                uint16_t propGround;
                uint8_t propLayer;

                if (resolvedCells[gridIndex].pixelObjectId == 0
                 && DioramaProps_GroundMaterial(snapshot, gridIndex, &propGround, &propLayer))
                    SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP], propGround,
                                       propLayer);
            }
            for (face = 0; face < cell->measuredBandCount; face++)
            {
                float middle;

                SetTerrainMaterial(&cell->measuredBands[face],
                    resolvedCells[gridIndex].measuredBands[face].metatileId,
                    resolvedCells[gridIndex].measuredBands[face].layer);
                if (cell->measuredAxis == DIORAMA_PLANE_AXIS_Z)
                    middle = (cell->measuredBands[face].v0
                            + cell->measuredBands[face].v1) * 0.5f;
                else
                    middle = (cell->measuredBands[face].u0
                            + cell->measuredBands[face].u1) * 0.5f;
                if (cell->measuredAxis == DIORAMA_PLANE_AXIS_Z)
                {
                    if (resolvedCells[gridIndex].measuredBands[face].sourceHalf == 0)
                        cell->measuredBands[face].v1 = middle;
                    else
                        cell->measuredBands[face].v0 = middle;
                }
                else
                {
                    if (resolvedCells[gridIndex].measuredBands[face].sourceHalf == 0)
                        cell->measuredBands[face].u1 = middle;
                    else
                        cell->measuredBands[face].u0 = middle;
                    cell->measuredBands[face].rotation = 1;
                }
            }
            if (cell->measuredFlags & DIORAMA_MEASURED_SILHOUETTE)
            {
                bool hasForeground = false;

                DioramaAtlas_GetSilhouetteAlphaMask(DioramaAtlas_CellSlot(source),
                                                    cell->foregroundAlpha);
                for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                    hasForeground |= cell->foregroundAlpha[face] != 0;
                if (!hasForeground)
                    for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                        cell->foregroundAlpha[face] = 0xFFFF;
            }
            if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                BuildMountainArtMask(snapshot, source, cell->terraceProfile,
                                     cell->foregroundAlpha,
                                     cell->mountainHeight);
            if (cell->shape == DIORAMA_SHAPE_LEDGE)
            {
                DioramaAtlas_GetForegroundAlphaMask(DioramaAtlas_CellSlot(source),
                                                    cell->foregroundAlpha);
                SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP],
                                   source->metatileId, DIORAMA_MATERIAL_BASE);
                for (face = DIORAMA_MATERIAL_FACE_NORTH;
                     face <= DIORAMA_MATERIAL_FACE_WEST; face++)
                    SetTerrainMaterial(&cell->materials[face], source->metatileId,
                                        DIORAMA_MATERIAL_FOREGROUND);
            }
            if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
             || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int pixel;

                SetTerrainMaterial(&cell->treeMaterial, source->metatileId,
                                   DIORAMA_MATERIAL_FULL);
                cell->treeMaskDirect = true;
                for (pixel = 0;
                     pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
                     pixel++)
                    cell->treeShade[pixel] = TreeShade(
                        snapshot, source, pixel % DIORAMA_VOXELS_PER_CELL,
                        pixel / DIORAMA_VOXELS_PER_CELL);
            }
            else if (cell->shape == DIORAMA_SHAPE_CLIFF)
            {
                uint16_t alpha[DIORAMA_VOXELS_PER_CELL];
                bool hasForeground = false;

                DioramaAtlas_GetForegroundAlphaMask(DioramaAtlas_CellSlot(source), alpha);
                for (face = 0; face < DIORAMA_VOXELS_PER_CELL; face++)
                    hasForeground |= alpha[face] != 0;
                SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP],
                                   source->metatileId,
                                   cell->terraceProfile != DIORAMA_TERRACE_NONE
                                     ? DIORAMA_MATERIAL_FULL
                                     : DIORAMA_MATERIAL_BASE);
                if (hasForeground)
                    for (face = DIORAMA_MATERIAL_FACE_NORTH;
                         face <= DIORAMA_MATERIAL_FACE_WEST; face++)
                        SetTerrainMaterial(&cell->materials[face], source->metatileId,
                            cell->terraceProfile != DIORAMA_TERRACE_NONE
                              ? DIORAMA_MATERIAL_FULL : DIORAMA_MATERIAL_FOREGROUND);
            }
            if (resolvedCells[gridIndex].structureSouthFacadeRows != 0
             && cell->structureLocalY == cell->structureHeight - 1)
            {
                uint8_t facadeRow;

                for (facadeRow = 0;
                     facadeRow < resolvedCells[gridIndex].structureSouthFacadeRows;
                     facadeRow++)
                {
                    const struct DioramaCellSnapshot *facadeSource = FindSnapshotCell(
                        snapshot, cell->structureX + cell->structureLocalX,
                        cell->structureY + cell->structureHeight - 1 - facadeRow);
                    uint16_t materialId = resolvedCells[gridIndex]
                        .materials[DIORAMA_MATERIAL_FACE_SOUTH].metatileId;

                    if (facadeSource == NULL || facadeSource->metatileId >= DIORAMA_TILE_COUNT)
                        break;
                    if (materialId == DIORAMA_MATERIAL_METATILE_SELF)
                        materialId = facadeSource->metatileId;
                    sMaterialPage = facadeSource->atlasPage;
                    SetTerrainMaterial(&cell->southFacadeMaterials[facadeRow], materialId,
                        resolvedCells[gridIndex].materials[DIORAMA_MATERIAL_FACE_SOUTH].layer);
                }
                sMaterialPage = source->atlasPage;
                if (facadeRow == resolvedCells[gridIndex].structureSouthFacadeRows)
                    cell->southFacadeCount = facadeRow;
            }
            if (x >= DIORAMA_TERRAIN_HALO && x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE
             && y >= DIORAMA_TERRAIN_HALO && y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE)
                hasInterior = true;
        }
    }
    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            struct DioramaTerrainCell *cell =
                &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];

            if (!cell->present)
                continue;
            if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
            {
                if (cell->metatileId == 198 || cell->metatileId == 199)
                    continue;
                for (face = 0;
                     !cell->treeHullReady
                     && face < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
                     face++)
                    cell->treeHullReady = cell->treeShade[face] != DIORAMA_TREE_SHADE_OFF;
            }
            else if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
                int bodyStart = cell->structureHeight - bodyRows;
                bool ready = cell->structureId != 0 && cell->structureWidth > 0
                          && cell->structureWidth <= 2;

                for (int sourceY = 0; ready && sourceY < bodyRows; sourceY++)
                    for (int sourceX = 0; ready && sourceX < cell->structureWidth; sourceX++)
                    {
                        struct DioramaTerrainCell *sourceCell = FindTerrainInputCell(
                            input, cell->structureX + sourceX,
                            cell->structureY + bodyStart + sourceY);

                        ready = sourceCell != NULL
                             && sourceCell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                             && sourceCell->structureId == cell->structureId;
                    }
                cell->treeHullReady = ready;
            }
            if ((cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
              || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
             && !cell->treeHullReady
             && !(cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
               && cell->structureWidth == 2))
            {
                const struct DioramaCellSnapshot *source = FindSnapshotCell(snapshot, cell->mapX,
                                                                            cell->mapY);

                sMaterialPage = source != NULL ? source->atlasPage : 0;
                for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                    SetTerrainMaterial(&cell->materials[face], cell->metatileId,
                                       DIORAMA_MATERIAL_FULL);
            }
        }
    sMaterialPage = 0;
    {
        /* The commonest flat ground on screen, by atlas slot (PrepareSync). */
        uint16_t groundMetatile = sSyncGroundSlot;

        for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
            for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
            {
                struct DioramaTerrainCell *cell =
                    &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];

                if (cell->present
                 && (cell->treeHullReady
                  || (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                   && cell->structureWidth == 2)))
                    for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                        SetTerrainMaterialSlot(&cell->underlayMaterials[face], groundMetatile,
                                               DIORAMA_MATERIAL_FULL);
            }
    }
    return hasInterior && AppendPixelObjects(snapshot, resolvedCells, chunkX, chunkY, input);
}

static bool UploadChunk(struct GLTerrainChunk *chunk,
                        const struct DioramaTerrainChunkInput *input,
                        uint64_t signature)
{
    struct DioramaTerrainMesh mesh;

    if (!DioramaTerrain_BuildChunk(input, sScratchVertices,
                                   DIORAMA_TERRAIN_MAX_VERTICES, &mesh))
        return false;
    if (chunk->vertexArray == 0)
    {
        dglGenVertexArrays(1, &chunk->vertexArray);
        dglGenBuffers(1, &chunk->vertexBuffer);
        ConfigureVertexArray(chunk->vertexArray, chunk->vertexBuffer);
    }
    dglBindBuffer(GL_ARRAY_BUFFER, chunk->vertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER,
                  mesh.vertexCount * sizeof(struct DioramaTerrainVertex),
                  sScratchVertices, GL_DYNAMIC_DRAW);
    chunk->occupied = true;
    chunk->stale = false;
    chunk->chunkX = input->chunkX;
    chunk->chunkY = input->chunkY;
    chunk->mapKey = sSyncMapKey;
    chunk->mapGeneration = input->mapGeneration;
    chunk->rulesGeneration = input->rulesGeneration;
    chunk->signature = signature;
    chunk->mesh = mesh;
    return true;
}

bool DioramaGLTerrain_Init(void)
{
    memset(sChunks, 0, sizeof(sChunks));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sMapGeneration = 0;
    sMapEditGeneration = 0;
    sRulesGeneration = 0;
    sLastSyncSequence = 0;
    if (!CreateProgram())
        return false;
    if (!CreateTreeBuffers())
        return false;
    dglGenVertexArrays(1, &sDebugVertexArray);
    dglGenBuffers(1, &sDebugVertexBuffer);
    ConfigureVertexArray(sDebugVertexArray, sDebugVertexBuffer);
    dglGenVertexArrays(1, &sFarVertexArray);
    dglGenBuffers(1, &sFarVertexBuffer);
    ConfigureVertexArray(sFarVertexArray, sFarVertexBuffer);
    DioramaFar_SetBuildingHeight(FarBuildingHeight);
    sFarBuilt = false;
    sFarShown = false;
    return true;
}

static bool FarBuildingHeight(uint16_t layoutId, int x, int y, float *top)
{
    uint16_t ground;
    uint8_t quarters;

    return DioramaBuildings_CellAt(layoutId, x, y, &ground, &quarters, top);
}

/* The distant scenery for a snapshot: drawn with the effects quality on
 * (gl_quality.h), outdoors (the snapshot has far blocks only there). */
static void SyncFar(const struct DioramaSceneSnapshot *snapshot)
{
    bool complete;

    sFarShown = snapshot->farOn && DioramaGLQuality_Effects();
    if (!sFarShown)
        return;
    if (sFarBuilt && snapshot->farGeneration == sFarGeneration
     && snapshot->gridOriginX == sFarGridX && snapshot->gridOriginY == sFarGridY)
        return;
    sFarVertexCount = DioramaFar_BuildMesh(snapshot, sFarVertices, DIORAMA_FAR_MAX_VERTICES,
                                           sFarSectors, &complete);
    dglBindBuffer(GL_ARRAY_BUFFER, sFarVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sFarVertexCount * sizeof(*sFarVertices), sFarVertices,
                  GL_STATIC_DRAW);
    sFarGeneration = snapshot->farGeneration;
    sFarGridX = snapshot->gridOriginX;
    sFarGridY = snapshot->gridOriginY;
    sFarBuilt = true;
    if (!complete)
        SDL_Log("Diorama: distant scenery cut short at %u vertices", sFarVertexCount);
    {
        static int sLog = -1;

        if (sLog < 0)
        {
            const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

            sLog = value != NULL && value[0] == '1';
        }
        if (sLog && sFarVertexCount != 0 && !sFarLogged)
        {
            SDL_Log("Diorama: distant scenery drawn: %u vertices", sFarVertexCount);
            sFarLogged = true;
        }
    }
}

bool DioramaGLTerrain_FarShown(void)
{
    return sFarShown && sFarVertexCount != 0;
}

void DioramaGLTerrain_Reset(void)
{
    unsigned i;

    for (i = 0; i < TERRAIN_CHUNK_POOL_MAX; i++)
    {
        sChunks[i].occupied = false;
        sChunks[i].active = false;
        sChunks[i].signature = 0;
    }
    memset(&sMetrics, 0, sizeof(sMetrics));
    sGeometryGeneration++;
    sSyncKeyValid = false;
    sPendingCount = 0;
}

/* Another map: none of the chunks is in view any more, all stay cached. */
static void LeaveMap(void)
{
    unsigned i;

    for (i = 0; i < TERRAIN_CHUNK_POOL_MAX; i++)
        sChunks[i].active = false;
    sGeometryGeneration++;
    sSyncKeyValid = false;
    sPendingCount = 0;
    sMapJustChanged = true;
    sHitsAtChange = sCacheHits;
}

static void ReleaseChunk(struct GLTerrainChunk *chunk)
{
    if (chunk->vertexBuffer != 0)
        dglDeleteBuffers(1, &chunk->vertexBuffer);
    if (chunk->vertexArray != 0)
        dglDeleteVertexArrays(1, &chunk->vertexArray);
    memset(chunk, 0, sizeof(*chunk));
}

void DioramaGLTerrain_SetCacheChunks(unsigned chunks)
{
    unsigned i;

    if (chunks < TERRAIN_GRID_CHUNKS)
        chunks = TERRAIN_GRID_CHUNKS;
    if (chunks > TERRAIN_CHUNK_POOL_MAX)
        chunks = TERRAIN_CHUNK_POOL_MAX;
    if (chunks == sChunkLimit)
        return;
    /* A smaller cache gives its last slots' memory back; whatever was in view
     * there is built again at the next sync. */
    for (i = chunks; i < sChunkLimit; i++)
        ReleaseChunk(&sChunks[i]);
    sChunkLimit = chunks;
    sSyncKeyValid = false;
    SDL_Log("Diorama: terrain cache %u chunks", chunks);
}

unsigned DioramaGLTerrain_CacheChunks(void)
{
    return sChunkLimit;
}

uint32_t DioramaGLTerrain_GetGeometryGeneration(void)
{
    return sGeometryGeneration;
}

/* PKMN_DIORAMA_LOG=1: how often a sync had nothing to rebuild. */
static bool LogEnabledTerrain(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

        sEnabled = value != NULL && value[0] == '1';
    }
    return sEnabled;
}

/* What every chunk's input shares, once per sync. */
static void PrepareSync(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolvedCells)
{
    static uint16_t counts[DIORAMA_ATLAS_SLOT_COUNT];
    size_t objectCount = gDioramaPixelObjectV2Count;
    size_t objectIndex;
    int index;

    memset(counts, 0, sizeof(counts));
    sSyncGroundSlot = 1;
    for (index = 0; index < snapshot->visibleCellCount && index < DIORAMA_MAX_VISIBLE_CELLS; index++)
    {
        uint16_t slot = DioramaAtlas_CellSlot(&snapshot->cells[index]);

        if (slot < DIORAMA_ATLAS_SLOT_COUNT
         && resolvedCells[index].shape == DIORAMA_SHAPE_FLAT
         && resolvedCells[index].archetype == DIORAMA_ARCHETYPE_GROUND)
            counts[slot]++;
    }
    for (uint16_t slot = 0; slot < DIORAMA_ATLAS_SLOT_COUNT; slot++)
        if (counts[slot] > counts[sSyncGroundSlot])
            sSyncGroundSlot = slot;
    if (objectCount > sizeof(sSyncAnchor) / sizeof(sSyncAnchor[0]))
        objectCount = sizeof(sSyncAnchor) / sizeof(sSyncAnchor[0]);
    for (objectIndex = 0; objectIndex < objectCount; objectIndex++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[objectIndex];
        bool supportActive = object->supportStructureId == 0;

        sSyncAnchor[objectIndex] = -1;
        for (index = 0; index < snapshot->visibleCellCount; index++)
            if (resolvedCells[index].pixelObjectId == object->id)
            {
                sSyncAnchor[objectIndex] = (int16_t)index;
                break;
            }
        for (index = 0; !supportActive && index < snapshot->visibleCellCount; index++)
            supportActive = resolvedCells[index].structureId == object->supportStructureId;
        sSyncSupportActive[objectIndex] = supportActive;
    }
}

/* Whether a cell's input reads its tiles' pixels (BuildInput: tree shades,
 * mountain art, silhouette and foreground masks of ledges and cliffs). */
static bool CellReadsPixels(const struct DioramaResolvedCell *resolved)
{
    return resolved->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
        || resolved->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
        || (resolved->measuredFlags & (DIORAMA_MEASURED_SILHOUETTE | DIORAMA_MEASURED_MOUNTAIN_ART))
        || resolved->shape == DIORAMA_SHAPE_LEDGE
        || resolved->shape == DIORAMA_SHAPE_CLIFF;
}

/* After BuildInput: what of the tiles' pixels the chunk's input read. */
static void NotePixelUse(struct GLTerrainChunk *chunk, const struct DioramaTerrainChunkInput *input)
{
    int i;

    chunk->hasPixelObjects = sCandidatePixels != 0;
    chunk->readsPixelCells = false;
    for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
    {
        const struct DioramaTerrainCell *cell = &input->cells[i];

        if (cell->present
         && (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
          || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
          || (cell->measuredFlags & (DIORAMA_MEASURED_SILHOUETTE | DIORAMA_MEASURED_MOUNTAIN_ART))
          || cell->shape == DIORAMA_SHAPE_LEDGE || cell->shape == DIORAMA_SHAPE_CLIFF))
        {
            chunk->readsPixelCells = true;
            return;
        }
    }
}

/* Whether a cell of a chunk's input area that reads pixels uses a tile that
 * changed (sSyncDirtyTiles). */
static bool ChunkUsesDirtyTiles(const struct DioramaSceneSnapshot *snapshot,
                                const struct DioramaResolvedCell *resolvedCells,
                                int chunkX, int chunkY)
{
    int originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE - DIORAMA_TERRAIN_HALO;
    int originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE - DIORAMA_TERRAIN_HALO;
    int x;
    int y;

    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            int gridX = originX + x - snapshot->gridOriginX;
            int gridY = originY + y - snapshot->gridOriginY;
            int index;
            const struct DioramaCellSnapshot *cell;
            int entry;

            if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
                continue;
            index = gridY * DIORAMA_GRID_WIDTH + gridX;
            if (index >= snapshot->visibleCellCount || !CellReadsPixels(&resolvedCells[index]))
                continue;
            cell = &snapshot->cells[index];
            for (entry = 0; entry < DIORAMA_METATILE_ENTRY_COUNT; entry++)
            {
                unsigned tile = cell->tileEntries[entry] & 0x3FF;

                if (sSyncDirtyTiles[tile / 8] & (1 << (tile % 8)))
                    return true;
            }
        }
    return false;
}

/* The chunks a change of the animated tiles alone can reach, built again;
 * the rest stand. */
static bool SyncAnimatedTiles(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaResolvedCell *resolvedCells,
                              const struct TerrainSyncKey *key)
{
    static struct DioramaTerrainChunkInput input;
    unsigned tile;
    unsigned rebuilt = 0;
    bool prepared = false;
    int i;

    memset(sSyncDirtyTiles, 0, sizeof(sSyncDirtyTiles));
    for (tile = 0; tile < DIORAMA_TILE_COUNT; tile++)
        if (memcmp(snapshot->tileGraphics + tile * DIORAMA_TILE_BYTES, sSyncTiles + tile * DIORAMA_TILE_BYTES,
                   DIORAMA_TILE_BYTES) != 0)
            sSyncDirtyTiles[tile / 8] |= 1 << (tile % 8);
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];
        uint64_t signature;

        if (!chunk->active)
            continue;
        chunk->lastSeenSequence = snapshot->sequence;
        /* Pixel objects take colours from cells anywhere: their chunks always look again. */
        if (!chunk->hasPixelObjects
         && !(chunk->readsPixelCells
           && ChunkUsesDirtyTiles(snapshot, resolvedCells, chunk->chunkX, chunk->chunkY)))
            continue;
        if (!prepared)
        {
            TreeTint(snapshot);
            PrepareSync(snapshot, resolvedCells);
            prepared = true;
        }
        sCandidatePixels = 0;
        if (!BuildInput(snapshot, resolvedCells, chunk->chunkX, chunk->chunkY, &input))
            continue;
        signature = DioramaTerrain_ChunkSignature(&input);
        if (signature != chunk->signature)
        {
            if (!UploadChunk(chunk, &input, signature))
            {
                sSyncKeyValid = false;
                return false;
            }
            rebuilt++;
        }
        NotePixelUse(chunk, &input);
    }
    sMetrics.rebuiltChunks = rebuilt;
    if (rebuilt != 0)
        sGeometryGeneration++;
    sSyncKey.tilesetAnimationGeneration = key->tilesetAnimationGeneration;
    memcpy(sSyncTiles, snapshot->tileGraphics, sizeof(sSyncTiles));
    sMapEditGeneration = snapshot->mapEditGeneration;
    sLastSyncSequence = snapshot->sequence;
    return sMetrics.activeChunks != 0;
}

/* A chunk's place: its mesh's bounds, or the cells it covers when it has none. */
static struct DioramaTerrainBounds ChunkBounds(const struct GLTerrainChunk *chunk, int chunkX, int chunkY)
{
    struct DioramaTerrainBounds bounds;

    if (chunk != NULL && chunk->occupied && chunk->mesh.vertexCount != 0)
        return chunk->mesh.bounds;
    bounds.minX = (float)(chunkX * DIORAMA_TERRAIN_CHUNK_SIZE);
    bounds.maxX = bounds.minX + DIORAMA_TERRAIN_CHUNK_SIZE;
    bounds.maxZ = -(float)(chunkY * DIORAMA_TERRAIN_CHUNK_SIZE);
    bounds.minZ = bounds.maxZ - DIORAMA_TERRAIN_CHUNK_SIZE;
    bounds.minY = -1.0f;
    bounds.maxY = 8.0f;
    return bounds;
}

/* Whether a chunk's rebuild cannot wait: near the player or on screen. */
static bool ChunkUrgent(const struct DioramaSceneSnapshot *snapshot, const struct GLTerrainChunk *chunk,
                        int chunkX, int chunkY)
{
    int centreX = snapshot->gridOriginX + DIORAMA_GRID_WIDTH / 2;
    int centreY = snapshot->gridOriginY + DIORAMA_GRID_HEIGHT / 2;
    int middleX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_CHUNK_SIZE / 2;
    int middleY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_CHUNK_SIZE / 2;
    struct DioramaTerrainBounds bounds;

    if (!sCameraKnownForSync)
        return true;
    if (abs(middleX - centreX) <= TERRAIN_NEAR_CELLS && abs(middleY - centreY) <= TERRAIN_NEAR_CELLS)
        return true;
    bounds = ChunkBounds(chunk, chunkX, chunkY);
    return DioramaTerrain_IsBoundsVisible(&bounds, sSyncCameraX, sSyncCameraZ, sSyncCameraPitch,
                                          sSyncFocalLength);
}

static void AddPending(int chunkX, int chunkY)
{
    unsigned p;

    for (p = 0; p < sPendingCount; p++)
        if (sPending[p][0] == chunkX && sPending[p][1] == chunkY)
            return;
    if (sPendingCount < TERRAIN_MAX_PENDING)
    {
        sPending[sPendingCount][0] = (int16_t)chunkX;
        sPending[sPendingCount][1] = (int16_t)chunkY;
        sPendingCount++;
    }
}

/*
 * After the grid moved and nothing else changed: whether every cell of a
 * chunk's input area is what it was (the same cells, resolved the same, in
 * view then and now), so its input would be built the same and the chunk can
 * stand. Not for a chunk with pixel objects (their anchors are grid indices)
 * or a south facade (it reads cells past the area).
 */
static bool RegionUnchanged(const struct DioramaSceneSnapshot *snapshot,
                            const struct DioramaResolvedCell *resolvedCells,
                            const struct TerrainSyncKey *previous, int chunkX, int chunkY)
{
    int originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE - DIORAMA_TERRAIN_HALO;
    int originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE - DIORAMA_TERRAIN_HALO;
    int x;
    int y;

    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            int mapX = originX + x;
            int mapY = originY + y;
            int newX = mapX - snapshot->gridOriginX;
            int newY = mapY - snapshot->gridOriginY;
            int oldX = mapX - previous->gridOriginX;
            int oldY = mapY - previous->gridOriginY;
            int newIndex = newY * DIORAMA_GRID_WIDTH + newX;
            int oldIndex = oldY * DIORAMA_GRID_WIDTH + oldX;
            bool inNew = newX >= 0 && newY >= 0 && newX < DIORAMA_GRID_WIDTH && newY < DIORAMA_GRID_HEIGHT
                      && newIndex < snapshot->visibleCellCount
                      && snapshot->cells[newIndex].mapX == mapX && snapshot->cells[newIndex].mapY == mapY;
            bool inOld = oldX >= 0 && oldY >= 0 && oldX < DIORAMA_GRID_WIDTH && oldY < DIORAMA_GRID_HEIGHT
                      && oldIndex < previous->visibleCellCount
                      && sSyncCells[oldIndex].mapX == mapX && sSyncCells[oldIndex].mapY == mapY;

            if (inNew != inOld)
                return false;
            if (!inNew)
                continue;
            if (resolvedCells[newIndex].structureSouthFacadeRows != 0
             || memcmp(&snapshot->cells[newIndex], &sSyncCells[oldIndex], sizeof(sSyncCells[0])) != 0
             || memcmp(&resolvedCells[newIndex], &sSyncResolved[oldIndex], sizeof(sSyncResolved[0])) != 0)
                return false;
        }
    return true;
}

/* Builds (or checks) one chunk; false when the cache or the GL failed. */
static bool SyncChunk(const struct DioramaSceneSnapshot *snapshot,
                      const struct DioramaResolvedCell *resolvedCells,
                      int chunkX, int chunkY, bool forceAllDirty, uint32_t rulesGeneration,
                      unsigned *offscreenBuilds)
{
    static struct DioramaTerrainChunkInput input;
    struct GLTerrainChunk *chunk;
    uint64_t signature;
    bool newIdentity = false;
    bool forceDirty = forceAllDirty;
    uint8_t dirtyIndex;

    sCandidatePixels = 0;
    if (!BuildInput(snapshot, resolvedCells, chunkX, chunkY, &input))
        return true;
    signature = DioramaTerrain_ChunkSignature(&input);
    for (dirtyIndex = 0; !forceDirty && dirtyIndex < snapshot->dirtyCellCount; dirtyIndex++)
        forceDirty = DioramaTerrain_DirtyCellAffectsChunk(
            snapshot->dirtyCells[dirtyIndex].mapX,
            snapshot->dirtyCells[dirtyIndex].mapY,
            chunkX, chunkY);
    chunk = FindChunk(chunkX, chunkY);
    if (chunk != NULL && chunk->occupied && !chunk->active && chunk->mapGeneration != snapshot->mapGeneration
     && chunk->rulesGeneration == rulesGeneration && chunk->signature == signature && !forceDirty)
    {
        /* Seen before this map change, the same as then: from the cache. */
        chunk->mapGeneration = snapshot->mapGeneration;
        chunk->stale = false;
        sCacheHits++;
    }
    if (newIdentity || chunk == NULL || !chunk->occupied
     || chunk->rulesGeneration != rulesGeneration
     || chunk->signature != signature || forceDirty)
    {
        /* Off screen and far: it may wait (its old mesh, if any, stands meanwhile). */
        if (!forceDirty && !ChunkUrgent(snapshot, chunk, chunkX, chunkY))
        {
            if (*offscreenBuilds >= TERRAIN_OFFSCREEN_BUILDS)
            {
                AddPending(chunkX, chunkY);
                sDeferredBuilds++;
                if (chunk != NULL && chunk->occupied && chunk->rulesGeneration == rulesGeneration)
                {
                    chunk->stale = true;
                    chunk->active = true;
                    chunk->lastSeenSequence = snapshot->sequence;
                }
                return true;
            }
            (*offscreenBuilds)++;
        }
        if (chunk == NULL)
        {
            chunk = AllocateChunk();
            newIdentity = true;
        }
        if (chunk == NULL)
            return false;
        if (!UploadChunk(chunk, &input, signature))
            return false;
        sMetrics.rebuiltChunks++;
    }
    chunk->active = true;
    chunk->lastSeenSequence = snapshot->sequence;
    NotePixelUse(chunk, &input);
    return true;
}

/* The chunks waiting for their build, nearest the player first, within the
 * sync's allowance (those that became urgent at once). */
static bool SyncPending(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolvedCells, uint32_t rulesGeneration)
{
    int16_t pending[TERRAIN_MAX_PENDING][2];
    unsigned count = sPendingCount;
    unsigned offscreen = 0;
    unsigned p;
    int minChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX, DIORAMA_TERRAIN_CHUNK_SIZE);
    int maxChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX + DIORAMA_GRID_WIDTH - 1,
                                            DIORAMA_TERRAIN_CHUNK_SIZE);
    int minChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY, DIORAMA_TERRAIN_CHUNK_SIZE);
    int maxChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY + DIORAMA_GRID_HEIGHT - 1,
                                            DIORAMA_TERRAIN_CHUNK_SIZE);
    uint32_t before = sMetrics.rebuiltChunks;

    memcpy(pending, sPending, count * sizeof(pending[0]));
    sPendingCount = 0;
    TreeTint(snapshot);
    PrepareSync(snapshot, resolvedCells);
    for (p = 0; p < count; p++)
    {
        if (pending[p][0] < minChunkX || pending[p][0] > maxChunkX
         || pending[p][1] < minChunkY || pending[p][1] > maxChunkY)
            continue; /* left the grid meanwhile */
        if (!SyncChunk(snapshot, resolvedCells, pending[p][0], pending[p][1], false, rulesGeneration,
                       &offscreen))
            return false;
    }
    if (sMetrics.rebuiltChunks != before)
        sGeometryGeneration++;
    return true;
}

/*
 * PKMN_DIORAMA_TERRAIN_CHECK=1 (tests): after a sync that skipped chunks
 * (nothing changed, or only animated tiles), build every active chunk's input
 * anyway and compare its signature with the chunk's: a mismatch would be a
 * chunk the shortcut left stale. Slow; logs each mismatch and a running count.
 */
static void CheckSkippedSync(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaResolvedCell *resolvedCells)
{
    static int sEnabled = -1;
    static struct DioramaTerrainChunkInput input;
    static uint32_t sChecked;
    static uint32_t sStale;
    int i;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_TERRAIN_CHECK");

        sEnabled = value != NULL && value[0] == '1';
    }
    if (!sEnabled)
        return;
    TreeTint(snapshot);
    PrepareSync(snapshot, resolvedCells);
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        bool pending = false;
        unsigned p;

        if (!chunk->active)
            continue;
        for (p = 0; p < sPendingCount; p++)
            pending |= sPending[p][0] == chunk->chunkX && sPending[p][1] == chunk->chunkY;
        if (pending)
            continue; /* off screen and waiting for its build: stale by design */
        sCandidatePixels = 0;
        if (!BuildInput(snapshot, resolvedCells, chunk->chunkX, chunk->chunkY, &input))
            continue;
        sChecked++;
        if (DioramaTerrain_ChunkSignature(&input) != chunk->signature)
        {
            sStale++;
            SDL_Log("Diorama: terrain check: chunk %d,%d stale after a skipped sync (sequence %llu)",
                    chunk->chunkX, chunk->chunkY, (unsigned long long)snapshot->sequence);
        }
    }
    if ((sChecked & 0xFFF) < 64)
        SDL_Log("Diorama: terrain check: %u chunks checked, %u stale", sChecked, sStale);
}

bool DioramaGLTerrain_Sync(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaResolvedCell *resolvedCells)
{
    struct TerrainSyncKey previousKey;
    bool gridMovedOnly = false;
    int minChunkX;
    int maxChunkX;
    int minChunkY;
    int maxChunkY;
    int chunkX;
    int chunkY;
    int i;
    uint32_t rulesGeneration = DioramaRules_GetGeneration();
    bool forceAllDirty = DioramaTerrain_ShouldInvalidateAll(
        sLastSyncSequence, snapshot->sequence,
        sMapEditGeneration, snapshot->mapEditGeneration,
        snapshot->dirtyCellCount, snapshot->dirtyOverflow);

    sClaimDebugVertexCount = 0;
    /* The claim outlines are drawn only by the terrain debug view. */
    for (i = 0; sDebugDrawn && i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        const struct DioramaResolvedCell *resolved = &resolvedCells[i];
        static const int8_t directions[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        uint8_t direction;

        if (resolved->structureId == 0 || resolved->shape == DIORAMA_SHAPE_HIDDEN)
            continue;
        for (direction = 0; direction < 4; direction++)
        {
            int gridX = cell->mapX + directions[direction][0] - snapshot->gridOriginX;
            int gridY = cell->mapY + directions[direction][1] - snapshot->gridOriginY;
            uint16_t neighborId = 0;
            struct DioramaTerrainVertex *first;
            struct DioramaTerrainVertex *second;
            float x0 = cell->mapX;
            float x1 = cell->mapX + 1.0f;
            float z0 = cell->mapY;
            float z1 = cell->mapY + 1.0f;
            float y = resolved->topHeight + 0.04f;

            if (gridX >= 0 && gridY >= 0 && gridX < DIORAMA_GRID_WIDTH
             && gridY < DIORAMA_GRID_HEIGHT)
            {
                int neighbor = gridY * DIORAMA_GRID_WIDTH + gridX;
                if (neighbor < snapshot->visibleCellCount
                 && snapshot->cells[neighbor].mapX == cell->mapX + directions[direction][0]
                 && snapshot->cells[neighbor].mapY == cell->mapY + directions[direction][1])
                    neighborId = resolvedCells[neighbor].structureId;
            }
            if (neighborId == resolved->structureId
             || sClaimDebugVertexCount + 2 > TERRAIN_DEBUG_MAX_VERTICES)
                continue;
            first = &sDebugVertices[sClaimDebugVertexCount++];
            second = &sDebugVertices[sClaimDebugVertexCount++];
            memset(first, 0, sizeof(*first));
            memset(second, 0, sizeof(*second));
            first->y = second->y = y;
            first->shade = second->shade = 1.0f;
            if (direction == 0 || direction == 2)
            {
                first->x = x0;
                second->x = x1;
                first->z = second->z = direction == 0 ? z0 : z1;
            }
            else
            {
                first->z = z0;
                second->z = z1;
                first->x = second->x = direction == 3 ? x0 : x1;
            }
        }
    }

    SyncFar(snapshot);
    sMetrics.rebuiltChunks = 0;
    if (rulesGeneration != sRulesGeneration)
    {
        DioramaGLTerrain_Reset(); /* other rules: nothing cached holds */
        sRulesGeneration = rulesGeneration;
    }
    if (snapshot->mapGeneration != sMapGeneration)
    {
        LeaveMap(); /* chunks of maps seen before come back by their signatures */
        sMapGeneration = snapshot->mapGeneration;
    }
    sSyncMapKey = MapKey(snapshot);
    {
        struct TerrainSyncKey key;
        unsigned cellCount = snapshot->visibleCellCount < DIORAMA_MAX_VISIBLE_CELLS
                           ? snapshot->visibleCellCount : DIORAMA_MAX_VISIBLE_CELLS;

        memset(&key, 0, sizeof(key));
        key.gridOriginX = snapshot->gridOriginX;
        key.gridOriginY = snapshot->gridOriginY;
        key.visibleCellCount = (uint16_t)cellCount;
        key.propsEnabled = DioramaProps_Enabled();
        key.complete = 1;
        key.mapGeneration = snapshot->mapGeneration;
        key.mapEditGeneration = snapshot->mapEditGeneration;
        key.paletteGeneration = snapshot->paletteGeneration;
        key.tilesetAnimationGeneration = snapshot->tilesetAnimationGeneration;
        key.rulesGeneration = rulesGeneration;
        key.pageSignature = DioramaAtlas_LastPageSignature();
        struct TerrainSyncKey animated = key;

        /* The same inputs but for the animated tiles' generation? */
        animated.tilesetAnimationGeneration = sSyncKey.tilesetAnimationGeneration;
        if (sSyncKeyValid && !forceAllDirty && snapshot->dirtyCellCount == 0
         && memcmp(&animated, &sSyncKey, sizeof(key)) == 0
         && memcmp(sSyncCells, snapshot->cells, cellCount * sizeof(sSyncCells[0])) == 0
         && memcmp(sSyncResolved, resolvedCells, cellCount * sizeof(sSyncResolved[0])) == 0)
        {
            bool ok = true;

            if (key.tilesetAnimationGeneration == sSyncKey.tilesetAnimationGeneration)
            {
                /* Nothing the chunks are built from changed: they stand as they are. */
                sSyncSkipped++;
                for (i = 0; i < (int)sChunkLimit; i++)
                    if (sChunks[i].active)
                        sChunks[i].lastSeenSequence = snapshot->sequence;
                sMapEditGeneration = snapshot->mapEditGeneration;
                sLastSyncSequence = snapshot->sequence;
            }
            else
            {
                /* Only the animated tiles (water, flowers) moved on: only a chunk
                 * whose input reads the pixels of a tile that changed is built
                 * again (most read none: flat water is its atlas slot). */
                sSyncAnimated++;
                ok = SyncAnimatedTiles(snapshot, resolvedCells, &key);
            }
            /* The chunks still waiting for their build, a few a sync. */
            if (ok && sPendingCount != 0)
                ok = SyncPending(snapshot, resolvedCells, rulesGeneration);
            CheckSkippedSync(snapshot, resolvedCells);
            if (!ok)
                sSyncKeyValid = false;
            return ok && sMetrics.activeChunks != 0;
        }
        sSyncRun++;
        if ((sSyncRun & 127) == 0 && LogEnabledTerrain())
            SDL_Log("Diorama: terrain syncs: %u ran the chunk loop, %u only animated tiles, "
                    "%u had nothing to do; %u chunks stood as the grid moved, %u builds waited, "
                    "%u came back from the cache (%u chunks)",
                    sSyncRun, sSyncAnimated, sSyncSkipped, sInteriorSkips, sDeferredBuilds,
                    sCacheHits, sChunkLimit);
        {
            struct TerrainSyncKey moved = key;

            /* Only the grid moved (the player walking)? Then a chunk whose
             * cells are all the same cells, resolved the same, stands. */
            moved.gridOriginX = sSyncKey.gridOriginX;
            moved.gridOriginY = sSyncKey.gridOriginY;
            moved.visibleCellCount = sSyncKey.visibleCellCount;
            gridMovedOnly = sSyncKeyValid && !forceAllDirty && snapshot->dirtyCellCount == 0
                         && memcmp(&moved, &sSyncKey, sizeof(key)) == 0;
        }
        previousKey = sSyncKey;
        /* Kept only once the loop below has gone through every chunk. */
        sSyncKeyValid = false;
        sSyncKey = key;
    }
    TreeTint(snapshot);
    PrepareSync(snapshot, resolvedCells);
    /* The commonest ground (tree hulls stand on it) is the whole grid's. */
    if (sSyncGroundSlot != sPrevGroundSlot)
        gridMovedOnly = false;
    sPrevGroundSlot = sSyncGroundSlot;
    for (i = 0; i < (int)sChunkLimit; i++)
        sChunks[i].active = false;
    sPendingCount = 0;
    minChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX, DIORAMA_TERRAIN_CHUNK_SIZE);
    maxChunkX = DioramaTerrain_FloorDiv(snapshot->gridOriginX + DIORAMA_GRID_WIDTH - 1,
                                       DIORAMA_TERRAIN_CHUNK_SIZE);
    minChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY, DIORAMA_TERRAIN_CHUNK_SIZE);
    maxChunkY = DioramaTerrain_FloorDiv(snapshot->gridOriginY + DIORAMA_GRID_HEIGHT - 1,
                                       DIORAMA_TERRAIN_CHUNK_SIZE);
    {
        /* Nearest the player first: the allowance for far chunks goes to the nearest. */
        int16_t order[TERRAIN_GRID_CHUNKS][2];
        int distance[TERRAIN_GRID_CHUNKS];
        unsigned count = 0;
        unsigned offscreen = 0;
        unsigned n;
        int centreX = snapshot->gridOriginX + DIORAMA_GRID_WIDTH / 2;
        int centreY = snapshot->gridOriginY + DIORAMA_GRID_HEIGHT / 2;

        for (chunkY = minChunkY; chunkY <= maxChunkY; chunkY++)
            for (chunkX = minChunkX; chunkX <= maxChunkX && count < TERRAIN_GRID_CHUNKS; chunkX++)
            {
                int dx = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_CHUNK_SIZE / 2 - centreX;
                int dy = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_CHUNK_SIZE / 2 - centreY;
                int d = dx * dx + dy * dy;
                unsigned at = count;

                while (at > 0 && distance[at - 1] > d)
                {
                    distance[at] = distance[at - 1];
                    order[at][0] = order[at - 1][0];
                    order[at][1] = order[at - 1][1];
                    at--;
                }
                distance[at] = d;
                order[at][0] = (int16_t)chunkX;
                order[at][1] = (int16_t)chunkY;
                count++;
            }
        for (n = 0; n < count; n++)
        {
            struct GLTerrainChunk *chunk;

            chunkX = order[n][0];
            chunkY = order[n][1];
            chunk = FindChunk(chunkX, chunkY);
            if (gridMovedOnly && chunk != NULL && chunk->occupied && !chunk->hasPixelObjects && !chunk->stale
             && chunk->mapGeneration == snapshot->mapGeneration && chunk->rulesGeneration == rulesGeneration
             && RegionUnchanged(snapshot, resolvedCells, &previousKey, chunkX, chunkY))
            {
                chunk->active = true;
                chunk->lastSeenSequence = snapshot->sequence;
                sInteriorSkips++;
                continue;
            }
            if (!SyncChunk(snapshot, resolvedCells, chunkX, chunkY, forceAllDirty, rulesGeneration,
                           &offscreen))
                return false;
        }
    }
    {
        unsigned cellCount = snapshot->visibleCellCount < DIORAMA_MAX_VISIBLE_CELLS
                           ? snapshot->visibleCellCount : DIORAMA_MAX_VISIBLE_CELLS;

        memcpy(sSyncCells, snapshot->cells, cellCount * sizeof(sSyncCells[0]));
        memcpy(sSyncResolved, resolvedCells, cellCount * sizeof(sSyncResolved[0]));
        memcpy(sSyncTiles, snapshot->tileGraphics, sizeof(sSyncTiles));
    }

    if (sMetrics.rebuiltChunks != 0
     || sLastChunkRange[0] != minChunkX || sLastChunkRange[1] != maxChunkX
     || sLastChunkRange[2] != minChunkY || sLastChunkRange[3] != maxChunkY)
    {
        sLastChunkRange[0] = minChunkX;
        sLastChunkRange[1] = maxChunkX;
        sLastChunkRange[2] = minChunkY;
        sLastChunkRange[3] = maxChunkY;
        sGeometryGeneration++;
    }
    sMetrics.residentChunks = 0;
    sMetrics.activeChunks = 0;
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        if (sChunks[i].occupied) sMetrics.residentChunks++;
        if (sChunks[i].active) sMetrics.activeChunks++;
    }
    sMapEditGeneration = snapshot->mapEditGeneration;
    sLastSyncSequence = snapshot->sequence;
    sSyncKeyValid = true;
    if (sMapJustChanged)
    {
        sMapJustChanged = false;
        if (LogEnabledTerrain())
            SDL_Log("Diorama: map %u.%u: %u chunks from the cache, %u built, %u waiting",
                    snapshot->mapGroup, snapshot->mapNum, sCacheHits - sHitsAtChange,
                    sMetrics.rebuiltChunks, sPendingCount);
    }
    if (gridMovedOnly)
        CheckSkippedSync(snapshot, resolvedCells);
    return sMetrics.activeChunks != 0;
}

static uint32_t AppendBoundsVertices(uint32_t count, const struct DioramaTerrainBounds *bounds)
{
    static const uint8_t cornerOrder[8] = {0, 1, 1, 2, 2, 3, 3, 0};
    float x[4] = {bounds->minX, bounds->maxX, bounds->maxX, bounds->minX};
    float z[4] = {bounds->maxZ, bounds->maxZ, bounds->minZ, bounds->minZ};
    float y = bounds->maxY + 0.03f;
    int i;

    for (i = 0; i < 8 && count < TERRAIN_DEBUG_MAX_VERTICES; i++, count++)
    {
        int corner = cornerOrder[i];
        sDebugVertices[count].x = x[corner];
        sDebugVertices[count].y = y;
        sDebugVertices[count].z = z[corner];
        sDebugVertices[count].u = 0.0f;
        sDebugVertices[count].v = 0.0f;
        sDebugVertices[count].shade = 1.0f;
        sDebugVertices[count].textureLayer = DIORAMA_TERRAIN_TEXTURE_FULL;
        sDebugVertices[count].reflectionMask = 0.0f;
    }
    return count;
}

void DioramaGLTerrain_Draw(GLuint atlasTexture, GLuint baseAtlasTexture,
                           GLuint foregroundAtlasTexture, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection, bool debug)
{
    uint32_t debugVertexCount = sClaimDebugVertexCount;
    uint32_t visibleTreeCount = 0;
    int i;

    sDebugDrawn = debug;
    sCameraKnownForSync = true;
    sSyncCameraX = cameraX;
    sSyncCameraZ = cameraZ;
    sSyncCameraPitch = cameraPitch;
    sSyncFocalLength = focalLength;
    sMetrics.visibleChunks = 0;
    sMetrics.culledChunks = 0;
    sMetrics.vertices = 0;
    sMetrics.triangles = 0;
    sMetrics.drawCalls = 0;
    dglUseProgram(sProgram);
    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1f(sAspectCorrectionLocation, aspectCorrection);
    {
        float eyeHeight;
        float eyeDistance;
        float screenOffsetY;

        DioramaCamera_GetRig(cameraPitch, &eyeHeight, &eyeDistance, &screenOffsetY);
        dglUniform1f(sCameraHeightLocation, eyeHeight);
        dglUniform1f(sCameraDistanceLocation, eyeDistance);
        dglUniform1f(sScreenOffsetLocation, screenOffsetY);
    }
    dglUniform1i(sImageLocation, 0);
    dglUniform1i(sBaseImageLocation, 1);
    dglUniform1i(sForegroundImageLocation, 2);
    dglUniform4f(sDebugColorLocation, 0.0f, 0.0f, 0.0f, -1.0f);
    dglUniform1i(sRenderPassLocation, 0);
    dglUniform1i(sShadowPassLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniform1i(sShadowEnabledLocation, DioramaGLShadow_BindForLighting() && !debug);
    dglUniform1f(sShadowAmbientLocation, DioramaGLShadow_Ambient());
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglUniform1f(sSaturationLocation, debug ? 1.0f : DioramaGLGrade_Saturation());
    if (debug)
    {
        DioramaGLLight_ApplyNeutral(&sLightUniforms);
        DioramaGLLamps_ApplyNone(&sLampUniforms);
    }
    else
    {
        DioramaGLLight_Apply(&sLightUniforms, cameraPitch);
        DioramaGLLamps_Apply(&sLampUniforms);
    }
    dglUniform3f(sPixelScaleLocation, 1.0f, 1.0f, 1.0f);
    dglUniform3f(sPixelOffsetLocation, 0.0f, 0.0f, 0.0f);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseAtlasTexture);
    dglActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, foregroundAtlasTexture);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (debug)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    /* The distant scenery: its colours are unfaded, so they follow the
     * palette's fade and night tint along the trees' line. */
    if (DioramaGLTerrain_FarShown() && !debug)
    {
        dglUniform3f(sPixelScaleLocation, sTreeScale[0], sTreeScale[1], sTreeScale[2]);
        dglUniform3f(sPixelOffsetLocation, sTreeOffset[0], sTreeOffset[1], sTreeOffset[2]);
        dglBindVertexArray(sFarVertexArray);
        for (i = 0; i < DIORAMA_FAR_SECTORS; i++)
        {
            const struct DioramaFarSector *sector = &sFarSectors[i];

            if (sector->count == 0
             || !DioramaTerrain_IsBoundsVisible(&sector->bounds, cameraX, cameraZ, cameraPitch, focalLength))
                continue;
            glDrawArrays(GL_TRIANGLES, (GLint)sector->first, (GLsizei)sector->count);
            sMetrics.vertices += sector->count;
            sMetrics.triangles += sector->count / 3;
            sMetrics.drawCalls++;
        }
        dglUniform3f(sPixelScaleLocation, 1.0f, 1.0f, 1.0f);
        dglUniform3f(sPixelOffsetLocation, 0.0f, 0.0f, 0.0f);
    }
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active)
            continue;
        if (!DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
        {
            sMetrics.culledChunks++;
            continue;
        }
        sMetrics.visibleChunks++;
        sMetrics.vertices += chunk->mesh.vertexCount;
        sMetrics.triangles += chunk->mesh.vertexCount / 3;
        sMetrics.drawCalls++;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        for (uint16_t tree = 0; tree < chunk->mesh.treeInstanceCount
             && visibleTreeCount < TERRAIN_MAX_VISIBLE_TREE_INSTANCES; tree++)
            sVisibleTreeInstances[visibleTreeCount++] = chunk->mesh.treeInstances[tree];
        if (debug)
            debugVertexCount = AppendBoundsVertices(debugVertexCount, &chunk->mesh.bounds);
    }
    if (visibleTreeCount != 0)
    {
        dglBindVertexArray(sTreeVertexArray);
        dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
        dglBufferData(GL_ARRAY_BUFFER,
                      visibleTreeCount * sizeof(struct DioramaTerrainTreeInstance),
                      sVisibleTreeInstances, GL_STREAM_DRAW);
        if (!debug)
        {
            dglUniform3f(sPixelScaleLocation, sTreeScale[0], sTreeScale[1], sTreeScale[2]);
            dglUniform3f(sPixelOffsetLocation, sTreeOffset[0], sTreeOffset[1], sTreeOffset[2]);
        }
        dglDrawArraysInstanced(GL_TRIANGLES, 0, sTreeModelVertexCount, visibleTreeCount);
        dglUniform3f(sPixelScaleLocation, 1.0f, 1.0f, 1.0f);
        dglUniform3f(sPixelOffsetLocation, 0.0f, 0.0f, 0.0f);
        sMetrics.vertices += sTreeModelVertexCount * visibleTreeCount;
        sMetrics.triangles += sTreeModelVertexCount * visibleTreeCount / 3;
        sMetrics.drawCalls++;
    }
    sMetrics.visibleTreeInstances = visibleTreeCount;
    sMetrics.treeModelVertices = sTreeModelVertexCount;
    if (debug)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x01);
    glStencilFunc(GL_ALWAYS, 1, 0x01);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    dglUniform1i(sRenderPassLocation, 1);
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active
         || !DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
            continue;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        sMetrics.drawCalls++;
    }
    glStencilMask(0x02);
    glStencilFunc(GL_ALWAYS, 2, 0x02);
    dglUniform1i(sRenderPassLocation, 2);
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active
         || !DioramaTerrain_IsBoundsVisible(&chunk->mesh.bounds, cameraX, cameraZ,
                                             cameraPitch, focalLength))
            continue;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        sMetrics.drawCalls++;
    }
    dglUniform1i(sRenderPassLocation, 0);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glDisable(GL_STENCIL_TEST);
    if (debug)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        dglUniform4f(sDebugColorLocation, 1.0f, 0.85f, 0.1f, 1.0f);
        dglBindVertexArray(sDebugVertexArray);
        dglBindBuffer(GL_ARRAY_BUFFER, sDebugVertexBuffer);
        dglBufferData(GL_ARRAY_BUFFER,
                      debugVertexCount * sizeof(struct DioramaTerrainVertex),
                      sDebugVertices, GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, debugVertexCount);
        sMetrics.drawCalls++;
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void DioramaGLTerrain_DrawShadow(GLuint atlasTexture, GLuint baseAtlasTexture,
                                 GLuint foregroundAtlasTexture)
{
    uint32_t treeCount = 0;
    int i;

    dglUseProgram(sProgram);
    dglUniform1i(sImageLocation, 0);
    dglUniform1i(sBaseImageLocation, 1);
    dglUniform1i(sForegroundImageLocation, 2);
    dglUniform4f(sDebugColorLocation, 0.0f, 0.0f, 0.0f, -1.0f);
    dglUniform1i(sRenderPassLocation, 0);
    dglUniform1i(sShadowPassLocation, 1);
    dglUniform1i(sShadowEnabledLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseAtlasTexture);
    dglActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, foregroundAtlasTexture);
    dglActiveTexture(GL_TEXTURE0);
    /* Everything resident casts, on screen or not: a tree just off the edge
     * still throws its shadow into view. */
    for (i = 0; i < (int)sChunkLimit; i++)
    {
        struct GLTerrainChunk *chunk = &sChunks[i];

        if (!chunk->active)
            continue;
        dglBindVertexArray(chunk->vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, chunk->mesh.vertexCount);
        for (uint16_t tree = 0; tree < chunk->mesh.treeInstanceCount
             && treeCount < TERRAIN_MAX_VISIBLE_TREE_INSTANCES; tree++)
            sVisibleTreeInstances[treeCount++] = chunk->mesh.treeInstances[tree];
    }
    if (treeCount != 0)
    {
        dglBindVertexArray(sTreeVertexArray);
        dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
        dglBufferData(GL_ARRAY_BUFFER,
                      treeCount * sizeof(struct DioramaTerrainTreeInstance),
                      sVisibleTreeInstances, GL_STREAM_DRAW);
        dglDrawArraysInstanced(GL_TRIANGLES, 0, sTreeModelVertexCount, treeCount);
    }
    dglUniform1i(sShadowPassLocation, 0);
}

const struct DioramaTerrainMetrics *DioramaGLTerrain_GetMetrics(void)
{
    return &sMetrics;
}

void DioramaGLTerrain_Shutdown(void)
{
    int i;

    for (i = 0; i < (int)sChunkLimit; i++)
    {
        if (sChunks[i].vertexBuffer != 0)
            dglDeleteBuffers(1, &sChunks[i].vertexBuffer);
        if (sChunks[i].vertexArray != 0)
            dglDeleteVertexArrays(1, &sChunks[i].vertexArray);
    }
    if (sDebugVertexBuffer != 0)
        dglDeleteBuffers(1, &sDebugVertexBuffer);
    if (sDebugVertexArray != 0)
        dglDeleteVertexArrays(1, &sDebugVertexArray);
    if (sFarVertexBuffer != 0)
        dglDeleteBuffers(1, &sFarVertexBuffer);
    if (sFarVertexArray != 0)
        dglDeleteVertexArrays(1, &sFarVertexArray);
    sFarVertexBuffer = 0;
    sFarVertexArray = 0;
    sFarVertexCount = 0;
    sFarBuilt = false;
    sFarShown = false;
    if (sTreeInstanceBuffer != 0)
        dglDeleteBuffers(1, &sTreeInstanceBuffer);
    if (sTreeVertexBuffer != 0)
        dglDeleteBuffers(1, &sTreeVertexBuffer);
    if (sTreeVertexArray != 0)
        dglDeleteVertexArrays(1, &sTreeVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    memset(sChunks, 0, sizeof(sChunks));
    sDebugVertexBuffer = 0;
    sDebugVertexArray = 0;
    sTreeInstanceBuffer = 0;
    sTreeVertexBuffer = 0;
    sTreeVertexArray = 0;
    sTreeModelVertexCount = 0;
    sProgram = 0;
}

#endif
