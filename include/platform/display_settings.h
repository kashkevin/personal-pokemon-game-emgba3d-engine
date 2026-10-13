#ifndef GUARD_PLATFORM_DISPLAY_SETTINGS_H
#define GUARD_PLATFORM_DISPLAY_SETTINGS_H

/*
 * Display and graphics settings of the native PC game, as pokeemerald.cfg keeps
 * them: the chosen resolution, how the window is shown, and the 3D view's
 * quality. Pure logic without SDL (tools/pc_port/tests checks it);
 * src/platform/sdl2.c applies it to the window and the renderer.
 *
 * A setting is written to the file only once it is the player's (read from the
 * file or chosen in the PC settings menu), as the camera and tree styles are:
 * a file that never held one follows the defaults.
 */
#include <stdbool.h>
#include <stdio.h>

enum DisplayMode
{
    DISPLAY_MODE_WINDOWED,
    DISPLAY_MODE_BORDERLESS, /* a window covering the display at its own size */
    DISPLAY_MODE_FULLSCREEN, /* exclusive: the display switches to the resolution */
    DISPLAY_MODE_COUNT,
};

enum ShadowQuality
{
    SHADOWS_OFF,
    SHADOWS_LOW,    /* 1024x1024 sun depth map */
    SHADOWS_MEDIUM, /* 2048x2048, the look before these settings */
    SHADOWS_HIGH,   /* 4096x4096 */
    SHADOWS_COUNT,
};

enum QualityPreset
{
    QUALITY_LOW,
    QUALITY_MEDIUM,
    QUALITY_HIGH, /* the default: the look before these settings */
    QUALITY_ULTRA,
    QUALITY_CUSTOM,
    QUALITY_COUNT,
};

struct DisplayResolution
{
    int width;
    int height;
};

/* 1280x720 (performance), 1920x1080 (the default), 2560x1440, 3840x2160. */
#define DISPLAY_RESOLUTION_COUNT 4
#define DISPLAY_DEFAULT_RESOLUTION 1
extern const struct DisplayResolution gDisplayResolutions[DISPLAY_RESOLUTION_COUNT];

/*
 * Frame rate limits (performance work 2026-10-11): at most this many frames a
 * second are drawn; 0 is no limit, the display's VSync alone deciding. The
 * game itself always runs its 59.73 frames a second (the GBA's) - a higher
 * frame rate draws the same game more smoothly, it never runs it faster.
 * In the menu's order: 60, 120, 144, 165, 180, 200, 240, unlimited.
 */
#define DISPLAY_FPS_CAP_COUNT 8
extern const unsigned short gDisplayFpsCaps[DISPLAY_FPS_CAP_COUNT];
#define DISPLAY_FPS_CAP_MIN 30
#define DISPLAY_FPS_CAP_MAX 1000

/*
 * The 3D scene's render scale: the share of the resolution's height the 3D
 * world is drawn at, then scaled up to it (100: native, every pixel drawn).
 * The game's pixel-art picture and text are always drawn at the window's own
 * resolution. In the menu's order: 100, 83, 75, 67, 50.
 */
#define DISPLAY_RENDER_SCALE_COUNT 5
extern const unsigned char gDisplayRenderScales[DISPLAY_RENDER_SCALE_COUNT];
#define DISPLAY_RENDER_SCALE_MIN 50

/*
 * The memory budget (performance work 2026-10-11): how far the 3D view's caches
 * may grow, in MB; 0 is automatic (an eighth of the PC's memory, 256 MB to
 * 1 GB). A budget, not an allocation: memory is used only as the caches fill
 * (today the terrain's chunk cache, in graphics memory) and the game itself
 * needs about 200 MB whatever the budget. The game is a 32-bit program with
 * 4 GB of address space, and a budget is held to what is free there and to half
 * the PC's memory. In the menu's order: auto, 512, 1024, 2048, 4096, 8192.
 */
#define DISPLAY_MEMORY_BUDGET_COUNT 6
extern const unsigned short gDisplayMemoryBudgets[DISPLAY_MEMORY_BUDGET_COUNT];

/* Any resolution in the file within these bounds is honoured (the test
 * harnesses use small windows). */
#define DISPLAY_MIN_WIDTH 320
#define DISPLAY_MIN_HEIGHT 180
#define DISPLAY_MAX_WIDTH 7680
#define DISPLAY_MAX_HEIGHT 4320

