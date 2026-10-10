#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_air.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_object_renderer.h"
#include "diorama/gl_quality.h"
#include "diorama/gl_target.h"

/* The 3DS project's sun (voxel_lighting.h): in the northwest, towards it
 * (-DX, 1, -DZ) with z pointing south; the wind blows the way shadows fall. */
#define SUN_DX 0.85f
#define SUN_DZ 0.55f

/*
 * Sunlit dust (ctr_voxel.c, DrawMotes). The 3DS fills 22 x 18 tiles around
 * its close camera's target with 32 specks, so they always enter off screen.
 * This view can see past the scene's 33 x 33 cells into the dark, where dust
 * would read as stars: the box is the scene's own (16 tiles each way of the
 * target), with as many specks to the tile, and a speck fades out over the
 * last tiles before the box's edge instead of popping there.
 */
#define MOTES 83
#define MOTE_VERTICES 12
#define MOTE_HALF_X 16.0f /* tiles each side of the target */
#define MOTE_HALF_Z 16.0f
#define MOTE_EDGE 3.0f    /* tiles over which a speck fades at the box's edge */
#define MOTE_LOW 0.3f     /* tiles above the ground under the player */
#define MOTE_HIGH 2.6f
#define MOTE_SIZE 0.10f   /* tiles from the middle to a corner */
#define MOTE_WIND 0.35f   /* tiles per second */

/* Sun rays (DrawRays, MakeRays): sizes in pixels of a picture 240 high, the
 * 3DS's, as wide as this one's aspect makes it. */
#define RAY_HEIGHT 240.0f

/*
 * The game's fog in the scene instead of over it (MakeFog, DrawFogBanks): two
 * banks of drifting mist lying over the ground, one about the player's knees
 * and one at chest height, each a horizontal sheet around the camera textured
 * with soft tileable noise, blended in depth tested but never written - a
 * wall or a tree parts them, a walker stands in them - and thinning into the
 * distance haze like the ground under them. The flat fog overlay is dropped
 * while they are up (DioramaGLAir_DrawsFog).
 */
#define FOG_DIM 128
#define FOG_GRID 8      /* cells along each side of a sheet */
#define FOG_REACH 32.0f /* tiles from the sheet's centre to its side */
#define FOG_FADE 0.55f  /* ... share of that reach at which it starts to thin */
/* This camera can see past the scene's 33 x 33 cells, where a bank would hang
 * over the dark: the banks also thin out over the last tiles before the
 * cells' edge (16 each way of the camera target), as the dust does. */
#define FOG_SCENE_HALF 16.0f
#define FOG_SCENE_EDGE 3.0f
#define FOG_SHEET_VERTICES (FOG_GRID * FOG_GRID * 6)

static const struct
{
    float period;         /* tiles the pattern covers before it repeats */
    float height;         /* tiles above the player's ground */
    float driftX, driftZ; /* tiles per second (x east, z south) */
    float strength;       /* the most the bank covers, at full fog */
} sFogBanks[2] = {
    {12.0f, 0.30f, -0.55f, 0.00f, 0.58f},
    {7.0f, 1.10f, -0.85f, -0.25f, 0.34f},
};

/* The dark of a cave in its fog (MakeGloom, CtrVoxel_Gloom): black rising
 * from nothing round the player to its most at the ring's rim, 520 pixels
 * across the 3DS's 240-high view. */
#define GLOOM_INNER 0.30f
#define GLOOM_RADIUS 1.0833f /* x the picture's height */
#define GLOOM_MAX 0.62f

static bool sTried;
static bool sReady;
static GLuint sMoteProgram;
static GLuint sScreenProgram;
static GLuint sMoteArray;
static GLuint sMoteBuffer;
static GLuint sQuadArray;
static GLuint sQuadBuffer;
static GLint sMoteCameraLocation;
static GLint sMotePitchLocation;
static GLint sMoteFocalLocation;
static GLint sMoteAspectLocation;
static GLint sMoteTintLocation;
static GLint sScreenModeLocation;
static GLint sScreenImageLocation;
static GLint sScreenViewLocation;
static GLint sScreenDriftLocation;
static GLint sScreenTintLocation;
static GLfloat sMoteVertices[MOTES * MOTE_VERTICES * 4];
static GLuint sFogProgram;
static GLuint sFogArray;
static GLuint sFogBuffer;
static GLuint sFogTexture;
static GLint sFogCameraLocation;
static GLint sFogPitchLocation;
static GLint sFogFocalLocation;
static GLint sFogAspectLocation;
static GLint sFogCentreLocation;
static GLint sFogPeriodLocation;
static GLint sFogColourLocation;
static GLint sFogHazeLocation;
static GLint sFogHazeColourLocation;
static GLint sFogMapLocation;
static GLint sFogSceneBoxLocation;
static GLint sScreenCentreLocation;
static bool sDrewFog;

