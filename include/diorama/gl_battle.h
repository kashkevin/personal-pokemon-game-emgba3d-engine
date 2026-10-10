#ifndef GUARD_DIORAMA_GL_BATTLE_H
#define GUARD_DIORAMA_GL_BATTLE_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

/*
 * The 3D battle's presentation (include/diorama/battle_scene.h): the world
 * seen from a battle camera on a stage near the player, the battle's own GBA
 * picture over it. Main thread only.
 */

bool DioramaGLBattle_Init(void);
void DioramaGLBattle_Shutdown(void);

/*
 * At the frame boundary, before the GBA picture is drawn (sdl2.c VDraw; the
 * game thread is parked): reads the game, starts and ends the 3D battle, steps
 * its camera one game frame, and returns the BGs the world stands in for in
 * this picture (gDioramaSceneLayers). 0 when the battle is drawn as ever.
 */
uint8_t DioramaGLBattle_BeginFrame(void);
/* The picture drawn (DioramaGL_UploadFrame): its RGBA, alpha the GBA picture's
 * coverage over the world (premultiplied), and the colour and cover of the
 * scene at each line's edges, to carry the picture out to a wide window. */
void DioramaGLBattle_UploadFrame(const uint32_t *rgba);

/*
 * Draws the whole frame when the 3D battle is up, true if it did; the
 * compositor draws as ever otherwise.
 */
bool DioramaGLBattle_Present(SDL_Window *window);

/* The battle is shown in 3D: the compositor keeps its overworld scene and
 * resources as they were. */
bool DioramaGLBattle_IsActive(void);

/*
 * A battle transition on the field (the 3DS project's RenderTransition): the
 * compositor draws the field in 3D under it if CanComposeField says it can be
 * composed - and if WantsField says this frame's picture was made for it, it
 * must draw a field, the last one if this frame's is refused. After the
 * field's ground and models it calls ComposeFieldBands, which lays them again
 * moved as the transition moves the GBA's map layers (true if it did); then
 * draws what the GBA draws as sprites or windows - the people, the weather, a
 * dark cave's light - and calls ComposeFieldPicture, which lays the
 * transition's picture over it all (the compositor then draws no 2D picture
 * of its own).
 */
bool DioramaGLBattle_CanComposeField(void);
bool DioramaGLBattle_WantsField(void);
bool DioramaGLBattle_ComposeFieldBands(int outW, int outH, int gameX, int gameY, int gameW, int gameH);
void DioramaGLBattle_ComposeFieldPicture(int outW, int outH, int gameX, int gameY, int gameW, int gameH);

#endif // GUARD_DIORAMA_GL_BATTLE_H
