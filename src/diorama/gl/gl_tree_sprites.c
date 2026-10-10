#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_grade.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_shadow.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/gl_tree_sprites.h"
#include "diorama/rules.h"
#include "diorama/tree_sprites.h"
#include "diorama/trees_3ds.generated.h"

/* Two quads a cell at most: the trunk on the ground and a piece of crown. */
#define VERTICES_PER_QUAD 6
#define MAX_VERTICES (DIORAMA_MAX_VISIBLE_CELLS * 2 * VERTICES_PER_QUAD)
#define TEXEL (1.0f / DIORAMA_TREES_3DS_DIM)
/* The trunk's art a little over the ground it is laid on. */
#define TRUNK_LIFT 0.004f

struct TreeVertex
{
    float x, y, z;
    float u, v;
};

static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLuint sTexture;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
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
/* The crowns first (the shadow pass draws only them), then the trunks. */
static struct TreeVertex sVertices[MAX_VERTICES];
static unsigned sCrownVertexCount;
static unsigned sVertexCount;
static uint64_t sSequence;
static uint32_t sGeometryHash;
static uint32_t sGeometryGeneration;

static const char sVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "uniform int shadowPass;\n"
    "uniform mat4 lightViewProjection;\n"
    "out vec2 uv;\n"
    "out vec3 sceneWorldPosition;\n"
    "out float viewDepth;\n"
    "void main() {\n"
    /* The terrain's projection (gl_terrain_renderer.c), so the two meet. */
    "  const float cameraHeight = 16.0;\n"
    "  const float cameraTargetVertical = -0.458944;\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = 80.0;\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float cameraDistance = (cameraHeight * pitchCos + cameraTargetVertical) / pitchSin;\n"
    "  float relativeX = position.x - cameraPosition.x;\n"
    "  float relativeZ = position.z - cameraPosition.y;\n"
    "  float depth = (cameraHeight - position.y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;\n"
    "  float vertical = -(cameraHeight - position.y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;\n"
    "  float clipX = relativeX * focalLength * 2.0 / 240.0 * aspectCorrection;\n"
    "  float clipY = -0.04 * depth + vertical * focalLength * 2.0 / 160.0;\n"
    "  float clipZ = ((farDepth + nearDepth) / (farDepth - nearDepth)) * depth"
    "              - (2.0 * farDepth * nearDepth) / (farDepth - nearDepth);\n"
    "  gl_Position = vec4(clipX, clipY, clipZ, depth);\n"
    "  if (shadowPass == 1) gl_Position = lightViewProjection * vec4(position, 1.0);\n"
    "  viewDepth = depth;\n"
    "  sceneWorldPosition = position;\n"
    "  uv = texCoord;\n"
    "}\n";

static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
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
    "void main() {\n"
    "  vec4 texel = texture(image, uv);\n"
    "  if (texel.a < 0.5) discard;\n" /* alpha test: no colour, no depth */
    "  if (shadowPass == 1) { color = vec4(1.0); return; }\n"
    "  vec3 c = clamp(texel.rgb * pixelScale + pixelOffset, 0.0, 1.0);\n"
    "  color = vec4(Grade(c) * Light(mix(shadowAmbient, 1.0, SunLit()))\n"
    "             * Dapple(sceneWorldPosition), 1.0);\n"
    "  color.rgb = Haze(color.rgb, viewDepth);\n"
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
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0 && sFocalLengthLocation >= 0
        && sAspectCorrectionLocation >= 0 && sImageLocation >= 0
        && sShadowPassLocation >= 0 && sShadowEnabledLocation >= 0
        && sLightMatrixLocation >= 0 && sShadowMapLocation >= 0
        && sShadowAmbientLocation >= 0 && sSaturationLocation >= 0
        && sPixelScaleLocation >= 0 && sPixelOffsetLocation >= 0 && lightLocated;
}

bool DioramaGLTreeSprites_Init(void)
{
    if (sReady)
        return true;
    if (!DioramaTreeSprites_Enabled())
        return false;
    if (!CreateProgram())
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
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct TreeVertex),
                           (const void *)offsetof(struct TreeVertex, x));
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct TreeVertex),
                           (const void *)offsetof(struct TreeVertex, u));
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    sReady = true;
    return true;
}

