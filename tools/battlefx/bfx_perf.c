/*
 * bfx_perf: times the Battle FX core in the game's own C host (src/diorama/battlefx/bfx_host.c,
 * QuickJS-ng), as the game runs it on the core's thread: a begin, then for every frame a step and its
 * frame packet. Prints a line per animation: the begin, the step and the frame packet (average, 95th
 * percentile, worst), and QuickJS's garbage collections.
 *
 *   bfx_perf <battlefx folder> MOVE_A,MOVE_B,... [repeats] [extra.js]
 *
 * <battlefx folder> is the game's data folder (bfx_core.js, bfx_moves.json). extra.js, when given, is
 * evaluated in the core's realm after it loads (an adapter or an experiment). The first repeat of each
 * animation includes QuickJS's first run through its code; the later ones are the steady state. With
 * BFX_PERF_REPORT set, the text extra.js's BFX_PERF_REPORT() returns is printed at the end.
 *
 * Built by tools/battlefx/build_perf.sh (Linux i386, the game's flags for QuickJS: -O2, SSE2 doubles).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diorama/battle_fx_host.h"

static double NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static char *ReadFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (data == NULL || fread(data, 1, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[n] = '\0';
    *size = (size_t)n;
    return data;
}

static int CompareDoubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

#define MAX_FRAMES 4000

int main(int argc, char **argv)
{
    static double steps[MAX_FRAMES], frames[MAX_FRAMES];
    char path[1024], request[512], list[4096];
    size_t coreSize, dataSize, extraSize = 0;
    char *core, *data, *extra = NULL, *save = NULL, *id;
    struct BfxHost *host;
    int repeats = argc > 3 ? atoi(argv[3]) : 2;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <battlefx folder> MOVE_A,MOVE_B,... [repeats] [extra.js]\n", argv[0]);
        return 2;
    }
    snprintf(path, sizeof(path), "%s/bfx_core.js", argv[1]);
    core = ReadFile(path, &coreSize);
    snprintf(path, sizeof(path), "%s/bfx_moves.json", argv[1]);
    data = ReadFile(path, &dataSize);
    if (argc > 4)
        extra = ReadFile(argv[4], &extraSize);
    if (core == NULL || data == NULL || (argc > 4 && extra == NULL))
    {
        fprintf(stderr, "cannot read the core, its data or %s\n", argc > 4 ? argv[4] : "");
        return 1;
    }
    host = BfxHost_Create();
    if (host == NULL || !BfxHost_Init(host, core, coreSize, data, dataSize, 512 * 1024)
     || !BfxHost_Settings(host, "{\"camera\":\"off\",\"palettes\":false,\"overlays\":false}")
     || (extra != NULL && !BfxHost_Eval(host, extra, extraSize, "extra.js")))
    {
        fprintf(stderr, "the core did not start: %s\n", host ? BfxHost_LastError(host) : "out of memory");
        return 1;
    }
    snprintf(list, sizeof(list), "%s", argv[2]);
    for (id = strtok_r(list, ",", &save); id != NULL; id = strtok_r(NULL, ",", &save))
    {
        for (int rep = 0; rep < repeats; rep++)
        {
            struct BfxBeginResult begun;
            struct BfxPacket packet;
            unsigned gc0 = BfxHost_GcCount(host);
            double t0, beginMs, stepTotal = 0, frameTotal = 0;
            int n = 0;

            snprintf(request, sizeof(request),
                     "{\"move\":\"%s\",\"attacker\":0,\"target\":1,\"double\":false,\"turn\":0,\"movePower\":80,"
                     "\"damage\":40,\"friendship\":255}", id);
            t0 = NowMs();
            if (!BfxHost_Begin(host, request, &begun))
            {
                printf("%s: refused (%s)\n", id, BfxHost_LastError(host));
                break;
            }
            beginMs = NowMs() - t0;
            for (;;)
            {
                double a = NowMs(), b, c;
                int r = BfxHost_Step(host);
                b = NowMs();
                if (r <= 0)
                    break;
                memset(&packet, 0, sizeof(packet));
                if (!BfxHost_Frame(host, -1, &packet))
                {
                    printf("%s: frame failed (%s)\n", id, BfxHost_LastError(host));
                    break;
                }
                BfxHost_DrainSounds(host);
                c = NowMs();
                if (n < MAX_FRAMES)
                {
                    steps[n] = b - a;
                    frames[n] = c - b;
                    n++;
                }
                stepTotal += b - a;
                frameTotal += c - b;
            }
            if (n == 0)
                continue;
            qsort(steps, (size_t)n, sizeof(double), CompareDoubles);
            qsort(frames, (size_t)n, sizeof(double), CompareDoubles);
            printf("%-24s rep %d: begin %7.1f ms; %4d frames; step %6.2f / %6.2f / %6.1f ms; frame packet %6.2f / %6.2f / "
                   "%6.1f ms (average / p95 / worst); %u garbage collections\n",
                   id, rep, beginMs, n, stepTotal / n, steps[(n * 95) / 100], steps[n - 1], frameTotal / n,
                   frames[(n * 95) / 100], frames[n - 1], BfxHost_GcCount(host) - gc0);
            fflush(stdout);
        }
    }
    if (getenv("BFX_PERF_REPORT") != NULL)
    {
        /* extra.js may keep its own numbers: BFX_PERF_REPORT() returns them as text */
        char *report = BfxHost_CallGlobal(host, "BFX_PERF_REPORT", NULL);
        if (report != NULL)
            printf("%s\n", report);
        free(report);
    }
    BfxHost_Destroy(host);
    return 0;
}
