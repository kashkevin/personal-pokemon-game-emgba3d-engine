#ifdef ENABLE_NATIVE_UI
/*
 * The native PC UI's controller (see native_ui/native_ui.h): the screen
 * registry, the frame-boundary capture, the picture policy, mouse input and
 * the button-press queue, the development switches. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "palette.h"
#include "native_ui/native_ui.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

/* src/platform/gba_easy_draw.c: GBA background layers left out of the picture */
extern uint8_t gPlatformPictureHideBgMask;

/* The screens in detection order (the first that detects wins). */
static const struct UiScreen *const sScreens[] = {
    &gUiScreen_Title,
    &gUiScreen_MainMenu,
    &gUiScreen_Options,
    &gUiScreen_Pause,
    &gUiScreen_Party,
    &gUiScreen_Summary,
};

static bool sInitialized;
static bool sEnabled;
static bool sTestCard;
static bool sLog;
static enum UiSize sSize;
static char sOnly[256];      /* PKMN_NATIVE_UI_SCREENS, "" = all */
static SDL_Window *sWindow;

static const struct UiScreen *sActive;
static uint32_t sActiveSince;
static uint32_t sFrame;
static int sPictureFlags;

/* mouse, in canvas coordinates of the last drawn frame */
static int sMouseX = -1;
static int sMouseY = -1;
static bool sMouseInside;

/* the press queue: each step holds its keys for one game frame */
#define MAX_PRESS_STEPS 64
static uint16_t sPressSteps[MAX_PRESS_STEPS];
static int sPressHead;
static int sPressCount;
static uint16_t sPrevKeys;        /* the player's own buttons last frame */
static uint16_t sPrevEngineKeys;  /* what the game got last frame */
static uint16_t sSwallowed;       /* the player's buttons a screen took, until released */

/* the 60 Hz step's time (for the smooth alpha between steps) */
static Uint64 sCaptureCounter;
/* the engine's full-screen fade at the last frame boundary */
static UiRGBA sScreenFade;
static float sLastDrawMs;
static double sDrawMsSum;
static int sDrawSamples;
static Uint32 sLastLogMs;

static bool ScreenAllowed(const char *name)
{
    const char *p;
    size_t len = strlen(name);

    if (sOnly[0] == '\0')
        return true;
    for (p = sOnly; (p = strstr(p, name)) != NULL; p += len)
        if ((p == sOnly || p[-1] == ',') && (p[len] == '\0' || p[len] == ','))
            return true;
    return false;
}

bool NativeUi_Init(SDL_Window *window)
{
    const char *env;

    sWindow = window;
    env = SDL_getenv("PKMN_NATIVE_UI");
    sEnabled = !(env != NULL && strcmp(env, "0") == 0);
    env = SDL_getenv("PKMN_NATIVE_UI_SCREENS");
    SDL_strlcpy(sOnly, env != NULL ? env : "", sizeof(sOnly));
    env = SDL_getenv("PKMN_NATIVE_UI_TESTCARD");
    sTestCard = env != NULL && strcmp(env, "1") == 0;
    env = SDL_getenv("PKMN_NATIVE_UI_SIZE");
    sSize = env != NULL && SDL_strcasecmp(env, "small") == 0 ? UI_SIZE_SMALL : UI_SIZE_LARGE;
    env = SDL_getenv("PKMN_NATIVE_UI_LOG");
    sLog = env != NULL && strcmp(env, "1") == 0;
    env = SDL_getenv("PKMN_NATIVE_UI_PROMPTS");
    if (env != NULL && (strcmp(env, "pad") == 0 || strcmp(env, "keyboard") == 0))
        UiW_ForcePromptDevice(strcmp(env, "pad") == 0 ? UI_DEVICE_PAD : UI_DEVICE_KEYBOARD);
    if (!sEnabled)
    {
        SDL_Log("Native UI: off (PKMN_NATIVE_UI=0): the GBA presentation is shown");
        return true;
    }
    if (!UiDraw_Init() || !UiText_Init() || !UiGfx_Init())
    {
        SDL_Log("Native UI: could not start; the GBA presentation is shown");
        sEnabled = false;
        return false;
    }
    sInitialized = true;
    SDL_Log("Native UI: on%s%s%s%s", sOnly[0] ? " (screens: " : "", sOnly, sOnly[0] ? ")" : "",
            sTestCard ? ", test card" : "");
    return true;
}

void NativeUi_Shutdown(void)
{
    if (!sInitialized)
        return;
    UiGfx_Shutdown();
    UiDraw_Shutdown();
    UiKit_Shutdown();
    sInitialized = false;
}

bool NativeUi_Enabled(void)
{
    return sEnabled && sInitialized;
}

bool NativeUi_LogEnabled(void)
{
    return sLog;
}

uint32_t NativeUi_Frame(void)
{
    return sFrame;
}

