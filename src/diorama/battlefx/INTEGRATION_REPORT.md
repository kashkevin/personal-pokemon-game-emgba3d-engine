# Battle FX in the EmeraldTogether game — integration report, round 2

Checkpoint **battlefx-int-wip-20261013-1** · engine branch `claude/battle-fx-integration` (the code of
`dc7fb055c`) · 2026-10-13

PRIVATE: the builds in this checkpoint contain game data, and `src/platform/bios.c` still contains
GPL/MPL-derived code. Do not share them.

This round does what your last message asked before the final handoff: the effects placed and sized for
the animated Pokemon sprites, a performance fallback that decides on this PC's own measurements instead of
a fixed 8 ms, and the handoff to the main EmeraldTogether session. The first integration
(battlefx-int-wip-20261012-1, `2907a098b`) is unchanged and kept: its report - in its checkpoint folder,
and on the branch as `INTEGRATION_REPORT_ROUND1.md` - covers everything before this round (all 621 moves
and 83 battle animations replayed frame for frame, the battles fought, the merge checks).

## Summary

- **Placement for the animated sprites.** Where the animated battle sprites show a battler (a frame of its
  own size - Wailord's front 89 x 75 where its GBA picture is 64 x 56, Lugia's front about 115 x 75),
  Battle FX now reads what is drawn, through the sprite system's own renderer interface (read only), and
  places the effects near each battler for its body as shown: moved with the body's centre and scaled with
  its size, per battler, front and back, singles and doubles, also while a script has moved the battler
  onto a background layer. The battler's own glow, flash and light are drawn from the frame shown (before,
  a flash drew the GBA picture's shape over the animated sprite). Battlers the pack does not hold (Megas,
  Primals, Gen 6, the Substitute doll) and every battle without the animated sprites draw exactly as
  before - checked bit for bit (below).
- **Quality and the fallback.** The fixed 8 ms drawing budget is gone as the policy. The HD effects now
  have quality tiers (full; high: 3/4 resolution; medium: half; low: half, no bloom or distortion) and the
  player builds choose by measuring this PC while it plays: nothing changes while the game shows all its
  60 frames a second, however much the effects cost; only when game frames go unseen and the HD layers'
  measured GPU time is part of why does the next tier down draw (on trial when the evidence is small,
  undone when it does not help); if even the lowest tier leaves the game under three quarters of its
  frames, the animation plays its GBA effects, as before.
  `quality=auto|full|high|medium|low|gba` in `battlefx\settings.cfg` (or `PKMN_BATTLEFX_QUALITY`) fixes it;
  a settings menu can set it (`BattleFx_SetQuality`). The 621 moves and the 81 status, general and special
  animations are untouched (the package's data and core are unchanged).
- **Handoff.** Battle FX merges into the unified game (`claude/emeraldtogether-unified` `8c8c11ab2`) with
  four conflicting files, all additions on both sides. `claude/battle-fx-unified-candidate` is that merge,
  ready to take: one merge commit with the tree of `claude/battle-fx-unified-trial`, where it was built
  and tested with the sprite pack. The newest native UI, handed over at the same time
  (ui-native-handoff-20261013-1), and Battle FX merge into each other without a conflict in either order
  (the same tree); that combined tree was built and its battles played here. The character customization
  is already in the unified game. Step by step: section 3 and COORDINATION.txt.
- **Verified here** (Linux, software OpenGL, the test builds): the placing on the unified trial with the
  sprite pack - seven galleries of twelve moves (Wailord, Joltik, Rayquaza, Lugia, Altaria, Pidgeot,
  Gardevoir; fronts and backs; a double battle) 84 of 84 animations exact, every move animation with Wailord
  against Joltik 651 of 651 exact, the Mega Evolutions drawn bit-identically with the placing on and off,
  four battles fought with the HD layer drawn (20 of 20 exact); without the sprites, the HD layers
  bit-identical to the first checkpoint's (108 of 108 read back), the 81 battle animations exact, four
  battles fought (21 of 21) and Battle FX off; the tiers (every frame exact at each) and the auto policy on
  software OpenGL (from `full` down to `low` where this machine fell behind, `low` kept while the game still
  showed three quarters of its frames, the GBA effects below that); the combined tree - the unified game,
  the newest native UI, Battle FX - built and four battles fought (18 of 18); the Windows builds under Wine;
  the GBA ROM byte-identical.
- **Not verified:** anything on a real graphics card or Windows itself (the benchmark kit in this
  checkpoint measures the tiers on your PC), a two-player battle, the player's own Mega Evolution, and the
  unified game's own player build with Battle FX (that build is the main session's to make).

## 1. Effects placed for the animated sprites

### What the animated sprites change

The scripts place every effect for the GBA picture: its 64 x 64 box, the game's coordinates for the
battler, its height and elevation. The sprite system (sprite-integration-v2, in the unified game) draws
each battler from its pack in place of the GBA picture: a frame of its own size standing on the GBA
picture's feet line and centred on its body. Compared over the pack (1588 facings with a GBA picture to
compare; the opaque boxes, the frames averaged by their durations):

| | |
| --- | --- |
| scale of the shown body against the GBA picture's (square root of width ratio x height ratio) | median 1.05, 90th percentile 1.25, 95th 1.34, largest 2.17 (Wailord's back), smallest 0.55 |
| larger by a fifth or more | 14.6 % of facings; by two fifths or more 3.2 % |
| smaller by a tenth or more | 17.5 % (back pictures mostly: their median is 0.99, fronts' 1.09) |
| body centre moved 8 GBA pixels or more | 10 % (the shown body is taller and stands on the same feet) |

Examples: Wailord's front 89 x 75 against 64 x 56, its back 97 x 66 against 64 x 21 (the GBA back
picture is mostly below the text box); Rayquaza's back 80 x 93 against 58 x 64; Lugia's front 115 x 75
against 60 x 64; Joltik's front 41 x 32 against 32 x 26; Pikachu's back 36 x 44 against 57 x 55.

