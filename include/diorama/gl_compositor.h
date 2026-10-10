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
/* The camera's framing (F7, pokeemerald.cfg cameraStyle, PKMN_DIORAMA_CAMERA):
 * the Diorama's own wide view or the 3DS project's. Setting it returns the
 * pitch and zoom to the framing's own. */
#define DIORAMA_CAMERA_WIDE 0
#define DIORAMA_CAMERA_3DS 1
void DioramaGL_SetCameraStyle(int style);
int DioramaGL_GetCameraStyle(void);
/* The pitch and zoom keys' steps away from the framing's own, so they can be
 * kept between runs (pokeemerald.cfg cameraPitch, cameraZoom). */
void DioramaGL_GetCameraSteps(int *pitchSteps, int *zoomSteps);
void DioramaGL_SetCameraSteps(int pitchSteps, int zoomSteps);
void DioramaGL_Shutdown(void);

#endif
