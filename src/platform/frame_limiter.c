/*
 * The frame rate limit: see include/platform/frame_limiter.h. Project-owned.
 */
#include "platform/frame_limiter.h"

void FrameLimiter_Reset(struct FrameLimiter *limiter)
{
    limiter->next = 0;
}

uint64_t FrameLimiter_Next(struct FrameLimiter *limiter, uint64_t now, uint64_t period)
{
    uint64_t start;

    if (period == 0)
    {
        limiter->next = 0;
        return 0;
    }
    if (limiter->next == 0 || now >= limiter->next + period)
    {
        /* The first frame, or a frame late by more than a period (a load, a
         * window being dragged): a new beat from now. */
        limiter->next = now + period;
        return 0;
    }
    start = limiter->next > now ? limiter->next : 0;
    limiter->next += period;
    return start;
}

#ifndef FRAME_LIMITER_UNIT_TEST
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

/* The last stretch is spun: a sleep may overshoot. With the precise timer
 * half a millisecond is left to spin; with a plain sleep (1 ms steps that can
 * take 2) two. */
#define SPIN_PRECISE_MS 0.5
#define SPIN_SLEEP_MS 2.0

void FrameLimiter_WaitUntil(uint64_t until)
{
    double frequency = (double)SDL_GetPerformanceFrequency();
#ifdef _WIN32
    /* Windows 10 1803 and later: a timer precise to well under a millisecond.
     * Made once; NULL where the system has none. */
    static HANDLE sTimer;
    static bool sTried;

    if (!sTried)
    {
        sTried = true;
        sTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
#endif
    for (;;)
    {
        uint64_t now = SDL_GetPerformanceCounter();
        double remainingMs;

        if (now >= until)
            return;
        remainingMs = (double)(until - now) * 1000.0 / frequency;
#ifdef _WIN32
        if (sTimer != NULL)
        {
            LARGE_INTEGER due;

            if (remainingMs <= SPIN_PRECISE_MS)
                break;
            /* relative, in 100 ns units */
            due.QuadPart = -(LONGLONG)((remainingMs - SPIN_PRECISE_MS) * 10000.0);
            if (SetWaitableTimer(sTimer, &due, 0, NULL, NULL, FALSE)
             && WaitForSingleObject(sTimer, (DWORD)remainingMs + 1) == WAIT_OBJECT_0)
                continue;
            sTimer = NULL; /* it failed once: plain sleeps from now on */
        }
#endif
        if (remainingMs - SPIN_SLEEP_MS < 1.0)
            break;
        SDL_Delay((Uint32)(remainingMs - SPIN_SLEEP_MS));
    }
    while (SDL_GetPerformanceCounter() < until)
    {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#endif
    }
}
#endif