/* ---------------------------------------------------------------- frame boundary */

/* A palette fade over every palette is a fade of the whole picture: the
 * replaced screens show it as a veil of the fade's colour. */
static UiRGBA CaptureScreenFade(void)
{
    u32 y = gPaletteFade.y;
    u32 c = gPaletteFade.blendColor;

    /* mode 0: NORMAL_FADE (palette.c); a fade that has settled part-way is
     * a tint (time of day, a dimmed menu), not a transition */
    if (y == 0 || gPaletteFade.mode != 0 || gPaletteFadeSelectedPalettes != PALETTES_ALL
     || (!gPaletteFade.active && y < 16))
        return UI_NONE;
    return UI_RGBA(((c & 0x1F) << 3) | ((c & 0x1F) >> 2), (((c >> 5) & 0x1F) << 3) | (((c >> 5) & 0x1F) >> 2),
                   (((c >> 10) & 0x1F) << 3) | (((c >> 10) & 0x1F) >> 2), y >= 16 ? 255 : (int)(y * 255 / 16));
}

UiRGBA NativeUi_ScreenFade(void)
{
    return sScreenFade;
}

double NativeUi_FrameAlpha(void)
{
    double a = (double)(SDL_GetPerformanceCounter() - sCaptureCounter) * 60.0 / (double)SDL_GetPerformanceFrequency();

    return a < 0 ? 0 : a > 1 ? 1 : a;
}

void NativeUi_CaptureFrame(void)
{
    const struct UiScreen *found = NULL;
    enum UiPicture picture = UI_PICTURE_KEEP;

    if (!NativeUi_Enabled())
        return;
    sFrame++;
    sCaptureCounter = SDL_GetPerformanceCounter();
    sScreenFade = CaptureScreenFade();
    for (size_t i = 0; i < SDL_arraysize(sScreens); i++)
    {
        const struct UiScreen *s = sScreens[i];

        if (s == NULL || !ScreenAllowed(s->name))
            continue;
        if (s->detect())
        {
            found = s;
            break;
        }
    }
    if (found != sActive)
    {
        if (sLog)
            SDL_Log("Native UI: screen %s -> %s", sActive != NULL ? sActive->name : "(game)",
                    found != NULL ? found->name : "(game)");
        sActive = found;
        sActiveSince = sFrame;
        /* a new screen starts with no queued presses from the old one */
        sPressCount = 0;
    }
    if (sActive != NULL)
    {
        if (sActive->capture != NULL)
            sActive->capture();
        picture = sActive->picture != NULL ? sActive->picture() : UI_PICTURE_HIDE_ALL;
    }
    switch (picture)
    {
    case UI_PICTURE_HIDE_ALL:
        sPictureFlags = NATIVE_UI_HIDE_PICTURE | NATIVE_UI_HIDE_UI_LAYER;
        gPlatformPictureHideBgMask = 0;
        break;
    case UI_PICTURE_HIDE_BG0:
        sPictureFlags = NATIVE_UI_HIDE_UI_LAYER;
        gPlatformPictureHideBgMask = 1 << 0;
        break;
    default:
        sPictureFlags = 0;
        gPlatformPictureHideBgMask = 0;
        break;
    }
}

int NativeUi_PictureFlags(void)
{
    return NativeUi_Enabled() ? sPictureFlags : 0;
}

void NativeUi_QueuePress(uint16_t key)
{
    NativeUi_QueuePresses(key, 1);
}

void NativeUi_QueuePresses(uint16_t key, int count)
{
    for (int i = 0; i < count && sPressCount + 2 <= MAX_PRESS_STEPS; i++)
    {
        sPressSteps[(sPressHead + sPressCount++) % MAX_PRESS_STEPS] = key;
        sPressSteps[(sPressHead + sPressCount++) % MAX_PRESS_STEPS] = 0;
    }
}

bool NativeUi_PressesPending(void)
{
    return sPressCount > 0;
}

uint16_t NativeUi_FilterKeys(uint16_t keys)
{
    uint16_t real = keys;
    uint16_t newKeys = keys & ~sPrevKeys;

    if (!NativeUi_Enabled())
        return keys;
    sPrevKeys = real;
    if (Platform_NativeUiPcMenuOpen())
    {
        /* the PC menu has the buttons: nothing queued lands meanwhile */
        sPressCount = 0;
        sPrevEngineKeys = keys;
        return keys;
    }
    if (sActive != NULL && sActive->filterKeys != NULL)
    {
        uint16_t filtered = sActive->filterKeys(keys, newKeys);

        /* a button the screen took stays away from the game until it is
         * released (else the game would see it go down a frame later) */
        sSwallowed |= keys & ~filtered;
        keys = filtered;
    }
    sSwallowed &= real;
    keys &= ~sSwallowed;
    /* A queued step waits until the screen reads buttons, and a press until
     * its button is up: the engine must see it go down (one edge a step). */
    if (sPressCount > 0 && (sActive == NULL || sActive->ready == NULL || sActive->ready()))
    {
        uint16_t step = sPressSteps[sPressHead];

        if (step == 0 || ((keys | sPrevEngineKeys) & step) == 0)
        {
            keys |= step;
            sPressHead = (sPressHead + 1) % MAX_PRESS_STEPS;
            sPressCount--;
        }
    }
    sPrevEngineKeys = keys;
    return keys;
}

