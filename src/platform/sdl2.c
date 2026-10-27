#ifdef PLATFORM_SDL2
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include "global.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/crash_report.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/mp_bridge.h"
#include "platform/shell_menu.h"
#include "platform/benchmark.h"
#include "platform/display_settings.h"
#include "platform/keybinds.h"
#include "platform/key_queue.h"
#include "platform/gamepad.h"
#include "platform/pc_menu.h"
#include "platform/png_write.h"
#include "platform/debug_scene.h"
#include "platform/memory_info.h"
#include "platform/frame_limiter.h"
#include "platform/game_speed.h"
#include "platform/battle_stat_stages.h"
#include "multiplayer_battle.h"
#include "fonts.h"
#include "constants/characters.h"
#include "multiplayer_save.h"
#include "malloc.h"
#if defined(DIORAMA_BATTLE_ENV_TEST) && !defined(ENABLE_DIORAMA)
#include "diorama/battle_env_test.h"   // the Classic 2D test build
#endif
#ifdef ENABLE_DIORAMA
#include "diorama/battle_env_test.h"
#include "diorama/battle_scene.h"
#include "diorama/camera.h"
#include "diorama/gl_battle.h"
#include "diorama/gl_compositor.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_pc_overlay.h"
#include "diorama/gl_quality.h"
#include "diorama/gl_target.h"
#include "diorama/gl_terrain_renderer.h"
#include "diorama/props.h"
#include "diorama/scene_snapshot.h"
#include "diorama/tree_sprites.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/native_ui.h"
#endif
#include "multiplayer_menu.h"
#endif

extern void (*const gIntrTable[])(void);

SDL_Thread *mainLoopThread;
SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
#if (defined(NATIVE_LINUX) || defined(_WIN32)) && !defined(ENABLE_DIORAMA)
#define MAX_BORDER_BACKGROUNDS 15
SDL_Texture *sdlBackgroundTextures[MAX_BORDER_BACKGROUNDS];
SDL_Texture *sdlBorderTexture;
#endif
static u8 sBorderBackgroundCount = 1;
SDL_AudioDeviceID sdlAudioDevice;
SDL_sem *vBlankSemaphore;
SDL_atomic_t isFrameAvailable;
// Update 30: the player-facing 2x speed (fast forward) was removed; timeScale stays 1.0 in play.
unsigned int videoScale = 1;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
// Battle speed (include/platform/game_speed.h; the owner's request of 2026-10-24): a battle between players runs this many
// times as fast. battleSpeed= in the settings file, 1 (normal) to GAME_SPEED_MAX; PKMN_BATTLE_SPEED overrides it (tests).
static unsigned sBattleSpeed = GAME_SPEED_DEFAULT;
static struct GameSpeed sGameSpeed;
static bool sTestSpeed; // PKMN_TEST_SPEED (test builds only): its own fixed speed, every frame counted as real

static char sSavePath[1024] = "pokeemerald-mp.sav";
static int sSoftResetChordFrames; // Ctrl+R holds the reset chord for the game to see
extern volatile bool8 gPlatformFlashDirty; // src/platform/agb_flash_pc.c
static char sConfigPath[1024] = "pokeemerald.cfg";
static u8 sBorderBackground;
static bool sHasBorderBackgroundConfig;
static u8 sBackgroundOrderVersion;
static u8 sPlatformSettings[PLATFORM_SETTING_COUNT] = {0, 4, 0, 1, 1, 10, 5};
// Display and graphics settings (include/platform/display_settings.h): the
// chosen resolution (1920x1080 unless the player chose another), windowed,
// borderless or fullscreen, and the 3D view's quality. fullscreen= and
// windowScale= of older files are read as before and turned into these.
static struct DisplaySettings sDisplay;
static int sLegacyFullscreen = -1;
static int sLegacyWindowScale = -1;
static struct PcMenuState sPcMenu; // F10: the PC settings menu (3D build)
static u16 keyboardKeys;           // the GBA buttons held on the keyboard (defined below)
static struct KeyQueue sKeyQueue; // what the keyboard did between game frames, one step per frame (key_queue.h)
static struct KeyQueue sPadQueue; // the same for the gamepad
#ifdef _WIN32
u16 GetXInputKeys(void);
static bool sPadConnected;        // the last read found a pad (the extra read between frames is made only then)
#endif

// Every change of the keyboard's buttons goes through here: the game frame takes them one step at a time, so a tap that
// begins and ends between two frames still reaches the game (Update 28).
static void KeyboardKeysChanged(void)
{
    KeyQueue_Set(&sKeyQueue, keyboardKeys);
}

// No button stays held for the game, and nothing pressed before reaches it later.
static void KeyboardKeysClear(void)
{
    keyboardKeys = 0;
    KeyQueue_Reset(&sKeyQueue);
}
static struct Keybinds sKeybinds;  // the keyboard bindings (key.* lines; include/platform/keybinds.h)
static bool sShowFrameRate;        // pokeemerald.cfg showFrameRate=1
static int sBenchmarkSeconds;      // --benchmark [seconds per resolution]
// --bench-name, --bench-log, --bench-csv, --bench-heights (include/platform/benchmark.h)
static char sBenchName[64] = "";
static char sBenchLogPath[1024] = "benchmark.log";
static char sBenchCsvPath[1024] = "";
#ifdef ENABLE_DIORAMA
// Each measured resolution's renderer counts and memory (performance work 2026-10-11).
struct BenchPhaseExtra
{
    bool taken;
    struct DioramaProfileStats stats;
    struct DioramaGpuMemory gpu;
    struct MemoryInfo memory;
    int renderWidth;
    int renderHeight;
};
static struct BenchPhaseExtra sBenchExtra[BENCHMARK_MAX_PHASES];
static int sBenchPhase = -1;
#endif
static Uint64 sLastPresentCounter;
static u32 sFrameRateFrames;
static Uint32 sFrameRateWindowMs;
static float sFrameRateShown;
#ifdef __ANDROID__
static SDL_GameController *androidController;
#endif

extern void AgbMain(void);
extern void DoSoftReset(void);

int DoMain(void *param);
void ProcessEvents(void);
static int SelfTestKeybinds(void);
void VDraw(SDL_Texture *texture);

static void ReadSaveFile(const char *path);
static void FlushSaveIfDirty(void);
static void ReadConfigFile(void);
static void ParseArguments(int argc, char **argv);

// Latched screen-reader hotkeys; see READER_KEY_* in platform.h.
static u16 sReaderKeys = 0;
static void StoreConfigFile(void);
static void ApplyPlatformSettings(void);
static void ApplyDisplaySettings(bool recenter);
#ifdef ENABLE_DIORAMA
static void ApplyMemoryBudget(void);
#endif
static void StoreSaveFile(void);
static void AfterPresent(void);

#ifdef ENABLE_DIORAMA
#define DIORAMA_VIDEO_SETTING_EVENT 1
static void ApplyDioramaVideoSetting(enum PlatformSetting setting, u8 value);
static void DrawPcOverlay(int outputWidth, int outputHeight);
#ifdef ENABLE_NATIVE_UI
static void DrawOverlays(int outputWidth, int outputHeight);
#endif
static bool HandlePcMenuKey(const SDL_KeyboardEvent *key);
static bool HandlePhotoKey(const SDL_KeyboardEvent *key);
#endif

#ifdef __ANDROID__
static void HandleTouchEvent(const SDL_TouchFingerEvent *event);
static void DrawTouchControls(void);
#endif

extern void NullTrap_Init(void);
extern void FaultCompat_Init(void);
extern void CrashReport_Init(void);
extern void GameMemory_Snapshot(void);
extern int NativeSelfTest_Decompress(const char *listPath);
extern int NativeSelfTest_PartyOrder(void);
extern int NativeSelfTest_BattleNames(void);
extern int NativeSelfTest_CryStop(void);
extern int NativeSelfTest_ShakeAnim(void);
extern int NativeSelfTest_LineBreak(void);
extern int NativeSelfTest_WildWeather(void);
extern int NativeSelfTest_Clock(void);
extern int NativeSelfTest_MpObjects(void);
extern int NativeSelfTest_CoopExp(void);
extern int NativeSelfTest_CoopController(void);
extern void MultiplayerCoop_TestArmAward(void);
extern int NativeSelfTest_SpawnClaims(void);
extern int NativeSelfTest_SecretBase(void);
extern int NativeSelfTest_Ladder(void);
extern int NativeSelfTest_DumpSpeciesData(const char *outPath);
extern int NativeSelfTest_Legend(void);
extern int NativeSelfTest_Shiny(void);
extern int NativeSelfTest_WildItems(void);
extern int NativeSelfTest_Ride(const char *sheetPath);
extern int NativeSelfTest_Carriers(const char *sheetPath);
extern int NativeSelfTest_FrontierSaves(void);
extern int NativeSelfTest_Coins(void);
extern int NativeSelfTest_Breeding(void);
extern int NativeSelfTest_Rival(void);
extern int NativeSelfTest_Update32(void);
extern int NativeSelfTest_HmBox(void);
extern int NativeSelfTest_LinkReplay(void);
extern int NativeSelfTest_StatStages(void);
extern int NativeSelfTest_MoveSwitch(void);
extern int NativeSelfTest_DumpLegendPool(const char *outPath);
extern int NativeSelfTest_Rewards(void);
extern int NativeSelfTest_CatchReconcile(void);
extern int NativeSelfTest_PokenavMap(void);
extern int NativeSelfTest_DumpWildWeights(const char *keysPath, const char *outPath);
extern int NativeSelfTest_DumpTowerData(const char *outPath);
extern int NativeSelfTest_Tower(void);
extern int NativeSelfTest_StartMenu(void);
extern int NativeSelfTest_SummaryIvEv(void);
extern bool32 GameMemory_Reset(void);

static jmp_buf sGameResetPoint; // where the game thread restarts after a soft reset

// Optional frame-rate log (PKMN_PERF_LOG=1): every 5 seconds, game frames and
// presented frames per second and the average time spent drawing and presenting.
static bool8 sPerfLog;
static u32 sPerfGameFrames;
// The sound queue's largest size in the log's window (bytes), and frames of sound left out or backlogs dropped
// (Platform_QueueAudio).
static Uint32 sAudioQueuedPeak;
static u32 sAudioDropped, sAudioCleared;
static u32 sPerfPresents;
static Uint64 sPerfPresentTicks;
static Uint64 sPerfWindowStart;

static void PerfLog_Init(void)
{
    const char *value = SDL_getenv("PKMN_PERF_LOG");

    sPerfLog = value != NULL && value[0] == '1';
    sPerfWindowStart = SDL_GetPerformanceCounter();
}

static void PerfLog_Presented(Uint64 start, Uint64 end)
{
    Uint64 frequency;
    double seconds;

    if (!sPerfLog)
        return;
    sPerfPresents++;
    sPerfPresentTicks += end - start;
    frequency = SDL_GetPerformanceFrequency();
    if (end - sPerfWindowStart < 5 * frequency)
        return;
    seconds = (double)(end - sPerfWindowStart) / frequency;
    SDL_Log("perf: %.1f game fps, %.1f presented fps, %.2f ms per present; sound queued up to %.0f ms, %u frames "
            "left out, %u backlogs dropped", sPerfGameFrames / seconds, sPerfPresents / seconds,
            (double)sPerfPresentTicks * 1000.0 / frequency / sPerfPresents,
            sAudioQueuedPeak * 1000.0 / (42060.0 * 2 * sizeof(float)), (unsigned)sAudioDropped, (unsigned)sAudioCleared);
    sAudioQueuedPeak = 0;
    sPerfGameFrames = 0;
    sPerfPresents = 0;
    sPerfPresentTicks = 0;
    sPerfWindowStart = end;
}

#ifndef ENABLE_DIORAMA
static bool sForceStoppedNotice; // test builds: PKMN_TEST_STOPPED_NOTICE=1 draws MS-125's notice, to check its drawing
#endif
#ifdef ENABLE_DIORAMA
// A live save holds the game for about 34 companion round trips of 2 frames
// each (a recovery's or a trade's import, about 1024), so its length followed
// the drawing rate: 26-31 s per save in the 3D view at 2.4 frames a second on
// a software renderer (app KNOWN_ISSUES M5). While the save holds the game and
// no menu is open nothing on screen changes (callbacks paused), so its frames
// run at 60 a second without being drawn, and the view is redrawn once a
// second (camera keys, a resized window). The save's frame deadlines (600
// frames between commands) keep their real-time meaning, as in mGBA.
// The save's commands before and after the hold still waited for the
// drawing: 4-6.5 s per save at 2.4 frames a second. When drawing one frame takes longer than SLOW_PRESENT_MS (the
// renderer cannot keep up), frames also run undrawn while the bridge has a
// save command in the game and for SAVE_ACTIVE_GRACE_MS after it, which
// spans the companion's turn between two commands. A renderer that keeps up
// draws every frame as before, so nothing changes on a graphics card.
#define HELD_REDRAW_MS 1000
#define SLOW_PRESENT_MS 25
#define SAVE_ACTIVE_GRACE_MS 250
static bool sHeldUnchanged; // sampled at the frame boundary, game thread parked
static Uint32 sLastPresentMs;
static Uint32 sLastPresentCostMs;
static Uint32 sSaveActiveUntilMs;

// The 3D view's look as pokeemerald.cfg keeps it: the camera mode (F7: the
// Adventure camera or the Diorama camera, diorama/camera.h), each mode's zoom
// and tilt steps, and the tree style (F8: the Diorama's voxel trees or the 3DS
// project's). PKMN_DIORAMA_CAMERA (adventure or diorama) and PKMN_DIORAMA_TREES
// (3ds, or anything else for the voxel trees) override the file for one run
// without being written to it, as the test scripts use them.
// The graphics overhaul (2026-10-10) replaced the camera styles of earlier
// builds (cameraStyle, cameraPitch, cameraZoom): those lines are not read, so
// every player starts in the Adventure camera (the 3DS framing the owner chose,
// eased and following the ground); the old wide view was the floating board.
// A mode or tree style is written only once it is the player's.
static u8 sCameraMode = DIORAMA_CAMERA_ADVENTURE;
static bool8 sTreeStyle3ds = TRUE;
static bool8 sCameraModeKept;
static bool8 sTreeStyleKept;
static int sCameraZoomSteps[2];  /* Adventure, Diorama */
static int sCameraTiltSteps[2];
static bool sCameraStepsDirty;
static Uint32 sCameraStepsChangedMs;
// Photo mode (F9): the game stands still, the camera moves, F12 saves the picture.
static bool sPhotoMode;
static bool sScreenshotRequested;
static char sScreenshotNotice[160];
static Uint32 sScreenshotNoticeMs;

static int EnvironmentStyle(const char *name, int fallback)
{
    const char *value = SDL_getenv(name);

    if (value == NULL || value[0] == '\0')
        return fallback;
    return SDL_strcasecmp(value, "3ds") == 0;
}

static int EnvironmentCameraMode(int fallback)
{
    const char *value = SDL_getenv("PKMN_DIORAMA_CAMERA");

    if (value == NULL || value[0] == '\0')
        return fallback;
    return SDL_strcasecmp(value, "diorama") == 0 ? DIORAMA_CAMERA_DIORAMA : DIORAMA_CAMERA_ADVENTURE;
}

// The world detail objects (diorama/props.h): the player's setting, unless
// PKMN_DIORAMA_PROPS=0 turns them off for this run.
static void ApplyDioramaDetails(void)
{
    const char *value = SDL_getenv("PKMN_DIORAMA_PROPS");

    DioramaProps_SetEnabled(sDisplay.details && !(value != NULL && value[0] == '0'));
}

static void ApplyDioramaLook(void)
{
    for (int mode = 0; mode < 2; mode++)
        DioramaGL_SetCameraSteps(mode, sCameraZoomSteps[mode], sCameraTiltSteps[mode]);
    DioramaGL_SetCameraMode(EnvironmentCameraMode(sCameraMode));
    DioramaTreeSprites_SetEnabled(EnvironmentStyle("PKMN_DIORAMA_TREES", sTreeStyle3ds));
}

// The zoom and tilt keys and the mouse wheel: written a second after the
// last change, so a turn of the wheel is one write. Photo mode's are its own.
static void KeepCameraSteps(void)
{
    int mode = DioramaGL_GetCameraMode();

    if (mode != DIORAMA_CAMERA_ADVENTURE && mode != DIORAMA_CAMERA_DIORAMA)
        return;
    DioramaGL_GetCameraSteps(mode, &sCameraZoomSteps[mode], &sCameraTiltSteps[mode]);
    sCameraStepsDirty = true;
    sCameraStepsChangedMs = SDL_GetTicks();
}

static void SetPhotoMode(bool on)
{
    if (on == sPhotoMode)
        return;
    sPhotoMode = on;
    paused = on;
    KeyboardKeysClear();
    DioramaGL_SetCameraMode(on ? DIORAMA_CAMERA_PHOTO : sCameraMode);
    SDL_Log("3D view: photo mode %s", on ? "on" : "off");
}
#endif

// A pause stops the game's frames, and with them the bridge to the companion (MpBridge_Frame runs at a frame's end):
// in a link battle or a held trade the other player and the host wait on this game, and a stop long enough ended the
// battle or the trade for both (2026-10-26, the owner's "battles freeze, stuck waiting"). Ctrl+P, the survey view (F5)
// and photo mode (F9) are refused then; ending a pause always works.
static bool LinkOperationActive(void)
{
    return MultiplayerBattle_IsActive() || MultiplayerSave_IsHeld();
}

static void RefusePause(void)
{
    SDL_Log("pause refused: a link battle or trade is under way");
#ifdef ENABLE_DIORAMA
    SDL_snprintf(sScreenshotNotice, sizeof(sScreenshotNotice), "No pausing during a link battle or trade.");
    sScreenshotNoticeMs = SDL_GetTicks();
#endif
}

// The game's own heap (src/malloc.c: the GBA's size, HEAP_SIZE): how much of it
// the game uses, sampled at frame boundaries while the game thread is parked,
// for the memory diagnostics - does the GBA's heap still hold (performance work
// 2026-10-11)? Read only; a list that does not look right is left alone.
static u32 sGameHeapNow;
static u32 sGameHeapPeak;

static void SampleGameHeap(void)
{
    const struct MemBlock *head = (const struct MemBlock *)gHeap;
    const struct MemBlock *block = head;
    u32 used = 0;
    unsigned guard = 0;

    if (head->magic != MALLOC_SYSTEM_ID)
        return;
    do
    {
        if ((const u8 *)block < gHeap || (const u8 *)block >= gHeap + HEAP_SIZE
         || block->magic != MALLOC_SYSTEM_ID)
            return;
        if (block->allocated)
            used += block->size + sizeof(struct MemBlock);
        block = block->next;
    } while (block != head && ++guard < 65536);
    sGameHeapNow = used;
    if (used > sGameHeapPeak)
        sGameHeapPeak = used;
}


