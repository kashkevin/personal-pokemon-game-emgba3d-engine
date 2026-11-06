#ifndef GUARD_DIORAMA_METATILE_ATLAS_H
#define GUARD_DIORAMA_METATILE_ATLAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

#define DIORAMA_METATILE_SIZE 16
/*
 * The atlas's slots: the map's own metatiles 0 to 1023 (slot = metatile id),
 * then 512 for each neighbour page (scene_snapshot.h, atlasPage): a page's
 * secondary metatiles 512 to 1023, drawn with its own tiles and palettes.
 * Its primary metatiles are the map's (the same tileset, tiles and palettes),
 * so they share slots 0 to 511. DioramaAtlas_Slot gives a cell's slot; the
 * atlas functions below take slots (page 0's are the metatile ids).
 */
#define DIORAMA_PAGE_FIRST_METATILE 512
#define DIORAMA_PAGE_METATILE_COUNT (DIORAMA_TILE_COUNT - DIORAMA_PAGE_FIRST_METATILE)
#define DIORAMA_ATLAS_SLOT_COUNT (DIORAMA_TILE_COUNT + DIORAMA_NEIGHBOUR_PAGES * DIORAMA_PAGE_METATILE_COUNT)
#define DIORAMA_ATLAS_NO_SLOT 0xFFFF
#define DIORAMA_ATLAS_COLUMNS 32
#define DIORAMA_ATLAS_ROWS (DIORAMA_ATLAS_SLOT_COUNT / DIORAMA_ATLAS_COLUMNS)
/* The rows of the map's own metatiles, and of each page. */
#define DIORAMA_ATLAS_MAP_ROWS (DIORAMA_TILE_COUNT / DIORAMA_ATLAS_COLUMNS)
#define DIORAMA_ATLAS_PAGE_ROWS (DIORAMA_PAGE_METATILE_COUNT / DIORAMA_ATLAS_COLUMNS)
#define DIORAMA_ATLAS_GUTTER 1
#define DIORAMA_ATLAS_STRIDE (DIORAMA_METATILE_SIZE + DIORAMA_ATLAS_GUTTER * 2)
#define DIORAMA_ATLAS_WIDTH (DIORAMA_ATLAS_COLUMNS * DIORAMA_ATLAS_STRIDE)
#define DIORAMA_ATLAS_HEIGHT (DIORAMA_ATLAS_ROWS * DIORAMA_ATLAS_STRIDE)
#define DIORAMA_ATLAS_PIXEL_COUNT (DIORAMA_ATLAS_WIDTH * DIORAMA_ATLAS_HEIGHT)
#define DIORAMA_ATLAS_PRESENT_BYTES (DIORAMA_ATLAS_SLOT_COUNT / 8)
#define DIORAMA_ATLAS_DIRTY_TILE_BYTES (DIORAMA_TILE_COUNT / 8)

struct DioramaAtlasUv
{
    float u0;
    float v0;
    float u1;
    float v1;
};

/* The art a cell is drawn with: its tiles (all 1024), and its palettes. */
struct DioramaAtlasArt
{
    const uint8_t *tileGraphics;
    const uint16_t *fadedPalette;
    const uint16_t *unfadedPalette;
};

/*
 * The atlas's regions (KNOWN_ISSUES MS-118, 2026-11-05): the secondary
 * metatiles of a page (scene_snapshot.h, atlasPage) are drawn in a region of
 * the atlas, region 0 slots 512 to 1023 (after the shared primary metatiles
 * 0 to 511) and region r (1 on) the r-th DIORAMA_PAGE_METATILE_COUNT slots
 * after them. A tileset keeps its region while it is in view
 * (DioramaAtlas_SetPages): crossing into a neighbouring map turns the pages
 * round (page 0 is always the map the player is on) but not the regions, so
 * every cell keeps its slot, the atlas keeps its art and the chunks built from
 * them stand. gDioramaAtlasPageRegion[page] is the region of each page, set by
 * DioramaAtlas_SetPages on the render thread (identity until the first call).
 */
extern uint8_t gDioramaAtlasPageRegion[1 + DIORAMA_NEIGHBOUR_PAGES];

/* The slot of a metatile of a page (0: the map's own), DIORAMA_ATLAS_NO_SLOT
 * for no metatile. */