void NativeUi_NotePadKeys(uint16_t padKeys)
{
    static uint16_t sPrevPad;

    if ((padKeys & ~sPrevPad) != 0)
        UiW_SetPromptDevice(UI_DEVICE_PAD);
    sPrevPad = padKeys;
}

/* ---------------------------------------------------------------- input */

static void WindowToCanvas(int wx, int wy, int *cx, int *cy)
{
    int ww = 1, wh = 1, dw = 1, dh = 1;

    SDL_GetWindowSize(sWindow, &ww, &wh);
    SDL_GL_GetDrawableSize(sWindow, &dw, &dh);
    if (ww > 0 && wh > 0)
    {
        wx = wx * dw / ww;
        wy = wy * dh / wh;
    }
    UiDraw_WindowToCanvas(wx, wy, cx, cy);
}

bool NativeUi_HandleEvent(const SDL_Event *event)
{
    if (!NativeUi_Enabled())
        return false;
    /* the PC menu (F10) is over everything and keeps its own input */
    if (Platform_NativeUiPcMenuOpen()
     && (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL))
        return false;
    switch (event->type)
    {
    case SDL_KEYDOWN:
        UiW_SetPromptDevice(UI_DEVICE_KEYBOARD);
        return false;
    case SDL_MOUSEMOTION:
        WindowToCanvas(event->motion.x, event->motion.y, &sMouseX, &sMouseY);
        sMouseInside = true;
        return false;
    case SDL_MOUSEBUTTONDOWN:
    {
        const struct UiHit *hit;

        WindowToCanvas(event->button.x, event->button.y, &sMouseX, &sMouseY);
        hit = UiDraw_HitAt(sMouseX, sMouseY);
        if (sLog)
            SDL_Log("Native UI: click %d at window (%d,%d) canvas (%d,%d): %s %d/%d", event->button.button,
                    event->button.x, event->button.y, sMouseX, sMouseY, hit != NULL ? "region" : "no region",
                    hit != NULL ? hit->id : 0, hit != NULL ? hit->index : 0);
        if (hit == NULL || sActive == NULL || sActive->click == NULL)
            return false;
        return sActive->click(hit, event->button.button);
    }
    case SDL_MOUSEWHEEL:
        if (sActive != NULL && sActive->wheel != NULL)
            return sActive->wheel(event->wheel.y);
        return false;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_LEAVE)
            sMouseInside = false;
        else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            sPressCount = 0; /* no queued press lands in a game the player left */
        return false;
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- drawing */

void UiTestCard_Draw(int t);

void NativeUi_Draw(int outputWidth, int outputHeight)
{
    Uint64 start;

    if (!NativeUi_Enabled() || (sActive == NULL && !sTestCard))
        return;
    start = SDL_GetPerformanceCounter();
    UiDraw_Begin(outputWidth, outputHeight, sSize);
    if (sActive != NULL && sActive->draw != NULL)
        sActive->draw((int)(sFrame - sActiveSince));
    if (sTestCard)
        UiTestCard_Draw((int)sFrame);
    UiDraw_End();
    sLastDrawMs = (float)((SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency());
    sDrawMsSum += sLastDrawMs;
    sDrawSamples++;
    if (sLog && SDL_GetTicks() - sLastLogMs >= 5000)
    {
        int quads, calls, uploads;

        UiDraw_Stats(&quads, &calls, &uploads);
        SDL_Log("Native UI: %s, UI pass %.3f ms CPU (mean of %d), %d quads, %d draw calls, scale %d, canvas %dx%d",
                sActive != NULL ? sActive->name : "test card", sDrawMsSum / sDrawSamples, sDrawSamples, quads, calls,
                UiDraw_View()->scale, UiDraw_View()->canvasWidth, UiDraw_View()->canvasHeight);
        sDrawMsSum = 0;
        sDrawSamples = 0;
        sLastLogMs = SDL_GetTicks();
    }
}

void NativeUi_QuitGame(void)
{
    SDL_Event quit;

    SDL_zero(quit);
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
}

void NativeUi_OpenPcMenu(void)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_F10;
    e.key.keysym.scancode = SDL_SCANCODE_F10;
    e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    e.type = SDL_KEYUP;
    e.key.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

float NativeUi_LastDrawMs(void)
{
    return sLastDrawMs;
}
#endif // ENABLE_NATIVE_UI