// ---------------------------------------------------------------------------
// Graphics start-up check and failure report (the launcher's Graphics mode).
//
//   --probe-graphics   Checks, on a hidden throwaway window, what this build's
//                      renderer needs (the 3D build: an OpenGL 3.3 core context,
//                      every entry point and the compositor's shaders; the
//                      Classic 2D build: an SDL renderer), prints one JSON line
//                      on stdout and exits: 0 = usable, 78 = not usable. It
//                      reads no save or settings file and writes nothing.
//   When the same things fail at a normal start the game says so in a message
//   box and exits with 78 (it never closes silently).
//
// Test hooks: PKMN_GRAPHICS_FORCE_FAIL=<reason> makes both fail as if the
// renderer were unavailable; PKMN_GRAPHICS_NO_DIALOG=1 prints the message
// instead of showing a message box (unattended runs).
// ---------------------------------------------------------------------------
#define EXIT_GRAPHICS_UNAVAILABLE 78

#ifdef ENABLE_DIORAMA
#define GRAPHICS_MODE_ID "3d"
#define GRAPHICS_MODE_NAME "3D Diorama"
#else
#define GRAPHICS_MODE_ID "2d"
#define GRAPHICS_MODE_NAME "Classic 2D"
#endif

static const char *GraphicsForcedFailure(void)
{
    const char *value = SDL_getenv("PKMN_GRAPHICS_FORCE_FAIL");

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0 ? value : NULL;
}

// A JSON string body: quotes, backslashes and control characters made safe.
static void JsonText(const char *in, char *out, size_t size)
{
    size_t n = 0;

    for (; in != NULL && *in != '\0' && n + 7 < size; in++)
    {
        unsigned char c = (unsigned char)*in;

        if (c == '"' || c == '\\')
        {
            out[n++] = '\\';
            out[n++] = (char)c;
        }
        else if (c < 0x20 || c >= 0x7f)
            out[n++] = ' ';
        else
            out[n++] = (char)c;
    }
    out[n] = '\0';
}

static void PrintGraphicsProbe(bool supported, const char *reason, const char *detail,
                               const char *renderer, const char *version)
{
    char r[64], d[240], g[160], v[100];

    JsonText(reason, r, sizeof(r));
    JsonText(detail, d, sizeof(d));
    JsonText(renderer, g, sizeof(g));
    JsonText(version, v, sizeof(v));
    printf("{\"graphics\":\"%s\",\"supported\":%s,\"reason\":\"%s\",\"detail\":\"%s\","
           "\"renderer\":\"%s\",\"version\":\"%s\"}\n",
           GRAPHICS_MODE_ID, supported ? "true" : "false", r, d, g, v);
    fflush(stdout);
}

static int RunGraphicsProbe(void)
{
    SDL_Window *window;
    const char *forced = GraphicsForcedFailure();

    if (forced != NULL)
    {
        PrintGraphicsProbe(false, "forced", forced, "", "");
        return EXIT_GRAPHICS_UNAVAILABLE;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        PrintGraphicsProbe(false, "sdl_init", SDL_GetError(), "", "");
        return EXIT_GRAPHICS_UNAVAILABLE;
    }
#ifdef ENABLE_DIORAMA
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    window = SDL_CreateWindow("probe", 0, 0, 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (window == NULL)
    {
        PrintGraphicsProbe(false, "window", SDL_GetError(), "", "");
        SDL_Quit();
        return EXIT_GRAPHICS_UNAVAILABLE;
    }
    {
        struct DioramaGLProbe probe;
        bool ok = DioramaGL_Probe(window, &probe);

        PrintGraphicsProbe(ok, probe.reason, probe.detail, probe.renderer, probe.version);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : EXIT_GRAPHICS_UNAVAILABLE;
    }
#else
    window = SDL_CreateWindow("probe", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    if (window == NULL)
    {
        PrintGraphicsProbe(false, "window", SDL_GetError(), "", "");
        SDL_Quit();
        return EXIT_GRAPHICS_UNAVAILABLE;
    }
    {
        // The game asks for an accelerated renderer; any renderer, software
        // included, is enough for the Classic 2D picture.
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
        SDL_RendererInfo info;
        bool ok = renderer != NULL;

        if (!ok)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        ok = renderer != NULL;
        if (ok && SDL_GetRendererInfo(renderer, &info) == 0)
            PrintGraphicsProbe(true, "ok", "", info.name, "");
        else
            PrintGraphicsProbe(ok, ok ? "ok" : "renderer", ok ? "" : SDL_GetError(), "", "");
        if (renderer != NULL)
            SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : EXIT_GRAPHICS_UNAVAILABLE;
    }
#endif
}

// A normal start whose graphics cannot start: say so, never close silently.
static int ReportGraphicsFailure(const char *reason, const char *detail)
{
    char message[900];

    SDL_Log("Graphics unavailable (%s): %s", GRAPHICS_MODE_NAME, detail);
    SDL_snprintf(message, sizeof(message),
#ifdef ENABLE_DIORAMA
        "EmeraldTogether could not start its 3D graphics on this PC.\n\n"
        "Your game, save and progress are untouched.\n\n"
        "Close this message, then start the game again from the EmeraldTogether launcher and "
        "choose \"Launch in Classic 2D\" (or set Graphics mode to Classic 2D under Help > Game display). "
        "Updating your graphics driver may also make 3D work.\n\n"
#else
        "EmeraldTogether could not start its Classic 2D graphics on this PC.\n\n"
        "Your game, save and progress are untouched. Updating your graphics driver may fix this.\n\n"
#endif
        "Technical detail: %s (%s)", reason, detail);
    if (SDL_getenv("PKMN_GRAPHICS_NO_DIALOG") != NULL)
        fprintf(stderr, "%s\n", message);
    else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EmeraldTogether", message, NULL);
    return EXIT_GRAPHICS_UNAVAILABLE;
}

// A development aid (PKMN_DEBUG_LOG=FILE): the game's console output (stdout, stderr and SDL's log) goes into FILE, and on
// Windows no console window opens. A script that started the game reads what it printed there: Windows gives a game started
// that way no readable output (tools/pc_port/rival_check.py). Without the variable nothing changes.
static void LogToStderr(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;
    (void)priority;
    fprintf(stderr, "%s\n", message);
}

static bool OpenDebugLog(void)
{
    const char *path = getenv("PKMN_DEBUG_LOG");

    if (path == NULL || path[0] == '\0' || freopen(path, "w", stdout) == NULL)
        return false;
#ifdef _WIN32
    _dup2(_fileno(stdout), _fileno(stderr));
#else
    dup2(fileno(stdout), fileno(stderr));
#endif
    setvbuf(stdout, NULL, _IONBF, 0); // one file position for both, each line written at once
    setvbuf(stderr, NULL, _IONBF, 0);
    SDL_LogSetOutputFunction(LogToStderr, NULL);
    return true;
}

int main(int argc, char **argv)
{
    // PKMN_HIGH_ADDRESS_TEST=1 (Windows): everything after this lies above 2 GB
    // (platform/memory_info.h); first, so SDL and the driver are tested too.
    uint64_t highAddressReserved = MemoryInfo_HighAddressTest();

    FaultCompat_Init(); // GBA results for integer division by zero
    NullTrap_Init(); // development aid; no-op unless PKMN_NULL_TRAP=1 on Linux
    CrashReport_Init(); // writes crash.log if the game crashes
    for (int i = 1; i < argc; i++)
    {
        // Before the console below: stdout must stay the launcher's pipe.
        if (strcmp(argv[i], "--probe-graphics") == 0)
            return RunGraphicsProbe();
        // The pre-game menu (include/platform/shell_menu.h): no game, no ROM, no console; stdout is the application's pipe.
        if (strcmp(argv[i], "--shell") == 0)
            return ShellMenu_Main(argc, argv);
    }

    // Open an output console on Windows (none with PKMN_DEBUG_LOG: the output goes into that file)
#ifdef _WIN32
    if (!OpenDebugLog())
    {
        AllocConsole() ;
        AttachConsole( GetCurrentProcessId() ) ;
        freopen( "CON", "w", stdout ) ;
    }
#else
    OpenDebugLog();
#endif

#ifdef _WIN32
    // Physical pixels: a 1920x1080 window is 1920x1080 pixels at any Windows
    // display scaling, and Windows never stretches (blurs) the game's picture on
    // a high-DPI display. Per monitor: moving the window to another display keeps it sharp.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef __ANDROID__
    SDL_setenv("SDL_AUDIODRIVER", "openslES", 1);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO
#ifdef __ANDROID__
                | SDL_INIT_GAMECONTROLLER
#endif
                ) < 0)
    {
        DBGPRINTF("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return ReportGraphicsFailure("sdl_init", SDL_GetError());
    }

#ifdef __ANDROID__
    for (int i = 0; i < SDL_NumJoysticks() && androidController == NULL; i++)
    {
        if (SDL_IsGameController(i))
            androidController = SDL_GameControllerOpen(i);
    }
#endif

#ifdef __ANDROID__
    char *prefPath = SDL_GetPrefPath("pokeemerald", "pokeemerald");
    if (prefPath != NULL)
    {
        SDL_snprintf(sSavePath, sizeof(sSavePath), "%spokeemerald.sav", prefPath);
        SDL_snprintf(sConfigPath, sizeof(sConfigPath), "%spokeemerald.cfg", prefPath);
        SDL_free(prefPath);
    }
#endif
    ParseArguments(argc, argv);
    if (highAddressReserved != 0)
        SDL_Log("High address test: %.0f MB below 2 GB reserved; the rest of this run lies above it",
                highAddressReserved / 1048576.0);
    {
        struct MemoryInfo memory;
        char line[320];

        MemoryInfo_Get(&memory);
        MemoryInfo_Format(&memory, line, sizeof(line));
        SDL_Log("Memory: %s", line);
    }
    if (GraphicsForcedFailure() != NULL)
        return ReportGraphicsFailure("forced", GraphicsForcedFailure()); // before any save is read
    ReadSaveFile(sSavePath);
    ReadConfigFile();

#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#ifdef ENABLE_DIORAMA
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#endif
#if defined(NATIVE_LINUX) || defined(_WIN32)
    {
        // The chosen resolution, or the largest window of its shape the
        // display has room for; ApplyPlatformSettings then sets the display mode.
        SDL_Rect usable;
        int windowWidth = sDisplay.width;
        int windowHeight = sDisplay.height;

        if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
            DisplaySettings_FitWindow(sDisplay.width, sDisplay.height, usable.w - 16, usable.h - 48,
                                      &windowWidth, &windowHeight);
        sdlWindow = SDL_CreateWindow("Pokemon Emerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
#ifdef ENABLE_DIORAMA
                                     | SDL_WINDOW_OPENGL
#endif
                                     );
    }
#else
    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (sdlWindow == NULL)
    {
        DBGPRINTF("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return ReportGraphicsFailure("window", SDL_GetError());
    }

#ifndef ENABLE_DIORAMA
#ifdef __ANDROID__
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
#elif defined(NATIVE_LINUX)
    // The accelerated SDL backends on Linux retain pixels briefly while
    // presenting the 240x160 texture. Keep the classic development path crisp.
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
#else
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
#endif
    if (sdlRenderer == NULL)
    {
        DBGPRINTF("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return ReportGraphicsFailure("renderer", SDL_GetError());
    }

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif

    for (int i = 1; i < 15; i++)
    {
        char filename[16];
#ifdef _WIN32
        snprintf(filename, sizeof(filename), "BG%d.bmp", i);
#else
        snprintf(filename, sizeof(filename), "BG%d.bmp", i);
#endif
        SDL_RWops *backgroundFile = SDL_RWFromFile(filename, "rb");
        if (backgroundFile == NULL)
            break;
        SDL_RWclose(backgroundFile);
        sBorderBackgroundCount++;
    }
    if (sBackgroundOrderVersion < 2)
    {
        if (sHasBorderBackgroundConfig)
        {
            if (sBorderBackground == 1)
                sBorderBackground = sBorderBackgroundCount;
            else if (sBorderBackground >= 2)
                sBorderBackground--;
        }
        sBackgroundOrderVersion = 2;
        StoreConfigFile();
    }
#ifdef ENABLE_DIORAMA
    if (!DioramaGL_Init(sdlWindow, sBorderBackgroundCount))
    {
        const char *missing = DioramaGL_MissingFunction();
        char detail[128];

        SDL_snprintf(detail, sizeof(detail), missing[0] != '\0' ? "%s is missing" : "OpenGL setup failed", missing);
        DioramaGL_Shutdown();
        SDL_DestroyWindow(sdlWindow);
        SDL_Quit();
        return ReportGraphicsFailure("opengl", detail);
    }
    DioramaSnapshotExchange_Init();
    DioramaScene_Init();
    ApplyDioramaLook(); // before the game thread makes its first snapshot
    DioramaGLQuality_Set(DisplaySettings_ShadowMapSize(sDisplay.shadows), sDisplay.lighting, sDisplay.effects);
    ApplyDioramaDetails();
    // PKMN_DEBUG_CLASSIC=1 (a test switch): start in the classic 2D view, as F4 switches to it.
    if (SDL_getenv("PKMN_DEBUG_CLASSIC") != NULL)
        DioramaGL_ToggleEnabled();
#ifdef ENABLE_NATIVE_UI
    // The native PC interface (src/native_ui) over the game, then the PC
    // settings menu and the frame rate over everything.
    NativeUi_Init(sdlWindow);
    DioramaGL_SetPictureFilter(NativeUi_PictureFlags);
    DioramaGL_SetOverlay(DrawOverlays);
#else
    DioramaGL_SetOverlay(DrawPcOverlay); // the PC settings menu and the frame rate
#endif
#endif
#if defined(NATIVE_LINUX) || defined(_WIN32)
#ifdef ENABLE_DIORAMA
    DioramaGL_LoadArtwork();
#else
    SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);
    SDL_Surface *borderSurface = SDL_LoadBMP("Border.bmp");
    for (int i = 0; i < sBorderBackgroundCount; i++)
    {
        char filename[16];
        snprintf(filename, sizeof(filename), i == 0 ? "BG.bmp" : "BG%d.bmp", i);
        SDL_Surface *backgroundSurface = SDL_LoadBMP(filename);
        if (backgroundSurface == NULL)
            continue;
        sdlBackgroundTextures[i] = SDL_CreateTextureFromSurface(sdlRenderer, backgroundSurface);
        SDL_FreeSurface(backgroundSurface);
    }
    if (sdlBackgroundTextures[0] == NULL)
        SDL_Log("Background image could not be loaded: %s", SDL_GetError());
    if (borderSurface == NULL)
    {
        SDL_Log("Border image could not be loaded: %s", SDL_GetError());
    }
    else
    {
        sdlBorderTexture = SDL_CreateTextureFromSurface(sdlRenderer, borderSurface);
        SDL_FreeSurface(borderSurface);
    }
#endif
#else
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    SDL_RenderSetIntegerScale(sdlRenderer, SDL_TRUE);
#endif
    ApplyPlatformSettings();
#ifdef ENABLE_DIORAMA
    if (sBenchmarkSeconds > 0)
    {
        Benchmark_Start(SDL_GetTicks(), sBenchmarkSeconds);
        DioramaGL_SetVSync(false); // the GPU's own rate, not the display's (not saved)
        DioramaGLTarget_SetScale(100); // native: every pixel of each resolution drawn
        SDL_Log("Benchmark: %d s at each resolution once the 3D field is drawn (at most %d s to reach it); "
                "benchmark.log follows", sBenchmarkSeconds, BENCHMARK_BOOT_MAX_MS / 1000);
    }
#else
    if (sBenchmarkSeconds > 0)
        SDL_Log("Benchmark: only in the 3D build (pokeemerald-mp-diorama)");
#endif

#ifndef ENABLE_DIORAMA
    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (sdlTexture == NULL)
    {
        DBGPRINTF("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_NONE);
#if defined(DIORAMA_BATTLE_ENV_TEST)
    sForceStoppedNotice = SDL_getenv("PKMN_TEST_STOPPED_NOTICE") != NULL && SDL_atoi(SDL_getenv("PKMN_TEST_STOPPED_NOTICE")) != 0;
#endif
#endif

    simTime = curGameTime = lastGameTime = SDL_GetPerformanceCounter();

    isFrameAvailable.value = 0;
    vBlankSemaphore = SDL_CreateSemaphore(0);

    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 42060;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    cgb_audio_init(want.freq);


    sdlAudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (sdlAudioDevice == 0)
        SDL_Log("Failed to open audio: %s", SDL_GetError());
    else
    {
        if (want.format != AUDIO_F32) /* we let this one thing change. */
            SDL_Log("We didn't get Float32 audio format.");
        SDL_PauseAudioDevice(sdlAudioDevice, 0);
    }
#ifndef __ANDROID__
    VDraw(sdlTexture);
#endif
    // Everything the game can touch is now at its power-on state; remember it so
    // a soft reset can restore it (src/platform/game_memory.c).
    GameMemory_Snapshot();
    // The window and OpenGL are up: a driver may have put its own crash handler in by now (crash_report.c).
    CrashReport_Reinstall();
    // 8 MB of stack as on Linux (Windows' default is 1-2 MB; MS-71's investigation), with room kept for the report
    // of an overflow (DoMain).
    mainLoopThread = SDL_CreateThreadWithStackSize(DoMain, "AgbMain", 8u << 20, NULL);
    PerfLog_Init();
#if defined(DIORAMA_BATTLE_ENV_TEST)
    // Test builds only (never a player's): PKMN_TEST_SPEED=n runs the game's clock n times as fast, for the gameplay
    // checks that visit every trainer, person and warp (tools/qa/gameplay_check.py). The sound is not queued then.
    if (SDL_getenv("PKMN_TEST_SPEED") != NULL && atoi(SDL_getenv("PKMN_TEST_SPEED")) > 1)
    {
        timeScale = atoi(SDL_getenv("PKMN_TEST_SPEED"));
        sTestSpeed = true;
    }
#endif
    GameSpeed_Reset(&sGameSpeed);
    if (SDL_getenv("PKMN_BATTLE_SPEED") != NULL && atoi(SDL_getenv("PKMN_BATTLE_SPEED")) >= 1)
        sBattleSpeed = atoi(SDL_getenv("PKMN_BATTLE_SPEED"));

    double accumulator = 0.0;
    struct FrameLimiter frameLimiter;

    FrameLimiter_Reset(&frameLimiter);
    while (isRunning)
    {
#ifndef __ANDROID__
        // The frame rate limit (display_settings.h): frames start on an even
        // beat. Only the drawing waits; the game's frames below follow their
        // own clock (the accumulator) at any limit. The benchmark is unlimited.
        {
            unsigned int fpsCap = Benchmark_Active() ? 0 : sDisplay.fpsCap;
            uint64_t period = fpsCap > 0 ? SDL_GetPerformanceFrequency() / fpsCap : 0;
            uint64_t until = FrameLimiter_Next(&frameLimiter, SDL_GetPerformanceCounter(), period);

            if (until != 0)
                FrameLimiter_WaitUntil(until);
        }
#endif
        ProcessEvents();
#if defined(DIORAMA_BATTLE_ENV_TEST)
        DioramaBattleEnvTest_Watchdog(SDL_GetTicks());  // test builds only: a plan's game stuck inside a frame
#endif
#ifdef _WIN32
        // A pad button pressed and released while a slow frame was drawing still reaches the game (key_queue.h)
        if (!sPcMenu.open && sPadConnected)
            KeyQueue_Set(&sPadQueue, GetXInputKeys());
#endif
#ifdef ENABLE_DIORAMA
        if (sCameraStepsDirty && SDL_GetTicks() - sCameraStepsChangedMs >= 1000)
        {
            sCameraStepsDirty = false;
            StoreConfigFile();
        }
#endif

        if (!paused)
        {
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            curGameTime = SDL_GetPerformanceCounter();
            double deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
            // Up to five real frames of game time per drawn frame, at any speed (a battle running faster, PKMN_TEST_SPEED):
            // a slow drawn frame must not hold a sped-up game back to one frame each.
            double longest = fixedTimestep * 5;

            if (deltaTime > longest)
                deltaTime = dt;
            lastGameTime = curGameTime;

            accumulator += deltaTime;

            while (accumulator >= dt)
            {
                if (SDL_AtomicGet(&isFrameAvailable))
                {
#ifdef ENABLE_NATIVE_UI
                    // The game is parked: the native UI reads its state and
                    // decides what of the GBA picture it replaces.
                    NativeUi_CaptureFrame();
#endif
#ifdef ENABLE_DIORAMA
                    DioramaProfile_Begin(DIORAMA_PROF_TICK);
#endif
                    VDraw(sdlTexture);
#ifndef ENABLE_DIORAMA
                    SDL_RenderClear(sdlRenderer);
#if defined(NATIVE_LINUX) || defined(_WIN32)
                    u8 backgroundOption = Platform_GetBorderBackground();
                    if (backgroundOption < sBorderBackgroundCount
                     && sdlBackgroundTextures[backgroundOption] != NULL)
                        SDL_RenderCopy(sdlRenderer, sdlBackgroundTextures[backgroundOption], NULL, NULL);
                    int outputWidth;
                    int outputHeight;
                    SDL_GetRendererOutputSize(sdlRenderer, &outputWidth, &outputHeight);
                    // Whole-number steps, so the pixel art stays sharp at every
                    // window size (display_settings.h).
                    int scale = DisplaySettings_PixelScale(outputWidth, outputHeight,
                                                           sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE]);
                    int gameWidth = DISPLAY_WIDTH * scale;
                    int gameHeight = DISPLAY_HEIGHT * scale;
                    SDL_Rect gameViewport = {(outputWidth - gameWidth) / 2,
                                             (outputHeight - gameHeight) / 2,
                                             gameWidth, gameHeight};
                    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &gameViewport);
                    if (sPlatformSettings[PLATFORM_SETTING_BORDER] && sdlBorderTexture != NULL)
                    {
                        SDL_Rect borderSource = {141, 18, 1000, 683};
                        int innerWidth = gameViewport.w - 2;
                        int innerHeight = gameViewport.h - 2;
                        SDL_Rect borderViewport = {
                            gameViewport.x + 1 - innerWidth * 19 / 961,
                            gameViewport.y + 1 - innerHeight * 20 / 643,
                            innerWidth * 1000 / 961,
                            innerHeight * 683 / 643
                        };
                        SDL_RenderCopy(sdlRenderer, sdlBorderTexture, &borderSource, &borderViewport);
                    }
#else
                    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
#endif
#ifdef __ANDROID__
                    SDL_RenderPresent(sdlRenderer);
#endif
#endif
                    SDL_AtomicSet(&isFrameAvailable, 0);

                    REG_DISPSTAT |= INTR_FLAG_VBLANK;

                    RunDMAs(DMA_HBLANK);

#ifdef __ANDROID__
                    if (REG_IE & INTR_FLAG_VBLANK)
#else
                    if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)
#endif
                        gIntrTable[4]();
                    REG_DISPSTAT &= ~INTR_FLAG_VBLANK;

                    // Frame boundary: the game thread is parked in VBlankIntrWait,
                    // so the save image, the multiplayer mailboxes and the key
                    // register are safe to touch.
                    FlushSaveIfDirty();
                    MpBridge_Frame();
                    CrashReport_FrameBoundary();
                    // Battle speed: the coming game frame's (a battle between players runs faster). The sound of the
                    // frame just finished was made above, at its own speed.
                    if (!sTestSpeed)
                        timeScale = GameSpeed_Frame(&sGameSpeed, MultiplayerBattle_SpeedUp() ? sBattleSpeed : 1);
                    {
                        u16 keys = Platform_GetKeyInput();
                        if (sSoftResetChordFrames > 0)
                        {
                            keys |= A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON;
                            sSoftResetChordFrames--;
                        }
                        if (Benchmark_Active())
                            keys = Benchmark_FilterKeys(keys, SDL_GetTicks());
#ifdef ENABLE_NATIVE_UI
                        // mouse intents become the game's own button presses
                        keys = NativeUi_FilterKeys(keys);
#endif
#if defined(DIORAMA_BATTLE_ENV_TEST)
                        keys = DioramaBattleEnvTest_FilterKeys(keys, SDL_GetTicks());
#endif
                        // In multiplayer the reset chord reaches the game only when
                        // its native reset handler has been offered it.
                        keys = MpBridge_FilterKeys(keys);
                        keys = DebugScene_FilterKeys(keys);
                        REG_KEYINPUT = ~keys & KEYS_MASK;
                    }
#ifdef ENABLE_DIORAMA
                    if (MpBridge_SaveCommandPending())
                        sSaveActiveUntilMs = SDL_GetTicks() + SAVE_ACTIVE_GRACE_MS;
                    sHeldUnchanged = !MultiplayerMenu_IsActive()
                                  && (MultiplayerSave_SkipCallbacks()
                                      || (sLastPresentCostMs > SLOW_PRESENT_MS
                                          && !SDL_TICKS_PASSED(SDL_GetTicks(), sSaveActiveUntilMs)));
#endif

                    if (sPerfGameFrames % 30 == 0)
                        SampleGameHeap();
                    SDL_SemPost(vBlankSemaphore);
                    sPerfGameFrames++;
#ifdef ENABLE_DIORAMA
                    DioramaProfile_End(DIORAMA_PROF_TICK);
#endif

                    accumulator -= dt;
                }
            }
        }

#ifndef __ANDROID__
#ifdef ENABLE_DIORAMA
        if (sHeldUnchanged && SDL_GetTicks() - sLastPresentMs < HELD_REDRAW_MS)
        {
            SDL_Delay(1); // no vsync wait paces this loop; the accumulator paces the frames
            continue;
        }
#endif
        Uint64 presentStart = SDL_GetPerformanceCounter();
#ifdef ENABLE_DIORAMA
        double renderStep = fixedTimestep / timeScale;
        float frameAlpha = renderStep > 0.0 ? accumulator / renderStep : 1.0f;

        if (frameAlpha < 0.0f) frameAlpha = 0.0f;
        if (frameAlpha > 1.0f) frameAlpha = 1.0f;
        DioramaGL_Present(Platform_GetBorderBackground(),
                          sPlatformSettings[PLATFORM_SETTING_BORDER],
                          sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE],
                          frameAlpha);
        sLastPresentMs = SDL_GetTicks();
        sLastPresentCostMs = (Uint32)((SDL_GetPerformanceCounter() - presentStart) * 1000
                                      / SDL_GetPerformanceFrequency());
#else
        SDL_RenderPresent(sdlRenderer);
#endif
        PerfLog_Presented(presentStart, SDL_GetPerformanceCounter());
        AfterPresent();
#endif
    }

    FlushSaveIfDirty();
    MpBridge_Shutdown();
#ifdef ENABLE_DIORAMA
    if (sCameraStepsDirty)
        StoreConfigFile();
#endif

#ifdef ENABLE_DIORAMA
#ifdef ENABLE_NATIVE_UI
    NativeUi_Shutdown();
#endif
    DioramaGL_Shutdown();
    DioramaSnapshotExchange_Shutdown();
#elif defined(NATIVE_LINUX) || defined(_WIN32)
    for (int i = 0; i < sBorderBackgroundCount; i++)
        SDL_DestroyTexture(sdlBackgroundTextures[i]);
    SDL_DestroyTexture(sdlBorderTexture);
#endif
    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
    return 0;
}

