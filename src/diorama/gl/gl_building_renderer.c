#ifdef ENABLE_DIORAMA
/*
 * The modelled buildings pass (see diorama/gl_building_renderer.h).
 *
 * After the terrain: every map instance on screen (a layout and where its
 * cell (0, 0) is in the scene) places its models, each made of the model's
 * triangles and its placement's ground patches, as the 3DS port's
 * VoxelBuildings_EmitSome does (ZallaxDev/pokeemerald-3Ds-dualscreen
 * 3ds_port/src/voxel/voxel_building.c, MIT). Pages hold palette slots; the
 * fragment shader looks them up in the live BG palette, so a fade or the
 * day/night tint changes one 256-colour texture, not the pages.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/buildings.h"
#include "diorama/gl_building_renderer.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_shadow.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"

#define MAX_INSTANCES 16

struct GLBuildingVertex
{
    float x, y, z;
    float u, v;      /* pixels of the page */
    float shade;
};

struct Instance
{
    uint16_t layout;
    int16_t originX;
    int16_t originY;
};

/* What the vertex buffer was built for: the maps on screen and the grid the
 * ground heights were read from (a building comes on screen as it scrolls). */
static int16_t sGridOriginX;
static int16_t sGridOriginY;

struct Batch
{
    uint16_t page;
    uint32_t first;
    uint32_t count;
};

static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLuint sPaletteTexture;
static GLuint *sPageTextures;
static unsigned sPageTextureCount;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
static GLint sPageLocation;
static GLint sPaletteLocation;
static GLint sShadowPassLocation;
static GLint sShadowEnabledLocation;
static GLint sLightMatrixLocation;
static GLint sShadowMapLocation;
static GLint sShadowAmbientLocation;
static struct Instance sInstances[MAX_INSTANCES];
static unsigned sInstanceCount;
static struct Batch *sBatches;
static unsigned sBatchCount;
static struct GLBuildingVertex *sVertices;
static uint32_t sVertexCount;
static uint32_t sVertexCapacity;
/* Bumped when the triangles change, not when the same ones are rebuilt:
 * the sun's depth map is kept until then (gl_shadow.h). */
static uint32_t sGeometryGeneration;
static uint32_t sGeometryHash;
static uint32_t sPaletteGeneration;
static bool sHasPalette;
static struct DioramaBuildingMetrics sMetrics;

static const char sVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "layout(location = 2) in float vertexShade;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "uniform int shadowPass;\n"
    "uniform mat4 lightViewProjection;\n"
    "out vec2 pagePixel;\n"
    "out float shade;\n"
    "out vec3 sceneWorldPosition;\n"
    "void main() {\n"
    /* The terrain's projection (gl_terrain_renderer.c), so the two meet. */
    "  vec3 worldPosition = position;\n"
    "  const float cameraHeight = 16.0;\n"
    "  const float cameraTargetVertical = -0.458944;\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = 80.0;\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float cameraDistance = (cameraHeight * pitchCos + cameraTargetVertical) / pitchSin;\n"
    "  float relativeX = worldPosition.x - cameraPosition.x;\n"
    "  float relativeZ = worldPosition.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - worldPosition.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - worldPosition.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0 * aspectCorrection;\n"
    "  float clipY = -0.04 * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "  if (shadowPass == 1) gl_Position = lightViewProjection * vec4(worldPosition, 1.0);\n"
    "  sceneWorldPosition = worldPosition;\n"
    "  pagePixel = texCoord;\n"
    "  shade = vertexShade;\n"
    "}\n";

