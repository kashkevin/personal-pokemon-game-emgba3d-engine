#ifdef ENABLE_DIORAMA

#include <math.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "constants/map_types.h"
#include "constants/weather.h"
#include "diorama/gl_light.h"
#include "diorama/gl_terrain_renderer.h"

/* The 3DS project's values (ctr_voxel.c). */
#define HAZE_MAX 0.14f
#define HAZE_START 1.05f /* x the eye-to-player distance */
#define HAZE_RAMP 0.60f  /* ... to full haze this much further */

struct Light
{
    float sun[3], shade[3]; /* the tint at full sun and in full shadow */
    float haze;             /* the most the distance haze takes */
    float hazeRgb[3];       /* what the distance fades to */
    float hazeStart, hazeRamp;
};

static struct Light sLight;

static bool Enabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LIGHT");

        sEnabled = value == NULL || value[0] != '0';
    }
    return sEnabled;
}

static void Set3(float out[3], float r, float g, float b)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
}

static struct Light Mix(const struct Light *a, const struct Light *b, float t)
{
    struct Light out = *a;
    int i;

    for (i = 0; i < 3; i++)
    {
        out.sun[i] += (b->sun[i] - a->sun[i]) * t;
        out.shade[i] += (b->shade[i] - a->shade[i]) * t;
        out.hazeRgb[i] += (b->hazeRgb[i] - a->hazeRgb[i]) * t;
    }
    out.haze += (b->haze - a->haze) * t;
    out.hazeStart += (b->hazeStart - a->hazeStart) * t;
    out.hazeRamp += (b->hazeRamp - a->hazeRamp) * t;
    return out;
}

static void Neutral(struct Light *light)
{
    Set3(light->sun, 1.0f, 1.0f, 1.0f);
    Set3(light->shade, 1.0f, 1.0f, 1.0f);
    light->haze = 0.0f;
    Set3(light->hazeRgb, 0.0f, 0.0f, 0.0f);
    light->hazeStart = HAZE_START;
    light->hazeRamp = HAZE_RAMP;
}

/* FogLight: the game's fog as light, coming in with the fog's own blend. Out
 * in the open a pale veil from much nearer than the clear-day haze; under the
 * ground the light goes cold and dim and the distance sinks into blue-black. */
static struct Light FogLight(const struct Light *clear, bool cave, float density)
{
    struct Light fog = *clear;

    if (cave)
    {
        Set3(fog.sun, 0.76f, 0.80f, 0.88f);
        Set3(fog.shade, 0.58f, 0.64f, 0.78f);
        fog.haze = 0.86f;
        Set3(fog.hazeRgb, 0.10f, 0.12f, 0.17f);
        fog.hazeStart = 0.86f;
        fog.hazeRamp = 0.55f;
    }
    else
    {
        Set3(fog.sun, 0.95f, 0.98f, 1.00f);
        fog.haze = 0.70f;
        Set3(fog.hazeRgb, 0.80f, 0.83f, 0.87f);
        fog.hazeStart = 0.80f;
        fog.hazeRamp = 0.65f;
    }
    return Mix(clear, &fog, density < 0.0f ? 0.0f : density > 1.0f ? 1.0f : density);
}

