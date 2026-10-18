#ifndef GUARD_DIORAMA_GL_OBJECT_RENDERER_H
#define GUARD_DIORAMA_GL_OBJECT_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaResolvedCell;

struct DioramaObjectMetrics
{
    uint16_t visibleObjects;
    uint16_t cachedFrames;
    uint16_t uploadedFrames;
    uint16_t drawCalls;
    uint16_t effects; /* field-effect sprites drawn (scene_snapshot.h struct DioramaEffectSnapshot) */
};

bool DioramaGLObjects_Init(void);
void DioramaGLObjects_Reset(void);
bool DioramaGLObjects_Sync(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaResolvedCell *resolvedCells);
void DioramaGLObjects_Draw(float frameAlpha, float cameraX, float cameraZ,
                           float cameraPitch, float focalLength,
                           float aspectCorrection);
const struct DioramaObjectMetrics *DioramaGLObjects_GetMetrics(void);
/* A player, their follower or another player hidden by the world in front
 * of them is drawn through it as a see-through ghost (on by default; photo
 * mode turns it off for its pictures). */
void DioramaGLObjects_SetSilhouettes(bool on);
/* How far the screen is faded out (0-1, the snapshot's screenFadeY / 16): the
 * walkers' shadows and see-through silhouettes are the renderer's own colours,
 * not the palette's, and fade with it (none on a black screen). */
void DioramaGLObjects_SetScreenFade(float amount);
/* The height of the ground under the player, as last seen (0 before any). */
float DioramaGLObjects_GetPlayerGround(void);
/* Where the player is drawn this frame: the middle of its cell in world
 * tiles (x east, z north), on the ground under it; false with no player. */
bool DioramaGLObjects_GetPlayerPose(float frameAlpha, float *x, float *groundY, float *z);
void DioramaGLObjects_Shutdown(void);

#endif
