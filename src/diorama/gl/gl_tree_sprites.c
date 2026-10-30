#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_grade.h"
#include "diorama/gl_lamps.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_shadow.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/terrain_mesh.h"
#include "diorama/gl_tree_sprites.h"
#include "diorama/rules.h"
#include "diorama/tree_sprites.h"
#include "diorama/trees_3ds.generated.h"

/*
 * Step 16 (the world geometry overhaul, phase C3; the owner: "trees must have
 * actual 3D trunks and canopy volume"): each crown stands as a solid of voxels
 * made from its own art, where the cut-out stood - each leaf pixel a column
 * through the crown's plane, longest where it is furthest inside the outline
 * (a rounded crown: the art on its front and back, each pixel's colour down its
 * sides) - and a trunk of bark stands under it, its foot on the trunk's art.
 * The models are made once from the texture (BuildModels); each tree cell draws
 * its part of them (a large tree's quarter, a small tree) as an instance, so a
 * tree that crosses a chunk or map edge is drawn once, as the cut-outs were.
 * PKMN_DIORAMA_TREE_CARDS=1 draws the cut-outs instead (tests: before and after).
 */
#define TEXEL (1.0f / DIORAMA_TREES_3DS_DIM)
/* The trunk's art a little over the ground it is laid on. */
#define TRUNK_LIFT 0.004f
/* The crowns' planes: 50 degrees from the ground, the high end to the north,
 * the art's transparent foot sunk into the ground (the 3DS port's). */
#define TILT_SIN 0.766044f
#define TILT_COS 0.642788f
#define CROWN_SINK 0.10f
/* A tile is 16 of the art's pixels; a voxel is one. */
#define PIXELS_PER_TILE 16.0f
/* A crown's depth in steps of four voxels, a quarter of a tile: the same
 * rounded crown from the field's cameras as steps of two (pictures compared),
 * for 58% of the indices: Petalburg Woods' frame from about 35 ms to 25 with
 * Mesa's software renderer (2026-10-30). */
#define CROWN_STEP 4
#define MAX_CROWN_LEVELS 8
#define NO_LEAF 0xFF
/* The art's faces start this far inside their texels, so nearest sampling at
 * a face's edge never takes the transparent texel beside it. */
#define TEXEL_INSET 0.01f
/* What a tree cell is (DioramaCellSnapshot.treeSprite - 1): a large tree's four
 * quarters, the small tree, Dewford Town's small tree, an ash-covered large
 * tree's four quarters (the Fallarbor tileset's), the Lavaridge tileset's large
 * tree's four quarters and its small tree (the General tileset's on the
 * volcano's ground: QuarterFirst), the Mossdeep tileset's round tree's four
 * quarters. */
#define PART_COUNT 19
/*
 * A large tree's quarter whose neighbour in its row (the other half of the
 * tree) or in its column (the other row) is not a tree cell - a wood's edge
 * cut by a cliff, a house or a map's border, some 2% of tree cells - stands
 * rounded off where that half would be, as at the outline, not cut flat:
 * each part's model in four kinds (a model slot: part * KINDS + kind, kind's
 * HALF_ALONE and ROW_ALONE bits). The small trees have one.
 */
#define KINDS 4
#define HALF_ALONE 1
#define ROW_ALONE 2
#define SLOT_COUNT (PART_COUNT * KINDS)
#define MAX_MODEL_VERTICES 49152
/*
 * How far from where the camera looks a tree stands as its solid (tiles); past it, its cut-out with its trunk, a
 * fraction of the vertices (step 16, phase D: with every tree solid, Petalburg Woods cost Mesa's software renderer
 * 230k vertices a frame against 132k before, 30.7 frames a second against 43.8; within 6 tiles, 109k and 41.1;
 * within 9, 149k and 36.4). From the field's cameras the cut-out is the solid's front: the probe's pictures of the
 * woods at 6 tiles and 9 look the same. PKMN_DIORAMA_TREE_REACH sets it for one run (0: every tree its solid).
 */
#define TREE_SOLID_REACH 6.0f
#define MAX_MODEL_INDICES (MAX_MODEL_VERTICES / 4 * 6)
#define MAX_CROWN_WIDTH 32
#define MAX_CROWN_HEIGHT 36

/* A model's corner: in its cell, from the cell's north-west corner on its
 * ground (x east, y up, z south). */
struct ModelVertex
{
    float x, y, z;
    float u, v;
    float shade;
};

/* A tree cell: its north-west corner on its ground, in the scene (z north). */
struct TreeInstance
{
    float x, y, z;
};

/*
 * A part's model in the index buffer, in this order: the cut-out of its crown
 * (only the sun's depth map draws it, unless the trees are the cut-outs), its
 * trunk, the trunk's art laid on the ground, the crown's faces an eye above
 * and south of it can see (every camera of the field is so), the crown's
 * faces looking down and north (its back). The colour pass draws from first:
 * seenCount indices when the eye is so for every tree it draws, else count;
 * the sun's depth map draws shadowCount from shadowFirst (the cut-out and the
 * trunk: a crown's shadow as the cut-out cast it, for a fraction of the solid's
 * cost each time the map is drawn again). bounds: the model's, in its cell.
 */
struct PartModel
{
    uint32_t first;
    uint32_t seenCount;
    uint32_t count;
    uint32_t shadowFirst;
    uint32_t shadowCount;
    uint32_t farFirst; /* a far tree's model (TREE_SOLID_REACH): the cut-out, the trunk, the trunk's art */
    uint32_t farCount;
    float minimum[3];
    float maximum[3];
};

/*
 * A crown's art in the texture (pack_trees.py) and where the 3DS port stands
 * it (voxel_tree.c EmitCell, EmitSmallCell): its plane's foot baseZ south of
 * the (upper left) cell's north edge, over columns x rows cells. The rows
 * from leafRows down are the trunk the small trees' art draws under their
 * leaves: the solid trunk stands there instead. thickness: how many voxels the
 * crown stands out of its plane each way at most, where it is roundness
 * pixels or more inside its outline (a quarter circle in between). The trunk
 * is trunkWidth voxels square, its bark the art's row from (barkX, barkY).
 */
struct CrownArt
{
    uint8_t x, y, width, height;
    uint8_t leafRows;
    uint8_t columns, rows;
    float baseZ;
    float thickness;
    float roundness;
    uint8_t trunkWidth;
    uint8_t barkX, barkY;
};

