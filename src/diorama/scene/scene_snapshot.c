#ifdef ENABLE_DIORAMA

#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle_pyramid.h"
#include "bg.h"
#include "decompress.h"
#include "diorama/map_connection.h"
#include "diorama/scene_snapshot.h"
#include "diorama/sprite_frame.h"
#include "diorama/battle_scene.h"
#include "diorama/tree_sprites.h"
#include "diorama/weather.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "field_door.h"
#include "field_effect_helpers.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "field_weather.h"
#include "gba/defines.h"
#include "main.h"
#include "menu.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "palette.h"
#include "scanline_effect.h"
#include "script.h"
#include "sprite.h"
#include "tilesets.h"
#include "window.h"
#include "constants/map_types.h"

#define CELL_RADIUS (DIORAMA_GRID_WIDTH / 2)

static struct DioramaSceneSnapshot sDraft;
static u16 sPreviousPalette[DIORAMA_FADED_PALETTE_ENTRIES];
static u8 sPreviousTileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
static u32 sMapGeneration;
static u32 sMapEditGeneration;
static u32 sPaletteGeneration;
static u32 sObjPaletteGeneration;
static u32 sTilesetAnimationGeneration;
static bool sHasPreviousPalette;
static bool sHasPreviousTileGraphics;
static bool sPublishedThisFrame;
static bool sMapChanged;
static s8 sPreviousMapGroup;
static s8 sPreviousMapNum;
static u16 sPreviousMapLayoutId;
static struct DioramaDirtyCell sDirtyCells[DIORAMA_MAX_DIRTY_CELLS];
static u8 sDirtyCellCount;
static bool sDirtyOverflow;
/* The tree style this frame's snapshot applies (-1 before the first): read
 * once per snapshot, since the main thread may switch it at any time (F8). */
static s8 sTreeStyle = -1;

/*
 * Neighbouring maps with other tiles (scene_snapshot.h, atlasPage). The
 * secondary tiles of each tileset paged so far, decompressed once and kept
 * for the rest of the game (the render thread reads them through the
 * snapshots, so they are never changed or freed): 16 KB each, a handful of
 * tilesets in a play session.
 */
#define MAX_PAGED_TILESETS 24

static struct
{
    const struct Tileset *tileset;
    u8 *tiles;
} sPagedTiles[MAX_PAGED_TILESETS];

/* Which tileset each page holds, kept from one snapshot to the next so that
 * a page's art in the 3D view's atlas stays put; a layout using it (for the
 * 3DS trees); whether this snapshot uses it. */
static const struct Tileset *sPageTilesets[DIORAMA_NEIGHBOUR_PAGES];
static const struct MapLayout *sPageLayouts[DIORAMA_NEIGHBOUR_PAGES];
static bool8 sPageUsed[DIORAMA_NEIGHBOUR_PAGES];

static bool LogEnabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");

        sEnabled = value != NULL && value[0] == '1';
    }
    return sEnabled;
}

/* PKMN_DIORAMA_NEIGHBOURS=0: neighbouring maps with other tiles are shown as
 * border tiles, as before. */
static bool PagesEnabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_NEIGHBOURS");

        sEnabled = value == NULL || value[0] != '0';
    }
    return sEnabled;
}

static const u8 *PagedTiles(const struct Tileset *tileset)
{
    u8 *tiles;
    u32 size;
    int i;

    for (i = 0; i < MAX_PAGED_TILESETS && sPagedTiles[i].tileset != NULL; i++)
        if (sPagedTiles[i].tileset == tileset)
            return sPagedTiles[i].tiles;
    /* Only compressed tilesets carry their size; the few uncompressed ones
     * are secret bases and the Cable Club, never a neighbour. (Not
     * IsCompressedData: it refuses 16 KB exactly, a full 512-tile set such as
     * the General tileset's.) */
    if (i == MAX_PAGED_TILESETS || tileset->tiles == NULL || !tileset->isCompressed)
        return NULL;
    size = GetDecompressedDataSize(tileset->tiles);
    if (size == 0 || size % DIORAMA_TILE_BYTES != 0 || size > 64 * 1024)
        return NULL;
    tiles = calloc(1, size > DIORAMA_PAGE_TILE_BYTES ? size : DIORAMA_PAGE_TILE_BYTES);
    if (tiles == NULL)
        return NULL;
    DecompressDataWithHeaderWram(tileset->tiles, tiles);
    sPagedTiles[i].tileset = tileset;
    sPagedTiles[i].tiles = tiles;
    return tiles;
}

/* A neighbour's palettes 6 to 12 as LoadSecondaryTilesetPalette would load
 * them, with the night palette mix UpdateAltBgPalettes gives the palettes the
 * tileset swaps (overworld.c), on a map with natural light. */
static void PagePalettes(const struct Tileset *tileset, u16 *palettes)
{
    int i;

    memcpy(palettes, tileset->palettes[DIORAMA_PAGE_FIRST_PALETTE],
           DIORAMA_PAGE_PALETTE_COUNT * 16 * sizeof(u16));
    if (!MapHasNaturalLight(gMapHeader.mapType))
        return;
    for (i = 0; i < DIORAMA_PAGE_PALETTE_COUNT; i++)
    {
        int palette = DIORAMA_PAGE_FIRST_PALETTE + i;

        if (tileset->swapPalettes & (1 << i))
            AvgPaletteWeighted((u16 *)tileset->palettes[palette],
                               (u16 *)tileset->palettes[(palette + 9) % 16],
                               palettes + i * 16, gTimeBlend.altWeight);
    }
}

/* The page (1 on) for a connected map's layout with its own secondary
 * tileset, 0 when none is free: a page keeps its tileset while it is in use,
 * so the art of its slots stays; one this snapshot has not used yet can be
 * given to another tileset. */
static u8 NeighbourPage(struct DioramaSceneSnapshot *snapshot, const struct MapLayout *layout,
                        u16 layoutId)
{
    const struct Tileset *tileset = layout->secondaryTileset;
    int page;

    for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
        if (sPageTilesets[page] == tileset)
            break;
    if (page == DIORAMA_NEIGHBOUR_PAGES)
    {
        const u8 *tiles;

        for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
            if (sPageTilesets[page] == NULL)
                break;
        if (page == DIORAMA_NEIGHBOUR_PAGES)
            for (page = 0; page < DIORAMA_NEIGHBOUR_PAGES; page++)
                if (!sPageUsed[page])
                    break;
        if (page == DIORAMA_NEIGHBOUR_PAGES || tileset->palettes == NULL
         || (tiles = PagedTiles(tileset)) == NULL)
            return 0;
        sPageTilesets[page] = tileset;
        if (LogEnabled())
            SDL_Log("Diorama: neighbour page %d holds the tiles of layout %u", page + 1, layoutId);
    }
    if (!sPageUsed[page])
    {
        struct DioramaNeighbourPage *published = &snapshot->neighbourPages[page];

        sPageUsed[page] = TRUE;
        sPageLayouts[page] = layout;
        published->tileset = tileset;
        published->tiles = PagedTiles(tileset);
        PagePalettes(tileset, published->palettes);
    }
    return page + 1;
}

static bool HasFieldUiWindow(void)
{
    u8 mapPopupWindowId = GetMapNamePopUpWindowId();
    unsigned i;

    for (i = 1; i < WINDOWS_MAX; i++)
        if (i != mapPopupWindowId && gWindows[i].window.bg != 0xFF)
            return true;
    return false;
}

/* Whether a window is on the screen: its first tile is in its background's
 * tilemap. A window stays allocated while it is cleared from the screen. */
static bool WindowShown(u8 windowId)
{
    const struct WindowTemplate *window;
    const u16 *tilemap;
    u32 index;

    if (windowId >= WINDOWS_MAX || gWindows[windowId].window.bg == 0xFF)
        return false;
    window = &gWindows[windowId].window;
    tilemap = GetBgTilemapBuffer(window->bg);
    if (tilemap == NULL || IsTileMapOutsideWram(window->bg)
     || GetBgAttribute(window->bg, BG_ATTR_TYPE) != BG_TYPE_NORMAL)
        return false;
    index = GetTileMapIndexFromCoords(window->tilemapLeft, window->tilemapTop,
                                      GetBgAttribute(window->bg, BG_ATTR_SCREENSIZE),
                                      GetBgMetricTextMode(window->bg, 1) * 0x20,
                                      GetBgMetricTextMode(window->bg, 2) * 0x20);
    return (tilemap[index] & 0x3FF) != 0;
}

/* The field's message box (window 0) is on the screen: while a message is
 * printed into it (only then is IsFieldMessageBoxHidden() false) and after,
 * while the script waits for a press; a save's prompt is printed into it
 * without the field's message task (U12). */
static bool FieldMessageBoxShown(void)
{
    return !IsFieldMessageBoxHidden() || WindowShown(0);
}

static bool FadesIn2D(void)
{
    static int sFades2D = -1;

    if (sFades2D < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_FADE_2D");
        sFades2D = value != NULL && value[0] == '1';
    }
    return sFades2D;
}

