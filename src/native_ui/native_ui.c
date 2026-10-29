#ifdef ENABLE_NATIVE_UI
/*
 * The native PC UI's controller (see native_ui/native_ui.h): the screen
 * registry, the frame-boundary capture, the picture policy, mouse input and
 * the button-press queue, the development switches. Project-owned.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
/* On Windows SDL_opengl.h includes windows.h, whose USER API (winuser.h) declares names the game's headers use for
   their own functions (ScrollWindow, window.h): it is left out here, where nothing needs it. wingdi.h's TRANSPARENT
   is gba/defines.h's to define. */
#if defined(_WIN32) && !defined(NOUSER)
#define NOUSER
#endif
#include <SDL2/SDL_opengl.h>
#undef TRANSPARENT

#include "global.h"
#include "battle_main.h"
#include "battle_transition.h"
#include "gpu_regs.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "window.h"
#include "native_ui/native_ui.h"
#include "native_ui/ui_bench.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

/* src/platform/gba_easy_draw.c: GBA background layers and OAM entries left out of the picture */
extern uint8_t gPlatformPictureHideBgMask;
extern uint32_t gPlatformPictureHideObj[4];
extern uint8_t gPlatformPictureWindowAs;

/* The screens in detection order (the first that detects wins). */
static const struct UiScreen *const sScreens[] = {
    &gUiScreen_Title,
    &gUiScreen_MainMenu,
    &gUiScreen_Options,
    &gUiScreen_Pause,
    &gUiScreen_Party,
    &gUiScreen_MoveSwitch,    /* before the summary: the relearner's move to forget is its replace step */
    &gUiScreen_Summary,
    &gUiScreen_Bag,
    &gUiScreen_Pokedex,
    &gUiScreen_Storage,
    &gUiScreen_PcItems,       /* the player's PC's ITEM STORAGE lists, over the field */
    &gUiScreen_PcMailbox,     /* the player's PC's MAILBOX, over the field */
    &gUiScreen_PcDecor,       /* the decoration PC's menus (room, secret base, the trader), over the field */
    &gUiScreen_StorageMenus,  /* the PC's first menus, over the field */
    &gUiScreen_DexNewEntry,   /* before the battle: a catch's new entry page runs inside it */
    &gUiScreen_Battle,
    &gUiScreen_Customize,
    &gUiScreen_Players,
    &gUiScreen_TrainerCard,
    &gUiScreen_Mail,         /* a Mail's letter: the Bag's CHECK, a Pokemon's READ, the PC's MAILBOX */
    &gUiScreen_EasyChat,     /* Easy Chat: a Mail's words, the profile, an interview, the Bard, the quiz */
    &gUiScreen_HallOfFame,   /* the Hall of Fame after the Champion, and its PC's records */
    &gUiScreen_Credits,      /* the credits' pages over the game's ride */
    &gUiScreen_TradeSequence, /* a trade's lines over its sequence (an in-game trade's, a link partner's) */
    &gUiScreen_HillBoard,    /* Trainer Hill's time board */
    &gUiScreen_PokeblockCase, /* the Pokéblock case */
    &gUiScreen_UsePokeblock, /* the Pokéblock's condition menu: who gets it, what it enhanced */
    &gUiScreen_PokeblockFeed, /* a Pokémon eating a Pokéblock: the scene's line */
    &gUiScreen_Contest,      /* a contest's appeals: the panels, the announcer, the moves over the stage */
    &gUiScreen_ContestResults, /* a contest's results: the bars, the placings, the winner's banner */
    &gUiScreen_BerryBlender, /* the Berry Blender: the names, the lines, the ranking and results round the blender */
    &gUiScreen_ContestPainting, /* a contest painting: its caption as a museum label */
    &gUiScreen_BerryTag,     /* a berry's tag (the Bag's CHECK TAG) */
    &gUiScreen_Diploma,      /* the POKéDEX diploma */
    &gUiScreen_Factory,      /* the Battle Factory's rentals and swaps (the Slateport tent's too) */
    &gUiScreen_DomeTree,     /* the Battle Dome's tourney tree */
    &gUiScreen_Shop,
    &gUiScreen_Naming,
    &gUiScreen_Starters,
    &gUiScreen_Evolution,
    &gUiScreen_EggHatch,     /* an Egg hatching */
    &gUiScreen_WallClock,    /* the wall clock: set at the start, or looked at */
    &gUiScreen_GameCorner,
    &gUiScreen_SlotMachine,  /* the Game Corner's slot machines (the reels' own screen) */
    &gUiScreen_Roulette,     /* the Game Corner's roulette tables (the wheel and the board) */
    &gUiScreen_RegionMap,    /* the Fly map, a Pokemon Center's wall map, the PokéNav's HOENN MAP */
    &gUiScreen_Pokenav,      /* the PokéNav's other pages (after the region map: it takes the HOENN MAP) */
    &gUiScreen_Transition, /* a battle transition on the field */
    &gUiScreen_FieldMove,  /* a field move's cut-in (an HM's Pokemon on its banner) */
    &gUiScreen_MatchCall,  /* a PokeNav call over the field */
    &gUiScreen_FieldText, /* the field's messages the screens above do not draw */
    &gUiScreen_MapName,   /* the location pop-up, over the field */
    &gUiScreen_Notice,    /* last: only over the calm field */
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
    UiBench_Init(sEnabled);
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

bool NativeUi_ReplacesBattleTextBox(void)
{
    extern bool UiBattle_Supported(void);

    return NativeUi_Enabled() && ScreenAllowed("battle") && UiBattle_Supported();
}

int NativeUi_BattlePanelTop(int outputWidth, int outputHeight)
{
    struct UiView view;

    UiView_Compute(&view, outputWidth, outputHeight, sSize);
    return view.originY + (view.canvasHeight - (UI_LOGICAL_HEIGHT - NATIVE_UI_BATTLE_PANEL_Y)) * view.scale;
}

bool NativeUi_LogEnabled(void)
{
    return sLog;
}

void NativeUi_Logf(const char *fmt, ...)
{
    char line[256];
    va_list ap;

    if (!sLog)
        return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    SDL_Log("Native UI: %s", line);
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

    /* mode 2: HARDWARE_FADE (palette.c), BLDY over the picture (the battle's
     * return from a menu, weather's fades): the screen's when it takes every
     * background and the sprites, else a layer effect (a move's) */
    if (gPaletteFade.mode == 2)
    {
        u32 cnt = GetGpuReg(REG_OFFSET_BLDCNT), level = GetGpuReg(REG_OFFSET_BLDY) & 0x1F;
        u32 effect = cnt & BLDCNT_EFFECT_EFF_MASK;

        if ((cnt & (BLDCNT_TGT1_BG_ALL | BLDCNT_TGT1_OBJ)) != (BLDCNT_TGT1_BG_ALL | BLDCNT_TGT1_OBJ)
         || (effect != BLDCNT_EFFECT_DARKEN && effect != BLDCNT_EFFECT_LIGHTEN) || level == 0
         || (!gPaletteFade.active && level < 16))
            return UI_NONE;
        c = effect == BLDCNT_EFFECT_DARKEN ? 0 : 255;
        return UI_RGBA(c, c, c, level >= 16 ? 255 : (int)(level * 255 / 16));
    }

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

/*
 * PKMN_NATIVE_UI_AUDIT=1 (a test switch): what of the game's own GBA interface
 * shows with no native screen over it, in the log when it changes ("Native UI
 * audit: ..."): its text and menu windows on screen (id, size, place in
 * tiles; the map name's pop-up marked), a battle transition playing
 * (B_TRANSITION_*), the game's main callback when it is neither the field's
 * nor a battle's (a screen of its own), and the field script last started.
 * Addresses are the program's own (nm names them). For the UI audit's walks
 * through the game (the owner's request of 2026-10-26: every place the old
 * look stays).
 */
static void AuditUncovered(void)
{
    static int sOn = -1;
    static char sLast[512];
    char line[512];
    size_t used = 0;
    unsigned i;

    if (sOn < 0)
    {
        const char *env = SDL_getenv("PKMN_NATIVE_UI_AUDIT");

        sOn = env != NULL && strcmp(env, "1") == 0;
    }
    if (!sOn)
        return;
    line[0] = '\0';
    if (sActive == NULL)
    {
        u8 popup = GetMapNamePopUpWindowId();
        u8 transition = BattleTransition_NativeUiPlaying();

        for (i = 0; i < WINDOWS_MAX && used + 48 < sizeof(line); i++)
            if (gWindows[i].window.bg != 0xFF && Window_NativeUiShown(i))
                used += (size_t)snprintf(line + used, sizeof(line) - used, " %s %u: %ux%u at %u,%u",
                                         i == popup ? "popup" : "window", i, gWindows[i].window.width,
                                         gWindows[i].window.height, gWindows[i].window.tilemapLeft,
                                         gWindows[i].window.tilemapTop);
        if (transition < B_TRANSITION_COUNT && used + 32 < sizeof(line))
            used += (size_t)snprintf(line + used, sizeof(line) - used, " transition %u", transition);
        if (gMain.callback2 != CB2_Overworld && gMain.callback2 != CB2_OverworldBasic
         && gMain.callback2 != BattleMainCB2 && used + 32 < sizeof(line))
            used += (size_t)snprintf(line + used, sizeof(line) - used, " screen %p", (void *)gMain.callback2);
        if (used != 0 && ScriptContext_IsEnabled() && used + 32 < sizeof(line))
            snprintf(line + used, sizeof(line) - used, " script %p", (const void *)ScriptContext_NativeUiStartedScript());
    }
    if (strcmp(line, sLast) == 0)
        return;
    if (line[0] != '\0')
        SDL_Log("Native UI audit: frame %u, the game's own interface with no native screen:%s", sFrame, line);
    else
        SDL_Log("Native UI audit: frame %u, none (%s)", sFrame, sActive != NULL ? sActive->name : "nothing shown");
    SDL_strlcpy(sLast, line, sizeof(sLast));
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
    AuditUncovered();
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
    case UI_PICTURE_HIDE_BG012:
        sPictureFlags = NATIVE_UI_HIDE_UI_LAYER;
        gPlatformPictureHideBgMask = (1 << 0) | (1 << 1) | (1 << 2);
        break;
    case UI_PICTURE_HIDE_BG02:
        sPictureFlags = NATIVE_UI_HIDE_UI_LAYER;
        gPlatformPictureHideBgMask = (1 << 0) | (1 << 2);
        break;
    default:
        sPictureFlags = 0;
        gPlatformPictureHideBgMask = 0;
        break;
    }
    memset(gPlatformPictureHideObj, 0, 4 * sizeof(uint32_t));
    if (sActive != NULL && sActive->hideObjects != NULL && picture != UI_PICTURE_HIDE_ALL)
        sActive->hideObjects(gPlatformPictureHideObj);
    gPlatformPictureWindowAs = sActive != NULL && sActive->windows != NULL && picture != UI_PICTURE_HIDE_ALL
                             ? (uint8_t)sActive->windows() : UI_WINDOWS_GAME;
}

const char *NativeUi_ScreenNow(void)
{
    if (NativeUi_Enabled())
        return sActive != NULL ? sActive->name : NULL;
    /* the UI is off (the benchmark's other run): what it would show, by its detection alone */
    for (size_t i = 0; i < SDL_arraysize(sScreens); i++)
    {
        if (sScreens[i] != NULL && sScreens[i]->detect())
            return sScreens[i]->name;
    }
    return NULL;
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
    if (sLog && count > 0)
        SDL_Log("Native UI: press %04x x%d queued", key, count);
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
    uint16_t real, newKeys;

    keys = UiBench_Keys(keys); /* PKMN_NATIVE_UI_BENCH: its script presses buttons, UI on or off */
    real = keys;
    newKeys = keys & ~sPrevKeys;
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

void NativeUi_NotePadDetails(bool down)
{
    static bool sPrevDown;

    if (down && !sPrevDown && NativeUi_Enabled() && !Platform_NativeUiPcMenuOpen())
    {
        UiW_SetPromptDevice(UI_DEVICE_PAD);
        if (sActive != NULL && sActive->pcKey != NULL)
            sActive->pcKey(UI_PCKEY_DETAILS);
    }
    sPrevDown = down;
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

bool NativeUi_MouseHeld(void)
{
    if (!NativeUi_Enabled() || Platform_NativeUiPcMenuOpen() || !sMouseInside)
        return false;
    return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) != 0;
}

const struct UiHit *NativeUi_MouseHover(void)
{
    if (!sMouseInside || sMouseX < 0)
        return NULL;
    return UiDraw_HitAt(sMouseX, sMouseY);
}

bool NativeUi_HandleEvent(const SDL_Event *event)
{
    if (!NativeUi_Enabled())
        return false;
    /* the PC menu (F10) is over everything and keeps its own input */
    if (Platform_NativeUiPcMenuOpen()
     && (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL))
        return false;
    /* a screen that takes typing: letters type instead of pressing buttons */
    if (sActive != NULL && sActive->textMode != NULL && !Platform_NativeUiPcMenuOpen() && sActive->textMode())
    {
        if (event->type == SDL_TEXTINPUT)
        {
            if (sActive->text != NULL)
                sActive->text(event->text.text);
            return true;
        }
        if (event->type == SDL_KEYDOWN && (event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0)
        {
            SDL_Keycode sym = event->key.keysym.sym;

            UiW_SetPromptDevice(UI_DEVICE_KEYBOARD);
            switch (sym)
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (!event->key.repeat)
                    NativeUi_QueuePress(A_BUTTON);
                return true;
            case SDLK_BACKSPACE:
            case SDLK_ESCAPE:
                NativeUi_QueuePress(B_BUTTON);
                return true;
            case SDLK_PAGEDOWN:
                if (!event->key.repeat)
                    NativeUi_QueuePress(SELECT_BUTTON);
                return true;
            case SDLK_PAGEUP:
                if (!event->key.repeat)
                    NativeUi_QueuePresses(SELECT_BUTTON, 2);
                return true;
            default:
                /* a key that types: its character comes as SDL_TEXTINPUT */
                if (sym >= SDLK_SPACE && sym < SDLK_DELETE)
                    return true;
                break;
            }
        }
    }
    switch (event->type)
    {
    case SDL_KEYDOWN:
        UiW_SetPromptDevice(UI_DEVICE_KEYBOARD);
        /* Tab: the screen's details view, where it has one */
        if (event->key.keysym.sym == SDLK_TAB && !event->key.repeat && sActive != NULL && sActive->pcKey != NULL)
            return sActive->pcKey(UI_PCKEY_DETAILS);
        return false;
    case SDL_MOUSEMOTION:
        WindowToCanvas(event->motion.x, event->motion.y, &sMouseX, &sMouseY);
        sMouseInside = true;
        return false;
    case SDL_MOUSEBUTTONDOWN:
    {
        const struct UiHit *hit;

        WindowToCanvas(event->button.x, event->button.y, &sMouseX, &sMouseY);
        sMouseInside = true;
        hit = UiDraw_HitAt(sMouseX, sMouseY);
        if (sLog)
            SDL_Log("Native UI: click %d at window (%d,%d) canvas (%d,%d): %s %d/%d", event->button.button,
                    event->button.x, event->button.y, sMouseX, sMouseY, hit != NULL ? "region" : "no region",
                    hit != NULL ? hit->id : 0, hit != NULL ? hit->index : 0);
        if (sActive == NULL || sActive->click == NULL)
            return false;
        if (hit == NULL)
        {
            /* a right click anywhere is the screen's "back" (its B) */
            static const struct UiHit sNoHit = {UI_HIT_NONE, 0, 0, 0, 0, 0};

            if (event->button.button != 3)
                return false;
            hit = &sNoHit;
        }
        if (sActive->click(hit, event->button.button))
            return true;
        /* a click on a prompt presses its button (the design's prompts are buttons) */
        if (hit->id == UI_HIT_HINT && event->button.button == 1 && !NativeUi_PressesPending())
        {
            static const uint16_t sHintKeys[UI_ACT_COUNT] = {
                [UI_ACT_CONFIRM] = A_BUTTON, [UI_ACT_CANCEL] = B_BUTTON, [UI_ACT_START] = START_BUTTON,
                [UI_ACT_SELECT] = SELECT_BUTTON, [UI_ACT_L] = L_BUTTON, [UI_ACT_R] = R_BUTTON,
            };

            if (hit->index >= 0 && hit->index < UI_ACT_COUNT && sHintKeys[hit->index] != 0)
            {
                NativeUi_QueuePress(sHintKeys[hit->index]);
                return true;
            }
        }
        return false;
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

/* GL timer queries for the UI pass's GPU time (benchmark and log): a small ring, read when ready */
typedef void (APIENTRY *PfnGenQueries)(GLsizei, GLuint *);
typedef void (APIENTRY *PfnBeginQuery)(GLenum, GLuint);
typedef void (APIENTRY *PfnEndQuery)(GLenum);
typedef void (APIENTRY *PfnGetQueryObjectiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PfnGetQueryObjectui64v)(GLuint, GLenum, GLuint64 *);
#define GPU_QUERIES 4
static struct
{
    bool tried, ok;
    PfnGenQueries gen;
    PfnBeginQuery begin;
    PfnEndQuery end;
    PfnGetQueryObjectiv getiv;
    PfnGetQueryObjectui64v getui64;
    GLuint ids[GPU_QUERIES];
    bool pending[GPU_QUERIES];
    int next;
} sGpu;
static double sGpuMsSum;
static int sGpuSamples;

static bool GpuTimerReady(void)
{
    if (!sGpu.tried)
    {
        sGpu.tried = true;
        sGpu.gen = (PfnGenQueries)SDL_GL_GetProcAddress("glGenQueries");
        sGpu.begin = (PfnBeginQuery)SDL_GL_GetProcAddress("glBeginQuery");
        sGpu.end = (PfnEndQuery)SDL_GL_GetProcAddress("glEndQuery");
        sGpu.getiv = (PfnGetQueryObjectiv)SDL_GL_GetProcAddress("glGetQueryObjectiv");
        sGpu.getui64 = (PfnGetQueryObjectui64v)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
        sGpu.ok = sGpu.gen != NULL && sGpu.begin != NULL && sGpu.end != NULL && sGpu.getiv != NULL && sGpu.getui64 != NULL;
        if (sGpu.ok)
            sGpu.gen(GPU_QUERIES, sGpu.ids);
    }
    return sGpu.ok;
}

/* collects finished queries (never waits for one) */
static void GpuTimerCollect(void)
{
    for (int i = 0; i < GPU_QUERIES; i++)
    {
        GLint available = 0;
        GLuint64 ns = 0;

        if (!sGpu.pending[i])
            continue;
        sGpu.getiv(sGpu.ids[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            continue;
        sGpu.getui64(sGpu.ids[i], GL_QUERY_RESULT, &ns);
        sGpu.pending[i] = false;
        sGpuMsSum += ns / 1.0e6;
        sGpuSamples++;
        UiBench_Gpu((float)(ns / 1.0e6));
    }
}

void NativeUi_Draw(int outputWidth, int outputHeight)
{
    Uint64 start;
    bool timed;

    UiBench_Frame(sLastDrawMs);
    sLastDrawMs = 0;
    if (!NativeUi_Enabled() || (sActive == NULL && !sTestCard))
        return;
    timed = (sLog || UiBench_Active()) && GpuTimerReady();
    if (timed)
    {
        GpuTimerCollect();
        timed = !sGpu.pending[sGpu.next];
        if (timed)
            sGpu.begin(GL_TIME_ELAPSED, sGpu.ids[sGpu.next]);
    }
    start = SDL_GetPerformanceCounter();
    UiDraw_Begin(outputWidth, outputHeight, sSize);
    if (sActive != NULL && sActive->draw != NULL)
        sActive->draw((int)(sFrame - sActiveSince));
    if (sTestCard)
        UiTestCard_Draw((int)sFrame);
    UiDraw_End();
    if (timed)
    {
        sGpu.end(GL_TIME_ELAPSED);
        sGpu.pending[sGpu.next] = true;
        sGpu.next = (sGpu.next + 1) % GPU_QUERIES;
    }
    sLastDrawMs = (float)((SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency());
    sDrawMsSum += sLastDrawMs;
    sDrawSamples++;
    if (sLog && SDL_GetTicks() - sLastLogMs >= 5000)
    {
        int quads, calls, uploads;

        UiDraw_Stats(&quads, &calls, &uploads);
        SDL_Log("Native UI: %s, UI pass %.3f ms CPU (mean of %d), %.3f ms GPU (%d), %d quads, %d draw calls, scale %d, canvas %dx%d",
                sActive != NULL ? sActive->name : "test card", sDrawMsSum / sDrawSamples, sDrawSamples,
                sGpuSamples > 0 ? sGpuMsSum / sGpuSamples : -1.0, sGpuSamples, quads, calls,
                UiDraw_View()->scale, UiDraw_View()->canvasWidth, UiDraw_View()->canvasHeight);
        sDrawMsSum = 0;
        sDrawSamples = 0;
        sGpuMsSum = 0;
        sGpuSamples = 0;
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
