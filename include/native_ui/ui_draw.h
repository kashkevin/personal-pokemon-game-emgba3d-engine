#ifndef GUARD_NATIVE_UI_DRAW_H
#define GUARD_NATIVE_UI_DRAW_H

/*
 * The native UI's drawing context: the design's 640x360 logical canvas shown at
 * the largest whole-number scale (x2 at 720p, x3 at 1080p, x4 at 1440p, x6 at
 * 4K; "Small" one step lower, never below x2), growing on the long axis for
 * other window shapes. Everything is a quad on the logical grid with nearest
 * sampling: solid fills, atlas pieces (shapes rasterised once by ui_kit.h and
 * uploaded once), engine-font glyphs and procedural pattern fills. The frame's
 * quads go to the GPU in one buffer and a few draw calls (one per clip).
 * Coordinates are content coordinates: (0, 0) is the top left of the centred
 * 640x360 area; UiG_Bleed gives the whole canvas.
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_kit.h"

#define UI_LOGICAL_WIDTH 640
#define UI_LOGICAL_HEIGHT 360
/* The battle HUD's bottom panel - the message plate, the commands, the moves - begins at this row of the 360-row
 * design, bottom-anchored (screens/ui_battle.c; NativeUi_BattlePanelTop). */
#define NATIVE_UI_BATTLE_PANEL_Y 284

enum UiSize
{
    UI_SIZE_LARGE,
    UI_SIZE_SMALL,
};

struct UiView
{
    int outputWidth;   /* the window's drawable pixels */
    int outputHeight;
    int scale;         /* whole-number factor */
    int canvasWidth;   /* logical canvas (>= 640x360 at 16:9) */
    int canvasHeight;
    int originX;       /* window pixel of the canvas's top left */
    int originY;
    int contentX;      /* canvas pixel of the 640x360 area's top left */
    int contentY;
};
void UiView_Compute(struct UiView *view, int outputWidth, int outputHeight, enum UiSize size);

/* An atlas piece: a rasterised bitmap living in one atlas layer. */
struct UiPiece
{
    uint16_t layer;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
};

/* Builds a piece's pixels on a cache miss (bm is w x h, cleared). */
typedef void (*UiBuildFn)(struct UiBitmap *bm, const void *ctx);

/* GL lifetime (render thread, with the game window's context current). */
bool UiDraw_Init(void);
void UiDraw_Shutdown(void);
/* A frame: begin (sets the view and the content origin), draw, end (to GL). */
void UiDraw_Begin(int outputWidth, int outputHeight, enum UiSize size);
void UiDraw_End(void);
const struct UiView *UiDraw_View(void);
/* Quads emitted and atlas uploads made in the last frame (for the timing log). */
void UiDraw_Stats(int *quads, int *drawCalls, int *uploads);

/* ------------------------------------------------------------ state (like a 2D canvas) */
void UiG_Save(void);
void UiG_Restore(void);
void UiG_Translate(int x, int y);
/* intersect the clip with a rectangle (current coordinates) */
void UiG_Clip(int x, int y, int w, int h);
/* the global alpha (0..1), restored by UiG_Restore */
void UiG_Alpha(double alpha);
double UiG_GetAlpha(void);
/* the whole canvas in current coordinates */
void UiG_Bleed(int *x0, int *y0, int *w, int *h);

/* ------------------------------------------------------------ drawing */
void UiG_Rect(int x, int y, int w, int h, UiRGBA c);
void UiG_Put(int x, int y, UiRGBA c);
void UiG_HLine(int x, int y, int w, UiRGBA c);
void UiG_VLine(int x, int y, int h, UiRGBA c);
/* a piece by key: built by fn on the first use, then drawn from the atlas */
const struct UiPiece *UiG_Cached(const char *key, int w, int h, UiBuildFn fn, const void *ctx);
void UiG_Blit(const struct UiPiece *piece, int x, int y);
/* part of a piece, optionally enlarged (whole factor), mirrored, faded */
void UiG_BlitPart(const struct UiPiece *piece, int sx, int sy, int sw, int sh, int dx, int dy,
                  int scale, bool flip, double alpha);
void UiG_Mask(const struct UiMask *m, int x, int y, UiRGBA c);
void UiG_Paint(const struct UiMask *m, int x, int y, UiRGBA body, const struct UiPaint *o);
/* PK G.panel: a rounded mask painted with a ramp. corners: tl, tr, bl, br */
struct UiPanelOpt
{
    bool noInk;
    bool noHi;
    bool noLo;
    int hiW;
    int loW;
    const struct UiPattern *pattern;
    UiRGBA patternColor;
    const struct UiMask *shape; /* a ready mask instead of the rounded one */
};
const struct UiMask *UiG_Panel(int x, int y, int w, int h, const struct UiRamp *r, int corner,
                               const struct UiPanelOpt *o);
const struct UiMask *UiG_Panel4(int x, int y, int w, int h, const struct UiRamp *r,
                                struct UiCorner tl, struct UiCorner tr, struct UiCorner bl,
                                struct UiCorner br, const struct UiPanelOpt *o);
/* a repeating pattern over a rectangle; (px, py) moves the pattern (scrolling) */
void UiG_FillPattern(const struct UiPattern *p, int x, int y, int w, int h, UiRGBA c, int px, int py);
/* one engine-font glyph cell (ui_text.c): w x h of the 16x16 cell at the font's code */
void UiG_GlyphQuad(int x, int y, int font, int code, int w, int h, UiRGBA fg, UiRGBA sh);
/* a full-canvas colour veil (screen fades) */
void UiG_Veil(UiRGBA c);
/* The quads drawn since a mark (UiG_Mark) squeezed horizontally towards x = cx
 * (current coordinates) by k (0 < k <= 1), each edge on a whole logical pixel:
 * pictures and glyphs lose columns as when a canvas is drawn narrower (the
 * design's card flip). Mouse regions and clips are not moved. */
int UiG_Mark(void);
void UiG_SquashX(int mark, int cx, double k);

/* ------------------------------------------------------------ mouse regions */
struct UiHit
{
    int id;
    int index;
    int x;      /* canvas coordinates */
    int y;
    int w;
    int h;
};
void UiG_Hit(int id, int index, int x, int y, int w, int h);
/* the top region under a canvas point in the last finished frame (NULL: none) */
const struct UiHit *UiDraw_HitAt(int canvasX, int canvasY);
/* window pixel -> canvas coordinates of the last frame's view */
void UiDraw_WindowToCanvas(int px, int py, int *cx, int *cy);
/* canvas -> current content coordinates */
void UiDraw_CanvasToContent(int cx, int cy, int *x, int *y);

#endif // GUARD_NATIVE_UI_DRAW_H