/* Bloom: two quarter-size targets, the copy and what is left over the threshold. */
static GLuint sBloomTextures[2];
static GLuint sBloomFramebuffers[2];
static int sBloomWidth;
static int sBloomHeight;
static bool sBloomReady;

static const char sMoteVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 vertex;\n" /* x, y, z, glint */
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "out float glint;\n"
    "void main() {\n"
    /* The scene's projection (gl_object_renderer.c), so the depth test agrees. */
    "  vec3 position = vertex.xyz;\n"
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
    "  glint = vertex.w;\n"
    "}\n";

static const char sMoteFragmentShader[] =
    "#version 330 core\n"
    "in float glint;\n"
    "out vec4 color;\n"
    "uniform vec4 tint;\n" /* colour, strength */
    "void main() { color = vec4(tint.rgb, clamp(glint * tint.a, 0.0, 1.0)); }\n";

static const char sFogVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec3 sheet;\n" /* x east, z south from the centre; the rim fade */
    "uniform vec2 cameraPosition;\n"
    "uniform float cameraPitch;\n"
    "uniform float focalLength;\n"
    "uniform float aspectCorrection;\n"
    "uniform vec3 centre;\n" /* the sheet's centre: x east, height, z south */
    "uniform float period;\n"
    "out vec2 uv;\n"
    "out float edge;\n"
    "out float viewDepth;\n"
    "out vec2 fromTarget;\n"
    "void main() {\n"
    "  vec3 position = vec3(centre.x + sheet.x, centre.y, -(centre.z + sheet.y));\n"
    "  fromTarget = position.xz - cameraPosition;\n"
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
    "  viewDepth = depth;\n"
    "  uv = sheet.xy / period;\n"
    "  edge = sheet.z;\n"
    "}\n";

