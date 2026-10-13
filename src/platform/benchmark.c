/*
 * --benchmark bookkeeping: see include/platform/benchmark.h. Project-owned.
 */
#include <stdlib.h>
#include <string.h>

#include "platform/benchmark.h"
#include "platform/display_settings.h"

/* The GBA buttons (gba/io_reg.h) pressed while booting: Start once (past the
 * intro), then A every few seconds (the title screen, Continue) until the 3D
 * field is drawn; then nothing more, so the field is left alone. */
#define BENCH_A_BUTTON 0x0001
#define BENCH_START_BUTTON 0x0008
#define BENCH_PRESS_MS 150
#define BENCH_START_AT_MS 8000
#define BENCH_A_FROM_MS 12000
#define BENCH_A_EVERY_MS 3000

static bool sActive;
static unsigned int sStartMs;
static unsigned int sBootEndMs; /* 0 until the field is drawn (or the boot timed out) */
static bool sSeen3d;
static int sSeconds;
static int sMeasuring = -1;  /* the resolution whose frames are being kept */
static double sTimes[BENCHMARK_MAX_FRAMES];
static int sCount;
static int sCount3d;
static unsigned int sFirstMs;
static unsigned int sLastMs;
static struct BenchmarkResult sResults[BENCHMARK_MAX_PHASES];
static int sResultCount;
static int sHeights[BENCHMARK_MAX_PHASES];
static int sPhaseCount;

void Benchmark_SetHeights(const int *heights, int count)
{
    sPhaseCount = 0;
    for (int i = 0; i < count && sPhaseCount < BENCHMARK_MAX_PHASES; i++)
    {
        if (heights[i] >= DISPLAY_MIN_HEIGHT && heights[i] <= DISPLAY_MAX_HEIGHT)
            sHeights[sPhaseCount++] = heights[i];
    }
}

static void DefaultHeights(void)
{
    if (sPhaseCount > 0)
        return;
    for (int i = 0; i < DISPLAY_RESOLUTION_COUNT; i++)
        sHeights[i] = gDisplayResolutions[i].height;
    sPhaseCount = DISPLAY_RESOLUTION_COUNT;
}

int Benchmark_PhaseCount(void)
{
    DefaultHeights();
    return sPhaseCount;
}

static unsigned int PhaseLengthMs(void)
{
    return BENCHMARK_WARMUP_MS + (unsigned int)sSeconds * 1000u;
}

/* The boot ends a moment after the first 3D frame, or when it takes too long. */
static bool BootEnded(unsigned int nowMs)
{
    if (sBootEndMs == 0 && nowMs - sStartMs >= BENCHMARK_BOOT_MAX_MS)
        sBootEndMs = nowMs;
    return sBootEndMs != 0 && (int)(nowMs - sBootEndMs) >= 0;
}

/* The resolution being drawn at this time, or -1 while booting or after the end. */
static int PhaseAt(unsigned int nowMs, bool *measuring)
{
    unsigned int elapsed;
    unsigned int inPhase;
    int phase;

    *measuring = false;
    if (!BootEnded(nowMs))
        return -1;
    elapsed = nowMs - sBootEndMs;
    phase = (int)(elapsed / PhaseLengthMs());
    if (phase >= Benchmark_PhaseCount())
        return -1;
    inPhase = elapsed % PhaseLengthMs();
    *measuring = inPhase >= BENCHMARK_WARMUP_MS;
    return phase;
}

void Benchmark_Start(unsigned int nowMs, int secondsPerResolution)
{
    sActive = true;
    sStartMs = nowMs;
    sBootEndMs = 0;
    sSeen3d = false;
    sSeconds = secondsPerResolution > 0 ? secondsPerResolution : BENCHMARK_DEFAULT_SECONDS;
    sMeasuring = -1;
    sCount = sCount3d = 0;
    sResultCount = 0;
    DefaultHeights();
}

int Benchmark_MeasuringPhase(unsigned int nowMs)
{
    bool measuring;
    int phase;

    if (!sActive)
        return -1;
    phase = PhaseAt(nowMs, &measuring);
    return measuring ? phase : -1;
}

bool Benchmark_Active(void)
{
    return sActive;
}

unsigned short Benchmark_FilterKeys(unsigned short keys, unsigned int nowMs)
{
    unsigned int elapsed = nowMs - sStartMs;

    if (!sActive || sSeen3d || sBootEndMs != 0)
        return keys;
    if (elapsed >= BENCH_START_AT_MS && elapsed < BENCH_START_AT_MS + BENCH_PRESS_MS)
        keys |= BENCH_START_BUTTON;
    if (elapsed >= BENCH_A_FROM_MS && (elapsed - BENCH_A_FROM_MS) % BENCH_A_EVERY_MS < BENCH_PRESS_MS)
        keys |= BENCH_A_BUTTON;
    return keys;
}

int Benchmark_RenderHeight(unsigned int nowMs)
{
    bool measuring;
    int phase;

    if (!sActive)
        return 0;
    phase = PhaseAt(nowMs, &measuring);
    return phase < 0 ? 0 : sHeights[phase];
}

