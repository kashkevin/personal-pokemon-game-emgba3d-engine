#ifdef PLATFORM_SDL2
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

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
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/mp_bridge.h"
#ifdef ENABLE_DIORAMA
#include "diorama/battle_scene.h"
#include "diorama/gl_battle.h"
#include "diorama/gl_compositor.h"
#include "diorama/scene_snapshot.h"
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
bool speedUp = false;
unsigned int videoScale = 1;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;

static char sSavePath[1024] = "pokeemerald-mp.sav";
static int sSoftResetChordFrames; // Ctrl+R holds the reset chord for the game to see
extern volatile bool8 gPlatformFlashDirty; // src/platform/agb_flash_pc.c
static char sConfigPath[1024] = "pokeemerald.cfg";
static u8 sBorderBackground;
static bool sHasBorderBackgroundConfig;
static u8 sBackgroundOrderVersion;
static u8 sPlatformSettings[PLATFORM_SETTING_COUNT] = {0, 4, 0, 1, 1, 10, 5};
#ifdef __ANDROID__
static SDL_GameController *androidController;
#endif

extern void AgbMain(void);
extern void DoSoftReset(void);

int DoMain(void *param);
void ProcessEvents(void);
void VDraw(SDL_Texture *texture);

static void ReadSaveFile(const char *path);
static void FlushSaveIfDirty(void);
static void ReadConfigFile(void);
static void ParseArguments(int argc, char **argv);

// Latched screen-reader hotkeys; see READER_KEY_* in platform.h.
static u16 sReaderKeys = 0;
static void StoreConfigFile(void);
static void ApplyPlatformSettings(void);
static void StoreSaveFile(void);

#ifdef ENABLE_DIORAMA
#define DIORAMA_VIDEO_SETTING_EVENT 1
static void ApplyDioramaVideoSetting(enum PlatformSetting setting, u8 value);
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
extern bool32 GameMemory_Reset(void);

static jmp_buf sGameResetPoint; // where the game thread restarts after a soft reset

// Optional frame-rate log (PKMN_PERF_LOG=1): every 5 seconds, game frames and
// presented frames per second and the average time spent drawing and presenting.
static bool8 sPerfLog;
static u32 sPerfGameFrames;
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
    SDL_Log("perf: %.1f game fps, %.1f presented fps, %.2f ms per present",
            sPerfGameFrames / seconds, sPerfPresents / seconds,
            (double)sPerfPresentTicks * 1000.0 / frequency / sPerfPresents);
    sPerfGameFrames = 0;
    sPerfPresents = 0;
    sPerfPresentTicks = 0;
    sPerfWindowStart = end;
}