static void ParseArguments(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--save") == 0 && i + 1 < argc)
            SDL_strlcpy(sSavePath, argv[++i], sizeof(sSavePath));
        // The settings file (window, sound, the 3D view's look). The launcher
        // starts every game in a new folder, so it names one that stays.
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            SDL_strlcpy(sConfigPath, argv[++i], sizeof(sConfigPath));
        else if (strcmp(argv[i], "--bridge") == 0 && i + 1 < argc)
        {
            // Multiplayer (src/platform/mp_bridge.c): never start without the
            // bridge that was asked for.
            if (!MpBridge_Configure(argv[++i]))
                exit(1);
        }
        // The launcher's character setup (draft_runtime.py): offline until the character is saved; the pause menu says so,
        // and the clock starts at 12:00 noon as a new world's does (siirtc_pc.c).
        else if (strcmp(argv[i], "--setup") == 0)
        {
            void SiiRtcPc_StartAtHour(int hour); // src/platform/siirtc_pc.c

            MpBridge_SetSetupSession();
            SiiRtcPc_StartAtHour(12);
        }
        // Hosting or joined (player_runtime.py), for the same link status.
        else if (strcmp(argv[i], "--link-role") == 0 && i + 1 < argc)
            MpBridge_SetRole(argv[++i]);
        // A measurement of the 3D view at each resolution (include/platform/benchmark.h).
        else if (strcmp(argv[i], "--benchmark") == 0)
        {
            sBenchmarkSeconds = BENCHMARK_DEFAULT_SECONDS;
            if (i + 1 < argc && SDL_atoi(argv[i + 1]) > 0)
                sBenchmarkSeconds = SDL_atoi(argv[++i]);
        }
        // The benchmark suite (tools/benchmark): a scene's name for the log, where the
        // log and a CSV row per resolution go, and which resolutions (heights, 16:9).
        else if (strcmp(argv[i], "--bench-name") == 0 && i + 1 < argc)
            SDL_strlcpy(sBenchName, argv[++i], sizeof(sBenchName));
        else if (strcmp(argv[i], "--bench-log") == 0 && i + 1 < argc)
            SDL_strlcpy(sBenchLogPath, argv[++i], sizeof(sBenchLogPath));
        else if (strcmp(argv[i], "--bench-csv") == 0 && i + 1 < argc)
            SDL_strlcpy(sBenchCsvPath, argv[++i], sizeof(sBenchCsvPath));
        else if (strcmp(argv[i], "--bench-heights") == 0 && i + 1 < argc)
        {
            int heights[BENCHMARK_MAX_PHASES];
            int count = 0;
            const char *cursor = argv[++i];

            while (*cursor != '\0' && count < BENCHMARK_MAX_PHASES)
            {
                heights[count++] = SDL_atoi(cursor);
                while (*cursor != '\0' && *cursor != ',')
                    cursor++;
                if (*cursor == ',')
                    cursor++;
            }
            Benchmark_SetHeights(heights, count);
        }
        else if (strcmp(argv[i], "--selftest-decompress") == 0 && i + 1 < argc)
            exit(NativeSelfTest_Decompress(argv[++i])); // src/platform/native_selftest.c
        else if (strcmp(argv[i], "--selftest-party-order") == 0)
            exit(NativeSelfTest_PartyOrder()); // src/platform/native_selftest.c (KNOWN_ISSUES N37)
        else if (strcmp(argv[i], "--selftest-battle-names") == 0)
            exit(NativeSelfTest_BattleNames()); // src/platform/native_selftest.c (KNOWN_ISSUES N38)
        else if (strcmp(argv[i], "--selftest-keybinds") == 0)
            exit(SelfTestKeybinds()); // the keyboard bindings through the real event loop
        else if (strcmp(argv[i], "--selftest-cry-stop") == 0)
            exit(NativeSelfTest_CryStop()); // src/platform/native_selftest.c (KNOWN_ISSUES N39)
        else if (strcmp(argv[i], "--selftest-shake-anim") == 0)
            exit(NativeSelfTest_ShakeAnim()); // src/platform/native_selftest.c (KNOWN_ISSUES N40)
        else if (strcmp(argv[i], "--selftest-line-break") == 0)
            exit(NativeSelfTest_LineBreak()); // src/platform/native_selftest.c (KNOWN_ISSUES U13)
        else if (strcmp(argv[i], "--selftest-wild-weather") == 0)
            exit(NativeSelfTest_WildWeather()); // src/platform/native_selftest.c (expansion Phase 2)
        else if (strcmp(argv[i], "--selftest-clock") == 0)
            exit(NativeSelfTest_Clock()); // src/platform/native_selftest.c (expansion Phase 11)
        else if (strcmp(argv[i], "--selftest-mp-objects") == 0)
            exit(NativeSelfTest_MpObjects()); // src/platform/native_selftest.c (KNOWN_ISSUES M15)
        else if (strcmp(argv[i], "--selftest-coop-exp") == 0)
            exit(NativeSelfTest_CoopExp()); // src/platform/native_selftest.c (co-op experience)
        else if (strcmp(argv[i], "--selftest-coop-controller") == 0)
            exit(NativeSelfTest_CoopController()); // src/platform/native_selftest.c (co-op AI controller blocks)
        else if (strcmp(argv[i], "--test-coop-exp-award") == 0)
            MultiplayerCoop_TestArmAward(); // the co-op experience award's screens, once (multiplayer_coop.c)
        else if (strcmp(argv[i], "--selftest-spawn-claims") == 0)
            exit(NativeSelfTest_SpawnClaims()); // src/platform/native_selftest.c (KNOWN_ISSUES M16-M19)
        else if (strcmp(argv[i], "--selftest-secret-base") == 0)
            exit(NativeSelfTest_SecretBase()); // src/platform/native_selftest.c (expansion Phase 6)
        else if (strcmp(argv[i], "--selftest-catch-reconcile") == 0)
            exit(NativeSelfTest_CatchReconcile()); // src/platform/native_selftest.c (World Event catch verdicts)
        else if (strcmp(argv[i], "--selftest-pokenav-map") == 0)
            exit(NativeSelfTest_PokenavMap()); // src/platform/native_selftest.c (Town Map markers)
        else if (strcmp(argv[i], "--selftest-rewards") == 0)
            exit(NativeSelfTest_Rewards()); // src/platform/native_selftest.c (Battle Frontier rewards over the wallet stream)
        else if (strcmp(argv[i], "--selftest-ladder") == 0)
            exit(NativeSelfTest_Ladder()); // src/platform/native_selftest.c (expansion Phases 8 and 10)
        else if (strcmp(argv[i], "--dump-legend-pool") == 0 && i + 1 < argc)
            exit(NativeSelfTest_DumpLegendPool(argv[++i])); // src/platform/native_selftest_u29.c (host parity)
        else if (strcmp(argv[i], "--selftest-breeding") == 0)
            exit(NativeSelfTest_Breeding()); // src/platform/native_selftest_u30.c (Update 30)
        else if (strcmp(argv[i], "--selftest-rival") == 0)
            exit(NativeSelfTest_Rival()); // src/platform/native_selftest_u31.c (Update 31)
        else if (strcmp(argv[i], "--selftest-update32") == 0)
            exit(NativeSelfTest_Update32()); // src/platform/native_selftest_u32.c (Update 32)
        else if (strcmp(argv[i], "--selftest-hm-box") == 0)
            exit(NativeSelfTest_HmBox()); // src/platform/native_selftest_hm.c (HMs from the PC boxes)
        else if (strcmp(argv[i], "--selftest-link-replay") == 0)
            exit(NativeSelfTest_LinkReplay()); // src/platform/native_selftest_replay.c (a link battle's replaying game)
        else if (strcmp(argv[i], "--selftest-stat-stages") == 0)
            exit(NativeSelfTest_StatStages()); // src/platform/native_selftest_stages.c (stat stages kept, shown, synced)
        else if (strcmp(argv[i], "--selftest-move-switch") == 0)
            exit(NativeSelfTest_MoveSwitch()); // src/platform/native_selftest_moves.c (move switching, MOVES)
        else if (strcmp(argv[i], "--selftest-coins") == 0)
            exit(NativeSelfTest_Coins()); // src/platform/native_selftest_u29.c (Update 29)
        else if (strcmp(argv[i], "--selftest-shiny") == 0)
            exit(NativeSelfTest_Shiny()); // src/platform/native_selftest_u29.c (Update 29)
        else if (strcmp(argv[i], "--selftest-wild-items") == 0)
            exit(NativeSelfTest_WildItems()); // src/platform/native_selftest_items.c (the 2026-10-27 wild item audit)
        else if (strcmp(argv[i], "--selftest-frontier-saves") == 0)
            exit(NativeSelfTest_FrontierSaves()); // native_selftest_frontier.c (saving during Frontier challenges)
        else if (strcmp(argv[i], "--selftest-ride") == 0)
            exit(NativeSelfTest_Ride(i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : NULL)); // native_selftest_ride.c (riding seats)
        else if (strcmp(argv[i], "--selftest-carriers") == 0)
            exit(NativeSelfTest_Carriers(i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : NULL)); // native_selftest_carriers.c (Surf, Dive, Fly)
        else if (strcmp(argv[i], "--selftest-legend") == 0)
            exit(NativeSelfTest_Legend()); // src/platform/native_selftest_u29.c (Update 29)
        else if (strcmp(argv[i], "--dump-species-data") == 0 && i + 1 < argc)
            exit(NativeSelfTest_DumpSpeciesData(argv[++i])); // src/platform/native_selftest_u29.c (data integrity)
        else if (strcmp(argv[i], "--dump-tower-data") == 0 && i + 1 < argc)
            exit(NativeSelfTest_DumpTowerData(argv[++i])); // src/platform/native_selftest_tower.c (Battle Tower: Legends)
        else if (strcmp(argv[i], "--selftest-tower") == 0)
            exit(NativeSelfTest_Tower()); // src/platform/native_selftest_tower.c (Battle Tower: Legends)
        else if (strcmp(argv[i], "--selftest-start-menu") == 0)
            exit(NativeSelfTest_StartMenu()); // src/platform/native_selftest_tower.c (MS-61)
        else if (strcmp(argv[i], "--selftest-summary-ivev") == 0)
            exit(NativeSelfTest_SummaryIvEv()); // src/platform/native_selftest_summary.c (own Pokemon's IVs and EVs)
        else if (strcmp(argv[i], "--dump-wild-weights") == 0 && i + 2 < argc)
        {
            const char *keysPath = argv[++i];
            exit(NativeSelfTest_DumpWildWeights(keysPath, argv[++i])); // the host's consistency check
        }
    }
}

