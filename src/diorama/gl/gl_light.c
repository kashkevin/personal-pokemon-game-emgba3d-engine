#ifdef ENABLE_DIORAMA

#include <math.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "fieldmap.h"
#include "constants/map_types.h"
#include "constants/rtc.h"
#include "constants/weather.h"
#include "diorama/gl_light.h"
#include "diorama/gl_terrain_renderer.h"

/* The 3DS project's values (ctr_voxel.c). */
#define HAZE_MAX 0.14f
#define HAZE_START 1.05f /* x the eye-to-player distance */
#define HAZE_RAMP 0.60f  /* ... to full haze this much further */

/* The sun dapples' pattern (MakeDapple); its period and stretch are in the
 * shader (gl_light.h, Dapple), 24 tiles along the sun and 0.70 of that across. */
#define DAPPLE_DIM 128
#define DAPPLE_PERIOD 24.0f
#define DAPPLE_DRIFT 0.25f /* tiles per second, the way the shadows fall */

/* At night (the game's clock) the dapples are this strong, and there are no
 * sun rays or sunlit dust; the 3DS has no night. As the sun's shadows
 * (gl_compositor.c, SunStrength). */
#define NIGHT_DAPPLES 0.5f

struct Light
{
    float sun[3], shade[3];      /* the tint at full sun and in full shadow */
    float haze;                  /* the most the distance haze takes */
    float dappleLow, dappleHigh; /* brightness under a dapple's shade, in its light */
    float rays;                  /* the sun rays' strength at their brightest */
    float bloom;                 /* glow around the brightest parts */
    float motes;                 /* the sunlit dust in the air, at its brightest */
    float hazeRgb[3];            /* what the distance fades to */
    float hazeStart, hazeRamp;
};

static struct Light sLight;
static GLuint sDappleTexture;
static bool sDappleTried;
/* Where the current map's origin lies in the world the dapples are laid on
 * (x east, z south, in tiles): moved at each map crossing, so the pattern
 * stays put on the ground (the 3DS port's sDappleAnchorX/Z). */
static float sAnchorX;
static float sAnchorZ;
static bool sAnchorKnown;
static uint8_t sAnchorMapGroup;
static uint8_t sAnchorMapNum;

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
    out.dappleLow += (b->dappleLow - a->dappleLow) * t;
    out.dappleHigh += (b->dappleHigh - a->dappleHigh) * t;
    out.rays += (b->rays - a->rays) * t;
    out.bloom += (b->bloom - a->bloom) * t;
    out.motes += (b->motes - a->motes) * t;
    out.hazeStart += (b->hazeStart - a->hazeStart) * t;
    out.hazeRamp += (b->hazeRamp - a->hazeRamp) * t;
    return out;
}

static void Neutral(struct Light *light)
{
    Set3(light->sun, 1.0f, 1.0f, 1.0f);
    Set3(light->shade, 1.0f, 1.0f, 1.0f);
    light->haze = 0.0f;
    light->dappleLow = light->dappleHigh = 1.0f;
    light->rays = light->bloom = light->motes = 0.0f;
    Set3(light->hazeRgb, 0.0f, 0.0f, 0.0f);
    light->hazeStart = HAZE_START;
    light->hazeRamp = HAZE_RAMP;
}

/*
 * The 3DS port's DappleLattice, Quintic, DappleNoise and MakeDapple
 * (ctr_voxel.c, MIT): three octaves of tileable value noise, then a soft
 * threshold - broad patches with ragged, blurred edges, a little more light
 * than shade. Not fatal: without it there are no dapples.
 */
static float DappleLattice(int x, int y, int period, uint32_t seed)
{
    uint32_t h = (uint32_t)(x % period) * 374761393u + (uint32_t)(y % period) * 668265263u
               + seed * 2246822519u;

    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFu) / 65535.0f;
}

