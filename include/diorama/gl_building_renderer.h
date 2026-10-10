#ifndef GUARD_DIORAMA_GL_BUILDING_RENDERER_H
#define GUARD_DIORAMA_GL_BUILDING_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

struct DioramaSceneSnapshot;
struct DioramaResolvedCell;

/*
 * Draws the modelled buildings (diorama/buildings.h) of the maps on screen,
 * from their palette-slot pages and the game's live BG palettes, so fades and
 * the day/night tint reach them as they reach the terrain.
 */
bool DioramaGLBuildings_Init(void);
void DioramaGLBuildings_Sync(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaResolvedCell *resolvedCells);
void DioramaGLBuildings_Draw(float cameraX, float cameraZ, float cameraPitch,
                             float focalLength, float aspectCorrection);
/* The sun's depth pass (diorama/gl_shadow.h). */
void DioramaGLBuildings_DrawShadow(void);
/* Changes whenever the triangles DrawShadow draws change. */
uint32_t DioramaGLBuildings_GetGeometryGeneration(void);
void DioramaGLBuildings_Shutdown(void);

struct DioramaBuildingMetrics
{
    uint32_t instances;
    uint32_t placements;
    uint32_t vertices;
    uint32_t drawCalls;
};
const struct DioramaBuildingMetrics *DioramaGLBuildings_GetMetrics(void);

#endif // GUARD_DIORAMA_GL_BUILDING_RENDERER_H
