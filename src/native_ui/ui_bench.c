#ifdef ENABLE_NATIVE_UI
/*
 * PKMN_NATIVE_UI_BENCH=<results.csv>: the native UI's frame-time benchmark.
 * The same scripted visit (the title screen, the main menu, the field, the
 * pause menu, the bag, the party) runs with the native UI on and, in a second
 * run, off (PKMN_NATIVE_UI=0: the GBA menus), so the two runs' frame times give
 * the UI's cost on this computer. Each phase keeps every presented frame's
 * interval; with the UI on also the UI pass's CPU time and its GPU time (a GL
 * timer query).
 *
 * The script presses the game's own buttons, like a player, one press at a time
 * (held a few game frames, then let go), and waits for each screen to be up
 * before it measures it, so a slow start or a hitch never puts it out of step;
 * a screen that does not come up ends the visit (its phase and the ones after
 * are written as "not reached"). Every measured phase also says how much of it
 * the expected screen was really up (screen_ok_pct). Nothing is saved (the save
 * file is a copy the launcher made). A development tool: not in the launcher or
 * the settings. Project-owned.
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
    BENCH_TITLE,
    BENCH_MAIN_MENU,
    BENCH_FIELD,
    BENCH_PAUSE,
    BENCH_BAG,
    BENCH_PARTY,
    BENCH_PHASES,
};

static const char *const sPhaseNames[BENCH_PHASES] = {"title", "main_menu", "field", "pause_menu", "bag", "party"};
/* the native screen each phase measures (the field: none, the overworld's own) */
static const char *const sPhaseScreens[BENCH_PHASES] = {"title", "main_menu", NULL, "pause", "bag", "party"};

enum StepKind
{
    STEP_WAIT,         /* ms */
    STEP_PRESS,        /* one press of keys */
    STEP_PRESS_UNTIL,  /* keys every ms until the phase's screen is up (the intro) */
    STEP_SCREEN,       /* until the phase's screen is up; ms: the most it may take */
    STEP_MEASURE,      /* the phase, for ms */
    STEP_END,
};

struct BenchStep
{
    uint8_t kind;
    uint8_t phase;      /* STEP_SCREEN, STEP_PRESS_UNTIL, STEP_MEASURE: which screen */
    uint16_t keys;
    unsigned int ms;
};

static const struct BenchStep sSteps[] = {
    /* start-up: Start through the intro, then the title screen and the main menu (CONTINUE) */
    {STEP_PRESS_UNTIL, BENCH_TITLE, START_BUTTON, 3000},
    {STEP_WAIT, 0, 0, 2500},
    {STEP_MEASURE, BENCH_TITLE, 0, 5000},
    {STEP_PRESS, 0, A_BUTTON, 0},
    {STEP_SCREEN, BENCH_MAIN_MENU, 0, 15000},
    {STEP_WAIT, 0, 0, 1500},
    {STEP_MEASURE, BENCH_MAIN_MENU, 0, 5000},
    {STEP_PRESS, 0, A_BUTTON, 0},
    {STEP_SCREEN, BENCH_FIELD, 0, 30000},
    /* the field (the map's name sign goes first), the pause menu, the bag, the party */
    {STEP_WAIT, 0, 0, 3000},
    {STEP_MEASURE, BENCH_FIELD, 0, 8000},
    {STEP_PRESS, 0, START_BUTTON, 0},
    {STEP_SCREEN, BENCH_PAUSE, 0, 10000},
    {STEP_WAIT, 0, 0, 2000},
    {STEP_MEASURE, BENCH_PAUSE, 0, 8000},
    {STEP_PRESS, 0, DPAD_DOWN, 0},
    {STEP_WAIT, 0, 0, 300},
    {STEP_PRESS, 0, DPAD_DOWN, 0},
    {STEP_WAIT, 0, 0, 300},
    {STEP_PRESS, 0, A_BUTTON, 0},
    {STEP_SCREEN, BENCH_BAG, 0, 10000},
    {STEP_WAIT, 0, 0, 2500},
    {STEP_MEASURE, BENCH_BAG, 0, 8000},
    {STEP_PRESS, 0, B_BUTTON, 0},
    {STEP_SCREEN, BENCH_PAUSE, 0, 10000},
    {STEP_WAIT, 0, 0, 1000},
    {STEP_PRESS, 0, DPAD_UP, 0},
    {STEP_WAIT, 0, 0, 300},
    {STEP_PRESS, 0, A_BUTTON, 0},
    {STEP_SCREEN, BENCH_PARTY, 0, 10000},
    {STEP_WAIT, 0, 0, 2500},
    {STEP_MEASURE, BENCH_PARTY, 0, 8000},
    {STEP_PRESS, 0, B_BUTTON, 0},
    {STEP_SCREEN, BENCH_PAUSE, 0, 10000},
    {STEP_WAIT, 0, 0, 1000},
    {STEP_PRESS, 0, B_BUTTON, 0},
    {STEP_SCREEN, BENCH_FIELD, 0, 10000},
    {STEP_END, 0, 0, 0},
};

#define BENCH_HOLD_FRAMES 4    /* game frames a scripted button stays down */
#define BENCH_GAP_FRAMES 4     /* and up, before the next one */
#define BENCH_START_MS 3000    /* the window is up and the first frames are drawn */
#define BENCH_MAX_MS 240000    /* the whole visit, at most */

