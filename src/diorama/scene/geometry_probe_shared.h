#ifndef GUARD_DIORAMA_GEOMETRY_PROBE_SHARED_H
#define GUARD_DIORAMA_GEOMETRY_PROBE_SHARED_H

#include <SDL2/SDL.h>

/* The geometry probe's place, between its game side (geometry_probe_game.c, the game thread) and its render side
 * (geometry_probe.c, the render thread). The game side writes the place, then the phase; the render side reads the
 * phase, then the place. */
enum
{
    GEO_OFF,
    GEO_IDLE,       /* game side: the next place's warp is due */
    GEO_WARPING,    /* game side: waiting for the field on the place */
    GEO_ARRIVED,    /* render side: the cells and the pictures */
    GEO_DONE,       /* game side: on to the next place */
    GEO_FINISHED,
};

struct GeoProbeShared
{
    SDL_atomic_t phase;
    SDL_atomic_t timedOut;      /* the game side waited too long for the render side */
    char key[96];
    int index, group, num, x, y;
    int arrivedGroup, arrivedNum, arrivedX, arrivedY;
    char failure[96];           /* why the field is not the place asked for, or empty */
};

extern struct GeoProbeShared gGeoProbe;

#endif // GUARD_DIORAMA_GEOMETRY_PROBE_SHARED_H
