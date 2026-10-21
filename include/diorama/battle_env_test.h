#ifndef GUARD_DIORAMA_BATTLE_ENV_TEST_H
#define GUARD_DIORAMA_BATTLE_ENV_TEST_H

/*
 * The battle-environment test driver (TEST BUILDS ONLY: compiled only with
 * -DDIORAMA_BATTLE_ENV_TEST, never in a player build: Makefile_pc BFX_TEST=1, with
 * DIORAMA=1 the 3D test build, without it the Classic 2D one). It lets
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
 *                            save:1  no battle: the test party and bag (below) are put
 *                            in the save, which is written; with PKMN_TEST_EXIT the
 *                            process ends once the file is on disk (test saves for the
 *                            multiplayer proofs: a party a story save does not have)
 *                            field:<frames>  no battle: PKMN_TEST_FIELD_KEYS on the field
 *                            (frame:KEY,... from then on, nothing else pressed) for that
 *                            many frames, then (PKMN_TEST_EXIT) the end - a held item
 *                            given or taken in the party menu, a save from the menu
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
 *   PKMN_TEST_LEAD_ONLY=1    the party is its first Pokemon alone (in memory only): a battle
 *                            lost ends when it faints
 *
 * For the battle sprite tests (platform/battle_sprites.h):
 *   PKMN_TEST_PARTY          the player's party instead: species:level[:flags],...
 *                            (flags: s shiny, f female, m male, i<item id> held item,
 *                            v<move id> first move)
 *   PKMN_TEST_FOE            flags for the foes wild/wild2/legendary make: flags,flags
 *   PKMN_TEST_BAG            items added to the bag: item:count,... (a Mega Ring...)
 *   PKMN_TEST_NAME           the player's name (A-Z a-z 0-9), as the battle starts or saved
 *   PKMN_TEST_KEYS           keys pressed once the action menu is up: frame:KEY,...
 *                            (A B START SELECT UP DOWN LEFT RIGHT L R, three frames each);
 *                            with PKMN_TEST_FIGHT the fight's presses start after the last one
 *   PKMN_TEST_CAPTURE_EVERY  with _COUNT: a series of captures <stem>-NN, one every that
 *   PKMN_TEST_CAPTURE_COUNT  many drawn frames from PKMN_TEST_CAPTURE_DELAY after the
 *   PKMN_TEST_CAPTURE_FROM   action menu came up (or after the battle was first drawn: battle)
 *   PKMN_TEST_PARADE         first:last - at the action menu, both sides show each species
 *   PKMN_TEST_PARADE_FRAMES  in turn for that many frames (default 12); _FLAGS s shiny,
 *   PKMN_TEST_PARADE_FLAGS   f female, m male; then (PKMN_TEST_EXIT) the process ends
 *
 * For the Mega Evolution checks (tools/battle_env/mega_check.py; on Windows too):
 *   PKMN_TEST_LOG            a file every line the driver logs is also appended to
 *   PKMN_TEST_FACTS=1        log the item, species and gimmick numbers a check names, and
 *                            every Mega Stone holder of the form change tables, then end
 *   PKMN_TEST_MEGA_LOG=1     log the lead and the Mega form its item makes as a battle
 *                            starts, every change of each battler's species, types, Ability
 *                            and active gimmick, the move menu's usable gimmick and trigger,
 *                            the lead again once the field is back, and off a battle every
 *                            change of the lead's held item and the bag's Mega Stones
 *   PKMN_TEST_MEGA_SWEEP     first:last[:level[:s]] - a wild battle (PKMN_TEST_BATTLE's) per
 *                            Mega Stone holder from the first to the last of the list, the
 *                            holder alone at that level (default 100) holding its stone (s:
 *                            shiny); then (PKMN_TEST_EXIT) the process ends
 *   PKMN_TEST_PARADE_MEGAS=1 the parade (above) shows the list's Mega forms instead
 *   PKMN_TEST_COINS          n: the Game Corner coins as the test starts (the Mega Ring and
 *                            the Mega Stones are bought with coins on PC); with
 *                            PKMN_TEST_MEGA_LOG the coins and the money logged when they change
 *   PKMN_TEST_FIELD_AFTER    frames: after a fought battle (PKMN_TEST_FIGHT), back on the
 *                            field, PKMN_TEST_FIELD_KEYS for that many frames, not the end
 *   PKMN_TEST_SOFT_RESET     n: the field keys choose the game's own soft reset (START >
 *                            PLAYER > RESET, YES; the A+B+START+SELECT chord never resets
 *                            on PC); once the game restarted, the autopilot through the
 *                            title and CONTINUE, the field logged, then the same field keys
 *                            again until the game has reset n times, and (PKMN_TEST_EXIT)
 *                            the end ("soft reset: the game did not restart" if one never
 *                            came)
 *   PKMN_TEST_FIELD_SHOTS    frame,...: with field: pictures <PKMN_TEST_MEGA_SHOT_STEM>-NN
 *                            (NN from 50, on after each soft reset) at those field-key
 *                            frames (Classic 2D build)
 *
 * For the Update 32 starter check (tools/battle_env/feature_check.py; on Windows too):
 *   PKMN_TEST_STARTER_LOG=1  on the field, whenever one changes: the map and the player's
 *                            place, the party, its lead's species, personality and shininess,
 *                            the starter claim (VAR_MULTIPLAYER_STARTER_CHOICE) and
 *                            FLAG_SYS_POKEMON_GET - a claim in Birch's lab, a save, a load,
 *                            a soft reset that brings the claim back
 *   PKMN_TEST_MEGA_SHOT      frames: a picture that long after the lead's Mega animation, as
 *   PKMN_TEST_MEGA_SHOT_STEM <stem>-NN.png (NN the sweep's holder, or 00)
 *
 * In the Classic 2D test build the pictures are the GBA frame (no .json beside them).
 *
 * Nothing here changes a battle: it starts one the way the debug menu's
 * "Try battle" does and reads the picture.
 */

#if defined(DIORAMA_BATTLE_ENV_TEST)

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
#ifndef ENABLE_DIORAMA
/* platform/sdl2.c, each presented GBA frame (the Classic 2D test build's captures) */
void DioramaBattleEnvTest_Frame2D(const unsigned short *pixels);
#endif

#endif

#endif // GUARD_DIORAMA_BATTLE_ENV_TEST_H