static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 pagePixel;\n"
    "in float shade;\n"
    "in vec3 sceneWorldPosition;\n"
    "out vec4 color;\n"
    "uniform sampler2D page;\n"
    "uniform sampler2D palette;\n"
    "uniform int shadowPass;\n"
    "uniform int shadowEnabled;\n"
    "uniform mat4 lightViewProjection;\n"
    "uniform sampler2DShadow shadowMap;\n"
    "uniform float shadowAmbient;\n"
    "float SunLight() {\n"
    "  if (shadowEnabled == 0) return 1.0;\n"
    "  vec4 lightPosition = lightViewProjection * vec4(sceneWorldPosition, 1.0);\n"
    "  vec3 p = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;\n"
    "  if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
    "  float lit = texture(shadowMap, vec3(p.xy, p.z - 0.0015));\n"
    "  return mix(shadowAmbient, 1.0, lit);\n"
    "}\n"
    "void main() {\n"
    "  ivec2 size = textureSize(page, 0);\n"
    "  ivec2 texel = clamp(ivec2(floor(pagePixel)), ivec2(0), size - ivec2(1));\n"
    "  int slot = int(texelFetch(page, texel, 0).r * 255.0 + 0.5);\n"
    "  if (slot == 255) discard;\n"
    "  if (shadowPass == 1) { color = vec4(1.0); return; }\n"
    "  color = vec4(texelFetch(palette, ivec2(slot, 0), 0).rgb * shade * SunLight(), 1.0);\n"
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
        SDL_Log("Diorama building shader compile failed: %s", log);
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
        SDL_Log("Diorama building shader link failed: %s", log);
        return false;
    }
    sCameraLocation = dglGetUniformLocation(sProgram, "cameraPosition");
    sCameraPitchLocation = dglGetUniformLocation(sProgram, "cameraPitch");
    sFocalLengthLocation = dglGetUniformLocation(sProgram, "focalLength");
    sAspectCorrectionLocation = dglGetUniformLocation(sProgram, "aspectCorrection");
    sPageLocation = dglGetUniformLocation(sProgram, "page");
    sPaletteLocation = dglGetUniformLocation(sProgram, "palette");
    sShadowPassLocation = dglGetUniformLocation(sProgram, "shadowPass");
    sShadowEnabledLocation = dglGetUniformLocation(sProgram, "shadowEnabled");
    sLightMatrixLocation = dglGetUniformLocation(sProgram, "lightViewProjection");
    sShadowMapLocation = dglGetUniformLocation(sProgram, "shadowMap");
    sShadowAmbientLocation = dglGetUniformLocation(sProgram, "shadowAmbient");
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0 && sFocalLengthLocation >= 0
        && sAspectCorrectionLocation >= 0 && sPageLocation >= 0 && sPaletteLocation >= 0
        && sShadowPassLocation >= 0 && sShadowEnabledLocation >= 0
        && sLightMatrixLocation >= 0 && sShadowMapLocation >= 0
        && sShadowAmbientLocation >= 0;
}

bool DioramaGLBuildings_Init(void)
{
    sReady = false;
    memset(&sMetrics, 0, sizeof(sMetrics));
    if (!DioramaBuildings_Init())
        return false;
    if (!CreateProgram())
        return false;
    sPageTextureCount = DioramaBuildings_PageCount();
    sPageTextures = calloc(sPageTextureCount + 1, sizeof(*sPageTextures));
    sBatches = calloc(sPageTextureCount + 1, sizeof(*sBatches));
    if (sPageTextures == NULL || sBatches == NULL)
        return false;
    dglGenVertexArrays(1, &sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct GLBuildingVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct GLBuildingVertex),
                           (void *)offsetof(struct GLBuildingVertex, u));
    dglEnableVertexAttribArray(2);
    dglVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(struct GLBuildingVertex),
                           (void *)offsetof(struct GLBuildingVertex, shade));
    glGenTextures(1, &sPaletteTexture);
    glBindTexture(GL_TEXTURE_2D, sPaletteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    sInstanceCount = 0;
    sBatchCount = 0;
    sVertexCount = 0;
    sHasPalette = false;
    sReady = true;
    return true;
}

static GLuint PageTexture(unsigned page)
{
    unsigned width;
    unsigned height;
    const uint8_t *slots;

    if (page >= sPageTextureCount)
        return 0;
    if (sPageTextures[page] != 0)
        return sPageTextures[page];
    if (!DioramaBuildings_Page(page, &width, &height, &slots))
        return 0;
    glGenTextures(1, &sPageTextures[page]);
    glBindTexture(GL_TEXTURE_2D, sPageTextures[page]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, slots);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return sPageTextures[page];
}

