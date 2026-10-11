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
 * brightness (each only when set). */
struct UiFilter
{
    float grayscale;   /* 0..1 */
    float sepia;       /* 0..1 */
    float saturate;    /* 0: unset, 1: unchanged */
    float brightness;  /* 0: unset, 1: unchanged */
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

/* applies a filter to RGBA pixels in place */
void UiFilter_Apply(uint8_t *rgba, int count, const struct UiFilter *f);
/* appends the filter's key ("" when none) */
void UiFilter_Key(char *buf, int size, const struct UiFilter *f);

#endif // GUARD_NATIVE_UI_GFX_H
