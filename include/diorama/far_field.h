#ifndef GUARD_DIORAMA_FAR_FIELD_H
#define GUARD_DIORAMA_FAR_FIELD_H

/*
 * Distant scenery (graphics overhaul, project-owned): the snapshot's far
 * blocks (scene_snapshot.h, DIORAMA_FAR_RADIUS) as simple columns past the
 * scene's own cells - each block's ground, hill, tree or building as high as
 * the rules make its cell, coloured with the average of its art - so a wide
 * view shows the world going on into the haze instead of ending. Vertex
 * coloured (DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR) and unfaded: the terrain
 * shader fades them along the palette line, like the voxel trees.
 */
#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"
#include "diorama/terrain_mesh.h"

/* Room for every far cell's top (a tree's is a low pyramid, 4 triangles) and
 * a few sides; the build stops, and says so, past it. */
#define DIORAMA_FAR_MAX_VERTICES (DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS * 4 * 30)

struct DioramaFarColumn
{
    bool present;
    bool tree;     /* a canopy: each cell a little higher or lower, peaked */
    float top;     /* the column's top, in tiles (0: the ground) */
    uint32_t rgba; /* R, G, B, A bytes */
};

/* Where a layout cell has a modelled building (diorama/buildings.h,
 * DioramaBuildings_CellAt): its height there in tiles. Set by the renderer;
 * without it far buildings stay at their rules height (flat). */
typedef bool (*DioramaFarBuildingHeight)(uint16_t layoutId, int x, int y, float *top);
void DioramaFar_SetBuildingHeight(DioramaFarBuildingHeight buildingHeight);
/* A far block's column: its rules height (a tree at least a canopy's
 * height), its colour a little paler than the art (the air in between). */
void DioramaFar_Column(const struct DioramaFarBlock *block, int blockX, int blockY,
                       struct DioramaFarColumn *column);
/* The mesh comes in sectors of 7 x 7 blocks, each with its bounds, so a view
 * draws only the ones it can see (the Adventure camera hardly any). */
#define DIORAMA_FAR_SECTOR_BLOCKS 7
#define DIORAMA_FAR_SECTORS_ACROSS (DIORAMA_FAR_BLOCKS / DIORAMA_FAR_SECTOR_BLOCKS)
#define DIORAMA_FAR_SECTORS (DIORAMA_FAR_SECTORS_ACROSS * DIORAMA_FAR_SECTORS_ACROSS)

struct DioramaFarSector
{
    uint32_t first;
    uint32_t count;
    struct DioramaTerrainBounds bounds;
};

/* The triangles of a snapshot's far blocks, leaving out the cells the
 * snapshot's grid holds, sector by sector (`sectors` may be NULL). Returns
 * the vertex count; *complete is false when `capacity` ran out. */
uint32_t DioramaFar_BuildMesh(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaTerrainVertex *vertices, uint32_t capacity,
                              struct DioramaFarSector *sectors, bool *complete);

#endif