static bool Reserve(uint32_t extra)
{
    uint32_t capacity = sVertexCapacity;
    struct GLBuildingVertex *grown;

    if (sVertexCount + extra <= sVertexCapacity)
        return true;
    if (capacity == 0)
        capacity = 4096;
    while (capacity < sVertexCount + extra)
        capacity *= 2;
    grown = realloc(sVertices, capacity * sizeof(*grown));
    if (grown == NULL)
        return false;
    sVertices = grown;
    sVertexCapacity = capacity;
    return true;
}

/* The scene height of the ground under a world cell (map coordinates), 0 when
 * the cell is not on screen. */
static float GroundHeightAt(const struct DioramaSceneSnapshot *snapshot,
                            const struct DioramaResolvedCell *resolvedCells, int mapX, int mapY)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (resolvedCells == NULL || gridX < 0 || gridY < 0
     || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return 0.0f;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount)
        return 0.0f;
    return resolvedCells[index].groundHeight;
}

/*
 * Whether any cell of a placement is in the scene as its map's: a model is
 * drawn with the ground it stands on, not out past the edge of the scene (a
 * rock of the next route's sea, floating in the dark).
 */
static bool PlacementInScene(const struct DioramaSceneSnapshot *snapshot,
                             const struct Instance *instance,
                             const struct DioramaBuildingPlacementInfo *info)
{
    int x;
    int y;

    for (y = 0; y < info->height; y++)
        for (x = 0; x < info->width; x++)
        {
            int mapX = instance->originX + info->x + x;
            int mapY = instance->originY + info->y + y;
            int gridX = mapX - snapshot->gridOriginX;
            int gridY = mapY - snapshot->gridOriginY;
            const struct DioramaCellSnapshot *cell;
            int index;

            if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
                continue;
            index = gridY * DIORAMA_GRID_WIDTH + gridX;
            if (index >= snapshot->visibleCellCount)
                continue;
            cell = &snapshot->cells[index];
            if ((cell->flags & DIORAMA_CELL_SOURCE_VALID) && cell->mapX == mapX && cell->mapY == mapY
             && cell->sourceLayoutId == instance->layout
             && cell->mapX - cell->sourceMapX == instance->originX
             && cell->mapY - cell->sourceMapY == instance->originY)
                return true;
        }
    return false;
}

static bool AppendPlacement(const struct DioramaSceneSnapshot *snapshot,
                            const struct DioramaResolvedCell *resolvedCells,
                            const struct Instance *instance, unsigned placement, uint16_t page)
{
    struct DioramaBuildingPlacementInfo info;
    const struct DioramaBuildingVertex *vertices = DioramaBuildings_Vertices();
    float worldX;
    float worldZ;
    float lift;
    uint32_t i;

    if (!DioramaBuildings_GetPlacement(placement, &info) || info.page != page
     || !PlacementInScene(snapshot, instance, &info))
        return true;
    /* The 3DS models count from the top-left corner of their top-left cell,
     * x east, z south. The Diorama's scene puts cell (x, y) centred on
     * (x, -y): east is +x, south is -z. */
    worldX = (float)(instance->originX + info.x) - 0.5f;
    worldZ = (float)(instance->originY + info.y) - 0.5f;
    /* A building on raised ground stands on it: the height of the cell under
     * its door, the bottom-left of its rectangle (the 3DS port's relief lift). */
    lift = GroundHeightAt(snapshot, resolvedCells, instance->originX + info.x,
                          instance->originY + info.y + info.height - 1);
    if (!Reserve(info.vertexCount + info.groundVertexCount))
        return false;
    for (i = 0; i < info.vertexCount; i++)
    {
        const struct DioramaBuildingVertex *v = &vertices[info.firstVertex + i];
        struct GLBuildingVertex *out = &sVertices[sVertexCount++];

        out->x = v->x + worldX;
        out->y = v->y + lift;
        out->z = -(v->z + worldZ);
        out->u = v->u + info.pageX;
        out->v = v->v + info.pageY;
        out->shade = v->shade;
    }
    /* The ground patches: what the map paints in the placement's cells
     * besides the building, laid flat over the ground (uv already in page
     * pixels), written a, b, c, a, c, d. */
    for (i = 0; i < info.groundVertexCount; i++)
    {
        const struct DioramaBuildingVertex *v = &vertices[info.firstGroundVertex + i];
        struct GLBuildingVertex *out = &sVertices[sVertexCount++];

        out->x = v->x + worldX;
        out->y = v->y + lift;
        out->z = -(v->z + worldZ);
        out->u = v->u;
        out->v = v->v;
        out->shade = v->shade;
    }
    sMetrics.placements++;
    return true;
}

