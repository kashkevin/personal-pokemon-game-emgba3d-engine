#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_grade.h"
#include "diorama/gl_lamps.h"
#include "diorama/gl_light.h"
#include "diorama/gl_shadow.h"
#include "diorama/gl_object_renderer.h"
#include "diorama/terrain_mesh.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/buildings.h"
#include "diorama/sprite_frame.h"

#define SPRITE_TEXTURE_CACHE_SIZE 48
#define OBJECT_RENDER_ITEM_CAPACITY (DIORAMA_MAX_OBJECTS + 1)

struct ObjectVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
};

struct SpriteTextureCacheEntry
{
    bool occupied;
    struct DioramaSpriteFrameKey key;
    GLuint texture;
    uint8_t width;
    uint8_t height;
    uint64_t lastUsedSequence;
};

struct ObjectRenderItem
{
    struct DioramaObjectSnapshot object;
    struct DioramaSpritePose previousPose;
    struct DioramaSpritePose currentPose;
    GLuint texture;
    GLuint reflectionTexture;
    uint8_t width;
    uint8_t height;
    bool interpolate;
    bool attachedToPlayer;
    int16_t previousAttachedOffsetX;
    int16_t previousAttachedOffsetY;
    int16_t attachedOffsetX;
    int16_t attachedOffsetY;
    float depthBias;
    bool drawOverPixelObjects;
};

static struct SpriteTextureCacheEntry sTextureCache[SPRITE_TEXTURE_CACHE_SIZE];
static struct ObjectRenderItem sItems[OBJECT_RENDER_ITEM_CAPACITY];
static struct DioramaSceneSnapshot sPreviousSnapshot;
static struct DioramaResolvedCell sPreviousResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
static uint16_t sPreviousResolvedCount;
static uint32_t sDecodePixels[DIORAMA_SPRITE_MAX_PIXELS];
static struct DioramaObjectMetrics sMetrics;
static uint64_t sSequence;
static uint32_t sMapGeneration;
static uint8_t sItemCount;
static bool sHasPreviousSnapshot;
static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLint sCameraLocation;
static GLint sCameraPitchLocation;
static GLint sFocalLengthLocation;
static GLint sAspectCorrectionLocation;
static GLint sCameraHeightLocation;
static GLint sCameraDistanceLocation;
static GLint sScreenOffsetLocation;
static GLint sImageLocation;
static GLint sDrawModeLocation;
static GLint sAnimationTimeLocation;
static GLint sDepthBiasLocation;
static GLint sShadowEnabledLocation;
static GLint sLightMatrixLocation;
static GLint sShadowMapLocation;
static GLint sShadowAmbientLocation;
static GLint sUprightZLocation;
static GLint sLightProbeLocation;
static GLint sShadeLocation;
static GLint sSaturationLocation;
static GLint sCastStrengthLocation;
static struct DioramaLightUniforms sLightUniforms;
static struct DioramaLampUniforms sLampUniforms;
/* The ground under the player, as last seen (DioramaGLObjects_GetPlayerGround). */
static float sPlayerGroundY;
/* Players seen through what hides them (DioramaGLObjects_SetSilhouettes). */
static bool sSilhouettes = true;
/* The player's item in sItems, or -1 (DioramaGLObjects_GetPlayerPose). */
static int sPlayerItem = -1;

/* The door standing open: see FindDoor. */
#define DOOR_DEPTH 0.03f   /* the opening, in front of its wall */
#define DOOR_HEIGHT 1.25f  /* the door's own metatile and the 4 pixels drawn over it */
#define DOOR_DUSK 0.5f     /* tiles before it over which a walker darkens */
#define DOOR_DARK 0.35f

static struct
{
    bool on;
    float west, east;  /* the doorway's columns, world x */
    float north;       /* the north edge of the door's cell, tiles south */
    float wall;        /* the wall's face, tiles south */
    float ground;
    float open;        /* 0-1 */
} sDoor;

static void FindDoor(const struct DioramaSceneSnapshot *snapshot,
                     const struct DioramaResolvedCell *resolvedCells);

static const char sObjectVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 texCoord;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    /* The eye (terrain_mesh.h, DioramaCameraRig). */
    "uniform float cameraHeight;\n"
    "uniform float cameraDistance;\n"
    "uniform float screenOffsetY;\n"
    "uniform float depthBias;\n"
    "out vec2 uv;\n"
    "out vec3 sceneWorldPosition;\n"
    "out float viewDepth;\n"
    "void main() {\n"
    "  sceneWorldPosition = position;\n"
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
    "  gl_Position = vec4(clipX, clipY, clipZ - depthBias * depth, depth);\n"
    "  viewDepth = depth;\n"
    "  uv = texCoord;\n"
    "}\n";

