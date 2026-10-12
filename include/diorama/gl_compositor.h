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
/* The OpenGL renderer, its vendor and version, for the benchmark's log. */
void DioramaGL_DescribeRenderer(char *buffer, size_t size);
void DioramaGL_Shutdown(void);

/*
 * For the 3D battle (gl_battle.c), which looks at the field's world from a
 * camera of its own while the compositor keeps it as it was.
 */
struct DioramaSceneSnapshot;
struct DioramaResolvedCell;
SDL_Window *DioramaGL_GetWindow(void);
bool DioramaGL_GetIntegerScale(void);
/* The automatic 3D view is chosen (not the classic view, F4). */
bool DioramaGL_IsAutoMode(void);
/* The last frame presented drew the field in 3D (gl_battle.c composes a
 * battle transition over it only then). */
bool DioramaGL_DrewField3D(void);
/* The field of that map as last drawn in 3D - or, if it never was (a battle
 * straight out of a scripted scene), as last seen, built now - its snapshot
 * and resolved cells, whose terrain, atlases and models are on the GPU; false
 * if there is none. */
bool DioramaGL_GetBattleWorld(u8 mapGroup, u8 mapNum, u16 mapLayoutId,
                              const struct DioramaSceneSnapshot **snapshot,
                              const struct DioramaResolvedCell **resolved);
/* The field's camera as last drawn (diorama/camera.h's view): the target as
 * DioramaGLTerrain_Draw takes it, the pitch, the focal length, and the eye
 * (terrain_mesh.h DioramaCameraRig: its height over y = 0, how far south of
 * the target). A field never drawn yet gets the Adventure camera's framing
 * round the game camera's focus. */
void DioramaGL_GetFieldCamera(float *cameraX, float *cameraZ, float *pitch, float *focalLength,
                              float *eyeHeight, float *eyeDistance);
/* Draws that world - sun shadows, terrain, trees and models, nobody in it -
 * from the camera given, with the eye in gDioramaCameraRig. */
void DioramaGL_DrawBattleWorld(float cameraX, float cameraZ, float pitch, float focalLength,
                               float aspectCorrection, int outputWidth, int outputHeight);
/* The weather's rain, snow, ash, sand or bubbles over that world, drawn as the
 * field draws its own over the 3D view, for `weather` (constants/weather.h;
 * DioramaBattleFrame.weather), at `opacity`, over the window; false when that
 * weather has none to draw. */
bool DioramaGL_DrawBattleWeather(uint8_t weather, int outputWidth, int outputHeight, float opacity);
/* That world with these tiles (the field's BG tiles, animated by
 * DioramaBattle_AnimateFieldTiles): its atlases updated where the tiles
 * changed; true when what it shows changed. */
bool DioramaGL_SetBattleTiles(const uint8_t *tileGraphics);
/* The texture holding the GBA picture last uploaded (DioramaGL_UploadFrame). */
unsigned DioramaGL_GetFrameTexture(void);
/* The field's scene as the 3D view last drew it, its snapshot and resolved
 * cells (for the battle's stage survey, a test tool); false if there is none. */
bool DioramaGL_GetFieldScene(const struct DioramaSceneSnapshot **snapshot,
                             const struct DioramaResolvedCell **resolved);

#endif