static const char sFogFragmentShader[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "in float edge;\n"
    "in float viewDepth;\n"
    "in vec2 fromTarget;\n"
    "out vec4 color;\n"
    "uniform sampler2D fogMap;\n"
    "uniform vec2 sceneBox;\n" /* half its side, the fade's width (tiles) */
    "uniform vec4 colour;\n" /* the fog's colour, the bank's strength */
    "uniform vec3 haze;\n"   /* gl_light.h: start, per unit of depth, most */
    "uniform vec3 hazeColor;\n"
    "void main() {\n"
    "  vec2 inside = clamp((sceneBox.x - abs(fromTarget)) / sceneBox.y, 0.0, 1.0);\n"
    "  inside = inside * inside * (3.0 - 2.0 * inside);\n"
    "  float coverage = texture(fogMap, uv).r * edge * colour.a * inside.x * inside.y;\n"
    "  if (coverage <= 0.002) discard;\n"
    "  float w = clamp((viewDepth - haze.x) * haze.y, 0.0, haze.z);\n"
    "  color = vec4(mix(colour.rgb, hazeColor, w), coverage);\n"
    "}\n";

static const char sScreenVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 corner;\n"
    "out vec2 screen;\n" /* 0-1 each way, y up */
    /* At the far end of the depth range, for the rays' depth test. */
    "void main() { screen = corner; gl_Position = vec4(corner * 2.0 - 1.0, 0.9998, 1.0); }\n";

static const char sScreenFragmentShader[] =
    "#version 330 core\n"
    "in vec2 screen;\n"
    "out vec4 color;\n"
    "uniform int mode;\n" /* 0 the rays, 1 the bloom's threshold, 2 the bloom added */
    "uniform sampler2D image;\n"
    "uniform vec2 viewSize;\n" /* the picture in the 3DS's pixels */
    "uniform float drift;\n"
    "uniform vec4 tint;\n"
    "uniform vec3 centre;\n" /* the gloom's: x, y (0-1), radius in pixels */
    "void main() {\n"
    "  if (mode == 0) {\n"
    /* RayStrength: strongest in the top left corner, gone 0.65 of the
     * diagonal away; the shafts are the dapple pattern's shaded parts,
     * turned 35 degrees off vertical and stretched almost to lines. */
    "    vec2 px = screen * viewSize;\n"
    "    float s = 1.0 - length(vec2(px.x, viewSize.y - px.y)) / (length(viewSize) * 0.65);\n"
    "    if (s <= 0.0) discard;\n"
    "    const vec2 across = vec2(0.819152, 0.573576);\n"
    "    const vec2 along = vec2(0.573576, -0.819152);\n"
    "    vec2 uv = vec2(dot(across, px) / 260.0 + drift, dot(along, px) / 4000.0);\n"
    "    color = vec4(tint.rgb, (1.0 - texture(image, uv).r) * s * s * tint.a);\n"
    "  } else if (mode == 3) {\n"
    /* The dark of a cave: nothing round the player, its most at the rim. */
    "    float t = (length((screen - centre.xy) * viewSize) / centre.z - 0.30) / 0.70;\n"
    "    t = clamp(t, 0.0, 1.0);\n"
    "    color = vec4(0.0, 0.0, 0.0, t * t * (3.0 - 2.0 * t) * tint.a);\n"
    "  } else if (mode == 1) {\n"
    "    color = vec4(max(texture(image, screen).rgb - 0.85, 0.0) * 4.0, 1.0);\n"
    "  } else {\n"
    "    color = vec4(texture(image, screen).rgb, tint.a);\n"
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
        SDL_Log("Diorama air shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkProgram(const char *vertexSource, const char *fragmentSource)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    GLuint program;
    GLint linked;

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
            dglDeleteShader(vertexShader);
        if (fragmentShader != 0)
            dglDeleteShader(fragmentShader);
        return 0;
    }
    program = dglCreateProgram();
    dglAttachShader(program, vertexShader);
    dglAttachShader(program, fragmentShader);
    dglLinkProgram(program);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[1024];

        dglGetProgramInfoLog(program, sizeof(log), NULL, log);
        SDL_Log("Diorama air shader link failed: %s", log);
        dglDeleteProgram(program);
        return 0;
    }
    return program;
}

/*
 * MakeFog: wisps rather than a wash - four octaves of the dapples' noise,
 * thresholded so that clear gaps open between the banks - and the sheets: a
 * grid around their own centre, the coverage falling to nothing towards the
 * rim so a sheet has no edge to see. Not fatal: without them the game's own
 * flat fog stays.
 */