static inline uint16_t DioramaAtlas_Slot(uint8_t page, uint16_t metatileId)
{
    uint8_t region;

    if (metatileId >= DIORAMA_TILE_COUNT || page > DIORAMA_NEIGHBOUR_PAGES)
        return DIORAMA_ATLAS_NO_SLOT;
    if (metatileId < DIORAMA_PAGE_FIRST_METATILE)
        return metatileId;
    region = gDioramaAtlasPageRegion[page];
    if (region == 0)
        return metatileId;
    return DIORAMA_TILE_COUNT + (region - 1) * DIORAMA_PAGE_METATILE_COUNT
         + (metatileId - DIORAMA_PAGE_FIRST_METATILE);
}

/* The region (above) of a page, and the secondary tileset a region holds (NULL: none yet). */
static inline uint8_t DioramaAtlas_PageRegion(uint8_t page)
{
    return page <= DIORAMA_NEIGHBOUR_PAGES ? gDioramaAtlasPageRegion[page] : 0;
}
const void *DioramaAtlas_RegionTileset(uint8_t region);
/* False with PKMN_DIORAMA_STICKY_PAGES=0: each page's region is its own number, as before (for comparisons). */
bool DioramaAtlas_StickyRegions(void);

static inline uint16_t DioramaAtlas_CellSlot(const struct DioramaCellSnapshot *cell)
{
    return DioramaAtlas_Slot(cell->atlasPage, cell->metatileId);
}

uint8_t DioramaMetatile_DecodePixel(const uint8_t *tileGraphics, uint16_t tileEntry,
                                    uint8_t x, uint8_t y);
uint32_t DioramaMetatile_ConvertColor(uint16_t bgr555, bool transparent);
void DioramaMetatile_Compose(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                             const uint16_t *palette, uint32_t *pixels);
void DioramaMetatile_ComposeLayer(const uint8_t *tileGraphics, const uint16_t *tileEntries,
                                  const uint16_t *palette, uint8_t layer,
                                  uint32_t *pixels);
void DioramaMetatile_BuildAlphaMask(const uint32_t *pixels,
                                    uint16_t rows[DIORAMA_METATILE_SIZE]);
void DioramaMetatile_BuildSilhouetteMask(const uint32_t *pixels,
                                         uint16_t rows[DIORAMA_METATILE_SIZE]);
/*
 * The pages' art from a snapshot: each page in use gets the snapshot's tiles
 * 0 to 511 and its own 512 to 1023, the snapshot's palettes with its own 6 to
 * 12 - faded as the snapshot's are (DioramaAtlas_MapPalette). Call it with
 * each snapshot before the atlas or the cells' art are used. Returns a
 * signature of the pages' tilesets and palettes: a change means their slots'
 * art changed.
 */
uint32_t DioramaAtlas_SetPages(const struct DioramaSceneSnapshot *snapshot);
/* What the last DioramaAtlas_SetPages returned (the renderers' change checks). */
uint32_t DioramaAtlas_LastPageSignature(void);
/* The art of a page (0, or one not set: the snapshot's own), and a cell's. */
struct DioramaAtlasArt DioramaAtlas_PageArt(const struct DioramaSceneSnapshot *snapshot,
                                            uint8_t page);
struct DioramaAtlasArt DioramaAtlas_CellArt(const struct DioramaSceneSnapshot *snapshot,
                                            const struct DioramaCellSnapshot *cell);
/* The highest region (above) of a page set by DioramaAtlas_SetPages (0: none but the map's own): the atlas rows in
 * use are the map's and those of regions 1 to this one. */
uint8_t DioramaAtlas_HighestPage(void);
/*
 * A signature of the art every region in use is drawn with this snapshot (DioramaAtlas_SetPages): its tileset and
 * the faded palettes its cells are drawn with, and the live palettes the pages share. The same signature means the
 * atlas's slots hold what they would be drawn with now, though the map changed (a crossing).
 */
uint32_t DioramaAtlas_DrawnSignature(void);

/*
 * The drawn signature's parts (KNOWN_ISSUES MS-82, 2026-11-06): what every region shares (the primary tileset the
 * regions were given under and the live palettes 0 to 5), and each region's own (its tileset and the faded palettes
 * 6 to 12 its cells are drawn with; 0 for a region no page uses in this snapshot). The same shared part means only a
 * region whose own part changed holds art its cells are no longer drawn with (DioramaAtlas_ClearRegion).
 */
