#ifndef GUARD_NATIVE_UI_GFX_H
#define GUARD_NATIVE_UI_GFX_H

/*
 * Pictures for the native UI: the design's own menu icons (embedded from the
 * UI preview) and the game's own art (Pokémon icons and pictures, item icons,
 * trainer pictures), decoded from the game's read-only data on the render
 * thread into atlas pieces. Filters (grayscale, brightness, sepia, saturate)
 * are the CSS ones the design uses, applied when the piece is built.
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_kit.h"

struct UiIconRect
{
    const char *name;
    int x;
    int y;
    int w;
    int h;
};
extern const unsigned char gUiIconsPng[];
extern const unsigned gUiIconsPngSize;
extern const struct UiIconRect gUiIconRects[];
extern const int gUiIconRectCount;

/* CSS-style filters, applied in this order: grayscale, sepia, saturate,
 * brightness (each only when set). A silhouette (the design's silhouette
 * option) paints every visible pixel one colour instead. */
struct UiFilter
{
    float grayscale;   /* 0..1 */
    float sepia;       /* 0..1 */
    float saturate;    /* 0: unset, 1: unchanged */
    float brightness;  /* 0: unset, 1: unchanged */
    UiRGBA silhouette; /* UI_NONE: none */
};

bool UiGfx_Init(void);
void UiGfx_Shutdown(void);
/* decodes a PNG (RGBA, straight alpha); free() the result */
uint8_t *UiPng_Decode(const unsigned char *data, int size, int *w, int *h);

/* W.icon: a pkui icon by name, centred in `size` when size differs (0: native size) */
bool UiGfx_Icon(const char *name, int x, int y, int size);
bool UiGfx_IconSize(const char *name, int *w, int *h);

/* The game's own art. 4bpp tiles in the GBA's 1D sprite layout -> RGBA. */
void UiGfx_Decode4bpp(uint8_t *rgba, int stride, const uint8_t *tiles, const uint16_t *palette, int w, int h);
/* W.monIcon: a party / box icon (32x32, frame 0 or 1) at x, y; filter may be NULL */
bool UiGfx_MonIcon(uint16_t species, uint32_t personality, bool isEgg, int frame, int x, int y,
                   const struct UiFilter *filter);

/* The game's compressed art (Pokémon front pictures, item icons). The game's
 * decompressors share its state: a screen's capture (frame boundary, game
 * parked) prepares a picture, its draw shows it. Draw returns false for a
 * picture that was not prepared. */
bool UiGfx_PrepareMonPic(uint16_t species, uint32_t personality, bool shiny, bool isEgg);
/* the picture with the lowest painted row's middle at footX, footY; frame 0 or 1 */
bool UiGfx_MonPic(uint16_t species, uint32_t personality, bool shiny, bool isEgg, int frame, int footX, int footY,
                  const struct UiFilter *filter);
/* ... enlarged by a whole factor (its pixels kept square), faded by alpha (0..1) */
bool UiGfx_MonPicEx(uint16_t species, uint32_t personality, bool shiny, bool isEgg, int frame, int footX, int footY,
                    int scale, double alpha, const struct UiFilter *filter);
/* the frame of the species' own front animation t frames after it started (0 once it ended) */
int UiGfx_MonPicFrameAt(uint16_t species, uint32_t personality, bool shiny, bool isEgg, int t);
bool UiGfx_PrepareItemIcon(uint16_t item);
/* W.item: the 24x24 icon at x, y */
bool UiGfx_ItemIcon(uint16_t item, int x, int y, const struct UiFilter *filter);
/* the icon enlarged by a whole factor (its pixels kept square), top left at x, y */
bool UiGfx_ItemIconScaled(uint16_t item, int x, int y, int scale);

/* a trainer's front picture (64x64; decompressed at the frame boundary like the above) */
bool UiGfx_PrepareTrainerPic(uint16_t picId);
bool UiGfx_TrainerPic(uint16_t picId, int x, int y, const struct UiFilter *filter);
/* a field (overworld) sprite frame, 16x32 4bpp frames in a row, with any 16-colour
 * palette (a recoloured trainer), enlarged by a whole factor, mirrored for flip */
bool UiGfx_FieldSprite(const void *tiles, int frameCount, const uint16_t *palette, int frame, int x, int y, int scale,
                       bool flip);
/* ... frames of w x h (multiples of 8) */
bool UiGfx_FieldSpriteSized(const void *tiles, int frameCount, const uint16_t *palette, int frame, int w, int h, int x,
                            int y, int scale, bool flip);

/* applies a filter to RGBA pixels in place */
void UiFilter_Apply(uint8_t *rgba, int count, const struct UiFilter *f);
/* appends the filter's key ("" when none) */
void UiFilter_Key(char *buf, int size, const struct UiFilter *f);

#endif // GUARD_NATIVE_UI_GFX_H