static bool MakeFog(void)
{
    static const struct
    {
        int period;
        float weight;
    } octaves[] = {{4, 1.0f}, {8, 0.55f}, {16, 0.30f}, {32, 0.12f}};
    static const unsigned corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
    static uint8_t texels[FOG_DIM * FOG_DIM];
    static GLfloat sheet[FOG_SHEET_VERTICES * 3];
    float cell = 2.0f * FOG_REACH / FOG_GRID;
    unsigned x;
    unsigned y;
    unsigned o;
    unsigned n = 0;

    for (y = 0; y < FOG_DIM; y++)
        for (x = 0; x < FOG_DIM; x++)
        {
            float noise = 0.0f;
            float total = 0.0f;
            float t;

            for (o = 0; o < sizeof(octaves) / sizeof(octaves[0]); o++)
            {
                float scale = (float)octaves[o].period / FOG_DIM;

                noise += octaves[o].weight * DioramaGLLight_Noise((float)x * scale, (float)y * scale,
                                                                  octaves[o].period, o + 11);
                total += octaves[o].weight;
            }
            t = (noise / total - 0.32f) / 0.40f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            texels[y * FOG_DIM + x] = (uint8_t)(t * t * (3.0f - 2.0f * t) * 255.0f + 0.5f);
        }
    glGenTextures(1, &sFogTexture);
    if (sFogTexture == 0)
        return false;
    glBindTexture(GL_TEXTURE_2D, sFogTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FOG_DIM, FOG_DIM, 0, GL_RED, GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (y = 0; y < FOG_GRID; y++)
        for (x = 0; x < FOG_GRID; x++)
            for (o = 0; o < 6; o++)
            {
                float sx = -FOG_REACH + (float)(x + corners[o][0]) * cell;
                float sz = -FOG_REACH + (float)(y + corners[o][1]) * cell;
                float r = sqrtf(sx * sx + sz * sz) / FOG_REACH;
                float edge = (1.0f - r) / (1.0f - FOG_FADE);

                edge = edge < 0.0f ? 0.0f : edge > 1.0f ? 1.0f : edge;
                sheet[n++] = sx;
                sheet[n++] = sz;
                sheet[n++] = edge * edge * (3.0f - 2.0f * edge);
            }
    dglGenVertexArrays(1, &sFogArray);
    dglGenBuffers(1, &sFogBuffer);
    dglBindVertexArray(sFogArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sFogBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(sheet), sheet, GL_STATIC_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (const void *)0);
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

/* Not fatal: without it there is no light in the air. */
static void Init(void)
{
    static const GLfloat quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};

    sTried = true;
    sMoteProgram = LinkProgram(sMoteVertexShader, sMoteFragmentShader);
    sScreenProgram = LinkProgram(sScreenVertexShader, sScreenFragmentShader);
    sFogProgram = LinkProgram(sFogVertexShader, sFogFragmentShader);
    if (sMoteProgram == 0 || sScreenProgram == 0 || sFogProgram == 0)
        return;
    sFogCameraLocation = dglGetUniformLocation(sFogProgram, "cameraPosition");
    sFogPitchLocation = dglGetUniformLocation(sFogProgram, "cameraPitch");
    sFogFocalLocation = dglGetUniformLocation(sFogProgram, "focalLength");
    sFogAspectLocation = dglGetUniformLocation(sFogProgram, "aspectCorrection");
    sFogCentreLocation = dglGetUniformLocation(sFogProgram, "centre");
    sFogPeriodLocation = dglGetUniformLocation(sFogProgram, "period");
    sFogColourLocation = dglGetUniformLocation(sFogProgram, "colour");
    sFogHazeLocation = dglGetUniformLocation(sFogProgram, "haze");
    sFogHazeColourLocation = dglGetUniformLocation(sFogProgram, "hazeColor");
    sFogMapLocation = dglGetUniformLocation(sFogProgram, "fogMap");
    sFogSceneBoxLocation = dglGetUniformLocation(sFogProgram, "sceneBox");
    sScreenCentreLocation = dglGetUniformLocation(sScreenProgram, "centre");
    if (sFogCameraLocation < 0 || sFogPitchLocation < 0 || sFogFocalLocation < 0
     || sFogAspectLocation < 0 || sFogCentreLocation < 0 || sFogPeriodLocation < 0
     || sFogColourLocation < 0 || sFogHazeLocation < 0 || sFogHazeColourLocation < 0
     || sFogMapLocation < 0 || sFogSceneBoxLocation < 0 || sScreenCentreLocation < 0
     || !MakeFog())
    {
        SDL_Log("Diorama air: fog banks unavailable");
        return;
    }
    sMoteCameraLocation = dglGetUniformLocation(sMoteProgram, "cameraPosition");
    sMotePitchLocation = dglGetUniformLocation(sMoteProgram, "cameraPitch");
    sMoteFocalLocation = dglGetUniformLocation(sMoteProgram, "focalLength");
    sMoteAspectLocation = dglGetUniformLocation(sMoteProgram, "aspectCorrection");
    sMoteTintLocation = dglGetUniformLocation(sMoteProgram, "tint");
    sScreenModeLocation = dglGetUniformLocation(sScreenProgram, "mode");
    sScreenImageLocation = dglGetUniformLocation(sScreenProgram, "image");
    sScreenViewLocation = dglGetUniformLocation(sScreenProgram, "viewSize");
    sScreenDriftLocation = dglGetUniformLocation(sScreenProgram, "drift");
    sScreenTintLocation = dglGetUniformLocation(sScreenProgram, "tint");
    if (sMoteCameraLocation < 0 || sMotePitchLocation < 0 || sMoteFocalLocation < 0
     || sMoteAspectLocation < 0 || sMoteTintLocation < 0 || sScreenModeLocation < 0
     || sScreenImageLocation < 0 || sScreenViewLocation < 0 || sScreenDriftLocation < 0
     || sScreenTintLocation < 0)
    {
        SDL_Log("Diorama air: shader uniforms missing");
        return;
    }

    dglGenVertexArrays(1, &sMoteArray);
    dglGenBuffers(1, &sMoteBuffer);
    dglBindVertexArray(sMoteArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sMoteBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(sMoteVertices), NULL, GL_STREAM_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)0);

    dglGenVertexArrays(1, &sQuadArray);
    dglGenBuffers(1, &sQuadBuffer);
    dglBindVertexArray(sQuadArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sQuadBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void *)0);
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    sReady = true;
}

