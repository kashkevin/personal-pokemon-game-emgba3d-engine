#ifndef GUARD_DIORAMA_GL_PC_OVERLAY_H
#define GUARD_DIORAMA_GL_PC_OVERLAY_H

#include <stdbool.h>

/*
 * The PC's own interface over the game: the settings menu (F10,
 * src/platform/pc_menu.h) and the frame rate. Drawn as the UI art direction
 * asks (the UI session's ART_DIRECTION.md, version 2): the engine's own
 * bitmap fonts and the original Route Caps face, light panels with stepped
 * corners in the Emerald green family, on a 640x360 logical canvas shown once
 * with a whole-number scale and nearest-neighbour sampling (x3 at 1080p, x4
 * at 1440p, x6 at 4K), so it is sharp at any resolution and display scaling;
 * a window of another shape gets a longer canvas, never a stretched one.
 */
struct DioramaOverlayRow
{
    const char *label;
    const char *value;
    bool selected;
    bool adjustable; /* arrows either side of a selected row's value */
};

struct DioramaOverlayMenu
{
    const char *title;
    const struct DioramaOverlayRow *rows;
    int count;
    int confirmSeconds; /* seconds left to keep a new display setting; -1: none waiting */
    const char *message;   /* one or two lines ('|' splits them) in a box under the rows; NULL: none */
    const char *backLabel; /* what Esc does on this sheet ("Close", "Back"); NULL: Close */
};

struct DioramaOverlayExtras
{
    const char *notice; /* one line in a message box at the bottom (NULL: none) */
    bool photoHints;    /* photo mode's keys along the bottom */
};

/* Draws the PC interface over the finished frame: the menu (NULL: closed), a
 * short readout in the top left corner (NULL: none) and the extras (NULL:
 * none). */
void DioramaPCOverlay_Draw(const struct DioramaOverlayMenu *menu, const char *corner,
                           const struct DioramaOverlayExtras *extras,
                           int outputWidth, int outputHeight);
void DioramaPCOverlay_Shutdown(void);

#endif // GUARD_DIORAMA_GL_PC_OVERLAY_H
