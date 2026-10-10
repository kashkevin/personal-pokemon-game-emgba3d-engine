#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_air.h"
#include "diorama/gl_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_object_renderer.h"

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

/* Not fatal: without it there is no light in the air. */
static void Init(void)
{
    static const GLfloat quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};

    sTried = true;
    sMoteProgram = LinkProgram(sMoteVertexShader, sMoteFragmentShader);
    sScreenProgram = LinkProgram(sScreenVertexShader, sScreenFragmentShader);
    if (sMoteProgram == 0 || sScreenProgram == 0)
        return;
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
    /* The copy: each quarter pixel a bilinear sample of the picture. */
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)drawFramebuffer);
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
    return sEnabled;
}

void DioramaGLAir_Draw(float cameraX, float cameraZ, float cameraPitch,
                       float focalLength, float aspectCorrection,
                       int outputWidth, int outputHeight)
{
    struct DioramaAirLight air;
    GLboolean scissor;

    if (!Enabled())
        return;
    DioramaGLLight_GetAir(&air);
    if (air.motes <= 0.005f && air.rays <= 0.005f && air.bloom <= 0.005f)
        return;
    if (!sTried)
        Init();
    if (!sReady || outputWidth <= 0 || outputHeight <= 0)
        return;
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, outputWidth, outputHeight);
    /* The motes in the scene, then the light over it (DrawMotes before
     * DrawRays), then the bloom from all of it. */
    if (air.motes > 0.005f)
        DrawMotes(&air, cameraX, cameraZ, cameraPitch, focalLength, aspectCorrection);
    if (air.rays > 0.005f && air.pattern != 0)
        DrawRays(&air, outputWidth, outputHeight);
    if (air.bloom > 0.005f)
        DrawBloom(&air, outputWidth, outputHeight);
    dglBindVertexArray(0);
    dglUseProgram(0);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

void DioramaGLAir_Shutdown(void)
{
    FreeBloom();
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