/* MoteRandom: a fixed pseudo-random number in 0..1 for mote i. */
static float MoteRandom(unsigned i, unsigned salt)
{
    uint32_t h = i * 2654435761u ^ salt * 2246822519u;

    h = (h ^ (h >> 15)) * 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFu) / 65535.0f;
}

static float Wrap(float value, float size)
{
    return value - floorf(value / size) * size;
}

/* 0 at the box's edge, 1 from MOTE_EDGE tiles inside it, smoothly. */
static float EdgeFade(float inside)
{
    float t = inside / MOTE_EDGE;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static GLfloat *MoteVertex(GLfloat *v, float x, float y, float z, float glint)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = glint;
    return v + 4;
}

/*
 * Each speck a small soft diamond facing the camera, bright in the middle and
 * gone at its corners: a fan of four triangles. They live in the anchored
 * world: one stays where it is as the camera passes, and the box around the
 * target they fill is wrapped, so as many are always in view.
 */
static void DrawMotes(const struct DioramaAirLight *air, float cameraX, float cameraZ,
                      float cameraPitch, float focalLength, float aspectCorrection)
{
    float seconds = (float)fmod(SDL_GetTicks() / 1000.0, 3600.0);
    float length = sqrtf(SUN_DX * SUN_DX + SUN_DZ * SUN_DZ);
    float windX = SUN_DX / length * MOTE_WIND;
    float windZ = SUN_DZ / length * MOTE_WIND; /* south */
    /* The camera's right and up in the scene (x east, y up, z north). */
    float upY = cosf(cameraPitch) * MOTE_SIZE;
    float upZ = sinf(cameraPitch) * MOTE_SIZE;
    float right = MOTE_SIZE;
    /* The box's centre, in the 3DS's axes (z south). */
    float originX = floorf(cameraX);
    float originZ = floorf(-cameraZ);
    float ground = DioramaGLObjects_GetPlayerGround();
    float r = (float)(air->fadeScale[0] < 0.0f ? 0.0f : air->fadeScale[0]);
    float g = (float)(air->fadeScale[1] < 0.0f ? 0.0f : air->fadeScale[1]);
    float b = (float)(air->fadeScale[2] < 0.0f ? 0.0f : air->fadeScale[2]);
    GLfloat *v = sMoteVertices;
    unsigned i;

    for (i = 0; i < MOTES; i++)
    {
        float phase = MoteRandom(i, 1) * 6.2832f;
        float speed = 0.6f + 0.8f * MoteRandom(i, 3);
        /* Where it has drifted to in the anchored world, then brought into
         * the box around the target. */
        float wx = MoteRandom(i, 2) * MOTE_HALF_X * 8.0f + seconds * windX * speed;
        float wz = MoteRandom(i, 4) * MOTE_HALF_Z * 8.0f + seconds * windZ * speed;
        float boxX = Wrap(wx - (originX + air->anchorX) + MOTE_HALF_X, 2.0f * MOTE_HALF_X)
                   - MOTE_HALF_X;
        float boxZ = Wrap(wz - (originZ + air->anchorZ) + MOTE_HALF_Z, 2.0f * MOTE_HALF_Z)
                   - MOTE_HALF_Z;
        float x = originX + boxX;
        float z = -(originZ + boxZ);
        float y = ground + MOTE_LOW + (MOTE_HIGH - MOTE_LOW) * MoteRandom(i, 5)
                + 0.15f * sinf(seconds * 0.7f + phase);
        float glint = 0.55f + 0.45f * sinf(seconds * (1.1f + MoteRandom(i, 6)) + phase * 2.0f);

        glint *= EdgeFade(MOTE_HALF_X - fabsf(boxX)) * EdgeFade(MOTE_HALF_Z - fabsf(boxZ));

        v = MoteVertex(v, x, y, z, glint);
        v = MoteVertex(v, x, y + upY, z + upZ, 0.0f);
        v = MoteVertex(v, x + right, y, z, 0.0f);
        v = MoteVertex(v, x, y, z, glint);
        v = MoteVertex(v, x + right, y, z, 0.0f);
        v = MoteVertex(v, x, y - upY, z - upZ, 0.0f);
        v = MoteVertex(v, x, y, z, glint);
        v = MoteVertex(v, x, y - upY, z - upZ, 0.0f);
        v = MoteVertex(v, x - right, y, z, 0.0f);
        v = MoteVertex(v, x, y, z, glint);
        v = MoteVertex(v, x - right, y, z, 0.0f);
        v = MoteVertex(v, x, y + upY, z + upZ, 0.0f);
    }

    dglUseProgram(sMoteProgram);
    dglUniform2f(sMoteCameraLocation, cameraX, cameraZ);
    dglUniform1f(sMotePitchLocation, cameraPitch);
    dglUniform1f(sMoteFocalLocation, focalLength);
    dglUniform1f(sMoteAspectLocation, aspectCorrection);
    /* Warm white (0xFFD8F4FF), as the palette's fade and tint have the scene. */
    dglUniform4f(sMoteTintLocation, 1.0f * r, (0xF4 / 255.0f) * g, (0xD8 / 255.0f) * b, air->motes);
    dglBindVertexArray(sMoteArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sMoteBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(sMoteVertices), sMoteVertices, GL_STREAM_DRAW);
    /* Hidden behind a roof, never hiding anything: tested, not written. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, MOTES * MOTE_VERTICES);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

static void DrawRays(const struct DioramaAirLight *air, int outputWidth, int outputHeight)
{
    double seconds = SDL_GetTicks() / 1000.0;
    float r = air->fadeScale[0] < 0.0f ? 0.0f : air->fadeScale[0];
    float g = air->fadeScale[1] < 0.0f ? 0.0f : air->fadeScale[1];
    float b = air->fadeScale[2] < 0.0f ? 0.0f : air->fadeScale[2];

    dglUseProgram(sScreenProgram);
    dglUniform1i(sScreenModeLocation, 0);
    dglUniform1i(sScreenImageLocation, 0);
    dglUniform2f(sScreenViewLocation, RAY_HEIGHT * outputWidth / outputHeight, RAY_HEIGHT);
    dglUniform1f(sScreenDriftLocation, (float)fmod(seconds * 0.012, 1.0));
    /* Warm white, the sun's own colour (0xFFD0F0FF). */
    dglUniform4f(sScreenTintLocation, 1.0f * r, (0xF0 / 255.0f) * g, (0xD0 / 255.0f) * b, air->rays);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, air->pattern);
    dglBindVertexArray(sQuadArray);
    /* Only over the scene: where nothing was drawn (the dark past the
     * scene's cells, which the 3DS's close camera never shows) the depth is
     * still the cleared 1, and no light is in the air. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*
 * DrawFogBanks: each sheet centred on the camera target, moved in whole
 * repeats of its pattern plus the drift, so the mist stays put on the ground
 * as the player walks and only the wind moves it. Coloured pale (a cave's
 * fog bluish grey), as the palette's fade and night tint have the scene, and
 * hazed like the ground under it.
 */
