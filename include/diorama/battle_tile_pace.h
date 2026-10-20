/*
 * How often a 3D battle's water and flowers may redraw the kept world
 * (gl_battle.c): the step between redraws, chosen from measured frame times.
 * Plain arithmetic, no GL, so it is tested on its own
 * (tests/diorama/test_battle_tile_pace.c); used by src/diorama/gl/gl_battle.c.
 *
 * The glide's frames choose the first step. After it, each frame that redrew
 * the world for the tiles is measured: redraws are spaced twice as far apart
 * when such frames run over by a quarter, and twice as close after
 * TILE_PACE_GOOD_REDRAWS quick ones. Past TILE_PACE_STEP_MAX game frames the
 * water stands still; it is tried again after TILE_PACE_PROBE_FIRST game
 * frames, then twice as long after every slow try (up to TILE_PACE_PROBE_MAX),
 * so a PC whose frames were only briefly slow (a hitch, a save, a window drag)
 * gets its water back, while a slow one pays for one redraw a minute at most.
 * Each step change starts a fresh measurement: one slow moment counts once.
 */
#ifndef GUARD_DIORAMA_GL_BATTLE_TILE_PACE_H
#define GUARD_DIORAMA_GL_BATTLE_TILE_PACE_H

#include <stdbool.h>
#include <stdint.h>

#define TILE_PACE_STEP_MAX 32u
#define TILE_PACE_GOOD_REDRAWS 8u
#define TILE_PACE_PROBE_FIRST 120u    /* 2 s of game frames */
#define TILE_PACE_PROBE_MAX 3840u     /* 64 s */

struct TilePace
{
    int mode;              /* -1 by frame time, 0 off, 1 on */
    unsigned step;         /* game frames between redraws: 1 at every change, 0 none */
    unsigned goodRedraws;  /* redraw frames in a row within the allowance */
    unsigned probeEvery;   /* step 0: game frames before the next try */
    float pace;            /* redraw frames since the last step change, ms (moving average) */
};

/* A new battle: the mode (-1, 0 or 1) and no step yet. */
void TilePace_Begin(struct TilePace *pace, int mode);

/* The glide is over: its frames' average time (`count` of them) against what a
 * frame may take chooses the first step. */
void TilePace_Decide(struct TilePace *pace, float glideAverage, uint32_t count, float allowance);

/* Whether the tiles may make the world redraw now, `sinceRedraw` game frames
 * after they last did. */
bool TilePace_Due(const struct TilePace *pace, uint32_t sinceRedraw);

/* A frame that redrew the world for the tiles took `ms`. Returns true when the
 * step changed. */
bool TilePace_Redrawn(struct TilePace *pace, float ms, float allowance);

#endif
