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

/* The core's frame to show with the picture being drawn now (NULL: none - the
 * picture's own effect sprites show), copied: the game side picks the frame
 * made for the game frame the picture shows (the picture drawn at a frame
 * boundary is the game's previous frame: OAM, scroll and palettes reach the
 * screen at the VBlank after the frame that set them). `restartTime`: the
 * particles' time runs on from this frame's from now. */
void BattleFxGL_ShowPacket(const struct BfxPacket *packet, bool restartTime);
/* bfx_game.c, before the renderer draws a present: a frame the core finished
 * since the boundary, for the picture being drawn, replaces an older one shown
 * in its place (the core ran behind). */
void BattleFx_LateFrame(void);
/* The frame shown with the picture being drawn: whether there is one, and how
 * many effect sprites in it the core still draws as GBA pixel art (none in
 * the v3.3 data: every graphics tag has an HD kit). */
bool BattleFxGL_Showing(void);
int BattleFxGL_ShowingPixelEffects(void);
/* ... and the BG layers whose effect picture it redraws (bits 1, 2). */
uint8_t BattleFxGL_ShowingHiddenBgs(void);

/* The game's battler sprites in the picture being drawn, read from OAM, OBJ
 * VRAM and the OBJ palettes exactly as the GBA renderer draws them: what the
 * HD layer needs to light, flash and glow the game's own Pokemon (and to keep
 * effect meshes behind them) without drawing them itself. */
#define BFX_GAME_SPRITE_MAX 64
struct BattleFxGameBattler
{
    bool shown;
    int x, y;              /* top-left of the sprite's box on the GBA screen (OAM x / y, wrapped) */
    int w, h;              /* the picture's size (64 x 64 for battlers) */
    bool affine, doubleSize, hflip, vflip;
    int m[4];              /* affine matrix, 8.8 fixed (screen -> texture), identity when not affine */
    int objMode, priority;
    uint8_t rgba[BFX_GAME_SPRITE_MAX * BFX_GAME_SPRITE_MAX * 4]; /* row 0 at the top, w x h used */
};
void BattleFxGL_TakeBattlers(const struct BattleFxGameBattler battlers[4]);

/* The drawing's cost for the animation with serial `serial` (the last one drawn when it is not
 * known): frames drawn, layers drawn, GPU time (timer queries; 0 samples when the driver has none)
 * and CPU time to submit the layers, in milliseconds. */
struct BattleFxRenderStats
{
    uint32_t serial;
    unsigned frames, layers, gpuSamples;
    double gpuMsTotal, gpuMsWorst, cpuMsTotal, cpuMsWorst;
};
void BattleFxGL_Stats(struct BattleFxRenderStats *stats);

#endif // GUARD_DIORAMA_BATTLE_FX_RENDER_H