static void DrawFogBanks(const struct DioramaAirLight *air, float cameraX, float cameraZ,
                         float cameraPitch, float focalLength, float aspectCorrection)
{
    double seconds = SDL_GetTicks() / 1000.0;
    float ground = DioramaGLObjects_GetPlayerGround();
    /* 0xFFF2EBE6 and a cave's 0xFFBFAB9E (0xAABBGGRR). */
    float base[3] = {0xE6 / 255.0f, 0xEB / 255.0f, 0xF2 / 255.0f};
    float cave[3] = {0x9E / 255.0f, 0xAB / 255.0f, 0xBF / 255.0f};
    float haze[3];
    float hazeColour[3];
    float colour[3];
    float targetZ = -cameraZ; /* south */
    unsigned bank;
    int i;

    DioramaGLLight_GetHaze(cameraPitch, haze, hazeColour);
    for (i = 0; i < 3; i++)
    {
        float c = (air->cave ? cave[i] : base[i]) * air->fadeScale[i] + air->fadeOffset[i];

        colour[i] = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
    }
    dglUseProgram(sFogProgram);
    dglUniform2f(sFogCameraLocation, cameraX, cameraZ);
    dglUniform1f(sFogPitchLocation, cameraPitch);
    dglUniform1f(sFogFocalLocation, focalLength);
    dglUniform1f(sFogAspectLocation, aspectCorrection);
    dglUniform3f(sFogHazeLocation, haze[0], haze[1], haze[2]);
    dglUniform3f(sFogHazeColourLocation, hazeColour[0], hazeColour[1], hazeColour[2]);
    dglUniform1i(sFogMapLocation, 0);
    dglUniform2f(sFogSceneBoxLocation, FOG_SCENE_HALF, FOG_SCENE_EDGE);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sFogTexture);
    dglBindVertexArray(sFogArray);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (bank = 0; bank < 2; bank++)
    {
        float period = sFogBanks[bank].period;
        float dx = (float)fmod(seconds * sFogBanks[bank].driftX, period);
        float dz = (float)fmod(seconds * sFogBanks[bank].driftZ, period);

        dglUniform3f(sFogCentreLocation, floorf(cameraX / period) * period + dx,
                     ground + sFogBanks[bank].height, floorf(targetZ / period) * period + dz);
        dglUniform1f(sFogPeriodLocation, period);
        dglUniform4f(sFogColourLocation, colour[0], colour[1], colour[2],
                     sFogBanks[bank].strength * air->fog);
        glDrawArrays(GL_TRIANGLES, 0, FOG_SHEET_VERTICES);
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* The dark of a cave, centred where the player stands (PlaceGloom: a tile
 * above its ground), in black over the scene. */
static void DrawGloom(const struct DioramaAirLight *air, float cameraPitch, float focalLength,
                      int outputWidth, int outputHeight)
{
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    float distance = (16.0f * pitchCos - 0.458944f) / pitchSin;
    float y = DioramaGLObjects_GetPlayerGround() + 1.0f;
    float depth = (16.0f - y) * pitchSin + distance * pitchCos;
    float vertical = -(16.0f - y) * pitchCos + distance * pitchSin;
    float clipY = -0.04f * depth + vertical * focalLength * 2.0f / 160.0f;
    float centreY = depth > 0.0001f ? 0.5f + 0.5f * clipY / depth : 0.5f;

    dglUseProgram(sScreenProgram);
    dglUniform1i(sScreenModeLocation, 3);
    dglUniform2f(sScreenViewLocation, (float)outputWidth, (float)outputHeight);
    dglUniform3f(sScreenCentreLocation, 0.5f, centreY, GLOOM_RADIUS * outputHeight);
    dglUniform4f(sScreenTintLocation, 0.0f, 0.0f, 0.0f, GLOOM_MAX * air->fog);
    dglBindVertexArray(sQuadArray);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void FreeBloom(void)
{
    if (sBloomFramebuffers[0] != 0)
        dglDeleteFramebuffers(2, sBloomFramebuffers);
    if (sBloomTextures[0] != 0)
        glDeleteTextures(2, sBloomTextures);
    memset(sBloomFramebuffers, 0, sizeof(sBloomFramebuffers));
    memset(sBloomTextures, 0, sizeof(sBloomTextures));
    sBloomWidth = sBloomHeight = 0;
    sBloomReady = false;
}

static bool PrepareBloomTargets(int width, int height)
{
    int i;

    if (sBloomReady && width == sBloomWidth && height == sBloomHeight)
        return true;
    FreeBloom();
    glGenTextures(2, sBloomTextures);
    dglGenFramebuffers(2, sBloomFramebuffers);
    for (i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, sBloomTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        /* The copy is read texel for texel; the bloom is stretched back smoothly. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i == 0 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i == 0 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        dglBindFramebuffer(GL_FRAMEBUFFER, sBloomFramebuffers[i]);
        dglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                sBloomTextures[i], 0);
        if (dglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            FreeBloom();
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBloomWidth = width;
    sBloomHeight = height;
    sBloomReady = true;
    return true;
}

/*
 * The picture drawn so far at a quarter of its size, keeping only what is
 * brighter than the threshold, (colour - 0.85) x 4, then stretched back over
 * it with bilinear filtering and added at the light's bloom strength.
 */
static void DrawBloom(const struct DioramaAirLight *air, int outputWidth, int outputHeight)
{
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    int width = outputWidth / 4;
    int height = outputHeight / 4;

    if (width < 1 || height < 1)
        return;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    if (!PrepareBloomTargets(width, height))
    {
        dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
        dglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFramebuffer);
        return;
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    /* The copy: each quarter pixel a bilinear sample of the picture (resolved
     * first when the scene is multisampled, gl_target.h). */
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, DioramaGLTarget_ReadableFramebuffer((GLuint)drawFramebuffer));
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, sBloomFramebuffers[0]);
    dglBlitFramebuffer(0, 0, outputWidth, outputHeight, 0, 0, width, height,
                       GL_COLOR_BUFFER_BIT, GL_LINEAR);
    /* What is over the threshold. */
    dglBindFramebuffer(GL_FRAMEBUFFER, sBloomFramebuffers[1]);
    glViewport(0, 0, width, height);
    dglUseProgram(sScreenProgram);
    dglUniform1i(sScreenModeLocation, 1);
    dglUniform1i(sScreenImageLocation, 0);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sBloomTextures[0]);
    dglBindVertexArray(sQuadArray);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    /* Added back over the picture. */
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFramebuffer);
    glViewport(0, 0, outputWidth, outputHeight);
    dglUniform1i(sScreenModeLocation, 2);
    dglUniform4f(sScreenTintLocation, 1.0f, 1.0f, 1.0f, air->bloom);
    glBindTexture(GL_TEXTURE_2D, sBloomTextures[1]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static bool Enabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_AIR");

        sEnabled = value == NULL || value[0] != '0';
    }
    /* The player's effects setting (gl_quality.h) as well. */
    return sEnabled && DioramaGLQuality_Effects();
}

