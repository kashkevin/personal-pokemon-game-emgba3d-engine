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
 */
#include <stdbool.h>

#include "platform/display_settings.h"

enum PcMenuItem
{
    PC_MENU_RESOLUTION,
    PC_MENU_DISPLAY_MODE,
    PC_MENU_VSYNC,
    PC_MENU_QUALITY,
    PC_MENU_SHADOWS,
    PC_MENU_LIGHTING,
    PC_MENU_EFFECTS,
    PC_MENU_ANTIALIASING,
    PC_MENU_PICTURE_SIZE,
    PC_MENU_FRAME_RATE,
    PC_MENU_CAMERA,
    PC_MENU_TREES,
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
#define PC_MENU_DISPLAY_KEPT     (1u << 7) /* the confirmation was answered: keep */
#define PC_MENU_DISPLAY_REVERTED (1u << 8) /* answered no, or it timed out */

/* After a resolution or display mode change the player has this long to keep
 * it; otherwise the previous one comes back (a display that shows nothing). */
#define PC_MENU_CONFIRM_MS 15000

struct PcMenuState
{
    bool open;
    int cursor;
    struct DisplaySettings settings;
    bool vsync;
    bool pictureFill;   /* the 240x160 picture as large as fits (otherwise 8/9 of the height) */
    bool showFrameRate; /* frames a second in a corner of the screen */
    bool camera3ds;
    bool trees3ds;
    bool hasCamera;     /* the 3D build: camera and trees rows */
    bool confirming;
    unsigned int confirmStartMs;
    struct DisplaySettings previous;
};

struct PcMenuRow
{
    const char *label;
    char value[40];
    bool selected;
    bool adjustable; /* left and right change it */
};

void PcMenu_Open(struct PcMenuState *menu, const struct DisplaySettings *settings, bool vsync,
                 bool pictureFill, bool showFrameRate, bool hasCamera, bool camera3ds, bool trees3ds);
unsigned int PcMenu_Input(struct PcMenuState *menu, enum PcMenuInput input, unsigned int nowMs);
/* The confirmation's countdown; returns PC_MENU_DISPLAY_REVERTED when it ran out. */
unsigned int PcMenu_Tick(struct PcMenuState *menu, unsigned int nowMs);
/* Seconds left to keep a display change, or -1 when none is waiting. */
int PcMenu_ConfirmSecondsLeft(const struct PcMenuState *menu, unsigned int nowMs);
/* The rows to draw (at most PC_MENU_ITEM_COUNT); returns how many. */
int PcMenu_Rows(const struct PcMenuState *menu, struct PcMenuRow *rows);

#endif
