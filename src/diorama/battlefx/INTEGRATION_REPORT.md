# Battle FX in the 3D battles — integration report

Checkpoint **battlefx-int-wip-20261012-1** · engine branch `claude/battle-fx-integration` · 2026-10-12

PRIVATE: the builds in this checkpoint contain game data, and `src/platform/bios.c` still contains
GPL/MPL-derived code. Do not share them.

## Summary

- The Battle FX package (v3.3: `battlefx_kit` commit `9848e06`, tag `lab-perf-v3.3`) now runs inside
  the game. Every move animation (621 moves, 651 animations with the second turns) and every battle
  animation the game plays (81 status, general and special scripts, and the controllers' send-out and
  shiny stars) is replayed by the Battle FX core beside the game's own animation and drawn in HD in
  the 3D battle, over Astra's newest battle environments (round 4, `aa63a98a3`, merged).
- The game stays the authority. Its own animation still runs, frame for frame, and decides
  everything; the core replays it from the game's state and only its picture reaches the screen. The
  replay is checked against the game every frame: in the final runs the effect sprites of the core
  and of the game are identical in every compared frame of every move in single and in double
  battles, of every battle animation (as the attacker), and of every animation in
  28 battles fought through to their end - wild, trainer, gym, Elite Four, double,
  Mega, shiny, the Safari Zone's and Wally's catches, weather, five 3D environments, a lost battle.
- Nothing about the battle changed. Battle FX never writes game memory; damage, accuracy, abilities,
  effects, turn order, saves and multiplayer are untouched, and the GBA ROM built from this branch is
  byte-identical (SHA-256 `f214735b…3387`). `PKMN_BATTLEFX=0` plays the original animations; an
  animation the HD layer cannot draw falls back to the GBA effects from that frame.
- Performance: the animation core runs on a thread of its own and never holds a game frame up. Drawing
  the HD layers is ordinary full-screen GPU work, but on this VM's software OpenGL it took longer than a
  frame and slowed the battle, so the player builds carry a drawing budget (8 ms a picture): over it,
  the original animations play, logged, and the game keeps 60 frames a second. What a graphics card
  costs at 1080p, 1440p and 4K is what the benchmark kit in this checkpoint measures.
- The animated battle sprites (sprite-integration-v2, in the EmeraldTogether integration): Battle FX
  merged into that integration in a trial branch, built and tested with the sprite pack:
  795 of 795 animations exact (every move, every battle animation, ten moves with pictures, 4 battles fought; the test build crashed once as it exited, known issues 13); the effects land on the right battlers, sized for the GBA boxes (Verification).
- Not verified: anything on a real graphics card (every picture and number here is from software
  OpenGL in the cloud, or Wine), 1440p and 4K drawing, a high-refresh display, Windows itself, a
  two-player battle, the player's own Mega Evolution, a voluntary switch, a Pokeblock throw in a
  real Safari battle, the animated sprites on this branch itself (only in the trial merge). The
  benchmark kit in this checkpoint measures the first three on your PC.

## Versions and sources

| | |
| --- | --- |
| Battle FX package | v3.3, `battlefx_kit` `9848e06` (tag `lab-perf-v3.3`): the core `battlefx/bfx_core.js`, textures, shaders, the converter |
| Animation data | converted from this game's own `battle_anim_scripts.s` and C sources by the package's converter (`tools/battlefx/convert_data.py`: three additions, below), packed into `battlefx/` |
| Game base | `claude/battle-environments-3` (Astra, battle3d-env-wip-20261012-2, `aa63a98a3`), merged into this branch at `865fbf745` |
| Built first on | `claude/battle-environments-2` (`719e16c78`, battle3d-env-wip-20261012-1); that state is tagged `battlefx-int-premerge-20261012` |
| JavaScript engine | QuickJS-ng v0.10.1 (MIT), `third_party/quickjs-ng`; stb_image 2.30 (public domain) |

## How it works (in short)