// The save image is the GBA's 128 KiB flash, byte for byte, so .sav files from
// the emulator build load unchanged. A missing or short file reads as erased flash.
static void ReadSaveFile(const char *path)
{
    FILE *saveFile = fopen(path, "rb");
    size_t bytesRead = 0;

    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
    if (saveFile == NULL)
    {
        SDL_Log("No save file at %s; starting with erased flash", path);
        return;
    }
    bytesRead = fread(FLASH_BASE, 1, sizeof(FLASH_BASE), saveFile);
    fclose(saveFile);
    SDL_Log("Loaded %u bytes of save data from %s", (unsigned)bytesRead, path);
}

static const char *KeyCodeToName(int code)
{
    return SDL_GetKeyName((SDL_Keycode)code);
}

static int KeyNameToCode(const char *name)
{
    return (int)SDL_GetKeyFromName(name);
}

static const struct KeybindNames sKeyNames = {KeyCodeToName, KeyNameToCode};

#ifndef ENABLE_DIORAMA
// The 3D view's own settings (camera, trees, zoom, tilt) pass through a
// Classic 2D session unchanged: this build does not use them, but it rewrites
// the same settings file, and a player who switches back to 3D keeps their choices.
static char sKept3dLines[6][48];
static unsigned sKept3dCount;

static bool Keep3dLine(const char *line)
{
    static const char *const keys[] = { "cameraMode=", "treeStyle=", "adventureZoom=", "adventureTilt=",
                                        "dioramaZoom=", "dioramaTilt=" };

    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        if (strncmp(line, keys[i], strlen(keys[i])) == 0)
        {
            if (sKept3dCount < sizeof(sKept3dLines) / sizeof(sKept3dLines[0]))
                SDL_snprintf(sKept3dLines[sKept3dCount++], sizeof(sKept3dLines[0]), "%s", line);
            return true;
        }
    }
    return false;
}
#endif

static void ReadConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "r");
    char line[64];
    unsigned int value;
#ifdef ENABLE_DIORAMA
    int steps;
#endif

    DisplaySettings_Defaults(&sDisplay);
    Keybinds_Defaults(&sKeybinds);
    if (configFile == NULL)
        return;
    while (fgets(line, sizeof(line), configFile) != NULL)
    {
        if (DisplaySettings_ParseLine(&sDisplay, line) || Keybinds_ParseLine(&sKeybinds, line, &sKeyNames))
            continue;
        if (sscanf(line, "showFrameRate=%u", &value) == 1)
            sShowFrameRate = value != 0;
        else if (sscanf(line, "borderBackground=%u", &value) == 1 && value < 16)
        {
            sBorderBackground = value;
            sHasBorderBackgroundConfig = true;
        }
        else if (sscanf(line, "backgroundOrder=%u", &value) == 1)
            sBackgroundOrderVersion = value;
        else if (sscanf(line, "fullscreen=%u", &value) == 1)
        {
            sPlatformSettings[PLATFORM_SETTING_FULLSCREEN] = value != 0;
            sLegacyFullscreen = value != 0;
        }
        else if (sscanf(line, "windowScale=%u", &value) == 1 && value >= 2 && value <= 6) // 6 = 1920x1080
        {
            sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE] = value;
            sLegacyWindowScale = value;
        }
        else if (sscanf(line, "integerScale=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE] = value != 0;
        else if (sscanf(line, "vsync=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_VSYNC] = value != 0;
        else if (sscanf(line, "border=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_BORDER] = value != 0;
        else if (sscanf(line, "volume=%u", &value) == 1 && value <= 10)
            sPlatformSettings[PLATFORM_SETTING_VOLUME] = value;
        else if (sscanf(line, "cryVolume=%u", &value) == 1 && value <= 10)
            sPlatformSettings[PLATFORM_SETTING_CRY_VOLUME] = value;
        else if (sscanf(line, "battleSpeed=%u", &value) == 1 && value >= 1 && value <= GAME_SPEED_MAX)
            sBattleSpeed = value;
#ifndef ENABLE_DIORAMA
        else if (Keep3dLine(line))
            ; // kept for the next 3D session
#endif
#ifdef ENABLE_DIORAMA
        else if (strncmp(line, "cameraMode=", 11) == 0)
        {
            sCameraMode = strncmp(line + 11, "diorama", 7) == 0 ? DIORAMA_CAMERA_DIORAMA
                                                                : DIORAMA_CAMERA_ADVENTURE;
            sCameraModeKept = TRUE;
        }
        else if (sscanf(line, "treeStyle=%u", &value) == 1)
        {
            sTreeStyle3ds = value == 1;
            sTreeStyleKept = TRUE;
        }
        else if (sscanf(line, "adventureZoom=%d", &steps) == 1)
            sCameraZoomSteps[DIORAMA_CAMERA_ADVENTURE] = steps;
        else if (sscanf(line, "adventureTilt=%d", &steps) == 1)
            sCameraTiltSteps[DIORAMA_CAMERA_ADVENTURE] = steps;
        else if (sscanf(line, "dioramaZoom=%d", &steps) == 1)
            sCameraZoomSteps[DIORAMA_CAMERA_DIORAMA] = steps;
        else if (sscanf(line, "dioramaTilt=%d", &steps) == 1)
            sCameraTiltSteps[DIORAMA_CAMERA_DIORAMA] = steps;
#endif
    }
    fclose(configFile);
    DisplaySettings_Finish(&sDisplay);
    Keybinds_Finish(&sKeybinds);
    // A file written before resolution= and displayMode= existed.
    DisplaySettings_ApplyLegacy(&sDisplay, sLegacyFullscreen, sLegacyWindowScale);
    sPlatformSettings[PLATFORM_SETTING_FULLSCREEN] = sDisplay.mode != DISPLAY_MODE_WINDOWED;
}

static void StoreConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "w");

    if (configFile == NULL)
        return;
    fprintf(configFile, "borderBackground=%u\n", sBorderBackground);
    fprintf(configFile, "backgroundOrder=2\n");
    // A resolution or display mode waiting for the player's confirmation (the
    // settings menu) is not written yet: the file keeps the one before it.
    struct DisplaySettings stored = sDisplay;

    if (sPcMenu.open && sPcMenu.confirming)
    {
        stored.width = sPcMenu.previous.width;
        stored.height = sPcMenu.previous.height;
        stored.mode = sPcMenu.previous.mode;
        stored.fullscreenMode = sPcMenu.previous.fullscreenMode;
        stored.resolutionKept = sPcMenu.previous.resolutionKept;
        stored.modeKept = sPcMenu.previous.modeKept;
    }
    // fullscreen= and windowScale= for builds older than resolution= (the
    // borderless kind was their fullscreen; windowScale only for the sizes it had).
    fprintf(configFile, "fullscreen=%u\n", stored.mode != DISPLAY_MODE_WINDOWED);
    if (stored.resolutionKept && DisplaySettings_LegacyWindowScale(&stored) != 0)
        fprintf(configFile, "windowScale=%u\n", DisplaySettings_LegacyWindowScale(&stored));
    DisplaySettings_Write(&stored, configFile);
    Keybinds_Write(&sKeybinds, configFile, &sKeyNames);
    if (sShowFrameRate)
        fprintf(configFile, "showFrameRate=1\n");
    fprintf(configFile, "integerScale=%u\n", sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE]);
    fprintf(configFile, "vsync=%u\n", sPlatformSettings[PLATFORM_SETTING_VSYNC]);
    fprintf(configFile, "border=%u\n", sPlatformSettings[PLATFORM_SETTING_BORDER]);
    fprintf(configFile, "volume=%u\n", sPlatformSettings[PLATFORM_SETTING_VOLUME]);
    fprintf(configFile, "cryVolume=%u\n", sPlatformSettings[PLATFORM_SETTING_CRY_VOLUME]);
    if (sBattleSpeed != GAME_SPEED_DEFAULT)
        fprintf(configFile, "battleSpeed=%u\n", sBattleSpeed);
#ifdef ENABLE_DIORAMA
    if (sCameraModeKept)
        fprintf(configFile, "cameraMode=%s\n", sCameraMode == DIORAMA_CAMERA_DIORAMA ? "diorama" : "adventure");
    if (sTreeStyleKept)
        fprintf(configFile, "treeStyle=%u\n", sTreeStyle3ds ? 1 : 0);
    fprintf(configFile, "adventureZoom=%d\n", sCameraZoomSteps[DIORAMA_CAMERA_ADVENTURE]);
    fprintf(configFile, "adventureTilt=%d\n", sCameraTiltSteps[DIORAMA_CAMERA_ADVENTURE]);
    fprintf(configFile, "dioramaZoom=%d\n", sCameraZoomSteps[DIORAMA_CAMERA_DIORAMA]);
    fprintf(configFile, "dioramaTilt=%d\n", sCameraTiltSteps[DIORAMA_CAMERA_DIORAMA]);
#else
    for (unsigned i = 0; i < sKept3dCount; i++)
        fprintf(configFile, "%s%s", sKept3dLines[i], strchr(sKept3dLines[i], '\n') != NULL ? "" : "\n");
#endif
    fclose(configFile);
}

static void ApplyPlatformSettings(void)
{
#ifdef ENABLE_DIORAMA
    DioramaGL_SetVSync(sPlatformSettings[PLATFORM_SETTING_VSYNC]);
#else
    SDL_RenderSetVSync(sdlRenderer, sPlatformSettings[PLATFORM_SETTING_VSYNC]);
#endif
    ApplyDisplaySettings(true);
}

// The window as the display settings ask. Windowed: the chosen resolution, or
// the largest window of its shape the display has room for; the 3D scene is
// drawn at the window's own size (and follows a resized window). Borderless:
// the window covers the display at the display's own size and the 3D scene is
// drawn at the chosen resolution (at the display's shape) and scaled to it.
// Fullscreen: the display switches to the chosen resolution (its closest mode
// at the desktop's refresh rate); if it has none, borderless instead. The
// pixel-art picture is always drawn at the window's own resolution in
// whole-number steps (gl_compositor.c), so it stays sharp in every mode.
static void ApplyDisplaySettings(bool recenter)
{
#if defined(NATIVE_LINUX) || defined(_WIN32)
    int display = SDL_GetWindowDisplayIndex(sdlWindow);

    if (display < 0)
        display = 0;
    sPlatformSettings[PLATFORM_SETTING_FULLSCREEN] = sDisplay.mode != DISPLAY_MODE_WINDOWED;
    if (sDisplay.mode == DISPLAY_MODE_FULLSCREEN)
    {
        SDL_DisplayMode want = {0};
        SDL_DisplayMode desktop;
        SDL_DisplayMode closest;

        want.w = sDisplay.width;
        want.h = sDisplay.height;
        if (SDL_GetDesktopDisplayMode(display, &desktop) == 0)
            want.refresh_rate = desktop.refresh_rate;
        if (SDL_GetClosestDisplayMode(display, &want, &closest) != NULL
         && SDL_SetWindowDisplayMode(sdlWindow, &closest) == 0
         && SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN) == 0)
        {
            SDL_Log("Display: fullscreen %dx%d at %d Hz", closest.w, closest.h, closest.refresh_rate);
        }
        else
        {
            SDL_Log("Display: no %dx%d mode on this display (%s); borderless instead",
                    sDisplay.width, sDisplay.height, SDL_GetError());
            SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
    }
    else if (sDisplay.mode == DISPLAY_MODE_BORDERLESS)
    {
        SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else
    {
        SDL_Rect usable;
        int top = 0;
        int left = 0;
        int bottom = 0;
        int right = 0;
        int width;
        int height;

        SDL_SetWindowFullscreen(sdlWindow, 0);
        if (SDL_GetDisplayUsableBounds(display, &usable) != 0)
            usable.w = usable.h = 0;
        if (SDL_GetWindowBordersSize(sdlWindow, &top, &left, &bottom, &right) != 0)
            top = left = bottom = right = 0;
        if (top == 0 && usable.w > 0)
        {
            // The frame is not known yet (the window was just made): room for one.
            top = 40;
            left = right = bottom = 8;
        }
        DisplaySettings_FitWindow(sDisplay.width, sDisplay.height,
                                  usable.w - left - right, usable.h - top - bottom, &width, &height);
        SDL_SetWindowSize(sdlWindow, width, height);
        if (recenter)
            SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        SDL_Log("Display: windowed %dx%d%s", width, height,
                width != sDisplay.width || height != sDisplay.height ? " (the chosen size did not fit)" : "");
    }
#endif
#ifdef ENABLE_DIORAMA
    // The 3D scene: the window's own size when windowed, the chosen resolution
    // in the fullscreen modes; multisampled if the quality asks.
    DioramaGLTarget_Configure(sDisplay.mode == DISPLAY_MODE_WINDOWED ? 0 : sDisplay.height,
                              sDisplay.antialiasing);
    // ... at the render scale (100: native); the benchmark measures native.
    DioramaGLTarget_SetScale(Benchmark_Active() ? 100 : sDisplay.renderScale);
    ApplyMemoryBudget();
#endif
}

#ifdef ENABLE_DIORAMA
static void ApplyDioramaVideoSetting(enum PlatformSetting setting, u8 value)
{
    if (setting == PLATFORM_SETTING_VSYNC)
    {
        DioramaGL_SetVSync(value);
    }
    else if (setting == PLATFORM_SETTING_FULLSCREEN || setting == PLATFORM_SETTING_WINDOW_SCALE)
    {
        ApplyDisplaySettings(true);
    }
}
#endif

#ifdef ENABLE_DIORAMA
// ---------------------------------------------------------------------------
// The PC settings menu (F10; include/platform/pc_menu.h): what it changed is
// applied at once; a resolution or display mode is written to pokeemerald.cfg
// only once the player keeps it.
// The memory budget (display_settings.h): the caches it sizes - today the 3D
// terrain's chunk cache. The address space free once the game, SDL and the
// graphics driver are loaded is measured once, the first time.
static unsigned int sMemoryEffectiveMB;

static void ApplyMemoryBudget(void)
{
    static unsigned int sAddressFreeMB;
    static unsigned int sPhysicalMB;
    struct MemoryInfo memory;

    if (sPhysicalMB == 0 && MemoryInfo_Get(&memory))
    {
        sPhysicalMB = (unsigned int)(memory.physicalTotal / 1048576);
        sAddressFreeMB = (unsigned int)(memory.addressSpaceAvailable / 1048576);
    }
    sMemoryEffectiveMB = DisplaySettings_EffectiveMemoryBudget(sDisplay.memoryBudget, sPhysicalMB,
                                                               sAddressFreeMB);
    DioramaGLTerrain_SetCacheChunks(DisplaySettings_TerrainCacheChunks(sMemoryEffectiveMB));
}

// The F10 menu's memory row: the budget in effect and the game's memory now.
static void UpdateMenuMemory(bool now)
{
    static Uint32 sLastMs;
    struct MemoryInfo memory;

    if (!now && SDL_GetTicks() - sLastMs < 500)
        return;
    sLastMs = SDL_GetTicks();
    sPcMenu.memoryEffectiveMB = sPcMenu.settings.memoryBudget == sDisplay.memoryBudget ? sMemoryEffectiveMB : 0;
    if (MemoryInfo_Get(&memory))
        sPcMenu.memoryInUseMB = (unsigned int)(memory.processResident / 1048576);
}

static void ApplyPcMenuResult(unsigned int bits)
{
    bool store = false;

    if (bits & PC_MENU_CHANGED_DISPLAY)
    {
        sDisplay.width = sPcMenu.settings.width;
        sDisplay.height = sPcMenu.settings.height;
        sDisplay.mode = sPcMenu.settings.mode;
        sDisplay.fullscreenMode = sPcMenu.settings.fullscreenMode;
        sDisplay.resolutionKept = sPcMenu.settings.resolutionKept;
        sDisplay.modeKept = sPcMenu.settings.modeKept;
        ApplyDisplaySettings(true);
    }
    if (bits & PC_MENU_DISPLAY_KEPT)
        store = true;
    if (bits & PC_MENU_CHANGED_QUALITY)
    {
        sDisplay.quality = sPcMenu.settings.quality;
        sDisplay.shadows = sPcMenu.settings.shadows;
        sDisplay.lighting = sPcMenu.settings.lighting;
        sDisplay.effects = sPcMenu.settings.effects;
        sDisplay.details = sPcMenu.settings.details;
        sDisplay.antialiasing = sPcMenu.settings.antialiasing;
        sDisplay.qualityKept = true;
        DioramaGLQuality_Set(DisplaySettings_ShadowMapSize(sDisplay.shadows), sDisplay.lighting, sDisplay.effects);
        ApplyDioramaDetails();
        DioramaGLTarget_Configure(sDisplay.mode == DISPLAY_MODE_WINDOWED ? 0 : sDisplay.height,
                                  sDisplay.antialiasing);
        store = true;
    }
    if (bits & PC_MENU_CHANGED_VSYNC)
    {
        sPlatformSettings[PLATFORM_SETTING_VSYNC] = sPcMenu.vsync;
        if (!Benchmark_Active())
            DioramaGL_SetVSync(sPcMenu.vsync);
        store = true;
    }
    if (bits & PC_MENU_CHANGED_FPS_CAP)
    {
        sDisplay.fpsCap = sPcMenu.settings.fpsCap;
        sDisplay.fpsCapKept = true;
        SDL_Log("Display: frame rate limit %s", sDisplay.fpsCap == 0 ? "off" : "on");
        store = true;
    }
    if (bits & PC_MENU_CHANGED_MEMORY_BUDGET)
    {
        sDisplay.memoryBudget = sPcMenu.settings.memoryBudget;
        sDisplay.memoryBudgetKept = true;
        ApplyMemoryBudget();
        UpdateMenuMemory(true);
        store = true;
    }
    if (bits & PC_MENU_CHANGED_RENDER_SCALE)
    {
        sDisplay.renderScale = sPcMenu.settings.renderScale;
        sDisplay.renderScaleKept = true;
        if (!Benchmark_Active())
            DioramaGLTarget_SetScale(sDisplay.renderScale);
        store = true;
    }
    if (bits & PC_MENU_CHANGED_PICTURE)
    {
        sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE] = sPcMenu.pictureFill;
        store = true;
    }
    if (bits & PC_MENU_CHANGED_FRAME_RATE)
    {
        sShowFrameRate = sPcMenu.showFrameRate;
        store = true;
    }
    if (bits & PC_MENU_CHANGED_CAMERA)
    {
        // As F7 does.
        sCameraMode = sPcMenu.cameraDiorama ? DIORAMA_CAMERA_DIORAMA : DIORAMA_CAMERA_ADVENTURE;
        DioramaGL_SetCameraMode(sCameraMode);
        sCameraModeKept = TRUE;
        store = true;
    }
    if (bits & PC_MENU_CHANGED_TREES)
    {
        // As F8 does.
        sTreeStyle3ds = sPcMenu.trees3ds;
        sTreeStyleKept = TRUE;
        DioramaTreeSprites_SetEnabled(sTreeStyle3ds);
        store = true;
    }
    if (bits & PC_MENU_CHANGED_KEYS)
    {
        // New bindings: no old key may stay held (its release would now find another action).
        sKeybinds = sPcMenu.keybinds;
        KeyboardKeysClear();
        store = true;
    }
    if (store)
        StoreConfigFile();
}

static bool HandlePhotoKey(const SDL_KeyboardEvent *key)
{
    if (!sPhotoMode)
        return false;
    switch (key->keysym.sym)
    {
    case SDLK_UP:
        DioramaGL_PhotoPan(0, 1);
        return true;
    case SDLK_DOWN:
        DioramaGL_PhotoPan(0, -1);
        return true;
    case SDLK_LEFT:
        DioramaGL_PhotoPan(-1, 0);
        return true;
    case SDLK_RIGHT:
        DioramaGL_PhotoPan(1, 0);
        return true;
    case SDLK_ESCAPE:
    case SDLK_x:
    case SDLK_BACKSPACE:
        if (key->repeat == 0)
            SetPhotoMode(false);
        return true;
    case SDLK_COMMA:
    case SDLK_PERIOD:
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS:
    case SDLK_F9:
    case SDLK_F12:
    case SDLK_F11:
        return false; // the camera's own keys, below
    default:
        return true; // nothing reaches the game while it stands still
    }
}

static bool HandlePcMenuKey(const SDL_KeyboardEvent *key)
{
    enum PcMenuInput input;

    if (key->keysym.sym == SDLK_F10)
    {
        if (key->repeat != 0)
            return true;
        if (sPcMenu.open)
        {
            // F10 closes; a display change not kept yet goes back first.
            if (sPcMenu.confirming)
                ApplyPcMenuResult(PcMenu_Input(&sPcMenu, PC_MENU_INPUT_BACK, SDL_GetTicks()));
            ApplyPcMenuResult(PcMenu_Input(&sPcMenu, PC_MENU_INPUT_BACK, SDL_GetTicks()));
        }
        else
        {
            PcMenu_Open(&sPcMenu, &sDisplay, sPlatformSettings[PLATFORM_SETTING_VSYNC],
                        sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE], sShowFrameRate, true,
                        sCameraMode == DIORAMA_CAMERA_DIORAMA, DioramaTreeSprites_Enabled());
            PcMenu_SetKeybinds(&sPcMenu, &sKeybinds, &sKeyNames);
            UpdateMenuMemory(true);
            KeyboardKeysClear(); // no button stays held for the game while the menu is open
        }
        return true;
    }
    if (!sPcMenu.open)
        return false;
    // Controls page, waiting for the key to bind: every key but Esc (cancel) and F11 (fullscreen,
    // as everywhere) is the answer. F10 was handled above, so the menu can always be left.
    if (PcMenu_IsCapturing(&sPcMenu) && key->keysym.sym != SDLK_F11)
    {
        if (key->repeat != 0)
            return true;
        if (key->keysym.sym == SDLK_ESCAPE)
            ApplyPcMenuResult(PcMenu_Input(&sPcMenu, PC_MENU_INPUT_BACK, SDL_GetTicks()));
        else
            ApplyPcMenuResult(PcMenu_CaptureKey(&sPcMenu, (int)key->keysym.sym));
        return true;
    }
    switch (key->keysym.sym)
    {
    case SDLK_UP:
        input = PC_MENU_INPUT_UP;
        break;
    case SDLK_DOWN:
        input = PC_MENU_INPUT_DOWN;
        break;
    case SDLK_LEFT:
        input = PC_MENU_INPUT_LEFT;
        break;
    case SDLK_RIGHT:
        input = PC_MENU_INPUT_RIGHT;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_z:
    case SDLK_SPACE:
        if (key->repeat != 0)
            return true;
        input = PC_MENU_INPUT_ACCEPT;
        break;
    case SDLK_ESCAPE:
    case SDLK_x:
    case SDLK_BACKSPACE:
        if (key->repeat != 0)
            return true;
        input = PC_MENU_INPUT_BACK;
        break;
    case SDLK_F11:
        return false; // fullscreen still switches
    default:
        return true; // nothing else reaches the game while the menu is open
    }
    ApplyPcMenuResult(PcMenu_Input(&sPcMenu, input, SDL_GetTicks()));
    return true;
}

