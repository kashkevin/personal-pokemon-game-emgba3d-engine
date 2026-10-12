#ifdef ENABLE_NATIVE_UI
/*
 * PKMN_NATIVE_UI_BENCH=<results.csv>: the native UI's frame-time benchmark.
 * The same scripted visit (the field, the pause menu, the bag, the party) runs
 * with the native UI on and, in a second run, off (PKMN_NATIVE_UI=0: the GBA
 * menus), so the two runs' frame times give the UI's cost on this computer.
 * Each phase keeps every presented frame's interval; with the UI on also the
 * UI pass's CPU time and its GPU time (a GL timer query). The script presses
 * the game's own buttons, like a player; nothing is saved (the save file is a
 * copy the launcher made). A development tool: not in the launcher or the
 * settings. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "global.h"
#include "main.h"
#include "overworld.h"
#include "native_ui/native_ui.h"
#include "native_ui/ui_bench.h"
#include "native_ui/ui_screen.h"

#define BENCH_MAX_FRAMES 20000

enum BenchPhase
{
    BENCH_FIELD,
    BENCH_PAUSE,
    BENCH_BAG,
    BENCH_PARTY,
    BENCH_PHASES,
};

static const char *const sPhaseNames[BENCH_PHASES] = {"field", "pause_menu", "bag", "party"};

/* the visit after the field is reached: ms from then, the buttons pressed (one edge), the phase measured */
struct BenchStep
{
    unsigned int atMs;
    uint16_t keys;
    int measure;        /* the phase measured from atMs to the next step; -1: none */
};

static const struct BenchStep sSteps[] = {
    {0, 0, -1},
    {3000, 0, BENCH_FIELD},
    {11000, START_BUTTON, -1},
    {14000, 0, BENCH_PAUSE},
    {22000, DPAD_DOWN, -1},
    {22400, DPAD_DOWN, -1},
    {22800, A_BUTTON, -1},
    {26500, 0, BENCH_BAG},
    {34500, B_BUTTON, -1},
    {38500, DPAD_UP, -1},
    {38900, A_BUTTON, -1},
    {42500, 0, BENCH_PARTY},
    {50500, B_BUTTON, -1},
    {54500, B_BUTTON, -1},
    {57000, 0, -1},
};

#define BENCH_PRESS_MS 120
#define BENCH_BOOT_START_MS 8000   /* START once (past the intro) */
#define BENCH_BOOT_A_FROM_MS 12000 /* then A every few seconds (title, CONTINUE) */
#define BENCH_BOOT_A_EVERY_MS 3000
#define BENCH_BOOT_MAX_MS 120000

static bool sActive;
static bool sUiOn;
static char sOut[512];
static unsigned int sStartMs;
static unsigned int sFieldMs;      /* 0 until the field has been up for a moment */
static unsigned int sFieldSeenMs;
static int sStep = -1;
static bool sDone;
static uint64_t sLastPresent;
static float *sTimes[BENCH_PHASES];
static float *sCpu[BENCH_PHASES];
static float *sGpu[BENCH_PHASES];
static int sCount[BENCH_PHASES];
static int sGpuCount[BENCH_PHASES];
static int sMeasuring = -1;

bool UiBench_Active(void)
{
    return sActive;
}

void UiBench_Init(bool uiOn)
{
    const char *env = SDL_getenv("PKMN_NATIVE_UI_BENCH");

    if (env == NULL || env[0] == '\0')
        return;
    SDL_strlcpy(sOut, env, sizeof(sOut));
    for (int p = 0; p < BENCH_PHASES; p++)
    {
        sTimes[p] = calloc(BENCH_MAX_FRAMES, sizeof(float));
        sCpu[p] = calloc(BENCH_MAX_FRAMES, sizeof(float));
        sGpu[p] = calloc(BENCH_MAX_FRAMES, sizeof(float));
        if (sTimes[p] == NULL || sCpu[p] == NULL || sGpu[p] == NULL)
            return;
    }
    sActive = true;
    sUiOn = uiOn;
    sStartMs = SDL_GetTicks();
    SDL_Log("Native UI benchmark: UI %s, results to %s", uiOn ? "on" : "off", sOut);
}

static int CompareFloat(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;

    return x < y ? -1 : x > y;
}