static const struct CrownArt sCrownArt[5] = {
    /* the large tree: its crown at 0,0, its trunk's art at 32,0 (a row of the stump's bark) */
    {0, 0, 32, 36, 36, 2, 2, 1.35f, 10.0f, 12.0f, 6, 32 + 7, 18},
    /* the small tree at 32,32, the trunk its art draws from row 24 */
    {32, 32, 16, 32, 24, 1, 1, 0.825f, 6.0f, 7.0f, 4, 32 + 4, 32 + 27},
    /* Dewford Town's at 64,0, from row 23 */
    {64, 0, 16, 32, 23, 1, 1, 0.975f, 6.0f, 7.0f, 4, 64 + 4, 26},
    /* the ash-covered large tree: the large crown in the ash trees' colours at 0,64, the same bark */
    {0, 64, 32, 36, 36, 2, 2, 1.35f, 10.0f, 12.0f, 6, 32 + 7, 18},
    /* the Mossdeep tileset's round tree at 96,0 (its own art), its trunk from row 22, the bark its row 26 */
    {96, 0, 32, 32, 22, 2, 2, 1.40f, 10.0f, 11.0f, 8, 96 + 12, 26},
};

static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLuint sIndexBuffer;
static GLuint sInstanceBuffer;
static GLuint sTexture;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
static GLint sCameraHeightLocation;
static GLint sCameraDistanceLocation;
static GLint sScreenOffsetLocation;
static GLint sImageLocation;
static GLint sShadowPassLocation;
static GLint sShadowEnabledLocation;
static GLint sLightMatrixLocation;
static GLint sShadowMapLocation;
static GLint sShadowAmbientLocation;
static GLint sSaturationLocation;
static GLint sPixelScaleLocation;
static GLint sPixelOffsetLocation;
static struct DioramaLightUniforms sLightUniforms;
static struct DioramaLampUniforms sLampUniforms;
static bool sVolumes;
static struct ModelVertex sModelVertices[MAX_MODEL_VERTICES];
static uint16_t sModelIndices[MAX_MODEL_INDICES];
static uint32_t sModelVertexCount;
static uint32_t sModelIndexCount;
static bool sModelFull;
static struct PartModel sParts[SLOT_COUNT];
/* Each pixel's level while a crown is made: how many CROWN_STEPs it stands out
 * of its plane past its middle voxel, NO_LEAF where there is no leaf. */
static uint8_t sLevels[MAX_CROWN_HEIGHT][MAX_CROWN_WIDTH];
static int8_t sMask[MAX_CROWN_HEIGHT * MAX_CROWN_WIDTH];
/* The instances, each part's together: every tree cell of the snapshot (the
 * sun's depth map), and those on screen (the colour pass, chosen each frame
 * the camera or the trees change; a solid crown is a few hundred faces, and
 * the snapshot holds some thousands of tree cells round a wood). */
static struct TreeInstance sInstances[DIORAMA_MAX_VISIBLE_CELLS];
static uint32_t sPartInstanceFirst[SLOT_COUNT];
static uint32_t sPartInstanceCount[SLOT_COUNT];
static uint32_t sInstanceCount;
static GLuint sVisibleBuffer;
static struct TreeInstance sVisible[DIORAMA_MAX_VISIBLE_CELLS];
static uint32_t sPartVisibleFirst[SLOT_COUNT];
static uint32_t sPartVisibleCount[SLOT_COUNT];
static uint32_t sPartVisibleNear[SLOT_COUNT]; /* the first of them, within TREE_SOLID_REACH */
static struct TreeInstance sFarScratch[DIORAMA_MAX_VISIBLE_CELLS];
static float sSolidReach;
static bool sVisibleSeen;
static float sVisibleCamera[4];
static uint32_t sVisibleGeneration;
static bool sVisibleValid;
static uint64_t sSequence;
static uint32_t sGeometryGeneration;
/*
 * What the instances are made of: each tree cell's place, part and ground, in
 * the snapshot's order (performance work 2026-10-11). A snapshot whose trees
 * are the last one's - nearly all of them: people walk, water moves, the trees
 * stand - uploads nothing.
 */
struct TreeKey
{
    int16_t mapX;
    int16_t mapY;
    uint8_t slot;
    uint8_t pad[3];
    float ground;
};
static struct TreeKey sKeys[DIORAMA_MAX_VISIBLE_CELLS];
static struct TreeKey sNewKeys[DIORAMA_MAX_VISIBLE_CELLS];
static unsigned sKeyCount;
static bool sKeysValid;

static const char sVertexShader[] =
    "#version 330 core\n"
    /* A model's corner in its cell (x east, y up, z south from the cell's
     * north-west corner on its ground), and per instance that corner in the
     * scene (z north). */
    "layout(location = 0) in vec3 corner;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in float faceShade;\n"
    "layout(location = 3) in vec3 cellOrigin;\n"
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
    "out vec3 sceneWorldPosition;\n"
    "out float viewDepth;\n"
    "void main() {\n"
    "  vec3 position = vec3(cellOrigin.x + corner.x, cellOrigin.y + corner.y, cellOrigin.z - corner.z);\n"
    /* The terrain's projection (gl_terrain_renderer.c), so the two meet. */
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = " DIORAMA_FAR_DEPTH_GLSL ";\n"
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
    "  if (shadowPass == 1) gl_Position = lightViewProjection * vec4(position, 1.0);\n"
    "  viewDepth = depth;\n"
    "  sceneWorldPosition = position;\n"
    "  uv = texCoord;\n"
    "  shade = faceShade;\n"
    "}\n";

static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in float shade;\n"
    "in vec3 sceneWorldPosition;\n"
    "in float viewDepth;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform int shadowPass;\n"
    "uniform int shadowEnabled;\n"
    "uniform mat4 lightViewProjection;\n"
    "uniform sampler2DShadow shadowMap;\n"
    "uniform float shadowAmbient;\n"
    /* The art's colours are baked: they follow the palette's fade and night
     * tint along the trees' palette line, as the voxel trees do (TreeTint). */
    "uniform vec3 pixelScale;\n"
    "uniform vec3 pixelOffset;\n"
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
    "  vec4 texel = texture(image, uv);\n"
    "  if (texel.a < 0.5) discard;\n" /* alpha test: no colour, no depth */
    "  if (shadowPass == 1) { color = vec4(1.0); return; }\n"
    "  vec3 c = clamp(texel.rgb * pixelScale + pixelOffset, 0.0, 1.0);\n"
    /* each face of a crown or trunk shaded by the way it looks, as the terrain's */
    "  color = vec4(Grade(c) * Light(shade * mix(shadowAmbient, 1.0, SunLit()))\n"
    "             * Dapple(sceneWorldPosition), 1.0);\n"
    "  color.rgb += Grade(c) * Lamps(sceneWorldPosition);\n"
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
        SDL_Log("Diorama tree shader compile failed: %s", log);
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
        SDL_Log("Diorama tree shader link failed: %s", log);
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
    sShadowPassLocation = dglGetUniformLocation(sProgram, "shadowPass");
    sShadowEnabledLocation = dglGetUniformLocation(sProgram, "shadowEnabled");
    sLightMatrixLocation = dglGetUniformLocation(sProgram, "lightViewProjection");
    sShadowMapLocation = dglGetUniformLocation(sProgram, "shadowMap");
    sShadowAmbientLocation = dglGetUniformLocation(sProgram, "shadowAmbient");
    sSaturationLocation = dglGetUniformLocation(sProgram, "saturation");
    sPixelScaleLocation = dglGetUniformLocation(sProgram, "pixelScale");
    sPixelOffsetLocation = dglGetUniformLocation(sProgram, "pixelOffset");
    lightLocated = DioramaGLLight_Locate(sProgram, &sLightUniforms);
    DioramaGLLamps_Locate(sProgram, &sLampUniforms);
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0 && sFocalLengthLocation >= 0
        && sAspectCorrectionLocation >= 0 && sImageLocation >= 0
        && sShadowPassLocation >= 0 && sShadowEnabledLocation >= 0
        && sLightMatrixLocation >= 0 && sShadowMapLocation >= 0
        && sShadowAmbientLocation >= 0 && sSaturationLocation >= 0
        && sPixelScaleLocation >= 0 && sPixelOffsetLocation >= 0 && lightLocated;
}