static const char sObjectFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in vec3 sceneWorldPosition;\n"
    "in float viewDepth;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform int drawMode;\n"
    "uniform float animationTime;\n"
    "uniform int shadowEnabled;\n"
    "uniform mat4 lightViewProjection;\n"
    "uniform sampler2DShadow shadowMap;\n"
    "uniform float shadowAmbient;\n"
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float cameraHeight;\n"
    "uniform float cameraDistance;\n"
    "uniform float depthBias;\n"
    "uniform float uprightZ;\n"
    "uniform float shade;\n"
    "uniform float castStrength;\n"
    DIORAMA_GLSL_GRADE
    /* The card leans back to face the camera, so its top half reaches over
     * whatever stands north of the feet - a counter, a house's front wall -
     * and was cut off by it. For hiding and being hidden it counts as a card
     * standing upright at uprightZ instead (its feet, or a model's front just
     * south of them): the point of that card on this pixel's line of sight.
     * Everything north of the feet is then behind the walker and everything
     * south of them in front, as the GBA draws them; the picture is the
     * leaning card's. */
    "float UprightDepth(float margin) {\n"
    "  const float nearDepth = 0.1;\n"
    "  const float farDepth = " DIORAMA_FAR_DEPTH_GLSL ";\n"
    "  float pitchSin = sin(cameraPitch);\n"
    "  float pitchCos = cos(cameraPitch);\n"
    "  float cameraZ = cameraPosition.y - cameraDistance;\n"
    "  vec3 p = sceneWorldPosition;\n"
    "  float depth = (cameraHeight - p.y) * pitchSin + (p.z - cameraZ) * pitchCos;\n"
    "  if (p.z > cameraZ && uprightZ > cameraZ)\n"
    "    depth *= clamp((uprightZ - cameraZ) / (p.z - cameraZ), 0.0, 1.0);\n"
    "  depth = max(depth - margin, nearDepth);\n"
    "  float ndc = (farDepth + nearDepth) / (farDepth - nearDepth)\n"
    "            - 2.0 * farDepth * nearDepth / ((farDepth - nearDepth) * depth) - depthBias;\n"
    "  return (gl_DepthRange.diff * ndc + gl_DepthRange.near + gl_DepthRange.far) * 0.5;\n"
    "}\n"
    /* gl_shadow.h: a sprite walking into a building's shadow darkens with
     * the ground there; sprites cast no shadow of their own into the map.
     * One sample for the whole card, three quarters of a tile over its feet
     * (as the 3DS port lights its cards): the leaning card itself reaches
     * into the wall behind a walker, and would come out half shaded. */
    "uniform vec3 lightProbe;\n"
    "float SunLit() {\n" /* 1 in the sun, 0 in a cast shadow */
    "  if (shadowEnabled == 0) return 1.0;\n"
    "  vec4 lightPosition = lightViewProjection * vec4(lightProbe, 1.0);\n"
    "  vec3 p = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;\n"
    "  if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
    "  return texture(shadowMap, vec3(p.xy, p.z - 0.0015));\n"
    "}\n"
    DIORAMA_GLSL_LIGHT
    DIORAMA_GLSL_LAMPS
    /* drawMode 5, the see-through ghost of a player something stands in
     * front of (DrawSilhouettes): its depth is taken this far nearer the eye,
     * so only what stands clearly in front of the walker shows it - a house,
     * a tree's crown - and not the grass at its feet. */
    "const float silhouetteMargin = 0.75;\n"
    "void main() {\n"
    "  gl_FragDepth = drawMode == 0 ? UprightDepth(0.0)\n"
    "               : (drawMode == 5 ? UprightDepth(silhouetteMargin) : gl_FragCoord.z);\n"
    "  if (drawMode == 5) {\n"
    "    vec4 texel = texture(image, uv);\n"
    "    if (texel.a < 0.5) discard;\n"
    /* The sprite's own light and dark in a cool blue, see-through: still
     * the player at a glance, plainly behind what hides them. */
    "    float luma = dot(texel.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    color = vec4(mix(vec3(0.13, 0.25, 0.52), vec3(0.66, 0.84, 1.0), luma), 0.62);\n"
    "    return;\n"
    "  }\n"
    "  if (drawMode == 3) {\n"
    "    color = vec4(0.0, 0.0, 0.0, 1.0);\n" /* an open doorway */
    "  } else if (drawMode == 4) {\n"
    /* The sprite's own picture laid on the ground along the sun (DrawCastShadow):
     * its silhouette in the 3DS port's dark blue, as strong as the sun is on
     * the walker (none in a building's shadow). */
    "    if (texture(image, uv).a < 0.5) discard;\n"
    "    float lit = shadowEnabled == 0 ? 0.0 : SunLit();\n"
    "    color = vec4(0.031, 0.078, 0.157, castStrength * clamp(lit, 0.0, 1.0));\n"
    "  } else if (drawMode == 1) {\n"
    "    vec2 point = uv * 2.0 - 1.0;\n"
    "    float distanceSquared = dot(point, point);\n"
    "    if (distanceSquared >= 1.0) discard;\n"
    "    color = vec4(0.02, 0.025, 0.03, 0.32 * (1.0 - distanceSquared));\n"
    "  } else {\n"
    "    vec2 sampleUv = uv;\n"
    "    if (drawMode == 2) sampleUv.x += sin(animationTime * 5.0 + uv.y * 14.0) * 0.012;\n"
    "    vec4 texel = texture(image, sampleUv);\n"
    "    if (texel.a < 0.5) discard;\n"
    /* A sprite stands in the light of the ground at its feet (the 3DS port). */
    "    color = vec4(Grade(texel.rgb) * Light(mix(shadowAmbient, 1.0, SunLit()))\n"
    "               * Dapple(lightProbe) * shade, texel.a);\n"
    "    color.rgb += Grade(texel.rgb) * Lamps(lightProbe) * shade;\n"
    "    color.rgb = Haze(color.rgb, viewDepth, sceneWorldPosition);\n"
    "  }\n"
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
        SDL_Log("Diorama object shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool CreateProgram(void)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, sObjectVertexShader);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sObjectFragmentShader);
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
        SDL_Log("Diorama object shader link failed: %s", log);
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
    sDrawModeLocation = dglGetUniformLocation(sProgram, "drawMode");
    sAnimationTimeLocation = dglGetUniformLocation(sProgram, "animationTime");
    sDepthBiasLocation = dglGetUniformLocation(sProgram, "depthBias");
    sShadowEnabledLocation = dglGetUniformLocation(sProgram, "shadowEnabled");
    sLightMatrixLocation = dglGetUniformLocation(sProgram, "lightViewProjection");
    sShadowMapLocation = dglGetUniformLocation(sProgram, "shadowMap");
    sShadowAmbientLocation = dglGetUniformLocation(sProgram, "shadowAmbient");
    sUprightZLocation = dglGetUniformLocation(sProgram, "uprightZ");
    sLightProbeLocation = dglGetUniformLocation(sProgram, "lightProbe");
    sShadeLocation = dglGetUniformLocation(sProgram, "shade");
    sSaturationLocation = dglGetUniformLocation(sProgram, "saturation");
    sCastStrengthLocation = dglGetUniformLocation(sProgram, "castStrength");
    lightLocated = DioramaGLLight_Locate(sProgram, &sLightUniforms);
    DioramaGLLamps_Locate(sProgram, &sLampUniforms);
    return sCameraLocation >= 0 && sCameraPitchLocation >= 0
        && sFocalLengthLocation >= 0 && sAspectCorrectionLocation >= 0
        && sImageLocation >= 0
        && sDrawModeLocation >= 0 && sAnimationTimeLocation >= 0
        && sDepthBiasLocation >= 0 && sShadowEnabledLocation >= 0
        && sLightMatrixLocation >= 0 && sShadowMapLocation >= 0
        && sShadowAmbientLocation >= 0
        && sUprightZLocation >= 0 && sLightProbeLocation >= 0
        && sShadeLocation >= 0 && sSaturationLocation >= 0
        && sCastStrengthLocation >= 0 && lightLocated;
}