/*
 * A quad in the 3DS port's terms: corners a (top left of the art) b c d,
 * each (x east, y up, z south) from the cell's north-west corner on its
 * ground, with the art's texel rectangle from (u0, v0) to (u1, v1). The scene's
 * z points north and a cell is centred on its grid coordinates.
 */
static struct TreeVertex *Quad(struct TreeVertex *out, int mapX, int mapY, float ground,
                               const float corners[4][3], float u0, float v0, float u1, float v1)
{
    static const unsigned order[VERTICES_PER_QUAD] = {0, 1, 2, 0, 2, 3};
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    unsigned i;

    for (i = 0; i < VERTICES_PER_QUAD; i++)
    {
        const float *corner = corners[order[i]];

        out[i].x = (float)mapX - 0.5f + corner[0];
        out[i].y = ground + corner[1];
        out[i].z = -((float)mapY - 0.5f + corner[2]);
        out[i].u = uv[order[i]][0];
        out[i].v = uv[order[i]][1];
    }
    return out + VERTICES_PER_QUAD;
}

/* The trunk's art over the whole cell, laid on its ground (VoxelMesh_Top). */
static struct TreeVertex *Trunk(struct TreeVertex *out, int mapX, int mapY, float ground,
                                int texelX, int texelY)
{
    const float corners[4][3] = {
        {0.0f, TRUNK_LIFT, 0.0f}, {1.0f, TRUNK_LIFT, 0.0f},
        {1.0f, TRUNK_LIFT, 1.0f}, {0.0f, TRUNK_LIFT, 1.0f},
    };

    return Quad(out, mapX, mapY, ground, corners, texelX * TEXEL, texelY * TEXEL,
                (texelX + 16) * TEXEL, (texelY + 16) * TEXEL);
}

/*
 * EmitCell: a large tree's quarter. The crown keeps its 32:36 aspect ratio,
 * 2 tiles wide and 2.25 long, at 50 degrees (sin and cos), its transparent
 * bottom margin sunk 0.10 into the ground so the leaves overlap the trunk;
 * each quarter carries its column and row of it.
 */
static struct TreeVertex *LargeCrown(struct TreeVertex *out, int mapX, int mapY, float ground,
                                     int part)
{
    const float rise = 1.723600f;
    const float run = 1.446272f;
    const float baseHeight = -0.10f;
    int col = part & 1;
    int row = part >> 1;
    float baseZ = 1.35f - (float)row;
    float top = 1.0f - (float)row * 0.5f;
    float bottom = top - 0.5f;
    const float corners[4][3] = {
        {0.0f, baseHeight + top * rise, baseZ - top * run},
        {1.0f, baseHeight + top * rise, baseZ - top * run},
        {1.0f, baseHeight + bottom * rise, baseZ - bottom * run},
        {0.0f, baseHeight + bottom * rise, baseZ - bottom * run},
    };

    return Quad(out, mapX, mapY, ground, corners, col * 16 * TEXEL, row * 18 * TEXEL,
                (col * 16 + 16) * TEXEL, (row * 18 + 18) * TEXEL);
}

/*
 * EmitSmallCell: the small crown is 16:32, 1 tile wide and 2 long, at the
 * same 50 degrees and sunk the same way, standing on its one-cell trunk, 0.15
 * further forward than half the large tree's (Dewford's another 0.15, so it
 * stands over the shadow drawn on its sand).
 */
static struct TreeVertex *SmallCrown(struct TreeVertex *out, int mapX, int mapY, float ground,
                                     bool dewford)
{
    const float rise = 1.532089f;
    const float run = 1.285575f;
    const float baseHeight = -0.10f;
    int cx = dewford ? 64 : 32;
    int cy = dewford ? 0 : 32;
    float baseZ = dewford ? 0.975f : 0.825f;
    const float corners[4][3] = {
        {0.0f, baseHeight + rise, baseZ - run},
        {1.0f, baseHeight + rise, baseZ - run},
        {1.0f, baseHeight, baseZ},
        {0.0f, baseHeight, baseZ},
    };

    return Quad(out, mapX, mapY, ground, corners, cx * TEXEL, cy * TEXEL,
                (cx + 16) * TEXEL, (cy + 32) * TEXEL);
}