uint32_t DioramaAtlas_SharedSignature(void);
uint32_t DioramaAtlas_RegionSignature(uint8_t region);

/* A region's first slot (DIORAMA_PAGE_METATILE_COUNT slots from it, whole rows of the atlas), and a slot's region
 * (DIORAMA_ATLAS_NO_REGION: a primary metatile's slot, which every page shares). */
#define DIORAMA_ATLAS_NO_REGION 0xFF

static inline uint16_t DioramaAtlas_RegionFirstSlot(uint8_t region)
{
    return region == 0 ? DIORAMA_PAGE_FIRST_METATILE
                       : (uint16_t)(DIORAMA_TILE_COUNT + (region - 1) * DIORAMA_PAGE_METATILE_COUNT);
}

static inline uint8_t DioramaAtlas_SlotRegion(uint16_t slot)
{
    if (slot < DIORAMA_PAGE_FIRST_METATILE || slot >= DIORAMA_ATLAS_SLOT_COUNT)
        return DIORAMA_ATLAS_NO_REGION;
    if (slot < DIORAMA_TILE_COUNT)
        return 0;
    return (uint8_t)(1 + (slot - DIORAMA_TILE_COUNT) / DIORAMA_PAGE_METATILE_COUNT);
}
/*
 * Fades colours as a palette was faded: every step the game takes from an
 * unfaded palette to the faded one (the day/night tint, a weather's colour
 * map, a screen fade) acts on each channel alone, so each channel's mapping
 * is read off the live palettes' BG colours (0 to 12, light colours left
 * out) and applied to `count` colours of `source`. Values the palettes do
 * not have are interpolated (and extrapolated along their best line).
 */
void DioramaAtlas_MapPalette(const uint16_t *liveUnfaded, const uint16_t *liveFaded,
                             const uint16_t *source, uint16_t *out, unsigned count);
void DioramaAtlas_Clear(uint32_t *atlasPixels, uint8_t *presentSlots);
/*
 * DioramaAtlas_Clear for the first pixelRows rows of the atlas only (the slots' present bits and the masks all
 * cleared): the rows of the map and of the pages in use, all a redraw uploads (KNOWN_ISSUES MS-118). A slot is
 * drawn whole into its own box, from the snapshot's art, so the rows below are neither read nor uploaded until a
 * redraw with more pages in use clears them.
 */
void DioramaAtlas_ClearRows(uint32_t *atlasPixels, uint8_t *presentSlots, unsigned pixelRows);
/* The same for one region's slots (DioramaAtlas_RegionFirstSlot): its rows of the atlas, their present bits and
 * masks; every other slot is left as it is (KNOWN_ISSUES MS-82). */
void DioramaAtlas_ClearRegion(uint32_t *atlasPixels, uint8_t *presentSlots, uint8_t region);
/*
 * Draws the slots of the snapshot's cells into the three atlases (whole,
 * lower layer, upper layer), those not present yet or using a dirty tile (of
 * the snapshot's tiles; a page's own never change) or forced. Indexed by
 * slot (DIORAMA_ATLAS_SLOT_COUNT): cutoutBaseSlots (the slot whose drawing is
 * a slot's lower layer, >= DIORAMA_ATLAS_SLOT_COUNT for its own), the
 * present, forced and updated bits.
 */
bool DioramaAtlas_Update(const struct DioramaSceneSnapshot *snapshot,
                         const uint16_t *cutoutBaseSlots,
                         uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                         uint32_t *foregroundAtlasPixels, uint8_t *presentSlots);
bool DioramaAtlas_UpdateDirty(const struct DioramaSceneSnapshot *snapshot,
                              const uint16_t *cutoutBaseSlots,
                              const uint8_t *dirtyTiles,
                              const uint8_t *forcedSlots,
                              uint32_t *atlasPixels, uint32_t *baseAtlasPixels,
                              uint32_t *foregroundAtlasPixels, uint8_t *presentSlots,
                              uint8_t *updatedSlots);
struct DioramaAtlasUv DioramaAtlas_GetUv(uint16_t slot);
void DioramaAtlas_GetForegroundAlphaMask(uint16_t slot,
                                         uint16_t rows[DIORAMA_METATILE_SIZE]);
void DioramaAtlas_GetSilhouetteAlphaMask(uint16_t slot,
                                         uint16_t rows[DIORAMA_METATILE_SIZE]);

#endif