/* How bright a face looking along n (x east, y up, z south) is: the voxel
 * tree's shades (terrain_mesh.c BuildTreeModel), up and south lit, down and north
 * darkest, east and west between. */
static float Shade(float ny, float nz)
{
    float shade = 0.82f;

    shade += ny > 0.0f ? 0.18f * ny : 0.24f * ny;
    shade += nz > 0.0f ? 0.18f * nz : 0.10f * nz;
    return shade < 0.55f ? 0.55f : shade > 1.0f ? 1.0f : shade;
}

/* A quad looking out along n (x east, y up, z south), shaded by it, its corners
 * taken anticlockwise seen from outside: the colour pass leaves out the faces of
 * the solids that look away from the eye (GL_CULL_FACE). */
static void AddQuad(const float corners[4][3], const float uv[4][2], float nx, float ny, float nz)
{
    static const uint16_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint16_t reversed[6] = {0, 2, 1, 0, 3, 2};
    float ab[3];
    float ac[3];
    bool clockwise;
    unsigned i;

    if (sModelVertexCount + 4 > MAX_MODEL_VERTICES || sModelIndexCount + 6 > MAX_MODEL_INDICES)
    {
        sModelFull = true;
        return;
    }
    for (i = 0; i < 3; i++)
    {
        ab[i] = corners[1][i] - corners[0][i];
        ac[i] = corners[2][i] - corners[0][i];
    }
    clockwise = (ab[1] * ac[2] - ab[2] * ac[1]) * nx + (ab[2] * ac[0] - ab[0] * ac[2]) * ny
              + (ab[0] * ac[1] - ab[1] * ac[0]) * nz < 0.0f;
    for (i = 0; i < 4; i++)
    {
        struct ModelVertex *vertex = &sModelVertices[sModelVertexCount + i];

        vertex->x = corners[i][0];
        vertex->y = corners[i][1];
        vertex->z = corners[i][2];
        vertex->u = uv[i][0];
        vertex->v = uv[i][1];
        vertex->shade = Shade(ny, nz);
    }
    for (i = 0; i < 6; i++)
        sModelIndices[sModelIndexCount++] = (uint16_t)(sModelVertexCount + (clockwise ? reversed : order)[i]);
    sModelVertexCount += 4;
}

/* A quad of the art's texel rectangle (u0, v0)-(u1, v1), corners a (its top
 * left) b c d in the cell (the 3DS port's terms). */
static void ArtQuad(const float corners[4][3], float u0, float v0, float u1, float v1, float ny, float nz)
{
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};

    AddQuad(corners, uv, 0.0f, ny, nz);
}

/* The trunk's art over the whole cell, laid on its ground (VoxelMesh_Top). */
static void TrunkArt(int texelX, int texelY)
{
    const float corners[4][3] = {
        {0.0f, TRUNK_LIFT, 0.0f}, {1.0f, TRUNK_LIFT, 0.0f},
        {1.0f, TRUNK_LIFT, 1.0f}, {0.0f, TRUNK_LIFT, 1.0f},
    };

    ArtQuad(corners, texelX * TEXEL, texelY * TEXEL, (texelX + 16) * TEXEL, (texelY + 16) * TEXEL, 1.0f, 0.0f);
}

/*
 * EmitCell's cut-out: a large tree's quarter. The crown keeps its aspect ratio
 * (the 3DS art's 32:36: 2 tiles wide and 2.25 long), at 50 degrees (sin and
 * cos), its transparent bottom margin sunk 0.10 into the ground so the leaves
 * overlap the trunk; each quarter carries its column and row of it.
 */
static void LargeCrownCard(const struct CrownArt *art, int part)
{
    const float rise = (float)art->height / PIXELS_PER_TILE * TILT_SIN;
    const float run = (float)art->height / PIXELS_PER_TILE * TILT_COS;
    const float baseHeight = -CROWN_SINK;
    int rowPixels = art->height / 2;
    int col = part & 1;
    int row = part >> 1;
    float baseZ = art->baseZ - (float)row;
    float top = 1.0f - (float)row * 0.5f;
    float bottom = top - 0.5f;
    const float corners[4][3] = {
        {0.0f, baseHeight + top * rise, baseZ - top * run},
        {1.0f, baseHeight + top * rise, baseZ - top * run},
        {1.0f, baseHeight + bottom * rise, baseZ - bottom * run},
        {0.0f, baseHeight + bottom * rise, baseZ - bottom * run},
    };

    ArtQuad(corners, (art->x + col * 16) * TEXEL, (art->y + row * rowPixels) * TEXEL,
            (art->x + col * 16 + 16) * TEXEL, (art->y + (row + 1) * rowPixels) * TEXEL, TILT_COS, TILT_SIN);
}

/*
 * EmitSmallCell's cut-out: the small crown is 16:32, 1 tile wide and 2 long, at
 * the same 50 degrees and sunk the same way, standing on its one-cell trunk,
 * 0.15 further forward than half the large tree's (Dewford's another 0.15, so
 * it stands over the shadow drawn on its sand).
 */
static void SmallCrownCard(const struct CrownArt *art)
{
    const float rise = 1.532089f;
    const float run = 1.285575f;
    const float baseHeight = -CROWN_SINK;
    const float corners[4][3] = {
        {0.0f, baseHeight + rise, art->baseZ - run},
        {1.0f, baseHeight + rise, art->baseZ - run},
        {1.0f, baseHeight, art->baseZ},
        {0.0f, baseHeight, art->baseZ},
    };

    ArtQuad(corners, art->x * TEXEL, art->y * TEXEL, (art->x + 16) * TEXEL, (art->y + 32) * TEXEL,
            TILT_COS, TILT_SIN);
}