static bool CollectInstances(const struct DioramaSceneSnapshot *snapshot,
                             struct Instance *instances, unsigned *count)
{
    unsigned n = 0;
    int i;

    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        struct Instance instance;
        unsigned k;
        unsigned first;

        if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID))
            continue;
        instance.layout = cell->sourceLayoutId;
        instance.originX = (int16_t)(cell->mapX - cell->sourceMapX);
        instance.originY = (int16_t)(cell->mapY - cell->sourceMapY);
        for (k = 0; k < n; k++)
            if (instances[k].layout == instance.layout && instances[k].originX == instance.originX
             && instances[k].originY == instance.originY)
                break;
        if (k < n || DioramaBuildings_LayoutPlacements(instance.layout, &first) == 0)
            continue;
        if (n == MAX_INSTANCES)
            return false;
        instances[n++] = instance;
    }
    *count = n;
    return true;
}

void DioramaGLBuildings_Sync(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaResolvedCell *resolvedCells)
{
    struct Instance instances[MAX_INSTANCES];
    unsigned count = 0;
    unsigned page;
    unsigned i;

    if (!sReady || snapshot == NULL)
        return;
    if (!sHasPalette || snapshot->paletteGeneration != sPaletteGeneration)
    {
        uint8_t rgba[256 * 4];

        for (i = 0; i < 256; i++)
        {
            uint16_t c = snapshot->fadedPalette[i];
            uint8_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;

            /* the atlas's expansion (metatile_atlas.c), so walls meet the ground */
            rgba[i * 4 + 0] = (uint8_t)((r << 3) | (r >> 2));
            rgba[i * 4 + 1] = (uint8_t)((g << 3) | (g >> 2));
            rgba[i * 4 + 2] = (uint8_t)((b << 3) | (b >> 2));
            rgba[i * 4 + 3] = 255;
        }
        glBindTexture(GL_TEXTURE_2D, sPaletteTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        sPaletteGeneration = snapshot->paletteGeneration;
        sHasPalette = true;
    }
    if (!CollectInstances(snapshot, instances, &count))
        count = 0;
    if (count == sInstanceCount && memcmp(instances, sInstances, count * sizeof(*instances)) == 0
     && snapshot->gridOriginX == sGridOriginX && snapshot->gridOriginY == sGridOriginY
     && sVertexCount != 0)
        return;
    memcpy(sInstances, instances, count * sizeof(*instances));
    sGridOriginX = (int16_t)snapshot->gridOriginX;
    sGridOriginY = (int16_t)snapshot->gridOriginY;
    sInstanceCount = count;
    sVertexCount = 0;
    sBatchCount = 0;
    sMetrics.instances = count;
    sMetrics.placements = 0;
    /* One batch a page: every map on screen draws from its own page. */
    for (page = 0; page < sPageTextureCount; page++)
    {
        uint32_t start = sVertexCount;

        for (i = 0; i < count; i++)
        {
            unsigned first;
            unsigned n = DioramaBuildings_LayoutPlacements(sInstances[i].layout, &first);
            unsigned p;

            for (p = 0; p < n; p++)
                if (!AppendPlacement(snapshot, resolvedCells, &sInstances[i], first + p,
                                     (uint16_t)page))
                    goto full;
        }
        if (sVertexCount != start)
        {
            sBatches[sBatchCount].page = (uint16_t)page;
            sBatches[sBatchCount].first = start;
            sBatches[sBatchCount].count = sVertexCount - start;
            sBatchCount++;
        }
    }
full:
    sMetrics.vertices = sVertexCount;
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sVertexCount * sizeof(*sVertices), sVertices, GL_STATIC_DRAW);
    {
        /* FNV-1a over the triangles: the grid moving a step rebuilds them
         * unchanged nearly every time. */
        const uint8_t *bytes = (const uint8_t *)sVertices;
        size_t size = (size_t)sVertexCount * sizeof(*sVertices);
        uint32_t hash = 2166136261u ^ sVertexCount;
        size_t k;

        for (k = 0; k < size; k++)
            hash = (hash ^ bytes[k]) * 16777619u;
        if (hash != sGeometryHash)
        {
            sGeometryHash = hash;
            sGeometryGeneration++;
        }
    }
}