static enum DioramaSceneKind GetOverworldSceneKind(u32 *fallbackReasons)
{
    if (gMain.callback2 != CB2_Overworld)
    {
        *fallbackReasons |= DIORAMA_FALLBACK_UNSUPPORTED_SCENE;
        return DIORAMA_SCENE_TRANSITION;
    }
    /* A screen fade on the field (a warp's fade out and in) stays in 3D: the
     * scene is coloured from the faded palettes, so it fades as the 2D view
     * does, instead of cutting to the 2D view for the fade. Set
     * PKMN_DIORAMA_FADE_2D=1 for the earlier behaviour. */
    if (gPaletteFade.active && FadesIn2D())
    {
        *fallbackReasons |= DIORAMA_FALLBACK_PALETTE_FADE;
        return DIORAMA_SCENE_TRANSITION;
    }
    if (FieldMessageBoxShown())
        return DIORAMA_SCENE_DIALOGUE;
    if (GetStartMenuWindowId() != WINDOW_NONE)
        return DIORAMA_SCENE_MENU;
    if (ArePlayerFieldControlsLocked() && HasFieldUiWindow())
        return DIORAMA_SCENE_MENU;
    if (ArePlayerFieldControlsLocked())
        return DIORAMA_SCENE_OVERWORLD_SCRIPTED;
    return DIORAMA_SCENE_OVERWORLD_FREE;
}

static void AddUiRect(struct DioramaSceneSnapshot *snapshot,
                      int x, int y, int width, int height)
{
    struct DioramaUiRect *rect;

    if (snapshot->uiRectCount >= DIORAMA_MAX_UI_RECTS || width <= 0 || height <= 0)
        return;
    rect = &snapshot->uiRects[snapshot->uiRectCount++];
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
}

static void AddStandardWindowRect(struct DioramaSceneSnapshot *snapshot, u8 windowId)
{
    int left;
    int top;
    int width;
    int height;

    if (windowId == WINDOW_NONE || windowId >= WINDOWS_MAX
     || gWindows[windowId].window.bg == 0xFF)
        return;
    left = GetWindowAttribute(windowId, WINDOW_TILEMAP_LEFT);
    top = GetWindowAttribute(windowId, WINDOW_TILEMAP_TOP);
    width = GetWindowAttribute(windowId, WINDOW_WIDTH);
    height = GetWindowAttribute(windowId, WINDOW_HEIGHT);
    AddUiRect(snapshot, (left - 1) * 8, (top - 1) * 8,
              (width + 2) * 8, (height + 2) * 8);
}

static void MarkOam(u32 *mask, u8 oamOrder)
{
    if (oamOrder < 128)
        mask[oamOrder / 32] |= 1u << (oamOrder % 32);
}

static void GetOamDimensions(const struct OamData *oam, int *width, int *height)
{
    static const u8 dimensions[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
    };

    if (oam->shape >= 3)
    {
        *width = 0;
        *height = 0;
        return;
    }
    *width = dimensions[oam->shape][oam->size][0];
    *height = dimensions[oam->shape][oam->size][1];
    if (oam->affineMode == ST_OAM_AFFINE_DOUBLE)
    {
        *width *= 2;
        *height *= 2;
    }
}

static void CopyUiProfile(struct DioramaSceneSnapshot *snapshot)
{
    u8 startMenuWindowId = GetStartMenuWindowId();
    u8 mapPopupWindowId = GetMapNamePopUpWindowId();
    bool hasFieldWindow = HasFieldUiWindow();
    unsigned i;

    if (FieldMessageBoxShown())
    {
        snapshot->uiFlags |= DIORAMA_UI_DIALOGUE;
        AddUiRect(snapshot, 0, 14 * 8, DISPLAY_WIDTH, DISPLAY_HEIGHT - 14 * 8);
    }
    if (startMenuWindowId != WINDOW_NONE)
        snapshot->uiFlags |= DIORAMA_UI_START_MENU;
    if (hasFieldWindow)
        snapshot->uiFlags |= DIORAMA_UI_FIELD_WINDOW;
    if (snapshot->uiFlags
        & (DIORAMA_UI_DIALOGUE | DIORAMA_UI_START_MENU | DIORAMA_UI_FIELD_WINDOW))
    {
        for (i = 1; i < WINDOWS_MAX; i++)
            if (i != mapPopupWindowId)
                AddStandardWindowRect(snapshot, i);
    }
    if (mapPopupWindowId != WINDOW_NONE)
    {
        snapshot->uiFlags |= DIORAMA_UI_MAP_POPUP;
        AddUiRect(snapshot, 0, 0, 12 * 8, 5 * 8);
    }

    if (snapshot->uiRectCount > 0)
    {
        unsigned charBase;

        snapshot->uiBgControl = REG_BG0CNT;
        snapshot->uiBgHOffset = REG_BG0HOFS;
        snapshot->uiBgVOffset = REG_BG0VOFS;
        charBase = (snapshot->uiBgControl >> 2) & 3;
        if (charBase > 2 || (snapshot->uiBgControl & (1 << 7))
         || (snapshot->uiBgControl >> 14) != 0)
        {
            snapshot->fallbackReasons |= DIORAMA_FALLBACK_INTERNAL_ERROR;
        }
        else
        {
            memcpy(snapshot->uiBgTileGraphics,
                   (const void *)BG_CHAR_ADDR(charBase),
                   sizeof(snapshot->uiBgTileGraphics));
            memcpy(snapshot->uiBgTilemap,
                   (const void *)BG_SCREEN_ADDR((snapshot->uiBgControl >> 8) & 0x1F),
                   sizeof(snapshot->uiBgTilemap));
        }
    }

    for (i = 0; i < snapshot->objectCount; i++)
        MarkOam(snapshot->fieldOamMask, snapshot->objects[i].oamOrder);
    if (snapshot->surfBlobValid)
        MarkOam(snapshot->fieldOamMask, snapshot->surfBlob.oamOrder);

    for (i = 0; i < 128; i++)
    {
        const struct OamData *oam = &gMain.oamBuffer[i];
        int x;
        int y;
        int width;
        int height;
        unsigned rectIndex;

        if (oam->affineMode == ST_OAM_AFFINE_ERASE)
            continue;
        if (snapshot->fieldOamMask[i / 32] & (1u << (i % 32)))
            continue;
        x = oam->x >= DISPLAY_WIDTH ? oam->x - 512 : oam->x;
        y = oam->y >= DISPLAY_HEIGHT ? oam->y - 256 : oam->y;
        GetOamDimensions(oam, &width, &height);
        for (rectIndex = 0; rectIndex < snapshot->uiRectCount; rectIndex++)
        {
            if (DioramaUI_RectIntersects(&snapshot->uiRects[rectIndex],
                                         x, y, width, height))
            {
                MarkOam(snapshot->interfaceOamMask, i);
                break;
            }
        }
    }
}

static bool CopyCellVisual(const struct MapLayout *layout, u16 metatileId,
                           u8 collision, u8 elevation,
                           struct DioramaCellSnapshot *cell)
{
    const u16 *attributes;

    cell->metatileId = metatileId;
    cell->collision = collision;
    cell->elevation = elevation;
    if (metatileId < NUM_METATILES_IN_PRIMARY
     && layout->primaryTileset != NULL
     && layout->primaryTileset->metatiles != NULL
     && layout->primaryTileset->metatileAttributes != NULL)
    {
        attributes = &layout->primaryTileset->metatileAttributes[metatileId];
        memcpy(cell->tileEntries,
               layout->primaryTileset->metatiles
                   + metatileId * DIORAMA_METATILE_ENTRY_COUNT,
               sizeof(cell->tileEntries));
    }
    else if (metatileId < NUM_METATILES_TOTAL
          && layout->secondaryTileset != NULL
          && layout->secondaryTileset->metatiles != NULL
          && layout->secondaryTileset->metatileAttributes != NULL)
    {
        u16 secondaryId = metatileId - NUM_METATILES_IN_PRIMARY;

        attributes = &layout->secondaryTileset->metatileAttributes[secondaryId];
        memcpy(cell->tileEntries,
               layout->secondaryTileset->metatiles
                   + secondaryId * DIORAMA_METATILE_ENTRY_COUNT,
               sizeof(cell->tileEntries));
    }
    else
    {
        memset(cell->tileEntries, 0, sizeof(cell->tileEntries));
        return false;
    }
    cell->behavior = UNPACK_BEHAVIOR(*attributes);
    cell->layerType = UNPACK_LAYER_TYPE(*attributes);
    return true;
}

/* Whether the live map's cell (x, y) is no longer what the layout itself draws there
 * (DIORAMA_CELL_CHANGED): compared with the layout's own data, not with
 * gMapHeader.mapLayout, which a generated floor (Trainer Hill) may stand in for. */
static bool CellChanged(const struct DioramaSceneSnapshot *snapshot, int x, int y, int mapX, int mapY)
{
    const struct MapLayout *layout = GetMapLayout(snapshot->mapLayoutId);

    if (layout == NULL || layout->map == NULL || x < 0 || y < 0
     || x >= layout->width || y >= layout->height)
        return false;
    return (layout->map[y * layout->width + x] & MAPGRID_METATILE_ID_MASK)
        != MapGridGetMetatileIdAt(mapX, mapY);
}