static void WriteResults(void)
{
    FILE *f;
    bool header;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    int w = 0, h = 0;
    SDL_Window *window = SDL_GL_GetCurrentWindow();

    if (window != NULL)
        SDL_GL_GetDrawableSize(window, &w, &h);
    f = fopen(sOut, "r");
    header = f == NULL;
    if (f != NULL)
        fclose(f);
    f = fopen(sOut, "a");
    if (f == NULL)
    {
        SDL_Log("Native UI benchmark: cannot write %s", sOut);
        return;
    }
    if (header)
        fprintf(f, "ui,phase,width,height,frames,mean_ms,median_ms,p99_ms,max_ms,mean_fps,low1pct_fps,"
                   "ui_cpu_ms,ui_gpu_ms,gpu\n");
    for (int p = 0; p < BENCH_PHASES; p++)
    {
        int n = sCount[p];
        double sum = 0, cpu = 0, gpu = 0;
        float median, p99, max;

        if (n == 0)
        {
            fprintf(f, "%s,%s,%d,%d,0,,,,,,,,,\"%s\"\n", sUiOn ? "on" : "off", sPhaseNames[p], w, h,
                    renderer != NULL ? renderer : "?");
            continue;
        }
        for (int i = 0; i < n; i++)
        {
            sum += sTimes[p][i];
            cpu += sCpu[p][i];
        }
        for (int i = 0; i < sGpuCount[p]; i++)
            gpu += sGpu[p][i];
        qsort(sTimes[p], (size_t)n, sizeof(float), CompareFloat);
        median = sTimes[p][n / 2];
        p99 = sTimes[p][n - 1 - n / 100];
        max = sTimes[p][n - 1];
        char gpuMs[24] = "";

        if (sGpuCount[p] > 0)
            snprintf(gpuMs, sizeof(gpuMs), "%.3f", gpu / sGpuCount[p]);
        fprintf(f, "%s,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%s,\"%s (%s)\"\n", sUiOn ? "on" : "off",
                sPhaseNames[p], w, h, n, sum / n, median, p99, max, sum > 0 ? 1000.0 * n / sum : 0, p99 > 0 ? 1000.0 / p99 : 0,
                cpu / n, gpuMs, renderer != NULL ? renderer : "?", version != NULL ? version : "?");
    }
    fclose(f);
    SDL_Log("Native UI benchmark: done (%s)", sOut);
}

/* frame boundary (game parked): the script's buttons */
uint16_t UiBench_Keys(uint16_t keys)
{
    unsigned int now, t;
    uint16_t press = 0;

    if (!sActive || sDone)
        return keys;
    now = SDL_GetTicks();
    t = now - sStartMs;
    if (sFieldMs == 0)
    {
        /* booting: START once, then A every few seconds until the field has been up 3 s */
        if (gMain.callback2 == CB2_Overworld)
        {
            if (sFieldSeenMs == 0)
                sFieldSeenMs = now;
            else if (now - sFieldSeenMs >= 3000)
            {
                sFieldMs = now;
                sStep = 0;
            }
            return keys;
        }
        sFieldSeenMs = 0;
        if (t >= BENCH_BOOT_START_MS && t < BENCH_BOOT_START_MS + BENCH_PRESS_MS)
            press = START_BUTTON;
        else if (t >= BENCH_BOOT_A_FROM_MS && (t - BENCH_BOOT_A_FROM_MS) % BENCH_BOOT_A_EVERY_MS < BENCH_PRESS_MS)
            press = A_BUTTON;
        if (t > BENCH_BOOT_MAX_MS)
        {
            SDL_Log("Native UI benchmark: the field was not reached");
            sDone = true;
            WriteResults();
            NativeUi_QuitGame();
        }
        return keys | press;
    }
    t = now - sFieldMs;
    while (sStep + 1 < (int)(sizeof(sSteps) / sizeof(sSteps[0])) && t >= sSteps[sStep + 1].atMs)
        sStep++;
    sMeasuring = sSteps[sStep].measure;
    if (t - sSteps[sStep].atMs < BENCH_PRESS_MS)
        press = sSteps[sStep].keys;
    if (sStep == (int)(sizeof(sSteps) / sizeof(sSteps[0])) - 1)
    {
        sDone = true;
        sMeasuring = -1;
        WriteResults();
        NativeUi_QuitGame();
    }
    return keys | press;
}

/* every presented frame: its interval, and the UI pass's own times (gpuMs < 0: not known yet) */
void UiBench_Frame(float uiCpuMs)
{
    uint64_t now = SDL_GetPerformanceCounter();
    int p = sMeasuring;

    if (!sActive)
        return;
    if (sLastPresent != 0 && p >= 0 && sCount[p] < BENCH_MAX_FRAMES)
    {
        sTimes[p][sCount[p]] = (float)((now - sLastPresent) * 1000.0 / SDL_GetPerformanceFrequency());
        sCpu[p][sCount[p]] = uiCpuMs;
        sCount[p]++;
    }
    sLastPresent = now;
}

void UiBench_Gpu(float gpuMs)
{
    int p = sMeasuring;

    if (sActive && p >= 0 && sGpuCount[p] < BENCH_MAX_FRAMES)
        sGpu[p][sGpuCount[p]++] = gpuMs;
}
#endif // ENABLE_NATIVE_UI
