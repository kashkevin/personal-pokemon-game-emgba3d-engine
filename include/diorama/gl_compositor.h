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
void DioramaGL_AdjustCameraZoom(int steps);
void DioramaGL_AdjustCameraPitch(int steps);
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
/* The field's camera of that frame (no interpolation): position, pitch and
 * focal length as DioramaGLTerrain_Draw takes them. */
void DioramaGL_GetFieldCamera(float *cameraX, float *cameraZ, float *pitch, float *focalLength);
/* Draws that world - sun shadows, terrain, trees and models, nobody in it -
 * from the camera given, with the eye in gDioramaCameraRig. */
void DioramaGL_DrawBattleWorld(float cameraX, float cameraZ, float pitch, float focalLength,
                               float aspectCorrection, int outputWidth, int outputHeight);
/* The texture holding the GBA picture last uploaded (DioramaGL_UploadFrame). */
unsigned DioramaGL_GetFrameTexture(void);

#endif
