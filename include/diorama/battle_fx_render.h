#ifndef GUARD_DIORAMA_BATTLE_FX_RENDER_H
#define GUARD_DIORAMA_BATTLE_FX_RENDER_H

/*
 * Between Battle FX's game side (src/diorama/battlefx/bfx_game.c) and its
 * renderer (src/diorama/gl/gl_battle_fx.c); both on the main thread, at the
 * frame boundary (include/diorama/battle_fx.h).
 */

#include <stdbool.h>
#include <stdint.h>

struct BfxPacket;

/* The frame the game thread finished (NULL: none). The renderer copies it and
 * shows it with the next picture the GBA renderer draws: the picture drawn at
 * a frame boundary is the game's previous frame (OAM, scroll and palettes
 * reach the screen at the VBlank after the frame that set them), so the frame
 * taken here is shown one boundary later, with its own picture. */
void BattleFxGL_TakePacket(const struct BfxPacket *packet);
/* The frame shown with the picture being drawn: whether there is one, and how
 * many effect sprites in it the core still draws as GBA pixel art (none in
 * the v3.3 data: every graphics tag has an HD kit). */
bool BattleFxGL_Showing(void);
int BattleFxGL_ShowingPixelEffects(void);
/* ... and the BG layers whose effect picture it redraws (bits 1, 2). */
uint8_t BattleFxGL_ShowingHiddenBgs(void);

#endif // GUARD_DIORAMA_BATTLE_FX_RENDER_H
