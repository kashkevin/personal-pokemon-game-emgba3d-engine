#ifndef GUARD_DIORAMA_BUILDINGS_H
#define GUARD_DIORAMA_BUILDINGS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Buildings modelled from their own drawing, as ZallaxDev/pokeemerald-3Ds-dualscreen
 * models them (its 3ds_port/src/voxel/voxel_building.c, MIT; see
 * src/diorama/README.md).
 *
 * tools/diorama_buildings/gen_voxel_buildings.py reads every building off its
 * art and refuses a model that does not reproduce that art pixel for pixel in
 * the GBA's projection; export_gl.py compiles the result into the program.
 * Nothing is modelled at run time: the renderer copies each placed model's
 * triangles into the scene, draws the plain ground under the cells the model
 * owns (DioramaBuildings_CellAt) and keeps the Diorama's own building
 * extrusion away from those cells.
 *
 * Layouts are the game's layout ids (gMapHeader.mapLayoutId); cell
 * coordinates are within the layout.
 */

/* A cell whose ground is its own metatile (less the quarters of its upper
 * layer the model stands for), not a ground metatile of the placement. */
#define DIORAMA_BUILDING_OWN_GROUND 0xFFFFu
#define DIORAMA_BUILDING_TRANSPARENT 255u

struct DioramaBuildingVertex
{
    float x, y, z; /* tiles from the placement's top-left cell; y is up */
    float u, v;    /* pixels of the model's page */
    float shade;   /* the face shade the model was drawn with */
};

struct DioramaBuildingPlacementInfo
{
    uint16_t layout;
    uint16_t x, y;          /* top-left cell in the layout */
    uint8_t width, height;  /* cells */
    uint16_t page;
    int16_t pageX, pageY;   /* where the model's drawing sits on the page */
    uint32_t firstVertex, vertexCount;            /* the model */
    uint32_t firstGroundVertex, groundVertexCount; /* its ground patches (6 a cell) */
};

bool DioramaBuildings_Init(void);
bool DioramaBuildings_IsAvailable(void);

/* The placements of a layout: *first is the index of the first. */
unsigned DioramaBuildings_LayoutPlacements(uint16_t layout, unsigned *first);
bool DioramaBuildings_GetPlacement(unsigned index, struct DioramaBuildingPlacementInfo *info);

struct DioramaSceneSnapshot;

/*
 * Which placements stand: one whose cells a script or the game has changed since the
 * layout's drawing (DIORAMA_CELL_CHANGED in the snapshot: a door opened, a light went out,
 * a barrier switched, a floor was generated) does not - the terrain draws those cells as
 * they are now. Validate recomputes it from a snapshot (FlattenResolvedCells does too) and
 * returns a hash of the placements left out; CellAt and FrontAt skip them.
 */
uint32_t DioramaBuildings_Validate(const struct DioramaSceneSnapshot *snapshot);
bool DioramaBuildings_PlacementStands(unsigned index);
const struct DioramaBuildingVertex *DioramaBuildings_Vertices(void);

/*
 * The ground under layout cell (x, y) when a model owns it: *ground is the
 * metatile to draw flat there (or DIORAMA_BUILDING_OWN_GROUND), *quarters
 * which quarters of the cell's upper layer the model stands for (bit
 * 2 * row + column), *top the model's height there in tiles.
 */
bool DioramaBuildings_CellAt(uint16_t layout, int x, int y,
                             uint16_t *ground, uint8_t *quarters, float *top);

/*
 * The face of a model a walker in layout cell (x, y) stands before: the
 * southernmost upright face looking south, of any model that reaches the cell,
 * crossing x0..x1 at `height` tiles over the model's base. In tiles of the
 * layout, cell (x, y) spanning x..x+1 and y..y+1 (z grows south, as y does).
 * False when there is none. A piece of furniture's front, a building's wall.
 */
bool DioramaBuildings_FrontAt(uint16_t layout, int x, int y, float x0, float x1,
                              float height, float *z);

struct DioramaSceneSnapshot;
struct DioramaResolvedCell;
/*
 * Turns every visible cell a model owns into plain flat ground drawn with the
 * placement's ground metatile, so the Diorama's own building extrusion, roofs
 * and pixel objects stay away from it and objects stand on the ground there.
 * `resolved` is index-aligned with snapshot->cells.
 */
void DioramaBuildings_FlattenResolvedCells(const struct DioramaSceneSnapshot *snapshot,
                                           struct DioramaResolvedCell *resolved,
                                           unsigned count);

unsigned DioramaBuildings_PageCount(void);
/* A page's texels, row by row from the top: BG palette slots (palette * 16 +
 * colour) or DIORAMA_BUILDING_TRANSPARENT. */
bool DioramaBuildings_Page(unsigned page, unsigned *width, unsigned *height,
                           const uint8_t **slots);

#endif // GUARD_DIORAMA_BUILDINGS_H
