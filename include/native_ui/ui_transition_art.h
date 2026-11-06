#ifndef GUARD_NATIVE_UI_TRANSITION_ART_H
#define GUARD_NATIVE_UI_TRANSITION_ART_H

/*
 * MS-108: a battle transition's own GBA art - a background's map, a sprite - drawn as smooth shapes at the window's
 * resolution (screens/ui_transition.c). The art stays the game's: its pixels as the game loaded them into VRAM, each
 * coloured from the game's palette of the frame (its palette cycling and fades), moved by its scroll line by line (the
 * waves) and blended as the GBA blends it. Only the stairs go: every output pixel weighs the art's pixels round it
 * (each a square blurred by a Gaussian), the palette slot with the most wins, blended with the next over one window
 * pixel, so the steps of a circle, an ellipse or a diagonal stroke become the curve and the line they draw. Strokes
 * one or two pixels wide weigh more (their mean width from the art), so they stay whole lines instead of beads. Drawn
 * by the GPU into the window's framebuffer, under the native UI's own quads of the frame. Project-owned.
 */
#include <stdbool.h>
#include <stdint.h>

/* the layers kept on the GPU (each its own texture, uploaded when its pixels change): BG0 and the three circles, or BG0
 * and the mugshot's four trainer pictures */
#define UI_ART_LAYER_COUNT 5
/* a window edge (UiArtLayer.winLeft, winRight) this far or further is open: nothing past it, over the 3D field too */
#define UI_ART_OPEN 8192

/* MS-108: a piece of the art redrawn as true circles (ui_transition_vectors.c): shapes painted in order, each a colour
 * of the layer's palette where all its circles' conditions hold - inside a circle (r > 0), outside it (r < 0; 0:
 * unused) - in the art's own pixels (a background's map, a sprite's box), measured from the art. Each of the art's
 * edges is an arc of one of the circles. Where the art draws a line along an edge of a shape (the logo's light-blue
 * lines), the circle gives the line's width, the line on the edge's middle in the shape's line colour. */
#define UI_ART_SHAPE_CIRCLES 3

struct UiArtCircle
{
    float x, y, r;
    float line;             /* the width of the line along this edge (art pixels; 0: none) */
};

struct UiArtShape
{
    uint8_t colour;
    uint8_t line;           /* the lines' colour (0: no lines) */
    struct UiArtCircle circles[UI_ART_SHAPE_CIRCLES];
};

struct UiArtPiece
{
    const char *name;
    const struct UiArtShape *shapes;
    int count;
};

enum UiArtPieceId
{
    UI_ART_PIECE_FRONTIER_LOGO,         /* BG0 of B_TRANSITION_FRONTIER_LOGO_WIGGLE, _WAVE (frontier_logo.png, .bin) */
    UI_ART_PIECE_FRONTIER_CENTRE,       /* BG0 of the Frontier circles' transitions (frontier_logo_center.png, .bin) */
    UI_ART_PIECE_FRONTIER_CIRCLE_TOP,   /* the circles' sprites, frontier_logo_circles.png's three frames */
    UI_ART_PIECE_FRONTIER_CIRCLE_LEFT,
    UI_ART_PIECE_FRONTIER_CIRCLE_RIGHT,
    UI_ART_PIECE_COUNT
};

extern const struct UiArtPiece gUiArtPieces[UI_ART_PIECE_COUNT];

/* The piece among the candidates whose shapes, sampled at the art's pixel middles, give the art's own pixels - at least
 * 98 % of its coloured ones (the rest are the art's own anti-aliasing and ties at its edges) - or NULL. A background's
 * art must be in one palette: its number in *palette. *differ: how many pixels differ. */
const struct UiArtPiece *UiArt_MatchPiece(const uint8_t *slots, int w, int h, bool background,
                                          const enum UiArtPieceId *candidates, int count, int *palette, int *differ);
/* The piece's box in its art's pixels (the union of its shapes' discs and lines, a pixel round): x0, y0, x1, y1 */
void UiArt_PieceBox(const struct UiArtPiece *piece, float box[4]);
/* The log's line for a piece found in the game's pixels. */
void UiArt_LogPiece(const struct UiArtPiece *piece, int differ);

