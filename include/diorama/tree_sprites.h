#ifndef GUARD_DIORAMA_TREE_SPRITES_H
#define GUARD_DIORAMA_TREE_SPRITES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The 3DS project's trees (pokeemerald-3Ds-dualscreen voxel_tree.c, MIT; see
 * tools/diorama_trees_3ds/README.md), an alternative to the Diorama's voxel
 * trees (F8, pokeemerald.cfg treeStyle=1, PKMN_DIORAMA_TREES=3ds): on outdoor maps of the General
 * tileset each tree cell is ground with the tree's trunk laid on it and a
 * cut-out crown standing on the trunk, and the cells its old canopy covered
 * get their ground back.
 *
 * Snapshot side (scene_snapshot.c): the tree's cells are published as ground,
 * so the rules and the terrain treat them as such, and each keeps what part of
 * a tree it was (DioramaCellSnapshot.treeSprite); gl_tree_sprites.c draws the
 * trees from that.
 */
/* What a cell was (DioramaCellSnapshot.treeSprite); 0, as a cleared cell
 * has it, for none. */
#define DIORAMA_TREE_SPRITE_NONE 0
#define DIORAMA_TREE_SPRITE_QUARTER 1       /* 1-4: a large tree's 2x2 quarter, 1 + row * 2 + column */
#define DIORAMA_TREE_SPRITE_SMALL 5         /* the General tileset's small tree */
#define DIORAMA_TREE_SPRITE_SMALL_DEWFORD 6 /* Dewford Town's small tree */

/* DioramaTreeSprites_Part's answers, the 3DS port's VoxelTree_Part. */
#define DIORAMA_TREE_PART_NONE (-1)
#define DIORAMA_TREE_PART_SMALL 4

bool DioramaTreeSprites_Enabled(void);
/* Takes effect from the next snapshot, which then redraws every cell. */
void DioramaTreeSprites_SetEnabled(bool enabled);
/* VoxelTree_Part: a large tree's quarter (0-3, row * 2 + column),
 * DIORAMA_TREE_PART_SMALL or DIORAMA_TREE_PART_NONE; `dewford` when the map's
 * secondary tileset is Dewford's (ids from 0x200 are the secondary tileset's). */
int DioramaTreeSprites_Part(bool dewford, uint16_t metatileId);
/* VoxelTree_GroundMetatile: what a cell the old canopy covered shows instead
 * (itself when it is not such a cell). */
uint16_t DioramaTreeSprites_GroundMetatile(bool dewford, uint16_t metatileId);
/* The ground under a tree's own cells (the trunk art covers it all). */
uint16_t DioramaTreeSprites_TrunkGround(bool dewford);

#endif
