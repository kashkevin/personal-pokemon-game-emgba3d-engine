#ifndef GUARD_DIORAMA_GL_PROPS_H
#define GUARD_DIORAMA_GL_PROPS_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaResolvedCell;

/*
 * The world detail objects pass (graphics overhaul, phase 2, project-owned;
 * diorama/props.h): each prop instance drawn from its art, kept in a small
 * texture of its own (the art with its key colour and painted shadow left
 * out, recomposed when the palettes fade or the tiles animate).
 *
 * A slab is one mesh for every prop, instanced: the art's front and back and,
 * for each column and row of pixels, the sides between them, each fragment
 * cut where its pixel is transparent - so whatever the art, every opaque pixel
 * is a little block, as the voxel trees and pixel objects are, and the flowers'
 * animated tiles need no new geometry. Cards (grass, flowers) are the front
 * and back alone, their tops swaying. Lit as the terrain is (grade, weather
 * light, dapples, the sun's shadow map, haze); slabs cast shadows.
 */
bool DioramaGLProps_Init(void);
/* The props of a snapshot, on the ground its resolved cells give them. */
void DioramaGLProps_Sync(const struct DioramaSceneSnapshot *snapshot,
                         const struct DioramaResolvedCell *resolvedCells);
void DioramaGLProps_Draw(float cameraX, float cameraZ, float cameraPitch,
                         float focalLength, float aspectCorrection);
/* The slabs into the sun's depth map (gl_shadow.h), standing still. */
void DioramaGLProps_DrawShadow(void);
/* Changes whenever what DrawShadow draws changes. */
uint32_t DioramaGLProps_GetGeometryGeneration(void);
/* What the last Sync placed, for the log and the benchmark. */
unsigned DioramaGLProps_InstanceCount(void);
void DioramaGLProps_Shutdown(void);

#endif
