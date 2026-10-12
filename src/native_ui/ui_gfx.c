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
#include "data.h"
#include "decompress.h"
#include "item_icon.h"
#include "pokemon.h"
#include "constants/items.h"
#include "constants/species.h"

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

static void UiGfx_ArtShutdown(void);

void UiGfx_Shutdown(void)
{
    stbi_image_free(sIcons);
    sIcons = NULL;
    UiGfx_ArtShutdown();
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

/* ---------------------------------------------------------------- the game's compressed art */

/* Pokémon pictures and item icons are compressed in the game's data; the
 * decompressors share the game's state, so they run only at the frame
 * boundary (a screen's capture prepares what its draw shows). The decoded
 * pictures stay here (RGBA) and reach the atlas on their first draw. */
#define ART_SLOTS 64
#define ART_MAX_ANIM 24
enum
{
    ART_MON_PIC = 1,
    ART_ITEM_ICON,
    ART_TRAINER_PIC,
};
struct ArtEntry
{
    uint32_t kind;
    uint32_t a;
    uint32_t b;
    int w;
    int h;            /* all frames, stacked */
    int frames;
    int footY;        /* the lowest painted row of any frame */
    int animCount;    /* the species' front animation (frame, duration) */
    uint8_t animFrame[ART_MAX_ANIM];
    uint8_t animDuration[ART_MAX_ANIM];
    uint8_t *rgba;
    uint32_t lastUse;
};
static struct ArtEntry sArt[ART_SLOTS];
static uint32_t sArtClock;
static uint8_t *sArtScratch;

static struct ArtEntry *ArtFind(uint32_t kind, uint32_t a, uint32_t b)
{
    for (int i = 0; i < ART_SLOTS; i++)
        if (sArt[i].rgba != NULL && sArt[i].kind == kind && sArt[i].a == a && sArt[i].b == b)
        {
            sArt[i].lastUse = ++sArtClock;
            return &sArt[i];
        }
    return NULL;
}

static struct ArtEntry *ArtNew(uint32_t kind, uint32_t a, uint32_t b, int w, int h)
{
    struct ArtEntry *e = &sArt[0];

    for (int i = 0; i < ART_SLOTS; i++)
    {
        if (sArt[i].rgba == NULL)
        {
            e = &sArt[i];
            break;
        }
        if (sArt[i].lastUse < e->lastUse)
            e = &sArt[i];
    }
    free(e->rgba);
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->a = a;
    e->b = b;
    e->w = w;
    e->h = h;
    e->rgba = calloc((size_t)w * h, 4);
    e->lastUse = ++sArtClock;
    return e->rgba != NULL ? e : NULL;
}

static bool ArtScratch(void)
{
    if (sArtScratch == NULL)
        sArtScratch = malloc(MAX_DECOMPRESSION_BUFFER_SIZE);
    if (sArtScratch != NULL)
        memset(sArtScratch, 0, MAX_DECOMPRESSION_BUFFER_SIZE);
    return sArtScratch != NULL;
}

static uint32_t MonPicKeyB(bool shiny, bool isEgg)
{
    return (shiny ? 1u : 0u) | (isEgg ? 2u : 0u);
}

bool UiGfx_PrepareMonPic(uint16_t species, uint32_t personality, bool shiny, bool isEgg)
{
    struct ArtEntry *e;
    const uint16_t *palette;
    const union AnimCmd *anim;
    int lowest = -1;

    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return false;
    if (ArtFind(ART_MON_PIC, ((uint32_t)species << 16) ^ personality, MonPicKeyB(shiny, isEgg) | ((uint32_t)species << 8)) != NULL)
        return true;
    if (!ArtScratch())
        return false;
    LoadSpecialPokePicIsEgg(sArtScratch, species, personality, TRUE, isEgg);
    palette = GetMonSpritePalFromSpeciesAndPersonalityIsEgg(species, shiny, personality, isEgg);
    e = ArtNew(ART_MON_PIC, ((uint32_t)species << 16) ^ personality, MonPicKeyB(shiny, isEgg) | ((uint32_t)species << 8),
               MON_PIC_WIDTH, MON_PIC_HEIGHT * MAX_MON_PIC_FRAMES);
    if (e == NULL || palette == NULL)
        return false;
    UiGfx_Decode4bpp(e->rgba, e->w, sArtScratch, palette, e->w, e->h);
    /* a picture of one frame leaves the second empty */
    e->frames = 1;
    for (int i = MON_PIC_WIDTH * MON_PIC_HEIGHT; i < e->w * e->h; i++)
        if (e->rgba[i * 4 + 3] != 0)
        {
            e->frames = 2;
            break;
        }
    for (int y = 0; y < e->h; y++)
        for (int x = 0; x < e->w; x++)
            if (e->rgba[(y * e->w + x) * 4 + 3] != 0)
                lowest = (y % MON_PIC_HEIGHT) > lowest ? (y % MON_PIC_HEIGHT) : lowest;
    e->footY = lowest < 0 ? MON_PIC_HEIGHT - 1 : lowest;
    /* the species' own front animation (its second sequence; the first is still),
     * where the game animates it */
    if (isEgg || !HasTwoFramesAnimation(species))
        e->frames = 1;
    anim = !isEgg && e->frames > 1 && gSpeciesInfo[species].frontAnimFrames != NULL
         ? gSpeciesInfo[species].frontAnimFrames[1] : NULL;
    for (int i = 0; anim != NULL && anim[i].type >= 0 && e->animCount < ART_MAX_ANIM; i++)
    {
        e->animFrame[e->animCount] = (uint8_t)(anim[i].frame.imageValue < (u32)e->frames ? anim[i].frame.imageValue : 0);
        e->animDuration[e->animCount] = (uint8_t)anim[i].frame.duration;
        e->animCount++;
    }
    return true;
}

struct ArtCtx
{
    const struct ArtEntry *e;
    int y;
    int h;
    const struct UiFilter *filter;
};

static void BuildArt(struct UiBitmap *bm, const void *ctx)
{
    const struct ArtCtx *c = ctx;

    memcpy(bm->d, &c->e->rgba[(size_t)c->y * c->e->w * 4], (size_t)c->e->w * c->h * 4);
    UiFilter_Apply(bm->d, c->e->w * c->h, c->filter);
}

int UiGfx_MonPicFrameAt(uint16_t species, uint32_t personality, bool shiny, bool isEgg, int t)
{
    const struct ArtEntry *e = ArtFind(ART_MON_PIC, ((uint32_t)species << 16) ^ personality,
                                       MonPicKeyB(shiny, isEgg) | ((uint32_t)species << 8));

    if (e == NULL || t < 0)
        return 0;
    for (int i = 0; i < e->animCount; i++)
    {
        int d = e->animDuration[i] > 0 ? e->animDuration[i] : 1;

        if (t < d)
            return e->animFrame[i];
        t -= d;
    }
    return 0;
}

bool UiGfx_MonPic(uint16_t species, uint32_t personality, bool shiny, bool isEgg, int frame, int footX, int footY,
                  const struct UiFilter *filter)
{
    const struct ArtEntry *e = ArtFind(ART_MON_PIC, ((uint32_t)species << 16) ^ personality,
                                       MonPicKeyB(shiny, isEgg) | ((uint32_t)species << 8));
    struct ArtCtx c;
    char key[128];
    char fkey[48];

    if (e == NULL)
        return false;
    frame = frame >= 0 && frame < e->frames ? frame : 0;
    c.e = e;
    c.y = frame * MON_PIC_HEIGHT;
    c.h = MON_PIC_HEIGHT;
    c.filter = filter;
    UiFilter_Key(fkey, sizeof(fkey), filter);
    snprintf(key, sizeof(key), "mpic:%u:%08x:%u:%d%s", (unsigned)species, (unsigned)personality,
             (unsigned)MonPicKeyB(shiny, isEgg), frame, fkey);
    UiG_Blit(UiG_Cached(key, MON_PIC_WIDTH, MON_PIC_HEIGHT, BuildArt, &c), footX - MON_PIC_WIDTH / 2, footY - e->footY);
    return true;
}

bool UiGfx_PrepareItemIcon(uint16_t item)
{
    struct ArtEntry *e;
    const uint16_t *palette;

    if (item == ITEM_NONE)
        return false;
    if (ArtFind(ART_ITEM_ICON, item, 0) != NULL)
        return true;
    if (!ArtScratch())
        return false;
    DecompressDataWithHeaderWram(GetItemIconPic(item), sArtScratch);
    palette = GetItemIconPalette(item);
    e = ArtNew(ART_ITEM_ICON, item, 0, 24, 24);
    if (e == NULL || palette == NULL)
        return false;
    UiGfx_Decode4bpp(e->rgba, 24, sArtScratch, palette, 24, 24);
    e->frames = 1;
    return true;
}

bool UiGfx_ItemIcon(uint16_t item, int x, int y, const struct UiFilter *filter)
{
    const struct ArtEntry *e = ArtFind(ART_ITEM_ICON, item, 0);
    struct ArtCtx c;
    char key[96];
    char fkey[48];

    if (e == NULL)
        return false;
    c.e = e;
    c.y = 0;
    c.h = 24;
    c.filter = filter;
    UiFilter_Key(fkey, sizeof(fkey), filter);
    snprintf(key, sizeof(key), "item:%u%s", (unsigned)item, fkey);
    UiG_Blit(UiG_Cached(key, 24, 24, BuildArt, &c), x, y);
    return true;
}

bool UiGfx_ItemIconScaled(uint16_t item, int x, int y, int scale)
{
    const struct ArtEntry *e = ArtFind(ART_ITEM_ICON, item, 0);
    struct ArtCtx c;
    char key[96];

    if (e == NULL)
        return false;
    c.e = e;
    c.y = 0;
    c.h = 24;
    c.filter = NULL;
    snprintf(key, sizeof(key), "item:%u", (unsigned)item);
    UiG_BlitPart(UiG_Cached(key, 24, 24, BuildArt, &c), 0, 0, 24, 24, x, y, scale < 1 ? 1 : scale, false, 1.0);
    return true;
}

bool UiGfx_PrepareTrainerPic(uint16_t picId)
{
    struct ArtEntry *e;
    const uint16_t *palette;

    if (ArtFind(ART_TRAINER_PIC, picId, 0) != NULL)
        return true;
    if (!ArtScratch())
        return false;
    DecompressDataWithHeaderWram(GetTrainerFrontPicData(picId), sArtScratch);
    palette = GetTrainerFrontPicPalette(picId);
    e = ArtNew(ART_TRAINER_PIC, picId, 0, 64, 64);
    if (e == NULL || palette == NULL)
        return false;
    UiGfx_Decode4bpp(e->rgba, 64, sArtScratch, palette, 64, 64);
    e->frames = 1;
    return true;
}

bool UiGfx_TrainerPic(uint16_t picId, int x, int y, const struct UiFilter *filter)
{
    const struct ArtEntry *e = ArtFind(ART_TRAINER_PIC, picId, 0);
    struct ArtCtx c;
    char key[96];
    char fkey[48];

    if (e == NULL)
        return false;
    c.e = e;
    c.y = 0;
    c.h = 64;
    c.filter = filter;
    UiFilter_Key(fkey, sizeof(fkey), filter);
    snprintf(key, sizeof(key), "tpic:%u%s", (unsigned)picId, fkey);
    UiG_Blit(UiG_Cached(key, 64, 64, BuildArt, &c), x, y);
    return true;
}

/* ---------------------------------------------------------------- field sprites */

struct FieldSheetCtx
{
    const uint8_t *tiles;
    const uint16_t *palette;
    int frames;
};

static void BuildFieldSheet(struct UiBitmap *bm, const void *ctx)
{
    const struct FieldSheetCtx *c = ctx;

    for (int f = 0; f < c->frames; f++)
        UiGfx_Decode4bpp(&bm->d[(size_t)f * 16 * 32 * 4], 16, c->tiles + f * 16 * 32 / 2, c->palette, 16, 32);
}

bool UiGfx_FieldSprite(const void *tiles, int frameCount, const uint16_t *palette, int frame, int x, int y, int scale,
                       bool flip)
{
    struct FieldSheetCtx c;
    uint32_t h = 2166136261u;
    char key[96];

    if (tiles == NULL || palette == NULL || frame < 0 || frame >= frameCount)
        return false;
    for (int i = 0; i < 16; i++)
        h = (h ^ palette[i]) * 16777619u;
    c.tiles = tiles;
    c.palette = palette;
    c.frames = frameCount;
    snprintf(key, sizeof(key), "ow:%p:%d:%08x", tiles, frameCount, (unsigned)h);
    UiG_BlitPart(UiG_Cached(key, 16, 32 * frameCount, BuildFieldSheet, &c), 0, frame * 32, 16, 32, x, y, scale, flip,
                 1.0);
    return true;
}

static void UiGfx_ArtShutdown(void)
{
    for (int i = 0; i < ART_SLOTS; i++)
    {
        free(sArt[i].rgba);
        sArt[i].rgba = NULL;
    }
    free(sArtScratch);
    sArtScratch = NULL;
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
    if (f->silhouette != UI_NONE)
    {
        for (int i = 0; i < count; i++)
        {
            rgba[i * 4] = (uint8_t)(f->silhouette & 0xFF);
            rgba[i * 4 + 1] = (uint8_t)((f->silhouette >> 8) & 0xFF);
            rgba[i * 4 + 2] = (uint8_t)((f->silhouette >> 16) & 0xFF);
        }
        return;
    }
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
                      && (f->brightness <= 0 || f->brightness == 1) && f->silhouette == UI_NONE))
    {
        if (size > 0)
            buf[0] = '\0';
        return;
    }
    if (f->silhouette != UI_NONE)
        snprintf(buf, (size_t)size, "s%08x", (unsigned)f->silhouette);
    else
        snprintf(buf, (size_t)size, "f%.3f,%.3f,%.3f,%.3f", f->grayscale, f->sepia, f->saturate, f->brightness);
}
#endif // ENABLE_NATIVE_UI
