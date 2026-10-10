#ifdef ENABLE_DIORAMA

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_field_light.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_object_renderer.h"

/* 3ds_video.c: the circle's soft edge and warm pool, in radii. */
#define LIGHT_INNER 0.82f /* where the edge starts to darken */
#define LIGHT_OUTER 1.18f /* where it is dark */
#define LIGHT_POOL 0.85f  /* how far the warm pool reaches */

static const char sVertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 corner;\n"
    "out vec2 screen;\n" /* output pixels, y up */
    "uniform vec2 viewSize;\n"
    "void main() { screen = corner * viewSize; gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0); }\n";

/*
 * MakeFlashLight's two textures as one pass. The dark: alpha the smoothed
 * step from LIGHT_INNER to LIGHT_OUTER, its colour amber where the edge is
 * half dark ("blended by alpha, the rim gives about a fifth of amber at its
 * middle") and black beyond, tinted towards the fade's colour as the screen
 * fades. The pool: (1 - d / LIGHT_POOL)^2 of a warm orange, added under the
 * dark. Premultiplied: drawn with (ONE, ONE_MINUS_SRC_ALPHA) it is the 3DS's
 * pool added, then its dark blended over.
 */
static const char sFragmentShader[] =
    "#version 330 core\n"
    "in vec2 screen;\n"
    "out vec4 color;\n"
    "uniform vec4 spot;\n" /* the circle's centre and radii, output pixels */
    "uniform vec4 fadeTo;\n" /* the fade's colour, how far faded */
    "uniform float glow;\n"
    "void main() {\n"
    "  float d = length((screen - spot.xy) / spot.zw);\n"
    "  float t = clamp((d - 0.82) / (1.18 - 0.82), 0.0, 1.0);\n"
    "  t = t * t * (3.0 - 2.0 * t);\n"
    "  float warm = 0.72 * (1.0 - t);\n"
    "  vec3 rim = mix(vec3(warm, warm * 150.0 / 255.0, warm * 60.0 / 255.0), fadeTo.rgb, fadeTo.a);\n"
    "  float pool = max(1.0 - d / 0.85, 0.0);\n"
    "  vec3 added = vec3(1.0, 196.0 / 255.0, 128.0 / 255.0) * (pool * pool * glow);\n"
    "  color = vec4(added * (1.0 - t) + rim * t, t);\n"
    "}\n";

static bool sTried;
static bool sReady;
static GLuint sProgram;
static GLuint sQuadArray;
static GLuint sQuadBuffer;
static GLint sViewLocation;
static GLint sSpotLocation;
static GLint sFadeLocation;
static GLint sGlowLocation;
static bool sLogEnabled;
static bool sWasOn;

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
        SDL_Log("Diorama field light shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void Init(void)
{
    static const GLfloat quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    GLuint vertexShader;
    GLuint fragmentShader;
    GLint linked;
    const char *log = SDL_getenv("PKMN_DIORAMA_LOG");

    sTried = true;
    sLogEnabled = log != NULL && log[0] == '1';
    vertexShader = CompileShader(GL_VERTEX_SHADER, sVertexShader);
    fragmentShader = CompileShader(GL_FRAGMENT_SHADER, sFragmentShader);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
            dglDeleteShader(vertexShader);
        if (fragmentShader != 0)
            dglDeleteShader(fragmentShader);
        SDL_Log("Diorama field light unavailable: a dark cave is drawn lit in 3D");
        return;
    }
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vertexShader);
    dglAttachShader(sProgram, fragmentShader);
    dglLinkProgram(sProgram);
    dglDeleteShader(vertexShader);
    dglDeleteShader(fragmentShader);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
    sViewLocation = dglGetUniformLocation(sProgram, "viewSize");
    sSpotLocation = dglGetUniformLocation(sProgram, "spot");
    sFadeLocation = dglGetUniformLocation(sProgram, "fadeTo");
    sGlowLocation = dglGetUniformLocation(sProgram, "glow");
    if (!linked || sViewLocation < 0 || sSpotLocation < 0 || sFadeLocation < 0 || sGlowLocation < 0)
    {
        dglDeleteProgram(sProgram);
        sProgram = 0;
        SDL_Log("Diorama field light unavailable: a dark cave is drawn lit in 3D");
        return;
    }
    dglGenVertexArrays(1, &sQuadArray);
    dglGenBuffers(1, &sQuadBuffer);
    dglBindVertexArray(sQuadArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sQuadBuffer);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
    dglBindVertexArray(0);
    dglBindBuffer(GL_ARRAY_BUFFER, 0);
    sReady = true;
}

