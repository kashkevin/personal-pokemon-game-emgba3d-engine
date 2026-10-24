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
#include "diorama/gl_target.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/metatile_atlas.h"
#include "diorama/props.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/terrain_mesh.h"
#include "fieldmap.h"
#include "metatile_behavior.h"
#include "platform/png_write.h"
#include "constants/metatile_behaviors.h"

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
    /* Its quads drawn from four corners each (DioramaTerrain_FoldQuads):
     * indexCount indices of indexType into drawVertices vertices; no index
     * buffer, the mesh's own vertices drawn as they are. */
    GLuint indexBuffer;
    GLenum indexType;
    uint32_t indexCount;
    uint32_t drawVertices;
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
/*
 * The voxel tree model (DioramaTerrain_BuildTreeModel), an instance per tree.
 * Performance work 2026-10-24 (the owner's Petalburg Woods, laggy to run
 * through): the Diorama camera has some 330 trees in its chunks there, 14,544
 * vertices each - about 4.8 million vertices a frame for the trees alone. The
 * model is drawn from indices, a quad's four corners shaded once
 * (DioramaTerrain_FoldQuads: the same triangles in the same order), and the
 * colour pass draws only the trees on screen (each tree tested, not only its
 * chunk); the picture is the same to the pixel (PKMN_DIORAMA_TREE_CHECK). The
 * sun's depth map draws every resident tree, as before, from the indices.
 *
 * A second range of indices holds the faces an eye above a tree and south of
 * it can see - all but those looking north or down, 1580 of the 2424 faces
 * (a third fewer triangles, and no fragments shaded for faces the model's own
 * faces then cover). Those face away from such an eye and the model is
 * closed, so each lies behind the model's own faces wherever it is on screen;
 * but where a face looking away meets one looking at the eye along an edge,
 * their depths tie at a few pixels and the later drawn wins: about 0.1% of the
 * picture's pixels take the other face's shade there (Petalburg Woods, the
 * Diorama camera: some 900 of 921,600 pixels a frame, by up to 12 of 255).
 * So it is opt-in, PKMN_DIORAMA_TREE_FACES=seen; when asked for, the colour
 * pass uses it if the eye is so for every tree it draws (the field's cameras
 * always are).
 */
enum
{
    TREE_RANGE_ALL,
    TREE_RANGE_VIEW,
    TREE_RANGE_COUNT
};
static GLuint sTreeVertexArray;
static GLuint sTreeVertexBuffer;
static GLuint sTreeIndexBuffer;
static GLuint sTreeInstanceBuffer;
static uint32_t sTreeModelVertexCount; /* six per face, before folding */
static GLenum sTreeIndexType;
static uint32_t sTreeIndexSize;
static uint32_t sTreeRangeFirst[TREE_RANGE_COUNT]; /* in indices */
static uint32_t sTreeRangeCount[TREE_RANGE_COUNT];
static struct DioramaTerrainBounds sTreeModelBounds; /* before an instance's offset */
/*
 * PKMN_DIORAMA_TREE_CHECK=1 (tests): the model is also kept as it was drawn
 * before (six vertices a face, every tree of every chunk on screen, every
 * face), and each frame's trees are drawn both ways over the same picture;
 * the pixels whose colour or depth differ are counted and logged, and the
 * first frames that differ are written out (tree-check-*.png). Slow.
 */
static GLuint sTreeCheckVertexArray;
static GLuint sTreeCheckVertexBuffer;
static struct DioramaTerrainTreeInstance sCheckTreeInstances[TERRAIN_MAX_VISIBLE_TREE_INSTANCES];
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

static bool TreeCheckEnabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_TREE_CHECK");

        sEnabled = value != NULL && value[0] == '1';
    }
    return sEnabled != 0;
}

/* PKMN_DIORAMA_TREE_FACES=seen (opt-in, see above): the trees from
 * TREE_RANGE_VIEW where the eye allows. */
static bool TreeFacesSeen(void)
{
    static int sSeen = -1;

    if (sSeen < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_TREE_FACES");

        sSeen = value != NULL && strcmp(value, "seen") == 0;
    }
    return sSeen != 0;
}

/* The per-instance offset (attribute 6) of a tree vertex array, from the
 * instance buffer. */
static void ConfigureTreeInstances(void)
{
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
    dglEnableVertexAttribArray(6);
    dglVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE,
                           sizeof(struct DioramaTerrainTreeInstance), NULL);
    dglVertexAttribDivisor(6, 1);
}

static bool CreateTreeBuffers(void)
{
    uint32_t faceCount = DioramaTerrain_TreeModelFaceCount();
    uint32_t *indices;
    uint32_t indexCount = 0;
    uint32_t viewCount = 0;
    uint32_t vertexCount;
    uint32_t total;
    uint32_t i;

    if (!DioramaTerrain_BuildTreeModel(sScratchVertices,
            DIORAMA_TERRAIN_MAX_VERTICES, &sTreeModelVertexCount, &sTreeModelBounds)
     || sTreeModelVertexCount == 0)
        return false;
    /* Room for every face's indices twice (both ranges). */
    indices = malloc((size_t)sTreeModelVertexCount * 2 * sizeof(*indices));
    if (indices == NULL)
        return false;
    dglGenVertexArrays(1, &sTreeVertexArray);
    dglGenBuffers(1, &sTreeVertexBuffer);
    dglGenBuffers(1, &sTreeIndexBuffer);
    dglGenBuffers(1, &sTreeInstanceBuffer);
    if (TreeCheckEnabled())
    {
        /* The model as it was drawn before, for the comparison. */
        dglGenVertexArrays(1, &sTreeCheckVertexArray);
        dglGenBuffers(1, &sTreeCheckVertexBuffer);
        ConfigureVertexArray(sTreeCheckVertexArray, sTreeCheckVertexBuffer);
        dglBufferData(GL_ARRAY_BUFFER, sTreeModelVertexCount * sizeof(struct DioramaTerrainVertex),
                      sScratchVertices, GL_STATIC_DRAW);
        ConfigureTreeInstances();
        dglBindVertexArray(0);
    }
    vertexCount = DioramaTerrain_FoldQuads(sScratchVertices, sTreeModelVertexCount, indices, &indexCount);
    sTreeRangeFirst[TREE_RANGE_ALL] = 0;
    sTreeRangeCount[TREE_RANGE_ALL] = indexCount;
    sTreeRangeFirst[TREE_RANGE_VIEW] = 0;
    sTreeRangeCount[TREE_RANGE_VIEW] = indexCount;
    /* Each face one quad (AppendPixelQuad), folded to four corners: face f
     * is indices 6f to 6f + 5. The faces an eye above and south of a tree
     * can see, in the model's order. */
    if (indexCount == faceCount * 6 && vertexCount == faceCount * 4)
    {
        for (i = 0; i < faceCount; i++)
        {
            enum DioramaTreeFacing facing = DioramaTerrain_TreeModelFacing(i);

            if (facing == DIORAMA_TREE_FACING_NORTH || facing == DIORAMA_TREE_FACING_DOWN)
                continue;
            memcpy(&indices[indexCount + viewCount], &indices[i * 6], 6 * sizeof(*indices));
            viewCount += 6;
        }
        sTreeRangeFirst[TREE_RANGE_VIEW] = indexCount;
        sTreeRangeCount[TREE_RANGE_VIEW] = viewCount;
    }
    total = indexCount + viewCount;
    sTreeIndexType = GL_UNSIGNED_INT;
    sTreeIndexSize = sizeof(uint32_t);
    if (vertexCount <= 0x10000)
    {
        /* in place: the uint16_t at i ends before the uint32_t at i+1 begins */
        uint16_t *shortIndices = (uint16_t *)(void *)indices;

        for (i = 0; i < total; i++)
            shortIndices[i] = (uint16_t)indices[i];
        sTreeIndexType = GL_UNSIGNED_SHORT;
        sTreeIndexSize = sizeof(uint16_t);
    }
    ConfigureVertexArray(sTreeVertexArray, sTreeVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(struct DioramaTerrainVertex),
                  sScratchVertices, GL_STATIC_DRAW);
    /* the element buffer is the vertex array's (bound above) */
    dglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sTreeIndexBuffer);
    dglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)total * sTreeIndexSize, indices, GL_STATIC_DRAW);
    ConfigureTreeInstances();
    dglBindVertexArray(0);
    free(indices);
    SDL_Log("Diorama: tree model %u faces, %u vertices drawn from %u (%u indices; %u seen from above and south)",
            faceCount, sTreeModelVertexCount, vertexCount, indexCount, viewCount);
    return true;
}

