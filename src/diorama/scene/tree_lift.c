#ifdef ENABLE_DIORAMA

/*
 * KNOWN_ISSUES MS-243: the height a large tree of the 3DS trees (tree_sprites.h) stands at, for the renderer
 * (gl_tree_sprites.c), the battle stage's search (gl_battle.c) and the geometry probe's cell dump
 * (gl_terrain_renderer.c). A file of its own: it reads the snapshot, the resolved cells and the modelled buildings,
 * which the tree tables in tree_sprites.c need not link.
 */

#include "diorama/buildings.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"
#include "diorama/tree_sprites.h"

/*
 * A large tree's four cells each hold a quarter of it (its crown's, its trunk's and the trunk's art's), and where the
 * rules put the cells on different levels - a terrace's edge or a ledge's line through the tree, some 4% of the
 * General tileset's large trees - each quarter stood on its own cell's height and the crown split by one to three
 * levels (KNOWN_ISSUES MS-243). The tree stands whole instead, at one height: the highest of its quarters' cells, so
 * that no part of it is sunk in the ground under it (its trunk, where the four meet, stands on all four cells); the
 * renderer draws the trunk and its art on the ground of a lower quarter down to that cell's own height
 * (gl_tree_sprites.c). The ground itself is not changed. Only the quarters in the snapshot's grid count (a tree at the
 * grid's edge, 40 cells from the camera, stands on the quarters there), and not a cell a modelled building owns
 * (its model draws that cell; the renderer leaves its quarter out).
 */
float DioramaTreeSprites_Lift(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaResolvedCell *resolvedCells, unsigned index)
{
    const struct DioramaCellSnapshot *cell;
    int first;
    int quarter;
    int gridX;
    int gridY;
    int k;
    float own;
    float highest;

    if (snapshot == NULL || resolvedCells == NULL || index >= snapshot->visibleCellCount)
        return 0.0f;
    cell = &snapshot->cells[index];
    first = DioramaTreeSprites_QuarterFirst(cell->treeSprite);
    if (first == DIORAMA_TREE_SPRITE_NONE)
        return 0.0f;
    quarter = cell->treeSprite - first;
    gridX = (int)(index % DIORAMA_GRID_WIDTH) - (quarter & 1);
    gridY = (int)(index / DIORAMA_GRID_WIDTH) - (quarter >> 1);
    own = resolvedCells[index].topHeight;
    highest = own;
    for (k = 0; k < 4; k++)
    {
        int x = gridX + (k & 1);
        int y = gridY + (k >> 1);
        unsigned other;
        const struct DioramaCellSnapshot *partner;

        if (x < 0 || y < 0 || x >= DIORAMA_GRID_WIDTH || y >= DIORAMA_GRID_HEIGHT)
            continue;
        other = (unsigned)(y * DIORAMA_GRID_WIDTH + x);
        if (other >= snapshot->visibleCellCount)
            continue;
        partner = &snapshot->cells[other];
        if (partner->treeSprite != first + k)
            continue;
        if ((partner->flags & DIORAMA_CELL_SOURCE_VALID)
         && DioramaBuildings_CellAt(partner->sourceLayoutId, partner->sourceMapX, partner->sourceMapY,
                                    NULL, NULL, NULL))
            continue;
        if (resolvedCells[other].topHeight > highest)
            highest = resolvedCells[other].topHeight;
    }
    return highest - own;
}

#endif // ENABLE_DIORAMA