/* The scene shaders' camera (gl_terrain_renderer.c): a world point in output
 * pixels, y up. */
static bool Project(float x, float y, float z, float cameraX, float cameraZ,
                    float cameraPitch, float focalLength, float aspectCorrection,
                    int outputWidth, int outputHeight, float *screenX, float *screenY)
{
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    float distance = (16.0f * pitchCos - 0.458944f) / pitchSin;
    float relativeZ = z - cameraZ;
    float depth = (16.0f - y) * pitchSin + (relativeZ + distance) * pitchCos;
    float vertical = -(16.0f - y) * pitchCos + (relativeZ + distance) * pitchSin;
    float clipX = (x - cameraX) * focalLength * 2.0f / 240.0f * aspectCorrection;
    float clipY = -0.04f * depth + vertical * focalLength * 2.0f / 160.0f;

    if (depth <= 0.0001f)
        return false;
    *screenX = (clipX / depth * 0.5f + 0.5f) * (float)outputWidth;
    *screenY = (clipY / depth * 0.5f + 0.5f) * (float)outputHeight;
    return true;
}

/*
 * 3ds_video.c FadeColor, FadeChannelTarget and DetectPaletteFade: how far
 * (0-16) the background colours are faded towards one colour, and which -
 * black, white, or any other one channel by channel.
 */
static uint16_t FadeColor(unsigned color, unsigned to, unsigned y)
{
    int r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
    int tr = to & 31, tg = (to >> 5) & 31, tb = (to >> 10) & 31;

    r += ((tr - r) * (int)y) >> 4;
    g += ((tg - g) * (int)y) >> 4;
    b += ((tb - b) * (int)y) >> 4;
    return (uint16_t)(r | g << 5 | b << 10);
}

static int FadeChannelTarget(const uint16_t *now, const uint16_t *unfaded, unsigned shift, unsigned y)
{
    int to;

    for (to = 0; to < 32; ++to)
    {
        unsigned i;

        for (i = 0; i < 256; ++i)
        {
            int u = (unfaded[i] >> shift) & 31, f = (now[i] >> shift) & 31;

            if (u + (((to - u) * (int)y) >> 4) != f)
                break;
        }
        if (i == 256)
            return to;
    }
    return -1;
}

static unsigned DetectPaletteFade(const uint16_t *now, const uint16_t *unfaded, uint16_t *color)
{
    unsigned w;
    unsigned y;

    if (!memcmp(now, unfaded, 256 * sizeof(*now)))
        return 0;
    for (w = 0; w < 2; ++w)
        for (y = 1; y <= 16; ++y)
        {
            unsigned i = 0;

            while (i < 256 && FadeColor(unfaded[i], w ? 0x7fff : 0, y) == (now[i] & 0x7fff))
                ++i;
            if (i == 256)
            {
                *color = w ? 0x7fff : 0;
                return y;
            }
        }
    for (y = 1; y <= 16; ++y)
    {
        int r = FadeChannelTarget(now, unfaded, 0, y), g, b;

        if (r < 0 || (g = FadeChannelTarget(now, unfaded, 5, y)) < 0
         || (b = FadeChannelTarget(now, unfaded, 10, y)) < 0)
            continue;
        *color = (uint16_t)(r | g << 5 | b << 10);
        return y;
    }
    return 0;
}

