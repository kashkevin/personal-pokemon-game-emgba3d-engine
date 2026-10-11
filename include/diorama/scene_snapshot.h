#ifndef GUARD_DIORAMA_SCENE_SNAPSHOT_H
#define GUARD_DIORAMA_SCENE_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/ui_overlay.h"

#define DIORAMA_SNAPSHOT_COUNT 3
/* The cells round the camera the 3D view holds: 49 x 49 since the graphics
 * overhaul (33 x 33 before), so the wider cameras and the edge fog
 * (gl_light.h) never show the scene's end. */
#define DIORAMA_GRID_WIDTH 49
#define DIORAMA_GRID_HEIGHT 49
#define DIORAMA_MAX_VISIBLE_CELLS (DIORAMA_GRID_WIDTH * DIORAMA_GRID_HEIGHT)
#define DIORAMA_MAX_OBJECTS 16
#define DIORAMA_FADED_PALETTE_ENTRIES 512
#define DIORAMA_TILE_COUNT 1024
#define DIORAMA_TILE_BYTES 32
#define DIORAMA_TILE_GRAPHICS_SIZE (DIORAMA_TILE_COUNT * DIORAMA_TILE_BYTES)
#define DIORAMA_OBJ_FRAME_MAX_BYTES 2048
#define DIORAMA_METATILE_ENTRY_COUNT 8
#define DIORAMA_MAX_DIRTY_CELLS 64
/*
 * Neighbouring maps drawn with their own tiles: a connected map whose
 * secondary tileset is not the map's own (Slateport seen from Route 110) has
 * its cells published with atlasPage 1 to DIORAMA_NEIGHBOUR_PAGES instead of
 * as border tiles. Its page (neighbourPages[atlasPage - 1]) holds that
 * tileset's 512 secondary tiles, read once on the game thread and kept, and
 * its palettes 6 to 12 as the game would have them now (unfaded: the night
 * palette mix of OW_ENABLE_DNS). The primary tileset is shared (only maps
 * with the same one are paged), so page cells keep the live tiles 0-511, and
 * palettes 0-5. Page 0 is the map's own tiles.
 */
#define DIORAMA_NEIGHBOUR_PAGES 3
#define DIORAMA_PAGE_FIRST_TILE 512
#define DIORAMA_PAGE_TILE_BYTES ((DIORAMA_TILE_COUNT - DIORAMA_PAGE_FIRST_TILE) * DIORAMA_TILE_BYTES)
#define DIORAMA_PAGE_FIRST_PALETTE 6
#define DIORAMA_PAGE_PALETTE_COUNT 7
/*
 * Distant scenery (graphics overhaul): past the scene's cells, the world out
 * to about DIORAMA_FAR_RADIUS tiles from the camera in blocks of 2 x 2 cells
 * on even map coordinates (so a step does not regroup them): the map, its
 * connected maps and theirs, the map's border block beyond. A block is one of
 * its cells (an obstacle if it has one: a tree, a building, a rock), with
 * where that cell comes from (for the rules' heights) and the average colour
 * of its metatile's art, unfaded. Published outdoors only (towns, cities,
 * routes); farGeneration changes when the blocks do.
 */
#define DIORAMA_FAR_BLOCK_CELLS 2
#define DIORAMA_FAR_RADIUS 48
#define DIORAMA_FAR_BLOCKS (DIORAMA_FAR_RADIUS + 1)

enum DioramaFarBlockFlags
{
    DIORAMA_FAR_VALID = 1 << 0,  /* the block has a metatile and a colour */
    DIORAMA_FAR_SOURCE = 1 << 1, /* ... and a map cell (layoutId, sourceX/Y, map) */
};

enum DioramaCellFlags
{
    DIORAMA_CELL_SOURCE_VALID = 1 << 0,
    DIORAMA_CELL_CONNECTED = 1 << 1,
    DIORAMA_CELL_BORDER = 1 << 2,
};

