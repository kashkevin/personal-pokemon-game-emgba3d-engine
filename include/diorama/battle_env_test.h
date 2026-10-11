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
 *                            wild2:<species>:<level>:<species>:<level>  (double)
 *                            legendary:<species>:<level>
 *                            trainer:<id>   double:<id>   two:<id>:<id>
 *   PKMN_TEST_BATTLE_DELAY   field frames to wait first (default 150)
 *   PKMN_TEST_CAPTURE        file stem: <stem>.png (the window) and
 *                            <stem>.json (what the battle camera sees) once
 *                            the action menu has been up for
 *   PKMN_TEST_CAPTURE_DELAY  battle frames (default 40)
 *   PKMN_TEST_EXIT=1         end the process after the capture
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

#endif

#endif // GUARD_DIORAMA_BATTLE_ENV_TEST_H