static bool TryCopyConnectedCell(struct DioramaSceneSnapshot *snapshot,
                                 int mapX, int mapY,
                                 struct DioramaCellSnapshot *cell)
{
    const struct MapConnection *connection = GetMapConnectionAtPos(mapX, mapY);
    const struct MapHeader *connectedHeader;
    const struct MapLayout *connectedLayout;
    int16_t connectedX;
    int16_t connectedY;
    u16 entry;
    u8 page = 0;

    if (connection == NULL)
        return false;
    connectedHeader = GetMapHeaderFromConnection(connection);
    if (connectedHeader == NULL || connectedHeader->mapLayout == NULL)
        return false;
    connectedLayout = connectedHeader->mapLayout;
    if (connectedLayout->map == NULL
     || connectedLayout->primaryTileset != gMapHeader.mapLayout->primaryTileset
     || connectedLayout->isFrlg != gMapHeader.mapLayout->isFrlg
     || !DioramaConnection_MapCoordinates(connection->direction, connection->offset,
                                          mapX - MAP_OFFSET, mapY - MAP_OFFSET,
                                          snapshot->mapWidth, snapshot->mapHeight,
                                          connectedLayout->width, connectedLayout->height,
                                          &connectedX, &connectedY))
        return false;
    /* Another secondary tileset: drawn from a page of the neighbour's own
     * tiles (scene_snapshot.h), or not at all (border tiles) without one. */
    if (connectedLayout->secondaryTileset != gMapHeader.mapLayout->secondaryTileset)
    {
        if (!PagesEnabled() || connectedLayout->isFrlg || connectedLayout->secondaryTileset == NULL)
            return false;
        page = NeighbourPage(snapshot, connectedLayout, connectedHeader->mapLayoutId);
        if (page == 0)
            return false;
    }

    if (mapX >= 0 && mapY >= 0
     && mapX < gBackupMapLayout.width && mapY < gBackupMapLayout.height
     && gBackupMapLayout.map[mapY * gBackupMapLayout.width + mapX] != MAPGRID_UNDEFINED)
        entry = gBackupMapLayout.map[mapY * gBackupMapLayout.width + mapX];
    else
        entry = connectedLayout->map[connectedY * connectedLayout->width + connectedX];
    cell->sourceMapX = connectedX;
    cell->sourceMapY = connectedY;
    cell->sourceMapGroup = connection->mapGroup;
    cell->sourceMapNum = connection->mapNum;
    cell->sourceLayoutId = connectedHeader->mapLayoutId;
    cell->flags = DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CONNECTED;
    cell->atlasPage = page;
    if (CopyCellVisual(connectedLayout, UNPACK_METATILE(entry),
                       UNPACK_COLLISION(entry), UNPACK_ELEVATION(entry), cell))
        return true;
    cell->atlasPage = 0;
    cell->flags = 0;
    return false;
}

/*
 * The 3DS project's trees (tree_sprites.h): on an outdoor map of the General
 * tileset a tree's cells are published as ground (its trunk's art covers
 * them), remembering what part of the tree each was, and the cells its old
 * canopy covered get their ground back (VoxelWorld_UsesTreeSprites and the
 * classification of voxel_world.c). Connected maps share the map's tilesets
 * (TryCopyConnectedCell), the border cells too, but for those on a page: they
 * share its primary (General) and have their own secondary.
 */
static void ApplyTreeSprites(struct DioramaSceneSnapshot *snapshot)
{
    const struct MapLayout *mapLayout = gMapHeader.mapLayout;
    int i;

    if (sTreeStyle <= 0 || mapLayout == NULL
     || mapLayout->primaryTileset != &gTileset_General
     || gMapHeader.mapType == MAP_TYPE_INDOOR || gMapHeader.mapType == MAP_TYPE_SECRET_BASE)
        return;
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        /* A neighbour's cell on a page has its own secondary tileset (the
         * primary, General, is shared). */
        const struct MapLayout *layout = cell->atlasPage != 0 ? sPageLayouts[cell->atlasPage - 1]
                                                              : mapLayout;
        bool dewford = layout->secondaryTileset == &gTileset_Dewford;
        int part = DioramaTreeSprites_Part(dewford, cell->metatileId);
        uint16_t ground;

        if (part != DIORAMA_TREE_PART_NONE)
        {
            bool secondary = cell->metatileId >= NUM_METATILES_IN_PRIMARY;

            if (!CopyCellVisual(layout, DioramaTreeSprites_TrunkGround(secondary),
                                cell->collision, cell->elevation, cell))
                continue;
            if (part == DIORAMA_TREE_PART_SMALL)
                cell->treeSprite = secondary ? DIORAMA_TREE_SPRITE_SMALL_DEWFORD
                                             : DIORAMA_TREE_SPRITE_SMALL;
            else
                cell->treeSprite = DIORAMA_TREE_SPRITE_QUARTER + part;
            continue;
        }
        ground = DioramaTreeSprites_GroundMetatile(dewford, cell->metatileId);
        if (ground != cell->metatileId)
            CopyCellVisual(layout, ground, cell->collision, cell->elevation, cell);
    }
}

static bool CopyCells(struct DioramaSceneSnapshot *snapshot)
{
    int x;
    int y;
    int index = 0;

    snapshot->gridOriginX = snapshot->cameraMapX - CELL_RADIUS;
    snapshot->gridOriginY = snapshot->cameraMapY - CELL_RADIUS;
    memset(sPageUsed, 0, sizeof(sPageUsed));
    memset(snapshot->neighbourPages, 0, sizeof(snapshot->neighbourPages));
    for (y = 0; y < DIORAMA_GRID_HEIGHT; y++)
    {
        for (x = 0; x < DIORAMA_GRID_WIDTH; x++, index++)
        {
            struct DioramaCellSnapshot *cell = &snapshot->cells[index];
            int mapX = snapshot->gridOriginX + x;
            int mapY = snapshot->gridOriginY + y;

            cell->mapX = mapX;
            cell->mapY = mapY;
            cell->flags = 0;
            cell->treeSprite = DIORAMA_TREE_SPRITE_NONE;
            cell->atlasPage = 0;
            if (mapX >= MAP_OFFSET && mapY >= MAP_OFFSET
             && mapX < MAP_OFFSET + snapshot->mapWidth
             && mapY < MAP_OFFSET + snapshot->mapHeight)
            {
                cell->sourceMapX = mapX - MAP_OFFSET;
                cell->sourceMapY = mapY - MAP_OFFSET;
                cell->sourceMapGroup = snapshot->mapGroup;
                cell->sourceMapNum = snapshot->mapNum;
                cell->sourceLayoutId = snapshot->mapLayoutId;
                cell->flags = DIORAMA_CELL_SOURCE_VALID;
                if (CellChanged(snapshot, cell->sourceMapX, cell->sourceMapY, mapX, mapY))
                    cell->flags |= DIORAMA_CELL_CHANGED;
            }
            else if (TryCopyConnectedCell(snapshot, mapX, mapY, cell))
                continue;
            else if (GetMapConnectionAtPos(mapX, mapY) == NULL)
            {
                cell->sourceMapGroup = snapshot->mapGroup;
                cell->sourceMapNum = snapshot->mapNum;
                cell->sourceLayoutId = snapshot->mapLayoutId;
                cell->flags = DIORAMA_CELL_BORDER;
            }
            if (!CopyCellVisual(gMapHeader.mapLayout, MapGridGetMetatileIdAt(mapX, mapY),
                                MapGridGetCollisionAt(mapX, mapY),
                                MapGridGetElevationAt(mapX, mapY), cell))
                return false;
        }
    }
    snapshot->visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    ApplyTreeSprites(snapshot);
    return true;
}

static void CopyTileGraphics(struct DioramaSceneSnapshot *snapshot)
{
    const u8 *tileGraphics = (const u8 *)BG_VRAM;

    if (!sHasPreviousTileGraphics
     || memcmp(sPreviousTileGraphics, tileGraphics, sizeof(sPreviousTileGraphics)) != 0)
    {
        memcpy(sPreviousTileGraphics, tileGraphics, sizeof(sPreviousTileGraphics));
        sTilesetAnimationGeneration++;
        sHasPreviousTileGraphics = true;
    }
    memcpy(snapshot->tileGraphics, tileGraphics, sizeof(snapshot->tileGraphics));
    snapshot->tilesetAnimationGeneration = sTilesetAnimationGeneration;
}

static u64 HashFrame(const u8 *data, u16 size)
{
    u64 hash = UINT64_C(1469598103934665603);
    u16 i;

    for (i = 0; i < size; i++)
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    return hash;
}