// F12: the window's picture as a PNG in the screenshots folder beside the
// game (created when needed), named by the date and time.
static void SaveScreenshot(int width, int height)
{
    char path[96];
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    uint8_t *pixels;

    if (width <= 0 || height <= 0)
        return;
    pixels = malloc((size_t)width * height * 4);
    if (pixels == NULL)
        return;
#ifdef _WIN32
    _mkdir("screenshots");
#else
    mkdir("screenshots", 0755);
#endif
    if (local != NULL)
        strftime(path, sizeof(path), "screenshots/emerald-%Y%m%d-%H%M%S", local);
    else
        SDL_snprintf(path, sizeof(path), "screenshots/emerald-%u", (unsigned)SDL_GetTicks());
    {
        /* Several in one second: -2, -3, ... rather than over the last one. */
        size_t stem = strlen(path);

        for (int copy = 1; copy < 100; copy++)
        {
            SDL_RWops *existing;

            if (copy == 1)
                SDL_snprintf(path + stem, sizeof(path) - stem, ".png");
            else
                SDL_snprintf(path + stem, sizeof(path) - stem, "-%d.png", copy);
            existing = SDL_RWFromFile(path, "rb");
            if (existing == NULL)
                break;
            SDL_RWclose(existing);
        }
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (PngWrite_RGBA(path, pixels, width, height, true))
    {
        SDL_snprintf(sScreenshotNotice, sizeof(sScreenshotNotice), "Picture saved: %s", path);
        SDL_Log("Screenshot: %s (%dx%d)", path, width, height);
    }
    else
    {
        SDL_snprintf(sScreenshotNotice, sizeof(sScreenshotNotice), "The picture could not be saved.");
        SDL_Log("Screenshot: could not write %s", path);
    }
    sScreenshotNoticeMs = SDL_GetTicks();
    free(pixels);
}

static void DrawPcOverlay(int outputWidth, int outputHeight)
{
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DioramaOverlayRow view[PC_MENU_ITEM_COUNT];
    struct DioramaOverlayMenu menu;
    char corner[96];
    bool showCorner = sShowFrameRate || Benchmark_Active();

    if (sPcMenu.open)
    {
        menu.count = PcMenu_Rows(&sPcMenu, rows);
        for (int i = 0; i < menu.count; i++)
        {
            view[i].label = rows[i].label;
            view[i].value = rows[i].value;
            view[i].selected = rows[i].selected;
            view[i].adjustable = rows[i].adjustable;
        }
        menu.title = PcMenu_Title(&sPcMenu);
        menu.message = PcMenu_Message(&sPcMenu);
        menu.backLabel = PcMenu_BackLabel(&sPcMenu);
        menu.rows = view;
        menu.confirmSeconds = PcMenu_ConfirmSecondsLeft(&sPcMenu, SDL_GetTicks());
    }
    if (showCorner)
    {
        int width;
        int height;
        int samples;

        DioramaGLTarget_GetLastSize(&width, &height, &samples);
        SDL_snprintf(corner, sizeof(corner), "%s%.0f fps   3D %dx%d%s", Benchmark_Active() ? "Benchmark   " : "",
                     sFrameRateShown, width, height, samples > 0 ? " MSAA" : "");
    }
    if (DebugScene_WantGlShot())
        sScreenshotRequested = true;
#if defined(DIORAMA_BATTLE_ENV_TEST) && defined(ENABLE_DIORAMA)
    // Test builds only: a PKMN_TEST_FIELD_SHOTS picture off a battle, the window as drawn (a menu's native screen too)
    {
        const char *stem = DioramaBattleEnvTest_FieldShotStem();
        uint8_t *pixels = stem != NULL && outputWidth > 0 && outputHeight > 0
                        ? malloc((size_t)outputWidth * outputHeight * 4) : NULL;

        if (pixels != NULL)
        {
            char path[640];

            SDL_snprintf(path, sizeof(path), "%s.png", stem);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, outputWidth, outputHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            PngWrite_RGBA(path, pixels, outputWidth, outputHeight, true);
            free(pixels);
            DioramaBattleEnvTest_CaptureWritten();
        }
    }
#endif
#if defined(DIORAMA_BATTLE_ENV_TEST)
    if (DioramaBattleEnvTest_WantWindowShot())
        sScreenshotRequested = true; // test builds only: the stat stage checks' pictures, battle HUD and all
#endif
    // F12: the picture as drawn, before the PC's own interface goes over it.
    if (sScreenshotRequested)
    {
        sScreenshotRequested = false;
        SaveScreenshot(outputWidth, outputHeight);
    }
    {
        struct DioramaOverlayExtras extras;

        extras.photoHints = sPhotoMode;
        extras.notice = sScreenshotNotice[0] != '\0' && SDL_GetTicks() - sScreenshotNoticeMs < 4000
                      ? sScreenshotNotice : NULL;
        // KNOWN_ISSUES MS-125: a battle between players that stopped itself waits for the launcher, which says so in
        // its own window only; the frozen battle on screen said nothing.
        if (extras.notice == NULL && MultiplayerBattle_StoppedForRecovery())
            extras.notice = "This battle stopped. In the launcher: Recover battle, or Abandon battle.";
        DioramaPCOverlay_Draw(sPcMenu.open ? &menu : NULL, showCorner ? corner : NULL, &extras,
                              outputWidth, outputHeight);
    }
}

#ifdef ENABLE_NATIVE_UI
static void DrawOverlays(int outputWidth, int outputHeight)
{
    NativeUi_Draw(outputWidth, outputHeight);
    DrawPcOverlay(outputWidth, outputHeight);
}
#endif

static void CpuName(char *buffer, size_t size)
{
    SDL_strlcpy(buffer, "unknown", size);
#ifdef _WIN32
    {
        HKEY key;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                          0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            char name[128];
            DWORD length = sizeof(name) - 1;
            DWORD type = 0;

            if (RegQueryValueExA(key, "ProcessorNameString", NULL, &type, (LPBYTE)name, &length) == ERROR_SUCCESS
             && type == REG_SZ)
            {
                name[length < sizeof(name) ? length : sizeof(name) - 1] = '\0';
                SDL_strlcpy(buffer, name, size);
            }
            RegCloseKey(key);
        }
    }
#else
    {
        FILE *info = fopen("/proc/cpuinfo", "r");
        char line[256];

        while (info != NULL && fgets(line, sizeof(line), info) != NULL)
        {
            char *colon = strchr(line, ':');

            if (strncmp(line, "model name", 10) == 0 && colon != NULL)
            {
                SDL_strlcpy(buffer, colon + 2, size);
                buffer[strcspn(buffer, "\r\n")] = '\0';
                break;
            }
        }
        if (info != NULL)
            fclose(info);
    }
#endif
}

static const char *AntialiasingName(unsigned char samples)
{
    return samples == 0 ? "off" : samples == 2 ? "2x" : samples == 4 ? "4x" : "8x";
}

// One row per measured resolution, appended to --bench-csv (the header when the
// file is new): what the benchmark suite's summary reads.
static void WriteBenchmarkCsv(const char *renderer, const char *cpu)
{
    struct BenchmarkResult results[BENCHMARK_MAX_PHASES];
    int count = Benchmark_Results(results, BENCHMARK_MAX_PHASES);
    FILE *csv;
    bool fresh;

    if (sBenchCsvPath[0] == '\0')
        return;
    csv = fopen(sBenchCsvPath, "r");
    fresh = csv == NULL;
    if (csv != NULL)
        fclose(csv);
    csv = fopen(sBenchCsvPath, "a");
    if (csv == NULL)
    {
        SDL_Log("Benchmark: %s could not be written", sBenchCsvPath);
        return;
    }
    if (fresh)
        fprintf(csv, "scene,width,height,render_width,render_height,quality,shadows,lighting,effects,details,antialiasing,"
                     "frames,frames_3d,seconds,avg_fps,low1_fps,low01_fps,median_ms,p90_ms,p95_ms,p99_ms,p999_ms,"
                     "worst_ms,stutters,cpu_frame_ms,cpu_sync_ms,cpu_tick_ms,cpu_swap_ms,gpu_frame_ms,gpu_terrain_ms,"
                     "gpu_shadow_ms,gpu_air_ms,draws,triangles_k,instances,upload_kb,ram_mb,ram_peak_mb,"
                     "address_space_free_mb,vram_game_mb,vram_driver_available_mb,gpu,cpu\n");
    for (int i = 0; i < count; i++)
    {
        const struct BenchmarkResult *r = &results[i];
        const struct BenchPhaseExtra *x = &sBenchExtra[i];
        const struct DioramaProfileStats *st = &x->stats;

        fprintf(csv, "%s,%d,%d,%d,%d,%s,%s,%d,%d,%d,%s,%d,%d,%.2f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,",
                sBenchName[0] != '\0' ? sBenchName : "scene", r->width, r->height, x->renderWidth, x->renderHeight,
                DisplaySettings_QualityName(sDisplay.quality), DisplaySettings_ShadowName(sDisplay.shadows),
                sDisplay.lighting, sDisplay.effects, sDisplay.details, AntialiasingName(sDisplay.antialiasing),
                r->frames, r->frames3d, r->seconds, r->averageFps, r->lowFps, r->low01Fps, r->medianMs,
                r->p90Ms, r->p95Ms, r->p99Ms, r->p999Ms, r->worstMs, r->stutters);
        fprintf(csv, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.1f,%.0f,%.1f,%.0f,%.0f,%.0f,%.1f,%d,",
                st->cpuMs[DIORAMA_PROF_FRAME], st->cpuMs[DIORAMA_PROF_SYNC], st->cpuMs[DIORAMA_PROF_TICK],
                st->cpuMs[DIORAMA_PROF_SWAP], st->gpuMs[DIORAMA_PROF_FRAME], st->gpuMs[DIORAMA_PROF_TERRAIN],
                st->gpuMs[DIORAMA_PROF_SHADOW], st->gpuMs[DIORAMA_PROF_AIR], st->drawsPerFrame,
                st->verticesPerFrame / 3000.0, st->instancesPerFrame,
                st->textureUploadKBPerFrame + st->bufferUploadKBPerFrame,
                x->memory.processResident / 1048576.0, x->memory.processPeakResident / 1048576.0,
                x->memory.addressSpaceAvailable / 1048576.0,
                (x->gpu.textureBytes + x->gpu.bufferBytes + x->gpu.renderbufferBytes) / 1048576.0,
                x->gpu.driverAvailableMB);
        // The GPU and CPU names may hold commas: quoted.
        fprintf(csv, "\"%s\",\"%s\"\n", renderer, cpu);
    }
    fclose(csv);
}

static void WriteBenchmarkLog(void)
{
    FILE *log = fopen(sBenchLogPath, "w");
    char renderer[256];
    char cpu[128];
    SDL_DisplayMode desktop;
    int windowWidth = 0;
    int windowHeight = 0;
    time_t now = time(NULL);
    char when[64];

    if (log == NULL)
    {
        SDL_Log("Benchmark: benchmark.log could not be written");
        return;
    }
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", localtime(&now));
    DioramaGL_DescribeRenderer(renderer, sizeof(renderer));
    CpuName(cpu, sizeof(cpu));
    SDL_GL_GetDrawableSize(sdlWindow, &windowWidth, &windowHeight);
    fprintf(log, "Pokemon Emerald native PC - 3D view benchmark (%s)\n\n", when);
    if (sBenchName[0] != '\0')
        fprintf(log, "Scene:    %s\n", sBenchName);
    fprintf(log, "GPU:      %s\n", renderer);
    fprintf(log, "CPU:      %s (%d logical processors), %d MB RAM\n", cpu, SDL_GetCPUCount(), SDL_GetSystemRAM());
    if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(sdlWindow), &desktop) == 0)
        fprintf(log, "Display:  %dx%d at %d Hz\n", desktop.w, desktop.h, desktop.refresh_rate);
    fprintf(log, "Window:   %dx%d (%s)\n", windowWidth, windowHeight, DisplaySettings_ModeName(sDisplay.mode));
    fprintf(log, "Quality:  %s (shadows %s, lighting %s, effects %s, world details %s, anti-aliasing %s)\n",
            DisplaySettings_QualityName(sDisplay.quality), DisplaySettings_ShadowName(sDisplay.shadows),
            sDisplay.lighting ? "on" : "off", sDisplay.effects ? "on" : "off", sDisplay.details ? "on" : "off",
            sDisplay.antialiasing == 0 ? "off" : sDisplay.antialiasing == 2 ? "2x" : sDisplay.antialiasing == 4 ? "4x" : "8x");
    fprintf(log, "Method:   VSync off, no frame rate limit, render scale 100%% (native); at each resolution the\n"
                 "          3D scene is drawn at that size (offscreen and scaled into the window when the window\n"
                 "          is smaller); %d s counted after a %d s warm-up. '3D frames' should equal 'frames'\n"
                 "          (the save standing outdoors). A stutter is a frame over twice the median and at\n"
                 "          least 8 ms over it. CPU and GPU times are per frame, measured by the game itself\n"
                 "          (GPU: timer queries, where the driver has them).\n\n",
            sBenchmarkSeconds, BENCHMARK_WARMUP_MS / 1000);
    Benchmark_WriteResults(log);
    {
        struct BenchmarkResult results[BENCHMARK_MAX_PHASES];
        int count = Benchmark_Results(results, BENCHMARK_MAX_PHASES);

        fprintf(log, "\nrenderer      3D drawn at  cpu frame ms  cpu sync ms  gpu frame ms  draws  triangles  uploads KB"
                     "  RAM MB  VRAM MB (game)\n");
        for (int i = 0; i < count; i++)
        {
            const struct BenchPhaseExtra *x = &sBenchExtra[i];
            char gpuMs[16];

            if (x->stats.gpuMs[DIORAMA_PROF_FRAME] >= 0.0)
                SDL_snprintf(gpuMs, sizeof(gpuMs), "%.2f", x->stats.gpuMs[DIORAMA_PROF_FRAME]);
            else
                SDL_strlcpy(gpuMs, "n/a", sizeof(gpuMs));
            fprintf(log, "%4dx%-4d     %4dx%-4d  %12.2f  %11.2f  %12s  %5.0f  %8.0fk  %10.1f  %6.0f  %14.1f\n",
                    results[i].width, results[i].height, x->renderWidth, x->renderHeight,
                    x->stats.cpuMs[DIORAMA_PROF_FRAME], x->stats.cpuMs[DIORAMA_PROF_SYNC], gpuMs,
                    x->stats.drawsPerFrame, x->stats.verticesPerFrame / 3000.0,
                    x->stats.textureUploadKBPerFrame + x->stats.bufferUploadKBPerFrame,
                    x->memory.processResident / 1048576.0,
                    (x->gpu.textureBytes + x->gpu.bufferBytes + x->gpu.renderbufferBytes) / 1048576.0);
        }
        if (count > 0)
        {
            char line[1536];

            DioramaProfile_FormatStats(&sBenchExtra[count - 1].stats, line, sizeof(line));
            fprintf(log, "\nLast resolution, each part of a frame: %s\n", line);
            DioramaProfile_FormatGpuMemory(&sBenchExtra[count - 1].gpu, line, sizeof(line));
            fprintf(log, "%s\n", line);
            MemoryInfo_Format(&sBenchExtra[count - 1].memory, line, sizeof(line));
            fprintf(log, "Memory: %s\n", line);
            fprintf(log, "Game heap (the GBA's size): peak %.1f KB of %.1f KB\n",
                    sGameHeapPeak / 1024.0, HEAP_SIZE / 1024.0);
        }
    }
    fclose(log);
    WriteBenchmarkCsv(renderer, cpu);
    SDL_Log("Benchmark: %s written", sBenchLogPath);
}

