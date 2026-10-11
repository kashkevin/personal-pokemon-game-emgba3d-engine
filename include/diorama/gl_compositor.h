#ifndef GUARD_DIORAMA_GL_COMPOSITOR_H
#define GUARD_DIORAMA_GL_COMPOSITOR_H

#include <stdbool.h>
#include <SDL2/SDL.h>

#include "global.h"

bool DioramaGL_Init(SDL_Window *window, u8 backgroundCount);
void DioramaGL_LoadArtwork(void);
void DioramaGL_UploadFrame(const u32 *argb8888);
void DioramaGL_Present(u8 background, bool border, bool integerScale, float frameAlpha);
void DioramaGL_SetVSync(bool enabled);
void DioramaGL_ToggleTerrainDebug(void);
void DioramaGL_ToggleEnabled(void);
void DioramaGL_CycleSurveyView(void);
void DioramaGL_EndSurvey(void);
void DioramaGL_RequestSurveyCapture(void);
/* The camera (diorama/camera.h): zoom steps (+ in) and tilt steps (+
 * steeper) of the current camera mode, within its limits. */
void DioramaGL_AdjustCameraZoom(int steps);
void DioramaGL_AdjustCameraPitch(int steps);
/* The camera mode (enum DioramaCameraMode): Adventure (default) and Diorama
 * (F7, kept in pokeemerald.cfg cameraMode), Photo (F9, never kept). */
void DioramaGL_SetCameraMode(int mode);
int DioramaGL_GetCameraMode(void);
/* Each mode's zoom and tilt steps, kept between runs (cameraZoom and
 * cameraTilt in pokeemerald.cfg, per mode). */
void DioramaGL_GetCameraSteps(int mode, int *zoomSteps, int *tiltSteps);
void DioramaGL_SetCameraSteps(int mode, int zoomSteps, int tiltSteps);
/* Photo mode: moves the camera by whole tiles (east +x, north +z). */
void DioramaGL_PhotoPan(int dx, int dz);
/* The PC's own interface (src/platform/sdl2.c): called last in every frame
 * presented, at the window's resolution with its framebuffer bound. */
void DioramaGL_SetOverlay(void (*overlay)(int outputWidth, int outputHeight));
/* The last frame presented drew the 3D scene (the benchmark counts them). */
bool DioramaGL_LastFrameWas3D(void);
/* The texture holding the GBA picture last uploaded (DioramaGL_UploadFrame),
 * brought up to date first: the picture is uploaded only when something draws
 * it (performance work 2026-10-11), so any code drawing with this texture -
 * the 3D battle's layout - gets it through here. */
unsigned DioramaGL_GetFrameTexture(void);
/* The OpenGL renderer, its vendor and version, for the benchmark's log. */
void DioramaGL_DescribeRenderer(char *buffer, size_t size);
void DioramaGL_Shutdown(void);

#endif