enum DioramaObjectFlags
{
    DIORAMA_OBJECT_PLAYER     = 1 << 0,
    DIORAMA_OBJECT_INVISIBLE  = 1 << 1,
    DIORAMA_OBJECT_OFFSCREEN  = 1 << 2,
    DIORAMA_OBJECT_REFLECTION = 1 << 3,
    DIORAMA_OBJECT_SHADOW     = 1 << 4,
    DIORAMA_OBJECT_SUBSPRITES = 1 << 5,
    DIORAMA_OBJECT_FRAME_INVALID = 1 << 6,
};

enum DioramaRenderMode
{
    DIORAMA_RENDER_CLASSIC_2D,
    DIORAMA_RENDER_AUTO,
    DIORAMA_RENDER_FORCE_3D,
    DIORAMA_RENDER_DEBUG
};

enum DioramaSceneKind
{
    DIORAMA_SCENE_UNAVAILABLE,
    DIORAMA_SCENE_OVERWORLD_FREE,
    DIORAMA_SCENE_OVERWORLD_SCRIPTED,
    DIORAMA_SCENE_DIALOGUE,
    DIORAMA_SCENE_MENU,
    DIORAMA_SCENE_BATTLE,
    DIORAMA_SCENE_TRANSITION,
    DIORAMA_SCENE_SPECIAL
};

enum DioramaFallbackReason
{
    DIORAMA_FALLBACK_NONE = 0,
    DIORAMA_FALLBACK_INVALID_SNAPSHOT = 1u << 0,
    DIORAMA_FALLBACK_UNSUPPORTED_SCENE = 1u << 1,
    DIORAMA_FALLBACK_MAP_UNAVAILABLE = 1u << 2,
    DIORAMA_FALLBACK_GRID_INCOMPLETE = 1u << 3,
    DIORAMA_FALLBACK_OBJECT_OVERFLOW = 1u << 4,
    DIORAMA_FALLBACK_PALETTE_UNAVAILABLE = 1u << 5,
    DIORAMA_FALLBACK_RENDERER_UNAVAILABLE = 1u << 6,
    DIORAMA_FALLBACK_INTERNAL_ERROR = 1u << 7,
    DIORAMA_FALLBACK_PALETTE_FADE = 1u << 8,
    DIORAMA_FALLBACK_MODAL_UI = 1u << 9,
    DIORAMA_FALLBACK_NON_OVERWORLD = 1u << 10,
    DIORAMA_FALLBACK_TILESET_UNAVAILABLE = 1u << 11,
    DIORAMA_FALLBACK_UNSUPPORTED_WEATHER = 1u << 12
};

struct DioramaCellSnapshot
{
    int16_t mapX;
    int16_t mapY;
    int16_t sourceMapX;
    int16_t sourceMapY;
    uint16_t metatileId;
    uint16_t sourceLayoutId;
    uint8_t behavior;
    uint8_t layerType;
    uint8_t collision;
    uint8_t elevation;
    uint8_t sourceMapGroup;
    uint8_t sourceMapNum;
    uint16_t flags;
    /* With the 3DS project's trees (tree_sprites.h): the part of a tree the
     * cell was, published as ground; DIORAMA_TREE_SPRITE_NONE (0) otherwise. */
    uint8_t treeSprite;
    /* 0: drawn with the map's own tiles; 1 to DIORAMA_NEIGHBOUR_PAGES: with a
     * neighbouring map's (its metatileId and tileEntries are that map's). */
    uint8_t atlasPage;
    uint16_t tileEntries[DIORAMA_METATILE_ENTRY_COUNT];
};

struct DioramaFarBlock
{
    uint16_t metatileId;
    uint16_t layoutId;
    int16_t sourceX;
    int16_t sourceY;
    uint8_t mapGroup;
    uint8_t mapNum;
    uint8_t flags;
    uint8_t unused;
    uint16_t color; /* BGR555, unfaded */
};

struct DioramaNeighbourPage
{
    /* The secondary tileset the page holds (struct Tileset *), for identity
     * only; NULL: the page is not in use. */
    const void *tileset;
    /* Its tiles 512 to 1023 (DIORAMA_PAGE_TILE_BYTES), never changed or freed. */
    const uint8_t *tiles;
    /* Its palettes 6 to 12, unfaded, as the game would load them now. */
    uint16_t palettes[DIORAMA_PAGE_PALETTE_COUNT * 16];
};