So an effect aimed at a large battler's GBA centre hits its lower body, a shield sized for 64 pixels
covers half of Wailord, and a projectile starts from the middle of a body rather than near its head.

### How Battle FX places them now

Read only, through the sprite system's renderer interface (`include/platform/battle_sprites.h`):
`gBattleSpriteObjViews` (the frame drawn in place of an OAM entry: its pixels, size, place in the entry's
box, colours, the fainting clip) and `gBattleSpriteBgCopies` (the copy a script puts on BG1 / BG2,
"monbg"). Nothing of the sprite system or the sprite library is changed; Battle FX's own file
`src/diorama/battlefx/bfx_attach.c` is the only place that reads them (`include/diorama/battle_fx_attach.h`),
and without `ENABLE_BATTLE_SPRITES` it reads nothing.

Every battle picture, per battler (`gl_battle_fx.c`, `MeasureLook`):

- the GBA picture's body: the opaque box of the picture in OAM (decoded as the GBA draws it);
- the shown body: the opaque box of the frame drawn, averaged over the frames of one look (an idle
  animation moves a few pixels; the effects should not shake with it). A look is one canvas (each facing
  of each form has its own) and about the same GBA picture - a new Pokemon, form or facing starts anew;
- from the two: the shift of the body's centre, one scale for both axes (the square root of the two
  ratios, held to 0.75 .. 1.6 - effects keep their shapes, and a back picture the text box cuts short
  does not stretch them threefold), and the region to place: both bodies, a tenth more.