// The benchmark's measured stretches: the renderer's counts and the memory of
// each, kept for the log (the frame times are the benchmark module's).
static void TrackBenchmarkPhase(Uint32 now)
{
    int phase = Benchmark_MeasuringPhase(now);

    if (phase == sBenchPhase)
        return;
    if (sBenchPhase >= 0 && sBenchPhase < BENCHMARK_MAX_PHASES)
    {
        struct BenchPhaseExtra *x = &sBenchExtra[sBenchPhase];
        int samples;

        DioramaProfile_TakeStats(&x->stats);
        DioramaProfile_GpuMemory(&x->gpu);
        MemoryInfo_Get(&x->memory);
        DioramaGLTarget_GetLastSize(&x->renderWidth, &x->renderHeight, &samples);
        x->taken = true;
    }
    if (phase >= 0)
    {
        struct DioramaProfileStats discard;

        DioramaProfile_SetTiming(true);
        DioramaProfile_TakeStats(&discard); // from here on
    }
    sBenchPhase = phase;
}
#endif

#ifdef ENABLE_DIORAMA
// PKMN_PERF_FRAME_LOG=path (development): a line per presented frame - the game frame (the frames PKMN_DEBUG_KEYS
// counts), the time since the last present, the CPU time over it of the main thread and of the whole program (every
// thread: a software renderer's own), whether the 3D field was drawn, the terrain's chunks rebuilt, vertices and tree
// instances drawn, and with PKMN_DIORAMA_PROFILE=1 the frame's terrain, sync and air times - for the frame-time
// percentiles of a scripted walk compared between two builds (CPU times also on a machine busy with other work).
static void FrameLog(double frameMs)
{
    static int sState; // 0 unread, 1 on, -1 off
    static FILE *sFile;
    static double sLastCpuMs;
    static double sLastProcessMs;
    double cpuMs = 0.0;
    double processMs = 0.0;
    const struct DioramaTerrainMetrics *terrain;

    if (sState == 0)
    {
        const char *path = SDL_getenv("PKMN_PERF_FRAME_LOG");

        sFile = path != NULL && path[0] != '\0' ? fopen(path, "w") : NULL;
        sState = sFile != NULL ? 1 : -1;
        if (sFile != NULL)
            fprintf(sFile, "game_frame,frame_ms,main_cpu_ms,process_cpu_ms,drew_3d,rebuilt_chunks,vertices,"
                           "tree_instances,terrain_ms,sync_ms,air_ms,inputs,input_ms,mesh_ms,upload_ms,prepare_ms,"
                           "sync_atlas_ms,sync_terrain_ms,sync_objects_ms,sync_buildings_ms,sync_trees_ms,"
                           "sync_props_ms\n");
    }
    if (sState < 0)
        return;
#if defined(NATIVE_LINUX) && defined(CLOCK_THREAD_CPUTIME_ID)
    {
        struct timespec now;

        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
            cpuMs = now.tv_sec * 1000.0 + now.tv_nsec / 1.0e6;
        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) == 0)
            processMs = now.tv_sec * 1000.0 + now.tv_nsec / 1.0e6;
    }
#endif
    terrain = DioramaGLTerrain_GetMetrics();
    fprintf(sFile, "%u,%.3f,%.3f,%.3f,%d,%u,%u,%u,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            DebugScene_GameFrame(), frameMs,
            sLastCpuMs != 0.0 ? cpuMs - sLastCpuMs : 0.0, sLastProcessMs != 0.0 ? processMs - sLastProcessMs : 0.0,
            DioramaGL_LastFrameWas3D() ? 1 : 0,
            (unsigned)terrain->rebuiltChunks, (unsigned)terrain->vertices, (unsigned)terrain->visibleTreeInstances,
            DioramaProfile_LastFrameMs(DIORAMA_PROF_TERRAIN), DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC),
            DioramaProfile_LastFrameMs(DIORAMA_PROF_AIR), (unsigned)terrain->inputBuilds,
            terrain->inputMicros / 1000.0, terrain->meshMicros / 1000.0, terrain->uploadMicros / 1000.0,
            terrain->prepareMicros / 1000.0, DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_ATLAS),
            DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_TERRAIN), DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_OBJECTS),
            DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_BUILDINGS), DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_TREES),
            DioramaProfile_LastFrameMs(DIORAMA_PROF_SYNC_PROPS));
    fflush(sFile);
    sLastCpuMs = cpuMs;
    sLastProcessMs = processMs;
}
#endif

// After every frame shown: the frame rate, the benchmark, the settings
// menu's confirmation countdown.
static void AfterPresent(void)
{
    Uint32 now = SDL_GetTicks();
    Uint64 counter = SDL_GetPerformanceCounter();
    double frameMs = sLastPresentCounter != 0
                   ? (double)(counter - sLastPresentCounter) * 1000.0 / SDL_GetPerformanceFrequency() : 0.0;

    sLastPresentCounter = counter;
    sFrameRateFrames++;
    if (now - sFrameRateWindowMs >= 500)
    {
        sFrameRateShown = sFrameRateFrames * 1000.0f / (now - sFrameRateWindowMs);
        sFrameRateFrames = 0;
        sFrameRateWindowMs = now;
    }
#ifdef ENABLE_DIORAMA
    FrameLog(frameMs);
    // PKMN_DIORAMA_PROFILE=1: where the frame's time goes, and the memory, every 2 s.
    if (DioramaProfile_LogRequested() && !Benchmark_Active())
    {
        static Uint32 sProfileLogMs;

        if (sProfileLogMs == 0)
            sProfileLogMs = now;
        else if (now - sProfileLogMs >= 2000)
        {
            struct DioramaProfileStats stats;
            struct DioramaGpuMemory gpu;
            struct MemoryInfo memory;
            char line[1536];

            sProfileLogMs = now;
            DioramaProfile_TakeStats(&stats);
            DioramaProfile_FormatStats(&stats, line, sizeof(line));
            SDL_Log("profile: %s", line);
            DioramaProfile_GpuMemory(&gpu);
            DioramaProfile_FormatGpuMemory(&gpu, line, sizeof(line));
            SDL_Log("profile: %s", line);
            MemoryInfo_Get(&memory);
            MemoryInfo_Format(&memory, line, sizeof(line));
            SDL_Log("profile: %s; game heap %.1f KB in use (peak %.1f KB of %.1f KB)", line,
                    sGameHeapNow / 1024.0, sGameHeapPeak / 1024.0, HEAP_SIZE / 1024.0);
        }
    }
    if (sPcMenu.open)
    {
        ApplyPcMenuResult(PcMenu_Tick(&sPcMenu, now));
        UpdateMenuMemory(false);
    }
    if (Benchmark_Active())
    {
        int height = Benchmark_RenderHeight(now);

        if (frameMs > 0.0)
            Benchmark_Frame(now, frameMs, DioramaGL_LastFrameWas3D());
        TrackBenchmarkPhase(now);
        DioramaGLTarget_Configure(height > 0 ? height
                                  : sDisplay.mode == DISPLAY_MODE_WINDOWED ? 0 : sDisplay.height,
                                  sDisplay.antialiasing);
        if (Benchmark_Finished(now))
        {
            TrackBenchmarkPhase(now + 0x40000000u); // the last stretch's counts
            WriteBenchmarkLog();
            isRunning = false;
        }
    }
#else
    (void)frameMs;
#endif
}

// Writes the whole flash image to a temporary file and renames it over the save,
// so a crash or power cut never leaves a half-written file behind.
static bool WriteSaveImage(void)
{
    char tempPath[sizeof(sSavePath) + 8];
    FILE *saveFile;
    size_t written;

    SDL_snprintf(tempPath, sizeof(tempPath), "%s.tmp", sSavePath);
    saveFile = fopen(tempPath, "wb");
    if (saveFile == NULL)
    {
        SDL_Log("Unable to write save file %s", tempPath);
        return false;
    }
    written = fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), saveFile);
    if (fflush(saveFile) != 0 || written != sizeof(FLASH_BASE))
    {
        fclose(saveFile);
        SDL_Log("Short write to %s", tempPath);
        return false;
    }
    fclose(saveFile);
#ifdef _WIN32
    if (!MoveFileExA(tempPath, sSavePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        SDL_Log("Unable to replace save file %s", sSavePath);
        return false;
    }
#else
    if (rename(tempPath, sSavePath) != 0)
    {
        SDL_Log("Unable to replace save file %s", sSavePath);
        return false;
    }
#endif
    return true;
}

#ifdef ENABLE_NATIVE_UI
// Save file writes, for the native UI's save screen: it reports the game as
// saved only once the file is written (Platform_NativeUiSaveWrites).
static u32 sSaveWritesDone;
static u32 sSaveWritesFailed;
#endif

static void StoreSaveFile(void)
{
    bool ok = WriteSaveImage();

#ifdef ENABLE_NATIVE_UI
    if (ok)
        sSaveWritesDone++;
    else
        sSaveWritesFailed++;
#endif
    (void)ok;
}

static void FlushSaveIfDirty(void)
{
    if (gPlatformFlashDirty)
    {
        gPlatformFlashDirty = FALSE;
        StoreSaveFile();
    }
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

// Stands in for the GBA timers the game reads to seed its RNG.
u32 Platform_GetEntropy32(void)
{
    Uint64 x = SDL_GetPerformanceCounter() ^ ((Uint64)time(NULL) << 32) ^ (Uint64)(uintptr_t)&x;

    // splitmix64 finaliser
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return (u32)(x ^ (x >> 32));
}

bool8 Platform_RealFrame(void)
{
    return sTestSpeed || GameSpeed_RealFrame(&sGameSpeed);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    DebugScene_AudioTap(audioBuffer, samplesPerFrame / sizeof(float));
#if defined(DIORAMA_BATTLE_ENV_TEST)
    if (timeScale > 1.0)
        return;     // PKMN_TEST_SPEED: faster than the sound card plays (the queue would only grow)
#endif
    if (sdlAudioDevice != 0)
    {
        int floatCount = samplesPerFrame / sizeof(float);
        float adjustedAudio[floatCount];

        // Battle speed: one game frame's sound a real frame, cross-faded (include/platform/game_speed.h).
        if (GameSpeed_Sound(&sGameSpeed, audioBuffer, floatCount, adjustedAudio) == 0)
            return;
        // The mixer leaves output headroom, so full volume sounds quiet. Apply a
        // make-up gain and hard-clamp to [-1,1] so loud passages can't clip. Measured with the audio audit
        // (PKMN_DEBUG_SCENE=audio, all 479 sounds): music peaks 0.06 at the median and 0.115 at most, a sound effect
        // 0.04 / 0.10, so the old gain of 2 left music near -18 dBFS peak. Gain 4 puts the loudest music at 0.46 and
        // music plus the loudest effect at 0.86: still under full scale, no clipping in the whole library.
        float volume = (sPlatformSettings[PLATFORM_SETTING_VOLUME] / 10.0f) * 4.0f;
        for (int i = 0; i < floatCount; i++)
        {
            float s = adjustedAudio[i] * volume;
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            adjustedAudio[i] = s;
        }
        // The queue never outgrows a few frames (MP+3D update 2026-10-26, progressive lag): the sound card plays on its own
        // clock, and a game clock a little fast or a device that stopped for a moment (a headset waking, the default
        // device changing) left sound queued that never drained, so the sound lagged further behind the longer one
        // played. Past 8 frames a frame's sound is left out; past 30 (half a second, a stalled device) the backlog goes.
        {
            Uint32 queued = SDL_GetQueuedAudioSize(sdlAudioDevice);

            sAudioQueuedPeak = queued > sAudioQueuedPeak ? queued : sAudioQueuedPeak;
            if (queued > (Uint32)samplesPerFrame * 30)
            {
                SDL_ClearQueuedAudio(sdlAudioDevice);
                sAudioCleared++;
            }
            else if (queued > (Uint32)samplesPerFrame * 8)
            {
                sAudioDropped++;
                return;
            }
        }
        if (SDL_QueueAudio(sdlAudioDevice, adjustedAudio, samplesPerFrame) < 0)
            SDL_Log("Failed to queue audio: %s", SDL_GetError());
    }
}

u8 Platform_GetBorderBackgroundCount(void)
{
    return sBorderBackgroundCount + 1;
}

u8 Platform_GetBorderBackground(void)
{
    if (sHasBorderBackgroundConfig)
        return sBorderBackground;
    return 0;
}

void Platform_SetBorderBackground(u8 selection)
{
    sBorderBackground = selection;
    sHasBorderBackgroundConfig = true;
    StoreConfigFile();
}

u16 Platform_GetReaderKeys(void)
{
    u16 keys = sReaderKeys;

    sReaderKeys = 0;
    return keys;
}

u8 Platform_GetSetting(enum PlatformSetting setting)
{
    return sPlatformSettings[setting];
}

void Platform_SetSetting(enum PlatformSetting setting, u8 value)
{
    sPlatformSettings[setting] = value;
    // The older two settings in terms of the display settings.
    if (setting == PLATFORM_SETTING_FULLSCREEN)
    {
        sDisplay.mode = value ? sDisplay.fullscreenMode : DISPLAY_MODE_WINDOWED;
        sDisplay.modeKept = true;
    }
    else if (setting == PLATFORM_SETTING_WINDOW_SCALE && value >= 2 && value <= 12)
    {
        sDisplay.width = 320 * value;
        sDisplay.height = 180 * value;
        sDisplay.resolutionKept = true;
    }
#ifdef ENABLE_DIORAMA
    if (setting == PLATFORM_SETTING_VSYNC
     || setting == PLATFORM_SETTING_FULLSCREEN
     || setting == PLATFORM_SETTING_WINDOW_SCALE)
    {
        SDL_Event event = {0};
        event.type = SDL_USEREVENT;
        event.user.code = DIORAMA_VIDEO_SETTING_EVENT;
        event.user.data1 = (void *)(uintptr_t)setting;
        event.user.data2 = (void *)(uintptr_t)value;
        SDL_PushEvent(&event);
    }
#else
    if (setting == PLATFORM_SETTING_VSYNC)
        SDL_RenderSetVSync(sdlRenderer, value);
#if defined(NATIVE_LINUX) || defined(_WIN32)
    else if (setting == PLATFORM_SETTING_FULLSCREEN || setting == PLATFORM_SETTING_WINDOW_SCALE)
        ApplyDisplaySettings(true);
#endif
#endif
    StoreConfigFile();
}

#ifdef __ANDROID__
JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_GbaControlsView_getBorderBackground(JNIEnv *env, jclass clazz)
{
    return Platform_GetBorderBackground();
}

JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_GbaControlsView_getPlatformSetting(JNIEnv *env, jclass clazz, jint setting)
{
    if (setting < 0 || setting >= PLATFORM_SETTING_COUNT)
        return 0;
    return Platform_GetSetting(setting);
}
#endif


// Key mappings: the bindings (sKeybinds) name the key of each action; this is the button each
// of the first KEYBIND_GAME_BUTTONS actions presses.
static const u16 sKeybindButtons[KEYBIND_GAME_BUTTONS] = {
    [KEYBIND_A] = A_BUTTON, [KEYBIND_B] = B_BUTTON, [KEYBIND_START] = START_BUTTON,
    [KEYBIND_SELECT] = SELECT_BUTTON, [KEYBIND_L] = L_BUTTON, [KEYBIND_R] = R_BUTTON,
    [KEYBIND_UP] = DPAD_UP, [KEYBIND_DOWN] = DPAD_DOWN, [KEYBIND_LEFT] = DPAD_LEFT,
    [KEYBIND_RIGHT] = DPAD_RIGHT,
};

static u16 keyboardKeys;

#ifdef __ANDROID__
#define MAX_TOUCH_FINGERS 10

struct TouchFinger
{
    SDL_FingerID id;
    float x;
    float y;
    bool active;
};

static struct TouchFinger touchFingers[MAX_TOUCH_FINGERS];
static u16 touchKeys;
static u16 controllerKeys;
static u16 controllerAxisKeys;
static Sint16 controllerAxisX;
static Sint16 controllerAxisY;

static bool IsInsideRect(int x, int y, SDL_Rect rect)
{
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &rect);
}

static int MinInt(int a, int b)
{
    return a < b ? a : b;
}

static int GetControlSideWidth(int windowWidth, int windowHeight)
{
    int sideWidth = (windowWidth - windowHeight * 3 / 2) / 2;
    int minimumWidth = windowWidth * 14 / 100;
    return sideWidth > minimumWidth ? sideWidth : minimumWidth;
}

