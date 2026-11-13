#ifndef GUARD_DIORAMA_GL_TERRAIN_RENDERER_H
#define GUARD_DIORAMA_GL_TERRAIN_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL_opengl.h>

#include "diorama/scene_snapshot.h"
#include "diorama/rules.h"

struct DioramaTerrainMetrics
{
    uint16_t residentChunks;
    uint16_t activeChunks;
    uint16_t rebuiltChunks;
    uint16_t visibleChunks;
    uint16_t culledChunks;
    uint32_t vertices;
    uint32_t triangles;
    uint16_t drawCalls;
    uint16_t visibleTreeInstances;
    uint32_t treeModelVertices;
    /* The last sync's work (PKMN_PERF_FRAME_LOG): chunk inputs built, and the
     * microseconds in inputs, meshes, uploads and the sync's preparation. */
    uint16_t inputBuilds;
    uint32_t inputMicros;
    uint32_t meshMicros;
    uint32_t uploadMicros;
    uint32_t prepareMicros;
};

bool DioramaGLTerrain_Init(void);
void DioramaGLTerrain_Reset(void);
/* The chunk cache's size (the memory budget's; at least the 100 chunks the grid
 * spans, at most 1024). Each chunk is one vertex buffer, about 100 to 250 KB of
 * graphics memory. */
void DioramaGLTerrain_SetCacheChunks(unsigned chunks);
unsigned DioramaGLTerrain_CacheChunks(void);
bool DioramaGLTerrain_Sync(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaResolvedCell *resolvedCells);
void DioramaGLTerrain_Draw(GLuint atlasTexture, GLuint baseAtlasTexture,
                           GLuint foregroundAtlasTexture, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection, bool debug);
/* A 3D battle's arena in a cramped room (diorama/battle_arena.h, KNOWN_ISSUES V10): its floor and walls, drawn with
 * the terrain by the terrain's own shader (the room's light and air) after the chunks, in the scene's coordinates,
 * and never cleared away (gl_light.h DioramaGLLight_SetClearing). SetArena keeps a copy of the vertices (none: a
 * count of 0); ShowArena draws them or not (the battle's world shows them, the field never). */
struct DioramaTerrainVertex; /* terrain_mesh.h */
void DioramaGLTerrain_SetArena(const struct DioramaTerrainVertex *vertices, uint32_t count);
void DioramaGLTerrain_ShowArena(bool shown);
/* The sun's depth pass (diorama/gl_shadow.h): every resident chunk and tree. */
void DioramaGLTerrain_DrawShadow(GLuint atlasTexture, GLuint baseAtlasTexture,
                                 GLuint foregroundAtlasTexture);
const struct DioramaTerrainMetrics *DioramaGLTerrain_GetMetrics(void);
/* Changes whenever what DrawShadow draws changes. */
uint32_t DioramaGLTerrain_GetGeometryGeneration(void);
/* Changes when the view the voxel trees' levels of detail were measured from for the shadow map has been left (a
 * zoom, a tilt): the shadow map is to be drawn again, so each tree's shadow stays its own model's (KNOWN_ISSUES
 * MS-244). Folded into the shadow map's caster generation. */
uint32_t DioramaGLTerrain_TreeLodGeneration(void);
/* How the palette's night tint and fades move a colour this frame, per channel
 * (faded = scale x unfaded + offset), fitted from the trees' palette. */
void DioramaGLTerrain_GetPaletteLine(float scale[3], float offset[3]);
/* Whether this frame draws distant scenery (diorama/far_field.h) past the
 * scene's cells. */
bool DioramaGLTerrain_FarShown(void);
#ifdef DIORAMA_BATTLE_ENV_TEST
#include <stdio.h>
/* The geometry probe (diorama/geometry_probe.h), test builds only: every chunk the last sync asked for built, and
 * each cell of the scene as the terrain mesh is built from it, a line each (the columns: the first line). */
bool DioramaGLTerrain_TestSettled(const struct DioramaSceneSnapshot *snapshot);
unsigned DioramaGLTerrain_TestDumpCells(const struct DioramaSceneSnapshot *snapshot,
                                        const struct DioramaResolvedCell *resolvedCells, FILE *file);
#endif
void DioramaGLTerrain_Shutdown(void);

#endif