static float ResolveDepthBias(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaResolvedCell *resolvedCells,
                              const struct DioramaObjectSnapshot *object)
{
    int i;

    for (i = 0; i < snapshot->visibleCellCount; i++)
        if (snapshot->cells[i].mapX == object->currentMapX
         && snapshot->cells[i].mapY == object->currentMapY)
        {
            const struct DioramaGeneratedPixelObjectV2 *pixelObject =
                DioramaRules_GetPixelObject(resolvedCells[i].pixelObjectId);

            return pixelObject == NULL ? 0.0f
                 : pixelObject->spriteDepthBiasMillionths / 1000000.0f;
        }
    return 0.0f;
}

static bool IsInFrontOfPixelObject(const struct DioramaSceneSnapshot *snapshot,
                                   const struct DioramaResolvedCell *resolvedCells,
                                   const struct DioramaObjectSnapshot *object)
{
    int i;

    for (i = 0; i < snapshot->visibleCellCount; i++)
        if (snapshot->cells[i].mapX == object->currentMapX
         && snapshot->cells[i].mapY == object->currentMapY - 1)
            return resolvedCells[i].pixelObjectId != 0;
    return false;
}

static void ClearTextureCache(void)
{
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
    {
        if (sTextureCache[i].texture != 0)
            glDeleteTextures(1, &sTextureCache[i].texture);
        memset(&sTextureCache[i], 0, sizeof(sTextureCache[i]));
    }
}

static struct SpriteTextureCacheEntry *FindTexture(const struct DioramaSpriteFrameKey *key)
{
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
        if (sTextureCache[i].occupied
         && DioramaSprite_FrameKeyEqual(&sTextureCache[i].key, key))
            return &sTextureCache[i];
    return NULL;
}

static struct SpriteTextureCacheEntry *AllocateTexture(void)
{
    struct SpriteTextureCacheEntry *oldest = &sTextureCache[0];
    int i;

    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
    {
        if (!sTextureCache[i].occupied)
            return &sTextureCache[i];
        if (sTextureCache[i].lastUsedSequence < oldest->lastUsedSequence)
            oldest = &sTextureCache[i];
    }
    if (oldest->texture != 0)
        glDeleteTextures(1, &oldest->texture);
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static struct SpriteTextureCacheEntry *GetTexture(const struct DioramaSceneSnapshot *snapshot,
                                                   const struct DioramaObjectSnapshot *object)
{
    struct DioramaSpriteFrameKey key;
    struct SpriteTextureCacheEntry *entry;
    uint8_t width;
    uint8_t height;

    DioramaSprite_MakeFrameKey(snapshot, object, &key);
    entry = FindTexture(&key);
    if (entry != NULL)
    {
        entry->lastUsedSequence = snapshot->sequence;
        return entry;
    }
    if (!DioramaSprite_DecodeFrame(snapshot, object, sDecodePixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height))
        return NULL;
    entry = AllocateTexture();
    glGenTextures(1, &entry->texture);
    if (entry->texture == 0)
        return NULL;
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, sDecodePixels);
    entry->occupied = true;
    entry->key = key;
    entry->width = width;
    entry->height = height;
    entry->lastUsedSequence = snapshot->sequence;
    sMetrics.uploadedFrames++;
    return entry;
}

static const struct DioramaObjectSnapshot *FindPreviousObject(
    const struct DioramaObjectSnapshot *object)
{
    int i;

    if (!sHasPreviousSnapshot)
        return NULL;
    for (i = 0; i < sPreviousSnapshot.objectCount; i++)
    {
        const struct DioramaObjectSnapshot *candidate = &sPreviousSnapshot.objects[i];

        if (candidate->localId == object->localId
         && candidate->mapNum == object->mapNum
         && candidate->mapGroup == object->mapGroup
         && !((candidate->flags ^ object->flags) & DIORAMA_OBJECT_PLAYER))
            return candidate;
    }
    return NULL;
}

bool DioramaGLObjects_Init(void)
{
    memset(sTextureCache, 0, sizeof(sTextureCache));
    memset(sItems, 0, sizeof(sItems));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sSequence = 0;
    sMapGeneration = 0;
    sItemCount = 0;
    sHasPreviousSnapshot = false;
    sReady = false;
    if (!CreateProgram())
        return false;
    dglGenVertexArrays(1, &sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct ObjectVertex), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct ObjectVertex),
                           (void *)offsetof(struct ObjectVertex, u));
    return true;
}

void DioramaGLObjects_Reset(void)
{
    ClearTextureCache();
    memset(sItems, 0, sizeof(sItems));
    memset(&sMetrics, 0, sizeof(sMetrics));
    sSequence = 0;
    sItemCount = 0;
    sHasPreviousSnapshot = false;
    sReady = false;
    sDoor.on = false;
}

