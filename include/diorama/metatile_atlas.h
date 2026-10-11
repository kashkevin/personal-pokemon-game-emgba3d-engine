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

/* The slot of a metatile of a page (0: the map's own), DIORAMA_ATLAS_NO_SLOT
 * for no metatile. */
static inline uint16_t DioramaAtlas_Slot(uint8_t page, uint16_t metatileId)
{
    if (metatileId >= DIORAMA_TILE_COUNT || page > DIORAMA_NEIGHBOUR_PAGES)
        return DIORAMA_ATLAS_NO_SLOT;
    if (page == 0 || metatileId < DIORAMA_PAGE_FIRST_METATILE)
        return metatileId;
    return DIORAMA_TILE_COUNT + (page - 1) * DIORAMA_PAGE_METATILE_COUNT
         + (metatileId - DIORAMA_PAGE_FIRST_METATILE);
}

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
/* The highest page set by DioramaAtlas_SetPages (0: none). */
uint8_t DioramaAtlas_HighestPage(void);
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
