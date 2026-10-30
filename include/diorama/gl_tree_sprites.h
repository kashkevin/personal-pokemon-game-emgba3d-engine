#ifndef GUARD_DIORAMA_GL_TREE_SPRITES_H
#define GUARD_DIORAMA_GL_TREE_SPRITES_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaResolvedCell;

/*
 * The 3DS project's trees (tree_sprites.h; pokeemerald-3Ds-dualscreen
 * voxel_tree.c EmitCell and EmitSmallCell, MIT): for each cell the snapshot
 * published as part of a tree, the trunk's art laid on the ground and its part
 * of the tree standing where the 3DS port stands its crown, a cut-out tilted
 * 50 degrees with its high end to the north: since step 16 a solid crown of
 * voxels made from that art and a trunk under it (gl_tree_sprites.c; the
 * cut-outs with PKMN_DIORAMA_TREE_CARDS=1). Lit like the terrain (grade, weather
 * light, dapples, shadow map, haze), casting shadows, following the palette's
 * fades and night tint as the voxel trees do. Nothing to draw unless the
 * snapshot has them (DioramaTreeSprites_Enabled).
 */
bool DioramaGLTreeSprites_Init(void);
/* Whether the trees stand as solids (not the cut-outs): the geometry probe's record. */
bool DioramaGLTreeSprites_Volumes(void);
/* The trees of a snapshot, on the ground its resolved cells give them. */
void DioramaGLTreeSprites_Sync(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaResolvedCell *resolvedCells);
void DioramaGLTreeSprites_Draw(float cameraX, float cameraZ, float cameraPitch,
                               float focalLength, float aspectCorrection);
/* The crowns into the sun's depth map (gl_shadow.h). */
void DioramaGLTreeSprites_DrawShadow(void);
/* Changes whenever what DrawShadow draws changes. */
uint32_t DioramaGLTreeSprites_GetGeometryGeneration(void);
void DioramaGLTreeSprites_Shutdown(void);

#endif
