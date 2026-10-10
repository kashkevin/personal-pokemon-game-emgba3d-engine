#ifdef ENABLE_DIORAMA

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

void DioramaMetatile_Compose(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             const uint16_t *palette, uint32_t *pixels)
{
    int layer;
    int tile;

    memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
    for (layer = 0; layer < 2; layer++)
    {
        for (tile = 0; tile < 4; tile++)
        {
            uint16_t entry = tileEntries[layer * 4 + tile];
            uint8_t paletteId = entry >> TILE_PALETTE_SHIFT;
            int tileX = (tile & 1) * 8;
            int tileY = (tile >> 1) * 8;
            int y;
            int x;

            for (y = 0; y < 8; y++)
            {
                for (x = 0; x < 8; x++)
                {
                    uint8_t colorId = DioramaMetatile_DecodePixel(tileGraphics, entry, x, y);

                    if (colorId != 0)
                    {
                        pixels[(tileY + y) * DIORAMA_METATILE_SIZE + tileX + x]
                            = DioramaMetatile_ConvertColor(palette[paletteId * 16 + colorId], false);
                    }
                }
            }
        }
    }
}

void DioramaMetatile_ComposeLayer(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                                  const uint16_t *palette, uint8_t layer,
                                  uint32_t *pixels)
{
    int tile;

    memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
    if (layer > 1)
        return;
    for (tile = 0; tile < 4; tile++)
    {
        uint16_t entry = tileEntries[layer * 4 + tile];
        uint8_t paletteId = entry >> TILE_PALETTE_SHIFT;
        int tileX = (tile & 1) * 8;
        int tileY = (tile >> 1) * 8;
        int y;
        int x;

        for (y = 0; y < 8; y++)
        {
            for (x = 0; x < 8; x++)
            {
                uint8_t colorId = DioramaMetatile_DecodePixel(tileGraphics, entry, x, y);

                if (colorId != 0)
                    pixels[(tileY + y) * DIORAMA_METATILE_SIZE + tileX + x]
                        = DioramaMetatile_ConvertColor(palette[paletteId * 16 + colorId], false);
            }
        }
    }
}