While an animation draws, the effects near each battler are moved with that centre and scaled about it on
the GBA screen, and lifted back to the world at their own depth exactly as the core lifts its points (its
`Space.lift` / `Space.project`): in full inside the region, fading out by 1.8 times its size, shared where
two battlers' regions meet (doubles), less for elements over 96 GBA pixels and not at all from 176 (a
screen-wide card stays). Cards and distortion cards (centre placed, card sized), particles (where each is
now: one flying from a battler to another leaves the one body and reaches the other), ribbons (each
vertex: a beam's two ends meet both bodies), meshes (centre placed, mesh sized) and the lights that light
them; ground decals scale about the battler's feet on the ground. This is code added to the card,
particle, ribbon and mesh shaders as they load - the shader files are the Lab's generated ones, unchanged;
a shader that is not the expected one loads without it, all four or none - and with no battler shown by
another frame every expression is the shader's own.

A battler a script moves onto a background layer ("monbg": Protect's and Swords Dance's user, Flamethrower's
target...) leaves OAM; the sprite system draws the copy from its frame too, and the look measured while it
was a sprite goes with the copy, about where the core has it (the core follows the layer's scroll).

The battler's own HD state - the glow behind it, the flash and the effects' light on it, its depth for
meshes behind it - is drawn from the frame shown (its shape and faded colours, placed as
`gba_easy_draw.c` `DrawLibraryObjLine` places it, not cut to the 64 x 64 box; the glow grows about the
shown body's centre). In the sprite trial of the last checkpoint these were still drawn from the GBA
tiles that stay in VRAM: a hit's flash showed the GBA picture over the animated sprite.

`attach=0` in `battlefx\settings.cfg` (or `PKMN_BATTLEFX_ATTACH=0`) keeps the GBA placing (the battlers
are still drawn from their frames). With `PKMN_BATTLEFX_LOG=1` each look is logged once ("battler 1 is
shown 89 x 75 (its GBA picture 64 x 56): the effects near it are moved +1.8, -14.7 pixels and scaled
1.37"). Drawing only: the core's replay and its frames, the parity with the game, the animations'
timing and the battle are untouched.

### Results

On the unified trial's test build with the sprite pack: seven galleries of twelve moves - Flamethrower,
Thunderbolt, Protect, Hyper Beam, Rock Slide, Swords Dance, Fire Spin, Slash, Absorb, Psychic, Earthquake,
Spore: projectiles and beams from one body to another, a shield around the user, a vortex around the
target, energy drained back, a strike, a screen-wide effect, a ground shake. Each look as the log names it
(the boxes are the bodies' opaque pixels, in GBA pixels; "up" moves the effects up the screen):

| Gallery (12 moves) | Each look placed: the body shown against the GBA picture's (opaque boxes, GBA pixels) - the centre moved, the scale | Animations exact |
| --- | --- | --- |
| Wailord (back) attacks Joltik (front) | Joltik's front 41 x 32 against 32 x 26: up 4, x 1.25; Wailord's back 99 x 63 against 64 x 21: up 24, right 2, x 1.60 (the limit) | 12 of 12 |
| Joltik (back) attacks Wailord (front) | Wailord's front 93 x 70 against 64 x 56: up 12, x 1.35; Joltik's back 40 x 30 against 47 x 25: up 4, x 1.01 | 12 of 12 |
| Joltik (front) attacks Wailord (back) | Joltik's front 41 x 32 against 32 x 26: up 4, x 1.26; Wailord's back 97 x 65 against 64 x 21: up 26, right 4, x 1.60 (the limit) | 12 of 12 |
| Lugia (back) attacks Rayquaza (front) | Rayquaza's front 89 x 83 against 61 x 64: up 13, right 9, x 1.37; Lugia's back 126 x 73 against 62 x 62: up 10, left 2, x 1.55 | 12 of 12 |
| Rayquaza (back) attacks Lugia (front) | Lugia's front 123 x 68 against 60 x 64: up 8, left 3, x 1.48; Rayquaza's back 76 x 94 against 58 x 64: up 24, left 8, x 1.39 | 12 of 12 |
| Altaria (back) attacks Pidgeot (front) | Pidgeot's front 55 x 67 against 64 x 61: up 3, left 1, x 0.98; Altaria's back 57 x 69 against 62 x 43: up 14, right 1, x 1.22 | 12 of 12 |
| double battle: Gardevoir and Wailord (backs) against Wailord and Lugia (fronts) | Wailord's front 86 x 82 against 64 x 56: up 15, right 2, x 1.41; Lugia's front 105 x 83 against 60 x 64: up 17, left 8, x 1.51; Gardevoir's back 67 x 74 against 42 x 52: up 12, left 2, x 1.51; Wailord's back 99 x 63 against 64 x 21: up 24, right 1, x 1.60 (the limit) | 12 of 12 |

Every one of the 84 animations exact (the placing draws; it never changes the replay). `results\placement\`
has a sheet per gallery: six of the moves at the same frame, placed for the battlers as shown (left) and as
for the GBA pictures (right, `attach=0`). In them: Flamethrower and Thunderbolt reach Wailord's and Lugia's
bodies where they are drawn, not their lower edges; Fire Spin's vortex wraps the large bodies instead of
their lower halves; Protect's shield is sized for Rayquaza's and Gardevoir's backs; Hyper Beam starts at the
user's body as shown and ends in the target's.

More, on the trial: every move animation (621 moves, 651 animations) with Wailord's back against Joltik's
front and the HD layer drawn, 651 of 651 exact (the looks followed Transform: Wailord in Joltik's shape);
Mega Rayquaza against Mega Salamence, drawn from their GBA pictures (the pack holds no Mega forms),
bit-identical with the placing on and off (23 of 23 layers read back), and against Wailord only Wailord is
placed; Pikachu's back, smaller in the pack (40 x 40 against 58 x 55), placed at x 0.75; four battles fought
with the HD layer drawn - Calvin, Tate & Liza (all four battlers placed, Wailord's back among them), Fabian
(his Manectric placed as shown, then its Mega Evolution - a GBA picture - as before) and the Safari Zone: 20
of 20 animations exact.

## 2. Performance: quality tiers and a fallback decided on this PC

### Why not a fixed number of milliseconds

The game runs its 59.73 frames a second on its own clock; the drawing does not pace it (`sdl2.c`: a
present that takes longer leaves game frames unseen, and below about 12 presents a second the game itself
slows). What a player sees is whether every game frame reaches the screen. The HD layers' cost is mostly
pixels: each layer copies the window region, draws the effects into a half-float target the size of the
region, runs a five-level bloom chain and composites. Estimated from the passes' sizes (not measured on a
graphics card), that is on the order of 100 MB of memory traffic a layer at 1920 x 1080, two layers while
an effect is both behind and in front of the battlers - divided by a graphics card's memory bandwidth,
well under a millisecond on a current desktop card and a few milliseconds on an integrated one - and it
grows with the window (4K is four times 1080p). On this cloud machine's software OpenGL, where the CPU
draws every pixel, the measured cost is in the table below. A fixed 8 ms budget is wrong both ways: it
would play GBA effects on a PC that keeps 60 frames a second at 9 ms of HD work (the 3D battle taking the
rest), and it says nothing on a high-refresh display, where every frame has less time but the game's own
60 frames are still all shown. Engines that adapt their resolution measure instead - Unreal Engine's
dynamic resolution, for example, sets the screen percentage from "the previous frames' GPU workload": a
frame time budget, a targeted headroom under it, a history of frames, and by default between 50 and 100 %
of the resolution (its documentation, linked at the end).

### The tiers

The same effects for less (`BattleFxGL_SetTier`): the effect layers are drawn at a share of the window's
resolution and scaled up as they are composited; the copy of the window and the composite stay at the
window's resolution (so the picture under the effects is untouched).

| Tier | Layer resolution | Bloom | Distortion |
| --- | --- | --- | --- |
| full | the window's | 5 levels | on |
| high | 3/4 | 5 levels | on |
| medium | 1/2 | 4 levels (the same size on the window) | on |
| low | 1/2 | none | none |

Measured here: six of the heaviest moves (Bolt Strike, Eruption, Surf, Hyper Beam, Blizzard, Thunder) at a
wild battle's menu, each tier fixed, in a 1834 x 1032 window (the largest the cloud's virtual screen
holds), the game waiting for the core every frame as the galleries do. This is software OpenGL (llvmpipe on
two CPU cores): the "GPU time" is the CPU drawing pixels. The milliseconds say nothing about a graphics
card; the proportions show what each tier saves when pixels are the cost.

| Tier | HD layers, a layer (median of the six moves) | a picture (1.6 - 2 layers) | against full | pictures a second |
| --- | --- | --- | --- | --- |
| full | 83.3 ms | 152.3 ms | 1.00 | 6.2 |
| high | 54.7 ms | 104.0 ms | 0.68 | 8.3 |
| medium | 42.6 ms | 81.2 ms | 0.53 | 10.0 |
| low | 33.5 ms | 61.9 ms | 0.41 | 11.7 |
| GBA effects (no HD layer) | - | - | - | 36.0 |

Every frame of every animation was exact in all five runs (1739 of 1739 compared frames each: the tiers
change the drawing, never the replay). On a CPU the fixed part of a layer - the copy of the window and the
composite, both at the window's resolution - keeps `low` at about 40 % of `full`; on a graphics card the
proportions will differ, and the benchmark measures them. For raising a tier again the policy assumes
larger steps than these (full 1.8 times high, high 2.25 times medium, medium 1.4 times low: the pixel
counts and the bloom), so it raises only with room to spare.

Pictures of one frame at each tier: `results\tiers\` (Hyper Beam, Surf, Psychic; the whole window at half
size, and the part at full size where `full` and `low` differ most). `high` and `medium` are a little softer;
`low` has no glow (bloom) around bright effects, so a beam shows its inner strands and looks thinner. The
shapes, colours and timing are the same at every tier.

### The policy (`quality=auto`, the player builds' default)

Measured on the PC itself, at every frame boundary (`bfx_game.c` `PaceCheck`; the measurement
`gl_battle_fx.c` `BattleFxGL_Pace`), over half-second windows of 30 game frames that showed HD layers
(each animation's first 6 pictures left out: textures uploaded, programs first used):

- **The game's pace kept** - at most one game frame of 30 never presented, and at least three quarters of
  the game's 60 frames shown a second of the clock (below about 12 pictures a second the game slows down
  instead of leaving frames unseen - `sdl2.c` then runs one game frame a picture - so unseen frames alone
  would take a slide show for the pace kept): nothing changes, whatever the HD layers cost. Every PC that
  keeps up is left at full quality.
- **Game frames unseen (or a slide show), and the HD layers could close much of the gap** - their GPU time
  (timer queries; the CPU's time where a driver has none) at least half of the pictures' lateness, or a
  quarter of each picture (when the pictures are faster than game frames on average and frames go unseen
  through uneven pictures, only the share counts): the next tier down, from this picture on.
- **Game frames unseen, and the HD layers a smaller but real part** (a tenth of a game frame, 1.7 ms, or
  more): the next tier down **on trial** - with VSync a picture slightly too slow waits a whole refresh, so
  the time between pictures understates how little would suffice. If the next window does not win back
  at least half the frames lost, the tier goes back and no more trials this battle: the HD effects are
  not what holds the game back here.
- **Less than that**: kept (logged once a battle: the slowness is elsewhere - the 3D world, the CPU).
- **At low, and the game still under three quarters of its frames because of them** (fewer than 45 shown
  a second; `gbaBelowFps`): that animation plays its GBA effects from that picture (as when the core falls
  behind), and after two such the battle plays the original animations (the existing mechanism; the next
  battle tries again). Above that, `low` stays: a few frames lost, the HD effects kept (logged once a
  battle).
- **Raising again**: at a battle's start, if the last battle kept the pace at this tier for 300 game
  frames or more with none lost and the next tier's estimated cost stays within a quarter of a game frame,
  the tier goes up one; a tier raised that has to be lowered again is not raised again this session.

Every change is logged ("the game showed 41.8 of its 60 frames a second while the HD effects took 9.6 ms
of each 24.1 ms picture on the GPU: high quality from now on"). The thresholds are fractions of the
game's own frame and of what was measured, not absolute milliseconds: one game frame of 30 for the pace,
three quarters of its frames for a slide show and for the last fallback; the one absolute number is the
floor for a trial (a tenth of a 60 Hz frame).

Measured here with `quality=auto` and the core on its own thread, as in the player build: the six heavy
moves at a battle's menu (Bolt Strike, Eruption, Surf, Hyper Beam, Blizzard, Thunder) at three window
sizes, and a battle fought. Software OpenGL draws far slower than a graphics card, so every one of these is
a PC that cannot keep up at `full` - the case the policy is for:

| Window | What the policy did (its log) | Animations that ended with their GBA effects |
| --- | --- | --- |
| 1834 x 1032 | full, high, medium, low within the battle's first animations; even at `low` the game showed 10 - 11 of its 60 frames a second (about 60 ms of HD work a picture: a slide show), so Bolt Strike and Eruption went on with their GBA effects, and after those two the battle played the original animations | 6 of 6 (two from mid-animation, then the battle's rest) |
| 640 x 360 | full to `low` by Bolt Strike; at `low` Eruption showed 53.6 frames a second: kept, until its heaviest part (43.9) went to its GBA effects; Surf and Hyper Beam drew in HD at `low`; Blizzard (34.8 a second) went to its GBA effects - the second, so Thunder played the original | 3 of 6 |
| 480 x 270 | full to `low` by Bolt Strike; Eruption's heaviest part (42.6 a second) went to its GBA effects; Surf, Hyper Beam, Blizzard (47.4 a second: kept) and Thunder drew in HD at `low` | 1 of 6 |
| 640 x 360, Calvin fought | full to medium in the send-out, `low` in Flamethrower; at `low` 56.6 frames a second: kept; the battle won | none |

Two changes came from these runs. The first version of the last step (the GBA effects for any frame lost
at `low`) sent 5 of 6 animations to their GBA effects at 640 x 360 and at 480 x 270, three of its four
fallbacks with the game still showing 53 - 56 frames a second: hence the floor (`gbaBelowFps`, three
quarters of the game's frames; `dc7fb055c`). And the first runs at 1834 x 1032 counted a slide show's
frames as shown - below about 12 pictures a second the game slows rather than skipping frames: hence the
pace measured against the clock (`3f84eeea0`). With the core not waiting, these runs compare no frames
(the galleries with the tiers fixed do).

What this machine cannot show is a PC that keeps up: there the policy changes nothing - its first test is
the pace kept - and the benchmark's `auto` run measures that on your graphics card.

### Configuration

| Setting (`battlefx\settings.cfg`) | Environment | |
| --- | --- | --- |
| `quality=auto` | `PKMN_BATTLEFX_QUALITY=auto` | the player builds' default (above) |
| `quality=full` / `high` / `medium` / `low` | the same | one tier, no automatic change (the test builds: `full`, so galleries and the benchmark draw everything) |
| `quality=gba` | `PKMN_BATTLEFX_QUALITY=gba` | the original animations (as `enabled=0`) |
| `gbaBelowFps=45` | `PKMN_BATTLEFX_GBA_BELOW_FPS=45` | `auto`'s last step: at `low`, the GBA effects when the game shows fewer frames a second than this because of the HD effects (0: never; 60: at any frame lost) |
| `attach=0` | `PKMN_BATTLEFX_ATTACH=0` | effects placed for the GBA pictures |
| `drawBudgetMs=N` | `PKMN_BATTLEFX_DRAW_BUDGET_MS=N` | optional explicit budget (none by default): an animation over it plays as on the GBA |
| `enabled=0` | `PKMN_BATTLEFX=0` | Battle FX off |

For a settings menu (the native UI's F10 screen, for example): `BattleFx_Quality()`,
`BattleFx_SetQuality(BATTLEFX_QUALITY_*)` and `BattleFx_QualityName()` in `include/diorama/battle_fx.h`
(main thread; it takes effect from the next picture). Not wired into any menu here - the menus are the UI
session's.

### What to measure on your PC

`benchmark\run-battlefx-benchmark.bat` plays fifteen of the heaviest moves at a wild battle's menu, each run
about 100 seconds (the game's own 60 frames a second): 1920 x 1080 at `full` and with the GBA effects, 3840 x
2160 at every tier and with the GBA effects (VSync off, no frame-rate limit), then once with `quality=auto`
and VSync on, as a player plays - 8 runs, about 14 minutes; `quick` 4 runs, about 7 minutes; `all` every
tier at 1080p, 1440p and 4K, about 27 minutes. Each run's `report.jsonl` gives per animation the HD layers'
GPU time, the tier and the pictures a second; `game.log` the auto policy's decisions. Claude reads the results
folder. These are the real graphics-card numbers this round could not get in the cloud. (Borderless, the
window is the display's own size: on a 1080p display the "4K" runs draw the effects at 1080p - each
`game.log` names the sizes.)

## 3. Handoff: Battle FX in the unified EmeraldTogether build

Nothing is merged into another session's branch and nothing is pushed: the unified game is the main
EmeraldTogether session's to build. This is what it needs.

**What is where** (engine; all in `engine-battle-fx-integration.bundle`, which needs `2907a098b` - the
first integration's bundle - and `8c8c11ab2` - emeraldtogether-unified-20261013-1's - restored first; the
exact commits and trees are in COORDINATION.txt section 1).

| | |
| --- | --- |
| `claude/battle-fx-integration`, tag `battlefx-int-wip-20261013-1` | THE BATTLE FX BRANCH: the first integration (`2907a098b`, tag `battlefx-int-wip-20261012-1`, kept) and this round's 11 commits, all in Battle FX's own files (no shared game file changed this round: `git diff --stat battlefx-int-wip-20261012-1 battlefx-int-wip-20261013-1`) |
| `claude/battle-fx-unified-candidate` | the merge ready to take: `git merge --no-ff battlefx-int-wip-20261013-1` into `claude/emeraldtogether-unified` `8c8c11ab2`, the four conflicts resolved. One merge commit; its tree is the tested trial's exactly |
| `claude/battle-fx-unified-trial` | TEST ONLY - where that merge was built and tested (with the sprite pack), commit by commit; the same tree |

**What the unified game already holds** (`git merge-base --is-ancestor` against `8c8c11ab2`): the 3D battle
environments' round 4 (`aa63a98a3`, which the Battle FX branch also holds), the overworld graphics handoff
(`53bb34a63`), the animated battle sprites (preview 9's, `8706f8f6c`) and the character customization
(`059dbc4b1`). Not yet: the newest native UI, which its session handed over at the same time
(ui-native-handoff-20261013-1: `claude/native-ui` `0b7523cdb`, and its own merge candidate
`claude/native-ui-unified-candidate` `89dea9e00`, also a merge into `8c8c11ab2`).

**Taking both handoffs** (Git Bash, in an engine clone with both bundles fetched; the UI's own
COORDINATION.txt says how to fetch its bundle):

1. If `claude/emeraldtogether-unified` is still `8c8c11ab2`: `git merge --ff-only
   ui-handoff/native-ui-unified-candidate` (the UI's step), then `git merge --no-ff
   claude/battle-fx-unified-candidate` - **no conflict** (`git merge-tree`); or the other way round
   (`--ff-only` Battle FX's candidate, then the UI's: no conflict either). Both orders give the same tree,
   which is also the tree of the trial with the UI merged - built and played here (section 4).
2. On a newer tip of yours: `git merge --no-ff battlefx-int-wip-20261013-1`. Four files conflict, all
   additions on both sides - against `8c8c11ab2` and against `8c8c11ab2` with the newest UI merged the
   same four with the same hunks (the UI leaves those files as `8c8c11ab2` has them):

| File | Resolution (as in the candidate and the trial) |
| --- | --- |
| `src/diorama/gl/gl_battle.c` | in `DioramaGLBattle_Present` keep both: `BattleFxGL_DrawBack(&fxLayout)` before `DrawPicture`, then `DrawPicture`, then the animated sprites' `DioramaGLBattleSprites_Draw` (`#ifdef ENABLE_BATTLE_SPRITES`), then `BattleFxGL_DrawFront(&fxLayout)` - the effects stay in front of the Pokemon, heads above the picture included |
| `src/platform/gba_easy_draw.c` | `isbgEnabled` clears both masks: `((((REG_DISPCNT >> 8) & 0xF) & ~gPlatformPictureHideBgMask & ~gDioramaSceneHideBg) & (1 << x))` in Diorama builds (the layers the native UI and Battle FX leave out), the native UI's form otherwise |
| `Makefile_pc` | both blocks: Battle FX's QuickJS-ng / stb objects inside the `DIORAMA` block, the native UI's and the `BATTLE_SPRITES` preserved objects, each with its own `endif` |
| `src/diorama/scene/battle_env_test.c` | test builds only: both sides' includes, options and helpers |

   If those four files are still as in `8c8c11ab2` on your branch, after the merge
   `git checkout claude/battle-fx-unified-candidate -- Makefile_pc src/diorama/gl/gl_battle.c
   src/diorama/scene/battle_env_test.c src/platform/gba_easy_draw.c` takes the resolution as built and
   tested here.

**With each part of the game.**

- *Latest 3D graphics and 3D battle environments.* Battle FX draws in the 3D battle's own frame: its back
  layer after the world, its front layer after the picture and the sprites' heads; it reads the window
  through `DioramaGLTarget_ReadableFramebuffer` (one GPU-to-GPU copy a layer). The effect layers follow the
  window's resolution and the quality tier, not the 3D render scale; the policy reacts only to the game's
  frames not reaching the screen, so the frame-rate limit and VSync settings need nothing from it.
- *Animated Pokemon sprites.* Read only, through `gBattleSpriteObjViews` / `gBattleSpriteBgCopies`, in one
  file of Battle FX's own (section 1); nothing of the sprite system or the library changed. Without the
  sprite pack, or for a Pokemon it does not hold, Battle FX draws as before.
- *New native UI.* The conflicts above; the UI's battle HUD hides none of Battle FX's sprites (checked by
  the UI session, ui-native-handoff-20261013-1 section 6); the battles its newest round changed (the Safari
  Zone, Wally's catch tutorial, two trainers at once) were fought here on the combined tree with Battle FX
  drawing (below). A settings row can call `BattleFx_SetQuality` (Auto, Full, High, Medium, Low, Off).
- *Character customization.* The trainer's throws are timed by the back picture's own animation table
  (since the first integration), so the characters' throws are followed; trainers are not drawn by the
  animated sprites, so the placing does not apply to them.

**Checks after merging.** `make` (the GBA ROM) gives SHA-256 `f214735b…3387`; the test build
(`make -f Makefile_pc DIORAMA=1 CC="gcc -DDIORAMA_BATTLE_ENV_TEST -DBATTLEFX_TEST"
TARGET=pokeemerald-mp-diorama-bfxtest OBJ_DIR=build/pc-linux-diorama-bfxtest`, `NATIVE_LINUX=1` on Linux)
runs `tools/battlefx/gallery.sh` with the sprite pack (`PKMN_BATTLE_SPRITES_PACK=...`): every animation exact
(`tools/battlefx/summarize.py`), and in a battle the log says `Battle FX: renderer ready ... effects placed
for the battlers as shown`. The player build needs the `battlefx` folder beside the executable, as the
sprite pack is; its `settings.cfg` needs no change (`quality=auto`, `attach=1` are the defaults).

**Rollback.** One merge commit; `battlefx\settings.cfg` `enabled=0` (or `quality=gba`, or
`PKMN_BATTLEFX=0`) plays the original animations without a rebuild; the first integration is tag
`battlefx-int-wip-20261012-1`.

## 4. Verification

All on the cloud computer: Linux, software OpenGL (llvmpipe) on a virtual screen, two CPU cores; the
Windows builds under Wine. The test builds (`BATTLEFX_TEST`) compare the game's own effect sprites with the
core's replay in every frame of every animation - "exact" below means every compared frame equal; the
HD drawing does not enter that comparison, so it is checked separately (read back, pictures). The game's
window was 1834 x 1032 (the largest the virtual screen holds) unless a size is given. The code tested is
this branch's: the galleries and battles of A and B ran on the build of `5a0cef4dc` (the trial's
`62b7014f0`); the only code changes after it, `3f84eeea0` and `dc7fb055c`, are the auto policy's (the pace
against the clock, the last step's floor), which run with `quality=auto` only - C, the all-moves gallery and
the Megas ran on builds with the first, D and E on builds with both.

**A. The Battle FX branch, without the animated sprites** (the drawing every other build gets):

| Check | Result |
| --- | --- |
| The HD layers against the first checkpoint's renderer: both builds read their layers back (`PKMN_BFX_TEST_LAYERS`, the particles' time fixed), ten moves | 108 layers drawn at the same frames in both runs, 108 bit-identical (9 of the moves had such frames; every animation of the run exact) |
| Every battle animation (status, general, special) as the player's side | 81 played, 81 exact, none failed or crashed (the Pokeblock throw, the rock throw and the trainer's ball throw do not end at a wild battle's menu - they wait for the Safari Zone - as in the first round, where they were exact at a Safari battle's menu; the ball throw again in the Safari battle below) |
| Battles fought to their end by the autopilot, the HD layer drawn | Calvin (won, 3 of 3 exact), the gym double battle Tate & Liza (won, 11 of 11), Fabian's Manectric Mega Evolving (won, 5 of 5), the Safari Zone (caught on the second ball, 2 of 2): 21 of 21 exact |
| Battle FX off (`PKMN_BATTLEFX=0`): Roxanne fought | won; no HD animation begun, the original animations played |

**B. The unified trial with the animated sprites and the placing** (the sprite pack, read from the
session's private copy):

| Check | Result |
| --- | --- |
| Seven placing galleries, 12 moves each (section 1) | 84 of 84 animations exact; every look logged once, as in the table of section 1; pictures in `results\placement\` |
| Every move animation (621 moves, 651 animations with second turns), Wailord's back against Joltik's front, the HD layer drawn (640 x 360) | 651 played, 651 exact, none failed; the HD layer drawn in every animation (72,371 pictures); the looks followed Transform (Wailord in Joltik's shape: Joltik's back placed); Ally Switch without a partner crashed the game after its animation, as in every build (known issue 9; the gallery resumed after it) |
| Mega Rayquaza against Mega Salamence (GBA pictures: the pack has no Mega forms), the placing on and off, layers read back | 23 layers drawn at the same frames in both runs, 23 bit-identical; no look placed for either Mega (against Wailord's back, only Wailord's); Pikachu's back, smaller in the pack (40 x 40 against 58 x 55), placed at x 0.75 (the limit); six moves in each of the six runs, all exact |
| Battles fought with the sprites, the HD layer drawn | Calvin (won, 3 of 3 exact), Tate & Liza (won, 11 of 11; all four battlers placed, six looks with the Pokemon sent out later, Wailord's back among them), Fabian (won, 5 of 5: his Manectric placed as shown, then its Mega Evolution - a GBA picture - placed as before), the Safari Zone (caught with the first ball, 1 of 1): 20 of 20 exact; "renderer ready ... effects placed for the battlers as shown" logged in each |

**C. The combined tree** - the unified game, the newest native UI and Battle FX (the tree both handoffs give
together, either order; a scratch build, no branch): the trial (`27242df69`) with `claude/native-ui`
`0b7523cdb` merged (no conflict; the same tree as the UI's candidate with Battle FX merged), built (the test
build; the native UI on) and four battles fought with the sprite pack: the Safari Zone (caught with the
first ball, 1 of 1 exact) and Wally's catch tutorial (his Ralts caught, 8 of 8) on the newest UI's screens
(the GBA text boxes hidden there: the UI draws its own), Calvin and Billy at once (won, 6 of 6), Calvin
(won, 3 of 3) - 18 of 18 exact, the HD layer drawn, the battlers placed as shown. No warning in the files
the UI's merge touched or in Battle FX's.

**D. Quality.** The tiers fixed: every frame exact in all five runs (section 2). The auto policy: at 1834 x
1032 (a slide show even at `low`) down to the GBA effects; at 640 x 360 and 480 x 270 down to `low`, kept
for most moves, the heaviest parts under 45 frames a second going to their GBA effects (3 and 1 of 6
animations); Calvin's battle at 640 x 360 at `low` throughout its Flamethrower, nothing to the GBA effects
(section 2). Built from `dc7fb055c`.

**E. Windows (under Wine, software OpenGL).** The Windows test build (the benchmark's), borderless on the
virtual screen: six moves (Flamethrower, Thunderbolt, Surf, Earthquake, Protect, Bolt Strike) 6 of 6 exact,
the HD layer drawn in all six. The Windows player build (windowed 640 x 360): starts, loads the core (720
ms) and runs at the title until the 45 seconds' time limit stops it, as intended. Both built from
`dc7fb055c`.

**F. Merges and the branch.**

- `git merge-tree` of the tag into `8c8c11ab2`: `Makefile_pc`, `src/diorama/gl/gl_battle.c`,
  `src/diorama/scene/battle_env_test.c`, `src/platform/gba_easy_draw.c` (section 3). Into the UI's
  candidate `89dea9e00`: the same four. The candidate's tree equals the one-shot merge with the trial's
  four files and the trial's own tree; the UI's candidate and Battle FX's merge into each other without a
  conflict in either order, to the same tree as the trial with the UI merged (the trees: COORDINATION.txt).
- The GBA ROM built from the tag: SHA-256 `f214735b…3387`, byte-identical (the launcher's pin).
- Builds: Linux test and player, Windows test and player (the branch); Linux test (the trial, the combined
  tree). No warning in Battle FX's files (the player build's full rebuild warns in eight game files, as it
  did before).
- `git diff --stat battlefx-int-wip-20261012-1 battlefx-int-wip-20261013-1`: 12 files, +1732 -124, all
  Battle FX's own (its four headers, its sources in `src/diorama/battlefx/` and `gl_battle_fx.c`, its
  README, `settings.cfg`, its data tool and the settings template; COORDINATION.txt section 1 lists them),
  and the two report files. Damage, accuracy, critical hits, abilities, move effects, stats, status, Mega
  Evolution, turn order, outcomes, saves and multiplayer code: untouched.

**A correction to the first checkpoint (battlefx-int-wip-20261012-1).** In its battles fought (not in the
galleries) the test build did not draw the HD layer: a test-driver setting was read after an early return
(test builds only; fixed in `5a0cef4dc`). The pictures from fought battles in its `results\pictures`
therefore show the GBA effects, and the HD layer was checked over galleries only. Its parity results stand
(the comparison does not depend on the drawing). This round's fought battles draw the HD layer (above).

## 5. Known issues and limits

New or changed in this round:

1. **The placing follows bodies, not mouths and hands.** The sprite library gives each facing its frame and a
   feet point, no mouth or hand points; the effects are moved and scaled with the shown body's box. A
   projectile therefore starts from the corresponding place on the shown body (for Wailord's back: its upper
   body), which matches the GBA's intent (the GBA's own projectiles start from the box's centre) but is not
   aimed at a mouth.
2. **One scale for both axes, held to 0.75 .. 1.6.** Bodies much wider than tall (or the reverse) against
   their GBA picture are approximated: effects keep their shapes. 0.3 % of the pack's facings are larger
   than 1.6 (Wailord's back, its GBA picture cut by the text box: 2.2) and 4.3 % smaller than 0.75; they
   get the limit.
3. **Big elements are placed less** (from 96 GBA pixels, not at all from 176): screen-wide backgrounds and
   flashes stay where the GBA puts them; a large vortex around a small Pokemon is moved less than its
   centre would ask.
4. **A battler on a background layer** (a script's "monbg") keeps the look measured while it was a sprite;
   its own HD glow / flash / light is not drawn while it is there (as with the GBA pictures before: the
   game's copy on the layer is not an OAM entry). The effects around it are placed.
5. **A new look settles over a second**: the shown body's box is a running mean over the look's frames
   (64 pictures), so right after a switch or a form change the placing follows the first frames more
   closely, then the average.
6. **Lower tiers draw at lower resolution**: at medium and low the battler's own flash and light are drawn
   at half resolution too (softer edges while a flash shows); the effects themselves are mostly soft
   (particles, glows) and scale well.
7. **The auto policy waits for evidence**: on a PC that cannot keep up it lowers the tier after half a second
   of game frames with the HD layers (a short stutter first; more on a PC that shows only a few pictures a
   second), and a trial may show a lower tier for half a second before it is undone. Without GPU timer
   queries (part of OpenGL 3.3, so a driver without them is rare) it measures the CPU's time, which
   understates the GPU's, so it lowers less readily. Exercised here only on software OpenGL; raising a
   tier again at a later battle's start is not exercised (the test runs play one battle each).
8. **Not Battle FX's, seen while testing** (COORDINATION.txt section 3): an animated sprite that reaches
   the GBA picture's left or right edge (Lugia's back wing, a large foe in a double battle) has its edge
   pixel row carried out across the world as a dark line (`gl_battle.c` `DrawCarriedOut`); Battle FX's
   layers are empty there (read back and checked).

Unchanged from the first integration (its report has the details):

9. **Ally Switch** without a partner crashes the native game (the GBA reads `gSprites[255]`); only the
   gallery forces it. In a double battle it is exact.
10. **One HD animation at a time** (a double battle's opening send-outs and a shiny send-out's overlap play
    as GBA).
11. **Contests** are not drawn by Battle FX.
12. **The core's cost** (QuickJS interprets the package's JavaScript): a begin 43 ms median on its own
    thread, a frame about 1.75 ms; the game never waits for it; an animation it falls more than 6 frames
    behind on plays as GBA from there.
13. **What the parity check sees**: effect sprites (graphics tag and screen position) every frame and the
    animations' lengths; palette blends, BG layers and battler offsets are not compared.
14. **Not in a fought battle**: the player's own Mega Evolution (the autopilot only presses A), a Pokeblock
    throw, a voluntary switch, Z-Moves / Tera (gallery only), a two-player battle.
15. **Test builds exit from the game's thread** (`exit()` in the test drivers while the main thread may be
    drawing; it crashed once in an earlier run, results written first). Test builds only.
16. **Round 4's multiplayer freeze** (the battle backdrop reading a trainer class for the other player)
    is on the Battle FX branch itself, which holds round 4 as it was; the unified game has the fix
    (`battle_capture.c` `BackdropEnvironment`), so the merged game does not freeze there.

## 6. This checkpoint

Folder `battlefx-int-wip-20261013-1` (in your private checkpoints folder). No playable build: the next
playable game with Battle FX is the main session's unified build.

| File | |
| --- | --- |
| `engine-battle-fx-integration.bundle` | `claude/battle-fx-integration`, `claude/battle-fx-unified-candidate`, `claude/battle-fx-unified-trial`, tag `battlefx-int-wip-20261013-1`. Needs `2907a098b` (battlefx-int-wip-20261012-1's bundle) and `8c8c11ab2` (emeraldtogether-unified-20261013-1's) restored first |
| `INTEGRATION_REPORT.md` | this report (also on the branch: `src/diorama/battlefx/INTEGRATION_REPORT.md`) |
| `COORDINATION.txt` | for the main EmeraldTogether session and the other sessions |
| `benchmark\run-battlefx-benchmark.bat`, `benchmark\saves\battle-test.sav` | the graphics card measurement (section 2, "What to measure on your PC") and the battle sessions' private test save |
| `test-run\`, `test-run.zip` | the Windows Battle FX test build the benchmark runs (a measuring tool with a test driver, not a game to play), its `battlefx` folder and `SDL2.dll`; the zip is the same folder |
| `debug-symbols\` | the test build's debug symbols (xz) |
| `results\` | the runs' summaries (`*.md`, `*.json`); `placement\` (the effects placed for the battlers as shown, left, against as for the GBA pictures, right); `tiers\` (one frame at each quality tier); `pictures\` (the battles fought with the sprites, on the trial and on the combined tree: the middle capture of each animation) |
| `SHA256SUMS.txt` | checksums of every file here |

Restore (Git Bash, in a clone that has battlefx-int-wip-20261012-1 and emeraldtogether-unified-20261013-1
restored, with none of these branches checked out):
`git fetch <path>\engine-battle-fx-integration.bundle "refs/heads/*:refs/heads/*" "refs/tags/*:refs/tags/*"`

Rollback: nothing here changes a game you play. The first integration is tag `battlefx-int-wip-20261012-1`
(its checkpoint folder untouched); in a build with Battle FX, `battlefx\settings.cfg` `enabled=0` (or
`quality=gba`) plays the original animations without a rebuild.

## Sources

- Unreal Engine documentation, "Dynamic Resolution":
  https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-resolution-in-unreal-engine
- Unreal Engine 4.27 documentation, "Dynamic Resolution" (console variables and defaults):
  https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-resolution?application_version=4.27
