#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/metatile_atlas.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_metatile_atlas.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void SetPixel(uint8_t *graphics, uint16_t tile, uint8_t x, uint8_t y, uint8_t color)
{
    uint8_t *packed = &graphics[tile * DIORAMA_TILE_BYTES + y * 4 + x / 2];

    if (x & 1)
        *packed = (*packed & 0x0F) | (color << 4);
    else
        *packed = (*packed & 0xF0) | color;
}

static void TestDecodeAndFlips(void)
{
    static uint8_t graphics[DIORAMA_TILE_GRAPHICS_SIZE];

    memset(graphics, 0, sizeof(graphics));
    SetPixel(graphics, 7, 1, 2, 5);
    SetPixel(graphics, 7, 6, 5, 9);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7, 1, 2) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 10), 6, 2) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 11), 1, 5) == 5);
    CHECK(DioramaMetatile_DecodePixel(graphics, 7 | (1 << 10) | (1 << 11), 1, 2) == 9);
}

static void TestColorAndComposition(void)
{
    static uint8_t graphics[DIORAMA_TILE_GRAPHICS_SIZE];
    uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT] = {0};
    uint16_t palette[DIORAMA_FADED_PALETTE_ENTRIES] = {0};
    uint32_t pixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t layerPixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    int x;
    int y;

    CHECK(DioramaMetatile_ConvertColor(0x001F, false) == 0xFFFF0000u);
    CHECK(DioramaMetatile_ConvertColor(0x03E0, false) == 0xFF00FF00u);
    CHECK(DioramaMetatile_ConvertColor(0x7C00, false) == 0xFF0000FFu);
    CHECK(DioramaMetatile_ConvertColor(0x7FFF, true) == 0x00FFFFFFu);

    memset(graphics, 0, sizeof(graphics));
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            SetPixel(graphics, 1, x, y, 2);
    SetPixel(graphics, 2, 0, 0, 3);
    entries[0] = 1 | (1 << 12);
    entries[4] = 2 | (1 << 12);
    palette[16 + 2] = 0x001F;
    palette[16 + 3] = 0x03E0;

    DioramaMetatile_Compose(graphics, entries, palette, pixels);
    CHECK(pixels[0] == 0xFF00FF00u);
    CHECK(pixels[1] == 0xFFFF0000u);
    CHECK(pixels[7 * DIORAMA_METATILE_SIZE + 7] == 0xFFFF0000u);
    CHECK(pixels[8] == 0);
    DioramaMetatile_ComposeLayer(graphics, entries, palette, 0, layerPixels);
    CHECK(layerPixels[0] == 0xFFFF0000u);
    CHECK(layerPixels[1] == 0xFFFF0000u);
    DioramaMetatile_ComposeLayer(graphics, entries, palette, 1, layerPixels);
    CHECK(layerPixels[0] == 0xFF00FF00u);
    CHECK(layerPixels[1] == 0);
}

static void TestAlphaMask(void)
{
    uint32_t pixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE] = {0};
    uint16_t rows[DIORAMA_METATILE_SIZE];

    pixels[10 * DIORAMA_METATILE_SIZE + 0] = 0xFF000000;
    pixels[10 * DIORAMA_METATILE_SIZE + 15] = 0xFFFFFFFF;
    pixels[15 * DIORAMA_METATILE_SIZE + 4] = 0xFF123456;
    DioramaMetatile_BuildAlphaMask(pixels, rows);
    CHECK(rows[9] == 0);
    CHECK(rows[10] == 0x8001);
    CHECK(rows[15] == 0x0010);
}

