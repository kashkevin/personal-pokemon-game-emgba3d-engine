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
/* the pictures as the GBA's affine sprites show them: scaled about the 64x64 box's
 * centre by 256 / affine (an OAM matrix value), clipped to the box, every GBA pixel
 * zoom x zoom logical pixels; the box's centre at cx, cy (prepared as above) */
bool UiGfx_MonPicAffine(uint16_t species, uint32_t personality, bool shiny, int frame, int cx, int cy, int affine, int zoom,
                        const struct UiFilter *filter);
bool UiGfx_TrainerPicAffine(uint16_t picId, int cx, int cy, int affine, int zoom, const struct UiFilter *filter);
/* a field (overworld) sprite frame, 16x32 4bpp frames in a row, with any 16-colour
 * palette (a recoloured trainer), enlarged by a whole factor, mirrored for flip */
bool UiGfx_FieldSprite(const void *tiles, int frameCount, const uint16_t *palette, int frame, int x, int y, int scale,
                       bool flip);
/* ... frames of w x h (multiples of 8) */
bool UiGfx_FieldSpriteSized(const void *tiles, int frameCount, const uint16_t *palette, int frame, int w, int h, int x,
                            int y, int scale, bool flip);
/* a trainer's field sprite (any body: the default trainer or a character; ui_engine.h
 * struct NativeFieldSprite) in a pose (0 walk, 1 run), direction (0 down, 1 left,
 * 2 up, 3 right) and step (0-3). x, y is the top left of a 16x32 frame's place,
 * as for UiGfx_FieldSprite: a wider frame is centred on it, standing on its foot. */
struct NativeFieldSprite;
bool UiGfx_NativeFieldSprite(const struct NativeFieldSprite *sprite, int pose, int dir, int step, int x, int y,
                             int scale);

/* applies a filter to RGBA pixels in place */
void UiFilter_Apply(uint8_t *rgba, int count, const struct UiFilter *f);
/* appends the filter's key ("" when none) */
void UiFilter_Key(char *buf, int size, const struct UiFilter *f);

#endif // GUARD_NATIVE_UI_GFX_H