void DioramaMetatile_BuildAlphaMask(const uint32_t *pixels,
                                    uint16_t rows[DIORAMA_METATILE_SIZE])
{
    int y;

    for (y = 0; y < DIORAMA_METATILE_SIZE; y++)
    {
        int x;

        rows[y] = 0;
        for (x = 0; x < DIORAMA_METATILE_SIZE; x++)
            if (pixels[y * DIORAMA_METATILE_SIZE + x] >> 24)
                rows[y] |= (uint16_t)1 << x;
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

uint32_t DioramaAtlas_SetPages(const struct DioramaSceneSnapshot *snapshot)
{
    uint32_t signature = 2166136261u;
    int page;

    sHighestPage = 0;
    for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
    {
        const struct DioramaNeighbourPage *source = &snapshot->neighbourPages[page];
        uintptr_t identity = (uintptr_t)source->tileset;
        const unsigned first = DIORAMA_PAGE_FIRST_PALETTE * 16;
        const unsigned count = DIORAMA_PAGE_PALETTE_COUNT * 16;

        sPageSet[page] = source->tileset != NULL && source->tiles != NULL;
        signature = HashBytes(signature, &identity, sizeof(identity));
        if (!sPageSet[page])
            continue;
        sHighestPage = page + 1;
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
    return signature;
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

void DioramaAtlas_Clear(uint32_t *atlasPixels, uint8_t *presentSlots)
{
    memset(atlasPixels, 0, DIORAMA_ATLAS_PIXEL_COUNT * sizeof(*atlasPixels));
    memset(presentSlots, 0, DIORAMA_ATLAS_PRESENT_BYTES);
    memset(sForegroundAlpha, 0, sizeof(sForegroundAlpha));
    memset(sSilhouetteAlpha, 0, sizeof(sSilhouetteAlpha));
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

static void BlitWithGutter(uint32_t *atlasPixels, uint16_t slot, const uint32_t *pixels)
{
    int atlasX = (slot % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int atlasY = (slot / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int y;
    int x;

    for (y = -DIORAMA_ATLAS_GUTTER; y < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; y++)
    {
        int sourceY = y < 0 ? 0 : (y >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : y);

        for (x = -DIORAMA_ATLAS_GUTTER; x < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; x++)
        {
            int sourceX = x < 0 ? 0 : (x >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : x);

            atlasPixels[(atlasY + y) * DIORAMA_ATLAS_WIDTH + atlasX + x]
                = pixels[sourceY * DIORAMA_METATILE_SIZE + sourceX];
        }
    }
}

/* The first cell drawn in a slot. */
static const struct DioramaCellSnapshot *FindSlotCell(const struct DioramaSceneSnapshot *snapshot,
                                                      unsigned cellCount, uint16_t slot)
{
    unsigned i;

    for (i = 0; i < cellCount; i++)
        if (DioramaAtlas_CellSlot(&snapshot->cells[i]) == slot)
            return &snapshot->cells[i];
    return NULL;
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
    bool changed = false;
    unsigned i;

    unsigned cellCount = snapshot->visibleCellCount;

    if (cellCount > DIORAMA_MAX_VISIBLE_CELLS)
        cellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < cellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        uint16_t slot = DioramaAtlas_CellSlot(cell);
        uint16_t baseSlot;
        const struct DioramaCellSnapshot *baseCell = NULL;
        struct DioramaAtlasArt art;
        bool present;
        bool dirty;

        if (slot >= DIORAMA_ATLAS_SLOT_COUNT)
            continue;
        baseSlot = cutoutBaseSlots != NULL ? cutoutBaseSlots[slot] : DIORAMA_ATLAS_NO_SLOT;
        present = HasBit(presentSlots, slot);
        dirty = EntriesUseDirtyTile(cell, dirtyTiles)
             || (forcedSlots != NULL && HasBit(forcedSlots, slot));
        if (baseSlot < DIORAMA_ATLAS_SLOT_COUNT)
        {
            baseCell = FindSlotCell(snapshot, cellCount, baseSlot);
            if (baseCell != NULL)
                dirty |= EntriesUseDirtyTile(baseCell, dirtyTiles);
        }
        if (present && !dirty)
            continue;
        art = DioramaAtlas_CellArt(snapshot, cell);
        DioramaMetatile_Compose(art.tileGraphics, cell->tileEntries, art.fadedPalette, metatilePixels);
        DioramaMetatile_BuildSilhouetteMask(metatilePixels, sSilhouetteAlpha[slot]);
        BlitWithGutter(atlasPixels, slot, metatilePixels);
        if (baseSlot < DIORAMA_ATLAS_SLOT_COUNT && baseCell != NULL)
        {
            struct DioramaAtlasArt baseArt = DioramaAtlas_CellArt(snapshot, baseCell);
            unsigned pixel;

            DioramaMetatile_Compose(baseArt.tileGraphics, baseCell->tileEntries,
                                    baseArt.fadedPalette, layerPixels);
            BlitWithGutter(baseAtlasPixels, slot, layerPixels);
            for (pixel = 0; pixel < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; pixel++)
                if (metatilePixels[pixel] == layerPixels[pixel])
                    metatilePixels[pixel] = 0;
            BlitWithGutter(foregroundAtlasPixels, slot, metatilePixels);
            DioramaMetatile_BuildAlphaMask(metatilePixels, sForegroundAlpha[slot]);
        }
        else
        {
            DioramaMetatile_ComposeLayer(art.tileGraphics, cell->tileEntries,
                                         art.fadedPalette, 0, layerPixels);
            BlitWithGutter(baseAtlasPixels, slot, layerPixels);
            DioramaMetatile_ComposeLayer(art.tileGraphics, cell->tileEntries,
                                         art.fadedPalette, 1, layerPixels);
            BlitWithGutter(foregroundAtlasPixels, slot, layerPixels);
            DioramaMetatile_BuildAlphaMask(layerPixels, sForegroundAlpha[slot]);
        }
        presentSlots[slot / 8] |= 1 << (slot % 8);
        if (updatedSlots != NULL)
            updatedSlots[slot / 8] |= 1 << (slot % 8);
        changed = true;
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