struct DioramaDirtyCell
{
    int16_t mapX;
    int16_t mapY;
};

struct DioramaObjectSnapshot
{
    uint8_t active;
    uint8_t localId;
    // pokeemerald-expansion: graphics ids are 16-bit (followers and visible
    // wild spawns use OBJ_EVENT_MON + species); localId is only unique within
    // one map, so objects are matched across frames by localId + map.
    uint16_t graphicsId;
    uint8_t mapNum;
    uint8_t mapGroup;
    uint8_t spriteId;
    uint8_t elevation;
    uint8_t previousElevation;
    uint8_t facingDirection;
    uint8_t movementDirection;
    uint8_t flags;
    int16_t currentMapX;
    int16_t currentMapY;
    int16_t previousMapX;
    int16_t previousMapY;
    int16_t screenX;
    int16_t screenY;
    int16_t spriteX2;
    int16_t spriteY2;
    int16_t mapPixelOffsetX;
    int16_t mapPixelOffsetY;
    int8_t centerToCornerX;
    int8_t centerToCornerY;
    uint8_t width;
    uint8_t height;
    uint8_t shadowSize;
    uint8_t reflectionHidden;
    uint8_t reflectionPaletteNum;
    int16_t reflectionOffsetY;
    uint8_t affineMode;
    /* The sprite's affine matrix (gOamMatrices, 8.8: texture = M x screen,
     * about the sprite's centre) when affineMode is normal or double size:
     * a follower shrinking into or growing out of its Poke Ball. */
    int16_t affineMatrix[4];
    uint8_t objMode;
    uint8_t bpp;
    uint16_t frameSize;
    uint64_t frameHash;
    uint16_t oamAttr0;
    uint16_t oamAttr1;
    uint16_t oamAttr2;
    uint16_t tileNum;
    uint8_t paletteNum;
    uint8_t oamShape;
    uint8_t oamSize;
    uint8_t hFlip;
    uint8_t vFlip;
    uint8_t priority;
    uint8_t subpriority;
    uint8_t oamOrder;
    uint8_t animNum;
    uint8_t animCmdIndex;
    uint8_t frameGraphics[DIORAMA_OBJ_FRAME_MAX_BYTES];
};

