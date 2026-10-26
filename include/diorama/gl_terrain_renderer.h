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
/* The sun's depth pass (diorama/gl_shadow.h): every resident chunk and tree. */
void DioramaGLTerrain_DrawShadow(GLuint atlasTexture, GLuint baseAtlasTexture,
                                 GLuint foregroundAtlasTexture);
const struct DioramaTerrainMetrics *DioramaGLTerrain_GetMetrics(void);
/* Changes whenever what DrawShadow draws changes. */
uint32_t DioramaGLTerrain_GetGeometryGeneration(void);
/* How the palette's night tint and fades move a colour this frame, per channel
 * (faded = scale x unfaded + offset), fitted from the trees' palette. */
void DioramaGLTerrain_GetPaletteLine(float scale[3], float offset[3]);
/* Whether this frame draws distant scenery (diorama/far_field.h) past the
 * scene's cells. */
bool DioramaGLTerrain_FarShown(void);
void DioramaGLTerrain_Shutdown(void);

#endif