void DioramaGLFieldLight_Draw(const struct DioramaSceneSnapshot *snapshot, float frameAlpha,
                              float cameraX, float cameraZ, float cameraPitch,
                              float focalLength, float aspectCorrection,
                              int outputWidth, int outputHeight)
{
    float x, groundY, z, cx, cy, x0, y0, x1, y1, scaleX, scaleY, t, radius, fade, glow;
    uint16_t fadeColor = 0;
    GLboolean scissor;
    GLboolean depth;

    if (snapshot == NULL || !snapshot->fieldLightOn)
    {
        if (sWasOn && sLogEnabled)
            SDL_Log("Diorama field light off");
        sWasOn = false;
        return;
    }
    if (!sTried)
        Init();
    if (!sReady || outputWidth <= 0 || outputHeight <= 0)
        return;
    /* CtrVoxel_PlayerLightSpot: half a tile up the player - the middle of the
     * GBA's circle is the middle of the player's metatile, where the body
     * stands - and the ground a GBA pixel covers, across and (foreshortened)
     * down. Without a player, the middle of the picture. */
    cx = outputWidth * 0.5f;
    cy = outputHeight * 0.5f;
    scaleX = scaleY = outputHeight / 160.0f;
    if (DioramaGLObjects_GetPlayerPose(frameAlpha, &x, &groundY, &z)
     && Project(x, groundY + 0.5f, z, cameraX, cameraZ, cameraPitch, focalLength,
                aspectCorrection, outputWidth, outputHeight, &cx, &cy)
     && Project(x - 1.0f, groundY, z, cameraX, cameraZ, cameraPitch, focalLength,
                aspectCorrection, outputWidth, outputHeight, &x0, &y0)
     && Project(x + 1.0f, groundY, z, cameraX, cameraZ, cameraPitch, focalLength,
                aspectCorrection, outputWidth, outputHeight, &x1, &y1))
    {
        scaleX = fabsf(x1 - x0) / 32.0f;
        if (Project(x, groundY, z - 1.0f, cameraX, cameraZ, cameraPitch, focalLength,
                    aspectCorrection, outputWidth, outputHeight, &x0, &y0)
         && Project(x, groundY, z + 1.0f, cameraX, cameraZ, cameraPitch, focalLength,
                    aspectCorrection, outputWidth, outputHeight, &x1, &y1))
            scaleY = fabsf(y1 - y0) / 32.0f;
    }
    if (scaleX < 0.01f) scaleX = 0.01f;
    if (scaleY < 0.01f) scaleY = 0.01f;
    /* A flame's slow breathing: two slow waves, a pixel or two; the glow
     * while Flash opens the circle; the dark fading with the screen. */
    t = (float)snapshot->fieldLightTicks;
    radius = snapshot->fieldLightShown
           * (1.0f + 0.010f * sinf(t * 0.071f) + 0.006f * sinf(t * 0.173f + 1.3f));
    if (radius < 0.5f)
        radius = 0.5f;
    fade = DetectPaletteFade(snapshot->fadedPalette, snapshot->unfadedPalette, &fadeColor) / 16.0f;
    glow = (0.10f + 0.03f * sinf(t * 0.21f) + 0.55f * snapshot->fieldLightBurst) * (1.0f - fade);
    if (glow < 0.0f)
        glow = 0.0f;
    if (!sWasOn && sLogEnabled)
        SDL_Log("Diorama field light on: radius %u (shown %.1f), at %.0f,%.0f, %.2f x %.2f pixels a GBA pixel",
                snapshot->fieldLightRadius, snapshot->fieldLightShown, cx, cy, scaleX, scaleY);
    sWasOn = true;

    scissor = glIsEnabled(GL_SCISSOR_TEST);
    depth = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, outputWidth, outputHeight);
    dglUseProgram(sProgram);
    dglUniform2f(sViewLocation, (float)outputWidth, (float)outputHeight);
    dglUniform4f(sSpotLocation, cx, cy, radius * scaleX, radius * scaleY);
    dglUniform4f(sFadeLocation, (fadeColor & 31) / 31.0f, ((fadeColor >> 5) & 31) / 31.0f,
                 ((fadeColor >> 10) & 31) / 31.0f, fade);
    dglUniform1f(sGlowLocation, glow);
    dglBindVertexArray(sQuadArray);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    dglBindVertexArray(0);
    dglUseProgram(0);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

void DioramaGLFieldLight_Shutdown(void)
{
    if (sQuadBuffer != 0)
        dglDeleteBuffers(1, &sQuadBuffer);
    if (sQuadArray != 0)
        dglDeleteVertexArrays(1, &sQuadArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sQuadBuffer = 0;
    sQuadArray = 0;
    sProgram = 0;
    sReady = false;
    sTried = false;
    sWasOn = false;
}

#endif // ENABLE_DIORAMA