static float Quintic(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float DappleNoise(float u, float v, int period, uint32_t seed)
{
    int x = (int)u;
    int y = (int)v;
    float fx = Quintic(u - (float)x);
    float fy = Quintic(v - (float)y);
    float a = DappleLattice(x, y, period, seed);
    float b = DappleLattice(x + 1, y, period, seed);
    float c = DappleLattice(x, y + 1, period, seed);
    float d = DappleLattice(x + 1, y + 1, period, seed);
    float top = a + (b - a) * fx;
    float bottom = c + (d - c) * fx;

    return top + (bottom - top) * fy;
}

static void MakeDapple(void)
{
    static const struct
    {
        int period;
        float weight;
    } octaves[] = {{3, 1.0f}, {6, 0.5f}, {12, 0.2f}};
    static uint8_t texels[DAPPLE_DIM * DAPPLE_DIM];
    unsigned x;
    unsigned y;
    unsigned o;

    sDappleTried = true;
    for (y = 0; y < DAPPLE_DIM; y++)
    {
        for (x = 0; x < DAPPLE_DIM; x++)
        {
            float n = 0.0f;
            float total = 0.0f;
            float t;

            for (o = 0; o < sizeof(octaves) / sizeof(octaves[0]); o++)
            {
                float scale = (float)octaves[o].period / DAPPLE_DIM;

                n += octaves[o].weight * DappleNoise((float)x * scale, (float)y * scale,
                                                     octaves[o].period, o + 1);
                total += octaves[o].weight;
            }
            t = (n / total - 0.40f) / 0.20f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            texels[y * DAPPLE_DIM + x] = (uint8_t)(t * t * (3.0f - 2.0f * t) * 255.0f + 0.5f);
        }
    }
    glGenTextures(1, &sDappleTexture);
    if (sDappleTexture == 0)
        return;
    glBindTexture(GL_TEXTURE_2D, sDappleTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, DAPPLE_DIM, DAPPLE_DIM, 0, GL_RED,
                 GL_UNSIGNED_BYTE, texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* A map crossing: the cells the new map shows of the one just left say how
 * far its origin moved. A warp has none, and the pattern starts anywhere. */
static void UpdateAnchor(const struct DioramaSceneSnapshot *snapshot)
{
    unsigned i;

    if (sAnchorKnown
     && (snapshot->mapGroup != sAnchorMapGroup || snapshot->mapNum != sAnchorMapNum))
    {
        for (i = 0; i < snapshot->visibleCellCount; i++)
        {
            const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

            if ((cell->flags & DIORAMA_CELL_CONNECTED)
             && cell->sourceMapGroup == sAnchorMapGroup
             && cell->sourceMapNum == sAnchorMapNum)
            {
                sAnchorX += (float)(cell->sourceMapX + MAP_OFFSET - cell->mapX);
                sAnchorZ += (float)(cell->sourceMapY + MAP_OFFSET - cell->mapY);
                if (SDL_getenv("PKMN_DIORAMA_LOG") != NULL)
                    SDL_Log("diorama light: map %u.%u -> %u.%u, dapples anchored at %.0f, %.0f",
                            sAnchorMapGroup, sAnchorMapNum, snapshot->mapGroup,
                            snapshot->mapNum, sAnchorX, sAnchorZ);
                break;
            }
        }
    }
    sAnchorMapGroup = snapshot->mapGroup;
    sAnchorMapNum = snapshot->mapNum;
    sAnchorKnown = true;
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
        fog.dappleLow = fog.dappleHigh = 1.0f;
        fog.rays = fog.bloom = fog.motes = 0.0f;
        Set3(fog.hazeRgb, 0.10f, 0.12f, 0.17f);
        fog.hazeStart = 0.86f;
        fog.hazeRamp = 0.55f;
    }
    else
    {
        Set3(fog.sun, 0.95f, 0.98f, 1.00f);
        fog.haze = 0.70f;
        /* Fog glows: more bloom, and no dust or rays to see in it. */
        fog.dappleLow = 0.95f;
        fog.dappleHigh = 1.02f;
        fog.rays = 0.0f;
        fog.bloom = 0.18f;
        fog.motes = 0.0f;
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

    if (snapshot != NULL)
        UpdateAnchor(snapshot);
    /* The 3DS port's rule: indoors (a building, a secret base) is unlit;
     * everywhere else, caves and the sea floor included, takes the light. */
    if (!Enabled() || !enabled || snapshot == NULL
     || snapshot->mapType == MAP_TYPE_INDOOR || snapshot->mapType == MAP_TYPE_SECRET_BASE)
    {
        Neutral(&sLight);
        return;
    }
    if (!sDappleTried)
        MakeDapple();
    /* LightFor: a clear day, then the weather's own. */
    Set3(light.sun, 1.00f, 0.99f, 0.95f);
    Set3(light.shade, 0.93f, 0.97f, 1.05f);
    light.haze = HAZE_MAX;
    light.dappleLow = 0.96f;
    light.dappleHigh = 1.04f;
    light.rays = 0.11f;
    light.bloom = 0.07f;
    light.motes = 0.85f;
    Set3(light.hazeRgb, 0xD2 / 255.0f, 0xE6 / 255.0f, 0xFA / 255.0f);
    light.hazeStart = HAZE_START;
    light.hazeRamp = HAZE_RAMP;
    switch (snapshot->weather)
    {
    case WEATHER_SUNNY:
    case WEATHER_DROUGHT:
        Set3(light.sun, 1.04f, 1.01f, 0.93f);
        light.haze = 0.12f;
        light.dappleLow = 0.94f;
        light.dappleHigh = 1.07f;
        light.rays = 0.14f;
        light.bloom = 0.10f;
        light.motes = 1.00f;
        break;
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
        /* Overcast: no sun to break into patches, rays or glinting dust. */
        Set3(light.sun, 0.81f, 0.88f, 0.98f);
        Set3(light.shade, 0.82f, 0.89f, 1.00f);
        light.haze = 0.30f;
        light.dappleLow = light.dappleHigh = 1.0f;
        light.rays = 0.0f;
        light.bloom = 0.08f;
        light.motes = 0.0f;
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
        light.dappleLow = 0.92f;
        light.dappleHigh = 1.03f;
        light.rays = 0.0f;
        light.bloom = 0.12f;
        light.motes = 0.0f;
        break;
    case WEATHER_SHADE:
    case WEATHER_SUNNY_CLOUDS:
        Set3(light.sun, 0.92f, 0.95f, 0.98f);
        light.haze = 0.25f;
        light.dappleLow = 0.92f;
        light.dappleHigh = 1.03f;
        light.rays = 0.0f;
        light.bloom = 0.12f;
        light.motes = 0.40f;
        break;
    default:
        break;
    }
    if (snapshot->timeOfDay == TIME_NIGHT)
    {
        light.dappleLow = 1.0f - (1.0f - light.dappleLow) * NIGHT_DAPPLES;
        light.dappleHigh = 1.0f + (light.dappleHigh - 1.0f) * NIGHT_DAPPLES;
        light.rays = 0.0f;
        light.motes = 0.0f;
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
    uniforms->dappleMap = dglGetUniformLocation(program, "dappleMap");
    uniforms->dapple = dglGetUniformLocation(program, "dapple");
    uniforms->dappleAnchor = dglGetUniformLocation(program, "dappleAnchor");
    return uniforms->sunTint >= 0 && uniforms->shadeTint >= 0
        && uniforms->haze >= 0 && uniforms->hazeColor >= 0
        && uniforms->dappleMap >= 0 && uniforms->dapple >= 0 && uniforms->dappleAnchor >= 0;
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
    bool dapples = sDappleTexture != 0 && sLight.dappleHigh - sLight.dappleLow > 0.01f;
    float drift = (float)fmod(SDL_GetTicks() / 1000.0 * DAPPLE_DRIFT, DAPPLE_PERIOD);

    dglUniform3f(uniforms->sunTint, sLight.sun[0], sLight.sun[1], sLight.sun[2]);
    dglUniform3f(uniforms->shadeTint, sLight.shade[0], sLight.shade[1], sLight.shade[2]);
    dglUniform3f(uniforms->haze, start, perDepth, sLight.haze);
    dglUniform3f(uniforms->hazeColor, sLight.hazeRgb[0], sLight.hazeRgb[1], sLight.hazeRgb[2]);
    dglUniform1i(uniforms->dappleMap, DIORAMA_LIGHT_TEXTURE_UNIT);
    dglUniform4f(uniforms->dapple, sLight.dappleLow, sLight.dappleHigh, drift, dapples ? 1.0f : 0.0f);
    dglUniform2f(uniforms->dappleAnchor, sAnchorX, sAnchorZ);
    dglActiveTexture(GL_TEXTURE0 + DIORAMA_LIGHT_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, dapples ? sDappleTexture : 0);
    dglActiveTexture(GL_TEXTURE0);
}

void DioramaGLLight_ApplyNeutral(const struct DioramaLightUniforms *uniforms)
{
    dglUniform3f(uniforms->sunTint, 1.0f, 1.0f, 1.0f);
    dglUniform3f(uniforms->shadeTint, 1.0f, 1.0f, 1.0f);
    dglUniform3f(uniforms->haze, 0.0f, 0.0f, 0.0f);
    dglUniform3f(uniforms->hazeColor, 0.0f, 0.0f, 0.0f);
    dglUniform1i(uniforms->dappleMap, DIORAMA_LIGHT_TEXTURE_UNIT);
    dglUniform4f(uniforms->dapple, 1.0f, 1.0f, 0.0f, 0.0f);
    dglUniform2f(uniforms->dappleAnchor, 0.0f, 0.0f);
}

void DioramaGLLight_GetAir(struct DioramaAirLight *air)
{
    float scale[3];
    float offset[3];
    int i;

    DioramaGLTerrain_GetPaletteLine(scale, offset);
    air->rays = sLight.rays;
    air->bloom = sLight.bloom;
    air->motes = sLight.motes;
    for (i = 0; i < 3; i++)
    {
        air->fadeScale[i] = scale[i];
        air->fadeOffset[i] = offset[i];
    }
    air->anchorX = sAnchorX;
    air->anchorZ = sAnchorZ;
    air->pattern = sDappleTexture;
}

void DioramaGLLight_Shutdown(void)
{
    if (sDappleTexture != 0)
        glDeleteTextures(1, &sDappleTexture);
    sDappleTexture = 0;
    sDappleTried = false;
    sAnchorKnown = false;
    sAnchorX = sAnchorZ = 0.0f;
}

#endif // ENABLE_DIORAMA
