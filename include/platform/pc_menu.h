#ifndef GUARD_PLATFORM_PC_MENU_H
#define GUARD_PLATFORM_PC_MENU_H

/*
 * The PC settings menu (F10): display and graphics settings while playing,
 * drawn by the 3D renderer at the window's own resolution with a TrueType font
 * (src/diorama/gl/gl_pc_overlay.c). This file is the menu's model only -
 * rows, values, navigation, the confirmation after a display change - without
 * SDL or OpenGL, so tools/pc_port/tests can check it. src/platform/sdl2.c owns
 * the keys and applies what changed. While the menu is open the game receives
 * no buttons; it keeps running (multiplayer connections never pause).
 *
 * The Controls page (added 2026-10-15) rebinds the keyboard (include/platform/keybinds.h). Its
 * navigation never follows the bindings - arrows, Enter/Z, Esc/X/Backspace and F10 always work -
 * so the menu stays usable whatever the player binds.
 */
#include <stdbool.h>

#include "platform/display_settings.h"
#include "platform/keybinds.h"

enum PcMenuItem
{
    PC_MENU_RESOLUTION,
    PC_MENU_DISPLAY_MODE,
    PC_MENU_RENDER_SCALE, /* the 3D build */
    PC_MENU_VSYNC,
    PC_MENU_FPS_CAP,
    PC_MENU_QUALITY,
    PC_MENU_SHADOWS,
    PC_MENU_LIGHTING,
    PC_MENU_EFFECTS,
    PC_MENU_DETAILS,
    PC_MENU_ANTIALIASING,
    PC_MENU_MEMORY_BUDGET, /* the 3D build */
    PC_MENU_PICTURE_SIZE,
    PC_MENU_FRAME_RATE,
    PC_MENU_CAMERA,
    PC_MENU_TREES,
    PC_MENU_CONTROLS, /* opens the Controls page */
    PC_MENU_CLOSE,
    PC_MENU_ITEM_COUNT,
};

enum PcMenuInput
{
    PC_MENU_INPUT_UP,
    PC_MENU_INPUT_DOWN,
    PC_MENU_INPUT_LEFT,
    PC_MENU_INPUT_RIGHT,
    PC_MENU_INPUT_ACCEPT,
    PC_MENU_INPUT_BACK,
};

/* What a step changed, for the platform to apply. */
#define PC_MENU_CHANGED_DISPLAY  (1u << 0) /* resolution or display mode */
#define PC_MENU_CHANGED_QUALITY  (1u << 1)
#define PC_MENU_CHANGED_VSYNC    (1u << 2)
#define PC_MENU_CHANGED_PICTURE  (1u << 3)
#define PC_MENU_CHANGED_CAMERA   (1u << 4)
#define PC_MENU_CHANGED_TREES    (1u << 5)
#define PC_MENU_CLOSED           (1u << 6)
#define PC_MENU_CHANGED_FRAME_RATE (1u << 9)
#define PC_MENU_CHANGED_FPS_CAP  (1u << 10)
#define PC_MENU_CHANGED_RENDER_SCALE (1u << 11)
#define PC_MENU_CHANGED_MEMORY_BUDGET (1u << 12)
#define PC_MENU_CHANGED_KEYS     (1u << 13) /* the keybinds changed: apply and store them */
#define PC_MENU_DISPLAY_KEPT     (1u << 7) /* the confirmation was answered: keep */
#define PC_MENU_DISPLAY_REVERTED (1u << 8) /* answered no, or it timed out */

/* After a resolution or display mode change the player has this long to keep
 * it; otherwise the previous one comes back (a display that shows nothing). */
#define PC_MENU_CONFIRM_MS 15000

enum PcMenuPage
{
    PC_MENU_PAGE_MAIN,
    PC_MENU_PAGE_CONTROLS,
};

struct PcMenuState
{
    bool open;
    int cursor;
    /* The Controls page. The rows are the keybind actions, then "Restore defaults" and "Back". */
    int page;
    int controlsCursor;
    bool capturing;            /* waiting for the key to bind to the action under the cursor */
    bool conflicting;          /* the key just pressed is another action's: Enter swaps, Esc cancels */
    int pendingKey;
    int pendingOther;
    char message[128];         /* the page's last prompt or result ('|' splits two lines) */
    struct Keybinds keybinds;  /* a copy being edited; the platform applies it on PC_MENU_CHANGED_KEYS */
    const struct KeybindNames *keyNames;
    struct DisplaySettings settings;
    bool vsync;
    bool pictureFill;   /* the 240x160 picture as large as fits (otherwise 8/9 of the height) */
    bool showFrameRate; /* frames a second in a corner of the screen */
    bool cameraDiorama;
    bool trees3ds;
    bool hasCamera;     /* the 3D build: render scale, world details, camera and trees rows */
    bool confirming;
    unsigned int confirmStartMs;
    struct DisplaySettings previous;
    /* Shown with the memory budget (the platform keeps them current): the
     * budget in effect and what the game uses now, in MB (0: unknown). */
    unsigned int memoryEffectiveMB;
    unsigned int memoryInUseMB;
};

struct PcMenuRow
{
    const char *label;
    char value[40];
    bool selected;
    bool adjustable; /* left and right change it */
};

void PcMenu_Open(struct PcMenuState *menu, const struct DisplaySettings *settings, bool vsync,
                 bool pictureFill, bool showFrameRate, bool hasCamera, bool cameraDiorama, bool trees3ds);
/* The bindings to edit and SDL's key names (call after PcMenu_Open). */
void PcMenu_SetKeybinds(struct PcMenuState *menu, const struct Keybinds *keybinds, const struct KeybindNames *names);
/* While true the platform sends the next key press to PcMenu_CaptureKey instead of PcMenu_Input
 * (Esc still goes to PcMenu_Input as BACK, which cancels). */
bool PcMenu_IsCapturing(const struct PcMenuState *menu);
unsigned int PcMenu_CaptureKey(struct PcMenuState *menu, int key);
/* The sheet's title, its prompt line under the rows (NULL: none) and what Esc does there. */
const char *PcMenu_Title(const struct PcMenuState *menu);
const char *PcMenu_Message(const struct PcMenuState *menu);
const char *PcMenu_BackLabel(const struct PcMenuState *menu);
unsigned int PcMenu_Input(struct PcMenuState *menu, enum PcMenuInput input, unsigned int nowMs);
/* The confirmation's countdown; returns PC_MENU_DISPLAY_REVERTED when it ran out. */
unsigned int PcMenu_Tick(struct PcMenuState *menu, unsigned int nowMs);
/* Seconds left to keep a display change, or -1 when none is waiting. */
int PcMenu_ConfirmSecondsLeft(const struct PcMenuState *menu, unsigned int nowMs);
/* The rows to draw (at most PC_MENU_ITEM_COUNT); returns how many. */
int PcMenu_Rows(const struct PcMenuState *menu, struct PcMenuRow *rows);

#endif
