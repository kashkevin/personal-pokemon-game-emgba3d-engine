#ifndef GUARD_DIORAMA_GL_SHADOW_H
#define GUARD_DIORAMA_GL_SHADOW_H

#include <stdbool.h>
#include <SDL2/SDL_opengl.h>

/*
 * Sun shadows for the Diorama scene: every frame the terrain, the trees and the
 * modelled buildings are drawn once more from the sun into a depth map, and the
 * terrain, building and sprite passes darken what the sun does not reach (a
 * sprite as a whole, from one point over its feet; sprites cast none).
 *
 * The sun stands where the 3DS port's baked lighting puts it (ZallaxDev/
 * pokeemerald-3Ds-dualscreen voxel_lighting.h, MIT): in the northwest, 45
 * degrees up, shadows falling southeast; the shade of a cast shadow is its
 * VOXEL_AMBIENT. The 3DS bakes the shadows into the meshes on the CPU; here the
 * GPU's depth map does it for everything on screen, every frame.
 *
 * PKMN_DIORAMA_SHADOWS=0 turns them off.
 */
#define DIORAMA_SHADOW_AMBIENT 0.70f
#define DIORAMA_SHADOW_TEXTURE_UNIT 3

bool DioramaGLShadow_Init(void);
bool DioramaGLShadow_IsEnabled(void);
/* Binds the depth map and fits the sun's view around the camera target.
 * False (and nothing to draw) when shadows are off. */
bool DioramaGLShadow_Begin(float cameraX, float cameraZ);
/* Back to the window's framebuffer and viewport. */
void DioramaGLShadow_End(int outputWidth, int outputHeight);
/* The sun's view-projection, column major (for glUniformMatrix4fv). */
const GLfloat *DioramaGLShadow_LightMatrix(void);
/* Binds the depth map to DIORAMA_SHADOW_TEXTURE_UNIT for the lit passes;
 * false when shadows are off this frame. */
bool DioramaGLShadow_BindForLighting(void);
void DioramaGLShadow_Shutdown(void);

#endif // GUARD_DIORAMA_GL_SHADOW_H
