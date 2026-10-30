#if defined(DIORAMA_BATTLE_ENV_TEST) && defined(ENABLE_DIORAMA)
/*
 * The geometry probe's game side (diorama/geometry_probe.h): the tour's places, the warps there and the wait for
 * the field. The render side (geometry_probe.c) takes the cells and the pictures and says when it is done.
 * Project-owned, test builds only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "rtc.h"
#include "script.h"
#include "constants/flags.h"
#include "constants/map_groups.h"
#include "constants/weather.h"
#include "diorama/geometry_probe.h"
#include "geometry_probe_shared.h"

#define GEO_MAX_PLACES 4096
#define GEO_ARRIVE_FRAMES 1800      /* game frames a place may take to give the field back */
#define GEO_RENDER_FRAMES 36000     /* game frames the render side may take (a slow software renderer) */

struct GeoPlace
{
    char key[96];
    int group, num, x, y;
};

static struct GeoPlace *sPlaces;
static int sPlaceCount, sNext, sLast, sFrames, sGapFrames;
static bool sInit, sActive, sWarpPending;
static int sHour = 12;
static bool sKeepWeather;
struct GeoProbeShared gGeoProbe;

static void Load(void)
{
    const char *path = SDL_getenv("PKMN_TEST_GEO_TOUR");
    const char *value;
    char line[256];
    FILE *file;

    sInit = true;
    if (path == NULL || path[0] == '\0')
        return;
    file = fopen(path, "r");
    if (file == NULL)
    {
        SDL_Log("Geometry probe: cannot read %s", path);
        return;
    }
    sPlaces = calloc(GEO_MAX_PLACES, sizeof(*sPlaces));
    while (sPlaces != NULL && sPlaceCount < GEO_MAX_PLACES && fgets(line, sizeof(line), file) != NULL)
    {
        struct GeoPlace *p = &sPlaces[sPlaceCount];

        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%95s %d %d %d %d", p->key, &p->group, &p->num, &p->x, &p->y) == 5)
            sPlaceCount++;
    }
    fclose(file);
    if ((value = SDL_getenv("PKMN_TEST_GEO_HOUR")) != NULL && value[0] != '\0')
        sHour = atoi(value);
    sKeepWeather = (value = SDL_getenv("PKMN_TEST_GEO_WEATHER")) != NULL && strcmp(value, "keep") == 0;
    value = SDL_getenv("PKMN_TEST_GEO_FIRST");
    sNext = value != NULL ? atoi(value) : 0;
    value = SDL_getenv("PKMN_TEST_GEO_LAST");  /* the place to stop before (one process of several) */
    sLast = value != NULL && value[0] != '\0' ? atoi(value) : sPlaceCount;
    if (sLast > sPlaceCount)
        sLast = sPlaceCount;
    sActive = sPlaceCount > 0;
    SDL_AtomicSet(&gGeoProbe.phase, GEO_IDLE);
    SDL_Log("Geometry probe: %d places from %s, starting at %d", sPlaceCount, path, sNext);
}

static bool FieldFree(void)
{
    return gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !gPaletteFade.active
        && !ScriptContext_IsEnabled();
}

static void Finish(void)
{
    SDL_Log("Geometry probe: tour done (to place %d of %d)", sLast, sPlaceCount);
    SDL_AtomicSet(&gGeoProbe.phase, GEO_FINISHED);
    fflush(NULL);
    _exit(0);
}