static void UpdateTouchKeys(void)
{
    int windowWidth;
    int windowHeight;
    SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    int sideWidth = GetControlSideWidth(windowWidth, windowHeight);
    int buttonSize = MinInt(sideWidth * 2 / 5, windowHeight / 6);
    int dpadUnit = MinInt(sideWidth / 3, windowHeight / 8);
    int dpadX = sideWidth * 2 / 3;
    int dpadY = windowHeight * 7 / 10;
    SDL_Rect dpadUp = {dpadX - dpadUnit / 2, dpadY - dpadUnit * 3 / 2,
                       dpadUnit, dpadUnit};
    SDL_Rect dpadDown = {dpadX - dpadUnit / 2, dpadY + dpadUnit / 2,
                         dpadUnit, dpadUnit};
    SDL_Rect dpadLeft = {dpadX - dpadUnit * 3 / 2, dpadY - dpadUnit / 2,
                         dpadUnit, dpadUnit};
    SDL_Rect dpadRight = {dpadX + dpadUnit / 2, dpadY - dpadUnit / 2,
                          dpadUnit, dpadUnit};
    SDL_Rect aButton = {windowWidth - sideWidth / 4 - buttonSize,
                        windowHeight * 58 / 100, buttonSize, buttonSize};
    SDL_Rect bButton = {windowWidth - sideWidth + sideWidth / 4,
                        windowHeight * 76 / 100, buttonSize, buttonSize};
    SDL_Rect selectButton = {sideWidth / 4, windowHeight / 4,
                             sideWidth / 2, windowHeight / 10};
    SDL_Rect startButton = {windowWidth - sideWidth * 3 / 4, windowHeight / 4,
                            sideWidth / 2, windowHeight / 10};
    SDL_Rect lButton = {sideWidth / 4, windowHeight / 20,
                        sideWidth / 2, windowHeight / 10};
    SDL_Rect rButton = {windowWidth - sideWidth * 3 / 4, windowHeight / 20,
                        sideWidth / 2, windowHeight / 10};

    touchKeys = 0;

    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (!touchFingers[i].active)
            continue;

        int x = touchFingers[i].x * windowWidth;
        int y = touchFingers[i].y * windowHeight;

        if (IsInsideRect(x, y, dpadUp)) touchKeys |= DPAD_UP;
        if (IsInsideRect(x, y, dpadDown)) touchKeys |= DPAD_DOWN;
        if (IsInsideRect(x, y, dpadLeft)) touchKeys |= DPAD_LEFT;
        if (IsInsideRect(x, y, dpadRight)) touchKeys |= DPAD_RIGHT;

        if (IsInsideRect(x, y, aButton)) touchKeys |= A_BUTTON;
        if (IsInsideRect(x, y, bButton)) touchKeys |= B_BUTTON;
        if (IsInsideRect(x, y, startButton)) touchKeys |= START_BUTTON;
        if (IsInsideRect(x, y, selectButton)) touchKeys |= SELECT_BUTTON;
        if (IsInsideRect(x, y, lButton)) touchKeys |= L_BUTTON;
        if (IsInsideRect(x, y, rButton)) touchKeys |= R_BUTTON;
    }
}

static void HandleTouchEvent(const SDL_TouchFingerEvent *event)
{
    int slot = -1;
    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (touchFingers[i].active && touchFingers[i].id == event->fingerId)
        {
            slot = i;
            break;
        }
        if (slot < 0 && !touchFingers[i].active)
            slot = i;
    }

    if (slot < 0)
        return;

    if (event->type == SDL_FINGERUP)
    {
        touchFingers[slot].active = false;
    }
    else
    {
        touchFingers[slot].id = event->fingerId;
        touchFingers[slot].x = event->x;
        touchFingers[slot].y = event->y;
        touchFingers[slot].active = true;
    }

    UpdateTouchKeys();
}

static const Uint8 *GetGlyph(char character)
{
    static const Uint8 glyphA[7] = {14, 17, 17, 31, 17, 17, 17};
    static const Uint8 glyphB[7] = {30, 17, 17, 30, 17, 17, 30};
    static const Uint8 glyphC[7] = {15, 16, 16, 16, 16, 16, 15};
    static const Uint8 glyphE[7] = {31, 16, 16, 30, 16, 16, 31};
    static const Uint8 glyphL[7] = {16, 16, 16, 16, 16, 16, 31};
    static const Uint8 glyphR[7] = {30, 17, 17, 30, 20, 18, 17};
    static const Uint8 glyphS[7] = {15, 16, 16, 14, 1, 1, 30};
    static const Uint8 glyphT[7] = {31, 4, 4, 4, 4, 4, 4};

    switch (character)
    {
    case 'A': return glyphA;
    case 'B': return glyphB;
    case 'C': return glyphC;
    case 'E': return glyphE;
    case 'L': return glyphL;
    case 'R': return glyphR;
    case 'S': return glyphS;
    case 'T': return glyphT;
    default:  return NULL;
    }
}

static void DrawControlLabel(SDL_Rect rect, const char *label)
{
    int length = SDL_strlen(label);
    int scale = MinInt(rect.h / 9, rect.w / (length * 6));
    if (scale < 1)
        scale = 1;
    int startX = rect.x + (rect.w - (length * 6 - 1) * scale) / 2;
    int startY = rect.y + (rect.h - 7 * scale) / 2;

    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, 230);
    for (int character = 0; character < length; character++)
    {
        const Uint8 *glyph = GetGlyph(label[character]);
        if (glyph == NULL)
            continue;
        for (int row = 0; row < 7; row++)
        {
            for (int column = 0; column < 5; column++)
            {
                if (glyph[row] & (1 << (4 - column)))
                {
                    SDL_Rect pixel = {startX + (character * 6 + column) * scale,
                                      startY + row * scale, scale, scale};
                    SDL_RenderFillRect(sdlRenderer, &pixel);
                }
            }
        }
    }
}

static void DrawControlRect(SDL_Rect rect, bool pressed, const char *label)
{
    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, pressed ? 150 : 65);
    SDL_RenderFillRect(sdlRenderer, &rect);
    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, pressed ? 230 : 130);
    SDL_RenderDrawRect(sdlRenderer, &rect);
    if (label != NULL)
        DrawControlLabel(rect, label);
}

static void DrawTouchControls(void)
{
    int windowWidth;
    int windowHeight;
    SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    int sideWidth = GetControlSideWidth(windowWidth, windowHeight);
    int buttonSize = MinInt(sideWidth * 2 / 5, windowHeight / 6);
    int dpadUnit = MinInt(sideWidth / 3, windowHeight / 8);
    int dpadX = sideWidth * 2 / 3;
    int dpadY = windowHeight * 7 / 10;

    SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);
    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    DrawControlRect((SDL_Rect){dpadX - dpadUnit / 2, dpadY - dpadUnit * 3 / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_UP, NULL);
    DrawControlRect((SDL_Rect){dpadX - dpadUnit / 2, dpadY + dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_DOWN, NULL);
    DrawControlRect((SDL_Rect){dpadX - dpadUnit * 3 / 2, dpadY - dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_LEFT, NULL);
    DrawControlRect((SDL_Rect){dpadX + dpadUnit / 2, dpadY - dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_RIGHT, NULL);
    DrawControlRect((SDL_Rect){windowWidth - sideWidth / 4 - buttonSize,
                               windowHeight * 58 / 100, buttonSize, buttonSize}, touchKeys & A_BUTTON, "A");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth + sideWidth / 4,
                               windowHeight * 76 / 100, buttonSize, buttonSize}, touchKeys & B_BUTTON, "B");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth * 3 / 4, windowHeight / 4,
                               sideWidth / 2, windowHeight / 10}, touchKeys & START_BUTTON, "START");
    DrawControlRect((SDL_Rect){sideWidth / 4, windowHeight / 4,
                               sideWidth / 2, windowHeight / 10}, touchKeys & SELECT_BUTTON, "SELECT");
    DrawControlRect((SDL_Rect){sideWidth / 4, windowHeight / 20,
                               sideWidth / 2, windowHeight / 10}, touchKeys & L_BUTTON, "L");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth * 3 / 4, windowHeight / 20,
                               sideWidth / 2, windowHeight / 10}, touchKeys & R_BUTTON, "R");

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    SDL_RenderSetIntegerScale(sdlRenderer, SDL_TRUE);
}

static u16 ControllerButtonMask(Uint8 button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A:             return A_BUTTON;
    case SDL_CONTROLLER_BUTTON_B:             return B_BUTTON;
    case SDL_CONTROLLER_BUTTON_BACK:          return SELECT_BUTTON;
    case SDL_CONTROLLER_BUTTON_START:         return START_BUTTON;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return L_BUTTON;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return R_BUTTON;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return DPAD_RIGHT;
    default:                                  return 0;
    }
}
#endif

void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
#ifdef ENABLE_NATIVE_UI
        // the native UI's mouse regions first (a wheel over a list scrolls it
        // instead of zooming the 3D camera)
        if (NativeUi_HandleEvent(&event))
            continue;
#endif
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
#if defined(NATIVE_LINUX) || defined(_WIN32)
        case SDL_WINDOWEVENT:
            // A fullscreen window moved to another display (Win+Shift+arrow, a
            // display unplugged): fullscreen there, at that display's own size.
            if (event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED
             && sDisplay.mode != DISPLAY_MODE_WINDOWED)
                ApplyDisplaySettings(false);
            break;
#endif
#ifdef ENABLE_DIORAMA
        case SDL_USEREVENT:
            if (event.user.code == DIORAMA_VIDEO_SETTING_EVENT)
                ApplyDioramaVideoSetting((enum PlatformSetting)(uintptr_t)event.user.data1,
                                         (u8)(uintptr_t)event.user.data2);
            break;
        case SDL_MOUSEWHEEL:
        {
            int steps = event.wheel.y;

            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                steps = -steps;
            DioramaGL_AdjustCameraZoom(steps);
            KeepCameraSteps();
            break;
        }
#endif
#ifdef __ANDROID__
        case SDL_CONTROLLERDEVICEADDED:
            if (androidController == NULL && SDL_IsGameController(event.cdevice.which))
                androidController = SDL_GameControllerOpen(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (androidController != NULL
             && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(androidController)) == event.cdevice.which)
            {
                SDL_GameControllerClose(androidController);
                androidController = NULL;
                controllerKeys = 0;
                controllerAxisKeys = 0;
                controllerAxisX = 0;
                controllerAxisY = 0;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            controllerKeys |= ControllerButtonMask(event.cbutton.button);
            break;
        case SDL_CONTROLLERBUTTONUP:
            controllerKeys &= ~ControllerButtonMask(event.cbutton.button);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                controllerAxisX = event.caxis.value;
            else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                controllerAxisY = event.caxis.value;

            controllerAxisKeys = 0;
            if (controllerAxisX < -16000) controllerAxisKeys |= DPAD_LEFT;
            if (controllerAxisX >  16000) controllerAxisKeys |= DPAD_RIGHT;
            if (controllerAxisY < -16000) controllerAxisKeys |= DPAD_UP;
            if (controllerAxisY >  16000) controllerAxisKeys |= DPAD_DOWN;
            break;
#endif
        case SDL_KEYUP:
        {
            int action = Keybinds_ActionForKey(&sKeybinds, (int)event.key.keysym.sym);

            if (action >= 0 && action < KEYBIND_GAME_BUTTONS)
                keyboardKeys &= ~sKeybindButtons[action], KeyboardKeysChanged();
            break;
        }
        case SDL_KEYDOWN:
#ifdef ENABLE_DIORAMA
            // F10 and, while the PC settings menu is open, its keys.
            if (HandlePcMenuKey(&event.key))
                break;
            // Photo mode: the arrows move the camera, Esc leaves.
            if (HandlePhotoKey(&event.key))
                break;
#endif
            {
                // The bound keys (Ctrl+R and Ctrl+P stay the reset chord and pause, whatever is bound).
                int action = Keybinds_ActionForKey(&sKeybinds, (int)event.key.keysym.sym);
                bool chord = (event.key.keysym.sym == SDLK_r || event.key.keysym.sym == SDLK_p)
                          && (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL));

                if (action >= 0 && action < KEYBIND_GAME_BUTTONS && !chord)
                {
                    keyboardKeys |= sKeybindButtons[action];
                    KeyboardKeysChanged();
                    break;
                }
            }
            switch (event.key.keysym.sym)
            {
            case SDLK_r:
                // Ctrl+R: hold A+B+START+SELECT for one frame (the chord is awkward
                // on a keyboard). The game decides what it means: this multiplayer
                // build hands the chord to MultiplayerLocalReset_HandleChord, which
                // never reboots from it: in the field or the START menu it is the
                // soft reset key (MS-61, multiplayer_soft_reset.c: the question, or
                // the Players menu's reset page in a world); in a battle it keeps
                // its Local Soft Reset meaning.
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                    sSoftResetChordFrames = 1;
                break;
            case SDLK_p:
                if ((event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) && !paused && LinkOperationActive())
                    RefusePause();
                else if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;
#ifdef ENABLE_DIORAMA
                    if (!paused)
                        DioramaGL_EndSurvey();
#endif
                }
                break;
            case SDLK_F11:
                // Windowed, or the fullscreen kind chosen last (borderless
                // unless the settings menu chose exclusive fullscreen).
                if (event.key.repeat == 0)
                {
                    sDisplay.mode = sDisplay.mode == DISPLAY_MODE_WINDOWED
                                  ? sDisplay.fullscreenMode : DISPLAY_MODE_WINDOWED;
                    sDisplay.modeKept = true;
                    ApplyDisplaySettings(true);
                    StoreConfigFile();
                }
                break;
#ifdef ENABLE_DIORAMA
            case SDLK_F3:
                if (event.key.repeat == 0)
                    DioramaGL_ToggleTerrainDebug();
                break;
            case SDLK_F4:
                if (event.key.repeat == 0)
                    DioramaGL_ToggleEnabled();
                break;
            case SDLK_F5:
                if (event.key.repeat == 0 && !paused && LinkOperationActive())
                    RefusePause();
                else if (event.key.repeat == 0)
                {
                    paused = true;
                    DioramaGL_CycleSurveyView();
                }
                break;
            case SDLK_F6:
                if (event.key.repeat == 0)
                    DioramaGL_RequestSurveyCapture();
                break;
            case SDLK_F7:
                // The camera: Adventure (the default) or Diorama (diorama/camera.h).
                if (event.key.repeat == 0 && !sPhotoMode)
                {
                    sCameraMode = sCameraMode == DIORAMA_CAMERA_DIORAMA ? DIORAMA_CAMERA_ADVENTURE
                                                                        : DIORAMA_CAMERA_DIORAMA;
                    DioramaGL_SetCameraMode(sCameraMode);
                    sCameraModeKept = TRUE;
                    SDL_Log("3D view: %s camera", sCameraMode == DIORAMA_CAMERA_DIORAMA ? "diorama" : "adventure");
                    StoreConfigFile();
                }
                break;
            case SDLK_F9:
                // Photo mode: the game stands still and the camera moves freely.
                if (event.key.repeat == 0 && !sPhotoMode && LinkOperationActive())
                    RefusePause();
                else if (event.key.repeat == 0 && DioramaGL_LastFrameWas3D())
                    SetPhotoMode(!sPhotoMode);
                break;
            case SDLK_F12:
                if (event.key.repeat == 0)
                    sScreenshotRequested = true;
                break;
            case SDLK_F8:
                // The trees: the Diorama's voxel trees or the 3DS project's.
                if (event.key.repeat == 0)
                {
                    sTreeStyle3ds = !DioramaTreeSprites_Enabled();
                    sTreeStyleKept = TRUE;
                    DioramaTreeSprites_SetEnabled(sTreeStyle3ds);
                    SDL_Log("3D view: %s trees", sTreeStyle3ds ? "3DS" : "voxel");
                    StoreConfigFile();
                }
                break;
            case SDLK_COMMA:
                DioramaGL_AdjustCameraPitch(-1);
                KeepCameraSteps();
                break;
            case SDLK_PERIOD:
                DioramaGL_AdjustCameraPitch(1);
                KeepCameraSteps();
                break;
            // Zoom without a mouse wheel (keyboard players): minus out, equals/plus in.
            case SDLK_MINUS:
            case SDLK_KP_MINUS:
                DioramaGL_AdjustCameraZoom(-1);
                KeepCameraSteps();
                break;
            case SDLK_EQUALS:
            case SDLK_KP_PLUS:
                DioramaGL_AdjustCameraZoom(1);
                KeepCameraSteps();
                break;
#endif
            // Screen-reader hotkeys. These aren't GBA buttons, so they're
            // latched here and drained by whichever screen is listening.
            case SDLK_l:
                sReaderKeys |= (event.key.keysym.mod & KMOD_SHIFT) ? READER_KEY_EXP : READER_KEY_LEVEL;
                break;
            case SDLK_h:
                sReaderKeys |= READER_KEY_HP;
                break;
            case SDLK_u:
                sReaderKeys |= READER_KEY_ITEM;
                break;
            case SDLK_t:
                sReaderKeys |= READER_KEY_STATUS;
                break;
            case SDLK_y:
                sReaderKeys |= READER_KEY_ALL;
                break;
            }
            break;
        }
    }
}

// --selftest-keybinds: the bindings through the real event loop (ReadConfigFile, ProcessEvents,
// Platform_GetKeyInput, StoreConfigFile) with synthetic key events and a scratch settings file.
static int sKeyChecks, sKeyFailures;

static void ExpectKeys(const char *what, bool ok)
{
    sKeyChecks++;
    if (!ok)
        sKeyFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void KeyEvent(SDL_Keycode sym, bool down, Uint16 mod)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = sym;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
    event.key.keysym.mod = mod;
    SDL_PushEvent(&event);
    ProcessEvents();
}

static void WriteScratchConfig(const char *text)
{
    FILE *file = fopen(sConfigPath, "w");

    if (file != NULL)
    {
        fputs(text, file);
        fclose(file);
    }
}

