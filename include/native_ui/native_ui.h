#ifndef GUARD_NATIVE_UI_H
#define GUARD_NATIVE_UI_H

/*
 * The native PC UI: the approved Gen 4/5-style interface drawn at the window's
 * resolution in place of the GBA menus' 240x160 presentation. The engine keeps
 * every rule and menu state machine; this layer reads its state at the frame
 * boundary, draws the design, hides the GBA picture of the screens it replaces
 * and turns mouse input into the engine's own button presses.
 *
 * Built with the 3D renderer (Makefile_pc DIORAMA=1 defines ENABLE_NATIVE_UI).
 * Everything runs on the main (render) thread; the capture runs while the game
 * thread is parked in VBlankIntrWait (src/platform/sdl2.c).
 *
 * Development switches (not for players; read once at start-up):
 *   PKMN_NATIVE_UI=0             the old GBA presentation everywhere (rollback)
 *   PKMN_NATIVE_UI_SCREENS=a,b   only these screens replaced (names: ui_screen.h)
 *   PKMN_NATIVE_UI_TESTCARD=1    the kit's test card over the game
 *   PKMN_NATIVE_UI_SIZE=small    UI size Small
 *   PKMN_NATIVE_UI_LOG=1         screen changes, clicks and timing in the log
 *   PKMN_NATIVE_UI_PROMPTS=pad   always the controller's prompts (or =keyboard)
 */
#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

/* Flags for the picture the game's own renderer shows (DioramaGL_SetPictureFilter). */
#define NATIVE_UI_HIDE_PICTURE  (1 << 0)  /* the whole 240x160 picture, its background and border */
#define NATIVE_UI_HIDE_UI_LAYER (1 << 1)  /* the field windows layer over the 3D view (BG0) */

/* Start-up and shutdown, with the window's GL context current. */
bool NativeUi_Init(SDL_Window *window);
void NativeUi_Shutdown(void);
bool NativeUi_Enabled(void);
/* Whether this battle's messages and menus are the native interface's (the GBA's text box, rows 112-159 of
 * the picture, is not shown): decided by the battle's type, so it holds from the battle's first frame. */
bool NativeUi_ReplacesBattleTextBox(void);

/* Frame boundary (game parked): which screen is up, its state, and the
 * picture policy for the frame about to be drawn. Called before VDraw. */
void NativeUi_CaptureFrame(void);
/* Frame boundary: the GBA buttons about to reach the game (mouse intents add
 * presses; a native-only menu row can hold keys back from the engine). */
uint16_t NativeUi_FilterKeys(uint16_t keys);
/* The gamepad buttons this frame (for the prompts' device). */
void NativeUi_NotePadKeys(uint16_t padKeys);
/* The pad's Y this frame: not one of the game's buttons; the UI's details view (as Tab). */
void NativeUi_NotePadDetails(bool down);

/* SDL events (main thread): returns true when the UI used the event (the
 * caller then skips its own handling, e.g. the 3D camera's mouse-wheel zoom). */
bool NativeUi_HandleEvent(const SDL_Event *event);

/* The picture policy for the compositor (NATIVE_UI_HIDE_*). */
int NativeUi_PictureFlags(void);

/* Draws the UI over the finished frame (the compositor's overlay, before the
 * PC's F10 menu), at the window's resolution with its framebuffer bound. */
void NativeUi_Draw(int outputWidth, int outputHeight);

/* Timing of the last UI pass (CPU milliseconds) for the performance log. */
float NativeUi_LastDrawMs(void);

#endif // GUARD_NATIVE_UI_H
