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
/*
 * A 3D battle's arena in a cramped room (diorama/battle_arena.h, KNOWN_ISSUES V10): copies of the room's own models
 * set round the battle, each a placement's model with its north-west cell at map (x, y), standing at `lift`, in the
 * map's palettes (`paletteRow`: scene_snapshot.h atlasPage). Drawn after the map's own models while ShowArenaModels
 * is on (the battle's world), and never cleared away (gl_light.h DioramaGLLight_SetClearing). None: a count of 0.
 */
struct DioramaGLArenaModel
{
    unsigned placement;
    int16_t x, y;
};
void DioramaGLBuildings_SetArenaModels(const struct DioramaGLArenaModel *models, unsigned count, float lift,
                                       uint8_t paletteRow);
void DioramaGLBuildings_ShowArenaModels(bool shown);
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
