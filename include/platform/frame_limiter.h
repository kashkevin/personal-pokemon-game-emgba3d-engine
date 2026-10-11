#ifndef GUARD_PLATFORM_FRAME_LIMITER_H
#define GUARD_PLATFORM_FRAME_LIMITER_H

/*
 * The frame rate limit (display_settings.h, performance work 2026-10-11):
 * frames start on an even beat of 1/limit seconds, so they are spaced evenly
 * instead of bunching up behind the display. Only drawing waits: the game's
 * own 59.73 frames a second run from their own clock (sdl2.c's accumulator),
 * whatever the limit, as do the network and the sound.
 *
 * The beat logic is pure (tools/pc_port/tests checks it); the waiting uses the
 * system's most precise sleep, then a short spin for the last fraction of a
 * millisecond.
 */
#include <stdbool.h>
#include <stdint.h>

struct FrameLimiter
{
    uint64_t next; /* when the next frame may start (performance counter); 0: not started */
};

void FrameLimiter_Reset(struct FrameLimiter *limiter);
/*
 * Before a frame: the counter value to wait for (0: start at once), and the
 * beat moves on. `period` is the counter's ticks per frame (0: no limit). A
 * frame late by more than a whole period starts a new beat rather than
 * hurrying to catch up.
 */
uint64_t FrameLimiter_Next(struct FrameLimiter *limiter, uint64_t now, uint64_t period);
/* Waits until SDL_GetPerformanceCounter() reaches `until` (sdl2.c, frame_limiter.c). */
void FrameLimiter_WaitUntil(uint64_t until);

#endif