// The door standing open (field_door.c); PKMN_DIORAMA_LOG=1 logs each change.
/*
 * The dark of a cave that needs Flash, and of the Battle Pyramid (the 3DS
 * project's 3ds_game_full.c CaptureFieldLight and 3ds_video.c
 * CtrVideo_SetFieldLight). The field writes a circle of window 0 edges round
 * the player line by line, with everything outside it hidden but the text
 * layer; its radius is read back from the line through its centre, where
 * the game writes exactly centre -/+ radius. Kept here, once a frame on the
 * game thread: the radius shown eases towards the game's (which grows a
 * pixel every other frame while Flash is used), and Flash's glow fades.
 */
static struct
{
    bool8 on;
    s16 radius;
    float shown, burst;
    u32 ticks;
} sFieldLight = {FALSE, -1, 0.0f, 0.0f, 0};

static bool8 ReadFieldLight(int *radius)
{
    const struct ScanlineEffect *effect = &gScanlineEffect;
    unsigned across;
    unsigned left;

    if (effect->state == 0 || effect->state == 3 || effect->dmaDest != &REG_WIN0H
     || ((effect->dmaControl >> 16) & DMA_32BIT)
     || gSaveBlock1Ptr == NULL || gMapHeader.mapLayout == NULL
     || (gSaveBlock1Ptr->flashLevel == 0 && !InBattlePyramid()))
        return FALSE;
    /* Both buffers hold the same circle once the radius settles; the one the
     * DMA reads this frame is the one the last VBlank armed. */
    across = gScanlineEffectRegBuffers[effect->srcBuffer ^ 1][DISPLAY_HEIGHT / 2];
    left = across >> 8;
    if ((across & 0xFF) == 0 && left == 0)
        return FALSE;
    *radius = left > 0 ? DISPLAY_WIDTH / 2 - (int)left : DISPLAY_WIDTH / 2;
    return TRUE;
}

static void CopyFieldLight(struct DioramaSceneSnapshot *snapshot)
{
    int radius;
    bool8 on = ReadFieldLight(&radius);

    if (on && (!sFieldLight.on || sFieldLight.radius < 0))
        sFieldLight.shown = (float)radius;
    else if (on && radius > sFieldLight.radius && sFieldLight.shown < radius - 6.0f)
        sFieldLight.burst = 1.0f;
    sFieldLight.on = on;
    if (!on)
    {
        sFieldLight.burst = 0.0f;
        return;
    }
    sFieldLight.radius = radius;
    sFieldLight.ticks++;
    sFieldLight.shown += ((float)radius - sFieldLight.shown) * 0.25f;
    if (radius > sFieldLight.shown - 0.5f && radius < sFieldLight.shown + 0.5f)
        sFieldLight.shown = (float)radius;
    sFieldLight.burst *= 0.955f;
    snapshot->fieldLightOn = TRUE;
    snapshot->fieldLightRadius = (u16)radius;
    snapshot->fieldLightShown = sFieldLight.shown;
    snapshot->fieldLightBurst = sFieldLight.burst;
    snapshot->fieldLightTicks = sFieldLight.ticks;
}

static void CopyDoor(struct DioramaSceneSnapshot *snapshot)
{
    static int sLogEnabled = -1;
    static uint8_t sLastLevel;
    s16 doorX, doorY;
    u8 doorWidth, doorLevel;

    if (DioramaDoor_Get(&doorX, &doorY, &doorWidth, &doorLevel))
    {
        snapshot->doorMapX = doorX;
        snapshot->doorMapY = doorY;
        snapshot->doorWidth = doorWidth;
        snapshot->doorLevel = doorLevel;
    }
    if (sLogEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");
        sLogEnabled = value != NULL && value[0] == '1';
    }
    if (sLogEnabled && snapshot->doorLevel != sLastLevel)
        SDL_Log("Diorama: door at %d,%d width %u open %u (frame %u)", snapshot->doorMapX,
                snapshot->doorMapY, snapshot->doorWidth, snapshot->doorLevel, gMain.vblankCounter1);
    sLastLevel = snapshot->doorLevel;
}

// PKMN_DIORAMA_LOG=1: report why a sprite frame could not be captured.
static bool FrameRefused(const struct DioramaObjectSnapshot *copy, const struct Sprite *sprite,
                         const char *reason, long detail)
{
    static int sLogEnabled = -1;
    static char sLast[192];
    char line[192];

    if (sLogEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_LOG");
        sLogEnabled = value != NULL && value[0] == '1';
    }
    if (sLogEnabled)
    {
        SDL_snprintf(line, sizeof(line),
                     "Diorama: frame of object local %u gfx %u not captured: %s (%ld); usingSheet %u images %p anim %u/%u tile %u shape %u size %u",
                     copy->localId, copy->graphicsId, reason, detail, sprite->usingSheet,
                     (const void *)sprite->images, sprite->animNum, sprite->animCmdIndex,
                     sprite->oam.tileNum, sprite->oam.shape, sprite->oam.size);
        if (SDL_strcmp(line, sLast) != 0)
        {
            SDL_strlcpy(sLast, line, sizeof(sLast));
            SDL_Log("%s", line);
        }
    }
    return false;
}

static bool CopyObjectFrame(struct DioramaObjectSnapshot *copy, const struct Sprite *sprite)
{
    const u8 *source;
    u8 width;
    u8 height;
    u16 expectedSize;

    if (!DioramaSprite_GetDimensions(copy->oamShape, copy->oamSize, &width, &height)
     || copy->bpp != ST_OAM_4BPP)
        return FrameRefused(copy, sprite, "shape/bpp", copy->bpp);
    expectedSize = width * height / 2;
    if (expectedSize > sizeof(copy->frameGraphics))
        return FrameRefused(copy, sprite, "too large", expectedSize);
    if (!sprite->usingSheet && sprite->images != NULL && sprite->anims != NULL)
    {
        const union AnimCmd *command = &sprite->anims[sprite->animNum][sprite->animCmdIndex];
        const struct SpriteFrameImage *image;

        if (command->type < 0)
            return FrameRefused(copy, sprite, "anim command", command->type);
        if (sprite->images[0].relativeFrames)
        {
            // pokeemerald-expansion: one entry describes every frame; frame N
            // starts N frame sizes in (as RequestSpriteFrameImageCopy does).
            image = &sprite->images[0];
            if (image->data == NULL || image->size < expectedSize)
                return FrameRefused(copy, sprite, "image size", image->data == NULL ? -1 : image->size);
            source = (const u8 *)image->data + image->size * command->frame.imageValue;
        }
        else
        {
            image = &sprite->images[command->frame.imageValue];
            if (image->data == NULL || image->size < expectedSize)
                return FrameRefused(copy, sprite, "image size", image->data == NULL ? -1 : image->size);
            source = image->data;
        }
    }
    else
    {
        u32 byteOffset = sprite->oam.tileNum * DIORAMA_TILE_BYTES;

        if (byteOffset + expectedSize > DIORAMA_TILE_GRAPHICS_SIZE)
            return FrameRefused(copy, sprite, "VRAM range", byteOffset);
        source = (const u8 *)OBJ_VRAM0 + byteOffset;
    }
    memcpy(copy->frameGraphics, source, expectedSize);
    copy->frameSize = expectedSize;
    copy->frameHash = HashFrame(copy->frameGraphics, expectedSize);
    return true;
}

/* Whether a sprite's subsprites draw anything but its frame whole. Drawn whole:
 * one part the size of the sprite's own OAM frame, centred (against the
 * sprite's OAM size, not its object's graphics: a follower shows its 16x32
 * Poke Ball while its graphics are the mon's), or full-width strips stacked top
 * to bottom in the frame's own tile order. In long grass the game splits a
 * walker so that its lower strip goes behind the grass at a lower priority; in
 * the 3D view the grass's own cards stand in front of it (graphics overhaul,
 * 2026-10-12: Routes 119 and 120 fell back to the 2D view while the player
 * stood in their long grass). *resolvedPriority gets the top part's priority
 * and *topPart that part, whose OAM entry gives the sprite's place in the OAM. */
static bool HasUnsupportedSubsprites(const struct Sprite *sprite,
                                     const struct ObjectEventGraphicsInfo *graphicsInfo,
                                     u8 *resolvedPriority, const struct Subsprite **topPart)
{
    const struct SubspriteTable *table;
    u8 oamWidth;
    u8 oamHeight;
    int top;
    int i;

    *topPart = NULL;
    if (sprite->subspriteTables == NULL || sprite->subspriteMode == SUBSPRITES_OFF)
        return false;
    table = &sprite->subspriteTables[sprite->subspriteTableNum];
    if (table->subspriteCount == 0 || table->subsprites == NULL)
        return false;
    if (graphicsInfo == NULL
     || !DioramaSprite_GetDimensions(sprite->oam.shape, sprite->oam.size, &oamWidth, &oamHeight))
        return true;
    top = -(oamHeight / 2);
    for (i = 0; i < table->subspriteCount; i++)
    {
        const struct Subsprite *part = &table->subsprites[i];
        u8 width;
        u8 height;

        if (!DioramaSprite_GetDimensions(part->shape, part->size, &width, &height)
         || width != oamWidth || part->x != -(oamWidth / 2) || part->y != top
         || part->tileOffset != (top + oamHeight / 2) / 8 * (oamWidth / 8))
            return true;
        top += height;
    }
    if (top != oamHeight / 2)
        return true;
    *topPart = &table->subsprites[0];
    if (sprite->subspriteMode != SUBSPRITES_IGNORE_PRIORITY)
        *resolvedPriority = table->subsprites[0].priority;
    return false;
}

