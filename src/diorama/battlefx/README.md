# Battle FX in the 3D battle

The HD move and battle animations of the Battle FX package (v3.3, `battlefx_kit` commit
`9848e06`, tag `lab-perf-v3.3`), drawn in the Diorama's 3D battle view in place of the GBA
effect sprites. Built only with `DIORAMA=1` (`ENABLE_DIORAMA`). Public interface:
`include/diorama/battle_fx.h`.

## What decides what

The game's own animation is the authority. `battle_anim.c` launches and runs every animation
exactly as before (script, sprites, tasks, sounds, palette fades, battler motion, length) and
the battle waits for it as ever. Beside it the Battle FX core - the package's animation runtime
and HD director (`battlefx/bfx_core.js`, run by QuickJS-ng) - replays the same script from the
same request, one step for every frame the game's animation advances. Its output only reaches
the screen: while it draws, the 3D view leaves the game's effect sprites (graphics tags
`ANIM_TAG_*`, the Poke Balls and their particles, told by the tile sheet each sprite shows) and
the effect BG layers it redraws out of the GBA picture, and draws the HD effects over the
picture and the world instead. Nothing in Battle FX writes game memory (the one getter pair
that notes a battler for item messages, `GetBattlerHoldEffect` / `GetBattlerAbility`, is read
with `gPotentialItemEffectBattler` put back) or decides anything about the battle: damage,
accuracy, turn order, abilities, saves and multiplayer are untouched. A failure only shows the
GBA effects again.

## Files

