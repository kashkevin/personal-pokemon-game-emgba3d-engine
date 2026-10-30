#if defined(DIORAMA_BATTLE_ENV_TEST) && defined(ENABLE_DIORAMA)
/*
 * The geometry probe's render side (diorama/geometry_probe.h): once the field is free on the place (the game side,
 * geometry_probe_game.c) and the 3D view has built the scene round the player, the cells as the terrain mesh is built
 * from them, then a picture from each camera asked for, against a sky the game never draws. Project-owned, test
 * builds only.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <SDL2/SDL.h>

#include "diorama/camera.h"
#include "diorama/geometry_probe.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/scene_snapshot.h"
#include "geometry_probe_shared.h"
#include "constants/map_types.h"

#define GEO_MAX_CAMERAS 8
#define GEO_SETTLE_FRAMES 8         /* presented frames after the scene is first complete */
#define GEO_SETTLE_LIMIT 400        /* presented frames to wait for a complete scene (then taken as it is) */
#define GEO_FRAMES_PER_SHOT 3       /* presented frames drawn with a camera before its picture */
#define GEO_DEG 0.017453293f

struct GeoCamera
{
    char name[12];
    float pitch;        /* degrees */
    float lens;         /* the vertical view, degrees */
    float extent[3];    /* tiles at the target: outdoors, indoors, caves */
};

enum
{
    STEP_WAIT,      /* for a complete scene */
    STEP_SHOOT,     /* the cameras, one after another */
    STEP_FINISHED,
};

static bool sInit, sMagentaSky = true;
static char sOut[400];
static struct GeoCamera sCameras[GEO_MAX_CAMERAS];
static int sCameraCount;
static int sStep, sFrames, sSettledFrames, sCamera, sCameraFrames, sShots, sCells, sPlaceIndex = -1;
static bool sShotDue;
static char sStem[600];
static char sStatus[48];
static unsigned sSettleWaited;

static void MakeFolder(const char *path)
{
    /* MinGW's mkdir takes no mode (the Windows package's graphics job runs the probe) */
#ifdef _WIN32
    int made = _mkdir(path);
#else
    int made = mkdir(path, 0777);
#endif

    if (made != 0 && errno != EEXIST)
        SDL_Log("Geometry probe: cannot make %s", path);
}

/* "a" the Adventure camera, "d" the Diorama camera (each at its own default framing: camera.c's sSpecs), "sNN" a
 * 34-tile view at NN degrees (the F5 survey's), "wNN" a 24-tile one. */
static void ParseCameras(const char *spec)
{
    char copy[256];
    char *save = NULL;

    SDL_strlcpy(copy, spec, sizeof(copy));
    for (char *token = SDL_strtokr(copy, ",", &save); token != NULL && sCameraCount < GEO_MAX_CAMERAS;
         token = SDL_strtokr(NULL, ",", &save))
    {
        struct GeoCamera *c = &sCameras[sCameraCount];

        SDL_strlcpy(c->name, token, sizeof(c->name));
        if (strcmp(token, "a") == 0)
            *c = (struct GeoCamera){"a", 42.0f, 33.0f, {11.5f, 9.0f, 10.5f}};
        else if (strcmp(token, "d") == 0)
            *c = (struct GeoCamera){"d", 52.0f, 20.0f, {24.0f, 15.0f, 18.0f}};
        else if ((token[0] == 's' || token[0] == 'w') && atoi(token + 1) > 0)
        {
            float extent = token[0] == 's' ? 34.0f : 24.0f;

            c->pitch = (float)atoi(token + 1);
            c->lens = 33.0f;
            c->extent[0] = c->extent[1] = c->extent[2] = extent;
        }
        else
            continue;
        SDL_strlcpy(c->name, token, sizeof(c->name));
        sCameraCount++;
    }
}

static void Init(void)
{
    const char *value;

    sInit = true;
    value = SDL_getenv("PKMN_TEST_GEO_OUT");
    SDL_strlcpy(sOut, value != NULL && value[0] != '\0' ? value : "geometry-probe", sizeof(sOut));
    MakeFolder(sOut);
    {
        char path[460];

        SDL_snprintf(path, sizeof(path), "%s/cells", sOut);
        MakeFolder(path);
        SDL_snprintf(path, sizeof(path), "%s/shots", sOut);
        MakeFolder(path);
    }
    value = SDL_getenv("PKMN_TEST_GEO_CAMERAS");
    ParseCameras(value != NULL && value[0] != '\0' ? value : "a,d,s15,s75");
    value = SDL_getenv("PKMN_TEST_GEO_SKY");
    sMagentaSky = !(value != NULL && value[0] == '0');
}

static void WriteResult(const struct DioramaSceneSnapshot *snapshot)
{
    char path[460];
    FILE *file;

    SDL_snprintf(path, sizeof(path), "%s/results.tsv", sOut);
    file = fopen(path, "a");
    if (file == NULL)
        return;
    fprintf(file, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%u\t%s\n", gGeoProbe.key, gGeoProbe.group,
            gGeoProbe.num, gGeoProbe.x, gGeoProbe.y, gGeoProbe.arrivedGroup, gGeoProbe.arrivedNum,
            snapshot != NULL ? snapshot->mapType : -1, sCells, sStatus, sShots, sFrames, sSettleWaited,
            gGeoProbe.failure[0] != '\0' ? gGeoProbe.failure : "-");
    fclose(file);
}