void DioramaGLAir_Draw(float cameraX, float cameraZ, float cameraPitch,
                       float focalLength, float aspectCorrection,
                       int outputWidth, int outputHeight)
{
    struct DioramaAirLight air;
    GLboolean scissor;

    sDrewFog = false;
    if (!Enabled())
        return;
    DioramaGLLight_GetAir(&air);
    if (air.motes <= 0.005f && air.rays <= 0.005f && air.bloom <= 0.005f
     && air.fog <= 0.005f)
        return;
    if (!sTried)
        Init();
    if (!sReady || outputWidth <= 0 || outputHeight <= 0)
        return;
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, outputWidth, outputHeight);
    /* The fog's banks lie in the scene, so they go before the light in the
     * air (motes, rays) that is added over everything; then the bloom from
     * all of it, and a cave's dark over the lot. */
    if (air.fog > 0.005f)
    {
        DrawFogBanks(&air, cameraX, cameraZ, cameraPitch, focalLength, aspectCorrection);
        sDrewFog = true;
    }
    if (air.motes > 0.005f)
        DrawMotes(&air, cameraX, cameraZ, cameraPitch, focalLength, aspectCorrection);
    if (air.rays > 0.005f && air.pattern != 0)
        DrawRays(&air, outputWidth, outputHeight);
    if (air.bloom > 0.005f)
        DrawBloom(&air, outputWidth, outputHeight);
    if (air.cave && air.fog > 0.005f)
        DrawGloom(&air, cameraPitch, focalLength, outputWidth, outputHeight);
    dglBindVertexArray(0);
    dglUseProgram(0);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

