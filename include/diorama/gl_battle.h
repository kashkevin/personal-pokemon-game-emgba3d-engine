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
 * Draws the whole frame when the 3D battle is up (or holding the world for a
 * moment after it), true if it did; the compositor draws as ever otherwise.
 */
bool DioramaGLBattle_Present(SDL_Window *window);

/* The battle is shown in 3D: the compositor keeps its overworld scene and
 * resources as they were. */
bool DioramaGLBattle_IsActive(void);
/*
 * A battle drawn in 3D has just ended on this map and the field has not been
 * drawn in 3D since: the compositor draws the field fading back in from the
 * battle in 3D as well (its palettes fade the world as they fade the 2D
 * picture), instead of the flat picture standing in until the fade is over.
 */
bool DioramaGLBattle_IsReturning(uint8_t mapGroup, uint8_t mapNum, uint16_t mapLayoutId);
/* The compositor has drawn the field in 3D from an ordinary scene: the return
 * from the battle is over. */
void DioramaGLBattle_FieldDrawn(void);
/* Over a field drawn from its steady palettes (`unfaded`), the fade that the
 * game's palettes (`faded`, RGB555) show: the whole window towards the fade's
 * colour, as the battle's world is faded. */
void DioramaGLBattle_DrawFieldFade(const uint16_t *faded, const uint16_t *unfaded, unsigned count,
                                   int outW, int outH);

#endif // GUARD_DIORAMA_GL_BATTLE_H
