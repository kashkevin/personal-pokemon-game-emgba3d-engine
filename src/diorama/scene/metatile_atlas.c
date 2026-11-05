#ifdef ENABLE_DIORAMA

#include <stdlib.h>
#include <string.h>

#include "diorama/metatile_atlas.h"

#define TILE_ID_MASK 0x03FF
#define TILE_HFLIP (1 << 10)
#define TILE_VFLIP (1 << 11)
#define TILE_PALETTE_SHIFT 12

static uint16_t sForegroundAlpha[DIORAMA_ATLAS_SLOT_COUNT][DIORAMA_METATILE_SIZE];
static uint16_t sSilhouetteAlpha[DIORAMA_ATLAS_SLOT_COUNT][DIORAMA_METATILE_SIZE];

/* The neighbour pages' art (DioramaAtlas_SetPages). */
static uint8_t sPageTiles[DIORAMA_NEIGHBOUR_PAGES][DIORAMA_TILE_GRAPHICS_SIZE];
static uint16_t sPageFaded[DIORAMA_NEIGHBOUR_PAGES][DIORAMA_FADED_PALETTE_ENTRIES];
static uint16_t sPageUnfaded[DIORAMA_NEIGHBOUR_PAGES][DIORAMA_FADED_PALETTE_ENTRIES];
static bool sPageSet[DIORAMA_NEIGHBOUR_PAGES];
static uint8_t sHighestPage;
static uint32_t sLastPageSignature;
/* The regions (metatile_atlas.h): each page's, the secondary tileset each holds and the primary they were given
 * under, and the last snapshot's drawn signature (DioramaAtlas_DrawnSignature). */
uint8_t gDioramaAtlasPageRegion[1 + DIORAMA_NEIGHBOUR_PAGES] = {0, 1, 2, 3};
static const void *sRegionTileset[1 + DIORAMA_NEIGHBOUR_PAGES];
static const void *sRegionPrimary;
static uint32_t sDrawnSignature;

uint8_t DioramaMetatile_DecodePixel(const uint8_t *tileGraphics, uint16_t tileEntry,
                                    uint8_t x, uint8_t y)
{
    uint16_t tileId = tileEntry & TILE_ID_MASK;
    uint8_t sourceX = tileEntry & TILE_HFLIP ? 7 - x : x;
    uint8_t sourceY = tileEntry & TILE_VFLIP ? 7 - y : y;
    uint8_t packed = tileGraphics[tileId * DIORAMA_TILE_BYTES + sourceY * 4 + sourceX / 2];

    return sourceX & 1 ? packed >> 4 : packed & 0x0F;
}