static void DumpCells(const struct DioramaSceneSnapshot *snapshot, const struct DioramaResolvedCell *resolved)
{
    char path[600];
    FILE *file;

    SDL_snprintf(path, sizeof(path), "%s/cells/%s.tsv", sOut, gGeoProbe.key);
    file = fopen(path, "w");
    if (file == NULL)
    {
        SDL_Log("Geometry probe: cannot write %s", path);
        return;
    }
    fprintf(file, "# map %u.%u layout %u type %u player %d %d grid %d %d\n", snapshot->mapGroup, snapshot->mapNum,
            snapshot->mapLayoutId, snapshot->mapType, snapshot->playerMapX, snapshot->playerMapY,
            snapshot->gridOriginX, snapshot->gridOriginY);
    sCells = (int)DioramaGLTerrain_TestDumpCells(snapshot, resolved, file);
    fclose(file);
}

static void Begin(void)
{
    sPlaceIndex = gGeoProbe.index;
    sStep = STEP_WAIT;
    sFrames = sSettledFrames = sCamera = sCameraFrames = sShots = sCells = 0;
    sSettleWaited = 0;
    sShotDue = false;
    SDL_strlcpy(sStatus, "ok", sizeof(sStatus));
}

void DioramaGeoProbe_RenderFrame(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaResolvedCell *resolved, bool ready3D)
{
    if (SDL_AtomicGet(&gGeoProbe.phase) != GEO_ARRIVED)
    {
        sPlaceIndex = -1;
        return;
    }
    if (!sInit)
        Init();
    if (sPlaceIndex != gGeoProbe.index)
        Begin();
    sFrames++;
    if (sStep == STEP_WAIT)
    {
        bool here = snapshot != NULL && snapshot->mapGroup == gGeoProbe.arrivedGroup
                 && snapshot->mapNum == gGeoProbe.arrivedNum;

        if (here && ready3D && DioramaGLTerrain_TestSettled(snapshot))
            sSettledFrames++;
        if (sSettledFrames < GEO_SETTLE_FRAMES && sFrames < GEO_SETTLE_LIMIT)
            return;
        sSettleWaited = (unsigned)sFrames;
        if (!here || !ready3D)
        {
            /* the field is not drawn in 3D here (a map the rules do not support), or not this map */
            SDL_strlcpy(sStatus, !here ? "other-map" : "not-3d", sizeof(sStatus));
            WriteResult(snapshot);
            sStep = STEP_FINISHED;
            SDL_AtomicSet(&gGeoProbe.phase, GEO_DONE);
            return;
        }
        if (sSettledFrames < GEO_SETTLE_FRAMES)
            SDL_strlcpy(sStatus, "unsettled", sizeof(sStatus));
        DumpCells(snapshot, resolved);
        sStep = STEP_SHOOT;
        sCamera = 0;
        sCameraFrames = 0;
    }
    if (sStep == STEP_SHOOT)
    {
        if (sCamera >= sCameraCount)
        {
            WriteResult(snapshot);
            sStep = STEP_FINISHED;
            SDL_AtomicSet(&gGeoProbe.phase, GEO_DONE);
            return;
        }
        if (++sCameraFrames >= GEO_FRAMES_PER_SHOT && !sShotDue)
        {
            SDL_snprintf(sStem, sizeof(sStem), "%s/shots/%s-%s", sOut, gGeoProbe.key, sCameras[sCamera].name);
            sShotDue = true;
        }
    }
}

bool DioramaGeoProbe_CameraView(float targetX, float targetZ, float groundY, int place,
                                struct DioramaCameraView *view)
{
    const struct GeoCamera *c;

    if (SDL_AtomicGet(&gGeoProbe.phase) != GEO_ARRIVED || sStep != STEP_SHOOT || sCamera >= sCameraCount)
        return false;
    c = &sCameras[sCamera];
    DioramaCamera_Frame(c->pitch * GEO_DEG, c->lens, c->extent[place >= 0 && place < 3 ? place : 0], groundY, view);
    view->x = targetX;
    view->z = targetZ;
    return true;
}

bool DioramaGeoProbe_SkyColour(float rgb[3])
{
    if (!sMagentaSky || SDL_AtomicGet(&gGeoProbe.phase) != GEO_ARRIVED)
        return false;
    rgb[0] = 1.0f;
    rgb[1] = 0.0f;
    rgb[2] = 1.0f;
    return true;
}

bool DioramaGeoProbe_VoidFloor(uint8_t mapType)
{
    /* read once, before the tour's first place has its first frame (Init): its chunks are built before it */
    static int sVoidFloor = -1;

    if (sVoidFloor < 0)
    {
        const char *tour = SDL_getenv("PKMN_TEST_GEO_TOUR");
        const char *sky = SDL_getenv("PKMN_TEST_GEO_SKY");

        sVoidFloor = tour != NULL && tour[0] != '\0' && !(sky != NULL && sky[0] == '0');
    }
    return sVoidFloor && (mapType == MAP_TYPE_INDOOR || mapType == MAP_TYPE_UNDERGROUND
                       || mapType == MAP_TYPE_SECRET_BASE);
}

const char *DioramaGeoProbe_CaptureStem(void)
{
    if (!sShotDue || SDL_AtomicGet(&gGeoProbe.phase) != GEO_ARRIVED)
        return NULL;
    return sStem;
}

void DioramaGeoProbe_CaptureWritten(void)
{
    if (!sShotDue)
        return;
    sShotDue = false;
    sShots++;
    sCamera++;
    sCameraFrames = 0;
}
#endif