uint32_t DioramaGLBuildings_GetGeometryGeneration(void)
{
    return sGeometryGeneration;
}

void DioramaGLBuildings_Draw(float cameraX, float cameraZ, float cameraPitch,
                             float focalLength, float aspectCorrection)
{
    unsigned i;

    sMetrics.drawCalls = 0;
    if (!sReady || sBatchCount == 0)
        return;
    dglUseProgram(sProgram);
    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1f(sAspectCorrectionLocation, aspectCorrection);
    dglUniform1i(sPageLocation, 0);
    dglUniform1i(sPaletteLocation, 1);
    dglUniform1i(sShadowPassLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniform1i(sShadowEnabledLocation, DioramaGLShadow_BindForLighting());
    dglUniform1f(sShadowAmbientLocation, DioramaGLShadow_Ambient());
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sPaletteTexture);
    dglActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); /* the models are not wound consistently */
    dglBindVertexArray(sVertexArray);
    for (i = 0; i < sBatchCount; i++)
    {
        GLuint texture = PageTexture(sBatches[i].page);

        if (texture == 0)
            continue;
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, (GLint)sBatches[i].first, (GLsizei)sBatches[i].count);
        sMetrics.drawCalls++;
    }
    glDisable(GL_DEPTH_TEST);
}

void DioramaGLBuildings_DrawShadow(void)
{
    unsigned i;

    if (!sReady || sBatchCount == 0)
        return;
    dglUseProgram(sProgram);
    dglUniform1i(sPageLocation, 0);
    dglUniform1i(sPaletteLocation, 1);
    dglUniform1i(sShadowPassLocation, 1);
    dglUniform1i(sShadowEnabledLocation, 0);
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sPaletteTexture);
    dglActiveTexture(GL_TEXTURE0);
    dglBindVertexArray(sVertexArray);
    for (i = 0; i < sBatchCount; i++)
    {
        GLuint texture = PageTexture(sBatches[i].page);

        if (texture == 0)
            continue;
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLES, (GLint)sBatches[i].first, (GLsizei)sBatches[i].count);
    }
    dglUniform1i(sShadowPassLocation, 0);
}

const struct DioramaBuildingMetrics *DioramaGLBuildings_GetMetrics(void)
{
    return &sMetrics;
}

void DioramaGLBuildings_Shutdown(void)
{
    unsigned i;

    if (sPageTextures != NULL)
    {
        for (i = 0; i < sPageTextureCount; i++)
            if (sPageTextures[i] != 0)
                glDeleteTextures(1, &sPageTextures[i]);
    }
    free(sPageTextures);
    free(sBatches);
    free(sVertices);
    sPageTextures = NULL;
    sBatches = NULL;
    sVertices = NULL;
    sPageTextureCount = 0;
    sVertexCount = sVertexCapacity = 0;
    sBatchCount = 0;
    sInstanceCount = 0;
    if (sPaletteTexture != 0)
        glDeleteTextures(1, &sPaletteTexture);
    sPaletteTexture = 0;
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sVertexBuffer = 0;
    sVertexArray = 0;
    sProgram = 0;
    sHasPalette = false;
    sReady = false;
}
#endif // ENABLE_DIORAMA