static uint32_t HashVertices(const struct TreeVertex *vertices, unsigned count)
{
    const uint8_t *bytes = (const uint8_t *)vertices;
    size_t length = count * sizeof(*vertices);
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < length; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash ^ count;
}

void DioramaGLTreeSprites_Sync(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaResolvedCell *resolvedCells)
{
    struct TreeVertex *crowns = sVertices;
    struct TreeVertex *trunks;
    unsigned crownCount = 0;
    unsigned i;
    uint32_t hash;

    if (!sReady || snapshot == NULL || snapshot->sequence == sSequence)
        return;
    sSequence = snapshot->sequence;
    /* The crowns from the front of the buffer, the trunks after them. */
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        if (cell->treeSprite == DIORAMA_TREE_SPRITE_NONE)
            continue;
        if (cell->treeSprite == DIORAMA_TREE_SPRITE_SMALL
         || cell->treeSprite == DIORAMA_TREE_SPRITE_SMALL_DEWFORD)
            crowns = SmallCrown(crowns, cell->mapX, cell->mapY, resolvedCells[i].topHeight,
                                cell->treeSprite == DIORAMA_TREE_SPRITE_SMALL_DEWFORD);
        else
            crowns = LargeCrown(crowns, cell->mapX, cell->mapY, resolvedCells[i].topHeight,
                                cell->treeSprite - DIORAMA_TREE_SPRITE_QUARTER);
        crownCount += VERTICES_PER_QUAD;
    }
    trunks = crowns;
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        float ground = resolvedCells[i].topHeight;

        if (cell->treeSprite == DIORAMA_TREE_SPRITE_NONE)
            continue;
        if (cell->treeSprite == DIORAMA_TREE_SPRITE_SMALL)
            trunks = Trunk(trunks, cell->mapX, cell->mapY, ground, 48, 32);
        else if (cell->treeSprite == DIORAMA_TREE_SPRITE_SMALL_DEWFORD)
            trunks = Trunk(trunks, cell->mapX, cell->mapY, ground, 80, 0);
        else
        {
            int part = cell->treeSprite - DIORAMA_TREE_SPRITE_QUARTER;

            trunks = Trunk(trunks, cell->mapX, cell->mapY, ground,
                           32 + (part & 1) * 16, (part >> 1) * 16);
        }
    }
    sCrownVertexCount = crownCount;
    sVertexCount = (unsigned)(trunks - sVertices);
    hash = HashVertices(sVertices, sCrownVertexCount);
    if (hash != sGeometryHash)
    {
        sGeometryHash = hash;
        sGeometryGeneration++;
    }
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sVertexCount * sizeof(struct TreeVertex),
                  sVertexCount != 0 ? sVertices : NULL, GL_DYNAMIC_DRAW);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DioramaGLTreeSprites_Draw(float cameraX, float cameraZ, float cameraPitch,
                               float focalLength, float aspectCorrection)
{
    float scale[3];
    float offset[3];

    if (!sReady || sVertexCount == 0)
        return;
    DioramaGLTerrain_GetPaletteLine(scale, offset);
    dglUseProgram(sProgram);
    dglUniform2f(sCameraLocation, cameraX, cameraZ);
    dglUniform1f(sCameraPitchLocation, cameraPitch);
    dglUniform1f(sFocalLengthLocation, focalLength);
    dglUniform1f(sAspectCorrectionLocation, aspectCorrection);
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
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sTexture);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    dglBindVertexArray(sVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sVertexCount);
    dglBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_DEPTH_TEST);
}

void DioramaGLTreeSprites_DrawShadow(void)
{
    if (!sReady || sCrownVertexCount == 0)
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
    dglBindVertexArray(sVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sCrownVertexCount);
    dglBindVertexArray(0);
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
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sTexture != 0)
        glDeleteTextures(1, &sTexture);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sVertexBuffer = sVertexArray = sTexture = sProgram = 0;
    sReady = false;
    sVertexCount = sCrownVertexCount = 0;
    sSequence = 0;
}

#endif // ENABLE_DIORAMA