uint32_t DioramaMetatile_ConvertColor(uint16_t bgr555, bool transparent)
{
    uint8_t red = bgr555 & 0x1F;
    uint8_t green = (bgr555 >> 5) & 0x1F;
    uint8_t blue = (bgr555 >> 10) & 0x1F;

    red = (red << 3) | (red >> 2);
    green = (green << 3) | (green >> 2);
    blue = (blue << 3) | (blue >> 2);
    return (transparent ? 0u : 0xFF000000u)
         | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

/*
 * KNOWN_ISSUES MS-118 (the atlas redrawn at a crossing): the pixels as DioramaMetatile_DecodePixel and
 * DioramaMetatile_ConvertColor give them, a tile's row at a time and each palette's colours converted once (the
 * per-pixel calls were two thirds of a redraw).
 */
struct ConvertedPalettes
{
    const uint16_t *palette;  /* the 16 palettes' raw colours, NULL: none yet */
    uint16_t converted;       /* bit p: colors[p] holds palette p's */
    uint32_t colors[16][16];  /* colour 0: 0 (transparent) */
};

static void ConvertedPalettes_Start(struct ConvertedPalettes *cache, const uint16_t *palette)
{
    cache->palette = palette;
    cache->converted = 0;
}

static const uint32_t *ConvertedPalettes_Get(struct ConvertedPalettes *cache, unsigned paletteId)
{
    if (!(cache->converted & (1u << paletteId)))
    {
        const uint16_t *source = cache->palette + paletteId * 16;
        unsigned i;

        cache->colors[paletteId][0] = 0;
        for (i = 1; i < 16; i++)
            cache->colors[paletteId][i] = DioramaMetatile_ConvertColor(source[i], false);
        cache->converted |= (uint16_t)(1u << paletteId);
    }
    return cache->colors[paletteId];
}

/* One tile (an entry: its tile, flips and palette) into quarter `tile` (0-3) of a metatile's pixels: `over` another
 * layer, colour 0 leaves the pixel as it was; else the quarter is written whole, colour 0 transparent (0). */
static void DrawTile(const uint8_t *tileGraphics, uint16_t entry, struct ConvertedPalettes *palettes,
                     uint32_t *pixels, int tile, bool over)
{
    const uint8_t *source = tileGraphics + (entry & TILE_ID_MASK) * DIORAMA_TILE_BYTES;
    const uint32_t *colors = ConvertedPalettes_Get(palettes, entry >> TILE_PALETTE_SHIFT);
    uint32_t *out = pixels + (tile >> 1) * 8 * DIORAMA_METATILE_SIZE + (tile & 1) * 8;
    int y;

    for (y = 0; y < 8; y++, out += DIORAMA_METATILE_SIZE)
    {
        const uint8_t *row = source + (entry & TILE_VFLIP ? 7 - y : y) * 4;
        uint8_t ids[8];
        int x;

        for (x = 0; x < 4; x++)
        {
            ids[x * 2] = row[x] & 0x0F;
            ids[x * 2 + 1] = row[x] >> 4;
        }
        if (!over && (entry & TILE_HFLIP))
        {
            for (x = 0; x < 8; x++)
                out[x] = colors[ids[7 - x]];
        }
        else if (!over)
        {
            for (x = 0; x < 8; x++)
                out[x] = colors[ids[x]];
        }
        else if (entry & TILE_HFLIP)
        {
            for (x = 0; x < 8; x++)
                if (ids[7 - x] != 0)
                    out[x] = colors[ids[7 - x]];
        }
        else
        {
            for (x = 0; x < 8; x++)
                if (ids[x] != 0)
                    out[x] = colors[ids[x]];
        }
    }
}

/* A metatile's layer (0 lower, 1 upper; another: nothing drawn): its four tiles write every pixel. */
static void ComposeLayerWith(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             struct ConvertedPalettes *palettes, uint8_t layer, uint32_t *pixels)
{
    int tile;

    if (layer > 1)
    {
        memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
        return;
    }
    for (tile = 0; tile < 4; tile++)
        DrawTile(tileGraphics, tileEntries[layer * 4 + tile], palettes, pixels, tile, false);
}

/* Both layers, the upper over the lower. */
static void ComposeWith(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                        struct ConvertedPalettes *palettes, uint32_t *pixels)
{
    int tile;

    for (tile = 0; tile < 8; tile++)
        DrawTile(tileGraphics, tileEntries[tile], palettes, pixels, tile & 3, tile >= 4);
}

void DioramaMetatile_Compose(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             const uint16_t *palette, uint32_t *pixels)
{
    struct ConvertedPalettes palettes;

    ConvertedPalettes_Start(&palettes, palette);
    ComposeWith(tileGraphics, tileEntries, &palettes, pixels);
}

void DioramaMetatile_ComposeLayer(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                                  const uint16_t *palette, uint8_t layer,
                                  uint32_t *pixels)
{
    struct ConvertedPalettes palettes;

    ConvertedPalettes_Start(&palettes, palette);
    ComposeLayerWith(tileGraphics, tileEntries, &palettes, layer, pixels);
}

void DioramaMetatile_BuildAlphaMask(const uint32_t *pixels,
                                    uint16_t rows[DIORAMA_METATILE_SIZE])
{
    int y;

    for (y = 0; y < DIORAMA_METATILE_SIZE; y++)
    {
        const uint32_t *row = pixels + y * DIORAMA_METATILE_SIZE;
        unsigned bits = 0;  /* the row's in a register (MS-118) */
        int x;

        for (x = 0; x < DIORAMA_METATILE_SIZE; x++)
            bits |= (unsigned)((row[x] >> 24) != 0) << x;
        rows[y] = (uint16_t)bits;
    }
}

void DioramaMetatile_BuildSilhouetteMask(const uint32_t *pixels,
                                         uint16_t rows[DIORAMA_METATILE_SIZE])
{
    uint16_t queue[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint8_t outside[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE] = {0};
    uint32_t cornerColors[4] = {
        pixels[0], pixels[DIORAMA_METATILE_SIZE - 1],
        pixels[(DIORAMA_METATILE_SIZE - 1) * DIORAMA_METATILE_SIZE],
        pixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE - 1],
    };
    uint32_t background = cornerColors[0];
    int bestCount = 0;
    int readIndex = 0;
    int writeIndex = 0;
    int i;

    for (i = 0; i < 4; i++)
    {
        int count = 0;
        int other;

        for (other = 0; other < 4; other++)
            count += cornerColors[other] == cornerColors[i];
        if (count > bestCount || (count == bestCount && cornerColors[i] < background))
        {
            background = cornerColors[i];
            bestCount = count;
        }
    }
    DioramaMetatile_BuildAlphaMask(pixels, rows);
    if (bestCount < 2)
        return;
    for (i = 0; i < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; i++)
    {
        int x = i % DIORAMA_METATILE_SIZE;
        int y = i / DIORAMA_METATILE_SIZE;

        if ((x == 0 || x == DIORAMA_METATILE_SIZE - 1
          || y == 0 || y == DIORAMA_METATILE_SIZE - 1)
         && pixels[i] == background && !outside[i])
        {
            outside[i] = 1;
            queue[writeIndex++] = i;
        }
    }
    while (readIndex < writeIndex)
    {
        int index = queue[readIndex++];
        int x = index % DIORAMA_METATILE_SIZE;
        int neighbors[4] = {
            x > 0 ? index - 1 : -1,
            x + 1 < DIORAMA_METATILE_SIZE ? index + 1 : -1,
            index >= DIORAMA_METATILE_SIZE ? index - DIORAMA_METATILE_SIZE : -1,
            index + DIORAMA_METATILE_SIZE < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE
                ? index + DIORAMA_METATILE_SIZE : -1,
        };
        int neighborIndex;

        for (neighborIndex = 0; neighborIndex < 4; neighborIndex++)
        {
            int neighbor = neighbors[neighborIndex];

            if (neighbor >= 0 && !outside[neighbor] && pixels[neighbor] == background)
            {
                outside[neighbor] = 1;
                queue[writeIndex++] = neighbor;
            }
        }
    }
    for (i = 0; i < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; i++)
        if (outside[i])
            rows[i / DIORAMA_METATILE_SIZE] &= ~((uint16_t)1 << (i % DIORAMA_METATILE_SIZE));
}

/* Each channel's mapping, unfaded value to faded (0 to 31), read off the
 * live BG palettes 0 to 12 (DioramaAtlas_MapPalette). */
static void FitChannelMaps(const uint16_t *unfaded, const uint16_t *faded, uint8_t maps[3][32])
{
    int sums[3][32] = {{0}};
    int counts[3][32] = {{0}};
    float sumX[3] = {0.0f};
    float sumY[3] = {0.0f};
    float sumXX[3] = {0.0f};
    float sumXY[3] = {0.0f};
    int samples = 0;
    int palette;
    int color;
    int channel;

    for (palette = 0; palette < DIORAMA_PAGE_FIRST_PALETTE + DIORAMA_PAGE_PALETTE_COUNT; palette++)
        for (color = 1; color < 16; color++)
        {
            uint16_t u = unfaded[palette * 16 + color];
            uint16_t f = faded[palette * 16 + color];

            /* A light colour (the high bit, OW_ENABLE_DNS): the night
             * blend gives it its own colour, not the palette's mapping. */
            if (u & 0x8000)
                continue;
            for (channel = 0; channel < 3; channel++)
            {
                int x = (u >> (channel * 5)) & 31;
                int y = (f >> (channel * 5)) & 31;

                sums[channel][x] += y;
                counts[channel][x]++;
                sumX[channel] += x;
                sumY[channel] += y;
                sumXX[channel] += (float)(x * x);
                sumXY[channel] += (float)(x * y);
            }
            samples++;
        }
    for (channel = 0; channel < 3; channel++)
    {
        float slope = 1.0f;
        float offset = 0.0f;
        int known[32];
        int x;

        if (samples > 0)
        {
            float meanX = sumX[channel] / samples;
            float meanY = sumY[channel] / samples;
            float variance = sumXX[channel] / samples - meanX * meanX;
            float covariance = sumXY[channel] / samples - meanX * meanY;

            slope = variance > 0.01f ? covariance / variance : 1.0f;
            if (slope < 0.0f)
                slope = 0.0f;
            offset = meanY - slope * meanX;
        }
        for (x = 0; x < 32; x++)
            known[x] = counts[channel][x] != 0
                     ? (2 * sums[channel][x] + counts[channel][x]) / (2 * counts[channel][x]) : -1;
        for (x = 0; x < 32; x++)
        {
            float value;
            int low = x;
            int high = x;

            if (known[x] >= 0)
            {
                maps[channel][x] = (uint8_t)known[x];
                continue;
            }
            while (low >= 0 && known[low] < 0)
                low--;
            while (high < 32 && known[high] < 0)
                high++;
            if (low >= 0 && high < 32)
                value = known[low] + (float)(known[high] - known[low]) * (x - low) / (high - low);
            else if (low >= 0)
                value = known[low] + slope * (x - low);
            else if (high < 32)
                value = known[high] + slope * (x - high);
            else
                value = slope * x + offset;
            value += 0.5f;
            maps[channel][x] = value < 0.0f ? 0 : (value > 31.0f ? 31 : (uint8_t)value);
        }
    }
}

void DioramaAtlas_MapPalette(const uint16_t *liveUnfaded, const uint16_t *liveFaded,
                             const uint16_t *source, uint16_t *out, unsigned count)
{
    uint8_t maps[3][32];
    unsigned i;

    FitChannelMaps(liveUnfaded, liveFaded, maps);
    for (i = 0; i < count; i++)
    {
        uint16_t color = source[i];

        out[i] = maps[0][color & 31] | (uint16_t)(maps[1][(color >> 5) & 31] << 5)
               | (uint16_t)(maps[2][(color >> 10) & 31] << 10);
    }
}

static uint32_t HashBytes(uint32_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    size_t i;

    for (i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

_Static_assert(DIORAMA_NEIGHBOUR_PAGES == 3, "gDioramaAtlasPageRegion's first value lists every region");

/* PKMN_DIORAMA_STICKY_PAGES=0: each page's region is its own number (page 0 the map's), as before 2026-11-05. */
bool DioramaAtlas_StickyRegions(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = getenv("PKMN_DIORAMA_STICKY_PAGES");

        sEnabled = value == NULL || value[0] != '0';
    }
    return sEnabled;
}

/* The first region no page has taken (one is always left: there are as many regions as pages). */
static uint8_t FreeRegion(const bool *taken, uint8_t preferred)
{
    uint8_t region;

    if (!taken[preferred])
        return preferred;
    for (region = 0; region <= DIORAMA_NEIGHBOUR_PAGES; region++)
        if (!taken[region])
            return region;
    return preferred;
}

/*
 * Each page's region (metatile_atlas.h): a page whose tileset a region holds keeps it; another tileset takes its
 * own number's region when that is free, else the first free one (a region whose tileset is out of view: its art
 * is drawn again as cells of the new one come into view, their slots not present). Pages not in use take what is
 * left (no cell is drawn in them).
 */
static void AssignRegions(const struct DioramaSceneSnapshot *snapshot)
{
    const void *tilesets[1 + DIORAMA_NEIGHBOUR_PAGES];
    bool taken[1 + DIORAMA_NEIGHBOUR_PAGES];
    bool placed[1 + DIORAMA_NEIGHBOUR_PAGES];
    uint8_t page;
    uint8_t region;

    memset(taken, 0, sizeof(taken));
    memset(placed, 0, sizeof(placed));
    tilesets[0] = snapshot->secondaryTileset;
    for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
        tilesets[1 + page] = sPageSet[page] ? snapshot->neighbourPages[page].tileset : NULL;
    if (!DioramaAtlas_StickyRegions() || snapshot->primaryTileset != sRegionPrimary)
    {
        /* another primary tileset: the regions' art is another's (only maps sharing one are paged) */
        memset(sRegionTileset, 0, sizeof(sRegionTileset));
        sRegionPrimary = snapshot->primaryTileset;
    }
    if (DioramaAtlas_StickyRegions())
        for (page = 0; page <= DIORAMA_NEIGHBOUR_PAGES; page++)
        {
            if (tilesets[page] == NULL)
                continue;
            for (region = 0; region <= DIORAMA_NEIGHBOUR_PAGES; region++)
                if (!taken[region] && sRegionTileset[region] == tilesets[page])
                {
                    gDioramaAtlasPageRegion[page] = region;
                    taken[region] = placed[page] = true;
                    break;
                }
        }
    for (page = 0; page <= DIORAMA_NEIGHBOUR_PAGES; page++)
    {
        if (placed[page] || tilesets[page] == NULL)
            continue;
        region = FreeRegion(taken, page);
        gDioramaAtlasPageRegion[page] = region;
        taken[region] = placed[page] = true;
        sRegionTileset[region] = tilesets[page];
    }
    for (page = 0; page <= DIORAMA_NEIGHBOUR_PAGES; page++)
    {
        if (placed[page])
            continue;
        region = FreeRegion(taken, page);
        gDioramaAtlasPageRegion[page] = region;
        taken[region] = true;
    }
}

uint32_t DioramaAtlas_SetPages(const struct DioramaSceneSnapshot *snapshot)
{
    uint32_t signature = 2166136261u;
    uint32_t drawn = 2166136261u;
    const unsigned first = DIORAMA_PAGE_FIRST_PALETTE * 16;
    const unsigned count = DIORAMA_PAGE_PALETTE_COUNT * 16;
    int page;

    sHighestPage = 0;
    for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
    {
        const struct DioramaNeighbourPage *source = &snapshot->neighbourPages[page];
        uintptr_t identity = (uintptr_t)source->tileset;

        sPageSet[page] = source->tileset != NULL && source->tiles != NULL;
        signature = HashBytes(signature, &identity, sizeof(identity));
        if (!sPageSet[page])
            continue;
        signature = HashBytes(signature, source->palettes, sizeof(source->palettes));
        memcpy(sPageTiles[page], snapshot->tileGraphics, DIORAMA_PAGE_FIRST_TILE * DIORAMA_TILE_BYTES);
        memcpy(sPageTiles[page] + DIORAMA_PAGE_FIRST_TILE * DIORAMA_TILE_BYTES, source->tiles,
               DIORAMA_PAGE_TILE_BYTES);
        memcpy(sPageUnfaded[page], snapshot->unfadedPalette, sizeof(sPageUnfaded[page]));
        memcpy(sPageFaded[page], snapshot->fadedPalette, sizeof(sPageFaded[page]));
        memcpy(sPageUnfaded[page] + first, source->palettes, count * sizeof(uint16_t));
        DioramaAtlas_MapPalette(snapshot->unfadedPalette, snapshot->fadedPalette,
                                source->palettes, sPageFaded[page] + first, count);
    }
    AssignRegions(snapshot);
    /* What the regions in use are drawn with: the live palettes every page shares (0 to 5: the primary metatiles'
     * slots are every page's), then each region's tileset and its page's palettes 6 to 12. Not 13 to 15: no
     * metatile is drawn with them (the message box's and the map name pop-up's change as the player walks). */
    drawn = HashBytes(drawn, &sRegionPrimary, sizeof(sRegionPrimary));
    drawn = HashBytes(drawn, snapshot->fadedPalette, first * sizeof(uint16_t));
    {
        /* by region, not by page: a crossing turns the pages round, and the same regions holding the same art must
         * give the same signature */
        const void *regionTileset[1 + DIORAMA_NEIGHBOUR_PAGES];
        const uint16_t *regionFaded[1 + DIORAMA_NEIGHBOUR_PAGES];
        uint8_t region;

        memset(regionTileset, 0, sizeof(regionTileset));
        memset(regionFaded, 0, sizeof(regionFaded));
        for (page = 0; page <= DIORAMA_NEIGHBOUR_PAGES; page++)
        {
            const void *tileset = page == 0 ? snapshot->secondaryTileset
                                : (sPageSet[page - 1] ? snapshot->neighbourPages[page - 1].tileset : NULL);

            if (tileset == NULL)
                continue;
            region = gDioramaAtlasPageRegion[page];
            regionTileset[region] = tileset;
            regionFaded[region] = page == 0 ? snapshot->fadedPalette : sPageFaded[page - 1];
            if (region > sHighestPage)
                sHighestPage = region;
        }
        for (region = 0; region <= DIORAMA_NEIGHBOUR_PAGES; region++)
        {
            if (regionTileset[region] == NULL)
                continue;
            drawn = HashBytes(drawn, &region, sizeof(region));
            drawn = HashBytes(drawn, &regionTileset[region], sizeof(regionTileset[region]));
            drawn = HashBytes(drawn, regionFaded[region] + first, count * sizeof(uint16_t));
        }
    }
    sDrawnSignature = drawn;
    /* The pages' signature (the terrain's sync key): the regions, not the pages, hold the art, so it is the drawn
     * one with the sticky regions, the pages' own as before without them. */
    sLastPageSignature = DioramaAtlas_StickyRegions() ? drawn : signature;
    return sLastPageSignature;
}

uint32_t DioramaAtlas_DrawnSignature(void)
{
    return sDrawnSignature;
}

const void *DioramaAtlas_RegionTileset(uint8_t region)
{
    return region <= DIORAMA_NEIGHBOUR_PAGES ? sRegionTileset[region] : NULL;
}

uint32_t DioramaAtlas_LastPageSignature(void)
{
    return sLastPageSignature;
}

struct DioramaAtlasArt DioramaAtlas_PageArt(const struct DioramaSceneSnapshot *snapshot,
                                            uint8_t page)
{
    struct DioramaAtlasArt art;

    if (page == 0 || page > DIORAMA_NEIGHBOUR_PAGES || !sPageSet[page - 1])
    {
        art.tileGraphics = snapshot->tileGraphics;
        art.fadedPalette = snapshot->fadedPalette;
        art.unfadedPalette = snapshot->unfadedPalette;
    }
    else
    {
        art.tileGraphics = sPageTiles[page - 1];
        art.fadedPalette = sPageFaded[page - 1];
        art.unfadedPalette = sPageUnfaded[page - 1];
    }
    return art;
}

struct DioramaAtlasArt DioramaAtlas_CellArt(const struct DioramaSceneSnapshot *snapshot,
                                            const struct DioramaCellSnapshot *cell)
{
    return DioramaAtlas_PageArt(snapshot, cell->atlasPage);
}

uint8_t DioramaAtlas_HighestPage(void)
{
    return sHighestPage;
}

void DioramaAtlas_ClearRows(uint32_t *atlasPixels, uint8_t *presentSlots, unsigned pixelRows)
{
    if (pixelRows > DIORAMA_ATLAS_HEIGHT)
        pixelRows = DIORAMA_ATLAS_HEIGHT;
    memset(atlasPixels, 0, (size_t)pixelRows * DIORAMA_ATLAS_WIDTH * sizeof(*atlasPixels));
    memset(presentSlots, 0, DIORAMA_ATLAS_PRESENT_BYTES);
    memset(sForegroundAlpha, 0, sizeof(sForegroundAlpha));
    memset(sSilhouetteAlpha, 0, sizeof(sSilhouetteAlpha));
}

void DioramaAtlas_Clear(uint32_t *atlasPixels, uint8_t *presentSlots)
{
    DioramaAtlas_ClearRows(atlasPixels, presentSlots, DIORAMA_ATLAS_HEIGHT);
}

static bool HasBit(const uint8_t *bits, uint16_t slot)
{
    return (bits[slot / 8] & (1 << (slot % 8))) != 0;
}

/* Whether a cell's drawing uses a dirty tile of the snapshot's: a page's own
 * tiles (512 on) never change. */
static bool EntriesUseDirtyTile(const struct DioramaCellSnapshot *cell, const uint8_t *dirtyTiles)
{
    int i;

    if (dirtyTiles == NULL)
        return false;
    for (i = 0; i < DIORAMA_METATILE_ENTRY_COUNT; i++)
    {
        uint16_t tileId = cell->tileEntries[i] & TILE_ID_MASK;

        if (cell->atlasPage != 0 && tileId >= DIORAMA_PAGE_FIRST_TILE)
            continue;
        if (dirtyTiles[tileId / 8] & (1 << (tileId % 8)))
            return true;
    }
    return false;
}

/* The slot's box: the metatile, its edge pixels repeated in the gutter around it (a row at a time, MS-118). */
static void BlitWithGutter(uint32_t *atlasPixels, uint16_t slot, const uint32_t *pixels)
{
    uint32_t *out = atlasPixels + (slot / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE * DIORAMA_ATLAS_WIDTH
                  + (slot % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE;
    int y;

    for (y = -DIORAMA_ATLAS_GUTTER; y < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; y++, out += DIORAMA_ATLAS_WIDTH)
    {
        int sourceY = y < 0 ? 0 : (y >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : y);
        const uint32_t *row = pixels + sourceY * DIORAMA_METATILE_SIZE;
        int x;

        for (x = 0; x < DIORAMA_ATLAS_GUTTER; x++)
        {
            out[x] = row[0];
            out[DIORAMA_ATLAS_GUTTER + DIORAMA_METATILE_SIZE + x] = row[DIORAMA_METATILE_SIZE - 1];
        }
        memcpy(out + DIORAMA_ATLAS_GUTTER, row, DIORAMA_METATILE_SIZE * sizeof(*row));
    }
}

/*
 * Each slot's first cell (DIORAMA_ATLAS_NO_CELL: none), worked out once a
 * pass: each cell with a cutout base looked for its base's first cell from
 * the grid's start, and every cell of a slot that used a changed tile drew
 * the slot again - its 1,000 to 2,000 cells of water drew the same 26 slots
 * each time the water moved on, 10 to 15 ms every few frames on the cloud
 * machine (performance work 2026-10-26, the owner's lag when running). A
 * slot is one metatile of one page, so its cells draw it the same and use
 * the same changed tiles: each slot is decided and drawn once a pass, from
 * its first cell as before.
 */
#define DIORAMA_ATLAS_NO_CELL 0xFFFF
static uint16_t sSlotFirstCell[DIORAMA_ATLAS_SLOT_COUNT];

static void FindSlotCells(const struct DioramaSceneSnapshot *snapshot, unsigned cellCount)
{
    unsigned i;

    memset(sSlotFirstCell, 0xFF, sizeof(sSlotFirstCell));
    for (i = cellCount; i-- > 0;)
    {
        uint16_t slot = DioramaAtlas_CellSlot(&snapshot->cells[i]);

        if (slot < DIORAMA_ATLAS_SLOT_COUNT)
            sSlotFirstCell[slot] = (uint16_t)i;
    }
}

/* The first cell drawn in a slot (FindSlotCells). */
static const struct DioramaCellSnapshot *FindSlotCell(const struct DioramaSceneSnapshot *snapshot, uint16_t slot)
{
    return sSlotFirstCell[slot] == DIORAMA_ATLAS_NO_CELL ? NULL : &snapshot->cells[sSlotFirstCell[slot]];
}

/* A pass's converted palettes, one set for each art's faded palette (the map's and up to DIORAMA_NEIGHBOUR_PAGES
 * pages', each fixed for the pass): a palette's colours converted once a pass (MS-118). */
struct PassPalettes
{
    unsigned count;
    struct ConvertedPalettes sets[DIORAMA_NEIGHBOUR_PAGES + 1];
};

static struct ConvertedPalettes *PassPalettes_For(struct PassPalettes *pass, const uint16_t *palette)
{
    unsigned i;

    for (i = 0; i < pass->count; i++)
        if (pass->sets[i].palette == palette)
            return &pass->sets[i];
    if (pass->count == DIORAMA_NEIGHBOUR_PAGES + 1)
        pass->count = 0;                /* never: a pass's arts are the map's and its pages' */
    ConvertedPalettes_Start(&pass->sets[pass->count], palette);
    return &pass->sets[pass->count++];
}

/* A slot drawn from its cell alone: the whole metatile (the upper layer over the lower, as DioramaMetatile_Compose
 * draws it: a drawn colour is never 0), its silhouette, the lower layer, the upper and its alpha. */
static void DrawSlotLayers(uint16_t slot, const struct DioramaAtlasArt *art, const uint16_t *tileEntries,
                           struct ConvertedPalettes *palettes, uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                           uint32_t *foregroundAtlasPixels)
{
    uint32_t metatilePixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t lowerPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t upperPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    unsigned pixel;

    ComposeLayerWith(art->tileGraphics, tileEntries, palettes, 0, lowerPixels);
    ComposeLayerWith(art->tileGraphics, tileEntries, palettes, 1, upperPixels);
    for (pixel = 0; pixel < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; pixel++)
        metatilePixels[pixel] = upperPixels[pixel] != 0 ? upperPixels[pixel] : lowerPixels[pixel];
    DioramaMetatile_BuildSilhouetteMask(metatilePixels, sSilhouetteAlpha[slot]);
    BlitWithGutter(atlasPixels, slot, metatilePixels);
    BlitWithGutter(baseAtlasPixels, slot, lowerPixels);
    BlitWithGutter(foregroundAtlasPixels, slot, upperPixels);
    DioramaMetatile_BuildAlphaMask(upperPixels, sForegroundAlpha[slot]);
}

bool DioramaAtlas_UpdateDirty(const struct DioramaSceneSnapshot *snapshot,
                              const uint16_t *cutoutBaseSlots,
                              const uint8_t *dirtyTiles,
                              const uint8_t *forcedSlots,
                              uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                              uint32_t *foregroundAtlasPixels, uint8_t *presentSlots,
                              uint8_t *updatedSlots)
{
    uint32_t metatilePixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t layerPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint8_t seen[DIORAMA_ATLAS_PRESENT_BYTES]; /* the slots this pass has decided */
    struct PassPalettes palettes;
    bool changed = false;
    unsigned i;

    unsigned cellCount = snapshot->visibleCellCount;

    if (cellCount > DIORAMA_MAX_VISIBLE_CELLS)
        cellCount = DIORAMA_MAX_VISIBLE_CELLS;
    FindSlotCells(snapshot, cellCount);
    memset(seen, 0, sizeof(seen));
    palettes.count = 0;
    for (i = 0; i < cellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        uint16_t slot = DioramaAtlas_CellSlot(cell);
        uint16_t baseSlot;
        const struct DioramaCellSnapshot *baseCell = NULL;
        struct DioramaAtlasArt art;
        bool present;
        bool dirty;

        if (slot >= DIORAMA_ATLAS_SLOT_COUNT || HasBit(seen, slot))
            continue;
        seen[slot / 8] |= 1 << (slot % 8);
        baseSlot = cutoutBaseSlots != NULL ? cutoutBaseSlots[slot] : DIORAMA_ATLAS_NO_SLOT;
        present = HasBit(presentSlots, slot);
        dirty = EntriesUseDirtyTile(cell, dirtyTiles)
             || (forcedSlots != NULL && HasBit(forcedSlots, slot));
        if (baseSlot < DIORAMA_ATLAS_SLOT_COUNT)
        {
            baseCell = FindSlotCell(snapshot, baseSlot);
            if (baseCell != NULL)
                dirty |= EntriesUseDirtyTile(baseCell, dirtyTiles);
        }
        if (present && !dirty)
            continue;
        art = DioramaAtlas_CellArt(snapshot, cell);
        if (baseSlot < DIORAMA_ATLAS_SLOT_COUNT && baseCell != NULL)
        {
            struct DioramaAtlasArt baseArt = DioramaAtlas_CellArt(snapshot, baseCell);
            unsigned pixel;

            ComposeWith(art.tileGraphics, cell->tileEntries, PassPalettes_For(&palettes, art.fadedPalette),
                        metatilePixels);
            DioramaMetatile_BuildSilhouetteMask(metatilePixels, sSilhouetteAlpha[slot]);
            BlitWithGutter(atlasPixels, slot, metatilePixels);
            ComposeWith(baseArt.tileGraphics, baseCell->tileEntries, PassPalettes_For(&palettes, baseArt.fadedPalette),
                        layerPixels);
            BlitWithGutter(baseAtlasPixels, slot, layerPixels);
            for (pixel = 0; pixel < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; pixel++)
                if (metatilePixels[pixel] == layerPixels[pixel])
                    metatilePixels[pixel] = 0;
            BlitWithGutter(foregroundAtlasPixels, slot, metatilePixels);
            DioramaMetatile_BuildAlphaMask(metatilePixels, sForegroundAlpha[slot]);
        }
        else
        {
            DrawSlotLayers(slot, &art, cell->tileEntries, PassPalettes_For(&palettes, art.fadedPalette),
                           atlasPixels, baseAtlasPixels, foregroundAtlasPixels);
        }
        presentSlots[slot / 8] |= 1 << (slot % 8);
        if (updatedSlots != NULL)
            updatedSlots[slot / 8] |= 1 << (slot % 8);
        changed = true;
    }
    /* General's rock wall, the face a tall side of plain ground shows (scene_snapshot.h rockFaceEntries,
     * terrain_mesh.c GroundSideShowsRock), whether or not a cell in view uses it: the map's own metatile,
     * drawn as a cell of it would be. */
    if (snapshot->rockFaceValid && !HasBit(seen, DIORAMA_ROCK_FACE_METATILE))
    {
        struct DioramaCellSnapshot rock;
        uint16_t slot = DIORAMA_ROCK_FACE_METATILE;

        memset(&rock, 0, sizeof(rock));
        rock.metatileId = DIORAMA_ROCK_FACE_METATILE;
        memcpy(rock.tileEntries, snapshot->rockFaceEntries, sizeof(rock.tileEntries));
        if (!HasBit(presentSlots, slot) || EntriesUseDirtyTile(&rock, dirtyTiles)
         || (forcedSlots != NULL && HasBit(forcedSlots, slot)))
        {
            struct DioramaAtlasArt art = DioramaAtlas_CellArt(snapshot, &rock);

            DrawSlotLayers(slot, &art, rock.tileEntries, PassPalettes_For(&palettes, art.fadedPalette),
                           atlasPixels, baseAtlasPixels, foregroundAtlasPixels);
            presentSlots[slot / 8] |= 1 << (slot % 8);
            if (updatedSlots != NULL)
                updatedSlots[slot / 8] |= 1 << (slot % 8);
            changed = true;
        }
    }
    return changed;
}

bool DioramaAtlas_Update(const struct DioramaSceneSnapshot *snapshot,
                         const uint16_t *cutoutBaseSlots,
                         uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                         uint32_t *foregroundAtlasPixels, uint8_t *presentSlots)
{
    return DioramaAtlas_UpdateDirty(snapshot, cutoutBaseSlots, NULL, NULL,
                                    atlasPixels, baseAtlasPixels,
                                    foregroundAtlasPixels, presentSlots, NULL);
}

struct DioramaAtlasUv DioramaAtlas_GetUv(uint16_t slot)
{
    float x = (slot % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE
            + DIORAMA_ATLAS_GUTTER + 0.5f;
    float y = (slot / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE
            + DIORAMA_ATLAS_GUTTER + 0.5f;
    struct DioramaAtlasUv uv = {
        x / DIORAMA_ATLAS_WIDTH,
        y / DIORAMA_ATLAS_HEIGHT,
        (x + DIORAMA_METATILE_SIZE - 1) / DIORAMA_ATLAS_WIDTH,
        (y + DIORAMA_METATILE_SIZE - 1) / DIORAMA_ATLAS_HEIGHT,
    };

    return uv;
}

void DioramaAtlas_GetForegroundAlphaMask(uint16_t slot,
                                         uint16_t rows[DIORAMA_METATILE_SIZE])
{
    if (slot >= DIORAMA_ATLAS_SLOT_COUNT)
        memset(rows, 0, DIORAMA_METATILE_SIZE * sizeof(*rows));
    else
        memcpy(rows, sForegroundAlpha[slot],
               DIORAMA_METATILE_SIZE * sizeof(*rows));
}

void DioramaAtlas_GetSilhouetteAlphaMask(uint16_t slot,
                                         uint16_t rows[DIORAMA_METATILE_SIZE])
{
    if (slot >= DIORAMA_ATLAS_SLOT_COUNT)
        memset(rows, 0, DIORAMA_METATILE_SIZE * sizeof(*rows));
    else
        memcpy(rows, sSilhouetteAlpha[slot],
               DIORAMA_METATILE_SIZE * sizeof(*rows));
}

#endif
