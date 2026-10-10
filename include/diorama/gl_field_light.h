#ifndef GUARD_DIORAMA_GL_FIELD_LIGHT_H
#define GUARD_DIORAMA_GL_FIELD_LIGHT_H

#include <stdbool.h>

#include "diorama/scene_snapshot.h"

/*
 * The dark of a cave that needs Flash, and of the Battle Pyramid, in the 3D
 * view, as the 3DS project draws it over its voxel world (pokeemerald-3Ds-
 * dualscreen 3ds_video.c VoxelFlashLight and MakeFlashLight, ctr_voxel.c
 * CtrVoxel_PlayerLightSpot; MIT, see src/diorama/README.md).
 *
 * On the GBA the game opens a circle of window 0 round the player and hides
 * everything outside it but the text layer. The 3D scene has no such window,
 * so without this a dark cave was drawn fully lit. Here the game's radius
 * (DioramaSceneSnapshot.fieldLight*) is measured on the floor round the
 * player, so the camera's foreshortening makes it an ellipse, with a soft
 * edge that warms to amber as it falls into the dark, a faint warm pool
 * round the player, a slow flicker, and a glow when Flash is used while the
 * circle opens. The dark follows the palette's fades.
 */
/* After the scene and the light in the air (gl_air.h), before the game's 2D
 * UI, with the scene's camera; the player as the sprites draw it. */
void DioramaGLFieldLight_Draw(const struct DioramaSceneSnapshot *snapshot, float frameAlpha,
                              float cameraX, float cameraZ, float cameraPitch,
                              float focalLength, float aspectCorrection,
                              int outputWidth, int outputHeight);
void DioramaGLFieldLight_Shutdown(void);

#endif