static bool sActive;
static bool sUiOn;
static char sOut[512];
static unsigned int sStartMs;
static int sStep;
static unsigned int sStepMs;       /* when the current step began */
static unsigned int sLastPressMs;  /* STEP_PRESS_UNTIL */
static bool sDone;
static int sFailedStep = -1;
static uint16_t sPressKey;         /* the button being pressed (0: none) */
static int sPressFrame;
static uint64_t sLastPresent;
static float *sTimes[BENCH_PHASES];
static float *sCpu[BENCH_PHASES];
static float *sGpu[BENCH_PHASES];
static int sCount[BENCH_PHASES];
static int sGpuCount[BENCH_PHASES];
static int sCheckFrames[BENCH_PHASES];
static int sCheckOk[BENCH_PHASES];
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
        fprintf(f, "ui,phase,status,width,height,frames,mean_ms,median_ms,p99_ms,max_ms,mean_fps,low1pct_fps,"
                   "ui_cpu_ms,ui_gpu_ms,screen_ok_pct,gpu\n");
    for (int p = 0; p < BENCH_PHASES; p++)
    {
        int n = sCount[p];
        double sum = 0, cpu = 0, gpu = 0;
        float median, p99, max;
        char gpuMs[24] = "";

        if (n == 0)
        {
            fprintf(f, "%s,%s,not reached,%d,%d,0,,,,,,,,,,\"%s (%s)\"\n", sUiOn ? "on" : "off", sPhaseNames[p], w, h,
                    renderer != NULL ? renderer : "?", version != NULL ? version : "?");
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
        if (sGpuCount[p] > 0)
            snprintf(gpuMs, sizeof(gpuMs), "%.3f", gpu / sGpuCount[p]);
        fprintf(f, "%s,%s,ok,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%s,%.0f,\"%s (%s)\"\n", sUiOn ? "on" : "off",
                sPhaseNames[p], w, h, n, sum / n, median, p99, max, sum > 0 ? 1000.0 * n / sum : 0,
                p99 > 0 ? 1000.0 / p99 : 0, cpu / n, gpuMs,
                sCheckFrames[p] > 0 ? 100.0 * sCheckOk[p] / sCheckFrames[p] : 0,
                renderer != NULL ? renderer : "?", version != NULL ? version : "?");
    }
    fclose(f);
    if (sFailedStep >= 0)
        SDL_Log("Native UI benchmark: done, the visit stopped at step %d (%s)", sFailedStep, sOut);
    else
        SDL_Log("Native UI benchmark: done (%s)", sOut);
}

/* the phase's screen is up: the field is the overworld with no menu of the UI's */
static bool PhaseScreenUp(int phase)
{
    const char *now = NativeUi_ScreenNow();

    if (sPhaseScreens[phase] == NULL)
        return now == NULL && gMain.callback2 == CB2_Overworld;
    return now != NULL && strcmp(now, sPhaseScreens[phase]) == 0;
}

static void Finish(void)
{
    sDone = true;
    sMeasuring = -1;
    WriteResults();
    NativeUi_QuitGame();
}

static void NextStep(unsigned int now)
{
    sStep++;
    sStepMs = now;
    sLastPressMs = 0;
}

/* frame boundary (game parked): the script's buttons */
uint16_t UiBench_Keys(uint16_t keys)
{
    unsigned int now;
    const struct BenchStep *step;
    uint16_t press = 0;

    if (!sActive || sDone)
        return keys;
    now = SDL_GetTicks();
    /* the press under way: held, then let go, before anything else happens */
    if (sPressKey != 0)
    {
        if (sPressFrame < BENCH_HOLD_FRAMES)
            press = sPressKey;
        if (++sPressFrame >= BENCH_HOLD_FRAMES + BENCH_GAP_FRAMES)
            sPressKey = 0;
        return keys | press;
    }
    if (now - sStartMs < BENCH_START_MS)
        return keys;
    if (sStepMs == 0)
        sStepMs = now;
    if (now - sStartMs > BENCH_MAX_MS && sFailedStep < 0)
    {
        SDL_Log("Native UI benchmark: the visit took too long (step %d)", sStep);
        sFailedStep = sStep;
        Finish();
        return keys;
    }
    step = &sSteps[sStep];
    sMeasuring = -1;
    switch (step->kind)
    {
    case STEP_WAIT:
        if (now - sStepMs >= step->ms)
            NextStep(now);
        break;
    case STEP_PRESS:
        sPressKey = step->keys;
        sPressFrame = 0;
        NextStep(now);
        break;
    case STEP_PRESS_UNTIL:
        if (PhaseScreenUp(step->phase))
            NextStep(now);
        else if (sLastPressMs == 0 || now - sLastPressMs >= step->ms)
        {
            sPressKey = step->keys;
            sPressFrame = 0;
            sLastPressMs = now;
        }
        else if (now - sStepMs > 90000)
        {
            SDL_Log("Native UI benchmark: the %s did not come up", sPhaseNames[step->phase]);
            sFailedStep = sStep;
            Finish();
        }
        break;
    case STEP_SCREEN:
        if (PhaseScreenUp(step->phase))
            NextStep(now);
        else if (now - sStepMs > step->ms)
        {
            SDL_Log("Native UI benchmark: the %s did not come up (step %d)", sPhaseNames[step->phase], sStep);
            sFailedStep = sStep;
            Finish();
        }
        break;
    case STEP_MEASURE:
        sMeasuring = step->phase;
        sCheckFrames[step->phase]++;
        if (PhaseScreenUp(step->phase))
            sCheckOk[step->phase]++;
        if (now - sStepMs >= step->ms)
        {
            sMeasuring = -1;
            NextStep(now);
        }
        break;
    default:
        Finish();
        break;
    }
    return keys;
}

/* every presented frame: its interval, and the UI pass's own CPU time */
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