static bool Leaf(const struct CrownArt *art, int x, int y)
{
    if (x < 0 || y < 0 || x >= art->width || y >= art->leafRows)
        return false;
    return gDioramaTrees3dsRgba[((art->y + y) * DIORAMA_TREES_3DS_DIM + art->x + x) * 4 + 3] >= 128;
}

/* While a model is made: its part's columns and rows of the art, and which of
 * its sides face a missing half or row (the crown rounds off there: no leaf
 * past them, CrownLevels). */
static int sPartLeft;
static int sPartTop;
static int sPartRight;
static int sPartBottom;
static bool sCutLeft;
static bool sCutRight;
static bool sCutTop;
static bool sCutBottom;

static bool CrownLeaf(const struct CrownArt *art, int x, int y)
{
    if ((sCutLeft && x < sPartLeft) || (sCutRight && x >= sPartRight) || (sCutTop && y < sPartTop)
     || (sCutBottom && y >= sPartBottom))
        return false;
    return Leaf(art, x, y);
}

/* Each leaf's level (sLevels) from its distance to the outline; the most. */
static int CrownLevels(const struct CrownArt *art)
{
    int most = 0;
    int x;
    int y;

    for (y = 0; y < art->height; y++)
    {
        for (x = 0; x < art->width; x++)
        {
            int nearest = 1 << 30;
            int ex;
            int ey;
            float e;
            int level;

            if (!CrownLeaf(art, x, y))
            {
                sLevels[y][x] = NO_LEAF;
                continue;
            }
            for (ey = -1; ey <= art->height; ey++)
                for (ex = -1; ex <= art->width; ex++)
                    if (!CrownLeaf(art, ex, ey))
                    {
                        int d = (ex - x) * (ex - x) + (ey - y) * (ey - y);

                        if (d < nearest)
                            nearest = d;
                    }
            e = sqrtf((float)nearest) / art->roundness;
            if (e > 1.0f)
                e = 1.0f;
            level = (int)lroundf(art->thickness * sqrtf(1.0f - (1.0f - e) * (1.0f - e)) / CROWN_STEP);
            if (level > MAX_CROWN_LEVELS)
                level = MAX_CROWN_LEVELS;
            sLevels[y][x] = (uint8_t)level;
            if (level > most)
                most = level;
        }
    }
    return most;
}

/* Where depth plane p (0 to 2 * levels + 1) lies, in voxels out of the plane:
 * the middle voxel from -0.5 to 0.5, then CROWN_STEPs. */
static float PlaneDepth(int plane, int levels)
{
    int cell = plane - levels;

    if (cell == 0)
        return -0.5f;
    if (cell > 0)
        return 0.5f + (float)(CROWN_STEP * (cell - 1));
    return -(0.5f + (float)(CROWN_STEP * -cell));
}

/* A point of the crown: the art's column x and row y (pixels from its top
 * left), depth voxels out of its plane toward the eye; in the cell col, row of
 * the tree (from its upper left). */
static void CrownPoint(const struct CrownArt *art, int col, int row, float x, float y, float depth,
                       float out[3])
{
    float along = ((float)art->height - y) / PIXELS_PER_TILE;

    out[0] = x / PIXELS_PER_TILE - (float)col;
    out[1] = -CROWN_SINK + along * TILT_SIN + depth / PIXELS_PER_TILE * TILT_COS;
    out[2] = art->baseZ - along * TILT_COS + depth / PIXELS_PER_TILE * TILT_SIN - (float)row;
}

/* One face of the crown's voxels (axis 0 the art's columns, 1 its rows, 2 the
 * depth; on its plane, from u0 to u1 and v0 to v1 along the next two axes), in
 * the grid of the part that starts at column x0, row y0 of the art. */
static void CrownFace(const struct CrownArt *art, int x0, int y0, int col, int row, int levels,
                      int axis, int sign, int plane, int u0, int u1, int v0, int v1)
{
    int uAxis = (axis + 1) % 3;
    int vAxis = (axis + 2) % 3;
    const int us[4] = {u0, u1, u1, u0};
    const int vs[4] = {v0, v0, v1, v1};
    /* the face's first column and row of the art, where its texture starts (a little inside them) */
    int firstX = x0 + (axis == 2 ? u0 : v0);
    int firstY = y0 + (axis == 2 ? v0 : u0);
    float corners[4][3];
    float uv[4][2];
    int i;

    for (i = 0; i < 4; i++)
    {
        int at[3];
        int x;
        int y;
        float u;
        float v;

        at[axis] = plane;
        at[uAxis] = us[i];
        at[vAxis] = vs[i];
        x = x0 + at[0];
        y = y0 + at[1];
        CrownPoint(art, col, row, (float)x, (float)y, PlaneDepth(at[2], levels), corners[i]);
        /* the art across a face that looks out of the plane; down a side, the colour of the pixels behind it (its
         * column of them, or its row) */
        u = (float)x + (x == firstX ? TEXEL_INSET : -TEXEL_INSET);
        v = (float)y + (y == firstY ? TEXEL_INSET : -TEXEL_INSET);
        if (axis == 0)
            u = (float)(x0 + (sign > 0 ? plane - 1 : plane)) + 0.5f;
        else if (axis == 1)
            v = (float)(y0 + (sign > 0 ? plane - 1 : plane)) + 0.5f;
        uv[i][0] = ((float)art->x + u) * TEXEL;
        uv[i][1] = ((float)art->y + v) * TEXEL;
    }
    if (axis == 0)
        AddQuad(corners, uv, (float)sign, 0.0f, 0.0f);
    else if (axis == 1)
        AddQuad(corners, uv, 0.0f, -TILT_SIN * sign, TILT_COS * sign);
    else
        AddQuad(corners, uv, 0.0f, TILT_COS * sign, TILT_SIN * sign);
}

static bool CrownSolid(int x, int y, int depthCell, int levels)
{
    int level = sLevels[y][x];

    return level != NO_LEAF && abs(depthCell - levels) <= level;
}

/*
 * The voxels of the crown's columns x0 to x0 + width - 1 and rows y0 to
 * y0 + height - 1 as faces, each the largest rectangle of faces looking the
 * same way on one plane (compile_vox.py's greedy faces: the art is the
 * texture, so one face may cross many colours): its back, or all the rest.
 */