`battle_anim.c` launches and runs every animation exactly as before. At the launch Battle FX sends the
core a request built from the game's state: the script the game chose (Secret Power's swap included),
the battlers, their sprites' places, the move's power, damage, turn, friendship and weather, the
random-number states, the script branch the game will take (read as the game's own tasks read it),
how many sprite slots the battle already uses and which loop runs the script. Then, for every frame the
game's animation advances, the core advances one frame - on a thread of its own, so its cost never
holds a game frame up - and sends a frame packet (particles, cards, ribbons, meshes, lights,
distortion) to the renderer. At each frame boundary the picture the game shows is paired with the core
frame made for the same game frame; the GBA effect sprites and effect BG layers that frame stands in
for are left out of the GBA picture, and the HD layers are drawn behind and in front of it in the
battle's own OpenGL frame (`DioramaGLBattle_Present`). When the game's animation ends, so does the
core's. The developer README is `src/diorama/battlefx/README.md`.

## Status of every item

Levels, highest reached: **Implemented** (written, builds) · **Tested in standalone Lab** (the
package's Lab, before this integration) · **Tested through native bridge** (in the game: the game's
own launcher plays the animation at a battle's menu and every frame of the core's replay is compared
with the game's - the galleries) · **Tested inside actual gameplay** (a battle fought through to its
end by the test autopilot, every animation it played compared) · **Not yet verified**. Everything
here ran in the cloud (Linux, software OpenGL) or under Wine; nothing on a graphics card.

| Item | Status | Evidence, notes |
| --- | --- | --- |
| Native bridge: the game's own animation events drive the core (launch, each frame the game's animation advances, its end) | Tested inside actual gameplay | 28 battles (30 runs), 281 animations, all exact |
| Hooks on the game's animation events, no move logic in the renderer | Tested inside actual gameplay | `battle_anim.c` launch/advance, `pokeball.c` / `battle_anim_throw.c` for the controllers' send-out and stars; the script the game chose is replayed, nothing re-decided |
| Native OpenGL renderer in the game's own GL context and frame; no second window, no render loop of its own | Tested inside actual gameplay | drawn in `DioramaGLBattle_Present`; software OpenGL only |
| No full-resolution CPU / GPU copies each frame | Implemented | one `glCopyTexSubImage2D` of the battle's region a layer (GPU to GPU) for distortion; nothing read back to the CPU (code; not profiled on a GPU) |
| Effects at the battlers' places in the 3D battles (Astra's round 4) | Tested inside actual gameplay | the effect sprites' places compared every frame; 3D battles on Route 101, trainers' maps, Granite Cave, Rustboro Gym, Route 119 (by the Weather Institute), Mt. Chimney |
| Real Pokemon sprites, attachment points | Tested inside actual gameplay (GBA sprites); the animated sprites: tested inside actual gameplay in a trial merge (not on this branch) | positions from the game's own sprites and coordinate functions, so every species' size and shape is the game's own; the animated battle sprites (sprite-integration-v2, in the EmeraldTogether integration): all moves, single battle: 651 of 651 exact (Ally Switch without a partner crashed the game, as on this branch: known issues 1); the battle animations: 81 of 81 exact (the test build crashed once as it exited after POKEBLOCK_THROW, a Safari animation that does not end at a wild battle's menu; the run resumed after it - known issues 13); ten moves with pictures (Wailord against Joltik and back, a shiny wild Pokemon and a female Pikachu, a double battle): 40 of 40 exact; battles fought: Calvin (won, 3 of 3 exact), Tate & Liza (double) (won, 11 of 11 exact), Fabian's Mega Manectric (won, 5 of 5 exact), the Safari Zone (caught, 2 of 2 exact) |
| Singles | Tested inside actual gameplay | single-battle gallery 651 of 651 animations exact; crashed the game: MOVE_ALLY_SWITCH (known issues 1: only the gallery plays Ally Switch without a partner) |
| Doubles (attacker and target per battler) | Tested inside actual gameplay | double-battle gallery 652 of 652 animations exact; a gym double battle, a wild double battle, two trainers at once fought |
| All 621 moves (651 animations with the second turns) | Tested through native bridge | every one exact in the single and the double gallery; 29 of them also in fought battles |
| All 83 battle animations (81 status / general / special scripts, the send-out, a shiny Pokemon's stars) | Tested through native bridge | the player's side: 81 of 81 scripts exact; POKEBLOCK_THROW, ROCK_THROW, BALL_THROW_WITH_TRAINER wait for the Safari Zone (at a wild battle's menu the game's own animation does not end) - at a Safari battle's menu 4 of 4 exact; played by the opponent: 80 of 81 exact - different: BALL_THROW_WITH_TRAINER from frame 11 (the trainer's ball throw is never the opponent's in this game (only the Safari, Wally and Old Man controllers throw with a trainer, all on the player's side), and at a wild battle's menu its game animation does not end); the send-out and the shiny stars: 6 of 6 exact for battlers 0, 1 and 2; 13 of the 83 also in fought battles |
| Mirror Move plays the copied move | Tested inside actual gameplay | Mirror Move against Roxanne: Mirror Move x4, the moves it copied: Rock Tomb x4 - all exact |
| Metronome calls the game's own choice | Tested inside actual gameplay | Metronome against a wild Zigzagoon: Metronome x1, the moves it called: Hyper Voice - all exact |
| Assist, Copycat, Me First, Sleep Talk | Tested through native bridge | their own animations exact; the move they call is a launch of the game's own, the path Metronome's took in a battle; not fought |
| Secret Power by environment | Tested inside actual gameplay | the game's own choice by the environment, each exact - Route 101: BODY_SLAM; Granite Cave: ROCK_THROW; Route 119: BODY_SLAM |
| Multi-hit moves | Tested inside actual gameplay | Fury Attack against Sidney: 24 Fury Attack hit animations, each exact |
| Two-turn moves | Tested inside actual gameplay | Fly against a route trainer: Fly x2 (the turn away and the strike), exact; every second turn (30) in the galleries |
| Rollout / Fury Cutter counters | Tested inside actual gameplay | against Roxanne: Rollout x4, Fury Cutter x4 on consecutive turns, exact |
| Weather | Tested inside actual gameplay | a sandstorm (x4) and rain (x15 in its 2 runs) every turn, the multiplayer twin Gym trainers' Pokemon; hail, sun: galleries |
| Terrain | Tested through native bridge | galleries only |
| Mega Evolution | Tested inside actual gameplay (the opponent's) | Fabian's Manectric Mega Evolves (B_ANIM_MEGA_EVOLUTION x1, exact); the player's own: gallery only (the autopilot cannot press the Mega trigger) |
| Status and general animations | Tested inside actual gameplay | fought: Ball Throw With Trainer, Held Item Berry, Leech Seed Drain, Mega Evolution, Rain Continues, Sandstorm Continues, Simple Heal, Stats Change, Status Brn, Status Slp, Switch Out Player Mon; all of them in the galleries |
| Shiny | Tested inside actual gameplay | a shiny wild Zigzagoon's stars, the test save's own shiny lead's stars (CONTROLLER_SHINY x19 in all) |
| Catching | Tested inside actual gameplay | the Safari Zone: the trainer's throws, shakes, the catch (BALL_THROW_WITH_TRAINER x1); Wally's catch tutorial: his Zigzagoon's Tackles, the call-back, his throw and the catch (BALL_THROW_WITH_TRAINER x1 in the run again); in the suite's run the Ralts fainted from the second Tackle first (the game's damage; known issues 12) - all exact |
| Switching | Tested inside actual gameplay (forced) | the opponents' send-outs (x31 in HD, the next Pokemon after each faint), the player's side's (x8: the lead, where it does not slide in as a follower; Wally's Zigzagoon) and Zigzagoon called back in the tutorial (SWITCH_OUT_PLAYER_MON); a voluntary switch: **not yet verified** |
| Victory / defeat | Tested inside actual gameplay | 30 runs: 25 won, 2 lost, 2 caught, 1 not ended. Lost: Wallace against the save's lead alone, on purpose; Sidney against the level-100 lead alone with Fury Attack. Not ended: the rain battle's suite run, at the party menu. Every battle ended in at least one run |
| Wild, trainer, gym, Elite Four, double battles | Tested inside actual gameplay | see Verification |
| Original GBA animations behind a development setting | Tested inside actual gameplay | `PKMN_BATTLEFX=0` (or `battlefx/settings.cfg` `enabled=0`): a battle against Roxanne fought to its end (won), no HD animation begun (0) |
| Failure: recorded, GBA from that frame, no crash, no desync | Tested inside actual gameplay (refused) | a move past the 621 in a fought battle (Fire Lash): logged "move 643 plays as on the GBA (not in the Battle FX data)"; the battle won. The core falling behind: on the busy VM it fell more than 6 frames behind in two of the thread runs (core on its own thread): logged, the rest of those animations played as on the GBA, the runs went on (`failed: true` in their reports) |
| Game logic at 60 frames a second, the HD layer between game frames | Tested inside actual gameplay | the core advances one frame per game frame (every animation's length equals the game's, all galleries and battles); the HD layer is drawn at every present, its particles' time running on between game frames; on this VM's software OpenGL the HD layers cost more than a frame, so the player builds' drawing budget keeps the game at 60 (next row); a graphics card and a 120-240 Hz display: **not yet verified** |
| Drawing budget: the HD layers never slow the game | Tested through native bridge | the fifteen heavy moves at 1280x720 on software OpenGL with the 8 ms budget: 2 animations over it (70.1 and 73.5 ms a picture, logged) went on as GBA, then "the HD effects are off until this battle ends"; the game at 59.9 frames a second (median; 42.5 with no budget) |
| 1080p / 1440p / 4K | 720p and 1080p measured (software OpenGL) | the HD layers' cost scales with the window's pixels (33 ms a layer at 720p, 70 at 1080p here); 1440p, 4K and any graphics card: **not yet verified** - the benchmark kit measures them |
| Nothing about the battle changed (damage, accuracy, abilities, effects, turn order, saves, multiplayer) | Tested inside actual gameplay | the GBA ROM byte-identical; Battle FX never writes game memory; every battle's outcome as the game decided it; a two-player battle: **not yet verified** |
| Automated regression tests | Implemented, run | `tools/battlefx/gallery.sh`, `realsuite.sh`, `summarize.py` |
| Windows test build | Tested under Wine | the Windows test build: 6 of 6 animations exact frame for frame (Flamethrower, Thunderbolt, Surf, Earthquake, Protect, Bolt Strike), drawn over the 3D battle; the Windows player starts and loads the core; on Windows itself: **not yet verified** |

## Verification

All on the final test build (`claude/battle-fx-integration` `3db63631c`; the report's own commit
after it changes no game code: the report, the developer README and the test suite's battle list - two
names, the rain battle's settings), Linux, software OpenGL (llvmpipe) on a virtual display, the game
waiting for the core every frame so that each game frame is compared with the core's frame for it.
"Exact": in every compared frame the effect sprites of the game and of the core are the same
(graphics tag and screen position); palette blends, BG layers and battler offsets are not compared.

### Galleries (the game's own launchers, at a battle's action menu)

| Run | Played | Exact | Different | GBA shown | Crashed | Game's own did not end there | Core begin ms (median / max) | Step ms (median) |
| --- | ---: | ---: | --- | --- | --- | --- | --- | ---: |
| All moves, single battle - the player's Pokemon attacking | 651 | 651 | 0 | 0 | MOVE_ALLY_SWITCH | 0 | 53 / 458 | 1.64 |
| All moves, double battle - battler 0 attacking, its partner and both foes present | 652 | 652 | 0 | 0 | 0 | 0 | 68 / 1061 | 1.91 |
| Battle animations - status, general and special scripts (the player's side) | 81 | 81 | 0 | 0 | 0 | B_ANIM_POKEBLOCK_THROW, B_ANIM_ROCK_THROW, B_ANIM_BALL_THROW_WITH_TRAINER | 32 / 435 | 1.65 |
| Battle animations as the opponent - played by battler 1 (a trainer's Mega Evolution...) | 81 | 80 | B_ANIM_BALL_THROW_WITH_TRAINER (frame 11) | 0 | 0 | B_ANIM_POKEBLOCK_THROW, B_ANIM_ROCK_THROW, B_ANIM_BALL_THROW_WITH_TRAINER | 30 / 692 | 1.82 |
| At a Safari battle's menu - Pokeblock and rock throws, the reactions, the trainer's ball throw | 4 | 4 | 0 | 0 | 0 | 0 | 32 / 138 | 1.42 |
| Send-out and shiny stars, battler 0 - the controllers' own animations | 2 | 2 | 0 | 0 | 0 | 0 | 54 / 58 | 1.77 |
| Send-out and shiny stars, battler 1 | 2 | 2 | 0 | 0 | 0 | 0 | 147 / 277 | 1.73 |
| Send-out and shiny stars, battler 2 - a double battle | 2 | 2 | 0 | 0 | 0 | 0 | 17 / 20 | 1.97 |

### Battles fought through to their end

The test autopilot presses A (the first move on the first target, every message); the party at level
100 unless noted. GBA notes: a controller animation the HD layer left to the GBA (known issues 2).

Two battles were run a second time on the same build, and both runs of each are in the table: Wally's catch tutorial, whose run in the suite ended before the throw (known issues 12); and the rain battle, whose suite run (the whole party, Flamethrower) never ended: the lead fainted against the rain team's Water Pokemon and the autopilot, which only presses A, stayed at the party menu (a second such run, stopped by hand, showed that menu: `results\pictures\rain__party_menu.png`); run again as `realsuite.sh` now runs it, the lead alone with Thunderbolt.

| Battle | Ended | Animations | Exact | GBA notes | Animations played |
| --- | --- | ---: | ---: | ---: | --- |
| a wild Zigzagoon, Route 101 | won | 2 | 2 | 0 | CTL_SHINY, FLAMETHROWER |
| Calvin, a route trainer | won | 3 | 3 | 0 | CTL_SEND_OUT, CTL_SHINY, FLAMETHROWER |
| Roxanne, Rustboro Gym | won | 12 | 12 | 0 | CTL_SEND_OUT x3, CTL_SHINY, FLAMETHROWER x4, HELD_ITEM_BERRY, SIMPLE_HEAL, ROCK_TOMB, STATS_CHANGE |
| Sidney, Elite Four | won | 14 | 14 | 0 | CTL_SEND_OUT x5, CTL_SHINY, STATS_CHANGE, FLAMETHROWER x6, FACADE |
| Tate & Liza, a gym double battle | won | 29 | 29 | 3 | FLAMETHROWER x14, CTL_SEND_OUT x2, HELD_ITEM_BERRY x2, SIMPLE_HEAL x2, LIGHT_SCREEN, STATUS_BRN x6, CALM_MIND, STATS_CHANGE |
| a wild double battle | won | 2 | 2 | 2 | FLAMETHROWER x2 |
| two trainers at once (Calvin, Billy) | won | 5 | 5 | 3 | CTL_SEND_OUT x2, FLAMETHROWER x3 |
| Wallace, the save's lead alone at its own level: lost on purpose | lost | 6 | 6 | 0 | CTL_SEND_OUT, CTL_SHINY, RAIN_DANCE, IMPRISON, RAIN_CONTINUES, WATER_SPOUT |
| a shiny wild Zigzagoon | won | 3 | 3 | 0 | CTL_SHINY x2, FLAMETHROWER |
| Fabian: his Manectric Mega Evolves | won | 5 | 5 | 0 | CTL_SEND_OUT, CTL_SHINY, MEGA_EVOLUTION, STATS_CHANGE, FLAMETHROWER |
| the Safari Zone: Safari Balls until caught | caught | 1 | 1 | 0 | BALL_THROW_WITH_TRAINER |
| Wally's catch tutorial (his controller plays it) - the suite's run | won: the Ralts fainted from the second Tackle, no throw (the game's damage roll; known issues 12) | 4 | 4 | 0 | CTL_SEND_OUT, TACKLE x2, CONFUSION |
| Wally's catch tutorial (his controller plays it) - run again | caught | 9 | 9 | 0 | CTL_SEND_OUT, TACKLE x2, GROWL x2, STATS_CHANGE x2, SWITCH_OUT_PLAYER_MON, BALL_THROW_WITH_TRAINER |
| Roxanne, Fury Cutter (the counter) | won | 12 | 12 | 0 | CTL_SEND_OUT x3, CTL_SHINY, FURY_CUTTER x4, HELD_ITEM_BERRY, SIMPLE_HEAL, ROCK_TOMB, STATS_CHANGE |
| Roxanne, Rollout (the counter) | won | 14 | 14 | 0 | CTL_SEND_OUT x3, CTL_SHINY, ROLLOUT x4, ROCK_TOMB x2, STATS_CHANGE x2, HELD_ITEM_BERRY, SIMPLE_HEAL |
| a wild Zigzagoon, Metronome | won | 3 | 3 | 0 | CTL_SHINY, METRONOME, HYPER_VOICE |
| Roxanne, Mirror Move | won | 23 | 23 | 0 | CTL_SEND_OUT x3, CTL_SHINY, ROCK_TOMB x8, STATS_CHANGE x5, MIRROR_MOVE x4, HELD_ITEM_BERRY, SIMPLE_HEAL |
| a wild Zigzagoon, Secret Power on Route 101 (plain ground) | won | 2 | 2 | 0 | CTL_SHINY, BODY_SLAM |
| Sidney, Fury Attack | lost | 47 | 47 | 0 | CTL_SEND_OUT x2, CTL_SHINY, STATS_CHANGE x6, FURY_ATTACK x24, SAND_ATTACK x2, LEECH_SEED, LEECH_SEED_DRAIN x6, COTTON_SPORE x3, FEINT_ATTACK x2 |
| Calvin, Fly | won | 4 | 4 | 0 | CTL_SEND_OUT, CTL_SHINY, FLY x2 |
| a multiplayer twin Gym trainer: sandstorm | won | 16 | 16 | 0 | CTL_SEND_OUT x3, CTL_SHINY, SANDSTORM_CONTINUES x4, FLAMETHROWER x4, HELD_ITEM_BERRY, SIMPLE_HEAL, ROCK_TOMB, STATS_CHANGE |
| a multiplayer twin Gym trainer: rain - the suite's run | not ended: the lead fainted and the autopilot, which only presses A, stayed at the party menu (no animation in its last 14 minutes) until the test's 20-minute limit | 39 | 39 | 0 | CTL_SEND_OUT x2, CTL_SHINY, RAIN_CONTINUES x11, FLAMETHROWER x7, HYPNOSIS, STATUS_SLP x4, SURF x4, STATUS_BRN x6, RAIN_DANCE, WATERFALL x2 |
| a multiplayer twin Gym trainer: rain - run again as the suite now runs it: the lead alone, Thunderbolt | won | 13 | 13 | 0 | CTL_SEND_OUT x3, CTL_SHINY, RAIN_CONTINUES x4, THUNDERBOLT x3, WATERFALL, FAKE_OUT |
| a wild Zigzagoon in Granite Cave | won | 2 | 2 | 1 | CTL_SEND_OUT, FLAMETHROWER |
| a wild Zigzagoon in Rustboro Gym (indoors) | won | 2 | 2 | 1 | CTL_SEND_OUT, FLAMETHROWER |
| a wild Zigzagoon on Route 119, by the Weather Institute | won | 2 | 2 | 1 | CTL_SEND_OUT, FLAMETHROWER |
| a wild Zigzagoon on Mt. Chimney | won | 2 | 2 | 1 | CTL_SEND_OUT, FLAMETHROWER |
| Secret Power in Granite Cave | won | 2 | 2 | 1 | CTL_SEND_OUT, ROCK_THROW |
| Secret Power on Route 119 (plain ground) | won | 2 | 2 | 1 | CTL_SEND_OUT, BODY_SLAM |
| Fire Lash, a move past the 621: refused, the game's own animation | won | 1 | 1 | 1 | CTL_SHINY |

28 battles in 30 runs (Wally's catch tutorial and the rain battle run a second time on the same build), 281 animations compared, 281 exact; 42 distinct animations (29 moves, 13 others).

### The Windows builds (Wine, 32-bit, software OpenGL)

- The Windows test build at a wild battle's menu, borderless on the 1920x1080 virtual display, the game waiting for the core every frame so that each frame is compared: 6 of 6 animations exact (Flamethrower, Thunderbolt, Surf, Earthquake, Protect, Bolt Strike), drawn in HD over the 3D battle (pictures in `results\wine`). Wine's OpenGL on software rendering took 187 ms a layer at 1920x1080 (median of the animations).
- Again windowed at 640x360 with the 3D log on: the game logs "3D on map 0.16 (wild), player 10,12 facing 4", drawn by Mesa's llvmpipe through Wine's OpenGL; 1 of 1 animations exact (Flamethrower), 11.5 ms a layer (`results\wine\windowed640_*`).
- The black pictures of Bolt Strike are the move's own: its animation darkens the battle's background (and, at the test's action menu, the menu's frame). It looks the same on Linux with the HD layer and without it (`results\wine\linux_MOVE_BOLT_STRIKE_*`); in a battle the menu is not up during a move.
- The Windows 3D player: starts, loads the core (ready 691 ms after start) and runs the title at 59.9 game frames a second (median), 45 seconds.
- Wine 32-bit with software OpenGL on a virtual display: nothing here says how Windows itself or a graphics card runs it.

### With the animated battle sprites (a trial merge)

Your brief asks for Battle FX with the animated sprites once they exist. They are in the EmeraldTogether
integration (`claude/emeraldtogether-integration` `8706f8f6c`, "Native 3D preview 9": sprite-integration-v2,
the native UI's rounds 3-4, the character selection), so this checkpoint adds a trial branch,
`claude/battle-fx-sprites-trial`: that integration with this branch merged in. Four files conflicted, all
additions on both sides, resolved as COORDINATION.txt 10 describes (and one brace the resolution dropped,
fixed in a commit of its own). Its GBA ROM is byte-identical (`f214735b…3387`). It was built (the Linux
test build) and run with the sprite pack from emeraldtogether-integration-20261012-3 (`battle-sprites.ebsp`:
948 species; private, not copied into this checkpoint). The sprites were drawn in every run (the log's
"Battle sprites: ... 948 targets").

Results, the game's effect sprites against the core's in every frame: all moves, single battle: 651 of 651 exact (Ally Switch without a partner crashed the game, as on this branch: known issues 1); the battle animations: 81 of 81 exact (the test build crashed once as it exited after POKEBLOCK_THROW, a Safari animation that does not end at a wild battle's menu; the run resumed after it - known issues 13); ten moves with pictures (Wailord against Joltik and back, a shiny wild Pokemon and a female Pikachu, a double battle): 40 of 40 exact; battles fought: Calvin (won, 3 of 3 exact), Tate & Liza (double) (won, 11 of 11 exact), Fabian's Mega Manectric (won, 5 of 5 exact), the Safari Zone (caught, 2 of 2 exact).

What the pictures show (`results\pictures\sprites_*`):

- Effects land on the right battler and in the right place. Flamethrower runs from a small Joltik into a
  Wailord and back. Thunderbolt strikes the target. Earthquake's dust covers all four battlers in a double
  battle. Surf's wave rises from the attacker.
- They are placed by the game's own coordinates for the battlers' 64x64 GBA boxes, as the original
  animations are. The sprite system aligns each animated picture on that box.
- The animated pictures are larger than the GBA box, Wailord most of all. An effect sized for the box
  therefore covers less of a big Pokémon, and a projectile starts from the battler's GBA centre: Wailord's
  middle, not its mouth.
- The library gives each facing its frame size and a feet point (`BslFacingInfo.anchor_x` and `anchor_y`),
  with no mouth or hand points. Scaling or aiming the HD effects by them is a renderer-side step, for a
  later round with the sprite session. It would leave the core's replay of the game untouched.
- Layering:
  - The back layer is drawn behind the battlers.
  - The front layer is drawn over them, the animated sprites' heads above the picture included.
  - The sprite session's overflow drawing comes first, then Battle FX's front layer.
- At the bottom of these test pictures is the native UI's panel area, which the test capture leaves out.
  Battlers and effects are cut at the same line there, as the GBA text box hides them in the game.
- Double Kick, Karate Chop, Trick and Avalanche are the improved versions (Lab v3). There is no floating
  footprint or hand; Trick swaps glowing bags along an arc, and Avalanche drops ice chunks in a snow cloud.

### The GBA ROM

Built from this branch after every change to a game file: SHA-256
`f214735b64e113fd10730eacca5764d757f2c93a3685230aa905af7a7cdd3387`, byte-identical.

## Performance

Measured on a 2-core cloud VM with software OpenGL (llvmpipe): the CPU draws every pixel, so these
are not a graphics card's numbers (a card draws the same passes in hardware; what that costs is for the
benchmark kit to measure). The core runs on a thread of its own (as the player build runs it).

**The core** (QuickJS interprets the package's JavaScript; `tools/battlefx/bfx_perf.c`, the game's
data, the heaviest moves, three plays each, steady state):

| Move | Frames | Begin ms (first play / then) | Step ms (average / p95) | Packet ms (average / p95 / worst) |
| --- | ---: | --- | --- | --- |
| Bolt Strike | 230 | 68 / 36 | 0.95 / 3.38 | 4.30 / 14.09 / 50.9 |
| Shock Wave | 250 | 24 / 26 | 0.86 / 2.19 | 2.90 / 7.20 / 11.7 |
| Zap Cannon | 85 | 8 / 8 | 0.76 / 2.15 | 3.01 / 10.60 / 23.5 |
| Volt Tackle | 265 | 27 / 26 | 0.83 / 3.25 | 2.22 / 6.59 / 38.1 |
| Eruption | 442 | 26 / 26 | 0.25 / 0.54 | 0.53 / 0.87 / 2.7 |
| Fairy Lock | 109 | 29 / 37 | 1.11 / 2.87 | 1.18 / 2.54 / 5.6 |
| Surf | 136 | 10 / 10 | 0.41 / 0.86 | 1.07 / 3.09 / 7.1 |
| Flamethrower | 127 | 14 / 15 | 0.50 / 0.94 | 0.82 / 1.34 / 6.9 |
| Blizzard | 364 | 42 / 42 | 0.63 / 1.86 | 1.45 / 3.48 / 22.9 |
| Sandstorm | 159 | 9 / 9 | 0.47 / 0.70 | 0.85 / 0.99 / 1.3 |
| Hyper Beam | 275 | 21 / 73 | 0.36 / 1.08 | 0.90 / 1.89 / 56.1 |
| Solar Beam | 101 | 10 / 10 | 0.41 / 0.75 | 0.72 / 1.02 / 2.7 |
| Thunder | 257 | 16 / 14 | 0.38 / 1.18 | 1.06 / 3.09 / 4.0 |
| Earthquake | 197 | 8 / 10 | 0.19 / 0.36 | 0.70 / 1.40 / 4.8 |
| Draco Meteor | 189 | 25 / 19 | 0.50 / 1.16 | 0.92 / 1.72 / 3.3 |

Begin = planning the animation when it starts (the game does not wait: the first picture waits at
most 60 ms once). Step = one frame of the replay; packet = the frame's drawing data. Over all the
galleries (three test runs at once on the 2 cores, so slower than one alone): begin 43 ms median (287 p95, 1061 at most, first plays
included), a step 1.75 ms median.

**The game with the HD layer on and off** (the fifteen heaviest moves at a wild battle's menu, no
frame cap, VSync off):

| Window | Effects | Animations | HD layer, ms (median) | Game frames a second (median / lowest) | Presents a second (median / lowest) | 99th percentile present interval, ms (median / worst) | GBA shown |
| --- | --- | ---: | ---: | --- | --- | --- | --- |
| 1280x720 windowed | HD, no budget | 16 | 33 | 42.5 / 15.9 | 15 / 10 | 101.1 / 179.0 | 0 |
| 1280x720 windowed | GBA | 16 | - | 59.7 / 46.0 | 70 / 21 | 27.9 / 93.7 | 0 |
| 1920x1080 borderless | HD, no budget | 16 | 70 | 8.3 / 5.3 | 7 / 5 | 206.6 / 349.7 | 0 |
| 1920x1080 borderless | GBA | 16 | - | 58.1 / 15.1 | 36 / 12 | 58.1 / 144.8 | 0 |
| 1280x720, the core on the game thread | HD, no budget | 16 | 33 | 33.6 / 15.4 | 14 / 10 | 128.0 / 230.4 | 0 |
| 1280x720 windowed | HD, the player builds' 8 ms budget | 16 | 35 | 59.9 / 50.5 | 76 / 29 | 24.2 / 110.8 | 16 (the budget) |

What this shows. Here the CPU also draws every pixel (llvmpipe), so the HD layers cost about 33 ms a
layer at 1280x720 and 70 at 1920x1080 (a picture draws two while an effect is both behind and in front
of the battlers): the GPU-to-GPU copy of the battle's region, the effects, a bloom chain of five
smaller targets and one composite, all rasterised by the CPU. The 3D battle paces the game by its
pictures, so with no budget the game fell to about 42 and 8 frames a second here while HD animations
played; with the GBA effects it kept about 60 (58 at 1920x1080, where the 3D world alone takes about
28 ms a picture on this VM). With the player builds' drawing budget (8 ms a picture) the first two
animations went on as GBA from their sixth picture (70 and 74 ms a picture, logged), the rest played the
original animations, and the game kept 60 frames a second. On a graphics card the same passes are
ordinary full-screen work; what they cost on yours at 1080p, 1440p and 4K is not measured here - the
benchmark kit measures it (its test build has no budget, so it shows the full cost; each game.log's
"drawn ... GPU ... ms a layer" lines give the layers' GPU time).

## Nothing about the battle changed — how that is kept and checked

- **Read only.** Battle FX reads the game's state at an animation's launch and at frame boundaries and
  writes none of it (`bfx_game.c`: "Everything here reads the game; nothing writes game memory"). The one
  getter pair that notes a battler for item messages (`GetBattlerHoldEffect` / `GetBattlerAbility`) is
  read with `gPotentialItemEffectBattler` put back. The core works on its own copy of the world.
- **The game decides when.** The core steps only when the game's animation advances, and an
  animation ends when the game's does; the battle waits for the game's animation as before. A core
  that is slow, refuses or fails changes nothing but the picture (its GBA effects show).
- **The GBA ROM is byte-identical** (SHA-256 `f214735b64e113fd10730eacca5764d757f2c93a3685230aa905af7a7cdd3387`,
  checked after every change to a game file): every hook is under `ENABLE_DIORAMA`, `PORTABLE` or the
  test builds' flags, with `#line` keeping the GBA build's line numbers (assertions embed them).
- **Game-code changes outside Battle FX** are three native-only crash fixes found by the galleries
  (`PORTABLE`, each where the GBA reads or writes past an array: Mimic and Struggle Bug's partner in a
  single battle, a sound number past the song table in Shadow Force's second turn) - none changes a
  battle's result - and test-build-only drivers.
- **Multiplayer.** Battle FX draws only; it sends nothing and reads nothing a partner's game does not
  have. Not run in a two-player battle.

## Found and fixed while integrating

Earlier rounds (stage 1-6, before this checkpoint's last day): the core reads the game's own sound
state (cries, the SE players: `waitsound`, Chatter's cries, the script's end), the game's random numbers
(`Random2` stream 29, `Random` for `SpriteCB_RandomCentredHits`), its sprite slots in use (Fairy Lock's
last bolt not made), the battlers' sprite places (a battler a move moved), the pan sound task
(`panse`), Spark's `x % 0`, the palette fade's 4-bit step, the Rollout / Ice Ball counters; lightning's
frame packets made twice as fast; the core moved to a thread of its own with the frame boundary pairing
its frames with the pictures; three native crashes fixed (above).

This checkpoint:

| Found by | What | Fixed |
| --- | --- | --- |
| general gallery | Knock Off's and Thief's item showed as GBA: the game frees the item's sheet while its sprite still shows it, and a heap template (the last used ball's icon) is freed once its sprite is made | effect sprites are told by the template they were made with, recorded in `CreateSpriteAt` |
| general gallery | Mega Evolution, Primal Reversion, Power Construct, Z-Move and Tera differed - the test driver's own doing: it reset the battlers' affine mode before them, as only a move's controller does | the test driver |
| a trainer's Mega Evolution, fought | a battle animation's script runs in a task (priority 10) after the sprites and OAM, not first in the frame as a move's: the core's sprites were a frame ahead | the request says `"order": "task"`; the core runs such scripts from a task of priority 10; the gallery now launches them through the game's launchers |
| Safari battle | a caught ball's stars outlive the script: they showed as GBA once the HD layer ended | the core goes on with the sprites a script leaves moving until the game's are gone |
| double-battle gallery | Parabolic Charge's double-battle branch ended early: the script's `goto ParabolicChargeHeal;` - GNU as ends a statement at `;`, the package's converter took it for part of the label | `convert_data.py` reads `;` as GNU as does |
| a test of the task order | every script task ran at priority 2 in the core; the game runs them by the script's priority (and sound tasks at 1) | the data carries the priorities; the core uses them |
| double-battle gallery | Doom Desire's and Future Sight's hit take the battle's target again (`AnimTask_SetAnimTargetToBattlerTarget`); the core kept the request's | the request carries the battle's target |
| a gym double battle, fought | a shiny Pokemon's stars beginning while the opening send-outs' particles (played as GBA) still flew hid those particles | such a controller animation plays as GBA |
| the double-battle gallery's results | a visible wild Pokemon could walk into the player before the test driver started its battle: a "double battle" gallery had run in a single one | the field stands still while a test battle waits (`PKMN_TEST_FREEZE_FIELD`); a run in the wrong battle is flagged NOT VALID |
| the battle animations played as the opponent | an opponent's Ultra Burst: this game's `AnimSuperpowerOrb` starts at the battle's attacker (`gBattlerAttacker`), the package's port at the animation's; the gallery's battle had another attacker (in a battle the two are the same for a gimmick: `TryActivateGimmick`) | the request carries `battlerAttacker`; the core follows the game in that callback, `AnimSuperpowerFireball` and `AnimDragonDanceOrb` |
| the same run (the trainer's ball throw at a wild battle's menu) and the game's code | Wally's catch tutorial: Wally comes back out and throws (`BattleScript_ActionWallyThrow`), but the request said the player's left battler is the trainer only in the Safari Zone, and the tutorial's ball starts elsewhere (`AnimTask_ThrowBall_StandingTrainer`) | `safari` also once Wally is out; `catchTutorial` moves the ball; a test battle for the tutorial (fought: every animation exact) |
| the performance runs (software OpenGL) | drawing the HD layers slowed the game during HD animations (42 and 8 frames a second at 1280x720 and 1920x1080): the 3D battle paces the game by its pictures | the player builds' drawing budget (8 ms a picture): over it an animation goes on as GBA (logged), after two the HD effects are off for the battle; checked: the game at 60 |
| reading the character session's branch (COORDINATION.txt 5) | the core times the trainer's throw by the Hoenn back pictures' frames; the custom characters' back pictures throw 4 frames sooner | the request carries the back picture's own animation table (`trainerAnims`); checked by playing the throws with a deliberately wrong built-in table: exact with the change, different (from frame 29 / 30) without it |

## Known issues and limits

1. **Ally Switch** without a partner crashes the native game (the GBA reads `gSprites[255]`). Only the
   gallery forces it: in a battle Ally Switch fails before its animation without an ally
   (`tryallyswitch`). In a double battle it is exact.
2. **One HD animation at a time.** A double battle's opening send-outs (two at once) play as GBA; when a
   shiny Pokemon is sent out, the send-out's HD stops when the stars begin and both finish as GBA; a
   controller animation beginning while another's sprites are on screen plays as GBA. If a battle
   animation starts while a previous one's leftover sprites still move, those are hidden early.
3. **Contests** are not drawn by Battle FX (their animations play as before).
4. **The core's cost** (QuickJS interprets the package's JavaScript): planning an animation at its
   begin takes 43 ms (median; up to 1061 ms) on the core's thread, a frame
   1.75 ms (median of the averages; the galleries, three runs at once on a 2-core VM), with occasional garbage-collection frames of tens to hundreds
   of ms. The game never waits for it; a picture waits at most 60 ms once for an animation's first
   frame and 8 ms when 3 or more frames behind. On a slow CPU an animation whose core falls more than 6
   frames behind plays as GBA from there (logged). Measured on a busy 2-core cloud VM, not a gaming PC.
5. **Placement.** Effects are placed in the battle picture's frame, where the battlers are (they are the
   picture's own sprites), and drawn at the window's resolution; the 3D world behind is drawn at the
   chosen render resolution and its camera glides between game frames - the effects ride with the
   battlers, not with the world.
6. **What the parity check sees.** Effect sprites (graphics tag and screen position) every frame, and
   the animations' lengths. Palette blends, BG layers and battler offsets are not compared.
7. **Not in a fought battle yet**: the player's own Mega Evolution (the autopilot only presses A), a
   Pokeblock throw (needs the Pokeblock case), a voluntary switch, Z-Moves / Tera (gallery only), a
   two-player battle, the animated sprites on this branch (only in the trial merge, `claude/battle-fx-sprites-trial`).
8. **The package (Lab) has the same two data issues** (Parabolic Charge's double branch, the tasks'
   priorities); upstream below.
9. **The player's lead slides in** without a ball when it follows the player on the field (this game's
   followers): there is no send-out to draw then, only a shiny lead's stars. Where the follower is
   hidden (the four other environments' battles) the ball send-out is drawn in HD - and the test
   save's lead is shiny, so its stars stop the send-out's HD (2).
10. **Round 4's multiplayer freeze is on this branch.** A native multiplayer battle in this branch's 3D
   game freezes as it begins: the battle environments' backdrop reads a trainer class for the other
   player (found by the EmeraldTogether session; fixed by `f86368c23` on `claude/combined-dev`, which
   merges with this branch without a conflict). Not Battle FX's; this branch keeps round 4 as it was.
   The Windows player here is meant for single-player battles.
11. **The drawing budget's 8 ms is a starting point**, from a 60 Hz frame's arithmetic (half for the HD
   layers, half for the 3D battle), not measured on a graphics card. A PC whose card takes longer than
   that at its resolution plays the original animations, logged; `drawBudgetMs=0` in
   `battlefx\settings.cfg` draws them anyway, `drawBudgetMs=12` allows more.
12. **Wally's catch tutorial can end without the catch** (the game's, not Battle FX's): in the suite's
   run his Zigzagoon's second Tackle knocked the Ralts out, so the battle ended as won and no ball was
   thrown. The tutorial has no critical hits, but Tackle has 40 power in this game and both Pokemon get
   random IVs and natures (`LoadWallyZigzagoon`, `StartWallyTutorialBattle`). Run again, it went as the
   story wants (the throw, the catch). Noted for the game's own balance; Battle FX changes no damage.
13. **The test builds exit from the game's thread** (`exit()` in the test drivers, on the `AgbMain` thread:
   after an animation that does not end, and at the end of a run) while the main thread may be drawing.
   Once, in the sprite trial's battle-animation gallery, that exit crashed: signal 11 in the OpenGL
   driver, the main thread's stack inside `DioramaGL_Present` (crash.log) while the game's thread was
   exiting. The runner resumed after it and every animation was exact; results are written before the
   exit. Test builds only (the player builds never exit there). Should a benchmark run end with a
   crash.log, its results are complete. The fix belongs in the test drivers (`_exit` after flushing, or
   leaving the exit to the main thread).

## Upstream for v3.4 (the package)

What the game had to add over the package's core, all in `bfx_game.c` (`kGameHelperJs`, installed over
the runtime when the core loads; none changes the Lab's behaviour) and `tools/battlefx/convert_data.py`.
They belong in the package so the Lab and the game run the same core:

- the request's game state: the script's branch (`variant` / `BFX_GAME_VARIANTS`), `rng2` / `rng1` /
  `rng1PerFrame` (the game's SFC32 streams), `rolloutTimer`, `spritesInUse`, `battlerSprites`,
  `battlerTarget`, `battlerAttacker`, `order: "task"`, `catchTutorial`, `trainerAnims` (the back picture's
  own frames), and `safari` meaning "the player's left battler is the trainer" (the catch tutorial too);
- the live sound state (`BFX_GAME_SOUND`, `se_frames.json`), `stopsound`, a sound by number, the `panse`
  task;
- the runtime: the script run from a task of priority 10 for battle animations; tasks at their own
  priorities (`createsoundtask` at 1); `AnimTask_SetAnimTargetToBattlerTarget`; the battle's attacker in
  `AnimSuperpowerOrb` / `AnimSuperpowerFireball` / `AnimDragonDanceOrb` (this game's; vanilla Emerald reads
  the animation's); the catch tutorial's ball start; Spark's `x % 0`; the
  palette fade's 4-bit step; the end that waits for sprites left moving; the hidden-BG mask
  (`BFX_GAME_HIDDEN_BGS`); the Poke Ball check (`BFX_GAME_HAS_BALL`);
- the renderer side: the faster ribbon packing (`packRibbons`);
- the converter: `panse`'s pan step and delay, the tasks' priorities, `;` ending a statement.

## Merging, once it is validated on your PC

Nothing is merged and nothing is pushed: `claude/battle-fx-integration` waits for your go.

1. **Validate first** (your PC): play with `windows-player-battle-fx.zip`; run
   `benchmark\run-battlefx-benchmark.bat` (the graphics card's numbers at 1080p, 1440p and 4K).
2. **Restore** (README.txt, RESTORE): battle3d-env-wip-20261012-2's engine bundle (`aa63a98a3`),
   ui-native-wip-20261012-4's (`035b263cc`) and emeraldtogether-integration-20261012-3's (`8706f8f6c`)
   first, then this checkpoint's.
3. **Order.** This branch already contains the battle environments' round 4
   (`claude/battle-environments-3`, merged at `865fbf745`). Merge it into the line that is to receive
   the battle environments, after them: `git merge --no-ff claude/battle-fx-integration`. If the battle
   environments have moved on since `aa63a98a3`, merge their newer tip into `claude/battle-fx-integration`
   first (the only conflict so far was two declarations in `gl_battle.c`, COORDINATION.txt 2) and run the
   checks below again.
4. **With the other lines** (git merge-tree against the tag's commit): the combined development game
   `claude/combined-dev` (`22661c214`: the overworld's newest, round 4, the multiplayer freeze fix)
   merges without a conflict - the natural next step; the native UI (round 4 `035b263cc`, round 5
   `31887d7dd` and the newest, ui-native-wip-20261013-3 `0b7523cdb`) conflicts in the same four files with
   the same hunks, all mechanical (COORDINATION.txt 4) - `claude/battle-fx-ui-trial`
   shows them resolved and builds (Linux 3D player, here; not run): use it as the reference for the resolution, not as the
   branch to merge; the EmeraldTogether integration (`8706f8f6c`, preview 9) conflicts in four files,
   all additions on both sides (COORDINATION.txt 10) - `claude/battle-fx-sprites-trial` is that merge,
   built and tested with the animated sprites.
5. **Checks after any merge**: `make` (the GBA ROM) still gives SHA-256 `f214735b…3387`; the test build
   (`make -f Makefile_pc DIORAMA=1 CC="gcc -DDIORAMA_BATTLE_ENV_TEST -DBATTLEFX_TEST"
   TARGET=pokeemerald-mp-diorama-bfxtest OBJ_DIR=build/pc-linux-diorama-bfxtest`, `NATIVE_LINUX=1` on
   Linux) runs `tools/battlefx/gallery.sh` (all moves single and double, all battle animations) and
   `tools/battlefx/realsuite.sh`; `tools/battlefx/summarize.py` must show every animation exact.
6. **Rollback.** Before the merge the main line is as it was; `battlefx-int-premerge-20261012` is this
   work before round 4; `PKMN_BATTLEFX=0` or `battlefx\settings.cfg` `enabled=0` turns Battle FX off
   without a rebuild (the game's own animations, as before).

## This checkpoint

`C:\Users\kashh\AppData\Local\PokemonPrivate\claude-checkpoints\battlefx-int-wip-20261012-1\` (PRIVATE)

| File | |
| --- | --- |
| `engine-battle-fx-integration.bundle` | `claude/battle-fx-integration` (tag `battlefx-int-wip-20261012-1`), the trials `claude/battle-fx-ui-trial` and `claude/battle-fx-sprites-trial`, tag `battlefx-int-premerge-20261012` (needs `aa63a98a3`, `035b263cc` and `8706f8f6c` restored first) |
| `windows-player-battle-fx.zip` | the Windows 3D player with Battle FX, SDL2.dll, the `battlefx` folder, BATTLE-FX.txt |
| `test-run\`, `test-run.zip` | the Windows test build the benchmark uses, with its `battlefx` folder (and the same packed) |
| `benchmark\` | `run-battlefx-benchmark.bat`, the battle sessions' private test save |
| `debug-symbols\` | the two Windows executables' debug symbols (xz) |
| `results\` | the final runs' summaries (Markdown, JSON); pictures from the battles fought (`pictures\<battle>__*`), the sprite trial (`pictures\sprites_*`) and the Wine runs (`wine\`) |
| `INTEGRATION_REPORT.md` | this report (also `src/diorama/battlefx/INTEGRATION_REPORT.md` on the branch) |
| `COORDINATION.txt` | for the other sessions |
| `README.txt`, `SHA256SUMS.txt` | |

The earlier checkpoints (the Lab's, the battle environments', the native UI's) are untouched.