/* A tree's place: the model's bounds moved by its offset. */
static struct DioramaTerrainBounds TreeBounds(const struct DioramaTerrainTreeInstance *tree)
{
    struct DioramaTerrainBounds bounds = sTreeModelBounds;

    bounds.minX += tree->x;
    bounds.maxX += tree->x;
    bounds.minY += tree->y;
    bounds.maxY += tree->y;
    bounds.minZ += tree->z;
    bounds.maxZ += tree->z;
    return bounds;
}

/* The eye of the scene's projection (the terrain vertex shader): over the
 * target at its rig's height, its distance south of it, looking north. */
struct TreeEye
{
    float x;
    float y;
    float z;
    float pitchSin;
    float pitchCos;
    float targetZ;
    float distance;
};

static struct TreeEye TreeEyeFor(float cameraX, float cameraZ, float cameraPitch)
{
    struct TreeEye eye;
    float screenOffsetY;

    DioramaCamera_GetRig(cameraPitch, &eye.y, &eye.distance, &screenOffsetY);
    eye.x = cameraX;
    eye.z = cameraZ - eye.distance;
    eye.targetZ = cameraZ;
    eye.pitchSin = sinf(cameraPitch);
    eye.pitchCos = cosf(cameraPitch);
    return eye;
}

/*
 * Whether this eye sees none of a tree's faces that look north or down, so
 * TREE_RANGE_VIEW draws it as every face would: the eye south of each such
 * face's plane and above it (each faces away from the eye, and the closed
 * model's own faces stand in front of it), and the whole tree past the near
 * plane, so none of those faces in front is cut away.
 */