/* The OAM entry a sprite drew this frame: its own, or with subsprites that of
 * its top part (placed as sprite.c's AddSubspritesToOamBuffer places it). */
static u8 FindPartOamOrder(const struct Sprite *sprite, u8 priority, const struct Subsprite *part)
{
    u8 i;
    u8 shape = sprite->oam.shape;
    u8 size = sprite->oam.size;
    s16 x = sprite->oam.x;
    s16 y = sprite->oam.y;

    if (part != NULL)
    {
        u8 width;
        u8 height;
        bool8 vFlip = (sprite->oam.matrixNum >> 4) & 1;

        shape = part->shape;
        size = part->size;
        if (!DioramaSprite_GetDimensions(shape, size, &width, &height))
            return 0xFF;
        y = (u16)(sprite->oam.y - sprite->centerToCornerVecY)
          + (vFlip ? -(part->y + height) : part->y);
        x = (s16)(sprite->oam.x - sprite->centerToCornerVecX) - width / 2;
    }
    for (i = 0; i < gOamLimit; i++)
    {
        const struct OamData *oam = &gMain.oamBuffer[i];

        if (oam->x == (x & 0x1FF) && oam->y == (y & 0xFF)
         && oam->tileNum == sprite->oam.tileNum
         && oam->paletteNum == sprite->oam.paletteNum
         && oam->shape == shape && oam->size == size
         && oam->priority == priority)
            return i;
    }
    return 0xFF;
}

static u8 FindOamOrder(const struct Sprite *sprite, u8 priority)
{
    return FindPartOamOrder(sprite, priority, NULL);
}

static void CopyAffineMatrix(struct DioramaObjectSnapshot *copy, const struct Sprite *sprite)
{
    if (sprite->oam.affineMode == ST_OAM_AFFINE_NORMAL || sprite->oam.affineMode == ST_OAM_AFFINE_DOUBLE)
    {
        const struct OamMatrix *matrix = &gOamMatrices[sprite->oam.matrixNum & (OAM_MATRIX_COUNT - 1)];

        copy->affineMatrix[0] = matrix->a;
        copy->affineMatrix[1] = matrix->b;
        copy->affineMatrix[2] = matrix->c;
        copy->affineMatrix[3] = matrix->d;
    }
}

static void CopySurfBlob(struct DioramaSceneSnapshot *snapshot)
{
    const struct ObjectEvent *player;
    const struct Sprite *playerSprite;
    const struct Sprite *sprite;
    struct DioramaObjectSnapshot *copy = &snapshot->surfBlob;
    u8 spriteId;

    snapshot->playerAvatarFlags = gPlayerAvatar.flags;
    if (gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return;
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    if (!player->active || player->spriteId >= MAX_SPRITES)
        return;
    spriteId = player->fieldEffectSpriteId;
    if (spriteId >= MAX_SPRITES)
        return;
    sprite = &gSprites[spriteId];
    if (!sprite->inUse || sprite->callback != UpdateSurfBlobFieldEffect)
        return;
    playerSprite = &gSprites[player->spriteId];
    memset(copy, 0, sizeof(*copy));
    copy->active = TRUE;
    copy->spriteId = spriteId;
    copy->flags = sprite->invisible ? DIORAMA_OBJECT_INVISIBLE : 0;
    copy->screenX = sprite->x + sprite->x2;
    copy->screenY = sprite->y + sprite->y2;
    copy->affineMode = sprite->oam.affineMode;
    CopyAffineMatrix(copy, sprite);
    copy->objMode = sprite->oam.objMode;
    copy->bpp = sprite->oam.bpp;
    copy->tileNum = sprite->oam.tileNum;
    copy->paletteNum = sprite->oam.paletteNum;
    copy->oamShape = sprite->oam.shape;
    copy->oamSize = sprite->oam.size;
    copy->hFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
               && (sprite->oam.matrixNum & ST_OAM_HFLIP) != 0;
    copy->vFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
               && (sprite->oam.matrixNum & ST_OAM_VFLIP) != 0;
    copy->priority = sprite->oam.priority;
    copy->subpriority = sprite->subpriority;
    copy->oamOrder = FindOamOrder(sprite, sprite->oam.priority);
    copy->animNum = sprite->animNum;
    copy->animCmdIndex = sprite->animCmdIndex;
    if (!CopyObjectFrame(copy, sprite))
    {
        copy->flags |= DIORAMA_OBJECT_FRAME_INVALID;
        return;
    }
    snapshot->surfBlobOffsetX = copy->screenX - (playerSprite->x + playerSprite->x2);
    snapshot->surfBlobOffsetY = copy->screenY - (playerSprite->y + playerSprite->y2);
    snapshot->surfBlobValid = TRUE;
}

static s16 GetReflectionOffsetY(const struct ObjectEvent *object)
{
    static const s16 bridgeOffsets[] = {12, 28, 44};
    u8 bridgeType = MetatileBehavior_GetBridgeType(object->previousMetatileBehavior);

    if (bridgeType == 0)
        bridgeType = MetatileBehavior_GetBridgeType(object->currentMetatileBehavior);
    if (bridgeType > 0 && bridgeType <= ARRAY_COUNT(bridgeOffsets))
        return bridgeOffsets[bridgeType - 1] - 2;
    return -2;
}

static void CopyObjects(struct DioramaSceneSnapshot *snapshot)
{
    int i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        const struct ObjectEvent *object = &gObjectEvents[i];
        struct DioramaObjectSnapshot *copy;
        const struct Sprite *sprite;
        const struct ObjectEventGraphicsInfo *graphicsInfo;
        u8 resolvedPriority;
        const struct Subsprite *topPart;
        s16 baseX;
        s16 baseY;

        if (!object->active || object->spriteId >= MAX_SPRITES)
            continue;
        sprite = &gSprites[object->spriteId];
        if (!sprite->inUse || snapshot->objectCount >= DIORAMA_MAX_OBJECTS)
            continue;
        graphicsInfo = GetObjectEventGraphicsInfo(object->graphicsId);
        resolvedPriority = sprite->oam.priority;

        copy = &snapshot->objects[snapshot->objectCount++];
        copy->active = TRUE;
        copy->localId = object->localId;
        copy->graphicsId = object->graphicsId;
        copy->mapNum = object->mapNum;
        copy->mapGroup = object->mapGroup;
        copy->spriteId = object->spriteId;
        copy->elevation = object->currentElevation;
        copy->previousElevation = object->previousElevation;
        copy->facingDirection = object->facingDirection;
        copy->movementDirection = object->movementDirection;
        copy->flags = (object->isPlayer ? DIORAMA_OBJECT_PLAYER : 0)
                    | ((object->invisible || sprite->invisible
                     || sprite->oam.affineMode == ST_OAM_AFFINE_ERASE) ? DIORAMA_OBJECT_INVISIBLE : 0)
                    | (object->offScreen ? DIORAMA_OBJECT_OFFSCREEN : 0)
                    | (object->hasReflection ? DIORAMA_OBJECT_REFLECTION : 0)
                    // pokeemerald-expansion shows an overworld shadow unless
                    // noShadow suppresses it (pret's hasShadow meant "jumping").
                    | (!object->noShadow ? DIORAMA_OBJECT_SHADOW : 0)
                    | (HasUnsupportedSubsprites(sprite, graphicsInfo, &resolvedPriority, &topPart)
                        ? DIORAMA_OBJECT_SUBSPRITES : 0);
        copy->currentMapX = object->currentCoords.x;
        copy->currentMapY = object->currentCoords.y;
        copy->previousMapX = object->previousCoords.x;
        copy->previousMapY = object->previousCoords.y;
        copy->screenX = sprite->x + sprite->x2;
        copy->screenY = sprite->y + sprite->y2;
        copy->spriteX2 = sprite->x2;
        copy->spriteY2 = sprite->y2;
        copy->width = graphicsInfo != NULL ? graphicsInfo->width : 0;
        copy->height = graphicsInfo != NULL ? graphicsInfo->height : 0;
        SetSpritePosToMapCoords(object->currentCoords.x, object->currentCoords.y, &baseX, &baseY);
        copy->mapPixelOffsetX = sprite->x - (baseX + 8);
        copy->mapPixelOffsetY = sprite->y + copy->height / 2 - (baseY + 16);
        copy->centerToCornerX = sprite->centerToCornerVecX;
        copy->centerToCornerY = sprite->centerToCornerVecY;
        copy->shadowSize = graphicsInfo != NULL ? graphicsInfo->shadowSize : 0;
        copy->reflectionHidden = object->hideReflection;
        copy->reflectionPaletteNum = gReflectionEffectPaletteMap[sprite->oam.paletteNum];
        copy->reflectionOffsetY = GetReflectionOffsetY(object);
        copy->affineMode = sprite->oam.affineMode;
        CopyAffineMatrix(copy, sprite);
        copy->objMode = sprite->oam.objMode;
        copy->bpp = sprite->oam.bpp;
        copy->oamAttr0 = sprite->oam.y
                       | (sprite->oam.affineMode << 8)
                       | (sprite->oam.objMode << 10)
                       | (sprite->oam.mosaic << 12)
                       | (sprite->oam.bpp << 13)
                       | (sprite->oam.shape << 14);
        copy->oamAttr1 = sprite->oam.x | (sprite->oam.matrixNum << 9) | (sprite->oam.size << 14);
        copy->oamAttr2 = sprite->oam.tileNum | (sprite->oam.priority << 10) | (sprite->oam.paletteNum << 12);
        copy->tileNum = sprite->oam.tileNum;
        copy->paletteNum = sprite->oam.paletteNum;
        copy->oamShape = sprite->oam.shape;
        copy->oamSize = sprite->oam.size;
        copy->hFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
                   && (sprite->oam.matrixNum & ST_OAM_HFLIP) != 0;
        copy->vFlip = sprite->oam.affineMode == ST_OAM_AFFINE_OFF
                   && (sprite->oam.matrixNum & ST_OAM_VFLIP) != 0;
        copy->priority = resolvedPriority;
        copy->subpriority = sprite->subpriority;
        copy->oamOrder = FindPartOamOrder(sprite, resolvedPriority, topPart);
        copy->animNum = sprite->animNum;
        copy->animCmdIndex = sprite->animCmdIndex;
        if (!CopyObjectFrame(copy, sprite))
            copy->flags |= DIORAMA_OBJECT_FRAME_INVALID;
    }
    CopySurfBlob(snapshot);
}