static int SelfTestKeybinds(void)
{
    char stored[1024] = "";
    FILE *file;
    size_t length;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("FAIL SDL could not start: %s\n", SDL_GetError());
        return 1;
    }
    SDL_strlcpy(sConfigPath, "keybinds-selftest.cfg", sizeof(sConfigPath));
    printf("keyboard bindings (key.* lines of the settings file)\n");

    // Defaults: the keys of every earlier build.
    remove(sConfigPath);
    ReadConfigFile();
    KeyEvent(SDLK_z, true, 0);
    KeyEvent(SDLK_UP, true, 0);
    ExpectKeys("default Z is A and Up is Up", Platform_GetKeyInput() == (A_BUTTON | DPAD_UP));
    KeyEvent(SDLK_z, false, 0);
    KeyEvent(SDLK_UP, false, 0);
    ExpectKeys("releasing them releases the buttons", Platform_GetKeyInput() == 0);
    // Update 28: a tap that begins and ends between two game frames (a slow 3D frame; Windows delivers both events in one
    // batch) used to leave nothing at the frame boundary, and the player had to press A again.
    KeyEvent(SDLK_z, true, 0);
    KeyEvent(SDLK_z, false, 0);
    ExpectKeys("a tap between two frames is down for one frame", Platform_GetKeyInput() == A_BUTTON);
    ExpectKeys("and up the next", Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_z, true, 0);
    ExpectKeys("a key held at the frame boundary is down", Platform_GetKeyInput() == A_BUTTON);
    KeyEvent(SDLK_z, false, 0);
    KeyEvent(SDLK_z, true, 0);
    KeyEvent(SDLK_z, false, 0);
    ExpectKeys("a second tap in the next batch: up for a frame first", Platform_GetKeyInput() == 0);
    ExpectKeys("then down again (a new press)", Platform_GetKeyInput() == A_BUTTON);
    ExpectKeys("then up", Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_RETURN, true, 0);
    KeyEvent(SDLK_BACKSLASH, true, 0);
    ExpectKeys("default Enter is Start, \\ is Select", Platform_GetKeyInput() == (START_BUTTON | SELECT_BUTTON));
    KeyEvent(SDLK_RETURN, false, 0);
    KeyEvent(SDLK_BACKSLASH, false, 0);
    KeyEvent(SDLK_SPACE, true, 0);
    ExpectKeys("Space (once fast forward) does nothing now: no speed-up exists", Platform_GetKeyInput() == 0 && timeScale == 1.0);
    KeyEvent(SDLK_SPACE, false, 0);

    // A settings file with bindings (and settings of other kinds left alone).
    WriteScratchConfig("volume=7\nkey.a=Q\nkey.b=Left Shift\nkey.fastforward=Tab\nkey.up=W\nkey.l=Escape\nkey.r=Nonsense\n");
    ReadConfigFile();
    ExpectKeys("the file's A key is Q", sKeybinds.key[KEYBIND_A] == SDLK_q);
    ExpectKeys("a reserved key (Esc) or an unknown name in the file is ignored",
               sKeybinds.key[KEYBIND_L] == SDLK_a && sKeybinds.key[KEYBIND_R] == SDLK_s);
    KeyEvent(SDLK_q, true, 0);
    ExpectKeys("Q presses A", Platform_GetKeyInput() == A_BUTTON);
    KeyEvent(SDLK_q, false, 0);
    KeyEvent(SDLK_z, true, 0);
    ExpectKeys("the old Z does nothing now", Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_z, false, 0);
    KeyEvent(SDLK_LSHIFT, true, 0);
    KeyEvent(SDLK_w, true, 0);
    ExpectKeys("Left Shift presses B and W presses Up", Platform_GetKeyInput() == (B_BUTTON | DPAD_UP));
    KeyEvent(SDLK_LSHIFT, false, 0);
    KeyEvent(SDLK_w, false, 0);
    KeyEvent(SDLK_UP, true, 0);
    ExpectKeys("the arrow that was Up does nothing once Up is W", Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_UP, false, 0);
    KeyEvent(SDLK_TAB, true, 0);
    ExpectKeys("an old key.fastforward=Tab line in the file binds nothing", Platform_GetKeyInput() == 0 && timeScale == 1.0);
    KeyEvent(SDLK_TAB, false, 0);
    KeyEvent(SDLK_ESCAPE, true, 0);
    ExpectKeys("Esc is never a game button", Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_ESCAPE, false, 0);

    // The reset chord stays Ctrl+R whatever is bound to R.
    WriteScratchConfig("key.a=R\n");
    ReadConfigFile();
    sSoftResetChordFrames = 0;
    KeyEvent(SDLK_r, true, KMOD_LCTRL);
    ExpectKeys("Ctrl+R is still the reset chord, not the A button", sSoftResetChordFrames == 1 && Platform_GetKeyInput() == 0);
    KeyEvent(SDLK_r, false, 0);
    KeyEvent(SDLK_r, true, 0);
    ExpectKeys("a plain R is the A button", Platform_GetKeyInput() == A_BUTTON);
    KeyEvent(SDLK_r, false, 0);

    // Stored: only what differs from the defaults, and other lines are kept by StoreConfigFile.
    WriteScratchConfig("key.a=Q\nkey.b=X\n");
    ReadConfigFile();
    StoreConfigFile();
    file = fopen(sConfigPath, "r");
    length = file != NULL ? fread(stored, 1, sizeof(stored) - 1, file) : 0;
    stored[length] = '\0';
    if (file != NULL)
        fclose(file);
    ExpectKeys("the changed binding is written back (key.a=Q)", strstr(stored, "key.a=Q\n") != NULL);
    ExpectKeys("a default binding is not written (key.b=X was the default)", strstr(stored, "key.b") == NULL);
    ReadConfigFile();
    ExpectKeys("what was written reads back", sKeybinds.key[KEYBIND_A] == SDLK_q && sKeybinds.key[KEYBIND_B] == SDLK_x);

    // A file with no bindings: back to the defaults.
    WriteScratchConfig("volume=3\n");
    ReadConfigFile();
    ExpectKeys("without key lines the defaults are back", sKeybinds.key[KEYBIND_A] == SDLK_z && !sKeybinds.custom[KEYBIND_A]);

    // The pad (the Windows XInput buttons and the SDL controller map into the same table): every face and shoulder button,
    // the d-pad and the stick reach the game, and Back/Cancel works from either left or right face button.
    {
        struct GamepadInput pad = {0};

        pad.buttons = GAMEPAD_A;
        ExpectKeys("pad A is the A button", Gamepad_MapKeys(&pad).keys == A_BUTTON);
        pad.buttons = GAMEPAD_X;
        ExpectKeys("pad X (left face) is the B button", Gamepad_MapKeys(&pad).keys == B_BUTTON);
        pad.buttons = GAMEPAD_B;
        ExpectKeys("pad B (right face) is also the B button: Cancel and Back work from it", Gamepad_MapKeys(&pad).keys == B_BUTTON);
        pad.buttons = GAMEPAD_START;
        ExpectKeys("pad Start is Start", Gamepad_MapKeys(&pad).keys == START_BUTTON);
        pad.buttons = GAMEPAD_BACK;
        ExpectKeys("pad Back is Select", Gamepad_MapKeys(&pad).keys == SELECT_BUTTON);
        pad.buttons = GAMEPAD_LEFT_SHOULDER | GAMEPAD_RIGHT_SHOULDER;
        ExpectKeys("pad shoulders are L and R", Gamepad_MapKeys(&pad).keys == (L_BUTTON | R_BUTTON));
        pad.buttons = GAMEPAD_DPAD_UP | GAMEPAD_DPAD_LEFT;
        ExpectKeys("pad d-pad reaches the menus", Gamepad_MapKeys(&pad).keys == (DPAD_UP | DPAD_LEFT));
        pad.buttons = 0;
        pad.stickX = -32767;
        ExpectKeys("pad stick left is Left", Gamepad_MapKeys(&pad).keys == DPAD_LEFT);
        pad.stickX = 0;
        pad.buttons = GAMEPAD_Y;
        ExpectKeys("pad Y is the details key (and no GBA button)", Gamepad_MapKeys(&pad).details && Gamepad_MapKeys(&pad).keys == 0);
    }

    remove(sConfigPath);
    printf("selftest-keybinds: %d checks, %d failed\n", sKeyChecks, sKeyFailures);
    SDL_Quit();
    return sKeyFailures ? 1 : 0;
}

#ifdef _WIN32

// XInput is loaded when it is first needed, newest first: xinput1_4.dll
// (Windows 8 and later), xinput1_3.dll (the DirectX redistributable),
// xinput9_1_0.dll (every Windows since Vista). Linked directly, the game
// imported xinput1_3.dll and would not start on a PC without the DirectX
// redistributable. With none of them there is simply no controller.
typedef DWORD (WINAPI *XInputGetStateFunc)(DWORD, XINPUT_STATE *);
static XInputGetStateFunc sXInputGetState;
static bool sXInputLoaded;

static DWORD LoadedXInputGetState(DWORD index, XINPUT_STATE *state)
{
    if (!sXInputLoaded)
    {
        static const char *const names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        unsigned int i;

        sXInputLoaded = true;
        for (i = 0; i < ARRAY_COUNT(names) && sXInputGetState == NULL; i++)
        {
            HMODULE module = LoadLibraryA(names[i]);

            if (module != NULL)
                sXInputGetState = (XInputGetStateFunc)(void *)GetProcAddress(module, "XInputGetState");
        }
    }
    return sXInputGetState != NULL ? sXInputGetState(index, state) : ERROR_DEVICE_NOT_CONNECTED;
}

#ifdef ENABLE_NATIVE_UI
static bool sXInputDetails; // the pad's Y: no game button; the native UI's details view
#endif

u16 GetXInputKeys(void)
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = LoadedXInputGetState(0, &state);
    u16 xinputKeys = 0;
    sPadConnected = dwResult == ERROR_SUCCESS;
#ifdef ENABLE_NATIVE_UI
    sXInputDetails = false;
#endif

    if (dwResult == ERROR_SUCCESS)
    {
        // The button, stick and trigger mapping is platform/gamepad.c (unit tested on any machine).
        struct GamepadInput input = {state.Gamepad.wButtons, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                                     state.Gamepad.bRightTrigger};
        struct GamepadKeys mapped = Gamepad_MapKeys(&input);

        xinputKeys = mapped.keys;
#ifdef ENABLE_NATIVE_UI
        sXInputDetails = mapped.details;
#endif

        // Update 30: no player-facing speed-up (the right trigger and the Space key used to run the game 5x faster).
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
    if (sPcMenu.open)
    {
        KeyQueue_Reset(&sPadQueue);
        return 0; // the PC settings menu has the keys; the game keeps running
    }
    // The keyboard (and the pad) as the events left them since the last frame, one step at a time: KeyQueue_Take is this
    // frame's keys; a tap shorter than a frame is one frame down and the next up.
    u16 keyboard = KeyQueue_Take(&sKeyQueue);
#ifdef _WIN32
    KeyQueue_Set(&sPadQueue, GetXInputKeys());
    u16 gamepadKeys = KeyQueue_Take(&sPadQueue);
#ifdef ENABLE_NATIVE_UI
    NativeUi_NotePadKeys(gamepadKeys); // the prompts show the pad's buttons
    NativeUi_NotePadDetails(sXInputDetails);
#endif
    return gamepadKeys | keyboard;
#elif defined(__ANDROID__)
    return keyboard | controllerKeys | controllerAxisKeys;
#endif

    return keyboard;
}

#ifndef ENABLE_DIORAMA
// KNOWN_ISSUES MS-125, the Classic 2D game: a notice drawn over the GBA picture with the game's own font (the 3D game
// has the PC overlay's notice box). A dark band across the picture's foot, the text wrapped to its width, white with a
// shadow. Only letters, digits and . , : ! ? are drawn (the notices are written with them).
static int NoticeCode(char c)
{
    if (c >= 'A' && c <= 'Z')
        return CHAR_A + (c - 'A');
    if (c >= 'a' && c <= 'z')
        return CHAR_a + (c - 'a');
    if (c >= '0' && c <= '9')
        return CHAR_0 + (c - '0');
    switch (c)
    {
    case '.': return CHAR_PERIOD;
    case ',': return CHAR_COMMA;
    case ':': return CHAR_COLON;
    case '!': return CHAR_EXCL_MARK;
    case '?': return CHAR_QUESTION_MARK;
    default:  return CHAR_SPACE;
    }
}

static int NoticeWidth(const char *text, int length)
{
    int width = 0;

    for (int i = 0; i < length; i++)
        width += gFontNormalLatinGlyphWidths[NoticeCode(text[i])];
    return width;
}

static void DrawNoticeGlyph(uint16_t *pixels, int code, int left, int top)
{
    const u16 *glyph = gFontNormalLatinGlyphs + 0x20 * code;

    for (int tile = 0; tile < 4; tile++)
    {
        for (int row = 0; row < 8; row++)
        {
            u16 bits = glyph[tile * 8 + row];

            for (int px = 0; px < 8; px++)
            {
                int value = (bits >> (14 - 2 * px)) & 3;
                int x = left + (tile & 1) * 8 + px, y = top + (tile >> 1) * 8 + row;

                if (value == 0 || value == 3 || x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
                    continue;
                pixels[y * DISPLAY_WIDTH + x] = value == 1 ? 0x7FFF : 0x2108;   // the ink white, its shadow dark grey
            }
        }
    }
}

static void DrawGbaNotice(uint16_t *pixels, const char *text)
{
    const char *lines[4];
    int lengths[4], count = 0, top, i;
    const char *p = text;

    // wrap at spaces to the picture's width less a margin
    while (*p != '\0' && count < 4)
    {
        int length = 0, lastSpace = -1;

        while (p[length] != '\0' && NoticeWidth(p, length + 1) <= DISPLAY_WIDTH - 12)
        {
            if (p[length] == ' ')
                lastSpace = length;
            length++;
        }
        if (p[length] != '\0' && lastSpace > 0)
            length = lastSpace;
        lines[count] = p;
        lengths[count++] = length;
        p += length;
        while (*p == ' ')
            p++;
    }
    top = DISPLAY_HEIGHT - 6 - count * 14;
    for (i = (top - 4) * DISPLAY_WIDTH; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
        pixels[i] = (pixels[i] >> 2) & 0x1CE7;   // the band: the picture at a quarter of its brightness
    for (i = 0; i < count; i++)
    {
        int x = (DISPLAY_WIDTH - NoticeWidth(lines[i], lengths[i])) / 2;

        for (int c = 0; c < lengths[i]; c++)
        {
            int code = NoticeCode(lines[i][c]);

            DrawNoticeGlyph(pixels, code, x, top + i * 14 - 2);
            x += gFontNormalLatinGlyphWidths[code];
        }
    }
}
#endif

void VDraw(SDL_Texture *texture)
{
    static uint16_t gbaImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    static uint32_t image[DISPLAY_WIDTH * DISPLAY_HEIGHT];

    memset(gbaImage, 0, sizeof(gbaImage));
#ifdef ENABLE_DIORAMA
    // Frame boundary (the game thread is parked): the 3D battle reads the game
    // and says which layers its world stands in for in this picture.
    gDioramaSceneLayers = DioramaGLBattle_BeginFrame();
#endif
    DrawFrame(gbaImage);
    // A battle's stat stage tags beside the healthboxes (Classic 2D, and 3D battles that keep the game's healthboxes)
    StatStages_DrawGba(gbaImage);
    DebugScene_Frame(gbaImage);
#ifndef ENABLE_DIORAMA
    // KNOWN_ISSUES MS-125: a battle between players that stopped itself waits for the launcher; the Classic 2D game says
    // so over it, as the 3D game's overlay does (DrawPcOverlay)
    if (MultiplayerBattle_StoppedForRecovery() || sForceStoppedNotice)
        DrawGbaNotice(gbaImage, "This battle stopped. In the launcher: Recover battle, or Abandon battle.");
#endif
#if defined(DIORAMA_BATTLE_ENV_TEST) && !defined(ENABLE_DIORAMA)
    DioramaBattleEnvTest_Frame2D(gbaImage);   // test builds only: the Classic 2D test build's captures
#endif
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        uint16_t color = gbaImage[i];
        uint32_t r = (color & 0x1F) * 255 / 31;
        uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
        uint32_t b = ((color >> 10) & 0x1F) * 255 / 31;
        image[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
#ifdef ENABLE_DIORAMA
        // Where the world shows through: premultiplied alpha (gba_easy_draw.c).
        if (gDioramaSceneLayers)
            image[i] = ((16u - gDioramaSceneCover[i]) * 255u / 16u) << 24 | (r << 16) | (g << 8) | b;
#endif
    }
#ifdef ENABLE_DIORAMA
    DioramaGL_UploadFrame(image);
    DioramaGLBattle_UploadFrame(image);
#else
    SDL_UpdateTexture(texture, NULL, image, DISPLAY_WIDTH * sizeof(Uint32));
#endif
    REG_VCOUNT = 161; // prep for being in VBlank period
}

int DoMain(void *data)
{
    CrashReport_ThreadStarted();
    // A soft reset longjmps back here with game memory restored, like the GBA
    // jumping to the cartridge entry point after clearing work RAM.
    setjmp(sGameResetPoint);
    AgbMain();
    return 0;
}

static u32 sVBlankWaits; // VBlankIntrWait calls; only the game thread touches it

void VBlankIntrWait(void)
{
    sVBlankWaits++;
    SDL_AtomicSet(&isFrameAvailable, 1);
    SDL_SemWait(vBlankSemaphore);
}

// A per-frame state machine polls a few times per frame; a tight loop polls
// thousands of times. Waiting only past this many polls keeps the first case
// exactly as on the GBA while unblocking the second.
#define SPIN_POLLS_BEFORE_VBLANK 64

bool32 Platform_SpinWaitForVBlank(void)
{
    static u32 sFrame, sPolls;

    if (sFrame != sVBlankWaits)
    {
        sFrame = sVBlankWaits;
        sPolls = 0;
    }
    if (++sPolls < SPIN_POLLS_BEFORE_VBLANK)
        return FALSE;
    sPolls = 0;
    VBlankIntrWait();
    return TRUE;
}

// BIOS SWI 0x00/0x01 as used by DoSoftReset. Runs on the game thread while the
// main thread waits for the next frame, so no other thread touches game memory.
// Game globals and the emulated hardware return to their power-on values; the
// window, audio device, save image (the cartridge) and platform state are kept.
void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    if (!GameMemory_Reset())
    {
        puts("Soft reset unavailable (no memory snapshot); exiting.");
        exit(0);
    }
    if (sdlAudioDevice != 0)
        SDL_ClearQueuedAudio(sdlAudioDevice);
    longjmp(sGameResetPoint, 1);
}

#endif

#ifdef ENABLE_NATIVE_UI
bool8 Platform_NativeUiPcMenuOpen(void)
{
    return sPcMenu.open;
}

void Platform_NativeUiKeyLabel(int action, char *out, int size)
{
    if (size <= 0)
        return;
    out[0] = '\0';
    if (action >= 0 && action < KEYBIND_COUNT)
        Keybinds_KeyLabel(sKeybinds.key[action], &sKeyNames, out, size);
}

void Platform_NativeUiSaveWrites(u32 *written, u32 *failed, bool8 *pending)
{
    *written = sSaveWritesDone;
    *failed = sSaveWritesFailed;
    *pending = gPlatformFlashDirty;
}

void Platform_NativeUiPictureRect(int outputWidth, int outputHeight, int *x, int *y, int *w, int *h,
                                  bool8 *fieldFillsWindow)
{
    // As the compositor places it (DioramaGL_Present): whole-number steps, centred.
    int scale = DisplaySettings_PixelScale(outputWidth, outputHeight, sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE]);

    *w = DISPLAY_WIDTH * scale;
    *h = DISPLAY_HEIGHT * scale;
    *x = (outputWidth - *w) / 2;
    *y = (outputHeight - *h) / 2;
#ifdef ENABLE_DIORAMA
    *fieldFillsWindow = DioramaGL_DrewField3D();
#else
    *fieldFillsWindow = FALSE;
#endif
}
#endif