static void TestOpaqueSilhouetteMask(void)
{
    uint32_t pixels[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint16_t rows[DIORAMA_METATILE_SIZE];
    int x;
    int y;

    for (y = 0; y < DIORAMA_METATILE_SIZE; y++)
        for (x = 0; x < DIORAMA_METATILE_SIZE; x++)
            pixels[y * DIORAMA_METATILE_SIZE + x] = 0xFF00AA88;
    for (y = 3; y < 15; y++)
        for (x = 2 + (y < 5); x < 14 - (y < 5); x++)
            pixels[y * DIORAMA_METATILE_SIZE + x] = 0xFF228844;

    DioramaMetatile_BuildSilhouetteMask(pixels, rows);
    CHECK(rows[0] == 0);
    CHECK(rows[3] == 0x1FF8);
    CHECK(rows[5] == 0x3FFC);
    CHECK(rows[15] == 0);
}

static void TestAtlasGuttersAndUv(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    struct DioramaAtlasUv uv;
    int originX;
    int originY;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.visibleCellCount = 2;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[1].metatileId = 33;
    snapshot.cells[0].tileEntries[0] = 1;
    snapshot.cells[0].tileEntries[1] = 1;
    snapshot.cells[0].tileEntries[2] = 1;
    snapshot.cells[0].tileEntries[3] = 1;
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    snapshot.fadedPalette[1] = 0x7FFF;

    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, NULL, atlas, baseAtlas, foregroundAtlas, present));
    CHECK(!DioramaAtlas_Update(&snapshot, NULL, atlas, baseAtlas, foregroundAtlas, present));
    CHECK((present[33 / 8] & (1 << (33 % 8))) != 0);

    originX = (33 % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (33 / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(atlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(atlas[(originY - 1) * DIORAMA_ATLAS_WIDTH + originX - 1] == 0xFFFFFFFFu);
    CHECK(atlas[(originY + 16) * DIORAMA_ATLAS_WIDTH + originX + 16] == 0xFFFFFFFFu);
    CHECK(baseAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0);

    uv = DioramaAtlas_GetUv(33);
    CHECK(uv.u0 > (float)(originX - 1) / DIORAMA_ATLAS_WIDTH);
    CHECK(uv.v0 > (float)(originY - 1) / DIORAMA_ATLAS_HEIGHT);
    CHECK(uv.u1 < (float)(originX + 16) / DIORAMA_ATLAS_WIDTH);
    CHECK(uv.v1 < (float)(originY + 16) / DIORAMA_ATLAS_HEIGHT);
}

static void TestCutoutDifference(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint16_t cutoutBases[DIORAMA_ATLAS_SLOT_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    int originX;
    int originY;
    int tile;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(cutoutBases, 0xFF, sizeof(cutoutBases));
    cutoutBases[33] = 1;
    snapshot.visibleCellCount = 2;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[1].metatileId = 1;
    for (tile = 0; tile < 4; tile++)
    {
        snapshot.cells[0].tileEntries[tile] = 1;
        snapshot.cells[1].tileEntries[tile] = 2;
    }
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 2 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    SetPixel(snapshot.tileGraphics, 1, 0, 0, 2);
    snapshot.fadedPalette[1] = 0x7FFF;
    snapshot.fadedPalette[2] = 0x001F;

    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, cutoutBases, atlas, baseAtlas,
                              foregroundAtlas, present));
    originX = (33 % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (33 / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(baseAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFFFFFFu);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFF0000u);
    CHECK(foregroundAtlas[originY * DIORAMA_ATLAS_WIDTH + originX + 1] == 0);
}

static void TestDirtyTileUpdates(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    uint16_t cutoutBases[DIORAMA_ATLAS_SLOT_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    uint8_t dirty[DIORAMA_ATLAS_DIRTY_TILE_BYTES];
    uint8_t forced[DIORAMA_ATLAS_PRESENT_BYTES];
    uint8_t updated[DIORAMA_ATLAS_PRESENT_BYTES];

    memset(&snapshot, 0, sizeof(snapshot));
    memset(cutoutBases, 0xFF, sizeof(cutoutBases));
    memset(dirty, 0, sizeof(dirty));
    memset(forced, 0, sizeof(forced));
    memset(updated, 0, sizeof(updated));
    snapshot.visibleCellCount = 3;
    snapshot.cells[0].metatileId = 33;
    snapshot.cells[0].tileEntries[0] = 1 | (1 << 10) | (2 << 12);
    snapshot.cells[1].metatileId = 34;
    snapshot.cells[1].tileEntries[0] = 2;
    snapshot.cells[2].metatileId = 35;
    snapshot.cells[2].tileEntries[0] = 3;
    cutoutBases[35] = 34;
    snapshot.fadedPalette[1] = 0x001F;
    snapshot.fadedPalette[2 * 16 + 1] = 0x03E0;
    memset(snapshot.tileGraphics + DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 2 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 3 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, cutoutBases, atlas, baseAtlas,
                               foregroundAtlas, present));

    dirty[1 / 8] |= 1 << (1 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, NULL,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[33 / 8] & (1 << (33 % 8))) != 0);
    CHECK((updated[34 / 8] & (1 << (34 % 8))) == 0);
    CHECK((updated[35 / 8] & (1 << (35 % 8))) == 0);

    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    dirty[2 / 8] |= 1 << (2 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, NULL,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[34 / 8] & (1 << (34 % 8))) != 0);
    CHECK((updated[35 / 8] & (1 << (35 % 8))) != 0);

    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    forced[33 / 8] |= 1 << (33 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, dirty, forced,
                                   atlas, baseAtlas, foregroundAtlas, present, updated));
    CHECK((updated[33 / 8] & (1 << (33 % 8))) != 0);
}