int main(int argc, char **argv)
{
    FaultCompat_Init(); // GBA results for integer division by zero
    NullTrap_Init(); // development aid; no-op unless PKMN_NULL_TRAP=1 on Linux
    CrashReport_Init(); // writes crash.log if the game crashes

    // Open an output console on Windows
#ifdef _WIN32
    AllocConsole() ;
    AttachConsole( GetCurrentProcessId() ) ;
    freopen( "CON", "w", stdout ) ;
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
        return 1;
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
    sdlWindow = SDL_CreateWindow("Pokemon Emerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
#ifdef ENABLE_DIORAMA
                                 | SDL_WINDOW_OPENGL
#endif
                                 );
#else
    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (sdlWindow == NULL)
    {
        DBGPRINTF("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
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
        return 1;
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
        DioramaGL_Shutdown();
        SDL_DestroyWindow(sdlWindow);
        SDL_Quit();
        return 1;
    }
    DioramaSnapshotExchange_Init();
    DioramaScene_Init();
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
    mainLoopThread = SDL_CreateThread(DoMain, "AgbMain", NULL);
    PerfLog_Init();

    double accumulator = 0.0;

    while (isRunning)
    {
        ProcessEvents();

        if (!paused)
        {
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            curGameTime = SDL_GetPerformanceCounter();
            double deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
            if (deltaTime > (dt * 5))
                deltaTime = dt;
            lastGameTime = curGameTime;

            accumulator += deltaTime;

            while (accumulator >= dt)
            {
                if (SDL_AtomicGet(&isFrameAvailable))
                {
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
                    int gameHeight;
                    int gameWidth;
                    if (sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE])
                    {
                        int scale = outputWidth / DISPLAY_WIDTH;
                        if (outputHeight / DISPLAY_HEIGHT < scale)
                            scale = outputHeight / DISPLAY_HEIGHT;
                        if (scale < 1)
                            scale = 1;
                        gameWidth = DISPLAY_WIDTH * scale;
                        gameHeight = DISPLAY_HEIGHT * scale;
                    }
                    else
                    {
                        gameHeight = outputHeight * 8 / 9;
                        gameWidth = gameHeight * 3 / 2;
                    }
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
                    {
                        u16 keys = Platform_GetKeyInput();
                        if (sSoftResetChordFrames > 0)
                        {
                            keys |= A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON;
                            sSoftResetChordFrames--;
                        }
                        // In multiplayer the reset chord reaches the game only when
                        // its native reset handler has been offered it.
                        keys = MpBridge_FilterKeys(keys);
                        REG_KEYINPUT = ~keys & KEYS_MASK;
                    }

                    SDL_SemPost(vBlankSemaphore);
                    sPerfGameFrames++;

                    accumulator -= dt;
                }
            }
        }

#ifndef __ANDROID__
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
#else
        SDL_RenderPresent(sdlRenderer);
#endif
        PerfLog_Presented(presentStart, SDL_GetPerformanceCounter());
#endif
    }

    FlushSaveIfDirty();
    MpBridge_Shutdown();

#ifdef ENABLE_DIORAMA
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
        else if (strcmp(argv[i], "--bridge") == 0 && i + 1 < argc)
        {
            // Multiplayer (src/platform/mp_bridge.c): never start without the
            // bridge that was asked for.
            if (!MpBridge_Configure(argv[++i]))
                exit(1);
        }
        else if (strcmp(argv[i], "--selftest-decompress") == 0 && i + 1 < argc)
            exit(NativeSelfTest_Decompress(argv[++i])); // src/platform/native_selftest.c
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

static void ReadConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "r");
    char line[64];
    unsigned int value;

    if (configFile == NULL)
        return;
    while (fgets(line, sizeof(line), configFile) != NULL)
    {
        if (sscanf(line, "borderBackground=%u", &value) == 1 && value < 16)
        {
            sBorderBackground = value;
            sHasBorderBackgroundConfig = true;
        }
        else if (sscanf(line, "backgroundOrder=%u", &value) == 1)
            sBackgroundOrderVersion = value;
        else if (sscanf(line, "fullscreen=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_FULLSCREEN] = value != 0;
        else if (sscanf(line, "windowScale=%u", &value) == 1 && value >= 2 && value <= 6) // 6 = 1920x1080
            sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE] = value;
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
    }
    fclose(configFile);
}

static void StoreConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "w");

    if (configFile == NULL)
        return;
    fprintf(configFile, "borderBackground=%u\n", sBorderBackground);
    fprintf(configFile, "backgroundOrder=2\n");
    fprintf(configFile, "fullscreen=%u\n", sPlatformSettings[PLATFORM_SETTING_FULLSCREEN]);
    fprintf(configFile, "windowScale=%u\n", sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE]);
    fprintf(configFile, "integerScale=%u\n", sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE]);
    fprintf(configFile, "vsync=%u\n", sPlatformSettings[PLATFORM_SETTING_VSYNC]);
    fprintf(configFile, "border=%u\n", sPlatformSettings[PLATFORM_SETTING_BORDER]);
    fprintf(configFile, "volume=%u\n", sPlatformSettings[PLATFORM_SETTING_VOLUME]);
    fprintf(configFile, "cryVolume=%u\n", sPlatformSettings[PLATFORM_SETTING_CRY_VOLUME]);
    fclose(configFile);
}