enum UiArtBlend
{
    UI_ART_BLEND_NONE,      /* drawn as it is */
    UI_ART_BLEND_ALPHA,     /* eva / 16 of it over evb / 16 of what is under it (the GBA's BLDALPHA) */
    UI_ART_BLEND_LIGHTEN,   /* towards white by y / 16 (BLDY) */
    UI_ART_BLEND_DARKEN,    /* towards black by y / 16 */
};

struct UiArtLayer
{
    const uint8_t *slots;   /* w x h GBA pixels, rows top down: the palette slot of each (a background's
                             * 16 * palette + colour, a sprite's colour; colour 0 of a palette is see-through) */
    int w;                  /* powers of two (a background's map: 256 or 512; a sprite: 8 to 64) */
    int h;
    bool background;        /* a background repeats past its map's edge and is carried out past the picture's edges
                             * as the compositor carries the picture out; a sprite has nothing outside itself */
    int palBase;            /* slot s is the frame's colour palBase + s (a background 0, a sprite 256 + 16 * its
                             * palette) */
    int originX;            /* the layer's (0, 0) on the GBA screen (a sprite's top left) */
    int originY;
    const int16_t *scrollX; /* a background's scroll by GBA line (160 lines), as BGnHOFS / BGnVOFS */
    const int16_t *scrollY;
    enum UiArtBlend blend;
    int eva;                /* 0..16 (more is 16, as on the GBA) */
    int evb;
    int y;
    /* a background shown only inside a window: [winLeft, winRight) on each GBA line (160 lines, GBA pixels; at
     * +-UI_ART_OPEN or further an edge is open, an empty line is UI_ART_OPEN, UI_ART_OPEN). Its edges are smooth at the
     * window's resolution: through the lines' values at their middles a cubic where neighbouring lines' edges slope (a
     * wave), the GBA's own step where they jump, anti-aliased over one window pixel. NULL: everywhere. */
    const int16_t *winLeft;
    const int16_t *winRight;
    bool wrapX;             /* carried out past the picture's left and right edges by its map's repeat (a banner the
                             * same all across) instead of each line's edge pixel */
    /* a sprite drawn as pixel art, not smoothed: each art pixel cell x cell window pixels, the art's top left at
     * (cornerX, cornerY) window pixels, mirrored across with flipX, up and down with flipY (0: smoothed) */
    int cell;
    int cornerX;
    int cornerY;
    bool flipX;
    bool flipY;
    /* the art redrawn as this piece's true circles (UiArt_MatchPiece found it in slots) instead of the smoothed pixels:
     * placed, scrolled line by line and blended as the art; a background's repeat taken nearest the piece; its colours
     * palBase + the shapes' (a background's palBase then 16 * its palette). NULL: the smoothed pixels. */
    const struct UiArtPiece *piece;
};

/* The GPU side is ready (made on the first call; FALSE for good when the GPU refused the shader or the art is switched
 * off with PKMN_TRANSITION_ART=0): only then may the GBA picture leave the art out. */
bool UiArt_Ready(void);
/* The Frontier's vector pieces may be drawn (the art ready, PKMN_TRANSITION_ART_VECTORS not 0). */
bool UiArt_VectorsOn(void);
/* The GBA's 512 colours this frame (BGR555: the backgrounds' 256, then the sprites'), before the frame's layers. */
void UiArt_SetPalette(const uint16_t *colours);
/* Draws a layer now (into the bound framebuffer, before the UI's quads). The GBA picture is at (px, py) window pixels
 * with k window pixels a GBA pixel; carry: the 3D field fills the window (backgrounds carried out to its edges, sprites
 * wherever they are), else only the picture's rectangle. id (0 .. UI_ART_LAYER_COUNT - 1) keeps the layer's texture. */
bool UiArt_Draw(int id, const struct UiArtLayer *layer, int outputWidth, int outputHeight, int px, int py, int k,
                bool carry);
void UiArt_Shutdown(void);

#endif // GUARD_NATIVE_UI_TRANSITION_ART_H