static double Percentile(const double *sorted, int count, int perThousand)
{
    int index = (int)(((long long)count * perThousand) / 1000);

    return sorted[index < count ? index : count - 1];
}

void Benchmark_Summarize(double *frameMs, int count, double seconds, struct BenchmarkResult *result)
{
    int i;
    int j;

    /* Insertion sort is enough for a few thousand frames, and needs no qsort
     * callback across the platform's calling conventions. */
    for (i = 1; i < count; i++)
    {
        double value = frameMs[i];

        for (j = i - 1; j >= 0 && frameMs[j] > value; j--)
            frameMs[j + 1] = frameMs[j];
        frameMs[j + 1] = value;
    }
    result->frames = count;
    result->seconds = seconds;
    result->averageFps = seconds > 0.0 ? count / seconds : 0.0;
    result->stutters = 0;
    if (count == 0)
    {
        result->medianMs = result->p99Ms = result->worstMs = result->lowFps = 0.0;
        result->p90Ms = result->p95Ms = result->p999Ms = result->low01Fps = 0.0;
        return;
    }
    result->medianMs = frameMs[count / 2];
    result->p99Ms = frameMs[(count * 99) / 100 < count ? (count * 99) / 100 : count - 1];
    result->worstMs = frameMs[count - 1];
    result->lowFps = result->p99Ms > 0.0 ? 1000.0 / result->p99Ms : 0.0;
    result->p90Ms = Percentile(frameMs, count, 900);
    result->p95Ms = Percentile(frameMs, count, 950);
    result->p999Ms = Percentile(frameMs, count, 999);
    result->low01Fps = result->p999Ms > 0.0 ? 1000.0 / result->p999Ms : 0.0;
    {
        double limit = result->medianMs * 2.0;

        if (limit < result->medianMs + 8.0)
            limit = result->medianMs + 8.0;
        for (i = count - 1; i >= 0 && frameMs[i] > limit; i--)
            result->stutters++;
    }
}

static void FinishPhase(void)
{
    struct BenchmarkResult *result;

    if (sMeasuring < 0 || sResultCount >= BENCHMARK_MAX_PHASES)
        return;
    result = &sResults[sResultCount++];
    memset(result, 0, sizeof(*result));
    result->height = sHeights[sMeasuring];
    result->width = (sHeights[sMeasuring] * 16 + 8) / 9;
    Benchmark_Summarize(sTimes, sCount, sCount > 0 ? (sLastMs - sFirstMs) / 1000.0 : 0.0, result);
    result->frames3d = sCount3d;
    sMeasuring = -1;
    sCount = sCount3d = 0;
}

void Benchmark_Frame(unsigned int nowMs, double frameMs, bool frame3d)
{
    bool measuring;
    int phase;

    if (!sActive)
        return;
    if (frame3d && !sSeen3d)
    {
        sSeen3d = true;
        if (sBootEndMs == 0)
            sBootEndMs = nowMs + BENCHMARK_SETTLE_MS;
    }
    phase = PhaseAt(nowMs, &measuring);
    if (sMeasuring >= 0 && (phase != sMeasuring || !measuring))
        FinishPhase();
    if (phase < 0 || !measuring)
        return;
    if (sMeasuring < 0)
    {
        sMeasuring = phase;
        sFirstMs = nowMs;
    }
    if (sCount < BENCHMARK_MAX_FRAMES)
    {
        sTimes[sCount++] = frameMs;
        if (frame3d)
            sCount3d++;
    }
    sLastMs = nowMs;
}

bool Benchmark_Finished(unsigned int nowMs)
{
    if (!sActive || !BootEnded(nowMs))
        return false;
    if (nowMs - sBootEndMs < (unsigned int)Benchmark_PhaseCount() * PhaseLengthMs())
        return false;
    FinishPhase();
    return true;
}

int Benchmark_Results(struct BenchmarkResult *results, int max)
{
    int count = sResultCount < max ? sResultCount : max;

    memcpy(results, sResults, count * sizeof(*results));
    return count;
}

void Benchmark_WriteResults(FILE *file)
{
    fprintf(file, "resolution   frames  3D frames  average fps  1%% low fps  median ms  99th %% ms  worst ms\n");
    for (int i = 0; i < sResultCount; i++)
    {
        const struct BenchmarkResult *r = &sResults[i];

        fprintf(file, "%4dx%-4d   %6d  %9d  %11.1f  %10.1f  %9.2f  %9.2f  %8.2f\n",
                r->width, r->height, r->frames, r->frames3d, r->averageFps, r->lowFps,
                r->medianMs, r->p99Ms, r->worstMs);
    }
    fprintf(file, "\nframe times   90th %% ms  95th %% ms  99.9th %% ms  0.1%% low fps  stutters\n");
    for (int i = 0; i < sResultCount; i++)
    {
        const struct BenchmarkResult *r = &sResults[i];

        fprintf(file, "%4dx%-4d     %9.2f  %9.2f  %11.2f  %12.1f  %8d\n",
                r->width, r->height, r->p90Ms, r->p95Ms, r->p999Ms, r->low01Fps, r->stutters);
    }
}