struct DisplaySettings
{
    int width; /* the chosen resolution */
    int height;
    unsigned char mode;           /* enum DisplayMode */
    unsigned char fullscreenMode; /* what F11 switches to: borderless or fullscreen */
    unsigned char quality;        /* enum QualityPreset */
    unsigned char shadows;        /* enum ShadowQuality */
    bool lighting;                /* weather light, sun dapples, colour grade */
    bool effects;                 /* dust, sun rays, bloom, mist banks */
    bool details;                 /* the 3D view's world detail objects: grass, flowers,
                                     signs, fences, lamps, rocks standing up */
    unsigned char antialiasing;   /* multisamples: 0, 2, 4 or 8 */
    unsigned short fpsCap;        /* frames a second at most; 0: no limit */
    unsigned char renderScale;    /* percent; 100: native */
    unsigned short memoryBudget;  /* MB; 0: automatic */
    bool resolutionKept;
    bool modeKept;
    bool qualityKept;
    bool qualityNamed; /* the file named a preset (parsing only) */
    bool fpsCapKept;
    bool renderScaleKept;
    bool memoryBudgetKept;
};

void DisplaySettings_Defaults(struct DisplaySettings *settings);
/* One line of the settings file; true if it held a display setting. Call
 * DisplaySettings_Finish after the last line. */
bool DisplaySettings_ParseLine(struct DisplaySettings *settings, const char *line);
void DisplaySettings_Finish(struct DisplaySettings *settings);
/* Files written before these keys existed (pass -1 for a key the file did
 * not hold): fullscreen=1 was the borderless kind; windowScale sized the
 * window in steps of 320x180, but 4 (1280x720) was every file's default and
 * is not taken for a choice. */
void DisplaySettings_ApplyLegacy(struct DisplaySettings *settings, int fullscreen, int windowScale);
void DisplaySettings_Write(const struct DisplaySettings *settings, FILE *file);
/* The legacy windowScale value for this resolution, or 0 if it has none. */
int DisplaySettings_LegacyWindowScale(const struct DisplaySettings *settings);

/* A preset's shadows, lighting, effects, world details and antialiasing. */
void DisplaySettings_SetQuality(struct DisplaySettings *settings, unsigned char preset);
/* The preset these values are, or QUALITY_CUSTOM. */
unsigned char DisplaySettings_MatchQuality(const struct DisplaySettings *settings);

/* The window's size for a resolution where boundsWidth x boundsHeight is free:
 * the resolution itself if it fits, otherwise the largest size of the same
 * shape that does (never below DISPLAY_MIN_*). */
void DisplaySettings_FitWindow(int width, int height, int boundsWidth, int boundsHeight,
                               int *outWidth, int *outHeight);
/* The 3D scene's size for a drawable of outputWidth x outputHeight: the chosen
 * resolution's height at the drawable's own shape, so nothing is stretched. */
void DisplaySettings_RenderSize(int height, int outputWidth, int outputHeight,
                                int *renderWidth, int *renderHeight);
/* The whole-number scale of the 240x160 picture, so pixel art stays sharp:
 * with 'fill' the largest that fits, otherwise the largest within 8/9 of the
 * height (6 at 1080p, 8 at 1440p, 12 at 2160p, as before at 16:9). At least 1. */
int DisplaySettings_PixelScale(int outputWidth, int outputHeight, bool fill);
/* The 3D scene's height for a target height at a render scale (100: the same). */
int DisplaySettings_ScaledHeight(int height, unsigned char renderScale);
/* The next frame rate limit in the menu's order (direction 1 or -1); a value of
 * the file's own goes to the nearest listed one that way. */
unsigned short DisplaySettings_StepFpsCap(unsigned short fpsCap, int direction);
/* The next render scale in the menu's order (1: smaller, -1: larger). */
unsigned char DisplaySettings_StepRenderScale(unsigned char renderScale, int direction);
/* The next memory budget in the menu's order (1: larger). */
unsigned short DisplaySettings_StepMemoryBudget(unsigned short budget, int direction);
/* The budget in effect, in MB: automatic or chosen, held to half the PC's memory
 * and to the free address space less what the game itself needs (0: unknown). */
unsigned DisplaySettings_EffectiveMemoryBudget(unsigned short budget, unsigned physicalMB,
                                               unsigned addressSpaceFreeMB);
/* The terrain chunk cache for a budget: 100 (the grid's) to 1024 chunks. */
unsigned DisplaySettings_TerrainCacheChunks(unsigned budgetMB);
/* The sun depth map's side for a shadow quality; 0 for none. */
int DisplaySettings_ShadowMapSize(unsigned char shadows);

const char *DisplaySettings_ModeName(unsigned char mode);
const char *DisplaySettings_QualityName(unsigned char quality);
const char *DisplaySettings_ShadowName(unsigned char shadows);
/* The index of a resolution in gDisplayResolutions, or -1. */
int DisplaySettings_ResolutionIndex(int width, int height);

#endif
