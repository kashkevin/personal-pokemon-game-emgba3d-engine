#ifndef GUARD_DIORAMA_GL_AIR_H
#define GUARD_DIORAMA_GL_AIR_H

#include <stdbool.h>

/*
 * The light in the air over the 3D scene, from the 3DS project's voxel view
 * (pokeemerald-3Ds-dualscreen, MIT, see src/diorama/README.md):
 *
 *  - Sunlit dust (ctr_voxel.c, DrawMotes): specks of light drifting with the
 *    wind the way the shadows fall, glinting, in the world around the camera
 *    target (hidden behind a roof), added over what is behind.
 *  - Sun rays (DrawRays, MakeRays): faint warm shafts from the sun's corner of
 *    the picture, the top left, made of the dapple pattern stretched almost
 *    to lines, added over everything.
 *  - Bloom (3ds_video.c, VoxelBloomPrepare and VoxelBloomCompose): the
 *    brightest parts of the picture glow softly - a quarter-size copy keeping
 *    what is brighter than 0.85, stretched back and added.
 *
 * How strong each is comes from the weather light (gl_light.h): rays and dust
 * only in sunshine and fair weather, bloom more in fog; none indoors. The
 * added light follows the palette's fades and night tint. PKMN_DIORAMA_AIR=0
 * turns these three off; PKMN_DIORAMA_LIGHT=0 all of the light.
 */

/* After the scene (terrain, buildings, sprites), before the weather and the
 * game's 2D UI: the same camera as the scene's renderers. */
void DioramaGLAir_Draw(float cameraX, float cameraZ, float cameraPitch,
                       float focalLength, float aspectCorrection,
                       int outputWidth, int outputHeight);
void DioramaGLAir_Shutdown(void);

#endif