// PKMN_DIORAMA_LOG=1: log why a frame cannot use the 3D view (once per change).
static void LogObjectRefusal(const struct DioramaObjectSnapshot *object, const char *reason)
{
    static int sLogEnabled = -1;
    static char sLast[160];
    char line[160];

    if (sLogEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");
        sLogEnabled = value != NULL && value[0] == '1';
    }
    if (!sLogEnabled)
        return;
    SDL_snprintf(line, sizeof(line),
                 "Diorama: object local %u gfx %u refused (%s): flags 0x%x affine %u objMode %u bpp %u shape %u size %u frame %u",
                 object->localId, object->graphicsId, reason, object->flags, object->affineMode,
                 object->objMode, object->bpp, object->oamShape, object->oamSize, object->frameSize);
    if (SDL_strcmp(line, sLast) != 0)
    {
        SDL_strlcpy(sLast, line, sizeof(sLast));
        SDL_Log("%s", line);
    }
}

bool DioramaGLObjects_Sync(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaResolvedCell *resolvedCells)
{
    struct ObjectRenderItem nextItems[OBJECT_RENDER_ITEM_CAPACITY];
    uint8_t nextItemCount = 0;
    bool playerFound = false;
    int playerItem = -1;
    int i;

    if (snapshot == NULL || resolvedCells == NULL)
        return false;
    if (snapshot->sequence == sSequence)
        return sReady;
    sMetrics.uploadedFrames = 0;
    if (sMapGeneration != 0 && snapshot->mapGeneration != sMapGeneration)
        ClearTextureCache();
    sMapGeneration = snapshot->mapGeneration;

    for (i = 0; i < snapshot->objectCount; i++)
    {
        const struct DioramaObjectSnapshot *object = &snapshot->objects[i];
        const struct DioramaObjectSnapshot *previousObject;
        struct SpriteTextureCacheEntry *texture;
        struct ObjectRenderItem *item;

        if (!object->active || (object->flags & (DIORAMA_OBJECT_INVISIBLE | DIORAMA_OBJECT_OFFSCREEN)))
            continue;
        if (!DioramaSprite_IsSupported(object) || nextItemCount >= DIORAMA_MAX_OBJECTS)
        {
            LogObjectRefusal(object, nextItemCount >= DIORAMA_MAX_OBJECTS ? "too many" : "unsupported sprite");
            return false;
        }
        texture = GetTexture(snapshot, object);
        if (texture == NULL)
        {
            LogObjectRefusal(object, "no texture");
            return false;
        }
        item = &nextItems[nextItemCount++];
        memset(item, 0, sizeof(*item));
        item->object = *object;
        item->texture = texture->texture;
        item->width = texture->width;
        item->height = texture->height;
        item->depthBias = ResolveDepthBias(snapshot, resolvedCells, object);
        item->drawOverPixelObjects = IsInFrontOfPixelObject(snapshot, resolvedCells, object);
        if ((object->flags & DIORAMA_OBJECT_REFLECTION) && !object->reflectionHidden)
        {
            struct DioramaObjectSnapshot reflection = *object;

            reflection.paletteNum = object->reflectionPaletteNum;
            reflection.vFlip = !object->vFlip;
            texture = GetTexture(snapshot, &reflection);
            if (texture == NULL)
                return false;
            item->reflectionTexture = texture->texture;
        }
        if (!DioramaSprite_BuildResolvedPose(snapshot, resolvedCells,
                                             snapshot->visibleCellCount,
                                             object, &item->currentPose))
        {
            LogObjectRefusal(object, "no pose");
            return false;
        }
        item->previousPose = item->currentPose;
        previousObject = FindPreviousObject(object);
        if (previousObject != NULL
         && DioramaSprite_CanInterpolateResolved(
                &sPreviousSnapshot, sPreviousResolvedCells, sPreviousResolvedCount,
                previousObject, snapshot, resolvedCells, snapshot->visibleCellCount,
                object))
        {
            DioramaSprite_BuildResolvedPose(&sPreviousSnapshot, sPreviousResolvedCells,
                                            sPreviousResolvedCount, previousObject,
                                            &item->previousPose);
            item->interpolate = true;
        }
        if (object->flags & DIORAMA_OBJECT_PLAYER)
        {
            playerFound = true;
            playerItem = nextItemCount - 1;
        }
    }
    /* The player hidden (gone through a door, a cutscene): the scene is still
     * drawn, only without the player and the surf blob it would carry. */
    if (playerFound
     && snapshot->surfBlobValid
     && !(snapshot->surfBlob.flags & DIORAMA_OBJECT_INVISIBLE)
     && nextItemCount < OBJECT_RENDER_ITEM_CAPACITY)
    {
        const struct DioramaObjectSnapshot *blob = &snapshot->surfBlob;
        struct SpriteTextureCacheEntry *texture;
        struct ObjectRenderItem *item;

        if (!DioramaSprite_IsSupported(blob))
            return false;
        texture = GetTexture(snapshot, blob);
        if (texture == NULL)
            return false;
        item = &nextItems[nextItemCount++];
        memset(item, 0, sizeof(*item));
        item->object = *blob;
        item->texture = texture->texture;
        item->width = texture->width;
        item->height = texture->height;
        item->previousPose = nextItems[playerItem].previousPose;
        item->currentPose = nextItems[playerItem].currentPose;
        item->interpolate = nextItems[playerItem].interpolate;
        item->depthBias = nextItems[playerItem].depthBias;
        item->drawOverPixelObjects = nextItems[playerItem].drawOverPixelObjects;
        item->attachedToPlayer = true;
        item->previousAttachedOffsetX = snapshot->surfBlobOffsetX;
        item->previousAttachedOffsetY = snapshot->surfBlobOffsetY;
        item->attachedOffsetX = snapshot->surfBlobOffsetX;
        item->attachedOffsetY = snapshot->surfBlobOffsetY;
        if (item->interpolate && sPreviousSnapshot.surfBlobValid)
        {
            item->previousAttachedOffsetX = sPreviousSnapshot.surfBlobOffsetX;
            item->previousAttachedOffsetY = sPreviousSnapshot.surfBlobOffsetY;
        }
    }

    memcpy(sItems, nextItems, nextItemCount * sizeof(*sItems));
    sItemCount = nextItemCount;
    sPlayerItem = playerFound ? playerItem : -1;
    if (playerFound)
        sPlayerGroundY = nextItems[playerItem].currentPose.groundY;
    FindDoor(snapshot, resolvedCells);
    sMetrics.visibleObjects = sItemCount;
    sMetrics.cachedFrames = 0;
    for (i = 0; i < SPRITE_TEXTURE_CACHE_SIZE; i++)
        if (sTextureCache[i].occupied)
            sMetrics.cachedFrames++;
    sPreviousSnapshot = *snapshot;
    memcpy(sPreviousResolvedCells, resolvedCells,
           snapshot->visibleCellCount * sizeof(*sPreviousResolvedCells));
    sPreviousResolvedCount = snapshot->visibleCellCount;
    sHasPreviousSnapshot = true;
    sSequence = snapshot->sequence;
    sReady = true;
    return true;
}