static void TestSlots(void)
{
    struct DioramaCellSnapshot cell;

    CHECK(DIORAMA_ATLAS_SLOT_COUNT == 1024 + DIORAMA_NEIGHBOUR_PAGES * 512);
    CHECK(DIORAMA_ATLAS_ROWS * DIORAMA_ATLAS_COLUMNS == DIORAMA_ATLAS_SLOT_COUNT);
    /* The map's own metatiles are their slots; a page shares the primary's. */
    CHECK(DioramaAtlas_Slot(0, 0) == 0);
    CHECK(DioramaAtlas_Slot(0, 513) == 513);
    CHECK(DioramaAtlas_Slot(0, 1023) == 1023);
    CHECK(DioramaAtlas_Slot(1, 1) == 1);
    CHECK(DioramaAtlas_Slot(2, 511) == 511);
    CHECK(DioramaAtlas_Slot(1, 512) == 1024);
    CHECK(DioramaAtlas_Slot(1, 1023) == 1535);
    CHECK(DioramaAtlas_Slot(2, 512) == 1536);
    CHECK(DioramaAtlas_Slot(DIORAMA_NEIGHBOUR_PAGES, 1023) == DIORAMA_ATLAS_SLOT_COUNT - 1);
    CHECK(DioramaAtlas_Slot(0, 1024) == DIORAMA_ATLAS_NO_SLOT);
    CHECK(DioramaAtlas_Slot(1, 0xFFFF) == DIORAMA_ATLAS_NO_SLOT);
    CHECK(DioramaAtlas_Slot(DIORAMA_NEIGHBOUR_PAGES + 1, 600) == DIORAMA_ATLAS_NO_SLOT);
    memset(&cell, 0, sizeof(cell));
    cell.metatileId = 600;
    cell.atlasPage = 2;
    CHECK(DioramaAtlas_CellSlot(&cell) == 1024 + 512 + 88);
}

static uint16_t Rgb(int r, int g, int b)
{
    return (uint16_t)(r | (g << 5) | (b << 10));
}

/* Live palettes 0-12 with colours spread over 0-31, faded by `fade`. */
static void MakeLivePalettes(uint16_t *unfaded, uint16_t *faded, int (*fade)(int channel, int value))
{
    int palette;
    int color;

    memset(unfaded, 0, DIORAMA_FADED_PALETTE_ENTRIES * sizeof(*unfaded));
    memset(faded, 0, DIORAMA_FADED_PALETTE_ENTRIES * sizeof(*faded));
    for (palette = 0; palette < 13; palette++)
        for (color = 1; color < 16; color++)
        {
            int r = (palette * 7 + color * 3) % 32;
            int g = (palette * 5 + color * 11) % 32;
            int b = (palette * 13 + color * 2) % 32;

            unfaded[palette * 16 + color] = Rgb(r, g, b);
            faded[palette * 16 + color] = Rgb(fade(0, r), fade(1, g), fade(2, b));
        }
}

static int Same(int channel, int value) { (void)channel; return value; }
static int Black(int channel, int value) { (void)channel; (void)value; return 0; }
/* A night tint: each channel scaled differently (TimeMixPalettes' tint). */
static int Night(int channel, int value)
{
    static const int scales[3] = {150, 170, 230};

    return value * scales[channel] >> 8;
}
/* Half way to white (a fade, BlendPalettes). */
static int HalfWhite(int channel, int value) { (void)channel; return value + ((31 - value) >> 1); }
/* A contrast colour map (field_weather.c's): not a line. */
static int Contrast(int channel, int value)
{
    (void)channel;
    return value < 8 ? 0 : (value > 23 ? 31 : (value - 8) * 31 / 15);
}