/*
 * Distant scenery (scene_snapshot.h, DIORAMA_FAR_RADIUS), computed when the
 * camera's cell or the map changes and kept in between.
 */
#define MAX_COLOUR_TILESETS 24

static struct DioramaFarBlock sFar[DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS];
static u32 sFarGeneration;
static u32 sFarMapGeneration;
static s16 sFarCameraX;
static s16 sFarCameraY;
static bool8 sFarComputed;

/* The average colour of each metatile (BGR555) of the General tileset and a
 * secondary one, from their own tiles and palettes. */
static struct
{
    const struct Tileset *secondary;
    const u8 *primaryTiles;
    const u8 *secondaryTiles;
    u16 *colours;          /* COLOUR_UNKNOWN until a metatile is asked for */
} sColourCache[MAX_COLOUR_TILESETS];

#define COLOUR_UNKNOWN 0xFFFF /* not a BGR555 colour */

static u16 AverageMetatileColour(const struct Tileset *primary, const u8 *primaryTiles,
                                 const struct Tileset *secondary, const u8 *secondaryTiles,
                                 u16 metatileId)
{
    const struct Tileset *owner = metatileId < NUM_METATILES_IN_PRIMARY ? primary : secondary;
    const u16 *entries = owner->metatiles
                       + (metatileId % NUM_METATILES_IN_PRIMARY) * DIORAMA_METATILE_ENTRY_COUNT;
    u32 sums[3] = {0, 0, 0};
    u32 count = 0;
    int x;
    int y;

    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
        {
            int tile = (y / 8) * 2 + x / 8;
            int layer;

            for (layer = 1; layer >= 0; layer--)
            {
                u16 entry = entries[layer * 4 + tile];
                u16 tileId = entry & 0x3FF;
                u8 palette = entry >> 12;
                int px = entry & (1 << 10) ? 7 - (x & 7) : x & 7;
                int py = entry & (1 << 11) ? 7 - (y & 7) : y & 7;
                const u8 *tiles = tileId < DIORAMA_PAGE_FIRST_TILE ? primaryTiles : secondaryTiles;
                u8 packed;
                u8 colour;
                u16 bgr;

                if (tiles == NULL)
                    continue;
                packed = tiles[(tileId % DIORAMA_PAGE_FIRST_TILE) * DIORAMA_TILE_BYTES + py * 4 + px / 2];
                colour = px & 1 ? packed >> 4 : packed & 0x0F;
                if (colour == 0)
                    continue;
                bgr = palette < DIORAMA_PAGE_FIRST_PALETTE ? primary->palettes[palette][colour]
                    : (palette < DIORAMA_PAGE_FIRST_PALETTE + DIORAMA_PAGE_PALETTE_COUNT
                       ? secondary->palettes[palette][colour] : 0);
                sums[0] += bgr & 31;
                sums[1] += (bgr >> 5) & 31;
                sums[2] += (bgr >> 10) & 31;
                count++;
                break;
            }
        }
    if (count == 0)
        return 0;
    return (u16)(((sums[0] + count / 2) / count)
               | (((sums[1] + count / 2) / count) << 5)
               | (((sums[2] + count / 2) / count) << 10));
}

/* The metatiles' average colours for a layout's tilesets (the General
 * primary and its secondary), NULL for another primary. */
static int ColourCacheIndex(const struct MapLayout *layout);

/* One metatile's average colour, worked out when asked for. A tileset does not say how many metatiles it holds (a secondary
 * holds up to 512, most far fewer), so working out all 1024 read past the end of the smaller ones' data (found with the
 * address sanitizer); a metatile a map really uses is always inside its tilesets. */
static u16 LayoutColour(const struct MapLayout *layout, u16 metatile)
{
    int i = ColourCacheIndex(layout);

    if (i < 0 || metatile >= NUM_METATILES_TOTAL)
        return COLOUR_UNKNOWN;
    if (sColourCache[i].colours[metatile] == COLOUR_UNKNOWN)
        sColourCache[i].colours[metatile] = AverageMetatileColour(&gTileset_General, sColourCache[i].primaryTiles,
                                                                  layout->secondaryTileset, sColourCache[i].secondaryTiles,
                                                                  metatile);
    return sColourCache[i].colours[metatile];
}

static int ColourCacheIndex(const struct MapLayout *layout)
{
    const u8 *primaryTiles;
    const u8 *secondaryTiles;
    u16 *colours;
    int i;

    if (layout == NULL || layout->primaryTileset != &gTileset_General || layout->isFrlg
     || layout->secondaryTileset == NULL || layout->secondaryTileset->metatiles == NULL
     || layout->secondaryTileset->palettes == NULL)
        return -1;
    for (i = 0; i < MAX_COLOUR_TILESETS && sColourCache[i].secondary != NULL; i++)
        if (sColourCache[i].secondary == layout->secondaryTileset)
            return i;
    if (i == MAX_COLOUR_TILESETS)
        return -1;
    primaryTiles = PagedTiles(&gTileset_General);
    secondaryTiles = PagedTiles(layout->secondaryTileset);
    colours = malloc(NUM_METATILES_TOTAL * sizeof(*colours));
    if (colours == NULL || primaryTiles == NULL || secondaryTiles == NULL)
    {
        free(colours);
        return -1;
    }
    for (u16 metatile = 0; metatile < NUM_METATILES_TOTAL; metatile++)
        colours[metatile] = COLOUR_UNKNOWN;
    sColourCache[i].secondary = layout->secondaryTileset;
    sColourCache[i].primaryTiles = primaryTiles;
    sColourCache[i].secondaryTiles = secondaryTiles;
    sColourCache[i].colours = colours;
    return i;
}

/* Where a connection's map lies from the map it leaves, whatever the
 * distance: the position (x, y) of the map left in the connected map's own
 * coordinates. */
static void ToConnectedMap(const struct MapConnection *connection, const struct MapLayout *from,
                           const struct MapLayout *to, s32 x, s32 y, s32 *toX, s32 *toY)
{
    switch (connection->direction)
    {
    case CONNECTION_NORTH: *toX = x - connection->offset; *toY = y + to->height; break;
    case CONNECTION_SOUTH: *toX = x - connection->offset; *toY = y - from->height; break;
    case CONNECTION_WEST: *toX = x + to->width; *toY = y - connection->offset; break;
    case CONNECTION_EAST: *toX = x - from->width; *toY = y - connection->offset; break;
    default: *toX = -100000; *toY = -100000; break;
    }
}

struct FarCell
{
    const struct MapLayout *layout; /* NULL: the map's border block */
    u16 layoutId;
    s16 x;
    s16 y;
    u8 mapGroup;
    u8 mapNum;
    u16 entry;
};

/* The layout the map is drawn from now. A map's scripts can switch it
 * (setmaplayoutindex: Route 111 without its Mirage Tower, Sootopolis during
 * the legends' battle, Shoal Cave at high tide): SetCurrentMapLayout sets
 * the save's id and gMapHeader.mapLayout, while gMapHeader.mapLayoutId keeps
 * the map's own - the rules and the building models would have read the
 * cells of a layout the game is not showing. */
static u16 CurrentLayoutId(void)
{
    return gSaveBlock1Ptr != NULL ? gSaveBlock1Ptr->mapLayoutId : gMapHeader.mapLayoutId;
}