static bool TreeSeenFromAboveSouth(const struct DioramaTerrainBounds *bounds, const struct TreeEye *eye)
{
    int i;

    if (!(eye->z < bounds->minZ && eye->y > bounds->maxY))
        return false;
    for (i = 0; i < 4; i++)
    {
        float y = (i & 1) ? bounds->maxY : bounds->minY;
        float z = (i & 2) ? bounds->maxZ : bounds->minZ;
        float depth = (eye->y - y) * eye->pitchSin + (z - eye->targetZ + eye->distance) * eye->pitchCos;

        if (!(depth > 0.2f)) /* the shaders' near plane is at 0.1 */
            return false;
    }
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

/*
 * A cell drawn over the water (DioramaRules_CellOverWater: a deck, a log
 * walkway, a pier) or over lava's edge (DioramaRules_CellLava 1: the rock
 * rimming Mt Chimney's lake): its side (the riser standing it down to the sea
 * lowered past it, or to the lava sunk in its crater, terrain_mesh.c
 * ColumnBottom) shows the whole cell, the deck's wood or the rim's rock, not
 * its lower layer - that stood Pacifidlog's decks and walkways on blocks of
 * water. A bridge's lower layer is the water under it, and water is water.
 * Added by the battle environments' work.
 */
static bool RiserShowsWholeCell(const struct DioramaSceneSnapshot *snapshot,
                                const struct DioramaCellSnapshot *source, uint8_t shape)
{
    uint16_t layoutId = (source->flags & DIORAMA_CELL_CONNECTED) && source->sourceLayoutId != 0
                      ? source->sourceLayoutId : snapshot->mapLayoutId;

    return shape != DIORAMA_SHAPE_BRIDGE && shape != DIORAMA_SHAPE_WATER
        && (DioramaRules_CellOverWater(source, snapshot->mapType)
         || DioramaRules_CellLava(source, layoutId) == 1);
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

/* The mask's pieces of fewer than MOUNTAIN_SPECK pixels (side by side) taken
 * out: on their own they stood as stray pillars. */
#define MOUNTAIN_SPECK 6

static void DropMountainSpecks(uint16_t rows[DIORAMA_VOXELS_PER_CELL])
{
    uint16_t seen[DIORAMA_VOXELS_PER_CELL] = {0};
    uint8_t stack[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint8_t piece[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];

    for (int start = 0; start < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; start++)
    {
        int startX = start % DIORAMA_VOXELS_PER_CELL;
        int startY = start / DIORAMA_VOXELS_PER_CELL;
        int top = 0;
        int count = 0;

        if (!(rows[startY] & (1u << startX)) || (seen[startY] & (1u << startX)))
            continue;
        seen[startY] |= 1u << startX;
        stack[top++] = start;
        while (top > 0)
        {
            int at = stack[--top];
            int x = at % DIORAMA_VOXELS_PER_CELL;
            int y = at / DIORAMA_VOXELS_PER_CELL;
            static const int8_t steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

            piece[count++] = at;
            for (int step = 0; step < 4; step++)
            {
                int nx = x + steps[step][0];
                int ny = y + steps[step][1];

                if (nx < 0 || ny < 0 || nx >= DIORAMA_VOXELS_PER_CELL || ny >= DIORAMA_VOXELS_PER_CELL
                 || !(rows[ny] & (1u << nx)) || (seen[ny] & (1u << nx)))
                    continue;
                seen[ny] |= 1u << nx;
                stack[top++] = ny * DIORAMA_VOXELS_PER_CELL + nx;
            }
        }
        if (count < MOUNTAIN_SPECK)
            for (int i = 0; i < count; i++)
                rows[piece[i] / DIORAMA_VOXELS_PER_CELL] &=
                    (uint16_t)~(1u << (piece[i] % DIORAMA_VOXELS_PER_CELL));
    }
}

/* How a mountain's rock rises from its foot (ShapeRockMasses), in pixels of
 * the cell: on its south, where the GBA draws a mountain's face, over the
 * whole cell; elsewhere, where it draws the rock's outline seen from above, a
 * few pixels. The relief of a cell's drawing stands at most ROCK_MASS_RELIEF
 * voxels over the rock (a crag), ROCK_MASS_CROWN_RELIEF (a tree's crown: such
 * a cell's rockMass is ROCK_MASS_CROWN). */
#define ROCK_MASS_FACE 16
#define ROCK_MASS_EDGE 5
#define ROCK_MASS_RELIEF 6
#define ROCK_MASS_CROWN_RELIEF 12
#define ROCK_MASS_CROWN 2
/* A cell whose upper layer draws the rock's edge over ground (the rules' art
 * mode "cutout"): its heights hold each pixel's distance into the outline. */
#define ROCK_MASS_OUTLINE 3

static void BuildMountainArtMask(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaCellSnapshot *source,
                                 bool rock, bool outline,
                                 uint8_t terraceProfile,
                                 uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                 uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                               * DIORAMA_VOXELS_PER_CELL],
                                 uint8_t *rockMass)
{
    uint32_t pixels[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
    uint16_t remaining[DIORAMA_VOXELS_PER_CELL];
    uint16_t eroded[DIORAMA_VOXELS_PER_CELL];
    unsigned visible = 0;
    unsigned depth = 0;
    unsigned maxDepth = 0;
    int row;
    bool rounded = false;
    struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, source);

    *rockMass = false;
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

    /* A mountain's rock (a cliff: MB_MOUNTAIN_TOP, or the rock the rules joined
     * to it; only cells the player cannot walk on) is part of one mass with the rock
     * round it (ShapeRockMasses). Here only the relief its drawing gives it, 0
     * to a cell: what its upper layer draws over the rock (a crag, a boulder)
     * a little pyramid from its outline in, a green thing (a mountain tree) a
     * rounded crown; a cell drawn whole on one layer is plain rock. A cell of
     * solid rock per pixel of the drawing made a mountain a field of flat
     * cubes and walls (the owner's report, 2026-10-24); each cell's own
     * pyramid before that, rows of spikes (2026-10-23). */
    if (rock)
    {
        uint32_t red = 0;
        uint32_t green = 0;
        uint32_t blue = 0;
        unsigned kept = 0;

        *rockMass = true;
        if (visible != 0 && visible != DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
            DropMountainSpecks(rows);
        for (int pixel = 0; pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
            if (rows[pixel / DIORAMA_VOXELS_PER_CELL] & (1u << (pixel % DIORAMA_VOXELS_PER_CELL)))
            {
                red += (pixels[pixel] >> 16) & 0xFF;
                green += (pixels[pixel] >> 8) & 0xFF;
                blue += pixels[pixel] & 0xFF;
                kept++;
            }
        memset(heights, 0, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL);
        if (kept == 0 || kept == DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
            return;
        if (outline)
        {
            /* the rock's edge over the ground (Route 106's rim, a reef in the
             * sea): each pixel of the outline its distance in from the ground
             * beside it in the cell (a chamfer, in pixels), the ground 0 */
            uint8_t distance[DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL];
            int pixel;

            for (pixel = 0; pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                distance[pixel] = (rows[pixel / DIORAMA_VOXELS_PER_CELL]
                                   & (1u << (pixel % DIORAMA_VOXELS_PER_CELL))) ? 255 : 0;
            for (int pass = 0; pass < 2; pass++)
                for (int step = 0; step < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; step++)
                {
                    int at = pass == 0 ? step : DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL - 1 - step;
                    int x = at % DIORAMA_VOXELS_PER_CELL;
                    int y = at / DIORAMA_VOXELS_PER_CELL;
                    int dx = pass == 0 ? -1 : 1;
                    int dy = pass == 0 ? -1 : 1;

                    if (distance[at] == 0)
                        continue;
                    if (x + dx >= 0 && x + dx < DIORAMA_VOXELS_PER_CELL
                     && distance[at + dx] + 1 < distance[at])
                        distance[at] = distance[at + dx] + 1;
                    if (y + dy >= 0 && y + dy < DIORAMA_VOXELS_PER_CELL
                     && distance[at + dy * DIORAMA_VOXELS_PER_CELL] + 1 < distance[at])
                        distance[at] = distance[at + dy * DIORAMA_VOXELS_PER_CELL] + 1;
                }
            for (pixel = 0; pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                heights[pixel] = distance[pixel] > DIORAMA_VOXELS_PER_CELL
                               ? DIORAMA_VOXELS_PER_CELL : distance[pixel];
            *rockMass = ROCK_MASS_OUTLINE;
            return;
        }
        rounded = green * 10 > red * 11 && green > blue;
        if (rounded)
            *rockMass = ROCK_MASS_CROWN;
        /* Only a crown stands up from the rock. What else the upper layer draws
         * on it is what the game draws over a walker - the lower half of a face,
         * a ledge's rim (Lavaridge's) - and stood up as crags it was rows of low
         * walls along every face (Route 112's "maze of walls", the owner's
         * report of 2026-10-24): it lies on the rock as drawn. */
        if (!rounded)
            return;
    }

    if (!rock && (visible == 0 || visible == DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL))
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
        if (rounded && maxDepth > 1)
        {
            /* a crown: 6 at the edge, rising fast and flattening to the
             * cell's height in the middle (1 - (1 - t)^2) */
            unsigned span = maxDepth - 1;
            unsigned rest = span - (pixelDepth - 1);

            heights[pixel] = 6 + (DIORAMA_VOXELS_PER_CELL - 6) * (span * span - rest * rest)
                                 / (span * span);
            continue;
        }
        if (rock)
        {
            /* a crag: from its outline up to its middle */
            heights[pixel] = pixelDepth * DIORAMA_VOXELS_PER_CELL / maxDepth;
            continue;
        }
        heights[pixel] = maxDepth == 1 ? DIORAMA_VOXELS_PER_CELL
            : 4 + (pixelDepth - 1) * (DIORAMA_VOXELS_PER_CELL - 4)
                / (maxDepth - 1);
    }
}


/* The foot of a rock mass's slope on one side, in voxels over the cell's own
 * ground: the ground beside it, up to the rock's top (`rise`). A neighbour of
 * the same mass (rock on the same ground) joins it: -1, no slope. The slope came
 * down to the mass's own ground on every open side, so where the ground beside
 * it stands higher - a path on the mountain level with its top, another piece
 * of rock on a higher ground - it cut a trench a level deep along it (the
 * owner's report of 2026-10-24); it now meets that ground. */
static float RockMassFoot(const struct DioramaTerrainChunkInput *input, int x, int y,
                          const struct DioramaTerrainCell *cell, float rise)
{
    const struct DioramaTerrainCell *other;
    float foot;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return 0.0f;    /* past the chunk's halo: lower is the safe guess (a face drawn twice, never a hole) */
    other = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
    if (!other->present)
        return 0.0f;
    if (other->rockMass && other->groundHeight == cell->groundHeight)
        return -1.0f;
    foot = (other->groundHeight - cell->groundHeight) * DIORAMA_VOXELS_PER_CELL;
    return foot < 0.0f ? 0.0f : foot > rise ? rise : foot;
}

static float RockMassEase(float t)
{
    if (t >= 1.0f)
        return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* One side's (or corner's) profile: from its foot up to `top`, eased over `t`;
 * a joined side (foot -1) or one whose ground is as high as the rock none. */
static float RockMassSide(float level, float foot, float t, float top)
{
    float side;

    if (foot < 0.0f || foot >= top)
        return level;
    side = foot + RockMassEase(t) * (top - foot);
    return side < level ? side : level;
}

/* A mountain is one mass of rock (the owner's report of 2026-10-24: Route 106's
 * mountain a field of flat-topped cubes at uneven heights, Route 112's rock a
 * maze of walls). Each of its cells stands its record's height over its
 * ground, rising to it from the mass's foot: over the whole cell where the
 * mass ends to the south (the face the GBA draws there: the drawing lies on
 * the slope), within ROCK_MASS_EDGE pixels elsewhere, rounded at the corners;
 * inside the mass it runs on level from cell to cell. The foot on each side is
 * the ground beside it (RockMassFoot): a path level with the rock's top meets
 * it flat. The cell's own relief (BuildMountainArtMask) stands on it. A cell's
 * heights depend only on it and its eight neighbours, so a chunk's halo is all
 * it needs. */
static void ShapeRockMasses(struct DioramaTerrainChunkInput *input)
{
    for (int y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            struct DioramaTerrainCell *cell = &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
            float north, south, east, west, northEast, northWest, southEast, southWest;
            float outlineBase;
            float rise;

            if (!cell->present || !cell->rockMass)
                continue;
            rise = (cell->visualHeight - cell->groundHeight) * DIORAMA_VOXELS_PER_CELL;
            if (rise < 1.0f)
                rise = DIORAMA_VOXELS_PER_CELL;
            north = RockMassFoot(input, x, y - 1, cell, rise);
            south = RockMassFoot(input, x, y + 1, cell, rise);
            east = RockMassFoot(input, x + 1, y, cell, rise);
            west = RockMassFoot(input, x - 1, y, cell, rise);
            northEast = RockMassFoot(input, x + 1, y - 1, cell, rise);
            northWest = RockMassFoot(input, x - 1, y - 1, cell, rise);
            southEast = RockMassFoot(input, x + 1, y + 1, cell, rise);
            southWest = RockMassFoot(input, x - 1, y + 1, cell, rise);
            /* the ground part of a cell whose upper layer draws the rock's edge
             * lies on the lowest ground beside it (its open sides'), not always
             * on its own: Route 116's rock edge (General 112) against grass a
             * level up left that grass a groove a level deep, its side the sky's
             * blue (the owner's report of 2026-10-24) */
            outlineBase = -1.0f;
            if (north >= 0.0f) outlineBase = north;
            if (south >= 0.0f && (outlineBase < 0.0f || south < outlineBase)) outlineBase = south;
            if (east >= 0.0f && (outlineBase < 0.0f || east < outlineBase)) outlineBase = east;
            if (west >= 0.0f && (outlineBase < 0.0f || west < outlineBase)) outlineBase = west;
            if (outlineBase < 0.0f)
                outlineBase = 0.0f;
            for (int pixelY = 0; pixelY < DIORAMA_VOXELS_PER_CELL; pixelY++)
                for (int pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
                {
                    /* distances to the mass's foot, each as a share of how far
                     * the rock takes to rise on that side */
                    float toNorth = (pixelY + 0.5f) / ROCK_MASS_EDGE;
                    float toSouth = (DIORAMA_VOXELS_PER_CELL - pixelY - 0.5f) / ROCK_MASS_FACE;
                    float toWest = (pixelX + 0.5f) / ROCK_MASS_EDGE;
                    float toEast = (DIORAMA_VOXELS_PER_CELL - pixelX - 0.5f) / ROCK_MASS_EDGE;
                    float top;
                    float level;
                    int pixel = pixelY * DIORAMA_VOXELS_PER_CELL + pixelX;

                    if (cell->rockMass == ROCK_MASS_OUTLINE)
                    {
                        /* the ground beside the outline is the mass's foot too */
                        float inside = cell->mountainHeight[pixel] == 0 ? 0.0f
                                     : (cell->mountainHeight[pixel] - 0.5f) / ROCK_MASS_EDGE;

                        top = rise;
                        level = outlineBase + RockMassEase(inside) * (rise - outlineBase);
                    }
                    else
                    {
                        top = rise + cell->mountainHeight[pixel]
                            * (cell->rockMass == ROCK_MASS_CROWN ? ROCK_MASS_CROWN_RELIEF
                                                                 : ROCK_MASS_RELIEF)
                            / (float)DIORAMA_VOXELS_PER_CELL;
                        level = top;
                    }
                    level = RockMassSide(level, north, toNorth, top);
                    level = RockMassSide(level, south, toSouth, top);
                    level = RockMassSide(level, west, toWest, top);
                    level = RockMassSide(level, east, toEast, top);
                    if (north < 0.0f && east < 0.0f)
                        level = RockMassSide(level, northEast,
                                             sqrtf(toNorth * toNorth + toEast * toEast), top);
                    if (north < 0.0f && west < 0.0f)
                        level = RockMassSide(level, northWest,
                                             sqrtf(toNorth * toNorth + toWest * toWest), top);
                    if (south < 0.0f && east < 0.0f)
                        level = RockMassSide(level, southEast,
                                             sqrtf(toSouth * toSouth + toEast * toEast), top);
                    if (south < 0.0f && west < 0.0f)
                        level = RockMassSide(level, southWest,
                                             sqrtf(toSouth * toSouth + toWest * toWest), top);
                    cell->mountainHeight[pixel] = level < 1.0f ? (level > 0.25f ? 1 : 0)
                                                : level > 255.0f ? 255 : (uint8_t)(level + 0.5f);
                }
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
                                   RiserShowsWholeCell(snapshot, source, cell->shape)
                                   ? DIORAMA_MATERIAL_FULL : DIORAMA_MATERIAL_BASE);
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
                BuildMountainArtMask(snapshot, source, cell->shape == DIORAMA_SHAPE_CLIFF,
                                     resolvedCells[gridIndex].artMode == DIORAMA_ART_CUTOUT,
                                     cell->terraceProfile,
                                     cell->foregroundAlpha,
                                     cell->mountainHeight, &cell->rockMass);
            if (cell->shape == DIORAMA_SHAPE_LEDGE)
            {
                DioramaAtlas_GetForegroundAlphaMask(DioramaAtlas_CellSlot(source),
                                                    cell->foregroundAlpha);
                SetTerrainMaterial(&cell->materials[DIORAMA_MATERIAL_FACE_TOP],
                                   source->metatileId, DIORAMA_MATERIAL_BASE);
                /* A ledge on the ground draws its lip as faces of its own
                 * (terrain_mesh.c); its sides are the lip alone. One a terrace
                 * raised or lowered is a block whose sides are the whole
                 * drawing, ground and lip: the lip alone left them see-through
                 * (graphics overhaul, 2026-10-12). */
                for (face = DIORAMA_MATERIAL_FACE_NORTH;
                     face <= DIORAMA_MATERIAL_FACE_WEST; face++)
                    SetTerrainMaterial(&cell->materials[face], source->metatileId,
                                        cell->groundHeight == 0.0f ? DIORAMA_MATERIAL_FOREGROUND
                                                                   : DIORAMA_MATERIAL_FULL);
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
            else if (cell->shape == DIORAMA_SHAPE_CLIFF
                  && (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                  && cell->terraceProfile == DIORAMA_TERRACE_NONE)
            {
                /* A mountain's rock (BuildMountainArtMask, ShapeRockMasses): the
                 * whole drawing on its top and its sides, the rock as the GBA
                 * shows it from above (its lower layer alone is the ground under
                 * it). */
                for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                    SetTerrainMaterial(&cell->materials[face], source->metatileId,
                                       DIORAMA_MATERIAL_FULL);
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
    ShapeRockMasses(input);
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

/* PKMN_DIORAMA_CHUNK_LOG=1: what each chunk built costs (development). */
static bool ChunkLog(void)
{
    static int sChunkLog = -1;

    if (sChunkLog < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_CHUNK_LOG");

        sChunkLog = value != NULL && value[0] == '1';
    }
    return sChunkLog != 0;
}

/*
 * PKMN_DIORAMA_MESH_CHECK=1 (tests): each chunk built again through the voxel
 * spans alone (DioramaTerrain_SetVoxelOccupancyOnly) and compared with the
 * build drawn: its vertices byte for byte, its bounds and its trees. Slow.
 */
static void CheckChunkMesh(const struct DioramaTerrainChunkInput *input, const struct DioramaTerrainMesh *mesh)
{
    static int sEnabled = -1;
    static uint32_t sChecked;
    static uint32_t sDiffering;
    static uint32_t sCompressed;
    struct DioramaTerrainVertex *vertices;
    struct DioramaTerrainMesh voxel;
    uint32_t capacity;
    bool same;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_MESH_CHECK");

        sEnabled = value != NULL && value[0] == '1';
    }
    if (!sEnabled)
        return;
    capacity = mesh->vertexCount * 2 + 4096;
    vertices = malloc(capacity * sizeof(*vertices));
    if (vertices == NULL)
        return;
    DioramaTerrain_SetVoxelOccupancyOnly(true);
    same = DioramaTerrain_BuildChunk(input, vertices, capacity, &voxel);
    DioramaTerrain_SetVoxelOccupancyOnly(false);
    same = same && voxel.vertexCount == mesh->vertexCount
        && memcmp(vertices, sScratchVertices, mesh->vertexCount * sizeof(*vertices)) == 0
        && memcmp(&voxel.bounds, &mesh->bounds, sizeof(voxel.bounds)) == 0
        && voxel.treeInstanceCount == mesh->treeInstanceCount
        && memcmp(voxel.treeInstances, mesh->treeInstances,
                  mesh->treeInstanceCount * sizeof(mesh->treeInstances[0])) == 0
        && voxel.geometryHash == mesh->geometryHash
        && voxel.topFaceCount == mesh->topFaceCount && voxel.bottomFaceCount == mesh->bottomFaceCount
        && voxel.sideFaceCount == mesh->sideFaceCount && voxel.featureFaceCount == mesh->featureFaceCount;
    free(vertices);
    sChecked++;
    sCompressed += mesh->usedCompressedOccupancy;
    if (!same)
    {
        sDiffering++;
        SDL_Log("Diorama: mesh check: chunk %d,%d differs from its voxel build (%u against %u vertices, "
                "compressed %d)", input->chunkX, input->chunkY, mesh->vertexCount, voxel.vertexCount,
                mesh->usedCompressedOccupancy);
    }
    if ((sChecked & 15) == 0 || !same)
        SDL_Log("Diorama: mesh check: %u chunks checked (%u compressed), %u differ", sChecked, sCompressed,
                sDiffering);
}

static bool UploadChunk(struct GLTerrainChunk *chunk,
                        const struct DioramaTerrainChunkInput *input,
                        uint64_t signature)
{
    struct DioramaTerrainMesh mesh;

    if (!DioramaTerrain_BuildChunk(input, sScratchVertices,
                                   DIORAMA_TERRAIN_MAX_VERTICES, &mesh))
        return false;
    CheckChunkMesh(input, &mesh);
    {
        if (ChunkLog())
        {
            unsigned mountain = 0, terrace = 0, cells = 0, i;

            for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
            {
                const struct DioramaTerrainCell *cell = &input->cells[i];

                if (!cell->present)
                    continue;
                cells++;
                if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                {
                    mountain++;
                    terrace += cell->terraceProfile != DIORAMA_TERRACE_NONE;
                }
            }
            SDL_Log("Diorama: chunk %d,%d: %u vertices, faces top %u bottom %u side %u, "
                    "spans %u, compressed %u, cells %u (mountain art %u, terraced %u)",
                    input->chunkX, input->chunkY, mesh.vertexCount, mesh.topFaceCount,
                    mesh.bottomFaceCount, mesh.sideFaceCount, mesh.occupancySpanCount,
                    mesh.usedCompressedOccupancy, cells, mountain, terrace);
        }
    }
    if (chunk->vertexArray == 0)
    {
        dglGenVertexArrays(1, &chunk->vertexArray);
        dglGenBuffers(1, &chunk->vertexBuffer);
        ConfigureVertexArray(chunk->vertexArray, chunk->vertexBuffer);
    }
    /* Its quads drawn from their four corners (performance, 2026-10-12: two
     * of a quad's six vertices repeat; every terrain pass shades a third
     * fewer and the buffers are a quarter smaller). The indices are written
     * into the scratch's unused tail - no memory of their own; a chunk too
     * big to leave room for them is drawn as before. */
    chunk->indexCount = 0;
    chunk->drawVertices = mesh.vertexCount;
    if ((uint64_t)(DIORAMA_TERRAIN_MAX_VERTICES - mesh.vertexCount) * sizeof(struct DioramaTerrainVertex)
        >= (uint64_t)mesh.vertexCount * sizeof(uint32_t))
    {
        uint32_t *indices = (uint32_t *)(void *)(sScratchVertices + mesh.vertexCount);
        uint32_t indexCount;
        uint32_t vertices = DioramaTerrain_FoldQuads(sScratchVertices, mesh.vertexCount,
                                                     indices, &indexCount);
        size_t indexSize = sizeof(uint32_t);

        chunk->indexType = GL_UNSIGNED_INT;
        if (vertices <= 0x10000)
        {
            /* in place: the uint16_t at i ends before the uint32_t at i+1 begins */
            uint16_t *shortIndices = (uint16_t *)(void *)indices;
            uint32_t i;

            for (i = 0; i < indexCount; i++)
                shortIndices[i] = (uint16_t)indices[i];
            chunk->indexType = GL_UNSIGNED_SHORT;
            indexSize = sizeof(uint16_t);
        }
        if (chunk->indexBuffer == 0)
            dglGenBuffers(1, &chunk->indexBuffer);
        /* the element buffer is the vertex array's: bind that first */
        dglBindVertexArray(chunk->vertexArray);
        dglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->indexBuffer);
        dglBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * indexSize, indices, GL_DYNAMIC_DRAW);
        chunk->indexCount = indexCount;
        chunk->drawVertices = vertices;
    }
    if (ChunkLog())
        SDL_Log("Diorama: chunk %d,%d: drawn from %u vertices, %u indices (%s)",
                input->chunkX, input->chunkY, chunk->drawVertices, chunk->indexCount,
                chunk->indexCount == 0 ? "none"
                : chunk->indexType == GL_UNSIGNED_SHORT ? "16-bit" : "32-bit");
    dglBindBuffer(GL_ARRAY_BUFFER, chunk->vertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER,
                  chunk->drawVertices * sizeof(struct DioramaTerrainVertex),
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

/* A chunk's triangles, from its indices when UploadChunk folded its quads. */
static void DrawChunk(const struct GLTerrainChunk *chunk)
{
    if (chunk->indexCount != 0)
        glDrawElements(GL_TRIANGLES, (GLsizei)chunk->indexCount, chunk->indexType, NULL);
    else
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)chunk->drawVertices);
}

static void ReleaseChunk(struct GLTerrainChunk *chunk)
{
    if (chunk->indexBuffer != 0)
        dglDeleteBuffers(1, &chunk->indexBuffer);
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

/* The tiles that changed since the chunks were built (sSyncTiles), into
 * sSyncDirtyTiles. */
static void FindDirtyTiles(const struct DioramaSceneSnapshot *snapshot)
{
    unsigned tile;

    memset(sSyncDirtyTiles, 0, sizeof(sSyncDirtyTiles));
    for (tile = 0; tile < DIORAMA_TILE_COUNT; tile++)
        if (memcmp(snapshot->tileGraphics + tile * DIORAMA_TILE_BYTES, sSyncTiles + tile * DIORAMA_TILE_BYTES,
                   DIORAMA_TILE_BYTES) != 0)
            sSyncDirtyTiles[tile / 8] |= 1 << (tile % 8);
}

/* The chunks a change of the animated tiles alone can reach, built again;
 * the rest stand. */
static bool SyncAnimatedTiles(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaResolvedCell *resolvedCells,
                              const struct TerrainSyncKey *key)
{
    static struct DioramaTerrainChunkInput input;
    unsigned rebuilt = 0;
    bool prepared = false;
    int i;

    FindDirtyTiles(snapshot);
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

/* Whether any of a chunk's own cells is in the snapshot: BuildInput builds
 * no input for one with none. */
static bool ChunkHasInterior(const struct DioramaSceneSnapshot *snapshot, int chunkX, int chunkY)
{
    int x;
    int y;

    for (y = 0; y < DIORAMA_TERRAIN_CHUNK_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            int mapX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE + x;
            int mapY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE + y;
            int gridX = mapX - snapshot->gridOriginX;
            int gridY = mapY - snapshot->gridOriginY;
            int gridIndex = gridY * DIORAMA_GRID_WIDTH + gridX;
            const struct DioramaCellSnapshot *source;

            if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT
             || gridIndex >= snapshot->visibleCellCount)
                continue;
            source = &snapshot->cells[gridIndex];
            if (source->mapX == mapX && source->mapY == mapY && source->metatileId < DIORAMA_TILE_COUNT)
                return true;
        }
    return false;
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

    /* A chunk off screen with the sync's allowance for them spent waits for
     * its build whatever its input holds, so that input is not built: every
     * waiting chunk's input was built again each frame to find two to build,
     * a dozen a frame after each step in Petalburg Woods (performance work
     * 2026-10-24). The decision is the one SyncChunk took below: it waits,
     * its old mesh standing (a chunk whose input turns out the same keeps
     * that mesh either way). */
    if (!forceAllDirty && *offscreenBuilds >= TERRAIN_OFFSCREEN_BUILDS)
    {
        bool dirty = false;

        chunk = FindChunk(chunkX, chunkY);
        for (dirtyIndex = 0; !dirty && dirtyIndex < snapshot->dirtyCellCount; dirtyIndex++)
            dirty = DioramaTerrain_DirtyCellAffectsChunk(snapshot->dirtyCells[dirtyIndex].mapX,
                                                         snapshot->dirtyCells[dirtyIndex].mapY,
                                                         chunkX, chunkY);
        if (!dirty && !ChunkUrgent(snapshot, chunk, chunkX, chunkY)
         && (chunk == NULL
          || (chunk->mapGeneration == snapshot->mapGeneration && !chunk->hasPixelObjects
           && ChunkHasInterior(snapshot, chunkX, chunkY))))
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
    }
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
    bool tilesMoved = false;
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

    /* Every sync, whether or not a chunk is rebuilt: the palette's fade and the
     * night tint change without the cells changing (the voxel trees and the
     * tree sprites read it every frame). */
    TreeTint(snapshot);
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
             * cells are all the same cells, resolved the same, stands. The
             * animated tiles may have moved on in the same frame (they do
             * every few frames: a step met them about every other time in
             * Petalburg Woods, and all 49 chunks' inputs were built again
             * for it - performance work 2026-10-24): then a chunk that reads
             * the pixels of a tile that changed is looked at too, as
             * SyncAnimatedTiles does. */
            moved.gridOriginX = sSyncKey.gridOriginX;
            moved.gridOriginY = sSyncKey.gridOriginY;
            moved.visibleCellCount = sSyncKey.visibleCellCount;
            moved.tilesetAnimationGeneration = sSyncKey.tilesetAnimationGeneration;
            gridMovedOnly = sSyncKeyValid && !forceAllDirty && snapshot->dirtyCellCount == 0
                         && memcmp(&moved, &sSyncKey, sizeof(key)) == 0;
            tilesMoved = gridMovedOnly && key.tilesetAnimationGeneration != sSyncKey.tilesetAnimationGeneration;
            if (tilesMoved)
                FindDirtyTiles(snapshot);
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
             && !(tilesMoved && chunk->readsPixelCells
               && ChunkUsesDirtyTiles(snapshot, resolvedCells, chunkX, chunkY))
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
            SDL_Log("Diorama: map %u.%u (layout %u): %u chunks from the cache, %u built, %u waiting",
                    snapshot->mapGroup, snapshot->mapNum, snapshot->mapLayoutId,
                    sCacheHits - sHitsAtChange, sMetrics.rebuiltChunks, sPendingCount);
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

/* Trees, as instances of the model's faces in one range of indices. */
static void DrawTrees(const struct DioramaTerrainTreeInstance *trees, uint32_t count, int range)
{
    if (count == 0)
        return;
    dglBindVertexArray(sTreeVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
    dglBufferData(GL_ARRAY_BUFFER, count * sizeof(struct DioramaTerrainTreeInstance), trees, GL_STREAM_DRAW);
    dglDrawElementsInstanced(GL_TRIANGLES, (GLsizei)sTreeRangeCount[range], sTreeIndexType,
                             (const void *)(uintptr_t)(sTreeRangeFirst[range] * sTreeIndexSize), (GLsizei)count);
    /* the element buffer stays the tree array's alone */
    dglBindVertexArray(0);
}

/*
 * PKMN_DIORAMA_TREE_CHECK=1: this frame's trees drawn as before (oldCount of
 * sCheckTreeInstances, every face, six vertices a face) and as now
 * (newCount of sVisibleTreeInstances in their range), each over its own copy
 * of the picture so far (a framebuffer of its own, so both start from the
 * same depth, whatever the window's depth format); the pixels whose colour
 * or depth came out different are counted. Then the frame's own trees, as
 * now.
 */
static void CheckTrees(uint32_t oldCount, uint32_t newCount, int range)
{
    static GLuint sFramebuffer;
    static GLuint sColor;
    static GLuint sDepthStencil;
    static int sWidth;
    static int sHeight;
    static uint8_t *sPixels[2];
    static uint32_t *sDepths[2];
    static bool sBroken;
    static unsigned sFrames;
    static unsigned sFramesDiffering;
    static unsigned sWritten;
    static uint64_t sPixelsDiffering;
    GLint viewport[4];
    GLuint target = DioramaGLTarget_Framebuffer();
    int width;
    int height;
    int pass;
    uint32_t colourDiffer = 0;
    uint32_t depthDiffer = 0;
    size_t p;

    glGetIntegerv(GL_VIEWPORT, viewport);
    width = viewport[2];
    height = viewport[3];
    if (!sBroken && width > 0 && height > 0 && (sFramebuffer == 0 || width != sWidth || height != sHeight))
    {
        if (sFramebuffer == 0)
        {
            dglGenFramebuffers(1, &sFramebuffer);
            dglGenRenderbuffers(1, &sColor);
            dglGenRenderbuffers(1, &sDepthStencil);
        }
        dglBindRenderbuffer(GL_RENDERBUFFER, sColor);
        dglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        dglBindRenderbuffer(GL_RENDERBUFFER, sDepthStencil);
        dglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        dglBindRenderbuffer(GL_RENDERBUFFER, 0);
        dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
        dglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sColor);
        dglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sDepthStencil);
        for (pass = 0; pass < 2; pass++)
        {
            free(sPixels[pass]);
            free(sDepths[pass]);
            sPixels[pass] = malloc((size_t)width * height * 4);
            sDepths[pass] = malloc((size_t)width * height * sizeof(uint32_t));
        }
        sWidth = width;
        sHeight = height;
        if (dglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE
         || sPixels[0] == NULL || sPixels[1] == NULL || sDepths[0] == NULL || sDepths[1] == NULL)
        {
            SDL_Log("Diorama: tree check unavailable (no framebuffer for the copies)");
            sBroken = true;
        }
        dglBindFramebuffer(GL_FRAMEBUFFER, target);
    }
    for (pass = 0; pass < 2 && !sBroken && width > 0 && height > 0; pass++)
    {
        while (glGetError() != GL_NO_ERROR)
            ;
        /* the picture so far, into the copy (the same viewport) */
        dglBindFramebuffer(GL_READ_FRAMEBUFFER, target);
        dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, sFramebuffer);
        dglBlitFramebuffer(viewport[0], viewport[1], viewport[0] + width, viewport[1] + height,
                           viewport[0], viewport[1], viewport[0] + width, viewport[1] + height,
                           GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
        dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
        if (glGetError() != GL_NO_ERROR)
        {
            SDL_Log("Diorama: tree check unavailable (the picture could not be copied)");
            sBroken = true;
            break;
        }
        if (pass == 0)
        {
            /* as before */
            dglBindVertexArray(sTreeCheckVertexArray);
            dglBindBuffer(GL_ARRAY_BUFFER, sTreeInstanceBuffer);
            dglBufferData(GL_ARRAY_BUFFER, oldCount * sizeof(struct DioramaTerrainTreeInstance),
                          sCheckTreeInstances, GL_STREAM_DRAW);
            if (oldCount != 0)
                dglDrawArraysInstanced(GL_TRIANGLES, 0, sTreeModelVertexCount, oldCount);
            dglBindVertexArray(0);
        }
        else
            DrawTrees(sVisibleTreeInstances, newCount, range);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(viewport[0], viewport[1], width, height, GL_RGBA, GL_UNSIGNED_BYTE, sPixels[pass]);
        glReadPixels(viewport[0], viewport[1], width, height, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, sDepths[pass]);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    dglBindFramebuffer(GL_FRAMEBUFFER, target);
    /* the frame's own trees */
    DrawTrees(sVisibleTreeInstances, newCount, range);
    if (sBroken || width <= 0 || height <= 0)
        return;
    for (p = 0; p < (size_t)width * height; p++)
    {
        colourDiffer += memcmp(&sPixels[0][p * 4], &sPixels[1][p * 4], 3) != 0;
        depthDiffer += sDepths[0][p] != sDepths[1][p];
    }
    sFrames++;
    sPixelsDiffering += colourDiffer;
    if (colourDiffer != 0 || depthDiffer != 0)
    {
        sFramesDiffering++;
        if (sWritten < 3)
        {
            char path[64];

            /* where they differ, in white over the frame as now, darkened */
            for (p = 0; p < (size_t)width * height; p++)
            {
                bool differs = memcmp(&sPixels[0][p * 4], &sPixels[1][p * 4], 3) != 0
                            || sDepths[0][p] != sDepths[1][p];

                sDepths[1][p] = differs ? 0xFFFFFFFFu
                              : 0xFF000000u | (uint32_t)(sPixels[1][p * 4] / 3)
                              | ((uint32_t)(sPixels[1][p * 4 + 1] / 3) << 8)
                              | ((uint32_t)(sPixels[1][p * 4 + 2] / 3) << 16);
            }
            SDL_snprintf(path, sizeof(path), "tree-check-%u-before.png", sWritten);
            PngWrite_RGBA(path, sPixels[0], width, height, true);
            SDL_snprintf(path, sizeof(path), "tree-check-%u-after.png", sWritten);
            PngWrite_RGBA(path, sPixels[1], width, height, true);
            SDL_snprintf(path, sizeof(path), "tree-check-%u-differ.png", sWritten);
            PngWrite_RGBA(path, (const uint8_t *)sDepths[1], width, height, true);
            sWritten++;
        }
    }
    SDL_Log("Diorama: tree check: %u trees as before, %u as now (%s faces): %u pixels differ in colour, "
            "%u in depth; %u of %u frames differed, %llu pixels in all",
            oldCount, newCount, range == TREE_RANGE_VIEW ? "seen" : "all", colourDiffer, depthDiffer,
            sFramesDiffering, sFrames, (unsigned long long)sPixelsDiffering);
}

void DioramaGLTerrain_Draw(GLuint atlasTexture, GLuint baseAtlasTexture,
                           GLuint foregroundAtlasTexture, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection, bool debug)
{
    uint32_t debugVertexCount = sClaimDebugVertexCount;
    uint32_t visibleTreeCount = 0;
    uint32_t checkTreeCount = 0;
    /* Every face unless asked for the seen ones alone; the terrain debug
     * view's lines show every face. */
    int treeRange = !debug && TreeFacesSeen() ? TREE_RANGE_VIEW : TREE_RANGE_ALL;
    struct TreeEye eye = TreeEyeFor(cameraX, cameraZ, cameraPitch);
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
        sMetrics.vertices += chunk->drawVertices;
        sMetrics.triangles += chunk->mesh.vertexCount / 3;
        sMetrics.drawCalls++;
        dglBindVertexArray(chunk->vertexArray);
        DrawChunk(chunk);
        /* Its trees on screen (the chunk's bounds hold them all, but a
         * chunk at the picture's edge has some off it). */
        for (uint16_t tree = 0; tree < chunk->mesh.treeInstanceCount; tree++)
        {
            const struct DioramaTerrainTreeInstance *instance = &chunk->mesh.treeInstances[tree];
            struct DioramaTerrainBounds bounds;

            if (checkTreeCount < TERRAIN_MAX_VISIBLE_TREE_INSTANCES && TreeCheckEnabled())
                sCheckTreeInstances[checkTreeCount++] = *instance;
            if (visibleTreeCount >= TERRAIN_MAX_VISIBLE_TREE_INSTANCES)
                continue;
            bounds = TreeBounds(instance);
            if (!DioramaTerrain_IsBoundsVisible(&bounds, cameraX, cameraZ, cameraPitch, focalLength))
                continue;
            if (treeRange == TREE_RANGE_VIEW && !TreeSeenFromAboveSouth(&bounds, &eye))
                treeRange = TREE_RANGE_ALL;
            sVisibleTreeInstances[visibleTreeCount++] = *instance;
        }
        if (debug)
            debugVertexCount = AppendBoundsVertices(debugVertexCount, &chunk->mesh.bounds);
    }
    if (visibleTreeCount != 0 || checkTreeCount != 0)
    {
        if (!debug)
        {
            dglUniform3f(sPixelScaleLocation, sTreeScale[0], sTreeScale[1], sTreeScale[2]);
            dglUniform3f(sPixelOffsetLocation, sTreeOffset[0], sTreeOffset[1], sTreeOffset[2]);
        }
        if (TreeCheckEnabled())
            CheckTrees(checkTreeCount, visibleTreeCount, treeRange);
        else
            DrawTrees(sVisibleTreeInstances, visibleTreeCount, treeRange);
        dglUniform3f(sPixelScaleLocation, 1.0f, 1.0f, 1.0f);
        dglUniform3f(sPixelOffsetLocation, 0.0f, 0.0f, 0.0f);
        sMetrics.vertices += sTreeRangeCount[treeRange] * visibleTreeCount;
        sMetrics.triangles += sTreeRangeCount[treeRange] * visibleTreeCount / 3;
        sMetrics.drawCalls++;
    }
    sMetrics.visibleTreeInstances = visibleTreeCount;
    sMetrics.treeModelVertices = sTreeRangeCount[treeRange];
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
        DrawChunk(chunk);
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
        DrawChunk(chunk);
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
        DrawChunk(chunk);
        for (uint16_t tree = 0; tree < chunk->mesh.treeInstanceCount
             && treeCount < TERRAIN_MAX_VISIBLE_TREE_INSTANCES; tree++)
            sVisibleTreeInstances[treeCount++] = chunk->mesh.treeInstances[tree];
    }
    /* every face: the sun sees them from another side */
    DrawTrees(sVisibleTreeInstances, treeCount, TREE_RANGE_ALL);
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
        if (sChunks[i].indexBuffer != 0)
            dglDeleteBuffers(1, &sChunks[i].indexBuffer);
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
    if (sTreeIndexBuffer != 0)
        dglDeleteBuffers(1, &sTreeIndexBuffer);
    if (sTreeVertexBuffer != 0)
        dglDeleteBuffers(1, &sTreeVertexBuffer);
    if (sTreeVertexArray != 0)
        dglDeleteVertexArrays(1, &sTreeVertexArray);
    if (sTreeCheckVertexBuffer != 0)
        dglDeleteBuffers(1, &sTreeCheckVertexBuffer);
    if (sTreeCheckVertexArray != 0)
        dglDeleteVertexArrays(1, &sTreeCheckVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    memset(sChunks, 0, sizeof(sChunks));
    sDebugVertexBuffer = 0;
    sDebugVertexArray = 0;
    sTreeInstanceBuffer = 0;
    sTreeIndexBuffer = 0;
    sTreeVertexBuffer = 0;
    sTreeVertexArray = 0;
    sTreeCheckVertexBuffer = 0;
    sTreeCheckVertexArray = 0;
    sTreeModelVertexCount = 0;
    sProgram = 0;
}

#endif