static void CrownFaces(const struct CrownArt *art, int x0, int y0, int width, int height, int col, int row,
                       int levels, bool back)
{
    int size[3];
    int step;

    size[0] = width;
    size[1] = height;
    size[2] = levels * 2 + 1;
    /* the faces looking out of the plane first: drawn before the sides behind them, they hide them early */
    for (step = 0; step < 3; step++)
    {
        int axis = (step + 2) % 3;
        int uAxis = (axis + 1) % 3;
        int vAxis = (axis + 2) % 3;
        int across = size[uAxis];
        int down = size[vAxis];
        int plane;

        for (plane = 0; plane <= size[axis]; plane++)
        {
            int u;
            int v;

            for (v = 0; v < down; v++)
            {
                for (u = 0; u < across; u++)
                {
                    int at[3];
                    bool before = false;
                    bool after = false;

                    at[uAxis] = u;
                    at[vAxis] = v;
                    if (plane > 0)
                    {
                        at[axis] = plane - 1;
                        before = CrownSolid(x0 + at[0], y0 + at[1], at[2], levels);
                    }
                    if (plane < size[axis])
                    {
                        at[axis] = plane;
                        after = CrownSolid(x0 + at[0], y0 + at[1], at[2], levels);
                    }
                    sMask[v * across + u] = before && !after ? 1 : after && !before ? -1 : 0;
                }
            }
            for (v = 0; v < down; v++)
            {
                u = 0;
                while (u < across)
                {
                    int8_t key = sMask[v * across + u];
                    int runAcross = 1;
                    int runDown = 1;
                    int i;
                    int j;

                    if (key == 0)
                    {
                        u++;
                        continue;
                    }
                    while (u + runAcross < across && sMask[v * across + u + runAcross] == key)
                        runAcross++;
                    for (;;)
                    {
                        bool same = v + runDown < down;

                        for (i = 0; same && i < runAcross; i++)
                            same = sMask[(v + runDown) * across + u + i] == key;
                        if (!same)
                            break;
                        runDown++;
                    }
                    for (j = 0; j < runDown; j++)
                        for (i = 0; i < runAcross; i++)
                            sMask[(v + j) * across + u + i] = 0;
                    /* the back (looking down and north) on its own, or all but it */
                    if ((axis == 2 && key < 0) == back)
                        CrownFace(art, x0, y0, col, row, levels, axis, key, plane, u, u + runAcross, v,
                                  v + runDown);
                    u += runAcross;
                }
            }
        }
    }
}

/*
 * The trunk under a crown: trunkWidth voxels square under the art's middle
 * column, its south side a voxel north of where the leaves over it end (so the
 * crown covers its top), from the ground to the crown's plane over its south
 * side; the piece of it in the tree's cell col, row (a large tree's trunk
 * stands where its four quarters meet). Its sides show a row of the art's bark.
 */
static void Trunk(const struct CrownArt *art, int col, int row)
{
    int half = art->trunkWidth / 2;
    int middle = art->width / 2;
    int foot = art->leafRows;
    float along;
    float box[2][3];
    float piece[2][3];
    float top;
    int x;
    int face;

    for (x = middle - half; x < middle + half; x++)
    {
        int y = art->leafRows - 1;

        while (y >= 0 && !Leaf(art, x, y))
            y--;
        if (y + 1 < foot)
            foot = y + 1;
    }
    along = (float)(art->height - foot) / PIXELS_PER_TILE;
    box[0][0] = (float)(middle - half) / PIXELS_PER_TILE;
    box[1][0] = (float)(middle + half) / PIXELS_PER_TILE;
    box[1][2] = art->baseZ - along * TILT_COS - 1.0f / PIXELS_PER_TILE;
    box[0][2] = box[1][2] - (float)art->trunkWidth / PIXELS_PER_TILE;
    top = -CROWN_SINK + (art->baseZ - box[1][2]) / TILT_COS * TILT_SIN + 0.02f;
    box[0][1] = 0.0f;
    box[1][1] = top;
    piece[0][0] = box[0][0] > (float)col ? box[0][0] : (float)col;
    piece[1][0] = box[1][0] < (float)(col + 1) ? box[1][0] : (float)(col + 1);
    piece[0][2] = box[0][2] > (float)row ? box[0][2] : (float)row;
    piece[1][2] = box[1][2] < (float)(row + 1) ? box[1][2] : (float)(row + 1);
    piece[0][1] = 0.0f;
    piece[1][1] = top;
    if (piece[0][0] >= piece[1][0] || piece[0][2] >= piece[1][2])
        return;
    /* the four sides (south, north, east, west) and the top */
    for (face = 0; face < 5; face++)
    {
        float corners[4][3];
        float uv[4][2];
        float a;
        float b;
        float barkV = ((float)art->barkY + 0.5f) * TEXEL;
        int i;

        if (face < 2)
        {
            float z = piece[face == 0][2];

            a = piece[0][0];
            b = piece[1][0];
            corners[0][0] = a, corners[0][1] = top, corners[0][2] = z;
            corners[1][0] = b, corners[1][1] = top, corners[1][2] = z;
            corners[2][0] = b, corners[2][1] = 0.0f, corners[2][2] = z;
            corners[3][0] = a, corners[3][1] = 0.0f, corners[3][2] = z;
            a = (a - box[0][0]) * PIXELS_PER_TILE;
            b = (b - box[0][0]) * PIXELS_PER_TILE;
        }
        else if (face < 4)
        {
            float edge = piece[face == 2][0];

            a = piece[0][2];
            b = piece[1][2];
            corners[0][0] = edge, corners[0][1] = top, corners[0][2] = a;
            corners[1][0] = edge, corners[1][1] = top, corners[1][2] = b;
            corners[2][0] = edge, corners[2][1] = 0.0f, corners[2][2] = b;
            corners[3][0] = edge, corners[3][1] = 0.0f, corners[3][2] = a;
            a = (a - box[0][2]) * PIXELS_PER_TILE;
            b = (b - box[0][2]) * PIXELS_PER_TILE;
        }
        else
        {
            corners[0][0] = piece[0][0], corners[0][1] = top, corners[0][2] = piece[0][2];
            corners[1][0] = piece[1][0], corners[1][1] = top, corners[1][2] = piece[0][2];
            corners[2][0] = piece[1][0], corners[2][1] = top, corners[2][2] = piece[1][2];
            corners[3][0] = piece[0][0], corners[3][1] = top, corners[3][2] = piece[1][2];
            a = b = (float)half;
        }
        for (i = 0; i < 4; i++)
        {
            corners[i][0] -= (float)col;
            corners[i][2] -= (float)row;
        }
        uv[0][0] = uv[3][0] = ((float)art->barkX + a + TEXEL_INSET) * TEXEL;
        uv[1][0] = uv[2][0] = ((float)art->barkX + b - TEXEL_INSET) * TEXEL;
        uv[0][1] = uv[1][1] = uv[2][1] = uv[3][1] = barkV;
        /* south, north, east, west, up */
        AddQuad(corners, uv, face == 2 ? 1.0f : face == 3 ? -1.0f : 0.0f, face == 4 ? 1.0f : 0.0f,
                face == 0 ? 1.0f : face == 1 ? -1.0f : 0.0f);
    }
}

/* The sprite of the quarter 0 of the large tree a part is a quarter of
 * (DIORAMA_TREE_SPRITE_NONE: a small tree). */