static void CheckMapped(int (*fade)(int channel, int value), int tolerance)
{
    static uint16_t unfaded[DIORAMA_FADED_PALETTE_ENTRIES];
    static uint16_t faded[DIORAMA_FADED_PALETTE_ENTRIES];
    uint16_t source[32];
    uint16_t out[32];
    int i;

    MakeLivePalettes(unfaded, faded, fade);
    for (i = 0; i < 32; i++)
        source[i] = Rgb(i, 31 - i, (i * 9) % 32);
    DioramaAtlas_MapPalette(unfaded, faded, source, out, 32);
    for (i = 0; i < 32; i++)
    {
        int channel;

        for (channel = 0; channel < 3; channel++)
        {
            int in = (source[i] >> (channel * 5)) & 31;
            int got = (out[i] >> (channel * 5)) & 31;
            int want = fade(channel, in);

            CHECK(got - want <= tolerance && want - got <= tolerance);
        }
        CHECK((out[i] & 0x8000) == 0);
    }
}

static void TestMapPalette(void)
{
    static uint16_t unfaded[DIORAMA_FADED_PALETTE_ENTRIES];
    static uint16_t faded[DIORAMA_FADED_PALETTE_ENTRIES];
    uint16_t source[2];
    uint16_t out[2];

    CheckMapped(Same, 0);
    CheckMapped(Black, 0);
    CheckMapped(Night, 0);
    CheckMapped(HalfWhite, 0);
    CheckMapped(Contrast, 0);
    /* A light colour (the high bit) is faded by its own rule: it does not
     * teach the mapping. */
    MakeLivePalettes(unfaded, faded, Same);
    unfaded[3 * 16 + 4] = Rgb(31, 31, 31) | 0x8000;
    faded[3 * 16 + 4] = Rgb(0, 0, 0);
    source[0] = Rgb(31, 31, 31);
    source[1] = Rgb(31, 31, 31) | 0x8000;
    DioramaAtlas_MapPalette(unfaded, faded, source, out, 2);
    CHECK(out[0] == Rgb(31, 31, 31));
    CHECK(out[1] == Rgb(31, 31, 31));
    /* Values the live palettes do not have are interpolated (here every
     * odd value, and above 24 extrapolated along the line). */
    {
        int palette;
        int color;
        int value;

        memset(unfaded, 0, sizeof(unfaded));
        memset(faded, 0, sizeof(faded));
        for (palette = 0; palette < 13; palette++)
            for (color = 1; color < 16; color++)
            {
                int v = ((palette + color) % 13) * 2;

                unfaded[palette * 16 + color] = Rgb(v, v, v);
                faded[palette * 16 + color] = Rgb(Night(0, v), Night(1, v), Night(2, v));
            }
        for (value = 0; value < 32; value++)
        {
            int channel;

            source[0] = Rgb(value, value, value);
            DioramaAtlas_MapPalette(unfaded, faded, source, out, 1);
            for (channel = 0; channel < 3; channel++)
            {
                int got = (out[0] >> (channel * 5)) & 31;
                int want = Night(channel, value);

                CHECK(got - want <= 1 && want - got <= 1);
            }
        }
    }
    /* No colours to learn from: unchanged. */
    memset(unfaded, 0, sizeof(unfaded));
    memset(faded, 0, sizeof(faded));
    source[0] = Rgb(3, 17, 29);
    DioramaAtlas_MapPalette(unfaded, faded, source, out, 1);
    CHECK(out[0] == Rgb(3, 17, 29));
}