void DioramaGLLight_Update(const struct DioramaSceneSnapshot *snapshot, bool enabled)
{
    struct Light light;
    float scale[3];
    float offset[3];
    int i;

    /* The 3DS port's rule: indoors (a building, a secret base) is unlit;
     * everywhere else, caves and the sea floor included, takes the light. */
    if (!Enabled() || !enabled || snapshot == NULL
     || snapshot->mapType == MAP_TYPE_INDOOR || snapshot->mapType == MAP_TYPE_SECRET_BASE)
    {
        Neutral(&sLight);
        return;
    }
    /* LightFor: a clear day, then the weather's own. */
    Set3(light.sun, 1.00f, 0.99f, 0.95f);
    Set3(light.shade, 0.93f, 0.97f, 1.05f);
    light.haze = HAZE_MAX;
    Set3(light.hazeRgb, 0xD2 / 255.0f, 0xE6 / 255.0f, 0xFA / 255.0f);
    light.hazeStart = HAZE_START;
    light.hazeRamp = HAZE_RAMP;
    switch (snapshot->weather)
    {
    case WEATHER_SUNNY:
    case WEATHER_DROUGHT:
        Set3(light.sun, 1.04f, 1.01f, 0.93f);
        light.haze = 0.12f;
        break;
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
        /* Overcast. */
        Set3(light.sun, 0.81f, 0.88f, 0.98f);
        Set3(light.shade, 0.82f, 0.89f, 1.00f);
        light.haze = 0.30f;
        break;
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
    case WEATHER_UNDERWATER:
    case WEATHER_UNDERWATER_BUBBLES:
        /* The fog sprites' blend: EVA 12 when the fog is in (4 under water). */
        light = FogLight(&light, snapshot->mapType == MAP_TYPE_UNDERGROUND,
                         snapshot->weatherBlendEVA / 12.0f);
        break;
    case WEATHER_SNOW:
    case WEATHER_VOLCANIC_ASH:
    case WEATHER_SANDSTORM:
        /* The weather's own ash or sand fills the air instead. */
        Set3(light.sun, 0.93f, 0.92f, 0.88f);
        light.haze = 0.36f;
        break;
    case WEATHER_SHADE:
    case WEATHER_SUNNY_CLOUDS:
        Set3(light.sun, 0.92f, 0.95f, 0.98f);
        light.haze = 0.25f;
        break;
    default:
        break;
    }
    /* The sky's haze under the palette's night tint and fades, as the trees. */
    DioramaGLTerrain_GetPaletteLine(scale, offset);
    for (i = 0; i < 3; i++)
    {
        float c = light.hazeRgb[i] * scale[i] + offset[i];

        light.hazeRgb[i] = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
    }
    sLight = light;
}

bool DioramaGLLight_Locate(GLuint program, struct DioramaLightUniforms *uniforms)
{
    uniforms->sunTint = dglGetUniformLocation(program, "sunTint");
    uniforms->shadeTint = dglGetUniformLocation(program, "shadeTint");
    uniforms->haze = dglGetUniformLocation(program, "haze");
    uniforms->hazeColor = dglGetUniformLocation(program, "hazeColor");
    return uniforms->sunTint >= 0 && uniforms->shadeTint >= 0
        && uniforms->haze >= 0 && uniforms->hazeColor >= 0;
}

void DioramaGLLight_Apply(const struct DioramaLightUniforms *uniforms, float cameraPitch)
{
    /* The eye's depth of the point the camera looks at (the player): the
     * shaders' camera, 16 tiles up, aimed 0.458944 tiles below the ground. */
    float pitchSin = sinf(cameraPitch);
    float pitchCos = cosf(cameraPitch);
    float distance = (16.0f * pitchCos - 0.458944f) / pitchSin;
    float eye = 16.0f * pitchSin + distance * pitchCos;
    float start = eye * sLight.hazeStart;
    float perDepth = sLight.haze / (eye * sLight.hazeRamp);

    dglUniform3f(uniforms->sunTint, sLight.sun[0], sLight.sun[1], sLight.sun[2]);
    dglUniform3f(uniforms->shadeTint, sLight.shade[0], sLight.shade[1], sLight.shade[2]);
    dglUniform3f(uniforms->haze, start, perDepth, sLight.haze);
    dglUniform3f(uniforms->hazeColor, sLight.hazeRgb[0], sLight.hazeRgb[1], sLight.hazeRgb[2]);
}

void DioramaGLLight_ApplyNeutral(const struct DioramaLightUniforms *uniforms)
{
    dglUniform3f(uniforms->sunTint, 1.0f, 1.0f, 1.0f);
    dglUniform3f(uniforms->shadeTint, 1.0f, 1.0f, 1.0f);
    dglUniform3f(uniforms->haze, 0.0f, 0.0f, 0.0f);
    dglUniform3f(uniforms->hazeColor, 0.0f, 0.0f, 0.0f);
}

#endif // ENABLE_DIORAMA