static int QuarterFirst(int part)
{
    static const uint8_t firsts[] = {
        DIORAMA_TREE_SPRITE_QUARTER, DIORAMA_TREE_SPRITE_ASH_QUARTER, DIORAMA_TREE_SPRITE_LAVARIDGE_QUARTER,
        DIORAMA_TREE_SPRITE_MOSSDEEP_QUARTER,
    };
    int sprite = part + 1;
    size_t i;

    for (i = 0; i < sizeof(firsts); i++)
        if (sprite >= firsts[i] && sprite < firsts[i] + 4)
            return firsts[i];
    return DIORAMA_TREE_SPRITE_NONE;
}

/* The models of the parts (PartModel's order), and their bounds. */
static bool BuildModels(void)
{
    const char *cards = SDL_getenv("PKMN_DIORAMA_TREE_CARDS");
    const char *reach = SDL_getenv("PKMN_DIORAMA_TREE_REACH");
    int slot;

    sVolumes = !(cards != NULL && cards[0] == '1');
    sSolidReach = reach != NULL && reach[0] != '\0' ? (float)SDL_atof(reach) : TREE_SOLID_REACH;
    sModelVertexCount = sModelIndexCount = 0;
    sModelFull = false;
    for (slot = 0; slot < SLOT_COUNT; slot++)
    {
        int part = slot / KINDS;
        int kind = slot % KINDS;
        int first = QuarterFirst(part);
        bool large = first != DIORAMA_TREE_SPRITE_NONE;
        const struct CrownArt *art = &sCrownArt[first == DIORAMA_TREE_SPRITE_ASH_QUARTER ? 3
                                                : first == DIORAMA_TREE_SPRITE_MOSSDEEP_QUARTER ? 4 : large ? 0
                                                : part + 1 == DIORAMA_TREE_SPRITE_SMALL_DEWFORD ? 2 : 1];
        int quarter = large ? part + 1 - first : 0;
        int col = large ? (quarter & 1) : 0;
        int row = large ? (quarter >> 1) : 0;
        int columns = art->width / art->columns;
        int rows = art->height / art->rows;
        int levels;
        struct PartModel *model = &sParts[slot];
        uint32_t firstVertex = sModelVertexCount;
        uint32_t artFirst;
        uint32_t artCount;
        uint32_t i;
        int axis;

        if (kind != 0 && (!large || !sVolumes))
        {
            /* one kind of small tree, of cut-out */
            sParts[slot] = sParts[slot - kind];
            continue;
        }
        sPartLeft = col * columns;
        sPartRight = sPartLeft + columns;
        sPartTop = row * rows;
        sPartBottom = sPartTop + rows;
        sCutLeft = (kind & HALF_ALONE) && col == 1;
        sCutRight = (kind & HALF_ALONE) && col == 0;
        sCutTop = (kind & ROW_ALONE) && row == 1;
        sCutBottom = (kind & ROW_ALONE) && row == 0;
        levels = sVolumes ? CrownLevels(art) : 0;

        model->shadowFirst = sModelIndexCount;
        if (large)
            LargeCrownCard(art, quarter);
        else
            SmallCrownCard(art);
        model->first = sVolumes ? sModelIndexCount : model->shadowFirst;
        if (sVolumes)
            Trunk(art, col, row);
        model->shadowCount = sModelIndexCount - model->shadowFirst;
        artFirst = sModelIndexCount;
        /* the trunk's art on the ground (an ash or a Lavaridge tree's cell shows its own ground: the 3DS art is
         * grass) */
        if (first == DIORAMA_TREE_SPRITE_QUARTER)
            TrunkArt(32 + col * 16, row * 16);
        else if (part + 1 == DIORAMA_TREE_SPRITE_SMALL)
            TrunkArt(48, 32);
        else if (part + 1 == DIORAMA_TREE_SPRITE_SMALL_DEWFORD)
            TrunkArt(80, 0);
        artCount = sModelIndexCount - artFirst;
        if (sVolumes)
            CrownFaces(art, col * columns, row * rows, columns, rows, col, row, levels, false);
        model->seenCount = sModelIndexCount - model->first;
        if (sVolumes)
            CrownFaces(art, col * columns, row * rows, columns, rows, col, row, levels, true);
        model->count = sModelIndexCount - model->first;
        /* far away, the cut-out with its trunk and the trunk's art on the ground: the same indices again, after the
         * model (the cut-outs: the model itself) */
        model->farFirst = sModelIndexCount;
        if (sVolumes && sModelIndexCount + model->shadowCount + artCount <= MAX_MODEL_INDICES)
        {
            for (i = 0; i < model->shadowCount; i++)
                sModelIndices[sModelIndexCount++] = sModelIndices[model->shadowFirst + i];
            for (i = 0; i < artCount; i++)
                sModelIndices[sModelIndexCount++] = sModelIndices[artFirst + i];
        }
        else if (sVolumes)
            sModelFull = true;
        model->farCount = sVolumes ? sModelIndexCount - model->farFirst : model->count;
        if (!sVolumes)
            model->farFirst = model->first;
        for (axis = 0; axis < 3; axis++)
        {
            model->minimum[axis] = 1.0e9f;
            model->maximum[axis] = -1.0e9f;
        }
        for (i = firstVertex; i < sModelVertexCount; i++)
        {
            const float at[3] = {sModelVertices[i].x, sModelVertices[i].y, sModelVertices[i].z};

            for (axis = 0; axis < 3; axis++)
            {
                if (at[axis] < model->minimum[axis])
                    model->minimum[axis] = at[axis];
                if (at[axis] > model->maximum[axis])
                    model->maximum[axis] = at[axis];
            }
        }
    }
    if (sModelFull)
    {
        SDL_Log("Diorama: the trees' models do not fit (%u vertices)", (unsigned)MAX_MODEL_VERTICES);
        return false;
    }
    SDL_Log("Diorama: trees as %s, %u vertices, %u indices (a large tree's quarters %u %u %u %u, the small trees %u %u;"
            " seen from above and south %u %u %u %u, %u %u)",
            sVolumes ? "solid crowns and trunks" : "cut-outs", (unsigned)sModelVertexCount,
            (unsigned)sModelIndexCount, (unsigned)sParts[0].count, (unsigned)sParts[KINDS].count,
            (unsigned)sParts[2 * KINDS].count, (unsigned)sParts[3 * KINDS].count, (unsigned)sParts[4 * KINDS].count,
            (unsigned)sParts[5 * KINDS].count, (unsigned)sParts[0].seenCount, (unsigned)sParts[KINDS].seenCount,
            (unsigned)sParts[2 * KINDS].seenCount, (unsigned)sParts[3 * KINDS].seenCount,
            (unsigned)sParts[4 * KINDS].seenCount, (unsigned)sParts[5 * KINDS].seenCount);
    return true;
}