static void UploadAndDraw(const struct ObjectVertex vertices[6])
{
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglBufferData(GL_ARRAY_BUFFER, 6 * sizeof(*vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    sMetrics.drawCalls++;
}

static void DrawShadow(const struct ObjectRenderItem *item, struct DioramaSpritePose pose)
{
    float radiusX = 0.28f + item->object.shadowSize * 0.06f;
    float radiusZ = 0.16f + item->object.shadowSize * 0.035f;
    float y = pose.groundY + 0.012f;
    const struct ObjectVertex vertices[6] = {
        {pose.x - radiusX, y, pose.z - radiusZ, 0, 0},
        {pose.x + radiusX, y, pose.z - radiusZ, 1, 0},
        {pose.x + radiusX, y, pose.z + radiusZ, 1, 1},
        {pose.x - radiusX, y, pose.z - radiusZ, 0, 0},
        {pose.x + radiusX, y, pose.z + radiusZ, 1, 1},
        {pose.x - radiusX, y, pose.z + radiusZ, 0, 1},
    };

    UploadAndDraw(vertices);
}

/*
 * Out in the sun a walker's shadow is its own picture laid on the ground: the
 * feet stay where they are and every row slides away from the sun by its
 * height, where the fixed sun puts the shadow of a card standing there. Same
 * texture as the sprite, so it walks, turns and animates with it. Ported from
 * the 3DS project's EmitCastShadow (voxel_entities.c, MIT; see
 * diorama/buildings.h), on flat ground (the 3DS lifts each corner with its
 * relief). Indoors and in bad weather, with no sun, the round shadow stays.
 */
#define CAST_SHADOW_ALPHA 0.36f /* the 3DS port's VOXEL_CAST_SHADOW_ALPHA */
#define CAST_SHADOW_LIFT 0.03f  /* over decals */
#define SUN_EAST 0.85f          /* the shadow's way per tile of height (gl_shadow.c) */
#define SUN_SOUTH 0.55f

static void DrawCastShadow(const struct ObjectRenderItem *item, struct DioramaSpritePose pose)
{
    float halfWidth = item->width / 32.0f;
    float height = item->height / 16.0f;
    float y = pose.groundY + CAST_SHADOW_LIFT;
    float east = SUN_EAST * height;
    float south = SUN_SOUTH * height;
    const struct ObjectVertex vertices[6] = {
        {pose.x - halfWidth, y, pose.z, 0, 1},
        {pose.x + halfWidth, y, pose.z, 1, 1},
        {pose.x + halfWidth + east, y, pose.z - south, 1, 0},
        {pose.x - halfWidth, y, pose.z, 0, 1},
        {pose.x + halfWidth + east, y, pose.z - south, 1, 0},
        {pose.x - halfWidth + east, y, pose.z - south, 0, 0},
    };

    glBindTexture(GL_TEXTURE_2D, item->texture);
    dglUniform3f(sLightProbeLocation, pose.x, pose.groundY + 0.75f, pose.z);
    UploadAndDraw(vertices);
}

static const struct DioramaCellSnapshot *FindCell(int mapX, int mapY)
{
    int i;

    for (i = 0; i < sPreviousSnapshot.visibleCellCount; i++)
        if (sPreviousSnapshot.cells[i].mapX == mapX && sPreviousSnapshot.cells[i].mapY == mapY)
            return &sPreviousSnapshot.cells[i];
    return NULL;
}

/*
 * Furniture against a room's back wall is modelled from its drawing, and the
 * drawing comes forward of the wall: to the middle of the cell before it,
 * where a walker's card stands, or further (a Pokemon Center's PC, two pixels
 * past it). The card's upright stand-in (UprightDepth in the shader) then
 * moves south to a pixel in front of the model's face there, as on the GBA.
 * Under either edge of the card, not only its middle: walking along the
 * furniture, half the card is over a model's cell before its middle is.
 * Ported from the 3DS project's ModelPush (voxel_entities.c, MIT; see
 * diorama/buildings.h). Returns how far south of the feet, in tiles.
 */
#define CARD_CLEAR (1.0f / 16.0f)
#define MODEL_PUSH_MAX 0.5f      /* further than that is not furniture's front */
#define MODEL_PUSH_HEIGHT 0.25f  /* where on the card the face is looked for */

static float ModelPush(struct DioramaSpritePose pose, float halfWidth)
{
    float left = pose.x - halfWidth + CARD_CLEAR;
    float right = pose.x + halfWidth - CARD_CLEAR;
    float south = -pose.z;
    int mapY = (int)floorf(south + 0.5f);
    int lastX = (int)floorf(right + 0.5f);
    int mapX;
    float push = 0.0f;

    if (!DioramaBuildings_IsAvailable())
        return 0.0f;
    for (mapX = (int)floorf(left + 0.5f); mapX <= lastX; mapX++)
    {
        const struct DioramaCellSnapshot *cell = FindCell(mapX, mapY);
        float originX;
        float originY;
        float cardZ;
        float front;
        float need = 0.0f;

        if (cell == NULL || !(cell->flags & DIORAMA_CELL_SOURCE_VALID))
            continue;
        /* the layout's tiles, cell x spanning x..x+1 */
        originX = (float)(mapX - cell->sourceMapX) - 0.5f;
        originY = (float)(mapY - cell->sourceMapY) - 0.5f;
        cardZ = south - originY;
        /* A model's face in the cell, whether or not the cell is the model's:
         * the PC's foot is drawn on the floor the walker uses it from. */
        if (DioramaBuildings_FrontAt(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY,
                                     left - originX, right - originX, MODEL_PUSH_HEIGHT, &front)
         && front + CARD_CLEAR > cardZ && front - cardZ + CARD_CLEAR <= MODEL_PUSH_MAX)
            need = front - cardZ + CARD_CLEAR;
        else if (DioramaBuildings_CellAt(cell->sourceLayoutId, cell->sourceMapX,
                                         cell->sourceMapY, NULL, NULL, NULL))
            need = CARD_CLEAR;
        if (need > push)
            push = need;
    }
    return push;
}

/*
 * A door standing open (the snapshot's door, from field_door.c). The game
 * animates its doors on the tilemap, which this view does not show, so a
 * walker went into a door that stayed shut. Here the doorway is a black
 * opening on the wall the door is drawn on, widening as the door opens, and
 * a walker going through it darkens and is gone into the dark, as on the GBA
 * it goes behind the wall. Ported from the 3DS project's EmitDoor and
 * DoorLight (voxel_entities.c, MIT; see diorama/buildings.h). Only on
 * modelled buildings: the 3DS also lays it flat on an unmodelled building's
 * door cell, whose drawing lies flat there; ours stand upright.
 */

static void FindDoor(const struct DioramaSceneSnapshot *snapshot,
                     const struct DioramaResolvedCell *resolvedCells)
{
    const struct DioramaCellSnapshot *cell = NULL;
    int gridX = snapshot->doorMapX - snapshot->gridOriginX;
    int gridY = snapshot->doorMapY - snapshot->gridOriginY;
    int index = gridY * DIORAMA_GRID_WIDTH + gridX;
    float front;

    sDoor.on = false;
    if (snapshot->doorLevel == 0 || snapshot->doorWidth == 0 || !DioramaBuildings_IsAvailable()
     || gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT
     || index >= snapshot->visibleCellCount)
        return;
    cell = &snapshot->cells[index];
    if (cell->mapX != snapshot->doorMapX || cell->mapY != snapshot->doorMapY
     || !(cell->flags & DIORAMA_CELL_SOURCE_VALID)
     || !DioramaBuildings_CellAt(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY,
                                 NULL, NULL, NULL))
        return;
    /* the 3DS's VoxelBuildings_DoorWall: across the middle of the cell, half a tile up */
    if (!DioramaBuildings_FrontAt(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY,
                                  cell->sourceMapX + 0.49f, cell->sourceMapX + 0.51f, 0.5f, &front))
        return;
    sDoor.on = true;
    sDoor.west = snapshot->doorMapX - 0.5f;
    sDoor.east = sDoor.west + snapshot->doorWidth;
    sDoor.north = snapshot->doorMapY - 0.5f;
    /* the layout's tiles to the scene's: as ModelPush, cell y spanning y..y+1 */
    sDoor.wall = front + (float)(cell->mapY - cell->sourceMapY) - 0.5f;
    sDoor.ground = resolvedCells[index].groundHeight;
    sDoor.open = snapshot->doorLevel >= 3 ? 1.0f : snapshot->doorLevel / 3.0f;
}

static void DrawDoorOpening(void)
{
    float middle = (sDoor.west + sDoor.east) * 0.5f;
    float half = (sDoor.east - sDoor.west) * 0.5f * sDoor.open;
    float z = -(sDoor.wall + DOOR_DEPTH);
    float top = sDoor.ground + DOOR_HEIGHT;
    const struct ObjectVertex vertices[6] = {
        {middle - half, sDoor.ground, z, 0, 0},
        {middle + half, sDoor.ground, z, 1, 0},
        {middle + half, top, z, 1, 1},
        {middle - half, sDoor.ground, z, 0, 0},
        {middle + half, top, z, 1, 1},
        {middle - half, top, z, 0, 1},
    };

    UploadAndDraw(vertices);
}

/* How lit a walker is: 1 away from the doorway, darker as it comes up to it,
 * 0 through it (not drawn, nor its shadow or reflection). */
static float DoorDusk(struct DioramaSpritePose pose)
{
    float south = -pose.z;

    if (!sDoor.on || pose.x < sDoor.west || pose.x >= sDoor.east
     || south < sDoor.north || south >= sDoor.wall + DOOR_DEPTH + DOOR_DUSK)
        return 1.0f;
    if (south <= sDoor.wall + DOOR_DEPTH)
        return 0.0f;
    return DOOR_DARK + (1.0f - DOOR_DARK) * (south - sDoor.wall - DOOR_DEPTH) / DOOR_DUSK;
}

/*
 * An affine sprite (a follower shrinking into or growing out of its Poke Ball)
 * on its card: the GBA's matrix maps screen to texture about the sprite's
 * centre (texture = M x screen, 8.8), so each corner of the picture lands at
 * M^-1 x its texture offset from the centre, along the card's right and up.
 * V1 in KNOWN_ISSUES: these were refused, and the view cut to 2D for them.
 */
static void AffineCard(const struct ObjectRenderItem *item, struct DioramaSpritePose pose,
                       float pitchSin, float pitchCos, struct ObjectVertex vertices[6])
{
    static const float sCorners[4][4] = { /* texture offset (halves), uv */
        {-1.0f, 1.0f, 0, 1}, {1.0f, 1.0f, 1, 1}, {1.0f, -1.0f, 1, 0}, {-1.0f, -1.0f, 0, 0},
    };
    const int16_t *m = item->object.affineMatrix;
    float a = m[0] / 256.0f, b = m[1] / 256.0f, c = m[2] / 256.0f, d = m[3] / 256.0f;
    float det = a * d - b * c;
    float height = item->height / 16.0f;
    float centreY = pose.y + height * 0.5f * pitchCos;
    float centreZ = pose.z + height * 0.5f * pitchSin;
    struct ObjectVertex corner[4];
    int i;

    for (i = 0; i < 4; i++)
    {
        float tx = sCorners[i][0] * item->width * 0.5f;
        float ty = sCorners[i][1] * item->height * 0.5f;
        float sx = (d * tx - b * ty) / det; /* screen pixels from the centre, y down */
        float sy = (-c * tx + a * ty) / det;
        float up = -sy / 16.0f;

        corner[i].x = pose.x + sx / 16.0f;
        corner[i].y = centreY + up * pitchCos;
        corner[i].z = centreZ + up * pitchSin;
        corner[i].u = sCorners[i][2];
        corner[i].v = sCorners[i][3];
    }
    vertices[0] = corner[0];
    vertices[1] = corner[1];
    vertices[2] = corner[2];
    vertices[3] = corner[0];
    vertices[4] = corner[2];
    vertices[5] = corner[3];
}

static void DrawBillboard(const struct ObjectRenderItem *item, struct DioramaSpritePose pose,
                          float pitchSin, float pitchCos)
{
    float halfWidth = item->width / 32.0f;
    float height = item->height / 16.0f;
    float topY = pose.y + height * pitchCos;
    float topZ = pose.z + height * pitchSin;
    struct ObjectVertex vertices[6] = {
        {pose.x - halfWidth, pose.y, pose.z, 0, 1},
        {pose.x + halfWidth, pose.y, pose.z, 1, 1},
        {pose.x + halfWidth, topY, topZ, 1, 0},
        {pose.x - halfWidth, pose.y, pose.z, 0, 1},
        {pose.x + halfWidth, topY, topZ, 1, 0},
        {pose.x - halfWidth, topY, topZ, 0, 0},
    };

    if (item->object.affineMode != 0)
        AffineCard(item, pose, pitchSin, pitchCos, vertices);
    glBindTexture(GL_TEXTURE_2D, item->texture);
    dglUniform1f(sDepthBiasLocation, item->depthBias);
    dglUniform1f(sUprightZLocation, pose.z - ModelPush(pose, halfWidth));
    dglUniform3f(sLightProbeLocation, pose.x, pose.groundY + 0.75f, pose.z);
    dglUniform1f(sShadeLocation, DoorDusk(pose));
    UploadAndDraw(vertices);
}

static void DrawReflection(const struct ObjectRenderItem *item,
                           struct DioramaSpritePose pose,
                           float pitchSin, float pitchCos)
{
    float halfWidth = item->width / 32.0f;
    float height = item->height / 16.0f;
    float bottomY = pose.y - height * pitchCos;
    float bottomZ = pose.z - height * pitchSin;
    const struct ObjectVertex vertices[6] = {
        {pose.x - halfWidth, pose.y, pose.z, 0, 0},
        {pose.x + halfWidth, pose.y, pose.z, 1, 0},
        {pose.x + halfWidth, bottomY, bottomZ, 1, 1},
        {pose.x - halfWidth, pose.y, pose.z, 0, 0},
        {pose.x + halfWidth, bottomY, bottomZ, 1, 1},
        {pose.x - halfWidth, bottomY, bottomZ, 0, 1},
    };

    glBindTexture(GL_TEXTURE_2D, item->reflectionTexture);
    UploadAndDraw(vertices);
}

/* Who is shown through what hides them: the player, their follower and the
 * other players (multiplayer.h's remote objects, local ids 241-243). */
static bool HasSilhouette(const struct DioramaObjectSnapshot *object)
{
    return (object->flags & DIORAMA_OBJECT_PLAYER) != 0
        || object->localId == 0xFE /* OBJ_EVENT_ID_FOLLOWER */
        || (object->localId >= 241 && object->localId < 244);
}

void DioramaGLObjects_SetSilhouettes(bool on)
{
    sSilhouettes = on;
}

void DioramaGLObjects_Draw(float frameAlpha, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection)
{
    struct DioramaSpritePose poses[OBJECT_RENDER_ITEM_CAPACITY];
    uint8_t order[OBJECT_RENDER_ITEM_CAPACITY];
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    bool sunLit;
    float sunStrength;
    int i;

    if (!sReady)
        return;
    if (frameAlpha < 0.0f) frameAlpha = 0.0f;
    if (frameAlpha > 1.0f) frameAlpha = 1.0f;
    for (i = 0; i < sItemCount; i++)
    {
        poses[i] = sItems[i].interpolate
            ? DioramaSprite_InterpolatePose(sItems[i].previousPose, sItems[i].currentPose, frameAlpha)
            : sItems[i].currentPose;
        if (sItems[i].attachedToPlayer)
        {
            float offsetX = sItems[i].previousAttachedOffsetX
                          + (sItems[i].attachedOffsetX - sItems[i].previousAttachedOffsetX)
                          * frameAlpha;
            float offsetY = sItems[i].previousAttachedOffsetY
                          + (sItems[i].attachedOffsetY - sItems[i].previousAttachedOffsetY)
                          * frameAlpha;

            poses[i] = DioramaSprite_ApplyScreenOffset(poses[i], offsetX, offsetY,
                                                       cameraPitch);
        }
        order[i] = i;
    }
    for (i = 1; i < sItemCount; i++)
    {
        uint8_t value = order[i];
        int position = i;

        while (position > 0)
        {
            const struct ObjectRenderItem *left = &sItems[order[position - 1]];
            const struct ObjectRenderItem *right = &sItems[value];
            if (DioramaSprite_CompareDepth(&poses[order[position - 1]], left->object.priority,
                     left->object.subpriority, left->object.oamOrder, &poses[value],
                     right->object.priority, right->object.subpriority,
                     right->object.oamOrder, cameraZ, cameraPitch) <= 0)
                break;
            order[position] = order[position - 1];
            position--;
        }
        order[position] = value;
    }

    sMetrics.drawCalls = 0;
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
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
    dglUniform1i(sShadowMapLocation, DIORAMA_SHADOW_TEXTURE_UNIT);
    sunLit = DioramaGLShadow_BindForLighting();
    dglUniform1i(sShadowEnabledLocation, sunLit);
    dglUniform1f(sShadowAmbientLocation, DioramaGLShadow_Ambient());
    dglUniformMatrix4fv(sLightMatrixLocation, 1, GL_FALSE, DioramaGLShadow_LightMatrix());
    dglUniform1f(sSaturationLocation, DioramaGLGrade_Saturation());
    DioramaGLLight_Apply(&sLightUniforms, cameraPitch);
    DioramaGLLamps_Apply(&sLampUniforms);
    dglUniform1f(sAnimationTimeLocation, (float)fmod(
        (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(), 120.0));
    dglActiveTexture(GL_TEXTURE0);
    dglUniform1f(sDepthBiasLocation, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    dglUniform1f(sShadeLocation, 1.0f);
    dglUniform1i(sDrawModeLocation, 2);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0x01);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];

        if (sItems[index].reflectionTexture != 0 && sItems[index].object.affineMode == 0
         && DoorDusk(poses[index]) > 0.0f)
        {
            struct DioramaSpritePose reflectionPose = DioramaSprite_ApplyScreenOffset(
                poses[index], 0, sItems[index].object.reflectionOffsetY, cameraPitch);

            /* lit as the walker it mirrors */
            dglUniform3f(sLightProbeLocation, poses[index].x, poses[index].groundY + 0.75f,
                         poses[index].z);
            DrawReflection(&sItems[index], reflectionPose, pitchSin, pitchCos);
        }
    }
    glDisable(GL_STENCIL_TEST);
    sunStrength = sunLit ? (1.0f - DioramaGLShadow_Ambient()) / (1.0f - DIORAMA_SHADOW_AMBIENT) : 0.0f;
    dglUniform1f(sCastStrengthLocation, CAST_SHADOW_ALPHA * (sunStrength > 1.0f ? 1.0f : sunStrength));
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];
        if ((sItems[index].object.flags & DIORAMA_OBJECT_SHADOW)
         && !sItems[index].attachedToPlayer && DoorDusk(poses[index]) > 0.0f)
        {
            if (sunStrength > 0.0f && sItems[index].object.affineMode == 0)
            {
                dglUniform1i(sDrawModeLocation, 4);
                DrawCastShadow(&sItems[index], poses[index]);
            }
            else
            {
                dglUniform1i(sDrawModeLocation, 1);
                DrawShadow(&sItems[index], poses[index]);
            }
        }
    }

    /* Before the walkers themselves: a player whom the world hides (a house
     * south of them, a tree's crown) as a see-through ghost, where only the
     * world - not another walker - stands in front of them. */
    if (sSilhouettes)
    {
        glDepthFunc(GL_GREATER);
        dglUniform1i(sDrawModeLocation, 5);
        dglUniform1f(sShadeLocation, 1.0f);
        for (i = 0; i < sItemCount; i++)
        {
            int index = order[i];

            if (HasSilhouette(&sItems[index].object) && DoorDusk(poses[index]) > 0.0f)
                DrawBillboard(&sItems[index], poses[index], pitchSin, pitchCos);
        }
        glDepthFunc(GL_LEQUAL);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (sDoor.on)
    {
        dglUniform1i(sDrawModeLocation, 3);
        DrawDoorOpening();
    }
    dglUniform1i(sDrawModeLocation, 0);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];

        if (DoorDusk(poses[index]) > 0.0f)
            DrawBillboard(&sItems[index], poses[index], pitchSin, pitchCos);
    }
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 2, 0x02);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDisable(GL_DEPTH_TEST);
    for (i = 0; i < sItemCount; i++)
    {
        int index = order[i];

        if (sItems[index].drawOverPixelObjects && DoorDusk(poses[index]) > 0.0f)
            DrawBillboard(&sItems[index], poses[index], pitchSin, pitchCos);
    }
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

float DioramaGLObjects_GetPlayerGround(void)
{
    return sPlayerGroundY;
}

bool DioramaGLObjects_GetPlayerPose(float frameAlpha, float *x, float *groundY, float *z)
{
    struct DioramaSpritePose pose;

    if (sPlayerItem < 0 || sPlayerItem >= sItemCount)
        return false;
    if (frameAlpha < 0.0f) frameAlpha = 0.0f;
    if (frameAlpha > 1.0f) frameAlpha = 1.0f;
    pose = sItems[sPlayerItem].interpolate
         ? DioramaSprite_InterpolatePose(sItems[sPlayerItem].previousPose,
                                         sItems[sPlayerItem].currentPose, frameAlpha)
         : sItems[sPlayerItem].currentPose;
    *x = pose.x;
    *groundY = pose.groundY;
    *z = pose.z;
    return true;
}

const struct DioramaObjectMetrics *DioramaGLObjects_GetMetrics(void)
{
    return &sMetrics;
}

void DioramaGLObjects_Shutdown(void)
{
    ClearTextureCache();
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sVertexBuffer = 0;
    sVertexArray = 0;
    sProgram = 0;
    sReady = false;
}

#endif
