#ifdef ENABLE_DIORAMA
/*
 * Lamp light (see diorama/gl_lamps.h). Project-owned (graphics overhaul,
 * phase 4).
 */
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_lamps.h"

/* The shaders' spelling of DIORAMA_MAX_LAMPS. */
typedef char LampCountCheck[DIORAMA_MAX_LAMPS == 16 ? 1 : -1];

#define MAX_SCENE_LAMPS 128
/* The warm light of a lamp at its strongest (a texel is multiplied by 1 plus
 * this times the falloff). */
#define LAMP_RED 1.35f
#define LAMP_GREEN 0.95f
#define LAMP_BLUE 0.45f

static float sPlaces[MAX_SCENE_LAMPS][4];
static unsigned sCount;
static float sStrength;
static bool sActive;
static float sFocusX;
static float sFocusZ;
/* The nearest DIORAMA_MAX_LAMPS, worked out when the lamps or the focus move. */
static float sNearest[DIORAMA_MAX_LAMPS][4];
static unsigned sNearestCount;
static bool sNearestValid;

void DioramaGLLamps_Set(const float (*places)[4], unsigned count, float strength)
{
    if (count > MAX_SCENE_LAMPS)
        count = MAX_SCENE_LAMPS;
    if (count != 0)
        memcpy(sPlaces, places, count * sizeof(sPlaces[0]));
    sCount = count;
    sStrength = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    sNearestValid = false;
}

void DioramaGLLamps_SetFocus(float x, float z)
{
    if (x != sFocusX || z != sFocusZ)
        sNearestValid = false;
    sFocusX = x;
    sFocusZ = z;
}

void DioramaGLLamps_SetActive(bool active)
{
    sActive = active;
}

float DioramaGLLamps_Strength(void)
{
    return sActive && sCount != 0 ? sStrength : 0.0f;
}

static void FindNearest(void)
{
    float distance[DIORAMA_MAX_LAMPS];
    unsigned i;

    sNearestCount = 0;
    for (i = 0; i < sCount; i++)
    {
        float dx = sPlaces[i][0] - sFocusX;
        float dz = sPlaces[i][2] - sFocusZ;
        float d = dx * dx + dz * dz;
        unsigned at = sNearestCount;

        /* Insertion into the sorted few. */
        while (at > 0 && distance[at - 1] > d)
            at--;
        if (at >= DIORAMA_MAX_LAMPS)
            continue;
        if (sNearestCount < DIORAMA_MAX_LAMPS)
            sNearestCount++;
        memmove(&distance[at + 1], &distance[at], (sNearestCount - 1 - at) * sizeof(distance[0]));
        memmove(&sNearest[at + 1], &sNearest[at], (sNearestCount - 1 - at) * sizeof(sNearest[0]));
        distance[at] = d;
        memcpy(sNearest[at], sPlaces[i], sizeof(sNearest[0]));
    }
    sNearestValid = true;
}

bool DioramaGLLamps_Locate(GLuint program, struct DioramaLampUniforms *uniforms)
{
    uniforms->count = dglGetUniformLocation(program, "lampCount");
    uniforms->places = dglGetUniformLocation(program, "lampPlaces");
    uniforms->colour = dglGetUniformLocation(program, "lampColour");
    /* A shader may leave some out (its compiler drops what it does not use). */
    return uniforms->count >= 0;
}

void DioramaGLLamps_ApplyNone(const struct DioramaLampUniforms *uniforms)
{
    if (uniforms->count >= 0)
        dglUniform1i(uniforms->count, 0);
}

void DioramaGLLamps_Apply(const struct DioramaLampUniforms *uniforms)
{
    float strength = DioramaGLLamps_Strength();

    if (strength <= 0.0f || uniforms->count < 0)
    {
        DioramaGLLamps_ApplyNone(uniforms);
        return;
    }
    if (!sNearestValid)
    {
        FindNearest();
        {
            static int sLog = -1;

            if (sLog < 0)
            {
                const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

                sLog = value != NULL && value[0] == '1';
            }
            if (sLog && sNearestCount != 0)
                SDL_Log("Diorama: lamps: %u of %u lit (strength %.2f), nearest at (%.1f, %.1f, %.1f) reach %.1f",
                        sNearestCount, sCount, sStrength, sNearest[0][0], sNearest[0][1], sNearest[0][2],
                        sNearest[0][3]);
        }
    }
    dglUniform1i(uniforms->count, (GLint)sNearestCount);
    if (uniforms->places >= 0 && sNearestCount != 0)
        dglUniform4fv(uniforms->places, (GLsizei)sNearestCount, &sNearest[0][0]);
    if (uniforms->colour >= 0)
        dglUniform3f(uniforms->colour, LAMP_RED * strength, LAMP_GREEN * strength, LAMP_BLUE * strength);
}

#endif // ENABLE_DIORAMA