bool DioramaGLTreeSprites_Init(void)
{
    if (sReady)
        return true;
    /* Made whatever the tree style: it can be switched while the game runs. */
    if (!CreateProgram() || !BuildModels())
        return false;
    glGenTextures(1, &sTexture);
    glBindTexture(GL_TEXTURE_2D, sTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DIORAMA_TREES_3DS_DIM, DIORAMA_TREES_3DS_DIM, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, gDioramaTrees3dsRgba);
    /* Pixel art, as the 3DS draws it: no blending between texels. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    dglGenVertexArrays(1, &sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglGenBuffers(1, &sIndexBuffer);
    dglGenBuffers(1, &sInstanceBuffer);
    dglGenBuffers(1, &sVisibleBuffer);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sModelVertexCount * sizeof(struct ModelVertex), sModelVertices,
                  GL_STATIC_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct ModelVertex),
                           (const void *)offsetof(struct ModelVertex, x));
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct ModelVertex),
                           (const void *)offsetof(struct ModelVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(struct ModelVertex),
                           (const void *)offsetof(struct ModelVertex, shade));
    /* the element buffer is the vertex array's */
    dglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIndexBuffer);
    dglBufferData(GL_ELEMENT_ARRAY_BUFFER, sModelIndexCount * sizeof(uint16_t), sModelIndices, GL_STATIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
    dglEnableVertexAttribArray(3);
    dglVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(struct TreeInstance), NULL);
    dglVertexAttribDivisor(3, 1);
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    sReady = true;
    return true;
}

bool DioramaGLTreeSprites_Volumes(void)
{
    return sReady && sVolumes;
}

/* Whether the tree cell at (mapX, mapY) holds the sprite given (a quarter's
 * partner); a cell past the snapshot's grid counts as holding it, so a tree at
 * the scene's edge does not change as the grid moves. */
static bool CellHolds(const struct DioramaSceneSnapshot *snapshot, int mapX, int mapY, uint8_t sprite)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index = gridY * DIORAMA_GRID_WIDTH + gridX;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT
     || index >= (int)snapshot->visibleCellCount)
        return true;
    return snapshot->cells[index].treeSprite == sprite;
}

/* A tree cell's model slot: its part, and for a large tree's quarter whether
 * the other half of its row and the other row of its column are there. */
static int CellSlot(const struct DioramaSceneSnapshot *snapshot, const struct DioramaCellSnapshot *cell)
{
    int part = cell->treeSprite - 1;
    int first = QuarterFirst(part);
    int quarter;
    int col;
    int row;
    int kind = 0;

    if (first == DIORAMA_TREE_SPRITE_NONE)
        return part * KINDS;
    quarter = cell->treeSprite - first;
    col = quarter & 1;
    row = quarter >> 1;
    if (!CellHolds(snapshot, cell->mapX + (col == 0 ? 1 : -1), cell->mapY,
                   (uint8_t)(first + (quarter ^ 1))))
        kind |= HALF_ALONE;
    if (!CellHolds(snapshot, cell->mapX, cell->mapY + (row == 0 ? 1 : -1), (uint8_t)(first + (quarter ^ 2))))
        kind |= ROW_ALONE;
    return part * KINDS + kind;
}

void DioramaGLTreeSprites_Sync(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaResolvedCell *resolvedCells)
{
    uint32_t filled[SLOT_COUNT];
    unsigned count = 0;
    unsigned i;
    int slot;

    if (!sReady || snapshot == NULL || snapshot->sequence == sSequence)
        return;
    sSequence = snapshot->sequence;
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        struct TreeKey *key;

        if (cell->treeSprite < DIORAMA_TREE_SPRITE_QUARTER || cell->treeSprite > PART_COUNT)
            continue;
        key = &sNewKeys[count++];
        memset(key, 0, sizeof(*key));
        key->mapX = cell->mapX;
        key->mapY = cell->mapY;
        key->slot = (uint8_t)CellSlot(snapshot, cell);
        key->ground = resolvedCells[i].topHeight;
    }
    if (sKeysValid && count == sKeyCount
     && memcmp(sNewKeys, sKeys, count * sizeof(struct TreeKey)) == 0)
        return;
    memcpy(sKeys, sNewKeys, count * sizeof(struct TreeKey));
    sKeyCount = count;
    sKeysValid = true;
    /* each slot's instances together, in the snapshot's order */
    memset(sPartInstanceCount, 0, sizeof(sPartInstanceCount));
    for (i = 0; i < count; i++)
        sPartInstanceCount[sKeys[i].slot]++;
    for (slot = 0, sInstanceCount = 0; slot < SLOT_COUNT; slot++)
    {
        sPartInstanceFirst[slot] = sInstanceCount;
        filled[slot] = 0;
        sInstanceCount += sPartInstanceCount[slot];
    }
    for (i = 0; i < count; i++)
    {
        const struct TreeKey *key = &sKeys[i];
        struct TreeInstance *instance = &sInstances[sPartInstanceFirst[key->slot] + filled[key->slot]++];

        instance->x = (float)key->mapX - 0.5f;
        instance->y = key->ground;
        instance->z = -((float)key->mapY - 0.5f);
    }
    /* Other trees, so other crowns: the sun's depth map is drawn again. */
    sGeometryGeneration++;
    dglBindBuffer(GL_ARRAY_BUFFER, sInstanceBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sInstanceCount * sizeof(struct TreeInstance),
                  sInstanceCount != 0 ? sInstances : NULL, GL_DYNAMIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* Each part's instances drawn with its model: those on screen in the colour
 * pass (its seen faces when sVisibleSeen), every one in the sun's depth map. */
static void DrawParts(bool shadow)
{
    int part;

    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, shadow ? sInstanceBuffer : sVisibleBuffer);
    for (part = 0; part < SLOT_COUNT; part++)
    {
        const struct PartModel *model = &sParts[part];
        int distant; /* not 'far': Windows' headers define far (and near) as nothing */

        /* in the colour pass the near trees' solids, then the far ones' cut-outs (ChooseVisible) */
        for (distant = 0; distant < (shadow ? 1 : 2); distant++)
        {
            uint32_t first = shadow ? model->shadowFirst : distant ? model->farFirst : model->first;
            uint32_t indexCount = shadow ? model->shadowCount : distant ? model->farCount
                                : sVisibleSeen ? model->seenCount : model->count;
            uint32_t instanceFirst = shadow ? sPartInstanceFirst[part]
                                   : sPartVisibleFirst[part] + (distant ? sPartVisibleNear[part] : 0);
            uint32_t instanceCount = shadow ? sPartInstanceCount[part]
                                   : distant ? sPartVisibleCount[part] - sPartVisibleNear[part] : sPartVisibleNear[part];

            if (instanceCount == 0 || indexCount == 0)
                continue;
            dglVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(struct TreeInstance),
                                   (const void *)(instanceFirst * sizeof(struct TreeInstance)));
            dglDrawElementsInstanced(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_SHORT,
                                     (const void *)(first * sizeof(uint16_t)), (GLsizei)instanceCount);
        }
    }
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    dglBindVertexArray(0);
}