bool DioramaGeoProbe_FieldTick(void)
{
    int phase;

    if (!sInit)
        Load();
    if (!sActive)
        return false;
    phase = SDL_AtomicGet(&gGeoProbe.phase);
    switch (phase)
    {
    case GEO_IDLE:
        /* the field free: a place whose own script keeps it (a cutscene on arrival) ends the process, which the
           tool starts again at the next place - a warp in the middle of a script's message box broke the game heap
           (two aborts in the first tour: the text printers' blocks freed at the next map's load) */
        if (++sGapFrames < 10 || (!FieldFree() && sGapFrames < GEO_ARRIVE_FRAMES))
            return true;
        if (sNext >= sLast)
            Finish();
        if (!FieldFree())
        {
            SDL_Log("Geometry probe: the field was not free after place %d; stopping here", sNext - 1);
            fflush(NULL);
            _exit(5);
        }
        sGapFrames = 0;
        UnfreezeObjectEvents();
        {
            const struct GeoPlace *p = &sPlaces[sNext];

            SDL_strlcpy(gGeoProbe.key, p->key, sizeof(gGeoProbe.key));
            gGeoProbe.group = p->group;
            gGeoProbe.num = p->num;
            gGeoProbe.x = p->x;
            gGeoProbe.y = p->y;
            gGeoProbe.index = sNext;
            SDL_Log("Geometry probe: place %d %s", sNext, p->key);  /* where a run that stopped got to */
            /* noon, the cave lit as with Flash: the shapes, not the hour (in memory only) */
            RtcCalcLocalTimeOffset(0, sHour, 0, 0);
            FlagSet(FLAG_SYS_USE_FLASH);
            /* the Battle Pike's room picks its trainer from the challenge's battle number when entered, which the
             * game sets before it lets the player in (battle_pike.c PrepareOneTrainer loops for ever on none) */
            if ((p->num | (p->group << 8)) == MAP_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL
             && gSaveBlock2Ptr->frontier.curChallengeBattleNum == 0)
                gSaveBlock2Ptr->frontier.curChallengeBattleNum = 1;
            sWarpPending = true;
            SetWarpDestination(p->group, p->num, WARP_ID_NONE, 0, 0);
            DoWarp();
            ResetInitialPlayerAvatarState();
        }
        sFrames = 0;
        SDL_AtomicSet(&gGeoProbe.phase, GEO_WARPING);
        return true;
    case GEO_WARPING:
        if (++sFrames > GEO_ARRIVE_FRAMES)
        {
            gGeoProbe.arrivedGroup = gSaveBlock1Ptr->location.mapGroup;
            gGeoProbe.arrivedNum = gSaveBlock1Ptr->location.mapNum;
            SDL_strlcpy(gGeoProbe.failure, "the field was not given back", sizeof(gGeoProbe.failure));
            SDL_AtomicSet(&gGeoProbe.phase, GEO_ARRIVED);
            return true;
        }
        if (!FieldFree())
            return true;
        if (!sKeepWeather)
            SetCurrentAndNextWeatherNoDelay(WEATHER_NONE);
        /* the people stand still: the same picture from every camera (not the player: a surfer's blob is theirs,
           and the next warp moves them) */
        FreezeObjectEventsExceptOne(gPlayerAvatar.objectEventId);
        gGeoProbe.arrivedGroup = gSaveBlock1Ptr->location.mapGroup;
        gGeoProbe.arrivedNum = gSaveBlock1Ptr->location.mapNum;
        gGeoProbe.arrivedX = gSaveBlock1Ptr->pos.x;
        gGeoProbe.arrivedY = gSaveBlock1Ptr->pos.y;
        gGeoProbe.failure[0] = '\0';
        if (gGeoProbe.arrivedGroup != gGeoProbe.group || gGeoProbe.arrivedNum != gGeoProbe.num)
            SDL_strlcpy(gGeoProbe.failure, "a script took the player elsewhere", sizeof(gGeoProbe.failure));
        sFrames = 0;
        SDL_AtomicSet(&gGeoProbe.phase, GEO_ARRIVED);
        return true;
    case GEO_ARRIVED:
        /* the render side never got there (the field drawn in 2D, which syncs no 3D scene): on with the tour, the
           place without a line in results.tsv (the check reports it not inspected) */
        if (++sFrames > GEO_RENDER_FRAMES)
        {
            SDL_Log("Geometry probe: %s: the render side did not finish", gGeoProbe.key);
            SDL_AtomicSet(&gGeoProbe.timedOut, 1);
            SDL_AtomicSet(&gGeoProbe.phase, GEO_DONE);
        }
        return true;
    case GEO_DONE:
        sNext++;
        SDL_AtomicSet(&gGeoProbe.phase, GEO_IDLE);
        return true;
    default:
        return true;
    }
}

bool DioramaGeoProbe_TakeWarpCoords(int16_t *x, int16_t *y)
{
    if (!sWarpPending)
        return false;
    sWarpPending = false;
    *x = (int16_t)gGeoProbe.x;
    *y = (int16_t)gGeoProbe.y;
    return true;
}
#endif
