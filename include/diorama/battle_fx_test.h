#ifndef GUARD_DIORAMA_BATTLE_FX_TEST_H
#define GUARD_DIORAMA_BATTLE_FX_TEST_H

/*
 * The Battle FX test driver (TEST BUILDS ONLY: compiled only with
 * -DBATTLEFX_TEST, never in a player build; with -DDIORAMA_BATTLE_ENV_TEST,
 * whose driver starts the battle). At the battle's action menu it plays move
 * animations one after another through the game's own animation system - the
 * way Controller_DoMoveAnimation does (DoMoveAnim, then gAnimScriptCallback
 * every frame until the animation ends) - and for each one records:
 *
 *   - the game's frame count and the Battle FX core's;
 *   - parity: every frame, the effect sprites the game has on screen (graphics
 *     tag and position) against the ones the core's replay has, frame by frame;
 *   - window captures at chosen frames of the core's animation;
 *   - the core's cost (begin, a frame).
 *
 *   PKMN_BFX_TEST_MOVES     MOVE_A,MOVE_B,... or "all" (every move in the core's data)
 *   PKMN_BFX_TEST_OUT       folder for report.jsonl, summary.json and the captures (default bfx_test)
 *   PKMN_BFX_TEST_SHOTS     frames of the core's animation to capture: n,n,... or "auto" (a quarter,
 *                           half and three quarters through) or "none" (default auto)
 *   PKMN_BFX_TEST_VIEW      hd (default) or gba: the core still runs (parity, frame numbers), but the
 *                           GBA effect sprites stay and nothing HD is drawn: the reference pictures
 *   PKMN_BFX_TEST_ATTACKER, PKMN_BFX_TEST_TARGET   battlers (default 0 and 1)
 *   PKMN_BFX_TEST_TURN      gAnimMoveTurn (default 0)
 *   PKMN_BFX_TEST_DELAY     action-menu frames before the first animation (default 40)
 *   PKMN_BFX_TEST_EXIT=1    end the process when every animation has played (code 0; 6 if one failed)
 *
 * Nothing here exists in a player build. It changes the battle only as far as
 * playing an animation does (sprites, palettes, the animation variables).
 */

#include <stdbool.h>
#include <stdint.h>

#if defined(ENABLE_DIORAMA) && defined(BATTLEFX_TEST)

struct BfxHost;

/* battle_controller_player.c, HandleInputChooseAction (game thread): true while the
 * driver plays its animations (the menu waits). */
bool BattleFxTest_OnActionMenu(void);
/* bfx_game.c, the end of a game frame in which the core stepped (game thread). */
void BattleFxTest_AfterStep(struct BfxHost *host, uint32_t serial, int coreFrame);
/* gl_battle_fx.c, after the front layer (main thread): the frame shown. */
void BattleFxTest_AfterDraw(uint32_t serial, int frame, bool showing, int outW, int outH);
/* Whether the HD layer is shown (PKMN_BFX_TEST_VIEW). */
bool BattleFxTest_ViewHd(void);

#endif

#endif // GUARD_DIORAMA_BATTLE_FX_TEST_H