/*
 * The instances on screen, each part's together (the terrain's test of a
 * chunk's bounds, DioramaTerrain_IsBoundsVisible), and whether the eye is
 * above and south of every one of them, so their backs need not be drawn: each
 * back face looks down and north, away from such an eye, behind the crown's
 * own front (gl_terrain_renderer.c TreeSeenFromAboveSouth, the voxel trees'
 * test). Chosen again only when the camera or the trees change.
 */
static void ChooseVisible(float cameraX, float cameraZ, float cameraPitch, float focalLength)
{
    const float camera[4] = {cameraX, cameraZ, cameraPitch, focalLength};
    struct DioramaTerrainView view;
    uint32_t count = 0;
    int part;

    if (sVisibleValid && sVisibleGeneration == sGeometryGeneration
     && memcmp(camera, sVisibleCamera, sizeof(camera)) == 0)
        return;
    memcpy(sVisibleCamera, camera, sizeof(camera));
    sVisibleGeneration = sGeometryGeneration;
    sVisibleValid = true;
    sVisibleSeen = true;
    DioramaTerrain_PrepareView(&view, cameraX, cameraZ, cameraPitch, focalLength);
    for (part = 0; part < SLOT_COUNT; part++)
    {
        const struct PartModel *model = &sParts[part];
        uint32_t farCount = 0;
        uint32_t i;

        sPartVisibleFirst[part] = count;
        /* the nearest first (the snapshot's cells run from the north): the trees behind are then hidden early */
        for (i = sPartInstanceCount[part]; i-- > 0;)
        {
            const struct TreeInstance *instance = &sInstances[sPartInstanceFirst[part] + i];
            struct DioramaTerrainBounds bounds;
            int corner;

            bounds.minX = instance->x + model->minimum[0];
            bounds.maxX = instance->x + model->maximum[0];
            bounds.minY = instance->y + model->minimum[1];
            bounds.maxY = instance->y + model->maximum[1];
            bounds.minZ = instance->z - model->maximum[2];
            bounds.maxZ = instance->z - model->minimum[2];
            if (!DioramaTerrain_IsBoundsInView(&view, &bounds))
                continue;
            /* past the reach from where the camera looks: the cut-out (its back faces need no test) */
            if (sSolidReach > 0.0f
             && (instance->x + 0.5f - cameraX) * (instance->x + 0.5f - cameraX)
                + (instance->z - 0.5f - cameraZ) * (instance->z - 0.5f - cameraZ) > sSolidReach * sSolidReach)
            {
                sFarScratch[farCount++] = *instance;
                continue;
            }
            sVisible[count++] = *instance;
            if (!(cameraZ - view.cameraDistance < bounds.minZ && view.cameraHeight > bounds.maxY))
                sVisibleSeen = false;
            for (corner = 0; corner < 4 && sVisibleSeen; corner++)
            {
                float y = (corner & 1) ? bounds.maxY : bounds.minY;
                float z = (corner & 2) ? bounds.maxZ : bounds.minZ;

                /* the whole crown past the near plane (0.1), so none of its front is cut away */
                if (!((view.cameraHeight - y) * view.pitchSin + (z - cameraZ + view.cameraDistance) * view.pitchCos
                      > 0.2f))
                    sVisibleSeen = false;
            }
        }
        sPartVisibleNear[part] = count - sPartVisibleFirst[part];
        memcpy(&sVisible[count], sFarScratch, farCount * sizeof(*sFarScratch));
        count += farCount;
        sPartVisibleCount[part] = count - sPartVisibleFirst[part];
    }
    dglBindBuffer(GL_ARRAY_BUFFER, sVisibleBuffer);
    dglBufferData(GL_ARRAY_BUFFER, count * sizeof(struct TreeInstance), count != 0 ? sVisible : NULL,
                  GL_DYNAMIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DioramaGLTreeSprites_Draw(float cameraX, float cameraZ, float cameraPitch,
                               float focalLength, float aspectCorrection)
{
    float scale[3];
    float offset[3];

    if (!sReady || sInstanceCount == 0)
        return;
    ChooseVisible(cameraX, cameraZ, cameraPitch, focalLength);
    DioramaGLTerrain_GetPaletteLine(scale, offset);
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
    dglUniform1i(sShadowPassLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniform1i(sShadowEnabledLocation, DioramaGLShadow_BindForLighting());
    dglUniform1f(sShadowAmbientLocation, DioramaGLShadow_Ambient());
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglUniform1f(sSaturationLocation, DioramaGLGrade_Saturation());
    dglUniform3f(sPixelScaleLocation, scale[0], scale[1], scale[2]);
    dglUniform3f(sPixelOffsetLocation, offset[0], offset[1], offset[2]);
    DioramaGLLight_Apply(&sLightUniforms, cameraPitch);
    DioramaGLLamps_Apply(&sLampUniforms);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sTexture);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    /* the solids' faces looking away left out; a cut-out is seen from both sides */
    if (sVolumes)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }
    else
        glDisable(GL_CULL_FACE);
    DrawParts(false);
    glDisable(GL_CULL_FACE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_DEPTH_TEST);
}

void DioramaGLTreeSprites_DrawShadow(void)
{
    if (!sReady || sInstanceCount == 0)
        return;
    dglUseProgram(sProgram);
    dglUniform1i(sImageLocation, 0);
    dglUniform1i(sShadowPassLocation, 1);
    dglUniform1i(sShadowEnabledLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sTexture);
    glDisable(GL_CULL_FACE);
    DrawParts(true);
    dglUniform1i(sShadowPassLocation, 0);
}

uint32_t DioramaGLTreeSprites_GetGeometryGeneration(void)
{
    return sGeometryGeneration;
}

void DioramaGLTreeSprites_Shutdown(void)
{
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sIndexBuffer != 0)
        dglDeleteBuffers(1, &sIndexBuffer);
    if (sInstanceBuffer != 0)
        dglDeleteBuffers(1, &sInstanceBuffer);
    if (sVisibleBuffer != 0)
        dglDeleteBuffers(1, &sVisibleBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sTexture != 0)
        glDeleteTextures(1, &sTexture);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sVertexBuffer = sIndexBuffer = sInstanceBuffer = sVisibleBuffer = sVertexArray = sTexture = sProgram = 0;
    sReady = false;
    sInstanceCount = 0;
    memset(sPartInstanceCount, 0, sizeof(sPartInstanceCount));
    memset(sPartVisibleCount, 0, sizeof(sPartVisibleCount));
    memset(sPartVisibleNear, 0, sizeof(sPartVisibleNear));
    sVisibleValid = false;
    sSequence = 0;
    sKeysValid = false;
    sKeyCount = 0;
}

#endif // ENABLE_DIORAMA