static void TestNeighbourPages(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint8_t pageTiles[DIORAMA_PAGE_TILE_BYTES];
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static const int sTileset = 0;
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    uint8_t dirty[DIORAMA_ATLAS_DIRTY_TILE_BYTES];
    uint8_t updated[DIORAMA_ATLAS_PRESENT_BYTES];
    struct DioramaAtlasArt art;
    uint32_t signature;
    uint16_t pageSlot = DioramaAtlas_Slot(2, 600);
    int originX;
    int originY;
    int i;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(pageTiles, 0, sizeof(pageTiles));
    /* The map's own metatile 600 (its tile 520, palette 6 colour 1: red) and
     * the neighbour's (page 2: its own tile 520, palette 6 colour 1: blue),
     * and a primary metatile the page shares (tile 1, palette 1). */
    snapshot.visibleCellCount = 3;
    snapshot.cells[0].metatileId = 600;
    snapshot.cells[1].metatileId = 600;
    snapshot.cells[1].atlasPage = 2;
    snapshot.cells[2].metatileId = 5;
    snapshot.cells[2].atlasPage = 2;
    for (i = 0; i < 4; i++)
    {
        snapshot.cells[0].tileEntries[i] = 520 | (6 << 12);
        snapshot.cells[1].tileEntries[i] = 520 | (6 << 12);
        snapshot.cells[2].tileEntries[i] = 1 | (1 << 12);
    }
    memset(snapshot.tileGraphics + 520 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(snapshot.tileGraphics + 1 * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    memset(pageTiles + (520 - DIORAMA_PAGE_FIRST_TILE) * DIORAMA_TILE_BYTES, 0x11, DIORAMA_TILE_BYTES);
    MakeLivePalettes(snapshot.unfadedPalette, snapshot.fadedPalette, Night);
    snapshot.unfadedPalette[6 * 16 + 1] = snapshot.fadedPalette[6 * 16 + 1] = Rgb(31, 0, 0);
    snapshot.unfadedPalette[1 * 16 + 1] = snapshot.fadedPalette[1 * 16 + 1] = Rgb(0, 31, 0);
    snapshot.neighbourPages[1].tileset = &sTileset;
    snapshot.neighbourPages[1].tiles = pageTiles;
    snapshot.neighbourPages[1].palettes[1] = Rgb(0, 0, 31); /* palette 6, colour 1 */

    signature = DioramaAtlas_SetPages(&snapshot);
    CHECK(DioramaAtlas_HighestPage() == 2);
    art = DioramaAtlas_CellArt(&snapshot, &snapshot.cells[0]);
    CHECK(art.tileGraphics == snapshot.tileGraphics && art.fadedPalette == snapshot.fadedPalette);
    art = DioramaAtlas_PageArt(&snapshot, 1); /* not in use: the map's own */
    CHECK(art.tileGraphics == snapshot.tileGraphics);
    art = DioramaAtlas_CellArt(&snapshot, &snapshot.cells[1]);
    CHECK(art.tileGraphics != snapshot.tileGraphics);
    CHECK(memcmp(art.tileGraphics, snapshot.tileGraphics, DIORAMA_PAGE_FIRST_TILE * DIORAMA_TILE_BYTES) == 0);
    CHECK(memcmp(art.tileGraphics + DIORAMA_PAGE_FIRST_TILE * DIORAMA_TILE_BYTES, pageTiles,
                 DIORAMA_PAGE_TILE_BYTES) == 0);
    CHECK(art.unfadedPalette[6 * 16 + 1] == Rgb(0, 0, 31));
    /* faded as the live palettes are (the night tint of this test) */
    CHECK(art.fadedPalette[6 * 16 + 1] == Rgb(0, 0, Night(2, 31)));
    CHECK(art.fadedPalette[1 * 16 + 1] == snapshot.fadedPalette[1 * 16 + 1]);
    CHECK(art.fadedPalette[13 * 16 + 1] == snapshot.fadedPalette[13 * 16 + 1]);
    /* the same pages give the same signature; another tileset another */
    CHECK(DioramaAtlas_SetPages(&snapshot) == signature);
    snapshot.neighbourPages[1].tileset = &sTileset + 1;
    CHECK(DioramaAtlas_SetPages(&snapshot) != signature);
    snapshot.neighbourPages[1].tileset = &sTileset;
    CHECK(DioramaAtlas_SetPages(&snapshot) == signature);

    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_Update(&snapshot, NULL, atlas, baseAtlas, foregroundAtlas, present));
    CHECK((present[600 / 8] & (1 << (600 % 8))) != 0);
    CHECK((present[pageSlot / 8] & (1 << (pageSlot % 8))) != 0);
    CHECK((present[5 / 8] & (1 << (5 % 8))) != 0);
    originX = (600 % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (600 / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(atlas[originY * DIORAMA_ATLAS_WIDTH + originX] == 0xFFFF0000u);
    originX = (pageSlot % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    originY = (pageSlot / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    CHECK(atlas[originY * DIORAMA_ATLAS_WIDTH + originX]
          == DioramaMetatile_ConvertColor(Rgb(0, 0, Night(2, 31)), false));
    CHECK(DioramaAtlas_GetUv(pageSlot).v0 > (float)(originY - 1) / DIORAMA_ATLAS_HEIGHT);

    /* An animated tile of the map's: tile 520 is the page's own there, so
     * only the map's cell redraws; tile 1 is shared. */
    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    dirty[520 / 8] |= 1 << (520 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, NULL, dirty, NULL, atlas, baseAtlas,
                                   foregroundAtlas, present, updated));
    CHECK((updated[600 / 8] & (1 << (600 % 8))) != 0);
    CHECK((updated[pageSlot / 8] & (1 << (pageSlot % 8))) == 0);
    memset(dirty, 0, sizeof(dirty));
    memset(updated, 0, sizeof(updated));
    dirty[1 / 8] |= 1 << (1 % 8);
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, NULL, dirty, NULL, atlas, baseAtlas,
                                   foregroundAtlas, present, updated));
    CHECK((updated[5 / 8] & (1 << (5 % 8))) != 0);

    /* No pages: every cell is drawn with the map's own art again. */
    memset(snapshot.neighbourPages, 0, sizeof(snapshot.neighbourPages));
    DioramaAtlas_SetPages(&snapshot);
    CHECK(DioramaAtlas_HighestPage() == 0);
    art = DioramaAtlas_CellArt(&snapshot, &snapshot.cells[1]);
    CHECK(art.tileGraphics == snapshot.tileGraphics);
}

/*
 * KNOWN_ISSUES MS-118: the atlas draws a tile's row at a time with each palette's colours converted once; what it
 * draws must stay what the per-pixel functions give (DioramaMetatile_DecodePixel, DioramaMetatile_ConvertColor).
 */
static uint32_t sRandom = 0x2545F491u;

static uint32_t Random(void)
{
    sRandom ^= sRandom << 13;
    sRandom ^= sRandom >> 17;
    sRandom ^= sRandom << 5;
    return sRandom;
}

static void RandomBytes(uint8_t *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        bytes[i] = (uint8_t)Random();
        if (Random() % 3 == 0)
            bytes[i] &= 0xF0; /* transparent pixels too */
    }
}

/* layers: bit 0 the lower, bit 1 the upper (over it). */
static void ReferenceCompose(const uint8_t *graphics, const uint16_t *entries, const uint16_t *palette,
                             unsigned layers, uint32_t *pixels)
{
    unsigned layer, tile, x, y;

    memset(pixels, 0, DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE * sizeof(*pixels));
    for (layer = 0; layer < 2; layer++)
        for (tile = 0; (layers & (1u << layer)) && tile < 4; tile++)
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                {
                    uint16_t entry = entries[layer * 4 + tile];
                    uint8_t color = DioramaMetatile_DecodePixel(graphics, entry, (uint8_t)x, (uint8_t)y);

                    if (color != 0)
                        pixels[((tile >> 1) * 8 + y) * DIORAMA_METATILE_SIZE + (tile & 1) * 8 + x]
                            = DioramaMetatile_ConvertColor(palette[(entry >> 12) * 16 + color], false);
                }
}

static void ReferenceMask(const uint32_t *pixels, uint16_t rows[DIORAMA_METATILE_SIZE])
{
    unsigned x, y;

    for (y = 0; y < DIORAMA_METATILE_SIZE; y++)
    {
        rows[y] = 0;
        for (x = 0; x < DIORAMA_METATILE_SIZE; x++)
            if (pixels[y * DIORAMA_METATILE_SIZE + x] >> 24)
                rows[y] |= (uint16_t)(1u << x);
    }
}

static void TestDrawingMatchesPerPixelReference(void)
{
    static uint8_t graphics[DIORAMA_TILE_GRAPHICS_SIZE];
    static uint16_t palette[DIORAMA_FADED_PALETTE_ENTRIES];
    uint32_t expected[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t actual[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint16_t expectedRows[DIORAMA_METATILE_SIZE], actualRows[DIORAMA_METATILE_SIZE];
    unsigned round, i;

    for (round = 0; round < 3000; round++)
    {
        uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT];
        uint8_t layer;

        if (round % 300 == 0)
        {
            RandomBytes(graphics, sizeof(graphics));
            for (i = 0; i < DIORAMA_FADED_PALETTE_ENTRIES; i++)
                palette[i] = (uint16_t)Random();
        }
        for (i = 0; i < DIORAMA_METATILE_ENTRY_COUNT; i++)
            entries[i] = (uint16_t)Random(); /* every tile, flip and palette */
        ReferenceCompose(graphics, entries, palette, 3, expected);
        memset(actual, 0xA5, sizeof(actual));
        DioramaMetatile_Compose(graphics, entries, palette, actual);
        CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
        ReferenceMask(expected, expectedRows);
        DioramaMetatile_BuildAlphaMask(actual, actualRows);
        CHECK(memcmp(expectedRows, actualRows, sizeof(expectedRows)) == 0);
        for (layer = 0; layer < 3; layer++)
        {
            ReferenceCompose(graphics, entries, palette, layer < 2 ? 1u << layer : 0, expected);
            memset(actual, 0x5A, sizeof(actual));
            DioramaMetatile_ComposeLayer(graphics, entries, palette, layer, actual);
            CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
        }
    }
}

/* The slot's box in an atlas against a metatile's pixels, the gutter repeating its edge. */
static bool BoxMatches(const uint32_t *atlas, uint16_t slot, const uint32_t *pixels)
{
    int originX = (slot % DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int originY = (slot / DIORAMA_ATLAS_COLUMNS) * DIORAMA_ATLAS_STRIDE + DIORAMA_ATLAS_GUTTER;
    int x, y;

    for (y = -DIORAMA_ATLAS_GUTTER; y < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; y++)
        for (x = -DIORAMA_ATLAS_GUTTER; x < DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER; x++)
        {
            int sourceX = x < 0 ? 0 : x >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : x;
            int sourceY = y < 0 ? 0 : y >= DIORAMA_METATILE_SIZE ? DIORAMA_METATILE_SIZE - 1 : y;

            if (atlas[(originY + y) * DIORAMA_ATLAS_WIDTH + originX + x]
                != pixels[sourceY * DIORAMA_METATILE_SIZE + sourceX])
                return false;
        }
    return true;
}

static void TestPassMatchesPerPixelReference(void)
{
    static struct DioramaSceneSnapshot snapshot;
    static uint8_t pageTiles[DIORAMA_PAGE_TILE_BYTES];
    static uint32_t atlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t baseAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint32_t foregroundAtlas[DIORAMA_ATLAS_PIXEL_COUNT];
    static uint16_t cutoutBases[DIORAMA_ATLAS_SLOT_COUNT];
    static uint16_t firstCell[DIORAMA_ATLAS_SLOT_COUNT];
    uint8_t present[DIORAMA_ATLAS_PRESENT_BYTES];
    uint32_t whole[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t lower[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint32_t upper[DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE];
    uint16_t expectedRows[DIORAMA_METATILE_SIZE], actualRows[DIORAMA_METATILE_SIZE];
    unsigned i, e, checked = 0, cutouts = 0;

    memset(&snapshot, 0, sizeof(snapshot));
    RandomBytes(snapshot.tileGraphics, sizeof(snapshot.tileGraphics));
    RandomBytes(pageTiles, sizeof(pageTiles));
    for (i = 0; i < DIORAMA_FADED_PALETTE_ENTRIES; i++)
    {
        snapshot.unfadedPalette[i] = (uint16_t)(Random() & 0x7FFF);
        snapshot.fadedPalette[i] = (uint16_t)(Random() & 0x7FFF);
    }
    snapshot.neighbourPages[0].tileset = pageTiles; /* its identity only */
    snapshot.neighbourPages[0].tiles = pageTiles;
    for (i = 0; i < DIORAMA_PAGE_PALETTE_COUNT * 16; i++)
        snapshot.neighbourPages[0].palettes[i] = (uint16_t)(Random() & 0x7FFF);
    snapshot.visibleCellCount = 600;
    for (i = 0; i < snapshot.visibleCellCount; i++)
    {
        struct DioramaCellSnapshot *cell = &snapshot.cells[i];

        cell->atlasPage = (uint8_t)(Random() % 2);
        cell->metatileId = (uint16_t)(Random() % 160 + (cell->atlasPage ? 480 : 0));
        for (e = 0; e < DIORAMA_METATILE_ENTRY_COUNT; e++) /* a slot's cells draw the same, as in the game */
            cell->tileEntries[e] = (uint16_t)((cell->metatileId * 2654435761u + cell->atlasPage * 40503u + e * 977u) >> 7);
    }
    snapshot.rockFaceValid = 1;
    for (e = 0; e < DIORAMA_METATILE_ENTRY_COUNT; e++)
        snapshot.rockFaceEntries[e] = (uint16_t)Random();
    for (i = 0; i < DIORAMA_ATLAS_SLOT_COUNT; i++)
        cutoutBases[i] = firstCell[i] = 0xFFFF;
    for (i = 0; i < snapshot.visibleCellCount; i++)
    {
        uint16_t slot = DioramaAtlas_CellSlot(&snapshot.cells[i]);

        if (firstCell[slot] == 0xFFFF)
            firstCell[slot] = (uint16_t)i;
        if (Random() % 4 == 0)
            cutoutBases[slot] = DioramaAtlas_CellSlot(&snapshot.cells[Random() % snapshot.visibleCellCount]);
    }
    DioramaAtlas_SetPages(&snapshot);
    DioramaAtlas_Clear(atlas, present);
    memset(baseAtlas, 0, sizeof(baseAtlas));
    memset(foregroundAtlas, 0, sizeof(foregroundAtlas));
    CHECK(DioramaAtlas_UpdateDirty(&snapshot, cutoutBases, NULL, NULL, atlas, baseAtlas, foregroundAtlas, present,
                                   NULL));
    for (i = 0; i < DIORAMA_ATLAS_SLOT_COUNT; i++)
    {
        const struct DioramaCellSnapshot *cell = firstCell[i] != 0xFFFF ? &snapshot.cells[firstCell[i]] : NULL;
        const struct DioramaCellSnapshot *baseCell = NULL;
        struct DioramaCellSnapshot rock;
        struct DioramaAtlasArt art;
        unsigned pixel;

        if (cell == NULL && i == DIORAMA_ROCK_FACE_METATILE)
        {
            memset(&rock, 0, sizeof(rock));
            rock.metatileId = DIORAMA_ROCK_FACE_METATILE;
            memcpy(rock.tileEntries, snapshot.rockFaceEntries, sizeof(rock.tileEntries));
            cell = &rock;
        }
        else if (cutoutBases[i] < DIORAMA_ATLAS_SLOT_COUNT && firstCell[cutoutBases[i]] != 0xFFFF)
        {
            baseCell = &snapshot.cells[firstCell[cutoutBases[i]]];
        }
        CHECK(((present[i / 8] >> (i % 8)) & 1) == (cell != NULL));
        if (cell == NULL)
            continue;
        art = DioramaAtlas_CellArt(&snapshot, cell);
        ReferenceCompose(art.tileGraphics, cell->tileEntries, art.fadedPalette, 3, whole);
        if (baseCell != NULL)
        {
            struct DioramaAtlasArt baseArt = DioramaAtlas_CellArt(&snapshot, baseCell);

            ReferenceCompose(baseArt.tileGraphics, baseCell->tileEntries, baseArt.fadedPalette, 3, lower);
            for (pixel = 0; pixel < DIORAMA_METATILE_SIZE * DIORAMA_METATILE_SIZE; pixel++)
                upper[pixel] = whole[pixel] == lower[pixel] ? 0 : whole[pixel];
            cutouts++;
        }
        else
        {
            ReferenceCompose(art.tileGraphics, cell->tileEntries, art.fadedPalette, 1, lower);
            ReferenceCompose(art.tileGraphics, cell->tileEntries, art.fadedPalette, 2, upper);
        }
        CHECK(BoxMatches(atlas, (uint16_t)i, whole));
        CHECK(BoxMatches(baseAtlas, (uint16_t)i, lower));
        CHECK(BoxMatches(foregroundAtlas, (uint16_t)i, upper));
        ReferenceMask(upper, expectedRows);
        DioramaAtlas_GetForegroundAlphaMask((uint16_t)i, actualRows);
        CHECK(memcmp(expectedRows, actualRows, sizeof(expectedRows)) == 0);
        DioramaMetatile_BuildSilhouetteMask(whole, expectedRows);
        DioramaAtlas_GetSilhouetteAlphaMask((uint16_t)i, actualRows);
        CHECK(memcmp(expectedRows, actualRows, sizeof(expectedRows)) == 0);
        checked++;
    }
    CHECK(checked > 200 && cutouts > 20);
}

int main(void)
{
    TestDrawingMatchesPerPixelReference();
    TestPassMatchesPerPixelReference();
    TestDecodeAndFlips();
    TestColorAndComposition();
    TestAlphaMask();
    TestOpaqueSilhouetteMask();
    TestAtlasGuttersAndUv();
    TestCutoutDifference();
    TestDirtyTileUpdates();
    TestSlots();
    TestMapPalette();
    TestNeighbourPages();
    puts("metatile atlas tests passed");
    return EXIT_SUCCESS;
}