/* The cell at map coordinates (mapX, mapY) as far as the static maps go:
 * the map (its own grid, with any changes), a connected map or one connected
 * to that, else the map's border block. */
static void FindFarCell(int mapX, int mapY, struct FarCell *cell)
{
    const struct MapLayout *layout = gMapHeader.mapLayout;
    const struct MapConnection *connection;
    s32 x = mapX - MAP_OFFSET;
    s32 y = mapY - MAP_OFFSET;
    int i;

    memset(cell, 0, sizeof(*cell));
    if (x >= 0 && y >= 0 && x < layout->width && y < layout->height)
    {
        cell->layout = layout;
        cell->layoutId = CurrentLayoutId();
        cell->x = x;
        cell->y = y;
        cell->mapGroup = gSaveBlock1Ptr->location.mapGroup;
        cell->mapNum = gSaveBlock1Ptr->location.mapNum;
        cell->entry = (u16)MapGridGetMetatileIdAt(mapX, mapY)
                    | (u16)(MapGridGetCollisionAt(mapX, mapY) << 10)
                    | (u16)(MapGridGetElevationAt(mapX, mapY) << 12);
        return;
    }
    connection = GetMapConnectionAtPos(mapX, mapY);
    if (connection != NULL)
    {
        const struct MapHeader *header = GetMapHeaderFromConnection(connection);
        s32 cx;
        s32 cy;

        if (header != NULL && header->mapLayout != NULL && header->mapLayout->map != NULL)
        {
            ToConnectedMap(connection, layout, header->mapLayout, x, y, &cx, &cy);
            if (cx >= 0 && cy >= 0 && cx < header->mapLayout->width && cy < header->mapLayout->height)
            {
                cell->layout = header->mapLayout;
                cell->layoutId = header->mapLayoutId;
                cell->x = cx;
                cell->y = cy;
                cell->mapGroup = connection->mapGroup;
                cell->mapNum = connection->mapNum;
                cell->entry = header->mapLayout->map[cy * header->mapLayout->width + cx];
                return;
            }
        }
    }
    /* A map connected to one of the map's connections. */
    if (gMapHeader.connections != NULL)
        for (i = 0; i < gMapHeader.connections->count; i++)
        {
            const struct MapConnection *first = &gMapHeader.connections->connections[i];
            const struct MapHeader *header;
            s32 x1;
            s32 y1;
            int k;

            if (first->direction == CONNECTION_DIVE || first->direction == CONNECTION_EMERGE)
                continue;
            header = GetMapHeaderFromConnection(first);
            if (header == NULL || header->mapLayout == NULL || header->connections == NULL)
                continue;
            ToConnectedMap(first, layout, header->mapLayout, x, y, &x1, &y1);
            for (k = 0; k < header->connections->count; k++)
            {
                const struct MapConnection *second = &header->connections->connections[k];
                const struct MapHeader *farHeader; /* not "far": a macro on Windows */
                s32 x2;
                s32 y2;

                if (second->direction == CONNECTION_DIVE || second->direction == CONNECTION_EMERGE
                 || (second->mapGroup == gSaveBlock1Ptr->location.mapGroup
                  && second->mapNum == gSaveBlock1Ptr->location.mapNum))
                    continue;
                farHeader = GetMapHeaderFromConnection(second);
                if (farHeader == NULL || farHeader->mapLayout == NULL || farHeader->mapLayout->map == NULL)
                    continue;
                ToConnectedMap(second, header->mapLayout, farHeader->mapLayout, x1, y1, &x2, &y2);
                /* on the connection's side of the map (as GetMapConnectionAtPos) */
                if ((second->direction == CONNECTION_NORTH && y1 >= 0)
                 || (second->direction == CONNECTION_SOUTH && y1 < header->mapLayout->height)
                 || (second->direction == CONNECTION_WEST && x1 >= 0)
                 || (second->direction == CONNECTION_EAST && x1 < header->mapLayout->width))
                    continue;
                if (x2 < 0 || y2 < 0 || x2 >= farHeader->mapLayout->width || y2 >= farHeader->mapLayout->height)
                    continue;
                cell->layout = farHeader->mapLayout;
                cell->layoutId = farHeader->mapLayoutId;
                cell->x = x2;
                cell->y = y2;
                cell->mapGroup = second->mapGroup;
                cell->mapNum = second->mapNum;
                cell->entry = farHeader->mapLayout->map[y2 * farHeader->mapLayout->width + x2];
                return;
            }
        }
    cell->entry = (u16)MapGridGetMetatileIdAt(mapX, mapY)
                | (u16)(MapGridGetCollisionAt(mapX, mapY) << 10);
}

static bool8 FarEnabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_FAR");

        sEnabled = value == NULL || value[0] != '0';
    }
    return sEnabled;
}

static int FloorEven(int value)
{
    return value >= 0 ? value & ~1 : -((-value + 1) & ~1);
}

static void ComputeFar(s16 cameraX, s16 cameraY)
{
    int originX = FloorEven(cameraX - DIORAMA_FAR_RADIUS);
    int originY = FloorEven(cameraY - DIORAMA_FAR_RADIUS);
    int innerLeft = cameraX - DIORAMA_GRID_WIDTH / 2;
    int innerTop = cameraY - DIORAMA_GRID_HEIGHT / 2;
    int i;
    int j;

    for (j = 0; j < DIORAMA_FAR_BLOCKS; j++)
        for (i = 0; i < DIORAMA_FAR_BLOCKS; i++)
        {
            struct DioramaFarBlock *block = &sFar[j * DIORAMA_FAR_BLOCKS + i];
            int left = originX + i * DIORAMA_FAR_BLOCK_CELLS;
            int top = originY + j * DIORAMA_FAR_BLOCK_CELLS;
            struct FarCell chosen;
            u16 colour;
            int k;

            memset(block, 0, sizeof(*block));
            /* the scene's own cells draw themselves */
            if (left >= innerLeft && top >= innerTop
             && left + DIORAMA_FAR_BLOCK_CELLS <= innerLeft + DIORAMA_GRID_WIDTH
             && top + DIORAMA_FAR_BLOCK_CELLS <= innerTop + DIORAMA_GRID_HEIGHT)
                continue;
            /* an obstacle stands for the block (a tree, a building), else its
             * top-left cell */
            FindFarCell(left, top, &chosen);
            for (k = 1; k < 4 && UNPACK_COLLISION(chosen.entry) == 0; k++)
            {
                struct FarCell other;

                FindFarCell(left + (k & 1), top + (k >> 1), &other);
                if (UNPACK_COLLISION(other.entry) != 0)
                    chosen = other;
            }
            colour = LayoutColour(chosen.layout != NULL ? chosen.layout : gMapHeader.mapLayout, UNPACK_METATILE(chosen.entry));
            if (colour == COLOUR_UNKNOWN)
                continue;
            block->metatileId = UNPACK_METATILE(chosen.entry);
            block->color = colour;
            block->flags = DIORAMA_FAR_VALID;
            if (chosen.layout != NULL)
            {
                block->flags |= DIORAMA_FAR_SOURCE;
                block->layoutId = chosen.layoutId;
                block->sourceX = chosen.x;
                block->sourceY = chosen.y;
                block->mapGroup = chosen.mapGroup;
                block->mapNum = chosen.mapNum;
            }
        }
}

/* The distant scenery into a snapshot: outdoors only, worked out again when
 * the camera's cell or the map changed. */
static void CopyFar(struct DioramaSceneSnapshot *snapshot)
{
    u8 mapType = gMapHeader.mapType;

    if (!FarEnabled() || gMapHeader.mapLayout == NULL
     || (mapType != MAP_TYPE_TOWN && mapType != MAP_TYPE_CITY && mapType != MAP_TYPE_ROUTE
      && mapType != MAP_TYPE_OCEAN_ROUTE))
        return;
    if (!sFarComputed || sFarMapGeneration != sMapGeneration
     || sFarCameraX != snapshot->cameraMapX || sFarCameraY != snapshot->cameraMapY)
    {
        ComputeFar(snapshot->cameraMapX, snapshot->cameraMapY);
        if (LogEnabled() && (!sFarComputed || sFarMapGeneration != sMapGeneration))
        {
            int valid = 0;
            int sourced = 0;

            for (int i = 0; i < DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS; i++)
            {
                valid += (sFar[i].flags & DIORAMA_FAR_VALID) != 0;
                sourced += (sFar[i].flags & DIORAMA_FAR_SOURCE) != 0;
            }
            SDL_Log("Diorama: distant scenery: %d blocks (%d from maps, the rest border)", valid, sourced);
        }
        sFarComputed = TRUE;
        sFarMapGeneration = sMapGeneration;
        sFarCameraX = snapshot->cameraMapX;
        sFarCameraY = snapshot->cameraMapY;
        sFarGeneration++;
    }
    snapshot->farOn = TRUE;
    snapshot->farGeneration = sFarGeneration;
    snapshot->farOriginX = (s16)FloorEven(sFarCameraX - DIORAMA_FAR_RADIUS);
    snapshot->farOriginY = (s16)FloorEven(sFarCameraY - DIORAMA_FAR_RADIUS);
    memcpy(snapshot->farBlocks, sFar, sizeof(sFar));
}