struct DioramaSceneSnapshot
{
    uint64_t sequence;
    uint32_t mapGeneration;
    uint32_t mapEditGeneration;
    uint32_t paletteGeneration;
    uint32_t objPaletteGeneration;
    uint32_t tilesetAnimationGeneration;
    uint8_t valid;
    uint8_t tilesetResourcesValid;
    enum DioramaSceneKind sceneKind;
    uint32_t fallbackReasons;
    uint8_t mapGroup;
    uint8_t mapNum;
    uint16_t mapLayoutId;
    uint16_t mapWidth;
    uint16_t mapHeight;
    uint8_t mapCoordinateOffset;
    uint8_t mapType;
    uint8_t weather;
    uint8_t nextWeather;
    uint8_t weatherTransitionComplete;
    uint8_t timeOfDay; /* gTimeOfDay: enum TimeOfDay (constants/rtc.h) */
    uint8_t rainVisibleCount;
    uint16_t fogScrollOffset;
    uint16_t weatherBlendEVA;
    uint16_t weatherBlendEVB;
    int16_t cameraMapX;
    int16_t cameraMapY;
    int16_t cameraPixelX;
    int16_t cameraPixelY;
    int8_t cameraSubpixelX;
    int8_t cameraSubpixelY;
    int16_t cameraPanX;
    int16_t cameraPanY;
    int16_t gridOriginX;
    int16_t gridOriginY;
    uint16_t visibleCellCount;
    struct DioramaCellSnapshot cells[DIORAMA_MAX_VISIBLE_CELLS];
    uint8_t dirtyCellCount;
    uint8_t dirtyOverflow;
    struct DioramaDirtyCell dirtyCells[DIORAMA_MAX_DIRTY_CELLS];
    uint8_t objectCount;
    struct DioramaObjectSnapshot objects[DIORAMA_MAX_OBJECTS];
    uint8_t uiFlags;
    uint8_t uiRectCount;
    struct DioramaUiRect uiRects[DIORAMA_MAX_UI_RECTS];
    uint32_t fieldOamMask[DIORAMA_OAM_MASK_WORDS];
    uint32_t interfaceOamMask[DIORAMA_OAM_MASK_WORDS];
    uint16_t uiBgControl;
    uint16_t uiBgHOffset;
    uint16_t uiBgVOffset;
    uint8_t uiBgTileGraphics[DIORAMA_UI_BG_TILE_BYTES];
    uint16_t uiBgTilemap[DIORAMA_UI_BG_MAP_BYTES / sizeof(uint16_t)];
    uint8_t playerAvatarFlags;
    int16_t playerMapX;
    int16_t playerMapY;
    uint8_t playerFacingDirection;
    /* A door standing open (field_door.c, DioramaDoor_Get): its leftmost cell
     * in map grid coordinates (as cells[].mapX), its width in cells and how
     * far open, 1 to 3; doorLevel 0 when no door is open. */
    int16_t doorMapX;
    int16_t doorMapY;
    uint8_t doorWidth;
    uint8_t doorLevel;
    /* The dark of a cave that needs Flash, and of the Battle Pyramid: the
     * circle of light the game opens round the player
     * (field_screen_effect.c SetFlashScanlineEffectWindowBoundaries), kept
     * as the 3DS project keeps it (3ds_game_full.c CaptureFieldLight,
     * 3ds_video.c CtrVideo_SetFieldLight): the game's radius in GBA pixels,
     * the radius shown (eased towards it, so the circle opens smoothly), the
     * glow of Flash being used and a frame count for the flame's flicker.
     * fieldLightOn 0: no dark. */
    uint8_t fieldLightOn;
    uint16_t fieldLightRadius;
    float fieldLightShown;
    float fieldLightBurst;
    uint32_t fieldLightTicks;
    uint8_t surfBlobValid;
    int16_t surfBlobOffsetX;
    int16_t surfBlobOffsetY;
    struct DioramaObjectSnapshot surfBlob;
    uint16_t unfadedPalette[DIORAMA_FADED_PALETTE_ENTRIES];
    uint16_t fadedPalette[DIORAMA_FADED_PALETTE_ENTRIES];
    uint8_t tileGraphics[DIORAMA_TILE_GRAPHICS_SIZE];
    /* Neighbouring maps with other tiles (see DIORAMA_NEIGHBOUR_PAGES): page
     * p is neighbourPages[p - 1]. */
    struct DioramaNeighbourPage neighbourPages[DIORAMA_NEIGHBOUR_PAGES];
    /* Distant scenery (see DIORAMA_FAR_RADIUS): block (i, j) is
     * farBlocks[j * DIORAMA_FAR_BLOCKS + i], its top-left cell at map coordinates
     * (farOriginX + 2i, farOriginY + 2j) (as cells[].mapX); farOn 0: none. */
    uint8_t farOn;
    uint32_t farGeneration;
    int16_t farOriginX;
    int16_t farOriginY;
    struct DioramaFarBlock farBlocks[DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS];
};

void DioramaSnapshotExchange_Init(void);
void DioramaSnapshotExchange_Shutdown(void);
void DioramaSnapshotExchange_Publish(const struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshotExchange_CopyLatest(struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshot_CanRenderGrid(const struct DioramaSceneSnapshot *snapshot);
bool DioramaSnapshot_CanRenderFlatMap(const struct DioramaSceneSnapshot *snapshot);

void DioramaScene_Init(void);
void DioramaScene_BeginFrame(void);
void DioramaScene_PublishOverworld(void);
void DioramaScene_EndFrame(bool inBattle);
void DioramaScene_MarkMapChanged(void);
void DioramaScene_MarkCellDirty(int16_t mapX, int16_t mapY);

#ifdef DIORAMA_TEST
typedef void (*DioramaSnapshotTestPinnedHook)(void *userdata);
void DioramaSnapshotExchange_TestReset(void);
void DioramaSnapshotExchange_TestSetPinnedHook(DioramaSnapshotTestPinnedHook hook, void *userdata);
#endif

#endif
