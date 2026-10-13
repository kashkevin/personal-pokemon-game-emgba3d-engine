#ifndef GUARD_DIORAMA_BATTLE_ENV_TEST_H
#define GUARD_DIORAMA_BATTLE_ENV_TEST_H

/*
 * The battle-environment test driver (TEST BUILDS ONLY: compiled only with
 * -DDIORAMA_BATTLE_ENV_TEST, never in a player build). It lets
 * tools/battle_env/survey.py photograph a battle on every map without anyone
 * at the keyboard:
 *
 *   PKMN_TEST_BATTLE         what to start once the field is ready:
 *                            wild:<species>:<level>
 *                            safari:<species>:<level>  (a Safari Zone battle: A throws balls)
 *                            wild2:<species>:<level>:<species>:<level>  (double)
 *                            legendary:<species>:<level>
 *                            trainer:<id>   double:<id>   two:<id>:<id>
 *                            partner:<id>:<id or 0>:<partner>  (Multi, in-game partner)
 *   PKMN_TEST_BATTLE_DELAY   field frames to wait first (default 150)
 *   PKMN_TEST_CAPTURE        file stem: <stem>.png (the window) and
 *                            <stem>.json (what the battle camera sees) once
 *                            the action menu has been up for
 *   PKMN_TEST_CAPTURE_DELAY  battle frames (default 40)
 *   PKMN_TEST_EXIT=1         end the process after the capture
 *   PKMN_TEST_AUTOPILOT=1    press the keys a person would: Start and A to Continue,
 *                            B through a map's scene and the battle's opening lines
 *   PKMN_TEST_TIMEOUT        seconds: no picture by then (a battle drawn in 2D has none),
 *                            say so and - with PKMN_TEST_EXIT - end the process (code 3)
 *   PKMN_TEST_FIGHT          seconds: after the picture, press A through the battle (the
 *                            first move, every line) and report its outcome once the field
 *                            is back; with PKMN_TEST_EXIT end there (0 won, 4 otherwise,
 *                            5 not over in time)
 *   PKMN_TEST_STRONG=1       the party at level 100 and healed when the field is up (in
 *                            memory only), so a fought battle is won
 *   PKMN_TEST_SHINY=1        the wild Pokemon is shiny (in memory only)
 *   PKMN_TEST_MEGA=1         the opponent trainer's first Pokemon that can Mega Evolve
 *                            holds its Mega Stone (in memory only): its AI Mega Evolves it
 *   PKMN_TEST_FREEZE_FIELD=1 the field's objects stand still while the battle waits to
 *                            start (a visible wild Pokemon cannot start another first)
 *   PKMN_TEST_FIRST_MOVE=<n>  with PKMN_TEST_STRONG, the party's first move in place of
 *                            Flamethrower (move number; in memory only)
 *
 * Nothing here changes a battle: it starts one the way the debug menu's
 * "Try battle" does and reads the picture.
 */

#if defined(ENABLE_DIORAMA) && defined(DIORAMA_BATTLE_ENV_TEST)

#include <stdbool.h>

/* overworld.c, CB1_Overworld (game thread) */
void DioramaBattleEnvTest_FieldTick(void);
/* battle_controller_player.c, HandleInputChooseAction (game thread) */
void DioramaBattleEnvTest_OnActionMenu(void);
/* gl_battle.c (render thread): the stem to capture this frame, or NULL */
const char *DioramaBattleEnvTest_CaptureStem(void);
void DioramaBattleEnvTest_CaptureWritten(void);
/* platform/sdl2.c, the keys read each frame (PKMN_TEST_AUTOPILOT) */
unsigned short DioramaBattleEnvTest_FilterKeys(unsigned short keys, unsigned int nowMs);

#endif

#endif // GUARD_DIORAMA_BATTLE_ENV_TEST_H