void DioramaScene_Init(void)
{
    memset(&sDraft, 0, sizeof(sDraft));
    memset(sPreviousPalette, 0, sizeof(sPreviousPalette));
    memset(sPreviousTileGraphics, 0, sizeof(sPreviousTileGraphics));
    sMapGeneration = 0;
    sMapEditGeneration = 0;
    sPaletteGeneration = 0;
    sObjPaletteGeneration = 0;
    sTilesetAnimationGeneration = 0;
    sHasPreviousPalette = false;
    sHasPreviousTileGraphics = false;
    sPublishedThisFrame = false;
    sMapChanged = true;
    sPreviousMapGroup = -1;
    sPreviousMapNum = -1;
    sPreviousMapLayoutId = 0;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
}

void DioramaScene_BeginFrame(void)
{
    sPublishedThisFrame = false;
}

void DioramaScene_MarkMapChanged(void)
{
    sMapChanged = true;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
}

void DioramaScene_MarkCellDirty(int16_t mapX, int16_t mapY)
{
    u8 i;

    sMapEditGeneration++;
    for (i = 0; i < sDirtyCellCount; i++)
        if (sDirtyCells[i].mapX == mapX && sDirtyCells[i].mapY == mapY)
            return;
    if (sDirtyCellCount >= DIORAMA_MAX_DIRTY_CELLS)
    {
        sDirtyOverflow = true;
        return;
    }
    sDirtyCells[sDirtyCellCount].mapX = mapX;
    sDirtyCells[sDirtyCellCount].mapY = mapY;
    sDirtyCellCount++;
}

void DioramaScene_PublishOverworld(void)
{
    u16 cameraX;
    u16 cameraY;
    s8 mapGroup = gSaveBlock1Ptr->location.mapGroup;
    s8 mapNum = gSaveBlock1Ptr->location.mapNum;

    memset(&sDraft, 0, sizeof(sDraft));
    sDraft.sceneKind = GetOverworldSceneKind(&sDraft.fallbackReasons);
    /* Not at its own colours: fading, or faded out whole and standing there (a script's fadescreen to
     * black, Route 101's bag before the starter choice, leaves the field black with no fade running).
     * The 3D battle stands on the last field seen with steady colours: a black one gave the first
     * battle a black world (Update 26, bug 3). A part-way fade that has settled (time of day, a
     * dimmed menu) is a tint of the field's own and stays steady, as native_ui.c CaptureScreenFade. */
    sDraft.paletteFading = gPaletteFade.active != 0
                        || (gPaletteFade.mode == 0 && gPaletteFade.y >= 16 /* palette.c NORMAL_FADE */);
    sDraft.battleTransition = gMain.callback2 == CB2_OverworldBasic && DioramaBattle_TransitionActive();
    sDraft.valid = gMapHeader.mapLayout != NULL;
    if (!sDraft.valid)
        sDraft.fallbackReasons |= DIORAMA_FALLBACK_MAP_UNAVAILABLE;

    sDraft.mapGroup = mapGroup;
    sDraft.mapNum = mapNum;
    sDraft.mapLayoutId = CurrentLayoutId();
    sDraft.mapCoordinateOffset = MAP_OFFSET;
    sDraft.mapType = gMapHeader.mapType;
    sDraft.weather = GetCurrentWeather();
    sDraft.nextWeather = gWeatherPtr->nextWeather;
    sDraft.weatherTransitionComplete = gWeatherPtr->weatherChangeComplete;
    sDraft.timeOfDay = gTimeOfDay;
    sDraft.rainVisibleCount = gWeatherPtr->curRainSpriteIndex;
    sDraft.fogScrollOffset = gWeatherPtr->fogHScrollOffset;
    sDraft.weatherBlendEVA = gWeatherPtr->currBlendEVA;
    sDraft.weatherBlendEVB = gWeatherPtr->currBlendEVB;
    if (!DioramaWeather_CanRender(sDraft.weather, sDraft.nextWeather))
        sDraft.fallbackReasons |= DIORAMA_FALLBACK_UNSUPPORTED_WEATHER;
    if (sDraft.valid)
    {
        sDraft.mapWidth = gMapHeader.mapLayout->width;
        sDraft.mapHeight = gMapHeader.mapLayout->height;
    }

    if (sMapChanged || mapGroup != sPreviousMapGroup || mapNum != sPreviousMapNum
     || sDraft.mapLayoutId != sPreviousMapLayoutId)
    {
        sMapGeneration++;
        sMapChanged = false;
        sPreviousMapGroup = mapGroup;
        sPreviousMapNum = mapNum;
        sPreviousMapLayoutId = sDraft.mapLayoutId;
    }
    {
        s8 treeStyle = DioramaTreeSprites_Enabled();

        if (sTreeStyle != treeStyle)
        {
            /* Every tree cell's drawing changes with the tree style. */
            if (sTreeStyle >= 0)
                sDirtyOverflow = true;
            sTreeStyle = treeStyle;
        }
    }
    sDraft.mapGeneration = sMapGeneration;
    sDraft.mapEditGeneration = sMapEditGeneration;
    sDraft.dirtyCellCount = sDirtyCellCount;
    sDraft.dirtyOverflow = sDirtyOverflow;
    memcpy(sDraft.dirtyCells, sDirtyCells,
           sDirtyCellCount * sizeof(*sDirtyCells));
    if (!sHasPreviousPalette
     || memcmp(sPreviousPalette, gPlttBufferFaded, 256 * sizeof(*sPreviousPalette)) != 0)
        sPaletteGeneration++;
    if (!sHasPreviousPalette
     || memcmp(sPreviousPalette + 256, gPlttBufferFaded + 256,
               256 * sizeof(*sPreviousPalette)) != 0)
        sObjPaletteGeneration++;
    memcpy(sPreviousPalette, gPlttBufferFaded, sizeof(sPreviousPalette));
    sHasPreviousPalette = true;
    memcpy(sDraft.unfadedPalette, gPlttBufferUnfaded, sizeof(sDraft.unfadedPalette));
    memcpy(sDraft.fadedPalette, gPlttBufferFaded, sizeof(sDraft.fadedPalette));
    sDraft.paletteGeneration = sPaletteGeneration;
    sDraft.objPaletteGeneration = sObjPaletteGeneration;

    if (sDraft.valid)
    {
        CopyTileGraphics(&sDraft);
        GetCameraFocusCoords(&cameraX, &cameraY);
        sDraft.cameraMapX = cameraX;
        sDraft.cameraMapY = cameraY;
        GetCameraOffsetWithPan(&sDraft.cameraPixelX, &sDraft.cameraPixelY);
        sDraft.cameraSubpixelX = gFieldCamera.x;
        sDraft.cameraSubpixelY = gFieldCamera.y;
        if (gFieldCamera.movementSpeedX > 0 && sDraft.cameraSubpixelX > 0)
            sDraft.cameraSubpixelX -= 16;
        else if (gFieldCamera.movementSpeedX < 0 && sDraft.cameraSubpixelX < 0)
            sDraft.cameraSubpixelX += 16;
        if (gFieldCamera.movementSpeedY > 0 && sDraft.cameraSubpixelY > 0)
            sDraft.cameraSubpixelY -= 16;
        else if (gFieldCamera.movementSpeedY < 0 && sDraft.cameraSubpixelY < 0)
            sDraft.cameraSubpixelY += 16;
        GetCameraPan(&sDraft.cameraPanX, &sDraft.cameraPanY);
        sDraft.tilesetResourcesValid = CopyCells(&sDraft);
        if (!sDraft.tilesetResourcesValid)
            sDraft.fallbackReasons |= DIORAMA_FALLBACK_TILESET_UNAVAILABLE;
        CopyFar(&sDraft);
        CopyObjects(&sDraft);
        if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT
         && gObjectEvents[gPlayerAvatar.objectEventId].active)
        {
            const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

            sDraft.playerMapX = player->currentCoords.x - MAP_OFFSET;
            sDraft.playerMapY = player->currentCoords.y - MAP_OFFSET;
            sDraft.playerFacingDirection = player->facingDirection;
        }
        CopyDoor(&sDraft);
        CopyFieldLight(&sDraft);
        CopyUiProfile(&sDraft);
    }
    DioramaSnapshotExchange_Publish(&sDraft);
    sPublishedThisFrame = true;
    sDirtyCellCount = 0;
    sDirtyOverflow = false;
}

void DioramaScene_EndFrame(bool inBattle)
{
    if (!sPublishedThisFrame)
    {
        memset(&sDraft, 0, sizeof(sDraft));
        sDraft.sceneKind = inBattle ? DIORAMA_SCENE_BATTLE : DIORAMA_SCENE_UNAVAILABLE;
        sDraft.fallbackReasons = DIORAMA_FALLBACK_INVALID_SNAPSHOT
                               | DIORAMA_FALLBACK_NON_OVERWORLD;
        DioramaSnapshotExchange_Publish(&sDraft);
    }
}

#endif
