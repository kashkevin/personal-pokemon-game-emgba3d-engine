#ifdef ENABLE_NATIVE_UI
/*
 * Pictures for the native UI (see native_ui/ui_gfx.h). Project-owned.
 * PNG decoding: stb_image v2.30 (public domain / MIT, Sean Barrett), vendored
 * in third_party/, PNG only, from memory.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"

static uint8_t *sIcons;
static int sIconsW;
static int sIconsH;

uint8_t *UiPng_Decode(const unsigned char *data, int size, int *w, int *h)
{
    int channels;

    return stbi_load_from_memory(data, size, w, h, &channels, 4);
}

bool UiGfx_Init(void)
{
    if (sIcons == NULL)
        sIcons = UiPng_Decode(gUiIconsPng, (int)gUiIconsPngSize, &sIconsW, &sIconsH);
    return sIcons != NULL;
}

void UiGfx_Shutdown(void)
{
    stbi_image_free(sIcons);
    sIcons = NULL;
}

static const struct UiIconRect *FindIcon(const char *name)
{
    for (int i = 0; i < gUiIconRectCount; i++)
        if (strcmp(gUiIconRects[i].name, name) == 0)
            return &gUiIconRects[i];
    return NULL;
}

bool UiGfx_IconSize(const char *name, int *w, int *h)
{
    const struct UiIconRect *r = FindIcon(name);

    if (r == NULL)
        return false;
    *w = r->w;
    *h = r->h;
    return true;
}

static void BuildIcon(struct UiBitmap *bm, const void *ctx)
{
    const struct UiIconRect *r = ctx;

    for (int y = 0; y < r->h; y++)
        memcpy(&bm->d[y * bm->w * 4], &sIcons[((r->y + y) * sIconsW + r->x) * 4], (size_t)r->w * 4);
}

bool UiGfx_Icon(const char *name, int x, int y, int size)
{
    const struct UiIconRect *r = FindIcon(name);
    char key[96];

    if (r == NULL || sIcons == NULL)
        return false;
    snprintf(key, sizeof(key), "icon:%s", name);
    if (size > 0 && size != r->w)
    {
        x -= (r->w - size) >> 1;
        y -= (r->h - size) >> 1;
    }
    UiG_Blit(UiG_Cached(key, r->w, r->h, BuildIcon, r), x, y);
    return true;
}

/* ---------------------------------------------------------------- the game's own art */

static int C5To8(int v)
{
    return (v << 3) | (v >> 2);
}

/* 4bpp tiles in the GBA's one-dimensional sprite layout, w x h pixels, with a
 * 16-colour BGR555 palette (index 0 clear) -> RGBA */
void UiGfx_Decode4bpp(uint8_t *rgba, int stride, const uint8_t *tiles, const uint16_t *palette, int w, int h)
{
    int tilesWide = w / 8;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int tile = (y / 8) * tilesWide + x / 8;
            uint8_t byte = tiles[tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            int index = (x & 1) ? byte >> 4 : byte & 15;
            uint8_t *p = &rgba[(y * stride + x) * 4];

            if (index == 0)
            {
                p[0] = p[1] = p[2] = p[3] = 0;
            }
            else
            {
                uint16_t c = palette[index];

                p[0] = (uint8_t)C5To8(c & 31);
                p[1] = (uint8_t)C5To8((c >> 5) & 31);
                p[2] = (uint8_t)C5To8((c >> 10) & 31);
                p[3] = 255;
            }
        }
    }
}

struct MonIconCtx
{
    const uint8_t *tiles;
    const uint16_t *palette;
    int frame;
    const struct UiFilter *filter;
};

static void BuildMonIcon(struct UiBitmap *bm, const void *ctx)
{
    const struct MonIconCtx *c = ctx;

    UiGfx_Decode4bpp(bm->d, bm->w, c->tiles + c->frame * 32 * 32 / 2, c->palette, 32, 32);
    UiFilter_Apply(bm->d, 32 * 32, c->filter);
}

