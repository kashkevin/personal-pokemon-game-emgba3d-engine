#ifndef GUARD_DIORAMA_GL_TARGET_H
#define GUARD_DIORAMA_GL_TARGET_H

#include <stdbool.h>

/*
 * Where the 3D scene is drawn (the display settings, src/platform/sdl2.c).
 *
 * Straight into the window when the scene's render size is the window's own
 * and there is no multisampling: windowed mode, or a fullscreen mode at the
 * chosen resolution - the path used before these settings. Otherwise into an
 * offscreen framebuffer of its own size (the chosen resolution's height at the
 * window's shape, so nothing stretches), multisampled if asked, which is then
 * scaled into the window (bilinear). The pixel-art layers (the GBA picture,
 * text boxes, menus) are drawn after it at the window's own resolution, in
 * whole-number steps, so they stay sharp whatever the scene's size.
 *
 * Passes that leave the scene's framebuffer for one of their own (the sun's
 * depth map, the bloom) come back to DioramaGLTarget_Framebuffer().
 */
/* renderHeight 0: the window's own size; samples 0, 2, 4 or 8. */
void DioramaGLTarget_Configure(int renderHeight, int samples);
/* Before the scene: binds its framebuffer and gives its size. */
void DioramaGLTarget_Begin(int outputWidth, int outputHeight, int *renderWidth, int *renderHeight);
/* The framebuffer the scene is being drawn into (0: the window's). */
unsigned DioramaGLTarget_Framebuffer(void);
/* A framebuffer the scene drawn so far can be read from at its own size: the
 * one given, or with multisampling a resolved copy (a multisampled picture
 * cannot be scaled while it is read; the bloom reads it). */
unsigned DioramaGLTarget_ReadableFramebuffer(unsigned current);
/* After the scene: into the window (scaled, multisamples resolved); the
 * window's framebuffer is bound again. */
void DioramaGLTarget_End(int outputWidth, int outputHeight);
/* The last frame's scene size, for the frame-rate log and the benchmark. */
void DioramaGLTarget_GetLastSize(int *width, int *height, int *samples);
void DioramaGLTarget_Shutdown(void);

#endif // GUARD_DIORAMA_GL_TARGET_H
