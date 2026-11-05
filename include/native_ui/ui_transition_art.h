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

/* the layers kept on the GPU (each its own texture, uploaded when its pixels change): BG0 and the three circles */
#define UI_ART_LAYER_COUNT 4

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
};

/* The GPU side is ready (made on the first call; FALSE for good when the GPU refused the shader or the art is switched
 * off with PKMN_TRANSITION_ART=0): only then may the GBA picture leave the art out. */
bool UiArt_Ready(void);
/* The GBA's 512 colours this frame (BGR555: the backgrounds' 256, then the sprites'), before the frame's layers. */
void UiArt_SetPalette(const uint16_t *colours);
/* Draws a layer now (into the bound framebuffer, before the UI's quads). The GBA picture is at (px, py) window pixels
 * with k window pixels a GBA pixel; carry: the 3D field fills the window (backgrounds carried out to its edges, sprites
 * wherever they are), else only the picture's rectangle. id (0 .. UI_ART_LAYER_COUNT - 1) keeps the layer's texture. */
bool UiArt_Draw(int id, const struct UiArtLayer *layer, int outputWidth, int outputHeight, int px, int py, int k,
                bool carry);
void UiArt_Shutdown(void);

#endif // GUARD_NATIVE_UI_TRANSITION_ART_H