bool UiGfx_MonIcon(uint16_t species, uint32_t personality, bool isEgg, int frame, int x, int y,
                   const struct UiFilter *filter)
{
    struct MonIconCtx c;
    char key[128];
    char fkey[48];

    PokemonIcon_NativeUiArt(species, personality, isEgg, &c.tiles, &c.palette);
    if (c.tiles == NULL || c.palette == NULL)
        return false;
    c.frame = frame & 1;
    c.filter = filter;
    UiFilter_Key(fkey, sizeof(fkey), filter);
    snprintf(key, sizeof(key), "mon:%p:%p:%d%s", (const void *)c.tiles, (const void *)c.palette, c.frame, fkey);
    UiG_Blit(UiG_Cached(key, 32, 32, BuildMonIcon, &c), x, y);
    return true;
}

/* ---------------------------------------------------------------- filters (CSS Filter Effects 1) */

static float Clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

void UiFilter_Apply(uint8_t *rgba, int count, const struct UiFilter *f)
{
    if (f == NULL)
        return;
    for (int i = 0; i < count; i++)
    {
        float r = rgba[i * 4] / 255.0f, g = rgba[i * 4 + 1] / 255.0f, b = rgba[i * 4 + 2] / 255.0f;
        float nr, ng, nb;

        if (f->grayscale > 0)
        {
            float a = 1 - Clamp01(f->grayscale);

            nr = (0.2126f + 0.7874f * a) * r + (0.7152f - 0.7152f * a) * g + (0.0722f - 0.0722f * a) * b;
            ng = (0.2126f - 0.2126f * a) * r + (0.7152f + 0.2848f * a) * g + (0.0722f - 0.0722f * a) * b;
            nb = (0.2126f - 0.2126f * a) * r + (0.7152f - 0.7152f * a) * g + (0.0722f + 0.9278f * a) * b;
            r = nr; g = ng; b = nb;
        }
        if (f->sepia > 0)
        {
            float a = 1 - Clamp01(f->sepia);

            nr = (0.393f + 0.607f * a) * r + (0.769f - 0.769f * a) * g + (0.189f - 0.189f * a) * b;
            ng = (0.349f - 0.349f * a) * r + (0.686f + 0.314f * a) * g + (0.168f - 0.168f * a) * b;
            nb = (0.272f - 0.272f * a) * r + (0.534f - 0.534f * a) * g + (0.131f + 0.869f * a) * b;
            r = nr; g = ng; b = nb;
        }
        if (f->saturate > 0 && f->saturate != 1)
        {
            float s = f->saturate;

            nr = (0.213f + 0.787f * s) * r + (0.715f - 0.715f * s) * g + (0.072f - 0.072f * s) * b;
            ng = (0.213f - 0.213f * s) * r + (0.715f + 0.285f * s) * g + (0.072f - 0.072f * s) * b;
            nb = (0.213f - 0.213f * s) * r + (0.715f - 0.715f * s) * g + (0.072f + 0.928f * s) * b;
            r = nr; g = ng; b = nb;
        }
        if (f->brightness > 0 && f->brightness != 1)
        {
            r *= f->brightness;
            g *= f->brightness;
            b *= f->brightness;
        }
        rgba[i * 4] = (uint8_t)lrintf(Clamp01(r) * 255.0f);
        rgba[i * 4 + 1] = (uint8_t)lrintf(Clamp01(g) * 255.0f);
        rgba[i * 4 + 2] = (uint8_t)lrintf(Clamp01(b) * 255.0f);
    }
}

void UiFilter_Key(char *buf, int size, const struct UiFilter *f)
{
    if (f == NULL || (f->grayscale <= 0 && f->sepia <= 0 && (f->saturate <= 0 || f->saturate == 1)
                      && (f->brightness <= 0 || f->brightness == 1)))
    {
        if (size > 0)
            buf[0] = '\0';
        return;
    }
    snprintf(buf, (size_t)size, "f%.3f,%.3f,%.3f,%.3f", f->grayscale, f->sepia, f->saturate, f->brightness);
}
#endif // ENABLE_NATIVE_UI