bool DioramaGLAir_DrawsFog(void)
{
    return sDrewFog;
}

void DioramaGLAir_Shutdown(void)
{
    FreeBloom();
    if (sFogBuffer != 0)
        dglDeleteBuffers(1, &sFogBuffer);
    if (sFogArray != 0)
        dglDeleteVertexArrays(1, &sFogArray);
    if (sFogTexture != 0)
        glDeleteTextures(1, &sFogTexture);
    if (sFogProgram != 0)
        dglDeleteProgram(sFogProgram);
    sFogBuffer = sFogArray = sFogTexture = sFogProgram = 0;
    sDrewFog = false;
    if (sMoteBuffer != 0)
        dglDeleteBuffers(1, &sMoteBuffer);
    if (sQuadBuffer != 0)
        dglDeleteBuffers(1, &sQuadBuffer);
    if (sMoteArray != 0)
        dglDeleteVertexArrays(1, &sMoteArray);
    if (sQuadArray != 0)
        dglDeleteVertexArrays(1, &sQuadArray);
    if (sMoteProgram != 0)
        dglDeleteProgram(sMoteProgram);
    if (sScreenProgram != 0)
        dglDeleteProgram(sScreenProgram);
    sMoteBuffer = sQuadBuffer = sMoteArray = sQuadArray = 0;
    sMoteProgram = sScreenProgram = 0;
    sTried = false;
    sReady = false;
}

#endif // ENABLE_DIORAMA