static void ApplyPlatformSettings(void)
{
#ifdef ENABLE_DIORAMA
    DioramaGL_SetVSync(sPlatformSettings[PLATFORM_SETTING_VSYNC]);
#else
    SDL_RenderSetVSync(sdlRenderer, sPlatformSettings[PLATFORM_SETTING_VSYNC]);
#endif
#if defined(NATIVE_LINUX) || defined(_WIN32)
    SDL_SetWindowFullscreen(sdlWindow, sPlatformSettings[PLATFORM_SETTING_FULLSCREEN]
                                      ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!sPlatformSettings[PLATFORM_SETTING_FULLSCREEN])
    {
        int scale = sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE];
        SDL_SetWindowSize(sdlWindow, 320 * scale, 180 * scale);
        SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
#endif
}

#ifdef ENABLE_DIORAMA
static void ApplyDioramaVideoSetting(enum PlatformSetting setting, u8 value)
{
    if (setting == PLATFORM_SETTING_VSYNC)
    {
        DioramaGL_SetVSync(value);
    }
    else if (setting == PLATFORM_SETTING_FULLSCREEN)
    {
        SDL_SetWindowFullscreen(sdlWindow, value ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!value)
        {
            int scale = sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE];
            SDL_SetWindowSize(sdlWindow, 320 * scale, 180 * scale);
            SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    else if (setting == PLATFORM_SETTING_WINDOW_SCALE && !sPlatformSettings[PLATFORM_SETTING_FULLSCREEN])
    {
        SDL_SetWindowSize(sdlWindow, 320 * value, 180 * value);
        SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}
#endif

// Writes the whole flash image to a temporary file and renames it over the save,
// so a crash or power cut never leaves a half-written file behind.
static void StoreSaveFile(void)
{
    char tempPath[sizeof(sSavePath) + 8];
    FILE *saveFile;
    size_t written;

    SDL_snprintf(tempPath, sizeof(tempPath), "%s.tmp", sSavePath);
    saveFile = fopen(tempPath, "wb");
    if (saveFile == NULL)
    {
        SDL_Log("Unable to write save file %s", tempPath);
        return;
    }
    written = fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), saveFile);
    if (fflush(saveFile) != 0 || written != sizeof(FLASH_BASE))
    {
        fclose(saveFile);
        SDL_Log("Short write to %s", tempPath);
        return;
    }
    fclose(saveFile);
#ifdef _WIN32
    if (!MoveFileExA(tempPath, sSavePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        SDL_Log("Unable to replace save file %s", sSavePath);
#else
    if (rename(tempPath, sSavePath) != 0)
        SDL_Log("Unable to replace save file %s", sSavePath);
#endif
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

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    if (sdlAudioDevice != 0)
    {
        int floatCount = samplesPerFrame / sizeof(float);
        float adjustedAudio[floatCount];
        // The mixer leaves output headroom, so full volume sounds quiet. Apply a
        // modest make-up gain and hard-clamp to [-1,1] so loud passages can't clip.
        float volume = (sPlatformSettings[PLATFORM_SETTING_VOLUME] / 10.0f) * 2.0f;
        for (int i = 0; i < floatCount; i++)
        {
            float s = audioBuffer[i] * volume;
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            adjustedAudio[i] = s;
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
    else if (setting == PLATFORM_SETTING_FULLSCREEN)
    {
        SDL_SetWindowFullscreen(sdlWindow, value ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!value)
        {
            int scale = sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE];
            SDL_SetWindowSize(sdlWindow, 320 * scale, 180 * scale);
            SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    else if (setting == PLATFORM_SETTING_WINDOW_SCALE && !sPlatformSettings[PLATFORM_SETTING_FULLSCREEN])
    {
        SDL_SetWindowSize(sdlWindow, 320 * value, 180 * value);
        SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
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


// Key mappings
#define KEY_A_BUTTON      SDLK_z
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_a
#define KEY_R_BUTTON      SDLK_s
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key) \
case KEY_##key:  keyboardKeys &= ~key; break;

#define HANDLE_KEYDOWN(key) \
case KEY_##key:  keyboardKeys |= key; break;

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
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
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
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYUP(A_BUTTON)
            HANDLE_KEYUP(B_BUTTON)
            HANDLE_KEYUP(START_BUTTON)
            HANDLE_KEYUP(SELECT_BUTTON)
            HANDLE_KEYUP(L_BUTTON)
            HANDLE_KEYUP(R_BUTTON)
            HANDLE_KEYUP(DPAD_UP)
            HANDLE_KEYUP(DPAD_DOWN)
            HANDLE_KEYUP(DPAD_LEFT)
            HANDLE_KEYUP(DPAD_RIGHT)
            case SDLK_SPACE:
                if (speedUp)
                {
                    speedUp = false;
                    timeScale = 1.0;
                    SDL_ClearQueuedAudio(sdlAudioDevice);
                    SDL_PauseAudioDevice(sdlAudioDevice, 0);
                }
                break;
            }
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYDOWN(A_BUTTON)
            HANDLE_KEYDOWN(B_BUTTON)
            HANDLE_KEYDOWN(START_BUTTON)
            HANDLE_KEYDOWN(SELECT_BUTTON)
            HANDLE_KEYDOWN(L_BUTTON)
            HANDLE_KEYDOWN(R_BUTTON)
            HANDLE_KEYDOWN(DPAD_UP)
            HANDLE_KEYDOWN(DPAD_DOWN)
            HANDLE_KEYDOWN(DPAD_LEFT)
            HANDLE_KEYDOWN(DPAD_RIGHT)
            case SDLK_r:
                // Ctrl+R: hold A+B+START+SELECT for one frame (the chord is awkward
                // on a keyboard). The game decides what it means: this multiplayer
                // build hands the chord to the Local Soft Reset handler
                // (MultiplayerLocalReset_HandleChord) and never reboots from it.
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                    sSoftResetChordFrames = 1;
                break;
            case SDLK_p:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;
#ifdef ENABLE_DIORAMA
                    if (!paused)
                        DioramaGL_EndSurvey();
#endif
                }
                break;
            case SDLK_F11:
                if (event.key.repeat == 0)
                    Platform_SetSetting(PLATFORM_SETTING_FULLSCREEN,
                                        !sPlatformSettings[PLATFORM_SETTING_FULLSCREEN]);
                break;
            case SDLK_SPACE:
                if (!speedUp)
                {
                    speedUp = true;
                    timeScale = 5.0;
                    SDL_PauseAudioDevice(sdlAudioDevice, 1);
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
                if (event.key.repeat == 0)
                {
                    paused = true;
                    DioramaGL_CycleSurveyView();
                }
                break;
            case SDLK_F6:
                if (event.key.repeat == 0)
                    DioramaGL_RequestSurveyCapture();
                break;
            case SDLK_COMMA:
                DioramaGL_AdjustCameraPitch(-1);
                break;
            case SDLK_PERIOD:
                DioramaGL_AdjustCameraPitch(1);
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

#ifdef _WIN32
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;

        if (oldTimeScale != timeScale)
        {
            if (timeScale > 1.0)
            {
                SDL_PauseAudioDevice(sdlAudioDevice, 1);
            }
            else
            {
                SDL_ClearQueuedAudio(sdlAudioDevice);
                SDL_PauseAudioDevice(sdlAudioDevice, 0);
            }
        }
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
#ifdef _WIN32
    u16 gamepadKeys = GetXInputKeys();
    return gamepadKeys | keyboardKeys;
#elif defined(__ANDROID__)
    return keyboardKeys | controllerKeys | controllerAxisKeys;
#endif

    return keyboardKeys;
}

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
