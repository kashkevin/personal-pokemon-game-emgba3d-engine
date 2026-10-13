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
 *
 * For the battle sprite tests (platform/battle_sprites.h):
 *   PKMN_TEST_PARTY          the player's party instead: species:level[:flags],...
 *                            (flags: s shiny, f female, m male, i<item id> held item,
 *                            v<move id> first move)
 *   PKMN_TEST_FOE            flags for the foes wild/wild2/legendary make: flags,flags
 *   PKMN_TEST_BAG            items added to the bag: item:count,... (a Mega Ring...)
 *   PKMN_TEST_KEYS           keys pressed once the action menu is up: frame:KEY,...
 *                            (A B START SELECT UP DOWN LEFT RIGHT L R, three frames each)
 *   PKMN_TEST_CAPTURE_EVERY  with _COUNT: a series of captures <stem>-NN, one every that
 *   PKMN_TEST_CAPTURE_COUNT  many drawn frames from PKMN_TEST_CAPTURE_DELAY after the
 *   PKMN_TEST_CAPTURE_FROM   action menu came up (or after the battle was first drawn: battle)
 *   PKMN_TEST_PARADE         first:last - at the action menu, both sides show each species
 *   PKMN_TEST_PARADE_FRAMES  in turn for that many frames (default 12); _FLAGS s shiny,
 *   PKMN_TEST_PARADE_FLAGS   f female, m male; then (PKMN_TEST_EXIT) the process ends
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
