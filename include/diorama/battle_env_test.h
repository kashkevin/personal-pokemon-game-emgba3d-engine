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
 * For Battle Tower: Legends (tools/battle_env/tower_check.py; on Windows too):
 *   PKMN_TEST_BATTLE=tower:<floor>  the solo floor through the game's own start (src/tower_legends.c): its intro, the
 *                            battle against the floor's trainer, its end (healed, held items back, no money, the
 *                            result in the save's Tower record); logged: "tower battle", each "tower foe" as built,
 *                            then at the end "tower end" and each "tower own" Pokemon
 *   PKMN_TEST_BADGES=n, PKMN_TEST_CHAMPION=0/1  with tower:<floor>, the badges and the Hall of Fame
 *                            flag in memory (the Tower experience cap's runs)
 *   PKMN_TEST_TOWER_RECORD=H:P  with save:1, the save's Tower record as a climb that cleared floor H and goes on at
 *                            P (a launcher proof's fixture: the host imports it on the player's first Tower action)
 *   PKMN_TEST_FAST_OPTIONS=1 with save:1, the save's options fast text and battle scene off (a player's own OPTION
 *                            choices; the launcher proof's 100-floor climb)
 *   PKMN_TEST_TOWER_WIN=1    a test advantage for the won runs: the lead's HP full again at each of its turns and its
 *                            most effective move chosen (in memory only)
 *
 * For the battle sprite tests (platform/battle_sprites.h):
 *   PKMN_TEST_PARTY          the player's party instead: species:level[:flags],...
 *                            (flags: s shiny, f female, m male, i<item id> held item,
 *                            v<move id> first move, w<n> every IV n, e one experience point
 *                            short of the next level)
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
 *   PKMN_TEST_TRAIN_LOG=1    on the field, whenever one changes: the lead's species, IVs, Hyper
 *                            Training bits and Ability (its slot), the Bag's Bottle Caps, Gold
 *                            Bottle Caps and Ability Patches - each used in the Bag, refused, saved
 *                            and loaded (Battle Tower: Legends' items)
 *   PKMN_TEST_BAG_LOG        item[,item...] (at most 4): on the field, whenever one changes: the
 *                            money and those items' counts in the Bag - a Poke Mart's purchase and
 *                            sale, saved and loaded (tools/battle_env/shop_check.py)
 *   PKMN_TEST_MEGA_SHOT      frames: a picture that long after the lead's Mega animation, as
 *   PKMN_TEST_MEGA_SHOT_STEM <stem>-NN.png (NN the sweep's holder, or 00)
 *
 * For the gameplay checks (tools/qa/gameplay_check.py; the final master update, phase 10):
 *   PKMN_TEST_PLAN           a file of targets, one per line: <key> <kind> <group> <num> <x> <y> <dir> <arg>
 *                            (kind talk: an object, arg its local id; sign, hidden: a background event, arg its
 *                            index; warp, trigger: arg the warp's or coordinate trigger's index; a trigger logs its
 *                            variable's value and the one it runs on). For each, from wherever the player is: the target
 *                            object shown, every other trainer of that map counted as beaten and the target trainer
 *                            as not, a hidden item put back, the party healed; then a warp to x,y facing dir, A (a
 *                            warp: dir held), B through every line and question, A through a battle, until the field
 *                            is free; "plan target N KEY begin / at / battle / map now / end / hang" lines say what
 *                            happened (the end: frames, script, battles, outcome, trainers, map, money, the beaten
 *                            and own flags, the bag's changes). In memory only: nothing is saved
 *   PKMN_TEST_PLAN_FIRST     n: start at the plan's line n (0-based, comments not counted)
 *   PKMN_TEST_PLAN_FRAMES    frames a target may keep the field (default 2400; 10800 more per battle): then a hang,
 *                            pictured as <PKMN_TEST_MEGA_SHOT_STEM>-99.png, and the process ends (code 7)
 *
 * In the Classic 2D test build the pictures are the GBA frame (no .json beside them).
 *
 * Nothing here changes a battle: it starts one the way the debug menu's
 * "Try battle" does and reads the picture.
 */

#if defined(DIORAMA_BATTLE_ENV_TEST)

#include <stdarg.h>
#include <stdbool.h>

/* overworld.c, CB1_Overworld (game thread) */
void DioramaBattleEnvTest_FieldTick(void);
/* assertf.c, AssertfCrashScreen (game thread): the assert, logged; a driven game (PKMN_TEST_EXIT) ends there */
void DioramaBattleEnvTest_OnAssert(const char *fmt, va_list va);
/* overworld.c, SetPlayerCoordsFromWarp (game thread): the plan runner's own warp's place, once (a map's far side: the
   warp's own coordinates are s8, to 127) */
bool DioramaBattleEnvTest_TakeWarpCoords(s16 *x, s16 *y);
/* text.c (read with the game thread parked): the first text printer's place in its text, NULL with none printing */
const unsigned char *DioramaBattleEnvTest_TextPosition(void);
/* script_menu.c (read with the game thread parked): a script's multichoice or YES/NO menu taking input */
int DioramaBattleEnvTest_ScriptMenuUp(void);
/* easy_chat.c (read with the game thread parked): -1 no Easy Chat screen, 0 one, 1 its exit prompt up */
int DioramaBattleEnvTest_EasyChatState(void);
/* script_menu.c (read with the game thread parked): the PokéNav tutorial's forced START menu taking input */
int DioramaBattleEnvTest_ForcedStartMenuUp(void);
/* pokenav_menu_handler.c (read with the game thread parked): the key the PokéNav tutorial wants, 0 when none is
   open */
int DioramaBattleEnvTest_PokenavTutorialKey(void);
/* naming_screen.c (read with the game thread parked): a naming screen taking input */
int DioramaBattleEnvTest_NamingScreenUp(void);
/* battle_controller_player.c, HandleInputChooseAction (game thread) */
void DioramaBattleEnvTest_OnActionMenu(void);
/* battle_util.c, GetAdjustedDamage (game thread): a hit that would knock out this battler leaves it 1 HP instead */
bool DioramaBattleEnvTest_HoldOnForMega(unsigned battler);
/* wild_encounter_ow.c, SetSpeciesInfoForOWE (game thread): PKMN_TEST_OWE_SHINY=1 every other overworld wild Pokemon the
   game makes is shiny (the first one is), =2 every one; otherwise the game's own roll */
bool DioramaBattleEnvTest_OweShiny(bool rolled);
/* multiplayer_spawns.c, MultiplayerSpawns_Tick (game thread): PKMN_TEST_SPAWNS="species:x:y:flags:spawnId:personality;..."
   the shared spawns a world's host would send for the map the player is on, written into the spawn mailbox as the host
   writes them; true (the world taken as connected) while there are any, with *fieldReady as the field's;
   PKMN_TEST_SPAWNS_UNTIL=<frames>: after that many, the host lists none */
bool DioramaBattleEnvTest_FakeSpawnHost(bool *fieldReady);
/* multiplayer_world.c, MultiplayerWorld_Tick (game thread): PKMN_TEST_WORLD_EVENT="species:x:y:flags" the World Event
   Pokemon a world's host would show on the player's map (flags 1: shiny), written into the world mailbox as the host
   writes it; true (the world taken as connected) while there is one, with *fieldReady as the field's */
bool DioramaBattleEnvTest_FakeWorldEvent(bool *fieldReady);
/* wild_encounter_ow.c (test builds): an overworld encounter's kind */
enum { OWE_KIND_WILD = 1, OWE_KIND_ROAMER, OWE_KIND_OUTBREAK, OWE_KIND_FEEBAS };
struct ObjectEvent;
unsigned DioramaBattleEnvTest_OweKind(struct ObjectEvent *owe);
/* roamer.c (test builds): the roamer stands on the player's map now; with PKMN_TEST_ROAMER it is met whenever an
   encounter is made there (TryStartRoamerEncounter: its 1 in 4 roll still made) */
void Roamer_TestPlaceHere(unsigned roamerIndex);
bool DioramaBattleEnvTest_RoamerAlwaysMet(void);
/* gl_battle.c (render thread): the stem to capture this frame, or NULL */
const char *DioramaBattleEnvTest_CaptureStem(void);
void DioramaBattleEnvTest_CaptureWritten(void);
/* platform/sdl2.c, the keys read each frame (PKMN_TEST_AUTOPILOT) */
unsigned short DioramaBattleEnvTest_FilterKeys(unsigned short keys, unsigned int nowMs);
/* sdl2.c main loop, every turn (the game thread perhaps mid-frame): a plan's game stuck inside a frame ends as a hang */
void DioramaBattleEnvTest_Watchdog(unsigned int nowMs);
#ifndef ENABLE_DIORAMA
/* platform/sdl2.c, each presented GBA frame (the Classic 2D test build's captures) */
void DioramaBattleEnvTest_Frame2D(const unsigned short *pixels);
#else
/* platform/sdl2.c, each presented 3D frame off a battle: a field picture's stem due now (PKMN_TEST_FIELD_SHOTS), or NULL */
const char *DioramaBattleEnvTest_FieldShotStem(void);
#endif

#endif

#endif // GUARD_DIORAMA_BATTLE_ENV_TEST_H