| File | |
| --- | --- |
| `include/diorama/battle_fx.h` | The hooks and settings. |
| `src/diorama/battlefx/bfx_game.c` | Game side: requests, the core's thread, the controllers' animations, OAM marks, the frame boundary. |
| `src/diorama/battlefx/bfx_host.c`, `include/diorama/battle_fx_host.h` | The QuickJS-ng host for the core: begin / step / frame packets. |
| `src/diorama/gl/gl_battle_fx.c`, `include/diorama/battle_fx_render.h` | The renderer (OpenGL 3.3, the game's context and frame): particles, cards, ribbons, meshes, lights, distortion, bloom, the game's own battlers lit / flashed / glowing. |
| `src/diorama/battlefx/bfx_test.c`, `include/diorama/battle_fx_test.h` | The test driver (test builds only, `-DBATTLEFX_TEST`). |
| `battlefx/` | The data the game reads beside the executable (`tools/battlefx/pack_data.py`; `MANIFEST.txt` lists every file's SHA-256). |
| `third_party/quickjs-ng`, `third_party/stb` | QuickJS-ng v0.10.1 (MIT), stb_image 2.30 (public domain); `VENDORED.txt` in each. |
| `tools/battlefx/` | `convert_data.py` / `pack_data.py` (the data folder), `gallery.sh` / `realsuite.sh` / `summarize.py` (tests), `bfx_check.c` / `build_check.sh` (the core's numbers under the game's 32-bit builds against the package's), `bfx_perf.c` / `build_perf.sh` (the core's begin, step and frame packet timed in the game's own QuickJS host). |

Hooks in game code (each under `ENABLE_DIORAMA`, GBA line numbers kept with `#line`, the GBA
ROM byte-identical): `battle_anim.c` (launch, the script advancing), `sprite.c` (OAM marks),
`main.c` (frame start and end), `pokeball.c` (a send-out's ball made), `battle_anim_throw.c` (a
shiny Pokemon's stars), `gl_battle.c` (drawing), `gba_easy_draw.c` (BG layers left out),
`battle_controller_player.c`, `battle_controller_safari.c` and `battle_controller_wally.c` (test builds only). Fixes found by the galleries, native builds
only (`PORTABLE`): `battle_anim_effects_1.c` (Mimic without a target partner),
`battle_anim_mon_movement.c` (Struggle Bug's partner shake without a partner),
`platform/m4a_pc.c` (a song number past the song table: Shadow Force / Phantom Force).

## The core's thread

The core runs on a thread of its own (the one that loads it). The game thread hands it each
animation's begin and one step a frame and does not wait: the core's cost (a begin plans the
whole animation; a heavy frame; a QuickJS garbage collection) never holds a game frame up. Each
frame the core makes is copied out of its arrays into a slot of its own (the particle rings into
one mirror, with the ranges changed since they were last taken).

The frame boundary (`BattleFx_FrameBoundary`, main thread, the game thread parked) shows the
frame made for the game frame whose OAM the picture shows: the picture drawn after game frame N
is frame N - 1's (OAM reaches the screen at the VBlank after the frame that built it), and the
core's frame for N - 1 goes with it. When the core is behind, the picture waits a little for it
(up to 60 ms once for an animation's first frame, 8 ms when 3 or more frames behind), then shows
the newest frame made (up to 6 frames old); a frame finished later in the picture's time
replaces it before a present (`BattleFx_LateFrame`). An animation's effect sprites wait up to 4
pictures for its first frame rather than show as GBA pixel art first; an animation the core
falls further behind on plays its GBA effects from there on. With `PKMN_BATTLEFX_LOG=1` the log
says, per animation, how its pictures went (with the core's frame for them, an older one, none,
and how long the pictures waited).

`sync=1` (`PKMN_BATTLEFX_SYNC=1`; the default in test builds) makes the game thread wait for
every command: the frames pair with the game's frame for frame (the parity tests), and the test
driver may use the core between frames.

## The replay follows the game frame for frame

The test builds compare, every frame, the effect sprites the game has on screen (graphics tag
and position) with the core's. To make the replay the game's own:

- **Data from this game's tree.** The package's data is converted with pokeemerald-expansion's
  default config (`GEN_LATEST` = Gen 9); this game sets Gen 6, which changes Leech Life's
  absorb branch and some moves' power, category and target. `battlefx/bfx_moves.json` is
  converted from this repository by the package's own converter (see Data below), keeping the
  `panse` commands' pan step and delay.
- **The branch the game takes.** Script branches on a task's result (Seismic Toss's damage
  level, Weather Ball's weather, Ice Ball's and Rollout's counter, Fury Cutter, Spit Up,
  Present, Magnitude, Return, Frustration, Techno Blast's Drive, side and double checks) are
  chosen from the game's own state, read as its tasks read it (`BranchValue`, `GameVariant`).
- **The game's random numbers.** The runtime's `Random2` is the game's (SFC32 stream 29 from
  `gRng2Value` when the animation begins); `SpriteCB_RandomCentredHits`, the one animation
  callback drawing from `Random`, gets stream 1 from `gRngValue`, advanced each frame as the
  vblank handlers do (the count measured from the game, `Rng1PerFrame`).
- **The game's sound.** The runtime has no audio. Replaying the game's animation, the core gets
  the game's sound state at the start of each frame (`BattleFx_OnFrameBegin`: `IsCryPlaying()`,
  the SE1 / SE2 music players' pause and track bits); the step's own starts and stops change it
  as m4a does, so cry waits (Chatter's double cries), `waitsound` and the script's end (its
  tasks, then up to 90 frames of sound effects) are the game's. `panse` runs the game's pan sound
  task (`Task_PanFromInitialToTarget`, which `waitsound` and the end wait for); `stopsound`
  stops SE1 / SE2; a sound effect named by number (Mystical Fire's `playse 177`) is that
  number's. While the core plans an animation at its begin it estimates the sounds from
  `battlefx/se_frames.json` (each sound effect's length and music player, measured in the game).
- **The game's sprites.** The battle's own sprites besides effect sprites take slots of the 64
  (`spritesInUse`): the core keeps as many taken, so an effect sprite the game cannot make
  (Fairy Lock's last bolt) is not made in the core either. The battlers' sprites start where the
  game has them (`battlerSprites`), not always their home.
- **The loop that runs the script.** A move's animation is run by its battler's controller
  (`Controller_DoMoveAnimation`, in `BattleMainCB1`): each frame its script first, then
  `AnimateSprites`, `BuildOamBuffer`, `UpdatePaletteFade`, `RunTasks` - the runtime's own order. A
  status, battle or special animation (`LaunchStatusAnimation`, `TryHandleLaunchBattleTableAnimation`,
  `InitAndLaunchSpecialAnimation`) is run by the task of priority 10 its launcher makes: its script
  runs inside `RunTasks`, after the sprites and the OAM, so what it makes moves and shows from the
  next frame. Their requests say `"order": "task"` and the core runs their scripts from a task of
  priority 10 too.
- **The order of the tasks.** The game runs its tasks in priority order. The data carries each
  `createvisualtask`'s priority and the core makes a script's tasks with it (`createsoundtask`'s with
  1, as `battle_anim.c` does); the package's runtime made every one with 2. Future Sight's and Doom
  Desire's hit take the battle's own target again (`AnimTask_SetAnimTargetToBattlerTarget`): the
  request carries it (`battlerTarget`).
- **The battle's attacker.** This game's `AnimSuperpowerOrb` and `AnimSuperpowerFireball` start at
  the battle's attacker (`gBattlerAttacker`) and `AnimDragonDanceOrb` takes its radius from it,
  where the package's ports read the animation's attacker. They are the same battler in a move's
  animation and when a gimmick plays its animation (Ultra Burst, Mega Evolution), not when another
  battler plays one; the request carries the battle's attacker (`battlerAttacker`) and the core
  follows the game.
- **The trainer who throws.** In the Safari Zone the player's left battler is the trainer's back
  picture, and in Wally's catch tutorial once Wally has come back out to throw: the request's
  `safari` says so (the core gives that battler the trainer's throwing frames), and
  `catchTutorial` moves the tutorial's ball to where the game starts it (`AnimTask_ThrowBall_StandingTrainer`).
- **Sprites a script leaves moving.** A script can end with effect sprites still moving (a caught
  ball's stars). The core goes on replaying them with the game's, a frame for each game frame, until
  the game's are gone (at most 120 frames), so they stay HD.
- **GBA arithmetic where the port differed.** `x % 0` in Spark's blinking sparks is the GBA's
  (0 only for x = 0), the palette fade's delta is the 4-bit field it is in `palette.c`, and
  `gAnimDisableStructPtr->rolloutTimer` is the game's.

These are installed by the game side over the core's runtime when it loads (`kGameHelperJs` in
`bfx_game.c`); they change nothing when the core runs in the Battle FX Lab. They belong in the
package itself (v3.4): see the integration report.

## The controllers' own animations

A send-out (`CONTROLLER_SEND_OUT`, hooked once `Task_DoPokeballSendOutAnim` has made the ball,
after the multiplayer battle's send-out gate) and a shiny Pokemon's stars (`CONTROLLER_SHINY`)
are not battle animation scripts; the controllers run them. The core replays them from the
frame the game's ball or stars begin, stepped every frame, aligned to the game's frame (its
ball made in the same frame), and ends with its own replay (nothing waits for it). The core
replays one animation at a time: two that begin in the same frame (a double battle's opening
send-outs, two shiny Pokemon at once), one while another plays, or one that begins while another
animation's effect sprites are still on screen (a shiny Pokemon's stars while the double battle's
opening send-outs, played as on the GBA, still throw their balls' particles) stay GBA, so a picture
never leaves another animation's sprites out.

## Settings

`battlefx/settings.cfg` (`enabled=1`, `log=0`; `sync=1` optional) or the environment (which overrides
the file): `PKMN_BATTLEFX=0` plays the original GBA animations (the development fallback),
`PKMN_BATTLEFX_LOG=1` logs every animation (the game's and the core's frame counts, the core's
cost, how its pictures went; the renderer's GPU / CPU time), `PKMN_BATTLEFX_SYNC=1` makes the
game wait for the core every frame, `PKMN_BATTLEFX_DIR=<folder>` reads the data from there
(default: `battlefx` beside the executable, then in the working folder). An animation the core
refuses or fails is logged and plays as on the GBA; the failure count is kept
(`BattleFx_Failures`).

## Data

```sh
# the animation data, converted from THIS game's tree (read only) by the package's converter
python3 tools/battlefx/convert_data.py <battlefx_kit> --expansion <this repository> --out game_moves.json
# the sound effect lengths, measured by the test build (it plays each at a battle's action menu)
PKMN_TEST_BATTLE=wild:263:30 PKMN_TEST_AUTOPILOT=1 PKMN_BFX_TEST_SE_TABLE=se_frames.json PKMN_BFX_TEST_EXIT=1 \
  ./pokeemerald-mp-diorama-bfxtest --save <a save>
# the folder
python3 tools/battlefx/pack_data.py --package <battlefx_kit>/native --data game_moves.json --se-frames se_frames.json \
  --source "Battle FX v3.3: battlefx_kit commit 9848e06, tag lab-perf-v3.3"
```

`convert_data.py` adds three things to the package's converter (v3.4 material): the `panse` commands'
pan step and delay, each `createvisualtask`'s priority (the task rows' 8th field), and a `;` read as
the end of a statement as GNU as reads it (Parabolic Charge's
`goto ParabolicChargeHeal;`, which the package's reader took for a label it could not find, ending
the double-battle branch early). Redo both after changing animation scripts, the generation config or
sound effects.

## Tests

The test build: `make -f Makefile_pc DIORAMA=1 CC="gcc -DDIORAMA_BATTLE_ENV_TEST -DBATTLEFX_TEST"
TARGET=pokeemerald-mp-diorama-bfxtest OBJ_DIR=build/pc-linux-diorama-bfxtest` (`NATIVE_LINUX=1`
for Linux; the Windows one the same with the mingw compiler). With the battle environment test
driver (`PKMN_TEST_BATTLE=wild:263:30 PKMN_TEST_AUTOPILOT=1`) a battle starts, and at its action
menu the Battle FX driver (`include/diorama/battle_fx_test.h`) plays:

- `PKMN_BFX_TEST_MOVES=all` every move (two-turn moves both turns), `PKMN_BFX_TEST_ANIMS=all`
  every battle animation, `PKMN_BFX_TEST_ANIMS=CONTROLLER_SEND_OUT,CONTROLLER_SHINY` the
  controllers' own (started as the controller starts them), each through the game's own code;
- per animation (`report.jsonl`): the game's and the core's frame counts, the parity of the
  effect sprites, frames after the core ended in which the game still showed effect sprites,
  whether the game's own animation ended at all here ("stuck": the Safari Zone's Pokeblock throw
  at a wild battle's menu waits for what only the Safari Zone gives it), the core's and the
  renderer's cost; captures (`PKMN_BFX_TEST_SHOTS`), traces (`PKMN_BFX_TEST_TRACE=1`: every
  frame's sprite lists, the game's and the core's task, fade, sprite and random-number state);
- the effect sprites an animation leaves on screen once the HD layer has ended (`afterEnd`, the 24
  frames the driver waits before the next one) and the frames the HD layer went on with them
  (`hdTail`);
- a battle fought (`PKMN_TEST_FIGHT`) with `PKMN_BFX_TEST_REAL_PARITY=1`: the parity of every
  animation the battle itself plays (`real_report.jsonl`).

Battle animations are launched through the game's own launchers, so their scripts run in the
launchers' tasks as in a battle; `PKMN_BFX_TEST_ANIM_ATTACKER=1` plays them as the opponent (a
trainer's Mega Evolution). The battle environment test driver (test builds only) can also start a
Safari Zone battle (`PKMN_TEST_BATTLE=safari:SPECIES:LEVEL`: the Safari menu is hooked as the
player's, so the gallery plays the Pokeblock and rock throws and the trainer's ball throw there, and
a fought one throws Safari Balls), start Wally's catch tutorial (`PKMN_TEST_BATTLE=tutorial:0`: his
Zigzagoon in the party, his controller plays the battle by itself to the catch), make the wild Pokemon shiny (`PKMN_TEST_SHINY=1`), give the
opponent's Pokemon its Mega Stone (`PKMN_TEST_MEGA=1`: the trainer's AI Mega Evolves it), and keep
the field still while the battle waits to start (`PKMN_TEST_FREEZE_FIELD=1`, set by `gallery.sh`:
otherwise a visible wild Pokemon may walk into the player and start another battle first;
`gallery.sh` notes a run whose battle was not the one asked for in `out/problems.txt`, and
`summarize.py` marks it NOT VALID).

`tools/battlefx/gallery.sh` runs one (a run that crashes the game resumes after the animation
that crashed; a stuck one goes on in a new game), `tools/battlefx/realsuite.sh` the battles of
the real-battle suite, and `tools/battlefx/summarize.py` sums the results up.
